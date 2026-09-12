#!/usr/bin/env python3
"""Derive compact event fixtures and replay the real portable C++ selector.

Only state descriptors, hashes and ordering are retained: no bytecode/vertices.
0.3 omitted clear-time viewport/MRT absence results; 0.4 logs record the clear
viewport but still no extra-render-target getter results. Strict replay preserves
each uncertainty; the explicit-assumption replay supplements only what the
fixture lacks, for a conditional replay. --baseline-header replays the same
events through an older copy of the selector header and reports, per frame, the
previous decision next to the current one.
"""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from summarize_capture import summarize

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/renderer/scene_boundary.h'
HARNESS = ROOT / 'verification/probe/scene_boundary_replay.cpp'
KINDS = dict(clear=0, draw_begin=1, set_rt=2, set_depth=3, stretch_rect=4, color_fill=6)
KIND_NAMES = {0: 'Clear', 1: 'Draw', 2: 'SetRenderTarget', 3: 'SetDepth', 4: 'StretchRect', 5: 'Unsupported', 6: 'ColorFill'}
STATES = ['AwaitInitialClear', 'Background', 'Scene', 'AwaitCopy', 'AwaitBloomTarget', 'AwaitBloomDraw',
          'AwaitDepthRebind', 'AwaitFinalClear', 'Selected', 'Rejected']
REJECTIONS = ['None', 'InvalidFrame', 'Invalidated', 'Sequence', 'FailedCall', 'Pattern']
SURFACE_FIELDS = ('known', 'identity', 'container', 'width', 'height', 'format', 'msaa')
VIEWPORT_ASSUMPTION = 'Full clear-time viewport (0.3 did not query it).'
MRT_ASSUMPTION = 'No additional MRTs (captures omit failed/absent RT getter records).'
# Record prefixes the derivation reads. Gameplay logs are hundreds of megabytes,
# so the trace is streamed and everything else (constants, samplers, ...) dropped.
RELEVANT = ('capture_event ', 'clear ', 'clear_rect ', 'clear_rects ', 'clear_viewport ', 'surface ', 'set_rt ',
            'set_depth ', 'stretch_rect ', 'stretch_source_rect ', 'stretch_dest_rect ', 'color_fill ', 'draw ',
            'draw_result ', 'viewport ', 'frame_begin ', 'frame_end ', 'shader ', 'state id=7 ', 'state id=14 ',
            'texture stage=0 ')


def good(result):
    return result is not None and not int(result, 16) & 0x80000000


def read_trace(path):
    """Stream a capture log; return the relevant records and the raw SHA-256."""
    digest = hashlib.sha256()
    kept = []
    with path.open('rb') as handle:
        for raw in handle:
            digest.update(raw)
            line = raw.decode('utf-8', 'replace').rstrip('\r\n')
            if line.startswith(RELEVANT):
                kept.append(line)
    return '\n'.join(kept), digest.hexdigest()


def derive(raw, name, sha256=None):
    summary = summarize(raw, {})
    result = dict(schema=1, source=dict(name=name, sha256=sha256 or hashlib.sha256(raw.encode()).hexdigest()),
                  assumptions=[MRT_ASSUMPTION], surfaces={}, event_types=[], frames=[])
    interned = {}
    clears_without_viewport = 0
    def surface(s):
        if not s: return None
        key = s.get('identity', '0')
        value = {k: int(s.get(k, '0')) for k in SURFACE_FIELDS if k != 'known'}
        value['known'] = (key == '0' or all(k in s for k in ('width','height','format','msaa')))
        existing = result['surfaces'].get(key)
        if existing is not None and existing != value:
            raise ValueError('Conflicting lifetime descriptor: ' + key)
        result['surfaces'][key] = value
        return key
    for key, frame in summary['frames'].items():
        if not (frame['complete'] and frame.get('draw_count_matches') and
                frame.get('event_sequence_contiguous') and good(frame.get('present_result'))):
            raise ValueError('Incomplete/invalid frame ' + key)
        draws = {d['index']: d for d in frame['draws']}
        events=[]; after_draw=[]; bound_depth = None
        for e in frame['events']:
            op=e['op']; out=dict(kind=KINDS.get(op,5),result_known='result' in e,
                                 result=int(e.get('result','80004005'),16))
            details=e['details']
            def detail(event):return next((d for d in details if d['event']==event),{})
            def role(role):return next((d for d in details if d['event']=='surface' and d.get('role')==role),{})
            if op=='draw_begin':
                d=draws[int(e['after_draw'])+1]; states=d['states']; outcome=d.get('draw_result',{})
                target=next((s for s in d['targets'] if s['role']=='rt0'),{})
                depth=d.get('depth',{})
                out.update(result_known='result' in outcome,result=int(outcome.get('result','80004005'),16),
                           rt=surface(target),depth=surface(depth) if depth else bound_depth,
                           only_rt0=False,observed_only_rt0=len(d['targets'])==1,
                           vs=int(d['vs'],16),ps=int(d['ps'],16),topology=d['topology'],primitives=d['primitives'],
                           z_enable=states.get('7',0),z_write=states.get('14',0),
                           draw_state_known=all(s in states for s in ('7','14')) and bool(d['vs'] and d['ps']))
                tex=next((t for t in d.get('texture',[]) if t['stage']=='0'),{})
                out['texture0']=int(tex.get('identity','0'))
                vp=d.get('viewport',{})
                out['viewport']=[bool(vp),*[int(vp.get(k,0)) for k in ('x','y','w','h')],
                                 float(vp.get('minz',0)),float(vp.get('maxz',1))]
            elif op=='clear':
                c=detail('clear');out.update(rt=surface(role('clear_rt0')),depth=surface(role('clear_depth')),
                    clear_flags=int(c.get('flags',0)),clear_z=float(c.get('z',0)),rect_count=int(c.get('rect_count',0)),
                    only_rt0=False,observed_only_rt0=True)
                vp=detail('clear_viewport')
                if vp and good(vp.get('result')):
                    out['viewport']=[True,*[int(vp.get(k,0)) for k in ('x','y','w','h')],
                                     float(vp.get('minz',0)),float(vp.get('maxz',1))]
                else: clears_without_viewport += 1
                bound_depth=out['depth']
            elif op=='set_rt':out.update(rt=surface(role('binding')),rt_index=int(detail('set_rt').get('index',0)))
            elif op=='set_depth':
                out['depth']=surface(role('depth_binding'))
                if good(e.get('result')):bound_depth=out['depth']
            elif op=='color_fill':
                out.update(destination=surface(role('color_fill_target')),
                           destination_rect_null=detail('color_fill').get('rect_null')=='1')
            elif op=='stretch_rect':
                c=detail('stretch_rect');out.update(source=surface(role('stretch_source')),destination=surface(role('stretch_dest')),
                    source_rect_null=c.get('source_rect_null')=='1',destination_rect_null=c.get('dest_rect_null')=='1')
            encoded=json.dumps(out,sort_keys=True)
            if encoded not in interned:
                interned[encoded]=len(result['event_types']);result['event_types'].append(out)
            events.append(interned[encoded]);after_draw.append(int(e['after_draw']))
        device,frame_index=map(int,key.split(':'))
        result['frames'].append(dict(device=device,generation=1,frame=frame_index,draws=len(draws),
                                     event_types=events,after_draw=after_draw))
    if clears_without_viewport: result['assumptions'].insert(0, VIEWPORT_ASSUMPTION)
    return result


def expand(fixture, frame, assume=False):
    events=[]
    for sequence, index in enumerate(frame['event_types'],1):
        e=deepcopy(fixture['event_types'][index]);e['sequence']=sequence  # callers mutate nested lists
        for role in ('rt','depth','source','destination'):
            e[role]=dict(fixture['surfaces'].get(e.get(role),{}))
        if assume:
            e['only_rt0']=e.get('observed_only_rt0',False)
            if e['kind']==0 and not e.get('viewport',[False])[0]:
                e['viewport']=[True,0,0,e['rt'].get('width',0),e['rt'].get('height',0),0,1]
        events.append(e)
    return events


def encode(e):
    values=[e.get(k,0) for k in ('kind','sequence','result_known','result')]
    def surf(key):return [e.get(key,{}).get(k,0) for k in SURFACE_FIELDS]
    values+=surf('rt')+surf('depth')+e.get('viewport',[False,0,0,0,0,0,1])
    values += [e.get(k,0) for k in ('only_rt0','rt_index','vs','ps','texture0','draw_state_known',
                                    'topology','primitives','z_enable','z_write','clear_flags','rect_count','clear_z')]
    values += surf('source')+surf('destination')+[e.get('source_rect_null',False),e.get('destination_rect_null',False)]
    return 'E '+' '.join(str(int(v)) if isinstance(v,bool) else str(v) for v in values)


def build(destination, header=None):
    """Compile the harness against the production header, or against another
    copy of it (a baseline) laid out under the same relative include path."""
    harness = HARNESS
    if header is not None:
        tree = Path(destination).parent / ('tree-' + Path(destination).name)
        (tree / 'src/renderer').mkdir(parents=True, exist_ok=True)
        (tree / 'verification/probe').mkdir(parents=True, exist_ok=True)
        shutil.copyfile(header, tree / 'src/renderer/scene_boundary.h')
        harness = tree / 'verification/probe/scene_boundary_replay.cpp'
        shutil.copyfile(HARNESS, harness)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(harness),'-o',str(destination)],check=True)


def replay(executable, frame, events, extra_commands=None, signature_profile=None):
    lines=[]
    if signature_profile is not None:
        lines.append('P '+' '.join(str(value) for pair in signature_profile for value in pair))
    lines.append(f"B {frame['device']} {frame['generation']} {frame['frame']}")
    for index,e in enumerate(events):
        lines.extend((extra_commands or {}).get(index,[]));lines.append(encode(e))
    lines.append('Q')
    p=subprocess.run([str(executable)],input='\n'.join(lines)+'\n',capture_output=True,text=True,check=True)
    selections=[];transitions=[]
    for line in p.stdout.splitlines():
        words=line.split()
        if words[0]=='S':selections.append(dict(zip(('sequence','candidate','confirmed','color','depth','epoch'),map(int,words[1:]))))
        elif words[0]=='T':transitions.append(dict(zip(('sequence','before','after'),map(int,words[1:]))))
        elif words[0]=='Q':state,rejection,rejection_sequence=map(int,words[1:])
    return dict(selections=selections,state=state,rejection=rejection,rejection_sequence=rejection_sequence,
                transitions=transitions)


def describe(outcome, frame, events):
    """Human-readable decision: the confirmed boundary, or the phase and event
    that rejected the frame."""
    confirmed=[s for s in outcome['selections'] if s['confirmed']]
    if outcome['state']==8 and confirmed:
        s=confirmed[0]
        return dict(decision='selected',sequence=s['sequence'],after_draw=frame['after_draw'][s['sequence']-1],
                    depth_epoch=s['epoch'])
    if outcome['state']!=9:
        return dict(decision='incomplete',state=STATES[outcome['state']])
    sequence=outcome['rejection_sequence']
    phase=0
    for t in outcome['transitions']:
        if t['sequence']<sequence or (t['sequence']==sequence and t['after']!=9): phase=t['after']
    event=events[sequence-1] if 0<sequence<=len(events) else {}
    text=dict(decision='rejected',sequence=sequence,phase=STATES[phase],reason=REJECTIONS[outcome['rejection']],
              event=KIND_NAMES.get(event.get('kind'),'?'),after_draw=frame['after_draw'][sequence-1] if sequence else 0)
    if event.get('kind')==1: text.update(vs=f"{event.get('vs',0):016x}",ps=f"{event.get('ps',0):016x}")
    if event.get('kind')==0: text.update(clear_flags=event.get('clear_flags'))
    return text


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--trace',type=Path);p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--baseline-header',type=Path,help='older scene_boundary.h to replay for a previous-vs-new comparison')
    args=p.parse_args()
    if args.trace:
        raw,sha=read_trace(args.trace)
        fixture=derive(raw,args.trace.name,sha)
        args.fixture.write_text(json.dumps(fixture,indent=1)+'\n')
    else:fixture=json.loads(args.fixture.read_text())
    report=dict(source=fixture['source'],assumptions=fixture['assumptions'],frames=[])
    with tempfile.TemporaryDirectory() as temp:
        executable=Path(temp)/'replay';build(executable)
        baseline=None
        if args.baseline_header:
            baseline=Path(temp)/'baseline';build(baseline,args.baseline_header)
        changed=0
        for frame in fixture['frames']:
            strict=replay(executable,frame,expand(fixture,frame,False))
            events=expand(fixture,frame,True)
            conditional=replay(executable,frame,events)
            for selection in conditional['selections']:
                selection['after_draw']=frame['after_draw'][selection['sequence']-1]
            entry=dict(device=frame['device'],frame=frame['frame'],draws=frame['draws'],
                       observed_only=strict,with_explicit_assumptions=conditional,
                       decision=describe(conditional,frame,events))
            if baseline:
                previous=replay(baseline,frame,events)
                entry['previous_decision']=describe(previous,frame,events)
                entry['changed']=entry['previous_decision']!=entry['decision']
                changed+=entry['changed']
            report['frames'].append(entry)
            line=f"frame {frame['device']}:{frame['frame']} draws={frame['draws']} new={entry['decision']}"
            if baseline: line+=f" previous={entry['previous_decision']}"
            print(line)
    decisions=[f['decision']['decision'] for f in report['frames']]
    report['summary']=dict(frames=len(decisions),selected=decisions.count('selected'),rejected=decisions.count('rejected'),
                           incomplete=decisions.count('incomplete'))
    if baseline:
        previous=[f['previous_decision']['decision'] for f in report['frames']]
        report['summary'].update(previous_selected=previous.count('selected'),previous_rejected=previous.count('rejected'),
                                 changed=changed,baseline_header_sha256=hashlib.sha256(args.baseline_header.read_bytes()).hexdigest())
    def label(path):  # Repo-relative when inside the tree; a fixture elsewhere keeps its absolute path.
        try: return str(path.relative_to(ROOT))
        except ValueError: return str(path)
    report['sources_sha256']={label(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in
        (HEADER,HARNESS,Path(__file__).resolve(),args.fixture.resolve())}
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(f"Replayed {len(report['frames'])} frames: {report['summary']}; wrote {args.output}")

if __name__=='__main__':main()
