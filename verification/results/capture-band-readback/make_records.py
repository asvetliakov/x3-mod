"""Regenerate the comparison records of this directory from the local case directories (2026-10-08 runs).

usage: python3 make_records.py   (run from anywhere; the case directories are untracked under
verification/probe/build/ of the worktree that ran them and are not kept)
"""
import contextlib
import io
import pathlib

import compare

HERE = pathlib.Path(__file__).resolve().parent
BUILD = HERE.parents[1] / 'probe/build'
HDR, SHADOW, BENCH = ('motion-output-seam-hdr-on-20261008-', 'motion-output-seam-ownership-shadow-replay-cascades-20261008-',
                      'motion-output-bench-5120x1440-hdr-on-taa-off-20261008-')
RECORDS = {
    'wined3d_seam_hdr.txt': [('whole', HDR + '130348-988126'), ('band', HDR + '130122-540278'), ('rows5', HDR + '130353-915450'),
                             ('failband0', HDR + '130358-335856'), ('failcreate', HDR + '130400-600861'),
                             ('failband1', HDR + '130356-285136')],
    'wined3d_seam_shadow.txt': [('whole', SHADOW + '130402-833329'), ('band', SHADOW + '130254-843232'),
                                ('rows7', SHADOW + '130405-316316')],
    'wined3d_bench_5120.txt': [('whole_old_dll', BENCH + '130435-661881'), ('band', BENCH + '130407-719094')],
    # The final source (release_band_staging() moved after release_bolt_buffer() in release_resources) rebuilt
    # in build/ and rerun on wined3d against the earlier whole-path runs.
    'wined3d_final_hdr.txt': [('whole', HDR + '130348-988126'), ('final_band', HDR + '133111-158410'),
                              ('final_rows5', HDR + '133116-406741')],
    'wined3d_final_shadow.txt': [('whole', SHADOW + '130402-833329'), ('final_band', SHADOW + '133118-849835')],
    'dxvk_seam_hdr.txt': [('whole', HDR + '130459-611558'), ('band', HDR + '130456-081770'),
                          ('rows5', HDR + '130500-874752'), ('failcreate', HDR + '130503-333694'),
                          ('failband1', HDR + '130502-108584')],
    'dxvk_bench_5120.txt': [('whole1', BENCH + '131109-441902'), ('whole2', BENCH + '132125-397259'),
                            ('whole3', BENCH + '132133-184125'), ('band1', BENCH + '130508-722617'),
                            ('band2', BENCH + '131212-277444'), ('band3', BENCH + '132051-280993'),
                            ('band4', BENCH + '132158-145175')],
}


def resolve(prefix):
    found = sorted(BUILD.glob(prefix + '*'))
    assert len(found) == 1, (prefix, found)
    return found[0]


for name, runs in RECORDS.items():
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        compare.main([f'{label}={resolve(prefix)}' for label, prefix in runs])
    (HERE / name).write_text(out.getvalue())
    print(name, len(out.getvalue().splitlines()), 'lines')
