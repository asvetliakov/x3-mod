#!/usr/bin/env python3
"""Startup-replica runner: reproduce the load-screen media hang without the game.

Runs the retained voice_startup_replica.exe under the X3 bottle (caller holds
the Wine lock and supplies any GST_* plugin environment). The probe's own 15 s
watchdog prints REPLICA_HUNG step=<name>; this runner watches stdout for that
marker, samples the probe's host processes with macOS `sample` into the output
directory, terminates them and records "hung at <step>". No game launch.
"""
import argparse,json,os,re,signal,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT / "verification/probe"))
from run_voice_stream_probe import fields,hr,digest,bottle,game_running
MODES=('game','nopause','early_update','single','explicit','game-ds','game-ds-stereo','game-dmo','game-dmo-fallback','game-dmo-skip')
GAME_DS_MODES=('game-ds','game-ds-stereo','game-dmo','game-dmo-fallback','game-dmo-skip')  # section-10 DirectSound init, visible window, 004d1d40 teardown
KEY_STEPS=('dmo_wrapper_init','dmo_wrapper_init_fallback','dmo_wrapper_skip','dmo_wrapper_add','remove_dmo_wrapper','release_graph','open_file','stream_run','control_pause','control_run','buffer_stop','control_stop','stream_stop','primary_create','primary_play','primary_set_format')
MEDIA_IDS=(144,244,144)  # stream 1 (played), 2 and 3 (restored, never pumped)
PLUGIN_KEYS=('GST_PLUGIN_PATH_1_0','GST_REGISTRY_1_0')
EXE_NAME='voice_startup_replica.exe'


def validate(text):
    rows=[(line.split()[0],fields(line)) for line in text.splitlines() if line.startswith('REPLICA_')]
    assert rows and rows[0][0]=='REPLICA_HEADER'
    header=rows[0][1]
    assert header['schema']=='1' and header['audible']=='0' and header['mode'] in MODES and int(header['thread'])>0
    streams=int(header['streams']);assert streams==(1 if header['mode']=='single' else 3)
    assert int(header['watchdog_ms'])==15000 and 0<=int(header['dwell_ms'])<=20000
    assert not any(k=='REPLICA_ABORT' for k,_ in rows)
    pending=None;stages=[];seq=0
    for kind,row in rows:
        if kind=='REPLICA_BEGIN':
            assert pending is None and int(row['seq'])==seq;pending=row
        elif kind=='REPLICA_STAGE':
            assert pending and all(row[k]==pending[k] for k in pending)
            hr(row['hr']);assert float(row['wall_ms'])>=0;stages.append(row);pending=None;seq+=1
    hungs=[r for k,r in rows if k=='REPLICA_HUNG'];assert len(hungs)<=1
    completes=[r for k,r in rows if k=='REPLICA_COMPLETE']
    startup=[r for k,r in rows if k=='REPLICA_STARTUP'];assert len(startup)==1
    primary=[r for k,r in rows if k=='REPLICA_PRIMARY']
    if header['mode'] in GAME_DS_MODES and startup[0]['ds_present']=='1':
        assert len(primary)==1 and hr(primary[0]['create_hr'])==0 and int(primary[0]['flags'],16) in (0xd1,0x11,0x1) and primary[0]['listener'] in ('0','1')
    else:assert not primary
    created=[r for k,r in rows if k=='REPLICA_STREAM']
    for row in created:
        assert 1<=int(row['stream'])<=streams and row['created'] in ('0','1')
        assert row['role']==('played' if row['stream']=='1' else 'restored')
        failures=[r for k,r in rows if k=='REPLICA_FAILURE' and r['stream']==row['stream']]
        if row['created']=='1':assert row['fatal']=='none' and not failures and hr(row['hr'])==0
        else:assert len(failures)==1 and failures[0]['name']==row['fatal'] and failures[0]['hr']==row['hr'] and hr(row['hr'])&0x80000000
        if header['mode']=='early_update' and row['created']=='1':assert row['early_update_hr']!='8000000a'
        else:assert row['early_update_hr']=='8000000a' or row['created']=='0'
    assert [int(r['stream']) for r in created]==([1] if streams==1 else [2,3,1])[:len(created)]
    plays=[r for k,r in rows if k=='REPLICA_PLAY'];assert len(plays)<=1
    polls=[r for k,r in rows if k=='REPLICA_POLL']
    result=dict(mode=header['mode'],dwell_ms=int(header['dwell_ms']),streams=streams,created=sum(r['created']=='1' for r in created),stream_rows=created,
                startup=startup[0],primary=primary[0] if primary else None,stages=stages,polls=polls,play=plays[0] if plays else None,completed=False,hung_step=None,
                key_steps=[dict(stream=int(r['stream']),name=r['name'],attempt=int(r['attempt']),hr=r['hr'],wall_ms=float(r['wall_ms'])) for r in stages if r['name'] in KEY_STEPS])
    if hungs:
        # The step the watchdog names must be the one still open on the main thread.
        h=hungs[0];assert not completes
        assert pending is None or pending['name']==h['step'],'watchdog step differs from the open step'
        assert int(h['elapsed_ms'])>=15000 and h['thread']==header['thread']
        result.update(hung_step=h['step'],hung_stream=int(h['stream']),hung_elapsed_ms=int(h['elapsed_ms']),hung_after_stages=len(stages))
        return result
    assert pending is None,'unterminated step without a watchdog report'
    assert completes==[dict(streams=str(streams),audible='0')]
    assert len(created)==streams
    played=[r for r in created if r['stream']=='1']
    if played and played[0]['created']=='1':
        assert len(plays)==1;p=plays[0]
        for k in ('cycles','completed','queued','pending_polls','eos','stuck','errors','bytes','elapsed_ms'):assert int(p[k])>=0
        assert int(p['completed'])<=5 and int(p['elapsed_ms'])<=31000
        assert int(p['cycles'])>=int(p['completed'])+int(p['queued'])
        if int(p['cycles']):assert polls
    else:assert not plays and not polls
    result['completed']=True
    return result


def parse_sample(text):
    """Per-thread leaf call path from a macOS `sample` report (compact, not the tree)."""
    threads=[];current=None
    for line in text.splitlines():
        m=re.match(r'^\s*(?:\d+\s+)?(Thread_\d+)(.*)$',line)
        if m:
            current=dict(thread=m.group(1),label=m.group(2).strip()[:80],frames=[]);threads.append(current);continue
        if current is None:continue
        f=re.match(r'^\s*(?:[+!:| ]+)?\s*(\d+)\s+(\S.*?)\s+\(in ([^)]*)\)',line)
        if f and len(current['frames'])<48:
            current['frames'].append(f'{f.group(2).strip()[:70]} ({f.group(3)})')
        elif line.startswith('Binary Images'):current=None
    return threads


def process_rows():
    """pid -> (ppid, state, args) snapshot; empty on a ps failure."""
    try:out=subprocess.run(['ps','-axo','pid=,ppid=,state=,args='],capture_output=True,text=True,timeout=10).stdout
    except (subprocess.SubprocessError,OSError):return {}
    rows={}
    for line in out.splitlines():
        parts=line.split(None,3)
        if len(parts)==4 and parts[0].isdigit() and parts[1].isdigit():rows[int(parts[0])]=(int(parts[1]),parts[2],parts[3])
    return rows


def replica_pids(rows=None,wrapper=None,name=EXE_NAME):
    """Live PE replica processes: matched by image name and by descent from the wine wrapper.

    The wine wrapper's own pid and this runner are excluded (the wrapper is killed
    last, by the Popen handle); zombies are already dead and are not survivors.
    Descendants of the wrapper count only when they are PE images, so wineserver
    and other bottle services are never touched.
    """
    rows=process_rows() if rows is None else rows
    stem=name[:-4] if name.lower().endswith('.exe') else name
    def descends(pid):
        seen=set()
        while pid in rows and pid not in seen:
            seen.add(pid);pid=rows[pid][0]
            if wrapper is not None and pid==wrapper:return True
        return False
    pids=[]
    for pid,(ppid,state,args) in rows.items():
        if pid in (os.getpid(),wrapper) or state.startswith('Z') or 'python' in args.lower():continue
        if stem in args or (descends(pid) and '.exe' in args.lower()):pids.append(pid)
    return sorted(pids)


def pids_for(name=EXE_NAME):
    """Host pids of the Wine-side probe processes (the runner and lock wrapper also name the EXE; skip python)."""
    return replica_pids(name=name)[:4]


def sample_processes(output,seconds):
    records=[]
    for pid in pids_for(EXE_NAME):
        path=output/f'sample-{pid}.txt'
        try:
            r=subprocess.run(['sample',str(pid),str(seconds),'-file',str(path)],capture_output=True,text=True,timeout=seconds+40)
            ok=r.returncode==0 and path.is_file()
        except (subprocess.SubprocessError,OSError):ok=False
        record=dict(pid=pid,file=str(path),ok=ok)
        if ok:record['threads']=parse_sample(path.read_text(errors='replace'))
        records.append(record)
    return records


def _still_alive(pids,rows=None):
    rows=process_rows() if rows is None else rows
    return sorted(p for p in pids if p in rows and not rows[p][1].startswith('Z'))


def _signal(pids,number,killed):
    for pid in pids:
        killed.add(pid)
        try:os.kill(pid,number)
        except OSError:pass


def terminate(proc,grace=2.0):
    """Kill the PE replica processes, then the wine wrapper.

    Terminating the wrapper alone leaves the PE process (the hung probe) spinning
    under wine and invisible to a wrapper-anchored lookup, so the PE images are
    enumerated and signalled first: SIGTERM, at most `grace` seconds, SIGKILL.
    Returns the pids signalled and any process still alive afterwards.
    """
    killed=set();wrapper=proc.pid
    _signal(replica_pids(wrapper=wrapper),signal.SIGTERM,killed)
    deadline=time.monotonic()+grace
    while time.monotonic()<deadline and replica_pids(wrapper=wrapper):time.sleep(0.2)
    _signal(replica_pids(wrapper=wrapper),signal.SIGKILL,killed)
    proc.terminate()
    try:proc.wait(5)
    except subprocess.TimeoutExpired:
        try:proc.kill();proc.wait(10)
        except (subprocess.TimeoutExpired,OSError):pass
    # The wrapper is gone, so the parent chain no longer resolves: check the pids
    # already identified by pid as well as any remaining name match.
    def left():return sorted(set(replica_pids())|set(_still_alive(killed)))
    deadline=time.monotonic()+grace
    while time.monotonic()<deadline and left():
        _signal(left(),signal.SIGKILL,killed);time.sleep(0.3)
    return dict(killed_pids=sorted(killed),survivors=left())


def tail(path,limit=65536):
    if not path.is_file():return ''
    with path.open('rb') as f:
        f.seek(max(0,path.stat().st_size-limit));return f.read().decode('utf-8','replace')


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--exe',type=Path,required=True);ap.add_argument('--exe-sha256',required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--mode',choices=MODES,default='game');ap.add_argument('--dwell-ms',type=int,default=0);ap.add_argument('--timeout',type=float,default=150.0)
    ap.add_argument('--sample-seconds',type=int,default=3);ap.add_argument('--label',default=None);ap.add_argument('--record',type=Path,default=None)
    a=ap.parse_args()
    assert bottle.BOTTLE=='X3' and not game_running()
    exe=a.exe.resolve();assert exe.name==EXE_NAME and digest(exe)==a.exe_sha256,'retained EXE mismatch; no rebuild'
    assert not pids_for(EXE_NAME),'a replica process is already running'
    a.output.mkdir(parents=True,exist_ok=False)
    media=[bottle.game_dir()/f'addon/mov/{sid:05d}.dat' for sid in MEDIA_IDS]
    identities=[dict(path=str(p),bytes=p.stat().st_size,sha256=digest(p)) for p in media]
    env=os.environ.copy();env.setdefault('WINEDEBUG','-all,+winediag')
    plugin={k:env[k] for k in PLUGIN_KEYS if k in env}
    command=[bottle.WINE,*bottle.wine_args(),str(exe),a.mode,str(a.dwell_ms),*['Z:'+str(p).replace('/','\\') for p in media]]
    report=dict(schema=1,label=a.label,mode=a.mode,dwell_ms=a.dwell_ms,completed=False,hung_step=None,exe_sha256=a.exe_sha256,media=identities,bottle=bottle.describe(),
                plugin_env=plugin,plugin_present=len(plugin)==len(PLUGIN_KEYS),gst_debug=env.get('GST_DEBUG'),gst_debug_file=env.get('GST_DEBUG_FILE'),wine_debug=env['WINEDEBUG'])
    start=time.monotonic();samples=[];abort=None;kill=dict(killed_pids=[],survivors=[])
    with (a.output/'stdout.txt').open('wb') as stdout,(a.output/'stderr.txt').open('wb') as stderr:
        proc=subprocess.Popen(command,cwd=a.output,env=env,stdout=stdout,stderr=stderr)
        while proc.poll() is None:
            time.sleep(0.5)
            if 'REPLICA_HUNG ' in tail(a.output/'stdout.txt'):
                report['hang_detected_at_seconds']=time.monotonic()-start
                samples=sample_processes(a.output,a.sample_seconds);kill=terminate(proc);break
            if time.monotonic()-start>a.timeout:
                abort=f'{a.timeout:g}s external timeout without watchdog report';samples=sample_processes(a.output,a.sample_seconds);kill=terminate(proc);break
        report['exit_code']=proc.returncode
    report['killed_pids']=kill['killed_pids'];report['survivors']=kill['survivors']
    report['process_wall_seconds']=time.monotonic()-start
    report['samples']=samples
    report['stderr_bytes']=(a.output/'stderr.txt').stat().st_size
    for p in (a.output/'gst.log',):
        if p.is_file():report['gst_log_bytes']=p.stat().st_size;report['gst_log_tail']=tail(p,4096).splitlines()[-12:]
    try:
        assert (a.output/'stdout.txt').stat().st_size<=4*1024*1024
        report.update(validate((a.output/'stdout.txt').read_text(errors='replace')))
    except (AssertionError,ValueError,KeyError,IndexError) as e:report['abort']='invalid diagnostic: '+str(e)
    if abort:report['abort']=abort
    if report['hung_step']:report['outcome']=f"hung at {report['hung_step']} (stream {report.get('hung_stream')})"
    elif report['completed']:report['outcome']='completed'
    else:report['outcome']='inconclusive: '+report.get('abort','nonzero exit or invalid output')
    (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    if a.record:
        compact={k:report.get(k) for k in ('label','mode','dwell_ms','outcome','completed','hung_step','hung_stream','hung_elapsed_ms','hung_after_stages','created','exit_code','killed_pids','survivors','process_wall_seconds','plugin_present','plugin_env','gst_debug','exe_sha256','abort')}
        compact['bottle']=report['bottle'];compact['media_sha256']=[m['sha256'] for m in identities]
        compact['play']=report.get('play');compact['output']=str(a.output)
        for k in ('startup','primary','key_steps','stream_rows'):compact[k]=report.get(k)
        compact['sample_threads']=[dict(pid=s['pid'],threads=[dict(thread=t['thread'],top=t['frames'][:6]) for t in s.get('threads',[])]) for s in samples]
        compact['gst_log_tail']=report.get('gst_log_tail')
        record=json.loads(a.record.read_text()) if a.record.is_file() else dict(schema=1,runs={})
        record['runs'][a.label or a.output.name]=compact
        a.record.parent.mkdir(parents=True,exist_ok=True);a.record.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('outcome','completed','hung_step','created','exit_code','killed_pids','survivors','process_wall_seconds','abort') if k in report}))
    return 0 if report['completed'] else 3 if report['hung_step'] else 1

if __name__=='__main__':raise SystemExit(main())
