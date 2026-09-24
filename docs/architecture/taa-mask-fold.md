# TAA mask fold: removing the full-resolution "tests" draw

Design note, 2026-09-25. Proposed; the main session ratifies. Question: remove the line-mask tests draw
(`line_mask_camera_depth_thin_ps.hlsl`, one full-resolution pass, 1.38-1.40 ms at 5120x1440 [M]) by moving what it
produces into the passes that remain, without changing the accepted look. Constraint from the run315 lattice triage
(orchestrator, 2026-09-25): the screen-space thin test (the four 7-tap depth-class searches) stays at full resolution;
the Terran plant's lattice is 64 opaque cell draws that cast no thin vote (RT2 `.a = 1` on 100 % of the region), its
lines are 1 px gaps at a 7.5 px pitch, and the vote alone puts 7.5 % of the flickering pixels in the A' region against
72-80 % with the search. "Search kept" is the baseline below; "without" figures are informational.

Marks: **[M]** measured in this session's tool results (run312 / run315 / run310 `gpu_*` summaries, the RESOLVE_BUDGET
rows of `verification/results/bottle-X3/temporal-lattice.txt`, the program manifests), **[I]** inferred.

## Decision

Fold the tests draw into the A' resolve (`resolve_far_camera_hold`): the resolve reads the four-channel lane itself,
computes the far weight, both gates (own and nearest-depth neighbour), the vote flag, the 7-tap search and the emissive
vote per pixel, composes the holds exactly as today, and writes the R32F depth history as a third render target. The
S4 half-resolution box pair stays and gates on the previous frame's region hold plus this frame's vote (a superset of
today's gate, minus the retired sentinel class); where the resolve needs the box and the block was not computed it
takes the 7x7 min / max in place. Expected TAA span at rest, 5120x1440: 5.45 ms today [M] -> about 3.5-3.9 ms [I], of
which about 1.0 ms is the sentinel-class retirement (the box no longer runs over the sky) and 0.55-0.9 ms is the fold
itself. The whole 1.4 ms of the tests draw is not recoverable: about 0.6 ms of it is bandwidth on texels the resolve
must then read itself (section 2).

## 1. What the tests draw produces and who reads it (question 1)

Target `line_masks_[0]`, A8R8G8B8 (UNORM8), full resolution, plus COLOR1 = the lane's centre texel into the R32F
depth history (`depths_[next]`, S1). Written once per frame by `line_mask_camera_depth_thin_ps.hlsl` (257 slots,
1,042 words [M]); read by the box rows / columns at s8, by the resolve at s8 (own texel and the nearest-depth
neighbour's), and dumped as `taa_mask` by `motion_output.cpp:1838` for F8 bursts and the fixture cases that read them.

| output | computed from | precision it needs | consumers | can the consumer compute it itself |
| --- | --- | --- | --- | --- |
| `r` = screen openness of the pixel's own correspondence, `saturate(1 - (speed - LO) * W)` | the motion texel at uv (routed) or the camera path c0..c3 at the pixel's depth | UNORM8 today; the hold quantises to quarters anyway | resolve: `ownS = min(tests.r, beside.r)`; box gate `m.a > m.r` | resolve: yes, it already fetches the motion at uv and at `dilatedUV` and computes `previousUV` for the same pixel (`speed` exists in the THIN_CLIP path); box rows: no (16 motion texels per block) |
| `g` = far weight `saturate((d - d0) * inv)` on a valid depth | the centre depth | UNORM8 | resolve only (`stabilise.rg = tests.gg * c11.yz`: filter weight, history weight target) | resolve: yes, 3 slots from the depth it reads at s1 |
| `b` = flag (fragmented search, or vote, or emissive vote) as 254/255 + sentinel class as 1/255 | 28 lane taps along four lines (search); lane `.a` at the centre (vote); 9 scene taps (emissive) | one bit each | resolve: `flagged` (region hold h = L), `carriesClass` (retired); box gate: `inRegion` this frame, class | resolve: yes for all three (the emissive vote's 3x3 luma minimum rides the existing 3x3 clip loop); box: the vote yes (one lane texel per pixel), the search no (28 taps per pixel x 16 pixels per block) |
| `a` = camera openness, `max(screen, camera-relative)` on a routed valid-depth pixel, 1 on the unrouted camera path | motion texel, depth, lane `.b` (view z) with c8 / c9 | UNORM8 | resolve: `ownC = min(tests.a, beside.a)`, `closureHold`; box gate `m.a > m.r` | resolve: yes (the neighbour's from the motion texel it already fetches at `dilatedUV` and the nearest depth; the lane `.b` of the winner is one more lane read of the 3x3 loop it already runs) |
| COLOR1 = the lane centre texel -> R32F depth history | the lane at s1 | bit-for-bit `.r` (every later reader compares depths) | the resolve's 3x3 depth loop and the box columns' sky test this frame; the next frame's `previousDepth` proof (s3) | this frame: nobody can read a copy that does not exist yet, so the resolve reads the lane for its 3x3 loop; the next frame: the resolve writes the copy as MRT2 |

Two facts fall out. Every scalar the tests draw computes is reproducible in the resolve from texels the resolve already
fetches (the centre depth, the motion at uv and at the dilated position) plus a wider centre texel (the lane, 16 B
instead of the 4 B copy). What cannot move without a one-frame lag is the **box gate's use of the search flag**: the
box runs before the resolve, and the search costs 28 taps per pixel, which a half-resolution pass testing 16 pixels per
block cannot afford (section 4.3 handles this).

## 2. Where the 1.4 ms goes

Bytes per pixel of the tests draw: motion 16 B + lane 16 B read, mask 4 B + depth 4 B written = 40 B/px = 295 MB at
5120x1440, about 0.9 ms at the bench's 330 MB/ms [I]. The 28 search taps cost at most 0.07-0.13 ms (fixture,
vote-only source against both [M]) and under 0.03 ms flown [M]. The remainder, about 0.3 ms, is the fixed cost of a
full-resolution draw on this backend (the S4 fixture found the fixed per-draw cost dominant at 1280x768 [M]).

After the fold the resolve still reads the motion (it does today) and the lane instead of the 4 B copy (+12 B/px),
still writes the depth (+4 B/px, as MRT2) and drops the mask write. Recoverable: the fixed cost 0.3, the motion read
0.36, the mask write 0.09, the lane read minus the copy read 0.09 = about 0.84 ms [I]; the search on 16 B texels inside
a fetch-bound program costs more than in the tests draw (+0.15-0.35 [I]) and the new arithmetic about 0.15 [I]. Net
0.55-0.9 ms [I].

## 3. Options (question 2)

Baseline figures, 5120x1440 at rest, run312 windows w10-w17 [M]: `taa` 5.36-5.47, `taa_mask_tests` 1.39, `taa_box`
1.30 (S4, sentinel stabiliser on: the class opens the box over the sky), `taa_resolve` 2.44-2.46, `taa_copy` 0.02.
With the sentinel class retired first (the brief's assumption, section 7): the box gates on `a > r` inside the region
only, which at rest is nothing (a = r = 1) and under a pan is the region; box about 0.25-0.35 [I] (fixed cost of two
half-resolution draws plus the gate reads), `taa` about 4.4-4.5 [I]. Every option below is costed from that.

Executed-slot cost 1 us per slot per frame (AGENTS.md); `far_camera_hold` is 616 slots / 2,479 words [M],
`line_mask_camera_depth_thin` 257 / 1,042 [M], rows_half 158, columns_half 330 [M].

| option | static slots | executed / px added | ms at 5120x1440 [I] | `taa` after [I] | what the search costs there |
| --- | ---: | ---: | ---: | ---: | --- |
| (a) fold into the resolve (recommended) | resolve +260-300 -> 880-920; 3 camera mask programs retired (-241 / -257 / -223) | +120-150 (gates x2, far weight, vote, emissive luma in the clip loop, hold read at the unreprojected texel, MRT2), +100 on the search's rolled loops | resolve +0.5-0.85 (lane centre and 3x3 on 16 B texels +0.15-0.3, search +0.15-0.35, ALU +0.15, in-place 7x7 fallback +0.05 under a pan); tests -1.39; mask-span sampler binds -0.05 | 3.5-3.9 (search kept); 3.35-3.6 without | compiled in; 28 taps at L1 on the lane, +0.15-0.35; a `[branch]` on c10.y (the existing vote-only source) is the only off switch and is diagnostic |
| (b) gates / far weight / depth fold in the S4 rows pass | rows +80-100 for one gate per block; a full-resolution depth copy cannot be written by a half-resolution pass | +30 per block for the gate, but the pass must then run on every block (no discard) | rows 0.4 -> about 0.7; the depth copy needs its own full-resolution draw (+0.5-0.6) or MRT2 on the resolve anyway | 4.3-4.6 | the search cannot run here (16 pixels x 28 taps per block); it needs (c)'s draw or (a)'s resolve on top |
| (c) keep a reduced full-resolution draw: search flag + depth copy only, the resolve takes the gates / far weight / vote / emissive | reduced draw about 130; resolve +110 | +10 (draw), +70 (resolve) | reduced draw 0.85-1.0 (reads the lane, writes 4 + 4 B, fixed cost kept; no motion read); resolve +0.1-0.15 | 4.0-4.2 | same-frame for the box gate, 28 taps on the 16 B lane as today's fold, cheapest where it is |
| (d) fold the box pair into the resolve too (E extended): the 7x7 computed in place where `b > a` | resolve +50-60 on top of (a); rows / columns retired, box targets released (59 MB at 5120x1440) | +40 taps x the region fraction | region 10 %: +0.3 (about (a)'s box cost); a screen-filling region under a pan: +2.5-3 against S4's bounded 1.3 | 3.5-3.9 at 10 %, unbounded above | as (a) |

Half-resolution gates (b) lose exactly at the thin edges the feature exists for: a 1 px strut and its background pixel
share one gate; the block minimum closes the strut's stabiliser beside any routed mover (safe direction, but the Run 62
class of silhouette flicker returns on the block edge), the block maximum ghosts the mover; the far weight at half
resolution bleeds the 0.985 weight one pixel across every hull / sky silhouette. The resolve computes the same values
per pixel for about 60 executed slots, so (b) buys nothing it does not lose.

(c) keeps the box gate exact and same-frame and is the fallback if the replay in section 6 rejects (a)'s one-frame
exposure; it saves 0.3-0.5 ms less than (a). (d) is a one-flag experiment on top of (a) (skip the box draws, marker
0 everywhere, the in-place 7x7 does the work): the same build measures both; it is not the default because its worst
case is unbounded by the region size.

## 4. Recommendation (question 3)

### 4.1 Pass order and targets

| # | draw | resolution | program | reads | writes |
| --- | --- | --- | --- | --- | --- |
| 1 | colour copy (8-bit route only) | full | `copy_` | the display surface | scratch FP16 (unchanged) |
| 2 | box rows | W/2 x (H/2 + 1) | `thin_box_rows_half` (gate rewritten, emitter retired) | s0 scene, s1 the **lane** (vote), s7 previous age (held region) | row pair (min, max), MRT x2 FP16 |
| 3 | box columns | W/2 x H/2 | `thin_box_columns_half` (gate rewritten, sky / emitter branch retired, no depth read) | s0 scene, s1 lane, s2 / s3 row pair, s7 previous age | box pair, MRT x2 FP16, `low.a = 1` where computed |
| 4 | resolve | full | `resolve_far_camera_hold` (folded) | s0 scene, **s1 the lane (16 B)**, s2 / s3 / s7 histories, s4 motion, s5 / s6 reactive, s9 / s10 boxes, s11 / s12 linear twins | MRT0 colour FP16, MRT1 age R32F (count + holds), **MRT2 depth R32F** (= lane `.r`, bit for bit) |
| 5 | sharpen / display | full | unchanged | | |

Gone: the tests draw and `line_masks_[0]` (29.5 MB at 5120x1440), the `taa_mask` sampler binds, the three camera
mask programs (`line_mask_camera`, `_depth`, `_depth_thin`), the s8 reads of the box programs and the resolve. The
plain far path (no camera gate: `line_mask` mode 2 and the screen-gate chain) is not flown and stays as it is; it is
the fallback when the camera gate is refused and can be retired in a later cleanup, not here.

MRT count: 3 on the resolve (FP16 + R32F + R32F). D3D9 requires `NumSimultaneousRTs >= 3` and
`D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS` (already required for the age target); the bottle reports
`NumSimultaneousRTs=4 mrt_independent_bitdepths=1` [M]. `camera_gate_available()` gains `render_targets_ >= 3`; below
that the camera gate is refused with one log row, as every other capability refusal in the pass (no second program set).
RT2 leaves the device right after the draw, as RT1 does today; `SavedState` already restores every target up to
`render_targets_`.

### 4.2 The folded resolve

Reads and computes, in the order the program already has (the age read stays before the clip, the Catmull-Rom weight
arithmetic keeps its instruction order as the S3 / A' re-baselines required):

- centre lane texel at s1 (`.r` depth, `.b` view z, `.a` vote); the 3x3 dilation loop reads the lane and keeps the
  winner's `.b` (one more `mov` in the loop);
- far weight `g = validDepth(d) ? saturate((d - c13.x) * c13.y) : 0` (c13.xy = `far_d0`, `far_inv`; the scales stay in
  c11.yz);
- own gate: the camera path at uv with the pixel's own depth, `c9.xyz / .b` where c9.w = 1 (else c8), the routed
  correspondence from the motion at uv (already fetched for the band term's alpha); `r` and `a` exactly as
  `gateOpen()` computes them (openness, the 1e-20 rule for a non-finite routed correspondence, no vote on the sentinel).
  This camera path is a separate variable from the resolve's `cameraUV`, which must stay the rotation-only far-plane
  path (the band term depends on it, resolve.hlsl line 444);
- neighbour gate: the same at `dilatedUV` with `nearest` and the winner's `.b`, from the motion texel already fetched
  there; `ownS = min(r, r_beside)`, `ownC = min(a, a_beside)` as today. When `dilate == 0` the two are one computation;
- flag: vote (`validDepth(.r) && .a >= 0 && .a < 1`) or the search (the four 7-tap lines over the lane's `.r`, the
  `classChange` rule unchanged, first fragmented line ends it; skipped where the vote fired, and under c10.y = 1) or
  the emissive vote (centre luma > E and the 3x3 luma minimum below a third of it: the minimum is accumulated in the
  existing 3x3 clip loop from the raw luma, +3 slots per iteration, the same non-finite rule);
- holds: `flagged`, `closureHold`, `regionHold`, `holds`, `stabilise` composed exactly as now, with `tests.r/g/b/a`
  replaced by the values above, the class term gone;
- box: `boxed` = the block box where `boxLow.a > 0.5`; else, where `stabilise.b > stabilise.a` (the box is wanted),
  the 7x7 min / max of the weighed finite scene in place (a rolled loop of 40 taps beyond the 3x3 already in `low` /
  `high`, `[branch]`); else the 3x3 clip as today. The in-place box is the full-resolution reference box, so the S4
  containment oracle holds trivially on those pixels;
- MRT2 = the centre lane `.r` in every lane (the R32F target stores `.x`).

Gate precision: the first build quantises `r`, `g`, `a` to UNORM8 (`floor(x * 255 + 0.5) / 255`, 3 slots each) so the
folded resolve reproduces today's composition bit for bit wherever the box marker is the same; that is the fixture's
identity oracle (section 6). Dropping the quantisation later is a separate, measurable step.

Constants (all uploaded once per frame with the resolve's block, restored by the state block): c8 `depthParallax`,
c9 `laneParallax`, c10 (E, vote-only source) as the mask draw takes them today; c13 = (`far_d0`, `far_inv`, thin on,
0); c11 and c24 unchanged (S in c11.x becomes 0 with the retirement and the term is removed from the program).
`options.x` (per-pixel motion) and c24.zw (LO, 1 / (HI - LO)) are already the resolve's.

Static size: 616 + about 260-300 -> 880-920 slots [I] (plan 950); first-draw compile about 0.2-0.3 s cold [I] from
the 0.65 s at 4k slots figure. Words about 3,600.

### 4.3 The box gate without the tests target

`boxOpen(p)` in both half-resolution programs becomes `voted(lane(p)) || heldRegion(previousAge(p))`: the lane's
`.a` in [0, 1) with a valid `.r`, or h > 0 in the age fraction at the same unreprojected texel (the read the programs
already do). No `a > r`, no class. Consequences:

- voted geometry (routed thin draws) opens the box the same frame, as today;
- search-only pixels (the Terran lattice's 1 px gaps) open the box from their second frame in the region: the resolve
  writes h = L this frame, the box reads it next frame. On the first frame the resolve's in-place 7x7 covers them where
  `b > a`, so the blend is the reference box, not the 3x3 clip: no one-frame-late edge in the output;
- at rest the box now computes on the held region (today: nowhere, `a = r`). Cost: the region fraction of the
  half-resolution box, about 0.1-0.15 ms at the run312 stand [I]; the resolve multiplies it by `(b - a) = 0` there.
  Under a pan `a > r` holds on nearly every region pixel today, so the block set is the same;
- a pixel with `b > a` only through its neighbour's screen gate, whose block the gate did not open, took the 3x3 clip
  today (the S4 ledger's "half-only pixels") and takes the in-place 7x7 now: the reference box, looser than the 3x3,
  within the accepted bound.

The in-place fallback runs on the search-only leading edge of a moving region (5-9 px per frame at the pan speeds
flown) and on the neighbour-driven edge pixels: 1-3 % of pixels under a pan [I], 40 taps each, +0.05-0.1 ms [I].
Worst case, a large search-only region appearing at once with a valid history (a station entering the frame in one
frame): +2.5 ms for that frame [I]; after a cut there is no history and the current-only return comes first.

### 4.4 Region hold and hysteresis

Unchanged encoding: `(h + 128 (q (L + 1) + t)) / 65536` in the age fraction, h = L on a flag, the closure peak hold
from the camera openness, the screen gate not held, the reprojected read for the resolve and the unreprojected read
for the box. The only change of timing is the box gate's view of a search flag (one frame), which 4.3 covers. The
holds are now computed from float gates quantised to UNORM8, so `closureHold`'s `k = floor(4.5 - 4 own)` sees the same
inputs as today.

### 4.5 Route side (`src/proxy/motion_output.cpp`)

- `TemporalPass::Output::line_mask` is null on the A' path; the `taa_mask` readback (`motion_output.cpp:1838`) has
  nothing to dump. The fixture cases that classify it (`run_motion_output.py` 3879, 4099-4167, 4222: thin-hold and
  thin-vote cases) read the flag from the `taa_age` dump instead (`frac(|age|) * 65536 mod 128 == L` is "flagged this
  frame") and the vote from the `depth` dump's `.a`. The gates themselves are no longer dumped; a diagnostic that needs
  them writes them from the resolve into the age's spare fraction bits or a debug-only target, not in this change;
- nothing else: the lane is already bound as `in.current_depth`, c8 / c9 / c10 are already produced per frame, the
  thin vote and the fade owner keep writing `.a`.

## 5. Native Windows

Documented D3D9 only: `SetRenderTarget(2, ...)` under `NumSimultaneousRTs >= 3` and `MRTINDEPENDENTBITDEPTHS`
(D3DCAPS9 at initialise, refused with a log row otherwise), one ps_3_0 program of about 900 slots created by
`CreatePixelShader` (the modern cap is 32,768; wined3d reports 512 and runs it, measured 2026-09-24), point-sampled
`A32B32G32R32F` and `R32F` textures as the tests draw and the resolve already use, `[branch]` / `[loop]` / `clip` as in
the programs today. The R32F MRT2 write of a float lane is a documented conversion (single channel stored). Nothing
depends on wined3d layouts or Wine exports. Unverified natively (no Windows test host): the mixed-format 3-RT write is
the one native behaviour that only the refusal path covers, and the per-slot cost figure is this backend's.

## 6. Risks and what proves each (question 4)

| risk | mechanism | proof |
| --- | --- | --- |
| look: the box one frame late on search-only pixels | 4.3; covered by the in-place 7x7 | lattice mode: a new `FOLD_FALLBACK` row on the pan scenes (unvoted fragmented bar, the run315 1 px gap / 7.5 px pitch lattice scene) comparing the resolve's fallback pixels against the CPU 7x7 (0 differ); the crawl bound (2.34 rms at rest, the motion-start and stop-after-pan rows) unchanged; the `taa_lattice_gate_replay.py` oracle with the superset gate on the run315 bursts |
| look: box computed at rest on the region | `(b - a) = 0` there | lattice identity row: rest scenes byte-identical to the reference chain |
| look: gate precision | UNORM8 quantisation kept in build 1 | `temporal-pass.txt` and lattice references: every scene where the box marker set is unchanged is byte-identical (the oracle lists the scenes where the superset gate opens more blocks; only those may differ, within containment) |
| look: the search on the lane instead of the copy | reads `.r` of the same texel: identical bits | the thin-source scenes (voted bar over sky, unvoted fragmented bar, voted bar over panel) flag exactly the reference's pixel set |
| depth fold precision | MRT2 = lane `.r`, one instruction, same bytes as S1's COLOR1 | the S1 hash oracle on `depths_` (byte-identical) |
| Reset / device lost | no new resource; `line_masks_[0]` released on the A' path; `before_reset` unchanged; RT2 unbound after the draw or on loss as RT1 | `run_temporal_pass.py` Reset row (`Reset ok`), the state row with RT2 in the restored set, the lost-device case |
| stream-offset defect (ledger, filed 2026-09-25) | `D3DSBT_ALL` block created on the first run does not take later stream offsets at `Capture`; this change edits the same `SavedState` path (RT2) | fix it first or alongside (a small separate change: re-create or refresh the block when the captured offsets differ), and run the state-check cases from a non-hostile initial state so the RT2 restore is not masked by the same defect |
| alt-tab / double cursor | no window or presentation change | the standing check on the next flight; no new evidence needed |
| MRT count below 3 | refusal path | a fixture row with the caps override (the runner has `FLICKER_CAPS`; add `render_targets=2`): camera gate refused, reason row, plain far path runs |
| compile time / slot count | 880-920 slots | RESOLVE_BUDGET row `far_camera_hold` within the 2048 ceiling; the first-draw timing row |
| performance | section 3 | the fixture's timing row at 5120x1440 (tests 0, resolve, box at rest and under the pan scene); flight `--gpu-sync-timing` at the run312 stand: `taa_mask_tests` absent, `taa` 3.5-3.9 expected |

Fixtures named by the brief: `run_temporal_pass.py` (744/278 today; rows that legitimately change: RESOLVE_BUDGET
`far_camera_hold`, the three camera mask rows gone, `depth_fold_reason` `lane_mrt` -> `resolve_mrt`, the thin-vote
reason rows now from the resolve, the state row with RT2), the lattice set (610/107 plus the new rows above), and the
motion cases `seam-taa-thin-vote-far-on-source-{both,screen,vote}`, `seam-taa-thin-hold-half` (the `taa_mask`
assertion moved to the age dump) and the cutout-owner cases (`.a = 1` is no vote: unchanged).

## 7. Sentinel-class retirement (question 5)

Folded into the same change. The class code lives in the tests draw (`classCode`, `carriesClass`), the box gate
(`emitter.y`, the bright-tap code of the rows, the sky / emitter branch of the columns with its four depth reads) and
c11.x / c23 of the resolve and box programs; the fold rewrites every one of those sites, so retiring separately would
mean writing the class into the folded resolve and then deleting it. The retirement's own saving (about 1.0 ms at rest
[I]: the box no longer opens over the sky) is reported separately from the fold's in the flight, by flying the
retirement build's `--gpu-sync-timing` first if the orchestrator wants the split measured; the fixture timing row gives
both.

Retire: `X3M_TAA_SENTINEL_STABILISER` / `--taa-sentinel-stabiliser` and `sentinel_strength`, `sentinel_emitter`,
`configure_sentinel()` / `sentinel_available()`; `thin_box_rows_hold` / `thin_box_columns_hold` (the full-resolution
separable pair exists only for the stabiliser; the full-resolution 49-tap `thin_box_hold` stays for odd sizes);
c23 and `fp16_above` in the box path; `carriesClass`, `classCode`, c11.x; `box_rows_failed_` for the full-resolution
rows. Keep: `thin_region_emissive` (its own opt-in), the fade owner. Fade-rt2-ownership.md section 5 lists the same
code with its 36-slot figure for the tests draw; that program is retired whole here.

## 8. Unknown, and what settles it

- The resolve's cost with 16 B depth texels in its 3x3 loop and the search (the +0.5-0.85 bracket): only the fixture
  timing at 5120x1440 settles it. If the resolve grows by more than 1.0 ms the fold is a wash and (c) is the answer.
- The box's cost with the class off and the superset gate at rest (the region fraction): the same timing row on the
  lattice pan scene and the run312 stand replay; no measured figure exists today (run313 has no `--gpu-sync-timing`).
- Whether the UNORM8-quantised float gates equal the tests draw's on every pixel (the UNORM write rounds to nearest
  on this backend; `floor(x * 255 + 0.5)` matches nearest-even except at exact halves): the identity row shows it or
  names the pixels.
- Native: the 3-RT mixed-format write and the 900-slot program are cross-compiled, not run.

## 9. Considered and why they lose

- **(b) gates in the S4 rows pass**: half-resolution gates lose at thin edges (section 3) and cannot write the
  full-resolution depth copy; the search cannot run there.
- **(c) reduced full-resolution draw**: exact and simplest, but keeps the fixed draw cost and the 16 B lane read, so it
  recovers 0.3-0.5 ms less than (a); it is the fallback if (a)'s replay fails.
- **(d) no box passes, 7x7 in the resolve where `b > a`**: unbounded by the region size (+2.5-3 ms on a screen-filling
  region); available as a one-flag A/B on the (a) build.
- **Box gated on last frame's hold only, 3x3 clip on the first frame** (the lifted-cap note's (d) objection, the
  Run 59 class of edge): the in-place 7x7 removes the objection for 0.05-0.1 ms.
- **G32R32F depth history carrying the flag / gates for the box**: doubles every depth read (+0.18 ms per set) and
  still lags one frame for the box.
- **CPU-side skip of the box when the camera is still**: exact only without translation (near geometry under
  translation moves faster than the far-plane corners); the region-only gate at rest costs 0.1-0.15 ms and needs no
  new invariant.
- **Every-other-frame mask (G)**: the 2-frame flicker of the treatment under a pan, rejected before
  (taa-thin-geometry-alternatives.md section 3.6).
- **Half-resolution search**: refused by the constraint (1 px gaps at a 7.5 px pitch need a per-pixel test); a
  half-resolution detector would have to prove it catches them, and the block form of section 3 (b) shows what it loses.
