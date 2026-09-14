#!/usr/bin/env python3
"""Verify ownership fixture outcomes without treating expired addresses as identity."""
from pathlib import Path
from collections import Counter
import json
import hashlib
import re
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
root=Path(__file__).resolve().parents[2];results=bottle.results_dir(root)
def verify():
    (results/'ownership-verification.json').write_text(json.dumps({'result':'RUNNING'})+'\n')
    manifest=json.loads((results/'ownership-build-verification.json').read_text())
    assert manifest.get('passed') and manifest.get('fresh_build') and manifest.get('phase')=='complete','Fresh ownership build/run did not complete'
    assert manifest['sources_before_build']==manifest['sources_after_build']==manifest['sources'],'Source changed during ownership verification'
    assert manifest['binaries_before']==manifest['binaries_after'],'Build products changed during ownership verification'
    for name,expected in manifest['sources'].items():
        assert hashlib.sha256((root/name).read_bytes()).hexdigest()==expected,'Source differs from verified build: '+name
    assert set(manifest['fixtures'])==set(manifest['binaries_before'])=={'baseline','wrapped'},'Incomplete fixture/binary maps'
    for mode,entry in manifest['fixtures'].items():
        assert entry['exe_sha256']==manifest['binaries_before'][mode],'Fixture executable differs from pre/post build maps'
        binary=root/'verification/probe/build'/f'ownership_{mode}.exe'
        assert hashlib.sha256(binary.read_bytes()).hexdigest()==entry['exe_sha256'],'Current build executable differs from verified run: '+mode
        assert hashlib.sha256((results/f'ownership-{mode}.txt').read_bytes()).hexdigest()==entry['report_sha256'],'Report does not match run manifest'
    assert set(manifest['fixtures'])=={'baseline','wrapped'} and all(item['exit']==0 for item in manifest['fixtures'].values()),'Incomplete or failed fixture run'
    reports={mode:(results/f'ownership-{mode}.txt').read_text() for mode in ('baseline','wrapped')}
    summary={}
    for mode,trace in reports.items():
        assert not re.search(r'^CHECK .* FAIL$',trace,re.M),mode
        terminal=trace.rstrip().splitlines()[-1] if trace.strip() else ''
        endings=[line for line in trace.splitlines() if line.startswith('OWNERSHIP RESULT ')]
        end=re.fullmatch(r'OWNERSHIP RESULT checks=(\d+) failures=(\d+)',terminal) if endings==[terminal] else None
        check_lines=[line for line in trace.splitlines() if line.startswith('CHECK ')]
        assert end and int(end[2])==0 and int(end[1])==len(check_lines)=={'baseline':370,'wrapped':553}[mode],mode+' incomplete check inventory'
        assert all(line.endswith(' PASS') for line in check_lines),mode+' nonpassing check'
        summary[mode]={'checks':int(end[1]),'failures':int(end[2])}
    # These are actual backend contracts, including Preview's additional chain
    # enumeration behavior. Native reference-count magnitudes are diagnostic only.
    def observations(trace):
        return [line for line in trace.splitlines() if line.startswith('OBSERVE ') and 'device_release_with_child=' not in line]
    baseline_observations=observations(reports['baseline']);wrapped_observations=observations(reports['wrapped'])
    assert baseline_observations==wrapped_observations, 'Backend observation mismatch: '+repr([(a,b) for a,b in zip(baseline_observations,wrapped_observations) if a!=b])
    base_results=Counter(line for line in reports['baseline'].splitlines() if line.startswith('RESULT '))
    wrapped_results=Counter(line for line in reports['wrapped'].splitlines() if line.startswith('RESULT '))
    assert not (base_results-wrapped_results),'Wrapper changed API HRESULT outcomes'
    required=['scanned prefix bound','nested lock stays invalid','final release erases the record','reset clears the records','relearned after reset',
              'renderer history released before failed reset','renderer history released before successful reset','renderer history destroyed at final logical device release','renderer history lives while public child retains device','application wrapper rejected as renderer resource','failed factory adoption preserves native reference','native backend destroyed after final logical device release','Ex factory rejected at ownership boundary']
    for name in required:assert f'CHECK {name} PASS' in reports['wrapped'],name
    summary.update(result='PASS',backend_observations_equal=True,shared_hresult_outcomes_equal=True,limits=['Canonical identity checked while references overlap; an external-zero wrapper may be re-created.','No application or native refcount magnitude used as a production ownership rule.','Synthetic single-threaded fixture only; not integrated with installed game proxy.'])
    (results/'ownership-verification.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary))

if __name__=='__main__':
    try:
        verify()
    except (AssertionError,KeyError,OSError,ValueError) as exc:
        (results/'ownership-verification.json').write_text(json.dumps({'result':'FAIL','reason':str(exc)},indent=2)+'\n')
        raise
