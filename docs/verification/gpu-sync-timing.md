# GPU sync timing (`--gpu-sync-timing`)

Design and log format: [engine-frame-time.md, "GPU sync timing"](../architecture/engine-frame-time.md#gpu-sync-timing).

## 2026-09-23: implementation (uncommitted worktree, not installed)

- Host: `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_gpu_sync_timing`
  (core arithmetic through `verification/probe/gpu_sync_timing_core_fixture.cpp` in release and
  ASan/UBSan builds, 32 checks; launcher dry run and `--vanilla` refusal; the runner's parse and
  acceptance on synthetic transcripts; the production wiring of all 14 passes).
  8 tests OK (measured). Full host suite (`run_host_suite.py`): 239 modules, 2,450 tests, 0 failing
  (measured; the bloom-lifetime fixture and the fog-card mock gained the off-state helpers).
- Build: all targets from a scratch CMake tree (MinGW i686, RelWithDebInfo) with 0 warnings
  (measured); `verification/probe/check_no_x87.py` on the DLL: PASS, 660 reachable, 0 violations
  (measured). `GpuSyncTiming::detach`/`release` are reachable from the light hooks through the
  device context's destructor; they hold only the FNSAVE/FRSTOR transport the audit allows.
  Fixture executable sha256 `8136127c…8fb8` (build-gst-51513, uncommitted tree).
- Wine fixture: `verification/probe/gpu_sync_timing_fixture.cpp` (CMake target
  `gpu_sync_timing_fixture`; 30 checks: both soft-fail paths, references, CPU state across a
  boundary, 48 frames of fake passes in three 16-frame windows, exact sync count, Reset,
  failed Reset, detach). Runner:
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_gpu_sync_timing.py --exe <build>/gpu_sync_timing_fixture.exe`
  writes `verification/results/bottle-X3/gpu-sync-timing.{json,txt}`.
- Not verified: native Windows behaviour; the in-game pass figures (one diagnostic flight).

## 2026-09-23: Wine fixture in bottle X3 (orchestrator run)

`verification/results/bottle-X3/gpu-sync-timing.json` (tracked on main; exe `8136127c…8fb8`,
built from the round-0 tree): passed, 30/30 checks, 15.6 s (measured). Attach 28 queries holding
28 device references; both soft-fail paths refused cleanly; 48 frames in three 16-frame windows
with 1,440 syncs (28 boundaries + the second Bloom pair per frame), 0 issue/data failures, 0
timeouts, 0 dropped frames; Reset released and recreated all 28. Sync floor: empty pair 18 us
median, a pair around one 16x16 quad 264 us median; the fixture's heavy fog pass (96 blended
full-screen quads plus the nested light pass) 5,760 us; serialised dt median 24,064 us.

## 2026-09-23: review round 1

Queries released only at the frame's end under `BloomOperation` after the failure cut-off
(references zeroed before the release loop); four session timeouts latch a cut-off no Reset
lifts; 50 ms spin limit while the previous boundary failed; native `TestCooperativeLevel` at
the frame's `scene` begin abandons a lost frame without a spin; an abandoned frame files no dt;
the fixture's CPU-state check also compares the x87 control word (FNSTCW). The fixture source
changed, so the recorded exe predates it: rebuild before the next run.

## Log columns

| Column | Meaning |
| --- | --- |
| `window` | 300-frame window index (1-based) |
| `pass` | pass name; `none` when a window measured no pass (the row still carries dt) |
| `median_us`, `p90_us` | nearest-rank window figures of the serialised span per frame (CPU submission plus GPU execution, includes about 18 us sync floor per pair and any nested pass; pairs of one pass in a frame add up) |
| `n` | frames of the window that measured the pass |
| `wait_median_us` | the end spin alone: GPU work still pending when the CPU finished submitting |
| `session_n`, `session_median_us`, `session_p90_us` | the session so far, from the histogram (exact below 64 us, <= 6.25 % above) |
| `dt_median_us`, `dt_p90_us` | Present-to-Present CPU interval of the serialised frames in the window (abandoned frames excluded) |
| `frames`, `window_frames` | first..last frame of the window and its frame count |
| `dropped` | frames abandoned by a failed or timed-out sync or a non-cooperative device |
| `unclosed` | passes still open at a frame's end (not filed) |

`volumetric_fog_repair_census` (one row per window while the device offers `D3DQUERYTYPE_OCCLUSION`; fog-gpu-cost.md
step A): `n` frames of the window with a fog repair draw; `median_ppm`, `p90_ppm`, `max_ppm` the pixels the repair
wrote per frame in parts per million of the target (`area`); `last_pixels` the last such frame's count; for counted draws of kept frames whose result was not read, `unread`
(`GetData` S_FALSE: not ready at the frame's end, expected 0), `lost` (`D3DERR_DEVICELOST`) and `failed` (any other
refusal) apart. The repair's `clip` drops pixels that need no
repair and pixels whose full-resolution march comes out exactly empty (T 1, S 0), so the count is a lower bound on the
marched repair pixels, equal to it wherever every repaired ray crosses fog.

## Run 274 (Run 73 C, 2026-09-23): first flight, 1920×1080

Diagnostic ran (available=1, 28 queries, 15 windows, dropped 0, no timeouts, no Reset). Per-pass medians and the
reading of them are in [engine-frame-time.md, "Run 274"](../architecture/engine-frame-time.md#run-274-gpu-per-pass-cost-at-19201080-run-73-c-2026-09-23):
proxy passes 11.7 ms serialised vs engine span 4.7 ms in the fogged stand window; fog_route 4.43 and taa 2.90 ms
are the two large ones. Open: the final window and the `gpu_sync_timing_summary` rows were not written at exit;
per-frame rows (or an inside/outside-engine tag on hdr_writeback flushes) are needed to isolate an F8 burst.
Summariser: `verification/results/run274-gpu-sync/gpu_sync_windows.py`.

## 2026-09-24: `taa_*` sub-passes (uncommitted worktree, not installed)

Five passes appended after `present` (indices of the first 14 unchanged): `taa_copy`, `taa_mask`, `taa_box`,
`taa_resolve`, `taa_display`, marked inside `TemporalPass::run` through `configure_sync_timing` (the motion output
sets it before every run; null when the option is off: one branch per boundary, no device call). 19 passes, 38
queries. Each pair adds about 0.26 ms to `taa` and to the serialised frame, so the next flight's `taa` reads about
1.1 ms higher than run274's for the same work (four pairs at the 0.264 ms light-pair floor on the HDR route, where
`taa_display` does not run; inferred). What each
covers: [engine-frame-time.md, "TAA stage cost"](../architecture/engine-frame-time.md#taa-stage-cost-2026-09-24).

- Host: `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_gpu_sync_timing`,
  8 tests OK (measured): the core fixture checks 19 passes / 38 boundaries and the new names; the wiring test finds
  one begin and one end site per sub-pass in `temporal_pass.cpp`.
- Wine fixture (`gpu_sync_timing_fixture.cpp`): each frame now nests the five sub-pass pairs inside `taa`; the
  nesting check requires `taa` >= each sub-pass median, the light reference is `taa_resolve`, and the exact sync
  count follows `boundary_count` (48 x (38 + 2)). Check count unchanged (30). Run in bottle X3 (scratch CMake build,
  exe `9ba31650…ac7e`, 8.5 s): PASS 30/30, 38 queries holding 38 device references, 1,920 syncs, 0 failures, 0
  timeouts, 0 dropped frames, Reset recreated all 38; each sub-pass around one 16x16 quad reads 188–190 us window
  median and the `taa` pair nesting all five 1,049 us (measured). The runner's rewritten
  `gpu-sync-timing.{json,txt}` were restored to the committed ones.
- Temporal pass (`TemporalPass` null marks) and motion output unchanged: see "TAA stage cost" for the
  `run_temporal_pass.py` (744 / 278 / 546, report byte-identical) and `run_motion_output.py` (190 cases, 271,369
  checks, 0 differing stable fields) results.

Note (2026-09-23): the committed `verification/results/bottle-X3/temporal-lattice.txt` still carries the pre-cut instruction-slot rows (line_mask 368, line_mask_camera 344, thin_box_rows 35); the post-cut counts 399 / 372 / 77 come from the TAA cost worktree's lattice run and are refreshed by the next full temporal run that is committed.

## 2026-09-23: `fog_*` sub-passes and the repair census (fog-gpu-cost.md step A; uncommitted worktree, not installed)

Three passes appended after `taa_display` (indices 0-18 unchanged): `fog_march` 19, `fog_composite` 20, `fog_repair`
21, one `Span` each around the stored path's three quads in `FogPass::execute` (null marks when the option is off: one
branch per boundary, no device call). 22 passes, 44 event queries, plus one optional occlusion query for the census
(45 device references in the fixture). Each fog pair adds about 0.264 ms to `fog_route` and the serialised frame
(inferred from the light-pair floor). Census: `Marks::census_begin/census_end` (default no-op), `CensusSpan` inside
the `fog_repair` Span; `GpuSyncTiming` issues BEGIN/END on the first bracket of a frame and reads the count without
FLUSH in `frame()` after the Present pair retired the GPU (no extra spin); the tracker files it with the frame
(an abandoned frame drops it) and reports n / median / p90 / max ppm per window.

- Host (measured): `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_gpu_sync_timing
  verification.analysis.test_fog_density_shaders verification.analysis.test_volumetric_fog verification.analysis.test_fog_route_bridge`
  39 tests OK; the core fixture now has 35 checks (22 passes / 44 boundaries, the fog names and indices,
  `census_ppm`, census filing with abandoned and unread frames, per-window reset); the wiring test finds one Span per
  fog sub-pass in `fog_pass.cpp`.
- Wine fixture (bottle X3, scratch CMake build of the final tree, exe `fe5bbba4…`, 10.3 s): PASS 32/32 (was 30: one
  `*_repair_census_exact` check per phase), 44 queries + census holding 45 device references, 2,208 syncs
  (48 x (44 + 2)), 0 failures / timeouts / dropped frames, Reset recreated all 45. Each frame nests the three fog pairs
  in `fog_route` (window medians 176-195 us around one 16x16 quad each) and brackets the `fog_repair` quad with the
  census plus a second 32x32 bracket that must not count: every window n=16, 256 pixels, 277 ppm of 1280x720,
  unread / lost / failed 0, also after the Reset (measured). The core fixture feeds not-ready, lost and failed reads
  and checks each counter apart. The runner's rewritten `gpu-sync-timing.{json,txt}` were restored.
- Production FogPass fixture: `repair_census_counts_the_pixels_the_repair_writes` (volumetric-fog.md, same date).

## Run 280 (Run 75 C, 2026-09-23): the TAA and fog splits at 1920×1080

Run75 DLL (22 passes; `taa_display` has no rows on the HDR route, so `taa` carries four sub-pairs). Medians in ms,
each sub-pass including its own 0.264 ms floor (measured; `verification/results/run280-gpu-sync-split/`):

| window | taa | copy | mask | box | resolve |
| --- | --- | --- | --- | --- | --- |
| busy clear sector (Oort Cloud, idx 21), still | 6.0 | 0.45 | 2.13 | 1.18 | 2.08 |
| same, fast flight | 6.2–6.3 | 0.46 | 2.02–2.07 | 1.17 | 2.45–2.55 |
| fogged sector (idx 2), turning in place | 4.0 | 0.45 | 1.23 | 0.83 | 1.27 |
| fogged, moving | 4.2 | 0.45 | 1.33 | 0.83–0.93 | 1.37 |

Reading: motion adds 0.4–0.5 ms and only in the resolve (the history lookup, inferred); the sector difference is
content: the three stabiliser mask draws (+0.8 ms), the resolve (+0.7) and the box (+0.35) all grow with hull
coverage. The mask passes are the largest single TAA cost in a busy sector, ahead of the resolve. Fog, fogged sector:
`fog_route` 6.65–6.88 = `fog_march` 4.79–5.01 + `fog_composite` 0.55–0.57 + `fog_repair` 0.57–0.58 + motes 0.21 +
0.5–0.6 exclusive; net of floors the march is 4.5–4.7 ms (the cost model said 2.9–3.3), composite and repair about
0.3 each; the repair writes only 60–187 px per frame (30–104 ppm, `volumetric_fog_repair_census`, unread/lost/failed 0),
so its cost is the full-screen prologue, not the repaired marches. `fog_route` net of the three added floors is
about 1.5 ms above Run 274 (possibly a different fogged sector; open). The final window and the
`gpu_sync_timing_summary` rows are again missing at shutdown (open). Consequences for the plan: TAA → merge or
cheapen the mask passes before touching the history filter; fog → step B (far bins) and step C (quarter-resolution
march, the repair fraction is tiny) are the levers.

## GPU backend A/B fixture (2026-09-24, bottle X3)

Question: does the same GPU work cost less on D3D11 (DXMT over Metal) than on D3D9 (Wine 11.15
wined3d; no `renderer` value under `HKCU\Software\Wine\Direct3D`, so its default OpenGL backend)?
Input to the revised ratification of [d3d9-to-d3d11-translation.md](../architecture/d3d9-to-d3d11-translation.md).

- Fixture `verification/probe/gpu_backend_ab_fixture.cpp` (CMake target `gpu_backend_ab_fixture`,
  MinGW i686, SSE2, four-byte incoming stack), runner `verification/probe/run_gpu_backend_ab.py`,
  host test `verification/analysis/test_gpu_backend_ab.py` (4 tests OK, measured). Command:
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_gpu_backend_ab.py`;
  exit 0, 25/25 checks, 18.3 s, exe `218ff4486374…` (measured). Bottle X3, WineArch arm64,
  `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`; modules d3d9 Wine 11.15 builtin (wined3d),
  d3d11/dxgi DXMT (winemetal), d3dcompiler_47 Wine 11.15 (vkd3d). Summary
  `verification/results/bottle-X3/gpu-backend-ab/summary.json`; tables printed by
  `python3 verification/results/bottle-X3/gpu-backend-ab/table.py`; raw `fixture.txt` stays local.
- Workloads, same vertex data and HLSL on both APIs (vs_3_0/ps_3_0 and vs_4_0/ps_4_0): a full-screen
  pass (4 bilinear taps of an FP16 source, 3x3 ALU loop) into A16B16G16R16F; a 460-draw scene
  (1,012 triangles per draw from 16 VB+IB INDEX16 meshes, D3D9 MANAGED / D3D11 IMMUTABLE, 4 rotated
  A8R8G8B8 mip-mapped textures, X8R8G8B8 + D24S8, depth on, alpha test one draw in five, per draw
  32 SetRenderState + 27 SetSamplerState with 1.30 value changes, 4.5 SetTexture, VS/PS, 47 + 36
  float4 constants; D3D11 side as a translator issues it: shadow compare, pre-created state objects
  on change, one cbuffer `Map` WRITE_DISCARD per draw, a `clip()` ps variant for alpha test); the
  same scene at 2 triangles per draw (`scene_460_cpu`, submission only).
- Pixel work (measured): the pass covers 100.00 % of the target (cleared to zero first) with channel
  means 0.69/0.61/0.72 at both sizes, identical on both APIs. The scene covers 97.4 % (1920x1080)
  and 97.3 % (5120x1440); the 460 quads rasterise 4.52 screens (from the geometry), and 2.11 / 2.09
  screens of samples pass depth and alpha test (occlusion query, same on both APIs within 0.02 %).
  The runner now refuses a readback below absolute floors (pass coverage 0.999, scene 0.9) as well
  as a disagreement above 5 % between the APIs.
- Timing: per iteration, event bracket (CPU time, work, event query, spin; D3D11 `End` + `Flush`)
  with a D3D11 timestamp-disjoint pair around the same work, 30 warm-up + 300 measured, medians;
  then **pipelined**: the same work 300 times back to back with a Present-like flush per iteration
  and one drain, total / 300 (throughput with no idle gaps). D3D9 timestamps are refused
  (`CreateQuery` TIMESTAMP / TIMESTAMPDISJOINT / TIMESTAMPFREQ = `D3DERR_NOTAVAILABLE`), so D3D9
  has no GPU-only figure. Empty bracket 14.8 us D3D9, 19.6 us D3D11.

Medians in us, all measured (p90 in `summary.json`):

| workload | size | D3D9 event | D3D11 event | D3D11 timestamp | event ratio D3D9/D3D11 | D3D9 submit | D3D11 submit | submit ratio | pipelined D3D9 / D3D11 | pipelined ratio |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full-screen pass | 1920x1080 | 526.4 | 419.7 | 356.0 | 1.25 | 1.6 | 2.3 | 0.70 | 128.8 / 127.0 | 1.01 |
| full-screen pass | 5120x1440 | 655.0 | 585.6 | 521.9 | 1.12 | 1.8 | 2.4 | 0.75 | 465.8 / 416.3 | 1.12 |
| scene, 460 draws | 1920x1080 | 3,691.9 | 1,276.6 | 1,215.4 | 2.89 | 871.5 | 154.6 | 5.64 | 2,204.8 / 358.6 | 6.15 |
| scene, 460 draws | 5120x1440 | 4,708.4 | 1,145.4 | 1,083.0 | 4.11 | 866.4 | 162.0 | 5.35 | 2,213.1 / 377.6 | 5.86 |
| scene, 2 triangles per draw | 1920x1080 | 2,673.0 | 711.2 | 655.0 | 3.76 | 873.7 | 161.9 | 5.40 | 2,127.2 / 334.8 | 6.35 |

Reading:
- Fill-bound GPU work costs about the same on both backends: the pipelined full-screen pass differs
  by 1.4 % at 1920x1080 and 12 % at 5120x1440 (the previous run: 1.5 % and 5 %) (measured). The
  single-pass event brackets (1.25x, 1.12x) carry a fixed per-bracket cost the pipelined run does not
  have (the D3D11 timestamp span of one pass is 356 us against 127 us of throughput; inferred: GPU
  wake-up after each drain).
- The scene is not GPU-bound on either backend. D3D9 pipelined is 2.13-2.21 ms per frame at both
  sizes and with 2 triangles per draw, so the wined3d/OpenGL submission path (about 4.8 us per draw
  end to end) is the limit; D3D11 is 0.33-0.38 ms (measured). The submitting thread spends 0.87 ms on
  D3D9 and 0.15-0.16 ms on the translator-shaped D3D11 sequence (measured); the rest of the D3D9 frame
  runs on wined3d's CS thread, which the fixture cannot time in-process (Wine's `GetProcessTimes`
  equals `GetThreadTimes` and ticks at 10 ms; raw values only in `fixture.txt`).
- The D3D11 event and timestamp brackets agree within 10 % for the scene (5.0-8.6 %) but not for
  the 1080p pass (18 %; 12 % at 5120x1440; 12 % and 8 % net of the empty bracket) (measured). Both
  include DXMT's encode-to-GPU latency: the scene timestamp span (1,215 us) is 3.4x the pipelined
  frame (359 us), so DXMT timestamps are latency spans, not GPU busy time (inferred).
- Run-to-run spread over three runs of the same workloads (measured): D3D11 scene event at
  1920x1080 1,282 / 977 / 1,277 us, so the 1080p event ratio ranges 2.9-3.8x; at 5120x1440 it
  ranges 2.5-4.1x; the pipelined scene ratio 5.6-6.2x and the submit ratio 5.2-5.6x are stable.
- Not measured: the game's own frame (this replays the census pattern, not the engine), a real
  translator's per-call cost beyond the shadow compare and `Map`, native Windows.

## 2026-09-24: `taa_mask_*` draws (taa-high-resolution.md step 0; uncommitted worktree, not installed)

Three passes appended after `fog_repair`, indices 0-21 unchanged: `taa_mask_tests` 22, `taa_mask_x` 23, `taa_mask_y` 24.
Each is one `Span` around one draw of the mask loop in `TemporalPass::run`, nested inside `taa_mask`, and always closes.
The far-only configuration draws once, reported as `taa_mask_tests`. That makes 25 passes and 50 event queries. Each
pair adds about 0.26 ms to `taa_mask`, `taa` and the serialised frame (inferred from the light-pair floor), so on the
flown thin-region configuration `taa_mask` reads about 0.8 ms higher than in Run 280 for the same work. Null marks
when the option is off: one branch per draw, no device call.

- Host (measured): `test_gpu_sync_timing` passes 8 tests. The core fixture's check count is unchanged at 35: its
  names check now requires 25 passes / 50 boundaries and indices 21 / 22 / 24. The wiring test finds one Span per mask
  draw in `temporal_pass.cpp`.
- Wine fixture, bottle X3, scratch CMake build of this tree (exe `967b5568…`), measured:
  - PASS 32/32 (count unchanged; the nesting check now also requires `taa_mask` >= each `taa_mask_*` median).
  - 50 queries plus the census hold 51 device references, and Reset recreated all 51.
  - 2,496 syncs = 48 x (50 + 2), with 0 failures, timeouts or dropped frames.
  - Window 3 medians: `taa_mask` 854 us, nesting `taa_mask_tests` / `_x` / `_y` at 209 / 195 / 209 us (one 16x16 quad
    each); `taa` 2,111 us.
  - The runner's rewritten `gpu-sync-timing.{json,txt}` were restored.

