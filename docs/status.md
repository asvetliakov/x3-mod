# Project status

Updated 2026-09-14. This is the current handoff. Earlier checkpoints are in
[status history](status-history-2026-09-13.md); read them only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md), and [original objective](user-objective.md)
retain the full scope.

The installed gameplay build is checkpoint `541e380`. Its scoped integration
checks pass; gameplay acceptance of the new glow and material coverage remains.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Installed DLL SHA-256 is
`f4094be54971cb48171175536aded8f31a49e6853167ce14be7584320ab4f36e` (13,325,135 bytes). The
[install record](../verification/results/linear-material-install.json) binds its source, scoped verification,
load check, and rollback DLL.

The installed renderer includes verified TAA, an FP16 scene target, AgX SDR writeback, fixed EV 0 by default,
and Auto through Ctrl+Shift+F9. Ctrl+Shift+F10 switches bloom contribution. Bloom, linear materials, and linear
emissions remain opt-in. Installed material coverage is **162 exact pairs / 130 originals**; installed default-off
emission coverage is twenty exact SM2 DEFAULT/INSTANCE pairs.

The installed chase defaults remain 13° pitch, distance 0.9, rotation/position response 0.28/0.38 s, offset 0.45,
and lag limits 8°/0.10. Vanilla camera is the default. Loading acceleration, predictive lead marker, central chase
HUD, and the selected WRAP/motion fixes are included.

## Latest gameplay evidence

[Run 26](verification/run26-comparison.md), user run 7, is complete on `75dbbed`. Bloom attached and all 153
sampled prepare/commit records succeeded, closing the pure-device failure. Visual acceptance failed: OFF/ON
looked alike and native emitter halos disappeared because the replacement omitted X3's alpha-authored glow.

The user prefers the brighter Auto screenshots. Auto's fresh and held targets are +2 EV in all 376 active reports;
applied exposure reaches +2 after convergence rather than adapting usefully in this sample. The next same-run comparison caps Auto at **+1.5 EV**,
with fixed 0 retained as the reference. This tests the requested milder option alongside restored glow;
the general default remains fixed 0 pending that comparison.

The central chase crosshair/distance is visible. Selection stutter still occurs, including with chase disabled.
Run26 finds 286–454 ms frames after six of nine target changes; Present, TAA, HDR, metering, logging, loading
probes, and the native lead/HUD routines are too small to explain those witnesses.

Runs 20–23 remain the material evidence: converted surfaces are brighter, coverage is visibly partial, and gloss
appears reduced. The distant asteroid shimmer disappears closer. Captured far/near draws use different native distance-fade states: far alpha-blends
with Z writes off and background temporal treatment; near is opaque with valid object motion/depth.
Those observations do not yet establish the cause of the visible shimmer.

## Newly installed and qualified

The [combined qualification](verification/combined-glow-materials.md) records reviewed failures/fixes,
retained binaries and scoped limits. Source `541e380` passed the linked x87 audit (218 reachable functions),
the DLL load check and nine embedded bloom-program checks. EXE and bottle configuration are unchanged;
the previous DLL and installation record are retained for rollback.

- **Authored bloom:** the retained-alpha correction is installed. Component GPU evidence passes 36 image
  cases and 16 controls, Reset, exact destination alpha, and RGB within one display code. Gameplay remains.
- **162-pair materials:** all 52 inventoried additional SM3 opaque pairs have reviewed conversions.
  Detached GPU: 3,923 cases. Live corpus: 2,576 frames / 32,516 checks; scalar WRAP: 144 / 3,208;
  XT14 including four DEFAULT linkage repairs: 628 / 10,836,608. Exact temporal/state twins and Reset pass.
- **20-pair SM2 emission:** all 384 SM2 archive occurrences are covered by the bounded route. Detached
  coverage/fused runs and 208 live functional frames / 2,185,516 checks pass. It remains default-off;
  gameplay and its measured cost remain open. [Nine SM1 pairs](architecture/linear-emission-sm1.md)
  and nonadditive composition are planned, not implemented.
- **Selection diagnostics:** 23 native sites and the owned Present bridge are installed, default-off.
  The CPU fixture passes 815 checks at about 0.134 ms added per synthetic loop.
  `--game-phases --telemetry` enables the bounded trace; gameplay results remain pending.

## Current open issues

- **Selection stalls:** measured renderer and installed HUD/solver paths are excluded. A delayed native
  `NotifyTargetLock` event and the rest of the main loop are covered by the new diagnostics; cause and fix remain unknown.
- **Shimmer/temporal:** preserve the asteroid's far alpha/background mixture. Do not force opaque depth or infer
  a LOD change. Bound diffuse alpha, pixel overlap/order, and exact selected-target-to-node identity remain open.
- **Material appearance and coverage:** exclude accidental loss of native gloss terms before artistic tuning. The
  [coverage ledger](architecture/material-coverage.md) accounts for all 817 archive pass identities; older profiles,
  transparent, background, and other scene writers remain beyond installed coverage.
- **Bloom/exposure:** installed authored glow needs gameplay acceptance. Compare Auto capped at +1.5 EV
  with fixed 0 before deciding the default; prior +2-cap behavior was essentially a constant boost.
- **HDR scope:** FP16 and AgX work, but much of the scene is still compatibility-decoded gamma-space lighting.
  Scene-referred lighting, complete linear blending, and verified HDR display output remain incomplete.
- **Native Windows:** Windows-compatible source cross-compiles, but no native-Windows runtime is verified. Depth
  provision, CreateDeviceEx adoption, MRT/PS2.1, Reset/presentation, performance, and HDR output remain gaps.
- **Window/cursor:** the macOS menu bar and double cursor after alt-tab remain open. Queue run 4 is the optional
  vanilla comparison.

## Next user action

[Run 8](verification/user-runs.md#8-restored-glow-milder-exposure-and-selection-trace--ready) is ready.
One dry-run validated the combined command without launching the game. It combines restored emitter glow,
Auto capped at +1.5 versus fixed 0, broader selection timing, and expanded material images. Emission remains
explicitly off through launcher defaults to keep this visual comparison focused.

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
