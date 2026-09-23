# Volumetric fog verification ledger

Owning design: `docs/architecture/volumetric-fog.md` ("Stage 1 implementation"). One entry per run of
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_fog_pass.py`
(record `verification/results/bottle-X3/fog-pass-gpu1.json`, report `fog-pass-gpu1.txt`; the Wine log stays local).

| Date | Change | Checks | Lit fraction vs CPU (mean abs / within one 1/16 step) | Composite law | Fenced chain 1280x768 / 1920x1080 | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-09-19 | stage 1 (pass, four programs, live glue, launcher; uncommitted worktree) | 85 / 0 failures | map twin 1.4e-4 / 100 % (both jitter phases, cascade 0 absent); analytic oracle 2.0e-3 / 99.87 %; R32F RT2 vs the view-depth lane 1e-6 / 100 % | 0 of 2,949,120 channels over 2e-3 relative (max 9.8e-4), alpha unchanged, energy bound 0 violations, no channel darker than `L exp(-tau_max)`; sky cap closed form within 1.7e-3 | sky update on 1.16-1.40 ms, off 0.75-0.98 ms / on 1.99 ms, off 1.29 ms | PASS |
| 2026-09-19 | review fixes (lost-device and attach retry policy, cut ends the sector hold, env prerequisites, sky update 1 frame in 32) | 89 / 0 failures (+4: attach retry on the same object, `D3DERR_DEVICELOST` in target creation leaves nothing retained and the state/scene untouched, the retry allocates and is bit-identical) | unchanged (map twin 1.4e-4 / 100 %) | unchanged (0 over, max 9.8e-4) | 768p: sky on 1.06-1.07 ms, off 0.87-0.88 ms | PASS |

**Sky update cost** (review question): fenced one by one the two sky quads read 0.31 ms each, but the whole transaction
with and without them differs by 0.19 ms (1.07 vs 0.88 ms, both 768p blocks; submit 0.20 vs 0.14 ms), so about two
thirds of the per-quad figure is the fence itself; the real cost is about 0.06 ms CPU plus 0.13 ms of two render-pass
switches. The live pass now updates on 1 frame in 32 with weight 0.5 (about a 1 s time constant), about 0.006 ms per
frame amortised. Folding both levels into one 1x1 quad would save one of the two switches on that frame; not done
(it changes a program, hence every manifest pin again).

**What the fixture proves** (synthetic 1280x768 RT2: a 800x600-unit plate at z = 2,000, a wall at z = 20,000 below the
horizon, sky above; sun toward (0, 0.6, 0.8); three 512-texel cascades of 4k/16k/64k units rasterised on the CPU;
`tau_max` 0.05, R 3,000 so the shaft is measurable; timing at the production R 10,000, `tau_max` 0.02):

- *Lit fraction*: GPU readback against `verification/probe/fog_reference.h` evaluated twice: with the same maps (the
  shader's twin) and with an analytic ray/plate/wall shadow test that uses no map (independent of the lookup code; it
  differs only at map-texel edges).
- *Shaft behind the occluder*: mean F 0.8595 over the 6,700 half pixels whose rays pass under the plate; *sky*: every
  sentinel half pixel beside the plate has F exactly 1 (far samples leave the last cascade and count lit), and its
  composite equals `L exp(-tau_max) + hue E p (1 - exp(-tau_max))` in closed form.
- *Energy bound*: added linear radiance <= `4 max(E) p_HG(1) (1 - exp(-tau_max))` = 0.1294 on every channel.
- *Sky hue*: the 1x1 history equals the CPU mean of the same 9,216 taps within 5e-4 (coverage 44.6 %); a second
  update blends by 0.25 within 1.3e-3; `update_sky = false` leaves it bit-identical; a frame without sky keeps it.
- *Cascades*: cascade 0 absent falls through to cascade 1 (twin 1.5e-4); no valid cascade gives F = 1 everywhere and
  the veil is still applied (the replay-refused frame).
- *State*: hostile render/sampler/texture-stage state, 17 pixel constants, viewport, scissor, RT0-RT2 (RT2 is the
  sampled depth texture, as in the route), depth surface and vertex textures byte-identical around every execute;
  RT1 and RT2 contents byte-identical after it (no write to the motion or depth targets).
- *Off path*: `tau_max = 0`, unknown query state, g 0.95, a non-unit sun and an 8-bit target are refused at
  validation with the state untouched, the target bit-identical and nothing allocated. (The live off path never
  reaches the pass: `fog_requested_` guards every site; host test below.)
- *Faults*: third target allocation fails -> every partial target released; third program creation fails -> detached;
  march draw, `StretchRect` and sky draw failures restore the state and leave the target untouched; recovery is
  bit-identical; `D3DERR_DEVICELOST` in the composite stops restoration and a later execute still runs.
- *Reset*: `before_reset` releases the 8 target interfaces and the block (15 -> 6 references), execute and prepare
  refuse while pending, and after a native `Reset` both F and the composited target are bit-identical to the first run.
- *Gates*: ps_2_0, 64 slots, missing INVSRCALPHA and missing RT-to-RT StretchRect are refused by name; a 512-slot
  device attaches (largest program 224 slots: the march).

**Cost** (EVENT-fenced windows, detached, CrossOver Preview; not game frame time). CPU submit of one transaction:
0.14-0.15 ms without the sky update (149 device calls), 0.20-0.21 ms with it (172 calls; 1 frame in 8). The backend
refuses `TIMESTAMP` queries (`gpu_timestamp_ms = -1`), so the GPU side is the fenced window minus the submit:
0.62-0.83 ms at 1280x768 and 1.14 ms at 1920x1080 without the sky update, up to 1.20 / 1.79 ms with it. Per quad,
each fenced alone: march 0.56-0.68 ms, composite 0.54-0.65 ms, the two sky quads 0.31 ms each although they shade
64 + 1 pixels (the per-quad floor of this backend). Against the note's proposal (+0.7 ms median CPU in a paired
flight): the CPU submit is inside it; the fenced chain (0.75-1.40 ms) is above the note's 0.5-0.9 ms GPU estimate in
the blocks with the sky update. The paired on/off flight that decides this has not been flown.

**Offline cross-check against the stage 0 mock** (`python3 tools/analysis/fog_shader_twin.py /tmp/x3-bottleX3-run174
28644 --tau 0.02 --g 0.3`; Argon Prime, capture 4). The script's mock-law branch reproduces the mock's own printout
(`fog_offline_mock.py ... --tau 0.02 --steps 16 --g 0.3 --albedo-mix 1 --aerial-half 0`): F geometry mean 0.834, sky
mean 0.872, F < 0.9 on 55.8 %, F < 0.5 on 1.7 %, E_sun 2.614. The shader law (slots 1-3 only, 8-bit F, 9,216 sparse sky
taps clamped at 4, ceil half grid) against it: |dF| mean 0.0011, p99 0.019, 0.09 % of pixels beyond one 1/16 step;
hue within 0.3 %; in-scatter luma mean 0.00591 in both, relative difference p50 6e-4, p99 2.1e-2. Stated tolerance:
mean |dF| <= 0.002, p99 <= 0.02, mean in-scatter within 0.2 %. Limits: this is a numpy twin of the shader law, linked
to the GPU by the fixture's map twin, not a GPU run on the dump; both branches use the mock's image-estimated E_sun,
because the dump does not carry the sun light's colour words, so the tracked `E_sun = pi decode(Color0)` is not
covered; dropping cascade 0 (the own-ship map) changed F by at most 0.137 on this frame.

**Host tests**: `verification/analysis/test_volumetric_fog.py` (10 tests: strength ladder, sun radiance law and clamp,
gate order, sector latch ramp/hold/forced/gap, the four programs' slot counts <= 512 and manifest word counts, reference
invariants, the runner's parser and acceptance refusals, launcher dependencies/ranges/inherited environment, production
wiring); the sampler fixture gained the Ctrl+Alt+F9/F10 chords (14,375 checks, was 13,366). Host fixtures that compile
`MotionOutput` functions gained fog doubles (`linear_material_live_fixture.cpp`, `motion_hdr_scene_fixture.cpp`,
`motion_wrap_state_fixture.cpp`). Full host suite on 2026-09-19 before those fixes: 2,306 tests, 13 failures, of which
11 were this change's (9 bloom manifests pinning the generator hash, 2 fixtures without doubles; all fixed and rerun)
and 2 are in `test_motion_output_runner` (`seam-taa-fade-route-overlay-*` cases, HDR manual-exposure set), whose
inputs this change does not touch. The suite was not rerun in full after the fixes; the affected modules were.

**Generator**: `generate_rigid_motion_pixel.py --check` PASS over 31 programs; adding the four entries changed the
generator's hash, so its 27 other manifests and the 9 bloom manifests were regenerated natively
(`generate_bloom_programs.py --d3dx <X3 bottle>`): one `tool_sources` line each, no embedded header changed.

**Build**: the proxy DLL links with `src/renderer/fog_pass.cpp` (scratch build, not a candidate);
`check_no_x87.py`: 558 reachable functions, 0 violations.

**Not verified**: anything in the game (the pass has never run on a real frame: sector latch timing, `E_sun` from the
real colour words, g, shimmer under TAA, glow dimming, cost in a flight); native Windows execution; the stage 2
temporal fixture does not exist yet.


## Sector-record diagnostic implementation (2026-09-20)

`--sector-background` / `X3M_SECTOR_BACKGROUND` is opt-in and observational only:
first successful BeginScene once/frame, Present fallback when no BeginScene
occurs; copied sample, no row pointer retained, executable identity gate and
LastError preservation. Once-per-second and change rows carry the §11.5 fields,
raw camera consistency, separately computed far floor and parent-sector cross-check.
Invalid/missing records never drive rendering; the existing card-presence rule remains.

Source: `src/proxy/sector_background.h`, capture/launcher wiring. Normal synthetic
ready sample is 212 bytes, 21 bounded reads including exactly one 288-byte row;
registry walk capped at 32 links. Option off performs no sample, clock or gate work.
Deep independent review accepted the source and evidence. Focused host command:
`PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_sector_background verification.analysis.test_object_capture verification.analysis.test_volumetric_fog`
passed 17 tests (5.126 s); reader 64 checks and extracted production wrapper 15.
Reviewer separately reran the four diagnostic tests (2.240 s). Isolated capture.cpp
MinGW i386 compilation with SSE2 and the required four-byte incoming-stack flags
passed. No Wine execution or game launch was used for this checkpoint.

Limitations: same-thread layout/lifetime proof is static; the existing engine-memory
validation/copy race remains. Native Windows execution and the live named-sector,
menu/load and gate-jump comparisons are unverified. No sample is possible without
D3D frame traffic. The raw-camera/far-floor correction and instruction addresses
are recorded in [sector-fog §11.4](../reverse-engineering/sector-fog.md#114-safe-read-recipe).


## Run 48 B — run180 (2026-09-20)

Bottle X3, CrossOver Preview, arm64 Wine/FEX; session environment records
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1` (WineArch arm64 from the bottle
configuration, not an independently logged session key). Four 32-frame bursts:
17726–17757 / 19234–19265 / 51755–51786 / 59600–59631. By the user's chronology
these are Argon Prime fog on/off, The Hole, Atreus' Clouds; sector names are not
in the log. Recorded strength 0.02, anisotropy 0.3, fog on/off/on/on. All 128
captured frames have sun lane available and sun-shadow apply successful. The user
forgot to inspect advertisement signs specifically and noticed no issue.

The user prefers **0.02**, estimates **about 2 FPS** cost, and now wants the vanilla
cloud cards replaced. This supersedes the stage-1 stacking preference. Six, seven
and eight matching card draws occur in the first frames of the fog-on bursts,
respectively; indexed two-triangle quads, stride 24, exact known fog pair and
screen-blend/no-depth/no-stencil state. Replacement design is in the owning note.
The Hole and Atreus' Clouds supply the requested 16-card-family captures, but the
engine-record chain remains unflown. Do not confuse observed on-screen draw count
with the record's total dust count.

No fog timing mode was enabled. F8 readbacks dominate captured-frame durations,
so they cannot measure the live cost; the 2 FPS report is not converted to ms.
At frame 65785 the pass is idle (`sector`, weight 0, cards 0), but no explicit
sector-transition marker proves gate association or the fade-out duration.
Loading and clear-sector consistency remain for the sector diagnostic flight.

Local evidence/reproduction: `verification/results/run48b-triage/`; the contact
sheet uses presented BGRA colour converted to RGB before resizing (alpha is not
an opacity mask). The separately reported first-view stutters are outside the
capture windows; see [engine frame time](../architecture/engine-frame-time.md).


## Card replacement source qualification (2026-09-20)

Opt-in `--volumetric-fog-cards replace` masks only RT0 colour writes on strictly
validated native fog cards, calls the original draw once and restores the exact
mask. `keep` remains the default. Source observation continues while masked;
one successful stacked warm-up precedes replacement. F9 off restores vanilla
immediately; a failed/skipped medium after suppression faults replacement and
medium off until Reset/restart. One failed frame can lack both layers. The
per-frame `refused` field is a boolean, not a draw count.

Owner command (bottle X3, WineArch arm64, FEX_X87REDUCEDPRECISION=1,
WINEMSYNC=1):
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_fog_pass.py --cards-only`.
Final run passed **23 checks**, exited naturally with code 0, and completed
explicit device/factory release and window destruction. Fixture SHA-256:
`1864f2141454387d8438b0461ef3ca681a7e317cbb386c91fb83608351a45e2e`.
Evidence remains local in `verification/results/bottle-X3/fog-card-mask-gpu1.*`.
The failed-draw witness returns `D3DERR_INVALIDCALL` with no index buffer on both
native/masked paths, restores mask 7 and identical state, and makes two mask
setter calls. An earlier topology-zero assumption was invalid on this backend.
An earlier PASS22 process hung after its assertions and required SIGTERM;
its wrapper returned zero despite intervention, so that run is excluded from
clean-exit acceptance. The final fixture explicitly destroys its window.

Scratch full proxy build passed with SSE2 and four-byte incoming-stack flags;
`check_no_x87.py` passed 89 roots / 565 reachable functions / 0 violations.
Focused owner checks initially passed 22 tests with one missing-corpus skip.
The subsequent full host suite (2,177 tests, 452.290 s) exposed test doubles
missing the new fog/sector fields and incomplete local shader inputs; it did
not qualify a candidate. Six affected test/mock files were repaired, with
40 focused tests passing (12.131 s): material fixture 20,491 checks and capture
lifetime fixture 206 checks. Full host acceptance remains required before a
candidate, after remaining input issues are resolved.

Limits: the GPU fixture exercises the production colour-mask helper and
FogPass readiness/Reset, not the complete live proxy hook chain or captured
shader pair. Host tests extract the production routing/finalization bodies.
Per-card latency is unmeasured; two added state calls per suppressed card are
verified. Native Windows and game-flight replacement quality remain unverified.


**Performance qualification reopened before install:** the implementation forces
all state hooks in replacement mode, whereas run147/run181 used the hybrid
unhooked setter path. The two mask calls per card therefore do not bound its
whole-frame overhead. The design is being reconsidered to validate only matching
card draws through documented getters, retaining the production setter path.
The source checkpoint is not a flight-ready or performance-qualified candidate.


### Card-only state validation revision (2026-09-20)

The performance concern above is resolved in source: replacement no longer
forces global SetRenderState/SetSamplerState hooks and removes its slot-102
hook. Strict shader/declaration/shape/query/recording guards precede a fresh
native GetStreamSourceFreq and checked state reads; getter failure refuses
suppression. Unhooked per-draw invalidation prevents stale values, while existing
hooked mode retains its state-block resynchronization. Frequency is never cached.

Independent deep review accepted the revision. Focused host tests: 36 passed
in 8.772 s, then three expanded card tests passed in 1.878 s. Actual production
cache methods cover all 12 render-state getter failures, frequency failure and
instancing, consecutive changed states, hooked/unhooked parity and refusal before
reads. Material fixture: 20,495 checks, including 14 cache/state-block/Reset
checks. Maximum six/eight-card work is 90/120 calls: 12 state reads, one frequency
read and two mask writes per card. Noncards issue no fog-specific getters.
Both production translation units cross-compile with the required SSE2/stack
flags; seven changed methods / 11 compiled bodies have zero x87 violations.
This object audit does not replace the pending linked-candidate audit.

Historical X3 timing-off benchmarks measured about 64 ns per render-state write
and 57 ns per sampler write from the removed hooks; a roughly 3 ms whole-frame
projection is not a measured saving for this revision. Getter latency and actual
flight cost remain unmeasured. The previously qualified native mask/FogPass
transaction is unchanged; complete live routing and native Windows remain open.


## Run49B: run185 visual rejection and reader validation (2026-09-20)

The user rejects the uniform wash, especially in heavy sectors: replace it with
spatial cloud patches, intensity variation and clear gaps. This is a visual
redesign requirement, separate from whether the source-card suppression and
read-only engine-record reader operate correctly. No automatic 0.01/0.05
family-strength policy is accepted by this flight.

Four 32-frame bursts, all fog **on** (g=0.3), are present:

| Frames | Strength | Background record | Dust instances | Raw near/far |
| --- | ---: | --- | ---: | --- |
| 1974–2005 | 0.01 | bluewell, index 2 | 8 | 18,000,000 / 18,500,000 |
| 9204–9235 | 0.005 | bluewell, index 2 | 8 | 18,000,000 / 18,500,000 |
| 21901–21932 | 0.05 | foggreenoutlands, index 14 | 16 | 3,000,000 / 3,500,000 |
| 26447–26478 | 0.05 | foggreenoutlands, index 14 | 16 | 3,000,000 / 3,500,000 |

The user does not recall the capture-sector order. Background family/index is
not a unique universe-sector name; do not label these Argon Prime, The Hole or
Atreus' Clouds without additional evidence. First and second bursts have distinct
sector pointers; the two green bursts share one. Every in-window sector row is
`ready`, row/camera valid, raw camera fade matches the record, and parent-sector
anchor matches. Across the session the reader reports 496 ready rows and 18
no-cockpit rows; no other status is reported. This supports the chain in the
observed sectors, not universal dynamic-policy or clear-sector transition
acceptance.

Startup requested strength 0.02 and `cards=replace`, timing off; 22 toggle and
22 strength-change events establish the captured settings above. No F8 burst
intersects an off interval, so there is no same-view captured off comparison.
Source-card counts per burst are 124/110/223/212 (ranges 3–5, 2–6, 5–10, 4–11
per frame). These are visible draws, not the record's allocated population.
Known shader pair, two-triangle shape and expected blend/zero depth-write states are
present; these rows do not link native `alpha13c` or provide stride.

First-burst replacement reports show up to five cards observed/suppressed,
zero refusal/fault, ready=1, warmup=0, applied=1. The 64-row card-report cap is
exhausted before later bursts, so those bursts do **not** prove suppression
health. No explicit replacement-fault event occurs anywhere. Absence of capped
per-frame reports does not mean fog was disabled. No GPU cost or native Windows
acceptance is inferred from F8 CPU timings.

Raw present-image inspection shows two blue-family views and two broadly green
views, consistent with the user's complaint; displayed color alone cannot
separate painted sky, vanilla cards and the replacement medium. The architecture
note records the offline spatial-density redesign. HDR and TAA captures already
contain fog, so preview artifacts must not pretend to reconstruct a clean source.

Local reproduction: `verification/results/run49b-fog/reproduce.py`, `result.json`
and `result.md`; original session `/tmp/x3-bottleX3-run185`. The streaming pass
read 20,837,778 lines / 1,262,076,084 bytes and validated its JSON. Representative
present sheet: `/tmp/x3-run185-present-sheet.png`. No game or Wine execution.


## User confirmation: clear sectors (2026-09-20)

The user confirms that volumetric fog cleared in sectors without fog in a
previous flight. **Clear-sector removal is visually accepted.** The exact
run/transition and fade duration were not identified in this confirmation;
retain the earlier limits on trace-based timing and sector-reader coverage,
but do not request another flight solely to establish visible clearing. This
does not accept the uniform fog appearance or close the spatial redesign.


## Spatial family volume: offline recipe accepted for GPU prototype (2026-09-20)

The bounded host experiments established one offline artistic family recipe for later fixture-only GPU prototyping. They did not implement or verify a game/GPU fog fix.

### Rejected support experiments

- The native-texture warped-support experiment stopped before camera rendering because the green normalization factor was `K=8.307928044`, above the fixed `K<=8` gate. Its result remains `REJECT_NORMALIZATION` in [`fog-warped-support/report.json`](/tmp/x3-fog-patchy-replay/verification/results/fog-warped-support/report.json).
- The first genuine 3D noise-bank experiment used one shared 12% support and extinction policy. It improved the bank/cavity shape but failed the four-view morphology gate: frame 1974 exceeded the opacity ceiling (`p99=0.141251`), while green frames 21901 and 26447 had only `2.3601%` and `1.2021%` dense sky. Its immutable result is [`fog-volume-support/report.json`](/tmp/x3-fog-patchy-replay/verification/results/fog-volume-support/report.json).

### Accepted offline family prior

The fixed follow-up retained the same periodic 32,768-unit, 128³ 3D noise/cavity field, seeds, palettes, 12,000-unit horizon and exact-zero cavities. It assigned bluewell 12% occupied volume with `sigma=0.010/4000`, and foggreenoutlands 24% with `sigma=0.025/4000`. Density was normalized independently to mean 1 inside occupied voxels; the old common gain and target-mean factors were not applied to this recipe. Blue support remained unchanged, green support contained blue, and the resulting whole-tile means were 0.12 and 0.24.

These are hand-authored artistic priors for two named families. The 24% green support is not derived from D16/D8, body counts, sector size, or any universal instance-count formula. FogNear/FogFar and the view-distance consumer remain separate object/card-distance behavior and do not scale this medium's support or extinction.

At 240×144 with 128-step, geometry-clipped 12,000-unit columns, the admissible sigma intervals were `[0, 4.7513938e-6]` for blue and `[4.8050719e-6, 1.1918821e-5]` for green; both fixed coefficients lie inside. The four representative results were:

| Frame | Family | Clear sky | Dense sky | p99 opacity | Largest clear component | Largest dense component |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1974 | bluewell | 52.2950% | 22.2469% | 0.053928 | 52.1606% | 15.1304% |
| 9204 | bluewell | 88.0706% | 0% | 0.013198 | 88.0673% | 0% |
| 21901 | foggreenoutlands | 53.0422% | 12.9667% | 0.053751 | 44.3565% | 8.7568% |
| 26447 | foggreenoutlands | 55.9672% | 9.7830% | 0.037165 | 54.9562% | 5.1042% |

All four fixed view gates passed; the two green views also passed their clear/dense component thresholds. Across the four 24-versus-128 static comparisons, the worst T error was p99 `0.000468232`, max `0.001246764`. Across the first eight frames of each window (32 pre-TAA frames total), the worst 24-versus-64 T error was p99 `0.000597944`, max `0.002156604`; the worst consecutive absolute-error-change p99 was `0.000435175`, and false opacity above 0.002 on finite-reference-empty samples was 0. Periodic/negative-wrap, exact-vacuum, camera round-trip, nesting, conditional-mean and periodic speckle checks passed.

The offline visual review accepted the family recipe for the next fixture-only GPU prototype. The images show coherent broad banks and clear gaps, with deliberately soft boundaries. This acceptance covers only the fixed host preview and assumed-light card-free sandbox; run185 present images already contain native fog. It does not establish in-game appearance, fog TAA/history, depth/transparent correctness, lighting or self-shadowing, D3D9 atlas precision, Reset behavior, performance, or native Windows/CrossOver execution.

Evidence and visual controls:

- [`family policy report`](/tmp/x3-fog-patchy-replay/verification/results/fog-family-volume-policy/report.json)
- [`density topology`](/tmp/x3-fog-patchy-replay/verification/results/fog-family-volume-policy/density-topology.png)
- [`run185 frame 1974`](/tmp/x3-fog-patchy-replay/verification/results/fog-family-volume-policy/run185-frame-1974-bluewell.png)
- [`run185 frame 9204`](/tmp/x3-fog-patchy-replay/verification/results/fog-family-volume-policy/run185-frame-9204-bluewell.png)
- [`run185 frame 21901`](/tmp/x3-fog-patchy-replay/verification/results/fog-family-volume-policy/run185-frame-21901-foggreenoutlands.png)
- [`run185 frame 26447`](/tmp/x3-fog-patchy-replay/verification/results/fog-family-volume-policy/run185-frame-26447-foggreenoutlands.png)
- [`old card-free run153 sandbox`](/tmp/x3-fog-patchy-replay/verification/results/fog-family-volume-policy/run153-frame7741-sandbox.png)
- [`fixed experiment helper`](/tmp/x3-fog-patchy-replay/tools/analysis/fog_family_volume_policy.py)


### Spatial fog first GPU march checkpoint (2026-09-20)

The standalone D3D9 fixture passes 22 checks and five march transactions, with
all 16 sky/geometry/boundary/whole-frame metric groups passing across four
original captures. Maximum transmittance error against the matching float32
reference is 0.00048828125; maximum scattering-channel error is 0.0000152587890625.
Atlas centres match exactly; maximum filtering error is 0.001953125, within its
gate (four binary16 steps at one witness, not one ULP). X3/arm64 command time
is 4.795 s; this includes fixture work and is not rendering performance.
Independent deep source/evidence review passed. A reserved HLSL identifier was
renamed after compiler rejection; arithmetic and thresholds were unchanged.

The [compact record](../../verification/results/fog-volume-gpu-march.json) binds
source/compiler/shader/input hashes. This is march-only qualification: composite,
synthetic clipping/ordering, full state/Reset/fault coverage, 32-frame checks and
complete-transaction timing remain pending. No production fog change or native
Windows execution is claimed.

### Spatial fog composite checkpoint (2026-09-20)

The standalone GPU composite now passes nine captured/synthetic cases and
74 fixture checks, with unchanged numerical tolerances and all eight synthetic
clipping, colour-order and thin-surface contracts passing. Four actual captures
preserve all **1,649,517 pixels whose compatible GPU half-resolution inputs are
exactly empty**, plus 86 separately reported CPU-float32-empty repair pixels.
Fourteen focused host tests and independent deep source/evidence review pass.
The [compact composite record](../../verification/results/fog-volume-gpu-composite.json)
binds the executable, shaders, runtime source and revised checker separately.

The first composite exposed two shader defects: interpolated UV rounding could
select the previous half-resolution footprint and bypass full-pixel repair;
weighted transmittance could drift below one on an empty field. Canonical integer
pixel coordinates and weighted opacity fix those defects. The next run's remaining
identity rejection came from classifying tiny nonzero fog as empty after CPU
FP16 rounding. The identity assertion now uses the compositor's actual GPU
half-resolution input footprint. The old CPU-rounded-empty changed counts
(5,295 / 1,843 / 2,013 / 2,896) remain reported; numerical ST/composite gates and
synthetic zero checks are unchanged. Adversarial tests reject a one-bit change
on actual empty input and independent numerical failures. Reanalysis reused the
immutable readbacks without another Wine run; both failed reports are retained.

The observed CPU-float32-empty to GPU-nonempty counts are zero diagnostics, not
an additional acceptance gate. GPU full-float repair ST is not directly read
back, so those repairs retain the stated evidence limit. The 5.245-second runtime
and 6.467-second host reanalysis are fixture durations, not rendering performance.
Full state/Reset/fault checks, 32-frame coverage and transaction timing remain
pending. No production fog change, clean replacement preview, game visual
acceptance or native Windows execution is established by this checkpoint.

### Spatial fog state and recovery checkpoint (2026-09-20)

The standalone fixture passes **101 state/recovery checks** and its slab-scene
march/composite parity checks. Hostile render state, auxiliary target/depth bytes,
16 warmed output/reference checks, query/recording/MSAA/foreign-resource refusals,
partial allocation and injected operation/restore failures pass. A held DEFAULT
resource causes a real Reset failure; releasing it permits retry, re-upload and
byte-identical output. Lost-device HRESULT paths are injected; an actual device
loss was not observed. Twenty host tests and independent source/runtime review
cover this checkpoint. The X3 command took 6.521143 seconds, not a performance
measurement. The [compact recovery record](../../verification/results/fog-volume-gpu-state-recovery.json)
binds sources, executable, shaders, results and three retained failed attempts.

The first full transaction lost all 16 stream offsets despite its state-block
restore. Explicit public stream buffer/offset/stride/frequency capture and restore
now preserve them, with fixed stack storage and balanced temporary references on
partial capture and loss. This adds 64 getter/setter calls and up to 16 Releases
at 16 streams; the observed transaction has 254 instrumented device calls (excluding COM
releases and resource-validation calls). Isolated
fresh and recaptured ALL-state-block probes both restored all offsets, so this
is not evidence that state blocks universally omit stream offsets.

A subsequent fixture failure revealed that repeated SYSTEMMEM-to-DEFAULT uploads
were not re-marking the source dirty. AddDirtyRect before each refill fixes that
fixture defect; exact source bytes before each fault and baseline output on every
warm iteration now pass. The earlier compound failure did not identify which
predicate failed, so that retained report alone cannot prove its sole cause.
The corrected run records every fault predicate and injection count.

The 32-frame inputs and full-transaction performance remain open, including the
added stream-preservation cost. No production fog change, clean replacement
preview, game visual acceptance or native Windows execution is claimed.

### Spatial fog 32-frame GPU sequence checkpoint (2026-09-20)

The frozen recovery executable passes the first eight frames from each of four
run185 capture windows at 120x72, plus two synthetic periodic seam controls.
Including the atlas witness, **35 cases / 282 fixture checks** pass; all 34
march/composite comparisons pass independent recomputation. Maximum T error is
0.00048828125, maximum scattering-channel error 0.0000076294, and worst composite
relative RGB p99/max is 0.00306892 / 0.00387898. Alpha is bit-identical. All
109,760 actual-half-input-empty pixels and 111 CPU-float32-empty repair pixels
remain unchanged; the 32 captured frames use 322 repair pixels. Both GPU seam
controls are nonempty and varying.

Input review corrected three defects before execution: depth point sampling was
misaligned with the reduced D3D integer-centre rays; the initial seam controls
traversed only empty fog; convergence had been marked passed without measurement.
The corrected point-resampling error is at most two-thirds of a source pixel.
Both controls now cross nonzero varying density, and all 34 measured 24-versus-128
step comparisons pass (worst p99/max T 0.000614152 / 0.001929462). Inconsistent or
nonfinite projection metadata is refused. The superseded inputs remain local.
Twenty host tests pass, and a deterministic twin reproduces all 238 case assets.

The [compact sequence record](../../verification/results/fog-volume-gpu-sequence.json)
binds the independent preparer and frozen runtime sources, inputs, shaders and
results. The X3 command took 6.435954 seconds; this is not rendering performance.
These are reduced spatial/arithmetic checks on already-fogged run185 images,
not exact native-resolution ray identity, a clean replacement preview, history/
TAA acceptance or native Windows qualification. Full-float GPU repair ST remains
unread; its CPU-empty identity control is separate. Full-transaction timing is
the next detached fixture check.

### Spatial fog complete-transaction timing checkpoint (2026-09-20)

The detached fixture passes the fixed performance gates with the qualified pass
and shaders unchanged. Each profile runs 16 warm and 64 measured iterations,
round-robin across four views. All 160 rows and 26 correctness/lifetime checks
pass. Independent review recomputes the statistics directly from raw QPC ticks:

| Profile | Submit median | EVENT-completed median | Completed p95 |
| --- | ---: | ---: | ---: |
| 1280x768 captures | 0.1336 ms | 1.1002 ms | 1.584285 ms |
| 1920x1080 resized performance inputs | 0.1330 ms | 1.22575 ms | 1.501395 ms |

The fixed gates are submit median <=0.25 ms for each profile, completed median
<=1.25/2.0 ms respectively, and completed p95 <=2.5 ms at 1920. The 1280 p95 is
reported but was not gated. No samples were dropped and no quality parameters
were reduced. The interval includes validation, state preservation, scene copy,
march, full-pixel repairs, composite, restoration and reference cleanup, including
the added 64 stream calls. Its 254 instrumented-call count excludes validation
calls/Releases; those operations remain inside the measured elapsed interval.

A completed pre-fence excludes uploads, pristine-scene reset and earlier work.
The measured completion includes EVENT issue/poll overhead; CPU submit is wall
time, not thread CPU use. No active disjoint query was introduced and GPU
timestamps were not collected. Two resident family atlases occupy 35,692,800
bytes per profile. All 16 baseline/final readback pairs match, eight 1280 outputs
match earlier accepted readbacks, and allocation/reference checks remain stable.

Seven host tests and independent source/runtime review pass. The
[compact timing record](../../verification/results/fog-volume-gpu-timing.json)
binds source, inputs, executable, shaders and full measurements. The command's
5.934438-second duration is fixture runtime. These measurements do not establish
game FPS, native Windows performance or production integration; the separately
qualified 32-frame sequence retains its existing limits. The next work is the
production scene/sector/resource boundary, including loading cost.


### Production field assets and decoder checkpoint (2026-09-20)

The production build now regenerates both qualified procedural fields without
raw game assets, capture files or verification imports. NumPy 2.0.2 is checked
at CMake configuration using the explicitly selected Python interpreter; missing
or mismatched dependencies fail with instructions, without installing packages
or changing the interpreter. The README documents the build prerequisite.

Both decoded hashes exactly match the accepted bluewell/green fields. The sparse
zero/literal resources total 6,769,480 bytes including headers, embedded as RCDATA;
generated files remain untracked. The portable decoder checks independent expected
metadata, every run/bound, finite nonnegative half values, exact length and checksum,
and leaves empty output on failure. The final six focused tests cover deterministic
rebake, two successful decodes, eleven corruptions per profile, allocation failure,
atomic output, i686/windres compilation and configure success/failure. The preceding
selected fog/sector/camera suite passed 33 tests. Independent source/evidence review
cleared both the asset implementation and build usability follow-up.

The root integrated CMake build compiles the decoder and resource and links the
DLL successfully; its generated manifest exactly matches the reviewed twin builds.
The [compact asset record](../../verification/results/fog-field-assets.json) binds
source, generated hashes and the uninstalled integration DLL. Host-only decode
times were 22.52/18.12 ms; these exclude Win32 resource lookup, GPU upload, Wine
and first-use paging, so they are not a game loading bound. Actual resource
execution, renderer integration, loading/Reset cost and flight appearance remain
pending. This checkpoint does not activate spatial fog.


### Spatial production renderer qualification (2026-09-20)

The actual production FogPass now runs against embedded field resources and
public D3D9 in the detached X3 fixture. Independent review clears 113 state and
recovery checks, four-view numerical testing (72 checks, 32 variants, 64
readbacks), and the captured 32-frame sequence (576 checks, 256 variants, 512
readbacks). The sequence uses reduced 120x72 views, not a full-resolution flight.
The state witness covers borrowed/open and closed scenes, CPU preservation,
explicit stream restoration, failure injection and a real Reset blocker/retry.
Injected device-loss results do not establish naturally occurring device loss.

Both profiles pass the fixed complete-transaction timing gates, with 16 warmup
and 64 balanced measurements per resolution. At 1280x768 the CPU median is
0.1323 ms and EVENT-completed median/p95 are 1.0840/1.1731 ms. At 1920x1080 they
are 0.1332 ms and 1.9808/2.0934 ms; the median has only 0.0192 ms margin to its
2 ms gate. All 28 correctness checks and baseline/final pixels pass. These are
whole-transaction wall times including completion polling, not GPU timestamps
or game FPS. The 1920x1080 workload uses nearest-resized captured inputs,
not native 1080p captures. The [compact production record](../../verification/results/fog-spatial-production-2026-09-20.json)
binds each executable, report, command and bottle environment.

The streaming checksum decoder, independently cleared by `review_fog_decode`,
preserves exact atlas output while
removing its second full-buffer scan. Host median decode time falls from
18.917/18.166 ms to 3.321/4.345 ms across 20 fresh-process samples per profile.
The optimized actual-pass rerun passes all 113 state checks. Its single first
resource-decode/upload observation is 18.3519 ms versus the earlier 53.2362 ms;
these separate-run observations are not a statistical loading benchmark.

The [production fog-through-TAA replay](../../verification/results/fog-temporal-replay/report.md)
was independently cleared by `review_fog_temporal` and uses the existing
resolve and captured translation/motion/depth/jitter. All
210,672 reduced crop samples remain finite. Incremental fog change has mean
0.1184 and p99 1.2041 display-relative luma codes. This is a derived numerical
witness, not a quality pass: the captured scene already contains fog, and reduced
sampling cannot establish trailing banks, thin foreground or disocclusion quality.
A translated in-game post-fog TAA/current comparison remains necessary.

The separate production-fragment/card bridge passes 152 checks in 8.612 s,
independently cleared by `review_lattice_gpu`. It exercises real native card
calls and FogPass against synthetic owner/sector/camera/shader-identity inputs.
Five refusal cases preserve final pixels with no fog transaction; actual scene
reopen failure reaches proxy poisoning, survives F9 and recovers through Reset
rewarm. This does not exercise engine hooks, selector/full dispatch or GPU TAA
history; real native Reset is covered by the separate 113-check state witness.
The full host suite passes 2,414 tests in 694.921 s; the bridge checker adds
three focused passes after its module was integrated following discovery. The
clean committed candidate build, linked CPU audit and two actual-DLL HDR/TAA
smoke cases also pass; [status](../status.md) records the current flight candidate.
Native Windows runtime and user visual acceptance remain open.

### Spatial directional shaft implementation and host witness (2026-09-20)

The next-flight shaft source now multiplies only spatial in-scattering by
same-frame replay visibility. Current maps remain usable when surface-shadow
application is refused. Individual unavailable maps fall back independently;
previous-frame far maps are rejected. Constant world/texel bias and manual
2×2 comparison filtering replace the analytic fog's surface-clamp fallback.
The owning architecture records coverage, coordinate, lifetime and cost rules.

The selected host command passes **33 tests** in **1.954 s**:
`PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fog_shadows verification.analysis.test_fog_spatial_reference verification.analysis.test_volumetric_fog verification.analysis.test_fog_route_bridge`.
Seven new tests cover D3D9 texel convention, a thin occluder under two view
transforms, continuous cascade XY boundaries/coarser fallback, unavailable maps,
bias, unchanged transmission/empty identity, and native frame/row validation.
The actual FogPass and extended fixture separately cross-compile with i686
SSE2, four-byte incoming stack alignment and `-Wall -Wextra -Werror`.

Root-owned shader generation passes; march/composite use **315/506 ps_3_0 slots**
(1397/2162 words). The initial composite was 515 slots; removing a redundant
already-guarded uniform branch brought it below the required 512-slot limit.
The fixture now requires **132** named checks, adding actual dark/lit R32F
comparisons in each slot, stale/invalid map fallback, S-only changes, unchanged
T/empty pixels, CPU preservation, fractional PCF, blending into lit/dark coarser maps and no
retained map references. The numeric fixture adds two full-pixel repair
variants with dark/all-lit maps: **40 variants / 88 checks**, including a
zero-incident-radiance CPU reference and exact all-lit/unshadowed comparison.
Hostile state
now includes samplers 4–6 and all 22 modified pixel constants. These new runtime
checks are prepared, not yet execution evidence in this checkpoint.

`fog_shadow_replay.py --step 2` against run185 frames 1974, 9204, 21901 and
26447 and the frozen accepted atlases passes **983,040** actual half-grid rays
in **15.800 s**. Transmission and unavailable-map output are bit-identical;
**410,638** empty rays remain exact identity. Only **four rays**, all in frame
26447, have cloud/shadow overlap; the largest scattering reduction is
**0.0009555351**. The other three views have zero reduction. This limits any
appearance claim: the geometric shadow must overlap occupied cloud, and the
implementation does not add density or paint beams to manufacture that overlap.
Raw detailed host evidence stays at `/tmp/x3-spatial-fog-shafts-offline-half.json`;
the [compact host record](../../verification/results/fog-spatial-shafts-host.json)
records these observations and scoped checks.

Actual new-shader GPU behavior, full-pixel shadow repair, state/Reset recovery,
whole-transaction timing, native Windows runtime and flight visual acceptance
remain open. Existing unshadowed runtime evidence is not reused as proof of the
new shadow sampling. The installed Run53 candidate is unchanged.


### Spatial directional shafts actual GPU checkpoint (2026-09-20)

Root-owned locked execution in bottle **X3**, CrossOver Preview, WineArch
**arm64**, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`, passes the new production
shader/renderer state fixture: **132 checks** in **8.894 s**. This includes
actual per-slot dark/lit comparisons, fractional PCF, coarser-cascade blending,
exact unchanged transmission, invalid/stale-map fallback, hostile sampler and
constant restoration, CPU/LastError preservation, borrowed map references,
injected loss and the existing real Reset blocker/retry. The executable hash is
`56002b6d21af12684286b8042d020ccce9c31c819f47e509b0dfe960879c8a85`.

The same frozen executable passes four-view numerical qualification in
**8.597 s**: **40 variants**, **88 raw checks**, **80 readback hashes**, all
finite with exact source alpha. Full-resolution repair covers **737,280 pixels
per view** in the controlled invalid-half-depth variants. All-lit shadow repair
(v9) exactly equals unshadowed repair (v7); fully occluded repair (v8) differs
on those repaired pixels and passes the zero-incident-radiance CPU reference
with unchanged extinction. These runs use the original bluewell/green assets;
they do not qualify the separate all-family expansion.

Independent deep source/state/numerical review clears this checkpoint. The
[compact record](../../verification/results/fog-spatial-shafts-host.json) now
binds both report hashes, commands, executable and bottle provenance. Detailed
reports remain `/tmp/x3-spatial-fog-shafts-gpu-v2/state/report.json` and
`/tmp/x3-spatial-fog-shafts-gpu-v2/numeric/report.json`. Root's protected-file
checks confirm the EXE, bottle configuration and installed DLL are unchanged.

Whole-transaction shaft cost is still unmeasured; a paired timing extension
must use the same shaders/resources and toggle map validity. The captured
four-ray cloud/shadow overlap finding above still limits appearance claims.
Native Windows execution, naturally occurring device loss, temporal flight
appearance and acceptance remain open. No game launch or candidate installation
is part of this checkpoint.

### Asset-backed family expansion host checkpoint (2026-09-20)

The isolated branch implementation covers **14 asset-backed positive-card
families**: all 11 mapped families across 35 shipped sectors, plus unused
fogblue, fogkhaak and khaakhive. Additional unused fogred, foggreenoutlands and
foggreeneye records use the same named profiles. The [stock census](../reverse-engineering/sector-fog-census.md)
retains the asset boundary: xtmgreenring has no dust bodies, and earth's diffuse
reference is unresolved. These two families, unknown names and invalid samples
retain native fallback; D=0 stays clear in ordinary mode. Explicit debug forcing
keeps its pre-existing override semantics.

Original bluewell/green decoded atlases and packets compare byte-identical to
the prior qualified assets. Twelve additions use occupancy 0.12 / sigma 2.5e-6
as a provisional artistic baseline, independent of card count, body size and
FogNear/FogFar. Their four-stop colour arrays derive from winning native DXT1
textures: linear-sRGB conversion, nonzero Rec.709-luminance sorting, means in
25–40 / 40–55 / 55–70 / 70–85 percentile bands, then per-stop peak normalization.
Texture member names and decoded hashes are retained in the recipe; local
palette evidence is `/tmp/x3-fog-family-palettes.json` and its companion `.md`.
This preserves colour references, not the native spatial arrangement or density.

The [compact result](../../verification/results/fog-all-families-2026-09-20.json)
records **28 affected host tests passing in 54.555 s**: two byte-identical bakes,
14 successful decoder/full-checksum cases, 154 corruption cases and allocation
failure handling, every family selection and card warmup/suppression path,
clear/invalid/unknown fallback, census inventory coverage, and i686 decoder plus
14-resource compilation. The bridge/timing Python checks validate their evidence
parsers; they are not new GPU bridge or timing runs. The retained final bake takes
19.581 s and packages **34,142,200 bytes**, with all decoded hashes pinned.
Generated RC entries use `.rc` because windres treats `.h` includes as headers
and would otherwise report no resources. C/C++ fragments retain `_inc.h` names.

No shader, FogPass transaction or hook changes belong to this expansion. Selection
adds a bounded, allocation-free scan of at most 14 names at the first BeginScene;
there is no added per-draw work. One active 17,846,400-byte CPU atlas and one
DEFAULT GPU atlas remain, with the existing generation/warmup, failure and Reset
rules. Source inspection reuses those unchanged invariants; it does not establish
actual GPU execution for all 14 fields. Native Windows execution, new-family
appearance, first-use/switch loading and combined candidate integration remain
open. Independent source, evidence and documentation review cleared this
checkpoint. No Wine, game,
DLL build or installation occurred; [status](../status.md) remains the sole
installed-build description.


### Asset-backed families actual GPU checkpoint (2026-09-20)

The root-owned detached actual FogPass run passes **145 checks and 18 renders**
with all 14 embedded family resources. The [compact family record](../../verification/results/fog-all-families-2026-09-20.json)
binds report, executable/build, immutable inputs, checker and execution evidence.
Environment: bottle **X3**, CrossOver Preview, WineArch **arm64**,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`. The child exits 0 in **10.533 s**;
this is whole-fixture elapsed time, not GPU cost or a loading benchmark.

A single fixed 64×48 synthetic sky view at world origin zero exercises each
profile at its authored sigma and unity multiplier, without per-family camera
search. Actual readbacks contain **736–768 nonempty ST pixels** and
**2,954–3,072 changed RGB pixels** per render; all retain exact source alpha and
finite values. Against the CPU float32 24-step trilinear half-atlas reference,
worst transmission p99/max error is **0.00048828125 / 0.00048828125**; scattering
p99/max is **0.000030517578125 / 0.000030517578125**; relative composite RGB
p99/max is **0.00097087381 / 0.00129032263**. Every case passes the existing
numerical gates, so an empty-field identity result cannot satisfy this witness.

The sequence selects IDs 1–14, revisits 1 / 2 / 14, and renders retained profile
14 after a real successful Reset. Both revisits and Reset produce byte-exact
ST and composite readbacks. Switches publish increasing generations with one
upload each; stale frames and invalid IDs refuse before device writes. Warm
reuse performs no upload, and observed ownership stays at one 17,846,400-byte
CPU atlas and the pass's ten references. Reset drops DEFAULT ownership to four
references, retains that CPU buffer and reuploads it once; detach releases all.
The fixture also checks hostile-state and CPU/LastError preservation on ordinary
prepare/execute paths. Existing broader failure/Reset evidence remains separate.

This checkpoint uses the **unshadowed** shader in a separate executable. It does
not test shafts combined with all families, nor all-family geometry boundaries,
full-pixel repair, game appearance, native Windows execution or loading latency.
No game or installed build changed. Independent runtime review cleared all
145 checks and reproduced the 18-case report from raw readbacks; the original
host checkpoint above remains historical evidence.


### Fourteen-family and shaft integration GPU checkpoint (2026-09-20)

The isolated combined source (`bcf1021a`) passes **145 checks / 18 executions**
in X3 (arm64 Wine, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), **8.128 s**
fixture elapsed. All fourteen profiles render through the combined shaders;
family revisits and Reset restoration remain exact. Independent deep review
revalidated 31 build inputs and 50 prepared inputs and checked raw readbacks.
The unchanged shaft source and original asset bytes retain the prior 132 state
checks and 40 numerical cases; this combined harness exercises unshadowed
fallback on a synthetic sky view, not a new shadow/geometry matrix.
The [compact record](../../verification/results/fog-families-shafts-integration-2026-09-20.json)
binds execution and evidence. Full host qualification and shaft transaction cost
remain pending; native Windows and flight appearance are unverified. No install.

The combined-source full host discovery passed **2,431 tests** (two skipped)
in **753.309 s**, exit 0; the compact record binds the full command and local log.
Shaft transaction timing remains pending and is run after host load ends.

### Spatial directional shafts paired transaction timing (2026-09-20)

Root-owned locked execution and independent runtime review pass the frozen
paired timing fixture in bottle **X3**, CrossOver Preview, WineArch **arm64**,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`. Both arms use identical shaders,
family fields, views and resident captured cascade maps; only the three map
validity flags change. Consecutive A/B and B/A pairs balance order. No slow
samples were discarded. There are **400 transactions**: 80 warm and 320 measured,
with 320 total common-workload transactions and 80 separately labelled repair
stress transactions. All **63 checks**, **40 baseline/final readback pairs**
(80 hashes) and **10 accepted-output matches** pass. Per-arm output stability,
finite output, exact alpha, A/B transmission/empty identity and warmed
allocation/reference stability hold.

| Common captured workload | 1280×768 | 1920×1080 |
| --- | ---: | ---: |
| Off completion median / p95 (ms) | 0.69995 / 0.75594 | 0.89795 / 1.22361 |
| On completion median / p95 (ms) | 0.81870 / 0.86627 | 0.97460 / 1.34958 |
| Paired on-minus-off completion median / p95 (ms) | 0.11310 / 0.15556 | 0.11890 / 0.14847 |
| On CPU-submit median (ms) | 0.12960 | 0.12810 |

Both common arms pass the unchanged absolute gates: CPU-submit median ≤0.25 ms,
completion median ≤1.25/2.0 ms at 1280/1920, and 1920 completion p95 ≤2.5 ms.
The paired delta is the median of within-pair differences, not the difference
of arm medians. Both arms include the new fixed sampler-state overhead;
absolute gates therefore remain necessary. Each arm/resolution has 64 measured
common samples. Captured maps mostly return lit at occupied cloud samples,
so this measures their ordinary projection/comparison cost; the earlier four-ray
cloud/shadow overlap finding still limits visual claims.

Repair stress forces full-pixel marching at **737,280 / 1,555,200 pixels** for
1280/1920 and has 16 measured pairs per resolution. On completion median/p95 is
**1.6014/1.8258 ms** at 1280 and **1.7492/3.1711 ms** at 1920. Paired incremental
medians are **0.1464/0.15325 ms**. The **3.1711 ms** 1080p stress p95 exceeds the
common workload's 2.5 ms threshold; this deliberately extreme separate workload
is neither mixed into the common aggregate nor claimed to pass its gate.

Queries remain idle during `execute`. QPC measures complete validation,
state capture, scene suspension/copy/reopen, march, full-pixel repair, composite,
restoration and transient reference cleanup; an END-only EVENT/GetData completion
follows before the completion clock. Pristine-scene resets, prefences, caller
Begin/EndScene, witness copies and readbacks remain outside the measured interval.
These are **EVENT-completed wall times including polling**, not pure GPU timings
or game FPS. The 1920 inputs are nearest-resized captured workloads. Values from
older independent runs do not establish an optimization beyond this paired result.

Cold preparation remains separate. Reading, validating, allocating and uploading
12 fixture-only captured maps (201,326,592 bytes) takes **171.0600 ms submit /
196.6735 ms completion**; production borrows already-created replay maps and does
not incur that fixture setup. Two-family field decode/allocation/upload submit
observations are **12.1213/17.9008 ms** at 1280 and **12.9215/16.2690 ms** at 1920;
target submission is **0.4826/0.0338 ms** and **0.0138/0.0120 ms**, respectively.
Full per-resolution residency setup completes in **155.5606/271.7332 ms**.
These are individual fixture observations, not a loading-time distribution.

The [compact record](../../verification/results/fog-spatial-shafts-host.json)
binds the detailed report, commands, build/input hashes, bottle and lock timing.
Executable SHA-256:
`838444d24227d133391d0d9d2f7c37f300ee50a5ecc57caedb77dc3f2ae1980e`.
Detailed output is `/tmp/x3-spatial-fog-shafts-timing-v1-results/report.json`;
fixture execution is **9.26156 s**, lock wait **0.000003417 s**, wrapper child
elapsed **10.58533 s**, exit zero. Nineteen affected host tests pass. Timing
covers the original two profiles; separate all-family correctness does not
establish all-family performance. Native Windows execution and flight appearance
remain open. No installed-build change or game launch is part of this checkpoint.

Combined timing-tool integration preserves the fourteen-resource inventory and
adds the included family fixture header to the build-input hashes, as requested
by review. The 19 affected timing host tests pass in 0.136 s. Production shaders
and renderer are unchanged; the existing paired measurements remain applicable.

The combined production DLL is retained uninstalled after one clean
RelWithDebInfo build: 54,352,248 bytes. Linked audit passes 95 roots / 540
reachable functions / zero violations; selected actual-DLL HDR ownership and
TAA smoke cases pass 43/83 checks. The smoke report remains explicitly PARTIAL
for its selected scope. The [DLL record](../../verification/results/fog-families-shafts-dll-2026-09-20.json)
binds source, artifact and evidence. No game launch or install was performed.


### Run53B camera-cut replacement warmup correction (2026-09-20)

Run194's final F8 burst, frames **31481–31512** in The Hole, keeps the same
foggreenoutlands authority (profile 2, sector `66328b20`, index 14, dust 16),
with strength **0.03 / density multiplier 1.5**. The [compact triage witness](../../verification/results/run53b-triage/compact-repro.json)
records cuts on **31495–31505**, without a fog toggle, sector change, fault or
refusal in the burst. Source cleared replacement readiness after every cut,
including successful spatial passes. That necessarily reentered stacked warmup
on **31496–31506**. The cut itself does not invalidate current camera/depth or
change the current-frame spatial field's resources; it remains a TAA history
invalidation. The fix removes only this post-completion cut-driven disarm.

The affected host test now compiles the **actual `run_volumetric_fog` method**,
its existing card methods, and the exact fog policy reset statements extracted
from `before_reset`. The 32-frame sequence uses six synthetic cards per frame
and the captured 11-cut pattern. Before the fix it fails with **11 warmups,
126/192 suppressed cards and 32 successful volume applications**. After the fix
it passes with **zero warmups, 192/192 suppressed and 32 applications**; duplicate
scene-end calls remain no-ops, masks restore and transient mock references
return to baseline. These counts are host routing witnesses, not the capture's
card counts or actual pixel writes.

Eight recovery scenarios cover same-family sector change, profile change,
device generation change, Reset policy, off/on toggle, failed warmup retry,
post-suppression execute failure and late prerequisite loss. Genuine readiness
changes still warm up once; late loss/failure still faults until Reset. Existing
mutable-guard, mask rollback, scene-loss, field-generation and native-call-count
checks remain included. The exact Reset policy extraction does not execute a
native Reset or test DEFAULT-resource release.

Command: `PYTHONPATH=verification/probe python3 -m unittest -v verification.analysis.test_fog_cards verification.analysis.test_fog_sector_policy verification.analysis.test_fog_route_bridge`.
**8 tests pass**; retained local logs are
`/tmp/x3-fog-cut-warmup-checks/fixed.log` and `prefix.log`. The latter reintroduces
only the removed line into the in-memory test source and exits 1 with the
pre-fix witness; production files are not changed by that check.
`i686-w64-mingw32-g++ -std=gnu++17 -O2 -g -DWIN32_LEAN_AND_MEAN -DNOMINMAX -Wall -Wextra -Wno-cast-function-type -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -c src/proxy/motion_output.cpp -o /tmp/x3-fog-cut-warmup-motion_output.o`
passes. This establishes x86 compilation, not native Windows execution.

No shader, D3D transaction, resource lifetime, CPU/LastError preservation
boundary, hook instruction or rollback path changes. Per-draw work is unchanged;
scene end removes one conditional store, without new allocation, locking or
validation. The existing spatial/shaft state, numerical, Reset and timing
checkpoints above remain relevant to those unchanged implementations. New actual
GPU execution, native Windows behavior and user flight appearance remain open;
this host checkpoint alone does not establish elimination of visible flicker.
No Wine, full DLL build, install, commit or game launch belongs to this change.

### Run197 first-person camera precision correction (2026-09-21)

User-labeled first-person F8 frames **2633–2664** in
`/tmp/x3-bottleX3-run197/session-20260921-000016-212.log` have valid cameras and
**zero cuts in all 32 frames**, inside a continuous `card_refused` interval
2562–2742. The frame2660 report has observed2, suppressed0, refused1, ready0,
warmup0, applied0 and fault0. Engine bluewell sector authority remains unchanged.
The fog world's `1e-4` Gram tolerance rejects all 32 captured rotations. Each
rotation reconstructed on the documented `/65536` grid matches actual captured
`object_matrix role=view` float bits exactly. Across the session, the unchanged
actual C++ camera parser accepts all **3,080** valid camera samples; the old fog
helper accepts **1,082** and refuses **1,998**. Every logged application/refusal
agrees with this check: 92 `ok`, 94 `card_refused`, one initial `world_basis`.

After aligning the helper with the upstream `1e-3` near-rigid tolerance, the
actual C++ helper accepts **3,080/3,080**, including **32/32** F8 views. Maximum
measured Gram error is `0.0001526300329715`; determinant range is
`0.999796784330055..1.000023415016184`. The unchanged true inverse has maximum
uploaded-float roundtrip error `5.94e-8`. Remaining noncapture camera samples
are reconstructed from seven-digit camera logs on the fixed-point grid; only
the 32 F8 rotations have the independent raw-bit cross-check.

The existing `verification/probe/fog_spatial_math_fixture.cpp` now exercises
exact first-person and worst captured rotations, translated camera origin,
inverse roundtrip, both sides of the admission boundary, scale/shear/singular
and independent determinant refusal, and nonfinite sun; earlier nonfinite
rotation/translation and reflection checks remain. Command:
`clang++ -std=c++17 -O2 -Wall -Wextra -Werror verification/probe/fog_spatial_math_fixture.cpp -o /tmp/x3-run54-firstperson-fog/fog-spatial-math`;
executing it passes. The same fixture against the original helper aborts at the
captured-camera admission assertion (exit -6). Reproduction and compact triage:
`/tmp/x3-run54-firstperson-fog/diagnose.py`, `summary.json`, `report.md`,
`witness.log`, `fixed-witness.log`, and `prefix-math.log`.

`PYTHONPATH=verification/probe python3 -m unittest -v verification.analysis.test_fog_cards verification.analysis.test_fog_sector_policy verification.analysis.test_fog_route_bridge verification.analysis.test_camera_reprojection`
passes **17 tests in 6.193 s**. The actual MotionOutput card-method tests preserve
cut, sector, failure and Reset-policy coverage; their parameter helper is
mocked, so they do not independently exercise the new camera-to-route admission.
`i686-w64-mingw32-g++ -std=gnu++17 -O2 -g -DWIN32_LEAN_AND_MEAN -DNOMINMAX -Wall -Wextra -Wno-cast-function-type -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -c src/proxy/motion_output.cpp -o /tmp/x3-run54-firstperson-fog/motion_output.o`
passes. No shader/GPU state/resource or hook/ABI implementation changes; existing
GPU state/Reset evidence remains applicable. A candidate integration gate can
reuse the actual route bridge with a captured rotation to prove admitted
replacement reaches the real pass. No new broad GPU qualification or benchmark
is justified by this constant-only change. This checkpoint establishes host
numerical behavior and Windows cross-compilation, not native Windows execution
or corrected game appearance. No Wine, game launch, full DLL build, install or
commit was performed by this task.

The follow-up actual-D3D route bridge adds **33 captured rotations** (the 32 F8
frames plus worst frame2703) through the unchanged production parameter helper,
card admission and `FogPass`. After the existing warmup and single replacement
transition, every frame must suppress its native card, apply one real fog
transaction, preserve state and RT1/RT2 bytes, and request no additional history
invalidation. Four refusal cases cover reflection, excessive shear, nonfinite
rotation and the independent uploaded-inverse Gram boundary. Each must forward
native color exactly, issue no fog pass, avoid a fault latch and recover on the
next valid camera. The inverse-boundary case explicitly passes `fog_world_basis`
and fails `fog_valid_params`: the two guards remain independent.

Only rotations are captured inputs here: camera translation is synthetic and
holds the fixture's world origin fixed; sun, sector and owner are authored.
There is no shaft-map publication, selector-hook execution or new native Reset
coverage. The synthetic replay owner was updated to provide the typed no-map
interface required by the current production fragment. The checker requires all
33 occurrences of each captured-frame witness and all four refusal/recovery
witnesses; **four checker host tests pass**. Fixture x86 cross-compilation passes
with the normal SSE2/incoming-stack flags. Frozen build:
`/tmp/x3-run54-firstperson-fog/route-build-v3/build.json` and
`fog_route_bridge.exe`. Build command:
`python3 verification/probe/fog_route_bridge_build.py --production-root /tmp/x3-run54-fog-camera --spatial-root /tmp/x3-run54-fog-camera --asset-data /tmp/x3-run54-candidate/build/generated/fog_field --output /tmp/x3-run54-firstperson-fog/route-build-v3`.
Root-owned locked execution now passes **515 checks**, exit **0**, in bottle
**X3**, CrossOver Preview, WineArch **arm64**, with
`FEX_X87REDUCEDPRECISION=1` and `WINEMSYNC=1`. All **33** captured rotations
suppress their native card and apply the real fog pass without further history
invalidation; all **four** malformed-camera cases preserve exact native color,
perform no fog writes and recover. The inverse-Gram boundary refusal occurs
once as required. Existing bridge state/auxiliary-target, LastError,
sector/generation, fault and policy-Reset assertions also pass. Lock wait is
**0.000003875 s**; wrapper child elapsed is **4.784124042 s** (not a renderer
performance measurement).

Frozen fixture SHA-256:
`d65f6c448ee0176cfeb894bfee9d63c2f9ddaed19bd527c74e062b2f0a8d4b57`.
Evidence: `/tmp/x3-run54-firstperson-fog/route-runtime.log`, `route-lock.json`,
`route-result.json` and `route-prepared.json`. The checker binds the executable
and runtime log; the build binds its production and fixture inputs. The root
used `verification/probe/wine_lock.py` with the X3 environment above, native
`d3d9=b`, the frozen fixture, and
`Z:\private\tmp\x3-fog-renderer-production\build\fog-production-r3\state-bound\cases.txt`.
This establishes actual-D3D camera-to-route admission for those captured
rotations under the stated synthetic-owner/no-map scope. Native Windows,
actual game reader/selector dispatch, new native Reset coverage and corrected
flight appearance remain unverified by this run.

## Run55: first-person fog accepted (2026-09-21)

The user reports `/tmp/x3-bottleX3-run199` (414 referenced files): fog is fixed
in first-person view, and F8 was taken in that view. This accepts the visible
first-person correction; it does not establish every family or shafts appearance.
The flight crashed again, so overall candidate qualification remained open.
[Capture evidence](../../verification/results/run55-fog-triage/report.md): frames
2188–2219 have 32 valid cameras and all 32 pass the actual corrected helper.
Frame 2197 samples replacement ready/applied with 3/3 cards suppressed and no
refusal/fault. Fog-frame telemetry is sparse and has no row inside F8; the user
report establishes appearance, not a per-frame FogPass or shafts acceptance.

## Run56 request: 30–40 km offline range experiment (2026-09-21)

The reviewed [replay report](../../verification/results/fog-distance-replay-2026-09-21/report.md)
rejects the tested 24/48-bin far extension, not the user's requested visibility
range. It preserves the current 24-step near segment and compares filtered far
sampling with an unfiltered 128/64-unit reference on four endpoint views from
run200, using two fog families. Reference convergence passes (worst absolute
transmittance difference 0.000128); candidate24/48 errors reach 0.293/0.195.
The errors are consistent with isotropic filtering erasing direction-dependent
columns, but this checkpoint does not isolate filtering from quadrature error.
Volume-mean preservation does not guarantee each viewing ray's opacity.

Canonical focused tests pass 9/9. Independent source/evidence review corrected
pixel-centre/jitter reconstruction, point-sun cascade selection, unsupported
confidence intervals on deterministic samples, and mixed sky/geometry counts.
Scattering comparisons use captured point-sun direction and unit radiance; they
do not establish production-scaled radiance parity. The sky clear-fraction screen
is descriptive and does not establish user rejection of the spatial recipe.
No production code, candidate, Wine execution or flight changed.

Next authorized offline experiment: accurate reference-only endpoint images,
with the near segment unchanged and separate 2.4–30 km and 30–40 km shell
contributions. Preserve density, scale and occupancy. Inspect converged images
before choosing a different spatial distribution or GPU integration algorithm.
GPU cost, native Windows execution and flight appearance remain unverified.

### Accurate distant-shell reference

The subsequent [reference-only experiment](../../verification/results/fog-distance-reference-2026-09-21/report.md)
passes numerical convergence: 2,304 deterministic rays per endpoint view, four
views, worst 128/64-unit far-transmittance p99/max differences 0.0001333/0.0002205.
The near24 contribution remains exact. The 30–40 km shell varies spatially but
exceeds 0.002 opacity on 66.38–94.25% of sampled sky rays; variation alone does
not establish isolated patches or user approval. Complete-column sky clear
fraction below 0.002 is 0–0.78%, a descriptive screen only.

Independent review passes 11 focused host tests and verifies all 48 local image
hashes. It corrected report laws/status, split-shell operation counts and
shell-local correlation labels. Scattering still uses normalized unit radiance.
The images are cloud-only reference comparisons, not a recomposited flight or
a proposed real-time implementation. No new medium, density, world scale or
production integrator is selected; production cost and appearance remain open.

### Fixed macro-bank comparison: not selected

The [single fixed morphology experiment](../../verification/results/fog-macro-comparison-2026-09-21/report.md)
multiplies density and premultiplied colour by the same family field sampled at
P/16, clamped to [0,1]. Local detail scale, strength and world anchoring are
unchanged; there was no parameter search or mean-density compensation. Both
current and banked fields converge in the unfiltered reference.

The candidate creates distant clear space but removes all sampled nearby fog:
each of four endpoint views has near T exactly 1 and S exactly 0 on all 2,304
rays. This is independent of the thresholded useful-pixel counts. It therefore
is not selected as a replacement for the accepted nearby appearance. Complete
sky clear fractions change from 0–0.78% to 29.67–97.36%; these descriptive
numbers are not user visual acceptance. Only this fixed recipe/views are closed.
Independent review passes 14 focused host tests and all 16 image hashes.
No production, game, Wine or installation change was made.

### Next bounded experiment: coarse far transport reconstruction

Selected for offline feasibility only: a 64×36 angular grid with 32 cumulative
far-transport planes, `s(k)=12000+5875*k`, preserving the unchanged near24 term
and original family density/colour. Reconstruct at independent 128×72 captured
pixel/depth rays using optical-depth interpolation along each ray and bilinear
S/T interpolation between rays. Dense 64/128-unit unfiltered integration remains
the reference; it is not a proposed runtime marcher.

Freeze the four run200 endpoints and all grid/bin choices before measuring.
Reference convergence gates are T p99/max 0.00025/0.00075; reconstruction gates
are T 0.001/0.003 and normalized S per channel 0.0005/0.002. Report sky,
geometry, depth-boundary and synthetic thin-depth witnesses separately. A failing
endpoint closes this fixed candidate without resolution/bin/threshold tuning.
Only if endpoints pass, check fixed moving-camera holdouts. No performance or
appearance acceptance follows from a numerical pass; long-range accumulated
haze remains a separate unsolved requirement.

Native card placements are not a recovered sparse world-space cloud layout:
[sector-fog §12](../reverse-engineering/sector-fog.md) records a camera-nearest
periodic lattice, view-dependent opacity and refresh-dependent body/scale.
Extruding those cards would invent volume thickness and support. This experiment
therefore introduces neither such an extrusion nor a retuned macro mask.

### Coarse far-transport result: rejected

The [fixed prefix experiment](../../verification/results/fog-prefix-reconstruction-2026-09-21/report.md)
completed four views × 9,216 independent holdout rays in 103.13 s. Reference
and prefix-quadrature convergence pass (worst T maxima 0.000212073 and
0.000158787), but reconstruction fails in every endpoint and boundary group.
Complete all-ray T p99 is 0.059696–0.127455 against 0.001; maximum is
0.116005–0.206792 against 0.003. Normalized scattering also fails. Temporal
replay correctly stops here; no grid/bin/threshold tuning follows.

Eight focused operator tests and eight image bindings pass independent combined
source/evidence review. Post-execution fixes add depth-file provenance and honest
witness labels; the preserved executed source and raw numerical report remain
separately hash-bound from final report generation. No numerical arrays or
images were changed by finalization. Cloud-only images also show that the
accurate unchanged long-range field accumulates broad attenuation. Numerical
accuracy alone would not establish the requested clear gaps or visual acceptance.
No production code, Wine execution, game, build or installation changed.

### Finite-bank preview: reference appearance not selected

The [reviewed fixed-bank preview](../../verification/results/fog-finite-banks-2026-09-21/report.md)
produces separated clouds with clear space, but the user explicitly prefers
**broader, connected clouds** (2026-09-21). Do not carry this isolated-bank
layout into a production build. This visual decision is separate from the
500-unit integration failure: reference convergence passes (worst T maximum
0.000220120), while candidate sampling fails 3/12 canonical views and the green
entry/exit temporal check; worst canonical T p99/max is 0.001370600/0.003345430.
No radius/spacing/seed or sample-count tuning followed.

Nearer banks obscure the nominal target in the all-bank 30/38 km views, so six
same-camera bank0-only diagnostics were added. The target remains visible at
those distances. Its nearest support is 26.727 km at the 30 km pose and
34.730 km at the 38 km pose. The scene is synthetic cloud-only transport, not
final game lighting or composited flight evidence.

Nine focused tests and all 32 image bindings pass independent review. Original
transport and 26 images remain cached; full-resolution analytic edge coverage
was recomputed and six isolated-bank images were newly evaluated, with separate
provenance. Corrected cost witnesses see 5–10 full-resolution visible banks and
3,574–6,422 per-bank-union support-edge pixels potentially requiring repair.
These are operation/coverage counts, not GPU timing. Production, native Windows,
state/Reset, radiance/shafts and flight acceptance remain open.

### Fixed paired-lobe connected-cloud reference preview

The frozen six-view, reference-only preview passed its numerical 128/64 transport check: worst T p99/max was 0.0000814766/0.000187635 against 0.00025/0.00075. The host analysis took 259.69 s; 9 focused tests passed, and hashes for all 6 fixed-scale cloud-only view sheets plus 2 localXY support/detail slice sheets bind to the report. The largest analytic count was 1,558 implied atlas reads per ray at 128-unit spacing; it excludes lighting, shadows, repair and GPU timing.

This establishes reference convergence only. The broad near views and speckled oval distant views require parent/user appearance review; no selection is recorded. It does not approve aesthetics, a production integrator, GPU cost, flight appearance or native runtime behavior. No parameter iteration followed the result.

[Checkpoint](../../verification/results/fog-connected-preview-2026-09-21/checkpoint.json) and [review](../../verification/results/fog-connected-preview-2026-09-21/review.json).

User appearance verdict: interior close to the target; the distant single-bulb
shape is rejected. The requested distribution is irregular cloud patches
covering a sector or substantial parts of it. Numerical results remain valid;
the paired-lobe layout is not selected for production.

### Fixed sector-wide irregular-patches reference preview

The frozen six-view analytic reference passed its 128/64 numerical check: worst T p99/max was 0.000131214/0.000238419 against 0.00025/0.00075. The host run took 164.84 s; 12 focused tests passed and all 6 comparison sheets plus 2 sectorXY slices are hash-bound. At 128-unit spacing, each full ray evaluates 37,512 macro corner hashes and the maximum is 3,126 fine-atlas reads before lighting, shadows, repair, state traffic or GPU timing.

The fixed field removes the rejected finite bulb silhouette but does not produce material clear pockets in the saved canonical views. For every A/C ray the macro-zero sample fraction is exactly zero and macro support length is 200,000 render units. At B, macro-zero fraction mean/p99/max is 0.000521/0.020825/0.078055 and mean support length is 199,896.64 render units. Complete-column clear fractions below .002 are 0.00130/0.00293/0.00336 for bluewell A/B/C and 0/0/0.000326 for foggreenoutlands. The fixed sectorXY slice alone is 14.2517% exact-zero, 46.9971% transition and 38.7512% full macro weight.

The sheets therefore replace the bulb with full-frame dense, repeating fine-field structure; visible diagonal/repeated detail remains. Root has not selected this trial for production, while appearance acceptance remains for parent/user review. No parameter iteration or new preview followed. This note is additive to frozen report SHA-256 `3e3c995709a9651e154db37a5598b847a2007bf773256abbd36642d2cbfe7444`; source and numeric report are unchanged.

[Checkpoint](../../verification/results/fog-sector-patches-2026-09-21/checkpoint.json), [review](../../verification/results/fog-sector-patches-2026-09-21/review.json).

User reiterates the target: more clear space and diverse density throughout the
sector, with varied shapes/sizes and internal density. This blanket-like trial
is not selected.

### Cloud mass and internal detail: cheap column screen

A fixed alternative uses nonperiodic broad cloud mass plus smaller-scale
modulation/erosion, rather than the old tiled family alpha as primary density.
The [screen](../../verification/results/fog-mass-column-screen-2026-09-21/checkpoint.json)
evaluates two green comparison poses at 16×9 rays and 256/512 midpoint stations.
It samples 221,184 shared world positions in 0.209 s; 9 focused tests and
independent source/evidence review pass. Mean opacity A/B is 0.230902/0.252757
for mass alone and 0.177520/0.204308 with detail. The largest transmission
discrepancy between station counts is 0.0000671116. These are sparse-column
estimates, not the earlier dense reference or an image.

The sampled positive lengths decrease substantially, but only A has a sampled
fully clear ray; no broad visual-gap acceptance follows. The result justifies
one fixed mass/detail image comparison and pixel-area witness. No density
rescale, seed search, production integrator or runtime approval is selected.

### Fixed green mass/detail reference packet

The frozen green A/B packet passed its fixed 128/64 along-ray reference check; worst near/full/shell T max was 0.0000035763. The host run took 88.05 s, 9 focused tests passed, and all four base sheets plus two B area-witness sheets are hash-bound. The density arms use the exact frozen F/B/D recipe. Both use constant family chroma `sum(original_J)/sum(original_rho)` = (0.2007273, 1.0, 0.1207049), so this controls density and preserves average colour rather than spatial chroma variation.

The sheets remove the finite bulb and repeated diagonal carpet. They show broader connected irregular regions and visually dark/open gaps; the erosion arm adds relatively subtle mottled interior variation, while the interiors remain fairly smooth. Parent/user appearance selection is pending. B's 256x144 linear S/T area average remains close in mean to the 128x72 point view; full T p99/max differences are 0.000699/0.001447 for mass-only and 0.001051/0.002879 for mass-plus-erosion. This is one angular witness, not an angular convergence proof.

The analysis evaluated 201,738,240 unique world stations and is host reference work, not a production cost or GPU timing result. No recipe, camera, gain, seed, family, path, bake or shader was changed or added after seeing the output.

[Checkpoint](../../verification/results/fog-mass-detail-preview-2026-09-21/checkpoint.json), [review](../../verification/results/fog-mass-detail-preview-2026-09-21/review.json).

The user finds the distribution close and requests a little more patchiness and
varying density. The overall mass layout is the selected visual direction;
one modest internal-detail refinement is next. This is not production approval.

### Modest internal-detail refinement

Following the user’s request for a little more patchiness and varying density,
the fixed reference preserves the broad mass layout and changes only internal
modulation/weak-edge erosion. Two green A/B views and one B pixel-area witness
pass six focused tests and independent source/evidence review. Worst 128/64
along-ray transmission difference is 0.000004709. Mean opacity falls from
0.178401/0.204468 to 0.158121/0.184848 without gain compensation.

The images show modest additional mottling and clearer weak edges. B’s
area-versus-point transmission p99/max difference is 0.001188/0.002951; this
is one angular witness, not convergence proof. The 81.73-second host reference
provides no GPU cost estimate. Final game appearance and runtime representation
remain open; no production fog or installed build changed.

[Checkpoint](../../verification/results/fog-mass-detail-refinement-2026-09-21/checkpoint.json),
[review](../../verification/results/fog-mass-detail-refinement-2026-09-21/review.json).

### Fixed global 64-sample transport: negative preflight

The fixed analytic field screen evaluates 32×18 rays from each existing A/B
view, explicit depth cases and small camera movements. Exactly 64 global
midpoints share contributions across near/middle/shell intervals. Nine gates
fail, including full-distance transmission and temporal stability. A-sky full
transmission p99/max error is 0.002622/0.003182; worst motion residual is
0.004867. Dense 64/128-unit references, composition, empty/invalid-depth laws
and eight focused tests pass. Independent review accepts the negative result.

The proposed corner-cache representation would need mean/max 358.92/452
density texture reads per sky ray, versus 48 currently; these are operation
counts, not GPU timings. This method stops before cache/shader implementation
and without a step-count search. A stored, filtered final-density representation
is a separate design question; the user-liked mass/detail appearance is retained.

The 2.864-second host run emitted six NumPy/Accelerate matrix warnings. A finite
4,096-point diagnostic repeated identically, and the finite JSON/reference laws
show no observed corruption. This remains a tooling portability limitation;
future rotation evaluation should avoid that warning path.

[Checkpoint](../../verification/results/fog-analytic64-screen-2026-09-21/checkpoint.json),
[review](../../verification/results/fog-analytic64-screen-2026-09-21/review.json).

## Stored-density runtime screen closed (2026-09-21)

One run of `tools/analysis/fog_density_runtime_screen.py` against the ratified
[plan](../architecture/fog-density-runtime-plan.md) (plan SHA-256 `ca767429…`),
after an independent pre-run review (vacuous alpha law removed, dense read scan
and aggregate cost reporting added, startup plan-digest check, existing-output
refusal, two pre-existing crash fixes; 13 focused tests pass) and a combined
post-run review. Host time 74.6 s; no parameter changed.

| Metric | Result | Gate |
| --- | --- | --- |
| Candidate vs dense64 T p99 (far/shell, four populations) | .00197 / .00170 / .00144 / .00117 | .001 (fail) |
| Candidate vs dense64 T max | ≤ .00236 | .003 (pass) |
| Near segments T p99 / max | ≤ 2.5e-5 / 2.9e-5 | pass |
| Temporal quadrature max | .00184 | .003 (pass) |
| Representation vs exact field T p99 / max | .0067–.0147 / .0222 | not gated |
| Reference convergence (analytic/filtered, 3 segments, 2 poses) | all converged | pass |
| Mean reads per ray (candidate / sky / worst case) | 132.0 / 132 / 172 at L=29300 | intent, not GPU |
| Lazy nodes fine / far; full bake | 192,467 / 109,837; 4,194,304 nodes = 8 MiB | intent |

Verdict: the failed gate is quadrature in the far interval, and closing it would
require changing the frozen 24+40 sample counts; overall accuracy is dominated by
the representation/prefilter error, which the four fixed images show as a loss
of the liked mass/detail refinement (notches and lobe edges blurred, small dark
holes filled), with no banding, blockiness, LOD seam or taper edge. Against the
closed global64 screen this route is strictly better (T p99 .00197 vs .002622,
max .00236 vs .003182, temporal .00184 vs .004867, 132 vs ~359 reads) and still
insufficient. The route is closed; no stored-density production integrator is
selected. Open plan gap: the plan's source-alpha 0/.37/1 law has no substitute in
this screen. Compact results:
[summary](../../verification/results/fog-density-runtime-screen/summary.json),
[report](../../verification/results/fog-density-runtime-screen/report.md);
full report and images stay local under `/tmp/x3-fog-density-runtime-screen`.

Addendum, same day: the user inspected the four images and accepts the appearance
("okay for me, don't see any differences between columns"). The .001 quadrature
gate is below display resolution; the orchestrator rescaled the runtime accuracy
gate to half a display code (T p99 ≤ .002, max ≤ .003, temporal ≤ .003), which
the measured run passes. The route is reopened for a production integration
design; no shader, build or game execution yet.

## Stored-density runtime integration, checkpoint 1: generator (2026-09-21)

Production `src/fog/fog_density_generator.{h,cpp}` (field, eight-point prefilter,
RNE FP16, node/window/storage/texel address law, 32×32 brick and 129² tile
packing with the duplicate border; no D3D) against the validated
`tools/analysis/fog_density_runtime_screen.py`, through the witness tool
`verification/probe/fog_density_generator_host.cpp` and
`verification/analysis/test_fog_density_generator.py` (12 tests, both a native
arm64 clang++ build and an x86_64 `-msse2 -mfpmath=sse` build under Rosetta;
all builds `-O2 -ffp-contract=off`, no fast-math).

| Check | Result |
| --- | --- |
| FP16 node words, 4,128 signed random keys + Q-witness corners (both levels) | 4,128 / 4,128 identical (0 differing, 0 ULP) |
| Full 129² tiles, fine group 0 / far group 31 (2 × 65,536 nodes) | identical words; border column/row 128 = storage 0 |
| Field float32 at 1,004 points, R / R² constants | bitwise identical |
| Nonzero world offset O_s (512 nodes/level) and O_s = k·delta identity | identical |
| float32 → binary16 RNE vs NumPy (all finite halves, ±1 ulp neighbours, 60k randoms, overflow/subnormal edges) | 127,598 words identical |
| ±5500 shift witnesses: origin, local, containment, trilinear of C++ words vs report `fine`/`far` | 22 / 22 equal (the test fails when the local report is absent unless `X3M_FOG_GOLDEN_OPTIONAL=1`) |
| binary16 → float32 round trip of all 65,536 half words vs NumPy (review fix: subnormal exponent 112-e; 0x0001 → 0x33800000) | exact |
| `lod_weights` at 0/20000/22500/25000/30000/100000/150000/175000/200000/250000 vs the screen's smoothstep law; `atlas_offset` (last border texel ends at 4,260,096 B); `duplicate_tile_border` in place for groups 0/9/31 (rest of atlas untouched); unknown level refused | pass |
| Address law vs `window_origin`/`pack_address`, 2 cameras × 5 points × 3 axis shifts × 2 levels; lane 0,1,2,3,0,3 | pass |

Throughput (32×32-texel bricks = 4,096 nodes each, one thread, `bench 3`):

| Build | fine nodes/s | far nodes/s | 65,536-node tile |
| --- | --- | --- | --- |
| Native arm64 (this Mac) | 3,706,211 | 3,669,203 | 17.9 ms |
| x86_64 SSE2 under Rosetta | 2,698,325 | 2,729,522 | 23.7 ms |
| i686 MinGW under Wine/FEX, bottle X3 (arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`) | 1,933,051 | 1,933,727 | 33.8 / 33.5 ms |

Wine rows: final exe `e67a7603…` run by the orchestrator
(`/tmp/x3-fog-generator-bench-v2/`, rc 0, lock wait 3 µs, child 9.45 s);
`tile_group0_fnv1a` fine `db363799c20b21ea` / far `4279b0dcca4e5e02` equal the
native build's checksums for the same source (131,072 nodes identical under
FEX). An earlier run of the pre-performance-pass exe (`9880cd96…`,
`/tmp/x3-fog-generator-bench-v1/`) measured 0.46 M nodes/s with the same
checksums; the pass (`floor_i32`, int32 key conversion, shared XY weights)
removed every x87 control-word, conversion and arithmetic instruction from the
i686 TU (only cdecl `flds`/`fstps` float-return moves remain) and gave 4.2× under
FEX with native rates unchanged. The review fix to `half_to_float` (subnormal
exponent 112-e) is off the generation path; the native checksums were re-derived
after it and are unchanged.

Initial-fill estimate for ≈0.97 M far + ≈1.69 M fine nodes on one worker:
Wine/FEX ≈2.66 M nodes / 1.93 M nodes/s ≈ **1.4 s**; native arm64 ≈ 0.72 s. Below
the design's "add a second worker if above ~5 s" line for the CrossOver target;
native Windows remains an estimate.

## Stored-density runtime integration, checkpoint 2: shaders and GPU numerics (2026-09-21)

Static caches only; no cache manager, worker, slab upload, readiness ramp or Reset
(checkpoint 3). The production `FogPass` still binds the old programs.

Sources: `src/fog/fog_density_field_inc.h` (level sampler, single 64-bin loop, shared
footprint law) with `fog_density_{march,composite,repair}_ps.hlsl`; fragments
`src/renderer/fog_density_*_program_inc.h` from `tools/shaders/generate_rigid_motion_pixel.py`
(native `d3dx9_37` flow, `--check` PASS). Registers: c0–c21 as today (c2.xyz unused, far
readiness folds into sigma), c22/c23 = camera modulo 128·delta (centred) and 1/delta per
level, c24 = family mean chroma and fine readiness; s1 fine atlas, s7 far atlas. The
verification-only `verification/probe/fog_density_march_exact_ps.hlsl` fetches the eight
texels with POINT sampling (no shafts, to stay inside 32 temporaries).

| Program | Slots (Microsoft ps_3_0 table, flow control included) | Words | Texture reads per pixel |
| --- | --- | --- | --- |
| march | 415 | 1,768 | 1 depth + 2 per level sample: 49 near-only, 133 sky, 173 worst (L = 29,300); shaft taps unchanged |
| composite | 203 | 888 | 10 (scene, depth, 4 half depths, 4 ST); never marches |
| repair | 510 | 2,112 | 5 depth reads, then `clip`; repaired pixels add the scene and one march |
| march, texel exact (verification) | 301 | 1,259 | 8 per level sample (529 sky) |

`ambient_occlusion_program_slots` charges flow control one slot each and therefore reads
lower; the table above uses `verification/probe/fog_density_shader_slots.py`. Repair needed
three reductions to fit (sign-only footprint test, vectorised depth classes and tap
coordinates); its two-slot headroom is the constraint on any later shader addition.

Hardware bilinear cannot replace any part of the eight-point prefilter: that is a bake-time
mean of analytic field evaluations at ±delta/4, not a filter over stored texels. At run time
it replaces the XY half of the trilinear reconstruction, not exactly: measured below.

Fixture `verification/probe/fog_density_shader_fixture.cpp` (i686 MinGW, SSE2, four-byte
incoming stack, `-ffp-contract=off`), driven by `fog_density_shader_run.py build|run|check`,
reference from `tools/analysis/fog_density_shader_reference.py` (imports the screen
unchanged; screen digest `da3dcbe2…` preserved). Both atlases are filled by
`generate_tile` through SYSTEMMEM → `UpdateTexture` for poses A/B and the six ±4500/5000/5500
shifts each (24 atlases, 1.955 M nodes/s under FEX); 128×72 rays identical to the screen;
2,240 sky rays, 70 geometry rays (5 witnesses × 7 depths × 2 poses), 60 shifted rays.
Run: bottle X3, arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`, exe
`7fc0de3c8c8b501cfb6279fdc1829164ce8a1070b4b0d47cd81c65a9941ebeed`, 31.7 s, 12 fixture checks,
output `/tmp/x3-fog-density-shader-v1/fixture4`; compact
[summary](../../verification/results/fog-density-shader/summary.json).

| Comparison (2,370 rays; p99 / max) | T | S per channel |
| --- | --- | --- |
| GPU texel exact, 32F target, vs host 24+40 candidate | 1.07e-6 / 2.21e-6 | 3.9e-7 / 5.2e-7 |
| GPU bilinear, 32F target, vs host candidate (gate T .002 / .003; parity T max .001) | 7.0e-5 / 1.47e-4 | 1.9e-5 / 5.6e-5 |
| GPU bilinear, production RGBA16F target, vs host candidate | 4.94e-4 / 5.52e-4 | 8.1e-5 / 1.45e-4 |
| GPU bilinear vs host filtered dense64 (gate T .002 / .003) | .001708 / .002342 | .000702 / .001255 |
| host candidate vs host dense64 (no GPU) | .001689 / .002360 | .000706 / .001251 |
| FP16 bilinear vs texel exact, all 258,048 pixels of the fogged cases | 6.1e-5 / 2.36e-4 | 1.5e-5 / 1.03e-4 |
| temporal residual vs dense64, 60 shift pairs (gate .003) | max .001833 | |

Passed: every T gate, temporal, slots, exact identity for depth 0 / NaN / +inf and for a
zero cache, exact scene through composite and repair on a zero cache, source alpha .37
exact, composite keeps the scene on zero weight, repair leaves compatible pixels
bit-identical and rewrites 17,899 of 18,432 zero-weight pixels to within 4.9e-4 (one
RGBA16F step) of a full-resolution march. §7 assumption measured: FP16 bilinear on this
backend costs at most 2.4e-4 in T, an eighth of the p99 gate; the dominant implementation
error is the RGBA16F (S,T) target step near T = 1 (4.9e-4), not filtering. The texel-exact
path (4× the fetches) is not needed for T.

Not met, reported outside the result: the design's §5 S gates. Parity S max 3e-5 holds for
texel-exact fetches (5.2e-7) and not for bilinear (5.6e-5); dense64 S p99 .0005 is already
exceeded by the host's own frozen candidate (.000706), so no faithful implementation can
meet it, and only the T gate was rescaled. Both need an orchestrator decision.

Fixture timing at 1280×768 (march 640×384), **not game FPS and not a credible GPU cost**:
EVENT-completed median (p95) march .78 (.90) ms sky / .90 (1.01) ms worst case, transaction
(march + composite + repair) .95 (.98) ms, CPU submit median .016 / .061 ms; synchronised to a mapped
`GetRenderTargetData`, net of a clear-plus-readback baseline, 1.29 / 1.04 / .83 ms; slope of
ten marches against one in a frame .012 ms per march. The measurements do not scale with
the work submitted (the transaction is not dearer than the march alone, ten marches cost
.1 ms more than one), so this backend does not expose GPU execution time to any of the
three methods. That is consistent with (not shown to be the cause of) the old pass measuring
1.08 ms while the user saw ≈2 FPS. The design's 3.0 / 4.0 ms gate is therefore unverifiable by this kind of fixture.

Not proven: native Windows (documented D3D9 only, never executed there; FP16 bilinear
precision is unspecified by D3D9), dynamic caches, uploads, origin advance, readiness
ramps, Reset, hostile state, shafts with the new march (disabled in this fixture),
real GPU cost. `LodWeights::far` in `src/fog/fog_density_generator.h` collides with the
`far` macro of `windef.h`; the fixture includes the header first, the pass will need a rename.

## Stored-density runtime integration, checkpoint 3: cache manager, worker, uploads, ramps, Reset (2026-09-21)

Production: `src/fog/fog_density_cache.{h,cpp}` (D3D-free manager and worker) and the density
path of `src/renderer/fog_pass.{h,cpp}` (`FogDensityConfig`, `prepare_density`,
`FogFrame::density`, march → composite → repair in the existing state capture). Both new
sources are in the `d3d9` target (`-ffp-contract=off`); a scratch production build links
(`cmake --build … --target d3d9`, not a candidate). The path is unreachable until
`FogDensityConfig::enabled`: before that no thread, allocation, program or device call exists
(fixture check `off_no_worker_no_resources_no_device_calls`). Proxy wiring, sector offset,
launcher option and logging are checkpoint 4. No shader changed: the fragments and their
510 / 512 repair budget are those of checkpoint 2.

Mechanism, as built:

- **Worker.** One `std::thread`, below-normal priority, parked on a condition variable when
  idle. It generates 32×32×4-node jobs with `node_word` into a scratch buffer and commits
  each under a mutex held for one scatter copy. Residency is one node box per level, grown one
  slab at a time (first fill = need box + 2 nodes, nearest job first, far level before fine;
  then the six sides out to the 128³ window). A retarget (camera node ≥ 2 fine / 9 far nodes
  off the window centre, the plan's .25 km / 4 km triggers) intersects the box with the new
  window before any slot is overwritten, so a recentre generates only the entering slabs.
- **Render thread never waits.** Every shared access is a try-lock; a miss retries next frame.
  Steady state is three atomic loads: no lock, no allocation, no device call.
- **Uploads.** SYSTEMMEM staging (`LockRect` + `D3DLOCK_NO_DIRTY_UPDATE`) → DEFAULT RGBA16F by
  `UpdateSurface` with explicit rectangles. Budget per `prepare_density`: **1,065,024 B
  (8 tiles) and 64 rectangles**. Dirty tracking keeps four body rectangles plus the duplicate
  column and row per tile, so an x slab and a y slab through one tile stay two strips (a
  2-node diagonal recentre uploads 265–373 KB instead of the whole 4.26 MB level). A box
  reaches the render side only after every region committed for it was uploaded and confirmed.
- **Readiness.** 1/90 per frame. Far multiplies sigma and gates drawing (zero device calls at
  0); fine multiplies `lambda` (far-only interior while fine fills). A level drops to 0 at once
  when the camera's need box leaves its resident box (cut, sector change, load, Reset), ramps
  down when only a one-node guard is violated, ramps up otherwise. `execute` re-checks the
  need box for its own camera, so a stale ramp can never sample non-resident nodes.
- **Reset / loss.** `before_reset` drops the two DEFAULT atlases only; worker, CPU caches and
  staging survive, and every committed tile is uploaded again under the normal budget. A failed
  upload poisons the pass until Reset (`D3DERR_DEVICENOTRESET`, no device calls) and re-queues
  everything. `detach` joins the worker (never under the loader lock);
  `abandon_density_worker` is the process-exit form (no join, no lock, leaks).
- **Refusal.** `MaxPixelShader30InstructionSlots < 512`, program, staging, atlas or worker
  creation failure refuses the density path once (`density_status().reason`) and leaves the
  legacy family path untouched.
- The cache TU contains no x87 instruction (objdump of the i686 object: SSE2 truncate-and-correct
  floor, ordered compares instead of `fabs`).

Host: `verification/analysis/test_fog_density_cache.py` builds
`verification/probe/fog_density_cache_host.cpp` natively: 63 checks (exact-cover job plans,
box growth, first fill = every node once, five +2-node recentres from storage 126 across the
tile seam and lane 3→0 each generating exactly 128³−126³ = 96,776 nodes and equalling a
from-scratch `generate_tile` atlas, negative 9-far-node recentre, guard / need violations,
Reset mid-recentre with an injected upload failure, identity change, 2.6e6-unit cut, NaN
camera; then the live worker: fill, 40 moving-camera steps, 24 start / invalidate / Reset /
stop storms, invalidate under load). Native arm64: far resident 305 ms, fine 806 ms,
3.75 M nodes/s, `step` 12 ns per frame, worst `stop` 1.05 ms.

Wine: `fog_density_shader_run.py build|run|check` now builds and runs two executables in one
locked run (bottle X3, arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), output
`/tmp/x3-fog-density-pass-v1`, summary `PASS`, 12 / 12 gates:
shader fixture `e814bb18c886aee9451b6a259c791ac6be8844953de14852755852f0aa8a8e78` (38.1 s, 12 checks),
pass fixture `a91837c390243e33d4130255c7bc2a9a57ab773f5941babd957fcc7c0634b984` (26.9 s,
48 checks, **1,006 state restorations**, 14 atlas comparisons with 0 differing bytes);
compact [summary](../../verification/results/fog-density-shader/summary.json) (schema 2).

| Pass fixture check (production `FogPass`, 128×72 unless noted) | Result |
| --- | --- |
| Dynamic fill vs from-scratch `generate_tile` atlases, both DEFAULT atlases read back | bit-identical; 4,194,304 nodes generated once |
| March through the pass vs CPU double march of `node_word` (35 sky rays) | T 4.9e-4, S 1.1e-4 |
| Recentre +2 nodes on all axes while rendering the old pose | 96,776 nodes, 338,768 B; image byte-identical |
| 2.6e6-unit camera cut | both ramps 0 in the same frame, zero-device-call frame, refill |
| Three +2-node recentres from storage 126 (seam, border copies, group 31 lane 3 → group 0 lane 0) | 96,776 nodes each, 265–373 KB each, atlases bit-identical to from-scratch |
| Seam-crossing rays on both levels vs CPU; control with a broken wrap | 4.8e-4; control .0346 (fine 16 / 7, far 128 / 96 seam / lane samples) |
| Shafts: fully lit map, fully shadowed map, split map vs CPU PCF twin | bit-identical to no map; S = 0 with identical T; 1.0e-4 |
| Ramps over 611 drawn frames (382 no-fog frames before) | monotone, ≤ 1/90 per frame; worst per-frame ΔT .00586 ≤ bound .00912 (τmax .488) |
| Reset mid-fill; Reset of a complete cache | bit-identical atlases and image; 8,520,192 B re-uploaded, 0 nodes regenerated |
| Injected `D3DERR_DEVICELOST` in `UpdateSurface` | poisoned until Reset without device calls, then bit-identical |
| 31×17 target (half 16×9): repair vs CPU march through the raster pixel; half-pixel control; composite at the clamped edge | 4.9e-4; control .0193; 4.8e-4; source alpha exact |
| Hostile state (16 samplers, c0–c31, MRT, streams, scissor, viewport) around every `execute` (scene open and closed), the resource-creating first `prepare_density` and both legacy transactions | 1,006 byte-identical snapshots |
| Refusal (`MaxPixelShader30InstructionSlots` = 511): legacy Bluewell transaction with and without the refused request | `density_ps30_slots`; no worker; legacy output bit-identical and fogged |
| `detach` in the middle of a fill | joined in 7.3 ms, 0 references |

Measured budgets (render-thread CPU under this harness; **not game FPS, not GPU cost**):
time to first fog 589 ms, fine 1,552 ms after the first `prepare_density`, worker
1.95 M nodes/s under FEX while the fixture renders; first `prepare_density` 3.3 ms (programs,
four textures, caches, thread); steady `prepare_density` 0.48 µs per frame; upload frames
(5,955) median 18.7 µs, p95 34.8, p99 67, max 294 µs, 5.9 µs per `UpdateSurface`; per-frame
maxima reached 1,065,024 B and 64 calls, never exceeded. Memory only after enable: CPU
2 × 4.06 MiB caches + 2 × 4.06 MiB staging, GPU 8.1 MiB. Frames here are ≈ 2 ms; at 60 FPS
each 90-frame ramp is 1.5 s.

Checkpoint-2 numeric gates, same run (2,370 rays, p99 / max): bilinear 32F vs candidate T
7.0e-5 / 1.47e-4, S 1.9e-5 / 5.6e-5; vs dense64 T .001708 / .002342, S .000702 / .001255;
production RGBA16F vs candidate T 4.94e-4 / 5.52e-4, S 8.1e-5 / 1.45e-4; vs dense64 T
.001856 / .002694, S .000696 / .001269; temporal .001833 (32F), .001862 (RGBA16F).

Checkpoint-2 carry-overs:

1. **Repair `c0`.** The repair draw now uploads its own `c0` (`density_repair_projection`).
   Its value equals the march's, and that is the finding: `FogParams::m20/m21` carry the
   *full-resolution* quad term (+1/W, −1/H), the march re-derives the uv of the full texel it
   taps, and the repair shades its own texel, so both look through the raster pixel of their
   depth tap. The review's half-pixel offset exists only under the checkpoint-2 fixture's
   host-screen convention (ray through the half-pixel centre, `c0.z = −1/W`), not under the
   pass contract; adding +0.5/W would create the offset. Proven rather than argued: repaired
   pixels match a CPU march through raster pixel P to 4.9e-4, and the same reference moved
   half a pixel misses by .0193.
2. The repair reference is that CPU march (no `c0`, uv or program shared with the GPU path);
   the old fixture check is kept under the honest name
   `repair_program_consistent_with_march_program`.
3. `fog_density_shader_run.py`: S gated at p99 ≤ .002 / max ≤ .003; production RGBA16F and
   temporal rows (32F and RGBA16F) inside the PASS predicate; bilinear parity S (5.6e-5) and
   texel-exact parity S (5.2e-7) under `reported_not_gated`; no failing gate beside PASS.
4. `LodWeights::far` / `fine` → `far_level` / `fine_level`.
5. Odd sizes: no shader defect found (`(w+1)/2` half targets put the last half sample on the
   last full pixel); the 31×17 run exercises the `sizes.zw − 1` clamp at half 15 / 8.
6. Shafts and the explicit seam / lane 3→0 assertion run through the production march (table).
7. No shader was touched; timing above is CPU only and is not cited as GPU performance.

Not proven: native Windows (documented D3D9 and C++ threads only, never executed there);
GPU cost and game FPS (the design's 3.0 / 4.0 ms gate stays unverifiable on this backend,
checkpoint 2); behaviour under the proxy's real locks and DLL unload (checkpoint 4 must call
`detach` on the device release path and `abandon_density_worker` from process detach);
worker rate inside the game under FEX with contended cores. Turning the option off after it
was on parks the worker and keeps its memory until `detach`. This is the first `std::thread`
in `d3d9.dll` (winpthreads, statically linked; the sampler uses `CreateThread`).

### Checkpoint 3 review fixes (2026-09-21)

Correction to the entry above: its threaded host result was **not reproducible**. The reviewer
measured `fog_density_cache_host threaded` at 5 FAIL / 7 PASS. Cause, harness only: the
quiescence test was `ready == 1 && !has_work()`, and `has_work()` is false while the worker is
generating its next slab, so a partially grown far window was compared with a full static
atlas. Production readiness does not have the hole: a level's `ready` depends on the need box
being inside the resident box, which advances only after the slab's commits were handed out
and confirmed uploaded; the stepped test now proves it (`has_work_false_mid_fill_is_not_idle`,
`mid_fill_resident_box_is_exactly_the_uploaded_slab`, `committed_but_not_uploaded_is_not_resident`).
`ready == 1` with an incomplete 128³ window is intended (need box + 2 nodes first).

- `DensityCache::idle()` (worker saw the last posted request and found nothing to generate,
  everything uploaded and confirmed) replaces `has_work()` as the harness's quiescence test;
  the harness no longer reads `worker_origin()` against a live worker (atlas origin from the
  render-owned `gpu_box`, worker state only after `stop()`).
- Job-distance midpoint no longer adds `lo + hi` in int32; `kCameraLimit` (1e12) is public and
  a stepped fill at (1e12, −1e12, 5.6e11) equals the static atlases; `nextafter(1e12)` is refused.
- The byte budget now includes the first rectangle (a budget under one tile is raised to one
  tile, 133,128 B); a one-tile-budget drain never exceeds 133,128 B per frame.
- A failed `prepare_density` clears `density_status().available`.
- `MotionOutput::release_resources` detaches and resets `fog_` with the other passes (it
  previously relied on `~FogPass`). `DensityCache::stop()` abandons instead of joining if the
  mutex cannot be taken within 250 ms. `fog_pass.h` states who calls
  `abandon_density_worker()` (the proxy's `DLL_PROCESS_DETACH` only, checkpoint 4). **Until
  checkpoint 4 wires it, a dynamic `FreeLibrary` of the DLL with a live device, and process
  exit through DllMain with a live worker, are unsupported.**

Repeat counts after the fix: `fog_density_cache_host threaded` **50 / 50 PASS**, 0 differing
bytes in any run; `python3 -m unittest verification.analysis.test_fog_density_cache`
**10 / 10 OK** (3 tests; the host tool's `all` mode reports 71 checks). Scratch `d3d9` target rebuilds with the
`motion_output.cpp` change. Production pass behaviour changed, so the Wine run was repeated:
output `/tmp/x3-fog-density-pass-v2`, summary PASS 12 / 12 gates, shader fixture
`45d36180f755473725567c1bcd25bb15cd24a662322172957a9c8f9fe127284b` (38.5 s), pass fixture
`5e13472eda67fa72868e47b6b141f92c6bc6117d82a014993adf6b0cce441d4a` (26.8 s, 51 checks,
1,157 state restorations, 0 differing atlas bytes; new checks
`failed_upload_clears_availability`, `device_release_path_leaves_device_refcount_balanced`
(device references 1 → 1 after a mid-fill detach, join 3.3 ms), `detached_pass_starts_no_worker`).
Budgets unchanged: first fog 581 ms, 1.95 M nodes/s, upload frames median 18.5 µs / p95 33 /
max 380 µs, steady 0.48 µs, per-frame maxima 1,065,024 B and 64 calls. The tracked summary is
the v2 run.

## Stored-density runtime integration, checkpoint 4: proxy wiring, launcher option, lifetime (2026-09-21)

Source integration and fixture evidence; **no candidate was built and nothing was flown**. The
mechanism is described in the [architecture note](../architecture/volumetric-fog.md#stored-density-range-option-2026-09-21).
`--volumetric-fog-range {legacy,stored}` → `X3M_VOLUMETRIC_FOG_RANGE` (default `legacy`; the DLL
accepts only the exact string `stored`). `MotionOutput` posts the previous scene end's double
camera at the owner latch, keys the cache by `fog_sector_placement` (whole far nodes, ±2048),
invalidates on a sample gap, and marks a frame `density` only when the far level is drawable for
that frame's own camera; otherwise the frame is native (`density_unprepared` / `density_filling`).

### Checkpoint-3 review findings closed here

1. **Hang on normal quit (HIGH), reproduced and fixed.** `DllMain` `DLL_PROCESS_DETACH` now calls
   `x3m::abandon_fog_density_workers()` first: it walks `devices` without the capture mutex and
   without logging. `DensityCache::abandon()` no longer joins, detaches, notifies or locks (item 4);
   mutex, condition variable and thread live in raw storage whose destructors never run for an
   abandoned cache. The exit fixture proves both the defect and the CRT order on this backend:
   with the worker parked, the control child (no abandon) reaches `static_destructor_begin` and
   never returns (terminated by the 40 s watchdog); with the abandon the same child exits 2 ms
   after `ready`, marks in the order `dllmain_detach_process_exit`, `dllmain_detach_end`,
   `static_destructor_begin`, `static_destructor_end`. Mid-fill: 2 ms as well (its control did not
   hang in this run: the 250 ms give-up or a worker killed outside the wait; not relied on).
2. **Free of an abandoned cache.** `FogPass` releases the cache through `DensityCache::retire`:
   `stop()`, then `delete` only if the cache was not abandoned. Host witness under
   AddressSanitizer: a helper thread holds the mutex with a live worker, `retire` returns after
   ≥ 250 ms, the object is read afterwards (a free would be a reported use-after-free), the worker
   then takes the mutex, sees the stop flag and leaves.
3. **Module pin.** `DensityCache::start` pins its module with
   `GetModuleHandleExW(PIN | FROM_ADDRESS)` before the first worker exists; a failed pin refuses
   the worker (`density_worker`, legacy fallback).
5. `DisableThreadLibraryCalls` and winpthreads' TLS callback: recorded in
   [platform portability](../architecture/platform-portability.md), not verified.

### Evidence

Host (native clang, no Wine):
`PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fog_density_cache verification.analysis.test_fog_route_bridge verification.analysis.test_volumetric_fog verification.analysis.test_fog_sector_policy verification.analysis.test_fog_cards`
— cache 4 tests (the new one builds the harness with `-fsanitize=address -DX3M_FOG_DENSITY_TEST_HOOKS`:
`thread_creation_failure_is_refused`, `start_succeeds_after_a_refused_start`,
`held_lock_abandons_after_250ms_without_free`, `abandon_then_retire_is_prompt_and_leaks`,
`abandoned_cache_does_not_restart`, plus source assertions that `abandon()` and the detach walk
contain no join/detach/notify/lock/log and that `DllMain` calls the walk first); launcher
`test_range_option` (default `legacy`, both values in the dry-run environment, an inherited
`stored` is overwritten, refused without `--volumetric-fog`, with an unknown value and with
strength 0); placement (64 sectors: distinct keys and offsets, whole far nodes, independent of
strength / frame / generation / family); the fragment's host mock (option off: a sample gap never
reaches the density path; on: exactly one invalidation).

Wine, one locked run (bottle X3, arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), output
`/tmp/x3-fog-density-route-v2`, lock wait 0.000004 s, child 58.7 s (a first run, `-v1`, passed with
the same witnesses before a comment-only edit of `fog_pass.h`; `-v2` binds the final sources):
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/fog_route_bridge_run.py run --output /tmp/x3-fog-density-route-v2 --cases /tmp/x3-fog-family-gpu-inputs-final/cases.txt`,
then `fog_route_bridge_run.py check` → compact
[summary](../../verification/results/fog-density-route/summary.json), PASS.

| Step | Executable SHA-256 | Result |
| --- | --- | --- |
| Baseline bridge: today's fixture (`--baseline`) against the production tree of `184843cd` | `c332a225ab9884e4…` | 515 checks, 2.6 s |
| Route bridge: unchanged fragment, real `FogPass`, real worker | `f160a65392025b44…` | 26,009 checks (one state-restoration, once-only and RT1/RT2 check per frame), 12.7 s |
| Exit fixture (DLL `daef5fbc97b333bf…`, exe `cf2babbd516ce39d…`) | | 4 checks, 43.2 s (40 s is the hanging control) |

| Route witness | Result |
| --- | --- |
| Legacy bit-identical to before | five legacy image hashes (warm, replaced, 33 captured cameras, new family, after Reset) equal between the `184843cd` build and this build; the legacy route never creates a cache |
| Capability refusal (`MaxPixelShader30InstructionSlots` 511) | exactly one `event=refused reason=density_ps30_slots fallback=legacy` line, no worker, warm and replaced frames bit-identical to legacy |
| Stored renders through the route | 302 filling frames native-exact with no fog transaction, then march + composite + repair once per frame (3 quads, 1 copy), image ≠ legacy ≠ scene; steady frame bit-identical; state snapshot restored every frame |
| Ramps | far and fine monotone, ≤ 1/90 per frame, no fault |
| Sector change | new key, offset a whole number of far nodes, readiness 0 and native card in the same frame, one TAA invalidation request, refill + ramps, different image; back to the first sector: bit-identical to its first image |
| Ctrl+Alt+F9 off / on | off: native-exact, no `prepare_density` work, worker completes its window and parks (11,063,232 nodes, stable 400 ms); on: bit-identical image, same pass references and allocations, same device refcount, 0 nodes regenerated |
| Reset (pass edges + owner reset state) | 8,520,192 B re-uploaded, 0 nodes regenerated, native while refilling, bit-identical afterwards |
| Load (3 frames without a sample) | invalidated in the same frame, refill, ramps, bit-identical afterwards |
| Camera | 300-unit cut: stays resident, no ramp; 2.6e6-unit jump: native in the same frame (the card is refused before the cache has heard of the camera), no fault, refill |
| Device release with a live, generating worker | production release statements: joined in 2.6 ms, device refcount 13 → 13; whole run leaves the device refcount unchanged |
| Abandon, then `~FogPass` | 0.86 ms, device refcount balanced, no join |
| Logging | 23 `volumetric_fog_cache` lines for the whole run, no per-frame line without the timing option |

Measured under this harness (render-thread CPU, FEX; **not game FPS, not GPU cost**): time to
first fog 782–836 ms after the key, fine ready 1,726–1,781 ms (each includes its 90-frame ramp at
≈ 2.2 ms frames); `prepare_volumetric_fog_density` steady median 1.90 µs / p99 47 µs (163 frames),
upload frames median 20.4 µs / p99 70 µs (3,952 frames); one 5.6 ms maximum, attributed (not isolated) to the first call
(three programs, four textures, two caches, thread, chroma scan of the decoded family packet).
Bluewell mean chroma .0516 / .2695 / 1.0, sigma 2.5e-6. With `legacy` the added cost is two
untaken branches per frame.

Scratch production build (not a candidate):
`cmake -S . -B <scratch> -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3`,
`cmake --build <scratch> --target d3d9` links;
`python3 verification/probe/check_no_x87.py <scratch>/d3d9.dll` (DLL `aa313250e8506c56…`): PASS, roots 95, reachable 544,
violations 0. The audit walks the light hooks; the worker is its own thread (x87 state is per
thread and it calls nothing of the game), and checkpoint 3 already showed its TU free of x87.

### Not proven / unsupported

- Native Windows: documented D3D9, Win32 and C++ threads only, cross-compiled, never executed there.
- The checkpoint-3 pass fixture was not rerun after the cache lifetime change (raw sync storage,
  `retire`, pin); the route bridge and the host harness exercise the changed code.
- Native device Reset stays with the pass fixture; the bridge uses the pass Reset edges.
- GPU cost and game FPS; worker rate inside the game; appearance (constant family chroma, card
  hand-over to a ramping medium, far-only interior while fine fills): the flight.
- Placement hashes engine heap tokens: clouds move between sessions and may move on a reload.
- `TerminateThread` of the render thread inside a cache lock; a second device using the pass
  while `DLL_PROCESS_DETACH` walks the map on a dynamic `FreeLibrary` (the pin makes the unload a
  no-op once a worker existed; before that no worker exists).
- An abandoned cache leaks ≈ 8.5 MiB CPU memory and one thread handle (process exit, or the
  250 ms give-up on device release).

### Checkpoint 4 review fixes (2026-09-21)

Independent review: accepted with findings. Main `85c7c821` was merged first (clean; its motion-route
option is unrelated). The entry above is superseded where this one differs.

1. **Abandon walk only at process exit.** `DllMain`: `if (reserved != nullptr) x3m::abandon_fog_density_workers();`
   A `FreeLibrary` detach no longer iterates `devices` while live threads mutate it; the pin makes a
   `FreeLibrary` with a worker unreachable.
2. **Session-stable placement (design amendment).** `fog_sector_placement` hashes the background record
   `index`, the family profile and the recipe; sector, table and record heap tokens never enter the key
   (they still drive the card / TAA rewarm through `same_key`). Host test: same index + profile + recipe
   with different tokens → same key and translation; 63 other indices, another profile and another recipe
   → different. Bridge: `heap_token_change_keeps_key_cache_and_image` (cards re-warm; no re-key, no new
   first fill, no ramp, image bit-identical). Sectors sharing one background record share a placement.
3. **Cards stay until the far ramp is complete.** `volumetric_fog_begin_frame` keeps the card policy in
   warm-up while `ready_far < 1` (the ramping medium stacks on the native cards), and the card
   predicate needs `ready_far == 1`. Bridge: `stored_ramp_stacks_medium_on_native_cards` (≥ 80 ramp
   frames, each native-source-exact with march + composite + repair) and
   `cards_never_masked_below_full_far_ramp` over the whole run; the fragment's host double asserts the
   same at ready 0 / .25 / .99 / 1 / back to .5.
4. **Load detection.** A sample gap invalidates only when it also spans more than 500 ms of wall clock
   (`fog_density_gap_ms`); no existing load signal reaches this route (the latch's `cut` is diagnostic).
   Bridge: `one_frame_gap_keeps_cache`, `one_frame_gap_resumes_bit_identical` (no new first fill, no
   ramp), and the long gap (3 frames + 600 ms) still invalidates in the same frame and ramps. Host
   double: a missed sample without time and a slow frame without a missed sample keep the cache.
5. / 6. **Family chroma is a tracked table, not a runtime scan.** `src/renderer/fog_family_chroma_inc.h`
   (14 rows, generated by `tools/build/fog_family_chroma.py` from the hash-pinned packets with the
   stored-density screen's definition) replaces the 2 M-texel scan, so nothing of it runs on any
   thread. `test_tracked_family_chroma_matches_the_packets` recomputes all 14 from a fresh bake with
   its own run decoder and atlas-interior sum: float32-identical; bluewell .0516 / .2695 / 1.0 as logged
   by the bridge. A profile without a row logs one `event=refused reason=family_constants
   fallback=legacy` line instead of staying silently inert.
   Prepare cost, now split: resource-creating first call **3.4 ms** (three programs, four textures, two
   caches, thread); steady median 1.5 µs / p99 24 µs (181 frames); upload frames median 24.6 µs / p99
   115 µs, maximum **2.8 ms**, the only one of 4,037 above 1 ms, 24 rectangles at frame 2,581, the first
   upload after the pass Reset. The earlier 5.6 ms maximum was never isolated; the split
   does not show whether the scan was its cause.
7. **Accepted costs (reviewer-confirmed).** Each 250 ms give-up leaks ≈ 8.5 MiB CPU, the thread and
   the module pin. In stored mode the legacy family atlas stays decoded (4.26 MB CPU per the review)
   and resident (≈ 17.8 MB VRAM) although never sampled. Proposed, not implemented: release it in
   stored mode and re-create it on a refusal or a switch back.

Evidence. Host: the five modules above plus `verification.analysis.test_fog_field_assets`, 35 tests OK.
Wine (bottle X3, arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), same command, output
`/tmp/x3-fog-density-route-v4`, lock wait 0.000003 s, child 76.4 s, `check` PASS, tracked summary
rewritten: baseline bridge built from main `85c7c821` (`e3923231da60be01…`) 515 checks; route bridge
(`47839d34bacddb77…`) **26,692 checks**, five legacy image hashes equal to the baseline, 22
`volumetric_fog_cache` lines; exit fixture (DLL `a0a18c92132f874b…`, exe `ab7e4fc93185de64…`) 4 checks,
idle and mid-fill exit 2 ms after `ready` with the DllMain marks before the static destructor, idle
control hangs. First fog 866–902 ms, fine 1,923–1,949 ms (ramps included); Reset 8,520,192 B re-uploaded,
0 nodes regenerated; release with a generating worker joined in 8.9 ms, refcount 13 → 13; abandon +
`~FogPass` 1.03 ms. `/tmp/x3-fog-density-route-v3` passed the same witnesses (25,774 checks) before the
cost split was added to the fixture; `/tmp/x3-fog-density-route-v3-fixture-bug` is a fixture-side
failure (the witness compared node totals while the worker was still growing its window; it now
compares `first_fills`).

Scratch `d3d9` links after the merge (DLL `cc92c8cc0b9ea486…`). `check_no_x87.py`: **FAIL, roots 95,
reachable 548, 2 violating functions, both from main's merged option and outside this change**:
`MotionOutput::unmatched_static_rows` (`fstpl`) and `renderer::static_previous_rows` (`fabs` on the x87
stack). Before the merge the same audit passed (95 / 544 / 0). Not fixed here.

### Run214 (Run60 install, session B) read-only triage (2026-09-21)

Read-only triage of `/tmp/x3-bottleX3-run214` (session log
`session-20260921-201359-212.log`, 292 MB, no full read: queried with grep/python).
Config confirmed by log line 17-18: `volumetric_fog_range mode=stored atlas_bytes=4260096
levels=2 ... ramp_frames=90 worker_threads=1` and `volumetric_fog_mode ... strength=0.03
density_scale=1.5 ... cards=replace`. No `D3DERR_*`, no unhandled-exception/crash text, one
benign `error=203` (telemetry span code); log ends on a normal `frame_end`/`mip_bias_summary`
pair at frame 29712 — clean quit.

Two F8 bursts dumped: frames 2815-2822 (elapsed ~62.3-65.7 s, `bluewell` profile) and
24630-24637 (elapsed ~324.0-327.0 s, `foggreenoutlands`, sector index 14 — per the existing
note above this is a fog-family tag, not a confirmed "The Hole" sector name). Shaft term: the
march inlines `fog_visibility` once (per the runtime-integration architecture note) but
`fog_pass.cpp` only declares `fog_cascade_max=3` and binds `shadow_maps[0..2]`, while this run's
`X3M_SHADOW_CASCADES` configures 5 cascades — cascades 4-5 (37.5 km-150 km) are never reachable
by the fog march. No per-frame `cascades_bound` log line exists in this build, so whether any
cascade was actually bound for these frames is not verifiable from this log; that is a real
diagnostic gap.

Black smear (`screenshots/fog-smear.png`, mtime aligned with burst 1): the diagonal dark streak
below the station is already present, at constant intensity (0.0950 -> 0.0943 mean luminance,
<2% drift, no growth) across all 8 frames of burst 1, in `hdr_1_2815..2822.rgba32f`-paired
`hdr_1_*.rgba16f` (the post-fog-composite, pre-tonemap linear HDR readback at
`motion_output.cpp:6146`). No RGBA16F NaN/Inf/negative values in any of the 16 dumped HDR frames.
Depth channel B is the `-1` invalid/sky sentinel in both the streak crop (y330:420,x900:1050,
mean lum 0.0950) and the adjacent clear-fog crop (y330:420,x1150:1280, mean lum 0.1359) — per the
documented march rule ("invalid depth keeps identity") neither pixel group receives fog, so the
luminance gap is inherited from the pre-fog station render, not from the fog composite, fog
history/reprojection, or TAA (no TAA dump was captured this run to rule TAA in or out directly).

Distant-station flicker (burst 2, frames 24630-24637): station crop (y220:300,x560:680)
mean luminance ranges 0.09140-0.09575 across the 8 static frames (<5% spread), consistent with
normal jitter noise rather than a strong per-frame alternation; depth.b=-1 there too (far-LOD
billboard, not depth-tested geometry). Zero `motion_unmatched_static` lines occur in or near this
window (last occurrence at log line 944367, well before frame 24630's line 1165863). No
`far_stabiliser` text appears anywhere in the 292 MB log — `X3M_TAA_FAR_STABILISER` activity is
configured but not instrumented, so its per-frame gating for these frames cannot be checked from
existing evidence; a targeted diagnostic build would need to log the far-stabiliser blend weight
and the unmatched-static gate per frame during a panning capture to settle question C.

Blending seams (LOD/tile-border/banding): not established this run — no dedicated near/far-grid
boundary capture exists in this dump set; the reviewed crops did not show an isolated hard edge,
but this is inconclusive rather than a clean pass.

## Run 60 session B diagnosis (run214): the smear is a shadow shaft; far stations are unrouted (2026-09-21)

Diagnosis only, no production edit, no Wine. Evidence `/tmp/x3-bottleX3-run214` (local), numbers in
`verification/results/fog-run214-diagnosis.json`. [M] measured, [I] inferred. This corrects two
statements of the triage entry above.

**Corrections to the triage.** (1) The lane depth dump is four-channel: `.r` is the class/device
depth (`-1` sky sentinel), `.b` the view z. `march_depth` (`src/fog/fog_density_field_inc.h:96-101`)
keeps identity only for *geometry with an invalid view z*; a sky pixel marches the full 200 000
horizon and is composited. "depth = -1, so no fog is applied" is wrong: the streak pixels are sky
and are fogged. (2) `hdr_1_*` is `hdr_->target()` read in `hdr_writeback`
(`src/proxy/motion_output.cpp:6145`), after `run_volumetric_fog()` (`:1895`/`:1807`) and after the
TAA resolve on the same target, before tonemap. No pre-fog stage was dumped; the first (only) stage
already shows the streak. (3) The fog binds apply slots 1..3, not 0..2: slot 0 (own ship) is skipped
(`src/proxy/motion_output_fog_inc.h:62-64`), log `shadow_maps=3` on all 62 applied frames. With
extents 250/1500/7500/37500/150000 the march samples the 1500, 7500 and 37 500 maps; only the
150 000 cascade is unreachable.

**Symptom 1, black smear: hypothesis (e), a real shadow shaft, no ambient floor.** [M] Frame 2815: the
station is routed geometry (20 039 px, view z 18 043-39 165, median 20 514), inside the 37 500
cascade. A contrast-stretched 8 px box of the HDR luminance shows not one streak but a fan of
parallel dark bands, each starting at a station part (both rings, the truss, the main body) and
running down-left. Sun (log `shadow_replay_sun_point dir0`) = (0.6448, 0.7604, -0.0773) world;
view-space (row-vector, `s*V`) = (0.808, 0.575, -0.129). Shadow rays `P - t*sun` from three station
pixels, projected with p00 = 0.8, p11 = 1.3333, overlay the bands along their whole length;
predicted screen orientation 143.9 deg, structure-tensor orientation of the band region
(x560:800, y300:450) 141.0 deg. Band luminance 0.095 against 0.136 adjacent sky (-30 %), constant
over 8 frames, as a static caster and sun require. [I] It reads as a black smear rather than a
shaft because `lit += T*a*visibility` (`fog_density_field_inc.h:127`) has no ambient or
multiple-scatter term, so a shadowed segment contributes zero inscatter while still extinguishing;
PCF on a 73 unit/texel map with a point sun gives a hard-edged umbra tens of km long with no
penumbra widening; and at this density most sky luminance is inscatter, so its removal is large.
Rejected: (a) the bands follow the sun, not card quads, and `cards=1`/`result=0` on applied frames;
(b) the station is valid geometry here and the dark pixels are sky; (c) bands are tens of px wide
and hundreds long, not an edge-width upsample error; (d) no drift over the burst (0.0950-0.0943).
Remedies to evaluate (not done): an ambient/shadow floor on `visibility` (for example
`lerp(floor, 1, visibility)`), a distance fade of shaft strength, or a penumbra that widens with
caster distance.

**Symptom 2, distant stations flicker on vertical pan.** Burst 2 is not static: [M] `camera_state
rotation_deg` 0.25-4.9 deg/frame, image shift -9,-9,-5,-3,+3,+26 px/frame vertical. [M] The two
distant stations carry no lane depth (0 geometry px in y220:300 x560:680) and no motion: frame 24630
has 190 draws, 6 routed (own ship); station draws are blended `zwrite=0` and refused
`no_zwrite` (26) or `overlay_node` (62). [M] Tracked over the pan, the station crop shows single
pixel bright/dark speckle that changes every frame (frame-to-frame normalised correlation 0.30-0.58
against 0.74-0.93 for star fields in the same frames), while its median stays 0.0908-0.0914
(<0.7 %). Measured shifts of both stations and both star fields agree with the scene-camera
far-plane prediction within 1 px, so mis-reprojection is not the cause at that resolution.
Ranking:
1. Unrouted sentinel station, plain resolve [M+I]. `farWeight(sentinel) = 0`
   (`src/temporal/line_mask_ps.hlsl:21`) and `classChange` needs a valid depth on one side (`:20`),
   so these pixels get neither the far stabiliser nor the thin mask: history is clamped to the 3x3
   box every frame (`src/temporal/resolve.hlsl:462-464`) and sub-pixel hull detail aliases. Vertical
   pans show it most where the detail is horizontal (panel rows, trusses) [I].
2. Far-stabiliser speed gate [I]. Even for routed far geometry the 0.985 weight fades to the base
   weight between 0.03 and 0.25 px/frame (`resolve.hlsl:486`, defaults `src/proxy/capture.cpp:112`);
   any pan switches it off, so detail that is calm at rest flickers exactly while panning.
3. Camera gate closed by routed sentinel glass (taa-lattice-crawl.md section 32.5): applies to
   nearer, routed stations only; not to the stations of this burst (unrouted).
4. Fog on sentinel station pixels: marched as sky to 200 000 regardless of true distance (a veil
   error, not a flicker): crop median moves <0.7 % over the pan; sky-class half-res weights are
   plain bilinear. Low.
5. LOD taper / `unmatched_static` (0 lines in the window) / background-view deviation
   (`background_rotation_deg` median 0.16 deg while turning, but shifts match the scene camera): low.
Settling capture, no new build needed: the same flags plus `--taa-debug`, one F8 burst during a
slow (2-5 px/frame) vertical pan on a distant station, then switch fog off with the runtime fog toggle and repeat. Speckle
already in `color_*` (pre-resolve) means source aliasing of an unstabilised sentinel object (1);
speckle only in `taa_*` with `taa_age` pinned near 1 means clip/gate (2/3); disappearance with fog
off would implicate (4). If a build is made anyway, add the line-mask target to the debug dumps.

**Cascade gap.** `fog_cascade_max = 3` (`src/renderer/fog_pass.h:42`) loses only the 150 000 cascade:
march samples farther than about 37.5 km (times the select margin) from the cascade-3 centre get
`shadow_weight = 0`, visibility 1, so casters beyond that range throw no shafts and fog between
37.5 km and the 200 km horizon is always lit. [I] Minor: that map is 293 units/texel, it is the one
cascade replayed on alternate frames under budget (`far_replayed=0` on 16 frames here), and
`fog_shadow_current` would drop it on those frames, so binding it would add shaft flicker for little
visible gain. Leave at 3; the triage's "cascades 4-5" is wrong.

## Stored-density look presets L0-L3: shaders, pass, launcher, hotkey (2026-09-21)

Design and constants: `docs/architecture/fog-density-runtime-integration.md`, "Look presets". Not installed, not
flown; appearance is unjudged. [M] measured in bottle X3 unless marked.

- **L0 unchanged.** The shared include changed only behind `FOG_LOOK`; regenerating the four base programs left
  their headers byte-identical (no working-tree change; march bytecode `4dacf7e4...` pinned in
  `test_fog_density_shaders.py`). Base gates of `fog_density_shader_run.py` unchanged: candidate T max 1.5e-4,
  S max 5.6e-5 against the host.
- **Slots / fetches** (Microsoft table, `fog_density_shader_slots.py`): march L1 344/13, L2-3 418/15; repair L1
  445/18, L2-3 506/20; composite 210/10; every march and repair keeps exactly one `rep` loop. The device
  reports `max_ps30_instruction_slots=512` (fixture CAPS line): a first version at 641 slots could not have
  been created here, which forced the two-cascade shaft read and the tent `level_sample`.
- **GPU versus host** (`look_march` in `tools/analysis/fog_density_shader_reference.py`, 576 stratified rays per
  case, gates p99 .002 / max .003): eight look cases (A/B sky for L1-L3, A depth 149999 for L2, A fully shadowed
  L1): worst |dT| 3.3e-4 (FP32 target) / 6.2e-4 (RGBA16F), worst |dS| 1.8e-4 / 3.6e-4; L3 leaves out 2 of 576
  pixels whose noise argument is within 2e-3 of a `frac` wrap. Reference min T .32-.48 at strength .03 (L0 mean T
  .82 on the same poses), 45-69 % of sky rays exactly empty.
- **Black shaft** (run214): fully shadowed sky under L0 is S = 0 exactly on 9216 fogged pixels
  (`look0_fully_shadowed_fog_is_black`); under L1 every fogged pixel has S > 0 in all channels
  (`look1_fully_shadowed_fog_is_coloured`, min 1.4e-7 FP32), and the host test shows a greener hue than the lit
  sample. The production pass repeats it on its RGBA16F target for T < .98 (970 pixels); nearer 1 the in-scatter
  is below the smallest normal half and this backend flushes it to zero.
- **Production pass** (`fog_density_pass_fixture`, 58 checks, 1155 hostile-state restorations): look programs
  created with the base programs; L0 after cycling byte-identical to before; L1/L2/L3 differ, only L3 depends on
  the frame phase; switching creates and allocates nothing (references and allocations equal); L2 after a
  mid-fill Reset byte-identical to before; composite/repair consistency repeated with the FOG_LOOK 2 programs
  and `T^k` (worst 4.9e-4).
- **Legacy path** (`fog_spatial_build.py` / `fog_spatial_run.py`, this `fog_pass.cpp`): numeric 40/40 variant
  checks pass, state report `passed`.
- **Host**: `test_fog_look_reference.py` (header constants equal the Python mirror for all four presets; law
  properties; every tunable's range), `test_fog_density_shaders.py`, `test_volumetric_fog.py` (launcher
  `--volumetric-fog-look`), `test_comparison_hotkeys.py` (Ctrl+Alt+F11 disjoint from Ctrl+Shift+F11),
  `test_fog_cards.py` (MotionOutput double). Scratch RelWithDebInfo DLL: `check_no_x87.py` PASS, 547 reachable
  functions, no violations.
- **Not measured**: GPU time. The fixture's slope timing reads 0.006-0.012 ms per 640x384 march for every
  preset, i.e. the backend defers the work past the readback; use `--volumetric-fog-timing` in flight.
  Native Windows: cross-compiled only.
- **Open**: look programs switch cascades hard (no cross-fade) and ignore the finest map; one sun-ward tap
  instead of two; L3 offsets all 64 bins (the review suggested starting with the near 24:
  `X3M_FOG_LOOK_JITTER_FAR=0`); shimmer under TAA untested; edge erosion and a generator-side recipe for
  sub-400 m structure not done (reasons in the architecture note).

### Look presets, review fixes on merged main e8e97da0 (2026-09-21)

- The column cap (70000) now ends every ray, sky and geometry, so a distant hull and the sky beside it agree
  (host test: equal S and T). Slots after the change: march L1 341/13, L2-3 415/15; repair L1 442/18, L2-3 503/20.
- Two-cascade cross-fade (.85 to .95) compiled and measured: repair L2 546 slots, does not fit 512; the hard
  switch stays (open).
- TAA off or failed holds the L3 offset phase at 0. C++ creation gate is `slots < 512`, as the test.
  Launcher help states `--volumetric-fog-anisotropy` has no effect under looks 1-3.
- All shader manifests regenerated and `generate_rigid_motion_pixel.py --check` PASS under the lock; only the
  four look march/repair headers changed. Fog shader + pass fixtures rerun: PASS, nine look cases (added L1
  geometry at 45000 units, 315 fogged rays), worst |dT| 6.2e-4 and |dS| 3.6e-4 on RGBA16F; pass fixture 58 checks,
  1173 state restorations. Host: 113 tests OK (fog, TAA, shader provenance, hotkeys, launcher). Scratch DLL
  rebuilt: `check_no_x87.py` PASS, 547 functions, no violations.
- One fixture run hung in the shader fixture while a parallel `cmake -j` build loaded the machine (process ended
  by hand, rerun on an idle machine passed in 54 s); not reproduced, cause unknown.


## Run 61 session B diagnosis (run220): ribs are the coverage-variation term; L3 = L2 is expected (2026-09-22)

Flight `/tmp/x3-bottleX3-run220` (Run61 DLL `0bc8ff36`, commit `ed105485`, bluewell then foggreenoutlands, 48 654 applied fog
frames, 82 preset switches). Compact record: `verification/results/fog-run220-diagnosis.json`. No production edit, Wine or build.

Evidence limits. The F8 burst dumps post-fog HDR, depth, motion and shadow maps; **no fog target is dumped**, and the log carries
neither camera world position nor the inverse view rows, so the flight pose could not be replayed and optical-depth statistics
come from the host reference, not the flight. The three bursts are 3685-3692 (L1), 4477-4484 (L2) and 13583-13590 (L2), all at
0.50x: **there is no L3 burst**. `--volumetric-fog-timing` records CPU wall time of the pass (`cpu_us`), not GPU time.

**1. Ribs (measured in the host reference, consistent with screenshots).** `fog2.png`/`fog3.png`: 6-8 parallel, evenly spaced
bands (about 45-50 px at 1280x768) inside one patch, straight, fading with the patch, not concentric around the camera and not
radiating from the sun (vertical in `fog2`, diagonal in `fog3`). In the L1 and L2 bursts the band profile is identical between
frame 0 and frame 7 (correlation .999 at zero shift): static, not a per-frame sampling artefact. Cause: the coverage variation
`cover = look_edge.x * wave1.x * wave2.y` (`src/fog/fog_density_field_inc.h:235`). `wave1.x` is a function of world Y only (5
cycles per 65536 = 13107 units) and `wave2.y` of world X only (13 cycles = 5041 units, 1.0 km), so the term is a set of infinite
slabs `x = const`, constant along Z, moving the coverage by +-.12 on a remap whose visible range starts at .35 with exponent 2
(at stored rho .5 the remapped density swings .002 to .17) and sigma x8. Reproduction (scratch render, the reference's
`look_march` law on a dense store of the screen's field, 320x192, tan half-vfov .75, 1.0x): looking along world Z the slabs
appear exactly as in `fog3.png`; 6-px high-pass RMS of 1-T: L2 .00707, coverage variation off .00240, warp off (variation on)
.00713, both off .00219, L3 .00707. Looking along X (slabs face-on) all variants agree (.0010-.0012). So (c)/coverage term is the
cause; the warp itself, slice lerp (b), bin banding (a), lattice (d), self-shadow (e), FP16 (f) and upsample (g) are not: the
reference has exact trilinear lookups, no half-resolution pass and float64 arithmetic and still shows the ribs, and removing the
one term removes them. 48 px at 5041 units puts the ribbed patch near 54 000 units (10.7 km), inside the 70 000 cap (inferred).

**2. L3 = L2.** `look_phase` is `counters_.jitter_index` when jitter and TAA are live (`src/proxy/motion_output_fog_inc.h:422`);
the log shows `jitter=1 jitter_index=1 taa=1` on burst frames and `JITTER_NEAR=1 JITTER_FAR=1` resolved, L3 selected 20 561 frames
(code + log; not measured in a dump, no L3 burst). Reference: one L3 frame differs from L2 by mean 7e-5 / max .0015 in T, the
8-phase mean by max .0009: the 24+40 bins already resolve this band-limited field, so the offset has nothing to remove and an
identical picture is the expected result. The ribs are not depth-slice banding, which is why L3 does not touch them.

**3. Distribution and fade.** LOD blend 20 000-30 000 units (4-6 km) in all presets: as designed. Taper: L0 150 000-200 000
(30-40 km); **L1-L3 end every ray at `SKY_CAP` 70 000 with the taper over 52 500-70 000 (10.5-14 km)**, by design of the look
presets but short of the 30-40 km the plan quotes: with L1-L3 no fog beyond 14 km is drawn. Readiness: far ramp frames 900-989,
both levels 1.0 at frame 1040, 1/90 per frame, re-ramped after each sector change (28 217, 43 966); 226 `density_filling` frames.
Reference (diagonal pose, 1.0x): 46 % of sky rays exactly empty; 1-T mean .037, p50 .0005, p99 .215, max .26 for L1 = L2 = L3
(the presets differ in light, not extinction); .019/.114 at 0.5x, .053/.304 at 1.5x. L2 sun-lit sum is .56 of L1 (self-shadow +
powder). Phase forward/back 37:1, 30 deg / 150 deg 4.9:1; ambient sun-side (.018,.094,.350) vs away (.110,.034,.133).
`shadow_maps=2` on applied frames; shaft presence was not isolated in this flight. Per preset (applied, non-capture frames; scenes
differ, frame time is 1 ms quantised): cpu_us mean/p95 L0 534/750, L1 552/760, L2 554/763, L3 476/716; frame dt mean 12.0, 11.5,
11.7, 10.2 ms (84-98 FPS), p95 16 ms each. No preset cost is visible at this resolution of measurement; GPU time is not logged.

**4. Strength 1.5.** The code default is already 1.0x: `fog_strength_default = .02f` (`src/renderer/fog_pass_math.h:11`),
launcher `const=0.02` (`tools/manage.py:381`), overlay scale `strength/.02`. 1.50x came from the published run command
`--volumetric-fog 0.03` in `docs/verification/user-runs.md:122` (log: `X3M_VOLUMETRIC_FOG_STRENGTH=0.03`, `density_scale=1.5`).
Change that argument to `0.02` (or drop the value).

**5. Fix.** Cheapest: `coverage_variation = 0` at `src/renderer/fog_look_math.h:18`. No shader change, slot budgets unchanged
(repair L2 503, march L2 415); the user can confirm today with `X3M_FOG_LOOK_COVERAGE_VARIATION=0` on the installed DLL. Cost:
edges are again one iso-surface of the warped noise (the warp stays and did not rib in the reference). If edge variation is
wanted back, it must not be a product of single-axis waves: drive it from a stored sample (for L2 the sun-ward far tap is already
fetched) rather than from `look_wave`; that needs a slot count before it is promised.

## 2026-09-22 — Run220 sun-hidden-behind-station triage (read-only)

User report: sun disappears fully once a station covers roughly half its disc, wants it to stay
visible (and light shafts) until fully covered. Triaged `/tmp/x3-bottleX3-run220` (Run61
0bc8ff36 install, HDR+TAA+sun shadow lane+stored fog, 3 F8 bursts of 8 frames:
3685-3692, 4477-4484, 13583-13590 — only 3 exist, not the "4th" the user recalled).

HDR readback (`hdr_1_<frame>.rgba16f`, 1280x768): bursts 1-2 show hazy sky, no sun disc/glow above
background. Burst 3 (13583-13590) shows a diffuse fog glow centred behind a near station but no
distinct disc/corona pixels; glow-patch mean luminance is flat (~0.39) and max rises only
0.609→0.650 monotonically over the 8 frames — no full/half/gone step is visible in this capture,
i.e. the flare chain appears to stay hidden for the whole window. The session log has zero hits for
`occlusion`/`CreateQuery`/`IDirect3DQuery9`, and zero for flare/corona/disc/halo keywords: the proxy
does not instrument this path at all, so no shader hash, blend state or draw count is attributable to
the sun draws from this log.

Root cause is documented, not new: `docs/reverse-engineering/lens-flare-visibility.md` — vanilla CPU
mesh-collision probe `0x00488720` (single caller `0x004715d0`/`0x00471630`) sets `record+0x30` to 0/1
per sun/lens record as **one boolean for the whole chain** (disc+corona+flares together); any single
hit of the swept probe volume against a candidate occluder hides the whole record, then
`0x00471660` ramps `record+0x10` ±100 (clamped 0..200) into a two-frame full→half→gone size step —
not a per-pixel or coverage-fraction fade. No mod code (depth handling, sun shadow lane, HDR/bloom
clamp, fog replace cards) is in this path; no route refusal/suppression lines reference it.

Open: need a dedicated diagnostic to catch the actual transition — one F8 burst timed to straddle the
probe's boolean flip, logging `record+0x30`, `record+0x10/+0x34` and the lens-scene draw count per
frame (or a debug print at `0x00488720`'s return and `0x00471660`), since none of Run220's 3 bursts
caught the disc mid-visible or mid-ramp.

Evidence: `/Users/asvetl/x3-mod/verification/results/run220-sun-occlusion-triage.json`.

## Rib fix, 22 km fade, look 2 default (2026-09-22)

Source: `src/fog/fog_density_field_inc.h` (FOG_LOOK), `src/renderer/fog_look_math.h`, host twin
`tools/analysis/fog_density_shader_reference.py`. Host evaluation is a scratch render (not tracked): the reference's look law,
T only, on a dense float16 store of the screen's field around pose A's origin, 320x192, tan half-vfov .75, 1.0x
(sigma = screen sigma / 1.5, x8), sky rays, views along world Z, X, Y and the pose-A diagonal (D).

**1. Coverage variation.** `cover = (.12/3) x sum of three parabolic-sine plane waves` with wave vectors (1,-2,1), (2,1,-1),
(-1,1,2) cycles per 65536 units (whole cycles, so world anchored under the camera modulo; 26756 units = 5.35 km each at 5000
units per km, about two repeats inside the 13 km full-density range), evaluated at the warped position; no fetch. A
stored-density driver was not used: the coverage is needed before the sample's own remap and L1 has no spare tap; an extra
`level_sample` does not fit repair L2, and neither does a second wave scale (repair L2 is at 510 of 512). Metrics against
variation off in the same view, 240x144, default fade: stripe score = largest normalised autocorrelation of the 6-px
high-passed 1-T at lags 12-90 px (periodic ribs score high); HP = high-pass RMS ratio; edge = pixels whose `1-T > .01`
footprint differs from variation off, per footprint-perimeter pixel. Views: world Z, X, Y, the pose-A diagonal D, along each
wave vector (k0-k2) and along each pairwise cross product (x0-x2), where a plane-wave lattice would line up.

| Set | Wavelength | Worst stripe excess over off (view) | Stripe range | HP ratio | Edge px (views with an edge) |
| --- | --- | --- | --- | --- | --- |
| first attempt (4,-2,3),(-2,5,3),(3,3,-5) | 2.0-2.4 km (wrongly recorded as 10-12 km) | +.124 (x1: .232 vs .108) | .104-.232 | 1.14-1.43 | .84-1.77 |
| **(1,-2,1),(2,1,-1),(-1,1,2)** | 5.35 km | **+.025** (Y .109 vs .089) | .107-.185 | 1.02-1.12 | .71-2.27 |
| (2,1,-2),(-1,2,1),(1,-1,2) | 4.4-5.4 km | +.047 | .098-.228 | .98-1.14 | .81-3.29 |
| (1,2,-2),(2,-1,1),(-2,1,2) | 4.4-5.4 km | +.030 | .090-.185 | .97-1.08 | .89-2.35 |
| (1,-1,2),(2,1,-1),(-1,2,1) | 5.35 km | +.049 | .089-.227 | .96-1.12 | .66-3.12 |

Chosen set per view, stripe new/off: Z .126/.153, X .156/.139, Y .109/.089, D .164/.139, k0 .185/.181, x0 .112/.115,
k1 .147/.146, x1 .170/.187, k2 .132/.142, x2 .107/.111. Old product law for scale (320x192): stripe .43 (Z), .83 (Y), HP 2.5-2.9.
The chosen set adds almost no high-pass energy (it moves edges at the 5 km scale rather than texturing them) and its stripe
score is within .025 of variation off in all ten views. Limit: this reshapes patch outlines at kilometre scale; it does not add
sub-kilometre edge detail (that remains the generator-side recipe noted in the architecture note).
Slots / static texture instructions, one `rep` loop each: march L1 349/13, L2-3 422/15; repair L1 450/18, L2-3 510/20
(before: 341, 415, 442, 503); composite look 210/10 unchanged. L0 march, repair, composite and the exact variant recompiled
byte-identical (headers unchanged; only the include hash in their records moved).

**2. Fade range** (orchestrator decision: no 30-40 km default). Default `TAPER_START` 65000, `SKY_CAP` 112500: full density to
13 km, smoothstep to nothing at 22.5 km, all rays. Both are `X3M_FOG_LOOK_*` overrides (14 km: 52500/70000; 35 km:
150000/200000). Bin layout kept at 24 + 40. Sky-ray 1-T (160x96, measured with the first-attempt coverage set; the range conclusion does not depend on it) and the 64-bin march against a
384+640-bin march, plus the change when every sample moves half a bin (`offset .25` vs `.75`, the coarse-step shimmer proxy):

| View | Range | mean | p50 | p99 | exactly empty | err p99 / max | half-bin p99 / max |
| --- | --- | --- | --- | --- | --- | --- | --- |
| D | 14 km | .082 | .0001 | .478 | .50 | .0006 / .0014 | .0012 / .0023 |
| D | 22 km (default) | .097 | .0003 | .508 | .49 | .0027 / .0055 | .0051 / .0102 |
| D | 35 km | .167 | .103 | .592 | .23 | .0194 / .0390 | .0332 / .0587 |
| Z | 14 / 22 / 35 km | .078 / .147 / .183 | 0 / 0 / .0035 | .592 / .787 / .862 | .61 / .53 / .45 | max .0021 / .0052 / .0337 | max .0035 / .0112 / .0526 |
| X | 14 / 22 / 35 km | .023 / .044 / .111 | 0 / 0 / .007 | .318 / .571 / .756 | .76 / .74 / .39 | max .0011 / .0044 / .0291 | max .0023 / .0080 / .0480 |

The 35 km range veils the sky (D median .10, empty share .50 to .23) and its 4700-unit far bins under-sample the x8 medium
(half-bin change up to .059 in T: visible shimmer under forward motion for L1/L2); the 22 km default keeps the empty share and
the median of the 14 km law and stays near 1 % worst case. Bin splits at the default: 20+44 err max .0041-.0047, half-bin max
.0087-.0092; 16+48 err max .0032-.0036, half-bin .0061-.0106: under a third better at best and the near bins grow to 750 units
(past the 512-unit fine node), so the layout was not changed. Camera-inside-fog poses were not part of this measurement.

**3. Defaults.** Stored range starts on look 2: launcher (`X3M_VOLUMETRIC_FOG_LOOK` = 2 unless `--volumetric-fog-look` is given;
legacy range stays 0) and the DLL fallback (`renderer::fog_look_default`, absent or malformed variable). Overlay 1.0x is
`--volumetric-fog 0.02` or the bare flag (`const=0.02`, overlay = strength / .02); 0.03 gives 1.50x.

**4. Fixture** (bottle X3, arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`): `fog_density_shader_run.py build/run/check` PASS,
all 16 gates, pass fixture 58 checks, none failed, run 43.8 s + 27.5 s. GPU versus host `look_march` over the nine look cases
(576 rays each, 221-295 fogged; the taper-depth case is now 90000 units): T max .00036 (FP32) / .00074 (RGBA16F), S max
.00037 / .00060, gates .003. Generator `--check` PASS for the nine fog programs. Summary:
`verification/results/fog-density-shader/summary.json`.

## Run 62 fog flight diagnosis (run222): the L2 smear is one off-screen station's shaft, stamped per bin (2026-09-22)

Diagnosis only: no production edit, no Wine, no build. Evidence `/tmp/x3-bottleX3-run222` (local), `screenshots/fog4.png`,
numbers in `verification/results/fog-run222-diagnosis.json`. [M] measured, [I] inferred. Method: a CPU re-march of the look
bin law (24+40 bins, cap 112500, taper 65000, shaft floor .15, two-cascade hard switch at .85, the shader's 2x2 comparison)
over the dumped `shadow_map2/3` with the logged `camera_state` and `shadow_replay_map_basis`, 320x192 rays, **uniform density**
(the stored field was not reconstructed, so modelled contrast is a proxy; geometry and sampling are exact). Scratch scripts
were not committed.

**Bursts and dumps.** [M] Five bursts: 6756, 7910, 9152, 9539 are L2 (the first `volumetric_fog_look` switch is frame 21487),
31040 is L3. Each has `hdr_1` (post fog + TAA, pre tonemap), `depth_1`, `motion_1` and `shadow_map0..4`; no fog T/S target
and no pre-fog colour. All burst heads: `applied=1 reason=ok shadow_maps=2 sun=tracked`, sun (.6433, .7612, -.0819). The maps
are **2048^2**, so the fog reads 7.3 units/texel (7500) and 36.6 (37 500), not 73.

**1. What the smear is.** [M] 9539 is the fog4.png pose; 9152 shows no smear. In 9539 maps 2 and 3 hold one long station
(28 caster records, 97 855 primitives, cascade mask 31; a spine with 3+3 towers, about 22 500 x 7200 units in light space,
30 264 occluder texels in map 3) at 16 069 units, view (15796, 2794, 942): **3.2 km to the right of the camera and off
screen**. Its shadow slab sweeps across the view in front of the camera. The visible far station (48 560 units) is in cascade
4 only, which the fog does not bind: it throws no shaft and is not the caster. The re-march reproduces the picture: a dark
core line at -10.4 deg (fog4.png about -9.9 deg) converging on the anti-solar point at the left, and above it the comb.
**The comb is the station's tower silhouette stamped once per 2512-unit far bin**: with bin centres each bin contributes one
point sample of the shadow, so every bin projects its own copy of the silhouette. Along lines 40-48 px above the core the
detrended HDR profile correlates .65-.68 with the bin-centre model and .02-.14 with the dense (16 sub-samples per bin)
model; tooth amplitude 2.2-2.4 % rms in the HDR against 2.7-3.2 % modelled. Bin centres against dense: rms .0102, p99 .040,
max .097 of lit in-scatter; the mean of 8 L3 phases: rms .0023, p99 .009, max .024, which is the user's "much reduced".
HDR core/side luminance in 9539: median .854, min .785, 16 px FWHM.

**Why it appears and disappears.** [M] Not the sampling: 9152 was taken 41 units from the 9539 camera position with another
heading, and there the station has **no caster record at all** (map 3: 41 occluder texels, a ship; map 2: 1048). The engine
did not submit the station, so it left every shadow map. Shadow-caster retention was on (`mode=live`) but held nothing:
`static=0` at both bursts (15 nodes `moving` at 9539), and over the session `static=0` on 15 577 of 30 035 frames. So the
shaft pops with engine submission, which depends on heading and position. [M] Secondary: moving the camera half a far bin
(1256 units) decorrelates the comb completely (synthetic box, error-pattern correlation -.04 while the dense images
correlate .94), so under way the teeth also crawl. Rejected [M]: cascade boundary or alternate-frame update
(`far_replayed=1`, cascade 4 unbound, the caster sits inside both bound maps); repair/half-res disagreement and sentinel
depth (the smear pixels are plain sky, tens of px wide); history (static over the 8 frames). [M] Coverage: 70 % of the
in-scatter weight of a sky ray (min 49 %) lies inside the two bound cascades; beyond .85 x 37 500 lateral units fog is
always lit, so shafts end there with a hard edge.

**2. Host reproduction.** [M] Synthetic box (spine 22 000 x 600 plus three 600 x 3000 towers, 13 300 units sun-ward, the 9539
camera and sun, analytic visibility): bin centres give the same comb (three towers become a dozen teeth; rms .0058, max
.075 against dense), the 8-phase mean removes it (rms .0013, max .016), and both dense and L3 keep **one crisp dark core
line**, the spine seen edge-on from inside its own shadow plane (min .18 of lit with the .15 floor). `look_march` in
`tools/analysis/fog_density_shader_reference.py` takes only a scalar `shadowed`; the mirror above reuses its `look_noise`.
A per-sample visibility callback there is the natural host twin for the fix below (not done).

**3. The L3 "black lines".** [I] Correct geometry, not an artefact: the dense reference of 31040 shows a fan of thin shafts
from the station's spars converging on the anti-solar point, and the L3 mean matches it (rms .0022). No stair-steps, acne
or seam in the model at 36.6 units/texel. They read as lines rather than shafts because the sun is a point (no penumbra),
the comparison is one 2x2 tap (about one screen pixel at 20 000 units) and the umbra keeps full contrast over tens of km.
[M] In the 31040 HDR they are weak: core/side median .996, min .909; the 9539 pose is the strong case (.785). What would
soften them, in order: penumbra that widens with receiver-to-occluder distance (the four fetched depths already give the
blocker distance); a higher floor (`X3M_FOG_LOOK_SHADOW_FLOOR`, now .15: .3 can be tried in flight with no build); a
distance fade of shaft strength. Blurring the half-res S target is not advised: it holds the cloud detail as well.

**4. Fix set, ranked by payoff over cost** (slot and fetch figures are estimates [I], not compiled).
1. **Retention of the station caster** (shadow-caster-retention.md rule 3): this is the appear/disappear defect and is not a
   fog change. Find why every node is `moving` for half the session. No slots, no GPU cost. Owner: shadow lane.
2. **Offset the shaft lookup only, in L2** (`fog_density_field_inc.h:245`: `view_direction*(bin.y+(offset-.5)*bin.x)` with
   the `:218-220` offset computed for L2 too and applied to density only under L3; `fog_look_math.h:80` gates the amplitude
   rows). About 4-6 slots in march L2 (422 of 512); repair keeps centres and stays at 510. No GPU cost. Removes the comb at
   the 8-phase rate above and leaves cloud edges un-dithered, so the L3 noise does not come with it. Needs TAA; with TAA
   off the phase is held (`motion_output_fog_inc.h:422`) and the comb becomes a static dither.
3. **Or default to L3** (constants only, zero slots). [M] cost seen in the 31040 burst at rest, after TAA: temporal
   std/mean median .69 %, p99 3.0 % (L2 static 9152: .04 %, .28 %), single-frame spatial high-pass median .51 % against
   .08 %. Visible as faint grain, not boiling; the documented caveats stand (repair outline while history is short, static
   dither with TAA off).
4. **Shaft visibility pass** (the planned move of the lookup out of march/repair): a quarter-res, 64-slice visibility
   atlas filled with four stratified taps per bin (rms .0026, max .024 against dense without any temporal help), then one
   filtered read per march sample. Frees the `fog_look_visibility` body (`:83-100`, roughly 90 slots) in march and repair,
   which pays for cascade cross-fade, the finest cascade, cascade 4 and the penumbra of section 3. Fetches 160x96x64x16 =
   15.7 M per frame against up to 63 M shaft fetches today. One new target and program; Reset, hostile-state and
   native-parity work as for the other fog targets.
5. More bins near occluders: not practical in one ps_3_0 `rep` loop; item 4 achieves the same with its taps.

## Shaft lookup offset in L1-L3 (2026-09-22): the comb fix without the L3 grain

Change: item 2 of the run222 fix set. `fog_density_field_inc.h` offsets only the `fog_look_visibility` position by the
interleaved-gradient value of the covering half-resolution pixel and the TAA phase (`look_taps.zw`, c32.zw =
`max(JITTER_*, SHADOW_JITTER)`, and `max(JITTER_*, 0)` when `FogFrame::look_resolved` is false, i.e. TAA off or failed);
density and lighting samples stay at bin centres. `X3M_FOG_LOOK_SHADOW_JITTER` 0..1, default 1; 0 = the former law in L1/L2
(L3's lookup rides its sample offset). L0 untouched (march `4dacf7e4...`, headers unchanged). [M] measured, [I] inferred.

**Slots / static texture instructions** [M]: march L0 415/17, L1 365/13 (+16), L2-3 425/15 (+3); repair L0 510/22, L1 466/18
(+16, offsets like its march), L2-3 510/20 (bin centres, `FOG_LOOK_NO_OFFSET`: the noise costs about 15 slots, 2 are free);
composite 203/10 and 210/10; one `rep` loop in every march and repair. Fetches per sample unchanged.

**Host study** [M]: `tools/analysis/fog_shaft_sampling_study.py --capture /tmp/x3-bottleX3-run222 --frame 9539 --frame 31040
--field-origin 95576,97323,62698` -> `verification/results/fog-shaft-sampling-study.json`. It marches `look_march` (now with a
per-sample `visibility(points, rays, ds)` callback) over the dumped maps 2 and 3, 160x96 rays, against density at bin
centres with 16 lookups per bin; error = (S - S_ref)/S_lit, green. The normalisation differs from the diagnosis (S_lit
here carries ambient, lift and self-shadow), so absolute values are about 2.8x smaller; the ratio agrees (4.5x against 4.4x).

| 9539, rms / p99 / max | uniform density | analytic family field (1885 shaft rays) |
| --- | --- | --- |
| bin centres (L2 before) | .0036 / .0144 / .0372 | .0041 / .0188 / .0590 |
| lookup offset, one phase | .0037 / .0147 / .0414 | .0049 / .0219 / .0847 |
| lookup offset, 8-phase mean | **.0008 / .0032 / .0073** | **.0010 / .0043 / .0176** |
| L3, 8-phase mean | .0012 / .0037 / .0071 | not comparable (density re-sampled) |
| two lookups per bin, no offset | .0021 / .0087 / .0209 | .0021 / .0090 / .0295 |
| two lookups + offset, 8-phase mean | .0004 / .0017 / .0042 | .0004 / .0017 / .0062 |

31040 gives the same picture (.0040 -> .0008 uniform, .0051 -> .0010 field). So the lookup offset alone reaches the L3 figure.

**Grain and the leak test** [M], temporal std/mean over the 8 phases, raw and after an exponential history of weight .9 at a
held camera (an ideal resolve: the flight's clamped TAA leaves more, .69 % measured for L3, so read these as ratios).
"Clear" rays are those the 16-lookup reference finds unshadowed. On them the lookup offset gives median 0 and p99 0 in three
studies (p99 .004 % raw at 31040 field), but not 0 everywhere: max raw 3.3e-5 (9539 uniform, 1 ray), 4.7e-6 (9539 field, 4
rays), 0 (31040 uniform), 3.6e-3 (31040 field, 84 rays; .030 % after history). Cause: the reference steps 157 units in a
far bin and misses thinner occluders (the spar fan of 31040) that an offset lookup, or the bin-centre one, lands on;
every differing clear ray is such a ray (`clear_rays_differing_without_meeting_an_occluder` = 0 in all four). Not a leak:
on the 2397 / 2755 / 3320 / 3988 rays where no lookup of any phase met an occluder, all 8 phases equal the unshadowed
march bit for bit, and cascade selection sits inside the lookup, so it cannot touch an unshadowed ray either. L3 on the
clear rays: raw median .17 % / p99 .64 % (9539 field), after history .010 % / .038 %. Inside shafts, after history: lookup
offset median .034 % / p99 .23 %, L3 .13 % / .53 %. So fog that meets no occluder is exactly L2's, and shaft interiors and
thin-occluder rays get at most L3's grain.

**Repair / march mismatch** [M]: an L2-3 repair pixel keeps the bin-centre lookup beside offset half-resolution
neighbours. Its step inside shafts, relative to local in-scatter (uniform 9539 / field 9539 / uniform 31040 / field 31040):
against the resolved neighbours rms .43 / .87 / .48 / 1.8 %, p99 1.7 / 3.5 / 1.8 / 6.8 %, max 4.2 / 7.9 / 4.8 / **9.3 %**; against
one unresolved phase rms .61 / 1.3 / .69 / 2.7 %, max 6.9 / 14.6 / 8.3 / **17.4 %**. Outside shafts 0. This is the old comb error,
now carried by the repair pixels alone, and it is spatially coherent: repair pixels form one-pixel chains along hull
silhouettes, and the comb varies slowly along such a chain, so inside a strong shaft it can read as a faint outline
segment rather than noise. [I] Expected below visibility against a hull edge, not shown. No mitigation fits the 2 free
slots of repair L2 (the noise alone is about 15); repair L1 offsets like its neighbours. Flight A/B:
`X3M_FOG_LOOK_SHADOW_JITTER=0` removes the mismatch and brings the comb back.

**TAA off** [M by constants test]: `look_resolved` false zeroes the L1/L2 amplitude, so lookup position and cascade
selection are the bin centre's; with TAA on, the offset also dithers the hard .85 cascade switch across the hand-over
band, which the resolve averages.

**Two lookups per bin** [M]: compiled for march L2 as an inner `rep` of 2 at +-1/4 bin: 434 slots, 15 static fetches, so it
fits the march (not repair L2). It halves the one-frame error and the in-shaft grain (table). Not adopted: it doubles the
shaft fetches of the march (up to 8 per non-empty sample) and the fixture's slope timing cannot price that (it reads
.005-.025 ms per 640x384 march, below its own noise). Needs a flight timing or a working GPU timer first.

**Penumbra / floor**: a distance-widening penumbra is not free (repair L2 has 2 slots), so none was added, and
`X3M_FOG_LOOK_SHADOW_FLOOR` stays .15: raising it costs no slots but flattens the L2 look the user likes and can be tried
in flight without a build (.3).

**Fixture** (bottle X3, arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`). March cases `shadow=2`: a 64-texel map with dark
column pairs addressed by view depth (3750 units per pair), host twin `stripe_visibility`: `A_look1_stripes`,
`A_look2_stripes` (phase 5), `A_look3_stripes` (phase 3, `jitter=0.25` so the one-bin lookup separates from the
quarter-bin sample) and `A_look2_stripes_held` (`resolved=0`: bin centres). GPU against host: S max 2.8e-4 (fp32) / 3.7e-4
(fp16), 572-576 of 576 pixels compared; the other lookup law would miss by .0139 (L1), .0076 (L2), .0070 (L3), above the
.003 gate (`look_shaft_offset_exercised`). Repair with a shaft map bound (new; 1024-texel stripes, 47-unit pairs over the
20000-unit geometry columns): repair L1 and L2 outputs against the host march of the repaired ray (L1 with the covering
half pixel's noise, L2 bin centres), 573 / 576 stratified odd pixels: max 4.9e-4 each, while the opposite law is .0147 /
.0105 away (gate `repair_shaft_lookup`); repair L2 also equals the GPU bin-centre march to 4.9e-4. `fog_density_shader_run.py
build/run/check` PASS, 29 + 58 checks, 13 look cases, all gates true; generator `--check` PASS for the nine fog programs.
Host modules `test_fog_look_reference test_fog_density_shaders test_volumetric_fog test_shader_compiler_provenance`: 26 tests
pass. Scratch production build RelWithDebInfo links; `check_no_x87.py` reports no violations. No flight yet.

## Run 63 session B verdict (2026-09-22, run224)

Run63 DLL `b0cde491…`, L2 at 1.0x with the shadow-only shaft lookup jitter and caster
retention engaged: the user reports the station shadow shaft **no longer disappears**
and reads like L3 (clean shafts) while the fog stays L2-calm. No rim on silhouettes
was reported. Capture preserved in `/tmp/x3-bottleX3-run224` (not triaged; no open
symptom). This closes the run222 shaft comb and the appear/disappear defect. Next:
collapse the presets to one look (L2 law), keep the env tuning; dust motes; shadow
lookup in its own pass for cascade cross-fade, the finest cascade and penumbra.

## Run 239 triage: fog cleared as the plateau cost (2026-09-22)

`/tmp/x3-bottleX3-run239` station-angle ~50 fps plateau (vs ~100 fps control,
same session): `volumetric_fog_frame cpu_us` is flat (~0.6-0.7 ms) in both the
slow and fast windows — fog is 3-7 % of `dt` and does not track the fps drop,
matching the user's own toggle test. The cost is in `frame_phases`
`view_submit` (draw/state submission, 4.9x), tied to shadow-lane
`receiver_draws` and retention's `live_c4` cascade caster count (both ~6x).
Full split: `docs/architecture/engine-frame-time.md` "Run 239: frame time
with the split".

## 2026-09-22: GPU timer built, not merged (bottle X3 has no timestamp queries)

A `--gpu-timing` pass timer (IDirect3DQuery9 TIMESTAMP / TIMESTAMPFREQ / TIMESTAMPDISJOINT ring around the fog, TAA, bloom and Present work) was implemented and fixture-qualified on branch `worktree-agent-a2b067e4005541a7a` (commit 423bd098). On bottle X3 the D3D9 device refuses all three query types (`CreateQuery(nullptr)` = 0x8876086a; adapter reported as "NVIDIA GeForce 8800 GTX"/nvd3dum.dll), so the timer can only soft-fail here; it is kept on the branch for native Windows and not merged. GPU cost on this Mac is measured by frame-time A/B with the pass on and off (fps overlay / `--frame-timing`), which run239 already did for fog: flat within a millisecond.

## Single look: L0/L1/L3 retired, the L2 law is the only one (2026-09-22)

The user accepted L2 (runs 231-237), so the preset machinery is gone and the L2 law is the single
stored-range look; `docs/architecture/fog-density-runtime-integration.md`, "The look", owns the law and
`docs/architecture/cleanup-inventory-2026-09-22.md` batch 8 owns the removal list. No look selection exists
any more: `--volumetric-fog-look` is refused by name, `X3M_VOLUMETRIC_FOG_LOOK` is dropped by the launcher and
ignored by the DLL with one `volumetric_fog_look_ignored` line, Ctrl+Alt+F11 no longer cycles anything and the
overlay reads `FOG 1.50x` without an L-suffix. A look-program creation failure is now final
(`density_program_create`): there is no unshaped fallback, so the stored path stays off and legacy is untouched.

**Shader slots, stored range.** Before: eight created programs, 3104 Microsoft-table ps_3_0 slots
(unshaped march/composite/repair 415/203/510, L1 march/repair 365/466, L2 march/repair 425/510, look
composite 210). After: three created programs, **1145 slots** (march `fog_density_march_look` 425/15 texture
instructions, repair `fog_density_repair_look` 510/20, composite `fog_density_composite_look` 210/10), all
under the 512-slot ceiling, the march still one `rep` loop. The three unshaped programs (1128 slots) and the
texel-exact march stay as the shader fixture's parity reference and are no longer created by the renderer.
Deleted with L1: `fog_density_{march,repair}_look1_ps.hlsl`, their `*_program_inc.h` and
`verification/results/fog-density-{march,repair}-look1-program.json`; the kept pair lost the `2` in its name.

**Bit-identity (acceptance).** `fog_density_shader_run.py build|run|check` on bottle X3 before the change
(`/tmp`-local output, reference `ref-before`) and after (`fog-after3`, reference `ref-after`), same baked
packets. All seven stored-density programs keep their bytecode: march `4dacf7e4d3ffa909`, composite
`ec97163d99f8a0be`, repair `d40f9a07538f1e67`, exact `a718e8e9161ae256`, look march `6a347ac2c07d4be7`, look
composite `6c6a78b9fb72c408`, look repair `155a82e2833141db` (16-hex prefixes; full values in the program
JSONs). The six reused L2 cases and the repair-with-shafts image are byte-for-byte equal, FP32 and FP16
(sha256 prefixes of the new files, old name -> new name):

| Case | bilinear32 | bilinear16 |
| --- | --- | --- |
| `A_look2_sky` -> `A_look_sky` | `5a6ce47b291850ef` | `8e3cbf876597046c` |
| `A_look2_depth3` -> `A_look_depth3` | `5a6ce47b291850ef` | `8e3cbf876597046c` |
| `B_look2_sky` -> `B_look_sky` | `84d78267f42bfce8` | `e4fa96f9500cd8cf` |
| `A_look2_stripes` -> `A_look_stripes` | `942e53e4023007fe` | `f137782af8513f83` |
| `A_look2_stripes_held` -> `A_look_stripes_held` | `297f4cdb1fa90556` | `2d5d71614d4519f7` |
| `repair2_shafts.full` -> `repair_shafts.full` | `88d6d32842e0a067` | - |

(`A_look_depth3` hashes equal the sky case because its 149999-unit depth clamps to the 112500-unit column cap.)
The host reference arrays for those cases are equal element for element as well, so both sides of the
comparison are unchanged. Fixture result after: **PASS, 17 gates**, shader fixture 23 checks, pass fixture 57
checks / 0 failures / 1105 state restorations (1162 before the change, with the preset cycle's extra frames), run 46.5 s + 28.6 s, summary
`verification/results/fog-density-shader/summary.json`.

**Fixture changes with the retirement.** Look cases are now `A_look_{sky,stripes,stripes_held,depth3,depth90000,shadowed}`
and `B_look_sky` (seven, from thirteen); `look0_shadowed_black` and `look1_shadowed_coloured` became
`look_shadowed_coloured` (a fully shadowed sample keeps coloured light: ambient plus the shaft and lift
floors); `look_presets_versus_host` is `look_versus_host`; the repair-with-shafts gate has one entry instead of
two. The pass fixture's CPU twin (`verification/probe/fog_density_cpu_march.h`) gained the look law
(remap over the coverage waves, domain warp, two-lobe phase, coloured ambient, lift octave, one-tap
Beer-powder, tinted extinction in `apply`) at the bin centres, because every stored draw now uses it; its
frames keep `look_resolved` false, and `cpu_setup` throws if that ever changes. The former
"fully shadowed zero in-scatter" shaft check is now
`shafts_fully_shadowed_dim_coloured_same_transmittance`: transmittance bit-identical to the unshadowed frame,
in-scatter dimmer but positive. The preset-cycle checks became "frame to frame byte-identical" and
"phase and resolve move no density sample" (the retired L3 sample offset is gone; `look_self.zw` stay zero).

**Tuning variables kept** (`X3M_FOG_LOOK_<NAME>`, read once at init, all read by the look):
`COVERAGE`, `EXPONENT`, `SIGMA_SCALE`, `COVERAGE_VARIATION`, `WARP_CYCLES_NEAR`, `WARP_NEAR`,
`WARP_CYCLES_FAR`, `WARP_FAR`, `FORWARD_G`, `FORWARD_WEIGHT`, `BACK_G`, `ALBEDO_WHITE`, `AMBIENT_GAIN`,
`EXTINCTION_TINT`, `SCATTER_LIFT`, `LIFT_FLOOR`, `SHADOW_FLOOR`, `SKY_CAP`, `TAPER_START`, `SELF_SHADOW`,
`POWDER`, `TAP_DISTANCE`, `TAP_LENGTH`, `SHADOW_JITTER`, plus `AMBIENT_SUN` / `AMBIENT_AWAY` (r,g,b).
**Removed:** `JITTER_NEAR`, `JITTER_FAR` (L3 only).

**Other checks.** Full host suite `227 modules, 2260 tests, 0 failing`; scratch production build
(`build-fogl2`, MinGW i686 RelWithDebInfo) `check_no_x87.py` PASS, 0 violations over 637 reachable functions;
`tools/manage.py launch --dry-run --bottle X3` with the Run 65 session C options (no `--volumetric-fog-look`)
exits 0 with no `X3M_VOLUMETRIC_FOG_LOOK` in the environment. The nine `bloom-*-program.json` records were
restamped for the new `generate_rigid_motion_pixel.py` hash (one `tool_sources` line each, bytecode and
headers untouched, as in commit `babe1547`); a native re-promotion cannot run from a worktree because
`stage_bloom_programs.py` pins absolute main-checkout paths.

## Sun-visibility grid pass built, default off (2026-09-22)

The design of `docs/architecture/fog-shadow-pass.md` implemented behind `X3M_FOG_SHADOW_PASS=1`
(`--fog-shadow-pass on`, default off; `on` requires `--volumetric-fog-range stored`); its "As built"
section records the deviations (full-angle radius with a one-sided penumbra, atlas-texel strata, s4 with
two sampler calls, the grid target created at `prepare_density`). [M] measured on bottle X3 (arm64,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), fixture output `/tmp`-local (`fogshadow/build3`,
reference `ref2`, the same baked packets), summary `verification/results/fog-density-shader/summary.json`.

**Programs** [M] (`fog_density_shader_slots.py`, Microsoft table): unchanged bytecode for the seven existing
programs (march `4dacf7e4`, composite `ec97163d`, repair `d40f9a07`, exact `a718e8e9`, look march `6a347ac2`
425/15, look composite `6c6a78b9` 210/10, look repair `155a82e2` 510/20); new `fog_density_visibility_grid`
`a9639c74` **319 slots / 12 fetches / 2 loops**, `fog_density_march_grid` `75e87bb7` **352 / 8 / 1**,
`fog_density_repair_grid` `2a2f3b77` **453 / 13 / 1**. All ten under 512.

**Acceptance** [M], `fog_density_shader_run.py build|run|check` (after the review fixes, `build4`): **PASS, 28 gates**,
shader fixture 30 checks, pass fixture 73 checks / 0 failures / 1099 state restorations (c0..c41 hostile).

1. Pass off: the five look images and `repair_shafts.full` hash exactly as in the look-collapse table above
   (`5a6ce47b`/`8e3cbf87`, `84d78267`/`e4fa96f9`, `942e53e4`/`f137782a`, `297f4cdb`/`2d5d7161`, `88d6d328`);
   gate `pass_off_bit_identical`.
2. Pass on, no cascade: `A_grid_sky`, `A_grid_depth3`, `B_grid_sky` byte-identical to the in-march images
   (FP32 and FP16). Stripes: GPU grid march against the host march reading the host atlas on 576 stratified
   rays, S max **2.2e-4** (FP32) / **3.1e-4** (FP16), p99 6.0e-5 / 1.6e-4 (gate 5e-4); T max 3.6e-4 / 6.2e-4
   against the host, **0.0** against the in-march program; the in-march offset law is .0069 away on 11 pixels.
   Atlas twin: max 1 LSB on every texel (stripes 139, held 157, seam 135,978 (a .3 rounding tie), penumbra 142,
   repair 117 of 147,456 texels differ by one step).
3. Seam (`A_grid_seam`, cascade 0 x = 1e-5 z handing over at z = 85 km to cascade 1, one row map read at v .3
   and .8): centre ray .298 on slices 0–52, .353 / .478 / .604 / .718 on 53–56, .8 from 57; max step between
   consecutive slices .149 over all texels (ramp increment .126, bound .165 with the stratum term), against the
   hard switch's .5.
4. Penumbra (`A_grid_penumbra`, slab at depth .1, texel 36.6, range 200,000, slice 58 at 98.7 km): 10–90 %
   width **3.4 / 6.5 / 9.8 texels** at blocker distances 10 / 30 / 60 thousand units, expected 0.0093 d / texel
   = 2.5 / 7.6 / 15.2 (ratios 1.34 / .86 / .64, monotone); the in-march 2×2 law is 1 texel at every distance.
5. Repair (`repair_grid_shafts.full`, 1024-texel stripes over the 20,000-unit columns): 576 odd pixels against
   the host march of the repaired ray reading the host atlas, max **4.9e-4**; the bin-centre in-march repair law is
   .0050 away (5 pixels beyond .003).
6. Production `FogPass` with `shadow_pass`: grid target only after prepare, the pass drawn only with a cascade
   (`FogResult::grid`), no-cascade frame byte-identical to the in-march instance, a fully lit map identical to no
   map, split map: T bit-identical to the in-march programs and in-scatter moved on 770 of 1174 fogged pixels
   (max .033), **19 extra device calls** against the pass-on no-cascade frame (16 against toggle-off, which binds
   the three maps), repeated frames byte-identical with no new reference or allocation,
   Reset releases the target and the next prepare re-creates it with a byte-identical frame, detach leaves
   `references() == 0` and the device refcount balanced; every transaction under hostile caller state (c0..c41
   set hostile since the review) restored. Review fix: an injected RGBA8 target failure leaves the stored path
   available with `shadow_pass_refused=density_grid_target`, the split frame byte-identical to the in-march
   instance's, sticky until detach (gate `grid_refused_falls_back_to_in_march`); a column cap below 12040 or
   non-finite lights the frame (`grid_column_cap`) instead of dividing by a zero slice width (`test_fog_shadow_grid`).

**Not measured**: GPU time (no timestamp queries on this bottle; the slope timing reads 0.008–0.014 ms per
1280×768 pass or march, noise) — the flight's at-rest frame-time A/B decides; the run222 host transmittance
study (design item 3) was not run. **Host**: `test_fog_shadow_grid` (7 tests: header against the Python twin),
`test_fog_density_shaders`, `test_fog_look_reference`, `test_volumetric_fog` (`--fog-shadow-pass` mapping),
`test_shader_compiler_provenance`; full host suite `run_host_suite.py`: 229 modules, 2272 tests, 2 modules failing (`test_sun_share_lane`, `test_launcher_stderr_tee`: both launch `manage.py`, which refused with "another installer or launcher is active" while the user's game session was running; not fog-related). The nine `bloom-*-program.json` records were restamped for the new `generate_rigid_motion_pixel.py` hash (one `tool_sources` line each, as before). Scratch build
`build-fogshadow` (MinGW i686 RelWithDebInfo) `check_no_x87.py` PASS, 637 reachable functions, 0 violations;
`tools/manage.py launch --dry-run --bottle X3` with the Run 65 session C options plus `--fog-shadow-pass on`
exits 0 with `X3M_FOG_SHADOW_PASS=1`. Fetch ceilings per frame: 1280×768 62.9 M map + ≤ 15.7 M grid fetches
(3.9 MB atlas); 2560×1440 235.9 M + ≤ 59.0 M (14.7 MB).

## Run 251: fog shadow pass in flight, no A/B (2026-09-22)

Run 67 session C, Run67 DLL `621cad63…`, `/tmp/x3-bottleX3-run251/session-20260922-231055-216.log`
(`--fog-shadow-pass on --shadow-cascade-min-footprint 8`, stored range, single look), compared with session B
(run250, same fog options, pass off). [M] = measured by the named script, [I] = inferred; scripts and outputs
in `verification/results/run251-fog-shadow/`.

- Options [M] (`log_facts.py`): one `volumetric_fog_shadow_pass enabled=1 grid=quarter slices=64 tiles=4x4
  format=A8R8G8B8 cascades=3 taps=4 penumbra=1,1,16` row. The build logs no per-frame grid or fallback row
  (no fog row type naming either) [M], so whether each frame took the grid path is not observable.
- Device calls [M] (`fog_cost_retention.py`): every applied frame issues **338** fog calls (37,917 frames)
  against **324** in run250 (7,411 frames): +14, where the fixture measured 19 extra device calls for a pass-on
  frame with a cascade (section "Sun-visibility grid pass built, default off"). The gap is unexplained.
- No device-call failure [M] (`log_facts.py`): all 37,917 applied `volumetric_fog_frame` rows carry
  `result=00000000 restore=00000000`.
- `cpu_us` [M] (`fog_cost_retention.py`, first 200 applied frames skipped): p50 682 / p95 1,430 us against
  run250's 782 / 1,356 us. This is render-thread CPU time of the fog transaction, not GPU time.
- Frame time is not comparable [M] (`fog_cost_retention.py`, `frame_timing` windows): run251's bursts sit at
  95 (9600-9900) and 129 (11400) `draws_p50`, most of the flight at 95-254, against 391-416 at run250's
  bursts. Different scenes, not an A/B.
- Captures [M] (`log_facts.py`, `footprint_census.py`): two complete 8-frame bursts, 9772-9779 and
  11400-11407, 8 files per frame.
- Shaft edge [M] (`shaft_edge_width.py`, horizontal luminance profile across the pale column above the ship,
  rows 330-460): 10-90 % rise 261 / 241 px (frame 9772) and 278 / 218 px (frame 11400), left / right flank,
  contrast 97-149 %. Absolute widths with no pass-off capture; that the column is a shaft is [I].

**Outcome.** Nothing failed and the pass ran on every applied frame's call count, but neither the look nor
the cost is measured: both need an in-session A/B, which the next build provides with a Ctrl+Shift+F11
toggle and a per-frame grid row (in progress). User verdict: "softer?", low confidence.

## Route bridge baseline pinned to 6f16dbf6 (2026-09-23)

The `--baseline` build of `fog_route_bridge_build.py` produces the legacy bit-identity witness: the route
bridge's five legacy image hashes must equal its output. That build now uses a pinned production root,
**`6f16dbf6`**, the direct parent of the stored-density wiring `39c98242`, checked out with
`git worktree add --detach`. Earlier runs used `184843cd` and `85c7c821`, which also predate the wiring. The
harness comes from the current tree, and cleanup batch 5 (`7c8e0462`) renamed `ao_default_m22/m32` to
`projection_default_m22/m32`. Under `X3M_ROUTE_BRIDGE_BASELINE`, `verification/probe/fog_route_owner_inc.h`
therefore also defines the old names with the same values. The change is limited to the harness and leaves
production source untouched. Host builds [M], MinGW i686, asset data `build-post-batch5/generated/fog_field`:
the baseline build (root `6f16dbf6`) produced exe `00c9bf2fbab6a939…` with 58 bound inputs; the route build
(current tree) produced exe `fdfd1422df4c1114…` with 65 bound inputs. `test_fog_route_bridge`: 5 tests OK.
Not run under Wine yet.


## Fog shadow-pass A/B toggle fixtures on main (2026-09-23)

Main `b4f32d8c` (toggle) and `805e9186` (bridge baseline pin). Pass fixture `fog_density_shader_run.py build/run/check` against a reference from `tools/analysis/fog_density_shader_reference.py`: `RESULT PASS checks=78 failures=0`, 28/28 documented gates plus the `grid_*` A/B checks, `GRID_REPORT calls=20 net_calls=15`, `GRID_TOGGLE off_calls=323 on_calls=338 difference=15` (measured; one earlier attempt hit the runner's 540 s timeout under host load and passed on retry). Route bridge (`fog_route_bridge_run.py run` under the Wine lock, baseline root `6f16dbf6`): baseline 515 checks PASS, bridge 30,476 checks PASS with all 15 `shadow_ab_*` names PASS (toggle off logs one row, in-march frame byte-identical, grid kept, Reset while off keeps no grid, toggle on recreates the grid once with no null target and matches the pre-toggle grid frame, change rows throttled and on their own budget), exit 4 checks PASS; `legacy_bit_identical_to_baseline` true (`verification/results/fog-density-route/summary.json`). Both measured.

## Run 256: fog shadow-pass A/B toggle in flight (2026-09-23)

Run 68 C (`/tmp/x3-bottleX3-run256`, Run68 DLL `39c8c70d…`): the fog command with
`--fog-shadow-pass on` and the Ctrl+Shift+F11 toggle. Script:
`verification/results/run256-fog-toggle/fog_toggle_triage.py`. All measured unless marked.

- **The toggle worked; it has no on-screen notice by design** (`src/proxy/capture.cpp`
  handler calls only the fog toggle, no `comparison_notice`; both notes say so). 27
  `fog_shadow_pass_toggle key=ctrl_shift_f11` rows, alternating, all `refused=none`, from
  frame 2711 to 5932, presses as close as 13 frames. The user saw no hint and no visual change.
- **Rendering did switch:** on state 4,536 applied rows (`march=grid`, `grid_built=1`,
  `grid_cascades=7`, `shadow_maps=3`, grid calls 20 / net 14, `grid_frame_term` p50 0.326 ms,
  max 0.854, cpu p50 724.8 µs); off state 1,811 rows (`march=in_march fallback=toggled_off`,
  `shadow_maps=2`, cpu p50 668.9 µs).
- **Cost:** session frame time p50 15.0 ms (7,232 `frame_end` rows); with fog applied, on
  p50 15.0 / p95 19.0 ms, off p50 15.0 / p95 17.0 ms.
- **Look:** not distinguishable by the user in flight; the captures (5651–5658 on, 6395–6402
  off) are different scenes and were not diffed. No device failures or refusals.

**Decision (2026-09-23):** no visible win and a small cost, so `--fog-shadow-pass` stays
default off; the option and the toggle remain for a paired-capture comparison if wanted.

## Dust motes (2026-09-23)

`--fog-dust-motes N[,SIZE[,STREAK]]` (`X3M_FOG_DUST_MOTES`, default off, stored range only;
[fog-dust-motes.md](../architecture/fog-dust-motes.md), "As built"). Worktree build on main `72262ba1` (after the
motion-weight commit `022798ba`), review fixes applied (caller cut, launcher/DLL option bounds), not committed at the
time of the runs; bottle X3 (arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`). All measured.
Compact record: [bottle-X3/fog-dust-motes.json](../../verification/results/bottle-X3/fog-dust-motes.json),
written by `verification/results/fog-dust-motes/summarize.py` from the three runs below.

- DLL: `cmake -S . -B build-motes -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo`,
  `cmake --build build-motes -j8` (`d3d9.dll` `a3bd8d001f3d1bf0…`); `check_no_x87.py`: 639 reachable functions, 0 violations.
- Programs: `generate_rigid_motion_pixel.py --shader fog_dust_motes_vertex --shader fog_dust_motes_look
  --shader fog_dust_motes_grid` under the Wine lock; `fog_density_shader_slots.py`: 311 / 257 ps_3_0 slots,
  101 vs_3_0; the ten existing programs' bytecode unchanged.
- Pass fixture: `fog_density_shader_run.py build/run/check` (reference `/tmp/x3-run67-fog-ref`, exporter and screen
  digests unchanged), lock wait 0.0 s, child 92.3 s: `RESULT PASS checks=117 failures=0 state_restorations=1141`,
  30 / 30 gates (new `motes_cases`, `motes_programs_below_512`), the 78 existing checks all PASS, accepted look
  hashes equal, `GRID_REPORT calls=20 net_calls=15`, `GRID_TOGGLE off_calls=323 on_calls=338 difference=15`
  (unchanged); the 29 `M_motes_*` / `motes_off_bit_identical` checks PASS, among them the caller cut
  (`MOTES_CUT control_streak=1 cut_streak=0`, 6 units and 2 degrees, the frame within 0.38 of the still twin's
  tolerance). `MOTES_CALLS off=322 on=334 stage=12`; the per-case numbers are in the note's table.
- Route bridge: `fog_route_bridge_run.py run --cases /tmp/x3-fog-family-gpu-inputs-final/cases.txt`, baseline rebuilt
  from `git archive 6f16dbf6`, lock wait 0.0 s, child 79.4 s: baseline PASS, bridge 36,333 checks PASS (15
  `shadow_ab_*` names, 13 `motes_ab_*` names), exit 4 checks, `legacy_bit_identical_to_baseline` true. Two earlier
  attempts failed on the fixture's own expectations (a frame-number jump to force a periodic row disarms the card
  replacement for one warm-up frame; a Reset re-creates the family atlas too, +5 not +4) and were corrected in
  `fog_route_density_inc.h`.
- Temporal row (m): `run_temporal_pass.py` under the lock on the rebased tree (the motion-weight rows included), lock
  wait 0.0 s, child 136.1 s: `passed: true`, `RESULT PASS numerical=744 state_restorations=278 generations=2`, 546
  samples (712 / 528 of main plus 32 / 18 for case (m)), 84 `MOTION_WEIGHT` and 28 `SETA_EXIT` rows, the new
  `MOTE_STREAK` asserts true on 8 rows. The tracked `temporal-pass.txt`, `temporal-lattice.txt` and
  `temporal-pass-summary.json` are this run's (the summary's report digests equal the files).
- Host: `test_volumetric_fog`, `test_comparison_hotkeys` (comparison controls 16,393 checks), `test_fog_density_shaders`,
  `test_fog_route_bridge`, `test_fog_cards` (mock extended), `test_sector_background`, `test_sun_share_lane`,
  `test_shader_compiler_provenance`, `test_fps_overlay`, `test_fog_field_assets`, `test_fog_density_cache` OK.
- Launcher: `manage.py launch --bottle X3 --dry-run` with the fog prerequisites, `--volumetric-fog 0.02
  --volumetric-fog-cards replace --volumetric-fog-range stored --fog-dust-motes 2048` resolves
  `X3M_FOG_DUST_MOTES=2048,4,128`; without `--volumetric-fog-range stored` it exits 2 with
  `--fog-dust-motes requires --volumetric-fog-range stored.`

## Run 264: fog dust motes in flight (2026-09-23)

Run 70 B (`/tmp/x3-bottleX3-run264`, Run69 DLL `70abe438…`): the fog command with
`--fog-dust-motes 2048` and the Ctrl+Alt+F11 toggle (4 toggles). Outputs:
`verification/results/run264-motes/` (`log_summary.py`, `sky_blobs.py`). All measured unless
marked. The user: no fps effect; too many (wants 30–40 % fewer); too big ("snow").

- Mode row `count=2048 size=4 streak=128 RADIUS=1000 NEAR=25 MAX_PX=12 GAIN=1 SOFT=0.02
  DRIFT=20`; 7,936 applied frames with motes on (`mote_calls=12`, 336 device calls) vs 838
  off (324); no refusal; `mote_shift_px` p50 2.3 px; `mote_streak=0` only on the first stage
  frame, two |delta| > R gaps and the two toggles; no cut rows.
- **Cost:** frame time p50 11.79 ms on vs 11.71 off in the same scene (130 draws), fog cpu_us
  +20–25 µs (inferred from spans).
- **Look (on/off capture pair 5048/6411, sky census):** 94–112 extra mote-sized blobs, extent
  p10/50/90 5/8/14 px and width 4/6/10 px (round discs, not streaks: the streak at 2.3 px/frame
  is 2–9 px), peak about 1.0× the fog behind them (p90 1.3×); the design estimated ~120 motes.
  "Snow" = disc size at the near end (12 px cap inside 333 units) and brightness, not streaks.
- **Next (B2):** `--fog-dust-motes 1300,3` with `X3M_FOG_MOTES_MAX_PX=8` (37 % fewer, 25 %
  smaller, near cap 8 px); `X3M_FOG_MOTES_GAIN=0.6` if still too bright; size 3 is the floor
  (the 3×3 TAA clip argument). Open: the on frame's sky median was 12.5 % brighter than the
  off frame's, more than the motes' coverage explains (scene/fog state difference, inferred).

**Run 70 B2 (2026-09-23, user report, no session preserved):** flown with `--fog-dust-motes 1300,2`
and `X3M_FOG_MOTES_MAX_PX=8`: "good setting, I like it"; told that 2 px sits below the note's
4 px TAA minimum (section 3) and dims under the 3×3 clip, the user settled on size 3. **Decision:**
motes on by default under the stored range at `1300,3,128` with `MAX_PX 8` (`--fog-dust-motes 0`
opts out). The Run 264 entry's "size 3 is the floor" was a working limit; the note's derived
minimum is 4 px, and 3 is the user's accepted trade. Reproducing Run 264 (2048,4 at MAX_PX 12)
now needs `X3M_FOG_MOTES_MAX_PX=12`.

## Data-driven fog families: tool, loader and host cases (2026-09-23)

[fog-family-data.md](../architecture/fog-family-data.md), "Implementation" (option C): `tools/analysis/fog_families.py`
writes `<game>/x3m/fog-families.bin`; the DLL loads it once at the first fog sector sample and decodes one packet per
family switch; compiled names first; any failure falls back to the 14 compiled profiles (header) or disables the row.
Worktree on main `70cd9d92`, not committed at the time of the runs; host only, no Wine run. All measured.

- Palette convention settled: the 12 provisional stock palettes are reproduced bit for bit from the installed `01.cat`
  (floor-565 DXT decode with truncating interpolation, `numpy.percentile` linear, bands inclusive at both ends, stops
  rounded to 9 decimals); `lod_atlas.decode_dds` or half-open bands miss by up to 0.09.
- Tool evidence: [tool-dryrun/summary.txt](../../verification/results/fog-family-data/tool-dryrun/summary.txt)
  (`run_tool_evidence.py`, root from `make_mod_root.py`). Stock X3: 16 positive families, 14 `covered_by_build`
  (12 / 12 match), `earth` `texture_missing`, `xtmgreenring` `no_dust_bodies`, 1.3 s; with
  `--background-palette earth xtmgreenring` one family (`earth`, 2,281,316 bytes), `xtmgreenring`
  `background_missing`. Vanilla + Mayhem 3 + Renegades root: 56 families, 50 packets, 114,063,336 bytes, `--jobs 1`
  114.1 s, `--jobs 4` 39.0 s, byte-identical; `--check` PASS; the host build of the loader decodes all 56 rows.
- Host: `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest` over `test_fog_families`, `test_fog_family_file`
  and the ten fog modules (`test_comparison_hotkeys`, `test_fog_cards` (mock extended), `test_fog_family_gpu`,
  `test_fog_field_assets` (pinned packet hashes unchanged), `test_fog_route_bridge`, `test_fog_sector_policy` (file
  table cases added), `test_fps_overlay`, `test_sector_background`, `test_sun_share_lane`, `test_volumetric_fog`):
  84 tests OK, 116 s. The loader fixture (`fog_family_file_fixture.cpp`, host build): 62 cases (valid and shared-packet
  loads, absent, directory, 18 header rejections, 31 row / packet-row disables, 9 switch-time decoder failures that
  disable only their packet's rows);
  Python validator and DLL loader agree on 13 corruptions; `X3M_FOG_FAMILIES` `0` / `none` disable.
- Build: `cmake -S . -B build-fogfam-9107 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
  -DPython3_EXECUTABLE=/usr/bin/python3`, `cmake --build build-fogfam-9107 -j8`: 0 warnings, `d3d9.dll` `72b78821…`
  and the new target `fog_family_file_fixture.exe` `c44688db…`; `check_no_x87.py`: 639 reachable, PASS;
  `fog_field_assets.cpp.obj` 0 x87 instructions, `fog_pass.cpp.obj` 11 as at `70cd9d92` (float returns).
- Wine (orchestrator, bottle X3): the i686 fixture's `--self-test` PASS, `cases=62 compiled=14`
  (`verification/results/fog-family-data/fixture-wine/`). Not run: the GPU family fixture with a file family
  (note §5), a flight.
- Review round 1 (two reviewers): names with `"` / `\` refused; `--install --replace` restores the `.previous` pair
  on a failed write or validation and re-checks for a running game before moving anything; duplicates checked against
  every earlier row (a disabled first row left `find()` and `row()` on different rows: `field_row_disabled` every
  frame, reviewer probe in [dup-probe/](../../verification/results/fog-family-data/dup-probe/)); a packet header may
  not carry a compiled id; `family_table()` is null until loaded; a first allocation failure at a switch is retried;
  switch-time log lines at most once per row; families sharing a packet share the decoded field; the launcher prints
  a `fog families:` line. Fixture 67 cases (host).
- Incident: building the first synthetic root wrote through two pre-existing directory symlinks of an older
  scratch root and replaced five loose files of `/tmp/x3-mod1` (`addon/types/{Jobs.txt,TBackgrounds.txt,gamestarts.xml}`,
  `addon/maps/{WareTemplate.xml,x3_universe.xml}`) with self-referencing links; the tree needs re-extraction before
  it is used again. The evidence above reads TBackgrounds and the map from `addon/07.cat` (as the census did);
  `make_mod_root.py` now refuses an existing destination and symlinked parents.

## Run 271 (Run 72 B, 2026-09-23): hand-over after a sector change, docked view

Measured (`verification/results/run271-music-keep/fog_entry_timeline_out.txt`): after a sector change the medium is
drawn from +1.3–2.0 s and the vanilla cards are suppressed only at +3.5–4.6 s, when the far density level is ready
(far_ready 3.6–4.7 s; card warm-up one frame after; no atlas decode with the compiled profiles); a 6 s `no_cockpit`
span precedes the first sector frame, during which no sector identity exists for the fill. After undocking with the
cache resident the hand-over is immediate (+0.03/+0.52 s). Docked spans (25 s, 14 s): the sample has scene
authority but the sector detector refuses with `anchor_mismatch` (the ship's parent is the station, not the sector;
`sector_background.h` cross-check), so profile 0 and the native cards (7 observed, 0 suppressed). Unexplained: 37
frames of card refusal after the second undock. Next: a design note on starting the fill from the sector-change
event and masking the cards earlier; accept the docked anchor when the parent's parent is the cockpit sector.

## Fog hand-over R1+R2 and docked walk: implementation, host and build (2026-09-23)

[fog-handover.md](../architecture/fog-handover.md), "Implementation": cold-start readiness step
(`--fog-handover-step`), cold fill with one whole-atlas latch (`--fog-handover-coldfill`), docked parent
walk (`--fog-docked`), all default on, all off = the previous behaviour. Worktree on `3b757320`, not
committed at the time; no Wine run, no flight. All figures measured unless marked.

- Host witness `fog_handover_host.cpp` (`PYTHONPATH=verification/probe:verification/analysis /usr/bin/python3
  -m unittest verification.analysis.test_fog_handover`: 6 tests OK, 48 checks;
  [host_witness_out.txt](../../verification/results/fog-handover/host_witness_out.txt)). Stepped cache, one slab
  per frame: cold step resident frame 6 = ready frame 6; switches off ramp 89 frames after the resident frame
  with the far level in 4 budgeted latches (max 1,065,024 B); warm jump with the step on ramps 89 frames
  (largest step 1/90) and reports nothing; cold fill generates exactly the far first-fill box (1,124,864 nodes),
  holds the worker, then one latch of one rectangle of 4,260,096 B (whole far atlas, fine 0 rects), resident
  and stepped two frames after the cold start; settled atlases equal a from-scratch fill (0 differing bytes)
  in every case. Walk: depth 0 reads = legacy reads; depths 1/2/3 found with 3/5/7 extra reads; depth 4,
  another sector, null / misaligned / unreadable parent refused; a parent cycle stops after 7 reads. The
  host millisecond fields are not flight timings (the stepped worker runs inside the frame loop).
- Fog and related host modules: every `test_fog_*`, `test_volumetric_*` and `test_sector_background`
  (29 modules, 246 tests) and the 93 other modules that read `manage.py`, `capture.cpp`, `motion_output`,
  `fog_pass` or `sector_background` (1,136 tests), all green via `run_host_suite.py --modules`.
  `test_sector_background`'s wrapper host now counts 22 checks (the docked span adds 3).
- Build: `cmake -S . -B build-fh-5823 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
  -DPython3_EXECUTABLE=/usr/bin/python3`, `cmake --build build-fh-5823 -j8` (scratch directory outside the
  repository): 0 warnings, `d3d9.dll` `144634cad51e7e72…`; `check_no_x87.py` PASS, 660 reachable. The new code
  adds no x87 instruction ([x87_lines.py](../../verification/results/fog-handover/x87_lines.py),
  [x87_lines_out.txt](../../verification/results/fog-handover/x87_lines_out.txt): the only x87 sites in the
  touched functions are the existing `float(elapsed)` in `DensityCache::step` and the existing `far_ready` log
  line). `fog_density_shader_run.py build`: 0 warnings, `fog_density_pass_fixture.exe` `d56281998c73…` with the
  new cold hand-over case (`HANDOVER` row, 5 checks), not run.
- Open: the Wine run of the density pass fixture (the accepted look hashes are unaffected by construction:
  no shader or look change, the switches default off in `FogDensityConfig`); a flight with two gate
  transits into fogged sectors and one docking (station and carrier) for the `volumetric_fog_handover` and
  `volumetric_fog_docked` lines.

## Fog hand-over R3 and review fixes (2026-09-23)

[fog-handover.md](../architecture/fog-handover.md), "R3 implementation" and the revised "Implementation".
Worktree on `c704cc12` (main, rebased onto it), not committed at the time; no flight. All figures measured
unless marked.

- Wine run of the pass fixture with the cold hand-over case (orchestrator, bottle X3, merged tree before the
  review fixes below): `RESULT PASS checks=122`, `HANDOVER frames=460 oversize=1 whole=1 early=0 latches=1
  upload_bytes=4260096 ready_ms=579.2 drawable_ms=579.2 fill_ms=576.4 fill_busy_ms=575.0 fill_cpu_ms=580.0`,
  both `cold_handover` atlases 0 differing bytes, check PASS with every gate true
  ([fixture-wine/](../../verification/results/fog-handover/fixture-wine/): `handover_rows.txt`, `run_tail.txt`,
  `check_out.json`, `commands.txt`). Pass executable `89c069f0…`: rebuilt on the merged tree, so not the
  worktree scratch build's `d56281998c73…` of the previous entry. The fixture was not rerun after the fixes
  below (the rebuilt executable `af8b6d79…` compiles, 0 warnings).
- R3: `--fog-handover-prefill` (default on). The CreateTexture/CreateVertexBuffer hooks poll at most once per
  250 ms while no Present has come for over 250 ms, walk the global object list tail (≤ 8 nodes, ≤ 24 reads)
  and start the far fill of a found fog sector at the sector origin; the first Ready sample confirms (same
  key) or discards; the transit's gap invalidation waits for that decision.
- Review fixes: the cards are armed in the cold-step frame (`FogCardPolicy::arm_on_cold_step`, after the
  density latch), so they are masked in the frame the medium reaches full density; the whole-atlas latch
  waits for a published box holding the need box plus the readiness guard, and the held worker extends
  toward it; `level_dirty(1)` is false during the fill until a box is published; a lost whole latch is taken
  again; the shared sample keeps the raw `anchor_check` (walk in `anchor_walk`, `anchor_refused()`); the
  `volumetric_fog_docked` line is capped at 256.
- Host witness `fog_handover_host.cpp` (`test_fog_handover`, 9 tests OK; 85 checks after the last round;
  [host_witness_out.txt](../../verification/results/fog-handover/host_witness_out.txt)): a new identity during
  the hold gets one whole latch of its own field; a lost latch is retaken (2 whole latches); the latch waits
  for a camera that moved 20 far nodes; the real worker thread parks and is woken (1 whole latch, far staging
  offered 0 times before it, resident = ready frame, settled, 1.1 s); the prefill hands over at frame 5 after
  three extension slabs and one latch and settles to the destination field; the walk accepts the tail (10
  reads) and the eighth node (24 reads), is cut at eight (17 reads), refuses every listed condition; the gate
  and the decision as specified; the card arming arms, faults on a failed frame, respects refusal.
- Host modules: the 29 fog/volumetric/sector modules (249 tests) and the 93 modules that read the touched
  files (1,136 tests), green via `run_host_suite.py --modules`.
- Build: game guard `[]`; `cmake` scratch build `build-fh-r3`: 0 warnings, `d3d9.dll` `77cad0d566b66b79…`;
  `check_no_x87.py` PASS, 663 reachable (as on main). Touched functions carry only the two existing x87
  sites (`float(elapsed)` in `DensityCache::step`, the `far_ready` log line;
  [x87_lines_out.txt](../../verification/results/fog-handover/x87_lines_out.txt) with the build, objects and
  filters in its header lines); the audit is redone at the gate on the then-current base.
- Fixture rerun on the R3 tree (orchestrator, bottle X3, gate build): `RESULT PASS checks=122`, `HANDOVER frames=651
  oversize=1 whole=1 early=0 latches=1 upload_bytes=4260096 ready_ms=815.8 drawable_ms=815.8 fill_ms=814.2
  fill_busy_ms=812.1 fill_cpu_ms=800.0`, check PASS; pass executable `3a748f3c57fd…`, shader fixture `27457bf3aa01…`
  (`fixture-wine/`: `handover_row_r3.txt`, `check_out_r3.json`, `run_tail_r3.txt`).
- Last review round (R3): the thread is checked before the 250 ms slot is taken; `DensityCache::prefill`
  returns whether the camera was posted and the poll logs `started` only then (else `not_posted`, retried);
  the resident field's key is skipped (`current_key`); the walk's worst case is a `static_assert` against the
  25-read budget; a MotionOutput-level case in `test_fog_cards` runs the production `fog_prefill_confirm` in
  `volumetric_fog_sector_sample` (gap held while pending, confirmed keeps, discarded invalidates once, a later
  gap invalidates as before); the note states the creation-hook envelope paid without telemetry.
- Open: a flight with two gate transits into fogged sectors (the lead time: `stall_ms` of the first `found`
  poll against the stall length; confirmed/discarded; the hand-over line), one docking at a station and in
  a carrier; creations per frame for the hook envelope's cost; the whole-atlas frame's cost under `--gpu-sync-timing`
  (`FogFill`, estimated 0.5–2 ms copy plus 1–3 ms upload by the reviewer, not measured).

## Run 73 B (run273): four hand-over gaps, fixes and the card-refusal diagnostic (2026-09-23)

[fog-handover.md](../architecture/fog-handover.md), "Run 73 B findings and fixes". Worktree on `e8615d05`
(main), not committed at the time; no flight yet. Triage evidence and the query written for this entry:
`verification/results/run273-fog-bolts/` (`handover_gaps.py` + `_out.txt` over the 325 MB session log, rows
only). All figures measured unless marked.

- Cause shared by B, C and D: the arrival frame's owner latch posts the previous scene end's camera, the
  source sector's position; the worker fills a first far box there (1.09 M nodes) and again at the real
  position one frame later. Frame 16258: 2,349,664 nodes at the latch, twice a first fill of 1,092,727 (the
  "~700 ms outside the fill"; `handover_gaps_out.txt` node deltas); frame 24765: 2,184,796 nodes after arrival although
  the confirmed prefill had filled the origin box (discarded by the stale retarget). Fixed: the sample drops
  the camera on a sector change, a gap or a prefill decision (the arrival latch posts nothing) and the cache's
  invalidate forgets the posted camera (`request_.camera_valid = false`, `posted_need_` emptied).
- A (19555, bluewell -> bluewell): no `sample_gap` because the stall frame's `no_cockpit` sample kept the
  frames consecutive; the poll skipped the resident key (`current_key`); warm `residency` ramp, epoch
  19556 -> `far_ready` 19676: 120 frames, `ms=3460.0`. Fixed: a Ready sample of another sector object (token, or `[sector+8]` id) with the same key is a
  cold start (epoch `transit`); the prefill re-centres the resident key at the origin as a cold start
  (`recentred`), confirmed keeps it.
- B (24765): not re-keyed (the `sector_key` epoch line was the proxy's key copy; the cache's configure was a
  no-op); the loss was the stale post. Fixed: a confirmed prefill adopts its key (`adopted=1` on the confirm
  line), the prefill start is an epoch (`prefill`), and under the cold hold the far box grows to the far target
  only (host witness `PREFILL_ADOPTED`: 795,144 nodes to the latch for the 228 km arrival against 1,103,336
  for a first fill; window-edge slabs by the same geometry 1,372,189, `run273-fixes/window_edge_slabs.py`). Expected: about 0.44 s of worker time plus one latch
  (inferred at 1.8 M nodes/s) against 1.15 s measured.
- C (33817, docked load): the first card of every frame from the cold step (33860) to the undock (34244) was
  refused before or at the readiness check; the scene end had passed every component but `density_drawable`
  in 33817-33859; the docked view in flight (31504-33815) was masked 7/7 a frame. Not pinned from the code
  (a field diff of every row type between 31510 and 33870 shows nothing the gates read). Delivered: the
  `volumetric_fog_cards` line names the frame's first refusal (`refusal=` gate or readiness component),
  same checks in the same order; the next docked load settles it.
- D (16258, new game): the prefill found bluewell 7.1 s into the 12.6 s stall but had no worker (`not_posted`).
  Fixed: the poll constructs the `FogPass` and `prefill_density` starts the worker (no device call), takes the
  switches from the proxy; `attach` keeps an existing worker. The 612 ms frame 16307 was not the latch:
  599 ms in `pre_render` (engine time before BeginScene), slowest hooked call 1.9 ms, `views` 12.8 ms; the same
  shape at 19596 (707 ms) and 24847 (188 ms), 1.2-2.4 s after each arrival (inferred: the autosave). The
  latch stays whole; the docked load's latch frame was 12 ms. Expected first fog frame on a new game:
  arrival + 2 frames (`NEW_GAME_PREFILL`).
- Host: `test_fog_handover` (98 checks after the review round, with the poll plan cases: `INVALIDATE_PARKS`, `TRANSIT_COLD` largest step 1.000,
  `RECENTRE_PREFILL` stepped 5 frames after a 20 km arrival, `PREFILL_ADOPTED`, `NEW_GAME_PREFILL` latch in
  the first posted frame and ready 1.000 in the next) and `test_fog_cards` (`run273_transit`,
  `run273_prefill_adopt`, `run273_card_refusal named=12` with `gate:`/`ready:` prefixes and the cards line's change
  key): 12 tests OK; `test_sector_background`,
  `test_fog_density_cache`, `test_volumetric_fog` (27 tests OK); `test_fog_route_bridge`,
  `test_fog_density_shaders`, `test_fog_sector_policy` (14 tests OK). `test_launcher_switches` does not exist;
  the launcher switches are `test_fog_handover.FogHandoverWiring.test_launcher_switches`.
  [run273-fixes/host_witness_out.txt](../../verification/results/fog-handover/run273-fixes/host_witness_out.txt).
- Review round (F1-F8): figures re-derived from tracked files, the poll's allocation documented and bounded to one
  attempt per stall (`prefill_refused`), the prefill gated on strength and its worker released on attach refusal, the
  flown sector's token keeps the field (`fog_prefill::plan`, `current_sector`), the refusal name in the cards line's
  change key, prefixed refusal names; rebuilt `d3d9.dll` `3b1f82172b11df4e…`, 0 warnings, x87 671 reachable / no
  violations; fixture rerun PASS (rows below refreshed from it).
- Build: scratch `cmake` (MinGW i686, RelWithDebInfo) 0 warnings, `d3d9.dll` `251342ea732eb5c6…` (first round);
  `check_no_x87.py` 671 reachable, no violations.
- Fixture (bottle X3, `wine_lock.py`, rerun after the review round): `RESULT PASS checks=122 failures=0`, `HANDOVER
  frames=497 oversize=1 whole=1 early=0 latches=1 upload_bytes=4260096 ready_ms=621.9 drawable_ms=621.9 fill_ms=619.7
  fill_busy_ms=617.9 fill_cpu_ms=610.0`, check PASS with every gate true, pass executable `051f107a37ca…` (first
  round: frames=390, ready_ms=583.2, `978d0d2bfe38…`)
  ([run273-fixes/fixture_rows.txt](../../verification/results/fog-handover/run273-fixes/fixture_rows.txt),
  `execution.json`).
- Open: the flight (Run 73 C or later) with a new game into a fogged sector, one same-family gate, one gate
  into another family and one docked save load: `refusal=` after the docked load, `recentred`/`adopted=1`
  lines, the hand-over `frames` per arrival; the arrival position during the stall (sector-transit-order
  row 8) for a two-frame hand-over far from the origin; the engine stall 1.2-2.4 s after arrival.

## 2026-09-23: fog route step A: sub-pass boundaries, repair census, early-out off by default (uncommitted worktree)

Design: [fog-gpu-cost.md, "Step A implemented"](../architecture/fog-gpu-cost.md#step-a-implemented-2026-09-23).

- Programs (measured, `verification/results/fog-gpu-cost/step_a_programs.py`, output beside it): 12/12 fog programs
  byte-identical to 72645b5e (march_look 425, march_grid 352, repair 510 slots); the early-out sits in
  `fog_density_field_inc.h` under `FOG_MARCH_EARLY_OUT`, which no program defines, so the eleven programs including
  that file were regenerated and only their provenance include hash moved. Review decision: defined, it cost 11-12
  slots in the loop (425 -> 436, 352 -> 364, measured in the first build of this step) and cannot fire at 1.0x.
- Fixture (bottle X3, `fog_density_shader_run.py build/run/check`, reference `/tmp/x3-run67-fog-ref`): shader fixture
  `RESULT PASS checks=30`, pass fixture `RESULT PASS checks=123 failures=0 state_restorations=1125` (122 + the census
  check), `check` PASS with 30/30 gates including `pass_off_bit_identical` (11/11 accepted look hashes),
  `candidate_T/S`, `slots_below_512`, `march_loops_kept` (measured, final tree). `step_a_fixture_identity.py` (output
  beside it) compares the shader fixture and the pass fixture against the committed summary: 568 figures compared,
  0 differing, 95 keys the committed summary predates (motes, grid, hand-over rows), 33 worker-scheduling counters
  that differ between two runs of identical code (listed VARIES). The recorded
  `verification/results/fog-density-shader/summary.json` is this run's (march shader hashes back to 72645b5e).
  An earlier run of this step with the early-out defined had the same 11 look hashes (it never fired).
- Census (measured): the odd 31x17 target needs the repair on 255 pixels; the repair's clip also drops the 130 whose
  march is exactly empty, and the occlusion query around the repair quad counted 125, equal to the CPU twin's
  non-empty count. The census therefore counts written repair pixels (a lower bound on marched ones).
- Not verified: native Windows; the early-out on a saturating column (off; no fixture case reaches T < 1e-4).

## Run 75 pre-qualification: transit identity and the camera drop (2026-09-23)

The Run75 candidate (from 72645b5e) failed the route bridge at `heap_token_change_keeps_key_cache_and_image`
(9,054 checks; bridge frame 695, token 0x1000 -> 0x7000, same index and family: epoch `transit`, refill).
Two regressions of 72645b5e, both fixed in the worktree (not yet a candidate):
- The same-family transit rule fired on any heap-token change. run273's 19555 jump (measured,
  [run273-fog-bolts/transit_identity.py](../../verification/results/run273-fog-bolts/transit_identity.py) + `.txt`)
  kept index 2 and changed the token (`119301a8` -> `6cb11818`) and the id at `[sector+8]` (2317 -> 3221). Rule:
  `fog_prefill::other_sector(id, last_id)`, both ids known and different; a token change with the same or an unread
  id keeps field, image and camera. The poll's `plan` uses the same rule (review F3). A same-id reallocation is
  inferred from the bridge's synthetic case, not observed in flight; the observed same-sector rebuild (docked load,
  4370 -> 3926) changed the id and is covered by the sample gap.
- Hidden behind the first abort: the stale-camera drop also fired on any token change, so the latch of a sector
  change without ids skipped `configure` and the bridge failed `sector_change_rekeys_whole_far_node_offset`
  (61 of 110 names). The drop now follows the transit rule (transit, gap, decided prefill).
- Host: host witness 102 checks (`transit_needs_another_known_sector_id`, `HEAP_TOKEN_CHANGE` at the cache level with
  the rule applied by hand, `plan_token_change_same_id/id_unread_*`); the production sample's wiring is
  `run273_transit` in `fog_card_motion_cases_inc.h` (`realloc=1`, `token=1` camera kept, `run273=1` one
  invalidation); `test_fog_handover` + `test_fog_cards` 13 tests OK, with `test_fog_route_bridge`,
  `test_volumetric_fog` 37 OK.
- Build: scratch MinGW i686 0 warnings, `check_no_x87.py` PASS 673 reachable, no violations.
- Bridge (bottle X3, `wine_lock.py`): PASS 35,865 checks + 4 exit checks, 110 names, none only in Run73 or only
  here, shadow/motes A/B 15/13/4 as Run73, no `transit` epoch. The 6f16dbf6 baseline builds with the repository
  harness again (`fog_route_bridge_build.py` skips the two `X3M_ROUTE_BRIDGE_BASELINE`-guarded includes in its
  include scan). Fog pass fixture: PASS 122 checks with `HANDOVER frames=462 whole=1 latches=1 ready_ms=577.8`.
  Rows: [run75-transit-identity/rows_out.txt](../../verification/results/fog-handover/run75-transit-identity/rows_out.txt)
  (`rows.sh`).
- Open: the bridge passes no ids, so production's one-frame-later re-key on a sector change (camera dropped by the
  id change) is not in the bridge.

## Run 278 (Run 75 A, 2026-09-23): hand-over fixes accepted, docked load pinned

User report: a new game into a fogged sector (undocked) and gate transits between fogged sectors, including
same-family, show our fog immediately; the docked save load still shows the engine fog until undock. The cards line
names it: `volumetric_fog_cards … observed=7 refused=1 ready=0 … refusal=gate:states` on 1,115 frames from frame
11692 (measured, grep of the session log; 12,638 frames `refusal=none`). The docked-at-load card draw fails the
render-state gate, not readiness; fix in progress.
## Run 75 A (run278): docked-at-load card refusal pinned to the states gate, z/cull variant admitted (2026-09-23)

Worktree change, not yet a candidate. Log `/tmp/x3-bottleX3-run278/session-20260923-222804-212.log` (Run75 DLL
from db13d929); rows and counts by
[run278-docked-states/rows.sh](../../verification/results/fog-handover/run278-docked-states/rows.sh) (`rows_out.txt`).
- Measured: `refusal=gate:states` on 1,115 `volumetric_fog_cards` rows, frames 11692 (the cold step's `far_ready`,
  `warmup=0`) to 12806, every one `observed=7 suppressed=0 refused=1 ready=0`; 12,638 rows `refusal=none`; at 12807
  the same 7 cards a frame are masked 7/7 (`mode=2`, pass `applied=1`) and no other per-frame row changes (draws
  159 -> 164 (frames 12692 -> 12812), `set_rt=4` on both sides, `rs_resyncs=0 rs_invalidations=0 sb_resyncs=0`, `state_hooks
  state_shadow=1`). No row carries the card's states (capture off: 0 `capture=1` frames, 0 `motion_route` rows).
- Inferred (engine side, sector-fog.md sections 4 and 10): the draw is the fog card (pair, declaration, shape and
  stream frequency passed; the same draws are masked after the undock) and its `ZENABLE`/`CULLMODE` carry the
  material's own text (`g_ZEnable 1`, `g_CullMode 2`) instead of the dust pass's override (0, NONE) in the scene
  the load sets up. Not verified in flight: the next docked load prints the refused vector.
- Fix (`fog_card_match.h`): `ZENABLE` 0 or 1, `CULLMODE` NONE or CW; zwrite 0, stencil 0, mask 7, alpha test 0,
  fill SOLID and the ONE/INVSRCCOLOR/ADD/no-separate-alpha blend stay exact (a masked card writes nothing under
  either variant; the replacement changes only `COLORWRITEENABLE`). Diagnostic `volumetric_fog_card_states`
  (the 12 fields) on the refusal path, at most once per 300 frames (`fog_card_states_log_frame_`).
- Host: `run278_docked_states admitted=6 refused=12 spaced=1` in `fog_card_motion_cases_inc.h` ((1,CW), (0,CW),
  (1,NONE) admitted hooked and unhooked with the in-flight card's calls: 2 mask sets, 12 or 0 state gets; z 2,
  cull 3, cull 0, zwrite 1, stencil 1, alpha test 1 refused `gate:states`; the row's spacing 300 frames, one row
  per frame at most); the policy driver's per-member loop (+2 refused for every member, +1 admitted only for
  z and cull, the docked vector, zwrite 1 refused); the four fog modules 37 tests OK (`test_fog_cards` 3,
  `test_fog_handover`, `test_volumetric_fog`, `test_fog_route_bridge`).
- Build: scratch MinGW i686 RelWithDebInfo, 0 warnings, d3d9.dll 55,968,044 bytes; `check_no_x87.py` PASS,
  673 reachable functions, no violations.
- Fog pass fixture (bottle X3, `wine_lock.py`): PASS 123 checks, 30/30 gates, `HANDOVER frames=483 whole=1
  latches=1 ready_ms=605.6`, 51.4 s. The first attempt idled after writing all 143 images until the 540 s
  runner limit (the hang the Run75 record notes); the orphan fixture was killed and the rerun passed.
- Route bridge (bottle X3, `wine_lock.py`, checkout harness for both builds): PASS 37,281 checks + 4 exit checks,
  110 names, none only in Run73 / Run75 or only here, shadow/motes A/B 15/13 as before; against the qualified
  Run75 log (36,585) the six per-card names (`native_card_forward_once`, `native_card_LastError_preserved`,
  `once_only_hook_fallback_guard`, `one_fog_transaction_maximum`, `route_RT1_RT2_bytes`,
  `route_pass_state_restored`) each count +116, a uniform frame-count difference between runs (inferred), not a
  changed verdict. Timings: baseline 2.8 s, bridge 18.6 s, exit 43.4 s; the lock waited 1,503 s behind two other
  agents' runs. Tracked `verification/results/bottle-X3/` unchanged.
- Not verified: the refused vector in flight (the next docked load's `volumetric_fog_card_states` row) and native
  Windows.

## 2026-09-23: fog route step B: 24 far bins behind `--fog-far-bins` (uncommitted worktree, default 40, not installed)

Design and numbers: [fog-gpu-cost.md](../architecture/fog-gpu-cost.md), "Step B implemented". [M] unless marked.

- Law: `FOG_FAR_BINS` in the look law (default 40), far step (L - 12000) / 24 = 4187.5 at the 112,500 cap (1.02 far
  nodes), loop 48; range, cap and taper unchanged. Programs `fog_density_{march,repair}_look_far24` 425 / 510 slots,
  15 / 20 texture instructions, rep 48; all 12 default fog programs byte-identical to b4e2fcff
  ([step_b_programs_out.txt](../../verification/results/fog-gpu-cost/step_b_programs_out.txt)).
- Switch: launcher `--fog-far-bins {40,24}` → `X3M_FOG_FAR_BINS` (always written; 24 only with the stored range, refused
  with `--fog-shadow-pass on`); `FogDensityConfig::far_bins` picks the march/repair pair at `prepare_density`, any other
  count `E_INVALIDARG`; one `volumetric_fog_far_bins … requested=<value> refused=none|shadow_pass|cap|invalid` init row
  and `far_bins=` on the cache config row.
- Review fixes (same day): a count change builds the new pair before dropping the old and keeps the working pair on
  failure (sticky `far_bins_refused=program`, composite not re-created); FogPass clamps 24 to 40 itself from the first
  shadow-pass prepare until detach and above a 120,000 column cap (`fog_far_bins_refused` logged once); a hand-set
  value other than 40/24 is echoed as invalid. The variant's record moved to `fog-density-shader/far24.json`
  (summary 32.7 KB).
- Fixture after the review fixes (bottle X3, `wine_lock.py`, child 109 s): PASS 37/37, pass fixture 142 checks of which
  19 far-bin; identity against b4e2fcff and the deviation table unchanged from the first run below. Route bridge
  (`fog_route_bridge_build.py` build + `--baseline` from an archive of 6f16dbf6, `build-exit`, `run --cases
  /tmp/x3-fog-family-gpu-inputs-final/cases.txt`, child 81 s, `check`): PASS 37,155 checks, 110 names, exit 4 checks,
  `legacy_bit_identical_to_baseline` true (`verification/results/fog-density-route/summary.json`).
- First fixture run (bottle X3, `wine_lock.py`, lock wait 337 s, child 95 s; shader 51 s, pass 43 s):
  `fog_density_shader_run.py build/run/check --reference /tmp/x3-run67-fog-ref --variant-reference
  /tmp/x3-run76-fog-ref-far24` PASS 37/37: the 30 default gates incl. `pass_off_bit_identical` (11/11 hashes), 0 of 662
  default figures differ from b4e2fcff's summary
  ([step_b_fixture_identity_out.txt](../../verification/results/fog-gpu-cost/step_b_fixture_identity_out.txt)); the 7
  `far24_*` gates against the variant's own host reference (T max .00073, S max .00045; repair .00051); pass fixture 136
  checks incl. 13 far-bin checks (CPU twin at 24 bins T .00054 / S .00011, back at 40 byte-identical, same 322 device
  calls, count 32 refused, Reset, detach). The exporter at 40 bins reproduces all 37 look/repair/grid arrays of run67.
- Look move, far24 against 40 on the GPU (RGBA16F, fogged pixels): T max .010-.030, mean .0014-.0046; S max .007-.013,
  mean .0004-.0008; 12-48 % of fogged pixels past .003; signed mean T within ±.0006 (unbiased resampling)
  ([step_b_deviation_out.txt](../../verification/results/fog-gpu-cost/step_b_deviation_out.txt)).
- Build: scratch MinGW i686 RelWithDebInfo 0 warnings, `check_no_x87.py` PASS 673 reachable, no violations. Host:
  `test_fog_density_shaders`, `test_volumetric_fog` (new `test_far_bins_option`), `test_fog_route_bridge`,
  `test_fog_look_reference`, `test_fog_shadow_grid` 45 OK; `test_comparison_hotkeys`, `test_fog_cards`,
  `test_fog_field_assets`, `test_fog_family_file`, `test_fog_handover`, `test_gpu_sync_timing`,
  `test_sector_background` 48 OK; after the review fixes the twelve modules together: 93 tests OK.
- Open: the flight (same stand, `--gpu-sync-timing`, `--fog-far-bins 40` then `24`; `fog_march` and the look).

## Run 283 (Run 76 C, 2026-09-24): docked-at-load card pinned to alpha test, admitted (uncommitted worktree, not installed)

- Flight (Run76 DLL from 02b34ace, `/tmp/x3-bottleX3-run283/session-20260924-003849-212.log`): the refused vector
  at frames 961, 1261 and 1561 was identical, `z=0 zwrite=0 atest=1 blend=1 mask=7 cull=1 stencil=0 fill=3 src=2
  dst=4 op=1 sepalpha=0`; 829 frames `refusal=gate:states` (961-1789), 901 `refusal=none` after the undock
  (measured, [rows.sh](../../verification/results/fog-handover/run283-docked-states/rows.sh), output beside it).
  The only state differing from the in-flight card is `ALPHATESTENABLE` 1; z/cull were the dust pass's override.
- Fix: `FogCardStates::matches()` admits `ALPHATESTENABLE` 0 or 1 (2 and unknown -1 refuse). A masked card writes no
  colour (the replacement forces `COLORWRITEENABLE` 0), zwrite 0 and stencil 0 stay exact, so alpha test only
  discards fragments of a draw that writes nothing; the composite sets its own states in a state block; pair,
  declaration, shape, scene and frequency gates unchanged; the z/cull admission of 79ebac48 stays
  ([fog-handover.md](../architecture/fog-handover.md) case C, run283 paragraph).
- Host: `run278_docked_states admitted=8 refused=22` (the run283 vector admitted hooked and unhooked; zwrite 1,
  stencil 1, alpha test 2, blend 0, mask 15, fill 2 refused from the run283 base), the flip loop's refused alpha-test
  value is 2; `test_fog_cards`, `test_fog_handover`, `test_volumetric_fog`, `test_fog_route_bridge` 38 tests OK.
- Build: scratch MinGW i686 RelWithDebInfo 0 warnings, `check_no_x87.py` PASS 673 reachable. Fog pass fixture (bottle
  X3, `wine_lock.py`, lock wait 44 s, child 105 s): `--reference /tmp/x3-run67-fog-ref --variant-reference
  /tmp/x3-run76-fog-ref-far24` PASS 37/37 gates, pass fixture 142 checks 0 failures. Route bridge (build, `--baseline`
  from the 6f16dbf6 archive, `build-exit`, `run --cases /tmp/x3-fog-family-gpu-inputs-final/cases.txt`, child 79 s,
  `check`): PASS 36,549 checks + 4 exit checks, 110 names, 0 FAIL, `legacy_bit_identical_to_baseline` true.
- Open: a docked save load flight with this matcher.
