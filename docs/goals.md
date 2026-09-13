# Current goals and acceptance state

Reconciled 2026-09-13 against the [handoff](handoff-2026-09-13.md), the
[original objective](user-objective.md) and the recorded game runs. This is the
current checklist; older plans and status entries describe earlier checkpoints.
The overall objective remains incomplete. Native Windows/Direct3D and CrossOver
Preview are both required targets; native Windows runtime behavior is untested.

| # | Goal | Current state and remaining acceptance |
|---|---|---|
| 1 | True HDR | FP16 RT0 redirection and identity write-back work in game. The FP16 scene still contains gamma-space game lighting; downstream decoding does not make that lighting scene-referred. Material/lighting replacement and verified HDR display output remain. |
| 2 | Modern tonemapping | AgX is implemented and was seen in game at fixed EV 0. Keep AgX. A custom X3 look remains a later tuning task. |
| 3 | FP16 lighting and HDR emissive | The [linear material path](architecture/scene-linear-materials.md) has design/disassembly, combined shaders and detached/live D3D9 qualification. The opt-in candidate is reviewed and installed; run 6 confirms a visible material change, with final appearance still under development; only covered opaque material evaluation becomes linear, not all scene blending or HDR output. Default-off additive emission integration now passes independent source/evidence review and isolated X3 live-route qualification; it is installed, while gameplay and its measured cost remain open. |
| 4 | HDR bloom | [Live integration](architecture/hdr-bloom-boundary.md) connects the qualified filter/executor and compositor bridge. Combined X3 lifetime checks pass 714/714 across both reference models, including Reset/ResetEx and original exceptions; independently reviewed and installed, with first gameplay acceptance pending. It runs the original compositor once then replaces RGB, preserving original state/resources/alpha. Component image/state/recovery evidence is linked from the design. Initial bloom uses the current decoded gamma-space scene; real radiance still requires materials. Gameplay quality and frame cost remain unverified. |
| 5 | Exposure / optional adaptation | [Reviewed outdoor-space evaluation](architecture/space-exposure-policy.md) selects fixed EV 0 as the next default, with automatic exposure retained as an explicit option and same-run toggle. Installed Auto mostly stays at +2 EV in runs 24/25; the default/control changes are reviewed and source-integrated; installation and visual acceptance remain pending. |
| 6 | New material shaders | **Partial coverage, not complete.** Installed: 110 reviewed pairs / 73 originals across Argon/shared/Split/Terran hulls and standard lighting; [detached and live qualification](architecture/linear-hull-materials.md) passes. Run 6 is complete: converted surfaces visibly brighten, coverage is partial and gloss needs later tuning; selection stutters and distant shimmer remain under investigation. The [remaining 52 SM3 opaque pairs](reverse-engineering/remaining-sm3-opaque-materials.md), older profiles, particles and other blended writers still need conversion and appropriate composition/coverage semantics. Existing native shaders remain in use for uncovered materials. The [complete ledger](architecture/material-coverage.md) accounts for all 817 archive pass identities; the full goal includes all necessary families and whole-scene linear lighting. |
| 7 | GTAO/SSAO | Not started. The motion route supplies R32F depth on RT2. |
| 8 | Better directional/self shadows | Not started. |
| 9 | Reflections/SSR | Not started. Needs a defined off-screen/environment fallback. |
| 10 | Improved/soft particles | Not started. |
| 11 | TAA | **Done and verified in game.** Same-draw motion, engine camera reprojection and scene-end resolve; stable history and removed tremble/shimmer. RCAS sharpen and mip bias were also verified. Candidate defaults 0.75 / −0.5 await an actual 0.75 capture before being enabled; that setting is currently modeled, not measured in game. |
| 12 | Volumetric nebula/fog | Not started. |
| 13 | Depth-aware lens effects | Not started. |
| 14 | Additional improvements | Loading reduced from 87 s to about 34–38 s on X3; route-on frame time from 16.9 to 12.1 ms in the recorded comparisons. Crypto cache, reader and adjacency fixes passed independent review and scoped fixtures. All are installed; run 17 accepts the crypto path on X3 (844-check probe 12.835 → 0.1353 s). Run 18 accepts reader/adjacency verify equivalence; run 19 accepts their fast-mode co-activation without faults or reported stutters. Its 41.571 s save load had crypto cache off and is not a controlled speed comparison. The 27.574 s save gap is not a controlled cache A/B. Diagnostic timings are not uninstrumented game FPS. |
| 15 | macOS menu bar | Not started. Also track the separate game/macOS double cursor after alt-tab; first compare with vanilla. |
| 16 | Clustered forward lighting | Not started. Material and light reconstruction precede implementation. |
| 17 | Modern third-person chase camera | **In progress.** Run 18 confirms aiming works, the 13° angle is good, and no trembling/visible camera problem was noticed. Requested distance 0.9 and slower 0.28/0.38 s response are reviewed and host-verified; these defaults are now installed and the user accepted the placement in run 20 A. The predictive aim indicator correction and transition diagnostics are implemented and reviewed, with host/site and X3 CPU-state checks passing; installation is complete and gameplay acceptance remains. Run 20 A sees the predictive hint; exact log/alignment acceptance is being checked. A separate selected-object distance/crosshair is missing in chase, and selection stutters are under investigation. Automatic chase-view restoration still needs transition evidence; docking is untested. Gameplay frame cost remains unqualified. Vanilla stays the default. |

Evidence: [iteration 13](verification/iteration-13.md),
[HDR scene path](verification/hdr-scene-path.md),
[compositor and glow](reverse-engineering/compositor-and-glow.md),
[window and cursor](architecture/window-and-cursor.md), and the
[current status](status.md). The handoff records the fixed-exposure run 16;
its raw log remains local in `/tmp/x3-bottleX3-run16/`. Inspection confirmed
`taa_debug=0` and no resolved/presented readbacks. Seven logged unresolved HDR
inputs were recovered into that snapshot from the live capture directory;
they are not a measured final-image baseline.

## Execution order

1. The reviewed camera, loading, bloom and DEFAULT/BUMPMAP material candidate is
   installed. Use the [brief user run queue](verification/user-runs.md) for current
   commands, prerequisites and acceptance; completed runs are not new requests.
   The agent never launches the game. Copy each session and readbacks to a new
   `/tmp/x3-bottleX3-run<N>/` before bounded analysis.
2. Convert all necessary scene families using the [complete coverage ledger](architecture/material-coverage.md),
   including uncaptured and older-profile variants. Every pass identity needs a
   converted role or a justified non-material disposition. The preceding 40 Split/standard-lighting pairs are GPU/live qualified. The
   next group adds shared/Split BUMPMAP and Terran DEFAULT/BUMPMAP: 40 pairs,
   bringing the source to 110. Source review and whole-group GPU/live qualification now pass;
   the combined build is installed and awaits gameplay acceptance.
3. The [revised emission path](architecture/linear-emission-composition.md) passes
   detached and live X3 qualification and is installed behind a default-off flag.
   It preserves native recovery, original source results and supplemental
   temporal coverage for enhanced emissions. Its measured cost remains material;
   a separate prototype fuses native-image copying and emission-target clearing,
   with exact differential checks and paired timing required before adoption.
   Opaque coverage alone does not justify changing blends or claiming scene-wide
   linear composition. Existing native-writer limitations and gameplay acceptance
   remain explicit.
4. Analyse the user's captures and fix observed failures. Reader and adjacency
   fast modes require meaningful verification work with zero admitted mismatches;
   an all-fallback session does not qualify them. Keep exposure/bloom and
   fixed-exposure materials comparisons distinct, as the run queue specifies.
5. Continue material/light reconstruction toward whole-scene linear lighting and
   HDR emissive composition, then the remaining rendering goals below. Tune the
   custom AgX look from controlled captures as lighting evolves. Native Windows
   runtime acceptance and HDR display output remain separate obligations.

Additional candidates retained in the plan: temporal upscaling built on the
working TAA inputs (no assumption of DLSS availability), menu-bar handling and
the double cursor investigation. Finish the nearby loading work before adding
more loading instrumentation. The remaining original goals stay on the
[roadmap](architecture/roadmap.md); none are silently dropped. Supplemental
suggestions in the original objective—anisotropic filtering/LOD/draw-distance
tuning, color-managed UI and a texture overhaul—remain unevaluated candidates,
not new implementation commitments.
