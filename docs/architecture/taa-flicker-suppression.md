# TAA flicker suppression without global blur

**Ratified 2026-09-19 (orchestrator).** Steps 0–3 go into one candidate, all default-off, in this order, each
verified by the temporal-pass fixture before the next: step 0 must be bit-identical (program hashes of the
resolve's live path may change only by the removal of the snapshot modes; resolved output identical on every
existing fixture frame); the drifting-lattice fixture cases (0.25 / 0.4 / 0.6 px per frame, pitch 4 and 2.37,
depth-sentinel background, per-pixel and 8-px block band rms plus contrast) land with step 1 and are the oracle
for steps 1–2; the launcher refuses `--taa-adaptive-weight` without `--taa-thin-clip` (alone it dims thin
lattices); every program stays within 512 ps_3_0 slots, and the 521-slot filtered variant is brought under 512 by
step 0 as a side effect. Step 4 waits for Flight A. Sequence redesign and the selective LOD bias stay rejected.
The current-sample filter stays as an option but is not part of the plan (it does not touch the slow band).

Status: design proposal, 2026-09-19, for ratification. Nothing here is implemented. Evidence base:
`docs/verification/motion-output.md` ("Run 139", "Run 44 B", "run148/run149"), `src/temporal/resolve.hlsl`,
`src/renderer/temporal_pass.cpp`, `src/temporal/rcas.hlsl`, `src/temporal/bloom_common.hlsl`, and the scratch
measurements of section 9. Tags: **[M]** measured, **[I]** inferred from a model or source, **[A]** assumed.

## 1. Decision

Two resolve-side changes that act only on thin / coverage-toggling pixels, plus one bloom-input fix; stable
surfaces keep today's path bit for bit (no current-sample filter, same sharpen, `--taa-mip-bias -0.5` stays).

1. **Thin-feature soft clip.** Where the current 3x3 depth mixes the sentinel with geometry (by construction
   the measured "flip + edge" classes, >= 92 % of the flicker energy), the clipped history is pulled only part
   of the way to the clip box: `old = lerp(clamp(old), old, s)`, `s` about 0.75, faded out by screen speed
   above 2 px/frame. This is the lever for the **moving** lattice and the precondition for lever 2.
2. **Age- and speed-dependent history weight.** `w = min(n / (n + 1), w_max(speed))`, `n` a per-pixel
   accumulated-frame count in a second render target; `w_max` 0.97 for slow content, returning to 0.9 at a
   speed set by two launcher constants. This is the lever for the **static** jitter-period ripple.
3. **Alpha history on the HDR route**, because bloom's authored-glow term reads an unaccumulated alpha.

Enabler: move the resolve's snapshot modes into their own program, 507 -> 433 slots **[M]**; with all of the
above the main program measures 475 slots, 487 with the current filter on top (guaranteed limit 512) **[M]**.

## 2. Facts

- Static scene (run142, 0.08 px/frame): the residual is the `(1 - w)` ripple of a period-8 input; resolved
  fast-band rms 1.49 / 2.42 codes on flip px against raw 27.0 / 21.1, the w = 0.9 gains **[M]**. A coverage
  model (random edges/lines through Halton-8) gives resolved/raw 0.078; the dumps give 0.083 **[I vs M]**.
- Drifting scene (run148/149, 0.4-0.6 px/frame): on flip px the period 8-32 band is 11-12 codes, period 4-8
  4.2-5.4, period 2-4 2.3-3.1; presented flip-px mean 3.9-4.9 **[M]**. The metric is per fixed pixel, so it
  contains true motion: an ideally antialiased lattice has 3-7 codes in that band at these speeds and up to
  40 at 0.1-0.25 px/frame **[I, section 3]**. 11-12 is an upper bound on the artefact, not its size.
- `--taa-mip-bias 0` vs -0.5: no visible difference, flip count -4.6 %, ripple not lower **[M]**. The selective
  LOD bias on alpha-tested draws is **dropped**; the -0.5 default stays.
- Gains of the exponential average at period 8 / 4 / 2: w 0.9: 0.136 / 0.074 / 0.053; 0.95: 0.067 / 0.036 /
  0.026; 0.97: 0.040 / 0.022 / 0.015 **[I, closed form]**; fixture lattice W 0.95 -> 0.481 **[M]**.
- History colour is `A16B16G16R16F`; its alpha is **not free**: the resolve writes the current alpha, the
  write-back carries it and `bloomPrefilter` uses it as the authored-glow weight **[M, source]**.
  `TemporalPass` already owns R32F render targets; the route already requires
  `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS` (`motion_output.cpp:915`, `:2420`) **[M, source]**.
- Bloom's RGB source is already the resolved FP16 scene; the luminance weighting is applied to the current
  sample, every neighbour and every history tap before clip and blend; history resampling is already
  Catmull-Rom over 16 point taps **[M, source/docs]**.
- RCAS 0.75 raises the presented mean 1.02 -> 1.82 (plant), 0.88 -> 1.24 (station) **[M]**.

## 3. The moving sub-pixel lattice (1-D model, **[I]** throughout)

Model: lines 0.8 px wide, pitch 2.0 / 2.37 / 2.8 / 3.1 px, contrast 150 codes, point-sampled with the Halton
x-jitter, drifting v px/frame; the resolve's reprojection, Catmull-Rom history, 3-sample min/max intersected
with mean +/- 1.25 sigma, blend w; 128 analysed frames. Axis-aligned lines are the worst case for the clip
(three distinct columns only), so absolute numbers are pessimistic; ratios are the content. Two metrics:
per-pixel band rms (what the capture triage reports; contains real motion) and the band rms of **8-px block
means**, which an ideal antialiased moving lattice keeps below 1 code at v >= 0.4 (4 at v <= 0.25) and which
is what the eye sees of an unresolved lattice: brightness crawl. "Contrast" = spatial std / ideal.

| v | config | per-px p2-4 / p4-8 / p8-32 | block p2-4 / p4-8 / p8-32 | contrast |
|---|---|---|---|---|
| 0 | base | 9.3 / 9.1 / 0 | 5.3 / 5.4 / 0 | 0.55 |
| 0 | soft 0.75 | 5.7 / 4.7 / 0 | 2.5 / 2.7 / 0 | 0.91 |
| 0 | w 0.97 | 3.9 / 3.8 / 0 | 2.7 / 2.6 / 0 | **0.22** |
| 0 | w 0.97 + soft 0.75 | 2.3 / 2.3 / 0.1 | 1.2 / 1.4 / 0 | 0.53 |
| 0.4 | base | 6.4 / 9.9 / 11.5 | 3.9 / 3.5 / 7.9 | 0.42 |
| 0.4 | soft 0.75 | 4.1 / 9.4 / 7.8 | 1.7 / 1.9 / 5.0 | 0.32 |
| 0.4 | w 0.97 | 2.9 / 3.9 / 5.5 | 2.1 / 1.7 / 3.8 | 0.19 |
| 0.4 | w 0.97 + soft 0.75 | 1.5 / 3.3 / 3.9 | 0.8 / 0.8 / 2.5 | 0.14 |
| 0.4 | no clip (bound) | 3.4 / 9.4 / 4.5 | 0.8 / 1.3 / 2.2 | 0.26 |
| 0.5 | base | 7.0 / 10.7 / 15.7 | 4.8 / 2.7 / 5.4 | 0.50 |
| 0.5 | soft 0.75 | 5.5 / 10.2 / 13.5 | 2.4 / 2.3 / 4.1 | 0.43 |
| 0.5 | w 0.97 + soft 0.75 | 1.9 / 4.0 / 13.2 | 1.0 / 1.0 / 4.9 | 0.36 |
| 0.6 | base | 7.7 / 10.0 / 9.5 | 3.9 / 5.0 / 7.3 | 0.39 |
| 0.6 | soft 0.75 | 5.8 / 9.1 / 6.9 | 1.4 / 2.8 / 5.2 | 0.31 |
| 0.6 | w 0.97 + soft 0.75 | 1.9 / 3.5 / 3.1 | 0.6 / 1.3 / 2.5 | 0.13 |
| 0.5-0.6 | filter A 1.0 | -20..-35 % / -25..-40 % / -8 % | 0 / 0 / 0 | 0.32-0.44 |
| 0.5-0.6 | linear instead of Catmull-Rom | 0 / -11..-23 % / -3..-22 % | 0 / 0 / 0..-14 % | -8..-14 % |

What the model says:
- **Clip collapse is the moving-lattice mechanism.** On the phases where none of the neighbourhood samples
  lands on a line the box degenerates to the background and the clamp erases the accumulated line; the next
  covered phase re-admits the aliased sample at weight 0.1 and the erased history takes 10+ frames to
  rebuild. The harmonics this leaves unfiltered beat against the drift (harmonic 5 of a 2.37 px pitch at
  0.5 px/frame aliases to an 18-frame period): the slow crawl. The model's per-px slow band (9.5-15.7) is the
  size of the measured one (11-12). Removing the clip cuts the block-mean flicker 3-5x in every band at every
  speed; widening gamma to 2 without the min/max box changes nothing (run 139 found the same).
- The **current-sample filter does not touch the slow band** (-8 % per px, 0 on block means); it only
  low-passes the fast bands, at a contrast cost. Catmull-Rom is already in and is the right choice.
- **A high weight alone dims a thin lattice**: static contrast 0.55 -> 0.22 at w 0.97, because the history
  erased by a collapse refills at 3 % per frame instead of 10 %. Without the clip the contrast is
  w-independent (1.25 -> 1.28). So lever 2 must not ship without lever 1; with soft 0.75 the contrast is back
  at 0.53 and the fast bands are at 25 % of today's.
- Under drift a long memory trades crawl for contrast: at 0.4-0.6 px/frame w 0.97 + soft takes the slow
  block band 7.3-7.9 -> 2.5 (-66 %) while the lattice contrast falls 0.4 -> 0.13 (a moving 1-px lattice
  averages to grey); at exactly 0.5 px/frame the beat is not reduced. Whether grey-while-drifting is
  preferable to crawl is a user judgement, hence the speed gate is two launcher constants, flown both ways.
- 2-D caveat: real lines are oblique and the 3x3 has nine samples, so collapses are rarer than in 1-D. Run 139
  measured the clamp moving 5.1 % of plant px and a -15 % static gain from disabling it; the drifting share is
  unmeasured. The fixture's drifting lattice (section 7) measures the real shader.

## 4. Design

### 4.0 Step 0 — split the snapshot modes out (no behaviour change)

`options.z > 0.5` draws get their own ps_3_0 program; the main resolve drops to 433 slots and the filtered
variant to 444 (today 521), closing the portability gap of 2026-09-19. Gate: `run_temporal_pass.py` readbacks
bit-identical (508 / 278 / 386 samples), `RESOLVE_BUDGET within_guaranteed_512=1` for every variant. The
plain bytecode hash changes; the off-path guarantee becomes "output bit-identical", which the lattice case
already checks over 128 frames. No flight.

### 4.1 Step 1 — thin-feature soft clip (`--taa-thin-clip S`, 0..1, default off)

- Mask: in the existing 3x3 depth loop, record "saw a valid depth" and "saw the sentinel" (the centre
  included); `thin = both`. No extra fetch; stateless; independent of the colour, so it cannot bias the
  average toward one coverage state. It is phase-stable: a +/-0.5 px jitter keeps a line inside the 3x3.
- Action: `old = lerp(clamp(old, low, high), old, S * thin * (1 - saturate((speed - 2) / 2)))`.
- Expected **[I]**: static fast bands -40..-50 % on axis-aligned lattices, about -15 % on the real plant
  (run-139 clamp share); drifting 0.4-0.6 px/frame: block-mean period 2-4 -50..-63 %, period 4-8 -17..-46 %, slow band -23..-37 %,
  per-px slow band -14..-32 %; lattice contrast static 0.55 -> 0.91 (thin lines get brighter and sharper),
  drifting -15..-25 %. Measured slots: 446.
- Ghosting: the mask is also true on every silhouette against the background, one pixel wide. A stale colour
  there decays by `S * w` = 0.68 per frame instead of being clamped at once; a silhouette moving faster than
  1 px/frame leaves the mask after one frame, above 2 px/frame the term is off. Lattice in front of
  *geometry* (no sentinel in the 3x3) is not covered; the depth-jump test is unreliable at depth -> 1.

### 4.2 Step 2 — age/speed history weight (`--taa-adaptive-weight WMAX[,LO,HI]`, default off)

- State: R32F ping-pong pair beside the colour history, written as `COLOR1` of the resolve, one point fetch
  at the nearest reprojected history texel. Early returns write `n = 1`; the blend writes `min(n + 1, 64)`.
- `w = min(n / (n + 1), WMAX - (WMAX - history.z) * saturate((speed - LO) / (HI - LO)))`; defaults LO 0.1,
  HI 0.5 px/frame (drift keeps today's 0.9), alternative for the flight LO 0.8, HI 1.5 (drift gets 0.97).
- Expected **[I]**: static jitter ripple x 0.29 -> resolved flip-px fast band 2.84 -> about 0.9, scene mean
  1.02 -> 0.30-0.41, presented 1.82 -> 0.55-0.75 with the sharpen untouched; static sharpness unchanged
  given step 1. After a cut the first 8 frames are an exact running mean over one jitter cycle: supersampled
  in about 8 frames instead of 20. With the wide gate: drifting per-px fast bands -60..-77 %, slow band -10..-68 % (least at exactly 0.5 px/frame),
  contrast of a drifting 1-px lattice down to 0.13-0.36. Measured slots with step 1: 475.
- Risks: legitimate in-box changes on still pixels (blinking lights reach 53 % in 25 frames instead of
  93 %); FP16 stall error up to 0.0003 / (1 - w) = 0.01; clamp-bounded ghosts on still pixels last 3.3x
  longer. If blinking lights regress, restrict `WMAX` to `thin` pixels (the mask is already there).

### 4.3 Step 3 — alpha history, HDR route only (`--taa-alpha-history`, default off)

On the far objects of run142 the authored-glow term, proxied as the 16x16-block mean of `luma x alpha` of the
resolved dumps, flickers **35-38 % of its mean** with the current alpha and 1.4-2.4 % with a time-averaged
alpha; resolved luma flickers 0.3-0.9 % per block; 1002 station px and 156 plant px swing alpha by > 0.25
**[M, section 9]**. Bloom is fed a resolved colour and an unresolved mask, so small authored glows pulse
**[I]**. Whether the user's windows are authored glow or light-map highlights is **unknown**; the highlight
term uses resolved RGB and is steady, and the weighting already bounds bright current samples before the
clip. Design: blend alpha with the same `w`, clamped to the 3x3 current alpha range, only for an FP16 input.
About +8 slots **[A]**. `bloom-authored-glow.md` states "resolved alpha bit-identical to HDR alpha" and must
be amended with the option.

### 4.4 Step 4 — flicker-aware sharpen (only if Flight A still shows shimmer)

RCAS is locally linear, so steps 1-2 scale the presented ripple with the resolved one. If presented flip-px
flicker is still visible, pass the `thin` mask to the sharpen (state target becomes two-channel, one extra
fetch in `taa_sharpen_ps` / `agx_sharpen_ps`) and scale the lobe by `1 - c * thin`. Expected: presented ->
resolved level (x 0.55-0.7). Cost: exactly the distant thin features lose their sharpening, part of what the
user called blurred; last, default off.

## 5. Rejected

1. **`|current - history|` detector driving the weight.** Biased: a line covered on 1 of 8 phases gets weight
   0.03 on its bright frame and 0.1 on the dark ones and converges to 0.041 instead of 0.125 (33 % brightness)
   **[I]**. The same detector gating the current filter loses energy the same way.
2. **History alpha as a flicker store**: taken by the scene alpha that bloom consumes.
3. **Jitter sequence design.** All 5040 orderings of the Halton-8 points and of a Sobol (0,3,2)-net, scored
   by the filtered ripple over random edges and lines: Halton order 0.0314, best 0.0307 (-2 %), worst 0.0453;
   Sobol-8 0.0331; antithetic pairs 0.030-0.035 (they cancel only at exactly 50 % coverage); **16 samples
   +26 %**; 4 samples -14 % at half the coverage levels **[I]**. Keep Halton(2,3) x 8; the age term handles
   cut convergence.
4. **Catmull-Rom history / 5-tap form**: Catmull-Rom is in place; the bilinear-tap form is a fetch-count
   optimisation against the pass's point-sampling contract, and linear resampling buys at most -22 % of the
   slow band for -10 % contrast.
5. **Current-sample filter limited to flagged pixels**: does nothing for the slow band (section 3) and
   softens exactly the distant detail the user wants; the global form was flown and rejected. After step 0 it
   fits 512 slots and stays available as an option.
6. **Wider sigma clip**: gamma 2 changes nothing; the min/max box is what binds (run 139 and section 3).
7. **Reduced sharpen as the fix**: trades the sharpness the user asked for; stays the user's knob.
8. **Selective LOD bias on alpha-tested draws**: refuted by run148/149.
9. **Feeding bloom from the resolved history**: already the case for RGB; the gap is alpha (step 3).

## 6. Cost and native Windows

- CPU: nothing per draw; per frame one more constant register, one `SetRenderTarget(1)` pair and one
  `SetTexture` around the resolve draw. GPU: +1 point fetch, +1 R32F output, +42 slots on a draw with about
  40 fetches; memory 2 x W x H x 4 B (7.9 MB at 1280x768, 66 MB at 3840x2160) **[I]**.
- Size **[A]**: step 0 about 60 lines + generator entry; step 1 about 40; step 2 about 150; step 3 about 40.
- Documented D3D9 only: ps_3_0 within 512 slots, MRT with independent bit depths (already a route
  prerequisite, to be checked by `TemporalPass` for step 2), R32F render target (already used by the pass).
  A missing capability logs `unavailable=1` and runs the plain resolve. Native behaviour stays unverified.

## 7. Plan, fixture and flights (everything default off until flown)

Fixture additions to `run_temporal_pass.py` mode `lattice` (CPU oracle = the existing resolve model extended
with the two terms): **drifting variants** of the 1-px lattice at 0.25, 0.4 and 0.6 px/frame along x, pitch 4
and a non-integer pitch 2.37, lines 0.8 px wide over a depth-sentinel background (so `thin` is exercised),
256 frames; reported per config: per-px and 8-px-block band rms (periods 2-4, 4-8, 8-32) and spatial contrast
against the analytic box-filtered lattice; shader vs oracle within the FP16 bound.

| step | option | fixture gate | band it targets |
|---|---|---|---|
| 0 | none | bit-identical readbacks; all variants <= 512 slots | none |
| 1 | `--taa-thin-clip 0.75` | shader = oracle within the FP16 bound; block-mean period 2-4 flicker <= 0.65 x baseline static and drifting, no band above baseline; contrast >= baseline (static) / >= 0.75 x (drifting); moving-square ghost <= stated bound; pixels with `thin = 0` bit-identical | slow + fast, moving and static |
| 2 | `--taa-adaptive-weight 0.97` | static ratio 0.29 +/- 0.05; static lattice brightness within 1/255 of baseline **with step 1 on**; flat ramp error <= 0.01; frame-8 cut error <= baseline frame-20 error; speed >= HI bit-identical to baseline | period 4-8 / 2-4, static; slow band with the wide gate |
| 3 | `--taa-alpha-history` | alpha lattice ripple ratio as colour; off and 8-bit route bit-identical | bloom pulse |
| 4 | sharpen mask | presented lattice ripple <= 1.15 x resolved | presented, all bands |

**Flight A** (one candidate with steps 0-3, one session, plant at the shimmering angle while drifting as the
user normally flies, then the distant station, `--taa-debug`, 32 frames each): passes off / thin-clip /
thin-clip + adaptive (narrow gate) / thin-clip + adaptive (wide gate) / all + alpha history. The user judges
crawl, distant sharpness, blinking lights and window glow. Flight B only if step 4 is needed.

## 8. Unknown, and what settles it

- Artefact share of the measured 11-12 code slow band: a motion-compensated or block-mean spectrum of the
  run148 dumps (they include `motion_1`); no new flight.
- Real 2-D clip-collapse share under drift, and all step-1/2 magnitudes: the drifting fixture, then Flight A.
- Windows = authored glow or light-map highlight: alpha of the window ROI in an existing `taa_1` dump.
- Final slot counts: the fixture's `RESOLVE_BUDGET` lines (scratch sketches: 446 / 475 / 487).
- Silhouette halo from the soft clip on fast ships: the moving-square case, then Flight A.

## 9. Session measurements (scratch, untracked)

- Slots: `D3DXCompileShader` + `D3DXDisassembleShader` of the bottle's `d3dx9_37.dll`, two Wine runs under
  `wine_lock.py` (bottle X3) on text-substituted copies of `resolve.hlsl`: 507 (calibration, equals the
  tracked figure); without snapshot branches 433; + speed gate 440; + age MRT 463; filtered without snapshot
  444; thin soft clip 446; thin + speed + age 475; that + filter 487; speed gate on today's program 517.
  Sketches, not final source: expect +/- 10.
- Jitter orderings: numpy, 3000 random edges + 3000 random lines, FFT of coverage x exponential-average gain.
- Drifting lattice: numpy 1-D model of section 3 (`drift3.py` in the session scratchpad).
- Alpha: run142 `taa_1_<f>.rgba16f`, frames 8-31, far px = depth > 0.999, top 1 % glow blocks, half second
  difference; the block mean stands in for a bloom pyramid level, the true pulse depends on `bloomRadiance.w`.
