"""Host tests of the FL oversized-disc port (src/engine/fl/circle_fill_fl.cpp, src/engine/fl/circle_fill_sites_fl.h).

Facts: docs/reverse-engineering/fl/circle-fill.md, "Port (2026-11-03)"; site verifier
verification/probe/verify_circle_fill_site_fl.py. (a) The native fixture verification/probe/fl_circle_fill.cpp (MinGW
i686 with the project's flags, run natively through WSL interop) maps X3FL.exe into a relocated 64 KB-aligned private
image and runs the real rasterizer, lock, class function, class-0 leaf and CRT fill unpatched and patched; every RUN
line it prints is checked here against an independent model of the instruction stream (`native_spans`: the 32-bit
wrapping stock routine; with wrap=False the exact port), the route against `route` below, and the patched/native
effect pairs against each other. (b) The installed executable through the site verifier. (c) Edition wiring.
"""
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'verification/probe/fl_circle_fill.cpp'
SHIM = ROOT / 'verification/probe/fl_circle_fill_shim.h'
EXE = Path(os.environ.get('X3M_FL_EXE', '/mnt/c/X3FL/X3FL.exe'))
FL_SHA256 = 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'
INT_MIN = -2 ** 31


def i32(x):
    return (x + 2 ** 31) % 2 ** 32 - 2 ** 31


def s16(x):
    return (x + 32768) % 65536 - 32768


def u16(x):
    return x & 0xffff


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def quotient(aspect):
    return i32(((aspect * aspect) + 0x8000) >> 16)


def native_b(A, aspect):
    """0x10ca24..58 under round-to-nearest: q to float32, times 2^-16 (float32), A / that (binary64), truncated;
    out of range or NaN gives the int32 indefinite value."""
    den = f32(f32(quotient(aspect)) * 2.0 ** -16)
    try:
        v = A / den
    except ZeroDivisionError:
        return INT_MIN
    if math.isnan(v) or not (-2 ** 31 <= v < 2 ** 31):
        return INT_MIN
    return math.trunc(v)


def native_spans(r_word, aspect, cx, cy, W, H, wrap=True, cap=4_000_000):
    """The ordered leaf calls of 0x10c940 as (x0, y, x1) signed words: wrap=True the stock routine's 32-bit decision
    values, wrap=False the exact recurrence (the port). Clip flag before the lock from signed words; x0 = max(left, 0),
    x1 = right < W ? right : W - 1, x0 > x1 skipped, each row's low word unsigned against the height; cy + y then
    cy - y. Raises when `cap` iterations pass (the caller must not ask for a non-terminating input)."""
    w = i32 if wrap else (lambda v: v)
    R = s16(r_word)
    A = w(R * R)
    B = native_b(A, aspect)
    if not wrap:
        assert 0 <= B <= 32767 ** 2
    clip = s16(cx) + R >= s16(W) or s16(cx) - R < 0 or s16(cy) + R >= s16(H) or s16(cy) - R < 0
    out = []

    def pair(left, right, y):
        up, down = cy + y, cy - y
        if clip:
            x0 = max(s16(left), 0)
            x1 = s16(right) if s16(right) < s16(W) else s16(u16(W) - 1)
            if x0 > x1:
                return
            if u16(up) < u16(H):
                out.append((x0, s16(up), x1))
            if u16(down) < u16(H):
                out.append((x0, s16(down), x1))
        else:
            out.append((s16(left), s16(up), s16(right)))
            out.append((s16(left), s16(down), s16(right)))

    Y, X, steps = u16(r_word), 0, 0
    d = w(2 * A * (1 - B) + 2 * B * (R - 1) * R + B)
    if w(R * B) > 0:
        left = right = u16(cx)
        while True:
            steps += 1
            if steps > cap:
                raise RuntimeError('region 1 does not stop')
            if d >= 0:
                pair(left, right, Y)
                Y -= 1
                d = w(d - s16(Y) * 4 * B)
            d = w(d + (2 * s16(X) + 3) * 2 * A)
            left -= 1
            X += 1
            right += 1
            if not w(s16(Y) * B) > w(s16(X) * A):
                break
    x, y = s16(X), s16(Y)
    d = w(((y - 2) * y + 1) * 2 * B + (x + 1) * x * 2 * A + (1 - 2 * B) * A)
    if y + 1 == 0:
        return out
    left, right = cx - X, cx + X
    while True:
        steps += 1
        if steps > cap:
            raise RuntimeError('region 2 does not stop')
        pair(left, right, Y)
        if not d > 0:
            X += 1
            d = w(d + s16(X) * 4 * A)
            left -= 1
            right += 1
        Y -= 1
        d = w(d + (3 - 2 * s16(Y)) * 2 * B)
        if s16(Y) + 1 == 0:
            return out


def route(r_word, aspect, cx, cy, W, H):
    """The documented replay-free routing (circle-fill.md, "Routing and clipping policy"), written from the note."""
    r = s16(r_word)
    if r <= 0 or (r <= 813 and aspect == 65536):
        return 'stock_fast'
    q = quotient(aspect)
    if q <= 0:
        return 'stock_quotient'
    if q >= 65536:
        return 'wide'
    A = r * r
    B = (A << 16) // q
    if B > 32767 ** 2:
        return 'stock_axis'
    clip = s16(cx) + r >= s16(W) or s16(cx) - r < 0 or s16(cy) + r >= s16(H) or s16(cy) - r < 0
    if not clip and B > A:
        spans = exact_unclipped(r_word, B, cx, cy)
        if any(not (0 <= x0 <= x1 < s16(W) and 0 <= y < s16(H)) for x0, y, x1 in spans):
            return 'stock_extent'
    return 'wide'


def exact_unclipped(r_word, B, cx, cy):
    """The exact recurrence's unclipped spans for an integer B (the routing preflight's walk)."""
    R = s16(r_word)
    A = R * R
    out = []
    Y, X = u16(r_word), 0
    d = 2 * A * (1 - B) + 2 * B * (R - 1) * R + B
    if R * B > 0:
        left = right = u16(cx)
        while True:
            if d >= 0:
                out += [(s16(left), s16(cy + Y), s16(right)), (s16(left), s16(cy - Y), s16(right))]
                Y -= 1
                d -= s16(Y) * 4 * B
            d += (2 * s16(X) + 3) * 2 * A
            left -= 1
            X += 1
            right += 1
            if not s16(Y) * B > s16(X) * A:
                break
    x, y = s16(X), s16(Y)
    d = ((y - 2) * y + 1) * 2 * B + (x + 1) * x * 2 * A + (1 - 2 * B) * A
    if y + 1 == 0:
        return out
    left, right = cx - X, cx + X
    while True:
        out += [(s16(left), s16(cy + Y), s16(right)), (s16(left), s16(cy - Y), s16(right))]
        if d <= 0:
            X += 1
            d += s16(X) * 4 * A
            left -= 1
            right += 1
        Y -= 1
        d += (3 - 2 * s16(Y)) * 2 * B
        if s16(Y) + 1 == 0:
            return out


def records_hash(spans, cls=0):
    h = 0xcbf29ce484222325
    for x0, y, x1 in spans:
        for b in bytes([cls]) + struct.pack('<hhh', x0, y, x1):
            h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h


RUN = re.compile(r'^RUN kind=(\w+) id=(\d+) r=(-?\d+) aspect=(-?\d+) cx=(-?\d+) cy=(-?\d+) w=(-?\d+) h=(-?\d+) '
                 r'mxcsr=(\w+) route=(\w+) records=(\d+) hash=([0-9a-f]+) getdesc=(\d+) lockrect=(\d+) recovery=(\d+) '
                 r'classes=(\d+) diags=(\d+) bits=(\w+) flags=([0-9a-f]+) mxcsr_out=([0-9a-f]+) regs=(\w+) tag=(\w+)$', re.M)


def parse_runs(text):
    runs = []
    for m in RUN.finditer(text):
        g = m.groups()
        runs.append(dict(kind=g[0], id=int(g[1]), r=int(g[2]), aspect=int(g[3]), cx=int(g[4]), cy=int(g[5]),
                         w=int(g[6]), h=int(g[7]), mxcsr=int(g[8], 16), route=g[9], records=int(g[10]),
                         hash=int(g[11], 16), effects=g[12:17] + g[17:20], regs=g[20], tag=g[21]))
    return runs


def expected(run):
    """(model kind, spans) for a plain run (class 0 leaves, round-to-nearest, nothing scripted)."""
    args = (run['r'], run['aspect'], run['cx'], run['cy'], run['w'], run['h'])
    if run['kind'] == 'native' or route(*args) != 'wide':
        return 'stock', native_spans(*args, wrap=True)
    return 'exact', native_spans(*args, wrap=False)


def build(tmp):
    """Builds the fixture from the production sources with the CMake per-file flags; None when the toolchain, interop
    or the executable is absent."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists() or not EXE.exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_CIRCLE_FILL_FL_FIXTURE', '-msse2',
              '-mfpmath=sse']
    units = (('src/engine/fl/circle_fill_fl.cpp', ['-fno-exceptions', '-mno-sse', '-mno-mmx', '-mfpmath=387',
                                                   '-include', str(SHIM), '-DX3M_CIRCLE_FILL_FL_SHIM']),
             ('src/proxy/engine_patch.cpp', []), ('src/engine/fl/executable_identity_fl.cpp', []))
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_circle_fill.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return exe


def windows_path(path):
    return subprocess.run(['wslpath', '-w', str(path)], check=True, capture_output=True, text=True).stdout.strip()


class FlCircleFillNativeFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-circle-')
        cls.exe = build(cls.tmp.name)
        cls.out = None
        if cls.exe:
            run = subprocess.run([str(cls.exe), windows_path(EXE)], text=True, capture_output=True, timeout=600)
            cls.out, cls.code, cls.err = run.stdout, run.returncode, run.stderr
            cls.runs = parse_runs(cls.out)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        if self.exe is None:
            self.skipTest('needs i686-w64-mingw32-g++, WSL interop and X3FL.exe')

    def test_fixture_passes(self):
        fails = '\n'.join(l for l in self.out.splitlines() if l.startswith('FAIL'))[:4000]
        self.assertEqual(self.code, 0, fails + self.err[-1000:])
        self.assertRegex(self.out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', self.out).group(1)), 200)
        flips = re.search(r'FLIPS windows=(\d+) bits=(\d+) refused=(\d+) relocs=(\d+) relocs_refused=(\d+)', self.out)
        self.assertEqual(flips.group(2), flips.group(3))
        self.assertEqual((flips.group(1), flips.group(4), flips.group(5)), ('11', '24', '24'))
        self.assertIn('FAULTS taken=5', self.out)
        self.assertIn('MXCSR modes=5 inputs=6 same=30/30', self.out)
        self.assertRegex(self.out, r'CANARY cases=12 good=12 pixels=\d+')

    def test_rows(self):
        rows = re.findall(r'^LOG (circle_fill.*)$', self.out, re.M)
        site = [r for r in rows if r.startswith('circle_fill site=')]
        self.assertTrue(all(' edition=fl ' in r and ' rva=10c940 ' in r and ' threshold=813 ' in r for r in site), site)
        self.assertEqual(sum('status=patched reason=ok' in r and 'write=atomic' in r for r in site), 2)
        self.assertIn('status=refused reason=late_claim', site[-1])
        wide = [r for r in rows if r.startswith('circle_fill_wide ')]
        self.assertEqual(len(wide), 1, wide)
        self.assertIn(' edition=fl ', wide[0])

    def test_window_hashes_match_header(self):
        line = re.search(r'^WINDOWS (.*)$', self.out, re.M).group(1)
        got = dict(kv.split('=') for kv in line.split())
        header = (ROOT / 'src/engine/fl/circle_fill_sites_fl.h').read_text()
        want = dict(re.findall(r'\{"([a-z0-9_]+)", [a-z0-9_x]+, (?:0x[0-9a-f]+|\d+), 0x([0-9a-f]+)u', header))
        self.assertEqual(len(want), 11)
        self.assertEqual({k: int(v, 16) for k, v in got.items()}, {k: int(v, 16) for k, v in want.items()})

    def test_ordered_spans_against_the_model(self):
        """Every plain RUN (round-to-nearest, no scripted lock failure, dimension change or class fake): records and hash
        equal the model (native runs and stock routes: the wrapping routine; wide: the exact recurrence); every route
        equals the note's routing; every run kept the caller's registers, stack, DF, x87 CW, XMM7, MXCSR controls and
        LastError."""
        compared = cases = 0
        for run in self.runs:
            args = (run['r'], run['aspect'], run['cx'], run['cy'], run['w'], run['h'])
            if run['route'] != 'unpatched':
                self.assertEqual(run['route'], route(*args), run)
            self.assertEqual(run['regs'], 'ok', run)
            if run['tag'] != 'plain':
                continue
            kind, spans = expected(run)
            self.assertEqual((run['records'], run['hash']), (len(spans), records_hash(spans)), (kind, run))
            compared += run['records']
            cases += 1
        self.assertGreaterEqual(cases, 600)
        self.assertGreaterEqual(compared, 1_000_000)
        print(f'\nordered-span differential: {cases} runs, {compared} ordered records equal the model', file=sys.stderr)

    def test_differential_covers_the_note(self):
        """Radii 813/814/825/826/1290/1291/32767 run wide at aspect 65536 and match the exact model; native runs of
        826+ differ from it (the defect); non-unit aspects take every route; spans touch all four surface edges."""
        by = {}
        for run in self.runs:
            by.setdefault(run['id'], {})[run['kind']] = run
        wide_radii, edges, routes = set(), set(), set()
        for pair in by.values():
            p = pair.get('patched')
            if p:
                routes.add(p['route'])
            if not p or p['tag'] != 'plain' or p['aspect'] != 65536 or p['route'] != 'wide':
                continue
            spans = native_spans(p['r'], p['aspect'], p['cx'], p['cy'], p['w'], p['h'], wrap=False)
            if (p['records'], p['hash']) == (len(spans), records_hash(spans)):
                wide_radii.add(s16(p['r']))
                if p['w'] == 1024:
                    xs = {x for a, _, b in spans for x in (a, b)}
                    ys = {y for _, y, _ in spans}
                    edges |= {'left'} if 0 in xs else set()
                    edges |= {'right'} if 1023 in xs else set()
                    edges |= {'top'} if 0 in ys else set()
                    edges |= {'bottom'} if 767 in ys else set()
        self.assertTrue({814, 825, 826, 1290, 1291, 32767} <= wide_radii, wide_radii)
        self.assertEqual(edges, {'left', 'right', 'top', 'bottom'})
        self.assertEqual(routes, {'stock_fast', 'stock_quotient', 'stock_axis', 'stock_extent', 'wide'})
        # The unclipped 826 disc (surface 1716 square, centre 858): the stock routine's spans differ from the port's.
        native_826 = [r for r in self.runs if r['kind'] == 'native' and r['r'] == 826 and r['aspect'] == 65536
                      and r['w'] == 1716 and r['tag'] == 'plain']
        exact = native_spans(826, 65536, 858, 858, 1716, 1716, wrap=False)
        self.assertTrue(native_826 and all(r['hash'] != records_hash(exact) for r in native_826), native_826)

    def test_patched_equals_native_where_comparable(self):
        """For every input run both patched and natively whose route is not wide: the same records, hash and effects
        (GetDesc, LockRect, recovery and class calls, diagnostics, surface bits/flags, MXCSR out). Wide pairs are
        compared in the fixture itself (MXCSR, lock and dimension-change sections) and against the model above."""
        by = {}
        for run in self.runs:
            by.setdefault(run['id'], {})[run['kind']] = run
        same = 0
        for pair in by.values():
            if 'patched' not in pair or 'native' not in pair:
                continue
            p, n = pair['patched'], pair['native']
            if p['route'] != 'wide':
                self.assertEqual((p['records'], p['hash'], p['effects']), (n['records'], n['hash'], n['effects']), p)
                same += 1
        self.assertGreaterEqual(same, 100)


@unittest.skipUnless(EXE.is_file(), 'installed X3FL.exe not present')
class FlCircleFillExecutable(unittest.TestCase):
    def test_site_verifier(self):
        """verify_circle_fill_site_fl.py on the installed X3FL.exe: every check, every mutation guard."""
        run = subprocess.run([sys.executable, str(ROOT / 'verification/probe/verify_circle_fill_site_fl.py'), str(EXE)],
                             capture_output=True, text=True, timeout=900)
        self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr[-2000:])
        report = json.loads(run.stdout)
        self.assertEqual((report['result'], report['failed']), ('PASS', []))
        self.assertEqual(len(report['checks']), 29)
        self.assertEqual(len(report['mutation_guards']), 8)
        self.assertTrue(all(report['mutation_guards'].values()), report['mutation_guards'])
        self.assertEqual(report['direct_callers'], ['0xa7b19'])


def strip(text):
    return re.sub(r'//.*', '', text)


class FlCircleFillModel(unittest.TestCase):
    def test_model_reproduces_the_note(self):
        self.assertEqual(native_b(814 * 814, 1051682), 2572)
        self.assertEqual((814 * 814 * 65536) // quotient(1051682), 2573)
        self.assertTrue(all(native_spans(r, 65536, 2000, 2000, 4000, 4000) ==
                            native_spans(r, 65536, 2000, 2000, 4000, 4000, wrap=False) for r in (1, 300, 813, 825)))
        self.assertNotEqual(native_spans(826, 65536, 2000, 2000, 4000, 4000),
                            native_spans(826, 65536, 2000, 2000, 4000, 4000, wrap=False))
        witness = native_spans(300, 32768, 400, 400, 800, 800)
        self.assertEqual((len(witness), min(x for x, _, _ in witness), max(x for _, _, x in witness)), (602, -199, 999))
        self.assertEqual(route(300, 32768, 400, 400, 800, 800), 'stock_extent')
        self.assertEqual(route(900, 32768, 2000, 2000, 4000, 4000), 'wide')
        self.assertEqual(route(32767, 256, 0, 0, 832, 700), 'stock_axis')
        self.assertEqual(route(1640, 0, 80, 120, 832, 700), 'stock_quotient')
        self.assertEqual(route(813, 65536, 80, 120, 832, 700), 'stock_fast')


class FlCircleFillWiring(unittest.TestCase):
    def test_edition_lists_and_capture(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/circle_fill_fl.cpp)', cmake)
        self.assertIn('set_source_files_properties(src/engine/fl/circle_fill_fl.cpp\n  PROPERTIES COMPILE_OPTIONS '
                      '"-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")', cmake)
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('circle_fill.cpp', linked)  # AP only: the fl target links its own provider
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        second = capture.rindex('circle_fill::initialize();')
        self.assertNotEqual(second, capture.index('circle_fill::initialize();'))
        self.assertIn('#if X3M_EDITION_FL', capture[second - 600:second])
        self.assertLess(capture.rindex('fov::initialize();'), second)
        self.assertLess(second, capture.index('own_sun_hook::initialize();'))

    def test_provider_contract(self):
        module = strip((ROOT / 'src/engine/fl/circle_fill_fl.cpp').read_text())
        self.assertIn('movsx eax, word ptr [esp+8]\n    test eax, eax\n    jle 2f\n    cmp eax, 813\n    jg 1f\n'
                      '    cmp dword ptr [esp+16], 65536\n    je 2f\n1:  pushfd\n    pushad\n    cld', module)
        self.assertIn('install_window_open()', module)
        self.assertIn('x3m::config::get(L"X3M_CIRCLE_FILL_FIX", text, ap::setting_capacity)', module)
        self.assertIn('edition=fl status=%s reason=%s mode=%s setting=%s write=%s threshold=%ld continue=%08lx', module)
        # The B conversion: the engine's instruction sequence, after the class call.
        self.assertLess(module.index('x3m_circle_fill_fl_class(class_va_'), module.index('x3m_circle_fill_fl_axis(A,'))
        self.assertLess(module.index('F::route(cx, cy, r, aspect'), module.index('x3m_circle_fill_fl_lock(lock_va_'))
        self.assertIn('cvtdq2ps xmm0, xmm0\n    mulss xmm0, dword ptr [_x3m_circle_fill_fl_scale]\n    cvtps2pd xmm0, xmm0\n'
                      '    divsd xmm1, xmm0\n    cvttsd2si eax, xmm1', module)


if __name__ == '__main__':
    unittest.main()
