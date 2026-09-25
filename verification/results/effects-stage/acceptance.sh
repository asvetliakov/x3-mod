#!/bin/sh
# Effects stage acceptance (docs/verification/effects-stage.md; docs/architecture/effects-modernisation-opus.md
# section 9): the host modules, a scratch mingw build of the d3d9 target with 0 warnings, the x87 audit and the
# ps/vs slot counts of the six stage programs. Patterned on verification/results/bolt-footprint/acceptance.sh. Run
# from the repository root with no game running
# (python3 -c "import sys; sys.path.insert(0,'verification/probe'); import game_guard; print(game_guard.game_running())" prints []).
# Output: every line below is tee'd into acceptance_out.txt beside this script. No Wine, no launch.
# No set -e: every step reports and the script runs to the end. The printed DLL hash is provenance only (the build
# path is embedded in the debug information).
OUT=verification/results/effects-stage/acceptance_out.txt
{
echo "acceptance modules:"
PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_effects_stage_core verification.analysis.test_effect_keys verification.analysis.test_bolt_footprint verification.analysis.test_screen_emission_live 2>&1 | grep -E '^(Ran|OK|FAILED)'
echo "modules naming the launcher baseline or the hotkeys:"
PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_comparison_hotkeys verification.analysis.test_taa_image_defaults 2>&1 | grep -E '^(Ran|OK|FAILED)'
echo "key table:"
/usr/bin/python3 tools/effects/effect_keys.py --output tools/effects/effect_keys.json --check
echo "program slots (vs_3_0 / ps_3_0, the conservative table of ps3_program_slots.h):"
/usr/bin/python3 - <<'EOF'
import re, struct, sys
sys.path.insert(0, 'verification/probe')
from pathlib import Path
COST = {31: 0, 48: 0, 81: 0, 46: 0, 37: 8, 38: 3, 36: 3, 32: 3, 33: 2, 90: 2, 18: 2, 91: 2, 92: 2, 95: 2}
def slots(words):
    i, n = 1, 0
    while i < len(words) - 1:
        t = words[i]
        if t & 0xffff == 0xfffe:
            i += 1 + ((t >> 16) & 0x7fff); continue
        n += COST.get(t & 0xffff, 1); i += 1 + ((t >> 24) & 15)
    return n
for name in ('bolt_vertex', 'bolt_pixel', 'shell_vertex', 'shell_pixel', 'decal_vertex', 'decal_pixel'):
    words = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{8})u', Path('src/renderer/effects_%s_program_inc.h' % name).read_text())]
    print('  effects_%s slots=%d words=%d' % (name, slots(words), len(words)))
EOF
B=build-es-$$
if cmake -S . -B "$B" -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 > /dev/null 2>&1 \
    && cmake --build "$B" -j8 --target d3d9 > "$B/build.log" 2>&1; then build_exit=0; else build_exit=$?; fi
warnings=$(grep -c 'warning:' "$B/build.log" 2>/dev/null || true)
echo "build_exit=$build_exit warnings=${warnings:-none}"
if [ "$build_exit" -eq 0 ]; then
    /usr/bin/python3 verification/probe/check_no_x87.py "$B/d3d9.dll" | /usr/bin/python3 -c "import json,sys; d=json.load(sys.stdin); print('x87', d['result'], 'roots', len(d.get('roots', d.get('root_symbols', []))) if isinstance(d.get('roots', d.get('root_symbols', [])), (list, dict)) else d.get('roots_count'), 'reachable', d['reachable_functions'], 'violations', len(d['violations']))"
    echo "sha256_prefix=$(shasum -a 256 "$B/d3d9.dll" | cut -c1-16) (provenance only, build-directory dependent)"
else
    echo "x87 skipped: no DLL"; tail -20 "$B/build.log"
fi
rm -rf "$B"
echo "scratch_removed=$([ -e "$B" ] && echo 0 || echo 1)"
} 2>&1 | tee "$OUT"
