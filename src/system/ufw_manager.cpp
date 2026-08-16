#include "system/ufw_manager.h"
#include "common/logger.h"
#include <cstdlib>
#include <sstream>
#include <cstdio>
#include <array>
#include <memory>

UFWManager::UFWManager() : is_running(false) {}

UFWManager::~UFWManager() {
    stop_worker();
}

// 🛡️ HÀM BẮT LOG THỜI GIAN THỰC (BẮT SẠCH STDOUT + STDERR + RETURN CODE)
static void run_cmd_verbose(const std::string& cmd) {
    std::string full_cmd = cmd + " 2>&1"; // Gom cả luồng lỗi stderr về stdout
    std::array<char, 256> buffer;
    std::string output = "";
    
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) {
        ZHI_LOG_ERR("[NFT-EXEC-FAIL] Failed to open system pipe for command: " + cmd);
        return;
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    
    int status = pclose(pipe);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    
    // Trim bớt ký tự xuống dòng ở cuối
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    if (exit_code == 0) {
        ZHI_LOG_INFO("[NFT-SUCCESS] Executed: '" + cmd + "' | Output: " + (output.empty() ? "OK" : output));
    } else {
        ZHI_LOG_ERR("[NFT-ERROR] Exit Code [" + std::to_string(exit_code) + "] for: '" + cmd + "' | Error Output: " + output);
    }
}

void UFWManager::start_worker() {
    if (is_running) return;

    ZHI_LOG_INFO("=== INITIALIZING NFTABLES SET INFRASTRUCTURE ===");

    // 1. Tạo table/chain cơ bản
    run_cmd_verbose("sudo /usr/sbin/nft add table inet filter");
    run_cmd_verbose("sudo /usr/sbin/nft add chain inet filter input '{ type filter hook input priority 0; policy drop; }'");
    run_cmd_verbose("sudo /usr/sbin/nft add chain inet filter zhiauth_dynamic");
    run_cmd_verbose("sudo /usr/sbin/nft add rule inet filter input jump zhiauth_dynamic");

    // 2. TẠO CÁC SET CHỐNG TRÙNG LẶP (Dùng double-quotes bọc nguyên câu lệnh để shell không nuốt ngoặc nhọn)
    run_cmd_verbose("sudo /usr/sbin/nft \"add set inet filter zhiauth_quic_ips { type ipv4_addr; }\"");
    run_cmd_verbose("sudo /usr/sbin/nft \"add set inet filter zhiauth_kcp_ips { type ipv4_addr; }\"");
    run_cmd_verbose("sudo /usr/sbin/nft \"add set inet filter zhiauth_icmp_ips { type ipv4_addr; }\"");

    // 3. DỌN SẠCH SETS & CHAIN CON MỖI LẦN BOOT DAEMON
    run_cmd_verbose("sudo /usr/sbin/nft flush set inet filter zhiauth_quic_ips");
    run_cmd_verbose("sudo /usr/sbin/nft flush set inet filter zhiauth_kcp_ips");
    run_cmd_verbose("sudo /usr/sbin/nft flush set inet filter zhiauth_icmp_ips");
    run_cmd_verbose("sudo /usr/sbin/nft flush chain inet filter zhiauth_dynamic");

    // 4. NẠP 3 RULE KHUNG CỐ ĐỊNH DUYỆT THEO SET
    run_cmd_verbose("sudo /usr/sbin/nft add rule inet filter zhiauth_dynamic ip saddr @zhiauth_quic_ips udp dport 4433 accept");
    run_cmd_verbose("sudo /usr/sbin/nft add rule inet filter zhiauth_dynamic ip saddr @zhiauth_kcp_ips udp dport 6666 accept");
    run_cmd_verbose("sudo /usr/sbin/nft add rule inet filter zhiauth_dynamic ip saddr @zhiauth_icmp_ips ip protocol icmp accept");

    is_running = true;
    worker_thread = std::thread(&UFWManager::worker_loop, this);
    ZHI_LOG_INFO("UFW Task Worker Thread initialized with Verbose NFTables Set Architecture.");
}

void UFWManager::stop_worker() {
    if (!is_running) return;
    is_running = false;
    cv.notify_one();
    
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    ZHI_LOG_INFO("UFW Task Worker Thread stopped cleanly.");
}

void UFWManager::push_task(const std::string& ip, int port, const std::string& proto, bool allow) {
    UFWTask task{ip, port, proto, allow};
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue.push(task);
    }
    cv.notify_one();
}

void UFWManager::worker_loop() {
    while (is_running) {
        UFWTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            cv.wait(lock, [this] { return !task_queue.empty() || !is_running; });

            if (!is_running && task_queue.empty()) break;

            task = task_queue.front();
            task_queue.pop();
        }

        execute_system_command(task);
    }
}

void UFWManager::execute_system_command(const UFWTask& task) {
    std::ostringstream oss;
    
    std::string set_name;
    if (task.protocol == "icmp") {
        set_name = "zhiauth_icmp_ips";
    } else if (task.port == 4433) {
        set_name = "zhiauth_quic_ips";
    } else {
        set_name = "zhiauth_kcp_ips";
    }

    if (task.is_allow_action) {
        // CÚ PHÁP THÊM IP VÀO SET
        oss << "sudo /usr/sbin/nft \"add element inet filter " << set_name << " { " << task.ip_address << " }\"";
    } else {
        // CÚ PHÁP XÓA IP KHỎI SET
        oss << "sudo /usr/sbin/nft \"delete element inet filter " << set_name << " { " << task.ip_address << " }\"";
    }

    run_cmd_verbose(oss.str());
}