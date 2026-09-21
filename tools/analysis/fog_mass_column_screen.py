#!/usr/bin/env python3
"""Cheap density-only green A/B column screen for one fixed mass/detail recipe."""
from __future__ import annotations
import argparse,hashlib,importlib.util,json,time
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); return m
fog=load('fog_distance_replay',HERE/'fog_distance_replay.py')
connected=load('fog_connected_preview',HERE/'fog_connected_preview.py')
patches=load('fog_sector_patches_preview',HERE/'fog_sector_patches_preview.py')
F=np.float32; P=fog.PERIOD; R=patches.R; WIDTH,HEIGHT=16,9; SIGMA=9.375e-6
RANGES=(('near_0_2.4km',0.,fog.NEAR),('middle_2.4_30km',fog.NEAR,fog.WINDOW_START),('shell_30_40km',fog.WINDOW_START,fog.FAR),('complete_0_40km',0.,fog.FAR))

def digest(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as stream:
  for block in iter(lambda:stream.read(1<<20),b''): h.update(block)
 return h.hexdigest()

def fields(points):
 x=np.asarray(points,np.float64); r2=R@R
 f=(2*patches.value_noise(x/(2*P),3)+patches.value_noise((x@R.T)/P,4))/3
 B=fog.smoothstep(.10,.40,f).astype(F); Dlo=((patches.value_noise((x@r2.T)/(P/4),5)+1)/2).astype(F); Dhi=((patches.value_noise(x/(P/16),6)+1)/2).astype(F)
 detail=np.maximum(0,B*(.65+.35*Dlo)-.15*(1-B)*Dhi).astype(F)
 return {'f':f.astype(F),'B':B,'Dlo':Dlo,'Dhi':Dhi,'mass_only':B,'mass_plus_erosion':detail}

def metric(values):
 a=np.asarray(values,np.float64).ravel(); return {'count':int(a.size),'mean':float(a.mean()) if a.size else 0.,'p0':float(np.percentile(a,0)) if a.size else 0.,'p10':float(np.percentile(a,10)) if a.size else 0.,'p50':float(np.percentile(a,50)) if a.size else 0.,'p90':float(np.percentile(a,90)) if a.size else 0.,'p99':float(np.percentile(a,99)) if a.size else 0.,'max':float(a.max(initial=0))}

def bin_overlap(count,start,end):
 width=fog.FAR/count; lo=np.arange(count)*width; return np.maximum(np.minimum(lo+width,end)-np.maximum(lo,start),0).astype(np.float64)

def summarize_density(rho,distance,count,start,end):
 overlap=bin_overlap(count,start,end)[:,None]; active=overlap>0; support=rho>0; window=1-fog.smoothstep(fog.WINDOW_START,fog.FAR,distance)
 occupied=np.sum(overlap*support,axis=0); raw=np.sum(rho*overlap,axis=0,dtype=np.float64); tapered=np.sum(rho*window*overlap,axis=0,dtype=np.float64); tau=SIGMA*tapered; T=np.exp(-tau)
 samples=rho[active[:,0]]; chords=[]
 for ray in range(rho.shape[1]):
  length=0.
  for present,width in zip(support[:,ray],overlap[:,0]):
   if present and width>0: length+=width
   elif length: chords.append(length); length=0.
  if length: chords.append(length)
 return {'rho_samples':metric(samples),'exact_zero_sample_fraction':float(np.mean(samples==0)),'positive_support_sample_fraction':float(np.mean(samples>0)),'per_ray_sampled_positive_bin_fraction':metric(np.sum(support*active,axis=0)/max(int(active.sum()),1)),'midpoint_positive_occupied_length_estimate':metric(occupied),'midpoint_positive_chord_length_estimate':metric(chords),'raw_density_integral':metric(raw),'windowed_density_integral':metric(tapered),'tau':metric(tau),'T':metric(T),'opacity':metric(1-T),'_arrays':{'occupied':occupied,'tapered':tapered,'tau':tau,'T':T}}

def evaluate_pose(origin,direction,count):
 width=fog.FAR/count; distance=((np.arange(count)+.5)*width).astype(np.float64)[:,None]; points=np.asarray(origin,np.float64)+direction[None,:,:]*distance[...,None]; density=fields(points); arms={}
 for arm in ('mass_only','mass_plus_erosion'):
  arms[arm]={'ranges':{name:summarize_density(density[arm],distance,count,start,end) for name,start,end in RANGES}}
 return arms

def strip_arrays(row): return {k:(strip_arrays(v) if isinstance(v,dict) else v) for k,v in row.items() if k!='_arrays'}
def pose_rows(): return [dict(p,name=name) for p,name in zip(connected.poses()[:2],('A','B'))]
def rays_for_pose(pose): return pose['origin'],connected.camera_rays(pose['origin'],pose['forward'],pose['up'],WIDTH,HEIGHT)
def error_metric(a,b): return metric(np.abs(np.asarray(a)-np.asarray(b)))

def laws():
 points=np.array([[0,0,0],[12345,-6789,2222],[-30000,40000,-50000]],np.float64); row=fields(points)
 full_tau=SIGMA*175000; near_tau=SIGMA*fog.NEAR
 return {'rotation_orthonormal':bool(np.allclose(R.T@R,np.eye(3),rtol=0,atol=1e-15)),'fields_finite':bool(all(np.isfinite(v).all() for v in row.values())),'mass_bounded':bool(np.all((row['B']>=0)&(row['B']<=1))),'detail_support_preserving':bool(np.all((row['mass_plus_erosion']>=0)&(row['mass_plus_erosion']<=row['B']))),'mass_arm_equals_B_exact':bool(np.array_equal(row['mass_only'],row['B'])),'sigma_derivation_exact':bool(abs(SIGMA-6.25e-6*(.03/.02))<1e-20),'full_bound_tau_T_opacity_exact':bool(abs(full_tau-1.640625)<1e-15 and abs(np.exp(-full_tau)-np.exp(-1.640625))<1e-15 and abs(-np.expm1(-full_tau)+np.expm1(-1.640625))<1e-15),'near_12000_render_units_2.4km_bound_tau_T_opacity_exact':bool(abs(near_tau-.1125)<1e-15 and abs(np.exp(-near_tau)-np.exp(-.1125))<1e-15 and abs(-np.expm1(-near_tau)+np.expm1(-.1125))<1e-15),'range_bin_overlaps_partition_full_column':bool(np.allclose(sum(bin_overlap(512,a,b) for _,a,b in RANGES[:3]),bin_overlap(512,0,fog.FAR),rtol=0,atol=0))}

def validate(result):
 if result.get('schema')!=1 or set(result.get('poses',{}))!={'A','B'}: raise ValueError('invalid pose/schema result')
 if result.get('operation_counts',{}).get('unique_world_stations')!=221184: raise ValueError('wrong station count')
 for pose in result['poses'].values():
  if set(pose.get('arms',{}))!={'mass_only','mass_plus_erosion'}: raise ValueError('invalid arm result')
 json.dumps(result,sort_keys=True,allow_nan=False)

def run(asset_data,output,design):
 started=time.monotonic(); output.mkdir(parents=True,exist_ok=True); manifest_path=asset_data/'manifest.json'; manifest=json.loads(manifest_path.read_text()); profile=next(x for x in manifest['profiles'] if x['name']=='foggreenoutlands')
 if abs(float(profile['base_sigma'])-6.25e-6)>1e-15: raise ValueError('unexpected green base sigma')
 sources={'analysis_sha256':digest(Path(__file__)),'diagnosis_sha256':digest(design),'distance_replay_sha256':digest(HERE/'fog_distance_replay.py'),'connected_preview_sha256':digest(HERE/'fog_connected_preview.py'),'sector_patches_preview_sha256':digest(HERE/'fog_sector_patches_preview.py'),'manifest_sha256':digest(manifest_path),'green_profile':{'name':profile['name'],'base_sigma':profile['base_sigma'],'effective_sigma':SIGMA,'decoded_sha256':profile['decoded_sha256']}}
 poses={}
 for pose in pose_rows():
  origin,direction=rays_for_pose(pose); raw={count:evaluate_pose(origin,direction,count) for count in (256,512)}; arms={}
  for arm in ('mass_only','mass_plus_erosion'):
   ranges={}
   for name,_,_ in RANGES:
    ref=raw[512][arm]['ranges'][name]; coarse=raw[256][arm]['ranges'][name]; ranges[name]={'reference512':strip_arrays(ref),'coarse256':strip_arrays(coarse),'residual_256_vs_512':{'midpoint_positive_occupied_length_estimate_abs':error_metric(ref['_arrays']['occupied'],coarse['_arrays']['occupied']),'windowed_density_integral_abs':error_metric(ref['_arrays']['tapered'],coarse['_arrays']['tapered']),'tau_abs':error_metric(ref['_arrays']['tau'],coarse['_arrays']['tau']),'T_abs':error_metric(ref['_arrays']['T'],coarse['_arrays']['T'])}}
   arms[arm]={'ranges':ranges}
  effects={}
  for name,_,_ in RANGES:
   mass=raw[512]['mass_only']['ranges'][name]['_arrays']; detail=raw[512]['mass_plus_erosion']['ranges'][name]['_arrays']; effects[name]={'midpoint_positive_occupied_length_estimate_detail_minus_mass':metric(detail['occupied']-mass['occupied']),'windowed_density_integral_detail_minus_mass':metric(detail['tapered']-mass['tapered']),'tau_detail_minus_mass':metric(detail['tau']-mass['tau']),'T_detail_minus_mass':metric(detail['T']-mass['T'])}
  poses[pose['name']]={'origin':origin.tolist(),'forward':pose['forward'].tolist(),'up':pose['up'].tolist(),'ray_grid':[WIDTH,HEIGHT],'arms':arms,'erosion_effect_reference512':effects}
 full_tau=SIGMA*175000; near_tau=SIGMA*fog.NEAR
 result={'schema':1,'result':'completed-fixed-mass-column-screen','decision_scope':'density/column numerical preflight only; no art acceptance or automatic full preview','sources':sources,'contract':{'family':'foggreenoutlands','poses':['A','B'],'ray_grid':[WIDTH,HEIGHT],'uniform_midpoint_station_counts':[256,512],'unique_world_stations':221184,'arms':['mass_only','mass_plus_erosion'],'ranges':[[n,a,b] for n,a,b in RANGES],'density':'fixed F/B/D recipe; no original alpha or old slow macro','sigma_derivation':'6.25e-6 * (.03/.02) = 9.375e-6','sigma_effective':SIGMA,'extinction_bounds':{'full_effective_length_render_units':175000.,'full_tau':full_tau,'full_T':float(np.exp(-full_tau)),'full_opacity':float(-np.expm1(-full_tau)),'near_length_render_units':fog.NEAR,'near_length_km':2.4,'near_tau':near_tau,'near_T':float(np.exp(-near_tau)),'near_opacity':float(-np.expm1(-near_tau)),'role':'mathematical bounds only; not visual gates'},'support_length_definition':'midpoint-positive bin widths are summed; chord and occupied lengths are quadrature estimates, not continuous support-root solutions','scattering_or_images':False,'parameter_search':False},'laws':laws(),'poses':poses,'recorded_rejected_green_complete_mean_opacity_context':{'A':.311597,'B':.318151,'use':'context only; no fitting'},'operation_counts':{'rays_per_pose':WIDTH*HEIGHT,'unique_world_stations':221184,'noise_evaluations_per_station':4,'lattice_corner_hashes_per_station':32,'total_lattice_corner_hashes':221184*32,'fine_atlas_reads':0,'S_or_phase_evaluations':0},'limitations':['This screen measures deterministic sparse-ray columns, extinction and erosion effects; it cannot establish appearance, area filtering or production feasibility.','Sampled-positive occupied/chord lengths estimate support from midpoint bins; they are not exact continuous roots.','Counts are descriptive and carry no statistical confidence intervals. No opacity, support or convergence threshold selects an art result.','The fixed screen stops after this inventory regardless of outcome; no full preview follows automatically.'],'host_seconds':time.monotonic()-started}
 if not all(result['laws'].values()): raise ValueError('operator laws failed')
 validate(result); (output/'report.json').write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n'); report_sha=digest(output/'report.json')
 complete=[poses[p]['arms'][a]['ranges']['complete_0_40km']['reference512'] for p in ('A','B') for a in ('mass_only','mass_plus_erosion')]; tau_max=max(x['tau']['max'] for x in complete); opacity_max=max(x['opacity']['max'] for x in complete); residual=max(poses[p]['arms'][a]['ranges']['complete_0_40km']['residual_256_vs_512']['T_abs']['max'] for p in ('A','B') for a in ('mass_only','mass_plus_erosion'))
 (output/'report.md').write_text(f'''# Fixed green A/B mass-column screen\n\nResult: **completed fixed density-only preflight**. This is not art acceptance and does not trigger a full preview.\n\nThe two16x9 saved-camera sky-ray sets compare mass-only and mass+erosion using256/512 uniform midpoint stations. Sigma is 6.25e-6*(.03/.02)=9.375e-6. Mathematical full-column tau/T/opacity bounds are {full_tau:.9g}/{np.exp(-full_tau):.9g}/{-np.expm1(-full_tau):.9g}; near12,000-render-unit (2.4km) bounds are {near_tau:.9g}/{np.exp(-near_tau):.9g}/{-np.expm1(-near_tau):.9g}. These are contextual, not quality gates. Measured complete-column maximum tau/opacity is {tau_max:.9g}/{opacity_max:.9g}; worst256-vs512 absolute T residual is {residual:.9g}. Full near/middle/shell sampled support, rho, density-integral, tau/T and erosion-effect distributions are in report.json. Sampled-positive occupied/chord lengths sum midpoint-bin widths and are not exact continuous support roots.\n\nNo S, images, area witness, rescaling, seed search, atlas-alpha carrier, old slow macro, game, Wine, build, production edit, install or commit was used. Host runtime {result['host_seconds']:.3f}s.\n'''); (output/'summary.json').write_text(json.dumps({'result':result['result'],'analysis_sha256':sources['analysis_sha256'],'diagnosis_sha256':sources['diagnosis_sha256'],'report_sha256':report_sha,'host_seconds':result['host_seconds']},indent=2,sort_keys=True)+'\n'); return result

def main():
 p=argparse.ArgumentParser(); p.add_argument('--asset-data',type=Path,required=True); p.add_argument('--output',type=Path,required=True); p.add_argument('--design',type=Path,default=Path('/tmp/x3-fog-sector-patches-diagnosis.md')); a=p.parse_args(); r=run(a.asset_data,a.output,a.design); print(json.dumps({'result':r['result'],'seconds':r['host_seconds'],'output':str(a.output)},sort_keys=True))
if __name__=='__main__': raise SystemExit(main())
