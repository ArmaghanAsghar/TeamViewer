#include "SessionWorker.hpp"

#include "peerdesk/auth.hpp"
#include "peerdesk/codec.hpp"
#include "peerdesk/protocol.hpp"

#include <QtCore/QDateTime>

#include <span>
#include <vector>

namespace peerdesk {

SessionWorker::SessionWorker(QObject* parent) : QObject(parent) {}

void SessionWorker::connectToHost(QString host, quint16 port, QString username, QString password,
                                  QString caFile) {
    stop_ = false;
    emit statusChanged("Connecting over TLS…");
    std::string err;
    auto c = TlsConn::connect(host.toStdString(), port, caFile.toStdString(), err);
    if (!c) {
        emit unreachable(QString::fromStdString(err));
        return;
    }
    conn_ = std::move(*c);

    if (!conn_.send(env_hello(username.toStdString()))) {
        emit unreachable("Connection dropped during hello");
        conn_.close();
        return;
    }

    proto::Envelope env;
    if (!conn_.recv(env, 8000) || !env.has_auth_challenge()) {
        if (env.has_auth_fail()) {
            emit authFailed(QString::fromUtf8(auth_fail_text(env.auth_fail().reason())));
        } else {
            emit unreachable("Host did not start authentication");
        }
        conn_.close();
        return;
    }
    const auto parsed = challenge_from_bytes(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(env.auth_challenge().salt().data()),
                                 env.auth_challenge().salt().size()),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(env.auth_challenge().nonce().data()),
                                 env.auth_challenge().nonce().size()),
        env.auth_challenge().t_cost(), env.auth_challenge().m_cost(),
        env.auth_challenge().parallelism());
    if (!parsed) {
        emit unreachable("Bad auth challenge");
        conn_.close();
        return;
    }
    emit statusChanged("Proving password (Argon2id + HMAC, password stays off the wire)…");
    const auto resp = make_auth_response(password.toStdString(), *parsed);
    if (!conn_.send(env_auth_response(resp.hmac))) {
        emit unreachable("Connection dropped during auth");
        conn_.close();
        return;
    }
    if (!conn_.recv(env, 15000)) {
        emit unreachable("Host timed out during auth");
        conn_.close();
        return;
    }
    if (env.has_auth_fail()) {
        emit authFailed(QString::fromUtf8(auth_fail_text(env.auth_fail().reason())));
        conn_.close();
        return;
    }
    if (!env.has_auth_ok()) {
        emit unreachable("Unexpected host reply after auth");
        conn_.close();
        return;
    }
    emit sessionReady(static_cast<int>(env.auth_ok().width()),
                      static_cast<int>(env.auth_ok().height()));
    pump();
}

void SessionWorker::enqueueMouse(quint8 action, quint8 button, quint16 x, quint16 y, qint16 wheel) {
    std::lock_guard<std::mutex> g(mu_);
    mice_.push(MouseEvent{static_cast<MouseAction>(action), button, x, y, wheel});
}

void SessionWorker::enqueueKey(quint8 down, quint32 keysym) {
    std::lock_guard<std::mutex> g(mu_);
    keys_.push(KeyEvent{down, keysym});
}

void SessionWorker::disconnectSession() {
    stop_ = true;
    if (conn_.is_open()) conn_.send(env_disconnect());
}

void SessionWorker::flush_input() {
    std::queue<MouseEvent> m;
    std::queue<KeyEvent> k;
    {
        std::lock_guard<std::mutex> g(mu_);
        m.swap(mice_);
        k.swap(keys_);
    }
    while (!m.empty()) {
        if (!conn_.send(env_mouse(m.front()))) return;
        m.pop();
    }
    while (!k.empty()) {
        if (!conn_.send(env_key(k.front()))) return;
        k.pop();
    }
}

void SessionWorker::pump() {
    H264Decoder dec;
    std::string derr;
    if (!dec.open(derr)) {
        emit sessionEnded(QString::fromStdString(derr));
        conn_.close();
        return;
    }
    auto last_ping = QDateTime::currentMSecsSinceEpoch();
    while (!stop_ && conn_.is_open()) {
        flush_input();
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (now - last_ping > 2000) {
            conn_.send(env_ping());
            last_ping = now;
        }
        proto::Envelope env;
        if (!conn_.recv(env, 40)) {
            if (!conn_.is_open()) break;
            continue;
        }
        if (env.has_video()) {
            const auto& v = env.video();
            std::vector<uint8_t> rgb;
            int w = 0, h = 0;
            std::string e;
            const auto bytes = std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(v.h264().data()), v.h264().size());
            if (!dec.decode_to_rgb(bytes, rgb, w, h, e) || rgb.empty()) continue;
            QImage img(rgb.data(), w, h, w * 3, QImage::Format_RGB888);
            emit frameArrived(img.copy());
            continue;
        }
        if (env.has_pong()) continue;
        if (env.has_error()) {
            emit sessionEnded(QString::fromStdString(env.error().message()));
            conn_.close();
            return;
        }
        if (env.has_disconnect()) break;
    }
    conn_.close();
    emit sessionEnded(stop_ ? "Disconnected" : "Connection dropped");
}

}  // namespace peerdesk
