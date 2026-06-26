#pragma once

#include <jni.h>
#include "logging.hpp"
// Standalone module: no Magisk internal headers

#ifdef MAGISK_DEBUG
#include <sys/sysmacros.h>
#endif

#define ZYGISKLDR       "libzygisk.so"
#define NBPROP          "ro.dalvik.vm.native.bridge"

#if defined(__LP64__)
#define ZLOGD(...) LOGD("zygisk64: " __VA_ARGS__)
#define ZLOGE(...) LOGE("zygisk64: " __VA_ARGS__)
#define ZLOGI(...) LOGI("zygisk64: " __VA_ARGS__)
#define ZLOGW(...) LOGW("zygisk64: " __VA_ARGS__)
#else
#define ZLOGD(...) LOGD("zygisk32: " __VA_ARGS__)
#define ZLOGE(...) LOGE("zygisk32: " __VA_ARGS__)
#define ZLOGI(...) LOGI("zygisk32: " __VA_ARGS__)
#define ZLOGW(...) LOGW("zygisk32: " __VA_ARGS__)
#endif

// Extreme verbose logging
// #define ZLOGV(...) ZLOGD(__VA_ARGS__)
#define ZLOGV(...) (void*)0

#ifdef MAGISK_DEBUG
#define DIAG(tag) do { \
    char _ns[64] = "?", _init[64] = "?"; \
    readlink("/proc/self/ns/mnt", _ns, sizeof(_ns)-1); \
    readlink("/proc/1/ns/mnt", _init, sizeof(_init)-1); \
    struct stat _st; int _ss = stat("/storage", &_st); \
    char _sd[64] = "?", _sp[64] = "?", _ms[32] = "?"; \
    readlink("/sdcard", _sd, sizeof(_sd)-1); \
    readlink("/storage/self/primary", _sp, sizeof(_sp)-1); \
    FILE *_f = fopen("/proc/self/mountinfo", "re"); \
    if (_f) { char _b[512]; while (fgets(_b, sizeof(_b), _f)) \
        if (strstr(_b, " /storage ")) { char *_p = strstr(_b, "shared:"); \
        if (_p) { size_t _l = strcspn(_p, " "); if (_l > 23) _l = 23; \
        memcpy(_ms, _p, _l); _ms[_l] = 0; } break; } \
        fclose(_f); } \
    ZLOGD("D%d p=%d u=%d n=%s in=%s " \
          "sa=%d sl=%s pa=%d pl=%s " \
          "ea=%d da=%d ss=%d mj=%d mn=%d " \
          "sh=%d ms=%s\n", \
        (tag), getpid(), getuid(), _ns, _init, \
        access("/sdcard", F_OK) == 0, _sd, \
        access("/storage/self/primary", F_OK) == 0, _sp, \
        access("/storage/emulated/0", F_OK) == 0, \
        access("/data/media/0", F_OK) == 0, \
        _ss == 0 ? (int)(_st.st_mode & S_IFMT) : -1, \
        _ss == 0 ? (int)major(_st.st_dev) : -1, \
        _ss == 0 ? (int)minor(_st.st_dev) : -1, \
        access("/share", F_OK) == 0, _ms); \
} while(0)
#else
#define DIAG(tag) do {} while(0)
#endif

void hook_entry();
void hookJniNativeMethods(JNIEnv *env, const char *clz, JNINativeMethod *methods, int numMethods);

// The reference of the following structs
// https://cs.android.com/android/platform/superproject/main/+/main:art/libnativebridge/include/nativebridge/native_bridge.h

struct NativeBridgeRuntimeCallbacks {
    const char* (*getMethodShorty)(JNIEnv* env, jmethodID mid);
    uint32_t (*getNativeMethodCount)(JNIEnv* env, jclass clazz);
    uint32_t (*getNativeMethods)(JNIEnv* env, jclass clazz, JNINativeMethod* methods,
                                 uint32_t method_count);
};

struct NativeBridgeCallbacks {
    uint32_t version;
    void *padding[5];
    bool (*isCompatibleWith)(uint32_t);
};
