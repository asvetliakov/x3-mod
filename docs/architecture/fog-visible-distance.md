# Fog visible distance: a long distance fade instead of a 22.5 km wall

Design note (2026-10-25), for the user request "increase volumetric fog visible distance by x3 or
even x4-x5, less dense for distant fog (a fade?), to avoid fog pop-ups". Implemented the same day, both steps
at once with the orchestrator's amendments (§6 "As built"; uncommitted worktree, not installed, not flown). Owning
notes: [volumetric-fog.md](volumetric-fog.md) ("The look", "Stored-density range option"),
[fog-density-runtime-integration.md](fog-density-runtime-integration.md) (levels, windows, worker),
[fog-gpu-cost.md](fog-gpu-cost.md) (march cost), [fog-handover.md](fog-handover.md) (sector entry).
Numbers: `verification/results/fog-visible-distance-2026-10-25/` (`derive.py` -> `derive.txt`,
`log_rows.py` -> `log_rows.txt` over the user's 2026-10-25 native log). [M] measured, [I] inferred.
Units: render units, 5000 per km.

**Recommendation.** Replace the 13-22.5 km smoothstep with a long inverse-square fade, density
`w(s) = 1` to 13 km, then `(13 km / s)^2`, ending in a smoothstep over the last quarter of the cap,
and extend the cap to **3x (67.5 km)** with 40 extra geometric "tail" bins. Deliver it in two
steps behind one key, `fog_distance` (1.0-3.0, default 3; 1 = today's programs, bit-identical):

1. **Step 1, shader and constants only:** the fade law and the tail bins on the existing far
   level, cap 39 km (x1.73). The far window already holds 40 km of density, so no memory, fill or
   worker change.
2. **Step 2, a third "horizon" density level** (spacing 8192 units, same 128-node window, atlas
   format and generator) for the 39-67.5 km tail. It costs 8.5 MB of address space and about 0.3 s
   of background fill. (Amended 2026-10-25: both steps were built together; step 1 is what draws
   when the horizon level is off or fails, §6.)

The fade, not the factor, removes the pop. A cloud that today goes from barely visible to
half density in 3.5 km of travel takes 36 km at 3x [I]. Under SETA that is about ten times slower on
screen at the same speed.

## 1. What bounds the visible distance today, and what the pop most likely is

Shipped default (release 1.5.1, the user's `x3m.ini` leaves every fog key at its default; log
`volumetric_fog_range mode=stored ... levels=2`, `march_scale scale=4` [M]):

| Bound | Value | Where |
| --- | --- | --- |
| Column cap: every ray, sky or geometry, ends here | 112,500 = **22.5 km** | `FogLookTuning::sky_cap`, `src/renderer/fog_look_math.h:43` -> c25.w |
| Distance fade: density x `1 - smoothstep(13 km, 22.5 km, s)` | 65,000 = **13 km** start | `taper_start`, same line -> c35.yz; applied in `march_depth`, `src/fog/fog_density_field_inc.h:248-251` |
| Horizon constant (sky ray length before the cap) | 200,000 = 40 km | `fog::kTaperEnd` -> c3.w, `src/renderer/fog_pass.cpp:1316` |
| Bins | 24 over 0-2.4 km (ds 0.1 km), 40 over 2.4-22.5 km (ds 2512 = 0.5 km) | `march_depth`, `FOG_FAR_BINS` 40 |
| Fine level | 512-unit nodes, 128^3 window (+-13 km), sampled to 6 km (LOD blend 4-6 km) | `fog_density_generator.h:25-58`, `kLevelReach` |
| Far level | 4096-unit nodes, 128^3 window (+-52 km), **need box reach 40 km**, retarget at 9 nodes (7.4 km) | `fog_density_cache.h:31-35` |
| Cards | `replace` masks every matched native card, at any distance, once the far ramp completes | `fog_card_policy.h`, `fog_card_mask.h` |

So the visible fog ends at **22.5 km**, which is the cap. The far window could already serve 40 km. The
cap came from the 2026-09-21 range decision (ledger `docs/verification/volumetric-fog.md`, "Fade
range"): a full-density 35 km law veiled the sky (view D sky 1-T median .0003 -> .103, empty share
.49 -> .23 [M, fixture]) and its 4700-unit far bins shimmered (half-bin T change up to .059 [M]).
Both failures come from **full density** to the cap; the fade below addresses both.

**Most likely pop: the cap fade itself [I].** The smoothstep compresses the whole appearance into
a few km. Weight w >= .05 begins at 21.2 km and w = .5 at 17.75 km: **3.46 km of travel**, with a
maximum slope of .158 per km (`derive.txt` [I]). The fog's sky is a sphere around the camera, so as the ship
flies, cloud banks grow out of nothing at a fixed distance ahead. At 1x a 300 m/s ship needs 12 s
for 3.5 km; under SETA (on 65-98 % of the user's frames) at 3-5 km/s game speed [I] it takes 0.7-1.2 s.
That reads as a pop.

Ruled out or secondary, with the evidence from today's log (`log_rows.txt` [M]):

- *Level or window hand-over in flight:* no in-flight loss of far residency. All 4 `epoch
  reason=residency` rows fall in the frame of an `hdr_target` re-creation, which is a device Reset
  (alt-tab), 4 of 4. The 10 other epochs are sector prefills. Fine-level dips (lambda ramping to far-only
  under SETA) are not logged outside `--volumetric-fog-timing` (`volumetric_fog_cache_frame`
  `ready_fine`), so a detail "morph" at 4-6 km cannot be excluded from this log. It would look
  like a cloud sharpening near the ship, not one appearing far ahead.
- *Density fill at sector entry:* fog becomes drawable `far_ready` 745-3119 ms (median 1529)
  after the epoch, `fine_ready` 1153-4311 ms (median 2259), cold far fill 457-540 ms, 12 entries
  [M]. That is a whole-sector appearance after a jump, the hand-over note's subject, not a
  distance pop.
- *Card masking:* applied at sector entry only, distance independent.
- *Shaft pops* (engine shadow submission, ledger "Why it appears and disappears") affect light
  shafts, not cloud presence.

One question to the user settles which one it is: "does a cloud appear ahead of you while you
fly inside a sector, or does the fog appear all at once after a gate?" The first is this note.
The second is `fog-handover.md`.

## 2. Options for x3, x4, x5

### The fade law (shared by every option)

`w(s) = 1` for s <= 13 km (the accepted near look is unchanged there), else
`w(s) = (13 km / s)^p * (1 - smoothstep(.75 C, C, s))` with cap C, p = 2. One constant row, no
fetch, about 6 ALU per bin. Inverse-square makes a cloud's density change at the rate of its
angular size (twice the relative rate of perspective growth), so approach reads as natural. The
end smoothstep only removes the last .02-.04 of w, so it has no visible edge. `derive.txt` [I]:

| Cap | p | w at 22.5 / 45 / 67.5 km | w >= .05 from | w >= .5 from | appearance band | equivalent full-density path |
| --- | --- | --- | --- | --- | --- | --- |
| today 22.5 km | smoothstep | 0 / 0 / 0 | 21.2 km | 17.75 km | **3.5 km** | 17.75 km |
| 39-40 km (x1.73-1.78) | 2 | .33 / 0 / 0 | 35.8 km | 18.4 km | 17.4 km | 21.2 km |
| 67.5 km (x3) | 2 | .33 / .083 / 0 | 54.4 km | 18.4 km | **36.0 km** | 23.1 km |
| 90 km (x4) | 2 | .33 / .083 / .037 | 58.1 km | 18.4 km | 39.8 km | 23.9 km |
| 112.5 km (x5) | 2 | same | 58.1 km | 18.4 km | 39.8 km | 24.3 km |
| 67.5 km (x3) | 1.5 | .44 / .155 / 0 | 59.2 km | 20.6 km | 38.6 km | 26.8 km |
| 90 km (x4) | 1.5 | .44 / .155 / .085 | 75.6 km | 20.6 km | 55.0 km | 28.4 km |

The equivalent path (the integral of w) measures the sky veil for a statistically uniform field.
The rejected 35 km law had 32.5 km, today has 17.75. With p = 2, x3 adds 30 % and x4/x5 add
almost nothing visible, because w <= .037 beyond 67.5 km. Real visibility past 3x needs p = 1.5,
which brings the veil close to the rejected law. **So with p = 2, x3 is the useful ceiling, and x4/x5
cost more than they show.**

### Tail bins (shared)

Keep today's 24 + 40 bins over [0, 22.5 km] and add **40 geometric bins** from 22.5 km to C in a
second `[loop]`. Each bin is wider than the last by a fixed ratio: 1.45 % at 39 km (ds 1630-2856
units), 2.78 % at x3 (ds 3133-9143), 3.53 % at x4 (3967-15,329) [I]. Shimmer scales with w x
(ds / node). At x3 the coarsest bins sit where w < .07, so the expected half-bin change stays at or
below today's .0102 (ledger, 22 km default) [I, to be measured]. The tail loop skips the shaft
lookup (beyond the cascades' lateral extent fog is always lit, ledger "Coverage") and the domain
warp (1400 + 500 units, sub-node at these spacings). It keeps density, coverage waves, the
sun-ward tap and extinction, so it costs about 0.6 of a near bin [I].

### Options and their costs

| | A: far level only (step 1) | **B: + horizon level, 8192 units (step 2, recommended)** | C: + horizon level, 16384 units | D: 256-node far window |
| --- | --- | --- | --- | --- |
| Reach | 39 km (x1.73) | x3 (x4 with 7-node retarget slack) | x5 | x3-x5 |
| Address space (CPU cache + SYSTEMMEM staging) | 0 | +8.52 MB | +8.52 MB | +~60 MB (8x per copy) |
| VRAM (DEFAULT) | 0 | +4.26 MB | +4.26 MB | +~30 MB |
| Need-box fill at sector entry (2.29 M nodes/s [M]) | 0 | 0.61 M nodes, 0.27 s (x3); 1.44 M, 0.63 s (x4) | 0.36 M, 0.16 s (x5) | 4.8-21.7 M, 2.1-9.5 s |
| Whole window, background | 0 | 2.10 M, 0.92 s | same | 8x |
| SETA: retarget work | 0 | 8 slabs (131 k nodes, 60 ms) per 13 km of travel at x3; 1.4-2.3 % of the worker at 3-5 km/s [I] | negligible | large |
| March (both q2/q4 programs and repair) | +40 tail bins | same | same | same |

GPU march: today's `fog_march` was 2.68 ms at 5120x1440, scale 4 (Run 77 C, measured on the Mac
CrossOver GPU, `fog-gpu-cost.md`). 40 tail bins at about 0.6 of a near bin add about 37 % of the march,
**about +1.0 ms on that GPU** [I]. The composite is unchanged, and repair marches only on depth-class edge
pixels. On the user's RTX 5070 Ti the cost is **not measured**. Expect less in absolute terms, but
it must be measured. Slots: march 425 -> about 560-590, repair 510 -> about 650-680 [I]. The project
plans against 32,768 (AGENTS.md), so this is not a constraint. First-draw compile stays far below
the measured 0.65 s cold at 4k slots (tens of ms [I]). Per-frame CPU: one more constant row
(step 1); one more level in `step()` and a shared upload budget (step 2). No allocation, lock or
validation on the draw path in either step.

Memory against the native evidence: the Run184 rows show free virtual address space between
164.8 MB (minimum) and 182.3 MB (p5). The median is 522.9 MB, and 84 of 390 rows are below 300 MB [M,
`verification/results/fog-prepare-oom-2026-10-21/run184-memory-rows.txt`]. The low points are the
game's own saw-tooth before it purges. The horizon level's 8.52 MB is 5.2 % of the minimum
[I]. It fits, provided it is allocated once at stored-path attach with the other two levels
(today's `cpu_bytes=17,040,384` [M] -> 25,560,576), never per sector. Only the DEFAULT texture is
recreated at Reset, where the one native out-of-memory prepare happened (2026-10-21). A failure
there must drop to the step-1 law with one log row, not fault the fog. Whether a DEFAULT-pool
texture costs address space on native D3D9 is unknown (§5).

Native Windows: steps 1 and 2 use only what the stored path already qualifies:
`CreateTexture` SYSTEMMEM/DEFAULT RGBA16F, `LockRect`/`UpdateSurface`, ps_3_0 with a 9th sampler
(ps_3_0 has 16), a plain Win32 worker. No new capability or format class is needed. The user plays native,
so the acceptance flight is native verification.

### Why the others lose

- **C (16384-unit horizon level, x5):** its node spacing (3.3 km) puts only two nodes in the field's
  6.55 km mass period, and the 8-point prefilter (offsets +-delta/4) does not low-pass the 1.64 km
  modulation. Distant banks would be a different, aliased shape that morphs at the far->horizon
  blend. It also buys nothing visible: with p = 2, w <= .037 beyond 67.5 km.
- **D (bigger far window):** the atlas law is built on 128 nodes (32 Z groups x 4 lanes, 129-texel
  tiles). 256^3 is 8x memory per copy (about 34 MB x 3), which does not fit the 165 MB trough. Any
  non-power-of-two window rewrites the address law and its fixtures.
- **Coarser far level (8192 instead of a third level):** frees the third atlas but coarsens every
  cloud from 6 km out, which is the look the user accepted. Rejected.
- **Distant-fog panorama** (march 40-90 km into a low-resolution cube map, refreshed every few km):
  this is the cheapest per frame, but it adds a pass, targets, Reset handling and its own refresh
  cross-fade, which would cause a new pop. It is more mechanism than the user wants.
- **x4/x5 with p = 1.5:** real visibility to 75-90 km, but the equivalent path is 28-30 km, close
  to the 32.5 km law that veiled the sky. It also needs C for x5.
- **Fade only, no extension (cap 22.5 km, earlier and longer fade):** this shortens today's fog.
  It moves the edge closer instead of removing it.

## 3. Recommendation, config, acceptance, kill criterion

**Recommended:** the p = 2 inverse-square fade, 40 geometric tail bins, and `fog_distance = 3`
(cap = value x 22.5 km, clamped to 1.0-3.0). Values up to 1.73 use the far level (step 1). Above that the
horizon level is needed (step 2). `1` keeps today's programs and constants, bit-identical, which is the
kill switch. The key is the only config surface: p, the 13 km start and the bin count are baked
constants, like the rest of `FogLookTuning`. (Amended 2026-10-25: no hotkey; the user removed the
in-game comparison keys, so the A/B is `fog_distance` edited in `x3m.ini` between launches. Step 2 ships
with step 1, so there is no 1.73 clamp in the default.)

**Implementation scope:**
- Step 1: `fog_density_field_inc.h` (fade law, tail loop), the four march/repair programs (q2, q4),
  `fog_look_math.h` (constants), c3.w = cap, the key and the launcher.
- Step 2: a third level in the generator and cache (the third delta, reach = cap + 4900 for warp and
  tap, retarget 8 nodes). Worker priority: far need, fine need, horizon need, then window growth.
  The horizon level gets its own 90-frame readiness ramp on the tail weight and stays out of the
  cold-fill hold, so first fog after a jump is not delayed. (As built, §6: `kLevelCount` stays 2 with
  `kMaxLevels` 3, and a warm horizon that loses its lead goes before the fine need.)

**Verification that proves it:**
- Host reference (`tools/analysis/fog_density_shader_reference.py` on views D, Z, X, 160x96, same
  method as the 2026-09-21 range table). Gates:
  - output within s <= 13 km unchanged;
  - view D sky 1-T median <= .02 and empty share >= .40 (today .0003 / .49; rejected .103 / .23);
  - half-bin T change max <= .0102 (today's default);
  - an approach sweep, the maximum change in a test bank's 1-T per km of travel <= 1/3 of today's.
- Step 2 host:
  - the horizon level's 8-point node law against a 2x2x2-far-node box reference, w-weighted T
    p99 <= .003 (if it fails, the box law costs 8x generation: 2.1 s need box and 7.4 s window at x3 [I]);
  - three-level cache tests, including a SETA travel case with 2 s game steps at 5 km/s: the
    horizon readiness never reaches 0, and the fine need box is filled no later than with two levels.
- Program creation through the native D3D9 validator harness
  (`verification/results/hull-widen-native-validator/`), slots logged.

**Acceptance the user can judge in one flight** (native, 5120x1440, `perf = 1` for the FPS overlay),
in a fogged sector such as today's `litcube151` family:
1. Under SETA, fly at a cloud bank 30-60 km away. Distant banks show as faint shapes long before
   they are near. No bank appears or thickens from nothing within a few seconds.
2. `fog_distance = 1` in `x3m.ini` for one launch and 3 for the next, at the same spot. The open sky
   does not look milky compared with 1, and the FPS difference is acceptable to the user.
3. Log: no `volumetric_fog_cache event=refused`, no `volumetric_fog_prepare_retry` row attributable
   to the fog, and `fine_ready` medians comparable to today's 2259 ms. Step 2 adds a `horizon_ready ms=` row.

**Kill criterion:**
- If the user still sees clouds pop in flight, the diagnosis is wrong. Set the default to 1 and triage with
  `volumetric_fog_timing = 1` (`ready_fine` dips) instead.
- If the fog costs more than 10 % FPS against 1 in the A/B, or the sky looks veiled, set the default to 1.
- After step 2, any horizon-level allocation failure or out-of-memory prepare in a flight sets the
  default back to 1.73.

## 4. Native cost summary

| | Step 1 | Step 2 |
| --- | --- | --- |
| Address space | 0 | +8.52 MB (5.2 % of the 164.8 MB minimum) |
| VRAM | 0 | +4.26 MB |
| Time to first fog after a jump | unchanged (median 1.53 s [M]) | unchanged; tail ramps in +0.27 s after fine [I] |
| Worker under SETA | unchanged | +1.4-2.3 % [I] |
| `fog_march` | +about 37 % (+1.0 ms on the Run 77 GPU [I]) | same |
| Slots / compile | march about 590, repair about 680; tens of ms [I] | same |

## 5. Unknown, and what settles it

- **Which pop the user means:** the one question in §1. A flight with `volumetric_fog_timing = 1` (ini; `X3M_VOLUMETRIC_FOG_TIMING`)
  settles in-flight readiness dips.
- **RTX 5070 Ti march cost:** an `X3M_GPU_SYNC_TIMING` session (`fog_march` rows), 1 vs 3 at the same spot.
- **Veil and shimmer at 3x:** the host reference gates above. p = 2 is a starting value, not a measured choice.
- **Horizon node law aliasing:** the box-reference host screen above. This is the only step-2 look risk.
- **Address-space cost of DEFAULT textures on native D3D9:** the `volumetric_fog_memory` row
  (`avail_virtual`, `texture_mem`) before and after the horizon atlas is created, in the debug tier.
- **SETA game speed in fog:** the 3-5 km/s figure is inferred. A `seta_manual_flight` speed field
  or camera-position delta per frame in the acceptance log measures it.

## 6. As built (2026-10-25; uncommitted worktree, not installed, not flown)

Both steps, with the orchestrator's amendments: no hotkey; `fog_distance` 1-3, default 3, in the schema, the
regenerated `x3m.ini` template and the launcher (`--fog-distance D`, requires `--volumetric-fog-range stored`, so it is
refused under `--vanilla`); a horizon failure falls back to the 39 km cap and never latches the fog off. Evidence and
producing scripts: `verification/results/fog-visible-distance-2026-10-25/`; ledger `docs/verification/volumetric-fog.md`,
"2026-10-25: long distance fade". [M] measured, [I] inferred.

**Selection** (`fog_look_math.h`, `fog_distance_*`). `fog_distance` <= 1 (and NaN or unparsable, logged `invalid`)
draws today's `fog_density_{march,repair}_look[_q4]` with today's 36 constant rows and 8 samplers and starts a
two-level cache: those four programs recompiled byte-identical on this host [M, all 54 generator programs `--check`
PASS]. Above 1: the distance programs. Above 1.7333 (`cap > 195,000`): the horizon level as well. The cap is
`value x sky_cap`, at most 195,000 without the horizon level.

**Shader** (`fog_density_field_inc.h`, `#ifdef FOG_DISTANCE`, FOG_LOOK only; programs
`fog_density_{march,repair}_distance[_q4]`; the composite is shared). c3.w = c25.w = C, c35.yz = .75 C and 1/(.25 C),
c36 = the horizon level's camera modulo and 1/8192, c37 = (S0^2 = 65,000^2, tail start 112,500, horizon readiness, 0),
s8 = the horizon atlas (LINEAR; the far atlas is bound while there is none, never read at readiness 0). The 24 + 40 bins
keep today's layout over [0, min(L, 112,500)] with `w = min(1, S0^2/s^2) x (1 - smoothstep(.75 C, C, s))`. The tail is
a second rolled `[loop]` of 40 bins at geometric centres over [112,500, L] (`q = exp2(log2(L/112500)/40)`, centre
`112500 sqrt(q)`, width `centre (q-1)/sqrt(q)`, centre *= q): no shaft lookup, no warp, coverage waves, the sun-ward
tap and both extinction octaves. The far share hands over to the horizon share across 175,000-195,000 (smoothstep h);
the horizon share is `h x readiness` and the blend is renormalised over what is present, so an absent or unready
horizon level is never sampled and the far share fades out by 195,000.

**Programs** [M, `validate_programs.json`, Microsoft ps_3_0 table / native SysWOW64 validator through WSL interop]:

| Program | Slots | Texture instr. | Loops | Validator |
| --- | --- | --- | --- | --- |
| march_distance / _q4 | 717 / 717 | 23 | 2 | 0 messages, End S_OK, 1 c# per instruction |
| repair_distance / _q4 | 803 / 803 | 28 | 2 | same |
| today's march_look / repair_look (unchanged) | 425 / 510 | 15 / 20 | 1 | same |

The renderer refuses only a malformed distance program (0 slots); a device that refuses one at creation draws today's set
(`volumetric_fog_distance event=refused reason=program fallback=distance_1`, sticky until detach). Today's programs keep
their 512-slot check.

**Cache** (`fog_density_cache.{h,cpp}`). `kLevelCount` stays 2 (every stored frame has fine and far; the host witnesses
and the generator's `address()` keep their meaning) and `kMaxLevels` = 3 sizes the per-level arrays; level
`kHorizonLevel` = 2 exists only after `enable_horizon()` (below). Horizon reach 342,400 (the 3x cap + tap +
warp) for every cap above 1.7333, retarget 8 nodes. Deviation from §3, for SETA: with the strict order "far need, fine
need, horizon need" a fine level that cannot keep up (the fine need box moves 0.8 nodes a frame at 5 km/s) takes every
slab and the tail drops out. As built: (1) the far need (cold-fill hold unchanged, the horizon never under it); (2) a
warm horizon (its box held need + 8 nodes once in the epoch) that lost its 8-node lead: one slab ahead of the fine
need, and while the fine need is open the next slab is the fine level's, so the two alternate until the lead is back
(the fine level waits at most one horizon slab, <= 32,768 nodes or about 14 ms [I], per fine slab; a 10 km SETA step
costs the horizon about 4 such slabs); (3) the fine need;
(4) a cold horizon's need + lead (after the fine need, so first fog after a jump is not delayed: `fine_resident` frame
equal with and without the level [M]); (5) window growth fine, far, horizon. Horizon slabs are at most 2 nodes thick
(<= 32,768 nodes, about 14 ms at 2.29 M nodes/s [I]); the first fill starts from a seed around the camera node.
Uploads take far, then fine, then the horizon under the same byte budget. The horizon has its own 90-frame ramp
(`FrameState::ready[2]`); a Reset drops it with the others.

**Fail closed.** The horizon CPU cache (4,260,096 B) is allocated by `DensityCache::enable_horizon`, which
`prepare_density` calls once the distance programs exist (a prefill's worker has two levels; `cpu_cache` on failure:
the cache keeps two levels); its SYSTEMMEM staging and DEFAULT atlas follow at the same `prepare_density` (`staging`,
`atlas`), the atlas again after every Reset; a failed `LockRect` or `UpdateSurface` of a horizon rectangle (`lock`,
`upload`). Any of them that is not a lost device calls `FogPass::horizon_fallback`: the textures go,
`DensityCache::drop_horizon` (final: readiness 0 at once, no work, no uploads; at the next post, under the mutex, the
worker request drops the level and its CPU cache is freed, and a horizon job in flight discards its slab), the cap becomes
195,000, one `volumetric_fog_distance event=horizon_fallback reason=... cap_km=39.0` row and one
`volumetric_fog_memory reason=horizon_fallback` row. Never `E_OUTOFMEMORY` from `prepare_density`, so the out-of-memory
retry/fault counter of 2026-10-21 does not see it. Witnessed under fault injection on the production FogPass
(`verification/probe/fog_pass_horizon_fault.cpp`, `test_fog_cards`): each of the five failures, a lost device (a
Reset, not a fallback), refused distance programs (today's set, no horizon allocation) and fog_distance 1.

**Log rows.** `volumetric_fog_distance_mode distance= requested= refused= programs=distance|look horizon= cap_km=` at
init (stored range); `volumetric_fog_range ... levels=3 cpu_bytes=25560576` with the horizon (17,040,384 without);
`volumetric_fog_cache event=horizon_ready ms=` once per epoch beside `far_ready` / `fine_ready`; the two refusal rows above.

**Cost.** Address space +8,520,192 B (CPU cache + SYSTEMMEM staging, 5.2 % of the Run184 164.8 MB minimum) and VRAM
+4,260,096 B (DEFAULT atlas) [I, from kAtlasBytes]. Render thread: `DensityCache::step` 18.5 -> 25.3 ns per frame still,
15.6 -> 21.9 ns moving (median of quiet host runs, x86-64 clang -O2; noisy runs 19-46 ns) [M, `horizon_host.txt` PERF];
the constants are a dozen float operations [I]. Worker under SETA (5 km/s on screen, a 2 s game step every 2 s): the
horizon takes 4.3 % of the generated nodes, the fine level is resident in 771 of 900 frames against 768 without the
level, the far and horizon readiness never reach 0 [M, host model]. GPU: +292 static slots per program; the
tail costs about 0.6 of a near bin per bin (§2, +1.0 ms on the Run 77 GPU [I]); not measured on the RTX 5070 Ti.

**Host gates on the reference field** (`field_gates.py` -> `field_gates.json`; the runtime screen's poses A and B,
144 sky rays each, today's law against fog_distance 3 [M]):

| | today A / B | fog_distance 3 A / B | 3 without horizon (39 km) A / B | design gate |
| --- | --- | --- | --- | --- |
| sky 1-T median | .0000 / .0000 | **.109 / .091** | .041 / .050 | <= .02 |
| exactly empty share | .49 / .65 | **.12 / .15** | .21 / .33 | >= .40 |
| sky 1-T mean | .104 / .082 | .159 / .171 | .127 / .147 | |
| shimmer, max dT per 250-unit step | .0115 / .0186 | .0109 / .0179 | .0115 / .0186 | <= today |
| isolated bank: largest 1-T change per km beyond 15 km | .111 | **.050** | | <= 1/3 of today |
| isolated bank: visible (1-T > .02) from / half from | 22.6 / 18.6 km | 60.0 / 23.6 km | | |
| horizon 8-point law against the 2x2x2 far-node box law, sky dT p99 / max | | **.020 / .026** (A), **.020 / .030** (B) | | p99 <= .003 |

**Decision (user, 2026-10-25: "some haze is actually good"):** the default stays 3; the sky-veil, empty-share and
approach-slope gates are waived by user decision, and the horizon aliasing result below is accepted pending the flight.

The shimmer gate holds. The veil and the approach-slope gates fail as specified: every sky ray now crosses 67.5 km of
field, so few stay exactly empty, and `(S0/s)^2` is as steep at 13 km (2/S0 = .154 per km) as today's smoothstep at its
steepest (.158), so the largest slope only halves; what changes is the lead-in (a bank is visible from 60 km instead of
22.6). Whether that reads as veil or as distant banks is the user's call in the A/B flight (§3 kill criterion); p, S0
and the cap are baked constants. The horizon node law fails its aliasing screen by a factor of about 7: past 39 km the
banks take a different shape from the far level's, blended across 35-39 km. The box law is the design's named remedy
(8x the horizon generation: about 2.1 s need box and 7.4 s window at x3 [I]); not built, the flight decides.

Not done here: the GPU fixture of the distance programs (Wine only) and any flight.
