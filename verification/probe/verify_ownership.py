#!/usr/bin/env python3
"""Verify ownership fixture outcomes without treating expired addresses as identity."""
from pathlib import Path
from collections import Counter
import json
import re
root=Path(__file__).resolve().parents[2];results=root/'verification/results'
def verify():
    manifest=json.loads((results/'ownership-build-verification.json').read_text())
    assert set(manifest['fixtures'])=={'baseline','wrapped'} and all(item['exit']==0 for item in manifest['fixtures'].values()),'Incomplete or failed fixture run'
    reports={mode:(results/f'ownership-{mode}.txt').read_text() for mode in ('baseline','wrapped')}
    summary={}
    for mode,trace in reports.items():
        assert not re.search(r'^CHECK .* FAIL$',trace,re.M),mode
        end=re.search(r'^OWNERSHIP RESULT checks=(\d+) failures=(\d+)$',trace,re.M)
        assert end and int(end[2])==0,mode
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
    required=['renderer history released before failed reset','renderer history released before successful reset','renderer history destroyed at final logical device release','renderer history lives while public child retains device','application wrapper rejected as renderer resource','failed factory adoption preserves native reference','native backend destroyed after final logical device release','Ex factory rejected at ownership boundary']
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
