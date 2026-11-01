#!/bin/sh
# Regenerate the FL shader rows from the FL archives (offline; no game, no Wine):
# index + extract the FL programs into a local scratch sweep, run the motion
# profile generator against the AP base result (part 4a), then derive the FL
# hull material rows of the original-shading variants (part 4b). Writes
#   verification/results/fl-shader-coverage/motion-output-profiles-fl.json
#   src/renderer/motion_output_profiles_fl_inc.h
#   verification/results/fl-shader-coverage/linear-materials-fl.json
#   src/renderer/{linear_material_pixels,linear_material_pairs,linear_xt_profiles,
#                 material_exposure_profiles,linear_emission_hull}_fl_inc.h
# Proof (separately, scratch outside the repository): validate_variants.py
# (native validator over every FL variant), ap_identity.py (AP parity).
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
python3 tools/analysis/inspect_fl_linear_materials.py --programs "$S/programs" --manifest "$S/manifest.json" \
    --emit-headers
