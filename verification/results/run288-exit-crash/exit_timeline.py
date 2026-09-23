#!/usr/bin/env python3
"""Per run: session-log clock anchor, last row's qpc as UTC, the fault line's UTC from launcher-stderr.log, the gap,
whether the device-teardown summaries (shadow_retention_summary final=1 / hull_lightmap_widen_summary) were logged,
the logged engine pointer, and the collide handler address (proxy load delta). Reads only head/tail of each log."""
import glob, os, re, sys, datetime as dt
def utc(s): return dt.datetime.strptime(s.rstrip('Z'), "%Y-%m-%dT%H:%M:%S.%f")
def tail(path, n=400_000):
    with open(path, 'rb') as f:
        f.seek(max(0, os.path.getsize(path) - n)); return f.read().decode('utf-8', 'replace').splitlines()[1:]
def head(path, n=3_000_000):
    with open(path, 'rb') as f: return f.read(n).decode('utf-8', 'replace').splitlines()
for run in sys.argv[1:] or [str(r) for r in range(281, 289)]:
    d = f"/tmp/x3-bottleX3-run{run}"
    logs = sorted(glob.glob(f"{d}/session-*.log"))
    if not logs: print(f"run{run} no session log"); continue
    h = head(logs[0]); t = tail(logs[0])
    anchor = next((l for l in h if l.startswith("clock_anchor")), "")
    m = re.search(r"utc=(\S+) qpc=(\d+) qpc_frequency=(\d+)", anchor)
    a_utc, a_qpc, freq = utc(m.group(1)), int(m.group(2)), int(m.group(3))
    last_qpc = None
    for l in reversed(t):
        q = re.search(r"\bqpc=(\d+)", l)
        if q: last_qpc = int(q.group(1)); break
    last_utc = a_utc + dt.timedelta(seconds=(last_qpc - a_qpc) / freq)
    teardown = [k for k in ("shadow_retention_summary", "motion_output_mip_bias_summary", "hull_lightmap_widen_summary") if any(l.startswith(k) for l in t[-200:])]
    handler = next((re.search(r"handler=0x([0-9a-f]+)", l).group(1) for l in h[:400] if l.startswith("collide_sat_sse2")), None)
    import subprocess
    g = subprocess.run(["grep", "-m1", "-oE", "engine=(0[1-9a-f]|[1-9a-f][0-9a-f])[0-9a-f]{6}", logs[0]], capture_output=True, text=True).stdout.strip()
    engine = g.split("=")[1] if g else None
    commit = next((re.search(r"source_commit=([0-9a-f]{8})", l).group(1) for l in h[:10] if l.startswith("proxy_identity")), None)
    err = f"{d}/launcher-stderr.log"; fault = None
    if os.path.exists(err):
        for l in open(err, errors='replace'):
            if "Unhandled" in l or "page fault" in l or "Backtrace" in l:
                fault = l.strip(); break
    gap = ""
    if fault:
        fu = utc(re.match(r"\[(\S+)\]", fault).group(1)); gap = f" fault_utc={fu.time()} gap_s={(fu - last_utc).total_seconds():.3f}"
    delta = f"{int(handler,16) - 0x6fbd6e80:#x}" if handler and commit == "bc47873b" else "n/a"
    print(f"run{run} commit={commit} last_row_utc={last_utc.time()} teardown_rows={','.join(teardown) or 'none'} engine={engine} "
          f"sat_handler=0x{handler} proxy_delta_vs_pref={delta}{gap} fault={'yes' if fault else 'no'}")
