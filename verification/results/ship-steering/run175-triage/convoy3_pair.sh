#!/bin/sh
# Run175 A second triage, Q1 (convoy 3 at cb58): the rows that decide the first leader 0x12. Usage: sh convoy3_pair.sh ROWS
# (ROWS: the ship-row extract of rows.sh). Prints: the pair verdict rows (escort c2e8, leader 2280) frames 60300-61100; the escorts'
# w=stop episodes (lane rows, gaps > 3 s split); the controller transitions of the three ships; the capital width census (the type
# whose width was read, and when); the arrival placement widths.
R=$1
echo "== pair rows (c2e8, 2280) 60300-61100"
grep -E '^ship_gate_ctl_pair' "$R" | grep -E 'a=0x9170c2e8 b=0x91292280|a=0x91292280 b=0x9170c2e8' | awk '{split($2,a,"="); if (a[2]>=60300 && a[2]<=61100) print}' |
  awk '{o=""; for(i=1;i<=NF;i++){split($i,k,"="); if(k[1]~/^(frame|t_ms|a|kind|verdict|before|why|t_c_s|yielder|d_m|cause|gap_m)$/) o=o" "$i}; print o}'
for sh in 0x9170c2e8 0x916f9348; do
  echo "== $sh w=stop episodes (first frame, first t, last t, rows)"
  grep -E '^ship_gate_ctl_lane ' "$R" | grep "ship=$sh" | grep ' w=stop' | awk '{for(i=1;i<=NF;i++){split($i,k,"="); if(k[1]=="t_ms") t=k[2]/1000; if(k[1]=="frame") f=k[2]}; print f, t}' |
    awk 'NR==1{s=$2;sf=$1;p=$2;n=1;next} {if($2-p>3){print " ", sf, s, "->", p, n; s=$2;sf=$1;n=0} p=$2;n++} END{if (NR) print " ", sf, s, "->", p, n}'
done
echo "== controller transitions"
grep -E '^ship_gate_ctl |^ship_gate_ctl_rewrite' "$R" | grep -E '0x91292280|0x9170c2e8|0x916f9348' | awk '{split($2,a,"="); if (a[2]>=58400 && a[2]<=70500) print}' |
  awk '{o=""; for(i=1;i<=NF;i++){split($i,k,"="); if(k[1]~/^(frame|t_ms|ship|order|from|to|why|stage|site|from_m|to_m)$/) o=o" "$i}; print $1 o}'
echo "== width census (types read) and capital arrival widths"
grep '^ship_capital_census' "$R" | awk '{print $2, $3, $5, $6, $8}'
grep '^ship_gate_launch' "$R" | grep 'capital=1' | awk '{o=""; for(i=1;i<=NF;i++){split($i,k,"="); if(k[1]~/^(frame|ship|class|r_m|w_m)$/) o=o" "$i}; print o}'
