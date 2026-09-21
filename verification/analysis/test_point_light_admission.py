"""Host checks of the point-light root-admission patch (src/proxy/point_light_admission_core.h).

The site verifier on a synthetic image (byte window, whole instructions, both
branch targets, interior-branch and changed-byte refusal), the source constants,
the site/detour encoders, the install log-line parser, the integer predicate
compiled from the core header with the host compiler over synthetic chains, and
the --point-light-root-admission launcher gate (--dry-run only, never a launch).
The installed executable is only read when present. No Wine, no game.
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
import verify_point_light_site as probe  # noqa: E402
from verification.analysis.test_chase_aim_sites import synthetic_image  # noqa: E402

HARNESS = r'''
#include "point_light_admission_core.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
using namespace x3m::point_light_admission::core;
struct Memory {
    std::map<std::uint32_t, unsigned char> bytes;
    std::vector<std::uint32_t> log;
    void word(std::uint32_t at, std::uint32_t v) { for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<unsigned char>(v >> (8 * i)); }
    void node(std::uint32_t at, std::uint32_t parent, std::int32_t scale, std::int32_t x, std::int32_t y, std::int32_t z) {
        word(at + parent_offset, parent); word(at + scale_offset, std::uint32_t(scale));
        word(at + position_offset, std::uint32_t(x)); word(at + position_offset + 4, std::uint32_t(y)); word(at + position_offset + 8, std::uint32_t(z));
    }
    void light(std::uint32_t at, std::int32_t range, std::int32_t x, std::int32_t y, std::int32_t z) { node(at, 0, 0, x, y, z); word(at + range_offset, std::uint32_t(range)); }
    bool read(std::uint32_t at, void* out, unsigned size) {
        log.push_back(at);
        for (unsigned i = 0; i < size; ++i) { auto it = bytes.find(at + i); if (it == bytes.end()) return false; static_cast<unsigned char*>(out)[i] = it->second; }
        return true;
    }
};
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    auto reader = [](Memory& m) { return [&m](std::uint32_t a, void* o, unsigned n) { return m.read(a, o, n); }; };
    const std::uint32_t L = 0x1000, N = 0x2000, R = 0x3000, M = 0x4000;
    { Memory m; m.light(L, 1000, 0, 0, 0); m.node(R, 0, 19536, 3461, 0, 0); m.node(N, R, 200, 1232, 0, 0);
      check(root_admission(N, L, reader(m)) == Outcome::admitted, "run-22 clamp admitted through the station root");
      check(m.log.size() == 6, "reads: two parent links, root scale+position, light range+position"); }
    { Memory m; m.light(L, 1000, 0, 0, 0); m.node(R, 0, 500, 81192, 0, 0); m.node(N, R, 200, 81192, 0, 0);
      check(root_admission(N, L, reader(m)) == Outcome::root_rejected, "outpost root rejected"); }
    { Memory m; m.light(L, 1000, 0, 0, 0); m.node(N, 0, 200, 1232, 0, 0);
      check(root_admission(N, L, reader(m)) == Outcome::node_is_root, "null parent: native rejection stands");
      check(m.log.size() == 1, "root node costs one read"); }
    { Memory m; m.light(L, 1000, 0, 0, 0); m.node(R, 0, 500, 900, 1200, 0); m.node(N, R, 200, 0, 0, 0);
      check(root_admission(N, L, reader(m)) == Outcome::admitted, "exact reach: sqrt(900^2+1200^2)=1500 == 1000+500 admits");
      m.node(R, 0, 499, 900, 1200, 0);
      check(root_admission(N, L, reader(m)) == Outcome::root_rejected, "one unit short rejects"); }
    { Memory m; m.light(L, 1000, 0, 0, 0); m.node(R, 0, -1001, 0, 0, 0); m.node(N, R, 200, 0, 0, 0);
      check(root_admission(N, L, reader(m)) == Outcome::reach_negative, "negative reach rejects");
      m.node(R, 0, -1000, 0, 0, 0);
      check(root_admission(N, L, reader(m)) == Outcome::admitted, "zero reach at zero distance admits"); }
    { Memory m; m.light(L, 0x7fffffff, 0, 0, 0); m.node(R, 0, 0x7fffffff, std::int32_t(0x80000000), std::int32_t(0x80000000), std::int32_t(0x80000000)); m.node(N, R, 200, 0, 0, 0);
      check(root_admission(N, L, reader(m)) == Outcome::admitted, "extreme values: 3*2^62 <= (2^32-2)^2 without overflow");
      m.light(L, 1, 0, 0, 0); m.node(R, 0, 1, std::int32_t(0x80000000), std::int32_t(0x80000000), std::int32_t(0x80000000));
      check(root_admission(N, L, reader(m)) == Outcome::root_rejected, "extreme deltas with a small reach reject"); }
    { Memory m; m.light(L, 1000, 0, 0, 0); m.node(M, N, 200, 0, 0, 0); m.node(N, M, 200, 0, 0, 0);
      check(root_admission(N, L, reader(m)) == Outcome::chain_cycle, "two-node cycle");
      m.node(N, N, 200, 0, 0, 0); check(root_admission(N, L, reader(m)) == Outcome::chain_cycle, "self parent");
      m.node(M, M, 200, 0, 0, 0); m.node(N, M, 200, 0, 0, 0); check(root_admission(N, L, reader(m)) == Outcome::chain_cycle, "parent self-loop"); }
    { Memory m; m.light(L, 1000, 0, 0, 0);
      for (unsigned depth = 1; depth <= 10; ++depth) {
          std::uint32_t prev = N;
          for (unsigned i = 1; i <= depth; ++i) { const std::uint32_t at = 0x10000 + i * 0x1000; m.node(prev, at, 200, 5000, 0, 0); prev = at; }
          m.node(prev, 0, 19536, 3461, 0, 0);
          const Outcome o = root_admission(N, L, reader(m));
          check(o == (depth + 1 <= max_hops ? Outcome::admitted : Outcome::chain_too_deep), "depth within max_hops-1 admits, beyond rejects");
      }
      m.node(N, 0x50000, 200, 0, 0, 0); m.node(0x50000, 0x51000, 200, 0, 0, 0); m.node(0x51000, 0x52000, 200, 0, 0, 0); m.node(0x52000, 0x50000, 200, 0, 0, 0);
      check(root_admission(N, L, reader(m)) == Outcome::chain_too_deep, "three-node cycle ends at the hop bound"); }
    { Memory m; m.light(L, 1000, 0, 0, 0); m.node(N, 0x7000, 200, 0, 0, 0);
      check(root_admission(N, L, reader(m)) == Outcome::chain_unreadable, "unreadable parent");
      m.word(0x7000 + parent_offset, 0); check(root_admission(N, L, reader(m)) == Outcome::root_unreadable, "unreadable root fields");
      m.node(0x7000, 0, 500, 0, 0, 0); m.bytes.erase(L + range_offset); check(root_admission(N, L, reader(m)) == Outcome::light_unreadable, "unreadable light"); }
    unsigned char site[site_length]; encode_site_patch(0x004c27af, 0x10000000, site);
    std::uint32_t rel = 0;
    std::memcpy(&rel, site + 1, 4); check(site[0] == 0xe9 && site[5] == 0x90 && 0x004c27b4 + rel == 0x10000000, "site patch encoding: JMP rel32 = detour - (site + 5); NOP");
    unsigned char d[detour_length]; encode_detour(0x10000000, 0x20000000, admit_va, reject_va, 0x30000000, d);
    std::memcpy(&rel, d + 2, 4); check(d[0] == 0x0f && d[1] == 0x8e && 0x10000006 + rel == 0x10000000 + detour_counted_offset, "JLE counted");
    check(d[6] == 0x50 && d[7] == 0x56 && d[8] == 0xff && d[9] == 0x75 && d[10] == 0x0c, "push eax; push esi; push [ebp+0xc]");
    std::memcpy(&rel, d + 12, 4); check(d[11] == 0xe8 && 0x10000010 + rel == 0x20000000, "call handler");
    check(d[16] == 0x83 && d[17] == 0xc4 && d[18] == 0x0c && d[19] == 0x85 && d[20] == 0xc0, "add esp,12; test eax,eax");
    std::memcpy(&rel, d + 23, 4); check(d[21] == 0x0f && d[22] == 0x85 && 0x1000001b + rel == admit_va, "JNZ admit");
    std::memcpy(&rel, d + 28, 4); check(d[27] == 0xe9 && 0x10000020 + rel == reject_va, "JMP reject");
    std::memcpy(&rel, d + 34, 4); check(d[32] == 0xff && d[33] == 0x05 && rel == 0x30000000, "inc dword [counter]");
    std::memcpy(&rel, d + 39, 4); check(d[38] == 0xe9 && 0x1000002b + rel == admit_va, "JMP admit after the count");
    check(isqrt64(0) == 0 && isqrt64(1) == 1 && isqrt64(3) == 1 && isqrt64(4) == 2 && isqrt64(3461ull * 3461ull) == 3461 && isqrt64(3461ull * 3461ull + 6921) == 3461 && isqrt64(3461ull * 3461ull + 6923) == 3462 && isqrt64(~0ull) == 0xffffffffu, "isqrt64");
    { Memory m; Detail det; m.light(L, 1000, 0, 0, 0); m.node(R, 0, 19536, 3461, 0, 0); m.node(N, R, 200, 1232, 0, 0);
      check(root_admission(N, L, reader(m), &det) == Outcome::admitted && det.root == R && det.depth == 1 && det.root_scale == 19536 && det.root_reach == 20536 && det.root_dist_sq == 3461ull * 3461ull, "detail of a walk");
      m.node(N, 0, 200, 1232, 0, 0);
      check(root_admission(N, L, reader(m), &det) == Outcome::node_is_root && det.root == 0 && det.depth == 0 && det.root_reach == 0, "detail of a root node is zero"); }
    check(std::strcmp(outcome_name(Outcome::admitted), "root_admit") == 0 && std::strcmp(outcome_name(Outcome::root_rejected), "root_reject") == 0 && std::strcmp(outcome_name(Outcome::chain_too_deep), "chain_too_deep") == 0, "outcome names");
    check(site_va + site_length == admit_va && admit_va + site_rel32 == reject_va && window_va + site_offset == site_va, "address relations");
    check(std::memcmp(expected_window + site_offset, expected_site, site_length) == 0, "site bytes inside the window");
    std::printf("point_light_admission_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('point_light_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def image(*changes):
    """A synthetic PE with the window, the reject target and the known incoming label inside the function range."""
    jcc = lambda at, opcode, target: (at, b'\x0f' + opcode + struct.pack('<i', target - (at + 6)))
    extra = [(probe.WINDOW_VA, probe.WINDOW), (probe.REJECT_VA, probe.REJECT_PREFIX),
             jcc(0x4c2737, b'\x85', probe.KNOWN_LABEL),
             # the loop's three slot-skip branches to the reject target (empty slot, LightDir_0/1 already bound)
             jcc(0x4c26f6, b'\x8c', probe.REJECT_VA), jcc(0x4c2713, b'\x84', probe.REJECT_VA), jcc(0x4c2723, b'\x84', probe.REJECT_VA),
             *changes]
    return synthetic_image(extra=extra, text_size=0x100000)


def inspect_image(data):
    return probe.inspect(data, probe.decode(data), probe.CORE.read_text())


class PointLightSite(unittest.TestCase):
    def test_synthetic_image_passes_all_but_identity(self):
        report = inspect_image(image())
        checks = {k: v for k, v in report['checks'].items() if k != 'exe_identity'}
        self.assertTrue(all(checks.values()), report)
        self.assertFalse(report['checks']['exe_identity'])
        self.assertEqual(report['result'], 'FAIL')
        self.assertEqual(report['incoming_window_branches'], [('0x4c2737', '0x4c27b7')])

    def test_changed_bytes_and_interior_branch_refused(self):
        cases = {
            'site_whole_instruction': (probe.SITE_VA + 1, b'\x8e'),          # jle instead of jg
            'window_bytes': (probe.WINDOW_VA + 1, b'\x87'),                   # sub eax,[edi+0x158]
            'reject_target_boundary': (probe.REJECT_VA + 3, b'\x58'),         # mov eax,[esp+0x58]
            'no_interior_branch': (0x4c2a40, b'\xe9' + struct.pack('<i', probe.SITE_VA + 2 - (0x4c2a40 + 5))),
        }
        for failed, change in cases.items():
            with self.subTest(check=failed):
                report = inspect_image(image(change))
                self.assertFalse(report['checks'][failed], report)
        report = inspect_image(image((probe.REJECT_VA + 4, b'\x8b\x15\x19\x85\x60\x00')))
        self.assertFalse(report['checks']['reject_target_boundary'])

    def test_source_constants(self):
        self.assertEqual(probe.source_constants(probe.CORE.read_text()), probe.EXPECTED_CONSTANTS)
        self.assertEqual(probe.WINDOW[probe.SITE_VA - probe.WINDOW_VA:][:6], bytes.fromhex('0f8f40020000'))
        self.assertEqual(struct.unpack('<i', probe.SITE[2:])[0], probe.REJECT_VA - (probe.SITE_VA + 6))

    def test_encoders(self):
        self.assertEqual(probe.encode_site_patch(probe.SITE_VA, probe.SITE_VA + 5), b'\xe9\x00\x00\x00\x00\x90')
        detour = probe.encode_detour(0x10000000, 0x20000000, probe.ADMIT_VA, probe.REJECT_VA, 0x30000000)
        self.assertEqual(len(detour), 43)
        self.assertEqual(detour[:2], b'\x0f\x8e')
        self.assertEqual(struct.unpack('<I', detour[2:6])[0], 32 - 6)                      # JLE to the counted branch at +32
        self.assertEqual(detour[6:11], b'\x50\x56\xff\x75\x0c')
        self.assertEqual(struct.unpack('<I', detour[12:16])[0], (0x20000000 - 0x10000010) & 0xffffffff)
        self.assertEqual(detour[16:21], b'\x83\xc4\x0c\x85\xc0')
        self.assertEqual(struct.unpack('<I', detour[23:27])[0], (probe.ADMIT_VA - 0x1000001b) & 0xffffffff)
        self.assertEqual(struct.unpack('<I', detour[28:32])[0], (probe.REJECT_VA - 0x10000020) & 0xffffffff)
        self.assertEqual(detour[32:34], b'\xff\x05')
        self.assertEqual(struct.unpack('<I', detour[34:38])[0], 0x30000000)
        self.assertEqual(struct.unpack('<I', detour[39:43])[0], (probe.ADMIT_VA - 0x1000002b) & 0xffffffff)
        with self.assertRaises(ValueError):
            probe.encode_site_patch(1 << 32, 0)

    def test_frame_and_node_line_parsers(self):
        line = ('12:00:00.000 point_light_admission_frame device=1 frame=5100 tests=11400 fast_admit=4200 reject=7200 walks=310 memo_hits=6890 '
                'root_admit=250 root_reject=40 chain_unreadable=0 chain_too_deep=0 chain_cycle=0 node_is_root=20 root_unreadable=0 light_unreadable=0 reach_negative=0 samples=64')
        row = probe.parse_frame_line(line)
        self.assertEqual((row['device'], row['frame'], row['tests'], row['fast_admit'], row['reject'], row['walks'], row['memo_hits'], row['samples']), (1, 5100, 11400, 4200, 7200, 310, 6890, 64))
        self.assertTrue(row['sums_ok'])
        self.assertFalse(probe.parse_frame_line(line.replace('walks=310', 'walks=311'))['sums_ok'])
        self.assertFalse(probe.parse_frame_line(line.replace('tests=11400', 'tests=11401'))['sums_ok'])
        self.assertIsNone(probe.parse_frame_line(line.replace(' samples=64', '')))
        node = probe.parse_node_line('point_light_node device=1 frame=5100 node=0f1a2b30 root=0f000010 depth=2 dist=1232 reach=1200 root_dist=3461 root_reach=20536 verdict=root_admit node_scale=200 root_scale=19536')
        self.assertEqual(node, {'device': 1, 'frame': 5100, 'node': 0x0f1a2b30, 'root': 0x0f000010, 'depth': 2, 'dist': 1232, 'reach': 1200, 'root_dist': 3461,
                                'root_reach': 20536, 'verdict': 'root_admit', 'node_scale': 200, 'root_scale': 19536})
        node = probe.parse_node_line('point_light_node device=1 frame=5100 node=0f1a2b30 root=00000000 depth=0 dist=5000 reach=1300 root_dist=0 root_reach=0 verdict=node_is_root node_scale=300 root_scale=0')
        self.assertEqual((node['root'], node['depth'], node['verdict']), (0, 0, 'node_is_root'))
        self.assertIsNone(probe.parse_node_line('point_light_admission_frame device=1 frame=5100'))

    def test_log_line_parser(self):
        row = probe.parse_log_line('00:00:01.234 point_light_root_admission requested=1 patched=1 reason=ok write=plain site=0x004c27af detour=0x0a1b2c3d handler=0x6a001234')
        self.assertEqual(row, {'requested': True, 'patched': True, 'reason': 'ok', 'write': 'plain', 'site': 0x4c27af, 'detour': 0x0a1b2c3d, 'handler': 0x6a001234})
        row = probe.parse_log_line('point_light_root_admission requested=1 patched=0 reason=bytes_mismatch write=none site=0x004c27af detour=0x00000000 handler=0x6a001234')
        self.assertEqual((row['patched'], row['reason'], row['detour']), (False, 'bytes_mismatch', 0))
        self.assertIsNone(probe.parse_log_line('point_light_root_admission requested=1 patched=1 reason=ok'))
        self.assertIsNone(probe.parse_log_line('lod_scale requested=2 applied=2 game_value=1 proxy_value=0.5 patched=1 reason=ok write=plain'))

    def test_core_predicate_compiled(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-point-light-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'point_light_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'point_light_admission_core checks_failed=0\n')

    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report)


class PointLightLaunchOption(unittest.TestCase):
    def launch(self, directory, *args, inherited=None):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
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
            code, output, _ = self.launch(directory, inherited={'X3M_POINT_LIGHT_ROOT_ADMISSION': '1'})
            self.assertEqual(code, 0)
            self.assertNotIn('X3M_POINT_LIGHT_ROOT_ADMISSION', json.loads(output)['env'])

    def test_dry_run_carries_the_switch(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--point-light-root-admission')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']}, {'X3M_POINT_LIGHT_ROOT_ADMISSION': '1'})


if __name__ == '__main__':
    unittest.main()
