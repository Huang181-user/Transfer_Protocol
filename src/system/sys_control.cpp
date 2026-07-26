#include "system/sys_control.h"
#include "common/logger.h"
#include <filesystem>

namespace fs = std::filesystem;

bool SysControl::verify_system_storage_paths(const std::string& storage_path) {
    ZHI_LOG_DEBUG("Initiating integrity checks for targeted system mount path: '" + storage_path + "'");
    
    try {
        // Sử dụng C++17 filesystem để check sự tồn tại của ổ đĩa gộp, triệt tiêu thư viện Regex
        if (fs::exists(storage_path) && fs::is_directory(storage_path)) {
            ZHI_LOG_INFO("Target storage infrastructure storage pool directory validated successfully: " + storage_path);
            return true;
        } else {
            ZHI_LOG_WARN("Target mount path exists but is either corrupted or not recognized as a directory: " + storage_path);
            return false;
        }
    } catch (const fs::filesystem_error& e) {
        ZHI_LOG_ERR("OS Filesystem subsystem subsystem returned a critical access error: " + std::string(e.what()));
        return false;
    }
}

bool SysControl::drop_process_privileges(uid_t target_uid, gid_t target_gid) {
    ZHI_LOG_INFO("Request received to drop process execution privileges to Target UID: " + 
                 std::to_string(target_uid) + " | Target GID: " + std::to_string(target_gid));

    // 1. Hạ quyền Group ID trước
    if (setgid(target_gid) != 0) {
        ZHI_LOG_ERR("Security isolation failure: Unable to drop process Group ID privileges (setgid failed).");
        return false;
    }
    ZHI_LOG_DEBUG("Process Group ID (GID) successfully restricted to system target identity zone.");

    // 2. Hạ quyền User ID sau
    if (setuid(target_uid) != 0) {
        ZHI_LOG_ERR("Security isolation failure: Unable to drop process User ID privileges (setuid failed).");
        return false;
    }
    ZHI_LOG_INFO("Process security context successfully downgraded. Process sandbox isolation operational.");
    
    // In ra danh tính mới sau khi thiến quyền để kiểm chứng debug
    log_current_execution_identity();
    return true;
}

void SysControl::log_current_execution_identity() {
    uid_t current_uid = getuid();
    gid_t current_gid = getgid();
    
    ZHI_LOG_DEBUG("Current Active Process Execution Identity State -> Real UID: " + 
                 std::to_string(current_uid) + " | Real GID: " + std::to_string(current_gid));
}
