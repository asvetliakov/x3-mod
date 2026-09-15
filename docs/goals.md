# Current goals and acceptance state

This file tracks acceptance state only, one row per goal, and changes only when
that state changes; ordering, the installed build and the current handoff live
in [status](status.md), the original scope in the
[objective](user-objective.md). Status is one of Planned, Accepted (in game),
Installed, In progress, Held, Closed or Rejected. The material scope clarification of 2026-09-15
now lives in the [material allocation assessment](architecture/material-investment.md).

| # | Goal | Status | Current state | Owning note |
|---|---|---|---|---|
| 1 | True HDR | In progress | FP16 RT0 redirection and identity write-back run in game, but the scene still carries compatibility-decoded gamma-space lighting; scene-referred lighting and verified HDR display output remain open (status 2026-09-15, "HDR scope"). | [hdr-scene-path.md](architecture/hdr-scene-path.md) |
| 2 | Modern tonemapping | Installed | AgX SDR write-back is installed and seen in game; a custom X3 look is later tuning. | [hdr-transfer.md](architecture/hdr-transfer.md) |
| 3 | FP16 lighting and HDR emissive | Held | The opt-in linear material path is installed, but the user plays original hull shading without `--linear-materials` since 2026-09-15 and no further linear-hull processing is planned; scene-wide linear blending and linear emissive composition (the bracket route) are Rejected in favour of the source gains `--emission-source-gain` / `--screen-emission-additive`, which push emitters above 1.0 in the native code-value space for exposure and bloom (run 26 accepted the bolts); the remaining HDR gap is scene-referred lighting and verified HDR display output. | [linear-emission-cost.md](architecture/linear-emission-cost.md) |
| 4 | HDR bloom | Installed | Live integration with authored-glow correction at gain 0.375 / scatter 0.65 is installed and accepted for halo strength in run 9; it still blooms a decoded gamma-space scene and its gameplay frame cost is unqualified. | [hdr-bloom-boundary.md](architecture/hdr-bloom-boundary.md) |
| 5 | Exposure / optional adaptation | Installed | Auto exposure with a fixed-EV comparison is installed; the user raised the ceiling to **+1.3 EV** after run 26 (2026-09-16) and that default sits on main, while the installed `5726a37b…` build still caps at +1.0 EV. | [space-exposure-policy.md](architecture/space-exposure-policy.md) |
| 6 | New material shaders | In progress | 168 reviewed pairs / 137 originals are installed with the distance-fade route default-on under linear materials, but the user plays original hulls, so fill 0.05 (main, after run 26) applies only to converted materials; selective exposure is closed and code-value fill rejected, while the linear-light fill inside the original programs and root-object point-light admission are scheduled default-off after the replay fixture (ratified 2026-09-16). | [original-shading-critique.md](architecture/original-shading-critique.md) |
| 7 | GTAO/SSAO | Closed | The default-off half-resolution chain works (≈180–210 µs CPU, zero when off) but is invisible at X3 viewing distances in runs 19–21; the user closed it on 2026-09-15 and it stays off by default. | [ambient-occlusion-scale.md](architecture/ambient-occlusion-scale.md) |
| 8 | Better directional/self shadows | In progress | The sun-share lane is integrated default-off and run66 attributes its zero availability to a state-gate coverage blocker (53,712 `state` refusals on three XT signatures) plus non-depth writers, with the fix under review; run65's replay-candidate predicates pass and the one-cascade depth replay fixture is in progress. No shadows are applied. | [directional-shadows.md](verification/directional-shadows.md) |
| 9 | Reflections/SSR | Planned | Not started; needs a defined off-screen/environment fallback. | [roadmap.md](architecture/roadmap.md) |
| 10 | Improved/soft particles | Planned | Not started. | [roadmap.md](architecture/roadmap.md) |
| 11 | TAA | Installed | Same-draw motion, engine camera reprojection and the scene-end resolve are verified in game with RCAS sharpen and mip bias; distant asteroid/station shimmer (run 11) remains open and the 0.75 / −0.5 candidate defaults are modeled, not captured. | [temporal-integration.md](architecture/temporal-integration.md) |
| 12 | Volumetric nebula/fog | Planned | Not started. | [roadmap.md](architecture/roadmap.md) |
| 13 | Depth-aware lens effects | Planned | Not started. | [roadmap.md](architecture/roadmap.md) |
| 14 | Additional improvements | In progress | Crypto cache, reader and adjacency fast paths are installed and accepted in runs 17–19, target-name speech and the removed selection pause are accepted (runs 16/18), but run 65 measures a 21.5 s save-load stall again, attributed to per-item overhead on a larger save rather than one blocking phase. | [loading-observations.md](reverse-engineering/loading-observations.md) |
| 15 | macOS menu bar | Planned | Not started; the separate double cursor after alt-tab reproduces in vanilla (run 4, 2026-09-14) and is not a proxy regression. | [window-and-cursor.md](architecture/window-and-cursor.md) |
| 16 | Clustered forward lighting | Planned | Not started; material and light reconstruction precede implementation. | [roadmap.md](architecture/roadmap.md) |
| 17 | Modern third-person chase camera | In progress | Run 26 accepted the raised camera (`--chase-pitch-down-deg 0.5 --chase-offset-y 0.50`), now the default on main; `--chase-view-restore` is implemented and installed but refused at the consume seam on a wrong ref cell in run65, corrected on main and awaiting a gameplay run. Vanilla stays the default and docking is untested. | [chase-view-transition.md](reverse-engineering/chase-view-transition.md) |

Cross-cutting state: emitters above 1.0 use `--emission-source-gain` and
`--screen-emission-additive` (installed, `--linear-emissions` in its
full-surface bracket shape rejected 2026-09-15); run65 accepted bolts at
additive gain 2 while engines were unchanged because the source gain refused
separate-alpha blends, fixed on main ([cost note](architecture/linear-emission-cost.md),
[emission ledger](verification/screen-emission.md)). `--lod-scale` is closed
default-off ([note](architecture/lod-scale.md)). Native Windows runtime
behavior is unverified ([gaps](architecture/platform-portability.md)), and the
session identity logging merged as `5eda356` rides the next candidate.
