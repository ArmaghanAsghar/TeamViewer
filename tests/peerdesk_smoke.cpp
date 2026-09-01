#include "host_server.hpp"

#include "peerdesk/auth.hpp"
#include "peerdesk/codec.hpp"
#include "peerdesk/map.hpp"
#include "peerdesk/net.hpp"
#include "peerdesk/protocol.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <thread>
#include <unistd.h>

namespace {

int g_fails = 0;

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        ++g_fails;
    } else {
        std::cout << "ok  " << msg << "\n";
    }
}

void test_map() {
    expect(peerdesk::map_coord(50, 100, 1920) == 960, "map_coord mid");
    expect(peerdesk::map_coord(0, 100, 1920) == 0, "map_coord origin");
    expect(peerdesk::map_coord(99, 100, 1920) == 1900, "map_coord near end");
    const auto box = peerdesk::fit_letterbox(800, 800, 1600, 900);
    expect(box.dest_w > 0 && box.dest_h > 0, "letterbox size");
    int hx = 0, hy = 0;
    expect(peerdesk::map_letterbox_point(box.dest_x, box.dest_y, box, 1600, 900, hx, hy) && hx == 0 &&
               hy == 0,
           "letterbox origin");
}

void test_protocol() {
    const auto hello = peerdesk::env_hello("jordan");
    expect(hello.has_hello() && hello.hello().username() == "jordan" &&
               hello.hello().version() == peerdesk::kProtocolVersion,
           "hello envelope");
    peerdesk::MouseEvent m{peerdesk::MouseAction::Down, 1, 100, 200, 0};
    const auto me = peerdesk::env_mouse(m);
    peerdesk::MouseEvent back;
    expect(me.has_mouse() && peerdesk::mouse_from_proto(me.mouse(), back) && back.x == 100 &&
               back.action == peerdesk::MouseAction::Down,
           "mouse roundtrip");
}

void test_auth() {
    peerdesk::AuthChallenge ch;
    ch.t_cost = peerdesk::kArgonT;
    ch.m_cost = peerdesk::kArgonM;
    ch.parallelism = peerdesk::kArgonP;
    expect(peerdesk::random_bytes(ch.salt), "salt rng");
    expect(peerdesk::random_bytes(ch.nonce), "nonce rng");
    const auto hash = peerdesk::argon2id_raw("peerdesk", ch.salt, ch.t_cost, ch.m_cost, ch.parallelism);
    const auto good = peerdesk::make_auth_response("peerdesk", ch);
    const auto bad = peerdesk::make_auth_response("nope", ch);
    expect(peerdesk::verify_auth_response(hash, ch, good), "hmac accepts good password");
    expect(!peerdesk::verify_auth_response(hash, ch, bad), "hmac rejects bad password");
}

void test_h264() {
    peerdesk::H264Encoder enc;
    peerdesk::H264Decoder dec;
    std::string err;
    expect(enc.open(64, 64, 10, err), "h264 encoder open");
    expect(dec.open(err), "h264 decoder open");
    std::vector<uint8_t> rgb(64 * 64 * 3, 40);
    for (size_t i = 0; i < rgb.size(); i += 3) rgb[i] = 200;
    std::string annexb;
    bool got = false;
    for (int i = 0; i < 8 && !got; ++i) {
        expect(enc.encode_rgb(rgb, annexb, err), "h264 encode");
        if (annexb.empty()) continue;
        std::vector<uint8_t> out;
        int w = 0, h = 0;
        got = dec.decode_to_rgb(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(annexb.data()),
                                                         annexb.size()),
                                out, w, h, err) &&
              w == 64 && h == 64 && out.size() == rgb.size();
    }
    expect(got, "h264 roundtrip");
}

bool client_hello_auth(peerdesk::TlsConn& c, const std::string& user, const std::string& pass,
                       peerdesk::proto::Envelope& env) {
    if (!c.send(peerdesk::env_hello(user))) return false;
    if (!c.recv(env, 8000) || !env.has_auth_challenge()) return false;
    const auto ch = peerdesk::challenge_from_bytes(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(env.auth_challenge().salt().data()),
                                 env.auth_challenge().salt().size()),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(env.auth_challenge().nonce().data()),
                                 env.auth_challenge().nonce().size()),
        env.auth_challenge().t_cost(), env.auth_challenge().m_cost(),
        env.auth_challenge().parallelism());
    if (!ch) return false;
    const auto resp = peerdesk::make_auth_response(pass, *ch);
    if (!c.send(peerdesk::env_auth_response(resp.hmac))) return false;
    return c.recv(env, 15000);
}

void test_session() {
    const auto dir =
        std::filesystem::temp_directory_path() / ("peerdesk-smoke-" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);

    peerdesk::HostServer::Config cfg;
    cfg.port = 0;
    cfg.bind = "127.0.0.1";
    cfg.synthetic = true;
    cfg.inject = false;
    cfg.data_dir = dir;
    cfg.bootstrap_user = "jordan";
    cfg.bootstrap_password = "peerdesk";
    cfg.fps = 8;

    peerdesk::HostServer server(cfg);
    std::string err;
    expect(server.setup(err), "server setup");
    if (!err.empty() && !server.is_listening()) {
        std::cerr << "setup: " << err << "\n";
        return;
    }
    const auto port = server.port();
    expect(port != 0, "ephemeral port");
    const auto ca = server.cert_path();
    std::thread th([&] { server.run(); });

    peerdesk::proto::Envelope env;
    {
        std::string e;
        auto c = peerdesk::TlsConn::connect("127.0.0.1", port, ca, e);
        expect(c.has_value(), "tls connect (bad-pass path)");
        if (c) {
            expect(client_hello_auth(*c, "jordan", "wrong", env), "auth exchange (bad)");
            expect(env.has_auth_fail(), "wrong password -> AuthFail");
            expect(env.auth_fail().reason() == peerdesk::proto::AUTH_FAIL_BAD_CREDENTIALS,
                   "AuthFail is bad-credentials");
        }
    }

    std::string e;
    auto live = peerdesk::TlsConn::connect("127.0.0.1", port, ca, e);
    expect(live.has_value(), "tls connect (good)");
    expect(live && client_hello_auth(*live, "jordan", "peerdesk", env), "auth exchange (good)");
    expect(env.has_auth_ok(), "good password -> AuthOk");
    expect(env.auth_ok().width() >= 320 && env.auth_ok().height() >= 200, "auth-ok has host size");

    bool got_frame = false;
    for (int i = 0; i < 80 && live; ++i) {
        if (!live->recv(env, 500)) continue;
        if (env.has_video() && !env.video().h264().empty()) {
            got_frame = true;
            break;
        }
    }
    expect(got_frame, "received an H.264 video frame");

    auto busy = peerdesk::TlsConn::connect("127.0.0.1", port, ca, e);
    expect(busy.has_value(), "second viewer can reach host");
    if (busy) {
        expect(client_hello_auth(*busy, "jordan", "peerdesk", env), "second viewer auth");
        expect(env.has_auth_fail(), "second viewer rejected");
        expect(env.auth_fail().reason() == peerdesk::proto::AUTH_FAIL_SESSION_BUSY, "busy reason");
        busy->close();
    }

    if (live) {
        live->send(peerdesk::env_disconnect());
        live->close();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    auto again = peerdesk::TlsConn::connect("127.0.0.1", port, ca, e);
    expect(again.has_value(), "reconnect tls");
    expect(again && client_hello_auth(*again, "jordan", "peerdesk", env), "reconnect auth");
    expect(env.has_auth_ok(), "reconnect without restarting server");
    server.request_stop();
    if (again) again->close();
    th.join();
    std::filesystem::remove_all(dir);
}

void test_tls_requires_ca() {
    std::string e;
    auto c = peerdesk::TlsConn::connect("127.0.0.1", 1, {}, e);
    expect(!c && e.find("PEERDESK_CA_FILE") != std::string::npos, "connect without CA is refused");
}

}  // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    test_map();
    test_protocol();
    test_auth();
    test_h264();
    test_tls_requires_ca();
    test_session();
    if (g_fails) {
        std::cerr << g_fails << " check(s) failed\n";
        return 1;
    }
    std::cout << "All PeerDesk smoke checks passed (J0–J3, Protobuf, H.264, pinned TLS).\n";
    return 0;
}
