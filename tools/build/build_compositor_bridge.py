#!/usr/bin/env python3
"""Cross-compile the isolated x86 SEH unit; no DLL integration or execution.

GNU PE ld cannot consume MS COFF SafeSEH symbol-index metadata. Producing a
GNU-linkable copy requires explicit --gnu-pe-no-safeseh acknowledgment; the
original compiler object is always retained. This does not disable DEP/ASLR.
A narrow msvcrt import library supplies only compiler-generated _except_handler3,
without placing broad -lmsvcrt-os ahead of the project's existing UCRT libraries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'src/proxy/compositor_bridge_seh.c'
HEADER=ROOT/'src/proxy/compositor_bridge.h'
FLAGS=['-target','i686-pc-windows-msvc','-fms-extensions','-O2','-Wall','-Wextra','-Werror',
       '-msse2','-mfpmath=sse','-mstackrealign','-mstack-alignment=4']

def run(args):
    subprocess.run([str(a) for a in args],cwd=ROOT,check=True)

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def sections(path):
    """COFF section bytes; used to prove metadata removal leaves code/data exact."""
    data=Path(path).read_bytes()
    machine,count,_,symbols,symbol_count,optional,_=struct.unpack_from('<HHIIIHH',data)
    strings=symbols+18*symbol_count
    if machine!=0x14c or optional!=0:raise ValueError('Expected i386 COFF object')
    result={}
    for i in range(count):
        start=20+40*i
        name=data[start:start+8].split(b'\0',1)[0].decode('ascii')
        if name.startswith('/'):
            name=data[strings+int(name[1:]):].split(b'\0',1)[0].decode('ascii')
        size,offset=struct.unpack_from('<II',data,start+16)
        result[name]=data[offset:offset+size] if offset else bytes(size)
    return result

def relocations(path, objdump='i686-w64-mingw32-objdump'):
    output=subprocess.check_output([objdump,'-r',str(path)],text=True)
    return '\n'.join(line for line in output.splitlines()
                     if 'file format' not in line and str(path) not in line).strip()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',required=True,type=Path)
    parser.add_argument('--gnu-pe-no-safeseh',action='store_true')
    parser.add_argument('--clang',default=os.environ.get('X3M_SEH_CLANG','/usr/bin/clang'))
    # CMake passes its cross-toolchain paths; direct fixture users keep the
    # established prefixed tools from PATH.
    for tool in ('nm','objcopy','objdump','dlltool'):
        parser.add_argument('--'+tool,default='i686-w64-mingw32-'+tool)
    args=parser.parse_args()
    out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    raw=out/'compositor_bridge_seh.obj';assembly=out/'compositor_bridge_seh.s'
    run([args.clang,*FLAGS,'-c',SOURCE,'-o',raw])
    run([args.clang,*FLAGS,'-S',SOURCE,'-o',assembly])
    undefined=subprocess.check_output([args.nm,'-u',str(raw)],text=True)
    names={line.split()[-1] for line in undefined.splitlines() if line.strip()}
    if names!={'__except_handler3','_x3m_compositor_bridge_invoke'}:
        raise RuntimeError('Unexpected isolated SEH dependencies: '+repr(names))
    raw_sections=sections(raw)
    if '.xdata' not in raw_sections or '.sxdata' not in raw_sections:
        raise RuntimeError('Missing compiler-generated SEH/SafeSEH metadata')
    products=[raw,assembly]
    if args.gnu_pe_no_safeseh:
        link=out/'compositor_bridge_seh_gnu.obj'
        run([args.objcopy,'--remove-section=.sxdata',raw,link])
        linked_sections=sections(link)
        if set(linked_sections)!=set(raw_sections)-{'.sxdata'}:
            raise RuntimeError('Link copy changed section inventory beyond .sxdata')
        # objcopy can zero-pad a code section to its declared alignment. Existing
        # bytes must remain exact; added bytes must be zero padding only.
        for name,value in linked_sections.items():
            before=raw_sections[name]
            if value[:len(before)]!=before or any(value[len(before):]):
                raise RuntimeError('Link copy changed section contents: '+name)
        if relocations(raw,args.objdump)!=relocations(link,args.objdump):raise RuntimeError('Link copy changed relocations')
        products.append(link)
    definition=out/'compositor_bridge_seh_runtime.def'
    definition.write_text('LIBRARY msvcrt.dll\nEXPORTS\n_except_handler3\n')
    library=out/'libx3m_compositor_seh_runtime.a'
    run([args.dlltool,'--input-def',definition,'--output-lib',library])
    products.extend([definition,library])
    report={'clang':subprocess.check_output([args.clang,'--version'],text=True).splitlines()[0],
            'flags':FLAGS,'source_hashes':{str(p.relative_to(ROOT)):sha(p) for p in (SOURCE,HEADER,Path(__file__).resolve())},
            'products':{p.name:sha(p) for p in products},'undefined_symbols':sorted(names),
            'gnu_pe_no_safeseh':args.gnu_pe_no_safeseh,
            'runtime_import':{'module':'msvcrt.dll','symbol':'_except_handler3'},
            'native_windows_verified':False,'production_dll_linked':False}
    (out/'compositor_bridge_build.json').write_text(json.dumps(report,indent=2)+'\n')
    print(out)
if __name__=='__main__':main()
