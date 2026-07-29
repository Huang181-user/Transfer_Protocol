// --- client.h ---
#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <vector>
#include <winsock2.h>
#include "ikcp.h"

class ZhiClient {
public:
    ZhiClient();
    ~ZhiClient();

    bool start(const std::string& ip, int port, const std::string& key);
    void stop();

    // Hàm test gửi yêu cầu Ping-Pong lên Server qua hầm KCP mã hóa
    bool test_connection();

    static int kcp_output_cb(const char* buf, int len, ikcpcb* kcp, void* user);

private:
    SOCKET udp_socket;
    sockaddr_in server_addr;
    ikcpcb* kcp_cb;
    std::string master_key;
    bool is_running;

    void run_update_loop();
};

#endif // CLIENT_H