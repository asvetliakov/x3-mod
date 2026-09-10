#!/usr/bin/env python3
"""Extract verified copies/clears and the observed X3 bloom chain from schema 2.

This is offline evidence for the captured executable/effect hashes, not a generic
HUD classifier. Draw indices locate events; they are never hardcoded boundaries.
"""
import argparse
import hashlib
import json
from pathlib import Path
from summarize_capture import summarize

BLOOM_PAIRS = [
    ('cbbf26102694c961','1c90e79667bdaddf'),
    ('6059306306203243','f3172baa8dd19a40'),
    ('6059306306203243','241c3fa33270f58e'),
    ('1279d081455f5815','ff6eed5a5ddf3a3a')]


def good(result):
    return result is not None and not int(result,16)&0x80000000


def detail(event,kind):
    return next((d for d in event['details'] if d['event']==kind),{})


def surface(event,role):
    return next((d for d in event['details'] if d['event']=='surface' and d.get('role')==role),{})


def compact_surface(s):
    return {k:s[k] for k in ('identity','width','height','format','msaa','container') if k in s}


def inspect_frame(frame):
    ds=frame['draws'];events=frame.get('events',[])
    valid=(frame['complete'] and frame.get('draw_count_matches') and good(frame.get('present_result'))
           and frame.get('event_sequence_contiguous') and all(good(d.get('draw_result',{}).get('result')) for d in ds))
    report=dict(valid=bool(valid),draws=len(ds),events=len(events),clears=[],copies=[],bloom_chains=[])
    if not valid: return report
    for e in events:
        if not good(e.get('result')): continue
        if e['op']=='clear':
            args=detail(e,'clear')
            report['clears'].append(dict(after_draw=int(e['after_draw']),seq=int(e['seq']),
                flags=int(args['flags']),depth_value=float(args['z']),rect_count=int(args['rect_count']),
                target=compact_surface(surface(e,'clear_rt0')),depth=compact_surface(surface(e,'clear_depth'))))
        elif e['op']=='stretch_rect':
            args=detail(e,'stretch_rect')
            report['copies'].append(dict(after_draw=int(e['after_draw']),seq=int(e['seq']),
                source=compact_surface(surface(e,'stretch_source')),destination=compact_surface(surface(e,'stretch_dest')),
                filter=int(args['filter']),full_rects=args['source_rect_null']=='1' and args['dest_rect_null']=='1'))
    pairs=[(d['vs'],d['ps']) for d in ds]
    for first in range(len(ds)-3):
        if pairs[first:first+4]!=BLOOM_PAIRS: continue
        chain=ds[first:first+4]
        targets=[next((t for t in d['targets'] if t['role']=='rt0'),{}) for d in chain]
        if not all(t.get('identity','0')!='0' for t in targets): continue
        # A/B/A/main target order, with full-size source copy just before the chain.
        a,b,again,main=targets
        if a['identity']!=again['identity'] or len({a['identity'],b['identity'],main['identity']})!=3: continue
        if not all(t.get('width')==a.get('width') and t.get('height')==a.get('height') for t in (b,again)): continue
        previous=[c for c in report['copies'] if c['after_draw']==chain[0]['index']-1 and c['source'].get('identity')==main['identity'] and c['full_rects']]
        if len(previous)!=1: continue
        copied=previous[0]
        parent=copied['destination'].get('container','0')
        if parent=='0' or not any(t.get('identity')==parent for t in chain[0].get('texture',[])): continue
        after=[c for c in report['clears'] if c['after_draw']>=chain[-1]['index'] and c['flags']&2]
        report['bloom_chains'].append(dict(draw_indices=[d['index'] for d in chain],
            scene_copy=copied,targets=[compact_surface(t) for t in targets],
            next_depth_clear=after[0] if after else None,
            trailing_draws=len(ds)-first-4,
            trailing_depth_enabled=sum(d['states'].get('7',0)!=0 for d in ds[first+4:])))
    return report


def analyze(trace):
    summary=summarize(trace,{})
    return dict(frames={key:inspect_frame(frame) for key,frame in summary['frames'].items()},
        limits=['Recognizes exact observed bloom shader pairs and resource flow only.',
                'Depth clears invalidate content while allocation identities remain unchanged.',
                'A post-bloom draw is not proven to be HUD by its ordering or pixel shader alone.'])


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();raw=args.trace.read_bytes();report=analyze(raw.decode())
    report['source']=dict(trace=args.trace.name,sha256=hashlib.sha256(raw).hexdigest())
    args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(f"{len(report['frames'])} frames; {sum(len(f['bloom_chains']) for f in report['frames'].values())} resource-validated bloom chains")

if __name__=='__main__': main()
