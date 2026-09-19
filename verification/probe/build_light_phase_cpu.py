#!/usr/bin/env python3
"""Build/audit the light-phase stamps and their ABI fixture. Never runs Wine."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS

ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/verification/light-phases'
X87=re.compile(r'^\s*[0-9a-f]+:\t[0-9a-f ]+\t(f[a-z0-9]+|emms)\b',re.M)  # the mnemonic column of objdump -d


def audit_no_x87(path):
    """No x87 arithmetic or state mutation; only read-only FNSTCW is allowed."""
    assembly=subprocess.run(['i686-w64-mingw32-objdump','-d',str(path)],capture_output=True,text=True,check=True).stdout
    if '<_x3m_light_phase_enter>:' not in assembly:
        raise RuntimeError('handler missing: x3m_light_phase_enter')
    found=sorted(set(X87.findall(assembly))-{'fnstcw'})
    if found:
        raise RuntimeError('x87 opcodes in light_phases.o: '+', '.join(found))
    return {'handler':'x3m_light_phase_enter','x87_arithmetic_or_mode_writes':0,'readonly_fnstcw':len(re.findall(r'\bfnstcw\b',assembly)),'sha256':hashlib.sha256(Path(path).read_bytes()).hexdigest()}


def build(objects_only=False):
    BUILD.mkdir(parents=True,exist_ok=True)
    objects=[]
    for source,stem in (
        ('src/proxy/light_phases.cpp','light'),
        ('src/proxy/lean_stub.cpp','lean'),
        ('src/proxy/engine_patch.cpp','patch'),
        ('verification/probe/light_phase_cpu_fixture.cpp','fixture')):
        out=BUILD/(stem+'.o')
        subprocess.run(['i686-w64-mingw32-g++',*FLAGS,'-DX3M_GAME_PHASE_FIXTURE','-c',str(ROOT/source),'-o',str(out)],check=True,cwd=ROOT)
        objects.append(out)
    # Audit the production handler without the test-only entry points.
    audit=BUILD/'production_handler.o'
    subprocess.run(['i686-w64-mingw32-g++',*FLAGS,'-c',str(ROOT/'src/proxy/light_phases.cpp'),'-o',str(audit)],check=True,cwd=ROOT)
    report={'objects':[str(p) for p in objects],'cpu_audit':audit_no_x87(audit),'runtime':'not run'}
    if not objects_only:
        exe=BUILD/'light_phase_cpu_fixture.exe'
        subprocess.run(['i686-w64-mingw32-g++',*map(str,objects),'-static','-static-libgcc','-static-libstdc++',
                        '-Wl,--image-base,0x10000000','-o',str(exe)],check=True,cwd=ROOT)
        report['binary']=str(exe)
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objects-only',action='store_true')
    args=parser.parse_args()
    print(json.dumps(build(args.objects_only),indent=2))
