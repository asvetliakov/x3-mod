#!/usr/bin/env python3
"""Real-asset proof that the fog family bake works on Farnham's Legacy (writes summary.txt beside itself).

Builds a scratch FL root under SCRATCH: X3FL.exe and every stock catalogue (root NN.cat/.dat, addon2/NN.cat/.dat)
as symlinks into the FL install (read only), plus addon2/08.cat/.dat written with sector_fog_census.write_catalogue
(the writer lod_overlay.py uses) holding two Mayhem fog families copied from the AP install: their TBackgrounds rows
merged into a copy of FL's addon2/types/TBackgrounds, their dust bodies and their nebula textures, each resolved
through sector_fog_census.Assets on the AP install (stored bytes copied as stored; nothing copyrighted is tracked,
only names, counts and hashes go into summary.txt).

Then runs tools/analysis/fog_families.py --game SCRATCH --install --replace and --check, compares each baked
family's decoded atlas hash with the AP install's record, checks that every stock FL dust sector's family
(verification/results/fl-port/sector-background/fogged_sectors.txt) stays covered_by_build, and runs
tools/regenerate/x3m_regenerate.py's run() on SCRATCH with the LOD overlay step stubbed (a full FL LOD bake on the
symlinked stock root would take hours and write overlay catalogues; the fog steps, the engine-body step and the
fog check run for real).

Usage: python3 synthetic_fl_mod.py SCRATCH [--fl /mnt/c/X3FL] [--ap /mnt/c/X3] [--family greenspot2 deepred2]
Never writes into the FL or AP install.
"""
import argparse
import contextlib
import gzip
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from unittest import mock

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
for _p in (ROOT / 'tools' / 'analysis', ROOT / 'tools' / 'regenerate', ROOT / 'verification' / 'probe'):
    sys.path.insert(0, str(_p))
import catalogue_mounts  # noqa: E402
import sector_fog_census as sfc  # noqa: E402

FOG_TOOL = ROOT / 'tools' / 'analysis' / 'fog_families.py'
FOGGED = ROOT / 'verification' / 'results' / 'fl-port' / 'sector-background' / 'fogged_sectors.txt'
NEBULA = 'objects/environments/nebulae'


def stored_bytes(assets, path):
    """The winning entry's bytes as stored in its catalogue (still packed), for a faithful copy."""
    entry = assets.candidates(path)[-1]
    if 'loose' in entry:
        return entry['loose'].read_bytes(), entry['path']
    with entry['cat'].with_suffix('.dat').open('rb') as stream:
        stream.seek(entry['offset'])
        raw = stream.read(entry['size'])
    return bytes(v ^ 0x33 for v in raw), entry['path']


def table_lines(data):
    lines = [line.strip() for line in data.decode('utf-8-sig').splitlines()
             if line.strip() and not line.lstrip().startswith('//')]
    return lines[0], lines[1:]


def build_root(scratch, fl, ap, families):
    if scratch.exists():
        shutil.rmtree(scratch)
    (scratch / 'addon2').mkdir(parents=True)
    os.symlink(fl / 'X3FL.exe', scratch / 'X3FL.exe')
    linked = 0
    for directory in ('', 'addon2'):
        for cat in catalogue_mounts.numbered_run(fl / directory):
            for suffix in ('.cat', '.dat'):
                os.symlink(cat.with_suffix(suffix), scratch / directory / cat.with_suffix(suffix).name)
                linked += 1
    ap_assets = sfc.Assets(ap, edition='ap')
    fl_assets = sfc.Assets(fl, edition='fl')
    import fog_families as ff
    ap_table, ap_source = ap_assets.logical('types/tbackgrounds', ('.pck', '.txt'))
    fl_table, fl_source = fl_assets.logical('types/tbackgrounds', ('.pck', '.txt'))
    _, ap_rows = table_lines(ap_table)
    fl_head, fl_rows = table_lines(fl_table)
    fl_names = {row['family'] for row in sfc.backgrounds(fl_table)}
    grouped = {}
    for row in sfc.backgrounds(ap_table):
        grouped.setdefault(row['family'], []).append(row)
    members, copied = [], []
    added_rows = []
    for name in families:
        if name in fl_names:
            raise SystemExit(f'{name} already exists in FL TBackgrounds; pick another family')
        records = [r for r in grouped.get(name, []) if r['dust'] > 0]
        if not records:
            raise SystemExit(f'{name}: no positive-dust AP TBackgrounds row')
        added_rows += [ap_rows[r['index']] for r in records]
        plan = ff.plan_family(ap_assets, name, records, set(), {})
        if plan['status'] != 'ok':
            raise SystemExit(f'{name}: AP plan {plan["status"]}/{plan["reason"]}')
        paths = [p['member'] for p in plan['parts'] if p.get('member')] + sorted({t['member'] for t in plan['textures']})
        for path in paths:
            data, member = stored_bytes(ap_assets, path)
            archive = 'addon2/' + sfc.canonical(member).removeprefix('addon/')
            if archive not in {m for m, _ in members}:
                members.append((archive, data))
        copied.append(dict(name=name, records=len(records), parts=sum(1 for p in plan['parts'] if p.get('member')),
                           textures=len({t['member'] for t in plan['textures']})))
    version, _count, *_ = fl_head.split(';')
    rows = fl_rows + added_rows
    table = (f'{version};{len(rows)};\n' + '\n'.join(rows) + '\n').encode()
    members.insert(0, ('addon2/types/TBackgrounds.pck', gzip.compress(table, mtime=0)))
    number = len(catalogue_mounts.numbered_run(scratch / 'addon2')) + 1
    cat = scratch / 'addon2' / f'{number:02d}.cat'
    sfc.write_catalogue(cat, members)
    return dict(linked=linked, cat=cat.relative_to(scratch).as_posix(), members=len(members),
                dat_bytes=cat.with_suffix('.dat').stat().st_size, fl_rows=len(fl_rows), added_rows=len(added_rows),
                fl_tbackgrounds=fl_source['source'], ap_tbackgrounds=ap_source['source'], copied=copied)


def tool(*argv):
    done = subprocess.run([sys.executable, str(FOG_TOOL), *map(str, argv)], capture_output=True, text=True)
    return done.returncode, done.stdout + done.stderr


def regenerate(scratch):
    import x3m_regenerate as regen

    def stub_lod(log, game, *args, **kwargs):
        log.say('LOD overlay: stubbed by synthetic_fl_mod.py (not part of the fog proof)')
    out = io.StringIO()
    with mock.patch.object(regen, 'lod_step', stub_lod), mock.patch.object(regen, 'running_game', return_value=[]), \
            contextlib.redirect_stdout(out):
        code = regen.main(['--game-dir', str(scratch), '--no-wait'])
    return code, out.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('scratch', type=Path)
    parser.add_argument('--fl', type=Path, default=Path('/mnt/c/X3FL'))
    parser.add_argument('--ap', type=Path, default=Path('/mnt/c/X3'))
    parser.add_argument('--family', nargs=2, default=['greenspot2', 'deepred2'])
    args = parser.parse_args()
    scratch = args.scratch.resolve()
    for install in (args.fl.resolve(), args.ap.resolve()):
        if scratch == install or scratch.is_relative_to(install):
            raise SystemExit('SCRATCH must be outside the game installs')
    built = build_root(scratch, args.fl.resolve(), args.ap.resolve(), args.family)
    lines = [f'scratch FL root: {built["linked"]} stock catalogue files symlinked from {args.fl}; {built["cat"]} written '
             f'({built["members"]} members, {built["dat_bytes"]} B .dat): TBackgrounds = FL {built["fl_tbackgrounds"]} '
             f'({built["fl_rows"]} rows) + {built["added_rows"]} AP rows from {built["ap_tbackgrounds"]}']
    lines += [f'  copied {c["name"]}: {c["records"]} records, {c["parts"]} dust parts, {c["textures"]} textures' for c in built['copied']]
    lines.append(f'edition detected: {catalogue_mounts.edition(scratch)}')
    code, text = tool('--game', scratch, '--install', '--replace', '--jobs', '2')
    lines.append(f'fog_families.py --install --replace: exit {code}; {text.strip().splitlines()[-1]}')
    code, text = tool('--game', scratch, '--check')
    lines.append(f'fog_families.py --check: exit {code}; {text.strip().splitlines()[0]}')
    record = json.loads((scratch / 'x3m' / 'fog-families.json').read_text())
    ap_record = json.loads((args.ap / 'x3m' / 'fog-families.json').read_text())
    ap_by_name = {f['name']: f for f in ap_record['families']}
    lines.append(f'record: edition={record["edition"]} counts={json.dumps(record["counts"], sort_keys=True)} '
                 f'file={record["file"]["bytes"]} B families={record["file"]["families"]} packets={record["file"]["packets"]} '
                 f'sha256={record["file"]["sha256"]}')
    lines.append(f'layers: {", ".join(record["catalogue_layers"])}')
    baked = [f for f in record['families'] if f.get('packet') is not None]
    lines.append(f'families baked = {len(baked)}')
    for f in baked:
        ap_f = ap_by_name.get(f['name'], {})
        same = ap_f.get('decoded_sha256') == f['decoded_sha256']
        lines.append(f'  {f["name"]}: profile_id={f["profile_id"]:#010x} packet={f["packet"]} packet_bytes={f["packet_bytes"]} '
                     f'decoded_sha256={f["decoded_sha256"][:16]} nonzero_texels={f["nonzero_texels"]} '
                     f'parts_from={sorted({p["source"] for p in f["parts"] if p.get("source")})} '
                     f'same_atlas_as_AP_install={same} (AP profile_id={ap_f.get("profile_id", 0):#010x})')
    status = {f['name']: f['status'] for f in record['families']}
    fogged = re.findall(r'family=(\S+)', FOGGED.read_text())
    sector_status = {}
    for family in fogged:
        sector_status[status.get(family, 'absent')] = sector_status.get(status.get(family, 'absent'), 0) + 1
    lines.append(f'FL stock dust sectors ({FOGGED.relative_to(ROOT)}): {len(fogged)}, by family status '
                 f'{json.dumps(sector_status, sort_keys=True)}; families {sorted(set(fogged))}')
    code, text = regenerate(scratch)
    lines.append(f'x3m_regenerate run (LOD overlay stubbed): exit {code}')
    keep = ('edition:', '== fog', 'fog ', 'processing fog', 'refused fog', 'LOD overlay', 'all done', 'FAILED')
    for line in text.splitlines():
        body = line.split('] ', 1)[1] if line.startswith('[') and '] ' in line else line
        if body.startswith(keep):
            lines.append('  ' + body)
    summary = '\n'.join(lines) + '\n'
    (HERE / 'summary.txt').write_text(summary)
    print(summary, end='')


if __name__ == '__main__':
    main()
