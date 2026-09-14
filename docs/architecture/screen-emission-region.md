# Packed screen emission inside the region bracket

Design note, 2026-09-14, for ratification at `2b14bba`: whether the qualified packed screen law
([screen-emission-overlap.md](screen-emission-overlap.md)) is integrated through the ratified in-place region
bracket ([linear-distance-fade-region.md](linear-distance-fade-region.md), policy 4, run 11). Not implemented.


Ratified by the main session 2026-09-14 as the route to pursue, conditional on the
bullet-bound feasibility (step B): unbounded draws refuse to native, never full
viewport; the first action is the capture query for the row-19 vertex buffer's lock
flags and position layout, then the disassembly brief of section 6 only if the
captures cannot answer it.
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
