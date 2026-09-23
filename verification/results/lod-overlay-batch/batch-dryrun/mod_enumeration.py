#!/usr/bin/env python3
"""Census-level enumeration of a vanilla+mod root with the batch's enumeration (.bob members, text bodies,
trailing-byte tolerance; no baking): counts of mod-catalogue bodies by category, member extension, eligibility
and refusal reason, texture sources outside dds/, and writes only_mods.txt (a --only list of mod bodies for the
baking dry run) beside this script. Usage: python3 mod_enumeration.py MODROOT [--jobs N]"""
import os
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import lod_batch_census as census  # noqa: E402
import lod_overlay  # noqa: E402


def main():
    game = Path(sys.argv[1])
    jobs = int(sys.argv[sys.argv.index('--jobs') + 1]) if '--jobs' in sys.argv else max(1, (os.cpu_count() or 2) - 2)
    width = lod_overlay.effective_width()
    opts = dict(sizes=(1024, 2048), include_other=False, widths=(width,), rule=dict(census.RULE))
    rows, skipped = census.run(game, opts, jobs, include_text=True)
    mod = [r for r in rows if r['source'].startswith('addon/') and int(r['source'][6:8]) >= 5]
    print(f'root {game}: rows {len(rows)} (skipped sources {skipped}); mod-catalogue winners (addon/05+) {len(mod)}')
    print('mod winners by category', dict(Counter(r['cat'] for r in mod)))
    print('mod winners by member extension', dict(Counter(r['path'].rsplit(".", 1)[-1].lower() for r in mod)))
    print('mod winners by source', dict(sorted(Counter(r['source'] for r in mod).items())))
    el = [r for r in mod if r['eligible']]
    print(f'mod eligible {len(el)}: by category {dict(Counter(r["cat"] for r in el))}, by extension'
          f' {dict(Counter(r["path"].rsplit(".", 1)[-1].lower() for r in el))}')
    print('mod refusals (a body counts once per reason)',
          dict(sorted(Counter(x for r in mod for x in r['refuse']).items(), key=lambda x: -x[1])))
    print('mod filters', dict(Counter(x for r in mod if not r['refuse'] for x in r['filter'])))
    print(f'mod bodies with tolerated trailing bytes {sum(1 for r in mod if r.get("trailing"))}'
          f' (eligible {sum(1 for r in el if r.get("trailing"))}); refused trailing_bytes'
          f' {sum(1 for r in mod if "trailing_bytes" in r["refuse"])}')
    print(f'mod eligible with more than one atlas material (mixed effects)'
          f' {sum(1 for r in el if r.get("atlas_materials", 1) > 1)}, with a second UV set {sum(1 for r in el if r.get("uv2"))}')
    non_dds = [r for r in el if any(not s.lower().startswith('dds/') for s in r.get('texture_sources', ()))]
    print(f'mod eligible reading a texture outside dds/ (tex/ jpg|tga) {len(non_dds)}; texture members by top dir/ext'
          f' {dict(Counter((s.split("/")[0], s.rsplit(".", 1)[-1].lower()) for r in el for s in r.get("texture_sources", ())))}')
    all_el = [r for r in rows if r['eligible']]
    print(f'all eligible {len(all_el)} ({dict(Counter(r["cat"] for r in all_el))}); atlas sizes at {width} wide'
          f' {dict(sorted(Counter(r["atlas"][width]["size"] for r in all_el).items()))};'
          f' estimated atlas bytes {sum(r["atlas"][width]["bytes"] for r in all_el) / 1e6:.0f} MB'
          f' (cap 2048 {sum(r["atlas"][width]["bytes_cap2048"] for r in all_el) / 1e6:.0f} MB)')
    print('all refusals', dict(sorted(Counter(x for r in rows for x in r['refuse']).items(), key=lambda x: -x[1])))
    print('all filters', dict(Counter(x for r in rows if not r['refuse'] for x in r['filter'])))
    pick = sorted(el, key=lambda r: -r.get('r0_drawn', 0))
    chosen = ([r for r in pick if r.get('trailing')][:3] + non_dds[:3]
              + [r for r in pick if r.get('atlas_materials', 1) > 1][:2]
              + [r for r in pick if r['path'].lower().endswith('.bob')][:6])
    seen, lines = set(), []
    for r in chosen:
        if r['name'] not in seen:
            seen.add(r['name'])
            lines.append(f'{r["name"]}  # {r["source"]}:{r["path"]} r0_drawn={r.get("r0_drawn")}')
    (Path(__file__).resolve().parent / 'only_mods.txt').write_text('\n'.join(lines) + '\n')
    print(f'only_mods.txt: {len(lines)} mod bodies for the baking dry run')


if __name__ == '__main__':
    main()
