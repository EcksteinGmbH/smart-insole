#!/usr/bin/env python3
"""线性发电机本地网页工作站（仅监听本机）。"""

import json
import mimetypes
import threading
import time
import webbrowser
from argparse import Namespace
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse


PHASE_LABELS = {
    "idle": "等待开始",
    "static_pre": "保持静止",
    "active": "按节拍手摇",
    "static_post": "停止并保持静止",
    "awaiting_meter": "输入万用表读数",
    "saving": "正在保存结果",
    "complete": "试验完成",
    "stopped": "已停止",
    "error": "发生错误",
}


def number(payload, key, default=None, minimum=None, maximum=None):
    value = payload.get(key, default)
    if value in (None, ""):
        return None
    try:
        converted = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{key} 必须是数字") from exc
    if minimum is not None and converted < minimum:
        raise ValueError(f"{key} 不能小于 {minimum}")
    if maximum is not None and converted > maximum:
        raise ValueError(f"{key} 不能大于 {maximum}")
    return converted


def integer(payload, key, default=None, minimum=None, maximum=None):
    value = number(payload, key, default, minimum, maximum)
    return None if value is None else int(value)


def required_text(payload, key):
    value = str(payload.get(key, "")).strip()
    if not value:
        raise ValueError(f"缺少 {key}")
    return value


def build_capture_args(payload, output_root, demo=False):
    mode = payload.get("mode")
    if mode not in ("open", "short"):
        raise ValueError("mode 必须是 open 或 short")
    waveform = payload.get("waveform")
    if waveform not in ("ac", "dc"):
        raise ValueError("开始前必须确认 AC 或 DC")
    pre_seconds = number(payload, "pre_seconds", 3.0, 0.0, 60.0)
    active_seconds = number(payload, "active_seconds", 20.0, 0.2 if demo else 5.0, 600.0)
    post_seconds = number(payload, "post_seconds", 3.0, 0.0, 60.0)
    return Namespace(
        generator=required_text(payload, "generator"),
        revision=required_text(payload, "revision"),
        mode=mode,
        repeat=integer(payload, "repeat", 1, 1, 999),
        meter_value=None,
        meter_unit=payload.get("meter_unit") or ("mA" if mode == "short" else "V"),
        meter_model=str(payload.get("meter_model") or "unknown").strip(),
        meter_range=str(payload.get("meter_range") or "unknown").strip(),
        waveform=waveform,
        test_point="output_pogo_pins",
        operator=str(payload.get("operator") or "unknown").strip(),
        notes=str(payload.get("notes") or "").strip(),
        pre_seconds=pre_seconds,
        active_seconds=active_seconds,
        post_seconds=post_seconds,
        target_frequency=number(payload, "target_frequency", 2.5, 0.1, 10.0),
        sensor_bandwidth=number(payload, "sensor_bandwidth", 20.0, 1.0, 256.0),
        device_id=integer(payload, "device_id", None, 0, 255),
        output_root=str(output_root),
        trial_dir=None,
        port=str(payload.get("port") or "").strip() or None,
        baud=integer(payload, "baud", 460800, 1200, 3000000),
        sample_rate=number(payload, "sample_rate", 30.0, 1.0, 200.0),
        frequency=number(payload, "frequency", 2.5, 0.1, 8.0),
        amplitude_g=number(payload, "amplitude_g", 3.0, 0.01, 15.0),
        noise_g=number(payload, "noise_g", 0.03, 0.0, 1.0),
        seed=integer(payload, "seed", 1, 0, 1000000),
    )


class CaptureManager:
    def __init__(self, core, output_root):
        self.core = core
        self.output_root = Path(output_root).expanduser().resolve()
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.thread = None
        self.args = None
        self.rows = None
        self.capture_meta = None
        self.demo = False
        self.live = deque(maxlen=900)
        self.state = self._initial_state()

    @staticmethod
    def _initial_state():
        return {
            "state": "idle",
            "phase": "idle",
            "phase_label": PHASE_LABELS["idle"],
            "message": "配置参数后开始测试",
            "progress": 0.0,
            "elapsed_s": 0.0,
            "total_s": 0.0,
            "sample_count": 0,
            "device_id": None,
            "mode": None,
            "meter_unit": None,
            "result": None,
            "error": None,
        }

    def snapshot(self):
        with self.lock:
            snapshot = dict(self.state)
            snapshot["live"] = list(self.live)
        return snapshot

    def start(self, payload, demo=False):
        args = build_capture_args(payload, self.output_root, demo=demo)
        with self.lock:
            if self.state["state"] in ("capturing", "awaiting_meter", "saving"):
                raise ValueError("已有试验正在进行，请先完成或停止")
            self.stop_event.clear()
            self.args = args
            self.rows = None
            self.capture_meta = None
            self.demo = demo
            self.live.clear()
            total = args.pre_seconds + args.active_seconds + args.post_seconds
            self.state = self._initial_state()
            self.state.update(
                {
                    "state": "capturing",
                    "phase": "static_pre",
                    "phase_label": PHASE_LABELS["static_pre"],
                    "message": "演示采集中" if demo else "正在等待传感器数据",
                    "total_s": total,
                    "mode": args.mode,
                    "meter_unit": args.meter_unit,
                }
            )
            self.thread = threading.Thread(target=self._capture_worker, daemon=True)
            self.thread.start()
        return self.snapshot()

    def _progress(self, elapsed, total, phase):
        with self.lock:
            self.state["elapsed_s"] = elapsed
            self.state["total_s"] = total
            self.state["progress"] = min(1.0, elapsed / total) if total else 0.0
            self.state["phase"] = phase
            self.state["phase_label"] = PHASE_LABELS.get(phase, phase)

    def _phase(self, phase, _elapsed):
        with self.lock:
            self.state["phase"] = phase
            self.state["phase_label"] = PHASE_LABELS.get(phase, phase)
            self.state["message"] = {
                "static_pre": "保持设备静止，正在采集基线",
                "active": "开始按目标节拍手摇",
                "static_post": "停止手摇并保持静止",
            }.get(phase, phase)

    def _frame(self, frame, elapsed, total):
        point = {
            "t": round(frame["elapsed_s"], 4),
            "x": round(frame["ax_g"], 4),
            "y": round(frame["ay_g"], 4),
            "z": round(frame["az_g"], 4),
            "phase": frame["phase"],
        }
        with self.lock:
            self.live.append(point)
            self.state["sample_count"] += 1
            self.state["device_id"] = frame["device_id"]
            self.state["elapsed_s"] = elapsed
            self.state["progress"] = min(1.0, elapsed / total) if total else 0.0

    def _capture_worker(self):
        try:
            if self.demo:
                rows = self.core.simulate_rows(self.args)
                total = self.args.pre_seconds + self.args.active_seconds + self.args.post_seconds
                previous = 0.0
                previous_phase = None
                for row in rows:
                    if self.stop_event.is_set():
                        break
                    elapsed = row["elapsed_s"]
                    wait = max(0.0, elapsed - previous)
                    if wait:
                        time.sleep(wait)
                    if row["phase"] != previous_phase:
                        self._phase(row["phase"], elapsed)
                        previous_phase = row["phase"]
                    self._progress(elapsed, total, row["phase"])
                    self._frame(row, elapsed, total)
                    previous = elapsed
                meta = {"source": "simulation"}
            else:
                rows, port, device_id, discarded = self.core.capture_rows(
                    self.args,
                    on_frame=self._frame,
                    on_phase=self._phase,
                    on_progress=self._progress,
                    stop_event=self.stop_event,
                )
                meta = {
                    "source": "serial_capture",
                    "port": port,
                    "device_id": device_id,
                    "discarded_bytes": discarded,
                }
            if self.stop_event.is_set():
                with self.lock:
                    self.state.update(
                        state="stopped",
                        phase="stopped",
                        phase_label=PHASE_LABELS["stopped"],
                        message="本轮数据未保存",
                    )
                return
            with self.lock:
                self.rows = rows
                self.capture_meta = meta
                self.state.update(
                    state="awaiting_meter",
                    phase="awaiting_meter",
                    phase_label=PHASE_LABELS["awaiting_meter"],
                    message="请输入本轮万用表稳定读数",
                    progress=1.0,
                )
        except SystemExit as exc:
            self._set_error(str(exc))
        except Exception as exc:  # noqa: BLE001 - boundary converts failures for UI
            self._set_error(str(exc))

    def _set_error(self, message):
        with self.lock:
            self.state.update(
                state="error",
                phase="error",
                phase_label=PHASE_LABELS["error"],
                message=message,
                error=message,
            )

    def stop(self):
        with self.lock:
            if self.state["state"] != "capturing":
                raise ValueError("当前没有正在进行的采集")
            self.state["message"] = "正在停止……"
            self.stop_event.set()
        return self.snapshot()

    def finalize(self, payload):
        meter_value = number(payload, "meter_value", None, 0.0)
        if meter_value is None:
            raise ValueError("请输入万用表读数")
        with self.lock:
            if self.state["state"] != "awaiting_meter" or self.rows is None:
                raise ValueError("当前没有等待保存的试验")
            self.args.meter_value = meter_value
            if payload.get("notes"):
                self.args.notes = str(payload["notes"]).strip()
            args = self.args
            rows = self.rows
            meta = dict(self.capture_meta)
            self.state.update(
                state="saving",
                phase="saving",
                phase_label=PHASE_LABELS["saving"],
                message="正在分析并保存本轮结果",
            )
        try:
            manifest = self.core.manifest_from_args(args, meta["source"])
            if meta["source"] == "serial_capture":
                manifest["sensor"]["device_id"] = meta["device_id"]
                manifest["serial"] = {
                    "port": meta["port"],
                    "baud": args.baud,
                    "discarded_bytes": meta["discarded_bytes"],
                }
            else:
                manifest["simulation"] = {
                    "sample_rate_hz": args.sample_rate,
                    "frequency_hz": args.frequency,
                    "amplitude_g": args.amplitude_g,
                    "noise_g": args.noise_g,
                    "seed": args.seed,
                }
            trial_dir, summary = self.core.save_trial(args, manifest, rows)
            relative = trial_dir.relative_to(self.output_root).as_posix()
            result = {
                "trial_dir": str(trial_dir),
                "report_url": f"/runs/{relative}/report.html",
                "summary_url": f"/runs/{relative}/summary.json",
                "trial_summary_url": f"/runs/{relative}/trial_summary.csv",
                "raw_csv_url": f"/runs/{relative}/imu_raw.csv",
                "metrics": summary["metrics"],
            }
            with self.lock:
                self.state.update(
                    state="complete",
                    phase="complete",
                    phase_label=PHASE_LABELS["complete"],
                    message="本轮结果已保存",
                    result=result,
                )
            return result
        except Exception as exc:  # noqa: BLE001 - boundary converts failures for UI
            self._set_error(str(exc))
            raise


def serial_ports(core):
    ports = []
    for port in core.list_serial_ports():
        if port.vid is None and not port.device.startswith(("/dev/ttyUSB", "/dev/ttyACM")):
            continue
        adapter_name = core.rf_adapter_name(port)
        ports.append(
            {
                "device": port.device,
                "description": port.description,
                "vid_pid": f"{port.vid:04x}:{port.pid:04x}" if port.vid is not None else None,
                "rf_adapter": adapter_name,
                "recommended": adapter_name is not None,
            }
        )
    return {"ports": ports, "rf_detected": any(port["recommended"] for port in ports)}


def history_records(output_root, limit=200):
    root = Path(output_root).expanduser().resolve()
    records = []
    if not root.exists():
        return records
    for path in root.rglob("summary.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            manifest = data["manifest"]
            metrics = data["metrics"]
            relative = path.parent.relative_to(root).as_posix()
            records.append(
                {
                    "created_at": manifest.get("created_at"),
                    "generator": manifest.get("generator"),
                    "revision": manifest.get("revision"),
                    "mode": manifest.get("mode"),
                    "repeat": manifest.get("repeat"),
                    "waveform": manifest.get("waveform"),
                    "meter_value": metrics.get("meter_value"),
                    "meter_unit": metrics.get("meter_unit"),
                    "accel_rms": metrics.get("primary_accel_rms_mps2"),
                    "frequency_hz": metrics.get("dominant_frequency_hz"),
                    "velocity_index": metrics.get("velocity_index_mps"),
                    "normalized": metrics.get("normalized_meter_per_mps"),
                    "valid": metrics.get("valid"),
                    "report_url": f"/runs/{relative}/report.html",
                    "trial_summary_url": f"/runs/{relative}/trial_summary.csv",
                    "raw_csv_url": f"/runs/{relative}/imu_raw.csv",
                }
            )
        except (KeyError, OSError, ValueError, TypeError):
            continue
    records.sort(key=lambda item: item.get("created_at") or "", reverse=True)
    return records[:limit]


def build_handler(core, manager, index_path):
    class Handler(BaseHTTPRequestHandler):
        server_version = "GeneratorWorkstation/1.0"

        def log_message(self, fmt, *args):
            print(f"[web] {self.address_string()} {fmt % args}")

        def send_json(self, value, status=200):
            data = json.dumps(value, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def send_file(self, path):
            try:
                data = path.read_bytes()
            except OSError:
                self.send_error(404)
                return
            mime = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
            self.send_response(200)
            self.send_header("Content-Type", f"{mime}; charset=utf-8" if mime.startswith("text/") else mime)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def read_json(self):
            length = int(self.headers.get("Content-Length", "0"))
            if length > 1_000_000:
                raise ValueError("请求过大")
            raw = self.rfile.read(length)
            return json.loads(raw or b"{}")

        def do_GET(self):
            path = urlparse(self.path).path
            if path == "/":
                self.send_file(index_path)
                return
            if path == "/api/status":
                self.send_json(manager.snapshot())
                return
            if path == "/api/ports":
                try:
                    self.send_json(serial_ports(core))
                except SystemExit as exc:
                    self.send_json({"error": str(exc)}, 500)
                return
            if path == "/api/history":
                self.send_json({"records": history_records(manager.output_root)})
                return
            if path.startswith("/runs/"):
                relative = Path(unquote(path[len("/runs/"):]))
                candidate = (manager.output_root / relative).resolve()
                if candidate != manager.output_root and manager.output_root not in candidate.parents:
                    self.send_error(403)
                    return
                self.send_file(candidate)
                return
            self.send_error(404)

        def do_POST(self):
            try:
                payload = self.read_json()
                path = urlparse(self.path).path
                if path == "/api/capture/start":
                    self.send_json(manager.start(payload, demo=False))
                elif path == "/api/demo/start":
                    self.send_json(manager.start(payload, demo=True))
                elif path == "/api/capture/stop":
                    self.send_json(manager.stop())
                elif path == "/api/capture/finalize":
                    self.send_json(manager.finalize(payload))
                else:
                    self.send_error(404)
            except (ValueError, json.JSONDecodeError) as exc:
                self.send_json({"error": str(exc)}, 400)
            except Exception as exc:  # noqa: BLE001 - HTTP boundary
                self.send_json({"error": str(exc)}, 500)

    return Handler


def run_server(core, args):
    output_root = Path(args.output_root).expanduser().resolve()
    manager = CaptureManager(core, output_root)
    index_path = Path(__file__).with_name("generator_web.html")
    if not index_path.exists():
        raise SystemExit(f"缺少网页文件: {index_path}")
    handler = build_handler(core, manager, index_path)
    server = ThreadingHTTPServer((args.host, args.port), handler)
    url = f"http://{args.host}:{server.server_port}/"
    print(f"线性发电机网页工作站: {url}")
    print("按 Ctrl+C 停止；网页只在本机运行，不上传数据。")
    if not args.no_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n网页工作站已停止")
    finally:
        server.server_close()
    return 0
