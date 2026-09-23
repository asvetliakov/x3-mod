#!/usr/bin/env python3
"""Merged-LOD pilot overlay checks (prints numbers only; reads game archives, writes nothing).

A. The documented selection rule (bob1.final_index, Very High) against every
   finite-s multi-LOD row of a node census (run255: drawn_lod and census lod).
B. The produced overlay: per member, parse + serialise byte-identical, original
   records identical to the installed source (compact: 3 records in all, record 0
   equal as parsed and its bytes, located in the decoded source, equal at the same
   offset of the decoded overlay plus the bytes of any appended materials;
   pad/append-pad: every original record), C (index
   new_lod of the manifest) equal to the source's last record
   in points, record flags, part flags, part ints and per-part face/7-int multisets
   (only the grouping may differ), C's groups (kind, material, faces), pad a copy
   of C, Very High bands; CAT/DAT sizes and SHA-256. Sources are read with any
   x3m-lod overlay catalogue skipped (lod_overlay.original_assets). Kind: glow =
   material in the overlay manifest's glow list, alpha = alpha-tests or
   alpha-blends (lod_overlay.alpha_materials), else opaque. Checks for --collapse
   glow: one opaque group, at most one alpha group, and exactly one group per glow
   material used by the source's last record (recomputed with glow_materials).
   glow-area P: kind light = material in the manifest's area_kept list (recomputed
   with light_area_materials); the glow and light materials each keep one group.
   Material table: the source's materials are a prefix of the overlay's; every
   appended (synthesized) material has index = position and equals its dominant
   except g_Mat* FLOAT parameters; every group index of every record is inside the
   table; the synthesized set and C recomputed from the source (synth_materials,
   coarse_record) equal the overlay's. Alpha kinds use the overlay's table.
   Also: the highest main-view final index over all View Distance settings (the
   engine sets node+0x130 |= 0x100000 at >= 3) and whether the last record (the
   collision source) equals the source's last record in geometry.
   --collapse atlas (lod_atlas.py): kind atlas = the manifest's atlas material; C and
   the appended materials recomputed with lod_atlas.collapse (layout only, no baking)
   equal the overlay's, points included; C and the last record equal the source's last
   record in positions/normals/flags/faces with UVs ignored (C_positions_equal /
   last_positions_equal; duplicated points map back to their source point); the
   atlas material equals its dominant except the texture slots and g_Mat*; every
   atlas texture member is in the catalogue, its DDS hash, format, size and full mip
   chain match the manifest, the material names it and it resolves by its stem under
   dds/ in the overlay root; the sampling and UV checks (lod_atlas.check) are re-run on
   the written C and the written, decoded atlases (bump: also the normal angle error).
   Slots without an atlas (alpha; specular without --atlas-specular) must be NULL.
   --source-record N (manifest source_record; default the last record): every "source's
   last record" above that concerns C (C_equals_source_record, C_positions_equal, the
   recomputations, the kept sets, the shape check) reads record N instead; the
   last_* fields still compare with the source's last record (the collision source
   before the overlay). When the manifest names a pad_source (the source record is not the
   coarsest), the pad must equal that original record (pad_equals_source_LOD<n>) instead of
   being a copy of C (pad_is_copy). A part flagged 0x8000 (lod_atlas.HIDDEN_PART, copied group by
   group by the atlas collapse) is left out of the shape check.

  python3 verification/results/lod-overlay-pilot/pilot_check.py \
      <node_census_out.txt> <overlay root containing addon/NN.cat>
"""
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402
from sector_fog_census import Assets, unpack  # noqa: E402

ROW = re.compile(r'^\S+ \S+\s+\d+\s+\d+\s+\d+ \[(\d+)\] (\d+),(\d+),\w+ (\d+) ([\d,]+) (\S+)$')


def census(path):
    rows = ok = 0
    bad = []
    for line in Path(path).read_text().splitlines():
        m = ROW.match(line)
        if not m or int(m.group(4)) < 2:
            continue
        drawn, s, lod = int(m.group(1)), int(m.group(2)), int(m.group(3))
        th = [int(v) for v in m.group(5).split(',')[1:]]
        want = bob1.final_index(th, s, 'very-high')
        rows += 1
        if drawn == lod == want:
            ok += 1
        else:
            bad.append((m.group(6), s, th, drawn, lod, want))
    print(f'A census rule (very-high, f=1): multi-LOD rows {rows}, match {ok}, mismatch {len(bad)}')
    for b in bad:
        print(f'  mismatch {b}')


def same_geometry(c, src):
    if (c['points'], c['flags'], len(c['parts'])) != (src['points'], src['flags'], len(src['parts'])):
        return False
    for a, b in zip(c['parts'], src['parts']):
        if a['flags'] != b['flags'] or a.get('bounds') != b.get('bounds'):
            return False
        for key in ('faces', 'extra'):
            if sorted(x for g in a['groups'] for x in g.get(key, ())) != \
                    sorted(x for g in b['groups'] for x in g.get(key, ())):
                return False
    return True


def strip_uv(p):
    o = lod_atlas.uv_offset(p[0])
    return p if o is None else p[:o] + p[o + 2:]


def same_positions(c, src):
    """Geometry equal with the first UV set ignored (atlas: rewritten UVs, duplicated points)."""
    if (c['flags'], len(c['parts'])) != (src['flags'], len(src['parts'])):
        return False
    face = lambda pts, f: tuple(strip_uv(pts[i]) for i in f[:3]) + (f[3],)
    for a, b in zip(c['parts'], src['parts']):
        if a['flags'] != b['flags'] or a.get('bounds') != b.get('bounds'):
            return False
        if sorted(face(c['points'], f) for g in a['groups'] for f in g['faces']) != \
                sorted(face(src['points'], f) for g in b['groups'] for f in g['faces']):
            return False
    return True


def textures_only(m, dom, names):
    """Atlas material = dominant except the texture slots (atlas names / NULL) and g_Mat* FLOATs."""
    slots = {b't_diffusetexture', b't_lightmaptexture', b't_speculartexture', b't_bumptexture', b't_alphatexture'}
    keep = lambda q: [x for x in q.get('params', ()) if not lod_overlay.is_light_param(x[0], x[1])
                      and not (x[1] == 8 and x[0].lower() in slots)]
    got = {n.lower(): v for n, t, v in m.get('params', ()) if t == 8}
    return (keep(m) == keep(dom) and [x[:2] for x in m['params']] == [x[:2] for x in dom['params']]
            and all(got.get(k) == v for k, v in names.items()))


def atlas_checks(root, entries, raw, rec, marker, assets, mats, omats, srec, ladder, c, src_alpha):
    """(recomputed materials, recomputed C, text) for a --collapse atlas member."""
    at = rec['atlas']
    opts = marker.get('atlas_options', {})
    recomputed = list(mats)
    body = Path(rec['member']).stem
    res = lod_atlas.collapse(assets, body, recomputed, srec, src_alpha, at['px'],  # px incl. --screen-width
                             tuple(opts.get('sizes', (1024, 2048))), opts.get('specular', False),
                             marker.get('synth_material', True), bump=opts.get('bump', True),
                             force_uv2=opts.get('force_uv2', False),
                             force_mixed_effects=opts.get('force_mixed_effects', False))
    overlay_assets = Assets(Path(root))
    decoded, tex_ok = {}, True
    names = {lod_atlas.SLOT_NAMES[t['slot']]: t['name'].encode('latin1') for t in at['textures']}
    for slot in ('bump', 'specular', 'alpha'):
        names.setdefault(lod_atlas.SLOT_NAMES.get(slot, b't_alphatexture'), b'NULL')
    for t in at['textures']:
        e = entries.get(t['member'])
        if e is None:
            tex_ok = False
            continue
        dds = unpack(bytes(v ^ 0x33 for v in raw[e['offset']:e['offset'] + e['size']]))
        w, h, mips, fmt = lod_atlas.dds_format(dds)
        resolved, _ = overlay_assets.logical(t['member'].rsplit('.', 1)[0], ('.pck', '.dds', '.tga'))
        tex_ok &= (hashlib.sha256(dds).hexdigest() == t['dds_sha256'] and (w, h) == (at['size'], at['size'])
                   and mips == at['size'].bit_length() and fmt == t['format'] and resolved == dds)
        decoded[t['slot']] = lod_atlas.decode_dds(dds)
    tex_ok &= textures_only(omats[at['material']], mats[at['dominant']], names)
    chk = lod_atlas.check(srec, ladder[c], res, decoded, omats)
    s = ' '.join(f'{slot}: vs_source_rgb {"/".join(f"{x:.2f}" for x in d["src_rgb"])} vs_prefiltered_rgb'
                 f' {"/".join(f"{x:.2f}" for x in d["box_rgb"])} vs_source_a {"/".join(f"{x:.2f}" for x in d["src_a"])}'
                 + (f' vs_source_angle_deg {"/".join(f"{x:.2f}" for x in d["src_angle"])} vs_prefiltered_angle_deg'
                    f' {"/".join(f"{x:.2f}" for x in d["box_angle"])}' if 'src_angle' in d else '')
                 for slot, d in chk['slots'].items())
    text = (f' atlas {at["size"]}x{at["size"]} tiles {len(at["tiles"])} min_texels_per_px {at["min_texels_per_px"]:.3f}'
            f' duplicated_points {at["duplicated_points"]} textures_ok={tex_ok}'
            f' uv_inside_tile {chk["inside"]}/{chk["vertices"]} uv_inside_gutter {chk["in_gutter"]}/{chk["vertices"]}'
            f' max_map_error_texels {chk["max_map_error_texels"]:.3f} sampling(mean/p95/max) {s}'
            f' textures {[(t["slot"], t["format"], t["dds_bytes"], t["dds_sha256"][:16]) for t in at["textures"]]}')
    return recomputed, res['record'], text


def _record(lod):
    w = bob1.Writer()
    bob1.write_lod(w, lod)
    return bytes(w.b)


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def overlay(root):
    (cat,) = sorted(Path(root).glob('addon/[0-9][0-9].cat'))
    dat = cat.with_suffix('.dat')
    raw = dat.read_bytes()
    marker = json.loads(cat.with_name(cat.stem + lod_overlay.MARKER_SUFFIX).read_text())
    glow_of = {b['member']: set(b.get('glow', ())) for b in marker['bodies']}
    body_of = {b['member']: b for b in marker['bodies']}
    assets, skipped = lod_overlay.original_assets(bob1.DEFAULT_GAME)
    print(f'B collapse {marker.get("collapse")} glow_luma {marker.get("glow_luma")} glow_share'
          f' {marker.get("glow_share")} area_percent {marker.get("area_percent")} synth_material'
          f' {marker.get("synth_material")}; sources read without {skipped}')
    entries = {e['path']: e for e in read_catalogue(cat)}
    for e in entries.values():
        if e['path'].lower().startswith('dds/'):
            continue                                    # atlas textures: checked with their body
        data = unpack(bytes(v ^ 0x33 for v in raw[e['offset']:e['offset'] + e['size']]))
        tree = bob1.parse(data)
        ladder = bob1.lods(tree)
        src_data, src = assets.get(e['path'])
        src_tree = bob1.parse(src_data)
        source = bob1.lods(src_tree)
        n = len(source)
        sr = body_of[e['path']].get('source_record', n - 1)
        srec = source[sr]
        mats = bob1.materials(src_tree)
        omats = bob1.materials(tree)
        src_alpha = lod_overlay.alpha_materials(mats)
        alpha = lod_overlay.alpha_materials(omats)
        glow = glow_of[e['path']]
        rec = body_of[e['path']]
        collapse = rec.get('collapse', marker.get('collapse'))
        used = sorted({g['material'] for p in srec['parts'] for g in p['groups']})
        glow_ok = glow == (lod_overlay.glow_materials(assets, mats, used, marker['glow_luma'], marker['glow_share'])
                           if collapse in lod_overlay.KEEPING else set())
        area_kept = set(rec.get('area_kept', ()))
        area_ok = (area_kept == lod_overlay.light_area_materials(assets, mats, srec, glow,
                                                                 marker['area_percent'])[0]
                   if collapse == 'glow-area' else not area_kept)
        kept = glow | area_kept
        atlas_mat = rec['atlas']['material'] if collapse == 'atlas' else None
        kind = lambda m: ('atlas' if m == atlas_mat else 'glow' if m in glow else 'light' if m in area_kept
                          else 'alpha' if m in alpha else 'opaque')
        n_src = len(mats)
        appended = omats[n_src:]
        light_only = lambda a, b: ([p for p in a.get('params', ()) if not lod_overlay.is_light_param(p[0], p[1])]
                                   == [p for p in b.get('params', ()) if not lod_overlay.is_light_param(p[0], p[1])])
        syn_of = {s['index']: s['dominant'] for s in rec.get('synth', ())}
        table_ok = (omats[:n_src] == mats and sorted(syn_of) == list(range(n_src, len(omats)))
                    and all(m.get('index') == i and (i == atlas_mat or light_only(m, mats[syn_of[i]]))
                            and {k: v for k, v in m.items() if k != 'params' and k != 'index'}
                            == {k: v for k, v in mats[syn_of[i]].items() if k != 'params' and k != 'index'}
                            for i, m in enumerate(appended, n_src)))
        indices_ok = all(g['material'] < len(omats) for l in ladder for p in l['parts'] for g in p['groups'])
        placement, c, pad = rec.get('placement', 'pad'), rec['new_lod'], rec['pad_lod']
        atlas_text = ''
        if collapse == 'atlas':
            recomputed, c_recomputed, atlas_text = atlas_checks(root, entries, raw, rec, marker, assets, mats, omats,
                                                                srec, ladder, c, src_alpha)
        else:
            recomputed = list(mats)
            remap, _ = (lod_overlay.synth_materials(recomputed, srec, src_alpha, collapse, kept)
                        if marker.get('synth_material') else ({}, []))
            c_recomputed = lod_overlay.coarse_record(srec, None, src_alpha, collapse, kept, remap)
        synth_ok = recomputed == omats
        if placement == 'compact':
            r0 = _record(source[0])                     # source round-trips, so these are its record 0 bytes
            at = src_data.find(r0)
            w = bob1.Writer()                           # appended materials shift record 0 by their bytes
            ver = next(bob1.MATVER[t] for t, _ in tree['sections'] if t in bob1.MATVER) if appended else 6
            for m in appended:
                bob1.write_material(w, m, ver)
            shift = len(w.b)
            originals = (len(ladder) == 3 and ladder[0] == source[0] and at > 0
                         and data[at + shift:at + shift + len(r0)] == r0)
        else:
            originals = ladder[:n] == source
        shape_ok = True
        splits = {(sp['part'], sp['material']): len(sp['points'])
                  for sp in (rec.get('atlas') or {}).get('split_groups', ())}
        for pi, (cp, sp) in enumerate(zip(ladder[c]['parts'], srec['parts'])):
            if collapse == 'atlas' and cp['flags'] & lod_atlas.HIDDEN_PART:
                continue
            kinds = [kind(g['material']) for g in cp['groups']]
            kept_groups = sorted(g['material'] for g in cp['groups'] if g['material'] in kept)
            allowed = lambda k: max([1] + [splits.get((pi, g['material']), 1) for g in cp['groups']
                                           if kind(g['material']) in k])         # atlas split into groups
            shape_ok &= (kinds.count('opaque') + kinds.count('atlas') <= allowed(('opaque', 'atlas'))
                         and kinds.count('alpha') <= allowed(('alpha',))
                         and all(len({i for f in g['faces'] for i in f[:3]}) <= 0xffff for g in cp['groups'])
                         and kept_groups == sorted({g['material'] for g in sp['groups']} & kept))
        c_groups = [(kind(g['material']), g['material'], len(g['faces'])) for p in ladder[c]['parts'] for g in p['groups']]
        c_recomputed['value'] = ladder[c]['value']
        c_ok = c_recomputed['parts'] == ladder[c]['parts'] and c_recomputed['points'] == ladder[c]['points']
        synth_text = ' '.join(f'mat{s["index"]}~{s["dominant"]}:' + ','.join(
            f'{q["name"]}={q["dominant"] / 65536:.4g}->{q["written"] / 65536:.4g}' for q in s['params']
            if q['dominant'] != q['written']) for s in rec.get('synth', ())) or 'none'
        th = [l['value'] for l in ladder[1:]]
        pad_src = rec.get('pad_source')
        pad_ref = source[pad_src] if pad_src is not None else ladder[c]
        copy = (pad is not None and pad_ref['parts'] == ladder[pad]['parts']
                and pad_ref['points'] == ladder[pad]['points'] and pad_ref['flags'] == ladder[pad]['flags'])
        pad_name = 'pad_is_copy' if pad_src is None else f'pad_equals_source_LOD{pad_src}'
        max_final = max(max(bob1.drawable(th, v)) for v in bob1.VIEW_DISTANCE)
        print(f'B {e["path"]}: source {src["source"]} stored {e["size"]} decoded {len(data)}'
              f' roundtrip_equal={bob1.serialise(tree) == data} placement {placement} C_index {c} pad_index {pad}'
              f' originals_identical={originals} lods {n}->{len(ladder)} thresholds {th}'
              f' source thresholds {[l["value"] for l in source[1:]]}'
              f' C/pad groups {[sum(len(p["groups"]) for p in ladder[i]["parts"]) for i in (c, pad) if i is not None]}'
              f' source_record {sr} source_points {len(srec["points"])} source_faces'
              f' {sum(len(g["faces"]) for p in srec["parts"] for g in p["groups"])} C_points {len(ladder[c]["points"])}'
              f' C_part_flags {[hex(p["flags"]) for p in ladder[c]["parts"]]}'
              f' pad_points {len(ladder[pad]["points"]) if pad is not None else None}'
              f' {pad_name}={copy} C_equals_source_record={same_geometry(ladder[c], srec)}'
              f' last_equals_source_last={same_geometry(ladder[-1], source[-1])}'
              f' C_positions_equal={same_positions(ladder[c], srec)}'
              f' last_positions_equal={same_positions(ladder[-1], source[-1])}'
              f' max_main_view_final_index={max_final}'
              f' glow_recomputed_equal={glow_ok} area_kept_recomputed_equal={area_ok} C_shape_ok={shape_ok}'
              f' C_recomputed_equal={c_ok} materials {n_src}->{len(omats)} table_ok={table_ok}'
              f' indices_in_table={indices_ok} synth_recomputed_equal={synth_ok} synth {synth_text}'
              f' C_groups {c_groups}'
              f' very-high {bob1.format_bands(bob1.selection_bands(th))}'
              f' high {bob1.format_bands(bob1.selection_bands(th, "high"))}' + atlas_text)
    for p in (cat, dat):
        print(f'B file {p.name} bytes {p.stat().st_size} sha256 {sha(p)}')


if __name__ == '__main__':
    census(sys.argv[1])
    overlay(sys.argv[2])
