"""Every distinct material slot name of the winning vanilla bodies, resolved by the old baker rule and by
lod_atlas.lookup (texture-lookup.md sections 9-11, 2026-09-24); prints the names whose result changed and the
diffuse regressions.

Old rule (lod_atlas before 2026-09-24): NULL/0/'' -> none; dds/<stem> (pck, dds), then tex/<stem> (jpg, tga, bmp);
else unresolved. New: lod_atlas.lookup -> none, member, placeholder, or refused:<census reason> (an AtlasError, e.g.
texture_animation_unsupported). A regression is a name that loaded a member before, is used in a diffuse slot, and
now gives none or a placeholder (an explicit refusal is not one). Bottle X3 read-only, overlay slots skipped.
About 150 s.

    python3 verification/results/lod-overlay-batch/texture_lookup_old_new.py \
        > verification/results/lod-overlay-batch/texture_lookup_old_new_out.txt
"""
import sys
from collections import Counter, defaultdict
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import body_materials  # noqa: E402
import lod_atlas  # noqa: E402
import lod_batch_census  # noqa: E402
import lod_overlay  # noqa: E402

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'


def old(assets, name):
    if body_materials.is_null(name):
        return 'none'
    stem = PurePosixPath(name.decode('latin1').replace('\\', '/')).stem
    try:
        for folder, exts in (('dds', ('.pck', '.dds')), ('tex', ('.jpg', '.tga', '.bmp'))):
            data, info = assets.logical(f'{folder}/{stem}', exts)
            if data is not None:
                return f'{info["source"]}:{info["member"]}'
    except ValueError:
        return 'ambiguous'
    return 'unresolved'


def new(assets, name):
    if body_materials.is_null(name):
        return 'none'
    try:
        found = lod_atlas.lookup(assets, name)
    except lod_atlas.AtlasError as exc:
        return 'refused:' + lod_batch_census.atlas_reason(exc)
    if found is None:
        return 'none'
    entry, placeholder = found
    return f'placeholder:{placeholder}' if placeholder else f'{entry["source"]}:{entry["path"]}'


def kind_of(result):
    return result.split(':')[0] if result.startswith(('placeholder', 'refused')) or result in (
        'none', 'unresolved', 'ambiguous') else 'member'


def main():
    assets, skipped = lod_overlay.original_assets(GAME)
    slots, bodies = defaultdict(set), Counter()
    for key, entries in list(assets.entries.items()):
        k = key.removeprefix('addon/')
        if not k.startswith('objects/') or not k.endswith(('.bob', '.bod')):
            continue
        try:
            data = assets.read_entry(entries[-1])
            tree = bob1.parse(data, lod_overlay.MAX_TRAILING) if bob1.kind(data) else bob1.parse_text(data)
        except Exception:
            continue
        finally:
            assets.cache.clear()
        seen = set()
        for mat in bob1.materials(tree):
            for slot, raw in body_materials.slots(mat).items():
                raw = raw if isinstance(raw, bytes) else str(raw).encode()
                slots[raw].add(slot)
                if raw not in seen:
                    seen.add(raw)
                    bodies[raw] += 1
    kinds, changed, regress = Counter(), [], []
    for name in slots:
        a, b = old(assets, name), new(assets, name)
        assets.cache.clear()
        if a == b:
            kinds['same'] += 1
            continue
        ka, kb = kind_of(a), kind_of(b)
        kinds[f'{"loaded_before" if ka == "member" else "refused_before"} -> {kb}'] += 1
        if ka == 'member':
            changed.append((name, a, b))
            if kb in ('none', 'placeholder') and slots[name] & {'diffuse', 'texture'}:
                regress.append((name, a, b))
    print(f'skipped overlay sources {skipped}; distinct slot names {len(slots)}')
    print(f'by outcome: {dict(sorted(kinds.items()))}')
    print(f'names that loaded a member before and now resolve differently: {len(changed)}')
    for name, a, b in sorted(changed):
        print(f'  CHANGED {name!r} slots {sorted(slots[name])} (bodies {bodies[name]}): {a} -> {b}')
    print(f'diffuse regressions (loaded before, now none or placeholder): {len(regress)}')
    for name, a, b in sorted(regress):
        print(f'  REGRESSION {name!r} (bodies {bodies[name]}): {a} -> {b}')


if __name__ == '__main__':
    main()
