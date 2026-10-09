#!/usr/bin/env bash
# 非交互抓取设备串口日志（esp-idf-monitor >= 1.10 的脚本模式），带 ELF 回溯解码。
# 用法:
#   tools/capture.sh [秒数=20] [期望的正则(可选)] [--no-reset]
# 例:
#   tools/capture.sh 15                      # 复位后抓 15 秒
#   tools/capture.sh 30 "Advertising started" # 复位后等到匹配或 30 秒超时（超时退出码 110）
#   tools/capture.sh 60 "" --no-reset         # 不复位，直接旁听 60 秒
# 日志写入 logs/monitor_<时间>.log，同时打印到 stdout。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT/firmware"
IDF_EXPORT="${IDF_EXPORT:-$HOME/esp/esp-idf-v5.4.1/export.sh}"
PORT="${ESPPORT:-/dev/ttyACM0}"

SECS="${1:-20}"
EXPECT="${2:-}"
RESET=1
[[ "${3:-}" == "--no-reset" ]] && RESET=0

# shellcheck disable=SC1090
. "$IDF_EXPORT" >/dev/null 2>&1

ELF="$(ls "$APP_DIR"/build/*.elf 2>/dev/null | head -1 || true)"
mkdir -p "$ROOT/logs"
LOG="$ROOT/logs/monitor_$(date +%Y%m%d_%H%M%S).log"

set +e
{
  [[ $RESET -eq 1 ]] && echo "reset"
  if [[ -n "$EXPECT" ]]; then
    echo "expect --timeout $SECS $EXPECT"
  else
    echo "sleep $SECS"
  fi
  echo "exit"
} | idf-monitor --port "$PORT" --target esp32c3 \
      --toolchain-prefix riscv32-esp-elf- ${ELF:+"$ELF"} 2>&1 \
  | sed -u 's/\x1b\[[0-9;]*m//g' | tee "$LOG"
rc=${PIPESTATUS[1]}
echo "--- log saved: $LOG (monitor exit=$rc)"
exit "$rc"
