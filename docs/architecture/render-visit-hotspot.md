# Render visit hotspot: which sub-step of the per-part visit dominates on DXVK, and what can be detoured

Design note, 2026-10-09, for ratification. Question (user go 2026-10-09): the engine's per-object render
visit costs 20-38 us per part under CrossOver/FEX; forwarded draws are nearly free on DXVK (Run 138 A) and
skipping whole hidden parts in the engine gained little (Run 139 A). Which sub-step inside the visit
dominates, and can that one step be detoured with a cached or cheaper replacement, patched at function level?
A rewrite is out. Inputs: [engine-frame-time.md](engine-frame-time.md) (section 1, 2.2, 2.6-2.12, Run 46 D,
Run52), [sampling-profiler.md](../verification/sampling-profiler.md) (profiler limits, run 31/36/37/38, the
BeginPass and technique fixtures, submit phases), [view-submit-hot-path.md](../reverse-engineering/view-submit-hot-path.md)
(R1-R8, Run 47 B, Run 48 C), [frame-loop-phases.md](../reverse-engineering/frame-loop-phases.md),
[effect-pass-loop.md](../reverse-engineering/effect-pass-loop.md), [constant-uploads.md](../reverse-engineering/constant-uploads.md),
[camera-and-lights.md](../reverse-engineering/camera-and-lights.md), [lod-selection.md](../reverse-engineering/lod-selection.md),
[engine-side-occlusion-cull.md](../reverse-engineering/engine-side-occlusion-cull.md),
[engine-state-filter.md](engine-state-filter.md), [state-call-fast-path.md](state-call-fast-path.md),
[effect-pass-replay.md](effect-pass-replay.md), the Run 138/139 triage outputs
(`verification/results/run138-dxvk-triage/cost_model_out.txt`, `run139-triage/`). Nothing was run under Wine;
no source was edited. Marks: **M** measured in a named run or fixture, **I** inferred by arithmetic from
measured parts, **A** assumed.

**Answer.** The engine's own sub-steps of the visit are each already measured and small (Run 48 C, wined3d,
510 draws: world matrix 19 us/frame, view inverse 71 us, technique+End 129 us, queue sort 5 us, cache walk 0;
all M), so there is no engine traversal step worth a detour. The visit's cost is the material pass inside
`0x004c0150`: D3DX's `BeginPass` re-applies all 57 pass states on every draw (M, fixture), and 94-99 % of the
resulting render/sampler-state calls are redundant at the device (M, run91/run240). That is the only
repeated, exactly cacheable work on the path (same material, same 53 of 57 states across parts and frames).
On wined3d the detour was closed because a redundant native call costs 10 ns (M); **on DXVK the per-call cost
has never been measured, and the DXVK per-draw slope (23.6-32 us, M) exceeds the wined3d upstream anatomy
(14.6 us, M) by 8-16 us with the draw interval bounded at <= 3 us (I, Run 138 A), which points at the state
stream**. Recommendation: do not build yet; run the two existing no-game fixtures on the DXVK backend
(section 3a) and, if a redundant `SetSamplerState` costs >= 60 ns there, one `--perf --draw-trace` flight
(3b); then build the state-manager slot filter of section 4 (Option A of engine-state-filter.md, revived for
DXVK, sampler states first). Everything else on the visit stays closed (section 5).

## 1. Ranked breakdown of the per-part visit

Per draw (one sub-mesh, one pass; the busy views draw one pass per sub-mesh and 1-3 sub-meshes per part).
The wined3d column is the only split ever measured; the DXVK column is what today's evidence allows.

| rank | sub-step | function / site | wined3d, us per draw | DXVK today, us per draw | mark | evidence |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `BeginPass`: D3DX walks the pass, evaluates dirty parameters/preshaders, issues 57 state-manager callbacks (19 RS, 28 SS, 4 textures, VS, PS, 4 constant uploads) through the game's thunks to the device | `0x004c3ffe` (effect slot 64); manager vtable `0x00562a8c` slots 7-10, 13, 14 | 6.57 (run113, 827 draws; run105 6.58); of it D3DX's own walk 1.5 + 1.8 dirty evaluation (fixture), the rest the 57 forwarded calls | unknown; **8-16 above wined3d by subtraction** (slope 23.6-32 minus wined3d's non-draw 14.6 minus DXVK draw <= 3) | M wined3d, I DXVK | sampling-profiler.md run 38 B, BeginPass fixture; `cost_model_out.txt` |
| 2 | `prepare`: pass-loop tail, `End`, SEH unlink, the caller's remainder, the next node's traversal step (world matrix `0x004bdee0`, texture step `0x004f66e0`, light selection `0x0047d5e0` when gated), `0x004c0150` prologue, three engine-wrapper binds (declaration/stream/indices through the proxy's hooks, ~0.13 us each), `GetTechniqueByName`+`SetTechnique` (0.0085) | last `pass_end` -> `0x004c1eab` | 6.36 at 827 draws, 30 at 60 draws: about 1.8 ms per frame fixed plus ~4 per draw | same order (engine code and proxy hooks are backend-independent); the DXVK window-fit intercepts are -0.8..+0.9 ms, so the fixed part is <= 1 ms there | M wined3d, I DXVK | run113; Run 48 C per-candidate spans; state-hook benchmark rows |
| 3 | draw interval: route apply/undo, cascades, retention, lease, cutouts, engine light, then native `DrawIndexedPrimitive` | `0x004c403c` and the proxy's draw hook | 8.71 (native 2.5-2.56, proxy 5.4-7.6) | **<= 3** (I): Run 138 A skipped 110 of 236 draws before the route (`evaluate_draw`) and `view_submit` moved 6.63 -> 6.54 ms, inside the window noise of ~0.3 ms | M wined3d, I DXVK | run113, run89/91; status.md Run 138 A |
| 4 | `setup`: `Begin`, ~75 parameter setters (5 `SetMatrix`, 10 `SetVector`, 15 `SetInt`, 10 `SetFloat`, 10 `SetBool`, 22 cached texture sets), two engine RS writes, geometry guard; the point-light admission loop (8 slots, 2 full tests) and the two integer normalisations are inside it | `0x004c1eab` -> `0x004c3ff0` | 1.63 | same (D3DX and engine code) | M wined3d, I DXVK | run113; camera-and-lights.md cost model (<= 0.3 ms/frame for the light loop) |
| 5 | per-view and per-frame fixed work charged to the visit: cull/LOD pass `0x0047cfe0`, traversal `0x0047d9c0`, queue sort, view setup | once per view | inside the 1.8 ms fixed `prepare` plus `view_setup` 0.9-1.7 ms per frame | `view_setup` 0.81-0.95 ms per frame (run20/21) | M | run113, run139 triage |
| 6 | light selection `0x0047d5e0` (R7: light-chain walk, x87 dot per light, `_qsort`) | per node when `[node+0xa0] >= 2` and `0x00488170(node,0) >= 2` | **never measured**; `--light-phases` stamps exist and were never flown | unknown | - | view-submit-hot-path.md 5.6, R7 qualification |
| 7 | `EndPass` | `0x004c4047` | 0.11 | same | M | run113 |

What the engine's arithmetic itself costs: 2,500-3,500 x86 instructions per draw across `0x004bdee0`,
`0x004c4fc0`, the traversal step and the non-D3DX parts of `0x004c0150`, 0.6-1.3 us per draw at the measured
FEX rate (I, view-submit-hot-path.md section 3); an SSE2 rewrite of any one routine is bounded by that.

Why the DXVK column is inferred: no DXVK session carries `--pass-phases`, `--residual-phases` or state stamps
(`grep -l pass_phases verification/results/run13*-dxvk-triage` is empty). The one hard DXVK fact is the
Run 138 A parity, which places almost the whole 24-32 us slope upstream of the draw hook's skip point
(`motion_output.cpp:5986`, `evaluate_draw` runs before the route apply), i.e. in ranks 1, 2 and 4. Of those,
only rank 1 contains backend-owned work (the 57 device calls), so the 8-16 us excess over wined3d is most
plausibly DXVK's per-state-call cost under FEX (A: DXVK's d3d9 takes its device lock and emits a CS command
per changed state; a same-value call still pays the lock and the compare). The wined3d benchmark that closed
elision (redundant `SetRenderState` 10.1 ns, `SetSamplerState` 10.9 ns, M) was never repeated on DXVK.

## 2. The detour candidate

**The redundant device calls of the pass's state block, cut at the game's `ID3DXEffectStateManager`.**

- Repeated work with a cacheable result: for a given material, 53 of the 57 callbacks per `BeginPass` carry
  the same values on every part and every frame (19 render states, 28 sampler states, 4 textures, VS, PS);
  only the four constant uploads change. Across different materials the values are mostly shared as well:
  the device-level redundancy measured with the proxy's shadow is 94.3-94.9 % of `SetRenderState` and
  98.8-99.3 % of `SetSamplerState` (M, run91 at 1,006 draws, run240 at 448 draws), i.e. ~47 redundant calls
  per draw (18.8 RS x 0.94 + 32.9 SS x 0.99, run240 mix).
- Function and call site, from the RE notes: D3DX calls the manager through vtable `0x00562a8c`
  (`CBaseStateManager`, 21 slots in `.rdata`): slot 7 `0x004b49f0` SetRenderState, slot 9 `0x004b4a30`
  SetTextureStageState, slot 10 `0x004b4a10` SetSamplerState, slot 8 `0x004b4a50` SetTexture; each a 21-byte
  stdcall thunk that loads `[this+4]` and tail-jumps into the device vtable (frame-loop-phases.md section 2,
  engine-state-filter.md Option A). The base class is the live one whenever the proxy strips `PUREDEVICE`
  (`device_creation_policy effective=0x42`, M run87; the game's factory `0x004b5550` picks the class from
  `GetCreationParameters`), which is why the game's own memoising `CPureDeviceStateManager` (RS tree,
  constant shadow) is not in play and every callback reaches the device.
- Bound: parts x calls x (c_DXVK - c_filter). With c_filter ~5 ns (A: bounds check, one compare, `ret`) and
  47 redundant calls per draw at the 220-260 app-draw views of run20/run21:

| c_DXVK per redundant call | per draw | per frame at 250 draws | verdict |
| --- | --- | --- | --- |
| 10 ns (the wined3d value) | 0.24 us | 0.06 ms | closed, as on wined3d |
| 60 ns | 2.6 us | 0.65 ms | marginal; build only with the flight's `apply` confirming |
| 100 ns | 4.5 us | 1.1 ms | build |
| 300 ns (the in-game hooked figure of run240) | 13.9 us | 3.5 ms | build; this would explain most of the DXVK excess |

Sampler states alone (32.6 redundant per draw) carry 70 % of the bound and have the cleaner invalidation set
(section 4), so they are phase 1.

Not the candidate, and why (numbers in section 5): the engine traversal steps (R1-R8, all <= 0.13 ms per
frame, M), the D3DX parameter setters (a same-value `Set*` already costs nothing in native D3DX, M), the
technique lookup (0.007 ms, M), the per-draw light admission (<= 0.3 ms, I), and full pass replay (saves only
D3DX's own walk, <= 3.3 us per draw, for a motion-output-sized component; closed 2026-09-17).

## 3. The smallest measurement that firms the ranking

In order of cost; (a) needs no flight and decides whether (b) is worth the user's time.

**(a) Two existing fixtures on the DXVK backend, no game, no build.** Bottle X3's `CX_GRAPHICS_BACKEND` is
`dxvk` by the user's hand, and neither runner overrides it (`grep CX_ verification/probe/run_state_hook_benchmark.py
run_effect_beginpass.py` is empty), so they run on DXVK today; each record must state the backend (add the
backend line the pipeline-cost fixture already records) and the fixture must be re-run on wined3d once
(`--wine-env` exists only on `run_motion_output.py`/`run_d3d9_backend_smoke.py`; a backend switch on these two
runners is a small tooling change, or the run is bracketed by the bottle setting).

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_state_hook_benchmark.py   # against the Run139 DLL, no rebuild
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_effect_beginpass.py --dll d3dx9_37=n
```

Rows to read: `SetRenderState_same`, `SetSamplerState_same`, `SetSamplerState rotating`, `SetTexture_same`,
`SetVertexShaderConstantF`, `SetStreamSource + DrawIndexedPrimitive`, the no-op vtable call (native and
production columns); BeginPass regimes (i)/(ii)/(iii) with the 57-callback count. Decision rule:
`SetSamplerState_same` native >= 60 ns on DXVK, or BeginPass regime (ii) >= 2x its wined3d 3.30 us, confirms
the state stream as the DXVK hotspot and opens section 4; <= 30 ns and (ii) within 20 % of 3.30 us closes
it the way wined3d was closed, and the DXVK excess then sits in rank 2 or 4 (engine/proxy code), which only
(b) can split.

**(b) One flight, DXVK, the Run 138/139 close-capital orbit, 60 s hold, no F8:**

```sh
python3 tools/manage.py launch --perf --draw-trace
```

`--perf` carries `X3M_FRAME_TIMING` (state hooks on, count-only state bucket) and `X3M_FRAME_PHASES`;
`--draw-trace` adds `X3M_PASS_PHASES`, `X3M_RESIDUAL_PHASES`, `X3M_LIGHT_PHASES`, `X3M_LOOP_PHASES`,
`X3M_GAME_PHASES` and the per-draw route fields. The state-stamp interval (`X3M_FRAME_TIMING_STATE_STAMPS`) is
fixture-only since 2026-09-26 and the launcher drops an inherited value, so there is no `state_p50_us`;
`apply_p50_us` brackets `BeginPass` with all 57 calls inside it, which is the number wanted. Rows, at the
300-frame windows with 220-260 app draws (compare pairs with each other, not frame time against run20/21):
`pass_phases passes_p50 apply_p50_us draw_p50_us end_p50_us`; `residual_phases prepare_p50_us setup_p50_us`;
`frame_timing state_calls_p50 state_top state_redundant redundant_top draw_p50_us draw_native_p50_us`;
`frame_phases view_submit_p50_us views_p50_us view_setup_p50_us`; `light_phases` (R7 calls and time by
caller, first ever); `motion_output_frame gate_us route_draw_us`. Self-cost inside `view_submit`: pass and
residual stamps 5 x ~250 x ~95 ns = 0.12 ms; the state hooks +59 ns x ~15,000 calls = 0.9 ms (inside `apply`;
subtract `state_calls_p50 x 59 ns` from it). Expected if (a) confirms: `apply` 12-20 us per pass on DXVK
against 6.6 on wined3d; `draw` <= 3 us; `prepare` 4-7 us. Do not add `--profile`: the sampler attributes
nothing under FEX (0 of 1,497,596 leaf samples in `X3AP.exe`, run167; run84 the same).

## 4. Detour design sketch: state-manager slot filter (Option A revived for DXVK)

- **Hook site.** Not a code patch: three byte-verified dword stores into the live manager vtable
  `0x00562a8c` (`.rdata`), slot 10 first (`SetSamplerState`, expected `0x004b4a10`), then slot 7
  (`SetRenderState`, `0x004b49f0`) and slot 9 (`SetTextureStageState`, `0x004b4a30`, uncounted today) as
  phases 2 and 3. `VirtualProtect` the page, write under the install window, restore the original dwords
  on rollback. Install only when `GetCreationParameters` shows `PUREDEVICE` clear (otherwise the pure class
  `0x00562afc` is live, which already memoises RS; refuse), when the three slots hold exactly the thunk
  addresses and the thunk bodies match their 21 bytes, and after the effect loader's single
  `SetStateManager` call site `0x004bb07e` has run (the manager object exists at `renderer+0x1c`,
  `*0x00608b3c`; it is rebuilt on device recreation, `0x004dbe30`, so re-verify after Reset). Slot 8
  `SetTexture` is excluded: the proxy's hooked `SetTexture` owns texture identity, mip bias and composition,
  and the game's cached texture setter `0x004b9ed0` (22 calls per draw) bypasses the manager anyway.
- **Replacement.** A proxy `__stdcall` function per slot with the thunk's signature (`this, [stage,] state,
  value`, `ret 0xc`/`0x10`): bounds-check the index (sampler < 16, type < 14; RS < 256), compare with the
  shadow entry; equal and known -> `return D3D_OK`; otherwise store and forward through `[this+4]`'s device
  vtable exactly as the thunk did. No x87, no allocation, no logging, `LastError` untouched (the device call
  may set it; the skip path leaves the previous value, which native D3DX ignores). Compiled with the project's
  `-msse2 -mstackrealign` flags; `check_no_x87.py` covers it.
- **Cache key.** Device state, not material state: `(sampler, type)` -> last forwarded value, `(RS)` ->
  value, each with a known bit. The material repetition is what makes it hit; no per-material table, no
  effect handle, no node identity.
- **Invalidation (all entries unknown).** Every path that changes device RS/SS without passing the filter:
  the proxy's own native writes (route apply/undo restore exact values, so the shadow stays valid after a
  successful undo; `invalidate_render_states` on a failed restore invalidates the filter too; lazy-RT flush,
  sun lane, shadow replay, compositor, TAA, fog and engine-light passes all run under the proxy's central
  native-write seams, which call one `filter_invalidate()`); `Clear` and `SetRenderTarget` (per view);
  `BeginScene`/`EndScene`/`Present`; state-block `Apply`/`EndStateBlock`; `before_reset`/`after_reset`;
  device loss. The engine's direct device `SetRenderState` sites (17, all in the overlay routines
  `0x004c53d0`/`0x004c55d0`/`0x004c5830`) are not observed by the proxy in production (hybrid unhook); they
  are separated from the material draws of the next view by that view's `Clear`, which is the invalidation
  point, but this ordering is **not proven** (the cockpit view's material draws follow the 2D overlay pass in
  the frame routine). Phase 1 (sampler states) avoids the question if the image has no direct
  `SetSamplerState` site outside the two manager classes: one `X3ConstantUploads.java disp:0x114` sweep
  settles it. The filter forwards every write while unknown, so an over-eager invalidation costs calls, never
  correctness.
- **Native parity.** A hit means the device already holds the value; the forwarded stream differs only in
  omitted no-ops, so rendering is identical by construction. On native Windows the proxy's device is non-pure
  and the runtime already filters redundant states: zero behaviour change there, only the call count.
  Documented D3D9 and Win32 only (`VirtualProtect`, the manager's COM ABI); nothing Wine-specific.
- **Cost on the hot path.** Per callback the filter replaces the 6-instruction thunk plus the device call with
  a bounds check, one load, one compare and `ret` on a hit (~5 ns, A); on a miss it adds those ~5 ns to the
  existing path. Nothing is added to the proxy's own hooks, which stay off for RS/SS in production.
- **Rollback.** Restore the three dwords (expected values re-checked before the write), `VirtualProtect`
  back; an `x3m.ini` key (`state_filter = off|samplers|all`, default off until flown) and one
  `state_filter` install row with reason codes like the other engine patches.
- **Risks under emulation.** None specific to FEX: the write is data, not code (no JIT cache concern); the
  filter is ordinary x86 proxy code called from D3DX's x86. The project-level risks are the invalidation set
  (above), the pure-manager case (refused), and the proxy's `X3M_STATE_SHADOW=1` diagnostic path, which must
  see the same values the filter forwards (it hooks below the filter, so it sees fewer calls but the same
  device state).
- **What proves it.** A fixture: a fake manager object with the game's layout, the three slot stores, refusal
  on byte mismatch and on `PUREDEVICE`, rollback, the hit/miss/forward counts, forwarding while unknown and
  after each invalidation reason; the state-hook benchmark gains a "filtered manager" row (per-call cost on a
  hit and on a miss, on both backends). In flight under `--perf`: `state_calls_p50` falls from ~59 to ~12
  per draw and `state_redundant` to ~0 with `draws_p50`, `draw_pairs` and `cutout_pairs` unchanged; the
  saving is read as delta `apply_p50_us` x `passes_p50` in a same-build A/B at matched app-draw windows
  (the Run 138 A shape), expected 0.6-3.5 ms per frame per the table in section 2; felt FPS in a session
  without `--perf`.

## 5. What stays closed, with numbers

| item | bound | why | mark |
| --- | --- | --- | --- |
| Engine traversal detours R1-R8 (view inverse, `fptan`, technique skip, cache walk, sort, world matrix, normalisations) | each <= 0.13 ms per frame at 510 draws: R1 71 us, R8 19 us, R3 65+64 us, R5 5 us, R4 0 | Run 48 C stamps (`--submit-phases`, run181) | M |
| D3DX replacement / pass replay / setter-dedup wrapper | saves D3DX's own walk 1.5 us + 1.8 us dirty evaluation at most, <= 3.3 us per draw = 0.8 ms at 250 draws; a same-value `Set*` costs 0.1 us and no callbacks; replay must still issue the 57 calls | BeginPass fixture; effect-pass-replay.md decision 2026-09-17 | M |
| Technique-handle cache | 0.007 ms per frame | technique microbenchmark | M |
| Per-draw point-light admission loop, SEH frame, parameter setup | <= 0.3, <= 0.1, 0 ms | camera-and-lights.md cost model; frame-loop-phases.md | I |
| Redundant-state elision in the proxy's device hooks | net loss ~1.1 ms on wined3d (hook 59 ns x 23,160 calls to save 10 ns x 22,500) | state-call-fast-path.md "Elision revisited" | M |
| Draw-call culling (proxy skip, engine skip) | ~0 at 110 skipped draws (Run 138 A); engine skip 18 parts/frame, net loss below ~200 scene draws (Run 139 A) | occlusion-cull ledger, run139 triage | M |
| Threading the submission | no patch-sized version: device without `MULTITHREADED` (flags 0x52), one thread for simulation, traversal and every D3D call, D3DX and the state manager not thread-safe; the GPU thread already exists (CSMT / DXVK CS) | engine-frame-time.md 2.10 | M structure |
| SSE2 trampolines for x87 routines | all engine arithmetic in `view_submit` is 0.3-0.65 ms per frame at 510 draws | view-submit-hot-path.md section 3 (collide SSE2 parity 31.0 vs 31.4 ns) | I |
| Instancing / material sort | <= 1.1 ms; 5 % candidates, 7 texture stages per draw, order break | run91, merged-lod-feasibility.md | M |
| LOD bias | one full LOD step buys ~7 % of draws | Run 42 D | M |
| Sampling profiler for attribution | blind under FEX (0 x3ap leaf samples in run84 and run167) | sampling-profiler.md | M |

## 6. Alternatives considered and why they lose

- **Cache the light block per ship (admitted point lights for all parts of one node tree).** The admission
  predicate is per part (`trunc(|node - light|) <= light[+0x158] + node[+0x70]`, camera-and-lights.md), so a
  per-ship answer changes lighting at the admission edge; and the whole loop is <= 0.3 ms per frame (I).
- **Cache the world matrix / view-space constants across frames.** R8 is 19 us per frame and R1 71 us (M);
  nothing to win.
- **Replace `BeginPass` with a recorded state list (pass replay) keyed by material.** Exact only with an
  FXLC/preshader evaluator; even then it must issue the 57 calls (D3DX re-applies regardless) and saves
  <= 3.3 us per draw for a motion-output-sized component; closed 2026-09-17 and not reopened by anything in
  the DXVK evidence. The slot filter takes the same redundant calls out from below for a few hundred lines.
- **Re-hook `SetRenderState`/`SetSamplerState` in the proxy and elide there.** Pays 59 ns per call on all
  ~50 calls per draw (hybrid-unhook measurement) to save the native cost on the redundant ones: a net loss
  at c_DXVK <= 70 ns and inferior to the manager slot at any c, since the slot sits upstream of the
  device dispatch.
- **Keep `PUREDEVICE` so the game's own memoising manager is live.** It memoises RS, shaders and constants
  but not sampler states (the largest class), and the proxy strips the flag because the hybrid unhook reads
  device state with `Get*`, which a pure device refuses; reversing that re-opens the state-shadow hot path
  (state-call-fast-path.md step 5).
- **Engine-side skip of whole parts (Run 139)** and **draw-level cull (Run 138)**: measured ~0 to net loss;
  closed.
- **One more `--profile` flight.** Blind under FEX twice; a third run cannot attribute anything.

## Unknowns and what settles them

- DXVK's per-call cost of a redundant and a changing `SetRenderState`/`SetSamplerState`/`SetTexture` under
  FEX, and the in-fixture `BeginPass` on DXVK: section 3a (no game).
- The in-game split of the DXVK slope into `apply`/`prepare`/`setup`/`draw`: section 3b (one flight).
- R7 light selection frequency and time: `light_phases` in the same flight (never flown).
- Whether any image site outside the two manager classes calls the device's `SetSamplerState` (`+0x114`) or
  `SetTextureStageState` (`+0x10c`): one `X3ConstantUploads.java` sweep; decides phase 1's invalidation set.
- Whether the 17 direct `SetRenderState` sites ever run between a view's `Clear` and that view's last
  material draw: static reading of the frame routine's overlay and cockpit ordering, or a full-state capture
  (the d3d9-to-d3d11 census item); decides phase 2.
- `D3DXFX_DONOTSAVESTATE` is passed (`0x004c1ebe`), so D3DX records no state blocks; whether D3DX ever reads
  back device state through the manager (it has no `Get*` slots) is settled by the vtable: none.
- Native Windows: source-compatible by construction (documented COM ABI, `VirtualProtect`); not verifiable
  here; entry for [platform-portability.md](platform-portability.md) when built.

## Ledger

- 2026-10-09 01:44, fixture (a) on bottle X3 / DXVK (`run_state_hook_benchmark.py`, defaults; record `verification/results/bottle-X3/state-hook-benchmark.json`, since overwritten by the 02:40 run below; measured): the `native` case (direct device calls, no proxy) gives `SetSamplerState_same` 35.9 ns, `SetRenderState_same` 36.7, `SetTexture_same` 35.5, `SetTextureStageState_same` 34.5, `SetRenderState` 55.0, `SetStreamSource` 99.2; the `proxy-timing-off` case crashed (exit 5, execute-access page fault at 0x74666f73 after DXVK QueryInterface warnings; the pre-existing DXVK fixture teardown crash class, not diagnosed), so the proxy-path figure and `run_effect_beginpass.py` were not measured. Reading: a redundant call costs 36 ns inside DXVK plus the proxy's forwarding; at 250 draws x 47 redundant calls that is 0.4 ms at 36 ns and under 1 ms even at 80 ns (inferred), below the 60 ns rule on the measured part. Decision pending with the user; recommendation: closed, the bound is under 1 ms for a vtable hook into the game's state manager with shadow-state invalidation risk.

## Part 2: pre-submit work

2026-10-09, same marks. Question (user): in run20 (DXVK, in flight) `dt` p50 13.7 ms splits into `pre_render`
5.3 ms (max 6.8), `views` 7.4 (`view_setup` 0.9, `view_submit` 4.8) and `scene_end` 0.2 (M, `frame_phases`
windows of `/tmp/x3-bottleX3-run20`), so 40 % of the frame precedes the render routine. Can that part be made
cheaper, given it grows with the nodes in the sector? Inputs: [main-loop-input-region.md](../reverse-engineering/main-loop-input-region.md)
(the `input_part=0` region, run380/run381), [script-task-scheduler.md](../reverse-engineering/script-task-scheduler.md)
(the animation tick `0x0048f550`, the scheduler `0x0049f770`, the probe set), [object-lifetimes.md](../reverse-engineering/object-lifetimes.md)
Run383 and the [dust-leak report](../../verification/results/run383-dust-leak/report-for-mayhem-author.md),
[dust-leak-fix.md](../verification/dust-leak-fix.md) Run 109 A (run384), the run94/run96/run129 game- and
loop-phase ledgers in sampling-profiler.md, `src/proxy/loop_phase_sites.h`, `game_phase_sites.h`,
`frame_phases_core.h`, logging-tiers.md, `tools/config/schema.py`. The brief's "Run 107/108/109 sections of
engine-frame-time.md" do not exist under those names; the flights are run380/run381 (`main-loop-input-region.md`
section 6) and run384 (`dust-leak-fix.md`), used here.

**Answer.** `pre_render` is the main loop from the Present return to the render prologue: clock, message pump,
audio channels, the script scheduler (`PendingVm`), media services, the Input phase (whose first 49 bytes are
the `input_part=0` region: animation tick, per-sector driver, deferred-delete sweep, then the script/VM
sub-path `input_part=1`), simulation `0x00416750`, cockpit update `0x0041cde0`, and the proxy's post-Present
work. Only the `input_part=0` region has ever been split in flight (10 `--loop-phases` stamps): it is
~0.9 ms of the 5.3 ms, flat and small once the dust leak is fixed. The other ~4.4 ms (I) has no per-site
measurement in any normal-speed frame: the game-phase tape only records frames at or above its threshold
(20 ms under `--draw-trace`), and every normal-frame figure below comes from slow-frame tapes of other runs. The
animation tick, the user's candidate, costs 0.14-0.25 ms at 4.5-5.5k nodes (M run384) and run20 holds
2.8-3.5k nodes (M), so an inert-node skip is bounded at ~0.1-0.25 ms and is not worth its exactness proof.
**No pre-submit detour is justified today; the Run 140 A perf flight should carry the two existing stamp
groups with a lowered tape threshold (section 2.2) so the 4.4 ms gets an owner first.**

### 2.1 Composition of `pre_render`

| segment | site(s) | per frame | scales with | mark | evidence |
| --- | --- | --- | --- | --- | --- |
| clock, pump, channels, services | `0x00403af0`-`0x00403b04` | < 0.1 ms combined | constant | M (run94 tape, slow frames only) | sampling-profiler.md run 32 C: "all others < 0.1 %" |
| script scheduler `PendingVm` (`0x0049f770`, runnable tasks only) | `0x00403aff` | 0.81 ms (run129 corvette stand); 2.76 ms p50 on run94's slow frames; 70.8 % of its lightest 50.8 ms frame | runnable script tasks and their work; run20 holds **26,246 live tasks** (M, `scene_graph_census tasks=`), runnable share unknown | M, other builds/sectors | run129 (Run 42 A), run94 |
| `input_part=0`: animation tick `0x0048f550` (`cutevent`) | `0x00403b12` | 0.14-0.25 ms at 4,515-5,477 nodes with the dust fix (run384); 0.05 -> 12.3 ms while leaking 27 nodes/frame (run381) | **live scene nodes** (walk ~34 insns/node, 30-50 ns/node from run384, 66-92 ns/key fit in run382) + animated nodes x dt + cut instances; run20: 2,785-3,544 nodes, 0 cut instances, 700-840 unattached (M) | M | dust-leak-fix.md Run 109 A; main-loop-input-region.md run381 table |
| `input_part=0`: per-sector driver `0x0043a360` (`containers`) | `0x00403b17` | 0.66-0.80 ms (Mayhem battle, 1 active sector): collide 0.52-0.61, simulate 0.02-0.04, post 0.01, passb 0.14-0.19; collide is episodic (0.3 ms to 26 ms at a collide-heavy spot, run150) | objects in the sector (collide O(N^2) pair loop + narrow phase; simulate per object; passb economy tick) | M | run380/run381 tables; run150 |
| `input_part=0`: deferred-delete sweep `0x0045b660` | `0x00403b1c` | 0.00 ms | objects flagged for deletion | M | run381 |
| `input_part=1` (script/VM sub-path of the Input phase) | `[0x00403b3a, 0x00403dc5)` | 1.35 ms p50 on run94's slow frames | scripts | M, other sector | run94 |
| simulation `0x00416750` | `0x00403f2a` | < 1 % of run94's slow frames; never measured on a normal frame | objects/AI | M (slow frames) | run94 |
| cockpit update `0x0041cde0` | `0x00403f2f` | 0.44 ms p50 (run94 slow frames) | constant-ish (HUD) | M, other build | run94 |
| proxy post-Present (`present_end()` fires before `after_present`: telemetry lines, retention census, `scene_graph_census` walk 38-59 us, HDR latch) | proxy | bounded by run89's `scene` bucket 1.25 ms, which also holds the compositor; 0.1-0.3 ms on DXVK (I) | constant; census walk capped at 1.8 ms | I | engine-frame-time.md 2.1; run20 `walk_us` |

Sum check: the stamped region is 0.8-1.0 ms (I from the rows above), so 4.3-4.5 ms of run20's 5.3 ms lies in
`PendingVm`, `input_part=1`, simulation, cockpits and the proxy's post-Present work, none of which was
measured in this run or on this backend. run381's "region tracks `pre_render` minus a constant ~5 ms" (M,
Mayhem battle) says the same: the unsplit remainder is ~3.6-5 ms there as well.

What grows with what: scene nodes drive only the animation walk (and the proxy's census); objects drive
collide/simulate/passb (0.7 ms flat in a battle sector, collide episodic); script tasks drive `PendingVm` and
`input_part=1`; the rest is constant. With the dust leak fixed nothing in the stamped region grows with
elapsed time (run384: `cutevent` 248 -> 140 us over 19,826 frames, M), but run384's `input` still rose
4.6 -> 7.2 ms over the stay with `dt` 16 -> 23 ms, "unexplained" in its ledger: that growth is in the
unstamped 4.4 ms, and it is the one open pre-submit question with a measured symptom.

### 2.2 Splitting `pre_render` in flight on DXVK with the current build

Yes, with two existing `--draw-trace` members and one ini key; no build:

```sh
python3 tools/manage.py launch --perf --draw-trace --config
```

with `x3m.ini` (section `logging`, developer keys, `tools/config/schema.py:542-547`): `loop_phases = 1`,
`game_phases = 1`, **`game_phase_threshold_ms = 12`** (DLL default 20; the launcher drops an inherited
`X3M_GAME_PHASE_THRESHOLD_MS`, so the ini is the only way). `--draw-trace` already sets `X3M_LOOP_PHASES` and
`X3M_GAME_PHASES`; the threshold is what makes the tape record ordinary 12-18 ms DXVK frames instead of only
stalls. Rows to read:

- `loop_phases frame=N`: `cutevent_p50_us`, `containers_p50_us` (with `collide/simulate/post/passb_p50_us`
  nested), `sweep_p50_us`, `region_p50_us`, `input_p50_us` (= `pre_render` when `game_phases` is off, the
  Input phase when on), `region_max_owner`, `sectors_p50`. Self-cost 0.37 us per frame plus ~0.5 us per
  active sector (M fixture).
- `game_phase_slow_frame` + up to 96 `game_phase_segment` rows per frame above the threshold: per-site
  durations for `pump`, `clock`, `channels`, `pending_vm`, `services`, `input` (`input_part` 0/1/2),
  `simulation`, `cockpits`, `render`, `post_render`; reduce by site over the window (run129 did this at the
  20 ms threshold: Input 27.4 ms, `PendingVm` 0.81 ms). Verbosity: 96 rows per qualifying frame; at 12 ms
  about half of run20's frames qualify, ~200k rows for a 60 s hold, acceptable for one flight.
- `scene_graph_census` (already in `--perf`): `engine_nodes`, `tasks`, `cuts`, `unattached` per 300 frames,
  to regress `cutevent` and `pending_vm` on counts.
- `frame_phases pre_render_p50_us` for the total, and `dust_leak_fix hits=` (the fix is on).

Caveat: the game-phase group and the loop-phase group share the lean/shared stub mechanisms and were flown
together in run96/run129 (M); the only group that conflicts with a default patch is `--submit-phases`
(fixture-only), not these.

### 2.3 Detour candidates, with bounds

| candidate | site | bound at run20's counts | verdict |
| --- | --- | --- | --- |
| Animation tick skipping inert nodes (no cut `+0x258`, no `0x1000` flag below) in `0x0048f2b0` | `0x0048f2e4` (post-order walk; every writer of `+0x258`/`+0x1f4` would need an index hook) | the whole walk is 0.1-0.25 ms at 2.8-5.5k nodes (M run384, I run20); a skip saves less than that | closed at the normal node count; only a sector with > 20k live nodes (the leak case, fixed by `dust_leak_fix`) made it visible |
| The per-millisecond `CutEvent` loop `0x0048f3c0`-`0x0048f4ac` (step to the next `i % 33 == 1`) | `0x0048f3c0` | exact; worth it only with thousands of animated nodes; run20 has 0 cut instances and the tick is 0.1-0.25 ms | closed until `node_animate` counts say otherwise |
| Interval C bucket iteration (`0x004efda0` re-lookup per key) | `0x0048f616` | O(buckets + instances); run20 `cuts=0`, `cut_buckets=8` | nothing to gain |
| Collide pair loop | `0x0045d250` | already carries `--collide-box-cull`, SAT SSE2, memo (defaults); 0.5-0.6 ms flat in a battle sector, 26 ms episodes at collide-heavy spots (M run150/run129) | no new lever; episodic cost is a separate investigation (sector-collide.md) |
| Simulation per object `0x00452ad0`, economy tick `0x004596e0`, attach `0x004526b0` | sector driver | 0.02-0.04 + 0.14-0.19 ms (M) | too small |
| Script scheduler / interpreter (`PendingVm`, `input_part=1`) | `0x0049f770`, `0x004a26a0` | unknown on this build; 26,246 live tasks in run20 (M); plausibly the largest unstamped item (A) | cannot be detoured without changing game logic; a cheaper interpreter is a rewrite. Only measurement applies |
| Proxy post-Present work | proxy | 0.1-0.3 ms (I); the census walk 38-59 us (M) | proxy-owned; trim only if the tape shows it |

Nothing in `pre_render` has a hook site, an exact cache and a bound above 0.3 ms at run20's counts. The
animation tick is cheap now because the dust fix keeps the unattached list at ~800 instead of 150,000
(run383 -> run384, M); the tick's 77 ns/node figure is the leak regime's slope, not a normal-sector cost.

### 2.4 Closed

| item | number | mark |
| --- | --- | --- |
| Animation-tick node skip | <= 0.25 ms per frame at <= 5.5k nodes | M run384 / I |
| Sector driver (collide, simulate, post, passb) as a whole | 0.66-0.80 ms flat; all five callees mutate game state and fire script callbacks, no behaviour-preserving skip | M run380/381 |
| Deferred-delete sweep | 0.00 ms | M run381 |
| `input_part=0` as the growth owner | 0.9 ms with the dust fix; the leak was the data (170 of 248 Mayhem backgrounds request dust from families with no bodies) | M run383/384 |
| Skipping/rate-limiting script tasks, animation updates or off-screen scene walks | changes game logic; out of scope | - |

Ledger: 2026-10-09, Part 2 written from run20 (`frame_phases`, `scene_graph_census`, `dust_leak_fix` rows,
queried by `grep`/`awk` only), run380/381/383/384 results and the run94/96/129 ledgers; no Wine run, no
source change; the pre-submit split (2.2) is the request for Run 140 A, and no pre-submit detour is proposed.

### Unknowns (Part 2)

- The owner of the ~4.4 ms outside the stamped region on DXVK (`PendingVm`, `input_part=1`, simulation,
  cockpits, proxy post-Present): the 2.2 flight.
- Whether run384's slow `input` growth (4.6 -> 7.2 ms over a stay) recurs with the tape at 12 ms, and which
  segment carries it.
- The runnable share of the 26,246 live script tasks and the cost per runnable task; `game_phase_segment`
  gives the time, `scene_graph_census tasks=` the live count, the runnable count needs the list-length
  counter of script-task-scheduler.md section 4 (not built).
- Whether the developer ini keys are honoured in `--config` player mode on this build (run380 ran
  `--perf --config` with `loop_phases = 1` successfully, M; `game_phase_threshold_ms` through the ini was not
  exercised in any run).
- 2026-10-09 02:40, fixture (a) again after the final-Release vtable fix (worktree agent-ac5772c217651a323, DXVK, `run_state_hook_benchmark.py --dll build/d3d9.dll`, exit 0; measured): `SetSamplerState_same` native 40.6 ns, proxy production path 39.4 ns, `SetRenderState_same` 38.2 / 40.7 ns (the shadow-timing-off case 109 ns and timing-on 192 ns are diagnostic builds). The full path through the proxy costs the same as DXVK alone, 39 ns per redundant call: 47 x 250 x 39 ns = 0.46 ms per frame at a dense close view (inferred). Below the 60 ns rule: the state-manager slot filter is CLOSED. Part 1's remaining open item is the in-flight `pass_phases apply` split, carried by the Run 140 A perf flight for the record only.
- 2026-10-09 Run 140 A flight 2 (run23, `--perf --draw-trace`, loop_phases + game_phases at 12 ms; `verification/results/run140-triage/run23_phases.py`, measured): the pre_render split is complete: segments on slow frames p50 PendingVm 1.31 ms (p95 6.7), Input 1.26 + 0.97, Clock 0.98, Cockpits 0.79, Presentation 0.25, summing to ~5.5 ms against pre_render 5.55 ms; loop_phases p50 cutevent 0.23, containers 0.95, sweep 0.04, region 1.23, input 2.27 ms. So the formerly unstamped 4.4 ms is the script scheduler (PendingVm), the clock/pump segment and cockpits: game logic, no engine detour. Per draw: pass_phases apply 3.36 ms/frame, draw 2.38, end 0.03; residual prepare 1.01, setup 0.39, other 1.69; frame_timing state_calls 11,557 per frame with ~3,860 redundant across the three counters, gap_draw 30.9 us per draw. The apply phase (D3DX state application) is 3.4 ms per frame at this view; the redundant share at 39 ns per call is 0.15 ms: the slot filter stays closed. Part 1 and Part 2 are closed with flight evidence.
