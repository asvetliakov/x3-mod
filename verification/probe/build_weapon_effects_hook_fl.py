#!/usr/bin/env python3
"""Build and audit the weapon-effects FL hook fixture (src/engine/fl/weapon_effects_sites_fl.h, the FL entry shims).
Never runs the game.

src/proxy/weapon_effects_patch.cpp is compiled with X3M_EDITION_FL and the proxy's flags plus `-include
weapon_effects_hook_fixture_shim.h -DX3M_WEAPON_EFFECTS_SHIM`, routing its read-back reads, rollback VirtualProtect and
restore_call through the fixture's one-shot fault seam; src/proxy/engine_patch.cpp and src/proxy/weapon_effects_hook.cpp
are compiled with X3M_EDITION_FL, the handler TU with the production CMake flags (-fno-exceptions -mno-sse -mno-mmx
-mfpmath=387). The audit checks that the FL handler object carries no x87/MMX opcode and no undefined symbol, that the
six FL handler entry points contain no call instruction, that each of the six FL stubs is the expected
pushf/save/handler/forward-or-skip skeleton ending in its tail jump or `xor eax,eax; ret` (and the projectile batch stub
its latch / range / bit test before the tail jump; the re-shade's batch scope stub its counted call and plain ret),
that engine_patch.o carries
`lock cmpxchg8b`, that the module routes through the seam (no VirtualProtect import of its own), and that every FL window
in weapon_effects_sites_fl.h equals the installed X3FL.exe's bytes with its relocated dword masked (/mnt/c/X3FL/X3FL.exe,
read through its PE section table; an absent EXE fails the audit).
"""
import json
import re
import struct
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/weapon-effects-hook-fl'
EXE = BUILD / 'weapon_effects_hook_fixture_fl.exe'
SHIM = 'verification/probe/weapon_effects_hook_fixture_shim.h'
IMAGE_BASE = 0x800000
HANDLER_FLAGS = ('-fno-exceptions', '-mno-sse', '-mno-mmx', '-mfpmath=387')
# Each stub: pushf, 3 saves, cld, optional extra pushes, lea/push, call, add, (test|cmp), 3 pops, (jnz|je), popf,
# jmp [continue], popf, xor, ret. The taps and object tap forward unconditionally (no skip tail).
IMPACT_STUB = ['pushf', 'push', 'push', 'push', 'cld', 'push', 'push', 'push', 'push', 'lea', 'push', 'call', 'add',
               'test', 'pop', 'pop', 'pop', 'jne', 'popf', 'jmp', 'popf', 'xor', 'ret']
TAP_STUB = ['pushf', 'push', 'push', 'push', 'cld', 'lea', 'push', 'call', 'add', 'pop', 'pop', 'pop', 'popf', 'jmp']
BOLT_TAP_STUB = ['pushf', 'push', 'push', 'push', 'cld', 'push', 'call', 'add', 'pop', 'pop', 'pop', 'popf', 'jmp']
MUZZLE_STUB = ['pushf', 'push', 'push', 'push', 'cld', 'push', 'push', 'lea', 'push', 'call', 'add', 'test', 'pop',
               'pop', 'pop', 'jne', 'popf', 'jmp', 'popf', 'xor', 'ret']
FLIGHT_STUB = ['pushf', 'push', 'push', 'push', 'cld', 'lea', 'push', 'call', 'add', 'test', 'pop', 'pop', 'pop', 'jne',
               'popf', 'jmp', 'popf', 'xor', 'ret']
DEATH_STUB = ['pushf', 'push', 'push', 'push', 'cld', 'push', 'push', 'push', 'lea', 'push', 'call', 'add', 'cmp',
              'pop', 'pop', 'pop', 'je', 'popf', 'jmp', 'popf', 'xor', 'ret']
# The projectile stage's batch submit (no handler: the latch, the body-id range and the enabled-set bit, the two tail
# jumps, then the skip path `add [skipped],1; popf; mov eax,1; ret`).
BATCH_STUB = ['pushf', 'cmp', 'je', 'push', 'mov', 'cmp', 'ja', 'bt', 'pop', 'jb', 'popf', 'jmp', 'pop', 'popf', 'jmp',
              'add', 'popf', 'mov', 'ret']
# The bullet re-shade's batch scope (reshade mode): the batch's body id [EDX+0x0c] published through a saved EAX and the
# counters raised under a saved EFLAGS, the 4-byte pad (lea) and the two stack words re-pushed, the call through the
# continue, the pad and words dropped (lea), the scope lowered, a plain ret.
BATCH_SCOPE_STUB = ['pushf', 'push', 'mov', 'mov', 'pop', 'add', 'add', 'popf', 'lea', 'push', 'push', 'call', 'lea',
                    'pushf', 'sub', 'popf', 'ret']
STUBS = {'_x3m_weapon_effects_stub_batch_fl': BATCH_STUB, '_x3m_weapon_effects_stub_batch_scope_fl': BATCH_SCOPE_STUB,
         '_x3m_weapon_effects_stub_impact_fl': IMPACT_STUB, '_x3m_weapon_effects_stub_tap_fl': TAP_STUB,
         '_x3m_weapon_effects_stub_bolt_tap_fl': BOLT_TAP_STUB, '_x3m_weapon_effects_stub_muzzle_fl': MUZZLE_STUB,
         '_x3m_weapon_effects_stub_flight_fl': FLIGHT_STUB, '_x3m_weapon_effects_stub_death_fl': DEATH_STUB}
HANDLERS = ('_x3m_weapon_event_fl', '_x3m_weapon_tap_fl', '_x3m_weapon_bolt_tap_fl', '_x3m_weapon_muzzle_fl',
            '_x3m_weapon_flight_fl', '_x3m_weapon_death_fl')
FL_EXE = Path('/mnt/c/X3FL/X3FL.exe')
SITES = ROOT / 'src/engine/fl/weapon_effects_sites_fl.h'
X87 = re.compile(r'^\s*[0-9a-f]+:\s+(f[a-z0-9]+|emms)\b|\b(st\(\d\)|mm[0-7])\b')


def exe_bytes(path, rva, n):
    """n bytes at RVA rva of a PE image (its section table)."""
    d = path.read_bytes()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    sections, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
    for i in range(sections):
        vsize, vaddr, rsize, raw = struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8)
        if vaddr <= rva < vaddr + max(vsize, rsize):
            off = rva - vaddr + raw
            return d[off:off + n]
    raise ValueError(f'rva {rva:#x} outside the image')


def site_windows():
    """The eight site windows of weapon_effects_sites_fl.h: name -> {rva, length, bytes (masked), fixup_offset}."""
    text = SITES.read_text()
    arrays = {m.group(1): bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', m.group(2)))
              for m in re.finditer(r'constexpr unsigned char (\w+)\[\d+\] = \{([^}]*)\}', text)}
    out = {}
    for row in re.finditer(r'\{\{"(\w+)", (0x[0-9a-f]+), (\d+), (\w+), (no_fixup|\d+), [^,]+, 0x[0-9a-f]+\}, \d+, 0x[0-9a-f]+\}', text):
        name, rva, length, var, fixoff = row.groups()
        out[name] = {'rva': int(rva, 16), 'length': int(length), 'bytes': arrays[var],
                     'fixup_offset': None if fixoff == 'no_fixup' else int(fixoff)}
    return out


def windows_match_exe(path=FL_EXE):
    """Every FL window's masked bytes equal the EXE's with its fixup dword zeroed: {name: bool}, None without the EXE."""
    if not path.is_file():
        return None
    out = {}
    for name, w in site_windows().items():
        live = bytearray(exe_bytes(path, w['rva'], w['length']))
        if w['fixup_offset'] is not None:
            live[w['fixup_offset']:w['fixup_offset'] + 4] = b'\0\0\0\0'
        out[name] = bytes(live) == w['bytes']
    return out


def tool(name, *args):
    return subprocess.run(['i686-w64-mingw32-' + name, *args], capture_output=True, text=True, check=True).stdout


def body(listing, name):
    if f'<{name}>:' not in listing:
        return []
    return [line for line in listing.split(f'<{name}>:', 1)[1].split('\n\n', 1)[0].splitlines()
            if re.match(r'^\s*[0-9a-f]+:', line)]


def stub(listing, name):
    rows = [m.group(1) for line in body(listing, name) if (m := re.match(r'^\s*[0-9a-f]+:\s+([a-z]+)', line))]
    # up to the first ret (skip tail) or, for the forward-only stubs, the first jmp
    if 'ret' in rows:
        end = rows.index('ret')
    elif 'jmp' in rows:
        end = rows.index('jmp')
    else:
        end = len(rows) - 1
    return rows[:end + 1]


def audit(objects):
    handler = tool('nm', str(objects['handler']))
    undefined = [line.split()[-1] for line in handler.splitlines() if re.match(r'^\s+U ', line)]
    hlist = tool('objdump', '-d', '-Mintel', '--no-show-raw-insn', str(objects['handler']))
    handler_x87 = [line.strip() for line in hlist.splitlines() if X87.search(line)]
    stubs = {name: stub(hlist, name) for name in STUBS}
    stub_ok = {name: stubs[name] == want for name, want in STUBS.items()}
    handler_calls = {name: [line.strip() for line in body(hlist, name) if re.search(r'\bcall\b', line)] for name in HANDLERS}
    handler_rows = {name: len(body(hlist, name)) for name in HANDLERS}
    # The impact handler's own frame (its entry `sub esp,N`): the fixture's STACK_DEPTH is this plus the impact stub's
    # 0x2c (return, EFLAGS and three saves, four inputs, the entry pointer, the handler call's return).
    entry = body(hlist, '_x3m_weapon_event_fl')[:8]
    frame = [int(m.group(1), 16) for m in (re.search(r'sub\s+esp,0x([0-9a-f]+)', line) for line in entry) if m]
    module = tool('nm', str(objects['module']))
    module_own_protect = [l.strip() for l in module.splitlines() if re.search(r'__imp__VirtualProtect@', l)]
    patch = tool('objdump', '-d', '--no-show-raw-insn', str(objects['patch']))
    exe = windows_match_exe()
    record = {'handler_undefined': undefined, 'handler_x87': handler_x87, 'handler_calls': handler_calls,
              'handler_instructions': handler_rows, 'handler_frame_bytes': frame[0] if frame else 0,
              'stubs': stubs, 'stub_ok': stub_ok,
              'windows_match_exe': exe, 'module_imports_own_protect': module_own_protect,
              'engine_patch_lock_cmpxchg8b': len(re.findall(r'lock cmpxchg8b', patch))}
    if (undefined or handler_x87 or not all(stub_ok.values()) or module_own_protect or
            record['engine_patch_lock_cmpxchg8b'] < 1 or any(handler_calls.values()) or
            not all(handler_rows.values()) or exe is None or not all(exe.values())):
        raise RuntimeError('fixture build audit failed: ' + json.dumps(record))
    return record


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    objects = {}
    units = [('verification/probe/weapon_effects_hook_fixture_fl.cpp', 'fixture', []),
             ('src/proxy/weapon_effects_patch.cpp', 'module', ['-include', str(ROOT / SHIM), '-DX3M_WEAPON_EFFECTS_SHIM']),
             ('src/proxy/weapon_effects_hook.cpp', 'handler', HANDLER_FLAGS),
             ('src/proxy/engine_patch.cpp', 'patch', [])]
    for source, stem, extra in units:
        flags = [f for f in FLAGS if f != '-fno-exceptions'] if stem == 'fixture' else list(FLAGS)
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *flags, '-DX3M_EDITION_FL=1', *extra, '-c', str(ROOT / source),
                        '-o', str(out)], check=True, cwd=ROOT)
        objects[stem] = out
    subprocess.run(['i686-w64-mingw32-g++', *(str(objects[k]) for k in ('fixture', 'module', 'handler', 'patch')),
                    '-static', '-static-libgcc', '-static-libstdc++', '-Wl,--strip-debug',
                    f'-Wl,--image-base,{IMAGE_BASE:#010x}', '-Wl,--disable-dynamicbase', '-Wl,--disable-reloc-section',
                    '-o', str(EXE)], check=True, cwd=ROOT)
    return {'binary': str(EXE.relative_to(ROOT)), 'toolchain': tool('g++', '--version').splitlines()[0], 'audit': audit(objects)}


if __name__ == '__main__':
    print(json.dumps(build(), indent=1))
