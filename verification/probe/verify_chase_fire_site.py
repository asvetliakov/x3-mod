#!/usr/bin/env python3
"""Verify the consumer-local cursor-fire JZ and its original CMP, read-only."""
import json
from pathlib import Path
from verify_chase_aim_sites import (HookSpec, FIRE_FUNCTION, Image, DEFAULT_EXE,
    disassemble_functions, image_bytes, inspect_site, parse_source_specs, _is_direct_control)
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'src/proxy/chase_fire.cpp'
SPEC=HookSpec('chase_cursor_admission',0x445a41,bytes.fromhex('0f8464020000'),*FIRE_FUNCTION)
CMP=bytes.fromhex('833de87c600000')

def verify(exe=DEFAULT_EXE, source=None):
    image=Image(image_bytes(exe))
    instructions=disassemble_functions(exe)[FIRE_FUNCTION]
    row=inspect_site(image,SPEC,instructions)
    span=[i for i in instructions if SPEC.va<=i.va<SPEC.end]
    compare=[i for i in instructions if i.va==SPEC.va-len(CMP)]
    source_rows=parse_source_specs(SOURCE.read_text() if source is None else source)
    parity=source_rows==[dict(name=SPEC.name,va=SPEC.va,bytes=SPEC.expected,length=6,rel32_offset=0,rel32_target=2)]
    # parse_source_specs' last two keys retain its historical field names;
    # the C++ fields are ret_pop=0 and rel32_offset=2, respectively.
    checks=dict(preferred_base=image.image_base==0x400000,source_spec=parity,
        exact_jz=row['bytes_ok'],whole_instruction=row['whole_instructions'],
        no_interior_branch=row['no_interior_branch'],
        original_compare=len(compare)==1 and compare[0].raw==CMP and compare[0].end==SPEC.va,
        relative_jz_target=len(span)==1 and span[0].mnemonic in ('je','jz') and _is_direct_control(span[0])==0x445cab)
    return dict(passed=all(checks.values()),site=hex(SPEC.va),bytes=SPEC.expected.hex(),rel32_offset=2,checks=checks)

if __name__=='__main__':
    result=verify();print(json.dumps(result,sort_keys=True));raise SystemExit(0 if result['passed'] else 1)
