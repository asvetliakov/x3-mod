# Frame draw counts

Ledger for the per-frame draw count (`frame_end ... draws=`, the `--perf` overlay's `DRAWS`).
Scripts and their small outputs: [`verification/results/run341-draw-calls/`](../../verification/results/run341-draw-calls/)
(`commands.txt` lists each producing command). Session logs are local (`/tmp/x3-bottleX3-run337..343`).

## 2026-09-26: what the counter counts, the two plateaus, the 300 (runs 337-343)

**What is counted (source, measured against capture rows).** `ctx.draws` is incremented only in
`snapshot()` (`src/proxy/capture.cpp:1040`), which the four application draw hooks call
(`draw_primitive` :2075, `draw_indexed` :2134, `draw_up` :2194, `draw_indexed_up` :2232), and is zeroed
after each Present (:1926). The proxy's own passes (shadow replay `shadow_replay_pass.cpp:512`, sun shadow
apply, sun occlusion, fog, TAA, HDR, bloom, the fps notice) draw through saved native vtable slots and are
not counted. Check: in every captured frame the `draw` row count equals `frame_end draws=` (e.g. run341
frame 810: 236 = 236). The `--perf` overlay shows the same counter as a rolling mean over four 250 ms
buckets, i.e. about the last second (`src/proxy/fps_overlay.h:25-51`). The LOD overlay is baked data the
game loads, so its merged draws are game draws and are counted like any other.

**Plateaus (measured, `plateau-check.txt`).** Every frame with draws >= 500 in all seven sessions is in
the main menu (sector rule `no_cockpit`, no `shadow_replay_depth` row): 175 / 506 / 677 / 115 / 72 / 70 / 42
frames in runs 337..343; in-flight maximum 328 / 444 / 419 / 337 / 327 / 327 / 327. The earlier whole-session
p90 of ~495 comes from the menu frames of the short sessions (run341: 304 menu frames of 2337). The ~230
vs ~106 difference is the scene: run337 flew only in backgrounds 14 (p50 117) and 67 (p50 102) and never in
background 21 (greenvoid), the Run 91/92 stand. Within one build, run339 had p50 100 in background 67 and
p50 294 in background 21. Neither the cascade layout nor `--taa-debug` can change the counter (proxy passes
are uncounted); background 21 without `--taa-debug` (run338) had p50 238.

| Session / sector segment | frames | p50 | p90 | max |
| --- | --- | --- | --- | --- |
| run337 bg14 / bg67 | 1965 / 8685 | 117 / 102 | 134 / 120 | 328 / 193 |
| run338 bg67 / bg14 / bg21 | 8468 / 5742 / 5167 | 99 / 120 / 238 | 124 / 215 / 355 | 196 / 408 / 444 |
| run339 bg67 / bg21 | 5149 / 4976 | 100 / 294 | 124 / 394 | 186 / 419 |
| run340 / 341 / 342 / 343 bg21 | 3928 / 2033 / 940 / 1696 | 227 / 230 / 230 / 232 | 260 / 253 / 294 / 253 | 337 / 327 / 327 / 327 |
| main menu, all runs | 42..677 per run | 496..530 | 524..545 | 527..559 |

**Breakdown of captured frames (measured, `draw_breakdown.py`).** Categories: background = before the first
depth write; opaque = depth-writing scene-node draws; hud = after `scene_end_marker` on the main target;
game post = after the marker on the game's 2560x720 target.

| Frame | total | background | opaque | blended | additive | game post | HUD | proxy shadow replay (uncounted), per cascade 0..4 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| run341 810 rest | 236 | 5 (2%) | 203 (86%) | 3 (1%) | 0 | 3 (1%) | 22 (9%) | 493: 25/28/115/150/175 |
| run341 986 pan | 227 | 5 | 204 (90%) | 3 | 0 | 3 | 12 (5%) | 499 |
| run341 2098 firing | 247 | 7 (3%) | 203 (82%) | 5 (2%) | 4 (2%) | 3 | 25 (10%) | 510: 23/24/92/173/198 |
| run340 4081 moving+pan | 312 | 5 | 279 (89%) | 3 | 0 | 3 | 22 (7%) | 525 |
| run339 2242 bg67 (~106 class) | 109 | 6 (6%) | 68 (62%) | 18 (17%) | 2 (2%) | 3 (3%) | 12 (11%) | 152 |

Firing adds about 11-16 draws (bolts and effects: +2 blended, +4-5 additive, +2 background, +3 HUD).
The menu plateau has no capture, so it has no producer breakdown.

**The 300 (measured).** In runs 341-343 the 290-310 frames are 111 / 85 / 83 and, except one frame
in run343 (1244), all fall in the first 1.5-2.5 s after `save_load_complete` (frames 203-353, 218-324,
191-293). They end when `lod_switch` rows move `argon_spacedock` and `military_outpost_middleb` from lod 0
to lod 1 (run341 frames 381/387), after which draws settle at ~188-236. The overlay's one-second mean
reads ~295-320 there. Other ~300 scenes in flight: run340 capture 4081-4085 (299-312, 279 opaque draws on
84 nodes) and run339's background-21 span 8880-11039 (270-397). In the latter, `lod_switch` rows show
Argon M7/M1 capital ships and turret props entering finer LODs (inferred attribution, no capture).

**Avoidable work (measured, `nodes-run341-810.txt`, frame 810).**
- The proxy's shadow replay issues 493 native draws, about 2.1x the game's 236 (uncounted). Of 257 caster
  rows, 90 are retained casters the game did not draw this frame: 182 of the 493 issues, but only 45,711 of
  1,606,685 issued primitives (2.8%). The logs do not record whether a retained or a cascade-4 caster
  shades a visible receiver.
- 5 nodes hold 110 of the 203 opaque draws: `military_outpost_middleb` lod 0 (34, s=1741), two unnamed
  nodes of model 35ba45c3 (census `body=-`, 1 LOD record) at s=4 and s=5 with 25 draws each, `owp_large`
  lod 0 (20, s=104), `argon_L_solarpowerplant` lod 1 (6). The two 25-draw nodes are 50 draws (25% of
  opaque) for objects the census sizes at 4-5. They have a single LOD record, so the LOD overlay has no
  coarse record to replace.
- 53 of the 203 opaque draws are on nodes at lod > 0, mostly 4 per node.

**2026-09-26: model 35ba45c3 identified (measured, offline).** It is the carrier hangar interior
`EObject01`, the inline body `P 3; B 100003` of the scene `stations\docks\DockCarrier_scene`
(`types/CutData` row 9014; `02.cat` `objects/stations/docks/dockCarrier_scene.pbd`). The two frame-810
nodes sit under the Argon M1 (`36480a00` → `36480000` → root `3647fb00`, whose other child is
`argon_M1`) and the Argon M7 (`3849b310` → `3849a910` → root `384980f8`, sibling `Argon_M7`), per the
`object_ancestor` rows. A scene-embedded body's model id is `local + (cut_id − 1) · 100000` (EXE
`0x004920f1..0x00492105` for text scenes, `0x00491521..0x00491534` for binary CUT1), so
901400003 = 100003 + 9013 · 100000. The body table only has slots for ids below about 20000 + 2,200, so the
census prints `body=-` for every such id. The inline body has 1 LOD record, scale 39157 (= census
`radius`), 1 part and 25 groups (= 25 draws), and 3,994 faces. Its per-group face counts equal the frame's
25 `motion_route primitives=` values in order on both nodes. The frame's other `body=-` ids are the same
scene's doors (`35ba45c4`/`c5`, 1 draw each), `DockCarrier_quicklaunch_scene` (`35b8bf20/21/23`) and
`dock5ports_arm_scene` (`35b42b43/44`); all of them have one LOD.
Method: `verification/results/run341-draw-calls/name_model_id.py <hex id>` or `--log <session.log> --frame <n>`
(output `unnamed-models-run341-810.txt`). The overlay baker cannot give it a coarse record today:
`bob1.parse_text` refuses text scenes, and the overlay addresses `objects\<body name>` members, which an
inline scene body does not have. The logs do not record whether the hull hides the interior; its 12×6 px
bounds lie inside the M1's bounds.

## 2026-09-29: `issued=`

`frame_end` appends `issued=`: the application draws the proxy forwarded to the device (`ctx.issued`, counted in the
four draw hooks when the route submits, zeroed with `draws` after each Present). `draws=` keeps its meaning (every
hooked application draw, including the ones the proxy answers with `D3D_OK` without a device call: lens-flare gain 0,
the bolt single copy, the small-prop cull, a lost motion state). The `--perf` overlay's DRAWS figure now shows issued
draws; run376 showed 346 there with the flare skip active because it counted skipped draws.
