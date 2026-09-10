#!/usr/bin/env python3
"""Summarize existing telemetry without launching Wine or changing the source log.

Usage: python3 verification/cursor/analyze_cursor.py LOG OUTPUT.json
Counts refer to emitted records, not all polls or API calls. Preserve source line
numbers and SHA-256 so conclusions can be checked against the original evidence.
"""
import collections
import hashlib
import json
from pathlib import Path
import re
import sys

source, output = map(Path, sys.argv[1:])
digest = hashlib.sha256()
counts = collections.Counter()
flags = collections.Counter()
handles = collections.Counter()
windows, cursor_states, presentations, exceptional = [], [], [], []
cursor_metrics = collections.Counter()
iat_cursor_metrics = collections.Counter()
last_context = None
start = frequency = None
with source.open("rb") as stream:
    for line_number, raw in enumerate(stream, 1):
        digest.update(raw)
        line = raw.decode("utf-8", errors="replace").strip()
        kind = line.split(" ", 1)[0]
        counts[kind] += 1
        if not kind.startswith(("telemetry_", "loading_")):
            continue
        item = dict(re.findall(r"(\w+)=([^\s]+)", line))
        item["source_line"] = line_number
        if kind == "telemetry_start":
            start, frequency = int(item["qpc"]), int(item["qpc_frequency"])
        if start and "qpc" in item:
            item["seconds_since_start"] = round((int(item["qpc"]) - start) / frequency, 6)
        if kind == "telemetry_window_context":
            last_context = item
        elif kind == "telemetry_window":
            item["context"] = last_context
            windows.append(item)
        elif kind == "telemetry_cursor_poll":
            flags[item.get("flags", "unavailable")] += 1
            handles[item.get("cursor", "unavailable")] += 1
            cursor_states.append(item)
        elif kind == "telemetry_presentation":
            presentations.append(item)
        elif kind == "telemetry_metric" and item.get("name", "").startswith("cursor_"):
            cursor_metrics[item["name"]] += int(item["count"])
        elif kind == "loading_metric" and item.get("op") in ("SetCursor", "SetCursorPos"):
            iat_cursor_metrics[item["op"]] += int(item["count"])
        if "reset" in kind or kind == "telemetry_cursor_api":
            exceptional.append({"kind": kind, **item})
for item in windows:
    candidates = [s for s in cursor_states if "qpc" in s]
    item["nearest_cursor_poll"] = min(candidates, key=lambda s: abs(int(s["qpc"]) - int(item["qpc"]))) if candidates else None
report = {
    "source_name": source.name, "source_sha256": digest.hexdigest(),
    "bytes": source.stat().st_size, "lines": line_number,
    "emitted_cursor_poll_count": len(cursor_states),
    "emitted_cursor_flag_counts": dict(flags), "emitted_cursor_handle_counts": dict(handles),
    "d3d_cursor_api_records": counts["telemetry_cursor_api"],
    "d3d_cursor_metric_calls": dict(cursor_metrics),
    "main_module_iat_cursor_metric_calls": dict(iat_cursor_metrics),
    "window_snapshots": windows, "presentations": presentations,
    "cursor_api_or_reset_records": exceptional,
    "limitations": [
        "Polls are rate-limited to 4 Hz and emitted only on change; no host cursor visibility was measured.",
        "IAT hook coverage is main module only, not DirectInput or dynamically resolved calls.",
        "No exact timestamp associates the user's double-cursor observation with an individual poll.",
        "No omitted zero-count metric proves absence outside the instrumented interfaces."
    ]
}
output.write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps({"output": str(output), "sha256": report["source_sha256"], "polls": len(cursor_states), "windows": len(windows), "d3d_cursor_records": counts["telemetry_cursor_api"]}))
