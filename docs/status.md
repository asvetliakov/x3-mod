# Project status

Updated 2026-09-13. This is the current handoff. Earlier checkpoints are in
[status history](status-history-2026-09-13.md); read them only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md), and [original objective](user-objective.md)
retain the full scope.

The installed gameplay build is still checkpoint `75dbbed`. The reviewed changes below
are ahead of that build; source commits are not installation evidence.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Installed DLL SHA-256 is
`3cbb350c3148e3e703677182cb9fb7b85e28af048f5b19c0dbe29b570ef9e42a` (13,030,325 bytes). The
[install record](../verification/results/linear-material-install.json) binds its source, scoped verification,
load check, and rollback DLL.

The installed renderer includes verified TAA, an FP16 scene target, AgX SDR writeback, fixed EV 0 by default,
and Auto through Ctrl+Shift+F9. Ctrl+Shift+F10 switches bloom contribution. Bloom, linear materials, and linear
emissions remain opt-in. Installed material coverage is **116 exact pairs / 83 originals**; installed default-off
emission coverage is five exact SM2 DEFAULT pairs.

The installed chase defaults remain 13° pitch, distance 0.9, rotation/position response 0.28/0.38 s, offset 0.45,
and lag limits 8°/0.10. Vanilla camera is the default. Loading acceleration, predictive lead marker, central chase
HUD, and the selected WRAP/motion fixes are included.

## Latest gameplay evidence

[Run 26](verification/run26-comparison.md), user run 7, is complete on `75dbbed`. Bloom attached and all 153
sampled prepare/commit records succeeded, closing the pure-device failure. Visual acceptance failed: OFF/ON
looked alike and native emitter halos disappeared because the replacement omitted X3's alpha-authored glow.

The user prefers the brighter Auto screenshots. Auto's fresh and held targets are +2 EV in all 376 active reports;
applied exposure reaches +2 after convergence rather than adapting usefully in this sample. Fixed **+1.5 EV** is the next artistic candidate,
with fixed 0 retained as the reference. Do not change the installed default until restored glow can be judged.

The central chase crosshair/distance is visible. Selection stutter still occurs, including with chase disabled.
Run26 finds 286–454 ms frames after six of nine target changes; Present, TAA, HDR, metering, logging, loading
probes, and the native lead/HUD routines are too small to explain those witnesses.

Runs 20–23 remain the material evidence: converted surfaces are brighter, coverage is visibly partial, and gloss
appears reduced. The distant asteroid shimmer disappears closer. Captured far/near draws use different native distance-fade states: far alpha-blends
with Z writes off and background temporal treatment; near is opaque with valid object motion/depth.
Those observations do not yet establish the cause of the visible shimmer.

## Source ahead of the installed build

- **Authored bloom glow:** main contains the reviewed retained-alpha correction. The standalone X3 GPU corpus
  passes 36 image cases and 16 controls, Reset, exact destination alpha, and RGB within one display code. The
  promoted shaders are qualified; candidate-DLL integration, gameplay appearance, and gameplay cost remain.
- **148-pair materials:** main adds all 32 Boron/Paranid pairs, reaching **148 pairs / 115 originals in source**.
  Pure conversion, scalar-WRAP transport, review, and the 3,549-case detached X3 GPU corpus pass. The reviewed
  expanded live fixture has not run on the GPU; no 148-pair DLL is installed.
- **20-pair SM2 emission source:** main adds all 15 remaining SM2 effects/engine pairs to the existing five,
  covering 384 SM2 archive occurrences. Host review and 100 variants / 1,483 assertions pass. The added fifteen
  now pass the 376-case coverage and 271-case fused-composition X3 GPU runs, including exact native-output
  parity and scoped Reset checks. The expanded 52-frame live matrix remains. Nine SM1 pairs and
  nonadditive blend contracts remain separate.
- **XT materials:** all 14 pairs are implemented and source-reviewed in an isolated worktree, including four
  authored DEFAULT linkage repairs. Detached/live fixture preparation is being reviewed; GPU qualification
  remains. Main stays at 148 pairs so this expansion does not delay the immediate glow/diagnostic build.
- **Selection diagnostics:** 23 native sites and the owned Present bridge are implemented and independently
  reviewed. The X3 CPU fixture passes 815 checks, with about 0.134 ms added per synthetic loop.
  `--game-phases --telemetry` enables the bounded trace; gameplay results remain pending.

## Current open issues

- **Selection stalls:** measured renderer and installed HUD/solver paths are excluded. A delayed native
  `NotifyTargetLock` event and the rest of the main loop are covered by the new diagnostics; cause and fix remain unknown.
- **Shimmer/temporal:** preserve the asteroid's far alpha/background mixture. Do not force opaque depth or infer
  a LOD change. Bound diffuse alpha, pixel overlap/order, and exact selected-target-to-node identity remain open.
- **Material appearance and coverage:** exclude accidental loss of native gloss terms before artistic tuning. The
  [coverage ledger](architecture/material-coverage.md) accounts for all 817 archive pass identities; older profiles,
  XT, transparent, background, and other scene writers remain beyond installed coverage.
- **Bloom/exposure:** authored glow needs integrated gameplay acceptance before selecting +1.5 EV. Auto's current
  +2-cap behavior remains only an option.
- **HDR scope:** FP16 and AgX work, but much of the scene is still compatibility-decoded gamma-space lighting.
  Scene-referred lighting, complete linear blending, and verified HDR display output remain incomplete.
- **Native Windows:** Windows-compatible source cross-compiles, but no native-Windows runtime is verified. Depth
  provision, CreateDeviceEx adoption, MRT/PS2.1, Reset/presentation, performance, and HDR output remain gaps.
- **Window/cursor:** the macOS menu bar and double cursor after alt-tab remain open. Queue run 4 is the optional
  vanilla comparison.

## Next candidate conditions

1. Keep the reviewed glow, 148-pair material and selection-diagnostic source together; retain the separate
   source-reviewed XT work until its GPU qualification is ready.
2. Make one clean candidate build and matching fixture seam. Qualify the 148-pair live matrix, scalar WRAP,
   expanded default-off emission route, and affected integration/load/CPU checks against those retained binaries.
3. Fix any concrete failures, then install reversibly with the previous DLL/record retained for rollback.
   Update the single install record, verify installed bytes and validate the combined user command.

No historical gameplay capture is required for every shader alias. Exact contracts may be implemented from the
complete archive; runtime scene owner, target, sampler-sRGB, blend/depth, and capability gates decide whether a
submission is enhanced. Unsupported or nonadditive submissions stay native.

Only after that candidate is installed should the next enhanced user run compare restored emitter glow, fixed 0
versus the +1.5 exposure candidate, broader selection timing, and expanded material images. No enhanced gameplay
run is ready now; the agent never launches the game.

## Stable foundation and later scope

TAA is implemented and verified in game with same-draw motion, camera reprojection, scene-end resolve, history
rejection, sharpen, and mip bias. Loading fell from about 87 s to roughly 34–38 s; reader, adjacency, DAT, gzip,
and crypto-cache paths have scoped accepted evidence. Chase aiming and placement are accepted; automatic view
restoration, alignment across more scenes, docking, and gameplay frame cost remain.

The installed five-pair emission route has isolated live qualification and native recovery, but run26 left it off;
gameplay appearance and cost were not evaluated there. GTAO, shadows, reflections, improved particles, volumetrics,
lens effects, clustered lighting, and HDR display remain on the [roadmap](architecture/roadmap.md).

Use the brief [run queue](verification/user-runs.md) for user actions. New Wine fixtures use X3 and the shared lock.
Keep raw captures/builds local, use focused verification, and update owning notes instead of expanding this handoff.
