#!/usr/bin/env python3
"""Instruction-aware verification of the seven X3M_CHASE_VIEW_RESTORE spans.

Each span must decode as whole instructions inside its containing region and
contain no relative control; additionally no direct branch anywhere in the
executable's .text may target a span interior (the store seam sits mid-function,
so a region-local check alone would be insufficient). The optimized dispatch
convergence is checked too: the member-store handler 0x4a4027 ends in
`jmp 0x4a3ffd` and the global-store handler 0x4a3ff0 falls through into it.
"""
import argparse
import json
import re
import subprocess
from pathlib import Path
import verify_chase_aim_sites as common
from verify_chase_transition_sites import spec_table

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'src/proxy/chase_transition.cpp'
H=common.HookSpec
# Regions are bounded listings ending on decoded instruction boundaries.
SITES=(
 H('chase_restore_store',0x4a3ffd,bytes.fromhex('03700c803e08'),0x4a3ff0,0x4a4033),
 H('chase_restore_task_complete',0x4a2260,bytes.fromhex('538b5c240c'),0x4a2260,0x4a22a2),
 H('chase_restore_task_abort',0x4a2420,bytes.fromhex('538b5c240c'),0x4a2420,0x4a2467),
 H('chase_restore_vm_construct',0x49c9a0,bytes.fromhex('5333db895e04'),0x49c9a0,0x49c9dc),
 H('chase_restore_vm_clear',0x49ea80,bytes.fromhex('83ec08558b6c2410'),0x49ea80,0x49eab4),
 H('chase_restore_vm_load',0x4a0880,bytes.fromhex('6aff68c8005300'),0x4a0880,0x4a08ab),
 H('chase_restore_eh_adapter',0x52f298,bytes.fromhex('b8f4e55600'),0x52f298,0x52f2a2),
)
STORE_SEAM=0x4a3ffd
def decode(exe):
    result={}
    for start,end in sorted({(s.function_start,s.function_end) for s in SITES}):
        result[(start,end)]=common.parse_objdump(common.objdump_window(exe,start,end,timeout=30),start,end)
    return result
_BRANCH=re.compile(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(j[a-z]+|call|loop[a-z]*)\s+(?:short\s+)?0x([0-9a-f]+)',re.M)
def text_branches(exe):
    """Every direct branch target in .text (a linear objdump of the whole image)."""
    run=subprocess.run([common.OBJDUMP,'-d','-Mintel','-j','.text',str(exe)],check=True,capture_output=True,text=True,timeout=300)
    return [(int(m.group(1),16),m.group(2),int(m.group(3),16)) for m in _BRANCH.finditer(run.stdout)]
def verify(exe=common.DEFAULT_EXE,source=SOURCE,whole_text=True):
    data=common.image_bytes(exe);decoded=decode(exe)
    report=common.verify(data,decoded,spec_table(Path(source).read_text(),'restore_specs'),specs=SITES)
    checks=report['checks']
    seam=decoded[(0x4a3ff0,0x4a4033)];by={i.va:i for i in seam}
    converge=by.get(0x4a4031);store=by.get(STORE_SEAM);before=by.get(0x4a3ffa)
    checks['optimized_dispatch_convergence']={'ok':bool(converge and converge.mnemonic=='jmp' and converge.operands.endswith(hex(STORE_SEAM)) and store is not None and before is not None and before.end==STORE_SEAM),
        'member_handler_jmp':converge.operands if converge else None}
    if whole_text:
        hits=[]
        for at,mn,target in text_branches(exe):
            for s in SITES:
                if s.va<target<s.end:hits.append({'at':hex(at),'mnemonic':mn,'target':hex(target),'site':s.name})
        checks['no_text_branch_into_span_interior']={'ok':not hits,'interior_branches':hits}
    report['result']='PASS' if all(c['ok'] for c in checks.values()) else 'FAIL'
    return report
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,default=common.DEFAULT_EXE);p.add_argument('--json',action='store_true');a=p.parse_args()
    r=verify(a.exe)
    print(json.dumps(r if a.json else {'result':r['result'],**{k:v['ok'] for k,v in r['checks'].items()}},indent=2))
    raise SystemExit(r['result']!='PASS')
