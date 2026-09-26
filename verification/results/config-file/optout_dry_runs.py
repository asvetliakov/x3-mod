#!/usr/bin/env python3
"""Settings file review (2026-09-26): what the DLL resolves for each launcher opt-out (the reviewer's opt-out script,
extended). For every opt-out, a hermetic `manage.py launch --dry-run` (test_config_schema.launch_env: temporary game
directory, fake proxy, nothing launched) in both launcher modes, resolved through tools/config/resolve.py:
- developer (default launch): X3M_CONFIG and how many schema defaults the DLL would pick up for variables the launcher
  left out (the review finding: 1 to 33 under the former X3M_CONFIG=none; 0 under bare);
- player (--config, no file): the X3M_* variables sent, and how many settings resolve differently from the developer
  launch (0: player mode without a file flies the same settings).
Writes optout_dry_runs.txt beside this script.

    python3 verification/results/config-file/optout_dry_runs.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'verification/analysis'))
sys.path.insert(0, str(ROOT / 'tools/config'))
import resolve  # noqa: E402
import schema  # noqa: E402
import test_config_schema as t  # noqa: E402

OPT_OUTS = (['--no-volumetric-fog'], ['--no-sun-occlusion'], ['--no-taa'], ['--no-hdr'], ['--camera', 'vanilla'], ['--no-shadow-cascades'],
            ['--no-lod-occlusion'], ['--no-music-keep'], ['--no-motion-output'], ['--no-ownership'], ['--cull-small-parts', '0'],
            ['--no-crypt-cache'], ['--no-direct'])
IGNORED = ('X3M_CONFIG', *t.INSTALLATION)


def effective(resolved):
    return {k: resolve.value(resolved, k) for k in resolved if k not in IGNORED}


def main():
    module, game, wine, directory = t.hermetic_launcher()
    lines, failures = [], []
    with directory:
        for args in OPT_OUTS:
            try:
                developer = t.launch_env(module, game, wine, *args)
                player = t.launch_env(module, game, wine, '--config', *args)
            except SystemExit as error:
                lines.append(f'{" ".join(args)}: exit {error.code} (no such option)')
                continue
            dev_resolved = resolve.resolve(developer)
            picked = sorted(k for k, (_, source) in dev_resolved.items() if source == 'default')
            a, b = effective(dev_resolved), effective(resolve.resolve(player))
            differ = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
            if picked or differ or developer.get('X3M_CONFIG') != 'bare':
                failures.append(args)
            lines.append(f'{" ".join(args)}: developer X3M_CONFIG={developer.get("X3M_CONFIG")} sent={len(developer)} '
                         f'defaults_picked_up={len(picked)}{picked[:6]} | player sent={len(player)} '
                         f'{sorted(player)[:6]}{"..." if len(player) > 6 else ""} differ_from_developer={len(differ)}{differ[:6]}')
    lines.append(f'schema defaults {sum(e["default"] is not None for e in schema.SETTINGS)}')
    text = '\n'.join(lines) + '\n'
    (HERE / 'optout_dry_runs.txt').write_text(text)
    print(text, end='')
    print('PASS' if not failures else f'FAIL {failures}')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
