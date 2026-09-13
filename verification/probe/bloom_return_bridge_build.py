#!/usr/bin/env python3
"""Build only; never executes Wine or the game. Clang emits the SEH frame."""
from pathlib import Path
import os
import subprocess
ROOT=Path(__file__).resolve().parents[2]
PROBE=ROOT/'verification/probe'
BUILD=PROBE/'build'

def run(args):
    subprocess.run(args,cwd=ROOT,check=True)

def main():
    BUILD.mkdir(exist_ok=True)
    cc=os.environ.get('X3M_SEH_CLANG','/usr/bin/clang')
    seh=BUILD/'bloom_return_bridge_seh.obj'
    common=['-target','i686-pc-windows-msvc','-fms-extensions','-O2','-Wall','-Wextra','-Werror','-msse2','-mfpmath=sse','-mstackrealign','-mstack-alignment=4']
    run([cc,*common,'-c',str(PROBE/'bloom_return_bridge_seh.c'),'-o',str(seh)])
    run([cc,*common,'-S',str(PROBE/'bloom_return_bridge_seh.c'),'-o',str(BUILD/'bloom_return_bridge_seh.s')])
    # GNU PE ld does not consume MS COFF symbol-index SafeSEH metadata. Keep the
    # original object for inspection; omit metadata only in its link copy.
    # Runtime scope table/handler/unwind code are untouched. No SafeSEH claim.
    link_obj=BUILD/'bloom_return_bridge_seh_link.obj'
    run(['i686-w64-mingw32-objcopy','--remove-section=.sxdata',str(seh),str(link_obj)])
    run(['i686-w64-mingw32-gcc','-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-msse2','-mfpmath=sse','-mstackrealign','-mincoming-stack-boundary=2',str(PROBE/'bloom_return_bridge_fixture.c'),str(PROBE/'bloom_return_bridge.S'),str(link_obj),'-o',str(BUILD/'bloom_return_bridge.exe'),'-lmsvcrt-os'])
    with (BUILD/'bloom_return_bridge_disassembly.txt').open('w') as out:
        subprocess.run(['i686-w64-mingw32-objdump','-dr',str(BUILD/'bloom_return_bridge.exe')],check=True,stdout=out)
    print(BUILD/'bloom_return_bridge.exe')

if __name__=='__main__':main()
