#!/usr/bin/env python3
"""Evidence for tools/analysis/fog_families.py (run from the repository root; read-only for the
bottle and the mod trees; outputs go to SCRATCH, only the compact summary and tables here).

  run_tool_evidence.py SCRATCH [--fixture HOST_FIXTURE]

1. stock bottle X3, --dry-run --jobs 4                        (fog-families-stock-dryrun.txt)
2. stock bottle, --out --background-palette earth xtmgreenring --jobs 4
3. synthetic vanilla+mod root (make_mod_root.py), --dry-run --jobs 4   (fog-families-mod-dryrun.txt)
4. same root, --out --jobs 1 and --out --jobs 4 (wall time, bytes, packets)
5. optional: the host build of verification/probe/fog_family_file_fixture.cpp --probe on the
   jobs-4 file through X3M_FOG_FAMILIES (every row loaded and decoded by the DLL's loader code)
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
TOOL = ROOT / 'tools/analysis/fog_families.py'


def run(args, env=None):
    started = time.monotonic()
    done = subprocess.run([sys.executable, str(TOOL), *map(str, args)], cwd=ROOT, capture_output=True, text=True, env=env)
    if done.returncode:
        raise SystemExit(f'{args}: exit {done.returncode}\n{done.stderr[-2000:]}')
    lines = done.stdout.strip().splitlines()
    summary = json.loads(next(l for l in reversed(lines) if l.startswith('{')))
    return lines, summary, round(time.monotonic() - started, 1)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('scratch', type=Path)
    p.add_argument('--fixture', type=Path, help='host build of fog_family_file_fixture.cpp')
    a = p.parse_args()
    s = a.scratch.resolve()
    out = []
    root = s / 'fogfam-modroot'
    if not root.exists():
        subprocess.run([sys.executable, str(HERE / 'make_mod_root.py'), str(root)], check=True, capture_output=True)
    lines, summary, wall = run(['--dry-run', '--jobs', '4'])
    (HERE / 'fog-families-stock-dryrun.txt').write_text('\n'.join(lines) + '\n')
    matches = sum('matches_build=1' in l for l in lines)
    out.append(f'stock bottle X3 --dry-run --jobs 4: families={summary["families"]} counts={summary["counts"]} '
               f'provisional palettes matching the build={matches}/12 tool_wall_s={summary["wall_seconds"]} process_wall_s={wall}')
    lines, summary, wall = run(['--out', s / 'fogfam-out-stock-bg', '--background-palette', 'earth', 'xtmgreenring', '--jobs', '4'])
    out.append(f'stock bottle X3 --out --background-palette earth xtmgreenring --jobs 4: counts={summary["counts"]} '
               f'file_bytes={summary["file_bytes"]} packets={summary["packets"]} tool_wall_s={summary["wall_seconds"]}')
    lines, summary, wall = run(['--game', root, '--dry-run', '--jobs', '4'])
    (HERE / 'fog-families-mod-dryrun.txt').write_text('\n'.join(lines) + '\n')
    out.append(f'mod root --dry-run --jobs 4: families={summary["families"]} counts={summary["counts"]} '
               f'tbackgrounds={summary["tbackgrounds"]} tool_wall_s={summary["wall_seconds"]} process_wall_s={wall}')
    for jobs in (1, 4):
        target = s / f'fogfam-out-mod-j{jobs}'
        lines, summary, wall = run(['--game', root, '--out', target, '--jobs', jobs])
        out.append(f'mod root --out --jobs {jobs}: counts={summary["counts"]} file_bytes={summary["file_bytes"]} '
                   f'packets={summary["packets"]} tool_wall_s={summary["wall_seconds"]} process_wall_s={wall}')
    same = (s / 'fogfam-out-mod-j1/x3m/fog-families.bin').read_bytes() == (s / 'fogfam-out-mod-j4/x3m/fog-families.bin').read_bytes()
    out.append(f'jobs 1 and jobs 4 files byte-identical: {same}')
    check = subprocess.run([sys.executable, str(TOOL), '--game', root, '--check', '--out', s / 'fogfam-out-mod-j4'],
                           cwd=ROOT, capture_output=True, text=True)
    out.append(f'--check on the jobs-4 file: exit {check.returncode} {check.stdout.strip().splitlines()[-1][:80] if check.stdout.strip() else ""}')
    if a.fixture:
        env = dict(os.environ, X3M_FOG_FAMILIES=str(s / 'fogfam-out-mod-j4/x3m/fog-families.bin'))
        probe = subprocess.run([str(a.fixture), '--probe'], capture_output=True, text=True, env=env)
        ok = sum(1 for l in probe.stdout.splitlines() if l.startswith('DECODE ') and 'status=ok' in l and 'match=1' in l and 'found_after=1' in l)
        table = next((l for l in probe.stdout.splitlines() if l.startswith('TABLE ')), '')
        out.append(f'host loader probe (X3M_FOG_FAMILIES): exit {probe.returncode} {" ".join(table.split()[1:6])} decoded_ok={ok}')
    text = '\n'.join(out) + '\n'
    (HERE / 'summary.txt').write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
