#!/usr/bin/env python3
"""Cross-compile dedicated CPU/ABI/fault/benchmark fixture; never executes it."""
import hashlib
import json
import shlex
import re
import subprocess
from pathlib import Path
import build_collide_memo as memo
ROOT=memo.ROOT
BUILD=ROOT/'build/verification/collide-query-phases'


def audit(obj):
    text=subprocess.run([memo.OBJDUMP,'-d','-Mintel','--no-show-raw-insn',str(obj)],check=True,capture_output=True,text=True).stdout
    counts={}
    for name,envelopes in [('descent',2),('memo',2)]:
        block=re.search(rf'<_x3m_collide_query_{name}_thunk>:\n(.*?)(?:\n\n|\Z)',text,re.S)
        if not block:raise RuntimeError('missing thunk '+name)
        body=block.group(1)
        # Memo has two branch exits as well as the ordinary two envelopes.
        saves=len(re.findall(r'\bfnsave\b',body));restores=len(re.findall(r'\bfrstor\b',body))
        expected_restores=4 if name=='memo' else 3
        if saves!=envelopes or restores!=expected_restores:raise RuntimeError('CPU envelope count mismatch '+name)
        if len(re.findall(r'\bmovdqu\b',body))!=8*(saves+restores+2):raise RuntimeError('XMM envelope incomplete '+name)
        if len(re.findall(r'\bpushf\b',body))!=saves or len(re.findall(r'\bpopf\b',body))!=restores+2:raise RuntimeError('flags envelope incomplete '+name)
        if 'fxrstor' in body or 'fxsave' in body:raise RuntimeError('unqualified x87 transport')
        counts[name]={'saves':saves,'restores':restores,'xmm_moves':8*(saves+restores+2)}
    return counts


SOURCES=[('verification/probe/collide_query_phases_fixture.cpp','fixture'),('src/proxy/collide_memo.cpp','memo'),('src/proxy/collide_query_phases.cpp','phases'),('src/proxy/collide_sat_sse2.cpp','sat'),('src/proxy/engine_patch.cpp','patch')]
FLAGS=['i686-w64-mingw32-g++',*memo.FLAGS,'-DX3M_COLLIDE_QUERY_FIXTURE','-I',str(memo.BUILD)]
RECORD=BUILD/'build-record.json'


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_hashes(names):
    return {name:sha256(ROOT/name) for name in sorted(names)}


def build():
    BUILD.mkdir(parents=True,exist_ok=True)
    engine=memo.engine_fragment()
    # Compiler dependency output gives only this fixture's actual project inputs,
    # not a whole-tree manifest. Generated engine bytes remain local/untracked.
    inputs={str(Path(__file__).resolve().relative_to(ROOT)),str(Path(memo.__file__).resolve().relative_to(ROOT)),
            'verification/probe/run_collide_query_phases.py','verification/probe/run_chase_aim_trace.py'}
    for source,_ in SOURCES:
        deps=subprocess.run([*FLAGS,'-MM','-MT','inputs',str(ROOT/source)],check=True,cwd=ROOT,capture_output=True,text=True).stdout
        for name in shlex.split(deps.replace('\\\n',' ').split(':',1)[1]):
            inputs.add(str(Path(name).resolve().relative_to(ROOT)))
    before=source_hashes(inputs)
    compiler=subprocess.run([FLAGS[0],'--version'],check=True,capture_output=True,text=True).stdout.splitlines()[0]
    commands=[];objects=[]
    for source,stem in SOURCES:
        obj=BUILD/(stem+'.o');command=[*FLAGS,'-c',str(ROOT/source),'-o',str(obj)]
        commands.append(command);subprocess.run(command,check=True,cwd=ROOT);objects.append(obj)
    binary=BUILD/'collide_query_phases_fixture.exe'
    command=[FLAGS[0],*map(str,objects),'-static','-static-libgcc','-static-libstdc++',*memo.LINK,'-o',str(binary)]
    commands.append(command);subprocess.run(command,check=True,cwd=ROOT)
    after=source_hashes(inputs)
    if before!=after:raise RuntimeError('fixture inputs changed during compilation')
    result={'binary':str(binary),'binary_sha256':sha256(binary),'source_sha256':before,'compiler':compiler,'commands':commands,
            'engine':engine,'abi_audit':audit(BUILD/'phases.o'),'memo_audit':memo.audit_module(BUILD/'memo.o'),'runtime':'not run'}
    RECORD.write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':print(json.dumps(build(),indent=2))
