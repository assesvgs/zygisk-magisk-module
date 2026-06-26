#include <android/native_bridge.h>
#include "ipc_client.hpp"
#include "logging.hpp"

void hook_entry();

static void on_unload() {
    zygisk_disconnect();
}

static void *on_load(const char *) {
    return nullptr;
}

static bool is_compatible(const char *caller) {
    // 注意：此处的 LOGI 是安全的；崩溃仅发生在 on_load/on_unload 回调中
    // 因为 VPhoneOS 在 native bridge 加载/卸载时 logd 不可用
    LOGI("isCompatible: installing PLT hooks");
    hook_entry();
    LOGI("isCompatible: done, returning false");
    return false;
}

static NativeBridgeCallbacks callbacks = {
    .version = 3,
    .isCompatible = is_compatible,
    .loadNativeBridge = on_load,
    .unloadNativeBridge = on_unload,
    .isSupported = nullptr,
    .getVersion = nullptr,
};

extern "C" NativeBridgeCallbacks NativeBridgeItf = callbacks;
