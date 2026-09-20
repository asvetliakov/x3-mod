#!/usr/bin/env python3
"""Read-only validation of the retained, intentionally every-frame diagnostic run.
No Wine, no subprocess, no source edits, no normalization of captured evidence.
The standard runner rejection is retained; this is NOT standard-case acceptance.
"""
import hashlib
import json
import math
import statistics
import sys
from collections import defaultdict
from decimal import Decimal
from pathlib import Path

ROOT = Path('/tmp/x3-submission-attribution')
NAME = 'production-ownership-taa-hdr-attribution-auto'
DIRECTORY = ROOT / 'verification/probe/build/motion-output-production-ownership-taa-hdr-attribution-auto-20260920-050531-786733'
RESULT = Path('/tmp/x3-submission-auto-validation.json')
checks = 0

def require(condition, label):
    global checks
    checks += 1
    if not condition:
        raise RuntimeError(label)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def fields(line):
    return dict(w.split('=', 1) for w in line.split() if '=' in w)

def main():
    # Reused existing validators contain assertions: refuse -O explicitly.
    require(__debug__, 'run without Python -O; existing helper assertions required')
    partial_path = DIRECTORY / 'harness-result.json'
    partial = json.loads(partial_path.read_text())
    require(partial['status'] == 'FAIL' and partial['passed'] is False, 'preserve generic harness failure')
    require(partial['selected_cases'] == [NAME] and partial['consume_only'] and not partial['build_commands'], 'retained-only selected case')
    require("'frame_log': '1'" in partial['error'] and partial['error'].startswith('AssertionError'), 'expected generic frame_log rejection')
    require(partial['bottle']['name'] == 'X3' and partial['bottle']['wine_arch'] == 'arm64', 'bottle X3 arm64')
    require(partial['bottle']['environment'] == {'FEX_X87REDUCEDPRECISION':'1','WINEMSYNC':'1'}, 'emulation environment')
    report_path = DIRECTORY / 'harness-result.txt'
    with report_path.open() as f:
        require(f.readline().strip() == f'==== {NAME} exit=0', 'actual retained process exit zero')
    stdout_path = DIRECTORY / 'fixture-stdout.txt'
    stdout = stdout_path.read_text() # 12.6 KiB fixture output, not the large capture
    require('FAIL' not in stdout and stdout.count('RESULT ') == 1, 'single successful fixture terminal')
    terminal = fields(stdout.splitlines()[-1])
    require(stdout.splitlines()[-1].startswith('RESULT PASS '), 'fixture process PASS')
    require((terminal['checks'],terminal['restorations'],terminal['frames']) == ('59','51','12'), 'expected fixture checks/restorations/frames')
    require(stdout.count('RESET PASS') == 1, 'fixture Reset PASS once')
    restores = [fields(l) for l in stdout.splitlines() if l.startswith('RESTORE ')]
    require(len(restores) == 51 and all(r['differences']=='0' for r in restores), '51 exact state restorations')
    require('CHECK device final Release reaches zero with the route\'s objects released PASS' in stdout, 'device final Release zero')
    require('CHECK factory final Release reaches zero PASS' in stdout, 'factory final Release zero')
    mode = fields(next(l for l in stdout.splitlines() if l.startswith('MODE ')))
    for k,v in {'seam':'0','enabled':'1','jitter':'1','taa':'1','bench':'0','width':'64','height':'64','hdr':'1','msaa':'0','camera':'0','hook':'0','rt_mode':'perdraw'}.items():
        require(mode[k]==v, f'fixture mode {k}')
    traces = list((DIRECTORY/'x3-modern-captures').glob('session-*.log'))
    require(len(traces)==1, 'one retained capture')
    trace_path=traces[0]
    selected=defaultdict(list); helper_lines=[]; failure_lines=[]; fallback=False
    prefixes={'motion_output_mode','motion_output_device','motion_output_frame','motion_output_reset','motion_output_release',
              'hdr_tonemap','hdr_device','hdr_frame','hdr_target','hdr_readback','shadow_replay_candidates_mode',
              'shadow_replay_depth_mode','shadow_lease_retirement','reset_begin','reset_end','device_hooked','device_destroy','taa_invalidate'}
    with trace_path.open() as f:
        for line in f:
            prefix=line.split(' ',1)[0]
            if prefix in prefixes:
                selected[prefix].append(fields(line))
            if prefix.startswith(('hdr_','ownership_','admission','application_admission','scene_depth')):
                helper_lines.append(line)
            if prefix.startswith(('motion_output_taa_failed','motion_output_fill_failed','motion_output_apply_failed','motion_output_restore_failed','hdr_unwind=')):
                failure_lines.append(line[:200])
            fallback |= 'mode=native_fallback' in line
    require(not failure_lines and not fallback, 'no operation/restore/unwind/fallback failure')
    def one(prefix):
        require(len(selected[prefix])==1, f'one {prefix}')
        return selected[prefix][0]
    config=one('motion_output_mode')
    for k,v in {'requested':'1','taa':'1','taa_debug':'1','jitter':'1','jitter_samples':'8','frame_log':'1','rt_mode':'perdraw','state_shadow':'auto','scene_hook':'0','hdr':'1','taa_k':'-1.00000','mip_bias':'0','taa_sharpen':'0.000'}.items():
        require(config[k]==v, f'actual diagnostic configuration {k}')
    exposure=one('hdr_tonemap')
    for k,v in {'enabled':'1','requested':'agx','tonemap':'1','tonemap_reason':'ok','meter':'1','meter_reason':'ok','exposure':'auto','fixed_dt_ms':'16.000','chain_format':'G32R32F'}.items():
        require(exposure[k]==v, f'actual HDR configuration {k}')
    candidates=one('shadow_replay_candidates_mode');depth=one('shadow_replay_depth_mode')
    require(all(candidates[k]=='1' for k in ('requested','enabled','motion_output','ownership')), 'lease prerequisites enabled')
    require(all(depth[k]=='1' for k in ('requested','enabled','motion_output','ownership')) and depth['size']=='64', 'depth replay 64 configured')
    def indexed(prefix):
        rows=selected[prefix];out={int(r['frame']):r for r in rows}
        require(len(rows)==len(out)==12 and sorted(out)==list(range(12)), f'unique complete {prefix} coverage')
        return out
    hdr=indexed('hdr_frame');leases=indexed('shadow_lease_retirement');frames=indexed('motion_output_frame')
    keys=['readback_transfer_lock_us','readback_extract_unlock_us','readback_statistics_adapt_us']
    hdr_rows=[];deltas=[]
    for frame,h in hdr.items():
        pending=frame not in (0,9)
        require(h['readback_clock_errors']=='0', f'HDR clock health {frame}')
        require(h['exposure']=='auto' and h['tonemap']=='agx' and h['meter']=='00000000', f'HDR execution {frame}')
        require(h['stepped']==str(int(pending)) and h['readback']==('00000000' if pending else '00000001'), f'readback cadence/Reset {frame}')
        values=[Decimal(h[k]) for k in keys];total=Decimal(h['readback_us'])
        require(all(v.is_finite() and v>=0 for v in values+[total]), f'finite HDR timers {frame}')
        delta=abs(total-sum(values));deltas.append(delta)
        require(delta<=Decimal('0.20'), f'exhaustive HDR bucket sum {frame}')
        require(all(v>0 for v in values) if pending else all(v==0 for v in values+[total]), f'measured or no-pending buckets {frame}')
        if pending: require(h['dt_ms']=='16.000' and int(h['tiles'])>0, f'adaptation consumed meter {frame}')
        hdr_rows.append({'frame':frame,'stepped':pending,'total_us':float(total),'buckets_us':[float(v) for v in values]})
        l=leases[frame]
        require(l['calls']=='2' and l['records']==l['refs']==l['clock_errors']=='0', f'empty lease scan counts/health {frame}')
        require(math.isfinite(float(l['us'])) and float(l['us'])>0, f'lease timer measured {frame}')
        m=frames[frame]
        require(all(m[k]=='00000000' for k in ('fill_result','fill_restore','present','taa_result','taa_restore')), f'D3D/Present results {frame}')
        require(all(m[k]=='0' for k in ('apply_failures','restore_failures','active_queries','scene_open')), f'state/queries {frame}')
        require(m['latched']==m['filled']==m['committed']==m['taa_attempted']==m['taa_resolved']==m['taa_hdr']=='1' and m['taa_skip']=='0', f'TAA/HDR actually ran {frame}')
        require(m['taa_history']==str(int(pending)) and m['taa_copy']=='00000001', f'TAA post-Reset history/FP16 {frame}')
        require(m['scene_end_source']=='stretchrect' and m['scene_end_check']=='3' and m['scene_hook']=='0', f'expected boundary {frame}')
        require(m['rt_mode']=='perdraw' and m['timing']=='cpu_qpc' and int(m['set_rt'])==4*int(m['routed']), f'route state/timing {frame}')
    require(one('reset_end')['result']=='00000000' and one('motion_output_reset')['generation']=='2', 'successful Reset generation')
    require(one('taa_invalidate')['site']=='reset' and selected['taa_invalidate'][0]['frame']=='9', 'only Reset invalidates history')
    require([int(t['frame']) for t in selected['hdr_target']]==[0,9], 'HDR targets recreated after Reset')
    require(len(selected['device_hooked'])==len(selected['device_destroy'])==len(selected['motion_output_release'])==1, 'one balanced device lifecycle')
    # Reuse unchanged, independently scoped deeper HDR and ownership checks;
    # explicit frames=range(12) matches actual frame_log=1, without editing logs.
    sys.path.insert(0,str(ROOT/'verification/probe'))
    import run_motion_output as runner
    helper_trace=''.join(helper_lines)
    deeper_hdr=runner.validate_hdr(NAME,helper_trace,DIRECTORY,True,None,range(12),range(1,9),'bloom_copy',True)
    deeper_ownership=runner.validate_ownership(NAME,'ownership',True,helper_trace)
    require(deeper_hdr['enabled'] and deeper_ownership['wrapped'], 'existing HDR/ownership validators pass at actual coverage')
    dll=DIRECTORY/'d3d9.dll';exe=DIRECTORY/'motion_output_fixture.exe'
    require(digest(dll)==digest(ROOT/'build/d3d9.dll'), 'retained DLL matches clean candidate')
    frozen_dll=[v for k,v in partial['binaries'].items() if Path(k).resolve()==(ROOT/'build/d3d9.dll').resolve()]
    frozen_exe=[v for k,v in partial['binaries'].items() if Path(k).resolve()==Path('/Users/asvetl/x3-mod/verification/probe/build/motion_output_fixture.exe').resolve()]
    require(frozen_dll==[digest(dll)] and frozen_exe==[digest(exe)], 'retained bytes match runner pre-execution binary hashes')
    require(digest(exe)==digest(Path('/Users/asvetl/x3-mod/verification/probe/build/motion_output_fixture.exe')), 'retained fixture provenance')
    return {'result':'PASS_SCOPED_TIMER_VALIDATION','standard_case_pass':False,'checks':checks,
            'bottle':partial['bottle'],'case':NAME,'directory':str(DIRECTORY),
            'provenance':{'dll_sha256':digest(dll),'fixture_sha256':digest(exe),'capture_sha256':digest(trace_path),'stdout_sha256':digest(stdout_path)},
            'generic_harness':{'status':partial['status'],'passed':partial['passed'],'error':partial['error'],'preserved':True,
                              'additional_incompatible_assumptions':['generic motion/HDR frame inventory expects 0..8; intentional frame_log=1 logs 0..11']},
            'process':{'exit':0,'exit_evidence':str(report_path),'checks':59,'state_restorations':51,'frames':12,'reset_passes':1,'final_device_and_factory_references':0},
            'configuration':{'motion':config,'hdr':exposure,'candidates':candidates,'depth':depth},
            'hdr':{'rows':12,'successful_readbacks':10,'no_pending_frames':[0,9],'clock_errors':0,'max_bucket_sum_delta_us':float(max(deltas)),
                   'bucket_names':keys,'rows_compact':hdr_rows,'existing_hdr_validator':'PASS; actual frames0..11, capture1..8'},
            'lease':{'rows':12,'calls':sum(int(l['calls'])for l in leases.values()),'records':0,'references':0,'clock_errors':0,
                     'median_us_per_frame':statistics.median(float(l['us'])for l in leases.values()),'min_us':min(float(l['us'])for l in leases.values()),'max_us':max(float(l['us'])for l in leases.values())},
            'deeper_checks':{'D3D_state_Present_TAA_Hdr': 'PASS all12 frames','HDR_Reset_recreation':'PASS frames0/9','ownership':deeper_ownership},
            'limitations':['Generic runner remains FAIL; this is not standard case acceptance.', 'Only empty lease scans are exercised; nonempty releases remain unqualified by this run.',
                          'Successful/no-pending readback only; no injected HRESULT or clock failures in this run.', '64x64 synthetic CrossOver run; no gameplay/performance or native Windows claim.']}

if __name__=='__main__':
    try:
        output=main()
    except BaseException as error:
        output={'result':'FAIL_SCOPED_TIMER_VALIDATION','standard_case_pass':False,'checks':checks,'error':repr(error)}
        RESULT.write_text(json.dumps(output,indent=2)+'\n')
        raise
    RESULT.write_text(json.dumps(output,indent=2)+'\n')
    print(json.dumps({k:output[k]for k in ('result','checks','process','lease')}))
