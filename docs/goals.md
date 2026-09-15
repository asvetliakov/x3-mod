# Current goals and acceptance state

Reconciled 2026-09-14 against the [handoff](archive/handoff-2026-09-13.md), the
[original objective](user-objective.md) and the recorded game runs. This is the
current checklist; older plans and status entries describe earlier checkpoints.
The overall objective remains incomplete. Native Windows/Direct3D and CrossOver
Preview are both required targets; native Windows runtime behavior is untested.

| # | Goal | Current state and remaining acceptance |
|---|---|---|
| 1 | True HDR | FP16 RT0 redirection and identity write-back work in game. The FP16 scene still contains gamma-space game lighting; downstream decoding does not make that lighting scene-referred. Material/lighting replacement and verified HDR display output remain. |
| 2 | Modern tonemapping | AgX is implemented and was seen in game at fixed EV 0. Keep AgX. A custom X3 look remains a later tuning task. |
| 3 | FP16 lighting and HDR emissive | The [linear material path](architecture/scene-linear-materials.md) has design/disassembly, combined shaders and detached/live D3D9 qualification. The opt-in candidate is reviewed and installed; run 6 confirms a visible material change, with final appearance still under development; only covered opaque material evaluation becomes linear, not all scene blending or HDR output. Default-off additive emission integration now passes independent source/evidence review and isolated X3 live-route qualification; it is installed, while gameplay and its measured cost remain open. |
| 4 | HDR bloom | [Live integration](architecture/hdr-bloom-boundary.md) connects the qualified filter/executor and compositor bridge. Combined X3 lifetime checks pass 714/714 across both reference models, including Reset/ResetEx and original exceptions; independently reviewed and installed, with visual acceptance still pending. It runs the original compositor once then replaces RGB, preserving original state/resources/alpha. Component image/state/recovery evidence is linked from the design. Initial bloom uses the current decoded gamma-space scene; real radiance still requires materials. Run 7 confirms actual preparation/commit, but fails visual acceptance: the replacement omits native alpha-authored emitter glow. The [authored-glow correction](architecture/bloom-authored-glow.md) is reviewed and installed with GPU-qualified shaders and bounded component timing; Run 8 confirms visible glow but requests more strength; Run 9 confirms the installed gain 0.35 restores substantial halos. The user approved a slightly tighter, stronger core: gain 0.375/scatter 0.65 is reviewed, host-tested and installed, pending gameplay acceptance. Gameplay frame cost remains unqualified. |
| 5 | Exposure / optional adaptation | Auto exposure and fixed-EV comparison are implemented. Following run54 and further appearance tuning, the user chose Auto capped at **+1.0 EV** on 2026-09-15; the source and launcher default change is reviewed and complete; the DLL fallback awaits the next candidate. The earlier +1.5 profile remains historical acceptance. [Status](status.md) records the installed build; [exposure policy](architecture/space-exposure-policy.md) owns the behavior. |
| 6 | New material shaders | **Partial whole-scene coverage.** Installed: **168 reviewed pairs / 137 originals**; run 6 confirms brighter covered surfaces, partial station coverage and gloss needing further work. Coverage includes all 52 inventoried additional SM3 opaque pairs. Boron/Paranid scalar transport and all 14 [XT conversions/DEFAULT repairs](architecture/xt-materials.md) are independently reviewed; 3,923 detached X3 GPU cases pass. The expanded corpus, WRAP and XT live matrices pass; installation is complete, with gameplay appearance still pending. Older profiles, glass/transparency, particles, backgrounds and other color writers still need complete conversion/composition policies; the [coverage ledger](architecture/material-coverage.md) retains all 817 archive pass identities. The six additional SM3 glass pairs are now installed (included in the 168-pair total); detached GPU (254 cases / 2,286 samples) and live routing (216 frames / 4,258,208 checks) pass. Gameplay acceptance remains pending. The distance-fade region route and the two-pair alpha-tested cutout runtime are installed and passed the user run 11, 14 and 15 witnesses (zero covered pixels outside the derived rectangles over 196, 238 and 587 sampled frames) with frame time equal to run 28 in run 11; the user accepted the fade route as default-on on 2026-09-14 (launcher flip pending the next candidate). The station docking-port pair composes on the route since run 14, and run51 attributes its distance darkening to per-node point-light admission, not a minification correction. Run54 confirms fill visibly brightens hulls; the user chose **0.03** as the new fill default on 2026-09-15. The source/launcher default change is reviewed; the DLL fallback awaits the next candidate. The user also approved keeping base hull shading at EV0 while exposing specular/reflection/emissive terms; that split is not implemented yet. Gloss/reflection appearance remains unfinished. Target-name speech works and the selection pause is gone (runs 16/18, plugin v4 + DMO fallback hook, 2026-09-14); distant shimmer remains under investigation. |
| 7 | GTAO/SSAO | In progress: design v1 ratified (half-resolution GTAO before the TAA resolve, default-off), detached pass reviewed (112 fixture checks; chain 0.51–0.53 ms warm / 1.06–1.11 ms cold at 1280×768, 1.33 ms at 1080p, each quad carrying a fixture fence). Step 2 (scene-end chain behind `--ambient-occlusion`, Ctrl+Shift+F11 toggle) is installed and ran in game in runs 19–21 at radii 2, 20 and 100 m: it works (≈180–210 µs CPU, zero when off) but is not visible at X3's viewing distances, and the user decided on 2026-09-15 that AO stays off by default and is not pursued further ([scale note](architecture/ambient-occlusion-scale.md)). Goal treated as evaluated and declined for open space; directional shadowing (goal 8) is the route that reads at distance. |
| 8 | Better directional/self shadows | In progress: sun-share extraction is host-qualified for 108 pixel originals and preserves existing output. Live RT2 publication, GPU precision, portable replay feasibility and cascades remain; no rendered shadows are implemented. The inherited material sanitizer read-port violation is repaired, reviewed and CrossOver GPU-parity qualified; native Windows execution remains unverified. |
| 9 | Reflections/SSR | Not started. Needs a defined off-screen/environment fallback. |
| 10 | Improved/soft particles | Not started. |
| 11 | TAA | **Done and verified in game.** Same-draw motion, engine camera reprojection and scene-end resolve; stable history and removed camera trembling; distant asteroid/station shimmer remains open. RCAS sharpen and mip bias were also verified. Candidate defaults 0.75 / −0.5 await an actual 0.75 capture before being enabled; that setting is currently modeled, not measured in game. Run 11 reports distant asteroid and station shimmer as parts of geometry appearing to vanish; the cause is open. |
| 12 | Volumetric nebula/fog | Not started. |
| 13 | Depth-aware lens effects | Not started. |
| 14 | Additional improvements | Loading reduced from 87 s to about 34–38 s on X3; route-on frame time from 16.9 to 12.1 ms in the recorded comparisons. Crypto cache, reader and adjacency fixes passed independent review and scoped fixtures. All are installed; run 17 accepts the crypto path on X3 (844-check probe 12.835 → 0.1353 s). Run 18 accepts reader/adjacency verify equivalence; run 19 accepts their fast-mode co-activation without faults or reported stutters. Its 41.571 s save load had crypto cache off and is not a controlled speed comparison. The 27.574 s save gap is not a controlled cache A/B. Diagnostic timings are not uninstrumented game FPS. |
| 15 | macOS menu bar | Not started. Also track the separate game/macOS double cursor after alt-tab; first compare with vanilla. |
| 16 | Clustered forward lighting | Not started. Material and light reconstruction precede implementation. |
| 17 | Modern third-person chase camera | **In progress.** Run 18 confirms aiming works, the 13° angle is good, and no trembling/visible camera problem was noticed. Requested distance 0.9 and slower 0.28/0.38 s response are reviewed and host-verified; these defaults are now installed and the user accepted the placement in run 20 A. The predictive aim indicator correction and transition diagnostics are implemented and reviewed, with host/site and X3 CPU-state checks passing; installation is complete and gameplay acceptance remains. Run 20 A sees the predictive hint; exact log/alignment acceptance is being checked. The central distance/crosshair is visible in run 7 screenshots; selection stutter still occurs and the new timing evidence is being analysed. Automatic chase-view restoration still needs transition evidence; docking is untested. Gameplay frame cost remains unqualified. Vanilla stays the default. |

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
   converted role or a justified non-material disposition. Installed coverage is
   168 pairs, including six SM3 glass pairs, all 14 XT contracts and correction of
   four malformed native DEFAULT linkages. Nine SM1 emission pairs pass isolated
   X3 promotion/parity qualification and await live integration; glass, distance fading, older profiles and other scene writers
   remain in scope.
3. The [revised emission path](architecture/linear-emission-composition.md) passes
   detached and live X3 qualification and is installed behind a default-off flag.
   It preserves native recovery, original source results and supplemental
   temporal coverage for enhanced emissions. Its measured cost remains material;
   the reviewed native-image copy / emission-target clear fusion is now installed
   after exact component and live-route qualification. Gameplay and further cost
   reduction remain open.
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
