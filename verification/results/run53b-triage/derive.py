#!/usr/bin/env python3
"""Rebuild the compact Run53B fog flicker witness from the preserved inputs."""
import argparse
import hashlib
import json
from collections import Counter
from datetime import datetime, timedelta
from pathlib import Path

CARD_PS = "f7e0b6647a3bfa62"


def fields(line):
    return {k: v for token in line.split()[1:] if "=" in token
            for k, v in [token.split("=", 1)]}


def utc(anchor, qpc, anchor_qpc, frequency):
    return (anchor + timedelta(seconds=(qpc - anchor_qpc) / frequency)).isoformat(
        timespec="milliseconds").replace("+00:00", "Z")


def pixel_stats(captures, first, second, width, height):
    def read(frame):
        raw = (captures / f"present_1_{frame}.bgra8").read_bytes()
        if len(raw) != width * height * 4:
            raise ValueError(f"frame {frame}: expected {width * height * 4} bytes, got {len(raw)}")
        rgb = memoryview(raw)
        return [(rgb[i + 2], rgb[i + 1], rgb[i]) for i in range(0, len(raw), 4)]
    a, b = read(first), read(second)
    luma_a = sum((r * 54 + g * 183 + bl * 19) // 256 for r, g, bl in a) / len(a)
    luma_b = sum((r * 54 + g * 183 + bl * 19) // 256 for r, g, bl in b) / len(b)
    pale_a = sum(min(x) >= 160 and max(x) - min(x) <= 20 for x in a)
    pale_b = sum(min(x) >= 160 and max(x) - min(x) <= 20 for x in b)
    diffs = [abs(ar - br) + abs(ag - bg) + abs(ab - bb)
             for (ar, ag, ab), (br, bg, bb) in zip(a, b)]
    return {"frames": [first, second], "mean_luma": [round(luma_a, 3), round(luma_b, 3)],
            "mean_absolute_rgb_delta": round(sum(diffs) / len(diffs) / 3, 3),
            "pixels_delta_at_least_24": sum(d >= 72 for d in diffs),
            "pale_neutral_pixels": [pale_a, pale_b]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, default=Path("/tmp/x3-bottleX3-run194/session-20260920-215930-212.log"))
    parser.add_argument("--captures", type=Path, default=Path("/tmp/x3-bottleX3-run194"))
    parser.add_argument("--validate", type=Path, help="existing compact-repro.json to compare")
    args = parser.parse_args()

    digest = hashlib.sha256(); anchor = anchor_qpc = frequency = None
    captures, sectors, fog_events, card_draws, cuts = [], [], [], Counter(), []
    width = height = None
    with args.log.open("rb") as raw:
        for binary in raw:
            digest.update(binary)
            line = binary.decode("utf-8", "replace").strip()
            if not line:
                continue
            name = line.split(" ", 1)[0]; data = fields(line)
            if name == "clock_anchor":
                anchor = datetime.fromisoformat(data["utc"].replace("Z", "+00:00")); anchor_qpc = int(data["qpc"]); frequency = int(data["qpc_frequency"])
            if name == "volumetric_fog_prepare":
                width, height = int(data["width"]), int(data["height"])
            try:
                frame = int(data["frame"])
            except KeyError:
                continue
            if name == "frame_end" and data.get("capture") == "1":
                captures.append((frame, int(data["qpc"])))
            elif name == "sector_background":
                sectors.append((frame, data))
            elif name.startswith("volumetric_fog_"):
                fog_events.append((frame, name, data))
            elif name == "draw" and data.get("ps") == CARD_PS:
                card_draws[frame] += 1
            elif name == "motion_output_cut" and data.get("cut") == "1":
                cuts.append(frame)
    if None in (anchor, anchor_qpc, frequency, width, height):
        raise ValueError("missing clock anchor, QPC frequency, or fog target dimensions")
    groups = []
    for item in captures:
        if not groups or item[0] != groups[-1][-1][0] + 1:
            groups.append([])
        groups[-1].append(item)
    def prior(items, frame):
        return max((item for item in items if item[0] <= frame), default=None, key=lambda item: item[0])
    bursts = []
    for group in groups:
        start, end = group[0][0], group[-1][0]
        sector = prior(sectors, start)
        profile = prior([x for x in fog_events if x[1] == "volumetric_fog_sector"], start)
        strength = prior([x for x in fog_events if x[1] in {"volumetric_fog_strength", "volumetric_fog_toggle"}], start)
        bursts.append({"frames": [start, end], "count": len(group),
                       "utc": [utc(anchor, group[0][1], anchor_qpc, frequency), utc(anchor, group[-1][1], anchor_qpc, frequency)],
                       "sector": sector[1].get("name", "").strip('"') if sector else None,
                       "profile": int(profile[2]["profile"]) if profile else None,
                       "strength": float(strength[2]["strength"]) if strength else None,
                       "density_scale": float(strength[2]["density_scale"]) if strength else None})
    final_start, final_end = bursts[-1]["frames"]
    final_cuts = [f for f in cuts if final_start <= f <= final_end]
    cut_runs = []
    for frame in final_cuts:
        if not cut_runs or frame != cut_runs[-1][-1] + 1:
            cut_runs.append([])
        cut_runs[-1].append(frame)
    warmups = [[run[0] + 1, min(run[-1] + 1, final_end)] for run in cut_runs]
    first_warmup_start, first_warmup_end = warmups[0]
    result = {
        "source_log_sha256": digest.hexdigest(), "qpc_frequency": frequency,
        "fog_target": [width, height], "capture_bursts": bursts,
        "final_burst": {"frames": [final_start, final_end], "cut_frames": final_cuts,
          "expected_warmup_native_card_windows": warmups,
          "source_card_draws": {"ps": CARD_PS, "total": sum(card_draws[f] for f in range(final_start, final_end + 1)),
            "warmup_windows": [sum(card_draws[f] for f in range(start, end + 1)) for start, end in warmups],
            "per_frame": {str(f): card_draws[f] for f in range(final_start, final_end + 1)}},
          "pixel_witness": [pixel_stats(args.captures, cut_runs[0][0], first_warmup_start, width, height),
                            pixel_stats(args.captures, first_warmup_end, first_warmup_end + 1, width, height)]}}
    if args.validate:
        expected = json.loads(args.validate.read_text())
        burst_keys = ("frames", "count", "utc", "sector", "profile", "strength", "density_scale")
        expected_bursts = [{k: item[k] for k in burst_keys} for item in expected["capture_bursts"]]
        actual_bursts = [{k: item[k] for k in burst_keys} for item in bursts]
        expected_pixels = expected["final_burst"]["present_pixel_witness"]
        actual_pixels = result["final_burst"]["pixel_witness"]
        checks = [expected["source_log_sha256"] == result["source_log_sha256"],
                  expected["qpc_frequency"] == result["qpc_frequency"],
                  expected_bursts == actual_bursts,
                  expected["final_burst"]["cut_frames"] == final_cuts,
                  expected["final_burst"]["source_card_draws"]["draws"] == result["final_burst"]["source_card_draws"]["total"],
                  [x["source_card_draws"] for x in expected["final_burst"]["card_draw_windows"] if x["expected_policy"] == "warmup-native-cards"] == result["final_burst"]["source_card_draws"]["warmup_windows"],
                  expected_pixels["f31495_to_f31496"] == {k: actual_pixels[0][k] for k in expected_pixels["f31495_to_f31496"]},
                  expected_pixels["f31506_to_f31507"] == {k: actual_pixels[1][k] for k in expected_pixels["f31506_to_f31507"]}]
        result["validation"] = {"path": str(args.validate), "checks": len(checks), "passed": sum(checks), "ok": all(checks)}
        if not all(checks):
            raise SystemExit(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
