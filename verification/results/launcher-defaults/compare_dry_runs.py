#!/usr/bin/env python3
"""Stand-command promotion (2026-09-25): compare the launcher's dry runs.

Runs `tools/manage.py launch --bottle X3 --dry-run` on the host (no Wine, nothing launched): the empty
command, the Run 84 A stand command and the short stand command. Prints the
X3M_* differences between them, and of each against the stand environment recorded before the promotion
(stand_env_before.json), and writes comparison.json beside this script.
Since the logging tiers (2026-09-26, docs/architecture/logging-tiers.md) the telemetry/debug options of both stand
commands are the two groups: the Run 84 A command is replayed with its functional options plus --debug --perf, the short
stand command is `--direct --debug --perf`, and the X3M_MOTION_FRAME_LOG=1 shell prefix is gone (the launcher drops it).
Expected: empty vs stand differ only in X3M_DEBUG / X3M_PERF and X3M_SHADOW_CASCADE_SIZES (the promoted
2048,4096,4096,4096,2048 against the stand's explicit 2048 x5, user decision 2026-09-25); the stand differs from its
recorded environment only by X3M_MUSIC_KEEP=1 and X3M_SHADOW_ALPHA_CASTERS=1 (the two new defaults), by the variables
of the options removed on 2026-09-25 (REMOVED; docs/verification/launcher-options-inventory.md, "Removed 2026-09-25"),
which the launcher no longer sends, by the logging variables the launcher no longer sends (its TIERED_VARIABLES) and by
the two groups. The Run 84 A stand command is replayed without --loading-intervals (removed 2026-09-25).
Since 2026-09-26 REMOVED also carries X3M_MESH_ADJACENCY_DUMP and X3M_VOLUMETRIC_FOG_EVERYWHERE (options and DLL reads
removed; the recorded stand sent both as 0).
"""
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
STAND = ('--direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa '
         '--debug --perf --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 '
         '--crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --screen-emission-additive 2 '
         '--screen-emission-additive-alpha 0 --emission-source-gain 2 --sun-shadow-lane '
         '--shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on '
         '--shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance '
         '--shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 '
         '--shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 '
         '--light-map-far-fade 80,220 --motion-rt-mode lazy --volumetric-fog 0.02 '
         '--volumetric-fog-cards replace --volumetric-fog-range stored --capture-start 999999 '
         '--capture-frames 8 --capture-delay 300 --cull-small-parts 4').split()
# The stand command since 2026-09-26 (docs/verification/user-runs.md, "Stand command"): --direct plus the two logging groups.
STAND_SHORT = '--direct --debug --perf'.split()
GROUPS = {'X3M_DEBUG': [None, '1'], 'X3M_PERF': [None, '1']}


def tiered_variables():
    sys.path.insert(0, str(ROOT / "tools"))  # manage.py imports its sibling media_package
    spec = importlib.util.spec_from_file_location('compare_dry_runs_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return set(module.TIERED_VARIABLES)


NEW = {'X3M_MUSIC_KEEP': '1', 'X3M_SHADOW_ALPHA_CASTERS': '1'}
# Intended functional difference: the promoted map sizes (user decision 2026-09-25) differ from the Run 84 A stand's
# explicit 2048 x5, which an explicit --shadow-cascade-sizes still selects.
SIZES = {'X3M_SHADOW_CASCADE_SIZES': ['2048,2048,2048,2048,2048', '2048,4096,4096,4096,2048']}
# The single map and its four variables were removed on 2026-09-25 (docs/architecture/directional-shadows.md, "Single map
# removed"): the recorded Run 84 A stand still carried them, the launcher no longer sends them.
# X3M_TAA_SENTINEL went the same day (user decision: --taa-sentinel removed, the resolve's policy always auto).
# The options removed on 2026-09-25 (docs/verification/launcher-options-inventory.md, "Removed 2026-09-25"): their variables with
# the recorded stand's values. The DLL resolves each absent variable to that value's behaviour (off, 40 far bins, scope all,
# the camera gate and the vote source following the thin vote).
REMOVED = {'X3M_SHADOW_REPLAY_SIZE': '1024', 'X3M_SHADOW_REPLAY_EXTENT': '250.0', 'X3M_SHADOW_REPLAY_DEPTH_HALF': '512.0', 'X3M_SHADOW_REPLAY_CAP': '512',
           'X3M_TAA_SENTINEL': 'auto',
           'X3M_CULL_SMALL_PARTS_SCOPE': 'all', 'X3M_DEPTH_COPY': '0', 'X3M_EMISSION_GAIN': '1.0', 'X3M_FINITE_POSITIONS': '0', 'X3M_FOG_FAR_BINS': '40',
           'X3M_FOG_SHADOW_PASS': '0', 'X3M_LIGHTMAP_EMISSIVE_GAIN': '1.0', 'X3M_LINEAR_DISTANCE_FADE': '0', 'X3M_LINEAR_EMISSIONS': '0',
           'X3M_LINEAR_MATERIALS': '0', 'X3M_LOADING_INTERVALS': '1', 'X3M_MATERIAL_DIRECT_GAIN': '1.0', 'X3M_MATERIAL_EMISSIVE_GAIN': '1.0',
           'X3M_MATERIAL_FILL': '0.0', 'X3M_MESH_CACHE': '0', 'X3M_MOTION_CAPTURE': '0', 'X3M_SCENE_DEPTH_CAPTURE': '0', 'X3M_SCREEN_EMISSION': '0',
           'X3M_SCREEN_EMISSION_BOUND': '0', 'X3M_SCREEN_EMISSION_GAIN': '1.0', 'X3M_SCREEN_EMISSION_TIMING': '0', 'X3M_TAA_THIN_REGION_GATE': 'camera',
           'X3M_TAA_THIN_REGION_SOURCE': 'vote', 'X3M_TAA_THIN_REGION_SOURCE_DEFAULT': '1',
           # Removed 2026-09-26 with their options and DLL reads (the in-game adjacency dump, the forced fog profile).
           'X3M_MESH_ADJACENCY_DUMP': '0', 'X3M_VOLUMETRIC_FOG_EVERYWHERE': '0'}


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
    tiered = tiered_variables()
    # The logging variables of the recorded stand: the launcher no longer sends any of them.
    recorded_tiered = {k: [v, None] for k, v in recorded['env'].items() if k in tiered}
    empty, empty_switches = dry_run([], False)
    stand, stand_switches = dry_run(STAND, False)
    short, short_switches = dry_run(STAND_SHORT, False)
    result = {
        'empty_vs_stand': diff(empty, stand),
        'stand_vs_recorded_stand': diff(recorded['env'], stand),
        'empty_vs_recorded_stand': diff(recorded['env'], empty),
        'short_stand_vs_stand': diff(stand, short),
        'switches': {'empty': empty_switches, 'stand': stand_switches, 'recorded': recorded['exe_switches']},
        'counts': {'empty': len(empty), 'stand': len(stand), 'recorded': len(recorded['env'])},
    }
    new_defaults = {k: [None if k == 'X3M_MUSIC_KEEP' else '0', v] for k, v in NEW.items()}
    removed = {k: [v, None] for k, v in REMOVED.items()}
    checks = {
        'empty_vs_stand only the two groups + the promoted map sizes': result['empty_vs_stand'] == {**GROUPS, 'X3M_SHADOW_CASCADE_SIZES': SIZES['X3M_SHADOW_CASCADE_SIZES'][::-1]},
        'stand vs recorded only the two new defaults, the removed and the logging variables, and the groups': result['stand_vs_recorded_stand'] == {**new_defaults, **removed, **recorded_tiered, **GROUPS},
        'empty vs recorded: the two new defaults + the map sizes + the removed and the logging variables': result['empty_vs_recorded_stand'] == {**new_defaults, **removed, **recorded_tiered, **SIZES},
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
