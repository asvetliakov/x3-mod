# Project status

Updated 2026-09-16 (run28 candidate installed; run 28 queued). This is the short current handoff; the current
session handoff is [handoff-2026-09-17.md](handoff-2026-09-17.md). The day's narrative
(chronology, superseded candidates, run-by-run detail, earlier prepared-design
prose, stable-foundation prose) is in
[status history 2026-09-14](archive/status-history-2026-09-14.md); earlier
checkpoints are in [status history 2026-09-13](archive/status-history-2026-09-13.md).
Read history only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md) and
[original objective](user-objective.md) retain the full scope.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Installed DLL SHA-256 is
`7102a2f14a2ce71421f76b90bc37b5f99aa6aa8a4b9c2b6db242ace07736031f`
(16,748,838 bytes), built once on Opus from clean committed main `ee5a406`
(2026-09-17; marker `X3M_SOURCE_COMMIT=ee5a406…`, no `-dirty`). The
[build record](../verification/results/run34-candidate-build.json) binds the
clean build (14 s, zero warnings), the 494-function no-x87 audit (76 roots,
media-cue handlers included), the 17 exports, the four site verifiers against
the EXE, the stamp CPU fixture (8,521 checks, 0 failures, fixture sha
recorded; pass 90.3 ns, loop 88.6 ns, media-cue pass 280 ns / refuse 116 ns
per dispatch), the state-hook benchmark on the candidate bytes
([record](../verification/results/bottle-X3/state-hook-benchmark-run34.json):
production SetRenderState 13.4 ns vs native 15.5, SetSamplerState 10.9 vs
11.0, draw pair 1,020 ns), five motion-output parity cases equal to the
committed record, and the launcher dry-runs for the trace, cache and v5
runtime options. A first attempt from `1f255f2` failed its fixture gate on a
`-Werror=cast-function-type` cast in the new clock anchor and was discarded
(`/tmp/x3-candidate-dXW89V`); the fix is `ee5a406`. The
[install record](../verification/results/run34-candidate-install.json) binds the
installed bytes, unchanged EXE/bottle hashes and the rollback. The previous
run33 DLL `03c0c9f4…` and manifest are in `/tmp/x3-candidate-uemMxM/rollback`.
Run 34's four session commands passed `--dry-run`; no game launched.

This build adds, on top of run33's: the launcher's stderr tee into the
session directory (`launcher-stderr.log`, UTC-prefixed) with a `clock_anchor`
and `qpc=` on every window line; `loaded_module` identity lines for the D3D9
backend and `d3dx9_37.dll`; the media-cue gate at `0x00498140`
(`--media-cue-trace`, `--media-cue-cache on|off`, launcher default **on** since
run 34 A2 confirmed the bound (the installed DLL reads the variable; the
run-34 dry-runs were made with the old default), `--media-cue-retry-s 30`; [note](reverse-engineering/media-cue-playback.md),
[ledger](verification/media-cues.md)); stamp arena 20,480 B. Off by default:
pass, loop and media-cue groups. Appearance and production hooks unchanged
from run32/33. Defaults unchanged: camera 0.5°/0.50, EV ceiling +1.3, mip
bias -0.5, sharpen 0.75, fill 0.05 (linear only); original hull shading. No
shadows applied. The v5 decoder runtime (`/tmp/x3-wma-plugin-v5`: MP3, MP2,
MPEG-1 video, program-stream demux on top of wmav2) is host-verified and
selected per session with `--voice-decoder`.

Existing TAA, FP16 scene target, AgX SDR writeback, Ctrl+Shift+F9 EV0 comparison
and Ctrl+Shift+F10 bloom toggle remain. Material coverage is 168 exact pairs /
137 original programs; bloom, linear materials and linear emissions remain opt-in.
Chase defaults: 0.5° pitch, offset 0.50, distance 0.9, responses 0.28/0.38 s,
lag limits 8°/0.10. Vanilla camera remains the default.

## Session 2026-09-17: run 29 received, run 30 candidate

Run 29 came back as `run83` (A), `run84` (A2 with the profiler) and `run85`
(B). Outcomes and decisions:

- **Engines never responded to any gain:** the ship engine glow is the jump
  gate's program pair `d5e1c753…/8360f422…`, drawn on 92 of 122 engine
  materials with the screen blend ONE/INVSRCCOLOR that the gain admission
  refused ([effect shader users](reverse-engineering/effect-shader-users.md)).
  Only 39 pixel / 24 vertex programs were ever created across 81 sessions;
  18 of the 20 gain pairs and 8 of the 9 bullet pairs never were. Decision
  (user): one gain, one key; screen draws substituted additive under the gain
  ([cost note](architecture/linear-emission-cost.md), "Screen substitution").
  Plan and attribution table: [emitter-plan.md](architecture/emitter-plan.md).
- **Halo persisted with alpha 0 and native bolts:** the bloom source is
  unbounded, so gained or overlapping bolts saturate the tonemapper into a
  white disc; the kernel itself is narrow (50 % at 2–3 px). Ratified and
  merged: `--bloom-source-clamp` ([bloom-falloff.md](architecture/bloom-falloff.md)).
- **Chase pose:** fixed per the user; no centre-then-jump.
- **FPS:** busy view 14.7 ms p50 at ~240 draws vs 8.1 ms at ~86 facing away;
  Present 9–15 µs throughout (CPU-side, scales with draws). The sampling
  profiler attributes nothing under FEX (every leaf a syscall thunk), so
  `--frame-timing` gained proxy self-time buckets and the slowest hooked call
  ([schema](verification/sampling-profiler.md)).
- **Sun lane:** available on 4,577 of 4,695 frames in run85 under linear
  materials, the first time in game; no cutout draws occurred, so the cutout
  admission fix is unexercised. Next shadow step: the original-program share
  producer and a lane latch without the linear-material prerequisite
  ([contract](architecture/legacy-sun-application.md)).
- **Mods:** two Mayhem packages inspected, no shader files; hash-keyed
  transforms fail closed ([mod-compatibility.md](architecture/mod-compatibility.md)).
  The unknown-program report (`shader_unknown` / `shader_population`,
  `36d25a7`) is merged on main and rides the next candidate. Decision: no `.fx`
  archive edits.

Run 30 is complete (below).

## Session 2026-09-17 (later): run 30 received, run 31 candidate

Run 30 came back as `run87` (A) and `run88` (B). Outcomes and decisions:

- **Engines respond** to the single `--emission-source-gain` (screen draws
  substituted additive); **bolt halo accepted** at `--bloom-source-clamp 1.0`
  with `--screen-emission-additive-alpha 0`; the user keeps both values.
- **Frame split (run87):** a busy frame of 28.5 ms at 457 draws carried
  ~30,000 hooked state calls, of which the `state` bucket was 8.9 ms; about
  two thirds of that was the diagnostic's own timing, the rest the proxy's
  full CPU-state envelope (FNSAVE/FRSTOR, 1,004 ns under FEX) on every draw
  and binding hook ([attribution](verification/sampling-profiler.md)). The game
  drives its state through an unfiltered `ID3DXEffectStateManager` and sorts
  its render list with an O(n²) bubble sort at `0x0047e620`
  ([frame loop](reverse-engineering/frame-loop-phases.md)).
- **Merged for run 31:** count-only state timing with `state_top=` and gap
  attribution (`dcbe43c`); the light envelope (MXCSR + LastError, 9.9 ns) on
  the draw and binding hooks with an x87-free draw path (`e8bac89`, draw pair
  4,197 to 1,276 ns); the setter dispatch trim (`79ccb59`/`4adf3dd`,
  SetRenderState 119.6 to 79.0 ns, SetSamplerState 108.3 to 68.6 ns; the
  50 ns target waits on a no-exceptions unit for the light hooks,
  [design](architecture/state-call-fast-path.md)); `--frame-phases`, ten
  byte-verified stamps in the engine's frame routine `0x00471f50` attributing
  the game's own time (`fcbddb2`, two reviews, CPU fixture 8,033 checks). The
  hybrid unhook with `Get*` at draw and the bubble-sort patch are the next
  fast-path steps, after run 31 shows what is left.
- **Sun lane (run88):** available on every frame (14,924 of 14,924) under
  linear materials; still no cutout draws, so the cutout admission is
  unexercised. Run 31 session B goes to an Argon factory, farm, solar plant or
  trading station ([ledger](verification/directional-shadows.md)).
- **Kept:** point-light admission default-off; no `.fx` archive edits; the
  proxy over engine trampolines for the renderer (trampolines stay for
  engine-side fixes only); three concurrent agents unless the user raises it.

Run 31 came back as `run89` (A) and `run90` (B), both on the installed build:

- **Busy frame attributed (run89):** 37.5 ms p50 at 987 draws and 61,896
  hooked state calls; the `views` phase (view submission) carries 32.5 ms,
  scene update with the O(n²) sort only 65 µs. Inside submission the hooked
  draws including native cost 7.9 ms; the 23.4 ms between hooked calls is game
  code, of which the state-call chain (about 63 per draw from the unfiltered
  effect state manager) is roughly 6 ms proxy plus native and the rest the
  emulated D3DX apply loop. Empty view 6.8 ms at 51 draws
  ([ledger](verification/sampling-profiler.md)). Next fast-path steps proposed:
  a redundant-state counter against the proxy shadow, the hybrid unhook
  (about 5 ms), then a state-manager filter trampoline if the no-op fraction
  justifies it; awaiting the user's go.
- **Sun lane (run90):** available on all 16,041 frames, depth replay 40.7 µs
  median, zero skips; **zero cutout draws for the third session** although
  both cutout programs compile at startup. The telemetry has no per-draw
  shader-pair counter, so which programs the station drew is unknown; the next
  diagnostic build adds one before any further session B
  ([ledger](verification/directional-shadows.md)).
- No unknown programs, claim failures, truncated stamps or chase refusals in
  either session.

Merged after run 31 and installed as the run32 candidate: the hybrid unhook
and the three counters (above), the motion-output runner pins for the record's
pre-`3df7b9f` defaults (`39d9863`: the fade-oracle and auto-exposure drifts
were defaults drift, not rendering), and the restored linear-emission
validators (`8ba98c9`, host suite 1,955 tests green). Fast-path state after
step 5: the proxy's per-call share of the busy frame is ~1 µs per draw plus
the hooked texture/constant/binding setters; the remaining cost is the
engine's per-draw work, which the run 32 counters size for a state-manager
filter and proxy instancing ([design](architecture/state-call-fast-path.md)).

Run 32 came back as `run91` (A1), `run92` (A2), `run93` (B) and `run94` (C):

- **Unhooked proxy (run92):** the same busy view fell from 37.5 ms (run89,
  hooks and frame timing on) to 26.5 ms p50, hooks confirmed absent, mip-bias
  apply/restore balanced over 8.1 M calls. The proxy is out of the state path.
- **Counters (run91):** redundant sets 95 % (render states), 99 % (sampler),
  40 % (texture) of the ~19,400 shadowed calls per frame; instancing
  candidates 5 % of draws, material-sortable 6 %. Design decision: no
  engine-side state filter (bounded at 0.3–1.0 ms, native setters cost
  11–15 ns) and no proxy instancing or sorting
  ([engine-state-filter.md](architecture/engine-state-filter.md)). The
  remaining ~23 ms of game code per busy frame is split next by
  `--pass-phases`, four accumulate-only stamps in the effect pass loop
  `0x004c0150` (BeginPass, draw, EndPass; once per draw; RE validated,
  [effect-pass-loop.md](reverse-engineering/effect-pass-loop.md)), in flight.
- **Cutout pairs draw everywhere (run91/run93):** ~108 per frame in the busy
  Argon view, 561 k over run93; under the default configuration they are
  routed through the tested-opaque arm with the lane share written, which
  the old counters could not show; `cutout_opaque_*` telemetry merged for the
  next candidate ([ledger](verification/directional-shadows.md)).
- **Slow sectors are not rendering (run93/run94):** a quiet sector ran at
  ~390 ms per frame with 95.7 % in the main loop's broad input region
  (`0x00403b09`–`0x00403f2a`: input wait, synchronous script and save paths),
  render 3 %, deferred script VM 1 %; the same stall existed on Windows. The
  region is being decompiled for stamp sites and a candidate owner
  ([ledger](verification/sampling-profiler.md), run94 section).

Run 33 came back as `run95` (A), `run96` (B) and `run97` (C):

- **Busy frame per draw (run95):** of 20.1 ms view submission at 981 passes,
  the device draw call is 8.6 ms (8.7 µs per draw: ~1 µs proxy, the rest
  Wine's D3D9 path), BeginPass 6.5 ms (6.7 µs), engine work between passes
  4.5 ms (4.6 µs), EndPass 0.1 ms. Design ratified
  ([effect-pass-replay.md](architecture/effect-pass-replay.md)): first the
  no-code bottle experiments (CrossOver's DXVK backend for D3D9; builtin vs
  native `d3dx9_37`, which the next build identifies with a `loaded_module`
  line), then host prerequisites (compiled-effect state classification, two
  residual stamps), and a pass-replay `ID3DXEffect` wrapper at the EXE's
  `D3DXCreateEffect` import (3–5 ms bound) only if the frame still needs it.
- **Quiet-sector stall (run96):** 99.8 % of a ~380 ms frame is one call, the
  per-sector object pass `0x0045b720`, one sector and one container per frame.
  The user observes GStreamer-CRITICAL bursts on the launcher's stderr once or
  twice per slow frame (twice per session otherwise), suggesting a per-object
  media stream created or destroyed and failing each frame; the proxy log
  cannot confirm it (no stderr capture, no filenames), so the next build tees
  the launcher's stderr into the session directory with a clock anchor.
  **Decompiled:** `0x0045b720` is the per-sector media-cue selector
  ([note](reverse-engineering/sector-post-pass.md)): it scores the `Videos`
  table against nearby objects and restarts the winning cue through
  `0x00498140` into the DirectShow graph constructor; a cue whose graph
  cannot be built is freed and retried every frame, one file probe plus
  CoCreateInstance and Render per frame, which is the stall (and the
  GStreamer criticals under Wine; on Windows the same retry with a missing
  codec). Bounding is behaviour-neutral: a negative cache at `0x00498140`
  leaves the game in the same state as a real failure. **Decision (user,
  2026-09-17): both.** Run 34 A1 (run98) named the cue: id 2 =
  `mov\00002.dat`, a 533 MB MPEG-1 video elementary stream, one failed
  ~390 ms graph build per frame (no MPEG video decoder in the v4 runtime or
  CrossOver's set). A2 (run99): the negative cache at `0x00498140` reduces it
  to one real attempt per 30 s and the sector runs at 7–9 ms; **the stall is
  gone (user confirmed)** and the launcher default is now on. A3 (run100/101):
  the v5 decoder runtime (`avdec_mpeg2video`, `mpegpsdemux`) makes the graphs
  build and the first comm dialog then hangs the main loop in Wine's video
  path; **v5 is parked**, the cache is the fix, the recipe stays documented
  ([ledger](verification/media-cues.md)).
- **Cutout under the lane (run97):** ~90 cutout draws per frame routed through
  the tested-opaque arm with the lane share written, ~8 refused for no depth
  write; both cutout pairs write depth so lane = routed. Linear materials plus
  the lane cost about +3 ms in the busy view, mostly in the draw call.

Run 34 is complete (run98–102): the stall is solved by the media-cue cache
(default on); the v5 decoder runtime is parked (comm-dialog hang); CrossOver's
DXVK D3D9 renders black on this Preview, so the bottle's graphics backend must
be switched back before the next run. No run is queued; next steps are in
[handoff-2026-09-17.md](handoff-2026-09-17.md).

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
