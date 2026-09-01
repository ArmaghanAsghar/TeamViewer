#include "host_server.hpp"

#include "peerdesk/cert.hpp"

#include <csignal>
#include <iostream>

namespace {
peerdesk::HostServer* g_server = nullptr;
void on_sig(int) {
    if (g_server) g_server->request_stop();
}
}  // namespace

int main(int argc, char** argv) {
    peerdesk::HostServer::Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--port") {
            cfg.port = static_cast<uint16_t>(std::stoi(need("--port")));
        } else if (a == "--bind") {
            cfg.bind = need("--bind");
        } else if (a == "--data-dir") {
            cfg.data_dir = need("--data-dir");
        } else if (a == "--user") {
            cfg.bootstrap_user = need("--user");
        } else if (a == "--password") {
            cfg.bootstrap_password = need("--password");
        } else if (a == "--synthetic") {
            cfg.synthetic = true;
        } else if (a == "--no-inject") {
            cfg.inject = false;
        } else if (a == "--fps") {
            cfg.fps = std::stoi(need("--fps"));
        } else if (a == "--help" || a == "-h") {
            std::cout <<
                "peerdesk-server — Ubuntu host for PeerDesk (J0)\n"
                "  --port N          listen port (default 4473)\n"
                "  --bind ADDR       default 0.0.0.0\n"
                "  --data-dir PATH   hashes + TLS cert/key\n"
                "  --user NAME       create this login if the store is empty (default jordan)\n"
                "  --password STR    password for that login (default peerdesk)\n"
                "  --synthetic       draw a host canvas instead of X11 (tests / non-X11)\n"
                "  --no-inject       do not open XTEST\n";
            return 0;
        } else {
            std::cerr << "Unknown flag: " << a << "\n";
            return 2;
        }
    }

    peerdesk::HostServer server(cfg);
    g_server = &server;
    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);

    std::string err;
    if (!server.setup(err)) {
        std::cerr << "peerdesk-server: " << err << "\n";
        return 1;
    }
    std::cout << "peerdesk-server: listening on " << cfg.bind << ":" << server.port()
              << (cfg.synthetic ? " (synthetic display)" : " (X11 capture)") << "\n";
    std::cout << "peerdesk-server: login '" << cfg.bootstrap_user
              << "' (password hashed on disk, never stored in plaintext)\n";
    std::string ferr;
    const auto fp = peerdesk::cert_sha256_fingerprint(server.cert_path(), ferr);
    if (!fp.empty()) {
        std::cout << "peerdesk-server: TLS SHA256 fingerprint " << fp << "\n";
        std::cout << "peerdesk-server: viewers must set PEERDESK_CA_FILE=" << server.cert_path()
                  << "\n";
    }
    server.run();
    std::cout << "peerdesk-server: stopped (process stays up across client disconnect; this is a clean exit)\n";
    return 0;
}
