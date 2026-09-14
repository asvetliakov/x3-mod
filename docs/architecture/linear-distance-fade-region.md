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
- Live witness (`X3M_FADE_WITNESS=<k>`, launcher `--fade-witness [K]`,
  default off, `K` defaults to 30; requires `--linear-distance-fade`).
  `MotionOutput::witness_readback` runs at the Present boundary of every
  k-th frame: when the frame derived at least one fade rectangle, admitted
  no emission draw and the coverage is valid (not stopped, quarantined or
  state-lost), the M target is copied once through `GetRenderTargetData`
  into a retained `D3DPOOL_SYSTEMMEM` surface (size from `GetDesc`, format
  must be `A16B16G16R16F`; released at Reset and retirement, recreated on a
  size change) and every covered pixel (any of x, y, z positive) is tested
  against the union of the rectangles of the frame's *prepared* fade draws
  (1024 stored per frame; past that the frame is flagged `overflow=1`, is
  still sampled and takes the whole target as its union, which cannot
  produce a false violation). One `fade_witness` line per k-th frame
  (`sampled`, `reason` in `no_pass`/`no_fade`/`emission`/`mask_invalid`,
  `rects`/`rects_prepared`/`rects_unprepared`, `covered`, `outside`,
  `union`, the prepared counts and the eight-bucket `f` histogram: ≤1 %,
  ≤2 %, ≤5 %, ≤10 %, ≤25 %, ≤50 %, <100 %, full), and the first 64 per-DIP
  `fade_region` lines of those frames (`lines_truncated` counts the rest)
  are also written outside capture frames. `X3M_FADE_WITNESS` must be
  digits only and shorter than 32 characters, else off. Integers only; the
  readback ticks go into the frame's `readbacks`/`readback_us`. Off, the
  per-draw and per-frame paths do nothing. The validator
  (`validate_witness` in `run_linear_distance_fade_live.py`) requires a
  line per k-th frame, the expected reason per frame, `rects_prepared`
  equal to `fade_prepared`, `rects` equal to the frame's `fade_region`
  lines plus `lines_truncated`, a successful readback and **zero** outside
  pixels on every sampled frame, and reports the `f` histogram and the
  VB revision distribution; a violation raises `WitnessViolation` with the
  per-frame counts. The seam DLL alone reads `X3M_FIXTURE_FADE_RECT=l,t,r,b`
  (never compiled into production) and reports that rectangle as the
  bound-derived region of every admitted fade draw, so the fixture, which
  has no seam scope, exercises a sub-viewport rectangle.
  Run (`linear-distance-fade-live-witness.json`, bottle X3, lock holder
  `fade-witness2`, 18 processes): the 15 baseline processes / 302 frames /
  140 TAA readbacks are unchanged (checks 4,502,395: the fixture gained one
  check per resolved frame with the cutout commits after the 4,502,255
  result of a85bcef; not a witness effect, the witness is off in those
  processes and their logs carry no `fade_witness` line); three witness
  processes of 30 frames each (2.6/2.5/2.2 s), all reproducing the
  fade-on/emission-off images, alpha hashes and counters bit for bit.
  `witness-full` (no rectangle: full 64×64 viewport, `reason=3`,
  `status=no_scope`): 19 sampled frames, 8 skipped `no_fade`, 3 skipped
  `mask_invalid`, 20,992 covered pixels, union 77,824, **0 outside**, 26
  `fade_region` lines all at `f = 1` (22 on sampled frames, which is the
  histogram total, 4 on skipped frames), VB 8 revision 0 on all 26.
  `witness-rect` (`8,16,56,48`, both fixture footprints): same sampling,
  union 29,184 (19 × 1536), 20,992 covered, **0 outside**,
  `f_permille=375` on all 26 (histogram: 22 in the ≤50 % bucket). `witness-control`
  (`8,16,40,48`, excludes the second footprint): the validator fails with
  `WitnessViolation` on exactly frames 16, 18, 20, 24 and 28 with 512
  outside pixels each (the 16×32 strip of the second fade source), which
  the runner records as the expected failure and requires exactly. The
  witness cost at game resolution is unmeasured: one readback of the
  A16B16G16R16F M target per k frames (16.6 MB per sample at 1920×1080,
  `k = 30` by default) plus the CPU pass over its pixels.

Open from step 1: the per-draw cost of the two content views is not yet
measured in the game; the half-conversion item above remains.

## Step 2 — implemented 2026-09-14 (in-place bracket, detached fixture; no runtime route)

Code: `LinearCompositionPolicy::DistanceFadeInPlace = 4` in
`src/renderer/linear_emission_pass.{h,cpp}`. The boundary carries
`RECT region` + `region_known`; `LinearEmissionCompletion` gains `recovery`.
Attach: policy 4 needs the fade caps plus `D3DPRASTERCAPS_SCISSORTEST` and
shares the prototype-1 composite program (no third program; `CreatePs` is
still called once per policy family). Bracket, per admitted DIP, exactly the
section-3 shape: `save()`, `source_ok`, `select_region`, region backup
(`draw(b, A, copy, &R)` = fused copy program with `SCISSORTESTENABLE = TRUE`
and `SetScissorRect(R)`: B|R = A|R, E|R = 0), `restore(A)` + the MRT/mask/
separate-alpha/augmented-program setters as today; the source draws into A;
`finish`: `draw(A, b, composite, &R)` (s0 = B, s1 = E, prototype-1 program
unchanged), `restore(A)`, phase back to idle. `owning_candidate()` is null,
`acknowledge_exchange` returns `D3DERR_INVALIDCALL`, `recover_native()` returns
`None`; `coverage_valid()` is true right after a successful `finish`. All
device calls go through the native slots; the only new slot is `StretchRect`
(34). Native call counts per in-place bracket (static count from the code,
three simultaneous RTs, source-over state set): 65 getters in `save()`,
149 setters + 1 quad draw in `prepare` (backup 67, restore 69, source setup
13), 136 setters + 1 quad draw in `finish` (composite 67, restore 69); 285
setters total, 4 more than prototype 1 (the two scissor pairs), and no
exchange/acknowledgement afterwards. A failure after the source adds one
`StretchRect`. No new pool, no full-screen pass: both quad passes are
scissored to R.

Deviations from sections 3–4, all fail-closed:

- **Failure after the source is `Incomplete` with an exact recovery, not
  `Native`.** The brief for this step required it: after a failed source or a
  failed composite (including its scissor set) the rectangle of A may hold
  partial writes, so `finish` restores state and then copies B|R back into
  A|R with a same-size, same-format `StretchRect(D3DTEXF_NONE)` (documented
  render-target to render-target copy; the fixture already used it as its
  exact reference copy). When the restore itself failed, the recovery first
  detaches the three sampler stages (B may still be bound where the composite
  sampled it) and then copies. The image is `Incomplete`, `mask_valid =
  false`, `blocked = true` — the frame is suppressed like a failed source
  today. The "certified native fallback" of section 4 is therefore not
  reachable for this policy; with a successful recovery the object is absent
  from that frame instead of native. **A failed recovery (ladder stage 8)
  leaves the partial source writes in A|R**; `mask_valid` and `blocked` are
  the same (cleared/blocked), so "absent from the frame" holds only for a
  successful recovery and the frame is suppressed either way. A restore
  failure after a successful composite keeps the composed rectangle
  (`recovery = S_FALSE`) and is `Incomplete` + blocked as before; a restore
  failure combined with a failed source (stage 9) still recovers the
  rectangle exactly. Fault seams: `Copy`, `RegionScissor`, `SourceBind`
  (pre-source, clean refusal), `Composite`, `CompositeScissor`, `Restore`,
  `RegionRecovery`.
- `select_region` intersects the caller's rectangle with the target, the
  saved application viewport and — when `SCISSORTESTENABLE` was saved on —
  the application scissor (the step-1 deferral); unknown or empty selects the
  whole target.
- The fixture's separate-copy twin (`fixture_separate_copy`) drops policy 4 at
  attach: the region backup must initialise E|R in the same rasterisation.
- `verification/probe/linear_emission_pass_stubs/d3d9.h` gained
  `RasterCaps`/`D3DPRASTERCAPS_SCISSORTEST` so the existing host scenarios
  (`linear_emission_pass_host.cpp`, unchanged, 72 scenarios / 1750 checks)
  still compile; they request policies 1 and 3 only.

Acceptance:

- Host: `PYTHONPATH=verification/probe python3 -m unittest
  verification.analysis.test_linear_distance_fade
  verification.analysis.test_linear_distance_fade_report
  verification.analysis.test_fade_region` — 22 tests OK
  (`test_linear_emission_pass_host` separately: 72 scenarios, 1750 checks).
  New: the in-place
  source contract (scissor caps gate, `StretchRect` slot, `select_region`, no
  third program, no Wine symbols) and the report validator for the in-place
  witness (batch counts, region/rectangle twins, ladder with first HRESULT,
  source-once, recovery HRESULT, suppression and no exchange, caps refusal,
  Reset, timing group) with 16 mutation rejections.
- Wine fixture (`verification/results/bottle-X3/linear-distance-fade-gpu-region2.json`,
  raw `/tmp/x3-distance-fade-region2b-r1`, bottle X3, 8.4 s): the 71 existing
  cases unchanged versus `region1c` (257 source calls, 580,608 numerical
  channels, 183,264 exact raw and 193,536 exact energy channels, max
  tolerance fraction 7.19e-5, 5 fault cases, 4 + 3 refusals; 29 region cases,
  21 bounded, 0 violations, 1535 covered pixels). In place: a second component
  instance (own pool, own M) ran **293 brackets** on bit-exact copies of every
  pre-draw A — 252 case steps (246 before Reset, alternating a known
  conservative rectangle and the unknown whole target, 6 after Reset), all 29
  region cases with the production rectangle (`scissor` → `4,4,13,13`,
  `viewport_offset` → `5,5,12,12`, `offscreen` → `0,0,1,1`), 8 rectangle
  cases (11 brackets: whole target unknown, exact, partial, zero coverage
  under an application scissor, 1×1, disjoint pair, overlapping pair,
  unknown-then-known pair — the pairs share one frame so B and E outside the
  second rectangle are stale) and the no-scissor fallback twin; **586
  bit-exact comparisons** (in-place A = exchanged C and in-place M =
  exchanged M after every bracket), 0 differences. Ladder (9 stages, source
  issued once each, exchange never reached): partial VS setter, copy and
  region scissor faults are clean pre-source refusals (`first = E_FAIL`,
  A and M untouched, coverage kept, frame not blocked); invalid source
  (`first = D3DERR_INVALIDCALL`), composite and composite-scissor faults
  recover A|R exactly (`recovery = S_OK`, A equals the pre-draw image),
  coverage invalid, frame blocked; restore fault keeps the composed
  rectangle bit-equal to the exchanged C; a recovery fault reports `E_FAIL`
  and blocks; a restore fault together with a failed source detaches the
  sampler stages and still recovers A|R exactly. Capability refusal: `RasterCaps` without `SCISSORTEST` attaches
  with `supported = available = 2`, an in-place boundary is refused with
  `D3DERR_NOTAVAILABLE` before any getter, and the same boundary as policy 2
  completes through the exchange with a result equal to the in-place twin.
  Reset: an in-place bracket interrupted after `prepare` leaves the caller's
  A bound, E/M detached, 4 references, pool recreated after `ensure_targets`.
- Timing (detached, EVENT-fenced windows of prepare/source/finish per frame,
  M clear outside the window, median of 8 after 2 warm-ups; not game FPS):

  | Size | f | DIPs | Prototype 1 (ms) | In place (ms) |
  |---|---:|---:|---:|---:|
  | 1280×768 | 1.0 | 1 / 4 / 16 | 0.94 / 2.45 / 5.07 | 0.64 / 1.60 / 4.98 |
  | 1280×768 | 0.060 | 1 / 4 / 16 | 0.56 / 1.16 / 3.60 | 0.32 / 0.59 / 1.93 |
  | 1280×768 | 0.010 | 1 / 4 / 16 | 0.51 / 1.04 / 2.82 | 0.28 / 0.58 / 1.92 |
  | 1920×1080 | 1.0 | 1 / 4 / 16 | 1.11 / 3.51 / 12.67 | 1.10 / 3.47 / 12.69 |
  | 1920×1080 | 0.060 | 1 / 4 / 16 | 0.84 / 2.16 / 7.58 | 0.39 / 0.65 / 1.93 |
  | 1920×1080 | 0.010 | 1 / 4 / 16 | 0.75 / 2.08 / 6.80 | 0.33 / 0.59 / 1.98 |

  At the whole target the two policies cost the same (the traffic is 48 B/px
  either way; the exchange itself is free here). At the run27 bound
  `f ≈ 0.06` the in-place window is 0.25× (1080p, 16 DIPs) and resolution
  independent: ≈ 0.12 ms per bracket, which is the fixed CPU-side state
  traffic (65 getters, 285 setters) that section 6 named as the next target.
  Prototype 1's small-`f` windows are cheaper than its `f = 1` windows only
  because the source draw itself shrinks.

Not done here: the runtime route (`motion_output.cpp` still requests and
publishes policies 1–2 only; `finish_composition` must treat an in-place
completion as published and skip `publish_composition`), the live timings and
per-frame region counters (step 3).

## Step 3 — implemented 2026-09-14 (runtime route, live witness and timings)

Code: `src/proxy/motion_output.{h,cpp}`. At the HDR latch the pass is attached
with the producer bits plus bit 4 whenever distance fade is requested
(`requested = producers | 4`); `composition_required_producers_` stays the
producer bits (`supported_policies & 3`), so required coverage, the
availability check, the `linear_composition_device` line (`requested=6/7`,
`supported`/`available` carry bit 4 only when the device reports
`D3DPRASTERCAPS_SCISSORTEST`) and fixture key 16 keep their meaning.
`prepare_composition` selects `DistanceFadeInPlace` for an admitted fade draw
when `caps().supports(4)`, else the exchange policy 2 (the fail-closed
fallback; emission always keeps policy 1). The boundary carries the step-1
rectangle (`route.fade_region.rect`, already the full viewport on every doubt
and clipped to the owning target) with `region_known = true`; the pass
intersects it with the saved viewport and an enabled application scissor. No
per-draw allocation or lookup was added: the rectangle is the one step 1
already derived. `LinearEmissionCompletion` gained `RECT region` (the
rectangle actually backed up and composed) so the runtime counts real region
pixels without a second `select_region`.

`finish_composition`, in-place branch (the exchange path is unchanged and not
reached; `publish_composition` is never called for policy 4): the completion's
region area goes to `region_pixels` and to `pool_traffic_estimate_bytes` at
48 bytes per region pixel (exchange brackets still add 56 bytes per target
pixel at prepare). **Certified native recovery for the in-place policy**:
`Linear` with a successful restore and valid coverage counts `linear`,
`linear_fade`, `in_place_linear` and marks the frame enhanced — A holds the
composed rectangle, no exchange. Anything else (failed source, composite or
composite scissor, restore failure) is `incomplete` + `in_place_incomplete`,
stops the frame (`composition_frame_stopped_`, `invalidate_taa`): the reactive
policy becomes `Unavailable` with a null mask and no history is published. The
two sub-cases differ only in what A|R holds: a successful recovery
(`recovery = S_OK`) leaves the exact pre-draw bits (the object is absent from
the frame, A is native everywhere), a failed recovery (`recovery_failures`,
`last_recovery`) leaves the partial source writes; neither reaches the
exchange path's "certified native fallback", which for the in-place policy is
unreachable by construction. A failed restore additionally sets
`composition_state_lost_` (device state unknown), exactly like a failed
exchange acknowledgement. Mixed frames: an emission bracket after an in-place
fade bracket copies A into the whole of B and clears E in the same full-size
rasterization, so stale B/E regions left by the in-place bracket are never
read; the emission exchange changes only the owning slot and `c`, which the
in-place bracket never touches, and the next in-place bracket reads
`hdr_->target()` afresh. M is cleared once per frame by `begin_frame` and only
accumulates (fixture frames 17–20, 22–25, 27–29 mix both orders).

Present-boundary lines: `linear_composition_frame` gained `in_place`,
`in_place_linear`, `in_place_incomplete`, `region_pixels`; the refusals line
gained `recovery_failures` and `last_recovery`. Fixture status keys 27
(in-place prepared), 28 (in-place Linear), 29 (region pixels, low 32 bits);
`FADE_LIVE` prints keys 0–29.

Runner (`run_linear_distance_fade_live.py`): `expected_sources` models a
composite fault on a fade source as Incomplete (frame 21 is now stopped;
emission keeps Native), `validate_functional` requires `s16 = producers`,
`s18 = s19 = producers | 4·fade`, `s27 = s14`, `s28 = s15`,
`s10 = s4 − s27` (only emission exchanges) and `s29` equal to the sum over
prepared fade sources of the derived (or injected) rectangle intersected with
that source's application scissor. `compare_witness` requires `witness-full`
and `witness-rect` bit-exact with the baseline; `witness-control` must keep
alpha, coverage and counters and differ in the composed color on its violation
frames (the excluded strip is native in A now, which the exchange path hid).
Timing runs per resolution and pair order: fade off, then fade on at
`f ∈ {1, 0.06, 0.01}` (pair 1 reversed); the sub-viewport fractions come from
`X3M_FIXTURE_FADE_RECT` (seam only) centred on the timed source inside its
scissor. `validate_timing` reads the per-frame `linear_composition_frame`
lines and requires `prepared = in_place = in_place_linear = linear` equal to
the frame's DIP count and `region_pixels = count × region`.

Acceptance (worktree of a57ebc4):

- Host: `PYTHONPATH=verification/probe python3 -m unittest
  verification.analysis.test_linear_distance_fade
  verification.analysis.test_linear_distance_fade_report
  verification.analysis.test_fade_region
  verification.analysis.test_linear_distance_fade_live_report
  verification.analysis.test_linear_emission_pass_host
  verification.analysis.test_linear_cutout_contract
  verification.analysis.test_motion_output_runner` — 48 tests OK (host pass
  fixture 72 scenarios / 1750 checks, cutout 41 scenarios / 274 checks).
- DLL: RelWithDebInfo build zero warnings; `check_no_x87.py build/d3d9.dll`
  PASS, 223 reachable functions, no violations. Not an install candidate.
- Live (`verification/results/bottle-X3/linear-distance-fade-live-region3.json`,
  bottle X3, lock holder `fade-region3`, lock wait 0 s, 48.8 s, 26 processes,
  536 frames, 224 TAA readbacks = the 140 of the 5 functional processes plus
  84 of the 3 witness processes): the 5 functional and 2 admission processes
  keep 30/30/30/30/30/4/4 frames, 38 sources and 21 samples each, exact
  native destination alpha and source-once for all six fade producers and the
  mixed fade/emission orderings (the fixture's per-source checks), and the
  lazy/per-draw twins agree bit for bit. Check counts: fade-off processes
  unchanged (779,890 / 900,854 / 61,597 / 60,565); each fade-on process has
  4,097 fewer (897,782 / 894,708 / 894,708 versus 901,879 / 898,805 /
  898,805 in the witness run): 4,096 from frame 21's `emission_mask_valid`-
  gated mask-union comparison, which no longer runs, plus 1 check most likely
  from the frame's history expectation — its fade source now takes the
  in-place composite fault as Incomplete (`in_place_incomplete=1`,
  `recovery_failures=0`, `last_composition=80004005`, `mask_valid=0`) where
  prototype 1 published a certified native B. Every prepared fade source
  composed in place (`s27 = s14`, `s10 = s4 − s27` on all frames); region
  pixels per frame are 1,024 per prepared fade source (the 32×32 scissor
  intersection of the full 64×64 derived rectangle), 1,536 on the control's
  two-source frames. Witness: `witness-full` and `witness-rect` 18 sampled
  frames each (frame 21 is now `mask_invalid`: skipped `no_fade` 8,
  `mask_invalid` 4), 19,968 covered pixels, union 73,728 / 27,648,
  **0 outside**, 26 `fade_region` lines (21 sampled, all `f=1` / all
  `f≤0.5`), bit-exact with the baseline; `witness-control` fails exactly on
  frames 16, 18, 20, 24, 28 with 512 outside pixels each and differs from the
  baseline in the composed color on each of those frames and nowhere before
  frame 16 (alpha, coverage and counters equal on every frame). The witness
  comparison is bit-exact within this run; against earlier runs frame 21's
  TAA image legitimately differs because history is now dropped on
  Incomplete.
- Timing (live fixture, EVENT-fenced source windows, median of 4 samples per
  count after 2 warm-ups, both pair orders; the fixture's fade sources draw
  under a quarter-viewport application scissor, so the derived full viewport
  composes an actual region of `f = 0.25`, and the injected rectangles
  compose exactly 0.0601 / 0.0100 of the viewport; the source raster is the
  same at every fraction; not game FPS):

  | Size | requested f (actual) | region px | 1 DIP | 4 DIPs | 16 DIPs |
  |---|---|---:|---:|---:|---:|
  | 1280×768 | 1 (0.25) | 245,760 | 0.556 / 0.674 | 1.371 / 1.278 | 2.747 / 2.808 |
  | 1280×768 | 0.06 (0.0601) | 59,032 | 0.474 / 0.497 | 1.113 / 1.099 | 2.613 / 2.611 |
  | 1280×768 | 0.01 (0.0100) | 9,856 | 0.790 / 0.812 | 1.045 / 1.123 | 2.450 / 2.656 |
  | 1920×1080 | 1 (0.25) | 518,400 | 0.676 / 0.688 | 1.247 / 1.738 | 2.745 / 2.717 |
  | 1920×1080 | 0.06 (0.0601) | 124,550 | 0.562 / 0.594 | 1.459 / 1.476 | 2.445 / 3.119 |
  | 1920×1080 | 0.01 (0.0100) | 20,736 | 0.882 / 0.590 | 1.364 / 1.401 | 3.336 / 2.518 |

  Fade-off windows in the same processes: 1280×768 0.738/0.338, 0.449/0.561,
  0.824/0.914 ms; 1920×1080 0.336/0.590, 0.351/0.566, 0.825/1.301 ms. The
  prototype-1 exchange route measured in the witness run (same fixture, same
  windows): 1280×768 0.742/0.868, 1.881/1.870, 5.097/3.845 ms; 1920×1080
  1.064/1.028, 3.106/3.181, 8.037/8.211 ms (the brief's earlier live figures
  0.476–0.656 / 2.501–2.570 / 6.608–7.107 ms at 1080p). In place, the
  16-DIP window at 1080p is 2.7 ms against 6.6–8.2 ms and no longer depends
  on the resolution or on the region fraction: the 16-DIP rows give
  0.10–0.16 ms per bracket at every size and `f`, the 4-DIP rows 0.12–0.28 ms
  and the 1-DIP rows −0.19…+0.55 ms (within the window noise), which is the
  fixed CPU-side state traffic (65 getters, 285 setters per bracket) that
  section 6 named as the next target. The sub-viewport fractions show no
  further gain at this fixture because the region traffic at `f = 0.25` is
  already below the per-bracket floor.

Risk accepted: in-place correctness depends on the step-1 rectangle being
conservative — a pixel the source touches outside it stays native in A and
only M records it; the fixture witness and the `--fade-witness` user capture
are the only detectors, which is why the user run must carry
`--fade-witness`.

Open from step 3: native Windows execution of the in-place route is
cross-compiled but unverified (`platform-portability.md`); the in-game
`f` distribution and the bound *hit* path are still only witnessed by the
user's capture (`fade_region_frame`, `linear_composition_frame`); a fade
composite fault now removes the object from the frame (exact recovery)
instead of showing it natively, which the game never exercises unless a
scissored quad draw fails.

## User run 11 — 2026-09-14

Gameplay evidence from snapshot `/tmp/x3-bottleX3-run36/` on installed
checkpoint `3f06979` (`--linear-distance-fade --fade-witness`). 543
`fade_witness` lines: 196 sampled (`sampled=1`), 347 unsampled all
`reason=no_fade`; covered pixels outside the rectangles 0 in every sampled
frame (max 0); `rects_unprepared`, `overflow`, `lines_truncated` all 0. The
risk accepted at step 3 — that the step-1 rectangle is conservative — held,
with zero outside pixels over 196 sampled frames. Aggregated `f_hist` 1962, 98,
11, 0, 0, 0, 0, 0 and max `f_permille` 24: the run exercised only shallow fades
(≤ 5 %). Regions: 2087 per-DIP lines, 100 frame summaries, 0 full-viewport
fallbacks (`full=0`, all `status=bound`); area mean 3367 px², median 2352 px²,
max 24150 px² against a 983040 px² viewport (≈0.3 % typical, 2.5 % max). Frame
time (`frame_end dt_ms`, 60-frame samples) n=59 median 4097 µs, p95 10104 µs,
max 27040 µs against run 28 n=99 median 4104 µs, p95 8824 µs, max 26467 µs:
equal medians, and the run-11 tail is a smaller sample with no fade-cost field,
so it is not attributable. No poisoned or evicted regions, no reset/recovery.
