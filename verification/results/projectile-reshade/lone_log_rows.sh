#!/bin/sh
# FL 67 A, the lone path's log evidence (bullet-draw-batch.md section 7; grep only, x3m.log is 1.1 GB): per 300-frame
# row in the three capture windows the bullet re-shade's batch counts, the top draw pairs' effects DEFAULT / INSTANCE
# counts and the muzzle re-shade's matches (the object-trace scope live at the material submit); the source-only gain's
# variant on the effects PS (its clamp); one engine_draw row binding the effects pair for an effects body. Fields are
# read by name (key=value), so rows that gain fields keep their output.
LOG=${1:-/mnt/c/X3FL/x3m.log}
# kv(row) -> v[key] for every key=value field; win(): the frame in a capture window
AWK='function kv(   i, p) { delete v; for (i = 1; i <= NF; ++i) { p = index($i, "="); if (p) v[substr($i, 1, p - 1)] = substr($i, p + 1) } }
function win(f) { return (f >= 37800 && f <= 39900) || (f >= 60300 && f <= 61800) || (f >= 74400 && f <= 75000) }'
grep -a "^bullet_reshade_frame" "$LOG" | awk "$AWK"'
{ kv(); if (win(v["frame"] + 0)) print "frame=" v["frame"], "submits=" v["submits"], "draws=" v["draws"],
  "additive=" v["additive"], "flagged=" v["flagged"], "outside=" v["outside"] }'
grep -a "^draw_pairs" "$LOG" | awk "$AWK"'
{ kv(); if (!win(v["frame"] + 0)) next; d = "-"; s = "-"; n = split(v["top"], p, ",")
  for (i = 1; i <= n; ++i) { if (p[i] ~ /^d5e1c75351ed3f04\//) d = p[i]; if (p[i] ~ /^89193868c61c3846\//) s = p[i] }
  print "frame=" v["frame"], "effects_default:", d, "effects_instance:", s }'
grep -a "^muzzle_reshade_frame" "$LOG" | awk "$AWK"'
{ kv(); if (win(v["frame"] + 0)) print "frame=" v["frame"], "matched=" v["matched"], "draws=" v["draws"] }'
grep -a -m1 "^emission_source_gain_variant .*original=8360f422de08b5bd" "$LOG" | awk "$AWK"'
{ kv(); print "original=" v["original"], "gain=" v["gain"], "clamp=" v["clamp"], "normalized=" v["normalized"] }'
grep -a -m1 "^engine_draw .*vs=d5e1c75351ed3f04" "$LOG" | awk "$AWK"'
{ kv(); print "vs=" v["vs"], "ps=" v["ps"], "pair=" v["pair"], "name=" v["name"], "body=" v["body"] }'
