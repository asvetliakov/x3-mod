#!/usr/bin/env python3
"""The weapon-effects ring's capacity-bound sizes in the i686 MinGW build (docs/verification/weapon-effects.md, "Ring
1024 and the reach cull"): sizeof Ring, HookState, HullSlot, HullState and the stack frames of stage::build /
stage::build_hull (-fstack-usage), and the DLL's .data / .bss virtual sizes (objdump -h), before and after.

  python3 verification/results/weapon-effects-ring/ring_sizes.py [--base 41fd2a4fb] [--before-dll D] [--after-dll D]
      > verification/results/weapon-effects-ring/ring_sizes_out.json

"before" compiles the base commit's src/ (git archive into a temporary directory) and reads the Run241 DLL (built from
that commit); "after" compiles this checkout's src/ and reads build-ring/d3d9.dll. Each side compiles one translation
unit with the DLL's flags and prints one JSON object with both sides and the static (.data + .bss) delta.
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TU = r'''
#include "weapon_effects_hull.h"
#include "weapon_effects_patch.h"
namespace ws = x3m::weapon_effects::stage;
namespace wc = x3m::weapon_effects::core;
extern "C" const unsigned x3m_sizes[] = {sizeof(wc::Ring), sizeof(wc::HookState), sizeof(ws::HullSlot), sizeof(ws::HullState),
                                         wc::ring_capacity};
bool rd(std::uintptr_t, void*, std::size_t) { return false; }
wc::Ring ring;
ws::HullState state;
ws::HullCounts hull(const ws::HullView& v, ws::HullBuffers& b) { return ws::build_hull(ring, state, &rd, v, ws::HullTuning{}, b); }
ws::Counts shield(const ws::Camera& c, x3m::renderer::WeaponPatch* p, x3m::renderer::WeaponDecal* d) {
    return ws::build(ring, 0, &rd, c, ws::Tuning{}, p, wc::max_patches, d, wc::max_decals);
}
'''


def sizes(src):
    with tempfile.TemporaryDirectory() as tmp:
        tu = Path(tmp) / 'sizes.cpp'
        tu.write_text(TU)
        obj = Path(tmp) / 'sizes.o'
        subprocess.run(['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-msse2', '-mfpmath=sse', '-mstackrealign',
                        '-mincoming-stack-boundary=2', '-fstack-usage', '-I', str(src / 'proxy'), '-c', str(tu), '-o',
                        str(obj)], check=True, cwd=tmp)
        dump = subprocess.run(['i686-w64-mingw32-objdump', '-s', '-j', '.rdata', str(obj)], capture_output=True,
                              text=True, check=True).stdout
        words = []
        for line in dump.splitlines():
            m = re.match(r'\s+[0-9a-f]+ ((?:[0-9a-f]{2,8} ?){1,4})', line)
            if m:
                for w in m.group(1).split():
                    words.append(int.from_bytes(bytes.fromhex(w.ljust(8, '0')), 'little'))
        su = (Path(tmp) / 'sizes.su').read_text()
    frames = {}
    for line in su.splitlines():
        for name in ('build_hull', 'build'):
            if ' x3m::weapon_effects::stage::' + name + '(' in line:
                frames[name] = max(frames.get(name, 0), int(line.split('\t')[1]))
    names = ('ring', 'hook_state', 'hull_slot', 'hull_state', 'ring_capacity')
    return {**dict(zip(names, words[:5])), 'stack_build_hull': frames.get('build_hull'), 'stack_build': frames.get('build')}


def dll_sections(dll):
    """The .data and .bss virtual sizes (objdump -h: the Size column is the section's virtual size)."""
    out = subprocess.run(['i686-w64-mingw32-objdump', '-h', str(dll)], capture_output=True, text=True,
                         check=True).stdout
    found = {}
    for line in out.splitlines():
        cols = line.split()
        if len(cols) >= 3 and cols[1] in ('.data', '.bss'):
            found['dll_' + cols[1][1:]] = int(cols[2], 16)
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', default='41fd2a4fb')
    ap.add_argument('--before-dll', default=str(Path.home() / 'x3-mod/build-run241/d3d9.dll'))
    ap.add_argument('--after-dll', default=str(ROOT / 'build-ring/d3d9.dll'))
    a = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        archive = subprocess.run(['git', '-C', str(ROOT), 'archive', a.base, 'src'], capture_output=True,
                                 check=True).stdout
        subprocess.run(['tar', '-x', '-C', tmp], input=archive, check=True)
        before = {'src': f'{a.base} src', **sizes(Path(tmp) / 'src')}
    after = {'src': 'checkout src', **sizes(ROOT / 'src')}
    before.update({'dll': a.before_dll, **dll_sections(a.before_dll)})
    after.update({'dll': str(Path(a.after_dll).relative_to(ROOT)) if Path(a.after_dll).is_relative_to(ROOT) else a.after_dll,
                  **dll_sections(a.after_dll)})
    delta = (after['dll_data'] + after['dll_bss']) - (before['dll_data'] + before['dll_bss'])
    print(json.dumps({'before': before, 'after': after, 'static_delta_bytes': delta}, indent=1))


if __name__ == '__main__':
    main()
