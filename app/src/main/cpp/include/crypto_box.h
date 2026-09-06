#ifndef ZHIAUTH_CRYPTO_BOX_H
#define ZHIAUTH_CRYPTO_BOX_H
#include <vector>
#include <string>
#include <cstdint>
class CryptoBox {
public:
    static bool initialize();
    static bool encrypt_payload(const std::vector<uint8_t>& plaintext, const std::string& symmetric_key, std::vector<uint8_t>& ciphertext);
    static bool decrypt_payload(const std::vector<uint8_t>& ciphertext, const std::string& symmetric_key, std::vector<uint8_t>& plaintext);
};
#endif
