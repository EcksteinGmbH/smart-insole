# 智能鞋垫固件

ESP32-C3-MINI（RISC-V，单核，4MB flash）+ 2×LSM6DS3TR-C（I2C 0x6A/0x6B）+ 3 个振动电机（LEDC PWM），通过 NimBLE BLE 与手机通信。

**新会话先读 [docs/开发文档.md](docs/开发文档.md)**（背景、决策、变更记录、测试基线、待办）。

## 目录
- `firmware/` — ESP-IDF 工程（`main/src/app_*.c` 应用层，`components/` 驱动层）
- `硬件配置和软件功能说明V0.3.xlsx` — 引脚与功能需求（硬件规格的权威来源）
- `《智能鞋垫API文档V1.0.0》.docx` — BLE JSON 命令接口，已交付 App 工程师，**固件以它为准**
- `docs/开发文档.md` — 开发文档（每次重要改动后同步更新 §8 变更记录 / §10 待办）
- `docs/问题记录.md` — 问题清单（`ISS-xxx` 编号、状态、证据、方案）；**发现新问题先记到这里**，修复后更新状态和处理记录
- `firmware_backup/` — 出厂固件整片 flash 备份（`esptool write_flash 0 <bin>` 可恢复），勿删
- `tools/capture.sh` — 非交互串口日志抓取
- `tools/ble_test.sh` — BLE 主机端测试（scan / cmd / selftest / stream），venv 在 `tools/.venv`
- `tools/generator_test.sh` — 线性发电机工作站（WT9011DCL-RF 采集 / 分析 / T0-T1 对比）
- `.mcp.json` — 项目级 MCP：`espressif-docs`（官方芯片/IDF 文档，已认证；工具 search_espressif_sources，有限额，单次聚焦查询）
- `logs/` — 抓取的日志

## 环境
- ESP-IDF **v5.4.1**（与 `dependencies.lock` 一致），路径 `~/esp/esp-idf-v5.4.1`
- 每个 shell 先执行：`. ~/esp/esp-idf-v5.4.1/export.sh`（不写入 ~/.bashrc）
- 设备：USB-Serial/JTAG，`/dev/ttyACM0`（303a:1001）

## 命令（在 `firmware/` 下）
- 编译：`idf.py build`
- 烧录：`idf.py -p /dev/ttyACM0 flash` —— **烧录前必须征得用户同意**（`.claude/settings.local.json` 仅放行这一条命令的原样写法）
- 日志（根目录）：`tools/capture.sh <秒> [正则] [--no-reset]`（超时退出码 110）
- BLE（根目录）：`tools/ble_test.sh --address 30:ED:A0:80:9A:6E selftest`（全局参数放子命令前）
- 发电机测试（根目录）：`tools/generator_test.sh --help`（流程见 `docs/线性发电机测试工作站.md`）
- JTAG：后台 `idf.py openocd`，再 `riscv32-esp-elf-gdb -batch -ex "target extended-remote :3333" -ex "mon reset halt" ... build/xrs_code.elf`

## 规则
- Git：每完成一个独立改动就 `git commit`（小步、说明清楚）；`.git/hooks/post-commit` 会在后台自动 push 到 GitHub（EcksteinGmbH/smart-insole，德国侧据此查看代码），失败记录在 `.git/auto-push.log`
- **禁止**运行交互式 `idf.py monitor` / `menuconfig`（需要 TTY 会卡住）；看日志用 `tools/capture.sh`
- 改 Kconfig 选项：编辑 `sdkconfig` 后 `idf.py build`，或用 `idf.py menuconfig` 前先问用户
- ESP-IDF API 以本地 `~/esp/esp-idf-v5.4.1/components/*/include` 与 `examples/` 为准，不凭记忆、不用 v6 文档
- MCP / skills / 工具只装在本项目范围（`.mcp.json`、`.claude/`、`.vscode/`），不改全局配置
- 不要修改 `managed_components/`（由组件管理器生成）

## 已知注意点
- USB-Serial/JTAG 已是副控制台，USB 口可看应用日志；编辑 sdkconfig 后 `idf.py reconfigure` 并 diff
- 无 32 kHz 晶振：BLE 开启时自动 light sleep 不生效；静止时 IMU 陀螺掉电，BLE 帧中陀螺值冻结（详见开发文档 §9.1）
- `protocol.c` 等源文件为 CRLF 换行，编辑时保持
- 深度睡眠时 USB-Serial/JTAG 设备会从主机消失；开启了自动 light sleep（PM + tickless idle）
- BLE 协议所有字段值均为字符串；IMU 二进制流在 0xFFF4（`55 AA LEN + 2×6×float + sum`）
