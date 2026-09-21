#!/usr/bin/env python3
"""One fixed, modest detail refinement of the selected mass distribution."""
from __future__ import annotations
import argparse,hashlib,importlib.util,json,time
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); return m
fog=load('fog_distance_replay',HERE/'fog_distance_replay.py')
preview=load('fog_mass_detail_preview',HERE/'fog_mass_detail_preview.py')
screen=preview.screen; connected=preview.connected
F=np.float32; WIDTH,HEIGHT=128,72; SUN=np.array([1.,0.,0.],F); SEGMENTS=preview.SEGMENTS
EXPECTED_PREVIEW_SHA='eb8c2f67ab0b66e9fe98353d2a8abbe540400eb0f43dc45c96228f35beaeadc1'
EXPECTED_SCREEN_SHA='1c6ecc1d6cbdee7799f7291b22f568800a7d58d824f55a055a139ace48193106'

def digest(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as stream:
  for block in iter(lambda:stream.read(1<<20),b''): h.update(block)
 return h.hexdigest()

def refined_density(points):
 row=screen.fields(points); return np.maximum(0,row['B']*(.50+.50*row['Dlo'])-.20*(1-row['B'])*row['Dhi']).astype(F)

def integrate_segment(origin,direction,spacing,start,end,chroma,sigma,chunk=64):
 n=len(direction); S=np.zeros((n,3),F); T=np.ones(n,F); positive=np.zeros(n,np.int32); samples=np.zeros(n,np.int32); occupied=np.zeros(n,np.float64); rho_sum=np.zeros(n,np.float64)
 length=end-start; count=int(np.ceil(length/spacing)); index=np.arange(count)[:,None]; lo=start+index*spacing
 for first in range(0,n,chunk):
  sl=slice(first,min(first+chunk,n)); d=direction[sl]; ds=np.clip(end-lo,0,spacing).astype(F); ds=np.broadcast_to(ds,(count,len(d))).copy(); distance=(lo+ds*.5).astype(F); points=np.asarray(origin,np.float64)+d[None,:,:]*distance[...,None]; rho=refined_density(points); rgba=preview.make_rgba(rho,chroma)
  segS,segT,_=fog.integrate_samples(rgba,ds,distance,sigma,d,True,SUN); S[sl]=segS; T[sl]=segT; positive[sl]=(rho>0).sum(axis=0); samples[sl]=count; occupied[sl]=np.sum(ds*(rho>0),axis=0); rho_sum[sl]=rho.sum(axis=0,dtype=np.float64)
 return {'S':S,'T':T,'positive':positive,'samples':samples,'occupied':occupied,'rho_sum':rho_sum}

def integrate(origin,direction,spacing,chroma,sigma):
 seg={name:integrate_segment(origin,direction,spacing,start,end,chroma,sigma) for name,start,end in SEGMENTS}; near={k:seg['near'][k] for k in ('S','T')}; middle={k:seg['middle'][k] for k in ('S','T')}; shell={k:seg['shell'][k] for k in ('S','T')}; full=preview.compose(preview.compose(near,middle),shell); samples=sum(seg[n]['samples'] for n,_,_ in SEGMENTS); positive=sum(seg[n]['positive'] for n,_,_ in SEGMENTS)
 return {'near':near,'middle':middle,'shell':shell,'full':full,'column':{'sampled_density_mean':sum(seg[n]['rho_sum'] for n,_,_ in SEGMENTS)/samples,'sampled_positive_fraction':positive/samples,'midpoint_positive_occupied_length_estimate':sum(seg[n]['occupied'] for n,_,_ in SEGMENTS)}}

def write_base_sheet(output,prior_path,stem,result):
 from PIL import Image,ImageDraw
 scale=5; w,h=WIDTH,HEIGHT; top,left=18,82
 with Image.open(prior_path) as prior:
  if prior.size!=(2982,1098): raise ValueError('unexpected cached prior-reviewed-direction sheet dimensions')
  cached=prior.crop((left+2*w*scale,top,left+3*w*scale,top+3*h*scale)).copy()
 groups=[cached]
 for key in ('near','full','shell'):
  row=result[key]; groups.append([preview.rgb(row['S'],.03),preview.scalar(1-row['T'],.4),preview.scalar(row['T'],1.)])
 hist=preview.histogram_panel([1-result[k]['T'] for k in ('near','full','shell')]); sheet=Image.new('RGB',(left+4*w*scale+hist.width,top+3*h*scale),'black'); draw=ImageDraw.Draw(sheet)
 for col,label in enumerate(('accepted-direction old detail cached','refined 0-2.4km','refined 0-40km','refined 30-40km shell')):
  draw.text((left+col*w*scale+2,3),label,fill='white')
  if col==0: sheet.paste(groups[col],(left,top))
  else:
   for row,panel in enumerate(groups[col]): sheet.paste(Image.fromarray(panel).resize((w*scale,h*scale),Image.Resampling.NEAREST),(left+col*w*scale,top+row*h*scale))
 for row,label in enumerate(('S / .03','opacity / .4','T / 1')): draw.text((2,top+row*h*scale+3),label,fill='white')
 sheet.paste(hist,(left+4*w*scale,top)); path=output/f'{stem}-cloud-only.png'; sheet.save(path); return path.name

def laws(chroma):
 points=np.array([[0.,0.,0.],[12345.,-6789.,2222.],[-30000.,40000.,-50000.]]); row=screen.fields(points); refined=refined_density(points); rgba=preview.make_rgba(refined,chroma)
 return {'frozen_screen_laws':bool(all(screen.laws().values())),'mass_B_matches_frozen_threshold_law':bool(np.array_equal(row['B'],fog.smoothstep(.10,.40,row['f']).astype(F))),'refinement_finite_bounded_by_B':bool(np.isfinite(refined).all() and np.all((refined>=0)&(refined<=row['B']))),'refinement_never_exceeds_prior_detail':bool(np.all(refined<=row['mass_plus_erosion'])),'no_density_outside_mass':bool(np.all(refined[row['B']==0]==0)),'no_peak_boost':bool(float(refined.max(initial=0))<=1),'premultiplied_density_chroma_identity':bool(np.array_equal(rgba[...,3],refined) and np.array_equal(rgba[...,:3],refined[:,None]*chroma)),'area_average_constant_identity':bool(np.array_equal(preview.downsample_area({'S':np.ones((HEIGHT*2*WIDTH*2,3),F),'T':np.ones(HEIGHT*2*WIDTH*2,F)})['T'],np.ones(HEIGHT*WIDTH,F)))}

def validate(result,output):
 from PIL import Image
 expected={f'green-pose{pose}-refined-detail-cloud-only.png':(2982,1098) for pose in ('A','B')}; expected['green-poseB-refined-detail-area-witness.png']=(2342,1098)
 if result.get('schema')!=1 or set(result.get('images',{}))!=set(expected): raise ValueError('expected exact three-image manifest')
 for name,want in result['images'].items():
  if digest(output/name)!=want: raise ValueError(f'image hash mismatch {name}')
  with Image.open(output/name) as image:
   if image.size!=expected[name]: raise ValueError(f'image dimensions mismatch {name}')
 json.dumps(result,sort_keys=True,allow_nan=False)

def run(asset_data,prior_output,output,plan):
 started=time.monotonic(); output.mkdir(parents=True,exist_ok=True); manifest_path=asset_data/'manifest.json'; manifest=json.loads(manifest_path.read_text()); profile=next(x for x in manifest['profiles'] if x['name']=='foggreenoutlands'); base_sigma=float(profile['base_sigma']); sigma=base_sigma*1.5
 if abs(base_sigma-6.25e-6)>1e-15 or abs(sigma-screen.SIGMA)>1e-15: raise ValueError('unexpected green sigma contract')
 packet=asset_data/'foggreenoutlands.fogbin'; volume=fog.decode_packet(packet,manifest); rho_sum=float(volume[...,3].sum(dtype=np.float64)); chroma=(volume[...,:3].sum(axis=(0,1,2),dtype=np.float64)/rho_sum).astype(F)
 prior_report_path=prior_output/'report.json'; prior_report=json.loads(prior_report_path.read_text()); preview_sha=digest(HERE/'fog_mass_detail_preview.py'); screen_sha=digest(HERE/'fog_mass_column_screen.py')
 if preview_sha!=EXPECTED_PREVIEW_SHA or screen_sha!=EXPECTED_SCREEN_SHA or prior_report.get('sources',{}).get('analysis_sha256')!=EXPECTED_PREVIEW_SHA or prior_report.get('sources',{}).get('mass_column_screen_sha256')!=EXPECTED_SCREEN_SHA: raise ValueError('cached prior-reviewed-direction dependency mismatch')
 sources={'analysis_sha256':digest(Path(__file__)),'base_preview_plan_sha256':digest(plan),'frozen_preview_source_sha256':preview_sha,'frozen_screen_source_sha256':screen_sha,'cached_dependency_checks':{'prior_analysis_matches_expected':True,'prior_screen_matches_expected':True},'manifest_sha256':digest(manifest_path),'packet_sha256':digest(packet),'decoded_sha256':profile['decoded_sha256'],'prior_report_sha256':digest(prior_report_path),'prior_images':{}}
 poses={}; images=[]; all_converged=True
 for pose in screen.pose_rows():
  origin=pose['origin']; direction=connected.camera_rays(origin,pose['forward'],pose['up'],WIDTH,HEIGHT); ref128=integrate(origin,direction,128.,chroma,sigma); ref64=integrate(origin,direction,64.,chroma,sigma); conv={key:preview.convergence(ref128[key],ref64[key]) for key in ('near','full','shell')}; ok=all(preview.convergence_pass(x) for x in conv.values()); all_converged &= ok
  prior=prior_output/f'green-pose{pose["name"]}-mass_plus_erosion-cloud-only.png'; prior_sha=digest(prior)
  if prior_report['images'].get(prior.name)!=prior_sha: raise ValueError('cached accepted-direction image not report-bound')
  sources['prior_images'][prior.name]=prior_sha; image=write_base_sheet(output,prior,f'green-pose{pose["name"]}-refined-detail',ref64); images.append(image)
  old=prior_report['poses'][pose['name']]['arms']['mass_plus_erosion']; row={'convergence_128_vs_64':conv,'converged':ok,'near_opacity':preview.metric(1-ref64['near']['T']),'complete_opacity':preview.metric(1-ref64['full']['T']),'shell_opacity':preview.metric(1-ref64['shell']['T']),'column':{k:preview.metric(v) for k,v in ref64['column'].items()},'old_detail_cached_opacity_means':{'near':old['near_opacity']['mean'],'complete':old['complete_opacity']['mean'],'shell':old['shell_opacity']['mean']},'refined_minus_old_opacity_mean':{'near':float(np.mean(1-ref64['near']['T'])-old['near_opacity']['mean']),'complete':float(np.mean(1-ref64['full']['T'])-old['complete_opacity']['mean']),'shell':float(np.mean(1-ref64['shell']['T'])-old['shell_opacity']['mean'])},'base_image':image}
  if pose['name']=='B':
   high_direction=connected.camera_rays(origin,pose['forward'],pose['up'],WIDTH*2,HEIGHT*2); high=integrate(origin,high_direction,64.,chroma,sigma); area={key:preview.downsample_area(high[key]) for key in ('near','full','shell')}; witness={key:{'S':preview.convergence(area[key],ref64[key])['S_normalized_unit_radiance'],'T':preview.metric(np.abs(area[key]['T']-ref64[key]['T'])),'point_opacity':preview.metric(1-ref64[key]['T']),'area_opacity':preview.metric(1-area[key]['T'])} for key in ('near','full','shell')}; area_image=preview.write_area_sheet(output,'green-poseB-refined-detail',ref64['full'],area['full']); images.append(area_image); row['B_2x_linear_ST_area_average_witness']={'scope':'limited angular witness, not angular convergence proof','metrics':witness,'image':area_image}
  poses[pose['name']]={'origin':origin.tolist(),'forward':pose['forward'].tolist(),'up':pose['up'].tolist(),'refined_detail':row}
 laws_row=laws(chroma); status='passed-fixed-detail-refinement-reference' if all_converged and all(laws_row.values()) else 'inconclusive-fixed-detail-refinement'; counts={str(sp):sum(int(np.ceil((b-a)/sp)) for _,a,b in SEGMENTS) for sp in (128.,64.)}; base_stations=2*WIDTH*HEIGHT*(counts['128.0']+counts['64.0']); high_stations=WIDTH*2*HEIGHT*2*counts['64.0']; paths=[output/name for name in sorted(images)]
 result={'schema':1,'result':status,'appearance_selection':'pending parent/user review','decision_scope':'one parent-authorized fixed refinement only: new green A/B detail views plus B 2x area witness; no parameter search or automatic follow-up','sources':sources,'contract':{'family':'foggreenoutlands','poses':['A','B'],'density':'rho=max(0,B*(.50+.50*Dlo)-.20*(1-B)*Dhi)','authorization':'modest detail refinement after user requested a little more patches and varying density','preserved':'F/B/hash/scales/cameras/sigma/chroma/taper','old_detail':'cached comparison only; .65+.35 Dlo and .15 erosion','base_grid':[WIDTH,HEIGHT],'base_spacings_render_units':[128,64],'B_area_grid':[WIDTH*2,HEIGHT*2],'B_area_spacing_render_units':64,'B_downsample':'linear arithmetic mean of each2x2 S and T independently','chroma':chroma.tolist(),'base_sigma':base_sigma,'effective_sigma':sigma,'no_tuning':True,'no_peak_boost_floor_or_global_gain':True},'gates':{'base_T_convergence':all_converged,'laws_passed':all(laws_row.values()),'laws':laws_row},'poses':poses,'operation_counts':{'quadrature_stations_per_ray_by_spacing':counts,'base_unique_world_stations':base_stations,'B_2x_unique_world_stations':high_stations,'total_unique_world_stations':base_stations+high_stations,'noise_evaluations_per_station':4,'lattice_corner_hashes_per_station':32,'GPU_or_FPS_claim':False},'images':{p.name:digest(p) for p in paths},'limitations':['This one fixed refinement is an appearance packet, not a parameter search or production representation.','The B area average is one angular witness, not angular convergence proof.','Constant family-average chroma does not preserve spatial chroma variation.','No peak boost, density floor or global gain was added.'],'host_seconds':time.monotonic()-started}
 validate(result,output); (output/'report.json').write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n'); report_sha=digest(output/'report.json'); rows=[p['refined_detail'] for p in poses.values()]; worst=max(x['convergence_128_vs_64'][k]['T']['max'] for x in rows for k in ('near','full','shell')); area=poses['B']['refined_detail']['B_2x_linear_ST_area_average_witness']['metrics']['full']['T']['max']; deltaA=poses['A']['refined_detail']['refined_minus_old_opacity_mean']['complete']; deltaB=poses['B']['refined_detail']['refined_minus_old_opacity_mean']['complete']
 (output/'report.md').write_text(f'''# One fixed green detail refinement\n\nResult: **{status}**. A/B refined-detail views use the selected mass distribution with only `.50+.50*Dlo` and `.20` weak-edge erosion. This one parent-authorized refinement follows the user's request for a little more patches and density variation; no search follows automatically. Worst128/64 near/full/shell T max is {worst:.9g}; appearance remains a visible judgment.\n\nTwo hash-bound base sheets compare cached old detail with refined near/full/shell views; one B sheet compares point rays with direct2x2 linear S/T area averaging. Complete mean opacity changes versus cached old detail are A {deltaA:+.9g}, B {deltaB:+.9g}; these are descriptive and no gain or normalization was applied. Largest B full area-versus-point T difference is {area:.9g}; this is not angular convergence proof.\n\nNo tuning, other recipe/family/camera/path, bake, shader, game, Wine, build, production edit, install or commit occurred. Host runtime {result['host_seconds']:.2f}s.\n'''); (output/'summary.json').write_text(json.dumps({'result':status,'analysis_sha256':sources['analysis_sha256'],'base_preview_plan_sha256':sources['base_preview_plan_sha256'],'report_sha256':report_sha,'images':len(result['images']),'host_seconds':result['host_seconds']},indent=2,sort_keys=True)+'\n'); return result

def main():
 p=argparse.ArgumentParser(); p.add_argument('--asset-data',type=Path,required=True); p.add_argument('--prior-output',type=Path,default=Path('/tmp/x3-fog-mass-detail-preview')); p.add_argument('--output',type=Path,required=True); p.add_argument('--plan',type=Path,default=Path('/tmp/x3-fog-mass-detail-preview-plan.md')); a=p.parse_args(); r=run(a.asset_data,a.prior_output,a.output,a.plan); print(json.dumps({'result':r['result'],'seconds':r['host_seconds'],'output':str(a.output)},sort_keys=True))
if __name__=='__main__': raise SystemExit(main())
