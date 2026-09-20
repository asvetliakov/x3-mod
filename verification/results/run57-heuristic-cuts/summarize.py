from pathlib import Path
import json,re,collections
p=Path(__file__).resolve().parent;src=next(Path('/tmp/x3-bottleX3-run205').glob('session*.log'));counts=collections.Counter();options={};mode={};motion=[];camera=[];frames={};resets=[];loading=[];epochs=collections.defaultdict(set)
for line in src.open():
 typ=line.split(' ',1)[0];counts[typ]+=1
 if typ not in ['proxy_options','motion_output_mode','motion_output_frame','camera_state','frame_end','loading_phase'] and not any(x in typ for x in ['reset','lifetime','epoch']):continue
 d=dict(re.findall(r'(\w+)=([^\s]+)',line))
 if typ=='proxy_options':options={k:v for k,v in d.items() if any(x in k for x in ['CUT','FRAME_LOG','FRAME_END_STRIDE','CAMERA_LOG','FOG','LIGHT_MAP_FAR','HULL_LIGHTMAP','TELEMETRY'])}
 elif typ=='motion_output_mode':mode=d
 elif typ=='motion_output_frame':motion.append(d)
 elif typ=='camera_state' and 'frame'in d:camera.append(d)
 elif typ=='loading_phase':loading.append(d)
 elif typ=='frame_end':frames[int(d['frame'])]=d
 elif typ!='camera_state':
  resets.append({'type':typ,'fields':d})
  for k,v in d.items():
   if 'epoch'in k:epochs[k].add(v)
keys=['frame','routed','cut','cut_median_px','cut_missing','taa_attempted','taa_resolved','taa_history','taa_skip','taa_result','camera_valid','camera_reason','camera_cut','camera_rotation_deg']
invalid=[{k:r.get(k) for k in keys} for r in motion if r['taa_history']=='0' and r['taa_attempted']=='1']
for r in invalid:r['elapsed_ms']=frames.get(int(r['frame']),{}).get('elapsed_ms')
summary={'session':str(src),'bytes':src.stat().st_size,'options':options,'mode':mode,'rows':dict(counts),'motion_frames':len(motion),'frame_range':[motion[0]['frame'],motion[-1]['frame']],'elapsed_ms_range':[frames[min(frames)]['elapsed_ms'],frames[max(frames)]['elapsed_ms']],'global_cut_count':sum(r['cut']=='1' for r in motion),'camera_cut_count':sum(r['camera_cut']=='1' for r in motion),'camera_policy_reason_counts':dict(collections.Counter((r.get('camera_reason','?') for r in motion))),'history_counts':dict(collections.Counter(r['taa_history'] for r in motion)),'attempted_counts':dict(collections.Counter(r['taa_attempted'] for r in motion)),'resolved_counts':dict(collections.Counter(r['taa_resolved'] for r in motion)),'skip_counts':dict(collections.Counter(r['taa_skip'] for r in motion)),'apply_failures':sum(int(r['apply_failures']) for r in motion),'restore_failures':sum(int(r['restore_failures']) for r in motion),'median_max':max(float(r['cut_median_px']) for r in motion),'missing_max':max(float(r['cut_missing']) for r in motion),'median_over_old48':sum(float(r['cut_median_px'])>48 for r in motion),'missing_over_old025':sum(float(r['cut_missing'])>.25 for r in motion),'old_either_count':sum(float(r['cut_median_px'])>48 or float(r['cut_missing'])>.25 for r in motion),'attempted_history_invalid':invalid,'camera_cut_rows':[{k:r.get(k) for k in keys} for r in motion if r['camera_cut']=='1'],'loading_phases':loading,'reset_epoch_rows':resets,'epoch_values':{k:sorted(v) for k,v in epochs.items()}}
assert len(motion)==len(frames)==len(camera)
assert sorted(frames)==list(range(len(frames)))
assert summary['global_cut_count']==0 and float(mode['cut_missing'])==1 and float(mode['cut_median_px'])>1e29 and mode['frame_log']=='1'
(p/'summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps({k:v for k,v in summary.items() if k not in ['rows','attempted_history_invalid','reset_epoch_rows','mode']},indent=2));print('attempted_history_invalid',len(invalid));print('reset_epoch_rows',len(resets))
