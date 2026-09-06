#ifndef ZHIAUTH_UFW_MANAGER_H
#define ZHIAUTH_UFW_MANAGER_H

#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

struct UFWTask {
    std::string ip_address;
    int port;
    std::string protocol; // "udp" hoặc "tcp"
    bool is_allow_action; // true = OPEN (allow), false = CLOSE (delete allow)
};

class UFWManager {
public:
    UFWManager();
    ~UFWManager();

    // Kích hoạt luồng công nhân chạy ngầm
    void start_worker();
    
    // Dừng luồng an toàn khi tắt daemon
    void stop_worker();
    
    // Đẩy một yêu cầu đóng/mở port vào hàng đợi tuyến tính
    void push_task(const std::string& ip, int port, const std::string& proto, bool allow);

private:
    // Vòng lặp vô tận của Worker Thread gắp việc ra làm tuần tự
    void worker_loop();
    
    // Hàm thực thi tương tác lệnh hệ thống thô qua ubuntu_admin
    void execute_system_command(const UFWTask& task);

    std::queue<UFWTask> task_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::thread worker_thread;
    std::atomic<bool> is_running;
};

#endif // ZHIAUTH_UFW_MANAGER_H
