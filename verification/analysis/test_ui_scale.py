"""Host checks of the UI-scale option (src/proxy/ui_scale_sites.h, src/proxy/ui_scale.cpp, tools/manage.py --ui-scale).

The core header compiled on the host (every expected byte window against the verifier's table, the auto mapping,
the fixed-point forms, the virtual size, the mouse remainder accumulator, the cursor conversion, the projection
transform against the engine's own formula for a W/s x H/s viewport over every anchor combination, the X3M_UI_SCALE
parser, the ten stub encoders with their bytes decoded by objdump and the registers they write, the map label/pick stubs
run on a small emulator of their decoded instructions), the all-or-none transaction compiled from ui_scale.cpp against a
fake engine_patch, the site verifier on the installed executable and its refusal on a copy with a site byte changed, the log-line parsers, the production
wiring and the --ui-scale launcher option (--dry-run only, never a launch). No Wine.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_ui_scale_sites as verifier  # noqa: E402
from source_text import source_text  # noqa: E402

EXE = Path(verifier.DEFAULT_EXE)

HARNESS = r'''
#include "ui_scale_sites.h"
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace x3m::ui_scale::sites;
static void hex(const unsigned char* p, unsigned n) { for (unsigned i = 0; i < n; ++i) std::printf("%02x", p[i]); std::printf("\n"); }
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    // 0..8 the site bytes, 9..20 the windows, 21 the addresses
    hex(expected_projection_site, projection_site_length); hex(expected_width_site, size_site_length); hex(expected_height_site, size_site_length);
    hex(expected_main_x_site, mouse_site_length); hex(expected_main_y_site, mouse_site_length); hex(expected_menu_x_site, mouse_site_length);
    hex(expected_menu_y_site, mouse_site_length); hex(expected_cursor_site, cursor_site_length); hex(expected_entry_site, entry_site_length);
    hex(expected_projection_tail, projection_tail_length); hex(expected_caller1, caller1_length); hex(expected_caller2, caller2_length);
    hex(expected_width_case, size_case_length); hex(expected_height_case, size_case_length); hex(expected_main_x_window, main_x_window_length);
    hex(expected_main_y_window, main_y_window_length); hex(expected_menu_x_window, menu_x_window_length); hex(expected_menu_y_window, menu_y_window_length);
    hex(expected_menu_callee, menu_callee_length); hex(expected_cursor_pre, cursor_pre_length); hex(expected_cursor_post, cursor_post_length);
    std::printf("%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", (unsigned long)projection_site_va, (unsigned long)width_site_va,
                (unsigned long)height_site_va, (unsigned long)main_x_site_va, (unsigned long)main_y_site_va, (unsigned long)menu_x_site_va,
                (unsigned long)menu_y_site_va, (unsigned long)cursor_site_va, (unsigned long)entry_site_va, (unsigned long)projection_return_va,
                (unsigned long)cursor_return_va);
    // 22 the auto mapping, 23 the fixed-point forms, 24 the virtual sizes
    const unsigned heights[] = {0, 720, 1080, 1200, 1440, 1600, 2160, 4320, 8640};
    for (unsigned h : heights) std::printf("%g ", auto_scale(h));
    std::printf("\n");
    const double scales[] = {1.0, 1.25, 1.5, 2.0, 3.0};
    for (double s : scales) std::printf("%u/%u ", inverse16(s), fixed256(s));
    std::printf("\n");
    std::printf("%u %u %u %u %u %u\n", virtual_size(5120, inverse16(1.25)), virtual_size(1440, inverse16(1.25)), virtual_size(5120, inverse16(1.5)),
                virtual_size(1440, inverse16(3.0)), virtual_size(5120, identity_inverse16), virtual_size(1440, identity_inverse16));
    // 25 the accumulator from its start (one half): ten +1 and ten -1 at 1.5 (symmetric), a 7-pixel jump at 2, the identity
    std::int32_t acc = accumulator_start, total = 0;
    for (int i = 0; i < 10; ++i) total += mouse_step(1, inverse16(1.5), &acc);
    std::printf("%ld %ld ", (long)total, (long)acc);
    acc = accumulator_start; total = 0;
    for (int i = 0; i < 10; ++i) total += mouse_step(-1, inverse16(1.5), &acc);
    std::printf("%ld %ld ", (long)total, (long)acc);
    acc = accumulator_start;
    std::printf("%ld %ld ", (long)mouse_step(7, inverse16(2.0), &acc), (long)acc);
    acc = accumulator_start;
    std::printf("%ld %ld ", (long)mouse_step(-32768, identity_inverse16, &acc), (long)acc);
    acc = accumulator_start;
    std::printf("%ld %ld\n", (long)mouse_step(32767, identity_inverse16, &acc), (long)acc);
    check(accumulator_start == 0x8000, "accumulator start");
    check(cursor_real(2048, fixed256(1.25)) == 2560 && cursor_real(0, fixed256(2.0)) == 0 && cursor_real(1000, identity_fixed256) == 1000 &&
          cursor_real(3413, fixed256(1.5)) == 5120 && cursor_real(-3, fixed256(1.5)) == -4, "cursor_real");
    // The projection transform equals the engine's own P for the W/s x H/s viewport, every anchor combination, three scales.
    const std::uint32_t anchors[] = {0x200, 0x200 | 0x1000, 0x200 | 0x2000, 0x200 | 0x4000, 0x200 | 0x8000, 0x200 | 0x1000 | 0x4000,
                                     0x200 | 0x2000 | 0x8000, 0x200 | 0x1000 | 0x2000 | 0x4000 | 0x8000, 0x200 | 0x10000 | 0x4000 | 0x2000};
    double worst = 0;
    for (double s : scales) {
        for (std::uint32_t flags : anchors) {
            float p[16], q[16];
            engine_projection(p, 5120, 1440, 100, 50, flags);
            scale_projection(p, flags, static_cast<float>(s));
            engine_projection(q, int(5120 / s + 0.5), int(1440 / s + 0.5), 100, 50, flags);
            const bool exact = s == 1.0 || s == 2.0 || s == 1.25 || s == 1.5 || s == 3.0;
            (void)exact;
            for (unsigned i = 0; i < 16; ++i) {
                const double e = std::fabs(double(p[i]) - double(q[i]));
                if (e > worst) worst = e;
            }
            check(p[14] == 1.0f && p[15] == 1.0f && p[10] == 0.0f, "projection untouched elements");
        }
    }
    std::printf("%.3g\n", worst);  // 26: the worst difference (the viewport rounding at 3413.33 -> 3413 for s = 1.5 and 1706.67 for 3)
    float p[16];
    engine_projection(p, 5120, 1440, 100, 50, 0x200 | 0x1000 | 0x4000);
    scale_projection(p, 0x200 | 0x1000 | 0x4000, 1.0f);
    float v[16];
    engine_projection(v, 5120, 1440, 100, 50, 0x200 | 0x1000 | 0x4000);
    check(!std::memcmp(p, v, sizeof p), "scale 1 is the identity");
    check(anchor_x(0x200 | 0x1000 | 0x2000) == -1.0f && anchor_y(0x200 | 0x4000 | 0x8000) == 1.0f, "anchor precedence");
    // 27 the parser
    double value = 0;
    const char* texts[] = {"", "auto", "1", "1.25", "1.5", "2", "3", "+2.5", ".5", "1.", "abc", "auto ", "1e1", "0.9", "3.01", "-1", "Auto"};
    for (const char* t : texts) std::printf("%d:%g ", (int)parse_setting(t, &value), value);
    std::printf("\n");
    check(parse_setting(L"1.25", &value) == Parse::value && value == 1.25, "wide parser");
    check(in_range(1.0) && in_range(3.0) && !in_range(0.99) && !in_range(3.01) && !in_range(NAN), "in_range");
    // 28..33 the six stubs with sample operands
    unsigned char a[projection_stub_length], b[size_stub_length], c[main_mouse_stub_length], d[menu_mouse_stub_length], e[cursor_stub_length], f[entry_stub_length];
    encode_projection_stub(0x10000100, 0x10000104, 0x10000108, 0x1000010c, 0x10000110, 0x10000120, 0x10001ffc, a); hex(a, sizeof a);
    encode_size_stub(0x10000200, 0x10001ffc, b); hex(b, sizeof b);
    encode_main_mouse_stub(delta_x_offset, 0x10000200, 0x10000300, 0x10001ffc, c); hex(c, sizeof c);
    encode_menu_mouse_stub(delta_y_offset, 0x10000200, 0x10000304, 0x10001ffc, d); hex(d, sizeof d);
    encode_cursor_stub(0x10000400, 0x10001ffc, e); hex(e, sizeof e);
    encode_entry_stub(0x10000500, 0x10001ffc, f); hex(f, sizeof f);
    // 34..41 the click-point sites G and H: site bytes, case windows, the icon test prefix, addresses, the two stubs
    hex(expected_overlay_site, overlay_site_length); hex(expected_aim_site, aim_site_length);
    hex(expected_overlay_case, overlay_case_length); hex(expected_aim_case, aim_case_length); hex(expected_icon_test, icon_test_length);
    std::printf("%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", (unsigned long)overlay_case_va, (unsigned long)overlay_site_va,
                (unsigned long)overlay_return_va, (unsigned long)aim_case_va, (unsigned long)aim_site_va, (unsigned long)aim_return_va,
                (unsigned long)aim_flags_va, (unsigned long)icon_test_va);
    unsigned char g[overlay_stub_length], h[aim_stub_length];
    encode_overlay_stub(0x10000400, 0x10001ffc, g); hex(g, sizeof g);
    encode_aim_stub(0x10000400, 0x10001ffc, h); hex(h, sizeof h);
    check(cursor_real(-1, fixed256(1.0)) == -1 && cursor_real(-1, fixed256(3.0)) == -3 && cursor_real(-1, fixed256(1.25)) == -1, "the -1 sentinel stays negative");
    // 42..50 the map sites I and J: site bytes, windows, addresses, the two stubs, the label rounding at 1, 1.5, 2, 3
    hex(expected_map_label_site, map_label_site_length); hex(expected_map_pick_site, map_pick_site_length);
    hex(expected_map_label_window, map_label_window_length); hex(expected_map_callback, map_callback_length);
    hex(expected_map_pick_case, map_pick_case_length);
    std::printf("%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", (unsigned long)map_label_site_va, (unsigned long)map_label_return_va,
                (unsigned long)map_pick_case_va, (unsigned long)map_pick_site_va, (unsigned long)map_pick_return_va,
                (unsigned long)map_pick_flags_va, (unsigned long)map_state_va, (unsigned long)map_vm_slot_va);
    unsigned char ml[map_label_stub_length], mp[map_pick_stub_length];
    encode_map_label_stub(0x10000200, 0x10001ffc, ml); hex(ml, sizeof ml);
    encode_map_pick_stub(0x10000400, 0x10001ffc, mp); hex(mp, sizeof mp);
    const std::int32_t samples[] = {0, 1, -1, 2, -2, 3, -3, 5, -5, 90, -90, 231, -231, 2559, -2559, 1000001, -1000001, INT32_MAX, INT32_MIN};
    const double map_scales[] = {1.0, 1.5, 2.0, 3.0}, symmetric_scales[] = {1.25, 1.5, 2.0, 3.0};
    for (double s : map_scales)
        for (std::int32_t v : samples) std::printf("%ld ", (long)map_offset(v, inverse16(s)));
    std::printf("\n");
    for (std::int32_t v : samples) {
        check(map_offset(v, identity_inverse16) == v, "map_offset identity");
        for (double s : symmetric_scales)
            if (v != INT32_MIN) check(map_offset(-v, inverse16(s)) == -map_offset(v, inverse16(s)), "map_offset symmetric");
    }
    std::printf("ui_scale_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


MAP_SAMPLES = [0, 1, -1, 2, -2, 3, -3, 5, -5, 90, -90, 231, -231, 2559, -2559, 1000001, -1000001, 2**31 - 1, -2**31]
MASK = 0xffffffff


def s32(v):
    v &= MASK
    return v - (1 << 32) if v & 0x80000000 else v


class Stub:
    """Runs a decoded stub (objdump Intel text) on 32-bit registers, a dword memory and CF/ZF/SF/OF, recording what it writes."""
    FLAG_WRITERS = ('imul', 'cmp', 'add', 'adc', 'sar', 'shrd')

    def __init__(self, registers, memory):
        self.r, self.m, self.flags, self.written, self.flag_writers = dict(registers), dict(memory), None, set(), []

    def address(self, operand):
        m = re.fullmatch(r'DWORD PTR (?:ds:(0x[0-9a-f]+)|\[(\w+)(?:\+(0x[0-9a-f]+))?\])', operand)
        return int(m.group(1), 16) if m.group(1) else (self.r[m.group(2)] + int(m.group(3) or '0', 16)) & MASK

    def get(self, operand):
        if operand in self.r:
            return self.r[operand]
        return self.m[self.address(operand)] if operand.startswith('DWORD PTR') else int(operand, 16) & MASK

    def put(self, operand, value):
        key = operand if operand in self.r else self.address(operand)
        (self.r if operand in self.r else self.m)[key] = value & MASK
        self.written.add(key)

    def subtract_flags(self, a, b):
        d = (a - b) & MASK
        return {'CF': a < b, 'ZF': d == 0, 'SF': bool(d >> 31), 'OF': not -2**31 <= s32(a) - s32(b) < 2**31}

    def run(self, decoded):
        for k, line in enumerate(decoded):
            mnemonic, _, operands = line.partition(' ')
            args = operands.split(',') if operands else []
            if mnemonic == 'jmp':
                return args[0]
            if mnemonic == 'mov':
                self.put(args[0], self.get(args[1]))
            elif mnemonic == 'imul' and len(args) == 1:
                p = s32(self.r['eax']) * s32(self.get(args[0]))
                self.put('eax', p)
                self.put('edx', p >> 32)
            elif mnemonic == 'imul':
                self.put(args[0], s32(self.get(args[0])) * s32(self.get(args[1])))
            elif mnemonic == 'cmp':
                self.flags = self.subtract_flags(self.get(args[0]), self.get(args[1]))
            elif mnemonic in ('add', 'adc'):
                a, b = self.get(args[0]), self.get(args[1])
                total = a + b + (1 if mnemonic == 'adc' and self.flags['CF'] else 0)
                self.put(args[0], total)
                self.flags = {'CF': total > MASK, 'ZF': total & MASK == 0, 'SF': bool(total >> 31 & 1), 'OF': None}
            elif mnemonic == 'sar':
                self.put(args[0], s32(self.get(args[0])) >> int(args[1], 16))
                self.flags = None
            elif mnemonic == 'shrd':
                self.put(args[0], ((self.get(args[1]) << 32) | self.get(args[0])) >> int(args[2], 16))
                self.flags = None
            else:
                raise AssertionError(f'unmodelled instruction {line}')
            if mnemonic in self.FLAG_WRITERS:
                self.flag_writers.append(k)
        raise AssertionError('no final jmp')


ROLLBACK_PREFIX = r'''
#include "ui_scale_sites.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
namespace x3m::engine_patch {
constexpr unsigned max_prologue = 16;
struct SiteSpec { const char* name; std::uintptr_t address; unsigned char expected[max_prologue]; unsigned length; unsigned ret_pop; unsigned rel32_offset; };
struct Site { SiteSpec spec{}; void** entry = nullptr; unsigned char patched[5]{}; bool patched_in = false, atomic_write = false; const char* status = "unclaimed"; };
}
namespace fake {
int fail_index = -1, fail_stage = -1, claims = 0;
std::uintptr_t restore_fail = 0;
bool window = true, identity = true;
std::vector<std::uintptr_t> claimed, restored;
std::vector<x3m::engine_patch::Site*> live;
void* heads[16];
void* slots[16];
unsigned char tails[16];
}
namespace x3m::engine_patch {
bool install_window_open() { return fake::window; }
bool claim(Site& s, const SiteSpec& spec) {
    const int k = fake::claims++;
    s.spec = spec;
    if (k == fake::fail_index && fake::fail_stage == 0) { s.status = "bytes_mismatch"; return false; }
    s.patched_in = true;
    s.status = "patched";
    s.atomic_write = (spec.address & 7) + 5 <= 8;
    s.patched[0] = 0xe9;
    fake::heads[k] = &fake::tails[k];
    s.entry = &fake::heads[k];
    fake::claimed.push_back(spec.address);
    fake::live.push_back(&s);
    return true;
}
bool store_pointer(void** slot, void* value) {
    if (fake::claims - 1 == fake::fail_index && fake::fail_stage == 1) return false;
    *slot = value;
    return true;
}
void* push_front(Site& s, void* stub) {
    if (fake::claims - 1 == fake::fail_index && fake::fail_stage == 2) return nullptr;
    void* previous = *s.entry;
    *s.entry = stub;
    return previous;
}
bool read_code(std::uintptr_t address, unsigned char* out, unsigned n) {
    for (Site* s : fake::live)
        if (s->spec.address == address) {
            std::memcpy(out, s->spec.expected, n);
            std::memcpy(out, s->patched, 5);
            if (fake::claims - 1 == fake::fail_index && fake::fail_stage == 3) out[0] = 0xcc;
            return true;
        }
    return false;
}
bool restore(Site& s) {
    fake::restored.push_back(s.spec.address);
    if (s.spec.address == fake::restore_fail) return false;
    s.patched_in = false;
    s.status = "restored";
    return true;
}
}
volatile std::int32_t x3m_ui_scale_mouse_acc[4];
volatile std::uint32_t x3m_ui_scale_excluded_camera;
namespace {
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::ui_scale::sites;
const char* failed_site_ = "-";
bool pin_self() { return true; }
void set_cells(double) { fake::identity = false; }
void set_identity() { fake::identity = true; }
std::uintptr_t emit_stub(unsigned index, void*** slot_out);
'''
ROLLBACK_SUFFIX = r'''
std::uintptr_t emit_stub(unsigned index, void*** slot_out) {
    *slot_out = &fake::slots[index];
    return 0x10000000u + 0x100u * index;
}
}
int main() {
    unsigned checks = 0, failures = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    auto reset = [&] {
        for (unsigned i = 0; i < site_count; ++i) { site_[i] = engine_patch::Site{}; live_[i] = false; write_[i] = nullptr; fake::slots[i] = nullptr; }
        patched_ = false; failed_site_ = "-"; fake::fail_index = fake::fail_stage = -1; fake::restore_fail = 0; fake::claims = 0;
        fake::window = true; fake::identity = true; fake::claimed.clear(); fake::restored.clear(); fake::live.clear();
    };
    reset();
    check(!std::strcmp(install_all(1.5), "ok") && patched_ && any_live() && !fake::identity && fake::restored.empty(), "transaction");
    for (unsigned i = 0; i < site_count; ++i) check(live_[i] && fake::claimed[i] == claims[i].va, "claim order");
    for (unsigned i = 0; i < site_count; ++i) std::printf("%s%s", i ? " " : "", claims[i].name);
    std::printf("\n");
    // The bypass words: the instruction after each displaced span; the projection's word is the tail.
    check(fake::slots[MapLabel] == reinterpret_cast<void*>(sites::map_label_return_va) &&
          fake::slots[MapPick] == reinterpret_cast<void*>(sites::map_pick_return_va) && fake::slots[Projection] == &fake::tails[Projection],
          "continuation words");
    for (int k = 0; k < int(site_count); ++k)
        for (int stage = 0; stage < 4; ++stage) {
            reset();
            fake::fail_index = k;
            fake::fail_stage = stage;
            const char* r = install_all(1.5);
            check(!std::strcmp(r, stage == 0 ? "bytes_mismatch" : stage == 3 ? "readback_failed" : "chain_failed"), "failure reason");
            check(!patched_ && !any_live() && fake::identity && !std::strcmp(failed_site_, claims[k].name), "all or none");
            check(fake::restored.size() == unsigned(k + (stage ? 1 : 0)), "every earlier claim restored");
            check(int(fake::claimed.size()) == k + (stage ? 1 : 0), "nothing claimed after the failure");
        }
    reset();
    fake::fail_index = MapPick;
    fake::fail_stage = 1;
    fake::restore_fail = sites::map_pick_site_va;
    check(!std::strcmp(install_all(1.5), "rollback_failed") && live_[MapPick] && !patched_ && fake::identity, "rollback failed keeps J registered");
    for (unsigned i = 0; i < site_count; ++i)
        if (i != MapPick) check(!live_[i], "the others restored");
    reset();
    fake::window = false;
    check(!std::strcmp(install_all(1.5), "late_claim") && fake::claims == 0 && !any_live() && fake::identity, "late window refused");
    std::printf("ui_scale_rollback checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
'''


def extract(source, signature):
    """The definition starting at `signature` through its matching closing brace."""
    start = source.index(signature)
    depth, k = 0, source.index('{', start)
    while True:
        depth += {'{': 1, '}': -1}.get(source[k], 0)
        if depth == 0:
            return source[start:k + 1]
        k += 1


def load_manage():
    spec = importlib.util.spec_from_file_location('ui_scale_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def decode(objdump, directory, name, blob, vma):
    path = Path(directory) / f'{name}.bin'
    path.write_bytes(blob)
    text = subprocess.run([objdump, '-D', '-b', 'binary', '-m', 'i386', '-Mintel', f'--adjust-vma={vma:#x}', str(path)],
                          capture_output=True, text=True, check=True).stdout
    return [' '.join(line.split('\t')[2].split()) for line in text.splitlines() if line.count('\t') >= 2 and line.split('\t')[2].strip()]


class UiScaleCore(unittest.TestCase):
    def test_core_compiled(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-ui-scale-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'ui_scale_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            lines = run.stdout.splitlines()
            self.assertEqual(lines[-1], 'ui_scale_core checks_failed=0')
            sites = ['projection', 'width', 'height', 'main_x', 'main_y', 'menu_x', 'menu_y', 'cursor', 'entry']
            self.assertEqual([bytes.fromhex(l) for l in lines[0:9]], [verifier.SITES[s][1] for s in sites])
            windows = ['projection_tail', 'caller1', 'caller2', 'width_case', 'height_case', 'main_x_window', 'main_y_window', 'menu_x_window',
                       'menu_y_window', 'menu_callee', 'cursor_pre', 'cursor_post']
            self.assertEqual([bytes.fromhex(l) for l in lines[9:21]], [verifier.WINDOWS[w][1] for w in windows])
            self.assertEqual([int(v, 16) for v in lines[21].split()], [verifier.SITES[s][0] for s in sites] + [0x4be24b, 0x4074f1])
            self.assertEqual(lines[22].split(), ['1', '1', '1', '1', '1.5', '1.5', '2', '3', '3'])
            self.assertEqual(lines[23].split(), ['65536/256', '52429/320', '43691/384', '32768/512', '21845/768'])
            self.assertEqual(lines[24].split(), ['4096', '1152', '3413', '480', '5120', '1440'])
            self.assertEqual(lines[25].split(), ['7', '10926', '-7', '54610', '4', '0', '-32768', '32768', '32767', '32768'])
            self.assertLess(float(lines[26]), 2e-4)  # the W/s viewport rounding (3413 for 3413.33) at the P[12] scale of 2/3413
            parsed = dict(zip(['', 'auto', '1', '1.25', '1.5', '2', '3', '+2.5', '.5', '1.', 'abc', 'auto ', '1e1', '0.9', '3.01', '-1', 'Auto'],
                              lines[27].split()))
            self.assertEqual(parsed, {'': '0:1', 'auto': '1:1', '1': '2:1', '1.25': '2:1.25', '1.5': '2:1.5', '2': '2:2', '3': '2:3', '+2.5': '2:2.5',
                                      '.5': '2:0.5', '1.': '2:1', 'abc': '3:1', 'auto ': '3:1', '1e1': '3:1', '0.9': '2:0.9', '3.01': '2:3.01',
                                      '-1': '3:1', 'Auto': '3:1'})
            stubs = [bytes.fromhex(l) for l in lines[28:34]]
            self.assertEqual([len(s) for s in stubs], [verifier.STUB_LENGTHS[k] for k in ('projection', 'size', 'main_mouse', 'menu_mouse', 'cursor', 'entry')])
            self.assertEqual(stubs[1], bytes.fromhex('0faf0500020010 0500800000 c1e810 ff25fc1f0010'.replace(' ', '')))
            self.assertEqual(stubs[4], bytes.fromhex('0faf0500040010 0580000000 c1f808 0faf1500040010 81c280000000 c1fa08 ff25fc1f0010'.replace(' ', '')))
            objdump = shutil.which('i686-w64-mingw32-objdump')
            if not objdump:
                self.skipTest('i686-w64-mingw32-objdump not found: the stub decode is skipped')
            projection = decode(objdump, directory, 'projection', stubs[0], 0x10000000)
            self.assertEqual(projection, ['cmp ebp,DWORD PTR ds:0x10000104', 'je 0x10000084', 'movss xmm0,DWORD PTR ds:0x10000100',
                                          'movss xmm1,DWORD PTR [eax]', 'mulss xmm1,xmm0', 'movss DWORD PTR [eax],xmm1',
                                          'movss xmm1,DWORD PTR [eax+0x14]', 'mulss xmm1,xmm0', 'movss DWORD PTR [eax+0x14],xmm1',
                                          'mov ecx,ebx', 'shr ecx,0xc', 'and ecx,0x3', 'movss xmm2,DWORD PTR [ecx*4+0x10000110]',
                                          'movss xmm1,DWORD PTR [eax+0x30]', 'subss xmm1,xmm2', 'mulss xmm1,xmm0', 'addss xmm1,xmm2',
                                          'movss DWORD PTR [eax+0x30],xmm1', 'mov ecx,ebx', 'shr ecx,0xe', 'and ecx,0x3',
                                          'movss xmm2,DWORD PTR [ecx*4+0x10000120]', 'movss xmm1,DWORD PTR [eax+0x34]', 'subss xmm1,xmm2',
                                          'mulss xmm1,xmm0', 'addss xmm1,xmm2', 'movss DWORD PTR [eax+0x34],xmm1',
                                          'inc DWORD PTR ds:0x10000108', 'jmp DWORD PTR ds:0x10001ffc', 'inc DWORD PTR ds:0x1000010c',
                                          'jmp DWORD PTR ds:0x10001ffc'])
            # The projection stub writes ECX (scratch), XMM0..2 and P[0], P[5], P[12], P[13] plus the two counters: nothing else.
            written = {d.split(' ', 1)[1].split(',')[0] for d in projection if d.split(' ')[0] in ('mov', 'shr', 'and', 'movss', 'mulss', 'subss', 'addss', 'inc')}
            self.assertEqual(written, {'ecx', 'xmm0', 'xmm1', 'xmm2', 'DWORD PTR [eax]', 'DWORD PTR [eax+0x14]', 'DWORD PTR [eax+0x30]',
                                       'DWORD PTR [eax+0x34]', 'DWORD PTR ds:0x10000108', 'DWORD PTR ds:0x1000010c'})
            self.assertEqual(decode(objdump, directory, 'size', stubs[1], 0x10000000),
                             ['imul eax,DWORD PTR ds:0x10000200', 'add eax,0x8000', 'shr eax,0x10', 'jmp DWORD PTR ds:0x10001ffc'])
            self.assertEqual(decode(objdump, directory, 'main', stubs[2], 0x10000000),
                             ['movsx eax,WORD PTR [eax+0x414]', 'imul eax,DWORD PTR ds:0x10000200', 'add eax,DWORD PTR ds:0x10000300',
                              'mov ds:0x10000300,eax', 'sar eax,0x10', 'and DWORD PTR ds:0x10000300,0xffff', 'movzx eax,ax',
                              'jmp DWORD PTR ds:0x10001ffc'])  # through the slot word, which install_site fills with site + 7
            self.assertEqual(decode(objdump, directory, 'menu', stubs[3], 0x10000000),
                             ['movsx edi,WORD PTR [edx+0x416]', 'imul edi,DWORD PTR ds:0x10000200', 'add edi,DWORD PTR ds:0x10000304',
                              'mov DWORD PTR ds:0x10000304,edi', 'sar edi,0x10', 'and DWORD PTR ds:0x10000304,0xffff',
                              'jmp DWORD PTR ds:0x10001ffc'])
            # Every stub's final jump is indirect through a slot word inside the arena block (never a code address as m32).
            for name, blob in zip(('projection', 'size', 'main', 'menu', 'cursor', 'entry'), stubs):
                jumps = [d for d in decode(objdump, directory, name, blob, 0x10000000) if d.startswith('jmp DWORD PTR')]
                self.assertTrue(jumps and all(j == 'jmp DWORD PTR ds:0x10001ffc' for j in jumps), (name, jumps))
            self.assertEqual(decode(objdump, directory, 'cursor', stubs[4], 0x10000000),
                             ['imul eax,DWORD PTR ds:0x10000400', 'add eax,0x80', 'sar eax,0x8', 'imul edx,DWORD PTR ds:0x10000400',
                              'add edx,0x80', 'sar edx,0x8', 'jmp DWORD PTR ds:0x10001ffc'])
            entry = decode(objdump, directory, 'entry', stubs[5], 0x10000000)
            self.assertEqual(entry, ['mov ecx,DWORD PTR [esp+0x8]', 'xor edx,edx', 'cmp DWORD PTR [edx+0x10000500],ecx', 'je 0x1000002b',
                                     'cmp DWORD PTR [edx+0x10000500],0x0', 'je 0x10000025', 'add edx,0x10', 'cmp edx,0x80', 'jb 0x10000006',
                                     'sub edx,0x10', 'mov DWORD PTR [edx+0x10000500],ecx', 'mov ecx,DWORD PTR [esp+0x4]',
                                     'test DWORD PTR [ecx+0x130],0x200', 'je 0x10000043', 'inc DWORD PTR [edx+0x10000504]', 'jmp 0x10000049',
                                     'inc DWORD PTR [edx+0x10000508]', 'jmp DWORD PTR ds:0x10001ffc'])
            # No stub calls, pops or touches the x87 stack; only G pushes (the two displaced pushes it re-issues).
            for name, blob in zip(('projection', 'size', 'main', 'menu', 'cursor', 'entry'), stubs):
                mnemonics = {d.split(' ')[0] for d in decode(objdump, directory, name, blob, 0x10000000)}
                self.assertFalse(mnemonics & {'push', 'pop', 'call', 'ret'} or any(m.startswith('f') for m in mnemonics), name)
            # G and H (the click point): bytes, windows and addresses against the verifier; the stubs decoded.
            self.assertEqual([bytes.fromhex(l) for l in lines[34:36]], [verifier.SITES['overlay_icon'][1], verifier.SITES['cursor_aim'][1]])
            self.assertEqual([bytes.fromhex(l) for l in lines[36:39]], [verifier.WINDOWS[w][1] for w in ('overlay_case', 'aim_case', 'icon_test')])
            self.assertEqual([int(v, 16) for v in lines[39].split()], [0x42ecc5, 0x42ece0, 0x42ece5, 0x42ddc0, 0x42ddf1, 0x42ddf7, 0x42ddee, 0x4299a0])
            g, h = bytes.fromhex(lines[40]), bytes.fromhex(lines[41])
            self.assertEqual((len(g), len(h)), (verifier.STUB_LENGTHS['overlay'], verifier.STUB_LENGTHS['aim']))
            self.assertEqual(decode(objdump, directory, 'overlay', g, 0x10000000),
                             ['mov esi,DWORD PTR [esi+0x6]', 'imul ecx,DWORD PTR ds:0x10000400', 'add ecx,0x80', 'sar ecx,0x8',
                              'imul esi,DWORD PTR ds:0x10000400', 'add esi,0x80', 'sar esi,0x8', 'push ecx', 'push esi',
                              'jmp DWORD PTR ds:0x10001ffc'])
            aim = decode(objdump, directory, 'aim', h, 0x10000000)
            self.assertEqual(aim, ['mov esi,DWORD PTR [ebx+0x6]', 'mov edi,DWORD PTR [ebx+0xb]', 'imul esi,DWORD PTR ds:0x10000400',
                                   'add esi,0x80', 'sar esi,0x8', 'imul edi,DWORD PTR ds:0x10000400', 'add edi,0x80', 'sar edi,0x8',
                                   'cmp ecx,0x4', 'jmp DWORD PTR ds:0x10001ffc'])
            # H writes only ESI and EDI (ECX, the argument count the final cmp reads, is untouched); G only ESI and ECX.
            self.assertEqual({d.split(' ', 1)[1].split(',')[0] for d in aim if d.split(' ')[0] in ('mov', 'imul', 'add', 'sar')}, {'esi', 'edi'})
            self.assertEqual({d.split(' ', 1)[1].split(',')[0] for d in decode(objdump, directory, 'overlay', g, 0x10000000)
                              if d.split(' ')[0] in ('mov', 'imul', 'add', 'sar')}, {'esi', 'ecx'})
            for name, blob in (('overlay', g), ('aim', h)):
                mnemonics = {d.split(' ')[0] for d in decode(objdump, directory, name, blob, 0x10000000)}
                self.assertFalse(mnemonics & {'pop', 'call', 'ret'} or any(m.startswith('f') for m in mnemonics), name)
            # I and J (the galaxy map): bytes, windows and addresses against the verifier; the stubs decoded and run.
            self.assertEqual([bytes.fromhex(l) for l in lines[42:44]], [verifier.SITES['map_label'][1], verifier.SITES['map_pick'][1]])
            self.assertEqual([bytes.fromhex(l) for l in lines[44:47]], [verifier.WINDOWS[w][1] for w in ('map_label_window', 'map_callback', 'map_pick_case')])
            self.assertEqual([int(v, 16) for v in lines[47].split()], [0x41eab2, 0x41eab8, 0x42e934, 0x42e952, 0x42e959, 0x42e949, 0x608508, 0x57fc64])
            ml, mp = bytes.fromhex(lines[48]), bytes.fromhex(lines[49])
            self.assertEqual((len(ml), len(mp)), (verifier.STUB_LENGTHS['map_label'], verifier.STUB_LENGTHS['map_pick']))
            label = decode(objdump, directory, 'map_label', ml, 0x10000000)
            rounding = ['imul DWORD PTR ds:0x10000200', 'cmp edx,0x80000000', 'adc eax,0x7fff', 'adc edx,0x0', 'shrd eax,edx,0x10']
            self.assertEqual(label, ['mov ecx,eax', 'mov eax,DWORD PTR [esp]', *rounding, 'mov DWORD PTR [esp],eax', 'mov eax,ecx', *rounding,
                                     'mov ecx,DWORD PTR ds:0x57fc64', 'jmp DWORD PTR ds:0x10001ffc'])
            pick = decode(objdump, directory, 'map_pick', mp, 0x10000000)
            self.assertEqual(pick, ['mov esi,DWORD PTR [esi+0x6]', 'imul esi,DWORD PTR ds:0x10000400', 'add esi,0x80', 'sar esi,0x8',
                                    'imul ecx,DWORD PTR ds:0x10000400', 'add ecx,0x80', 'sar ecx,0x8', 'mov DWORD PTR [esp+0xc],edi',
                                    'cmp DWORD PTR ds:0x608508,edi', 'jmp DWORD PTR ds:0x10001ffc'])
            for name, decoded in (('map_label', label), ('map_pick', pick)):
                mnemonics = {d.split(' ')[0] for d in decoded}
                self.assertFalse(mnemonics & {'push', 'pop', 'call', 'ret'} or any(m.startswith('f') or 'xmm' in d for m in mnemonics for d in decoded), name)
            # The label rounding: the C++ twin, the Python twin and the decoded stub agree at 1, 1.5, 2, 3 for positive and negative
            # offsets (a tie away from zero: +-3 at s = 2 give +-2), and s = 1 reproduces every input exactly, the 32-bit extremes included.
            twin = [int(v) for v in lines[50].split()]
            scales = (1.0, 1.5, 2.0, 3.0)
            self.assertEqual(twin, [verifier.map_offset(v, verifier.inverse16(s)) for s in scales for v in MAP_SAMPLES])
            self.assertEqual(twin[:len(MAP_SAMPLES)], MAP_SAMPLES)
            at2 = dict(zip(MAP_SAMPLES, twin[2 * len(MAP_SAMPLES):3 * len(MAP_SAMPLES)]))
            self.assertEqual([at2[v] for v in (1, -1, 3, -3, 5, -5)], [1, -1, 2, -2, 3, -3])
            at15 = dict(zip(MAP_SAMPLES, twin[len(MAP_SAMPLES):2 * len(MAP_SAMPLES)]))
            self.assertEqual([at15[v] for v in (90, -90, 231, -231, 2559, -2559)], [60, -60, 154, -154, 1706, -1706])
            vm, held = 0x0badf00d, {'ebx': 0x13, 'esi': 0x11111111, 'edi': 0x22222222, 'ebp': 0x33333333, 'esp': 0x0019f000}
            for s in scales:
                for x, y in zip(MAP_SAMPLES, reversed(MAP_SAMPLES)):
                    m = Stub({'eax': x & MASK, 'ecx': 0xdeadbeef, 'edx': 0xfeedface, **held},
                             {0x0019f000: y & MASK, 0x0019f004: 0x5555, 0x10000200: verifier.inverse16(s), 0x0057fc64: vm})
                    self.assertEqual(m.run(label), 'DWORD PTR ds:0x10001ffc')
                    self.assertEqual((s32(m.r['eax']), s32(m.m[0x0019f000])), (verifier.map_offset(x, verifier.inverse16(s)), verifier.map_offset(y, verifier.inverse16(s))), (s, x, y))
                    self.assertEqual(m.r['ecx'], vm)  # the displaced load, last
                    self.assertTrue(m.written <= {'eax', 'ecx', 'edx', 0x0019f000} and all(m.r[k] == v for k, v in held.items()) and m.m[0x0019f004] == 0x5555)
            # J: E's rounding on ESI (x) and ECX (y); the store; EDX, EAX, EBX, EDI, EBP untouched; the cmp of the map-state cell is
            # the last flag writer (only the jmp follows), so EFLAGS at the continuation equal the engine's cmp [0x608508],edi.
            self.assertEqual(Stub({}, {}).FLAG_WRITERS, ('imul', 'cmp', 'add', 'adc', 'sar', 'shrd'))
            for s in scales:
                for state in (0, 1, 0x80000000):
                    for x, y in ((0, 0), (1, -1), (3, -3), (-1, -1), (2047, 575), (-2400, 700)) + (((8388607, -8388608),) if s == 1.0 else ()):
                        regs = {'eax': 0x0a0a0a0a, 'ebx': 0x0b0b0b0b, 'ecx': y & MASK, 'edx': 0xfeedface, 'esi': 0x00500000, 'edi': 0,
                                'ebp': 0x0c0c0c0c, 'esp': 0x0019f000}
                        m = Stub(regs, {0x00500006: x & MASK, 0x10000400: verifier.fixed256(s), 0x00608508: state, 0x0019f00c: 0x77})
                        self.assertEqual(m.run(pick), 'DWORD PTR ds:0x10001ffc')
                        self.assertEqual((s32(m.r['esi']), s32(m.r['ecx'])), (verifier.cursor_real(x, verifier.fixed256(s)), verifier.cursor_real(y, verifier.fixed256(s))))
                        if s == 1.0:
                            self.assertEqual((s32(m.r['esi']), s32(m.r['ecx'])), (x, y))  # the identity (|v| < 2**23, two-operand imul)
                        self.assertEqual(m.m[0x0019f00c], 0)
                        self.assertTrue(m.written <= {'esi', 'ecx', 0x0019f00c} and all(m.r[k] == regs[k] for k in ('eax', 'ebx', 'edx', 'edi', 'ebp', 'esp')))
                        self.assertEqual(m.flags, m.subtract_flags(state, 0))
                        self.assertEqual(m.flag_writers[-1], pick.index('cmp DWORD PTR ds:0x608508,edi'))
                        self.assertEqual(pick[m.flag_writers[-1] + 1:], ['jmp DWORD PTR ds:0x10001ffc'])

    def test_rollback_all_or_none(self):
        # install_all / install_site / restore_all / any_live and the claim table compiled from ui_scale.cpp against a fake engine_patch:
        # every claim order, a failure at each of the twelve claims and each of four stages, a failed restore, the late window.
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        source = (ROOT / 'src/proxy/ui_scale.cpp').read_text()
        start = source.index('constexpr unsigned site_count')
        table = source[start:source.index('\n', source.index('bool patched_ = false;')) + 1]
        body = '\n'.join(extract(source, s) for s in ('const char* install_site(unsigned index)', 'bool restore_all()', 'bool any_live()',
                                                       'const char* install_all(double s)'))
        with tempfile.TemporaryDirectory(prefix='x3-ui-scale-rollback-') as temporary:
            cpp, exe = Path(temporary) / 'rollback.cpp', Path(temporary) / 'rollback'
            cpp.write_text(ROLLBACK_PREFIX + table + body + ROLLBACK_SUFFIX)
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-I', str(ROOT / 'src/proxy'),
                                    str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            names, result = run.stdout.splitlines()[-2:]
            self.assertEqual(names.split(), ['ui_scale_width', 'ui_scale_height', 'ui_scale_main_x', 'ui_scale_main_y', 'ui_scale_menu_x', 'ui_scale_menu_y',
                                             'ui_scale_cursor', 'ui_scale_overlay_icon', 'ui_scale_cursor_aim', 'ui_scale_map_label', 'ui_scale_map_pick',
                                             'ui_scale_projection'])
            self.assertRegex(result, r'^ui_scale_rollback checks=\d+ failures=0$')

    def test_python_twins_and_line_parsers(self):
        self.assertEqual([verifier.auto_scale(h) for h in (0, 720, 1080, 1200, 1440, 1600, 2160, 4320, 8640)], [1, 1, 1, 1, 1.5, 1.5, 2, 3, 3])
        self.assertEqual((verifier.inverse16(1.25), verifier.fixed256(1.25), verifier.virtual_size(5120, 52429), verifier.cursor_real(2048, 320)),
                         (52429, 320, 4096, 2560))
        self.assertEqual(verifier.mouse_step(-1, 43691, 0), (-1, 21845))
        self.assertEqual(verifier.mouse_step(-1, 43691, verifier.ACCUMULATOR_START), (-1, 54613))
        self.assertEqual(verifier.parse_camera_line('ui_scale_camera frame=7 excluded_camera=0badf00c previous=00000000'),
                         {'frame': 7, 'camera': 0x0badf00c, 'previous': 0})
        line = ('ui_scale setting=auto status=pending reason=pending mode=auto scale=1.0000 diagnostic=active diagnostic_write=atomic '
                'site=004be246')
        self.assertEqual(verifier.parse_log_line(line), {'setting': 'auto', 'status': 'pending', 'reason': 'pending', 'mode': 'auto', 'scale': 1.0,
                                                         'diagnostic': 'active', 'diagnostic_write': 'atomic', 'site': 0x4be246})
        install = ('ui_scale_install width=5120 height=1440 scale=1.2500 status=patched reason=ok failed_site=- mode=auto virtual=4096x1152 '
                   'inverse16=52429 fixed256=320 writes=plain,atomic,plain,plain,atomic,atomic,plain,plain arena_used=1234')
        parsed = verifier.parse_install_line(install)
        self.assertEqual((parsed['width'], parsed['height'], parsed['scale'], parsed['status'], parsed['vw'], parsed['vh'], parsed['inverse16'],
                          parsed['fixed256'], parsed['writes'].count('atomic'), parsed['arena']), (5120, 1440, 1.25, 'patched', 4096, 1152, 52429, 320, 3, 1234))
        frame = 'ui_scale_frame frame=300 frames=300 scale=1.2500 scaled=57 excluded=9 excluded_camera=0badf00c cameras=0badf00c:9/0,1c0ffee0:57/812'
        self.assertEqual(verifier.parse_frame_line(frame), {'frame': 300, 'frames': 300, 'scale': 1.25, 'scaled': 57, 'excluded': 9,
                                                            'camera': 0x0badf00c, 'cameras': {0x0badf00c: (9, 0), 0x1c0ffee0: (57, 812)}})
        self.assertEqual(verifier.parse_frame_line('ui_scale_frame frame=600 frames=300 scale=1.0000 scaled=0 excluded=0 excluded_camera=00000000 cameras=-')['cameras'], {})
        self.assertEqual(verifier.parse_restore_line('ui_scale_restore status=restored registered=0 diagnostic=none'),
                         {'status': 'restored', 'registered': 0, 'diagnostic': 'none'})
        self.assertIsNone(verifier.parse_log_line('ui_scale_install width=1 height=1'))

    def test_production_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertEqual(capture.count('ui_scale::initialize();'), 1)
        self.assertLess(capture.index('fov::initialize();'), capture.index('ui_scale::initialize();'))
        self.assertLess(capture.index('ui_scale::initialize();'), capture.index('sun_flare_fix::initialize();'))
        self.assertLess(capture.index('submit_phases::initialize();'), capture.index('ui_scale::initialize();'))  # the shared entry bytes
        self.assertEqual(capture.count('ui_scale::device_created(p->BackBufferWidth,p->BackBufferHeight);'), 1)
        self.assertLess(capture.index('cull_small_parts::set_backbuffer_width(p->BackBufferWidth);'), capture.index('ui_scale::device_created('))
        self.assertLess(capture.index('ui_scale::device_created('), capture.index('log("create_device_result hr=%08lx",hr);'))
        self.assertEqual(capture.count('ui_scale::after_reset(p?p->BackBufferWidth:0u,p?p->BackBufferHeight:0u);'), 1)
        self.assertEqual(capture.count('ui_scale::present(ctx.frame);'), 1)
        self.assertLess(capture.index('close_install_window("first_present");'), capture.index('ui_scale::present(ctx.frame);'))
        self.assertIn('if (reserved == nullptr) x3m::ui_scale::shutdown();', source_text(ROOT / 'src/proxy/loader.cpp'))
        self.assertIn('src/proxy/ui_scale.cpp', source_text(ROOT / 'CMakeLists.txt'))
        module = source_text(ROOT / 'src/proxy/ui_scale.cpp')
        for needle in ('L"X3M_UI_SCALE"', '"too_long"', '"invalid_setting"', '"out_of_range"', '"executable_mismatch"', '"projection_mismatch"',
                       '"caller_mismatch"', '"size_case_mismatch"', '"mouse_mismatch"', '"cursor_mismatch"', '"late_claim"', '"arena_full"',
                       '"chain_failed"', '"readback_failed"', '"rollback_failed"', '"pin_failed"', '"off_auto"', 'pin_self()',
                       'install_window_open()', 'executable_verified()', 'engine_patch::claim(site_[index],spec)', 'engine_patch::restore(site_[index]);',
                       'engine_patch::store_pointer(slot, continuation)', 'engine_patch::push_front(site_[index],reinterpret_cast<void*>(stub))',
                       'set_identity();', 'restore_all();', 'sites::auto_scale(height)', 'log_tier::cached_debug', 'GET_MODULE_HANDLE_EX_FLAG_PIN',
                       'log_handle()', 'WriteFile(handle', 'SetLastError(error);', 'x3m::engine_memory::read', '"patched_unverified"',
                       'sites::encode_projection_stub(', 'sites::encode_size_stub(', 'sites::encode_main_mouse_stub(', 'sites::encode_menu_mouse_stub(',
                       'sites::encode_cursor_stub(', 'sites::encode_entry_stub(', 'sites::encode_overlay_stub(', 'sites::encode_aim_stub(',
                       '"click_mismatch"', '"map_mismatch"', 'sites::encode_map_label_stub(', 'sites::encode_map_pick_stub(', 'c.bypass ? reinterpret_cast<void*>(c.va + c.length) : *site_[index].entry',
                       '*slot != continuation', 'if (log_tier::debug() && requested_)', 'frame % report_window == 0',
                       'constexpr unsigned report_window = 300;', 'if (applied) refresh_excluded_camera(0);',
                       'if (patched_) refresh_excluded_camera(frame);', 'log("ui_scale_camera frame=%llu excluded_camera=%08lx previous=%08lx"',
                       'acc = sites::accumulator_start;'):
            self.assertIn(needle, module)
        self.assertNotIn('claims[index].va + sites::mouse_site_length', module)  # never a code address as the jmp's memory operand
        # The twelve claims in the documented order, the map pair before the projection, the projection last.
        order = [module.index(f'"ui_scale_{n}"') for n in ('width', 'height', 'main_x', 'main_y', 'menu_x', 'menu_y', 'cursor', 'overlay_icon',
                                                            'cursor_aim', 'map_label', 'map_pick', 'projection')]
        self.assertEqual(order, sorted(order))
        header = source_text(ROOT / 'src/proxy/ui_scale_sites.h')
        self.assertNotIn('windows.h', header)
        self.assertIn('constexpr float anchor_x_table[4]={0.0f,-1.0f,1.0f,-1.0f};', header)
        self.assertIn('constexpr float anchor_y_table[4]={0.0f,1.0f,-1.0f,1.0f};', header)
        # The schema entry and the template.
        schema = source_text(ROOT / 'tools/config/schema.py')
        self.assertIn("entry('ui_scale', 'float', 'camera'", schema)
        self.assertIn("'auto', range=(1.0, 3.0), choices=('auto',), launcher='--ui-scale')", schema)  # auto since 0.9.0
        self.assertIn(';ui_scale = auto', (ROOT / 'assets/x3m.ini').read_text())
        self.assertIn('"X3M_UI_SCALE", "ui_scale", Type::Float, "auto"', (ROOT / 'src/config/config_schema_inc.h').read_text())


@unittest.skipUnless(EXE.is_file(), 'installed executable not present')
class UiScaleSite(unittest.TestCase):
    def run_verifier(self, exe):
        return subprocess.run([sys.executable, str(ROOT / 'verification/probe/verify_ui_scale_sites.py'), '--exe', str(exe)],
                              capture_output=True, text=True, timeout=600, cwd=ROOT / 'verification/probe')

    def test_installed_executable(self):
        run = self.run_verifier(EXE)
        self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(report['result'], 'PASS')
        self.assertEqual(len(report['checks']), 92)
        self.assertEqual(report['site_bytes'], {name: b.hex() for name, (_, b, _, _) in verifier.SITES.items()})
        self.assertEqual((report['raw_branch_hits_not_interior'], report['branches_into_spans'], report['dword_refs'], report['jump_table_entries_inside'],
                          report['overlapping_claims'], report['xmm_in_site_functions'], report['projection_register_writes']), ([], [], [], [], [], [], []))
        self.assertEqual(report['branches_to_site_starts']['entry'], ['0x47e002', '0x47e70c'])  # the two callers, no other caller
        self.assertEqual(sum(len(v) for k, v in report['branches_to_site_starts'].items() if k != 'entry'), 0)
        self.assertEqual(report['claims_in_windows'], [['submit_phase_world_return_a', '0x47e007'], ['submit_phase_world_return_b', '0x47e711']])
        self.assertEqual(report['entry_claims'], ['submit_phase_world_enter'])
        self.assertEqual(report['projection_following'], ['pop', 'pop', 'pop', 'pop', 'add', 'ret'])
        self.assertEqual(report['cursor_aim_following'][0], 'jl')  # the live-EFLAGS consumer right after the H span
        self.assertEqual(report['ins_dispatcher_claims'], [['chase_mode_script', '0x42e742'], ['fov_sites:setfocus_site_va', '0x42dbf8']])
        self.assertTrue(report['checks']['aim_flags_live_proof'] and report['checks']['click_sites_one_qword'])
        self.assertEqual(report['map_label_following'][0], 'push')  # x, pushed by the engine after the span
        self.assertEqual([m for _, m, _ in report['map_pick_between_cmp_and_je']], ['mov'] * 4)  # nothing between the cmp and the je writes EFLAGS
        self.assertEqual(report['map_state_text_hits'], ['0x41e414', '0x41e72d', '0x41eb9c', '0x42e94b', '0x42eaf4'])
        self.assertEqual(report['map_label_edx_readers'], [])
        self.assertTrue(all(report['checks'][k] for k in ('map_pick_flags_live_proof', 'map_label_edx_dead', 'map_label_flags_dead', 'map_label_slot_is_y',
                                                          'map_pick_edx_reaches_callees', 'map_sites_one_qword', 'map_state_references')))
        self.assertGreater(report['instructions'], 8000)

    def test_changed_site_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ('projection', 'cursor', 'cursor_aim', 'map_label', 'map_pick'):
                copy = Path(directory) / 'X3AP.exe'
                copy.write_bytes(verifier.patched_image(EXE.read_bytes(), name))
                report = json.loads(self.run_verifier(copy).stdout)
                self.assertEqual(report['result'], 'FAIL', name)
                self.assertFalse(report['checks'][f'{name}_bytes'], name)
                self.assertTrue(report['checks']['exe_identity'])


class UiScaleLaunchOption(unittest.TestCase):
    NAME = 'X3M_UI_SCALE'

    def launch(self, directory, *args, inherited=None, vanilla=False):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        if not vanilla:
            (game / 'd3d9.dll').write_bytes(b'proxy')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(game), *args]
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

    def test_default_explicit_values_vanilla_and_player_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.value(directory), 'auto')  # the default since 0.9.0
            self.assertEqual(self.value(directory, inherited={self.NAME: '2'}), 'auto')  # a stale value never travels
            self.assertEqual(self.value(directory, '--ui-scale', 'auto'), 'auto')
            self.assertEqual(self.value(directory, '--ui-scale', '1.5'), '1.5')
            self.assertEqual(self.value(directory, '--ui-scale', '1.25', inherited={self.NAME: 'auto'}), '1.25')
            self.assertEqual(self.value(directory, '--ui-scale', '2.0'), '2')
            self.assertEqual(self.value(directory, '--ui-scale', '3'), '3')
            self.assertEqual(self.value(directory, '--ui-scale', '1'), '1')
            self.assertEqual(self.value(directory, '--ui-scale', '1.33333'), '1.3333')
            self.assertIsNone(self.value(directory, vanilla=True, inherited={self.NAME: '2'}))
            self.assertIsNone(self.value(directory, '--config'))  # player mode: the default is not sent
            self.assertEqual(self.value(directory, '--config', '--ui-scale', 'auto'), 'auto')

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, vanilla, message in ((('--ui-scale', '1.5'), True, 'cannot be combined with --vanilla'),
                                           (('--ui-scale', 'auto'), True, 'cannot be combined with --vanilla'),
                                           (('--ui-scale', '5'), False, 'out of range'),
                                           (('--ui-scale', '0.99'), False, 'out of range'),
                                           (('--ui-scale', '3.001'), False, 'out of range'),
                                           (('--ui-scale', 'Auto'), False, 'out of range'),
                                           (('--ui-scale', 'nan'), False, 'out of range'),
                                           (('--ui-scale', '2x'), False, 'out of range')):
                with self.subTest(args=args, vanilla=vanilla):
                    code, _, error = self.launch(directory, *args, vanilla=vanilla)
                    self.assertNotEqual(code, 0)
                    self.assertIn(message, error)

    def test_launch_command_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            default = json.loads(self.launch(directory)[1])
            scaled = json.loads(self.launch(directory, '--ui-scale', '1.5')[1])
            self.assertEqual(default['command'], scaled['command'])
            self.assertEqual({k: v for k, v in scaled['env'].items() if default['env'].get(k) != v}, {self.NAME: '1.5'})


if __name__ == '__main__':
    unittest.main()
