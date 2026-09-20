#!/usr/bin/env python3
"""Prepare a pinned, unregistered x86 LAV diagnostic provider outside the game.

Only downloads the official release archive; never executes its installers or
registration scripts. The archive's SHA256 is pinned to GitHub's release digest.
Third-party binaries/headers stay in the requested local directory, not source.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import urllib.request
import zipfile

VERSION = '0.81'
COMMIT = 'c97e4049aff5d2ed86a2aa517b6a75357daf83b0'
URL = 'https://github.com/Nevcairiel/LAVFilters/releases/download/0.81/LAVFilters-0.81-x86.zip'
ARCHIVE_SHA256 = 'b74388c23ce5acac8d0242609fbc3c5cdd90da400e6dbc22eb334f3623bfde3a'
SOURCE_CLSID = '{B98D13E7-55DB-4385-A33D-09FD1BA26338}'
VIDEO_CLSID = '{EE30215D-164F-4A92-A4EB-9D4C13390F9F}'
BINARIES = ('LAVSplitter.ax', 'LAVVideo.ax', 'avcodec-lav-62.dll', 'avformat-lav-62.dll',
            'avutil-lav-60.dll', 'avfilter-lav-11.dll', 'swscale-lav-9.dll',
            'swresample-lav-6.dll', 'libbluray.dll')
SYSTEM = frozenset('ole32.dll user32.dll oleaut32.dll shlwapi.dll comctl32.dll d3d9.dll '
                  'kernel32.dll advapi32.dll shell32.dll version.dll msvcrt.dll bcrypt.dll '
                  'crypt32.dll ncrypt.dll ws2_32.dll'.split())

# Matched official release members, independently pinned against record edits.
PINNED_FILES = {'LAVSplitter.ax': '8fcd0823e407ada5d5d75ec37f16aa0a4e9980a56fad3bb0b1dac8c856a3358f', 'LAVVideo.ax': '84ac9e2f4da06d52518557cb8c3e04315c0364f01f822bde761e284d0b8d2cdd', 'avcodec-lav-62.dll': 'fc8ecc6c8f0b411e74a93a4183e1ec0b1f269fd17a84780ab9350f08b6ca9fb1', 'avformat-lav-62.dll': '441220c1ed4ed790dbc5f7a9b9b68dfff1d0f9a3499f24d69d2c16bce83cccc9', 'avutil-lav-60.dll': '20354cc20b71eb328ee68f27053bd2079dd900f9dc4b078b2d32b9d67c30d1e9', 'avfilter-lav-11.dll': 'edeb71ad745bc8c798528dd9140c0ddda6e1f6bbcd79a604652466a805eb3d78', 'swscale-lav-9.dll': 'c7e43cf3729efe5848e526cae3d6021f63b6fbe03edc6a18af601d2604c28224', 'swresample-lav-6.dll': 'd324ff9a1d7068a367cab1861d24dd31aedb1f844c73018070e9a64cf834055f', 'libbluray.dll': 'b520c3e262cbffc725e912e442ed76637c79b5cc8bd593ffbcb05547bcea5838', 'LAVFilters.Dependencies.manifest': 'a1f0031cfd528235d48d78425eef03913ecaadf2ea9e33f27c6416e75510731e', 'COPYING': '189b1af95d661151e054cea10c91b3d754e4de4d3fecfb074c1fb29476f7167b', 'README.md': 'e1aa1e99041f3009200307565085fdd1d77848d615d16c57eeb3c47622f8c664', 'include/LAVVideoSettings.h': 'a05a5369f5fd7478c76a9c814a8927e7240833338e89cd60ce58219e206af72e', 'include/LAVSplitterSettings.h': '0a3bfa85874e18c33bc27fcb2cef3edebfc0072cc7116d732a224f54cbf2f804'}

def sha(data):
    return hashlib.sha256(data).hexdigest()


def manifest():
    files = '\n'.join('  <file name="'+name+'" />' for name in BINARIES[2:])
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
            '<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">\n'
            '  <assemblyIdentity type="win32" name="X3.MediaFixture.LAV" version="0.81.0.0" processorArchitecture="x86" />\n'
            '  <file name="LAVSplitter.ax"><comClass clsid="'+SOURCE_CLSID+'" threadingModel="Both" /></file>\n'
            '  <file name="LAVVideo.ax"><comClass clsid="'+VIDEO_CLSID+'" threadingModel="Both" /></file>\n'+files+'\n</assembly>\n')


def inspect_pe(path, objdump):
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if data[offset:offset+4] != b'PE\0\0' or struct.unpack_from('<H', data, offset+4)[0] != 0x14c:
        raise ValueError('provider is not x86 PE: '+path.name)
    output = subprocess.check_output([objdump, '-p', str(path)], text=True, timeout=20)
    dependencies = re.findall(r'DLL Name: ([^\r\n]+)', output)
    allowed = SYSTEM | {n.lower() for n in BINARIES}
    if not dependencies or any(n.lower() not in allowed for n in dependencies):
        raise ValueError('unresolved provider imports: '+path.name)
    return dict(sha256=sha(data), bytes=len(data), machine='I386', imports=dependencies)


def prepare(output, archive=None, objdump='i686-w64-mingw32-objdump'):
    if output.exists():
        raise ValueError('output must be a new private directory')
    data = archive.read_bytes() if archive else urllib.request.urlopen(URL, timeout=30).read(32*1024*1024)
    if sha(data) != ARCHIVE_SHA256:
        raise ValueError('official archive hash mismatch')
    names = (*BINARIES, 'LAVFilters.Dependencies.manifest', 'COPYING', 'README.md',
             'include/LAVVideoSettings.h', 'include/LAVSplitterSettings.h')
    package = zipfile.ZipFile(io.BytesIO(data))
    output.mkdir(parents=True)
    for name in names:
        target = output/name
        target.parent.mkdir(exist_ok=True)
        target.write_bytes(package.read(name))
    (output/'provider.manifest').write_text(manifest())
    binaries = {name: inspect_pe(output/name, objdump) for name in BINARIES}
    files = {name: sha((output/name).read_bytes()) for name in (*names, 'provider.manifest')}
    record = dict(schema=1, provider='LAVFilters', version=VERSION, architecture='x86',
                  release_url=URL, archive_sha256=ARCHIVE_SHA256, source_commit=COMMIT,
                  files=files, binaries=binaries, classes=dict(source=SOURCE_CLSID, video=VIDEO_CLSID),
                  threading_model='Both', runtime_only_settings=True, distribution_qualified=False,
                  source_evidence=['demuxer/LAVSplitter/LAVSplitter.h', 'decoder/LAVVideo/LAVVideo.h',
                                   'common/baseclasses/dllsetup.cpp'],
                  excluded=['LAVAudio.ax', 'IntelQuickSyncDecoder.dll', 'registration scripts'],
                  limitation='Static PE imports only; runtime loaded-module closure must be checked.')
    (output/'provider.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


def verify_record(path):
    data = json.loads(path.read_text())
    if (data.get('schema') != 1 or data.get('archive_sha256') != ARCHIVE_SHA256 or
            data.get('source_commit') != COMMIT or data.get('architecture') != 'x86' or
            data.get('classes') != dict(source=SOURCE_CLSID, video=VIDEO_CLSID)):
        raise ValueError('unrecognized provider provenance')
    if any(data.get('files', {}).get(k) != v for k, v in PINNED_FILES.items()):
        raise ValueError('provider members do not match pinned official archive')
    required = set(BINARIES) | {'provider.manifest', 'LAVFilters.Dependencies.manifest',
                              'include/LAVVideoSettings.h', 'include/LAVSplitterSettings.h'}
    if not required <= data.get('files', {}).keys():
        raise ValueError('incomplete provider closure')
    for name, expected in data['files'].items():
        target = path.parent/name
        if not target.resolve().is_relative_to(path.parent.resolve()) or sha(target.read_bytes()) != expected:
            raise ValueError('provider file changed: '+name)
    if (path.parent/'provider.manifest').read_text() != manifest():
        raise ValueError('activation manifest changed')
    return data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--archive', type=Path)
    p.add_argument('--objdump', default='i686-w64-mingw32-objdump')
    a = p.parse_args()
    print(json.dumps(prepare(a.output.resolve(), a.archive, a.objdump), indent=2))


if __name__ == '__main__':
    main()
