"""The engine body table's by-name helpers (src/proxy/engine_body_resolver.h, moved out of the deleted lens-flare cull core
on 2026-11-02; engine_effects_core.h resolves engine_bodies.json through them).

Compiled on the host (no Windows dependency): the default-name parser, the engine's case-folding compare, the slot ->
id inverse of census::body_slot, the manager header read through a synthetic 32-bit image and the slot name pointer.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include "engine_body_resolver.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace x3m::engine_body_resolver;
static unsigned failed = 0;
static void expect(bool ok, const char* what) { if (!ok) { ++failed; std::printf("FAIL %s\n", what); } }
static unsigned char memory[0x60000];
static bool reader(std::uintptr_t address, void* out, std::size_t size) {
    if (address < 0x800 || address + size > sizeof memory) return false;
    std::memcpy(out, memory + address, size);
    return true;
}
static void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(memory + at, &v, 4); }
int main() {
    std::int32_t id = 0;
    expect(default_name_id("v\\00752", &id) && id == 752 && default_name_id("v\\11011", &id) && id == 11011, "default names parse");
    expect(!default_name_id("v\\0752", &id) && !default_name_id("ships\\x", &id) && !default_name_id("V\\00752", &id) &&
           !default_name_id("v\\007520", &id) && !default_name_id(nullptr, &id), "other forms refused");
    expect(name_equal("v\\01006", "V\\01006") && !name_equal("v\\01006", "v/01006") && !name_equal("v\\0100", "v\\01006"), "case fold, separators distinct");
    expect(slot_id(752, 11000) == 752 && slot_id(2000, 11000) == 11000 && slot_id(11002, 11000) == 20002, "slot -> id");
    for (std::int32_t probe : {0, 999, 10000, 19999, 20003}) {
        std::uint32_t slot = 0;
        expect(census::body_slot(probe, 11000, 4, &slot) && slot_id(slot, 11000) == probe, "slot_id inverts census::body_slot");
    }
    const std::uint32_t slots = 0x2000;
    put32(0x1000 + 0xb4, 11000); put32(0x1000 + 0xb8, 4); put32(0x1000 + 0xbc, slots);
    put32(0x0ffc, 0x1000);
    put32(slots + 753 * 0x1c + 0x0c, 0x50000);
    const Table t = read_table(&reader, 0x0ffc);
    expect(t.valid && t.manager == 0x1000 && t.fixed == 11000 && t.dynamic == 4 && t.slots == slots, "table header");
    std::uint32_t p = 0;
    expect(slot_name_pointer(&reader, t, 753, &p) && p == 0x50000, "slot name pointer");
    Table wrap = t; wrap.slots = 0xfffffff0u;
    expect(!slot_name_pointer(&reader, wrap, 1, &p), "a wrapping slot address refused");
    expect(!read_table(&reader, 0x0ff8).valid && !read_table(&reader, 0x5ffff).valid, "no manager or unreadable: invalid");
    put32(0x1000 + 0xb4, 11001);
    expect(!read_table(&reader, 0x0ffc).valid, "a fixed count other than 11000: invalid");
    std::printf("engine_body_resolver checks_failed=%u\n", failed);
    return failed ? 1 : 0;
}
'''


class EngineBodyResolver(unittest.TestCase):
    def test_core_compiled(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-engine-body-resolver-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'engine_body_resolver_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout.splitlines()[-1], 'engine_body_resolver checks_failed=0')

    def test_engine_effects_uses_it(self):
        core = (ROOT / 'src/proxy/engine_effects_core.h').read_text()
        self.assertIn('#include "engine_body_resolver.h"', core)
        self.assertNotIn('lens_flare_cull', core)


if __name__ == '__main__':
    unittest.main()
