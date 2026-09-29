# run385 draw attribution and LOD state (three F8 bursts)

Source: `/tmp/x3-bottleX3-run385/session-20260929-173412-212.log` (Run109 DLL 09f08cff…, commit a16a47f5,
Mayhem 3, 5120x1440, `--direct --debug --perf --config`). Producer: `python3 attrib.py <log> run385_attrib.json > attrib_out.txt`
(one pass, ~1.3 s). `run385_attrib.json` holds every table below; `attrib_out.txt` is the printed report.
Method: draw / object_context / object_ancestor(+ancestry) / capture_event draw_begin / cull_census joined by
(frame, index/node); unnamed (`body=-`) nodes are attributed to the named hull/station drawn under the same
scene-graph root. Drawn LOD predicted from `s`, `thr` with f = 1.0 and the Very High `-1` (object_fade
`config768=3`, measured) and checked against `lod=`. km = units / 505 / 1000 (505 u/m inferred).

## Per burst (frame 0 of each burst; the 8 frames differ by <= 13 draws)

| | A 4827-4834 | B 8142-8149 | C 10210-10217 |
| --- | --- | --- | --- |
| frame_end draws / issued (8 frames) | 642-650 / 615-623 | 375-381 / 284-290 | 366-387 / 344-365 |
| captured dt (capture-inflated) | 877-940 ms | 735-811 ms | 727-791 ms |
| uncaptured dt p50, 120 frames before / after | 32 / 34 ms | 21 / 22 ms | 22 / 22 ms |
| frame_timing window before / containing: dt p50, draws p50, us/draw | 4800: 21.6 ms, 345, 26.4; 5100: 34.0 ms, 642, 25.7 | 8100: 22.6, 378, 24.4; 8400: 29.2, 493, 25.8 | 10200: 23.3, 398, 24.9; 10500: 23.1, 357, 25.9 |
| frame_phases views / pre_render p50 (before; containing) | 10.6 / 6.6; 21.8 / 7.9 ms | 10.3 / 7.5; 17.0 / 8.3 ms | 12.9 / 6.6; 12.6 / 7.1 ms |
| loop_phases input / region / containers / cutevent p50 (before; containing) | 6580/1388/1006/351; 7928/1523/1139/368 us | 7453/1655/1221/401; 8304/1799/1333/421 | 6641/1456/1036/383; 7068/1497/1059/415 |
| scene_end_marker | 460 | 181 | 244 |
| lens-flare/glow sprites (post, `v\007xx`, `v\010xx`) | 168 | 181 | 115 |
| HUD (post, menugfx + no census) | 15 | 15 | 15 |
| unnamed carrier parts (`body=-`, lods=1) | 186 (Raptor 117, Ocelot 69) | 27 (Raptor) | 25 (Raptor) + 27 (Sadiablo freighter) |
| hull draws | 48 | 23 | 17 |
| turret props drawn / skipped by prop cull | 77 / 27 | 14 / 90 | 9 / 21 |
| stations | 29 | 12 | 53 |
| asteroids | 68 | 0 | 78 |
| engine glows / sky / other | 15 / 5 / 5 | 7 / 5 / 3 | 5 / 5 / 4 |
| census kept / culled_min / culled_small / culled_size / culled_prop | 453/128/327/52/27 | 291/70/122/62/90 | 295/138/356/74/21 |
| duplicate draws (same node, shaders, prims, vb, ib, decl) | 0 | 0 | 0 |
| capture draw_begin gap p50 per class | 548-609 us, every class | 578-704 us | 545-614 us |

The capture-frame per-draw gap is uniform across classes (capture logging dominates), so it cannot split engine
CPU by class. The uncaptured windows give 24-26 us per draw on average and views tracks the draw count
(345 -> 10.6 ms, 642 -> 21.8 ms).

## Hull LOD state (kept rows; all kept census rows match the predicted LOD: A 453/453, B 291/291, C 295/295)

| body | radius | thr | effective 0->1 (Very High) | A: d km, s, lod, draws | B | C |
| --- | --- | --- | --- | --- | --- | --- |
| split_m1_raptor\hull | 539786 | 100000,30,120 | s < 120, beyond 5.70 km | 3.32, 206, 0, 15 | 7.82, 87, 1, 2 | 13.43, 50, 1, 2 |
| split_m2p_ocelot\hull | 925654 | 100000,49,183 | s < 183, beyond 6.41 km | 6.17, 190, 0, 13 | 18.12, 64, 1, 2 | not in census |
| split_m4_scorpion\hull (own ship) | 6244 | 100000,30,15,5 | 0.53 km; 1->2 at 1.58 km | 0.03, 269, 0, 2 | same | same |
| split_m7_cobra\hull | 208882 | 100000,30,120 | 2.21 km | 8.02/10.50, lod 1 | 12.86/15.35, 1 | 3.88, 1 |
| split_m6_* (appallox, dragon, heavy_dragon), tp_nomad | 38843-52787 | 100000,30-48,120-177 | 0.28-0.48 km | 9-10 km, lod 1 | 15-17 km, 1 | 5.6-14 km, 1 |

Raw loop boundaries D_i = r*640/T_i: Raptor 22.80 (i=1), 5.70 (i=2) km; Ocelot 23.94, 6.41 km. With T2 > T1 and the
Very High `-1`, the loop's first hit is i=2 below T2, so record 1 is drawn from 5.70 / 6.41 km outward and record 2
is never drawn (inferred from the selection rule, consistent with every row). Record-0 hulls: Raptor 3.32 km and
Ocelot 6.17 km (A), Scorpion 0.03 km (all), each inside its switch distance. Distant full-detail geometry comes from
single-record bodies (lods=1), not from wrong switches: unnamed carrier parts, asteroid_A/B_ClassMine (29-49 km),
engine glows, sky.

## Props (census and cull_small_prop_box on frame 0)

A: prop census kept 76, culled_prop 27 (split_m1turretB_socket 24, m7turretB 3), engine culls 58; kept boxes 76,
extent 4.2-37.4 px, p50 5.9 px. B: kept 13, culled_prop 90 (the Ocelot's 72 m1turret props at 18 km plus Raptor
props), kept extent 4.1-37.4 px, p50 5.5 px (6 are the own ship's weapondummy). C: kept 8 (6 weapondummy + 1 m7
turret + 1 exempt target), culled_prop 21. cull_small_props_frame (300 frames): 4882 culled 10728 / kept 13177;
8182 26559 / 4641; 10282 6600 / 2400.
