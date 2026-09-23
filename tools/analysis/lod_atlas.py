#!/usr/bin/env python3
"""Texture-atlas collapse of a merged-LOD coarse record (lod_overlay.py --collapse atlas).

The coarse record C (a copy of the body's coarsest record, or of the lod_overlay.py
--source-record) is drawn with ONE opaque
material per body whose diffuse and light-map textures are atlases baked from the
materials it replaces; alpha-tested/blended materials keep one merged group per part
as in --collapse two. Layout (docs/architecture/merged-lod-feasibility.md, "Overlay
tooling"; census: tools/analysis/atlas_census.py):

- One tile per distinct texture set (diffuse, light map, bump[, specular]) of the opaque
  materials. Each face is moved by an integer UV shift (floor of its min u / min v,
  tolerance EPS) so it starts in the first period; the tile holds the union of the
  shifted face ranges (its whole span, so a tiling texture repeats inside the tile).
  Faces are never split. A point used by faces that need different shifts (or
  different tiles, or an alpha face) is duplicated; the first use keeps the index.
- Tile content = source texels over the span (max of the slot textures' sizes per
  axis; NULL / NONE_* do not count) times one uniform scale <= 1, rounded up to 4
  texels, plus a GUTTER (8) texel gutter per side; shelf packing (tallest first) into N x N
  finds the largest scale that fits. N is the first of --atlas-size, 2x, ... up to
  --atlas-max-size that fits at full source density (scale 1) or whose minimum
  atlas-texels-per-screen-pixel over the tiles is
  >= MIN_RATIO (2) at the switch size px = T_pad * screen_width / 1280 (T is in the 1280-wide
  reference: real px = s * m00 * width / 1280; lod_overlay.py --screen-width) (inferred need per tile:
  sqrt(face area / UV area) * px / radius, radius = max |position|; as atlas_census).
- UVs: u' = (cx + (u - shift_u - lo_u) * cw / span_u) / N (v likewise; texel edges,
  16.16 rounded). The mapping is a positive per-axis affine map, so the tangent and
  binormal directions of the per-group 7-int records are unchanged: records are
  regenerated per output group as one record per referenced point index, in the order
  the group's faces first use the points (both the shipped invariant), copies of the
  source point's record.
- Baking: DXT1/3/5 and uncompressed sources are decoded, the tile (content and
  gutter) is area-resampled from the repeating source over its UV range extended by
  the gutter (the gutter holds the true neighbouring texels of the repeat, instead of
  a clamped edge), NULL light/specular maps are constant black (the engine's 32x32
  placeholder is black: hull-self-illumination.md; alpha 0 as NONE_BLACK, since the
  shipped light maps carry an alpha that follows their RGB), NONE_* maps are resampled
  like any texture. Unused atlas area is black (diffuse alpha 255, light/specular 0; bump flat).
- Mip chain (to 1x1), tile-aware: every level is baked the same way at its own density,
  each tile's footprint (content + gutter, rounded out to whole level texels) area-resampled
  from the tile's own repeating source, the texels overlapping a tile's content written
  last, the unused area kept at the background. A box filter of the whole atlas would mix
  neighbouring tiles (and the unused area) into tile borders once the gutter is below one
  texel (from mip 3 at a 4-texel gutter); here the gutter stays the tile's own repeat at every
  level and bright tiles cannot bleed into neighbours. With the 8-texel gutter a whole
  gutter texel survives to mip 3; contents of adjacent tiles share texels only from the
  level where the 16-texel gap between them falls below one texel.
- Bump: the shipped bump maps are swizzled tangent-space normal maps (DXT5, x in alpha,
  y in R = G = B, z implied; bump_maps_out.txt). They are decoded to unit vectors,
  resampled as vectors and renormalised at every mip level (tile-aware, above); a NULL
  bump is the flat normal (0, 0, 1). Re-encoded in
  the same swizzle, so the bump atlas is DXT5 like its sources.
- Output format (--atlas-format): dxt (default) = DXT1, or DXT5 when a slot's baked
  alpha is not uniformly 255; per 4x4 block, endpoints from the min/max projection on
  the principal RGB axis, RGB565, 4-colour palette, nearest index (DXT5 alpha: min/max,
  8-value palette, nearest index). a8r8g8b8 = uncompressed (debugging).
- Group size: an output group referencing more than MAX_GROUP_POINTS (60,000) distinct points
  is split into groups of the same material, each within the limit: whole source groups are
  packed first-fit in decreasing size (a source group above the limit is cut in face order);
  a point used by an earlier split group is duplicated with its tangent record, so split
  groups share no point (the engine builds one mesh per group; the shipped outpost record 0
  has 125,867 points over 34 groups). The limit leaves room below 65,535 for the bowtie
  vertices D3DXCleanMesh adds at load (0x004bc680).
- Hidden parts: a part whose flags carry HIDDEN_PART (0x8000; stored at part +0x60, and the
  collection helper 0x0047d9c0 skips such a part, emission-draw-order.md) is not atlased:
  its groups are copied with their own materials and UVs (points shared with atlased faces
  are duplicated), and its materials take no part in the tiles, the effect check or the
  g_Mat* mean. Record 0 of argon_TL / M2 / M1 has one (a 24-point box).
- Refusals: a record with a second UV set (point flag 0x04; only the first pair is
  rewritten) unless force_uv2, and opaque materials of more than one effect file unless
  force_mixed_effects.
- Material: a copy of the dominant opaque material (face count over all parts) with
  t_DiffuseTexture / t_LightMapTexture = the atlases, t_SpecularTexture = the
  specular atlas (--atlas-specular) or NULL (as 120 of 7198 shipped argon.fx
  materials), t_BumpTexture = the bump atlas (default; NULL with bump off),
  t_AlphaTexture NULL,
  every g_Mat* FLOAT the face-area-weighted mean over all atlased materials (unless
  synth is off), appended to MAT6 with record index = position.
- Textures: member dds/x3m_lod_<body>_<slot>.pck (gzip DDS, as every shipped texture)
  named in the material as x3m_lod\\x3m_lod_<body>_<slot>.tga: the engine resolves a
  material texture name by its stem under dds\\ (shipped names carry a directory and
  .tga while the members are dds/<stem>.pck; the texture loader 0x004dc540 takes the
  extension list "pck dds", loading-orchestration.md).
"""
import gzip
import hashlib
import math
import struct
import zlib
from pathlib import PurePosixPath

import numpy as np

import bob1
import body_materials

EPS = 0.01                 # UV slack for the integer shift (atlas_census.EPS)
BLOCK = 4
GUTTER = 8                 # texels per side; a whole gutter texel survives to mip 3
UV_ONE = 65536.0
MIN_RATIO = 2.0
MAX_GROUP_POINTS = 60000   # distinct points per output group: 16-bit indices per subgroup, with headroom
                           # for D3DXCleanMesh (0x004bc680, flags 3) splitting bowtie vertices before
                           # the vertex count is taken (fan_estimate.py)
HIDDEN_PART = 0x8000       # part flag: skipped by the collection helper 0x0047d9c0 (never atlased)
SLOT_NAMES = {'diffuse': b't_diffusetexture', 'light': b't_lightmaptexture', 'bump': b't_bumptexture',
              'specular': b't_speculartexture'}
NULL_TEXEL = (0.0, 0.0, 0.0, 0.0)   # NULL light/specular map: black placeholder; alpha 0 like NONE_BLACK
FORMATS = ('dxt', 'a8r8g8b8')


class AtlasError(ValueError):
    pass


# --- DDS decode / encode -------------------------------------------------------------------

def _expand565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.int32)


def _palette(c0, c1, four):
    p0, p1 = _expand565(c0), _expand565(c1)
    f = four[:, None]
    p2 = np.where(f, (2 * p0 + p1 + 1) // 3, (p0 + p1) // 2)
    p3 = np.where(f, (p0 + 2 * p1 + 1) // 3, 0)
    return np.stack([p0, p1, p2, p3], 1)                          # (n, 4, 3)


def _decode_color(blk, dxt1):
    c0 = blk[:, 0].astype(np.uint32) | (blk[:, 1].astype(np.uint32) << 8)
    c1 = blk[:, 2].astype(np.uint32) | (blk[:, 3].astype(np.uint32) << 8)
    bits = blk[:, 4:8].copy().view('<u4')[:, 0]
    four = (c0 > c1) | (not dxt1)
    pal = _palette(c0, c1, four)
    idx = (bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))) & 3
    rgb = np.take_along_axis(pal, idx[:, :, None].astype(np.int64), 1)
    a = np.full(idx.shape, 255, np.int32)
    if dxt1:
        a[(idx == 3) & ~four[:, None]] = 0
    return rgb, a


def _alpha_palette(a0, a1):
    a0, a1 = a0.astype(np.int32), a1.astype(np.int32)
    k = np.arange(1, 7)
    eight = ((7 - k[None, :]) * a0[:, None] + k[None, :] * a1[:, None] + 3) // 7
    six = ((5 - k[None, :4]) * a0[:, None] + k[None, :4] * a1[:, None] + 2) // 5
    six = np.concatenate([six, np.zeros((len(a0), 1), np.int32), np.full((len(a0), 1), 255, np.int32)], 1)
    rest = np.where((a0 > a1)[:, None], eight, six)
    return np.concatenate([a0[:, None], a1[:, None], rest], 1)     # (n, 8)


def _decode_dxt5_alpha(blk):
    bits = np.zeros(len(blk), np.uint64)
    for k in range(6):
        bits |= blk[:, 2 + k].astype(np.uint64) << np.uint64(8 * k)
    idx = (bits[:, None] >> (np.uint64(3) * np.arange(16, dtype=np.uint64))) & np.uint64(7)
    return np.take_along_axis(_alpha_palette(blk[:, 0], blk[:, 1]), idx.astype(np.int64), 1)


def _blocks_to_image(texels, mw, mh):
    bw, bh = max(1, (mw + 3) // 4), max(1, (mh + 3) // 4)
    img = texels.reshape(bh, bw, 4, 4, texels.shape[-1]).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, -1)
    return img[:mh, :mw]


def dds_format(data):
    """(width, height, mips, fmt) with fmt 'DXT1'/'DXT3'/'DXT5' or ('RGB', bits, masks, alpha mask)."""
    if len(data) < 128 or data[:4] != b'DDS ':
        raise AtlasError('not a DDS file')
    h, w = struct.unpack_from('<II', data, 12)
    mips = max(1, struct.unpack_from('<I', data, 28)[0])
    pf_flags, fourcc, bits = struct.unpack_from('<I4sI', data, 80)
    if pf_flags & 4:
        if fourcc not in (b'DXT1', b'DXT3', b'DXT5'):
            raise AtlasError(f'unsupported DDS fourcc {fourcc!r}')
        return w, h, mips, fourcc.decode()
    if bits not in (24, 32):
        raise AtlasError(f'unsupported uncompressed DDS bit count {bits}')
    masks = struct.unpack_from('<4I', data, 92)
    return w, h, mips, ('RGB', bits, masks[:3], masks[3] if pf_flags & 1 else 0)


def _level_bytes(fmt, mw, mh):
    if isinstance(fmt, str):
        return max(1, (mw + 3) // 4) * max(1, (mh + 3) // 4) * (8 if fmt == 'DXT1' else 16)
    return mw * mh * fmt[1] // 8


def decode_dds(data, level=0):
    """RGBA uint8 array (h, w, 4) of one mip level of a DXT1/3/5 or 24/32-bit DDS."""
    w, h, mips, fmt = dds_format(data)
    if not 0 <= level < mips:
        raise AtlasError(f'mip {level} outside 0..{mips - 1}')
    off, mw, mh = 128, w, h
    for _ in range(level):
        off += _level_bytes(fmt, mw, mh)
        mw, mh = max(1, mw // 2), max(1, mh // 2)
    size = _level_bytes(fmt, mw, mh)
    if off + size > len(data):
        raise AtlasError('truncated DDS')
    raw = np.frombuffer(data, np.uint8, size, off)
    if isinstance(fmt, str):
        bsize = 8 if fmt == 'DXT1' else 16
        blk = raw.reshape(-1, bsize)
        if fmt == 'DXT1':
            rgb, a = _decode_color(blk, True)
        else:
            rgb, _ = _decode_color(blk[:, 8:], False)
            if fmt == 'DXT3':
                nib = np.stack([blk[:, :8] & 15, blk[:, :8] >> 4], -1).reshape(-1, 16)
                a = nib.astype(np.int32) * 17
            else:
                a = _decode_dxt5_alpha(blk[:, :8])
        tex = np.concatenate([rgb, a[:, :, None]], -1)
        return _blocks_to_image(tex, mw, mh).astype(np.uint8)
    _, bits, masks, amask = fmt
    step = bits // 8
    px = raw.reshape(-1, step).astype(np.uint32)
    v = sum(px[:, k] << np.uint32(8 * k) for k in range(step))
    chans = []
    for c, m in enumerate(masks + (amask,)):
        if not m:
            chans.append(np.full(v.shape, 255 if c == 3 else 0, np.uint32))
            continue
        sh = (m & -m).bit_length() - 1
        chans.append(((v & m) >> sh) * 255 // (m >> sh))
    return np.stack(chans, -1).reshape(mh, mw, 4).astype(np.uint8)


def _to565(rgb):
    q = np.rint(np.clip(rgb, 0, 255) * np.array([31, 63, 31]) / 255.0).astype(np.uint32)
    return (q[:, 0] << 11) | (q[:, 1] << 5) | q[:, 2]


def _image_to_blocks(img):
    h, w = img.shape[:2]
    ph, pw = max(4, -(-h // 4) * 4), max(4, -(-w // 4) * 4)
    if (ph, pw) != (h, w):
        img = np.pad(img, ((0, ph - h), (0, pw - w), (0, 0)), mode='edge')
    return img.reshape(ph // 4, 4, pw // 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4)


def _encode_color(rgb):
    """rgb (n, 16, 3) float -> (n, 8) uint8 DXT1-mode colour blocks (always 4-colour, c0 > c1)."""
    mean = rgb.mean(1)
    d = rgb - mean[:, None]
    cov = np.einsum('nki,nkj->nij', d, d)
    axis = np.linalg.eigh(cov)[1][:, :, 2]                       # principal axis (largest eigenvalue)
    t = np.einsum('nki,ni->nk', d, axis)
    hi = _to565(mean + t.max(1)[:, None] * axis)
    lo = _to565(mean + t.min(1)[:, None] * axis)
    c0, c1 = np.maximum(hi, lo), np.minimum(hi, lo)
    pal = _palette(c0, c1, np.ones(len(c0), bool)).astype(np.float32)
    dist = ((rgb[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)
    idx = dist.argmin(-1).astype(np.uint32)
    idx[c0 == c1] = 0                                            # equal endpoints: 3-colour mode, index 0 only
    bits = (idx << (2 * np.arange(16, dtype=np.uint32))).sum(1, dtype=np.uint64).astype(np.uint32)
    out = np.zeros((len(rgb), 8), np.uint8)
    out[:, 0:2] = c0.astype('<u2').view(np.uint8).reshape(-1, 2)
    out[:, 2:4] = c1.astype('<u2').view(np.uint8).reshape(-1, 2)
    out[:, 4:8] = bits.astype('<u4').view(np.uint8).reshape(-1, 4)
    return out


def _encode_alpha(a):
    """a (n, 16) float -> (n, 8) uint8 DXT5 alpha blocks (8-value mode, a0 > a1, or a0 == a1)."""
    q = np.rint(np.clip(a, 0, 255)).astype(np.int32)
    a0, a1 = q.max(1), q.min(1)
    pal = _alpha_palette(a0, a1)
    idx = np.abs(q[:, :, None] - pal[:, None, :]).argmin(-1).astype(np.uint64)
    idx[a0 == a1] = 0
    bits = (idx << (np.uint64(3) * np.arange(16, dtype=np.uint64))).sum(1, dtype=np.uint64)
    out = np.zeros((len(a), 8), np.uint8)
    out[:, 0], out[:, 1] = a0, a1
    for k in range(6):
        out[:, 2 + k] = ((bits >> np.uint64(8 * k)) & np.uint64(255)).astype(np.uint8)
    return out


def encode_dxt(img, fourcc, chunk=32768):
    """RGBA uint8 (h, w, 4) -> DXT1 or DXT5 level bytes."""
    blocks = _image_to_blocks(img).astype(np.float32)
    parts = []
    for s in range(0, len(blocks), chunk):
        b = blocks[s:s + chunk]
        color = _encode_color(b[:, :, :3])
        parts.append(color if fourcc == 'DXT1' else np.concatenate([_encode_alpha(b[:, :, 3]), color], 1))
    return np.concatenate(parts).tobytes()


def dds_header(w, h, mips, fmt):
    d = bytearray(128)
    d[:4] = b'DDS '
    if fmt == 'A8R8G8B8':
        struct.pack_into('<7I', d, 4, 124, 0x2100f, h, w, w * 4, 0, mips)
        struct.pack_into('<8I', d, 76, 32, 0x41, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000)
    else:
        struct.pack_into('<7I', d, 4, 124, 0xa1007, h, w, _level_bytes(fmt, w, h), 0, mips)
        struct.pack_into('<II4s', d, 76, 32, 4, fmt.encode())
    struct.pack_into('<I', d, 108, 0x401008)                     # COMPLEX | TEXTURE | MIPMAP
    return bytes(d)


def mip_chain(img):
    """Box mip chain of a float (n, n, 4) power-of-two image, as rounded uint8 levels."""
    levels, cur = [], img.astype(np.float64)
    while True:
        levels.append(np.clip(np.rint(cur), 0, 255).astype(np.uint8))
        h, w = cur.shape[:2]
        if h == 1 and w == 1:
            return levels
        if h > 1:
            cur = 0.5 * (cur[0::2] + cur[1::2])
        if w > 1:
            cur = 0.5 * (cur[:, 0::2] + cur[:, 1::2])


def to_normals(img):
    """Swizzled normal map (x in A, y in G) -> float (h, w, 3) vectors, z = sqrt(1 - x^2 - y^2)."""
    f = np.asarray(img, np.float64)
    x, y = f[..., 3] * (2 / 255) - 1, f[..., 1] * (2 / 255) - 1
    return np.stack([x, y, np.sqrt(np.clip(1 - x * x - y * y, 0, None))], -1)


def normalize(v):
    n = np.linalg.norm(v, axis=-1, keepdims=True)
    flat = np.zeros_like(v)
    flat[..., 2] = 1
    return np.where(n > 1e-8, v / np.maximum(n, 1e-8), flat)


def from_normals(v):
    """Unit vectors -> float RGBA in the source swizzle: R = G = B = y, A = x."""
    x, y = (v[..., 0] + 1) * 127.5, (v[..., 1] + 1) * 127.5
    return np.stack([y, y, y, x], -1)


def normal_mip_chain(vec):
    """Mip chain of a (n, n, 3) vector image: each level the box filter of the previous one,
    renormalised; returned as rounded uint8 swizzled RGBA levels."""
    levels, cur = [], normalize(np.asarray(vec, np.float64))
    while True:
        levels.append(np.clip(np.rint(from_normals(cur)), 0, 255).astype(np.uint8))
        h, w = cur.shape[:2]
        if h == 1 and w == 1:
            return levels
        if h > 1:
            cur = 0.5 * (cur[0::2] + cur[1::2])
        if w > 1:
            cur = 0.5 * (cur[:, 0::2] + cur[:, 1::2])
        cur = normalize(cur)


def write_dds(levels, fmt):
    h, w = levels[0].shape[:2]
    out = [dds_header(w, h, len(levels), fmt)]
    for lv in levels:
        out.append(lv[:, :, [2, 1, 0, 3]].tobytes() if fmt == 'A8R8G8B8' else encode_dxt(lv, fmt))
    return b''.join(out)


# --- texture resolution -------------------------------------------------------------------

def texture_bytes(assets, name):
    """Decoded DDS bytes of a material texture name (stem under dds/), None for NULL."""
    if name is None or body_materials.is_null(name):
        return None
    stem = 'dds/' + PurePosixPath(name.decode('latin1').replace('\\', '/')).stem
    try:
        data, _ = assets.logical(stem, ('.pck', '.dds', '.tga'))
    except ValueError as exc:
        raise AtlasError(f'texture {name!r}: {exc}') from None
    if data is None:
        raise AtlasError(f'texture {name!r} does not resolve under dds/')
    if data[:4] != b'DDS ':
        raise AtlasError(f'texture {name!r} is not a DDS file')
    return data


class Textures:
    """Decoded mip 0 of material textures by name (lower-case), None for NULL."""
    def __init__(self, assets):
        self.assets, self.cache = assets, {}

    def get(self, name):
        key = None if name is None or body_materials.is_null(name) else name.lower()
        if key not in self.cache:
            data = texture_bytes(self.assets, name)
            self.cache[key] = None if data is None else decode_dds(data)
        return self.cache[key]

    def size(self, name):
        """(w, h) of a real (not NULL, not NONE_*) texture, else None."""
        if name is None or body_materials.is_null(name) or body_materials.is_stock(name):
            return None
        data = texture_bytes(self.assets, name)
        w, h, _, _ = dds_format(data)
        return w, h


# --- layout -------------------------------------------------------------------------------

def uv_offset(flags):
    return (4 if flags & 1 else 1) if flags & 2 else None


def point_uv(p):
    o = uv_offset(p[0])
    return None if o is None else (p[o] / UV_ONE, p[o + 1] / UV_ONE)


def with_uv(p, u, v):
    o = uv_offset(p[0])
    q = list(p)
    q[o], q[o + 1] = u, v
    return tuple(q)


def face_shift(uvs):
    return (math.floor(min(u for u, _ in uvs) + EPS), math.floor(min(v for _, v in uvs) + EPS))


def material_slots(material, slots=('diffuse', 'light')):
    s = body_materials.slots(material)
    return {k: s.get(k) for k in slots}


def tile_key(slots):
    return tuple((k, None if v is None or body_materials.is_null(v) else v.lower()) for k, v in slots.items())


def _side(full, scale):
    return max(BLOCK, BLOCK * math.ceil(full * scale / BLOCK - 1e-9))


def shelf_pack(rects, n):
    """Shelf packing, tallest first (ties: wider, then index), into n x n: [(x, y)] or None."""
    order = sorted(range(len(rects)), key=lambda i: (-rects[i][1], -rects[i][0], i))
    pos, x, y, shelf = [None] * len(rects), 0, 0, 0
    for i in order:
        w, h = rects[i]
        if w > n or h > n:
            return None
        if x + w > n:
            y, x, shelf = y + shelf, 0, 0
        if y + h > n:
            return None
        pos[i] = (x, y)
        x, shelf = x + w, max(shelf, h)
    return pos


def fit(tiles, n, gutter):
    """Largest uniform scale <= 1 whose tiles shelf-pack into n x n: (scale, contents, positions)."""
    def attempt(s):
        cont = [(_side(t['full'][0], s), _side(t['full'][1], s)) for t in tiles]
        return cont, shelf_pack([(w + 2 * gutter, h + 2 * gutter) for w, h in cont], n)
    cont, pos = attempt(1.0)
    if pos is not None:
        return 1.0, cont, pos
    lo, hi = 0.0, 1.0
    for _ in range(40):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if attempt(mid)[1] is not None else (lo, mid)
    cont, pos = attempt(lo)
    if pos is None:
        raise AtlasError(f'{len(tiles)} tiles do not fit a {n}x{n} atlas at any scale')
    return lo, cont, pos


def plan_layout(record, mats, alpha, textures, px, sizes=(1024, 2048), gutter=GUTTER, slots=('diffuse', 'light'),
                min_ratio=MIN_RATIO):
    """Tiles, per-face keys and the chosen atlas size for the opaque faces of `record`."""
    pts = record['points']
    radius = max((math.sqrt(p[1] ** 2 + p[2] ** 2 + p[3] ** 2) for p in pts if p[0] & 1), default=0.0)
    tiles, tile_of, face_keys = [], {}, {}
    for pi, part in enumerate(record['parts']):
        if part['flags'] & HIDDEN_PART:
            continue
        for gi, g in enumerate(part['groups']):
            mi = g['material']
            if mi in alpha:
                continue
            if mi not in tile_of:
                if not 0 <= mi < len(mats) or 'params' not in mats[mi]:
                    raise AtlasError(f'material {mi} is not an effect material; cannot atlas it')
                names = material_slots(mats[mi], slots)
                if names['diffuse'] is None or body_materials.is_null(names['diffuse']):
                    raise AtlasError(f'material {mi} has no diffuse texture')
                key = tile_key(names)
                t = next((t for t in tiles if t['key'] == key), None)
                if t is None:
                    t = dict(key=key, names=names, mats=[], lo=[math.inf, math.inf], hi=[-math.inf, -math.inf],
                             area=0.0, uv_area=0.0, faces=0)
                    tiles.append(t)
                t['mats'].append(mi)
                tile_of[mi] = tiles.index(t)
            ti = tile_of[mi]
            t = tiles[ti]
            for fi, f in enumerate(g['faces']):
                uvs = [point_uv(pts[i]) for i in f[:3]]
                if any(x is None for x in uvs):
                    raise AtlasError(f'material {mi}: a face uses a point without UV')
                su, sv = face_shift(uvs)
                face_keys[pi, gi, fi] = (ti, su, sv)
                for k, (lo_, hi_) in enumerate(((min(u for u, _ in uvs) - su, max(u for u, _ in uvs) - su),
                                                (min(v for _, v in uvs) - sv, max(v for _, v in uvs) - sv))):
                    t['lo'][k] = min(t['lo'][k], lo_)
                    t['hi'][k] = max(t['hi'][k], hi_)
                t['area'] += body_materials.face_area(pts, f)
                (a, b), (c, d), (e, h) = uvs
                t['uv_area'] += 0.5 * abs((c - a) * (h - b) - (e - a) * (d - b))
                t['faces'] += 1
    for t in tiles:
        sizes_ = [s for s in (textures.size(v) for v in t['names'].values()) if s]
        t['base'] = (max(s[0] for s in sizes_), max(s[1] for s in sizes_)) if sizes_ else (32, 32)
        if t['faces'] == 0:
            t['lo'], t['hi'] = [0.0, 0.0], [1.0, 1.0]
        span = []
        for k in range(2):
            w = t['hi'][k] - t['lo'][k]
            if w < 1.0 / t['base'][k]:                 # at least one source texel, centred
                mid = (t['hi'][k] + t['lo'][k]) / 2
                w = 1.0 / t['base'][k]
                t['lo'][k] = mid - w / 2
            span.append(w)
        t['span'] = tuple(span)
        t['full'] = (t['base'][0] * span[0], t['base'][1] * span[1])
        t['need'] = (math.sqrt(t['area'] / t['uv_area']) * px / radius
                     if px and radius and t['uv_area'] > 0 else None)
    if not tiles:
        return None
    tried = []
    for n in sizes:
        scale, cont, pos = fit(tiles, n, gutter)
        ratios = [math.sqrt(c[0] * c[1] / (t['span'][0] * t['span'][1])) / t['need']
                  for t, c in zip(tiles, cont) if t['need']]
        tried.append((n, scale, cont, pos, min(ratios) if ratios else math.inf))
        if tried[-1][4] >= min_ratio or scale >= 1.0:     # at scale 1 a larger atlas adds nothing
            break
    n, scale, cont, pos, low = tried[-1]
    for t, c, p in zip(tiles, cont, pos):
        t['content'] = c
        t['origin'] = (p[0] + gutter, p[1] + gutter)             # content origin, texels
        t['ratio'] = (math.sqrt(c[0] * c[1] / (t['span'][0] * t['span'][1])) / t['need']) if t['need'] else None
    return dict(size=n, scale=scale, gutter=gutter, tiles=tiles, face_keys=face_keys, radius=radius, px=px,
                min_ratio=low, ratio_ok=low >= min_ratio, tried=[(x[0], x[1], x[4]) for x in tried],
                slots=tuple(slots))


def atlas_uv(t, n, u, v, su, sv):
    """Atlas UV (periods of the atlas, float) of an original UV under shift (su, sv) in tile t."""
    x = t['origin'][0] + (u - su - t['lo'][0]) * t['content'][0] / t['span'][0]
    y = t['origin'][1] + (v - sv - t['lo'][1]) * t['content'][1] / t['span'][1]
    return x / n, y / n


# --- record -------------------------------------------------------------------------------

def dominant(groups):
    faces = {}
    for g in groups:
        faces[g['material']] = faces.get(g['material'], 0) + len(g['faces'])
    return max(faces, key=lambda m: faces[m]) if faces else None


def _regen_extra(src_groups, out_faces, origin, ref_group):
    """One record per point index referenced by out_faces, in the order the faces first use
    them (the shipped convention: every shipped group lists its records in that order); each
    takes the vectors of its source point's record in the source group whose face first
    referenced it (else in any source group of this output group). Returns (records,
    referenced indices without a source record)."""
    recs = {gi: {e[0]: e for e in g.get('extra', ())} for gi, g in src_groups}
    out, missing = [], 0
    for j in dict.fromkeys(i for f in out_faces for i in f[:3]):
        o = origin[j]
        e = recs[ref_group[j]].get(o) or next((r[o] for r in recs.values() if o in r), None)
        if e is None:
            missing += 1
            continue
        out.append((j,) + tuple(e[1:]))
    return out, missing


def _greedy(faces, limit):
    chunks, cur, used = [], [], set()
    for f in faces:
        new = set(f[:3]) - used
        if cur and len(used) + len(new) > limit:
            chunks.append(cur)
            cur, used = [], set()
            new = set(f[:3])
        cur.append(f)
        used |= new
    return chunks + [cur] if cur else chunks


def split_faces(faces, limit, blocks=None):
    """Split a face list into chunks referencing at most `limit` distinct points each. With
    `blocks` (a block id per face, e.g. the source group), whole blocks are packed first-fit
    in decreasing size (a block larger than the limit is first split greedily in face order),
    so a source group stays in one chunk and few points are shared between chunks; face order
    is kept inside a chunk. Without blocks: greedy in face order, a new chunk opening when the
    next face would push past the limit."""
    if len({i for f in faces for i in f[:3]}) <= limit:
        return [list(faces)] if faces else []
    if blocks is None:
        return _greedy(faces, limit)
    by = {}
    for k, (f, b) in enumerate(zip(faces, blocks)):
        by.setdefault(b, []).append((k, f))
    pieces = []
    for items in by.values():
        if len({i for _, f in items for i in f[:3]}) <= limit:
            pieces.append(items)
        else:
            pos = {id(f): k for k, f in items}
            pieces += [[(pos[id(f)], f) for f in c] for c in _greedy([f for _, f in items], limit)]
    pieces.sort(key=lambda it: (-len({i for _, f in it for i in f[:3]}), it[0][0]))
    bins = []                                     # [items, point set]
    for it in pieces:
        pts = {i for _, f in it for i in f[:3]}
        for bn in bins:
            if len(bn[1] | pts) <= limit:
                bn[0].extend(it)
                bn[1] |= pts
                break
        else:
            bins.append([list(it), set(pts)])
    return [[f for _, f in sorted(bn[0], key=lambda x: x[0])] for bn in sorted(bins, key=lambda bn: min(bn[0])[0])]


def rewrite_record(record, layout, atlas_index, alpha, alpha_remap=None, max_group_points=None):
    """C with rewritten UVs, duplicated points and regrouped parts; returns (lod, info). An output
    group referencing more than max_group_points distinct points is split (split_faces: whole
    source groups packed first-fit) into groups of the same material; a point used by an earlier split group is
    duplicated (with its tangent record) so that the split groups share no point."""
    pts = record['points']
    new_pts, origin, owner, index, copies_of = list(pts), list(range(len(pts))), {}, {}, {}

    def idx(i, key):
        k = (i, key)
        if k not in index:
            if i not in owner:
                owner[i], index[k], copies_of[i] = key, i, [i]
            else:
                index[k] = len(new_pts)
                new_pts.append(pts[i]); origin.append(i); copies_of[i].append(index[k])
        return index[k]

    parts, faces_map, pending = [], [], []
    n = layout['size'] if layout else 0
    for pi, part in enumerate(record['parts']):
        new = {'flags': part['flags'], 'groups': []}
        groups = part['groups']
        pre = part['flags'] & bob1.PART_PRECOMPUTED
        if part['flags'] & HIDDEN_PART:                   # copied group by group, original UVs
            classes = [([(gi, g)], g['material'], False) for gi, g in enumerate(groups)]
        else:
            classes = [([(gi, g) for gi, g in enumerate(groups) if g['material'] not in alpha], atlas_index, True),
                       ([(gi, g) for gi, g in enumerate(groups) if g['material'] in alpha], None, False)]
        for cls, material, atlased in classes:
            if not cls:
                continue
            if material is None:
                d = dominant([g for _, g in cls])
                material = (alpha_remap or {}).get(d, d)
            faces, ref_group, blocks = [], {}, []
            for gi, g in cls:
                for fi, f in enumerate(g['faces']):
                    key = layout['face_keys'][pi, gi, fi] if atlased else None
                    out = (idx(f[0], key), idx(f[1], key), idx(f[2], key), f[3])
                    faces.append(out)
                    blocks.append(gi)
                    for j in out[:3]:
                        ref_group.setdefault(j, gi)
                    if key is not None:
                        faces_map.append((pi, f, out, g['material'], key[0]))
            pending.append((new, pi, cls, material, faces, ref_group, pre, blocks))
        if 'bounds' in part:
            new['bounds'] = list(part['bounds'])
        parts.append(new)
    for (i, key), j in index.items():
        if key is not None:
            t = layout['tiles'][key[0]]
            u, v = point_uv(pts[i])
            au, av = atlas_uv(t, n, u, v, key[1], key[2])
            new_pts[j] = with_uv(pts[i], int(round(au * UV_ONE)), int(round(av * UV_ONE)))
    missing, split = 0, []
    for new, pi, cls, material, faces, ref_group, pre, blocks in pending:
        chunks = split_faces(faces, max_group_points or MAX_GROUP_POINTS, blocks)
        if len(chunks) > 1:
            split.append(dict(part=pi, material=material, faces=len(faces),
                              points=[len({j for f in c for j in f[:3]}) for c in chunks]))
        seen = set()
        for k, chunk in enumerate(chunks):
            if k:                                         # points of earlier chunks: own copies
                dup = {}
                for j in dict.fromkeys(j for f in chunk for j in f[:3]):
                    if j in seen:
                        dup[j] = len(new_pts)
                        new_pts.append(new_pts[j]); origin.append(origin[j])
                        copies_of[origin[j]].append(dup[j])
                        ref_group[dup[j]] = ref_group[j]
                chunk = [tuple(dup.get(j, j) for j in f[:3]) + (f[3],) for f in chunk]
            seen |= {j for f in chunk for j in f[:3]}
            grp = {'material': material, 'faces': chunk}
            if pre:
                grp['extra'], miss = _regen_extra(cls, chunk, origin, ref_group)
                missing += miss
            new['groups'].append(grp)
    lod = {'value': None, 'flags': record['flags']}
    if 'bones' in record:
        lod['bones'] = list(record['bones'])
    lod['points'] = new_pts
    if 'weights' in record:
        lod['weights'] = [record['weights'][o] for o in origin]
    lod['parts'] = parts
    return lod, dict(origin=origin, duplicated=len(new_pts) - len(pts), faces=faces_map,
                     missing_records=missing, copies_of=copies_of, split=split)


def atlas_material(mats, dom, names, areas, synth=True):
    """Copy of mats[dom] with the atlas textures and (synth) area-weighted g_Mat* means; rows as
    lod_overlay.synth_materials."""
    base = mats[dom]
    replace = {b't_speculartexture': b'NULL', b't_bumptexture': b'NULL', b't_alphatexture': b'NULL'}
    replace.update({SLOT_NAMES[s]: v for s, v in names.items()})
    have = {n.lower() for n, t, _ in base['params'] if t == 8}
    for need in (b't_diffusetexture', b't_lightmaptexture'):
        if need not in have:
            raise AtlasError(f'dominant material {dom} has no {need.decode()} parameter')
    params, rows = [], []
    for name, typ, val in base['params']:
        low = name.lower()
        if typ == 8 and low in replace:
            val = replace[low]
        elif synth and typ == 2 and low.startswith(b'g_mat'):
            num = den = 0.0
            for mi, a in areas.items():
                v = [pv for pn, pt, pv in mats[mi].get('params', ()) if pt == 2 and pn.lower() == low]
                if v:
                    num += a * v[0][0]; den += a
            if den > 0:
                mean = num / den
                rows.append((name.decode('latin1'), val[0], mean, int(round(mean))))
                val = [int(round(mean))]
        params.append((name, typ, val))
    return dict(base, index=len(mats), params=params), rows


def texture_names(body, slots):
    stems = {s: f'x3m_lod_{body}_{s}' for s in slots}
    return ({s: b'x3m_lod\\' + st.encode() + b'.tga' for s, st in stems.items()},
            {s: f'dds/{st}.pck' for s, st in stems.items()})


def collapse(assets, body, mats, record, alpha, px, sizes=(1024, 2048), specular=False, synth=True,
             gutter=GUTTER, min_ratio=MIN_RATIO, textures=None, bump=True, force_uv2=False,
             force_mixed_effects=False, max_group_points=None):
    """Atlas collapse of `record`: appends the atlas material (then any synthesized alpha material)
    to `mats` in place. Returns dict(record, layout, atlas_index, names, members, synth, ...)."""
    import lod_overlay
    textures = textures or Textures(assets)
    areas, opaque = {}, []
    for part in record['parts']:
        if part['flags'] & HIDDEN_PART:
            continue
        for g in part['groups']:
            if g['material'] not in alpha:
                opaque.append(g)
                areas[g['material']] = areas.get(g['material'], 0.0) + sum(
                    body_materials.face_area(record['points'], f) for f in g['faces'])
    if not opaque:
        raise AtlasError('the record has no opaque faces to atlas')
    uv2 = sum(1 for p in record['points'] if p[0] & 4)
    if uv2 and not force_uv2:
        raise AtlasError(f'{uv2} points carry a second UV set (point flag 0x04); only the first UV pair is'
                         ' rewritten into the atlas, so anything sampling the second set would be wrong;'
                         ' --force-uv2 overrides')
    effects = sorted({mats[m].get('effect', b'').decode('latin1').lower() for m in areas})
    if len(effects) > 1 and not force_mixed_effects:
        raise AtlasError(f'the opaque materials use {len(effects)} effect files {effects}; one atlas material'
                         ' draws them all with one effect; --force-mixed-effects overrides')
    dom = dominant(opaque)
    has_bump = any(t == 8 and n.lower() == SLOT_NAMES['bump'] for n, t, _ in mats[dom].get('params', ()))
    slots = ('diffuse', 'light') + (('bump',) if bump and has_bump else ()) + (('specular',) if specular else ())
    layout = plan_layout(record, mats, alpha, textures, px, sizes, gutter, slots, min_ratio)
    names, members = texture_names(body, slots)
    mat, rows = atlas_material(mats, dom, names, areas, synth)
    atlas_index = len(mats)
    mats.append(mat)
    report = [dict(index=atlas_index, dominant=dom, absorbed=sorted(areas), params=rows, atlas=True)]
    remap = {}
    if synth:
        alpha_only = {'points': record['points'], 'parts': [
            {'flags': p['flags'], 'groups': [g for g in p['groups'] if g['material'] in alpha]}
            for p in record['parts'] if not p['flags'] & HIDDEN_PART]}
        remap, more = lod_overlay.synth_materials(mats, alpha_only, alpha, 'two', frozenset())
        report += more
    lod, info = rewrite_record(record, layout, atlas_index, alpha, remap, max_group_points)
    effects = sorted({(mats[m].get('effect', b'').decode('latin1'), mats[m].get('technique')) for m in areas})
    return dict(record=lod, layout=layout, atlas_index=atlas_index, dominant=dom, names=names, members=members,
                slots=slots, synth=report, info=info, effects=effects, textures=textures)


# --- baking -------------------------------------------------------------------------------

def level_weights(k0, k1, scale, origin, content, lo, span, src_n):
    """(k1 - k0, src_n) area-resampling matrix for one axis of a tile at one mip level: level
    texel k covers level-0 atlas texels [k * scale, (k + 1) * scale), which map to the source
    periods lo + (x - origin) * span / content of a repeating source (origin / content: the
    tile's content origin and size in level-0 texels)."""
    m = np.zeros((k1 - k0, src_n), np.float32)
    step = scale * span / content * src_n
    for r, k in enumerate(range(k0, k1)):
        a = (lo + (k * scale - origin) * span / content) * src_n
        b = a + step
        j = math.floor(a)
        while j < b:
            cov = min(b, j + 1) - max(a, j)
            if cov > 0:
                m[r, j % src_n] += cov
            j += 1
        m[r] /= step
    return m


def axis_weights(n_out, gutter, lo, span, src_n):
    """(n_out + 2 gutter, src_n) area-resampling matrix: target texel k covers
    [lo + (k - gutter) * span / n_out, + span / n_out) periods of a repeating source."""
    return level_weights(-gutter, n_out + gutter, 1, 0, n_out, lo, span, src_n)


def tile_boxes(t, gutter, level):
    """Level-`level` texel boxes (x0, x1, y0, y1) of a tile: its footprint (content and gutter,
    rounded outwards) and the texels that overlap its content."""
    s = 1 << level
    (cx, cy), (cw, ch) = t['origin'], t['content']
    return ((cx - gutter) // s, -(-(cx + cw + gutter) // s), (cy - gutter) // s, -(-(cy + ch + gutter) // s)), \
           (cx // s, -(-(cx + cw) // s), cy // s, -(-(cy + ch) // s))


def bake_level(layout, sources, slot, level=0):
    """One mip level of a slot's atlas, tile-aware: every tile's footprint is area-resampled
    from its own repeating source at this level's density (so the gutter is the tile's own
    repeat continuation at every level, never a neighbour's texels), the texels overlapping a
    tile's content are written last (content beats a neighbour's gutter), and the unused area
    keeps the background (diffuse black opaque, light/specular black alpha 0, bump flat).
    Float32 (n >> level, n >> level, 4) RGBA; bump: (.., 3) unit vectors. `sources`: per
    tile index the float32 source (bump: vectors) or None (NULL)."""
    n, g = layout['size'], layout['gutter']
    nl, s = max(1, n >> level), 1 << level
    bump = slot == 'bump'
    img = np.zeros((nl, nl, 3 if bump else 4), np.float32)
    img[:, :, -1] = 1 if bump else 255 if slot == 'diffuse' else 0     # flat normal / opaque / black
    null = np.array((0, 0, 1) if bump else NULL_TEXEL, np.float32)
    inner = []
    for t, src in zip(layout['tiles'], sources):
        (x0, x1, y0, y1), over = tile_boxes(t, g, level)
        (cx, cy), (cw, ch) = t['origin'], t['content']
        if src is None:
            block = np.broadcast_to(null, (y1 - y0, x1 - x0, len(null)))
        else:
            H, W = src.shape[:2]
            my = level_weights(y0, y1, s, cy, ch, t['lo'][1], t['span'][1], H)
            mx = level_weights(x0, x1, s, cx, cw, t['lo'][0], t['span'][0], W)
            tmp = np.tensordot(my, src, axes=(1, 0))                                   # (ty, W, c)
            block = np.tensordot(tmp, mx, axes=(1, 1)).transpose(0, 2, 1)             # (ty, tx, c)
            if bump:
                block = normalize(block)
        img[y0:y1, x0:x1] = block
        inner.append((over, (x0, y0), block))
    for (ox0, ox1, oy0, oy1), (x0, y0), block in inner:
        img[oy0:oy1, ox0:ox1] = block[oy0 - y0:oy1 - y0, ox0 - x0:ox1 - x0]
    return img


def tile_sources(layout, textures, slot):
    out = []
    for t in layout['tiles']:
        src = textures.get(t['names'][slot])
        out.append(None if src is None else (to_normals(src) if slot == 'bump' else src).astype(np.float32))
    return out


def bake(layout, textures, levels=None):
    """{slot: [float32 level images, level 0 first]} (see bake_level); `levels` defaults to
    the full chain down to 1x1."""
    count = layout['size'].bit_length() if levels is None else levels
    out = {}
    for slot in layout['slots']:
        sources = tile_sources(layout, textures, slot)
        out[slot] = [bake_level(layout, sources, slot, lv) for lv in range(count)]
    return out


def to_levels(slot, images):
    """Float level images -> rounded uint8 RGBA levels (bump: unit vectors -> swizzled)."""
    if slot == 'bump':
        return [np.clip(np.rint(from_normals(normalize(v.astype(np.float64)))), 0, 255).astype(np.uint8)
                for v in images]
    return [np.clip(np.rint(v.astype(np.float64)), 0, 255).astype(np.uint8) for v in images]


def used_mask(layout):
    n, g = layout['size'], layout['gutter']
    m = np.zeros((n, n), bool)
    for t in layout['tiles']:
        (cx, cy), (cw, ch) = t['origin'], t['content']
        m[cy - g:cy + ch + g, cx - g:cx + cw + g] = True
    return m


def encode(images, layout, fmt='dxt'):
    """{slot: dict(dds, format, bytes, sha256, error=[(mean, p95) per R,G,B,A] over used texels)}."""
    if fmt not in FORMATS:
        raise AtlasError(f'unknown atlas format {fmt!r}')
    mask = used_mask(layout)
    out = {}
    for slot, images_ in images.items():
        levels = to_levels(slot, images_)
        if fmt == 'a8r8g8b8':
            f = 'A8R8G8B8'
        else:
            f = 'DXT1' if (levels[0][:, :, 3] == 255).all() else 'DXT5'
        dds = write_dds(levels, f)
        back = decode_dds(dds)
        diff = np.abs(back.astype(np.int16) - levels[0].astype(np.int16))[mask]
        err = [(float(diff[:, c].mean()), float(np.percentile(diff[:, c], 95))) for c in range(4)]
        out[slot] = dict(dds=dds, format=f, bytes=len(dds), sha256=hashlib.sha256(dds).hexdigest(), error=err,
                         decoded=back)
    return out


def stored(dds):
    return gzip.compress(dds, mtime=0)


# --- checks -------------------------------------------------------------------------------

def bilinear(img, u, v, wrap):
    """RGBA float samples of img at UV arrays (texel centres at (i + 0.5) / size)."""
    h, w = img.shape[:2]
    x, y = u * w - 0.5, v * h - 0.5
    x0, y0 = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    fx, fy = (x - x0)[:, None], (y - y0)[:, None]
    if wrap:
        xs, ys = (x0 % w, (x0 + 1) % w), (y0 % h, (y0 + 1) % h)
    else:
        xs = (np.clip(x0, 0, w - 1), np.clip(x0 + 1, 0, w - 1))
        ys = (np.clip(y0, 0, h - 1), np.clip(y0 + 1, 0, h - 1))
    f = img.astype(np.float64)
    top = f[ys[0], xs[0]] * (1 - fx) + f[ys[0], xs[1]] * fx
    bot = f[ys[1], xs[0]] * (1 - fx) + f[ys[1], xs[1]] * fx
    return top * (1 - fy) + bot * fy


def _box(img, u0, v0, du, dv):
    """Area average of a repeating img over [u0, u0 + du) x [v0, v0 + dv) (periods)."""
    h, w = img.shape[:2]

    def axis(lo, span, n):
        a, b = lo * n, (lo + span) * n
        js, ws = [], []
        j = math.floor(a)
        while j < b:
            c = min(b, j + 1) - max(a, j)
            if c > 0:
                js.append(j % n); ws.append(c)
            j += 1
        return np.array(js), np.array(ws) / (b - a)
    ys, wy = axis(v0, dv, h)
    xs, wx = axis(u0, du, w)
    return np.einsum('y,x,yxc->c', wy, wx, img[np.ix_(ys, xs)].astype(np.float64))


def _stats(errs):
    e = np.asarray(errs, np.float64)
    return (float(e.mean()), float(np.percentile(e, 95)), float(e.max())) if len(e) else (0.0, 0.0, 0.0)


def check(source_record, out_record, result, atlases, mats):
    """(b) per slot: atlas bilinear at the rewritten face centroid vs the source bilinear (wrap)
    at the original centroid (mip 0), and vs the source box-filtered over one atlas texel
    (a reference without the downsampling loss); mean / p95 / max over faces of the mean
    |RGB| error and of the |A| error (0..255).
    (c) every rewritten vertex UV inside its tile content (and gutter), and the inverse-mapped
    centroid equal to the original one modulo the integer shift (max error in source texels)."""
    layout, textures = result['layout'], result['textures']
    n, g = layout['size'], layout['gutter']
    spts, opts = source_record['points'], out_record['points']
    faces = result['info']['faces']
    inside = in_gutter = 0
    worst_map = 0.0
    su_list = []
    for pi, sf, of, mi, ti in faces:
        t = layout['tiles'][ti]
        (cx, cy), (cw, ch) = t['origin'], t['content']
        for j in of[:3]:
            u, v = point_uv(opts[j])
            x, y = u * n, v * n
            tol = 2.0 * n / UV_ONE
            inside += cx - tol <= x <= cx + cw + tol and cy - tol <= y <= cy + ch + tol
            in_gutter += cx - g <= x <= cx + cw + g and cy - g <= y <= cy + ch + g
        ou = np.mean([point_uv(spts[i]) for i in sf[:3]], 0)
        nu = np.mean([point_uv(opts[j]) for j in of[:3]], 0)
        su, sv = face_shift([point_uv(spts[i]) for i in sf[:3]])
        inv_u = t['lo'][0] + (nu[0] * n - cx) * t['span'][0] / cw
        inv_v = t['lo'][1] + (nu[1] * n - cy) * t['span'][1] / ch
        worst_map = max(worst_map, abs(inv_u - (ou[0] - su)) * t['base'][0], abs(inv_v - (ou[1] - sv)) * t['base'][1])
        su_list.append((ou, nu, mi, t))
    out = dict(faces=len(faces), vertices=3 * len(faces), inside=inside, in_gutter=in_gutter,
               max_map_error_texels=worst_map, slots={})
    angle = lambda x, y: np.degrees(np.arccos(np.clip((normalize(to_normals(x)) * normalize(to_normals(y))).sum(-1),
                                                       -1, 1)))
    for slot, atlas in atlases.items():
        bump = slot == 'bump'
        keys = ('src_rgb', 'src_a', 'box_rgb', 'box_a') + (('src_angle', 'box_angle') if bump else ())
        errs = {k: [] for k in keys}
        by_tex = {}
        for k, (ou, nu, mi, t) in enumerate(su_list):
            name = material_slots(mats[mi], (slot,)).get(slot)
            by_tex.setdefault(None if name is None or body_materials.is_null(name) else name.lower(), []).append(k)
        for name, ks in by_tex.items():
            src = textures.get(name)
            nu = np.array([su_list[k][1] for k in ks])
            ou = np.array([su_list[k][0] for k in ks])
            a = bilinear(atlas, nu[:, 0], nu[:, 1], False)
            if src is None:
                null = from_normals(np.array([0.0, 0.0, 1.0])) if bump else np.array(NULL_TEXEL)
                ref = box = np.broadcast_to(null, a.shape)
            else:
                ref = bilinear(src, ou[:, 0], ou[:, 1], True)
                vec = to_normals(src) if bump else src     # bump: box-average vectors, then renormalise
                box = []
                for k, (u, v) in zip(ks, ou):
                    t = su_list[k][3]
                    du, dv = t['span'][0] / t['content'][0], t['span'][1] / t['content'][1]
                    box.append(_box(vec, u - du / 2, v - dv / 2, du, dv))
                box = from_normals(normalize(np.array(box))) if bump else np.array(box)
            errs['src_rgb'] += list(np.abs(a - ref)[:, :3].mean(1))
            errs['src_a'] += list(np.abs(a - ref)[:, 3])
            errs['box_rgb'] += list(np.abs(a - box)[:, :3].mean(1))
            errs['box_a'] += list(np.abs(a - box)[:, 3])
            if bump:
                errs['src_angle'] += list(angle(a, ref))
                errs['box_angle'] += list(angle(a, box))
        out['slots'][slot] = {k: _stats(v) for k, v in errs.items()}
    return out


# --- preview ------------------------------------------------------------------------------

def write_png(path, rgb):
    h, w = rgb.shape[:2]
    raw = b''.join(b'\x00' + rgb[y].tobytes() for y in range(h))

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def preview(dds, layout, path, limit=512):
    """PNG of the first mip with side <= limit, tile content outlined in magenta."""
    _, _, mips, _ = dds_format(dds)
    level = 0
    while (layout['size'] >> level) > limit and level + 1 < mips:
        level += 1
    img = decode_dds(dds, level)[:, :, :3].copy()
    s = 1 << level
    h, w = img.shape[:2]
    for t in layout['tiles']:
        (cx, cy), (cw, ch) = t['origin'], t['content']
        x0, y0 = min(w - 1, cx // s), min(h - 1, cy // s)
        x1, y1 = min(w - 1, max(x0, (cx + cw) // s - 1)), min(h - 1, max(y0, (cy + ch) // s - 1))
        img[y0, x0:x1 + 1] = img[y1, x0:x1 + 1] = (255, 0, 255)
        img[y0:y1 + 1, x0] = img[y0:y1 + 1, x1] = (255, 0, 255)
    write_png(path, img)
    return level


# --- lod_overlay entry --------------------------------------------------------------------

def build(assets, body, mats, record, alpha, px, sizes=(1024, 2048), fmt='dxt', specular=False, synth=True,
          bump=True, force_uv2=False, force_mixed_effects=False, max_group_points=None):
    """collapse + bake + encode + check; the result carries the encoded DDS per slot and the checks."""
    res = collapse(assets, body, mats, record, alpha, px, sizes, specular, synth, bump=bump, force_uv2=force_uv2,
                   force_mixed_effects=force_mixed_effects, max_group_points=max_group_points)
    images = bake(res['layout'], res['textures'])
    res['encoded'] = encode(images, res['layout'], fmt)
    res['check'] = check(record, res['record'], res, {s: e['decoded'] for s, e in res['encoded'].items()}, mats)
    return res


def _num(x):
    return None if x is None else round(float(x), 6)


def summary(res):
    """JSON-ready description of an atlas build (manifest)."""
    L, c = res['layout'], res['check']
    return dict(
        material=res['atlas_index'], dominant=res['dominant'], size=L['size'], scale=_num(L['scale']),
        gutter=L['gutter'], px=L['px'], min_texels_per_px=_num(L['min_ratio']), ratio_ok=bool(L['ratio_ok']),
        tried=[dict(size=n, scale=_num(s), min_texels_per_px=_num(m)) for n, s, m in L['tried']],
        duplicated_points=res['info']['duplicated'], points=len(res['record']['points']),
        split_groups=res['info']['split'],
        missing_tangent_records=res['info']['missing_records'], effects=[list(e) for e in res['effects']],
        tiles=[dict(mats=t['mats'], names={k: (v.decode('latin1') if v else None) for k, v in t['names'].items()},
                    lo=[_num(x) for x in t['lo']], span=[_num(x) for x in t['span']], base=list(t['base']),
                    content=list(t['content']), origin=list(t['origin']), texels_per_px=_num(t['ratio']))
               for t in L['tiles']],
        textures=[dict(slot=s, name=res['names'][s].decode('latin1'), member=res['members'][s], format=e['format'],
                       dds_bytes=e['bytes'], dds_sha256=e['sha256'],
                       compression_error_rgba=[[_num(m), _num(p)] for m, p in e['error']])
                  for s, e in res['encoded'].items()],
        check=dict(faces=c['faces'], vertices=c['vertices'], uv_inside_content=c['inside'],
                   uv_inside_gutter=c['in_gutter'], max_map_error_texels=_num(c['max_map_error_texels']),
                   slots={s: {k: [_num(x) for x in v] for k, v in d.items()} for s, d in c['slots'].items()}))


def format_summary(s):
    """Report lines for lod_overlay.describe."""
    tried = ', '.join(f'{t["size"]}: {t["min_texels_per_px"]:.3f}' for t in s['tried'])
    lines = [f'atlas {s["size"]}x{s["size"]} (min atlas texels per screen pixel at px={s["px"]:g}: {tried}; '
             f'>= 2: {"yes" if s["ratio_ok"] else "NO"}) scale {s["scale"]:.4f} tiles {len(s["tiles"])} gutter '
             f'{s["gutter"]} duplicated points {s["duplicated_points"]} -> {s["points"]} points, tangent records'
             f' missing {s["missing_tangent_records"]}; material mat{s["material"]} = copy of mat{s["dominant"]};'
             f' effects {s["effects"]}']
    for sp in s.get('split_groups', ()):
        lines.append(f'atlas split: part {sp["part"]} mat{sp["material"]} {sp["faces"]} faces -> {len(sp["points"])}'
                     f' groups of {sp["points"]} points (<= MAX_GROUP_POINTS each; shared points duplicated)')
    for t in s['textures']:
        err = ' '.join(f'{c}={m:.2f}/{p:.0f}' for c, (m, p) in zip('RGBA', t['compression_error_rgba']))
        lines.append(f'atlas {t["slot"]}: {t["member"]} ({t["name"]}) {t["format"]} {t["dds_bytes"]} bytes'
                     f' sha256 {t["dds_sha256"][:16]}; compression error mean/p95 {err}')
    c = s['check']
    lines.append(f'atlas check: faces {c["faces"]}, vertex UVs inside their tile {c["uv_inside_content"]}/'
                 f'{c["vertices"]} (inside tile+gutter {c["uv_inside_gutter"]}), max inverse-map error'
                 f' {c["max_map_error_texels"]:.3f} source texels')
    for slot, d in c['slots'].items():
        lines.append(f'atlas sampling {slot} (per-face centroid, mean/p95/max of |error| 0..255): vs source mip 0'
                     f' RGB {"/".join(f"{x:.2f}" for x in d["src_rgb"])} A {"/".join(f"{x:.2f}" for x in d["src_a"])};'
                     f' vs source prefiltered to the atlas texel RGB {"/".join(f"{x:.2f}" for x in d["box_rgb"])}'
                     f' A {"/".join(f"{x:.2f}" for x in d["box_a"])}'
                     + (f'; normal angle (degrees) vs source {"/".join(f"{x:.2f}" for x in d["src_angle"])},'
                        f' vs prefiltered {"/".join(f"{x:.2f}" for x in d["box_angle"])}' if 'src_angle' in d else ''))
    return lines
