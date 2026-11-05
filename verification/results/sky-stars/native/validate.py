#!/usr/bin/env python3
"""Native D3D9 validator round trip of the sky-star programs (no device, no window, no game): the embedded words of
src/renderer/sky_stars_{,dim_}{vertex,pixel}_program_inc.h go through SysWOW64 d3d9.dll!Direct3DShaderValidatorCreate9 via the
WSL-interop harness verification/results/hull-widen-native-validator/native_validate.cpp, and their instruction slots
through the production counter src/renderer/ps3_program_slots.h. Writes result.json beside this script (names, sizes,
slot counts, the validator's End() result and messages only). Usage: python3 validate.py <scratch dir>
"""
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HARNESS = ROOT / 'verification/results/hull-widen-native-validator/native_validate.cpp'
PROGRAMS = {'sky_stars_vs': ROOT / 'src/renderer/sky_stars_vertex_program_inc.h',
            'sky_stars_ps': ROOT / 'src/renderer/sky_stars_pixel_program_inc.h',
            # Decision C2: the deferred batch's twins (SKY_STARS_DIM).
            'sky_stars_dim_vs': ROOT / 'src/renderer/sky_stars_dim_vertex_program_inc.h',
            'sky_stars_dim_ps': ROOT / 'src/renderer/sky_stars_dim_pixel_program_inc.h'}
SLOTS = r'''
#include "ps3_program_slots.h"
#include <cstdio>
#include <vector>
int main(int argc, char** argv) {
    for (int a = 1; a < argc; ++a) {
        std::FILE* f = std::fopen(argv[a], "rb");
        std::vector<std::uint32_t> w; std::uint32_t x;
        while (std::fread(&x, 4, 1, f) == 1) w.push_back(x);
        std::fclose(f);
        if (w[0] == 0xfffe0300u) w[0] = x3m::renderer::ps3_version_token; // the vs_3_0 twin, as the passes count it
        std::printf("%s slots=%u\n", argv[a], x3m::renderer::ps3_program_slots(w.data(), w.size()));
    }
}
'''


def words(header):
    return [int(v, 16) for v in re.findall(r'0x([0-9a-f]{8})u', header.read_text())]


def main():
    scratch = Path(sys.argv[1])
    scratch.mkdir(parents=True, exist_ok=True)
    paths = []
    for name, header in PROGRAMS.items():
        path = scratch / (name + '.bin')
        data = words(header)
        path.write_bytes(struct.pack('<%dI' % len(data), *data))
        paths.append(path)
    exe = scratch / 'native_validate.exe'
    subprocess.run(['i686-w64-mingw32-g++', '-O1', '-static', str(HARNESS), '-o', str(exe)], check=True)
    out = subprocess.run([str(exe), *[p.name for p in paths]], cwd=scratch, check=True, capture_output=True,
                         text=True).stdout
    (scratch / 'slots.cpp').write_text(SLOTS)
    subprocess.run(['g++', '-std=c++17', '-O1', '-I', str(ROOT / 'src/renderer'), str(scratch / 'slots.cpp'), '-o',
                    str(scratch / 'slots')], check=True)
    slots = {Path(k).name: v for k, v in re.findall(r'(\S+)\.bin slots=(\d+)', subprocess.run([str(scratch / 'slots'), *map(str, paths)],
                                                                         check=True, capture_output=True,
                                                                         text=True).stdout)}
    rows, current = [], None
    for line in out.splitlines():
        m = re.match(r'(\S+)\.bin words=(\d+)', line)
        if m:
            current = {'program': m.group(1), 'words': int(m.group(2)), 'messages': []}
            rows.append(current)
            continue
        m = re.search(r'msg instr=(-?\d+) id=(\d+): (.*)', line)
        if m:
            current['messages'].append({'instruction': int(m.group(1)), 'id': int(m.group(2)), 'text': m.group(3).strip()})
        m = re.search(r'end=(\w+) messages=(\d+)', line)
        if m:
            current['end'] = m.group(1)
    for row in rows:
        row['slots'] = int(slots[Path(row['program']).name])
        row['source_header'] = str(PROGRAMS[row['program']].relative_to(ROOT))
    result = {'validator': 'SysWOW64 d3d9.dll Direct3DShaderValidatorCreate9 (native Windows host, WSL interop, no device)',
              'harness': str(HARNESS.relative_to(ROOT)), 'slot_counter': 'src/renderer/ps3_program_slots.h',
              'passed': all(r.get('end') == '00000000' and not r['messages'] for r in rows), 'programs': rows}
    (Path(__file__).parent / 'result.json').write_text(json.dumps(result, indent=1) + '\n')
    for r in rows:
        print(r['program'], 'words', r['words'], 'slots', r['slots'], 'end', r.get('end'), 'messages', len(r['messages']))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
