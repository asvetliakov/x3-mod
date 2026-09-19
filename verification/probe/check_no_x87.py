#!/usr/bin/env python3
"""Proves the LightCallBoundary precondition on a built proxy DLL.

The motion route's hot setter hooks (src/proxy/capture.cpp, X3M_SHADOW_HOOK
instances using LightCallBoundary) preserve only MXCSR and the last error
around the native call, on the grounds that nothing on their path executes an
x87 instruction. This script disassembles the DLL, walks the static call graph
from each light hook over every function it can reach through direct calls
(indirect calls are the native vtable slot, Win32 imports, which the full
boundary does not cover either, and x3m::call_preserved thunks, which run
their callee under a full FNSAVE/FRSTOR envelope: cpu_state.h) and fails on
any x87 opcode other than the state-transport instructions the project itself
uses (fnsave, the fninit after it in CpuState::capture, frstor) and the SSE
control transfers (stmxcsr/ldmxcsr). Any
indirect call, through a volatile function pointer or otherwise, is invisible
to this walk, not only call_preserved's thunk; the runtime FNSAVE/FRSTOR
envelope, not the audit, is what covers such callees.

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
               'set_texture', 'set_sampler_state',  # the mip LOD bias's sampler shadow (X3M_TAA_MIP_BIAS)
               # state-call fast path steps 2-3 (docs/architecture/state-call-fast-path.md): the per-draw
               # binding hooks and the four draw hooks are light too; their whole route (before_draw,
               # after_draw, scene depth, draw input, capture logging) is walked from here.
               'set_stream_source', 'set_indices', 'set_declaration', 'set_fvf',
               'draw_primitive', 'draw_indexed', 'draw_up', 'draw_indexed_up']
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
# The point-light root-admission handler (src/proxy/point_light_admission.cpp)
# runs on the engine's submission thread with no boundary: walked too. The
# pass-phase stamp handler (src/proxy/pass_phases.cpp, X3M_PASS_PHASES=1) runs
# under LightCallBoundary at ~4,024 dispatches per busy frame: same rule, and
# the loop-phase handler (src/proxy/loop_phases.cpp, X3M_LOOP_PHASES=1) and
# the residual handler (src/proxy/residual_phases.cpp, X3M_RESIDUAL_PHASES=1)
# behind the same lean stub.
EXTERN_ROOTS = ['_x3m_probe_enter', '_x3m_probe_exit', '_x3m_resource_read_entry', '_x3m_pool_fopen', '_x3m_pool_fclose',
                '_x3m_point_light_root_admits', '_x3m_pass_phase_enter', '_x3m_loop_phase_enter', '_x3m_residual_phase_enter',
                # the submit-phase handler (src/proxy/submit_phases.cpp, X3M_SUBMIT_PHASES=1) behind the context variant of the lean stub
                '_x3m_submit_phase_enter',
                '_x3m_media_cue_enter', '_x3m_media_cue_return',
                # the cull-census handlers (src/proxy/cull_census.cpp, X3M_CULL_CENSUS=1) run inside the cull/LOD pass, no boundary
                '_x3m_cull_census_measure', '_x3m_cull_census_exit',
                # the narrow-census bracket handlers (src/proxy/collide_narrow_census.cpp, X3M_COLLIDE_NARROW_CENSUS=1) run around the
                # engine's x87 narrow phase under LightCallBoundary only
                '_x3m_collide_narrow_pre', '_x3m_collide_narrow_post',
                # the SSE2 separating-axis replacement (src/proxy/collide_sat_sse2.cpp, X3M_COLLIDE_SAT_SSE2=1): the thunk the engine's
                # x87 BVH descent calls ~2.3e5 times per frame and its body, no boundary at all (stmxcsr/ldmxcsr only)
                '_x3m_collide_sat_thunk', '_x3m_collide_sat_sse2',
                # the no-contact memo (src/proxy/collide_memo.cpp, X3M_COLLIDE_MEMO=1): thunk and both handlers run inside the engine's x87 mesh-pair
                # query with no boundary at all; they compare and copy words and hold no floating-point arithmetic
                '_x3m_collide_memo_thunk', '_x3m_collide_memo_lookup', '_x3m_collide_memo_store']
# The lock view without the FNSAVE/FRSTOR shell (src/ownership/d3d9_ownership.cpp,
# route-per-draw-cost.md lever 2a): called only from the draw hooks' route, it
# preserves nothing itself, so it and its core are a required root, and its own
# reachable set must hold no state transport either (the shell is really gone).
OWNERSHIP_LIGHT = '__ZN3x3m9ownership26get_buffer_lock_view_lightE'
ALLOWED = {'fnsave', 'fninit', 'frstor', 'stmxcsr', 'ldmxcsr', 'fwait'}  # fninit only follows fnsave in CpuState::capture
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
    light = [n for n in functions if n.startswith(OWNERSHIP_LIGHT)]
    if len(light) != 1:
        print(json.dumps({'result': 'FAIL', 'error': f'get_buffer_lock_view_light: {len(light)} symbols {light}'}))
        return 1
    roots['ownership::get_buffer_lock_view_light'] = light[0]
    shell = {name: [l.strip() for l in functions[name] if parse(l)[0] in ('fnsave', 'frstor')]
             for name in walk(functions, light)}
    shell = {name: lines for name, lines in shell.items() if lines}
    if shell:
        print(json.dumps({'result': 'FAIL', 'error': 'state transport under get_buffer_lock_view_light', 'violations': shell}))
        return 1
    seen = walk(functions, roots.values())
    violations = {name: lines for name, lines in seen.items() if lines}
    summary = {'result': 'FAIL' if violations else 'PASS', 'dll': str(dll), 'roots': roots,
               'reachable_functions': len(seen), 'violations': violations}
    print(json.dumps(summary, indent=2))
    return 1 if violations else 0


if __name__ == '__main__':
    sys.exit(main())
