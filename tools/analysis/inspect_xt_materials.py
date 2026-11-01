#!/usr/bin/env python3
"""Derive the bounded 14-program XT material edit contract; never export bytes.

This source proof uses only exact local original programs and the existing SM3
walker. Reported fields are identities, offsets and register/channel roles.
The native equations and authored DEFAULT policy are in architecture/xt-materials.md.
"""
import argparse
import json
from pathlib import Path
import inspect_motion_output_profiles as motion

PAIRS = [
    ('fffdabd910793aba', False, False, False, 1409, 0),
    ('e6794b6ec37ff71a', False, False, False, 1417, 0),
    ('5f82ecacd39529cd', True, False, False, 1529, 0),
    ('f1b0e820c7b488c3', True, False, False, 1474, 0),
    ('6733b119142c8d42', True, False, False, 1466, 0),
    ('496049cec2066ed3', True, False, False, 1474, 0),
    ('d51cf763125cb85a', True, True, False, 1396, 5),
    ('31445adb0a62d134', True, True, False, 1418, 4),
    ('fd58e6b7e8cf969c', False, False, True, 1429, 4),
    ('dd87737d697c6764', False, False, True, 1455, 4),
    ('d22f2ce2c740e6a7', True, False, True, 1552, 4),
    ('1de3d2dde345a7e3', True, False, True, 1578, 4),
    ('75fb9c6b05e28ea2', True, False, True, 1541, 4),
    ('edaef099780fcafe', True, False, True, 1567, 4),
]

# AP register roles: LightDir_Color0/1 in c6/c8, palette from c14 (DEFAULT) or c17
# (BUMPMAP), temporaries up to r5 (r6 terraformer). --fl-report supplies the FL
# rows' CTAB-derived roles instead (light0, light1, palette_first, temporary base).
def derive(directory, pairs=PAIRS, roles=None):
    result = []
    for hash_, bump, damage, terra, base_at, base_reg in pairs:
        light0, light1, palette_first, temporary_base = (roles or {}).get(
            hash_, (6, 8, 17 if bump else 14, 7 if terra else 6))
        code = (directory / f'ps_{hash_}.bin').read_bytes()
        items = motion.instructions(code)[1]
        fingerprint=14695981039346656037
        for byte in code: fingerprint=((fingerprint^byte)*1099511628211)&0xffffffffffffffff
        assert f'{fingerprint:016x}' == hash_
        row = dict(ps=hash_, vs='37c34a7478544c14' if bump else '494fe349b8bc12ec',
                   words=len(code)//4, bump=bump, damage=damage, terra=terra,
                   base_at=base_at, base_reg=base_reg, lights=[], palette=[], clamps=[],
                   scalar=[], rgb=[], textures=[], branches=[])
        tainted = {('v0', c) for c in 'xyz'} | {(f'c{i}', c) for i in [light0,light1,*range(palette_first,palette_first+5)] for c in 'xyz'}
        stack=[]
        for item in items:
            at, op = item['dword'], item['opcode']
            if at == base_at:
                tainted.update((f'r{base_reg}', c) for c in 'xyz')
            dst, sources = motion.split_operands(item,3)
            if op in [40,41]:
                stack.append([set(tainted),None]); row['branches'].append([at,op])
            elif op == 42:
                stack[-1][1]=set(tainted);tainted=set(stack[-1][0]);row['branches'].append([at,op])
            elif op == 43:
                before,other=stack.pop();tainted |= other if other is not None else before;row['branches'].append([at,op])
            if op in motion.HEADER_OPCODES or not dst:
                continue
            if dst['name']=='oC0':
                row['final_rgb' if dst['mask']=='xyz' else 'alpha']=at
            if op == 66:
                sampler=sources[1]['register']; register=dst['register']
                row['textures'].append(dict(at=at,sampler=sampler,register=register))
            operand=2
            for source in sources:
                if source['name'] in [f'c{light0}',f'c{light1}']:
                    assert dst['mask']=='xyz' and op in [5,4]
                    row['lights'].append([at+operand,source['register']])
                if source['register_type']==2 and palette_first<=source['register']<palette_first+5:
                    assert dst['mask']=='xyz' and source['swizzle']=='xyzw' and op in [5,4]
                    row['palette'].append([at+operand,source['register']])
                if source['name']==('v7' if bump else 'v5'):
                    assert dst['mask']=='xyz' and source['swizzle'] in ['xxxx','yyyy'] and op in [5,4]
                    row['scalar'].append([at+operand,'xy'.index(source['swizzle'][0])])
                operand+=1+int(source.get('relative',False))
            if op==1 and any(source['name']=='v0' for source in sources):
                assert dst['mask']=='xyz' and set(dst['modifiers'])=={'partial_precision','saturate'}
                row['clamps'].append(at)
            # Independent branch-aware linear-origin flow. Scalar/angular work
            # never receives a radiance tag; samples kill overwritten lanes.
            depends = op!=66 and any((source['name'],source['swizzle']['xyzw'.index(c)]) in tainted
                                    for source in sources for c in dst['mask'])
            for c in dst['mask']: tainted.discard((dst['name'],c))
            if depends:
                assert dst['mask']=='xyz', (hash_,at,dst)
                assert op in [1,2,4,5], (hash_,at,op)
                row['rgb'].append(at)
                tainted.update((dst['name'],c) for c in 'xyz')
            if op==66 and sources[1]['register'] in ([3,4] if bump else [2,3]):
                tainted.update((dst['name'],c) for c in 'xyz')
        assert not stack and len(row['lights'])==4 and len(row['palette'])==5
        assert len(row['clamps'])==2 and len(row['scalar'])==2
        assert sorted(x[1] for x in row['scalar'])==[0,1]
        assert len(row['textures'])==(7 if bump else 5)
        assert base_at in [it['dword'] for it in items]
        assert row['final_rgb'] in row['rgb']
        row['temps']=sorted({x['register'] for item in items if item['opcode'] not in motion.HEADER_OPCODES
                            for x in ([motion.split_operands(item,3)[0]]+motion.split_operands(item,3)[1])
                            if x and x['register_type']==0})
        assert max(row['temps'])==temporary_base-1
        result.append(row)
    return result

def fl_rows(report):
    """The FL XT rows of inspect_fl_linear_materials.py's report: this tool's own derivation over them and
    the generator's fields side by side (equal when both derivations agree)."""
    rows=[p for p in report['programs'] if p['row']['family']=='xt']
    pairs=[(p['ps'],p['row']['bump'],False,p['row']['terra'],p['row']['base_at'],p['row']['base_reg']) for p in rows]
    roles={p['ps']:(p['row']['light0'],p['row']['light1'],p['row']['palette_first'],p['row']['temporary_base']) for p in rows}
    return pairs, roles, {p['ps']:p['row'] for p in rows}

def compare(derived, generated):
    """Field-by-field agreement of a derived row with the generator's row."""
    textures=sorted(derived['textures'],key=lambda t:t['sampler'])
    checks=dict(final_rgb=derived['final_rgb']==generated['final_rgb'], alpha=derived['alpha']==generated['alpha'],
                textures=[t['at'] for t in textures]==generated['textures'],
                texture_registers=[t['register'] for t in textures]==generated['texture_registers'],
                lights=sorted(map(list,derived['lights']))==sorted(map(list,generated['lights'])),
                palette=sorted(map(list,derived['palette']))==sorted(map(list,generated['palette'])),
                clamps=derived['clamps']==generated['clamps'],
                scalar=sorted(map(list,derived['scalar']))==sorted(map(list,generated['scalar'])),
                rgb=derived['rgb']==generated['rgb'])
    return [name for name,ok in checks.items() if not ok]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--fl-report',type=Path,help='derive the FL XT rows of this inspect_fl_linear_materials.py '
                        'report (FL corpus in --programs) and compare them with the report')
    args=parser.parse_args()
    if args.fl_report:
        pairs,roles,generated=fl_rows(json.loads(args.fl_report.read_text()))
        rows=derive(args.programs,pairs,roles)
        mismatches={row['ps']:compare(row,generated[row['ps']]) for row in rows}
        mismatches={k:v for k,v in mismatches.items() if v}
        args.output.write_text(json.dumps({'schema':1,'scope':'12 FL XT original PS derived by this tool with the FL '
                                           'register roles, compared with inspect_fl_linear_materials.py; derived '
                                           'metadata only','programs':len(rows),
                                           'rgb_sites':sum(len(r['rgb']) for r in rows),
                                           'fields_compared':['final_rgb','alpha','textures','texture_registers','lights',
                                                              'palette','clamps','scalar','rgb'],
                                           'mismatches':mismatches},indent=1)+'\n')
        print(f'{len(rows)} FL XT programs; {len(mismatches)} differ from the generator report')
        return 1 if mismatches else 0
    rows=derive(args.programs)
    args.output.write_text(json.dumps({'schema':1,'scope':'14 exact XT original PS; derived metadata only','programs':rows},indent=2)+'\n')
    print(f'{len(rows)} XT programs; {sum(len(r["rgb"]) for r in rows)} RGB sites; no bytecode exported')
if __name__=='__main__': raise SystemExit(main())
