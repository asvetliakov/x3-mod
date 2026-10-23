"""SETA and manual flight (X3M_SETA_MANUAL_FLIGHT, docs/architecture/seta-manual-flight.md): the core of
src/proxy/seta_manual_flight_core.h compiled on the host (verification/probe/seta_manual_flight_host.cpp), the claim
sites and compared windows against X3AP.exe (instruction boundaries, entries, liveness, the live-image comparison with
every other declared claim applied), the five stubs executed natively with the DLL's flags
(verification/probe/seta_manual_flight_stub_fixture.cpp through WSL interop), the launcher flag on hermetic
`manage.py launch --dry-run` runs (nothing launched), the config entry and the production wiring. No game, no Wine."""
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import verification.analysis.test_seta_restore as seta_restore

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
import schema  # noqa: E402

EXE = Path('/mnt/c/X3/X3AP.exe')
SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
BASE = 0x00400000
HOST = ROOT / 'verification/probe/seta_manual_flight_host.cpp'
FIXTURE = ROOT / 'verification/probe/seta_manual_flight_stub_fixture.cpp'
# The DLL's code-generation flags (CMakeLists.txt) for the native fixture.
MINGW_FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse', '-mstackrealign',
               '-mincoming-stack-boundary=2']
ROTATION = {'seta_manual_flight_yaw': 0x0040ebde, 'seta_manual_flight_pitch': 0x0040ee3e, 'seta_manual_flight_roll': 0x0040f04d}
STRAFE = {'seta_manual_flight_strafe_y': 0x00411e0c, 'seta_manual_flight_strafe_x': 0x00411eb2}
# The stop block's nine flag rows 0x004120c5 + 8k and their flags (docs/reverse-engineering/seta-manual-flight.md section 2)
STOP_ROWS = ['stop_yaw', 'stop_pitch', 'stop_roll', 'stop_turret_yaw', 'stop_turret_pitch', 'stop_strafe_x', 'stop_strafe_y',
             'stop_speed', 'stop_look']
FLAGS = [0x00608e60, 0x00608e88, 0x00608eb0, 0x00608ed8, 0x00608f0c, 0x00608f40, 0x00608f6c, 0x00608f98, 0x00608fd0]
FLAG_READERS = re.compile(r'^(j(?!mp)\w+|set\w+|cmov\w+|adc|sbb|pushf\w*|lahf|rc[lr])$')


def host_build(directory):
    compiler = shutil.which('c++') or shutil.which('clang++')
    if compiler is None:
        raise unittest.SkipTest('A host C++ compiler is required')
    exe = Path(directory) / 'seta_manual_flight_host'
    build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(HOST), '-o', str(exe)],
                           capture_output=True, text=True, timeout=120)
    if build.returncode:
        raise AssertionError(build.stdout + build.stderr)
    return exe


class Core(unittest.TestCase):
    def test_decisions(self):
        with tempfile.TemporaryDirectory(prefix='x3-smf-') as d:
            run = subprocess.run([str(host_build(d))], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('failures=0 PASS', run.stdout)
            self.assertGreaterEqual(int(re.search(r'checks=(\d+)', run.stdout).group(1)), 530)


class Image(unittest.TestCase):
    """The windows the installer compares and the six claims, against the pinned X3AP.exe."""

    @classmethod
    def setUpClass(cls):
        if not EXE.is_file() or hashlib.sha256(EXE.read_bytes()).hexdigest() != SHA256:
            raise unittest.SkipTest('The pinned X3AP.exe is required at %s' % EXE)
        try:
            import capstone
        except ImportError:
            raise unittest.SkipTest('capstone is required')
        cls.directory = tempfile.TemporaryDirectory(prefix='x3-smf-image-')
        cls.host = host_build(cls.directory.name)
        listing = lambda flag: [l.split() for l in subprocess.run([str(cls.host), flag], capture_output=True, text=True,  # noqa: E731
                                                                  timeout=30, check=True).stdout.splitlines()]
        cls.windows = [(n, int(va, 16), int(length), int(part)) for n, va, length, part in listing('--windows')]
        sites = listing('--sites')
        cls.sites = [(f[0], int(f[1], 16), int(f[2])) for f in sites]
        cls.stop = [(f[0], int(f[1], 16), bytes.fromhex(f[3]), bytes.fromhex(f[4])) for f in sites if len(f) == 5]
        data = EXE.read_bytes()
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        count, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
        sections = [struct.unpack_from('<IIII', data, pe + 24 + optional + 40 * i + 8) for i in range(count)]
        image = bytearray(max(va + max(vs, rs) for vs, va, rs, _ in sections))
        for vs, va, rs, raw in sections:
            image[va:va + rs] = data[raw:raw + rs]
        cls.image = image
        vs, va, rs, raw = [s for s in sections if s[1] == 0x1000][0]
        cls.text = (BASE + va, data[raw:raw + rs])
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        md.skipdata = True
        cls.starts, cls.branches = set(), []
        for a, size, mnemonic, op in md.disasm_lite(cls.text[1], cls.text[0]):
            cls.starts.add(a)
            if (mnemonic.startswith('j') or mnemonic == 'call') and op.startswith('0x'):
                cls.branches.append((a, int(op, 16)))
        cls.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def read(self, va, n):
        return bytes(self.image[va - BASE:va - BASE + n])

    def decode(self, va, n):
        return [(i.address, i.size, i.mnemonic, i.op_str) for i in self.md.disasm(self.read(va, n), va)]

    def install_check(self, image, mode=2):
        path = Path(self.directory.name) / 'image.bin'
        path.write_bytes(bytes(image))
        run = subprocess.run([str(self.host), '--image', str(path), str(mode)], capture_output=True, text=True, timeout=60)
        return run.returncode, run.stdout.strip()

    def test_tables(self):
        self.assertEqual([n for n, *_ in self.windows], STOP_ROWS + ['stop_block', 'yaw_controller', 'pitch_controller', 'roll_controller',
                                                                     'strafe_block', 'mouse_dispatch', 'mouse_table'])
        self.assertEqual(self.sites, [('seta_manual_flight_' + n, 0x004120cb + 8 * k, 2) for k, n in enumerate(STOP_ROWS[:7])] +
                         [('seta_manual_flight_stop_look', 0x0041210b, 2)] +
                         [(n, va, 14) for n, va in ROTATION.items()] + [(n, va, 6) for n, va in STRAFE.items()])
        self.assertEqual(self.install_check(self.image), (0, 'image mode=2 windows=16 mismatch=none'))  # the EXE on disk
        self.assertEqual(self.install_check(self.image, 1), (0, 'image mode=1 windows=10 mismatch=none'))

    def patched_image(self):
        image = bytearray(self.image)
        for _, va, _, patched in self.stop:
            image[va - BASE:va - BASE + 2] = patched
        return image

    def test_stop_rows(self):
        # the nine rows `cmp [flag],ebx; jcc`, each window at its row; the jne of rows 1..8 to the stop, the look je to the join
        rows = dict((n, (va, size)) for n, va, size, _ in self.windows)
        for k, (name, flag) in enumerate(zip(STOP_ROWS, FLAGS)):
            va = 0x004120c5 + 8 * k
            self.assertEqual(rows[name], (va, 8), name)
            insns = self.decode(va, 8)
            self.assertEqual([(a, s, m) for a, s, m, _ in insns], [(va, 6, 'cmp'), (va + 6, 2, 'je' if k == 8 else 'jne')], name)
            self.assertEqual(insns[0][3], 'dword ptr [0x%x], ebx' % flag, name)
            self.assertEqual(insns[1][3], '0x412136' if k == 8 else '0x41210d', name)
        self.assertEqual(self.read(0x004120a5, 2), bytes.fromhex('33 db'))  # EBX = 0
        # the eight rewrites: the EXE holds each original, at the row's jcc, two bytes inside one aligned qword
        self.assertEqual([(va, orig.hex(), new.hex()) for _, va, orig, new in self.stop],
                         [(0x004120cb + 8 * k, '75%02x' % (0x40 - 8 * k), '6690') for k in range(7)] + [(0x0041210b, '7429', 'eb29')])
        for name, va, original, _ in self.stop:
            self.assertEqual(self.read(va, 2), original, name)
            self.assertLessEqual((va & 7) + 2, 8, name)  # write_code's lock cmpxchg8b path
            self.assertEqual(va >> 12, 0x412, name)  # one page: one protection span
        # the patched image: every instruction start 0x004120bb..0x00412135 unchanged, the rewrites decode as intended,
        # the speed row's jne and the stop's je untouched
        patched = self.patched_image()
        before = self.decode(0x004120bb, 0x00412136 - 0x004120bb)
        after = [(i.address, i.size, i.mnemonic, i.op_str) for i in self.md.disasm(bytes(patched[0x004120bb - BASE:0x00412136 - BASE]), 0x004120bb)]
        self.assertEqual([x[:2] for x in before], [x[:2] for x in after])
        decoded = dict((a, (m, o)) for a, _, m, o in after)
        for name, va, *_ in self.stop:
            self.assertEqual(decoded[va], ('jmp', '0x412136') if name.endswith('_look') else ('nop', ''), name)
        self.assertEqual(decoded[0x00412103], ('jne', '0x41210d'))
        self.assertEqual(decoded[0x0041211d], ('je', '0x412136'))
        # entries: nothing in .text branches into 0x004120bc..0x0041210c or into a rewrite's interior, no dword points there;
        # after the patch the speed jne is the only branch to the stop 0x0041210d and the look row never falls through
        self.assertEqual([(a, t) for a, t in self.branches if 0x004120bb < t < 0x0041210d], [])
        self.assertEqual([(a, t) for a, t in self.branches for _, va, *_ in self.stop if t == va + 1], [])
        text_va, text = self.text
        self.assertEqual([text_va + i for i in range(len(text) - 3) if 0x004120bb < struct.unpack_from('<I', text, i)[0] < 0x0041210d], [])
        self.assertEqual(sorted(a for a, t in self.branches if t == 0x0041210d), [0x004120cb + 8 * k for k in range(8)])
        self.assertEqual(decoded[0x0041210b][0], 'jmp')
        # the stop path keeps its own warp check
        self.assertEqual(self.read(0x00412113, 12), bytes.fromhex('81 ba cc 00 00 00 00 00 01 00 74 17'))
        self.assertEqual([(a, t) for a, t in self.branches if 0x0041210d < t < 0x00412136], [])
        # outside seta_restore's stop_call window 0x0041211f+23
        self.assertFalse([va for _, va, *_ in self.stop if va < 0x0041211f + 23 and 0x0041211f < va + 2])

    def test_claim_boundaries_and_entries(self):
        text_va, text = self.text
        for name, va, length in [s for s in self.sites if s[0] in ROTATION or s[0] in STRAFE]:
            insns = self.decode(va, length)
            self.assertEqual(sum(s for _, s, *_ in insns), length, name)  # whole instructions
            self.assertTrue(all(a in self.starts for a, *_ in insns) and va + length in self.starts, name)
            self.assertNotIn('j', ''.join(m[0] for _, _, m, _ in insns), name)  # no relative branch inside (rel32_offset 0)
            interior = [(a, t) for a, t in self.branches if va < t < va + length]
            self.assertEqual(interior, [], name)  # entered only at the first byte
            pointers = [text_va + i for i in range(len(text) - 3) if va < struct.unpack_from('<I', text, i)[0] < va + length]
            self.assertEqual(pointers, [], name)  # no jump-table entry into the span
        for name, va in ROTATION.items():
            insns = self.decode(va, 14)
            self.assertEqual([m for _, _, m, _ in insns], ['mov', 'fimul', 'mov'], name)
            self.assertEqual(insns[0][3], 'eax, dword ptr [0x606f34]', name)
            self.assertEqual(insns[1][3], 'dword ptr [eax + 0x714]', name)
            self.assertEqual(insns[2][3], 'ecx, dword ptr [%s + 0x24]' % ('esi' if name.endswith('roll') else 'edi'), name)
            joins = [a for a, t in self.branches if t == va]
            self.assertEqual(len(joins), 1, name)  # the class-7 accel path's jmp; the other class falls through
        for name, va in STRAFE.items():
            self.assertEqual(self.decode(va, 6), [(va, 6, 'fild', 'dword ptr [esi + 0x714]')], name)

    def test_liveness_after_each_span(self):
        # rotation: sub esp,8 rewrites the flags at once; the step is consumed by fmul 0.001; ECX is pushed unchanged;
        # the slew 0x0040e9a0 reads neither EDX nor EAX (fnstsw writes AX before every test)
        for name, va in ROTATION.items():
            after = self.decode(va + 14, 0x20)
            self.assertEqual((after[0][2], after[0][3]), ('sub', 'esp, 8'), name)
            self.assertEqual((after[1][2], after[1][3]), ('fmul', 'qword ptr [0x565500]'), name)
            push = next(k for k, x in enumerate(after) if x[2] == 'push')
            self.assertEqual(after[push][3], 'ecx', name)
            self.assertFalse([x for x in after[:push] if re.search(r'\becx\b', x[3])], name)
        slew = self.decode(0x0040e9a0, 0x89)
        self.assertFalse([x for x in slew if re.search(r'\b(edx|dx|dl|dh)\b', x[3])])
        self.assertTrue(all(x[2] == 'fnstsw' for x in slew if re.search(r'\b(eax|ax)\b', x[3])))
        self.assertEqual(struct.unpack('<d', self.read(0x00565500, 8))[0], 0.001)
        # strafe: x87 only up to the first flag write (`test ah`), no flag reader before it; the 1/k-scaled ms feeds the
        # limit 20000 * ms * 0.001 (fmul st(3)/fmul st(2) or fmulp st(4)/fmulp st(2)) and the result reaches ftol
        for name, va in STRAFE.items():
            after = self.decode(va + 6, 0x40)
            first_write = next(k for k, x in enumerate(after) if x[2] in ('test', 'cmp', 'sub', 'add', 'and', 'or', 'xor'))
            self.assertFalse([x for x in after[:first_write] if FLAG_READERS.match(x[2])], name)
            self.assertTrue(all(x[2].startswith('f') for x in after[:first_write - 1]), name)
            self.assertEqual(after[first_write][2:], ('test', 'ah, 0x41' if name.endswith('_x') else 'ah, 5'), name)
        self.assertEqual(struct.unpack('<f', self.read(0x00565650, 4))[0], 20000.0)

    def test_every_declared_claim(self):
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        import verify_collide_sites
        claims = verify_collide_sites.other_claims(ROOT, skip_prefix='seta_manual_flight', own_names=())
        self.assertGreater(len(claims), 100)
        hits = [(n, hex(a), w) for n, a, length in claims for w, va, size, _ in self.windows if a < va + size and va < a + length]
        self.assertEqual(hits, [])  # no other feature claims inside a compared window
        image = bytearray(self.image)
        for name, address, length in claims:
            at = address - BASE
            if 0 <= at < len(image) - 5:
                image[at:at + min(5, length)] = b'\xe9\0\0\0\0'[:min(5, length)]
        self.assertEqual(self.install_check(image), (0, 'image mode=2 windows=16 mismatch=none'))
        # the stop rows 0x004120c5..0x0041210c hold no other claim (the input routine's entry 0x00410100, seta_restore's
        # stop_call 0x0041211f included)
        self.assertEqual([(n, hex(a)) for n, a, length in claims if a < 0x0041210d and 0x004120c5 < a + length], [])
        # and the other direction: this feature's claims, the eight 2-byte rewrites with their length, are visible to
        # every other feature's collector
        mine = [c for c in verify_collide_sites.other_claims(ROOT, skip_prefix='-', own_names=()) if c[0].startswith('seta_manual_flight')]
        self.assertEqual(sorted(mine), sorted(self.sites))
        self.assertEqual(sorted(c for c in mine if c[2] == 2), sorted((n, va, 2) for n, va, *_ in self.stop))
        # the live image with every claim, this feature's eight actual rewrites included: part A refuses a second
        # install, naming the first row
        for _, va, _, patched in self.stop:
            image[va - BASE:va - BASE + 2] = patched
        self.assertEqual(self.install_check(image, 1), (1, 'image mode=1 windows=10 mismatch=stop_yaw va=0x004120c5 readable=1 got=391d608e60006690'))

    def test_foreign_write_names_the_window(self):
        image = bytearray(self.image)
        image[ROTATION['seta_manual_flight_pitch'] - BASE:ROTATION['seta_manual_flight_pitch'] - BASE + 5] = b'\xe9\0\0\0\0'
        self.assertEqual(self.install_check(image),
                         (1, 'image mode=2 windows=16 mismatch=pitch_controller va=0x0040ee30 readable=1 got=d80de0545600eb06'))
        self.assertEqual(self.install_check(image, 1)[0], 0)  # part A alone does not depend on part B's windows
        image = bytearray(self.image)
        image[0x00411f1a - BASE] ^= 0xff  # the strafe block's last byte, past every 16-byte prologue bound
        code, line = self.install_check(image)
        self.assertEqual((code, line.split(' got=')[0]), (1, 'image mode=2 windows=16 mismatch=strafe_block va=0x00411dd0 readable=1'))
        image = bytearray(self.image)
        image[0x0041211d - BASE] = 0xeb  # the previous release's stop byte is a mismatch too
        self.assertEqual(self.install_check(image, 1)[1].split(' va=')[0], 'image mode=1 windows=10 mismatch=stop_block')
        # each row is compared whole and named: its jcc, its rel8, its flag operand, the unpatched speed row
        for va, value, name in ((0x004120fb, b'\xeb\x00', 'stop_strafe_y'), (0x0041210c, b'\x2a', 'stop_look'),
                                (0x004120ff, b'\x99', 'stop_speed'), (0x00412104, b'\x09', 'stop_speed'),
                                (0x004120cf, b'\x89', 'stop_pitch')):
            image = bytearray(self.image)
            image[va - BASE:va - BASE + len(value)] = value
            code, line = self.install_check(image, 1)
            self.assertEqual((code, line.split(' va=')[0]), (1, 'image mode=1 windows=10 mismatch=' + name), hex(va))


class NativeStubs(unittest.TestCase):
    """The stubs as the DLL builds them, executed on Windows (WSL interop): the original path through a tail of the
    engine's displaced bytes and the scaled path, at k = 1, 2.5, 10 and a governor case, the mouse-delta skip, the
    record contract, rounding; every register, ESP, DF, the x87 depth and the values below st0."""

    def test_stubs_execute(self):
        compiler = shutil.which('i686-w64-mingw32-g++')
        if not compiler:
            self.skipTest('MinGW cross compiler unavailable')
        if not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
            self.skipTest('WSL interop (a Windows host) is required to execute the i686 fixture')
        with tempfile.TemporaryDirectory(prefix='x3-smf-native-') as d:
            exe = Path(d) / 'seta_manual_flight_stub_fixture.exe'
            build = subprocess.run([compiler, *MINGW_FLAGS, '-I', str(ROOT / 'src/proxy'), str(FIXTURE), '-static', '-s',
                                    '-Wl,--disable-dynamicbase', '-o', str(exe)], capture_output=True, text=True, timeout=180)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            try:
                run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
            except OSError as error:
                self.skipTest('the fixture cannot be executed here: %s' % error)
            self.assertEqual(run.returncode, 0, run.stdout[-4000:] + run.stderr)
            self.assertIn('failures=0 PASS', run.stdout)
            self.assertGreaterEqual(int(re.search(r'checks=(\d+)', run.stdout).group(1)), 921)


class Launcher(seta_restore.Launcher):
    def test_default_on_and_opt_out(self):
        code, env, error = self.launch()
        self.assertEqual(code, 0, error)
        self.assertEqual(env['X3M_SETA_MANUAL_FLIGHT'], '2')
        for value in ('0', '1', '2'):
            self.assertEqual(self.launch('--seta-manual-flight', value)[1]['X3M_SETA_MANUAL_FLIGHT'], value)
        self.assertEqual(self.launch(inherited={'X3M_SETA_MANUAL_FLIGHT': '0'})[1]['X3M_SETA_MANUAL_FLIGHT'], '2')  # no stale value
        self.assertEqual(self.launch('--seta-manual-flight', '3')[0], 2)

    def test_not_forwarded_under_vanilla(self):
        code, env, error = self.launch(vanilla=True, inherited={'X3M_SETA_MANUAL_FLIGHT': '2'})
        self.assertEqual(code, 0, error)
        self.assertNotIn('X3M_SETA_MANUAL_FLIGHT', env)
        code, _, error = self.launch('--seta-manual-flight', '1', vanilla=True)
        self.assertEqual(code, 2)
        self.assertIn('--seta-manual-flight cannot be combined with --vanilla', error)

    def test_player_mode(self):
        self.assertNotIn('X3M_SETA_MANUAL_FLIGHT', self.launch('--config')[1])  # the DLL's default (2) stands in
        self.assertEqual(self.launch('--config', '--seta-manual-flight', '0')[1]['X3M_SETA_MANUAL_FLIGHT'], '0')


class Config(unittest.TestCase):
    def test_schema_entry(self):
        e = schema.BY_KEY['seta_manual_flight']
        self.assertEqual((e['env'], e['type'], e['default'], e['choices'], e['launcher'], e['developer'], e['env_only'], e['section']),
                         ('X3M_SETA_MANUAL_FLIGHT', 'enum', '2', ('0', '1', '2'), '--seta-manual-flight', False, False, 'engine'))

    def test_generated_views(self):
        header = (ROOT / 'src/config/config_schema_inc.h').read_text()
        self.assertRegex(header, r'\{"X3M_SETA_MANUAL_FLIGHT", "seta_manual_flight", Type::Enum, "2", .*"0\|1\|2"')
        template = (ROOT / 'assets/x3m.ini').read_text()
        block = re.search(r'((?:^; .*\n)+);seta_manual_flight = 2$', template, re.M)
        self.assertIsNotNone(block)
        self.assertEqual(len(block.group(1).splitlines()), 2)  # a two-line comment

    def test_read_site(self):
        source = (ROOT / 'src/proxy/seta_manual_flight.cpp').read_text()
        self.assertIn('x3m::config::get(L"X3M_SETA_MANUAL_FLIGHT", setting, 4)', source)
        self.assertIn("length == 1 && setting[0] >= L'0' && setting[0] <= L'2'", source)  # anything else: nothing patched


class Wiring(unittest.TestCase):
    def test_production_wiring(self):
        source = (ROOT / 'src/proxy/seta_manual_flight.cpp').read_text()
        stubs = (ROOT / 'src/proxy/seta_manual_flight_stubs.h').read_text()
        # every window whole before any write; the late window, identity and pin first; rollback on a later failure
        self.assertNotIn('verify_bytes', source)
        self.assertIn('compare_windows(', source)
        self.assertIn('engine_patch::read_code(at,', source)
        order = [source.index(s) for s in ('executable_verified()', 'install_window_open()', 'windows_match(mode)',
                                           'pin_self()) {', 'patch_stop()) {', 'claim_sites(reason)) {')]
        self.assertEqual(order, sorted(order))
        self.assertIn('const bool sites_back = release_sites();\n        const bool stop_back = unpatch_stop();', source)
        self.assertIn('window=0x%08lx window_name=%s window_length=%u expected=%s got=%s', source)
        self.assertIn('"seta_manual_flight installed=1 mode=%u stop=speed_only stop_sites=%u stop_rows=0x%08lx stop_status=%s', source)
        # part A: one protection span, each rewrite registered before its write, rollback of every written row by read-back
        self.assertIn('VirtualProtect(code, stop_span_length, PAGE_EXECUTE_READWRITE, &stop_protection_)', source)
        self.assertIn('stop_patched_[i] = true;\n        bool used_atomic = false;\n        written = write_stop(', source)
        self.assertEqual(source.count('restore_stop_rows()'), 3)  # the definition, the install rollback, unpatch_stop
        self.assertIn('"seta_manual_flight scale frame=%llu wall_ms=%llu rate=0x%08lx', source)
        self.assertIn('row_interval_ms = 500', source)
        self.assertIn('x3m_smf_continue[a] = address_of(*sites_[a].entry);\n        if (!engine_patch::push_front(', source)
        self.assertIn('x3m::LightCallBoundary cpu;', source)
        # the stubs: save, call, restore, then read the decision; the two scaled tails
        for needle in ('pushfd\n    push eax\n    push ecx\n    push edx', 'cld\n    push \\ship', 'pop eax\n    popfd\n    cmp byte ptr [_x3m_smf_apply], 0',
                       'fmul qword ptr [_x3m_smf_step_factor]\n    mov ecx, dword ptr [_x3m_smf_target]\n    mov eax, dword ptr ds:0x00606f34',
                       'fild dword ptr [esi+0x714]\n    fmul qword ptr [_x3m_smf_inv_k]', 'X3M_SMF_ROTATION roll, 2, esi, edi'):
            self.assertIn(needle, stubs)
        self.assertIn("'_x3m_seta_manual_flight_axis'", (ROOT / 'verification/probe/check_no_x87.py').read_text())
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('src/proxy/seta_manual_flight.cpp'), 2)  # the DLL and the -fno-exceptions list
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertLess(capture.index('seta_restore::initialize();'), capture.index('seta_manual_flight::initialize();'))
        self.assertIn('seta_manual_flight::present(ctx.frame);', capture)
        self.assertIn('x3m::seta_manual_flight::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
