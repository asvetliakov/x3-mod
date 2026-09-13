#!/usr/bin/env python3
"""Instruction-aware verification of native cockpit lifetime/update/transition seams."""
import argparse
import json
import subprocess
from pathlib import Path
import verify_chase_aim_sites as common

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'src/proxy/chase_transition.cpp'
H=common.HookSpec
SITES=(
 H('chase_cockpit_construct',0x41f8d0,bytes.fromhex('6aff68dd075300'),0x41f8d0,0x41fe19),
 H('chase_cockpit_complete',0x41fe04,bytes.fromhex('8b4c24145f'),0x41f8d0,0x41fe19),
 H('chase_cockpit_destroy',0x41ffc0,bytes.fromhex('6aff686b095300'),0x41ffc0,0x420260),
 H('chase_update_begin',0x4205e0,bytes.fromhex('558bec83e4f0'),0x4205e0,0x4216dc),
 H('chase_update_end_internal',0x4216c3,bytes.fromhex('5f5e5b8be5'),0x4205e0,0x4216dc),
 H('chase_update_end',0x4216d3,bytes.fromhex('5f5e5b8be5'),0x4205e0,0x4216dc),
 H('chase_mode_script',0x42e742,bytes.fromhex('898850010000'),0x42d340,0x42f063),
 H('chase_mode_load',0x419e06,bytes.fromhex('899550010000'),0x419430,0x41c6df),
 H('chase_connect',0x422cd0,bytes.fromhex('83ec0883f809'),0x422cd0,0x422e54),
)
def decode(exe):
    result={}
    for start,end in sorted({(s.function_start,s.function_end) for s in SITES}):
        run=subprocess.run([common.OBJDUMP,'-d','-Mintel','--insn-width=16',f'--start-address={start}',f'--stop-address={end}',str(exe)],check=True,capture_output=True,text=True,timeout=30)
        result[(start,end)]=common.parse_objdump(run.stdout,start,end)
    return result

def verify(exe=common.DEFAULT_EXE,source=SOURCE):
    return common.verify(Path(exe).read_bytes(),decode(exe),Path(source).read_text(),specs=SITES)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,default=common.DEFAULT_EXE);p.add_argument('--json',action='store_true');a=p.parse_args()
    r=verify(a.exe)
    print(json.dumps(r if a.json else {'result':r['result'],**{k:v['ok'] for k,v in r['checks'].items()}},indent=2))
    raise SystemExit(r['result']!='PASS')
