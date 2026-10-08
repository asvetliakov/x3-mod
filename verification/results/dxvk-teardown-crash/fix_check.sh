#!/bin/sh
# The 2026-10-09 fix check (Hooks::release_original, src/proxy/capture.cpp): both DXVK fixtures through the
# proxy, then the rows that decide it. Run from the repository root; $DLL is the built proxy, $EXE a copy of
# build/d3d9_backend_smoke_fixture.exe in an otherwise empty directory, $PROXY a directory holding only $DLL.
#   X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_state_hook_benchmark.py --dll "$DLL"
#   X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_d3d9_backend_smoke.py \
#       --d3d9 "$PROXY/d3d9.dll" --d3d9-order n,b --exe "$EXE" --no-sweep --name proxy-release-fix
R=verification/results/bottle-X3
grep -c "Unhandled page fault\|Unhandled exception" $R/state-hook-benchmark-wine.log
for d in $(ls -dt verification/probe/build/state-hook-benchmark-proxy-* | head -3); do
    echo "$d exception_rows=$(grep -cE '^exception ' $d/x3m.log) $(grep -E '^device_destroy' $d/x3m.log)"
done
python3 -c "
import json; d = json.load(open('$R/state-hook-benchmark.json'))['cases']
for c in ('native', 'proxy-timing-off', 'proxy-shadow-timing-off', 'proxy-timing-on'):
    n = d[c]['ns_per_call']; print(c, 'SetSamplerState_same', n['SetSamplerState_same'], 'SetRenderState_same', n['SetRenderState_same'])
s = json.load(open('$R/d3d9-backend-smoke/proxy-release-fix.json'))
print('smoke', {k: s.get(k) for k in ('passed', 'exit_code', 'elapsed_s', 'dll_overrides')}, s['report']['result'] if 'report' in s else '')"
grep -cE '^exception ' "$PROXY/x3m.log"; grep -E '^(device_hooked|device_destroy)' "$PROXY/x3m.log"
