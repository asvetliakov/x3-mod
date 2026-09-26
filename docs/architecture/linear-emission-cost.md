# Linear emission composition: cost and the route to a usable build

**Ratified 2026-09-15 (orchestrator):** `--linear-emissions` in its full-surface
exchange shape is not pursued; its gameplay cost was never measured and stays
unmeasured. If the user wants brighter emitters, the first step is the
source-only encoded gain on the same pairs (no bracket). The in-place region
bracket port and burst batching are conditional designs, funded only if linear
composition itself is wanted after that gain is judged. No user run is queued.

Design note, 2026-09-15. Question: can `--linear-emissions` (ordered additive
composition in linear light, [linear-emission-composition.md](linear-emission-composition.md))
be made cheap enough for gameplay, or is it a dead end? Constraint: it must work
without `--linear-materials` (off by user decision); production code stays on
documented D3D9.

## Verdict

The route is not a dead end, but its present shape (full-surface exchange
bracket per admitted draw) is the expensive one, and it has **never been
measured in gameplay**: no snapshot log requests producer bit 1 (all
`linear_composition_device requested=` values are 6, 8 or 14 across runs
36–64), and the completed-run table contains no `--linear-emissions` flag. The
cost judgement rests on fixture numbers. Two things follow:

1. If the goal is *brighter, bloomed engine/weapon emissions*, the cheapest
   answer is not this route at all: a source-only encoded gain on the same 20
   pairs adds one PS multiply and no bracket (section 4). It is not linear
   composition and must not be labelled so.
2. If linear composition itself is wanted, the fade route has already measured
   the shape that makes it affordable: the **in-place region bracket** cut the
   same bracket from ≈0.47 ms to a 0.10–0.16 ms floor per draw in the fixture.
   Applying that shape to emission (section 5, option A) is the recommended
   first step, followed by lazy batching of the historical two-draw burst.

## 1. Where the cost goes today (exchange policy 1)

Per admitted indexed draw, `LinearEmissionPass::prepare`/`finish`
(`src/renderer/linear_emission_pass.cpp:909`, `:1019`) and
`MotionOutput::prepare_composition`/`finish_composition`
(`src/proxy/motion_output.cpp:3440–3600`):

| Stage | Work | Bytes per target pixel |
| --- | --- | ---: |
| `save()` | 65 getters (RTs, depth, viewport, textures, states, samplers) | 0 |
| Fused copy | one full-target quad: B = A, E = 0 (two FP16 targets) | 8 read + 16 write |
| Source draw | original DIP with 3 MRTs: B native, E linear, M coverage | native + 16 |
| Composite | full-target quad C = E==0 ? A : encode(decode(A)+E), alpha from B | 24 read + 8 write |
| `restore()` | 285 setters | 0 |
| Exchange | `HdrPass::exchange_target` (13 COM queries, `hdr_pass.cpp:349`), `describe_surface`, `hdr_dirty_` | 0 |

The runtime books this as 56 B/px per exchange bracket
(`motion_output.cpp:3503`): 55 MB at 1280×768, 116 MB at 1920×1080, per draw.
Pool memory is B/E/C/M = 32 B/px (31 MiB at 1280×768, 66 MiB at 1080p).

Per frame while the flag is on, regardless of emission presence: M full clear
in `begin_frame` (0.305 / 0.369 ms medians at 1280×768 / 1080p, fixture) and
the supplemental TAA coverage input (+0.059 / +0.122 ms, fixture),
[composition note](linear-emission-composition.md#complete-twenty-pair-fixture-and-detached-qualification).

Measured bracket costs, all CrossOver/FEX QPC→EVENT completion medians, not
game FPS (paths under `verification/results/bottle-X3/`):

| Evidence | Native | Enhanced | Per bracket |
| --- | ---: | ---: | ---: |
| Detached ordered bracket, 1080p, two single-source brackets (`linear-emission-gpu.json`) | 0.360 | 1.063 | ≈0.35 |
| Detached MRT candidate, 1080p / 768p, two brackets (`linear-emission-mrt-gpu.json`) | 0.605 / 0.541 | 2.401 / 1.471 | ≈0.90 / 0.47 |
| Live route, 1080p, one 25 % source per frame, unpaired (`linear-emission-live-gpu.json`) | 1.610 | 2.308 | ≈0.70 |
| Fade fixture, exchange policy (same bracket shape), 1080p, 1/4/16 DIPs ([fade-region note](linear-distance-fade-region.md#step-3--implemented-2026-09-14-runtime-route-live-witness-and-timings)) | 0.34–0.59 | 1.06 / 3.1 / 8.0–8.2 | ≈0.47 |
| Same, 1280×768 | 0.34–0.91 | 0.74–0.87 / 1.88 / 3.8–5.1 | ≈0.2–0.3 |
| Fade fixture, **in-place region** policy, both sizes, 16 DIPs | same | 2.45–3.34 | **0.10–0.16** (floor at every `f`) |

Fusion of copy and clear is already installed and was worth −0.05 / −0.08 ms
per bracket; the zero-emission composite branch won 45 of 96 pairs (no
benefit); the three-output "C in MRT" shape saves at most 30 % of traffic per
the fade cost model. Those are done or dead.

## 2. What a gameplay frame would pay (estimate, never measured)

Workload: the base five pairs appear as **one burst of two consecutive draws
per affected frame** in the historical capture (16 bursts / 16 frames,
[emission-draw-order.md](../reverse-engineering/emission-draw-order.md#historical-ordering-corroboration));
the twenty-pair set covers 384 archive occurrences, per-frame counts unknown.
Baseline frame time at 1280×768 is ≈13.7 ms (run 11 / run 28 `frame_end`
window medians 4097 / 4104 ms, [fade-region note](linear-distance-fade-region.md#default--user-decision-2026-09-14)).

Estimate at 1280×768: 2 brackets × 0.2–0.3 ms + 0.36 ms fixed ≈ 0.8–1.0 ms
(+6–7 % of the frame) on ordinary frames; a frame with ten admitted engine
draws ≈ 2.5–3.5 ms (+20–25 %). At 1080p double the bracket term. This is a
fixture extrapolation. The decisive measurement is one gameplay run with the
flag on: `linear_emission_frame prepared=`, `pool_traffic_estimate_bytes=` and
`frame_end dt_ms` against the same flight without the flag.

## 3. The destination is not linear, so decode cannot be skipped

Under `--hdr` the FP16 scene target holds gamma-2.2 *code values*
("Never call this target scene-linear",
[scene-linear-materials.md](scene-linear-materials.md)); every uncovered writer
still writes encoded values into it. Skipping decode is only possible after a
whole-writer conversion, which the boundary decision rejected. This also rules
out "render emissions directly into FP16 with a rewritten blend":
`encode(decode(A) + L)` is not a fixed-function blend. The only blend-expressible
brightening is `A + k·s` in encoded space, which is section 4.

## 4. Is the user's goal reachable without composition?

At gain 1 the linear route is not systematically brighter than native. Native
FP16 ADD/ONE/ONE yields `decode(A + f·s)`; the route yields
`decode(A) + f·decode(s)`. For unfaded or overlapping sources
`(a+s)^2.2 ≥ a^2.2 + s^2.2`, so native is brighter; only for faded sources
over black (`f^2.2 < f`) is linear brighter. The brightness lever is the gain in
either route. `--emission-gain` exists only inside the bracket
(`tools/manage.py:87`, `linear_emission.cpp::source_output`, gain in c31);
`--screen-emission-gain` belongs to the bullet packed route and requires
`--linear-materials` (`manage.py:212`), so it is unavailable now.

**Source-only encoded gain** (`--emission-source-gain k`, additive pairs only):
a PS variant of the same 20 exact pairs that multiplies the native RGB output
by `k` before the game's own ADD/ONE/ONE blend; no MRT, no copy, no composite,
no exchange, no M, no per-frame clear. Per-draw cost is the two `SetPixelShader`
calls the same-draw motion route already pays for substituted programs
(`motion_output.cpp:3689` set, `:3088` restore), under
the existing admission predicate (ADD/ONE/ONE, Z-write off, mask 15, no sRGB
sampler); the 71 screen-blend draws of the same pairs stay native because their
blend fails the predicate. Bloom sees `decode(A + k·s)` and its threshold is on
exposed-linear luminance ([hdr-bloom-filter.md](hdr-bloom-filter.md)), so
brighter emitters cross it. Native Windows: pure documented PS 2.x bytecode.
What it breaks: nothing native-visible, but it is superadditive on overlaps (the
native law) and is *not* linear composition; TAA reactivity is whatever the
native draws get today. It must never be applied to screen/alpha blends, where
source RGB is also the destination attenuation (`k·s > 1` goes negative).
Cost estimate: unmeasurable at frame scale (two setter calls per draw).

### Implemented (2026-09-15): `--emission-source-gain G`

Option `--emission-source-gain G` (`X3M_EMISSION_SOURCE_GAIN`, finite 1..8,
default `1.0` = off, launcher-validated) is the ratified first step. It is
not linear composition and is never labelled so.

- Transformer: `linear_emission_source_gain_variant` (`src/renderer/linear_emission.cpp`)
  admits the same ten exact PS2.0/PS2.x programs of the twenty reviewed pairs
  (same profile table and `structure`/`original_shape` proof as the linear
  route) and inserts one `def c31 = (G, 0, 0, 0)` before the declarations and
  one `mul r0.xyz, r0, c31.x` immediately before the untouched native
  `mov_pp oC0, r0`. Every original word, the native output MOV and the raw alpha
  lane are retained; +10 DWORDs, +1 arithmetic slot (max 8 of 64). Gain 1
  returns the original bytes (byte identity: no option, no variant).
- Proxy: `capture.cpp` reads the variable once (`emission_source_gain_mode`
  line only when the value is not 1 or unparsable; out of range keeps 1).
  `MotionOutput::configure_emission_source_gain` before attach; the variant is
  created in `register_pixel_shader` (`emission_source_gain_variant` line),
  the pair is identified at `SetVertexShader`/`SetPixelShader` by the existing
  `linear_emission_pair_reviewed` lookup (one lookup per setter, cached as
  `shadow_.source_gain_eligible_variant`), and `prepare_source_gain` binds the
  variant natively for one draw when the shared colour law
  `renderer::linear_emission_source_gain_blend` admits the shadowed state:
  ALPHABLENDENABLE on, SRCBLEND ONE, DESTBLEND ONE, BLENDOP ADD,
  SRGBWRITEENABLE off, with the FP16 redirect active and the scene bound;
  `after_draw` restores the application's program. SEPARATEALPHABLENDENABLE
  and the alpha triple (SRCBLENDALPHA/DESTBLENDALPHA/BLENDOPALPHA, shadowed
  in `composition_blend[3..6]` by the same setter hook and resync, no per-draw
  getter) never gate: the variant multiplies rgb only, so the additive colour
  law holds whatever the alpha blend does. Run 26 (run65) showed why: every
  engine candidate came with `sepalpha=1` (ONE/ONE/ADD colour, 11 of the 16
  logged samples) or the screen blend ONE/INVSRCCOLOR (5), so the original
  "separate alpha off" gate admitted zero draws. Screen refuses with the
  distinct reason `screen_blend` (gained screen `bg + G*s*(1-bg)` is not additive and darkens where the FP16 scene holds bg > 1); every other blend, sRGB write or op refuses as `blend`; unknown state
  and inactive redirect refuse silently (counted); the bracket routes
  (`--linear-emissions`, fade, screen) take precedence for a draw they admit.
  A failed bind restores at once; a failed restore is the existing
  `motion_state_lost_` condition. Observability without capture: one
  `emission_source_gain_frame device= frame= gain= admitted= refused_blend=
  refused_screen= refused_other= refused_unknown= refused_state= bind_failures=`
  line per frame that saw at least one candidate draw
  (`refused_other = unknown + state + bind`), and
  `emission_source_gain_refused ... reason=blend|screen_blend blend= src= dst=
  op= sepalpha= srcalpha= dstalpha= opalpha= srgb=` samples capped at 16 per
  reason per device epoch (-1 = state not shadowed).
- Actual prerequisite: `--hdr` (FP16 scene target so values above 1 survive
  for bloom/exposure). `--hdr` itself requires `--motion-output`, which owns
  the shader registration, the pair identification and the render-state
  shadow the admission reads; nothing else is required (no `--linear-materials`,
  `--linear-emissions`, `--taa` or `--ownership`; the DLL gate reads
  `hdr_requested` only). Exclusive with `--linear-emissions`: the launcher
  rejects the pair and the DLL refuses the gain with
  `emission_source_gain_mode ... refused=linear_emissions`, since the bracket
  carries its own `--emission-gain` for the same pairs and a draw that route
  admits would otherwise take precedence silently.
- Cost: off = one null-pointer test per draw and one pointer copy per
  `SetPixelShader`; on = two native `SetPixelShader` calls and about ten
  compares per admitted draw, one MUL per fragment. No bracket, no copies, no
  composition, no per-frame clear, no constant traffic.
- Evidence: `verification/analysis/test_linear_emission_source_gain.py` (host
  oracle over the 10 programs / 20 pairs: gain 1 identity, exact DEF+MUL
  insertion, alpha and native output untouched; launcher and DLL gate) and
  `run_linear_emission.py --mode source-gain` (fixture mode `--source-gain`:
  80 cases, native vs. gain 1/2/3.5/8 over zero and non-zero FP16
  backgrounds; law `bg + G (native - bg)`, alpha and gain-1 images bit-exact).
  Ledger row: [screen-emission.md](../verification/screen-emission.md).

### Screen substitution (2026-09-16): one `--emission-source-gain G` for all twenty pairs

The 2026-09-16 engine/effect family split (`--effect-source-gain`,
Ctrl+Shift+F4; history in `docs/archive/` and the screen-emission ledger) is
**undone**. Its premise was that the engine glow and the impact sprites are
separable by shader identity; the material survey
([effect-shader-users.md](../reverse-engineering/effect-shader-users.md))
shows the engine glow is drawn by the same DEFAULT pair as the jump gate and
the effect sprites (VS `d5e1c753` / PS `8360f422`, from
`objects/effects/engines/*`), and that 92 of the 122 engine materials blend
ONE/INVSRCCOLOR (screen), which the source gain refused as `screen_blend`
(run 28: refused_screen 124,827, refused_state 101,263, admitted 78,307 =
the ONE/ONE gate materials). That is why the engines never responded to the
gain. Two decisions follow: one option, one hotkey, one gain for all twenty
pairs (`X3M_EMISSION_SOURCE_GAIN`, Ctrl+Shift+**F6**, which since the run 41
regrouping also carries the `--hull-emitters` guide lights: an
`--emission-source-gain` above 1 implies them and hands them its value, which
an explicit `--hull-emission-gain G` still overrides; a passed
`--effect-source-gain` is a launcher error naming the replacement), and the
screen draws are admitted through a blend substitution.

- Why screen cannot carry the gain as is: `INVSRCCOLOR` reads the *gained*
  output, so `S·G + D·(1 − S·G)` goes negative for `S·G > 1` on the FP16
  target (and darkens wherever `D > 1`).
- Substitution: for an admitted draw whose colour blend is ADD/ONE/INVSRCCOLOR
  (separate alpha as the admission already allows), `prepare_source_gain`
  sets `DESTBLEND ONE` for that draw before binding the gain variant, exactly
  as `--screen-emission-additive` does for the bullet pair
  (`prepare_screen_additive`, set unconditionally, restored through the
  setter shadow). The draw becomes additive `G·S + D`; `finish_source_gain`
  puts the application's program and its shadowed DESTBLEND back after the
  draw. A failed DESTBLEND setter leaves the draw native (counted
  `refused_screen`, sample `reason=screen_substitute_failed`); a failed
  program bind after the substitution unwinds the DESTBLEND through the
  shadow before returning; a failed restore is the lost-state path
  (`motion_state_lost_`, render-state resync, TAA invalidated). Only while
  the F6 toggle is on; off, the draw goes out native (screen blend, native
  program). The ONE/ONE path is unchanged.
- Accepted trade (user preference: cheap `> 1.0` emitters over the exact
  blend law): over black the additive result equals the native screen draw
  for `S ≤ 1` (identical when `D = 0`); over a lit background it is brighter
  by `D·S` (native `S + D − D·S`, substituted `S·G + D`), never negative and
  never darker than native. With separate alpha off (the engine materials)
  the alpha lane also changes from `a + D.a·(1 − a)` to `a + D.a`, the
  additive bullets' alpha law; `a` itself is untouched by the variant.
- Blend law (`renderer::linear_emission_source_gain_blend`): sRGB write now
  refuses before the screen test (the substitution needs the linear FP16
  target), so screen + sRGB is a `blend` refusal, not `Screen`.
- Cost: one native `SetRenderState` before and one after a substituted screen
  draw; nothing on the additive draws or on any other draw (the pair pointer
  test and the toggle flag are unchanged). One variant per original program
  (ten), no per-family creation.
- Registry: `linear_emission_pair_index(vs, ps)` (0..19 or the count);
  `LinearEmissionFamily`, `linear_emission_pair_info` and
  `linear_emission_family_name` are gone from the renderer and the proxy.
- Observability: `emission_source_gain_pair device= frame= vs= ps= gain=
  screen=` once per pair per device epoch (`screen=1` when the first
  admission substituted); `emission_source_gain_frame device= frame= gain=
  admitted= admitted_screen= refused_blend= refused_screen= refused_other=
  refused_unknown= refused_state= bind_failures=` (one admitted counter,
  `admitted_screen` the substituted share, `refused_screen` only screen
  draws whose substitution failed); `emission_source_gain_refused ...
  reason=blend|screen_substitute_failed`; `emission_source_gain_refused_state
  device= frame= vs= ps= hdr= scene= open= recording= primitives= msaa=
  blend= src= dst= op= sepalpha=`; `emission_source_gain_toggle device=
  frame= accepted= enabled= requested= gain=`; `emission_source_gain_variant
  ... gain=` without a family.
- `refused_other` in run 27 (66,024 of 124,774 candidates) was entirely
  `refused_state` (`refused_unknown` 0, `bind_failures` 0): the draw failed the
  device-state predicate (`hdr_state_`, `route.scene`, `scene_open_`, state
  block recording, zero primitives or MSAA) before the blend law ran, so no
  blend triple exists for it by construction. Its shape is a constant 10 draws
  per frame in 6,181 frames from the first candidate frame (1305) on, plus
  6/4/2 per frame elsewhere, present in every capture frame (3510–3517,
  4330–4337) as `d5e1c753`/`8360f422` `motion_input` rows with `admitted=0`
  and regardless of firing; that matches the historical ~10-per-frame late
  overlay population of the same pair
  ([emission-draw-order.md](../reverse-engineering/emission-draw-order.md),
  "924 late overlay draws"), i.e. `route.scene` false. Inference, not a
  witness: the state sample line is the witness. It is not engine glow
  and is not admitted. No SRCALPHA/ONE or other premultiplied additive
  population exists in runs 26/27 (`refused_blend` 0 in run 27), so the
  truth table is otherwise unchanged: admitting SRCALPHA/ONE would be valid
  for the colour law (`dst + a·G·s = dst + G·(a·s)`, alpha untouched) but
  there is no draw to admit and fail-closed stays.
- Evidence: host `test_linear_emission_source_gain.py` (registry 20 rows
  without a family, compiled 80-cell lookup; launcher gate and the removed
  option's error; substitution order, rollback and restore pins; frame /
  pair / state line grammar; fixture and runner coverage) and
  `run_linear_emission.py --mode source-gain`: 120 ONE/ONE cases byte-identical
  to the pre-change raw baseline, plus 40 `source_gain_screen` cases (20 pairs
  × black and grey 128/255 backgrounds, gains 2 / 5 / 8 substituted, gain-1
  slot native): the substituted images match `G·s + D` and the gain-1 slot
  the native screen law within one FP16 code
  ([screen-emission.md](../verification/screen-emission.md)).

## Hull light-map gain (2026-09-18): `--hull-lightmap-gain G`

What `--hull-emitters` reaches and what it does not
([hull-self-illumination.md](../reverse-engineering/hull-self-illumination.md)
§3): its twelve `standard_lighting` / `XT_standard_lighting` programs are the
ONE/ONE emitter materials (position lights, deco flares, warning signs, warp
tunnels) and its admission takes only ADD ONE/ONE draws, so station windows
and hull lights, which are opaque draws of the **race** hull programs
(`argon.fb`, `argon2s.fb`, `split.fb`, `khaak/teladi/xenon.fb`), never see
it. Those windows are the light-map (self-illumination) term: 100 of the 108
reviewed material pixel originals (66 hull, 20 palette, 14 XT; the four glass
and four asteroid programs have no such term) fetch `LightMapTexSampler` as
their last `texld` (s2 in the DEFAULT layouts, s3 in BUMPMAP) and add its RGB
at weight 1 to the lit colour in the final colour instruction (`add oC0.xyz,
r1, r0`; XT `mad oC0.xyz, r1, r2.z, r0`; the six XT `terra` rows through one
intervening `mad r1.xyz, r2, r3.w, r1`), while `.w` feeds the alpha `lrp`
(`g_EnableGlow`). No constant scales the RGB.

**Implemented.** Option `--hull-lightmap-gain G` (`X3M_HULL_LIGHTMAP_GAIN`,
finite 1..8, default 1 = off; requires `--hdr`; excludes `--linear-materials`,
whose converted programs carry `--lightmap-emissive-gain`; composes with
`--original-fill`). The transform lives in the original-program transformer
(`original_fill_transform`, exposed as
`linear_material_hull_lightmap_gain_pixel_variant`): the fill variant (K, or
the plain motion/depth variant at K = 0) plus one shader-local
`def c223 = (G, 0, 0, 0)` and one `mul rL.xyz, rL, c223.x` immediately after
the pinned fetch (`Pixel::texture[bump?3:2]`, `XtPixel::texture[bump?3:2]`
with `texture_reg`), +10 DWORDs, +1 executable instruction, +1 weighted slot
(max base 184, gained plain variant 185, gained share variant 264 of 512). `lightmap_term` proves the term on the original
(fetch form and stage, no write or read of rL.xyz before the single consumer,
no flow control in the tail, nothing after the final reads it, c223 unread)
and again on the emitted program (one DEF, one read, the MUL directly after
the fetch, no motion/fill/share insertion touching rL.xyz), so G = 1 and the
eight programs without the term are byte-identical to the fill variant and
anything else fails closed. Native Windows: documented ps_3_0 bytecode; c223
is the last float constant and is read by no original (max c26).

Runtime: one variant per reviewed program at registration
(`hull_lightmap_variant`, beside the fill variant), selected in
`bind_variant_pair` over the plain or fill-composed motion variant of the
routed reviewed pair while the FP16 scene is active and the F4 flag is on;
undone with the route. Per-draw cost: one bool test
(`shadow_.hull_lightmap_pair`, false without the option) and two pointer
compares on the routed path, no state read, no allocation; creation cost is
one more transform and `CreatePixelShader` per reviewed program (108). Every
routed opaque draw of the 100 programs carries the gain (unrouted draws keep the
original program, as with `--original-fill`): the black 32x32 placeholder
light maps of props and emitter materials multiply to zero, so no admission
policy is needed, and any non-window light-map art (panel stripes, decals)
brightens with the windows. **Ctrl+Shift+F4** (removed 2026-09-26, [in-game keys](comparison-hotkeys.md#removed-2026-09-26)) toggled this gain alone
(its own flag since the run 41 regrouping; the guide lights of
`--hull-emitters` moved to Ctrl+Shift+F6 with the effects gain they now take
by default, and `hull_emission_gain_toggle` logs the driving key and both
families' states); per-frame `hull_lightmap_frame gain= fill= admitted= toggled=`.
The launcher forwards G = 4 with `--hdr` unless another value is given (user
selection after run 41 C / run128); the DLL's own default stays 1 = off.
The sun-share lane (`--sun-shadow-lane`, the user's configuration) carries it
too: the original share producer (`linear_material_original_sun_share_pixel_variant`,
its c212/c221 DEFs and r11-r23 disjoint from c223 and rL) takes the same
fragment, a gained share variant (`sun_original_lightmap_variant`) is created
beside every share variant and the lane binds it while the flag is on. The
share follows its law on the gained colour, `s' Y(C') = s Y(C) = Y(S)`: the
light map is unlit, so the sun's code-value contribution is unchanged and its
fraction of the brighter pixel is smaller, which is what the apply must
darken by. The apply removes `s'·C'` from the pixel, so a shadowed pixel's
chroma follows the gained colour (inherent to the scalar luma share: the
window tint is not separated from the sunlit hull). The game light maps'
texel content is unverified (the RE note infers windows from the term's
position, not from decoded art); a user bracket settles it. Evidence: [screen-emission.md](../verification/screen-emission.md)
(2026-09-18 rows).

## 5. Options for the linear route, ranked

| Rank | Option | Saving | Basis | Risk | Windows (documented D3D9) |
| --- | --- | --- | --- | --- | --- |
| A | **In-place emission policy**: new policy bit sharing the fade in-place bracket (`LinearCompositionPolicy::DistanceFadeInPlace` shape, RT0 = A, B = region backup, composite A\|R from B\|R and E\|R). Region from `derive_fade_region` when the draw has a verified object scope; else full viewport in place. | Full viewport: 56→48 B/px, no C, no exchange/ack/descriptor refresh (est. 0.47→≈0.3 ms at 1080p). With a region: 0.47→0.10–0.16 ms per draw (**measured** for the fade shape). | Fade step 3 timings above; identical programs and pool | Region validity for effects geometry (unknown, section 7); otherwise the same failure ladder the fade route runs in game (runs 11–21, witness `outside=0`) | Yes: scissor caps already required by fade |
| B | **Lazy batching of consecutive admitted draws**: keep the bracket open after the DIP; close on the first device call that is not another admitted draw or a whitelisted setter (any RT/depth/viewport/scissor change, Clear, StretchRect, GetRenderTargetData, Lock, query issue, non-admitted PS/blend state, EndScene, Present). | One backup + composite + state floor per burst instead of per draw: −0.34 ms per two-draw burst measured (1080p, 0.728 vs 1.063), −0.88 ms MRT shape; with A, ≈ −0.1–0.16 ms per extra draw (est.) | RE: 16/16 historical bursts are consecutive draws with identical state except VB/IB/texture and VS c10–c11 | Hook-side: every device method must pass the close check (the proxy vtable already does); a missed reader would expose pre-composite A. The RE's "adjacent ordinals do not qualify batching" concern is about the capture, not a runtime that sees every call | Yes |
| C | **State-floor reduction**: skip getters whose value MotionOutput's shadow already knows, set only differing states, keep the pass's fullscreen state across brackets of one frame with dirty tracking. | −50–70 % of the 0.10–0.16 ms floor (est.) | 65 getters + 285 setters per bracket is the measured floor | Stale shadow → wrong restore; keep the existing 34-restoration live check | Yes |
| D | **Cheaper temporal publication**: clear M with `Clear(rects)` over last frame's region union; publish "known empty" without a clear on frames with no admitted draw (consumer skips dilation). Do **not** drop M: VS programs without a motion row have no other reactivity. | −0.3 ms/frame fixed (est.; the fixture clear figure includes overhead) | M clear 0.305/0.369 ms measured | Consumer contract change in `TemporalPass` (host-testable) | Yes |
| E | Cap brackets per frame, fallback to native | No average saving; bounded worst case | — | A refused *required* producer stops the frame and drops TAA (`prepare_composition`), so the cap needs a coverage-only submission (source with M as the only extra target) rather than a plain native draw | Yes (RT1/RT2 = NULL is documented) |
| F | End-of-frame accumulation (E all frame, one composite) | Fixed ≥0.8 ms/frame at 1080p (E clear 0.43 + composite 0.72 measured) unless region-bounded | — | Changes the law: every burst is followed by 3–16 nonadditive blends (46 screen, 16 particle, 16 alpha draws in the sampled tails) that would no longer attenuate the emission | Yes, but rejected on correctness |

A then B then C is the order: A removes the bandwidth term and the exchange, B
halves the remaining floor for the observed workload, C attacks what is left.
D is independent and small. E is a safety valve to add with A. F is abandoned.

Expected result of A+B at the historical workload (estimate): one burst per
frame ≈ 0.15–0.25 ms plus ≈0.1 ms fixed after D, i.e. ≈2 % of a 13.7 ms frame
at 1280×768; ten-draw frames ≈ 0.6–1.0 ms. That is usable if the estimate holds.

## 6. Recommended first step and its acceptance measurement

Implement option A (`AdditiveEmissionInPlace`, policy bit 16) in
`linear_emission_pass.cpp`/`motion_output.cpp`, reusing the policy-4 bracket
with the emission MRT layout, plus a capture-frame `emission_region` log line
(bound/unbound, `f_permille`) so the region question is answered from the log.
No shader change; the 20 augmented PS programs and the composite program are
unchanged.

Acceptance, in order:

1. Host: existing `test_linear_emission_pass_host` and `test_linear_emission_live`
   extended with the in-place emission twin (image bit-exact against the
   exchange policy on the same inputs, as the fade twin was).
2. Fixture (`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py
   python3 verification/probe/run_linear_emission_live.py`): the 1080p
   16-DIP window at ≤ 3.0 ms against the exchange policy's 6.6–8.2 ms, and the
   per-bracket delta ≤ 0.2 ms at both sizes — the numbers the fade in-place
   step produced for the same shape.
3. One user run with `--linear-emissions --emission-gain 2` (no linear
   materials): `linear_emission_frame prepared=` histogram, `region_pixels`,
   `in_place_linear == prepared`, `emission_region` bound fraction, and the
   `frame_end dt_ms` window median within +0.5 ms per frame of the same flight
   without the flag. The same run answers the appearance question the critic
   posed ([material-investment.md](material-investment.md#critic-reassessment-after-sparse-lighting-clarification)).

If the user only wants brightness, run section 4's source gain first: it needs
no bracket work and its acceptance is the same run with `frame_end` unchanged
and a screenshot pair.

## 7. Unknown, and what settles it

- Whether engine/effects draws carry a verified object scope and part AABB for
  `derive_fade_region`. The RE says the historical bursts report scoped
  object context at depth one, and the deferred path reaches the same bound
  ([render-node-bounds.md](../reverse-engineering/render-node-bounds.md#3-the-deferred-path-reaches-the-same-bound)),
  but the billboard/direct-clip VS families are excluded by identity and the
  emission DEFAULT VS applies a texture matrix. The `emission_region` log line
  in the first user run settles it; until then the policy falls back to the
  full viewport in place, which still removes the exchange.
- Actual per-frame admitted draw count for the twenty pairs in gameplay: the
  same run.
- How much of the 0.10–0.16 ms floor is Wine/FEX call overhead versus native
  driver cost: a native Windows measurement is impossible here; option C's
  benefit is estimated.
- Whether `hdr_resolved_ = nullptr` after each exchange forces extra resolve
  work per bracket: disappears with A (no exchange).

## Abandon

End-of-frame accumulation (law change and fixed cost); skipping decode (the
target is encoded); the three-output C shape and the zero-emission branch
(measured no gain); dropping M coverage (ghosting on VS without motion rows);
any further work on `--screen-emission-gain` while linear materials are off.
