#!/usr/bin/env python3
"""Review probe (2026-11-13), the production first sighting of a swapped set: its ranges have no extent yet (the
extent read is queued at a range's first draw). Builds test_shadow_retention's store driver with two scenarios and
prints PROBE rows; run from the repository root (host C++ compiler, no Wine). Output: probe.txt beside this script.

* `swap`: a static node (one coarse record) re-seen with a detailed record whose extent arrives one sighting later,
  then unseen for 30 frames. Per row: the swap counters, the class, the records held and the records replayed
  (admitted) with their vertex buffer (100: the old set kept as the copy, 200: the new set).
* `rot`: the same swap with the node turned 1 rad in place: the origin alone repeats (pending), the box does not
  once the extent arrives (moved), and the node drops on its next unseen frame.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'verification/probe'), str(ROOT / 'verification/analysis'), str(ROOT / 'tools/analysis')]
import test_shadow_retention as t  # noqa: E402

probe = r'''
    {   Rig r; place(w, 20, 0, 60);
        const auto row = [&](const char* step, const sr::FrameStats& f) {
            const auto& n = r.store->nodes[r.store->find_node(70)]; unsigned vb_min = 0, vb_max = 0;
            for (unsigned q = 0; q < r.store->admitted_count; ++q) { const unsigned vb = unsigned(r.store->draws[r.store->admitted[q]].key.vb); vb_min = q ? std::min(vb_min, vb) : vb; vb_max = std::max(vb_max, vb); }
            std::printf("PROBE swap step=%s kept=%u pending=%u moved=%u moving_dropped=%u is_static=%d records=%u refs=%d replayed=%u replayed_vb=%u..%u\n", step, f.lod_swap_kept, f.lod_swap_pending, f.lod_swap_moved,
                        f.moving_dropped, int(r.store->find_node(70) != sr::none && n.is_static), r.store->draws_used, r.held(), r.store->admitted_count, vb_min, vb_max); };
        for (unsigned i = 0; i < 10; ++i) { r.draw(70, w, 100, 1); r.end(); r.next(); }
        r.no_extent = true; r.draw(70, w, 200, 0); r.no_extent = false; r.end(); row("swap_no_extent", r.next());
        r.end(); row("gap_unseen", r.next());
        r.draw(70, w, 200, 0); r.end(); row("extent_arrives", r.next());
        for (unsigned i = 0; i < 30; ++i) { r.end(); const auto f = r.next(); if (i == 0 || i == 29) row(i ? "unseen_30" : "unseen_1", f); }
    }
    {   Rig r; place(w, 20, 0, 60, .3); place(w2, 20, 0, 60, 1.3);
        for (unsigned i = 0; i < 10; ++i) { r.draw(71, w, 100, 1); r.end(); r.next(); }
        r.no_extent = true; r.draw(71, w2, 200, 0); r.no_extent = false; r.end(); auto f = r.next();
        std::printf("PROBE rot step=swap_no_extent kept=%u pending=%u moved=%u is_static=%d\n", f.lod_swap_kept, f.lod_swap_pending, f.lod_swap_moved, int(r.store->nodes[r.store->find_node(71)].is_static));
        r.draw(71, w2, 200, 0); r.end(); f = r.next();
        std::printf("PROBE rot step=extent_arrives kept=%u pending=%u moved=%u is_static=%d streak=%u\n", f.lod_swap_kept, f.lod_swap_pending, f.lod_swap_moved,
                    int(r.store->nodes[r.store->find_node(71)].is_static), unsigned(r.store->draws[r.store->nodes[r.store->find_node(71)].head].streak));
        r.end(); f = r.next(); std::printf("PROBE rot step=unseen_1 moving_dropped=%u nodes=%u\n", f.moving_dropped, r.store->nodes_used); }
'''
marker = '    float w[12], w2[12];\n'
assert t.DRIVER.count(marker) == 1 and 'bool no_extent' in t.DRIVER, 'the driver changed: update the probe'
src = t.DRIVER.replace(marker, marker + probe, 1)
compiler = shutil.which('c++') or shutil.which('g++')
with tempfile.TemporaryDirectory(prefix='x3-lodswap-probe-') as d:
    (Path(d) / 'driver.cpp').write_text(src)
    subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(Path(d) / 'driver.cpp'), '-o', str(Path(d) / 'driver')], check=True)
    out = subprocess.run([str(Path(d) / 'driver')], capture_output=True, text=True).stdout
print('\n'.join(line for line in out.splitlines() if line.startswith(('PROBE', 'RESULT'))))
