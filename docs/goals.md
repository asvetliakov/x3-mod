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
| 3 | FP16 lighting and HDR emissive | The [linear material path](architecture/scene-linear-materials.md) has design/disassembly, combined shaders and detached/live D3D9 qualification. The opt-in candidate is reviewed and installed; game acceptance is pending; only covered opaque material evaluation becomes linear, not all scene blending or HDR output. |
| 4 | HDR bloom | [Live integration](architecture/hdr-bloom-boundary.md) connects the qualified filter/executor and compositor bridge. Combined X3 lifetime checks pass 714/714 across both reference models, including Reset/ResetEx and original exceptions; independently reviewed and installed, with first gameplay acceptance pending. It runs the original compositor once then replaces RGB, preserving original state/resources/alpha. Component image/state/recovery evidence is linked from the design. Initial bloom uses the current decoded gamma-space scene; real radiance still requires materials. Gameplay quality and frame cost remain unverified. |
| 5 | Automatic exposure | Existing whole-scene log-average meter overexposes black space (about +7 EV). Space-aware tile meter passed independent branch review and both-bottle fixtures; the reviewed build is integrated and installed; game validation remains. |
| 6 | New material shaders | Bounded DEFAULT and [Argon BUMPMAP](architecture/linear-bump-materials.md) materials are implemented, independently reviewed and installed, including uncaptured archive variants. Offline, GPU and live-route qualification are linked from the owning design; gameplay acceptance remains. Whole-scene linear lighting still needs a policy for every color writer, including blended emission and temporal reactivity. |
| 7 | GTAO/SSAO | Not started. The motion route supplies R32F depth on RT2. |
| 8 | Better directional/self shadows | Not started. |
| 9 | Reflections/SSR | Not started. Needs a defined off-screen/environment fallback. |
| 10 | Improved/soft particles | Not started. |
| 11 | TAA | **Done and verified in game.** Same-draw motion, engine camera reprojection and scene-end resolve; stable history and removed tremble/shimmer. RCAS sharpen and mip bias were also verified. Candidate defaults 0.75 / −0.5 await an actual 0.75 capture before being enabled; that setting is currently modeled, not measured in game. |
| 12 | Volumetric nebula/fog | Not started. |
| 13 | Depth-aware lens effects | Not started. |
| 14 | Additional improvements | Loading reduced from 87 s to about 34–38 s on X3; route-on frame time from 16.9 to 12.1 ms in the recorded comparisons. Crypto cache, reader and adjacency fixes passed independent review and scoped fixtures. All are installed; run 17 accepts the crypto path on X3 (844-check probe 12.835 → 0.1353 s). Reader/adjacency acceptance remains. The 27.574 s save gap is not a controlled cache A/B. Diagnostic timings are not uninstrumented game FPS. |
| 15 | macOS menu bar | Not started. Also track the separate game/macOS double cursor after alt-tab; first compare with vanilla. |
| 16 | Clustered forward lighting | Not started. Material and light reconstruction precede implementation. |
| 17 | Modern third-person chase camera | **In progress.** Integrated and reviewed; the second flight reports no trembling after anchor correction. The [third flight](verification/chase-third-run.md) isolates native cursor-active admission as the chase firing failure. The scoped fix and requested 13-degree pitch / 0.85 distance are reviewed, X3-qualified and installed; gameplay acceptance is pending. Vanilla remains the default. Menus, aiming, cuts, reset survival and frame cost still require acceptance; [current status](status.md) links the installed build and qualification history. |

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
2. The [revised emission candidate](architecture/linear-emission-composition.md#detached-mrt-candidate-qualification)
   passes detached X3 qualification for untouched-channel preservation and a
   current native recovery image. A measured branch optimization showed no
   consistent benefit; retain the baseline and qualify actual game shaders,
   live target ownership and complete temporal coverage, with batching/cost
   still under investigation. Opaque coverage alone does not justify changing blends.
3. Analyse the user's captures and fix observed failures. Reader and adjacency
   fast modes require meaningful verification work with zero admitted mismatches;
   an all-fallback session does not qualify them. Keep exposure/bloom and
   fixed-exposure materials comparisons distinct, as the run queue specifies.
4. Continue material/light reconstruction toward whole-scene linear lighting and
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
