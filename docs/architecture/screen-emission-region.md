# Packed screen emission inside the region bracket

Design note, 2026-09-14, for ratification at `2b14bba`: whether the qualified packed screen law
([screen-emission-overlap.md](screen-emission-overlap.md)) is integrated through the ratified in-place region
bracket ([linear-distance-fade-region.md](linear-distance-fade-region.md), policy 4, run 11). Not implemented.


Ratified by the main session 2026-09-14 as the route to pursue, conditional on the
bullet-bound feasibility (step B): unbounded draws refuse to native, never full
viewport; the first action is the capture query for the row-19 vertex buffer's lock
flags and position layout, then the disassembly brief of section 6 only if the
captures cannot answer it.
Step D (the per-draw vertex hull that replaces the AABB checkpoints for the bullet bound) is
designed in [screen-emission-bullet-bound.md](screen-emission-bullet-bound.md).
## Decision

**Yes, as policy `PackedScreenInPlace = 8` reusing the fade bracket's shape verbatim — backup A|R →
producer → scissored composite into A|R → exact recovery — with the producer's targets and the composite
program swapped.** The prototype's three fullscreen passes become two scissored quads and one scissored
composite; native-B assembly, the C target, exchange and publication disappear because the result lands in
A and failure is the fade route's (exact recovery, `Incomplete`, frame stopped). Every prototype open item
collapses except **the bound**: the only observed screen population (row 19, 54 draws in 12 frames, ≈4.5 per
frame) is unscoped at the draw and its VS transforms world-space vertices through VP c0–3, so
`derive_fade_region` has no part descriptor and no object-space box for it. Unbounded, the bracket is the
full-viewport prototype (≈2.3 ms per DIP at 1080p, ≈10 ms per frame) and must not be admitted. Two steps:
**(A)** the policy and fixtures, admitting only bounded draws; **(B)** a bullet bound taken from the vertex
data at Unlock — the decisive new piece, one capture query first (section 6).

## 1. Bracket shape, cost and budget

Per admitted DIP, `R` the caller's rectangle intersected by `select_region` as today:

1. `save()`; require `caps().supports(8)`: `NumSimultaneousRTs ≥ 4` (`linear_emission_pass.cpp:613` caps
   `rt_count` at 4), scissor, independent masks, post-pixel blending, FP16 `CheckDeviceFormat` for INVSRCALPHA.
2. **Backup** B|R = A|R, the existing fused copy under scissor (fade step 2). Seams `Copy`/`RegionScissor`. 24f.
3. **Plane init** under the same scissor: the prototype's 22 ALU + 1 TEX program, P_c|R = (A_c, decode(A)_c,
   0), M.alpha = A.alpha with M.red masked off. New seam `PlaneInit`. 40f. A second quad is unavoidable:
   backup, three planes and M.alpha are five outputs against D3D9's four.
4. `restore(scene)`, then RT0 = **M** (mask red|alpha), RT1–3 = planes (mask RGB), `DESTBLEND = INVSRCALPHA`
   for the native INVSRCCOLOR, all else native (alpha test, Z, cull). Producer: the qualified `PackedScreen`
   PS (201 DWORDs, 42+1 slots), untouched VS. **A is not written by the source** (section 3).
5. **Composite** into A|R under scissor: the prototype's C assembly (26 ALU + 5 TEX) with s0 = **B** (pre-draw
   A: ordered-zero channel copy, unchanged alpha), the planes and M. Seams `Composite`/`CompositeScissor`. 48f.
6. `restore(scene)`. No candidate, no exchange; `owning_candidate()` null as for policy 4.

Logical traffic 112f B/px (fade 48f; prototype ≈128 at f = 1): 263 KB per bracket at the run-11 median
rectangle (2,352 px). Fixed cost is the fade floor (0.10–0.16 ms per bracket at 16 DIPs; 65 getters, 285
setters) plus one quad, two `SetRenderTarget`, three texture binds and ≈8 render states: **estimate
0.15–0.25 ms per bounded bracket**, measured in step 2. Storage: three new planes are 47.5 MiB at 1080p
(22.5 at 1280×768), or **15.8 MiB (7.5)** with E and C serving as P_r/P_g — alias-safe because every bracket
initialises the region it reads, but an optimisation the step-2 stale-read case must prove. The prototype's
assembled-B and C targets and its 110.7 MiB seven-target footprint are not needed.

Budget: run 11 admitted ≈20 fade brackets per frame with no median change against run 28, so the fixture
floor is an upper bound in game. At 4.5 bullet DIPs per frame the bounded route costs an estimated 0.7–1.1 ms
of floor, ≈17–27 % of the 4.1 ms median as an upper bound — material, the fixed-cost target the region note
names. A per-draw cap `f ≤ 0.25` (else native) bounds the traffic term to ≈58 MB per bracket at 1080p.
Acceptance for step B: median frame time within +0.5 ms of run 11 in a bullet-heavy flight; otherwise the
setter-storm optimisation precedes the feature.

## 2. Failure and publication: what collapses, what remains

Collapsed into the ratified in-place branch of `finish_composition`:
- **Persistent B-assembly failure**: there is no B assembly; the native fallback the prototype could not
  promise is replaced by certified exact recovery. Composite/scissor failure after a successful source →
  `RegionRecovery` B|R → A|R, `Incomplete`, `mask_valid = false`, frame stopped, history dropped (the bullet
  is absent that frame, as a fade composite fault removes the asteroid). A failed recovery leaves A|R
  untouched anyway (the source wrote only planes) and sets `composition_state_lost_` as today.
- **Restoration, publication, private storage**: A stays owning target and physical RT0 after `restore`;
  planes are pool-private like B/E; failures before the source are refusal 5 with the source native.
- **Live ownership/history**: M.red accumulates `1 + (1−a)·old ≥ 1`, still "nonzero = reactive" for
  `resolve.hlsl`; M.alpha is per-bracket scratch (seeded in step 3, consumed in step 5). Remaining: confirm
  history-copy and diagnostic readers of M read red only; the first-HRESULT chain gains the `PlaneInit`
  position; the prototype's CPU content-recovery rung is dropped (A is intact).

## 3. Overlap inside one DIP; sharing a rectangle with a fade draw

The packed law needs only that all fragments of the DIP hit the same four targets in submission order with
the substituted blend; the bracket changes targets and scissor around the DIP, not the DIP. The scissor
applies to the quads only — the source keeps the application scissor, so M outside the rectangle still
records a non-conservative bound and the run-11 witness stays the detector. Because the source writes planes,
not A, a non-conservative rectangle leaves the bullet **missing** outside it rather than native as for the
fade — the C-shape failure the region note rejected, and why step B's bound must come from the vertex data.

Fade and packed brackets in one frame are sequential in-place brackets on A: a later bracket backs up the
already composed A (correct source-over order either way) and seeds its own scratch (E|R or planes|R) after
the backup, so nothing stale is read. Both leave depth untouched (Z-write off). M is the union; TAA stays
"covered = current-only". An emission exchange bracket after a packed one rewrites B and E whole (run 11).

## 4. Admission set and bound availability

Admit by exact state (`ALPHABLENDENABLE`, `ADD/ONE/INVSRCCOLOR`, mask 15, separate alpha off, Z-write
off, dither off, sRGB off, PROJECTED off per the SM1 note, alpha test any function/reference) for all nine
SM1 pairs; the route is pair-agnostic and the packed producer serves scalar and bullet bodies alike. Only
row 19 has been observed, only in this state. If the six DEFAULT/INSTANCE effects are scoped,
`derive_fade_region` applies with `matrix_register` = 0. For bullets the part path is unavailable by evidence
(`effects-engine-remaining-emission.md:121`): with step A alone the fallback rate for the observed population
is **100 % → refused to native**, a new refusal reason, not frame-stopping (the screen producer is not
required; every bullet is native without coverage today, so a refused draw is the status quo). SM1 live
registration (pair rows, TSS PROJECTED cache, variant creation; SM1 note step 3) precedes both steps.

## 5. Fixtures

- Reused unchanged: `run_linear_emission_sm1.py` and the packed corpus (648 rows, 540 in-domain) as oracle.
- `run_linear_distance_fade.py` extended as in fade step 2: every packed in-domain row through policy 8 under
  a rectangle equals the prototype's C **bit-exactly inside R** and A outside; two overlapping primitives in
  one DIP inside one R, forward and reverse; a draw straddling an injected rectangle (`X3M_FIXTURE_FADE_RECT`)
  with the M-outside-union witness firing; fade→packed and packed→fade on overlapping rectangles with a
  sequential oracle; emission exchange after packed (the plane-reuse alias case); the fault ladder including
  `PlaneInit` (first HRESULT, source-once, exchange never reached); `NumSimultaneousRTs = 3` refusal; Reset.
- `run_linear_distance_fade_live.py`: screen sources mixed with fade/emission at 1/4/16 DIPs, exact native
  alpha, source-once, paired windows, counters (`packed*`, `unbounded_refused`, region pixels).
- User acceptance: one run with the witness on and bullets fired — zero covered pixels outside the
  rectangles, bounded rate for row 19 near 100 %, `f` histogram, frame-time median against run 11's 4.097 ms.

## 6. Risks, unknowns and the bounded briefs

- **Bullet bound (step B).** At Unlock of the row-19 VB, take position extrema over the mapped window (the
  finite-evidence scan already walks the mapped payload under MFENCE/thread/revision/window checks,
  `d3d9_ownership.cpp:907–909`) into a per-lock window record (offset, length, first vertex, storage, six
  extrema, revision); at the draw require the DIP's vertex range inside that window and the revision
  unchanged, then project the world-space box through VP c0–3 with jitter via the existing `derive`.
  Nonfinite or garbage extrema → native; a few KB scanned per lock. Two facts settle feasibility, from the
  existing captures' ownership buffer-event records for the row-19 VB ids before any disassembly: the lock
  flags (the finite scan rejects DISCARD/NOOVERWRITE as `UnsupportedWrite`, so the window record is a new
  contract) and position storage/stride. If the captures lack them, a bounded `disassemble` brief: the writer
  and DIP site of the row-19 VB (start at the particle renderer `0x004bf4c0`, `emission-draw-order.md`), its
  Lock flags/range, vertex layout, and whether a CPU bullet list with position/radius exists there.
- **Scalar-fade pairs** (rows 16/20–24): a descriptor layout unlike the asteroid part yields
  `BackLink`/`NoRecord` → native; settle when a capture shows such a draw. **Non-conservative bound is a hard
  failure** (missing pixels), the witness the only detector. Also: M consumer audit (section 2); INVSRCALPHA
  on the FP16 plane format on the user's device (caps key); q > 1 flag cancellation stays excluded.

Alternatives: the **fullscreen prototype with exchange** as qualified (≈2.3 ms per DIP at 1080p, seven
targets, unresolved B-assembly/publication contract); **composing into B and copying back** without the
backup quad (saves 24f but changes the ratified ladder for no measured gain at run-11 rectangle sizes);
**scissoring the source too** (blinds the witness); **admitting unbounded draws at full viewport** as the
fade does (three fullscreen quads per bullet at the observed rate, so unbounded must mean native).

## Step B evidence, 2026-09-14 (capture query)

The run-28 and run-11 session logs contain no draw with `ps=ec1f5c4a2f4e1445`
(the shader is compiled and dumped, never bound); the row-19 evidence in the
effects-engine note came from an earlier capture set not under `/tmp`. The same
VS `5e484a06672e28fb` draws in those frames with `ps=0a523f33ac47ae05` from a
dynamic write-only vertex buffer (`usage=520`), locked with `D3DLOCK_DISCARD`,
positions `FLOAT3` at offset 0, stride 24 (TEXCOORD FLOAT2 at 12, D3DCOLOR at
20), one draw per buffer revision (revisions 926/1149/1367 in frames
1476/1699/1917). The ownership layer records lock flags and revision but no byte
range, so the window record of section 6 is a new contract, and the writer site
must come from disassembly (brief dispatched).

Disassembly answered both facts: the writer, its whole-buffer `D3DLOCK_DISCARD`
lock at `0x004bfdd9`, the `memcpy` of `count*24` bytes from a persistent
system-memory copy, and the non-indexed `DrawPrimitive(4, 0, count/3)` at
`0x004c008a` are documented in
[bullet vertex buffer writer](../reverse-engineering/effects-engine-remaining-emission.md#bullet-vertex-buffer-writer-2026-09-14).
Consequences for step B: the lock window is the whole 147456-byte buffer while
only the leading `primCount*3` vertices are valid, so an Unlock-time scan must
be reduced to that prefix at the draw (`StartVertex` is always 0); positions are
world-space `FLOAT3` at offset 0, stride 24; and the game keeps no bullet radius
or bound to read instead.

## Step A — implemented 2026-09-14 (policy 8, detached fixture; no admission or route)

`LinearCompositionPolicy::PackedScreenInPlace = 8` in `src/renderer/linear_emission_pass.{h,cpp}`, the section-1
bracket verbatim: `save()`, packed `source_ok` (ONE/INVSRCCOLOR, mask 15, separate alpha off, Z-write off, alpha test
any, VS untouched), region backup B|R = A|R (fused copy), **plane init** under the same scissor (new fault seam
`PlaneInit`; masks M = 8, planes = 7), `restore(M)` + RT1–3 = planes, masks 9/7/7/7, `DESTBLEND = INVSRCALPHA`,
augmented PS only; `finish`: scissored packed composite into A (s0–2 planes, s3 M, s4 B), `restore(A)`, the fade
ladder's exact recovery. Planes: **E = P_r, C = P_g, one new P_b** (one 15.8 MiB FP16 plane added at 1080p; the five-target pool is
≈79 MiB); M.alpha
is the per-bracket scratch lane — consumer audit: `resolve.hlsl` reads `.r` only, the live witness readback tests
RGB, the temporal route hands the texture to that resolve; nothing reads alpha. Caps: `NumSimultaneousRTs ≥ 4`,
independent masks, scissor, `D3DPBLENDCAPS_ONE`/`INVSRCALPHA`; two ps_3_0 programs generated from the prototype's
helpers with identical arithmetic (`tools/shaders/generate_screen_emission_programs.py`); the save/restore
inventory grows to five sampler stages only when policy 8 is available. `motion_output.cpp` untouched (step C).
Fixture (`verification/probe/screen_emission_step_a_fixture.cpp`, includes the frozen packed prototype TU; runner
`run_screen_emission_step_a.py`; result `verification/results/bottle-X3/screen-emission-gpu-step-a.json`): all
**540 in-domain rows** (9 pairs × 20 cases × one-DIP overlap / two DIPs / reverse, 180 each) through policy 8
under the prototype-coverage rectangle (+1 px), the unknown whole target and +3 px are **bit-exact inside R
against the prototype's C and A outside, M red/green/blue whole and alpha inside R equal, no coverage outside
any rectangle** (508,518 region pixels); the injected straddling rectangle (`--rect=8,8,24,24`) keeps the law
inside, A outside and fires the witness (419 covered pixels outside); fade→packed and packed→fade on overlapping
rectangles pass a sequential oracle (fade stage against a CPU oracle, max tolerance fraction 0.14; packed stage
bit-exact against the prototype run on the intermediate; coverage is counted as the live witness does, a
non-negative nonzero half); an emission exchange after a packed bracket equals a
fresh pass bit-exactly (E/C alias); the 10-stage ladder (copy, region scissor, plane init, source bind → clean
refusal with A and M coverage untouched, frame not blocked; source, composite, composite scissor, restore,
recovery, restore+recovery → `Incomplete`, first HRESULT chronological, A exact even with a failed recovery,
exchange never reached, frame blocked); four capability refusals (three targets alone/beside the others,
INVSRCALPHA, masks); Reset with an interrupted bracket (owned RT0 replaced by the backbuffer, planes detached,
pool recreated, post-Reset row exact). Cost, paired EVENT-fenced windows at 16 DIPs: native 0.33 ms →
packed 2.42 / 2.59 ms (1280×768, 2352 / 24150 px) and 2.40 / 2.52 ms (1920×1080): **≈0.13–0.14 ms per bracket**,
the fixed setter floor of the fade bracket plus one quad, resolution independent. Deviations: the fixture
lives beside the packed prototype rather than in `run_linear_distance_fade.py` (its corpus and C oracle are
there); the rectangle is injected by argument; a refusal after plane init leaves M.alpha|R seeded (scratch).

## Step B — implemented (2026-09-14)

No game hook. `src/proxy/locked_prefix_core.h` (header-only, host-testable) holds a
16-entry fixed table of per-buffer records (no allocation) with the state machine
Marked → Pending → Published | Invalid. A record exists only for a buffer an admitted
screen-emission draw has been seen from: `MotionOutput::derive_prefix_region` (vertex
shader in `screen_emission_admission.h`, today the bullet VS `5e484a06672e28fb`, the table
step C's admission shares; non-indexed TRIANGLELIST, StartVertex 0, stream 0 stride 24,
declaration POSITION FLOAT3 at 0, stream-0 frequency 1 by one documented
`GetStreamSourceFreq`, else refused) marks the stream-0 buffer through the lookup, so the
first draw after creation is refused by design and no other DISCARD lock is ever scanned
(≈1 buffer per part batch per frame). At a marked buffer's Unlock the ownership layer
(`X3M_SCREEN_EMISSION_BOUND=1`, default off, `Options::locked_prefix_bounds`) scans the
mapped window of a `D3DLOCK_DISCARD` lock from offset 0 with an explicit size once under
MFENCE as FLOAT3 at 0, stride 24, at most 6144 vertices, into 64 cumulative checkpoints:
checkpoint k holds the min/max over vertices [0, 96(k+1)) and the first checkpoint whose
prefix holds NaN, ±inf or |c| > 2^24 (`world_limit`). Every Lock advances the revision
(Pending); a nested or non-DISCARD lock, SizeToLock 0, thread mismatch, failed Unlock or
ProcessVertices makes it Invalid; final Release erases, Reset clears (both gated by the
option). At the draw checkpoint ⌈primCount·3/96⌉−1 (a superset with at most 95 stale
vertices; a draw past the scanned vertices or 6144 is `Cover::Beyond`, refused) goes through
`resolve_locked_prefix` (`BoundSource::LockedPrefix`) and `derive` with the c0–3 rows;
`route.jittered` is false for the unmodified bullet shaders, so the box is projected without
jitter until step C decides whether the packed producer jitters. `prefix_region.bound ==
false` means refuse to native in step C, never the full viewport; the derivation's ticks go
to `route.ticks`. Cost: 6144-vertex scan 23 µs mean / 26 µs max per lock under FEX
(`screen-emission-bound-gpu1.json`; `screen-emission-bound-live1.json` through the proxy
DLL); the ≈20 µs host figure is a cached-memory host CPU number. Native D3D9 maps dynamic
buffers write-combined, where reads are far slower: that cost is unmeasured and bounded by
the allowlist, not by the scan. Evidence: host `--prefix` (42 scenario lines, 300 random
superset cases, 0 failures); detached fixture 13 cases (11 bound, 2 refused, 0 violations,
after-Reset relearn); `ownership_wrapped.exe` 553 checks (61 per device iteration on the
Lock/Unlock path: unmarked ignored, mark, scan, nested, non-DISCARD, NaN tail, erase at
Release, clear at Reset); proxy-loaded `run_locked_prefix_live.py` 13 frames under the game
bullet VS/PS (8 bound, 5 refused); x87 audit PASS (`check_no_x87.py`, 224 reachable
functions, no violation; the scan is integer/SSE scalar). Nothing consumes the rectangle yet.

### Step B near-plane clipping (2026-09-14, run 15 diagnosis)

Run 15 (`session-20260914-163102-212.log`, five firing F8 captures) showed the bound
refusing half of the bullet draws and bounding the other half with 58–90 % rectangles:
`locked_prefix_frame` totals draws=800 bound=400 refused=400, every refusal `reason_w`
(NonPositiveW). Projecting the logged boxes through the `camera_state` view of each frame
(v·View convention) gives the cause: the player's own bullet batches (432–486 vertices)
straddle the camera plane with view depth from −320 to +7726 (a corner at w ≤ 0 refused the
whole box, drawn native), the 3200–3300-vertex batches lie 76–96 km behind the camera (all
w < 0, nothing visible, refused for the wrong reason), and the admitted 1056-vertex batches
sit 128–2122 units in front of it, where a 1000-unit AABB legitimately projects to most of
the viewport. The native half beside the packed half explains a mixed, dimmer look; whether
the packed composite itself is dimmer is what the diagnostic below measures.

Fix (`fade_region_math.h`, `project_box(..., NearClip*)`, `derive(..., near_clip)`; the
locked-prefix source only, the part-bound fade route keeps NonPositiveW → full viewport):
the box is cut against the D3D near plane in clip space before the divide. D3D rasterises
only 0 ≤ z ≤ w, so the plane is clip z = 0 (the game's zn, 6 in gameplay, is wherever the
c0–3 rows put it; nothing is hard-coded and the fixture rows put it at w = 1). The polytope
box ∩ {z ≥ 0} is convex; its vertices are the corners with z ≥ 0 plus the exact crossings
(z is affine along an edge, t = za/(za − zb)) of the 12 box edges, and with w > 0 at each of
them the rectangle is the hull of their projections, padded and intersected with the
viewport as before. Every corner behind → `Reason::BehindNear` (7), refused to native as a
distinct reason (`reason_near`); a remaining vertex with w ≤ 0 (non-perspective rows) stays
NonPositiveW. `Region::clipped` carries the number of corners cut; the half-float expansion
puts a box whose near face lies exactly on the plane 2⁻¹⁰ behind it (clipped=4, same
rectangle; the functional fixture's identity-row quad shows the same clipped=4 because its
zero stale tail sits on z = 0). A straddling box is still bounded by its visible part only; a batch that starts
just in front of the camera keeps a large rectangle by geometry (an AABB touching the
camera fills the view) — that is the cost of the AABB checkpoints, not a bound defect.

Grammar: `locked_prefix ... reason=%u clipped=%u ...` per draw on capture frames;
`locked_prefix_frame ... clipped=%u ... reason_near=%u` (clipped counts bound draws whose
box was cut). Capture frames only, zero cost otherwise (one bool per admitted packed draw):
`packed_sample device= frame= index= rect=l,t,r,b clipped=n composed=l,t,r,b centre=x,y
format=<D3DFORMAT> pre=r,g,b,a pre_y=<Rec.709 luminance> post=r,g,b,a post_y=
pre_result=%08lx post_result=%08lx scan_result=%08lx scan_px= changed_px= max_pre_y=
max_post_y= sum_pre_y= sum_post_y= argmax=x,y argmax_pre=r,g,b argmax_post=r,g,b` — the HDR
target A sampled at the centre of the bound
rectangle after `prepare` (A|R copied to B|R, A untouched) and after the composite, one
documented `GetRenderTargetData` (whole surface: the destination must match the source's
size) into two retained system-memory surfaces of A's size and format (pre and post) plus a
1×1 `LockRect`
each (A16B16G16R16F, A32B32G32R32F, A8R8G8B8/X8R8G8B8 decoded; other formats fail closed
with `D3DERR_NOTAVAILABLE`). Cap: the first `packed_sample_cap` = 4 admitted packed draws
of a capture frame are sampled, the rest are counted in `packed_sample_skipped=` on the
`linear_composition_frame` line (a run-15 frame with 400 admitted draws costs 8 copies, not
800). The retained surfaces are allocated once per size/format, counted by
`device_references()`, released with the witness copy at Reset and teardown; the pending
`pre` is cleared in `begin_frame`, so an unmatched pre never pairs with a later frame's
post.

The bracket around a thin beam spans up to the viewport, so its centre is usually not a
bullet pixel (run 17: 20 lines with bit-identical `pre`/`post` centres). The post sample
therefore also scans the whole rectangle, clipped to the copies' extent, out of the two
retained images (`scan_px` scanned pixels, `scan_result` the scan's HRESULT; the scan is
skipped with its result when either readback failed): `changed_px` counts pixels whose RGB
bits differ, `max_pre_y`/`max_post_y` and `sum_pre_y`/`sum_post_y` are the Rec.709
luminance maxima and sums, and `argmax=x,y` with `argmax_pre`/`argmax_post` is the post
maximum's location and its two colours — a bullet pixel, so `argmax_post` against the
native run is the "dimmer" verdict the next user run needs, and `sum_post_y - sum_pre_y` is the
bracket's total contribution. One pass per rectangle, no allocation and no D3D call beyond
the two `LockRect`s; a full-viewport FP16 bracket is ~1 M pixels, and at most four
rectangles are scanned per capture frame.

Per-frame timing is a separate opt-in, so the option itself stays free of per-frame
logging: `--screen-emission-timing` (`X3M_SCREEN_EMISSION_TIMING=1`, requires the enabled
`--screen-emission`) logs one
`screen_emission_frame device= frame= packed_admitted= brackets_px= cpu_us=` line per
Present, where `packed_admitted`/`brackets_px` are that frame's composition counters and
`cpu_us` is the `QueryPerformanceCounter` delta since the previous Present (the wall-clock
frame time; 0 on the first one). One QPC and one log call per Present, one predicate when
off. The live runner's `screen-timing-line` case turns the flag on over the functional
process and checks the lines with `validate_screen_emission_frames`
(`verification/probe/run_linear_distance_fade_live.py`): one line per Present of the device,
frames increasing, every Present after the first with a measured delta, counters equal to
that frame's `linear_composition_frame`, and the functional totals and packed samples
unchanged against the run without the flag; the other cases assert the line is absent.

Evidence: host `test_fade_region.py` (8 tests: `--near` 600 random straddling cases, 0
failures, 0 outside points; hand cases straddle/exact/behind/beam equal to a Python
restatement, beam rect (30,0,64,43) = 34 % of 64×64); `run_locked_prefix_live.py` 15 frames
(9 bound, 6 refused; near_straddle clipped=4 rect (46,10,96,51) f=222‰, near_exact
clipped=4 (46,10,96,63), near_behind reason 7 clipped=8 refused, near_beam (46,0,96,63)
f=341‰, all covering their footprints; 11 scans, 24.0 µs/scan); `run_linear_distance_fade_live.py
--screen-emission` (`screen-emission-live1.json`, 12 processes) with kinds n/x/h/b at frames
17–20: 21 frames, 26 sources, 19 eligible / 13 admitted / 12 linear / 3 unbounded (first
draws and the behind-plane h), bound rectangles n (30,6,64,35) 24 %, x (30,6,64,43) 31 %,
b (30,0,64,43) 36 % of 64×64, each covering its footprint, fade witness outside=0 on all ten
sampled frames (17, 18, 20 included), injected straddle violations {…, 17: 84, 18: 128,
20: 128}, seven `packed_sample` lines over capture frames 2–9 (six with a changed centre
luminance, the composite-fault frame unchanged, `packed_sample_skipped=0` on all 21 frames,
held references 59 unchanged with the retained surface released), Reset and caps runs unchanged (see the
ledger [screen-emission.md](../verification/screen-emission.md)); x87 audit PASS (224
reachable functions).

## Step C — implemented (2026-09-14)

Runtime admission behind `--screen-emission` (`X3M_SCREEN_EMISSION=1`, which sets and implies
`X3M_SCREEN_EMISSION_BOUND=1`; requires `--linear-materials --taa --motion-output --ownership`; default
off). `screen_emission_admission.h` is the one table for step B's scan allowlist and this admission: the nine
SM1 pairs (row 19 first), the three INSTANCE_BULLETS vertex shaders flagged bound-capable (one body, c0-3 =
g_mViewProjection); the six DEFAULT/INSTANCE bodies register their promoted producer but have no bound
source, so their draws refuse to native. `linear_emission_sm1.cpp` joins the production build; the
`PackedScreen` producer of each of the six SM1 pixel shaders is created once at registration
(`screen_emission_variant` line), the VS stays original. `prepare_composition` admits an exact pair in the
native screen state (blend on, ADD, ONE/INVSRCCOLOR, mask 15, separate alpha off, Z-write off, sRGB write
off, dither off — a different state is a pair refusal, an unknown one a readiness refusal; alpha test and Z
test any; sampler 0 sRGB off and one documented `GetTextureStageState` per bounded candidate refusing
PROJECTED, both readiness refusals) through policy 8 with the bound locked-prefix rectangle, derived before
admission (`before_draw` order). No bound → `packed_unbounded_refused`, no policy 8 → `packed_caps_refused`,
both native, outside the refusal histogram and never frame-stopping (producer 8 is never required); refusals
1–5 and the in-place ladder are the fade route's. `DrawPrimitive` now carries the scene permission and draw
scope. Fade, packed and exchange brackets are sequential per draw; TAA stays covered = current-only (M.red);
the untouched bullet VS is not jittered, so the rectangle is projected without jitter. Frame line:
`packed_eligible/admitted/linear/incomplete/unbounded_refused/caps_refused/region_pixels`; the witness union
takes packed rectangles (`packed_prepared`, `packed_region` lines) and packed brackets no longer skip the
sample. Fix on the way: the witness's retained readback surface is now counted by `device_references()`.
Evidence (`screen-emission-live1.json`, 12 processes, seam DLL, 64×64): functional 17 frames, 22 sources,
15 eligible / 10 admitted / 9 linear / 1 incomplete (composite fault: A|R recovered, frame stopped) / 2
unbounded (first draw after creation and after Reset) / refusal 5, PROJECTED-stage, sRGB-sampler and
dither-on draws native (readiness 2, pair 1), 441 px per bracket, 38 samples
within tolerance fraction 0.14 of the float64 law, packed alpha equal to the native blend, TAA reference
bit-exact, witness 7 sampled frames 0 outside; off control 0 eligible; straddle (injected 8×16 half) fires
on exactly frames 2/7/10/12 with 128 outside pixels and the other half missing; caps control 13 refused
native, pool of 4. Paired windows (source, median delta on−off per bracket): 1280×768 0.15–0.38 ms at
2,967 px; 1920×1080 0.16–0.49 ms at 5,959 px (at 16 DIPs 0.36 → 2.99 ms at both sizes, ≈0.165 ms per bracket).
Host: `test_screen_emission_live` 7 OK; x87 audit PASS. Native Windows behaviour remains unverified.

### Run 17 defect (2026-09-14): the per-fragment decode collapses accumulated dim fragments

Run 17 (`/tmp/x3-bottleX3-run42/`, candidate `5b92484a` from `066e18f`) admitted every bound bullet draw
(500/500 bound, 2–4 packed brackets per firing frame, `packed_incomplete` 0) and the bolts nearly vanished
in the game while every fixture passed. The bracket executes: the witness counts 5,000–9,400 covered M
pixels per firing frame inside the union with 0 outside, the 20 `packed_sample` centres sit off the thin
bolts (post = pre, both `S_OK`), and the shader-local `def c30/c31` survive the application's pixel
constant block (run 17 sets c0–c35 at every bullet draw, c30/c31 included): a live fixture frame that set
that exact block after the PS bind composed the packed law within tolerance and restored the block (kind
`c`, not kept; the D3DMetal bottle gives `def` precedence, native Windows is unverified). The root cause is
the law itself under the bullet geometry. A bolt is a chain of overlapping soft sprites drawn in one
non-indexed DIP (99 quads for the run-17 198-primitive draw); natively the encoded values accumulate
`B' = q + (1 - q) B` and are decoded once by the write-back, whereas policy 8 decodes each fragment
(`E = decode(T) h`) before accumulating `L' = E + (1 - q) L`. For one fragment on black the two coincide
(`encode(decode(q)) = q`); for n dim fragments the packed sum is `decode(T)/T` times smaller per unit of
native coverage, so the soft tail (small T, many overlaps) collapses while the bright core (T ≈ 1) barely
changes. `verification/analysis/screen_emission_display_ratio.py` measures this through the run-17
write-back (gamma-2.2 decode, EV 0, AgX, no look) on a synthetic soft sprite chain (the DXT5 sprite and
vertex alpha were not captured): overlap 1 identical laws; overlap 8, σ 3 px, spacing 2 px on black,
display luminance native / packed at 0, 3, 5, 7 px off the axis = 0.779/0.662, 0.749/0.567, 0.586/0.317,
0.202/0.051 (tail-to-core ratio 0.26 native vs 0.08 packed); gain 4 lifts the core past native (0.863) with
the tail ratio still 0.17; on a nebula (A ≈ 0.2) the tail ratio is 0.52 vs 0.32. Option (a), the
display-referred source (the linear radiance that reproduces the native display result under the active
write-back), is `decode(native B)`, i.e. the packed red lane: exact by construction for every order and
overlap and identical to native, so it carries no enhancement; option (b), a calibrated gain, cannot
restore the tail-to-core ratio. Neither is adopted here; the composition contract for the accumulated
bullet sprites is the orchestrator's decision. No production change was made for this defect.


## Step E — composition contract for accumulated sprites (ratified 2026-09-14 evening)

Decision by the orchestrator, autonomous session: the packed screen law is redefined so that the
composed bullet equals the native result by construction and the enhancement is a separate gain.
The bracket keeps accumulating the native encoded value `B_native` (the red lane, ONE/INVSRCCOLOR in
encoded space, exactly as the game does) and converts it once at publication:
`E = decode(B_native_after) - decode(B_native_before)` (the bolt's own display-referred contribution,
decoded once) scaled by `g` (`--screen-emission-gain`, default 1.0). At `g = 1` the presented bolt is
native to within the write-back's rounding (tail/core ratio preserved); `g > 1` lifts the bolt into
HDR so bloom and exposure see it. The per-fragment decode of the previous law is withdrawn for
sprite chains; unbounded/unknown draws still refuse to native. The user chooses `g` from a gameplay
comparison; nothing defaults above 1.

### Step E — implemented (2026-09-14)

The composition is now the ratified law end to end. Producer (`linear_emission_sm1.cpp`,
`PackedScreen`): `M = (1, 0, 0, a)`, `P_c = (q_c, q_c, q_c, q_c)`, no per-fragment decode (11/12
arithmetic slots instead of 41/42, no POW; `config.gain` is unused for this output). Bracket
(`linear_emission_pass.cpp`): the source writes the red|blue plane lanes only
(`COLORWRITEENABLE1..3 = 5`), so the red lane accumulates `B_native` under ONE/INVSRCALPHA exactly as
the game's ONE/INVSRCCOLOR does, the blue lane `q + (1 - q) old` is the modified flag and the green
lane keeps `decode(A) = decode(B_before)` from the plane initialization. The composite
(`generate_screen_emission_programs.py`, `linear_screen_composite_inc.h`, 184 DWORDs, 44 ALU) decodes
once: `C = encode(max(g decode(max(P.x, 1e-10)) + (1 - g) P.y, 0))`, unmodified channels copy A, alpha
is M.alpha; `def c1 = (g, 1 - g, 2.2, 1e-10)` is authored at `g = 1` and patched at attach
(`configure_packed_gain`, finite 0..16, before attach only; exactly one `def c1` or the policy is
withheld), so at `g = 1` the `(1 - g)` lane is an exact zero and the composed lane is
`decode(B_native)` bit for bit before the encode. No constant traffic per bracket, no extra pass: the
per-pixel cost moves from the fragments to the composite (+3 POW per rectangle pixel). Gain:
`--screen-emission-gain G` (0.5..8, requires `--screen-emission`) sets `X3M_SCREEN_EMISSION_GAIN`
(always explicit, default `1.0`); `capture.cpp` reads it once (`screen_emission_mode ... gain=
gain_valid=`; unparsable or out of range keeps 1), `MotionOutput::configure_screen_emission(bool,
gain)` hands it to the pass before its attach.

Evidence. Packed corpus (`run_linear_emission_sm1_packed.py`, `linear-emission-sm1-packed.json`): 864
rows (9 pairs × 24 cases × 4 schedules; schedule 3 is a new 8-layer overlapping chain in ONE DIP,
`max_layers` 8 on every full-coverage case), 720 in-domain rows with 0 failures, 144 boundary rows
with 54 operational failures (the q lane overflows to non-finite on the `overflow`/`hdr` boundary
chains, signed q on the chain; witness `failure_p0_c15_s3`); at gain 1 in domain C equals the native B
within one FP16 code, exact on 99.39 % of the 2,603,097 surviving channel values and one code low on
15,900 (`native_c_off_by_one`, the GPU POW round trip `encode(decode(B))`); helper budgets
initialize 22 / assemble_b 5 / assemble_c 44 ALU. Live fixture (`run_linear_distance_fade_live.py
--screen-emission`, `screen-emission-live1.json`, 13 processes): frame 21 draws the fade source and
then kind `c`, eight 16×16 soft-sprite quads (tint (.55, 1, .45), σ 3 px, opaque alpha) shifted 2 px
along x from pixel 8 in one DIP, rectangle (8, 24, 38, 40), 480 px, 456 changed, 8 layers; the composed
rectangle equals the native twin (the off run) within 1 FP16 code with the alpha exact, outside it A is
untouched in every run; the `screen-gain2` run (`X3M_SCREEN_EMISSION_GAIN=2`) follows
`encode(decode(A) + 2 (decode(B) - decode(A)))` on the twin within tolerance fraction 0.15 and is
brighter than native on all 1,300 bolt channels; the withdrawn per-fragment law would differ from
native by up to 0.38. Functional 22 frames / 28 sources, 53 samples within fraction 0.124 (0.243 at
gain 2), 20 eligible / 14 admitted / 13 linear / 1 incomplete / 3 unbounded, witness 0 outside.
Bracket cost, paired windows (median delta on−off, source window, 16 DIPs, per bracket): the retained
step C record (`screen-emission-live1.json` at `883797d`) 0.165 / 0.160 ms at 1280×768 and
0.164 / 0.172 ms at 1920×1080 (2.64 / 2.55 and 2.62 / 2.76 ms per window); after 0.171 / 0.164 ms and
0.167 / 0.164 ms (2.73 / 2.63 and 2.67 / 2.62 ms), i.e. no added cost. `packed_samples.luminance_changed`
falls from 6 to 0: step E leaves a native-valued pixel exactly native at g = 1 (the pre/post centre
samples differ in alpha only), whereas the step C law changed the centre's luminance. `screen_emission_display_ratio.py`: the step E
column equals native at every distance and background (tail/core 0.259 = native; gain 2 → 0.343, gain
4 → 0.441 with the core lifted to 0.868 / 0.930). Host: x87 audit PASS (224 reachable). Native Windows
behaviour remains unverified.
