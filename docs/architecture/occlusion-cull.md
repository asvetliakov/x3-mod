# Occlusion cull of ship sub-parts

`X3M_OCCLUSION_CULL=on|off` (`occlusion_cull` in `x3m.ini`, default on; user decision 2026-10-08). Skips the game's
draws of turrets, dock ports, antennas and similar ship parts that the previous frame's occlusion test found completely
hidden behind their hull. Render-only: the engine's state, the cull/LOD pass and the simulation are untouched.

Sources: `src/proxy/occlusion_cull_core.h` (pure rules and bookkeeping), `src/renderer/occlusion_cull_pass.{h,cpp}`
(the D3D side), `src/proxy/motion_output_occlusion_cull_inc.h` (draw-site integration), the call site in
`MotionOutput::evaluate_draw` right after the small-prop cull. Evidence: [ledger](../verification/occlusion-cull.md).

## Why

At close capital-ship views 67-91 sub-part draws per frame are fully hidden behind hulls (run14/run15 captures,
`verification/results/occlusion-cull-estimate/`), at a measured 26 us of game-thread submit cost per draw: 1.7-2.4 ms
per frame. Hull pieces draw before the parts they hide, so a test issued at the part's own draw site sees the hull's
depth.

## Candidates

A main-scene draw (gate 2 passed) with z test on and blending off is classified by its scope node's body path through
the engine's body table (the census path, `cull_census_core.h`), cached per model id and memoised per node per frame:

| class | rule (lower-cased path, `/` read as `\`) |
| --- | --- |
| part | a dock cut scene's inline body id (`dock_model`), `ships\props\`, or a path containing `turret`, `weapon`, `gbarrel`, `dock`, `antenna` |
| effect | a path containing `effects\` (engine jets, glows): never a candidate |
| hull | any other `ships\` body |
| other | everything else (stations, environments, menu graphics, unnamed) |

A hull draw records its node as this frame's hull owner, and its parent when that parent is a ship root. A ship root
is a top-level node (null parent): every hull draw with a known ancestry in the run14/run15 captures has ancestry 2
(hull -> root), parts 2 or 3 (`verification/results/occlusion-cull/ancestry.{py,txt}`, measured;
[ship-scene-parts.md](../reverse-engineering/ship-scene-parts.md)). A part's hull "drew this frame" when one of its
ancestors (at most six parent links, ending at its own root) is an owner, so no node above a ship root, and no other
object's root, can carry a hull verdict. Every part draw with a known vertex extent of the buffer's current revision
(the shadow-replay extent cache the small-prop cull and the `object_bounds` rows use; a stale extent of an earlier
revision is treated as no bounds until refreshed) and known clip rows is a candidate; alpha-tested parts are allowed.

The extents exist only while the shadow-replay candidate counter runs (a cascade set, the launcher default, or the
diagnostic counter). Without it the device logs one `occlusion_cull_device attached=0 configured=0
reason=no_bounds_source` row and the cull is off there.

## Mechanism

At the candidate's draw site, before the draw is forwarded (and before its jitter):

1. At the frame's first candidate the pass reads the older frames' queries with `GetData(..., 0)`, never a flush:
   frame N-2's tests that were not ready at their first read are polled again before their slot is reused, then frame
   N-1's. Ready and 0 samples = hidden, ready and any sample = visible, not ready or an error = no result. The decision
   uses the most recent ready result of the same draw, one or two frames old (`ready_age=` counts age 1, age 2, none per
   frame); no ready result draws.
2. It issues this frame's test: the screen rectangle of the vertex-extent box's eight corners (through the draw's own
   clip rows), inflated by one pixel on every side, at the corners' nearest depth (`core::test_rect`), under an
   occlusion query. A rectangle at the nearest depth over the projection is strictly more conservative than the
   12-triangle box: it can only report visible where the box is hidden. It is the estimate's own test (screen box and
   `zmin`), needs two constants instead of a vertex upload, and the one-pixel margin covers the rasterisation rules and
   the frame's sub-pixel TAA jitter.
3. It answers skip when the same draw's (key: scope node, vertex buffer, first vertex, vertex count, model id) most
   recent ready test read hidden, its hull drew this frame and its rectangle is stable against that test's. The real draw is then not forwarded (the hook
   returns D3D_OK, as the small-prop cull does).

The test draw: vs_3_0 `mad o0, v0, c253, c252` over an eight-vertex MANAGED strip (the unit square in both windings;
`D3DCULL_CCW` culls vertices 0-3 and `D3DCULL_CW` 4-7 on both backends, so the application's cull mode is kept and the
winding chosen), a ps_3_0 that writes 0 to every output the device has (oC0..oC3 at most), z write off, alpha test, stencil and
separate alpha off, blend op ADD, and `ALPHABLENDENABLE` with `SRCBLEND ZERO` / `DESTBLEND ONE`, so every bound target
keeps its value. Every output is written because the motion route's lazy mode keeps its own RT1 (A32B32G32R32F motion)
and RT2 (R32F depth) bound between routed draws, and a D3D9 output the shader does not write is undefined (0 x NaN = NaN
under the blend). The targets bound at the test are read from the device (not the shadow: under the HDR redirect RT0 is
the FP16 scene target): each must pass `CheckDeviceFormat(D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING)` and more than one
needs `D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING`, else the part is drawn untested. With RT1/RT2 bound, over 60 frames at
128 tests (measured, `gate.json` "mrt"): zeros + blend 1.3 us (DXVK) / 17 us (wined3d) of pipeline per test with RT1/RT2
byte-identical (a stored -0.0 included); masking RT1/RT2 (COLORWRITEENABLE1/2 0) 64 / 67 us and unbinding them as the
lazy flush does 68 / 58 us, steady-state render-pass splits. The RT0 colour write mask is not touched either (55-79 us on
DXVK). Everything changed is restored from the native getters (shaders, stream 0; the declaration first, then the FVF
when the application's binding was one) and the route's shadow (render states, `c252-c253` when the application wrote
them). A restore failure takes the route's restore-failure path (state invalidated, TAA invalidated).

Refused (drawn untested): no scope node, no extent or rows, a corner at or behind the eye or in front of the near plane,
a depth function other than LESS/LESSEQUAL/GREATER/GREATEREQUAL, sRGB writes, a fill mode other than SOLID, a depth or
slope bias, user clip planes, an application query open, a stream-0 frequency other than 1, a bound target that cannot
blend (or several without MRT blending), a full pool slot.

## Pool and lifetime

One pass per device: 1,024 occlusion queries (two frame slots of 512, the ring; no per-frame allocation), created at
the first part draw through the native `CreateQuery` under the route's reference accounting (`taa_call`; every query
holds a device reference: 1,028 references with the programs, measured). `CreateQuery(D3DQUERYTYPE_OCCLUSION, nullptr)`
other than S_OK: one `occlusion_cull_device attached=0 reason=unsupported` row and the cull stays off until a Reset.
`before_reset` releases the queries and empties the ring; a successful Reset arms the recreation, done at the next
part under the accounting; the first frame after it has no previous result and draws everything. The programs,
declaration and strip survive Reset. Teardown detaches under the accounting.

No budget (user 2026-10-08): every candidate is tested every frame until the frame's 512 queries are used; the rest is
drawn untested and counted `pool_truncated=`.

## Guards

- Stability: the hidden verdict carries over only while the rectangle's centre moved at most a quarter of its size
  (16 px minimum) and its area changed at most (4/3)^2. A turret turning in place keeps it; a teleport, a camera cut or a
  fast pan draws the part that frame (`unstable=`).
- Hull: a part whose hull did not draw this frame is drawn (`no_hull=` counts its tests).
- Reset: the first frame after draws everything.
- Re-emergence: a part revealed with an unchanged rectangle (the hull moved away) is skipped once more and drawn from the
  next frame: one frame late (`drawn_late=`), accepted; two frames late when that frame's decision used an age-2 result
  (its frame N-1 test not ready yet).

## Cost (measured, bottle X3, 2026-10-08)

Per candidate on DXVK (CrossOver's bundled DXVK over MoltenVK): 3.0-3.2 us CPU p50 on the game thread including the
readback (max 3.37 us in the first run, 4.72 us in the rerun of 2026-10-08: run-to-run spread), 4.8-5.2 us p50 pipeline
time (DXVK's worker and the GPU, 5.5 max); the query's own share 0.4-1.4 us. Per frame: 128 tests 0.39-0.60 ms CPU /
0.6-0.67 ms pipeline; 512 tests 1.56-1.64 ms CPU / 2.7-2.8 ms pipeline. Every test's result was ready one frame later at
13 and 20 ms frame pacing up to 512 per frame. At the close views (130-190 candidates, 67-91 hidden) the CPU cost is
0.4-0.9 ms against a saving of 1.74-2.37 ms. wined3d: 1.8 us CPU and 20-23 us pipeline per test (the
saving there was not measured). A per-test dynamic-buffer `Lock` costs 50-80 us on wined3d, hence the constants.

## Logging

`occlusion_cull_config` once at start-up; `occlusion_cull_device` at attach (or `reason=no_bounds_source`); under
`--debug` one `occlusion_cull` row per frame with candidates (`candidates tested hidden skipped pool ready not_ready
ready_lag2 ready_age=age1,age2,none errors drawn_late pool_truncated unstable no_hull refused failed no_bounds unbounded
state`); in every tier one `occlusion_cull_session` row every 300 frames with the session totals.

## Known limits

- A skipped part is not a shadow-replay caster that frame: a hidden turret's sun shadow on visible hull disappears
  while it is skipped (the small-prop cull has the same property).
- Only the part draws themselves are skipped; nothing is skipped on the engine side.
