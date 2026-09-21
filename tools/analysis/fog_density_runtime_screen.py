#!/usr/bin/env python3
"""Frozen two-level stored-final-density CPU comparison; no production cache."""
from __future__ import annotations
import argparse,hashlib,importlib.util,json,math,platform,sys,time
from itertools import product
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); return m
fog=load('fog_distance_replay',HERE/'fog_distance_replay.py'); preview=load('fog_mass_detail_preview',HERE/'fog_mass_detail_preview.py'); screen=preview.screen; patches=screen.patches
F=np.float32; SUN=np.array([1.,0.,0.],F); SIGMA=screen.SIGMA; W,H=128,72; FAR=fog.FAR; NEAR=fog.NEAR; WINDOW=fog.WINDOW_START
LEVELS={'fine':512.,'far':4096.}; SEGMENTS=(('near',0.,NEAR),('middle',NEAR,WINDOW),('shell',WINDOW,FAR)); OFFSETS=np.array(list(product((-.25,.25),repeat=3)),np.float64)
EXPECTED_REFINEMENT='0fdfc2c872de2ebe51b241c6eea241bec683a0de2cb217e9b36592decc3a9ad4'; EXPECTED_PLAN='ca7674296bb28ad26c7c043a7ca812dc4f0f68c592e1fd54f383fa2ccf8aea38'; DEFAULT_PLAN=Path('/Users/asvetl/x3-mod/docs/architecture/fog-density-runtime-plan.md'); DEPTHS=np.array([1000.,11999.,12001.,149999.,150001.,199999.,200001.]); WITNESS=((0,0),(31,0),(0,17),(31,17),(16,9))
def digest(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''): h.update(b)
 return h.hexdigest()
def metric(x):
 a=np.asarray(x,np.float64).ravel(); return {k:float(v) for k,v in [('mean',a.mean()),('p50',np.percentile(a,50)),('p99',np.percentile(a,99)),('max',a.max(initial=0))]}|{'count':int(a.size)}
def rotate(x,M):
 a=np.asarray(x,np.float64); return np.stack(tuple(a[...,0]*M[i,0]+a[...,1]*M[i,1]+a[...,2]*M[i,2] for i in range(3)),axis=-1)
def field(points):
 x=np.asarray(points,np.float64); R=screen.R; R2=np.array([[sum(R[i,k]*R[k,j] for k in range(3)) for j in range(3)] for i in range(3)])
 f=(2*patches.value_noise(x/(2*screen.P),3)+patches.value_noise(rotate(x,R)/screen.P,4))/3
 B=fog.smoothstep(.10,.40,f).astype(F); dlo=((patches.value_noise(rotate(x,R2)/(screen.P/4),5)+1)/2).astype(F); dhi=((patches.value_noise(x/(screen.P/16),6)+1)/2).astype(F)
 return np.maximum(0,B*(.50+.50*dlo)-.20*(1-B)*dhi).astype(F)
def rays(pose):
 x,y=np.meshgrid(np.arange(W),np.arange(H)); u=(x+.5)/W; v=(y+.5)/H; t=math.tan(math.radians(30)); local=np.stack(((2*u-1)*(W/H)*t,(1-2*v)*t,np.ones_like(u)),axis=-1); local/=np.linalg.norm(local,axis=-1,keepdims=True)
 forward=np.asarray(pose['forward'],np.float64); up=np.asarray(pose['up'],np.float64); right=np.cross(up,forward); right/=np.linalg.norm(right); up=np.cross(forward,right); world=local[...,0,None]*right+local[...,1,None]*up+local[...,2,None]*forward; return (world/np.linalg.norm(world,axis=-1,keepdims=True)).reshape(-1,3).astype(F)
def populations():
 sx,sy=np.meshgrid(4*np.arange(32)+2,4*np.arange(18)+2); cx,cy=np.meshgrid(np.arange(48,80),np.arange(27,45)); return {'stratified':(sx.ravel(),sy.ravel()),'central_crop':(cx.ravel(),cy.ravel())}
def window_origin(camera,delta): return np.floor(np.asarray(camera)/delta).astype(np.int64)-63
def pack_address(local):
 q=np.asarray(local,np.int64); z=q[...,2]; group=z//4; return q[...,0]+(group%8)*128,q[...,1]+(group//8)*128,z%4
class LazyStore:
 def __init__(self): self.nodes={name:{} for name in LEVELS}; self.generated={name:0 for name in LEVELS}; self.hits={name:0 for name in LEVELS}; self.eval_count={name:0 for name in LEVELS}; self.seconds={name:0. for name in LEVELS}
 def get(self,name,keys):
  delta=LEVELS[name]; unique=np.unique(np.asarray(keys,np.int64).reshape(-1,3),axis=0); cache=self.nodes[name]; missing=[tuple(x) for x in unique if tuple(x) not in cache]; self.hits[name]+=len(unique)-len(missing)
  if missing:
   start=time.monotonic(); k=np.asarray(missing,np.float64); pts=(k[:,None,:]+OFFSETS[None,:,:])*delta; vals=field(pts.reshape(-1,3)).reshape(-1,8).mean(axis=1); words=vals.astype(np.float16)
   for key,value in zip(missing,words): cache[key]=value
   self.seconds[name]+=time.monotonic()-start; self.generated[name]+=len(missing); self.eval_count[name]+=8*len(missing)
  return np.array([cache[tuple(x)] for x in np.asarray(keys,np.int64).reshape(-1,3)],np.float16).reshape(np.asarray(keys).shape[:-1])
 def sample_level(self,name,points,camera):
  delta=LEVELS[name]; p=np.asarray(points,np.float64)/delta; base=np.floor(p).astype(np.int64); frac=p-base; origin=window_origin(camera,delta); local=base-origin
  if np.any((local<0)|(local>126)): raise ValueError(f'{name} cache coverage')
  out=np.zeros(len(base),np.float64)
  for dz,dy,dx in product((0,1),repeat=3):
   off=np.array([dx,dy,dz]); w=np.prod(np.where(off,frac,1-frac),axis=1); out+=w*self.get(name,base+off)
  return out.astype(F)
 def sample(self,points,distance,camera,counts=None):
  s=np.asarray(distance,np.float64); lam=1-fog.smoothstep(20000.,30000.,s); out=np.zeros(len(s),F); reads=np.zeros(len(s),np.int16)
  fi=lam>0; fa=lam<1
  if fi.any(): out[fi]+=lam[fi]*self.sample_level('fine',np.asarray(points)[fi],camera); reads[fi]+=2
  if fa.any(): out[fa]+=(1-lam[fa])*self.sample_level('far',np.asarray(points)[fa],camera); reads[fa]+=2
  if counts is not None: counts.add(reads)
  return out
class ReadCounter:
 """Running density-read aggregate; per-sample counts are never retained."""
 __slots__=('total','peak','samples')
 def __init__(self): self.total=0; self.peak=0; self.samples=0
 def add(self,r): a=np.asarray(r,np.int64); self.total+=int(a.sum()); self.peak=max(self.peak,int(a.max(initial=0))); self.samples+=int(a.size)
 def row(self,rays): return {'density_read_total':self.total,'sampled_points':self.samples,'max_reads_per_sample':self.peak,'mean_reads_per_sample':(self.total/self.samples if self.samples else 0.),'mean_reads_per_ray':(self.total/rays if rays else 0.)}
def compose(a,b): return {'S':a['S']+a['T'][:,None]*b['S'],'T':a['T']*b['T']}
def bins(limit,mode,value):
 L=np.asarray(limit,np.float64); n=len(L)
 if mode=='candidate':
  count=np.where(L>NEAR,64,np.where(L>0,24,0)); idx=np.arange(64)[:,None]; near_end=np.minimum(L,NEAR); nd=near_end/24; fd=np.divide(np.maximum(L-NEAR,0),40,out=np.zeros_like(L),where=L>NEAR); lo=np.where(idx<24,idx*nd,(idx-24)*fd+NEAR); hi=np.where(idx<24,(idx+1)*nd,(idx-23)*fd+NEAR)
 else:
  count=np.ceil(L/value).astype(np.int32); idx=np.arange(int(count.max(initial=0)))[:,None]; d=np.divide(L,count,out=np.zeros_like(L),where=count>0); lo=idx*d; hi=(idx+1)*d
 active=idx<count; hi=np.minimum(hi,L); ds=np.where(active,np.maximum(hi-lo,0),0); return lo,hi,lo+.5*ds,ds,active,count
def candidate_reads(limits,chunk=512):
 """Density reads per ray of the frozen 24+40 candidate grid, from bin midpoint LOD weights."""
 L=np.asarray(limits,np.float64).ravel(); out=np.zeros(L.size,np.int64)
 for first in range(0,L.size,chunk):
  sl=slice(first,min(first+chunk,L.size)); _,_,d,_,a,_=bins(L[sl],'candidate',0); lam=1-fog.smoothstep(20000.,30000.,d); out[sl]=np.sum((2*(lam>0)+2*(lam<1))*a,axis=0)
 return out
def integrate(origin,direction,limit,chroma,evaluator,mode,value,store=None,chunk=64,representation=False):
 n=len(direction); result={k:{'S':np.zeros((n,3),F),'T':np.ones(n,F)} for k,_,_ in SEGMENTS}; result['full']={'S':np.zeros((n,3),F),'T':np.ones(n,F)}; reads=ReadCounter(); composition=[]
 false_tau=np.zeros(n,np.float64); density_delta=np.zeros(n,np.float64); density_samples=np.zeros(n,np.int64)
 for first in range(0,n,chunk):
  sl=slice(first,min(first+chunk,n)); d=direction[sl]; L=np.clip(np.asarray(limit[sl],np.float64),0,FAR); lo,hi,dist,ds,active,count=bins(L,mode,value); pts=np.asarray(origin,np.float64)+d[None,:,:]*dist[...,None]; rho=np.zeros(active.shape,F)
  if active.any(): rho[active]=evaluator(pts[active],dist[active],origin,reads)
  if representation and active.any():
   truth=np.zeros(active.shape,F); truth[active]=field(pts[active]); win=1-fog.smoothstep(WINDOW,FAR,dist); false_tau[sl]=SIGMA*np.sum(rho*win*ds*(truth==0),axis=0); density_delta[sl]=np.sum(np.abs(rho-truth)*ds,axis=0); density_samples[sl]=active.sum(axis=0)
  rgba=preview.make_rgba(rho,chroma)
  for name,a,b in SEGMENTS:
   overlap=np.maximum(np.minimum(hi,b)-np.maximum(lo,a),0); S,T,_=fog.integrate_samples(rgba,overlap.astype(F),dist.astype(F),SIGMA,d,True,SUN); result[name]['S'][sl]=S; result[name]['T'][sl]=T
  S,T,_=fog.integrate_samples(rgba,ds.astype(F),dist.astype(F),SIGMA,d,True,SUN); result['full']['S'][sl]=S; result['full']['T'][sl]=T
  c=compose(compose({k:result['near'][k][sl] for k in ('S','T')},{k:result['middle'][k][sl] for k in ('S','T')}),{k:result['shell'][k][sl] for k in ('S','T')}); composition.append(max(float(np.max(np.abs(c['T']-T))),float(np.max(np.abs(c['S']-S)))))
 result['_cost']={'density_reads':reads.row(n),'composition_max':max(composition,default=0.),'sample_counts':count.tolist(),'false_support_optical_depth':false_tau.tolist(),'absolute_density_length_error':density_delta.tolist(),'density_samples':{'total':int(density_samples.sum()),'max_per_ray':int(density_samples.max(initial=0)),'rays':int(n)}}; return result
def analytic_eval(points,distance,camera,reads): return field(points)
def filtered_eval(store): return lambda points,distance,camera,reads:store.sample(points,distance,camera,reads)
def error(a,b): return {'T':metric(np.abs(a['T']-b['T'])),'S_normalized_unit_radiance':[metric(np.abs(a['S'][:,i]-b['S'][:,i])) for i in range(3)]}
def conv_pass(e): return e['T']['p99']<=.00025 and e['T']['max']<=.00075
def quad_pass(e): return e['T']['p99']<=.001 and e['T']['max']<=.003 and all(x['p99']<=.0005 and x['max']<=.002 for x in e['S_normalized_unit_radiance'])
def score(origin,direction,limit,chroma,store):
 ar64=integrate(origin,direction,limit,chroma,analytic_eval,'dense',64.); ar128=integrate(origin,direction,limit,chroma,analytic_eval,'dense',128.); ev=filtered_eval(store); fr64=integrate(origin,direction,limit,chroma,ev,'dense',64.,representation=True); fr128=integrate(origin,direction,limit,chroma,ev,'dense',128.); cand=integrate(origin,direction,limit,chroma,ev,'candidate',0)
 rows={}; technical=True
 for seg in ('near','shell','full'):
  ac=error(ar128[seg],ar64[seg]); fc=error(fr128[seg],fr64[seg]); q=error(cand[seg],fr64[seg]); rep=error(fr64[seg],ar64[seg]); total=error(cand[seg],ar64[seg]); ok=conv_pass(ac) and conv_pass(fc) and quad_pass(q); technical &= ok
  rows[seg]={'analytic_128_vs64':ac,'analytic_converged':conv_pass(ac),'filtered_128_vs64':fc,'filtered_converged':conv_pass(fc),'filtered_candidate_vs_dense64':q,'quadrature_passed':quad_pass(q),'filtered_dense_vs_analytic_dense':rep,'candidate_vs_analytic_total':total}
 comp=max(x['_cost']['composition_max'] for x in (ar64,ar128,fr64,fr128,cand)); technical &= comp<=2e-6
 return {'segments':rows,'composition_max':comp,'representation_density':{'false_support_optical_depth':metric(fr64['_cost']['false_support_optical_depth']),'absolute_density_length_error':metric(fr64['_cost']['absolute_density_length_error'])},'measured_density_reads':{'candidate':cand['_cost']['density_reads'],'filtered_dense64':fr64['_cost']['density_reads'],'filtered_dense64_samples_per_ray':fr64['_cost']['density_samples']},'passed':technical},{'analytic':ar64,'filtered':fr64,'candidate':cand}
def subset_score(data,take):
 rows={}; passed=True
 for seg in ('near','shell','full'):
  a,f,c=(data[k][seg] for k in ('analytic','filtered','candidate')); sl=lambda r:{'S':r['S'][take],'T':r['T'][take]}; rep=error(sl(f),sl(a)); total=error(sl(c),sl(a)); q=error(sl(c),sl(f)); ok=quad_pass(q); passed &= ok; rows[seg]={'filtered_candidate_vs_dense64':q,'quadrature_passed':ok,'filtered_dense_vs_analytic_dense':rep,'candidate_vs_analytic_total':total}
 false=np.asarray(data['filtered']['_cost']['false_support_optical_depth'])[take]; density=np.asarray(data['filtered']['_cost']['absolute_density_length_error'])[take]
 return rows,passed,{'false_support_optical_depth':metric(false),'absolute_density_length_error':metric(density)}
def laws(store,chroma):
 vectors=np.array([[0,0,0],[-300000,200000,-100000],[65536,-65536,32768],[1,-2,3]],np.float64); R=screen.R; scalar=np.array([[sum(v[j]*R[i,j] for j in range(3)) for i in range(3)] for v in vectors]); coord=np.allclose(rotate(vectors,R),scalar,rtol=0,atol=1e-12)
 rho=np.array([0,.4,1],F); rgba=preview.make_rgba(rho,chroma); zero=LazyStore(); zero.get=lambda name,keys:np.zeros(np.asarray(keys).shape[:-1],np.float16); p=np.array([[0.,0.,0.]]); empty=zero.sample(p,np.array([0.]),np.zeros(3))
 local=np.array([[0,0,z] for z in (0,1,2,3,4,125,126)]); ax,ay,lane=pack_address(local); lanes=lane.tolist()==[0,1,2,3,0,1,2]
 # No source-alpha law: this screen produces per-ray (S,T) only and has no RGBA source composition or
 # clip step, so any alpha-preservation assertion here would compare a copy with itself.
 return {'component_rotation_matches_independent_scalar':bool(coord),'refined_field_matches_frozen':bool(np.array_equal(field(vectors),load('refcheck',HERE/'fog_mass_detail_refinement.py').refined_density(vectors))),'density_finite_bounded':bool(np.isfinite(field(vectors)).all() and np.all((field(vectors)>=0)&(field(vectors)<=1))),'premultiplied_identity':bool(np.array_equal(rgba[:,:3],rho[:,None]*chroma) and np.array_equal(rgba[:,3],rho)),'zero_cache_empty':bool(empty[0]==0),'rgba_lane_transition_and_edges':bool(lanes and np.all(ax>=0) and np.all(ay>=0))}
def write_sheet(output,name,rows):
 from PIL import Image,ImageDraw
 scale=8; ww,hh=32,18; labels=('analytic dense','filtered dense','filtered24+40','signed candidate-analytic'); segs=('near','full','shell'); sheet=Image.new('RGB',(4*ww*scale,6*hh*scale+20),'black'); draw=ImageDraw.Draw(sheet)
 for c,label in enumerate(labels): draw.text((c*ww*scale+2,2),label,fill='white')
 for si,seg in enumerate(segs):
  a,f,c=rows['analytic'][seg],rows['filtered'][seg],rows['candidate'][seg]
  vals=(a['S'],f['S'],c['S'],c['S']-a['S']); ops=(1-a['T'],1-f['T'],1-c['T'],(1-c['T'])-(1-a['T']))
  for col,v in enumerate(vals):
   z=(np.clip(v/.03,0,1) if col<3 else np.clip(.5+v/.004,0,1)); im=(z.reshape(hh,ww,3)**(1/2.2)*255+.5).astype(np.uint8); sheet.paste(Image.fromarray(im).resize((ww*scale,hh*scale),Image.Resampling.NEAREST),(col*ww*scale,20+(2*si)*hh*scale))
  for col,v in enumerate(ops):
   z=(np.clip(v/.4,0,1) if col<3 else np.clip(.5+v/.006,0,1)); im=np.repeat((z.reshape(hh,ww,1)*255+.5).astype(np.uint8),3,2); sheet.paste(Image.fromarray(im).resize((ww*scale,hh*scale),Image.Resampling.NEAREST),(col*ww*scale,20+(2*si+1)*hh*scale))
 path=output/name; sheet.save(path); return path
def generation_probe():
 rows={}
 for name,delta in LEVELS.items():
  for label,keys in [('brick',np.stack(np.meshgrid(np.arange(32),np.arange(32),np.arange(32),indexing='ij'),-1).reshape(-1,3)),('slab',np.stack(np.meshgrid(np.arange(32),np.arange(32),np.array([32]),indexing='ij'),-1).reshape(-1,3))]:
   s=LazyStore(); start=time.monotonic(); s.get(name,keys); elapsed=time.monotonic()-start; rows[f'{name}_{label}']={'nodes':len(keys),'payload_bytes':2*len(keys),'analytic_density_evaluations':8*len(keys),'host_seconds':elapsed}
 return rows
def validate(r,output):
 if r['schema']!=1 or set(r['poses'])!={'A','B'} or len(r['images'])!=4: raise ValueError('schema')
 from PIL import Image
 for n,h in r['images'].items():
  if digest(output/n)!=h: raise ValueError('image hash')
  with Image.open(output/n) as im:
   if im.size!=(1024,884): raise ValueError('image dimensions')
 json.dumps(r,allow_nan=False,sort_keys=True)
def run(asset_data,output,plan):
 start=time.monotonic(); plan=Path(plan)
 if not plan.is_file(): raise ValueError(f'plan file not found: {plan}')
 plan_sha=digest(plan)
 if plan_sha!=EXPECTED_PLAN: raise ValueError(f'plan digest mismatch: {plan} is {plan_sha}, expected {EXPECTED_PLAN}')
 if output.exists(): raise ValueError(f'output directory already exists, refusing to mix stale files: {output}')
 output.mkdir(parents=True)
 if digest(HERE/'fog_mass_detail_refinement.py')!=EXPECTED_REFINEMENT: raise ValueError('refinement mismatch')
 manifest=json.loads((asset_data/'manifest.json').read_text()); profile=next(x for x in manifest['profiles'] if x['name']=='foggreenoutlands'); base=float(profile['base_sigma'])
 if not math.isfinite(base) or abs(base*1.5-SIGMA)>1e-15: raise ValueError('sigma')
 volume=fog.decode_packet(asset_data/'foggreenoutlands.fogbin',manifest); chroma=(volume[...,:3].sum((0,1,2),dtype=np.float64)/volume[...,3].sum(dtype=np.float64)).astype(F); store=LazyStore(); poses={}; images=[]; all_pass=True; pops=populations()
 for pose in screen.pose_rows():
  allr=rays(pose); combined=np.unique(np.concatenate([y*W+x for x,y in pops.values()])); directions=allr[combined]; scored,data=score(pose['origin'],directions,np.full(len(directions),FAR,F),chroma,store); all_pass &= scored['passed']; prows={}
  for pop,(x,y) in pops.items():
   ids=y*W+x; take=np.searchsorted(combined,ids); subset={k:{seg:{q:v[seg][q][take] for q in ('S','T')} for seg in ('near','full','shell')} for k,v in data.items()}; popscore,popok,repdensity=subset_score(data,take); all_pass &= popok; image=write_sheet(output,f'green-pose{pose["name"]}-{pop}-density-runtime.png',subset); images.append(image); prows[pop]={'ray_count':len(ids),'score':popscore,'reference_convergence_from_deduplicated_pose_population':{seg:{k:v for k,v in scored['segments'][seg].items() if k in ('analytic_128_vs64','analytic_converged','filtered_128_vs64','filtered_converged')} for seg in ('near','shell','full')},'representation_density':repdensity,'opacity':{kind:{seg:metric(1-subset[kind][seg]['T']) for seg in ('near','full','shell')} for kind in subset},'clear_fraction_opacity_below_0_002':{kind:{seg:float(np.mean((1-subset[kind][seg]['T'])<.002)) for seg in ('near','full','shell')} for kind in subset},'internal_detail_contrast':{kind:float(np.std(1-subset[kind]['full']['T'])) for kind in subset},'image':image.name}
  wi=np.array([j*32+i for i,j in WITNESS]); sx,sy=pops['stratified']; wd=allr[sy[wi]*W+sx[wi]]; vl=np.linalg.norm(np.stack((((2*(sx[wi]+.5)/W-1)*(W/H)*math.tan(math.radians(30))),((1-2*(sy[wi]+.5)/H)*math.tan(math.radians(30))),np.ones(5)),1),axis=1); requested=np.tile(DEPTHS,5); depth_b=requested/np.repeat(vl,7); limits=np.minimum(depth_b*np.repeat(vl,7),FAR); geometry,_=score(pose['origin'],np.repeat(wd,7,axis=0),limits.astype(F),chroma,store); all_pass &= geometry['passed']
  invalid=[]; invalid_ok=True
  for value in (0.,float('nan'),float('inf')):
   raw=np.full(5,value); invalid_limit=np.where((~np.isfinite(raw))|(raw<=0),0,raw*vl); inv=integrate(pose['origin'],wd,invalid_limit,chroma,filtered_eval(store),'candidate',0); identity=bool(np.array_equal(inv['full']['S'],np.zeros((5,3),F)) and np.array_equal(inv['full']['T'],np.ones(5,F))); invalid_ok &= identity; invalid.append({'depth_b':'NaN' if np.isnan(value) else 'Infinity' if np.isinf(value) else value,'identity':identity})
  all_pass &= invalid_ok; poses[pose['name']]={'populations':prows,'measured_density_reads':scored['measured_density_reads'],'geometry_measured_density_reads':geometry['measured_density_reads'],'geometry':geometry,'invalid_depth_cases':invalid,'depth_reconstruction_max':float(np.max(np.abs(limits-np.minimum(requested,FAR))))}
 # Temporal fixed rays and Q LOD witnesses.
 temporal=[]; temporal_ok=True; representation=[]
 for pose in screen.pose_rows():
  allr=rays(pose); sx,sy=pops['stratified']; wi=np.array([j*32+i for i,j in WITNESS]); d=allr[sy[wi]*W+sx[wi]]; states=[]; Q=np.asarray(pose['origin'])+25000*np.asarray(pose['forward'])
  for shift in (-5500.,-5000.,-4500.,0.,4500.,5000.,5500.):
   origin=np.asarray(pose['origin'])+shift*np.asarray(pose['forward']); sc,data=score(origin,d,np.full(5,FAR,F),chroma,store); states.append((shift,data)); dist=float(np.linalg.norm(Q-origin)); fine=store.sample_level('fine',Q[None,:],origin)[0] if dist<30000 else None; far=store.sample_level('far',Q[None,:],origin)[0] if dist>20000 else None; lam=float(1-fog.smoothstep(20000,30000,np.array([dist]))[0]); blend=(lam*(fine or 0)+(1-lam)*(far or 0)); representation.append({'pose':pose['name'],'shift':shift,'Q_distance':dist,'fine':None if fine is None else float(fine),'far':None if far is None else float(far),'lambda':lam,'blended':float(blend),'analytic':float(field(Q[None,:])[0])})
  for a,b in zip(states,states[1:]):
   for ray in range(5):
    for seg in ('near','shell','full'):
     q=abs((b[1]['candidate'][seg]['T'][ray]-a[1]['candidate'][seg]['T'][ray])-(b[1]['filtered'][seg]['T'][ray]-a[1]['filtered'][seg]['T'][ray])); rep=abs((b[1]['filtered'][seg]['T'][ray]-a[1]['filtered'][seg]['T'][ray])-(b[1]['analytic'][seg]['T'][ray]-a[1]['analytic'][seg]['T'][ray])); temporal.append({'pose':pose['name'],'shifts':[a[0],b[0]],'ray':ray,'segment':seg,'quadrature_residual':float(q),'representation_residual':float(rep)}); temporal_ok &= q<=.003
 all_pass &= temporal_ok
 # Cache shift/window laws at fixed signed points.
 shift_rows=[]; cache_ok=True
 for name,delta in LEVELS.items():
  cams=[np.array([0.,0.,0.]),np.array([-10000.,20000.,-30000.])]
  for cam in cams:
   pts=cam+np.array([[0,0,0],[100,-200,300],[delta*.49,delta*.2,-delta*.3]])
   base=store.sample_level(name,pts,cam); base_keys=np.floor(pts/delta).astype(np.int64); words0=store.get(name,base_keys).view(np.uint16)
   for axis in range(3):
    shifted=cam+np.eye(3)[axis]*delta; pending=LazyStore(); pending.nodes[name].update(store.nodes[name]); held=store.sample_level(name,pts,cam); got=pending.sample_level(name,pts,shifted); words1=pending.get(name,base_keys).view(np.uint16); words_match=bool(np.array_equal(words0,words1)); ok=bool(np.allclose(base,got,rtol=0,atol=2e-7) and np.array_equal(held,base) and words_match); cache_ok &= ok; shift_rows.append({'level':name,'camera':cam.tolist(),'axis':axis,'shared_FP16_words_match':words_match,'old_origin_data_held_while_pending':bool(np.array_equal(held,base)),'match_after_complete_switch':ok,'max':float(np.max(np.abs(base-got)))})
 laws_row=laws(store,chroma); all_pass &= cache_ok and all(laws_row.values()); generation=generation_probe(); elapsed=time.monotonic()-start; lazy={'generated_nodes':store.generated,'cache_hits':store.hits,'analytic_density_evaluations':store.eval_count,'host_generation_seconds':store.seconds}
 # Representative read counts plus a dense scan for the worst case, from bin midpoints only.
 reads=[{'limit':float(L),'reads':int(r)} for L,r in zip((1000.,11999.,12001.,150001.,200000.),candidate_reads((1000.,11999.,12001.,150001.,200000.)))]
 scan_limits=np.arange(0.,FAR+1.,100.); scan=candidate_reads(scan_limits); worst=int(np.argmax(scan))
 reads_scan={'step':100.,'limit_min':0.,'limit_max':float(FAR),'sampled_limits':int(scan.size),'max_reads_per_ray':int(scan[worst]),'argmax_limit':float(scan_limits[worst]),'sky_ray_reads':int(candidate_reads((FAR,))[0]),'mean_reads_per_ray':float(scan.mean())}
 status='passed-stored-density-runtime-screen' if all_pass else 'failed-stored-density-runtime-screen'; result={'schema':1,'result':status,'decision':('eligible for parent appearance/cost review only' if all_pass else 'close fixed candidate; no tuning or prototype'),'sources':{'analysis_sha256':digest(__file__),'plan_sha256':plan_sha,'refinement_sha256':digest(HERE/'fog_mass_detail_refinement.py'),'manifest_sha256':digest(asset_data/'manifest.json'),'packet_sha256':digest(asset_data/'foggreenoutlands.fogbin')},'contract':{'levels':LEVELS,'nodes_per_level':[128,128,128],'prefilter':'fixed eight offsets +/-delta/4, average then FP16','LOD':'1-smoothstep(20000,30000,s)','candidate':'24 near +40 far, max64','resident_GPU_payload_bytes':8*1024*1024,'two_GPU_plus_staging_payload_bytes':24*1024*1024,'world_anchor':True},'gates':{'reference_and_quadrature':all(p['geometry']['passed'] and all(v['score']['full']['quadrature_passed'] for v in p['populations'].values()) for p in poses.values()),'temporal_quadrature':bool(temporal_ok),'cache_shift_identity':bool(cache_ok),'laws':laws_row,'all_passed':bool(all_pass)},'poses':poses,'temporal':{'rows':temporal,'quadrature_max':max(x['quadrature_residual'] for x in temporal),'representation_max':max(x['representation_residual'] for x in temporal)},'Q_LOD_witnesses':representation,'cache_shift_witnesses':shift_rows,'cost':{'candidate_reads_by_limit':reads,'candidate_reads_dense_scan':reads_scan,'measured_density_reads_by_pose':{name:{'pose_population':p['measured_density_reads'],'geometry_rays':p['geometry_measured_density_reads']} for name,p in poses.items()},'lazy':lazy,'generation_probe':generation,'full_bake_estimate':{'nodes':2*128**3,'analytic_density_evaluations':2*128**3*8,'scalar_payload_bytes':2*128**3*2},'one_node_slab_per_level':{'nodes':128**2,'scalar_payload_bytes':128**2*2,'analytic_density_evaluations':128**2*8},'host_platform':f'{platform.platform()} Python{sys.version.split()[0]} NumPy{np.__version__}','not_GPU_or_native_Windows_timing':True},'images':{p.name:digest(p) for p in images},'limitations':['Representation differences are descriptive appearance evidence, not relaxed technical gates or bit equivalence.','Sparse saved-ray subsets and contact sheets are not flight/TAA/pixel-area acceptance.','Host lazy generation and extrapolated full/slab counts do not establish x86, native-Windows, loading, GPU, repair, shadow or FPS cost.','No full cache, manager, upload, shader, bake file or production edit was made.'],'host_seconds':elapsed}
 validate(result,output); (output/'report.json').write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n'); report_sha=digest(output/'report.json'); failed=[]
 for pose,p in poses.items():
  if not p['geometry']['passed']: failed.append(f'{pose}/geometry')
  for pop,v in p['populations'].items():
   for seg,row in v['score'].items():
    conv=v['reference_convergence_from_deduplicated_pose_population'][seg]
    if not(conv['analytic_converged'] and conv['filtered_converged'] and row['quadrature_passed']): failed.append(f'{pose}/{pop}/{seg}')
 if not temporal_ok: failed.append('temporal');
 (output/'report.md').write_text(f'''# Stored final-density runtime screen\n\nResult: **{status}**; {result['decision']}. Failed technical groups: {', '.join(failed) if failed else 'none'}. Temporal quadrature max {result['temporal']['quadrature_max']:.9g}; representation movement max {result['temporal']['representation_max']:.9g}.\n\nThe packet separates analytic/filtered reference convergence, filtered24+40 quadrature and filtered-vs-analytic representation difference. Four fixed-scale sheets cover A/B stratified and central-crop rays. Appearance remains parent/user judgment. Lazy nodes: fine {lazy['generated_nodes']['fine']}, far {lazy['generated_nodes']['far']}; host runtime {elapsed:.2f}s.\n\nNo full cache, cache manager, shader, GPU, Wine, game, build, production edit, tuning, install or commit occurred.\n'''); (output/'summary.json').write_text(json.dumps({'result':status,'decision':result['decision'],'analysis_sha256':result['sources']['analysis_sha256'],'plan_sha256':result['sources']['plan_sha256'],'report_sha256':report_sha,'images':len(images),'host_seconds':elapsed},indent=2,sort_keys=True)+'\n'); return result
def main():
 p=argparse.ArgumentParser(); p.add_argument('--asset-data',type=Path,required=True); p.add_argument('--output',type=Path,required=True); p.add_argument('--plan',type=Path,default=DEFAULT_PLAN); a=p.parse_args(); r=run(a.asset_data,a.output,a.plan); print(json.dumps({'result':r['result'],'decision':r['decision'],'seconds':r['host_seconds']}))
if __name__=='__main__': main()
