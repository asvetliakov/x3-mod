# Sun-shadow cascade configuration for X3's distances

Design note for ratification, 2026-09-17. Extends [shadow-cascades.md](shadow-cascades.md)
(the mechanism, implemented on main: N ≤ 4 camera-centred snapped cascades, per-cascade
extent / size / cap and an issue budget as launcher options, far cascade on alternate frames
over budget, far fade, positional sun, retention in flight) with the *numbers*: which receivers
need shadows at which distances, and the extents, sizes, caps and cadence for run 39. Ledger:
[../verification/directional-shadows.md](../verification/directional-shadows.md).

Markers: **M** measured in this repository's runs or fixtures, **A** assumed (stated source),
**I** inferred by arithmetic from M/A facts. Units: 1 m = 5 units; a screen pixel at view
distance `z` is `z/768` units at 1920 wide (`z/512` at 1280: every px-per-texel figure below
is ×0.67 there); texel = `2E / size`; px per texel at distance `z` = `768 · texel / z`.
Object sizes are the brief's approximations (**A**: no ship-size index exists under `docs/` or
`tools/`; only archive listings): M5 ~20 m, M3 ~40–60 m, M6 ~150–200 m, M7 ~700 m, TL ~1 km,
M1/M2 1.5–2 km, station 1–3 km, complex 5–10 km.

## Decision

Keep the prototype extents **250 / 1,500 / 7,500 / 25,000**; sizes **4096 / 4096 / 4096 /
2048**; caps 128 / 512 / 1024 / 1024 and budget 640 unchanged; C0–C2 every frame, C3 alone on
alternate frames when over budget (the implemented policy). Four cascades; no fifth; no
own-ship-adaptive C0 for now. Launcher: `--shadow-cascades default --shadow-cascade-sizes
4096,4096,4096,2048`. One option flips C3 back to 4096 if the user sees the seam at 1.5–3 km.

Ratified by the orchestrator 2026-09-18 with one amendment: run 39 flies set R first and,
at the same station, a second segment with C3 = 50,000 at 4096² (set H's far cascade on R's
near cascades) so the far-cascade pool (`c3=`, `capped3=`) and the at-rest frame delta decide
between 5 km and 10 km reach on measurement. The own-ship-adaptive C0 and the ratio guard are
scheduled after the first big-ship flight.

## 1. Receivers and what they need

| Receiver | View distance (units) | Pixel size (u) | Texel for ≤ 2 px | Cascade serving it | px/texel there |
| --- | --- | --- | --- | --- | --- |
| Own fighter hull, chase camera 20–40 u behind (**M** run 37) | 30–300 | 0.04–0.39 | ≤ 0.08–0.8 | C0 (0.122) | 3.1 at 30 u, 0.4 at 250 |
| Own M6 hull (~1,000 u long; camera at ≥ 0.3× hull, **A**) | 300–1,300 | 0.4–1.7 | ≤ 0.8–3.4 | C1 (0.732) | 1.9–0.4 |
| Own M2 hull (~10,000 u; camera 2,000–4,000 u, **A**) | 2,000–14,000 | 2.6–18 | ≤ 5–36 | C2 (3.66) then C3 | 1.4–0.4 |
| Ship's shadow on a deck / bay while flying along at 50–400 m | 250–2,000 | 0.3–2.6 | ≤ 0.65–5.2 | C1, C2 | 2.25 → 1.4 |
| Station-on-station within 300 m, docking bay interior | 250–1,500 | 0.3–2 | ≤ 0.65–3.9 | C1 | 2.25 → 0.4 |
| One station's inter-part shadows, 0.5–1.5 km | 2,500–7,500 | 3.3–9.8 | ≤ 6.5–20 | C2 | 1.1 → 0.4 |
| Whole station / capital in view at 1.5–5 km | 7,500–25,000 | 9.8–33 | ≤ 20–65 | C3 @2048 (24.4) | 2.5 → 0.75 (4096: 1.25 → 0.4) |
| Distant stations 5–30 km | 25,000–150,000 | 33–195 | ≤ 65–390 | none | — |

Do inter-part shadows read at 5–30 km, given the engine's N·L? **I**: a 2 km station is 307 px
tall at 5 km, 102 px at 15 km, 51 px at 30 km; an M7-class arm (3,500 u) casts a band of 107 /
36 / 18 px on the body. N·L already darkens faces turned from the sun, so the shadow adds only
the dark band across a *lit* face: it reads plainly at 5 km, as fine detail at 10–15 km, and at
30 km it is 18 px on a low-LOD receiver whose small parts the engine culls by projected size
(shadow-caster-retention.md, `node+0x1d8 < 1`). A 5 km reach (C3 = 25,000) covers the whole
"station or capital in view" class; 10 km (extent 50,000, the option maximum) is the richer
alternative; beyond that a cascade buys nothing visible. Stations are typically several km
apart (**A**), so C3 at 25,000 usually holds one station and its traffic, at 50,000 several.

## 2. The sets

Texel and px/texel at the near end of use (the previous extent, or 30 u for the hull) and at
the far end (the extent). Memory adds one shared 4096² `D24X8` attachment (64 MiB). Issues per
frame: C0/C1 **M** (run 37/38: `bounds` median 8, max 51–52 at extents 250 and 1,500, the
latter under the extent-cache flicker so an upper bound), C2/C3 **I** from the 93–930 z-writing
pool (**M**) plus the retained store (**M**: 833 records over three views). Clear bytes assume
Clear touches colour + the viewport-sized region of the depth attachment.

**Recommended (R)** — 272 MiB, clears ≈ 400 MiB per frame (384 for C0–C2, 16 averaged for C3):

| Cascade | E | Size | Texel | px/texel near → far | Map MiB | Expected issues | Cap | Refresh |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C0 | 250 | 4096 | 0.122 | 3.1 (hull) → 0.4 | 64 | 8–52 + tens retained | 128 | every frame |
| C1 | 1,500 | 4096 | 0.732 | 2.25 → 0.4 | 64 | ≤ 52 + tens retained | 512 | every frame |
| C2 | 7,500 | 4096 | 3.66 | 1.9 → 0.4 | 64 | 100–900 at a station | 1024 | every frame |
| C3 | 25,000 | 2048 | 24.4 | 2.5 → 0.75 | 16 | 500–1,024 at a station | 1024 | alternate frames over budget |

**Leaner (L)** — 224 MiB, clears ≈ 300 MiB/frame: as R with C0 at 2048 (texel 0.244; hull
6.25 px at 30 u, still 2× finer than the 12.5 px / 8.4 px at 1280 the user accepted in run 37,
**M**). C0 carries 8–52 draws on a 20 %-occupied map (**M**), so its 4096² costs a 128 MiB
clear per frame for a 2× finer hull texel and nothing else; this is the first fallback if run
39's at-rest delta is over ~1.5 ms.

**Richer (H)** — 320 MiB, clears ≈ 450 MiB/frame: 250 / 1,500 / 9,000 / 50,000, all 4096
(ratios 6 / 6 / 5.6; texels 0.122 / 0.732 / 4.39 / 24.4; near-end px/texel 3.1 / 2.25 / 2.25 /
2.1). Reaches 10 km with ≤ 2.25 px per texel everywhere. Its cost is the C3 pool: everything
within 10 km, likely above the 1,024-record capacity at a busy sector, and a cap drops casters
in submission order (popping) unless the retention store's "live first, then nearest" fill is
in. Not for run 39.

*Amendment (2026-09-18, "Caster pool control" below):* that blocker is lifted when the three pool
options are on: `--shadow-cascade-records 1024,1024,2048,4096` sizes the record list for C3,
`--shadow-cascade-drop-order importance` replaces submission-order drops (popping) by size-ordered
ones, and `--shadow-cascade-static-from 3` keeps moving casters out of C3 so its pool is the
static geometry alone. H is then a configuration, not a code change; its cost is what run 39 A2
measures (`c3=`, `capped3=`, `dropped_min_size3=`, `static_only_refused3=`, `select_us=`).

**When C3 at 4096 versus 2048 matters.** Only between 7,500 and ~15,000 u (1.5–3 km), where
2048 gives 2.5–1.25 px per texel against 1.25–0.6; beyond 3 km both are sub-pixel. At the C2/C3
seam the softness steps 0.4 → 2.5 px (6.7×) with 2048 against 0.4 → 1.25 px (3.3×) with 4096,
blended over the 713 u band. The price of 4096 there is 48 MiB resident, 48 MiB more clear on
C3's replay frames and 48 MiB more per F8 frame; since C3 alternates at any station (§4), the
clear saving is ~24 MiB per frame, so the 2048 choice is about memory and dump size, and is
reversible with one option (`--shadow-cascade-sizes 4096`) if the user sees the seam.

## 3. Four cascades suffice; a fifth is not recommended

Every receiver class of §1 within 5 km is served at ≤ 3.1 px per texel with four maps, and the
only unserved class (5–30 km) is better served by widening C3 to 50,000 (set H) than by a
fifth map: a C4 at 50,000 with C3 at 25,000 would hold ratio 2 and duplicate C3's pool.

Code change if a fifth is ever wanted (not now): `shadow_cascade_max` 4 → 5 (`ShadowCascadeSet`,
`ShadowCascadeBounds`, `shadow_replay_maps_max`, the mask bits); the apply program gains a
fifth sampler (`s5`; ps_3_0 has 16), five constants (`c33–c37` of 224), a fifth projection,
weight and `[branch]` PCF: the program is 406 of 512 slots today (**M**), and one PCF branch
with its selection arithmetic is roughly 70 slots (**I** from four branches in 406 minus the
shared prologue), so a fifth lands near 480 — inside the limit but with no margin; the
generator's `--check` compile settles it. Per-pixel cost: three more `dp4`, two compares and a
weight for every pixel (~1 % of the quad), one more PCF only in its band. The launcher's
`SHADOW_CASCADE_MAX`, the F8 `shadow_map4` readback and the fixture cases follow.

## 4. Caps, budget and cadence

Caps are not the cost control; they bound storage (52 B per issue, 137 KiB at the defaults,
**M**) and drop casters in submission order when exceeded, which pops shadows. Keep 128 / 512 /
1024 / 1024: C0/C1 measured maxima (52) leave 2–10× headroom for retained off-screen parts;
C2/C3 at 1024 equal the record capacity, so they can never engage before the record list is
full. Raise nothing for set R. (Set H raises C3's records and cap to 4,096 and drops by importance:
"Caster pool control" below.)

Budget 640 stays. Cost model at 1.3 µs per issue (**M** run 81/109; 1.0–1.4 in the cascade
fixture) plus ≈ 40 µs fixed and 15–20 µs per further map (**M**): fixed ≈ 95 µs for four maps.

| Frame | Issues | CPU replay | Share of 22 ms |
| --- | --- | --- | --- |
| Open space, median (8 + 8 + 100 + 100, C3 every frame) | 216 | 0.38 ms | 1.7 % |
| Station, C3 frame (52 + 52 + 930 + 1,024) | 2,058 | 2.77 ms | 12.6 % |
| Station, averaged with C3 alternating | 1,546 | 2.10 ms | 9.6 % |

At any station C2 alone exceeds 640, so C3 alternates there by construction; every frame in
open space. That is the intended steady state, not a degradation.

**Which cascades may alternate.** A moving caster's shadow is one frame stale on odd frames. At
3,000 u/s (a fast fighter, **A**: 100–600 m/s fighter speeds) and 22 ms, the shift is 66 u;
on-screen at the near end of each cascade's use: C1 200 px, C2 34 px, C3 6.8 px (2 px at its
far end). Only C3 may alternate (implemented); C2 would visibly jitter shadows of traffic on a
station at 300 m. If run 39 puts the CPU replay above ~2 ms at stations, the next step is the
retention store's static/moving classification: skip a far cascade's replay entirely while no
moving caster is in its box and the camera has moved less than a quarter extent (a held basis is
already how odd frames are applied), which also lets C2 skip. Not a fixed cadence for C2.

### Caster pool control (implemented 2026-09-18, default off)

Three launcher options control the far cascades' pool instead of the map; every default reproduces
the previous behaviour byte for byte (same records, same drops, same log lines). Evidence:
[../verification/directional-shadows.md](../verification/directional-shadows.md), "Caster pool
control".

1. **`--shadow-cascade-static-from K`** (`X3M_SHADOW_CASCADE_STATIC_FROM`, 1..N−1 for N cascades;
   K = N or 0 is refused by the launcher and the DLL, never a silent no-op): cascades `i ≥ K`
   admit static casters only. Classification per draw, before the caps: the retention store's
   verdict for a node it knows (`--shadow-retention-census` or `--shadow-caster-retention`: eight
   verified sightings within eps; the store's node-level verdict is the stronger one and is the
   intended companion of this option); otherwise `src/proxy/shadow_caster_class.h`, a two-way ring
   with two entries per record of the list (2,048 at the default 1,024 records, 8,192 at 4,096;
   112 B each) keyed by the node serial and the draw's vertex range, holding the draw's object →
   world rows at its *anchor* sighting: within `--shadow-caster-retention-eps` (0.05 u, the store's
   law, `drift2` on the draw's own extent) of the anchor is static; beyond it is moving and the
   sighting becomes the new anchor, so a slow drifter accumulates against its anchor and is
   reclassified once its drift reaches eps (the store's `d.world` rule), never re-anchored under
   it. A first sighting, an evicted entry, a draw without a lifetime serial or without rows is a
   miss and counts as moving for that frame. The ring's limit: it knows nothing across a device
   re-attach or a ring eviction (a frame with more distinct static-only candidates than twice the
   record list evicts), and its anchor is per draw range, so a node whose parts are re-keyed (LOD
   swap) misses once; the store carries none of these. A draw refused from every cascade it met is
   not a candidate at all (it neither leases nor consumes cap room). Counters on the
   `shadow_replay_candidates` line, only while the option is on: `static_only_refused<i>=`,
   `large_admitted<i>=` and `class_miss<i>=` (misses that refused cascade i) per cascade, then
   `class_store= class_ring=` (draws classified by each source). **Large casters (`--shadow-cascade-large-min L`, `X3M_SHADOW_CASCADE_LARGE_MIN`,
   world units, default 0 = strict):** a moving draw is admitted to a static-only cascade anyway
   when its world AABB extent (the largest side of the box the mask test built; a rigid quantity of
   the draw's own box, so a mesh part's, not the whole ship's) is `≥ L`, counted `large_admitted<i>`;
   a draw without an extent yet (origin rule) has extent 0. Recommended **L = 1,500**: X3 hull
   parts scale with the class (1 m = 5 u: M5 ≈ 100 u, M3 ≈ 250, M6 ≈ 900, M7 ≈ 3,500, TL ≈ 5,000,
   M2/M1 7,500–10,000), and the run-115 F8 census (`shadow_retention_caster half=`, 54 nodes, max
   per node handle and model) measured fighter, turret and small station parts at 100–610 u, one
   moving hull at 29,939 u and one station hull at 38,269 u with nothing between 610 and 29,939:
   1,500 admits M7 and larger hulls (and TL, M2, M1) and refuses everything a fighter or a
   station's small parts submit; the sample is two F8 frames of one run, so `large_admitted3=`
   against `static_only_refused3=` in run 39 A2 is the calibration. Retained
   records (the store's own replays) are static by construction and pass unfiltered. Memory: the
   ring, 224 KiB at 1,024 records (896 KiB at 4,096), allocated once at attach while the option is
   on. A stopped mover becomes static on its next sighting within eps of where it stopped; a
   moving caster that the store already classified static keeps the store's verdict until the
   store reclassifies it (its next verified sighting).
2. **`--shadow-cascade-drop-order importance`** (`X3M_SHADOW_CASCADE_DROP_ORDER`, default
   `submission`): the draw path records every admitted caster (up to the record list) and the
   scene end selects, per cascade over its cap, the `cap` largest by projected size = sun-space AABB
   diagonal / distance of its centre from the camera, both from the corner transform the bounds
   mask already performs (`shadow_cascade_bounds_mask(..., &projected)`); a draw without an extent
   (origin rule) has size 0. Order: size descending, node serial ascending, record index ascending,
   so the kept set is a function of the frame's casters and not of their submission order (a
   permuted order keeps the same set; two parts of one node with equal size fall back to
   submission order). Hysteresis at the cap boundary: a caster the cascade kept last frame ranks
   above the rest while its size is at least 0.8 × the cutoff (the smallest size the plain order
   would keep this frame), still bounded by the cap, so two near-equal casters at the boundary do
   not alternate; the previous frame's kept set is an open-addressed table of caster keys (two
   slots per record, 16 B each, refilled at every scene end). One `std::nth_element` per over-cap
   cascade over its records, a second one only when a dropped caster was kept last frame within
   the band; the records left without a cascade are compacted out with their geometry leases
   retired and counted as the draw-time cap counted them (`capped<i>=`, `capped=`). Line fields,
   only while on: `dropped_min_size<i>=` (the largest size among the dropped, 0 while nothing was
   dropped: what popping would cost) and `select_us=` (the selection, the table refill and the
   compaction, under the LastError envelope). With the retention store on, every admitted draw
   is a sighting (previously the capped ones were not).
3. **`--shadow-cascade-records N[,N...]`** (`X3M_SHADOW_CASCADE_RECORDS`, 1..4096, default 1024):
   the record capacity per cascade; a cascade's issues are bounded by `min(cap, records)`
   (`ShadowCascadeSet::bound`), `--shadow-cascade-caps` now accepts 1..4096, and the issue storage
   is the sum of the bounds. The record list (records, geometry leases, draw list, quiet flags) is
   sized to the largest per-cascade records at device creation (about 400 bytes per record: 1.6 MiB
   at 4,096, allocated once at attach; the inline 1,024 arrays serve the default); the boxes nest,
   so the union of the cascades' casters is about the outermost's, and a frame with more distinct
   casters than the list counts `overflow=` as before. The retention store's draw list follows
   (`records + 4096`). The issue budget and the far-cascade cadence are unchanged.

Costs (`sun-shadow-apply-cascades` bench, `SUNAPPLY_BOUNDS_BENCH`): the mask with the size
beside it against the mask alone, the classification per draw, and the scene-end selection at a
full 4,096-record list are in the ledger section. Native Windows: documented D3D9 only (no new
device calls; the selection and classification are CPU bookkeeping); unverified natively.

## 5. Own-ship-adaptive C0 and a ratio guard: implemented, default off (2026-09-17)

**Amendment (2026-09-17).** Implemented behind `--shadow-cascade-adaptive-c0 K`
(`X3M_SHADOW_CASCADE_ADAPTIVE_C0`, K within [0.5, 8], suggested 1.5; absent or 0: the set
below is byte-identical to before). The law, in `src/renderer/shadow_replay_projection.h`
(`shadow_cascade_adaptive_update`, `shadow_cascade_adapt_c0`, `shadow_cascade_ratio_guard_mask`)
and `src/proxy/motion_output_shadow_adaptive_inc.h`:

- **Own ship.** The active control cockpit of the registry at `0x608504` (the walk of
  `chase_lead::active` / `object_capture::target`), its ref object `cockpit+0xc` and that
  object's root render node `ref+0x70` with the node's handle `+0x28`
  (`object_capture::own_ship`; the chase camera's anchor, chase-camera-first-flight.md). Resolved
  once per frame at the first candidate draw, behind `object_trace::executable_verified()`;
  a draw belongs to the ship when its scope node is the root or reaches it through the parent
  links `+0x18` (cached per node, re-walked every 256 frames). No new hook.
- **Radius.** Per frame the largest AABB-corner distance from the object origin through the
  draw's rows (view units = world units) over the ship's z-writing draws with a known extent
  (`shadow_cascade_draw_radius`, 12 ns on the host per own-ship draw; other draws pay one
  pointer compare or one cache probe).
- **Commit, at the frame boundary** (`begin_frame`, so one frame's box test, replay and apply
  share one set): another ship with a measured radius commits at once, as does the first
  measurement of the current ship; a ship not measured yet, no ship at all (menu, cockpit
  view without hull draws) and a > 20 % size change are candidates that commit after 8
  consistent boundaries. `E0 = max(E0_config, K × radius)`, clamped to the last cascade's
  extent (the depth towards the light stays `2 E_last`) and the 50,000 maximum; texel
  `2 E0 / size`, depth behind `max(512, 2 E0)`, forward offset `min(128, E0 × 128/250)`. A
  changed E0 re-snaps C0's grid (its texel changed) and voids only C0's retained map; the far
  cascades keep theirs. Log: `shadow_cascade_set reason= own_node= own_status= own_radius= e0=
  texel0= depth_behind0= active_mask= k= pending_radius= pending_frames=` on every commit and
  on F8 frames (`reason=capture`).
- **Ratio guard.** After each commit, every following cascade whose extent is below 3× the
  previous *active* cascade's is dropped while that holds (`active_mask`; the reading of
  "E_{i−1}" as the previous kept cascade: with E0 = 3,000 both 1,500 and 7,500 go, 25,000 stays).
  A dropped cascade keeps its map, size and cap (no reallocation) but has an empty box (no
  record or retained draw carries its bit), replays nothing, its retained basis is void, and
  the apply quad gets rows that put every pixel outside its margin, so the selection falls
  through to the next active cascade (the shader's "absent = lit" would otherwise open a hole
  between E0 and the dropped extent). The `sun_shadow_apply_params` line prints the per-frame
  extents as before; the twin reads them from there.
- **Expected in flight.** M5/M3 (radius ≤ 165 at K 1.5): nothing changes. M6 (radius ~500–
  700): E0 750–1,050, C1 (1,500) dropped, C2 kept (7,500 ≥ 3 E0 up to E0 = 2,500). M2 (radius
  ~5,000): E0 = 7,500, C1 and C2 dropped, C3 kept: 7,500 / 25,000, two maps.

**What the first big-ship flight must measure** (to set K, still unknown): `camera_state t`
against the ship node's position (`node+0xb0`, engine integers × 0.01) in the F8 frames, i.e.
the chase-camera distance as a fraction of the hull radius; K ≈ 1 + that fraction keeps the
whole hull and its shadow inside C0. The `shadow_cascade_set` lines give `own_radius`; the
`sun_shadow_apply_params` line gives `texel_world0` to judge the hull's px per texel.

The original argument, kept for the record:

The fighter is the hard case, not the capital: its camera sits at 0.1–0.2× the hull length,
while X3's chase view frames the whole ship for bigger classes (**A**: the chase-camera RE note
mentions a distance scale without a figure; `camera_state t` against the hull records in a
future run measures it). With a camera at ≥ 0.3× the hull, an M6 hull falls into C1 at 1.1–1.9
px per texel and an M2 into C2 at 0.7–1.4: already better than the fighter's 3.1 px on C0. The
part of a big hull inside the 500 u C0 box is served by C0 at a finer texel, with a softness
step at the 212–237 u band — fine. So `E0 = max(250, k × radius)` buys resolution nobody needs
and costs: an own-ship radius source (the `slice0` origin rule admits draws whose object origin
is within 250 u of the camera, which misses a capital whose origin is 2,000 u away; the chase
camera's follow-target node and its bounding sphere would need a disassembly note under
`docs/reverse-engineering/`), a hysteresis (quantised steps 250 / 500 / 1,000 / 2,000, 60
frames stable; a step re-snaps C0's grid, a one-time pop), and a re-centring of C0 on the ship
rather than the camera (today the forward offset is capped at 128 u). The one real benefit is
cost: a ratio guard dropping cascades with `E_i < 3 E_{i−1}` would make an M2 fly 1,000 / 7,500
/ 25,000, saving one 64 MiB clear and ≤ 52 issues. Defer until the user flies a class above
M6 and the run shows the C0/C1 seam on the hull; the extents are options, so a per-ship
launcher choice covers the interim without code.

## 6. Risks

- **Clears.** Set R clears ≈ 400 MiB per frame (C0–C2 colour + depth 128 MiB each, every
  frame). Model (**A**): if wined3d over Metal turns a full-surface Clear after
  `SetRenderTarget` into a render-pass load action, the cost is ~0; if it writes memory at
  ~250 GB/s (M-series class bandwidth, **A**), 128 MiB is 0.5 ms and the frame pays 1.6 ms
  (7 % of 22 ms). Fill is small (**I**: 20 % occupancy, depth-only, four-`dp4` VS). Apply
  sampling ≈ 18 M reads per frame (**I**), ~0.1 ms. Worst total GPU ≈ 1.8 ms; with the CPU
  replay 2.1 ms that is ≈ 4 ms (18 %) at a busy station, 0.5 ms in open space. Fallback order if
  measured heavy: C0 → 2048 (saves 96 MiB of clear, hull 6.25 px), then C1 → 2048 (saves 96
  MiB, deck shadows at 4.5 px at 50 m). Resident 272 MiB is untested in this 32-bit process;
  run 38 A ran 128 MiB (**M**) without refusal. A failed create refuses the pass and logs.
- **Depth precision.** Not a risk. Range `R = 2 E_max + depth_behind` = 50,512 (C0) to
  100,000 (C3): `R32F` resolves 6e-8 × R = 0.003–0.006 u, `D24X8` 0.003–0.006 u, both < 5 %
  of the 0.122 texel; the bias 0.54 + texel term is 200+ ulps. World magnitude does not enter
  the float rows: the constant terms are (object origin − centre) / E, O(1), because the
  products are formed in double and narrowed last (`shadow_replay_projection.h`).
- **Blend band width.** 10 % of the 0.95 margin: 24 / 143 / 713 / 2,375 u for C0–C3. The far
  fade runs from 4.25 to 4.75 km lateral: a station straddling it shows a 500 m gradient to
  lit, which is the intended fade. The C2/C3 seam (1.28–1.43 km) is where the 6.7× softness
  step of set R is blended; the user's verdict there decides C3's size.
- **Texel snapping with retained bases.** Odd-frame C3 samples a map whose centre was snapped
  on the even frame; static casters are exact by construction. The only residual is the sun
  re-derivation (**M**: every `distance / size` of travel across the light: 3,830 u at 4096,
  7,660 u at 2048; shift ≤ 1 texel for casters within the extent, 6.5 texels for a caster
  50,000 u light-ward). C3 at 2048 re-derives half as often. The camera-centred box loses a
  66 u sliver per frame at its edge, inside the fade band: invisible.
- **Alternate-frame jitter** of fast movers' shadows on far receivers: 2–7 px at 30 Hz (§4),
  partly absorbed by the resolve; watch for it in run 39 at a busy station.

## 7. Cost on the hot path and native Windows

None of this changes per-draw code: the four-cascade bounds mask is 44.6 ns per z-writing draw
against 45.0 for the single verdict (**M**) whatever the extents; the extents only change how
many records meet each box. Native Windows: the same documented D3D9 as shadow-cascades.md
§6; a 2048² far map is within every device's `MaxTextureWidth`; unverified natively.

## 8. What run 39 must measure

Fly set R (`--shadow-cascades default --shadow-cascade-sizes 4096,4096,4096,2048`,
`--shadow-replay-depth`, F8 bursts at a station, at a docking bay, at 1.5–3 km from a station
and in open space; a busy-station at-rest segment of ≥ 600 frames).

1. `shadow_replay_depth us= issues= draws0..3= far_replayed= far_frame= budget=` over the whole
   session (34k lines in run 38: regress, do not sample): slope µs per issue, intercept, the
   share of frames with `far_replayed=0`, the C2/C3 issue distributions (settles §4's model).
2. `shadow_replay_candidates c0..3= capped0..3=`: `capped2/3 > 0` in any frame means the record
   list is full and H is out of reach without the retention fill order.
3. GPU cost: `frame_end dt_ms` median at rest, cascades on versus off. **No shadow hotkey
   exists** (comparison-hotkeys.md: F4/F5/F6 and F11 belong to other features, F8 is the
   capture key), so either two launches at the same at-rest spot (content differs between
   launches: report both medians and the draw counts) or a small `implement` task adds a
   Ctrl+Shift toggle of the cascade replay + apply before the run — recommended, since the GPU
   number is the one figure the whole size decision hangs on. GPU delta = frame delta −
   `shadow_replay_depth us` − `sun_shadow_apply_frame us`.
4. F8: per-cascade map occupancy and depth-minimum, `sun_shadow_apply_params texel_world<i>`,
   the twin's `f<0.9` on the hull (acne at 0.122) and on a deck (C1/C2). Attach line: `halved`,
   any create failure. `shadow_replay_sun_point distance` (the finite-sun residual assumes
   ≈ 1.57e7).
5. User verdicts: hull self-shadow at 3.1 px; the seams at 50 m, 300 m and 1.5 km (softness
   steps, any lit gap); the far fade at 4.5 km; jitter of moving ships' shadows on a station
   beyond 1.5 km; and whether 2048 on C3 is noticed at 1.5–3 km.

## Unknown, and what settles it

- The clear/fill cost of a 4096² map on this backend: run 39 §8.3 (the decisive number).
- C2/C3 issue counts with retention on: run 39 §8.1–2.
- Chase-camera distance for M6 and above (the §5 argument rests on it): `camera_state t`
  against the own hull's records the first time the user flies a bigger class.
- Own-ship node identity for an adaptive C0: resolved in code from the chase camera's ref
  object (§5); whether every hull part's scope node reaches the ship's root through `+0x18`
  is assumed from the ancestry captures (station-material-distance.md) and is confirmed by
  `own_radius` on the first flight with the option.
- Resident 272–320 MiB of render targets in the 32-bit process under wined3d: the attach log.
