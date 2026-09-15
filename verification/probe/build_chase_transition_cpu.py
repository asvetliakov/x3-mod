#!/usr/bin/env python3
"""Build/audit the 18-stub chase CPU fixture and bounded benchmark. Never runs Wine."""
import json
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS,audit_object
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/verification/chase-transition'
def build():
 BUILD.mkdir(parents=True,exist_ok=True);objects=[]
 for source,stem in [('src/proxy/chase_transition.cpp','audit'),('src/proxy/chase_lead.cpp','lead_audit'),('verification/probe/chase_transition_cpu_fixture.cpp','fixture'),('src/proxy/engine_patch.cpp','patch'),('src/proxy/engine_memory.cpp','memory')]:
  out=BUILD/(stem+'.o');flags=list(FLAGS)
  if stem=='memory':flags+=['-mno-sse','-mno-mmx','-mfpmath=387']
  subprocess.run(['i686-w64-mingw32-g++',*flags,'-c',str(ROOT/source),'-o',str(out)],check=True,cwd=ROOT)
  if not stem.endswith('audit'):objects.append(out)
 audit={'transition':audit_object(BUILD/'audit.o','x3m_chase_transition_enter'),
        'restore':audit_object(BUILD/'audit.o','x3m_chase_restore_enter'),
        'lead':audit_object(BUILD/'lead_audit.o','x3m_chase_lead_enter')}
 exe=BUILD/'chase_transition_cpu_fixture.exe'
 subprocess.run(['i686-w64-mingw32-g++',*map(str,objects),'-static','-static-libgcc','-static-libstdc++','-o',str(exe)],check=True,cwd=ROOT)
 return {'binary':str(exe),'cpu_audit':audit,'runtime':'not run'}
if __name__=='__main__':print(json.dumps(build(),indent=2))
