#!/usr/bin/env python3
"""Stand-command promotion (2026-09-25): compare the launcher's dry runs.

Runs `tools/manage.py launch --bottle X3 --dry-run` on the host (no Wine, nothing launched): the empty
command, the Run 84 A stand command and the short stand command of 2026-09-25 (the last two with
X3M_MOTION_FRAME_LOG=1 in the shell). Prints the
X3M_* differences between them, and of each against the stand environment recorded before the promotion
(stand_env_before.json), and writes comparison.json beside this script.
Expected: empty vs stand differ only in the telemetry/debug variables and X3M_SHADOW_CASCADE_SIZES (the promoted
2048,4096,4096,4096,2048 against the stand's explicit 2048 x5, user decision 2026-09-25); the stand differs from its
recorded environment only by X3M_MUSIC_KEEP=1 and X3M_SHADOW_ALPHA_CASTERS=1 (the two new defaults).
"""
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
STAND = ('--direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa '
         '--telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 '
         '--crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --screen-emission-additive 2 '
         '--screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane '
         '--shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay '
         '--shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance '
         '--shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 '
         '--shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 '
         '--light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --volumetric-fog 0.02 '
         '--volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 '
         '--capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases '
         '--object-bounds-log --cull-census').split()
# The stand command since 2026-09-25 (docs/verification/user-runs.md, "Stand command"): --direct plus the telemetry flags.
STAND_SHORT = ('--direct --telemetry --camera-log 1 --loading-intervals --shadow-retention-census --fps-overlay '
               '--frame-end-stride 1 --volumetric-fog-timing '
               '--frame-timing --frame-phases --object-bounds-log --cull-census').split()
TELEMETRY = {'X3M_TELEMETRY', 'X3M_CAMERA_LOG', 'X3M_LOADING_INTERVALS', 'X3M_SHADOW_RETENTION_CENSUS', 'X3M_MOTION_FRAME_LOG',
             'X3M_FPS_OVERLAY', 'X3M_FRAME_END_STRIDE', 'X3M_VOLUMETRIC_FOG_TIMING', 'X3M_FRAME_TIMING', 'X3M_FRAME_PHASES', 'X3M_OBJECT_BOUNDS_LOG', 'X3M_CULL_CENSUS'}
NEW = {'X3M_MUSIC_KEEP': '1', 'X3M_SHADOW_ALPHA_CASTERS': '1'}
# Intended functional difference: the promoted map sizes (user decision 2026-09-25) differ from the Run 84 A stand's
# explicit 2048 x5, which an explicit --shadow-cascade-sizes still selects.
SIZES = {'X3M_SHADOW_CASCADE_SIZES': ['2048,2048,2048,2048,2048', '2048,4096,4096,4096,2048']}
# The single map and its four variables were removed on 2026-09-25 (docs/architecture/directional-shadows.md, "Single map
# removed"): the recorded Run 84 A stand still carried them, the launcher no longer sends them.
REMOVED = {'X3M_SHADOW_REPLAY_SIZE': '1024', 'X3M_SHADOW_REPLAY_EXTENT': '250.0', 'X3M_SHADOW_REPLAY_DEPTH_HALF': '512.0', 'X3M_SHADOW_REPLAY_CAP': '512'}


def dry_run(arguments, frame_log):
    environ = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
    if frame_log:
        environ['X3M_MOTION_FRAME_LOG'] = '1'
    result = subprocess.run([sys.executable, str(ROOT / 'tools/manage.py'), 'launch', '--bottle', 'X3', '--dry-run', *arguments],
                            capture_output=True, text=True, env=environ, cwd=ROOT)
    if result.returncode:
        raise SystemExit(f'dry run {arguments[:3]}... failed: {result.stderr[-400:]}')
    data = json.loads(result.stdout)
    exe = next(i for i, c in enumerate(data['command']) if c.endswith('X3AP.exe'))
    return {k: v for k, v in data['env'].items() if k.startswith('X3M_')}, data['command'][exe + 1:]


def diff(a, b):
    return {k: [a.get(k), b.get(k)] for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)}


def main():
    recorded = json.loads((HERE / 'stand_env_before.json').read_text())
    empty, empty_switches = dry_run([], False)
    stand, stand_switches = dry_run(STAND, True)
    short, short_switches = dry_run(STAND_SHORT, True)
    result = {
        'empty_vs_stand': diff(empty, stand),
        'stand_vs_recorded_stand': diff(recorded['env'], stand),
        'empty_vs_recorded_stand': diff(recorded['env'], empty),
        'short_stand_vs_stand': diff(stand, short),
        'switches': {'empty': empty_switches, 'stand': stand_switches, 'recorded': recorded['exe_switches']},
        'counts': {'empty': len(empty), 'stand': len(stand), 'recorded': len(recorded['env'])},
    }
    checks = {
        'empty_vs_stand only telemetry/debug + the promoted map sizes': set(result['empty_vs_stand']) <= TELEMETRY | set(SIZES)
            and result['empty_vs_stand'].get('X3M_SHADOW_CASCADE_SIZES') == SIZES['X3M_SHADOW_CASCADE_SIZES'][::-1],
        'stand vs recorded only the two new defaults and the removed single-map variables': result['stand_vs_recorded_stand'] == {**{k: [None if k == 'X3M_MUSIC_KEEP' else '0', v] for k, v in NEW.items()}, **{k: [v, None] for k, v in REMOVED.items()}},
        'empty vs recorded: telemetry/debug + the two new defaults + the map sizes + the removed single-map variables': set(result['empty_vs_recorded_stand']) <= TELEMETRY | set(NEW) | set(SIZES) | set(REMOVED)
            and all(result['empty_vs_recorded_stand'][k] == [v, None] for k, v in REMOVED.items())
            and all(result['empty_vs_recorded_stand'][k][1] == v for k, v in NEW.items())
            and result['empty_vs_recorded_stand'].get('X3M_SHADOW_CASCADE_SIZES') == SIZES['X3M_SHADOW_CASCADE_SIZES'],
        'same X3AP switches': empty_switches == stand_switches == short_switches == recorded['exe_switches'],
        'short stand command == old stand command but the map sizes': result['short_stand_vs_stand'] == SIZES,
    }
    result['checks'] = checks
    (HERE / 'comparison.json').write_text(json.dumps(result, indent=1) + '\n')
    print('counts', result['counts'])
    for name in ('empty_vs_stand', 'stand_vs_recorded_stand', 'empty_vs_recorded_stand', 'short_stand_vs_stand'):
        print(f'{name}: {len(result[name])} variables')
        for key, (a, b) in result[name].items():
            print(f'  {key}: {a!r} -> {b!r}')
    for name, ok in checks.items():
        print('PASS' if ok else 'FAIL', name)
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
