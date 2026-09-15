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
