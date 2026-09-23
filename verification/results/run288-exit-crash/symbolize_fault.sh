#!/bin/sh
# Symbolize the Run77 exit fault (EIP 0x76B137FB) in the installed proxy (sha256 268db207...).
# Load delta = logged collide_sat_sse2 handler (0x76b46e80, run287/288) - nm address of _x3m_collide_sat_thunk.
D="$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/d3d9.dll"
shasum -a 256 "$D" | cut -c1-16
THUNK=$(i686-w64-mingw32-nm "$D" | awk '$3=="_x3m_collide_sat_thunk"{print $1}')
DELTA=$((0x76b46e80 - 0x$THUNK)); PREF=$((0x76B137FB - DELTA))
printf 'thunk=0x%s delta=%#x load_base=%#x pref_eip=%#x\n' "$THUNK" $DELTA $((0x6fb40000 + DELTA)) $PREF
i686-w64-mingw32-addr2line -f -C -i -e "$D" $(printf '%#x' $PREF)
i686-w64-mingw32-objdump -d --start-address=$(printf '%#x' $PREF) --stop-address=$(printf '%#x' $((PREF+2))) "$D" | tail -1
printf 'fault_address - logged engine (0x03b88788) = %#x\n' $((0x03B88794 - 0x03b88788))
