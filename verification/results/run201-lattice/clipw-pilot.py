#!/usr/bin/env python3
"""Bounded host-only exact-subset/transform-order discriminator, not hardware emulation."""
import hashlib,json,struct,time
from pathlib import Path
import numpy as np
start=time.process_time();base=Path('/tmp/x3-lattice-gpu-inputs-v5');manifest=json.loads((base/'manifest.json').read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for name,expected in manifest['files'].items():assert sha(base/name)==expected
witness_path=Path('/tmp/x3-lattice-fresh-discrepancy.json');witness=json.loads(witness_path.read_text())
assert sha(base/'manifest.json')==witness['inputs']['manifest']['sha256']
raw=(base/'inputs.bin').read_bytes();pos=12;faces={}
def halton(n,b):
 f=np.float32(1);v=np.float32(0)
 while n:f=np.float32(f/b);v=np.float32(v+np.float32(f*(n%b)));n//=b
 return v
j=np.array([np.float32(2)*(halton(6,2)-np.float32(.5))/np.float32(1280),np.float32(-2)*(halton(6,3)-np.float32(.5))/np.float32(768)],np.float32)
for slot in range(2):
 m=np.frombuffer(raw[pos:pos+4096],'<f4').reshape(256,4)[24:28].copy();pos+=4416
 mj=m.copy();mj[:2]=np.float32(m[:2]+np.float32(j[:,None]*m[3]))
 for _ in range(4 if slot==0 else 3):
  g,f,owner=struct.unpack_from('<III',raw,pos);v=np.frombuffer(raw[pos+12:pos+132],'<f2').astype(np.float32).reshape(3,20)[:,:4];pos+=132
  projected={}
  for mode,mat in [('oracle_postdivide_jitter',m),('fixture_postjitter_constants',mj)]:
   q=np.sum(v[:,None,:]*mat[None,:,:],axis=-1,dtype=np.float32).astype(np.float64)
   xy=q[:,:2]/q[:,3,None]*[640,-384]+[640,384]
   if mode=='oracle_postdivide_jitter':xy+=np.array([-.125,float(np.float32(halton(6,3)-np.float32(.5)))])
   projected[mode]=(xy,q[:,3])
  faces[owner]=projected
assert pos==len(raw) and len(faces)==7
rows=[]
for r in witness['records']:
 x,y=r['point'];row={'point':r['point'],'GPU_owner':r['fixture_owner'],'GPU_W':r['combined_W'],'exact_capture_control':-1 in r['matching_depth_and_W_cases'],'modes':{}}
 for mode,(t,w) in faces[r['fixture_owner']].items():
  a,b,c=t;area=np.cross(b-a,c-a);u=np.cross(np.array([x,y])-a,c-a)/area;v=np.cross(b-a,np.array([x,y])-a)/area
  ww=1/((1-u-v)/w[0]+u/w[1]+v/w[2]);row['modes'][mode]={'W':float(ww),'residual':float(ww-r['combined_W']),'barycentric':[float(1-u-v),float(u),float(v)]}
 rows.append(row)
summary={mode:{'controls_within_0_02':sum(abs(r['modes'][mode]['residual'])<=.02 for r in rows if r['exact_capture_control']),'controls':12,'max_control_abs_residual':max(abs(r['modes'][mode]['residual']) for r in rows if r['exact_capture_control']),'control_range':[min(r['modes'][mode]['residual'] for r in rows if r['exact_capture_control']),max(r['modes'][mode]['residual'] for r in rows if r['exact_capture_control'])]} for mode in rows[0]['modes']}
out={'proposal':'pending parent ratification','inputs':{'manifest':sha(base/'manifest.json'),'witness':sha(witness_path),'script':sha(Path(__file__))},'rows':rows,'summary':summary,'cpu_seconds':time.process_time()-start,'limits':['GPU owner is supplied only to isolate arithmetic; this is not a CPU ownership qualifier.','np.sum DP4 and ideal interpolation remain conditional; no hardware subpixel/centroid/precision claim.','Known historical GPU/capture discrepancies remain unknown.']}
p=Path('/tmp/x3-run201-clipw-pilot.json');p.write_text(json.dumps(out,allow_nan=False,indent=2)+'\n');assert len(json.loads(p.read_text())['rows'])==14
print(json.dumps({'summary':summary,'cpu_seconds':out['cpu_seconds'],'output':str(p)}))
