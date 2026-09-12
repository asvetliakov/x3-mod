#!/usr/bin/env python3
"""Verify telemetry against the operations of telemetry_fixture.cpp."""
from collections import Counter
import json
from pathlib import Path
import sys
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tools/analysis'))
from summarize_capture import fields
from summarize_telemetry import summarize
results=bottle.results_dir(root)
outputs=[(results/f'telemetry-{mode}.txt').read_text() for mode in ('baseline','off','on')]
assert len(set(outputs))==1,'Telemetry changed the fixture API results or readback pixels'
assert 'TELEMETRY FIXTURE: 0 failures' in outputs[0]
off=(results/'telemetry-off-capture.log').read_text()
assert not any(line.startswith(('telemetry_','loading_trace')) for line in off.splitlines()),'Opt-out emitted telemetry'
on=(results/'telemetry-on-capture.log').read_text()
parsed=summarize(on)
assert not parsed['rejected'],parsed['rejected']
metrics=Counter();failures=Counter();byte_counts=Counter();events=[];cursor_positions=0;suppressed=0
lines=on.splitlines();first_present=next(i for i,line in enumerate(lines) if line.startswith('telemetry_first_present '))
assert any(line.startswith('telemetry_summary device=1 ') and 'reason=interval' in line for line in lines[:first_present]),'Loading interval only reported after Present'
for line in lines:
    f=fields(line)
    if line.startswith('telemetry_metric '):
        count=int(f['count']);buckets=[int(v) for v in f['buckets'].split(',')]
        assert sum(buckets)==count
        assert 0<=float(f['min_us'])<=float(f['max_us'])<=float(f['total_us'])+0.001
        if f['device']=='1':
            metrics[f['name']]+=count;failures[f['name']]+=int(f['failures']);byte_counts[f['name']]+=int(f['bytes'])
    elif line.startswith('capture_event '):
        events.append(f)
    elif line.startswith('telemetry_cursor_api ') and f.get('op')=='position':cursor_positions+=1
    elif line.startswith('telemetry_summary '):suppressed+=int(f['position_suppressed'])
expected={'texture':3,'cube_texture':1,'volume_texture':1,'render_target':1,'depth_stencil':1,'vertex_buffer':1,'index_buffer':1,'shader_ps_backend':1,'reset':1,'cursor_properties':1,'cursor_position':300,'cursor_show':3,'present_normal':2,'present_capture':1,'frame_capture':1,'snapshot':1,'draw_backend':1}
for name,count in expected.items():assert metrics[name]==count,(name,metrics[name],count)
assert failures['texture']==1 and sum(failures.values())==1
assert byte_counts['vertex_buffer']==64 and byte_counts['index_buffer']==12
assert metrics['frame_normal']==0,'Reset gap leaked into normal frame intervals'
assert cursor_positions==1 and suppressed==299,(cursor_positions,suppressed)
assert [f['op'] for f in events]==['set_rt','set_depth','clear','clear','set_depth','set_rt','stretch_rect','set_rt','draw_begin'],events
assert [int(f['seq']) for f in events]==list(range(1,len(events)+1))
assert all(f['device']=='1' and f['frame']=='1' and f['after_draw']=='0' and int(f['qpc'])>0 for f in events)
assert int(events[5]['result'],16)&0x80000000,'Failed RT bind lost result'
assert any('telemetry_window_context ' in line and 'gui_focus=' in line and 'clip=' in line for line in lines)
assert any('reset_count=1' in line for line in lines if line.startswith('telemetry_first_present '))
report=dict(result='PASS',checks=['baseline/off/on API and readback equivalence','telemetry off emits no telemetry','loading summary before Present','bounded operation counts and failure counts','cursor position rate limit','capture event order/result/context','capture interval separation and reset gap isolation','SDK vtable slot compilation'],metrics=dict(metrics),capture_events=len(events),cursor_position_events=cursor_positions,cursor_positions_suppressed=suppressed)
(results/'telemetry-verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
