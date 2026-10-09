#!/usr/bin/env bash
# 用项目内 venv（tools/.venv，含 bleak）运行 BLE 测试工具
# 首次: uv venv tools/.venv && uv pip install --python tools/.venv/bin/python bleak==1.1.1
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$DIR/.venv/bin/python" -I "$DIR/ble_test.py" "$@"
