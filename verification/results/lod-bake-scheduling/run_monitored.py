#!/usr/bin/env python3
"""Run a command and sample its process tree every --interval s with `ps` (macOS/Linux): wall time, peak and
mean resident bytes of the whole tree, and the number of multiprocessing workers (spawned children other than
the resource tracker) over time. Writes a compact JSON summary (--out) and prints one line.

  python3 run_monitored.py --out summary.json [--interval 0.5] [--phase-marker TEXT] -- CMD ...

--phase-marker: a line of the command's stdout containing TEXT starts the second phase (the bake after the
census); the summary gives worker counts per phase. The command's output is copied to --log (default
<out>.log).
"""
import argparse
import json
import subprocess
import sys
import threading
import time


def tree(root):
    out = subprocess.run(['ps', '-axo', 'pid=,ppid=,rss=,command='], capture_output=True, text=True).stdout
    procs = {}
    for line in out.splitlines():
        parts = line.split(None, 3)
        if len(parts) < 3:
            continue
        procs[int(parts[0])] = (int(parts[1]), int(parts[2]) * 1024, parts[3] if len(parts) > 3 else '')
    members, frontier = {root}, [root]
    while frontier:
        p = frontier.pop()
        for pid, (ppid, _, _) in procs.items():
            if ppid == p and pid not in members:
                members.add(pid)
                frontier.append(pid)
    rss = sum(procs[p][1] for p in members if p in procs)
    workers = sum(1 for p in members if p in procs and 'spawn_main' in procs[p][2])
    return rss, workers


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--log')
    ap.add_argument('--interval', type=float, default=0.5)
    ap.add_argument('--phase-marker')
    ap.add_argument('cmd', nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == '--' else a.cmd
    t0 = time.time()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    phase = {'t': None}
    log = open(a.log or a.out + '.log', 'w')

    def pump():
        for line in proc.stdout:
            log.write(line)
            if a.phase_marker and phase['t'] is None and a.phase_marker in line:
                phase['t'] = time.time() - t0
    th = threading.Thread(target=pump, daemon=True)
    th.start()
    samples = []
    while proc.poll() is None:
        rss, workers = tree(proc.pid)
        samples.append((time.time() - t0, rss, workers))
        time.sleep(a.interval)
    th.join()
    log.close()
    wall = time.time() - t0

    def stats(rows):
        if not rows:
            return None
        return dict(samples=len(rows), workers_max=max(r[2] for r in rows),
                    workers_mean=round(sum(r[2] for r in rows) / len(rows), 2),
                    rss_peak_gib=round(max(r[1] for r in rows) / 2**30, 2),
                    rss_mean_gib=round(sum(r[1] for r in rows) / len(rows) / 2**30, 2))
    split = phase['t']
    summary = dict(cmd=cmd, exit=proc.returncode, wall_s=round(wall, 1), interval_s=a.interval,
                   whole=stats(samples), phase_marker=a.phase_marker, phase2_start_s=split and round(split, 1),
                   phase1=stats([s for s in samples if split is not None and s[0] < split]),
                   phase2=stats([s for s in samples if split is not None and s[0] >= split]),
                   timeline=[[round(t, 1), round(r / 2**30, 2), w] for t, r, w in samples[::max(1, len(samples) // 400)]])
    with open(a.out, 'w') as f:
        json.dump(summary, f)
    print(json.dumps({k: v for k, v in summary.items() if k not in ('timeline', 'cmd')}))
    return proc.returncode


if __name__ == '__main__':
    sys.exit(main())
