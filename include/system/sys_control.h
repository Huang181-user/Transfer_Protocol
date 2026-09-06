#ifndef ZHIAUTH_SYS_CONTROL_H
#define ZHIAUTH_SYS_CONTROL_H

#include <string>
#include <unistd.h>
#include <sys/types.h>

class SysControl {
public:
    SysControl() = default;
    ~SysControl() = default;

    // Kiểm tra xem thư mục đích gộp MergerFS (/mnt/HDD_merge) có đang hoạt động hay không
    bool verify_system_storage_paths(const std::string& storage_path);

    // Xử lý luồng hạ quyền lực từ admin tối cao về UID/GID của user thường để chạy tiến trình con an toàn
    bool drop_process_privileges(uid_t target_uid, gid_t target_gid);

    // Lấy thông tin UID hiện tại của tiến trình đang chạy
    void log_current_execution_identity();
};

#endif // ZHIAUTH_SYS_CONTROL_H
