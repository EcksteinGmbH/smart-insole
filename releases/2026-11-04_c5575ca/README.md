# 智能鞋垫固件 2026-11-04

## 推荐文件

`smart-insole_2026-11-04_full.bin` 是完整刷机镜像，包含 bootloader、分区表和应用程序。正常交付、备份或整机刷写时使用它，从 Flash 地址 `0x0` 写入。

`smart-insole_2026-11-04_app.bin` 仅包含应用程序。只有明确需要单独更新应用时才使用它，从 Flash 地址 `0x10000` 写入。

## 版本信息

- 芯片：ESP32-C3，4 MB Flash
- ESP-IDF：v5.4.1
- 固件版本：`c5575ca`
- 构建日期：2026-11-04
- 真机验证：刷机成功，启动正常，非振动 BLE 自检 25/25 通过

## 完整镜像刷机命令

```bash
esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 smart-insole_2026-11-04_full.bin
```

刷机前可在本目录执行 `sha256sum -c SHA256SUMS.txt` 检查文件完整性。

本次真机烧录和功能检查结果见 [`烧录测试结果_2026-11-04.md`](烧录测试结果_2026-11-04.md)。
