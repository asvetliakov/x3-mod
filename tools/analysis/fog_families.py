#!/usr/bin/env python3
"""Generate <game>/x3m/fog-families.bin: volumetric-fog profiles for nebula families the build
does not compile in (mod families, opted-in missing-asset families).

Design: docs/architecture/fog-family-data.md (option C, "Implementation"). The DLL reads the
file once at the first fog sector sample and one packet per family switch; the 14 compiled
profiles always win and stay in force when the file is absent or invalid.

Inputs, through sector_fog_census.Assets (the resolver bob1.py and lod_batch_census.py use;
installed catalogues, loose files, then --mod-cat layers):

- TBackgrounds rows with NumDustInstances > 0, grouped by family name (column 7). Families the
  build compiles (tools/fog_field_recipe.py PROFILES) are reported `covered_by_build` and never
  written; their palettes are still derived so a dry run shows the regression.
- Per family: part weight w_i = sum over its records of DustBodyRate[i]; rated parts
  objects/environments/nebulae/<f>/nebula_<f>_dust_partNN (.pbd/.bod text, .pbb/.bob binary);
  every MATERIAL6 with effect nebulafog.fx; its t_DiffuseTexture as dds/<stem> (.pck/.dds/.tga).
  A part's weight is split evenly over its nebulafog materials; a texture used several times
  sums its shares; each distinct texture (decoded SHA-256) is decoded once per family.

Palette rule (fog-family-data.md §3, convention settled by the 12-palette regression in
verification/analysis/test_fog_families.py, which reproduces every provisional stock palette of
fog_field_recipe.py bit for bit from the installed 01.cat):

- DDS level 0. DXT1/3/5 colour blocks: RGB565 endpoints expanded by integer floor
  (c * 255 // 31, g * 255 // 63), palette entries by truncating integer interpolation
  ((2a + b) // 3, (a + 2b) // 3, 3-colour mode (a + b) // 2 and black); DXT3/5 always 4-colour.
  24/32-bit uncompressed: exact 8-bit channels. Alpha ignored. Texels in block order.
- Linear RGB by the IEC 61966-2-1 piecewise EOTF in float64; Y = 0.2126 R + 0.7152 G + 0.0722 B;
  texels with Y == 0 dropped. Pixel weight W_T / N_T (W_T the texture's summed share, N_T its
  nonzero texel count), so every part counts by its rate, not its resolution.
- Uniform pixel weights (one texture, as every stock family): band edges numpy.percentile
  (linear) at 25/40/55/70/85; band k = texels with edge_k <= Y <= edge_k+1 (both ends inclusive);
  stop = numpy mean of linear RGB. Non-uniform weights: the same estimator with weights, band
  edges interpolated at plotting positions (S_k - w_k) / (S_n - w_n) over the Y-sorted texels
  (which reduces to numpy's linear rule for equal weights) and weighted band means.
- Each stop divided by its largest component, rounded to 9 decimals, stored as float32. An empty
  band or a zero peak refuses the family (palette_degenerate).
- Occupancy 0.12 and base_sigma 2.5e-6 (provisional_artistic) unless --profile NAME=occ,sigma;
  mean chroma sum(rgb) / sum(density) over the baked 128^3 volume (fog_family_chroma.py's rule).

Refusals (recorded, never silent): name_invalid (not 1..31 printable ASCII, or '"' / '\\'), no_dust_bodies,
no_nebulafog_material, texture_missing, texture_format_unsupported, palette_degenerate,
profile_id_collision, table_full. `earth` (texture missing) and `xtmgreenring` (no dust bodies)
refuse by default; --background-palette NAME derives NAME's palette from its sky instead
(dds/nebula_<f>_background_part_01..04_diff, else the diffuse textures of
nebula_<f>_background_01's materials, equal weights; background_missing when neither exists).

The atlas is bake_fog_fields.py's shared field thresholded at the family occupancy and coloured
by its four stops (family_density, colour_volume, atlas_from_volume, packetize): an X3FOGPK v1
packet whose header carries the family's profile id. Identical decoded atlases share one packet
(its header id is the first family's). Profile id = FNV-1a 32 of the name | 0x10000.

File layout (little endian; the DLL validates every field, src/renderer/fog_field_assets.cpp):
  header 64: magic 'X3FOGFAM', u32 version 1, header bytes 64, recipe id 1, family count
    (1..256), packet count (1..families), family row bytes 112, packet row bytes 80, table
    offset 64, table bytes, reserved 0, u64 FNV-1a 64 of the table, u64 file size
  family row 112: name[32] (NUL-padded), u32 profile id, u32 packet index, f32 base_sigma,
    f32 occupancy, f32 chroma[3], f32 colours[4][3], u32 flags (1 background palette, 2 override)
  packet row 80: u64 offset, u64 size, u32 width 1560, height 1430, texel bytes 8, decoded bytes
    17,846,400, u64 decoded FNV-1a 64, u32 packet profile id, u32 reserved 0, decoded SHA-256
  then the packets verbatim. fog-families.json beside it records inputs, hashes and refusals.

Usage (NumPy 2.0.2 required, as for the build):
  fog_families.py --dry-run [--game DIR]                  family table and refusals, no bake
  fog_families.py --out DIR [--jobs N]                     DIR/x3m/fog-families.bin + .json
  fog_families.py --install [--replace] [--force-running]  into <game>/x3m/ (one .previous kept)
  fog_families.py --check [--out DIR]                      installed (or DIR's) file vs catalogues; a PASS
                                                           refreshes the launch fingerprint (fog_family_inputs.py)
tools/manage.py fog-families --bottle X3 [--check|--install ...] forwards here with the bottle's game directory.
Options: --mod-cat PATH ... (extra catalogue layers), --family NAME ..., --profile
NAME=occupancy,sigma, --background-palette NAME ..., --jobs N (parallel palette + bake).
Removing <game>/x3m/fog-families.bin (and .json) reverts to the compiled 14; so does
X3M_FOG_FAMILIES=0 in the DLL's environment. Native Windows: the same Python and NumPy with
--game pointing at the install; the DLL looks next to X3AP.exe.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import multiprocessing
import os
from pathlib import Path, PurePosixPath
import struct
import sys
import time

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
for _path in (HERE, ROOT / 'tools', ROOT / 'tools' / 'build'):
    if str(_path) not in sys.path:
        sys.path.insert(0, str(_path))
import bake_fog_fields as baker  # noqa: E402
import bob1  # noqa: E402
import fog_family_inputs  # noqa: E402
import fog_field_recipe as recipe  # noqa: E402
import lod_atlas  # noqa: E402
import sector_fog_census as sfc  # noqa: E402

FILE_NAME = 'fog-families.bin'
RECORD_NAME = 'fog-families.json'
MAGIC = b'X3FOGFAM'
VERSION = 1
HEADER = struct.Struct('<8s10IQQ')
FAMILY_ROW = struct.Struct('<32sIIff3f12fI')
PACKET_ROW = struct.Struct('<QQIIIIQII32s')
PACKET_HEADER = baker.HEADER
MAX_ROWS = 256
DYNAMIC_BIT = 0x10000
FLAG_BACKGROUND = 1
FLAG_OVERRIDE = 2
DECODED_BYTES = recipe.ATLAS_WIDTH * recipe.ATLAS_HEIGHT * recipe.TEXEL_BYTES
MAX_PACKET = PACKET_HEADER.size + DECODED_BYTES
MAX_FILE = HEADER.size + MAX_ROWS * (FAMILY_ROW.size + PACKET_ROW.size) + MAX_ROWS * MAX_PACKET
OCCUPANCY = 0.12
SIGMA = 2.5e-6
BANDS = (25, 40, 55, 70, 85)
COMPILED = tuple(recipe.PROFILES)
BODY_EXTENSIONS = ('.pbd', '.bod', '.pbb', '.bob')
TEXTURE_EXTENSIONS = ('.pck', '.dds', '.tga')
assert (HEADER.size, FAMILY_ROW.size, PACKET_ROW.size) == (64, 112, 80)
# IEC 61966-2-1 EOTF of every 8-bit code, float64.
_CODES = np.arange(256, dtype=np.float64) / 255.0
SRGB_TO_LINEAR = np.where(_CODES <= 0.04045, _CODES / 12.92, ((_CODES + 0.055) / 1.055) ** 2.4)


class Refusal(Exception):
    def __init__(self, reason, detail=''):
        super().__init__(reason)
        self.reason, self.detail = reason, detail


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fnv1a32(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def fnv1a64(data):
    return baker.fnv1a64(data)


def profile_id(name):
    return fnv1a32(name.encode('ascii')) | DYNAMIC_BIT


def valid_name(name):
    # The sampler escapes '"' and '\\' in the name it compares, so such a row could never match.
    return 1 <= len(name) <= 31 and all(0x20 <= ord(c) <= 0x7e and c not in '"\\' for c in name)


def round9(value):
    return float(f'{float(value):.9f}')


# --- palette -------------------------------------------------------------------------------

def decode_rgb(data):
    """Level-0 RGB uint8 texels (N, 3) under the settled palette convention (module notes)."""
    width, height, _, fmt = lod_atlas.dds_format(data)
    if not isinstance(fmt, str):
        return lod_atlas.decode_dds(data)[..., :3].reshape(-1, 3)
    bw, bh = max(1, (width + 3) // 4), max(1, (height + 3) // 4)
    size = 8 if fmt == 'DXT1' else 16
    if 128 + bw * bh * size > len(data):
        raise lod_atlas.AtlasError('truncated DDS')
    raw = np.frombuffer(data, np.uint8, bw * bh * size, 128).reshape(-1, size)
    block = raw if fmt == 'DXT1' else raw[:, 8:]
    c0 = block[:, 0].astype(np.int64) | (block[:, 1].astype(np.int64) << 8)
    c1 = block[:, 2].astype(np.int64) | (block[:, 3].astype(np.int64) << 8)

    def expand(c):
        return np.stack([((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31], -1)
    p0, p1 = expand(c0), expand(c1)
    four = ((c0 > c1) | (fmt != 'DXT1'))[:, None]
    p2 = np.where(four, (2 * p0 + p1) // 3, (p0 + p1) // 2)
    p3 = np.where(four, (p0 + 2 * p1) // 3, 0)
    palette = np.stack([p0, p1, p2, p3], 1)
    bits = block[:, 4:8].copy().view('<u4')[:, 0].astype(np.int64)
    index = (bits[:, None] >> (2 * np.arange(16))) & 3
    rgb = np.take_along_axis(palette, index[:, :, None], 1)
    if width % 4 or height % 4:
        return lod_atlas._blocks_to_image(rgb, width, height).reshape(-1, 3)
    return rgb.reshape(-1, 3)


def texture_pool(data, weight):
    """(linear RGB, Y, pixel weight, nonzero, texels) of one texture with summed share `weight`."""
    try:
        rgb = decode_rgb(data)
    except lod_atlas.AtlasError as error:
        raise Refusal('texture_format_unsupported', str(error)) from None
    linear = SRGB_TO_LINEAR[rgb]
    y = linear[:, 0] * 0.2126 + linear[:, 1] * 0.7152 + linear[:, 2] * 0.0722
    keep = y != 0
    nonzero = int(keep.sum())
    return linear[keep], y[keep], (weight / nonzero if nonzero else 0.0), nonzero, len(y)


def palette_stops(pools):
    """Four normalised linear-RGB stops (rounded to 9 decimals) and the five band edges."""
    pools = [p for p in pools if p[3] > 0 and p[2] > 0]
    if not pools:
        raise Refusal('palette_degenerate', 'no nonzero texels')
    linear = pools[0][0] if len(pools) == 1 else np.concatenate([p[0] for p in pools])
    y = pools[0][1] if len(pools) == 1 else np.concatenate([p[1] for p in pools])
    uniform = len({p[2] for p in pools}) == 1
    if uniform:
        edges = np.percentile(y, BANDS)
        weights = None
    else:
        weights = np.concatenate([np.full(len(p[1]), p[2]) for p in pools])
        order = np.argsort(y, kind='stable')
        ys, ws = y[order], weights[order]
        cumulative = np.cumsum(ws)
        span = cumulative[-1] - ws[-1]
        if len(ys) < 2 or not span > 0:
            raise Refusal('palette_degenerate', 'single texel')
        edges = np.interp(np.array(BANDS) / 100.0, (cumulative - ws) / span, ys)
    stops = []
    for k in range(4):
        band = (y >= edges[k]) & (y <= edges[k + 1])
        if not band.any():
            raise Refusal('palette_degenerate', f'empty band {BANDS[k]}-{BANDS[k + 1]}')
        mean = linear[band].mean(0) if uniform else np.average(linear[band], axis=0, weights=weights[band])
        peak = mean.max()
        if not peak > 0:
            raise Refusal('palette_degenerate', f'zero peak in band {BANDS[k]}-{BANDS[k + 1]}')
        stops.append([round9(v) for v in mean / peak])
    return stops, [float(e) for e in edges], uniform


def atlas_chroma(atlas):
    """sum(rgb) / sum(density) over the 128^3 atlas interior in float64, as float32."""
    total = np.zeros(4, np.float64)
    for z in range(recipe.GRID):
        y, x = (z // 12) * 130 + 1, (z % 12) * 130 + 1
        total += atlas[y:y + recipe.GRID, x:x + recipe.GRID].astype(np.float64).sum((0, 1))
    return [float(v) for v in (total[:3] / total[3]).astype(np.float32)]


# --- per-family job (runs in a worker with --jobs > 1) -------------------------------------

_SHARED = None


def _init_worker(shared):
    global _SHARED
    _SHARED = shared


def family_job(job):
    """Palette (and, with a shared field, the baked packet) of one family. Never raises."""
    started = time.monotonic()
    out = dict(name=job['name'], textures=[])
    try:
        pools = []
        for texture in job['textures']:
            pool = texture_pool(texture['data'], texture['weight'])
            pools.append(pool)
            out['textures'].append(dict(sha256=texture['sha256'], nonzero=pool[3], texels=pool[4]))
        stops, edges, uniform = palette_stops(pools)
        out.update(colours=stops, band_edges=edges, uniform_weights=uniform, palette_seconds=round(time.monotonic() - started, 3))
        if job.get('bake'):
            if _SHARED is None:
                raise RuntimeError('shared field missing')
            carrier, mask, eligible, t1, interpolation = _SHARED
            density = baker.family_density(carrier, mask, eligible, t1, job['occupancy'])
            atlas = baker.atlas_from_volume(baker.colour_volume(density, interpolation, np.array(stops, dtype=np.float32)))
            packet, row = baker.packetize(atlas, dict(id=job['profile_id']))
            out.update(packet=packet, packet_row=row, chroma=atlas_chroma(atlas))
    except Refusal as refusal:
        out.update(refusal=refusal.reason, detail=refusal.detail)
    out['seconds'] = round(time.monotonic() - started, 3)
    return out


# --- catalogue resolution ------------------------------------------------------------------

def body_materials(data, member):
    """[(effect, diffuse path)] of a text or binary body."""
    if member.lower().endswith(('.pbd', '.bod')):
        return [(m['effect'].strip(), m['parameters'].get('t_DiffuseTexture', '').strip()) for m in sfc.body_metadata(data)['materials']]
    materials = []
    for m in bob1.materials(bob1.parse(data)):
        effect = m.get('effect', b'').decode('latin1')
        diffuse = next((v for name, _, v in m.get('params', []) if name.lower() == b't_diffusetexture' and isinstance(v, bytes)), b'')
        materials.append((effect.strip(), diffuse.decode('latin1').strip()))
    return materials


def texture_stem(path):
    return 'dds/' + PurePosixPath(path.replace('\\', '/')).stem


def read_texture(assets, stem):
    """(bytes, provenance) of the winning texture, or (None, None); not kept in the resolver cache."""
    data, source = assets.logical(stem, TEXTURE_EXTENSIONS)
    if data is None:
        return None, None
    assets.cache.pop((source['source'], source['member']), None)
    meta = sfc.texture_metadata(data)
    return data, dict(stem=stem, member=source['member'], source=source['source'], sha256=source['decoded_sha256'],
                      format=meta.get('format'), width=meta.get('width'), height=meta.get('height'), bytes=len(data))


def background_textures(assets, family):
    """Sky textures of `family` for --background-palette: equal weights."""
    found = []
    for k in range(1, 5):
        stem = f'dds/nebula_{family}_background_part_{k:02d}_diff'
        if assets.logical(stem, TEXTURE_EXTENSIONS)[0] is not None:
            found.append(stem)
    if found:
        return found, 'background_parts'
    data, source = assets.logical(f'objects/environments/nebulae/{family}/nebula_{family}_background_01', BODY_EXTENSIONS)
    if data is None:
        return [], 'background_missing'
    materials = body_materials(data, source['member'])
    nebula = [m for m in materials if m[0].lower() == 'nebula.fx'] or materials
    stems = []
    for _, path in nebula:
        leaf = PurePosixPath(path.replace('\\', '/')).name.upper()
        if path and path not in ('0', 'NULL') and not leaf.startswith('NONE_'):
            stem = texture_stem(path)
            if stem not in stems:
                stems.append(stem)
    return stems, 'background_body'


def plan_family(assets, name, records, background, overrides):
    """Resolution of one family without decoding: parts, texture shares, status."""
    rates = [sum(r['body_rates'][i] for r in records) for i in range(8)]
    plan = dict(name=name, records=[r['index'] for r in records], rates=rates, parts=[], textures=[], missing=[],
                palette_source='dust', flags=0, occupancy=OCCUPANCY, base_sigma=SIGMA,
                density_status='provisional_artistic', status='ok', reason=None)
    if name in overrides:
        plan['occupancy'], plan['base_sigma'] = overrides[name]
        plan['flags'] |= FLAG_OVERRIDE
        plan['density_status'] = 'override'
    if not valid_name(name):
        plan.update(status='refused', reason='name_invalid')
        return plan
    shares = {}
    materials_seen = 0
    for slot in range(1, 9):
        if rates[slot - 1] <= 0:
            continue
        data, source = assets.logical(f'objects/environments/nebulae/{name}/nebula_{name}_dust_part{slot:02d}', BODY_EXTENSIONS)
        if data is None:
            plan['parts'].append(dict(slot=slot, weight=rates[slot - 1], status='missing'))
            continue
        assets.cache.pop((source['source'], source['member']), None)
        fog = [m for m in body_materials(data, source['member']) if m[0].lower() == 'nebulafog.fx']
        materials_seen += len(fog)
        plan['parts'].append(dict(slot=slot, weight=rates[slot - 1], member=source['member'], source=source['source'],
                                  sha256=source['decoded_sha256'], nebulafog_materials=len(fog)))
        for _, path in fog:
            stem = texture_stem(path) if path and path != '0' else None
            if stem is None:
                plan['missing'].append(dict(slot=slot, texture=path))
                continue
            shares[stem] = shares.get(stem, 0.0) + rates[slot - 1] / len(fog)
    if name in background:
        stems, source_kind = background_textures(assets, name)
        plan.update(palette_source=source_kind, flags=plan['flags'] | FLAG_BACKGROUND, missing=[])
        if not stems:
            plan.update(status='refused', reason='background_missing')
            return plan
        shares = {stem: 1.0 for stem in stems}
    elif not any(p.get('member') for p in plan['parts']):
        plan.update(status='refused', reason='no_dust_bodies')
        return plan
    elif not materials_seen:
        plan.update(status='refused', reason='no_nebulafog_material')
        return plan
    for stem, weight in shares.items():
        data, provenance = read_texture(assets, stem)
        if data is None:
            plan['missing'].append(dict(texture=stem))
            continue
        plan['textures'].append(dict(provenance, weight=weight))
    if plan['missing']:
        plan.update(status='refused', reason='texture_missing')
    if name in COMPILED and plan['status'] == 'ok':
        plan['status'] = 'covered_by_build'
    return plan


def texture_payload(assets, plan):
    """Distinct textures of a plan with their bytes (read at submission, dropped after)."""
    merged = {}
    for texture in plan['textures']:
        entry = merged.setdefault(texture['sha256'], dict(sha256=texture['sha256'], weight=0.0, stem=texture['stem']))
        entry['weight'] += texture['weight']
    for entry in merged.values():
        entry['data'] = read_texture(assets, entry['stem'])[0]
    return list(merged.values())


def enumerate_families(assets):
    data, source = assets.logical('types/tbackgrounds', ('.pck', '.txt'))
    if data is None:
        raise SystemExit('no types/TBackgrounds in the catalogues')
    grouped = {}
    for row in sfc.backgrounds(data):
        if row['dust'] > 0:
            grouped.setdefault(row['family'], []).append(row)
    return grouped, dict(source, records=len(sfc.backgrounds(data)))


# --- file ----------------------------------------------------------------------------------

def build_file(rows, packets):
    """Bytes of the family file. rows: dicts (name, profile_id, packet, base_sigma, occupancy,
    chroma[3], colours[4][3], flags); packets: dicts (bytes, decoded_fnv1a int, profile_id, decoded_sha256)."""
    if len(rows) > MAX_ROWS or not (1 <= len(packets) <= len(rows) if rows else not packets):
        raise ValueError('family/packet count out of range')
    table_bytes = len(rows) * FAMILY_ROW.size + len(packets) * PACKET_ROW.size
    offset = HEADER.size + table_bytes
    table = bytearray()
    for row in rows:
        table += FAMILY_ROW.pack(row['name'].encode('ascii'), row['profile_id'], row['packet'], row['base_sigma'], row['occupancy'],
                                 *row['chroma'], *[c for stop in row['colours'] for c in stop], row['flags'])
    for packet in packets:
        table += PACKET_ROW.pack(offset, len(packet['bytes']), recipe.ATLAS_WIDTH, recipe.ATLAS_HEIGHT, recipe.TEXEL_BYTES,
                                 DECODED_BYTES, packet['decoded_fnv1a'], packet['profile_id'], 0, bytes.fromhex(packet['decoded_sha256']))
        offset += len(packet['bytes'])
    header = HEADER.pack(MAGIC, VERSION, HEADER.size, recipe.RECIPE_ID, len(rows), len(packets), FAMILY_ROW.size, PACKET_ROW.size,
                         HEADER.size, table_bytes, 0, fnv1a64(bytes(table)), offset)
    return header + bytes(table) + b''.join(p['bytes'] for p in packets)


def read_file(path):
    """The DLL loader's validation (fog_field_assets.cpp load_family_file) in Python, for --check
    and the tests. Returns dict(status, reason, rows, packets); rows carry 'disabled'."""
    path = Path(path)
    if not path.exists():
        return dict(status='absent', reason='absent', rows=[], packets=[])
    if not path.is_file():
        return dict(status='rejected', reason='open_failed', rows=[], packets=[])
    size = path.stat().st_size

    def rejected(reason):
        return dict(status='rejected', reason=reason, rows=[], packets=[], bytes=size)
    if size < HEADER.size:
        return rejected('truncated_header')
    if size > MAX_FILE:
        return rejected('oversized')
    with path.open('rb') as stream:
        head = stream.read(HEADER.size)
        (magic, version, header_size, recipe_id, families, packets, row_bytes, packet_bytes,
         table_offset, table_bytes, reserved, table_fnv, file_size) = HEADER.unpack(head)
        checks = [(magic == MAGIC, 'bad_magic'), (version == VERSION, 'version'), (header_size == HEADER.size, 'header_size'),
                  (recipe_id == recipe.RECIPE_ID, 'recipe'), (families <= MAX_ROWS, 'family_count'),
                  (1 <= packets <= families if families else packets == 0, 'packet_count'),  # 0/0: empty table
                  (row_bytes == FAMILY_ROW.size and packet_bytes == PACKET_ROW.size, 'row_size'),
                  (table_offset == HEADER.size, 'table_offset'),
                  (table_bytes == families * FAMILY_ROW.size + packets * PACKET_ROW.size, 'table_bytes'),
                  (reserved == 0, 'reserved'), (file_size == size, 'file_size'),
                  (table_offset + table_bytes <= size, 'table_past_eof')]
        for ok, reason in checks:
            if not ok:
                return rejected(reason)
        stream.seek(table_offset)
        table = stream.read(table_bytes)
        if fnv1a64(table) != table_fnv:
            return rejected('table_checksum')
        table_end = table_offset + table_bytes
        packet_rows = []
        for j in range(packets):
            offset, psize, width, height, texel, decoded, fnv, profile, preserved, digest = PACKET_ROW.unpack_from(
                table, families * FAMILY_ROW.size + j * PACKET_ROW.size)
            packet_rows.append(dict(offset=offset, size=psize, width=width, height=height, texel_bytes=texel, decoded_bytes=decoded,
                                    decoded_fnv1a=fnv, profile_id=profile, reserved=preserved, decoded_sha256=digest.hex()))
        rows = []
        for i in range(families):
            raw = FAMILY_ROW.unpack_from(table, i * FAMILY_ROW.size)
            name_bytes = raw[0]
            row = dict(name_bytes=name_bytes, profile_id=raw[1], packet=raw[2], base_sigma=raw[3], occupancy=raw[4],
                       chroma=list(raw[5:8]), colours=[list(raw[8 + 3 * k:11 + 3 * k]) for k in range(4)], flags=raw[20], disabled=None)
            end = name_bytes.find(b'\0')
            text = name_bytes[:end if end >= 0 else 32]
            name_ok = (0 < end <= 31 and not any(name_bytes[end:]) and all(0x20 <= b <= 0x7e and b not in b'"\\' for b in text))
            row['name'] = text.decode('ascii') if name_ok else ''

            def unit(v):
                return v == v and 0.0 <= v <= 1.0
            sigma, occupancy = np.float32(row['base_sigma']), np.float32(row['occupancy'])
            if not name_ok:
                reason = 'name'
            elif row['name'] in COMPILED:
                reason = 'name_compiled'
            elif row['profile_id'] != profile_id(row['name']):
                reason = 'profile_id'
            elif row['packet'] >= packets:
                reason = 'packet_index'
            elif not (np.isfinite(sigma) and sigma > 0 and sigma <= np.float32(1e-4)):
                reason = 'sigma'
            elif not (np.isfinite(occupancy) and np.float32(.01) <= occupancy <= np.float32(.5)):
                reason = 'occupancy'
            elif not all(unit(v) for v in row['chroma']):
                reason = 'chroma'
            elif row['flags'] & ~(FLAG_BACKGROUND | FLAG_OVERRIDE):
                reason = 'flags'
            elif not all(unit(v) for stop in row['colours'] for v in stop):
                reason = 'colour'
            else:
                reason = None
                for earlier in rows:  # disabled or not: a name or id appears at most once
                    if earlier['name'] == row['name']:
                        reason = 'name_duplicate'
                        break
                    if earlier['profile_id'] == row['profile_id']:
                        reason = 'profile_id_duplicate'
                        break
            row['disabled'] = reason
            rows.append(row)
        for j, packet in enumerate(packet_rows):
            users = [r for r in rows if r['packet'] == j and not r['disabled']]
            if not users:
                continue
            owners = [r for r in rows if r['packet'] == j]  # the header id is the first family's, enabled or not
            reason = None
            if (packet['width'], packet['height'], packet['texel_bytes'], packet['decoded_bytes']) != (
                    recipe.ATLAS_WIDTH, recipe.ATLAS_HEIGHT, recipe.TEXEL_BYTES, DECODED_BYTES):
                reason = 'packet_dimensions'
            elif packet['decoded_fnv1a'] == 0:
                reason = 'packet_checksum_zero'
            elif (not any(r['profile_id'] == packet['profile_id'] for r in owners) or not packet['profile_id'] & DYNAMIC_BIT
                  or packet['reserved']):
                reason = 'packet_profile'
            elif not PACKET_HEADER.size < packet['size'] <= MAX_PACKET:
                reason = 'packet_size'
            elif packet['offset'] < table_end or packet['offset'] > size or packet['size'] > size - packet['offset']:
                reason = 'packet_offset'
            else:
                stream.seek(packet['offset'])
                head = PACKET_HEADER.unpack(stream.read(PACKET_HEADER.size))
                expected = (b'X3FOGPK\0', 1, PACKET_HEADER.size, packet['profile_id'], recipe.RECIPE_ID, packet['width'],
                            packet['height'], packet['texel_bytes'], packet['decoded_bytes'])
                if head[:9] != expected or head[10] != packet['decoded_fnv1a'] or head[11] != 0:
                    reason = 'packet_header'
            if reason:
                for r in rows:
                    if r['packet'] == j and not r['disabled']:
                        r['disabled'] = reason
    return dict(status='loaded', reason='ok', rows=rows, packets=packet_rows, bytes=size)


def write_atomic(path, data):
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_bytes(data)
    os.replace(temporary, path)


# --- driver --------------------------------------------------------------------------------

def running_game():
    probe = str(ROOT / 'verification' / 'probe')
    if probe not in sys.path:
        sys.path.insert(0, probe)
    from game_guard import game_running
    return game_running()


def refuse_if_running(args):
    if args.force_running:
        return
    try:
        lines = running_game()
    except RuntimeError as exc:
        raise SystemExit(f'--install: cannot tell whether the game is running ({exc}); pass --force-running to override') from None
    if lines:
        raise SystemExit(f'--install: the game is running ({lines[0]}); quit it first (--force-running overrides)')


def parse_overrides(values):
    overrides = {}
    for value in values or ():
        name, sep, numbers = value.partition('=')
        try:
            occupancy, sigma = (float(v) for v in numbers.split(','))
        except ValueError:
            raise SystemExit(f'--profile {value!r}: expected NAME=occupancy,sigma') from None
        if not sep or not name:
            raise SystemExit(f'--profile {value!r}: expected NAME=occupancy,sigma')
        if not 0.01 <= occupancy <= 0.5 or not 0 < sigma <= 1e-4:
            raise SystemExit(f'--profile {value!r}: occupancy must be in [0.01, 0.5], sigma in (0, 1e-4]')
        overrides[name] = (occupancy, sigma)
    return overrides


def derive(assets, plans, jobs, bake):
    """Runs family_job for every plan that needs a palette; returns {name: result}."""
    todo = [p for p in plans if p['status'] in ('ok', 'covered_by_build')]
    shared = baker.bake_fields() if bake and any(p['status'] == 'ok' for p in todo) else None
    results = {}

    def job(plan):
        return dict(name=plan['name'], textures=texture_payload(assets, plan), bake=bake and plan['status'] == 'ok',
                    occupancy=plan['occupancy'], profile_id=plan.get('profile_id', 0))
    if jobs <= 1:
        _init_worker(shared)
        for plan in todo:
            results[plan['name']] = family_job(job(plan))
        return results
    context = multiprocessing.get_context('spawn')
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs, mp_context=context, initializer=_init_worker,
                                                initargs=(shared,)) as pool:
        pending, queue = set(), list(todo)
        while queue or pending:
            while queue and len(pending) < 2 * jobs:  # bounded: texture bytes are read at submission
                pending.add(pool.submit(family_job, job(queue.pop(0))))
            done, pending = concurrent.futures.wait(pending, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in done:
                result = future.result()
                results[result['name']] = result
    return results


def generate(args):
    started = time.monotonic()
    game = Path(args.game).resolve()
    mods = [Path(m).resolve() for m in args.mod_cat or ()]
    assets = sfc.Assets(game, mods=mods)
    grouped, tbackgrounds = enumerate_families(assets)
    names = sorted(grouped)
    if args.family:
        unknown = sorted(set(args.family) - set(names))
        if unknown:
            raise SystemExit(f'--family: no positive-dust TBackgrounds rows named {", ".join(unknown)}')
        names = [n for n in names if n in set(args.family)]
    overrides = parse_overrides(args.profile)
    background = set(args.background_palette or ())
    plans = [plan_family(assets, name, grouped[name], background, overrides) for name in names]
    # Ids before the bake (the packet header carries them); collisions and the row cap refuse later names.
    seen = {}
    for plan in plans:
        if plan['status'] != 'ok':
            continue
        plan['profile_id'] = profile_id(plan['name'])
        if plan['profile_id'] in seen:
            plan.update(status='refused', reason='profile_id_collision', collides_with=seen[plan['profile_id']])
        elif len(seen) >= MAX_ROWS:
            plan.update(status='refused', reason='table_full')
        else:
            seen[plan['profile_id']] = plan['name']
    bake = not args.dry_run
    results = derive(assets, plans, max(1, args.jobs), bake)
    rows, packets, by_sha = [], [], {}
    for plan in plans:
        result = results.get(plan['name'])
        if result is None:
            continue
        plan['texture_stats'] = result['textures']
        plan['palette_seconds'] = result.get('palette_seconds')
        plan['seconds'] = result['seconds']
        if 'refusal' in result:
            plan.update(status='refused', reason=result['refusal'], detail=result['detail'])
            continue
        plan['colours'] = result['colours']
        plan['band_edges'] = result['band_edges']
        plan['uniform_weights'] = result['uniform_weights']
        if plan['status'] == 'covered_by_build':
            built = recipe.PROFILES[plan['name']]
            plan['matches_build'] = bool(np.array_equal(np.array(plan['colours'], np.float32), built['colours']))
            plan['build_density_status'] = built.get('density_status', 'existing_capture_tuned')
            continue
        if not bake:
            continue
        row = result['packet_row']
        plan.update(chroma=result['chroma'], decoded_sha256=row['decoded_sha256'], decoded_fnv1a=row['decoded_fnv1a'],
                    nonzero_texels=row['nonzero_texels'], packet_bytes=row['resource_bytes'])
        index = by_sha.get(row['decoded_sha256'])
        if index is None:
            index = by_sha[row['decoded_sha256']] = len(packets)
            packets.append(dict(bytes=result['packet'], decoded_fnv1a=int(row['decoded_fnv1a'], 16),
                                profile_id=plan['profile_id'], decoded_sha256=row['decoded_sha256']))
        plan['packet'] = index
        rows.append(dict(name=plan['name'], profile_id=plan['profile_id'], packet=index, base_sigma=plan['base_sigma'],
                         occupancy=plan['occupancy'], chroma=plan['chroma'], colours=plan['colours'], flags=plan['flags']))
    counts = {}
    for plan in plans:
        key = plan['status'] if plan['status'] != 'refused' else 'refused:' + plan['reason']
        counts[key] = counts.get(key, 0) + 1
    # Nothing to add still yields a file: the 64-byte empty table (0 families, 0 packets) the DLL
    # loads as 'nothing added', so the launch line can say ok and turn stale when a mod adds catalogues.
    data = None if args.dry_run else build_file(rows, packets)
    record = dict(schema=1, tool='tools/analysis/fog_families.py', tool_sha256=sha256(Path(__file__).read_bytes()),
                  baker_sha256=sha256(Path(baker.__file__).read_bytes()), recipe_sha256=sha256(Path(recipe.__file__).read_bytes()),
                  numpy=np.__version__, recipe_id=recipe.RECIPE_ID, game=str(game), catalogue_layers=assets.layers,
                  mod_cats=[str(m) for m in mods], launch_inputs=fog_family_inputs.launch_inputs(game, mods),
                  tbackgrounds=tbackgrounds, jobs=max(1, args.jobs), dry_run=args.dry_run,
                  palette_rule='fog-family-data.md §3; floor-565 DXT decode, inclusive numpy-linear percentile bands, 9-decimal stops',
                  counts=counts, families=[{k: v for k, v in p.items()} for p in plans],
                  file=None if data is None else dict(name=FILE_NAME, bytes=len(data), sha256=sha256(data), families=len(rows),
                                                      packets=len(packets)),
                  wall_seconds=None)
    record['wall_seconds'] = round(time.monotonic() - started, 2)
    return plans, record, data


def empty_table_cover(record):
    """'N compiled cover N families, R refused' for a record with nothing to add (the launch line's wording)."""
    covered = record['counts'].get('covered_by_build', 0)
    refused = sum(n for key, n in record['counts'].items() if key.startswith('refused'))
    return f'{covered} compiled cover {covered + record["file"]["packets"]} families, {refused} refused'


def print_table(plans, record):
    for p in plans:
        stops = p.get('colours')
        stop_text = ' '.join('(' + ','.join(f'{c:.3f}' for c in s) + ')' for s in stops) if stops else '-'
        extra = f" matches_build={int(p['matches_build'])}" if 'matches_build' in p else ''
        pid = f"{p['profile_id']:#010x}" if 'profile_id' in p else '-'
        print(f"{p['name']:<18} records={len(p['records']):<2} parts={sum(1 for x in p['parts'] if x.get('member'))} "
              f"textures={len({t['sha256'] for t in p['textures']})} status={p['status']}"
              f"{'/' + p['reason'] if p['reason'] else ''} id={pid} source={p['palette_source']} stops={stop_text}{extra}")
    file = record['file']
    print(json.dumps(dict(counts=record['counts'], families=len(plans), file_bytes=file and file['bytes'],
                          packets=file and file['packets'], jobs=record['jobs'], wall_seconds=record['wall_seconds'],
                          tbackgrounds=record['tbackgrounds']['source']), sort_keys=True))


def check(args):
    game = Path(args.game).resolve()
    folder = (Path(args.out).resolve() if args.out else game) / 'x3m'
    record_path, file_path = folder / RECORD_NAME, folder / FILE_NAME
    if not file_path.exists() or not record_path.exists():
        print(f'STALE missing {file_path if not file_path.exists() else record_path}')
        return 1
    loaded = read_file(file_path)
    record = json.loads(record_path.read_text())
    problems = []
    if loaded['status'] != 'loaded':
        problems.append(f"file {loaded['status']}: {loaded['reason']}")
    problems += [f"row {r['name'] or '?'} disabled: {r['disabled']}" for r in loaded['rows'] if r['disabled']]
    if record.get('file', {}) and sha256(file_path.read_bytes()) != record['file']['sha256']:
        problems.append('file differs from its record')
    for key, module in (('baker_sha256', baker), ('recipe_sha256', recipe)):
        if record.get(key) != sha256(Path(module.__file__).read_bytes()):
            problems.append(f'{key} changed since generation')
    assets = sfc.Assets(game, mods=[Path(m) for m in record.get('mod_cats', ())])
    grouped, tbackgrounds = enumerate_families(assets)
    if tbackgrounds['decoded_sha256'] != record['tbackgrounds']['decoded_sha256']:
        problems.append('TBackgrounds changed')
    for family in record['families']:
        if family.get('packet') is None:
            continue
        rows = grouped.get(family['name'])
        if not rows:
            problems.append(f"{family['name']}: no longer a positive-dust family")
            continue
        background = {family['name']} if family['flags'] & FLAG_BACKGROUND else set()
        overrides = {family['name']: (family['occupancy'], family['base_sigma'])} if family['flags'] & FLAG_OVERRIDE else {}
        now = plan_family(assets, family['name'], rows, background, overrides)
        if sorted(t['sha256'] for t in now['textures']) != sorted(t['sha256'] for t in family['textures']):
            problems.append(f"{family['name']}: texture inputs changed")
        if [p.get('sha256') for p in now['parts']] != [p.get('sha256') for p in family['parts']] or now['rates'] != family['rates']:
            problems.append(f"{family['name']}: body inputs or rates changed")
    for problem in problems:
        print('STALE', problem)
    if not problems:
        print(f"PASS {file_path} families={len(loaded['rows'])} packets={len(loaded['packets'])} bytes={loaded['bytes']}")
        # Every input still matches: refresh the stat fingerprint the launcher compares, so a touched
        # or re-copied catalogue does not keep the launch line at 'stale'.
        stale = fog_family_inputs.launch_difference(game, record)
        if stale is not None or 'launch_inputs' not in record:
            record['launch_inputs'] = fog_family_inputs.launch_inputs(game, record.get('mod_cats') or ())
            write_atomic(record_path, (json.dumps(record, indent=1, sort_keys=True) + '\n').encode())
            print(f'launch fingerprint refreshed ({stale or "record predates it"})')
    return 1 if problems else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    parser.add_argument('--mod-cat', type=Path, nargs='+', help='extra catalogue layers (later wins), as sector_fog_census --mod')
    parser.add_argument('--family', nargs='+', help='restrict to these families')
    parser.add_argument('--profile', action='append', metavar='NAME=OCCUPANCY,SIGMA', help='per-family density override (recorded)')
    parser.add_argument('--background-palette', nargs='+', metavar='NAME', help='derive NAME\'s palette from its sky (opt-in; earth, xtmgreenring)')
    parser.add_argument('--out', type=Path, help='output root outside the game directory: writes OUT/x3m/fog-families.bin and .json')
    parser.add_argument('--install', action='store_true', help='write into <game>/x3m/ (refuses while X3AP runs)')
    parser.add_argument('--replace', action='store_true', help='with --install: replace an installed file, keeping one .previous')
    parser.add_argument('--force-running', action='store_true', help='with --install: skip the running-game refusal')
    parser.add_argument('--dry-run', action='store_true', help='resolve and derive palettes, print the table; no bake, no write')
    parser.add_argument('--check', action='store_true', help='validate the installed (or --out) file against the current catalogues')
    parser.add_argument('--jobs', type=int, default=min(4, os.cpu_count() or 1), help='parallel family jobs (default min(4, cpus))')
    args = parser.parse_args(argv)
    if np.__version__ != recipe.REQUIRED_NUMPY:
        raise SystemExit(f'NumPy {recipe.REQUIRED_NUMPY} required, found {np.__version__}')
    game = Path(args.game).resolve()
    if args.check:
        return check(args)
    if not args.dry_run and args.install == (args.out is not None):
        raise SystemExit('pass exactly one of --out DIR or --install (or --dry-run / --check)')
    if args.out is not None:
        out = args.out.resolve()
        if out == game or out.is_relative_to(game):
            raise SystemExit('--out must be outside the game directory (use --install to target it)')
    target = game / 'x3m' if args.install else (args.out.resolve() / 'x3m' if args.out else None)
    if args.install and not args.dry_run:
        refuse_if_running(args)
        if (target / FILE_NAME).exists() and not args.replace:
            raise SystemExit(f'{target / FILE_NAME} exists; pass --replace (the current file is kept as .previous)')
    plans, record, data = generate(args)
    print_table(plans, record)
    if args.dry_run:
        return 0
    if args.install:
        refuse_if_running(args)  # again: the bake can take minutes
        if (target / FILE_NAME).exists() and not args.replace:
            raise SystemExit(f'{target / FILE_NAME} appeared during the bake; pass --replace')
    target.mkdir(parents=True, exist_ok=True)
    moved = []
    try:
        if args.install:
            for name in (FILE_NAME, RECORD_NAME):
                if (target / name).exists():
                    os.replace(target / name, target / (name + '.previous'))
                    moved.append(name)
        write_atomic(target / FILE_NAME, data)
        write_atomic(target / RECORD_NAME, (json.dumps(record, indent=1, sort_keys=True) + '\n').encode())
        loaded = read_file(target / FILE_NAME)
        if loaded['status'] != 'loaded' or any(r['disabled'] for r in loaded['rows']):
            raise RuntimeError(f'written file fails validation: {loaded["reason"]}')
    except (OSError, RuntimeError) as error:
        if args.install:
            # Roll back: the new pair goes, the moved pair returns to its names.
            for name in (FILE_NAME, RECORD_NAME):
                (target / (name + '.tmp')).unlink(missing_ok=True)
                if name in moved:
                    os.replace(target / (name + '.previous'), target / name)
                else:
                    (target / name).unlink(missing_ok=True)
        raise SystemExit(f'{error}; ' + ('the previous installation was restored' if args.install else 'output incomplete')) from None
    print(f"wrote {target / FILE_NAME} bytes={len(data)} families={record['file']['families']} packets={record['file']['packets']}"
          + ('' if record['file']['families'] else f' (empty table: nothing to add; {empty_table_cover(record)})'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
