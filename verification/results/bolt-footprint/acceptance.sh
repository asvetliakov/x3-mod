#!/bin/sh
# Bolt footprint acceptance (docs/verification/bolt-footprint.md): host modules,
# a scratch build of the d3d9 target with 0 warnings and the x87 audit. Run
# from the repository root with no game running
# (python3 -c "import sys; sys.path.insert(0,'verification/probe'); import game_guard; print(game_guard.game_running())" prints []).
# Output: every line below is tee'd into acceptance_out.txt beside this script. No Wine, no launch.
# No set -e: every step reports and the script runs to the end, so the build's
# exit status is always printed (a failed configure or build prints its non-zero
# code and skips the x87 audit) and the scratch build directory is always
# removed. The printed DLL hash is provenance only: it is not reproducible
# across build directories (the build path is embedded in the debug
# information), so two runs print different prefixes for the same sources.
OUT=verification/results/bolt-footprint/acceptance_out.txt
{
echo "acceptance modules:"
PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_bolt_footprint verification.analysis.test_screen_emission_live 2>&1 | grep -E '^(Ran|OK|FAILED)'
echo "modules naming the bullet route or the launcher baseline:"
PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_screen_emission_additive_transform verification.analysis.test_fade_region verification.analysis.test_motion_wrap_states verification.analysis.test_linear_emission_source_gain verification.analysis.test_comparison_hotkeys verification.analysis.test_music_keep 2>&1 | grep -E '^(Ran|OK|FAILED)'
B=build-bf-$$
if cmake -S . -B "$B" -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 > /dev/null 2>&1 \
    && cmake --build "$B" -j8 --target d3d9 > "$B/build.log" 2>&1; then build_exit=0; else build_exit=$?; fi
warnings=$(grep -c 'warning:' "$B/build.log" 2>/dev/null || true)
echo "build_exit=$build_exit warnings=${warnings:-none}"
if [ "$build_exit" -eq 0 ]; then
    /usr/bin/python3 verification/probe/check_no_x87.py "$B/d3d9.dll" | /usr/bin/python3 -c "import json,sys; d=json.load(sys.stdin); print('x87', d['result'], 'reachable', d['reachable_functions'], 'violations', len(d['violations']))"
    echo "sha256_prefix=$(shasum -a 256 "$B/d3d9.dll" | cut -c1-16) (provenance only, build-directory dependent)"
else
    echo "x87 skipped: no DLL"
fi
rm -rf "$B"
echo "scratch_removed=$([ -e "$B" ] && echo 0 || echo 1)"
} 2>&1 | tee "$OUT"
