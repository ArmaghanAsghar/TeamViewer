#include "peerdesk/net.hpp"

#include "peerdesk/cert.hpp"

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <arpa/inet.h>
#include <csignal>
#include <iomanip>
#include <netinet/tcp.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <poll.h>
#include <sstream>
#include <sys/time.h>

#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

namespace peerdesk {
namespace {

void ignore_sigpipe() {
    static const int once = [] {
        std::signal(SIGPIPE, SIG_IGN);
        return 0;
    }();
    (void)once;
}

bool set_recv_timeout(int fd, int timeout_ms) {
    if (fd < 0) return false;
    timeval tv{};
    if (timeout_ms < 0) {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
    } else {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
    }
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

bool wait_fd(int fd, int timeout_ms, bool write) {
    pollfd p{};
    p.fd = fd;
    p.events = write ? POLLOUT : POLLIN;
    const int rc = ::poll(&p, 1, timeout_ms);
    return rc > 0 && (p.revents & (write ? POLLOUT : POLLIN));
}

bool fingerprints_match(const std::filesystem::path& ca_file, X509* peer, std::string& err) {
    if (!peer) {
        err = "TLS handshake failed (no peer certificate)";
        return false;
    }
    const auto want = cert_sha256_fingerprint(ca_file, err);
    if (want.empty()) return false;
    unsigned char* der = nullptr;
    const int len = i2d_X509(peer, &der);
    if (len <= 0 || !der) {
        err = "TLS handshake failed (peer cert encode)";
        return false;
    }
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(der, static_cast<size_t>(len), hash);
    OPENSSL_free(der);
    std::ostringstream o;
    o << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        if (i) o << ':';
        o << std::setw(2) << static_cast<int>(hash[i]);
    }
    if (o.str() != want) {
        err = "TLS handshake failed (certificate does not match PEERDESK_CA_FILE)";
        return false;
    }
    return true;
}

}  // namespace

struct TlsConn::Impl {
    asio::io_context ioc;
    std::unique_ptr<asio::ssl::context> ssl_ctx;
    std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket>> stream;
    bool open = false;
};

TlsConn::TlsConn() = default;
TlsConn::TlsConn(TlsConn&& o) noexcept = default;
TlsConn& TlsConn::operator=(TlsConn&& o) noexcept = default;
TlsConn::~TlsConn() { close(); }

void TlsConn::close() {
    if (!impl_ || !impl_->stream) {
        impl_.reset();
        return;
    }
    asio::error_code ec;
    impl_->stream->lowest_layer().cancel(ec);
    impl_->stream->shutdown(ec);
    impl_->stream->lowest_layer().close(ec);
    impl_->open = false;
    impl_.reset();
}

bool TlsConn::is_open() const { return impl_ && impl_->open && impl_->stream; }

bool TlsConn::send(const proto::Envelope& env) {
    if (!is_open()) return false;
    std::string bytes;
    if (!env.SerializeToString(&bytes) || bytes.size() > kMaxPayload) return false;
    const uint32_t n = htonl(static_cast<uint32_t>(bytes.size()));
    std::vector<uint8_t> buf(4 + bytes.size());
    std::memcpy(buf.data(), &n, 4);
    std::memcpy(buf.data() + 4, bytes.data(), bytes.size());
    asio::error_code ec;
    asio::write(*impl_->stream, asio::buffer(buf), ec);
    if (ec) {
        impl_->open = false;
        return false;
    }
    return true;
}

bool TlsConn::recv(proto::Envelope& env, int timeout_ms) {
    env.Clear();
    if (!is_open()) return false;
    set_recv_timeout(impl_->stream->lowest_layer().native_handle(), timeout_ms);
    uint32_t n_be = 0;
    asio::error_code ec;
    asio::read(*impl_->stream, asio::buffer(&n_be, 4), ec);
    if (ec) {
        if (ec == asio::error::timed_out || ec == asio::error::would_block ||
            ec == asio::error::try_again) {
            return false;
        }
        impl_->open = false;
        return false;
    }
    const uint32_t n = ntohl(n_be);
    if (n > kMaxPayload) {
        impl_->open = false;
        return false;
    }
    std::string bytes;
    bytes.resize(n);
    if (n > 0) {
        const int body_timeout = timeout_ms < 0 ? 10000 : std::max(timeout_ms, 2000);
        set_recv_timeout(impl_->stream->lowest_layer().native_handle(), body_timeout);
        asio::read(*impl_->stream, asio::buffer(bytes), ec);
        if (ec) {
            impl_->open = false;
            return false;
        }
    }
    if (!env.ParseFromString(bytes)) {
        impl_->open = false;
        return false;
    }
    return true;
}

std::optional<TlsConn> TlsConn::connect(const std::string& host, uint16_t port,
                                        const std::filesystem::path& ca_file, std::string& err) {
    ignore_sigpipe();
    if (ca_file.empty() || !std::filesystem::exists(ca_file)) {
        err = "TLS certificate not trusted (set PEERDESK_CA_FILE to the host server.crt)";
        return std::nullopt;
    }

    TlsConn conn;
    conn.impl_ = std::make_unique<Impl>();
    asio::error_code ec;
    asio::ip::tcp::resolver resolver(conn.impl_->ioc);
    const auto results = resolver.resolve(host, std::to_string(port), ec);
    if (ec || results.empty()) {
        err = "Host unreachable (DNS/address)";
        return std::nullopt;
    }

    conn.impl_->ssl_ctx = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_client);
    SSL_CTX_set_min_proto_version(conn.impl_->ssl_ctx->native_handle(), TLS1_2_VERSION);
    conn.impl_->ssl_ctx->set_verify_mode(asio::ssl::verify_none);

    asio::ip::tcp::socket sock(conn.impl_->ioc);
    asio::connect(sock, results, ec);
    if (ec) {
        err = "Host unreachable (connect failed)";
        return std::nullopt;
    }
    const int one = 1;
    ::setsockopt(sock.native_handle(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    conn.impl_->stream =
        std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(std::move(sock), *conn.impl_->ssl_ctx);
    conn.impl_->stream->handshake(asio::ssl::stream_base::client, ec);
    if (ec) {
        err = "TLS handshake failed";
        return std::nullopt;
    }
    X509* peer = SSL_get_peer_certificate(conn.impl_->stream->native_handle());
    std::unique_ptr<X509, void (*)(X509*)> hold(peer, X509_free);
    if (!fingerprints_match(ca_file, peer, err)) return std::nullopt;
    conn.impl_->open = true;
    return conn;
}

struct TlsListener::Impl {
    asio::io_context ioc;
    asio::ssl::context ssl_ctx{asio::ssl::context::tls_server};
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor;
    uint16_t port = 0;
};

TlsListener::TlsListener() : impl_(std::make_unique<Impl>()) {}
TlsListener::~TlsListener() { close(); }

void TlsListener::close() {
    if (!impl_) return;
    asio::error_code ec;
    if (impl_->acceptor) impl_->acceptor->close(ec);
    impl_->acceptor.reset();
    impl_->port = 0;
}

bool TlsListener::is_open() const { return impl_ && impl_->acceptor && impl_->acceptor->is_open(); }
uint16_t TlsListener::port() const { return impl_ ? impl_->port : 0; }

bool TlsListener::listen_on(const std::string& bind, uint16_t port, const std::filesystem::path& cert,
                            const std::filesystem::path& key, std::string& err) {
    ignore_sigpipe();
    close();
    asio::error_code ec;
    impl_->ssl_ctx = asio::ssl::context(asio::ssl::context::tls_server);
    SSL_CTX_set_min_proto_version(impl_->ssl_ctx.native_handle(), TLS1_2_VERSION);
    impl_->ssl_ctx.use_certificate_file(cert.string(), asio::ssl::context::pem, ec);
    if (ec) {
        err = "Failed to load TLS certificate";
        return false;
    }
    impl_->ssl_ctx.use_private_key_file(key.string(), asio::ssl::context::pem, ec);
    if (ec) {
        err = "Failed to load TLS key";
        return false;
    }

    asio::ip::tcp::endpoint ep;
    if (bind.empty() || bind == "0.0.0.0") {
        ep = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port);
    } else {
        ep = asio::ip::tcp::endpoint(asio::ip::make_address(bind, ec), port);
        if (ec) {
            err = "Invalid bind address";
            return false;
        }
    }
    impl_->acceptor = std::make_unique<asio::ip::tcp::acceptor>(impl_->ioc);
    impl_->acceptor->open(ep.protocol(), ec);
    impl_->acceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
    impl_->acceptor->bind(ep, ec);
    if (ec) {
        err = "Port already in use (bind failed)";
        impl_->acceptor.reset();
        return false;
    }
    impl_->acceptor->listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        err = "listen() failed";
        impl_->acceptor.reset();
        return false;
    }
    impl_->acceptor->non_blocking(true);
    impl_->port = impl_->acceptor->local_endpoint().port();
    return true;
}

std::optional<TlsConn> TlsListener::accept_one(int timeout_ms, std::string& err) {
    if (!is_open()) {
        err = "Not listening";
        return std::nullopt;
    }
    if (!wait_fd(impl_->acceptor->native_handle(), timeout_ms, false)) return std::nullopt;

    asio::error_code ec;
    asio::ip::tcp::socket sock(impl_->ioc);
    impl_->acceptor->accept(sock, ec);
    if (ec) {
        err = "accept() failed";
        return std::nullopt;
    }
    const int one = 1;
    ::setsockopt(sock.native_handle(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    TlsConn conn;
    conn.impl_ = std::make_unique<TlsConn::Impl>();
    conn.impl_->ssl_ctx = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_server);
    // Share server certs by copying files already loaded — use the listener context.
    conn.impl_->stream = std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(
        std::move(sock), impl_->ssl_ctx);
    conn.impl_->stream->handshake(asio::ssl::stream_base::server, ec);
    if (ec) {
        err = "TLS accept handshake failed";
        return std::nullopt;
    }
    conn.impl_->open = true;
    return conn;
}

}  // namespace peerdesk
