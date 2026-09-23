#!/bin/sh
# Run75 transit-identity fix: compact rows from the scratch outputs (outputs beside this script, rows_out.txt).
# $1 = host witness stdout (fog_handover_host.cpp built with clang++ as in test_fog_handover), $2 = scratch root.
grep -E 'transit_needs|heap_token|HEAP_TOKEN|plan_|^RESULT' "$1"
python3 verification/probe/fog_route_bridge_run.py check --output "$2/route3" --summary "$2/route3/summary.json" | tail -1
python3 verification/results/run70-candidate-audit.py bridge /tmp/x3-run73-candidate/route/bridge.log "$2/route3/bridge.log" |
  python3 -c "import json,sys;d=json.load(sys.stdin);[print(k,json.dumps(d[k])) for k in ('checks','names','only_old','only_new','shadow_ab_names','motes_ab_names','motes_ab_rows')]"
printf 'bridge_transit_epochs=%s\n' "$(grep -c 'reason=transit' "$2/route3/bridge.log")"
grep -E '^HANDOVER|^RESULT' "$2/fogpass/pass_stdout.txt"
