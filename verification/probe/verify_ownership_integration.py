#!/usr/bin/env python3
"""Check capture cleanup and legacy semantics through the actual DLL."""
from collections import Counter
from pathlib import Path
import json
import re
import sys
root=Path(__file__).resolve().parents[2];results=root/'verification/results'
sys.path.insert(0,str(root/'tools/analysis'));sys.path.insert(0,str(root/'verification/probe'))
from summarize_capture import fields, summarize
from run_ownership_integration import MODES, selected_fixtures, sources, binaries
from verify_capture_state import verify as verify_capture


def verify_admission(trace, requested, final_count, device_count=0):
    """Serial fixture witness: distinguish nested factory and completed roots."""
    def records(prefix, names):
        result=[]
        for line in trace.splitlines():
            if not line.startswith(prefix+' '):
                continue
            values={}
            for token in line.split()[1:]:
                assert token.count('=')==1, 'Malformed admission field'
                key,value=token.split('=')
                assert key not in values and key in names, 'Duplicate or unknown admission field'
                if key=='phase':
                    assert value in ('device','factory'), 'Unknown admission teardown phase'
                    values[key]=value
                else:
                    assert re.fullmatch(r'[0-9]+',value), 'Invalid admission counter'
                    values[key]=int(value)
                    assert values[key]<=0xffffffffffffffff, 'Admission counter overflow'
            assert set(values)==set(names), 'Missing admission field'
            result.append(values)
        return result
    modes=records('application_admission_mode', ('requested','enabled','live_replay','coverage_complete'))
    assert modes==[dict(requested=int(requested),enabled=int(requested),live_replay=0,coverage_complete=0)], 'Admission startup mode mismatch'
    finals=records('application_admission_final', ('phase','active_roots','waiting_roots','admitted_roots','promotions','vetoes','first_veto','enabled'))
    assert Counter(row['phase'] for row in finals)==Counter({phase:count for phase,count in (('factory',final_count),('device',device_count)) if count}), 'Admission final teardown inventory mismatch'
    previous=0
    nested_factories=0
    previous_vetoes=0
    first_veto=0
    for index,row in enumerate(finals):
        assert row['enabled']==int(requested), 'Admission final mode mismatch'
        assert row['waiting_roots']==row['promotions']==0, 'Admission waiters or replay promotion leaked'
        assert row['vetoes']<=0xffffffff and row['first_veto']<=0xffffffff, 'Admission veto overflow'
        assert row['vetoes']|previous_vetoes==row['vetoes'], 'Permanent admission veto was cleared'
        if first_veto:
            assert row['first_veto']==first_veto, 'First admission veto changed'
        if row['vetoes']:
            reason=row['first_veto']
            assert reason and reason&(reason-1)==0 and reason&row['vetoes']==reason, 'First veto must identify a retained single reason'
            first_veto=reason
        else:
            assert row['first_veto']==0, 'First veto exists without a veto'
        previous_vetoes=row['vetoes']
        if row['active_roots']:
            # Last child -> captured device -> factory can expose the still-live
            # outer device entry. Only its immediately following finished device
            # witness proves that serial teardown actually leaves the monitor.
            assert requested and row['phase']=='factory' and row['active_roots']==1, 'Unexpected active teardown root'
            assert index+1<len(finals) and finals[index+1]['phase']=='device' and finals[index+1]['active_roots']==0, 'Nested factory lacks finished outer device witness'
            nested_factories+=1
        if requested:
            assert row['admitted_roots']>0 and row['admitted_roots']>=previous, 'Enabled admission root counter missing or regressed'
            previous=row['admitted_roots']
        else:
            assert row['admitted_roots']==row['vetoes']==row['first_veto']==0, 'Disabled admission performed bookkeeping'
    assert finals and finals[-1]['active_roots']==finals[-1]['waiting_roots']==0, 'Final serial teardown is not quiescent'
    return dict(enabled=bool(requested), final_factories=final_count, final_devices=device_count,
                nested_factory_witnesses=nested_factories, admitted_roots=previous,
                promotions=0, final_vetoes=finals[-1]['vetoes'])


def verify_portable_capture_metrics(metrics):
    """Authored capture fixture: each device writes VB60, IB6, then VB60 bytes.

    Counters are cumulative samples, never summed across repeated frame batches.
    POSITIONT is still unsupported by the motion reader, so no XYZ query runs.
    """
    expected = {(str(device), str(frame), 'present') for device in (1, 2) for frame in range(6)}
    expected |= {(str(device), '6', phase) for device in (1, 2) for phase in ('reset_before', 'reset_after')}
    samples = {}
    for metric in metrics:
        key = tuple(metric[k] for k in ('device', 'frame', 'phase'))
        assert key not in samples, 'Duplicate portable metric sample'
        samples[key] = metric
    assert set(samples) == expected, 'Portable upload metric inventory differs from authored fixture'
    for (device, frame, phase), metric in samples.items():
        replacement = int(frame) >= 4
        writes, classified = (3, 126) if replacement else (2, 66)
        assert metric['result'] == metric['status'] == '00000000'
        assert metric['requested'] == metric['active'] == '1'
        assert int(metric['generation']) == (2 if phase == 'reset_after' else 1)
        assert all(int(metric[k]) == writes for k in ('uploads', 'publications', 'scans'))
        assert int(metric['classified_bytes']) == classified
        assert int(metric['allocation_failures']) == int(metric['position_components']) == 0
        assert int(metric['peak_payload_bytes']) >= 10, 'VB76 requires nineteen four-bit cells'
        if phase == 'present':
            assert int(metric['sidecars']) >= 2 and int(metric['metadata_bytes']) > 0
            assert int(metric['payload_bytes']) >= 10, 'Readable managed VB atlas missing'
        if phase == 'reset_after':
            assert int(metric['payload_bytes']) == 0, 'Reset must retire position atlases'
    return dict(devices=2, uploads_per_device=3, publications_per_device=3,
                scans_per_device=3, classified_bytes_per_device=126,
                partial_vertex_upload_bytes=60, whole_index_upload_bytes=6,
                source_position_layout='POSITIONT remains unsupported', cumulative_samples_not_summed=True)


def verify_geometry(trace, requested, enabled, native_fallback=False, portable_capture=False):
    """Separate portable upload acceptance from unsupported renderer inputs."""
    assert not portable_capture or (requested and enabled and not native_fallback)
    mode = [fields(line) for line in trace.splitlines() if line.startswith('finite_upload_mode ')]
    metrics = [fields(line) for line in trace.splitlines() if line.startswith('finite_upload_metric ')]
    reasons = [fields(line) for line in trace.splitlines() if line.startswith('finite_upload_reason ')]
    portable = None
    if requested:
        assert len(mode) == 1 and mode[0]['requested'] == '1'
        assert mode[0]['enabled'] == str(int(enabled)) and mode[0]['payload_retained'] == '0'
        assert metrics, 'Requested finite statistics missing'
        if enabled and not native_fallback:
            assert all(m['result'] == '00000000' and m['requested'] == '1' for m in metrics)
            assert any(m['phase'] == 'present' and m['active'] == '1' and int(m['generation']) > 0 for m in metrics)
        else:
            assert all(m['result'] == '80070057' and m['requested'] == m['active'] == '0' for m in metrics)
        if portable_capture:
            portable = verify_portable_capture_metrics(metrics)
            assert not any(r['reason'] == '9' and int(r['count']) for r in reasons), 'Readable managed fixture allocations were refused'
        else:
            # UP-only/no-buffer cases cannot own an atlas. Enabled owners still
            # allocate their fixed 2048-pointer identity index (8192 bytes x86);
            # native fallback has no owner and must report zero metadata too.
            assert all(int(m[k]) == 0 for m in metrics for k in
                       ('payload_bytes', 'peak_payload_bytes', 'sidecars',
                        'global_payload_bytes', 'global_sidecars', 'publications', 'scans', 'classified_bytes', 'position_components'))
            expected_metadata=8192 if enabled and not native_fallback else 0
            assert all(int(m['metadata_bytes'])==expected_metadata for m in metrics), 'Unexpected empty-owner fixed metadata allocation'
    else:
        assert not mode and not metrics and not reasons, 'Inherited finite opt-in leaked into baseline'
    capture = summarize(trace, {})
    draws = [draw for frame in capture['frames'].values() for draw in frame['draws']]
    assert len(draws) == sum(line.startswith('motion_geometry ') for line in trace.splitlines()), 'Orphan or duplicate geometry record'
    known_indices = 0
    for draw in draws:
        assert draw['motion_input_matches_draw'] and draw['motion_geometry_matches_draw']
        motion, geometry = draw['motion_input'], draw['motion_geometry']
        assert motion['vertex_finite_verified'] == motion['lifetime_verified'] == '0'
        assert geometry['source_qualified'] == '0' and geometry['source_hash'] == '0000000000000000'
        assert geometry['source_words'] == '0' and geometry['finite_state'] == '0'
        assert geometry['index_required'] == str(int(draw['kind'] == 'indexed'))
        # This fixture's sole failed indexed call explicitly unbinds its IB.
        # A failed draw in general may still carry truthful storage extrema.
        expected_index = portable_capture and draw['kind'] == 'indexed' and draw['draw_result']['result'] == '00000000'
        if expected_index:
            assert geometry['index_requested'] == geometry['index_known'] == geometry['index_range_verified'] == geometry['index_exact'] == '1'
            assert geometry['index_status'] == '00000000' and geometry['index_reason'] == '0'
            assert geometry['index_generation'] == geometry['index_revision'] == motion['ib_revision'] == '1'
            assert geometry['index_min'] == '0' and geometry['index_max'] == '2'
            known_indices += 1
        else:
            assert geometry['index_known'] == geometry['index_range_verified'] == '0'
    if portable_capture:
        assert known_indices == 8, 'Four successful indexed draws per normal/pure device must use actual IB evidence'
    return dict(scoped_geometry_records=len(draws), finite_metric_records=len(metrics),
                known_index_draws=known_indices, portable_uploads=portable,
                native_contract_rejection_reported=any(int(r['count']) > 0 for r in reasons if r['reason'] == '9'))


def verify_motion_mode(trace, configured):
    mode = [fields(line) for line in trace.splitlines() if line.startswith('motion_capture_mode ')]
    assert len(mode) == 1 and mode[0]['requested'] == str(int(configured))
    assert mode[0]['scope'] == 'private_rigid_diagnostic' and mode[0]['temporal_consumer'] == '0'
    assert mode[0]['enabled'] == '0' and mode[0]['reason'] == 'write_exclusion_unavailable', 'Live replay must retain its explicit write-exclusion gate'
    assert not any(line.startswith('motion_replay ') for line in trace.splitlines()), 'Synthetic executable must not select a game replay boundary'
    return dict(motion_configured=configured, motion_enabled=False, motion_disabled_reason='write_exclusion_unavailable', motion_replay_records=0)


def verify():
    (results/'ownership-integration-verification.json').write_text(json.dumps({'result':'RUNNING'})+'\n')
    build=json.loads((results/'ownership-integration-build.json').read_text())
    assert build['result']=='PASS' and build['sources_before_build']==build['sources_at_start']==build['sources'],'Source provenance mismatch'
    assert build['binaries_at_start']==build['binaries_at_end'],'Binary provenance mismatch'
    assert build['sources']==sources() and build['binaries_at_end']==binaries(), 'Current source/binary mismatch'
    assert build['source_tree_unchanged_during_run'] and build['binaries_unchanged_during_run']
    expected_cases={f'ownership-integration-{mode}-{name}' for mode in MODES for name, _, _ in selected_fixtures(mode)}
    assert len(expected_cases)==26 and set(build['cases'])==expected_cases and all(c['exit']==0 for c in build['cases'].values()),'Incomplete integration run'
    report={'result':'PASS','dll_sha256':build['dll_sha256'],'cases':{}}
    for mode in MODES:
        for name, executable, _ in selected_fixtures(mode):
            prefix=f'ownership-integration-{mode}-{name}'
            import hashlib
            case=build['cases'][prefix]
            assert hashlib.sha256((results/(prefix+'.txt')).read_bytes()).hexdigest()==case['report_sha256']
            assert hashlib.sha256((results/(prefix+'-capture.log')).read_bytes()).hexdigest()==case['trace_sha256']
            assert case['dll_sha256']==build['dll_sha256']
            assert case['exe_sha256']==build['binaries_at_start']['verification/probe/build/'+executable]
            text=(results/(prefix+'.txt')).read_text();trace=(results/(prefix+'-capture.log')).read_text()
            assert 'FAIL' not in text,(mode,name)
            hooks=[fields(line) for line in trace.splitlines() if line.startswith('device_hooked ')]
            destroyed=[fields(line) for line in trace.splitlines() if line.startswith('device_destroy ')]
            expected={'smoke':2,'capture':2,'lifetime':16,'contracts':3,'auto':1}[name]
            assert len(hooks)==len(destroyed)==expected,(mode,name,len(hooks),len(destroyed))
            assert {h['device'] for h in hooks}=={h['device'] for h in destroyed}
            assert len({h['device'] for h in hooks})==expected
            wrapped=[line for line in trace.splitlines() if line.startswith('ownership_factory mode=wrapped ')]
            if mode in ('on','copy_depth','scene_depth','finite_on','motion_requested','admission_on'):assert len(wrapped)==(16 if name=='lifetime' else 1),(mode,name,wrapped)
            else:assert not wrapped,(mode,name)
            assert 'mode=native_fallback' not in trace,(mode,name)
            if mode=='depth_only':assert 'requested=0 depth_copy_requested=1 depth_copy_enabled=0' in trace
            depth=[fields(line) for line in trace.splitlines() if line.startswith('ownership_copy_depth ')]
            if mode in ('on','copy_depth','scene_depth','finite_on','motion_requested','admission_on'):
                assert len([d for d in depth if d['phase']=='create_after'])==expected
                if mode in ('on','finite_on','admission_on'):assert all(d['requested']=='0' and d['available']=='0' for d in depth)
                else:
                    assert all(d['requested']=='1' and d['available']=='1' and d['source_bound']=='1' for d in depth)
                    assert all(d['copy_valid']=='0' and d['copy_epoch']=='0' and d['source_format']=='77' for d in depth),'No automatic copy should occur'
                    assert int(depth[-1]['generation'])>int(depth[0]['generation']),'Reset must replace copy storage'
                if name=='auto':assert len([d for d in depth if d['phase']=='reset_after'])==1
            else:assert not depth
            if mode in ('object_requested','motion_requested'):
                assert 'object_trace active=0 status=executable_mismatch' in trace
                assert 'object_lifetime active=0 status=executable_mismatch' in trace
            if mode in ('scene_depth','motion_requested'):
                assert 'scene_depth_frame phase=begin ' in trace
                assert 'scene_depth_copy ' not in trace, 'Unrecognized synthetic frame must not select game boundary'
            else:assert 'scene_depth_frame phase=begin ' not in trace
            if name in ('smoke','capture'):
                states={int(fields(line)['id']) for line in trace.splitlines() if line.startswith('state ')}
                assert {52,53,54,55,56,57,58,59,185,186,187,188,189}<=states,'Missing stencil capture state'
            if name=='capture':
                verify_capture(trace)
                cpu_clear=[fields(line) for line in text.splitlines() if line.startswith('CPU_CLEAR ')]
                assert len(cpu_clear)==4 and sorted(c['case'] for c in cpu_clear)==['failure','failure','success','success']
                assert all(c['incoming']==c['outgoing']==c['args']=='1' for c in cpu_clear), 'Actual Clear hook CPU/argument boundary changed'
                assert all(bool(int(c['result'],16)&0x80000000)==(c['case']=='failure') for c in cpu_clear), 'Clear success/failure control missing'
                buffers=[fields(line) for line in trace.splitlines() if line.startswith('buffer_content ')]
                assert {b['kind'] for b in buffers}=={'vertex','index'}, 'Missing buffer revision diagnostics'
                if mode == 'finite_on':
                    assert all(b['requested'] == b['known'] == '1' and b['revision'] == '1' and b['pending'] == '0' for b in buffers), 'Finite option must independently enable write revisions'
                else:
                    assert all(b['requested']=='0' and b['known']=='0' for b in buffers), 'Disabled tracking must not claim stable content'
                expected_result='00000000' if mode in ('on','finite_on','admission_on') else '80070057'
                assert all(b['result']==expected_result for b in buffers), 'Native and wrapper metadata query paths differ'
            if name=='lifetime':assert 'INTEGRATION LIFETIME RESULT failures=0' in text
            if name=='contracts':assert 'OWNERSHIP RESULT checks=' in text and 'failures=0' in text
            admission = verify_admission(trace, mode in ('admission_on','admission_native'), 16 if name=='lifetime' else 1, expected)
            if mode=='admission_native':
                assert admission['final_vetoes'] & 2, 'Native factory escape must veto its unobserved route'
            geometry = verify_geometry(trace, mode in ('finite_on','finite_without_ownership','motion_requested'), mode in ('finite_on','motion_requested'),
                                       portable_capture=mode == 'finite_on' and name == 'capture')
            motion_mode = verify_motion_mode(trace, mode == 'motion_requested')
            if name in ('smoke', 'capture'):
                assert geometry['scoped_geometry_records'] > 0, 'Captured draw geometry missing'
            if mode == 'finite_on' and name == 'capture':
                assert geometry['portable_uploads'] and geometry['known_index_draws'] == 8, 'Portable allocation/index proof missing'
            reused=expected-len({h['ptr'] for h in hooks})
            if name=='lifetime':assert reused>0,'Address reuse not exercised; increase fixture rounds'
            report['cases'][f'{mode}-{name}']={'devices':expected,'destroyed':len(destroyed),'reused_device_addresses':reused, **geometry, **motion_mode, 'admission': admission}
    for name in ('capture','lifetime','contracts','auto'):
        a=(results/f'ownership-integration-off-{name}.txt').read_text()
        b=(results/f'ownership-integration-on-{name}.txt').read_text()
        # Numeric native Release results are diagnostics, not wrapper semantics.
        normalize=lambda value:[line for line in value.splitlines() if not line.startswith('OBSERVE device_release_with_child=')]
        assert normalize(a)==normalize(b),(name,'API report differs')
    def admission_api_report(path):
        # Each smoke process reports its distinct isolated DLL directory.
        return [line for line in path.read_text().splitlines() if not line.startswith('D3D9 loaded: ')]
    for name in ('smoke','capture','lifetime','contracts','auto'):
        assert admission_api_report(results/f'ownership-integration-admission_on-{name}.txt')==admission_api_report(results/f'ownership-integration-on-{name}.txt'), 'Admission observation changes original fixture API results'
    assert admission_api_report(results/'ownership-integration-admission_native-smoke.txt')==admission_api_report(results/'ownership-integration-off-smoke.txt'), 'Native-route admission observation changes original smoke API results'
    assert (results/'ownership-integration-off-auto.txt').read_text()==(results/'ownership-integration-copy_depth-auto.txt').read_text(),'Auto-depth loader smoke changes app-visible outcomes'
    assert (results/'ownership-integration-off-auto.txt').read_text()==(results/'ownership-integration-scene_depth-auto.txt').read_text(),'Scene observer changes app-visible outcomes'
    for finite_mode, baseline_mode, name in (('finite_on','on','capture'), ('finite_without_ownership','off','capture'), ('motion_without_prereqs','off','capture'), ('motion_requested','off','auto')):
        assert (results/f'ownership-integration-{finite_mode}-{name}.txt').read_bytes() == (results/f'ownership-integration-{baseline_mode}-{name}.txt').read_bytes(), 'Diagnostic option wiring changes original fixture API results'
    report['limits']=['Motion option cases establish configuration gates and synthetic-executable refusal, not successful replay or temporal consumption.','Synthetic DLL integration, not gameplay validation.','Direct3D9Ex remains native and uninstrumented.','Finite capture verifies readable managed upload/index evidence while POSITIONT/UP inputs remain ineligible; no XYZ or live motion candidate is established.','Original-preserving depth-copy allocation smoke only; numerical/content behavior has a separate fixture.']
    assert build['sources']==sources() and build['binaries_at_end']==binaries(), 'Source/binary changed during verification'
    report['current_sources_and_binaries_match']=True
    (results/'ownership-integration-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
if __name__=='__main__':
    try:verify()
    except (AssertionError,KeyError,OSError,ValueError) as exc:
        (results/'ownership-integration-verification.json').write_text(json.dumps({'result':'FAIL','reason':str(exc)},indent=2)+'\n')
        raise
