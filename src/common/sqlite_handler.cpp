#include "common/sqlite_handler.h"
#include "common/logger.h"
#include <sstream>

SQLiteHandler::SQLiteHandler() : db(nullptr) {}
SQLiteHandler::~SQLiteHandler() { if (db) sqlite3_close(db); }

bool SQLiteHandler::initialize_database(const std::string& db_path) {
    db_file_path = db_path;
    int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_exec(db, "PRAGMA journal_mode = WAL; PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    return true;
}

// KHÔNG BĂM NỮA! BÊN GO ĐÃ BĂM SẴN RỒI, CHỈ VIỆC SO SÁNH CHUỖI
bool SQLiteHandler::authenticate_user(const std::string& username, const std::string& pre_hashed_password, UserRecord& out_user) {
    sqlite3_stmt* stmt = nullptr;
    std::string query = "SELECT id, username, password_hash, system_uid, shared_path, permission FROM users WHERE username = ?;";
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    bool auth_success = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string db_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (db_hash == pre_hashed_password) {
            out_user.id = sqlite3_column_int(stmt, 0);
            out_user.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            out_user.system_uid = sqlite3_column_int(stmt, 3);
            out_user.shared_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            auth_success = true;
        }
    }
    sqlite3_finalize(stmt);
    return auth_success;
}

bool SQLiteHandler::add_active_session(int user_id, const std::string& token_hash, const std::string& local_ip, const std::string& tailscale_ip, const std::string& hardware_hash) {
    sqlite3_stmt* stmt = nullptr;
    std::string query = "INSERT INTO active_sessions (user_id, token_hash, local_ip, tailscale_ip, hardware_hash) VALUES (?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, token_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, local_ip.empty() ? nullptr : local_ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, tailscale_ip.empty() ? nullptr : tailscale_ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, hardware_hash.c_str(), -1, SQLITE_TRANSIENT);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

// Bỏ các hàm râu ria khác cho gọn
bool SQLiteHandler::verify_session_token(const std::string& t, const std::string& h, UserRecord& u) { return false; }
bool SQLiteHandler::update_session_roaming(const std::string& t, const std::string& n, bool i) { return false; }
bool SQLiteHandler::terminate_session(const std::string& t) { return false; }
