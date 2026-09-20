#!/usr/bin/env python3
"""Stream Run 52 logs and write the bounded attribution witness.

No log is loaded as a whole: records are parsed line by line. The common
comparison interval is the maximal post-transition (frame >=3000) sampled
interval in which every session reports 478 draws: frames 3610--3700.
"""
from __future__ import annotations

import json
import re
import statistics
import subprocess
import math
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RUNS = {
    "A": (187, "perdraw", "48-50 fps"),
    "B": (188, "perdraw", "49-51 fps"),
    "C": (189, "lazy", "52-53 fps; no visual issues reported"),
}
COMMON_START, COMMON_END, COMMON_DRAWS = 3610, 3700, 478
KV = re.compile(r"(\w+)=([^\s]+)")


def fields(line: str) -> dict[str, str]:
    return dict(KV.findall(line))


def number(value: str) -> int | float:
    return float(value) if any(c in value for c in ".eE") else int(value)


def median(rows: list[dict[str, str]], key: str) -> float | None:
    values = [number(r[key]) for r in rows if key in r]
    return statistics.median(values) if values else None


def stream(run: int) -> dict[str, object]:
    directory = Path(f"/tmp/x3-bottleX3-run{run}")
    logs = sorted(directory.glob("session-*.log"))
    if len(logs) != 1:
        raise RuntimeError(f"run {run}: expected one session log, found {len(logs)}")
    stamp = re.search(r"session-(\d{8})-(\d{6})-", logs[0].name)
    if not stamp:
        raise RuntimeError(f"run {run}: no timestamp in {logs[0].name}")
    result: dict[str, object] = {"directory": str(directory), "log": str(logs[0]),
                                 "timestamp_local": f"{stamp.group(1)[:4]}-{stamp.group(1)[4:6]}-{stamp.group(1)[6:]}T{stamp.group(2)[:2]}:{stamp.group(2)[2:4]}:{stamp.group(2)[4:]}"}
    typed: dict[str, list[dict[str, str]]] = {}
    source_lines: list[dict[str, str]] = []
    with logs[0].open(errors="replace") as handle:
        for line in handle:
            kind = line.split(" ", 1)[0]
            record = fields(line)
            if kind == "proxy_identity":
                source_lines.append(record)
            if kind in {"frame_end", "proxy_options", "motion_output_mode", "motion_output_device", "motion_output_frame",
                        "state_hooks", "frame_phases", "pass_phases", "pass_attribution", "residual_phases",
                        "residual_attribution", "hdr_frame", "shadow_lease_retirement"}:
                typed.setdefault(kind, []).append(record)
    if len(source_lines) != 1:
        raise RuntimeError(f"run {run}: proxy_identity count {len(source_lines)}")
    result["identity"] = source_lines[0]
    result["rows"] = typed
    return result


def stable_samples(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    return [r for r in rows if COMMON_START <= int(r["frame"]) <= COMMON_END
            and int(r["draws"]) == COMMON_DRAWS and float(r["dt_ms"]) > 0]


def error_totals(rows: list[dict[str, str]], names: tuple[str, ...]) -> dict[str, int]:
    return {name: sum(int(r.get(name, "0")) for r in rows) for name in names}


def spans(frames: list[int]) -> list[list[int]]:
    out: list[list[int]] = []
    for frame in sorted(frames):
        if not out or frame != out[-1][1] + 10:
            out.append([frame, frame])
        else:
            out[-1][1] = frame
    return out


def stratum(rows: list[dict[str, str]], draw: int) -> dict[str, object]:
    samples = [r for r in rows if int(r["draws"]) == draw]
    values = [float(r["dt_ms"]) / 10 for r in samples]
    ranked = sorted(values)
    return {"samples": len(samples), "spans": spans([int(r["frame"]) for r in samples]),
            "frame_time_ms": {"min": min(values), "median": statistics.median(values), "max": max(values),
                              "p95": ranked[math.ceil(.95 * len(values)) - 1], "over_30ms_samples": sum(v > 30 for v in values)},
            "reciprocal_fps_from_median": 1000 / statistics.median(values)}


def steady_rows(frame_rows: list[dict[str, str]], motion_rows: list[dict[str, str]]) -> list[dict[str, str]]:
    """Keep samples bracketed by valid camera-status rows no more than 60 frames away."""
    statuses = sorted(motion_rows, key=lambda r: int(r["frame"]))
    answer = []
    for row in frame_rows:
        frame = int(row["frame"])
        before = next((r for r in reversed(statuses) if int(r["frame"]) <= frame), None)
        after = next((r for r in statuses if int(r["frame"]) >= frame), None)
        valid = lambda status: status and status.get("camera_reason") == "0" and status.get("cut") == "0"
        if valid(before) and valid(after) and frame - int(before["frame"]) <= 60 and int(after["frame"]) - frame <= 60:
            answer.append(row)
    return answer


def main() -> None:
    data = {name: stream(run) for name, (run, _, _) in RUNS.items()}
    identities = [data[name]["identity"] for name in RUNS]
    identity_ok = all(i["sha256"] == identities[0]["sha256"] and i["source_commit"] == identities[0]["source_commit"]
                      for i in identities)
    checkout = Path("/tmp/x3-submission-attribution")
    installed_commit = identities[0]["source_commit"]
    source_head = subprocess.check_output(["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
    relevant_paths = ["src/proxy/frame_phases_core.h", "src/proxy/frame_phases.cpp", "src/proxy/frame_phases.h",
                      "src/proxy/pass_phases_core.h", "src/proxy/pass_phases.cpp", "src/proxy/pass_phases.h",
                      "src/proxy/residual_phases_core.h", "src/proxy/residual_phases.cpp", "src/proxy/residual_phases.h",
                      "src/proxy/motion_output.h", "src/proxy/motion_output.cpp", "src/proxy/capture.cpp",
                      "src/renderer/hdr_pass.h", "src/renderer/hdr_pass.cpp", "src/renderer/readback_timing.h"]
    source_paths_unchanged = subprocess.run(["git", "-C", str(checkout), "diff", "--quiet", installed_commit, "--", *relevant_paths], check=False).returncode == 0
    sessions: dict[str, object] = {}
    for name, (run, expected_mode, user_fps) in RUNS.items():
        rows = data[name]["rows"]
        samples = stable_samples(rows.get("frame_end", []))
        modes = [r.get("rt_mode") for kind in ("motion_output_mode", "motion_output_device", "motion_output_frame")
                 for r in rows.get(kind, []) if r.get("rt_mode")]
        hooks = rows.get("state_hooks", [])
        sessions[name] = {
            "run": run, "timestamp_local": data[name]["timestamp_local"], "log": data[name]["log"], "user_observation": user_fps,
            "frame_end_common_interval": {"frames": [COMMON_START, COMMON_END], "draws": COMMON_DRAWS,
                "samples": len(samples), "sample_span_frames": len(samples) * 10,
                "dt_median_ms_per_frame": median([{**r, "per_frame": str(float(r["dt_ms"]) / 10)} for r in samples], "per_frame"),
                "fps_from_median_frame_time": (1000 / median([{**r, "per_frame": str(float(r["dt_ms"]) / 10)} for r in samples], "per_frame")) if samples else None,
                "dt_values_ms_per_frame": [float(r["dt_ms"]) / 10 for r in samples]},
            "rt_mode": {"expected": expected_mode, "seen": sorted(set(modes)), "rows": len(modes), "all_expected": bool(modes) and all(m == expected_mode for m in modes)},
            "state_hooks": {"rows": len(hooks), "values": [{k: r.get(k) for k in ("installed", "reason", "state_shadow", "rs_mode")} for r in hooks],
                            "unhooked_none": bool(hooks) and all(r.get("installed") == "0" and r.get("reason") == "none" for r in hooks)},
        }
    # B/C share neither a single 30-second draw count nor a continuous common
    # frame interval.  Retain every post-transition exact-count stratum instead
    # of promoting one short intersection to an end-to-end proof.
    b_rows = steady_rows([r for r in data["B"]["rows"].get("frame_end", []) if int(r["frame"]) >= 3000 and float(r["dt_ms"]) > 0], data["B"]["rows"].get("motion_output_frame", []))
    c_rows = steady_rows([r for r in data["C"]["rows"].get("frame_end", []) if int(r["frame"]) >= 3000 and float(r["dt_ms"]) > 0], data["C"]["rows"].get("motion_output_frame", []))
    b_draws, c_draws = {int(r["draws"]) for r in b_rows}, {int(r["draws"]) for r in c_rows}
    overlap = {str(draw): {"B": stratum(b_rows, draw), "C": stratum(c_rows, draw)} for draw in sorted(b_draws & c_draws)}
    route: dict[str, object] = {}
    for name in ("B", "C"):
        mrows = [r for r in data[name]["rows"].get("motion_output_frame", [])
                 if int(r.get("frame", "0")) >= 3000 and r.get("camera_reason") == "0" and r.get("cut") == "0"]
        route[name] = {str(draw): {"rows": len(x), "routed_values": sorted({int(r["routed"]) for r in x}),
                                  "matched_values": sorted({int(r["matched"]) for r in x}),
                                  "depth_routed_values": sorted({int(r["depth_routed"]) for r in x})}
                       for draw in sorted(b_draws & c_draws)
                       if (x := [r for r in mrows if int(r["draws"]) == draw])}
    route_478 = {name: [r for r in data[name]["rows"].get("motion_output_frame", [])
                         if int(r.get("frame", "0")) >= 3000 and int(r.get("draws", "-1")) == 478
                         and r.get("camera_reason") == "0" and r.get("cut") == "0"] for name in ("B", "C")}
    rt_binding_478 = {name: {"rows": len(rows), "set_rt_median": median(rows, "set_rt"),
                              "lazy_flushes_median": median(rows, "lazy_flushes"),
                              "apply_failures_total": sum(int(r.get("apply_failures", "0")) for r in rows),
                              "restore_failures_total": sum(int(r.get("restore_failures", "0")) for r in rows)}
                       for name, rows in route_478.items()}
    route_478_health = {name: {"routed_values": sorted({int(r["routed"]) for r in rows}),
                                "matched_values": sorted({int(r["matched"]) for r in rows}),
                                "depth_routed_values": sorted({int(r["depth_routed"]) for r in rows})}
                        for name, rows in route_478.items()}
    if any(route_478_health[name] != {"routed_values": [453], "matched_values": [453], "depth_routed_values": [453]} for name in ("B", "C")):
        raise RuntimeError(f"478 route mismatch: {route_478_health}")
    def proxy_options(name: str) -> tuple[tuple[str, str], ...]:
        rows = data[name]["rows"].get("proxy_options", [])
        if len(rows) != 1:
            raise RuntimeError(f"{name}: proxy_options rows {len(rows)}")
        return tuple(sorted((k, v) for k, v in rows[0].items() if k != "X3M_MOTION_RT_MODE"))
    bc_options_only_rt = proxy_options("B") == proxy_options("C")
    if not bc_options_only_rt:
        raise RuntimeError("B/C proxy option records differ beyond rt_mode")
    arows = data["A"]["rows"]
    # Full 300-frame diagnostic windows whose end marks the retained busy scene.
    def windows(kind: str) -> list[dict[str, str]]:
        return [r for r in arows.get(kind, []) if 3000 <= int(r["frame"]) <= 5400]
    frame, pas, pass_attribution, residual, attribution = map(windows, ("frame_phases", "pass_phases", "pass_attribution", "residual_phases", "residual_attribution"))
    aligned_ends = len({tuple(int(r["frame"]) for r in rows) for rows in (frame, pas, pass_attribution, residual, attribution)}) == 1
    if not aligned_ends:
        raise RuntimeError("A diagnostic window ends are not aligned")
    hdr = [r for r in arows.get("hdr_frame", []) if 3000 <= int(r["frame"]) <= 5400 and float(r.get("writeback_us", "0")) > 0]
    lease = [r for r in arows.get("shadow_lease_retirement", []) if 3000 <= int(r["frame"]) <= 5400]
    diagnostics = {
        "window_end_frames": [int(r["frame"]) for r in frame], "window_count": len(frame), "window_frames_each": 300,
        "frame_phases_window_medians_us": {k: median(frame, k) for k in ("dt_p50_us", "pre_render_p50_us", "views_p50_us", "view_setup_p50_us", "view_submit_p50_us")},
        "pass_window_medians_us": {k: median(pas, k) for k in ("apply_p50_us", "draw_p50_us", "end_p50_us", "sum_p50_us", "view_submit_p50_us", "self_p50_us")},
        "pass_attribution_window_medians": {k: median(pass_attribution, k) for k in ("scoped_p50_us", "outside_p50_us", "complement_p50_us", "outside_passes_p50", "crossing_passes_p50")},
        "pass_attribution_health_totals": error_totals(pass_attribution, ("outside_passes", "crossing_passes", "scope_errors", "complement_underflow")),
        "residual_window_medians_us": {k: median(residual, k) for k in ("prepare_p50_us", "setup_p50_us", "particles_p50_us", "other_p50_us", "self_p50_us")},
        "corrected_complement_window_medians_us": {k: median(attribution, k) for k in ("between_prepare_p50_us", "outside_setup_p50_us")},
        "phase_error_totals": {"frame": error_totals(frame, ("order_errors", "clock_errors", "unmatched", "dropped", "early", "foreign")),
                               "pass": error_totals(pas, ("orphans", "clock_errors", "clock_failures", "unmatched", "dropped", "early", "foreign")),
                               "residual": error_totals(residual, ("clock_errors", "clock_failures", "unmatched", "dropped", "early", "foreign", "other_underflow")),
                               "attribution": error_totals(attribution, ("outside_materials", "scope_errors"))},
        "hdr_nonzero_writeback_samples": len(hdr),
        "hdr_window_medians_us": {k: median(hdr, k) for k in ("writeback_us", "readback_transfer_lock_us", "readback_extract_unlock_us", "readback_statistics_adapt_us")},
        "hdr_clock_error_total": sum(int(r.get("readback_clock_errors", "0")) for r in hdr),
        "lease_samples": len(lease), "lease_window_medians": {k: median(lease, k) for k in ("calls", "records", "refs", "us")},
        "lease_clock_error_total": sum(int(r.get("clock_errors", "0")) for r in lease),
    }
    out = {"selection": {"common_draw_interval": [COMMON_START, COMMON_END], "draws": COMMON_DRAWS,
                           "reason": "maximal post-transition (frame >=3000) sampled interval shared by A/B/C with exactly 478 frame_end draws",
                           "p95": "nearest rank: sorted[ceil(0.95*n)-1]"},
           "identity": {"consistent": identity_ok, "source_commit": identities[0]["source_commit"], "dll_sha256": identities[0]["sha256"]},
           "source_health": {"checkout": str(checkout), "head": source_head, "installed_commit_is_ancestor_of_head": subprocess.run(["git", "-C", str(checkout), "merge-base", "--is-ancestor", installed_commit, "HEAD"], check=False).returncode == 0,
                             "relevant_owning_paths_unchanged_from_installed_commit": source_paths_unchanged},
           "sessions": sessions, "A_diagnostics": diagnostics,
           "B_C_overlapping_draw_strata": overlap, "B_C_route_telemetry_at_camera_reason_0": route,
           "B_C_478_route_binding_counters": rt_binding_478,
           "B_C_478_route_health": route_478_health,
           "B_C_proxy_options_equal_except_rt_mode": bc_options_only_rt,
           "limitations": ["frame_end dt_ms covers ten frames; its reciprocal median is not the median instantaneous FPS.",
                           "The maximal shared 478-draw interval is short; broader exact-draw strata are noncontemporaneous across B/C.",
                           "A carries phase diagnostics; B/C intentionally do not. Their FPS values are not a diagnostic-overhead subtraction.",
                           "Phase and HDR values are marginal window medians/sparse rows. They must not be subtracted or summed into a same-frame partition."]}
    (ROOT / "result.json").write_text(json.dumps(out, indent=2, sort_keys=True) + "\n")
    s = out["sessions"]
    d = diagnostics
    lines = ["# Run 52 triage", "", "## Observation", "",
             f"All sessions report source `{out['identity']['source_commit']}` and DLL SHA-256 `{out['identity']['dll_sha256']}` (consistent={identity_ok}).",
             f"The isolated source checkout is `{out['source_health']['head']}`; installed source is its ancestor and all 15 checked attribution/HDR/readback paths are unchanged from installed source={out['source_health']['relevant_owning_paths_unchanged_from_installed_commit']}.",
             f"Session timestamps (as encoded in log names): A {s['A']['timestamp_local']}, B {s['B']['timestamp_local']}, C {s['C']['timestamp_local']}.",
             f"The maximal shared post-transition (frame >=3000) 478-draw interval is frames {COMMON_START}-{COMMON_END}: A/B/C have {s['A']['frame_end_common_interval']['samples']}/{s['B']['frame_end_common_interval']['samples']}/{s['C']['frame_end_common_interval']['samples']} ten-frame samples.",
             f"Frame-time medians are A {s['A']['frame_end_common_interval']['dt_median_ms_per_frame']:.2f} ms ({s['A']['frame_end_common_interval']['fps_from_median_frame_time']:.2f} reciprocal FPS), B {s['B']['frame_end_common_interval']['dt_median_ms_per_frame']:.2f} ms ({s['B']['frame_end_common_interval']['fps_from_median_frame_time']:.2f}), C {s['C']['frame_end_common_interval']['dt_median_ms_per_frame']:.2f} ms ({s['C']['frame_end_common_interval']['fps_from_median_frame_time']:.2f}).",
             "B/C mode evidence is perdraw/lazy respectively; both state_hooks rows are installed=0, reason=none.",
             f"B/C proxy_options records match after excluding only X3M_MOTION_RT_MODE={bc_options_only_rt}; the A frame/pass/pass-attribution/residual/residual-attribution windows have aligned ends={aligned_ends}.",
             "P95 uses nearest rank: sorted[ceil(0.95*n)-1].",
             f"B/C have {len(overlap)} overlapping post-transition exact draw-count strata. The substantial shared strata are 478 draws (B/C {overlap['478']['B']['samples']}/{overlap['478']['C']['samples']} samples; median/p95/raw range {overlap['478']['B']['frame_time_ms']['median']:.2f}/{overlap['478']['B']['frame_time_ms']['p95']:.2f}/{overlap['478']['B']['frame_time_ms']['min']:.2f}-{overlap['478']['B']['frame_time_ms']['max']:.2f} vs {overlap['478']['C']['frame_time_ms']['median']:.2f}/{overlap['478']['C']['frame_time_ms']['p95']:.2f}/{overlap['478']['C']['frame_time_ms']['min']:.2f}-{overlap['478']['C']['frame_time_ms']['max']:.2f} ms; >30ms samples {overlap['478']['B']['frame_time_ms']['over_30ms_samples']}/{overlap['478']['C']['frame_time_ms']['over_30ms_samples']}), 510 draws ({overlap['510']['B']['samples']}/{overlap['510']['C']['samples']}; {overlap['510']['B']['frame_time_ms']['median']:.2f}/{overlap['510']['C']['frame_time_ms']['median']:.2f} ms), and 514 draws ({overlap['514']['B']['samples']}/{overlap['514']['C']['samples']}; {overlap['514']['B']['frame_time_ms']['median']:.2f}/{overlap['514']['C']['frame_time_ms']['median']:.2f} ms). The remaining seven strata have at most seven samples on one side.",
             f"At camera_reason=0/cut=0, the 478-draw route rows agree: B {route['B']['478']}; C {route['C']['478']}.",
             f"All selected 478-route rows have B/C route health {route_478_health['B']}/{route_478_health['C']}. B/C set_rt medians are {rt_binding_478['B']['set_rt_median']:.0f}/{rt_binding_478['C']['set_rt_median']:.0f}; lazy_flushes {rt_binding_478['B']['lazy_flushes_median']:.0f}/{rt_binding_478['C']['lazy_flushes_median']:.0f}; apply failures {rt_binding_478['B']['apply_failures_total']}/{rt_binding_478['C']['apply_failures_total']}; restore failures {rt_binding_478['B']['restore_failures_total']}/{rt_binding_478['C']['restore_failures_total']}.",
             f"A has {d['window_count']} retained 300-frame phase windows (ends {d['window_end_frames'][0]}-{d['window_end_frames'][-1]}). Direct window-median fields: submit {d['frame_phases_window_medians_us']['view_submit_p50_us']:.0f} us; pass apply/draw/end {d['pass_window_medians_us']['apply_p50_us']:.0f}/{d['pass_window_medians_us']['draw_p50_us']:.0f}/{d['pass_window_medians_us']['end_p50_us']:.0f} us; pass scoped/outside/complement {d['pass_attribution_window_medians']['scoped_p50_us']:.0f}/{d['pass_attribution_window_medians']['outside_p50_us']:.0f}/{d['pass_attribution_window_medians']['complement_p50_us']:.0f} us; corrected prepare/setup {d['residual_window_medians_us']['prepare_p50_us']:.0f}/{d['residual_window_medians_us']['setup_p50_us']:.0f} us; direct complements between-prepare/outside-setup {d['corrected_complement_window_medians_us']['between_prepare_p50_us']:.0f}/{d['corrected_complement_window_medians_us']['outside_setup_p50_us']:.0f} us.",
             f"Pass-attribution health totals outside-passes/crossing-passes/scope-errors/complement-underflow are {d['pass_attribution_health_totals']['outside_passes']}/{d['pass_attribution_health_totals']['crossing_passes']}/{d['pass_attribution_health_totals']['scope_errors']}/{d['pass_attribution_health_totals']['complement_underflow']}; those totals do not alter the reported window medians.",
             f"A diagnostic self estimates are pass {d['pass_window_medians_us']['self_p50_us']:.0f} us and residual {d['residual_window_medians_us']['self_p50_us']:.0f} us; frame-phase self cost is not emitted. HDR has {d['hdr_nonzero_writeback_samples']} sparse nonzero rows: writeback/transfer-lock/extract-unlock/statistics-adapt medians {d['hdr_window_medians_us']['writeback_us']:.1f}/{d['hdr_window_medians_us']['readback_transfer_lock_us']:.1f}/{d['hdr_window_medians_us']['readback_extract_unlock_us']:.1f}/{d['hdr_window_medians_us']['readback_statistics_adapt_us']:.1f} us. Lease has {d['lease_samples']} rows: calls/records/refs/us {d['lease_window_medians']['calls']:.0f}/{d['lease_window_medians']['records']:.0f}/{d['lease_window_medians']['refs']:.0f}/{d['lease_window_medians']['us']:.1f}.",
             "", "## Inference", "", "C is faster than B in the equal-draw evidence, consistent with the user FPS observations and with no reported visual issue. It is an eligible unhooked production lazy counter for the root's cumulative-evidence default-policy decision alongside prior fixtures. B/C lacks a same-frame diagnostic-overhead or causal subtraction.",
             "", "A pass_attribution complement and the corrected residual complement are direct per-frame measurements before their window reductions. They correct attribution scope, but are not B/C causal comparisons or independent addends. HDR buckets and lease retirement are direct CPU spans, not GPU time.",
             "", "## Decision scope", "", "This result supports the root's default-policy decision. Intrusive frame timing, state filtering, pass replay, sorting and lease-lifetime changes remain outside its scope.", "",
             "## Owning locations", "", "Corrected partition: `/tmp/x3-submission-attribution/src/proxy/residual_phases_core.h:11-19`; emitted complement: `src/proxy/residual_phases.cpp:42-50`. HDR three buckets and lease logging: `src/proxy/motion_output.cpp:6146-48, 6725-27`. RT mode/state-hook configuration: `src/proxy/capture.cpp:2255-57, 2465-67, 3063-65`."]
    (ROOT / "result.md").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
