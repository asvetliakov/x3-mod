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
object per frame), collision `0x0045d250` (**resolved for run129's 26 ms
plateau**: it holds an explicit unguarded all-pairs loop over the sector's class
buckets with a `FSQRT` per pair for all class combinations except a short list,
and the swept query `0x0045cab0` scans every sector object per bucket-0 object;
loop structure, two broadphase hook sites and the census that must precede them
are in [../reverse-engineering/sector-collide.md](../reverse-engineering/sector-collide.md)),
the timed tick `0x004596e0` (own
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

**Implemented 2026-09-18 (not yet flown): `--collide-box-cull`.** Run 42 A named the owner: sector collide
`0x0045d250`, 26.2 ms flat (96 % of pre_render). Its all-pairs loop square-roots every allowed pair with no
bounding-box reject ([sector-collide.md](../reverse-engineering/sector-collide.md)). `X3M_COLLIDE_BOX_CULL=1` inserts
the integer box at the two pair tests (`0x0045d58e`, `0x0045cc7c`) and jumps to the engine's own continue label for
pairs its `dist > R` compare would discard anyway; nothing else changes (section 10 of the note; ledger
[collide-box-cull.md](../verification/collide-box-cull.md)). How the flight measures it: the same stand twice with
`--telemetry --frame-phases --loop-phases`, once with `--collide-box-cull`; compare `loop_phases collide_p50_us`
(saving) and read `collide_census p1_pairs_p50 / p1_rejected_p50 / p2_cands_p50 / p2_rejected_p50` (pair count and
reject share per frame; F8 gives exact `collide_census_frame` rows). Fixture estimate, harness included, Wine/FEX:
75.6 -> 67.0 ns per box-rejected pair, 74.4 -> 78.6 ns per pair the box keeps; per-pair engine cost under FEX is what
the flight supplies (`collide_p50_us / p1_pairs_p50` with the option off is not available, since the counters live
in the stubs: use `p1_pairs_p50` from the on run for both).

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

**Measured 2026-09-18 (run129 A and the route bench).** What the frame-line
fields bracket (`motion_output.cpp`, `draw_stamp` pairs): `gate_us` is
`before_draw` minus the apply, the sentinel fill and a lazy flush;
`route_draw_us` is the apply (before the native draw; the four
`SetRenderTarget` calls are inside it, so `set_rt_us` is a sub-span, not an
addend) plus the undo after the draw; `jitter_us` is the two clip-row writes.
None of them contains the native draw, `after_draw`'s candidate, retention,
sun-lane or cutout work, or the hook envelope. Run129 c2 (825 routed draws,
`rs_mode=get`, `state_shadow=0`, `rt_mode=perdraw`): gate 3.7 + route_draw 5.7
(set_rt 2.55 inside it, 0.64 per bind with its stamps) + jitter 0.32 = **9.7 µs
proxy-only per routed draw** (M), not the 12.3 of the triage table, which added
`set_rt_us` twice. The `--telemetry-draw` stamps themselves (18 QPC per routed
draw; QPC is 0.07 µs on this bottle, not a 0.75 µs syscall) are inside those
spans: +1.5 µs per routed draw (M, bench).

Route bench (`verification/probe/run_route_bench.py`, fixture mode
`routebench`: 400 consecutive routed draws of the reviewed pair per frame on
the fixture device under the seam DLL, DrawPrimitive wall time, median of ten
frames; `off` is the proxy's draw hook with the route off, 1.48 µs; results
`verification/results/bottle-X3/route-bench-*.json`). Proxy cost per routed
draw before the trims (M): per-draw production route **7.5**; with the
ownership wrapper (the gameplay configuration: the route's ~21 changing
native calls go through the wrapper) **9.5**; plus the single-map depth-replay
lease per candidate draw (`GetVertexDeclaration` through the wrapper, two
buffer-lock views under the registry mutex, three AddRefs, the record)
**+1.9**; five cascades (mask, `caster_key`, extent lookup) **+0.25**; caster
retention (journal) **+0.26**; `X3M_TELEMETRY_DRAW=1` **+1.5**. Inside the
plain 7.5: RT2 (two binds, two masks, one read) 0.9 and the RT1 pair about the
same (lazy mode removes 2.05: three binds, two masks, two reads); the jitter's
two row writes 0.18; the eleven per-draw `GetRenderState` reads of the hybrid
unhook < 0.1 (the shadow configuration measures 9.07 against 8.98: no gain to
be had from caching state reads); the rest is the variant VS/PS binds and
restores, the two constant uploads and the wrap-state reads/sets, about 21
changing wined3d calls at ~0.3 µs each. The bench's prediction for run129's
configuration (ownership + cascades + retention + telemetry-draw, 704 leased
records of 825 routed draws) is ≈ 13 µs per routed draw; the 9.7 the fields
report is that minus the unbracketed `after_draw` work and the wrapper's share
of the hook envelope.

Trim implemented: the depth lease takes the stream-0 verdict from the
declaration hook's own `GetDeclaration` read
(`shadow_.declaration_stream0_only`) and gate 4's `GetStreamSourceFreq` value
carried on the route, dropping two calls per leased draw. Its gain is at the
bench's noise floor: three alternating runs of the pre-trim and the final seam
DLL (`route-bench-ab-base-N.json` / `-ab-final-N.json`, DrawPrimitive µs per
draw, median of the three, pre-trim -> final): ownership 11.27 -> 11.18, depth
lease 12.94 -> 12.79, cascades + retention 13.58 -> 13.41, i.e. 0.1-0.2 µs per
leased draw against a run-to-run spread of ±0.2 (≈ 0.1 ms per 825-draw frame,
not resolved). Every compared fixture case equals the pre-trim build
([motion-output.md](../verification/motion-output.md), "2026-09-18 —
routed-draw cost bench").

Dropped after review: skipping the pixel-ABI upload (c216-c217) while the
device still holds it. `resync_shadow` runs at enable and after Reset and sets
`ps_reserved_written` from a successful `GetPixelShaderConstantF(216)`, so on a
real device the undo restores the range after every routed draw and the skip
can never fire; making it fire means not restoring c216 per draw, which is a
restore-contract change. The 0.24 µs the first bench showed for it was noise.

Refused: `--motion-rt-mode lazy` as the default. Lazy re-installs the
SetRenderState/SetSamplerState hooks (`state_hooks reason=lazy_rt`; the held
write masks need the write observation): +64/+57 ns per call on the 49,598
light-pair calls of run89's 987-draw frame ≈ 3.1 ms against 2.05 µs × ~900
routed draws ≈ 1.85 ms saved, a net loss of ≈ 1.2 ms (I, from measured
per-call and per-draw numbers) unless lazy can hold the masks without the
hooks, which is a restore-contract change. Levers left, all outside a
per-draw trim: the ownership wrapper's share of the route's own calls (2.0
µs per routed draw ≈ 1.7 ms at 825, a direct native path for the route's
setters under the wrapper's identity contract); the lease's per-draw wrapper
work (1.5 µs per leased draw; a per-frame declaration reference dedupe would
change the identities the retention store keys); the per-draw telemetry
(diagnostic only: do not read route cost off a `--telemetry-draw` session
without subtracting it).

### 2.3 Projected-size culling of small parts — 9.6 ms at 2 px (M, census), implemented as `--cull-small-parts <px>`, unflown

Mechanism: the cull/LOD pass `0x0047cfe0` computes a small-object measure
`s = r*640/D` per node and culls against the per-node thresholds
`+0x1d8`/`+0x1dc` at `0x0047d258`-`0x0047d2cf` (static, M). The run131 census
(`--cull-census`, frame 4991 of the run117 station view, 901 draws / 32 ms,
`tools/analysis/cull_census.py`) measured the classes with the engine's own
numbers: nodes under 2 px are 403 draws (9.55 ms at 23.7 us/draw), under 4 px
458 (10.85 ms), under 8 px 479; the engine's own cull already removes 91.5 % of
the sub-2 px nodes, and every surviving tiny node has `+0x1d8 = +0x1dc = 0`,
so the lever is a floor under a threshold the assets leave at zero.
Implemented (2026-09-18) as `tools/manage.py launch --cull-small-parts <px>`
(`X3M_CULL_SMALL_PARTS_PX`, default absent or 0 = vanilla): one `engine_patch`
trampoline at `0x0047d2a2` (lod-selection.md, "Cull small parts site")
compares the pass's `s` against a per-frame threshold derived from the live
projection scale and the back-buffer width with the census's own bucket rule
and sends a node below it down the engine's size-cull instruction at
`0x0047d2c3`; disjoint from the census's and the lod_scale's claims, all three
coexist. Expected saving at the flight settings: 2 px about 9.6 ms of the
32 ms frame (403 of the 878 census-attributed draws; 901 in the frame), 4 px about 10.9 ms (458), from the census, both lower bounds (a culled node also culls its `0x40000`-flagged children at `0x0047d055`-`0x0047d076`); the
per-draw proxy work saved with them is on top. Not yet flown: the first
session with the option on should capture the same station view with
`--cull-census` too, so the rows name the stub's culls (`verdict=culled_small`)
and `frame_end draws`/`dt` give the real saving. Risk: popping of thin parts
(antennas, clamps) whose radius is small but whose length is not, the same
bias as the engine's own radius cull, and the pop can cascade to descendants; the threshold applies in every view, so small casters leave the shadow and env maps too, and the one main-view `m00` scales every view; the hot-path cost is one compare and a dead
branch per node per view when the frame's threshold is 0 and the stub's
straight-line integer code otherwise (fixture: 0.235 -> 0.244 us per 12-node
pass disarmed, 0.237 armed with 7 culled, Wine/FEX). Native parity by
construction (documented Win32 only).

### 2.4 Distance LOD bias in code — 0 to several ms (A), engine patch

**Closed 2026-09-18 (run 42 D, run132):** View Distance Very High → High at the run117 station gave the
user ≈ 1.5 fps; the 30 s busy windows read 923 draws / 33.4 ms against 861 / 31.2 (M), a ≈ 7 % draw
reduction for one full LOD step on every body. A fractional bias would give less. Not worth a patch.

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

## Attribution at the ~50 fps baseline (2026-09-19)

Triage from existing flight logs (no new launch). Sources: run147
(`/tmp/x3-bottleX3-run147`, busy station, new cull default, `--frame-phases`
only, no `--loop-phases`/`--frame-timing`/`--pass-phases`), run150/run151
(`/tmp/x3-bottleX3-run150`, `.../run151`, collide area, `--frame-phases
--loop-phases`, `--collide-box-cull` narrow-phase census active), run153
(`/tmp/x3-bottleX3-run153`, plant, baseline, same options as run147). All
numbers are `frame_phases`/`loop_phases`/`collide_narrow` window p50 lines
(nearest-rank percentiles over 300-frame windows), read with `grep`/Python
only; no full-file read.

**Run147, busy station, window `frame=3600` (510 draws median, 49.2 fps).**
`frame_phases frame=3600`: `dt_p50_us=20320`.

| Phase | ms | % of dt |
|---|---|---|
| `pre_render` (script/AI/collide/sim/proxy post-Present, unsplit here) | 3.28 | 16.1 % |
| `prologue`+`scene_update`+`begin_scene` | 0.41 | 2.0 % |
| `views` total | 16.21 | 79.8 % |
| — `view_setup` (light select, state/camera/viewport/clear) | 1.70 | 8.4 % |
| — `view_submit` (traversal/sort/D3DX submission `0x004c0150`) | 11.15 | 54.9 % |
| — residual (particles, scene-end composite: TAA/shadow replay/sun apply, env-map, fixups; difference of phase medians) | 3.362 | 16.5 % |
| `scene_end` (game `EndScene` + gated tails) | 0.30 | 1.5 % |
| `present` (native Present) | 0.006 | 0.03 % |

`present_p50_us=6` (run150/151 busy windows: 6-8 us) confirms this frame is
CPU-bound, not vsync/GPU-bound: `dt` is almost entirely engine+proxy CPU work.
No `--frame-timing`/`--pass-phases` in this run, so `view_submit` cannot be
split into engine per-object cost vs D3DX apply vs proxy per-draw hook vs
native `DrawIndexedPrimitive`; the run124/run113 per-draw anatomy in section 1
(23.7/23.4 us/draw) is the only prior measurement of that split and is reused
here as context, not re-measured.

**Run150, collide area, peak window `frame=5400` (422 draws median, 24.2 fps).**
`frame_phases frame=5400`: `dt_p50_us=41297`. `loop_phases frame=5400`:
`input_p50_us=29780 collide_p50_us=26508 simulate_p50_us=39 post_p50_us=8
passb_p50_us=95`.

| Phase | ms | % of dt |
|---|---|---|
| `pre_render` total | 29.78 | 72.1 % |
| — `collide` (sector collide `0x0045d250`, all-pairs + narrow-phase BVH) | 26.51 | 64.2 % |
| — `simulate`+`post`+`passb` | 0.14 | 0.3 % |
| — residual (script VM, cockpit `0x0041cde0`, audio/media, proxy post-Present; no site) | 3.13 | 7.6 % |
| `views` total | 11.17 | 27.1 % |
| — `view_setup` | 0.60 | 1.4 % |
| — `view_submit` | 8.80 | 21.3 % |
| — residual | 1.77 | 4.3 % |
| `scene_end`+`present`+other core stamps | ~0.10 | 0.2 % |

`collide_narrow frame=5399` in the same run: `node_pairs_p50=225045
mesh_pairs_p50=174 accepted_p50=17 narrow_us_p50=26336`. 225,045 BVH node-pair
visits and 174 mesh pairs per frame produce only 17 accepted contacts, at a
narrow-phase cost (26.3 ms) matching the `loop_phases` `collide` bucket
(26.5 ms) almost exactly. Contrast the same run's low-collide window
`frame=2700`: `dt_p50_us=8051`, `collide_p50_us=298` (3.7 % of dt) — collide
is episodic, not a constant per-frame tax, consistent with section 2.1's
run129/§10 finding. Run151 shows the same collide/no-collide swing (e.g.
`frame=5400` `collide_p50_us=8867` of `dt_p50_us=22247`, `frame=2700`
`collide_p50_us=503` of `dt_p50_us=8102`).

**Run153, plant, baseline window `frame=9000` (65-85 fps range across the
run).** `frame_phases frame=9000`: `dt_p50_us=11714 pre_render_p50_us=2967
views_p50_us=7951 view_setup_p50_us=910 view_submit_p50_us=4924`. No
`--loop-phases` in this run, so `pre_render`'s 2.97 ms is not split further
here; it is in the same 2.9-5.5 ms range as run147/run150's non-collide
windows, consistent with collide/script/AI cost being small away from a
collide-heavy area.

**What is attributed vs not.** `frame_phases` accounts for 100 % of `dt` by
construction (nine phases summed by the diagnostic itself). Inside that:
`pre_render` is split into collide/simulate/post/passb only where
`--loop-phases` ran (run150/151); a 3.1 ms residual remains unattributed
there (script VM, cockpit update, audio/media, proxy post-Present all share
`pre_render` with no per-site stamp). `views` is split into `view_setup`/
`view_submit` everywhere, but the "views minus setup minus submit"
residual (particles, TAA, shadow replay, sun apply, env-map, fixups) has no
site of its own in any of these four logs; `view_submit` itself is not split
into engine traversal/sort vs D3DX `BeginPass` vs draw submission (engine
vs proxy vs native `DrawIndexedPrimitive`) because none of these runs carry
`--frame-timing` or `--pass-phases` (`grep -c "^frame_timing qpc"` and
`"^pass_phases qpc"` are 0 in all four logs). The sampling profiler
(`X3M_PROFILE=1`) is not present in any of these logs either
(`grep -c "^profile_report scope"` = 0), and per `sampling-profiler.md`'s
frame-timing section, it would attribute nothing useful under FEX anyway
(every leaf lands on the `ntdll` syscall thunk); the buckets above are the
only proxy/game split this evidence supports.

**What one more launch would need to close the gap:** the same collide-area
stand and the same busy-station stand, each flown once with `--telemetry
--frame-phases --loop-phases --pass-phases --frame-timing
--frame-timing-state-stamps 8` (stamps at a stride cheap enough per
`sampling-profiler.md`'s cost table) together, so `view_submit` splits into
`apply`/`draw`/`end` and the `frame_timing` `draw_native`/`state_us`
buckets in the same windows as `loop_phases`' `collide`/residual split — one
launch, both scenes, no new instrumentation needed (all four options already
exist and are documented above).

**Top 5 engine-side CPU costs ranked by measured/estimated ms** (x87-heavy
candidates for FEX flagged):

| Rank | Cost | ms (p50) | Where measured | Address(es) |
|---|---|---|---|---|
| 1 | Sector narrow-phase collide (BVH traversal, FSQRT-heavy all-pairs and narrow phase) | 26.5 (peak collide window); 0.3-2.6 (non-collide windows) | run150 `loop_phases frame=5400` `collide_p50_us`; `collide_narrow frame=5399` (225,045 node pairs, 174 mesh pairs, 17 accepted) | broad `0x0045d250`; narrow sites `0x0045d665`, `0x0048a9a5`, `0x004e2530`, `0x004e2190` ([sector-collide.md](../reverse-engineering/sector-collide.md)) |
| 2 | View submission: per-object traversal/sort + D3DX material pass loop | 11.15 (run147 busy); 8.6-8.8 (run150/151 busy) | `frame_phases` `view_submit_p50_us` | traversal `0x0047e920`, sort `0x0047e620`, submission `0x0047e6e0`, D3DX pass loop `0x004c0150` |
| 3 | `views` residual: particles, scene-end composite (TAA, shadow replay, sun apply), env-map, fixups | 3.362 (run147 busy); 1.6-3.1 (run150/151 busy) | `frame_phases` `views_p50_us - view_setup_p50_us - view_submit_p50_us` (difference of medians) | not individually sited here; `--residual-phases` separates particles from other; existing proxy pass timings give partial further attribution (follow-up audit below) |
| 4 | `pre_render` residual outside collide (script VM, cockpit update, audio/media, proxy post-Present) | 3.13 (run150 peak-collide window); 2.9-4.7 (non-collide windows, unsplit — no `--loop-phases`) | `loop_phases` `input_p50_us - collide - simulate - post - passb` (run150/151 only) | script VM address not in this evidence; cockpit update `0x0041cde0` (named in section 1, not separately stamped) |
| 5 | Per-view setup (light selection, state/camera/viewport/clear) | 1.70 (run147 busy); 0.6-1.7 (run150/151 busy) | `frame_phases` `view_setup_p50_us` | light selection `0x004892a0`; setup span `0x0047224c`-`0x00472270` |

Rank 1 is the clear outlier and matches section 2.1's `--collide-box-cull`
lever already implemented (unflown for this A/B): these logs did not run with
the option toggled off in the same session to report a direct saving, so the
box-cull's actual ms reduction is not in this evidence (the fixture-only
75.6→67.0 ns/pair estimate in section 2.1 stands). Ranks 2-3 are the next
levers by size (sections 2.5 draw-queue sort, 2.6/2.7 state/batching, closed
per section 4) but are already covered by the existing note; nothing here
changes those conclusions, it only re-confirms their relative size against a
fresh collide-heavy sample.

## Run 46 D — view_submit split (2026-09-19)

`/tmp/x3-bottleX3-run162`, busy station view, `--collide-sat-sse2
--collide-memo --loop-phases --pass-phases --frame-timing
--frame-timing-state-stamps 8`, one F8. First run in this note with
`frame_phases`/`loop_phases`/`pass_phases`/`frame_timing` all present
together (17 windows each). Steady busy plateau: windows `frame=3300..4800`
all `dt_p50_us` 26.4-27.2 ms; window `frame=4200` used below (draws_p50=510,
matching run147's draw count exactly).

`frame_phases frame=4200`: `dt_p50_us=27237`. `frame_timing frame=4200`:
`dt_p50_us=27249` (12 us apart, 0.04 %) — the two independent stamps agree
tightly; no sign the diagnostics inflate `dt` relative to each other. But
run147 (no `--loop-phases`/`--pass-phases`/`--frame-timing`, same 510
draws/frame) measured `dt_p50_us=20320`, 6.9 ms lower. Estimated stamped-state
self-cost at `state_calls_p50=34983`, N=8 sampling, ~230 ns/stamped call
(`sampling-profiler.md`): (34983/8) x 0.23 us ~= 1.0 ms. `pass_phases`/
`loop_phases` self-cost is 183/0 us — negligible. That leaves ~5.9 ms of the
6.9 ms gap to run147 unexplained by instrumentation alone; the two sessions
are different flights of the same view (camera angle, NPC/AI state), so this
is evidence of a real dt difference, not proof of its cause — open below.

**Hierarchical split** (`frame_phases`/`loop_phases`/`pass_phases`, ms):

| Phase | ms | % of dt |
|---|---|---|
| `pre_render` | 3.495 | 12.8 % |
| — `loop_phases` collide+simulate+post+passb (`sum_p50_us`) | 0.817 | 3.0 % |
| — residual (script VM, cockpit, audio, proxy post-Present) | 2.678 | 9.8 % |
| `prologue+scene_update+begin_scene` | 0.437 | 1.6 % |
| `views` | 22.899 | 84.1 % |
| — `view_setup` | 1.652 | 6.1 % |
| — `view_submit` | 18.316 | 67.3 % |
| —— `pass_phases` sum (`apply+draw+end`) | 14.833 | 54.5 % |
| —— view_submit residual (engine traversal/sort/entry, not in pass loop) | 3.483 | 12.8 % |
| — views residual (particles, TAA/shadow/sun composite, env-map) | 2.931 | 10.8 % |
| `scene_end`+`present` | 0.382 | 1.4 % |

`pass_phases frame=4200`: `passes_p50=503 apply_p50_us=9371 draw_p50_us=5298
end_p50_us=62`. `apply` (D3DX `Begin`/`BeginPass`/`CommitChanges`, mostly
state submission) tracks `frame_timing`'s `state_p50_us=9028` almost exactly
— the two diagnostics are measuring the same state-setting cost from two
sites, cross-confirming it. `pass_phases draw_p50_us=5298` similarly tracks
`frame_timing draw_p50_us=5153` — both are the draw-call-and-below cost.

**`frame_timing` proxy/native/state split** (ms, `frame=4200`):
`draw_p50_us=5153 draw_native_p50_us=1274 scene_p50_us=3111
state_p50_us=9028 present_p50_us=6`, `state_calls_p50=34983` (68.6/draw),
`draws_p50=510`. `gap_pre_p50_us=3781 gap_draw_p50_us=5755
gap_post_p50_us=481` (sum 10.02 ms); `dt - (draw+scene+state+present) =
27.249 - 17.298 = 9.951` ms — the gap-field sum and the subtraction agree to
0.07 ms, both estimating the game/engine CPU time outside every hooked call
(traversal, sort, matrix work — no hook-lock wait is in this remainder per
`sampling-profiler.md`, since stamps are taken lock-held).

**Requested ms/frame table** (dt=27.24-27.25 ms, two independent partitions
of the same frame, not additive across rows from different partitions):

| Bucket | ms | % of dt | Fix kind |
|---|---|---|---|
| Engine-between-calls (traversal/sort/matrix, x87 candidate) | 9.95-10.02 | ~36.6 % | engine patch |
| Native state calls (SetSamplerState/SetRenderState/.../CommitChanges) | 9.03 | 33.1 % | proxy (state fast path) |
| Proxy per-draw hook overhead (`draw_p50 - draw_native_p50`) | 3.88 | 14.2 % | proxy |
| Proxy post passes (EndScene hook, AgX/bloom compositor, Present pre/post) | 3.11 | 11.4 % | proxy |
| Native/wined3d draw call itself (`draw_native_p50`) | 1.27 | 4.7 % | driver, largely unavoidable |
| Present/wait (native `Present`) | 0.006 | 0.02 % | none needed — confirms CPU-bound |
| Unattributed (within noise of the two partitions' 0.07-0.6 ms disagreement) | ~0 | ~0 % | n/a |

Per-draw: 510 draws/frame, 10.11 us/draw total proxy `draw` hook (2.50 us
native + 7.61 us proxy overhead), 68.6 state calls/draw. No cull-census or
route-admission lines are present in this log (only `frame_phases`/
`loop_phases`/`pass_phases`/`frame_timing`/`media_cue_window`), so routed
share cannot be read from this session; §2's ~7-10 us/draw ownership/lease
figures are prior-run context, not re-measured here.

**Top 5 by ms:** (1) engine-between-calls, 9.95-10.02 ms, engine patch —
largest single bucket, not further sited in this run; (2) state calls /
D3DX apply, 9.03 ms, proxy fix — `redundant_top` shows heavy redundant
`D3DRS` churn (e.g. `7:152430` over the 300-frame window), consistent with
`state-call-fast-path.md`'s existing lever; (3) proxy per-draw hook, 3.88 ms,
proxy fix (route-per-draw-cost.md §§1-3 levers); (4) proxy post passes
(scene-end/compositor/Present), 3.11 ms, proxy fix; (5) native draw call,
1.27 ms, driver — only reducible by fewer/batched draws (§2.7, closed).

**Open:** the 6.9 ms dt gap vs run147 at equal draw count is only ~1 ms
explained by state-stamp self-cost; the remainder needs a matched pair of
launches at the same stand, one with and one without
`--frame-timing-state-stamps`, to separate instrumentation cost from a real
scene/AI difference between the two flights. Engine-between-calls (9.95-10.02
ms, the largest bucket) has no per-site split in this run; closing it needs
disassembly-level sampling (section 1's profiler leaves) at this same window,
not another `frame_timing` pass.


## Run 48 B/C: first-view freezes and the 40 FPS busy view (2026-09-20)

User report: run180 sometimes pauses about 0.2 s during the first camera sweep;
repeating the view is smooth. This predates fog. Five aligned, non-capture flight
witnesses are pre-render dominated (frame-end duration / phase pre-render ms):
32101 483/472.906; 45129 465/457.369; 50594 207/202.394;
61502 192/184.949; 63609 248/243.225. Their views take only 4.563–9.791 ms,
camera_cut=0, captures are at least 10.095 s away and the recorded initial load
is hundreds of seconds earlier. No exact gate event is established.

The largest single measured log flush in run180 is 30.6 microseconds. Shader dump
max is 7.441 ms; native pixel-shader creation 1.198 ms, texture creation 0.873 ms,
vertex/index-buffer creation 0.603/1.045 ms. None individually explains these
185–473 ms pre-render spans. This does not exclude all logging/instrumentation:
formatting and buffer-fill writes are not separately timed, and `pre_render`
includes the proxy's after-Present work as well as engine update phases. No
first-use asset or compilation cause is established.

Run181 steady windows 3000–5400 (nine 300-frame rows) confirm the reported 40 FPS:
median of window frame-time medians 24.297 ms, about 41.2 FPS (23.406–25.136 ms).

| phase, ms | earlier run147 busy view | run181 |
| --- | ---: | ---: |
| full frame | 20.320 | 24.297 |
| pre_render | 3.280 | 5.721 |
| views | 16.210 | 17.989 |
| view setup (inside views) | 1.700 | 1.639 |
| view submit (inside views) | 11.150 | 12.737 |
| scene end | 0.300 | 0.364 |

This is cross-flight evidence, not a controlled regression measurement. Frame3600
motion counts are almost identical: 510/485 routed/matched in run147, 511/485 in
run181. Run181 uses submission stamps, thin region 0.97, far stabiliser 0.985,
light-map fade 40,110,1, per-draw RT mode; fog and frame-timing state stamps are off.
Submit diagnostic self cost is 0.549 ms/frame; it explains part of the 3.977 ms
increase, not all. No separate live timing isolates the thin-region pass. The
phase medians are not additive, and equal draws do not explain pre-render growth.

**Next measurement:** existing `--loop-phases --game-phases
--game-phase-threshold-ms 20`, retaining telemetry/frame phases, frame-end stride
10, no F8 during the stutter observation. This splits collision/simulation/post/
passb and reports long game intervals; a residual still needs script/cockpit/
audio/media/proxy-after-Present attribution. Use this in the consolidated next
fog/sector flight before adding any new tracing. Do not reopen state filtering,
instancing or pass replay. The submission-specific disposition is in
[view-submit hot path](../reverse-engineering/view-submit-hot-path.md).

Local reproducible witnesses: `verification/results/run48b-triage/stutter_reproduce.py`
and `verification/results/run48c-triage/reproduce.py`. Bottle X3, arm64 Wine/FEX,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`; native Windows not measured.

## Performance follow-up audit — 2026-09-20 (diagnostic plan ratified)

**Diagnostic plan ratified by the orchestrator.** Close the measured small submission
candidates for the run181 view, not the whole performance investigation. First
attribute pre-render growth and first-view stalls; use the existing post-pass
timings; then obtain a submission split with the production setter configuration.
No production change, build, Wine execution or game launch accompanies this audit.

The evidence needs three corrections to the older interpretation above:

- Raw run147 `frame_phases frame=3600` is `dt=20320`, `pre_render=3276`,
  `views=16211`, `view_setup=1704`, `view_submit=11145` microseconds.
  Thus **views minus setup minus submit is 3.362 ms (16.5% of dt), not
  5.36 ms (26.4%)**. This is a difference of window medians, not the median
  of a measured residual. That flight has 29 frame-phase windows and zero
  loop/pass/residual/frame-timing windows. Its 3.276 ms pre-render and
  11.145 ms submit were not individually attributed in that capture.
- Run162's 9.95–10.02 ms “engine-between-calls” is a whole-frame complement,
  including unhooked library code and pre-render, not 10 ms of optimizable
  draw preparation. Its 3.483 ms submission complement is the relevant older
  prepare/setup estimate; [view-submit hot path](../reverse-engineering/view-submit-hot-path.md)
  §1 already corrects this. The 0.3–0.65 ms arithmetic figure in that note
  is a model extrapolated from another routine, not a measured universal ceiling.
- Raw mode rows show run147/run181 `state_hooks installed=0 reason=none`,
  but run162 `installed=1 reason=frame_timing`. `capture.cpp`'s
  `hook_device` installs setter hooks when `frame_timing::active`; the
  [state-call fast path](state-call-fast-path.md#hybrid-unhook-step-5-implemented)
  explains their additional cost. Subtracting only the estimated 1 ms QPC
  stamp cost cannot isolate scene variance in run162's 6.9 ms gap. Turning
  only state stamps off also leaves the diagnostic setter hooks installed.
  Neither native Present's few microseconds nor CPU QPC pass timings alone
  exclude asynchronous GPU/driver work or waits charged elsewhere.

**What remains closed, and why.** Run181's nine steady windows have R3
SetTechnique/End 65/64 us, R5 sort 5 us over 30 calls, R4 zero walk calls at
the median, R1 view inverse 71 us and R8 world matrix 19 us. No replacement
is justified for these sites in this view. R2/R6 share the unsplit 0.988 ms
setup block; neither has an individual measurement. R7 at `0x0047d5e0`
is absent from `submit_phase_sites.h` and remains unmeasured. Its gated
light-list walk and `_qsort` are distinct from the measured R5 queue sort.
Keep state filtering, pass replay, instancing, draw reordering and blanket
x87 conversion closed on the existing cost/correctness evidence. Reopen
only a named assumption contradicted by a measured current cost.

**Proxy lever disposition.** The [route-cost design](route-per-draw-cost.md)
and [motion-output ledger](../verification/motion-output.md) contain the
implementation and fixture evidence, but some introductory “unflown” labels
are historical. Raw run147 already has `motion_direct enabled=1 admission=0
slots=7`: lever 1 stage A and 2a are in the 50 FPS baseline, not future gains.
The fixture reduced wrapper overhead 2.15 to 0.77/0.92 us per routed draw
(the original <=0.6 target was missed), and lease overhead 1.98 to 0.90/0.73 us.
The remaining eight interface-input unwraps explain stage A's limit; stage B
still requires an identity/lifetime design, not more value-only bypasses.

Lever 2b remains conditional. Source now confirms the previously untraced
ordering: `MotionOutput::retention_scene_end` drains retirements, can flush
on a sun change and calls `release_retention_pending` before replay
(`motion_output.cpp` scene-end call; `motion_output_shadow_retention_inc.h`).
`run_shadow_replay_cascades` releases depth leases after the transaction
(`motion_output_shadow_replay_inc.h`). Borrowing store references without
pinning them through lease retirement is unsafe. The earlier 0.5–0.7 us per
matched lease estimate scales to only about 0.20–0.27 ms at run181's median
391 leased draws, plus unmeasured scene-end release cost; matching eligibility
and pin overhead reduce that estimate further. Buffer revision reads remain
mandatory. `release_depth_leases()` lies outside the recorded replay `us` span.

Lever 3 is implemented and fixture-equivalent, but has no proven flight FPS
gain. Run165/166 reduced proxy draw overhead by about 0.899 ms while native
draw time rose about 0.425 ms and draw counts differed. Both enabled diagnostic
setter hooks. The ledger explicitly leaves the cause unresolved; the handoff's
“wined3d defers the cost” is a plausible explanation, not a demonstrated one.
Run167 is lazy without frame timing but with the blind sampling profiler, not
a clean paired control. Keep lazy optional. The fixture's 2.53 us per depth
draw suggests about 1.2 ms at 485 routed draws before displacement, flushes
and noise; actual net benefit could be zero or negative.

**Existing captures answer part of the residual now.** Streaming the two
`session-*.log` files for frames 3301–3600 gives the following CPU QPC medians;
these are cross-flight observations, not a causal A/B or additive partition.

| Recorded scope | run147, us | run181, us | Rows per run |
| --- | ---: | ---: | ---: |
| Shadow replay transaction | 756.05 | 851.0 | 300 |
| Retention scene-end processing | 104.25 | 168.9 | 300 |
| TAA run | 282.0 | 444.0 | 5 |
| HDR writeback | 94.0 | 518.0 | 5 |
| HDR meter, **inside writeback** | 51.1 | 451.8 | 5 |

Selectors are `shadow_replay_depth us`, `shadow_retention_frame us`,
`motion_output_frame taa_run_us` and `hdr_frame writeback_us/meter_us`, with
integer `frame` in `[3301,3600]`. Logs are
`/tmp/x3-bottleX3-run147/session-20260919-054326-212.log` and
`/tmp/x3-bottleX3-run181/session-20260920-003127-212.log`; parse line-by-line
with Python `re.findall` and `statistics.median`. TAA/HDR rows are sparse and
can be cadence-biased. `MotionOutput::hdr_writeback` and `HdrPass::copy_draw`
prove meter time is nested; TAA subfields are likewise not extra costs.
These observations justify investigating the HDR-meter/writeback increase
and the combined TAA work, but cannot assign the thin-region feature a cost
or account for the full 3.977 ms frame-time difference. Existing run180 slow
witnesses locate stalls in pre-render but do not identify its callee; F8
images cannot reconstruct missing execution timings.

For the five individual frames 3360, 3420, 3480, 3540 and 3600, compute
`writeback_us - meter_us` **within each row before reducing**: its median is
43.9 us in run147 and 50.9 us in run181. Thus almost all of this sparse
writeback increase lies inside `HdrPass::meter_chain`'s bracket, not the outer
tone-map/state-restore work. The cadence limitation and cross-flight caveat
still apply; this does not establish whether the meter does more work or waits
for prior GPU work.

Ranked next investigations, with budget distinguished from possible saving:

| Priority | Investigation and deciding evidence | Scope/cost and limit |
| --- | --- | --- |
| 1 | Use the already queued `--loop-phases --game-phases --game-phase-threshold-ms 20` with frame phases. Compare steady pre-render as well as non-capture stalls; read owner/segment rows before choosing a patch. | 5.721 ms steady pre-render is a budget, not a saving; 185–473 ms stalls deserve separate tail analysis. Existing stamps, no new per-draw work. |
| 2 | Align existing replay/retention/TAA/HDR fields and full telemetry-window metrics, checking capture proximity and sparse cadence. If an increase persists, isolate that pass at a fixed view with feature-equivalent output. | Existing rows cost no new runtime work. HDR writeback's observed +0.424 ms and TAA's +0.162 ms are leads, not causal savings or GPU timings. |
| 3 | Consolidate `--residual-phases` into the next diagnostic flight; it implies pass/frame phases without `--frame-timing` (`tools/manage.py`). Inspect apply/draw/end, prepare/setup and particles/other with `state_hooks reason=none`. | At ~503 passes, four pass dispatches plus ~504 material and nine particle stamps cost about 0.23 ms at the existing 91 ns model; verify reported self cost. Material net 11.620 ms is a mixed budget, not recoverable engine time. |
| 4 | Include a minimal opt-in R7 whole-call count and elapsed time by caller in the consolidated candidate, after hook/ABI qualification. Do not initially stamp the light loop or qsort separately. | No present saving estimate. Fixed accumulators, no per-call allocation/logging/lock; roughly 0.204 us per timed call from two 102 ns dispatches is only a preliminary model (0.102 ms at 500 calls). Count-only cannot size the candidate and would risk another flight. |
| 5 | Measure lease-retirement cost and live-record match rate before designing 2b pins. Then prove sun flush, eviction, mutation, shadow-off, skipped scene-end, Reset/loss and teardown cannot free a borrowed resource. | About 0.20–0.27 ms estimated at current leased count plus unknown releases; pin and lookup cost may consume it. No naive borrowed-pointer patch. |
| 6 | If a <=1 ms opportunity merits the user time, compare perdraw/lazy in matched production-mode flights without frame timing or the profiler; preserve feature set and camera/draw counts. | Existing option, no implementation required. Fixture ceiling roughly 1.2 ms; use end-to-end frame distributions, not just bind count or proxy-draw savings. |

The main-loop RE already names script VM `0x0049f770 -> 0x004a26a0`,
cockpit `0x0041cde0 -> 0x004205e0`, audio `0x0049a130` and media `0x00498370`
in [frame-loop phases](../reverse-engineering/frame-loop-phases.md). Their
existing game-phase segments should choose the next disassembly target.
`capture.cpp` closes `frame_phases::present_end()` before
`MotionOutput::after_present()` and reporting, so an unexplained pre-render
residual must still consider proxy formatting, drains and resource retirement;
a small measured log flush alone does not exclude those. Do not run the blind
FEX leaf sampler again or disassemble an arbitrary hot-looking routine first.

The existing diagnostic flags to add to the parent's complete feature-preserving
launch command are exactly:

```text
--telemetry --frame-phases --loop-phases --game-phases --game-phase-threshold-ms 20 --residual-phases --frame-end-stride 10
```

They need no new implementation. Preserve the current rendering options and
`--motion-rt-mode perdraw`; remove `--frame-timing`, state stamps, `--profile`
and the already-flown 22-site `--submit-phases` group from this attribution
flight. No F8 during the first-view-stall observation. The new R7 option name
and the separately owned collision query/descent option are for the parent to
ratify and add to this same candidate/run; the subsequently qualified flag is `--light-phases`.

Bounded offline `i686-w64-mingw32-objdump -d -M intel` on the bottle EXE
confirmed two direct calls to R7: `0x0042173b` and `0x0047dff1`, returning
to `0x00421740` and `0x0047dff6`. The older hot-path note's `0x0047d9f6`
caller-frame address is not this call. R7 has the `node+0xa0` and
`0x00488170` gates, a conditional `_qsort` at `0x0047d9a3`, and a shared
`pop edi; pop esi; pop ebx; mov esp,ebp; pop ebp; ret 4` epilogue at
`0x0047d9ab`. The subsequent targeted RE qualified the entry/common-exit spans and inbound
edges in [view-submit hot path](../reverse-engineering/view-submit-hot-path.md#r7-whole-call-timing-boundary-qualification-2026-09-20). Runtime instrumentation subsequently passed its CPU fixtures and independent
review; run49 results follow below. Distinguish both callers
in the timing so non-submission work is not charged to `view_submit` by
assumption. Full-call timing is enough to decide whether deeper R7 work is
worthwhile; keep nested qsort detail deferred unless the qualification reveals
a reason it is needed in the first build.

**Acceptance and portability.** Parent ratification selects the diagnostic
scope, not an optimization claim. Accept attribution only with valid sites,
zero clock/unmatched/overflow errors, known capture exclusions and reported
diagnostic self cost; compare equivalent setter modes and do not sum marginal
percentiles as a frame partition. Any later production proposal must show a
repeatable end-to-end gain, unchanged rendering/simulation contracts, relevant
state/Reset/lifetime fixtures, CPU/LastError preservation and a hot-path cost
account. Existing tools use documented QPC/COM APIs and validated game EXE
sites; no Wine-private dependency is needed. Native Windows functionality and
timing remain unverified, and FEX costs must not be projected as native gains;
the gap stays in [platform portability](platform-portability.md). Open questions
are the pre-render owner, R7 frequency, current production apply/draw split,
post-pass cadence/waits, safe lease pins and lazy's net production benefit.


### Run180 stall aggregation follow-up

The five witness frames produce ordinary frame-tagged log volume: 14 lines /
4.63–4.76 KB for four, 17 lines / 5.16 KB at frame45129, versus nearby
13–14 lines / 4.51–4.75 KB. No frame-tagged resource creation or shader dump
accounts for them. Frame45129's overlapping loader interval reports only
FindFirstFileA 230.4 us, FindClose 1.4 us and log-flush at most 1 us.
Frame61502 overlaps a one-second cumulative loader summary with 35 texture
creations / 133.705 ms inclusive and 148 mesh creations / 33.750 ms. These
are not per-call timestamps; file work nests inside them and the summary
cannot be assigned to the 184.949 ms pre-render stall or summed as independent
cost. The other four witness windows have no comparable creation summary.
Instrumentation and loader work therefore remain possible owners; the next
loop/game-phase flight must establish attribution before a patch is selected.


## Run49 A: three-scene diagnostic/counter flight (2026-09-20)

The user flew run183 with phase diagnostics and run184 with them off. Both used
three scenes in order: busy-station save, **new game in Argon Prime** (the reported
camera-turn stutters), then the corvette collision/solar-plant save. The user says
the busy-station save does not exhibit these camera stutters. Both sessions log
the same DLL/source identity, 18 identical common mode records, original hulls,
light-map fade 80,220 with floor 1, per-draw RT binding, and `state_hooks=0`
(`reason=none`). The differing media trampoline address is an allocation address,
not a setting change. No F8 image burst was taken; this flight gives no new
moving-lattice image-quality acceptance.

Camera-validity transitions bound run183 gameplay scenes at 408–8050,
8870–16315 and 16664–30253; run184 Argon is 8568–15701. Only the first load
has `loading_phase` markers, so those markers must not be used as a complete
three-scene segmentation. Conservative interior windows exclude transitions.

### Busy station: approximately 50 FPS without phase diagnostics

Run183 plateau windows ending 4800–6900 have 472 pass/material calls per frame,
479 draws and 454 routed matches, without camera cuts. Median of eight 300-frame
window medians: frame **20.675 ms**, pre-render 3.749, views 16.073, view setup
1.621, submission 11.203 and scene end 0.354 ms. The nearby run184 plateau
(frames 4210–5090) has 477 draws / 452 matches and about **19.5 ms/frame**,
versus 20.8 ms from the diagnostic ten-frame stream. These separate flights
support diagnostic-associated overhead, not an exact causal 1.3 ms cost or a
persistent 40-FPS regression. `frame_end dt_ms` spans ten frames here; it is
not a single-frame time.

**R7 closed for this view:** all 2,400 selected light-timer frames are valid,
with 83 traversal calls / 17 us and one cockpit call / 1 us per frame. Reported
self estimate is 17 us/frame. Unknown callers and all selected-window health
counters are zero. Do not subtract the self estimate to claim precise native
cost: the two reported p50s total 18 us and include instrumentation.
They are not a same-frame combined percentile, but are too small to justify
this optimization target. The global reducer's startup `dropped=1` lies outside this selection.

Pass apply/draw/end medians are 3.518/4.815/0.056 ms; residual
prepare/setup/particles/other are 6.488/0.872/0.015/3.159 ms. These nested scopes
are not an additive frame partition. Sparse proxy rows contain TAA 361.1 us,
HDR writeback/meter 444.5/399.5 us, replay/apply 791.3/60.5 us.
For 36 matched HDR rows, the median rowwise writeback-minus-meter is 45.65 us
(range 41.9–67.8 us); meter is nested. The 3.159 ms other scope includes scene
composite, environment-map work, fixups and loop tail. These timings do not
establish new recoverable engine time or justify reopening the rejected state
filter, sorting, pass-replay or instancing patches. Any next proxy change needs
an isolated, feature-equivalent comparison and the existing lifetime proof.

### Argon Prime: stalls also occur with phase diagnostics off

Run183 contains 11 complete >100 ms Argon frames: one `pending_vm`, five
`render`, five `input`. Here `input` contains the sector update, not merely
OS input. Frame8912 takes 617.247 ms, of which 598.308 ms is PendingVm;
render witnesses 9108/9109/9121/9364/9420 take 321–892 ms. Sector-post/input
witnesses 10924/13000/15382/15521/15920 take 247–521 ms. All 187 associated
segment CPU stamps are valid; fully in-scene timer windows have zero clock,
failed-clock, unmatched, overflow, order or dropped errors.

Every render witness overlaps 16 GStreamer critical lines in launcher stderr.
Three later post/input witnesses (10924/13000/15382) overlap eight lines each
and are spaced about 30 seconds apart. This matches the existing failed-cue
retry interval; media windows report repeated ID2 failures and cache refusals.
It is **temporal correlation**, not a measured per-call causal attribution.
PendingVm8912 and post15521/15920 do not overlap those backend errors and remain
unattributed. `video_*` counters are zero; tracing was off, so caller tags and
per-constructor durations are unavailable. Render stalls cannot be assigned to
shader/resource creation from untimestamped or cumulative loader summaries.

Run184 has large stalls despite phase diagnostics being off: its interior
Argon ten-frame spans ending 9150 and 9160 take **1,412 and 1,155 ms**. These
are block durations, not individual-frame latencies or frame-equivalent pairs.
QPC-mapped counter spans contain 32 and 24 GStreamer critical lines,
respectively, so the backend correlation also persists with phase diagnostics
off. Thus the new phase diagnostics are not required for the stall to occur.

The next decisive existing option is `--media-cue-trace` with loop/game phases
in Argon Prime. It can identify ID, caller and constructor duration before any
new hook or cache policy is proposed. Keep retry at 30 seconds for this first
trace; run49B has now been reported. Refusal outcomes can
consume the trace's 32/s outcome limit independently of proceeded-entry logging,
so an entry without an outcome does not itself prove a hang. Check suppressed,
foreign and dropped counters before treating absence as evidence.
A longer existing retry interval can test
periodic retry causality after tracing; it cannot be assumed to solve first-view
render calls or the other three unattributed witnesses. Increasing retry to an
hour also delays recovery for legitimate transient failures; it is not a new
default or a behavior-neutral fix. Keep run49's fog flight
separate from claims that these timing issues are fixed.

Local reproducible evidence: `verification/results/run49a-busy/` and
`verification/results/run49a-stutters/`, each with `reproduce.py`, validated
`result.json` and `result.md`; the original logs remain in `/tmp/x3-bottleX3-run183`
and `run184`. No Wine or game execution was used for this analysis.


## Remaining submission and proxy attribution (2026-09-20)

Independent source/evidence review found that residual `material()` subtracts
the last pass-end clock, which survives view boundaries. During normal
operation it resets at frame discard; a failed pass-end QPC also clears it.
Consequently run49's prepare 6.488 ms can include between-view composite/setup
work. Healthy counters do not prove submission-local coverage, and differences
of marginal medians are not same-frame residual budgets. The previous
sampling-profiler description of prepare as engine-only is corrected.

Three bounded opportunities remain, with no current-flight saving established:

- Existing lazy-RT mode needs a matched production-mode comparison. The fixture
  2.53 µs/depth-draw estimate extrapolates to about 1.15 ms at 454 routed draws
  before displaced work/flushes; it is not a measured flight upper bound.
- HDR meter plus readback records a same-row median 600.0 µs across 36 busy
  samples (meter 399.45 µs, readback 184.25 µs). Readback is already deferred and
  includes transfer, extraction, statistics and adaptation. Transfer alone
  cannot be blamed from this timer.
- Median 364 leased draws across 2,101 rows gives only an estimated 0.182–0.255 ms
  acquisition opportunity from prior fixtures, plus unmeasured retirement.
  Lease retirement occurs after the timed cascade replay. Borrowed unpinned
  references remain unsafe; no lease optimization is ratified.

**Ratified diagnostic scope:** reuse existing view-boundary clocks to separate
within-submission preparation from cross-view gaps; account explicitly for
passes outside submission and compute residuals on each frame before window
reduction. Split HDR readback exhaustively into transfer/lock, extraction/
unlock and statistics/adaptation. Add one timed lease-retirement span/count.
Use existing opt-in diagnostics, fixed counters, no extra per-draw QPC and no
new engine hook site. A two-view host case with a long intervening composite
must prove that composite time cannot inflate submission-local preparation.
This is attribution work, not a promised FPS improvement.

R7, state filtering, sorting, instancing and pass replay remain closed on their
existing evidence. Moving collision and media are separate investigations.
Source-compatible Windows behavior does not establish native execution.
The independent audit and streamed witness are local:
`/tmp/x3-submission-remaining-audit.md`,
`/tmp/x3-submission-remaining-witness.py`; 36 HDR and 2,101 lease/retention rows
reproduced, with unique frames/device 1 and 3,840 metering tiles checked.
Implementation is isolated on `experiment/submission-attribution-2026-09-20`;
no installed candidate change follows from this audit.


## Submission attribution diagnostic implementation (2026-09-20)

View/pass accounting now intersects existing timestamps with cumulative view
submission time, separating cross-view work without another per-draw clock.
Outside-view and crossing passes are explicit, and submission complement is
computed per frame before window statistics. HDR readback has three exhaustive
buckets (transfer/lock, extraction/unlock, statistics/adaptation), adding two
clocks only when timing is enabled. Lease-retirement timing surrounds the
existing release walk without changing ownership.

Independent deep review covers source and evidence. Sixty-four affected host
tests pass; the final estimate-only update has twenty affected tests passing.
The serialized X3 CPU fixture passes 8,881 checks with zero failures. Measured
pass dispatch 101.4 ns and residual two-site average 108.9 ns set estimates 102/109 ns.
These are fixture estimates, not measured flight overhead. The unchanged loop
group measures 136 ns here versus its retained historical 91 ns estimate.
HDR/lease records remain sparsely sampled; they do not partition each 300-frame
window on identical coverage. Completed full host/build qualification is bound in the
[compact record](../../verification/results/submission-attribution-qualification-2026-09-20.json).
No new hook sites, rendering policy or native Windows runtime claim are added.


## Run52: corrected attribution and remaining optimization scope (2026-09-20)

Run187 supplies nine aligned 300-frame windows, ending at frames 3000–5400.
The direct per-frame-before-reduction submission complement has window-median
field 2.825 ms, with scoped pass time 8.389 ms and outside-pass time 0. Zero
scope errors, crossing passes or complement underflows are reported; 90 passes
fall wholly outside submission across these windows. Submission's window-median
field is 11.229 ms. Do not derive a new residual by subtracting these marginal
medians.

Corrected submission-local preparation/setup fields are 1.947 / 0.867 ms; the
separately measured between-preparation complement is 4.518 ms. Thus the old
approximately 6.5 ms preparation figure was not all engine work inside submission.
The new complement does not expose a new independently identified multi-ms
patch target. Existing decisions against sorting, state filtering, instancing,
R7 and pass replay remain; no new engine hook is justified by this flight.

Forty sparse nonzero HDR rows give transfer/lock 154.9 µs, extraction/unlock
8.45 µs and statistics/adaptation 85.15 µs median. Writeback is 448.2 µs median,
with its meter nested. Forty-one lease rows give retirement 63.4 µs median
for 342 records / 1026 references. These CPU spans have different coverage from
phase windows; they are neither GPU times nor additive frame partitions. All
reported clock/error counters in the selected phase, readback and lease rows
are clean. The measured sizes do not justify weakening lease ownership or
changing exposure/readback quality merely to chase the old contaminated total.

The actionable proxy change is the existing lazy-binding mode, now accepted
from the matched production-mode B/C counter; its [decision and exact counts](../verification/motion-output.md#run52-lazy-render-target-binding-accepted-as-launcher-default-2026-09-20)
live in the motion-output ledger. Moving collision and media remain separate
active investigations. No new broad timing flight is requested.

Evidence: [run52-triage](../../verification/results/run52-triage/result.md),
with streamed reproduction, explicit camera bracketing, full B/C option comparison,
aligned diagnostic windows and installed-source dependency checks.
