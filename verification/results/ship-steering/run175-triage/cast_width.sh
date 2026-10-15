#!/bin/sh
# Run175 A second triage, Q1: cast return_trip with the escorts' hull half-width w = r (the DLL's ctl_escort fallback for a capital
# type never width-read: Run175's M2 type 252, census row only at its commit, frame 67773) against the cast as committed (w = 0.53 r).
# Builds two scratch copies of the fixture (no source edit): a per-second pair-verdict print (X3M_C_PAIR) for each member's lane
# predecessor, and the variant with kReturnTrip's escorts at w_r 1.0. Prints the return rows and the pair verdict tallies per run.
# Usage (repository root): sh verification/results/ship-steering/run175-triage/cast_width.sh   (no Wine, no game; ~1 min)
set -e
H=$(mktemp -d); F=verification/probe/ship_steering_host.cpp; D=$(dirname "$0")
python3 -I - "$F" "$H/pair.cpp" "$H/wr.cpp" <<'PY'
import sys
src, pair, wr = sys.argv[1:4]
L = open(src).read().split('\n')
n = next(i for i, l in enumerate(L) if "// 7: certified from a merge's first certificate" in l)
L.insert(n, '''            if (std::getenv("X3M_C_PAIR") && mm->lane_pred && now_ms / 1000 != (now_ms - dt) / 1000) { const int qi = gc::find(*g, mm->lane_pred); if (qi >= 0) { const gc::Member& Q = g->m[qi];
                std::printf("  tc %s pair t=%.0f ship %u pred %d v %s cause %s t_c %u ewhy %s mouth %d o %x Po %x pos %.0f,%.0f,%.0f\\n", su.tag, double(now_ms) / 1000.0, k, qi,
                    gc::pair_name(gc::PairV(g->v[mi][qi])), gc::pair_why_name(g->why[mi][qi]), unsigned(g->tc[mi][qi]),
                    gc::ewhy_name(gc::e_reason(*g, unsigned(mi), unsigned(qi), now_end, true, true, mm->st.order, Q.st.order)), int(mm->redep && gc::lane_mouth_on(*g, *mm, Q, now_end)), mm->st.order, Q.st.order,
                    c.m.pos[0] / 500.0, c.m.pos[1] / 500.0, c.m.pos[2] / 500.0); } }''')
s = '\n'.join(L); open(pair, 'w').write(s)
a = '''    {&kRtM2, 0.53, kArrRet, 0, 0, 0, 0, 0, 0, 100000, false, 6000},
    {&kRtM2, 0.53, kArrRet, 0, 0, 0, 0, 0, 0, 215000, false, 6000},'''
assert a in s
open(wr, 'w').write(s.replace(a, a.replace('0.53', '1.0')))
PY
for v in pair wr; do c++ -std=c++17 -O2 -w -I verification/probe "$H/$v.cpp" -o "$H/$v"; done
for v in pair wr; do
    echo "== variant $v ($( [ $v = pair ] && echo 'escorts w = 0.53 r, as committed' || echo 'escorts w = r'))"
    X3M_C_PAIR=1 X3M_C_TRACE=1 X3M_C_ONLY=return_trip "$H/$v" --lane > "$H/$v.txt" 2>/dev/null || true
    python3 -I - "$H/$v.txt" <<'PY'
import sys, re, collections
c = collections.Counter(); first = {}
for l in open(sys.argv[1]):
    if l.startswith('scenario C return_trip return tf'):
        print(l[:240].rstrip())
        for k, n in sorted(c.items()): print('    ship %s pred %s %s/%s e=%s %s: %d s (first t=%s)' % (*k, n, first[k]))
        c = collections.Counter(); first = {}; continue
    m = re.match(r'  tc return_trip pair t=(\d+) ship (\d) pred (\d+) v (\S+) cause (\S+) t_c (\d+) ewhy (\S+) mouth (\d)', l)
    if m and m[2] == '1' and m[3] == '0':  # escort 1 behind the leader: the pair of Run175's first 0x12
        k = (m[2], m[3], m[4], m[5], m[7], 'mouth' + m[8]); c[k] += 1; first.setdefault(k, m[1])
PY
    echo "  x12 rows: $(grep -cE '^  tc return_trip x12 ' "$H/$v.txt" || true); first: $(grep -m1 -E '^  tc return_trip x12 ' "$H/$v.txt" || true)"
done
rm -rf "$H"
