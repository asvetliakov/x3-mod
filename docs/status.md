# Project status

Updated 2026-09-15 evening (run26 candidate installed; run 26 queued). This is the short current handoff; the current
session handoff is [handoff-2026-09-15.md](handoff-2026-09-15.md). The day's narrative
(chronology, superseded candidates, run-by-run detail, earlier prepared-design
prose, stable-foundation prose) is in
[status history 2026-09-14](archive/status-history-2026-09-14.md); earlier
checkpoints are in [status history 2026-09-13](archive/status-history-2026-09-13.md).
Read history only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md) and
[original objective](user-objective.md) retain the full scope.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Installed DLL SHA-256 is
`5726a37b702cb808ebdb8106f884e9cedca32066c02cfbcff1c413d9e3122c59`
(15,034,835 bytes), built once on Opus from clean committed main `f2b7406`
(2026-09-15 evening). The [build record](../verification/results/run26-candidate-build.json)
binds the clean build (10.84 s, zero warnings), 225-function no-x87 audit, exact
17 exports and the eight-check X3 DLL load; the
[install record](../verification/results/run26-candidate-install.json) binds the
installed bytes, the unchanged EXE/bottle hashes and the rollback. Rollback DLL and
manifest are in `/tmp/x3-candidate-QkAfwa/rollback`. Run 26's launch passed `--dry-run`;
no game launched.

**Provenance caveat:** before this install the bottle held DLL `3f1b9941…`
(14,905,020 bytes, installed 17:00 local), not the `4c3ac81e…` run25 build this
file previously recorded; a `/tmp/x3-candidate-run26/` candidate (`6df394fe…`,
09:06) and snapshots run61–run64 also exist without repo records. Triage
(log headers, build logs, `.codex`, temp build trees) could not tie either DLL
to a commit: the session logs carry no proxy hash or env header, the run26
source worktree is deleted, and the 17:00 manifest names only
`/Users/asvetl/x3-mod/build/d3d9.dll`. Runs 62–64 (16:13–17:15) ran the packed
screen-emission route with `materials=0`, so that DLL already carried an
uncommitted decoupling. The `3f1b9941…` DLL is retained as the rollback; the
proxy now logs its own hash, embedded source commit and effective `X3M_*`
options at startup (`proxy_identity` / `proxy_options`, merged as `5eda356`,
122 ms once per process under CrossOver; not in the installed `5726a37b…`
build, rides the next candidate), and `manage.py install` records the DLL's
embedded commit, so this cannot recur silently.

This build adds, all default-off unless stated: `--chase-view-restore`,
`--emission-source-gain G`, `--screen-emission-additive G`, `--shadow-replay-candidates`,
sun-lane refusal buckets inside `--sun-shadow-lane`, and `--screen-emission` no
longer requiring `--linear-materials`. It keeps the run25 features (native
constant-port repair, `--chase-hud-anchor centre` default, gate diagnostics,
`--loading-intervals`). It applies no shadows. Auto exposure ceiling +1.0 EV and
fill 0.03 (fill only with linear materials) remain.

Existing TAA, FP16 scene target, AgX SDR writeback, Ctrl+Shift+F9 EV0 comparison
and Ctrl+Shift+F10 bloom toggle remain. Material coverage is 168 exact pairs /
137 original programs; bloom, linear materials and linear emissions remain opt-in.
Chase defaults remain 13° pitch, distance 0.9, responses 0.28/0.38 s, offset 0.45,
and lag limits 8°/0.10. Vanilla camera remains the default.

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
- Shadows: [replay gates](architecture/shadow-replay-gates.md) ratified; the
  depth-replay fixture is not funded until a lane-independent caster-candidate
  counter (`--shadow-replay-candidates`, in flight) answers four predicates in
  the next run.
- Pre-existing host failure: `test_linear_material_live.test_production_control_flow`
  fails to compile its mock on `main` (missing sun-lane fields); tracked, unowned.

The run26 candidate is installed (see above) and
[run 26](verification/user-runs.md) is queued: session A with original hulls,
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
