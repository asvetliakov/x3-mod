"""Word-identity gate for src/fog/fog_density_generator.cpp against the validated
Python screen (tools/analysis/fog_density_runtime_screen.py): field, eight-point
prefilter, FP16 law, address law and tile packing. Builds the native host tool
with clang++ (plus an x86_64 SSE2 build under Rosetta when available); no Wine.
"""
import importlib.util, json, os, shutil, struct, subprocess, sys, tempfile, unittest
from itertools import product
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [ROOT / 'verification/probe/fog_density_generator_host.cpp', ROOT / 'src/fog/fog_density_generator.cpp']
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off']
LEVELS = {'fine': 512., 'far': 4096.}
REPORT = Path('/tmp/x3-fog-density-runtime-screen/report.json')  # local golden values, optional
TILE_ORIGINS = {'fine': (95576., 97323., 82698.), 'far': (-10000., 20000., -30000.)}  # bench cameras


def load_screen():
    s = importlib.util.spec_from_file_location('density_runtime', ROOT / 'tools/analysis/fog_density_runtime_screen.py')
    m = importlib.util.module_from_spec(s); s.loader.exec_module(m); return m


def fnv1a(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h


def fmt(rows):
    return '\n'.join(' '.join(repr(float(v)) if isinstance(v, (float, np.floating)) else str(int(v)) for v in row) for row in rows) + '\n'


class Tool:
    def __init__(self, executable, prefix=()):
        self.executable, self.prefix = executable, list(prefix)

    def run(self, args, stdin='', binary=False):
        r = subprocess.run(self.prefix + [str(self.executable)] + [str(a) for a in args], input=stdin if binary else stdin.encode(),
                           capture_output=True, check=True)
        return r.stdout if binary else r.stdout.decode()

    def words(self, delta, keys, offset=None):
        out = self.run(['words', repr(delta)] + ([repr(float(v)) for v in offset] if offset else []), fmt(keys)).split()
        return np.array([int(w, 16) for w in out[0::2]], np.uint16), np.array([int(w, 16) for w in out[1::2]], np.uint32)


class FogDensityGenerator(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m = load_screen()
        compiler = shutil.which('clang++')
        if compiler is None: raise RuntimeError('clang++ is required: the word-identity gate cannot be skipped')
        cls.temporary = tempfile.TemporaryDirectory(prefix='x3-fog-generator-')
        native = Path(cls.temporary.name) / 'generator-host'
        subprocess.run([compiler] + FLAGS + [str(s) for s in SOURCES] + ['-o', str(native)], check=True, capture_output=True)
        cls.tools = {'native': Tool(native)}
        x86 = Path(cls.temporary.name) / 'generator-host-x86_64'
        probe = subprocess.run([compiler, '-arch', 'x86_64', '-msse2', '-mfpmath=sse'] + FLAGS + [str(s) for s in SOURCES] + ['-o', str(x86)], capture_output=True)
        if probe.returncode == 0 and subprocess.run(['arch', '-x86_64', str(x86), 'constants'], capture_output=True).returncode == 0:
            cls.tools['x86_64-sse2'] = Tool(x86, ('arch', '-x86_64'))
        else:
            print('fog_density_generator: optional x86_64 SSE2 build unavailable (no Rosetta); native build only', file=sys.stderr)
        cls.rng = np.random.default_rng(20260921)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def each_tool(self):
        return self.tools.values()

    def test_rotation_constants_bitwise(self):
        R = self.m.screen.R; R2 = np.array([[sum(R[i, k] * R[k, j] for k in range(3)) for j in range(3)] for i in range(3)])
        expect = [' '.join(struct.pack('>d', v).hex() for v in row) for row in (*R, *R2)]
        for tool in self.each_tool(): self.assertEqual(tool.run(['constants']).splitlines(), expect)

    def test_field_words_identical(self):
        pts = np.concatenate([self.rng.uniform(-3e5, 3e5, (1000, 3)), [[0, 0, 0], [-300000, 200000, -100000], [65536, -65536, 32768], [1, -2, 3]]])
        ref = self.m.field(pts).view(np.uint32)
        for tool in self.each_tool():
            got = np.array([int(w, 16) for w in tool.run(['field'], fmt(pts)).split()], np.uint32)
            np.testing.assert_array_equal(got, ref)

    def test_node_words_identical_on_4096_signed_nodes_and_shift_witnesses(self):
        store = self.m.LazyStore(); total = 0
        witness = {name: set() for name in LEVELS}
        for pose in self.m.screen.pose_rows():
            Q = np.asarray(pose['origin']) + 25000 * np.asarray(pose['forward'])
            for name, delta in LEVELS.items():
                base = np.floor(Q / delta).astype(np.int64)
                for corner in product((0, 1), repeat=3): witness[name].add(tuple(base + corner))
        for name, delta in LEVELS.items():
            span = 1200 if name == 'fine' else 150
            keys = np.concatenate([self.rng.integers(-span, span, (2048, 3)), np.array(sorted(witness[name]), np.int64)])
            self.assertGreater((keys < 0).any(axis=1).sum(), 900)
            ref = store.get(name, keys).view(np.uint16)
            means = np.array([np.mean(self.m.field((k + self.m.OFFSETS) * delta)) for k in keys[:64]], np.float32).view(np.uint32)
            for tool in self.each_tool():
                words, floats = tool.words(delta, keys)
                differing = int((words != ref).sum())
                self.assertEqual(differing, 0, f'{name}: {differing}/{len(keys)} FP16 words differ, max |ulp| '
                                 f'{int(np.abs(words.astype(np.int32) - ref.astype(np.int32)).max())}')
                np.testing.assert_array_equal(floats[:64], means)
            total += len(keys)
        self.assertGreaterEqual(total, 4096)

    def test_shift_witnesses_reconstruct_report_values(self):
        """±5500 camera shifts move the window, not the world nodes: addresses stay contained and the trilinear
        reconstruction of the C++ words equals the screen's Q witnesses (from the local report when present)."""
        golden = {}
        if REPORT.is_file():
            for row in json.loads(REPORT.read_text())['Q_LOD_witnesses']: golden[(row['pose'], row['shift'])] = row
        elif os.environ.get('X3M_FOG_GOLDEN_REQUIRED') == '1':
            self.fail(f'golden report {REPORT} is absent and X3M_FOG_GOLDEN_REQUIRED=1')
        # Absent golden (the default, and the former X3M_FOG_GOLDEN_OPTIONAL=1): the address checks run alone and
        # the test then reports itself skipped for the 22 value comparisons.
        tool = self.tools['native']; checked = 0
        for pose in self.m.screen.pose_rows():
            forward = np.asarray(pose['forward']); Q = np.asarray(pose['origin']) + 25000 * forward
            for shift in (-5500., -5000., -4500., 0., 4500., 5000., 5500.):
                cam = np.asarray(pose['origin']) + shift * forward
                for level, (name, delta) in enumerate(LEVELS.items()):
                    lines = tool.run(['address', level, *[repr(float(v)) for v in cam]], fmt([Q])).splitlines()
                    origin = np.array(lines[0].split()[1:], np.int64); f = lines[1].split()
                    np.testing.assert_array_equal(origin, self.m.window_origin(cam, delta))
                    key = np.array(f[:3], np.int64); local = np.array(f[3:6], np.int64); frac = np.array([struct.unpack('>d', bytes.fromhex(h))[0] for h in f[9:12]])
                    np.testing.assert_array_equal(key, np.floor(Q / delta)); np.testing.assert_array_equal(local, key - origin); self.assertEqual(f[12], '1')
                    np.testing.assert_array_equal(frac, Q / delta - np.floor(Q / delta))
                    corners = np.array([key + c for c in product((0, 1), repeat=3)]); words, _ = tool.words(delta, corners)
                    value = 0.
                    for (dx, dy, dz), w in zip(product((0, 1), repeat=3), words.view(np.float16)):
                        value += np.prod(np.where([dx, dy, dz], frac, 1 - frac)) * float(w)
                    dist = float(np.linalg.norm(Q - cam)); expect = golden.get((pose['name'], shift), {}).get(name)
                    if expect is not None and ((name == 'fine' and dist < 30000) or (name == 'far' and dist > 20000)):
                        self.assertEqual(np.float32(value), np.float32(expect)); checked += 1
        if golden: self.assertEqual(checked, 22)
        else: self.skipTest(f'address checks passed; the 22 golden value checks need the local report {REPORT} '
                            '(absent; set X3M_FOG_GOLDEN_REQUIRED=1 to make its absence a failure)')

    def test_nonzero_world_offset(self):
        for name, delta in LEVELS.items():
            keys = self.rng.integers(-300, 300, (256, 3))
            for tool in self.each_tool():
                base, _ = tool.words(delta, keys + np.array([3, -5, 7]))
                shifted, _ = tool.words(delta, keys, offset=(3 * delta, -5 * delta, 7 * delta))
                np.testing.assert_array_equal(shifted, base)
                offset = (1234.5, -777.25, 31.75)
                ref = np.mean(self.m.field(((keys[:, None, :] + self.m.OFFSETS[None]) * delta + np.array(offset)).reshape(-1, 3)).reshape(-1, 8), axis=1).astype(np.float16).view(np.uint16)
                got, _ = tool.words(delta, keys, offset=offset); np.testing.assert_array_equal(got, ref)

    def test_half_conversion_is_numpy_rne(self):
        halves = np.arange(65536, dtype=np.uint16).view(np.float16)
        finite = halves[np.isfinite(halves)].astype(np.float32)
        values = np.concatenate([finite, self.rng.uniform(-1, 1, 20000).astype(np.float32), self.rng.uniform(-70000, 70000, 20000).astype(np.float32),
                                 (self.rng.random(20000) * 1e-4).astype(np.float32), np.nextafter(finite[:2048], np.float32(np.inf)), np.nextafter(finite[:2048], np.float32(-np.inf)),
                                 np.array([65504, 65520, 65519.99, 65536, 1e-8, 5.9604645e-08, 2.9802322e-08, 2.98023e-08, 8.940697e-08, 0, -0., 1e30, np.inf, -np.inf], np.float32)])
        with np.errstate(over='ignore'): ref = values.astype(np.float16).view(np.uint16)
        for tool in self.each_tool():
            got = np.array([int(w, 16) for w in tool.run(['half'], '\n'.join(f'{b:08x}' for b in values.view(np.uint32)) + '\n').split()], np.uint16)
            np.testing.assert_array_equal(got, ref); self.assertGreater(len(values), 90000)

    def test_half_to_float_round_trips_every_half_word(self):
        halves = np.arange(65536, dtype=np.uint16); ref = halves.view(np.float16).astype(np.float32).view(np.uint32)
        for tool in self.each_tool():
            got = np.array([int(w, 16) for w in tool.run(['unhalf'], '\n'.join(f'{h:04x}' for h in halves) + '\n').split()], np.uint32)
            finite = np.isfinite(halves.view(np.float16))
            np.testing.assert_array_equal(got[finite], ref[finite])  # every finite half, subnormals included, exact
            self.assertTrue(np.array_equal(got[~finite] & 0xff800000, ref[~finite] & 0xff800000))  # inf/NaN class and sign
            self.assertEqual(got[1], 0x33800000)

    def test_lod_weights_match_screen_law(self):
        s = np.array([0., 20000., 22500., 25000., 30000., 100000., 150000., 175000., 200000., 250000.])
        lam = 1 - self.m.fog.smoothstep(20000., 30000., s); taper = 1 - self.m.fog.smoothstep(150000., 200000., s)
        for tool in self.each_tool():
            rows = [l.split() for l in tool.run(['lod'], '\n'.join(repr(float(v)) for v in s) + '\n').splitlines()]
            got_lam = np.array([struct.unpack('>d', bytes.fromhex(r[0]))[0] for r in rows]); got_taper = np.array([struct.unpack('>d', bytes.fromhex(r[1]))[0] for r in rows])
            np.testing.assert_array_equal(got_lam, lam); np.testing.assert_array_equal(got_taper, taper)
            self.assertEqual([r[2] for r in rows], ['1' if v > 0 else '0' for v in lam]); self.assertEqual([r[3] for r in rows], ['1' if v < 1 else '0' for v in lam])

    def test_atlas_layout_offsets(self):
        tool = self.tools['native']
        probes = [(0, 0, 0), (127, 127, 3), (0, 0, 4), (127, 127, 127), (5, 9, 66), (128 - 1, 0, 31 * 4)]
        lines = tool.run(['layout'], fmt(probes)).splitlines()
        self.assertEqual([int(v) for v in lines[0].split()], [1032, 516, 8, 8256, 4260096, 129, 32])
        for (sx, sy, sz), line in zip(probes, lines[1:]):
            offset, tx, ty, lane = (int(v) for v in line.split()); group = sz // 4
            self.assertEqual((tx, ty, lane), ((group % 8) * 129 + sx, (group // 8) * 129 + sy, sz % 4)); self.assertEqual(offset, ty * 8256 + tx * 8)
        # The last tile's border texel (row 515, column 1031) ends exactly at kAtlasBytes.
        self.assertEqual((3 * 129 + 128) * 8256 + (7 * 129 + 128) * 8 + 8, 4260096)

    def test_duplicate_tile_border_in_place(self):
        tool = self.tools['native']
        for group in (0, 9, 31):
            atlas = np.frombuffer(tool.run(['border', group], b'', binary=True), np.uint16).reshape(516, 1032, 4)
            x = np.arange(1032)[None, :, None]; y = np.arange(516)[:, None, None]; pattern = ((x * 7 + y * 13 + np.arange(4)[None, None, :]) & 0xffff).astype(np.uint16)
            tx, ty = (group % 8) * 129, (group // 8) * 129; tile = atlas[ty:ty + 129, tx:tx + 129]
            np.testing.assert_array_equal(tile[:128, :128], pattern[ty:ty + 128, tx:tx + 128])
            np.testing.assert_array_equal(tile[:, 128], tile[:, 0]); np.testing.assert_array_equal(tile[128, :], tile[0, :])
            untouched = np.ones((516, 1032), bool); untouched[ty:ty + 129, tx:tx + 129] = False
            np.testing.assert_array_equal(atlas[untouched], pattern[untouched])

    def test_address_law_storage_texel_and_cache_shift(self):
        tool = self.tools['native']
        for level, (name, delta) in enumerate(LEVELS.items()):
            for cam in (np.zeros(3), np.array([-10000., 20000., -30000.])):
                pts = cam + np.array([[0, 0, 0], [100, -200, 300], [delta * .49, delta * .2, -delta * .3], [-delta * 63, -delta * 63, -delta * 63], [delta * 63.999, delta * 63.999, delta * 63.999]])
                base = [l.split() for l in tool.run(['address', level, *[repr(float(v)) for v in cam]], fmt(pts)).splitlines()[1:]]
                contained = 0
                for axis in range(3):
                    moved = cam + np.eye(3)[axis] * delta
                    shifted = [l.split() for l in tool.run(['address', level, *[repr(float(v)) for v in moved]], fmt(pts)).splitlines()[1:]]
                    for p, a, b in zip(pts, base, shifted):
                        key = np.floor(p / delta).astype(np.int64); storage = key % 128; group = storage[2] // 4
                        self.assertEqual([int(v) for v in a[:3]], key.tolist()); self.assertEqual(a[:3], b[:3])
                        self.assertEqual([int(v) for v in a[6:9]], [(group % 8) * 129 + storage[0], (group // 8) * 129 + storage[1], storage[2] % 4])
                        self.assertEqual(a[6:9], b[6:9], 'storage texel is origin independent')
                        local_a = np.array(a[3:6], np.int64); local_b = np.array(b[3:6], np.int64)
                        np.testing.assert_array_equal(local_a, key - self.m.window_origin(cam, delta)); np.testing.assert_array_equal(local_a - local_b, np.eye(3, dtype=np.int64)[axis])
                        self.assertEqual(a[12], '1' if ((local_a >= 0) & (local_a <= 126)).all() else '0'); contained += a[12] == '1'
                self.assertGreaterEqual(contained, 4 * 3, 'the origin, near and far-corner points sit inside the window')
        # Lane transition and group wrap: storage z 0,1,2,3,4,127 -> lanes 0,1,2,3,0,3 and tiles (0,0),(1,0),(7,3).
        rows = [l.split() for l in tool.run(['address', 0, 0., 0., 0.], fmt([[0., 0., z * 512.] for z in (0, 1, 2, 3, 4, 127)])).splitlines()[1:]]
        self.assertEqual([int(r[8]) for r in rows], [0, 1, 2, 3, 0, 3]); self.assertEqual([(int(r[6]), int(r[7])) for r in rows], [(0, 0)] * 4 + [(129, 0), (7 * 129, 3 * 129)])
        far = [l.split() for l in tool.run(['address', 1, 0., 0., 0.], fmt([[4096. * 127, 0., 0.], [4096. * 128, 0., 0.]])).splitlines()[1:]]
        self.assertEqual([r[12] for r in far], ['0', '0'], 'local 190/191 is outside the window')
        bad = subprocess.run([str(tool.executable), 'address', '2', '0', '0', '0'], input=b'', capture_output=True)
        self.assertEqual(bad.returncode, 2, 'an unknown level is refused')

    def test_tile_packing_border_and_identity(self):
        store = self.m.LazyStore()
        for name, delta in LEVELS.items():
            cam = TILE_ORIGINS[name]; origin = self.m.window_origin(np.array(cam), delta); group = 0 if name == 'fine' else 31
            storage = np.arange(128); keys = origin[:, None] + ((storage[None, :] - origin[:, None]) & 127)  # node per storage index, per axis
            ref = np.zeros((128, 128, 4), np.uint16)
            for lane in range(4):
                grid = np.stack(np.meshgrid(keys[0], keys[1], np.array([keys[2][4 * group + lane]]), indexing='ij'), -1).reshape(-1, 3)
                ref[:, :, lane] = store.get(name, grid).view(np.uint16).reshape(128, 128).T  # [row=y, column=x]
            for tool in self.each_tool():
                tile = np.frombuffer(tool.run(['tile', repr(delta), *origin.tolist(), group], b'', binary=True), np.uint16).reshape(129, 129, 4)
                np.testing.assert_array_equal(tile[:128, :128], ref)
                np.testing.assert_array_equal(tile[:, 128], tile[:, 0]); np.testing.assert_array_equal(tile[128, :], tile[0, :])
                if name == 'fine' and tool is self.tools['native']:
                    bench = json.loads(tool.run(['bench', '0.05']))['levels']['fine']
                    self.assertEqual(bench['origin'], origin.tolist()); self.assertEqual(int(bench['tile_group0_fnv1a'], 16), fnv1a(tile.tobytes()))
                    self.assertGreater(bench['nodes_per_second'], 0)


if __name__ == '__main__':
    unittest.main()
