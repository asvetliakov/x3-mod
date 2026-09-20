# Run57C / run205 bounded acceptance evidence

User reports no flashes and no smear, old scenery or ghost trails during the tested save load and sector travel; user explicitly accepts disabling both heuristic cuts by default. These are user observations, not image measurements from this log.

Session `/tmp/x3-bottleX3-run205/session-20260921-032344-212.log`: 193,872,681 bytes. BottleX3; same a51d1e75 DLL/source85da89a8 as A/B. Header and runtime mode both confirm median1e30 (FP32-rounded runtime value), missing1, per-frame motion/frame-end/camera logging; camera20deg, gain4, fade80/220/floor1, fog0.

Measured: 26,371 contiguous frames0–26370; elapsed3.154–370.786s. **Zero global heuristic cuts.** Old bounds were exceeded on70 median>48 rows and76 missing>.25 rows (142-frame union). 24,629 TAA attempts all resolved successfully;24,620 used history.1,742 rows skipped TAA (skip2/no attempt); no apply/restore failures.

Nine attempted/resolved frames had history0:
- Initial first resolved scene frame1579, elapsed43.865s; no previous valid camera, no camera_cut. Loading log explicitly marks save_load_begin1578 and save_load_complete1579.
- Retained chase-snap cuts1735,6664,6665,21242, elapsed46.003,102.943,102.966,287.156s. Their camera reasons are0,3,0,3 and rotations.8527,0,0,0deg. Source `motion_output.cpp:1623–1625` separately ORs chase-snap generation into camera_cut; `camera_reprojection.h:138–143` does not set rotation cut for reason0/3. This classifies these four as chase-snap cuts, not >20deg cuts.
- Retained rotation cuts21418,21476,21507,21573, elapsed290.299,290.797,291.060,291.621s; rotations97.5583,167.1741,117.7961,177.7595deg, camera reason4. All camera-cut rows have cut0 (the heuristic bit).

No explicit D3D-reset event is present. The sole object_lifetime startup row says active_without_baseline; no load/registry epoch values are logged here. The log proves a save-load interval and retained chase/rotation invalidation, but does **not** prove sector-travel epochs changed or every transition path was exercised. User's successful sector-travel observation remains the acceptance evidence for that scenario.

Reproduce: `python3 verification/results/run57-heuristic-cuts/summarize.py`. This streams the log, emits compact `summary.json`, and asserts matching frame counts, contiguous frame IDs, runtime thresholds, frame-log1 and zero heuristic cuts. No source changes, Wine/game, build or fixture work performed.
