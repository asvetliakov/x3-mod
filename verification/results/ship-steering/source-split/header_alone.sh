#!/bin/sh
# Each header of the ship steering source split compiles alone (a one-line TU per header, -fsyntax-only) with the DLL's i686 MinGW
# flags and with the host fixture's c++ flags (the core headers only: the internal ones include <windows.h>).
#   sh verification/results/ship-steering/source-split/header_alone.sh   (from the repository root; no Wine, no game)
set -u
cd "$(dirname "$0")/../../../.." || exit 1
I686="i686-w64-mingw32-g++ -std=gnu++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -fno-exceptions -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DX3M_VERSION=\"1.2\""
HOST="c++ -std=c++17 -O2 -Wall -Wextra -Werror"
CORE="ship_gate_board_core.h ship_gate_geometry_core.h ship_gate_lane_core.h ship_gate_certificate_core.h ship_gate_arrival_core.h ship_gate_ctl_core.h"
INTERNAL="ship_steering_internal.h ship_gate_ctl_glue.h ship_dock_glue.h"
fail=0
for h in $CORE $INTERNAL; do
    [ -f "src/proxy/$h" ] || continue
    for cc in i686 host; do
        case "$cc" in host) case " $INTERNAL " in *" $h "*) continue;; esac; cmd=$HOST;; *) cmd=$I686;; esac
        if printf '#include "%s"\n' "$h" | $cmd -Isrc/proxy -x c++ -fsyntax-only - 2>/tmp/header_alone.err; then
            echo "$h $cc PASS"
        else
            echo "$h $cc FAIL"; head -5 /tmp/header_alone.err; fail=1
        fi
    done
done
exit $fail
