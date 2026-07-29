#include "rpc_client/crypto_box.h"
#include <sodium.h>

bool CryptoBox::initialize() { return sodium_init() >= 0; }

bool CryptoBox::encrypt_payload(const std::vector<uint8_t>& plaintext, const std::string& symmetric_key, std::vector<uint8_t>& ciphertext) {
    if (symmetric_key.size() != crypto_aead_chacha20poly1305_ietf_KEYBYTES) return false;
    ciphertext.resize(crypto_aead_chacha20poly1305_ietf_NPUBBYTES + plaintext.size() + crypto_aead_chacha20poly1305_ietf_ABYTES);
    unsigned char* nonce = ciphertext.data(); randombytes_buf(nonce, crypto_aead_chacha20poly1305_ietf_NPUBBYTES);
    unsigned long long ciphertext_len;
    crypto_aead_chacha20poly1305_ietf_encrypt(ciphertext.data() + crypto_aead_chacha20poly1305_ietf_NPUBBYTES, &ciphertext_len, plaintext.data(), plaintext.size(), nullptr, 0, nullptr, nonce, reinterpret_cast<const unsigned char*>(symmetric_key.data()));
    return true;
}

bool CryptoBox::decrypt_payload(const std::vector<uint8_t>& ciphertext, const std::string& symmetric_key, std::vector<uint8_t>& plaintext) {
    if (symmetric_key.size() != crypto_aead_chacha20poly1305_ietf_KEYBYTES) return false;
    
    // 🔥 CHỐT CHẶN TỬ THẦN: Khóa mõm gói tin rác < 28 bytes, ngăn chặn Underflow nổ tung Terminal!
    if (ciphertext.size() < crypto_aead_chacha20poly1305_ietf_NPUBBYTES + crypto_aead_chacha20poly1305_ietf_ABYTES) return false;
    
    const unsigned char* nonce = ciphertext.data();
    unsigned long long plaintext_len;
    plaintext.resize(ciphertext.size() - crypto_aead_chacha20poly1305_ietf_NPUBBYTES - crypto_aead_chacha20poly1305_ietf_ABYTES);
    if (crypto_aead_chacha20poly1305_ietf_decrypt(plaintext.data(), &plaintext_len, nullptr, ciphertext.data() + crypto_aead_chacha20poly1305_ietf_NPUBBYTES, ciphertext.size() - crypto_aead_chacha20poly1305_ietf_NPUBBYTES, nullptr, 0, nonce, reinterpret_cast<const unsigned char*>(symmetric_key.data())) != 0) return false;
    return true;
}
