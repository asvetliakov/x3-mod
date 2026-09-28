#!/usr/bin/env python3
"""Classic (non-effect) material bake, 2026-09-29: compares a scratch batch bake made with the tools before the
change with one made after it, and reports the outcome of the formerly non_effect_material bodies.

  python3 nonfx_bake_check.py BEFORE_OUT AFTER_OUT [--game GAME] [--previous OUT] > nonfx_bake_check_out.txt

BEFORE_OUT / AFTER_OUT are `lod_overlay.py --batch --only FILE --out DIR` roots (one addon/NN.cat each); the
game tree is only read (the installed batch record names the formerly refused bodies).
1. Byte identity: every body of the before bake has the same member set in the after bake and every member
   (body and atlas textures) is byte-identical; a body that differs lists the members that do.
2. Outcome of the ship/station bodies the installed record refuses non_effect_material: baked, filtered and
   refused with the reason; record-0 draws against merged draws (total and the ten largest); atlas sizes;
   merged classic materials, bump atlases (the tilted-normal case) and kept classic groups.
3. Written bodies: the body's own material records are unchanged (the first source_materials records equal the
   source's), and every merged classic material carries the constants of the classic parameter block derived
   here from the record words of the materials it absorbed (non-effect-materials.md section 3), independently
   of lod_atlas.classic_shape.
4. With --previous (a bake of the same list by the tools before the review fixes of 2026-09-29, which merged a
   BUMPMAP_LOW record of a model without the tangent declaration into a DEFAULT material): the bodies it baked that
   the after bake does not, and those whose merged draws rose.
"""
import argparse
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tools/analysis'), str(ROOT / 'verification/probe')]
import bob1                                          # noqa: E402
import lod_overlay                                   # noqa: E402
from inspect_x3 import read_catalogue                # noqa: E402
from sector_fog_census import unpack                 # noqa: E402


def overlay(root):
    cat = next((Path(root) / 'addon').glob('*.cat'))
    raw = bytes(v ^ 0x33 for v in cat.with_suffix('.dat').read_bytes())
    members = {e['path']: raw[e['offset']:e['offset'] + e['size']] for e in read_catalogue(cat)}
    marker = json.loads(cat.with_name(cat.stem + '.x3m-lod.json').read_text())
    record = json.loads((Path(root) / 'x3m-lod-batch.json').read_text())
    return members, {b['name']: b for b in marker['bodies']}, {b['name']: b for b in record['bodies']}, record


def paths(b):
    return sorted(m['path'] for m in b['members'])


def block_constants(m):
    """16.16 (diffuse strength, specular strength, specular power, emissive rgb) of the classic parameter block
    (0x004c13a6..0x004c14d6) from a classic record's words."""
    fx = lambda x: int(round(x * 65536))
    c, si = m['colors'], m['colors'][11]
    power = (m['w24'] if m['w24'] else sum(c[6:9]) / 768 * 100) + 1
    return (0 if si else fx(m['w2c'] * 0.01), fx(m['w26'] * 0.01), fx(power)) + tuple(
        fx(x * si / 25500) if si else 0 for x in c[3:6])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('before')
    ap.add_argument('after')
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    ap.add_argument('--previous')
    a = ap.parse_args()
    bm, bmark, _, brec = overlay(a.before)
    am, amark, arows, rec = overlay(a.after)
    print(f'tool_sha256 before {brec["settings"]["tool_sha256"][:16]} after {rec["settings"]["tool_sha256"][:16]}')
    print('== 1. byte identity (bodies of the before bake)')
    same = 0
    for name in sorted(bmark):
        after = amark.get(name)
        diff = ['not baked'] if after is None else (
            [p for p in paths(bmark[name]) if bm[p] != am.get(p)] +
            [p for p in paths(after) if p not in paths(bmark[name])])
        same += not diff
        print(f'  {"identical" if not diff else "DIFFERS  "} {name} members {len(paths(bmark[name]))}'
              + (f' differing: {[p.rsplit("_", 1)[-1] if p.startswith("dds/") else "body" for p in diff]}'
                 if diff else ''))
    print(f'  identical {same} of {len(bmark)}')

    installed = json.loads((a.game / 'addon' / 'x3m-lod-batch.json').read_text())
    former = [b['name'] for b in installed['bodies']
              if 'non_effect_material' in (b.get('refuse') or []) and b['cat'] != 'other']
    print(f'== 2. outcome of the {len(former)} ship/station bodies the installed record refuses non_effect_material')
    outcome = collections.Counter()
    rows = []
    for name in former:
        r = arows.get(name)
        if r is None:
            outcome['not enumerated'] += 1
        elif name in amark:
            outcome['baked'] += 1
            rows.append((r, amark[name]))
        elif r['refuse']:
            outcome['refused ' + ','.join(r['refuse'])] += 1
            print(f'  refused {name}: {r["refuse"]} {r.get("error") or ""}'[:230])
        elif r['filter']:
            outcome['filtered ' + ','.join(r['filter'])] += 1
        else:
            outcome['eligible, not baked'] += 1
            print(f'  eligible but not baked {name}')
    for k, v in sorted(outcome.items()):
        print(f'  {k}: {v}')
    nothing = ('helper_body', 'classic_nothing_to_atlas')
    content = [n for n in former if not set((arows.get(n) or {}).get('filter') or ()) & set(nothing)]
    for n in ('ships/props/cameradummy', 'stations/docks/M6dockCarrier_scene_dummy'):
        r = arows.get(n) or {}
        print(f'  {n}: member {r.get("member")} refuse {r.get("refuse")} filter {r.get("filter")}'
              f' baked {n in amark}')
    print(f'  content bodies (neither {" nor ".join(nothing)}): {len(content)}; of them baked'
          f' {sum(1 for n in content if n in amark)}, filtered'
          f' {dict(collections.Counter(",".join(arows[n]["filter"]) for n in content if arows[n]["filter"] and not arows[n]["refuse"]))},'
          f' refused {dict(collections.Counter(",".join(arows[n]["refuse"]) for n in content if arows[n]["refuse"]))}')
    r0, merged = sum(r['r0_drawn'] for r, _ in rows), sum(m['draws'] for _, m in rows)
    print(f'  baked: record-0 draws {r0} -> merged draws {merged}')
    print('  ten largest by record-0 draws (record 0 -> merged; atlas; merged materials; kept classic; bump atlas):')
    for r, m in sorted(rows, key=lambda x: (-x[0]['r0_drawn'], x[0]['name']))[:10]:
        at = m['atlas']
        print(f'    {r["r0_drawn"]:3d} -> {m["draws"]:2d}  {at["size"]}  materials {len(at["materials"])}'
              f' (classic {len(at.get("classic_materials", []))})  kept {len(at.get("kept_classic", {}))}'
              f'  bump {"yes" if any(t["slot"] == "bump" for t in at["textures"]) else "no"}  {r["name"]}')
    print('  atlas sizes: ' + json.dumps(collections.Counter(str(m['atlas']['size']) for _, m in rows), sort_keys=True))
    print('  atlas bytes (stored members, all slots): '
          f'{sum(x["bytes"] for _, m in rows for x in m["members"] if x["path"].startswith("dds/"))}')
    print('  bodies with a merged classic material: '
          f'{sum(1 for _, m in rows if m["atlas"].get("classic_materials"))}; with two or more: '
          f'{sum(1 for _, m in rows if len(m["atlas"].get("classic_materials", [])) > 1)}')
    print(f'  bodies with a bump atlas: {sum(1 for _, m in rows if any(t["slot"] == "bump" for t in m["atlas"]["textures"]))}')
    kept = collections.Counter(v for _, m in rows for v in m['atlas'].get('kept_classic', {}).values())
    print(f'  kept classic groups by reason (materials): {dict(kept)}')
    print(f'  kept light-bleed materials: {sum(len(m["atlas"].get("kept_light_bleed", [])) for _, m in rows)}')

    print('== 3. written bodies')
    assets, _ = lod_overlay.original_assets(a.game)
    intact = consts_ok = classic_mats = 0
    bad = []
    for r, m in rows:
        entry = bob1.resolve_body(assets, r['name'])
        src = bob1.materials(bob1.parse(assets.read_entry(entry), lod_overlay.MAX_TRAILING))
        out = bob1.materials(bob1.parse(unpack(am[m['member']])))
        ok = out[:len(src)] == src and len(src) == m['source_materials']
        intact += ok
        if not ok:
            bad.append((r['name'], 'source records changed'))
        for s in m['synth']:
            if s.get('effect') != 'standard_lighting.fx' or s['index'] not in m['atlas'].get('classic_materials', []):
                continue
            classic_mats += 1
            p = {n.lower(): v for n, _, v in out[s['index']]['params']}
            want = {block_constants(src[i]) for i in s['absorbed']}
            got = (p[b'g_matdiffusestrength'][0], p[b'g_matspecularstrength'][0], p[b'g_matspecularpower'][0]) + \
                tuple(p[b'g_matemissivecolor'][:3])
            state = (p[b'g_alphablendenable'], p[b'g_alphatestenable'], p[b'g_zwriteenable'], p[b'g_zenable'],
                     p[b'g_cullmode'], p[b'g_matreflectionstrength'], p[b'g_alphavalue']) == \
                ([0], [0], [1], [1], [2], [0], [65536])
            good = want == {got} and state and out[s['index']]['effect'] == b'standard_lighting.fx'
            consts_ok += good
            if not good:
                bad.append((r['name'], f'material {s["index"]}: constants {got} against {sorted(want)}, state {state}'))
    print(f'  bodies whose own material records are unchanged: {intact} of {len(rows)}')
    print(f'  merged classic materials with the constants of their source records and the opaque state:'
          f' {consts_ok} of {classic_mats}')
    for name, what in bad:
        print(f'  MISMATCH {name}: {what}')
    if a.previous:
        _, pmark, prows, _ = overlay(a.previous)
        print('== 4. against the bake before the review fixes')
        gone = sorted(n for n in pmark if n in former and n not in amark)
        more = sorted(n for n in pmark if n in amark and n in former and amark[n]['draws'] > pmark[n]['draws'])
        print(f'  baked before the fixes {sum(1 for n in pmark if n in former)}, after {len(rows)}')
        for n in gone:
            r = arows.get(n) or {}
            print(f'  no longer baked: {n} (merged draws were {pmark[n]["draws"]} of {prows[n]["r0_drawn"]}):'
                  f' refuse {r.get("refuse")} filter {r.get("filter")} {(r.get("error") or "")[:150]}')
        for n in more:
            print(f'  reduced: {n} merged draws {pmark[n]["draws"]} -> {amark[n]["draws"]} of {arows[n]["r0_drawn"]};'
                  f' kept classic {amark[n]["atlas"].get("kept_classic")}')
        print(f'  no longer baked {len(gone)}, reduced {len(more)}')


main()
