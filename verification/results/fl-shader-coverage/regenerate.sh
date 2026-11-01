#!/bin/sh
# Regenerate the FL motion-row extension from the FL archives (offline; no game,
# no Wine): index + extract the FL programs into a local scratch sweep, then run
# the profile generator against the AP base result. Writes
#   verification/results/fl-shader-coverage/motion-output-profiles-fl.json
#   src/renderer/motion_output_profiles_fl_inc.h
# The scratch sweep holds game-derived bytes: keep it outside the repository.
# The default scratch is the FL corpus the structure runner and the wrap test read.
# usage: regenerate.sh [<FL game dir, default /mnt/c/X3FL>] [<scratch dir, default /tmp/x3-fl-shader-sweep>]
set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
GAME=${1:-/mnt/c/X3FL}
S=${2:-/tmp/x3-fl-shader-sweep}
mkdir -p "$S"
cd "$R"
python3 tools/analysis/index_shaders.py "$GAME" --edition fl --output "$S/index.json"
python3 tools/analysis/sweep_shaders.py extract "$GAME" --edition fl --index "$S/index.json" \
    --raw-directory "$S/programs" --manifest "$S/manifest.json"
python3 tools/analysis/inspect_motion_output_profiles.py --inventory "$S/manifest.json" \
    --raw-directory "$S/programs" --game "$GAME" --edition fl \
    --base-profiles verification/results/motion-output-profiles.json \
    --output verification/results/fl-shader-coverage/motion-output-profiles-fl.json \
    --emit-header src/renderer/motion_output_profiles_fl_inc.h
