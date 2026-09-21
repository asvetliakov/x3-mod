#!/usr/bin/env python3
"""Fixed green A/B mass-only versus mass-plus-erosion reference images."""
from __future__ import annotations
import argparse,hashlib,importlib.util,json,time
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); return m
fog=load('fog_distance_replay',HERE/'fog_distance_replay.py')
connected=load('fog_connected_preview',HERE/'fog_connected_preview.py')
screen=load('fog_mass_column_screen',HERE/'fog_mass_column_screen.py')
F=np.float32; WIDTH,HEIGHT=128,72; SUN=np.array([1.,0.,0.],F); SEGMENTS=(('near',0.,fog.NEAR),('middle',fog.NEAR,fog.WINDOW_START),('shell',fog.WINDOW_START,fog.FAR))

def digest(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as stream:
  for block in iter(lambda:stream.read(1<<20),b''): h.update(block)
 return h.hexdigest()
def metric(values):
 a=np.asarray(values,np.float64).ravel(); return {'count':int(a.size),'mean':float(a.mean()) if a.size else 0.,'p50':float(np.percentile(a,50)) if a.size else 0.,'p90':float(np.percentile(a,90)) if a.size else 0.,'p99':float(np.percentile(a,99)) if a.size else 0.,'max':float(a.max(initial=0))}
def compose(a,b): return {'S':a['S']+a['T'][:,None]*b['S'],'T':a['T']*b['T']}
def make_rgba(rho,chroma):
 rgba=np.empty(np.asarray(rho).shape+(4,),F); rgba[...,:3]=np.asarray(rho,F)[...,None]*np.asarray(chroma,F); rgba[...,3]=rho; return rgba

def integrate_segment(origin,direction,spacing,start,end,chroma,sigma,chunk=64):
 n=len(direction); outputs={arm:{'S':np.zeros((n,3),F),'T':np.ones(n,F),'positive_samples':np.zeros(n,np.int32),'samples':np.zeros(n,np.int32),'occupied_length':np.zeros(n,np.float64)} for arm in ('mass_only','mass_plus_erosion')}
 length=end-start; count=int(np.ceil(length/spacing)); index=np.arange(count)[:,None]; lo=start+index*spacing
 for first in range(0,n,chunk):
  sl=slice(first,min(first+chunk,n)); d=direction[sl]; ds=np.clip(end-lo,0,spacing).astype(F); ds=np.broadcast_to(ds,(count,len(d))).copy(); distance=(lo+ds*.5).astype(F); points=np.asarray(origin,np.float64)+d[None,:,:]*distance[...,None]; density=screen.fields(points)
  for arm in outputs:
   rho=density[arm]; rgba=make_rgba(rho,chroma)
   S,T,_=fog.integrate_samples(rgba,ds,distance,sigma,d,True,SUN); outputs[arm]['S'][sl]=S; outputs[arm]['T'][sl]=T; outputs[arm]['positive_samples'][sl]=(rho>0).sum(axis=0); outputs[arm]['samples'][sl]=count; outputs[arm]['occupied_length'][sl]=np.sum(ds*(rho>0),axis=0)
 return outputs

def integrate(origin,direction,spacing,chroma,sigma):
 segments={name:integrate_segment(origin,direction,spacing,start,end,chroma,sigma) for name,start,end in SEGMENTS}; result={}
 for arm in ('mass_only','mass_plus_erosion'):
  near={k:segments['near'][arm][k] for k in ('S','T')}; middle={k:segments['middle'][arm][k] for k in ('S','T')}; shell={k:segments['shell'][arm][k] for k in ('S','T')}; full=compose(compose(near,middle),shell)
  samples=sum(segments[name][arm]['samples'] for name,_,_ in SEGMENTS); positive=sum(segments[name][arm]['positive_samples'] for name,_,_ in SEGMENTS); occupied=sum(segments[name][arm]['occupied_length'] for name,_,_ in SEGMENTS)
  result[arm]={'near':near,'middle':middle,'shell':shell,'full':full,'cost':{'samples':samples,'positive_samples':positive,'positive_fraction':positive/samples,'midpoint_positive_occupied_length_estimate':occupied}}
 return result

def convergence(ref128,ref64): return {'T':metric(np.abs(ref128['T']-ref64['T'])),'S_normalized_unit_radiance':[metric(np.abs(ref128['S'][:,c]-ref64['S'][:,c])) for c in range(3)]}
def convergence_pass(row): return row['T']['p99']<=.00025 and row['T']['max']<=.00075

def downsample_area(result):
 S=result['S'].reshape(HEIGHT*2,WIDTH*2,3).reshape(HEIGHT,2,WIDTH,2,3).mean(axis=(1,3),dtype=np.float64).astype(F)
 T=result['T'].reshape(HEIGHT*2,WIDTH*2).reshape(HEIGHT,2,WIDTH,2).mean(axis=(1,3),dtype=np.float64).astype(F)
 return {'S':S.reshape(-1,3),'T':T.reshape(-1)}

def rgb(values,maximum): return (np.clip(values.reshape(HEIGHT,WIDTH,3)/maximum,0,1)**(1/2.2)*255+.5).astype(np.uint8)
def scalar(values,maximum):
 x=(np.clip(values.reshape(HEIGHT,WIDTH)/maximum,0,1)*255+.5).astype(np.uint8); return np.repeat(x[...,None],3,axis=-1)
def signed_rgb(values,maximum): return (np.clip(.5+values.reshape(HEIGHT,WIDTH,3)/(2*maximum),0,1)*255+.5).astype(np.uint8)
def signed_scalar(values,maximum):
 x=(np.clip(.5+values.reshape(HEIGHT,WIDTH)/(2*maximum),0,1)*255+.5).astype(np.uint8); return np.repeat(x[...,None],3,axis=-1)

def histogram_panel(opacities,names=('near','full','shell'),width=340,height=HEIGHT*5*3):
 from PIL import Image,ImageDraw
 image=Image.new('RGB',(width,height),'black'); draw=ImageDraw.Draw(image); colors=('cyan','yellow','magenta'); top=44
 for row,(name,values,color) in enumerate(zip(names,opacities,colors)):
  y0=top+row*120; hist,_=np.histogram(values,bins=40,range=(0,1)); peak=max(int(hist.max()),1)
  draw.text((4,y0-16),f'{name} opacity 0..1 p50={np.percentile(values,50):.3f} p90={np.percentile(values,90):.3f} max={np.max(values):.3f}',fill=color)
  for i,count in enumerate(hist):
   x0=4+i*8; bar=int(90*count/peak); draw.rectangle((x0,y0+90-bar,x0+6,y0+90),fill=color)
 draw.text((4,4),'nonclipped 0..1 histograms; independent vertical normalization',fill='white'); return image

def write_base_sheet(output,prior_path,stem,arm_result):
 from PIL import Image,ImageDraw
 scale=5; w,h=WIDTH,HEIGHT; top,left=18,82
 with Image.open(prior_path) as prior:
  if prior.size!=(2642,1098): raise ValueError('unexpected cached rejected sheet dimensions')
  cached=prior.crop((left+2*w*scale,top,left+3*w*scale,top+3*h*scale)).copy()
 groups=[cached]
 for key in ('near','full','shell'):
  r=arm_result[key]; groups.append([rgb(r['S'],.03),scalar(1-r['T'],.4),scalar(r['T'],1.)])
 hist=histogram_panel([1-arm_result[k]['T'] for k in ('near','full','shell')]); sheet=Image.new('RGB',(left+4*w*scale+hist.width,top+3*h*scale),'black'); draw=ImageDraw.Draw(sheet)
 for col,label in enumerate(('rejected macro cached','new 0-2.4km','new 0-40km','new 30-40km shell')):
  draw.text((left+col*w*scale+2,3),label,fill='white')
  if col==0: sheet.paste(groups[col],(left,top))
  else:
   for row,panel in enumerate(groups[col]): sheet.paste(Image.fromarray(panel).resize((w*scale,h*scale),Image.Resampling.NEAREST),(left+col*w*scale,top+row*h*scale))
 for row,label in enumerate(('S / .03','opacity / .4','T / 1')): draw.text((2,top+row*h*scale+3),label,fill='white')
 sheet.paste(hist,(left+4*w*scale,top)); path=output/f'{stem}-cloud-only.png'; sheet.save(path); return path.name

def write_area_sheet(output,stem,point,area):
 from PIL import Image,ImageDraw
 scale=5; w,h=WIDTH,HEIGHT; top,left=18,82
 groups=[[rgb(point['S'],.03),scalar(1-point['T'],.4),scalar(point['T'],1.)],[rgb(area['S'],.03),scalar(1-area['T'],.4),scalar(area['T'],1.)],[signed_rgb(area['S']-point['S'],.005),signed_scalar(area['T']-point['T'],.02),scalar(np.abs(area['T']-point['T']),.02)]]
 hist=histogram_panel([1-point['T'],1-area['T'],np.abs(area['T']-point['T'])],('point full opacity','area full opacity','|area T - point T|')); sheet=Image.new('RGB',(left+3*w*scale+hist.width,top+3*h*scale),'black'); draw=ImageDraw.Draw(sheet)
 for col,label in enumerate(('128x72 point rays','256x144 linear S/T area average','area minus point')):
  draw.text((left+col*w*scale+2,3),label,fill='white')
  for row,panel in enumerate(groups[col]): sheet.paste(Image.fromarray(panel).resize((w*scale,h*scale),Image.Resampling.NEAREST),(left+col*w*scale,top+row*h*scale))
 for row,label in enumerate(('S/.03 or dS/.005','opacity/.4 or dT/.02','T/1 or |dT|/.02')): draw.text((2,top+row*h*scale+3),label,fill='white')
 sheet.paste(hist,(left+3*w*scale,top)); path=output/f'{stem}-area-witness.png'; sheet.save(path); return path.name

def laws(chroma):
 rho=np.array([0.,.25,1.],F); rgba=make_rgba(rho,chroma)
 a={'S':np.array([[.1,.2,.3]],F),'T':np.array([.8],F)}; b={'S':np.array([[.05,.04,.03]],F),'T':np.array([.7],F)}; c=compose(a,b)
 return {'screen_recipe_laws':bool(all(screen.laws().values())),'chroma_finite_nonnegative':bool(chroma.shape==(3,) and np.isfinite(chroma).all() and np.all(chroma>=0)),'premultiplied_density_chroma_identity':bool(np.array_equal(rgba[...,3],rho) and np.array_equal(rgba[...,:3],rho[:,None]*chroma) and np.array_equal(rgba[0],np.zeros(4,F))),'transport_composition_exact':bool(np.array_equal(c['S'],a['S']+a['T'][:,None]*b['S']) and np.array_equal(c['T'],a['T']*b['T'])),'area_average_constant_identity':bool(np.array_equal(downsample_area({'S':np.ones((HEIGHT*2*WIDTH*2,3),F),'T':np.ones(HEIGHT*2*WIDTH*2,F)})['T'],np.ones(HEIGHT*WIDTH,F)))}

def validate(result,output):
 from PIL import Image
 expected={f'green-pose{pose}-{arm}-cloud-only.png':(2982,1098) for pose in ('A','B') for arm in ('mass_only','mass_plus_erosion')}
 expected.update({f'green-poseB-{arm}-area-witness.png':(2342,1098) for arm in ('mass_only','mass_plus_erosion')})
 if result.get('schema')!=1 or set(result.get('images',{}))!=set(expected): raise ValueError('expected exact six-image manifest')
 if set(result.get('poses',{}))!={'A','B'}: raise ValueError('expected A/B')
 for name,want in result['images'].items():
  if digest(output/name)!=want: raise ValueError(f'image hash mismatch {name}')
  with Image.open(output/name) as image:
   if image.size!=expected[name]: raise ValueError(f'image dimensions mismatch {name}')
 json.dumps(result,sort_keys=True,allow_nan=False)

def run(asset_data,prior_output,output,plan):
 started=time.monotonic(); output.mkdir(parents=True,exist_ok=True); manifest_path=asset_data/'manifest.json'; manifest=json.loads(manifest_path.read_text()); profile=next(x for x in manifest['profiles'] if x['name']=='foggreenoutlands')
 base_sigma=float(profile['base_sigma']); sigma=base_sigma*1.5
 if abs(base_sigma-6.25e-6)>1e-15 or abs(sigma-screen.SIGMA)>1e-15: raise ValueError('unexpected green sigma contract')
 packet=asset_data/'foggreenoutlands.fogbin'; volume=fog.decode_packet(packet,manifest); rho_sum=float(volume[...,3].sum(dtype=np.float64)); chroma=(volume[...,:3].sum(axis=(0,1,2),dtype=np.float64)/rho_sum).astype(F)
 prior_report_path=prior_output/'report.json'; prior_report=json.loads(prior_report_path.read_text()); sources={'analysis_sha256':digest(Path(__file__)),'plan_sha256':digest(plan),'mass_column_screen_sha256':digest(HERE/'fog_mass_column_screen.py'),'manifest_sha256':digest(manifest_path),'packet_sha256':digest(packet),'decoded_sha256':profile['decoded_sha256'],'prior_report_sha256':digest(prior_report_path),'prior_images':{}}
 poses={}; images=[]; all_converged=True
 for pose in screen.pose_rows():
  origin=pose['origin']; direction=connected.camera_rays(origin,pose['forward'],pose['up'],WIDTH,HEIGHT); ref128=integrate(origin,direction,128.,chroma,sigma); ref64=integrate(origin,direction,64.,chroma,sigma); arms={}
  prior=prior_output/f'foggreenoutlands-pose{pose["name"]}-cloud-only.png'; prior_sha=digest(prior)
  if prior_report['images'].get(prior.name)!=prior_sha: raise ValueError('cached rejected image not report-bound')
  sources['prior_images'][prior.name]=prior_sha
  high=None
  if pose['name']=='B':
   high_direction=connected.camera_rays(origin,pose['forward'],pose['up'],WIDTH*2,HEIGHT*2); high=integrate(origin,high_direction,64.,chroma,sigma)
  for arm in ('mass_only','mass_plus_erosion'):
   conv={key:convergence(ref128[arm][key],ref64[arm][key]) for key in ('near','full','shell')}; ok=all(convergence_pass(x) for x in conv.values()); all_converged &= ok
   base_image=write_base_sheet(output,prior,f'green-pose{pose["name"]}-{arm}',ref64[arm]); images.append(base_image)
   row={'convergence_128_vs_64':conv,'converged':ok,'near_opacity':metric(1-ref64[arm]['near']['T']),'complete_opacity':metric(1-ref64[arm]['full']['T']),'shell_opacity':metric(1-ref64[arm]['shell']['T']),'sampled_positive_fraction':metric(ref64[arm]['cost']['positive_fraction']),'midpoint_positive_occupied_length_estimate':metric(ref64[arm]['cost']['midpoint_positive_occupied_length_estimate']),'base_image':base_image}
   if high is not None:
    area={key:downsample_area(high[arm][key]) for key in ('near','full','shell')}; witness={key:{'S':convergence(area[key],ref64[arm][key])['S_normalized_unit_radiance'],'T':metric(np.abs(area[key]['T']-ref64[arm][key]['T'])),'point_opacity':metric(1-ref64[arm][key]['T']),'area_opacity':metric(1-area[key]['T'])} for key in ('near','full','shell')}; area_image=write_area_sheet(output,f'green-poseB-{arm}',ref64[arm]['full'],area['full']); images.append(area_image); row['B_2x_linear_ST_area_average_witness']={'scope':'limited angular witness, not angular convergence proof','metrics':witness,'image':area_image}
   arms[arm]=row
  poses[pose['name']]={'origin':origin.tolist(),'forward':pose['forward'].tolist(),'up':pose['up'].tolist(),'arms':arms}
 laws_row=laws(chroma); status='passed-mass-detail-reference-convergence' if all_converged and all(laws_row.values()) else 'inconclusive-mass-detail-reference'
 counts={str(spacing):sum(int(np.ceil((end-start)/spacing)) for _,start,end in SEGMENTS) for spacing in (128.,64.)}; base_stations=2*WIDTH*HEIGHT*(counts['128.0']+counts['64.0']); high_stations=WIDTH*2*HEIGHT*2*counts['64.0']; total_stations=base_stations+high_stations
 image_paths=[output/name for name in sorted(images)]; result={'schema':1,'result':status,'appearance_selection':'pending parent/user review','sources':sources,'contract':{'family':'foggreenoutlands','poses':['A','B'],'arms':['mass_only','mass_plus_erosion'],'base_grid':[WIDTH,HEIGHT],'base_spacings_render_units':[128,64],'B_area_grid':[WIDTH*2,HEIGHT*2],'B_area_spacing_render_units':64,'B_downsample':'linear arithmetic mean of each2x2 S and T independently','chroma':chroma.tolist(),'chroma_derivation':'sum original premultiplied J / sum original rho; same constant in both arms','base_sigma':base_sigma,'strength_ratio':1.5,'effective_sigma':sigma,'lighting':'synthetic +X sun, normalized unit radiance, g=.3','no_tuning':True},'gates':{'base_T_convergence':all_converged,'laws_passed':all(laws_row.values()),'laws':laws_row},'poses':poses,'operation_counts':{'quadrature_stations_per_ray_by_spacing':counts,'base_unique_world_stations':base_stations,'B_2x_unique_world_stations':high_stations,'total_unique_world_stations':total_stations,'noise_evaluations_per_station':4,'lattice_corner_hashes_per_station':32,'density_atlas_reads':0,'one_time_chroma_reduction_from_decoded_atlas':True,'GPU_or_FPS_claim':False},'images':{p.name:digest(p) for p in image_paths},'limitations':['Reference convergence does not select appearance or a production representation.','The B 2x area average is one angular witness, not an angular convergence proof.','Constant family-average chroma controls density; it does not preserve spatial chroma detail.','Analytic operation counts are host reference work, not GPU timing or a candidate implementation.','Cloud-only normalized-unit-radiance sheets are not game composites or production-scaled lighting.','No parameter, seed, gain, camera, family or recipe search was performed.'],'host_seconds':time.monotonic()-started}
 validate(result,output); (output/'report.json').write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n'); report_sha=digest(output/'report.json'); rows=[a for p in poses.values() for a in p['arms'].values()]; worst=max(x['convergence_128_vs_64'][k]['T']['max'] for x in rows for k in ('near','full','shell')); area=max(poses['B']['arms'][a]['B_2x_linear_ST_area_average_witness']['metrics']['full']['T']['max'] for a in poses['B']['arms'])
 (output/'report.md').write_text(f'''# Fixed green mass/detail image packet\n\nResult: **{status}**. Four A/B arm views pass/fail only the fixed along-ray reference check; worst near/full/shell T max is {worst:.9g} against .00075. Appearance remains pending.\n\nSix hash-bound sheets contain four cached-rejected/near/full/shell comparisons and two B point-ray versus2x-linear-S/T-area-average witnesses. The largest complete B area-versus-point T difference is {area:.9g}; this is one angular witness, not angular convergence proof. Each sheet includes nonclipped0..1 opacity histograms beside unchanged display scales.\n\nNo tuning, new family/camera/path, bake, shader, game, Wine, build, production edit, install or commit occurred. Host runtime {result['host_seconds']:.2f}s.\n'''); (output/'summary.json').write_text(json.dumps({'result':status,'analysis_sha256':sources['analysis_sha256'],'plan_sha256':sources['plan_sha256'],'report_sha256':report_sha,'images':len(result['images']),'host_seconds':result['host_seconds']},indent=2,sort_keys=True)+'\n'); return result

def main():
 p=argparse.ArgumentParser(); p.add_argument('--asset-data',type=Path,required=True); p.add_argument('--prior-output',type=Path,default=Path('/tmp/x3-fog-sector-patches-preview')); p.add_argument('--output',type=Path,required=True); p.add_argument('--plan',type=Path,default=Path('/tmp/x3-fog-mass-detail-preview-plan.md')); a=p.parse_args(); r=run(a.asset_data,a.prior_output,a.output,a.plan); print(json.dumps({'result':r['result'],'seconds':r['host_seconds'],'output':str(a.output)},sort_keys=True))
if __name__=='__main__': raise SystemExit(main())
