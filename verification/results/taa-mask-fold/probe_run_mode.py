#!/usr/bin/env python3
"""Run one temporal fixture mode under Wine (call through wine_lock.py). Usage: run_mode.py <root> <mode|-> <out>"""
import os,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]);mode=sys.argv[2];out=Path(sys.argv[3])
exe=root/'verification/probe/build/temporal_pass_fixture.exe'
cmd=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine','--bottle','X3','--no-update','--dll','d3d9=b','--workdir',str(exe.parent),str(exe),r'C:\X3\d3dx9_37.dll',
     'Z:'+str(root/'src/temporal/depth_decode.hlsl'),'Z:'+str(root/'src/temporal/resolve.hlsl'),'Z:'+str(root/'src/temporal/taa_sharpen_ps.hlsl')]
if mode!='-':cmd.append(mode)
with out.open('w') as f,open(str(out)+'.err','w') as e:
    r=subprocess.run(cmd,stdout=f,stderr=e,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=1800)
print('exit',r.returncode)
