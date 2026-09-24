"""Host checks of the cull-census trampolines (src/proxy/cull_census_core.h).

The site verifier on a synthetic image (both windows, whole instructions, the
flag writer/consumer next to each site, interior-branch, extra-source and
changed-byte refusal, the LOD-ladder pattern and the EBX/ESP/slot writer
sets), the source constants, the stub encoders, the install, frame and
per-node row parsers (with and without the ladder fields), the core
classification, the exit-site model-pointer guard, the ladder suffix and the
body-name slot mapping and suffix compiled with the host compiler, the tools/analysis/cull_census.py bucket table on a synthetic
log, and the --cull-census launcher gate (--dry-run only, never a launch). The
installed executable is only read when present. No Wine, no game.
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
import verify_cull_census_sites as probe  # noqa: E402
from verification.analysis.test_chase_aim_sites import synthetic_image  # noqa: E402

HARNESS = r'''
#include "cull_census_core.h"
#include <cstdio>
#include <cstring>
using namespace x3m::cull_census::core;
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    check(size_limit(4, false, 99) == 4 && size_limit(4, true, 9) == 9 && size_limit(4, true, 2) == 4 && size_limit(-3, true, 0) == 0 && size_limit(-3, false, 0) == -3, "size_limit: signed max with the parent only when present");
    {
        static TrackSlot table[track_size];
        std::int32_t from = -1; unsigned cap = 99;
        check(track_observe(table, 0x1000, 0x2000, 7, 0, 1, &from) == Observed::seeded && track_observe(table, 0x1000, 0x2000, 7, 0, 2, &from) == Observed::same
              && track_observe(table, 0x1000, 0x2000, 7, 2, 3, &from) == Observed::switched && from == 0, "track: seed, same, switch 0 -> 2");
        check(track_observe(table, 0x1000, 0x3000, 7, 1, 3, &from) == Observed::seeded && track_observe(table, 0x1000, 0x2000, 7, 1, 3, &from) == Observed::same,
              "track: another view is its own key; a second visit in the same frame is ignored");
        check(track_observe(table, 0x1000, 0x2000, 7, 1, 5, &from) == Observed::seeded && track_observe(table, 0x1000, 0x2000, 8, 0, 6, &from) == Observed::seeded,
              "track: a frame gap or a changed model re-seeds without a switch");
        static TrackSlot fresh[track_size];
        const std::uint32_t base = 0x00500000, h0 = track_hash(base, 0);
        std::uint32_t other = 1; while (track_hash(base, other) != h0) ++other;   // a colliding (node, view) key outside the 16 below
        unsigned seeded = 0; for (std::uint32_t n = 0; n < track_probe; ++n) seeded += track_observe(fresh, base + n, 0, 1, 0, 1, &from) == Observed::seeded;
        check(seeded == track_probe && track_observe(fresh, base, other, 1, 0, 1, &from) == Observed::untracked, "track: 16 colliding keys fill the probe window, the 17th is untracked");
        check(track_observe(fresh, base, other, 1, 0, 3, &from) == Observed::seeded && track_observe(fresh, base + 1, 0, 1, 0, 3, &from) == Observed::seeded,
              "track: a slot not seen on the previous frame is reused; the original keys are still found past it");
        check(parse_lod_switch_cap("16", 2, &cap) && cap == 16 && parse_lod_switch_cap("0", 1, &cap) && cap == 0 && !parse_lod_switch_cap("4097", 4, &cap)
              && !parse_lod_switch_cap("1x", 2, &cap) && !parse_lod_switch_cap("", 0, &cap), "parse_lod_switch_cap: digits, 0..4096");
        char text[12]; format_int(text, sizeof text, -2147483647 - 1); check(!std::strcmp(text, "-2147483648"), "format_int int32 minimum");
        format_int(text, sizeof text, 25); check(!std::strcmp(text, "25"), "format_int 25");
    }
    Entry e{}; e.exited = 1; e.flags_in = 0x1002; e.flags_out = 0x1002; e.measure = 5; e.limit = 0;
    check(classify(e) == Verdict::kept, "kept when the renderable bit survives");
    e.flags_out = 0x1000; e.limit = 8; check(classify(e) == Verdict::culled_size, "culled_size below a positive limit");
    e.limit = 5; check(classify(e) == Verdict::culled_other, "measure equal to the limit is not a size cull");
    e.limit = 0; e.measure = 0; check(classify(e) == Verdict::culled_min, "culled_min below 1 without the 0x4000000 flag");
    e.flags_in = 0x4001002; check(classify(e) == Verdict::culled_other, "0x4000000 keeps a degenerate node out of culled_min");
    e.flags_in = 0x1002; e.measure = 10; check(classify(e) == Verdict::culled_other, "cleared later (env-map or fade) is culled_other");
    e.exited = 0; check(classify(e) == Verdict::no_exit, "no exit recorded");
    e.limit = -5; e.measure = -9; e.exited = 1; check(classify(e) == Verdict::culled_min, "negative limit never culls by size");
    check(!std::strcmp(verdict_name(Verdict::kept), "kept") && !std::strcmp(verdict_name(Verdict::culled_size), "culled_size") && !std::strcmp(verdict_name(Verdict::no_exit), "no_exit"), "verdict names");
    unsigned char m[measure_stub_length]; encode_measure_stub(0x10000000, 0x20000000, 0x30000000, 0x1000002c, m);
    std::uint32_t v = 0;
    std::memcpy(&v, m + 2, 4); check(m[0] == 0x80 && m[1] == 0x3d && v == 0x20000000 && m[6] == 0 && m[7] == 0x74 && m[8] == measure_stub_continue - 9, "measure stub: cmp byte [enabled],0; je continue");
    check(m[9] == 0x50 && m[10] == 0x51 && m[11] == 0x52 && !std::memcmp(m + 12, "\xff\x74\x24\x34\xff\x74\x24\x20\xff\x74\x24\x40\x56\x57", 14), "measure stub: pushes (view, d, s, measure, node)");
    std::memcpy(&v, m + 27, 4); check(m[26] == 0xe8 && 0x1000001f + v == 0x30000000, "measure stub: call handler");
    std::memcpy(&v, m + 39, 4); check(!std::memcmp(m + 31, "\x83\xc4\x14\x5a\x59\x58\xff\x25", 8) && v == 0x1000002c, "measure stub: add esp,20; pops; jmp [next]");
    unsigned char x[exit_stub_length]; encode_exit_stub(0x10000100, 0x20000000, 0x30000100, 0x10000120, x);
    std::memcpy(&v, x + 2, 4); check(x[0] == 0x80 && x[1] == 0x3d && v == 0x20000000 && x[7] == 0x74 && x[8] == exit_stub_continue - 9 && x[9] == 0x50 && x[11] == 0x52, "exit stub: test, je, pushes");
    check(!std::memcmp(x + 12, "\xff\x74\x24\x1c\xff\x74\x24\x24\x53\x57", 10), "exit stub: pushes (d slot, model slot, ebx, node)");
    std::memcpy(&v, x + 23, 4); check(x[22] == 0xe8 && 0x1000011b + v == 0x30000100, "exit stub: call handler");
    std::memcpy(&v, x + 35, 4); check(!std::memcmp(x + 27, "\x83\xc4\x10\x5a\x59\x58\xff\x25", 8) && v == 0x10000120, "exit stub: add esp,16; pops; jmp [next]");
    check(exit_model_pointer(0x0a000000, 0x0a000000, 100000) == 0x0a000000, "model pointer: EBX equal to the model slot, not D");
    check(exit_model_pointer(100000, 0x0a000000, 100000) == 0 && exit_model_pointer(100000, 100000, 100000) == 0, "model pointer: EBX still D (culled at 0x0047d2e7) is refused");
    check(exit_model_pointer(0, 0, 5) == 0 && exit_model_pointer(0x0a000000, 0x0b000000, 5) == 0, "model pointer: null EBX or a stale slot is refused");
    std::uint32_t slot = 0;
    check(body_slot(0, 11000, 3, &slot) && slot == 0 && body_slot(999, 11000, 3, &slot) && slot == 999, "body slot: ids below 1000 are their slot");
    check(!body_slot(1000, 11000, 3, &slot) && !body_slot(8999, 11000, 3, &slot) && body_slot(9000, 11000, 3, &slot) && slot == 0 && body_slot(19999, 11000, 3, &slot) && slot == 10999, "body slot: 1000..8999 invalid, 9000..19999 -> id - 9000");
    check(body_slot(20000, 11000, 3, &slot) && slot == 11000 && body_slot(20002, 11000, 3, &slot) && slot == 11002 && !body_slot(20003, 11000, 3, &slot), "body slot: dynamic ids bounded by the dynamic count");
    check(!body_slot(-1, 11000, 3, &slot) && !body_slot(2147483647, 11000, 3, &slot) && !body_slot(20000, 11000, 0, &slot), "body slot: negative, huge and no dynamic slot refused");
    char body[body_suffix_size];
    body_default_name(43, body); check(!std::strcmp(body, "v\\00043"), "body default name v\\%05d");
    body_default_name(123456, body); check(!std::strcmp(body, "v\\123456"), "body default name wider than five digits");
    format_body(body, "stations\\docks\\argon_dock_center"); check(!std::strcmp(body, " body=stations\\docks\\argon_dock_center"), "body suffix: name as stored");
    format_body(body, nullptr); check(!std::strcmp(body, " body=-"), "body suffix: unknown");
    format_body(body, ""); check(!std::strcmp(body, " body=-"), "body suffix: empty name");
    format_body(body, "a b\tc\x7f\x80"); check(!std::strcmp(body, " body=a?b?c??"), "body suffix: space, control and high bytes sanitised");
    char longest[200]; std::memset(longest, 'x', sizeof longest - 1); longest[sizeof longest - 1] = 0;
    format_body(body, longest); check(std::strlen(body) == 6 + body_name_cap && body_name_cap == 63, "body suffix: capped at 63 characters");
    check(body_global_va == 0x00608518 && body_fixed_count_offset == 0xb4 && body_dynamic_count_offset == 0xb8 && body_slots_offset == 0xbc
          && body_slot_stride == 0x1c && body_slot_name_offset == 0x0c && body_fixed_count == 11000, "body table constants");
    char text[128];
    const std::int32_t thr[ladder_cap] = {900, 100, 50, 25, -1, 7, 8, 9};
    check(format_ladder(text, sizeof text, true, 3, 3, thr) && !std::strcmp(text, " lods=3 thr=900,100,50"), "ladder suffix: three records");
    check(format_ladder(text, sizeof text, false, 3, 3, thr) && !std::strcmp(text, " lods=- thr=-"), "ladder suffix: no model pointer");
    check(format_ladder(text, sizeof text, true, 0, 0, thr) && !std::strcmp(text, " lods=0 thr=-"), "ladder suffix: count 0 has no thresholds");
    check(format_ladder(text, sizeof text, true, 4, 0, thr) && !std::strcmp(text, " lods=4 thr=-"), "ladder suffix: unreadable record keeps the count");
    check(format_ladder(text, sizeof text, true, -2, 0, thr) && !std::strcmp(text, " lods=-2 thr=-"), "ladder suffix: a negative count word is shown as read");
    check(format_ladder(text, sizeof text, true, 12, 8, thr) && !std::strcmp(text, " lods=12 thr=900,100,50,25,-1,7,8,9"), "ladder suffix: capped at eight records");
    const std::int32_t extreme[ladder_cap] = {-2147483647 - 1, 2147483647, 0, 0, 0, 0, 0, 0};
    check(format_ladder(text, sizeof text, true, 2, 2, extreme) && !std::strcmp(text, " lods=2 thr=-2147483648,2147483647"), "ladder suffix: int32 extremes");
    check(!format_ladder(text, 10, true, 3, 3, thr) && std::strlen(text) == 9, "ladder suffix: truncation reported, terminated");
    check(std::memcmp(measure_window + measure_site_offset, measure_site, site_length) == 0 && std::memcmp(exit_window + exit_site_offset, exit_site, site_length) == 0, "site bytes inside the windows");
    check(measure_window_va + measure_site_offset == measure_site_va && measure_site_va + site_length == measure_next_va && exit_window_va + exit_site_offset == exit_site_va && exit_site_va + site_length == exit_next_va, "address relations");
    check(sizeof(Entry) == 72 && ring_size == 8192, "entry size and ring bound (the parent link, model pointer, +0x130 and flag31 included)");
    std::printf("cull_census_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''

SYNTHETIC_LOG = '''x3-modern-renderer version=0.4 schema=2 capture_start=0 capture_frames=0 pointer_bits=32
cull_census requested=1 patched=1 reason=ok measure_site=0x0047d258 exit_site=0x0047d528 write_measure=atomic write_exit=plain stub_measure=0x0a100000 stub_exit=0x0a100040 ring=8192
surface role=rt0 ptr=041e1f28 identity=1 width=1280 height=768 format=21 usage=1 msaa=0 container=0 container_type=0 container_result=80004002
frame_begin device=1 frame=3494
object_context device=1 frame=3494 index=1 scoped=1 valid=127 session=1 scope_depth=1 mesh=3f8b2798 node=34762be8 node_handle=58245 camera=34766bf8 camera_handle=60312 registry=0395f0c0 engine=03977168 model=000050e8 lod=00000000 flags12c=00001002 flags130=00600000
object_matrix role=projection row=0 bits=3f4ccccc,00000000,00000000,00000000
draw device=1 frame=3494 index=1 kind=indexed topology=4 primitives=768 vs=7b6393fe2d3e1d85 ps=6109cf64c03529dd
object_context device=1 frame=3494 index=2 scoped=1 valid=127 session=1 scope_depth=1 mesh=3f8b2798 node=34762be8 node_handle=58245 camera=34766bf8 camera_handle=60312 registry=0395f0c0 engine=03977168 model=000050e8 lod=00000000 flags12c=00001002 flags130=00600000
draw device=1 frame=3494 index=2 kind=indexed topology=4 primitives=32 vs=7b6393fe2d3e1d85 ps=6109cf64c03529dd
object_context device=1 frame=3494 index=3 scoped=1 valid=127 session=1 scope_depth=1 mesh=3f8b2798 node=34763000 node_handle=58246 camera=34766bf8 camera_handle=60312 registry=0395f0c0 engine=03977168 model=000050e9 lod=00000002 flags12c=00001002 flags130=00600000
draw device=1 frame=3494 index=3 kind=indexed topology=4 primitives=100 vs=7b6393fe2d3e1d85 ps=6109cf64c03529dd
object_context device=1 frame=3494 index=4 scoped=1 valid=127 session=1 scope_depth=1 mesh=3f8b2798 node=34769999 node_handle=58247 camera=34766bf8 camera_handle=60312 registry=0395f0c0 engine=03977168 model=000050ea lod=00000000 flags12c=00001002 flags130=00600000
draw device=1 frame=3494 index=4 kind=indexed topology=4 primitives=7 vs=7b6393fe2d3e1d85 ps=6109cf64c03529dd
cull_census_frame device=1 frame=3494 entries=5 overflow=0 unmeasured=3 exited=5 ring=8192
cull_census device=1 frame=3494 view=34766bf8 node=34762be8 model=000050e8 s=128 measure=256 d=100000 radius=20000 thr_1dc=0 thr_1d8=0 limit=0 flags_in=00001002 flags_out=00001002 lod=0 verdict=kept
cull_census device=1 frame=3494 view=34766bf8 node=34763000 model=000050e9 s=3 measure=6 d=100000 radius=500 thr_1dc=0 thr_1d8=0 limit=0 flags_in=00001002 flags_out=00001002 lod=2 verdict=kept
cull_census device=1 frame=3494 view=34766bf8 node=34763100 model=000050eb s=1 measure=1 d=100000 radius=100 thr_1dc=0 thr_1d8=4 limit=4 flags_in=00001002 flags_out=00001000 lod=0 verdict=culled_size
cull_census device=1 frame=3494 view=34766bf8 node=34763200 model=000050ec s=9 measure=18 d=100000 radius=1400 thr_1dc=0 thr_1d8=0 limit=0 flags_in=00009002 flags_out=00009000 lod=2 verdict=culled_other
cull_census device=1 frame=3494 view=01000000 node=34762be8 model=000050e8 s=128 measure=256 d=100000 radius=20000 thr_1dc=0 thr_1d8=0 limit=0 flags_in=00001002 flags_out=00001002 lod=1 verdict=kept
frame_end device=1 frame=3494 draws=4 capture=1 present=00000000 elapsed_ms=3602 dt_ms=33 qpc=14300437161427
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('cull_census_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_summariser():
    spec = importlib.util.spec_from_file_location('cull_census_summary', ROOT / 'tools/analysis/cull_census.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def image(*changes):
    """A synthetic PE with the pass's two windows, its documented incoming branches and the final ret 8."""
    rel32 = lambda at, opcode, target: (at, opcode + struct.pack('<i', target - (at + len(opcode) + 4)))
    extra = [(probe.MEASURE_WINDOW_VA, probe.MEASURE_WINDOW), (probe.EXIT_WINDOW_VA, probe.EXIT_WINDOW),
             (0x47d231, b'\xeb' + bytes([probe.MEASURE_SITE_VA - 0x47d233])),
             rel32(0x47d085, b'\x0f\x84', probe.EXIT_SITE_VA), rel32(0x47d0a4, b'\xe9', probe.EXIT_SITE_VA), rel32(0x47d0ea, b'\xe9', probe.EXIT_SITE_VA),
             rel32(0x47d112, b'\xe9', probe.EXIT_SITE_VA), rel32(0x47d1a2, b'\x0f\x8c', probe.EXIT_SITE_VA), rel32(0x47d1af, b'\x0f\x84', probe.EXIT_SITE_VA),
             rel32(0x47d2e7, b'\xe9', probe.EXIT_SITE_VA),
             (probe.RET_VA, b'\xc2\x08\x00'),
             *((va, bytes.fromhex(h)) for va, h in probe.LADDER_PATTERN),
             *changes]
    return synthetic_image(extra=extra, text_size=0x100000)


def inspect_image(data):
    return probe.inspect(data, probe.decode(data), probe.CORE.read_text())


class CullCensusSites(unittest.TestCase):
    def test_synthetic_image_passes_all_but_identity(self):
        report = inspect_image(image())
        checks = {k: v for k, v in report['checks'].items() if k != 'exe_identity'}
        self.assertTrue(all(checks.values()), report)
        self.assertFalse(report['checks']['exe_identity'])
        self.assertEqual(report['result'], 'FAIL')
        self.assertEqual(report['measure_sources'], ['0x47d231', '0x47d24e'])
        self.assertEqual(len(report['exit_sources']), 8)
        self.assertEqual(report['interior_branches'], [])

    def test_changed_bytes_and_branches_refused(self):
        cases = {
            'measure_window_bytes': (probe.MEASURE_WINDOW_VA + 3, b'\x28'),                  # mov [esp+0x28],eax
            'measure_site_whole_instruction': (probe.MEASURE_SITE_VA + 2, b'\xd8'),          # mov eax,[edi+0x1d8]
            'measure_next_writes_flags': (probe.MEASURE_NEXT_VA, b'\x8b\xc0'),                # mov eax,eax instead of test
            'exit_window_bytes': (probe.EXIT_WINDOW_VA + 2, b'\x02'),                        # cmp ecx,2
            'exit_site_whole_instructions': (probe.EXIT_SITE_VA + 3, b'\x83\x3f\x01'),       # cmp [edi],1
            'exit_next_consumes_flags': (probe.EXIT_NEXT_VA, b'\xeb\x18'),                   # jmp instead of je
            'no_interior_branch': (0x47d380, b'\xe9' + struct.pack('<i', probe.MEASURE_SITE_VA + 2 - (0x47d380 + 5))),
            'exit_sources': (0x47d400, b'\xe9' + struct.pack('<i', probe.EXIT_SITE_VA - (0x47d400 + 5))),
            'function_ret': (probe.RET_VA, b'\xc2\x04\x00'),
            'ladder_pattern_bytes': (0x47d321, b'\x0f\xbf\x6b\x12'),                      # movsx ebp,word [ebx+0x12]
            'ebx_writers_between_sites': (0x47d480, b'\x8b\xd8'),                          # an extra mov ebx,eax on the LOD path
            'esp_writers_between_sites': (0x47d480, b'\x50'),                               # an unbalanced push between the sites
            'slot_writers': (0x47d480, b'\x89\x44\x24\x14'),                              # mov [esp+0x14],eax
        }
        for failed, change in cases.items():
            with self.subTest(check=failed):
                report = inspect_image(image(change))
                self.assertFalse(report['checks'][failed], report)
        report = inspect_image(image((0x47d400, b'\xe9' + struct.pack('<i', probe.EXIT_SITE_VA + 3 - (0x47d400 + 5)))))
        self.assertFalse(report['checks']['no_interior_branch'])  # a branch onto the displaced cmp

    def test_source_constants(self):
        self.assertEqual(probe.source_constants(probe.CORE.read_text()), probe.EXPECTED_CONSTANTS)
        self.assertEqual(probe.MEASURE_SITE, bytes.fromhex('8b87dc010000'))
        self.assertEqual(probe.EXIT_SITE, bytes.fromhex('8b7f0c833f00'))
        self.assertEqual(len(probe.MEASURE_WINDOW), 31)
        self.assertEqual(len(probe.EXIT_WINDOW), 27)

    def test_encoders(self):
        stub = probe.encode_measure_stub(0x10000000, 0x20000000, 0x30000000, 0x1000002c)
        self.assertEqual(len(stub), 43)
        self.assertEqual(stub[:2], b'\x80\x3d')
        self.assertEqual(struct.unpack('<I', stub[2:6])[0], 0x20000000)
        self.assertEqual(stub[6:9], b'\x00\x74\x1c')
        self.assertEqual(stub[9:26], b'\x50\x51\x52\xff\x74\x24\x34\xff\x74\x24\x20\xff\x74\x24\x40\x56\x57')
        self.assertEqual(struct.unpack('<I', stub[27:31])[0], (0x30000000 - 0x1000001f) & 0xffffffff)
        self.assertEqual(stub[31:39], b'\x83\xc4\x14\x5a\x59\x58\xff\x25')
        self.assertEqual(struct.unpack('<I', stub[39:43])[0], 0x1000002c)
        exit_stub = probe.encode_exit_stub(0x10000100, 0x20000000, 0x30000100, 0x10000120)
        self.assertEqual(len(exit_stub), 39)
        self.assertEqual(exit_stub[6:22], b'\x00\x74\x18\x50\x51\x52\xff\x74\x24\x1c\xff\x74\x24\x24\x53\x57')
        self.assertEqual(struct.unpack('<I', exit_stub[23:27])[0], (0x30000100 - 0x1000011b) & 0xffffffff)
        self.assertEqual(exit_stub[27:35], b'\x83\xc4\x10\x5a\x59\x58\xff\x25')
        self.assertEqual(struct.unpack('<I', exit_stub[35:39])[0], 0x10000120)
        with self.assertRaises(ValueError):
            probe.encode_measure_stub(1 << 32, 0, 0, 0)

    def test_line_parsers(self):
        row = probe.parse_log_line('00:00:01.234 cull_census requested=1 patched=1 reason=ok measure_site=0x0047d258 exit_site=0x0047d528 write_measure=atomic write_exit=plain stub_measure=0x0a100000 stub_exit=0x0a100040 ring=8192')
        self.assertEqual(row, {'requested': True, 'patched': True, 'reason': 'ok', 'measure_site': 0x47d258, 'exit_site': 0x47d528, 'write_measure': 'atomic',
                               'write_exit': 'plain', 'stub_measure': 0x0a100000, 'stub_exit': 0x0a100040, 'ring': 8192})
        self.assertIsNone(probe.parse_log_line('cull_census requested=1 patched=0 reason=bytes_mismatch'))
        self.assertIsNone(probe.parse_log_line('point_light_root_admission requested=1 patched=1 reason=ok write=plain site=0x004c27af detour=0x0a1b2c3d handler=0x6a001234'))
        frame = probe.parse_frame_line('cull_census_frame device=1 frame=3494 entries=8192 overflow=17 unmeasured=3 exited=8192 ring=8192')
        self.assertEqual((frame['entries'], frame['overflow'], frame['unmeasured'], frame['exited'], frame['bounded']), (8192, 17, 3, 8192, True))
        self.assertFalse(probe.parse_frame_line('cull_census_frame device=1 frame=1 entries=8193 overflow=0 unmeasured=0 exited=0 ring=8192')['bounded'])
        self.assertIsNone(probe.parse_frame_line('cull_census_frame device=1 frame=1 entries=1'))
        node = probe.parse_row('cull_census device=1 frame=3494 view=34766bf8 node=34763100 model=000050eb s=1 measure=1 d=100000 radius=100 thr_1dc=0 thr_1d8=4 limit=4 flags_in=00001002 flags_out=00001000 lod=0 verdict=culled_size')
        self.assertEqual(node, {'device': 1, 'frame': 3494, 'view': 0x34766bf8, 'node': 0x34763100, 'model': 0x50eb, 's': 1, 'measure': 1, 'd': 100000, 'radius': 100,
                                'thr_1dc': 0, 'thr_1d8': 4, 'limit': 4, 'flags_in': 0x1002, 'flags_out': 0x1000, 'lod': 0, 'verdict': 'culled_size'})
        prefix = 'cull_census device=1 frame=3494 view=34766bf8 node=34763000 model=000050e9 s=3 measure=6 d=100000 radius=500 thr_1dc=0 thr_1d8=0 limit=0 flags_in=00001002 flags_out=00001002 lod=0 '
        laddered = probe.parse_row(prefix + 'verdict=kept lods=3 thr=900,100,50')
        self.assertEqual((laddered['verdict'], laddered['lods'], laddered['thr']), ('kept', 3, [900, 100, 50]))
        self.assertEqual({k: probe.parse_row(prefix + 'verdict=kept lods=- thr=-')[k] for k in ('lods', 'thr')}, {'lods': None, 'thr': []})
        self.assertEqual({k: probe.parse_row(prefix + 'verdict=kept lods=0 thr=-')[k] for k in ('lods', 'thr')}, {'lods': 0, 'thr': []})
        scoped = probe.parse_row(prefix + 'verdict=culled_small scope=bodies lods=1 thr=-5')
        self.assertEqual((scoped['verdict'], scoped['lods'], scoped['thr']), ('culled_small', 1, [-5]))
        self.assertNotIn('lods', node)   # a row written before the ladder fields carries neither key
        self.assertNotIn('body', laddered)   # nor a row written before the body field
        named = probe.parse_row(prefix + 'verdict=kept lods=3 thr=900,100,50 body=stations\\docks\\argon_dock_center')
        self.assertEqual((named['lods'], named['thr'], named['body']), (3, [900, 100, 50], 'stations\\docks\\argon_dock_center'))
        self.assertIsNone(probe.parse_row(prefix + 'verdict=culled_size scope=all lods=- thr=- body=-')['body'])
        self.assertEqual(probe.parse_row(prefix + 'verdict=kept lods=- thr=- body=v\\00043')['body'], 'v\\00043')
        self.assertIsNone(probe.parse_row('cull_census_frame device=1 frame=3494 entries=5 overflow=0 unmeasured=3 exited=5 ring=8192'))
        self.assertIsNone(probe.parse_row('cull_census requested=1 patched=1 reason=ok'))

    def test_core_compiled(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-cull-census-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'cull_census_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'cull_census_core checks_failed=0\n')

    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report)


class CullCensusSummary(unittest.TestCase):
    def test_bucket_table_and_join(self):
        summary = load_summariser()
        parsed = summary.parse(SYNTHETIC_LOG.splitlines())
        self.assertEqual(parsed['width'], 1280)
        self.assertAlmostEqual(parsed['m00'], 0.8, places=6)
        self.assertEqual(parsed['frames'][3494]['unmeasured'], 3)
        self.assertEqual(len(parsed['rows'][3494]), 5)
        result = summary.summarize(parsed)
        frame = result['frames'][3494]
        self.assertEqual(frame['view'], 0x34766bf8)              # the view with the most rows, not the env-map view
        self.assertEqual(frame['nodes'], 4)
        self.assertAlmostEqual(frame['px_per_s'], 0.8, places=6)
        self.assertEqual((frame['draws'], frame['joined_draws']), (4, 3))   # node 34769999 has no census row
        table = {c['bucket']: c for c in frame['table']}
        self.assertEqual((table['<1']['nodes'], table['1-2']['nodes'], table['2-4']['nodes'], table['8-16']['nodes'], table['>16']['nodes']), (0, 1, 1, 1, 1))
        self.assertEqual((table['1-2']['culled'], table['8-16']['culled'], table['>16']['kept']), (1, 1, 1))
        self.assertEqual((table['2-4']['draws'], table['2-4']['tris'], table['>16']['draws'], table['>16']['tris']), (1, 100, 2, 800))
        px = {c['bucket']: c for c in frame['px_table']}
        self.assertEqual((px['<1']['nodes'], px['2-4']['nodes'], px['4-8']['nodes'], px['>16']['nodes']), (1, 1, 1, 1))   # s=1 -> 0.8 px, s=3 -> 2.4, s=9 -> 7.2, s=128 -> 102.4
        self.assertAlmostEqual(px['>16']['ms'], 2 * 23.7 / 1000.0, places=9)
        self.assertEqual(frame['savings']['under_2px']['draws'], 0)
        self.assertEqual(frame['savings']['under_4px']['draws'], 1)
        self.assertAlmostEqual(frame['savings']['under_4px']['ms'], 0.0237, places=9)
        self.assertEqual(result['average']['frames'], 1)
        text = summary.render(result)
        self.assertIn('frame 3494: view=34766bf8 nodes=4', text)
        self.assertIn('| >16 | 1 | 1 | 0 | 2 | 800 | 0.047 |', text)
        self.assertIn('under_4px: 1 draws (0.024 ms)', text)

    def test_model_join_names_flags_radius_class_and_scope(self):
        summary = load_summariser()
        extra = ('cull_census device=1 frame=3494 view=34766bf8 node=34763300 model=000050ed s=2 measure=4 d=2600000 radius=30000 thr_1dc=0 thr_1d8=0 limit=0 '
                 'flags_in=01001002 flags_out=01001000 lod=0 verdict=culled_small scope=bodies\n')
        parsed = summary.parse((SYNTHETIC_LOG + extra).splitlines())
        self.assertEqual(parsed['rows'][3494][-1]['scope'], 'bodies')
        self.assertEqual(parsed['rows'][3494][0]['flags_in'], 0x1002)
        frame = summary.summarize(parsed)['frames'][3494]
        self.assertEqual(frame['bodies_px'], 4.0)
        models = {m['model']: m for m in frame['models']}
        self.assertEqual(set(models), {0x50e9, 0x50ed})            # s=3 kept (2.4 px) and the culled_small body; culled_size/other rows are the engine's
        self.assertEqual((models[0x50ed]['body_flags'], models[0x50ed]['radius_class'], models[0x50ed]['verdict'], models[0x50ed]['scope'], models[0x50ed]['draws']), (0x1000000, '>20k', 'culled_small', 'bodies', 0))
        self.assertEqual((models[0x50e9]['body_flags'], models[0x50e9]['radius_class'], models[0x50e9]['draws'], models[0x50e9]['scope']), (0, '<1k', 1, None))
        text = summary.render(summary.summarize(parsed))
        self.assertIn('| 000050ed | 01000000 | >20k | 30000 | 2600000-2600000 | 1 | 0 | culled_small | bodies |', text)
        self.assertIn('no model -> object-type table in the repository', text)
        self.assertEqual(summary.summarize(parsed, bodies_px=2.0)['frames'][3494]['models'][0]['model'], 0x50ed)

    def test_focus_factor_matches_the_cull(self):
        # A default --fov launch (F = 0x3470): the summariser scales px per s by F/0x4000 exactly as cull_small_parts_core.h does,
        # taking F from the frame's projection rows (P[5] at 5120x1440: m00 0.5, m11 1.7778), else the value row, else 0x4000.
        import struct as _struct
        import verify_cull_small_parts_site as small
        summary = load_summariser()
        bits = lambda v: _struct.unpack('<I', _struct.pack('<f', v))[0]
        m11 = 1.7777636
        log = SYNTHETIC_LOG.replace('object_matrix role=projection row=0 bits=3f4ccccc,',
                                    f'object_matrix role=projection row=0 bits={bits(0.5):08x},00000000,00000000,00000000\n'
                                    f'object_matrix role=projection row=1 bits=00000000,{bits(m11):08x},')
        frame = summary.summarize(summary.parse(log.splitlines()), width=5120)['frames'][3494]
        self.assertEqual(frame['focus'], 0x3470)
        self.assertEqual(summary.focus_from_projection(0.5, m11), small.focus_from_projection(0.5, m11))
        self.assertAlmostEqual(frame['px_per_s'], 0.5 * 5120 / 1280 * 0x3470 / 0x4000, places=5)
        # The class the cull removes at 2 px is exactly the summariser's s * px_per_s < 2 rows.
        t = small.threshold_for(2, 0.5, 5120, 0x3470)
        self.assertEqual(t, 2)
        self.assertTrue(all((s < t) == (s * frame['px_per_s'] < 2) for s in range(0, 64)))
        # No projection row 1: the logged focus of the value row; neither: 0x4000 (the synthetic log above, unchanged).
        with_value = SYNTHETIC_LOG + 'cull_small_parts_value px=2 m00=0.5 width=5120 threshold=2 focus=0x3470\n'
        self.assertEqual(summary.summarize(summary.parse(with_value.splitlines()))['frames'][3494]['focus'], 0x3470)
        self.assertEqual(summary.summarize(summary.parse(SYNTHETIC_LOG.splitlines()))['frames'][3494]['focus'], 0x4000)
        self.assertAlmostEqual(summary.summarize(summary.parse(SYNTHETIC_LOG.splitlines()), focus=0x2000)['frames'][3494]['px_per_s'], 0.4, places=6)
        self.assertIn('focus=0x3470', summary.render(summary.summarize(summary.parse(with_value.splitlines()))))
        # The frame's own cull_small_parts_frame focus (the F the cull applied) wins over the last value row and the projection rows;
        # a frame without one falls back to the value row.
        frame_row = ('cull_small_parts_frame device=1 frame=3494 px=4 threshold=3 culled=12 m00=0.3147 width=5120 scope=all projectiles=on '
                     'exempt_bullet=0 focus=0x471c source=scene\n')
        with_frame = with_value + frame_row
        self.assertEqual(summary.summarize(summary.parse(with_frame.splitlines()))['frames'][3494]['focus'], 0x471c)
        self.assertEqual(summary.summarize(summary.parse((log + frame_row).splitlines()), width=5120)['frames'][3494]['focus'], 0x471c)
        other_frame = with_value + frame_row.replace('frame=3494', 'frame=3495')
        self.assertEqual(summary.summarize(summary.parse(other_frame.splitlines()))['frames'][3494]['focus'], 0x3470)
        self.assertEqual(summary.summarize(summary.parse(with_frame.splitlines()), focus=0x2000)['frames'][3494]['focus'], 0x2000)
        # run309's vanilla projection (m00 0.3750374, m11 1.333461) is 16383.0 unrounded: snapped to the default like the core.
        self.assertEqual((summary.focus_from_projection(0.3750374, 1.333461), small.focus_from_projection(0.3750374, 1.333461)), (0x4000, 0x4000))
        self.assertEqual((summary.focus_from_projection(0.3685, 1.3103), small.focus_from_projection(0.3685, 1.3103)), (0x40b6, 0x40b6))

    def test_view_and_frame_selection(self):
        summary = load_summariser()
        parsed = summary.parse(SYNTHETIC_LOG.splitlines())
        env = summary.summarize(parsed, view=0x01000000, m00=1.0, us_per_draw=10.0)
        self.assertEqual(env['frames'][3494]['nodes'], 1)
        self.assertEqual({c['bucket']: c['draws'] for c in env['frames'][3494]['px_table']}['>16'], 2)
        self.assertAlmostEqual(env['frames'][3494]['px_table'][-1]['ms'], 0.02, places=9)
        self.assertEqual(summary.summarize(parsed, frames=(1, 2))['frames'], {})
        self.assertEqual(summary.parse_frames('3494-3501'), (3494, 3501))
        self.assertEqual(summary.parse_frames('7'), (7, 7))
        self.assertEqual(summary.bucket_of(0.5), 0)
        self.assertEqual(summary.bucket_of(16), 5)
        self.assertEqual(summary.bucket_of(15.99), 4)

    def test_main_json(self):
        summary = load_summariser()
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'session.log'
            log.write_text(SYNTHETIC_LOG)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                code = summary.main([str(log), '--json', '--frames', '3494'])
            self.assertEqual(code, 0)
            result = json.loads(output.getvalue())
            self.assertEqual(result['frames']['3494']['nodes'], 4)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                summary.main([str(log), '--frames', '1'])
            self.assertEqual(output.getvalue().strip(), 'no cull_census rows')


class CullCensusLaunchOption(unittest.TestCase):
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
            code, output, _ = self.launch(directory, inherited={'X3M_CULL_CENSUS': '1'})
            self.assertEqual(code, 0)
            self.assertNotIn('X3M_CULL_CENSUS', json.loads(output)['env'])

    def test_dry_run_carries_the_switch(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--cull-census')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']}, {'X3M_CULL_CENSUS': '1'})

    def test_lod_switch_log_option(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            added = lambda out: {k: v for k, v in json.loads(out)['env'].items() if k not in baseline['env']}
            code, output, error = self.launch(directory, '--lod-switch-log')
            self.assertEqual(code, 0, error)
            self.assertEqual(added(output), {'X3M_CULL_CENSUS': '1', 'X3M_LOD_SWITCH_LOG': '16'})
            code, output, error = self.launch(directory, '--cull-census', '--lod-switch-log', '4')
            self.assertEqual(code, 0, error)
            self.assertEqual(added(output), {'X3M_CULL_CENSUS': '1', 'X3M_LOD_SWITCH_LOG': '4'})
            code, output, _ = self.launch(directory, inherited={'X3M_LOD_SWITCH_LOG': '8'})
            self.assertEqual(code, 0)
            self.assertNotIn('X3M_LOD_SWITCH_LOG', json.loads(output)['env'])
            for bad in ('0', '4097'):
                code, _, error = self.launch(directory, '--lod-switch-log', bad)
                self.assertEqual(code, 2)
                self.assertIn('--lod-switch-log out of range', error)


if __name__ == '__main__':
    unittest.main()
