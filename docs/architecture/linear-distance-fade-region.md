# Distance-fade region composition (prototype 2)

Design note, 2026-09-14, for ratification. Owner of the region decision;
[linear-distance-fade.md](linear-distance-fade.md) owns the fade route and its
measured prototype-1 costs, [render-node-bounds.md](../reverse-engineering/render-node-bounds.md)
owns the bound. Nothing here is implemented; no Wine, build or game was run.

## Decision

Compose the fade **in place, inside a conservative pixel rectangle**: back up
the rectangle of A into B, run the native source into **A itself** (with E and
M as MRT 1/2 as today), then composite the rectangle back into A from B and E.
The rectangle is the eight corners of the owning mesh part's object-space AABB
projected through the rows the draw actually uses, expanded by one pixel and
intersected with viewport and scissor. Anything unknown selects the full
viewport, which costs exactly prototype 1.

The approved shape in the resume note (three-output preparation B=A, E=0, C=A,
region composite into C, owning-slot exchange) is rejected on arithmetic: it
keeps a full-viewport pass in every bracket (`32 + 24f` bytes/pixel) and is
*worse* than prototype 1 (48) whenever the bound is unknown or `f > 2/3`. The
in-place shape costs `48f` and never exceeds prototype 1. It also fails softer:
a non-conservative rectangle leaves native pixels outside it, whereas the C
shape would leave the object *missing* there (C outside the region is A copied
before the source). See *Alternatives*.

## 1. Obtaining the bound at the draw

Settled by the RE note: `part = *(descriptor + 0)` where `descriptor` is the
first stack argument of `0x004c0150`; `part+0x40/44/48` centre ×4 and
`part+0x50/54/58` half-extent ×4 as int32, `/65536` in POSITION0 units; the
draw at `0x004c403c` covers one whole subgroup, so the part AABB is a superset.

Hook site: **the existing `0x004c5228` seam** in `src/proxy/object_trace.cpp`.
Its TLS scope already holds `args[0]` (the descriptor). `object_trace` gains
one accessor returning `args[0]` and the scope depth without any memory read;
no new patch site, no new SEH analysis. Reads go through `engine_memory::read`
(validated, region-cached VirtualQuery, `rep movsb`, no XMM), the path the
route already uses per draw.

Per admitted fade DIP (after the existing refusals 0–4 in
`MotionOutput::prepare_composition`, before `prepare()`):

1. Look up the stream-0 VB allocation id (`shadow_.stream0`) in a fixed-capacity
   open-addressed table reserved at attach (1024 entries; no allocation after
   attach; table full → full viewport, counted). Entry: VB id, IB id,
   descriptor, part, first-seen VB and IB revisions, six floats (centre,
   half-extent already expanded, see 2), poisoned flag.
2. Hit: require IB id equal, scope descriptor equal to the entry, and both
   `get_buffer_content_view` results `known && !ambiguous && pending_locks == 0
   && revision == first-seen`. Any mismatch **poisons the entry permanently**
   (allocation ids never recur; Reset/teardown drops the table) and selects the
   full viewport. This is the relock gate: the AABB was computed at load from
   the CPU vertex array, so any observed write after first sight means the VB
   may no longer be bounded by it, and re-reading the same AABB cannot repair
   that. `track_buffer_writes` is already forced on when object trace is active
   (`loader.cpp:178`).
3. Miss: read `part` (4 B), `part+0x40..0x5b` (28 B), `part+0x64` (4 B) and
   require `part+0x64 == descriptor` (back-link consistency). Then walk the
   descriptor's subset records (`+0x08` count, `+0x0c` array, stride `0x1a8`,
   cap 16) and require some record's `+0x0c`/`+0x10` to map through
   `capture_state::resource_id` to the bound stream-0 VB and IB ids. This
   proves the scoped descriptor owns the buffers being drawn. Insert the entry
   with the current revisions. Any failed read, mismatch or cap → full viewport.

No scope (object trace off, depth 0) → full viewport. The table is only ever
populated from a verified scope, so the "buffer identity" route of the RE note
serves cache hits, never learning. Per-draw hot-path cost: one table probe, two
`get_buffer_content_view` calls (a recursive mutex, a map find and a metadata
read — to be measured in step 1; run27 shows one fade candidate per captured
frame and the live fixture's 16-DIP batch is the stress case), and on a miss
three to five validated reads.
No shader lookup, no allocation, no floating-point work before admission.

## 2. Projection

Rows: `shadow_.rows[window_of(vs_row->matrix_register)]` — c24–c27 for all six
Asteroid VS (`rigid-position-profiles.md`, `constant-uploads.md`
`g_mWorldViewProjection` c24–27; the row's `matrix_register` generalises).
`apply_jitter` runs in `evaluate_draw` **before** `prepare_composition` and
writes `row0 + jx·row3, row1 + jy·row3` to the device while the shadow keeps the
unjittered rows. Project with the rows the draw uses: apply the same jitter
term when `route.jittered`, so jitter needs no separate margin. `rows_known`
false → full viewport.

Half-extent expansion: the VB stores POSITION0 as FLOAT16_4 while the AABB was
accumulated from the int16 source values; a half-float rounding of a value in
`[1, 2]` can exceed the int16 bound by at most one ULP (`2^-10`). Expand each
half-extent by `2^-10` POSITION units before projecting; this covers nearest or
truncating conversion for the whole `|p| ≤ 2` domain. The exact rounding at
`0x004bbb10`/`0x004bc1c0` is an open item (below) but cannot exceed this.

For the eight corners `(cx ± hx, cy ± hy, cz ± hz, 1)`, in double precision:
`clip = (r0·p, r1·p, r2·p, r3·p)`. If any `w ≤ 0` or any value is nonfinite →
full viewport (the projected hull of a box is convex only when `w > 0` at every
corner; a near-plane crossing with `w > 0` is fine, `z` is not needed).
Otherwise `sx = X + (x/w + 1)·W/2`, `sy = Y + (1 − y/w)·H/2` using the
application viewport (`shadow_.viewport`, required known, and re-checked
against `saved.viewport` inside the pass). Rectangle
`[floor(min sx) − 1, ceil(max sx) + 1] × [floor(min sy) − 1, ceil(max sy) + 1]`:
the one-pixel margin covers the D3D9 integer pixel-centre convention, the
top-left rule and float32 rounding of the GPU's dot products (relative 1e-6 of
w, far below a pixel). Intersect with the viewport rectangle, with the
application scissor rectangle when `SCISSORTESTENABLE` is set, and with the
target. Empty → a 1×1 rectangle at the viewport origin (compositing an untouched
pixel is an exact no-op, and the path stays uniform). Additionally require
`FILLMODE == SOLID` (a line or point fill could exceed the hull); MSAA is
already refused. The guard band and user clip planes only remove coverage.
Anisotropic sampling is irrelevant; M is written per rasterised pixel, so the
rectangle must contain the raster footprint, which the hull argument gives.

Sanity bound from run27 (`/tmp/x3-run27-asteroid-constants.json`, far draw
`1af31630/4fee`): `w` at the node origin is 254,094; row lengths 14,047 (x),
23,412 (y). Projecting the **largest possible** POSITION0 box (`|p| ≤ 2`, the
int16/16384 encoding limit) through those rows gives 20.2% × 28.7% of the
viewport (387×310 px at 1080p, `f ≤ 0.058`) with all eight `w` in
212,089–296,099; a unit box gives `f = 0.0137`, a half-unit box `f = 0.0034`.
The real extents of model `4fee` were not captured. Because fade admission
happens only at node distance ≥ N (≈ 250,000 units here, matching this draw's
`w`), a faded asteroid at a given node scale is never larger on screen than at
this threshold.

## 3. Pass shape: in-place region bracket

New policy bit `DistanceFadeInPlace = 4` in `LinearEmissionPass`, sharing the
B/E/C/M pool and the existing programs; additive emission and the prototype-1
exchange path are unchanged (the exchange path remains the fixture reference).
Boundary gains `RECT region` (target pixels, already intersected by the caller)
and `bool region_known` (diagnostic; the pass treats unknown as full target).

Bracket, per admitted DIP:

1. `save()` as today; `source_ok` plus `D3DPRASTERCAPS_SCISSORTEST`.
2. **Region backup**: `target(b)`, `SetRt(1, e)`, `full_state()`, then
   `SCISSORTESTENABLE = TRUE`, `SetScissorRect(region)`, the existing fused
   copy program: B|R = A|R, E|R = 0. Same program, same −0.5 quad, same exact
   raw-A copy the fixture already proved. Traffic `24f`.
3. `restore(boundary.scene)` — RT0 is **A** (the owning target, as the game
   bound it), then `SetRt(1, e)`, `SetRt(2, m)`, the write masks, separate
   alpha blend, augmented VS/PS exactly as today. The source draws its native
   RGB source-over into A, `(L, a)` into E, coverage into M.
4. `finish(source)`: composite into **A** with `SCISSORTESTENABLE = TRUE`,
   `SetScissorRect(region)`, sampling `s0 = B` (the pre-draw backup) and
   `s1 = E`. The prototype-1 program is unchanged: it takes raw A from s0 for
   `q = 0` and alpha from `s0.w`; with s0 = B those are the pre-draw values,
   and `A.a` is untouched by the RGB-only source, so `C.a = B.a = A.a` holds
   as before. Traffic `24f`.
5. `restore(boundary.scene)`. No owning candidate, no exchange, no
   acknowledgement: A already holds the result. `owning_candidate()` returns
   null for this policy and `finish_composition` treats a successful in-place
   completion as published.

Why the bracket is correct outside and inside the rectangle: outside, A is
whatever the native source left there — nothing if the rectangle is
conservative, native fade pixels if it is not (soft failure, and M still marks
them because M is raster-driven). Inside, every pixel is rewritten from the
pre-draw backup and E: `q = 0` restores the exact pre-draw bits, `q > 0`
composes. E outside the rectangle may hold stale values from an earlier
bracket; it is never read there, and inside it was cleared by step 2. B outside
the rectangle is stale and never read. Repeated disjoint or tiny rectangles in
one frame are independent brackets; overlapping rectangles back up the already
composed A, which is the correct source-over order. M is cleared once per frame
by `begin_frame` (full clear, scissor explicitly off) and only accumulates.
There is no C and therefore no stale-C leak.

The B copy is regionalised **because** the source draws into A: B is a region
backup, not a recovery image. Native recovery is intrinsic — after a successful
source, A contains exactly the native result, so a composite failure yields
`Native` without any surface swap. Full-size B remains necessary only for the
exchange-based policies.

## 4. Failure contract

- Step 2 fails (bind, copy, scissor): `restore(scene)`; clean refusal before
  the source, counted as today's refusal 5; A and M untouched; the source runs
  natively; a required producer stops the frame as today. Restore failure →
  `blocked`, `state_lost`, submission suppressed — unchanged.
- Source fails: `Incomplete`, `mask_valid = false`, `blocked` — unchanged.
- Composite fails after a successful source: image `Native`, M valid, A holds
  the native pixels, no exchange; completeness retained under the existing
  "certified native fallback" rule, now certified by construction. Restore
  still runs; its failure is `Incomplete` + `blocked` as today.
- First HRESULT preserved in chronological order exactly as `prepare`/`finish`
  do now (`out.operation` chain, `completion.source` dominating cleanup,
  restore recorded after). Source executes at most once. Reader/export
  quarantine, Reset retirement and `composition_state_lost_` handling are
  unchanged; the exchange/acknowledgement code is simply not reached for this
  policy, and the fixture must show it is not reached.
- Region computation never fails: every uncertainty selects the full target.

## 5. Native Windows

Documented D3D9 only: MRT, independent write masks, separate alpha blend (all
already required), plus `D3DPRASTERCAPS_SCISSORTEST`, `SetScissorRect` and
`D3DRS_SCISSORTESTENABLE`. No `Clear` with rectangles is used, so the
scissor-versus-Clear question never arises. The bound comes from game-EXE
internals (in scope) behind the same exact-executable gate as `object_trace`
(SHA-256, call-site bytes) and Win32 `VirtualQuery` validation; nothing depends
on Wine. Everything fails closed to the full viewport, which is prototype 1
executed in place. Native execution stays unverified and is tracked in
`platform-portability.md` like the rest of the fade route.

## 6. Cost model

Logical bytes per viewport pixel per DIP, `f` = rectangle area fraction:

| Shape | Traffic | f = 1 (unknown) | f = 0.058 | f = 0.014 |
|---|---|---:|---:|---:|
| Prototype 1 (measured) | 48 | 48 | 48 | 48 |
| Three-output C shape (resume item 2) | 32 + 24f | 56 | 33.4 | 32.3 |
| **In place (this note)** | 48f | 48 | 2.8 | 0.7 |

Prototype 1 measured 6.61–7.11 ms source windows at 1080p/16 DIPs. At the
run27 upper bound `f ≤ 0.058` the in-place traffic is ≤ 6% of that; the
remaining bracket cost is then the fixed CPU-side state traffic (the 65–67
getters and the setter storm per bracket), which must be measured separately
and is the next optimisation target, not this one. The three-output shape can
at best remove 30% of prototype 1's traffic and never pays when the bound is
unknown. These are logical transfer estimates, not timings.

## 7. Build steps and qualification

**Step 1 — bound, projection, conservativeness witness (no composition change).**
Files: `src/proxy/object_trace.{h,cpp}` (scope accessor), a new
`src/proxy/fade_region.{h,cpp}` (table, reads, gating) with the projection as
pure functions in a host-testable header, counters and one `motion_route`
log field set. Acceptance:

- Host test (`verification/analysis/test_fade_region.py` driving a small C++
  host binary, or a C++ host test beside `linear_emission_pass_host.cpp`):
  random boxes and matrices, 10⁴ interior points per case projected on the CPU
  must land inside the rectangle; `w ≤ 0`, nonfinite, unknown rows, off-screen,
  viewport offset, scissor, jitter on/off, zero and maximal (`|p| = 2`)
  extents. Exact expected rectangles for hand cases.
- Wine fixture (extend `run_linear_distance_fade.py` with a region case group,
  prototype-1 bracket unchanged): draw asteroid-pair geometry with a known
  synthetic AABB through the real bracket, compute the rectangle with the
  production function, read back **M** and require every covered pixel inside
  the rectangle; cases: interior, all four viewport edges, scissor, fully
  off-screen, corners at negative and near-zero `w` (must select full
  viewport), near-plane crossing with `w > 0`, jitter at every Halton index,
  tiny (sub-pixel) and huge boxes. Acceptance: zero violations, every unknown
  case reports full viewport, no change to the existing 71-case totals.
- Diagnostic live witness for the user's next capture (no launch by agents):
  per fade DIP log `part`, six AABB values, rectangle, `f`, VB/IB revisions,
  cache hit/poison, scope depth; every k-th frame with emission off read M
  back and count pixels outside the union of that frame's rectangles.
  Acceptance: zero such pixels, revision distribution constant per VB, and the
  `f` histogram that fixes the cost expectation.

**Step 2 — in-place bracket** in `LinearEmissionPass` (policy 4), fault seams
for region copy, scissor set, composite and restore; detached GPU fixture:
for every existing case, in-place A must equal the prototype-1 exchanged C
**bit-exactly** (same program, same inputs), plus zero/full/partial coverage,
repeated disjoint and overlapping rectangles in one frame (no stale E/B read,
M union exact), the failure ladder (first HRESULT, source-once, suppression
after state loss, no exchange reached), Reset, capability refusal without
scissor caps.

**Step 3 — runtime integration** in `motion_output.cpp` and the live runner:
exact native destination alpha and source-once for all six producers and mixed
fade/emission ordering; paired 1/4/16-DIP windows at 1280×768 and 1920×1080
with `f ∈ {1, ≈0.06, ≈0.01}` and actual region-pixel counters per frame.

## Alternatives considered

- **Three-output preparation + region composite into C + exchange** (resume
  item 2). Correct, but keeps a full-viewport 32 B/px pass per bracket, is
  worse than prototype 1 when the bound is unknown, and turns a
  non-conservative rectangle into missing object pixels. It also keeps the
  exchange and recovery machinery that the in-place shape makes unnecessary.
- **Hooking `0x004c4fc0` entry** (part in ECX). Direct, but a new patch site
  with its own SEH frame; the existing seam gives the same part for one extra
  dereference.
- **Node/LOD-0 sphere or `part+0x30` L∞ radius**. Rejected by the RE note:
  not per LOD, not a Euclidean bound, and non-uniform scale is real.

## Unknown, and what settles it

- **VB rewrite after load: settled.** [mesh-buffer-rewrite.md](../reverse-engineering/mesh-buffer-rewrite.md)
  finds every model VB/IB write inside the once-per-LOD fill `0x004bb470`
  before the first draw, so the part AABB is a load-time constant. The
  revision gate stays as defence in depth and the step-1 witness as evidence.
- **Half conversion rounding**: the drawn buffers are `CloneMesh` output with
  POSITION0 FLOAT16_4 on a device advertising that capability, so the `2^-10`
  expansion is required, not optional; the rounding mode is unresolved and one
  ULP is covered by it.
- **`0x10000000` container branch** fields read from file: the M-outside-union
  witness covers it empirically; a bounded read of that parser confirms the six
  fields are stored, not zeroed.
- **Toggle VS position path**: all 16 profiles pass the DP4 inspector; residual
  is the RE note's caveat, closed by the same witness.
- **Scope presence** for every admitted fade DIP (deferred path proven
  statically): step-1 counters.
- **Actual `f` distribution and fixed bracket cost**: step-1 witness and step-3
  timings; no gameplay claim before them.

## Step 1 — implemented 2026-09-14 (bound, projection, witness; no composition change)

Code: `src/proxy/fade_region_math.h` (pure projection: `project_box`,
`derive`, `jitter_rows`, shared with the fixture and the host driver),
`src/proxy/fade_region_core.h` (`BoundTable`, Windows-free, driven through an
`Environment` of three function pointers: validated read, seam scope, buffer
content view), `src/proxy/fade_region.{h,cpp}` (production binding to
`engine_memory::read`, `object_trace::scope_descriptor` and
`ownership::get_buffer_content_view`; `resolve` preserves LastError),
`object_trace::scope_descriptor` (TLS scope's `args[0]` and depth, no memory
read), `MotionOutput::derive_fade_region` (after refusals 0–4 of
`prepare_composition`, before `prepare()`; `apply_jitter` now uses the shared
`jitter_rows`). Per admitted fade DIP the `fade_region` capture line carries
bound/reason/status, hit/poison/eviction, scope depth, descriptor, part, the
six raw AABB fields, VB/IB ids and revisions, the rectangle and the area
fraction as an integer per mille (no float formatting in the draw hook); the
per-frame `fade_region_frame` line (Present boundary) sums bound/full/hit/
miss/poisoned/evicted, the reason and status histograms, the mean `f` and the
table occupancy. Fixture status keys 22–26 expose the counters.

Table: 1024 entries reserved with the composition pass, dropped at Reset and
teardown; keyed by the VB allocation id, 32-slot probe window. A full window
evicts its oldest unpoisoned entry (poisoned entries are kept as long as any
unpoisoned one exists); the slot is chosen before any game read and committed
only after every read and lookup succeeded, so a refused read evicts nothing.
Per-hit cost: one probe plus two `get_buffer_content_view` calls (each one
recursive-mutex acquisition, one map find, one metadata read); no other
registry lookup. A miss adds five validated reads (descriptor head, AABB,
back-link, up to 16 subset records at 8 bytes each).

Deviations from section 1, all fail-closed:

- Subset-record ownership is proven by identity: the record's `+0x0c`/`+0x10`
  pointers must equal the application buffer pointers the device was given at
  `SetStreamSource`/`SetIndices`. Both are the public ownership wrappers (the
  capture hooks observe the wrapper device, `loader.cpp`), the same COM
  identities D3DX handed the engine; no `resource_id` or native lookup on a
  pointer read from game memory. A hit re-checks both identities, the IB id
  and the descriptor before the revision gate; any mismatch poisons.
- `FILLMODE` is shadowed separately (`shadow_.fill_mode`, refreshed at
  resync with composition requested); unknown or non-SOLID selects the full
  viewport (`Reason::FillMode`).
- The scissor intersection is deferred to step 2, where the pass already
  holds `saved.scissor`; scissor only removes coverage, so the step-1
  rectangle stays conservative without it (fixture case `scissor`).
- The rectangle is intersected with the owning target's size; an empty
  result is the 1×1 rectangle at the viewport origin. An unknown or empty
  application viewport selects the whole owning target (never an empty
  rectangle), like every other refusal selects the full viewport.

Acceptance (after review fixes, main merged at c978089):

- Host: `PYTHONPATH=verification/probe python3 -m unittest
  verification.analysis.test_linear_distance_fade
  verification.analysis.test_linear_distance_fade_report
  verification.analysis.test_fade_region` — 19 tests OK. The driver
  (`verification/probe/fade_region_host.cpp`) ran 400 random cases
  (viewport offset, zero and maximal `|p| = 2` extents, arbitrary rows,
  jitter on half of them) with 10⁴ interior points each: 326 bounded cases,
  0 points outside, 74 refused, all by `w ≤ 0`; a Python re-projection of 40
  cases agrees bit-exactly on the rectangle and 10⁴+ sampled pixels; hand
  rectangles (identity rows, half 0.5, 16×16 → `(2,2,15,15)`, off-screen →
  `(0,0,1,1)`, viewport `(4,4,8,8)` → `(4,4,12,12)`, zero extent →
  `(6,6,11,11)`, jitter ±0.5 px → `(3,3,15,15)`/`(2,2,14,14)`); every
  doubt (corner `w = 0`, negative `w`, NaN/inf rows or centre, unknown rows,
  unknown/poisoned bound, negative extent, non-solid fill) yields the full
  viewport and an empty viewport the whole target. Table scenarios
  (`--table`, fake descriptor/part/record memory, fake registry): learn
  (5 reads, revisions 7/3, AABB `400,-800,200,1200,600,300` → centre
  `400/65536…`), hit (0 reads), back-link mismatch (3 reads), no record
  (5 reads), revision advance → poison, poison persists, IB-id and
  descriptor mismatch → poison, matching ids with other wrapper identities →
  poison (never a hit), box outside the `|p| ≤ 2` domain → invalid (full
  viewport), content unknown, read refused, 32-entry
  window full → eviction of the oldest unpoisoned entry (poisoned kept),
  refused read evicts nothing, clear.
- DLL: `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
  -DCMAKE_BUILD_TYPE=RelWithDebInfo; cmake --build build -j8` — zero
  warnings; `python3 verification/probe/check_no_x87.py build/d3d9.dll` —
  PASS, 223 reachable functions, no violations (fresh build after the merge
  with main 03a660c and the review fixes). Not an install candidate.
- Wine fixture (`linear-distance-fade-gpu-region1c.json`, bottle X3, 4.9 s, after the merge with main 03a660c):
  the 71 existing cases unchanged (257 source calls, 580,608 numerical
  channels, 183,264 exact raw and 193,536 exact energy channels, max
  tolerance fraction 7.19e-5); 29 region cases through the prototype-1
  bracket with the production `derive`, 1535 covered pixels of the M target
  read back, **0 outside their rectangle**, 21 bounded cases and 8
  full-viewport refusals (`w_zero`, `w_negative`, `nan_rows`, `inf_rows`,
  `rows_unknown`, `bound_unknown`, `negative_extent`, `fill_wireframe`).
  Area fractions at 16×16: interior 0.316, edges 0.211–0.246, corner 0.164,
  off-screen 0.0039, tiny 0.098, near-plane crossing 0.316, viewport offset
  0.766 of its 8×8 viewport, jitter indices 1–8 0.316–0.391, `w_tiny` and
  `huge` 1.0 (clamped, still bound-derived). The detached fixture has no
  seam scope and no ownership registry, so the bound *hit* path is proven on
  the host only; at runtime the `fade_region_frame` counters (`bound` versus
  `full`, status histogram) show which path the user's capture took.
- Live witness: the per-DIP and per-frame lines above are wired for the
  user's next capture. The every-k-th-frame M readback with emission off and
  its "pixels outside the union" validator are **not** wired: they need a
  readback path in `MotionOutput` plus a validator in
  `run_linear_distance_fade_live.py`; the live fixture has no seam scope, so
  it would only exercise the full-viewport path. Not run.

Open from step 1: the per-draw cost of the two content views is not yet
measured in the game; the half-conversion item above remains.
