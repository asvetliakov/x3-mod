#!/usr/bin/env python3
"""Build and audit the two partial-sun-occlusion fixtures. Never runs Wine.

  hook  sun_occlusion_hook_fixture.exe: the production hook module (src/proxy/sun_occlusion.cpp,
        production flags, -fno-exceptions) with engine_patch.cpp over two synthetic call sites.
        Audit: both thunks are the documented instruction sequences, and the probe handler's
        object code holds no x87 / MMX instruction and no floating-point arithmetic outside the
        FNSAVE / FRSTOR boundary of the two lens callbacks.
  gpu   sun_occlusion_fixture.exe: the production pass (src/renderer/sun_occlusion_pass.cpp with
        its embedded program) and the pixel wrap (src/renderer/lens_visibility_variant.cpp).
"""
import json
import re
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/sun-occlusion'
OBJDUMP = 'i686-w64-mingw32-objdump'
GPU_FLAGS = ('-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2')
PROBE_THUNK = ['pushf', 'cld', 'push', 'push', 'call', 'add', 'cmp', 'je', 'popf', 'ret', 'popf', 'jmp']
LENS_THUNK = ['pushf', 'cld', 'call', 'popf', 'push', 'call', 'add', 'pushf', 'cld', 'call', 'popf', 'ret']
NO_SSE = ['-mno-sse', '-mno-mmx', '-mfpmath=387']   # engine_memory.cpp's production flags (CMakeLists.txt)
BOUNDARY = {'fnsave', 'fninit', 'frstor', 'stmxcsr', 'ldmxcsr', 'fwait'}
FLOAT_RE = re.compile(r'^(f[a-z0-9]+|emms|(add|sub|mul|div|sqrt|max|min|cmp)[sp][sd]|cvt\w+|u?comis[sd])$')


def functions(obj):
    listing = subprocess.run([OBJDUMP, '-d', '--no-show-raw-insn', '-w', str(obj)], capture_output=True, text=True, check=True).stdout
    out, name = {}, None
    for line in listing.splitlines():
        head = re.match(r'^[0-9a-f]+ <([^>]+)>:', line)
        if head:
            name = head.group(1); out[name] = []
            continue
        row = re.match(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)', line)
        if row and name:
            out[name].append(row.group(1))
    return out


def audit_module(obj):
    code = functions(obj)
    strip = lambda rows: [m.rstrip('dw') if m.startswith(('pushf', 'popf')) else m for m in rows if m not in ('nop', 'lea', 'xchg', 'data16', 'cs', 'nopw', 'nopl')]
    if strip(code['_x3m_sun_probe_thunk']) != PROBE_THUNK or strip(code['_x3m_sun_lens_thunk']) != LENS_THUNK:
        raise RuntimeError('thunk shape changed: %s / %s' % (code['_x3m_sun_probe_thunk'], code['_x3m_sun_lens_thunk']))
    probe = [m for m in code['_x3m_sun_probe_decide'] if FLOAT_RE.match(m)]
    if probe:
        raise RuntimeError('floating point in the probe handler: %s' % probe[:6])
    outside = {name: [m for m in rows if FLOAT_RE.match(m) and m not in BOUNDARY] for name, rows in code.items()}
    outside = {k: v for k, v in outside.items() if v}
    if outside:
        raise RuntimeError('floating point outside the CPU-state boundary: %s' % outside)
    symbols = subprocess.run([OBJDUMP, '-t', str(obj)], capture_output=True, text=True, check=True).stdout
    if any(name in symbols for name in ('_Unwind_', '__gxx_personality', '__cxa_throw')):
        raise RuntimeError('exception runtime symbols in the hook module')
    return {'probe_thunk': PROBE_THUNK, 'lens_thunk': LENS_THUNK, 'probe_handler_float_free': True, 'module_float_free_outside_boundary': True,
            'no_exception_runtime_symbols': True, 'functions': len(code)}


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    objects = []
    for source, stem, extra in [('verification/probe/sun_occlusion_hook_fixture.cpp', 'hook_fixture', []), ('src/proxy/sun_occlusion.cpp', 'module', []),
                                ('src/proxy/engine_patch.cpp', 'patch', []), ('src/proxy/engine_memory.cpp', 'memory', NO_SSE)]:
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *FLAGS, *extra, '-c', str(ROOT / source), '-o', str(out)], check=True, cwd=ROOT)
        objects.append(out)
    audit = audit_module(BUILD / 'module.o')
    hook = BUILD / 'sun_occlusion_hook_fixture.exe'
    subprocess.run(['i686-w64-mingw32-g++', *map(str, objects), '-static', '-static-libgcc', '-static-libstdc++', '-o', str(hook)], check=True, cwd=ROOT)
    gpu = BUILD / 'sun_occlusion_fixture.exe'
    subprocess.run(['i686-w64-mingw32-g++', *GPU_FLAGS, '-static', str(ROOT / 'verification/probe/sun_occlusion_fixture.cpp'), str(ROOT / 'src/renderer/sun_occlusion_pass.cpp'),
                    str(ROOT / 'src/renderer/lens_visibility_variant.cpp'), '-o', str(gpu), '-luser32'], check=True, cwd=ROOT)
    return {'hook': str(hook), 'gpu': str(gpu), 'module_audit': audit, 'runtime': 'not run'}


if __name__ == '__main__':
    print(json.dumps(build(), indent=1))
