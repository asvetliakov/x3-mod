# Bloom falloff: why the bolt halo is a disk and how to make it dissolve

2026-09-16. Design note for ratification; nothing here is implemented. Numbers
come from the CPU oracle `tools/analysis/bloom_reference.py` and
`tools/analysis/agx_reference.py` at the live constants; no game pixel was read.

## Question

On the run29 build (all gains 5, `--screen-emission-additive-alpha 0`) the
bolt halo persists, persists with the additive substitution off (native
bolts), and vanishes only with bloom off. The user asks whether the halo can
dissolve gradually ("at 50% of the centre 0.7 opacity, at 55% 0.65, at 60%
0.6") and whether it does that now.

## What the pipeline does now

Live parameters (`capture.cpp:676-687`, `bloom.h`): 5 levels, threshold 1,
knee 0.5, scatter 0.65, `authored_glow_gain` 0.375, `highlight_gain` 0.05,
strength 1 (F10 sets 0), decode gamma 2.2, exposure `2^EV` with the auto EV
capped at +1.3 (factor 2.46), no decoded-space clamp (`X3M_HDR_CLAMP` unset,
65504).

Per source texel (`bloomPrefilter`, `bloom_common.hlsl`): decode the FP16 code,
expose, take luminance `y`, soft-threshold weight `w` (1 - 1/y above the knee),
alpha `a = clamp(scene.a, 0, 1)`, and feed the pyramid
`e * (a * 0.375 + (1 - a) * 0.05 * w)`. Both terms are unbounded above: an
over-1 code is decoded (`code^2.2`) and exposed before the scale.

The pyramid at 1280x768 is 640x384, 320x192, 160x96, 80x48, 40x24 (all even;
the 4-tap extract runs at level 0). Down is a 2x2 box; up is
`U_i = 0.35 D_i + 0.65 T(U_{i+1})` with a [1 2 1]/4 tent on bilinear; the final
`T(U_0)` is added at full resolution before AgX. The DC share per level is
D0 0.35, D1 0.23, D2 0.15, D3 0.10, D4 0.18 (texels of 2, 4, 8, 16, 32 px).
Radial profile of a unit impulse, `k(r)/k(0)` (oracle, 192x192, the two
extreme sub-texel phases of the 32-px coarsest cell):

| r px | 1 | 2 | 4 | 8 | 12 | 16 | 24 | 32 | 48 | 64 | 80 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| corner phase | 0.75 | 0.50 | 0.10 | 0.036 | 0.013 | 0.0079 | 0.0030 | 0.0019 | 0.00081 | 0.00040 | 0.00015 |
| centre phase | 0.99 | 0.73 | 0.20 | 0.020 | 0.0062 | 0.0032 | 0.0016 | 0.0010 | 0.00052 | 0.00020 | 0.00006 |

50% at 2-3 px, 10% at 5 px, 1% at 10-15 px, 0.1% at 33-44 px; the kernel is
exactly zero beyond about 120 px (compact support of the tent cascade; a 6-px
bar's bloom reaches zero 123 px from its edge). Peak of a unit impulse is
0.019 of the source; an extended source approaches its own value. The profile
is phase-dependent (box decimation), asymmetric by up to 5 px at the 1% level.

AgX (`agx_reference.py`, `MAX_EV` 4.026) maps exposed-linear neutral values
to display: 0.0035 (black space) 0.04, 0.01 0.10, 0.05 0.28, 0.18 0.50, 0.5
0.68, 1 0.79, 2 0.87, 4 0.93, 8 0.97, 16 and above 0.998. Everything above
16.3 exposed is clipped white; the bloom sum is added before this curve.

## Why the halo persists

Extract feed per texel (neutral code, gamma 2.2), from `prefilter()`:

| source | alpha | EV 0: e / feed | EV +1.3: e / feed |
| --- | --- | --- | --- |
| background 0.05 | 1 | 0.001 / 0.0005 | 0.003 / 0.0013 |
| hull 0.5 | 1 | 0.22 / 0.082 | 0.54 / 0.20 |
| native bolt 1.0 | 0 | 1.0 / 0.006 | 2.46 / 0.073 |
| native bolt 1.0 | 1 | 1.0 / 0.375 | 2.46 / 0.92 |
| additive gain 2 (code 2) | 0 | 4.6 / 0.18 | 11.3 / 0.52 |
| additive gain 5 (code 5) | 0 | 34.5 / 1.67 | 84.9 / 4.20 |
| additive gain 5 (code 5) | 1 | 34.5 / 12.9 | 84.9 / 31.8 |
| gain 5, 4 sprites overlapped (code 20) | 0 | 728 / 36 | 1793 / 90 |
| gain 5, 8 sprites overlapped (code 40) | 0 | 3346 / 167 | 8239 / 412 |

1. **Alpha 0 does not remove the bolt from the pyramid.** `K = 0` only
   removes the authored term. The highlight term is thresholded but not
   bounded: at gain 5 one sprite layer feeds 1.7-4.2 per texel, four to five
   times a native code-1 bolt with alpha 1 (0.92), and the DESTBLEND ONE chain
   sums codes before the `code^2.2` decode, so overlapping sprites feed 36-412.
   Whatever `D.a` the background holds is irrelevant at these values; the
   observation is explained without scene alpha being 1. (What the backdrop
   writes to alpha is still undocumented: the one documented `Clear` uses
   colour 0, `constant-uploads.md:209`; the space background at code 0.05
   feeds 0.0013 even at alpha 1, so a global alpha of 1 would matter only for
   hulls, whose lightmap glow is the native intent.)
2. **Native bolts** screen-blend to code at most 1 with alpha near 1 over the
   sprite chain (`bloom-per-source-attenuation.md`), so they bloom through the
   authored term at 0.375-0.92 per texel: the same law and strength as an
   engine or the sun in run 9. That halo is the accepted authored glow, not
   a defect of the bolt path; it can only be reduced per source through the
   alpha route, which needs the additive option.
3. **Gain 5 cannot brighten the bolt itself.** A single-layer bolt code above
   2.36 (EV +1.3) or 3.56 (EV 0) is already clipped to display 0.998 by AgX.
   Above that, additional gain shows up only as halo.

## Why the halo is a flat disk with an edge

Oracle profile of a 6x40 px bar of amplitude S over black, display value at
`dx` px from the bar edge, through AgX (`tonemap_engine`, decode none):

| S (feed) | 0 | 2 | 4 | 8 | 12 | 16 | 24 | 32 | 48 | 64 | 80 | flat top (>=0.95) | <0.5 at | <0.1 at |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0.92 (native bolt, engine) | 0.61 | 0.51 | 0.38 | 0.25 | 0.19 | 0.15 | 0.10 | 0.08 | 0.06 | 0.05 | 0.04 | none | 3 | 25 |
| 1.5 | 0.70 | 0.59 | 0.47 | 0.31 | 0.24 | 0.19 | 0.13 | 0.10 | 0.07 | 0.05 | 0.05 | none | 4 | 34 |
| 4.2 (gain 5, one layer, alpha 0) | 0.84 | 0.76 | 0.65 | 0.48 | 0.39 | 0.33 | 0.23 | 0.18 | 0.11 | 0.08 | 0.05 | none | 8 | 54 |
| 32 (gain 5 alpha 1, or 2-3 layers) | 0.99 | 0.96 | 0.92 | 0.82 | 0.74 | 0.67 | 0.55 | 0.48 | 0.33 | 0.22 | 0.12 | 3 px | 30 | 85 |
| 412 (gain 5, 8 layers, alpha 0) | 1.00 | 1.00 | 1.00 | 1.00 | 0.98 | 0.96 | 0.92 | 0.88 | 0.77 | 0.64 | 0.44 | 18 px | 76 | 104 |

For S in the tens and hundreds the sum is above AgX's 16.3 clip over a wide
ring, so the inner halo is a saturated white plateau (the "disk"); the
visible ramp begins where `S k(r)` falls through 16 and, because AgX is
log-like, the display value then drops roughly linearly with radius (about
0.01 per pixel at S 412) until the kernel's compact support ends ~120 px
out, on a straight tent ramp with 32-px kinks from the coarsest level. At
S around 1 the whole halo sits on AgX's compressive segment: no plateau,
0.6 at the edge, half-brightness within 3-4 px, gone by 25-35 px.

The user's description (a linear opacity ramp that is still 0.6 at 60% of
the radius) is not what the pipeline does at any S. At small S the display
profile is the tent cascade compressed by AgX: steep in the first 4 px, then
a long dim tail. At large S it is a white plateau followed by a near-linear
ramp to a hard cutoff. The plateau, not the kernel, is what reads as a disk.

## Recommendation: cap the bloom source at the native scene-map ceiling

Add a bloom-only decoded-space source clamp of **1.0** (game code 1.0), the
ceiling of the native A8R8G8B8 scene map the original compositor read
(`compositor-and-glow.md`, "Exact native extraction"). The pyramid then sees
at most what the native glow could see; the display path keeps its HDR values.

**Mechanism.** `bloomExposed` already applies `min(decoded, bloomRadiance.y)`
before exposure; `c27.y` is filled from the display firefly clamp
(`bloom_pass.cpp:557`, `prepare_bloom` in `bloom.h:100`). Add
`float source_clamp = kAgxClampOff;` to `BloomParams` (validated like
`threshold`), and set `c.radiance[1] = min(p.agx.exposure[1], p.filter.source_clamp)`
in both places. No shader edit, no new register, no new lane. As implemented
(ratified 2026-09-16): the option is `--bloom-source-clamp C` /
`X3M_BLOOM_SOURCE_CLAMP` (finite, 0 < C <= 64, requires `--hdr-bloom`); absent
means no clamp, so the installed behaviour is unchanged until the user run
brackets 1.0 against 2.0 and none; 1.0 is the recommended value. Logged once
as `bloom_source_clamp_mode` and as `clamp=` in the `bloom_prepare` line.
The oracle already models it: `bloom_reference.exposed(..., clamp_max)`.

**Effect.** Every source is capped per channel at code 1 before the split, so
the feed is bounded by 0.375 x 2.46 = 0.92 at the EV ceiling regardless of
gain or overlap. Bolts with alpha 0 at any gain feed 0.05 x 0.59 x 2.46 =
0.073, the native-bolt-with-alpha-0 value: a faint rim (scaling the 0.92 row
above, edge B 0.027, display about 0.2, background by about 8 px), no plateau,
no support-edge cut. Engines
and the sun at native code (run 9) are bit-identical, since their codes never
exceed 1; engines at `--emission-source-gain` 2 or 5, whose ONE/ONE sums
exceed code 1 in FP16, bloom as native code-1 engines instead of growing a
plateau (the run-28 "engines brighter" remark concerned the presented engine,
which the clamp does not touch). Per-channel clamping shifts an
oversaturated hue toward white exactly as the 8-bit native scene map did.

**Hot path.** Zero per-frame GPU cost (one constant lane, already uploaded
once per pass); one `min` on the CPU per pass. No per-draw work.

**TAA and Reset.** No resource, no history, no state; the constant block is
rebuilt every pass from parameters.

**Native Windows.** A constant value through the existing documented
`SetPixelShaderConstantF` path; the shader is unchanged and stays within the
qualified slot counts (`verification/hdr-bloom-filter.md`). Windows runtime
remains unverified as for every feature.

**What the user would see.** Bolts (alpha 0): the white disk replaced by a
thin soft rim; the bolt core stays white. Bolts (native, F5 off): unchanged
authored glow (0.92 profile above), the same as engines. Engines and sun:
unchanged at native gain; at gain 5 the halo returns to the run-9 size.
The falloff shape is untouched; only its amplitude is bounded.

**Verification.**

- Host: `test_bloom_reference.py` gains a 128x128 impulse profile test at the
  live parameters (levels 5, scatter 0.65) asserting 50% by 3 px, 10% by
  5 px, 1% by 15 px, energy sum 1 within 1e-3; and a 6x40 bar at code 5 alpha
  0, authored 0.375 / highlight 0.05, exposure 2^1.3: with `clamp_max` 1 the
  AgX display profile has maximum below 0.5 and decreases monotonically from
  the bar edge; with the clamp off it holds >= 0.95 over at least 8 px.
  Identity: any image with codes <= 1 gives the same `bloom()` with clamp 1
  and clamp off (the run-9 baseline).
- GPU: `run_bloom_pass.py` and `bloom_pass_fixture.cpp` get a `source_clamp`
  field (magic `X3BP0003`, count 36 -> 40) and the size bound raised from 32 to
  64 so a 5-level case exists. Four new cases at the live constants (0.375 /
  0.05 / threshold 1 / knee 0.5 / scatter 0.65, which no case pins today; the
  authored cases use 0.1 / 0.2 and default scatter 0.7 on 3 effective
  levels): 64x40 bar at code 5, alpha 0 and alpha 1, clamp 1 and clamp off,
  compared to the oracle at the existing 3-code bound. `expected()` passes
  `clamp_max=c['source_clamp']` to `ref.bloom` only (the base AgX keeps its
  own clamp). `test_bloom_pass_fixture.py` re-pins the input hash (new
  field) and must keep the expected-output hash
  `46c4b26a...` unchanged for the first 36 cases. One run under
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_bloom_pass.py`
  after the build; ledger `docs/verification/bloom-pass-fixture.md`.
- Game: one launch pair by the user, bolts at gain 5 with alpha 0, clamp 1
  versus `X3M_HDR_BLOOM_SOURCE_CLAMP=65504`, F10 toggled on both, plus one
  engine-at-gain-5 view. The comparison is the disk versus rim, not a number.

## Alternatives considered

- **Chroma-preserving luminance cap on the feed** (scale by
  `min(1, L / luma(feed))` after the alpha split): 4 more slots per tap
  (generic sRGB extract 362 -> about 398 of 512), needs a constant lane
  (c28.y, currently the unread sRGB flag copy) and an oracle change. Keeps
  hue on oversaturated sources, which the native map did not; buys nothing
  for the disk. Fallback if the per-channel white shift is disliked.
- **Karis luma weighting in the downsample.** Suppresses isolated fireflies
  but re-normalises its weights, so a uniformly extreme 2x2 keeps its value;
  an extended bolt still saturates. Loses.
- **Tonemap-aware extraction** (bloom the AgX-compressed source). Bounds the
  feed but changes every source non-linearly (0.92 -> the compressed value),
  breaking the run-9 identity, and puts the full AgX curve into the six
  extract variants, which already ran into the 512-slot limit once. Loses.
- **Lower authored gain, or gate it by the threshold.** Dims engines and sun
  equally; the bolt at alpha 0 is not in the authored term anyway. Loses.
- **Lower the additive gain.** Above code 2.36 the extra gain is invisible
  on the bolt (AgX clip), so gain 2-2.5 is the honest brightness ceiling;
  but even gain 2 feeds 0.52 per layer and tens with overlap, so the disk
  survives at lower amplitude. Complementary, not sufficient.
- **Reshape the kernel: per-level scatter weights, lower scatter, or 6
  levels.** Per-level weights are cheap (an array in `BloomParams`, `c26.z`
  set per level in the existing per-draw constant upload, an oracle loop,
  zero per-frame cost, no resource) and are the only way to obtain a flatter,
  wider ramp like the one the user described; 6 levels adds one texture pair
  and pushes the support to ~250 px. Either changes the engine and sun halo
  radius accepted in run 9 and does nothing about the plateau. Keep as the
  follow-up if the capped halo is judged too tight, with the shares chosen
  against the profile table above.

## Unknowns

1. **Scene alpha at bolt and background pixels** (readback, as in
   `bloom-per-source-attenuation.md` unknown 1). Not needed for this
   decision; the highlight term alone accounts for the observation.
2. **Engine codes in the FP16 target at gain 2 and 5.** Run65 samples show
   ONE/ONE blending; the summed code range is unmeasured. The clamp bounds
   it either way.
3. **Which S regime the user's screenshot corresponds to.** The table covers
   one layer (4.2) to eight layers (412); the sprite overlap at the core is a
   modelled 8, not a measured count. A readback of the FP16 scene at a bolt
   would place it.
4. **Native scene-map ceiling as the right clamp value.** The `A8R8G8B8`
   scene map is documented; the clamp of 1.0 reproduces its ceiling in
   decoded space, but the game's presentation of >1 codes never existed, so
   0.5 or 2 remain legitimate user preferences (the env bracket).
