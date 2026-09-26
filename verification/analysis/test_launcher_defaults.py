"""Stand-command promotion (user decision 2026-09-25): tools/manage.py launch with no options produces the
environment of the Run 84 A stand command minus its telemetry/debug options, plus --music-keep and
--shadow-alpha-casters on (docs/verification/launcher-options-inventory.md, "Defaults promoted"). Since the logging
tiers (2026-09-26, docs/architecture/logging-tiers.md) the stand's telemetry/debug options are --debug --perf, which add
X3M_DEBUG=1 / X3M_PERF=1 and nothing else; the launcher sends no logging variable otherwise.

EXPECTED_EMPTY and STAND_TELEMETRY are the X3M_* variables of the dry-run JSON recorded by
verification/results/launcher-defaults/compare_dry_runs.py on bottle X3 (comparison.json there); the launch
here is hermetic (a temporary game directory, a fake proxy, nothing launched).
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
# The Run 84 A stand command without --loading-intervals (removed 2026-09-25) and with its telemetry/debug options as the
# two logging groups (2026-09-26).
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
# The stand command since 2026-09-26 (docs/verification/user-runs.md, "Stand command").
STAND_SHORT = '--direct --debug --perf'.split()
EXPECTED_EMPTY = {
    'X3M_BLOOM_SOURCE_CLAMP': '1.0', 'X3M_BOLT_FOOTPRINT': '3,12', 'X3M_CAMERA': 'chase', 'X3M_CAMERA_CUT_DEG': '20.0',
    'X3M_CAPTURE_DELAY': '300', 'X3M_CAPTURE_FRAMES': '8', 'X3M_CAPTURE_START': '999999', 'X3M_CHASE_COMBAT_TIGHTNESS': '0.0',
    'X3M_CHASE_DISTANCE_SCALE': '1.05', 'X3M_CHASE_FOV_COMPENSATE': '1', 'X3M_CHASE_HUD_ANCHOR': 'forward',
    'X3M_CHASE_OFFSET_Y': '0.5', 'X3M_CHASE_PITCH_DOWN_DEG': '0.5', 'X3M_CHASE_SCENE_FIX': '0',
    'X3M_CHASE_VIEW_RESTORE': '1', 'X3M_COLLIDE_BOX_CULL': '1', 'X3M_COLLIDE_MEMO': '1', 'X3M_COLLIDE_SAT_SSE2': '1',
    'X3M_CRYPT_CACHE': '1', 'X3M_CULL_SMALL_PARTS_PROJECTILES': 'on', 'X3M_CULL_SMALL_PARTS_PX': '4.0000', 'X3M_DAT_HANDLES': '1',
    'X3M_EMISSION_SOURCE_GAIN': '2.0', 'X3M_FADE_RT2_OWNER': 'on', 'X3M_FADE_RT2_OWNER_DEFAULT': '1', 'X3M_FOG_DOCKED': '1', 'X3M_FOG_DUST_MOTES': '1300,3,128',
    'X3M_FOG_HANDOVER_COLDFILL': '1', 'X3M_FOG_HANDOVER_PREFILL': '1', 'X3M_FOG_HANDOVER_STEP': '1',
    'X3M_FOG_MARCH_SCALE': '4', 'X3M_FOG_MOTES_MAX_PX': '8', 'X3M_FOV': '90',
    'X3M_GZ_BUFFER': '1', 'X3M_GZ_BUFFER_KB': '256', 'X3M_HDR': '1', 'X3M_HDR_BLOOM': '1',
    'X3M_HDR_CLAMP': '0.0', 'X3M_HDR_DECODE': 'gamma2.2', 'X3M_HDR_DITHER': '1', 'X3M_HDR_EV': '0.0',
    'X3M_HDR_EV_DEADBAND': '0.25', 'X3M_HDR_EV_MANUAL': '', 'X3M_HDR_EV_MAX': '1.3', 'X3M_HDR_EV_MIN': '-3.0',
    'X3M_HDR_EXPOSURE': 'auto', 'X3M_HDR_KEY_PULL': '0.25', 'X3M_HDR_LOOK': 'none', 'X3M_HDR_METER_BG': '0.001953125',
    'X3M_HDR_METER_EDGE_WEIGHT': '0.35', 'X3M_HDR_TONEMAP': 'agx', 'X3M_HDR_WHITE_TARGET': '0.9',
    'X3M_HULL_EMISSION_GAIN': '2.0', 'X3M_HULL_EMISSIVE_WIDENING': '4,4', 'X3M_HULL_LIGHTMAP_GAIN': '4.0', 'X3M_LIGHT_MAP_FAR_FADE': '80,220,1',
    'X3M_LOD_OCCLUSION': 'all', 'X3M_LOD_OCCLUSION_DEFAULT': '1',
    'X3M_MEDIA_CUE_CACHE': '1', 'X3M_MEDIA_CUE_RETRY_S': '30', 'X3M_MESH_ADJACENCY': 'fast',
    'X3M_MOTION_CUT_MEDIAN_PX': '1e30',
    'X3M_MOTION_CUT_MISSING': '1', 'X3M_MOTION_JITTER': '1', 'X3M_MOTION_OUTPUT': '1', 'X3M_MOTION_RT_MODE': 'lazy',
    'X3M_MUSIC_KEEP': '1', 'X3M_OBJECT_LIFETIME': '1', 'X3M_OBJECT_TRACE': '1', 'X3M_ORIGINAL_FILL': '0.01',
    'X3M_ORIGINAL_FILL_DEFAULT': '1', 'X3M_OWNERSHIP': '1', 'X3M_PAUSE_KEY_ONLY': '1',
    'X3M_RESOURCE_READ': 'fast', 'X3M_SCENE_HOOK': '1',
    'X3M_SCREEN_EMISSION_ADDITIVE': '2.0', 'X3M_SCREEN_EMISSION_ADDITIVE_ALPHA': '0.0',
    'X3M_SHADOW_ALPHA_CASTERS': '1', 'X3M_SHADOW_CASCADES': '250.0,1500.0,7500.0,37500.0,150000.0',
    'X3M_SHADOW_CASCADE_ADAPTIVE_C0': '1.5', 'X3M_SHADOW_CASCADE_DROP_ORDER': 'importance',
    'X3M_SHADOW_CASCADE_MIN_FOOTPRINT': '8.0', 'X3M_SHADOW_CASCADE_RECORDS': '1024,1024,2048,4096,4096',
    'X3M_SHADOW_CASCADE_SIZES': '2048,4096,4096,4096,2048', 'X3M_SHADOW_CASTER_RETENTION': '1',
    'X3M_SHADOW_REPLAY_CANDIDATES': '1', 'X3M_SHADOW_REPLAY_DEPTH': '1',
    'X3M_SHADOW_SUN_POLL': '1',
    'X3M_SUN_FLARE_FIX': 'on', 'X3M_SUN_OCCLUSION': '1', 'X3M_SUN_OCCLUSION_CORE_F': '1', 'X3M_SUN_OCCLUSION_DEFAULT': '1',
    'X3M_SUN_SHADOW_APPLY': '1', 'X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS': '20.97152', 'X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS': '0.2',
    'X3M_SUN_SHADOW_BIAS_UNITS': '0.53571875', 'X3M_SUN_SHADOW_LANE': '1', 'X3M_TAA': '1', 'X3M_TAA_BOX_RESOLUTION': 'half',
    'X3M_TAA_BOX_RESOLUTION_DEFAULT': '1', 'X3M_TAA_FAR_CLIP': '7x7', 'X3M_TAA_FAR_CLIP_DEFAULT': '1',
    'X3M_TAA_FAR_GATE': 'camera', 'X3M_TAA_FAR_GATE_DEFAULT': '1', 'X3M_TAA_FAR_STABILISER': '0.985,0,60,68,0.03,0.25',
    'X3M_TAA_MIP_BIAS': '-0.5', 'X3M_TAA_MOTION_WEIGHT': '0.7,2,8', 'X3M_TAA_SHARPEN': '0.75',
    'X3M_TAA_SKY_HISTORY': 'strict', 'X3M_TAA_SKY_HISTORY_EXIT_PX': '0.25', 'X3M_TAA_THIN_REGION': '0.97,1',
    'X3M_TAA_THIN_REGION_EMISSIVE': '1', 'X3M_TAA_THIN_VOTE': 'on', 'X3M_TAA_THIN_VOTE_DEFAULT': '1',
    'X3M_TAA_UNMATCHED_STATIC': 'node', 'X3M_TERRAN_STATION_LOD': 'size',
    'X3M_VOICE_DMO_FALLBACK': '1', 'X3M_VOLUMETRIC_FOG': '1', 'X3M_VOLUMETRIC_FOG_CARDS': 'replace',
    'X3M_VOLUMETRIC_FOG_RANGE': 'stored', 'X3M_VOLUMETRIC_FOG_STRENGTH': '0.02',
    'X3M_WINDOW_MONITOR_RECT': '1', 'X3M_WINDOW_MONITOR_RECT_DEFAULT': '1',
}
STAND_TELEMETRY = {'X3M_DEBUG': '1', 'X3M_PERF': '1'}
# The one functional difference of the old stand command: it passes the Run 84 A map sizes explicitly, while the
# promoted default is 2048,4096,4096,4096,2048 (user decision 2026-09-25). An explicit value wins.
STAND_EXPLICIT = {'X3M_SHADOW_CASCADE_SIZES': '2048,2048,2048,2048,2048'}
# Set from the installation, not from the options (the voice decoder discovery): not compared here.
INSTALLATION = {'X3M_VOICE_DMO_FALLBACK'}
# Opt-outs of the promoted defaults: each alone is accepted (no refusal) and turns its variable off
# (None: the variable is not sent; for --no-direct, the X3 switches are dropped).
OPT_OUTS = (
    (('--no-direct',), None, None),
    (('--camera', 'vanilla'), 'X3M_CHASE_VIEW_RESTORE', '0'),
    (('--no-chase-view-restore',), 'X3M_CHASE_VIEW_RESTORE', '0'),
    (('--no-ownership',), 'X3M_TAA', '0'),
    (('--no-object-trace',), 'X3M_OBJECT_LIFETIME', '0'),
    (('--no-object-lifetime',), 'X3M_OBJECT_TRACE', '0'),
    (('--no-motion-output',), 'X3M_MOTION_OUTPUT', '0'),
    (('--no-taa',), 'X3M_TAA', '0'),
    (('--no-hdr',), 'X3M_HDR', '0'),
    (('--no-hdr-tonemap',), 'X3M_HDR_TONEMAP', 'identity'),
    (('--no-hdr-bloom',), 'X3M_HDR_BLOOM', '0'),
    (('--no-bloom-source-clamp',), 'X3M_BLOOM_SOURCE_CLAMP', None),
    (('--no-crypt-cache',), 'X3M_CRYPT_CACHE', '0'),
    (('--no-gz-buffer',), 'X3M_GZ_BUFFER', '0'),
    (('--resource-read', 'native'), 'X3M_RESOURCE_READ', 'native'),
    (('--no-dat-handles',), 'X3M_DAT_HANDLES', '0'),
    (('--mesh-adjacency', 'native'), 'X3M_MESH_ADJACENCY', 'native'),
    (('--no-screen-emission-additive',), 'X3M_SCREEN_EMISSION_ADDITIVE', '0'),
    (('--no-screen-emission-additive-alpha',), 'X3M_SCREEN_EMISSION_ADDITIVE_ALPHA', None),
    (('--emission-source-gain', '1'), 'X3M_EMISSION_SOURCE_GAIN', '1.0'),
    (('--no-sun-shadow-lane',), 'X3M_SUN_SHADOW_APPLY', '0'),
    (('--no-shadow-replay-depth',), 'X3M_SHADOW_CASCADES', '0'),
    (('--no-sun-shadow-apply',), 'X3M_SUN_SHADOW_APPLY', '0'),
    (('--shadow-alpha-casters', 'off'), 'X3M_SHADOW_ALPHA_CASTERS', '0'),
    (('--no-shadow-cascades',), 'X3M_VOLUMETRIC_FOG', '0'),
    (('--shadow-cascade-drop-order', 'submission'), 'X3M_SHADOW_CASCADE_DROP_ORDER', 'submission'),
    (('--no-shadow-cascade-adaptive-c0',), 'X3M_SHADOW_CASCADE_ADAPTIVE_C0', None),
    (('--no-shadow-caster-retention',), 'X3M_SHADOW_CASTER_RETENTION', '0'),
    (('--no-volumetric-fog',), 'X3M_VOLUMETRIC_FOG', '0'),
    (('--volumetric-fog-cards', 'keep'), 'X3M_VOLUMETRIC_FOG_CARDS', 'keep'),
    (('--volumetric-fog-range', 'legacy'), 'X3M_VOLUMETRIC_FOG_RANGE', 'legacy'),
    (('--cull-small-parts', '0'), 'X3M_CULL_SMALL_PARTS_PX', None),
    (('--no-music-keep',), 'X3M_MUSIC_KEEP', None),
    (('--capture-start', '120'), 'X3M_CAPTURE_START', '120'),
    (('--capture-delay', '0'), 'X3M_CAPTURE_DELAY', None),
)


def load_manage():
    spec = importlib.util.spec_from_file_location('launcher_defaults_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LauncherDefaults(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = load_manage()
        cls.directory = tempfile.TemporaryDirectory()
        game = Path(cls.directory.name) / 'game'
        game.mkdir()
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'proxy')
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        cls.game = game
        cls.wine = Path(cls.directory.name) / 'wine'
        cls.wine.touch()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def launch(self, *args, vanilla=False, inherited=None):
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(self.game), *args]
        environ = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
        environ.update(inherited or {})
        output, error = io.StringIO(), io.StringIO()
        module = self.module
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', self.wine), \
                mock.patch.object(module, 'VOICE_DECODER_REPO', None), mock.patch.dict(module.os.environ, environ, clear=True), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, None, error.getvalue()
        return 0, json.loads(output.getvalue()), error.getvalue()

    def env(self, *args, **kw):
        code, data, error = self.launch(*args, **kw)
        self.assertEqual(code, 0, error)
        return {k: v for k, v in data['env'].items() if k.startswith('X3M_') and k not in INSTALLATION}

    def test_empty_command_is_the_stand_without_telemetry(self):
        expected = {k: v for k, v in EXPECTED_EMPTY.items() if k not in INSTALLATION}
        self.assertEqual(self.env(), expected)
        _, data, _ = self.launch()
        self.assertEqual(data['command'][-3:], ['-noabout', '-skipintro', '-runinbg'])

    def test_stand_commands_add_only_the_two_groups(self):
        empty = {k: v for k, v in EXPECTED_EMPTY.items() if k not in INSTALLATION}
        stand = self.env(*STAND)
        self.assertEqual(stand, {**empty, **STAND_TELEMETRY, **STAND_EXPLICIT})
        self.assertEqual({k for k in stand if stand[k] != empty.get(k)}, set(STAND_TELEMETRY) | set(STAND_EXPLICIT))
        self.assertEqual(self.env(*STAND_SHORT), {**empty, **STAND_TELEMETRY})
        # The former X3M_MOTION_FRAME_LOG=1 shell prefix is dropped: the DLL's debug group gives the cadence.
        self.assertEqual(self.env(*STAND_SHORT, inherited={'X3M_MOTION_FRAME_LOG': '1'}), {**empty, **STAND_TELEMETRY})

    def test_vanilla_sends_nothing_modded(self):
        env = self.env(vanilla=True)
        for name in ('X3M_OWNERSHIP', 'X3M_MOTION_OUTPUT', 'X3M_TAA', 'X3M_HDR', 'X3M_SUN_SHADOW_LANE', 'X3M_SHADOW_REPLAY_DEPTH',
                     'X3M_VOLUMETRIC_FOG', 'X3M_CRYPT_CACHE', 'X3M_GZ_BUFFER', 'X3M_DAT_HANDLES', 'X3M_SHADOW_ALPHA_CASTERS'):
            self.assertEqual(env[name], '0', name)
        self.assertEqual((env['X3M_CAMERA'], env['X3M_RESOURCE_READ'], env['X3M_MESH_ADJACENCY']), ('vanilla', 'native', 'native'))
        for name in ('X3M_MUSIC_KEEP', 'X3M_SHADOW_CASCADE_DROP_ORDER', 'X3M_BLOOM_SOURCE_CLAMP', 'X3M_CULL_SMALL_PARTS_PX'):
            self.assertNotIn(name, env)
        _, data, _ = self.launch(vanilla=True)
        self.assertNotIn('-noabout', data['command'])

    def test_every_promoted_default_has_an_accepted_opt_out(self):
        for args, name, value in OPT_OUTS:
            with self.subTest(args=args):
                code, data, error = self.launch(*args)
                self.assertEqual(code, 0, error)
                if name is None:
                    self.assertNotIn('-noabout', data['command'])
                elif value is None:
                    self.assertNotIn(name, data['env'])
                else:
                    self.assertEqual(data['env'][name], value)

    def test_explicit_dependants_are_still_refused(self):
        for args, needle in ((('--no-taa', '--sun-shadow-lane'), '--sun-shadow-lane requires'),
                             (('--no-shadow-cascades', '--shadow-caster-retention'), '--shadow-caster-retention requires --shadow-cascades'),
                             (('--no-volumetric-fog', '--volumetric-fog-range', 'stored'), 'require --volumetric-fog'),
                             (('--camera', 'vanilla', '--chase-view-restore'), '--camera chase'),
                             # verify left the launcher on 2026-09-26 (the fixture sets X3M_MESH_ADJACENCY=verify itself)
                             (('--mesh-adjacency', 'verify'), "invalid choice: 'verify'")):
            with self.subTest(args=args):
                code, _, error = self.launch(*args)
                self.assertEqual(code, 2)
                self.assertIn(needle, error)

    def test_taa_k_and_sentinel_options_removed(self):
        # --taa-k / X3M_TAA_K and --taa-sentinel / X3M_TAA_SENTINEL were removed on 2026-09-25
        # (user decisions; docs/architecture/temporal-integration.md, "Derivation of k" and
        # "Policy selection per frame"): the options are unknown and an inherited value of the variables is dropped,
        # never forwarded (k derived only, policy always auto). The --taa-sentinel refusal stub went with the
        # --taa-sentinel-stabiliser stub it guarded against abbreviation (both plain unknown options since 2026-09-25).
        for value in ('1', '0'):
            code, _, error = self.launch('--taa-k', value)
            self.assertEqual(code, 2, value); self.assertIn('unrecognized arguments', error)
        for args in (('--taa-sentinel', '1'), ('--taa-sentinel', 'auto'), ('--taa-sentinel',), ('--taa-sentinel-stabiliser', '1')):
            code, _, error = self.launch(*args)
            self.assertEqual(code, 2, args); self.assertIn('unrecognized arguments', error)
        for vanilla in (False, True):
            code, data, error = self.launch(vanilla=vanilla, inherited={'X3M_TAA_K': '0', 'X3M_TAA_SENTINEL': '1'})
            self.assertEqual(code, 0, error)
            self.assertNotIn('X3M_TAA_K', data['env']); self.assertNotIn('X3M_TAA_SENTINEL', data['env'])

    def test_removed_options_are_unknown_and_their_variables_dropped(self):
        # docs/verification/launcher-options-inventory.md, "Removed 2026-09-25": every removed option is a plain unknown
        # argument (exit 2), and none of their variables survives from an inherited shell environment.
        for args in (('--taa-current-filter',), ('--taa-line-filter',), ('--taa-thin-clip',), ('--taa-adaptive-weight',), ('--taa-region-hold',),
                     ('--volumetric-fog-look', 'L1'), ('--lod-scale', '2'), ('--linear-materials',), ('--material-fill', '0.05'),
                     ('--material-direct-gain', '1'), ('--material-emissive-gain', '1'), ('--lightmap-emissive-gain', '1'),
                     ('--linear-distance-fade',), ('--no-linear-distance-fade',), ('--linear-emissions',), ('--emission-gain', '1'),
                     ('--screen-emission',), ('--screen-emission-gain', '2'), ('--screen-emission-timing',), ('--fade-witness',), ('--shimmer-trace',),
                     ('--hull-emitters',), ('--hull-emission-gain', '2'), ('--fog-shadow-pass', 'on'), ('--fog-far-bins', '24'),
                     ('--taa-history-taps', '16'), ('--taa-thin-region-gate', 'screen'), ('--taa-thin-region-source', 'both'),
                     ('--mesh-cache',), ('--depth-copy',), ('--scene-depth-capture',), ('--motion-capture',), ('--finite-positions',),
                     ('--loading-intervals',), ('--audio-sites',), ('--profile-raw',), ('--cull-small-parts-scope', 'bodies')):
            with self.subTest(args=args):
                code, _, error = self.launch(*args)
                self.assertEqual(code, 2, args)
                self.assertTrue('unrecognized arguments' in error or 'ambiguous option' in error, error[-200:])
        inherited = {name: '1' for name in self.module.REMOVED_VARIABLES}
        for vanilla in (False, True):
            code, data, error = self.launch(vanilla=vanilla, inherited=inherited)
            self.assertEqual(code, 0, error)
            self.assertFalse(set(self.module.REMOVED_VARIABLES) & set(data['env']), vanilla)


if __name__ == '__main__':
    unittest.main()
