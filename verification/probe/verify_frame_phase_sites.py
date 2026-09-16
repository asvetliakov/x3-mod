#!/usr/bin/env python3
"""Read-only instruction/ABI qualification of the ten frame-phase stamps.

The independent ledger below comes from the static study in
docs/reverse-engineering/frame-loop-phases.md (section 4). The whole render
routine 0x00471f50..0x0047260b (ret) is decoded, not isolated bytes. Verified:
source table order and SiteSpec fields, exact bytes, whole instructions, no
branch into a span interior, the exact set of incoming edges landing on each
span start (direct edges enumerated within the routine only; the routine has
no indirect jump and the image holds no data reference to any span byte), the one relative branch per call span with its rel32 replayed at
alternate arena addresses (the tail copy re-bases it), no indirect jump in the
routine, no data reference to any span byte in the image, disjointness from
the scene hook, the flag consumer after the overlays span and the qsort call
ahead of the views span (its `add esp,0x10` runs in the tail at the game's
ESP). No Wine or game launch.
"""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/proxy/frame_phase_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
FRAME = (0x471f50, 0x47260c)  # ret at 0x47260b, int3 padding after
# name, address, bytes, rel32 destination (0=none), rel32 field offset
LEDGER = (
 ('prologue',0x471f6c,'e84f300800',0x4f4fc0,1),
 ('scene_update',0x472044,'e877f4ffff',0x4714c0,1),
 ('begin_scene',0x4720b5,'a13c8b6000',0,0),
 ('views',0x472186,'33db83c410',0,0),
 ('overlays',0x47238d,'33db395c2418',0,0),
 ('text',0x4724ec,'a118856000',0,0),
 ('scene_end',0x472574,'e8d72c0500',0x4c5250,1),
 ('view_setup_begin',0x47224c,'56e84e700100',0x4892a0,2),
 ('view_submit_begin',0x472270,'8b461c8b684c',0,0),
 ('view_submit_end',0x4722c8,'8b1518856000',0,0),
)
SITES = tuple(common.HookSpec('frame_phase_'+row[0],row[1],bytes.fromhex(row[2]),*FRAME) for row in LEDGER)
TARGETS = {row[1]: row[3] for row in LEDGER if row[3]}
RELATIVE = {row[1]: row[4] for row in LEDGER if row[3]}
# Every direct edge that lands on a span start (frame-loop-phases.md, section
# 4 "Validation performed"); the other sites have no incoming edge.
INCOMING = {
 0x471f6c: set(), 0x472044: {0x471f8d,0x471fd1,0x47201b}, 0x4720b5: {0x472081}, 0x472186: set(),
 0x47238d: {0x472191}, 0x4724ec: {0x47216a,0x472439,0x4724b9,0x4724c2,0x4724cb,0x4724e0},
 0x472574: {0x4724fb}, 0x47224c: set(), 0x472270: set(), 0x4722c8: {0x472279},
}
SCENE_HOOK = (0x4721b1, 0x4721b6)  # X3M_SCENE_HOOK's call site inside interval 4
ARENAS = (0x10000000, 0x71000000, 0xf1000000)


def decode(exe=DEFAULT_EXE):
    run = subprocess.run([common.OBJDUMP,'-d','-Mintel','--insn-width=16',
                          f'--start-address={FRAME[0]}',f'--stop-address={FRAME[1]}',str(exe)],
                         check=True,capture_output=True,text=True,timeout=30)
    return {FRAME: common.parse_objdump(run.stdout,*FRAME)}


def source_checks(text):
    actual = common.parse_source_specs(text)
    expected = [dict(name=s.name,va=s.va,bytes=s.expected,length=len(s.expected),
                     rel32_offset=0,rel32_target=RELATIVE.get(s.va,0)) for s in SITES]
    return actual == expected


def relocated_bytes(spec, arena):
    """Independent modulo-32-bit reference for the tail copy's one rel32 field."""
    result = bytearray(spec.expected)
    if spec.va in TARGETS:
        offset = RELATIVE[spec.va]
        original = struct.unpack_from('<i',result,offset)[0]
        target = (spec.va+offset+4+original) & 0xffffffff
        struct.pack_into('<I',result,offset,(target-arena-offset-4) & 0xffffffff)
    return bytes(result)


def inspect(image, decoded, source, data=None):
    checks = {'preferred_base': image.image_base == common.IMAGE_BASE,
              'source_specs': source_checks(source)}
    instructions = decoded.get(FRAME,[])
    by_va = {i.va:i for i in instructions}
    rows = []
    for spec in SITES:
        row = common.inspect_site(image,spec,instructions)
        span = [i for i in instructions if spec.va <= i.va < spec.end]
        controls = [i for i in span if common._is_direct_control(i) is not None]
        if spec.va in TARGETS:
            offset = RELATIVE[spec.va]
            row['relative_contract'] = (bool(span) and len(controls)==1 and controls[0] is span[-1]
                and controls[0].mnemonic=='call' and common._is_direct_control(controls[0])==TARGETS[spec.va]
                and controls[0].end==spec.end and controls[0].va-spec.va+len(controls[0].raw)-4==offset)
            row['relocation_targets'] = [
                (arena+offset+4+struct.unpack_from('<i',relocated_bytes(spec,arena),offset)[0]) & 0xffffffff
                for arena in ARENAS]
            row['relocation_ok'] = row['relocation_targets']==[TARGETS[spec.va]]*len(ARENAS)
        else:
            row['relative_contract'] = not controls
            row['relocation_ok'] = all(relocated_bytes(spec,arena)==spec.expected for arena in ARENAS)
        sources = {i.va for i in instructions if common._is_direct_control(i)==spec.va}
        row['incoming_sources'] = sorted(f'{s:#010x}' for s in sources)
        row['incoming_ok'] = sources==INCOMING[spec.va]
        row['ok'] = all(row[k] for k in ('bytes_ok','whole_instructions','no_interior_branch',
                                         'relative_contract','relocation_ok','incoming_ok'))
        rows.append(row)
    checks['sites'] = all(row['ok'] for row in rows)
    ordered = sorted(SITES,key=lambda s:s.va)
    checks['nonoverlap'] = all(a.end <= b.va for a,b in zip(ordered,ordered[1:]))
    checks['complete_routine'] = bool(instructions) and instructions[0].va==FRAME[0] and instructions[-1].end==FRAME[1]
    checks['single_ret_no_indirect_jump'] = (sum(i.mnemonic=='ret' for i in instructions)==1
        and not any(i.mnemonic=='jmp' and 'PTR' in i.operands for i in instructions))
    checks['scene_hook_disjoint'] = all(s.end <= SCENE_HOOK[0] or s.va >= SCENE_HOOK[1] for s in SITES)
    # The flag-consuming jle after the overlays span, and the qsort call whose
    # four pushed arguments the views span's `add esp,0x10` removes.
    checks['overlays_flag_consumer'] = 0x472393 in by_va and by_va[0x472393].mnemonic=='jle'
    checks['views_follows_qsort_call'] = (0x472181 in by_va and by_va[0x472181].mnemonic=='call'
        and by_va[0x472181].end==0x472186)
    if data is not None:
        inside = [struct.pack('<I',va) for s in SITES for va in range(s.va,s.end)]
        checks['no_data_reference'] = all(data.find(word)<0 for word in inside)
    return {'result':'PASS' if all(checks.values()) else 'FAIL','checks':checks,'sites':rows}


def verify(exe=DEFAULT_EXE,source=SOURCE):
    data = Path(exe).read_bytes()
    try:
        report = inspect(common.Image(data),decode(exe),Path(source).read_text(),data)
    except (ValueError,OSError,struct.error,subprocess.SubprocessError) as error:
        return {'result':'FAIL','checks':{'decode':False},'error':str(error)}
    report['checks']['exe_identity'] = (hashlib.sha256(data).hexdigest()==common.EXPECTED_SHA256
                                        and len(data)==common.EXPECTED_SIZE)
    report['exe_sha256'] = hashlib.sha256(data).hexdigest()
    report['result'] = 'PASS' if all(report['checks'].values()) else 'FAIL'
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=DEFAULT_EXE)
    parser.add_argument('--source',type=Path,default=SOURCE)
    parser.add_argument('--json',action='store_true')
    args=parser.parse_args()
    report=verify(args.exe,args.source)
    print(json.dumps(report if args.json else {'result':report['result'],**report['checks']},indent=2))
    raise SystemExit(report['result']!='PASS')
