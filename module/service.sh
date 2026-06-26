#!/system/bin/sh
MODDIR=${0%/*}
log -t Zygisk "service.sh: start"

if pgrep -x zygiskd >/dev/null 2>&1; then
  log -t Zygisk "zygiskd already running"
  exit 0
fi

log -t Zygisk "zygiskd not running, respawning"
exec "$MODDIR/bin/zygiskd" daemon
