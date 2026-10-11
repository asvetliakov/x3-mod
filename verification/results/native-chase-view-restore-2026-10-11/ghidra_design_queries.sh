#!/bin/sh
# Ghidra queries behind docs/architecture/chase-view-restore-engine-state.md
# (2026-10-12). Read-only on the analysed project; raw decompiler/listing
# output stays local under the scratchpad (not tracked). Run from the repo root.
set -e
OUT=${1:-/tmp/x3-chase-restore-design}
mkdir -p "$OUT"
H="JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64 $HOME/ghidra_12.1.4_PUBLIC/support/analyzeHeadless $HOME/x3-ghidra-research X3Render -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis"
# 1. Input routine, by-name script-call wrappers, joystick/hat action source.
eval $H -postScript X3DecompileFunctions.java "$OUT/input.c" 00410100 0049f570 0049f4c0 0049f430 0040f850 0049f680
# 2. Every register/stack-relative store to +0x150 in the image (18 rows; the
#    three cockpit writers are 00419e06, 0041fbe8, 0042e742).
eval $H -postScript X3GrepInsns.java "$OUT/grep_150.txt" 'MOV dword ptr \\[[A-Z]{3} \\+ 0x150\\],'
# 3. Listings: input-routine prologue and first Input call, InputAction site,
#    keyboard down/up loop with its Input call and post-call flags, the
#    main-loop caller 00404280 and the wrapper prologue.
eval $H -postScript X3ListRange.java "$OUT/ranges.txt" 00410100:00410140 004101a0:004101e0 00411d80:00411db0 004109e9:00410ac0 00410ac0:00410b80 00404340:00404390 00404280:004042a0 0049f570:0049f590
# 4. Callers and body sizes.
eval $H -postScript X3XrefsTo.java "$OUT/xrefs.txt" 00410100 00404280 0049f570 0049f4c0
eval $H -postScript X3FunctionContext.java "$OUT/fctx.txt" 00410100 0049f570 00404280
echo "wrote $OUT"
