#!/usr/bin/env python3
"""Host-only actual FogPass fixture build; refuses stale embedded shaders."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def shaders_current():
    records={}
    for name in ('march','composite'):
        source=ROOT/f'src/fog/fog_{name}_ps.hlsl';header=ROOT/f'src/renderer/fog_{name}_program_inc.h'
        record=json.loads((ROOT/f'verification/results/fog-{name}-program.json').read_text())
        if record['source_sha256']!=digest(source) or record['header_sha256']!=digest(header):raise ValueError(f'{name}: stale embedded shader; root must run the existing shader generator')
        includes=record.get('includes') or {}
        if 'src/fog/fog_field_inc.h' not in includes or any(digest(ROOT/p)!=h for p,h in includes.items()):raise ValueError(f'{name}: stale shader includes')
        records[name]=record
    return records

def asset_inputs(data):
    manifest=json.loads((data/'manifest.json').read_text())
    paths=[data/'manifest.json',data/'fog_field_assets_metadata_inc.h',data/'fog_field_assets_resource_inc.h']
    if (data/'fog_field_assets_entries.rc').exists():paths.append(data/'fog_field_assets_entries.rc')
    return paths+[data/(row['name']+'.fogbin') for row in manifest['profiles']]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--asset-root',type=Path,required=True);ap.add_argument('--asset-data',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if (out/'fog_spatial_fixture.exe').exists():raise ValueError('refuse overwrite of existing fixture executable; choose a new output prefix')
    shaders=shaders_current();asset=a.asset_root.resolve();data=a.asset_data.resolve()
    text=(asset/'cmake/fog_field_assets.rc.in').read_text().replace('@X3M_FOG_BLUEWELL_BIN@',(data/'bluewell.fogbin').as_posix()).replace('@X3M_FOG_FOGGREENOUTLANDS_BIN@',(data/'foggreenoutlands.fogbin').as_posix())
    rc=out/'fog-fields.rc';rc.write_text(text)
    subprocess.run(['i686-w64-mingw32-windres','-I',str(data),str(rc),'-O','coff','-o',str(out/'fog-fields.o')],check=True)
    flags=['-std=c++17','-O2','-Wall','-Wextra','-Werror','-msse2','-mfpmath=sse','-mstackrealign','-mincoming-stack-boundary=2','-static','-DX3M_FOG_PASS_FIXTURE']
    sources=[ROOT/'verification/probe/fog_spatial_fixture.cpp',ROOT/'src/renderer/fog_pass.cpp',asset/'src/renderer/fog_field_assets.cpp']
    command=['i686-w64-mingw32-g++',*flags,'-I'+str(asset/'src/renderer'),'-I'+str(data),*map(str,sources),str(out/'fog-fields.o'),'-o',str(out/'fog_spatial_fixture.exe'),'-luser32']
    subprocess.run(command,check=True)
    inputs=[*sources,ROOT/'verification/probe/fog_spatial_state_inc.h',ROOT/'src/renderer/fog_pass.h',ROOT/'src/renderer/fog_volume_math.h',asset/'src/renderer/fog_field_assets.h',*asset_inputs(data)]
    for name in ('march','composite'):inputs.extend([ROOT/f'src/fog/fog_{name}_ps.hlsl',ROOT/f'src/renderer/fog_{name}_program_inc.h'])
    inputs.append(ROOT/'src/fog/fog_field_inc.h')
    record=dict(executable_sha256=digest(out/'fog_spatial_fixture.exe'),inputs={str(p):digest(p) for p in inputs},command=command,shaders=shaders)
    (out/'build.json').write_text(json.dumps(record,indent=2)+'\n');print(out/'fog_spatial_fixture.exe')
if __name__=='__main__':main()
