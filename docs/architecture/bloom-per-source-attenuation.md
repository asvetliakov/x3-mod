# Bloom per-source attenuation: bolts versus engines and sun

2026-09-16. Design note for ratification; nothing here is implemented.

## Question

The bloom pass consumes only the whole resolved FP16 scene
(`BloomPrepare::scene`, `src/renderer/bloom_pass.h:41`; whole-target boundary
check `bloom_pass.cpp:526`) with no mask input. Can the halo of the nine
screen-emission-additive bullet pairs (`src/proxy/screen_emission_admission.h:31-43`,
bound at `src/proxy/motion_output.cpp:3734-3745`, DESTBLEND ONE plus the
`AdditiveGain` PS2 variant at gain 2) be reduced without lowering the bolts'
brightness, and per emitter family in general?

## What the current code already does

The answer hinges on one fact: **the FP16 scene's alpha channel is already
the per-pixel bloom weight**, and the bolts already write it.

- The live call site runs the extract in the authored-glow mode
  (`capture.cpp:676-678`: `authored_glow_gain 0.375`, `highlight_gain 0.05`).
  In that mode `bloomPrefilter` (`src/temporal/bloom_common.hlsl`) computes, per
  source texel with exposed colour `e` and clamped alpha `a = clamp(scene.a, 0, 1)`:
  `scale = a * 0.375 + (1 - a) * 0.05 * weight`, where `weight` is the
  threshold/knee soft cut. The `a` term is **unthresholded**: an alpha-1 pixel
  blooms at 0.375 x e at any brightness; an alpha-0 pixel blooms only through
  the 0.05-scaled threshold term. This mirrors the native compositor's
  downsample (`D.rgb = A x C`, highlight `h x (1 - sat(A))`,
  `docs/reverse-engineering/compositor-and-glow.md`, "Exact native extraction").
- The bullet pixel shaders output the sampled texture alpha unchanged
  (`docs/architecture/linear-emission-sm1.md`: "output alpha **a**, not
  `a*h`"; the `AdditiveGain` variant multiplies colour lanes only,
  `src/renderer/linear_emission_sm1.cpp:221`). With mask 15, separate alpha
  off and DESTBLEND ONE, the target alpha accumulates `a + D.a`
  (`screen-emission-region.md`, "Additive option"). A bolt is a chain of
  overlapping sprites (99 quads per draw in run 17; modelled overlap 8 at the
  core), so the summed alpha reaches the extract's clamp of 1 over most of
  the footprint. Inference from the shader shapes, not a measured value: the
  texture alpha content is unknown (see Unknowns).
- Alpha survives to the extract: the TAA resolve writes the current alpha
  through and never blends history alpha (`src/temporal/resolve.hlsl:131-133`);
  the AgX writeback and RCAS carry alpha unchanged (`hdr_writeback_ps.hlsl:4-6`,
  `taa_sharpen_ps.hlsl:32`); the bloom candidate's alpha never reaches main
  (RGB-only copy, `hdr-bloom-boundary.md` step 4). Nothing else reads scene
  alpha: the meter shaders and the resolve's reactive mask do not
  ("never infer it from alpha", `resolve.hlsl:114`). TAA history is RGB;
  no Reset-owned resource holds alpha state.
- Consequences for the options: the extract already reads a `float4` and
  already uses `.a`, so a per-pixel weight costs nothing more in the bloom
  pass. Raising `threshold`/`knee` cannot touch the authored term at all.
  Per-source **radius** is a property of the shared pyramid (`levels`,
  `scatter`); only strength is separable per source without a second pyramid.

If the bolts' accumulated alpha is near 1, the halo is the authored term:
0.375 x (2 x q) spread by five levels at scatter 0.65, fed by the whole bolt
footprint including its dim tail. That is consistent with "a broad soft halo
over black that survives the effect-gain split" (run68; `docs/status.md`).

## Recommendation: attenuate the bolts' alpha contribution at the additive draw

Option 1 in its blend-state form. Leave the bloom pass, its shaders, the
`AdditiveGain` variant and the source gain untouched; change only what the
admitted additive draw writes to the scene's alpha channel.

**Mechanism.** In `MotionOutput::prepare_screen_additive`, after the existing
`DESTBLEND = ONE` set, additionally set for the one draw:
`SEPARATEALPHABLENDENABLE = TRUE`, `BLENDOPALPHA = ADD`,
`DESTBLENDALPHA = ONE`, and `SRCBLENDALPHA = BLENDFACTOR` with
`D3DRS_BLENDFACTOR` alpha = `k` (`k = 0` uses `SRCBLENDALPHA = ZERO` and
needs no blend-factor cap). The colour law stays `G q + D`; the alpha law
becomes `k a + D.a`. `finish_screen_additive` restores the four states to
their shadowed admitted values (`composition_blend[3..6]`, already shadowed by
the setter hook with no per-draw getter, `motion_output.cpp:990, 3022`) and
`BLENDFACTOR` to its value, shadowed the same way as `DITHERENABLE` today
(one `GetRenderState` when unknown, then the shadow). The PS alpha output is
untouched, so the 19 of 54 alpha-tested bullet draws (`linear-emission-sm1.md`)
keep their native alpha test. Refuse the attenuation (draw stays as today)
when the state is unknown, when `D3DPMISCCAPS_SEPARATEALPHABLEND` is absent,
or when `k` is not 0 or 1 and `D3DPBLENDCAPS_BLENDFACTOR` is absent in
`SrcBlendCaps`; log the refusal once like the existing eight reasons.

**Knob.** `--screen-emission-additive-alpha k` (`X3M_SCREEN_EMISSION_ADDITIVE_ALPHA`,
finite 0..1, default 1 = today's law). Because alpha accumulates and the
extract clamps at 1, intermediate `k` mainly thins the tail while a
many-layer core stays saturated until `k < 1/overlap`; `k = 0` is the clean
first test: bolts then bloom only through the thresholded highlight term
like any bright pixel, while engines, sun and every other alpha-authored
emitter keep the authored channel. With `k = 0` and G = 2, a bolt texel at
exposed luminance 2 feeds the pyramid 0.05 x 0.5 x e = 0.05 instead of 0.75
(15x less); its presented brightness is unchanged (colour law untouched).

**Hot-path cost.** Off (`k = 1`): nothing. On: per admitted bullet draw
+4 `SetRenderState` before and +4 after (5/5 when `BLENDFACTOR` is used),
on top of today's 1 `GetTextureStageState`, 2 `SetRenderState`, 2
`SetPixelShader`. At the 54 bullet draws of the sampled firing frame that is
at most ~540 native setters per frame, no draws, no bandwidth, no allocations,
no per-frame log lines. The bloom pass cost is unchanged (authored-mode full
pass 1.17 ms median at 1280x768 in the authored-glow timing fixture,
`verification/results/bottle-X3/bloom-authored-glow.json`).

**Native Windows.** Documented D3D9 render states and caps only
(`D3DRS_SEPARATEALPHABLENDENABLE`, `D3DRS_SRCBLENDALPHA/DESTBLENDALPHA/BLENDOPALPHA`,
`D3DRS_BLENDFACTOR`, `D3DPMISCCAPS_SEPARATEALPHABLEND`, `D3DPBLENDCAPS_BLENDFACTOR`).
Separate-alpha blending into A16B16G16R16F is covered by the same
post-pixel-shader-blending format query the FP16 path already relies on.
No Wine-private export, layout or hash. Windows runtime behaviour stays
unverified, as for every feature.

**TAA and Reset.** No new resource, no history input, no change to the
resolve, the handoff, `before_reset` or the recovery copy. The changed alpha
reaches main through the ordinary writeback exactly as today's `a + D.a`
does; with `--hdr-bloom` the native compositor's RGB is overwritten and its
outgoing alpha is zero (`compositor-and-glow.md`, run 26), so the only
consumer that changes is the extract. Without `--hdr-bloom` the native glow
shrinks for the bolts in the same authored sense, which is the intent.

**Files.** `src/proxy/motion_output.cpp` (`prepare_screen_additive`,
`finish_screen_additive`, the `DITHERENABLE`-style `BLENDFACTOR` shadow, a
caps read at configure), `src/proxy/motion_output.h` (the `k` member and
shadow slot), `src/proxy/capture.cpp` (env read next to
`X3M_SCREEN_EMISSION_ADDITIVE` at :2225-2233, configure call),
`tools/manage.py` (option next to `--screen-emission-additive`, :145, :295-299,
:474). No renderer, shader or bloom change.

**Acceptance.** Extend `run_linear_distance_fade_live.py`'s additive case
(`validate_screen_additive`, :1122-1152, which already reads the FP16 target
back and checks `D'.a = a + D.a` on every quad pixel) with `k = 0` and
`k = 0.5`: colour still `G q + D` bit-exact against the `k = 1` run, alpha
`k a + D.a` within one FP16 code, alpha-tested quad coverage identical to
the native twin, refusal counter 0 with the caps present, and the restored
state read back equal to the admitted values. Host: extend
`verification/analysis/test_screen_emission_live.py` (line grammar, launcher
gate, caps refusal). The extract's alpha response needs no new proof:
`run_bloom_pass.py` authored cases (12 cases, gains 0.1/0.2, masks, EVs,
both parities; `test_bloom_pass_fixture.py:70-77`) and the
`tools/analysis/bloom_reference.py` oracle already pin `scale = a g + (1-a) h w`.
Run it once under
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_linear_distance_fade_live.py --screen-emission ...`
after the build; one launch `--dry-run` with the new option. Ledger:
`docs/verification/screen-emission.md`.

**Per-family extension.** The same alpha-factor bracket fits
`prepare_source_gain` for the engine and effect families (the alpha triple is
already shadowed, `composition_blend[4..6]`). The engine draws blend colour
ONE/ONE/ADD with separate alpha on (run65 samples: `src=2 dst=2 op=1
sepalpha=1`); their alpha factors were not logged in run65 or run68, so the
engine alpha law, and hence what a family `k` means there, is unknown. Do not
fund it until the bolt result and that law are in hand.

## Alternatives considered

- **1b. Alpha multiply in the `AdditiveGain` variant** (`mul r0.a, r0.a, c31.y`;
  `c31.y` is a free lane, `linear_emission_sm1.cpp:212`). Zero extra state
  sets, but the alpha test reads `oC0.a`, so it changes coverage on the 19
  alpha-tested bullet draws unless gated on `ALPHATESTENABLE`, which splits
  the law in two. Keep as the fallback if a device lacks separate-alpha
  blending.
- **2. Hull mask from the bullet regions.** The additive option runs no
  bound, Unlock scan or bracket (`screen-emission-region.md`, "Additive
  option"); step D belongs to the packed route and requires `--ownership`.
  Reinstating it costs 1.7-27 us hull derivation per draw, ~10 us per scan,
  ~17 us sentinel per lock (`screen-emission-bullet-bound.md`, "Step D
  implemented") plus a mask surface (clear, per-draw rectangle fill) and a
  second sampler in all six extract programs. It attenuates by screen
  rectangle, not by source: an engine, the sun or a station glow inside a
  bolt's rectangle (0.1-17.5 % of the viewport per draw, several per firing
  frame) would be attenuated too. Loses on both cost and correctness.
- **3. Second extract / per-level weights for the masked region.** Needs the
  mask of option 2 and roughly doubles the pyramid (+~1 ms at 1280x768 per
  the timing fixture) or adds a second pyramid's scratch. It is the only
  route to a per-source **radius**; funded only if the strength-only route is
  judged insufficient.
- **4. Global knobs.** Lowering G lowers bolt brightness (excluded by the
  requirement). Raising `threshold`/`knee` does not touch the authored term
  and only dims the engine/sun highlight bloom. Lowering `authored_glow_gain`
  (0.375, `capture.cpp:676`) dims engines and sun equally with the bolts.
  None separates the bolts.

## Unknowns and what settles them

1. **Bolt alpha in the FP16 scene** (the premise). Settled by a readback of
   the resolved scene alpha at a bolt and at an engine pixel on a firing
   frame; the session log has no pixel values and no image dump exists on
   the capture path. Cheapest live experiment without per-draw code: one
   env override of `authored_glow_gain` to 0 (legacy RGB-only extract) on a
   relaunch. Halo gone = authored term confirmed; halo unchanged = the
   threshold term or something outside the bloom pass, and this note's
   recommendation would not help.
2. **Halo attribution.** Run 28 tests the EV ceiling and sharpen/mip bias
   first (`docs/archive/handoff-2026-09-16.md`, open question 1). Read its result
   before funding this change.
3. **Engine alpha law** for the per-family extension (see above).
4. **Bottle caps.** `D3DPBLENDCAPS_BLENDFACTOR` and
   `D3DPMISCCAPS_SEPARATEALPHABLEND` on the X3 bottle's device are not
   recorded anywhere in the repo; the live fixture's caps case reports them.
