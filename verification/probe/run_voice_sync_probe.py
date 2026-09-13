#!/usr/bin/env python3
"""Hash-bound direct reader diagnostic; root runs under the shared Wine lock."""
import argparse,json,math,os,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT / "verification/probe"))
from run_voice_stream_probe import fields,hr,digest,bottle,game_running
WINE_CHANNELS='-all,+quartz,+wmvcore,+wmadec,+winegstreamer,+winediag'
GST_CHANNELS='2,WINE:5,decodebin:4,typefind:4'


def validate(text):
    rows=[(line.split()[0],fields(line)) for line in text.splitlines() if line.startswith(('SYNC_','VOICE_'))]
    assert rows[0][0]=='SYNC_HEADER'
    assert {k:rows[0][1][k] for k in ('schema','cases','processes','audible')}==dict(schema='1',cases='2',processes='1',audible='0')
    assert int(rows[0][1]['thread'])>0
    assert not any(k in ('SYNC_ABORT','VOICE_ABORT') for k,r in rows)
    pending=None;stages=[]
    for kind,row in rows:
        if 'source' in row:
            assert row['source'] in ('0','144','244') and row['wrapper']==row['route']=='0'
            assert row['repeat']==('-1' if row['source']=='0' else '0')
        if kind=='VOICE_BEGIN':assert pending is None and int(row['seq'])==len(stages);pending=row
        if kind=='VOICE_STAGE':
            assert pending and all(row[k]==v for k,v in pending.items())
            assert row['cpu_valid']=='1' and all(math.isfinite(float(row[k])) and float(row[k])>=0 for k in ('wall_ms','cpu_ms'))
            hr(row['hr']);stages.append(row);pending=None
    assert pending is None
    startup=[r for r in stages if r['source']=='0']
    startup_passed={r['name'] for r in startup if not hr(r['hr'])&0x80000000}
    loads=[r for r in startup if r['name']=='load_wmvcore'];assert len(loads)==1
    lookups=[r for r in startup if r['name']=='find_create']
    prerequisite=loads[0]
    if 'load_wmvcore' in startup_passed:
        assert len(lookups)==1;prerequisite=lookups[0]
    else:assert not lookups
    assert 'co_initialize' in startup_passed
    cases=[r for k,r in rows if k=='SYNC_CASE'];assert [r['source'] for r in cases]==['144','244']
    for case in cases:
        own=[(k,r) for k,r in rows if r.get('source')==case['source']]
        local=[r for r in stages if r['source']==case['source']]
        passed={r['name'] for r in local if not hr(r['hr'])&0x80000000}
        for name in ('created','opened','metadata','closed','cleanup'):assert case[name] in ('0','1')
        assert case['cleanup']=='1'
        assert int(case['metadata'])<=int(case['opened'])<=int(case['created'])
        assert int(case['closed'])<=int(case['opened'])
        failures=[r for k,r in own if k=='VOICE_FAILURE']
        if case['fatal']=='none':assert not failures and case['metadata']==case['closed']=='1' and hr(case['hr'])==0
        else:assert len(failures)==1 and failures[0]['name']==case['fatal'] and failures[0]['hr']==case['hr'] and hr(case['hr'])&0x80000000
        creations=[r for r in local if r['name']=='create_sync_reader']
        if hr(prerequisite['hr'])&0x80000000:
            assert not creations and case['created']=='0'
            assert case['fatal']==prerequisite['name'] and case['hr']==prerequisite['hr']
        else:
            assert len(creations)==1
            activation=creations[0]
            assert bool(hr(activation['hr'])&0x80000000)==(case['created']=='0')
            if case['created']=='0':assert case['fatal']=='create_sync_reader' and case['hr']==activation['hr']
        opens=[r for r in local if r['name']=='sync_open']
        if case['created']=='1':
            assert {'load_wmvcore','find_create'}<=startup_passed
            assert 'create_sync_reader' in passed and len(opens)==1
            assert opens[0]['hr']==case['open_hr']
            assert bool(hr(case['open_hr'])&0x80000000)==(case['opened']=='0')
            if case['opened']=='0':assert case['fatal']=='sync_open' and case['hr']==case['open_hr']
        else:assert not opens and case['open_hr']=='8000000a'
        counts=[r for k,r in own if k=='SYNC_OUTPUTS'];types=[r for k,r in own if k=='SYNC_TYPE']
        assert len(counts)<=1
        if counts:assert counts[0]['count']==case['outputs'] and 'output_count' in passed
        if case['opened']=='0':assert not counts and not types and 'sync_close' not in {r['name'] for r in local}
        else:
            assert len([r for r in local if r['name']=='sync_close'])==1
            assert len([r for r in local if r['name']=='output_count'])==1
            if case['fatal'] in ('none','sync_close'):assert case['metadata']=='1'
            else:
                assert case['metadata']=='0'
                assert case['fatal'] in ('output_count','output_bound','output_props','media_size','media_bound','media_type','media_payload_bound')
                if case['fatal'] in ('output_count','output_props','media_size','media_type'):
                    assert any(r['name']==case['fatal'] and r['hr']==case['hr'] for r in local)
            closes=[r for r in local if r['name']=='sync_close']
            assert bool(hr(closes[0]['hr'])&0x80000000)==(case['closed']=='0')
            if case['fatal']=='sync_close':assert case['hr']==closes[0]['hr']
        if case['closed']=='1':assert 'sync_close' in passed
        for index,r in enumerate(types):
            assert int(r['index'])==index and 72<=int(r['size'])<=65536
            assert 0<=int(r['format_bytes'])<=int(r['size'])
            assert all(0<=int(r[k])<=0xffffffff for k in ('tag','channels','rate','bits','align','avg','extra'))
            for name in ('output_props','media_size','media_type'):
                assert any(x['name']==name and int(x['attempt'])==index+1 and not hr(x['hr'])&0x80000000 for x in local)
        if case['metadata']=='1':assert len(counts)==1 and 0<=int(case['outputs'])<=16 and len(types)==int(case['outputs'])
        case.update(types=types,measured_wall_ms=sum(float(r['wall_ms']) for r in local),measured_cpu_ms=sum(float(r['cpu_ms']) for r in local))
    assert [r for k,r in rows if k=='SYNC_COMPLETE']==[dict(cases='2',processes='1',audible='0')]
    return dict(completed=True,cases=cases,stages=stages,opened=sum(c['opened']=='1' for c in cases))


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--exe',type=Path,required=True);ap.add_argument('--exe-sha256',required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    assert bottle.BOTTLE=='X3' and not game_running()
    exe=a.exe.resolve();assert digest(exe)==a.exe_sha256,'retained EXE mismatch; no rebuild'
    a.output.mkdir(parents=True,exist_ok=False)
    media=[bottle.game_dir()/f'addon/mov/{sid:05d}.dat' for sid in (144,244)]
    identities=[dict(path=str(p),bytes=p.stat().st_size,sha256=digest(p)) for p in media]
    env=os.environ.copy();env.update(WINEDEBUG=WINE_CHANNELS,GST_DEBUG=GST_CHANNELS,GST_DEBUG_NO_COLOR='1')
    command=[bottle.WINE,*bottle.wine_args(),str(exe),*['Z:'+str(p).replace('/','\\') for p in media]]
    report=dict(schema=1,completed=False,restored_speech=False,exe_sha256=a.exe_sha256,media=identities,bottle=bottle.describe(),wine_debug=WINE_CHANNELS,gst_debug=GST_CHANNELS)
    assert not game_running();start=time.monotonic()
    with (a.output/'stdout.txt').open('wb') as stdout,(a.output/'stderr.txt').open('wb') as stderr:
        try:r=subprocess.run(command,cwd=a.output,env=env,stdout=stdout,stderr=stderr,timeout=75)
        except subprocess.TimeoutExpired:report['abort']='75s external timeout'
        else:report['exit_code']=r.returncode
    report['process_wall_seconds']=time.monotonic()-start
    report['stderr_bytes']=(a.output/'stderr.txt').stat().st_size
    if report.get('exit_code')==0:
        try:
            assert (a.output/'stdout.txt').stat().st_size<=1024*1024
            report.update(validate((a.output/'stdout.txt').read_text()))
        except (AssertionError,ValueError,KeyError,IndexError) as e:report['abort']='invalid diagnostic: '+str(e)
    elif 'abort' not in report:report['abort']='nonzero process exit; inspect last BEGIN'
    if report['completed']:report['outside_measured_stages_seconds']=report['process_wall_seconds']-sum(float(s['wall_ms']) for s in report['stages'])/1000
    (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('completed','opened','abort','process_wall_seconds','stderr_bytes') if k in report}))
    return 0 if report['completed'] else 1

if __name__=='__main__':raise SystemExit(main())
