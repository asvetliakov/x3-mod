# Moving-lattice crawl: independent review of the current line of work (2026-09-21)

Status: design review for ratification. No code, build, Wine or game run; one host measurement on
run201 (section 4). Tags: **[M]** measured (this session or cited with its source), **[I]** inferred,
**[A]** assumed. Inputs: [taa-lattice-crawl.md](taa-lattice-crawl.md) §§1-31,
[mesh-buffer-rewrite.md](../reverse-engineering/mesh-buffer-rewrite.md), the three lattice-upload
contracts, [ownership-shadow-lifetime-diagnosis.md](ownership-shadow-lifetime-diagnosis.md),
[temporal-integration.md](temporal-integration.md), [taa-distant-line-fade.md](taa-distant-line-fade.md),
`src/temporal/resolve.hlsl`, `line_mask_ps.hlsl`, `rigid_motion_ps.hlsl`,
`verification/results/run201-lattice/`, `/tmp/x3-bottleX3-run201`. Closed decisions respected: no
MSAA/SSAA, no global blur, no jitter reshuffle, no luminance clamp, nothing that weakens TAA globally.

## 1. Verdict

The geometry-capture line (§§17-31, B1/B2a/B2b) is technically sound as engineering but is not on the
path to the crawl fix. It answers "what bytes did the game upload for two meshes", a question whose
answer is already known to IoU 0.998 from the parsed `terran_spp_panel.pbb` (§17 **[M]**), and whose
remaining uncertainty (2 of 14 tile pixels, face42/face47, §§21-24) is about reproducing the game's
rasterizer bit-exactly, not about why the resolve crawls. The crawl's mechanism under rotation is
already determined by the resolve design: the accepted stationary remedy (`--taa-thin-region 0.97`)
is switched off by its own speed gate above 0.25 px/frame, and the truss moves at 1.4-10 px/frame in
every rotation capture (§14 **[M]**, section 4 **[M]**). The user therefore sees the ordinary w = 0.9 resolve
with its 3x3 clip on a fringe pattern wider than 3x3, exactly the run176 baseline crawl, translating.

Recommendation: **pivot to a resolve-side replay experiment first** (camera-relative gate plus a wide-box
clip on the fragmented region, section 5 option A), a host-only job on captures already on disk, and
in parallel design the **program-level footprint dimming** of the lattice draws (option B), which needs
no geometry payload. **Park B2a/B2b at their current qualified checkpoints** (committed, default-off,
unwired); do not spend the next flight on the payload. Finish the ownership shadow lifetime fix on its
own merits: it is a real production defect, not lattice progress.

## 2. What is established

| Fact | Source | Tag |
|---|---|---|
| Stationary crawl on the edge-on arm = fringes of the foreshortened lattice toggling geometry/sky across the 8 jitter phases; 100-160 code swings; history accepted, clip discards it because a fringe is wider than 3x3 | §12 | M |
| Thin region (fragmented 7-tap mask, 11x11 grow, clip off, W 0.97) removes it at rest: arm rms 16.9 -> 2.34; user confirmed on run177 | §13, §13.1, §14 | M |
| Region strength is closed by the fastest pixel within 8 px above HI = 0.25 px/frame (`line_mask_ps.hlsl` compose stage, `b *= 1 - a`) | shader, §13 | M |
| run177 rotation: region speed p50/p90 6.85/9.23 px/frame, gate active on 0.07 % of region pixel-frames | §14 | M |
| run201 rotation bursts: routed-pixel speed p50 3.9 px/frame (frame 10450), 1.4-1.7 (12010/12018); stationary burst p50 = p99 = 0.30 (the previous-row jitter bias, not motion) | section 4 | M |
| Motion vectors are exact: rigid replay fits the truss to 0.001/0.002 px median/p99 | §15 | M |
| Variance clip never binds on a binary lattice at rest (H2), Catmull-Rom is not the term (H3), the creep floor is reconstruction of 0.7-px lines at 3.8-px pitch (H4) | §11 | M |
| Retaining W 0.97 under coherent camera motion with clip off gains 28.9 % tracked rms (Keys -0.65) but admits a 236-code stale patch against 47 for the clipped resolve | §15 | M |
| Analytic source coverage cuts geometry-coverage rms by 75 % but no displayed-RGB or TAA result follows; the RGB admission failed | §19, §20 | M |
| Mesh source identified; 22/22 triangle counts, 20/22 vertex counts match; projection IoU 0.998 | §17 | M |
| Per-draw ID is CPU-side only: RT3 is unbound in all six run201 draw records; no ID target exists in `motion_output.cpp` / `temporal_pass.cpp` | run201 compact report, grep | M |
| The ownership shader-shadow bug (dead wrapper restored after injection) is real and independent of the lattice | lifetime diagnosis | M |

## 3. (a) The geometry-capture hypothesis

**What the chain is.** Payload (466,224 bytes of two VB/IB pairs at the CloneMesh seam) -> GPU fixture
with observed geometry -> explain the 2/14 dark-centre discrepancy -> a qualified "raster oracle" ->
source-coverage RGB model -> a renderer change that renders the struts with coverage instead of binary
raster. Each arrow after the first is unbuilt and the last two failed once already (§18 rejected, §20
negative). The notes themselves say so at every checkpoint ("does not establish a visible crawl
correction", §§21-31).

**What the payload settles.** The 3 extra runtime vertices and the D3DX split provenance, FP16
rounding (nearest vs truncate: §31 rounding pilot changed 0 owners), and whether the reconstruction
used for the fixture equals the live upload. **What it cannot settle:** texture parity, the
simultaneous draw-input transaction, later writers, historical run177 ownership (§25, §31), and above
all the displayed crawl, which is a resolve-stage phenomenon independent of which of two coplanar faces
owns a pixel.

**Is there a renderer change at the end?** Only two are plausible in D3D9 without MSAA, and neither is
enabled by the bytes:

1. *Strut widening with coverage alpha* (vertex shader expands sub-pixel struts to 1 px and scales
   colour by true width / 1 px). Needs per-vertex strut adjacency, i.e. a per-mesh preprocessing of
   VB/IB at load; the CloneMesh seam would then be a **rewrite** seam, a much larger feature than the
   diagnostic (blend order, Z write, 16 instances x 22 groups, Reset, native parity). Not designed,
   not costed. The parsed `.pbb` already gives the adjacency to design it offline. **[I]**
2. *Program-level footprint dimming* (section 5 option B): a constant per frame per lattice program.
   Needs no geometry at all.

**Proportionality.** The machinery built for this diagnostic: private CloneMesh interval RE, portable
staging core (39 cases/596 assertions), SJLJ ABI shell (158 checks), readable-MANAGED prerequisite
(157), manual observer (421), callsite adapter (389), pin lifecycle (386), schema-2 reader (47 tests),
plus B2a/B2b pending (174 and 141 changed lines). All qualified, all reviewed **[M]**. Against that: the
information gain over §17's reconstruction is at most a few vertices and one rounding mode, and no
decision about the crawl depends on it. This is disproportionate for a diagnostic. It would be
proportionate only as the foundation of the mesh-rewrite feature (1 above), which nobody has ratified.
The one lasting asset is the lifetime defect it surfaced.

**Verdict on (a):** not sound as a crawl hypothesis; sound as low-priority provenance work. Do not
finish it to a flight now.

## 4. (b) Physical cause of rotation crawl

Ranked by evidence:

1. **The stationary fix is gated off, so the baseline fringe ripple returns, translating** **[M + I]**.
   §14 measured run177 rotation at 6.85-10 px/frame; this session measured run201 (`motion_1_*.rgba32f`,
   RG = previous UV, alpha 1 = routed; speed = |prevUV - pixel centre| in px, uncorrected for the
   ±0.5 px previous-row jitter): frame 9796/9804 stationary p50/p90/p99 = 0.304/0.306/0.306 (pure
   jitter bias, true speed ~0); 10442 0.47/0.54/0.81; 10450 3.92/5.40/6.19; 12010 1.42/2.11/2.40;
   12018 1.66/4.23/6.48. The gate closes at 0.25 px/frame. With `b = 0` the far/thin program is the
   plain resolve bit for bit (§13 fixture), i.e. w = 0.9 with the 3x3 clip. At rest that configuration
   is the run176 baseline the user called crawl. Nothing further needs to be true for the user to see
   crawl during rotation; this is the null hypothesis and it is sufficient.
2. **Fractional reprojection blur of the history** **[M at slow speed, I at fast]**. Every history fetch
   under motion lands at a fractional offset; the strut's history is a blurred line (contrast x 0.54
   under 0.07 px/frame drift, §3) blended 0.9 with a binary current sample. The 10 % binary term is the
   same ripple as in 1; the blur itself is not crawl. At 7 px/frame the beads of §3 slip 20-40 px/frame
   and fuse; the slow creep floor (H4) is not what the user sees while rotating.
3. **Reprojection error of thin features** (H1). Undecided under motion in §11, but §15 fits the
   truss motion to 0.001 px; residual error would be a per-pixel sub-0.1 px term, small against 1.
4. **Depth/ID mismatch, sky/geometry dilation**: closest-depth 3x3 dilation gives the gaps between
   struts the strut's motion; under pure rotation the far-plane camera path and the strut path
   coincide, so no term. Under ship translation (run201 burst 10442 also translates 24,634 raw units)
   parallax between struts and sky enters, bounded by the clip. Secondary.
5. **Motion-vector quality on same-draw output**: excluded by §15's fit.

**Cheap checks from evidence on disk (host only, no engine work):**

- Replay `taa_resolve_replay.py ... thin` on run177 6392-6423 and run201 10442-10473 / 12010-12041 with
  the gate forced open on the region (or `GATES=0.03:99`): the tracked rms (material tracking of §15,
  scripts in `/tmp/x3-motion-lattice-replay/tools/analysis/`) against the installed configuration
  answers directly whether the crawl is item 1. Expected from §15: 20-30 % rms reduction with clip off.
- The same replay with a 7x7 (or region-wide) min/max clip instead of clip off, plus §15's 6x6
  stale-patch injection: bounds the ghost admitted by the open gate. Not yet measured anywhere.
- Screen recording: `screenshots/lattice.mov` is a standstill (§12). The three `lightmap*.mov` of
  2026-09-21 are unclassified; the user should say whether any shows the plant while rotating. A
  recording of the rotation would let the §12 method (movie against dump, 7.5 Hz spectrum) confirm the
  presented ripple frequency under motion.
- `verification/results/run48a-lattice-triage/reconstruct_gate_witness.py` applied to run201 confirms
  gate closure with the actual mask operators (the measurement above used raw motion only).

## 5. (c) Alternatives

| # | Option | Mechanism | Evidence so far | Cost on the hot path | Risk / why it loses or wins |
|---|---|---|---|---|---|
| A | **Camera-relative thin-region gate + wide-box clip** | Gate on `|routed - camera path at depth|` instead of screen speed, so a coherent pan keeps W 0.97 on the fragmented region; replace clip-off with a 7x7 (or 11x11 region) min/max box so only colours absent from the neighbourhood are rejected | §15: 28.9 % rms with clip off (Keys -0.65), unsafe; wide-box clip unmeasured | Mask shader already computes both paths (`gateClosure`); the box needs one more separable min/max pass (two channels, `a` is free) or 48 fetches in the resolve; zero per-draw CPU | Wins if the wide box bounds the stale patch to about the clipped resolve's 47 codes while leaving the fringe ripple untouched (both extremes are present in any 7x7 of a lattice). Blur of the lattice under motion is the price and is confined to the region; ships crossing the region ghost for up to 1/(1-W) frames, as accepted at rest |
| B | **Program-level footprint dimming** of the lattice draws (ps `5e0a10fe` / vs `4944d81d`, materials 4/6/18) | Colour x `saturate(k * strut_width / footprint_per_px)`: a coverage estimate with one constant strut width per program; opaque + alpha test unchanged, so no sorting or Z change | §10/§11: "fade to half contrast is x 0.5 crawl by construction"; light-map far fade accepted by the user with the same trade | One constant per frame per routed program; one MUL in the injected PS variant; nothing in the resolve | Attacks the alias contrast at the source under any motion. Dims toward black: correct over space and dark cells (the arm, the panel interiors), wrong over a bright nebula (bounded). Needs the route to vary a PS variant per program, which the motion-output variant generator already does **[I]**; strut width from the `.pbb` (about 60-100 units, §12 footprint x 0.7 px) |
| C | Thin-feature history weight driven by motion magnitude (no clip change) | W raised on the region, clip kept | §13: clip on leaves 74 % of the ripple; §12: w 0.97 with clip on 14.0 vs 14.8 | none new | Loses: the clip is the discarding stage |
| D | Per-draw ID target (RT3) for ID-stable clamp relaxation | Write a draw/program id per routed draw, relax the clip where the id is stable | none; RT3 unbound today | One more MRT write on every routed draw, a 4th target's memory, every routed PS variant changed | Too broad for this problem; ID stability is what the depth-class fragmentation mask already approximates. Keep as a later general tool |
| E | Coverage dilation into history (geometry-independent) | Widen thin current features by one pixel before blending | none | 9 taps | Loses: it is a spatial blur of every silhouette in the region; §10 showed blurring harder removes beads without moving the crawl |
| F | Finish B2a/B2b to a flight | Payload capture | §§17-31 | Diagnostic only | Loses on payoff (section 3); park |
| G | Accept a bounded residual | Document the gated-off regime | run177 user verdict | none | Acceptable only after A or B is tried; the user has asked for the fix |

## 6. (d) Recommendation and next three steps

**Pivot.** Do not fly the payload. Land the lifetime fix independently. Then:

1. **Host replay of option A on existing captures** (`verify`/`triage`-class task, no Wine). Inputs:
   run177 6392-6423, run201 10442-10473 and 12010-12041, run159 (slow) and run177 5674-5705
   (stationary) as controls. Variants: installed; gate open on the region with clip off; gate open with
   7x7 box clip; gate open with 11x11 box clip; each with plain Catmull-Rom and W 0.97. Report §15's
   tracked rms, gradient energy, the 6x6 stale-patch witness at (950,115)/(947,123), and background
   trail p99 on run161 (0.84 px/frame, the trail case of §13). **Acceptance:** tracked rms <= 0.80 of
   installed, gradient >= 0.90, stale patch <= 2 x the clipped resolve's 47 codes, run161 trail p99 not
   above the installed thin region's; stationary and slow controls bit-identical to today. If it passes
   -> implement as a gate mode of `--taa-thin-region` (fixture: the lattice mode with a camera-panning
   shard scene plus a bright crossing patch), then flight. If clip-off is the only passing variant,
   stop A: the §15 ghost verdict stands.
2. **Option B design and proxy replay.** Confirm from the run177 packets and the `.pbb` parse (§17,
   `/tmp/x3-lattice-ownership`) that the lattice programs draw only struts (no panel faces) and what
   strut width they carry; replay run177 rotation with the raw HDR of region pixels scaled by 0.5 to
   confirm tracked rms about x 0.5; then a `design`-grade check of the variant generator's ability to
   inject a colour scale per program and of the footprint source (`--cull-small-parts` already has
   `s = r*640/D` at `0x0047d2a2`; the far gate has `footprint = 2z/(p00*width)`). **Acceptance:** replay
   x 0.5 +- 0.1; a seam fixture case asserting the scaled colour on the program's draws, RT1/RT2 and all
   other draws bit-identical. Implement only if step 1 fails or leaves a visible residual.
3. **The single most useful flight** (after 1 or 2 ships a candidate): the plant at the run177
   position, 64 frames, **slow pan** (about 0.1-0.5 px/frame on the truss, near the gate knee) and a
   **fast pan** (the user's usual speed), baseline vs candidate, `--taa-debug` F8 at each, plus a screen
   recording of each pan. No slow-pan capture exists (§14 asked for one); it separates "gate closed"
   from "history blur" regimes and gives the replay its first ground truth under motion. The user
   supplies: the two captures, the recordings, and one sentence per pan on whether the crawl is gone,
   reduced, or replaced by blur or trails.

**What to do with B2a/B2b:** keep the worktrees, review-merge them as default-off, unwired code only if
the reviewer finds them harmless to the build (they add no per-draw work off), else leave them in
their worktrees with the checkpoint records already committed under `run201-lattice/`. Reopen only if
a mesh-rewrite feature (section 3, item 1) is ratified, for which the CloneMesh seam is the right place.

## 7. Unknown, and what settles it

- Whether a wide-box clip bounds ghosts under an open gate: step 1 replay, no new capture.
- Whether the user's perceptual threshold is met by a 20-30 % tracked-rms gain: only the flight.
- Whether the lattice programs draw struts only, and the strut width in render units: `.pbb` parse
  plus the run177/run201 packets (materials per draw are already in the ownership report **[M]**).
- Whether the motion-output variant generator can scale colour per program without a new shader
  family: read of `motion_output.cpp`'s variant registration, a bounded `Explore`.
- The `lightmap*.mov` recordings' content: ask the user.
- Native Windows: every option here uses `tex2Dlod`, constants and the project's RT1/RT2 contract;
  none is Wine-specific; none is runtime-verified on Windows, as for the existing resolve.

## 8. Hot path and native behaviour of the recommended option

Option A: no per-draw CPU work; the mask pass already computes the pixel's own and the camera path
(`gateClosure`), so the relative speed is a subtraction; the box clip is either one more separable
pass over the two mask targets (two channels; measured pass costs today: far +0.19 ms, thin region
+0.67-0.69 ms at 1280x768, §13.1) or 48 extra current fetches on region pixels in `resolve_far`
(496 of 512 slots today, so likely the separable pass). Option B: one constant and one MUL. Both fit
ps_3_0 and documented D3D9; native Windows behaviour is source-compatible and unverified, as for the
rest of the temporal pass.
