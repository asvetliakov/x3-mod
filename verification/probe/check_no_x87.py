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
# The CryptoAPI context/key cache's import entry points (loading_trace.cpp ->
# src/proxy/crypt_cache.cpp, X3M_CRYPT_CACHE=1) and the cache functions they
# reach: no boundary on the cached path either; the real ADVAPI32 calls are
# indirect through the bound originals and stop the walk.
CRYPT_HOOKS = ['crypt_acquire', 'crypt_release', 'crypt_import', 'crypt_key_destroy']
CRYPT_NAMESPACE = '__ZN3x3m11crypt_cache'   # x3m::crypt_cache::<name>
CRYPT_FUNCTIONS = ['acquire', 'release', 'import_key', 'destroy_key', 'shutdown', 'statistics']
# Light loading-trace rows (src/proxy/loading_trace_light.cpp, no CpuCallBoundary),
# the engine probe handlers and the resource reader's entry handler and .dat
# pool (src/proxy/resource_reader_core.cpp): the same rule, walked from their
# exact (namespace-qualified or extern "C") symbols.
LIGHT_LOADING_ROWS = ['file_open', 'file_read', 'file_seek', 'cursor_set', 'cursor_position', 'find_first', 'find_next', 'find_close',
                      'gz_open_traced', 'gz_read_traced', 'gz_seek_traced', 'gz_getc_traced', 'gz_tell_traced', 'gz_close_traced',
                      'gz_write', 'inflate_stream', 'xml_read', 'inflate_init2', 'inflate_end', 'crypt_acquire_context',
                      'crypt_release_context', 'crypt_import_key', 'crypt_create_hash', 'crypt_hash_data', 'crypt_verify_signature',
                      'crypt_get_hash_param', 'crypt_destroy_hash', 'crypt_destroy_key', 'create_directory', 'delete_file',
                      'move_file', 'move_file_ex', 'write_file', 'get_file_type', 'close_handle']
LIGHT_NAMESPACE = '__ZN3x3m13loading_trace5light'   # x3m::loading_trace::light::<name> (i386 PE: leading underscore)
EXTERN_ROOTS = ['_x3m_probe_enter', '_x3m_probe_exit', '_x3m_resource_read_entry', '_x3m_pool_fopen', '_x3m_pool_fclose']
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
    for hook in LIGHT_HOOKS + GZ_HOOKS + CRYPT_HOOKS:
        symbols = hook_symbol(functions, hook)
        if len(symbols) != 1:
            print(json.dumps({'result': 'FAIL', 'error': f'{hook}: {len(symbols)} symbols {symbols}'}))
            return 1
        roots[hook] = symbols[0]
    for row in LIGHT_LOADING_ROWS:
        prefix = f'{LIGHT_NAMESPACE}{len(row)}{row}E'
        symbols = [n for n in functions if n.startswith(prefix)]
        if len(symbols) != 1:
            print(json.dumps({'result': 'FAIL', 'error': f'light::{row}: {len(symbols)} symbols {symbols}'}))
            return 1
        roots['light::' + row] = symbols[0]
    for name in CRYPT_FUNCTIONS:
        prefix = f'{CRYPT_NAMESPACE}{len(name)}{name}E'
        symbols = [n for n in functions if n.startswith(prefix)]
        if len(symbols) != 1:
            print(json.dumps({'result': 'FAIL', 'error': f'crypt_cache::{name}: {len(symbols)} symbols {symbols}'}))
            return 1
        roots['crypt_cache::' + name] = symbols[0]
    for symbol in EXTERN_ROOTS:
        if symbol not in functions:
            print(json.dumps({'result': 'FAIL', 'error': f'{symbol}: not found'}))
            return 1
        roots[symbol] = symbol
    seen = walk(functions, roots.values())
    violations = {name: lines for name, lines in seen.items() if lines}
    summary = {'result': 'FAIL' if violations else 'PASS', 'dll': str(dll), 'roots': roots,
               'reachable_functions': len(seen), 'violations': violations}
    print(json.dumps(summary, indent=2))
    return 1 if violations else 0


if __name__ == '__main__':
    sys.exit(main())
