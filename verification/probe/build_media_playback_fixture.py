#!/usr/bin/env python3
"""Build only the standalone fixture. Does not build a proxy or execute Wine."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from prepare_lav_fixture import verify_record

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-municode',
         '-static', '-static-libgcc', '-static-libstdc++']
LIBS = ['-lamstrmid', '-lstrmiids', '-luuid', '-ld3d9', '-lole32', '-loleaut32', '-lversion']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=ROOT/'build/verification/media-playback/media_playback_fixture.exe')
    p.add_argument('--compiler', default='i686-w64-mingw32-g++')
    p.add_argument('--lav-provider-record', type=Path, help='optional pinned local public headers for explicit LAV fixture variant')
    a = p.parse_args()
    a.output = a.output.resolve()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    source = ROOT/'verification/probe/media_playback_fixture.cpp'
    extra = []
    provider = None
    if a.lav_provider_record:
        a.lav_provider_record = a.lav_provider_record.resolve()
        provider = verify_record(a.lav_provider_record)
        extra = ['-DX3_FIXTURE_LAV=1', '-isystem', str(a.lav_provider_record.parent/'include')]
    command = [a.compiler, *FLAGS, *extra, str(source), '-o', str(a.output), *LIBS]
    subprocess.run(command, check=True)
    record = dict(schema=1, command=command, toolchain=subprocess.check_output([a.compiler, '--version'], text=True).splitlines()[0],
                  source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  exe_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(), exe=str(a.output))
    record['lav_helper_sha256'] = hashlib.sha256((source.parent/'media_lav_fixture_inc.h').read_bytes()).hexdigest()
    record['lav_provider'] = provider
    a.output.with_suffix('.build.json').write_text(json.dumps(record, indent=2)+'\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
