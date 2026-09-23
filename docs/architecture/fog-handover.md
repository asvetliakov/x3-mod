# Fog hand-over after a sector change

Design note (2026-09-23), for ratification. No code, build, Wine or game work is authorized by
this note. Owning notes: [volumetric-fog.md](volumetric-fog.md) (stored-density range option,
card replacement), [fog-density-runtime-integration.md](fog-density-runtime-integration.md)
(levels, ramps, worker), [sector-fog.md](../reverse-engineering/sector-fog.md) §11 (detector).
Evidence: `verification/results/run271-music-keep/fog_entry_timeline_out.txt` (+ `.py`) and
`fog_handover_spans_out.txt` (+ `.py`, written for this note) over the run271 session log.

**Outcome.** Cut the visible vanilla-fog time from 3.5–4.6 s to an estimated 0.3–0.6 s with two
proxy-only changes, then to about two frames with a third: (R1) at a cold start the far
readiness steps to 1 instead of ramping over 90 frames, so the cards are masked in the frame the
far need box becomes resident (removes the measured 2.1–2.9 s ramp, which is 56–64 % of the
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
