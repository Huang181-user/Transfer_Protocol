#ifndef ZHIAUTH_MSQUIC_SERVER_H
#define ZHIAUTH_MSQUIC_SERVER_H
#include <msquic.h>
#include <string>
#include <vector>
#include <thread>

class MsQuicServer {
public:
    static bool initialize(uint16_t auth_port, uint16_t data_port, const std::string& cert_path, const std::string& key_path);
    static void shutdown();
private:
    static QUIC_STATUS QUIC_API AuthStreamCallback(HQUIC Stream, void* Context, QUIC_STREAM_EVENT* Event);
    static QUIC_STATUS QUIC_API AuthConnCallback(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event);
    static QUIC_STATUS QUIC_API AuthListenerCallback(HQUIC Listener, void* Context, QUIC_LISTENER_EVENT* Event);

    static QUIC_STATUS QUIC_API DataStreamCallback(HQUIC Stream, void* Context, QUIC_STREAM_EVENT* Event);
    static QUIC_STATUS QUIC_API DataConnCallback(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event);
    static QUIC_STATUS QUIC_API DataListenerCallback(HQUIC Listener, void* Context, QUIC_LISTENER_EVENT* Event);

    static void ProxyUdsTask(HQUIC Stream, std::string uds_path, std::vector<uint8_t> payload);
};
#endif
