#!/usr/bin/env python3
"""Build/qualify the actual read-only chase aim stubs on a synthetic x86 map.

--build-only never invokes Wine. Runtime use must be wrapped in wine_lock.py
and explicitly use X3M_FIXTURE_BOTTLE=X3. No mode starts the game.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/chase-aim-trace'
MODES = ('complete', 'bad0', 'bad1', 'bad2', 'bad3', 'late')
FLAGS = ('-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX')
SOURCES = ('CMakeLists.txt', 'src/proxy/chase_aim_trace.cpp', 'src/proxy/chase_aim_trace.h',
           'src/proxy/engine_patch.cpp', 'src/proxy/engine_patch.h',
           'src/proxy/engine_memory.cpp', 'src/proxy/engine_memory.h', 'src/proxy/cpu_state.h',
           'src/proxy/chase_camera.h', 'src/proxy/object_trace.h', 'src/proxy/telemetry.h',
           'src/proxy/capture.h', 'verification/probe/chase_aim_trace_fixture.cpp',
           'verification/probe/run_chase_aim_trace.py', 'verification/probe/bottle.py',
           'verification/probe/game_guard.py', 'verification/probe/wine_lock.py',
           'verification/probe/verify_chase_aim_sites.py', 'verification/probe/verify_chase_camera_site.py')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def audit_object(path, callback='x3m_chase_aim_enter'):
    listing = subprocess.run(['i686-w64-mingw32-objdump', '-t', str(path)], capture_output=True,
                             text=True, check=True).stdout
    forbidden = [line for line in listing.splitlines()
                 if any(name in line for name in ('_Unwind_', '__gxx_personality', '__cxa_throw'))]
    if forbidden:
        raise RuntimeError('exception runtime bookends outside CPU boundary: ' + '; '.join(forbidden))
    assembly = subprocess.run(['i686-w64-mingw32-objdump', '-dr', str(path)], capture_output=True,
                              text=True, check=True).stdout
    match = re.search(r'^\w+ <_' + re.escape(callback) + r'>:\n(.*?)(?=^\w+ <|\Z)', assembly, re.M | re.S)
    if not match:
        raise RuntimeError('CPU callback missing: ' + callback)
    body = match[1]
    markers = {name: len(re.findall(r'\b' + name + r'\b', body))
               for name in ('fnsave', 'frstor', 'stmxcsr', 'fninit', 'ldmxcsr', 'ret')}
    # Exact inventory derived from the CpuState contract (src/proxy/cpu_state.h)
    # plus the callback's own prologue; a lost fnsave or frstor must fail here.
    #   CpuState::capture (PreserveCpuState ctor): fnsave, fninit, stmxcsr
    #   callback prologue (after the envelope):    fninit, ldmxcsr 0x1f80
    #   CpuState::restore (PreserveCpuState dtor): frstor, ldmxcsr
    # call_preserved is not inlined into the callback, so it contributes nothing.
    if markers != {'fnsave': 1, 'frstor': 1, 'stmxcsr': 1, 'fninit': 2, 'ldmxcsr': 2, 'ret': 1}:
        raise RuntimeError('CPU boundary instruction inventory changed: ' + repr(markers))
    prefix = body[:body.index('fnsave')]
    if len(re.findall(r'\bcall\b', prefix)) != 1 or '__imp__GetLastError@0' not in prefix:
        raise RuntimeError('unexpected call before computational state capture')
    epilogue = body[body.index('SetLastError@4'):]
    epilogue = epilogue[:re.search(r'\bret\b', epilogue).end()]
    if re.search(r'\bcall\b', epilogue):
        raise RuntimeError('external call after LastError restoration')
    return {'no_exception_runtime_symbols': True, 'callback': callback,
            'boundary_instruction_inventory': markers, 'first_call': 'GetLastError',
            'last_epilogue_call': 'SetLastError', 'sha256': sha(path)}


def audit_fixture_image(path):
    """Fail closed before Wine: the loader owns the unchanged game site VAs."""
    data = Path(path).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[:2] != b'MZ' or data[pe:pe+4] != b'PE\0\0':
        raise RuntimeError('fixture is not PE')
    machine, count = struct.unpack_from('<HH', data, pe+4)
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    opt = pe+24
    magic = struct.unpack_from('<H', data, opt)[0]
    base = struct.unpack_from('<I', data, opt+28)[0]
    entry = base+struct.unpack_from('<I', data, opt+16)[0]
    size = struct.unpack_from('<I', data, opt+56)[0]
    dynamic = bool(struct.unpack_from('<H', data, opt+70)[0] & 0x40)
    if machine != 0x14c or magic != 0x10b or base != 0x400000 or dynamic:
        raise RuntimeError('fixture must be fixed-base x86 PE32 at 0x400000')
    sections = []
    for i in range(count):
        off = opt+optional_size+40*i
        name = data[off:off+8].rstrip(b'\0').decode('ascii')
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, off+8)
        characteristics = struct.unpack_from('<I', data, off+36)[0]
        sections.append(dict(name=name, va=base+rva, size=virtual_size,
                             raw_size=raw_size, raw=raw, characteristics=characteristics))
    maps = [s for s in sections if s['name'] == '.x3map']
    if len(maps) != 1:
        raise RuntimeError('synthetic PE section missing/duplicated')
    section = maps[0]
    if (section['va'], section['size']) != (0x401000, 0x21f000) or not section['characteristics'] & 0x80000000:
        raise RuntimeError('synthetic section address/extent/write permission changed')
    if section['characteristics'] & 0x20000000:
        raise RuntimeError('synthetic section must start non-executable')
    ordered = sorted((s for s in sections if s['size']), key=lambda s: s['va'])
    for left, right in zip(ordered, ordered[1:]):
        if left['va']+max(left['size'],left['raw_size']) > right['va']:
            raise RuntimeError('fixture sections overlap: '+left['name']+' / '+right['name'])
    raw_bytes = data[section['raw']:section['raw']+section['raw_size']]
    if len(raw_bytes) != section['raw_size'] or any(raw_bytes):
        raise RuntimeError('synthetic section contains nonzero fixture code/data')
    for s in sections:
        if s is section or not s['size']:
            continue
        if s['va'] < 0x620000 or s['va']+max(s['size'],s['raw_size']) > base+size:
            raise RuntimeError('real fixture section overlaps synthetic map/outside SizeOfImage: '+s['name'])
    text_section = [s for s in sections if s['name'] == '.text']
    if len(text_section) != 1 or not text_section[0]['va'] <= entry < text_section[0]['va']+text_section[0]['size']:
        raise RuntimeError('fixture entry is not in isolated real .text')
    from verify_chase_aim_sites import SITES, check_source_specs
    if not check_source_specs((ROOT/'src/proxy/chase_aim_trace.cpp').read_text())['ok']:
        raise RuntimeError('production site specs differ from independent site inventory')
    required = [(s.va, len(s.expected)+1) for s in SITES]
    required += [(0x606f38,4), (0x606fd4,4), (0x608504,4), (0x607ce8,12), (0x587b88,12)]
    if any(va < section['va'] or va+n > section['va']+section['size'] for va,n in required):
        raise RuntimeError('production site/global outside synthetic map')
    return dict(image_base=base, dynamic_base=dynamic, entry=entry, size_of_image=size,
                synthetic_va=section['va'], synthetic_size=section['size'],
                synthetic_initialized_bytes_zero=True, synthetic_initially_nonexecutable=True,
                sections_pairwise_nonoverlapping=True, real_sections_isolated=True,
                actual_production_addresses=True, required_ranges=required, sections=sections)


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    compiler = 'i686-w64-mingw32-g++'
    objects = []
    for source, stem in (('src/proxy/chase_aim_trace.cpp', 'production-audit'),
                         ('verification/probe/chase_aim_trace_fixture.cpp', 'fixture'),
                         ('src/proxy/engine_patch.cpp', 'engine-patch'),
                         ('src/proxy/engine_memory.cpp', 'engine-memory')):
        target = BUILD / (stem + '.o')
        flags = list(FLAGS)
        if stem == 'engine-memory':
            flags += ['-mno-sse', '-mno-mmx', '-mfpmath=387']
        subprocess.run([compiler, *flags, '-c', str(ROOT/source), '-o', str(target)], check=True, cwd=ROOT)
        if stem != 'production-audit':
            objects.append(target)
    audit = audit_object(BUILD/'production-audit.o')
    binary = BUILD/'chase_aim_trace_fixture.exe'
    subprocess.run([compiler, *map(str, objects), '-static', '-static-libgcc', '-static-libstdc++',
                    '-Wl,--image-base,0x00400000', '-Wl,--disable-dynamicbase',
                    '-Wl,--section-start,.x3map=0x00401000', '-Wl,--section-start,.text=0x00630000', '-o', str(binary)], check=True, cwd=ROOT)
    return binary, audit, audit_fixture_image(binary)


def parse(text, mode):
    match = re.search(r'^CHASE AIM RESULT mode=(\w+) checks=(\d+) failures=(\d+)\s*$', text, re.M)
    if not match or match[1] != mode or int(match[3]) or text.count('RESULT mode=') != 1:
        raise RuntimeError('missing/failing fixture result: ' + mode)
    rows = re.findall(r'^CHECK (.+) (PASS|FAIL)\r?$', text, re.M)
    if len(rows) != int(match[2]) or any(result != 'PASS' for _, result in rows):
        raise RuntimeError('incomplete/failing check inventory: ' + mode)
    return {'mode': mode, 'checks': int(match[2]), 'failures': 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--timeout', type=int, default=60)
    args = parser.parse_args()
    binary, audit, image_audit = build()
    summary = {'passed': False, 'game_launched': False, 'built': True, 'runtime_verified': False,
               'sources': {name: sha(ROOT/name) for name in SOURCES}, 'binary_sha256': sha(binary),
               'object_audit': audit, 'fixture_image_audit': image_audit, 'compile_flags': list(FLAGS)}
    if args.build_only:
        print(json.dumps(dict(summary, phase='build_only'), sort_keys=True))
        return 0
    import bottle
    from game_guard import game_running
    if bottle.BOTTLE != 'X3':
        raise RuntimeError('new qualification requires X3M_FIXTURE_BOTTLE=X3')
    if game_running():
        raise RuntimeError('game is running')
    results = bottle.results_dir(ROOT)
    summary['bottle'] = bottle.describe()
    modes = []
    for mode in MODES:
        if game_running():
            raise RuntimeError('game started before fixture: ' + mode)
        run = subprocess.run([bottle.WINE, *bottle.wine_args(), '--workdir', str(BUILD), str(binary), mode],
                             capture_output=True, text=True, timeout=args.timeout,
                             env=dict(os.environ, X3M_CAMERA='vanilla', X3M_TELEMETRY='0'))
        (results/f'chase-aim-{mode}.txt').write_text(run.stdout)
        (results/f'chase-aim-{mode}-wine.log').write_text(run.stderr)
        if run.returncode:
            raise RuntimeError(f'{mode} exited {run.returncode}; see fixture output')
        modes.append(parse(run.stdout, mode))
    final_sources = {name: sha(ROOT/name) for name in SOURCES}
    final_binary = sha(binary)
    if final_sources != summary['sources'] or final_binary != summary['binary_sha256']:
        raise RuntimeError('fixture source/binary changed during runtime qualification')
    summary.update(passed=True, runtime_verified=True, modes=modes,
                   post_runtime_sources=final_sources, post_runtime_binary_sha256=final_binary,
                   source_and_binary_stable_during_runtime=True)
    (results/'chase-aim-trace-summary.json').write_text(json.dumps(summary, indent=2, sort_keys=True)+'\n')
    print(json.dumps({'passed': True, 'cases': len(modes), 'checks': sum(x['checks'] for x in modes),
                      'summary': str(results/'chase-aim-trace-summary.json')}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
