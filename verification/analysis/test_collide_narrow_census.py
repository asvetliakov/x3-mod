"""Host checks of the sector-collide narrow-phase census (src/proxy/collide_narrow_census_core.h).

The three-site verifier on the installed executable and its refusals on a
patched copy (changed windows, a branch or a pointer into a displaced span,
another callee, a flag reader at a callee entry, a lost pair register, a
competing claim), the source constants, the stub encoders against the C++
ones, the key / hash / memo / ordering core on a compiled harness, the
install / window / pair line parsers, the production wiring, the x87 audit
roots and the --collide-narrow-census launcher gate (--dry-run only, never a
launch). No Wine.
"""
import contextlib
import importlib.util
import io
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_collide_sites as probe  # noqa: E402
from source_text import source_text

HARNESS = r'''
#include "collide_narrow_census_core.h"
#include <cstdio>
using namespace x3m::collide_narrow_census::core;
static unsigned failures = 0;
static void check(bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }
int main() {
    unsigned char a[stub_capacity];
    const N5Operands o{0x0045d66a, 0x0048ac80, 0x20001000, 0x20002000, 0x20000000, 0x20000008, 0x2000000c};
    const unsigned n5 = encode_n5_stub(0x10000000, o, a);
    check(n5 == n5_stub_length, "n5 length");
    for (unsigned i = 0; i < n5; ++i) std::printf("%02x", a[i]);
    std::printf("\n");
    const unsigned n6 = encode_n6_stub(0x10000200, 0x20000010, 0x0047f1b0, a);
    check(n6 == n6_stub_length, "n6 length");
    for (unsigned i = 0; i < n6; ++i) std::printf("%02x", a[i]);
    std::printf("\n");
    const unsigned n7 = encode_n7_stub(0x10000300, 0x20000014, 0x0060854c, 0x004e2535, a);
    check(n7 == n7_stub_length, "n7 length");
    for (unsigned i = 0; i < n7; ++i) std::printf("%02x", a[i]);
    std::printf("\n");
    const unsigned n8 = encode_n8_stub(0x10000400, 0x20000018, 0x004e2195, a);
    check(n8 == n8_stub_length && !std::memcmp(a + 6, n8_window, 5), "n8 length; the stub re-executes the three displaced instructions");
    for (unsigned i = 0; i < n8; ++i) std::printf("%02x", a[i]);
    std::printf("\n");
    unsigned char w[n7_window_length]; n7_expected(n7_hits_va, n7_mode_va, w);
    check(!std::memcmp(w, n7_window, n7_window_length), "n7 window with the engine operands is the pinned window");
    n7_expected(0x11223344, 0x55667788, w);
    check(w[0] == 0xa1 && w[1] == 0x44 && w[4] == 0x11 && w[10] == 0x88 && w[13] == 0x55 && w[14] == 0x00 && w[5] == 0x83, "n7 window operands");
    check(n5_site_va + call_length == n5_return_va && n7_site_va + call_length == n7_next_va && n8_site_va + call_length == n8_next_va && n5_callee[n5_callee_rel32 - 1] == 0xe8, "address relations");

    // Key, hashes and the comparison.
    static unsigned char object[0x100], physics[0x200];
    for (unsigned i = 0; i < sizeof object; ++i) object[i] = static_cast<unsigned char>(i * 7);
    for (unsigned i = 0; i < sizeof physics; ++i) physics[i] = static_cast<unsigned char>(i * 13 + 1);
    const ObjectKey k = read_key(0x1000, object, 0x2000, physics);
    check(k.object == 0x1000 && k.physics == 0x2000 && k.cls == load32(object + 0x48) % 65536 && k.subtype == (load32(object + 0x48) >> 16) && k.radius == std::int32_t(load32(object + 0xa4))
          && k.pos[0] == std::int32_t(load32(physics + 0x30)) && k.pos[2] == std::int32_t(load32(physics + 0x38)) && k.model == load32(physics + 0x140) && k.node_flags == load32(physics + 0x12c)
          && k.flags40 == load32(object + 0x40) && k.flags44 == load32(object + 0x44), "read_key fields");
    auto changed = [&](unsigned off) { unsigned char copy[0x200]; std::memcpy(copy, physics, sizeof copy); copy[off] ^= 1; return compare_key(read_key(0x1000, object, 0x2000, copy), k); };
    const std::uint8_t all = same_position | same_xform | same_saved;
    check(compare_key(k, k) == all, "identical keys");
    check(changed(0x30) == (all & ~same_position) && changed(0x3b) == (all & ~same_position) && changed(0x3c) == all, "position words 0x30..0x3b");
    check(changed(0xc0) == (all & ~same_xform) && changed(0xeb) == (all & ~same_xform) && changed(0xec) == all && changed(0x140) == (all & ~same_xform) && changed(0x12f) == (all & ~same_xform), "transform words 0xc0..0xeb, model id, node flags");
    check(changed(0xb0) == (all & ~same_saved) && changed(0xbf) == (all & ~same_saved) && changed(0xaf) == all && changed(0x180) == all, "saved position 0xb0..0xbf; contact scratch outside the key");

    // Memo annotation.
    static Entry then[4], now[5];
    for (unsigned i = 0; i < 4; ++i) { then[i].a = k; then[i].b = k; then[i].a.object = 10 + i; then[i].b.object = 20 + i; then[i].result = 0; then[i].visits = 100 * (i + 1); }
    for (unsigned i = 0; i < 4; ++i) now[i] = then[3 - i];                       // reversed order: the wrapped search still finds each
    now[4] = then[0]; now[4].a.object = 99;                                       // a new pair
    now[0].result = 1;                                                            // pair 3: same key, contact now
    now[1].a.pos[1] += 1;                                                         // pair 2: moved
    now[2].visits += 5;                                                           // pair 1: same key, other visit count
    then[0].result = 3;                                                           // pair 0: contact last frame
    const MemoSummary m = annotate(now, 5, then, 4);
    check(m.with_previous == 4 && m.unchanged == 3 && m.changed_position == 1 && m.changed_xform == 0 && m.changed_saved == 0 && m.memo_hits == 1 && m.memo_hit_visits == 205
          && m.memo_unsafe == 1 && m.visits_differ == 1, "memo summary");
    check(now[0].flags == (had_previous | all | unchanged | memo_unsafe) && now[1].flags == (had_previous | same_xform | same_saved)
          && now[2].flags == (had_previous | all | unchanged | memo_hit | visits_differ) && now[3].flags == (had_previous | all | unchanged) && now[4].flags == 0, "memo flags per entry");
    const MemoSummary none = annotate(now, 5, then, 0);
    check(none.with_previous == 0 && now[0].flags == 0, "no previous frame");
    // Ordering: visits descending, stable.
    static Entry e[6]; const std::uint32_t visits[6] = {5, 9, 5, 0, 9, 7}; std::uint16_t order[6];
    for (unsigned i = 0; i < 6; ++i) e[i].visits = visits[i];
    order_by_visits(e, 6, order);
    check(order[0] == 1 && order[1] == 4 && order[2] == 5 && order[3] == 0 && order[4] == 2 && order[5] == 3, "order by visits, stable");
    check(sizeof(Entry) <= 128 && ring_capacity == 256, "ring footprint");
    std::printf("collide_narrow_census_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''

WINDOW_LINE = ('collide_narrow frame=5399 frames=300 accepted_p50=7 accepted_max=9 accepted_sum=2100 mesh_pairs_p50=40 mesh_pairs_max=55 mesh_pairs_sum=12000 '
               'node_pairs_p50=61000 node_pairs_max=70000 node_pairs_sum=18300000 narrow_us_p50=21000 narrow_us_max=24000 narrow_us_sum=6300000 '
               'tri_tests_p50=1200 tri_tests_max=1900 tri_tests_sum=380000 '
               'recorded_sum=2100 with_previous_sum=2093 unchanged_sum=1800 memo_would_hit_sum=1794 memo_would_hit_permille=854 memo_visits_sum=18000000 memo_visits_permille=983 '
               'memo_unsafe_sum=0 memo_visits_differ_sum=0 changed_pos_sum=293 changed_xform_sum=120 changed_saved_sum=293 ring_overflow=0 dropped=0 deferred=0 nested=0 foreign=0 cross_thread_frames=0')
PAIR_LINE = ('collide_narrow_pair device=1 frame=5400 rank=0 of=7 a=0x0a1b2c30 b=0x0a1b4d10 class_a=5 class_b=7 subtype_a=12 subtype_b=301 model_a=4411 model_b=-1 '
             'radius_a=250000 radius_b=1200 flags40_a=0x00000001 flags44_a=0x00000003 flags40_b=0x01000000 flags44_b=0x00000001 node_flags_a=0x01000000 node_flags_b=0x01000000 '
             'pos_a=-1000,20,30 pos_b=500,-2147483648,30 d_max=2147483628 r_sum=251200 visits=60123 mesh_pairs=38 tri_tests=412 us=20950 result=0 contact=0 '
             'previous=1 same_pos=1 same_xform=1 same_saved=1 unchanged=1 memo_hit=1 memo_unsafe=0 visits_differ=0')


def load_manage():
    spec = importlib.util.spec_from_file_location('collide_narrow_census_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def patched_checks(data, changes, claims=None, sat=None):
    """The narrow checks on a copy of the installed image with bytes changed at the given VAs."""
    image = bytearray(data)
    for va, raw in changes:
        offset = va - 0x401000 + 0x400
        image[offset:offset + len(raw)] = raw
    narrow = probe.narrow_inputs(bytes(image))
    if claims is not None:
        narrow['claims'] = claims
    return probe.inspect(bytes(image), probe.decode(bytes(image)), source_text(probe.CORE),
                         probe.other_claims(), narrow, sat)


class NarrowSites(unittest.TestCase):
    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report)
        self.assertEqual(report['narrow_sites'], ['0x45d665', '0x48a9a5', '0x4e2530', '0x4e2190'])
        self.assertGreaterEqual(report['narrow_other_claims_checked'], 100)
        self.assertEqual(len([k for k in report['checks'] if k.startswith(('n5_', 'n6_', 'n7_', 'n8_', 'no_', 'narrow_'))]), 26)

    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_changed_bytes_branches_and_pointers_refused(self):
        data = probe.DEFAULT_EXE.read_bytes()
        rel = lambda at, target: b'\xe9' + struct.pack('<i', target - (at + 5))
        cases = {
            'n5_windows': [(probe.N5_RETURN + 2, b'\x0c')],                                  # add esp,0xc
            'n5_site_whole_call': [(probe.N5_SITE + 1, struct.pack('<i', 0x48a890 - probe.N5_RETURN))],
            'n5_pair_registers_live': [(0x45d626, b'\xbb')],                                 # mov ebx,0xa before the call
            'n5_callee_bytes': [(probe.N5_TARGET + 1, b'\x33\xc9')],                         # xor ecx,ecx: another register convention
            'n5_flags_dead_at_callee_entry': [(probe.N5_TARGET, b'\x9c')],                   # pushfd first
            'n5_callee_plain_ret': [(0x48ace6, b'\xc2')],
            'n5_flags_dead_at_return': [(probe.N5_RETURN, b'\x8d\x64\x24')],                 # lea: no flag write
            'n6_windows': [(probe.N6_SITE - 11, b'\xcb')],                                   # mov ecx,ebx
            'n6_callee_bytes': [(probe.N6_TARGET + 9, b'\x60')],
            'n7_window': [(probe.N7_SITE + 20, b'\x0f')],
            'n7_site_whole_mov': [(probe.N7_SITE, b'\x8b\x05')],
            'n8_window': [(probe.N8_SITE + 8, b'\x48')],                                     # mov eax,[esi+0x48]
            'n8_site_three_whole_instructions': [(probe.N8_SITE, b'\x81\xec\x34\x00\x00')],  # a longer first instruction
            'n8_sole_caller': [(0x4e2188, rel(0x4e2188, probe.N8_SITE))],
            'n8_flags_written_by_displaced_sub': [(probe.N8_SITE, b'\x8d\x64\x24')],       # lea esp,[esp+..]: no flag write
            'n7_inbound_calls': [(0x4e2188, rel(0x4e2188, probe.N7_SITE))],                  # a jump to the entry from elsewhere
            'no_rel32_into_spans': [(0x4e2188, rel(0x4e2188, probe.N7_SITE + 2))],
            'no_abs32_into_spans': [(0x45e0dc, struct.pack('<I', probe.N5_SITE + 3))],       # a jump-table slot into the span
            'no_short_jump_into_spans': [(0x48a9a1, b'\xeb\x05')],                           # jmp short 0x48a9a8
        }
        for failed, change in cases.items():
            with self.subTest(check=failed):
                report = patched_checks(data, change)
                self.assertFalse(report['checks'][failed], failed)
                self.assertEqual(report['result'], 'FAIL')

    def test_claims_and_overlap_detection(self):
        claims = probe.narrow_other_claims()
        addresses = {address for _, address, _ in claims}
        for expected in (probe.P1_SITE, probe.P2_SITE, probe.P1_COMPARE, 0x47d2a2, 0x47d258, 0x43a38e):
            self.assertIn(expected, addresses)
        self.assertNotIn(probe.N5_SITE, addresses)
        self.assertEqual(probe.narrow_overlaps(claims), [])
        for name, address in (('x', probe.N5_SITE + 4), ('y', probe.N6_SITE - 3), ('z', probe.N7_SITE + 30), ('w', probe.N5_TARGET + 50), ('v', probe.N6_TARGET)):
            self.assertEqual(probe.narrow_overlaps([(name, address, 5)]), [(name, hex(address))])
        self.assertEqual(probe.narrow_overlaps([('p', probe.N5_SITE - 13, 5), ('q', 0x47d2a2, 8), ('r', probe.P1_SITE, 26)]), [])
        # The box cull sees the census sites as foreign claims and stays clear of them.
        self.assertIn(probe.N5_SITE, {address for _, address, _ in probe.other_claims()})
        self.assertEqual(probe.overlaps(probe.other_claims()), [])

    def test_source_constants(self):
        self.assertEqual(probe.narrow_source_constants(source_text(probe.NARROW_CORE)), probe.NARROW_EXPECTED_CONSTANTS)

    def test_line_parsers(self):
        row = probe.parse_narrow_install_line('00:01 collide_narrow_census requested=1 patched=1 reason=ok n5_site=0x0045d665 n6_site=0x0048a9a5 n7_site=0x004e2530 '
                                              'write_n5=atomic write_n6=plain write_n7=atomic stub_n5=0x0a100000 stub_n6=0x0a100130 stub_n7=0x0a100140 ring=256 qpc_frequency=10000000 n8_site=0x004e2190 write_n8=plain stub_n8=0x0a100150')
        self.assertEqual((row['patched'], row['reason'], row['n7_site'], row['write_n6'], row['stub_n5'], row['ring'], row['qpc_frequency'], row['n8_site'], row['stub_n8']),
                         (True, 'ok', 0x4e2530, 'plain', 0x0a100000, 256, 10000000, 0x4e2190, 0x0a100150))
        self.assertIsNone(probe.parse_narrow_install_line('collide_narrow_census requested=1 patched=0 reason=bytes_mismatch'))
        window = probe.parse_narrow_window_line(WINDOW_LINE)
        self.assertEqual((window['frame'], window['accepted_p50'], window['node_pairs_sum'], window['tri_tests_max'], window['memo_would_hit_permille'], window['bounded']), (5399, 7, 18300000, 1900, 854, True))
        self.assertFalse(probe.parse_narrow_window_line(WINDOW_LINE.replace('ring_overflow=0', 'ring_overflow=3'))['bounded'])
        self.assertFalse(probe.parse_narrow_window_line(WINDOW_LINE.replace('memo_would_hit_sum=1794', 'memo_would_hit_sum=1801'))['bounded'])
        self.assertIsNone(probe.parse_narrow_window_line(WINDOW_LINE.replace(' deferred=0', '')))
        self.assertIsNone(probe.parse_narrow_window_line(PAIR_LINE))
        self.assertIsNone(probe.parse_window_line(WINDOW_LINE))
        pair = probe.parse_narrow_pair_line(PAIR_LINE)
        self.assertEqual((pair['a'], pair['class_b'], pair['model_b'], pair['pos_b'], pair['d_max'], pair['visits'], pair['tri_tests'], pair['memo_hit'], pair['bounded']),
                         (0x0a1b2c30, 7, -1, (500, -2147483648, 30), 2147483628, 60123, 412, 1, True))
        self.assertFalse(probe.parse_narrow_pair_line(PAIR_LINE.replace('d_max=2147483628', 'd_max=1500'))['bounded'])
        self.assertFalse(probe.parse_narrow_pair_line(PAIR_LINE.replace('result=0 contact=0', 'result=1 contact=1'))['bounded'])
        self.assertIsNone(probe.parse_narrow_pair_line(WINDOW_LINE))

    def test_core_compiled_and_encoder_parity(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-collide-narrow-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'narrow_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'), str(directory / 'harness.cpp'), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout[-2000:] + run.stderr)
            lines = run.stdout.splitlines()
            self.assertEqual(lines[-1], 'collide_narrow_census_core checks_failed=0')
            self.assertEqual(bytes.fromhex(lines[0]), probe.encode_n5_stub(0x10000000, probe.N5_RETURN, probe.N5_TARGET, 0x20001000, 0x20002000, 0x20000000, 0x20000008, 0x2000000c))
            self.assertEqual(bytes.fromhex(lines[1]), probe.encode_n6_stub(0x10000200, 0x20000010, probe.N6_TARGET))
            self.assertEqual(bytes.fromhex(lines[2]), probe.encode_n7_stub(0x10000300, 0x20000014, probe.N7_HITS, probe.N7_NEXT))
            self.assertEqual(bytes.fromhex(lines[3]), probe.encode_n8_stub(0x10000400, 0x20000018, probe.N8_NEXT))

    def test_encoders(self):
        at = 0x10000000
        stub = probe.encode_n5_stub(at, probe.N5_RETURN, probe.N5_TARGET, 0x20001000, 0x20002000, 0x20000000, 0x20000008, 0x2000000c)
        self.assertEqual(len(stub), probe.N5_STUB)
        self.assertEqual(stub[:9], b'\x81\x3c\x24' + struct.pack('<I', probe.N5_RETURN) + b'\x0f\x85')
        target = lambda field: (at + field + 4 + struct.unpack_from('<i', stub, field)[0]) & 0xffffffff
        calls = [i for i in range(len(stub) - 4) if stub[i] == 0xe8 and target(i + 1) in (0x20001000, probe.N5_TARGET, 0x20002000)]
        self.assertEqual([target(i + 1) for i in calls], [0x20001000, probe.N5_TARGET, 0x20002000])   # pre, the engine callee, post
        self.assertEqual(stub[calls[1] - 4:calls[1]], bytes.fromhex('8d642404'))                       # lea esp,[esp+4] right before the callee
        self.assertEqual(stub[-22:-16], b'\xff\x05' + struct.pack('<I', 0x20000008))
        self.assertEqual(stub[-11:-5], b'\xff\x05' + struct.pack('<I', 0x2000000c))
        self.assertEqual([target(len(stub) - 15), target(len(stub) - 4), target(len(stub) - 26)], [probe.N5_TARGET, probe.N5_TARGET, probe.N5_RETURN])
        self.assertEqual(at + 9 + 4 + struct.unpack_from('<i', stub, 9)[0], at + len(stub) - 11)       # jne foreign
        n7 = probe.encode_n7_stub(0x10000300, 0x20000014, probe.N7_HITS, probe.N7_NEXT)
        self.assertEqual(n7[:11], b'\xff\x05' + struct.pack('<I', 0x20000014) + b'\xa1' + struct.pack('<I', probe.N7_HITS))
        self.assertEqual((0x10000300 + 16 + struct.unpack('<i', n7[12:])[0]) & 0xffffffff, probe.N7_NEXT)
        with self.assertRaises(ValueError):
            probe.encode_n6_stub(1 << 32, 0, 0)

    def test_production_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertEqual(capture.count('collide_narrow_census::initialize();'), 1)
        self.assertEqual(capture.count('collide_narrow_census::present(ctx.id,ctx.frame,ctx.capture);'), 1)
        self.assertLess(capture.index('game_phases::initialize();'), capture.index('collide_narrow_census::initialize();'))
        self.assertIn('x3m::collide_narrow_census::shutdown();', source_text(ROOT / 'src/proxy/loader.cpp'))
        self.assertIn('src/proxy/collide_narrow_census.cpp', source_text(ROOT / 'CMakeLists.txt'))
        module = source_text(ROOT / 'src/proxy/collide_narrow_census.cpp')
        self.assertIn('L"X3M_COLLIDE_NARROW_CENSUS"', module)
        self.assertIn('length == 1 && setting[0] == L\'1\'', module)
        for needle in ('install_window_open()', 'executable_verified()', 'callee_mismatch', 'pin_self()', 'engine_patch::restore_call(n6_site_)', 'engine_patch::restore(n7_site_)', 'engine_patch::restore(n8_site_)', 'collide_narrow_census_n8',
                       'x3m::LightCallBoundary cpu;', 'force_align_arg_pointer'):
            self.assertIn(needle, module)
        self.assertNotIn('X3M_COLLIDE_BOX_CULL', module)   # independent of the box cull
        audit = source_text(ROOT / 'verification/probe/check_no_x87.py')
        self.assertIn("'_x3m_collide_narrow_pre', '_x3m_collide_narrow_post'", audit)


class NarrowLaunchOption(unittest.TestCase):
    def launch(self, directory, *args, inherited=None):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def test_absent_option_drops_the_variable_even_when_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, _ = self.launch(directory, inherited={'X3M_COLLIDE_NARROW_CENSUS': '1'})
            self.assertEqual(code, 0)
            self.assertNotIn('X3M_COLLIDE_NARROW_CENSUS', json.loads(output)['env'])

    def test_dry_run_carries_the_switch_independently_of_the_box_cull(self):
        # Part of --debug since the logging tiers (2026-09-26): the DLL reads X3M_COLLIDE_NARROW_CENSUS or X3M_DEBUG.
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--debug')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']}, {'X3M_DEBUG': '1'})
            both = json.loads(self.launch(directory, '--debug', '--collide-box-cull')[1])
            self.assertEqual({k: v for k, v in both['env'].items() if k not in baseline['env']}, {'X3M_DEBUG': '1', 'X3M_COLLIDE_BOX_CULL': '1'})
            code, _, error = self.launch(directory, '--collide-narrow-census')
            self.assertEqual(code, 2)
            self.assertIn('unrecognized arguments', error)
        self.assertIn('const bool group = log_tier::debug();', source_text(ROOT / 'src/proxy/collide_narrow_census.cpp'))


if __name__ == '__main__':
    unittest.main()
