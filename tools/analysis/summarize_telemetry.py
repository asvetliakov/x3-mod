#!/usr/bin/env python3
"""Summarize optional CPU diagnostics; never add overlapping API spans together.

Frame intervals include pacing and application work. Backend call durations are
wall-clock spans, not GPU timings. Captured intervals are reported separately.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
from summarize_capture import fields


def number(f, name):
    value=float(f[name])
    if not math.isfinite(value) or value < 0:
        raise ValueError('invalid nonnegative duration: '+name)
    return value


def summarize(trace):
    result=dict(clock=None, metrics={}, spans=[], markers=[], first_presents=[],
                event_counts={}, loading_events=[], loading_metrics={}, rejected=[],
                limits=['CPU-side wall-clock spans, not GPU execution time.',
                        'Metrics can overlap or nest; do not sum totals across names or devices.',
                        'Captured frame intervals include diagnostic disturbance.',
                        'Observed post-initialization hooks do not cover the whole process.',
                        'Summary windows flush on observed calls; no events does not prove idle time.',
                        'Loading deltas use per-field atomics; concurrent calls may straddle adjacent reports.'])
    counts=Counter()
    for line_number,line in enumerate(trace.splitlines(),1):
        event=line.partition(' ')[0]
        if not event.startswith(('telemetry_','loading_','capture_event')):
            continue
        f=fields(line);counts[event]+=1
        try:
            if event=='telemetry_start':
                freq=int(f['qpc_frequency'])
                if freq<=0: raise ValueError('invalid frequency')
                result['clock']=dict(frequency=freq, anchor=f.get('anchor'), start_qpc=int(f['qpc']))
            elif event=='telemetry_metric':
                count=int(f['count']);failures=int(f['failures']);buckets=[int(x) for x in f['buckets'].split(',')]
                if count<=0 or not 0<=failures<=count or len(buckets)!=6 or min(buckets)<0 or sum(buckets)!=count:
                    raise ValueError('inconsistent metric counts')
                total,minimum,maximum=(number(f,k) for k in ('total_us','min_us','max_us'))
                if minimum>maximum or total+0.001 < maximum:
                    raise ValueError('inconsistent duration range')
                byte_count=int(f['bytes'])
                if byte_count<0: raise ValueError('negative byte count')
                key=f['device']+':'+f['name']
                aggregate=result['metrics'].setdefault(key,dict(device=f['device'],name=f['name'],count=0,failures=0,total_us=0,min_us=minimum,max_us=0,bytes=0,buckets=[0]*6,windows=0))
                aggregate['count']+=count;aggregate['failures']+=failures;aggregate['total_us']+=total
                aggregate['min_us']=min(aggregate['min_us'],minimum);aggregate['max_us']=max(aggregate['max_us'],maximum)
                aggregate['bytes']+=byte_count;aggregate['windows']+=1
                aggregate['buckets']=[a+b for a,b in zip(aggregate['buckets'],buckets)]
            elif event=='telemetry_span':
                begin,end=int(f['qpc_begin']),int(f['qpc_end'])
                if end<begin: raise ValueError('reversed QPC span')
                item=dict(f)
                if result['clock']: item['duration_us']=(end-begin)*1000000/result['clock']['frequency']
                result['spans'].append(item)
            elif event=='telemetry_first_present':
                result['first_presents'].append(f)
            elif event in ('telemetry_marker','telemetry_phase_marker'):
                result['markers'].append(f)
            elif event=='loading_metric':
                names=('count','failures','pending','ambiguous','bytes','inclusive_ticks','exclusive_ticks','max_ticks','wrapper_tail_ticks')
                sample={k:int(f[k]) for k in names}
                if min(sample.values())<0: raise ValueError('negative loading metric')
                aggregate=result['loading_metrics'].setdefault(f['op'],dict.fromkeys(names,0))
                for k,value in sample.items():
                    aggregate[k]=max(aggregate[k],value) if k=='max_ticks' else aggregate[k]+value
            elif event.startswith('loading_'):
                if event=='loading_trace' and 'frequency' in f and int(f['frequency'])<=0:
                    raise ValueError('invalid loading clock frequency')
                result['loading_events'].append(dict(event=event,**f))
        except (KeyError,ValueError) as exc:
            result['rejected'].append(dict(line=line_number,event=event,reason=str(exc)))
    for metric in result['metrics'].values():
        metric['mean_us']=metric['total_us']/metric['count']
    coverage=[e for e in result['loading_events'] if e['event']=='loading_trace' and 'frequency' in e]
    if coverage:
        frequency=int(coverage[-1]['frequency'])
        if frequency>0:
            for metric in result['loading_metrics'].values():
                for name in ('inclusive','exclusive','max','wrapper_tail'):
                    metric[name+'_us']=metric[name+'_ticks']*1000000/frequency
    result['event_counts']=dict(counts)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace',type=Path);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();raw=args.trace.read_bytes();report=summarize(raw.decode())
    report['source']=dict(trace=args.trace.name,sha256=hashlib.sha256(raw).hexdigest())
    args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(f"{len(report['metrics'])} metric series; {len(report['rejected'])} rejected records -> {args.output}")


if __name__=='__main__': main()
