#pragma once

#include "peerdesk/protocol.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace peerdesk {

class TlsConn {
public:
    TlsConn();
    TlsConn(const TlsConn&) = delete;
    TlsConn& operator=(const TlsConn&) = delete;
    TlsConn(TlsConn&&) noexcept;
    TlsConn& operator=(TlsConn&&) noexcept;
    ~TlsConn();

    static std::optional<TlsConn> connect(const std::string& host, uint16_t port,
                                          const std::filesystem::path& ca_file, std::string& err);

    bool send(const proto::Envelope& env);
    bool recv(proto::Envelope& env, int timeout_ms);
    void close();
    bool is_open() const;

private:
    friend class TlsListener;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class TlsListener {
public:
    TlsListener();
    TlsListener(const TlsListener&) = delete;
    TlsListener& operator=(const TlsListener&) = delete;
    ~TlsListener();

    bool listen_on(const std::string& bind, uint16_t port, const std::filesystem::path& cert,
                   const std::filesystem::path& key, std::string& err);
    uint16_t port() const;
    std::optional<TlsConn> accept_one(int timeout_ms, std::string& err);
    void close();
    bool is_open() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace peerdesk
