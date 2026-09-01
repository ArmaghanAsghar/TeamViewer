#include "peerdesk/protocol.hpp"

namespace peerdesk {

proto::Envelope env_hello(const std::string& username) {
    proto::Envelope e;
    auto* h = e.mutable_hello();
    h->set_version(kProtocolVersion);
    h->set_username(username);
    return e;
}

proto::Envelope env_challenge(const std::array<uint8_t, 16>& salt,
                              const std::array<uint8_t, 32>& nonce, uint32_t t, uint32_t m,
                              uint32_t p) {
    proto::Envelope e;
    auto* c = e.mutable_auth_challenge();
    c->set_salt(salt.data(), salt.size());
    c->set_nonce(nonce.data(), nonce.size());
    c->set_t_cost(t);
    c->set_m_cost(m);
    c->set_parallelism(p);
    return e;
}

proto::Envelope env_auth_response(const std::array<uint8_t, 32>& hmac) {
    proto::Envelope e;
    e.mutable_auth_response()->set_hmac(hmac.data(), hmac.size());
    return e;
}

proto::Envelope env_auth_ok(uint32_t width, uint32_t height) {
    proto::Envelope e;
    e.mutable_auth_ok()->set_width(width);
    e.mutable_auth_ok()->set_height(height);
    return e;
}

proto::Envelope env_auth_fail(proto::AuthFailReason reason) {
    proto::Envelope e;
    e.mutable_auth_fail()->set_reason(reason);
    return e;
}

proto::Envelope env_video(uint32_t width, uint32_t height, const std::string& h264) {
    proto::Envelope e;
    auto* v = e.mutable_video();
    v->set_width(width);
    v->set_height(height);
    v->set_h264(h264);
    return e;
}

proto::Envelope env_mouse(const MouseEvent& ev) {
    proto::Envelope e;
    auto* m = e.mutable_mouse();
    m->set_action(static_cast<proto::MouseAction>(ev.action));
    m->set_button(ev.button);
    m->set_x(ev.x);
    m->set_y(ev.y);
    m->set_wheel_delta(ev.wheel_delta);
    return e;
}

proto::Envelope env_key(const KeyEvent& ev) {
    proto::Envelope e;
    auto* k = e.mutable_key();
    k->set_down(ev.down != 0);
    k->set_keysym(ev.keysym);
    return e;
}

proto::Envelope env_ping() {
    proto::Envelope e;
    e.mutable_ping();
    return e;
}

proto::Envelope env_pong() {
    proto::Envelope e;
    e.mutable_pong();
    return e;
}

proto::Envelope env_disconnect() {
    proto::Envelope e;
    e.mutable_disconnect();
    return e;
}

proto::Envelope env_error(const std::string& msg) {
    proto::Envelope e;
    e.mutable_error()->set_message(msg);
    return e;
}

bool mouse_from_proto(const proto::MouseEvent& p, MouseEvent& out) {
    if (p.action() == proto::MOUSE_UNSPECIFIED) return false;
    out.action = static_cast<MouseAction>(p.action());
    out.button = static_cast<uint8_t>(p.button());
    out.x = static_cast<uint16_t>(p.x());
    out.y = static_cast<uint16_t>(p.y());
    out.wheel_delta = static_cast<int16_t>(p.wheel_delta());
    return true;
}

bool key_from_proto(const proto::KeyEvent& p, KeyEvent& out) {
    out.down = p.down() ? 1 : 0;
    out.keysym = p.keysym();
    return true;
}

}  // namespace peerdesk
