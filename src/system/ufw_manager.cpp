#include "system/ufw_manager.h"
#include "common/logger.h"
#include <cstdlib>
#include <sstream>

UFWManager::UFWManager() : is_running(false) {}

UFWManager::~UFWManager() {
    stop_worker();
}

void UFWManager::start_worker() {
    if (is_running) return;
    is_running = true;
    worker_thread = std::thread(&UFWManager::worker_loop, this);
    ZHI_LOG_INFO("UFW Task Worker Thread successfully initialized and running background loop.");
}

void UFWManager::stop_worker() {
    if (!is_running) return;
    is_running = false;
    cv.notify_one(); // Đánh thức luồng dậy để nó tự thoát kết thúc vòng lặp
    
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    ZHI_LOG_INFO("UFW Task Worker Thread stopped cleanly. Resources deallocated.");
}

void UFWManager::push_task(const std::string& ip, int port, const std::string& proto, bool allow) {
    UFWTask task{ip, port, proto, allow};
    {
        // Khóa mutex để đẩy task vào hàng đợi an toàn, không sợ luồng khác ghi đè
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue.push(task);
    }
    
    ZHI_LOG_DEBUG("New UFW firewall rule queued -> Action: " + std::string(allow ? "ALLOW" : "DELETE") + 
                 " | Client IP: " + ip + " | Target Port: " + std::to_string(port) + " | Protocol: " + proto);
                 
    cv.notify_one(); // Báo hiệu cho Worker Thread đang ngủ thức dậy làm việc
}

void UFWManager::worker_loop() {
    while (is_running) {
        UFWTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            // Ngủ đông chờ cho đến khi có task mới hoặc có lệnh dừng hệ thống
            cv.wait(lock, [this] { return !task_queue.empty() || !is_running; });

            if (!is_running && task_queue.empty()) {
                break;
            }

            // Gắp tác vụ đầu hàng đợi ra xử lý
            task = task_queue.front();
            task_queue.pop();
        }

        // Thực thi lệnh độc quyền tuyến tính, triệt tiêu 100% Race Condition
        execute_system_command(task);
    }
}

void UFWManager::execute_system_command(const UFWTask& task) {
    std::ostringstream oss;
    
    // Ráp chuỗi lệnh hệ thống dựa trên tệp đường dẫn nhị phân đã cấu hình trong sudoers
    if (task.is_allow_action) {
        oss << "sudo /usr/sbin/ufw allow from " << task.ip_address 
            << " to any port " << task.port 
            << " proto " << task.protocol;
    } else {
        oss << "sudo /usr/sbin/ufw delete allow from " << task.ip_address 
            << " to any port " << task.port 
            << " proto " << task.protocol;
    }

    std::string cmd = oss.str();
    ZHI_LOG_INFO("Worker thread executing system infrastructure rule command: '" + cmd + "'");
    
    // Ném thẳng lệnh xuống nhân Linux Kernel
    int status = std::system(cmd.c_str());
    
    if (status == 0) {
        ZHI_LOG_INFO("Firewall modification successfully applied to OS environment network table.");
    } else {
        ZHI_LOG_ERR("OS firewall command execution failed with system status error code: " + std::to_string(status));
    }
}
