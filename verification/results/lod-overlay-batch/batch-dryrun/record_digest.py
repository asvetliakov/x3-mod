#!/usr/bin/env python3
"""Per-body digest of a lod_overlay.py --batch record JSON (x3m-lod-batch.json): draws, atlas size, texels/px,
bytes, seconds, refusal; then the counts. Usage: python3 record_digest.py RECORD.json"""
import json
import sys

r = json.load(open(sys.argv[1]))
print(f'record {sys.argv[1]}: slot {r["slot"]} retired {r["retired_slot"]} previous {r["previous_slot"]} dry_run {r["dry_run"]}')
print(f'settings: width {r["settings"]["screen_width"]} display {r["settings"]["display"]} sizes {r["settings"]["atlas"]["sizes"]}')
print(f'counts {r["counts"]}')
print(f'refused {r["refused"]}')
print(f'filtered {r["filtered"]}')
print(f'atlas sizes {r["atlas_sizes"]} bytes {r["bytes"]} ratio below 1.0 {len(r["ratio"]["below_1"])} below 2.0 {r["ratio"]["below_2"]} of {r["ratio"]["measured"]}')
print(f'draws {r["draws"]}; mixed-effect bodies {r["mixed_effect_bodies"]}; uv2 bodies {r["uv2_bodies"]}')
print(f'timing {r["timing"]}; sectors {r["sectors"]}; budget warnings {r["budget_warnings"]}')
for n in r['notes']:
    print('note:', n)
print('bodies (overlay bodies first):')
for b in sorted(r['bodies'], key=lambda b: (not b['eligible'], b['name'].lower())):
    if b['eligible']:
        print(f'  {b["name"]}: cat={b["cat"]} T_pad={b["t_pad"]}{"<T_1" if b.get("t_pad_below_t1") else ""}'
              f' r0_drawn={b.get("r0_drawn")} draws={b.get("draws")} atlas={b.get("atlas_size")}'
              f' ratio={b.get("ratio") if b.get("ratio") is None else round(b["ratio"], 2)}'
              f' atlas_mats={b.get("atlas_materials")} atlas_MB={(b.get("atlas_bytes") or 0) / 1e6:.2f}'
              f' member_MB={(b.get("member_bytes") or 0) / 1e6:.2f} s={b.get("seconds")} reused={b.get("reused")}'
              f'{" trailing=" + str(b["trailing"]) if b.get("trailing") else ""}'
              f'{" guard_waived" if b.get("guard_waived") else ""}')
    else:
        print(f'  {b["name"]}: cat={b["cat"]} refuse={",".join(b["refuse"]) or "-"} filter={",".join(b["filter"]) or "-"}'
              + (f' error="{b["error"]}"' if b.get('error') else '') + (f' bake_error="{b["bake_error"]}"' if b.get('bake_error') else ''))
