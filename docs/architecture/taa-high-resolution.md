# TAA at 5120x1440 on the D3D9 post chain

**Removed 2026-09-25** (user decision): S3's 16-tap point fallback (`--taa-history-taps 16`, `X3M_TAA_HISTORY_TAPS`, `resolve*_taps16.hlsl`) is gone; without FP16 and R32F filtering `TemporalPass::initialize` refuses and the device runs without TAA (`platform-portability.md`, entry of that date) (`docs/verification/launcher-options-inventory.md`, "4. Removed 2026-09-25").

Question: the D3D11 route is closed ([d3d11-post-chain-feasibility.md](d3d11-post-chain-feasibility.md): cross-API
sharing carries no pixels on the CrossOver target, and compute would have cut only the mask dilation, 2x). The TAA
stage is the seven-draw SM3 chain in `src/renderer/temporal_pass.cpp` (`taa_copy`, `taa_mask` x3, `taa_box` x2,
`taa_resolve`) and it scales with pixels; 5120x1440 is 3.56x the pixels of 1920x1080. What makes it affordable
there, within D3D9's documented API, SM3, native Windows parity and no per-draw work?

Inputs: the Run 280 split ([gpu-sync-timing.md, "Run 280"](../verification/gpu-sync-timing.md), medians including
a 0.264 ms sync-pair floor each), the mask bench and the byte-identical cuts already shipped
([engine-frame-time.md, "TAA stage cost" and "Mask draws"](engine-frame-time.md#taa-stage-cost-2026-09-24)),
the shaders (`src/temporal/resolve.hlsl`, `line_mask_ps.hlsl`, `thin_box_*_ps.hlsl`, `current_depth_ps.hlsl`)
and the fixture (`verification/probe/run_temporal_pass.py`, 744 numerical / 278 restorations, report byte-identical
to the committed `temporal-pass.txt`). Arithmetic: `verification/results/taa-high-resolution/cost_model.py`. Every
5120x1440 figure in this note is inferred; nothing has been flown at that resolution.

**Recommendation in one line.** Start with the two byte-identical cuts (fold the depth copy into the mask tests
draw as a second render target; halve the depth lane to `G32R32F`), then retire byte identity for three
output-changing steps in this order: 5-tap bilinear Catmull-Rom history, half-resolution sentinel box,
half-resolution mask dilations. Expected total (inferred): busy 1080p 4.8 -> about 4.0 ms at rest and 5.1 -> 4.1 ms
in fast flight; 5120x1440 13-16 -> about 10.5-13 ms at rest, 14-17.5 -> 11-14.5 in flight. That is 20-25 % off, not a
different order of magnitude: the SM3 resolve and the full-resolution tests draw remain, and the only lever beyond
this note is fewer pixels (a render scale with a TAA upsample), which is out of its scope.

## 1. Cost model per sub-pass

Net of the diagnostic floors (the production frame has no sync pairs), 1920x1080, Run 280 (measured):

| window | copy | mask (3 draws) | box (2 draws) | resolve | total |
| --- | --- | --- | --- | --- | --- |
| busy clear sector, still | 0.19 | 1.87 | 0.92 | 1.82 | 4.78 |
| same, fast flight | 0.20 | 1.79 | 0.91 | 2.24 | 5.12 |
| fogged sector, still | 0.19 | 0.97 | 0.57 | 1.01 | 2.72 |
| fogged, moving | 0.19 | 1.07 | 0.62 | 1.11 | 2.97 |

What each pass touches, in unique bytes per pixel (the neighbourhood taps of one pass hit the same cache lines, so
the bandwidth floor is unique bytes, not tap count; the bench measured about 0.1 ms per full-screen 16-byte read at
1080p, i.e. 330 MB/ms, and 0.035-0.04 ms fixed per full-screen draw):

| pass | reads | writes | B/px | bandwidth floor at 1080p | measured net | bound by |
| --- | --- | --- | --- | --- | --- | --- |
| `taa_copy` (`copy_` draw, lane path) | lane RT2 `A32B32G32R32F` 16 | R32F 4 | 20 | 0.13 + 0.04 | 0.19 | bandwidth |
| mask tests | R32F depth 4 (28 taps), motion 16, lane 16 (valid depth), scene centre 8 (routed) | A8R8G8B8 4 | 24 sky / 48 hull | 0.15-0.30 + 0.04 | bench 0.27-0.36 | bandwidth (the 28 depth taps cost <= 0.03; the gate arithmetic 0.09 on hull) |
| mask x, y | mask 4 (16 taps each) | 4 | 8 each | 0.05 + 0.04 each | bench 0.135 / 0.11 | tap issue (two min/max chains over 16 taps) |
| box rows | colour 8 (7 taps), mask 4 (7 taps) | FP16 MRT 16 (where a column reader opens) | 28 | 0.18 + 0.04 | 0.92 busy / 0.57 fogged for both | half bandwidth, half tap issue (7 + 14 FP16 taps with a finite test and a division each) |
| box columns | mask 4, rows 16 (14 taps), depth 4 | FP16 MRT 16 (always, zeros where closed) | 40 | 0.25 + 0.04 | (above) | |
| resolve `far_camera` | colour 8 (9 taps), depth 4 (9), previous colour 8 (1 or 16), previous depth 4 (1-4), motion 16 (2), age 4, mask 4, box 16 (2) | FP16 8 + R32F 4 | 76 | 0.48 + 0.04 | 1.82 still / 2.24 moving | fetch issue and ALU in rolled loops (29 fetches at rest, 44 under sub-pixel motion, 9-25 `weigh` divisions; 505 of 512 slots) |

Per frame at 1080p that is 228 B/px, 473 MB, a 1.43 ms bandwidth floor: 30 % of the busy-still total. The other 70 %
is tap issue and ALU, concentrated in the resolve (75 % of its cost is not bandwidth) and the mask x/y draws. Two
consequences: byte cuts (packing, half-resolution targets) help the copy, the tests draw and the box; tap cuts help
the resolve and the dilations. Both classes scale linearly with pixels. The mask's busy-sector excess over the bench
(1.87 in game against 0.84 on the bench for a hull scene) is unattributed; the three-draw split diagnostic the mask
note asks for settles which draw carries it, and until then every mask saving below is a bench figure.

5120x1440 (7.37 Mpx, 3.556x; inferred): holding the eight draws' fixed cost and scaling the rest linearly gives an
upper bound; the measured D3D9 dilation chain scaled 2.75x between the two resolutions
(414 -> 1,138 us, d3d11 note section 3), which gives a lower bound. Bandwidth alone: 1.68 GB per frame, 5.1 ms.

| window | 1080p net | 5120x1440 |
| --- | --- | --- |
| busy, still | 4.8 | 13.2-16.3 |
| busy, fast flight | 5.1 | 14.1-17.5 |
| fogged, still | 2.7 | 7.5-9.0 |
| fogged, moving | 3.0 | 8.2-9.9 |

For scale: Run 280's serialised frame was 25-31 ms at 1080p with 16 sync pairs (about 4 ms of floors), the engine's
own span 5-10 ms. The engine's fill-bound share grows with pixels too, so TAA's share of the frame stays roughly a
fifth; the absolute 13-17 ms is what has to come down. Memory at 5120x1440 (from temporal-integration.md "Cost and
memory"): 118 MB motion, 118 MB lane, 29 MB depth history, about 180 MB FP16 scratch and history, plus the mask,
box and row pairs (four FP16 targets, 236 MB); S2 and S4 below cut about 235 MB of it.

## 2. Steps, ranked

Ranked by expected saving at 5120x1440 per unit of look risk and fixture churn. "Hot path" is the game's per-draw
path: none of the steps adds per-draw CPU work; S2 changes what each routed draw writes (fewer bytes).

### S1. Fold the depth copy into the mask tests draw (byte-identical)

Mechanism. On the lane path (`depth_draw`: RT2 is `A32B32G32R32F`, the flown configuration) `taa_copy` is a
full-screen draw of `copy_` that point-samples the lane's `.r` into `depths_[next]`, and the mask tests draw then
reads `depths_[next]` at s1 (28 taps) and the lane at s5 (its `.b`, valid depth only). Instead the tests draw binds the
lane at s1, reads its `.b` from the same texel it already fetches for the centre (s5 goes), and writes
`depths_[next]` as COLOR1 (R32F beside the A8R8G8B8 mask; `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`, which the age MRT
already requires, `temporal_pass.cpp:205`). The box columns, the composition's fallback path (s6) and the resolve
read `depths_[next]` after the tests draw exactly as now. Output: the same `.r` of the same texels, so every target is
byte-identical; the pass sequence changes only when `far_on` (else the copy stays).

Cost. Saves the copy draw's 16 B read + 4 B write and its fixed cost, minus the tests draw's extra 4 B write and the
depth star walking 16-byte texels (L1 traffic x4 on the 28 taps; the bench bounds those taps at 0.03 ms on R32F).
Inferred: -0.14 to -0.17 ms at 1080p, -0.5 to -0.6 at 5120x1440. Slots: +2 on `line_mask_camera` (427 -> about 429).

Look risk: none. Fixture: `temporal-pass.txt` stays byte-identical in every numeric row; the runner's expected
`(744, 278, 2)` tuple moves only if the state-restoration count changes with the removed draw. Native Windows: the
same cap the age MRT already depends on; the copy path remains the fallback when `!mrt_age_`. Flight: none needed
beyond the next `--gpu-sync-timing` flight, where `taa_copy` should read only its floor.

### S2. Halve the depth lane: RT2 `A32B32G32R32F` -> `G32R32F` (byte-identical values)

**Stopped 2026-09-24:** the premise is wrong. On the lane RT2 `.g` is not a copy of `.r`: the route's programs overwrite
it with the sun share (the transformed materials' final `oC2.g` MOV, `linear_material.cpp:1713`, `material_motion.h:9`),
which the sun-shadow apply quads read beside `.r` and `.b`. See "S1 / S2 implemented" below.

Mechanism. `current_depth_ps.hlsl` writes `float4((z/w).xx, w, w)`; `.g` duplicates `.r` and `.a` duplicates `.b`.
Writing `float4(z/w, w, w, w)` instead keeps R32F's `.r` bit for bit and puts the clip w in `.g`, so a `G32R32F` RT2
carries (device depth, view z) in 8 bytes; the readers move from `.b` to `.g`: `line_mask_ps.hlsl` (s5),
`sun_shadow_apply_ps.hlsl` / `sun_shadow_cascade_apply_ps.hlsl` (receiver depth), the capture readbacks and the
motion-output format lists (`motion_output.cpp:2149, 2157, 2345, 7168`). `temporal_pass.cpp:64` already accepts
`G32R32F` as a depth input. Before implementing, grep for any reader of the lane's `.a` (none found in the temporal
shaders; the shadow lane comments name `.b` only).

Cost. 8 B/px less on every lane read (the copy or, after S1, the tests draw; the sun-shadow apply) and 8 B/px less
per covered pixel per routed draw (overdraw about 2x, inferred). Inferred: -0.1 ms on the TAA side and -0.1 on the
engine side at 1080p, -0.35 + -0.35 at 5120x1440; 59 MB less default-pool memory at 5120x1440.

Look risk: none (FP32 values unchanged). Fixture: temporal pass byte-identical; the motion-output fixture's RT2
format and readback rows (190 cases) re-baselined for the format only; the material-motion transformer test for the
fragment's new instruction shape (still no constant; `material_motion.cpp:387-405`). Native Windows: `G32R32F`
render targets are D3D10-class (inferred; the existing `CheckDeviceFormat` list gains the format and the 16-byte lane
stays the fallback).

### S3. 5-tap bilinear Catmull-Rom history (output-changing, low risk)

What the 16 taps do today (`resolve.hlsl:525-560`): a Catmull-Rom cubic (a = -0.5) over the 4x4 texel neighbourhood
of the history lookup, 16 point taps each `weigh`ed (the reversible luminance weighting, HDR route k > 0) and
finite-tested, renormalised over the finite taps, with a per-tap reactive check; on the texel grid (f = 0, the
snapped stationary case) the far variants still run the loop with weights (0, 1, 0, 0) and read one texel. The
negative lobes are what keep detail under fractional motion: the fixture's BLUR row (measured) has a 0.25 px/frame
scrolling wave keeping 0.93 of its amplitude under Catmull-Rom against 0.71 under plain bilinear (energy 0.86 vs
0.51). Plain bilinear is therefore not on the table: the user already notices blur in motion (lattice-ghosting
memory), and the RCAS sharpen would be asked to undo a resample blur every frame.

Mechanism. Jimenez's 5-tap form: sample the previous colour with hardware bilinear at five positions (the centre
row/column pairs combined by weight, `tc12 = tc1 + tc2 * w2 / (w1 + w2)`), dropping the four corner taps whose
combined weight is at most 1.6 % of the filter (w0 and w3 peak at -0.0625), renormalised. The 2x2 depth footprint
of the disocclusion proof stays point-sampled and unchanged. Keep the `all(f == 0)` point branch (one exact texel at
rest; the far variants dropped it for slots, which the shorter loop gives back). Reactive taps: bilinear samples of
the R32F mask at the same five positions (a 0/1 mask through bilinear is nonzero exactly where a contributing texel
is nonzero, a NaN texel stays NaN; `maskSafe` refuses both, so the policy is preserved with 5 fetches instead of
16). Finiteness: the history is this program's own output, finite by construction (`cleanColor`, the bounded inverse
weighting), so per-tap renormalisation becomes one finite test on the filtered result that refuses the lookup
(current-only), which is what the "Invalid history colour" fixture case expects (0.25 = current). Weighing happens
after the filter (the filter unit cannot weigh); at k = 0 identical, at k > 0 a second-order difference at bright
edges under motion, bounded exactly as today by the unchanged 3x3 clip.

Sampler contract change: s2 (and s6) become `D3DTEXF_LINEAR` for this program only; `src/temporal/README.md` says
hardware bilinear on any input violates the contract, so the README's sampler section and the `normalize` sampler
setup are updated together. Availability: `CheckDeviceFormat(D3DUSAGE_QUERY_FILTER, D3DFMT_A16B16G16R16F)`; the
16-tap program stays compiled and bound when the query fails.

Cost. The 16-tap path is the measured 0.4-0.5 ms motion delta of the resolve at 1080p (16 fetches, 16 divisions, 16
finite tests inside a dynamic loop). Inferred: -0.25 to -0.35 ms under motion at 1080p, 0 at rest; -0.9 to -1.2 at
5120x1440 in flight. Slots: the 4x4 loop with its `loopWeight` selects goes; expect 30-50 slots back on `far_camera`
(505 -> about 460; must be measured with fxc), which is what makes the next slot-bound edits possible at all.
Measured: 505 -> 504 (below); the expectation counted the rolled loop's 16 iterations as static slots.

Look risk on the accepted behaviours: lattice crawl at rest is untouched (f = 0, exact texel); the thin region,
camera gate, sentinel box, strict sky, band and exit reset, motion-weight cap are all in the mask or in the depth
proof and weight arithmetic, none of which changes; ghosting is bounded by the same clip. The visible change is a
sub-1 % filter-mass difference and the bilinear unit's sub-texel precision (8 bits on every D3D10-class GPU;
unverified on wined3d/Metal, see section 6). Fixture: the hand-specified cases keep their expected values (the 1D
Catmull-Rom cases are exact under the 5-tap form since w0.y = w3.y = 0 makes the corners vanish; the integer-shift
and stationary cases sit at f = 0; the zoom cases with a 2D fraction read uniform history, where any normalised
filter is exact), the CPU model of the resolve (`temporal_resolve.cpp`, the LATTICE_ORACLE and BLUR oracles) is updated to the
5-tap form at the same tolerances; `report_sha256` is re-committed as provenance. Native Windows: FP16 linear
filtering is universal on D3D10-class hardware (inferred), gated by the query above. Flight: a lattice pan
(run177-type rotation) and fast flight in the busy sector; pass criteria: crawl at rest unchanged, motion blur not
worse than today by eye, `taa_resolve` in flight within 0.1 ms of `taa_resolve` at rest.

**Measured (2026-09-24; implemented, fixture only, not flown).** Ledger:
[temporal-resolve.md](../verification/temporal-resolve.md), "2026-09-24 5-tap bilinear history".

| program | 16-tap words / slots | 5-tap words / slots |
| --- | --- | --- |
| plain (`resolve`) | 1,681 / 432 | 1,661 / 425 |
| thin | 1,818 / 465 | 1,814 / 469 |
| age | 1,947 / 494 | 1,940 / 495 |
| far | 1,948 / 493 | 1,931 / 493 |
| far_camera | 2,006 / 505 | 1,987 / **504** |

Slots are D3DXDisassembleShader's count (native `d3dx9_37`, the fixture's `RESOLVE_BUDGET` rows). The 16-tap loop is
rolled, so its body counts once; the five unrolled fetches with their weights cost about as much, and far_camera gains
1 slot, not 30-50 (3 before the texel-centre bias below, which costs 2 per program). The saving is dynamic: per pixel under motion 5 fetches and one division instead of 16 fetches, 16
finite tests and 16 divisions in a loop (inferred from the listings; unmeasured on the GPU until flown). The rest
branch stayed in the plain program only: in the thin / age / far variants it cost about 20 slots (far_camera 522 of
512, measured on a scratch compile), so they rested on the filter returning the exact texel at a texel centre, which the
fixture's `FILTER_PROBE` shows on this backend. *History since the A' re-baseline (below): every 5-tap program has the
point read at rest again; the texel-centre bias stays for the moving lookups that still sit on centres along one axis.* Filtering is a second binding of the same textures (s11 colour, s12
mask, LINEAR) rather than LINEAR on s2 / s6 or a manual 2x2 blend: the manual form needs the 12 non-corner texels as
point fetches (24 slots of `texldl` alone against 10), and the second binding costs no slot (a `dcl`) while s2 keeps the
point read at rest. The weights use the closed forms s = f(1-f)/2 (w0 = -s(1-f), w3 = -sf, w1 + w2 = 1 + s, total
1 - sx sy), 9 slots fewer than the textbook polynomials on the plain program.

- `FILTER_PROBE` (FP16 and R32F): every texel centre addressed as `(i + 0.5) / W` returns the texel bit for bit at
  W = 32, 1280, 5120; sub-texel weights are 8-bit (max error 1/512, mean 1/1024 over 1024 fractions). This settles the
  section-6 question for this backend; native drivers remain inferred.
- Relative identities hold: rest (plain and far_camera, 32 frames) identical to the 16-tap form; rows whose lookup
  fraction is 0 or 1/2 on the one moving axis are unchanged (`STATIONARY`, `LATTICE`, `THIN_REGION_CAMERA_STATIC`,
  whole-pixel pans such as `SETA_PAN` 3 px, the 0.5 px `THIN_REGION_PAN` / `THIN_REGION_CAMERA`: w2 / (w1 + w2) is then
  0 or exactly 1/2 in the filter's 8-bit weights). Other fractional 1-D motion moved slightly (the scrolling wave,
  the 0.30 px sentinel y-pan, camera yaw / pitch drift within ±0.004 px): the corners weigh 0 there as well, the
  difference is 8-bit filter weights against float point-tap weights.
- Semantic changes, the texel-centre bias (+2 slots per program) and why the far_camera drift row repeats the plain one
  (its gates are idle in that scene): the ledger entry linked above.
- Diagonal drift (0.30, 0.20) px/frame on a 1-px lattice: 17,619 of the channel samples differ, max 0.0171, at most
  2.5 % of `w (max - min)` of the current 3x3 (the clip bound), never above it.
- The 1-D cases are exact in arithmetic, not bit for bit: the scrolling wave keeps 0.9300 of its amplitude (0.9296
  with 16 taps), the 8-bit filter weights against the point taps' float weights.

### S4. Half-resolution sentinel box (output-changing, conservative)

*As built (2026-09-24, opt-in `--taa-box-resolution half`, not flown; [temporal-resolve.md](../verification/temporal-resolve.md)
"S4 half-resolution box"): the row pairs start at an odd row (texel g = rows 2g-1, 2g; target W/2 x (H/2 + 1)), so the
columns read 4 pairs = the exact 8x8 window; the pair runs for every camera-gate run of an even size, the stabiliser on or
off. The emitter bound below would not contain the full-resolution box where a pixel does not fire; as built a block takes
the inner 4x4 only when the bright tap is in the common 6x6 of its four windows, the 8x8 box of the dim taps when the bright
taps lie in the outer ring only, and the 8x8 of every tap across a silhouette: containment per pixel, ghost within 2 px.*

Mechanism. The box pair and the row pair become W/2 x H/2. Rows: per half-res texel the min/max over 2 rows x 8
columns (the union of the two pixels' 7-wide windows), 16 taps per texel = 4 per pixel instead of 7. Columns: 4
half-res rows (8 px), 8 taps per texel = 2 per pixel instead of 14. The result is the box over the 8x8 px block window
containing each pixel's 7x7: a superset, so the clip it feeds is never tighter than today. The emitter bound's inner
3x3 becomes the 4x4 covering the block (within 2 px of the tight clip instead of 1). The resolve reads s9/s10 at the
block texel (point; `sizeJitter` is per-target, so the box programs take their own 1/size constant). The columns keep
writing zeros where closed (the resolve multiplies `(b - a)` by `(box - clip)` everywhere).

Cost. Writes 16 B per 4 px instead of 16 B/px in both passes, the resolve reads 4 B/px instead of 16, taps per pixel
6 instead of 21. Inferred: -0.4 to -0.5 of the 0.92 ms at 1080p busy (about -0.25 fogged), -1.4 to -1.8 at 5120x1440;
about 177 MB less default-pool memory at 5120x1440 (the box and row pairs are four FP16 targets of 59 MB each).

Look risk. On the lattice replay the box binds on injected stale history, not on real bursts (all box variants within
0.02 % of clip-off; taa-lattice-crawl.md section 32): box7 bounded the stale ghost to 74 codes (1.58x the clipped
resolve) and box11 to 115. An 8x8 aligned box sits between, nearer box7 (inferred: at most about 1.7x). The user
accepts slight ghosting; the sentinel stabiliser's distant-station flicker fix is a weight and gate effect, not a
box-width one. Fixture: the sentinel `facets` oracle (`oracle_error <= 0.02`, `flicker_ratio < 0.6`, `mask_error`) and
the camera gate's `box_domain` / `stale` rows re-baselined against a CPU oracle of the block box, plus a new exact
containment oracle: for every pixel `low_half <= low_full` and `high_half >= high_full` (no tolerance). Native
Windows: nothing new (same formats, smaller targets). Flight: a pan across distant stations over sky (the stabiliser
scene) and laser fire against sky (the emitter bound); pass: no return of the flicker, no visible halo next to
lasers.

### S5. Half-resolution mask dilations with a full-resolution composition (output-changing, conservative)

Mechanism (as sketched in engine-frame-time.md "Mask draws", now costed). The tests draw stays full resolution: its
7-tap lines and 1-px line test are what see a 1-px strut, and at 5120x1440 distant struts are still at most a pixel.
The x draw writes W/2 x H/2: b is the maximum over 2 rows x 12 px (the union of the pair's 11-wide windows), a and r
the minimum over 2 x 18 px; 36 taps per texel = 9 per pixel instead of 16. The y draw runs at half resolution over 6
(b) and 9 (a, r) half-res rows, 2.25 taps per pixel instead of 16. A new full-resolution composition draw reads the
half-res result (1 tap) and the pixel's own tests texel (far weight, class code; 1 tap) and writes the final mask
exactly as the composition does now. Output: the region grows by at most 2 px, the speed gates close at most 2 px
earlier; both on the conservative side.

Cost. Bench (inferred from the shipped rows): x 0.135 -> about 0.07, y 0.11 -> about 0.03, plus the composition at
about 0.05: -0.1 ms at 1080p, -0.35 at 5120x1440; more if the split diagnostic attributes the in-game excess to the
x/y draws. Reach: the windows are in pixels, chosen at 1280x768 ("closure within 8 px, the reach of the region
itself", taa-lattice-crawl.md 13.1) and flown at 1920x1080; at 5120x1440 an 8-px reach is a quarter of that angle
horizontally, and the half-res dilation doubles it for the same tap count. That is the second reason to take this
step at 5120x1440 even though its 1080p saving is small.

Look risk: the thin region and its gate get wider, never narrower; the trail a body moving behind a static lattice
leaves stays bounded to the region (+2 px). Fixture: `thin_region_cases` (the CPU oracle of the mask) takes the
block windows; the emissive-vote and bad-motion rows re-baselined; a new exact containment oracle (`b_half >= b_full`,
`a_half <= a_full`, r likewise); the plain-silhouette bit-identity keeps (a silhouette is not fragmented under either
window). Native Windows: nothing new. Flight: the lattice stand at rest (crawl unchanged), a pan (camera gate), the
moving truss (trail bounded).

### S6. Pack the motion target RT1 to 8 bytes (deferred: producer ABI)

Which channels need 32 bits, from the resolve's reads: `.xy` is the absolute previous unjittered UV. FP16 has an
ulp of 2.4e-4 to 4.9e-4 UV in the upper half of the range, 1.25-2.5 px at 5120 wide: unusable. UNORM16
(`A16B16G16R16`) gives 0.08 px, but the stationary snap (`snapEpsilon` 1e-4 texel) then fails on every static pixel,
which would take the filtered path with a spurious 0.04 px offset every frame: blur at rest. `.z` is the expected
previous device depth: the fixture's "full precision object depth" case (0.5002 against 0.5) and the run215 far-plane
row (1 - 2^-16) need FP32 near 1; reversed (1 - z) in FP16 would do. `.w` is the state (-1, 0, 1): exact in FP16. So 8
bytes works only with a delta encoding of `.xy` (`A16B16G16R16F`: delta UV, reversed depth, state; FP16 relative
precision keeps a 50 px/frame delta within 0.03 px and a 0 delta exact), which needs the current pixel position in
the producer: `rigid_motion_ps.hlsl` receives only the previous clip position, the transformed material programs
likewise, so it means an interpolator or `VPOS` in every producer, the fill draw, the resolve, the mask, the capture
readbacks and the 190-case motion-output fixture. Saving: 8 B/px on the tests draw and the resolve plus the
producer's writes, -0.15 to -0.2 ms at 1080p, -0.5 to -0.7 at 5120x1440, 59 MB of memory. The most ABI churn per
millisecond of any step; take it only after S1-S5 are flown and if the 5120x1440 flight shows bandwidth still
dominant.

### Considered and not taken

- **(c) Removing the depth copy outright.** Its product is the next frame's history depth and this frame's R32F
  depth for four passes. Ping-ponging RT2 in the motion output makes those passes read 16-byte texels (+0.15-0.2 ms,
  a wash); a producer-side R32F MRT (a fourth render target on every routed draw plus the fill) costs 4 B/px times
  overdraw and a motion-output change. S1 gets the same saving with a pass-order change inside `TemporalPass`.
- **(f) Skipping the tests draw on frames without camera motion.** The mask depends on the jittered depth, which
  changes every frame, and on routed motion (ships move while the camera is still); a "nothing moved" frame is a
  docked or paused one. Saving 0.3 ms on rare frames for a CPU-side detector is not worth a code path.
- **Merging the box columns into the resolve** once S3 frees slots: saves the columns' 16 B write and the resolve's
  16 B read (about 0.1 ms) but adds 14 taps on every open pixel, which at rest is most of the sky. Net about zero.
- **Merging mask draws or an MRT pass**: each draw reads the previous one up to 8 px away (mask note).
- **Unrolling or splitting the mask programs**: measured zero on the bench.
- **Tests draw at half resolution**: loses 1-px struts (the crawl fix itself).
- **A render scale with TAA upsampling** (the game renders 3840x1080, the resolve outputs 5120x1440): the only lever
  that beats linear scaling, and it helps the whole frame, not TAA alone. It needs the proxy to give the game a
  smaller back buffer and upscale HUD and all; a separate design if the 5120x1440 flight shows the frame itself is
  not affordable.

## 3. What must not change, and how each step keeps it

| accepted behaviour | where it lives | steps that touch it | preserved by |
| --- | --- | --- | --- |
| Lattice crawl fixed at rest (line filter, thin region 0.97, camera gate; Run 59 / 61) | tests draw (full-res depth star), dilations, resolve's masked filter and weights | S5 widens the windows; S3 changes the history filter | tests draw stays full resolution; windows only grow (containment oracle); at rest f = 0 reads one exact texel (point branch kept) |
| Thin region closed by the fastest pixel within 8 px; trail bounded to the region | 17x17 minimum of the gates | S5 | the minimum over a superset window is at most today's: closes earlier, never later |
| Sentinel stabiliser (distant stations under a pan, S = 0.7 / E = 1) with the 7x7 box bound | box passes, resolve's `(b - a)` term | S4 | block box contains the 7x7 (containment oracle); emitter bound within 2 px instead of 1; weight and gate untouched |
| Strict sky history, band term, exit reset (SETA smear; Runs 244 / 249 / 254) | resolve's depth proof, `tolerance`, age sign | none | the 2x2 point depth footprint and the age read are untouched by S3 (colour taps only) |
| Motion history weight cap (Run 262) | resolve's `cap` | none | untouched |
| Stationary stability: a static scene reads its own texel, no phase ripple beyond (1-w)/(1+w) | snap to the texel grid, one tap | S3 | point branch at f = 0; STATIONARY one-step oracle (0.002) and drift_px carry over |
| HDR route: reversible luminance weighting exact at rest; fireflies bounded | `weigh` / `unweigh`, 3x3 clip | S3 weighs after the filter | identity at f = 0; the clip box is unchanged so the bound is unchanged |
| Fail-closed on NaN / non-finite inputs on both backends (`>=` / `<=` only) | every program | S1 (lane texels at s1), S3 (bilinear taps), S4 / S5 (new windows) | the same `.r` values; NaN through bilinear stays NaN and the `>=` / `<=` tests refuse it; min/max over more taps of finite UNORM8 / FP16 values |
| Reset, lost device, MRT unbind order, no per-draw CPU work | `TemporalPass::run` | S1 (one MRT more), S4 / S5 (smaller targets) | same ensure/drop pattern as the box rows; targets created at the pass's size, released across Reset |

## 4. Order and the reference procedure

0. **Diagnostics first, no output change.** Split `taa_mask` into its three draws in the `--gpu-sync-timing` build
   (the mask note's pending step) and fly the first 5120x1440 window with the split: it gives the scaling exponent
   per pass (2.75x or 3.56x) and which mask draw carries the busy-sector excess, both of which move the ranking above.
1. **S1 and S2**, byte-identical, under the existing hash oracle: `run_temporal_pass.py` must report the same 744
   numerical rows with `temporal-pass.txt` identical except the CPU timing and slot rows, plus the mask bench
   (`mask_bench.cpp`) for S1's tests-draw delta and the motion-output fixture for S2's format rows. One flight
   with the split confirms `taa_copy` at its floor.
2. **S3**, the first output-changing step, retires the byte hash of the report. From here the fixture's role is:
   - the hand-specified numeric cases (tolerance 0.002, HDR 0.01) are the algorithm's contract and carry over
     unchanged; a step that needs to move one of them says which and why;
   - the CPU oracles carry over with the model updated to the new arithmetic (the one-step STATIONARY oracle, the
     LATTICE_ORACLE / BLUR models of the resolve, `thin_region_cases`, the box oracle), at their existing tolerances;
   - the relative identities carry over unchanged (static-camera `colour_identical`, `e0_vs_plain_max_diff = 0`,
     sentinel-off rows identical, motion-weight off rows 0.000000): they compare variants within one build;
   - the whole-image look metrics become bounds instead of equalities: the lattice ripple ratio within 0.15 of the
     modelled 0.62, the 13.1 replay figures (2.34 rms / 22 p2p / 0 px > 40) as ceilings, the sentinel flicker ratio
     < 0.6, the stale-ghost bound (<= 1.7x for S4);
   - S4 and S5 add exact containment oracles (half-res superset of full-res, per pixel, both options built);
   - `report_sha256` is re-committed per step as provenance, not a gate; each step keeps the previous path behind an
     option until its flight is accepted, then the option is removed.
3. **S4**, then **S5**, each with its own fixture baseline and flight (section 2). S5 second because its 1080p saving
   is the smallest and its look argument (reach) is specific to 5120x1440.
4. **S6** only on evidence from the 5120x1440 flights that bandwidth still dominates after S1-S5.

Per step: one fixture run with both options, the relevant bench, then one `--gpu-sync-timing` flight at 1080p and one
at 5120x1440, both against the same three scenes (lattice stand at rest, a pan across distant stations over sky,
fast flight in the busy clear sector).

## 5. Totals (inferred)

| | busy still | busy fast flight | fogged still | fogged moving |
| --- | --- | --- | --- | --- |
| 1080p today (measured, net of floors) | 4.8 | 5.1 | 2.7 | 3.0 |
| S1 + S2 (byte-identical) | 4.55 | 4.85 | 2.45 | 2.7 |
| + S3 | 4.55 | 4.55 | 2.45 | 2.6 |
| + S4 | 4.1 | 4.1 | 2.2 | 2.35 |
| + S5 | 4.0 | 4.0 | 2.1 | 2.25 |
| 5120x1440 today | 13-16 | 14-17.5 | 7.5-9 | 8-10 |
| 5120x1440 after S1-S5 | 10.5-13 | 11-14.5 | 6-7.5 | 6.5-8 |

S2's engine-side saving (about 0.1 ms at 1080p, 0.35 at 5120x1440) is outside the `taa` bracket and not in the table.
What remains after the five steps is the resolve at rest (about 1.5 ms at 1080p, 5 ms at 5120x1440: 29 fetches and
the clip statistics per pixel in rolled SM3 loops) and the full-resolution tests draw; neither has a lever left inside
D3D9 short of fewer pixels.

## 6. Unknown, and what settles it

- **Where the busy-sector mask excess (0.9 ms over the bench) sits**: the three-draw split diagnostic; it decides
  whether S5 is worth 0.1 ms or 0.5 ms at 1080p.
- **The scaling exponent at 5120x1440** (2.75x or 3.56x per pass) and whether the frame itself is affordable there:
  the first 5120x1440 flight with `--gpu-sync-timing`, no build change.
- **Bilinear FP16 on this backend**: whether `CheckDeviceFormat(QUERY_FILTER, A16B16G16R16F)` succeeds under wined3d,
  the sub-texel weight precision, and whether a sample at an exact texel centre returns the texel bit for bit. A
  fixture probe (a 4x4 FP16 texture sampled at f = 0, 0.25, 0.5 against a CPU bilinear) settles it before S3 is
  written; the 0.002 case tolerance depends on it.
- **Native Windows**: `MRTINDEPENDENTBITDEPTHS` with A8R8G8B8 + R32F (S1), `G32R32F` render targets (S2), FP16 linear
  filtering (S3) are all D3D10-class caps and cannot be verified here; every step keeps today's path as the
  cap-gated fallback, so the worst case on an unexpected driver is today's cost, not a failure.
- **Whether the pixel-sized mask windows are right at 5120x1440 at all** (a quarter of the 1280x768 angular reach
  horizontally, and a 7-tap line test that sees a class change only within 3 px): the 5120x1440 lattice-stand flight,
  which may argue for S5 on look grounds before cost grounds.
- **Any reader of the lane's `.a`** before S2: none (grep, 2026-09-24), but S2 is stopped by `.g` (the sun share), not `.a`.

## Decision (main session, 2026-09-24)

Ratified in this order: (1) diagnostics: split `taa_mask` into its three draws for `--gpu-sync-timing` and fly the
first 5120×1440 session (the user's DISPLAY1 is 5120×1440 per the D3D11 probe); (2) S1 (depth copy folded into the
tests draw as an R32F second target) and S2 (lane RT2 to G32R32F) under the byte-identical hash oracle; (3) S3
(5-tap bilinear Catmull-Rom history), S4 (half-res box), S5 (half-res dilations) one at a time, each with a new
reference and one flight, the hash oracle retired for those as section 4 prescribes. Expected total 20–25 % off the
stage; the SM3 resolve and the full-res tests draw remain, so 5120×1440 needs the fog quarter-res march and the
overlay's draw cuts as well. Context: the D3D11 route is closed on the CrossOver target
([d3d11-post-chain-feasibility.md](d3d11-post-chain-feasibility.md)).

## S1 / S2 implemented (2026-09-24)

**Diagnostic split.** `--gpu-sync-timing` reports `taa_mask_tests`, `taa_mask_x` and `taa_mask_y` (indices 22-24,
appended; 25 passes, 50 event queries) nested inside `taa_mask`, one `Span` per draw of the mask loop
(`temporal_pass.cpp`, the loop in `run`). The far-only configuration has one draw, reported as `taa_mask_tests`. Each
pair adds its sync floor (about 0.26 ms) to `taa_mask` and `taa`. Ledger: [gpu-sync-timing.md](../verification/gpu-sync-timing.md).

**S1, shipped.** On a two- or four-channel current depth with a far-program run, the chain's first draw binds the
caller's depth at s1 and writes the next R32F depth history as COLOR1; the copy draw does not run. It uses two new
programs, `line_mask_depth_ps.hlsl` and `line_mask_camera_depth_ps.hlsl` (`line_mask_ps.hlsl` with
`X3M_MASK_DEPTH_OUT`). They are bound only for that one draw, so no draw writes `oC1` without a second target bound.
The camera variant takes the lane's `.b` from the centre texel it already fetched, so s5 stays unbound. The base
programs' bytecode is unchanged (same sha256). Instruction slots, measured: `line_mask_depth` 420 (base 428),
`line_mask_camera_depth` 407 (base 427). The fold needs `mrt_age_` (two targets and `MRTINDEPENDENTBITDEPTHS`) and the
program. Otherwise, or if creating the program fails, the copy draw runs as before. RT1 is unbound right after the
draw, before any later reader of the history depth (the composition's s6 fallback, the box columns, the resolve).

Consumers: the fold changes only `depths_[next]`, which receives the same `.r`. RT2 itself is untouched. The shadow
apply, the fog and the mask's lane term read RT2 directly, so their `.b` is the same texel as before.

Identity proof (bottle X3, measured):

- `run_temporal_pass.py` passes 744 / 278 / 546 with `temporal-pass.txt` byte-identical. The default mode has no lane
  input, so this shows that the paths without the fold are unchanged.
- The lattice mode's `DEPTH_FOLD` cases (RESULT 508 / 89, from 500 / 12) cover four configurations, three frames each
  under a pan: the camera program with and without the sentinel stabiliser, the screen-gate thin region and the far
  stabiliser alone. Every run starts from the hostile state and must restore it exactly, including RT1 and
  `COLORWRITEENABLE1`. The colour history, depth history, age target and final mask are compared byte for byte:
  - with the lane term off, against an R32F twin holding the same `.r`: the lane fold, the `G32R32F` fold, and the lane
    with the folding programs refused at creation (the copy-draw fallback) are all 0 bytes different;
  - with the lane term on (both camera configurations), the lane fold against the lane copy path: 0 bytes different.
    The fold reads `.b` from s1's centre texel, the copy path from s5.
  - `Diagnostics::depth_fold_reason` reads `lane_mrt`, `program` and `r32f_depth` as expected.
- A committed negative control, one lane texel's `.r` one ulp up, gives 3 differing depth-history bytes and must fail
  the identity.
- Faults with a lane input and the lane term on, once for a refused RT1 bind and once for a failed fold draw:
  - the run returns E_FAIL (as `operation`), publishes nothing and drops the history;
  - RT1 is unbound right after the attempt, and the caller's RT1 and `COLORWRITEENABLE1` come back;
  - the next run folds again from an empty history.
- A device Reset between two lane runs (`before_reset` / `after_reset`, every default-pool object released): the fold
  resumes byte-identical to its R32F twin.
- The seven lane-term flight rows (`THIN_REGION_CAMERA_FLIGHT lane=1`) match the committed report. These are 4-decimal
  summary rows, not a byte comparison; the byte comparison is the lane-term case above.

Evidence: `verification/results/taa-high-resolution/s1_identity.py` and its `_out.txt`, and `acceptance_out.txt`
beside them (build, x87, host, GPU sync and motion output).

Session log: the attachment's first completed run logs one line, `motion_output_taa_depth_fold device=N depth_fold=1|0
reason=lane_mrt|r32f_depth|d24_decode|far_off|mrt_caps|program`, so a flight shows the fold directly.

Expected saving (inferred, section 2): -0.14 to -0.17 ms at 1080p and -0.5 to -0.6 ms at 5120x1440. That is the
copy's 20 B/px and fixed cost, minus the 4 B COLOR1 write and the tests draw's taps now walking 16-byte texels.
The next `--gpu-sync-timing` flight on the lane configuration should show `taa_copy` at its floor.

**S2, stopped.** The lane RT2 carries three live channels:

- `.r` is device depth, read by everything.
- `.g` is the sun share, written by the route's final `oC2.g` MOV (`linear_material.h:114, 161`,
  `material_motion.h:9`) and read by `sun_shadow_apply_ps.hlsl` / `sun_shadow_cascade_apply_ps.hlsl`.
- `.b` is the clip w (view z), read by the shadow apply quads (receiver depth), by the fog march / composite /
  repair / motes (`fog_density_field_inc.h:135, 293`, `fog_field_inc.h:69`, `fog_dust_motes_ps.hlsl:15`) and by the
  camera mask's lane term.
- `.a` has no reader.

`G32R32F` holds two of the three live channels, and D3D9 has no three-channel float target. Moving w into `.g` would
need the share moved elsewhere (a fourth render target or a packed encoding), which is a producer ABI change outside
this step. The fog pass also refuses any RT2 that is not `A32B32G32R32F` (`fog_pass.cpp:726`). The 8 B/px and
59 MB at 5120x1440 stay on the table only with such a redesign.


## A' re-baseline implemented (taa-plan-lifted-slot-cap.md step 1; 2026-09-24, fixture, not flown)

One re-baseline of the TAA programs under the lifted slot cap; the ledger entry is
[temporal-resolve.md](../verification/temporal-resolve.md) "A' region hold".

- **Mask chain.** With the camera gate the chain is the tests draw alone (A' is the camera gate's only path since Run 79 A
  accepted it; `--taa-region-hold` and the dilated chain were removed, ledger "A' only: dilated chain removed"): `taa_mask` should read `taa_mask_tests` under `--gpu-sync-timing` (the x and y draws cost 1.33 ms
  of 8.76 at 5120x1440 in run290, measured). The second mask target (29.5 MB at 5120x1440) is released.
- **Resolve.** `resolve_far_camera_hold.hlsl` composes the region from the tests target with a region hold and a peak
  hold of the camera gate's closure, both one jitter cycle long (L = the jitter sample count, 8 by default), the gate's own
  openness taken as the smaller of the pixel's and its nearest-depth 3x3 neighbour's, carried in 16 fraction bits of the
  R32F age count (no new lane). The screen gate is not held, so the region reopens the frame a pan stops (fixture: the
  step after a stop equals the dilated gate's, 1.983 codes). The plan's linear 4-frame reopen failed the fixture's
  motion-start bounds; with the peak hold and the neighbour term the trail is +0.0295 over the plain resolve (bound 0.04;
  dilated gate +0.0298) and rest ripple equals the dilated gate's (1.0000).
- **Box.** The box programs gated on the composed mask (`b > a`), which the hold no longer draws; three twins gate on the
  tests texel inside the region (camera openness above screen openness, and this frame's flag or the previous frame's
  region hold at the same texel; or the class code with the stabiliser) and mark the texels they computed; the resolve
  takes the 3x3 clip for the added strength elsewhere. Fixture, arm scene under a 0.5 px/frame pan: the box runs on 0.546
  of the frame (dilated chain 0.719, the ungated tests test 1.000). The `taa_box` cost under a pan at 5120x1440 is the
  number the flight must read.
- **Consolidations.** The exact point read at rest is back in every 5-tap program (S3's rest dependence on the filter
  returning texel centres is gone; the centre bias stays for one-axis motion), and the HDR route weighs each of the five
  history blocks before the sum.
- **Cost, inferred from the listings** (`verification/results/taa-high-resolution/aprime_slots.py`): the hold resolve
  executes +22 instructions per pixel at rest and +90 moving against the S3 `far_camera` (0.02 / 0.10 ms at 5120x1440
  at 1.07 us per executed instruction per frame), 4 fewer bilinear fetches at rest and one more 4-byte fetch; slots
  616 (S3 504), within the 2,048 ceiling. Net expected: about -1.2 to -1.3 ms of `taa` at 5120x1440, less whatever the wider box gate adds under pans.
