# Status: session sections through 2026-09-19 (archived 2026-09-20)

Moved verbatim from `docs/status.md`. Relative links were written for `docs/`.

## Session 2026-09-19: four changes merged, run43 candidate installed

- Merged after their remaining reviews (Fable second review each): shimmer
  fixes, `--cull-small-parts` with the scope A/B the user asked for (run131
  rows: `all` 403 draws, `bodies` 395 at 2 px; the 8-draw difference is
  flag-derived, the rows carry no parent link), `--collide-box-cull` (margins
  and open points in [sector-collide.md](reverse-engineering/sector-collide.md)
  §10.1), the route bench and lease trim.
- The first candidate qualification caught a regression the reviews missed:
  the fade route's behind-camera `origin_distance` also fed shadow-caster
  admission (`seam-ownership-shadow-pool-hull` 2 admitted for 1). Fixed with
  `origin_distance_front` for the shadow caller; three host-suite drifts fixed.
- [route-per-draw-cost.md](architecture/route-per-draw-cost.md) ratified:
  lever 1 stage A + 2a (ownership-wrapper bypass for value-only calls, failing
  HRESULTs still observed on a cold path; light lock view) is the next proxy
  work item; lever 3 after it (fixtures, then one flight); 2b closed.
- **Run 43 read** (run133–139; outcomes in the [run table](verification/user-runs.md)): the collide box cull
  works but the 22 ms is the narrow phase on accepted pairs
  ([sector-collide.md](reverse-engineering/sector-collide.md) §11), census built; cull scope `bodies`
  saves nothing, `all` at 2 px takes 884 → 477 draws and ~30 → ~42 fps, now the default; the shimmer
  persists with every draw routed: resolve-side ripple on jittered sub-pixel geometry
  ([motion-output.md](verification/motion-output.md) "Run 139"), two default-off fixes built.
- Lever 1 stage A + 2a merged: wrapper overhead 2.15 → 0.77–0.92 µs per routed draw, lease 1.98 →
  0.73–0.90; stage B stays closed.
- Run 44 is queued ([user-runs.md](verification/user-runs.md) §44).

## Session 2026-09-18 (late night): run 42 read, four changes in flight

- **Run 42** (run129–132, outcomes in the [run table](verification/user-runs.md)): the 24 fps area is the
  sector collide routine `0x0045d250` (26 ms/frame flat, all-pairs loop with an x87 sqrt per pair,
  [RE](reverse-engineering/sector-collide.md)); the busy station has 403 of 901 draws under 2 px (9.55 ms)
  whose nodes carry zero size thresholds; View Distance High buys ≈ 1.5 fps (LOD lever closed); proxy-only
  route cost 9.7 µs per routed draw; residual shimmer = fade module behind the camera plane + the node's
  translucent sub-mesh (zwrite 0), both fixed on a branch; no other unrouted class exists.
- **Unmerged, in worktrees** (table with state and next step in the
  [handoff](archive/handoff-2026-09-18-late-night.md)): shimmer fixes + `unmatched=` reasons, `--cull-small-parts`,
  `--collide-box-cull`, per-draw trims + route bench; design note `route-per-draw-cost.md` in progress.
  Brief for that note: attribute and rank the three big proxy per-routed-draw pieces (ownership wrapper
  2.0 µs on the route's own calls, depth-replay lease 1.5 µs, hook-free lazy RT ≈ 2 µs; the current lazy
  mode is a net loss because it re-installs the light setter hooks), plus the hook envelope count per
  draw, with arithmetic at 830 draws, recommended order, acceptance evidence, what stays closed.
- Installed build unchanged (run42 candidate). Run 43 is drafted (§43), not flyable until the run43
  candidate exists.

## Session 2026-09-18 (night): run 41 read, linear default, fade route, engine levers

- **Run 41** (run123 A, run124 A2, run125/126 B, run128 C; outcomes in the
  [run table](verification/user-runs.md)): distant flicker gone under the linear
  encoding (±1 ULP flips 0 vs 7–20 % of far-cascade pixels); shadows ≈ 1.4 ms;
  ≥ 30 ms frames are the engine's > 800-draw view submission (28 of 33 ms);
  retention clean, K 1.5 confirmed; hull light-map gain accepted at 4.
- **Solar-panel shimmer** = the fade-band route inert under original shading
  since 2026-09-15 (probe gated on linear materials); fixed, fixture
  `seam-taa-fade-route-original` (resolved residual ≤ 0.055 px). An audit of
  every linear-material gate found no second functional instance
  ([asteroid-fog-temporal.md](reverse-engineering/asteroid-fog-temporal.md) "Run 125").
- **Engine frame time** ([design](architecture/engine-frame-time.md), ratified):
  the 24 fps corvette area is an 18 ms game-side pre-render episode (owner
  unknown; telemetry run 42 A attributes it); the busy station frame is 22 of
  32 ms view submission at 23.7 µs/draw; an offline census (world-scale proxy)
  puts 50–65 % of the 890 draws under 2 px (≈ 12 ms if culled) — the cull
  census hook sizes it from the engine's own measure in run 42 C; other levers:
  proxy per-draw hook work 1–3 ms, LOD bias (View Distance A/B, run 42 D).
  Closed with numbers: state filter, pass replay, instancing, threading.

## Session 2026-09-18 (evening): run 40 read, receiver precision, windows

- **Run 40** (run117 A, run118 A2, run119 B, run121/122 C; outcomes in the
  [run table](verification/user-runs.md)): period-2 blink fixed (23.8 % →
  0.79 %); 2048² maps accepted as default (replay 539 → 386 µs, c4 record cap
  never fired); the 150,000 extent is a half-extent (60 km box, 42 km corner,
  120 km deep along the sun; casters admitted to 44.6 km); corvette own radius
  449 u, K 1.5 holds it in C0 with margin; live retention clean over 34k
  frames; at-rest shadow cost ≈ 0.7 ms.
- **Far-station flicker** = RT2 fp32 z/w receiver precision (13.6 u per ULP at
  37 km; ±1 ULP re-rolls 9–16 % of far-cascade pixels on single-sided station
  faces; asteroids are closed, so the back-face rule saved them). Fixed behind
  the gated option; witness on the run117 captures: 9.1/14.2/15.7 % → 0/0.004/0 %
  ([ledger](verification/directional-shadows.md), "Run 40 A (run117) diagnosis",
  "Receiver depth (RT2 .b)", §5).
- **Near flicker (run119)** = a sun-grazing plane (|sun·n| 0.06) whose
  receiver-plane extrapolation amplified fp32 noise into ±3 u against a 2.18 u
  bias; slope margin fix 10.6 → 1.4 % per ULP ("Run 40 B (run119) near flicker").
  Open: a ±0.2 u per-frame along-ray receiver offset (game-side?), own-ship C0
  re-roll under a moving ship.
- **Hull emitters ≠ windows**: `--hull-emitters` reaches only the two additive
  guide-light programs; windows and hull lights are the light-map term inside
  the opaque race hull programs; `--hull-lightmap-gain` added.
- Tools: `shadow_map_diff.py` (basis-aligned; raw far-map "churn" was a
  moving-box artefact), `shadow_receiver_reroll.py`, retention summariser
  five-cascade fix, capture tools read 16 B/px RT2 dumps.

## Session 2026-09-18 (later): run 39 A read, reach and ship-size work

- **Run 39 A (run115):** sun poll on every frame, no cap hit, replay
  1.19 µs/draw + 34 µs, shadows 1–3 ms at rest, retention census clean through
  a gate jump and a load. Defect: a camera-following serrated band — the apply
  quad reconstructed receivers half a pixel off the RT2 sample (C1 over-bias
  71.9 % → 1.3 % corrected; sign verified by independent derivation). Stations
  at 5.6 / 12 km unshadowed: C3 reached 5 km. A2/B/C carried into run 40.
- **Reach:** five cascades 250 / 1,500 / 7,500 / 37,500 / 150,000 (30 km) at
  ratio 5 ([extents note](architecture/shadow-cascade-extents.md)); far
  cascades static-only with capital ships ≥ 1,500 u admitted (run115 extent
  census: nothing between 610 and 29,939 u), importance drop order with
  hysteresis, per-cascade records to 4,096; own-ship-adaptive C0 with the
  sliding ladder for the corvette/capital case. 2048² maps are run 40 A2.
- **Run 40 A (run116):** the band is gone and shadows reach 30 km; black areas
  flickered on distant lit surfaces — measured causes: a store/ring static-verdict
  feedback cycle (period-2 caster blink), far-cascade self-shadowing on the
  compare threshold re-rolled by the TAA-jittered receiver (fixed with back-face
  casters in cascades whose texel ≥ 8 u), thresholds too tight for far texels,
  and casters with the origin behind the camera refused everywhere; all fixed
  and installed. The static-only/capital rule is dropped from the run command
  (no cascade ever hit a cap). Open: the ~30 s distant flicker; K for the
  adaptive C0 from the corvette flight.
- **Run 40 A re-flown (run117, this build):** most flicker gone (period-2
  frames 23.8 % → 0.79 %, longest run 7); asteroids clean, lit station faces
  still flicker. Diagnosis on Fable: the raw far-map churn was a moving-box
  comparison artefact (aligned maps stable, no jitter leak, no duplicate
  admission; caster counts up because run116 refused ~230 far draws per frame);
  the residual is receiver precision — RT2 stores fp32 z/w, one ULP is 13.6 u of
  view depth at 37 km, and ±1 ULP alone re-rolls the factor on 9–16 % of far-cascade
  pixels on single-sided station faces ([ledger](verification/directional-shadows.md),
  "Run 40 A (run117) diagnosis"). Fix is an RT2 encoding change; design note in
  flight for ratification. Merged meanwhile: retention summariser five-cascade
  fix (the c4 record cap fired on 1.6 % of run117 frames), basis-aligned
  `tools/analysis/shadow_map_diff.py`, and the default-off FPS overlay
  (`--fps-overlay`, Ctrl+Alt+F7, [hotkeys note](architecture/comparison-hotkeys.md))
  for the next candidate. A2/B/C fly on the installed build.

## Session 2026-09-18: run 38 read, shadow system rebuilt

- **Run 38 root cause** (run111/112): the sun was read from PS register `c4`
  whatever program was bound; `c4` is `LightDir_Dir0` in 16 of 38 programs.
  42 % of frames had no valid sun (silent 250-unit fallback), 57 % a bogus
  (1,0,0) sun 107° off, 789 flips following the camera: the popping,
  wrong-direction and sliding shadows. Secondary: extent-cache thrash,
  near-plane clipping, half-texel lookup, engine view culling of off-screen
  casters. All fixed on main; caster counts from run 38 are void for
  calibration ([ledger](verification/directional-shadows.md), "Run 38 A").
- **Sun semantics** ([RE](reverse-engineering/camera-and-lights.md)): world
  space, object→light, positions are engine integers × 0.01, the sun ≈1.57e7
  units away; the engine's light array is polled hook-free per frame.
- **Run 38 B**: one stamp only, `prepare` 6.36 µs/draw bundled; the technique
  lookup measured offline at 0.007 ms/frame ⇒ trampoline dropped
  ([ledger](verification/sampling-profiler.md)).
- **Run 38 C**: 2 of 12 hull programs fired; 71 % of refusals were opaque
  routed draws that are never ONE/ONE; F6 was shared ⇒ own toggle and gain.
- Object-lifetime fixture: the ten FX-state failures were the environment
  (FXRSTOR does not reload x87 under FEX); control added, 0 failures.
- In flight for the next candidate: far-cascade caster policy (static-only
  cascades, importance drop order, per-cascade records), own-ship-adaptive
  C0 with ratio guard, toggle follow-ups, retention issue-check already
  batched (513 → 44.5 µs). Open: distant flicker every ~30 s (run 38 A);
  recheck in run 39.

## Session 2026-09-17 (latest): runs 36–37 complete, first shadows, experiments closed

- **Run 36:** builtin D3DX confirmed loaded and slower (+32 % BeginPass;
  closed). Session B (run106) showed no shadow; the apply path was proven
  correct offline (receiver-map residual within one unit, HDR darkening
  matches the twin): the view was backlit and casters were chosen by object
  origin, so only the own ship was in the map.
- **Run 37:** A1 FEX TSO off no measurable change (hardware TSO); A2 wined3d
  CSMT off doubles the draw call (+46 % frame): both closed. **B (run109):
  first visible shadows in game** (hull from station parts, ship on nearby
  parts) with geometry-chosen casters; station-on-station shadows need a
  larger box than 250 units: extent/depth/cap options and a scale-independent
  bias are in flight for run 38 at 4096 texels. C (run110): the H.264/AVI
  avatar file froze at the same post-create stage as MPEG-1 with zero blits
  witnessed: codec-independent Wine `amstream` block, video **parked**
  ([ledger](verification/media-cues.md)).
- **Merged:** residual-attribution stamp group (`--residual-phases`, arena
  24,576 B; review fixes pending), effect state classification (37 % constant,
  4.6 % expression-free passes), pass-replay assessment ratified (attribution
  fixture first, no replay yet), `proxy_environment` identity line, AO jitter
  term, video blit witness, `tools/media_transcode.py`, shader-fingerprint
  fallback design ratified (not scheduled), emitter phase 3 transform (under
  review, proxy wiring next).
- Concurrency raised by the user to six to eight agents.

## Session 2026-09-17 (late): run 35, shadow producer and apply pass, diagnostics

Merged on main after the run34 install (not yet in any installed build):

- **Media cues:** entry-side `media_cue_enter` line under `--media-cue-trace`
  (synchronous write before the allocator, 32/s limiter, `enter_suppressed=`),
  so a hung graph build names its cue (`221e384`; ledger
  [media-cues.md](verification/media-cues.md) §6).
- **D3DX experiment:** launcher `--d3dx native|builtin` (`c518720`). Run 35 A
  (`run103`) ran with the builtin override at the run95 view: visuals
  unchanged; the identity line could not prove which D3DX loaded (Wine keeps
  the native path and size for a mapped builtin, probe-verified), and the
  per-pass medians (BeginPass 8.90 vs run95's 6.64 µs) are confounded by the
  different build. `loaded_module` now reads the mapped image (`image_size`,
  `stamp`, `exports`, `wine_builtin`) and the launcher records its command and
  override string in `launcher-stderr.log` (`ccb1312`); the experiment repeats
  as run 36 A1/A2 on one build ([ledger](verification/sampling-profiler.md),
  run103 section).
- **Shadows:** the original-program share producer
  (`linear_material_original_sun_share_pixel_variant`, 108/108 admitted, GPU
  share error ≤ 1.9e-4 FP16 codes, `1139090`) and the scene-end sun-shadow
  apply pass (`SunShadowApplyPass`, synthetic fixture worst 0.998 FP16 codes,
  all skip and restore paths compared, `a41e028`) are reviewed and merged;
  neither is wired into the proxy yet. The lane latch without linear
  materials, cutout admission under original shading, bind-pair selection,
  scene-end order and `--sun-shadow-apply` are in flight
  ([contract](architecture/legacy-sun-application.md),
  [ledger](verification/directional-shadows.md)).
- **Housekeeping:** the run file holds only open runs; the 40 merged agent
  worktrees were removed (18 GB).

## Next user action

**Run 25 is received** as `/tmp/x3-bottleX3-run60` (60 referenced files). The
user confirms forward HUD alignment but prefers **centre as the default**;
forward remains opt-in for future camera tuning. The gate still resets the
view, as restoration is not implemented. Gate identity reconstruction continues. Loading retention is complete, while
every emitted sun-lane frame is refused by untracked-writer coverage; bounded
reason diagnostics are being added. No repeat is requested yet.

**Run 23 is complete**, preserved at `/tmp/x3-bottleX3-run54` (153 referenced
files), with four EV0/+1.5 far/near screenshots. The user reports brighter hulls
and subsequently chose **Auto capped at +1.0 EV and material fill 0.03** as
the new defaults. The changes are reviewed (34 focused tests) and the current
launcher now passes those values to the existing DLL. An affected dry-run
confirmed Auto / EV max 1.0 / fill 0.03; vanilla dry-run also passed. Direct
DLL fallbacks are included in the current build. The run54 brightness thresholds support fill, but
unmatched colour/reprojection evidence limits acceptance and does not establish
a measured 0.03 comparison. The user also approved a selective-exposure
feature: base hull shading at EV0, with reflections/specular/emission and other
scene effects still exposed. The [selective-exposure contract](architecture/material-selective-exposure.md)
is ratified: background-driven Auto, one compensated scene and one exposed-linear
TAA history. Create-time material variants are now independently reviewed and
host-qualified for all 137 covered stages; whole-program GPU and live integration
remain pending ([ledger](verification/material-exposure.md)). Following the
user-requested independent critique, further material runtime/coverage expansion
is held for a matched appearance comparison; the reviewed shader checkpoint is
retained ([allocation assessment](architecture/material-investment.md)).
The user excludes per-material roughness/metalness art review and has no planned
PBR/texture overhaul; automatic approximations remain unevaluated proposals.
See the [fill ledger](verification/fill-light.md) for numbers and limitations.
The [run queue](verification/user-runs.md) records the remaining telemetry request. K=0 qualified
4,177 cases with every recorded baseline row bit-identical; live qualification
passed 8 cases / 32,516 checks. The 23-case fill oracle has pre-target scene-linear
luma error 0 FP16 codes, encoded FP16 RGB error ≤1 code, and reconstructed
FP16-image luma error ≤3 codes ([ledger](verification/fill-light.md)).

Run24's gate portion is received as `/tmp/x3-bottleX3-run56`: the user confirms
a reset and did not use jumpdrive (no suitable save). The trace destroys the
old cockpit, creates generation 2 with a different ship pointer, then requests
mode 1 at script PC `0x000f0794` before the new sector is visible. The ratified
same-lifetime restore predicate would cancel; targeted reconstruction must
establish safe cross-recreation identity before implementation. Static analysis
also finds persistent script-mode and geometry resets; mode-only restoration
would be incomplete. The [gate reconstruction](reverse-engineering/chase-view-transition.md#2026-09-15-run24-gate-reconstruction-snapshot-run56)
now has reviewed identity/provenance/geometry diagnostics in source: 194 host
checks, nine EXE sites and 334 X3 CPU checks pass. They are installed and are being analysed against run60; restoration itself remains unimplemented. Jumpdrive shares
the static warp path but remains gameplay-unverified.

The chase HUD anchor (`centre` default) is now implemented and independently
reviewed in source, and installed in the consolidated candidate ([ledger](verification/chase-hud-anchor.md));
81 host tests pass; run60 confirms alignment at the current camera settings. The gate trace above now drives the view-restoration prerequisite. Shadows follow the ratified
route-B order: sun-lit-share lane, replay feasibility without shading, then cascades;
screen-space shadows are fallback only. The [sun-share extraction contract](reverse-engineering/sun-share-material-contract.md)
now covers all 108 pixel originals; the receiver/capability boundary is ratified
in the shadow note. The default-off sun-share runtime lane is now integrated and
independently reviewed: 216 extraction GPU cases, portable depth-history copying,
and 11 actual-renderer cases / 66 byte-exact TAA frames pass. Composition
exclusions, fallback and Reset are qualified within the recorded boundaries;
missing shader variants remain unavailable through Reset. Run60 refuses all 8,950 emitted frames due to untracked writers; identifying
those populations is the next coverage step. Shadow replay and cascades remain unimplemented
([ledger](verification/directional-shadows.md)).
The inherited PS3 constant-read-port violation is repaired and reviewed in
source: legacy/fill GPU reports and 1,162 fade readbacks match retained
baselines exactly ([repair ledger](verification/linear-material-constant-port.md)).
Native Windows runtime remains unverified; this repair is now installed. Replay admission has a ratified managed-buffer
feasibility boundary. Optional buffer-lock observation is now reviewed in source:
124 X3 fixture checks pass, including seven cold native-thread cases. It is not
installed or connected to replay; complete entry coverage and concurrency proof
remain required before activation
([counter contract](reverse-engineering/replay-native-callbacks.md)). Existing loading-phase evidence bounds the stall at 21.702 s: file opens and
mesh processing are large measured counter totals, but overlapping timers leave
exact wall-time attribution open. The bounded interval recorder is now independently
reviewed and qualified in source (163 X3 CPU checks); run60 retains all 11,867 records over a 7.279 s load, with a 1.670 s wrapper
union. This different loading sequence does not explain run48
([verification](verification/loading-intervals.md)). The
existing counter analysis remains limited by overlap ([analysis](reverse-engineering/loading-observations.md#run-48-bounded-attribution-of-the-save-load-interval-2026-09-15)). Completed runs remain in the
[run queue](verification/user-runs.md) and its archive.

## Session 2026-09-15 (resumed): decoupling, restore implementation, diagnostics

The user now plays **without linear materials** and prefers that look; no
further linear-hull processing is planned. Decisions this session:

- `--screen-emission` is decoupled from `--linear-materials` and merged
  (`409317e`): the only real dependencies were option gates plus the stage-0
  sRGB sampler shadow, now fed by the option; the DLL gate also requires
  ownership. Two independent reviews found no blocking issue; the no-materials
  fixture cases match the materials-on twin and the newly active sampler hook
  adds no measurable cost ([audit](architecture/linear-material-decoupling.md),
  [ledger](verification/screen-emission.md)). Distance fade, the sun lane,
  material gains/fill and the cutout route remain linear-material features.
- `--linear-emissions` in its full-surface exchange shape is not pursued; its
  gameplay cost was never measured. If brighter emitters are wanted, the first
  step is a source-only encoded gain ([cost note](architecture/linear-emission-cost.md)).
- Chase HUD: keep `centre`; the next run tries `--chase-pitch-down-deg 0.5
  --chase-offset-y 0.50` so the forward vanishing point sits at screen centre
  ([survey](architecture/chase-hud-reticle-survey.md)).
- The run60 gate-restore contract is ratified and `--chase-view-restore`
  (default off) is implemented, deep-reviewed on Fable and merged (`123f98d`):
  seven byte-verified sites, 722 X3 CPU checks / 0 failures
  (`verification/results/chase-restore-cpu.json`), idle seam prefilter
  0.004 µs. Not installed; gameplay reads `chase_view_restore_state`.
- Sun-lane refusal diagnostics were recreated (the earlier unmerged worktree no
  longer existed), reviewed with fixes and merged (`7f23195`): 16 reason buckets,
  64-entry writer signature cache, run60 log parses identically.
- Emitters above 1.0 without brackets, both reviewed and merged: `--emission-source-gain G`
  (`3ded947`, one MUL on the 20 additive emission pairs, requires `--hdr` only,
  excludes `--linear-emissions`; 80 GPU cases exact) and `--screen-emission-additive G`
  (`c40ee94`, the nine bullet pairs drawn in place with DESTBLEND ONE and a ps_2_0
  gain variant, requires `--motion-output --hdr`, exclusive with `--screen-emission`;
  bolt pixels above 1.0 within one FP16 code). The user prefers this over exact
  blend law ([cost note](architecture/linear-emission-cost.md),
  [screen-emission note](architecture/screen-emission-region.md) "Additive option").
- Point-light admission site identified (`0x004c27a1`–`0x004c27af`, per node,
  range 1000 world units; run-22 outcomes reproduced); no engine patch now
  ([note](reverse-engineering/camera-and-lights.md)). With original hull shading
  the docking-module darkening is native behaviour again (fill exists only in
  converted materials).
- Root-object point-light admission (`--point-light-root-admission`, default-off,
  six-byte site `0x004c27af`, per-(node, light) frame memo) is implemented,
  deep-reviewed and merged (`9ebcc2a`; 134 CPU checks, admit path native
  byte-for-byte; [ledger](verification/point-light-admission.md)). The
  linear-light fill inside the original programs (§1a option C) is in flight.
  Both ride the next candidate with the F8 baseline capture.
- Sun lane: the run66 blocker is fixed lane-only (`444478a`: alpha-tested opaque
  receivers admitted, only depth writers veto). One-cascade depth replay
  (`--shadow-replay-depth`, default-off) is implemented, deep-reviewed and merged
  (`46dc822`): map vs CPU projection ≤1e-4, byte-identical presented twins,
  ≈40 µs + 1.3 µs/draw, no consumer yet; open before any consumer: alpha-tested
  casters write full-quad depth, DEVICELOST mid-transaction convention.
- Run 27 done as run68 (2026-09-16): engines brighter, halo from the shared
  effects PS `8360f422…` now gained (split merged `a26eb9b`: 5 engine pairs under
  `--emission-source-gain`, 15 effect pairs under new `--effect-source-gain`,
  default 1; split undone 2026-09-16 in the worktree change "Screen
  substitution", `linear-emission-cost.md`); restore consumed on transits 1 and 3 but never re-armed on the
  fresh generation (fixed `220d2e9`: bounded precondition retry, refusal
  samples); mip bias -0.5 / sharpen 0.75 now defaults (`ca6ad2e`); point-light
  telemetry merged (`7521b79`), option kept but dropped from the run command
  (the cliff is barely visible under original shading). Run 28 candidate `2b0969e5…`
  from `a26eb9b` is installed; [run 28](verification/user-runs.md) is queued
  with the original-fill A/B. Open: the bolt halo reproduces with the bolt
  option alone (user test on the run27 build), so the effect gain is not its
  cause; leading suspect is the 1.0→1.3 EV ceiling (test `--hdr-ev-max 1.0`); the original-program fill
  (`--original-fill K`, option C) is reviewed and merged (`d864246`: 108/108
  programs, exact-power law, K=0 byte-identical, 92 GPU cases per K within one
  FP16 code) and rides the following candidate if the F8 baseline shows dark
  faces; its performance pass is the on/off `frame_end` delta in that run.
- Shadows: [replay gates](architecture/shadow-replay-gates.md) ratified; the
  depth-replay fixture is not funded until a lane-independent caster-candidate
  counter (`--shadow-replay-candidates`, in flight) answers four predicates in
  the next run.
- Host suite: a census at `e15d499` found 11 pre-existing failures (stale mocks,
  stale wiring literals, a moved fade baseline); all repaired without production
  edits in `73772bf`. The canonical discovery run is green (1883 tests, 2
  build-artifact skips) ([ledger](verification/host-suite-2026-09-15.md)).

Run 26 was received as run65/run66 (see the goals table); its command was session A with original hulls,
both gains at 2, chase restore, replay candidates and loading intervals; optional
session B with linear materials for the sun-lane buckets.

## Usage-limit checkpoint (earlier 2026-09-15)

The user requested a stop after the gate contract and material critic finish.
Both reports are complete; no new build, install or user run is queued.

- Gate reconstruction now identifies the optimized script assignment boundary
  `0x4a3ffd` / CODE `0xf0c4b`, with a proposed one-use warp ticket and seven
  validated spans. It lets the engine regenerate camera geometry rather than
  copying old pointers. The [owning note](reverse-engineering/chase-view-transition.md#2026-09-15-run25-identity-and-restoration-boundary-snapshot-run60)
  and `/tmp/x3-run60-gate/proof.json` retain the contract. Implementation,
  CPU/rollback/cancellation fixtures and independent hook review remain pending;
  another telemetry-only run is not presently required.
- The [critic reassessment](architecture/material-investment.md#critic-reassessment-after-sparse-lighting-clarification)
  strengthens the existing material hold. Sparse lights and no PBR plan weaken
  blanket conversion value; existing emission controls may suffice. Original-base
  emission/shadows are conditional designs, not implemented alternatives.
- Sun refusal diagnostics are saved **unmerged and unreviewed** in
  `/tmp/x3-sun-lane-refusal-diag` (seven files). Ten focused tests passed; strict
  syntax must be rerun after the final enum/log-name rename, then independent
  review. Do not treat this worktree as a candidate or duplicate its work.
- Reviewed buffer-lock observation is committed as `4a708af`; replay stays off.
  Run60 observations are committed as `8fea701`. The three untracked fill result
  JSON files are existing local artifacts and should remain untracked.

## User decisions (2026-09-14)

- Fade route default-on accepted (`--no-linear-distance-fade` opts out).
- AO fixture cost accepted.
- No per-frame screen-emission bracket cap; the bound fix comes first.
- Screen emission step E contract: decode the accumulated native B once at
  publication, `--screen-emission-gain` default 1 = presented-frame parity.
- Cutout-miss exemption: known-blended draws no longer count as a cutout miss.
- Voice: plugin path only, no native-codec or bottle-clone route; fail early on
  missing audio if the plugin path fails.
- After run 19 (late evening): docking-port design note stays unratified and the
  linear rule unchanged until run 20 captures a real far state and a ship pair;
  `--screen-emission-gain` 1 stays the default (no gain-2 run); AO stays
  default-off and run 20 tests appearance with `--ao-radius 20` and `--ao-debug`
  instead of a code change.

## Open items

- **Target speech (resolved):** plays in gameplay with `--voice-decoder
  /tmp/x3-wma-plugin-v4` and the fixed DMO fallback hook (runs 16 and 18); the
  run-16 crackle was a full-scale wrap in CrossOver's stock converter, removed by
  the plugin v4 float limit. The plugin lives outside the bottle and the repo
  (untracked, backed up). Ledger [voice-decoder.md](verification/voice-decoder.md),
  sequence [voice-startup-sequence.md](reverse-engineering/voice-startup-sequence.md).
- **Selection pause (resolved):** no target publication over 10 ms in runs 16/18
  against run 28's median 463 ms. Other unexplained slow-frame residuals remain
  open; see the [33-site trace](reverse-engineering/selection-native-vm.md).
- **Fade-band objects tremble under TAA (run 21, owner found; fixed, see above):** the "station
  section" in `screenshots/jitter1.png` is an asteroid in its distance-fade band
  seen through the hangar gap. Its blended colour pass is refused at the motion
  gate, the fade route binds it as a region and that region becomes the reactive
  mask, so the resolve returns the raw jittered sample every frame (measured
  shift = Δjitter, up to 0.9 px) while routed neighbours are reprojected
  ([asteroid-fog-temporal.md](reverse-engineering/asteroid-fog-temporal.md),
  "Run 49"). Fixed in the installed build: fade-band draws of reviewed pairs are
  routed with their own motion rows above 500 ‰ of the program's own fade
  fraction (hysteresis 100 ‰), masked current-only below; fixture resolved
  residual ≤ 0.07 px against a raw shift equal to the jitter; run 22 confirms
  in game ([linear-distance-fade-region.md](architecture/linear-distance-fade-region.md),
  "Fade-band route").
- **Distant shimmer:** root cause named — `cutout::missed` on a source-over
  *blended* alpha-tested cutout draw refused at the motion gate drops the whole
  frame's TAA history (15 % of frames in runs 11/14, none in run 15). The
  exemption is merged and rides the installed build; pending run 19 confirmation.
  Run 19 confirmed the exemption (reason 3 at 0.01 %) but distant zoomed asteroids
  still lost triangles: owner found and fixed in the installed build. The engine draws
  fogged asteroids as a depth-only prepass then a blended draw; the route jittered
  only the second, so facets failed LESSEQUAL on the jitter side
  ([asteroid-fog-temporal.md](reverse-engineering/asteroid-fog-temporal.md), "Run 47").
  The z_only programs are now jittered with the scene (fixture: zero holes, control
  frames drop out); run 20 confirms in game and its `unjittered_depth_writers`
  counter names any other unjittered depth writer. Ledger
  [motion-output.md](verification/motion-output.md).
- **Docking-module darkening (owner found, run 22):** the station module goes
  black at range because the player ship's white point light (the only point
  light in the session, attenuation 1/(1+0.01d)) is culled per node by range:
  `g_nNumLightPoint` flips 1→0 on the ten module nodes between ≈1150 and ≈1350
  world units while the large body node keeps it. With the sun in front of the
  camera and no ambient term, the camera-facing module faces have no light and go
  black; the sun-lit body is unaffected. Witnessed on the run-51 pair (same LOD 0,
  same pairs, textures and constants; only the light count differs; module luma
  ratio 1.29 near/far, dark fraction 0.25 → 0.02). Native behaviour, hence vanilla.
  Earlier LOD-step and fade explanations are withdrawn. Fix direction: an
  ambient/fill term in the converted materials (one MAD per pixel, also lights
  black bays and night sides), ratified, merged and GPU-qualified (see the fill ledger above).
  Widening point-light range is rejected; optional root-object admission follows
  the fill verdict and starts with disassembly.
  [station-material-distance.md](reverse-engineering/station-material-distance.md),
  "Run 22". `--lod-scale` is installed default-off and stays so: the user
  compared 3× against 1× and saw no visual difference (View Distance "Very
  High" already biases the LOD index); without the option nothing is patched
  ([lod-scale.md](architecture/lod-scale.md)).
- **Fade-band trembling:** fixed and accepted in run 22 (860 fading draws routed,
  zero holds, no trembling reported).
- **Bullets / screen emission:** step E (publication-time decode, parity within
  one FP16 code) and step D (per-draw vertex hull, fan batch 1.2 % of the viewport
  vs 89.6 % for the box; derive 1.7–27 µs per draw, sentinel fill 17 µs per lock)
  are installed; run 19 accepted the bolts at gain 1 and run 20 showed step D
  working (hull a third of the box on firing frames, no fullscreen bracket); run
  21 refuted the dimming (witness every frame, zero pixels outside the hull).
  Step D accepted in game. Ledger
  [screen-emission.md](verification/screen-emission.md), design
  [screen-emission-bullet-bound.md](architecture/screen-emission-bullet-bound.md).
- **Ambient occlusion (closed 2026-09-15):** step 2 stays installed and
  default-off behind `--ambient-occlusion`. Runs 19–21 showed the term invisible
  at gameplay distance at 2, 20 and 100 m (`radius_px = 256·radius_m/distance_m`,
  cap 64); the user decided AO stays off (open-space games do not read by AO
  either) and no sun-weighted AO v2 follows. Cost when off is zero (never called
  without the option; one early return when toggled off). The chain remains only
  as a base if the directional-shadows design chooses screen-space shadows.
  [ambient-occlusion-scale.md](architecture/ambient-occlusion-scale.md); design
  [ambient-occlusion.md](architecture/ambient-occlusion.md), ledger
  [ambient-occlusion.md](verification/ambient-occlusion.md).
- **Distance fade:** default-on in the installed build; the witness stayed clean
  in runs 11/14/15 (0 covered pixels outside the derived rectangles, 0
  full-viewport fallbacks). Gameplay acceptance of the default rides run 19.
  [linear-distance-fade-region.md](architecture/linear-distance-fade-region.md).
- **Exposure meter under `X3M_HDR_DECODE=none`:** the GPU level-0 value
  disagrees with the CPU reference beyond 1e-4 only in that decode mode, so the
  pass refuses the meter (`meter_reason=self_test`) and Auto exposure is off
  there; every other decode gives `ok`. Not a default configuration; open, not
  yet owned.
- **Bloom/exposure:** the installed correction uses gain 0.375/scatter 0.65 and
  the +1.0 EV Auto ceiling is the current user-selected default. The meter still mostly
  reaches its ceiling; physically informed adaptation remains unproved.
- **Material appearance and coverage:** exclude accidental loss of native gloss
  terms before artistic tuning. The [coverage ledger](architecture/material-coverage.md)
  accounts for all 817 archive pass identities; older profiles, transparent,
  background and other scene writers remain beyond installed coverage. The
  [glass extension](architecture/glass-materials.md) adds six reviewed SM3 pairs
  (168 pairs / 137 originals), preserving native gloss/Fresnel; gameplay
  acceptance pending.
- **HDR scope:** FP16 and AgX work, but much of the scene is still
  compatibility-decoded gamma-space lighting. Scene-referred lighting, complete
  linear blending and verified HDR display output remain incomplete.
- **Native Windows:** C++ source cross-compiles and the known PS3 sanitizer
  constant-read-port violation is repaired with CrossOver GPU parity evidence.
  Native-Windows runtime is unverified. Depth provision, CreateDeviceEx
  adoption, MRT/PS2.1, Reset/presentation, performance and HDR output remain gaps
  ([platform-portability.md](architecture/platform-portability.md)).
- **Window/cursor:** the macOS menu bar remains open. The double cursor after
  alt-tab reproduces in vanilla (run 4, 2026-09-14: move the desktop cursor
  outside the window before alt-tabbing back), so it is not a proxy regression;
  recipe and reading in [window-and-cursor.md](architecture/window-and-cursor.md).

Keep raw captures and builds local, use focused verification, and update the owning
note instead of expanding this handoff. Remaining scope: [roadmap](architecture/roadmap.md).

- **Loading time (census 2026-09-14 late evening):** no log marker splits menu and save load, so the
  34–38 s figure cannot be re-measured directly; the `frame_end elapsed_ms` at frame 600 is 40–45 s in
  runs 11/14/16/17, with one 23–32 s stall in the first 600 frames of which the instrumented reader/inflate
  path explains 10–11 s and the rest is unattributed. Next: a `loading_phase` marker at menu-shown and
  save-load-complete: installed and read in run 20 (menu_shown 12.9 s, save load
  19.2–40.9 s with a 21.7 s stall); next is attribution of that stall
  ([loading-observations.md](reverse-engineering/loading-observations.md), "Phase markers").
