#!/usr/bin/env python3
"""Bake and packetize deterministic spatial-fog family atlases."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import time

import numpy as np

sys.path.insert(0, str(Path(__file__).parents[1]))
import fog_field_recipe as recipe

MAGIC = b"X3FOGPK"
VERSION = 1
HEADER = struct.Struct("<8sIIIIIIIIIQI")
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
GRADIENTS = np.array([
    [-1,-1,0],[-1,1,0],[1,-1,0],[1,1,0],[-1,0,-1],[-1,0,1],
    [1,0,-1],[1,0,1],[0,-1,-1],[0,-1,1],[0,1,-1],[0,1,1],
], np.float64) / math.sqrt(2.)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fnv1a64(data: bytes) -> int:
    value = FNV_OFFSET
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & 0xffffffffffffffff
    return value


def fade(t):
    return t*t*t*(t*(t*6.-15.)+10.)


def smooth(lo, hi, x):
    t = np.clip((x-lo)/(hi-lo), 0., 1.)
    return t*t*(3.-2.*t)


class Noise:
    def __init__(self):
        self.cache = {}

    def lattice(self, seed, length):
        key = (seed, length)
        if key not in self.cache:
            n = int(recipe.PERIOD/length)
            self.cache[key] = np.random.default_rng(seed).integers(
                0, 12, size=(n, n, n), dtype=np.int64)
        return self.cache[key]

    def sample(self, seed, length, points):
        lattice = self.lattice(seed, length); n = lattice.shape[0]
        u = points/length; base = np.floor(u).astype(np.int64); t = u-base
        s = fade(t); out = np.zeros(points.shape[:-1], np.float64)
        for dz in (0, 1):
            wz = s[..., 2] if dz else 1-s[..., 2]
            iz = np.mod(base[..., 2]+dz, n); pz = t[..., 2]-dz
            for dy in (0, 1):
                wy = s[..., 1] if dy else 1-s[..., 1]
                iy = np.mod(base[..., 1]+dy, n); py = t[..., 1]-dy
                for dx in (0, 1):
                    wx = s[..., 0] if dx else 1-s[..., 0]
                    ix = np.mod(base[..., 0]+dx, n); px = t[..., 0]-dx
                    gradient = GRADIENTS[lattice[iz, iy, ix]]
                    dot = gradient[..., 0]*px + gradient[..., 1]*py + gradient[..., 2]*pz
                    out += wx*wy*wz*dot
        return out


def bake_fields():
    noise = Noise()
    axis = (np.arange(recipe.GRID)+.5)*recipe.PERIOD/recipe.GRID
    carrier = np.empty((recipe.GRID,)*3, np.float32)
    cavity = np.empty_like(carrier); chroma = np.empty_like(carrier)
    for z0 in range(0, recipe.GRID, 4):
        z, y, x = np.meshgrid(axis[z0:z0+4], axis, axis, indexing="ij")
        points = np.stack([x, y, z], -1); warp = np.empty_like(points)
        for j, seeds in enumerate(recipe.WARP_SEEDS):
            warp[..., j] = (recipe.WARP_WEIGHTS[0]*noise.sample(seeds[0], recipe.WARP_PERIODS[0], points) +
                            recipe.WARP_WEIGHTS[1]*noise.sample(seeds[1], recipe.WARP_PERIODS[1], points))
        warped = points + recipe.WARP_AMPLITUDE*warp
        carrier[z0:z0+len(z)] = (recipe.CARRIER[0][2]*noise.sample(recipe.CARRIER[0][0], recipe.CARRIER[0][1], warped) +
                                  recipe.CARRIER[1][2]*noise.sample(recipe.CARRIER[1][0], recipe.CARRIER[1][1], warped) +
                                  recipe.CARRIER[2][2]*noise.sample(recipe.CARRIER[2][0], recipe.CARRIER[2][1], warped)).astype(np.float32)
        cavity[z0:z0+len(z)] = (recipe.CAVITY[0][2]*noise.sample(recipe.CAVITY[0][0], recipe.CAVITY[0][1], warped) +
                                recipe.CAVITY[1][2]*noise.sample(recipe.CAVITY[1][0], recipe.CAVITY[1][1], warped)).astype(np.float32)
        chroma[z0:z0+len(z)] = noise.sample(recipe.CHROMA[0], recipe.CHROMA[1], warped).astype(np.float32)
    q70, q85 = np.percentile(cavity, recipe.CAVITY_QUANTILES)
    mask = 1.-smooth(q70, q85, cavity); eligible = mask > 0
    eligible_carrier = carrier[eligible]
    t1 = float(np.percentile(eligible_carrier, 99))
    q10h, q90h = np.percentile(chroma, recipe.CHROMA_QUANTILES)
    interpolation = smooth(q10h, q90h, chroma).astype(np.float32)
    return carrier, mask, eligible, t1, interpolation


def family_density(carrier, mask, eligible, t1, fraction):
    target = int(round(fraction*carrier.size)); count = int(eligible.sum())
    if count <= target:
        raise ValueError("insufficient eligible voxels")
    values = carrier[eligible]; order = count-target-1
    t0 = float(np.partition(values, order)[order])
    if not math.isfinite(t0) or t1 <= t0:
        raise ValueError("invalid support thresholds")
    raw = mask*smooth(t0, t1, carrier); occupied = raw > 0
    conditional = float(raw[occupied].mean()) if occupied.any() else 0.
    if not math.isfinite(conditional) or conditional <= 0:
        raise ValueError("invalid occupied conditional mean")
    density = (raw/conditional).astype(np.float32)
    if abs(float(occupied.mean())-fraction) > 1/carrier.size or abs(float(density[occupied].mean())-1) > 2e-6:
        raise ValueError("density normalization")
    return density


def colour_volume(density, interpolation, colours):
    segment = np.minimum((interpolation*3).astype(np.int64), 2)
    fraction = interpolation*3-segment
    chroma = colours[segment]*(1-fraction[..., None]) + colours[segment+1]*fraction[..., None]
    return np.concatenate([density[..., None]*chroma, density[..., None]], -1).astype(np.float32)


def atlas_from_volume(volume):
    if volume.shape != (recipe.GRID, recipe.GRID, recipe.GRID, 4) or not np.isfinite(volume).all() or np.any(volume < 0):
        raise ValueError("finite nonnegative weighted RGBA required")
    half = volume.astype('<f2')
    if not np.isfinite(half).all():
        raise ValueError("half overflow")
    atlas = np.zeros((recipe.ATLAS_HEIGHT, recipe.ATLAS_WIDTH, 4), '<f2')
    wrap = np.arange(-1, recipe.GRID+1) % recipe.GRID
    for z in range(recipe.GRID):
        oy, ox = (z//12)*130, (z%12)*130
        atlas[oy:oy+130, ox:ox+130] = half[z][np.ix_(wrap, wrap)]
    return atlas


def packetize(atlas: np.ndarray, profile: dict) -> tuple[bytes, dict]:
    decoded = atlas.tobytes(order='C')
    texels = np.frombuffer(decoded, dtype='V8')
    zero = texels == np.zeros((), dtype='V8')
    payload = bytearray(); runs = 0; nonzero = int((~zero).sum()); index = 0
    while index < len(texels):
        literal = not bool(zero[index]); end = index+1
        while end < len(texels) and bool(not zero[end]) == literal and end-index < 0x7fffffff:
            end += 1
        count = end-index
        payload += struct.pack('<I', count | (0x80000000 if literal else 0))
        if literal:
            payload += decoded[index*recipe.TEXEL_BYTES:end*recipe.TEXEL_BYTES]
        runs += 1; index = end
    checksum = fnv1a64(decoded)
    header = HEADER.pack(MAGIC, VERSION, HEADER.size, profile['id'], recipe.RECIPE_ID,
                         recipe.ATLAS_WIDTH, recipe.ATLAS_HEIGHT, recipe.TEXEL_BYTES,
                         len(decoded), runs, checksum, 0)
    return bytes(header+payload), dict(decoded_bytes=len(decoded), decoded_sha256=sha256(decoded),
                                       decoded_fnv1a=f'{checksum:016x}', nonzero_texels=nonzero,
                                       runs=runs, packet_bytes=len(payload), resource_bytes=len(header)+len(payload))


def metadata_header(rows):
    lines = ["// Generated by tools/build/bake_fog_fields.py; do not edit.",
             "#pragma once", "namespace x3m::renderer::fog_field {", "constexpr ProfileInfo kProfiles[] = {"]
    for row in rows:
        p = recipe.PROFILES[row['name']]
        lines.append(f"    {{Profile::{row['enum']}, {recipe.RECIPE_ID}u, {recipe.ATLAS_WIDTH}u, {recipe.ATLAS_HEIGHT}u, {recipe.TEXEL_BYTES}u, {row['decoded_bytes']}u, {p['base_sigma']:.9g}f, 0x{row['decoded_fnv1a']}ull, {p['resource_id']}u}},")
    lines += ["};", "} // namespace x3m::renderer::fog_field", ""]
    return '\n'.join(lines)


def resource_header():
    return ("// Generated by tools/build/bake_fog_fields.py; do not edit.\n#pragma once\n" +
            ''.join(f"#define X3M_FOG_{name.upper()}_RESOURCE_ID {profile['resource_id']}\n"
                    for name, profile in recipe.PROFILES.items()))


def resource_rows():
    # Relative names keep bakes byte-identical across output directories. Both
    # CMake and standalone windres add the generated directory to the RC path.
    return ("// Generated by tools/build/bake_fog_fields.py; do not edit.\n" +
            ''.join(f'X3M_FOG_{name.upper()}_RESOURCE_ID RCDATA "{name}.fogbin"\n'
                    for name in recipe.PROFILES))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--list-profiles', action='store_true')
    args = parser.parse_args()
    if args.list_profiles:
        print(';'.join(recipe.PROFILES))
        return 0
    if args.output_dir is None:
        parser.error('--output-dir is required when baking')
    if np.__version__ != recipe.REQUIRED_NUMPY:
        raise SystemExit(f'NumPy {recipe.REQUIRED_NUMPY} required, found {np.__version__}')
    started = time.monotonic(); args.output_dir.mkdir(parents=True, exist_ok=True)
    carrier, mask, eligible, t1, interpolation = bake_fields(); rows = []
    for name, profile in recipe.PROFILES.items():
        density = family_density(carrier, mask, eligible, t1, profile['occupancy'])
        atlas = atlas_from_volume(colour_volume(density, interpolation, profile['colours']))
        packet, row = packetize(atlas, profile)
        if row['decoded_sha256'] != profile['atlas_sha256']:
            raise SystemExit(f'{name} pinned atlas mismatch: {row["decoded_sha256"]}')
        (args.output_dir/f'{name}.fogbin').write_bytes(packet)
        rows.append(dict(name=name, enum=name[0].upper()+name[1:], profile_id=profile['id'],
                         resource_id=profile['resource_id'], base_sigma=profile['base_sigma'],
                         occupancy=profile['occupancy'],
                         density_status=profile.get('density_status', 'existing_capture_tuned'),
                         resource_sha256=sha256(packet), **row))
    metadata = metadata_header(rows); resources = resource_header(); rc_rows = resource_rows()
    (args.output_dir/'fog_field_assets_metadata_inc.h').write_text(metadata)
    (args.output_dir/'fog_field_assets_resource_inc.h').write_text(resources)
    (args.output_dir/'fog_field_assets_entries.rc').write_text(rc_rows)
    manifest = dict(schema=1, format='x3-fog-zero-literal-v1', numpy=np.__version__, prng=recipe.PRNG, recipe_id=recipe.RECIPE_ID,
                    period=recipe.PERIOD, grid=recipe.GRID, atlas=[recipe.ATLAS_WIDTH, recipe.ATLAS_HEIGHT],
                    seeds=list(range(186, 198)), periods=[2048, 4096, 8192],
                    baker_sha256=sha256(Path(__file__).read_bytes()),
                    recipe_sha256=sha256(Path(recipe.__file__).read_bytes()),
                    metadata_sha256=sha256(metadata.encode()), resource_header_sha256=sha256(resources.encode()),
                    resource_rows_sha256=sha256(rc_rows.encode()),
                    profiles=rows)
    (args.output_dir/'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True)+'\n')
    print(json.dumps(dict(output=str(args.output_dir), seconds=time.monotonic()-started,
                          packet_bytes=sum(r['resource_bytes'] for r in rows), profiles=len(rows)), sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
