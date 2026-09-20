#!/usr/bin/env python3
"""Build only; root controls all fixture execution through wine_lock.py."""
import argparse
import json
import struct
from pathlib import Path
import shutil
import subprocess
ROOT = Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,default=ROOT/'verification/probe/build/media_destination')
parser.add_argument('--worker-header',type=Path,default=ROOT/'src/media/lav_worker.h')
a=parser.parse_args();a.output.mkdir(parents=True,exist_ok=True)
shutil.copyfile(a.worker_header,a.output/'lav_worker.h')
cxx=shutil.which('i686-w64-mingw32-g++')
flags=['-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread','-msse2','-mfpmath=sse','-mstackrealign','-mincoming-stack-boundary=2',f'-I{a.output}',f'-I{ROOT/"src/media"}']
sources=['src/ownership/application_admission.cpp','src/ownership/application_admission_abi.cpp','src/ownership/d3d9_ownership.cpp','src/ownership/execution_state.cpp','src/ownership/finite_buffer_evidence.cpp','src/ownership/portable_managed_upload.cpp','src/proxy/media_presentation_gate.cpp','src/proxy/media_presentation_gate_win32.cpp','src/proxy/media_destination.cpp','verification/probe/media_destination_native.cpp','verification/probe/media_destination_x86.cpp']
objects=[]
for source in sources:
    output=a.output/(Path(source).stem+'.o');extra=['-fno-exceptions'] if Path(source).name in ['application_admission_abi.cpp','media_destination_x86.cpp'] else []
    subprocess.run([cxx,*flags,*extra,'-c',str(ROOT/source),'-o',str(output)],check=True);objects.append(str(output))
executable=a.output/'media_destination_native.exe'
subprocess.run([cxx,*flags,'-static','-Wl,--image-base,0x400000','-Wl,--disable-dynamicbase','-Wl,--section-start,.x3map=0x401000','-Wl,--section-start,.text=0x630000',*objects,'-o',str(executable),'-ldxguid','-luser32','-ladvapi32'],check=True)
def audit_image(path):
    data=path.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0];opt=pe+24
    assert data[:2]==b'MZ' and data[pe:pe+4]==b'PE\0\0'
    machine,count=struct.unpack_from('<HH',data,pe+4);optional_size=struct.unpack_from('<H',data,pe+20)[0]
    base=struct.unpack_from('<I',data,opt+28)[0];entry=base+struct.unpack_from('<I',data,opt+16)[0];extent=struct.unpack_from('<I',data,opt+56)[0]
    assert machine==0x14c and base==0x400000 and not struct.unpack_from('<H',data,opt+70)[0]&0x40
    sections=[]
    for i in range(count):
        at=opt+optional_size+i*40;name=data[at:at+8].rstrip(b'\0').decode('ascii')
        size,rva,raw_size,raw=struct.unpack_from('<IIII',data,at+8);flags=struct.unpack_from('<I',data,at+36)[0]
        sections.append((name,base+rva,size,raw_size,raw,flags))
    maps=[s for s in sections if s[0]=='.x3map'];assert len(maps)==1
    synthetic=maps[0];_,va,size,raw_size,raw,flags=synthetic
    assert (va,size)==(0x401000,0x21f000) and flags&0x80000000 and not flags&0x20000000
    assert len(data[raw:raw+raw_size])==raw_size and not any(data[raw:raw+raw_size])
    ordered=sorted((s for s in sections if s[2]),key=lambda s:s[1])
    assert all(left[1]+max(left[2],left[3])<=right[1] for left,right in zip(ordered,ordered[1:]))
    assert all(s is synthetic or not s[2] or (s[1]>=0x620000 and s[1]+max(s[2],s[3])<=base+extent) for s in sections)
    text=[s for s in sections if s[0]=='.text'];assert len(text)==1 and text[0][1]<=entry<text[0][1]+text[0][2]
    # Covers all19 original entries/continuations, relocated CALL/Jcc targets,
    # table globals, mock wrapper/record cells and initialization memset ranges.
    for address,length in [(0x490000,0x90000),(0x600000,0x20000)]:assert va<=address and address+length<=va+size
    return {'image_base':hex(base),'owned_map':[hex(va),hex(va+size)],'authored_ranges':[['0x490000','0x520000'],['0x600000','0x620000']],'real_text':hex(text[0][1]),'sections_nonoverlapping':True,'synthetic_bytes_zero':True,'synthetic_initially_nonexecutable':True}
(a.output/'image-audit.json').write_text(json.dumps(audit_image(executable),indent=2)+'\n')
print(executable)
