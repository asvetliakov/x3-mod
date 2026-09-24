"""Numbered material texture names across the winning vanilla bodies, classified by lod_atlas's engine rule.

Reads bottle X3 read-only (our overlay slots skipped via lod_overlay.original_assets). For every winning
objects/*.bob|.bod resource that bob1 parses, every material slot name starting with a digit or '-' (after the
3-character extension is dropped) is classified: ok (a Materials row with a texture), texid0 (the row's texture
id is 0, e.g. the NULL-like '0'), generated (MPF_GENERATED, e.g. 340; lod_atlas refuses
texture_generated), negative (a texture animation, lod_atlas.animation_frame), at_or_above_rows (would alias a
named texture registered at run time, texture-lookup.md section 3), no_integer ('-x': sscanf reads nothing, so
0x004f4cb0 treats it as a named texture). The id is not truncated to 16 bits here (lod_atlas does).
Resources bob1 cannot parse (scenes, CUT1, ...) are counted, not classified. About 145 s.

    python3 verification/results/lod-overlay-batch/numbered_names_scan.py \
        > verification/results/lod-overlay-batch/numbered_names_scan_out.txt
"""
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import body_materials  # noqa: E402
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'


def main():
    assets, skipped = lod_overlay.original_assets(GAME)
    rows = lod_atlas.materials_rows(assets)
    names, kinds, bodies, unparsed = Counter(), Counter(), 0, 0
    for key, entries in list(assets.entries.items()):
        k = key.removeprefix('addon/')
        if not k.startswith('objects/') or not k.endswith(('.bob', '.bod')):
            continue
        try:
            data = assets.read_entry(entries[-1])
            tree = bob1.parse(data, lod_overlay.MAX_TRAILING) if bob1.kind(data) else bob1.parse_text(data)
        except Exception:
            unparsed += 1
            continue
        finally:
            assets.cache.clear()
        bodies += 1
        for mat in bob1.materials(tree):
            for raw in body_materials.slots(mat).values():
                raw = raw if isinstance(raw, bytes) else str(raw).encode()
                s = raw.decode('latin1')
                if len(s) > 4 and s.rfind('.') == len(s) - 4:
                    s = s[:-4]
                if not (s[:1].isdigit() or s[:1] == '-'):
                    continue
                m = lod_atlas._LEADING_INT.match(s)
                n = int(m.group()) if m else None
                kind = ('no_integer' if n is None else 'negative' if n < 0 else 'at_or_above_rows' if n >= len(rows)
                        else 'texid0' if rows[n][0] == 0 else 'generated' if rows[n][1] & lod_atlas.MPF_GENERATED
                        else 'ok')
                kinds[kind] += 1
                names[(kind, raw)] += 1
    print(f'skipped overlay sources {skipped}; Materials rows {len(rows)}; bodies parsed {bodies}, unparsed {unparsed}')
    print(f'slot names by class: {dict(sorted(kinds.items()))}')
    for kind in sorted(kinds):
        top = [(r.decode("latin1"), c) for (k, r), c in names.most_common() if k == kind][:8]
        print(f'  {kind}: {top}')


if __name__ == '__main__':
    main()
