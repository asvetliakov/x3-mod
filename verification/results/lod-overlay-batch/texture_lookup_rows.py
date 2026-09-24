"""Engine texture lookup applied to the census rows refused `texture_unresolved`.

Static rule from docs/reverse-engineering/texture-lookup.md (X3AP.exe 0x004f4cb0 name -> id,
0x004f4160 id -> path, 0x004f3510 path -> loader chain / placeholder, resolver 0x004e7590).
Reads census.txt beside this script and the bottle X3 catalogues (our own overlay slots are
skipped via lod_overlay.original_assets). Prints, per refused body, every distinct texture name
of every material and slot with the member the engine would load, or the placeholder.
Game bytes are not written anywhere.

    PYTHONPATH=tools/analysis python3 verification/results/lod-overlay-batch/texture_lookup_rows.py \
        > verification/results/lod-overlay-batch/texture_lookup_rows_out.txt
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools' / 'analysis'))

import bob1  # noqa: E402
import body_materials  # noqa: E402
import lod_overlay  # noqa: E402
from sector_fog_census import unpack  # noqa: E402

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
NULL_NAMES = ('', '0', 'NULL', 'Null', 'null')          # 0x004f4cb0 via 0x00469700 (exact match)
MPF_GENERATED = 0x800000
# 0x004f3510 missing-texture placeholders (0x004d8f10 loads them at 0x004da6e6..0x004da794)
PLACEHOLDER = [(4, 'diff', 'dds/NONE_GRAY'), (4, 'bump', 'dds/NONE_NORMAL_LOW or dds/NONE_NORMAL'),
               (4, 'spec', 'dds/NONE_WHITE'), (5, 'light', 'dds/NONE_BLACK'),
               (4, 'occl', 'dds/NONE_OCCL_DECAL'), (6, 'envmap', 'dds/ENVI'), (4, 'envi', 'dds/ENVI')]
MPF = dict(MPF_NULL=0, MPF_ALPHATEST=1, MPF_DESTINATIONBLEND=2, MPF_ALPHABLEND=4, MPF_WIREFRAME=8,
           MPF_2SIDED=0x10, MPF_TEXTURE_NOTSWAPABLE=0x20, MPF_MULTIPLY2X=0x40, MPF_MULTIPLY=0x80,
           MPF_NOFILTERING=0x100, MPF_SRCCOLOR=0x400, MPF_ENVMAP=0x1000, MPF_BUMPMAP=0x2000,
           MPF_LIGHTMAP=0x4000, MPF_BESTQUALITY=0x8000, MPF_FONTSCALE=0x10000, MPF_READABLE=0x20000,
           MPF_WRITEABLE=0x40000, MPF_AUTOFREE=0x80000, MPF_RADIOSITY=0x100000,
           MPF_TEXTUREALPHA=0x200000, MPF_IMPORTPICTURE=0x400000, MPF_GENERATED=0x800000,
           MPF_XBOX_NOCOMPRESS=0x1000000, MPF_USEFXSHADER=0x2000000, MPF_HAZE=0x4000000)


def materials_rows(assets):
    """Rows of types/Materials (0x004f44a0 loads it through 0x0046f450: addon\\types first)."""
    data, info = assets.get('addon/types/Materials.pck')
    lines = [l for l in unpack(data).decode('latin1').splitlines() if l.strip() and not l.lstrip().startswith('/')]
    count = int(lines[0].split(';')[0])
    rows = []
    for line in lines[1:count + 1]:
        f = [x.strip() for x in line.split(';')]
        flags = 0
        for tok in f[15].split('|'):
            flags |= MPF.get(tok.strip(), 0)
        rows.append(dict(texid=int(f[12], 0), flags=flags, name=f[28]))
    return rows, info


def member(assets, path, exts):
    """First resolving member for a logical path and extension list, None if none."""
    try:
        data, info = assets.logical(path.replace('\\', '/'), exts)
    except ValueError as exc:
        return f'ambiguous ({exc})'
    return f'{info["member"]} [{info["source"]}]' if data is not None else None


def load_chain(assets, full):
    """0x004f3510 on an extensionless path: dds\\<basename> (pck dds), then the path with
    pck dds, tga, jpg; else the suffix placeholder (default NONE_BLACK)."""
    sep = max(full.rfind('\\'), full.rfind('/'))
    steps = ([('dds/' + full[sep + 1:], ('.pck', '.dds'))] if sep >= 0 else []) + \
        [(full, ('.pck', '.dds')), (full, ('.tga',)), (full, ('.jpg',))]
    for path, exts in steps:
        if ':' in path:                      # a drive letter matches no catalogue member and no
            continue                         # folder under the game root (loose scan)
        hit = member(assets, path, exts)
        if hit:
            return 'load ' + hit
    for n, suffix, tex in PLACEHOLDER:
        if full[-n:].lower() == suffix:
            return f'placeholder {tex} (suffix "{suffix}")'
    return 'placeholder dds/NONE_BLACK (default)'


def engine_lookup(assets, rows, raw, slot, low_bump=False):
    s = raw.decode('latin1')
    if slot == 'bump' and low_bump and '_bump' in s:     # 0x004ba3c0, VideoD3DFlags2 & 0x400
        s = s.replace('_bump', '_low_bump')
    if s in NULL_NAMES:
        return 'id 0: no texture'
    if len(s) > 4 and s.rfind('.') == len(s) - 4:          # 0x004f4d35..0x004f4d71
        s = s[:-4]
    m = re.match(r'[ \t]*[-+]?\d+', s) if (s[:1].isdigit() or s[:1] == '-') else None
    if m:                                                  # sscanf("%d") at 0x004f4ddf
        n = int(m.group())
        if n < 0 or n >= len(rows):
            return f'id {n}: outside the Materials rows (0x004f5110 bounds: no texture unless a named id)'
        row = rows[n]
        if row['texid'] == 0:
            return f'id {n}: Materials row texture id 0, no texture (0x004f5110)'
        if row['flags'] & MPF_GENERATED:
            return f'id {n}: MPF_GENERATED surface, no file (0x004f3950)'
        full = f'textures\\{row["name"]}' if row['name'] else f'tex\\true\\{n}'
        return f'id {n} -> {full}: ' + load_chain(assets, full)
    full = 'textures\\' + s                                # entry flag 0x10000000 clear
    return f'named -> {full}: ' + load_chain(assets, full)


def refused_rows():
    for line in (HERE / 'census.txt').read_text().splitlines():
        m = re.match(r'(\S+) .*refuse=(\S+)', line)
        if m and 'texture_unresolved' in m.group(2).split(','):
            yield m.group(1)


def trees(assets, name):
    stem = bob1.body_stem(name)
    for ext in ('.pbb', '.pbd'):
        for entry in assets.candidates(stem + ext)[-1:]:
            data = assets.read_entry(entry)
            tree = bob1.parse(data, lod_overlay.MAX_TRAILING) if bob1.kind(data) else bob1.parse_text(data)
            yield f'{entry["source"]}:{entry["path"]}', tree


def main():
    assets, skipped = lod_overlay.original_assets(GAME)
    rows, info = materials_rows(assets)
    print(f'skipped overlay sources: {skipped}')
    print(f'Materials: {info["source"]}:{info["member"]} rows={len(rows)} named={sum(1 for r in rows if r["name"])}')
    bodies = list(refused_rows())
    print(f'census texture_unresolved rows: {len(bodies)}')
    outcome = {}
    for body in bodies:
        print(f'\n{body}')
        for src, tree in trees(assets, body):
            print(f'  {src}')
            seen = set()
            for mi, mat in enumerate(bob1.materials(tree)):
                for slot, raw in body_materials.slots(mat).items():
                    raw = raw if isinstance(raw, bytes) else str(raw).encode()
                    if (slot, raw) in seen:
                        continue
                    seen.add((slot, raw))
                    res = engine_lookup(assets, rows, raw, slot)
                    stem = Path(raw.decode('latin1').replace('\\', '/')).stem
                    baker = 'null' if body_materials.is_null(raw) else \
                        member(assets, 'dds/' + stem, ('.pck', '.dds')) or \
                        member(assets, 'tex/' + stem, ('.jpg', '.tga', '.bmp'))   # lod_atlas.texture_source
                    if baker:
                        continue                                  # the baker resolves it already
                    kind = 'placeholder' if 'placeholder' in res else 'load' if ': load ' in res else 'other'
                    outcome[kind] = outcome.get(kind, 0) + 1
                    extra = ''
                    if slot == 'bump':
                        extra = ' | low_bump: ' + engine_lookup(assets, rows, raw, slot, low_bump=True)
                    print(f'    mat {mi} {slot} {raw!r}: {res}{extra}')
    print(f'\nnames the baker refuses, by engine outcome: {outcome}')


if __name__ == '__main__':
    main()
