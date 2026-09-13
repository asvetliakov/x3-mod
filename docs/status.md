# Project status

Updated 2026-09-14. This is the current handoff. Earlier checkpoints are in
[status history](status-history-2026-09-13.md); read them only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md), and [original objective](user-objective.md)
retain the full scope.

The installed gameplay build is checkpoint `d9413fc`. Its scoped integration
checks pass. Run 27 confirms working glow, requests more strength, and accepts
+1.5 EV appearance; material/temporal and selection issues remain.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Installed DLL SHA-256 is
`7b3dd2885f9568c0d791dcc58b197276889b9de80a91fbdec38528cc50bff2b7` (13,351,668 bytes). The
[install record](../verification/results/linear-material-install.json) binds its source, scoped verification,
load check, and rollback DLL.

The installed renderer includes verified TAA, an FP16 scene target, AgX SDR writeback, Auto capped at +1.5 EV by default,
and a fixed EV 0 comparison through Ctrl+Shift+F9. Ctrl+Shift+F10 switches bloom contribution. Bloom, linear materials, and linear
emissions remain opt-in. Installed material coverage is **162 exact pairs / 130 originals**; installed default-off
emission coverage is twenty exact SM2 DEFAULT/INSTANCE pairs.

The installed chase defaults remain 13° pitch, distance 0.9, rotation/position response 0.28/0.38 s, offset 0.45,
and lag limits 8°/0.10. Vanilla camera is the default. Loading acceleration, predictive lead marker, central chase
HUD, and the selected WRAP/motion fixes are included.

## Latest gameplay evidence

User run 8 is saved as [run 27](verification/run27-glow-selection.md) in `/tmp/x3-bottleX3-run27/`, on installed source
`541e380`. The user sees bloom but wants it stronger, accepts +1.5 EV, and still
reports selection pauses, distant asteroid/station shimmer and occasional
objects changing from dark to bright as the viewpoint moves. Bounded log/capture
analysis and targeted native disassembly identify the next bounded measurements.
[Run 9](verification/user-runs.md#9-stronger-glow-and-selectionvoice-timing--ready) is ready for stronger-glow and selection/voice checks.

The first phase witness isolates a 458.85 ms delayed native target-lock publication
inside a 485.01 ms noncapture frame. A broad pre-simulation region also contains selection-time
stalls; the synchronous internal callback/VM cause is not yet isolated. The
[33-site trace](reverse-engineering/selection-native-vm.md) separates input work,
target notification, voice playback, stream creation and seeking. Its reviewed
X3 fixture passes 7,606 checks, with about 0.1645 ms diagnostic overhead per
synthetic loop; the expanded trace is installed.
Auto reaches its +1.5 ceiling in 329/331 active reports, so the accepted appearance
mostly reflects a steady boost in this run rather than demonstrated useful adaptation.
The installed build adopts the accepted Auto/+1.5 profile as its default.
Material summaries show no shader-bind failures or sampler refusals; the only
explicit refused pair is glass. Far alpha-blended asteroids bypass the opaque
material/motion route. The captured near and far nodes differ, so a same-object
brightness transition or LOD cause is not established.

[Run 26](verification/run26-comparison.md) and [run 23](verification/run23-material-comparison.md)
retain the previous exposure, material and shimmer comparisons. The central chase
crosshair/distance is visible; selection stutter also occurs with chase disabled.

## Newly installed and qualified

The [prior combined qualification](verification/combined-glow-materials.md) supplies unchanged material/emission/bloom GPU evidence. Source `d9413fc` adds reviewed constant/default and diagnostic changes; 31 focused host tests and the 7,606-check native CPU fixture pass. Its clean build passes the linked x87 audit (218 reachable functions) and DLL load check (8 checks / 17 exports). EXE and bottle configuration are unchanged;
the previous DLL and installation record are retained for rollback.

- **Authored bloom:** the retained-alpha correction is installed. Component GPU evidence passes 36 image
  cases and 16 controls, Reset, exact destination alpha, and RGB within one display code. Run 27 confirms visible glow; stronger authored gain 0.35 is installed for Run 9.
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

- **Selection stalls:** measured renderer and installed HUD/solver paths are excluded. The new trace catches an expensive delayed native
  target-lock publication and pre-simulation stalls; targeted disassembly of their internal work is underway.
- **Shimmer/temporal:** preserve the asteroid's far alpha/background mixture. Do not force opaque depth or infer
  a LOD change. Bound diffuse alpha, pixel overlap/order, and exact selected-target-to-node identity remain open.
- **Material appearance and coverage:** exclude accidental loss of native gloss terms before artistic tuning. The
  [coverage ledger](architecture/material-coverage.md) accounts for all 817 archive pass identities; older profiles,
  transparent, background, and other scene writers remain beyond installed coverage. The
  [glass extension](architecture/glass-materials.md) adds six reviewed SM3 opaque-capable
  pairs in main source (168 pairs / 137 originals total), preserving native gloss/Fresnel.
  Host checks pass; GPU/live qualification and installation remain pending.
- **Bloom/exposure:** authored glow works but is too subtle. Gain 0.35 is installed for the next comparison;
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
one native submission, a linear blended layer, and shared reactive coverage; its
prototype must preserve native recovery and does not yet solve layered temporal
accumulation. The reviewed [screen-emission proposal](architecture/screen-emission-overlap.md)
uses four packed MRTs to preserve fragment order without replay. Its mathematical
prototype passes 540 in-domain X3 measurements with exact native RGB/alpha;
108 boundary rows expose range/overflow limits. Separate synchronized phase
timings also warn of substantial per-draw cost. Persistent native-image assembly
failure and runtime range admission remain open before integration. Both use documented D3D9
capabilities; neither has native-Windows runtime qualification.

## Next user action

[Run 9](verification/user-runs.md#9-stronger-glow-and-selectionvoice-timing--ready) is ready: compare stronger glow and select several targets while the new trace distinguishes notification, playback, stream creation and seeking. No F8 captures are needed. Run 8 is complete; the optional vanilla cursor comparison remains available.

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
