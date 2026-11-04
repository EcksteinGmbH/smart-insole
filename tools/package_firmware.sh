#!/usr/bin/env bash
# 把 ESP-IDF build 中真正需要交付的文件按“构建时间_固件版本”归档到 releases/。
# firmware/build/ 是编译系统工作目录，不在其中移动或重命名任何文件。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="$ROOT/firmware"
BUILD_DIR="$FIRMWARE_DIR/build"
IDF_EXPORT="${IDF_EXPORT:-$HOME/esp/esp-idf-v5.4.1/export.sh}"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
用法: tools/package_firmware.sh

先执行 idf.py build，再运行本脚本。脚本会生成：
  releases/<构建日期_时间>_<固件版本>/
    01_full_flash/       完整镜像，从 0x0 刷写
    02_separate_flash/   bootloader、分区表和应用镜像
    03_test_results/     放置该版本的测试报告与日志

已有同名归档不会被覆盖；带 `-dirty` 标记的未提交构建不会被归档。
EOF
  exit 0
fi

if [[ $# -ne 0 ]]; then
  echo "错误：不支持参数。请运行 tools/package_firmware.sh --help" >&2
  exit 2
fi

required_files=(
  "$BUILD_DIR/project_description.json"
  "$BUILD_DIR/xrs_code.bin"
  "$BUILD_DIR/bootloader/bootloader.bin"
  "$BUILD_DIR/partition_table/partition-table.bin"
  "$BUILD_DIR/flash_args"
)
for file in "${required_files[@]}"; do
  if [[ ! -f "$file" ]]; then
    echo "错误：缺少构建产物 $file" >&2
    echo "请先在 firmware/ 下激活 ESP-IDF v5.4.1 并运行 idf.py build。" >&2
    exit 1
  fi
done

if [[ ! -f "$IDF_EXPORT" ]]; then
  echo "错误：找不到 ESP-IDF 环境脚本 $IDF_EXPORT" >&2
  exit 1
fi

readarray -t metadata < <(
  python3 - "$BUILD_DIR/project_description.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as file:
    data = json.load(file)
print(data["project_version"])
print(data["target"])
PY
)
PROJECT_VERSION="${metadata[0]}"
TARGET="${metadata[1]}"

if [[ "$PROJECT_VERSION" == *-dirty* ]]; then
  echo "错误：当前构建版本为 $PROJECT_VERSION，包含未提交改动，不归档。" >&2
  echo "请先提交源码并重新执行 idf.py build，确保固件版本可追溯。" >&2
  exit 4
fi

BUILD_EPOCH="$(stat -c %Y "$BUILD_DIR/xrs_code.bin")"
BUILD_STAMP="$(date -d "@$BUILD_EPOCH" +%Y-%m-%d_%H%M)"
RELEASE_ID="${BUILD_STAMP}_${PROJECT_VERSION}"
RELEASE_DIR="$ROOT/releases/$RELEASE_ID"

if [[ -e "$RELEASE_DIR" ]]; then
  echo "错误：归档已存在，不会覆盖：$RELEASE_DIR" >&2
  exit 3
fi

FULL_IMAGE="$BUILD_DIR/xrs_code_full.bin"
# 直接合并现有构建产物，不调用 idf.py，避免打包动作触发重新编译或改变固件版本。
# shellcheck disable=SC1090
. "$IDF_EXPORT" >/dev/null 2>&1
(
  cd "$BUILD_DIR"
  python -m esptool --chip "$TARGET" merge_bin -o "$FULL_IMAGE" -f raw @flash_args
)

FULL_DIR="$RELEASE_DIR/01_full_flash"
PARTS_DIR="$RELEASE_DIR/02_separate_flash"
TEST_DIR="$RELEASE_DIR/03_test_results"
mkdir -p "$FULL_DIR" "$PARTS_DIR" "$TEST_DIR"

install -m 0644 "$FULL_IMAGE" "$FULL_DIR/smart-insole_full.bin"
install -m 0644 "$BUILD_DIR/xrs_code.bin" "$PARTS_DIR/smart-insole_app.bin"
install -m 0644 "$BUILD_DIR/bootloader/bootloader.bin" "$PARTS_DIR/bootloader.bin"
install -m 0644 "$BUILD_DIR/partition_table/partition-table.bin" "$PARTS_DIR/partition-table.bin"

cat >"$PARTS_DIR/FLASH_ADDRESSES.txt" <<'EOF'
0x00000000  bootloader.bin
0x00008000  partition-table.bin
0x00010000  smart-insole_app.bin
EOF

cat >"$TEST_DIR/README.md" <<'EOF'
# 测试结果

此目录用于保存与本次构建对应的烧录报告、启动日志、BLE 自检和数据流测试结果。
打包脚本不会自动把旧日志放入新版本，避免测试证据与固件版本错配。
EOF

cat >"$RELEASE_DIR/README.md" <<EOF
# 智能鞋垫固件 $RELEASE_ID

- 构建时间：$BUILD_STAMP
- 固件版本：\`$PROJECT_VERSION\`
- 目标芯片：\`$TARGET\`
- ESP-IDF：v5.4.1

## 目录

- \`01_full_flash/\`：推荐交付文件，完整镜像从地址 \`0x0\` 写入。
- \`02_separate_flash/\`：bootloader、分区表、应用镜像及各自地址。
- \`03_test_results/\`：该版本的测试报告和原始日志；新归档初始为空。

刷机或交付前，在本目录执行 \`sha256sum -c SHA256SUMS.txt\`。
EOF

(
  cd "$RELEASE_DIR"
  sha256sum \
    01_full_flash/smart-insole_full.bin \
    02_separate_flash/bootloader.bin \
    02_separate_flash/partition-table.bin \
    02_separate_flash/smart-insole_app.bin \
    > SHA256SUMS.txt
)

echo "固件归档已生成：$RELEASE_DIR"
