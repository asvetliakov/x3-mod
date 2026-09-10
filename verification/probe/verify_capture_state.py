#!/usr/bin/env python3
"""Check actual capture output against the synthetic fixture's API operations.

This verifies stateblock-restored live bindings, typed zero/nonzero constants,
resource lifetimes, surface/texture parent relationships and failed draw outcomes.
It deliberately does not infer scene-object identity from storage identity.
"""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/analysis'))
from summarize_capture import summarize


def verify(trace):
    capture = summarize(trace, {}, include_floats=True)
    frames = capture['frames']
    expected = {f'{device}:{frame}' for device in (1,2) for frame in range(1,6)}
    assert set(frames) == expected, f'Captured frames: {list(frames)}'
    all_vbs = set()
    for device in (1,2):
        initial, restored, cleared, recreated, up = [frames[f'{device}:{i}'] for i in range(1,6)]
        for frame in (initial,restored,cleared,recreated,up):
            assert frame['complete'] and frame['draw_count_matches']
            assert frame['present_result'] == '00000000'
        a,b,c,n,u = [f['draws'][0] for f in (initial,restored,cleared,recreated,up)]
        stream = lambda d: next(s for s in d['stream'] if s['slot']=='0')
        identity = stream(a)['identity']
        assert identity != '0' and stream(b)['identity'] == identity == stream(c)['identity']
        assert stream(n)['identity'] != identity and stream(n)['identity'] != '0'
        all_vbs.update((identity,stream(n)['identity']))
        assert stream(a)['offset']=='16' and stream(a)['stride']=='20' and stream(a)['frequency']=='1'
        assert a['indices']['identity'] == b['indices']['identity'] == n['indices']['identity'] != '0'
        assert a['draw_args'] == dict(base_vertex='0',min_vertex='0',num_vertices='3',start_index='0')
        assert a['texture'][0]['identity'] == b['texture'][0]['identity'] != '0'
        surface = a['targets'][0]
        assert surface['identity'] != '0' and surface['container'] != '0'
        assert capture['resources'][surface['identity']]['type'] == '1'  # SURFACE
        assert capture['resources'][surface['container']]['type'] == '3'  # TEXTURE
        for stage in ('vs','ps'):
            for d in (a,b):
                assert d['constants'][stage]['i']['0'] == [7,-2,2147483647,-2147483647]
                assert [d['constants'][stage]['b'][str(i)] for i in range(3)] == [[1],[0],[1]]
            assert c['constants'][stage]['i']['0'] == [0,0,0,0]
            assert [c['constants'][stage]['b'][str(i)] for i in range(3)] == [[0],[0],[0]]
            for typ in ('f','i','b'):
                assert a['constant_status'][stage][typ]['result'] == '00000000'
        assert a['constants']['vs']['f']['10'] == '80000000,00000000,00000000,00000000'
        assert len(cleared['draws']) == 2
        failed = cleared['draws'][1]
        assert failed['indices']['identity'] == '0'
        assert int(failed['draw_result']['result'],16) & 0x80000000
        assert a['draw_result']['result'] == '00000000'
        assert u['geometry']['source'] == 'user_memory' and 'stream' not in u and 'indices' not in u
        assert u['draw_args']['stride'] == '20' and u['draw_args']['index_format'] == '101'
    assert len(all_vbs) == 4, 'Allocation IDs must not alias across sequential devices'
    return dict(result='PASS', devices=2, frames=10, vertex_buffer_allocations=4,
                checks=['typed constants and zeros','stateblock restoration','resource identity reuse/recreation',
                        'texture surface parent identity','device lifetime frame separation',
                        'draw arguments/results','UP source isolation','signed zero'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace',type=Path)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    result=verify(args.trace.read_text())
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__': main()
