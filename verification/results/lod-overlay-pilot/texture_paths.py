#!/usr/bin/env python3
"""How material texture names resolve to catalogue members (evidence for the atlas names).

For every non-NULL t_* STRING slot of every material of the named bodies (all LODs'
materials, read with lod_overlay.original_assets, i.e. without the x3m-lod overlay):
the name's directory part and extension, whether dds/<stem> resolves under .pck/.dds/.tga
(body_materials.texture_info's rule) and with which member extension, and whether any
catalogue member matches the name with its directory kept (<dir>/<name> or dds/<dir>/<name>,
any of those extensions). Prints counts only.

  python3 verification/results/lod-overlay-pilot/texture_paths.py <body> ...
"""
import sys
from collections import Counter
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import lod_overlay  # noqa: E402

EXTS = ('.pck', '.dds', '.tga')


def main(bodies):
    assets, skipped = lod_overlay.original_assets(bob1.DEFAULT_GAME)
    print(f'sources read without {skipped}')
    names = {}
    for body in bodies:
        tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, body)))
        for m in bob1.materials(tree):
            for n, t, v in m.get('params', ()):
                if t == 8 and n.lower().startswith(b't_') and v.upper() not in (b'NULL', b'0', b''):
                    names[v.lower()] = v
    c, missing = Counter(), []
    for v in names.values():
        p = PurePosixPath(v.decode('latin1').replace('\\', '/'))
        c['names'] += 1
        c['with_directory'] += len(p.parts) > 1
        c[f'extension {p.suffix.lower()}'] += 1
        found = [(ext, e) for ext in EXTS for e in assets.candidates(f'dds/{p.stem}{ext}')]
        c['resolve_dds_stem'] += bool(found)
        for ext in {e['path'].rsplit('.', 1)[-1].lower() for _, e in found}:
            c[f'member extension .{ext}'] += 1
        if not found:
            missing.append(p.name)
        full = str(p.with_suffix(''))
        c['member_with_directory'] += any(assets.candidates(f'{pre}{full}{ext}') for pre in ('', 'dds/') for ext in EXTS)
    print('distinct texture names ' + ' '.join(f'{k}={v}' for k, v in sorted(c.items())))
    print(f'not resolving under dds/: {sorted(missing)}')
    atlas = [f'dds/x3m_lod_{PurePosixPath(b).name}' for b in bodies]
    taken = sum(bool(assets.candidates(f'{a}_{s}{ext}')) for a in atlas for s in ('diffuse', 'light') for ext in EXTS)
    print(f'atlas stems {len(atlas) * 2} (x3m_lod_<body>_diffuse/_light) present in the shipped catalogues: {taken}')


if __name__ == '__main__':
    main(sys.argv[1:])
