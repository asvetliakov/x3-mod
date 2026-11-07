#!/usr/bin/env python3
"""Build and audit the weapon-effects shield-hit hook fixture (the FL redirect fixture's native harness). Never runs
the game.

src/proxy/weapon_effects_patch.cpp is compiled unchanged with the proxy's flags plus `-include
weapon_effects_hook_fixture_shim.h -DX3M_WEAPON_EFFECTS_SHIM`, routing its read-back reads, rollback VirtualProtect and
restore_call through the fixture's one-shot fault seam; src/proxy/engine_patch.cpp (claim_call, the real write) and
src/proxy/weapon_effects_hook.cpp (the stub and the handler, integer only) are compiled as is, the handler TU with the
production CMake flags (-fno-exceptions -mno-sse -mno-mmx -mfpmath=387). The executable links at image base 0x00800000
so its own sections sit clear of the engine pages the fixture reserves at run time (0x00413000, 0x00449000,
0x00606000). The audit checks that weapon_effects_hook.o carries no x87/MMX opcode and no undefined symbol at all (no
CRT, no Win32 import: the brief's link check), that the two handler entry points _x3m_weapon_event / _x3m_weapon_tap
contain no call instruction (core::impact / core::tap and every helper inlined into this TU's own no-SSE code: no
COMDAT instance of another TU can be bound, review F10), that the impact stub is exactly pushf, push, push, push, cld, lea, push,
call, add, test, pop, pop, pop, jne, popf, jmp [continue], popf, xor, ret with `lea eax,[esp+0x10]`, that
engine_patch.o carries `lock cmpxchg8b`, and that the module routes through the seam (no VirtualProtect import of its
own). Step 4 (docs/architecture/bolt-muzzle-look.md): the bolt-tap stub has the tap's shape (pushf, push x3, cld, lea
eax,[esp+0x10], push, call, add, pop x3, popf, jmp [continue]), the flight stub the impact's, the muzzle stub passes EAX
and EDX (pushf, push x3, cld, push edx, push eax, lea eax,[esp+0x18], push, call, add esp,0xc, test, pop x3, jne, popf,
jmp [continue], popf, xor, ret), and the three handler entry points _x3m_weapon_bolt_tap / _x3m_weapon_muzzle /
_x3m_weapon_flight contain no call. Step 3 (docs/architecture/explosion-look.md): the two death stubs pass EBP (pushf, push
x3, cld, push ebp, lea eax,[esp+0x14], push, call, add esp,0x8, test, pop x3, jne, popf, jmp [continue], popf, xor,
ret), the handler entry points _x3m_weapon_death_area / _x3m_weapon_death_other contain no call, and the two death
windows of src/proxy/weapon_effects_death_sites.h equal the installed EXE's bytes (/mnt/c/X3/X3AP.exe, read through its
PE section table; recorded as death_windows_exe; an absent EXE fails the audit).
"""
import json
import re
import struct
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/weapon-effects-hook'
EXE = BUILD / 'weapon_effects_hook_fixture.exe'
SHIM = 'verification/probe/weapon_effects_hook_fixture_shim.h'
IMAGE_BASE = 0x800000
HANDLER_FLAGS = ('-fno-exceptions', '-mno-sse', '-mno-mmx', '-mfpmath=387')
STUB = ['pushf', 'push', 'push', 'push', 'cld', 'lea', 'push', 'call', 'add', 'test', 'pop', 'pop', 'pop', 'jne',
        'popf', 'jmp', 'popf', 'xor', 'ret']
TAP_STUB = ['pushf', 'push', 'push', 'push', 'cld', 'lea', 'push', 'call', 'add', 'pop', 'pop', 'pop', 'popf', 'jmp']
MUZZLE_STUB = ['pushf', 'push', 'push', 'push', 'cld', 'push', 'push', 'lea', 'push', 'call', 'add', 'test', 'pop', 'pop',
               'pop', 'jne', 'popf', 'jmp', 'popf', 'xor', 'ret']
DEATH_STUB = ['pushf', 'push', 'push', 'push', 'cld', 'push', 'lea', 'push', 'call', 'add', 'test', 'pop', 'pop', 'pop',
              'jne', 'popf', 'jmp', 'popf', 'xor', 'ret']
HANDLERS = ('_x3m_weapon_event', '_x3m_weapon_tap', '_x3m_weapon_bolt_tap', '_x3m_weapon_muzzle', '_x3m_weapon_flight',
            '_x3m_weapon_death_area', '_x3m_weapon_death_other')
GAME_EXE = Path('/mnt/c/X3/X3AP.exe')
DEATH_SITES = ROOT / 'src/proxy/weapon_effects_death_sites.h'


def exe_bytes(path, va, n):
    """n bytes at virtual address va of a PE image (its section table; the preferred image base)."""
    d = path.read_bytes()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    sections, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
    for i in range(sections):
        vsize, vaddr, rsize, raw = struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8)
        if vaddr <= va - base < vaddr + max(vsize, rsize):
            off = va - base - vaddr + raw
            return d[off:off + n]
    raise ValueError(f'{va:#x} outside the image')


def death_windows_exe(path=GAME_EXE, header=DEATH_SITES):
    """The header's two death windows against the installed EXE: {'area': bool, 'other': bool}, None without it."""
    if not path.is_file():
        return None
    text = header.read_text()
    out = {}
    for name in ('area', 'other'):
        va = int(re.search(r'\b%s_window_va = (0x[0-9a-f]+)' % name, text).group(1), 16)
        body = re.search(r'expected_%s_window\[window_length\] = \{([^}]*)\}' % name, text).group(1)
        expected = bytes(int(v, 16) for v in re.findall(r'0x[0-9a-f]+', body))
        out[name] = exe_bytes(path, va, len(expected)) == expected
    return out
X87 = re.compile(r'^\s*[0-9a-f]+:\s+(f[a-z0-9]+|emms)\b|\b(st\(\d\)|mm[0-7])\b')


def tool(name, *args):
    return subprocess.run(['i686-w64-mingw32-' + name, *args], capture_output=True, text=True, check=True).stdout


def stub(listing, name):
    body = listing.split(f'<{name}>:', 1)[1].split('\n\n', 1)[0] if f'<{name}>:' in listing else ''
    rows = [m.groups() for m in (re.match(r'^\s*[0-9a-f]+:\s+([a-z]+)\s*(.*)$', line) for line in body.splitlines()) if m]
    end = next((i for i, (m, _) in enumerate(rows) if m == 'ret'), len(rows) - 1)
    return [m for m, _ in rows[:end + 1]], [o.strip() for _, o in rows[:end + 1]]


def stub_to_jmp(listing, name):
    """A stub that ends in its forwarding jmp (no ret): the rows up to the first jmp."""
    body = listing.split(f'<{name}>:', 1)[1].split('\n\n', 1)[0] if f'<{name}>:' in listing else ''
    rows = [m.groups() for m in (re.match(r'^\s*[0-9a-f]+:\s+([a-z]+)\s*(.*)$', line) for line in body.splitlines()) if m]
    end = next((i for i, (m, _) in enumerate(rows) if m == 'jmp'), len(rows) - 1)
    return [m for m, _ in rows[:end + 1]], [o.strip() for _, o in rows[:end + 1]]


def body(listing, name):
    """The instruction rows of one function of an objdump listing."""
    if f'<{name}>:' not in listing:
        return []
    return [line for line in listing.split(f'<{name}>:', 1)[1].split('\n\n', 1)[0].splitlines()
            if re.match(r'^\s*[0-9a-f]+:', line)]


def audit(objects):
    handler = tool('nm', str(objects['handler']))
    undefined = [line.split()[-1] for line in handler.splitlines() if re.match(r'^\s+U ', line)]
    hlist = tool('objdump', '-d', '-Mintel', '--no-show-raw-insn', str(objects['handler']))
    handler_x87 = [line.strip() for line in hlist.splitlines() if X87.search(line)]
    i_mn, i_ops = stub(hlist, '_x3m_weapon_effects_stub_impact')
    stub_ok = i_mn == STUB and i_ops[5] == 'eax,[esp+0x10]' and i_ops[15].startswith('DWORD PTR ds:')
    b_mn, b_ops = stub_to_jmp(hlist, '_x3m_weapon_effects_stub_bolt_tap')
    bolt_tap_ok = b_mn == TAP_STUB and b_ops[5] == 'eax,[esp+0x10]' and b_ops[13].startswith('DWORD PTR ds:')
    m_mn, m_ops = stub(hlist, '_x3m_weapon_effects_stub_muzzle')
    muzzle_ok = (m_mn == MUZZLE_STUB and m_ops[5] == 'edx' and m_ops[6] == 'eax' and m_ops[7] == 'eax,[esp+0x18]' and
                 m_ops[10] == 'esp,0xc' and m_ops[17].startswith('DWORD PTR ds:'))
    f_mn, f_ops = stub(hlist, '_x3m_weapon_effects_stub_flight')
    flight_ok = f_mn == STUB and f_ops[5] == 'eax,[esp+0x10]' and f_ops[15].startswith('DWORD PTR ds:')
    death_ok = {}
    for name in ('area', 'other'):
        d_mn, d_ops = stub(hlist, f'_x3m_weapon_effects_stub_death_{name}')
        death_ok[name] = (d_mn == DEATH_STUB and d_ops[5] == 'ebp' and d_ops[6] == 'eax,[esp+0x14]' and
                          d_ops[9] == 'esp,0x8' and d_ops[16].startswith('DWORD PTR ds:'))
    exe = death_windows_exe()
    handler_calls = {name: [line.strip() for line in body(hlist, name) if re.search(r'\bcall\b', line)]
                     for name in HANDLERS}
    handler_rows = {name: len(body(hlist, name)) for name in handler_calls}
    # The handler's own stack frame (its entry `sub esp,N`; the stub adds 0x20 and the return address): recorded, the
    # fixture measures the depth it reaches below the synthetic frame. The first rows, not only the first two: GCC may
    # shrink-wrap the frame past the mode_native early return (the 1024-record ring's layout, 2026-11-07).
    entry = body(hlist, '_x3m_weapon_event')[:8]
    frame = [int(m.group(1), 16) for m in (re.search(r'sub\s+esp,0x([0-9a-f]+)', line) for line in entry) if m]
    module = tool('nm', str(objects['module']))
    module_own_protect = [l.strip() for l in module.splitlines() if re.search(r'__imp__VirtualProtect@', l)]
    patch = tool('objdump', '-d', '--no-show-raw-insn', str(objects['patch']))
    record = {'handler_undefined': undefined, 'handler_x87': handler_x87, 'handler_calls': handler_calls,
              'handler_instructions': handler_rows, 'handler_frame_bytes': frame[0] if frame else 0,
              'stub_impact': i_mn, 'stub_ok': stub_ok, 'stub_bolt_tap': b_mn, 'stub_bolt_tap_ok': bolt_tap_ok,
              'stub_muzzle': m_mn, 'stub_muzzle_ok': muzzle_ok, 'stub_flight': f_mn, 'stub_flight_ok': flight_ok,
              'stub_death_ok': death_ok, 'death_windows_exe': exe,
              'module_imports_own_protect': module_own_protect,
              'engine_patch_lock_cmpxchg8b': len(re.findall(r'lock cmpxchg8b', patch))}
    if (undefined or handler_x87 or not stub_ok or not bolt_tap_ok or not muzzle_ok or not flight_ok or
            not all(death_ok.values()) or exe is None or not all(exe.values()) or
            module_own_protect or record['engine_patch_lock_cmpxchg8b'] < 1 or
            any(handler_calls.values()) or not all(handler_rows.values())):
        raise RuntimeError('fixture build audit failed: ' + json.dumps(record))
    return record


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    objects = {}
    units = [('verification/probe/weapon_effects_hook_fixture.cpp', 'fixture', []),
             ('src/proxy/weapon_effects_patch.cpp', 'module', ['-include', str(ROOT / SHIM), '-DX3M_WEAPON_EFFECTS_SHIM']),
             ('src/proxy/weapon_effects_hook.cpp', 'handler', HANDLER_FLAGS),
             ('src/proxy/engine_patch.cpp', 'patch', [])]
    for source, stem, extra in units:
        flags = [f for f in FLAGS if f != '-fno-exceptions'] if stem == 'fixture' else list(FLAGS)
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *flags, *extra, '-c', str(ROOT / source), '-o', str(out)], check=True, cwd=ROOT)
        objects[stem] = out
    subprocess.run(['i686-w64-mingw32-g++', *(str(objects[k]) for k in ('fixture', 'module', 'handler', 'patch')),
                    '-static', '-static-libgcc', '-static-libstdc++', '-Wl,--strip-debug',
                    f'-Wl,--image-base,{IMAGE_BASE:#010x}', '-Wl,--disable-dynamicbase', '-Wl,--disable-reloc-section',
                    '-o', str(EXE)], check=True, cwd=ROOT)
    return {'binary': str(EXE.relative_to(ROOT)), 'toolchain': tool('g++', '--version').splitlines()[0], 'audit': audit(objects)}


if __name__ == '__main__':
    print(json.dumps(build(), indent=1))
