SKIPUNZIP=1

if [ "$API" -lt 26 ]; then
  abort "! Minimal supported sdk is 26 (Android 8.0)"
fi

if [ -z "$MAGISK_VER_CODE" ]; then
  abort "! Only Magisk is supported currently"
fi

case "$ARCH" in
  arm64) B=arm64-v8a; L=lib64;;
  arm)   B=armeabi-v7a; L=lib;;
  x64)   B=x86_64; L=lib64;;
  x86)   B=x86; L=lib;;
  *) abort "! Unsupported platform: $ARCH";;
esac

for f in module.prop post-fs-data.sh service.sh uninstall.sh sepolicy.rule; do
  unzip -o "$ZIPFILE" "$f" -d "$MODPATH" || abort "! Failed to extract $f"
done

mkdir -p "$MODPATH/bin"
unzip -o "$ZIPFILE" "lib/$B/libzygisk.so" -d "$MODPATH" || abort "! Failed to extract libzygisk.so"
mv -f "$MODPATH/lib/$B/libzygisk.so" "$MODPATH/libzygisk.so" || abort "! Failed to move libzygisk.so"
rm -rf "$MODPATH/lib"
unzip -o "$ZIPFILE" "bin/$B/zygiskd" -d "$MODPATH" || abort "! Failed to extract zygiskd"
mv -f "$MODPATH/bin/$B/zygiskd" "$MODPATH/zygiskd" || abort "! Failed to move zygiskd"
rm -rf "$MODPATH/bin/$B"

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/libzygisk.so" 0 0 0644
set_perm "$MODPATH/zygiskd" 0 0 0755

ui_print ""
ui_print "! 安装完成，请重启设备以激活 Zygisk"
ui_print ""
