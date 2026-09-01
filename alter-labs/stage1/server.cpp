// ALTER Stage 1 lab — TcpControlDaemon analog (Maître D': admit, do not hide).
// NOT shipped product. Do not copy into client/, server/, or shared/.
//
// Linux, C++17, POSIX only. Compile:
//   g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread -o alter-nxd server.cpp
//
// Run (requires bind on TCP 4000; may need root or CAP_NET_BIND_SERVICE
// only if the port is privileged — 4000 is unprivileged):
//   ./alter-nxd
//   ./alter-nxd --foreground   # skip daemonize; logs stay on the terminal
//
// No-stealth log after daemonize: /tmp/alter-nxd.log and syslog (LOG_DAEMON).
// GUI tray/libnotify is Stage 3 (would add a dependency); Stage 1 uses a
// prominent file + syslog line with peer IP:port on every accept.

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr uint16_t kNxTcpPort = 4000;  // Librarian: nxd default TCP
constexpr int kListenBacklog = SOMAXCONN;
constexpr std::size_t kRecvCap = 4096;
constexpr const char* kLogPath = "/tmp/alter-nxd.log";
constexpr const char* kHandshakeAck = "NXD/ALTER-STAGE1 TCP 4000\n";

// Written from the accept thread and from SIGTERM/SIGINT (async-signal-safe
// types only in the handler).
static volatile sig_atomic_t g_stop = 0;
static volatile sig_atomic_t g_listen_fd = -1;

static std::mutex g_log_mu;
static FILE* g_log = nullptr;
static bool g_foreground = false;

void log_line(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    // vsnprintf never writes more than sizeof(buf)-1 bytes plus NUL.
    const std::size_t len =
        static_cast<std::size_t>(n) < sizeof(buf) ? static_cast<std::size_t>(n)
                                                  : sizeof(buf) - 1;

    std::lock_guard<std::mutex> lock(g_log_mu);
    if (g_foreground) {
        std::fwrite(buf, 1, len, stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
    if (g_log != nullptr) {
        std::fwrite(buf, 1, len, g_log);
        std::fputc('\n', g_log);
        std::fflush(g_log);
    }
    syslog(LOG_NOTICE, "%s", buf);
}

[[noreturn]] void die_errno(const char* what) {
    const int e = errno;
    syslog(LOG_ERR, "%s: %s", what, std::strerror(e));
    std::fprintf(stderr, "alter-nxd: %s: %s\n", what, std::strerror(e));
    std::_Exit(EXIT_FAILURE);
}

// Rule 1: ignore SIGPIPE so write/send to a dropped peer returns EPIPE
// instead of killing the whole daemon.
void install_sigpipe_ign() {
    struct sigaction sa {};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa, nullptr) != 0) {
        die_errno("sigaction(SIGPIPE)");
    }
}

// Async-signal-safe: only sig_atomic_t stores and close().
void handle_stop(int /*sig*/) {
    g_stop = 1;
    const int fd = static_cast<int>(g_listen_fd);
    if (fd >= 0) {
        (void)close(fd);
    }
}

void install_stop_handlers() {
    struct sigaction sa {};
    sa.sa_handler = handle_stop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // no SA_RESTART: accept() should surface EINTR
    if (sigaction(SIGTERM, &sa, nullptr) != 0) {
        die_errno("sigaction(SIGTERM)");
    }
    if (sigaction(SIGINT, &sa, nullptr) != 0) {
        die_errno("sigaction(SIGINT)");
    }
}

// Strict 6-step POSIX daemonize (APUE-style, matching the Stage 1 lab brief):
//  1) umask(0)
//  2) fork(); parent _exit
//  3) setsid() — new session, no controlling TTY
//  4) chdir("/")
//  5) close inherited fds 0 .. open-max
//  6) reopen stdin/stdout/stderr on /dev/null via open + dup
void daemonize() {
    umask(0);

    const pid_t pid = fork();
    if (pid < 0) {
        die_errno("fork");
    }
    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        die_errno("setsid");
    }

    if (chdir("/") != 0) {
        die_errno("chdir(/)");
    }

    long maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0) {
        maxfd = 1024;
    }
    for (int fd = 0; fd < static_cast<int>(maxfd); ++fd) {
        (void)close(fd);
    }

    const int nullfd = open("/dev/null", O_RDWR);
    if (nullfd < 0) {
        _exit(EXIT_FAILURE);
    }
    // After closing 0..N, the first open should be fd 0. Force 1 and 2 with dup().
    if (nullfd != STDIN_FILENO) {
        if (dup2(nullfd, STDIN_FILENO) < 0) {
            _exit(EXIT_FAILURE);
        }
        (void)close(nullfd);
    }
    if (dup(STDIN_FILENO) < 0) {  // stdout
        _exit(EXIT_FAILURE);
    }
    if (dup(STDIN_FILENO) < 0) {  // stderr
        _exit(EXIT_FAILURE);
    }
}

bool open_nostealth_log() {
    g_log = std::fopen(kLogPath, "a");
    if (g_log == nullptr) {
        if (g_foreground) {
            std::fprintf(stderr, "alter-nxd: fopen(%s): %s (stdout only)\n",
                         kLogPath, std::strerror(errno));
            return true;
        }
        syslog(LOG_ERR, "fopen(%s): %s", kLogPath, std::strerror(errno));
        return false;
    }
    return true;
}

int create_listen_socket() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die_errno("socket");
    }

    const int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
        die_errno("setsockopt(SO_REUSEADDR)");
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kNxTcpPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        die_errno("bind(0.0.0.0:4000)");
    }
    if (listen(fd, kListenBacklog) != 0) {
        die_errno("listen");
    }
    return fd;
}

void peer_string(const sockaddr_in& peer, char* out, std::size_t outlen) {
    char ip[INET_ADDRSTRLEN] {};
    if (inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip)) == nullptr) {
        std::snprintf(out, outlen, "?:?");
        return;
    }
    std::snprintf(out, outlen, "%s:%u", ip,
                  static_cast<unsigned>(ntohs(peer.sin_port)));
}

// Rule 2: recv into a fixed stack array; copy/use only `n` bytes.
void session_loop(int conn_fd, std::string peer) {
    std::array<char, kRecvCap> buf {};

    while (g_stop == 0) {
        const ssize_t n = recv(conn_fd, buf.data(), buf.size(), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EPIPE || errno == ECONNRESET) {
                log_line("session end %s: peer dropped (%s)", peer.c_str(),
                         std::strerror(errno));
                break;
            }
            log_line("session recv %s: %s", peer.c_str(), std::strerror(errno));
            break;
        }
        if (n == 0) {
            log_line("session end %s: orderly close", peer.c_str());
            break;
        }

        // Capability / control bytes — bounded by n, never by a client length field.
        log_line("recv %s: %zd byte(s) (capped at %zu)", peer.c_str(), n,
                 buf.size());

        const ssize_t ack_len = static_cast<ssize_t>(std::strlen(kHandshakeAck));
        ssize_t off = 0;
        while (off < ack_len) {
            const ssize_t w =
                send(conn_fd, kHandshakeAck + off,
                     static_cast<std::size_t>(ack_len - off), MSG_NOSIGNAL);
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                log_line("session send %s: %s", peer.c_str(), std::strerror(errno));
                break;
            }
            off += w;
        }
        if (off < ack_len) {
            break;
        }
    }

    (void)close(conn_fd);
}

void run_accept_loop(int listen_fd) {
    g_listen_fd = listen_fd;
    log_line("*** ALTER nxd LISTENING *** tcp=0.0.0.0:%u pid=%ld (no stealth)",
             static_cast<unsigned>(kNxTcpPort), static_cast<long>(getpid()));

    while (g_stop == 0) {
        sockaddr_in peer {};
        socklen_t plen = sizeof(peer);
        const int conn = accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &plen);
        if (conn < 0) {
            if (g_stop != 0) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EBADF || errno == EINVAL) {
                break;
            }
            log_line("accept: %s", std::strerror(errno));
            continue;
        }

        char who[INET_ADDRSTRLEN + 16];
        peer_string(peer, who, sizeof(who));
        // Rule 3 / lab brief: prominent connection record (IP + port). Never silent.
        log_line("*** ALTER nxd CONNECTION *** peer=%s listen=0.0.0.0:%u pid=%ld",
                 who, static_cast<unsigned>(kNxTcpPort),
                 static_cast<long>(getpid()));

        try {
            std::thread(session_loop, conn, std::string(who)).detach();
        } catch (const std::exception& ex) {
            log_line("thread spawn failed for %s: %s", who, ex.what());
            (void)close(conn);
        }
    }

    g_listen_fd = -1;
    if (listen_fd >= 0) {
        (void)close(listen_fd);
    }
    log_line("*** ALTER nxd SHUTDOWN ***");
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--foreground") == 0 ||
            std::strcmp(argv[i], "-f") == 0) {
            g_foreground = true;
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            std::fprintf(stdout,
                         "alter-nxd Stage 1 (TCP %u)\n"
                         "  --foreground, -f   do not daemonize\n",
                         static_cast<unsigned>(kNxTcpPort));
            return EXIT_SUCCESS;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    openlog("alter-nxd", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    install_sigpipe_ign();
    install_stop_handlers();

    if (!g_foreground) {
        daemonize();
        // stdio is /dev/null; file + syslog carry no-stealth notices.
    }

    if (!open_nostealth_log()) {
        closelog();
        return EXIT_FAILURE;
    }

    const int listen_fd = create_listen_socket();
    run_accept_loop(listen_fd);

    if (g_log != nullptr) {
        std::fclose(g_log);
        g_log = nullptr;
    }
    closelog();
    return EXIT_SUCCESS;
}
