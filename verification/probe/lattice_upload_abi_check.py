#!/usr/bin/env python3
"""Focused structural/emitted checks; these do not replace runtime or review."""
import argparse
import ast
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def functions(text):
    found = list(re.finditer(r'^\w+ <([^>]+)>:\n', text, re.M))
    return {m.group(1): text[m.end():found[i+1].start() if i+1 < len(found) else len(text)]
            for i, m in enumerate(found)}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path)
    p.add_argument('--runtime', type=Path)
    args = p.parse_args()
    source = (ROOT/'src/ownership/clone_upload_abi.cpp').read_text()
    fixture = (ROOT/'verification/probe/lattice_upload_abi_fixture.cpp').read_text()
    checks = 0

    def require(ok, label):
        nonlocal checks
        checks += 1
        if not ok:
            raise AssertionError(label)

    require(source.index('frame->prepared = 1;') < source.index('abi::observer.prepare('),
            'prepare must be abort eligible before callbacks')
    require('args.source->CloneMesh(args.options, args.declaration,' in source,
            'production must use public CloneMesh')
    require('asm(".set __Unwind_SjLj_Register, _x3m_clone_sjlj_register")' in source,
            'EH-only compiler registration alias')
    require('_Unwind_SjLj_Unregister(frame->sjlj)' in source, 'native unregister opaque compiler frame')
    require('if (frame->saved_target) x3m_clone_saved_original(frame)' in source,
            'explicit target mode does not silently replace a captured null slot')
    require('offsetof(Frame, target) == 784 && offsetof(Frame, saved_target) == 788 && sizeof(Frame) == 800' in source,
            'saved-target frame fields retain established CPU/context offsets')
    require('RtlUnwind(frame, nullptr, nullptr, nullptr)' in fixture, 'fixture performs actual native unwind')
    require(fixture.count('cpp_escape();') == 2, 'C++ propagation tested before and after native unwind')
    for name in ('lattice_upload_abi_build.py', 'lattice_upload_abi_check.py'):
        ast.parse((ROOT/'verification/probe'/name).read_text())
        checks += 1
    if args.build:
        shell = functions((args.build/'production-shell.asm').read_text())
        eh = functions((args.build/'eh.asm').read_text())['_x3m_clone_body']
        entries = [body for name, body in shell.items() if 'clone_mesh_upload' in name]
        require(len(entries) == 2, 'manual and explicit target naked entries')
        original = shell['_x3m_clone_original']
        abort = shell['_x3m_clone_abort_cpp']
        unwind = shell['_x3m_clone_unwind']
        for entry in entries:
            require(entry.index('fnsave') < entry.index('GetLastError') < entry.index('_x3m_clone_body'),
                    'incoming save before EH work')
            require(entry.index('_x3m_clone_body') < entry.index('frstor'), 'outgoing restore after helper return')
        saved = shell['_x3m_clone_saved_original']
        require('0x310(' in saved and 'call   *' in saved and 'call   *0x30(' not in saved,
                'saved-target original dispatch uses explicit Frame target')
        require(saved.index('frstor') < saved.index('fnsave'), 'saved target state bookends')
        require(len(re.findall(r'DISP32\s+_x3m_clone_sjlj_register\b', eh)) == 1,
                'exactly one recorded EH registration')
        require('__Unwind_SjLj_Register' not in eh, 'no unrecorded EH registration')
        require('__Unwind_SjLj_Unregister' in eh, 'normal compiler unregister retained')
        require(eh.index('_x3m_clone_abort_cpp') < eh.index('___cxa_rethrow'), 'abort before original rethrow')
        require('call   *0x30(' in original, 'actual public CloneMesh slot')
        require(original.index('frstor') < original.index('call   *0x30(') < original.index('fnsave'),
                'original state restoration/capture bookends')
        require('fixture_original' not in original, 'no synthetic target in production')
        require(unwind.index('fnsave') < unwind.index('__Unwind_SjLj_Unregister') < unwind.index('frstor'),
                'native handler preserves state around compiler cleanup')
        require(abort.index('fnsave') < abort.index('%fs:0x0') < abort.index('frstor'),
                'C++ abort removes native frame inside state guard')
        require('SjLj_Register' not in abort and 'SjLj_Register' not in unwind,
                'cleanup helpers introduce no new SJLJ frames')
    if args.runtime:
        reports = [json.loads(line) for line in args.runtime.read_text().splitlines()
                   if line.startswith('{') and '"checks"' in line]
        require(len(reports) == 1, 'one runtime summary')
        report = reports[0]
        require(report['failures'] == 0 and report['checks'] >= 100, 'runtime assertions pass')
        require(report['native_unwinds'] == 2 and report['aborts'] == 4,
                'original + prepare native unwinds and two original C++ throws')
        require(report['inherited_wrapper_unwind_tested'] is False, 'fixture limitation retained')
    print(json.dumps({'structural_checks': checks, 'emitted_checked': bool(args.build),
                      'runtime_checked': bool(args.runtime), 'native_windows_verified': False}))


if __name__ == '__main__':
    main()
