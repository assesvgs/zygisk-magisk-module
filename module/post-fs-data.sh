#!/system/bin/sh
MODDIR=${0%/*}
log -t Zygisk "post-fs-data: start"

mkdir -p /data/adb/zygisk

# 修复 VPhoneOS 无 FUSE 的 sdcard：bind mount /data/media/0 到 /mnt/user/0/primary
# 符号链接链：/sdcard -> /storage/self/primary -> /storage/emulated/0 -> /mnt/user/0/primary -> /data/media/0
if [ -d /data/media/0 ] && ! grep -q ' /mnt/user/0/primary ' /proc/mounts 2>/dev/null; then
    mkdir -p /mnt/user/0/primary 2>/dev/null
    mount --bind /data/media/0 /mnt/user/0/primary 2>/dev/null
    # 删除 VPhoneOS 宿主机泄露的符号链接（指向宿主机的 /storage/emulated/0）
    rm -f /mnt/user/0/primary/0 2>/dev/null
    log -t Zygisk "Bound /data/media/0 -> /mnt/user/0/primary"
fi

# 补全符号链接链（每次启动运行）
if [ -d /mnt/user/0/primary ]; then
    mkdir -p /storage/emulated 2>/dev/null
    ln -sf /mnt/user/0/primary /storage/emulated/0 2>/dev/null
    log -t Zygisk "Linked /storage/emulated/0 -> /mnt/user/0/primary"
    rm -rf /storage/self/primary 2>/dev/null
    ln -sf /storage/emulated/0 /storage/self/primary 2>/dev/null
    log -t Zygisk "Linked /storage/self/primary -> /storage/emulated/0"
fi

# 修复权限以匹配标准 Android
chmod 771 /storage/emulated/ 2>/dev/null
chown root:sdcard_rw /storage/emulated/ 2>/dev/null
log -t Zygisk "Fixed /storage/emulated/ permissions"

# Mount libzygisk.so to system lib path (no Magisk overlay = no sdcard breakage)
LIBDIR=/system/lib64
[ ! -d $LIBDIR ] && LIBDIR=/system/lib
mount --bind "$MODDIR/libzygisk.so" $LIBDIR/libzygisk.so 2>/dev/null
chcon u:object_r:system_lib_file:s0 $LIBDIR/libzygisk.so 2>/dev/null
log -t Zygisk "Mounted libzygisk.so to $LIBDIR"

# Use bare name (standard approach) or fallback to absolute path if bind mount fails
if grep -q " $LIBDIR/libzygisk.so " /proc/mounts 2>/dev/null; then
    resetprop ro.dalvik.vm.native.bridge libzygisk.so
    log -t Zygisk "Using native bridge via bind mount: libzygisk.so"
else
    resetprop ro.dalvik.vm.native.bridge "$MODDIR/libzygisk.so"
    log -t Zygisk "Bind mount failed, fallback to absolute path"
fi

log -t Zygisk "Starting zygiskd daemon"
"$MODDIR/zygiskd" daemon &

log -t Zygisk "post-fs-data: done"
