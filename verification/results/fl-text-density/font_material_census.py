#!/usr/bin/env python3
"""Read-only census of the font requests and Materials text targets of X3FL (and clean AP for comparison).

Mounts the edition's catalogues (tools/analysis/sector_fog_census.Assets, catalogue_mounts order) and prints derived
facts only: the Fonts tables (family, size, y offset per language), the decoded SHA-256 of the stock f/ members (AP vs
FL), the loose f/ density pairs present, and the Materials row census the text-density row plan acts on
(MPF_GENERATED|MPF_WRITEABLE rows, FONTSCALE / NOFILTERING among them, file-backed FONTSCALE rows, largest dims).

usage: font_material_census.py [--fl /mnt/c/X3FL] [--ap /mnt/c/X3AP-orig] [--assets assets/fonts/generated/F]
"""
import argparse, hashlib, json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import sector_fog_census as sfc  # noqa: E402

MPF = dict(MPF_NULL=0, MPF_ALPHATEST=1, MPF_DESTINATIONBLEND=2, MPF_ALPHABLEND=4, MPF_WIREFRAME=8, MPF_2SIDED=0x10,
           MPF_TEXTURE_NOTSWAPABLE=0x20, MPF_MULTIPLY2X=0x40, MPF_MULTIPLY=0x80, MPF_NOFILTERING=0x100,
           MPF_SRCCOLOR=0x400, MPF_ENVMAP=0x1000, MPF_BUMPMAP=0x2000, MPF_LIGHTMAP=0x4000, MPF_BESTQUALITY=0x8000,
           MPF_FONTSCALE=0x10000, MPF_READABLE=0x20000, MPF_WRITEABLE=0x40000, MPF_AUTOFREE=0x80000,
           MPF_RADIOSITY=0x100000, MPF_TEXTUREALPHA=0x200000, MPF_IMPORTPICTURE=0x400000, MPF_GENERATED=0x800000,
           MPF_XBOX_NOCOMPRESS=0x1000000, MPF_USEFXSHADER=0x2000000, MPF_HAZE=0x4000000)
GEN, WR, FS, NOF = 0x800000, 0x40000, 0x10000, 0x100
DENSITY_FAMILIES = (('Tahoma', 13), ('Zekton', 26), ('ZektonES', 26), ('Harrier', 24))


def fonts_tables(a, prefix):
    out = {}
    for k in sorted(a.entries, key=str):
        ks = str(k)
        if ks.startswith(prefix + 'types/fonts') and ks.endswith('.txt'):
            text = a.get(k)[0].decode('latin1')
            rows = [l.strip().rstrip(';').split(';') for l in text.splitlines()
                    if l.strip() and not l.lstrip().startswith('#')][1:]
            out[ks] = [{'role': r[0], 'family': r[1], 'size': int(r[2]), 'yoff': int(r[3])} for r in rows]
    return out


def materials(a, key):
    data = a.get(key)[0]
    lines = [l for l in data.decode('latin1').splitlines() if l.strip() and not l.lstrip().startswith('/')]
    count = int(lines[0].split(';')[0])
    rows = []
    for i, line in enumerate(lines[1:count + 1]):
        f = [x.strip() for x in line.split(';')]
        flags = 0
        for tok in (t.strip() for t in f[15].split('|')):
            flags |= MPF.get(tok, 0) if not tok[:1].isdigit() else int(tok, 0)
        rows.append((i, flags, int(f[16], 0), int(f[17], 0)))
    gw = [r for r in rows if r[1] & GEN and r[1] & WR]
    return {'member': key, 'source': a.entries[key][-1]['source'], 'rows': count, 'parsed': len(rows),
            'generated_writeable': len(gw), 'gw_ids': [r[0] for r in gw],
            'gw_already_fontscale': sum(1 for r in gw if r[1] & FS),
            'gw_nofiltering': sum(1 for r in gw if r[1] & NOF),
            'file_backed_fontscale': [r[0] for r in rows if r[1] & FS and not r[1] & GEN],
            'gw_max_width': max(r[2] for r in gw), 'gw_max_height': max(r[3] for r in gw),
            'gw_max_height_ids': [r[0] for r in gw if r[3] == max(x[3] for x in gw)]}


def font_members(a):
    out = {}
    for k in sorted(a.entries, key=str):
        ks = str(k)
        if ks.startswith('f/') and ks.endswith(('.abc', '.tga', '.bmp')):
            out[ks] = {'source': a.entries[k][-1]['source'], 'sha256': hashlib.sha256(a.get(k)[0]).hexdigest()}
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--fl', type=Path, default=Path('/mnt/c/X3FL'))
    p.add_argument('--ap', type=Path, default=Path('/mnt/c/X3AP-orig'))
    p.add_argument('--assets', type=Path, default=ROOT / 'assets/fonts/generated/F')
    args = p.parse_args()
    fl = sfc.Assets(args.fl, edition='fl')
    ap = sfc.Assets(args.ap, edition='ap')
    report = {'fl': {'fonts_tables': fonts_tables(fl, 'addon2/'), 'fonts_tables_root': fonts_tables(fl, ''),
                     'materials': materials(fl, 'addon2/types/materials.txt'),
                     'loose_f_dir': (args.fl / 'f').is_dir()},
              'ap': {'fonts_tables': fonts_tables(ap, 'addon/') or fonts_tables(ap, ''),
                     'materials': materials(ap, next(k for k in ('addon/types/materials.txt', 'types/materials.txt')
                                                     if k in ap.entries))}}
    flm, apm = font_members(fl), font_members(ap)
    report['stock_f_members'] = {k: {'fl': flm.get(k, {}).get('sha256', '-')[:16], 'ap': apm.get(k, {}).get('sha256', '-')[:16],
                                     'fl_source': flm.get(k, {}).get('source')} for k in sorted(set(flm) | set(apm))}
    report['stock_f_identical'] = sum(1 for k in set(flm) & set(apm) if flm[k]['sha256'] == apm[k]['sha256'])
    shipped = sorted(x.name for x in args.assets.iterdir()) if args.assets.is_dir() else []
    requests = sorted({(r['family'], r['size']) for t in report['fl']['fonts_tables'].values() for r in t} | {('Tahoma', 13)})
    report['fl_requests'] = [f'{n}{s}' for n, s in requests]
    report['shipped'] = shipped
    report['missing_for_fl'] = {d: [f'{n}{s * d}' for n, s in requests
                                    if not {f'{n}{s * d}.abc', f'{n}{s * d}.tga'} <= set(shipped)] for d in (2, 3)}
    print(json.dumps(report, indent=1))


if __name__ == '__main__':
    main()
