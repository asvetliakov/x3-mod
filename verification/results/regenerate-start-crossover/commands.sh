#!/bin/sh
# Probes run 2026-09-28 (bottle X3, CrossOver Preview 20260821, xtajit64 = FEX-2604-755-g80951b9).
# Each probe ran only after game_guard.game_running() printed [] (separate command).
# S = session scratchpad; regen-empty = empty dir so nothing bakes. Traces stay local (untracked).
W="/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine"
# F  : failing spelling, file+reg+module+seh trace
CX_DEBUGMSG=+file,+reg,+module,+seh,+loaddll X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py "$W" --bottle X3 'C:\X3\x3m-regenerate.exe' --game-dir "Z:$S/regen-empty" --no-wait > $S/regen-probe-F.txt 2>&1   # exit 29
# FZ : same file by its Z: spelling
CX_DEBUGMSG=+file,+module,+seh,+loaddll X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py "$W" --bottle X3 'Z:\Users\asvetl\Library\Application Support\CrossOver\Bottles\X3\drive_c\X3\x3m-regenerate.exe' --game-dir "Z:$S/regen-empty" --no-wait > $S/regen-probe-FZ.txt 2>&1   # exit 1 (designed refusal)
# P1 : XDG_CONFIG_HOME override (not propagated to the Windows process: still Y:\.config\fex-emu, still crashes)
XDG_CONFIG_HOME=$S/xdg-empty CX_DEBUGMSG=+file X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py "$W" --bottle X3 'C:\X3\x3m-regenerate.exe' --game-dir "Z:$S/regen-empty" --no-wait > $S/regen-probe-P1.txt 2>&1   # exit 29
# P2 : byte-identical copy at C:\X3\q\ (filename at string offset 8), folder removed afterwards
CX_DEBUGMSG=+file X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py "$W" --bottle X3 'C:\X3\q\x3m-regenerate.exe' --game-dir "Z:$S/regen-empty" --no-wait > $S/regen-probe-P2.txt 2>&1   # exit 1 (designed refusal)
# Disassembly: objdump -d --no-show-raw-insn xtajit64.dll > xtajit64.dis ; python3 analyse.py
