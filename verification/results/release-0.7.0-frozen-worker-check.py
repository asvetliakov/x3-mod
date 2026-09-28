#!/usr/bin/env python3
"""Release 0.7.0 check of the spawned bake-worker launcher inside the frozen x3m-regenerate executables.

Builds one synthetic game root (make_root of verification/analysis/test_regenerate.py with 02.cat widened to
more bakeable bodies), then for each mode copies it (copytree, no symlinks) to the same fixed path, runs
x3m-regenerate --game-dir ROOT --no-wait --jobs 3, samples the process table every 50 ms, and records exit
code, wall time, processing lines, distinct bake-worker PIDs, processes left after exit and the sha256 of
every file in the root afterwards. Modes: source (python3 tools/regenerate/x3m_regenerate.py), host (the
built binary), windows (the .exe under Wine in bottle X3M-Build through verification/probe/wine_lock.py).

  release-0.7.0-frozen-worker-check.py --dist DIR --work DIR --out JSON [--modes source,host,windows,windows:1]
(MODE:N runs --jobs N instead of 3 and is recorded as MODE-jN.)
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
for p in (REPO / 'verification' / 'analysis', REPO / 'verification' / 'probe', REPO / 'tools' / 'analysis',
          REPO / 'tools' / 'regenerate'):
    sys.path.insert(0, str(p))


def make_template(folder):
    from test_regenerate import make_root
    from sector_fog_census import write_catalogue
    from test_bob1 import atlas_tree_lod0
    from test_lod_overlay_batch import mixed_tree, packed, uv2_tree, with_threshold
    game = make_root(folder)
    write_catalogue(game / '02.cat', [
        ('objects/ships/x/good.pbb', packed(atlas_tree_lod0())),
        ('objects/ships/x/good2.pbb', packed(atlas_tree_lod0())),
        ('objects/ships/x/good3.pbb', packed(atlas_tree_lod0())),
        ('objects/ships/x/mixed.pbb', packed(mixed_tree())),
        ('objects/ships/x/uv.pbb', packed(uv2_tree())),
        ('objects/ships/x/badtext.pbd', b'BODY 0\n'),
        ('objects/stations/y/good.pbb', packed(with_threshold(atlas_tree_lod0(), 160)))])
    return game


def ps_rows():
    out = subprocess.run(['ps', '-A', '-o', 'pid=,ppid=,command='], capture_output=True, text=True).stdout
    rows = {}
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3:
            rows[int(parts[0])] = (int(parts[1]), parts[2])
    return rows


def descendants(rows, root):
    kids, found, stack = {}, set(), [root]
    for pid, (ppid, _) in rows.items():
        kids.setdefault(ppid, []).append(pid)
    while stack:
        for k in kids.get(stack.pop(), []):
            if k not in found:
                found.add(k)
                stack.append(k)
    return found


def is_worker(cmd):
    return ('spawn_main' in cmd or '--multiprocessing-fork' in cmd) and 'resource_tracker' not in cmd


def related(mode, cmd, exe_name):
    return exe_name in cmd if mode == 'windows' else False


def run_mode(mode, cmd, env, exe_name):
    seen, stop = {}, threading.Event()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)

    def sample():
        while not stop.is_set():
            rows = ps_rows()
            pids = descendants(rows, proc.pid) if mode != 'windows' else set()
            pids |= {p for p, (_, c) in rows.items() if related(mode, c, exe_name)}
            for p in pids:
                if p in rows:
                    seen.setdefault(p, rows[p][1][:300])
            time.sleep(0.05)
    t = threading.Thread(target=sample, daemon=True)
    t0 = time.monotonic()
    t.start()
    out = proc.stdout.read().decode('utf-8', 'replace')
    rc = proc.wait()
    wall = time.monotonic() - t0
    stop.set()
    t.join()
    time.sleep(3)
    rows = ps_rows()
    left = sorted(p for p, c in seen.items() if p in rows and rows[p][1][:300] == c)
    left += sorted(p for p, (_, c) in rows.items() if 'x3m-regenerate' in c or 'x3m_regenerate' in c)
    workers = sorted(p for p, c in seen.items() if is_worker(c))
    return dict(exit=rc, wall_s=round(wall, 1), processing_model_lines=out.count('processing model '),
                all_done='all done' in out, processes_seen=len(seen), worker_pids=len(workers),
                resource_trackers=sum('resource_tracker' in c for c in seen.values()),
                left_after_exit=sorted(set(left)), bake_workers_line=next(
                    (l.strip()[:200] for l in out.splitlines() if 'baking with up to' in l), None)), out


def snapshot(game):
    return {str(p.relative_to(game)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(game.rglob('*')) if p.is_file()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dist', type=Path, required=True)
    ap.add_argument('--work', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--modes', default='source,host')
    a = ap.parse_args()
    import build as regen_build
    work = a.work.resolve()
    template = work / 'template'
    if not template.exists():
        make_template(template)
    fixed = work / 'run'
    record = json.loads(a.out.read_text()) if a.out.exists() else {}
    record.setdefault('modes', {})
    snaps = {}
    for spec in a.modes.split(','):
        mode, _, jobs = spec.partition(':')
        jobs = jobs or '3'
        name = mode if jobs == '3' else f'{mode}-j{jobs}'
        if fixed.exists():
            shutil.rmtree(fixed)
        shutil.copytree(template, fixed, symlinks=False)
        game = fixed / 'drive_c' / 'X3'
        env = dict(os.environ)
        args = ['--no-wait', '--jobs', jobs]
        exe_name = 'x3m-regenerate'
        if mode == 'source':
            cmd = [sys.executable, str(REPO / 'tools/regenerate/x3m_regenerate.py'), '--game-dir', str(game)] + args
        elif mode == 'host':
            exe = a.dist / 'x3m-regenerate'
            cmd = [str(exe), '--game-dir', str(game)] + args
        else:
            exe = a.dist / 'x3m-regenerate.exe'
            exe_name = 'x3m-regenerate.exe'
            env['X3M_FIXTURE_BOTTLE'] = 'X3'
            cmd = [sys.executable, str(REPO / 'verification/probe/wine_lock.py'), '--holder',
                   'release 0.7.0 frozen worker check (X3M-Build)', str(regen_build.CX_BIN / 'wine'), '--bottle',
                   'X3M-Build', regen_build.zpath(exe), '--game-dir', regen_build.zpath(game)] + args
        res, out = run_mode(mode, cmd, env, exe_name)
        (work / f'console-{name}.txt').write_text(out)
        snap = snapshot(game)
        snaps[name] = snap
        (work / f'snapshot-{name}.json').write_text(json.dumps(snap, indent=1))
        res['files'] = len(snap)
        record['modes'][name] = res
        dest = work / f'result-{name}'
        if dest.exists():
            shutil.rmtree(dest)
        fixed.rename(dest)
        print(name, json.dumps(res), flush=True)
    base = snaps.get('source') or json.loads((work / 'snapshot-source.json').read_text())
    for mode, snap in snaps.items():
        if mode == 'source':
            continue
        diff = sorted(k for k in set(base) | set(snap) if base.get(k) != snap.get(k))
        overlay = [k for k in set(base) | set(snap) if k.endswith(('.cat', '.dat')) and 'x3m-lod' in k
                   or k.startswith('addon/0') and k.endswith(('.cat', '.dat'))]
        record['modes'][mode]['vs_source'] = dict(
            differing_files=diff, overlay_members=sorted(overlay),
            overlay_identical=all(base.get(k) == snap.get(k) for k in overlay))
        if mode == 'windows-j1':
            ref = snaps.get('windows') or json.loads((work / 'snapshot-windows.json').read_text())
            record['modes'][mode]['vs_windows_jobs3'] = dict(
                differing_files=sorted(k for k in set(ref) | set(snap) if ref.get(k) != snap.get(k)),
                overlay_identical=all(ref.get(k) == snap.get(k) for k in overlay))
    a.out.write_text(json.dumps(record, indent=1) + '\n')


if __name__ == '__main__':
    main()
