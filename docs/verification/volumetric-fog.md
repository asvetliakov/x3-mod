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
