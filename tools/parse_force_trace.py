"""Validate v11 RAM trace transfer and decode sensor/state frames."""
import argparse
import json
import struct
from pathlib import Path

WIRE = struct.Struct("<III16s11H6hBBhH")
assert WIRE.size == 68
FLAG_NAMES = (
    "active", "ready", "blocked", "down", "tap_consumed", "candidate",
    "prepress", "candidate_moving", "dragging", "drag_armed", "suppress_motion",
    "previous_resting", "hold_cancelled",
)


def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def decode(payload, start_ms):
    fields = WIRE.unpack(payload)
    sample, work, io, raw = fields[:4]
    names = (
        "before_flags", "after_flags", "baseline_before", "baseline", "peak",
        "threshold", "repeat_threshold", "trough", "pressed_age", "candidate_age",
        "repeat_left", "output_x", "output_y", "event", "button_rc", "x_rc", "y_rc",
        "kind", "extra", "frame_rc", "reserved",
    )
    result = dict(zip(names, fields[4:], strict=True))
    result.update(sample_ms=sample, relative_ms=(sample-start_ms) & 0xFFFFFFFF,
                  work_us=work, io_us=io, raw_hex=raw.hex())
    for key in ("before_flags", "after_flags"):
        result[key.removesuffix("_flags")+"_state"] = [
            name for bit, name in enumerate(FLAG_NAMES) if result[key] & (1 << bit)
        ]
    result.update(
        fingers=raw[4], strength=int.from_bytes(raw[13:15], "big"), area=raw[15],
        abs_x=int.from_bytes(raw[9:11], "big"), abs_y=int.from_bytes(raw[11:13], "big"),
        raw_dx=int.from_bytes(raw[5:7], "big", signed=True),
        raw_dy=int.from_bytes(raw[7:9], "big", signed=True),
        gesture0=raw[0], gesture1=raw[1], system0=raw[2], system1=raw[3],
    )
    return result


def parse(document):
    lines = document["lines"]
    header = lines[0].split(",")
    if len(header) != 7 or header[:3] != ["DATA", "v11", "1"]:
        raise ValueError("Unexpected trace header/version")
    count, full, start, size = map(int, header[3:])
    if size != WIRE.size or count > 1536 or count < 0:
        raise ValueError("Invalid record count or size")
    if len(lines) != count + 2 or lines[-1] != f"END,{count}":
        raise ValueError("Incomplete trace")
    frames = []
    for index, line in enumerate(lines[1:-1]):
        parts = line.split(",")
        if len(parts) != 4 or parts[:2] != ["R", str(index)]:
            raise ValueError("Missing, repeated or out-of-order frame")
        payload = bytes.fromhex(parts[2])
        if len(payload) != size or checksum(payload) != int(parts[3], 16):
            raise ValueError("Frame size/checksum mismatch")
        frames.append(decode(payload, start))
    times = [f["relative_ms"] for f in frames]
    if any(b < a for a, b in zip(times, times[1:])):
        raise ValueError("Sensor clock out of order")
    if any(t > 10000 for t in times):
        raise ValueError("Frame outside capture window")
    return {
        "version": "v11", "buffer_full": bool(full), "started_ms": start,
        "records": count, "frames": frames,
        "force_edges": [{k: f[k] for k in
                        ("relative_ms", "event", "strength", "baseline", "button_rc",
                         "before_state", "after_state")} for f in frames if f["event"]],
        "input_errors": [f for f in frames if any(f[k] < 0 for k in
                                                ("button_rc", "x_rc", "y_rc", "frame_rc"))],
        "note": "Sensor-side timing only; PC arrival timestamps are separate. No hand-to-PC latency claim.",
    }


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--output", required=True)
    args = ap.parse_args()
    result = parse(json.loads(Path(args.input).read_text(encoding="utf-8-sig")))
    Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "frames"}, ensure_ascii=False))
