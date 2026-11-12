#!/bin/sh
# FL 68 A, the lone bolts unflagged (bullet-draw-batch.md section 7; grep only, x3m.log is 154 MB): per 300-frame
# window with lone draws, the bullet re-shade's batch and lone counts beside the additive route's window (pair draws,
# admitted, refused, of them `state`); the totals over those windows; the effects DEFAULT draws' shadowed blend and
# separate-alpha state from the source-only gain's refusal rows (the only FL 68 row that prints them). Fields are read
# by name (key=value), so rows that gain fields keep their output.
# lone_state_log_rows_out.txt: the FL 68 A session's log (154 MB; renamed x3m.prev.log at the next launch and replaced
# at the one after, so it is no longer on disk). lone_state_log_rows_s2_out.txt: the next session on the same DLL
# (session=20261112-213642-22824, 314 MB, x3m.prev.log at 21:48).
LOG=${1:-/mnt/c/X3FL/x3m.prev.log}
AWK='function kv(   i, p) { delete v; for (i = 1; i <= NF; ++i) { p = index($i, "="); if (p) v[substr($i, 1, p - 1)] = substr($i, p + 1) } }'
grep -a -e "^bullet_reshade_frame" -e "^screen_emission_additive_refused_window" "$LOG" | awk "$AWK"'
/^screen_emission_additive_refused_window/ { kv(); w = "pair_draws=" v["pair_draws"] " admitted=" v["admitted"] " refused=" v["refused"] " state=" v["state"]; ws = v["state"] + 0; next }
/^bullet_reshade_frame/ { kv(); if (v["lone"] + 0 == 0) next
  print "frame=" v["frame"], "draws=" v["draws"], "flagged=" v["flagged"], "lone=" v["lone"], "lone_flagged=" v["lone_flagged"],
        "lone_failures=" v["lone_failures"], "|", w
  n++; lone += v["lone"]; lf += v["lone_flagged"]; st += ws; dr += v["draws"]; fl += v["flagged"] }
END { print "windows=" n, "lone=" lone, "lone_flagged=" lf, "route_state_refused=" st, "batch_draws=" dr, "batch_flagged=" fl }'
grep -a "^emission_source_gain_refused_state .*vs=d5e1c75351ed3f04" "$LOG" | awk "$AWK"'
{ kv(); k = "vs=" v["vs"] " ps=" v["ps"] " blend=" v["blend"] " src=" v["src"] " dst=" v["dst"] " op=" v["op"] " sepalpha=" v["sepalpha"]; c[k]++ }
END { for (k in c) print "rows=" c[k], k }'
