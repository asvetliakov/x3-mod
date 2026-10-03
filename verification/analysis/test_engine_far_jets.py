"""Far engine jets (docs/architecture/engine-effects-modern.md "After flight E", review fixes): the portable core
src/proxy/engine_far_jets_core.h compiled on the host: the append's (node handle, view handle) dedupe (Seen) and the far
block's device count (Requests: armed while one device requests, the resolve's owner empties the buffer), and the
production wiring of both.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from source_text import source_text

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include "engine_far_jets_core.h"
#include <cstdio>
#include <memory>
using namespace x3m::engine_far_jets::core;
static unsigned failed = 0, checks = 0;
static void expect(bool ok, const char* what) { ++checks; if (!ok) { ++failed; std::printf("FAIL %s\n", what); } }
int main() {
    // ----------------------------------------------------------- dedupe
    std::unique_ptr<Seen> s(new Seen);
    s->begin();
    expect(s->insert(0xf9, 0x77) && !s->insert(0xf9, 0x77), "a repeated (node, view) pair is a duplicate");
    expect(s->insert(0xf9, 0x99) && s->insert(0xfa, 0x77), "another view or another node is new");
    expect(s->insert(0, 0x77) && s->insert(0, 0x77), "a zero node handle is never a duplicate");
    s->begin();
    expect(s->insert(0xf9, 0x77), "a new frame forgets the previous one");
    // A full frame: capacity distinct pairs (colliding low bits), then each again: every second insert a duplicate.
    s->begin();
    unsigned fresh = 0, dup = 0;
    for (unsigned i = 0; i < capacity; ++i) fresh += s->insert(0x1000u + (i << 11), 0x77);
    for (unsigned i = 0; i < capacity; ++i) dup += !s->insert(0x1000u + (i << 11), 0x77);
    expect(fresh == capacity && dup == capacity, "1,024 pairs a frame: all new once, all duplicates again");
    // The generation wrap clears the stamps once: nothing survives as a false duplicate.
    s->generation = 0xffffffffu;
    s->insert(0x55, 0x66);
    s->begin();
    expect(s->generation == 1 && s->insert(0x55, 0x66), "the generation wrap clears the stamps");
    // ----------------------------------------------------------- device count
    Requests r;
    bool a = false, b = false;
    const std::uintptr_t da = 0x1000, db = 0x2000;
    r.request(&a, true, da);
    r.request(&a, true, da);
    expect(r.devices == 1 && r.armed() && a, "a device counts once");
    r.request(&b, true, db);
    r.claim(db);
    expect(r.devices == 2 && r.clears(db) && !r.clears(da), "the resolve's owner alone empties the buffer");
    r.request(&a, false, da); // the old device's teardown after the new one configured itself
    expect(r.devices == 1 && r.armed() && r.clears(db), "an old device's teardown leaves the new one armed");
    r.request(&a, false, da);
    expect(r.devices == 1, "a withdrawal counts once");
    r.request(&b, false, db);
    expect(r.devices == 0 && !r.armed() && r.owner == 0 && r.clears(da), "the last withdrawal disarms and frees the buffer");
    r.request(&b, false, db);
    expect(r.devices == 0, "never below zero");
    expect(Requests{}.clears(da), "no owner: every requesting device empties the buffer");
    std::printf("engine_far_jets_core checks=%u failed=%u\n", checks, failed);
    return failed ? 1 : 0;
}
'''


def compiler():
    found = shutil.which('clang++') or shutil.which('c++')
    if found is None:
        raise unittest.SkipTest('A host C++ compiler is required')
    return found


class EngineFarJetsCore(unittest.TestCase):
    def test_core(self):
        with tempfile.TemporaryDirectory(prefix='x3-engine-far-jets-host-') as directory:
            source, executable = Path(directory) / 'harness.cpp', Path(directory) / 'engine_far_jets_host'
            source.write_text(HARNESS)
            built = subprocess.run([compiler(), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout)
        self.assertRegex(run.stdout.splitlines()[-1], r'^engine_far_jets_core checks=\d+ failed=0$')


class Wiring(unittest.TestCase):
    def test_wiring(self):
        motion = source_text(ROOT / 'src/proxy/motion_output.cpp')
        self.assertIn('engine_far_jets::request(&engine_far_counted_,false,this);', motion)  # ~MotionOutput
        self.assertIn('if(plumes_requested_)engine_far_jets::claim(this);', motion)  # the resolve
        self.assertNotIn('note_scene(', motion)
        plumes = source_text(ROOT / 'src/proxy/motion_output_engine_plumes_inc.h')
        self.assertIn('engine_far_jets::request(&engine_far_counted_,plumes_requested_,this);', plumes)
        self.assertIn('if(!engine_far_seen_.insert(f.handle,f.view_handle)){++engine_far_.duplicates;continue;}', plumes)
        self.assertIn('engine_ring_->scene[slot]=1u;', plumes)
        effects = source_text(ROOT / 'src/proxy/motion_output_engine_effects_inc.h')
        self.assertIn('if(plumes_requested_&&engine_far_jets::clears(this))engine_far_jets::begin_frame();', effects)


if __name__ == '__main__':
    unittest.main()
