#!/usr/bin/env python3
"""Read-only R7 whole-call span, caller, ABI and claim qualification. No Wine."""
import json
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)
import verify_chase_aim_sites as common
import verify_submit_phase_sites as shared
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'src/proxy/light_phase_sites.h'
REGIONS=((0x47d5e0,0x47d9b4),(0x4216e0,0x4218a1),(0x47d9c0,0x47e614))
SITES=(common.HookSpec('light_phase_enter',0x47d5e0,bytes.fromhex('558bec83e4f0'),*REGIONS[0]),
       common.HookSpec('light_phase_exit',0x47d9ab,bytes.fromhex('5f5e5b8be5'),*REGIONS[0]))
CALLERS={0x42173b:bytes.fromhex('e8a0be0500'),0x47dff1:bytes.fromhex('e8eaf5ffff')}
EXIT_EDGES={0x47d605,0x47d617,0x47d97c,0x47d998}
def decode(exe=common.DEFAULT_EXE):
    result={}
    for lo,hi in REGIONS:
        r=subprocess.run([common.OBJDUMP,'-d','-Mintel','--insn-width=16',f'--start-address={lo}',f'--stop-address={hi}',str(exe)],capture_output=True,text=True,check=True)
        result[(lo,hi)]=common.parse_objdump(r.stdout,lo,hi)
    return result
def inspect(data,decoded,source,claims,anchored=True):
    image=common.Image(data)
    ins=[i for region in REGIONS for i in decoded.get(region,[])]
    by_va={i.va:i for i in ins}
    specs=[(m.group(1),int(m.group(2),16),bytes(int(v,16) for v in __import__('re').findall(r'0x([0-9a-fA-F]{1,2})',m.group(3))),int(m.group(4)),int(m.group(5)),int(m.group(6))) for m in common._SOURCE_SPEC_RE.finditer(source)]
    checks={'identity':exe_identity.identity_ok(data),
            'source':specs==[(s.name,s.va,s.expected,len(s.expected),0,0) for s in SITES],
            'regions':all(decoded.get(r) and decoded[r][0].va==r[0] and decoded[r][-1].end==r[1] and all(a.end==b.va for a,b in zip(decoded[r],decoded[r][1:])) for r in REGIONS),
            'claim_anchors':anchored,
            'claims':all(s.end<=lo or s.va>=hi for s in SITES for lo,hi in claims)}
    for s in SITES:
        row=common.inspect_site(image,s,ins)
        span=[i for i in ins if s.va<=i.va<s.end]
        checks[s.name]=all(row[k] for k in ('bytes_ok','whole_instructions','no_interior_branch')) and not any(common._is_direct_control(i) is not None or i.mnemonic.startswith('f') for i in span)
    checks['exit_edges']={i.va for i in ins if common._is_direct_control(i)==0x47d9ab}==EXIT_EDGES
    checks['ret4']=image.read(0x47d9b0,4)==bytes.fromhex('5dc20400') and [i.va for i in decoded[REGIONS[0]] if i.mnemonic=='ret']==[0x47d9b1]
    checks['node_stack_arg']=image.read(0x47d5fb,3)==bytes.fromhex('8b5d08')
    checks['call_bytes']=all(image.read(va,5)==raw and by_va[va].mnemonic=='call' for va,raw in CALLERS.items())
    raw_callers=set();edges=set();interior=[]
    for off in range(shared.TEXT_OFFSET,shared.TEXT_OFFSET+shared.TEXT_SIZE-6):
        va=shared.TEXT_BASE+off-shared.TEXT_OFFSET;b=data[off]
        if b in (0xe8,0xe9):
            target=(va+5+struct.unpack_from('<i',data,off+1)[0])&0xffffffff
            if target==0x47d5e0:raw_callers.add(va)
        elif b==0x0f and 0x80<=data[off+1]<=0x8f:target=(va+6+struct.unpack_from('<i',data,off+2)[0])&0xffffffff
        elif b==0xeb or 0x70<=b<=0x7f or 0xe0<=b<=0xe3:target=(va+2+struct.unpack_from('<b',data,off+1)[0])&0xffffffff
        else:continue
        if target==0x47d9ab:edges.add(va)
        if any(s.va<target<s.end for s in SITES):interior.append((va,target))
    checks['raw_callers']=raw_callers==set(CALLERS)
    checks['raw_exit_edges']=edges==EXIT_EDGES
    checks['raw_interior']=not interior
    checks['absolute_references']=not any(struct.pack('<I',va) in data for s in SITES for va in range(s.va,s.end))
    return {'result':'PASS' if all(checks.values()) else 'FAIL','checks':checks,'exe_info':exe_identity.info(data),'instructions':sum(map(len,decoded.values())),'claims':len(claims)}
def verify():
    claims,anchored=shared.other_claims(ROOT/'src/proxy',SOURCE)
    return inspect(common.DEFAULT_EXE.read_bytes(),decode(),SOURCE.read_text(),claims,anchored)
if __name__=='__main__':
    report=verify();print(json.dumps(report,indent=2));raise SystemExit(report['result']!='PASS')
