#!/bin/sh
# Where the cull/LOD pass 0x0047cfe0 sends a node that fails its early tests, i.e. whether such a node can reach the
# cull_small_parts claim at 0x0047d2a2 (docs/architecture/engine-nozzle-source.md, question 1). Offline objdump of the
# installed EXE (the listing stays local); prints only the branch instructions and the child-walk head.
#   sh verification/results/engine-nozzle-source/pass_exits.sh > verification/results/engine-nozzle-source/pass_exits_out.txt
EXE="$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe"
i686-w64-mingw32-objdump -d -M intel --start-address=0x47cfe0 --stop-address=0x47d550 "$EXE" |
    grep -E '^\s+47d(09[b9]|0a4|0e[1a]|10[b9]|112|0ff|091|085|528|52b|52e|53c|541|543|546):' |
    sed -E 's/^\s+//'
