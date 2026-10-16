#!/bin/sh
# 2026-10-16 round 2, after the generator fixes for the two other native refusals:
# - linear_emission_sm1.cpp: the bolt far-flag variant also writes oC1 (ps_2_0 no-gap rule, id 573);
# - occlusion_cull_pass.cpp: rect_mad_vs_words stages c253 in r0 (vs_3_0 one c# read port, id 752).
# Rebuilds the screen-emission additive and flag variants of the captured original
# ec1f5c4a2f4e1445 (gain 2) with the working-tree generator, writes the occlusion pass's
# programs from the working-tree source (occlusion_programs.py), validates all of them with
# the native SysWOW64 d3d9 validator (native_validate.exe through WSL interop; no device,
# nothing launched), and checks the new flag variant byte for byte against the pre-fix
# generator's flag variant plus patch_screen_flag_oc1.py. Folds the output into
# result-round2.json (names, sizes, verdicts, messages only).
# usage: rerun_round2.sh <scratch dir> [<pre-fix commit, default badbff9e>]
# Generated shader bytes stay in the scratch dir (game-derived; never commit).
set -e
V=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$V/../../.." && pwd)
S=$1; BASE=${2:-badbff9e}
ORIGINAL=/mnt/c/X3/x3-modern-captures/ps_ec1f5c4a2f4e1445.bin
rm -rf "$S/new" "$S/old" "$S/witness"
mkdir -p "$S/new" "$S/old" "$S/witness"
g++ -std=c++17 -O2 -include cstring "$V/gen_screen_flag_variant.cpp" "$R/src/renderer/linear_emission_sm1.cpp" -o "$S/gen_screen"
git -C "$R" show "$BASE:src/renderer/linear_emission_sm1.cpp" > "$S/old/linear_emission_sm1.cpp"
g++ -std=c++17 -O2 -include cstring -I"$R/src/renderer" "$V/gen_screen_flag_variant.cpp" "$S/old/linear_emission_sm1.cpp" -o "$S/gen_screen_old"
i686-w64-mingw32-g++ -O1 -static "$V/native_validate.cpp" -o "$S/native_validate.exe"
"$S/gen_screen" "$ORIGINAL" "$S/new/ec1f5c4a2f4e1445" > "$S/gen.txt"
"$S/gen_screen_old" "$ORIGINAL" "$S/old/ec1f5c4a2f4e1445" > "$S/gen_old.txt"
python3 "$V/patch_screen_flag_oc1.py" "$S/old/ec1f5c4a2f4e1445-flag.bin" "$S/witness/ec1f5c4a2f4e1445-flag.bin" > /dev/null
if cmp -s "$S/witness/ec1f5c4a2f4e1445-flag.bin" "$S/new/ec1f5c4a2f4e1445-flag.bin"; then WITNESS=equal; else WITNESS=different; fi
python3 "$V/occlusion_programs.py" "$R" "$S/new" > /dev/null
(cd "$S/new" && ../native_validate.exe *.bin > ../native_round2.txt)
python3 - "$V/result-round2.json" "$S/native_round2.txt" "$WITNESS" "$BASE" <<'EOF'
import json, re, sys
rows, cur = [], None
for line in open(sys.argv[2]):
    m = re.match(r'(\S+)\.bin words=(\d+)', line)
    if m:
        cur = {'program': m.group(1), 'words': int(m.group(2)), 'messages': []}; rows.append(cur); continue
    m = re.search(r'msg instr=(-?\d+) id=(\d+): (.*)', line)
    if m: cur['messages'].append({'instruction_call': int(m.group(1)), 'id': int(m.group(2)), 'text': m.group(3).strip()})
    m = re.search(r'end=(\w+) messages=(\d+)', line)
    if m: cur['end'] = m.group(1)
json.dump({'validator': 'SysWOW64 d3d9.dll Direct3DShaderValidatorCreate9 (native Windows host, no device)',
           'parameters': 'screen emission ec1f5c4a2f4e1445 AdditiveGain gain 2 without/with far flag, fixed generator; '
                         'occl-* = occlusion_cull_pass.cpp programs from the working tree (ps1..ps4 = 1..4 oC MOVs)',
           'pre_fix_commit': sys.argv[4],
           'flag_equals_patch_screen_flag_oc1_witness': sys.argv[3] == 'equal',
           'rows': rows}, open(sys.argv[1], 'w'), indent=1)
for r in rows: print(r['program'], 'words', r['words'], 'end', r.get('end'), 'messages', len(r['messages']))
print('flag variant equals the patch witness:', sys.argv[3])
EOF
