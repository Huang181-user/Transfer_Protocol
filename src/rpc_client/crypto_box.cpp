#include "crypto_box.h"
#include "logger.h"
#include <sodium.h>
bool CryptoBox::initialize() {
    if (sodium_init() < 0) {
        ZHI_LOG_ERR("Libsodium initialization failed!");
        return false;
    }
    ZHI_LOG_INFO("ChaCha20-Poly1305 engine (Libsodium) ready.");
    return true;
}
bool CryptoBox::encrypt_payload(const std::vector<uint8_t>& plaintext, const std::string& symmetric_key, std::vector<uint8_t>& ciphertext) {
    if (symmetric_key.size() != crypto_aead_chacha20poly1305_ietf_KEYBYTES) {
        ZHI_LOG_ERR("Invalid key size!");
        return false;
    }
    ciphertext.resize(crypto_aead_chacha20poly1305_ietf_NPUBBYTES + plaintext.size() + crypto_aead_chacha20poly1305_ietf_ABYTES);
    unsigned char* nonce = ciphertext.data();
    randombytes_buf(nonce, crypto_aead_chacha20poly1305_ietf_NPUBBYTES);
    unsigned long long ciphertext_len;
    int result = crypto_aead_chacha20poly1305_ietf_encrypt(
        ciphertext.data() + crypto_aead_chacha20poly1305_ietf_NPUBBYTES,
        &ciphertext_len,
        plaintext.data(), plaintext.size(),
        nullptr, 0,
        nullptr,
        nonce,
        reinterpret_cast<const unsigned char*>(symmetric_key.data())
    );
    if (result != 0) {
        ZHI_LOG_ERR("Encryption failed!");
        return false;
    }
    ciphertext.resize(crypto_aead_chacha20poly1305_ietf_NPUBBYTES + ciphertext_len);
    return true;
}
bool CryptoBox::decrypt_payload(const std::vector<uint8_t>& ciphertext, const std::string& symmetric_key, std::vector<uint8_t>& plaintext) {
    if (symmetric_key.size() != crypto_aead_chacha20poly1305_ietf_KEYBYTES) {
        ZHI_LOG_ERR("Invalid key size!");
        return false;
    }
    if (ciphertext.size() < crypto_aead_chacha20poly1305_ietf_NPUBBYTES + crypto_aead_chacha20poly1305_ietf_ABYTES) {
        ZHI_LOG_WARN("Packet too short!");
        return false;
    }
    const unsigned char* nonce = ciphertext.data();
    const unsigned char* actual_cipher = ciphertext.data() + crypto_aead_chacha20poly1305_ietf_NPUBBYTES;
    unsigned long long actual_cipher_len = ciphertext.size() - crypto_aead_chacha20poly1305_ietf_NPUBBYTES;
    plaintext.resize(actual_cipher_len - crypto_aead_chacha20poly1305_ietf_ABYTES);
    unsigned long long plaintext_len;
    int result = crypto_aead_chacha20poly1305_ietf_decrypt(
        plaintext.data(),
        &plaintext_len,
        nullptr,
        actual_cipher, actual_cipher_len,
        nullptr, 0,
        nonce,
        reinterpret_cast<const unsigned char*>(symmetric_key.data())
    );
    if (result != 0) {
        ZHI_LOG_ERR("Decryption MAC validation failed!");
        return false;
    }
    plaintext.resize(plaintext_len);
    return true;
}
