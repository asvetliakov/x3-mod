#!/usr/bin/env python3
"""Derive seven exact SM3 glass source contracts; only names/roles/offsets leave.

The complete equations and all 30 archive pairs are in architecture/glass-materials.md.
This bounded extension does not reinterpret SM1/SM2 programs or admit draws.
"""
import argparse
import json
from pathlib import Path
import inspect_motion_output_profiles as motion

VERTICES = {'c30104cb0efb6675': (True,431,446,503,546),
            'e2ad860d5fbb3e59': (True,431,446,503,546),
            '74fdc00d802b4027': (False,392,400,458,501)}
PIXELS = {'a66fb1981ba755b2':True,'ebc9b2b3f1564e9a':True,
          'f31c9e2701c8eee4':False,'9d49f288800f898d':False}
PAIRS = [('c30104cb0efb6675',p) for p,b in PIXELS.items() if b] + [
    (v,p) for v in list(VERTICES)[1:] for p,b in PIXELS.items() if not b]


def derive(directory):
    result=[]
    for stage, hashes in [('vs',VERTICES),('ps',PIXELS)]:
        for hash_ in hashes:
            code=(directory/f'{stage}_{hash_}.bin').read_bytes()
            assert motion.fnv1a64(code)==hash_, 'exact original required'
            items=motion.instructions(code)[1]
            row=dict(id=f'{stage}_{hash_}',words=len(code)//4, rgb=[], lights=[],textures=[],color_uses=[],fresnel=[],declarations=[])
            base=PIXELS.get(hash_,False)
            tainted={('v0',c) for c in 'xyz'}|{(f'c{i}',c) for i in ([1,3] if base else [1]) for c in 'xyz'}
            for item in items:
                at,op=item['dword'],item['opcode']
                if op==31:
                    kind,num=motion.register_of(item['words'][1])
                    if kind==(6 if stage=='vs' else 1) and num in ([1,6] if stage=='vs' else [0,5]):
                        row['declarations'].append([at,num,(item['words'][1]>>16)&15,(item['words'][1]>>20)&15,item['words'][0]&31,(item['words'][0]>>16)&15])
                    continue
                if op in motion.HEADER_OPCODES:continue
                dst,sources=motion.split_operands(item,3)
                if not dst:continue
                if stage=='vs':
                    if dst['name']=='o6':
                        assert op==14 and dst['mask']=='x';row['fresnel'].append(at)
                    continue
                if dst['name']=='oC0':row['final_rgb' if dst['mask']=='xyz' else 'alpha']=at
                if op==66:
                    assert dst['name']=='r0' and dst['mask']=='xyzw'
                    row['textures'].append([at,sources[1]['register']])
                for n,source in enumerate(sources,2):
                    if source['name'] in ['c1','c3'] and (base or source['name']=='c1'):
                        assert op in [4,5] and dst['mask']=='xyz' and source['swizzle']=='xyzw'
                        row['lights'].append([at+n,source['register']])
                    if source['name'] in ['v0','v5']:
                        row['color_uses'].append([at+n,source['name'],source['swizzle']])
                        if source['name']=='v5':
                            assert op==5 and dst['mask']=='w' and source['swizzle']=='xxxx'
                            row['fresnel'].append(at+n)
                if op==1 and sources[0]['name']=='v0':
                    assert dst['mask']=='xyz' and set(dst['modifiers'])=={'partial_precision','saturate'}
                    row['clamp']=at
                depends=op!=66 and any((s['name'],s['swizzle']['xyzw'.index(c)]) in tainted for s in sources for c in dst['mask'])
                for c in dst['mask']:tainted.discard((dst['name'],c))
                if depends:
                    assert dst['mask']=='xyz' and op in [1,2,4,5]
                    row['rgb'].append(at);tainted.update((dst['name'],c) for c in 'xyz')
                if op==66 and sources[1]['register'] in [0,2]:tainted.update(('r0',c) for c in 'xyz')
            if stage=='ps':
                assert len(row['textures'])==3 and len(row['lights'])==(4 if base else 1)
                assert len(row['fresnel'])==1 and len(row['color_uses'])==3
                assert row['color_uses']==sorted([[row['clamp']+2,'v0','xyzw'],[row['fresnel'][0],'v5','xxxx'],[row['alpha']+3,'v0','wwww']])
                assert row['declarations']==[[row['declarations'][0][0],0,15,2,10,0],[row['declarations'][1][0],5,1,2,10,1]]
                row['base']=base
            else:
                loop,point,emissive,alpha,fresnel=VERTICES[hash_]
                assert row['fresnel']==[fresnel]
                row.update(loop=loop,point=point,emissive=emissive,alpha=alpha)
                assert [d[1:] for d in row['declarations']]==[[1,15,0,10,0],[6,1,0,10,1]]
            result.append(row)
    return {'schema':1,'scope':'Seven exact SM3 glass originals / six opaque-eligible pairs; no GPU qualification',
            'pairs':PAIRS,'programs':result,'transport':'Linear P: COLOR1.xyz o6/v5 full precision. Native Fresnel: COLOR0.x o1/v0 retains PP. Native alpha: COLOR0.w unchanged. Motion TEX4 and depth TEX5 unchanged.'}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'));p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.write_text(json.dumps(derive(a.programs),indent=2)+'\n')
