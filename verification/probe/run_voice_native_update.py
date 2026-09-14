#!/usr/bin/env python3
"""Retained null-event update probe: synthetic PCM control and actual voice files."""
import argparse,hashlib,json,math,os,struct,subprocess,sys,time,uuid,wave
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT / "verification/probe"))
from run_voice_stream_probe import bottle,game_running,digest,validate,fields,hr

def check_wave(path):
    with wave.open(str(path),'rb') as w:
        assert (w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getcomptype())==(1,2,44100,'NONE')
        assert w.getnframes()==70*44100
        for second in (10,60):
            w.setpos(second*44100);assert any(w.readframes(8820)),'control cue must be nonzero'

def check(text):
    assert ' sample_event=0\n' in text.splitlines(keepends=True)[0]
    result=validate(text,144,0,0)
    result['scope']='Synthetic70s mono44100PCM16 only; native async/null-event sample mode; no game archive or speech claim'
    return result

ASF_HEADER=uuid.UUID('75B22630-668E-11CF-A6D9-00AA0062CE6C')
ASF_PROPERTIES=uuid.UUID('8CABDCA1-A947-11CF-8EE4-00C00C205365')


def declared_seconds(path):
    """Play duration minus preroll from the documented ASF File Properties object."""
    with path.open('rb') as f:head=f.read(1<<16)
    assert uuid.UUID(bytes_le=head[:16])==ASF_HEADER,'not an ASF header object'
    count=struct.unpack('<I',head[24:28])[0];offset=30
    for _ in range(min(count,64)):
        if offset+24>len(head):break
        guid=uuid.UUID(bytes_le=head[offset:offset+16]);size=struct.unpack('<Q',head[offset+16:offset+24])[0]
        if guid==ASF_PROPERTIES:
            play,_send,preroll=struct.unpack('<QQQ',head[offset+64:offset+88])
            return (play-preroll*10000)/1e7
        if size<24:break
        offset+=size
    raise AssertionError('no ASF File Properties object')


def near(value,target,tolerance):
    return target>0 and abs(value-target)/target<=tolerance


def read_metrics(reads,bytes_per_second):
    """Per-read anchor error and span for one segment's VOICE_READ rows.

    The anchor is the first reported sample start of the segment; a read that
    follows N PCM bytes has byte-derived position N/bytes_per_second, so
    anchor_error = start - (first_start + N/bytes_per_second). Span is the
    read's own reported end minus start. Both in ms; empty/error rows
    (actual=0) carry no times and are skipped."""
    out=[];before=0;anchor=None
    for row in reads:
        actual=int(row['actual'])
        if actual<=0:continue
        start=int(row['start'])
        if anchor is None:anchor=start
        out.append(dict(index=int(row['index']),bytes=actual,
            anchor_error_ms=round((start-anchor-before*1e7/bytes_per_second)/1e4,3),
            span_ms=round((int(row['end'])-start)/1e4,3)))
        before+=actual
    return out


def read_summary(details):
    """Anchor-error and span statistics over a set of per-read metrics."""
    if not details:return dict(reads=0)
    errors=[d['anchor_error_ms'] for d in details];spans=[d['span_ms'] for d in details]
    return dict(reads=len(details),max_abs_anchor_error_ms=round(max(abs(e) for e in errors),3),
        min_anchor_error_ms=min(errors),max_anchor_error_ms=max(errors),
        min_span_ms=min(spans),max_span_ms=max(spans),mean_span_ms=round(sum(spans)/len(spans),3))


def check_actual(text,ids,window_s,tail_s,declared):
    rows=[(x.split()[0],fields(x)) for x in text.splitlines() if x.startswith('VOICE_')]
    assert rows and rows[0][0]=='VOICE_ACTUAL_HEADER'
    header=rows[0][1]
    assert {k:header[k] for k in ('schema','files','reopens','audible','sample_event')}==dict(schema='1',files='2',reopens='2',audible='0',sample_event='0')
    assert int(header['window_ms'])==round(window_s*1000) and int(header['tail_ms'])==round(tail_s*1000)
    assert int(header['thread'])>0
    assert not any(k=='VOICE_ABORT' for k,_ in rows)
    pending=None;seq=0
    for kind,row in rows:
        if kind=='VOICE_BEGIN':
            assert pending is None and int(row['seq'])==seq;pending=row
        elif kind=='VOICE_STAGE':
            assert pending is not None and all(row[k]==v for k,v in pending.items())
            assert row['cpu_valid']=='1' and all(math.isfinite(float(row[k])) and float(row[k])>=0 for k in ('wall_ms','cpu_ms'))
            hr(row['hr']);pending=None;seq+=1
    assert pending is None,'unterminated COM stage'
    assert rows[-1][0]=='VOICE_ACTUAL_COMPLETE' and rows[-1][1]==dict(files='2',reopens='2',audible='0')
    capture=[r for k,r in rows if k=='VOICE_CAPTURE']
    assert len(capture)==1 and int(capture[0]['bytes'])==int(capture[0]['written'])>0
    files=[r for k,r in rows if k=='VOICE_FILE']
    assert [r['source'] for r in files]==[str(i) for i in ids]
    summary=[];interior=[];tail_reads=[]
    for source,file in zip(ids,files):
        key=str(source)
        assert file['created']==file['complete']==file['eos']==file['monotonic']=='1'
        assert (file['rate'],file['channels'],file['bits'])==('44100','1','16')
        assert int(file['reads'])>0 and int(file['nonzero_reads'])>0 and int(file['bytes'])>0
        assert hr(file['hr'])==0 and file['fatal']=='none'
        decoded=float(file['decoded_s']);requested=float(file['requested_s'])
        assert near(decoded,requested,0.05),('decoded vs requested',key,decoded,requested)
        assert near(float(file['duration_s']),declared[source],0.05),('declared duration',key)
        segments=[r for k,r in rows if k=='VOICE_SEGMENT' and r['source']==key and r['repeat']=='0']
        assert [r['name'] for r in segments]==(['head','seek','tail'] if file['seeked']=='1' else ['head','tail'])
        bytes_per_second=int(file['rate'])*int(file['channels'])*int(file['bits'])//8
        assert bytes_per_second>0
        detail={}
        for segment in segments:
            assert int(segment['nonzero_reads'])>0 and segment['monotonic']=='1'
            reads=[r for k,r in rows if k=='VOICE_READ' and r['source']==key and r['repeat']=='0' and r['segment']==segment['name']]
            assert sum(int(r['actual'])>0 for r in reads)==int(segment['reads'])>0
            assert sum(int(r['actual']) for r in reads)==int(segment['bytes'])
            detail[segment['name']]=read_metrics(reads,bytes_per_second)
            (tail_reads if segment['name']=='tail' else interior).extend(detail[segment['name']])
            if segment['name']=='tail':
                assert segment['eos']=='1' and segment['end_reason']=='endofstream'
                assert hr(segment['post_eos_hr']) in (0x40001,0x40003,0x80070026) or not hr(segment['post_eos_hr'])&0x80000000
                assert near(float(segment['last_end'])/1e7,declared[source],0.05),('stream end vs declared',key)
            else:
                assert segment['eos']=='0' and segment['end_reason']=='window'
                assert near(float(segment['decoded_ms'])/1000.,window_s,0.05),('window',key,segment['name'])
                assert segment['start_ms']=='0' or near(float(segment['first_start'])/1e7,float(segment['start_ms'])/1000.,0.05)
        reopens=[r for k,r in rows if k=='VOICE_REOPEN' and r['source']==key]
        assert [r['attempt'] for r in reopens]==['1','2']
        for reopen in reopens:
            assert reopen['created']==reopen['read']=='1' and int(reopen['bytes'])>0 and reopen['fatal']=='none'
        roots=[r for k,r in rows if k=='VOICE_RELEASE' and r['source']==key and r['name']=='release_multimedia']
        assert len(roots)==3 and {r['refs'] for r in roots}=={'0'},'a stream object stayed alive'
        summary.append(dict(voice_id=source,reads=int(file['reads']),pcm_bytes=int(file['bytes']),
            nonzero_reads=int(file['nonzero_reads']),decoded_seconds=decoded,requested_seconds=requested,
            graph_duration_seconds=float(file['duration_s']),declared_seconds=declared[source],
            seeked=file['seeked']=='1',eos=True,live_root_objects=0,
            bytes_per_second=bytes_per_second,
            segments=[dict({k:s[k] for k in ('name','start_ms','reads','bytes','decoded_ms','span_ms','first_start','last_end','nonzero_reads','eos','end_reason','post_eos_hr')},
                reads_detail=detail[s['name']],reads_summary=read_summary(detail[s['name']])) for s in segments],
            reopens=[{k:r[k] for k in ('attempt','created','read','reads','bytes','nonzero_reads')} for r in reopens]))
    return dict(completed=True,files=summary,capture_bytes=int(capture[0]['bytes']),
        anchor=dict(interior=read_summary(interior),tail=read_summary(tail_reads)),
        scope='Actual game voice archives through the native AMMultiMediaStream route; bounded windows plus real end of stream; no speech restoration claim')


def write_wave(raw,path,rate=44100,channels=1,width=2):
    data=raw.read_bytes()
    with wave.open(str(path),'wb') as w:
        w.setparams((channels,width,rate,0,'NONE','not compressed'));w.writeframes(data)
    return len(data)


def run_actual(a):
    ids=[int(v) for v in a.voice_id]
    media=[bottle.game_dir()/f'addon/mov/{i:05d}.dat' for i in ids]
    exe=a.exe.resolve();assert digest(exe)==a.exe_sha256,'retained EXE mismatch; no rebuild'
    declared={i:declared_seconds(p) for i,p in zip(ids,media)}
    a.output.mkdir(parents=True,exist_ok=False)
    arguments=[]
    for i,p in zip(ids,media):arguments+=[str(i),'Z:'+str(p).replace('/','\\')]
    command=[bottle.WINE,*bottle.wine_args(),str(exe),'actual',str(round(a.window_seconds*1000)),str(round(a.tail_seconds*1000)),*arguments]
    report=dict(schema=1,mode='actual',completed=False,restored_speech=False,exe_sha256=a.exe_sha256,
        window_seconds=a.window_seconds,tail_seconds=a.tail_seconds,bottle=bottle.describe(),
        media=[dict(voice_id=i,path=str(p),bytes=p.stat().st_size,sha256=digest(p),declared_seconds=declared[i]) for i,p in zip(ids,media)])
    assert not game_running();start=time.monotonic()
    with (a.output/'stdout.txt').open('wb') as out,(a.output/'stderr.txt').open('wb') as err:
        try:r=subprocess.run(command,cwd=a.output,env=dict(os.environ,WINEDEBUG='-all,+winediag'),stdout=out,stderr=err,timeout=a.timeout)
        except subprocess.TimeoutExpired:report['abort']=f'{a.timeout}s external timeout'
        else:report['exit_code']=r.returncode
    report['process_wall_seconds']=time.monotonic()-start
    report['stdout_bytes']=(a.output/'stdout.txt').stat().st_size
    report['stderr_bytes']=(a.output/'stderr.txt').stat().st_size
    if report.get('exit_code')==0:
        try:
            assert report['stdout_bytes']<=4*1024*1024
            report.update(check_actual((a.output/'stdout.txt').read_text(),ids,a.window_seconds,a.tail_seconds,declared))
        except (AssertionError,ValueError,KeyError,IndexError) as e:report['abort']='invalid diagnostic: '+str(e)
    elif 'abort' not in report:report['abort']='nonzero process exit; last BEGIN identifies unresolved ownership'
    raw=a.output/'head.pcm'
    if raw.exists():
        wav=a.output/f'voice-{ids[0]:05d}-head.wav'
        report['wave']=dict(path=str(wav),bytes=write_wave(raw,wav),played=False)
    (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    if a.record:
        a.record.parent.mkdir(parents=True,exist_ok=True)
        a.record.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('completed','abort','process_wall_seconds','capture_bytes','anchor') if k in report}))
    return 0 if report['completed'] else 1


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--exe',type=Path,required=True);ap.add_argument('--exe-sha256',required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--mode',choices=('synthetic','actual'),default='synthetic')
    ap.add_argument('--pcm',type=Path,help='synthetic control WAV; required for --mode synthetic')
    ap.add_argument('--voice-id',action='append',default=None,help='actual mode voice archive id (default 144 and 244)')
    ap.add_argument('--window-seconds',type=float,default=20.0);ap.add_argument('--tail-seconds',type=float,default=5.0)
    ap.add_argument('--timeout',type=float,default=300.0);ap.add_argument('--record',type=Path,default=None)
    a=ap.parse_args()
    assert bottle.BOTTLE=='X3' and not game_running()
    if a.mode=='actual':
        a.voice_id=a.voice_id or ['144','244']
        assert len(a.voice_id)==2,'the native mode drives exactly the two voice archives'
        return run_actual(a)
    assert a.pcm is not None,'--pcm selects the synthetic control'
    exe=a.exe.resolve();assert digest(exe)==a.exe_sha256
    data=a.pcm.resolve();check_wave(data)
    a.output.mkdir(parents=True,exist_ok=False);start=time.monotonic()
    report=dict(completed=False,exe_sha256=a.exe_sha256,data_sha256=digest(data),bottle=bottle.describe(),restored_speech=False)
    try:r=subprocess.run([bottle.WINE,*bottle.wine_args(),str(exe),'144','0','0','Z:'+str(data).replace('/','\\')],cwd=a.output,env=dict(os.environ,WINEDEBUG='-all'),capture_output=True,text=True,timeout=90)
    except subprocess.TimeoutExpired as e:
        (a.output/'stdout.txt').write_bytes(e.stdout or b'');(a.output/'stderr.txt').write_bytes(e.stderr or b'');report['abort']='90s external timeout'
    else:
        (a.output/'stdout.txt').write_text(r.stdout);(a.output/'stderr.txt').write_text(r.stderr);report['exit_code']=r.returncode
        if not r.returncode:
            try:report.update(check(r.stdout))
            except (AssertionError,ValueError,KeyError,IndexError) as e:report['abort']='invalid diagnostic: '+str(e)
        else:report['abort']='nonzero exit; last BEGIN/ABORT identifies unresolved ownership'
    report['process_wall_seconds']=time.monotonic()-start
    (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('completed','abort','process_wall_seconds') if k in report}))
    return 0 if report['completed'] else 1
if __name__=='__main__':raise SystemExit(main())
