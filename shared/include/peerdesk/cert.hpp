#pragma once

#include <filesystem>
#include <string>

namespace peerdesk {

bool ensure_self_signed_cert(const std::filesystem::path& key, const std::filesystem::path& crt,
                             std::string& err);

std::string cert_sha256_fingerprint(const std::filesystem::path& crt, std::string& err);

}  // namespace peerdesk
