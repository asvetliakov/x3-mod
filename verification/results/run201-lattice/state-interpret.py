#!/usr/bin/env python3
"""Read-only packet versus frozen conditional fixture/source comparison."""
import json, struct, hashlib, sys
from pathlib import Path
import numpy as np
ROOT=Path('/Users/asvetl/x3-mod')
sys.path[:0]=[str(ROOT/'verification/probe'),'/tmp/x3-lattice-gpu-witness/verification/probe']
from lattice_state_packet import load
from lattice_gpu_inputs import instructions, words, fnv
RUN=Path('/tmp/x3-bottleX3-run201'); INPUT=Path('/tmp/x3-lattice-gpu-inputs-v5')
manifest=json.loads((INPUT/'manifest.json').read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for name,expected in manifest['files'].items():assert sha(INPUT/name)==expected,name
packets=sorted([load(p,require_complete=True) for p in RUN.glob('lattice-state-*.json')],key=lambda p:p['frame'])
assert [p['frame'] for p in packets]==[9796,10442,12010]
def fields(r):return {(f['kind'],f['index']):f for f in r['fields']}
def ints(f,k,i):return [int(x,16) for x in f[k,i]['words']]
def hx(w):return [f'{x:08x}' for x in w]
def diff(a,b):return [{'word':i,'register':i//4,'lane':'xyzw'[i%4],'fixture':f'{x:08x}','live':f'{y:08x}'} for i,(x,y) in enumerate(zip(a,b)) if x!=y]
def halton(n,b):
 f=np.float32(1); v=np.float32(0)
 while n:
  f=np.float32(f/np.float32(b));v=np.float32(v+np.float32(f*np.float32(n%b)));n//=b
 return v
raw=(INPUT/'inputs.bin').read_bytes(); pos=12; fixture=[]
for slot in range(2):
 c=words(raw[pos:pos+4416]);pos+=4416
 a=np.array(c[:1024],dtype='<u4').view('<f4').reshape(256,4).copy()
 a[252:256]=a[24:28]
 for row,j in [(24,np.float32(np.float32(2)*(halton(6,2)-np.float32(.5))/np.float32(1280))), (25,np.float32(np.float32(-2)*(halton(6,3)-np.float32(.5))/np.float32(768)))]:
  a[row]=np.float32(a[row]+np.float32(j*a[27]))
 fixture.append({'f':a.view('<u4').flatten().tolist(),'i':c[1024:1088],'b':c[1088:]})
 pos+=(4 if slot==0 else 3)*132
assert pos==len(raw)
# Fixture explicit state calls, plus arm2 alpha test. No implicit defaults promoted to measurements.
render={7:1,8:3,14:1,15:1,22:1,23:4,24:1,25:7,26:0,27:0,28:0,52:0,152:0,168:15,174:0,175:0,190:15,194:0,195:0,206:0,**{128+i:0 for i in range(8)}}
sampler={1:1,2:1,5:2,6:3,7:2,8:0,9:0,10:16,11:0}
vs=words((INPUT/'vertex.bin').read_bytes()); original_ps=words(Path('/tmp/x3-bottleX3-run177/ps_5e0a10fe752b6140.bin').read_bytes()); original_instructions=instructions(original_ps)
records=[]
for p in packets:
 for slot,r in enumerate(p['records']):
  f=fields(r);v=ints(f,'shader',0); ps=ints(f,'shader',1);pi=instructions(ps)
  assert v==vs
  assert all(ints(f,'sampler',stage*16+k)==[value] for stage in [0,3] for k,value in sampler.items())
  rd=[{'state':k,'fixture':f'{value:08x}','live':f[kind,k]['words'][0]} for kind in ['render'] for k,value in render.items() if ints(f,kind,k)!=[value]]
  assert rd==[{'state':168,'fixture':'0000000f','live':'00000007'}]
  assert ints(f,'constants_f',1)[12:16]==[0]*4
  assert ints(f,'constants_f',0)[39*4]==0x3f800000 and ints(f,'constants_b',0)[0]==0
  assert ints(f,'constants_f',0)[37*4:39*4]==[0x3f800000,0,0,0,0,0x3f800000,0,0]
  matches={str(at):[k for k,q in pi.items() if q==original_instructions[at]] for at in [1062,1065,1083,1092,1298,1336,1340,1349]}
  assert all(len(x)==1 for x in matches.values())
  assert not any(q[0]&65535==65 for q in pi.values())
  assert [k for k,q in pi.items() if len(q)>1 and q[1]&0xf000ffff==0x80000800 and q[1]&0x80000]==[1546]
  # Confirm alpha source r2.w survives between LRP and output MUL.
  assert not any(len(q)>1 and q[1]&0xf000ffff==0x80000002 and q[1]&0x80000 for k,q in pi.items() if 1445<=k<1546)
  for stage in [0,3]:
   levels=10 if slot==0 else 11
   assert ints(f,'texture_lod',stage)==[3,0,levels]
   for level in range(levels):
    desc=ints(f,'texture_desc',stage*32+level); tex=manifest['textures'][f'g{4 if slot==0 else 18}_{"diff" if stage==0 else "light"}.dds'][level]
    assert desc==[0x35545844,1,0,1,0,0,tex['width'],tex['height']]
  assert all(f['target',i]['hr']=='00000000' and f['target',i]['words'] for i in [0,1,2,4])
  assert f['target',3]['hr']=='88760866' and f['target',3]['words']==[]
  assert ints(f,'viewport',0)==[0,0,1280,768,0,0x3f800000]
  for target,fmt,usage in [(0,0x71,1),(1,0x74,1),(2,0x74,1),(4,0x4d,2)]:
   assert ints(f,'surface_desc',target)==[fmt,1,usage,0,0,0,1280,768]
  assert fnv(struct.pack('<%dI'%len(ps),*ps))==0xa62894482423b659
  assert ints(f,'vertex_desc',0)[2:4]==[8,1] and ints(f,'index_desc',0)[2:4]==[8,1]
  assert r['submitted'] and r['result']=='00000000'
  fullzero=[0]*896; fullzero[215*4]=struct.unpack('<I',struct.pack('<f',180048 if slot else 42953))[0];fullzero[215*4+1]=0x3f800000
  constants={t:diff(fixture[slot][t],ints(f,'constants_'+t,0)) for t in ['f','i','b']}
  constants['ps_f']=diff(fullzero,ints(f,'constants_f',1))
  records.append({'frame':p['frame'],'slot':slot,'draw':r['draw'],'vs_fixture_equal':True,'vs_dwords':len(v),'vs_fnv':f'{fnv(struct.pack("<%dI"%len(v),*v)):016x}', 'ps_dwords':len(ps),'ps_fnv':f'{fnv(struct.pack("<%dI"%len(ps),*ps)):016x}', 'alpha_slice_original_to_live_dword':matches,'render_differences':rd,'sampler_explicit_matches':18,'sampler_extra_words':{str(stage):{str(k):f['sampler',stage*16+k]['words'][0] for k in [3,4,12,13]} for stage in [0,3]},'texture_lod':{str(stage):f['texture_lod',stage]['words'] for stage in [0,3]}, 'route_words':f['route',0]['words'],'targets':{str(i):f['target',i] for i in range(5)}, 'surface_desc':{str(i):q['words'] for (k,i),q in f.items() if k=='surface_desc'},'constant_differences':constants})
base=fields(packets[0]['records'][1]); temporal=[]
for p in packets[1:]:
 f=fields(p['records'][1]); temporal.append({'frame':p['frame'],'vs_f':diff(ints(base,'constants_f',0),ints(f,'constants_f',0)), 'ps_f':diff(ints(base,'constants_f',1),ints(f,'constants_f',1))})
out={'status':'PASS','proposal':'pending parent ratification','inputs':{'manifest':{'path':str(INPUT/'manifest.json'),'sha256':sha(INPUT/'manifest.json')},'packets':[{'path':str(p),'sha256':sha(p)} for p in sorted(RUN.glob('lattice-state-*.json'))], 'fixture_sources':[{'path':str(p),'sha256':sha(p)} for p in [Path('/tmp/x3-lattice-gpu-witness/verification/probe/lattice_gpu_inputs.py'),Path('/tmp/x3-lattice-gpu-witness/verification/probe/lattice_gpu_fixture.cpp')]]},'records':records,'cross_burst_constants':temporal,'witness':'All six effective PS programs export oC1/oC2 and queried native targets1/2 are bound; six sampled draws submit S_OK. Payload and draw-input coherence remain unqualified.','limits':['State snapshot only; no payload identity or writer ownership.','Run201 different view/time cannot retroactively certify Run177.','No Wine, game or native Windows runtime execution.']}
out['inputs']['analysis_script']={'path':__file__,'sha256':sha(Path(__file__))}
out['constant_diff_encoding']=['flat_word_index','fixture_hex','live_hex']; compact=json.loads(json.dumps(out))
for r in compact['records']:
 r['constant_differences']={k:[[q['word'],q['fixture'],q['live']] for q in v] for k,v in r['constant_differences'].items()}
for r in compact['cross_burst_constants']:
 for k in ['vs_f','ps_f']:r[k]=[[q['word'],q['fixture'],q['live']] for q in r[k]]
path=Path('/tmp/x3-run201-state-interpret.json');path.write_text(json.dumps(compact,separators=(',',':'),allow_nan=False)+'\n'); check=json.loads(path.read_text()); assert len(check['records'])==6
print(json.dumps({'status':'PASS','records':6,'explicit_sampler_matches':108,'render_nonmask_differences':0,'vs_program_equal':6,'bound_auxiliary_targets':12,'constant_diff_counts':[{k:len(v) for k,v in r['constant_differences'].items()} for r in records],'cross_burst_registers':[{k:sorted({q['register'] for q in t[k]}) for k in ['vs_f','ps_f']} for t in temporal],'output':str(path)}))
