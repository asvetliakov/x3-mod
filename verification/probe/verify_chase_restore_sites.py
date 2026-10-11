#!/usr/bin/env python3
"""Instruction-aware verification of the X3M_CHASE_VIEW_RESTORE engine-path spans.

The four spans (the injection site at the input routine's entry 0x00410100 and
the three VM cancel boundaries) must decode as whole instructions inside their
containing region and contain no relative control; no direct branch anywhere in
the executable's .text may target a span interior. The input routine's extent
is proved too: objdump decodes [0x00410100, 0x00412250) gap-free, every direct
jump inside it stays inside it, and the thirteen calls to the by-name script
call wrapper 0x0049f570 return inside it (the Ghidra function body is 13
ranges with its last byte at 0x0041224f; docs/architecture/
chase-view-restore-engine-state.md section 4).
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
INPUT_ROUTINE=(0x410100,0x412250)
SCRIPT_CALL=0x49f570
SITES=(
 H('chase_restore_inject',0x410100,bytes.fromhex('558bec83e4f0'),*INPUT_ROUTINE),
 H('chase_restore_vm_clear',0x49ea80,bytes.fromhex('83ec08558b6c2410'),0x49ea80,0x49eab4),
 H('chase_restore_vm_load',0x4a0880,bytes.fromhex('6aff68c8005300'),0x4a0880,0x4a08ab),
 H('chase_restore_eh_adapter',0x52f298,bytes.fromhex('b8f4e55600'),0x52f298,0x52f2a2),
)
def decode(exe):
    result={}
    for start,end in sorted({(s.function_start,s.function_end) for s in SITES}):
        result[(start,end)]=common.parse_objdump(common.objdump_window(exe,start,end,timeout=60),start,end)
    return result
_BRANCH=re.compile(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(j[a-z]+|call|loop[a-z]*)\s+(?:short\s+)?0x([0-9a-f]+)',re.M)
def text_branches(exe):
    """Every direct branch target in .text (a linear objdump of the whole image)."""
    run=subprocess.run([common.OBJDUMP,'-d','-Mintel','-j','.text',str(exe)],check=True,capture_output=True,text=True,timeout=300)
    return [(int(m.group(1),16),m.group(2),int(m.group(3),16)) for m in _BRANCH.finditer(run.stdout)]
def _target(instruction):
    m=re.match(r'^(?:short\s+)?0x([0-9a-f]+)$',instruction.operands or '')
    return int(m.group(1),16) if m else None
def input_routine_extent(listing):
    """The decoded input routine: gap-free, ends in ret, jumps stay inside, 13 calls to 0x0049f570 return inside."""
    start,end=INPUT_ROUTINE
    inside=lambda va:start<=va<end
    jumps_out=[hex(i.va) for i in listing if i.mnemonic.startswith('j') and _target(i) is not None and not inside(_target(i))]
    calls=[i for i in listing if i.mnemonic=='call' and _target(i)==SCRIPT_CALL]
    returns=[i.end for i in calls]
    return {'ok':bool(listing) and listing[0].va==start and listing[-1].end==end and listing[-1].mnemonic=='ret' and not jumps_out and len(calls)==13 and all(inside(r) for r in returns),
            'last':hex(listing[-1].va) if listing else None,'last_mnemonic':listing[-1].mnemonic if listing else None,
            'jumps_out':jumps_out,'script_calls':len(calls),'script_call_returns':[hex(r) for r in returns]}
def verify(exe=common.DEFAULT_EXE,source=SOURCE,whole_text=True):
    data=common.image_bytes(exe);decoded=decode(exe)
    report=common.verify(data,decoded,spec_table(Path(source).read_text(),'restore_specs'),specs=SITES)
    checks=report['checks']
    checks['input_routine_extent']=input_routine_extent(decoded[INPUT_ROUTINE])
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
    print(json.dumps(r if a.json else {'result':r['result'],**{k:v['ok'] for k,v in r['checks'].items()},'input_routine':r['checks']['input_routine_extent']},indent=2))
    raise SystemExit(r['result']!='PASS')
