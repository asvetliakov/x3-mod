#!/usr/bin/env python3
"""Check capture cleanup and legacy semantics through the actual DLL."""
from collections import Counter
from pathlib import Path
import json
import re
import sys
root=Path(__file__).resolve().parents[2];results=root/'verification/results'
sys.path.insert(0,str(root/'tools/analysis'));sys.path.insert(0,str(root/'verification/probe'))
from summarize_capture import fields
from verify_capture_state import verify as verify_capture

def verify():
    build=json.loads((results/'ownership-integration-build.json').read_text())
    assert len(build['cases'])==12 and all(c['exit']==0 for c in build['cases'].values()),'Incomplete integration run'
    report={'result':'PASS','dll_sha256':build['dll_sha256'],'cases':{}}
    for mode in ('off','on','depth_only','sample_depth'):
        names=('smoke',) if mode=='depth_only' else ('auto',) if mode=='sample_depth' else ('smoke','capture','lifetime','contracts','auto')
        for name in names:
            prefix=f'ownership-integration-{mode}-{name}'
            text=(results/(prefix+'.txt')).read_text();trace=(results/(prefix+'-capture.log')).read_text()
            assert 'FAIL' not in text,(mode,name)
            hooks=[fields(line) for line in trace.splitlines() if line.startswith('device_hooked ')]
            destroyed=[fields(line) for line in trace.splitlines() if line.startswith('device_destroy ')]
            expected={'smoke':2,'capture':2,'lifetime':16,'contracts':3,'auto':1}[name]
            assert len(hooks)==len(destroyed)==expected,(mode,name,len(hooks),len(destroyed))
            assert {h['device'] for h in hooks}=={h['device'] for h in destroyed}
            assert len({h['device'] for h in hooks})==expected
            wrapped=[line for line in trace.splitlines() if line.startswith('ownership_factory mode=wrapped ')]
            if mode in ('on','sample_depth'):assert len(wrapped)==(16 if name=='lifetime' else 1),(mode,name,wrapped)
            else:assert not wrapped,(mode,name)
            assert 'mode=native_fallback' not in trace,(mode,name)
            if mode=='depth_only':assert 'requested=0 sampleable_depth_requested=1 sampleable_depth_enabled=0' in trace
            depth=[fields(line) for line in trace.splitlines() if line.startswith('ownership_depth ')]
            if mode in ('on','sample_depth'):
                assert len([d for d in depth if d['phase']=='create_after'])==expected
                if mode=='on':assert all(d['requested']=='0' and d['available']=='0' for d in depth)
                else:assert all(d['requested']=='1' for d in depth)
                if name=='auto':assert len([d for d in depth if d['phase']=='reset_after'])==1
            else:assert not depth
            if name in ('smoke','capture'):
                states={int(fields(line)['id']) for line in trace.splitlines() if line.startswith('state ')}
                assert {52,53,54,55,56,57,58,59,185,186,187,188,189}<=states,'Missing stencil capture state'
            if name=='capture':verify_capture(trace)
            if name=='lifetime':assert 'INTEGRATION LIFETIME RESULT failures=0' in text
            if name=='contracts':assert 'OWNERSHIP RESULT checks=' in text and 'failures=0' in text
            reused=expected-len({h['ptr'] for h in hooks})
            if name=='lifetime':assert reused>0,'Address reuse not exercised; increase fixture rounds'
            report['cases'][f'{mode}-{name}']={'devices':expected,'destroyed':len(destroyed),'reused_device_addresses':reused}
    for name in ('capture','lifetime','contracts','auto'):
        a=(results/f'ownership-integration-off-{name}.txt').read_text()
        b=(results/f'ownership-integration-on-{name}.txt').read_text()
        # Numeric native Release results are diagnostics, not wrapper semantics.
        normalize=lambda value:[line for line in value.splitlines() if not line.startswith('OBSERVE device_release_with_child=')]
        assert normalize(a)==normalize(b),(name,'API report differs')
    assert (results/'ownership-integration-off-auto.txt').read_text()==(results/'ownership-integration-sample_depth-auto.txt').read_text(),'Auto-depth loader smoke changes app-visible outcomes'
    report['limits']=['Synthetic DLL integration, not gameplay validation.','Direct3D9Ex remains native and uninstrumented.','Sampleable-depth loader wiring smoke only; numerical/content behavior has a separate fixture.']
    (results/'ownership-integration-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
if __name__=='__main__':
    try:verify()
    except (AssertionError,KeyError,OSError,ValueError) as exc:
        (results/'ownership-integration-verification.json').write_text(json.dumps({'result':'FAIL','reason':str(exc)},indent=2)+'\n')
        raise
