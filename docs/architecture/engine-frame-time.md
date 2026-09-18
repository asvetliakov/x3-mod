# Engine-side frame time: what a busy frame costs and which levers remain

Design note, 2026-09-18, **ratified by the orchestrator 2026-09-18** (order: telemetry session first, census and View Distance A/B offline/no-code in parallel, then choose among 2.1/2.3/2.4/2.2). Question: which engine-side changes
(patches, trampolines, culling/LOD policy, state-manager bypass, threading)
could cut the busy-view frame time under CrossOver Preview (arm64 Wine + FEX),
by how much, and what telemetry would firm each estimate. Inputs: run125
(`/tmp/x3-bottleX3-run125`, corvette) and run124 (`/tmp/x3-bottleX3-run124`,
fighter at the run117 station), both `--frame-phases` only; the run 31-38
ledgers in [sampling-profiler.md](../verification/sampling-profiler.md);
[frame-loop-phases.md](../reverse-engineering/frame-loop-phases.md),
[main-loop-input-region.md](../reverse-engineering/main-loop-input-region.md),
[engine-state-filter.md](engine-state-filter.md), [effect-pass-replay.md](effect-pass-replay.md),
[lod-selection.md](../reverse-engineering/lod-selection.md),
[camera-and-lights.md](../reverse-engineering/camera-and-lights.md),
[route-cost-run1.md](../verification/route-cost-run1.md). Nothing was run under
Wine; no source was edited. Marks: **M** measured in a named log or fixture,
**I** inferred by arithmetic from measured parts, **A** assumed.

## 1. What the telemetry says today

**Phase meaning** (`src/proxy/frame_phases_core.h`, `frame_phase_sites.h`). A
frame runs from one native `Present` return to the next. `pre_render` is
everything up to the render routine's prologue stamp `0x00471f6c`: message
pump, script VM, deferred callbacks, audio, media services, simulation
`0x00416750`, cockpit update `0x0041cde0`, the `input` region (cut events,
per-sector driver `0x0043a360`, deferred delete) **and the proxy's post-Present
work** (`present_end()` fires before `after_present`, the telemetry lines and
the retention census). `views` is the per-view loop `0x00472186`-`0x0047238d`;
`view_setup` (`0x0047224c`-`0x00472270`) and `view_submit` (`0x00472270`-
`0x004722c8`: traversal, cull, the O(n^2) queue sort, material submission
`0x004c0150` with every D3DX and device call) accumulate per view; `views -
setup - submit` holds particles, the scene-end composite (proxy scene hook, TAA,
shadow replay, sun apply), env-map and fixups. `scene_end` is the frame
`EndScene` and the gated tails up to the Present hook; `present` is the native
`Present` alone.

**Run124, fighter at the station (M).** Windows 2700-3600 are the busy plateau:

| window | draws p50 | dt p50/p95 | pre_render | views | view_setup | view_submit | other | scene_end |
|---|---|---|---|---|---|---|---|---|
| 3300 | 860 | 32.7 / 37.0 ms | 5.5 | 26.6 | 1.55 | 21.1 | 4.0 | 0.36 |
| 3600 | 930 | 32.0 / 36.8 ms | 3.2 | 28.0 | 1.46 | 22.1 | 4.5 | 0.38 |
| 5700-6600 | 369 | 15.6-16.7 ms | 2.9-3.0 | 12.1-13.0 | 0.86-0.94 | 9.3 | 2.0-2.8 | 0.18-0.27 |

`present` 6 us, `begin_scene` 0.3 ms, `prologue`+`scene_update` 0.12 ms, 9
views per frame throughout. `view_submit` per draw: 23.7 us at 930 draws,
25 us at 369 draws (M); the per-draw cost is flat, the busy frame is draw
count. The shadow lane replayed 779 caster draws in 1.42 ms plus 0.07 ms apply
at frame 3300 (M, `shadow_replay_depth`), inside `other`.

**Run125, corvette, the 24 fps area (M).** Per-50-frame medians from
`frame_end` (dt ms, draws): window 18600 = (14,162) (13,134) (15,163) (15,168)
(16,171) (19,165); window 18900 = (25,168) (23,179) (22,152) (31,297) (39,496)
(40,482); window 19200 = (43,482) (35,482) (30,489) (21,489) (22,497) (22,495).
The window lines: 18600 dt 15.8 / pre_render 7.0 / views 8.0 (submit 5.1);
18900 dt 28.0 / **pre_render 18.0** (p95 25.5) / views 9.6 (submit 5.8);
19200 dt 25.3 / pre_render 7.8 (p95 32.9) / views 15.5 (submit 12.2). So the
24 fps episode has two independent parts: (a) the draw count rising 165 -> 490
adds about 7 ms of `views` (12.2 - 5.1, at 25 us/draw, M), and (b)
`pre_render` rising from 5-7 ms to 18-20 ms **with the draw count unchanged**
(frames 18601-18750 at 152-179 draws run 22-25 ms against 13-16 ms in the
previous window at the same draws) and falling back to ~7 ms while draws stay
at 490 (frames 19051-19200). (b) is game-side and episodic; nothing in this log
names it (no `--game-phases`, no `--loop-phases`; the 50 ms segment threshold
would have covered only 8 of the 600 frames). Single-frame witnesses agree:
frame 18440 dt 43.1 ms with pre_render 34.0 and views 8.3; after the 65 ms
hitch at 18545 (views 57.9) the next two frames carry pre_render 11.5 and 10.9,
which looks like simulation catch-up after a long frame (I; `0x00452ad0` and
`0x004596e0` clamp their dt at 1000 ms per the main-loop note).

**Per-draw anatomy of `view_submit` (M unless marked), 930 draws = 22.1 ms.**
From run113 (`--pass-phases --residual-phases`, 827 draws): `prepare` 6.36 us
(engine traversal/cull/sort/world matrix/SEH entry/wrapper binds; the D3DX
technique lookup inside it is 0.0085 us), `setup` 1.63 us (`Begin` + ~75
parameter setters + 2 render states), `BeginPass` 6.57 us, draw interval 8.71
us, `EndPass` 0.11 us: 23.4 us, which reproduces run124's 23.7 us/draw. The
`prepare` bucket is 30 us/draw at 60 draws and 6.4 us at 827, i.e. about 1.8
ms per frame fixed plus ~4 us per draw (I from those two points). Inside the
8.7 us draw interval the forwarded wined3d `DrawIndexedPrimitive` is 2.56 us
(run89/run91 `draw_native`, M) and the proxy's own draw hook 5.4-5.5 us with
`--frame-timing` stamps on (`draw - draw_native`, M), so **about 3.5-5 us per
draw is proxy work, not Wine** (I; the "Wine draw 8.7 us" label in goals row
14 bundles both). Inside the 6.6 us `BeginPass`, native D3DX's own walk is
1.5 us with all 57 state callbacks issued on an idle device (fixture, M); the
other ~5 us is wined3d and the proxy's hooked `SetTexture`
(125 ns x 4.5), `SetVertexShader`/`SetPixelShader` (hooked, program-pair
lookup, cost unmeasured) and `SetVertexShaderConstantF` (75 ns) per pass (I).

**Where a 32 ms station frame goes (I, sums of the M parts above):** engine
per-object work ~5.5 ms, D3DX + wined3d state application ~6 ms, wined3d draws
~2.4 ms, proxy per-draw hooks ~3.5-4.7 ms, D3DX parameter setup ~1.5 ms,
per-view setup 1.5 ms, proxy per-frame (shadow replay 1.5, TAA ~0.2, fill 0.1)
~1.8 ms, game composite/env/fixups ~1 ms (run113 `other` 1.12), pre_render
3-5.5 ms, everything else < 1 ms. Diagnostic self-cost is inside these numbers
(`--frame-end-stride 1` writes ten log lines, 3.9 KB, per frame inside
pre_render, unmeasured).

## 2. Candidate levers

Savings are per busy frame at ~930 draws (run124) unless the lever is about the
run125 pre_render case. Effort: S (one site or option), M (a routine plus
fixture), L (a subsystem).

### 2.1 Attribute and patch `pre_render` (the run125 case) — up to 10-13 ms, unknown until measured

Mechanism: find which main-loop callee owns the 18-20 ms and either remove a
redundant per-frame scan (the media-cue negative cache at `0x00498140` was
exactly this kind of fix, 390 ms -> normal), memoise a per-object lookup, or
replace a hot x87 routine with an SSE2 trampoline. Candidates, from the
main-loop note: per-object simulation `0x00452ad0` (27 KB, 401 calls, per
object per frame), collision `0x0045d250` (its swept query `0x0045cab0` is the
one place an O(n^2) pair cost can hide), the timed tick `0x004596e0` (own
accumulator, catch-up work independent of frame rate), the script VM
(`PendingVm` was 70.8 % of run94's lightest recorded slow frame), cockpit
update `0x0041cde0`, and the proxy's own post-Present work (bounded by run89's
`scene` bucket 1.25 ms, which includes it, M). Particle update is inside
`views` (`particles` 0.017 ms/frame in run113, M), not here. Saving: the
episode adds 11-13 ms of pre_render over the 5-7 ms baseline (M); how much of
that is removable is 0-100 % until attributed (A). Risk: all five driver
callees mutate simulation state and dispatch script callbacks, so only a
specific redundant scan is a safe patch. Native parity: an EXE patch like the
media-cue cache. Effort: S-M once the owner is known. What settles it:
`--loop-phases` (per-window p50 of collide/simulate/post/passb, every frame, no
threshold) plus `--game-phases` with its segment threshold lowered from 50 ms
to 20 ms (`game_phase_mode frame_threshold_ms=50`, `game_phases.cpp:268`; small
change). A driver interval names the callee via `max_interval_owner`; the
residual (`input - sum`) or `PendingVm` sends the call tape, not the
loading-interval recorder, after the routine; then the disassemble agent on
that routine's inner loops.

### 2.2 Cut the proxy's per-draw hook work — 1-3 ms (I), proxy-owned

Not an engine lever, but the same size as the engine ones and cheaper.
`draw - draw_native` was 5.4-5.5 us per draw with timing stamps (M, run89/91);
the route's apply/undo was 6.68 us per routed draw, four `SetRenderTarget`
3.1 us of it, and the gate 16.65 us dominated by ~21 `ReadProcessMemory`
syscalls (M, 2026-09-12, before the read cache; current value unknown).
`--motion-rt-mode lazy` is fixture-equivalent and cuts the four binds to one
flush per run of routed draws. The sun lane adds 1.2-1.7 us per draw (M,
run97); cascades/light-map/receiver-depth since then are unmeasured. Saving:
each microsecond per draw is 0.93 ms at 930 draws; lazy RT ~1-1.5 ms (I), gate
and lane paths 0-1.5 ms (A). Risk: the route's restore contract (existing
fixtures). What settles it: `X3M_TELEMETRY_DRAW=1` (`gate_us`/`route_draw_us`/
`set_rt_us` in `motion_output_frame`; no launcher flag yet) plus
`--frame-timing` for `draw` vs `draw_native`.

### 2.3 Projected-size culling of small parts — 0 to ~6 ms (A), engine patch

Mechanism: the cull/LOD pass `0x0047cfe0` already computes a small-object
measure `s = r*640/D` per node and culls against the per-node thresholds
`+0x1d8`/`+0x1dc` at `0x0047d258`-`0x0047d2cf` (static, M). A stub at the
7-byte site `0x0047d42f` (option 3 of lod-selection.md: `engine_patch` claim,
EAX/EFLAGS dead, x87 empty) can scale `s` or raise that threshold so parts
under N screen pixels are never queued. Each removed draw saves ~23.7 us plus
its share of the caster census. Saving depends on how many of the 930 draws
are sub-pixel greebles; unmeasured. What settles it without a run: an offline
census of run124's capture frames 3494-3501 (`object_context`,
`object_position`, `object_basis`, `camera_state`, `draw` per draw) giving
projected radius per draw: 30 % under 4 px is ~6.6 ms, 5 % is ~1 ms. Risk:
visible popping of clamps and antennas; a hot-path stub per node per view
(~0.1 us x nodes, I). Native parity by construction. Effort: M (site verifier,
CPU fixture like `lod_scale`).

### 2.4 Distance LOD bias in code — 0 to several ms (A), engine patch

View Distance "Very High" subtracts one from every selected LOD index
(`0x0047d48b`, M static) and raises the far plane to 500 M; "High" keeps the
ladder. A distant station body is 15 draws / 11.5 k triangles at LOD 2 against
1 draw / 1.8 k at LOD 3 (M, capture joins), so one step is a large draw
multiplier for every distant body. The `lod_scale` mirror at `0x0047d44b`
with a factor below 1 biases coarser by a fractional step (small change; the
truncation cap only bites in the finer direction). Saving: the draw difference
between "Very High" and "High" at the same view, times 23.7 us; unmeasured.
What settles it with no code: one session at the run124 station view with
View Distance "High", comparing `frame_end draws`/`dt` against 930 / 32 ms.
Risk: visual; the user chose the finer ladder, so a fractional bias plus the
500 M plane is the compromise to test. Effort: S.

### 2.5 Replace the O(n^2) draw-queue sort — 0-1 ms (I), engine trampoline

`0x0047e620` is a linked-list bubble sort that restarts from the head after
any swap, called per (view, layer) at `0x004722af` and `0x0047248b` (M
static). Its cost sits in `prepare`, which grows ~linearly from 60 to 827
draws (1.8 ms + 4 us/draw, I), bounding the sort well under 1 ms unless the
per-layer queues are much longer than the draw count. A proxy merge sort
behind a `call` claim on the 5-byte site `0x004722af` needs the comparator's
key (the `0xc0`-byte routine, two directions by `view[0x270] & 0x80`; not
decompiled). What settles it: one `sort_us` stamp pair on that call in the
next diagnostic build. Effort: M. Same bucket, same bound: the linear cache
walk in the traversal `0x0047d9c0` (`malloc(0x70)` on a miss, no free).

### 2.6 Effect state manager bypass or filter — <= 1 ms, stays closed

The earlier <= 1 ms figure counted only the device-side cost of the ~63 state
calls per draw (11-15 ns each when redundant, M benchmark): 0.6-1.4 ms if all
vanished, 0.3-1.0 ms for the proven no-ops. The revisit does not move it:
native D3DX re-applies all 57 pass states on every `BeginPass` (M fixture) and
the redundant path is the cheap one; the ~5 us above the 1.5 us fixture floor
is wined3d processing the states that *do* change between materials plus the
proxy's hooked setters (I), which a same-value filter cannot skip. A full
bypass (own pass replay) stays bounded at 2.8-3.6 ms for a motion-output-sized
component with an FXLC evaluator. Unmeasured: the proxy's share inside
`BeginPass` (hooked shader pair lookup, `SetTexture` 125 ns, constants),
<= 1.5 us per pass (I); `--frame-timing --frame-timing-state-stamps` splits it.

### 2.7 Draw batching / instancing — <= 1.1 ms ceiling, stays closed

5.0 % of consecutive busy draws share a mesh, `same_mesh_any_range=0`, 6.0 %
share a material (M run91). A perfect instancer removes 47 of 930 draws =
1.1 ms and needs the vertex shaders to read a per-instance transform (rewrite
of the 22 effects); a material sort would break the engine's depth/layer order.

### 2.8 Wine-side per-draw cost — no lever left beyond 2.2

The forwarded draw is 2.56 us (M); CSMT off doubled it (M, run37); Wine's
builtin D3DX is +32 % on `BeginPass` (M, run36); FEX TSO off is within 1 %
(M, run37); DXVK rendered black (assessment). Reducing what the engine sends
wined3d per draw is 2.6; the proxy's own hooked setters are 2.2/2.6. Untested:
the wined3d Vulkan renderer (`renderer=vulkan`), one measurement session if
CrossOver exposes it (A; possibly no device).

### 2.9 FEX-side — nothing measurable is known

`FEX_X87REDUCEDPRECISION=1` is on; TSO is closed. JIT knobs (multiblock, AOT
cache) are not known to be exposed by CrossOver's FEX; effect unmeasured (A).
The structural lever is code: the engine's per-draw and per-node loops are x87
(`FILD`/`FMUL`/`FSQRT` in the point-light loop, the LOD multiply, the matrix
routines), so an SSE2 trampoline for one hot routine is the same class as 2.1.
Without a profiler that attributes under FEX the candidates come only from
stamps: the engine's per-draw share is ~4 us (I), so even halving all of it is
~1.9 ms (A). Not a first move.

### 2.10 Multi-threading the engine's submission — closed by structure

The device is created without `D3DCREATE_MULTITHREADED` (flags `0x52`, M run87);
one main-loop thread runs simulation, traversal and every D3D call (M,
effect-pass-loop.md); the traversal mutates node state (`+0x12c` flags, the
light-slot write at `0x004c27c5`, cache `malloc`), and D3DX effects and the
state manager are not thread-safe. Pipelining simulation N+1 against render N
needs double-buffered transforms the engine lacks. The GPU-side thread that
exists is wined3d's CSMT, worth 46 % of the frame (M). No patch-sized version.

### 2.11 Per-node point-light admission at `0x004c27a1` — <= 0.3 ms, not a lever

Per part: 8 slot iterations, 2 full tests of ~30 instructions plus `sqrtf` and
ftol (M static): 0.1-0.3 us per draw (A), <= 0.3 ms per frame; the root
admission option adds 22-88 ns per test (M fixture). A cost to know, not cut.

### 2.12 Smaller items, for completeness

SEH record and `0x4c8` frame per node in `0x004c0150`: <= 0.1 us per draw (A).
D3DX parameter setup 1.63 us per draw for ~75 setters (M), ~22 ns each:
nothing to dedup. Per-view fixed cost: 9 views x (0.16 ms setup + share of the
1.8 ms fixed `prepare`); which views cost what is not recorded, a view that
draws nothing might be skippable, <= 1-2 ms (A). Shadow replay 1.5 ms at 779
casters (M) is proxy-owned; a caster budget for cascades 3-4 is 0.5-1 ms (I).

## 3. Recommended order and the run to fly first

1. **Telemetry-only session, no captures, no code change:**
   `python3 tools/manage.py launch --telemetry --frame-phases --loop-phases
   --game-phases --pass-phases --residual-phases --frame-end-stride 10
   --capture-frames 0`, play options otherwise as in run124/125. Stamp cost
   ~0.4 ms per busy frame (pass + residual, M) plus ~1 us per active sector.
   Stand: (a) the run125 corvette area (frames 18600-19200), hold 90 s, with
   whatever triggered the 24 fps episode if it recurs; (b) face empty space
   30 s as control; (c) the run117 station of run124, hold the ~900-draw view
   60 s. Read: `loop_phases` intervals against `input_p50_us` (= pre_render),
   `residual_phases prepare/other`, `pass_phases` apply/draw on the current
   build, and any `game_phase_segment` rows.
2. If the loop residual or `PendingVm` owns the run125 pre_render, land the
   threshold change (20 ms) and, if needed, the `sort_us` stamp and
   `X3M_TELEMETRY_DRAW` launcher flag in one diagnostic build; fly once more.
3. In parallel, offline: the projected-size census on run124's captures (2.3)
   and the View Distance "High" A/B session (2.4, no code).
4. Then choose between 2.1 (if a redundant scan is named), 2.3/2.4 (if the
   census says draws are dominated by tiny parts), and 2.2 (always available,
   proxy-owned). 2.5 only if its stamp shows >= 1 ms.

## 4. What stays closed, with numbers

| Lever | Bound | Why |
|---|---|---|
| Engine-side state filter / manager bypass | 0.3-1.0 ms | device-side redundant calls are 11-15 ns; the rest of `BeginPass` is real state change and D3DX's walk |
| Pass replay (own `BeginPass`) | 2.8-3.6 ms | needs an FXLC/preshader evaluator; largest hot-path component; native precision unverifiable |
| Setter-dedup wrapper | 0 | same-value `Set*` already costs nothing in native D3DX (fixture) |
| Technique-handle cache | 0.007 ms | measured offline |
| Instancing / material sort | <= 1.1 ms | 5 % candidates, shader rewrite, order break |
| Wine builtin D3DX | -32 % on BeginPass | measured worse |
| CSMT off, FEX TSO off | +46 %, +-1 % | measured |
| Point-light loop, SEH per node, parameter setup | <= 0.3, <= 0.1, ~0 ms | arithmetic on measured per-call costs |
| Threading the submission | n/a | single-threaded engine and D3DX, non-MULTITHREADED device |

## Unknowns

The owner of run125's 18-20 ms `pre_render` (section 3 run); the proxy's
current per-draw cost after the shadow work (2.2); the projected-size
distribution of busy draws (offline census); the draw-count sensitivity to one
LOD step (View Distance A/B); the sort's share of `prepare` (one stamp pair).
None needs disassembly first; 2.1's patch and 2.5's comparator do, afterwards.
