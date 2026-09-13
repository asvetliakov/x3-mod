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

def derive(directory):
    result = []
    for hash_, bump, damage, terra, base_at, base_reg in PAIRS:
        code = (directory / f'ps_{hash_}.bin').read_bytes()
        items = motion.instructions(code)[1]
        fingerprint=14695981039346656037
        for byte in code: fingerprint=((fingerprint^byte)*1099511628211)&0xffffffffffffffff
        assert f'{fingerprint:016x}' == hash_
        row = dict(ps=hash_, vs='37c34a7478544c14' if bump else '494fe349b8bc12ec',
                   words=len(code)//4, bump=bump, damage=damage, terra=terra,
                   base_at=base_at, base_reg=base_reg, lights=[], palette=[], clamps=[],
                   scalar=[], rgb=[], textures=[], branches=[])
        palette_first = 17 if bump else 14
        tainted = {('v0', c) for c in 'xyz'} | {(f'c{i}', c) for i in [6,8,*range(palette_first,palette_first+5)] for c in 'xyz'}
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
                if source['name'] in ['c6','c8']:
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
        assert max(row['temps'])==(6 if terra else 5)
        result.append(row)
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();rows=derive(args.programs)
    args.output.write_text(json.dumps({'schema':1,'scope':'14 exact XT original PS; derived metadata only','programs':rows},indent=2)+'\n')
    print(f'{len(rows)} XT programs; {sum(len(r["rgb"]) for r in rows)} RGB sites; no bytecode exported')
if __name__=='__main__': main()
