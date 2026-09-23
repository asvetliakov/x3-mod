#!/usr/bin/env python3
"""Build-only actual fog-method/D3D bridge. Never launches Wine or rebuilds a DLL."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
from fog_spatial_build import asset_inputs
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--production-root',type=Path,required=True);p.add_argument('--spatial-root',type=Path,required=True);p.add_argument('--asset-data',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--baseline',action='store_true',help='production root predates the stored-density range (pinned: 6f16dbf6, parent of 39c98242, as a scratch git worktree): legacy witnesses and IMAGE hashes only');a=p.parse_args()
    prod=a.production_root.resolve();base=a.spatial_root.resolve();data=a.asset_data.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    exe=out/'fog_route_bridge.exe'
    if exe.exists():raise ValueError('refuse overwrite of frozen executable')
    # Complete fragment, byte for byte. The fixture supplies owner inputs only.
    fragment=prod/'src/proxy/motion_output_fog_inc.h';(out/'fog_route_methods_inc.h').write_bytes(fragment.read_bytes())
    text=(prod/'cmake/fog_field_assets.rc.in').read_text().replace('@X3M_FOG_BLUEWELL_BIN@',(data/'bluewell.fogbin').as_posix()).replace('@X3M_FOG_FOGGREENOUTLANDS_BIN@',(data/'foggreenoutlands.fogbin').as_posix())
    (out/'fog-fields.rc').write_text(text);subprocess.run(['i686-w64-mingw32-windres','-I',str(data),str(out/'fog-fields.rc'),'-O','coff','-o',str(out/'fog-fields.o')],check=True)
    for name in ('fog_pass.h','fog_volume_math.h','fog_pass_math.h'):
        if sha(base/'src/renderer'/name)!=sha(prod/'src/renderer'/name):raise ValueError('reused spatial header differs from production: '+name)
    sources=[ROOT/'verification/probe/fog_route_bridge.cpp',prod/'src/renderer/fog_pass.cpp',prod/'src/fog/fog_density_cache.cpp',prod/'src/fog/fog_density_generator.cpp',prod/'src/renderer/fog_field_assets.cpp']
    flags=['-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-msse2','-mfpmath=sse','-mstackrealign','-mincoming-stack-boundary=2','-static','-DX3M_FOG_PASS_FIXTURE',*(['-DX3M_ROUTE_BRIDGE_BASELINE'] if a.baseline else [])]
    command=['i686-w64-mingw32-g++',*flags,'-I'+str(out),'-I'+str(prod/'src/proxy'),'-I'+str(prod/'src/renderer'),'-I'+str(prod/'src/fog'),'-I'+str(data),'-DX3M_FOG_SPATIAL_BASE="'+str(base/'verification/probe/fog_spatial_fixture.cpp')+'"',*map(str,sources),str(out/'fog-fields.o'),'-o',str(exe),'-luser32','-ldxguid']
    inputs=[*sources,ROOT/'verification/probe/fog_route_owner_inc.h',*([] if a.baseline else [ROOT/'verification/probe/fog_route_density_inc.h']),fragment,base/'verification/probe/fog_spatial_fixture.cpp',base/'verification/probe/fog_spatial_state_inc.h',prod/'src/proxy/fog_sector_policy.h',prod/'src/renderer/fog_pass.h',prod/'src/renderer/fog_field_assets.h',*asset_inputs(data)]
    inputs.extend(prod/'src/renderer'/name for name in ('fog_volume_math.h','fog_pass_math.h','fog_march_program_inc.h','fog_composite_program_inc.h','quad_vertex_program.h','quad_vertex_program_inc.h','shadow_replay_projection.h'))
    # Bind the small actual include closure, including relative includes from
    # reused fixture helpers and the unchanged copied production fragment.
    inputs += [out/'fog_route_methods_inc.h',out/'fog-fields.rc',prod/'cmake/fog_field_assets.rc.in',Path(__file__).resolve(),ROOT/'verification/probe/fog_route_bridge_check.py']
    pending=list(inputs);bound=set()
    while pending:
        source=pending.pop().resolve()
        if source in bound:continue
        bound.add(source)
        if source.suffix not in ('.h','.cpp'):continue
        for name in re.findall(r'^\s*#include\s+"([^"\n]+)"',source.read_text(),re.M):
            candidates=[source.parent/name,out/name,prod/'src/proxy'/name,prod/'src/renderer'/name,prod/'src/fog'/name,data/name]
            found=next((p.resolve() for p in candidates if p.is_file()),None)
            if found is None and a.baseline and name in ('fog_route_density_inc.h','fog_density_cache.h','fog_prefill.h','../renderer/gpu_sync_timing_core.h'):continue # compiled out by X3M_ROUTE_BRIDGE_BASELINE (fog_route_owner_inc.h)
            if found is None:raise ValueError('unbound local include: '+str(source)+' '+name)
            pending.append(found)
    inputs=sorted(bound)
    before={str(p):sha(p) for p in inputs}
    subprocess.run(command,check=True)
    if before!={str(p):sha(p) for p in inputs}:raise ValueError('source changed during fixture build')
    (out/'build.json').write_text(json.dumps({'baseline':a.baseline,'executable':str(exe),'sha256':sha(exe),'command':command,'inputs':before},indent=2)+'\n');print(exe)
if __name__=='__main__':main()
