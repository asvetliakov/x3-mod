"""Host checks of the text-density option (src/proxy/text_density_sites.h, src/proxy/text_density.cpp, tools/manage.py
--text-density and the density-font install step).

The core header compiled on the host (every byte window against the verifier's table, the option parser, the density
mapping, the font-file rewrite, the Materials row plan, the engine-twin destination clip, the shadow budget, the
diagnostic set), the site verifier on the installed executable and its refusal on a copy with a site byte changed, the
log-row parsers, the production wiring, the schema entry, the --text-density launcher option (--dry-run only, never a
launch) and the font copy/removal on a temporary game directory. No Wine.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_text_density_sites as verifier  # noqa: E402
from source_text import source_text  # noqa: E402

EXE = Path(verifier.DEFAULT_EXE)
WINDOW_ORDER = ['font_window', 'materials_window', 'materials_callee', 'style_window', 'blt_block_window', 'config_read', 'row_flag_test',
                'row_count_read', 'entry_flag_test', 'lookup', 'alloc', 'alloc_fields', 'free', 'copy', 'colour_copy', 'alpha_leaf',
                'text_line_window', 'rect_fill_window', 'config_default_store', 'config_atol_store']

HARNESS = r'''
#include "text_density_sites.h"
#include <cstdio>
#include <cstring>
using namespace x3m::text_density::sites;
static void hex(const unsigned char* p, unsigned n) { for (unsigned i = 0; i < n; ++i) std::printf("%02x", p[i]); std::printf("\n"); }
int main(int argc, char** argv) {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    // 0..19 the windows in WINDOW_ORDER
    hex(expected_font_window, font_window_length); hex(expected_materials_window, materials_window_length);
    hex(expected_materials_callee, materials_callee_length); hex(expected_style_window, style_window_length);
    hex(expected_blit_window, blit_window_length); hex(expected_config_read, config_read_length); hex(expected_row_flag_test, row_flag_test_length);
    hex(expected_row_count_read, row_count_read_length); hex(expected_entry_flag_test, entry_flag_test_length); hex(expected_lookup, lookup_length);
    hex(expected_alloc, alloc_length); hex(expected_alloc_fields, alloc_fields_length); hex(expected_free, free_length); hex(expected_copy, copy_length);
    hex(expected_colour_copy, colour_copy_length); hex(expected_alpha_leaf, alpha_leaf_length); hex(expected_text_line_window, text_line_window_length);
    hex(expected_rect_fill_window, rect_fill_window_length); hex(expected_config_default_store, config_store_length);
    hex(expected_config_atol_store, config_store_length);
    // 20 the addresses
    std::printf("%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", (unsigned long)font_site_va, (unsigned long)font_return_va,
                (unsigned long)materials_site_va, (unsigned long)materials_target_va, (unsigned long)materials_return_va, (unsigned long)style_write_va,
                (unsigned long)blt_block_va, (unsigned long)blt_alpha_va, (unsigned long)text_line_va, (unsigned long)rect_fill_va,
                (unsigned long)config_slot_va, (unsigned long)table_slot_va);
    // 21 the parser, 22 the density mapping
    unsigned v = 0;
    const char* texts[] = {"", "auto", "1", "2", "3", "4", "0", "Auto", "2.0", "auto ", "12"};
    for (const char* t : texts) std::printf("%d:%u ", (int)parse_setting(t, &v), v);
    std::printf("\n");
    check(parse_setting(L"auto", &v) == Parse::automatic && parse_setting(L"3", &v) == Parse::value && v == 3 &&
          parse_setting(static_cast<const char*>(nullptr), &v) == Parse::automatic, "wide and null parser");
    const double scales[] = {1.0, 1.25, 1.5, 2.0, 2.01, 3.0, 0.5};
    for (double s : scales) std::printf("%u ", density_for(Parse::automatic, 1, s));
    std::printf("| %u %u %u %u %u\n", density_for(Parse::value, 1, 2.0), density_for(Parse::value, 2, 1.0), density_for(Parse::value, 3, 1.0),
                density_for(Parse::value, 0, 1.0), density_for(Parse::value, 9, 1.0));
    // 23 the font files
    char path[font_path_capacity];
    check(font_file("Tahoma", 13, 2, ".abc", path) && !std::strcmp(path, "f\\Tahoma26.abc"), "Tahoma26.abc");
    std::printf("%s ", path);
    check(font_file("Zekton", 26, 3, ".tga", path) && !std::strcmp(path, "f\\Zekton78.tga"), "Zekton78.tga");
    std::printf("%s ", path);
    check(font_file("ZektonES", 26, 2, ".abc", path) && !std::strcmp(path, "f\\ZektonES52.abc"), "ZektonES52.abc");
    std::printf("%s ", path);
    check(font_file("Harrier", 24, 2, ".tga", path) && !std::strcmp(path, "f\\Harrier48.tga"), "Harrier48.tga");
    std::printf("%s\n", path);
    check(!font_file("", 13, 2, ".abc", path) && !font_file("a\\b", 13, 2, ".abc", path) && !font_file("a/b", 13, 2, ".abc", path) &&
          !font_file("Tahoma", 0, 2, ".abc", path) && !font_file("Tahoma", 256, 2, ".abc", path) && !font_file("Tahoma", 13, 4, ".abc", path) &&
          !font_file("Ta homa", 13, 2, ".abc", path) && !font_file("abcdefghijklmnopqrstuvwxyzabcdefg", 13, 2, ".abc", path), "refused names");
    check(font_file("abcdefghijklmnopqrstuvwxyzabcdef", 255, 3, ".abc", path) && std::strlen(path) == 2 + 32 + 3 + 4, "the longest name");
    // 24 the row plan on the shipped flag combinations
    const RowPlan a = plan_row(0x8c0000, true), b = plan_row(0x8c0100, true), c = plan_row(0x8c0100, false), d = plan_row(0x10000, true),
                  e = plan_row(0x8d0000, true), f = plan_row(0x8d0100, true), g = plan_row(0x800000, true), h = plan_row(0x40000, true),
                  i = plan_row(0x810000, false);
    std::printf("%d:%lx %d:%lx %d:%lx %d:%lx %d:%lx %d:%lx %d:%lx %d:%lx %d:%lx\n", (int)a.edit, (unsigned long)a.flags, (int)b.edit, (unsigned long)b.flags,
                (int)c.edit, (unsigned long)c.flags, (int)d.edit, (unsigned long)d.flags, (int)e.edit, (unsigned long)e.flags, (int)f.edit,
                (unsigned long)f.flags, (int)g.edit, (unsigned long)g.flags, (int)h.edit, (unsigned long)h.flags, (int)i.edit, (unsigned long)i.flags);
    // 25 the clip twin
    int sx, sy, dx, dy, w, hh;
    auto run = [&](int W, int H, int a1, int a2, int a3, int a4, int a5, int a6) {
        sx = a1; sy = a2; dx = a3; dy = a4; w = a5; hh = a6;
        const bool drawn = clip_destination(W, H, &sx, &sy, &dx, &dy, &w, &hh);
        std::printf("%d,%d,%d,%d,%d,%d,%d ", (int)drawn, sx, sy, dx, dy, w, hh);
    };
    run(256, 256, 0, 0, -10, 5, 20, 20); run(256, 256, 0, 0, 250, 250, 20, 20); run(256, 256, 0, 0, 300, 0, 20, 20); run(256, 256, 0, 0, 0, -30, 20, 20);
    run(256, 256, 3, 4, 10, 10, 20, 20); run(256, 256, 0, 0, -5, -5, 300, 300); run(128, 64, 0, 0, 0, 64, 10, 10);
    std::printf("\n");
    // 26 the shadow budget, 27 the diagnostic set
    std::uint32_t bytes = 0;
    check(shadow_fits(512, 512, 2, 0, &bytes) && bytes == 4u << 20, "512x512 at 2");
    check(!shadow_fits(4096, 4096, 2, 0, &bytes), "a side over the maximum");
    check(!shadow_fits(512, 512, 2, shadow_budget_bytes - (4u << 20) + 1, &bytes), "over the budget");
    check(shadow_fits(512, 512, 2, shadow_budget_bytes - (4u << 20), &bytes), "exactly the budget");
    check(!shadow_fits(0, 512, 2, 0, &bytes) && !shadow_fits(512, 512, 0, 0, &bytes) && !shadow_fits(512, 512, 4, 0, &bytes), "empty or bad density");
    std::printf("%u %u %lu\n", shadow_slots, shadow_max_side, (unsigned long)shadow_budget_bytes);
    DiagnosticKey table[diagnostic_rows];
    unsigned count = 0;
    check(diagnostic_first(table, &count, DiagnosticKey{1, 7, 15}) && !diagnostic_first(table, &count, DiagnosticKey{1, 7, 15}) &&
          diagnostic_first(table, &count, DiagnosticKey{2, 7, 15}) && diagnostic_first(table, &count, DiagnosticKey{1, 8, 15}) && count == 3, "diagnostic set");
    for (unsigned k = 0; k < diagnostic_rows + 4; ++k) diagnostic_first(table, &count, DiagnosticKey{3, (int)k, 0});
    check(count == diagnostic_rows, "bounded");
    std::printf("%u %u %u %u\n", frame_eax, frame_ecx, frame_return, frame_args);
    // 28, 29 the font gate against a game directory (argv[1]): the missing pairs at d = 2 and d = 3; 30 the texture-limit fit
    const char* game = argc > 1 ? argv[1] : ".";
    auto exists = [&](const char* relative) {
        char full[512];
        std::snprintf(full, sizeof full, "%s/%s", game, relative);
        for (char* p = full; *p; ++p) if (*p == '\\') *p = '/';
        if (FILE* f = std::fopen(full, "rb")) { std::fclose(f); return true; }
        return false;
    };
    char missing[missing_list_capacity];
    unsigned n2 = missing_fonts(2, exists, missing);
    std::printf("%u %s\n", n2, missing);
    unsigned n3 = missing_fonts(3, exists, missing);
    std::printf("%u %s\n", n3, missing);
    std::printf("%u %u %u %u %u %u\n", caps_density(3, 64, 2048, 4096, 4096), caps_density(3, 64, 2048, 8192, 8192), caps_density(3, 64, 2048, 0, 0),
                caps_density(2, 1024, 1024, 2048, 1024), caps_density(2, 1024, 1024, 2048, 2048), caps_density(3, 1024, 1024, 2048, 0));
    std::printf("text_density_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('text_density_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_schema():
    spec = importlib.util.spec_from_file_location('text_density_schema', ROOT / 'tools/config/schema.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TextDensityCore(unittest.TestCase):
    def test_core_compiled(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-text-density-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'text_density_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            # The gate's game directory: Tahoma26 and Zekton52 complete, ZektonES52 without its .tga, no Harrier, nothing at 3x.
            game = directory / 'game'
            (game / 'f').mkdir(parents=True)
            for name in ('Tahoma26.abc', 'Tahoma26.tga', 'Zekton52.abc', 'Zekton52.tga', 'ZektonES52.abc', 'Harrier72.abc', 'Harrier72.tga'):
                (game / 'f' / name).write_bytes(b'x')
            run = subprocess.run([str(executable), str(game)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            lines = run.stdout.splitlines()
            self.assertEqual(lines[-1], 'text_density_core checks_failed=0')
            self.assertEqual(lines[28], '2 ZektonES52,Harrier48')
            self.assertEqual(lines[29], '3 Tahoma39,Zekton78,ZektonES78')
            self.assertEqual(lines[30].split(), ['2', '3', '3', '1', '2', '2'])
            for name in ('ZektonES52.tga', 'Harrier48.abc', 'Harrier48.tga'):
                (game / 'f' / name).write_bytes(b'x')
            complete = subprocess.run([str(executable), str(game)], capture_output=True, text=True, timeout=60).stdout.splitlines()
            self.assertEqual(complete[28], '0 -')
            self.assertEqual([bytes.fromhex(l) for l in lines[0:20]], [verifier.WINDOWS[w][1] for w in WINDOW_ORDER])
            self.assertEqual([int(v, 16) for v in lines[20].split()],
                             [0x48cdc0, 0x48cdc6, 0x48af71, 0x4f44a0, 0x48af76, 0x4f813b, 0x48c090, 0x48c460, 0x48b2d0, 0x48b0b0, 0x606f34, 0x6069ac])
            parsed = dict(zip(['', 'auto', '1', '2', '3', '4', '0', 'Auto', '2.0', 'auto ', '12'], lines[21].split()))
            self.assertEqual(parsed, {'': '0:1', 'auto': '0:1', '1': '1:1', '2': '1:2', '3': '1:3', '4': '2:1', '0': '2:1', 'Auto': '2:1', '2.0': '2:1',
                                      'auto ': '2:1', '12': '2:1'})
            self.assertEqual(lines[22].split(), ['1', '2', '2', '2', '3', '3', '1', '|', '1', '2', '3', '1', '3'])
            self.assertEqual(lines[23].split(), ['f\\Tahoma26.abc', 'f\\Zekton78.tga', 'f\\ZektonES52.abc', 'f\\Harrier48.tga'])
            # flag (nofilter cleared), flag, flag (nofilter kept), unflag a file-backed row, already flagged, nofilter only, a generated
            # row without WRITEABLE untouched, WRITEABLE alone untouched, a generated non-writeable flagged row untouched
            self.assertEqual(lines[24].split(), ['1:8d0000', '1:8d0000', '1:8d0100', '2:0', '0:8d0000', '1:8d0000', '0:800000', '0:40000', '0:810000'])
            self.assertEqual(lines[25].split(), ['1,10,0,0,5,10,20', '1,0,0,250,250,6,6', '0,0,0,300,0,20,20', '0,0,0,0,-30,20,20', '1,3,4,10,10,20,20',
                                                 '1,5,5,0,0,295,295', '0,0,0,0,64,10,10'])  # the engine's own quirk: a rect that starts
                                                                                             # left of 0 is not clipped on the right
            self.assertEqual(lines[26].split(), ['16', '4096', str(96 << 20)])
            self.assertEqual(lines[27].split(), ['7', '6', '9', '10'])

    def test_python_twins_and_line_parsers(self):
        self.assertEqual(verifier.parse_setting('auto'), ('auto', 1))
        self.assertEqual(verifier.density_for('auto', 1, 1.25), 2)
        self.assertEqual(verifier.font_file('Tahoma', 13, 2, '.abc'), 'f\\Tahoma26.abc')
        self.assertEqual(verifier.plan_row(0x8c0100, True), ('flag', 0x8d0000))
        self.assertEqual(verifier.clip_destination(256, 256, 0, 0, -10, 5, 20, 20), (True, 10, 0, 0, 5, 10, 20))
        self.assertEqual(verifier.missing_fonts(2, {'f\\Tahoma26.abc', 'f\\Tahoma26.tga'}), ['Zekton52', 'ZektonES52', 'Harrier48'])
        self.assertEqual((verifier.caps_density(3, 64, 2048, 4096, 4096), verifier.caps_density(2, 1024, 1024, 2048, 1024)), (2, 1))
        line = ('text_density setting=auto status=pending reason=pending mode=auto value=1 diagnostic=1 failed_site=- '
                'writes=atomic,atomic,atomic,atomic,atomic,atomic site=0048cdc0')
        self.assertEqual(verifier.parse_log_line(line), {'setting': 'auto', 'status': 'pending', 'reason': 'pending', 'mode': 'auto', 'value': 1,
                                                         'diagnostic': 1, 'failed_site': '-', 'writes': 'atomic,atomic,atomic,atomic,atomic,atomic',
                                                         'site': 0x48cdc0})
        install = ('text_density_install density=2 scale=1.2500 status=patched reason=ok mode=auto config_before=1 style=stock style_write=none '
                   'rows=35/8/29 fonts=Tahoma26,Zekton52,ZektonES52,Harrier48 missing=- caps_limited_d=0 max_texture=16384x16384 arena_used=2345')
        parsed = verifier.parse_install_line(install)
        self.assertEqual((parsed['density'], parsed['scale'], parsed['status'], parsed['config_before'], parsed['rows'], parsed['fonts'], parsed['missing'],
                          parsed['caps_limited_d'], parsed['max_w'], parsed['arena']),
                         (2, 1.25, 'patched', 1, '35/8/29', 'Tahoma26,Zekton52,ZektonES52,Harrier48', '-', 0, 16384, 2345))
        refused = verifier.parse_install_line('text_density_install density=1 scale=1.2500 status=refused reason=fonts_missing mode=auto config_before=1 '
                                              'style=stock style_write=none rows=pending fonts=-0,-0,-0,-0 missing=ZektonES52,Harrier48 caps_limited_d=0 '
                                              'max_texture=0x0 arena_used=2345')
        self.assertEqual((refused['status'], refused['reason'], refused['missing']), ('refused', 'fonts_missing', 'ZektonES52,Harrier48'))
        self.assertEqual(verifier.parse_rows_line('text_density_rows status=applied reason=ok density=2 rows=1238 flagged=35 unflagged=8 nofilter_cleared=29 '
                                                  'caps_limited_d=2 largest_row=1024x2048 missing=-'),
                         {'status': 'applied', 'reason': 'ok', 'density': 2, 'rows': 1238, 'flagged': 35, 'unflagged': 8, 'nofilter': 29, 'caps_limited_d': 2,
                          'largest_w': 1024, 'largest_h': 2048, 'missing': '-'})
        self.assertEqual(verifier.parse_font_line('text_density_font name=Tahoma size=13 density=2 status=scaled file=f\\Tahoma26.abc cell_width=32 y_offset=2'),
                         {'name': 'Tahoma', 'size': 13, 'density': 2, 'status': 'scaled', 'file': 'f\\Tahoma26.abc', 'cell_width': 32, 'y_offset': 2})
        self.assertEqual(verifier.parse_shadow_line('text_density_shadow src=1300 status=built reason=ok size=512x512 density=2 bytes=4194304 total=4194304 slots=1 ms=7'),
                         {'src': 1300, 'status': 'built', 'reason': 'ok', 'w': 512, 'h': 512, 'density': 2, 'bytes': 4194304, 'total': 4194304, 'slots': 1, 'ms': 7})
        self.assertEqual(verifier.parse_draw_line('text_density_draw fn=blt_alpha src=1300 dst=15 dst_flagged=1 src_flagged=0 src_generated=0 handled=1'),
                         {'fn': 'blt_alpha', 'src': 1300, 'dst': 15, 'dst_flagged': 1, 'src_flagged': 0, 'src_generated': 0, 'handled': 1})
        self.assertEqual(verifier.parse_reset_line('text_density_reset shadows=2 bytes=8388608'), {'shadows': 2, 'bytes': 8388608})
        self.assertEqual(verifier.parse_restore_line('text_density_restore status=restored registered=0'), {'status': 'restored', 'registered': 0})
        self.assertIsNone(verifier.parse_log_line('text_density_install density=2'))

    def test_production_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertEqual(capture.count('text_density::initialize();'), 1)
        self.assertLess(capture.index('ui_scale::initialize();'), capture.index('text_density::initialize();'))
        self.assertLess(capture.index('text_density::initialize();'), capture.index('sun_flare_fix::initialize();'))
        self.assertEqual(capture.count('text_density::device_created(ui_scale::scale(),out?*out:nullptr);'), 1)
        self.assertLess(capture.index('ui_scale::device_created('), capture.index('text_density::device_created('))
        self.assertLess(capture.index('text_density::device_created('), capture.index('log("create_device_result hr=%08lx",hr);'))
        self.assertEqual(capture.count('text_density::before_reset();'), 1)
        self.assertLess(capture.index('text_density::before_reset();'), capture.index('presentation_parameters("reset_before"'))
        self.assertIn('if (reserved == nullptr) x3m::text_density::shutdown();', source_text(ROOT / 'src/proxy/loader.cpp'))
        self.assertIn('src/proxy/text_density.cpp', source_text(ROOT / 'CMakeLists.txt'))
        module = source_text(ROOT / 'src/proxy/text_density.cpp')
        for needle in ('L"X3M_TEXT_DENSITY"', '"too_long"', '"invalid_setting"', '"executable_mismatch"', '"font_mismatch"', '"materials_mismatch"',
                       '"style_mismatch"', '"blit_mismatch"', '"helper_mismatch"', '"diagnostic_mismatch"', '"config_mismatch"', '"no_game_dir"',
                       '"late_claim"', '"chain_failed"', '"readback_failed"', '"rollback_failed"', '"pin_failed"', '"density_1"', '"config_unwritable"',
                       '"style_failed"', '"objects_exist"', '"tables_unreadable"', 'pin_self()', 'install_window_open()', 'executable_verified()',
                       'engine_patch::claim(site_[index],spec)', 'engine_patch::restore(site_[index]);', 'engine_patch::claim_call(materials_site_,',
                       'engine_patch::restore_call(materials_site_);', 'engine_patch::push_front(site_[index],reinterpret_cast<void*>(c.thunk))',
                       '*c.continuation = reinterpret_cast<std::uintptr_t>(*site_[index].entry);', 'restore_all();', 'restore_config();', 'restore_style();',
                       'engine_patch::write_code(sites::style_write_va,bytes,4)', 'GetModuleFileNameW(nullptr', 'GetFileAttributesW(full)',
                       'GET_MODULE_HANDLE_EX_FLAG_PIN', 'log_handle()', 'WriteFile(handle', 'SetLastError(error);', 'x3m::engine_memory::read',
                       'log_tier::cached_debug', 'InterlockedExchange(&active_,', 'AcquireSRWLockExclusive(&shadow_lock_);', 'free_surface(&shadows_[i].object);',
                       'sites::clip_destination(object_width(dst_object)/d,object_height(dst_object)/d,', 'sites::plan_row(*row,clear_nofiltering)',
                       'sites::density_for(mode_,value_,scale_)', 'sites::shadow_fits(', 'sites::diagnostic_first(', '"fonts_missing"', '"caps"',
                       'fonts_missing(density_)', 'sites::missing_fonts(d,', 'SUCCEEDED(device->GetDeviceCaps(&limits))', 'sites::caps_density(density_,largest_w_,largest_h_,caps_w_,caps_h_)',
                       'const Tables t = live_tables();', 'remember_refused(src);', 'if (!found && !shadow_refused(src))',
                       'shadow_bytes_ = 0;\n    refused_count_ = 0;',
                       'write_config(1);\n        restore_all();',
                       'pushfd\n    pushad\n    cld\n    mov eax, esp\n    push eax\n    call _x3m_text_density_font_open\n    add esp, 4\n    popad\n    popfd\n'
                       '    jmp dword ptr [_x3m_text_density_font_continue]',
                       'pop dword ptr [_x3m_text_density_materials_return]\n    call dword ptr [_x3m_text_density_materials_continue]\n    pushfd\n    pushad\n'
                       '    cld\n    call _x3m_text_density_materials_loaded\n    popad\n    popfd\n    jmp dword ptr [_x3m_text_density_materials_return]',
                       'call _x3m_text_density_blit\n    add esp, 8\n    mov dword ptr [esp+28], eax\n    popad\n    popfd\n    test eax, eax\n    jnz 1f\n'
                       '    jmp dword ptr [_x3m_text_density_blt_block_continue]\n1:  ret',
                       'mov edi, dword ptr [ebp+16]\n    mov eax, dword ptr [ebp+12]\n    call dword ptr [ebp+8]\n    add esp, 28\n    pop edi',
                       'mov ecx, dword ptr [esp+4]\n    jmp dword ptr [_x3m_text_density_lookup_fn]',
                       'log("text_density_install density=%u scale=%.4f status=%s reason=%s mode=%s config_before=%lu style=%s style_write=%s rows=%s fonts=%s "\n'
                       '        "missing=%s caps_limited_d=%u max_texture=%ux%u arena_used=%u"',
                       'x3m::log("text_density_font name=%s size=%u density=%u status=%s file=%s cell_width=%lu y_offset=%lu"',
                       'x3m::log("text_density_rows status=%s reason=%s density=%u rows=%u flagged=%u unflagged=%u nofilter_cleared=%u caps_limited_d=%u "',
                       'x3m::log("text_density_shadow src=%ld status=%s reason=%s size=%ux%u density=%u bytes=%lu total=%lu slots=%u ms=%lu"',
                       'x3m::log("text_density_draw fn=%s src=%ld dst=%ld dst_flagged=%u src_flagged=%u src_generated=%u handled=%u"',
                       'log("text_density_reset shadows=%u bytes=%lu"', '"text_density_restore status=%s registered=%u\\n"'):
            self.assertIn(needle, module, needle)
        # The production claims in order: the font open, the two blits, then the Materials call; the diagnostics last.
        order = [module.index(f'"text_density_{n}"') for n in ('font', 'blt_block', 'blt_alpha', 'text_line', 'rect_fill')]
        self.assertEqual(order, sorted(order))
        self.assertNotIn('float ', module.split('namespace x3m::text_density {')[0].split('extern "C" {')[0])  # integer core; the scale is a double
        for forbidden in ('new ', 'malloc', 'std::string', 'std::vector'):
            self.assertNotIn(forbidden, module)
        header = source_text(ROOT / 'src/proxy/text_density_sites.h')
        self.assertNotIn('windows.h', header)
        self.assertIn('constexpr std::uint32_t mpf_nofiltering=0x100,mpf_fontscale=0x10000,mpf_writeable=0x40000,', header)
        # The fixtures that compile the capture wrappers stub the two entry points.
        self.assertIn('inline bool device_created(double, IDirect3DDevice9*) { return false; }',
                      (ROOT / 'verification/probe/capture_device_creation_fixture.cpp').read_text())
        # The fonts are removed by uninstall only (the def and that one call); rollback leaves them for the previous DLL.
        manage = source_text(ROOT / 'tools/manage.py')
        self.assertEqual(manage.count('= remove_density_fonts(game)'), 1)
        self.assertLess(manage.index("args.action == 'uninstall'"), manage.index('= remove_density_fonts(game)'))
        self.assertLess(manage.index('= remove_density_fonts(game)'), manage.index("args.action == 'rollback'"))
        self.assertIn("'density_fonts': fonts['files']", manage)
        self.assertIn('static void before_reset() noexcept {}', (ROOT / 'verification/probe/capture_bloom_lifetime_fixture.cpp').read_text())

    def test_schema_entry(self):
        entry = next(e for e in load_schema().SETTINGS if e['key'] == 'text_density')
        self.assertEqual((entry['env'], entry['type'], entry['section'], entry['default'], entry['choices'], entry['launcher'], entry['developer']),
                         ('X3M_TEXT_DENSITY', 'enum', 'camera', 'auto', ('auto', '1', '2', '3'), '--text-density', False))
        self.assertIn(';text_density = auto', source_text(ROOT / 'assets/x3m.ini'))
        self.assertIn('"X3M_TEXT_DENSITY", "text_density", Type::Enum, "auto"', (ROOT / 'src/config/config_schema_inc.h').read_text())
        self.assertIn('text_density', source_text(ROOT / 'docs/architecture/config-file.md'))


@unittest.skipUnless(EXE.is_file(), 'installed executable not present')
class TextDensitySite(unittest.TestCase):
    def run_verifier(self, exe):
        return subprocess.run([sys.executable, str(ROOT / 'verification/probe/verify_text_density_sites.py'), '--exe', str(exe)],
                              capture_output=True, text=True, timeout=600, cwd=ROOT / 'verification/probe')

    def test_installed_executable(self):
        run = self.run_verifier(EXE)
        self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(report['result'], 'PASS')
        self.assertEqual(len(report['checks']), 72)
        self.assertEqual(report['site_bytes'], {name: b.hex() for name, (_, b, _, _) in verifier.SITES.items()})
        self.assertEqual((report['raw_branch_hits_not_interior'], report['branches_into_spans'], report['dword_refs'], report['overlapping_claims'],
                          report['lookup_eax_reads_before_write']), ([], [], [], [], []))
        self.assertEqual(report['font_callers'], ['0x41c9ab', '0x41f7a7', '0x496131', '0x49615b'])
        self.assertEqual(report['materials_callers'], ['0x403497'])
        self.assertEqual(report['cockpit_init_callers'], ['0x403a26', '0x4050f4'])
        self.assertEqual(report['config_field_stores'], ['0x4ecae3', '0x4ecff8'])
        self.assertEqual([b[2] for b in report['init_backward_branches']], [True] * 4)
        self.assertEqual(report['blt_block_following'], ['test', 'push', 'push', 'push', 'push', 'jge'])
        self.assertEqual(report['font_following'], ['push', 'push', 'push', 'xor', 'call'])
        self.assertGreater(report['instructions'], 3500)

    def test_changed_site_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ('font', 'blt_alpha', 'rect_fill'):
                copy = Path(directory) / 'X3AP.exe'
                copy.write_bytes(verifier.patched_image(EXE.read_bytes(), name))
                report = json.loads(self.run_verifier(copy).stdout)
                self.assertEqual(report['result'], 'FAIL', name)
                self.assertFalse(report['checks'][f'{name}_bytes'], name)
                self.assertTrue(report['checks']['exe_identity'])


class TextDensityLaunchOption(unittest.TestCase):
    NAME = 'X3M_TEXT_DENSITY'

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
            self.assertEqual(self.value(directory), 'auto')
            self.assertEqual(self.value(directory, inherited={self.NAME: '3'}), 'auto')  # a stale value never travels
            self.assertEqual(self.value(directory, '--text-density', '2'), '2')
            self.assertEqual(self.value(directory, '--text-density', '3', inherited={self.NAME: '1'}), '3')
            self.assertEqual(self.value(directory, '--text-density', '1'), '1')
            self.assertEqual(self.value(directory, '--text-density', 'auto'), 'auto')
            self.assertEqual(self.value(directory, '--ui-scale', '1.25'), 'auto')
            self.assertIsNone(self.value(directory, vanilla=True, inherited={self.NAME: '2'}))
            self.assertIsNone(self.value(directory, '--config'))  # player mode: the default is not sent
            self.assertEqual(self.value(directory, '--config', '--text-density', '2'), '2')

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, vanilla, message in ((('--text-density', '2'), True, 'cannot be combined with --vanilla'),
                                           (('--text-density', 'auto'), True, 'cannot be combined with --vanilla'),
                                           (('--text-density', '4'), False, 'out of range'),
                                           (('--text-density', '0'), False, 'out of range'),
                                           (('--text-density', '2.0'), False, 'out of range'),
                                           (('--text-density', 'Auto'), False, 'out of range'),
                                           (('--text-density', ''), False, 'out of range')):
                with self.subTest(args=args, vanilla=vanilla):
                    code, _, error = self.launch(directory, *args, vanilla=vanilla)
                    self.assertNotEqual(code, 0)
                    self.assertIn(message, error)

    def test_launch_command_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            default = json.loads(self.launch(directory)[1])
            dense = json.loads(self.launch(directory, '--text-density', '3')[1])
            self.assertEqual(default['command'], dense['command'])
            self.assertEqual({k: v for k, v in dense['env'].items() if default['env'].get(k) != v}, {self.NAME: '3'})


class TextDensityFontInstall(unittest.TestCase):
    NAMES = ['Harrier48.abc', 'Harrier48.tga', 'Tahoma26.abc', 'Tahoma26.tga', 'Zekton52.abc', 'Zekton52.tga']

    def test_copy_manifest_retain_and_remove(self):
        module = load_manage()
        with tempfile.TemporaryDirectory() as directory:
            fonts = Path(directory) / 'build' / 'fonts' / 'F'
            fonts.mkdir(parents=True)
            for name in self.NAMES:
                (fonts / name).write_bytes(b'font:' + name.encode())
            (fonts / 'notes.txt').write_text('ignored')
            game = Path(directory) / 'game'
            (game / 'f').mkdir(parents=True)
            (game / 'f' / 'Zekton52.abc').write_bytes(b'the player\'s own file')
            self.assertEqual(module.density_font_files(fonts), self.NAMES)
            self.assertEqual(module.density_font_files(Path(directory) / 'absent'), [])
            self.assertEqual(module.install_density_fonts(game, Path(directory) / 'absent'), {'copied': [], 'retained': [], 'manifest': None, 'files': {}})
            self.assertFalse((game / 'f' / module.FONTS_MANIFEST).exists())
            first = module.install_density_fonts(game, fonts)
            self.assertEqual(first['retained'], ['Zekton52.abc'])
            self.assertEqual(first['copied'], [n for n in self.NAMES if n != 'Zekton52.abc'])
            self.assertEqual(first['files'], {n: hashlib.sha256(b'font:' + n.encode()).hexdigest() for n in first['copied']})
            self.assertEqual((game / 'f' / 'Zekton52.abc').read_bytes(), b'the player\'s own file')
            self.assertEqual((game / 'f' / 'Tahoma26.tga').read_bytes(), b'font:Tahoma26.tga')
            manifest = json.loads((game / 'f' / module.FONTS_MANIFEST).read_text())
            self.assertEqual(sorted(manifest['files']), first['copied'])
            self.assertEqual(manifest['files']['Tahoma26.abc'], hashlib.sha256(b'font:Tahoma26.abc').hexdigest())
            # A second install rewrites the owned files and still leaves the foreign one; a changed owned file is retained on removal.
            (fonts / 'Tahoma26.abc').write_bytes(b'font:Tahoma26.abc v2')
            second = module.install_density_fonts(game, fonts)
            self.assertEqual((second['copied'], second['retained']), (first['copied'], ['Zekton52.abc']))
            self.assertEqual((game / 'f' / 'Tahoma26.abc').read_bytes(), b'font:Tahoma26.abc v2')
            (game / 'f' / 'Harrier48.tga').write_bytes(b'edited by the player')
            removed = module.remove_density_fonts(game)
            self.assertEqual(removed, {'removed': ['Harrier48.abc', 'Tahoma26.abc', 'Tahoma26.tga', 'Zekton52.tga'], 'retained': ['Harrier48.tga']})
            self.assertEqual(sorted(p.name for p in (game / 'f').iterdir()), ['Harrier48.tga', 'Zekton52.abc'])
            self.assertEqual(module.remove_density_fonts(game), {'removed': [], 'retained': []})
            # A copy that fails half-way: the manifest already owns the file (empty hash), so nothing is left unowned.
            (game / 'f' / 'Harrier48.tga').unlink()
            original = module.shutil.copyfile

            def failing(src, dst):
                if Path(src).name == 'Tahoma26.abc':
                    raise OSError('disk full')
                return original(src, dst)
            with mock.patch.object(module.shutil, 'copyfile', side_effect=failing):
                with self.assertRaises(OSError):
                    module.install_density_fonts(game, fonts)
            owned = json.loads((game / 'f' / module.FONTS_MANIFEST).read_text())['files']
            self.assertEqual(sorted(owned), ['Harrier48.abc', 'Harrier48.tga', 'Tahoma26.abc'])
            self.assertEqual(owned['Tahoma26.abc'], '')
            # A partial file left by the interrupted copy is owned (empty hash) and overwritten by the next install.
            (game / 'f' / 'Tahoma26.abc').write_bytes(b'partial')
            retried = module.install_density_fonts(game, fonts)
            self.assertIn('Tahoma26.abc', retried['copied'])
            self.assertEqual(retried['retained'], ['Zekton52.abc'])
            self.assertEqual((game / 'f' / 'Tahoma26.abc').read_bytes(), b'font:Tahoma26.abc v2')
            self.assertEqual(json.loads((game / 'f' / module.FONTS_MANIFEST).read_text())['files']['Tahoma26.abc'],
                             hashlib.sha256(b'font:Tahoma26.abc v2').hexdigest())
            self.assertEqual(module.remove_density_fonts(game)['retained'], [])
            self.assertEqual(sorted(p.name for p in (game / 'f').iterdir()), ['Zekton52.abc'])


if __name__ == '__main__':
    unittest.main()
