#!/usr/bin/env python3
"""Only the retained synthetic PCM control, using the game's null-event update."""
import argparse,hashlib,json,os,subprocess,sys,time,wave
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT / "verification/probe"))
from run_voice_stream_probe import bottle,game_running,digest,validate

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

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--exe',type=Path,required=True);ap.add_argument('--exe-sha256',required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--pcm',type=Path,required=True);a=ap.parse_args()
    assert bottle.BOTTLE=='X3' and not game_running()
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
