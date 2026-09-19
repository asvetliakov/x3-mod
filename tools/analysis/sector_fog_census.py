#!/usr/bin/env python3
"""Read-only AP sector/background/cloud census; output derived metadata only.

No game process, Wine, external package, extracted asset, or render change needed.
See docs/reverse-engineering/sector-fog.md for field/engine semantics. Distances
use 500 native units/metre; body coordinates/sizes are deliberately not relabelled
as physical cloud dimensions. Numbered archives and loose files are supported;
selected mods require explicit --mod arguments (later arguments override earlier).
"""
import argparse
import csv
import gzip
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import tempfile
import xml.etree.ElementTree as ET
from collections import Counter

from inspect_x3 import read_catalogue


def unpack(data):
    if data.startswith(b'\x1f\x8b'):
        return gzip.decompress(data)
    if len(data) > 3:
        key = data[0] ^ 0xc8
        if bytes(v ^ key for v in data[1:3]) == b'\x1f\x8b':
            return gzip.decompress(bytes(v ^ key for v in data[1:]))
    return data


def canonical(path):
    path = path.replace('\\', '/').lower()
    suffix = PurePosixPath(path).suffix
    # Packed and unpacked members refer to the same engine resource.
    folder = path.removeprefix('addon/').split('/')[0]
    packed_suffix = {'maps': '.xml', 't': '.xml', 'dds': '.dds'}.get(folder, '.txt')
    return str(PurePosixPath(path).with_suffix({'.pck': packed_suffix, '.pbd': '.bod', '.pbb': '.bob'}.get(suffix, suffix)))



def resource_key(path, selected_mod=False):
    """AP-selected mods may omit addon/; base-game resources keep their namespace."""
    key = canonical(path)
    if selected_mod and key.split('/')[0] in {'maps', 'types', 't', 'objects', 'dds'}:
        return 'addon/' + key
    return key


def require(condition, message='Validation failed'):
    """Acceptance checks must still execute under python -O."""
    if not condition:
        raise ValueError(message)


class Assets:
    def __init__(self, root, mods=()):
        self.root = root
        self.layers = []
        self.entries = {}
        self.loose = []
        self.cache = {}
        cats = sorted(root.glob('[0-9][0-9].cat')) + sorted((root / 'addon').glob('[0-9][0-9].cat'))
        layers = [(cat, False) for cat in cats] + [(cat, True) for cat in mods]
        for cat, selected_mod in layers:
            source = cat.relative_to(root).as_posix() if cat.is_relative_to(root) else cat.as_posix()
            self.layers.append(source)
            for entry in read_catalogue(cat):
                entry = dict(entry, source=source, cat=cat)
                self.entries.setdefault(resource_key(entry['path'], selected_mod), []).append(entry)
        for base in ('', 'addon'):
            for folder in ('maps', 'types', 't', 'objects', 'dds'):
                for path in sorted((root / base / folder).rglob('*')):
                    if path.is_file():
                        relative = path.relative_to(root).as_posix()
                        key = resource_key(relative)
                        if any('loose' in previous and canonical(previous['path']) == canonical(relative)
                               for previous in self.entries.get(key, [])):
                            raise ValueError('Ambiguous loose packed/unpacked aliases: ' + relative)
                        self.entries.setdefault(key, []).append(
                            dict(path=relative, source='loose:' + relative, loose=path))
                        self.loose.append(relative)

    def candidates(self, path):
        # AP namespace wins over base-game resources. Explicit selected mods
        # are rebased into AP at insertion; base TC loose files remain base-only.
        key = canonical(path).removeprefix('addon/')
        return self.entries.get('addon/' + key, []) or self.entries.get(key, [])

    def read_entry(self, entry):
        key = (entry['source'], entry['path'])
        if key not in self.cache:
            if 'loose' in entry:
                data = entry['loose'].read_bytes()
            else:
                with entry['cat'].with_suffix('.dat').open('rb') as stream:
                    stream.seek(entry['offset'])
                    raw = stream.read(entry['size'])
                if len(raw) != entry['size']:
                    raise ValueError('Truncated archive member: ' + str(key))
                data = bytes(v ^ 0x33 for v in raw)
            self.cache[key] = unpack(data)
        return self.cache[key]

    def get(self, path):
        entries = self.candidates(path)
        if not entries:
            raise FileNotFoundError(path)
        entry = entries[-1]
        return self.read_entry(entry), dict(source=entry['source'], member=entry['path'],
            decoded_sha256=hashlib.sha256(self.read_entry(entry)).hexdigest())

    def logical(self, stem, extensions):
        unique = {canonical(stem + extension): extension for extension in extensions}
        found = [(extension, self.candidates(stem + extension)) for extension in unique.values()]
        found = [(extension, entries) for extension, entries in found if entries]
        if len(found) > 1:
            raise ValueError('Ambiguous alternate formats: ' + stem)
        return self.get(stem + found[0][0]) if found else (None, None)


def backgrounds(data):
    lines = [line.strip() for line in data.decode('utf-8-sig').splitlines()
             if line.strip() and not line.lstrip().startswith('//')]
    version, count, *tail = lines[0].split(';')
    if int(version) != 25 or any(tail):
        raise ValueError('Only validated TBackgrounds version 25 is supported')
    rows = []
    for index, line in enumerate(lines[1:]):
        values = line.rstrip(';').split(';')
        if len(values) != 38:
            raise ValueError(f'Background {index}: expected 38 columns, got {len(values)}')
        near, far = int(values[24]), int(values[25])
        rows.append(dict(index=index, family=values[7], body_rates=list(map(int, values[11:19])),
            dust=int(values[19]), near_native=near, far_native=far,
            effective_near_native=near if far >= near else 0,
            effective_far_native=far if far >= near else 0,
            stardust_percent=int(values[26]), stardust_target=(int(values[26]) << 9) // 100,
            star_variants=int(values[8]), background_variants=int(values[9]),
            outside_variants=int(values[10]), identifier=values[37]))
    if len(rows) != int(count):
        raise ValueError('TBackgrounds header count mismatch')
    return rows


def names(assets):
    texts = {}
    provenance = []
    # Localization files overlay individual text entries, retaining earlier pages.
    for key in ('t/0001-l044.xml', 'addon/t/0001-l044.xml'):
        for entry in assets.entries.get(key, []):
            root = ET.fromstring(assets.read_entry(entry))
            provenance.append(dict(source=entry['source'], member=entry['path']))
            for page in root.findall('page'):
                pid = int(page.attrib['id'])
                for text in page.findall('t'):
                    texts[pid, int(text.attrib['id'])] = ''.join(text.itertext())

    page_versions = {}
    for pid, _ in texts:
        page_versions.setdefault(pid % 10000, set()).add(pid)
    page_versions = {page: sorted(versions, reverse=True) for page, versions in page_versions.items()}

    def resolve(page, ident, depth=0):
        if depth > 12:
            raise ValueError('Localization reference cycle')
        versions = page_versions.get(page, [])
        value = next((texts[pid, ident] for pid in versions if (pid, ident) in texts), '')
        return re.sub(r'\{(\d+),(\d+)\}', lambda m: resolve(int(m[1]), int(m[2]), depth + 1), value)
    return resolve, provenance


def body_metadata(data):
    text = data.decode('utf-8-sig')
    sizes = re.findall(r'(?m)^\s*(\d+);\s*/ Automatic body size', text)
    verts = re.findall(r'(?m)^\s*(-?\d+);\s*(-?\d+);\s*(-?\d+);\s*/\d+\s*$', text)
    materials = []
    for line in text.splitlines():
        if not line.startswith('MATERIAL6:'):
            continue
        values = line.split(';')
        count = int(values[4])
        fields = values[5:]
        if len(fields) < count * 3:
            raise ValueError('Truncated MATERIAL6')
        params = {fields[i * 3]: fields[i * 3 + 2] for i in range(count)}
        materials.append(dict(effect=values[3], parameters={key: value for key, value in params.items()
            if key in {'g_AlphaValue', 'g_AlphaBlendEnable', 'g_SrcBlend', 'g_DestBlend',
                       'g_ZEnable', 'g_ZWriteEnable', 't_DiffuseTexture', 't_AlphaTexture', 'Brightness'}}))
    points = [tuple(map(int, vertex)) for vertex in verts]
    return dict(automatic_body_sizes=list(map(int, sizes)), vertex_count=len(points),
        geometry_extent=[max(p[i] for p in points) - min(p[i] for p in points) for i in range(3)] if points else None,
        materials=materials)


def texture_metadata(data):
    if len(data) < 128 or data[:4] != b'DDS ':
        return dict(format='unsupported', bytes=len(data))
    height, width = struct.unpack_from('<II', data, 12)
    return dict(format=data[84:88].decode('ascii', errors='replace'), width=width, height=height,
                mip_count=struct.unpack_from('<I', data, 28)[0])



def resolve_material_textures(assets, material):
    for parameter, field in (('t_DiffuseTexture', 'texture'), ('t_AlphaTexture', 'alpha_texture')):
        path = material['parameters'].get(parameter, '')
        if not path or path == '0':
            material[field] = dict(status='not_referenced')
            continue
        texture_stem = 'dds/' + PurePosixPath(path.replace('\\', '/')).stem
        texture, source = assets.logical(texture_stem, ('.pck', '.dds', '.tga'))
        material[field] = (dict(texture_metadata(texture), **source, status='resolved')
                           if texture is not None else dict(status='missing', missing=texture_stem))


def validate_outputs(game, outputs, mods=()):
    outputs = [path for path in outputs if path is not None]
    for output in outputs:
        require(not output.resolve().is_relative_to(game.resolve()),
                'Outputs must be outside the read-only game directory')
    protected = [path for mod in mods for path in (mod, mod.with_suffix('.dat'))]
    for output in outputs:
        for input_path in protected:
            same = output.resolve() == input_path.resolve()
            if output.exists() and input_path.exists():
                same = same or output.samefile(input_path)
            require(not same, 'Output would overwrite a read-only mod CAT/DAT input')
    for index, output in enumerate(outputs):
        for previous in outputs[:index]:
            same = output.resolve() == previous.resolve()
            if output.exists() and previous.exists():
                same = same or output.samefile(previous)
            require(not same, 'JSON and CSV outputs must be distinct files')


def census(root, mods=()):
    assets = Assets(root, mods)
    map_bytes, map_source = assets.logical('maps/x3_universe', ('.pck', '.xml'))
    bg_bytes, bg_source = assets.logical('types/tbackgrounds', ('.pck', '.txt'))
    if map_bytes is None or bg_bytes is None:
        raise ValueError('Missing map or TBackgrounds')
    rows = backgrounds(bg_bytes)
    resolve, name_sources = names(assets)
    sectors = []
    for sector in ET.fromstring(map_bytes).iter('o'):
        if sector.get('t') != '1':
            continue
        x, y = int(sector.attrib['x']), int(sector.attrib['y'])
        background = [child for child in sector.findall('o') if child.get('t') == '2']
        if len(background) != 1:
            raise ValueError(f'Sector {x},{y}: expected one background')
        background = background[0]
        index = int(background.attrib['s'])
        if not 0 <= index < len(rows):
            raise ValueError('Sector background index out of bounds')
        row = rows[index]
        raw_name = resolve(7, 1020000 + 100 * (y + 1) + x + 1)
        display_name = re.sub(r'\([^)]*\)', '', raw_name).strip() or 'Unknown Sector'
        size = int(sector.attrib['size'])
        sectors.append(dict(x=x, y=y, name=display_name, name_file=raw_name, name_resolved=bool(raw_name),
            background_index=index, family=row['family'], sector_size_native=size,
            sector_size_km=size / 500000, dust=row['dust'], body_rates=row['body_rates'],
            near_native=row['near_native'], far_native=row['far_native'],
            near_km=row['near_native'] / 500000, far_km=row['far_native'] / 500000,
            stardust_percent=row['stardust_percent'], stardust_target=row['stardust_target'],
            neb_override=int(background.get('neb', '0')), stars_override=int(background.get('stars', '0'))))
    if len({(s['x'], s['y']) for s in sectors}) != len(sectors):
        raise ValueError('Duplicate sector coordinates')
    families = {}
    for family in sorted({row['family'] for row in rows}):
        parts = []
        for slot in range(1, 9):
            stem = f'objects/environments/nebulae/{family}/nebula_{family}_dust_part{slot:02d}'
            data, source = assets.logical(stem, ('.pbd', '.bod', '.pbb', '.bob'))
            if data is None:
                continue
            metadata = body_metadata(data) if source['member'].lower().endswith(('.pbd', '.bod')) else dict(unsupported_binary_body=True)
            for material in metadata.get('materials', []):
                resolve_material_textures(assets, material)
            parts.append(dict(slot=slot, **source, **metadata))
        families[family] = parts
    for sector in sectors:
        parts = families[sector['family']]
        sector['automatic_body_sizes'] = sorted({size for part in parts for size in part.get('automatic_body_sizes', [])})
        sector['present_body_slots'] = [part['slot'] for part in parts]
        alpha = [material['alpha_texture'] for part in parts for material in part.get('materials', [])]
        sector['alpha_texture_statuses'] = sorted({texture['status'] for texture in alpha})
        sector['alpha_texture_missing'] = sorted({texture['missing'] for texture in alpha if texture['status'] == 'missing'})
        sector['alpha_texture_sha256'] = sorted({texture['decoded_sha256'] for texture in alpha if texture['status'] == 'resolved'})
        sector['positive_rate_missing_slots'] = [i + 1 for i, rate in enumerate(sector['body_rates'])
            if rate > 0 and i + 1 not in sector['present_body_slots']]
    return dict(schema=2, distance_native_per_km=500000, archive_layers=assets.layers,
        loose_files=assets.loose, map=map_source, backgrounds_source=bg_source, name_layers=name_sources,
        backgrounds=rows, sectors=sorted(sectors, key=lambda s: (s['y'], s['x'])), families=families)


def write_csv(result, path):
    rows = [{key: ','.join(map(str, value)) if isinstance(value, list) else value
             for key, value in sector.items()} for sector in result['sectors']]
    with path.open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def self_test():
    checks = 0

    def check(condition, message):
        nonlocal checks
        require(condition, message)
        checks += 1

    def rejects(operation, message):
        try:
            operation()
        except ValueError:
            check(True, message)
        else:
            raise ValueError(message)

    def catalogue(cat, members):
        cat.parent.mkdir(parents=True, exist_ok=True)
        index = (cat.with_suffix('.dat').name + '\n' + ''.join(
            f'{name} {len(data)}\n' for name, data in members)).encode()
        cat.write_bytes(bytes(v ^ ((0xdb + i) & 255) for i, v in enumerate(index)))
        cat.with_suffix('.dat').write_bytes(bytes(v ^ 0x33 for _, data in members for v in data))

    payload = b'25;0;\n'
    compressed = gzip.compress(payload)
    key = 0x7a
    check(unpack(compressed) == payload, 'gzip decode')
    check(unpack(bytes([key ^ 0xc8]) + bytes(v ^ key for v in compressed)) == payload, 'XOR gzip decode')
    check(unpack(payload) == payload, 'plain decode')
    check(backgrounds(payload) == [], 'empty valid background table')
    rejects(lambda: backgrounds(b'25;1;\n'), 'Bad background row count accepted')
    check(canonical('Types/TBackgrounds.pck') == 'types/tbackgrounds.txt', 'packed type alias')
    check(canonical(r'Objects\Nebula\Cloud.pbd') == 'objects/nebula/cloud.bod', 'POSIX canonical key')
    check(resource_key(r'Addon\Maps\X3_Universe.pck') == 'addon/maps/x3_universe.xml', 'retained addon namespace')
    check(resource_key('maps/x3_universe.pck', True) == 'addon/maps/x3_universe.xml', 'selected mod AP namespace')
    sample = b'500; / Automatic body size\n-2; -3; 0; /0\n2; 3; 0; /1\n'
    check(body_metadata(sample)['geometry_extent'] == [4, 6, 0], 'quad extent')
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        catalogue(root / '01.cat', [('types/TBackgrounds.pck', b'old')])
        catalogue(root / '02.cat', [('types/TBackgrounds.pck', b'new')])
        check(Assets(root).get('types/tbackgrounds.pck')[0] == b'new', 'later numbered archive')
        catalogue(root / 'addon/01.cat', [('addon/types/TBackgrounds.pck', b'addon'),
            ('addon/t/0001-L044.pck', b'<language><page id="7"><t id="1">Stock</t></page></language>')])
        check(Assets(root).get('types/tbackgrounds.pck')[0] == b'addon', 'addon archive override')
        mod = root / 'mods/selected.cat'
        catalogue(mod, [('types/TBackgrounds.pck', b'mod'),
            ('t/0001-L044.pck', b'<language><page id="7"><t id="1">Mod</t></page></language>')])
        check(Assets(root, [mod]).get('types/tbackgrounds.pck')[0] == b'mod', 'unprefixed selected mod override')
        mod_names, _ = names(Assets(root, [mod]))
        check(mod_names(7, 1) == 'Mod', 'unprefixed selected mod localization')
        mod2 = root / 'mods/later.cat'
        catalogue(mod2, [('addon/types/TBackgrounds.pck', b'mod2')])
        check(Assets(root, [mod, mod2]).get('types/tbackgrounds.pck')[0] == b'mod2', 'later selected mod override')
        (root / 'types').mkdir()
        (root / 'types/TBackgrounds.txt').write_bytes(b'loose')
        check(Assets(root, [mod]).get('types/tbackgrounds.pck')[0] == b'mod', 'base TC loose cannot replace AP mod')
        check(Assets(root).get('types/tbackgrounds.pck')[0] == b'addon', 'base TC loose cannot replace AP stock')
        (root / 'addon/types').mkdir()
        (root / 'addon/types/TBackgrounds.txt').write_bytes(b'AP loose')
        check(Assets(root, [mod]).get('types/tbackgrounds.pck')[0] == b'AP loose', 'AP loose overrides selected mod')
        (root / 'addon/maps').mkdir(parents=True)
        (root / 'maps').mkdir()
        (root / 'maps/x3_universe.xml').write_text('base')
        (root / 'addon/maps/x3_universe.xml').write_text('addon')
        check(Assets(root).get('maps/x3_universe.xml')[0] == b'addon', 'loose addon preference')
        (root / 't').mkdir()
        (root / 't/0001-L044.xml').write_text(
            '<language><page id="7"><t id="1">Old</t><t id="2">Base</t></page>'
            '<page id="350007"><t id="1">TC</t></page>'
            '<page id="380007"><t id="1">AP {7,2}</t></page></language>')
        resolve, _ = names(Assets(root))
        check(resolve(7, 1) == 'AP Base', 'versioned localization reference')
        dds = bytearray(128)
        dds[:4] = b'DDS '
        struct.pack_into('<II', dds, 12, 3, 7)
        dds[84:88] = b'DXT1'
        catalogue(root / '03.cat', [('dds/alpha.pck', gzip.compress(bytes(dds)))])
        material = {'parameters': {'t_AlphaTexture': r'environments\test\alpha.tga'}}
        resolve_material_textures(Assets(root), material)
        alpha = material['alpha_texture']
        check(alpha['status'] == 'resolved' and alpha['width'] == 7 and alpha['height'] == 3
              and alpha['decoded_sha256'] == hashlib.sha256(dds).hexdigest(), 'alpha metadata and hash')
        check(alpha['member'] == 'dds/alpha.pck' and alpha['source'] == '03.cat', 'portable alpha provenance')
        material = {'parameters': {'t_AlphaTexture': 'missing.tga', 't_DiffuseTexture': '0'}}
        resolve_material_textures(Assets(root), material)
        check(material['alpha_texture']['status'] == 'missing'
              and material['texture']['status'] == 'not_referenced', 'missing alpha versus no reference')
        rejects(lambda: validate_outputs(root / 'game', [root / 'same', root / 'same']), 'Identical outputs accepted')
        rejects(lambda: validate_outputs(root / 'game', [mod], [mod]), 'Mod CAT output overwrite accepted')
        rejects(lambda: validate_outputs(root / 'game', [mod.with_suffix('.dat')], [mod]), 'Mod DAT output overwrite accepted')
        rejects(lambda: validate_outputs(root, [root / 'inside']), 'Game-directory output accepted')
        rejects(lambda: verify_stock({'sectors': [], 'backgrounds': []}), 'Invalid stock passed (including python -O)')
        (root / '02.dat').write_bytes(b'truncated')
        rejects(lambda: Assets(root), 'Bad CAT/DAT size accepted')
    print(f'self-test: {checks} checks passed (packing, fields, POSIX paths, geometry, layers, mods, '
          'loose overrides, localization, alpha metadata, output collisions, optimized validation)')



def verify_stock(result):
    """Explicit installed-data regression check; not a requirement for modded games."""
    sectors, rows = result['sectors'], result['backgrounds']
    require(len(sectors) == 239 and len(rows) == 83, 'Unexpected stock sector/background counts')
    require(Counter(s['dust'] for s in sectors) == {0: 204, 8: 6, 10: 4, 15: 2, 16: 22, 50: 1}, 'Unexpected stock dust population counts')
    require(all(s['name_resolved'] for s in sectors), 'Unresolved stock sector name')
    require(all(s['stardust_percent'] == 100 and s['stardust_target'] == 512 for s in sectors), 'Unexpected stock stardust population')
    require(all(s['neb_override'] == s['stars_override'] == 0 for s in sectors), 'Unexpected stock background override')
    require(all(row['far_native'] >= row['near_native'] for row in rows), 'Invalid stock near/far order')
    grid = {(s['x'], s['y']): s for s in sectors}
    for coord, expected in {
            (1, 3): ('Argon Prime', 2, 'bluewell', 8, 22500000, 18000000, 18500000, [45000]),
            (3, 2): ("Atreus' Clouds", 14, 'foggreenoutlands', 16, 25000000, 3000000, 3500000, [55000]),
            (0, 0): ('Kingdom End', 19, 'greenoutlands', 0, 14000000, 25000000, 30000000, [45000]),
            (16, 16): ('Veil of Delusion', 52, 'fogdeepred', 50, 20000000, 100000000, 125000000, [45000])}.items():
        row = grid[coord]
        require(tuple(row[key] for key in ('name', 'background_index', 'family', 'dust',
            'sector_size_native', 'near_native', 'far_native', 'automatic_body_sizes')) == expected, f'Unexpected representative sector: {coord}')
    require(result['map']['source'] == 'addon/02.cat', 'Unexpected stock map archive')
    require(result['backgrounds_source']['source'] == 'addon/03.cat', 'Unexpected stock background archive')
    require(sum(map(len, result['families'].values())) == 269, 'Unexpected stock cloud body count')
    alpha = [(family, material['alpha_texture']) for family, parts in result['families'].items()
             for part in parts for material in part.get('materials', [])]
    require(Counter(texture['status'] for _, texture in alpha) == {'not_referenced': 251, 'missing': 18},
            'Unexpected stock alpha texture availability')
    require({family for family, texture in alpha if texture['status'] == 'missing'} == {'uranus', 'uranus2', 'uranus3'},
            'Unexpected stock alpha texture families')
    print('stock validation: counts, names, stardust, ranges, overrides, four sectors, archive winners, alpha exceptions passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path, nargs='?')
    parser.add_argument('--mod', type=Path, action='append', default=[])
    parser.add_argument('--json', type=Path)
    parser.add_argument('--csv', type=Path)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--verify-stock', action='store_true', help='Check the documented installed AP dataset')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.game is None or args.json is None:
        parser.error('game and --json are required unless --self-test is used')
    game = args.game.expanduser().resolve()
    try:
        validate_outputs(game, (args.json, args.csv), args.mod)
    except ValueError as error:
        parser.error(str(error))
    result = census(game, [p.resolve() for p in args.mod])
    if args.verify_stock:
        verify_stock(result)
    args.json.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    if args.csv:
        write_csv(result, args.csv)
    print(json.dumps(dict(sectors=len(result['sectors']), backgrounds=len(result['backgrounds']),
        dust_positive=sum(s['dust'] > 0 for s in result['sectors']),
        dust_counts=dict(Counter(s['dust'] for s in result['sectors'])),
        families=len(result['families']), cloud_bodies=sum(map(len, result['families'].values())))))


if __name__ == '__main__':
    main()
