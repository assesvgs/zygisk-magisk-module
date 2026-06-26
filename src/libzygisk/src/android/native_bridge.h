#pragma once

#include <stdint.h>
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

// Minimal native bridge struct for Zygisk standalone module
// Adapted from AOSP: art/libnativebridge/include/nativebridge/native_bridge.h

typedef void* native_bridge_dl_handle_t;

typedef struct {
    uint32_t version;
    void* padding[5];
    bool (*isCompatible)(const char* caller);
    native_bridge_dl_handle_t (*loadNativeBridge)(const char* nativeBridgeLibrary);
    void* (*getTrampoline)(native_bridge_dl_handle_t handle, const char* name, const char* shorty, uint32_t len);
    void (*unloadNativeBridge)();
    bool (*isNativeBridgeLoaded)();
    void (*onUnloadEvents)();
    void* (*getAppEnv)(void* appNamespace);
    bool (*isSupported)(const char* lib);
    native_bridge_dl_handle_t (*loadNativeBridgeWithNamespace)(const char* nativeBridgeLibrary, const char* name);
    bool (*getVersion)(native_bridge_dl_handle_t handle, void* version_info);
} NativeBridgeCallbacks;

typedef struct {
    const char* (*getMethodShorty)(JNIEnv* env, jmethodID mid);
    uint32_t (*getNativeMethodCount)(JNIEnv* env, jclass clazz);
    uint32_t (*getNativeMethods)(JNIEnv* env, jclass clazz, JNINativeMethod* methods, uint32_t method_count);
} NativeBridgeRuntimeCallbacks;

#ifdef __cplusplus
}
#endif
