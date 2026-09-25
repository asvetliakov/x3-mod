#!/usr/bin/env python3
"""Read-only instruction/ABI qualification of the 33 game phase markers
(the 14 audio-path witnesses of the load hang build, X3M_AUDIO_SITES, were
removed on 2026-09-25).

The independent address/byte/target ledger below comes from targeted native
analysis. Decode complete containing routines, not isolated opcode-looking
bytes. Verify source table order, actual SiteSpec fields, whole instructions,
all direct incoming edges in the five routines, publisher jump-table targets,
and relative replay at alternate x86 arena addresses. No Wine or game launch.
"""
import argparse
import json
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_chase_aim_sites as common
from verify_chase_lead_sites import all_paths_reach

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/proxy/game_phase_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
MAIN = (0x403840, 0x404278)
OVERLAY = (0x42a2d0, 0x42c1de)
PUBLISHER = (0x425a10, 0x425c80)  # code ends immediately before its jump table
SOUND = (0x49a350, 0x49a436)
PRESENT = (0x4dac30, 0x4dac8d)
PLAYBACK = (0x4997c0, 0x499aec)  # code ends immediately before its jump table
STREAM = (0x498e30, 0x499022)
# name, address, bytes, complete containing routine, rel32 destination (0=none)
# [, rel32 field offset, mnemonic] for a relative branch that is not a call at offset 1
LEDGER = (
 ('loop_setup',0x403ab0,'81a008010000ffbfffff',MAIN,0),
 ('clock',0x403af0,'e80bd30a00',MAIN,0x4b0e00),
 ('pump',0x403af5,'e8b6f90c00',MAIN,0x4d34b0),
 ('channels',0x403afa,'e831660900',MAIN,0x49a130),
 ('pending_vm',0x403aff,'e86cbc0900',MAIN,0x49f770),
 ('services',0x403b04,'e867480900',MAIN,0x498370),
 ('input',0x403b09,'f686a004000001',MAIN,0),
 ('simulation',0x403f2a,'e821280100',MAIN,0x416750),
 ('cockpits',0x403f2f,'e8ac8e0100',MAIN,0x41cde0),
 ('render',0x403f34,'e817e00600',MAIN,0x471f50),
 ('post_render',0x403f39,'391d507c6000',MAIN,0),
 ('detail',0x403f5a,'e821300900',MAIN,0x496f80),
 ('presentation',0x403f5f,'391d507c6000',MAIN,0),
 ('tail',0x403f9e,'8b1560fc5700',MAIN,0),
 ('exit',0x4041eb,'8b86dc040000',MAIN,0),
 ('delayed_begin',0x42a45d,'e8aeb5ffff',OVERLAY,0x425a10),
 ('delayed_end',0x42a462,'e96f010000',OVERLAY,0x42a5d6),
 ('acquisition_begin',0x425bac,'6aff6a0155',PUBLISHER,0),
 ('acquisition_end',0x425bed,'8b15346f6000',PUBLISHER,0),
 ('cold_begin',0x49a3e4,'e8879a0500',SOUND,0x4f3e70),
 ('cold_end',0x49a3e9,'83c40885c0',SOUND,0),
 ('present_begin',0x4dac45,'8b4244ffd0',PRESENT,0),
 ('present_end',0x4dac4a,'3d68087688',PRESENT,0),
 ('input_body',0x403b3a,'39aed8040000',MAIN,0),
 ('input_after',0x403dc5,'f686a004000004',MAIN,0),
 ('publisher_begin',0x425a10,'538b5c2408',PUBLISHER,0),
 ('publisher_end',0x425c79,'5f5e5d5bc20400',PUBLISHER,0),
 ('playback_begin',0x499849,'e8e2f5ffff',PLAYBACK,0x498e30),
 ('playback_end',0x49984e,'83c4185fb801000000',PLAYBACK,0),
 ('create_begin',0x498ef8,'e843f2ffff',STREAM,0x498140),
 ('create_end',0x498f00,'83ef016685ff',STREAM,0),
 ('seek_begin',0x498f55,'e8d6740300',STREAM,0x4d0430),
 ('seek_end',0x498f5a,'83c40485c0',STREAM,0),
)
PHASE_COUNT = 33
SITES = tuple(common.HookSpec('game_phase_'+row[0],row[1],bytes.fromhex(row[2]),*row[3])
              for row in LEDGER)
TARGETS = {row[1]: row[4] for row in LEDGER if row[4]}
# rel32 field offset and expected mnemonic of every relative site (calls at offset 1)
RELATIVE = {row[1]: (row[5],row[6]) if len(row)>5 else (1,'jmp' if row[1]==0x42a462 else 'call')
            for row in LEDGER if row[4]}
JUMP_TABLE = (0x425a21,0x425a84,0x425ba2,0x425c1b,0x425b12)
MOV_JUMP_TABLE = (0x4997dd,0x499803,0x499859,0x499875,0x4998ee,0x499911,0x49982f,
                  0x499891,0x499992,0x4999ea,0x499a0a,0x499a23,0x499a68,0x499a8e)
RET_POP = {0x425a10:4,0x425c79:4}
JOIN_EDGES = (
    (0x403b10,0x403b3a),(0x403b26,0x403b3a),
    (0x403c85,0x403dc5),(0x403d84,0x403dc5),(0x403db6,0x403dc5),
    (0x425ba6,0x425c79),(0x425c1f,0x425c79),(0x425c57,0x425c79),
    (0x498ee0,0x498f00),(0x498eeb,0x498f00),
    (0x425bd6,0x425bed),(0x4041d8,0x403ab0),
)
SHARED_JOINS = {
    0x403b3a:{0x403b10,0x403b26},
    0x403dc5:{0x403c85,0x403d84,0x403db6},
    0x425c79:{0x425ba6,0x425c1f,0x425c57},
    0x498f00:{0x498ee0,0x498eeb},
}
# Context pins ABI-sensitive entry/exit semantics beyond the stolen bytes.
WITNESSES = {
 'delayed_request': (0x42a455,'51b8030000008bce'),
 'lock_timer': (0x42a438,'8b15346f60008b82180700002b86ec0100003de80300000f8c81010000'),
 'acquisition_skip': (0x425ba2,'33ed3bdd0f84cd000000'),
 'acquisition_conditional': (0x425bd6,'7515'),
 'cold_result_flags': (0x49a3ee,'8946107523'),
 'publisher_ret4': (0x425c79,'5f5e5d5bc20400'),
 'present_args': (0x4dac3c,'6a006a006a006a0050'),
}


def decode(exe=DEFAULT_EXE):
    decoded = {}
    for start,end in sorted({(s.function_start,s.function_end) for s in SITES}):
        run = subprocess.run([common.OBJDUMP,'-d','-Mintel','--insn-width=16',
                              f'--start-address={start}',f'--stop-address={end}',str(exe)],
                             check=True,capture_output=True,text=True,timeout=30)
        decoded[start,end] = common.parse_objdump(run.stdout,start,end)
    return decoded


def source_checks(text):
    # Shared historical parser names the last two fields misleadingly:
    # its rel32_offset means actual ret_pop; rel32_target means rel32_offset.
    actual = common.parse_source_specs(text)
    expected = [dict(name=s.name,va=s.va,bytes=s.expected,length=len(s.expected),
                     rel32_offset=RET_POP.get(s.va,0),rel32_target=RELATIVE[s.va][0] if s.va in TARGETS else 0)
                for s in SITES]
    return actual == expected


def relocated_bytes(spec, arena):
    """Independent x86 modulo-32-bit reference for SiteSpec's one rel32 field."""
    result = bytearray(spec.expected)
    if spec.va in TARGETS:
        offset = RELATIVE[spec.va][0]
        original = struct.unpack_from('<i',result,offset)[0]
        target = (spec.va+offset+4+original) & 0xffffffff
        struct.pack_into('<I',result,offset,(target-arena-offset-4) & 0xffffffff)
    return bytes(result)


def inspect(image, decoded, source):
    checks = {'preferred_base': image.image_base == common.IMAGE_BASE,
              'source_specs': source_checks(source)}
    all_instructions = [i for group in decoded.values() for i in group]
    rows = []
    for spec in SITES:
        own = decoded.get((spec.function_start,spec.function_end),[])
        # Include other decoded functions' edges without replacing own boundaries.
        row = common.inspect_site(image,spec,all_instructions if own else [])
        relative = spec.va in TARGETS
        span = [i for i in own if spec.va <= i.va < spec.end]
        controls = [i for i in span if common._is_direct_control(i) is not None]
        if relative:
            offset,expected_op = RELATIVE[spec.va]
            # Exactly one relative branch, the last instruction of the span, its
            # rel32 field at the declared offset (call/jmp: the whole span).
            row['relative_contract'] = (bool(span) and len(controls)==1 and controls[0] is span[-1] and
                controls[0].mnemonic==expected_op and
                common._is_direct_control(controls[0])==TARGETS[spec.va] and
                controls[0].end==spec.end and
                controls[0].va-spec.va+len(controls[0].raw)-4==offset and
                span[-1].raw==spec.expected[controls[0].va-spec.va:])
            row['relocation_targets'] = [
                (arena+offset+4+struct.unpack_from('<i',relocated_bytes(spec,arena),offset)[0]) & 0xffffffff
                for arena in (0x10000000,0x71000000,0xf1000000)]
            row['relocation_ok'] = row['relocation_targets']==[TARGETS[spec.va]]*3
        else:
            row['relative_contract'] = not controls
            row['relocation_ok'] = relocated_bytes(spec,0x71000000)==spec.expected
        row['ok'] = all(row[k] for k in ('bytes_ok','whole_instructions','no_interior_branch',
                                         'relative_contract','relocation_ok'))
        rows.append(row)
    checks['sites'] = all(row['ok'] for row in rows)
    ordered = sorted(SITES,key=lambda s:s.va)
    checks['nonoverlap'] = all(a.end <= b.va for a,b in zip(ordered,ordered[1:]))
    checks['complete_routines'] = all(decoded.get(b) for b in { (s.function_start,s.function_end) for s in SITES })
    by_va = {i.va:i for i in all_instructions}
    raw_table = image.read(0x425c80,20)
    table = struct.unpack('<5I',raw_table) if raw_table and len(raw_table)==20 else ()
    checks['publisher_jump_table'] = (table==JUMP_TABLE and all(target in by_va for target in table)
        and all(not s.va < target < s.end for s in SITES for target in table))
    raw_mov_table = image.read(0x499aec,4*len(MOV_JUMP_TABLE))
    mov_table = struct.unpack('<14I',raw_mov_table) if raw_mov_table and len(raw_mov_table)==56 else ()
    checks['mov_jump_table'] = (mov_table==MOV_JUMP_TABLE and all(target in by_va for target in mov_table)
        and all(PLAYBACK[0]<=target<PLAYBACK[1] for target in mov_table)
        and all(not s.va < target < s.end for s in SITES for target in mov_table))
    checks['abi_context'] = all(image.read(va,len(bytes.fromhex(raw)))==bytes.fromhex(raw)
                                for va,raw in WITNESSES.values())
    for name,bounds,start,end in (
        ('delayed_normal_endpoint',OVERLAY,0x42a45d,0x42a462),
        ('acquisition_normal_endpoint',PUBLISHER,0x425bac,0x425bed),
        ('cold_normal_endpoint',SOUND,0x49a3e4,0x49a3e9),
        ('present_normal_endpoint',PRESENT,0x4dac45,0x4dac4a),
        ('playback_command6_entry',PLAYBACK,0x49982f,0x499849),
        ('playback_normal_endpoint',PLAYBACK,0x499849,0x49984e),
        ('create_normal_endpoint',STREAM,0x498ef8,0x498f00),
        ('seek_normal_endpoint',STREAM,0x498f55,0x498f5a),
        ('publisher_mode3_endpoint',PUBLISHER,0x425c1b,0x425c79)):
        checks[name] = all_paths_reach(decoded.get(bounds,[]),start,end)
    # Entry edges are allowed. Preserve every proved shared join, both input
    # subdivisions and the normal main-loop backedge without treating shutdown
    # as a phase. The create join deliberately starts at 498f00, after cleanup.
    checks['join_edges'] = all(at in by_va and common._is_direct_control(by_va[at])==target
                              for at,target in JOIN_EDGES)
    checks['shared_join_sources'] = all(
        {i.va for i in all_instructions if common._is_direct_control(i)==target}==sources
        for target,sources in SHARED_JOINS.items())
    publisher_end = [by_va.get(at) for at in (0x425c79,0x425c7a,0x425c7b,0x425c7c,0x425c7d)]
    checks['publisher_ret4_epilogue'] = (all(publisher_end)
        and [i.mnemonic for i in publisher_end]==['pop','pop','pop','pop','ret']
        and publisher_end[-1].raw==b'\xc2\x04\x00' and publisher_end[-1].end==0x425c80)
    checks['publisher_mode3_switch'] = (0x425a1a in by_va
        and by_va[0x425a1a].raw==bytes.fromhex('ff2485805c4200')
        and len(table)==5 and table[3]==0x425c1b)
    checks['mov_command6_switch'] = (0x4997d6 in by_va
        and by_va[0x4997d6].raw==bytes.fromhex('ff2485ec9a4900')
        and len(mov_table)==14 and mov_table[6]==0x49982f)
    checks['present_indirect_call'] = (0x4dac48 in by_va and by_va[0x4dac48].raw==b'\xff\xd0')
    return {'result':'PASS' if all(checks.values()) else 'FAIL','checks':checks,'sites':rows}


def verify(exe=DEFAULT_EXE,source=SOURCE):
    data = Path(exe).read_bytes()
    try:
        report = inspect(common.Image(data),decode(exe),Path(source).read_text())
    except (ValueError,OSError,struct.error,subprocess.SubprocessError) as error:
        return {'result':'FAIL','checks':{'decode':False},'error':str(error)}
    report['checks']['exe_identity'] = exe_identity.identity_ok(data)
    report['exe_info'] = exe_identity.info(data)
    report['exe_sha256'] = report['exe_info']['sha256']
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
