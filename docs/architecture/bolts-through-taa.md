# Bolts through the TAA: keeping weapon bolts visible over distant stations

Design note, 2026-09-26, for the Run 93 A finding (`docs/verification/lod-overlay.md`, "Run 93 A: bolts still vanish
over distant stations"; scripts and per-frame table under `verification/results/run346-run93a-bolts/`). Status: built
2026-09-26 (the "Implemented" section at the end; one deviation from the ratified text: the flag alpha is negative, not
2 + held). [M] = measured in that ledger, [I] = inferred from the source or computed here.


**Ratified 2026-09-26 (orchestrator):** B' as written, with these conditions for the implementation: (1) every pixel
without a flag is bit-identical in colour, age, depth history and output alpha (fixture-pinned); (2) the flag costs nothing
outside the one or two late bullet draws per firing frame; (3) `X3M_BOLT_FAR_COMPOSITE` (default on) and `X3M_BOLT_FAR_SHOW`
(W, default 0.5) are schema settings with launcher options; (4) the sun-shadow cascade apply's read of RT2.g as the share is
verified, not assumed, for the default launch (the cascades are on by default), and the one-frame effect under a bolt is
stated with a number from the seam case; (5) the MRT post-pixel-shader blending cap is logged once at device creation;
(6) a seam case draws a far-depth quad and a one-frame additive streak through the full chain (route, resolve, write-back)
and asserts the streak survives at W over the far pixels as it does over space, plus the bit-identity of (1).

## The problem, in the resolve's terms

The single-copy rule works: the late bullet copy passes the depth test over a far station and adds 2.2-6.4 summed rgb to
the pre-resolve FP16 scene there, as much as or more than over space (0.9-5.0) [M]. A bolt writes neither depth nor the
RT2 lane (the additive route runs only on an unrouted draw, `motion_output.cpp:5694-5717`, after `restore_bindings_checked`
has unbound RT1/RT2), so a bolt pixel keeps the station's lane texel: `.r` = z/w >= 0.99991 (w 63,000-160,000 units,
beyond the far ramp d0/d1 = 76,800/87,040 units) [M], `.g` = the same z/w (`current_depth_ps.hlsl`: `float4((z/w).xx, w.xx)`;
-1 on a fade-owner "invalid-share" row; the fill sentinel is (-1,-1,-1,-1)), `.b` = `.a` = w. The default resolve
(`resolve.hlsl`, X3M_REGION_HOLD family) then derives from that texel: `farw` = 1 (`:432`), the 7x7 far clip (`:941`),
`farKeep` toward 0.985 (`:1033`), or the thin-region 0.97 through the vote in `.a` / the hold code (`:1034`). Effective keep
under the bolt: median 0.99 on the station against 0.39-0.56 (green) and 0.67-0.91 (blue) over space [M]; presented bolt
contrast -8..+22 codes against 25-110 [M]. The bolt is admitted, added, and then averaged away by the treatment the user
has just ratified for stations (`docs/verification/temporal-resolve.md`, Run 93 A decision: blur over shimmer; far/thin
weights stay).

Invariants for any fix: (1) a pixel without a bolt this frame is bit-identical in colour, age and depth history; (2) the far
and thin treatment is not weakened anywhere, including at pixels a bolt crossed in earlier frames; (3) the bolt over the
station reads as it does over space (no brightness step at the silhouette, no trail); (4) native D3D9 only.

## The residual that every in-resolve fix leaves (why A alone is not enough)

Any variant of option A lets the bolt into the history at that pixel: 15 % at the base weight, more through the 3x3 clamp
(over space the measured effective keep is 0.39-0.56 because the clamp pulls the dark history up to the current box), 100 %
under a plain reject. The next frame the bolt is gone, the far weight 0.985 and the 7x7 far clip return, and the bright
history is clamped to the 7x7 min/max of the current hull neighbourhood. Over space that box is dark (stars), so the residual
vanishes in one frame; over a textured hull the box spans the hull's local contrast (pre-resolve hull luma 0.27-0.86 [M]),
so a residual up to the local contrast survives and decays at 0.985 per frame: 74 % left after 20 frames, 40 % after 60 [I].
Every bolt fired at a station would leave a hull-bright scar along its path fading over about a second. "Reject history
this frame" makes it worse (100 % enters); "cap to the base weight" leaves 15-60 %. The only in-resolve cure is to reset
the age at the pixel (young ramp n/(n+1): the residual falls as 1/(n+1), 9 % after 10 frames), which trades the scar for a
wake: the far pixels the bolt crossed run below 0.95 for 19 frames and reach 0.985 only after 64, i.e. jitter flicker along
the bolt path on plants and lattices for about half a second, exactly the class the user rejected (run327/run332 sparkles).
The depth history cannot help: the disocclusion tolerance is 0.02 relative (`c6.y`), and the bolt's own z/w (<= 0.99950
[M]) is within 4e-4 of the station's, so a bolt depth in the lane would be "the same surface".

Conclusion: the bolt must not enter the station's history at all. That is option B's semantics (bolts outside the TAA),
and it can be had without re-issuing a single draw, because the pre-resolve FP16 scene target, which already holds the
depth-tested bolt at full strength, stays intact until the write-back samples the resolved image
(`hdr-scene-path.md` section 4: the resolve reads the scene as s0 with no copy and writes a separate history texture; the
tonemap draw runs after it in the same hook).

## Decision (recommended): B', a flagged post-resolve composite in the write-back

Three small pieces, one flag lane, no new target, no draw re-issue.

**1. The late bolt draw marks its coverage in RT2.g.** In `prepare_screen_additive` (`motion_output.cpp:6148`), after
every admission check and the single-copy drop, when `X3M_BOLT_FAR_COMPOSITE=1` (new option, default on after its flight)
and RT2 is the four-lane format (A32B32G32R32F or A16B16G16R16F, `motion_output.cpp:1328`; the lane-off R32F RT2 has no
`.g` and refuses the flag with a logged reason, which costs nothing because the far/thin programs need the four-lane RT2
for `gateOpen(lane.b)` anyway): bind RT1 with COLORWRITEENABLE1 = 0 and RT2 with COLORWRITEENABLE2 = GREEN (2) around the
draw, through the existing `bind_target` / mask save-restore of `bind_targets` (`:693`; RT1 is bound only so the MRT set
has no gap, which the route never exercises today), and bind the gained PS variant even at gain 1 (today G = 1 binds the
original SM1 program, which cannot write oC2). The variant (`linear_emission_sm1_pixel_variant`, `AdditiveGain`, ps_2_0)
gets three or four instructions before its output MOV: `max r, col.x, col.y; max r, r, col.z; mul r, r, K; mov oC2, r.xxxx`
with K = 32 as a DEF in the variant (the c216-c218 upload has no free lane: `c216.zw` is the motion fragment's jitter).
With the draw's ONE/ONE blend the lane becomes `g = base + K * add`, base in [-1, 1]; on a device without
D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING it becomes `K * add`. Either way the test `g > 1` is a bolt exactly when
`add > 2/K = 0.06` (worst base 1) and never on a non-bolt pixel (base <= 1). The dark texels of the bolt quad (`add` below
0.06) stay unflagged, so the flag follows the visible streak, not the quad. `.r/.b/.a` keep the station's values under the
GREEN mask, so the depth proof, the camera gate, the thin vote and the depth history are untouched. Per bolt draw: two
SetRenderTarget, two mask reads (shadowed), two mask writes, their undo (two masks, two unbinds), the PS bind the route
already does: about eight native calls, for the one or two late bullet draws per firing frame [I] (2,407 `bolt_copies` rows
in the run346 session [M]); the routed rows pay the same per draw about 450 times a frame.

**2. The resolve raises the flag, in the output alpha only.** X3M_REGION_HOLD family (the default `far_camera_hold`
program, 1017 slots; the same three lines in the `far` program's mask path `line_mask_ps.hlsl:185` for parity):

```
bool bolt = lane.g > 1;                                   // false exactly for every non-bolt texel
float held = max(stabilise.g * farOpen, stabilise.b);     // the share the far / thin-region weight adds over the base
... colour, age, depth history exactly as today ...
outAlpha = bolt && held > 0 ? 2 + held : blendedAlpha;    // after the alpha blend, never into the colour path
```

Colour, age and depth-history outputs are byte-identical to today at every pixel (the fixture pins it); the alpha changes
only at a flagged pixel that the resolve is holding (a bolt over space, over the near hull or over a far pixel under a
pan with `openC` closed is not flagged: there the bolt already shows at the ordinary retention, and the composite would
brighten it against its own look over space). The stored history alpha at that pixel is `2 + held` for one frame; the next
frame's alpha blend clamps the history alpha to the current 3x3 alpha range (`:1051`), so the flag cannot propagate.
Cost: one compare, one max, two selects: about four slots [I], 4 us per frame at 5120x1440 by the budget rule.

**3. The write-back composites from the scene at flagged pixels.** The tonemap programs (`hdr_writeback_ps.hlsl`,
`agx_sharpen_ps.hlsl`, `bloom_agx_ps.hlsl` and the dither twin) and the bloom extract (`bloom_common.hlsl:58`, which
reads the resolved alpha as the glow weight) get the scene FP16 texture as one more sampler (the pass owns it; it is the
resolve's s0) and, at the resolved texel `r`:

```
[branch] if (r.a >= 2) { float4 s = tex2Dlod(scene, uv); float share = saturate(r.a - 2);
                         r.rgb = lerp(r.rgb, s.rgb, W * share); r.a = s.a; }
```

`W` = `X3M_BOLT_FAR_SHOW`, default 1 since Run 94 A (the user judged 1 better than 0.5 in run349; 0.5 [I] before): the current bolt is shown at half strength over a held far pixel, matching the
measured retention over space (44-61 % green, 9-33 % blue [M]; the flight tunes it). `share` ramps the composite in with
the far weight across the d0-d1 contour, so there is no step at the far ramp's edge; inside the region (`stabilise.b` = 1)
it is full. The hull component under the bolt is then half the stabilised history and half the current jittered sample for
that one frame, under a bolt three to ten times brighter [M]: not visible [I]. `r.a = s.a` restores the bolt's own alpha
law (the per-source bloom attenuation) for the game's RT0 alpha and the bloom weight. Non-flagged pixels take the branch
not at all: bit-identical output, one compare per pixel, one texture fetch per flagged pixel (hundreds to a few thousand
per firing frame [I]). The fused sharpen (`agx_sharpen_ps.hlsl`) composites its centre tap only; its four neighbour taps
see the resolved (bolt-free) values, a slight softening of a streak that is soft already [I]. The bloom extract composites
too, so bolts over stations feed the mod bloom as bolts over space do today (they get none today over stations: 0-17 % of
the addition survives [M]).

**What the user sees.** Over space: today's image exactly. Over a far station or a held lattice: the bolt at W of its
current strength, added in linear HDR before the tonemap, with the station's history untouched (the bolt still leaks its
1.5 % into the history as it does today, so no new residual), no trail, no wake, no age reset. A bolt crossing a far
silhouette goes from its resolved retention over space to W over the station: continuous at W = the measured space
retention, which is why W is a tunable rather than 1.

**Native Windows.** SetRenderTarget on indices 1-2, per-target COLORWRITEENABLE (D3DPMISCCAPS_INDEPENDENTWRITEMASKS, which
the route already requires), a ps_2_0 oC2 write, float render targets with unclamped shader output, one more sampler on
full-screen quads: documented D3D9 throughout, no Wine-specific behaviour. The MRT blending cap changes the lane's base
term only; the lane's qualification already requires that cap, so the no-blend branch is unreachable (the lane-off RT2 refuses the flag). Not verified natively (the user cannot test Windows); tracked in
`platform-portability.md` when built.

**Failure modes.** (a) RT2 in R32F (lanes off): the flag is refused at the draw, the route stays as today, one logged
row. (b) A resolve early return (cut, reactive, sentinel policy, unproven depth) returns before the flag: the pixel is
current-only anyway and the bolt shows. (c) `--sun-shadow` (off by default): the cascade apply reads RT2.g as the sun share
(`sun_shadow_cascade_apply_ps.hlsl:82`, `saturate(ds.g)`), so a flagged pixel reads share 1 for that frame, under the bolt;
the apply runs before the resolve in the hook (`motion_output.cpp:2715`), and no pixel without a bolt changes. (d) The
alpha carrier reaches the game's RT0 alpha: at flagged pixels it is the scene alpha (the bolt's), elsewhere unchanged.
(e) A bolt over a far pixel inside a marked box texel (`boxLow.a`) or a region hold: the colour path is unchanged, so the
history holds as today; the composite is what shows the bolt. (f) The HDR redirect refused or the 8-bit path: the additive
route itself is refused without HDR (`reason=no_fp16_target`), so there is nothing to flag.

## Alternatives considered

**A. Drop the far and thin weights at flagged pixels (cap to the base weight, or reject).** Same flag lane, three
selects in the resolve, no write-back change: the smallest patch, and it makes the bolt frame look right (the fixture
would pass on frame N). It loses on frame N+1 onward: the bolt enters the history and the 7x7 far clip leaves a residual
bounded by the hull's local contrast that decays at 0.985 (the scar), or, with an age reset, a 64-frame wake of reduced
far stabilisation along the bolt path (the sparkle class the user rejected). Both violate invariant (2) at the pixels the
bolt crossed. Kept as the fallback only if the flight shows the composite's hull-under-bolt sample to be visible, which
the analysis does not predict.

**B. Hold the late bullet draws and re-issue them after the resolve.** Same semantics as B', with the cost and risk of
capturing and replaying a draw at scene end: the game's VS and its constants (the bullet VS reads c0-c3 at least), the
substitute vertex buffer (one per held draw, or a copy: the footprint plan may reuse it), the texture and its sampler
states, the declaration, about ten render states and the viewport, all of which draws 215-222 may change before the hook
[M: the state changes after the late draw are logged]; the depth surface is still bound at the hook (the pass's step 0
unbinds it itself, `hdr-scene-path.md` section 4) but must be rebound with the jittered projection, and the composite
must go into a scratch copy of the resolved image (59 MB of FP16 traffic per frame at 5120x1440 [I]) or the bolt enters
the history through the ping-pong. Without a depth gate it also changes the bolt's look over space (100 % instead of the
resolved 44-61 %), which the user has not asked for; with one it needs the lane anyway. Everything B delivers, B'
delivers from the scene texture the resolve already read.

**C. A luminance-jump exemption in the resolve.** A current pixel far above its history box is the definition of a far
sparkle: run327's plant sparkles and the lattice highlights that one jitter phase samples are exactly that, and the 7x7
far clip plus the 0.985 weight were accepted because they removed them ("Shimmering/sparkles are fixed", Run 86 A). The
exemption cannot tell a bolt from a jitter-phase highlight or a blinking far light, is not bit-identical outside bolt
pixels, and still leaves A's residual. Loses.

**D. Let the bolts write depth, or take the far weight from the bolt's own depth.** ZWRITE on the bullet draw puts the
whole quad, transparent margins included, into the game's depth buffer, occluding the Z-tested draws after it; and the
lane (what the resolve reads) is written only by routed fragments, not by the Z buffer. Routing the bolt with the depth
fragment would need a bullet VS variant exporting clip z/w (the VS is untouched today) and cannot survive the draw's
ONE/ONE blend, which makes the lane `dst + src` (z/w about 2: invalid, and a rejected history). Even a clean bolt depth
changes nothing: it is within the 0.02 tolerance of the station's. Loses; the additive-robust flag in `.g` is what
remains of it.

**Flag carriers not chosen.** The scene's alpha (RT0.a) is the bolt's bloom-attenuation alpha law. RT1's lanes carry the
station's motion and `.z` the previous depth, all read at that pixel. Reading RT2 in the write-back instead of carrying
the flag in the resolved alpha costs a 16-byte fetch per pixel (118 MB per frame at 5120x1440 [I]) and a second
computation of `farw`/hold there; the alpha carrier costs one compare and is exact for non-flagged pixels.

## Bounded implementation plan

1. `src/proxy/motion_output.cpp` (`prepare_screen_additive`, its undo at `:6433`), `motion_output.h`: the option, the
   lane-format check, RT1/RT2 bind and masks around the late bolt draw (reuse `bind_target`/mask restore), the variant
   bound at gain 1 too, counters `bolt_flag` / `bolt_flag_refused` on the frame line. `src/renderer/linear_emission_sm1.cpp`:
   the oC2 fragment behind a config bit, host test on the bytecode (beside `test_screen_emission_additive_transform.py`: one oC2 write, DEF K).
2. `src/temporal/resolve.hlsl` (REGION_HOLD family) and `line_mask_ps.hlsl:185`: the three lines above.
   `src/temporal/hdr_writeback_ps.hlsl`, `agx_sharpen_ps.hlsl`, `bloom_agx_ps.hlsl`, `hdr_writeback_dither_ps.hlsl`,
   `bloom_common.hlsl` (extract): the composite fragment; the pass binds the scene texture on those draws and uploads W.
3. Config: `X3M_BOLT_FAR_COMPOSITE` (0/1), `X3M_BOLT_FAR_SHOW` (W), schema entries, `config_schema_inc.h` regenerated with
   `--check`, launcher option `--bolt-far-composite`, x3m.ini template.
4. Fixtures. (a) `run_motion_output.py`, new seam case `seam-bolt-far-flag` beside the `seam-bolt-single-copy-*` family
   (`:898`): a routed far quad (lane `.r` above d1), then the late bolt draw; RT2 readback: `.g > 1` at streak pixels
   and `.r/.b/.a` equal to the quad's, RT1 unchanged, the existing state-restore assertions, the counters. (b)
   `run_temporal_pass.py`, new row family `BOLT_FAR_STREAK` (a generator beside `temporal_far_jitter_line_inc.h`): a static
   hull texture over a far lane (farw = 1) and over a sentinel band, a one-frame additive streak at frame N across both,
   lane `.g = z/w + K*add` at the streak; asserts: frame N colour and age byte-identical to the same row without the flag,
   alpha = 2 + held at the flagged pixels only; the write-back output at flagged far pixels = `resolved + W*(scene -
   resolved)`, its streak addition over far within 10 % of the addition over the sentinel band at W = the band's measured
   retention, every other pixel byte-identical; frames N+1..N+8 colour and age byte-identical to the unflagged row (no
   trail); the 22 `FAR_JITTER_LINE` rows and the lattice 584/90 records unchanged. (c) The host suite for the schema and
   bytecode tests.
5. Ledger: `docs/verification/lod-overlay.md` (the Run 93 A thread) or `temporal-resolve.md` gets the fixture outcome;
   this note gets an "as built" section.

## Flight check (Run 94 A)

The Run 93 A stand, one launch, F8 bursts at a distant station (63-160k units) and into space at the same range, plus a
burst at a lattice-heavy station and a slow pan through the first burst. From the captures, with the existing scripts in
`verification/results/run346-run93a-bolts/`: `bolt_present.py` presented contrast over the station in the 25-110 code
range of space (today -8..+22) [M baseline]; `bolt_band2.py` far/none addition ratio near 1 at W tuned; frame N+1..N+3 at
the bolt-path pixels within +-3 codes of the pre-bolt value (no scar); `run_sparkles.py` (run333) margin on a no-fire stretch of
the same station unchanged against run346 (invariant 2); under the pan the bolt shows at the ordinary retention (no
flag). User question: are the bullets in front of the station, and is there any trail or sparkle where they passed.

## Unknown, and what settles it

- Whether the MRT blending cap holds on the target device: the flag is robust to both, but the seam case should log
  `D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING` once so the `.g` base term is known.
- Whether an RT1 gap (RT2 bound, RT1 unbound) is legal on native D3D9: avoided by binding RT1 masked; no disassembly
  needed.
- W: 0.5 is inferred from the two bursts' space retention (0.39-0.91 keep); the flight sets it. If the silhouette step is
  visible at any fixed W, the refinement is to carry the resolve's own effective retention in the alpha's fraction instead
  of `held`, so the composite reproduces the ordinary-pixel result exactly.
- The hull-under-bolt sample (half stabilised, half current for one frame) is predicted invisible under a bolt 3-10x
  brighter; a run with W = 1 on a plant-heavy station is the direct test.

## Implemented (2026-09-26)

Built as ratified, with one deviation and one refinement:

- **Flag alpha = -(1 + held), not 2 + held.** The ratified positive encoding collided with the scene's own alpha: the
  additive route's ONE/ONE blend sums alpha, and two overlapping bolts over a base of 1 reach 3, which the write-back
  read as a flag (`seam-bolt-single-copy-empty` failed its reference compare with 256 mismatching pixels, worst code
  191, on the first run [M]). A blended scene alpha is never negative (non-negative sources, non-negative factors), so
  the resolve writes `-1 - held` and the consumers test `r.a <= -1`, `share = saturate(-r.a - 1)`. Same information,
  no collision; `seam-bolt-single-copy-empty` passes again with it [M].
- **Branch-free composite in the bloom programs, a `[branch]` in the write-back programs** (`bolt_far.hlsl`:
  `boltComposite` selects with one `cmp`, `boltCompositeBranch` wraps it in `if (r.a <= -1)`): the bloom bundle is
  qualified straight-line (`bloom_shader_limits.py`, 512 slots), the ps_3_0 write-back is not. Both write the same bits
  at every unflagged texel (the select never lerps with weight 0, which would propagate a non-finite scene texel).
- The plain identity write-back (`hdr_writeback_ps.hlsl`) is left untouched: the temporal pass uses it as the lane's
  R32F point copy with nothing at s2 and no c29, where a composite would corrupt the depth history. The composite lives
  in the dithered identity twin (the launcher default), a new dither-off identity twin (`hdr_writeback_bolt_ps.hlsl`,
  bound by `identity_shader()` when the composite is on and the dither off: the identity tonemap and the tonemap-failure
  fallback), the AgX program and its RCAS twin, the identity + RCAS program (`taa_sharpen_ps`, also the 8-bit route's
  and the bloom candidate's sharpen, where no flag can occur), `bloom_agx` and the six extracts. Only the StretchRect
  unwind (a failed shader write-back) shows the resolved image as is: its negative alpha clamps to 0 in the 8-bit copy.
  The `taa_<d>_<f>` capture dumps keep the flag alpha (`capture-format.md`).
- Only the late bullet copy flags: a candidate is a bullet-producer draw (the admitted bullet vertex shader) issued
  after the scene frame's first depth writer; the early copy of a first firing frame and the nine pairs' non-bullet
  additive draws never write the flag (the first build flagged every admitted additive draw).
- `--bolt-far-show 0` is the option off in full at the request site (no flag written, nothing composited; the once row
  says `show_off=1`): a flag nothing consumes would only reach the un-composited paths.
- The sun-shadow cascade apply (on by default, run after the late bullet draw and before the resolve) reads the lane's
  `.g` as the share; a flagged texel's sum `base + K * add` is not separable (add is any bolt value, the share any value
  in [0, 1]), so the apply treats `g > 1` as share 0 for that frame: the factor is 1, the hull pixel under the bolt and
  the bolt keep the scene value, instead of share 1, which at full shadow blacked both out. Pinned by the seam case
  (apply factor 1.0 at every flagged texel against 0.5 on the unflagged, fully shadowed hull; never darker).
- The far program (the screen-gate fallback, not the default) has no parity: its s1 is the R32F depth copy and the
  mask's four UNORM8 lanes are all taken, so no lane bit reaches its resolve; the flag never fires there and the bolt
  shows at the ordinary retention. Left as an open item.

Pieces: `linear_emission_sm1.cpp` (`LinearEmissionSm1Config::far_flag`: `max r2, r0.x, r0.y; max r2, r2, r0.z; mul r2,
r2, c31.z; mov oC2, r2` after the oC0 MOV, K = 32 in the DEF's z lane, 25 words over the native path, host-pinned by
`test_screen_emission_additive_transform.py` on the nine pairs at gains 1 and 2), `motion_output.cpp`
(`prepare_screen_additive`: the flag decided after every admission check and the single-copy drop, refused per draw
with one logged row per reason (`no_variant`, `no_target`, `r32f_lane`, `write_masks`, `bind`); `bind_bolt_flag_targets`
binds RT1 masked 0 and RT2 masked GREEN with the application's masks saved, `unbind_bolt_flag_targets` undoes in reverse
order from `finish_screen_additive` and both rollbacks; the flag variant is created with the cache beside the gained one
and bound at gain 1 too; `motion_output_mrt_blend` and `bolt_far_composite` once at attach; `bolt_flag` /
`bolt_flag_refused` on the frame line), `sun_shadow_cascade_apply_ps.hlsl` (`s = g > 1 ? 0 : saturate(g)`), `resolve.hlsl` (two lines after the alpha history: `boltHeld = max(stabilise.g *
farOpen, stabilise.b)`, `alpha = lane.g > 1 && boltHeld > 0 ? -1 - boltHeld : alpha`), `bolt_far.hlsl` / `bolt_far.h`
(s2, c29), `hdr_pass.cpp` (s2 = the target's container per write-back draw, c29 = W, stage and register saved and
restored only when on; `HdrDisplaySnapshot::bolt_show`), `bloom_pass.cpp` (`BloomPrepare::bolt_scene` / `bolt_show`
validated like `scene`, stage 2 in the bracket, c29 as the sixth bloom register), `capture.cpp` (the settings, the
retained `bolt_scene` released with `scene`), the schema (`bolt_far_composite`, `bolt_far_show`), the launcher
(`--bolt-far-composite`, `--bolt-far-show`; the default launch sends 126 X3M_* variables, 124 before [M]).

Slots (opcode slots of the embedded bytecode, `texldl` counted once, `tools/analysis/inspect_motion_output_profiles.py`)
[M]: `temporal_resolve_far_camera_hold` 907 -> 917 (+10; the other resolve programs byte-identical), `hdr_tonemap` 77 ->
91, `hdr_tonemap_sharpen` 411 -> 425, `hdr_writeback_dither` 12 -> 27 (the branch and its compare), `bloom_agx` 112 ->
120, the extracts 388 -> 460 (gamma), 416 -> 488 (srgb), 299 -> 371 (none), 172 -> 204 / 184 -> 216 / 132 -> 164
(even), all inside the bloom bundle's 512-slot gate (srgb generic 506 with `texldl` at two); the review round added
`sun_shadow_cascade_apply` 489 -> 491, `taa_sharpen` 95 -> 108 and the dither-off identity twin at 15 (the plain copy
stays at 1). Per frame at 5120x1440 by the budget rule (about 1 us per slot per full-screen pass) [I]: the resolve
+10 us, the write-back compare +1-2 us and a scene fetch only at flagged texels, the bloom extract (half resolution,
four taps) +8 us, `bloom_agx` +8 us; measured beside it, the fixture's `FOLD_TIMING` resolve at 5120 px went
2.850 -> 2.939 ms (rest, vote), 2.868 -> 2.948, 3.216 -> 3.328, 2.865 -> 2.885 (pan), 2.882 -> 2.908, 3.225 -> 3.230:
+6..+112 us, a single run each [M]. The flag itself: two SetRenderTarget, two mask reads, two mask writes and their
undo per late bullet draw (one or two per firing frame), nothing on any other draw.

Seam evidence (`run_temporal_pass.py`, `BOLT_FAR_STREAK`, two W, the alpha history off (the default flight) and on;
`temporal_bolt_far_streak_inc.h`) [M]: 128 flagged pixels (the streak over the far hull: 96 inside the far band at alpha
-2.000000, held = 1, and 32 on the ramp columns at alpha -1.501953, held = farw 0.502 after the UNORM8 quantisation: the
continuous share), 0 on the bright non-bolt band or over space; frame N colour rgb, age and depth history 0 differing
bytes against the unflagged run, alpha 0 differing bytes outside the flagged pixels; the dithered identity write-back 0
differing bytes over space and over the unflagged hull, largest error against `W * share * (scene - resolved)` at the
flagged pixels 0.117 code (W 0.5) / 0.109 (W 1.0); the streak's output addition over the hull 0.325 (W 0.5) / 0.618
(W 1.0) against 0.409 over space (ratio 0.795 / 1.511; the ramp columns count at half share); frames N+1..N+8 colour
rgb, age and depth 0 differing bytes in both alpha modes, and alpha 0 differing pixels with the alpha history off. With
the opt-in alpha history on (`--taa-alpha-history`, builtin off) the four hull pixels at the hull / space silhouette in
the streak rows carry a decaying alpha residual through all eight frames (the flagged texel's negative history alpha
enters their alpha blend, clamped to the 3x3 current range, which spans [0, 1] there; colour, age and depth never): a
limitation of that option, reported by the row, not asserted away. The default chain over the flagged frame (the display
dither off so the host model needs none): the AgX tonemap and `bloom_agx` within 0.125 code of the same programs over a
host-composited copy of the resolved image (the FP16 rounding of the copy), the tonemap bit-identical at every unflagged
pixel, the bloom extract 0.000 code; AgX + RCAS within 0.10 code of the host RCAS over the GPU's composited centre and
composite-free neighbour taps (the neighbours see the resolved values by design; against the sharpen of the
host-composited copy it differs by 8.5-14.6 codes at the streak's edges, the informational
`sharpen_vs_host_composite_codes`). The sun-shadow cascade apply over the flagged frame's lane (one cascade containing
every pixel, a map of zeros: fully shadowed, exponent 1): factor 0.500000 on the unflagged hull (1 - share 0.5),
1.000000 at every flagged texel, no flagged pixel darker than without the bolt (0 of 128). The motion fixture
(`seam-bolt-far-flag*`, `verification/results/bolt-far-flag/`) [M]: the late bullet draw admitted and flagged on every
frame with the four-lane RT2, RT2.g + 32.000 at every bolt pixel (K 32 x G 2 x texel 0.5 under the ONE/ONE blend: the
device blends RT2, `post_pixel_shader_blending=1`), `.r/.b/.a` and every other RT2 and RT1 byte unchanged, the state
snapshot restored; the lane-off R32F twin refused once per draw (`r32f_lane`), RT2 untouched; the composite-off twin
flags and refuses nothing; the cascade apply case (`sun-shadow-apply-cascades`, 4570 checks, worst 1.000 code) passes
with the changed program.
