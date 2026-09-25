#!/usr/bin/env python3
"""D3DX slot counts ("approximately N instruction slots used") of the camera-gate hold program before (651c2f42) and after the
far clip, and of the far program, through D3DXDisassembleShader of the bottle's d3dx9_37.dll (disasm_slots.cpp beside this).
CPU-only, no D3D device. Run: X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/results/far-clip-7x7/run_slots.py"""
import os, re, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import bottle  # noqa: E402
from game_guard import game_running  # noqa: E402
if game_running():
    raise SystemExit('X3AP running; postpone')
HOLD = 'src/renderer/temporal_resolve_far_camera_hold_program_inc.h'
with tempfile.TemporaryDirectory(prefix='x3-far-clip-slots-') as directory:
    work = Path(directory)
    sources = (('before', subprocess.run(['git', 'show', '651c2f42:' + HOLD], cwd=ROOT, check=True, capture_output=True, text=True).stdout),
               ('after', (ROOT / HOLD).read_text()), ('far', (ROOT / 'src/renderer/temporal_resolve_far_program_inc.h').read_text()))
    files = []
    for name, text in sources:
        words = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{8})u', text)]
        (work / (name + '.bin')).write_bytes(struct.pack('<%dI' % len(words), *words))
        files.append('Z:' + str(work / (name + '.bin')))
    exe = work / 'disasm_slots.exe'
    subprocess.run(['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-static', str(Path(__file__).with_name('disasm_slots.cpp')), '-o', str(exe)], check=True)
    subprocess.run([bottle.WINE, *bottle.wine_args(), '--dll', 'd3dx9_37=n', str(exe), 'Z:' + str(bottle.game_dir() / 'd3dx9_37.dll'), *files],
                   check=True, timeout=60, env=dict(os.environ, WINEDLLOVERRIDES='d3dx9_37=n'))
