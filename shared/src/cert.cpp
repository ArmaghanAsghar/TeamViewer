#include "peerdesk/cert.hpp"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

namespace peerdesk {
namespace {

struct BioFree {
    void operator()(BIO* b) const { BIO_free(b); }
};
struct PkeyFree {
    void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); }
};
struct X509Free {
    void operator()(X509* x) const { X509_free(x); }
};

std::string openssl_err() {
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return buf;
}

}  // namespace

bool ensure_self_signed_cert(const std::filesystem::path& key, const std::filesystem::path& crt,
                             std::string& err) {
    if (std::filesystem::exists(key) && std::filesystem::exists(crt)) return true;
    std::error_code ec;
    std::filesystem::create_directories(key.parent_path(), ec);

    std::unique_ptr<EVP_PKEY, PkeyFree> pkey(EVP_RSA_gen(2048));
    if (!pkey) {
        err = "RSA keygen failed: " + openssl_err();
        return false;
    }
    std::unique_ptr<X509, X509Free> x509(X509_new());
    if (!x509) {
        err = "X509_new failed";
        return false;
    }
    X509_set_version(x509.get(), X509_VERSION_3);
    ASN1_INTEGER_set(X509_get_serialNumber(x509.get()), 1);
    X509_gmtime_adj(X509_get_notBefore(x509.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(x509.get()), 60L * 60 * 24 * 365);
    X509_set_pubkey(x509.get(), pkey.get());
    X509_NAME* name = X509_get_subject_name(x509.get());
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("peerdesk-host"), -1, -1, 0);
    X509_set_issuer_name(x509.get(), name);
    if (X509_sign(x509.get(), pkey.get(), EVP_sha256()) == 0) {
        err = "X509_sign failed: " + openssl_err();
        return false;
    }

    std::unique_ptr<BIO, BioFree> kbio(BIO_new_file(key.c_str(), "wb"));
    if (!kbio || PEM_write_bio_PrivateKey(kbio.get(), pkey.get(), nullptr, nullptr, 0, nullptr,
                                          nullptr) != 1) {
        err = "Failed to write TLS key";
        return false;
    }
    std::unique_ptr<BIO, BioFree> cbio(BIO_new_file(crt.c_str(), "wb"));
    if (!cbio || PEM_write_bio_X509(cbio.get(), x509.get()) != 1) {
        err = "Failed to write TLS certificate";
        return false;
    }
    return true;
}

std::string cert_sha256_fingerprint(const std::filesystem::path& crt, std::string& err) {
    std::ifstream in(crt, std::ios::binary);
    if (!in) {
        err = "Cannot read certificate";
        return {};
    }
    std::unique_ptr<BIO, BioFree> bio(BIO_new_file(crt.c_str(), "rb"));
    if (!bio) {
        err = "Cannot open certificate";
        return {};
    }
    std::unique_ptr<X509, X509Free> x509(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    if (!x509) {
        err = "Invalid PEM certificate";
        return {};
    }
    unsigned char der[4096];
    unsigned char* p = der;
    const int len = i2d_X509(x509.get(), &p);
    if (len <= 0) {
        err = "i2d_X509 failed";
        return {};
    }
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(der, static_cast<size_t>(len), hash);
    std::ostringstream o;
    o << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        if (i) o << ':';
        o << std::setw(2) << static_cast<int>(hash[i]);
    }
    return o.str();
}

}  // namespace peerdesk
