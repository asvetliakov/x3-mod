"""Decode a body's overlay atlases (and optionally named source textures) from the FL mounts; print channel
means per slot and per atlas tile, save PNG previews to OUTDIR (scratch, untracked).
Usage: X3M_EDITION=fl atlas_stats.py GAME MARKER_JSON BODYNAME OUTDIR"""
import sys, json, io
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools/analysis'))
import numpy as np
from PIL import Image
import bob1
game, marker, name, out = Path(sys.argv[1]), sys.argv[2], sys.argv[3], Path(sys.argv[4])
out.mkdir(parents=True, exist_ok=True)
sfc = bob1._archive_modules()
assets = sfc.Assets(game)
body = next(b for b in json.load(open(marker))['bodies'] if b['name'] == name)
def entry(key):
    for k in (key, 'addon2/' + key):
        es = [e for e in assets.entries.get(k, []) if bob1.mounted(assets, e)]
        if es:
            return max(es, key=lambda e: bob1.mount_key(assets, e['source']))
    raise KeyError(key)
def decode(key):
    e = entry(key)
    im = Image.open(io.BytesIO(assets.read_entry(e)))
    im.load()
    return e['source'], np.asarray(im.convert('RGBA')).astype(float)
for t in body['atlas']['textures']:
    src, a = decode(t['member'].lower().replace('.pck', '.dds'))
    print('%-8s %s %s %dx%d mean RGBA=%s' % (t['slot'], src, t['format'], a.shape[1], a.shape[0], np.round(a.reshape(-1, 4).mean(0), 1)))
    Image.fromarray(a[:, :, :3].astype(np.uint8)).save(out / ('%s_rgb.png' % t['slot']))
    if t['slot'] == 'diffuse':
        for tile in body['atlas']['tiles']:
            x, y = tile['origin']; w, h = tile['content']
            sc = a.shape[0] / tile['base'][1] if 'base' in tile else 1
            sub = a[y:y + h, x:x + w, :3]
            print('   tile mats=%s share=%.3f origin=%s content=%s mean RGB=%s src=%s' % (
                tile['mats'], tile['share'], tile['origin'], tile['content'], np.round(sub.reshape(-1, 3).mean(0), 1), tile['sources']['diffuse']))
