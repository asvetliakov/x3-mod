#!/bin/sh
# 2026-10-16 round 3, the unwidened-alpha block (alpha-tested gain draws widen too):
# lightmap_widen_fetch closes with `texld rT, v1, s` + `mov rL.w, rT.w` (124 words).
# Builds gen_widen_variants against the working tree, generates the gain, widen
# (far-fade dynamic gain), swiden (static gain), sgain and swidenshare (sun-share
# producer) variants of the 16 captured originals, validates them with the native
# SysWOW64 d3d9 validator (native_validate.exe through WSL interop; no device,
# nothing launched), and checks every widened variant against the pre-change
# generator's bytes: identical except for the 7 inserted words, which must be the
# texld/mov pair on the light-map destination. Folds the output into
# result-round3.json (names, sizes, verdicts, messages, witness counts only).
# usage: rerun_round3.sh <scratch dir> [<pre-change commit, default HEAD>]
# Generated shader bytes stay in the scratch dir (game-derived; never commit).
set -e
V=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$V/../../.." && pwd)
S=$1; BASE=${2:-HEAD}
# The 16 hull originals of result.json (its screen-emission original ec1f5c4a2f4e1445 is not a hull program).
CAPTURES=/mnt/c/X3/x3-modern-captures
HASHES=$(python3 -c "import json,sys;print(' '.join(sorted({r['original'] for r in json.load(open(sys.argv[1]))['rows'] if r['kind'] == 'widen'})))" "$V/result.json")
rm -rf "$S/out" "$S/old" "$S/oldout"
mkdir -p "$S/out" "$S/old" "$S/oldout"
g++ -std=c++17 -O2 "$V/gen_widen_variants.cpp" "$R/src/renderer/linear_material.cpp" "$R/src/renderer/material_motion.cpp" -o "$S/gen"
i686-w64-mingw32-g++ -O1 -static "$V/native_validate.cpp" -o "$S/native_validate.exe"
(cd "$S" && ./gen "$CAPTURES" out $HASHES > gen.txt && ./native_validate.exe out/*.bin > native_round3.txt)
git -C "$R" show "$BASE:src/renderer/linear_material.cpp" > "$S/old/linear_material.cpp"
g++ -std=c++17 -O2 -I"$R/src/renderer" "$V/gen_widen_variants.cpp" "$S/old/linear_material.cpp" "$R/src/renderer/material_motion.cpp" -o "$S/gen_old"
(cd "$S" && ./gen_old "$CAPTURES" oldout $HASHES > gen_old.txt)
python3 - "$V/result-round3.json" "$S" "$(git -C "$R" rev-parse --short "$BASE")" <<'EOF'
import collections, json, re, struct, sys
out_path, S, base = sys.argv[1], sys.argv[2], sys.argv[3]
rows, cur = [], None
for line in open(S + '/native_round3.txt'):
    m = re.search(r'([0-9a-f]{16})-(\w+)\.bin words=(\d+)', line)
    if m:
        cur = {'original': m.group(1), 'kind': m.group(2), 'words': int(m.group(3)), 'messages': []}; rows.append(cur); continue
    m = re.search(r'msg instr=(-?\d+) id=(\d+): (.*)', line)
    if m: cur['messages'].append({'instruction_call': int(m.group(1)), 'id': int(m.group(2)), 'text': m.group(3).strip()})
    m = re.search(r'end=(\w+) messages=(\d+)', line)
    if m: cur['end'] = m.group(1)
def words(path):
    data = open(path, 'rb').read()
    return list(struct.unpack('<%dI' % (len(data) // 4), data))
# Witness: new = old with exactly 7 words inserted, `texld rT, v1, s` (3 operands) then `mov rL.w, rT.w`.
witness = collections.Counter()
for r in rows:
    if r['kind'] not in ('widen', 'swiden', 'swidenshare'):
        continue
    new, old = words('%s/out/%s-%s.bin' % (S, r['original'], r['kind'])), words('%s/oldout/%s-%s.bin' % (S, r['original'], r['kind']))
    p = 0
    while p < len(old) and new[p] == old[p]: p += 1
    ok = len(new) == len(old) + 7 and new[p + 7:] == old[p:]
    if ok:
        texld, dt, coord, sampler, mov, dm, sm = new[p:p + 7]
        rt = dt & 0x7ff; rl = dm & 0x7ff
        ok = (texld & 0xffff) == 66 and texld >> 24 & 15 == 3 and (dt >> 16) & 15 == 15 and \
             (mov & 0xffff) == 1 and mov >> 24 & 15 == 2 and (dm >> 16) & 15 == 8 and \
             (sm & 0x7ff) == rt and (sm >> 16) & 0xff == 0xff and (sampler >> 28) & 7 == 2 and (sampler >> 8) & 0x18 == 8
    r['alpha_insertion_witness'] = ok
    witness[(r['kind'], ok)] += 1
tally = collections.Counter((r['kind'], r.get('end'), len(r['messages'])) for r in rows)
json.dump({'validator': 'SysWOW64 d3d9.dll Direct3DShaderValidatorCreate9 (native Windows host, no device)',
           'parameters': 'hull: fill 0.01, gain 2, depth 1, K 4, B 4, working tree with the unwidened-alpha block '
                         '(texld rT, v1, s; mov rL.w, rT.w; 124 block words); widen = far-fade dynamic gain, '
                         'swiden = static gain, sgain/swidenshare = sun-share gained/widened',
           'pre_change_commit': base,
           'alpha_insertion_witness': 'widened bytes = the pre-change generator bytes with exactly the texld/mov pair inserted',
           'witness': [{'kind': k, 'equal': ok, 'programs': c} for (k, ok), c in sorted(witness.items())],
           'tally': [{'kind': k, 'end': e, 'messages': n, 'programs': c} for (k, e, n), c in sorted(tally.items())],
           'rows': rows}, open(out_path, 'w'), indent=1)
for (k, e, n), c in sorted(tally.items()): print(c, k, 'end', e, 'messages', n)
for (k, ok), c in sorted(witness.items()): print(c, k, 'alpha insertion witness', ok)
EOF
