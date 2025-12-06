#ifndef CRYPTOGRAPHIC_IDENTIFY_MANAGER_H
#define CRYPTOGRAPHIC_IDENTIFY_MANAGER_H

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif


#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <mutex>
#include <filesystem>

class CryptographicIdentityManager {
public:
    static void GenerateCertificate(std::string_view privateKeyPath, std::string_view certificatePath);
    static bool IsCertificateValid(std::string_view certificatePath);
    static void LoadOrGenerateKeyPair(const std::filesystem::path& path);

    static std::string GetPublicKey();
    static std::string GenerateRandomChallenge(size_t size = 32);
    static std::string SignChallenge(const std::string& challengeString);
    static bool VerifySignature(const std::string& publicKeyString, const std::string& challenge, const std::string& signature);

private:
    static std::mutex m_mutex;
    static EVP_PKEY* m_keyPair;

    static std::string GetOpenSSLError();
};

#endif //CRYPTOGRAPHIC_IDENTIFY_MANAGER_H
