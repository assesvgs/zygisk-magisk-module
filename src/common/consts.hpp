#pragma once

#define ZYGISK_SOCKET_PATH "/data/adb/zygisk/zygiskd.sock"

enum class ZygiskRequest : int32_t {
    GetInfo = 0,
    GetModuleDir = 1,
    ConnectCompanion = 2,
};

enum class ZygiskStateFlags : uint32_t {
    ProcessGrantedRoot = 0x01,
    ProcessOnDenyList = 0x02,
    DenyListEnforced = 0x04,
    ProcessIsMagiskApp = 0x08,
};

#pragma pack(push, 1)
struct zygisk_request_header_t {
    int32_t opcode;
    int32_t pid;
};
struct zygisk_response_header_t {
    int32_t code;
    int32_t uid;
    uint32_t flags;
};
#pragma pack(pop)
