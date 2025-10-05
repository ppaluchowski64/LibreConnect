#include <CryptographicIdentityManager.h>
#include <tracy/Tracy.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <DebugLog.h>
#include <cstdio>
#include <filesystem>

std::mutex CryptographicIdentityManager::m_mutex{};
EVP_PKEY* CryptographicIdentityManager::m_keyPair{nullptr};

void CryptographicIdentityManager::GenerateCertificate(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string privateKeyPath = (path / "privateKey.key").string();
    const std::string certificatePath = (path / "certificate.crt").string();

    std::filesystem::create_directories(path);

    const unsigned char * C_value = reinterpret_cast<const unsigned char*>("PL");
    const unsigned char * O_value = reinterpret_cast<const unsigned char*>("LibreConnect");
    const unsigned char * CN_value = reinterpret_cast<const unsigned char*>("localhost");
    constexpr int exp = 60 * 60 * 24 * 30;

    EVP_PKEY* privateKey = nullptr;
    EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!context || EVP_PKEY_keygen_init(context) <= 0 || EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context, NID_X9_62_prime256v1) <= 0 || EVP_PKEY_keygen(context, &privateKey) <= 0) {
        const std::string error = GetOpenSSLError();
        Debug::LogError("Failed to generate EC key ({})", error);
        EVP_PKEY_CTX_free(context);
        return;
    }

    EVP_PKEY_CTX_free(context);

    FILE* privateKeyFile = fopen(privateKeyPath.c_str(), "wb");
    if (!privateKeyFile) {
        Debug::LogError("Failed to open keyfile");
        return;
    }
    PEM_write_PrivateKey(privateKeyFile, privateKey, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(privateKeyFile);

    X509* certificate = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1);
    X509_gmtime_adj(X509_get_notBefore(certificate), 0);
    X509_gmtime_adj(X509_get_notAfter(certificate), exp);
    X509_set_version(certificate, 2);
    X509_set_pubkey(certificate, privateKey);

    X509_NAME* name = X509_get_subject_name(certificate);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, C_value, -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, O_value, -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, CN_value, -1, -1, 0);
    X509_set_issuer_name(certificate, name);

    if (!X509_sign(certificate, privateKey, EVP_sha256())) {
        const std::string error = GetOpenSSLError();
        Debug::LogError("Failed to sign certificate ({})", error);
        return;
    }

    FILE* certfile = fopen(certificatePath.c_str(), "wb");
    if (!certfile) {
        const std::string error = GetOpenSSLError();
        Debug::LogError("Failed to sign certificate ({})", error);
        return;
    }
    PEM_write_X509(certfile, certificate);
    fclose(certfile);

    X509_free(certificate);
    EVP_PKEY_free(privateKey);
}

bool CryptographicIdentityManager::IsCertificateValid(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    constexpr int certificateMinimalTimeLeft = 60 * 10;

    const std::string certPath = (path / "certificate.crt").string();
    FILE * fp = fopen(certPath.c_str(), "r");
    if (!fp) {
        return false;
    }

    X509 * cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!cert) {
        return false;
    }

    time_t now = time(nullptr);
    time_t future = now + certificateMinimalTimeLeft;

    const bool valid = (X509_cmp_time(X509_get_notBefore(cert), & now) <= 0 &&
        X509_cmp_time(X509_get_notAfter(cert), & future) >= 0);

    X509_free(cert);
    return valid;
}

void CryptographicIdentityManager::LoadOrGenerateKeyPair(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    constexpr int bits = 2048;

    EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!context || EVP_PKEY_keygen_init(context) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(context, bits) <= 0 || EVP_PKEY_keygen(context, &m_keyPair) <= 0) {
        Debug::LogError("OpenSSL error: {}", GetOpenSSLError());
    }

    EVP_PKEY_CTX_free(context);
}

std::string CryptographicIdentityManager::GetPublicKey() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_keyPair == nullptr) {
        Debug::LogError("Failed to get public key: key pair wasn't loaded!");
        return "";
    }

    BIO* publicKeyBio = BIO_new(BIO_s_mem());
    if (!PEM_write_bio_PUBKEY(publicKeyBio, m_keyPair)) {
        Debug::LogError("OpenSSL error: {}", GetOpenSSLError());
        return "";
    }


    BUF_MEM* buffer = nullptr;
    BIO_get_mem_ptr(publicKeyBio, &buffer);

    std::string publicKeyString = std::string(buffer->data, buffer->length);
    return publicKeyString;
}

std::string CryptographicIdentityManager::GenerateRandomChallenge(const size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string challengeString;
    challengeString.resize(size);

    if (RAND_bytes(reinterpret_cast<unsigned char*>(challengeString.data()), static_cast<int>(size)) != 1) {
        Debug::LogError("OpenSSL error: {}", GetOpenSSLError());
    }

    return challengeString;
}

std::string CryptographicIdentityManager::SingChallenge(const std::string& challengeString) {
    std::lock_guard<std::mutex> lock(m_mutex);

    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context ||
        EVP_DigestSignInit(context, nullptr, EVP_sha256(), nullptr, m_keyPair) <= 0 ||
        EVP_DigestSignUpdate(context, challengeString.data(), challengeString.size()) <= 0) {
        Debug::LogError("OpenSSL error: {}", GetOpenSSLError());
        EVP_MD_CTX_free(context);
        return "";
    }

    size_t size = 0;
    if (EVP_DigestSignFinal(context, nullptr, &size) <= 0) {
        Debug::LogError("OpenSSL error: {}", GetOpenSSLError());
        EVP_MD_CTX_free(context);
        return "";
    }

    std::string result;
    result.resize(size);

    if (EVP_DigestSignFinal(context, reinterpret_cast<unsigned char*>(result.data()), &size) <= 0) {
        Debug::LogError("OpenSSL error: {}", GetOpenSSLError());
    }

    EVP_MD_CTX_free(context);
    return result;
}

bool CryptographicIdentityManager::VerifySignature(const std::string& publicKey, std::string challenge, std::string signature) {
    std::lock_guard<std::mutex> lock(m_mutex);

    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context ||
        EVP_DigestVerifyInit(context, nullptr, EVP_sha256(), nullptr, m_keyPair) <= 0 ||
        EVP_DigestVerifyUpdate(context, challenge.data(), challenge.size()) <= 0) {
        Debug::LogError("OpenSSL error: {}", GetOpenSSLError());
        EVP_MD_CTX_free(context);
        return false;
    }

    const int ok = EVP_DigestVerifyFinal(context, reinterpret_cast<const unsigned char*>(signature.data()), signature.size());
    EVP_MD_CTX_free(context);
    return ok == 1;
}

std::string CryptographicIdentityManager::GetOpenSSLError() {
    std::lock_guard<std::mutex> lock(m_mutex);
    const unsigned long err = ERR_get_error();

    if (err == 0)
        return "Unknown OpenSSL error (no error on queue)";

    char errBuf[256];
    ERR_error_string_n(err, errBuf, sizeof(errBuf));
    return std::move(std::string(errBuf));
}
