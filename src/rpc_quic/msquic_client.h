#pragma once
#include "msquic.h" // Dung file header local tai tu Microsoft
#include <string>
#include <vector>
#include <future>

class MsQuicClient {
public:
    static bool initialize(const std::string& server_ip, uint16_t auth_port, uint16_t data_port);
    static void shutdown();
    static std::string auth_sync(const std::string& payload);
    static std::vector<uint8_t> send_vfs_sync(const std::vector<uint8_t>& payload, uint32_t req_id);
private:
    static QUIC_STATUS QUIC_API RpcStreamCallback(HQUIC Stream, void* Context, QUIC_STREAM_EVENT* Event);
    static QUIC_STATUS QUIC_API AuthStreamCallback(HQUIC Stream, void* Context, QUIC_STREAM_EVENT* Event);
    static QUIC_STATUS QUIC_API ClientConnCallback(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event);
};
