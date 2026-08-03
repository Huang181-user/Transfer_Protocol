#ifndef ZHIAUTH_VFS_PACKET_H
#define ZHIAUTH_VFS_PACKET_H

#include <cstdint>

#pragma pack(push, 1)

enum class VfsOpcode : uint8_t {
    OP_PING     = 0x00,
    OP_STAT     = 0x01,
    OP_LIST     = 0x02,
    OP_READ     = 0x03,
    OP_WRITE    = 0x04,
    OP_MKDIR    = 0x05,
    OP_DELETE   = 0x06,
    OP_RENAME   = 0x07,
    OP_TRUNCATE = 0x08, 
    OP_ERROR    = 0xFF
};

// TRẢ LẠI NGUYÊN BẢN CHO V6.0
struct VfsPacketHeader {
    uint32_t magic;      // 0x5A484941 ("ZHIA")
    VfsOpcode opcode;
    uint64_t session_id; // Sẽ giấu cả client_id và req_id vào đây!
    uint64_t offset;     
    uint32_t data_len;
    uint16_t path_len;
};

#pragma pack(pop)

#endif // ZHIAUTH_VFS_PACKET_H