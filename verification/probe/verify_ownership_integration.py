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

def verify_geometry(trace, requested, enabled, native_fallback=False):
    """These original fixtures are deliberately unsupported finite inputs."""
    mode = [fields(line) for line in trace.splitlines() if line.startswith('finite_upload_mode ')]
    metrics = [fields(line) for line in trace.splitlines() if line.startswith('finite_upload_metric ')]
    reasons = [fields(line) for line in trace.splitlines() if line.startswith('finite_upload_reason ')]
    if requested:
        assert len(mode) == 1 and mode[0]['requested'] == '1'
        assert mode[0]['enabled'] == str(int(enabled)) and mode[0]['payload_retained'] == '0'
        assert metrics, 'Requested finite statistics missing'
        if enabled and not native_fallback:
            assert all(m['result'] == '00000000' and m['requested'] == '1' for m in metrics)
            assert any(m['phase'] == 'present' and m['active'] == '1' and int(m['generation']) > 0 for m in metrics)
        else:
            assert all(m['result'] == '80070057' and m['requested'] == m['active'] == '0' for m in metrics)
        # Usage-zero fixture buffers and UP data must never get a certificate.
        assert all(int(m[k]) == 0 for m in metrics for k in
                   ('payload_bytes', 'peak_payload_bytes', 'sidecars', 'metadata_bytes',
                    'global_payload_bytes', 'global_sidecars', 'publications', 'scans', 'classified_bytes', 'position_components'))
    else:
        assert not mode and not metrics and not reasons, 'Inherited finite opt-in leaked into baseline'
    capture = summarize(trace, {})
    draws = [draw for frame in capture['frames'].values() for draw in frame['draws']]
    assert len(draws) == sum(line.startswith('motion_geometry ') for line in trace.splitlines()), 'Orphan or duplicate geometry record'
    for draw in draws:
        assert draw['motion_input_matches_draw'] and draw['motion_geometry_matches_draw']
        motion, geometry = draw['motion_input'], draw['motion_geometry']
        assert motion['vertex_finite_verified'] == motion['lifetime_verified'] == '0'
        assert geometry['source_qualified'] == '0' and geometry['source_hash'] == '0000000000000000'
        assert geometry['source_words'] == '0'
        assert geometry['finite_state'] == '0' and geometry['index_known'] == '0'
        assert geometry['index_range_verified'] == '0'
        assert geometry['index_required'] == str(int(draw['kind'] == 'indexed'))
    return dict(scoped_geometry_records=len(draws), finite_metric_records=len(metrics),
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
    assert len(build['cases'])==20 and all(c['exit']==0 for c in build['cases'].values()),'Incomplete integration run'
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
            if mode in ('on','copy_depth','scene_depth','finite_on','motion_requested'):assert len(wrapped)==(16 if name=='lifetime' else 1),(mode,name,wrapped)
            else:assert not wrapped,(mode,name)
            assert 'mode=native_fallback' not in trace,(mode,name)
            if mode=='depth_only':assert 'requested=0 depth_copy_requested=1 depth_copy_enabled=0' in trace
            depth=[fields(line) for line in trace.splitlines() if line.startswith('ownership_copy_depth ')]
            if mode in ('on','copy_depth','scene_depth','finite_on','motion_requested'):
                assert len([d for d in depth if d['phase']=='create_after'])==expected
                if mode in ('on','finite_on'):assert all(d['requested']=='0' and d['available']=='0' for d in depth)
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
                expected_result='00000000' if mode in ('on','finite_on') else '80070057'
                assert all(b['result']==expected_result for b in buffers), 'Native and wrapper metadata query paths differ'
            if name=='lifetime':assert 'INTEGRATION LIFETIME RESULT failures=0' in text
            if name=='contracts':assert 'OWNERSHIP RESULT checks=' in text and 'failures=0' in text
            geometry = verify_geometry(trace, mode in ('finite_on','finite_without_ownership','motion_requested'), mode in ('finite_on','motion_requested'))
            motion_mode = verify_motion_mode(trace, mode == 'motion_requested')
            if name in ('smoke', 'capture'):
                assert geometry['scoped_geometry_records'] > 0, 'Captured draw geometry missing'
            if mode == 'finite_on' and name == 'capture':
                assert geometry['native_contract_rejection_reported'], 'Unsupported managed usage was not refused'
            reused=expected-len({h['ptr'] for h in hooks})
            if name=='lifetime':assert reused>0,'Address reuse not exercised; increase fixture rounds'
            report['cases'][f'{mode}-{name}']={'devices':expected,'destroyed':len(destroyed),'reused_device_addresses':reused, **geometry, **motion_mode}
    for name in ('capture','lifetime','contracts','auto'):
        a=(results/f'ownership-integration-off-{name}.txt').read_text()
        b=(results/f'ownership-integration-on-{name}.txt').read_text()
        # Numeric native Release results are diagnostics, not wrapper semantics.
        normalize=lambda value:[line for line in value.splitlines() if not line.startswith('OBSERVE device_release_with_child=')]
        assert normalize(a)==normalize(b),(name,'API report differs')
    assert (results/'ownership-integration-off-auto.txt').read_text()==(results/'ownership-integration-copy_depth-auto.txt').read_text(),'Auto-depth loader smoke changes app-visible outcomes'
    assert (results/'ownership-integration-off-auto.txt').read_text()==(results/'ownership-integration-scene_depth-auto.txt').read_text(),'Scene observer changes app-visible outcomes'
    for finite_mode, baseline_mode, name in (('finite_on','on','capture'), ('finite_without_ownership','off','capture'), ('motion_without_prereqs','off','capture'), ('motion_requested','off','auto')):
        assert (results/f'ownership-integration-{finite_mode}-{name}.txt').read_bytes() == (results/f'ownership-integration-{baseline_mode}-{name}.txt').read_bytes(), 'Diagnostic option wiring changes original fixture API results'
    report['limits']=['Motion option cases establish configuration gates and synthetic-executable refusal, not successful replay or temporal consumption.','Synthetic DLL integration, not gameplay validation.','Direct3D9Ex remains native and uninstrumented.','Finite enabled cases verify wiring and unsupported-input refusal; positive finite uploads have separate original-data fixtures.','Original-preserving depth-copy allocation smoke only; numerical/content behavior has a separate fixture.']
    assert build['sources']==sources() and build['binaries_at_end']==binaries(), 'Source/binary changed during verification'
    report['current_sources_and_binaries_match']=True
    (results/'ownership-integration-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
if __name__=='__main__':
    try:verify()
    except (AssertionError,KeyError,OSError,ValueError) as exc:
        (results/'ownership-integration-verification.json').write_text(json.dumps({'result':'FAIL','reason':str(exc)},indent=2)+'\n')
        raise
