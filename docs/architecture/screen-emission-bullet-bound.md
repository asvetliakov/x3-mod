# Step D — per-draw screen hull for the bullet bound

Design note, 2026-09-14, sibling of [screen-emission-region.md](screen-emission-region.md) (step B and its
near-plane subsection, read in the fix worktree at `a5f5986`). Not implemented. Question: a tight, conservative
rectangle for the player's bullet batches near the camera, so packed policy-8 brackets stop covering 58–100 %.

Ratified 2026-09-14 by the orchestrator, conditional: implementation starts after run 16 reports the
near-plane-clipped route's counters and `packed_sample` brightness; the w-scaled pad finding applies to the
default-on fade route as well and is tracked there before step D lands.

## Decision

**One rectangle per draw, computed at the draw as the padded hull of the drawn vertices themselves
(per-triangle near-plane cut), from an exact prefix that a Lock-time sentinel delimits — not a per-batch
AABB, not k rectangles, not a stencil.** The AABB is inflated twice: a world-axis box around a beam that
is diagonal to the view, and a stale DISCARD tail (≤ 95 vertices) that in run 15 put the sector origin
into two batches. The vertex hull removes both at ≈ 4 `dp4` per vertex on the draw path and makes the
Unlock scan proportional to the bullets written instead of the 6144-vertex buffer. k rectangles lose on
fixed bracket cost; a stencil mask is the follow-up (D2) only if the measured hull is still too large.

## 1. Run-15 numbers (session `20260914-163102-212`, 1280×768, `zn` = 6; the camera rows reproduce the logged rects)

Every batch is 6 vertices per bullet (27–605 bullets; all 35 counts are multiples of 6). Two buffers are
the player's (vb 1908, 1909), each locked and drawn twice per frame; vb 1079 (3048–3300 vertices) lies
76–96 km behind the camera and is `BehindNear` after the fix. Clipped-AABB fraction the near-plane build
(run 16) will admit, against two beam models through the same view (`axis` = the box's longest axis as a
zero-width segment, `diag` = nearest to farthest corner; the true hull lies between the models and the AABB):

| frame | vb | bullets | box half (units) | view depth | AABB | axis | diag |
| --- | --- | ---: | --- | --- | ---: | ---: | ---: |
| 10128 | 1908 | 176 | 525, 596, 294 | 148…1842 | 82 % | 7.1 % | 1.4 % |
| 10128 | 1909 | 72 | 4785, 912, 1308 | −320…7726 | 100 % | 26 % | 20 % |
| 10373 | 1908/1909 | 605 / 216 | 57747, …, 15440 | −77779…6498 | 100 % | — | 25 % |
| 10929 | 1909 | 72 | 1197, 3470, 3124 | −68…9240 | 89 % | 9.4 % | 25 % |
| 11469 | 1908 / 1909 | 176 / 72 | 537, 622, 311 / 2730, 1817, 3992 | 345…2100 / −1642…6835 | 58 / 72 % | 4.7 / 15.5 % | 0.0 / 12.9 % |
| 11967 | 1908 / 1909 | 176 / 81 | 592, 659, 294 / 2653, 2129, 4103 | 256…2122 / −1899…6981 | 71 / 87 % | 6.5 / 23 % | 0.2 / 13 % |
| 15281 | 1908 / 1909 | 88 / 27 | 370, 387, 116 / 372, 389, 121 | 128…1223 / 186…1288 | 68 / 49 % | 7.8 / 7.3 % | 1.1 / 1.1 % |
| 15432 | 1908 / 1909 | 99 / 27 | 399, 406, 126 / 2104, 2224, 656 | 135…1300 / 438…6698 | 69 / 76 % | 7.6 / 11.4 % | 1.7 / 0.2 % |

Frame 10373 is the stale tail: both boxes end at exactly 0.000 on x and z (the origin) with 18 and 48
stale vertices inside checkpoints 37 and 13, while the written vertices lie near x ≈ −112 000. Composite
area today: `packed_region_pixels` 1,618,944 (2 brackets, frame 10128), 1,136,688 (11469), 2,866,254
(4 brackets, 15432). Scan cost in game: 938,016 µs / 23,073 scans = **40.7 µs per scan** (41.3 at frame
15432), always 6144 vertices; 2.84 locks per frame on average, 5 on a firing frame: ≈ 0.2 ms of CPU per
firing frame before any bracket runs.

Cost model (fixture): ≈ 0.165 ms fixed per bracket (16-DIP paired windows, both sizes) plus ≈ 1 ns per
pixel of area (the fullscreen prototype's 2.3 ms per DIP at 2.07 Mpx, an upper-bound scaling). A
full-viewport bracket at 1280×768 is ≈ 1.1 ms; frame 15432 ≈ 3.5 ms in four brackets, and the run-16 build
admits frame 10128 at 82 + 100 + 82 + 100 % ≈ 3.6 Mpx ≈ 4.3 ms, more than the 4.1 ms median frame.

## 2. Granularity: hull rectangle, k rectangles, stencil

- **k rectangles = k brackets.** Break-even is one fixed cost per extra rectangle, ≈ 165 kpx (17 % of
  1280×768, 8 % of 1080p). The diagonal beams above (13–26 %) split into four save ≤ 0.19 ms of area and
  add 0.5 ms; scattered fans gain less. No per-draw k escapes this; refused.
- **Stencil mask inside one bracket (D2, deferred).** Per-bullet footprint is small (a 30-unit quad at
  500 units ≈ 30 px wide; 176 bullets ≈ 3–4 % of the viewport): a pre-pass drawing the batch into stencil
  and three quads with `STENCILFUNC = EQUAL` bound the area term by coverage. The game's depth surface is
  D24X8 (`telemetry_presentation depth_format=77`, 0 stencil bits), so it needs a proxy D24S8 of A's size
  (two more `SetDepthStencilSurface`, one `Clear` with a rect, ≈ 8 states, one geometry draw with Z test
  off for a coverage superset). Larger change, D3DMetal early stencil unmeasured; only if the hull fails section 6.
- **Hull of the drawn vertices (chosen).** Tightest single rectangle; bracket, witness and fixtures unchanged.

## 3. Where the extrema come from

1. **Lock (marked buffer, DISCARD):** before returning the mapping, write a sentinel (all-ones DWORDs, a
   NaN) over the slots the previous prefix used (`count_prev × 24` bytes; the whole 147 KB the first
   time). DISCARD contents are undefined to the app and the writer only `memcpy`s its prefix (`0x004bfdd9`).
2. **Unlock:** scan from vertex 0 to the first sentinel: exact `count`, finiteness and `world_limit` over
   exactly the written vertices, and a copy of their positions (12 B each) into the record. No
   checkpoints, no superset. Cost ∝ `count`: ≈ 25 KB read + 12 KB write at 1056 vertices against 147 KB
   read today (fixture-measured); a real sentinel-valued vertex ends the scan early → `Beyond` → native.
3. **Draw (`derive_prefix_region`):** require `primCount·3 ≤ count` and the revision unchanged; project
   the prefix through the c0–3 rows the draw uses (exact rows, jitter-aware if the producer ever jitters).
   Per triangle: all clip z ≥ 0 → three points; straddling → in-front vertices plus the edge crossings
   (`project_box`'s edge rule, three edges); all behind → nothing; w ≤ 0 with z ≥ 0 cannot occur under a
   perspective row set and keeps `NonPositiveW`. Rectangle = padded hull ∩ viewport. Budget ≤ 25 µs per
   draw at 1056 vertices (4 double `dp4` each) through `route.ticks`. Storage 72 KB per record × 16 =
   1.2 MB, allocated at mark, never per draw. Projecting at Unlock instead (shadow rows, cumulative screen
   extrema, rows compared at the draw) pays the `dp4` on locks without a draw, cannot cut per triangle
   without the count, and refuses whenever the game uploads the rows after Unlock; it loses.

## 4. Conservativeness

D3D9 rasterises a pixel whose centre lies inside the clipped triangle, which lies inside the hull of its
projected in-front vertices and crossings, so hull + pad ⊇ footprint; the guard band changes nothing.
Arithmetic: the GPU's fp32 `dp4` on world coordinates ≈ 1.2e5 cancelling against the translation loses
≈ 4·2⁻²⁴·Σ|terms| ≈ 0.03 clip units, i.e. ≈ 0.003·640 / w px — under 0.2 px at w ≥ 100 (nearest run-15
bullet w = 128) but ≈ 3 px at w = `zn` = 6. The 1-px pad is not enough there: pad each projected point by
⌈2⁻²²·Σ|terms| / w · half_w⌉ + 1 px from the same products (no extra cost); today's AABB corners share
the exposure. Jitter ≤ 0.5 px stays inside the pad. Stale data: none (exact count). The witness
(`outside=0`) remains the runtime detector; a non-conservative rectangle is still a hard failure.

## 5. Native Windows

Documented D3D9 only; the sentinel is app-side memory inside the lock. Native maps dynamic buffers
write-combined: the sentinel is a streaming store, the prefix read is uncached (≈ 25 KB per lock,
unmeasured, replacing today's unmeasured 147 KB). If it matters, a staging lock (proxy memory returned,
prefix copied at Unlock) is the documented remedy and makes the retained copy free; unverifiable here.

## 6. Verification and the run-16 measurements

- Host (`test_fade_region.py`): random triangle lists, straddling and w near `zn`, against a top-left-rule
  scanline rasteriser with fp32-emulated `dp4`: footprint ⊆ rect, 0 outside, ≥ 600 cases; the sentinel
  scan on a host driver (count exact, early sentinel → `Beyond`).
- `run_locked_prefix_live.py`: today's near_* cases plus a zero-tail case (no inflation), a fan and a
  diagonal beam (hull ≪ box, footprint covered), `scan_us` per lock and draw ticks;
  `run_linear_distance_fade_live.py --screen-emission`: witness `outside=0`, straddle injection fires; x87 audit.
- Run 16 (the near-plane candidate as built) is the baseline: `locked_prefix_frame`
  `bound/refused/reason_near/clipped/f_mean`, `packed_region_pixels` per firing frame, `packed_sample
  pre_y/post_y` (the brightness answer), frame-time delta firing vs not. If that candidate is rebuilt
  anyway, a capture-only `locked_prefix_bullets` line (per-bullet extrema per marked lock, ≈ 36 KB for
  605 bullets) gives the exact hull and coverage offline first; otherwise D reports its own `f_mean` in run 17.
- Acceptance for D: `f_mean` ≤ 0.15 over admitted bullet brackets while firing (from 0.58–1.0),
  Σ `packed_region_pixels` ≤ 0.6 Mpx per firing frame at 1280×768 (from 1.1–2.9 M), scan CPU ≤ 50 µs per
  firing frame (from ≈ 200), witness `outside=0`, and step B's frame-time acceptance (median within
  +0.5 ms of run 11's 4.097 ms in a bullet-heavy flight). `f_mean` above 0.25 on the fan batches → D2.

## 7. Unknown

Tail zeros: Wine's fresh allocation (frame 10373 says so) or a game write — the sentinel is right either
way. Real spread of the 176-bullet fans (models 0–8 %, AABB 58–82 %): the dump or run 17. The fp32 bound
assumes no FMA contraction (the 2⁻²² pad covers one bit). Native WC read cost; D3DMetal early stencil (D2 only).
