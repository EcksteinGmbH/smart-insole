# 智能鞋垫固件 2026-11-04 16:05

## 目录说明

- `01_full_flash/`：推荐使用的完整镜像，包含 bootloader、分区表和应用程序。
- `02_separate_flash/`：需要分别刷写时使用的配套文件和地址说明。
- `03_test_results/`：与本固件对应的烧录报告和原始启动日志。

不要移动或整理 `firmware/build/` 内部文件；该目录由 ESP-IDF 管理。

## 版本信息

- 芯片：ESP32-C3，4 MB Flash
- ESP-IDF：v5.4.1
- 固件版本：`c5575ca`
- 构建日期：2026-11-04
- 真机验证：刷机成功，启动正常，非振动 BLE 自检 25/25 通过

## 完整镜像刷机命令

```bash
esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 01_full_flash/smart-insole_full.bin
```

刷机前可在本目录执行 `sha256sum -c SHA256SUMS.txt` 检查文件完整性。

本次真机烧录和功能检查结果见 [`03_test_results/烧录测试结果.md`](03_test_results/烧录测试结果.md)。
