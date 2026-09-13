#!/usr/bin/env python3
"""Build the production bridge with the reviewed synthetic CPU harness; no Wine."""
from pathlib import Path
import importlib.util
import json
import os
import subprocess
ROOT=Path(__file__).resolve().parents[2]
PROBE=ROOT/'verification/probe'
BUILD=PROBE/'build/compositor_bridge'
spec=importlib.util.spec_from_file_location('compositor_builder',ROOT/'tools/build/build_compositor_bridge.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)

def run(args):subprocess.run([str(a) for a in args],cwd=ROOT,check=True)

def main():
    run(['python3',ROOT/'tools/build/build_compositor_bridge.py','--output-dir',BUILD,'--gnu-pe-no-safeseh'])
    # Reuse only the original synthetic harness + its FP macros; do not compile
    # the old prototype entry/invoke implementation into this executable.
    old=(PROBE/'bloom_return_bridge.S').read_text()
    macros=old[old.index('.macro save_fp'):old.index('.globl _bloom_return_bridge')]
    harness=old[old.index('/* Synthetic harness'):]
    marker='    call *(%esp)\n    pushfl'
    if harness.count(marker)!=1:raise RuntimeError('Reviewed harness call boundary changed')
    harness=harness.replace(marker,'    call *(%esp)\n.globl _compositor_fixture_caller_pc\n_compositor_fixture_caller_pc:\n    pushfl')
    text='#include "bloom_return_bridge.h"\n.text\n'+macros+harness+'\n.section .rdata,"dr"\n.align 4\n_bloom_return_default_mxcsr: .long 0x1f80\n'
    asm=BUILD/'compositor_bridge_fixture_generated.S';asm.write_text(text)
    clang=os.environ.get('X3M_SEH_CLANG','/usr/bin/clang')
    outer=BUILD/'compositor_bridge_fixture_seh.obj'
    run([clang,*builder.FLAGS,'-c',PROBE/'compositor_bridge_fixture_seh.c','-o',outer])
    outer_link=BUILD/'compositor_bridge_fixture_seh_gnu.obj'
    run(['i686-w64-mingw32-objcopy','--remove-section=.sxdata',outer,outer_link])
    exe=BUILD/'compositor_bridge.exe'
    run(['i686-w64-mingw32-gcc','-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-msse2','-mfpmath=sse','-mstackrealign','-mincoming-stack-boundary=2','-I'+str(PROBE),PROBE/'compositor_bridge_fixture.c',asm,ROOT/'src/proxy/compositor_bridge.S',BUILD/'compositor_bridge_seh_gnu.obj',outer_link,BUILD/'libx3m_compositor_seh_runtime.a','-o',exe])
    imports=subprocess.check_output(['i686-w64-mingw32-objdump','-p',str(exe)],text=True)
    (BUILD/'compositor_bridge_imports.txt').write_text(imports)
    # The narrow library must not redirect general CRT routines into msvcrt.
    block=imports.split('DLL Name: msvcrt.dll\n',1)
    if len(block)!=2:raise RuntimeError('Missing explicit compiler runtime import')
    block=block[1].split('DLL Name:',1)[0]
    imported=[line.split()[-1] for line in block.splitlines() if '<none>' in line]
    if imported!=['_except_handler3']:raise RuntimeError('Broad msvcrt imports: '+repr(imported))
    with (BUILD/'compositor_bridge_disassembly.txt').open('w') as out:
        subprocess.run(['i686-w64-mingw32-objdump','-dr',str(exe)],stdout=out,check=True)
    # Packaging smoke DLL isolates the production bridge's real imports from
    # fixture startup/stdio. It is never installed or loaded by this builder.
    asm_obj=BUILD/'compositor_bridge.o'
    run(['i686-w64-mingw32-gcc','-c','-msse2','-mfpmath=sse','-mstackrealign','-mincoming-stack-boundary=2',ROOT/'src/proxy/compositor_bridge.S','-o',asm_obj])
    package=BUILD/'compositor_bridge_package.dll'
    run(['i686-w64-mingw32-gcc','-shared','-nostdlib','-Wl,--entry,0',asm_obj,BUILD/'compositor_bridge_seh_gnu.obj',BUILD/'libx3m_compositor_seh_runtime.a','-lkernel32','-o',package])
    package_imports=subprocess.check_output(['i686-w64-mingw32-objdump','-p',str(package)],text=True)
    (BUILD/'compositor_bridge_package_imports.txt').write_text(package_imports)
    import re
    modules=re.findall(r'DLL Name: (\S+)',package_imports)
    functions=[line.split()[-1] for line in package_imports.splitlines() if '<none>' in line]
    if set(modules)!={'KERNEL32.dll','msvcrt.dll'} or set(functions)!={'GetLastError','SetLastError','_except_handler3'}:
        raise RuntimeError('Unexpected production packaging imports: '+repr((modules,functions)))
    if '.sxdata' in subprocess.check_output(['i686-w64-mingw32-objdump','-h',str(package)],text=True):
        raise RuntimeError('GNU link retained unconsumed SafeSEH metadata')
    report=json.loads((BUILD/'compositor_bridge_build.json').read_text())
    report['packaging_smoke_dll_sha256']=builder.sha(package)
    report['packaging_smoke_imports']=dict(modules=modules,functions=functions)
    report['fixture_exe_sha256']=builder.sha(exe)
    report['fixture_msvcrt_imports']=imported
    (BUILD/'compositor_bridge_fixture_build.json').write_text(json.dumps(report,indent=2)+'\n')
    print(exe)
if __name__=='__main__':main()
