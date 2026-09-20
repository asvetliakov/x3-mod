#!/usr/bin/env python3
"""Host-only new timing executable; frozen R2 correctness executable is untouched."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
from fog_spatial_build import ROOT,digest,shaders_current,asset_inputs
FLAGS=['-std=c++17','-O2','-Wall','-Wextra','-Werror','-msse2','-mfpmath=sse','-mstackrealign','-mincoming-stack-boundary=2','-static','-DX3M_FOG_PASS_FIXTURE']

def snapshot(paths):return {str(p.resolve()):digest(p) for p in paths}
def unchanged(before):
    if any(digest(path)!=h for path,h in before.items()):raise ValueError('timing build input changed during operation')

def build(asset,data,out):
    asset,data,out=asset.resolve(),data.resolve(),out.resolve()
    if out.exists():raise ValueError('new timing build directory required')
    shaders=shaders_current()
    sources=[ROOT/'verification/probe/fog_spatial_timing_fixture.cpp',ROOT/'src/renderer/fog_pass.cpp',asset/'src/renderer/fog_field_assets.cpp']
    paths=[*sources,Path(__file__),ROOT/'verification/probe/fog_spatial_build.py',ROOT/'verification/probe/fog_spatial_timing_inc.h',ROOT/'verification/probe/fog_spatial_fixture.cpp',ROOT/'verification/probe/fog_spatial_state_inc.h',asset/'src/renderer/fog_field_assets.h',asset/'cmake/fog_field_assets.rc.in',*asset_inputs(data)]
    paths.extend(ROOT/'src/renderer'/name for name in ('fog_pass.h','fog_volume_math.h','fog_pass_math.h','ambient_occlusion_caps.h','quad_vertex_program.h','quad_vertex_program_inc.h','fog_march_program_inc.h','fog_composite_program_inc.h'))
    paths.extend(ROOT/'src/fog'/name for name in ('fog_field_inc.h','fog_march_ps.hlsl','fog_composite_ps.hlsl'));paths.append(ROOT/'src/proxy/cpu_state.h')
    toolchain={name:dict(path=shutil.which(name),version=subprocess.check_output([name,'--version'],text=True).splitlines()[0]) for name in ('i686-w64-mingw32-g++','i686-w64-mingw32-windres')}
    for tool in toolchain.values():tool['sha256']=digest(tool['path'])
    before=snapshot(paths);out.mkdir(parents=True)
    rc=out/'fog-fields.rc';rc.write_text((asset/'cmake/fog_field_assets.rc.in').read_text().replace('@X3M_FOG_BLUEWELL_BIN@',(data/'bluewell.fogbin').as_posix()).replace('@X3M_FOG_FOGGREENOUTLANDS_BIN@',(data/'foggreenoutlands.fogbin').as_posix()))
    windres=['i686-w64-mingw32-windres','-I',str(data),str(rc),'-O','coff','-o',str(out/'fog-fields.o')]
    command=['i686-w64-mingw32-g++',*FLAGS,'-I'+str(asset/'src/renderer'),'-I'+str(data),*map(str,sources),str(out/'fog-fields.o'),'-o',str(out/'fog_spatial_timing.exe'),'-luser32']
    subprocess.run(windres,check=True);subprocess.run(command,check=True);unchanged(before)
    if any(digest(tool['path'])!=tool['sha256'] for tool in toolchain.values()):raise ValueError('compiler changed during build')
    record=dict(kind='actual-production-timing',modes=['unshadowed','shadow-pairs'],executable_sha256=digest(out/'fog_spatial_timing.exe'),inputs=before,command=command,resource_command=windres,resource_rc_sha256=digest(rc),resource_object_sha256=digest(out/'fog-fields.o'),toolchain=toolchain,shaders=shaders)
    (out/'build.json').write_text(json.dumps(record,indent=2)+'\n');return record

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--asset-root',type=Path,required=True);ap.add_argument('--asset-data',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    result=build(a.asset_root,a.asset_data,a.output);print(json.dumps(dict(executable=str(a.output/'fog_spatial_timing.exe'),sha256=result['executable_sha256'])))
