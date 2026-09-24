#!/usr/bin/env python3
"""Effect of the F/0x4000 factor on the small-parts cull threshold (cull_small_parts_core.h threshold_for).

Prints, for the default 2 px and the 4/8 px settings, the threshold in the engine's s units at the
run131 projection (m00 bits 3f4ccccc, 1280 wide) and at 5120x1440, for the game's F = 0x4000 and the
--fov default F = 0x3470, with and without the factor, and the run131 census class that each
threshold culls (kept rows with s < t: nodes / draws; verification/fixtures/run131-cull-census-rows.json).
The run131 rows were captured at F = 0x4000, so the 0x3470 classes only show how many more rows the
higher threshold reaches, not a measured frame at the new FOV. Host only; no Wine, no game.
"""
import json
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_cull_small_parts_site as probe  # noqa: E402


def m00_for(focus, width, height):
    """cot(F/2) / W with W = 0.75 * w/h (displays at least as wide as 4:3)."""
    return 1 / math.tan(focus * math.pi / 65536) / (0.75 * width / height)


def main():
    document = json.loads((ROOT / 'verification/fixtures/run131-cull-census-rows.json').read_text())
    rows = document['rows']
    run131_m00 = struct.unpack('<f', struct.pack('<I', int(document['projection_m00_bits'], 16)))[0]

    def culled_class(threshold):
        flipped = [r for r in rows if r[8] == 0 and r[0] < threshold]
        return len(flipped), sum(r[9] for r in flipped)
    print('display          F       m00       px  t(no factor)  t(factor)  run131 class at t(factor) nodes/draws')
    for label, width, height, base_m00 in (('1280 (run131)', 1280, 768, run131_m00), ('5120x1440', 5120, 1440, None)):
        for focus in (0x4000, 0x3470):
            # run131: the captured P[0] scaled by cot(F/2) (cot(0x4000/2) = 1); 5120x1440: the engine's projection law.
            m00 = base_m00 / math.tan(focus * math.pi / 65536) if base_m00 is not None else m00_for(focus, width, height)
            for px in (2, 4, 8):
                plain, factored = probe.threshold_for(px, m00, width), probe.threshold_for(px, m00, width, focus)
                nodes, draws = culled_class(factored)
                print(f'{label:15s} {focus:#06x} {m00:9.6f} {px:4d} {plain:12d} {factored:10d}   {nodes:4d} / {draws:4d}')


if __name__ == '__main__':
    main()
