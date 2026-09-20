#!/usr/bin/env python3
"""Fetch only public FFmpeg headers pinned by LAV 0.81; no install or execution."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import urllib.request

COMMIT = '9e0d177b545e467afaae609e5dafff99c017dbba'
BASE = 'https://gitea.1f0.de/LAV/FFmpeg/raw/commit/'+COMMIT+'/'
ROOTS = ('libavformat/avformat.h', 'libavcodec/avcodec.h', 'libavutil/sha.h',
         'libavutil/imgutils.h', 'libavutil/pixdesc.h')
# configure emits these public architecture constants for x86. No private config.h.
AVCONFIG = b'#ifndef AVUTIL_AVCONFIG_H\n#define AVUTIL_AVCONFIG_H\n#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 1\n#endif\n'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(path):
    data = json.loads(path.read_text())
    if data.get('commit') != COMMIT or data.get('architecture') != 'x86':
        raise ValueError('wrong public header provenance')
    for name, value in data['files'].items():
        if '..' in PurePosixPath(name).parts or name.startswith('/') or digest(path.parent/name) != value:
            raise ValueError('header changed: '+name)
    if (path.parent/'libavutil/avconfig.h').read_bytes() != AVCONFIG:
        raise ValueError('wrong public x86 avconfig')
    return data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args(); root = a.output.resolve(); root.mkdir(parents=True, exist_ok=True)
    if (root/'headers.json').exists():
        print(json.dumps(verify(root/'headers.json'))); return
    pending = set(ROOTS); seen = {}
    def fetch(name):
        if name == 'libavutil/avconfig.h': return name, AVCONFIG
        try:
            with urllib.request.urlopen(BASE+name, timeout=20) as response:
                return name, response.read(1048576)
        except Exception as error: raise RuntimeError(name) from error
    while pending:
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            values = list(pool.map(fetch, sorted(pending)))
        pending = set()
        for name, raw in values:
            dest = root/name; dest.parent.mkdir(parents=True, exist_ok=True); dest.write_bytes(raw)
            seen[name] = hashlib.sha256(raw).hexdigest()
            for include in re.findall(r'^\s*#\s*include\s+"([^"]+)"', raw.decode(), re.M):
                if include == 'config.h': continue  # guarded by HAVE_AV_CONFIG_H; public consumer never defines it
                child = include if include.startswith('libav') else str(PurePosixPath(name).parent/include)
                if '..' in PurePosixPath(child).parts: raise ValueError('unexpected include traversal')
                if child not in seen: pending.add(child)
        pending -= seen.keys()
    record = dict(schema=1, commit=COMMIT, base_url=BASE, architecture='x86', files=seen,
                  generated_avconfig='configure public x86 constants: little endian, fast unaligned access',
                  private_headers=False)
    (root/'headers.json').write_text(json.dumps(record, indent=2)+'\n')
    verify(root/'headers.json'); print(json.dumps(dict(headers=len(seen), record=str(root/'headers.json'))))


if __name__ == '__main__': main()
