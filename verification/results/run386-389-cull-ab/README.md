# Run 110 A: engine-side lens-flare cull and carrier dock-port cull (run386-389)

Source logs: `/tmp/x3-bottleX3-run38{5,6,7,8,9}/session-*.log` (Run110 DLL e5e7ac15…, Mayhem 3, 5120x1440, `--direct --config`,
`fps_overlay = 1`; run385 = Run109 reference with `--debug --perf`). Producer: `python3 extract.py cull_ab.json`
(one pass per log, about 1.5 s; prints the `compare` block). `cull_ab.json` holds every table below.

Method. The plain runs (386-388) log `frame_end` only at frames 0 and 3600, so there is no per-frame dt, p50/p95 or issued
count for them. Timing is the 300-frame mean frame time from `media_cue_window` QPC deltas (10 MHz), checked on run385/389
against the per-frame `frame_end` mean (QPC mean = integer dt_ms mean + 0.5 ms, every window). Frames are aligned to the
sector's first frame S (first `cull_small_props_frame` window). Scene identity per window: prop-cull candidates per frame
(`cull_small_props_frame draws/300`) and flare draws per frame (`lens_flare_gain_frame draws/300`).

## Plain runs, 300-frame means (measured)

| | S | phase 1 (post-load view, props 82/frame) | phase 2 (carrier view, props 102/frame; 387: 122) | flares/frame |
| --- | --- | --- | --- | --- |
| 386 defaults | 648 | rel 252-851: 14.50, 14.51 ms (69.0 fps) | rel 1452-2351: 17.43, 17.56, 18.10 ms (57.4/57.0/55.3 fps) | 166 / 169-173 |
| 387 gain 0 | 401 | rel 199-798: 14.42, 14.55 ms | rel 1399-2298: 17.27, 17.41, 18.78 ms (57.9/57.4/53.2 fps) | 0.1-0.3 (skipped= 30-85 per 300) |
| 388 dock off | 557 | rel 43-642: 14.63, 14.93 ms (then 14.49) | rel 1243-2142: 19.77, 19.82, 21.23 ms (50.6/50.5/47.1 fps) | 166 / 170-180 |

Phase-2 pairs matched by sector-relative time (<= 150 frames apart): 387-386 = -0.16, -0.15, +0.68 ms;
388-386 = +2.39, +3.67 ms (the third 388 window is excluded: its view changes, props 97.6). Phase 1 has identical prop
counts and cull numbers (21897 / 22199 per 300 frames) in all three runs, and the three runs agree within 0.5 ms there:
that is the run-to-run noise at an identical scene. Within one run at constant prop count, adjacent windows differ by
0.05-1.4 ms; run385 shows a 16.4 -> 19.3 ms drift over 3,600 frames at a constant 335-337 draws (time since load matters).
`frame_end` frame 3600 (single frames, different views): 386 509/484 draws/issued, 387 262/262, 388 372/370.

## run389 (gain 0, --debug --perf), measured

- `lens_flare_cull status=patched reason=ok`; `lens_flare_cull_bodies bodies=42 mapped=42 scanned=15 fixed=11000 dynamic=21 restarts=0`
  (frame 2, one row, no restart). `culled=` per 300 frames 49876, 45348, 52945, 50948, 50594, 52336, 51205, 44974 (150-176 per
  frame); phase 1 49876 = 166.3/frame against 386/388's 166.2 flare draws/frame in the same view.
- `lens_flare_gain_frame skipped=` 43, 69, 68, 53 per 300 frames (frames 900-2100), 0 elsewhere; never gained, no refusals.
  Drawn `v\` bodies in the 24 burst frames: `v\01009` (post, lens bracket, model 0x4e33 = dynamic id 20019) 2x in burst B,
  `v\10667` (scene part, fixed id 10667) 4x across bursts. Neither is in the 42-name set (`src/proxy/lens_flare_cull_core.h:39-44`);
  run385 drew both too (`v\01009` at frames 4833, 4834, 10214).
- Dock cull per burst (`dock_culled=` row = census `culled_dock` on every frame): A 1038-1045: 1,1,1,1,1,0,0,0 (one node, s=3 at
  16 km); B 1742-1749: 27,29,31,32,31,33,33,33 (frame 1742: 24 Raptor nodes at 3-4 km with s 3-7, 3 nodes at 10 km with s 3-4);
  C 2940-2947: 3 each (9 km, s 3-5). Max culled s = 7 < dock_threshold 8. Kept unnamed carrier node in B: one, s=12, radius
  39157, 4.01 km, lods=1, 25 draws (the same 25-draw node run385 kept at s=14).
- Issued per burst frame: A 137-138 (post-load view, not the carrier view), B 201-204 (draws 275-278, 74 props skipped by the
  proxy prop cull), C 166-167. run385's 615/286/350 were at other views (A: Raptor 3.32 km, Ocelot 6.17 km at LOD 0, 168 flares).
- `frame_end` sector frames without capture: 2763, dt p50 20 / p95 25 ms; >50 ms: 354-355 (load), 1276 (325 ms, view switch to
  the carrier, 280 draws), 1431-1432 (90/62 ms). run385 has the same view-switch hitch (frame 1433, 318 ms).

## --perf windows (frame_phases), measured; fits inferred

| | window | dt p50 | issued | flares | views | overlays | view_setup | pre_render |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| run385 phase 1 | 900 | 16.2 | 261 | 166 | 7.89 | 2.68 | 0.87 | 4.86 |
| run389 phase 1 | 900 | 15.0 | 139 | 0 | 8.58 | 0.20 | 1.49 | 5.42 |
| run389 carrier view | 1500 / 1800 | 21.2 / 21.4 | 201 | 0 | 13.33 / 13.57 | 0.25 / 0.24 | 1.37 / 1.34 | 6.76 / 6.66 |
| run385 burst A containing | 5100 | 34.0 | 637 | 172 | 21.77 | 2.96 | 1.48 | 7.93 |

Least squares over uncaptured sector windows: views = 37.1 us x non-flare issued + 4.52 ms (run385, 29 windows) and
40.5 us + 4.01 ms (run389, 5 windows); overlays = 10.8 us x flares + 1.12 ms (run385). run389 overlays are 0.20-0.25 ms in
every window with 150-176 flares culled per frame. The flare sprites are drawn in the overlays phase, not in views.
run389 phase 1 drew about 70 more props than run385 (72 turret props exempt as the target; `cull_small_prop_box`
exempt_target=72 at 1038), so the two phase-1 views are not identical.
