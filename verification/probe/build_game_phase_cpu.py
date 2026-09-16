#!/usr/bin/env python3
"""Build/audit the actual game-phase stubs and ABI fixture. Never runs Wine."""
import argparse
import json
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS, audit_object

ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/verification/game-phases'


def build(objects_only=False):
    BUILD.mkdir(parents=True,exist_ok=True)
    objects=[]
    for source,stem in (
        ('src/proxy/game_phases.cpp','runtime'),
        ('src/proxy/frame_phases.cpp','frame'),
        ('src/proxy/pass_phases.cpp','pass'),
        ('src/proxy/loop_phases.cpp','loop'),
        ('src/proxy/lean_stub.cpp','lean'),
        ('verification/probe/game_phase_cpu_fixture.cpp','fixture'),
        ('src/proxy/engine_patch.cpp','patch'),
        ('src/proxy/engine_memory.cpp','memory')):
        out=BUILD/(stem+'.o')
        flags=[*FLAGS,'-DX3M_GAME_PHASE_FIXTURE']
        if stem=='memory':flags+=['-mno-sse','-mno-mmx','-mfpmath=387']
        subprocess.run(['i686-w64-mingw32-g++',*flags,'-c',str(ROOT/source),'-o',str(out)],check=True,cwd=ROOT)
        objects.append(out)
    # Audit the production callback without the test-only redirection branch.
    audit=BUILD/'production_callback.o'
    subprocess.run(['i686-w64-mingw32-g++',*FLAGS,'-c',str(ROOT/'src/proxy/game_phases.cpp'),'-o',str(audit)],check=True,cwd=ROOT)
    report={'objects':[str(p) for p in objects],
            'cpu_audit':audit_object(audit,'x3m_game_phase_enter'),'runtime':'not run'}
    if not objects_only:
        exe=BUILD/'game_phase_cpu_fixture.exe'
        subprocess.run(['i686-w64-mingw32-g++',*map(str,objects),'-static','-static-libgcc','-static-libstdc++',
                        '-Wl,--image-base,0x10000000','-o',str(exe)],check=True,cwd=ROOT)
        report['binary']=str(exe)
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objects-only',action='store_true')
    args=parser.parse_args()
    print(json.dumps(build(args.objects_only),indent=2))
