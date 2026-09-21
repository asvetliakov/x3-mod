#!/usr/bin/env python3
"""Fixed analytic 64-total-step transport screen for refined mass fog."""
from __future__ import annotations
import argparse,hashlib,importlib.util,json,math,time
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); return m
fog=load('fog_distance_replay',HERE/'fog_distance_replay.py')
preview=load('fog_mass_detail_preview',HERE/'fog_mass_detail_preview.py')
refinement=load('fog_mass_detail_refinement',HERE/'fog_mass_detail_refinement.py')
screen=preview.screen; connected=preview.connected
F=np.float32; SUN=np.array([1.,0.,0.],F); SIGMA=screen.SIGMA; FULL_W,FULL_H=128,72
FROZEN_REFINEMENT_SHA256='0fdfc2c872de2ebe51b241c6eea241bec683a0de2cb217e9b36592decc3a9ad4'
SEGMENTS=(('near',0.,fog.NEAR),('middle',fog.NEAR,fog.WINDOW_START),('shell',fog.WINDOW_START,fog.FAR))
DEPTH_LIMITS=(1000.,11999.,12001.,149999.,150001.,199999.,200001.); INVALID_DEPTHS=(0.,float('nan'),float('inf')); WITNESS_PIXELS=((0,0),(31,0),(0,17),(31,17),(16,9))

def digest(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as stream:
  for block in iter(lambda:stream.read(1<<20),b''): h.update(block)
 return h.hexdigest()
def metric(values):
 a=np.asarray(values,np.float64).ravel(); return {'count':int(a.size),'mean':float(a.mean()) if a.size else 0.,'p50':float(np.percentile(a,50)) if a.size else 0.,'p99':float(np.percentile(a,99)) if a.size else 0.,'max':float(a.max(initial=0))}
def compose(a,b): return {'S':a['S']+a['T'][:,None]*b['S'],'T':a['T']*b['T']}

def subset_rays(pose):
 full=connected.camera_rays(pose['origin'],pose['forward'],pose['up'],FULL_W,FULL_H); xs=4*np.arange(32)+2; ys=4*np.arange(18)+2; yy,xx=np.meshgrid(ys,xs,indexing='ij'); indices=(yy*FULL_W+xx).ravel(); return full[indices],indices,xx.ravel(),yy.ravel()
def witness_indices(): return np.array([j*32+i for i,j in WITNESS_PIXELS],np.int32)
def view_lengths(x,y):
 u=(np.asarray(x)+.5)/FULL_W; v=(np.asarray(y)+.5)/FULL_H; t=math.tan(math.radians(30)); local=np.stack(((2*u-1)*(FULL_W/FULL_H)*t,(1-2*v)*t,np.ones_like(u)),axis=-1); return np.linalg.norm(local,axis=1)

def density_at(points):
 row=screen.fields(points); rho=np.maximum(0,row['B']*(.50+.50*row['Dlo'])-.20*(1-row['B'])*row['Dhi']).astype(F); return row['B'].astype(F),rho

def integrate_grid(origin,direction,limit,chroma,kind,value,collect_cost=False,chunk=64,density_fn=None):
 density_fn=density_at if density_fn is None else density_fn
 n=len(direction); outputs={name:{'S':np.zeros((n,3),F),'T':np.ones(n,F)} for name,_,_ in SEGMENTS}; outputs['full']={'S':np.zeros((n,3),F),'T':np.ones(n,F)}
 cost={k:np.zeros(n,np.float64) for k in ('stations','B_positive_stations','rho_positive_stations','density_reads')}
 if collect_cost and kind=='steps' and int(value)==64: cost['B_positive_mask']=np.zeros((64,n),bool)
 composition_S=np.zeros((n,3),F); composition_T=np.ones(n,F)
 for first in range(0,n,chunk):
  sl=slice(first,min(first+chunk,n)); d=direction[sl]; L=np.clip(np.asarray(limit[sl],np.float64),0,fog.FAR)
  if kind=='steps': counts=np.where(L>0,int(value),0).astype(np.int32)
  else: counts=np.ceil(L/float(value)).astype(np.int32)
  max_count=int(counts.max(initial=0))
  if not max_count: continue
  delta=np.divide(L,counts,out=np.zeros_like(L),where=counts>0); index=np.arange(max_count)[:,None]; active=index<counts[None,:]; lo=index*delta[None,:]; hi=np.minimum((index+1)*delta[None,:],L[None,:]); ds=np.where(active,hi-lo,0.); distance=lo+ds*.5; points=np.asarray(origin,np.float64)+d[None,:,:]*distance[...,None]
  B=np.zeros(active.shape,F); rho=np.zeros(active.shape,F)
  if active.any(): B[active],rho[active]=density_fn(points[active])
  rgba=preview.make_rgba(rho,chroma); cost['stations'][sl]=counts; cost['B_positive_stations'][sl]=(B>0).sum(axis=0); cost['rho_positive_stations'][sl]=(rho>0).sum(axis=0); cost['density_reads'][sl]=4*counts+4*(B>0).sum(axis=0)
  if 'B_positive_mask' in cost: cost['B_positive_mask'][:max_count,sl]=B>0
  for name,start,end in SEGMENTS:
   overlap=np.maximum(np.minimum(hi,end)-np.maximum(lo,start),0.); S,T,_=fog.integrate_samples(rgba,overlap.astype(F),distance.astype(F),SIGMA,d,True,SUN); outputs[name]['S'][sl]=S; outputs[name]['T'][sl]=T
  S,T,_=fog.integrate_samples(rgba,ds.astype(F),distance.astype(F),SIGMA,d,True,SUN); outputs['full']['S'][sl]=S; outputs['full']['T'][sl]=T
  composed=compose(compose(outputs['near'],outputs['middle']),outputs['shell']); composition_S[sl]=composed['S'][sl]; composition_T[sl]=composed['T'][sl]
 outputs['composition_error']={'S':np.abs(outputs['full']['S']-composition_S),'T':np.abs(outputs['full']['T']-composition_T)}
 if collect_cost:
  outputs['cost']=cost
 return outputs

def errors(candidate,reference):
 return {'T':metric(np.abs(candidate['T']-reference['T'])),'S_normalized_unit_radiance':[metric(np.abs(candidate['S'][:,c]-reference['S'][:,c])) for c in range(3)]}
def candidate_pass(row): return row['T']['p99']<=.001 and row['T']['max']<=.003 and all(x['p99']<=.0005 and x['max']<=.002 for x in row['S_normalized_unit_radiance'])
def reference_pass(row): return row['T']['p99']<=.00025 and row['T']['max']<=.00075
def composition_pass(row): return row['T']['max']<=2e-6 and row['S']['max']<=2e-6

def score_population(origin,direction,limit,chroma,collect_cost=False):
 candidate=integrate_grid(origin,direction,limit,chroma,'steps',64,collect_cost); ref64=integrate_grid(origin,direction,limit,chroma,'spacing',64.); ref128=integrate_grid(origin,direction,limit,chroma,'spacing',128.); segments={}; passed=True
 for name in ('near','shell','full'):
  ce=errors(candidate[name],ref64[name]); re=errors(ref128[name],ref64[name]); cp=candidate_pass(ce); rp=reference_pass(re); passed &= cp and rp; segments[name]={'candidate64_total_vs_dense64':ce,'candidate_passed':cp,'dense128_vs64':re,'reference_converged':rp}
 composition={label:{'T':metric(row['composition_error']['T']),'S':metric(row['composition_error']['S'])} for label,row in (('candidate64_total',candidate),('dense64',ref64),('dense128',ref128))}
 composition_ok=all(composition_pass(row) for row in composition.values()); passed &= composition_ok
 result={'segments':segments,'passed':passed,'composition':composition,'composition_passed':composition_ok}
 if collect_cost: result['cost_raw']=candidate['cost']
 return result,candidate,ref64

def cost_summary(cost):
 stations=cost['stations']; B=cost['B_positive_stations']; rho=cost['rho_positive_stations']; reads=cost['density_reads']; return {'stations':metric(stations),'B_positive_fraction':metric(np.divide(B,stations,out=np.zeros_like(B),where=stations>0)),'rho_positive_fraction':metric(np.divide(rho,stations,out=np.zeros_like(rho),where=stations>0)),'density_reads_per_ray':metric(reads),'density_read_ratio_vs_old48':metric(reads/48.),'shadow_work':{'executed_shadow_reads':0,'potential_rho_positive_stations':metric(rho),'scope':'analytic normalized-lighting screen; no shadow map evaluated'},'full_resolution_repair_population':{'status':'not_applicable_without_full_resolution_depth_field','synthetic_geometry_cases_reported_separately':True}}

def branch_coherence_masks(masks):
 totals={'homogeneous_empty':0,'homogeneous_active':0,'mixed':0}
 for mask in masks:
  if np.asarray(mask).shape!=(64,18*32): raise ValueError('expected fixed64 by 32x18 B mask')
  groups=np.asarray(mask).reshape(64,18,32).reshape(64,9,2,16,2).sum(axis=(2,4))
  totals['homogeneous_empty']+=int((groups==0).sum()); totals['homogeneous_active']+=int((groups==4).sum()); totals['mixed']+=int(((groups>0)&(groups<4)).sum())
 total=sum(totals.values())
 return {**totals,'total_2x2_step_groups':total,'fractions':{k:v/total for k,v in totals.items()},'definition':'2x2 adjacent pixels at each of64 candidate stations; analytic branch-lane proxy, not measured GPU execution'}

def failed_gate_list(poses,temporal_pass,laws_row):
 failed=[]
 for pose,row in poses.items():
  for population in ('sky','geometry'):
   scored=row[population]
   for segment,data in scored['segments'].items():
    if not data['candidate_passed']: failed.append(f'{pose}/{population}/{segment}/candidate')
    if not data['reference_converged']: failed.append(f'{pose}/{population}/{segment}/reference')
   for grid,data in scored['composition'].items():
    if not composition_pass(data): failed.append(f'{pose}/{population}/composition/{grid}')
  for index,data in enumerate(row['invalid_depth_cases']):
   if not data['identity']: failed.append(f'{pose}/invalid_depth/{index}')
 if not temporal_pass: failed.append('temporal_movement')
 failed.extend(f'law/{name}' for name,value in laws_row.items() if not value)
 return failed

def laws(chroma):
 rho=np.array([0.,.25,1.],F); rgba=preview.make_rgba(rho,chroma); source=np.array([[.2,.3,.4,0.],[.4,.5,.6,.37],[.7,.8,.9,1.]],F); vacuum={'S':np.zeros((3,3),F),'T':np.ones(3,F)}; comp=source.copy(); comp[:,:3]=vacuum['S']+vacuum['T'][:,None]*source[:,:3]
 pose=screen.pose_rows()[0]; direction,_,_,_=subset_rays(pose); zero=integrate_grid(pose['origin'],direction[:3],np.zeros(3,F),chroma,'steps',64)
 empty=integrate_grid(pose['origin'],direction[:3],np.full(3,200000,F),chroma,'steps',64,density_fn=lambda points:(np.zeros(len(points),F),np.zeros(len(points),F)))
 points=np.array([[-1e7,2e7,-3e7],[-12345.5,67890.25,42.],[0.,0.,0.],[9e6,-8e6,7e6]],np.float64); _,duplicated=density_at(points); frozen=refinement.refined_density(points)
 return {'refined_field_laws':bool(all(refinement.laws(chroma).values())),'duplicated_density_exactly_matches_frozen_refinement':bool(np.array_equal(duplicated,frozen)),'premultiplied_density_chroma_identity':bool(np.array_equal(rgba[:,:3],rho[:,None]*chroma) and np.array_equal(rgba[:,3],rho)),'zero_limit_identity':bool(np.array_equal(zero['full']['S'],np.zeros((3,3),F)) and np.array_equal(zero['full']['T'],np.ones(3,F))),'nonzero_length_empty_medium_identity':bool(np.array_equal(empty['full']['S'],np.zeros((3,3),F)) and np.array_equal(empty['full']['T'],np.ones(3,F))),'source_alpha_0_037_1_preserved_exact':bool(np.array_equal(comp[:,3],source[:,3]))}

def validate(result):
 if result.get('schema')!=1 or set(result.get('poses',{}))!={'A','B'}: raise ValueError('invalid A/B result')
 if result.get('operation_counts',{}).get('candidate_steps_per_valid_ray')!=64: raise ValueError('not fixed64')
 json.dumps(result,sort_keys=True,allow_nan=False)

def run(asset_data,output,plan):
 started=time.monotonic(); output.mkdir(parents=True,exist_ok=True); manifest_path=asset_data/'manifest.json'; manifest=json.loads(manifest_path.read_text()); profile=next(x for x in manifest['profiles'] if x['name']=='foggreenoutlands'); base_sigma=float(profile['base_sigma'])
 if not math.isfinite(base_sigma) or abs(base_sigma*1.5-SIGMA)>1e-15: raise ValueError('sigma mismatch')
 if digest(HERE/'fog_mass_detail_refinement.py')!=FROZEN_REFINEMENT_SHA256: raise ValueError('frozen refinement source mismatch')
 packet=asset_data/'foggreenoutlands.fogbin'; volume=fog.decode_packet(packet,manifest); chroma=(volume[...,:3].sum(axis=(0,1,2),dtype=np.float64)/volume[...,3].sum(dtype=np.float64)).astype(F)
 sources={'analysis_sha256':digest(Path(__file__)),'runtime_plan_sha256':digest(plan),'frozen_refinement_source_sha256':digest(HERE/'fog_mass_detail_refinement.py'),'frozen_screen_source_sha256':digest(HERE/'fog_mass_column_screen.py'),'manifest_sha256':digest(manifest_path),'packet_sha256':digest(packet),'decoded_sha256':profile['decoded_sha256']}
 poses={}; all_pass=True; all_cost=[]; witness_data={}; invalid_pass=True
 for pose in screen.pose_rows():
  direction,indices,x,y=subset_rays(pose); sky_limit=np.full(len(direction),fog.FAR,F); sky,candidate,reference=score_population(pose['origin'],direction,sky_limit,chroma,True); all_pass &= sky['passed']; all_cost.append(sky.pop('cost_raw')); wi=witness_indices(); witness_data[pose['name']]={'pose':pose,'direction':direction[wi],'x':x[wi],'y':y[wi]}
  geometry_direction=np.repeat(direction[wi],len(DEPTH_LIMITS),axis=0); requested=np.tile(np.asarray(DEPTH_LIMITS,F),len(wi)); clipped=np.minimum(requested,fog.FAR); lengths=np.repeat(view_lengths(x[wi],y[wi]),len(DEPTH_LIMITS)); depth_b=requested/lengths; reconstructed=np.minimum(depth_b*lengths,fog.FAR); geometry,_,_=score_population(pose['origin'],geometry_direction,reconstructed.astype(F),chroma); all_pass &= geometry['passed']
  invalid_rows=[]
  for value in INVALID_DEPTHS:
   invalid=np.full(len(wi),value); invalid_limit=np.where((~np.isfinite(invalid))|(invalid<=0),0,invalid*view_lengths(x[wi],y[wi])); inv=integrate_grid(pose['origin'],direction[wi],invalid_limit.astype(F),chroma,'steps',64); identity=bool(np.array_equal(inv['full']['S'],np.zeros((len(wi),3),F)) and np.array_equal(inv['full']['T'],np.ones(len(wi),F))); all_pass &= identity; invalid_pass &= identity; invalid_rows.append({'depth_b':('NaN' if np.isnan(value) else 'Infinity' if np.isinf(value) else value),'rays':len(wi),'identity':identity})
  poses[pose['name']]={'sky':sky,'geometry':geometry,'geometry_cases':{'witness_subset_indices':wi.tolist(),'requested_radial_limits':list(DEPTH_LIMITS),'cases':len(requested),'synthetic_depth_b':metric(depth_b),'reconstructed_radial_error':metric(np.abs(reconstructed-clipped))},'invalid_depth_cases':invalid_rows}
 # Temporal residuals for adjacent -1024/0/+1024 X origins: 20 ray-pair cases total.
 temporal=[]; temporal_pass=True
 for pose_name,row in witness_data.items():
  states=[]
  for shift in (-1024.,0.,1024.):
   origin=np.asarray(row['pose']['origin'],np.float64)+np.array([shift,0,0]); limit=np.full(5,fog.FAR,F); cand=integrate_grid(origin,row['direction'],limit,chroma,'steps',64); ref=integrate_grid(origin,row['direction'],limit,chroma,'spacing',64.); states.append((shift,cand,ref))
  for pair in range(2):
   for ray in range(5):
    entry={'pose':pose_name,'ray':ray,'shifts':[states[pair][0],states[pair+1][0]],'segments':{}}; pair_ok=True
    for name in ('near','shell','full'):
     residual=abs((float(states[pair+1][1][name]['T'][ray])-float(states[pair][1][name]['T'][ray]))-(float(states[pair+1][2][name]['T'][ray])-float(states[pair][2][name]['T'][ray]))); entry['segments'][name]=residual; pair_ok &= residual<=.003
    entry['passed']=pair_ok; temporal_pass &= pair_ok; temporal.append(entry)
 all_pass &= temporal_pass
 laws_row=laws(chroma); all_pass &= all(laws_row.values()); combined_cost={key:np.concatenate([row[key] for row in all_cost],axis=(1 if key=='B_positive_mask' else 0)) for key in all_cost[0]}; cost=cost_summary(combined_cost)
 failed=failed_gate_list(poses,temporal_pass,laws_row)
 if all_pass==bool(failed): raise AssertionError('all_pass/failure witness mismatch')
 status='passed-analytic64-transport-screen' if all_pass else 'failed-analytic64-transport-screen'
 result={'schema':1,'result':status,'decision':('permits conditional cache comparison only' if all_pass else 'hard stop before cache/shader/step search'),'sources':sources,'contract':{'field':'frozen refined .50/.50/.20 density','poses':['A','B'],'subset':'x=4i+2,y=4j+2 from128x72,32x18 per pose','witness_subset_i_j':[list(x) for x in WITNESS_PIXELS],'candidate':'one global64 midpoint grid over clipped radial L; shared stations for exact segment-bin overlaps','dense_references':'one global grid each, n=ceil(L/h), h64/128 render units','segments':[[n,a,b] for n,a,b in SEGMENTS],'depth_limits':list(DEPTH_LIMITS),'movement_origin_X_offsets_render_units':[-1024,0,1024],'lighting':'constant green mean chroma, +X sun, normalized unit radiance','no_images_cache_GPU_or_search':True},'gates':{'candidate_T':{'p99':.001,'max':.003},'candidate_S_per_channel':{'p99':.0005,'max':.002},'reference_T':{'p99':.00025,'max':.00075},'composition_S_T_max':2e-6,'temporal_T_residual_max':.003,'invalid_depth_identity_cases_passed':invalid_pass,'laws':laws_row,'failed_gates':failed,'all_passed':all_pass},'poses':poses,'temporal_movement':{'cases':len(temporal),'passed':temporal_pass,'rows':temporal},'cost':{**cost,'population':'A/B sky subset only: 32x18 rays per pose, fixed64 stations'},'branch_coherence':{**branch_coherence_masks([row['B_positive_mask'] for row in all_cost]),'population':'A/B sky subset only'},'operation_counts':{'candidate_steps_per_valid_ray':64,'sky_rays':2*32*18,'geometry_cases':2*5*len(DEPTH_LIMITS),'invalid_cases':2*5*len(INVALID_DEPTHS),'movement_ray_pair_cases':len(temporal),'density_reads_per_step_min_max':[4,8],'old_density_reads_per_ray':48},'limitations':['This is an analytic sparse holdout, not appearance, captured motion, GPU timing or production feasibility.','Branch coherence is an analytic 2x2-pixel station grouping over A/B sky subsets, not measured GPU wave/quad execution.','No shadow maps or full-resolution depth-repair population are present; potential rho-positive shadow stations and this exclusion are reported.','A pass permits only the separately ratified cache comparison; a failure stops before cache allocation or shader work.'],'host_seconds':time.monotonic()-started}
 validate(result); (output/'report.json').write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n'); report_sha=digest(output/'report.json')
 temporal_max=max(v for row in temporal for v in row['segments'].values()); (output/'report.md').write_text(f'''# Fixed analytic 64-total-step transport screen\n\nResult: **{status}**. {result['decision']}. Failed gates: {', '.join(failed) if failed else 'none'}. Temporal movement residual max is {temporal_max:.9g} against .003 across {len(temporal)} ray-pair cases.\n\nThe candidate uses exactly64 global density stations per valid ray and shares each station across near/middle/shell overlap accounting. Dense h64/h128 references use the same global-grid convention. Cost ranges from {cost['density_reads_per_ray']['p50']:.0f} median to {cost['density_reads_per_ray']['max']:.0f} max analytic density reads per sky ray versus old48; this is not GPU timing.\n\nNo images, cache allocation, bake, shader, GPU, Wine, game, production edit, step-count search, tuning, install or commit occurred. Host runtime {result['host_seconds']:.2f}s.\n'''); (output/'summary.json').write_text(json.dumps({'result':status,'decision':result['decision'],'analysis_sha256':sources['analysis_sha256'],'runtime_plan_sha256':sources['runtime_plan_sha256'],'report_sha256':report_sha,'host_seconds':result['host_seconds']},indent=2,sort_keys=True)+'\n'); return result

def main():
 p=argparse.ArgumentParser(); p.add_argument('--asset-data',type=Path,required=True); p.add_argument('--output',type=Path,required=True); p.add_argument('--plan',type=Path,default=Path('/tmp/x3-fog-mass-runtime-plan.md')); a=p.parse_args(); r=run(a.asset_data,a.output,a.plan); print(json.dumps({'result':r['result'],'decision':r['decision'],'seconds':r['host_seconds'],'output':str(a.output)},sort_keys=True))
if __name__=='__main__': raise SystemExit(main())
