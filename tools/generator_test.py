#!/usr/bin/env python3
"""线性发电机测试工作站。

采集 WT9011DCL-RF 串口加速度，登记万用表读数，并生成可追溯的
单次试验报告和结构改造前/后的聚合对比。分析与模拟仅依赖标准库；
实物采集额外依赖 pyserial。
"""

import argparse
import csv
import html
import json
import math
import random
import re
import statistics
import struct
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path


PACKET_SIZE = 29
DEFAULT_BAUD = 460800
CSV_FIELDS = [
    "host_time_iso",
    "elapsed_s",
    "phase",
    "device_id",
    "ax_g",
    "ay_g",
    "az_g",
    "gx_dps",
    "gy_dps",
    "gz_dps",
    "hx_raw",
    "hy_raw",
    "hz_raw",
    "roll_deg",
    "pitch_deg",
    "yaw_deg",
    "battery_v",
]


class WitRfParser:
    """解析 WT9011DCL-RF 默认 0x55 0x61、29 字节数据帧。"""

    def __init__(self):
        self.buffer = bytearray()
        self.discarded_bytes = 0

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= PACKET_SIZE:
            if self.buffer[1:3] != b"\x55\x61":
                del self.buffer[0]
                self.discarded_bytes += 1
                continue
            raw = bytes(self.buffer[:PACKET_SIZE])
            del self.buffer[:PACKET_SIZE]
            values = struct.unpack_from("<13h", raw, 3)
            frames.append(
                {
                    "device_id": raw[0],
                    "ax_g": values[0] / 32768.0 * 16.0,
                    "ay_g": values[1] / 32768.0 * 16.0,
                    "az_g": values[2] / 32768.0 * 16.0,
                    "gx_dps": values[3] / 32768.0 * 2000.0,
                    "gy_dps": values[4] / 32768.0 * 2000.0,
                    "gz_dps": values[5] / 32768.0 * 2000.0,
                    "hx_raw": values[6],
                    "hy_raw": values[7],
                    "hz_raw": values[8],
                    "roll_deg": values[9] / 32768.0 * 180.0,
                    "pitch_deg": values[10] / 32768.0 * 180.0,
                    "yaw_deg": values[11] / 32768.0 * 180.0,
                    "battery_v": values[12] / 100.0,
                }
            )
        return frames


def now_iso():
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def slug(value):
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", value.strip()).strip("._-")
    return cleaned or "unnamed"


def phase_at(elapsed, pre_seconds, active_seconds):
    if elapsed < pre_seconds:
        return "static_pre"
    if elapsed < pre_seconds + active_seconds:
        return "active"
    return "static_post"


def default_meter_unit(mode):
    return "mA" if mode == "short" else "V"


def create_trial_dir(args):
    if getattr(args, "trial_dir", None):
        path = Path(args.trial_dir).expanduser().resolve()
    else:
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        name = f"{stamp}_{args.mode}_r{args.repeat:02d}"
        path = (
            Path(args.output_root).expanduser().resolve()
            / slug(args.generator)
            / slug(args.revision)
            / name
        )
    path.mkdir(parents=True, exist_ok=False)
    return path


def manifest_from_args(args, source):
    unit = args.meter_unit or default_meter_unit(args.mode)
    return {
        "schema_version": 1,
        "created_at": now_iso(),
        "source": source,
        "generator": args.generator,
        "revision": args.revision,
        "mode": args.mode,
        "repeat": args.repeat,
        "waveform": args.waveform,
        "test_point": args.test_point,
        "operator": args.operator,
        "notes": args.notes,
        "meter": {
            "model": args.meter_model,
            "range": args.meter_range,
            "value": args.meter_value,
            "unit": unit,
            "recording": "manual_single_value",
        },
        "motion": {
            "pre_seconds": args.pre_seconds,
            "active_seconds": args.active_seconds,
            "post_seconds": args.post_seconds,
            "target_frequency_hz": args.target_frequency,
            "sensor_bandwidth_hz": args.sensor_bandwidth,
        },
        "sensor": {
            "model": "WT9011DCL-RF",
            "device_id": getattr(args, "device_id", None),
            "acceleration_range_g": 16,
        },
    }


def ask_meter_value(args):
    if args.meter_value is not None:
        return args.meter_value
    if not sys.stdin.isatty():
        raise SystemExit("缺少 --meter-value；非交互运行时必须显式提供万用表读数")
    unit = args.meter_unit or default_meter_unit(args.mode)
    label = {"open": "开路电压", "short": "短路电流", "load": "负载电压"}[args.mode]
    while True:
        try:
            return float(input(f"请输入{label} ({unit}): ").strip())
        except ValueError:
            print("请输入数字。", file=sys.stderr)


def write_raw_csv(path, rows):
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def read_raw_csv(path):
    numeric = set(CSV_FIELDS) - {"host_time_iso", "phase"}
    rows = []
    with path.open(encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            converted = dict(row)
            for key in numeric:
                converted[key] = int(row[key]) if key == "device_id" else float(row[key])
            rows.append(converted)
    return rows


def list_serial_ports():
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise SystemExit(
            "缺少 pyserial。请执行: uv pip install --python tools/.venv/bin/python -r tools/requirements.txt"
        ) from exc
    return list(list_ports.comports())


def resolve_port(requested):
    if requested:
        return requested
    ports = list_serial_ports()
    ch340 = [p.device for p in ports if p.vid == 0x1A86]
    if len(ch340) == 1:
        return ch340[0]
    if len(ports) == 1:
        return ports[0].device
    found = ", ".join(p.device for p in ports) or "无"
    raise SystemExit(f"无法唯一确定 RF 主机串口（发现: {found}），请使用 --port 指定")


def capture_rows(args):
    try:
        import serial
    except ImportError as exc:
        raise SystemExit(
            "缺少 pyserial。请执行: uv pip install --python tools/.venv/bin/python -r tools/requirements.txt"
        ) from exc

    port = resolve_port(args.port)
    parser = WitRfParser()
    rows = []
    selected_id = args.device_id
    total = args.pre_seconds + args.active_seconds + args.post_seconds
    phase_seen = None
    print(f"打开 {port} @ {args.baud}，总采集 {total:.1f} s")
    with serial.Serial(port, args.baud, timeout=0.1) as device:
        device.reset_input_buffer()
        start_mono = time.monotonic()
        while True:
            elapsed = time.monotonic() - start_mono
            if elapsed >= total:
                break
            phase = phase_at(elapsed, args.pre_seconds, args.active_seconds)
            if phase != phase_seen:
                prompts = {
                    "static_pre": "保持静止（采集零偏）",
                    "active": "开始按目标节奏手摇",
                    "static_post": "停止并保持静止",
                }
                print(f"[{elapsed:6.2f}s] {prompts[phase]}")
                phase_seen = phase
            data = device.read(device.in_waiting or 1)
            for frame in parser.feed(data):
                if selected_id is None:
                    selected_id = frame["device_id"]
                    print(f"自动选择 RF 设备 ID {selected_id}")
                if frame["device_id"] != selected_id:
                    continue
                frame.update(
                    {
                        "host_time_iso": now_iso(),
                        "elapsed_s": round(time.monotonic() - start_mono, 6),
                        "phase": phase,
                    }
                )
                rows.append(frame)
    if not rows:
        raise SystemExit("未收到有效 WT9011DCL-RF 数据帧；请检查串口、波特率、RF 配对和设备 ID")
    print(f"收到 {len(rows)} 帧；丢弃 {parser.discarded_bytes} 个非帧字节")
    return rows, port, selected_id, parser.discarded_bytes


def simulate_rows(args):
    rng = random.Random(args.seed)
    total = args.pre_seconds + args.active_seconds + args.post_seconds
    count = max(1, int(total * args.sample_rate))
    start = datetime.now().astimezone()
    rows = []
    for index in range(count):
        elapsed = index / args.sample_rate
        phase = phase_at(elapsed, args.pre_seconds, args.active_seconds)
        moving = phase == "active"
        signal = args.amplitude_g * math.sin(2 * math.pi * args.frequency * elapsed) if moving else 0.0
        noise = lambda: rng.gauss(0.0, args.noise_g)
        rows.append(
            {
                "host_time_iso": (start + timedelta(seconds=elapsed)).isoformat(timespec="milliseconds"),
                "elapsed_s": round(elapsed, 6),
                "phase": phase,
                "device_id": args.device_id if args.device_id is not None else 0,
                "ax_g": signal + noise(),
                "ay_g": 0.15 * signal + noise(),
                "az_g": 1.0 + 0.05 * signal + noise(),
                "gx_dps": signal * 40 + noise(),
                "gy_dps": noise(),
                "gz_dps": noise(),
                "hx_raw": 0,
                "hy_raw": 0,
                "hz_raw": 0,
                "roll_deg": 0.0,
                "pitch_deg": 0.0,
                "yaw_deg": 0.0,
                "battery_v": 3.95,
            }
        )
    return rows


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        return None
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] * (upper - position) + ordered[upper] * (position - lower)


def dominant_frequency(times, values, low=0.5, high=8.0):
    if len(times) < 4 or times[-1] <= times[0]:
        return None, None
    centered = [value - statistics.fmean(values) for value in values]
    duration = times[-1] - times[0]
    step = max(0.02, 1.0 / (duration * 5.0))
    best_frequency = None
    best_power = -1.0
    frequency = low
    while frequency <= high + 1e-9:
        real = 0.0
        imaginary = 0.0
        for timestamp, value in zip(times, centered):
            angle = 2.0 * math.pi * frequency * (timestamp - times[0])
            real += value * math.cos(angle)
            imaginary -= value * math.sin(angle)
        power = real * real + imaginary * imaginary
        if power > best_power:
            best_frequency = frequency
            best_power = power
        frequency += step
    return best_frequency, best_power


def calculate_metrics(rows, manifest):
    active = [row for row in rows if row["phase"] == "active"]
    reasons = []
    if len(active) < 10:
        reasons.append("active 阶段有效样本少于 10 帧")
    if not active:
        return {"valid": False, "invalid_reasons": reasons or ["无 active 数据"]}

    times = [row["elapsed_s"] for row in active]
    duration = times[-1] - times[0] if len(times) > 1 else 0.0
    sample_rate = (len(times) - 1) / duration if duration > 0 else None
    intervals = [b - a for a, b in zip(times, times[1:]) if b > a]
    median_interval = statistics.median(intervals) if intervals else None
    gap_count = (
        sum(interval > median_interval * 1.5 for interval in intervals)
        if median_interval and median_interval > 0
        else 0
    )

    axes = {axis: [row[f"a{axis}_g"] for row in active] for axis in "xyz"}
    axis_stddev = {
        axis: statistics.pstdev(values) if len(values) > 1 else 0.0
        for axis, values in axes.items()
    }
    primary_axis = max(axis_stddev, key=axis_stddev.get)
    primary_values = axes[primary_axis]
    center = statistics.fmean(primary_values)
    centered_g = [value - center for value in primary_values]
    centered_mps2 = [value * 9.80665 for value in centered_g]
    accel_rms = math.sqrt(statistics.fmean(value * value for value in centered_mps2))
    dominant_hz, spectral_power = dominant_frequency(times, primary_values)
    velocity_index = (
        accel_rms / (2.0 * math.pi * dominant_hz)
        if dominant_hz and dominant_hz > 0
        else None
    )

    static_pre = [row for row in rows if row["phase"] == "static_pre"]
    reference = {}
    for axis in "xyz":
        source = static_pre or active
        reference[axis] = statistics.fmean(row[f"a{axis}_g"] for row in source)
    vector_dynamic = []
    for row in active:
        vector_dynamic.append(
            math.sqrt(sum((row[f"a{axis}_g"] - reference[axis]) ** 2 for axis in "xyz"))
            * 9.80665
        )
    clip_count = sum(
        any(abs(row[f"a{axis}_g"]) >= 15.8 for axis in "xyz") for row in active
    )
    if clip_count:
        reasons.append(f"检测到 {clip_count} 帧接近 ±16 g 量程上限")
    if duration < 5.0:
        reasons.append("active 阶段短于 5 s")
    if intervals and gap_count / len(intervals) > 0.05:
        reasons.append("超过 5% 的采样间隔存在掉帧/抖动")

    meter_value = manifest.get("meter", {}).get("value")
    normalized = (
        meter_value / velocity_index
        if meter_value is not None and velocity_index and velocity_index > 0
        else None
    )
    return {
        "valid": not reasons,
        "invalid_reasons": reasons,
        "sample_count_total": len(rows),
        "sample_count_active": len(active),
        "active_duration_s": duration,
        "sample_rate_hz": sample_rate,
        "median_interval_s": median_interval,
        "gap_count": gap_count,
        "primary_axis": primary_axis,
        "axis_stddev_g": axis_stddev,
        "primary_accel_rms_mps2": accel_rms,
        "primary_accel_p95_abs_mps2": percentile([abs(v) for v in centered_mps2], 0.95),
        "primary_accel_peak_abs_mps2": max(abs(v) for v in centered_mps2),
        "vector_dynamic_rms_mps2": math.sqrt(statistics.fmean(v * v for v in vector_dynamic)),
        "dominant_frequency_hz": dominant_hz,
        "dominant_spectral_power": spectral_power,
        "velocity_index_mps": velocity_index,
        "meter_value": meter_value,
        "meter_unit": manifest.get("meter", {}).get("unit"),
        "normalized_meter_per_mps": normalized,
        "clip_count": clip_count,
    }


def fmt(value, digits=4):
    if value is None:
        return "—"
    if isinstance(value, bool):
        return "是" if value else "否"
    if isinstance(value, (int, str)):
        return str(value)
    return f"{value:.{digits}f}"


def write_svg(path, rows, primary_axis):
    active = [row for row in rows if row["phase"] == "active"]
    if not active:
        path.write_text("<svg xmlns=\"http://www.w3.org/2000/svg\"/>", encoding="utf-8")
        return
    values = [row[f"a{primary_axis}_g"] for row in active]
    center = statistics.fmean(values)
    values = [value - center for value in values]
    times = [row["elapsed_s"] - active[0]["elapsed_s"] for row in active]
    width, height, pad = 1000, 320, 45
    maximum = max(max(abs(v) for v in values), 0.1)
    span = max(times[-1], 1e-6)
    stride = max(1, len(active) // 1000)
    points = []
    for timestamp, value in zip(times[::stride], values[::stride]):
        x = pad + timestamp / span * (width - 2 * pad)
        y = height / 2 - value / maximum * (height / 2 - pad)
        points.append(f"{x:.1f},{y:.1f}")
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<rect width="100%" height="100%" fill="#fff"/>
<line x1="{pad}" y1="{height/2}" x2="{width-pad}" y2="{height/2}" stroke="#aaa"/>
<line x1="{pad}" y1="{pad}" x2="{pad}" y2="{height-pad}" stroke="#333"/>
<polyline fill="none" stroke="#1769aa" stroke-width="1.5" points="{' '.join(points)}"/>
<text x="{pad}" y="20" font-family="sans-serif" font-size="14">active: a{primary_axis} - mean (g), ±{maximum:.2f} g</text>
<text x="{width-pad-100}" y="{height-10}" font-family="sans-serif" font-size="12">{span:.2f} s</text>
</svg>"""
    path.write_text(svg, encoding="utf-8")


def write_trial_summary_csv(path, manifest, metrics):
    row = {
        "generator": manifest.get("generator"),
        "revision": manifest.get("revision"),
        "mode": manifest.get("mode"),
        "repeat": manifest.get("repeat"),
        "valid": metrics.get("valid"),
        "meter_value": metrics.get("meter_value"),
        "meter_unit": metrics.get("meter_unit"),
        "sample_rate_hz": metrics.get("sample_rate_hz"),
        "primary_axis": metrics.get("primary_axis"),
        "accel_rms_mps2": metrics.get("primary_accel_rms_mps2"),
        "dominant_frequency_hz": metrics.get("dominant_frequency_hz"),
        "velocity_index_mps": metrics.get("velocity_index_mps"),
        "normalized_meter_per_mps": metrics.get("normalized_meter_per_mps"),
        "invalid_reasons": "; ".join(metrics.get("invalid_reasons", [])),
    }
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(row))
        writer.writeheader()
        writer.writerow(row)


def write_trial_report(path, manifest, metrics):
    metadata = [
        ("发电机", manifest.get("generator")),
        ("结构版本", manifest.get("revision")),
        ("工况", manifest.get("mode")),
        ("重复编号", manifest.get("repeat")),
        ("测试点", manifest.get("test_point")),
        ("波形记录", manifest.get("waveform")),
        ("万用表", manifest.get("meter", {}).get("model")),
        ("操作员", manifest.get("operator")),
    ]
    results = [
        ("试次有效", metrics.get("valid"), ""),
        ("万用表读数", metrics.get("meter_value"), metrics.get("meter_unit", "")),
        ("有效帧率", metrics.get("sample_rate_hz"), "Hz"),
        ("主运动轴", metrics.get("primary_axis"), ""),
        ("主轴加速度 RMS", metrics.get("primary_accel_rms_mps2"), "m/s²"),
        ("主频", metrics.get("dominant_frequency_hz"), "Hz"),
        ("速度激励指标", metrics.get("velocity_index_mps"), "m/s"),
        ("归一化电气指标", metrics.get("normalized_meter_per_mps"), f"{metrics.get('meter_unit', '')}/(m/s)"),
        ("削顶帧", metrics.get("clip_count"), ""),
    ]
    meta_rows = "".join(
        f"<tr><th>{html.escape(str(label))}</th><td>{html.escape(fmt(value))}</td></tr>"
        for label, value in metadata
    )
    result_rows = "".join(
        f"<tr><th>{html.escape(str(label))}</th><td>{html.escape(fmt(value))} {html.escape(unit)}</td></tr>"
        for label, value, unit in results
    )
    reasons = metrics.get("invalid_reasons", [])
    warning = (
        "<p class='warn'><strong>需复测：</strong>" + html.escape("；".join(reasons)) + "</p>"
        if reasons
        else "<p class='ok'><strong>质量检查通过。</strong></p>"
    )
    document = f"""<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<title>线性发电机试验报告</title><style>
body{{font-family:system-ui,sans-serif;max-width:980px;margin:32px auto;padding:0 20px;color:#202124}}
table{{border-collapse:collapse;width:100%;margin:12px 0 24px}}th,td{{border:1px solid #ddd;padding:8px;text-align:left}}th{{width:34%;background:#f5f7f9}}
.warn{{background:#fff3cd;padding:12px}}.ok{{background:#d9f4e5;padding:12px}}.note{{color:#555}}
</style></head><body><h1>线性发电机试验报告</h1><h2>试验信息</h2><table>{meta_rows}</table>
<h2>结果</h2>{warning}<table>{result_rows}</table><img src="acceleration.svg" alt="加速度曲线" style="max-width:100%">
<p class="note">归一化电气指标用于抵消手摇强度差异，是相对比较量，不等同于效率或输出功率。开路电压与短路电流来自不同试次，不能相乘作为功率。</p>
<p class="note">备注：{html.escape(str(manifest.get('notes') or '—'))}</p></body></html>"""
    path.write_text(document, encoding="utf-8")


def analyze_trial(trial_dir):
    trial_dir = Path(trial_dir).expanduser().resolve()
    manifest_path = trial_dir / "manifest.json"
    raw_path = trial_dir / "imu_raw.csv"
    if not manifest_path.exists() or not raw_path.exists():
        raise SystemExit(f"{trial_dir} 缺少 manifest.json 或 imu_raw.csv")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    rows = read_raw_csv(raw_path)
    metrics = calculate_metrics(rows, manifest)
    summary = {
        "schema_version": 1,
        "analyzed_at": now_iso(),
        "trial_dir": str(trial_dir),
        "manifest": manifest,
        "metrics": metrics,
    }
    write_json(trial_dir / "summary.json", summary)
    write_trial_summary_csv(trial_dir / "trial_summary.csv", manifest, metrics)
    write_svg(trial_dir / "acceleration.svg", rows, metrics.get("primary_axis", "x"))
    write_trial_report(trial_dir / "report.html", manifest, metrics)
    return summary


def median(values):
    return statistics.median(values) if values else None


def coefficient_of_variation(values):
    if len(values) < 2:
        return None
    mean = statistics.fmean(values)
    return statistics.stdev(values) / abs(mean) * 100.0 if mean else None


def load_valid_summaries(root):
    summaries = []
    for path in sorted(Path(root).expanduser().resolve().rglob("summary.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        if data.get("metrics", {}).get("valid"):
            summaries.append(data)
    return summaries


def aggregate(summaries):
    groups = {}
    for summary in summaries:
        manifest = summary["manifest"]
        metrics = summary["metrics"]
        key = (
            manifest.get("mode"),
            metrics.get("meter_unit"),
            manifest.get("waveform"),
            manifest.get("test_point"),
        )
        groups.setdefault(key, []).append(metrics)
    result = {}
    for key, metrics_list in groups.items():
        raw = [m["meter_value"] for m in metrics_list if m.get("meter_value") is not None]
        normalized = [
            m["normalized_meter_per_mps"]
            for m in metrics_list
            if m.get("normalized_meter_per_mps") is not None
        ]
        excitation = [m["velocity_index_mps"] for m in metrics_list if m.get("velocity_index_mps") is not None]
        result[key] = {
            "n": len(metrics_list),
            "raw_median": median(raw),
            "raw_cv_percent": coefficient_of_variation(raw),
            "normalized_median": median(normalized),
            "normalized_cv_percent": coefficient_of_variation(normalized),
            "velocity_index_median_mps": median(excitation),
        }
    return result


def percent_ratio(candidate, baseline):
    if candidate is None or baseline in (None, 0):
        return None
    return candidate / baseline * 100.0


def compare_roots(args):
    baseline_trials = load_valid_summaries(args.baseline)
    candidate_trials = load_valid_summaries(args.candidate)
    baseline = aggregate(baseline_trials)
    candidate = aggregate(candidate_trials)
    output_dir = Path(args.output_dir).expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=False)
    rows = []
    all_keys = sorted(set(baseline) | set(candidate))
    for key in all_keys:
        base = baseline.get(key, {})
        cand = candidate.get(key, {})
        normalized_retention = percent_ratio(cand.get("normalized_median"), base.get("normalized_median"))
        raw_retention = percent_ratio(cand.get("raw_median"), base.get("raw_median"))
        cvs = [base.get("normalized_cv_percent"), cand.get("normalized_cv_percent")]
        cv_ok = all(value is None or value <= args.maximum_cv for value in cvs)
        if not base or not cand:
            verdict = "数据不足"
        elif base.get("n", 0) < args.minimum_trials or cand.get("n", 0) < args.minimum_trials:
            verdict = "重复次数不足"
        elif normalized_retention is None:
            verdict = "无法计算"
        elif normalized_retention >= args.minimum_retention and cv_ok:
            verdict = "通过"
        else:
            verdict = "需复核"
        rows.append(
            {
                "mode": key[0],
                "unit": key[1],
                "waveform": key[2],
                "test_point": key[3],
                "baseline_n": base.get("n", 0),
                "candidate_n": cand.get("n", 0),
                "baseline_raw_median": base.get("raw_median"),
                "candidate_raw_median": cand.get("raw_median"),
                "raw_retention_percent": raw_retention,
                "baseline_normalized_median": base.get("normalized_median"),
                "candidate_normalized_median": cand.get("normalized_median"),
                "normalized_retention_percent": normalized_retention,
                "baseline_normalized_cv_percent": base.get("normalized_cv_percent"),
                "candidate_normalized_cv_percent": cand.get("normalized_cv_percent"),
                "baseline_velocity_index_mps": base.get("velocity_index_median_mps"),
                "candidate_velocity_index_mps": cand.get("velocity_index_median_mps"),
                "verdict": verdict,
            }
        )
    result = {
        "created_at": now_iso(),
        "baseline_root": str(Path(args.baseline).resolve()),
        "candidate_root": str(Path(args.candidate).resolve()),
        "minimum_retention_percent": args.minimum_retention,
        "maximum_cv_percent": args.maximum_cv,
        "minimum_trials_per_condition": args.minimum_trials,
        "baseline_valid_trials": len(baseline_trials),
        "candidate_valid_trials": len(candidate_trials),
        "rows": rows,
    }
    write_json(output_dir / "comparison.json", result)
    fieldnames = list(rows[0]) if rows else ["mode", "unit", "verdict"]
    with (output_dir / "comparison.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    html_rows = "".join(
        "<tr>" + "".join(
            f"<td>{html.escape(fmt(row.get(key), 2))}</td>"
            for key in (
                "mode", "unit", "waveform", "test_point", "baseline_n", "candidate_n", "baseline_raw_median",
                "candidate_raw_median", "raw_retention_percent", "normalized_retention_percent",
                "baseline_normalized_cv_percent", "candidate_normalized_cv_percent", "verdict",
            )
        ) + "</tr>"
        for row in rows
    )
    document = f"""<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><title>发电机 T0/T1 对比</title>
<style>body{{font-family:system-ui,sans-serif;max-width:1200px;margin:32px auto}}table{{border-collapse:collapse;width:100%}}th,td{{border:1px solid #ddd;padding:7px}}th{{background:#f5f7f9}}</style></head>
<body><h1>线性发电机 T0/T1 对比</h1><p>判定门槛：每个条件至少 {args.minimum_trials} 次有效重复，归一化保持率 ≥ {args.minimum_retention:.1f}%，归一化 CV ≤ {args.maximum_cv:.1f}%。</p>
<table><thead><tr><th>工况</th><th>单位</th><th>波形</th><th>测试点</th><th>T0 n</th><th>T1 n</th><th>T0 中位数</th><th>T1 中位数</th><th>原始保持率 %</th><th>归一化保持率 %</th><th>T0 CV %</th><th>T1 CV %</th><th>结论</th></tr></thead><tbody>{html_rows}</tbody></table>
<p>归一化值是万用表读数 ÷ 速度激励指标，仅用于同一测试方法下的前后相对比较。</p></body></html>"""
    (output_dir / "comparison.html").write_text(document, encoding="utf-8")
    return result


def add_trial_arguments(parser):
    parser.add_argument("--generator", required=True, help="发电机样品编号")
    parser.add_argument("--revision", required=True, help="结构版本，如 T0 / T1")
    parser.add_argument("--mode", required=True, choices=("open", "short", "load"), help="电气工况")
    parser.add_argument("--repeat", type=int, default=1, help="同工况重复编号")
    parser.add_argument("--meter-value", type=float, help="万用表读数；capture 缺省时交互询问")
    parser.add_argument("--meter-unit", help="默认 open/load=V，short=mA")
    parser.add_argument("--meter-model", default="unknown", help="万用表型号")
    parser.add_argument("--meter-range", default="auto", help="万用表量程")
    parser.add_argument("--waveform", choices=("unknown", "ac", "dc"), default="unknown")
    parser.add_argument("--test-point", default="output_pogo_pins")
    parser.add_argument("--operator", default="unknown")
    parser.add_argument("--notes", default="")
    parser.add_argument("--pre-seconds", type=float, default=3.0)
    parser.add_argument("--active-seconds", type=float, default=20.0)
    parser.add_argument("--post-seconds", type=float, default=3.0)
    parser.add_argument("--target-frequency", type=float, help="目标手摇频率 Hz")
    parser.add_argument("--sensor-bandwidth", type=float, default=20.0, help="传感器带宽记录值 Hz")
    parser.add_argument("--device-id", type=int, choices=range(256), help="RF 从机 ID；缺省自动锁定首个 ID")
    parser.add_argument("--output-root", default="test_runs")
    parser.add_argument("--trial-dir", help="显式指定新试次目录（必须不存在）")


def build_parser():
    parser = argparse.ArgumentParser(description="线性发电机 + WT9011DCL-RF 测试工作站")
    subparsers = parser.add_subparsers(dest="command", required=True)

    ports = subparsers.add_parser("ports", help="列出串口，辅助识别 RF 主机")
    ports.set_defaults(handler=command_ports)

    capture = subparsers.add_parser("capture", help="从 RF 主机采集一轮实物测试")
    add_trial_arguments(capture)
    capture.add_argument("--port", help="RF 主机串口，如 /dev/ttyUSB0；缺省自动识别")
    capture.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    capture.set_defaults(handler=command_capture)

    simulate = subparsers.add_parser("simulate", help="生成模拟试次，验证完整软件链路")
    add_trial_arguments(simulate)
    simulate.add_argument("--sample-rate", type=float, default=30.0)
    simulate.add_argument("--frequency", type=float, default=2.5)
    simulate.add_argument("--amplitude-g", type=float, default=3.0)
    simulate.add_argument("--noise-g", type=float, default=0.03)
    simulate.add_argument("--seed", type=int, default=1)
    simulate.set_defaults(handler=command_simulate)

    analyze = subparsers.add_parser("analyze", help="重新分析已有试次目录")
    analyze.add_argument("trial_dir")
    analyze.set_defaults(handler=command_analyze)

    compare = subparsers.add_parser("compare", help="聚合对比 T0 与 T1 的有效试次")
    compare.add_argument("baseline", help="T0 根目录")
    compare.add_argument("candidate", help="T1 根目录")
    compare.add_argument("--output-dir", required=True, help="新建的对比报告目录")
    compare.add_argument("--minimum-retention", type=float, default=90.0)
    compare.add_argument("--maximum-cv", type=float, default=15.0)
    compare.add_argument("--minimum-trials", type=int, default=5)
    compare.set_defaults(handler=command_compare)
    return parser


def validate_durations(args):
    for name in ("pre_seconds", "active_seconds", "post_seconds"):
        if getattr(args, name) < 0:
            raise SystemExit(f"--{name.replace('_', '-')} 不能为负数")
    if args.active_seconds <= 0:
        raise SystemExit("--active-seconds 必须大于 0")


def save_and_analyze(args, manifest, rows):
    trial_dir = create_trial_dir(args)
    manifest["trial_dir"] = str(trial_dir)
    write_json(trial_dir / "manifest.json", manifest)
    write_raw_csv(trial_dir / "imu_raw.csv", rows)
    summary = analyze_trial(trial_dir)
    metrics = summary["metrics"]
    print(f"试次目录: {trial_dir}")
    print(f"主频: {fmt(metrics.get('dominant_frequency_hz'))} Hz")
    print(f"速度激励指标: {fmt(metrics.get('velocity_index_mps'))} m/s")
    print(f"归一化电气指标: {fmt(metrics.get('normalized_meter_per_mps'))}")
    print(f"质量检查: {'通过' if metrics.get('valid') else '需复测'}")
    return 0 if metrics.get("valid") else 2


def command_ports(_args):
    ports = list_serial_ports()
    if not ports:
        print("未发现串口")
        return 1
    for port in ports:
        vid_pid = f"{port.vid:04x}:{port.pid:04x}" if port.vid is not None and port.pid is not None else "----:----"
        print(f"{port.device}\t{vid_pid}\t{port.description}")
    return 0


def command_capture(args):
    validate_durations(args)
    rows, port, device_id, discarded = capture_rows(args)
    args.meter_value = ask_meter_value(args)
    manifest = manifest_from_args(args, "serial_capture")
    manifest["sensor"]["device_id"] = device_id
    manifest["serial"] = {"port": port, "baud": args.baud, "discarded_bytes": discarded}
    return save_and_analyze(args, manifest, rows)


def command_simulate(args):
    validate_durations(args)
    if args.meter_value is None:
        args.meter_value = 20.0 if args.mode == "short" else 5.0
    rows = simulate_rows(args)
    manifest = manifest_from_args(args, "simulation")
    manifest["simulation"] = {
        "sample_rate_hz": args.sample_rate,
        "frequency_hz": args.frequency,
        "amplitude_g": args.amplitude_g,
        "noise_g": args.noise_g,
        "seed": args.seed,
    }
    return save_and_analyze(args, manifest, rows)


def command_analyze(args):
    summary = analyze_trial(args.trial_dir)
    print(json.dumps(summary["metrics"], ensure_ascii=False, indent=2))
    return 0 if summary["metrics"].get("valid") else 2


def command_compare(args):
    result = compare_roots(args)
    print(f"有效试次: T0={result['baseline_valid_trials']}, T1={result['candidate_valid_trials']}")
    for row in result["rows"]:
        print(
            f"{row['mode']}: 归一化保持率 {fmt(row['normalized_retention_percent'], 2)}% — {row['verdict']}"
        )
    print(f"对比报告: {Path(args.output_dir).resolve() / 'comparison.html'}")
    return 0 if result["rows"] and all(row["verdict"] == "通过" for row in result["rows"]) else 2


def main():
    args = build_parser().parse_args()
    return args.handler(args)


if __name__ == "__main__":
    raise SystemExit(main())
