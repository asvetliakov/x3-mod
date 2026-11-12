#!/usr/bin/env python3
"""Far-caster penumbra fixture (docs/architecture/shadow-far-caster-penumbra.md) on the native Windows host.

Builds motion_output_fixture.exe twice from this checkout: `tree` (the working tree's ShadowReplayPass) and `base`
(the same fixture sources compiled with X3M_FIXTURE_PENUMBRA_BASE against src/renderer/shadow_replay_pass.{h,cpp} of
--base, the pass before the change), runs both on the Windows side through WSL interop (native_host.py; the real
D3D9 runtime and GPU driver, never the game, no proxy DLL beside the fixture) in two modes:

- shadowalpha: the existing alpha-caster script; every ALPHA_MAP hash of tree must equal base's (the near maps
  bit-identical before/after), and the script's own checks pass in both.
- shadowpenumbra: a honeycomb-grille alpha plate (pitch 83 u, 16 % open) over a flat receiver at 500 / 3,000 / 4,000 /
  4,500 / 5,000 / 6,000 / 8,000 / 10,000 / 50,000 / 137,000 u toward the light in the texels of the flown cascades 1-3, GREATEREQUAL
  ref 1 and 128.
  Acceptance: (a) the 500 u maps hash-identical to base's (the dead zone); (b) each tree map's hole share within
  0.02 (absolute) of this runner's prediction from the texture's mip chain at the biased level (D3D9 trilinear
  sampling emulated on the host); (c) per ref and cascade the share non-increasing with distance and 0 at 137,000 u;
  (d) at every distance past the last map's bias edge (7,569 u: 2,000 + 2^1.5 texel / 0.0093; 50,000 and 137,000 u
  among them) the three cascades' shares within 0.02 of each other. Recorded: the base (unbiased) shares, the partial
  holes' size at 3,000-10,000 u (connected components of the hole bitmap), and the replay's
  GPU time per transaction (timestamp queries; flown map sizes 4,096^2 x 4 + 2,048^2, every map fully covered by
  the alpha plate) for both builds.

Writes verification/results/native-windows/shadow-penumbra/summary.json; builds, logs and hole bitmaps stay under
verification/probe/build/shadow-penumbra/ (untracked).

  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_shadow_penumbra.py --host native
"""
import argparse
import hashlib
import json
import math
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
sys.path.insert(0, str(PROBE))
import native_host  # noqa: E402
from game_guard import game_running  # noqa: E402

BUILD = PROBE / 'build/shadow-penumbra'
PASS_SOURCES = ('src/renderer/shadow_replay_pass.h', 'src/renderer/shadow_replay_pass.cpp')
PRODUCTION_SOURCES = PASS_SOURCES + ('src/renderer/shadow_replay_projection.h', 'src/proxy/motion_output_shadow_replay_inc.h')
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse', '-mstackrealign',
         '-mincoming-stack-boundary=2', '-DX3M_QUAD_FVF_SWITCH', '-static', '-static-libgcc', '-static-libstdc++']
LINKED = ['src/renderer/temporal_pass.cpp', 'src/renderer/sun_shadow_apply_pass.cpp', 'src/renderer/shadow_replay_pass.cpp',
          'src/renderer/linear_material.cpp', 'src/renderer/material_motion.cpp']
# The reviewed captured pair the fixture insists on (run_motion_output.py RAW); local, never tracked.
SHADERS = {'vs_53a0a641107ed76c.bin': 'bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c',
           'ps_8759c7838bbc86c2.bin': '9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0'}
SHADER_DIRS = (Path('/mnt/c/X3FL/x3-modern-captures'), Path('/mnt/c/X3/x3-modern-captures'), Path('/tmp/x3-shader-sweep/programs'))

# The scene (motion_output_shadow_alpha_inc.h, run_shadow_penumbra); the runner rebuilds the texture to predict.
TEX, LEVELS, PITCH, HEX = 256, 9, 83., .231 * 83.
MAP = 1024
FLOWN = (1500., 7500., 37500.)
DISTANCES = (500, 3000, 4000, 4500, 5000, 6000, 8000, 10000, 50000, 137000)
PARTIAL = (3000, 4000, 4500, 6000, 8000, 10000)  # the partial holes' size is recorded at these
SUPPORT = 1.5  # mips: shadow_penumbra_support (shadow_replay_projection.h)
# Beyond the last map's bias edge (2,000 + 2^1.5 texel / 0.0093 for 18.3 u: 7,569 u) every cascade tests one footprint.
AGREE_FROM = 2000 + 2 ** SUPPORT * (2 * FLOWN[-1] / 4096) / .0093
REFS = (1, 128)
TOLERANCE = .02


def fields(line):
    return dict(re.findall(r'(\w+)=(\S+)', line))


def git(*a):
    return subprocess.run(['git', '-C', str(ROOT), *a], capture_output=True, text=True, check=True).stdout


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def shaders():
    out = []
    for name, digest in SHADERS.items():
        for directory in SHADER_DIRS:
            path = directory / name
            if path.is_file() and sha(path) == digest:
                out.append(path)
                break
        else:
            raise SystemExit(f'the reviewed captured shader {name} (sha256 {digest[:12]}) is not in {", ".join(map(str, SHADER_DIRS))}')
    return out


def build(base_rev):
    """The tree and base fixture executables."""
    BUILD.mkdir(parents=True, exist_ok=True)
    tree = BUILD / 'fixture_tree.exe'
    subprocess.run(['i686-w64-mingw32-g++', *FLAGS, 'motion_output_fixture.cpp', *[f'../../{s}' for s in LINKED], '-o', str(tree),
                    '-ldxguid', '-luser32'], cwd=PROBE, check=True)
    # The base tree: this checkout's src/ and fixture sources, the pass of `base_rev`.
    root = BUILD / 'base-tree'
    if root.exists():
        shutil.rmtree(root)
    shutil.copytree(ROOT / 'src', root / 'src')
    (root / 'verification/probe').mkdir(parents=True)
    for path in PROBE.iterdir():
        if path.is_file() and path.suffix in ('.h', '.cpp'):
            shutil.copy2(path, root / 'verification/probe' / path.name)
    for source in PASS_SOURCES:
        (root / source).write_text(git('show', f'{base_rev}:{source}'))
    base = BUILD / 'fixture_base.exe'
    subprocess.run(['i686-w64-mingw32-g++', *FLAGS, '-DX3M_FIXTURE_PENUMBRA_BASE', 'motion_output_fixture.cpp',
                    *[f'../../{s}' for s in LINKED], '-o', str(base), '-ldxguid', '-luser32'], cwd=root / 'verification/probe', check=True)
    return {'tree': tree, 'base': base}


def run(exe, mode, tag, timeout):
    work = BUILD / f'{tag}-{mode}'
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    vs, ps = (native_host.windows_path(p) for p in shaders())
    t0 = time.time()
    done, text = native_host.run_native(exe, [vs, ps, mode], work, timeout, env={'X3M_MOTION_OUTPUT': '0'}, log=work / 'fixture')
    seconds = time.time() - t0
    passed = done.returncode == 0 and 'RESULT PASS' in text
    return work, text, passed, seconds


# ---- the host prediction: the texture's chain sampled as D3D9's trilinear filter does ----

def chain():
    import numpy as np
    n, r3 = TEX, math.sqrt(3.)
    w, h = 3 * PITCH, 2 * r3 * PITCH
    px = (np.arange(n) + .5) / n * w
    py = (np.arange(n) + .5) / n * h
    X, Y = np.meshgrid(px, py)
    open_ = np.zeros((n, n), bool)
    for j in range(-1, 3):
        for i in range(-1, 4):
            for odd in (0, 1):
                cx, cy = (i + .5 * odd) * PITCH, (j + .5 * odd) * r3 * PITCH
                q, r = np.abs(X - cx), np.abs(Y - cy)
                open_ |= (r <= r3 * .5 * HEX) & (r3 * q + r <= r3 * HEX)
    levels = [np.where(open_, 0, 255).astype(np.uint32)]
    for _ in range(1, LEVELS):
        a = levels[-1]
        levels.append(((a[0::2, 0::2] + a[0::2, 1::2] + a[1::2, 0::2] + a[1::2, 1::2] + 2) // 4).astype(np.uint32))
    digest = 14695981039346656037
    for level in levels:
        for b in level.astype(np.uint8).tobytes():
            digest = ((digest ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return levels, f'{digest:016x}'


def bias(distance, texel):
    p = .0093 * max(0., distance - 2000.) / texel
    return min(max(0., math.log2(max(1., p)) - SUPPORT), 15.)


def predict(levels, texel, extent, lod_bias, ref):
    """The hole bitmap the mip chain predicts on a MAP^2 map: texel (i, j) samples NDC (2i/MAP - 1, 1 - 2j/MAP)
    (D3D9 pixel centres), UV = world position over the tile, WRAP, LOD = log2 of the larger UV derivative in texels
    plus the bias, trilinear between the two levels (bilinear in each), killed where alpha < ref / 255."""
    import numpy as np
    w, h = 3 * PITCH, 2 * math.sqrt(3.) * PITCH
    idx = np.arange(MAP)
    x = (2. * idx / MAP - 1.) * extent
    y = (1. - 2. * idx / MAP) * extent
    u = x / w
    v = y / h
    rho = max(texel / w * TEX, texel / h * TEX)
    lod = math.log2(rho) + lod_bias

    def bilinear(level):
        a = levels[level].astype(np.float64) / 255.
        n = a.shape[0]
        s = u * n - .5
        t = v * n - .5
        s0 = np.floor(s); fs = s - s0
        t0 = np.floor(t); ft = t - t0
        s0 = s0.astype(np.int64) % n; s1 = (s0 + 1) % n
        t0 = t0.astype(np.int64) % n; t1 = (t0 + 1) % n
        top = a[np.ix_(t0, s0)] * (1 - fs)[None, :] + a[np.ix_(t0, s1)] * fs[None, :]
        bottom = a[np.ix_(t1, s0)] * (1 - fs)[None, :] + a[np.ix_(t1, s1)] * fs[None, :]
        return top * (1 - ft)[:, None] + bottom * ft[:, None]

    if lod <= 0.:
        alpha = bilinear(0)
    else:
        lod = min(lod, LEVELS - 1.)
        l0 = int(math.floor(lod)); f = lod - l0
        alpha = bilinear(l0) if f == 0. or l0 == LEVELS - 1 else bilinear(l0) * (1 - f) + bilinear(l0 + 1) * f
    return alpha < ref / 255., lod


def load_bits(path):
    import numpy as np
    return np.unpackbits(np.fromfile(path, np.uint8))[:MAP * MAP].reshape(MAP, MAP).astype(bool)


def components(holes):
    """Connected hole regions (4-neighbour) of a bitmap: their areas in texels."""
    import numpy as np
    label = np.zeros(holes.shape, np.int32)
    areas = []
    ys, xs = np.nonzero(holes)
    for y0, x0 in zip(ys.tolist(), xs.tolist()):
        if label[y0, x0]:
            continue
        areas.append(0)
        mark = len(areas)
        label[y0, x0] = mark
        stack = [(y0, x0)]
        while stack:
            y, x = stack.pop()
            areas[-1] += 1
            for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if 0 <= ny < MAP and 0 <= nx < MAP and holes[ny, nx] and not label[ny, nx]:
                    label[ny, nx] = mark
                    stack.append((ny, nx))
    return areas


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--host', choices=('native',), required=True, help='native: the Windows side of this WSL2 host (native_host.py)')
    parser.add_argument('--base', default='HEAD', help='revision whose ShadowReplayPass is the "before" build (default HEAD)')
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if native_host.missing():
        raise SystemExit('--host native needs ' + '; '.join(native_host.missing()))
    if game_running() or native_host.game_running_windows():
        raise SystemExit('the game is running')
    import numpy as np
    started = time.time()
    base_rev = git('rev-parse', args.base).strip()
    exes = build(base_rev)
    runs, texts = {}, {}
    for tag in ('base', 'tree'):
        for mode in ('shadowalpha', 'shadowpenumbra'):
            work, text, passed, seconds = run(exes[tag], mode, tag, args.timeout)
            runs[(tag, mode)] = {'passed': passed, 'seconds': round(seconds, 1), 'work': work}
            texts[(tag, mode)] = text
            print(f'{tag} {mode}: {"PASS" if passed else "FAIL"} {seconds:.1f} s')
    failures = []
    for key, r in runs.items():
        if not r['passed']:
            failures.append(f'{key[0]} {key[1]}: fixture failed')
    # Near maps: every shadowalpha ALPHA_MAP hash of tree equals base's.
    def alpha_maps(text):
        return {f['label']: f['hash'] for f in (fields(l) for l in text.splitlines() if l.startswith('ALPHA_MAP '))}
    near_base, near_tree = alpha_maps(texts[('base', 'shadowalpha')]), alpha_maps(texts[('tree', 'shadowalpha')])
    near = {label: {'base': near_base.get(label), 'tree': h, 'identical': near_base.get(label) == h} for label, h in near_tree.items()}
    if not near or set(near_base) != set(near_tree) or not all(v['identical'] for v in near.values()):
        failures.append('shadowalpha maps differ between base and tree')
    # Penumbra maps.
    levels, chain_hash = chain()
    def penumbra(text):
        return {(int(f['ref']), int(f['distance']), int(f['cascade'])): f for f in (fields(l) for l in text.splitlines() if l.startswith('PENUMBRA_MAP '))}
    maps_base, maps_tree = penumbra(texts[('base', 'shadowpenumbra')]), penumbra(texts[('tree', 'shadowpenumbra')])
    textures = [fields(l) for l in texts[('tree', 'shadowpenumbra')].splitlines() if l.startswith('PENUMBRA_TEXTURE ')]
    if not textures or textures[0].get('chain_hash') != chain_hash:
        failures.append(f'texture chain differs from the host rebuild ({textures[0].get("chain_hash") if textures else None} != {chain_hash})')
    rows, partial = [], {}
    for ref in REFS:
        for k in (1, 2, 3):
            shares = []
            for d in DISTANCES:
                key = (ref, d, k)
                t, b = maps_tree.get(key), maps_base.get(key)
                if not t or not b:
                    failures.append(f'missing map {key}')
                    continue
                texel, extent = float(t['texel']), float(t['extent'])
                lod_bias = bias(d, texel)
                holes = load_bits(runs[('tree', 'shadowpenumbra')]['work'] / t['bits'])
                predicted, lod = predict(levels, texel, extent, lod_bias, ref)
                base_holes = load_bits(runs[('base', 'shadowpenumbra')]['work'] / b['bits'])
                predicted_base, lod_base = predict(levels, texel, extent, 0., ref)
                share, share_p = float(holes.mean()), float(predicted.mean())
                row = {'ref': ref, 'distance': d, 'cascade': k, 'texel': round(texel, 6), 'bias': round(lod_bias, 4), 'lod': round(lod, 4),
                       'share': round(share, 5), 'predicted': round(share_p, 5), 'texel_agreement': round(float((holes == predicted).mean()), 5),
                       'share_base': round(float(base_holes.mean()), 5), 'predicted_base': round(float(predicted_base.mean()), 5),
                       'hash': t['hash'], 'hash_base': b['hash'], 'identical_to_base': t['hash'] == b['hash']}
                rows.append(row)
                shares.append(share)
                if abs(share - share_p) > TOLERANCE:
                    failures.append(f'{key}: share {share:.4f} vs predicted {share_p:.4f}')
                if d == 500 and t['hash'] != b['hash']:
                    failures.append(f'{key}: the dead-zone map differs from the unbiased program')
                if d in PARTIAL and holes.any():
                    areas = components(holes)
                    a = sorted(areas)
                    partial[f'r{ref}_d{d}_c{k}'] = {'regions': len(a), 'median_texels': a[len(a) // 2], 'max_texels': a[-1],
                                                     'median_diameter_u': round(2 * math.sqrt(a[len(a) // 2] / math.pi) * texel, 2)}
                elif d in PARTIAL:
                    partial[f'r{ref}_d{d}_c{k}'] = {'regions': 0}
            if any(b > a + 1e-9 for a, b in zip(shares, shares[1:])):
                failures.append(f'ref {ref} cascade {k}: shares not monotone {shares}')
            if shares and shares[-1] != 0.:
                failures.append(f'ref {ref} cascade {k}: share at 137,000 u is {shares[-1]}')
        for d in (d for d in DISTANCES if d >= AGREE_FROM):
            s = [r['share'] for r in rows if r['ref'] == ref and r['distance'] == d]
            if s and max(s) - min(s) > TOLERANCE:
                failures.append(f'ref {ref} distance {d}: cascades disagree {s}')
    calls = [fields(l) for l in texts[('tree', 'shadowpenumbra')].splitlines() if l.startswith('PENUMBRA_CALLS ')]
    gpu = {tag: {int(f['distance']): {k: f[k] for k in ('us_median', 'us_p10', 'us_p90', 'samples', 'disjoint')}
                 for f in (fields(l) for l in texts[(tag, 'shadowpenumbra')].splitlines() if l.startswith('PENUMBRA_GPU '))}
           for tag in ('base', 'tree')}
    changed = [line[3:] for line in git('status', '--porcelain', '--', *PRODUCTION_SOURCES).splitlines()]
    dirty = git('status', '--porcelain', '--untracked-files=no').strip()
    record = {
        'fixture': 'shadow-penumbra', 'note': 'docs/architecture/shadow-far-caster-penumbra.md',
        'commit': git('rev-parse', 'HEAD').strip() + ('-dirty' if dirty else ''), 'base': base_rev,
        'production_sources_dirty': bool(changed), 'dirty_paths': changed,
        'sha256': {p: sha(ROOT / p) for p in PRODUCTION_SOURCES},
        'host': native_host.identity(texts[('tree', 'shadowpenumbra')]),
        'runs': {f'{t} {m}': {'passed': r['passed'], 'seconds': r['seconds']} for (t, m), r in runs.items()},
        'texture': {'size': TEX, 'levels': LEVELS, 'pitch': PITCH, 'hex_radius': HEX, 'chain_hash': chain_hash,
                    'open_share': float(textures[0]['open_share']) if textures else None},
        'tolerance_share_abs': TOLERANCE,
        'near_maps': near,
        'penumbra_maps': rows,
        'partial_holes': partial,
        'calls_tree': calls[:1],
        'gpu_us_per_transaction': gpu,
        'failures': failures,
        'result': 'FAIL' if failures else 'PASS',
        'wall_seconds': round(time.time() - started, 1)}
    results = native_host.results_dir(ROOT, 'shadow-penumbra')
    (results / 'summary.json').write_text(json.dumps(record, indent=1) + '\n')
    print(f'{"ref":>3} {"dist":>6} {"c":>1} {"bias":>6} {"share":>7} {"pred":>7} {"agree":>7} {"base":>7} same_as_base')
    for r in rows:
        print(f'{r["ref"]:>3} {r["distance"]:>6} {r["cascade"]:>1} {r["bias"]:>6.2f} {r["share"]:>7.4f} {r["predicted"]:>7.4f} '
              f'{r["texel_agreement"]:>7.4f} {r["share_base"]:>7.4f} {int(r["identical_to_base"])}')
    print('near maps identical:', all(v['identical'] for v in near.values()), len(near))
    print('gpu us/transaction:', json.dumps(gpu))
    print('partial holes:', json.dumps(partial))
    for f in failures:
        print('FAIL', f)
    print('RESULT', record['result'], results / 'summary.json')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
