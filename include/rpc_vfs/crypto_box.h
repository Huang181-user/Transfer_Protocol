#ifndef ZHIAUTH_CRYPTO_BOX_H
#define ZHIAUTH_CRYPTO_BOX_H

#include <vector>
#include <string>
#include <cstdint>

class CryptoBox {
public:
    // Khởi động động cơ mã hóa (Phải gọi 1 lần khi hệ thống nổ máy)
    static bool initialize();
    
    // Đóng gói: Mặc giáp ChaCha20-Poly1305 + Bơm Nonce chống Replay Attack
    static bool encrypt_payload(const std::vector<uint8_t>& plaintext, const std::string& symmetric_key, std::vector<uint8_t>& ciphertext);
    
    // Gỡ gói: Rã giáp, kiểm tra tính toàn vẹn (MAC). Trả về false nếu gói tin bị can thiệp
    static bool decrypt_payload(const std::vector<uint8_t>& ciphertext, const std::string& symmetric_key, std::vector<uint8_t>& plaintext);
};

#endif // ZHIAUTH_CRYPTO_BOX_H
