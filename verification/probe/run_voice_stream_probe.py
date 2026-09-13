#!/usr/bin/env python3
"""Consume the retained EXE under the root-owned Wine lock; never build.

A complete diagnostic is successful even when native construction fails. The
report distinguishes complete measurement, construction, decoded PCM and
unresolved timeout. No media/registry/game-file writes or audio playback.
"""
import argparse
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import bottle
from game_guard import game_running

MATRIX = tuple(itertools.product((144, 244), (0, 1, 2), (1, 0)))


def select_variants(values):
    if not values:
        return MATRIX
    encoded = {f'{s}:{r}:{w}': (s,r,w) for s,r,w in MATRIX}
    if len(set(values)) != len(values) or any(value not in encoded for value in values):
        raise ValueError('variants must be unique exact SOURCE:ROUTE:WRAPPER entries')
    selected = {encoded[value] for value in values}
    return tuple(row for row in MATRIX if row in selected)


def fields(line):
    return dict(token.split('=', 1) for token in line.split()[1:])


def hr(value):
    assert len(value) == 8
    return int(value, 16)


def validate(text, source, route, wrapper):
    rows = [(x.split()[0], fields(x)) for x in text.splitlines() if x.startswith('VOICE_')]
    assert rows and rows[0][0] == 'VOICE_HEADER', 'missing header'
    header = rows[0][1]
    assert {k: int(header[k]) for k in ('schema','source','wrapper','route','repeats','cues','batches','native_flags','audible')} == dict(schema=1,source=source,wrapper=wrapper,route=route,repeats=2,cues=2,batches=2,native_flags=336,audible=0)
    assert int(header['thread']) > 0
    pending = None
    seq = 0
    stages = []
    for kind, row in rows[1:]:
        if 'source' in row:
            assert (int(row['source']),int(row['route']),int(row['wrapper'])) == (source,route,wrapper)
        if kind in ('VOICE_BEGIN', 'VOICE_STAGE'):
            assert (int(row['source']),int(row['route']),int(row['wrapper'])) == (source,route,wrapper)
            assert int(row['seq']) == seq
        if kind == 'VOICE_BEGIN':
            assert pending is None
            pending = row
        elif kind == 'VOICE_STAGE':
            assert pending is not None
            for key in pending:
                assert row[key] == pending[key], (key, row, pending)
            for key in ('wall_ms', 'cpu_ms'):
                assert math.isfinite(float(row[key])) and float(row[key]) >= 0
            assert row['cpu_valid'] == '1', 'thread CPU query failed'
            assert row['required'] in ('0', '1')
            hr(row['hr'])
            stages.append(row)
            pending = None
            seq += 1
    assert pending is None, 'unterminated COM stage'
    aborts = [r for k,r in rows if k == 'VOICE_ABORT']
    if aborts:
        assert len(aborts) == 1 and aborts[0]['stage'] == 'co_initialize'
        assert hr(aborts[0]['hr']) & 0x80000000
        assert not any(k in ('VOICE_CASE','VOICE_COMPLETE') for k,r in rows)
        return dict(startup_abort=aborts[0], cases=[], stages=stages, completed=False)
    startup = [r for k,r in rows if k == 'VOICE_STARTUP']
    assert len(startup) == 1 and startup[0]['primary_play'] == '0'
    assert startup[0]['ds_present'] in ('0','1') and startup[0]['window'] in ('0','1')
    assert bool(hr(startup[0]['ds_hr']) & 0x80000000) == (startup[0]['ds_present'] == '0')
    cases = [r for k,r in rows if k == 'VOICE_CASE']
    assert [int(r['repeat']) for r in cases] == [0,1]
    for row in cases:
        repeat = int(row['repeat'])
        assert (int(row['source']),int(row['route']),int(row['wrapper'])) == (source,route,wrapper)
        assert row['constructed'] in ('0','1') and row['decoded'] in ('0','1') and row['audio_enabled'] in ('0','1')
        assert row['cleanup'] == '1'
        assert row['constructed'] == '1' or row['decoded'] == '0'
        assert math.isfinite(float(row['duration'])) and float(row['duration']) >= 0
        failed = [r for k,r in rows if k == 'VOICE_FAILURE' and int(r['repeat']) == repeat]
        if row['decoded'] == '1':
            assert not failed and row['fatal'] == 'none' and hr(row['hr']) == 0
        else:
            assert len(failed) == 1 and failed[0]['name'] == row['fatal'] and failed[0]['hr'] == row['hr']
            assert hr(row['hr']) & 0x80000000
        formats = [r for k,r in rows if k == 'VOICE_FORMAT' and int(r['repeat']) == repeat]
        assert len(formats)<=1, 'duplicate format'
        for fmt in formats:
            assert (int(fmt['source']),int(fmt['route']),int(fmt['wrapper'])) == (source,route,wrapper)
            assert all(0<=int(fmt[k])<=0xffffffff for k in ('tag','rate','channels','bits','align','avg','extra'))
        if row['constructed']=='1' and row['audio_enabled']=='1':
            assert len(formats)==1, 'constructed audio lacks negotiated format'
            fmt=formats[0]
            assert int(fmt['tag'])==1 and int(fmt['bits'])==16 and int(fmt['channels'])>0
            assert int(fmt['align'])==2*int(fmt['channels'])
            assert int(fmt['rate'])>0 and 0<int(fmt['avg'])<=1000000
            assert int(fmt['avg'])==int(fmt['rate'])*int(fmt['align'])
        row['format']=formats[0] if formats else None
        downgrades=[r for k,r in rows if k=='VOICE_DOWNGRADE' and int(r['repeat'])==repeat]
        if row['audio_enabled']=='0':
            assert len(downgrades)==1 and downgrades[0]['flags']=='344' and downgrades[0]['no_audio']=='1'
            assert hr(downgrades[0]['hr'])&0x80000000
            adds=[r for r in stages if int(r['repeat'])==repeat and r['name']=='add_audio']
            assert len(adds)==2 and all(hr(r['hr'])&0x80000000 for r in adds)
            assert row['decoded']=='0' and not formats
        else:
            assert not downgrades
        row['downgrade']=downgrades[0] if downgrades else None
        pcm = [r for k,r in rows if k == 'VOICE_PCM' and int(r['repeat']) == repeat]
        expected = [(c,b) for c in range(2) for b in range(2)]
        assert [(int(r['cue']),int(r['batch'])) for r in pcm] == expected[:len(pcm)]
        if row['decoded'] == '1':
            assert len(pcm) == 4 and sum(int(r['nonzero']) for r in pcm) > 0
        for p in pcm:
            assert int(p['requested_ms']) - int(p['seek_ms']) == 500
            assert int(p['seek_ms']) == (10000 if int(p['cue']) == 0 else 60000)
            assert formats and int(formats[0]['align'])>0
            assert 0 < int(p['actual']) <= int(formats[0]['avg'])//10 and int(p['actual']) % int(formats[0]['align']) == 0
            assert int(p['end']) > int(p['start'])
            assert 0 <= int(p['nonzero']) <= int(p['actual'])//2
            assert 0 <= int(p['peak']) <= 32768 and int(p['energy']) >= 0
        if row['constructed'] == '1':
            required = {'activate_stream','initialize','get_graph','position_qi','control_qi',
                        'probe_manual_sink_guard','stream_run','control_pause'}
            if row['audio_enabled']=='1':
                required |= {'add_audio','audio_qi_pre','set_pcm','get_audio','audio_qi_post','get_format',
                             'activate_audio_data','set_buffer_native','data_format','create_sample','create_dsound_buffer'}
            passed = {r['name'] for r in stages if int(r['repeat'])==repeat and r['phase']=='create' and not hr(r['hr'])&0x80000000}
            assert required <= passed, 'constructed claim without complete native path'
            assert 'open_file' in passed or 'render' in passed
            sinks=[r for k,r in rows if k=='VOICE_SINK' and int(r['repeat'])==repeat]
            assert len(sinks)==1 and sinks[0]['manual_only']=='1'
            assert int(sinks[0]['terminals']) in ((1,) if row['audio_enabled']=='1' else (0,1))
        row['outcome_scope'] = ('probe_boundary' if row['fatal'] in ('probe_manual_sink_guard','pcm_domain') else
                                'constructed_no_audio' if row['constructed']=='1' and row['audio_enabled']=='0' else
                                'construction_failure' if row['constructed']=='0' else
                                'decoded' if row['decoded']=='1' else 'decode_failure')
        if len(pcm)==4:
            assert int(pcm[1]['end']) > int(pcm[0]['end'])
            assert int(pcm[2]['start']) > int(pcm[1]['end']), 'second cue failed to move forward'
            assert int(pcm[3]['end']) > int(pcm[2]['end'])
        row['pcm'] = pcm
        local_stages = [r for r in stages if int(r['repeat']) == repeat]
        row['stage_wall_ms'] = sum(float(r['wall_ms']) for r in local_stages)
        row['stage_cpu_ms'] = sum(float(r['cpu_ms']) for r in local_stages)
        row['construction_wall_ms'] = sum(float(r['wall_ms']) for r in local_stages if r['phase'] == 'create' and r['name'] != 'topology')
        row['cleanup_wall_ms'] = sum(float(r['wall_ms']) for r in local_stages if r['phase'] == 'cleanup')
        row['largest_stages'] = sorted(local_stages,key=lambda r:float(r['wall_ms']),reverse=True)[:5]
    complete = [r for k,r in rows if k == 'VOICE_COMPLETE']
    assert complete == [dict(cases='2',owner_thread='1',audible='0')]
    return dict(startup=startup[0], cases=cases, stages=stages,
                filters=[r for k,r in rows if k=='VOICE_FILTER'],
                pins=[r for k,r in rows if k=='VOICE_PIN'], completed=True)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while data := stream.read(1024*1024):
            h.update(data)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--exe-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--variant', action='append', help='only this exact SOURCE:ROUTE:WRAPPER entry; repeatable')
    args = parser.parse_args()
    selected = select_variants(args.variant)
    assert bottle.BOTTLE == 'X3', 'this run is scoped to the X3 bottle'
    assert not game_running(), 'game must be closed'
    exe = args.exe.resolve()
    assert digest(exe) == args.exe_sha256, 'retained EXE changed; no implicit rebuild'
    args.output.mkdir(parents=True, exist_ok=False)
    media = {sid:bottle.game_dir() / f'addon/mov/{sid:05d}.dat' for sid in sorted({v[0] for v in selected})}
    for path in media.values():
        assert path.is_file() and path.stat().st_size > 1000000
    identities = {str(sid):dict(path=str(p),bytes=p.stat().st_size,sha256=digest(p)) for sid,p in media.items()}
    report = dict(schema=1,bottle=bottle.describe(),exe=dict(path=str(exe),sha256=args.exe_sha256),
                  media=identities,variants=[],completed=False,restored_speech=False,
                  selected_matrix=[dict(source=s,route=r,wrapper=w) for s,r,w in selected],
                  planned_cases=2*len(selected),batch_scope='full' if selected==MATRIX else 'selected_subset',
                  scope='documented-API silent construction and PCM; no game callback/audio playback qualification')
    start = time.monotonic()
    # Use an isolated current directory: do not load the game's proxy DLL or
    # create native logs next to game files. Absolute media paths remain read-only.
    for sid,route,wrapper in selected:
        if time.monotonic()-start > 240:
            report['abort'] = '240 second overall budget';break
        assert not game_running(), 'game started while diagnostic held lease'
        name=f'{sid}-route{route}-wrapper{wrapper}'
        command=[bottle.WINE,*bottle.wine_args(),str(exe),str(sid),str(wrapper),str(route),
                 'Z:'+str(media[sid]).replace('/','\\')]
        env=os.environ.copy();env['WINEDEBUG']='-all'
        # No DLL override/codec/registry changes. Native primary buffer and all
        # secondary buffers remain stopped in the EXE.
        process_start=time.monotonic()
        try:
            result=subprocess.run(command,cwd=args.output,env=env,capture_output=True,text=True,timeout=45)
        except subprocess.TimeoutExpired as exc:
            (args.output/f'{name}.stdout.txt').write_bytes(exc.stdout or b'')
            (args.output/f'{name}.stderr.txt').write_bytes(exc.stderr or b'')
            report['abort']=f'{name}: external timeout';break
        process_wall=time.monotonic()-process_start
        (args.output/f'{name}.stdout.txt').write_text(result.stdout)
        (args.output/f'{name}.stderr.txt').write_text(result.stderr)
        if result.returncode != 0:
            report['abort']=f'{name}: exit {result.returncode}; last VOICE_BEGIN identifies timeout/crash stage';break
        try:
            checked=validate(result.stdout,sid,route,wrapper)
        except (AssertionError,ValueError,KeyError) as exc:
            report['abort']=f'{name}: invalid diagnostic: {exc}';break
        report['variants'].append(dict(source=sid,route=route,wrapper=wrapper,
                                      process_wall_seconds=process_wall,
                                      outside_measured_stages_seconds=process_wall-sum(float(r['wall_ms']) for r in checked['stages'])/1000.,
                                      **checked))
        print(name,[(r['constructed'],r['decoded'],r['fatal']) for r in checked['cases']],flush=True)
    report['elapsed_seconds']=time.monotonic()-start
    report['completed']=len(report['variants'])==len(selected) and all(r['completed'] for r in report['variants'])
    report['cases']=sum(len(r['cases']) for r in report['variants'])
    report['constructed']=sum(r['constructed']=='1' for v in report['variants'] for r in v['cases'])
    report['decoded']=sum(r['decoded']=='1' for v in report['variants'] for r in v['cases'])
    # Stages are bounded (~1000 total), retained to isolate CPU and first error.
    (args.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('completed','cases','constructed','decoded','elapsed_seconds')}))
    return 0 if report['completed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
