#include "host_server.hpp"

#include "peerdesk/auth.hpp"
#include "peerdesk/cert.hpp"
#include "peerdesk/codec.hpp"
#include "peerdesk/protocol.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <thread>

namespace peerdesk {
namespace {

std::string mouse_line(const MouseEvent& e) {
    std::string a = "MOVE";
    if (e.action == MouseAction::Down) a = "DOWN";
    if (e.action == MouseAction::Up) a = "UP";
    if (e.action == MouseAction::Wheel) a = "WHEEL";
    return a + " " + std::to_string(e.x) + "," + std::to_string(e.y);
}

}  // namespace

HostServer::HostServer(Config cfg) : cfg_(std::move(cfg)) {}

std::filesystem::path HostServer::cert_path() const { return cfg_.data_dir / "server.crt"; }

bool HostServer::setup(std::string& err) {
    if (cfg_.data_dir.empty()) {
        const char* home = std::getenv("HOME");
        cfg_.data_dir = home ? std::filesystem::path(home) / ".peerdesk" : std::filesystem::path(".peerdesk");
    }
    std::error_code ec;
    std::filesystem::create_directories(cfg_.data_dir, ec);

    const auto users_path = cfg_.data_dir / "users";
    std::string load_err;
    if (!users_.load(users_path, load_err)) {
        if (!cfg_.bootstrap_user.empty() && !cfg_.bootstrap_password.empty()) {
            if (!users_.upsert(cfg_.bootstrap_user, cfg_.bootstrap_password) ||
                !users_.save(users_path, err)) {
                err = "Could not create host login";
                return false;
            }
        } else {
            err = "No credential configured";
            return false;
        }
    }

    if (!random_bytes(fake_secret_)) {
        err = "RNG failed";
        return false;
    }

    const bool wayland = [] {
        const char* s = std::getenv("XDG_SESSION_TYPE");
        return s && std::string(s) == "wayland";
    }();
    if (wayland && !cfg_.synthetic) {
        err = "X11 required for v1 (Wayland host is D1). Use --synthetic for tests.";
        return false;
    }

    auto source = cfg_.synthetic
                      ? std::unique_ptr<ScreenSource>(new SyntheticCapture())
                      : std::unique_ptr<ScreenSource>(new X11Capture());
    if (!source->open(err)) return false;
    source.reset();

    if (cfg_.inject && !cfg_.synthetic) {
        InputInject probe;
        if (!probe.open(err)) return false;
    }

    const auto key = cfg_.data_dir / "server.key";
    const auto crt = cfg_.data_dir / "server.crt";
    if (!ensure_self_signed_cert(key, crt, err)) return false;
    if (!listener_.listen_on(cfg_.bind, cfg_.port, crt, key, err)) return false;
    return true;
}

void HostServer::request_stop() { stop_ = true; }

void HostServer::run() {
    std::thread session_th;
    while (!stop_) {
        std::string err;
        auto conn = listener_.accept_one(250, err);
        if (!conn) continue;
        if (session_th.joinable() && !session_active_) session_th.join();
        if (session_active_) {
            handle_client(std::move(*conn));
            continue;
        }
        if (session_th.joinable()) session_th.join();
        session_th = std::thread([this, c = std::move(*conn)]() mutable { handle_client(std::move(c)); });
    }
    if (session_th.joinable()) session_th.join();
}

bool HostServer::handshake(TlsConn& conn, std::string& err) {
    proto::Envelope env;
    if (!conn.recv(env, 8000) || !env.has_hello()) {
        err = "Expected hello";
        conn.send(env_auth_fail(proto::AUTH_FAIL_PROTOCOL));
        return false;
    }
    const auto& hello = env.hello();
    if (hello.version() != kProtocolVersion || hello.username().empty()) {
        conn.send(env_auth_fail(proto::AUTH_FAIL_PROTOCOL));
        err = "Bad hello";
        return false;
    }

    AuthChallenge ch;
    ch.t_cost = kArgonT;
    ch.m_cost = kArgonM;
    ch.parallelism = kArgonP;
    const auto user = users_.find(hello.username());
    if (user) {
        ch.salt = user->salt;
        ch.t_cost = user->t_cost;
        ch.m_cost = user->m_cost;
        ch.parallelism = user->parallelism;
    } else {
        const auto fake = hmac_sha256(
            fake_secret_, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(hello.username().data()),
                                                   hello.username().size()));
        std::copy(fake.begin(), fake.begin() + 16, ch.salt.begin());
    }
    if (!random_bytes(ch.nonce)) {
        err = "RNG failed";
        return false;
    }
    if (!conn.send(env_challenge(ch.salt, ch.nonce, ch.t_cost, ch.m_cost, ch.parallelism))) {
        err = "Send challenge failed";
        return false;
    }
    if (!conn.recv(env, 15000) || !env.has_auth_response()) {
        err = "Expected auth response";
        return false;
    }
    const auto& pr = env.auth_response().hmac();
    if (pr.size() != 32) {
        conn.send(env_auth_fail(proto::AUTH_FAIL_PROTOCOL));
        return false;
    }
    AuthResponse resp;
    std::copy(pr.begin(), pr.end(), resp.hmac.begin());

    const bool creds_ok = user && verify_auth_response(user->hash, ch, resp);
    if (!creds_ok) {
        conn.send(env_auth_fail(proto::AUTH_FAIL_BAD_CREDENTIALS));
        err = "Authentication failed";
        return false;
    }
    if (session_active_.exchange(true)) {
        conn.send(env_auth_fail(proto::AUTH_FAIL_SESSION_BUSY));
        err = "Host already has a viewer";
        session_active_ = true;
        return false;
    }

    std::unique_ptr<ScreenSource> source = cfg_.synthetic
                                               ? std::unique_ptr<ScreenSource>(new SyntheticCapture())
                                               : std::unique_ptr<ScreenSource>(new X11Capture());
    if (!source->open(err)) {
        session_active_ = false;
        conn.send(env_error(err));
        return false;
    }
    if (!conn.send(env_auth_ok(static_cast<uint32_t>(source->width()),
                               static_cast<uint32_t>(source->height())))) {
        session_active_ = false;
        err = "Send auth-ok failed";
        return false;
    }

    InputInject inject;
    InputInject* inj = nullptr;
    if (cfg_.inject) {
        std::string ierr;
        if (inject.open(ierr)) {
            inj = &inject;
        } else if (!cfg_.synthetic) {
            session_active_ = false;
            conn.send(env_error(ierr));
            err = ierr;
            return false;
        }
    }
    session_loop(conn, *source, inj);
    session_active_ = false;
    return true;
}

void HostServer::handle_client(TlsConn conn) {
    std::string err;
    if (!handshake(conn, err) && !err.empty()) {
        std::cerr << "peerdesk-server: session rejected: " << err << "\n";
    }
}

void HostServer::session_loop(TlsConn& conn, ScreenSource& source, InputInject* inject) {
    std::atomic<bool> live{true};
    std::mutex send_mu;
    std::thread cap([&] {
        H264Encoder enc;
        std::string enc_err;
        if (!enc.open(source.width() & ~1, source.height() & ~1, std::max(cfg_.fps, 1), enc_err)) {
            std::lock_guard<std::mutex> g(send_mu);
            conn.send(env_error(enc_err.empty() ? "Encode failed" : enc_err));
            live = false;
            return;
        }
        std::vector<uint8_t> rgb;
        std::string annexb;
        const auto period = std::chrono::milliseconds(std::max(1000 / std::max(cfg_.fps, 1), 16));
        while (live && !stop_) {
            std::string err;
            if (!source.grab_rgb(rgb, err)) {
                std::lock_guard<std::mutex> g(send_mu);
                conn.send(env_error(err.empty() ? "Capture failed" : err));
                live = false;
                break;
            }
            if (!enc.encode_rgb(rgb, annexb, err)) {
                std::lock_guard<std::mutex> g(send_mu);
                conn.send(env_error(err.empty() ? "Encode failed" : err));
                live = false;
                break;
            }
            if (!annexb.empty()) {
                std::lock_guard<std::mutex> g(send_mu);
                if (!conn.send(env_video(static_cast<uint32_t>(source.width()),
                                         static_cast<uint32_t>(source.height()), annexb))) {
                    live = false;
                    break;
                }
            }
            std::this_thread::sleep_for(period);
        }
    });

    auto last_rx = std::chrono::steady_clock::now();
    while (live && !stop_) {
        proto::Envelope env;
        if (!conn.recv(env, 250)) {
            if (!conn.is_open()) {
                live = false;
                break;
            }
            if (std::chrono::steady_clock::now() - last_rx > std::chrono::seconds(8)) {
                live = false;
                break;
            }
            continue;
        }
        last_rx = std::chrono::steady_clock::now();
        if (env.has_disconnect() || env.has_error()) {
            live = false;
            break;
        }
        if (env.has_ping()) {
            std::lock_guard<std::mutex> g(send_mu);
            conn.send(env_pong());
            continue;
        }
        if (env.has_mouse()) {
            MouseEvent e;
            if (mouse_from_proto(env.mouse(), e)) {
                source.note_input(mouse_line(e));
                if (inject) inject->apply_mouse(e);
            }
            continue;
        }
        if (env.has_key()) {
            KeyEvent e;
            if (key_from_proto(env.key(), e)) {
                source.note_input(std::string("KEY ") + (e.down ? "DOWN " : "UP ") +
                                  std::to_string(e.keysym));
                if (inject) inject->apply_key(e);
            }
        }
    }
    live = false;
    if (cap.joinable()) cap.join();
}

}  // namespace peerdesk
