#pragma once

#include "capture.hpp"
#include "inject.hpp"
#include "users.hpp"

#include "peerdesk/net.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace peerdesk {

class HostServer {
public:
    struct Config {
        std::string bind = "0.0.0.0";
        uint16_t port = kDefaultPort;
        std::filesystem::path data_dir;
        bool synthetic = false;
        bool inject = true;
        int fps = 15;
        std::string bootstrap_user = "jordan";
        std::string bootstrap_password = "peerdesk";
    };

    explicit HostServer(Config cfg);
    bool setup(std::string& err);
    void run();
    void request_stop();
    uint16_t port() const { return listener_.port(); }
    bool is_listening() const { return listener_.is_open(); }
    std::filesystem::path cert_path() const;

private:
    void handle_client(TlsConn conn);
    bool handshake(TlsConn& conn, std::string& err);
    void session_loop(TlsConn& conn, ScreenSource& source, InputInject* inject);

    Config cfg_;
    UserStore users_;
    TlsListener listener_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> session_active_{false};
    std::array<uint8_t, 32> fake_secret_{};
};

}  // namespace peerdesk
