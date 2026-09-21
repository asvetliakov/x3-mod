#!/usr/bin/env python3
"""Checkpoint A structure/emitter/runtime evidence checks, never a game runner."""
import argparse
import ast
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path)
    p.add_argument('--runtime', type=Path)
    p.add_argument('--stub', type=Path)
    p.add_argument('--tail', type=Path)
    args = p.parse_args()
    source = (ROOT/'src/proxy/lattice_upload_hook.cpp').read_text()
    checks = 0

    def require(ok, label):
        nonlocal checks
        checks += 1
        if not ok:
            raise AssertionError(label)

    require('site_va = 0x004bcc2b' in source and '{0x8b,0x41,0x30,0xff,0xd0}, 5, 20, 0' in source,
            'whole MOV/CALL span with stdcall20 contract')
    require('patch::claim(site, spec)' in source and 'claim_call(' not in source,
            'general Site rather than E8-only claim')
    require('object_trace::executable_verified()' in source and 'patch::install_window_open()' in source,
            'production executable and window gates')
    require('claim_reserve+stub_reserve' in source, 'preflight both arena reservations')
    require('patch::restore(site)' in source and 'if (site.patched_in) return ready' in source,
            'rollback ownership retained and no duplicate install')
    require('clone_mesh_upload_target' in source and 'InterlockedExchange(&armed' in source,
            'explicit saved target and atomic lifecycle route flag')
    for name in ('lattice_upload_hook_build.py', 'lattice_upload_hook_check.py'):
        ast.parse((ROOT/'verification/probe'/name).read_text())
        checks += 1
    if args.build:
        report = json.loads((args.build/'build.json').read_text())
        require(report['executed'] is False, 'build does not execute fixture')
        injected = (args.build/'engine_patch-injected.asm').read_text()
        require('_lattice_hook_protect_iat' in injected and '_lattice_hook_flush_iat' in injected,
                'fault injection really replaces focused object imports')
        from lattice_upload_abi_check import functions
        shell = functions((args.build/'shell.asm').read_text())
        saved = shell['_x3m_clone_saved_original']
        require('0x310(' in saved and 'call   *0x30(' not in saved,
                'production saved-target thunk reads Frame target, not vtable')
    if args.stub:
        stub = args.stub.read_bytes()
        require(len(stub) == 34, 'bounded emitted stub length')
        require(stub[:3] == bytes.fromhex('9c833d') and stub[7:12] == bytes.fromhex('0075069de9'),
                'flag-preserving route test and disabled tail jump')
        require(stub[16:22] == bytes.fromhex('9d8b413050e8'),
                'saved public target load/push precedes sole helper call')
        require(stub[26:30] == bytes.fromhex('83c418e9'), 'six cdecl args removed before continuation')
        require(struct.unpack_from('<i', stub, 12)[0] != 0 and struct.unpack_from('<i', stub, 22)[0] != 0,
                'tail and target-shell relocations populated')
    if args.tail:
        tail = args.tail.read_bytes()
        require(len(tail) == 10 and tail[:6] == bytes.fromhex('8b4130ffd0e9'),
                'disabled tail reproduces both complete instructions once')
    if args.runtime:
        reports = [json.loads(line) for line in args.runtime.read_text().splitlines()
                   if line.startswith('{') and '"checks"' in line]
        require(len(reports) == 1, 'single focused runtime report')
        r = reports[0]
        require(r['checks'] >= 200 and r['failures'] == 0 and r['wrong_target_calls'] == 0,
                'site CPU/arguments/stack/saved target/rollback checks pass')
        require(r['native_unwinds'] == 2 and r['aborts'] == 4,
                'real original/prepare native unwinds plus C++ before/after')
        require(r['inherited_wrapper_unwind_tested'] is False and r['native_windows_verified'] is False,
                'synthetic boundary and native Windows limitations retained')
    print(json.dumps({'checks': checks, 'emitted_checked': bool(args.stub),
                      'runtime_checked': bool(args.runtime), 'native_windows_verified': False}))


if __name__ == '__main__':
    main()
