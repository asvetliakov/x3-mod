#!/usr/bin/env python3
"""Effect texture key table for the effects stage (docs/architecture/effects-modernisation-opus.md 2.1, 8.2).

Writes effect_keys.json: for every effect texture the stage recognises, the upload-time key the ownership layer
computes at the texture's first level-0 Unlock (effects_stage_core.h sparse_key: FNV-1a 64 over the level-0 width,
height and D3DFORMAT value, then 16 runs of 256 bytes at fixed rows and columns of the level-0 image), its class, the
owning body's local half-extent and axis and the texture's mean colour as the tint. The texture bytes are resolved
the way the engine resolves a material texture name (docs/reverse-engineering/texture-lookup.md: `dds\\<stem>` as
pck then dds, then the name as tga then jpg; a loose file under the game folder first, then the catalogues from the
highest slot down) plus the mod packages given with --mod, each mounted above every numbered catalogue in the order
given (the last wins). Mod trees are read only; a synthetic root must be a copy, never a symlink.

The DDS level 0 is hashed as D3DX uploads it: DXT1/3/5 blocks verbatim, A8R8G8B8 / X8R8G8B8 rows verbatim. Any other
file format (24-bit RGB, palettised, non-DDS) is listed with no key (`"key": null`, `"reason"`), because the upload
bytes would be converted by the loader and the game's own path is not modelled; the census rows of a capture flight
(effect_draw key=) are the evidence that reconciles the table with the installed game.

    python3 tools/effects/effect_keys.py --output tools/effects/effect_keys.json
    python3 tools/effects/effect_keys.py --game /tmp/x3-synthetic --mod /tmp/x3-mod1/addon/mods/m.cat --output /tmp/k.json

No Wine, no launch; deterministic for the same inputs (`--check` compares the output against an existing file).
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from sector_fog_census import Assets  # noqa: E402
import bob1  # noqa: E402
import body_materials  # noqa: E402

FNV_OFFSET, FNV_PRIME, MASK = 0xcbf29ce484222325, 0x100000001b3, (1 << 64) - 1
KEY_RUNS, KEY_RUN_BYTES = 16, 256
HASH_NAME = 'sparse16x256-fnv1a64'
# D3DFORMAT values (effects_stage_core.h level0_layout).
FORMATS = {b'DXT1': (0x31545844, 'DXT1'), b'DXT3': (0x33545844, 'DXT3'), b'DXT5': (0x35545844, 'DXT5'), b'DXT2': (0x32545844, 'DXT2'), b'DXT4': (0x34545844, 'DXT4')}
FMT_A8R8G8B8, FMT_X8R8G8B8 = 21, 22
# Phase 1 (effects-modernisation-opus.md 8.1, 8.4): the impact-sprite bodies and the class each texture is listed as.
# Which sprite the shield hit really is stays open until the capture flight; the class column is the table's, not
# the engine's, and --class overrides it.
DEFAULT_BODIES = ('objects/v/10659', 'objects/v/00518', 'objects/v/12007', 'objects/v/12009')
DEFAULT_CLASSES = {'exp_PL_imp_diff': 'shield_hit', 'fx_sphereshockwave_diff': 'shield_hit', 'fx_bullets2_diff': 'bolt'}
EXTENSIONS = ('.pck', '.dds', '.tga', '.jpg')


def fnv_bytes(h, data):
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK
    return h


def fnv_u32(h, value):
    return fnv_bytes(h, struct.pack('<I', value & 0xffffffff))


def level0_layout(width, height, fmt):
    """(rows, row_bytes) of the level-0 image as the key sees it, or None."""
    if fmt == 0x31545844:
        return (height + 3) // 4, ((width + 3) // 4) * 8
    if fmt in (0x32545844, 0x33545844, 0x34545844, 0x35545844):
        return (height + 3) // 4, ((width + 3) // 4) * 16
    if fmt in (FMT_A8R8G8B8, FMT_X8R8G8B8):
        return height, width * 4
    return None


def sparse_key(width, height, fmt, rows, row_bytes, level0, pitch=None):
    """The C++ sparse_key over a level-0 image given as contiguous rows (pitch defaults to row_bytes)."""
    pitch = row_bytes if pitch is None else pitch
    h = fnv_u32(fnv_u32(fnv_u32(FNV_OFFSET, width), height), fmt)
    run = min(row_bytes, KEY_RUN_BYTES)
    span = row_bytes - run + 1
    for k in range(KEY_RUNS):
        row = (k * rows) // KEY_RUNS
        column = ((k * 2654435761) & 0xffffffff) % span
        start = row * pitch + column
        h = fnv_bytes(h, level0[start:start + run])
    return h


def dds_level0(data):
    """(width, height, D3DFORMAT, format name, level-0 bytes) of a DDS, or (None, reason)."""
    if len(data) < 128 or data[:4] != b'DDS ':
        return None, 'not_dds'
    height, width = struct.unpack_from('<II', data, 12)
    pf_flags, fourcc, bits = struct.unpack_from('<I4sI', data, 80)
    masks = struct.unpack_from('<IIII', data, 92)
    if pf_flags & 4:
        if fourcc not in FORMATS:
            return None, 'fourcc_' + fourcc.decode('latin1', 'replace')
        fmt, name = FORMATS[fourcc]
    elif bits == 32 and masks[:3] == (0x00ff0000, 0x0000ff00, 0x000000ff):
        fmt, name = (FMT_A8R8G8B8, 'A8R8G8B8') if masks[3] == 0xff000000 else (FMT_X8R8G8B8, 'X8R8G8B8')
    else:
        return None, 'rgb%d_unmodelled' % bits
    rows, row_bytes = level0_layout(width, height, fmt)
    size = rows * row_bytes
    if 128 + size > len(data):
        return None, 'truncated'
    return (width, height, fmt, name, data[128:128 + size]), None


def mean_tint(data):
    decoded = body_materials.texel_rgb(data)
    if not decoded:
        return [1.0, 1.0, 1.0]
    px = decoded[2]
    mean = [sum(p[i] for p in px) / (255.0 * len(px)) for i in range(3)]
    peak = max(max(mean), 1e-3)
    return [round(v / peak, 4) for v in mean]


class Layers:
    """The game's catalogues and loose files (Assets) plus mod packages mounted above them, resolved by the
    engine's rank: a loose file under the game folder, then the mods (last given first), then the catalogues."""

    def __init__(self, game, mods):
        self.assets = Assets(Path(game))
        self.mods = []
        for cat in mods:
            cat = Path(cat)
            if cat.is_symlink() or cat.with_suffix('.dat').is_symlink():
                raise ValueError('mod packages are read through copies, never symlinks: ' + str(cat))
            from inspect_x3 import read_catalogue
            self.mods.append((cat, read_catalogue(cat)))

    def read_mod(self, cat, entry):
        with cat.with_suffix('.dat').open('rb') as stream:
            stream.seek(entry['offset'])
            raw = stream.read(entry['size'])
        from sector_fog_census import unpack
        return unpack(bytes(v ^ 0x33 for v in raw))

    def resolve(self, path):
        """(bytes, source) for one exact member path, or (None, None)."""
        key = path.replace('\\', '/').lower()
        for entry in self.assets.candidates(path):
            if 'loose' in entry:
                return self.assets.read_entry(entry), entry['source']
        for cat, entries in reversed(self.mods):
            for entry in reversed(entries):
                member = entry['path'].replace('\\', '/').lower()
                if member == key or member == 'addon/' + key:
                    return self.read_mod(cat, entry), 'mod:' + cat.name + ':' + entry['path']
        entries = self.assets.candidates(path)
        if entries:
            entry = entries[-1]
            return self.assets.read_entry(entry), entry['source'] + ':' + entry['path']
        return None, None

    def texture(self, name):
        """The engine's texture lookup for a material name: dds/<stem> as pck then dds, then <path> as tga then jpg."""
        stem = PurePosixPath(name.replace('\\', '/')).stem
        for candidate in ('dds/' + stem + '.pck', 'dds/' + stem + '.dds', 'textures/' + stem + '.tga', 'textures/' + stem + '.jpg'):
            data, source = self.resolve(candidate)
            if data is not None:
                return data, source
        return None, None

    def bodies(self, body):
        """Every body form of a stem (pbb and pbd both, in the engine's search order) as (data, source)."""
        out = []
        for ext in bob1.BODY_EXTENSIONS:
            data, source = self.resolve(body + ext)
            if data is not None:
                out.append((data, source))
        return out


def body_facts(tree):
    """(half-extent xyz, axis) of LOD 0 from its points, plus the material texture names."""
    lods = bob1.lods(tree)
    names = []
    for m in bob1.materials(tree):
        for slot, value in body_materials.slots(m).items():
            if slot == 'diffuse' and value and not body_materials.is_null(value):
                names.append(PurePosixPath(value.decode('latin1').replace('\\', '/')).stem)
    if not lods or not lods[0].get('points'):
        return [0.0, 0.0, 0.0], [0.0, 0.0, 1.0], names
    pts = [p[1:4] for p in lods[0]['points']]
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    half = [round((hi[i] - lo[i]) / 2.0 / 65536.0, 6) for i in range(3)]  # 16.16 fixed point -> body units
    axis = [0.0, 0.0, 0.0]
    axis[max(range(3), key=lambda i: half[i])] = 1.0
    return half, axis, names


def texture_entry(layers, name, cls, body, body_source, half, axis):
    texture, texture_source = layers.texture(name)
    entry = dict(name=name, **{'class': cls}, body=body, body_source=body_source, extent=half, axis=axis)
    if texture is None:
        entry.update(key=None, reason='texture_missing')
        return entry
    level0, reason = dds_level0(texture)
    entry['texture_source'] = texture_source
    entry['texture_sha256'] = hashlib.sha256(texture).hexdigest()
    if level0 is None:
        entry.update(key=None, reason=reason)
    else:
        width, height, fmt, fmt_name, image = level0
        rows, row_bytes = level0_layout(width, height, fmt)
        entry.update(key='%016x' % sparse_key(width, height, fmt, rows, row_bytes, image), width=width, height=height, format=fmt_name, tint=mean_tint(texture))
    return entry


def build(game, mods, bodies, classes, textures=()):
    layers = Layers(game, mods)
    entries, seen = [], {}

    def add(entry):
        if entry.get('key') in seen:
            seen[entry['key']].setdefault('also', []).append(dict(body=entry.get('body'), body_source=entry.get('body_source')))
            return
        if entry.get('key'):
            seen[entry['key']] = entry
        entries.append(entry)

    for body in bodies:
        for data, source in layers.bodies(body):
            try:
                tree = bob1.parse(data)
            except bob1.FormatError as error:
                entries.append(dict(body=body, source=source, error=str(error)))
                continue
            half, axis, names = body_facts(tree)
            for name in names:
                cls = classes.get(name)
                if cls is not None:
                    add(texture_entry(layers, name, cls, body, source, half, axis))
    for name, cls in textures:  # textures listed directly (no body: no extent)
        add(texture_entry(layers, name, cls, None, None, [0.0, 0.0, 0.0], [0.0, 0.0, 1.0]))
    entries.sort(key=lambda e: (e.get('name', ''), e.get('body') or '', e.get('body_source') or ''))
    return dict(schema=1, hash=HASH_NAME, tool='tools/effects/effect_keys.py', game=str(game), mods=[str(m) for m in mods],
                classes=classes, entries=entries)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    parser.add_argument('--mod', type=Path, action='append', default=[], help='a mod package .cat mounted above every numbered catalogue (repeatable; the last wins)')
    parser.add_argument('--body', action='append', default=None, help='body stems to scan (default: the four impact-sprite bodies)')
    parser.add_argument('--class', dest='classes', action='append', default=[], metavar='NAME=CLASS', help='texture stem to class (default: %s)' % DEFAULT_CLASSES)
    parser.add_argument('--texture', action='append', default=[], metavar='NAME=CLASS', help='a texture listed directly, without a body (no extent)')
    parser.add_argument('--no-default-bodies', action='store_true', help='scan no body (only --body and --texture)')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--check', action='store_true', help='compare with the existing output instead of writing it')
    args = parser.parse_args(argv)
    classes = dict(DEFAULT_CLASSES)
    for item in args.classes:
        name, _, cls = item.partition('=')
        if not name or not cls:
            parser.error('--class takes NAME=CLASS')
        classes[name] = cls
    textures = []
    for item in args.texture:
        name, _, cls = item.partition('=')
        if not name or not cls:
            parser.error('--texture takes NAME=CLASS')
        textures.append((name, cls))
    bodies = args.body or ([] if args.no_default_bodies else list(DEFAULT_BODIES))
    table = build(args.game, args.mod, bodies, classes, textures)
    text = json.dumps(table, indent=1) + '\n'
    if args.check:
        current = args.output.read_text() if args.output.exists() else ''
        if current != text:
            print('DIFFERS', args.output)
            return 1
        print('SAME', args.output, len(table['entries']))
        return 0
    args.output.write_text(text)
    keyed = sum(1 for e in table['entries'] if e.get('key'))
    print('wrote', args.output, 'entries', len(table['entries']), 'keyed', keyed)
    return 0


if __name__ == '__main__':
    sys.exit(main())
