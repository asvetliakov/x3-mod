# Project status

Updated 2026-09-14. This is the current handoff. Earlier checkpoints are in
[status history](archive/status-history-2026-09-13.md); read them only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md), and [original objective](user-objective.md)
retain the full scope.

Work resumed 2026-09-14 under the Claude Code routing in `CLAUDE.md`. Unfinished
work from the pause (cutout source, reviewed fade results, decoder build plans
and the persistent backup) is recorded in
[the September 14 morning handoff](archive/handoff-2026-09-14.md); the current
handoff is [handoff-2026-09-14c.md](handoff-2026-09-14c.md), written after the autonomous
afternoon session (the evening one is archived). Nothing merged today is installed.
Checkpoints today: fade prototypes 1 and 1b committed (`d8e189b`, `3c3479a`),
with the [per-part AABB finding](reverse-engineering/render-node-bounds.md) and the
ratified [in-place region composition design](architecture/linear-distance-fade-region.md)
(`67baa55`; step 1, per-draw bound derivation, merged in `4bcedea` with 0 region
violations over 29 detached cases, plus the opt-in `--fade-witness` live M-readback
witness merged in `97a0aa8` with a proven negative control; step 2, the in-place
bracket in the composition pass, is merged (`f20f00f`: 586 bit-exact twins against
the exchanged path); step 3, the runtime route, is merged (`2f6ed17`: live 26
processes / 536 frames / 224 TAA readbacks, 1080p 16 DIPs ≈2.4–3.3 ms vs ≈8.0 ms
for prototype 1); the option stays default-off and nothing is installed); the two-pair alpha-tested [cutout runtime](architecture/alpha-tested-materials.md)
is merged (`b9ae8dd`; two reviews, x87 audit PASS at 223 reachable functions, live
X3 13 processes / 531 frames / 2,111,274 checks PASS, not installed);
the isolated WMA decoder adapter
([note](architecture/voice-decoder-adapter.md)) is built, evidence-reviewed and
decodes both real voice files through the null-event contract with exact cue
timestamps in build v3 (`6a2c763`), but gameplay launches hang on the loading
screen (runs 29–35 and the witness attempt, which produced no snapshot): the witness run shows `SetState(RUN)` returning `E_FAIL`
followed by a COM teardown wait; root cause open (see the handoff). The WMP11
bottle experiment was reverted byte-identically (`2e64c4d`).
User decision (2026-09-14): no native-codec or bottle-clone route; fix through the plugin path,
and if that proves too hard, fail early on the missing audio so the selection stutter goes away
(this supersedes the earlier rejection of negative caching).
User run 12 (Wine `CX_LOG` quartz/amstream trace, snapshot run37) locates the failure: the
game adds a qasf DMO Wrapper audio-decoder filter to its graph before the source; that
filter's `Pause` returns `E_FAIL` inside `SetState(RUN)`, and the main thread then calls
`RemoveFilter(MediaStreamFilter)` 3.8 million times until force-quit (the hang). Root-cause
diagnosis on the Wine side and disassembly of the game's DMO creation and teardown loop are
settled and reproduced (`docs/reverse-engineering/voice-startup-sequence.md` §11–13): the game
creates the wrapper for the Windows Media Speech decoder `{874131cb…}`, which is not registered
in the bottle; `Init` fails `REGDB_E_CLASSNOTREG`, the game adds the pin-less wrapper anyway,
`Pause` fails, and the graph never leaves Stopped so `RemoveFilter` returns `VFW_E_NOT_STOPPED`
forever. The replica mode `game-dmo` reproduces the E_FAIL and the spin. The fix is merged (`d1de1b9`,
two reviews): a byte-verified post-call hook at the game's `Init` return that, only on that
exact failure and only under `--voice-decoder`, retries `Init` with the registered WMA decoder
DMO; the replica then runs, decodes and tears down cleanly. Not installed; needs a user run.
The station docking-port source-over route (`docs/architecture/linear-station-source-over.md`,
reviewed, merged `f56a393`: seventh fade pair, read-only refused-rect diagnostic, 78 detached
cases / 300 bit-exact twins / 34 live processes, station windows ≈0.2 ms per draw) and the
host-test harness repairs (`93f359c`) are merged. The `f56a393` candidate was installed and then superseded the same day by `76d7750` (below).
After that install, main also carries the capture-only draw-state fields on `motion_route`
lines (`f4cd0a0`, for classifying the remaining gate-4 refusals); it rides the next candidate.
The full host suite is green again after the fixture-harness repairs (`93f359c`, merge of
`188ffb4`): 1710 tests, 0 failures, 5 skipped (after the step-C harness repair `6ac3dcd`).
Screen emission (goal 3): the region-bracketed design `docs/architecture/screen-emission-region.md`
is ratified; step B (bullet bound from Unlock-time extrema checkpoints, draw-learned allowlist,
reviewed, `2c15609`) is merged and step A (packed screen policy 8 in the pass, 540/540 rows
bit-exact, 0.13 ms per bracket, reviewed, merged `2c12f33`) are merged; step C (runtime admission behind
`--screen-emission`, two reviews, merged) completes the route: live 22 sources, 10 admitted, witness
0 outside, ≈0.16–0.18 ms per bracket at 16 DIPs; no per-frame bracket cap yet, so a bullet burst
of 30 costs ≈5 ms — run 15 measures it. The second candidate (`76d7750`, DLL `608b35d8…`) is installed; runs 13, 14 and 15 are ready
on it (record `verification/results/screen-emission-install.json`).
Ambient occlusion (goal 7): the design `docs/architecture/ambient-occlusion.md` is ratified as
v1 (half-resolution GTAO before the TAA resolve, default-off); its engine inputs are settled
(`c4f6940`: 0.2 m per view unit, default zn 6 / zf 2e6 in gameplay) and step 1, the detached pass with
its analytic-oracle fixture, is reviewed and merged (99 checks; term within 3.7e-4 of the float64
reference; chain after the step-1b reduction `30299db`: 0.51 ms warm / 1.06 ms first block at
1280×768, 1.33 ms at 1080p; independently reviewed 2026-09-14 afternoon: PASS, five low findings fixed and merged in `ddc243b`, fixture 112 checks / 0 failures). The scene-end placement waits for the run-14 capture query on Z-test-off
draws; nothing of it is referenced at runtime or installed.
User run 11 (fade region route plus the armed cutout runtime, `--linear-distance-fade
--fade-witness`) is complete on the candidate built from `3f06979`, which is installed (record
`verification/results/fade-region-cutout-install.json`); run 10 is on hold. The replica
with the game's DirectSound setup (`877317b`) still does not reproduce the `SetState(RUN)` failure.
Merged agent worktrees and the two merged cutout worktrees are pruned.
The opt-in per-frame shimmer trace (`--shimmer-trace`, reviewed, merged `09e62bb`) is ready to
ride the next candidate for the distant-shimmer report; not installed.

The installed gameplay build is checkpoint `066e18f` (previous `76d7750` retained for
rollback). Its scoped integration checks pass; the new options are default-off. Run 28 confirms stronger visible glow and reproduces distance-dependent
dark material on a docking port. Selection pauses are isolated to voice-stream
creation; the user confirms missing target-name speech.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Installed DLL SHA-256 is
`5b92484a70852525798b37e6d34d7203929b986ca44479edb7e349790ca83670` (14,091,812 bytes), built from
`066e18f` (voice DMO fallback hook fix, clipped bullet bound, launcher fade default-on; AO step 1b
detached). The [install record](../verification/results/voice-hook-install.json) binds its source, scoped
verification, load check, and the rollback DLL `608b35d8…` (`76d7750`). EXE and `cxbottle.conf` unchanged.

The installed renderer includes verified TAA, an FP16 scene target, AgX SDR writeback, Auto capped at +1.5 EV by default,
and a fixed EV 0 comparison through Ctrl+Shift+F9. Ctrl+Shift+F10 switches bloom contribution. Bloom, linear materials, and linear
emissions remain opt-in. Installed material coverage is **168 exact pairs / 137 originals**; installed default-off
emission coverage is twenty exact SM2 DEFAULT/INSTANCE pairs.

The installed chase defaults remain 13° pitch, distance 0.9, rotation/position response 0.28/0.38 s, offset 0.45,
and lag limits 8°/0.10. Vanilla camera is the default. Loading acceleration, predictive lead marker, central chase
HUD, and the selected WRAP/motion fixes are included.

## Latest gameplay evidence

User run 11 is the latest gameplay evidence: snapshot `/tmp/x3-bottleX3-run36/`
on installed checkpoint `3f06979`, with the fade region route and the armed
cutout runtime. The witness is clean — 543 `fade_witness` lines, 196 sampled,
zero covered pixels outside the derived rectangles in every sampled frame, no
`rects_unprepared`/`overflow`/truncation, 2087 region lines with 0
full-viewport fallbacks, and only shallow fades (`f_hist` 1962, 98, 11, 0, 0,
0, 0, 0). The cutout runtime stayed available (`cutout_caps=1`, 7827 routed
draws) and the docking port remained on the native path, still darkening on
approach; frame-time medians equal run 28 (`frame_end` windows of 4097 ms against 4104 ms; the field is a ~300-frame window total, ≈ 13.7 ms per frame, not µs per frame — corrected 2026-09-14, see the completed-run archive). Details
are in the [completed-run archive](archive/user-runs-completed.md), the
[region note](architecture/linear-distance-fade-region.md) and the
[cutout note](architecture/alpha-tested-materials.md).

The previous gameplay evidence, [run 28](verification/run28-glow-materials.md),
user run 9, is saved in `/tmp/x3-bottleX3-run28/` on installed source `d9413fc`. Screenshots show substantial
colored halos at gain 0.35. The user approved a slightly tighter, stronger core;
the installed correction uses gain 0.375/scatter 0.65, calibrated against saved
resolved-TAA inputs. Auto still mostly reaches its accepted +1.5-EV ceiling.

The user reproduces dark docking-port parts becoming bright on approach and recalls
it elsewhere. The one F8 burst shows stable routing and no common-shader fog-on
transition, so it cannot establish the cause. No material bind failures occur;
the Run 28 explicit refusals are the glass pair absent from that older build. Reviewed
[capture-only target/root/parent and fade diagnostics](reverse-engineering/station-material-distance.md)
are installed, pending gameplay association evidence.

The [33-site trace](reverse-engineering/selection-native-vm.md) isolates ten
input-side target publications to voice playback/stream creation. Eight take
424–512 ms, with virtually all time in creation. The user hears no target-name
speech. [Disassembly](reverse-engineering/voice-stream-creation.md) confirms null
creation returns, existing voice files and an existing successful-stream cache;
the standalone probe reproduces connection failure in all 24 constructions
across both actual voice files and three tested graph-construction routes. Removing the optional
speech decoder does not repair it. The explicit ASF reader exposes no pins; direct
reader diagnostics then identify an unavailable WMA8 decoder after successful ASF
recognition for both files. The native-null-event synthetic PCM control reads
nonzero audio in both graphs, narrowing the failure to compressed decoding.
The owning note now records the independently reviewed source and evidence;
no production audio repair is installed.
Other unexplained slow-frame residuals remain. The earlier
[Run 27 delayed publisher](verification/run27-glow-selection.md) is a separate witness.

[Run 26](verification/run26-comparison.md) and [Run 23](verification/run23-material-comparison.md)
retain previous appearance comparisons. The central chase crosshair/distance is
visible; selection stutter also occurs with chase disabled.

## Newly installed and qualified

The [prior combined qualification](verification/combined-glow-materials.md) supplies unchanged material/emission/bloom GPU evidence. Source `8442f43` adds six GPU/live-qualified glass pairs, tighter bloom and capture-only target/fade association diagnostics. The affected 27 bloom and 24 capture host tests pass; unchanged foundation evidence is reused. Its clean build passes the linked x87 audit (218 reachable functions) and DLL load check (8 checks / 17 exports). EXE and bottle configuration are unchanged;
the previous DLL and installation record are retained for rollback.

- **Authored bloom:** the retained-alpha correction is installed. Component GPU evidence passes 36 image
  cases and 16 controls, Reset, exact destination alpha, and RGB within one display code. Run 27 confirms visible glow; Run 28 shows stronger halos at gain 0.35; the approved gain 0.375/scatter 0.65 correction is now installed.
- **162-pair materials:** all 52 inventoried additional SM3 opaque pairs have reviewed conversions.
  Detached GPU: 3,923 cases. Live corpus: 2,576 frames / 32,516 checks; scalar WRAP: 144 / 3,208;
  XT14 including four DEFAULT linkage repairs: 628 / 10,836,608. Exact temporal/state twins and Reset pass.
- **20-pair SM2 emission:** all 384 SM2 archive occurrences are covered by the bounded route. Detached
  coverage/fused runs and 208 live functional frames / 2,185,516 checks pass. It remains default-off;
  gameplay and its measured cost remain open. The isolated [nine-pair SM1 promotion](architecture/linear-emission-sm1.md)
  now passes 1,674 X3 measurements / 216 creations with exact native RGB/alpha in
  all six modes. It is independently reviewed but not connected to the runtime;
  overlapping screen composition remains a separate unresolved contract.
- **Selection diagnostics:** 33 native sites and the owned Present bridge are installed, default-off.
  The CPU fixture passes 7,606 checks at about 0.1645 ms added per synthetic loop.
  `--game-phases --telemetry` enables the bounded trace; run 27 isolates the delayed publisher and broad pre-simulation spans.

An installed reviewed correction fixes native VM opcode validation in chase
transition diagnostics (`0x82`, not VM return `0x83`) and records saved dispatch
contexts without treating them as method entries; these are not camera or
stutter behavior fixes. See [provenance](reverse-engineering/chase-view-transition.md).

## Current open issues

- **Target speech (resolved 2026-09-14 evening):** target-name speech plays in gameplay with `--voice-decoder /tmp/x3-wma-plugin-v4` and the fixed DMO fallback hook (runs 16 and 18); the run-16 crackle was a full-scale wrap in CrossOver's stock audio converter, removed by the plugin v4 float limit. The plugin lives outside the bottle and the repo (untracked, backed up); the selection pause is gone: no target publication over 10 ms in runs 16/18 against run 28's median 463 ms (archive §18).
- **Selection stalls:** Run 28 isolates synchronous voice-stream creation inside target
  publication; target speech was absent then (see above). Investigate creation failure and lifecycle,
  retaining other unexplained slow-frame residuals rather than assigning all pauses to audio.
  The optional process-local WMA decoder adapter is built and, in the synchronous voice probe,
  both voice DATs now open (`open_hr 00000000`, PCM tag 1, mono 44100/16) via the libav
  Windows Media Audio 2 decoder, against `E_FAIL 0x80004005` for both before it. Decoder
  availability is not restored speech; see the
  [adapter note](architecture/voice-decoder-adapter.md).
- **Distant shimmer reported in run 11:** distant asteroids and stations appear to
  shimmer in motion, described as parts of geometry disappearing; the user is unsure
  whether it is new. The fade-region witness is clean, so the evidence does not
  implicate the region route; the cause is unidentified and the report is carried as
  part of the open TAA distant-shimmer item.
- **Shimmer/temporal:** preserve the asteroid's far alpha/background mixture. Do not force opaque depth or infer
  a LOD change. Bound diffuse alpha, pixel overlap/order, and exact selected-target-to-node identity remain open.
  The [normal/specular study](reverse-engineering/asteroid-specular-minification.md) identifies an independent
  minification hypothesis and bounded diagnostics; no smoothing policy is selected.
- **Material appearance and coverage:** exclude accidental loss of native gloss terms before artistic tuning. The
  [coverage ledger](architecture/material-coverage.md) accounts for all 817 archive pass identities; older profiles,
  transparent, background, and other scene writers remain beyond installed coverage. The
  [glass extension](architecture/glass-materials.md) adds six reviewed SM3 opaque-capable
  installed pairs (168 pairs / 137 originals total), preserving native gloss/Fresnel.
  Host, detached GPU (254 cases / 2,286 samples) and focused live routing
  (216 frames / 4,258,208 checks) pass. Gameplay acceptance remains pending.
- **Bloom/exposure:** the approved tighter-core correction uses gain 0.375/scatter 0.65;
  source/reference review and 27 affected host tests pass, and it is installed.
  +1.5 EV appearance is accepted and selected as the installed Auto default. The meter
  still mostly reaches its ceiling; physically informed adaptation remains unproved.
- **HDR scope:** FP16 and AgX work, but much of the scene is still compatibility-decoded gamma-space lighting.
  Scene-referred lighting, complete linear blending, and verified HDR display output remain incomplete.
- **Native Windows:** Windows-compatible source cross-compiles, but no native-Windows runtime is verified. Depth
  provision, CreateDeviceEx adoption, MRT/PS2.1, Reset/presentation, performance, and HDR output remain gaps.
- **Window/cursor:** the macOS menu bar and double cursor after alt-tab remain open. Queue run 4 is the optional
  vanilla comparison.

## Prepared designs

The reviewed [distance-fade proposal](architecture/linear-distance-fade.md) uses
one native submission, a linear blended layer, and shared reactive coverage. Its
detached prototype passes 71 X3 cases / 257 source calls with native recovery;
runtime admission and combined temporal-mask integration are implemented and
independently reviewed from production source `ad3fefe`; its clean DLL passes
the x87 audit. The actual X3 image/state/TAA/Reset qualification passes 302 frames
and 4,502,255 functional/admission checks. Prototype 1 (`d8e189b`) drops the
native-B sample from the fade composite (143 DWORDs); detached and live results
match the baselines exactly and the paired 1080p source window falls from
7.64–8.46 to 6.61–7.11 ms for 16 ordered DIPs. Performance is still not accepted;
the option stays off and the candidate is not installed. The next reduction needs
a proven conservative screen bound from the engine (disassembly in progress). It does
not yet solve layered temporal accumulation. The [alpha-tested material route](architecture/alpha-tested-materials.md)
passes the X3 capability check and detached coverage/alpha/depth/stencil twins
for two exact Argon pairs (48 cases / 864 twins). Live admission and TAA remain
unqualified, with no production gate change. The reviewed [screen-emission proposal](architecture/screen-emission-overlap.md)
uses four packed MRTs to preserve fragment order without replay. Its mathematical
prototype passes 540 in-domain X3 measurements with exact native RGB/alpha;
108 boundary rows expose range/overflow limits. Separate synchronized phase
timings also warn of substantial per-draw cost. Persistent native-image assembly
failure and runtime range admission remain open before integration. Both use documented D3D9
capabilities; neither has native-Windows runtime qualification.

## Next user action

Run 13 crashed at the loading screen with an execute-access page fault (snapshot `/tmp/x3-bottleX3-run38/`). Root cause found and reproduced in the replica: the DMO fallback hook declared the wrapper interface as a local abstract C++ class, GCC devirtualised its `Init` call into `call __cxa_pure_virtual` (absolute 0 in the DLL), and the relocated image jumped to its load delta; the emitted stub was correct. Fix (explicit C vtable binding, install-line addresses, a `--voice-decoder`-only fault witness, replica mode `game-dmo-hook` executing the production stub: unfixed build faults, fixed build completes with 3 activations) is merged (`331a6b5`) after an Opus review (root cause confirmed on the installed DLL bytes, whole-DLL zero-target audit clean) and a Fable second review (handler lock-free, vtable/ABI/stack contract, detach path); ledger `docs/verification/voice-decoder.md`. It rides the next candidate; run 13 is then retried. Run 14 is complete (`/tmp/x3-bottleX3-run39/`, archive §14): witness clean, the docking port composed on the linear route, the darkening persists with every logged draw input byte-identical across the approach captures (unchanged-state minification, a material-shader question). Run 15 is complete (`/tmp/x3-bottleX3-run40/`, archive §15): witness clean, but 400 of 800 bullet draws refused with clip w ≤ 0 and the admitted brackets covered 58–90 % of the viewport; the near-plane clipping fix of the bound (new `BehindNear` reason, clipped count, capped capture-only `packed_sample` luminance line) is merged (`066e18f`, Opus review; ledger `docs/verification/screen-emission.md`), but batches close to the camera still bound to 58–90 % of the view by box geometry, so a per-bullet bound design is being written; no bracket cap is adopted. The candidate from `066e18f` (hook fix, clipped bullet bound, fade default-on) is built and installed (DLL `5b92484a…`); runs 16 (run-13 retry) and 17 (bullet measurement) are ready in the run queue. User decisions (2026-09-14): fade route default-on accepted (launcher flip merged, `--no-linear-distance-fade` opts out, 14 launcher tests OK; rides the next candidate); AO fixture cost accepted, step 2 may start with its in-game hook-level measurement default-off; no screen-emission bracket cap, the bound fix comes first.

The original three-run plan on the installed `76d7750` candidate, in this order: run 13 (short: speech
with the decoder plugin and the DMO fallback hook; force-quit and report if the loading screen
hangs), run 14 (station route, fade witness, shimmer trace: asteroids in zoom, station approach,
F8 at distance and close), run 15 (run 14 plus `--screen-emission`: fire at a target for a few
seconds, F8 while firing). Commands and what to report are in the [run queue](verification/user-runs.md). The optional
vanilla cursor comparison remains available in the [run queue](verification/user-runs.md).

## Stable foundation and later scope

TAA is implemented and verified in game with same-draw motion, camera reprojection, scene-end resolve, history
rejection, sharpen, and mip bias. Loading fell from about 87 s to roughly 34–38 s; reader, adjacency, DAT, gzip,
and crypto-cache paths have scoped accepted evidence. Chase aiming and placement are accepted; automatic view
restoration, alignment across more scenes, docking, and gameplay frame cost remain.

The installed twenty-pair emission route has isolated live qualification and native recovery, but run26 left emission off;
gameplay appearance and cost were not evaluated there. GTAO, shadows, reflections, improved particles, volumetrics,
lens effects, clustered lighting, and HDR display remain on the [roadmap](architecture/roadmap.md).

Use the brief [run queue](verification/user-runs.md) for user actions. New Wine fixtures use X3 and the shared lock.
The reviewed XT fixture logging change preserves every assertion and failure witness while omitting bulk
success lines; see the [workflow follow-up](verification/workflow-audit-2026-09-13.md#2026-09-14-bounded-xt-fixture-output).
Keep raw captures/builds local, use focused verification, and update owning notes instead of expanding this handoff.
