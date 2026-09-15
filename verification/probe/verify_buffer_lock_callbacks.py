#!/usr/bin/env python3
"""Bounded emitted-code audit of new counter callbacks and snapshot ABI shell.

The snapshot's exception-enabled registry core is an explicit boundary, not part
of the allocation-free callback closure. Runtime fixtures verify its caught error
return and CPU envelope. This is not an application-entry coverage certificate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def audit(executable):
    disassembly = subprocess.check_output(['i686-w64-mingw32-objdump', '-d', '-C', str(executable)], text=True)
    symbols = subprocess.check_output(['i686-w64-mingw32-nm', str(executable)], text=True)
    imports = {}
    for line in symbols.splitlines():
        match = re.match(r'([0-9a-fA-F]+) I (__imp__\S+)', line)
        if match:
            imports[int(match[1], 16)] = match[2]
    functions = {}
    current = None
    for line in disassembly.splitlines():
        match = re.match(r'^([0-9a-fA-F]+) <(.*)>:$', line)
        if match:
            current = match[2]
            functions[current] = []
        elif current:
            functions[current].append(line)
    callbacks = [name for name in functions if '::LockSidecar::' in name and
                 any(method in name for method in ['::AddRef()', '::Release()', '::QueryInterface('])]
    shells = [name for name in functions if name.startswith('x3m::ownership::get_buffer_lock_view(')]
    cores = [name for name in functions if name.startswith('x3m::ownership::get_buffer_lock_view_core(')]
    assert len(callbacks) == 3 and len(shells) == 1 and len(cores) == 1, 'Missing or duplicate ABI entry'
    permitted_imports = {'__imp__GetLastError@0', '__imp__SetLastError@4', '__imp__GetCurrentThreadId@0'}
    results = []
    for name in callbacks + shells:
        body = '\n'.join(functions[name])
        assert all(op in body for op in ['fnsave', 'frstor', 'stmxcsr', 'ldmxcsr']), name
        assert not re.search(r'_Unwind|__cxa|emutls|pthread|malloc|calloc|operator new|terminate|abort', body), name
        targets = []
        first_save = body.index('fnsave')
        for match in re.finditer(r'\bcall\s+([^\n]+)', body):
            assert match.start() > first_save, f'Call before CPU save: {name}'
            instruction = match[1]
            indirect = re.match(r'\*0x([0-9a-fA-F]+)', instruction)
            direct = re.search(r'<(.*)>', instruction)
            if indirect:
                target = imports.get(int(indirect[1], 16))
                assert target in permitted_imports, (name, instruction, target)
            else:
                assert direct, f'Unresolved indirect call: {name}: {instruction}'
                target = direct[1]
                allowed = callbacks if name in callbacks else cores
                assert target in allowed, f'Unaudited transitive call: {name} -> {target}'
            targets.append(target)
        for match in re.finditer(r'\bjmp\s+[^\n]*<(.*)>', body):
            target = re.sub(r'\+0x[0-9a-f]+$', '', match[1])
            assert target == name, f'Unaudited tail call: {name} -> {target}'
        results.append({'entry': name, 'calls': targets})
    core = '\n'.join(functions[cores[0]])
    assert '__cxa_begin_catch' in core and '__cxa_end_catch' in core, 'Snapshot core must catch internally'
    cold = [name for name in functions if name.startswith('cold_call(')]
    assert len(cold) == 1, 'Missing raw CreateThread witness'
    assert not re.search(r'_Unwind|__cxa|emutls|pthread|malloc|calloc|operator new', '\n'.join(functions[cold[0]]))
    return {'passed': True, 'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
            'callback_closures': 3, 'snapshot_shells': 1, 'snapshot_core_catch': True,
            'cold_thread_entry_has_no_compiler_warmup': True, 'entries': results}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = audit(args.executable)
    rendered = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(rendered)
    print(json.dumps({key: value for key, value in result.items() if key != 'entries'}))
