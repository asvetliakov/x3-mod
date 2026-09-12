#!/usr/bin/env python3
"""Actual ownership-entry admission controls; no proxy, game or installation."""
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_guard import game_running  # noqa: E402

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'verification/results'
BUILD=ROOT/'verification/probe/build'
WINE=Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
INPUTS=['src/ownership/d3d9_ownership.cpp','src/ownership/d3d9_ownership.h',
 'src/ownership/d3d9_classes_inc.h','src/ownership/d3d9_forwarders_inc.h',
 'src/ownership/execution_state.cpp','src/ownership/execution_state.h',
 'src/ownership/finite_buffer_evidence.cpp','src/ownership/finite_buffer_evidence.h',
 'src/ownership/portable_managed_upload.cpp','src/ownership/portable_managed_upload.h',
 'src/ownership/application_admission.cpp','src/ownership/application_admission.h',
 'src/ownership/application_admission_abi.cpp','src/ownership/application_admission_abi.h',
 'tools/ownership/generate_d3d9_forwarders.py',
 'verification/probe/build_admission_dependencies.sh',
 'verification/probe/ownership_admission_fixture.cpp','verification/probe/build_ownership_admission.sh',
 'verification/probe/run_ownership_admission.py']
EXPECTED={'disabled':14,'enabled':33,**{f'private{i}':12 for i in range(7)},'shared':6,'foreign':5,'software':5}

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def sources():return {p:sha(ROOT/p) for p in INPUTS}
def no_game():
 if game_running():raise RuntimeError('game running or inventory unavailable')
def parse(mode,text):
 lines=text.splitlines();checks=EXPECTED[mode]
 if not lines or lines[-1]!=f'RESULT PASS mode={mode} checks={checks} failures=0' or sum(x.startswith('RESULT') for x in lines)!=1:raise ValueError(f'{mode}: terminal inventory')
 witnesses=[x for x in lines if x.startswith('CHECK ')]
 if len(witnesses)!=checks or any(not x.endswith(' PASS') for x in witnesses):raise ValueError(f'{mode}: failed/missing checks')
 samples=[]
 for line in lines:
  if line.startswith('SAMPLE '):
   match=re.fullmatch(r'SAMPLE trial=([1-7]) iterations=100000 ns_per_call=([0-9]+\.[0-9]+)',line)
   if not match:raise ValueError('bad sample')
   trial,value=match.groups()
   if int(trial)!=len(samples)+1 or float(value)<=0:raise ValueError('sample inventory/value')
   samples.append(float(value))
 count=7 if mode in ('enabled','disabled') else 0
 if len(samples)!=count or len(lines)!=checks+count+1:raise ValueError('unexpected/incomplete report')
 return {'checks':checks,'samples_ns':samples,'median_ns':statistics.median(samples) if samples else None}
def entry_audit():
 obj=BUILD/'ownership_admission/ownership.o'
 dump=subprocess.run(['i686-w64-mingw32-objdump','-r','-Cd',str(obj)],capture_output=True,text=True,check=True,timeout=30).stdout
 local=BUILD/'ownership_admission/ownership-disassembly.txt';local.write_text(dump)
 symbols=re.findall(r'^([0-9a-f]+) <([^\n]+)>:\n(.*?)(?=^[0-9a-f]+ <|\Z)',dump,re.M|re.S)
 emitted={}
 for address,name,body in symbols:
  match=re.search(r'::(Factory|Device|Texture|CubeTexture|VolumeTexture|Surface|Volume|VertexBuffer|IndexBuffer|VertexDeclaration|VertexShader|PixelShader|StateBlock|Query|SwapChain)::([A-Z]\w*)\(',name)
  if match and 'thunk' not in name:
   key='::'.join(match.groups())
   if key in emitted:raise ValueError('duplicate generated symbol')
   if re.search(r'SjLj|personality|gcc_except',body):raise ValueError('generated entry EH bookends')
   if 'ApplicationAdmissionAbi' not in body or 'process_admission_monitor' not in body:raise ValueError('missing entry admission')
   emitted[key]={'address':address,'body_sha256':hashlib.sha256(body.encode()).hexdigest()}
 source=(ROOT/'src/ownership/d3d9_forwarders_inc.h').read_text()
 authored=set('::'.join(x) for x in re.findall(r'^\w+ WINAPI (\w+)::(\w+)\(',source,re.M))
 if len(authored)!=297 or set(emitted)!=authored:raise ValueError('generated symbol coverage mismatch')
 if source.count('ApplicationAdmissionAbi admission(')!=297 or source.count('AdmissionVeto::PrivateUnknown')!=7 or source.count('AdmissionVeto::ExternalResource')!=8:raise ValueError('source inventory mismatch')
 # Preserve exception handling for handwritten ownership helpers.
 if 'Unwind_SjLj_Register' not in dump:raise ValueError('unexpected whole-TU exception suppression')
 return {'methods':emitted,'count':297,'object_sha256':sha(obj),'local_disassembly_sha256':sha(local),'generated_eh_references':0,'handwritten_eh_retained':True}
def main():
 summary=OUT/'ownership-admission-summary.json';report={'passed':False,'scope':'297 generated ownership entry accounting, selected actual COM dispatch/control witnesses, no live replay integration or whole-proxy ABI proof','runs':[]}
 summary.write_text(json.dumps(report,indent=2)+'\n')
 try:
  before=sources();report['source_hashes_before_build']=before;report['wine_sha256_before']=sha(WINE)
  subprocess.run(['sh',str(ROOT/'verification/probe/build_ownership_admission.sh')],check=True,capture_output=True,text=True,timeout=120)
  report['source_hashes_after_build']=sources()
  if sources()!=before:raise RuntimeError('source changed during build')
  report['entry_audit']=entry_audit();exe=BUILD/'ownership_admission_fixture.exe';report['executable_sha256_before']=sha(exe)
  for mode in EXPECTED:
   no_game();env=os.environ.copy();env['X3M_ADMISSION']='0' if mode=='disabled' else '1'
   command=[str(WINE),'--bottle','Steam','--no-update','--workdir',str(BUILD),str(exe),mode]
   run=subprocess.run(command,env=env,capture_output=True,text=True,timeout=60)
   raw=OUT/f'ownership-admission-{mode}.txt';err=OUT/f'ownership-admission-{mode}-stderr.txt';raw.write_text(run.stdout);err.write_text(run.stderr)
   item={'mode':mode,'command':command,'environment':{'X3M_ADMISSION':env['X3M_ADMISSION']},'exit_code':run.returncode,'report_sha256':sha(raw),'stderr_sha256':sha(err)}
   report['runs'].append(item)
   if run.returncode:raise RuntimeError(f'{mode}: fixture failed, inspect report')
   item.update(parse(mode,run.stdout))
   if sources()!=before:raise RuntimeError('source changed during run')
   print(f'{mode}: PASS {item["checks"]}',flush=True)
  report['source_hashes_after_run']=sources();report['executable_sha256_after']=sha(exe);report['wine_sha256_after']=sha(WINE)
  if sha(exe)!=report['executable_sha256_before'] or sha(WINE)!=report['wine_sha256_before']:raise RuntimeError('executable/runtime changed')
  report['checks']=sum(x['checks'] for x in report['runs']);report['passed']=True
 finally:summary.write_text(json.dumps(report,indent=2)+'\n')
 print(f'RESULT PASS modes={len(report["runs"])} checks={report["checks"]}')
if __name__=='__main__':main()
