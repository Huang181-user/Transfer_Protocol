#include "rpc_vfs/crypto_box.h"
#include "common/logger.h"
#include <sodium.h>

bool CryptoBox::initialize() {
    if (sodium_init() < 0) {
        ZHI_LOG_ERR("Hệ thống mã hóa Libsodium khởi động THẤT BẠI! Tắt Daemon ngay!");
        return false;
    }
    ZHI_LOG_INFO("Động cơ mã hóa ChaCha20-Poly1305 (Libsodium) đã sẵn sàng.");
    return true;
}

bool CryptoBox::encrypt_payload(const std::vector<uint8_t>& plaintext, const std::string& symmetric_key, std::vector<uint8_t>& ciphertext) {
    if (symmetric_key.size() != crypto_aead_chacha20poly1305_ietf_KEYBYTES) {
        ZHI_LOG_ERR("Lỗi độ dài Key bảo mật! Bắt buộc phải là 32 bytes.");
        return false;
    }

    // 1. Dành chỗ cho: [Nonce (12 Bytes)] + [Ciphertext] + [MAC (16 Bytes)]
    ciphertext.resize(crypto_aead_chacha20poly1305_ietf_NPUBBYTES + plaintext.size() + crypto_aead_chacha20poly1305_ietf_ABYTES);

    // 2. Tạo Nonce ngẫu nhiên (chống tấn công phát lại Replay Attack) và dán vào đầu mảng
    unsigned char* nonce = ciphertext.data();
    randombytes_buf(nonce, crypto_aead_chacha20poly1305_ietf_NPUBBYTES);

    // 3. Mã hóa phần thân dữ liệu (nằm ngay sau Nonce)
    unsigned long long ciphertext_len;
    int result = crypto_aead_chacha20poly1305_ietf_encrypt(
        ciphertext.data() + crypto_aead_chacha20poly1305_ietf_NPUBBYTES, // Vị trí ghi Ciphertext
        &ciphertext_len,
        plaintext.data(), plaintext.size(),
        nullptr, 0, // Không dùng Dữ liệu xác thực bổ sung (AD)
        nullptr,    // Không cần Secret Nonce
        nonce,
        reinterpret_cast<const unsigned char*>(symmetric_key.data())
    );

    if (result != 0) {
        ZHI_LOG_ERR("Cỗ máy Libsodium báo lỗi khi đang ép giáp mã hóa!");
        return false;
    }

    // Cắt gọt lại mảng (resize) cho khít với độ dài thực tế vừa ghi xuống
    ciphertext.resize(crypto_aead_chacha20poly1305_ietf_NPUBBYTES + ciphertext_len);
    return true;
}

bool CryptoBox::decrypt_payload(const std::vector<uint8_t>& ciphertext, const std::string& symmetric_key, std::vector<uint8_t>& plaintext) {
    if (symmetric_key.size() != crypto_aead_chacha20poly1305_ietf_KEYBYTES) {
        ZHI_LOG_ERR("Lỗi độ dài Key giải mã! Bắt buộc 32 bytes.");
        return false;
    }

    // Gói tin tối thiểu phải dài hơn mẩu Nonce và MAC
    if (ciphertext.size() < crypto_aead_chacha20poly1305_ietf_NPUBBYTES + crypto_aead_chacha20poly1305_ietf_ABYTES) {
        ZHI_LOG_WARN("Gói tin bị cắt xén hoặc quá ngắn! DROP lập tức!");
        return false;
    }

    // 1. Trích xuất mẩu Nonce ở 12 bytes đầu tiên
    const unsigned char* nonce = ciphertext.data();
    const unsigned char* actual_cipher = ciphertext.data() + crypto_aead_chacha20poly1305_ietf_NPUBBYTES;
    unsigned long long actual_cipher_len = ciphertext.size() - crypto_aead_chacha20poly1305_ietf_NPUBBYTES;

    // 2. Chuẩn bị mảng hứng dữ liệu nguyên bản (Plaintext)
    plaintext.resize(actual_cipher_len - crypto_aead_chacha20poly1305_ietf_ABYTES);

    // 3. Giải mã và Verify chữ ký MAC cùng lúc
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
        // LUÔN BẮN LOG GẮT Ở ĐÂY VÌ ĐÓ LÀ DẤU HIỆU CÓ NGƯỜI ĐANG QUÉT MẠNG HOẶC PHÁ GÓI TIN!
        ZHI_LOG_ERR("💀 BÁO ĐỘNG ĐỎ: Chữ ký MAC bị sai lệch hoặc Key không khớp! DROP GÓI TIN!");
        return false;
    }

    plaintext.resize(plaintext_len);
    return true;
}
