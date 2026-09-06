#ifndef ZHIAUTH_SQLITE_HANDLER_H
#define ZHIAUTH_SQLITE_HANDLER_H

#include <string>
#include <sqlite3.h>

struct UserRecord {
    int id;
    std::string username;
    std::string password_hash;
    int system_uid;
    std::string shared_path;
    std::string permission;
};

class SQLiteHandler {
public:
    SQLiteHandler();
    ~SQLiteHandler();

    // Khởi tạo kết nối DB và ép cấu hình WAL
    bool initialize_database(const std::string& db_path);
    
    // Xác thực tài khoản bằng mật khẩu thô (sẽ tự động băm kèm muối bên trong)
    bool authenticate_user(const std::string& username, const std::string& raw_password, UserRecord& out_user);
    
    // Nạp một Session hoạt động mới vào DB khi đăng nhập thành công
    bool add_active_session(int user_id, const std::string& token_raw, const std::string& local_ip, const std::string& tailscale_ip, const std::string& hardware_hash);
    
    // Kiểm tra tính hợp lệ của Token và đối chiếu dấu vân tay phần cứng (Chống trộm Token)
    bool verify_session_token(const std::string& token_raw, const std::string& hardware_hash, UserRecord& out_user);
    
    // Cập nhật động địa chỉ IP khi Client thực hiện Roaming (LAN <-> Tailscale)
    bool update_session_roaming(const std::string& token_raw, const std::string& new_ip, bool is_local_ip);
    
    // Xóa session khỏi hệ thống khi người dùng Logout hoặc bị Timeout
    bool terminate_session(const std::string& token_raw);

private:
    sqlite3* db;
    std::string db_file_path;
    
    // Hàm băm nội bộ SHA-256 thuần C++ để xử lý Pass và Token
    std::string calculate_sha256(const std::string& input);
};

#endif // ZHIAUTH_SQLITE_HANDLER_H
