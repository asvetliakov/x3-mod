#!/usr/bin/env python3
"""Proves the LightCallBoundary precondition on a built proxy DLL.

The motion route's hot setter hooks (src/proxy/capture.cpp, X3M_SHADOW_HOOK
instances using LightCallBoundary) preserve only MXCSR and the last error
around the native call, on the grounds that nothing on their path executes an
x87 instruction. This script disassembles the DLL, walks the static call graph
from each light hook over every function it can reach through direct calls
(indirect calls are the native vtable slot and Win32 imports, which the full
boundary does not cover either) and fails on any x87 opcode other than the
state-transport pairs the project itself uses (fnsave/frstor) and the SSE
control transfers (stmxcsr/ldmxcsr).

usage: check_no_x87.py [dll]   (default build/d3d9.dll); writes a JSON summary
to stdout and exits non-zero on a violation.
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OBJDUMP = 'i686-w64-mingw32-objdump'
LIGHT_HOOKS = ['set_vs', 'set_ps', 'set_vs_constant_f', 'set_vs_constant_i', 'set_ps_constant_f', 'set_viewport', 'set_render_state',
               'set_texture', 'set_sampler_state']  # the mip LOD bias's sampler shadow (X3M_TAA_MIP_BIAS)
# The gz read-ahead buffer's import entry points (src/proxy/loading_trace.cpp ->
# src/proxy/gz_buffer.cpp) run with no boundary at all on their fast path, so the
# same rule applies to them; the real zlib calls are indirect and stop the walk.
GZ_HOOKS = ['gz_read', 'gz_getc', 'gz_tell', 'gz_seek']
ALLOWED = {'fnsave', 'frstor', 'stmxcsr', 'ldmxcsr', 'fwait'}
FUNCTION = re.compile(r'^([0-9a-f]+) <(.+)>:$')
INSTRUCTION = re.compile(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} )+\s*([a-z][a-z0-9]*)\s*(.*)$')
DIRECT_CALL = re.compile(r'^(?:call|jmp)\s+[0-9a-f]+ <([^>]+)>')


def disassemble(dll):
    text = subprocess.run([OBJDUMP, '-d', '--no-show-raw-insn', '-w', str(dll)], check=True,
                          capture_output=True, text=True).stdout
    functions, name, body = {}, None, []
    for line in text.splitlines():
        m = FUNCTION.match(line)
        if m:
            if name is not None:
                functions[name] = body
            name, body = m.group(2), []
        elif name is not None and line.strip():
            body.append(line)
    if name is not None:
        functions[name] = body
    return functions


def parse(line):
    m = re.match(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)\s*(.*)$', line)
    return (m.group(1), m.group(2)) if m else (None, '')


def hook_symbol(functions, hook):
    # Anonymous-namespace stdcall functions: _ZN3x3m12_GLOBAL__N_1L<len><name>E...@<bytes>
    # (the L marks internal linkage; older GCC omits it).
    pattern = re.compile(r'_GLOBAL__N_1L?' + str(len(hook)) + re.escape(hook) + r'E')
    matches = [n for n in functions if pattern.search(n)]
    return matches


def walk(functions, roots):
    seen, stack = {}, list(roots)
    while stack:
        name = stack.pop()
        if name in seen or name not in functions:
            continue
        seen[name] = []
        for line in functions[name]:
            mnemonic, operands = parse(line)
            if not mnemonic:
                continue
            if mnemonic.startswith('f') and mnemonic not in ALLOWED:
                seen[name].append(line.strip())
            if mnemonic in ('call', 'jmp'):
                m = re.match(r'^[0-9a-f]+ <([^>]+)>', operands)
                if m:
                    target = m.group(1)
                    # PLT-style thunks to imports read as "<__imp_...>"; native
                    # slots are register/memory-indirect and never match here.
                    if not target.startswith('__imp_') and not target.startswith('*'):
                        stack.append(re.sub(r'[+-]0x[0-9a-f]+$', '', target))
    return seen


def main():
    dll = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/d3d9.dll'
    functions = disassemble(dll)
    roots = {}
    for hook in LIGHT_HOOKS + GZ_HOOKS:
        symbols = hook_symbol(functions, hook)
        if len(symbols) != 1:
            print(json.dumps({'result': 'FAIL', 'error': f'{hook}: {len(symbols)} symbols {symbols}'}))
            return 1
        roots[hook] = symbols[0]
    seen = walk(functions, roots.values())
    violations = {name: lines for name, lines in seen.items() if lines}
    summary = {'result': 'FAIL' if violations else 'PASS', 'dll': str(dll), 'roots': roots,
               'reachable_functions': len(seen), 'violations': violations}
    print(json.dumps(summary, indent=2))
    return 1 if violations else 0


if __name__ == '__main__':
    sys.exit(main())
