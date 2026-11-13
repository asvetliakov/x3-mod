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
own). Step 4 (docs/archive/effects-look-2026-11-08/bolt-muzzle-look.md): the bolt-tap stub has the tap's shape (pushf, push x3, cld, lea
eax,[esp+0x10], push, call, add, pop x3, popf, jmp [continue]), the flight stub the impact's, the muzzle stub passes EAX
and EDX (pushf, push x3, cld, push edx, push eax, lea eax,[esp+0x18], push, call, add esp,0xc, test, pop x3, jne, popf,
jmp [continue], popf, xor, ret), and the three handler entry points _x3m_weapon_bolt_tap / _x3m_weapon_muzzle /
_x3m_weapon_flight contain no call. Step 3: the two death stubs pass EBP (pushf, push x3, cld, push ebp, lea
eax,[esp+0x14], push, call, add esp,0x8, cmp eax,0x1, pop x3, je, popf, jmp [continue], popf, xor, ret; nothing after
the skip's ret since the big tiers' swap path left with the look layer on 2026-11-08), the handler entry points
_x3m_weapon_death_area / _x3m_weapon_death_other contain no call, and the two death windows of
src/proxy/weapon_effects_death_sites.h equal the installed EXE's bytes (/mnt/c/X3/X3AP.exe, read through its PE section
table; recorded as death_windows_exe; an absent EXE fails the audit). The fixture's cases are the hook fixture's own,
the step 4 and step 3 hook cases included (verification/probe/weapon_effects_hook_fixture.cpp).
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
DEATH_STUB = ['pushf', 'push', 'push', 'push', 'cld', 'push', 'lea', 'push', 'call', 'add', 'cmp', 'pop', 'pop', 'pop',
              'je', 'popf', 'jmp', 'popf', 'xor', 'ret']
# The reshade mode's batch scope stub (weapon_effects_batch_site.h, the AP twin of FL's): the batch's body id
# [[esp+8]+0x0c] read through a saved EAX ([esp+0x10] after pushf and push), the counters raised under a saved EFLAGS,
# the 12-byte pad and the four stack words re-pushed ([esp+0x1c] each), the call through the continue, the pad and words
# dropped (lea esp,[esp+0x1c]), the scope lowered, a plain ret.
BATCH_SCOPE_STUB = ['pushf', 'push', 'mov', 'mov', 'mov', 'pop', 'add', 'add', 'popf', 'lea', 'push', 'push', 'push',
                    'push', 'call', 'lea', 'pushf', 'sub', 'popf', 'ret']
HANDLERS = ('_x3m_weapon_event', '_x3m_weapon_tap', '_x3m_weapon_bolt_tap', '_x3m_weapon_muzzle', '_x3m_weapon_flight',
            '_x3m_weapon_death_area', '_x3m_weapon_death_other')
GAME_EXE = Path('/mnt/c/X3/X3AP.exe')
DEATH_SITES = ROOT / 'src/proxy/weapon_effects_death_sites.h'
BATCH_SITE = ROOT / 'src/proxy/weapon_effects_batch_site.h'


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


def batch_window_exe(path=GAME_EXE, header=BATCH_SITE):
    """The header's batch window against the installed EXE: True / False, None without the EXE."""
    if not path.is_file():
        return None
    text = header.read_text()
    va = int(re.search(r'\bwindow_va = (0x[0-9a-f]+)', text).group(1), 16)
    body = re.search(r'expected_window\[window_length\] = \{([^}]*)\}', text).group(1)
    expected = bytes(int(v, 16) for v in re.findall(r'0x[0-9a-f]+', body))
    return len(expected) == 19 and exe_bytes(path, va, len(expected)) == expected
X87 = re.compile(r'^\s*[0-9a-f]+:\s+(f[a-z0-9]+|emms)\b|\b(st\(\d\)|mm[0-7])\b')


def tool(name, *args):
    return subprocess.run(['i686-w64-mingw32-' + name, *args], capture_output=True, text=True, check=True).stdout


def stub(listing, name):
    body = listing.split(f'<{name}>:', 1)[1].split('\n\n', 1)[0] if f'<{name}>:' in listing else ''
    rows = [m.groups() for m in (re.match(r'^\s*[0-9a-f]+:\s+([a-z]+)\s*(.*)$', line) for line in body.splitlines()) if m]
    end = next((i for i, (m, _) in enumerate(rows) if m == 'ret'), len(rows) - 1)
    return [m for m, _ in rows[:end + 1]], [o.strip() for _, o in rows[:end + 1]]


def stub_tail(listing, name):
    """The instruction rows after a stub's first ret (none for every stub since the death swap path left)."""
    body = listing.split(f'<{name}>:', 1)[1].split('\n\n', 1)[0] if f'<{name}>:' in listing else ''
    rows = [m.groups() for m in (re.match(r'^\s*[0-9a-f]+:\s+([a-z]+)\s*(.*)$', line) for line in body.splitlines()) if m]
    rets = [i for i, (m, _) in enumerate(rows) if m == 'ret']
    return [m for m, _ in rows[rets[0] + 1:]] if rets else []


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
        tail = [m for m in stub_tail(hlist, f'_x3m_weapon_effects_stub_death_{name}') if m not in ('nop', 'xchg', 'lea')]
        death_ok[name] = (d_mn == DEATH_STUB and d_ops[5] == 'ebp' and d_ops[6] == 'eax,[esp+0x14]' and
                          d_ops[9] == 'esp,0x8' and d_ops[10] == 'eax,0x1' and d_ops[16].startswith('DWORD PTR ds:') and
                          not tail)
    s_mn, s_ops = stub(hlist, '_x3m_weapon_effects_stub_batch_scope')
    batch_ok = (s_mn == BATCH_SCOPE_STUB and s_ops[2] == 'eax,DWORD PTR [esp+0x10]' and
                s_ops[3] == 'eax,DWORD PTR [eax+0xc]' and s_ops[9] == 'esp,[esp-0xc]' and
                all(o == 'DWORD PTR [esp+0x1c]' for o in s_ops[10:14]) and s_ops[14].startswith('DWORD PTR ds:') and
                s_ops[15] == 'esp,[esp+0x1c]')
    batch_exe = batch_window_exe()
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
              'stub_batch_scope': s_mn, 'stub_batch_scope_ok': batch_ok, 'batch_window_exe': batch_exe,
              'module_imports_own_protect': module_own_protect,
              'engine_patch_lock_cmpxchg8b': len(re.findall(r'lock cmpxchg8b', patch))}
    if (undefined or handler_x87 or not stub_ok or not bolt_tap_ok or not muzzle_ok or not flight_ok or
            not all(death_ok.values()) or exe is None or not all(exe.values()) or not batch_ok or not batch_exe or
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
