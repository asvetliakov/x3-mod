# Current goals and acceptance state

Reconciled 2026-09-13 against the [handoff](handoff-2026-09-13.md), the
[original objective](user-objective.md) and the recorded game runs. This is the
current checklist; older plans and status entries describe earlier checkpoints.
The overall objective remains incomplete. Native Windows/Direct3D and CrossOver
Preview are both required targets; native Windows runtime behavior is untested.

| # | Goal | Current state and remaining acceptance |
|---|---|---|
| 1 | True HDR | FP16 RT0 redirection and identity write-back work in game. Content is still gamma-space game lighting decoded into FP16, not scene-referred radiance. Material/lighting replacement and verified HDR display output remain. |
| 2 | Modern tonemapping | AgX is implemented and was seen in game at fixed EV 0. Keep AgX. A custom X3 look remains a later tuning task. |
| 3 | FP16 lighting and HDR emissive | Not started. Requires the material pass; allocating an FP16 target does not complete this goal. |
| 4 | HDR bloom | Not started; next visual feature after the current integration and user tests. Build on the FP16 target and the existing compositor/glow map. Initial bloom will inherit the current scene-content limitation. |
| 5 | Automatic exposure | Existing whole-scene log-average meter overexposes black space (about +7 EV). Space-aware tile meter passed independent branch review and both-bottle fixtures; the reviewed build is integrated and installed; game validation remains. |
| 6 | New material shaders | Not started. Prerequisite for scene-referred lighting and real HDR, with shader coverage beyond captured scenes. |
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

1. **Completed:** review 30 passed on the frozen merged tree, was committed as
   `3124e0b` and installed in bottle X3. Its scoped fixture/runtime evidence does
   not close review 31 or establish native-Windows runtime behavior.
2. Crypto cache reviewed/fixed and merged from `8794a5d` (572 checks per bottle);
   space-aware exposure branch `e897011` reviewed/fixed and merged
   (98 motion/HDR cases per bottle). Both changes are installed at qualification
   checkpoint `c85c5b5`;
   run 17 accepts the crypto path on X3; exposure game acceptance remains pending.
3. Reader/adjacency review and affected fixtures are complete (review 31).
   Checkpoint `ae03d9a` is committed. Combined selected motion/HDR and
   CryptoAPI checks and independent artifact review passed. Qualification
   checkpoint `c85c5b5` is installed and its bytes and run commands verified.
4. Ask the user for the handoff's six controlled run groups: crypto, reader verify
   then fast, adjacency verify then fast, sharpen 0.75, new exposure, vanilla
   cursor comparison. Reader fast requires zero verification mismatches;
   adjacency fast requires meaningful admitted work with zero admitted-output
   mismatches, with unsupported domains explicitly falling back to native.
   Copy each session and readbacks to a new `/tmp/x3-bottleX3-run<N>/` before
   bounded analysis; never read a large log whole or launch the game ourselves.
5. Implement HDR bloom on FP16 as the next visual checkpoint, with measured
   cost, HUD separation and explicit treatment of the game's existing glow.
6. Develop the material pass for scene-referred lighting/HDR emissives. Tune a
   custom AgX look with controlled captures as lighting evolves.

Additional candidates retained in the plan: temporal upscaling built on the
working TAA inputs (no assumption of DLSS availability), menu-bar handling and
the double cursor investigation. Finish the nearby loading work before adding
more loading instrumentation. The remaining original goals stay on the
[roadmap](architecture/roadmap.md); none are silently dropped. Supplemental
suggestions in the original objective—anisotropic filtering/LOD/draw-distance
tuning, color-managed UI and a texture overhaul—remain unevaluated candidates,
not new implementation commitments.
