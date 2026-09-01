#pragma once

#include "peerdesk.pb.h"

#include <array>
#include <cstdint>
#include <string>

namespace peerdesk {

inline constexpr uint32_t kProtocolVersion = 1;
inline constexpr uint32_t kMaxPayload = 8 * 1024 * 1024;
inline constexpr uint16_t kDefaultPort = 4473;

enum class MouseAction : uint8_t { Move = 1, Down = 2, Up = 3, Wheel = 4 };

struct MouseEvent {
    MouseAction action = MouseAction::Move;
    uint8_t button = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    int16_t wheel_delta = 0;
};

struct KeyEvent {
    uint8_t down = 0;
    uint32_t keysym = 0;
};

inline const char* auth_fail_text(proto::AuthFailReason r) {
    switch (r) {
        case proto::AUTH_FAIL_BAD_CREDENTIALS:
            return "Authentication failed";
        case proto::AUTH_FAIL_SESSION_BUSY:
            return "Host already has a viewer";
        case proto::AUTH_FAIL_PROTOCOL:
            return "Protocol error";
        default:
            return "Authentication failed";
    }
}

proto::Envelope env_hello(const std::string& username);
proto::Envelope env_challenge(const std::array<uint8_t, 16>& salt,
                              const std::array<uint8_t, 32>& nonce, uint32_t t, uint32_t m,
                              uint32_t p);
proto::Envelope env_auth_response(const std::array<uint8_t, 32>& hmac);
proto::Envelope env_auth_ok(uint32_t width, uint32_t height);
proto::Envelope env_auth_fail(proto::AuthFailReason reason);
proto::Envelope env_video(uint32_t width, uint32_t height, const std::string& h264);
proto::Envelope env_mouse(const MouseEvent& e);
proto::Envelope env_key(const KeyEvent& e);
proto::Envelope env_ping();
proto::Envelope env_pong();
proto::Envelope env_disconnect();
proto::Envelope env_error(const std::string& msg);

bool mouse_from_proto(const proto::MouseEvent& p, MouseEvent& out);
bool key_from_proto(const proto::KeyEvent& p, KeyEvent& out);

}  // namespace peerdesk
