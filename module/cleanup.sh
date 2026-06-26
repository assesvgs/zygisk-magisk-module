if [ -f /data/adb/modules/zygisk/disable ]; then
  rm /data/adb/service.d/.zygisk_cleanup.sh 2>/dev/null
fi
