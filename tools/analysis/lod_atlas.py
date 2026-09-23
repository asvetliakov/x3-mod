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
- Clamped layout: when one scale reaches neither MIN_RATIO nor scale 1 at a size (a tile whose span
  is tens of thousands of periods, from a few faces with saturated 16.16 UVs, or a long repeat,
  squeezes the uniform scale towards zero), the same size is tried with (a) every face whose own UV
  extent exceeds OUTLIER_SPAN (256) periods span-clamped: left out of its tile's span and need, its
  UVs clamped into the tile (face key (tile, su, sv, 1)); (b) every tile's scale capped at
  MIN_RATIO / k (k = its texels per pixel at scale 1). It is taken when it reaches MIN_RATIO or
  scale 1, and at the largest size. The sizes are tried in order, uniform then clamped per size, so
  a body whose uniform layout reaches MIN_RATIO only at a larger size builds clamped at the smaller
  one (deliberate: a 1024 atlas at 2 texels/px is the fleet budget, 2048 is four times the bytes);
  only bodies whose uniform layout passes at the first size are unchanged. Over the 339 flown-sector
  bodies: 55 went 2048 uniform -> 1024 clamped, 71 with a uniform ratio 0.5..2 and 27 below 0.5
  moved to the clamped layout (verification/results/lod-overlay-batch/texel_share_compare_out.txt).
- Texel rule (texel_floor over tile_rows): each tile's share is its faces' mesh-space area over the
  atlased surface (measured; the screen-area share is inferred proportional). Tiles below
  --min-texels plus the span-clamped faces (ratio 0) are starved; lod_overlay refuses texel_floor
  only when they cover more than --texel-floor-share; the rest are reported as texel_clamped.
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
- Light bleed guard (build -> guard_bleed; Run 74 A, run277: argon_tech_S_laser_E's 20x20 solar-panel
  light tile 16 texels from the 44x12 exhaust tile came out 1.67x brighter and orange). The proxy widens
  the hull light-map fetch by K = 4 (tools/manage.py HULL_EMISSIVE_WIDENING_DEFAULT; the texldd raises the
  level by log2 K = WIDEN_LEVELS = 2, docs/architecture/hull-emissive-widening.md), so at the switch size a
  tile is read at L = ceil(log2(its atlas texels per pixel)) + 2, where the 8-texel gutter is below one
  texel and bilinear plus the widened footprint reach the neighbour. After the layout, light_bleed samples
  every tile's faces (7 barycentric points each) in the tile-aware light atlas level L with a +-1 texel
  box and against the same texels resampled from the tile's own light map alone; a tile whose
  face-area-weighted mean added Rec.709 luminance exceeds LIGHT_BLEED_MAX (4/255, absolute: the dark tiles
  at risk hold 0-1/255 of their own light, so a relative rule would flag DXT noise; laser_E's panel took
  +21.8) is over the bleed limit. It counts (flagged) only when its mesh-space face-area share of the
  atlased surface (the plan's, stable across repack and prune) is at least LIGHT_BLEED_SHARE (2 %,
  --light-bleed-share); a smaller one is reported as light_bleed_ignored with its share and texel size and
  takes no remedy: over the 22 Run 74 bodies the ships' flagged tiles sat mostly at L5-L8, i.e. tiles a few
  pixels wide at the switch size, whose tint is a few pixels while their kept groups cost 9 extra draws
  per set of ship instances, and no tint was seen in flight; laser_E's panel tile (L4) is well above the
  share. Remedies for the counted tiles, in order: (1) repack_layout, the emitter tiles (own mean light
  above EMITTER_LUMA 16/255, not flagged) packed first in their own shelves with an empty margin of
  2^(L+1) texels (L = the highest flagged level; halved until the pack keeps the minimum texel ratio >=
  min(MIN_RATIO, the plan's own) and the atlas scale within LIGHT_BLEED_SCALE_LOSS (15 %, --light-bleed-
  scale-loss) of the plan's; the plan's scale is the largest that packs, so the regions need a lower
  uniform scale at the same atlas size), taken when it leaves fewer tiles flagged; (2) every tile still
  flagged keeps its materials as their own groups (their own material, textures and UVs; one extra draw per
  part and material, after the atlas groups, like the glow collapse) at the current pack (prune_layout: the
  other tiles keep their places), up to LIGHT_BLEED_ROUNDS times; a tile still flagged then is accepted
  and reported as residual. The
  summary carries light_bleed (flagged = counted, ignored, remedy, repack, kept, residual, after) and
  kept_light_bleed.
  light_bleed_max 0 (lod_overlay --light-bleed-max 0) turns the guard off.
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
- Mixed effects: the opaque materials are grouped by effect file (.fx, case-insensitive);
  every effect gets its own merged material and one output group per part, all sharing
  the one atlas set (same tiles, same UV rewrite), so a body draws one atlas group per
  effect (238 vanilla bodies mix effects; lod-overlay-batch/summary.txt).
- Second UV set (point flag 0x04): the fleet's second set is the per-body occlusion decal
  unwrap (t_OcclusionTexture in [0,1], one texture per body, XT_standard_lighting.fx;
  lod-overlay-batch/classes/classes_out.txt). Only the first pair is rewritten; the second
  pair is copied through unchanged (with_uv keeps every other field), and the merged
  material keeps the dominant material's t_OcclusionTexture and the g_Mat* mean covers
  g_MatOcclStr. The body is refused when the opaque materials of one merged group carry
  more than one occlusion texture (NULL / NONE_* / absent count as "none"; none mixed with
  a decal refuses too), since one material cannot sample them all.
- Material: per effect a copy of that effect's dominant opaque material (face count over
  all parts) with t_DiffuseTexture / t_LightMapTexture = the atlases, t_SpecularTexture =
  the specular atlas (--atlas-specular) or NULL (as 120 of 7198 shipped argon.fx
  materials), t_BumpTexture = the bump atlas (default; NULL with bump off),
  t_AlphaTexture NULL (the specular atlas is baked only when some atlased material carries
  t_SpecularTexture), every g_Mat* FLOAT the face-area-weighted mean over the atlased
  materials of that effect (unless synth is off), appended to MAT6 with record index =
  position. A group material index outside the table (negative on the ad signs) is
  refused before any of this.
- Textures: member dds/x3m_lod_<body>_<slot>.pck (gzip DDS, as every shipped texture)
  named in the material as x3m_lod\\x3m_lod_<body>_<slot>.tga: the engine resolves a
  material texture name by its stem under dds\\ (shipped names carry a directory and
  .tga while the members are dds/<stem>.pck; the texture loader 0x004dc540 takes the
  extension list "pck dds", loading-orchestration.md). <body> is the caller's qualified
  stem (lod_overlay.qualified_stem: file stem + a 6-hex hash of the member path, so
  colliding stems such as ships/terran/terran_M3 and ships/usc/terran_m3 get distinct,
  stable names).
- Source textures are resolved as the engine's loader does: dds/<stem>.pck|.dds (path
  table +0x7c "dds\\%s", extension list "pck dds"), then tex/<stem>.jpg|.tga|.bmp (the
  jpg/tga wrapper 0x004f3510, +0x74 "tex\\%s"); the dds-before-tex order is inferred from the
  path table and the wrapper's role, not traced (no enumerated mod body reads a tex/ member,
  so no body depends on it); jpg/tga/bmp are decoded with Pillow
  (imported lazily; a missing Pillow refuses the body with a reason). The layout records
  the member each tile's textures came from (tile 'sources').
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
HULL_WIDENING_K = 4        # the proxy's light-map widening factor: tools/manage.py HULL_EMISSIVE_WIDENING_DEFAULT
                           # ('4', the launcher default wherever the light-map gain is active); its texldd scales the
                           # gradients by k = clamp(K x light-map texels per pixel, 1, K), so at >= 1 texel/px the fetch
                           # rises log2 K levels (docs/architecture/hull-emissive-widening.md section 0)
WIDEN_LEVELS = 2           # log2(HULL_WIDENING_K)
LIGHT_BLEED_MAX = 4.0      # light_bleed: mean added Rec.709 luminance (0..255) a tile may take from its neighbours
EMITTER_LUMA = 16.0        # light_bleed repack: a tile whose own mean light luminance (0..255) exceeds this is an emitter
LIGHT_BLEED_ROUNDS = 3     # light_bleed: keep rounds
LIGHT_BLEED_SCALE_LOSS = 0.15   # light_bleed repack: the largest relative drop of the atlas scale a repack may cost
LIGHT_BLEED_SHARE = 0.02   # light_bleed: a tile over the limit counts for the remedy only from this atlased-surface share
LUMA_709 = np.array((0.2126, 0.7152, 0.0722))
BARY7 = np.array([(1 / 3, 1 / 3, 1 / 3), (.6, .2, .2), (.2, .6, .2), (.2, .2, .6), (.8, .1, .1), (.1, .8, .1),
                  (.1, .1, .8)])   # light_bleed sample points per face (barycentric)
UV_ONE = 65536.0
MIN_RATIO = 2.0
OUTLIER_SPAN = 256.0       # clamped layout: a face repeating its texture over more periods than this is span-clamped
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

DDS_LOOKUP = ('dds', ('.pck', '.dds'))            # 0x004dc540 "pck dds" under +0x7c "dds\%s"
IMAGE_LOOKUP = ('tex', ('.jpg', '.tga', '.bmp'))  # 0x004f3510 jpg/tga wrapper under +0x74 "tex\%s"


def texture_source(assets, name):
    """(decoded member bytes, kind 'dds' | 'image', info) of a material texture name resolved as
    the engine's loader does (module notes): dds/<stem>.pck|.dds, then tex/<stem>.jpg|.tga|.bmp.
    info = dict(source, member, decoded_sha256) from Assets.get; None for NULL."""
    if name is None or body_materials.is_null(name):
        return None
    stem = PurePosixPath(name.decode('latin1').replace('\\', '/')).stem
    try:
        data, info = assets.logical(f'{DDS_LOOKUP[0]}/{stem}', DDS_LOOKUP[1])
        if data is None:
            data, info = assets.logical(f'{IMAGE_LOOKUP[0]}/{stem}', IMAGE_LOOKUP[1])
    except ValueError as exc:
        raise AtlasError(f'texture {name!r}: {exc}') from None
    if data is None:
        raise AtlasError(f'texture {name!r} does not resolve under dds/ (pck, dds) or tex/ (jpg, tga, bmp)')
    if data[:4] == b'DDS ':
        return data, 'dds', info
    if info['member'].lower().startswith(DDS_LOOKUP[0] + '/'):
        raise AtlasError(f'texture {name!r} is not a DDS file ({info["member"]})')
    return data, 'image', info


def texture_bytes(assets, name):
    """Decoded DDS bytes of a material texture name, None for NULL; a jpg/tga/bmp source is refused."""
    src = texture_source(assets, name)
    if src is None:
        return None
    data, kind, info = src
    if kind != 'dds':
        raise AtlasError(f'texture {name!r} is not a DDS file ({info["member"]})')
    return data


def _pil_image(data, name):
    import io
    try:
        from PIL import Image
    except ImportError:
        raise AtlasError(f'texture {name!r} is a jpg/tga/bmp member and Pillow (PIL) is not installed') from None
    try:
        return Image.open(io.BytesIO(data))
    except Exception as exc:
        raise AtlasError(f'texture {name!r}: cannot decode image ({exc})') from None


def decode_image(data, name=b'?'):
    """RGBA uint8 array (h, w, 4) of a jpg/tga/bmp member (Pillow, imported lazily)."""
    with _pil_image(data, name) as im:
        return np.asarray(im.convert('RGBA'), np.uint8)


def image_size(data, name=b'?'):
    with _pil_image(data, name) as im:
        return im.size


class Textures:
    """Decoded mip 0 of material textures by name (lower-case), None for NULL; sizes and the
    resolved member per name are cached too (plan_layout asks per tile per body)."""
    def __init__(self, assets):
        self.assets, self.cache, self.sizes, self.sources = assets, {}, {}, {}

    @staticmethod
    def key(name):
        return None if name is None or body_materials.is_null(name) else name.lower()

    def _resolve(self, name):
        """(data, kind, info) or None, with the member recorded under self.sources."""
        key = self.key(name)
        src = texture_source(self.assets, name)
        if key is not None and src is not None:
            self.sources[key] = dict(member=f'{src[2]["source"]}:{src[2]["member"]}', kind=src[1],
                                     decoded_sha256=src[2]['decoded_sha256'])
        return src

    def get(self, name):
        key = self.key(name)
        if key not in self.cache:
            src = self._resolve(name)
            if src is None:
                self.cache[key] = None
            else:
                data, kind, _ = src
                self.cache[key] = decode_dds(data) if kind == 'dds' else decode_image(data, name)
        return self.cache[key]

    def size(self, name):
        """(w, h) of a real (not NULL, not NONE_*) texture, else None."""
        if name is None or body_materials.is_null(name) or body_materials.is_stock(name):
            return None
        key = self.key(name)
        if key not in self.sizes:
            try:
                data, kind, _ = self._resolve(name)
                self.sizes[key] = dds_format(data)[:2] if kind == 'dds' else tuple(image_size(data, name))
            except AtlasError as exc:
                self.sizes[key] = exc
        if isinstance(self.sizes[key], Exception):
            raise self.sizes[key]
        return self.sizes[key]

    def source(self, name):
        """dict(member, kind, decoded_sha256) of a resolved texture name, None for NULL / unresolved."""
        key = self.key(name)
        if key is None:
            return None
        if key not in self.sources:
            try:
                self._resolve(name)
            except AtlasError:
                return None
        return self.sources.get(key)


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


def shelf_pack(rects, n, first=()):
    """Shelf packing, tallest first (ties: wider, then index), into n x n: [(x, y)] or None. With `first`
    (indices), those rects are packed first and the rest start on a new shelf (two regions)."""
    order = sorted(range(len(rects)), key=lambda i: (i not in first, -rects[i][1], -rects[i][0], i))
    pos, x, y, shelf = [None] * len(rects), 0, 0, 0
    for k, i in enumerate(order):
        w, h = rects[i]
        if w > n or h > n:
            return None
        if x + w > n or (k and first and order[k - 1] in first and i not in first):
            y, x, shelf = y + shelf, 0, 0
        if y + h > n:
            return None
        pos[i] = (x, y)
        x, shelf = x + w, max(shelf, h)
    return pos


def fit(tiles, n, gutter):
    """Largest uniform scale <= 1 whose tiles shelf-pack into n x n: (scale, contents, positions). A tile
    with a 'cap' (clamped layout) never takes more than cap x its full size."""
    def attempt(s):
        cont = [(_side(t['full'][0], min(s, t.get('cap', 1.0))), _side(t['full'][1], min(s, t.get('cap', 1.0))))
                for t in tiles]
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


def _tile_ratio(t, c):
    """Atlas texels per screen pixel of tile t with content c at the switch size (None without a need)."""
    return math.sqrt(c[0] * c[1] / (t['span'][0] * t['span'][1])) / t['need'] if t['need'] else None


def _tile_stats(base, limit, px, radius, min_ratio):
    """Copy of tile `base` with lo, hi, span, full and need over its faces. limit None: every face (the
    uniform layout). limit L (the clamped layout): a face whose own UV extent (after its integer shift)
    exceeds L periods on either axis is span-clamped: it is left out of lo/hi and the need, and its UVs
    are clamped into the tile at the rewrite; the tile's scale is capped at min_ratio / k (k = its atlas
    texels per screen pixel at scale 1) so a tile never holds more than min_ratio texels per pixel."""
    t = {k: v for k, v in base.items() if k != 'face_list'}
    lo, hi = [math.inf, math.inf], [-math.inf, -math.inf]
    need_area = uv_area = clamped_area = 0.0
    clamped = 0
    for _, ranges, area, uva, ext in base['face_list']:
        if limit is not None and ext > limit:
            clamped += 1
            clamped_area += area
            continue
        for k, (lo_, hi_) in enumerate(ranges):
            lo[k], hi[k] = min(lo[k], lo_), max(hi[k], hi_)
        need_area += area
        uv_area += uva
    if clamped == len(base['face_list']):                # no face left (or none at all): one period
        lo, hi = [0.0, 0.0], [1.0, 1.0]
    span = []
    for k in range(2):
        w = hi[k] - lo[k]
        if w < 1.0 / t['base'][k]:                       # at least one source texel, centred
            mid = (hi[k] + lo[k]) / 2
            w = 1.0 / t['base'][k]
            lo[k] = mid - w / 2
        span.append(w)
    t.update(lo=lo, hi=hi, span=tuple(span), full=(t['base'][0] * span[0], t['base'][1] * span[1]),
             need=(math.sqrt(need_area / uv_area) * px / radius if px and radius and uv_area > 0 else None),
             clamped_faces=clamped, clamped_area=clamped_area)
    if limit is not None:
        if clamped and clamped == len(base['face_list']):
            t['cap'] = 0.0                               # every face clamped: the smallest content
        elif t['need']:
            t['cap'] = min(1.0, min_ratio * t['need'] / math.sqrt(t['base'][0] * t['base'][1]))
    return t


def plan_layout(record, mats, alpha, textures, px, sizes=(1024, 2048), gutter=GUTTER, slots=('diffuse', 'light'),
                min_ratio=MIN_RATIO, outlier_span=OUTLIER_SPAN, keep=frozenset()):
    """Tiles, per-face keys and the chosen atlas size for the opaque faces of `record`.

    Per size: the uniform layout (one scale for every tile) is taken when its minimum tile ratio reaches
    min_ratio or its scale is 1. Otherwise the clamped layout is tried at the same size (span-clamped
    faces and capped tiles, _tile_stats) and taken when it reaches min_ratio or scale 1; at the largest
    size the clamped layout is taken. Every tile carries 'share' (its faces' mesh-space area over the
    atlased area), 'clamped_share' (the span-clamped faces' share) and 'ratio'. Materials in `keep`
    (light_bleed) are left out like the alpha materials."""
    pts = record['points']
    radius = max((math.sqrt(p[1] ** 2 + p[2] ** 2 + p[3] ** 2) for p in pts if p[0] & 1), default=0.0)
    tiles, tile_of = [], {}
    for pi, part in enumerate(record['parts']):
        if part['flags'] & HIDDEN_PART:
            continue
        for gi, g in enumerate(part['groups']):
            mi = g['material']
            if mi in alpha or mi in keep:
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
                    t = dict(key=key, names=names, mats=[], area=0.0, faces=0, face_list=[])
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
                ranges = ((min(u for u, _ in uvs) - su, max(u for u, _ in uvs) - su),
                          (min(v for _, v in uvs) - sv, max(v for _, v in uvs) - sv))
                (a, b), (c, d), (e, h) = uvs
                area = body_materials.face_area(pts, f)
                t['face_list'].append(((pi, gi, fi, su, sv), ranges, area,
                                       0.5 * abs((c - a) * (h - b) - (e - a) * (d - b)),
                                       max(ranges[0][1] - ranges[0][0], ranges[1][1] - ranges[1][0])))
                t['area'] += area
                t['faces'] += 1
    if not tiles:
        return None
    src = tiles
    source_of = getattr(textures, 'source', lambda name: None)
    total = sum(t['area'] for t in tiles)
    for t in tiles:
        sizes_ = [s for s in (textures.size(v) for v in t['names'].values()) if s]
        t['base'] = (max(s[0] for s in sizes_), max(s[1] for s in sizes_)) if sizes_ else (32, 32)
        t['sources'] = {k: (source_of(v) or {}).get('member') for k, v in t['names'].items()}
        t['share'] = t['area'] / total if total > 0 else 1.0 / len(tiles)
    variants = {False: [_tile_stats(t, None, px, radius, min_ratio) for t in src]}
    tried, chosen = [], None
    for n in sizes:
        for clamp in (False, True):
            if clamp not in variants:
                variants[True] = [_tile_stats(t, outlier_span, px, radius, min_ratio) for t in src]
            vt = variants[clamp]
            scale, cont, pos = fit(vt, n, gutter)
            ratios = [r for r in (_tile_ratio(t, c) for t, c in zip(vt, cont)) if r is not None]
            low = min(ratios) if ratios else math.inf
            tried.append((n, scale, low, clamp))
            chosen = (n, scale, cont, pos, low, clamp)
            if low >= min_ratio or scale >= 1.0:      # at scale 1 a larger atlas adds nothing
                break
        else:
            continue
        break
    n, scale, cont, pos, low, clamp = chosen
    tiles = variants[clamp]
    face_keys = {}
    for ti, (t, src_t, c, p) in enumerate(zip(tiles, src, cont, pos)):
        t['content'] = c
        t['origin'] = (p[0] + gutter, p[1] + gutter)             # content origin, texels
        t['ratio'] = _tile_ratio(t, c)
        t['capped'] = bool(clamp and t.get('cap', 1.0) < scale)
        t['clamped_share'] = t['clamped_area'] / total if total > 0 else 0.0
        for (pi, gi, fi, su, sv), _, _, _, ext in src_t['face_list']:
            face_keys[pi, gi, fi] = (ti, su, sv, 1) if clamp and ext > outlier_span else (ti, su, sv)
    return dict(size=n, scale=scale, gutter=gutter, tiles=tiles, face_keys=face_keys, radius=radius, px=px,
                min_ratio=low, ratio_ok=low >= min_ratio, tried=[x[:3] for x in tried],
                tried_clamped=[x[3] for x in tried], clamped=clamp, outlier_span=outlier_span, area=total,
                slots=tuple(slots))


def atlas_uv(t, n, u, v, su, sv, clamp=False):
    """Atlas UV (periods of the atlas, float) of an original UV under shift (su, sv) in tile t; clamp (a
    span-clamped face) clamps the shifted UV into the tile's span first."""
    du, dv = u - su - t['lo'][0], v - sv - t['lo'][1]
    if clamp:
        du, dv = min(max(du, 0.0), t['span'][0]), min(max(dv, 0.0), t['span'][1])
    x = t['origin'][0] + du * t['content'][0] / t['span'][0]
    y = t['origin'][1] + dv * t['content'][1] / t['span'][1]
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


def rewrite_record(record, layout, atlas_index, alpha, alpha_remap=None, max_group_points=None, keep=frozenset()):
    """C with rewritten UVs, duplicated points and regrouped parts; returns (lod, info). An output
    group referencing more than max_group_points distinct points is split (split_faces: whole
    source groups packed first-fit) into groups of the same material; a point used by an earlier split group is
    duplicated (with its tangent record) so that the split groups share no point. atlas_index is
    one atlas material index for every opaque group, or {source material: atlas material} (one
    output class per distinct atlas material per part, in first-use order). A material in `keep`
    (light_bleed) keeps its own material and UVs, one group per part after the atlas groups and before
    the alpha group, like the glow collapse's kept materials."""
    atlas_of = (lambda m: atlas_index) if isinstance(atlas_index, int) else atlas_index.__getitem__
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

    parts, faces_map, pending, span_clamped = [], [], [], set()
    n = layout['size'] if layout else 0
    for pi, part in enumerate(record['parts']):
        new = {'flags': part['flags'], 'groups': []}
        groups = part['groups']
        pre = part['flags'] & bob1.PART_PRECOMPUTED
        if part['flags'] & HIDDEN_PART:                   # copied group by group, original UVs
            classes = [([(gi, g)], g['material'], False) for gi, g in enumerate(groups)]
        else:
            classes = []
            for gi, g in enumerate(groups):
                if g['material'] in alpha or g['material'] in keep:
                    continue
                ai = atlas_of(g['material'])
                cls = next((c for c in classes if c[1] == ai), None)
                if cls is None:
                    cls = ([], ai, True)
                    classes.append(cls)
                cls[0].append((gi, g))
            for m in dict.fromkeys(g['material'] for g in groups if g['material'] in keep):
                classes.append(([(gi, g) for gi, g in enumerate(groups) if g['material'] == m], m, False))
            classes.append(([(gi, g) for gi, g in enumerate(groups) if g['material'] in alpha], None, False))
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
                        if len(key) > 3:                  # span-clamped face (plan_layout clamped layout)
                            span_clamped.add(len(faces_map))
                        faces_map.append((pi, f, out, g['material'], key[0]))
            pending.append((new, pi, cls, material, faces, ref_group, pre, blocks))
        if 'bounds' in part:
            new['bounds'] = list(part['bounds'])
        parts.append(new)
    for (i, key), j in index.items():
        if key is not None:
            t = layout['tiles'][key[0]]
            u, v = point_uv(pts[i])
            au, av = atlas_uv(t, n, u, v, key[1], key[2], len(key) > 3)
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
                     missing_records=missing, copies_of=copies_of, split=split, span_clamped=span_clamped)


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


OCCLUSION_SLOT = b't_occlusiontexture'


def effect_name(material):
    return material.get('effect', b'').decode('latin1').lower()


def occlusion_name(material):
    """Lower-case t_OcclusionTexture of an effect material; None for absent / NULL / NONE_*."""
    for n, t, v in material.get('params', ()):
        if t == 8 and n.lower() == OCCLUSION_SLOT:
            return None if body_materials.is_null(v) or body_materials.is_stock(v) else v.lower().decode('latin1')
    return None


def effect_classes(mats, opaque):
    """[(effect, [material indices in first-use order])] of the opaque groups, largest face count
    first (ties: first use). Refuses a group material index outside the table."""
    faces, order = {}, {}
    for g in opaque:
        m = g['material']
        if not 0 <= m < len(mats):
            raise AtlasError(f'group material index {m} is outside the material table (0..{len(mats) - 1})')
        eff = effect_name(mats[m])
        order.setdefault(eff, [])
        if m not in order[eff]:
            order[eff].append(m)
        faces[eff] = faces.get(eff, 0) + len(g['faces'])
    ranked = sorted(order, key=lambda e: (-faces[e], list(order).index(e)))
    return [(e, order[e]) for e in ranked]


def collapse(assets, body, mats, record, alpha, px, sizes=(1024, 2048), specular=False, synth=True,
             gutter=GUTTER, min_ratio=MIN_RATIO, textures=None, bump=True, max_group_points=None, keep=frozenset(),
             layout=None):
    """Atlas collapse of `record`: appends one atlas material per opaque effect file (then any
    synthesized alpha material) to `mats` in place. Returns dict(record, layout, atlas_index (the
    largest effect's), atlas_indices, atlas_of, names, members, synth, uv2, occlusion, kept, ...).
    Materials in `keep` (light_bleed) are not atlased: they keep their own groups, textures and UVs and
    take no part in the effect classes, the occlusion check or the g_Mat* means. `layout` (light_bleed's
    repack) replaces plan_layout; it must have been planned with the same keep set."""
    import lod_overlay
    textures = textures or Textures(assets)
    areas, opaque = {}, []
    for part in record['parts']:
        if part['flags'] & HIDDEN_PART:
            continue
        for g in part['groups']:
            if g['material'] not in alpha and g['material'] not in keep:
                opaque.append(g)
                areas[g['material']] = areas.get(g['material'], 0.0) + sum(
                    body_materials.face_area(record['points'], f) for f in g['faces'])
    if not opaque:
        raise AtlasError('the record has no opaque faces to atlas')
    uv2 = sum(1 for p in record['points'] if p[0] & 4)
    classes = effect_classes(mats, opaque)
    occlusion = {}
    for eff, mis in classes:
        names_ = sorted({occlusion_name(mats[m]) or 'none' for m in mis}, key=str)
        if len(names_) > 1:
            raise AtlasError(f'the opaque materials of effect {eff or "(none)"} carry {len(names_)} different'
                             f' occlusion textures {names_} (second UV set: {uv2} points); one merged material'
                             ' cannot sample them all')
        occlusion[eff] = names_[0]
    dom = dominant(opaque)
    has_bump = any(t == 8 and n.lower() == SLOT_NAMES['bump'] for n, t, _ in mats[dom].get('params', ()))
    has_spec = any(t == 8 and n.lower() == SLOT_NAMES['specular'] for m in areas for n, t, _ in mats[m].get('params', ()))
    slots = (('diffuse', 'light') + (('bump',) if bump and has_bump else ())
             + (('specular',) if specular and has_spec else ()))
    if layout is not None and tuple(layout['slots']) != tuple(slots):
        layout = None                      # keeping changed the slot set (dominant's bump, specular): re-plan
    layout = layout or plan_layout(record, mats, alpha, textures, px, sizes, gutter, slots, min_ratio, keep=keep)
    names, members = texture_names(body, slots)
    atlas_of, indices, report = {}, [], []
    for eff, mis in classes:
        d = dominant([g for g in opaque if g['material'] in mis])
        mat, rows = atlas_material(mats, d, names, {m: areas[m] for m in mis}, synth)
        idx = len(mats)
        mats.append(mat)
        indices.append(idx)
        for m in mis:
            atlas_of[m] = idx
        report.append(dict(index=idx, dominant=d, absorbed=sorted(mis), params=rows, atlas=True, effect=eff,
                           occlusion=occlusion[eff]))
    atlas_index = indices[0]
    remap = {}
    if synth:
        alpha_only = {'points': record['points'], 'parts': [
            {'flags': p['flags'], 'groups': [g for g in p['groups'] if g['material'] in alpha]}
            for p in record['parts'] if not p['flags'] & HIDDEN_PART]}
        remap, more = lod_overlay.synth_materials(mats, alpha_only, alpha, 'two', frozenset())
        report += more
    lod, info = rewrite_record(record, layout, atlas_of, alpha, remap, max_group_points, keep)
    effects = sorted({(mats[m].get('effect', b'').decode('latin1'), mats[m].get('technique')) for m in areas})
    return dict(record=lod, layout=layout, atlas_index=atlas_index, atlas_indices=indices, atlas_of=atlas_of,
                dominant=dom, names=names, members=members, slots=slots, synth=report, info=info,
                effects=effects, textures=textures, uv2=uv2, occlusion=occlusion, kept=sorted(keep))


# --- baking -------------------------------------------------------------------------------

def level_weights(k0, k1, scale, origin, content, lo, span, src_n):
    """(k1 - k0, src_n) area-resampling matrix for one axis of a tile at one mip level: level
    texel k covers level-0 atlas texels [k * scale, (k + 1) * scale), which map to the source
    periods lo + (x - origin) * span / content of a repeating source (origin / content: the
    tile's content origin and size in level-0 texels)."""
    # Vectorised (2026-09-23): per level texel the coverage of every source texel index t in
    # [floor(a), floor(b)] is folded onto t mod src_n: whole periods, the partial period of the
    # interior run and the two edge texels. The former per-texel Python loop scaled with the
    # tile's span in source texels (a tiling texture spanning hundreds of periods took hours).
    rows = k1 - k0
    step = scale * span / content * src_n
    k = np.arange(k0, k1, dtype=np.float64)
    a = (lo + (k * scale - origin) * span / content) * src_n
    b = a + step
    fa, fb = np.floor(a).astype(np.int64), np.floor(b).astype(np.int64)
    m = np.zeros((rows, src_n), np.float64)
    if rows == 0:
        return m.astype(np.float32)
    j = np.arange(src_n, dtype=np.int64)
    c = np.maximum(fb - fa - 1, 0)                    # interior texels fa+1 .. fb-1, coverage 1 each
    m += (c // src_n)[:, None]
    m += (((j[None, :] - ((fa + 1) % src_n)[:, None]) % src_n) < (c % src_n)[:, None])
    r = np.arange(rows)
    same = fb == fa
    np.add.at(m, (r, fa % src_n), np.where(same, b - a, fa + 1 - a))
    np.add.at(m, (r, fb % src_n), np.where(same, 0.0, b - fb))
    m /= step
    return m.astype(np.float32)


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


# --- light bleed --------------------------------------------------------------------------

def light_level(ratio, size, widen_levels=WIDEN_LEVELS):
    """Mip level the widened light-map fetch reads a tile at the switch size: ceil(log2(atlas texels per
    screen pixel)) (at least 0) + widen_levels, at most the atlas's 1x1 level; None without a ratio."""
    if ratio is None or not ratio > 0:
        return None
    return int(min(size.bit_length() - 1, max(0, math.ceil(math.log2(ratio) - 1e-9)) + widen_levels))


def _box3(img):
    """3x3 box mean of a 2-D array (edge texels repeated)."""
    h, w = img.shape
    q = np.pad(img, 1, mode='edge')
    return sum(q[dy:dy + h, dx:dx + w] for dy in range(3) for dx in range(3)) / 9.0


def tile_share(layout, t):
    """Mesh-space face-area share of tile `t` over the layout's atlased surface as planned (layout 'area',
    kept by repack_layout and prune_layout, so a pruned tile's share does not grow); the tile's 'share'
    when the layout carries no areas, None without either."""
    if t.get('area') is not None and layout.get('area'):
        return t['area'] / layout['area']
    return t.get('share')


def light_bleed(layout, record, info, sources, bleed_max=LIGHT_BLEED_MAX, widen_levels=WIDEN_LEVELS,
                share_min=0.0):
    """Per tile of `layout` (rewritten `record`, its rewrite info, per-tile float light sources as
    tile_sources): at L = light_level(tile ratio), the Rec.709 luminance (0..255) of the tile-aware light
    atlas level L (bake_level) around 7 barycentric samples per face (the level-L texel holding the sample
    and a +-1 texel box, the GPU's bilinear and widened footprint) against the same texels resampled from
    the tile's own repeating light map alone (the source at the equivalent level, no neighbour). 'added'
    = face-area-weighted mean of max(0, atlas box - own box); 'own' = mean of the own box (the tile's
    light); over when added > bleed_max, and then flagged (counted for the remedy) when the tile's share
    (tile_share; None counts) is at least share_min, else ignored. Rows carry share and texels (content
    width, height at level 0). Tiles without a ratio or faces are not checked (level None)."""
    n, g = layout['size'], layout['gutter']
    pts = record['points']
    uv_of = lambda j: point_uv(pts[j])
    by_tile = {}
    for _, _, of, _, ti in info['faces']:
        by_tile.setdefault(ti, []).append(of)
    atlas_lum, rows = {}, []
    for ti, (t, src) in enumerate(zip(layout['tiles'], sources)):
        L, faces = light_level(t.get('ratio'), n, widen_levels), by_tile.get(ti)
        row = dict(tile=ti, mats=list(t['mats']), name=(t['names'].get('diffuse') or b'').decode('latin1'),
                   light=(t['names'].get('light') or b'').decode('latin1'), level=L, own=None, added=None,
                   share=_num(tile_share(layout, t)), texels=list(t['content']), flagged=False, ignored=False)
        rows.append(row)
        if L is None or not faces:
            row['level'] = None
            continue
        if L not in atlas_lum:
            with np.errstate(divide='ignore', over='ignore', invalid='ignore'):
                atlas_lum[L] = _box3(bake_level(layout, sources, 'light', L)[..., :3].astype(np.float64) @ LUMA_709)
        nl, s = max(1, n >> L), 1 << L
        uv = np.array([[uv_of(j) for j in f[:3]] for f in faces], np.float64)          # (F, 3, 2)
        area = np.array([body_materials.face_area(pts, f) for f in faces], np.float64)
        if not area.sum() > 0:
            area = np.ones(len(faces))
        smp = np.einsum('kj,fjc->fkc', BARY7, uv)                                      # (F, 7, 2)
        ix = np.clip(np.floor(smp[..., 0] * nl).astype(np.int64), 0, nl - 1)
        iy = np.clip(np.floor(smp[..., 1] * nl).astype(np.int64), 0, nl - 1)
        got = atlas_lum[L][iy, ix]
        x0, x1, y0, y1 = int(ix.min()) - 1, int(ix.max()) + 2, int(iy.min()) - 1, int(iy.max()) + 2
        if src is None:
            own = np.zeros(got.shape)
        else:
            (cx, cy), (cw, ch) = t['origin'], t['content']
            with np.errstate(divide='ignore', over='ignore', invalid='ignore'):
                lum = src[..., :3].astype(np.float64) @ LUMA_709                       # linear: resample luminance
            my = level_weights(y0, y1, s, cy, ch, t['lo'][1], t['span'][1], lum.shape[0]).astype(np.float64)
            mx = level_weights(x0, x1, s, cx, cw, t['lo'][0], t['span'][0], lum.shape[1]).astype(np.float64)
            with np.errstate(divide='ignore', over='ignore', invalid='ignore'):   # spurious on macOS Accelerate
                own = _box3(my @ lum @ mx.T)[iy - y0, ix - x0]
            if not np.isfinite(own).all():
                raise AtlasError(f'light_bleed: non-finite own light in tile {ti}')
        w = area / area.sum()
        row['own'] = round(float((own.mean(1) * w).sum()), 3)
        row['added'] = round(float((np.maximum(0.0, got - own).mean(1) * w).sum()), 3)
        if row['added'] > bleed_max:
            small = row['share'] is not None and row['share'] < share_min
            row['flagged'], row['ignored'] = not small, small
    return dict(tiles=rows, flagged=[r['tile'] for r in rows if r['flagged']],
                ignored=[r['tile'] for r in rows if r['ignored']], max=bleed_max, widen_levels=widen_levels,
                share_min=share_min)


def repack_layout(layout, emit, pad, min_ratio=MIN_RATIO, max_loss=LIGHT_BLEED_SCALE_LOSS):
    """Copy of `layout` with the tiles in `emit` shelf-packed first (their own region) with an empty
    margin of `pad` texels around each (background, not gutter: the gutter holds the tile's own repeat),
    then the other tiles from a new shelf; same atlas size and face keys. plan_layout's scale is the
    largest that packs, so the regions and margins usually need a lower one: the largest uniform scale
    <= the layout's that packs is searched (fit), and it is taken only while the scale drops by at most
    max_loss relative to the layout's and the minimum tile ratio stays >= min(min_ratio, the layout's own
    minimum). The margin halves until both hold (down to 0, the regions alone); None when nothing does."""
    n, g = layout['size'], layout['gutter']
    tiles = layout['tiles']
    emit = set(emit)
    floor = min(min_ratio, layout['min_ratio']) - 1e-9
    top = layout['scale']

    def attempt(sc, m):
        cont = [(_side(t['full'][0], min(sc, t.get('cap', 1.0))), _side(t['full'][1], min(sc, t.get('cap', 1.0))))
                for t in tiles]
        rects = [(w + 2 * g + 2 * (m if i in emit else 0), h + 2 * g + 2 * (m if i in emit else 0))
                 for i, (w, h) in enumerate(cont)]
        return cont, shelf_pack(rects, n, emit)
    while True:
        cont, pos = attempt(top, pad)
        sc = top
        if pos is None:
            lo, hi = 0.0, top
            for _ in range(40):
                mid = (lo + hi) / 2
                lo, hi = (mid, hi) if attempt(mid, pad)[1] is not None else (lo, mid)
            sc = lo
            cont, pos = attempt(sc, pad)
        if pos is not None:
            ratios = [_tile_ratio(t, c) for t, c in zip(tiles, cont)]
            low = min((r for r in ratios if r is not None), default=math.inf)
            if low >= floor and sc >= top * (1.0 - max_loss) - 1e-12:
                break
        if pad == 0:
            return None
        pad //= 2
    m = lambda i: pad if i in emit else 0
    new = [dict(t, content=c, ratio=r, origin=(x + g + m(i), y + g + m(i)))
           for i, (t, c, r, (x, y)) in enumerate(zip(tiles, cont, ratios, pos))]
    return dict(layout, tiles=new, scale=sc, min_ratio=low, ratio_ok=low >= min_ratio,
                repacked=dict(emitters=sorted(emit), pad=pad, scale_before=layout['scale'], scale=sc,
                              min_ratio_before=layout['min_ratio'], min_ratio=low))


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


CHECK_FACES = 4096         # faces sampled per slot by check() (b); (c) always covers every face
CHECK_TEXELS = 2e7         # source texels the box reference of (b) may gather per slot (thins the sample)
CHECK_MAP_TEXELS = 1.0     # build() refuses a body with a face whose inverse-map error (c) exceeds this many source texels
CHECK_MAP_ATLAS_TEXELS = 0.1   # ... and this many atlas texels (16.16 rounding: <= size / 131072 atlas texels)


def check(source_record, out_record, result, atlases, mats, max_faces=CHECK_FACES):
    """(b) per slot: atlas bilinear at the rewritten face centroid vs the source bilinear (wrap)
    at the original centroid (mip 0), and vs the source box-filtered over one atlas texel
    (a reference without the downsampling loss); mean / p95 / max over the sampled faces of the
    mean |RGB| error and of the |A| error (0..255). At most max_faces faces are sampled, evenly
    spaced in face order (every face below that; 'sampled_faces' reports the count): the box
    reference gathers span/content source texels per face and axis, so a tiling texture spanning
    hundreds of periods on a 200k-face body cost an hour per body (2026-09-23).
    (c) every rewritten vertex UV inside its tile content (and gutter), and the inverse-mapped
    centroid equal to the original one modulo the integer shift (max error in source texels),
    over every face, also in atlas texels; 'map_error_faces' counts the faces off by more than
    CHECK_MAP_TEXELS source texels and CHECK_MAP_ATLAS_TEXELS atlas texels (a tile holding far fewer
    texels than its source turns the 16.16 rounding of the atlas UV into several source texels, which
    is not a mapping error). A span-clamped face (plan_layout clamped layout; UVs clamped into the tile) counts
    in the inside test only: it is neither inverse-mapped nor sampled ('span_clamped_faces')."""
    layout, textures = result['layout'], result['textures']
    n, g = layout['size'], layout['gutter']
    spts, opts = source_record['points'], out_record['points']
    faces = result['info']['faces']
    clamped = result['info'].get('span_clamped', ())
    inside = in_gutter = 0
    worst_map = worst_atlas = 0.0
    map_errors = 0
    su_list = []
    stride = max(1, -(-len(faces) // max_faces)) if max_faces else 1
    for fi, (pi, sf, of, mi, ti) in enumerate(faces):
        t = layout['tiles'][ti]
        if mi not in t['mats']:
            raise AtlasError(f'atlas check: face of material {mi} was placed in tile {ti} of materials {t["mats"]}')
        (cx, cy), (cw, ch) = t['origin'], t['content']
        for j in of[:3]:
            u, v = point_uv(opts[j])
            x, y = u * n, v * n
            tol = 2.0 * n / UV_ONE
            inside += cx - tol <= x <= cx + cw + tol and cy - tol <= y <= cy + ch + tol
            in_gutter += cx - g <= x <= cx + cw + g and cy - g <= y <= cy + ch + g
        if fi in clamped:
            continue
        ou = np.mean([point_uv(spts[i]) for i in sf[:3]], 0)
        nu = np.mean([point_uv(opts[j]) for j in of[:3]], 0)
        su, sv = face_shift([point_uv(spts[i]) for i in sf[:3]])
        inv_u = t['lo'][0] + (nu[0] * n - cx) * t['span'][0] / cw
        inv_v = t['lo'][1] + (nu[1] * n - cy) * t['span'][1] / ch
        du, dv = abs(inv_u - (ou[0] - su)), abs(inv_v - (ou[1] - sv))
        e_src = max(du * t['base'][0], dv * t['base'][1])
        e_atl = max(du * cw / t['span'][0], dv * ch / t['span'][1])
        worst_map, worst_atlas = max(worst_map, e_src), max(worst_atlas, e_atl)
        map_errors += int(e_src > CHECK_MAP_TEXELS and e_atl > CHECK_MAP_ATLAS_TEXELS)
        if fi % stride == 0:
            su_list.append((ou, nu, mi, t))
    # the box reference gathers (span / content * source side) texels per axis and face: thin the
    # sample further so that the gathered texels stay within CHECK_TEXELS per slot
    work = sum((t['span'][0] / t['content'][0] * t['base'][0] + 1) * (t['span'][1] / t['content'][1] * t['base'][1] + 1)
               for _, _, _, t in su_list)
    if work > CHECK_TEXELS:
        thin = math.ceil(work / CHECK_TEXELS)
        su_list = su_list[::thin]
    out = dict(faces=len(faces), vertices=3 * len(faces), inside=inside, in_gutter=in_gutter,
               max_map_error_texels=float(worst_map), max_map_error_atlas_texels=float(worst_atlas), map_error_faces=map_errors,
               sampled_faces=len(su_list), span_clamped_faces=len(clamped), slots={})
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

def prune_layout(layout, keep):
    """Copy of `layout` without the tiles of the materials in `keep` (light_bleed's kept groups): every
    other tile keeps its position and content (the freed area turns background), face keys are renumbered,
    shares and the minimum ratio recomputed. Removing tiles only removes light next to the others."""
    tiles = [t for t in layout['tiles'] if not set(t['mats']) & set(keep)]
    index = {id(t): k for k, t in enumerate(tiles)}
    old = {k: index.get(id(t)) for k, t in enumerate(layout['tiles'])}
    total = sum(t.get('area', 0.0) for t in tiles)
    tiles = [dict(t, share=t.get('area', 0.0) / total if total > 0 else 1.0 / len(tiles)) for t in tiles]
    keys = {f: (old[k[0]],) + tuple(k[1:]) for f, k in layout['face_keys'].items() if old[k[0]] is not None}
    ratios = [t['ratio'] for t in tiles if t.get('ratio') is not None]
    low = min(ratios) if ratios else math.inf
    return dict(layout, tiles=tiles, face_keys=keys, min_ratio=low, ratio_ok=low >= MIN_RATIO,
                pruned=sorted(keep))


def guard_bleed(assets, body, mats, record, alpha, px, sizes, specular, synth, bump, max_group_points, textures,
                bleed_max=LIGHT_BLEED_MAX, widen_levels=WIDEN_LEVELS, emitter_luma=EMITTER_LUMA,
                rounds=LIGHT_BLEED_ROUNDS, scale_loss=LIGHT_BLEED_SCALE_LOSS, share_min=LIGHT_BLEED_SHARE):
    """collapse with the light-atlas bleed guard: (result, report). After the layout, light_bleed checks
    every tile; a tile over the limit below share_min of the atlased surface is only reported (ignored, from
    the first check); with none counted the result is the unguarded collapse. Flagged tiles are remedied in this order of cost: (1) repack_layout with the emitter tiles
    (own light above emitter_luma, not flagged) in their own region and a 2^(L+1)-texel empty margin (L =
    the highest flagged level; halved as needed), accepted only when the atlas scale drops by at most
    scale_loss and fewer tiles are flagged afterwards; (2) every tile still flagged keeps its materials as
    their own groups (collapse keep: one extra draw per part and material) at the current pack
    (prune_layout: the other tiles keep their places). Repeated up to `rounds` times; a tile still flagged
    then is accepted and reported as residual. The atlas and alpha materials collapse appends depend on
    the keep set only, not on the layout, so `mats` matches whichever attempt is taken."""
    n_mats = len(mats)
    keep, repack = set(), None

    def run(layout=None):
        del mats[n_mats:]
        r = collapse(assets, body, mats, record, alpha, px, sizes, specular, synth, bump=bump,
                     max_group_points=max_group_points, textures=textures, keep=frozenset(keep), layout=layout)
        if 'light' not in r['slots']:
            return r, None
        return r, light_bleed(r['layout'], r['record'], r['info'], tile_sources(r['layout'], r['textures'], 'light'),
                              bleed_max, widen_levels, share_min)
    res, chk = run()
    first = chk
    for rnd in range(rounds + 1):
        if chk is None or not chk['flagged']:
            break
        rows = chk['tiles']
        emit = [r['tile'] for r in rows if r['own'] is not None and r['own'] > emitter_luma and not r['flagged']]
        if emit:
            top = max(rows[i]['level'] for i in chk['flagged'])
            lay = repack_layout(res['layout'], emit, min(1 << (top + 1), res['layout']['size']), max_loss=scale_loss)
            if lay is not None:
                res2, chk2 = run(lay)
                if len(chk2['flagged']) < len(chk['flagged']):
                    repack = dict(lay['repacked'], emitters=sorted(m for i in emit for m in rows[i]['mats']),
                                  level=top)
                    res, chk = res2, chk2
                    if not chk['flagged']:
                        break
        add = {m for i in chk['flagged'] for m in chk['tiles'][i]['mats']}
        atlased = {m for t in res['layout']['tiles'] for m in t['mats']}
        if rnd == rounds or atlased <= add:            # nothing left to keep: accepted, reported as residual
            break
        keep |= add
        res, chk = run(prune_layout(res['layout'], keep))
    flagged = [r for r in first['tiles'] if r['flagged']] if first else []
    remedy = (None if not flagged else 'residual' if chk['flagged'] else
              'keep+repack' if keep and repack else 'keep' if keep else 'repack')
    report = dict(max=bleed_max, widen_levels=widen_levels, emitter_luma=emitter_luma, scale_loss=scale_loss,
                  share_min=share_min, checked=sum(1 for r in first['tiles'] if r['level'] is not None) if first else 0,
                  flagged=flagged, ignored=[r for r in first['tiles'] if r['ignored']] if first else [], remedy=remedy, repack=repack, kept=sorted(keep),
                  residual=[r for r in chk['tiles'] if r['flagged']] if chk else [],
                  tiles=chk['tiles'] if chk else [])
    return res, report


def build(assets, body, mats, record, alpha, px, sizes=(1024, 2048), fmt='dxt', specular=False, synth=True,
          bump=True, max_group_points=None, textures=None, light_bleed_max=LIGHT_BLEED_MAX,
          widen_levels=WIDEN_LEVELS, light_bleed_scale_loss=LIGHT_BLEED_SCALE_LOSS, light_bleed_share=LIGHT_BLEED_SHARE):
    """collapse (with the light-bleed guard unless light_bleed_max is falsy) + bake + encode + check; the
    result carries the encoded DDS per slot, the checks and 'light_bleed' (guard_bleed's report)."""
    textures = textures or Textures(assets)
    if light_bleed_max:
        res, bleed = guard_bleed(assets, body, mats, record, alpha, px, sizes, specular, synth, bump,
                                 max_group_points, textures, light_bleed_max, widen_levels,
                                 scale_loss=light_bleed_scale_loss, share_min=light_bleed_share)
    else:
        res, bleed = collapse(assets, body, mats, record, alpha, px, sizes, specular, synth, bump=bump,
                              max_group_points=max_group_points, textures=textures), None
    res['light_bleed'] = bleed
    images = bake(res['layout'], res['textures'])
    res['encoded'] = encode(images, res['layout'], fmt)
    res['check'] = check(record, res['record'], res, {s: e['decoded'] for s, e in res['encoded'].items()}, mats)
    c = res['check']
    if c['inside'] < c['vertices']:
        raise AtlasError(f'atlas check: {c["vertices"] - c["inside"]} of {c["vertices"]} rewritten vertex UVs fall'
                         ' outside their tile content')
    if c.get('map_error_faces'):
        raise AtlasError(f'atlas check: inverse-map error {c["max_map_error_texels"]:.3f} source texels exceeds'
                         f' {CHECK_MAP_TEXELS} (and {CHECK_MAP_ATLAS_TEXELS} atlas texels) on {c["map_error_faces"]}'
                         ' faces')
    return res


def _num(x):
    return None if x is None else round(float(x), 6)


def tile_rows(layout):
    """Compact per-tile rows of a layout for the texel rule (texel_floor) and the census: the diffuse
    name, atlas texels per screen pixel at the switch size, the tile's share of the atlased surface
    (mesh-space face area; the screen share is inferred proportional to it), its UV span, and its
    span-clamped faces (count, share) and cap flag of the clamped layout."""
    return [dict(name=(t['names'].get('diffuse') or b'').decode('latin1'), texels_per_px=_num(t['ratio']),
                 share=_num(t['share']), span=[_num(x) for x in t['span']], clamped_faces=t.get('clamped_faces', 0),
                 clamped_share=_num(t.get('clamped_share', 0.0)), capped=bool(t.get('capped')))
            for t in layout['tiles']]


def texel_floor(rows, min_texels, floor_share):
    """Area-weighted texel rule over tile_rows: the atlased surface is split into each tile's kept faces
    (at the tile's ratio) and its span-clamped faces (ratio 0: their UVs do not reproduce the source).
    Starved = the parts below min_texels; refuse when their share exceeds floor_share (so floor_share 0
    is the old per-tile minimum rule, min_texels 0 disables). weighted_texels_per_px is the ratio at the
    floor_share area quantile (ascending): refuse <=> weighted_texels_per_px < min_texels. texel_clamped
    lists the starved parts per tile (tile, texels_per_px, share = the tile's share, starved_share,
    span_clamped_faces). Tiles without a measured ratio (no radius or UV area) take no part."""
    parts = []
    for k, r in enumerate(rows):
        cs = r.get('clamped_share') or 0.0
        kept = max(0.0, (r.get('share') or 0.0) - cs)
        if r.get('texels_per_px') is not None and kept > 0:
            parts.append((r['texels_per_px'], kept, k))
        if cs > 0:
            parts.append((0.0, cs, k))
    parts.sort(key=lambda x: x[0])
    starved, clamped = 0.0, {}
    for ratio, share, k in parts:
        if min_texels and ratio < min_texels:
            starved += share
            r = rows[k]
            e = clamped.setdefault(k, dict(tile=r['name'], texels_per_px=r.get('texels_per_px'), share=r.get('share'),
                                           starved_share=0.0, span_clamped_faces=r.get('clamped_faces', 0)))
            e['starved_share'] = round(e['starved_share'] + share, 6)
    total = sum(x[1] for x in parts)
    weighted, cum = None, 0.0
    for ratio, share, _ in parts:
        cum += share
        weighted = ratio
        if cum > floor_share * total + 1e-12:
            break
    return dict(min_texels=min_texels, floor_share=floor_share, starved_share=round(starved, 6),
                weighted_texels_per_px=_num(weighted), refuse=bool(min_texels) and starved > floor_share + 1e-12,
                texel_clamped=sorted(clamped.values(), key=lambda e: -e['starved_share']))


def bleed_summary(b):
    """JSON-ready light_bleed report (guard_bleed): the flagged (counted) and ignored (under share_min)
    tiles of the first check, the remedy, the kept materials, the residual tiles and the final check's rows of the flagged tiles' materials ('after');
    None when the guard was off."""
    if b is None:
        return None
    row = lambda r: {k: r.get(k) for k in ('tile', 'mats', 'name', 'light', 'level', 'own', 'added', 'share', 'texels')}
    hit = {m for f in b['flagged'] for m in f['mats']}
    return dict(max=b['max'], widen_levels=b['widen_levels'], emitter_luma=b['emitter_luma'], checked=b['checked'],
                scale_loss=b.get('scale_loss'), share_min=b.get('share_min'),
                flagged=[row(r) for r in b['flagged']], ignored=[row(r) for r in b.get('ignored', [])], remedy=b['remedy'], repack=b['repack'], kept=b['kept'],
                residual=[row(r) for r in b['residual']], after=[row(r) for r in b['tiles'] if set(r['mats']) & hit])


def summary(res):
    """JSON-ready description of an atlas build (manifest)."""
    L, c = res['layout'], res['check']
    return dict(
        material=res['atlas_index'], materials=list(res.get('atlas_indices', [res['atlas_index']])),
        dominant=res['dominant'], size=L['size'], scale=_num(L['scale']),
        gutter=L['gutter'], px=L['px'], min_texels_per_px=_num(L['min_ratio']), ratio_ok=bool(L['ratio_ok']),
        tried=[dict(size=n, scale=_num(s), min_texels_per_px=_num(m), clamped=c)
               for (n, s, m), c in zip(L['tried'], L.get('tried_clamped', [False] * len(L['tried'])))],
        clamped=bool(L.get('clamped')), outlier_span=L.get('outlier_span'), radius=_num(L['radius']),
        duplicated_points=res['info']['duplicated'], points=len(res['record']['points']),
        split_groups=res['info']['split'],
        missing_tangent_records=res['info']['missing_records'], effects=[list(e) for e in res['effects']],
        uv2_points=res.get('uv2', 0), occlusion=res.get('occlusion', {}),
        kept_light_bleed=list(res.get('kept', [])),
        light_bleed=bleed_summary(res.get('light_bleed')),
        tiles=[dict(mats=t['mats'], names={k: (v.decode('latin1') if v else None) for k, v in t['names'].items()},
                    sources=dict(t.get('sources', {})),
                    lo=[_num(x) for x in t['lo']], span=[_num(x) for x in t['span']], base=list(t['base']),
                    content=list(t['content']), origin=list(t['origin']), texels_per_px=_num(t['ratio']),
                    name=(t['names'].get('diffuse') or b'').decode('latin1'), share=_num(t['share']),
                    clamped_faces=t.get('clamped_faces', 0), clamped_share=_num(t.get('clamped_share', 0.0)),
                    capped=bool(t.get('capped')))
               for t in L['tiles']],
        textures=[dict(slot=s, name=res['names'][s].decode('latin1'), member=res['members'][s], format=e['format'],
                       dds_bytes=e['bytes'], dds_sha256=e['sha256'],
                       compression_error_rgba=[[_num(m), _num(p)] for m, p in e['error']])
                  for s, e in res['encoded'].items()],
        check=dict(faces=c['faces'], sampled_faces=c.get('sampled_faces', c['faces']), vertices=c['vertices'], uv_inside_content=c['inside'],
                   uv_inside_gutter=c['in_gutter'], max_map_error_texels=_num(c['max_map_error_texels']),
                   span_clamped_faces=c.get('span_clamped_faces', 0),
                   max_map_error_atlas_texels=_num(c.get('max_map_error_atlas_texels')),
                   map_error_faces=c.get('map_error_faces', 0),
                   slots={s: {k: [_num(x) for x in v] for k, v in d.items()} for s, d in c['slots'].items()}))


def format_summary(s):
    """Report lines for lod_overlay.describe."""
    tried = ', '.join(f'{t["size"]}{" clamped" if t.get("clamped") else ""}: {t["min_texels_per_px"]:.3f}'
                      for t in s['tried'])
    mats = s.get('materials', [s['material']])
    lines = [f'atlas {s["size"]}x{s["size"]} (min atlas texels per screen pixel at px={s["px"]:g}: {tried}; '
             f'>= 2: {"yes" if s["ratio_ok"] else "NO"}) scale {s["scale"]:.4f} tiles {len(s["tiles"])} gutter '
             f'{s["gutter"]} duplicated points {s["duplicated_points"]} -> {s["points"]} points, tangent records'
             f' missing {s["missing_tangent_records"]}; materials {["mat%d" % m for m in mats]} (mat{s["material"]}'
             f' = copy of mat{s["dominant"]}); effects {s["effects"]}'
             + (f'; second UV set on {s["uv2_points"]} points passed through, occlusion {s["occlusion"]}'
                if s.get('uv2_points') else '')]
    b = s.get('light_bleed')
    if b:
        mats_ = lambda r: 'mat' + '/'.join(str(m) for m in r['mats'])
        rp = b.get('repack')
        ign = b.get('ignored') or []
        tile_ = lambda r: (f'{mats_(r)} L{r["level"]}'
                           + (f' share {r["share"]:.4f}' if r.get('share') is not None else '')
                           + (f' {r["texels"][0]}x{r["texels"][1]} texels' if r.get('texels') else '')
                           + f' own {r["own"]:.1f} +{r["added"]:.1f}')
        lines.append(f'atlas light_bleed={len(b["flagged"]) + len(ign)} counted={len(b["flagged"])} ignored={len(ign)}'
                     f' kept={len(b["kept"])}: {b["checked"]} tiles checked at'
                     f' L = ceil(log2 texels/px) + {b["widen_levels"]}, flagged above {b["max"]:g}/255 mean added'
                     ' luminance'
                     + (f', counted from share {b["share_min"]:g}' if b.get('share_min') is not None else '')
                     + ('; flagged ' + ', '.join(tile_(r) for r in b['flagged']) + f'; remedy {b["remedy"]}'
                        if b['flagged'] else '')
                     + (f' (emitters mat{rp["emitters"]} in their own region, margin {rp["pad"]} texels, L'
                        f' {rp["level"]}, scale {rp["scale_before"]:.4f} -> {rp["scale"]:.4f}, min texels/px'
                        f' {rp["min_ratio_before"]:.3f} -> {rp["min_ratio"]:.3f})' if rp else '')
                     + (f'; kept as own groups {["mat%d" % m for m in b["kept"]]}' if b['kept'] else '')
                     + ('; after ' + ', '.join(f'{mats_(r)} +{r["added"]:.1f}' for r in b['after'])
                        if b.get('after') else '')
                     + ('; ignored (under the share) ' + ', '.join(tile_(r) for r in ign) if ign else '')
                     + (f'; RESIDUAL {len(b["residual"])} tiles' if b['residual'] else ''))
    x = s.get('texel')
    if x:
        w = x['weighted_texels_per_px']
        lines.append(f'atlas texel rule: min {s["min_texels_per_px"]:.3f}, area-weighted'
                     f' {"-" if w is None else f"{w:.3f}"} atlas texels per screen pixel (ratio at the'
                     f' {x["floor_share"]:g} area quantile); below --min-texels {x["min_texels"]:g}:'
                     f' {100 * x["starved_share"]:.2f} % of the atlased surface (--texel-floor-share'
                     f' {100 * x["floor_share"]:g} %); layout {"clamped" if s.get("clamped") else "uniform"},'
                     f' capped tiles {sum(1 for t in s["tiles"] if t.get("capped"))}, span-clamped faces'
                     f' {sum(t.get("clamped_faces", 0) for t in s["tiles"])}')
        for e in x['texel_clamped']:
            r = e['texels_per_px']
            lines.append(f'atlas texel_clamped: {e["tile"]} ratio {"-" if r is None else f"{r:.3f}"} share'
                         f' {100 * e["share"]:.2f} % starved {100 * e["starved_share"]:.2f} %'
                         f' span-clamped faces {e["span_clamped_faces"]}')
    for t in s['tiles']:
        odd = {k: v for k, v in t.get('sources', {}).items() if v and not v.split(':', 1)[-1].lower().startswith('dds/')}
        if odd:
            lines.append(f'atlas tile mats {t["mats"]}: non-dds sources ' + ', '.join(f'{k}={v}' for k, v in odd.items()))
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
                 f' {c["max_map_error_texels"]:.3f} source texels'
                 + (f' / {c["max_map_error_atlas_texels"]:.4f} atlas texels' if c.get('max_map_error_atlas_texels') is not None else '')
                 + (f', span-clamped faces {c["span_clamped_faces"]}' if c.get('span_clamped_faces') else ''))
    for slot, d in c['slots'].items():
        lines.append(f'atlas sampling {slot} ({c.get("sampled_faces", c["faces"])} face centroids, mean/p95/max of |error| 0..255): vs source mip 0'
                     f' RGB {"/".join(f"{x:.2f}" for x in d["src_rgb"])} A {"/".join(f"{x:.2f}" for x in d["src_a"])};'
                     f' vs source prefiltered to the atlas texel RGB {"/".join(f"{x:.2f}" for x in d["box_rgb"])}'
                     f' A {"/".join(f"{x:.2f}" for x in d["box_a"])}'
                     + (f'; normal angle (degrees) vs source {"/".join(f"{x:.2f}" for x in d["src_angle"])},'
                        f' vs prefiltered {"/".join(f"{x:.2f}" for x in d["box_angle"])}' if 'src_angle' in d else ''))
    return lines
