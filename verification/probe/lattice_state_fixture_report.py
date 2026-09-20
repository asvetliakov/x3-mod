#!/usr/bin/env python3
"""Validate the 12 real-helper mock packets after the owner-run EXE completes."""
import argparse
import json
from pathlib import Path
from lattice_state_packet import load
EXPECTED=['complete','complete','ambiguous','unavailable','unavailable','submission_failed',
          'submission_failed','reset','partial','ambiguous','partial','reset']


def report(directory):
    paths=list(Path(directory).glob('lattice-state-*.json'))
    if len(paths)!=len(EXPECTED):raise ValueError('expected exactly12 packet files in fresh output directory')
    packets={}
    for path in paths:
        packet=load(path)
        if packet['frame'] in packets:raise ValueError('duplicate frame')
        packets[packet['frame']]=packet
    for frame,status in enumerate(EXPECTED,1):
        packet=packets[frame]
        if packet['status']!=status:raise ValueError(f'frame{frame}: expected{status}, got{packet["status"]}')
        if status=='complete':load(next(p for p in paths if json.loads(p.read_text())['frame']==frame),True)
    # Concrete retained accepted record after nonmatching shader/declaration probes.
    if [r['draw'] for r in packets[2]['records']]!=[1,4]:raise ValueError('selector nonmatch erased an accepted record')
    for frame,kind,index in [(4,'render',195),(5,'stream_frequency',0)]:
        field=next(q for q in packets[frame]['records'][0]['fields'] if (q['kind'],q['index'])==(kind,index))
        if not int(field['hr'],16)&0x80000000 or field['words']:raise ValueError('query failure published invented values')
    return dict(result='PASS',packets=12,complete=2,refused=10,draw_input_coherence='unqualified',payload_copy_valid='not_attempted')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory');print(json.dumps(report(parser.parse_args().directory),indent=2))
