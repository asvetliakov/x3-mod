"""Answer-4 metrics of a scratch sky bake (docs/architecture/sector-background-modern.md; tools/analysis/sky_textures.py).

  python3 verification/results/sector-background-textures/sky_bake_metrics.py BAKE_DIR --game GAME [--json OUT]

BAKE_DIR is a `sky_textures.py --out` directory (sky-bake.json plus dds/*.dds). Every nebula part is compared with the
member it was baked from (the game's catalogue view without the x3m-lod overlay slots): plateau-block share and mean
identical-luma run on the bake box-downsampled to the source size and rounded to 8 bits, PSNR over the source's
non-zero texels, mean hue shift on texels with chroma > 8, zero violations at bake resolution, wrap-column difference
(limits: sky_textures.THRESHOLDS as redefined 2026-11-04; the run is printed for information only);
star textures: texels byte-identical after the header mark, payload 2. One row per texture, a threshold verdict per
row and in total, bake seconds and stored bytes (overlay growth) from the record. Exit status 0 only when every texture
passes. Prints numbers only; reads nothing but the bake directory and the game catalogues.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import lod_atlas  # noqa: E402
import sky_textures as st  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('bake', type=Path)
    ap.add_argument('--game', type=Path, required=True)
    ap.add_argument('--json', type=Path, help='write the per-texture metrics here (default BAKE/sky-metrics.json)')
    a = ap.parse_args(argv)
    record = json.loads((a.bake / 'sky-bake.json').read_text())
    assets = st.source_view(a.game.resolve())
    rows, failed = [], 0
    print(f"recipe {record['recipe_id']} tool_sha256 {record['tool_sha256']} options {json.dumps(record['options'], sort_keys=True)}")
    print('member | kind | size | s | stored B (source B) | plateau src->bake | run src->bake (info) | PSNR | hue deg | zero viol |'
          ' wrap src->bake | verdict')
    for r in record['rows']:
        entry = next(e for e in assets.candidates(r['member']) if e['source'] == r['source'])
        src = assets.read_entry(entry)
        bake = (a.bake / r['file']).read_bytes()
        mt = r['metrics']
        out = dict(member=r['member'], kind=mt['kind'], family=mt['family'], seconds=mt.get('seconds'),
                   stored_bytes=mt['bytes'], source_stored_bytes=r.get('source_stored_bytes'), timings=r.get('timings'))
        if mt['kind'] == 'stars':
            mark = lod_atlas.read_dds_mark(bake)
            bad = ([] if bake[:32] == src[:32] and bake[44:] == src[44:] else ['texels differ']) + \
                  ([] if mark and mark[4] == st.PAYLOAD_SKY_STARS else [f'mark {mark}'])
            out.update(mark=mark, failures=bad)
            print(f"{r['member']} | stars | {mt['width']}x{mt['height']} | {mt.get('seconds')} | {mt['bytes']}"
                  f" ({r.get('source_stored_bytes')}) | mark slot {mark[1]} lane {mark[2]} K {mark[3]} payload {mark[4]}"
                  f" | {'PASS' if not bad else 'FAIL ' + '; '.join(bad)}")
        else:
            m = st.metrics(src, bake)
            bad = st.failures(m)
            out.update(metrics=m, failures=bad, encoder_psnr=mt.get('encoder_psnr'))
            print(f"{r['member']} | part | {m['bake_size'][0]}x{m['bake_size'][1]} | {mt.get('seconds')} | {mt['bytes']}"
                  f" ({r.get('source_stored_bytes')}) | {m['plateau_share_source']:.3f}->{m['plateau_share']:.3f} |"
                  f" {m['run_source']:.2f}->{m['run']:.2f} | {m['psnr']} | {m['hue_shift_deg']} | {m['zero_violations']} |"
                  f" {m['wrap_diff_source']}->{m['wrap_diff']} | {'PASS' if not bad else 'FAIL ' + '; '.join(bad)}")
        failed += bool(out['failures'])
        rows.append(out)
    parts = [o for o in rows if o['kind'] == 'part']
    secs = [o['seconds'] for o in parts if o['seconds'] is not None]
    grow = sum(o['stored_bytes'] for o in rows)
    stock = sum(o['source_stored_bytes'] or 0 for o in rows)
    print(f"textures {len(rows)} (parts {len(parts)}), passing {len(rows) - failed}, failing {failed}; part bake s per"
          f" part min {min(secs, default=0):.2f} median {sorted(secs)[len(secs) // 2] if secs else 0:.2f} max"
          f" {max(secs, default=0):.2f}; stored {grow} B (the overlay members) against {stock} B of stock members")
    th = st.THRESHOLDS
    limits = dict(plateau_share=f"<= {th['plateau_share']} or <= {th['plateau_ratio']} x source; source >= {th['near_uniform']}: bake <= source and run <= source run / 3",
                  run='none, informational (near-uniform branch above)', psnr=f">= {th['psnr']}", hue_shift_deg=f"<= {th['hue_shift_deg']}")
    for k, lim in limits.items():
        vals = [o['metrics'][k] for o in parts]
        if vals:
            print(f'  {k}: min {min(vals)} max {max(vals)} (limit {lim})')
    (a.json or a.bake / 'sky-metrics.json').write_text(json.dumps(dict(record=str(a.bake / 'sky-bake.json'),
                                                                        thresholds=st.THRESHOLDS, rows=rows), indent=1) + '\n')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
