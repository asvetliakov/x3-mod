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
Stays closed; the *reason* is now measured, not just the bound:
[merged-lod-feasibility.md](merged-lod-feasibility.md) §4 (7 texture stages per
draw and one D3DX-created VB per subset, so at the run240 stand 74.7 % of
consecutive pairs share VS+PS but every draw inside a node has its own buffer
and its own texture set) and §3 (400 of the stand's 448 draws are at LOD 0 on
nodes 2–32 px wide, which is where the remaining asset-side lever is).

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


## Next-flight engine audit: no additional tracing (2026-09-20)

**Parent ratified:** add no engine hook sites, per-draw clocks or performance
sessions before the already queued lattice state observation. Broader engine
changes remain permitted, but Run52's corrected 2.825 ms submission-complement
field and 1.947/0.867 ms preparation/setup fields identify scopes, not removable
work. The 4.518 ms between-preparation complement is not engine-only. These
window-median fields are not additive; sparse HDR/lease rows cannot complete
that partition. Lazy RT's accepted 19.70 → 18.90 ms matched-draw result is already
accounted for, not another available saving. Collision work is paused.

The earlier measured closures remain: R3 technique/End 65/64 us, R5 sort 5 us,
R4 no sampled pressure, R1 inverse 71 us, R8 matrix 19 us (Run181); R7 traversal
83 calls/17 us plus cockpit 1 call/1 us (Run49, including instrumentation).
State filtering, pass replay, instancing, reordering and blanket arithmetic
conversion have no new mechanism or contrary cost evidence. Do not repeat the
FEX-blind leaf sampler. The unchanged R2 tangent-cache candidate is also closed:
isolated commit `09cd76b8` in `/tmp/x3-r2-fov-oracle` records zero qualified
numerical pairs/entry validations and 48 natural controls with zero PE admissions.
Requested SW32 was observed as SW0 by three independent state observations. This
is unsupported input, not cache equivalence or an engine-wide no-PE proof; do
not repeat that oracle or loosen its gate. Local evidence is
`/tmp/x3-r2-fov-runtime-v2/{result,execution}.json`; native Windows remains untested.

Concrete reopening evidence, ranked by useful remaining mechanism:

1. **Repeated view setup/traversal:** establish redundant work with unchanged
   inputs/side effects, or a new measured setup regression. The old nine-view /
   empty-view idea was never a measurement of wasted work. Frame-loop RE shows
   setup performs light/environment selection, viewport/clear and cull/LOD;
   zero submitted draws cannot justify skipping it. If a mechanism is established,
   reuse clocks at `0x0047224c/0x00472270/0x004722c8` for fixed-capacity per-view
   ordinal durations and pass-count deltas, with overflow/outside-scope reporting.
   `frame_phases.cpp::stamp` receives an index, not an engine view pointer. This
   would add fixed stores per view, no new QPC/site or draw-path allocation;
   total diagnostic cost still needs measurement. A two-view host case with a
   long composite and an empty-but-clearing view must preserve scope. This trace
   remains deferred until the prerequisite exists.
2. **Further draw reduction:** identify a substantial currently submitted fully
   invisible population in retained geometry/depth evidence, with preserved
   child traversal, animation and shadow casters. The old 403 sub-2px census
   precedes the accepted 2px default; it is not a remaining saving. Run132's
   coarser-LOD comparison already closed that alternative. No new culling or
   occlusion trace is justified by draw count alone.
3. **Animation update reuse:** `0x004f66e0` occurs in traversal and deferred drain,
   but [the existing RE](../reverse-engineering/shadow-caster-lifetime.md#0-0x004f66e0-is-an-animation-stepper-not-a-visibility-predicate)
   proves record writes, transform updates and possible track/emitter calls;
   its AL result drives caller refresh flags. First prove repeated unchanged
   inputs, all intervening writers and an appreciable cost. Two call sites do
   not establish redundant execution or permit once-per-frame memoization.

No production patch is selected. Any eventual optimization needs unchanged
rendering/side-effect contracts and a measured end-to-end gain after its own
cost. Documented Windows/D3D APIs and validated game sites remain the portability
boundary; FEX timings or source compatibility do not establish native Windows
behavior. The [lattice decision](taa-lattice-crawl.md#25-next-flight-state-decision-and-reopening-gate-2026-09-20)
retains the next discriminating observation. Detailed alternatives and source
mapping remain local in `/tmp/x3-next-flight-engine-lattice-audit.md`; this audit
made no production change and ran no build, Wine command or game.

## Run 238: frame time near the end (2026-09-22)

Session `/tmp/x3-bottleX3-run238/session-20260922-172032-212.log` (Run65
2d11aac4, session C: fog L2 1.0x, `--volumetric-fog-timing --telemetry`, TAA,
HDR, shadows, fog cards). No `--frame-timing`/`--frame-phases`; the log has no
per-draw GPU cost fields (`gate_us`, `route_draw_us` etc. are all 0.0 because
`per_draw=0`). Built a `frame_end` (`dt_ms`, `qpc`) time series: 6370 rows over
~125 s wall.

10 s-window p50/mean dt_ms is flat and healthy (~10-18 ms, i.e. 55-100 fps)
except one single-frame catastrophic stall at wall time t=123.27 s:
`frame_end frame=6224 dt_ms=383`, `frame_end frame=6225 dt_ms=6079`,
`frame_end frame=6226 dt_ms=73`. Frames 6100-6223 (pre) mean 15.8 ms max
23 ms; frames 6227-6369 (post) mean 11.0 ms max 133 ms. This one frame, not a
sustained region, is the entire "low FPS near the end" the user saw.

Cause, from evidence co-timed with frame 6225: `volumetric_fog_sector
frame=6225 reason=no_cockpit sector=00000000 index=-1` and
`volumetric_fog_cache frame=6225 event=epoch reason=sample_gap` (a fog-cache
sector-key jump), plus a burst of `loading_metric` rows inside that interval:
`D3DXCreateMesh` counts 59/333/175/189/230, `D3DXCreateTextureFromFileInMemoryEx`
up to 62-86 MB per burst, `GenerateAdjacency`/`OptimizeInplace` on hundreds of
meshes, hundreds of failing `FindFirstFileA` probes. `motion_output_frame`
frames 6224-6227 show `hook_state=9 hook_outside_scene=1 camera_valid=0`
(engine outside the render scene during this span). This is synchronous
mesh/texture streaming for a sector/geometry change, not fog or shadow render
cost: `sun_shadow_lane_frame`/`shadow_retention_frame` around frame 6223 show
ordinary counts (`us=368.9`, `receiver_draws=277`), and no per-frame field
scales with view direction in this log (camera_rotation_deg is 0 or small
throughout, since per-frame TAA is skipped during the stall).

Open issue: this log alone cannot attribute the 6.08 s stall to a specific
loading call (only bucketed per-second `loading_metric` sums exist, not a
per-call timestamp inside the stalled frame) nor confirm whether the "viewing
station/sector at an angle" framing in the report is this same event or a
separate, uncaptured slow interval. A repeat run should add `--frame-timing
--frame-phases --loading-intervals` (already available in `tools/manage.py`)
to get per-frame engine-phase splits and per-call loading intervals so a
single-frame streaming stall can be distinguished from a genuine per-frame
render-cost region tied to view angle.

## Run 239: frame time with the split (2026-09-22)

Session `/tmp/x3-bottleX3-run239/session-20260922-180304-212.log` (Run65
2d11aac4, session C: fog L2, TAA, HDR, shadows with retention, fog cards,
`--frame-timing --frame-phases --fps-overlay`). `frame_end`: 11449 rows over
204.8 s wall; `frame_timing`/`frame_phases`: 38 windows (300-frame stride).

**10 s-window `frame_end` p50/p95** (session-wide median dt=14.0 ms). Only
the startup window (0-10 s, n=4, includes the 2.3 s/1.0 s init frames)
formally exceeds 1.5x median. The user-reported region is a *sustained*
climb that never crosses the 1.5x-of-global-median bar but is a clear local
plateau: windows 40-100 s hold p50 10-12 ms (~85-100 fps); windows 150-200 s
hold p50 19 ms (~53 fps), 1.6-1.9x the 40-100 s baseline.

**300-frame `frame_timing`/`frame_phases` windows, control vs plateau**
(`frame_timing`/`frame_phases frame=3000` vs `frame=10200`, both `slow=0-1`,
no capture in either window):

| Field | frame=3000 (~100 fps) | frame=10200 (~52 fps) | ratio |
|---|---|---|---|
| `dt_p50_us` | 9981 | 19226 | 1.93x |
| `draws_p50` | 72 (`frame_end` draws=77) | 368 | 4.8-5.1x |
| `pre_render_p50_us` | 3750 | 2661 | 0.71x |
| `views_p50_us` | 5735 | 15727 | 2.74x |
| — `view_setup_p50_us` | 551 | 1302 | 2.36x |
| — `view_submit_p50_us` | 2201 | 10753 | **4.89x** |
| — views residual (particles/TAA/shadow-replay/sun/env-map) | 2983 | 3672 | 1.23x |
| `scene_end_p50_us`+`present_p50_us` | 139 | 182 | 1.31x |
| `draw_p50_us` (per-draw proxy) | 555 | 2990 | 5.4x |
| `draw_native_p50_us` | 208 | 1167 | 5.6x |
| `state_calls_p50` | 3739 | 23043 | 6.2x |

`view_submit`'s +8552 us carries 92 % of the +9245 us `dt` delta. `pre_render`
(script/AI/sim/proxy post-Present) is *lower* at the slow window, ruling out
non-render CPU cost. This is engine draw/state submission scaling with draw
count, not a GPU-bound present (present_p50_us stays 5-6 us both windows).

**Fog** (`volumetric_fog_frame`/`volumetric_fog_cards`, frame=3000 vs 10200):
`cpu_us` 718.3/581.0/544.3 (control) vs 684.6/598.4/634.3 (plateau) — flat,
~0.6-0.7 ms either way, 3-7 % of `dt`. Matches the user's report that toggling
fog did not change the fps: fog is not the cost carrier here.

**Shadow lane / retention** (`sun_shadow_lane_frame`/`shadow_retention_frame`,
frame=3000 vs 10200): `receiver_draws` 45→296 (6.6x), `cutout_opaque_routed`
6→44, `stamped_prims` 0→1052; `shadow_retention_frame` `records` 292→352,
`live_c4` (farthest-cascade retained live casters) **39→241 (6.2x)**, while
retention's own walk cost is *not* the driver (`us` 370.5→186.1, `walk_us`
369.6→185.4 — cheaper at the plateau, since fewer `records_unseen` are
rescanned: 253→111). The 6x growth is in cascade-classified live casters that
get drawn every frame (receiver_draws, part of `view_submit`), not in the
bookkeeping walk. `camera_rotation_deg` is near 0 both windows (0.0931 vs
0.0000): this is a static-camera plateau, not a per-frame pan cost.

**Streaming stalls, separated from the plateau**: nine single frames with
`dt_ms>200` in the whole log: frame 4 (7112 ms, startup load), frame 462
(17542 ms, `volumetric_fog_sector reason=bluewell` sector entry), frame 495
(559 ms), frames 10998-11005 (635-720 ms each, exactly the 8 `capture_event`
frames — an F8 burst inside the plateau, draws=368 there same as neighbors,
so the burst adds ~0.6-0.7 s of capture I/O on top of the plateau but is not
its cause), frame 11339 (438 ms) and frame 11340 (6433 ms,
`volumetric_fog_cache event=epoch reason=sample_gap`, sector exit). These are
discrete streaming events; the 150-200 s plateau itself has no `loading_metric`
correlate and no dt outlier — it is a genuine steady per-frame cost region.

**Conclusion**: yes, an angle/position-dependent *sustained* cost exists
(~1.9x dt, 50 vs 100 fps), and it is not a stutter and not fog. It is carried
by `view_submit` (draw + state submission), driven by a ~5-6x jump in per-frame
draw calls and state calls, itself tracking a 6.2x jump in shadow-retention's
farthest-cascade (`live_c4`) live caster count and shadow-lane
`receiver_draws` — i.e. more casters/receivers submitted into the shadow
cascades from this position than from the control position, independent of
what is visible on screen. This log cannot show whether those `live_c4`
casters are on-screen, occluded, or purely off-screen-but-in-frustum-bounds;
`shadow_retention_frame` has no per-caster screen-visibility field. A repeat
run with a per-cascade draw-count breakdown (main pass vs each shadow cascade
pass, not only the combined `receiver_draws`) and a caster bounding-box dump
at this stand would confirm whether the extra draws are off-screen casters
feeding distant cascades.

## Run 240: cull census at the stand (2026-09-22)

`/tmp/x3-bottleX3-run240/session-20260922-185208-216.log` (Run65 2d11aac4,
session C, `--cull-census --frame-timing --frame-phases --fps-overlay
--frame-timing-state-stamps 8`; F8 burst at the slow stand). Other processes
ran in parallel per the user's warning; ratios and counts are used, not
absolute times. `cull_census_frame` was on for the 8 captured frames
(5783-5790, exactly the F8 burst). `X3M_CULL_SMALL_PARTS_PX=2` was also live
(`cull_small_parts_value px=2 m00=0.79999995 width=1280 threshold=3`;
px = s * m00 * width / 1280 = s * 0.8 here), so the current stand already
culls `s<3` (`culled=404-448`/frame, `cull_small_parts_frame`).

**Census totals, 8 frames** (`grep -c "^cull_census device"` = 3981 entry
rows; per-frame breakdown identical to +-1 in `culled_size`):

| Verdict | count (8 frames) | per frame |
|---|---|---|
| considered (all rows) | 3981 | ~497.6 |
| `culled_size` | 1685 | ~211 |
| `culled_min` | 1232 | 154 (exact) |
| `culled_small` (s<3, current threshold) | 368 | 46 (exact) |
| `kept` (drawn) | 696 | 87 (exact) |

Of the 696 kept rows, 192 (24/frame, exact every frame) carry the exact
sentinel `s=117440512` (0x7000000) with `d` in {0,9,10,...,615}: these are
camera-local objects (models `0000568e`/`00005691`/`00005017`/`00005018`/
`0000568d`/`00005695`/`00005696`/`00005697`) whose engine distance is near
zero, saturating `s`; they are not comparable to a screen-px threshold and
are excluded below. The remaining 504 "normal" kept rows: `s` min 3, median
34, max 2134; `d` min 690, median 701231, max 229355495 (raw engine units,
scale not established in this evidence).

**`--cull-small-parts-px 3` / `4` at this stand**, applied to the 504
normal-drawn rows (px = s*0.8; current threshold already removes s<3):

| Threshold | rows removed (8 fr) | per frame | fraction of normal-drawn | est. ms/frame at 26 us/draw |
|---|---|---|---|---|
| px 3 (s<3.75, i.e. threshold 4) | 24 | 3 | 4.8 % | 0.078 |
| px 4 (s<5, i.e. threshold 5) | 40 | 5 | 7.9 % | 0.13 |

The same 5 (model,node) pairs recur in all 8 frames under px 4 — a stable
stand-local set, not flicker: model `000054f8`/node `381294c0`, model
`00005412`/nodes `31c52ac0` and `31c53240`, model `00005592`/node
`39ba7a20`, model `35ba45c3`/node `50451d20`. This log has no name/class for
these model ids beyond the hex value; confirming what they visually are
needs a model-id-to-asset lookup this session does not have. At 3-5
draws/frame removed, `--cull-small-parts-px 3/4` is not a lever for this
plateau (0.08-0.13 ms/frame against a ~10-14 ms dt gap below); `kept` (87
nodes/frame) is also far below `draws_p50` (448), i.e. each kept node
submits ~5.1 draws on average here — the cost is not concentrated in a few
huge-part-count nodes visible to the census.

**State-call split, `--frame-timing-state-stamps 8`** (`frame_timing`
`frame=1200` control, no capture, vs `frame=6000`, the window containing the
F8 burst; both `dt_p50_us` include parallel-process noise, ratios below do
not):

| Field | frame=1200 (control) | frame=6000 (stand) | ratio |
|---|---|---|---|
| `draws_p50` | 231 | 448 | 1.94x |
| `draw_p50_us` / draw | 7.45 us | 9.55 us | 1.28x |
| `draw_native_p50_us` / draw | 3.03 us | 3.56 us | 1.18x |
| `state_p50_us` / draw | 17.63 us | 20.68 us | 1.17x |
| `state_calls_p50` / draw | 57.3 | 64.3 | 1.12x |
| per state call (`state_p50_us/state_calls_p50`) | 0.308 us | 0.322 us | 1.05x |

Per-call cost is flat (1.05x); the ~21 us/draw state figure at the stand
(20.68 us here) grows almost entirely from more state calls per draw (57→64)
and more draws, not from a per-call slowdown. Using run46D's sampled
hook-self-cost estimate (~0.23 us per stamped call, N=8 stride, no native
per-site instrument in this run): stand self-cost ~ (28810/8)*0.23/448 =
1.85 us/draw; control ~ (13227/8)*0.23/231 = 1.65 us/draw. That leaves
~18.8 us/draw (stand) vs ~16.0 us/draw (control) as native/engine state
submission (`SetSamplerState`/`SetRenderState`, per `state_top`) — the
proxy's own hook overhead is a small, near-constant fraction (~9 %) of the
state-phase cost at both windows; this is an estimate, not a direct
per-site split (no separate native-side state-hook stamp exists in this
log).

**Plateau reproduction**: `frame=1200` (calm, pre-plateau) draws_p50=231,
dt_p50_us=14428; `frame=5700` (plateau, no capture) draws_p50=448,
dt_p50_us=28488; `frame=6000` (the F8-burst window) draws_p50=448,
dt_p50_us=26072 but dt_max_us=896566 and scene_max_us=204798 (one frame
spikes hard, consistent with the user's parallel-load warning and/or capture
I/O). The draw-count ratio (448/231=1.94x) reproduces run239's plateau
signature almost exactly (run239: 368/72=5.1x from a lower control baseline,
same direction); state_calls_p50 also holds flat at 28810 across both
plateau windows (5700 and 6000), i.e. the stand's draw/state load is stable
frame to frame and not an artifact of the capture burst. **Conclusion: the
plateau reproduces** (draws and state-call counts, not just dt) despite the
parallel load; absolute dt at this stand is additionally noisy from that
load and cannot be compared to run239's dt numbers directly.

### Run 242 follow-up (--cull-small-parts-px 4, same stand, 2026-09-22)

(1) `/tmp/x3-bottleX3-run242/session-20260922-190129-216.log`, F8 burst
frames 8352-8359 (window `frame=8400`/`8100`): `cull_small_parts_value px=4
threshold=6` (not 5 — `m00=0.799999952` not exactly 0.8, so
`ceil(4/0.79999995)` rounds up). vs run240 (px=2, threshold=3): `draws_p50`
448→397 (-51, -11.4%), `state_calls_p50` 28810→25962 (-9.9%), `dt_p50_us`
26072/28488→22096 (noisy, parallel load). Census verdicts, 8 frames:
`culled_small` 368→544 (46→68/frame, +22/frame — threshold 6 removes far
more than the 5/frame predicted for threshold 5, because the true threshold
is 6, not 5); `kept` 696→712 (87→89/frame, sentinel-adjusted 63→66/frame) —
essentially flat or slightly higher, not lower, in the 8-frame census sample
even though `draws_p50` (300-frame window) dropped 51. The 8 census frames
and the 300-frame `frame_timing` window are not the same sample (F8 pressed
at a different frame/camera position each run); this data cannot show
whether the drop is from the new threshold or from a slightly different
camera position, only that `culled_small` did rise sharply as predicted in
direction, more than in size.

(2) Draws per kept node: `motion_input` logs one line per actual draw with
`vs`/`ps` hashes, and its count on frame 5783 (run240) is exactly 448 —
matching `draws_p50`. 87 census-kept nodes → 448 draws = 5.1 draws/node
average, but the split is not even multi-pass: top `ps` hashes on frame 5783
are `ca6bfa4a6cca7e2a`=226 draws (50% of the frame, one dominant
material/shader), `8759c7838bbc86c2`=46, `5e0a10fe752b6140`=46,
`fffdabd910793aba`=35, `5f82ecacd39529cd`=28, `6109cf64c03529dd`=14 (the
rest under 12 each). This is one draw per submesh/part under a kept node
(many small parts sharing one dominant hull/opaque shader), not a fixed
N-pass-per-material pattern — no evidence here of separate shadow/reflection
view draws in this count (no `sun_shadow`/env-map per-draw lines match this
frame's `motion_input` index range).

(3) State-call mix per draw at the stand (`frame_timing frame=6000`,
run240, `state_top`, `draws_p50=448`): `set_sampler_state` 32.9/draw,
`set_render_state` 18.8/draw, `set_texture` 4.70/draw, `set_vs`/`set_ps`
1.0/draw each, `set_vs_constant_f` ~1.0/draw, `state_other_p50` 2.91/draw
(SetStreamSource/SetIndices/etc., not itemized). Redundancy
(`state_redundant`/`state_shadowed`, order render-state/sampler-state/
texture per `engine-state-filter.md`): render-state 1255502/1331110=94.3%
redundant, sampler-state 745871/754571=98.8%, texture 213028/631261=33.7% —
matching that note's baseline magnitudes (~94.9/99.3/40.2%), i.e. the
redundancy profile at this stand is not itself unusual.

## Run 245-247: lod-scale A/B at the stand (2026-09-22)

Same stand as run240/242, Run66 DLL 1f9a85f5. `/tmp/x3-bottleX3-run245`
(`--lod-scale 0.25`, session log `session-20260922-200550-212.log`, F8 burst
frames 7843-7850), `/tmp/x3-bottleX3-run246` (`--lod-scale 0.5`, log
`session-20260922-200918-212.log`, F8 frames 8975-8982),
`/tmp/x3-bottleX3-run247` (no `--lod-scale`, log
`session-20260922-201426-216.log`, F8 frames 12083-12090). None of the three
launcher commands carries `--cull-census`; only `object_context` and
`motion_input` are available, so per-node LOD/draw counts come from joining
those two by `(frame, index)`, not from `cull_census` `s`/`d` as in run240/242.

**(1) `lod_scale` line** (only two log lines per run, both before frame
capture):

| Run | requested | applied | proxy_value |
|---|---|---|---|
| 245 (0.25) | 0.25 | 0.25 | 4 |
| 246 (0.5) | 0.5 | 0.5 | 2 |
| 247 (none) | - | - (no `lod_scale` line) | - |

**(2) Per-F8-frame draws/nodes/LOD** (sum or per-frame as noted; join
`object_context`×`motion_input` on `(frame,index)`):

| Run | draws/frame | nodes/frame | LOD0 draws (8fr) | LOD1 | LOD2 | LOD3 |
|---|---|---|---|---|---|---|
| 245 (0.25) | 277 | 59 | 1144 | 440 | 248 | 384 |
| 246 (0.5) | 451 (459 last frame) | 64 (66 last) | 1960 | 1016 | 264 | 376 |
| 247 (none) | 394 | 48 | 2784 | 0 | 360 | 8 |
| 240 (ref, cull2, vanilla) | 448 | 58 | 400 | - | 46 | 2 |
| 242 (ref, cull4) | 397 | 56 | 381 | - | 15 | 1 |

Heavy nodes from run240 (matched by stable `model` id, not node handle, which
is per-process): model `000053a0` (run240's 36-draw/26px node, node
`31689c60`) and model `35ba45c3` (run240's 25-draw/2.4px node, node
`50451d20`):

| Run | model `000053a0` node | draws (8fr) | LOD | model `35ba45c3` node | draws (8fr) | LOD |
|---|---|---|---|---|---|---|
| 245 (0.25) | `316083c8` | 232 (29/fr) | **3** | not present in view | - | - |
| 246 (0.5) | `31646630` | 232 (29/fr) | **3** | `3ac0a048` | 200 (25/fr) | **0** |
| 247 (none) | `3662fc38` | 288 (36/fr) | **0** | not present in view | - | - |

Primitive-count evidence (`motion_route primitives=`, model `000053a0`, 2
matching F8 frames each): run245/246 (LOD3) sum **2550** over 58 draws (avg
44 prims/draw); run247 (LOD0) sum **49724** over 72 draws (avg 691
prims/draw) — a ~19x mesh-detail drop, i.e. this is a visibly coarser mesh,
not just fewer draws.

**(3) `frame_timing`/`frame_phases` nearest the F8 window:**

| Run | frame window | dt_p50_us | draws_p50 | state_calls_p50 | view_submit_p50_us |
|---|---|---|---|---|---|
| 245 (0.25) | 8100 | 17383 | 277 | 17127 | 8832 |
| 246 (0.5) | 9000 | 22164 | 426 | 25527 | 13058 |
| 247 (none) | 12000 | 21661 | 394 | 25454 | 12985 |

Extra ship in run246: models present in run246's F8 frames but absent from
both 245 and 247: `00004f75` (768 draws/8fr, nodes `35d4d2b0` LOD1 512 +
`39b15d98` LOD0 256), `00005531` (264 draws/8fr, LOD2), `35ba45c3` (200,
above), plus 3 negligible models (24 draws total). Total extra-ship draws
~157/frame in the F8 sample (1256/8). Corrected run246 draws_p50 estimate:
426 − 157 ≈ **269**, close to run245's 277 (0.25) and well under run247's 394
(vanilla) — consistent with 0.5 already coarsening the same heavy node as
0.25 does, once the extra ship's draws are backed out.

**(4) Conclusion:** model `000053a0` moved from LOD 0 (vanilla, 36 draws/fr,
691 prims/draw) to LOD 3 (29 draws/fr, 44 prims/draw) at *both* 0.25 and 0.5
— it has a reachable ladder and coarsens well before 0.25; this is a
threshold issue, not a single-LOD-record body, and a factor around 0.5
already lands it at the coarsest rung (no evidence here distinguishes 0.5
from a smaller factor for this specific model — both give the same LOD3
result). Model `35ba45c3` gives only one data point (run246, factor 0.5,
still LOD 0): it is absent from run245's and run247's F8 view (different
camera framing/extra-ship timing), so this evidence cannot say whether 0.25
or 0.5 would move it off LOD 0 — a repeat capture with `35ba45c3` in view at
both factors is the needed follow-up, not inferable from these three runs.
The `draws_p50` comparison is confounded by (a) the extra ship in run246 and
(b) the F8 8-frame sample not matching the 300-frame `frame_timing` window
(same caveat as run242); the corrected estimate above removes (a) but not
(b).

## Run 248: draw accounting at the stand (2026-09-22)

Run66 DLL 1f9a85f5, `/tmp/x3-bottleX3-run248/session-20260922-202645-472.log`
(`--lod-scale 0.5 --cull-small-parts 4 --frame-timing --frame-phases
--fps-overlay`; no `--cull-census`, so per-node data comes from joining
`object_context`×`motion_input`×`draw` on `(frame,index)`, as in run245-247,
not from census `s`/verdict rows). Three F8 bursts, exact per-frame draw
counts from `grep -c "^draw .*frame=<n> "`: frames 8055-8062 and 9872-9879
both **367 draws/frame** (`frame_timing` window `dt_p50_us=21174/20934`,
~47.3/47.8 fps — the reported stand), frames 14565-14572 **415 draws/frame**
(`dt_p50_us=23895`, ~41.8 fps, busier but not the 987-draw run31-33 view,
which is not reproduced in this capture).

**Bucket table, frame 8055 (stand, representative; camera-local model ids
from run240's sentinel list `0000568e/91/00005017/18/68d/95/96/97`):**

| Bucket | draws | % of 367 | prims | note |
|---|---|---|---|---|
| (a) camera-local | 13 | 3.5% | 4752 | 7 of 8 sentinel models present; node `3b5ca538`/model `00005018` draws 4x with distinct `vb`/`ib` (406/408/410/412) = 4 real submesh parts, not a duplicate pass |
| (b) frustum-outside, drawn | n/a | n/a | n/a | not measurable: no per-object world bounds decode this run (`object_position` is packed bits, scale unestablished per run240) |
| (c) occluded, drawn | n/a | n/a | n/a | same gap — no bounds-to-depth projection possible from this log |
| (d) 4-8px census survivors | n/a | n/a | n/a | requires `--cull-census`, absent this run |
| (e) draws with <20 prims | 70 | 19.1% | 502 (0.13% of frame's 385901 total) | dominated by model `000053a0` (15 of its 29 LOD3 draws) — same model run245-247 documented moving to LOD3's coarse, many-tiny-part mesh |
| (f) repeated same-node draws (multi-pass) | 0 | - | - | checked node `3b5ca538` 4x-repeat: distinct vb/ib per draw, same `vs`/`ps` — real submesh split, not a pass duplicate; `scene_end_marker`=1/frame confirms one view/scene, no second (env-map/HUD) pass |
| (g) remainder (normal visible geometry) | ~354 | ~96.5% | ~381k | top models: `0000552a` 37 draws/10812 prims, `00005411` 35/107397, `00005428` 34/52225, `000053aa` 33/108429, `00004f75` 32/18075, `00004f72` 31/15284 |

**Busy frame 14565** (415 draws): camera-local 7 draws, tiny(<20 prim) 59
draws, heaviest single model `000053b8` 68 draws/147498 prims (a body not
present in the stand frames — different view, not a duplication artifact).

**Cost estimate** at this run's measured `gap_draw_per_draw_us=27.2` (stand):
367 draws ≈ 9.98 ms of the 21.17 ms `dt_p50`. Camera-local (a) ≈ 0.35 ms;
tiny-prim (e) ≈ 1.90 ms in per-draw overhead despite negligible (0.13%)
primitive cost — the waste here is draw-call count, not vertex work, and is
not addressable by the existing per-node size-cull site (px 4 already active;
these 70 draws already passed it). Buckets (b)/(c)/(d) cannot be sized from
this evidence.

**Conclusion:** no evidence of duplicate/multi-pass submission or a second
scene view inflating the 367; camera-local (cockpit/ship) draws are a small
4% slice. The measurable waste is bucket (e) (19% of draws, <20 prims each,
concentrated in one LOD3 model's shattered mesh) — an instancing/merge
candidate, engine-only, not reachable from the proxy's current px-size cull
site. Buckets (b) frustum-outside and (c) occluded cannot be answered without
new instrumentation: **one launch** with `--cull-census` (per-node `s`/verdict)
plus a logged world-space AABB (min/max) per `object_context` row, so bounds
can be projected against the frustum and against the `depth_` readback on the
same stand, would settle both.

## Run 250: draw accounting at the stand and the busy view (2026-09-22)

Run 67 session B, Run67 DLL `621cad63…`, `/tmp/x3-bottleX3-run250/session-20260922-230123-216.log`
(`--lod-scale 0.5 --object-bounds-log --cull-small-parts 4 --frame-timing --frame-phases`, fog on).
[M] = measured by the named script, [I] = inferred; scripts and outputs in
`verification/results/run250-draws/`. The log's `lod_scale_value` row reads `applied=0.5` [M]
(`log_facts.py`); the heavy station body `000053a0` draws at LOD 3 [M] (`no_box_census_out.txt`,
`lod=00000003`; its identity as the heavy body is from the Run 245-247 section). Two F8 bursts: frame 7357,
the stand, **391 draws**, and frame 10550, the busy view, **416 draws** [M] (`draw_accounting.py` header).
The stand is not run248's exact position: the view translation differs by 8,570 units between run248 frame
8055 and run250 frame 7357 [M] (`log_facts.py`); the eye distance depends on the view-matrix convention
(1,566 or 14,246 units) and is not established.

**Buckets** [M] (`tools/analysis/draw_accounting.py`, saved as `draw_accounting_7357.txt` and
`draw_accounting_10550.txt`; ms at the tool's whole-frame rate `dt_p50 / draws_p50` = 55.61 / 57.74 us):

| bucket | 7357 draws | prims | ms | covered | 10550 draws | prims | ms | covered |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| offscreen | 0 | 0 | 0.00 | - | 12 | 13,196 | 0.69 | 0.00 |
| occluded | 25 | 2,398 | 1.39 | 1.00 | 1 | 194 | 0.06 | 1.00 |
| tiny | 13 | 339 | 0.72 | 0.03 | 34 | 2,070 | 1.96 | 0.03 |
| partial | 160 | 187,357 | 8.90 | 0.49 | 83 | 161,284 | 4.79 | 0.19 |
| visible | 99 | 169,580 | 5.51 | 0.00 | 205 | 299,598 | 11.84 | 0.00 |
| no_box | 94 | 49,728 | 5.23 | - | 81 | 21,953 | 4.68 | - |

**`no_box`** [M] (`no_box_census.py`), 7357 / 10550: 55 / 55 alpha-tested routed draws (`atest=1`, gate 0:
routed, but the run250 log has no box for alpha-tested draws), 25 / 21 without a `motion_route` row, 14 / 5
unrouted (gate 3 or 4), 8 / 7 unscoped (`model=0`, `scoped=0`); every draw with a box is routed.

**Scene difference** [M] (`model_diff.py`): against run248's stand (frame 8055, 367 draws) the changes are
per-model swaps of small ships, `00004f72` +62, `00004f76` +32, `0000552a` -37, `00004f75` -32 and single
draws; against run246 frame 8975 (451 draws) the same kind, `00004f75` -96, `00004f72` +93, `00005531` -33,
`00004f76` +32, `35ba45c3` -25, `00005529` -16. The station set is unchanged; the differences are traffic [I].

**Cost** [I: products of the rows above]. At the per-draw submission gap (`gap_draw_p50 / draws_p50` of the
enclosing `frame_timing` window, 10,671 / 391 = 27.29 us and 11,817 / 415 = 28.47 us [M],
`verification/results/run251-fog-shadow/fog_cost_retention.py`, run250 section; run248's stand 27.2 us, Run
248 section) the cullable buckets (offscreen + occluded + tiny) are 38 / 47 draws = **1.04 / 1.34 ms**
(2.11 / 2.71 ms at the tool's whole-frame rate), against visible + partial 14.4 / 16.6 ms at the tool's rate.
Timing windows [M] (`timing_windows.py`): 6900-7800 `dt_p50` 21.6-21.9 ms, `view_submit_p50` 12.4-12.9 ms,
391 draws; 10200-11100 `dt_p50` 23.0-24.6 ms, `view_submit_p50` 13.4-14.4 ms, 415-416 draws.

**Conclusion.** No cullable bucket justifies the next per-node cull: at most 1.3 ms of submission, most of it
in `tiny`, which the existing `--cull-small-parts` site already sees. The 55 alpha-tested draws per frame are
the largest unmeasured group; the extended `--object-bounds-log` (Object bounds log below, `alpha_tested=1`
rows) logs their boxes and needs one flight.

## Object bounds log (2026-09-22)

Sizing the two buckets the draw accounting could not size — drawn outside the
view frustum, and drawn while fully occluded by nearer geometry (X3 has no
occlusion culling) — needs per-draw bounds, which no log line carried. The
`--object-bounds-log` launcher option (`X3M_OBJECT_BOUNDS_LOG=1`, launch only,
default off) adds them on F8 capture frames only:

```
object_bounds device= frame= index= node= model= sx0= sy0= sx1= sy1= zmin= zmax= inside= [offscreen=1] [near=1] [alpha_tested=1] [stale=1]
```

One line per routed draw whose object box the caster-candidate route already
computed for its own verdict, so nothing transforms geometry a second time:
`src/renderer/object_bounds_projection.h` (header-only, no D3D types) puts the
box's eight corners through the draw's own clip rows — the rows the same-draw
motion output latches — and reports

* `sx0..sy1`: the projected screen box in pixels of the routed target (the size
  the `depth_` readback is dumped at), clipped to it; `offscreen=1` instead when
  the clipped box is empty;
* `zmin`/`zmax`: the box's device depth (`clip.z / clip.w`) range;
* `inside`: how many of the eight corners are inside all six frustum planes;
* `near=1`: the box straddles the eye plane, so its projection is unbounded —
  the row then carries the whole viewport and `zmin` 0.
* `alpha_tested=1`: an alpha-tested routed draw. The candidate route never
  computes a box for these (they are not casters), so on a diagnostic run their
  missing extents are collected on every frame and queued only after the
  frame's caster reads, into the slots (of 32) and bytes (of 1 MiB) the casters
  left; a caster read is never displaced or demoted. These reads are counted in
  the candidate summary's `reads=` and in `extent_refused`, so those counters
  are not comparable with a run without the option. The box is logged on the
  capture frame only and never feeds a verdict. An alpha-tested draw whose
  extent was not read in time has no row and stays in `no_box`, so the census
  must still account for alpha draws without a row. Rows written before this
  field existed (run250 and earlier) have no alpha-tested rows.
* `stale=1` (after `alpha_tested=1`): the box is an earlier buffer revision's
  extent (also one whose re-read was abandoned), not the draw's current one.

Prerequisites, enforced by the launcher and re-checked in the DLL (one
`object_bounds_mode` line records both): `--object-trace`, because `node=` and
`model=` are the verified submission scope's, and `--shadow-replay-candidates`
or `--shadow-replay-depth`, because the object box is that route's. `index=` is
the frame's draw index, joinable with `draw`, `object_context` and
`motion_route`. Off, the cost is one bool test on the box path, one on
the path of draws the candidate gate refuses and one per scene end; nothing is
patched, queued or written.

`tools/analysis/draw_accounting.py <run dir>` buckets one frame: it joins the
`object_bounds`, `draw` and `object_context` rows with that frame's
`depth_<device>_<frame>.r32f`/`.rgba32f` readback (device depth in `.r`, `-1`
where no routed draw covered the pixel) and reports draws, primitives and
milliseconds at the frame's measured per-draw cost (`frame_timing`
`dt_p50_us / draws_p50`), plus a per-node table. Buckets, in priority order:
`offscreen`, `occluded` (every sampled pixel of the box carries depth closer
than `zmin` by `--margin`), `tiny` (box area below `--tiny-px`, default 16 px²),
`partial` (some pixels covered; the covered fraction is reported), `visible`,
and `no_box` for the frame's remaining draws (unrouted, or routed without a
known box) so the buckets sum to the frame's own draw count. The header's
`alpha_tested=` and `stale=` count the frame's rows carrying those marks; a
stale box can put a draw in the wrong bucket. Sampling is capped
at `--max-samples` per box. Host coverage:
`verification/analysis/test_object_bounds_log.py` (projection core against known
matrices, the wiring and the launcher gate) and
`verification/analysis/test_draw_accounting.py` (synthetic log and depth image).

## Run 255: stand census with ladder, body and screen size (2026-09-23)

Run 68 B (`/tmp/x3-bottleX3-run255`, Run68 DLL `39c8c70d…`): the run248 stand set with
`--cull-census --object-bounds-log` and vanilla LOD (no `--lod-scale`). Outputs under
`verification/results/run255-census/` (`draw_accounting*.txt`, `acc_*`/`ladder_*` per frame,
`node_census.py`, `s_range.sh`). All measured unless marked; the eight frames of each burst
are identical (the game was probably paused).

- **Burst 2 is the run248/run250 stand** (same bodies): 466 draws per frame at 24.0 ms
  (77 alpha-tested; boxes 0 offscreen / 70 occluded / 18 tiny / 189 partial / 148 visible /
  41 no_box) against run250's 391 draws at 21.7 ms, which ran at lod-scale 0.5, so the +75
  draws cross a LOD-scale change (inferred; e.g. `argon_trading_station_partB` 26 → 36).
  Burst 1 (fighter save, run250's 10550 view) 447 draws at 24.2–24.8 ms, 68 alpha-tested.
- **Who draws:** about twelve bodies at 31–37 draws each carry ~390 of the 466 draws, at
  screen size s 11–80 (spacedock 1154), nearly all at LOD 0: Argon_m7m 37 (s 11, LOD 1),
  trading_station_partB 36 (s 34), tech_M_laser_cc 36 (s 21), spacedock 35, equipmentdock 33
  (s 80), trading_station_partA 32 (s 22), argon_M2 32 (s 30), argon_M1 32 (s 45),
  argon_TL ×2 31 (s 29, 39). Ladders are 100000,30,15,5 for the ships; the census LOD
  matched `object_context` on every node, consistent with the Very High rule at f = 1.
- **Alpha-tested draws** 68 / 77 per frame, four pixel shaders (`5e0a10fe` 47 at the stand);
  they exceed opaque draws only on tech_L_missile_C (9 vs 6) and tech_L_shield_F (8 vs 7).
- **Pilot placement finding:** with the Very High −1 rule a record is drawn when the record
  *after* it admits s, so the overlay's "before-last with T = T_last" placement draws the
  coarse record only below T_last (5 px ships, 15 px outpost), never at the stand. The pad
  placement (coarse record plus a never-drawn pad whose threshold T_pad is the switch size)
  replaces it ([lod-selection.md](../reverse-engineering/lod-selection.md)). Stand s of the
  pilot bodies: argon_TL 28–39, argon_M2 30–43, argon_M1 21–45, military_outpost_middleb 95.
  **Pilot thresholds chosen: T_pad 50 for the three ships, 100 for the outpost.** Expected
  saving at the stand: the four ship nodes' 126 draws become 4 (inferred).
- Anomalies: none (census overflow 0, no stale bounds rows, no device rows); a node with
  s = 117440512 is the offscreen sentinel (inferred).

## Run 257: merged-LOD pilot in flight (2026-09-23)

Run 69 A (`/tmp/x3-bottleX3-run257`, Run68 DLL, the run255 options, pilot overlay
`addon/05.cat` `c5737a0a…` with the two-group collapse). Scripts and outputs:
`verification/results/run257-pilot/`. All measured unless marked.

- **The overlay works as designed:** argon_M1 ×2 (s 21), argon_M2 (s 41) and argon_TL (s 28)
  drew LOD 4 at 2 draws each, the outpost (s 95) LOD 3 at 2 draws; at s 56 the M2 was back at
  LOD 0 with 32 draws (the 50 px switch). Fighter view 297 draws per frame against 447 in
  run255 (only the four pilot bodies changed: M1 64→4, outpost 68→36, M2 32→2, TL 31→2).
  Alpha-tested draws 48 / 38 against 68. Census overflow 0, no stale rows, no device rows.
- **User verdict:** stations and ships look fine, but the ships' engine glows vanish while the
  coarse record is drawn (the Titan/M2 at 41 px, back at 56 px). No child node went missing
  (`body_diff_out.txt`); the glow is the light map (stage 3) of the `exhaust` materials,
  dropped by the collapse onto one material (`ship_draws_*.txt`, `tex_*.txt`). Fix: the glow
  collapse rule ([merged-lod-feasibility.md](merged-lod-feasibility.md), "Overlay tooling").
- **Frame time is not comparable in burst 1:** 32–36 ms at 297 draws, but every phase and
  every fixed fullscreen pass slowed alike (HDR writeback 109→589 µs, TAA 0.80→2.23 ms) in
  two spans that start to the second with the orchestrator's host suite and `-j8` build
  during the flight (`slowdown_timeline_run257_out.txt`). Uncontended frames 2520–2819 at
  ~298 draws ran 20–21 ms against run255's 24–25 ms at 447 draws, consistent with the
  27 µs/draw slope (inferred). `footprint_refused4=19` is a value, not a count (mean 15.9 vs
  14.6 per frame). Rule: no host suite or build while the game is up.

## Run 258: glow collapse in flight (2026-09-23)

Run 69 B (`/tmp/x3-bottleX3-run258`, Run68 DLL, overlay `def76feb…` with the glow collapse,
pad placement, 50 / 100 px). Outputs: `verification/results/run258-glow/`. All measured.
The user: "I see glow now"; asks for 80 / 150.

- Fighter view (f6814): 272 draws at 18 ms p50 uncontended (run257 297 / 20 ms, run255
  447 / 24 ms; Argon_m7m with 38 draws left the view, the pilot bodies gained 13). Pilot nodes
  M2 (s 30), M1 ×2 (s 25, 21), TL (s 24) at LOD 4 with 5 draws each; outpost (s 96) at LOD 3
  with 3 draws, its lattice draw alpha-tested. The glow draws bind the same texture sets as
  the same materials' LOD 0 draws (M2 5/5, M1 9/10 matched by `stage_match.py`), on the
  DEFAULT-technique shader `8759c7838bbc86c2` (its normal sampler layout: diffuse, specular,
  light map on s2, cube; the run258 triage's "stages shifted" reading was an off-by-one draw
  in its script, corrected in `run259-lighting/draw_state.py`), so the DEFAULT technique
  samples the light map. Timeline flat 16.8–20.8 ms.
- Burst 2 had the M2 at s 183 and the M1 at 63, both LOD 0 (32 draws), so only burst 1 tests
  the overlay. No census overflow, no stale rows, no device rows.
- **Installed next (compact placement, 80 / 150):** the coarse record is LOD 1, the pad LOD 2,
  the original records 1–3 dropped (unreachable below T_pad); the 0x100000 flag is never set,
  so the bump-mapped technique applies. Run 69 C.

## Run 259 / 260: compact placement at 80 / 150 px, the lighting question (2026-09-23)

Run 69 C (`/tmp/x3-bottleX3-run259` fighter view with a paired outpost capture across the
switch, `/tmp/x3-bottleX3-run260` Argon Prime; Run68 DLL; overlay `cf6fd61e…`, compact
placement, glow collapse). Outputs: `verification/results/run259-lighting/`. All measured
unless marked. The user: 80 / 150 px acceptable, but the coarse model "stops receiving sun
lighting" (also true of Run 69 B, unreported then).

- The coarse draws now run the BUMPMAP technique (`4944d81d` / `ca6bfa4a`, flags130 0x40),
  run258's the DEFAULT one; both were judged unlit, so the technique is not the cause. Every
  record carries 38-byte points with unit normals plus per-point tangent/binormal records
  (declaration `96b83ce5`, stride 40, same at both levels); sun constants (c4/c5) identical
  to the fine draws; the sun-shadow lane and the light-map gain cover all five programs.
- **Outpost pair, same view:** the sun part follows the normal on both models (correlation
  0.39 vs 0.37 with max(n·l,0)) but is 23 % weaker on the coarse one (0.082 vs 0.107), and the
  non-sun part (light-map self-illumination, ×4 by the gain) drops 72 % (0.0060 vs 0.0213).
  Cause: the collapse paints every non-exhaust face with the dominant material, which has no
  light map and the lowest diffuse strength (`g_MatDiffuseStrength` 0.40 vs a triangle-
  weighted 0.56; the −29 % it predicts matches the −23 %, inferred). At level 0 about 39 % of
  a ship's hull area has a real light map; in C only the exhausts (1.6–5.7 %).
- **Next:** an area-ranked light-map rule (keep the largest lit materials up to P % of the
  lit area) and a synthesized dominant material with area-weighted lighting scalars
  ([merged-lod-feasibility.md](merged-lod-feasibility.md)); Run 69 B's `stage_match` texture
  rows were one draw off (corrected by `draw_state.py`), the glow-group match still holds.

## Run 261: area-ranked light maps and the synthesized material (2026-09-23)

Run 69 D (`/tmp/x3-bottleX3-run261`, Run68 DLL, overlay `87bf16cd…`: compact placement,
80 / 150 px, `--collapse glow-area 70`, synthesized outpost material). Outputs:
`verification/results/run261-area70/`. All measured. The user: the lighting difference at
the switch is "less visible now".

- Pilot nodes at LOD 1 with the expected draws: TL 9, M2 8, M1 ×2 9, outpost 10 (3 alpha);
  the outpost at s 155 drew LOD 0 (34 draws). No census overflow, no stale rows.
- **Outpost pair at an unchanged view (5564 coarse, 6392 fine):** sun part 0.103 vs 0.110
  (94 %, was 77 % in run259), non-sun part 0.0151 vs 0.0208 (73 %, was 29 %),
  facing/averted 1.93 vs 1.51. The collapsed draw uploads `g_MatDiffuseStrength` 0.499
  (was 0.400), so the synthesized material is in effect.
- Remaining gap: the coarse outpost's non-sun part is still 27 % lower and its sun part 6 %
  lower; no ship pair across 80 px was captured. Next: the atlas collapse (one material with
  baked diffuse and light-map atlases, [merged-lod-feasibility.md](merged-lod-feasibility.md)).
- Frame time 20.0 ms at 310 draws vs run259's 18.5 ms at 262 draws is a different scene, not
  an A/B (inferred).

## Run 265: atlas overlay in flight (2026-09-23)

Run 70 C (`/tmp/x3-bottleX3-run265`, Run69 DLL, atlas overlay `f3f607fa…` installed 05:08 with
the game closed, session 05:20). Outputs: `verification/results/run265-atlas/`. All measured
unless marked. The user: still sees a transition at the switch; asks whether the atlas was in.

- **In effect:** outpost at s 149 LOD 1 with 2 draws (atlas + alpha), at 151 LOD 0 with 34;
  Titan (M2) at s 73 LOD 1 with 1 draw, at 90 LOD 0 with 32; TL and M1 ×2 at LOD 1 with 1 draw.
  Coarse draws bind s0 diffuse DXT1, s1 bump DXT5, s3 light DXT5 at 2048² (TL, outpost) or
  1024² (M2, M1) with full mip chains; s2 (specular) is id 537, the engine's 32×32 NULL-specular
  stand-in (also bound by argon_spacedock LOD 0). No texture failure or mip rows. The resource
  reader logs no file names, so the bindings are the proof.
- **Outpost pair, same view:** sun part 0.111 vs 0.111 (100 %; run261 94 %), non-sun 0.0206 vs
  0.0220 (94 %; run261 73 %), mean luminance 0.132 vs 0.133. Per-pixel: median ratio 0.999;
  79 % of the difference energy is fine-scale; highlights (6 % of pixels) carry 20 % of it at
  ×1.22; plating ×0.95; windows/exhausts ×0.94; specular-zone luminance ratio 2.52 vs 2.08.
  Titan pair (unequal views, camera moved ~11.5 km): mean ×1.77, highlights ×2.87, specular
  zone 2.35 vs 1.92. Pixel-shader constants match (c9 2.84 vs 2.90, c12 0.524 vs 0.562), so the
  shine comes from the s2 placeholder (inferred, its texels are not logged).
- **Next (C2):** the overlay rebuilt with `--atlas-specular` (DXT5 specular atlas per body;
  16 textures, 19.3 MB) and installed.

## Run 268: atlas overlay with the specular atlas (2026-09-23)

Run 70 C2 (`/tmp/x3-bottleX3-run268`, Run69 DLL, overlay `7a060de7…` with diffuse/light/bump/
specular atlases). Outputs: `verification/results/run268-atlas-spec/`. All measured unless
marked. The user: "better now?", still some visible transition, acceptable if nothing is wrong.

- **Specular fixed:** the Titan's and M1's coarse draws bind s2 = the 1024² DXT5 specular atlas
  with full mips; 0 of 104 draws bind the placeholder; highlights ×1.07 on 0.2 % of pixels.
  The outpost stayed at LOD 0 in both captures (s 174 / 185), so no outpost pair this run.
- **Titan pair (not a fixed view: camera 10° / 11.9 km apart; the outpost served as a control
  for the view change: mean ×0.987, lit windows ×1.005):** mean ×1.33; lit windows and
  exhausts ×1.51 on 36 % of pixels carrying 59 % of the difference (non-sun 0.287 vs 0.126,
  glow area ×1.56, total ×1.29); plating ×0.80; silhouette IoU 0.79. Patch-scale change
  0.079 display units against 0.027 for the control and 0.007–0.012 frame noise: the LOD step
  is ~6–10× the noise (inferred).
- **Cause (inferred):** cross-tile bleed in the light atlas's mip chain (plain box filter over
  the whole atlas, 4-texel gutter, so ≤ 1 texel from mip 2 on, which is the level sampled at
  2–4 texels/px). Fix in progress: tile-aware mips and an 8-texel gutter, plus the LOD 0
  geometry as the coarse record's source ([merged-lod-feasibility.md](merged-lod-feasibility.md)).
- No anomalies (census overflow 0, stale 0, no texture failures).
