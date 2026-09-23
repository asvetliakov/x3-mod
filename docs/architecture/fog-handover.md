# Fog hand-over after a sector change

Design note (2026-09-23). R1, R2 (a)+(b), the docked walk ([Implementation](#implementation-2026-09-23))
and R3 ([R3 implementation](#r3-implementation-2026-09-23), on the read side established in
[sector-transit-order.md](../reverse-engineering/sector-transit-order.md)) are implemented; R2 (c) is not. Owning notes: [volumetric-fog.md](volumetric-fog.md) (stored-density range option,
card replacement), [fog-density-runtime-integration.md](fog-density-runtime-integration.md)
(levels, ramps, worker), [sector-fog.md](../reverse-engineering/sector-fog.md) §11 (detector).
Evidence: `verification/results/run271-music-keep/fog_entry_timeline_out.txt` (+ `.py`) and
`fog_handover_spans_out.txt` (+ `.py`, written for this note) over the run271 session log.

**Outcome.** Cut the visible vanilla-fog time from 3.5–4.6 s to an estimated 0.3–0.6 s with two
proxy-only changes, then to about two frames with a third: (R1) at a cold start the far
readiness steps to 1 instead of ramping over 90 frames, so the cards can be masked in the first
frame the far need box is resident, the same frame the medium reaches full density (removes the measured 2.1–2.9 s ramp, which is 56–64 % of the
hand-over); (R2) a cold-fill path in the density cache: far need box only, whole-atlas upload in
the first latch, optionally a coarse-first far pass (removes most of the measured 1.3–1.9 s fill);
(R3, option A) prefill the far level during the transit stall frame (measured 4.4–6.2 s for the two gate
transits, 12.5 s for the first entry; the save load at frame 1070 also stalled 12.5 s) from the destination sector's record, centred at the sector origin,
confirmed or discarded by the detector's first Ready sample. B (mask on near/mid readiness) is
rejected because it is the one option that shows less fog than vanilla by construction; C (card
fade) and D (residency across families) are follow-ups, not the answer. The docked view needs a
bounded parent-chain walk in the anchor cross-check, implementable separately.

## 1. What run271 measured

Frame numbers from `fog_entry_timeline_out.txt`; seconds from `frame_end` qpc
(`fog_handover_spans.py`). Three sector entries with fog, all first visits in the session.

| Entry (family) | Stall frame before the first sector frame | `density_filling` (fill to far-drawable) | Far ramp (89–90 frames) | Cards masked at |
| --- | --- | --- | --- | --- |
| 2634 bluewell, new key | 2633→2634: 12,525 ms (after 6.0 s of `no_cockpit` frames at 45.9 fps, after a 6,501 ms stall) | 28 frames, 1.29 s (21.7 fps) | 2.14 s (41.6 fps) | +3.5 s |
| 5329 bluewell, same key, `residency` epoch | 5327→5328: 4,374 ms (one `no_cockpit` frame) | 14 frames, 1.56 s (9.0 fps) | 2.93 s (30.4 fps) | +4.56 s |
| 8984 foggreenoutlands, new key | 8982→8983: 6,146 ms (one `no_cockpit` frame) | 66 frames, 1.94 s (34.1 fps) | 2.49 s (35.7 fps) | +4.48 s |

All measured. What the numbers establish:

- **The ramp is the larger part.** `kReadinessRampFrames = 90` (`fog_density_cache.h`), advanced
  by 1/90 per `step` once the far need box is resident (`fog_density_cache.cpp` 510–517); the
  cards are unmasked while `ready_far < 1` (`volumetric_fog_begin_frame`, `fog_card_ready_`) and
  masked only at `ready_far >= 1`. `far_ready` lands exactly 90 frames after `density_filling`
  ends in all three entries. During those frames the medium stacks on the cards at growing
  density, then the cards cut: the user sees vanilla, then more than vanilla, then a step down.
- **The fill is worker- or cadence-bound, not node-bound.** The far need box is
  `2*floor(200000/4096)+2 ≈ 99` nodes per axis, 0.97 M nodes (1.09 M with `kFirstFillSlack` 2);
  the worker generated 4,194,304 nodes (both full windows) in 2,168.6 ms busy time on entry 1
  (`far_ready` row), 1.93 M nodes/s on one thread. The far need box alone is therefore about 0.55 s
  of CPU (inferred), and the worker already schedules the far need box first (`fog_density_cache.cpp`
  377–383). The measured 1.3–1.9 s to drawable exceeds that, most clearly for entry 3 (66 frames at
  34 fps); the remainder is upload cadence (`kDefaultUploadBudget` 1,065,024 B per latch, a
  4,260,096 B far atlas, publication one frame after the copy) and possibly worker starvation by the
  FEX-translated main thread. Unresolved; the timing mode's `volumetric_fog_cache_frame` row
  (per-frame nodes/s and upload bytes) settles it.
- **A transit is one synchronous stall frame of 4.4–6.2 s** (12.5 s for the first entry and for the
  save load at 1070) during which the
  engine renders nothing and the proxy sees only resource-creation calls. The frame after it
  samples `no_cockpit` (registry handle 0); the sector is Ready one frame later. The worker thread
  is idle throughout the stall because it has no identity to fill.
- **Same identity, resident cache: immediate.** Undocking at 15252 masked the cards at +0.03 s
  (16749: +0.52 s, one unexplained `card_refused` run of 37 frames with no cache event). The second
  bluewell sector (5329) shares the placement key with the first (same background record index,
  profile, recipe), so the cache was not re-keyed, but the camera landed outside the retarget band
  of the old window: `residency` epoch, refill, ramp, 4.56 s. Residency alone does not buy a fast
  hand-over; the ramp and the fill do.
- **Rule today.** "Cards masked only after the far ramp completes so the hand-over never shows less
  fog than either medium alone" (volumetric-fog.md, stored-density option). The proposal keeps the
  invariant and drops the ramp from the mask condition, not the residency.

## 2. Recommendation

Three parts, deliverable in this order; each is useful alone.

**R1. Cold-start step instead of ramp (replace mode only).** In `DensityCache::step`, when the far
level's `ready_[1]` is 0 and `hard` (need box resident on the GPU) becomes true, set `ready_[1]` to
1 in that frame when the config asks for it (`FogDensityConfig::cold_ramp_frames = 0`; the proxy
sets 0 in `--volumetric-fog-cards replace`, 90 in `keep`). The fine ramp (`lambda`, LOD only) and
the soft ramp-down stay at 90. Reasoning on the invariant: `ready_far == 0` implies the cards were
unmasked, so a step to 1 shows exactly the same two frames as today's ramp end (vanilla cards, then
the medium at full density), minus the 90 stacked frames; at no frame is there less fog than
vanilla. In `keep` mode there is no mask and the ramp still hides a pop, so it stays. Applies to
sector entry, the `residency` epoch (a jump in the same key), `sample_gap` and Reset alike.
Removes 2.1–2.9 s (measured ramp). Hot path: one branch per frame in `step`. No shader, hook or
data work. TAA: unchanged, one `fog_card_transition(2)` reseed at the mask frame (today the medium
appeared as slow content over 90 frames and the reseed came at the mask; now both happen in one
frame, one reseed). Native Windows: identical code, no D3D change.

**R2. Cold-fill path in the cache.** (a) Cold upload budget: at an epoch (`invalidate`, `gpu_reset`,
identity change) let the next latches upload the whole far atlas (4.26 MB) in one or two
`UpdateSurface` batches instead of 8 tiles per frame; the frames after a transit are 30–110 ms
long anyway (measured 9–22 fps). Estimated cost 1–3 ms of copy on those frames (not measured).
(b) Keep the far need box strictly first (already so) and defer the fine need box until the far
need box is published, so no fine slab competes for the single worker during the cold fill.
(c) Optional coarse-first far pass: generate the far need box at 8,192-unit spacing (1/8 of the
samples, about 0.07 s CPU at the measured rate), write each coarse node replicated 2×2×2 into the
bricks, publish, then overwrite with the exact pass. Shader unchanged (it samples the same atlas);
the amount of fog is unbiased in expectation (point samples of the same field without the
8-point prefilter: more variance, same mean); the blockier far field lasts under a second beyond
20 km. Needs a per-slab coarse/exact state and a second publication per slab in the cache; it is
the part of R2 with real code risk and can be dropped if (a)+(b) already reach the target.
Estimated fill to drawable after R2: 0.3–0.6 s (a+b), 0.15–0.3 s with (c); estimates from the
0.55 s CPU bound and 1–2 upload frames, to be measured. Wrong-family risk: none (same identity).
TAA: none. Windows: same code.

**R3 = option A. Prefill during the transit stall.** See §3.A. Estimated result when the
destination is readable during the stall: the far need box is complete in the CPU cache before
the first sector frame (0.55 s of worker time inside a 4.4–6.2 s stall), the atlas uploads in the
first latch under R2(a), and the cards are masked at the first or second sector frame. Requires
one diagnostic flight first to learn which anchor exposes the destination during the stall.

Combined target: R1+R2 well under one second (estimated 0.3–0.6 s); R1+R2+R3 about two frames.
None of the three adds per-draw work; none touches the card mask mechanism (`COLORWRITEENABLE`,
`prepare_fog_card`).

## 3. Options

### A. Start the fill during the load from the destination sector (recommended as R3)

- **Where the engine knows the destination.** Gate, jumpdrive and scripted moves all end in
  `INS_CockpitSetSectorSpace` (dispatcher `FUN_0042d340` case `0xb`: `MOV [ESI+0x54],EAX; CALL
  0x00420360` at `0x0042d670`/`0x0042d673`, EAX = destination sector object, ESI = cockpit;
  sector-fog.md §11.2–11.3), which "takes effect in the same frame". A save load rebuilds the
  cockpit (`0x0043f420`, SECT/SOBJ chunks; loading-orchestration.md §3). What is **not** known from
  the notes: whether the `+0x54` write precedes the body/texture load inside the stall frame (then
  the whole stall is lead time) or follows it (then nothing is gained over today's one-frame sample).
  The `no_cockpit` sample after the stall says the active-control handle is 0 at the stall's end,
  so the direct chain may be dead during the stall even if the sector is written early.
- **Data the prefill needs.** Sector object → `+0x13c` background index → record `+0x44` → name →
  family profile (the compiled 14 or the file table), i.e. `fog_sector_frame` minus the cockpit,
  camera and anchor checks; then `fog_sector_placement(index, profile, recipe)` gives the identity
  and world offset without any cockpit. A window origin: the far window is camera-centred, and the
  ship's position in the new sector is not known until the engine places it (`[ship+0x70]+0x30..0x38`,
  sector-collide.md §1, if the ship has already moved during the stall). Fallback: centre the far
  window at the sector origin; the window is ±63 nodes (258 km) and the need box ±49 (200 km), so
  any camera within 57 km of the origin is fully covered and a farther gate keeps the intersection
  and fills the missing slab under R2. Fine is not prefilled (LOD only, ±2 km margin).
- **Hook and cost.** First step needs no trampoline: the proxy already intercepts every resource
  creation during the stall; sample the chain at most once per 250 ms once the current frame is
  older than `fog_density_gap_ms` (500 ms): at most about 25 bounded reads per 6 s stall, zero in an
  ordinary frame. If the registry handle is 0 during the stall, re-walk the registry for the
  previous frame's handle value (value-only, from `Sample::handle`) to reach the same cockpit row
  and read `+0x54`. Only if both are dead: a trampoline at `0x0042d670` (8 bytes, two instructions;
  EAX/ESI live; hook-site validation and a `disassemble` pass on `0x00420360`'s callers). Reads
  during a load are the §11.4 hazard window (type-table reallocation across a load): every hop is a
  committed-page validated read with the class-word and bounds checks, so a torn read yields
  `ReadFailure`/`BadIndex` and no prefill, never a crash.
- **Authority and confirmation.** The prefill is not authority. It calls `configure(identity)` and
  posts a synthetic camera; `fog_sector_` stays unset until the detector's first Ready sample, whose
  placement key either equals the prefilled one (`configure` is a no-op for the same identity; R1
  steps readiness when the real camera's need box is resident) or differs (`configure` invalidates:
  today's behaviour). A wrong family therefore costs one wasted fill on the worker thread during a
  stall the engine is spending anyway; it can never draw the wrong family, because drawing is
  gated by the detector's `fog_sector_.current(frame_)` as today.
- **TAA:** none (invisible until the mask, which is R1's single reseed). **Windows:** same EXE,
  offsets and reads; no Wine dependency. **Risk:** the worker thread runs during the engine's
  single-threaded load; on a machine with few cores it steals from the load. Core count of the
  user's machine is not recorded here.
- **Verification:** a diagnostic line at each sampled point in a stall frame (chain status, sector,
  index, name, ms until the stall ends) in one flight with two gate transits establishes the lead
  time; host test in `test_fog_density_cache.py` for prefill-then-confirm (same key: no invalidate,
  ready steps; different key: invalidate, ready 0); then the flight of §5.

### B. Mask the cards progressively when near/mid are ready — rejected

The stored field has two levels of *detail*, not of range: fine (512-unit nodes, sampled to
30 km) and far (4,096-unit nodes, 20–200 km, taper 150–200 km). There is no near/mid level that
becomes ready earlier: the worker fills the far need box first because it gates drawing, and
`fine_ready` arrived 0.4–1.2 s **after** `far_ready` in all three entries (measured). Nor can the
far ramp "finish under our medium": the shader has no per-node residency, the window origin
advances only when every slab is resident, and `density_drawable` requires the far need box. If the
cards were masked on fine residency alone, the medium would cover 0–30 km and the cards' 200 km
veil would be gone: with sigma_eff = 2.5e-6 × 1.5 per unit (bluewell config row, S = .03) and a
uniform density (S = .03 is the strength the runtime-integration note records for the user, assumed here),
tau(0–30 km) = 0.11 against tau(0–200 km, tapered) ≈ 0.66, so about 17 % of the
medium's optical depth for the whole far fill (0.6–1.9 s), far less fog than vanilla in the band the
user looks at. That violates the rule by construction, and the fill order it presumes is the
opposite of the real one. Loses.

### C. Cross-fade the cards out instead of a cut — follow-up, not the answer

Replace mode masks the cards with `D3DRS_COLORWRITEENABLE = 0` around the native draw
(`prepare_fog_card`/`finish_fog_card`); there is no colour override today, and "masking/replacing
the PS" was rejected in the card-replacement design. A fade is nevertheless feasible without shader
ownership: the `nebulafog` pixel shader `f7e0b6647a3bfa62` is "uniform-scaled texture RGB"
(shader-family-review.md), so one constant register scales the card colour, and for a screen blend
(`ONE/INVSRCCOLOR`) scaling the source colour by k is the exact fade
(`out = k·src + dst·(1 − k·src)`). Mechanism: on admitted card draws only, `SetPixelShaderConstantF`
of that register to k·current before the draw and restore after (2 calls per card, 1–4 cards per
frame, for N fade frames); the current value comes from the proxy's constant shadow or one
`GetPixelShaderConstantF` at admission. During the fade the medium is at full density, so there is
never less fog; the `fog_card_transition(2)` reseed can then be dropped (a fade is slow content).
Unknown: the register index (local shader dump; not documented). It shortens nothing: it polishes
R1's step. Do it only if the flight after R1–R2 shows the step as objectionable.

### D. Keep the cache resident across sector changes for recent families — follow-up

The residency cache holds **one** identity: `DensityCache::configure` with a different
`CacheIdentity` calls `invalidate()`, which drops both levels (`fog_density_cache.cpp` 462–471);
per identity it holds one 128³ window per level, 4 × 4,260,096 B of CPU memory (`cpu_bytes =
17,040,384` in the `volumetric_fog_range` row: two CPU caches plus two SYSTEMMEM stagings) and
8.1 MiB of GPU atlas. Retaining N previous identities costs 8.1 MiB of CPU cache each (stagings can
be shared) and still needs the GPU re-upload on return (8.5 MB; 1–2 frames under R2(a), 8 today) and
a far window centre within 57 km of the re-entry point; the fine window (±2 km margin) never
survives a different gate. All three run271 entries were first visits, so D would have gained
nothing in the flight that raised the question, and the same-key case (5329) shows that a retained
identity with a moved camera still refills and ramps. A one-slot LRU (the previous key) is a cheap
addition later for back-and-forth gate hops. Loses as the primary.

### E. A faster fill — adopted as R2

Bounds (measured unless marked): worker 1.93 M nodes/s on one thread (entry 1); far need box
0.97–1.09 M nodes → 0.5–0.57 s CPU (inferred); far atlas 4,260,096 B against a 1,065,024 B per-latch
budget → 4 latches plus one publication frame → 0.15–0.55 s at the measured 9–35 fps after entry.
The measured 1.3–1.9 s to drawable exceeds the sum, so cadence or starvation dominates (§1). Levers,
in order of payoff over risk: the cold upload budget (a), fine deferral (b), coarse-first far pass
(c), and last a second worker thread (halves CPU, competes with the game's main thread; not before
starvation and the core count are measured). "24 steps × 128³ samples" is not the bound: the
generator writes 4,096-node bricks and the first fill covers the need box plus slack, not the full
window, before drawing is allowed. TAA, wrong-family and Windows: none, none, same code.

## 4. Docked view: what accepting a parent-of-parent sector would take

`sector_background::sample` reads the cockpit's sector directly (`cockpit+0x54`) and cross-checks it
against the ref object's parent: `ref_object = [cockpit+0xc]`, `ref_sector = [ref_object+0x54]`,
`anchor_check = (sector == ref_sector)`; a mismatch makes the status `AnchorMismatch`, which
`fog_sector_frame` turns into `enabled = false` (reason `anchor_mismatch`): cards forwarded
natively, medium skipped, and every dock/undock changes `same_key` (status differs), so the cards
disarm and TAA reseeds on both edges (run271: 25 s and 14 s docked spans, cards observed
throughout). While docked, the ship's `+0x54` is its parent object, the station, not a sector
(`[obj+0x54]` = parent, class word `[obj+0x48]`, sector class 1; sector-collide.md §1, sector-fog.md
§11.2 hop 3).

Implementation, separately: after reading `ref_sector`, if `[ref_sector+0x48] != 1`, read
`[ref_sector+0x54]` again, at most two more hops (ship→station→sector; ship→carrier→station→sector
is the three-hop case), each hop a 4-byte-aligned committed-page read with the class check; compare
the first class-1 object found with `sector`; no class-1 object within the bound keeps `Mismatch`.
Cost: at most two extra bounded reads per frame while docked, none in flight. Risk: the identity
still comes from `cockpit+0x54`, unchanged; the walk only widens the consistency check, so a wrong
family needs `cockpit+0x54` to be stale *and* the walk to end at the same stale object. The real
hazard is a non-sector object whose `+0x54` is not a parent pointer for its class; the alignment,
committed-read and class checks turn that into "no match", never a dereference fault, but it would
silently keep the docked view vanilla for that class. Also unverified: that the docked external
camera at `cockpit+0x58` carries the same fog pair (the run271 docked samples report
`anchor_mismatch`, not `camera_mismatch`, so the camera check passed or was inactive). What settles
it: one docked diagnostic line (ref object class, each hop's class and pointer, hop count) in a
flight that docks at a station and inside a carrier; then the change plus a host case in
`test_fog_cards.py`/the sector-background tests for the two- and three-hop chains and the bound.

## 5. Verification

- **Host.** `test_fog_density_cache.py`: cold-start step (ready 0 → 1 in the residency frame with
  `cold_ramp_frames = 0`, ramp unchanged at 90; soft ramp-down unchanged), cold upload budget applied
  once per epoch, fine deferred until the far need box is published, coarse-then-exact overwrite
  (if (c) is built), prefill-then-confirm for both key outcomes. `test_fog_cards.py`: the mask
  condition still requires `ready_far >= 1` and `density_drawable`.
- **Fixture (Wine, `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py …`).** The
  detached `FogPass` fixture with static caches: a cold epoch followed by a resident frame draws at
  full density in that frame; the whole-atlas upload keeps the readback byte-identical to the
  budgeted upload after it completes; Reset re-upload unchanged.
- **Flight (one, two gate transits into fogged sectors, replace mode).** `fog_entry_timeline.py`
  on the session log: for each `volumetric_fog_sector` row with a fog profile, `cards first:
  suppressed` within 0.6 s (R1+R2) or 2 frames (with R3); the `density_filling` run ≤ 15 frames; no
  frame with `applied=1` and cards suppressed while `ready_far < 1` (add `ready_far` to the
  `volumetric_fog_cards` change line so this is checkable without timing mode); one TAA
  `FogTransition` invalidation per entry, not more; the `no_cockpit`-frame diagnostic of §3.A
  reporting the lead time per transit. User judges the step at the hand-over; if objectionable, C.

## 6. Unknown, and what settles it

1. Whether `cockpit+0x54` (or the sector object through another anchor) is readable during the
   stall frame and how early: the §3.A diagnostic flight; if the chain is dead, a `disassemble`
   pass on the callers of `FUN_0042d340` case `0xb` and on `0x00420360` for the order of the sector
   write and the body load.
2. Why the fill takes 1.3–1.9 s against a 0.55 s CPU bound: one flight with
   `--volumetric-fog-timing` (`volumetric_fog_cache_frame` per frame) over two entries.
3. The `nebulafog` colour-scale register (option C): the local shader disassembly.
4. The 37-frame `card_refused` run after the second undock (16749): the same timing flight.
5. Related, out of scope: a menu sets the registry handle to 0 (`no_cockpit`), which in a fogged
   sector would forward the cards and skip the medium for the menu's duration with a reseed on
   both edges (run271 shows a 6.0 s `no_cockpit` span at 45.9 fps, in a clear sector, so invisible
   there). Worth its own look.

## 7. Options considered and why they lose

- **B**, progressive mask: violates the invariant by construction and presumes a fill order the
  worker does not use (§3.B).
- **C** as the primary: shortens nothing; it only softens R1's step, and its register is unknown.
- **D** as the primary: helps only revisits within 57 km of the last window centre; none of the
  measured entries would have benefited; 8.1 MiB per retained identity.
- **A with a trampoline first:** the polling from resource-creation calls the proxy already owns
  reaches the same data at no per-frame cost and no hook-site risk; the trampoline is the fallback
  if the chain is dead during the stall.
- **Keeping the ramp and only speeding the fill (E alone):** leaves 2.1–2.9 s of ramp, the larger
  measured term, and the "vanilla, then more than vanilla, then a step down" sequence.
- **Masking the cards at ramp start with the medium ramping from 0:** less fog than vanilla for
  90 frames; rejected.
- **A shipped per-sector far bake:** already rejected in the runtime-integration note (world-anchored
  field, camera-following window); the stall-time prefill gets the same effect from the worker.

## Implementation (2026-09-23)

R1, R2 (a)+(b) and the docked walk (§4), each behind its own switch; R2 (c) (coarse-first) is not
built; R3 has its own section below. With the switches off, the atlas contents, the uploads and the
readiness are the previous ones byte for byte (`FogDensityConfig::handover_*` default false,
`sample(read)` without a walk argument reads exactly the legacy reads); the hand-over
instrumentation still runs with them off (two steady-clock reads per cold start and per hand-over,
one `GetThreadTimes` per worker epoch and one per far publication, the `volumetric_fog_handover`
line), so the A/B flight gets the same numbers from both sides.

| Switch | Environment | Launcher (default) | Scope |
| --- | --- | --- | --- |
| R1 cold-start step | `X3M_FOG_HANDOVER_STEP` | `--fog-handover-step` / `--no-fog-handover-step` (on) | stored range |
| R2 cold fill | `X3M_FOG_HANDOVER_COLDFILL` | `--fog-handover-coldfill` / `--no-fog-handover-coldfill` (on) | stored range |
| R3 prefill | `X3M_FOG_HANDOVER_PREFILL` | `--fog-handover-prefill` / `--no-fog-handover-prefill` (on) | stored range |
| Docked walk | `X3M_FOG_DOCKED` | `--fog-docked` / `--no-fog-docked` (on) | `--volumetric-fog`, either range |

The launcher always writes all four (`1` unless the `--no-` form is given); the DLL treats an
absent variable or anything but exactly `0` as on, and logs one `volumetric_fog_handover_mode`
line at init. A `--fog-handover-*` form without `--volumetric-fog-range stored`, or a `--fog-docked`
form without `--volumetric-fog`, is a launcher error.

**Cold start.** `DensityCache::invalidate()` is the cold start: `configure` with a new identity
(the proxy's sector re-key), `FogPass::invalidate_density` (the `sample_gap` load epoch) and the R3
prefill. The switches in force at that moment (`set_handover`, latched by
`FogPass::prepare_density` before `configure`) decide the whole fill, until the far readiness
reaches 1. Warm refills (the `residency` epoch: a jump or cut that loses the resident box in the
same identity) keep the 90-frame ramp and the budgeted uploads. A `gpu_reset` (device Reset, a
failed upload) keeps the ramp only after the hand-over; one during a cold start is part of that
cold start and still steps. This narrows §2 R1, which proposed the step for Reset and the
residency epoch too.

**R1.** In `step`, the far level's soft branch sets `ready_[1] = 1` instead of adding the ramp while
the cold start is pending; the fine ramp and the ramp-down are unchanged. The cold start ends when
the far readiness reaches 1. The card mask condition is untouched (`ready_far >= 1` and
`density_drawable`). **Same-frame masking:** the card policy decides warm-up at frame start from the
previous frame's readiness, so without more the step frame would draw the full medium over
unmasked cards for one frame (the legacy ramp had the same frame at its end). In the step frame,
`prepare_volumetric_fog_density` (at the HDR redirect latch, which precedes the scene's draws; a card
drawn before it in the frame is refused, never masked) calls
`FogCardPolicy::arm_on_cold_step()` when the report is due with the step and `ready_far >= 1`: the
cards are armed at once and masked in the frame the medium reaches full density. The frame's own
transaction is the proof: a masked card in a frame whose fog pass fails faults the replacement
until Reset, as before; a refused card, an inactive or faulted policy is not armed.

**R2.** At a cold start with the cold fill the request carries `cold_hold`: the worker generates the
far first-fill slab (need box plus `kFirstFillSlack`) and extends the far box until it holds the
need box grown by the readiness guard (a prefill centred elsewhere, a moving camera), then parks on
the condition variable, so no fine slab or window growth competes (§2 R2 (b)). `level_dirty(1)` is
false while nothing is published, so the far staging is not locked during the fill.
`take_uploads` hands out nothing of the far level until a published box holds the posted camera's
need box plus the guard (so the latched level is steppable at once), then one rectangle covering
the whole far atlas (4,260,096 B, one `UpdateSurface`, past the per-latch budget), clears every far
tile's dirty and reload state, marks the budget spent so the fine level waits for the next latch,
and releases the hold (serial bump and notify). A `gpu_reset` before the latch is covered by the
same whole-atlas copy; a latch lost to `confirm_uploads(false)` is taken again (and the report
counts both); switching the cold fill off during a fill releases the hold at the next latch and
the budgeted path continues; a new identity during the hold starts over with its own single latch.
Cost: one 4.26 MB `memcpy` on the render thread and one 4.26 MB `UpdateSurface` per cold start,
estimated 0.5–2 ms for the copy plus 1–3 ms for the upload in that one frame (review estimate, not
measured; `--gpu-sync-timing` brackets `prepare_volumetric_fog_density` as `FogFill`, which measures
it); nothing per frame otherwise (one branch in `step`, two in `take_uploads`, one in `level_dirty`).

**Hand-over line.** One `volumetric_fog_handover` line per cold start (at most 256 a session), in the
frame the far readiness reaches 1, with or without the switches: `step`, `coldfill`, `whole_atlas`,
`arm_frame`, `frames` and `ms` (cold start to hand-over), `drawable_frame` / `drawable_ms` (far need
box resident on the GPU), `fill_ms` (the worker published the far need box), `fill_busy_ms` (worker
generation wall time until then), `fill_cpu_ms` (worker thread CPU time until then:
`GetThreadTimes`, scheduler-tick resolution), `busy_ms` (worker generation wall time to drawable),
`latches` and `upload_bytes` (upload latches to drawable). Reading it for §6 item 2:
`drawable_ms - fill_ms` with `latches` is the upload cadence; `fill_busy_ms` well above
`fill_cpu_ms` is starvation of the worker thread; `fill_ms` well above `fill_busy_ms` is the worker
waiting (wake or lock), not generating. After an R3 prefill the cold start is the prefill, so `ms`
includes the rest of the stall.

**Docked walk.** `sector_background::sample(read, anchor_walk_limit)` (3): when
`[ref_object+0x54] != cockpit+0x54`, `walk_anchor` reads the class word `[obj+0x48]` and follows
`[obj+0x54]` for at most 3 hops (up to four objects examined: `[ref_object+0x54]` and three
parents; depth counts hops), each an aligned read through `engine_memory::read`; the first class-1
object decides (`found` if it is the cockpit's sector, else `other_sector`); `bound`, a null or
misaligned parent, or a failed read refuse. The shared sample keeps the raw `anchor_check`
(`mismatch` stays visible on the `sector_background` diagnostic line) and reports the walk in
`anchor_walk`; `anchor_refused()` (a raw mismatch the walk did not resolve) gives `anchor_mismatch`
and the fog's `sample_mismatch` (native cards). The identity still comes from `cockpit+0x54`; a
direct match reads exactly the legacy reads. Cost while docked: two reads per hop plus one (host
witness). One `volumetric_fog_docked` line (walk, depth, ref object, its parent, the last object,
the sector, `fallback=none|native_cards`), at most 256 a session, when a walked span starts or its
outcome changes. The `sector_background` diagnostic alone (without the fog) never walks.

**Verification.** `verification/analysis/test_fog_handover.py` over
`verification/probe/fog_handover_host.cpp` (85 checks with R3: cold step vs warm ramp vs legacy
ramp; the single whole-atlas latch with the GPU copy equal to the CPU cache and a settled field
equal to a from-scratch fill; the hold, switch-off, Reset and a lost latch during the cold fill; a
new identity during the hold; the latch waiting for the need box; the real worker thread parked and
woken, the far staging never offered while unpublished; the same-frame card arming; walk depths 0
to 3, bound, other sector, null/misaligned/unreadable parent, cycle, span reporting; launcher and
wiring checks); `sector_background_context_host.cpp` gains the docked span (22 checks);
`fog_density_pass_fixture.cpp` gains the cold hand-over case, run under Wine on the merged tree
(`HANDOVER` row, `verification/results/fog-handover/fixture-wine/`). Ledger:
[volumetric-fog.md](../verification/volumetric-fog.md), "Fog hand-over R1+R2 and docked walk" and
"Fog hand-over R3 and review fixes". Native Windows: documented `UpdateSurface`, `GetThreadTimes`,
`GetTickCount64` and engine reads only; no Wine-specific path.

## R3 implementation (2026-09-23)

The read side of [sector-transit-order.md](../reverse-engineering/sector-transit-order.md) §5,
without a trampoline; option A of §3.A with the global object list in place of the cockpit route
(the cockpit is freed at the start of the stall and `+0x54` is written last).

**Stall gate and poll sites.** Present stamps `GetTickCount64()` and the render thread
(`fog_prefill::Gate::present`). The `CreateTexture` and `CreateVertexBuffer` hooks (installed for the
prefill even without CPU telemetry) call `fog_prefill_poll` after the native call: outside a stall
the poll's own cost is one `GetTickCount64` and one compare (`now - present_ms <= 250`); in a stall
the thread is checked before the 250 ms slot is taken (another thread never spends it), then at most
one poll per 250 ms on the render thread, with `GetLastError` saved and restored, after the
executable identity check and an `engine_memory::next_frame()` (the stall reallocates).
**The hook envelope is new cost without telemetry:** every `CreateTexture` and `CreateVertexBuffer`
now pays the full existing hook envelope (`CpuCallBoundary` with its FNSAVE/FRSTOR, the recursive
hook mutex of `HookGuard`, the ownership admission bookkeeping, the `frame_timing` scope), in loads
and in flight alike. Not measured; the next flight counts creations per frame (CPU telemetry on: its
Texture and VertexBuffer counters) to size it. `--no-fog-handover-prefill` leaves the two hooks to CPU telemetry alone.

**Walk** (`src/proxy/fog_prefill.h`, `fog_prefill::walk`). `M = *0x0060850c`; `node = [M+0x10]`
(tailpred); backward through `[node+4]` for at most 8 nodes, stopping at 0 or the head `M+8`; the
first node whose type word `[node+0x48]` is class 1 subtype 0 (`0x00000001`; cut-scene spaces have a
non-zero subtype and are walked past) is the candidate, then: `[node+0x9c]` (u16) `== 0xcafe`, one
32-byte read at `+0x130` with the scene `+0x130 != 0` (the index `+0x13c` is taken from it), the id
`[node+8] !=` the last Ready sector's id; then the §11.4 record recipe shared with the detector
(`sector_background::read_record`: count, table, one row, one name). Every read is aligned and
validated through `engine_memory::read`; at most 24 reads (bounded at 25; host witness). Outcomes:
`found`, `head`, `bound`, `dead`, `no_scene`, `same_id`, `no_manager`, `malformed`, `read_failure`,
`loading`, `bad_count`, `bad_index`, `bad_record`. The last Ready id is read once per sector and
again at the first Ready sample after any break (one read per transit), not per frame.

**Start.** A found sector with a fog profile gives the placement identity exactly as the first Ready
frame will (`fog_sector_frame` → `fog_sector_placement`: key, recipe, offset) and
`FogPass::prefill_density` → `DensityCache::prefill`: `configure(identity)` (a cold start with the
switches of the last stored frame) and the camera posted at the sector origin; no device call, no
allocation, never waits. With the cold fill the worker fills the far need box around the origin and
parks; after the stall the real camera is posted and the held worker extends the far box to the
arrival's need box before the whole-atlas latch. The start is recorded as prefilled and unconfirmed
(`fog_prefill::Record`) only when the camera reached the worker (`DensityCache::prefill` returns
true); a missed lock logs `action=not_posted` and the next poll retries. A repeated poll finding the
same pending key does nothing (`already_started`); a found sector whose key is the resident field's
(`fog_density_key_`, another sector sharing its background record) is re-centred at the destination's
origin as a cold start (`recentred`; Run 73 B case A below, which replaced the earlier `current_key`
skip). Nothing is drawn from it:
drawing stays gated by `fog_sector_.current(frame_)`, set only by the detector. Before the first
stored frame of a session the poll constructs the `FogPass` object and `prefill_density` starts the
worker (no device call; Run 73 B case D); a refused path or a pending Reset logs `not_posted`.

**Confirmation.** While a prefill is pending, the transit's `sample_gap` invalidation is deferred.
At the detector's first Ready sample, `fog_prefill::decide`: the same placement key and recipe keep
the fill (`volumetric_fog_prefill event=confirmed lead_ms=… same_sector=… same_id=…`; the frame's
`configure` is then a no-op and R1/R2 finish it); a different key, or a sample without a fog profile,
invalidates as a load gap does (`event=discarded`, epoch `prefill_discarded`). The sector pointer and
id are reported, not required (the key owns the field).

**Logs.** One `volumetric_fog_prefill event=poll` line per poll (walk status, steps, reads, node, id,
index, family, `stall_ms` into the stall, action, key) and one per decision, at most 512 a session.

**Cost and risk.** In a stall: at most 24 validated reads per 250 ms and one far need box of worker
time (about 0.55 s of one core at the measured rate, fog-handover §1, inferred) during a load the
engine spends anyway; on few cores it competes with the load (not measured). A wrong read costs one
fill and is discarded at Ready; it cannot draw. Save loads expose the sector only at the end of the
stall (sector-transit-order §3), so R3 gains nothing there; R1+R2 still apply. A hitch of more than
250 ms without Present in ordinary flight (not a transit) also opens the gate: the walk then finds
the flown sector (`same_id`) or no candidate, and starts nothing; a started prefill in such a hitch
would be confirmed or discarded at the next Ready sample like any other (a `recentred` one of the
flown sector's own key would cost one cold fill, which needs its id to be unread: it is read at every
Ready sample after a break, so this is not expected). The lead time per
transit is what the `stall_ms` of the first `found` poll against the stall length measures in a
flight. Native Windows: the same EXE offsets and documented calls.

**Tests.** `fog_handover_host.cpp`: walk on synthetic list layouts (tail, eighth node within 24 reads,
cut at eight, head, empty list, cut-scene subtype walked past, freed marker, no scene, last Ready id,
no/unreadable manager, misaligned node, unreadable link, table loading, index out of range, bad
record), the stall gate (nothing before the first Present or within 250 ms of it, one poll per
250 ms), the decision (confirm on the same key and recipe, discard otherwise), and the cache prefill
(far need box at the origin, held, the Ready `configure` keeps it, hand-over after the extension
and one latch, settled field equal to the destination's, a different key starts over). The poll's
wiring, the deferred gap and the poll sites are source checks in `test_fog_handover.py`.

## Run 73 B findings and fixes (2026-09-23)

Run273 (Run 73 B, Run73 DLL from `4ff60c8a`, all four switches on) exposed four gaps. Evidence:
`verification/results/run273-fog-bolts/handover_gaps.py` (+ `_out.txt`, written for this section) over
the session log, and the triage files beside it. All figures measured unless marked; frames are
session frame ids.

**Common cause behind B, C and D: the arrival frame posts the sector just left.** The owner latch
posts "the previous scene end's camera" (fog_pass.h), which at the first frame of a new sector is the
source sector's position. `DensityCache::step` posts it, the worker does a full first fill around
it (1.09 M nodes, ~0.6 s at the measured 1.8 M nodes/s), the next frame's post retargets the window
to the real position, the intersection is empty (unrelated coordinates) and a second first fill runs.
Frame 16258 (D): 0 -> 2,349,664 nodes at the latch (`handover_gaps_out.txt`, node deltas), twice a
first fill of 1,092,727, and the run's hand-over line reports the worker busy for far longer than the
far need box's own fill (24848: `busy_ms=1742.1` against `fill_busy_ms=597.6`; the 16308 line, not
tracked here, showed the same shape: the "~700 ms outside the fill" of the brief is that wasted
first fill). Frame 24765 (B): 2,184,796 nodes after arrival, twice a first fill, although the
confirmed prefill had already filled the origin box during the stall; the stale post retargeted the
window away from it and discarded it. Fix, two parts:
`volumetric_fog_sector_sample` drops `fog_density_camera_valid_` on a sector change, a gap or a
prefill decision (this frame's latch posts nothing; the scene end re-validates the camera with the
new sector's and the next latch starts the fill there), and `DensityCache::apply_invalidate_locked`
clears `request_.camera_valid` (a new epoch has no camera until one is posted for it; `invalidate`
also empties `posted_need_` so the next `step` posts even a camera that did not move). Cost: one
frame between the sector's first frame and the fill start (the arrival frame is 40-110 ms long
anyway); the fill then starts at the right place once.

**A. Same-family gate (bluewell -> bluewell, 19555).** Not a cold start because the transit was no
`sample_gap`: the stall frame 19554 sampled `no_cockpit`, which stamped `fog_density_sample_frame_`,
so 19555 was consecutive and the 5.4 s of wall clock did not count; the prefill poll saw
`current_key` and did nothing; the first step at 19556 found the need box (190 km from the source
position, another sector) not resident and ran the warm `residency` ramp: epoch at 19556, `far_ready`
at 19676, 120 frames and `ms=3460.0` (`handover_gaps_out.txt`). Fix: (1) a Ready sample of another sector object than the last Ready one
(the sector token, or `[sector+8]` when both ids are known: the destination can reuse the freed
source's address) is a transit; with the same placement key while the field is configured it is a
cold start (`invalidate_density`, epoch `transit`: step and cold fill), a different key re-keys at
the latch as before; (2) the prefill no longer skips the resident key: `DensityCache::prefill` of
the resident identity is an `invalidate` (cold start) followed by the origin post, the poll logs
`recentred`, and the confirmation keeps that fill. The origin is still the best single guess (the
arrival at 19555 was 190 km from it, at 24765 228 km: X3 gates sit at the sector edges), so the
arrival post then extends the origin box under the hold instead of filling from scratch.

**Transit identity (Run75 bridge fix).** Rule (1) as first written also fired on a mere reallocation:
the route bridge's `heap_token_change_keeps_key_cache_and_image` (token 0x1000 -> 0x7000, same index
and family, same placement key) logged epoch `transit` and refilled. In run273 the 19555 jump kept
the background index (2 -> 2) and changed the token (`119301a8` -> `6cb11818`) and the id at
`[sector+8]` (2317 -> 3221; measured from the session log's `volumetric_fog_sector` and prefill poll
rows). The placement key already mixes the index, so the id is the only identity left: a transit is
`fog_prefill::other_sector(id, last_id)`, both ids known (non-zero) and different. A token change
with the same id, or with an unread id, is taken as a reallocation: no epoch, and the resident field,
image and camera stay (the bridge passes no id). A same-id reallocation is inferred from the bridge's
synthetic case, not observed in flight: the one observed same-sector rebuild (run273 docked load at
33817, foggreenoutlands index 14, id 4370 -> 3926, token `6c7c4640` -> `6cb0fd88`) changed the id and
is covered by the sample gap; a same-sector rebuild with a new id and no gap would cold-start, the
acceptable direction (a refill, never a stale field). The camera drop follows the same rule (transit, gap or a
decided prefill): as first written it fired on any token change, so the latch of a sector change
without ids skipped `configure` and the bridge failed `sector_change_rekeys_whole_far_node_offset`.
With ids known (production, prefill on) a sector change still drops the camera and re-keys one frame
later; the bridge, without ids, checks the same-frame re-key only. The ids are read when the prefill
option is on (its default with the stored range); with it off a same-family transit takes the warm
residency ramp and a sector change posts the previous scene end's camera once, as before 72645b5e.
The poll's `plan` uses the same rule (`plan(record, key, recipe, resident, id, ready_id)`, review
F3): a found resident key with the same id or either id unread is `current_sector` (the id-unread
hitch and a reallocation with the ready id unread keep the field); only `other_sector(id, ready_id)`
re-centres; the walk's token plays no part. Proof: the value rule and the plans in
`fog_handover_host.cpp` (`transit_needs_another_known_sector_id`, `plan_token_change_*`); the cache
case `HEAP_TOKEN_CHANGE` applies the rule by hand, and the production sample's wiring (no density
call, camera kept for a reallocation, one invalidation for the 2317 -> 3221 pair) is `run273_transit`
in `fog_card_motion_cases_inc.h` (`test_fog_cards`); the route bridge 110 names.

**B. Gate into foggreenoutlands (24765), confirmed prefill "re-keyed".** It was not re-keyed: the
`sector_key` epoch line at the latch came from the proxy's own key copy (`fog_density_key_` still
held bluewell's; `configure` in the cache was a no-op), and the refill was the stale post above. Fix:
a confirmed prefill adopts its key (`fog_density_key_ = key`, the confirm line carries `adopted=1`),
`rekeyed` compares the key only, the prefill start is an epoch (`prefill`) so `far_ready` and the
hand-over line count from the cold start, and the stale post is skipped. Remaining cost for an
arrival far from the origin: the far box grows from the origin box to the arrival's far target; under
the cold hold it now grows to the far target only, not to the window's edge (`work_once`, `want`
bounded by the far target: for the 24765 arrival 795,144 nodes against 1,103,336 for a first fill,
host witness `PREFILL_ADOPTED`; with window-edge slabs the same geometry gives 1,372,189,
`verification/results/fog-handover/run273-fixes/window_edge_slabs.py`, whose far-target figure
803,765 is within 1.1 % of the witness, a boundary-rounding difference), about 0.44 s of
worker time (inferred from 1.8 M nodes/s) plus one latch, against 1.15 s measured in the flight. Two
frames after arrival needs the arrival position during the stall, which is written last
(sector-transit-order section 2 row 8): open.

**C. Docked save load (33817): cards refused for 384 frames after the cold step.** The scene-end
path passed every gate up to `density_drawable` in 33817-33859 (`density_filling`), the far level
stepped at 33860 and armed the cards, and from 33860 the first card of every frame was refused
before or at the readiness check (`refused=1 ready=0`, `suppressed=0`), so the pass skipped as
`card_refused` until the undock (34244, 7/7 suppressed at once). The same docked view in flight
(31504-33815, 7 cards a frame) was masked throughout, so the condition is one the load changes and
the undock's view change clears; the rows of the two states differ in nothing the gates read
(field diff of every row type between 31510 and 33870: only camera, scene statistics and
`set_rt 14 -> 4`, `sun_shadow_lane_frame stamped 1 -> 0`). **Not pinned from the code** (measured:
the refusal happened; inferred: it is a card-time gate or readiness component that the scene end
does not evaluate, most likely a render-state or frame-structure difference after the load, since
every readiness component except `density_drawable` was true at the scene end of the same
frames). Fix delivered: the diagnostic. `prepare_fog_card` names the frame's first refusal
(`fog_off`, `pair`, `shape`, `scene`, `queries`, `composition`, `owner`, `taa`, `linear_depth`,
`sun_lane`, `pass_done`, `frequency`, `states`, `mask`, or the readiness verdict: the prerequisite's
own name, `resources`, the parameter check's name, `density_unprepared`, `density_ramp`,
`density_drawable`) and the `volumetric_fog_cards` line carries it as `refusal=`, per frame in
timing mode and at every change (60-frame spacing) otherwise; the gates are the same checks in the
same order, one branch each, no new per-card work. The next flight's first `refusal=` value after a
docked load settles it.

*Run 75 A (run278, Run75 DLL from db13d929) pinned the gate.* From the cold step (11692, `far_ready`,
`warmup=0`) to 12806 every frame's first card was refused at `gate:states` (1,115 frames, measured:
`verification/results/fog-handover/run278-docked-states/rows.sh`); at 12807 the same 7 cards a frame
were masked 7/7 with no other change in any per-frame row (draws 159 -> 164 at frames 12692 -> 12812, `set_rt` 4 both sides,
no resync, `rs_invalidations=0`), and the transits and the new-game start of the same flight were
accepted. So the draw is the fog card (pair, declaration, shape and stream frequency all passed; the
same 7 draws are masked after the undock) and one of the twelve render states the gate compares
differs while docked after a load. Which one is not in the log (no card row carries states; the
motion-route capture was off), so it is inferred from the engine side: the card material's own text
is `g_ZEnable 1` / `g_CullMode 2` and the dust pass overrides both to `ZENABLE 0` / `CULLMODE NONE`
in every in-flight card (run174, 422 draws, [sector-fog.md](../reverse-engineering/sector-fog.md)
sections 4 and 10); the docked-at-load view is the one scene set up by the load rather than by the
sector entry, and the undock's view change rebuilds it. Fix (`fog_card_match.h`): `ZENABLE` takes 0
or 1 and `CULLMODE` NONE or CW, the two documented sources of the card's state; `ZWRITEENABLE 0`,
`STENCILENABLE 0`, colour mask 7, `ALPHATESTENABLE 0`, `FILLMODE SOLID` and the screen-blend triple
stay exact, so a masked card still writes nothing (the replacement changes only `COLORWRITEENABLE`,
restored in `finish_fog_card`), and the pair, shape, scene and frequency gates are unchanged.
Diagnostic: the refused vector is printed as `volumetric_fog_card_states` (z, zwrite, atest, blend,
mask, cull, stencil, fill, src, dst, op, sepalpha; 12 state fields after device and frame) at most once per 300 frames, on the refusal
path only, so the next docked load names the state if it is not z/cull. Host:
`run278_docked_states` in `fog_card_motion_cases_inc.h` (the (1, CW), (0, CW), (1, NONE) vectors
admitted hooked and unhooked with the in-flight card's native calls; z 2, cull 3, cull 0, zwrite 1,
stencil 1, alpha test 1 refused as `gate:states`; the row's 300-frame spacing).

**D. New game into a fogged sector (16258).** Three parts. (1) The prefill could not start: the
poll found bluewell 7.1 s into the 12.6 s stall but `prefill_density` refused (`not_posted`)
because no stored frame had created the density worker (and no `FogPass` existed). Now the poll
constructs the `FogPass` (no device call) and `prefill_density` creates and starts the worker
(portable, no D3D), takes the hand-over switches from the proxy, and `FogPass::attach` keeps a
worker that exists (its `detach` released everything else); the first `prepare_density` finds it,
creates the atlases (their `gpu_reset` is covered by the whole-atlas latch, as before). (2) The
~700 ms of worker time outside the fill was the stale-camera first fill (above). (3) The 612 ms
frame 16307 was not the latch: `frame_phases_slow` puts 599 ms in `pre_render` (before BeginScene,
engine time) with the slowest hooked call at 1.9 ms and `views` at 12.8 ms, which bound the latch;
frames 24847 (188 ms) and 19596 (707 ms) have the same shape, 1.2-2.4 s after each arrival, and the
worker generated 0.95-1.2 M nodes during each such stall (the big upload after it is the backlog).
Inferred: the engine's autosave after a gate transit or a new game. The latch stays whole (the
docked load's latch frame was 12 ms, measured); nothing was split. Expected first fog frame on a
new game: the arrival frame's latch posts nothing (camera stale), the next latch posts the origin
camera and takes the whole-atlas latch (the box was filled in the stall), the frame after steps and
masks the cards: arrival + 2 frames, host witness `NEW_GAME_PREFILL` (latch in the first posted
frame, ready 1.000 in the next); the 600 ms engine stall a second later is untouched.

**Verification.** `fog_handover_host.cpp` (95 checks): `INVALIDATE_PARKS` (the worker parks after
an invalidate and fills only around the next posted camera), `TRANSIT_COLD` (a same-key transit
steps with one whole latch, largest step 1.000, settles bit for bit), `RECENTRE_PREFILL` (the
resident key re-centred at the origin as a cold start, confirmed keeps it, stepped 5 frames after a
20 km arrival), `PREFILL_ADOPTED` (nodes to the latch 795,144 < a first fill 1,103,336, one first
fill; the stale post 2,357,917 with two), `NEW_GAME_PREFILL`. `fog_card_motion_cases_inc.h`:
`run273_transit` (only an id change of the same key invalidates, once: token-only and same-id
reallocations keep the camera too, the run273 2317 -> 3221 pair invalidates; another key only drops
the camera, the first sector only drops the camera), host witness `HEAP_TOKEN_CHANGE` (no epoch, far atlas unchanged), `run273_prefill_adopt` (key adopted, camera
dropped; discarded invalidates once), `run273_card_refusal` (12 named refusals, kept for the frame,
reset at begin, unprepared is warm-up not refusal). Wiring checks in `test_fog_handover.py`.
