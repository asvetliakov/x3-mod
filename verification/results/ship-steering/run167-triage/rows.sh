#!/bin/sh
# Run166 triage (controller B.5, main f0fb46ad): "still a little circling?". Copied from ../run165-triage (hold_funnel.py made
# gate-generic; ctl_live.py and window_phase.py added). Run167 gates: 0x8ea0faf8, 0x8ea10ad0, 0x8ea13a58 (gates.txt); rows file kept outside the tree (425 MB).
# Usage: sh rows.sh [log] [pre-extracted rows file].
D=$(cd "$(dirname "$0")" && pwd); L=${1:-$D/x3m.log}
if [ -n "$2" ]; then T=$2; else T=$(mktemp); trap 'rm -f "$T"' EXIT
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage )' "$L" > "$T"; fi
cd "$D" || exit 1
python3 -I caps.py "$T" > caps.txt && python3 -I gates.py "$T" > gates.txt
python3 -I ctl_rows.py "$T" > ctl_rows.txt                         # Q1 membership transitions
python3 -I cap_orders.py "$T" --min-s 3 > cap_orders.txt           # Q1/Q3 order timelines, 0x12 episodes
python3 -I crowd.py "$T" > crowd.txt && python3 -I transits.py "$T" > transits.txt   # Q1/Q4 crossings, commit->launch, span
python3 -I sb_timeline.py "$T" > sb_timeline.txt
for g in faf8 0ad0 3a58; do python3 -I sb_seq.py "$T" $g > sb_seq_$g.txt; done   # Q2 sequencer per game second
python3 -I sb_laps_all.py "$T" > sb_laps_all.txt
python3 -I members.py "$T" > members.txt && python3 -I hold_funnel.py "$T" >> members.txt   # Q1 per-member stages, petal laps/radius, board counts
python3 -I hold_wait.py "$T" > hold_wait.txt                       # Q2 every hold second by reason; launch -> next release
python3 -I funnel_idle.py "$T" > funnel_idle.txt                   # Q2 funnel free with holders, by reason
python3 -I window_runs.py "$T" > window_runs.txt                   # Q2 funnel free, head out of window: runs
python3 -I e_evict.py "$T" > e_evict.txt && python3 -I x12_real.py "$T" > x12_real.txt   # Q3 evictions, own-gate E rows, 0x12
python3 -I small_gap.py "$T" > small_gap.txt                       # Q3 e_small_gap
python3 -I game_steps.py "$T" > game_steps.txt && python3 -I seta.py "$T" > seta.txt && python3 -I sb_counters.py "$T" > sb_counters.txt
python3 -I ctl_live.py "$T" > ctl_live.txt && python3 -I window_phase.py "$T" > window_phase.txt   # Q3 release-window phase, Q4 ctl_live
python3 -I escorts.py "$T" > escorts.txt                                 # Q1/Q5 escorts after the leader launch
# q4_target.py is Run166-specific (hard-coded holders); not run for Run167
python3 -I lap_speed.py "$T" > lap_speed.txt && python3 -I escort_wait.py "$T" > escort_wait.txt   # Run167 Q3/Q8 lap-law phases, escort waits
python3 -I hold_totals.py hold_wait.txt > hold_totals.txt && python3 -I release_wait.py "$T" > release_wait.txt   # Run167 Q2
python3 -I release_front.py "$T" > release_front.txt && python3 -I pairs.py "$T" > pairs.txt   # Run167 Q4 extended window, Q5 yields_to
python3 -I stuck.py "$T" > stuck.txt && python3 -I member_detours.py "$T" > member_detours.txt   # Run167 Q5 stuck, Q6 final-straight detours
for s in 0x8ea0a0c8 0x38fc9e50; do python3 -I head_track.py "$T" $s 4 > head_track_$s.txt; done   # Run167 Q2 head tracks (25763, 3071)
