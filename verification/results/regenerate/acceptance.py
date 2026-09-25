#!/usr/bin/env python3
"""Acceptance of x3m-regenerate on the synthetic root of verification/analysis/test_regenerate.py:
the source script, the host bundle dist/x3m-regenerate and (--windows) dist/x3m-regenerate.exe under Wine in
bottle X3M-Build each run on a fresh root with --no-wait; prints exit codes, wall time, the console lines
with timestamps and root paths removed, and whether they equal the source run's lines (order of the
'processing model' lines and their i/n counters follow completion order with several jobs, so the counter
is masked and the comparison is on the sorted set).
Output: acceptance_out.txt beside this script."""
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'verification' / 'analysis'), str(ROOT / 'verification' / 'probe')]
from test_regenerate import make_root  # noqa: E402

SECTOR = re.compile(r'  \| (==|\s+\d+ draws|    )')   # lod_batch_census.sector_report rows
CX_WINE = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'


def zpath(p):
    return 'Z:' + str(Path(p).resolve()).replace('/', '\\')


def normalise(text, game, windows=False):
    game = game.resolve()
    out = []
    for line in text.splitlines():
        line = re.sub(r'^\[\d\d:\d\d:\d\d\] ', '', line)
        line = line.replace(zpath(game) if windows else str(game), '<game>').replace(
            zpath(game.parents[1]) if windows else str(game.parents[1]), '<root>')
        line = re.sub(r'(done in|regenerated in) [\d.]+ (s|min)', r'\1 <t> \2', line).replace('\\', '/')
        out.append(line)
    return out


def main():
    rows, reference = [], None
    kinds = [('source', lambda g: [sys.executable, ROOT / 'tools/regenerate/x3m_regenerate.py', '--game-dir', g]),
             ('bundle', lambda g: [ROOT / 'dist/x3m-regenerate', '--game-dir', g])]
    if '--windows' in sys.argv:
        kinds.append(('windows', lambda g: [sys.executable, ROOT / 'verification/probe/wine_lock.py', CX_WINE,
                                            '--bottle', 'X3M-Build', zpath(ROOT / 'dist/x3m-regenerate.exe'),
                                            '--game-dir', zpath(g)]))
    with tempfile.TemporaryDirectory(prefix='x3m-regen-accept-') as folder:
        for kind, cmd in kinds:
            game = make_root(Path(folder) / kind)
            t0 = time.monotonic()
            r = subprocess.run([str(c) for c in cmd(game)] + ['--no-wait', '--jobs', '2'], capture_output=True, text=True,
                               errors='replace')
            wall = time.monotonic() - t0
            lines = normalise(r.stdout, game, kind == 'windows')
            log = normalise((game / 'x3m-regenerate.log').read_text(encoding='utf-8'), game, kind == 'windows')
            teed = all(l in log for l in lines)
            lines = [re.sub(r' \(\d+/\d+\)', ' (i/n)', l) if l.startswith('processing model') else l for l in lines]
            if reference is None:
                reference = lines
            rows.append(f'{kind}: exit {r.returncode}, wall {wall:.1f} s, {len(lines)} console lines, all in the log'
                        f' {teed}, log lines {len(log)}, same line set as source {sorted(lines) == sorted(reference)}')
            if kind == 'source':
                rows += ['  ' + l for l in lines]
                ref_log = log
            else:
                rows += [f'  only in {kind}: {l}' for l in sorted(set(lines) - set(reference))]
                rows += [f'  only in source: {l}' for l in sorted(set(reference) - set(lines))]
                extra = [l for l in ref_log if l not in log]
                sector = [l for l in extra if SECTOR.match(l)]
                rows.append(f'  source-log detail lines not in the {kind} log: {len(extra)}, of which {len(sector)} are'
                            ' the sector census report (repository result files, not bundled)'
                            + ''.join(f'\n    other: {l[:150]}' for l in extra if not SECTOR.match(l)))
    text = '\n'.join(rows) + '\n'
    (Path(__file__).parent / 'acceptance_out.txt').write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
