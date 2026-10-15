#!/bin/sh
# Decision A's mode against its level-0 twin with the module's capital lead-in (c) switched off for the arrival-side ships only (a temporary
# copy of the fixture whose m.gate_mode test also reads X3M_T_NOLEAD; the board membership as pure bodies kept): whether the mode's excess
# arrival-side 0x12 over the twin (stat_compare.txt) is the lead-in's. 20 seeds x tf 1 / 6 / 10 on return_trip and rt_titan_rest, then
# stat_compare.py (nolead_diag.txt). Usage, from the repository root: sh verification/results/ship-steering/convoy-gate-transit/arr_mode/nolead_diag.sh
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
mkdir -p "$H/verification/probe"
ln -s "$PWD/src" "$H/src"
sed 's|m.gate_mode = valid && !(g_arr_twin && m.arr_side);|m.gate_mode = valid \&\& !((g_arr_twin \|\| std::getenv("X3M_T_NOLEAD")) \&\& m.arr_side);|' \
  verification/probe/ship_steering_host.cpp > "$H/verification/probe/ship_steering_host.cpp"
grep -q X3M_T_NOLEAD "$H/verification/probe/ship_steering_host.cpp"
c++ -std=c++17 -O2 -Wall -Wextra "$H/verification/probe/ship_steering_host.cpp" -o "$H/host"
for c in return_trip rt_titan_rest; do for tf in 1 6 10; do
  X3M_T_NOLEAD=1 X3M_C_ONLY=$c X3M_C_TF=$tf X3M_C_SEEDN=20 X3M_C_ALL=1 "$H/host" --lane 2>/dev/null | grep ' mode tf ' > "$H/st_${c}_$tf.txt" &
done; done
wait
python3 -I "$OUT/stat_compare.py" "$H"/st_*.txt > "$OUT/nolead_diag.txt"
rm -rf "$H"
