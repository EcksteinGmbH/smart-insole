#!/usr/bin/env bash
# 用项目内 venv 运行线性发电机测试工作站。
# 首次: uv venv tools/.venv && uv pip install --python tools/.venv/bin/python -r tools/requirements.txt
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$DIR/.venv/bin/python" -I "$DIR/generator_test.py" "$@"
