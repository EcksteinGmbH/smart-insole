#!/usr/bin/env python3
"""智能鞋垫 BLE 主机端测试工具（基于 bleak，BlueZ）。

按《智能鞋垫API文档V1.0.0》校验 JSON 命令，并解析 0xFFF4 上的 IMU 二进制流。

用法（在项目根目录）:
  tools/ble_test.sh scan
  tools/ble_test.sh cmd '{"cmd":"info_get"}'
  tools/ble_test.sh selftest [--motors] [--robustness]
  tools/ble_test.sh stream --seconds 10
通用参数: --name ESP32C3_XRS | --address XX:XX:...  --timeout 秒

GATT（见 main/src/nimble.c）:
  0xFFF2 write   手机 -> 设备 JSON 命令
  0xFFF1 notify  设备 -> 手机 JSON 响应
  0xFFF4 notify  IMU 帧: 55 AA LEN [IMU0 6×float32][IMU1 6×float32] SUM
                 LEN = 1 + 24×(使能的IMU数)，SUM = (LEN..数据末) 字节和 & 0xFF
                 float 顺序: ax ay az (mg)  gx gy gz (mdps)，小端
"""
import argparse
import asyncio
import json
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

DEFAULT_NAME = "ESP32C3_XRS"


def uuid16(u):
    return f"0000{u:04x}-0000-1000-8000-00805f9b34fb"


CHAR_RESP = uuid16(0xFFF1)
CHAR_CMD = uuid16(0xFFF2)
CHAR_IMU = uuid16(0xFFF4)


# ---------------------------------------------------------------- 连接
async def find_device(args):
    if args.address:
        dev = await BleakScanner.find_device_by_address(args.address, timeout=args.timeout)
    else:
        dev = await BleakScanner.find_device_by_name(args.name, timeout=args.timeout)
    if dev is None:
        sys.exit(f"未找到设备 {args.address or args.name}（设备是否在广播/已被手机连接？）")
    return dev


async def acquire_mtu(client):
    # BlueZ 后端需要显式获取 MTU，否则 mtu_size 恒为 23
    backend = getattr(client, "_backend", None)
    if backend is not None and hasattr(backend, "_acquire_mtu"):
        try:
            await backend._acquire_mtu()
        except Exception as e:  # noqa: BLE001
            print(f"[warn] 获取 MTU 失败: {e}")
    return client.mtu_size


class JsonChannel:
    """FFF2 写 / FFF1 收，按花括号重组被拆包的 JSON 响应。"""

    def __init__(self, client, mtu):
        self.client = client
        self.chunk = max(20, mtu - 3)
        self.buf = bytearray()
        self.queue = asyncio.Queue()

    def on_notify(self, _char, data: bytearray):
        self.buf += data
        depth, start = 0, None
        for i, b in enumerate(self.buf):
            if b == ord("{"):
                if depth == 0:
                    start = i
                depth += 1
            elif b == ord("}") and depth:
                depth -= 1
                if depth == 0 and start is not None:
                    raw = bytes(self.buf[start:i + 1])
                    self.buf = self.buf[i + 1:]
                    self.queue.put_nowait(raw)
                    return self.on_notify(_char, bytearray())

    async def start(self):
        await self.client.start_notify(CHAR_RESP, self.on_notify)

    async def request(self, payload, timeout=3.0):
        data = payload if isinstance(payload, (bytes, bytearray)) else payload.encode()
        while not self.queue.empty():
            self.queue.get_nowait()
        for i in range(0, len(data), self.chunk):
            await self.client.write_gatt_char(CHAR_CMD, data[i:i + self.chunk], response=True)
        raw = await asyncio.wait_for(self.queue.get(), timeout)
        try:
            return json.loads(raw)
        except json.JSONDecodeError:
            return {"_raw": raw.decode(errors="replace")}


# ---------------------------------------------------------------- 自检
SCHEMA_MOTOR = ["cmd", "id", "freq", "duty", "times", "intensity", "run", "count", "err", "msg"]
SCHEMA_IMU_GET = ["cmd", "id", "enable", "freq", "acc_sleep_thresh", "acc_sleep_after_s", "err", "msg"]
SCHEMA_INFO = ["cmd", "bat", "voltage", "charging", "err", "msg"]


class Report:
    def __init__(self):
        self.rows = []

    def check(self, name, ok, detail=""):
        self.rows.append((name, ok, detail))
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  — {detail}" if detail else ""))

    def summary(self):
        failed = [r for r in self.rows if not r[1]]
        print(f"\n结果: {len(self.rows) - len(failed)}/{len(self.rows)} 通过")
        return 0 if not failed else 1


def all_str(resp):
    return all(isinstance(v, str) for v in resp.values())


def has_keys(resp, keys):
    missing = [k for k in keys if k not in resp]
    return not missing, ("缺少字段 " + ",".join(missing)) if missing else ""


async def selftest(ch, args):
    r = Report()

    async def req(obj, label, expect_reply=True):
        try:
            resp = await ch.request(json.dumps(obj, separators=(",", ":")))
        except asyncio.TimeoutError:
            if expect_reply:
                r.check(label, False, "无响应（超时）")
            else:
                print(f"    -> {json.dumps(obj, ensure_ascii=False)}\n    <- (无响应)")
            return None
        print(f"    -> {json.dumps(obj, ensure_ascii=False)}\n    <- {json.dumps(resp, ensure_ascii=False)}")
        return resp

    print("== info_get")
    resp = await req({"cmd": "info_get"}, "info_get 响应")
    if resp:
        ok, d = has_keys(resp, SCHEMA_INFO)
        r.check("info_get 字段齐全", ok, d)
        r.check("info_get err=0", resp.get("err") == "0")
        r.check("info_get bat 以 % 结尾", str(resp.get("bat", "")).endswith("%"))
        r.check("info_get charging ∈ yes/no", resp.get("charging") in ("yes", "no"))
        r.check("info_get 值均为字符串", all_str(resp))

    print("== imu_get")
    for i in ("0", "1"):
        resp = await req({"cmd": "imu_get", "id": i}, f"imu_get id={i} 响应")
        if resp:
            ok, d = has_keys(resp, SCHEMA_IMU_GET)
            r.check(f"imu_get id={i} 字段齐全(API文档)", ok, d)
            r.check(f"imu_get id={i} err=0", resp.get("err") == "0")
    resp = await req({"cmd": "imu_get", "id": "2"}, "imu_get id=2")
    if resp:
        r.check("imu_get id=2 → err=-1", resp.get("err") == "-1", resp.get("msg", ""))
    resp = await req({"cmd": "imu_get"}, "imu_get 无 id")
    if resp:
        r.check("imu_get 无 id → err=-2", resp.get("err") == "-2")

    print("== imu_set（使用 API 文档字段名，测试后恢复）")
    before = await req({"cmd": "imu_get", "id": "0"}, "imu_get 备份")
    resp = await req({"cmd": "imu_set", "id": "2", "acc_sleep_thresh": "500", "acc_sleep_after_s": "60"},
                     "imu_set 设置")
    if resp:
        r.check("imu_set err=0", resp.get("err") == "0")
        r.check("imu_set 回显 acc_sleep_thresh", "acc_sleep_thresh" in resp)
        r.check("imu_set 回显 acc_sleep_after_s", "acc_sleep_after_s" in resp)
    after = await req({"cmd": "imu_get", "id": "1"}, "imu_get 读回")
    if after:
        r.check("imu_set 后 acc_sleep_thresh 生效(=500)", after.get("acc_sleep_thresh") == "500",
                f"读回 {after.get('acc_sleep_thresh')}")
        r.check("imu_set 后 acc_sleep_after_s 生效(=60)", after.get("acc_sleep_after_s") == "60",
                f"读回 {after.get('acc_sleep_after_s')}")
    if before:
        thr = before.get("acc_sleep_thresh", "150")
        aft = before.get("acc_sleep_after_s", before.get("acc_sleep_after", "120"))
        await req({"cmd": "imu_set", "id": "2", "acc_sleep_thresh": thr, "acc_sleep_after_s": aft,
                   "acc_wake_thresh": thr, "acc_sleep_after": aft}, "imu_set 恢复")
    resp = await req({"cmd": "imu_set", "id": "3"}, "imu_set id=3")
    if resp:
        r.check("imu_set id=3 → err=-1", resp.get("err") == "-1", resp.get("msg", ""))

    print("== motor_get")
    for i in ("0", "1", "2"):
        resp = await req({"cmd": "motor_get", "id": i}, f"motor_get id={i}")
        if resp:
            ok, d = has_keys(resp, SCHEMA_MOTOR)
            r.check(f"motor_get id={i} 字段齐全", ok, d)
    resp = await req({"cmd": "motor_get", "id": "3"}, "motor_get id=3")
    if resp:
        r.check("motor_get id=3 → err=-1", resp.get("err") == "-1")
    resp = await req({"cmd": "motor_get"}, "motor_get 无 id")
    if resp:
        r.check("motor_get 无 id → err=-2", resp.get("err") == "-2")

    print("== motor_set 错误分支（不会振动）")
    resp = await req({"cmd": "motor_set", "id": "4", "freq": "1", "duty": "0.5", "times": "1"}, "motor_set id=4")
    if resp:
        r.check("motor_set id=4 → err=-1", resp.get("err") == "-1")
    resp = await req({"cmd": "motor_set", "id": "1"}, "motor_set 缺参数")
    if resp:
        r.check("motor_set 缺参数 → err=-3", resp.get("err") == "-3")

    if args.motors:
        print("== motor_set 实际振动（每个电机 2 次短振）")
        for i in ("0", "1", "2"):
            resp = await req({"cmd": "motor_set", "id": i, "freq": "2", "duty": "0.3", "times": "2",
                              "intensity": "600"}, f"motor_set id={i}")
            if resp:
                ok, d = has_keys(resp, SCHEMA_MOTOR)
                r.check(f"motor_set id={i} 字段齐全", ok, d)
                r.check(f"motor_set id={i} run=1", resp.get("run") == "1")
            await asyncio.sleep(1.2)
        resp = await req({"cmd": "motor_get", "id": "0"}, "motor_get 结束后")
        if resp:
            r.check("motor 0 完成 2 次后 run=0,count=2", resp.get("run") == "0" and resp.get("count") == "2",
                    f"run={resp.get('run')} count={resp.get('count')}")

    print("== 未知命令")
    resp = await req({"cmd": "no_such_cmd"}, "未知命令", expect_reply=False)
    r.check("未知命令不回包（当前实现）或回 err≠0", resp is None or resp.get("err") not in (None, "0"))

    if args.robustness:
        print("== 健壮性：数字类型字段（旧固件会因 valuestring=NULL 崩溃重启）")
        resp = await req({"cmd": "imu_get", "id": 0}, "imu_get id=数字0")
        r.check("数字类型 id 正常处理", bool(resp) and resp.get("err") == "0")
        await asyncio.sleep(0.5)
        r.check("设备仍连接", ch.client.is_connected)

    return r.summary()


# ---------------------------------------------------------------- IMU 流
async def stream(client, seconds, verbose):
    st = {"frames": 0, "bad_hdr": 0, "bad_sum": 0, "bad_len": 0, "sizes": {}, "last": None}

    def on_imu(_c, data: bytearray):
        st["sizes"][len(data)] = st["sizes"].get(len(data), 0) + 1
        if len(data) < 4 or data[0] != 0x55 or data[1] != 0xAA:
            st["bad_hdr"] += 1
            return
        ln = data[2]
        if len(data) != ln + 3:
            st["bad_len"] += 1
            return
        if (sum(data[2:-1]) & 0xFF) != data[-1]:
            st["bad_sum"] += 1
            return
        n = (ln - 1) // 4
        st["last"] = struct.unpack("<" + "f" * n, bytes(data[3:3 + 4 * n]))
        st["frames"] += 1
        if verbose:
            print(" ".join(f"{v:9.1f}" for v in st["last"]))

    await client.start_notify(CHAR_IMU, on_imu)
    t0 = time.monotonic()
    while time.monotonic() - t0 < seconds:
        await asyncio.sleep(1)
        if not verbose and st["last"]:
            v = st["last"]
            line = f"  t={time.monotonic() - t0:4.1f}s frames={st['frames']}"
            for k in range(len(v) // 6):
                a = v[6 * k:6 * k + 3]
                g = v[6 * k + 3:6 * k + 6]
                line += (f" | IMU{k} acc(mg)={a[0]:7.1f},{a[1]:7.1f},{a[2]:7.1f}"
                         f" gyr(dps)={g[0] / 1000:6.1f},{g[1] / 1000:6.1f},{g[2] / 1000:6.1f}")
            print(line)
    await client.stop_notify(CHAR_IMU)
    dt = time.monotonic() - t0
    print(f"\n有效帧 {st['frames']}，{st['frames'] / dt:.1f} 帧/秒；包长分布 {st['sizes']}；"
          f"坏帧头 {st['bad_hdr']} 长度不符 {st['bad_len']} 校验错 {st['bad_sum']}")
    if st["bad_len"] and min(st["sizes"]) <= 20:
        print("[提示] 包被截断到 20 字节：MTU 未协商成功（需 ≥55）")
    return 0 if st["frames"] else 1


# ---------------------------------------------------------------- main
async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default=DEFAULT_NAME)
    ap.add_argument("--address")
    ap.add_argument("--timeout", type=float, default=10.0)
    sub = ap.add_subparsers(dest="action", required=True)
    sub.add_parser("scan", help="扫描附近 BLE 设备")
    p = sub.add_parser("cmd", help="发送一条原始 JSON 命令")
    p.add_argument("json")
    p = sub.add_parser("selftest", help="按 API 文档逐条校验")
    p.add_argument("--motors", action="store_true", help="包含实际振动测试")
    p.add_argument("--robustness", action="store_true", help="发送数字类型字段（旧固件会重启）")
    p = sub.add_parser("stream", help="订阅 IMU 数据流")
    p.add_argument("--seconds", type=float, default=10)
    p.add_argument("-v", "--verbose", action="store_true", help="逐帧打印")
    args = ap.parse_args()

    if args.action == "scan":
        found = await BleakScanner.discover(timeout=args.timeout, return_adv=True)
        for dev, adv in sorted(found.values(), key=lambda x: -x[1].rssi):
            mark = "  <== 鞋垫" if (adv.local_name or dev.name) == args.name else ""
            print(f"{dev.address}  RSSI {adv.rssi:4d}  {adv.local_name or dev.name or ''}{mark}")
        return 0

    dev = await find_device(args)
    async with BleakClient(dev, timeout=args.timeout) as client:
        mtu = await acquire_mtu(client)
        print(f"已连接 {dev.address} ({dev.name})，MTU={mtu}")
        if args.action == "stream":
            return await stream(client, args.seconds, args.verbose)
        ch = JsonChannel(client, mtu)
        await ch.start()
        if args.action == "cmd":
            try:
                resp = await ch.request(args.json)
            except asyncio.TimeoutError:
                print("无响应（超时）")
                return 1
            print(json.dumps(resp, ensure_ascii=False, indent=2))
            return 0
        if args.action == "selftest":
            return await selftest(ch, args)
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
