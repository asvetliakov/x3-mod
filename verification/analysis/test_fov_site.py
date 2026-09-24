"""Host checks of the field-of-view option (src/proxy/fov_sites.h, src/proxy/fov.cpp, tools/manage.py --fov).

The site header compiled on the host (window, original immediate, refusal of a
changed or already patched window, the X3M_FOV parser and bounds 70..100, the
N -> F' remap table, the INS_SetFocus stub's table, pass-through and bytes
(decoded with objdump), install/read-back/rollback/restore against a copied
window at the engine's qword offset, restore only over our value), the bytes
against the site verifier's Python twin, the site verifier on the installed
executable and its refusal on copies with either site changed, the
install/restore/confirm line parsers, the production wiring and the --fov
launcher option (--dry-run only, never a launch). No Wine.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import math
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
import verify_fov_site as verifier  # noqa: E402

EXE = Path(verifier.DEFAULT_EXE)

HARNESS = r'''
#include "fov_sites.h"
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace x3m::fov::sites;
// install()/restore()'s Ops over a buffer: reads and stores only inside it, stores only while "writable",
// counters for the no-write claims, and three failure injections.
struct BufferOps {
    static constexpr std::uint32_t writable = 0x40, initial = 0x20;   // PAGE_EXECUTE_READWRITE / PAGE_EXECUTE_READ
    unsigned char* base; unsigned size;
    std::uint32_t protection = initial;
    unsigned writes = 0, protects = 0, stuck_after = 0;   // stuck_after: stores numbered above it do not land (0 = never)
    bool fail_protect = false, drop_writes = false, corrupt_first = false, last_atomic = false;
    bool in(std::uintptr_t at, unsigned n) const { const auto b = reinterpret_cast<std::uintptr_t>(base); return at >= b && at + n <= b + size; }
    bool read(std::uintptr_t at, unsigned char* out, unsigned n) { if (!in(at, n)) return false; std::memcpy(out, reinterpret_cast<const void*>(at), n); return true; }
    bool protect(std::uintptr_t at, unsigned n, std::uint32_t p, std::uint32_t* previous) {
        ++protects; if (fail_protect || !in(at, n)) return false; *previous = protection; protection = p; return true;
    }
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = last_atomic = (at & 7u) + n <= 8u;
        if (!in(at, n) || protection != writable) return false;
        ++writes;
        if (drop_writes || (stuck_after && writes > stuck_after)) return true;
        std::memcpy(reinterpret_cast<void*>(at), bytes, n);
        if (corrupt_first) { reinterpret_cast<unsigned char*>(at)[0] = 0x71; corrupt_first = false; }
        return true;
    }
};
static void hex(const unsigned char* p, unsigned n) { for (unsigned i = 0; i < n; ++i) std::printf("%02x", p[i]); std::printf("\n"); }
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    hex(expected_window, window_length); hex(expected_site, site_length); hex(expected_write, write_length);
    hex(expected_reader, reader_length); hex(expected_setfocus, setfocus_length);
    hex(expected_setfocus_case, setfocus_case_length); hex(expected_setfocus_site, setfocus_site_length); hex(expected_setfocus_callee, setfocus_callee_length);
    std::printf("%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", (unsigned long)window_va, (unsigned long)site_va, (unsigned long)write_va,
                (unsigned long)reader_va, (unsigned long)setfocus_va, (unsigned long)registry_slot_va, (unsigned long)setfocus_case_va,
                (unsigned long)setfocus_site_va, (unsigned long)setfocus_callee_va);
    // The N -> F' table and the bounds.
    const double requests[] = {70.0, 72.5, 75.0, 80.0, 85.0, 90.0, 95.0, 100.0};
    for (double v : requests) std::printf("%.4f %04x %.4f\n", v, focus_for_degrees(v), vertical_for_focus(focus_for_degrees(v)));
    check(focus_for_degrees(90.0) == 0x3470 && focus_for_degrees(70.0) == 0x2768 && focus_for_degrees(100.0) == 0x3b6f, "90 -> 0x3470, 70 -> 0x2768, 100 -> 0x3b6f");
    check(game_focus(90.0) == 0x4000 && game_focus(100.0) == 0x471c && game_focus(91.0) == 0x40b6 && game_focus(70.0) == 0x31c7, "the script's truncated F = (N << 16) / 360");
    check(focus_for_degrees(setting_min) >= near_plane_focus && focus_for_degrees(setting_max) < engine_focus && setting_default == 90.0,
          "70 keeps F' above the near-plane switch 0x2147, 100 stays below 0x4000");
    check(std::fabs(vertical_for_focus(engine_focus) - 73.7398) < 1e-3 && std::fabs(vertical_for_focus(0x3470) - 58.7159) < 1e-3, "0x4000 = 73.74 deg, 0x3470 = 58.72 deg vertical");
    check(remap_focus(0) == 0 && remap_focus(0x8000) == 0 && game_focus(0.0) == 0 && game_focus(180.0) == 0 && game_focus(-5.0) == 0 && game_focus(std::nan("")) == 0,
          "no focus outside (0, 0x8000) / (0, 180)");
    check(in_range(70.0) && in_range(100.0) && in_range(72.5) && !in_range(69.999) && !in_range(100.001) && !in_range(58.7155) && !in_range(std::nan("")), "bounds 70..100 inclusive");
    // The INS_SetFocus table: every N 50..130 through the stub's rounding, pass-through outside.
    std::uint16_t table[remap_count];
    build_remap_table(table);
    for (unsigned i = 0; i < remap_count; ++i) std::printf("%04x%s", table[i], i + 1 < remap_count ? " " : "\n");
    bool rows_ok = true;
    for (unsigned n = remap_first; n < remap_first + remap_count; ++n)
        rows_ok = rows_ok && remap_lookup((n << 16) / 360u, table) == remap_focus((n << 16) / 360u) && table[n - remap_first] == focus_for_degrees(double(n));
    check(rows_ok, "every N 50..130: the script's F maps to F'(N), the constructor's value for the same N");
    check(remap_lookup((49u << 16) / 360u, table) == (49u << 16) / 360u && remap_lookup((131u << 16) / 360u, table) == (131u << 16) / 360u &&
          remap_lookup((50u << 16) / 360u, table) == 0x1b6a && remap_lookup((130u << 16) / 360u, table) == 0x52ab, "N 49 and 131 pass through, 50 and 130 remap");
    bool monotonic = true;
    for (unsigned i = 1; i < remap_count; ++i) monotonic = monotonic && table[i] > table[i - 1];
    check(monotonic, "the table rises with N (no step backwards inside it)");
    bool rounding_ok = true;
    for (std::uint32_t f = remap_focus_min; f <= remap_focus_max; ++f) {
        const unsigned n = remap_index(f);
        rounding_ok = rounding_ok && n >= remap_first && n < remap_first + remap_count && std::fabs(f * 360.0 / 65536.0 - n) <= 0.5;
    }
    check(rounding_ok, "every F in [remap_focus_min, remap_focus_max] rounds to its nearest N inside the table");
    const std::uint32_t outside[] = {0u, 1u, remap_focus_min - 1, remap_focus_max + 1, 0x8000u, 0xfffffff0u, 0xffffffffu};
    for (std::uint32_t f : outside) check(remap_lookup(f, table) == f, "outside the table: unchanged");
    check(remap_lookup(0x4001, table) == 0x3470 && remap_lookup(0x471c, table) == 0x3b6f, "a non-canonical F maps to its nearest N");
    unsigned char stub[stub_length];
    encode_setfocus_stub(0x10002000u, 0x10001ffcu, stub);
    hex(stub, stub_length);
    check(plausible_focus(0x106) && plausible_focus(0x8000) && !plausible_focus(0x105) && !plausible_focus(0x8001) && !plausible_focus(0), "plausible 0x106..0x8000");
    // The X3M_FOV parser.
    double d = -1;
    check(parse_setting("game", &d) == Parse::game && parse_setting("", &d) == Parse::game && parse_setting(static_cast<const char*>(nullptr), &d) == Parse::game, "game, empty, null");
    check(parse_setting("58.7155", &d) == Parse::degrees && std::fabs(d - 58.7155) < 1e-9, "58.7155");
    check(parse_setting("+36", &d) == Parse::degrees && d == 36.0 && parse_setting(".5", &d) == Parse::degrees && d == 0.5 && parse_setting("120.", &d) == Parse::degrees && d == 120.0, "+36 .5 120.");
    check(parse_setting(L"73.74", &d) == Parse::degrees && std::fabs(d - 73.74) < 1e-9 && parse_setting(L"game", &d) == Parse::game, "wide");
    const char* rejected[] = {"Game", "GAME", "game ", " game", "games", "gam", "58,72", "1e2", "-40", "58.72deg", ".", "+", "58.7.2", "nan", "inf", "0x3470"};
    for (const char* t : rejected) { d = -1; check(parse_setting(t, &d) == Parse::invalid && d == -1, t); }
    check(setting_capacity == 32, "1..31 characters");
    unsigned char imm[write_length]; encode(0x3470, imm);
    check(imm[0] == 0x70 && imm[1] == 0x34 && imm[2] == 0 && imm[3] == 0, "encode little endian");
    // plan(): the engine window only.
    unsigned char copy[window_length];
    check(plan(expected_window) == nullptr, "engine window accepted");
    const unsigned offsets[] = {0, 3, 7, 12, site_offset, site_offset + 2, write_offset, write_offset + 1, write_offset + 3, 22, window_length - 1};
    for (unsigned at : offsets) {
        std::memcpy(copy, expected_window, window_length); copy[at] ^= 0x01;
        const char* r = plan(copy); check(r && !std::strcmp(r, "bytes_mismatch"), "a changed window byte is refused");
    }
    std::memcpy(copy, expected_window, window_length); std::memcpy(copy + write_offset, imm, write_length);
    { const char* r = plan(copy); check(r && !std::strcmp(r, "bytes_mismatch"), "an already patched window is refused"); }
    // install()/restore() against a copied window in a buffer, the imm32 at the engine's offset 4 of its 8-byte word
    // (window at +4: 0x0041c9cc & 7 == 4, so window + 16 & 7 == 4).
    alignas(8) static unsigned char code[64];
    unsigned char original[64];
    std::memset(code, 0xcc, sizeof code); std::memcpy(code + 4, expected_window, window_length); std::memcpy(original, code, sizeof code);
    const std::uintptr_t window = reinterpret_cast<std::uintptr_t>(code + 4), at = window + write_offset;
    check((at & 7u) == (write_va & 7u) && (at & 7u) == 4u, "buffer write span at the engine's qword offset");
    BufferOps ops{code, sizeof code};
    bool live = true, atomic = false; std::uint32_t protection = 0;
    auto is = [](const char* r, const char* want) { return r && !std::strcmp(r, want); };
    auto span_is = [&](const unsigned char* want) { return !std::memcmp(code + 4 + write_offset, want, write_length); };
    code[4 + 5] ^= 0x01;
    check(is(install(ops, window, true, 0x3470, &live, &atomic, &protection), "bytes_mismatch") && !live && ops.writes == 0 && ops.protects == 0 && span_is(expected_write), "mismatched window refused, nothing written");
    code[4 + 5] ^= 0x01;
    check(is(install(ops, window, false, 0x3470, &live, &atomic, &protection), "late_claim") && !live && ops.writes == 0 && ops.protects == 0, "closed install window refused, nothing written");
    check(is(install(ops, 0, true, 0x3470, &live, &atomic, &protection), "invalid_site") && ops.writes == 0, "null window refused");
    check(is(install(ops, window, true, engine_focus, &live, &atomic, &protection), "invalid_value") && is(install(ops, window, true, 0x105, &live, &atomic, &protection), "invalid_value") &&
          is(install(ops, window, true, 0x8001, &live, &atomic, &protection), "invalid_value") && ops.writes == 0 && ops.protects == 0, "0x4000, 0x105 and 0x8001 refused before any access");
    check(is(install(ops, reinterpret_cast<std::uintptr_t>(code) + 40, true, 0x3470, &live, &atomic, &protection), "unreadable") && ops.writes == 0, "window past the readable range refused");
    check(!std::memcmp(code, original, sizeof code), "buffer untouched by the refusals");
    check(is(install(ops, window, true, 0x3470, &live, &atomic, &protection), "ok") && live && atomic && ops.writes == 1, "install ok, one atomic write");
    check(span_is(imm) && code[4 + site_offset] == 0xc7 && code[4 + site_offset + 1] == 0x46 && code[4 + site_offset + 2] == 0x24 && protection == BufferOps::initial &&
          ops.protection == BufferOps::initial, "imm32 read back as 70 34 00 00, opcode c7 46 24 kept, protection restored");
    { unsigned char expect[64]; std::memcpy(expect, original, 64); std::memcpy(expect + 4 + write_offset, imm, write_length); check(!std::memcmp(code, expect, 64), "only the four immediate bytes changed"); }
    check(is(install(ops, window, true, 0x3470, &live, &atomic, &protection), "bytes_mismatch") && ops.writes == 1, "already patched window refused, no second write");
    unsigned char found[write_length]{}; bool found_read = false;
    auto found_is = [&](const unsigned char* want) { return found_read && !std::memcmp(found, want, write_length); };
    unsigned char other[write_length]; encode(0x3471, other);
    // Restore only over our value: another focus in the span (someone else's) is left alone.
    std::memcpy(code + 4 + write_offset, other, write_length);
    { const unsigned w = ops.writes, pr = ops.protects;
      check(is(restore(ops, at, 0x3470, protection, found, &found_read), "restore_not_owned") && found_is(other) && span_is(other) && ops.writes == w && ops.protects == pr,
            "a different focus in the span: restore_not_owned, nothing written"); }
    std::memcpy(code + 4 + write_offset, imm, write_length);
    check(is(restore(ops, at, 0x3470, protection, found, &found_read), "restored") && found_is(imm) && ops.writes == 2 && ops.last_atomic &&
          !std::memcmp(code, original, sizeof code) && ops.protection == BufferOps::initial, "restore returns 00 40 00 00 over our 70 34 00 00, atomic");
    check(is(restore(ops, at, 0x3470, protection, found, &found_read), "restored") && found_is(expected_write) && ops.writes == 2, "restore of a span already holding 00 40 00 00: nothing written");
    ops.drop_writes = true;
    check(is(install(ops, window, true, 0x3470, &live, &atomic, &protection), "patch_rolled_back") && !live && !std::memcmp(code, original, sizeof code), "unverified write rolled back, judged by read-back");
    ops.drop_writes = false; ops.fail_protect = true; const unsigned before = ops.writes;
    check(is(install(ops, window, true, 0x3470, &live, &atomic, &protection), "protect_failed") && !live && ops.writes == before, "protect failure: nothing written");
    ops.fail_protect = false; ops.stuck_after = ops.writes + 1; ops.corrupt_first = true;
    const unsigned char corrupt[write_length] = {0x71, 0x34, 0x00, 0x00};
    check(is(install(ops, window, true, 0x3470, &live, &atomic, &protection), "rollback_failed") && live && span_is(corrupt), "wrong bytes that cannot be undone stay registered (live)");
    { const unsigned w = ops.writes, pr = ops.protects;
      check(is(restore(ops, at, 0x3470, protection, found, &found_read), "restore_not_owned") && found_is(corrupt) && span_is(corrupt) && ops.writes == w && ops.protects == pr,
            "foreign bytes on a registered span: restore_not_owned, nothing written"); }
    ops.stuck_after = 0;
    std::memcpy(code + 4 + write_offset, imm, write_length);
    ops.drop_writes = true;
    check(is(restore(ops, at, 0x3470, protection, found, &found_read), "restore_failed") && found_is(imm) && span_is(imm) && ops.protection == BufferOps::initial,
          "restore store that does not land: restore_failed, found reported, protection put back");
    ops.drop_writes = false; ops.fail_protect = true;
    { const unsigned w = ops.writes;
      check(is(restore(ops, at, 0x3470, protection, found, &found_read), "restore_failed") && found_is(imm) && span_is(imm) && ops.writes == w, "restore protect failure: restore_failed, nothing written"); }
    ops.fail_protect = false;
    check(is(restore(ops, at, 0x3470, protection, found, &found_read), "restored") && !std::memcmp(code, original, sizeof code) && ops.protection == BufferOps::initial,
          "restore after the failures: 00 40 00 00, whole buffer as before");
    { const unsigned w = ops.writes, pr = ops.protects;
      check(is(restore(ops, reinterpret_cast<std::uintptr_t>(code) + sizeof code, 0x3470, protection, found, &found_read), "restore_not_owned") && !found_read &&
            ops.writes == w && ops.protects == pr, "unreadable span: restore_not_owned, found unread, nothing written"); }
    std::printf("fov_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('fov_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FovCore(unittest.TestCase):
    def test_core_compiled_bytes_conversion_parser_and_sequence(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-fov-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'fov_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            lines = run.stdout.splitlines()
            self.assertEqual(lines[-1], 'fov_core checks_failed=0')
            self.assertEqual([bytes.fromhex(line) for line in lines[0:8]], [verifier.WINDOW, verifier.SITE, verifier.WRITE, verifier.READER, verifier.SETFOCUS,
                                                                            verifier.CASE, verifier.SETFOCUS_SITE, verifier.CALLEE])
            self.assertEqual(bytes.fromhex(lines[0]), bytes.fromhex('895e20 c6461901 885e1a 895e1c c7462400400000 897c2430 895c242c'))
            self.assertEqual([int(v, 16) for v in lines[8].split()],
                             [verifier.WINDOW_VA, verifier.SITE_VA, verifier.WRITE_VA, verifier.READER_VA, verifier.SETFOCUS_VA, verifier.REGISTRY_SLOT_VA,
                              verifier.CASE_VA, verifier.SETFOCUS_SITE_VA, verifier.CALLEE_VA])
            # The C++ remap equals the Python twin for every tabled request and the whole INS_SetFocus table.
            table = {float(v): int(f, 16) for v, f, _ in (line.split() for line in lines[9:17])}
            self.assertEqual(table, {v: verifier.focus_for_degrees(v) for v in (70.0, 72.5, 75.0, 80.0, 85.0, 90.0, 95.0, 100.0)})
            self.assertEqual([int(v, 16) for v in lines[17].split()], verifier.remap_table())
            # The stub bytes: operands at their offsets, and objdump decodes the documented sequence.
            stub = bytes.fromhex(lines[18])
            self.assertEqual(len(stub), verifier.STUB_LENGTH)
            self.assertEqual(stub, bytes.fromhex('81f934230000 721f 81f9cc5c0000 7717 69d168010000 81c200800000 c1ea10 0fb70c55'.replace(' ', ''))
                             + struct.pack('<I', 0x10002000 - 2 * verifier.REMAP_FIRST) + bytes.fromhex('ff25') + struct.pack('<I', 0x10001ffc))
            objdump = shutil.which('i686-w64-mingw32-objdump')
            if objdump:
                blob = directory / 'stub.bin'
                blob.write_bytes(stub)
                text = subprocess.run([objdump, '-D', '-b', 'binary', '-m', 'i386', '-Mintel', '--adjust-vma=0x10000000', str(blob)],
                                      capture_output=True, text=True, check=True).stdout
                decoded = [' '.join(line.split('\t')[2].split()) for line in text.splitlines() if line.count('\t') >= 2]
                self.assertEqual(decoded, ['cmp ecx,0x2334', 'jb 0x10000027', 'cmp ecx,0x5ccc', 'ja 0x10000027', 'imul edx,ecx,0x168', 'add edx,0x8000',
                                           'shr edx,0x10', 'movzx ecx,WORD PTR [edx*2+0x10001f9c]', 'jmp DWORD PTR ds:0x10001ffc'])

    def test_python_twin_and_conversion_table(self):
        self.assertEqual(verifier.source_constants((ROOT / 'src/proxy/fov_sites.h').read_text()), verifier.EXPECTED_CONSTANTS)
        self.assertEqual(verifier.WINDOW[verifier.SITE_VA - verifier.WINDOW_VA:][:7], verifier.SITE)
        self.assertEqual(verifier.SITE[3:], verifier.WRITE)
        self.assertEqual(verifier.WRITE_VA // 8, (verifier.WRITE_VA + 3) // 8)   # one aligned qword: one lock cmpxchg8b
        self.assertEqual(verifier.SITE_VA // 8, (verifier.SITE_VA + 6) // 8)  # the whole seven-byte MOV lies in that qword too
        self.assertEqual(verifier.CASE[verifier.SETFOCUS_SITE_OFFSET:][:6], verifier.SETFOCUS_SITE)
        self.assertEqual(verifier.CASE[verifier.SETFOCUS_VA - verifier.CASE_VA:][:3], verifier.SETFOCUS)
        self.assertEqual((verifier.SETFOCUS_SITE_VA % 8, verifier.SETFOCUS_SITE_VA // 8), (0, (verifier.SETFOCUS_SITE_VA + 4) // 8))  # the jmp: one qword
        self.assertEqual({v: hex(verifier.focus_for_degrees(v)) for v in verifier.CONVERSIONS}, {v: hex(f) for v, f in verifier.CONVERSIONS.items()})
        self.assertTrue(verifier.conversion_ok())
        # The launcher default N = 90 is 0x3470; the vertical and wider-screen horizontals listed in --fov's help.
        manage = load_manage()
        default = float(manage.FOV_DEFAULT_SETTING)
        self.assertEqual((default, verifier.focus_for_degrees(default)), (90.0, 0x3470))
        vertical = verifier.vertical_for_focus(verifier.focus_for_degrees(default))
        self.assertEqual([round(verifier.horizontal_for_vertical(vertical, a)) for a in (16 / 9, 64 / 27, 3440 / 1440, 32 / 9)], [90, 106, 107, 127])
        self.assertEqual([round(verifier.horizontal_for_vertical(verifier.vertical_for_focus(0x4000), a)) for a in (16 / 9, 32 / 9)], [106, 139])
        self.assertEqual({n: round(verifier.vertical_for_focus(verifier.focus_for_degrees(n)), 1) for n in (70, 80, 90, 100)},
                         {70: 43.0, 80: 50.5, 90: 58.7, 100: 67.7})
        self.assertEqual((manage.FOV_MIN_DEG, manage.FOV_MAX_DEG), (verifier.SETTING_MIN, verifier.SETTING_MAX))

    def test_line_parsers(self):
        row = verifier.parse_log_line('00:01 fov site=0041c9dc status=patched reason=ok value=0x3470 vertical_deg=58.72 setting=90 write=atomic '
                                      'registry=written registry_before=0x4000 setfocus=active setfocus_write=atomic')
        self.assertEqual(row, {'site': 0x41c9dc, 'status': 'patched', 'reason': 'ok', 'value': 0x3470, 'vertical_deg': 58.72, 'setting': '90',
                               'write': 'atomic', 'registry': 'written', 'registry_before': 0x4000, 'setfocus': 'active', 'setfocus_write': 'atomic',
                               'patched': True})
        off = verifier.parse_log_line('fov site=0041c9dc status=off reason=game value=0x4000 vertical_deg=73.74 setting=- write=none registry=skipped registry_before=- '
                                      'setfocus=none setfocus_write=none')
        self.assertEqual((off['patched'], off['reason'], off['registry_before'], off['setfocus']), (False, 'game', None, 'none'))
        refused = verifier.parse_log_line('fov site=0041c9dc status=refused reason=setfocus_failed value=0x4000 vertical_deg=73.74 setting=90 write=atomic '
                                          'registry=skipped registry_before=- setfocus=readback_failed setfocus_write=atomic')
        self.assertEqual((refused['status'], refused['reason'], refused['setfocus']), ('refused', 'setfocus_failed', 'readback_failed'))
        unverified = verifier.parse_log_line('fov site=0041c9dc status=patched_unverified reason=rollback_failed value=0x3470 vertical_deg=58.72 setting=90 '
                                             'write=atomic registry=skipped registry_before=- setfocus=none setfocus_write=none')
        self.assertEqual((unverified['patched'], unverified['status']), (False, 'patched_unverified'))
        self.assertIsNone(verifier.parse_log_line('fov site=0041c9dc status=patched reason=ok value=0x3470'))
        self.assertIsNone(verifier.parse_log_line('fov site=0041c9dc status=patched reason=ok value=0x3470 vertical_deg=58.72 setting=90 write=atomic '
                                                  'registry=written registry_before=-'))   # the pre-remap row shape
        self.assertIsNone(verifier.parse_log_line('fov site=0041c9dc status=maybe reason=ok value=0x3470 vertical_deg=58.72 setting=- write=atomic registry=written '
                                                  'registry_before=- setfocus=active setfocus_write=atomic'))
        restore = verifier.parse_restore_line('fov_restore site=0041c9dc status=restored found=70340000 registered=0 setfocus=restored')
        self.assertEqual(restore, {'site': 0x41c9dc, 'status': 'restored', 'found': bytes.fromhex('70340000'), 'registered': False, 'setfocus': 'restored'})
        self.assertEqual(verifier.parse_restore_line('fov_restore site=0041c9dc status=restore_failed found=-- registered=1 setfocus=none')['found'], None)
        only_second = verifier.parse_restore_line('fov_restore site=0041c9dc status=none found=-- registered=1 setfocus=restore_not_owned')
        self.assertEqual((only_second['status'], only_second['registered'], only_second['setfocus']), ('none', True, 'restore_not_owned'))
        self.assertIsNone(verifier.parse_restore_line('fov_restore site=0041c9dc status=restored found=7034 registered=0 setfocus=none'))
        confirm = verifier.parse_confirm_line('fov_confirm frame=1 registry=0a1b2c30 focus=0x3470 expected=0x3470 match=1 vertical_deg=58.72 camera=skipped')
        self.assertEqual(confirm, {'frame': 1, 'registry': 0x0a1b2c30, 'focus': 0x3470, 'expected': 0x3470, 'match': True, 'vertical_deg': 58.72, 'camera': 'skipped'})
        absent = verifier.parse_confirm_line('fov_confirm frame=1 registry=absent focus=- expected=0x3470 match=0 vertical_deg=- camera=skipped')
        self.assertEqual((absent['registry'], absent['focus'], absent['match']), (None, None, False))

    def test_production_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('fov::initialize();'), 1)
        self.assertLess(capture.index('game_phases::initialize();'), capture.index('fov::initialize();'))
        self.assertLess(capture.index('fov::initialize();'), capture.index('cull_small_parts::initialize();'))
        self.assertEqual(capture.count('fov::present(ctx.frame);'), 1)
        self.assertLess(capture.index('close_install_window("first_present");'), capture.index('fov::present(ctx.frame);'))
        self.assertIn('if (reserved == nullptr) x3m::fov::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())
        self.assertIn('src/proxy/fov.cpp', (ROOT / 'CMakeLists.txt').read_text())
        module = (ROOT / 'src/proxy/fov.cpp').read_text()
        for needle in ('L"X3M_FOV"', 'setting_capacity', '"too_long"', '"invalid_setting"', '"out_of_range"', '"reader_mismatch"', '"setfocus_mismatch"',
                       '"setfocus_failed"', 'install_window_open()', 'executable_verified()', 'engine_patch::write_code', 'FlushInstructionCache', 'VirtualProtect',
                       'PAGE_EXECUTE_READWRITE', '"patched_unverified"', 'if (!std::strcmp(reason, "restored"))', 'log_handle()', 'WriteFile(handle',
                       'InterlockedCompareExchange', 'VirtualQuery', 'engine_memory::read', 'SetLastError(error);',
                       'engine_patch::claim(setfocus_site_, spec)', 'engine_patch::store_pointer(slot, *setfocus_site_.entry)',
                       'engine_patch::push_front(setfocus_site_, reinterpret_cast<void*>(stub))', 'engine_patch::restore(setfocus_site_);',
                       '"readback_failed"', 'rollback_constructor();', 'sites::build_remap_table(values);', 'sites::encode_setfocus_stub(',
                       'install_setfocus(sites::setfocus_site_va)', 'sites::focus_for_degrees(degrees)',
                       '"fov_restore site=%08lx status=%s found=%s registered=%u setfocus=%s\\n"',
                       'log("fov site=%08lx status=%s reason=%s value=0x%04lx vertical_deg=%.2f setting=%s write=%s registry=%s registry_before=%s setfocus=%s setfocus_write=%s"',
                       '"fov_confirm frame=%llu registry=%08lx focus=0x%04lx expected=0x%04lx match=%u vertical_deg=%.2f camera=skipped"'):
            self.assertIn(needle, module)
        # The stub runs no proxy code: no call into this module, only the arena block (stub, slot, table).
        self.assertNotIn('pin_self', module)
        self.assertLess(module.index('install_at(sites::window_va, focus = sites::focus_for_degrees(degrees))'), module.index('install_setfocus(sites::setfocus_site_va)'))
        header = (ROOT / 'src/proxy/fov_sites.h').read_text()
        for needle in ('plan(current)', 'memcmp(back, expected_write, write_length)', '"patch_rolled_back"', '"rollback_unprotected"', '"rollback_failed"',
                       '"restore_not_owned"', '"restore_failed"', '*live = true;', '"invalid_value"'):
            self.assertIn(needle, header)
        self.assertNotIn('windows.h', header)
        small = (ROOT / 'src/proxy/cull_small_parts.cpp').read_text()
        # the scene view's F (zoom included) from the latched scene projection; the registry only as the fallback
        self.assertIn('core::choose(scene_, sample.state.m00, sample.state.m11, scene_.usable() ? 0u : fov::current_focus())', small)
        self.assertIn('apply(choice.m00, width_, choice.focus, choice.source, choice.fallback);', small)


@unittest.skipUnless(EXE.is_file(), 'installed executable not present')
class FovSite(unittest.TestCase):
    def run_verifier(self, exe):
        return subprocess.run([sys.executable, str(ROOT / 'verification/probe/verify_fov_site.py'), '--exe', str(exe)],
                              capture_output=True, text=True, timeout=300, cwd=ROOT / 'verification/probe')

    def test_installed_executable(self):
        run = self.run_verifier(EXE)
        self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(report['result'], 'PASS')
        self.assertEqual((report['site_bytes'], report['reader_bytes'], report['setfocus_bytes']), ('c7462400400000', '8b15048560008b7224', '894a24'))
        self.assertEqual(report['constructor_callers'], ['0x403a26', '0x4050f4'])
        self.assertEqual((report['raw_branch_hits_not_interior'], report['incoming_window_branches'], report['dword_refs'], report['overlapping_claims']),
                         ([], [], 0, []))
        self.assertEqual((report['setfocus_site_bytes'], report['setfocus_jump_table_entry'], report['setfocus_table_entries_inside'],
                          report['setfocus_raw_branch_hits'], report['setfocus_dword_refs'], report['setfocus_overlapping_claims']),
                         ('8b1504856000', '0x42dbed', [], [], 0, []))
        self.assertEqual(report['setfocus_case_starts'], [hex(va) for va in verifier.CASE_STARTS])
        self.assertEqual(report['remap_table'][70 - 50:100 - 50 + 1:10], ['0x2768', '0x2dc5', '0x3470', '0x3b6f'])
        self.assertEqual((len(report['remap_table']), report['remap_table'][0], report['remap_table'][-1]), (81, '0x1b6a', '0x52ab'))

    def test_patched_copy_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory) / 'X3AP.exe'
            copy.write_bytes(verifier.patched_image(EXE.read_bytes()))
            report = json.loads(self.run_verifier(copy).stdout)
            self.assertEqual(report['result'], 'FAIL')
            self.assertTrue(report['checks']['exe_identity'])
            self.assertFalse(report['checks']['window_bytes'])
            self.assertEqual(report['site_bytes'], 'c7462470340000')

    def test_changed_setfocus_site_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory) / 'X3AP.exe'
            data = bytearray(EXE.read_bytes())
            image = verifier.common.Image(bytes(data))
            offset = next(raw + verifier.SETFOCUS_SITE_VA + 1 - (image.image_base + va) for name, _, va, size, raw, _ in verifier.exe_identity.section_table(bytes(data))
                          if image.image_base + va <= verifier.SETFOCUS_SITE_VA < image.image_base + va + size)
            data[offset] ^= 0x01   # 8b 15 -> 8b 14: another instruction
            copy.write_bytes(bytes(data))
            report = json.loads(self.run_verifier(copy).stdout)
            self.assertEqual(report['result'], 'FAIL')
            self.assertTrue(report['checks']['exe_identity'] and report['checks']['window_bytes'])
            self.assertFalse(report['checks']['setfocus_case_bytes'])


class FovLaunchOption(unittest.TestCase):
    NAME = 'X3M_FOV'

    def launch(self, directory, *args, inherited=None, vanilla=False, help_text=False):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        if not vanilla:   # a modded launch wants an installed proxy that matches its manifest
            (game / 'd3d9.dll').write_bytes(b'proxy')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', *(['--help'] if help_text else ['--dry-run']), *(['--vanilla'] if vanilla else []), '--game-dir', str(game), *args]
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

    def value(self, directory, *args, **kwargs):
        code, output, error = self.launch(directory, *args, **kwargs)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env'].get(self.NAME)

    def test_default_explicit_values_and_vanilla(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.value(directory), '90')
            self.assertEqual(self.value(directory, inherited={self.NAME: '58.7155'}), '90')   # a stale value never travels
            self.assertEqual(self.value(directory, '--fov', 'game'), 'game')
            self.assertEqual(self.value(directory, '--fov', '70'), '70')
            self.assertEqual(self.value(directory, '--fov', '100', inherited={self.NAME: 'game'}), '100')
            self.assertEqual(self.value(directory, '--fov', '72.5'), '72.5')
            self.assertEqual(self.value(directory, '--fov', '90.0'), '90')
            self.assertEqual(self.value(directory, '--fov', '85.12345'), '85.1235')
            self.assertIsNone(self.value(directory, vanilla=True, inherited={self.NAME: '90'}))

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, vanilla, message in ((('--fov', '58.72'), True, 'cannot be combined with --vanilla'),
                                           (('--fov', 'game'), True, 'cannot be combined with --vanilla'),
                                           (('--fov', '69.99'), False, 'out of range'),
                                           (('--fov', '100.01'), False, 'out of range'),
                                           (('--fov', '58.7155'), False, 'out of range'),   # the former vertical default
                                           (('--fov', 'Game'), False, 'out of range'),
                                           (('--fov', 'nan'), False, 'out of range'),
                                           (('--fov', 'inf'), False, 'out of range'),
                                           (('--fov', '90deg'), False, 'out of range')):
                with self.subTest(args=args, vanilla=vanilla):
                    code, _, error = self.launch(directory, *args, vanilla=vanilla)
                    self.assertNotEqual(code, 0)
                    self.assertIn(message, error)

    def test_launch_command_unchanged_and_help(self):
        with tempfile.TemporaryDirectory() as directory:
            default = json.loads(self.launch(directory)[1])
            game = json.loads(self.launch(directory, '--fov', 'game')[1])
            self.assertEqual(default['command'], game['command'])
            self.assertEqual({k: v for k, v in game['env'].items() if default['env'].get(k) != v}, {self.NAME: 'game'})
            code, output, _ = self.launch(directory, help_text=True)
            self.assertEqual(code, 0)
            text = ' '.join(output.split())
            for needle in ('--fov N|game', 'like X4', 'horizontal angle in degrees on a 16:9 screen, 70..100', 'default 90', '70 -> 43.0 deg', '80 -> 50.5',
                           '90 -> 58.7', '100 -> 67.7', 'wider screens get more width', '21:9 shows 106 deg (2560x1080; 107 on 3440x1440)',
                           '32:9 127 (5120x1440)', 'menu always starts from its own 90 whatever --fov is', 'first press jumps to the value of 91 (or 89)',
                           'the menu works in the same units as --fov', 'N 50..130', 'central 4:3 area', '73.74 deg vertical'):
                self.assertIn(needle, text)


if __name__ == '__main__':
    unittest.main()
