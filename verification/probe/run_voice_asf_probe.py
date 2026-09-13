#!/usr/bin/env python3
"""Consume only a reviewed explicit-ASF EXE under the root-owned Wine lock."""
import argparse
import itertools
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT / "verification/probe"))
from run_voice_stream_probe import fields, hr, digest, bottle, game_running

MATRIX=tuple(itertools.product((144,244),(0,1),(0,1)))
AUDIO='73647561-0000-0010-8000-00aa00389b71'


def key(row):
    return int(row['source']),int(row['route']),int(row['repeat'])


def validate(text):
    rows=[(line.split()[0],fields(line)) for line in text.splitlines() if line.startswith(('ASF_','VOICE_'))]
    assert rows and rows[0][0]=='ASF_HEADER'
    header=rows[0][1]
    assert {k:int(header[k]) for k in ('schema','sources','modes','repeats','cases','audible')}==dict(schema=1,sources=2,modes=2,repeats=2,cases=8,audible=0)
    assert int(header['thread'])>0
    assert not any(k in ('ASF_ABORT','VOICE_ABORT') for k,r in rows),'probe did not complete safely'
    pending=None;sequence=0;stages=[]
    for kind,row in rows[1:]:
        if 'source' in row:
            if int(row['source'])==0:
                assert int(row['repeat'])==-1 and row.get('phase') in ('startup','shutdown')
            else:
                assert key(row) in MATRIX and int(row['wrapper'])==int(row['route'])
        if kind=='VOICE_BEGIN':
            assert pending is None and int(row['seq'])==sequence;pending=row
        elif kind=='VOICE_STAGE':
            assert pending and int(row['seq'])==sequence
            assert all(row[k]==v for k,v in pending.items())
            assert row['cpu_valid']=='1'
            assert all(math.isfinite(float(row[k])) and float(row[k])>=0 for k in ('wall_ms','cpu_ms'))
            hr(row['hr']);stages.append(row);sequence+=1;pending=None
    assert pending is None
    startup=[r for k,r in rows if k=='VOICE_STARTUP']
    assert len(startup)==1 and startup[0]['primary_play']=='0'
    assert startup[0]['ds_present'] in ('0','1') and startup[0]['window'] in ('0','1')
    assert bool(hr(startup[0]['ds_hr'])&0x80000000)==(startup[0]['ds_present']=='0')
    cases=[r for k,r in rows if k=='ASF_CASE']
    assert [key(r) for r in cases]==list(MATRIX),'exact eight-case order required'
    for case in cases:
        identity=key(case);mode=identity[1]
        own=[(k,r) for k,r in rows if 'source' in r and key(r)==identity]
        local=[r for r in stages if key(r)==identity]
        attempted={r['name'] for r in local}
        passed={r['name'] for r in local if not hr(r['hr'])&0x80000000}
        for field in ('source_available','loaded','pins_selected','decoder_attempted','decoder_available','connected','constructed','decoded'):
            assert case[field] in ('0','1')
        source,loaded,pins,connected,made,decoded=(int(case[f]) for f in ('source_available','loaded','pins_selected','connected','constructed','decoded'))
        assert decoded<=made<=connected<=pins<=loaded<=source
        assert case['cleanup']=='1' and int(case['decoder_attempted'])==mode
        failures=[r for k,r in own if k=='VOICE_FAILURE']
        if decoded:
            assert not failures and case['fatal']=='none' and hr(case['hr'])==0
        else:
            assert len(failures)==1 and failures[0]['name']==case['fatal'] and failures[0]['hr']==case['hr']
            assert hr(case['hr'])&0x80000000
        caps=[r for k,r in own if k=='ASF_DECODER']
        if mode:
            assert len(caps)==1 and caps[0]['available']==case['decoder_available'] and caps[0]['hr']==case['decoder_hr']
            assert bool(hr(case['decoder_hr'])&0x80000000)==(case['decoder_available']=='0')
            assert 'wma_wrapper_activate' in {r['name'] for r in local},'decoder capability must run even when ASF fails'
            if case['decoder_available']=='1':
                assert {'wma_wrapper_activate','wma_wrapper_qi','wma_decoder_init'}<=passed
        else:
            assert not caps and case['decoder_available']=='0' and hr(case['decoder_hr'])==0
        if source:assert 'asf_activate' in passed
        if loaded:assert {'asf_add','asf_file_qi','asf_load'}<=passed
        assert len([r for r in local if r['name']=='asf_load'])<=1,'Load cannot retry same source'
        selections=[r for k,r in own if k=='ASF_SELECTION']
        types=[r for k,r in own if k=='ASF_TYPE']
        for item in types:
            assert item['role'] in ('source','sink','decoder_input','decoder_output')
            assert 0<=int(item['pin'])<16 and 0<=int(item['type'])<16
            assert all(0<=int(item[k])<=0xffffffff for k in ('format_bytes','tag','channels','rate','bits','align','avg','extra'))
        pinrows=[r for k,r in own if k=='ASF_PIN']
        assert len({(r['role'],r['pin']) for r in pinrows})==len(pinrows)
        for r in pinrows:
            assert r['role'] in ('source','sink','decoder_input','decoder_output') and 0<=int(r['pin'])<16
            assert r['direction'] in ('0','1') and r['audio'] in ('0','1')
            if r['audio']=='1':
                assert any(x['role']==r['role'] and x['pin']==r['pin'] and x['major']==AUDIO for x in types)
        roles={'source','sink'} if pins else set()
        if 'connect_source_decoder' in attempted:roles.add('decoder_input')
        if 'connect_decoder_manual' in attempted:roles.add('decoder_output')
        for role in roles:
            selected=[r for r in selections if r['role']==role]
            assert len(selected)==1 and selected[0]['candidates']=='1'
            direction=1 if role in ('source','decoder_output') else 0
            assert len([r for r in pinrows if r['role']==role and int(r['direction'])==direction and r['audio']=='1'])==1
            assert any(r['role']==role and r['major']==AUDIO for r in types)
            prefix={'source':'source','sink':'sink','decoder_input':'decoder_in','decoder_output':'decoder_out'}[role]
            assert prefix+'_enum_pins' in passed and prefix+'_enum_types' in passed and prefix+'_direction' in passed
            # Completed enumeration is evidence, not absence of type/pin rows.
            assert any(r['name']==prefix+'_pin_next' and r['hr']=='00000001' for r in local)
            assert any(r['name']==prefix+'_type_next' and r['hr']=='00000001' for r in local)
        if pins:assert 'sink_query_source_type' in attempted
        if 'connect_source_decoder' in attempted:assert 'decoder_query_source_type' in attempted
        if 'connect_decoder_manual' in attempted:assert 'sink_query_decoder_type' in attempted
        for edge,first,second in (('connect_auto_to_manual','source','sink'),
                                  ('connect_source_decoder','source','decoder_input'),
                                  ('connect_decoder_manual','decoder_output','sink')):
            if edge in passed:
                assert first+'_connected_type' in attempted
                if first+'_connected_type' in passed:
                    assert second+'_connected_type' in attempted
        negotiated=[r for k,r in own if k=='ASF_NEGOTIATED']
        assert len({r['role'] for r in negotiated})==len(negotiated)
        for r in negotiated:
            assert r['role'] in roles and r['role']+'_connected_type' in passed
            assert r['major']==AUDIO
            assert all(0<=int(r[k])<=0xffffffff for k in ('format_bytes','tag','channels','rate','bits','align','avg','extra'))
        # Successful selected-endpoint queries must publish their media type;
        # a failed query remains a first-fatal diagnostic, not an absent edge.
        for role in roles:
            if role+'_connected_type' in passed:
                assert any(r['role']==role for r in negotiated)
        if connected:
            assert {r['role'] for r in negotiated}==roles
            sink_type=next(r for r in negotiated if r['role']=='sink')
            assert sink_type['tag']=='1' and sink_type['bits']=='16'
            assert int(sink_type['align'])==2*int(sink_type['channels'])>0
            assert int(sink_type['avg'])==int(sink_type['rate'])*int(sink_type['align'])>0
        if mode:
            # Both sides of each direct edge must report the same negotiated
            # public format fields. Mode A permits intermediate conversion.
            for a,b in (('source','decoder_input'),('decoder_output','sink')):
                pair=[r for r in negotiated if r['role'] in (a,b)]
                if len(pair)==2:
                    assert {k:v for k,v in pair[0].items() if k!='role'}=={k:v for k,v in pair[1].items() if k!='role'}
        if connected:
            assert ({'connect_auto_to_manual'} if mode==0 else {'connect_source_decoder','connect_decoder_manual'})<=passed
            if mode:assert case['decoder_available']=='1'
        formats=[r for k,r in own if k=='VOICE_FORMAT']
        assert len(formats)<=1
        pcm=[r for k,r in own if k=='VOICE_PCM']
        if made:
            required={'activate_stream','initialize','add_audio','audio_qi_pre','set_pcm','get_graph','get_manual_filter',
                      'get_audio','audio_qi_post','get_format','activate_audio_data','set_buffer_native','data_format',
                      'create_sample','create_dsound_buffer','position_qi','control_qi','probe_manual_sink_guard','stream_run','control_pause'}
            assert required<=passed
            sinks=[r for k,r in own if k=='VOICE_SINK']
            assert len(sinks)==1 and sinks[0]['manual_only']=='1' and sinks[0]['terminals']=='1'
            assert len(formats)==1
            fmt=formats[0]
            assert int(fmt['tag'])==1 and int(fmt['bits'])==16 and int(fmt['channels'])>0
            assert int(fmt['align'])==2*int(fmt['channels']) and int(fmt['rate'])>0
            assert 0<int(fmt['avg'])<=1000000 and int(fmt['avg'])==int(fmt['rate'])*int(fmt['align'])
        else:assert not pcm
        assert [(int(r['cue']),int(r['batch'])) for r in pcm]==[(c,b) for c in range(2) for b in range(2)][:len(pcm)]
        for r in pcm:
            fmt=formats[0]
            assert 0<int(r['actual'])<=int(fmt['avg'])//10 and int(r['actual'])%int(fmt['align'])==0
            assert int(r['end'])>int(r['start'])
            assert int(r['seek_ms'])==(10000 if int(r['cue'])==0 else 60000)
            assert int(r['requested_ms'])==int(r['seek_ms'])+500
            assert 0<=int(r['nonzero'])<=int(r['actual'])//2 and 0<=int(r['peak'])<=32768 and int(r['energy'])>=0
        if decoded:
            assert len(pcm)==4 and sum(int(r['nonzero']) for r in pcm)>0
            assert int(pcm[1]['end'])>int(pcm[0]['end']) and int(pcm[2]['start'])>int(pcm[1]['end']) and int(pcm[3]['end'])>int(pcm[2]['end'])
        case.update(format=formats[0] if formats else None,pcm=pcm,offered_types=types,pin_inventory=pinrows,selections=selections,negotiated_types=negotiated,
                    measured_wall_ms=sum(float(r['wall_ms']) for r in local),
                    measured_cpu_ms=sum(float(r['cpu_ms']) for r in local),
                    largest_stages=sorted(local,key=lambda r:float(r['wall_ms']),reverse=True)[:5])
    assert [r for k,r in rows if k=='ASF_COMPLETE']==[dict(cases='8',processes='1',audible='0',owner_thread='1')]
    return dict(completed=True,cases=cases,stages=stages,startup=startup[0],
                connected_pins=[r for k,r in rows if k=='VOICE_PIN'],filters=[r for k,r in rows if k=='VOICE_FILTER'])


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--exe',type=Path,required=True);ap.add_argument('--exe-sha256',required=True);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    assert bottle.BOTTLE=='X3' and not game_running()
    exe=args.exe.resolve();assert digest(exe)==args.exe_sha256,'retained EXE changed; no implicit rebuild'
    args.output.mkdir(parents=True,exist_ok=False)
    media=[bottle.game_dir()/f'addon/mov/{sid:05d}.dat' for sid in (144,244)]
    identities=[]
    for path in media:
        assert path.is_file() and path.stat().st_size>1000000
        identities.append(dict(path=str(path),bytes=path.stat().st_size,sha256=digest(path)))
    command=[bottle.WINE,*bottle.wine_args(),str(exe),*['Z:'+str(p).replace('/','\\') for p in media]]
    env=os.environ.copy();env['WINEDEBUG']='-all';assert not game_running()
    report=dict(schema=1,exe_sha256=args.exe_sha256,media=identities,bottle=bottle.describe(),completed=False,restored_speech=False)
    started=time.monotonic()
    try:
        result=subprocess.run(command,cwd=args.output,env=env,capture_output=True,text=True,timeout=120)
    except subprocess.TimeoutExpired as exc:
        (args.output/'stdout.txt').write_bytes(exc.stdout or b'');(args.output/'stderr.txt').write_bytes(exc.stderr or b'');report['abort']='external120s timeout'
    else:
        (args.output/'stdout.txt').write_text(result.stdout);(args.output/'stderr.txt').write_text(result.stderr)
        report['exit_code']=result.returncode
        if result.returncode:report['abort']='nonzero exit; last BEGIN identifies interrupted stage'
        else:
            try:report.update(validate(result.stdout))
            except (AssertionError,ValueError,KeyError) as exc:report['abort']='invalid diagnostic: '+str(exc)
    report['process_wall_seconds']=time.monotonic()-started
    if report['completed']:
        report['constructed']=sum(c['constructed']=='1' for c in report['cases']);report['decoded']=sum(c['decoded']=='1' for c in report['cases'])
        report['outside_measured_stages_seconds']=report['process_wall_seconds']-sum(float(s['wall_ms']) for s in report['stages'])/1000.
    (args.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('completed','constructed','decoded','process_wall_seconds','abort') if k in report}))
    return 0 if report['completed'] else 1


if __name__=='__main__':raise SystemExit(main())
