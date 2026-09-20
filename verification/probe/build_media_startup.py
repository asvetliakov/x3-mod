#!/usr/bin/env python3
"""Build only; parent owns locked Wine execution. Never launches game."""
import json
from pathlib import Path
import subprocess
import struct
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/verification/media-startup'
FLAGS=('-std=c++17','-O2','-Wall','-Wextra','-Werror','-fno-exceptions','-msse2','-mfpmath=sse',
       '-mstackrealign','-mincoming-stack-boundary=2','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX')
MODES=('success','unknown','failed','reentrant','device_inside','late','identity','anchor','callbackfail')
def audit_image(path):
    data=Path(path).read_bytes()
    pe=struct.unpack_from('<I',data,0x3c)[0];opt=pe+24
    if data[:2]!=b'MZ' or data[pe:pe+4]!=b'PE\0\0':raise ValueError('not PE')
    machine,count=struct.unpack_from('<HH',data,pe+4)
    optional_size=struct.unpack_from('<H',data,pe+20)[0]
    base=struct.unpack_from('<I',data,opt+28)[0]
    entry=base+struct.unpack_from('<I',data,opt+16)[0]
    extent=struct.unpack_from('<I',data,opt+56)[0]
    dynamic=bool(struct.unpack_from('<H',data,opt+70)[0]&0x40)
    if machine!=0x14c or base!=0x400000 or dynamic:raise ValueError('fixed x86 image layout required')
    sections=[]
    for i in range(count):
        at=opt+optional_size+i*40
        name=data[at:at+8].rstrip(b'\0').decode('ascii')
        size,rva,raw_size,raw=struct.unpack_from('<IIII',data,at+8)
        flags=struct.unpack_from('<I',data,at+36)[0]
        sections.append((name,base+rva,size,raw_size,raw,flags))
    maps=[s for s in sections if s[0]=='.x3map']
    if len(maps)!=1:raise ValueError('one owned synthetic section required')
    synthetic=maps[0];_,va,size,raw_size,raw,flags=synthetic
    if (va,size)!=(0x401000,0x21f000) or not flags&0x80000000 or flags&0x20000000:raise ValueError('synthetic section extent/initial protection')
    if len(data[raw:raw+raw_size])!=raw_size or any(data[raw:raw+raw_size]):raise ValueError('synthetic section contains real fixture data')
    ordered=sorted((s for s in sections if s[2]),key=lambda s:s[1])
    for left,right in zip(ordered,ordered[1:]):
        if left[1]+max(left[2],left[3])>right[1]:raise ValueError('overlapping PE sections')
    for s in sections:
        if s is synthetic or not s[2]:continue
        if s[1]<0x620000 or s[1]+max(s[2],s[3])>base+extent:raise ValueError('real section overlaps authored addresses')
    text=[s for s in sections if s[0]=='.text']
    if len(text)!=1 or not text[0][1]<=entry<text[0][1]+text[0][2]:raise ValueError('entry outside real text')
    for at,length in ((0x402edc,6),(0x4d8470,64),(0x4faedc,6),(0x532314,4),(0x608b3c,4)):
        if at<va or at+length>va+size:raise ValueError('authored span outside owned map')
    return {'fixed_image_base':hex(base),'owned_map':[hex(va),hex(va+size)],
            'real_text':hex(text[0][1]),'sections_nonoverlapping':True,'synthetic_bytes_zero':True,
            'synthetic_initially_nonexecutable':True,'production_addresses_unchanged':True}

def build():
    BUILD.mkdir(parents=True,exist_ok=True)
    exe=BUILD/'media_startup_fixture.exe'
    subprocess.run(['i686-w64-mingw32-g++',*FLAGS,
        str(ROOT/'verification/probe/media_startup_cpu_fixture.cpp'),str(ROOT/'src/proxy/media_startup.cpp'),
        '-static','-static-libgcc','-static-libstdc++','-Wl,--image-base,0x400000',
        '-Wl,--disable-dynamicbase','-Wl,--section-start,.x3map=0x401000',
        '-Wl,--section-start,.text=0x630000','-o',str(exe)],check=True,cwd=ROOT)
    return {'binary':str(exe),'modes':MODES,'runtime':'not run','image_audit':audit_image(exe),'scope':'actual export boundary/Windows scheduling; authored image anchors/backend/service'}
if __name__=='__main__':print(json.dumps(build(),indent=2))
