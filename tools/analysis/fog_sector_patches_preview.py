#!/usr/bin/env python3
"""Fixed reference-only preview of sector-wide multiscale fog patches."""
from __future__ import annotations
import argparse, hashlib, importlib.util, json, math, time
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); return m
fog=load('fog_distance_replay',HERE/'fog_distance_replay.py')
connected=load('fog_connected_preview',HERE/'fog_connected_preview.py')
F=np.float32; P=fog.PERIOD; WIDTH,HEIGHT=128,72; SUN=np.array([1.,0.,0.],F); MASK=0xffffffff
R=np.array([[1,2,2],[2,1,-2],[-2,2,-1]],np.float64)/3
SCALES=(8*P,4*P,2*P); WEIGHTS=(4.,2.,1.)

def digest(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as f:
  for block in iter(lambda:f.read(1<<20),b''): h.update(block)
 return h.hexdigest()

def mix_array(v):
 v=np.asarray(v,np.uint64)&MASK; v^=v>>16; v=(v*0x7feb352d)&MASK
 v^=v>>15; v=(v*0x846ca68b)&MASK; return (v^(v>>16))&MASK

def value_noise(points,octave):
 u=np.asarray(points,np.float64); shape=u.shape[:-1]; flat=u.reshape(-1,3)
 cell=np.floor(flat).astype(np.int64); f=flat-cell; q=f*f*f*(f*(f*6-15)+10)
 seed=int(mix_array(np.array([((octave+1)*0x9e3779b9)&MASK]))[0]); out=np.zeros(len(flat),np.float64)
 for dz in (0,1):
  wz=q[:,2] if dz else 1-q[:,2]
  for dy in (0,1):
   wy=q[:,1] if dy else 1-q[:,1]
   for dx in (0,1):
    wx=q[:,0] if dx else 1-q[:,0]
    c=cell+np.array([dx,dy,dz],np.int64)
    h=mix_array(((c[:,0].astype(np.uint64)&MASK)*0x9e3779b9)^((c[:,1].astype(np.uint64)&MASK)*0x85ebca6b)^((c[:,2].astype(np.uint64)&MASK)*0xc2b2ae35)^0x58434647^seed)
    value=2*((h>>8).astype(np.float64)+.5)/16777216-1
    out += wx*wy*wz*value
 return out.reshape(shape).astype(F)

def macro(points):
 x=np.asarray(points,np.float64); r2=R@R
 b=(4*value_noise(x/SCALES[0],0)+2*value_noise((x@R.T)/SCALES[1],1)+value_noise((x@r2.T)/SCALES[2],2))/7
 return b.astype(F),fog.smoothstep(-.2,.2,b).astype(F)

def integrate_range(volume,origin,direction,limit,sigma,spacing,start,end,apply_window,chunk=48):
 n=len(direction); S=np.zeros((n,3),F); T=np.ones(n,F)
 for first in range(0,n,chunk):
  sl=slice(first,min(first+chunk,n)); d=direction[sl]; stop=np.minimum(np.clip(limit[sl],0,fog.FAR),end); lengths=np.maximum(stop-start,0); counts=np.ceil(lengths/spacing).astype(np.int32); count=int(counts.max(initial=0))
  if not count: continue
  index=np.arange(count)[:,None]; lo=start+index*spacing; ds=np.clip(stop[None,:]-lo,0,spacing).astype(F); distance=(lo+ds*.5).astype(F); active=ds>0
  points=np.asarray(origin,np.float64)+d[None,:,:]*distance[...,None]; _,M=macro(points); M[~active]=0
  rgba=np.zeros(points.shape[:-1]+(4,),F); supported=active&(M>0)
  if supported.any(): rgba[supported]=fog.sample_level(volume,points[supported])*M[supported,None]
  result=fog.integrate_samples(rgba,ds,distance,sigma,d,apply_window,SUN); S[sl],T[sl]=result[:2]
 return {'S':S,'T':T,'tau':-np.log(np.maximum(T,np.finfo(F).tiny))}


def integrate(volume,origin,direction,limit,sigma,spacing,collect=False,components=True,chunk=48):
 n=len(direction); S=np.zeros((n,3),F); T=np.ones(n,F)
 cost={k:np.zeros(n,np.float64) for k in ('active_samples','macro_zero_samples','macro_transition_samples','macro_one_samples','macro_support_length','fine_reads')}
 for first in range(0,n,chunk):
  sl=slice(first,min(first+chunk,n)); d=direction[sl]; lim=np.clip(limit[sl],0,fog.FAR); counts=np.ceil(lim/spacing).astype(np.int32); count=int(counts.max(initial=0))
  if not count: continue
  index=np.arange(count)[:,None]; lo=index*spacing; ds=np.clip(lim[None,:]-lo,0,spacing).astype(F); distance=(lo+ds*.5).astype(F); active=ds>0
  points=np.asarray(origin,np.float64)+d[None,:,:]*distance[...,None]; b,M=macro(points); M[~active]=0
  rgba=np.zeros(points.shape[:-1]+(4,),F); supported=active&(M>0)
  if supported.any(): rgba[supported]=fog.sample_level(volume,points[supported])*M[supported,None]
  full=fog.integrate_samples(rgba,ds,distance,sigma,d,True,SUN); S[sl],T[sl]=full[:2]
  cost['active_samples'][sl]=active.sum(axis=0); cost['macro_zero_samples'][sl]=(active&(M==0)).sum(axis=0)
  cost['macro_transition_samples'][sl]=(active&(M>0)&(M<1)).sum(axis=0); cost['macro_one_samples'][sl]=(active&(M==1)).sum(axis=0)
  cost['macro_support_length'][sl]=np.sum(ds*(M>0),axis=0); cost['fine_reads'][sl]=2*supported.sum(axis=0)
 result={'S':S,'T':T,'tau':-np.log(np.maximum(T,np.finfo(F).tiny))}
 if components:
  result['near']=integrate_range(volume,origin,direction,limit,sigma,spacing,0.,fog.NEAR,False,chunk)
  result['shell']=integrate_range(volume,origin,direction,limit,sigma,spacing,fog.WINDOW_START,fog.FAR,True,chunk)
 if collect: result['cost']=cost
 return result

def metric(values):
 a=np.asarray(values,np.float64).ravel(); return {'count':int(a.size),'mean':float(a.mean()) if a.size else 0.,'p50':float(np.percentile(a,50)) if a.size else 0.,'p90':float(np.percentile(a,90)) if a.size else 0.,'p99':float(np.percentile(a,99)) if a.size else 0.,'max':float(a.max(initial=0))}

def errors(a,b): return {'T':metric(np.abs(a['T']-b['T'])),'S_normalized_unit_radiance':[metric(np.abs(a['S'][:,c]-b['S'][:,c])) for c in range(3)]}
def converged(e): return e['T']['p99']<=.00025 and e['T']['max']<=.00075
def angular_variation(values):
 image=np.asarray(values,np.float64).reshape(HEIGHT,WIDTH); delta=np.concatenate((np.abs(np.diff(image,axis=0)).ravel(),np.abs(np.diff(image,axis=1)).ravel()))
 return {'standard_deviation':float(image.std()),'four_neighbor_absolute_difference':metric(delta)}

def poses():
 rows=connected.poses(); names=('A','B','C')
 return [dict(row,name=name,prior_name=row['name']) for row,name in zip(rows,names)]

def rgb(values,maximum,signed=False):
 image=values.reshape(HEIGHT,WIDTH,3); image=.5+image/(2*maximum) if signed else image/maximum
 return (np.clip(image,0,1)**(1/2.2)*255+.5).astype(np.uint8)
def scalar(values,maximum,signed=False):
 image=values.reshape(HEIGHT,WIDTH); image=.5+image/(2*maximum) if signed else image/maximum
 image=(np.clip(image,0,1)*255+.5).astype(np.uint8); return np.repeat(image[...,None],3,axis=-1)

def write_view(output,prior_path,stem,near,full,shell,ref128):
 from PIL import Image,ImageDraw
 scale=5; w,h=WIDTH,HEIGHT; top,left=18,82
 with Image.open(prior_path) as prior:
  if prior.size!=(2642,1098): raise ValueError(f'unexpected cached prior sheet dimensions: {prior.size}')
  prior_panel=prior.crop((left+w*scale,top,left+2*w*scale,top+3*h*scale)).copy()
 groups=[prior_panel]
 for result in (near,full,shell): groups.append([rgb(result['S'],.03),scalar(1-result['T'],.4),scalar(result['T'],1.)])
 sheet=Image.new('RGB',(left+4*w*scale,top+3*h*scale),'black'); draw=ImageDraw.Draw(sheet)
 labels=('prior paired-lobe cached','new 0-2.4km','new 0-40km','new 30-40km shell')
 for col,label in enumerate(labels):
  draw.text((left+col*w*scale+2,3),label,fill='white')
  if col==0: sheet.paste(groups[col],(left+col*w*scale,top))
  else:
   for row,panel in enumerate(groups[col]): sheet.paste(Image.fromarray(panel).resize((w*scale,h*scale),Image.Resampling.NEAREST),(left+col*w*scale,top+row*h*scale))
 for row,label in enumerate(('S / .03','opacity / .4','T / 1')): draw.text((2,top+row*h*scale+3),label,fill='white')
 path=output/f'{stem}-cloud-only.png'; sheet.save(path); return path.name

def write_slice(output,family,volume):
 from PIL import Image,ImageDraw
 axis=-8*P+(np.arange(128)+.5)*(16*P/128); yy,xx=np.meshgrid(axis,axis,indexing='ij'); points=np.stack((xx,yy,np.zeros_like(xx)),axis=-1)
 b,M=macro(points); density=fog.sample_level(volume,points)[...,3]*M
 def gray(v,lo,hi): return np.repeat((np.clip((v-lo)/(hi-lo),0,1)*255+.5).astype(np.uint8)[...,None],3,axis=-1)
 panels=(gray(b,-1,1),gray(M,0,1),gray(density,0,1)); scale=4; top=18
 sheet=Image.new('RGB',(3*128*scale,top+128*scale),'black'); draw=ImageDraw.Draw(sheet)
 for i,(label,panel) in enumerate(zip(('b / [-1,1]','M / [0,1]','M*rho0 / [0,1]'),panels)):
  draw.text((i*128*scale+2,3),label,fill='white'); sheet.paste(Image.fromarray(panel).resize((128*scale,128*scale),Image.Resampling.NEAREST),(i*128*scale,top))
 path=output/f'{family}-sectorXY-support-detail.png'; sheet.save(path)
 return path.name,{'bounds_render_units':[-8*P,8*P],'grid':[128,128],'b':metric(b),'M':metric(M),'modulated_density':metric(density),'M_zero_fraction':float(np.mean(M==0)),'M_transition_fraction':float(np.mean((M>0)&(M<1))),'M_one_fraction':float(np.mean(M==1))}

def laws():
 pts=np.array([[0.,0.,0.],[1234.,-5678.,9012.]],np.float64); b,M=macro(pts)
 volume=np.empty((8,8,8,4),F); volume[...,3]=.5; volume[...,:3]=.5*np.array([.2,.5,.8],F)
 direction=np.array([[1.,0,0],[-1.,0,0]],F); zero=integrate(volume,pts[0],direction,np.array([0.,-1.],F),4e-6,128.)
 source=np.array([[.2,.3,.4,.17],[.7,.6,.5,.83]],F); composite=source.copy(); composite[:,:3]=zero['S']+zero['T'][:,None]*source[:,:3]
 whole=integrate_range(volume,pts[0],direction[:1],np.array([4096.],F),4e-6,128.,0.,4096.,False)
 first=integrate_range(volume,pts[0],direction[:1],np.array([4096.],F),4e-6,128.,0.,2048.,False)
 second=integrate_range(volume,pts[0],direction[:1],np.array([4096.],F),4e-6,128.,2048.,4096.,False)
 composed_S=first['S']+first['T'][:,None]*second['S']; composed_T=first['T']*second['T']
 return {'rotation_orthonormal':bool(np.allclose(R.T@R,np.eye(3),rtol=0,atol=1e-15)),'rotation_proper':bool(abs(np.linalg.det(R)-1)<1e-15),'macro_finite_bounded':bool(np.isfinite(b).all() and np.all((M>=0)&(M<=1))),'zero_negative_depth_identity_exact':bool(np.array_equal(zero['S'],np.zeros_like(zero['S'])) and np.array_equal(zero['T'],np.ones_like(zero['T']))),'source_alpha_preserved_exact':bool(np.array_equal(composite[:,3],source[:,3])),'aligned_split_composition_parity':bool(float(whole['T'][0])<1 and np.allclose(composed_S,whole['S'],rtol=0,atol=2e-7) and np.allclose(composed_T,whole['T'],rtol=0,atol=2e-7))}

def endpoint_inventory(meta,depth_path):
 origin,direction,limit,geometry=fog.ray_set(meta,depth_path,64,36); camera_b,camera_M=macro(origin[None,:]); distances=(np.arange(32)+.5)*(fog.FAR/32); pts=origin+direction[:,None,:]*distances[None,:,None]; _,M=macro(pts)
 active=distances[None,:]<limit[:,None]
 return {'origin':origin.tolist(),'rays':len(direction),'sky_rays':int((~geometry).sum()),'geometry_rays':int(geometry.sum()),'camera_b':float(camera_b[0]),'camera_M':float(camera_M[0]),'sampled_64x36_rays_with_positive_macro_support':int(np.any((M>0)&active,axis=1).sum()),'sampled_64x36_rays_with_exact_clear_macro_sample':int(np.any((M==0)&active,axis=1).sum()),'coverage_samples_per_ray':32,'scope':'macro camera/coverage inventory only; no fine-density integration'}


def validate_result(result,output):
 if result.get('schema')!=1: raise ValueError('unexpected result schema')
 if sum(len(row.get('views',[])) for row in result.get('families',{}).values())!=6: raise ValueError('expected six views')
 if len(result.get('images',{}))!=8: raise ValueError('expected eight generated images')
 for name,want in result['images'].items():
  path=output/name
  if not path.is_file() or digest(path)!=want: raise ValueError(f'image hash mismatch: {name}')
 json.dumps(result,sort_keys=True,allow_nan=False)

def run(capture,asset_data,prior_output,output,design):
 started=time.monotonic(); output.mkdir(parents=True,exist_ok=True); manifest_path=asset_data/'manifest.json'; manifest=json.loads(manifest_path.read_text()); profiles={x['name']:x for x in manifest['profiles']}
 prior_report_path=prior_output/'report.json'; prior_report=json.loads(prior_report_path.read_text())
 if not isinstance(prior_report.get('images'),dict): raise ValueError('cached prior report has no image manifest')
 logs=sorted(capture.glob('session-*.log'))
 if len(logs)!=1: raise ValueError('expected one capture log')
 metadata,metadata_sha=fog.load_metadata(logs[0]); endpoints=[capture/f'depth_1_{frame}.rgba32f' for lo,hi,_ in fog.BURSTS for frame in (lo,hi)]
 sources={'analysis_sha256':digest(Path(__file__)),'test_contract':'focused separately','design_sha256':digest(design),'distance_replay_sha256':digest(HERE/'fog_distance_replay.py'),'connected_preview_sha256':digest(HERE/'fog_connected_preview.py'),'manifest_sha256':digest(manifest_path),'selected_capture_metadata_sha256':metadata_sha,'endpoint_depth_sha256':{p.name:digest(p) for p in endpoints},'prior_report_sha256':digest(prior_report_path),'prior_images':{},'packets':{}}
 laws_row=laws(); all_ok=all(laws_row.values()); families={}; images=[]
 for lo,hi,family in fog.BURSTS:
  packet=asset_data/f'{family}.fogbin'; volume=fog.decode_packet(packet,manifest); sigma=float(profiles[family]['base_sigma'])*1.5; sources['packets'][family]={'sha256':digest(packet),'decoded_sha256':profiles[family]['decoded_sha256']}; views=[]
  for pose in poses():
   origin=pose['origin']; direction=connected.camera_rays(origin,pose['forward'],pose['up']); limit=np.full(len(direction),fog.FAR,F)
   ref128=integrate(volume,origin,direction,limit,sigma,128.,collect=True,components=False); ref64=integrate(volume,origin,direction,limit,sigma,64.)
   e=errors(ref128,ref64); ok=converged(e); all_ok &= ok
   prior=prior_output/f"{family}-{pose['prior_name']}-cloud-only.png"
   if not prior.is_file(): raise FileNotFoundError(prior)
   prior_sha=digest(prior)
   if prior_report['images'].get(prior.name)!=prior_sha: raise ValueError(f'cached prior image not bound by prior report: {prior.name}')
   sources['prior_images'][prior.name]=prior_sha
   image=write_view(output,prior,f'{family}-pose{pose["name"]}',ref64['near'],ref64,ref64['shell'],ref128); images.append(image)
   active=np.maximum(ref128['cost']['active_samples'],1); cost=ref128['cost']
   opacity=1-ref64['T']
   views.append({'pose':pose['name'],'saved_camera_origin':origin.tolist(),'saved_camera_forward':pose['forward'].tolist(),'saved_camera_up':pose['up'].tolist(),'reference_128_vs_64':e,'converged':ok,'complete_optical_depth':metric(ref64['tau']),'complete_opacity':metric(opacity),'complete_opacity_angular_variation':angular_variation(opacity),'complete_clear_below_0.002':float(np.mean(opacity<.002)),'near_opacity':metric(1-ref64['near']['T']),'shell_opacity':metric(1-ref64['shell']['T']),'cost':{'active_samples':metric(cost['active_samples']),'macro_zero_fraction':metric(cost['macro_zero_samples']/active),'macro_transition_fraction':metric(cost['macro_transition_samples']/active),'macro_one_fraction':metric(cost['macro_one_samples']/active),'macro_support_column_length':metric(cost['macro_support_length']),'fine_atlas_reads':metric(cost['fine_reads']),'macro_corner_hashes':metric(cost['active_samples']*24),'lighting_shadows_repair_GPU_excluded':True},'image':image})
  slice_image,slice_row=write_slice(output,family,volume); images.append(slice_image)
  inv=[{'frame':frame,**endpoint_inventory(metadata[frame],capture/f'depth_1_{frame}.rgba32f')} for frame in (lo,hi)]
  families[family]={'views':views,'slice':{'image':slice_image,**slice_row},'captured_endpoint_coverage':inv}
 status='passed-sector-patches-reference-preview' if all_ok else 'inconclusive-sector-patches-reference-preview'; image_paths=[output/name for name in sorted(images)]
 result={'schema':1,'result':status,'appearance_selection':'pending parent/user review','sources':sources,'contract':{'macro':'signed quintic value noise at 8P/4P/2P, weights4:2:1, fixed proper rotation; M=smoothstep(-.2,.2,b)','family_field':'original world scale sampled once where M>0','reference_spacings':[128,64],'poses':['A','B','C'],'views':6,'lighting':'synthetic +X sun, normalized unit radiance, g=.3','outer_window':[150000,200000]},'gates':{'reference_converged':all(v['converged'] for f in families.values() for v in f['views']),'laws_passed':all(laws_row.values()),'laws':laws_row},'families':families,'images':{p.name:digest(p) for p in image_paths},'limitations':['Numerical convergence does not select appearance or a production integrator.','Prior paired-lobe panels are cached references, not recomputed.','Cloud-only unit-radiance sheets are not game composites or production-scaled lighting.','Analytic CPU counts exclude lighting, shadows, repair, tile/state traffic and GPU timing.','No parameter, seed, threshold, scale, strength or camera search was performed.'],'host_seconds':time.monotonic()-started}
 validate_result(result,output)
 (output/'report.json').write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n'); report_sha=digest(output/'report.json')
 views=[v for f in families.values() for v in f['views']]; worstp=max(v['reference_128_vs_64']['T']['p99'] for v in views); worst=max(v['reference_128_vs_64']['T']['max'] for v in views); reads=max(v['cost']['fine_atlas_reads']['max'] for v in views)
 sentence=('All six T convergence gates pass' if result['gates']['reference_converged'] else 'At least one T convergence gate fails')
 (output/'report.md').write_text(f'''# Fixed sector-wide irregular-patches reference preview\n\nResult: **{status}**. {sentence}; worst128/64 T p99/max is {worstp:.9g}/{worst:.9g} against .00025/.00075. Appearance remains pending.\n\nSix fixed A/B/C sheets compare cached paired-lobe reference, new0-2.4km, new0-40km and new30-40km shell at fixed scales. Two fixed sectorXY sheets show b, M and M*rho0. Maximum analytic fine-atlas reads per ray at128 spacing is {reads:.0f}; lighting, shadows, repair, state traffic and GPU timing are excluded.\n\nThis numerical reference does not approve aesthetics or a runtime architecture. Host runtime {result['host_seconds']:.2f}s. No tuning, game, Wine, build, production change, install or commit occurred.\n''')
 (output/'summary.json').write_text(json.dumps({'result':status,'analysis_sha256':sources['analysis_sha256'],'report_sha256':report_sha,'images':len(result['images']),'host_seconds':result['host_seconds']},indent=2,sort_keys=True)+'\n')
 return result

def main():
 p=argparse.ArgumentParser(); p.add_argument('--capture',type=Path,required=True); p.add_argument('--asset-data',type=Path,required=True); p.add_argument('--prior-output',type=Path,default=Path('/tmp/x3-fog-connected-preview')); p.add_argument('--output',type=Path,required=True); p.add_argument('--design',type=Path,default=Path('/tmp/x3-fog-sector-patches-design.md')); a=p.parse_args(); r=run(a.capture,a.asset_data,a.prior_output,a.output,a.design); print(json.dumps({'result':r['result'],'seconds':r['host_seconds'],'output':str(a.output)},sort_keys=True))
if __name__=='__main__': raise SystemExit(main())
