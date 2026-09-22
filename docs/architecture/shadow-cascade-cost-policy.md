# Shadow cascade cost policy (run239 stand)

Design note, 2026-09-22. Question: should the cascade distances change, and should small
objects or small parts be excluded from the far cascades, given run239's 10 → 19 ms plateau?
Evidence: [engine-frame-time.md](engine-frame-time.md) "Run 239"; the run239 session log
(local, `/tmp/x3-bottleX3-run239/session-20260922-180304-212.log`, queried by field only);
owning notes [shadow-cascades.md](shadow-cascades.md), [shadow-cascade-extents.md](shadow-cascade-extents.md)
("Caster pool control", "Reach geometry"), [shadow-caster-retention.md](shadow-caster-retention.md).
The staggered far-cascade refresh (`--shadow-cascade-refresh`, c3 every 2nd and c4 every 4th
frame, held maps between) and the per-cascade draw-count split are taken as given.

## Decision

1. The cascade policy is not where run239's 8.5 ms went. The whole shadow replay is
   **0.73 ms** at the plateau (`shadow_replay_depth us=725.6`, 571 issues, 1.27 µs each) against
   0.49 ms at the control (390 issues); its delta is 0.23 ms of the +9.2 ms frame delta. The
   plateau is the engine's own visible set: 368 application draws (`frame_end draws=`, counted at
   the draw hook, `capture.cpp:757`; the replay issues natively at the scene end and is not in
   that count), 296 of them z-writing scene draws, at ≈ 21 µs of engine + state-call time per
   draw plus ≈ 5 µs of proxy route work per draw (derivation below). Every lever in this note
   is bounded by 0.73 ms; the refresh already in flight takes ≈ 0.4 ms of it.
2. Do, for the same candidate as the refresh, in this order:
   - **(a) a per-cascade minimum caster footprint in light space**, screen-bound law
     `min_i = max(P · footprint_i, 3 texel_i)` with `P = 8` on c3 and c4 (`--shadow-cascade-min-px 0,0,0,8,8`);
     per part (the draw's own sun-space box, which the bounds mask already computes), live and
     retained alike. Zero visible loss by construction; 42 of 571 issues at this stand, far more
     at any fighter-heavy stand.
   - **(b) c4 = 112,500** (22.5 km lateral, texel 110 u = 22 m) as the default, 75,000 (15 km,
     73 u) as the A/B option. Quality-driven, cost-neutral (≤ 0.2 ms here).
   - **(c) caps `128,512,1024,512,384`** as a worst-case ceiling on the importance order
     (launcher value, no code): bounds a big-complex frame at ≈ 1.2 ms of replay instead of 2.7.
   - Not (d) receiver-side work and not (e) faster far-cascade ageing; reasons below.
3. The 8.5 ms needs a different instrument: a `cull_census` + `X3M_FRAME_TIMING_STATE_STAMPS`
   run at this stand (see Unknown).

## Configuration numbers

5,000 units = 1 km. Extents are half-extents of a camera-centred sun-space square; world texel
= 2E / size (`shadow_replay_world_texel`; logged `texel0=0.244140625` matches). The brief's
"c4 covers 150 km at 73 m per texel" is off twice: 150,000 u is a 30 km half-extent, so the
map spans 60 km at **146.5 u = 29.3 m per texel**.

| Cascade | E (u) | lateral reach | texel (u / m) | receivers it serves are ≥ 0.95·E_{i−1} away | main-view footprint there (u/px) |
|---|---|---|---|---|---|
| c0 | 250 | 50 m | 0.244 / 0.05 | — | — |
| c1 | 1,500 | 300 m | 1.46 / 0.29 | 238 u | 0.46 |
| c2 | 7,500 | 1.5 km | 7.32 / 1.5 | 1,425 u | 2.8 |
| c3 | 37,500 | 7.5 km | 36.6 / 7.3 | 7,125 u (1.4 km) | 13.9 |
| c4 | 150,000 | 30 km | 146.5 / 29.3 | 35,625 u (7.1 km) | 69.6 |

Main view in run239: 1280 × 768, `p00 = 0.8` (`camera_state`, `cull_small_parts_value m00=0.8
width=1280`), so a pixel covers `2d / (m00 · width) = d / 512` units at distance d. A pixel is
sampled from cascade i only when it lies outside cascade i−1's box (`select_margin` 0.95), and
its sun-space lateral distance is a lower bound on its Euclidean distance from the camera, so
every receiver pixel of cascade i is at least `0.95 · E_{i−1}` away. That gives a per-cascade
constant, independent of where the caster is: a caster whose light-space silhouette is under
`P · 0.95 · E_{i−1} / 512` units can darken at most P pixels of any receiver that cascade
serves (parallel light: the shadow is the silhouette's size on the receiver).

**Minimum caster size per cascade** (units / metres), screen bound versus texel bound:

| Cascade | 3 texels | P = 8 | P = 16 | binding |
|---|---|---|---|---|
| c2 | 22 / 4.4 | 22 / 4.5 | 45 / 9 | neither matters: an M5 is ≈ 100 u; nothing needed |
| c3 | 110 / 22 | 111 / 22 | 223 / 45 | coincide at P = 8; screen bound above |
| c4 | 440 / 88 | 557 / 111 | 1,114 / 223 | screen bound for P ≥ 7 |

At 1920 wide the footprints scale by 1280/1920; the proxy reads `m00`/width at the latch
already (the small-parts cull does exactly this read), so the law is computed per frame, not
configured in units.

Correction to the brief: `--light-map-far-fade 80,220` fades the hull light-map *gain*
(emissive windows), not the shadow; the shadow apply has no distance fade (the last cascade only
fades to lit in its edge blend band). With 80/220 u/px at d/512, the gain fade runs 8.2 → 22.5 km,
which is presumably why 22.5 km matches the fog figure; the fog fade at 22.5 km is taken from the
brief and not re-verified here.

## Where run239's time is (derivation)

Frame 10200 versus 3000 (`frame_timing`/`frame_phases`, the section cited above):

- `view_submit` is the engine span `0x00472270 … 0x004722c8` (`frame_phase_sites.h`): the
  view's draw loop. The scene-end replay runs after it; the "views residual" that contains the
  replay, apply, TAA and particles grew 2,983 → 3,672 µs, consistent with the replay's
  492 → 726 µs and `apply_us` 58 → 72.
- Per application draw: native `DrawIndexedPrimitive` 1,167 / 368 = **3.2 µs**; proxy work
  around the draw (2,990 − 1,167) / 368 = **5.0 µs** (control (555 − 208) / 72 = 4.8: linear,
  not a regime change); everything else inside `view_submit` (10,753 − 2,990) / 368 = **21 µs**
  per draw (control (2,201 − 555) / 72 = 23), i.e. 62 state calls per draw (23,043 / 368) plus
  the engine's own traversal. The 22 µs-per-draw colour-path figure of
  shadow-caster-retention.md ("Why not the engine side, B1") is the same number.
- The 296 extra draws × (21 + 5 + 3) µs ≈ 8.6 ms is the delta. How the 21 µs splits between
  engine CPU and the proxy's state-call hooks is not known from this log (state calls are
  counted, not stamped).

`live_c4 = 241` is `bounds = 241` of the 298 routed draws: the c4 population *is* the engine's
visible set (200 of the 242 c4-only records are wholly inside the frustum by the store's test).
Retention's own replay share: 188 of 571 issues (111 c4 + 62 c3 + 15 c2 retained records).

## Caster census at the stand (F8 frame 10998, 352 records, 43 nodes, 261 static, 0 unseen)

From `shadow_retention_caster half=`/`centre=` joined to `shadow_replay_caster cascades=` by
(vb, primitives); footprint = the largest lateral side of the world AABB projected
perpendicular to the logged sun. Records per mask: c4-only 242 (25 nodes), c3+c4 47, c2–c4 38,
c1–c4 4, all 21. Max-side p10 / p50 / p90: 446 / 6,992 / 20,352 u; Euclidean distance p10 /
p50 / p90: 8.6 / 19.0 / 33.5 km (c4-only: 8.3 / 24.2 / 36.0, 155 of 242 beyond 22.5 km); sun-axis
offset p10 / p90 −11.3 / +16.4 km. These are big station parts, not fighters; primitives total
532 k, median 282 per record.

| Gate | c4 (352 records) | c3 (110) | c2 (63) |
|---|---|---|---|
| 3 texels | 29 (13.3 k prims) | 8 (3.7 k) | 2 |
| P = 8 | 34 (13.5 k) | 8 (3.7 k) | 2 |
| P = 16 | 77 (19.5 k) | 9 (7.8 k) | 3 |

So (a) at P = 8 removes 42 of 571 issues here (≈ 53 µs; ≈ 15 µs once c3/c4 refresh at 1/2 and
1/4); P = 16 removes 86 (0.11 ms). The stand does not contain the population the gate is for:
fighters (M5 ≈ 100 u, M3 ≈ 250 u) and turrets/antennae (100–610 u in the run-115 census) are
below 557 u and replay into c4 at 1–4 texels for a shadow no receiver can show; at a 10 km
furball each fighter is one main-view pixel and its c4 replay is pure waste. Per-node view:
the eight largest nodes carry 21–30 parts each; node 24033 (609 u, 21 parts) has 4 parts under
3 texels.

Records inside a shorter c4 (rotation of the real sun basis unknown to the census, so a
sufficient/necessary bracket): E4 = 112,500: 211–331 (nodes 32–40); 75,000: 183–197 (29–30);
50,000: 105–162; drop c4 (37,500): 41–127. Primitives follow: 440–504 k, 397–420 k, 239–383 k,
113–339 k of 532 k.

## The five options

**(a) Per-cascade minimum caster footprint.** Threshold per cascade
`min_i = max(P_i · 0.95 · E_{i−1} · 2 / (m00 · width), 3 · texel_i)`, recomputed with the
cascade set (adaptive c0 / ladder) and the latch. Test: the two lateral sides of the draw's
sun-space box, which `shadow_cascade_bounds_mask` already forms (`smin/smax`; `shadow_cascade_box_extent`
is the largest of the three sides and would keep depth-long slivers — use the two lateral axes),
against `min_i` before setting bit i; the same compare in the store's unseen walk
(`shadow_retention_core.h` `walk_unseen`, where `sun_interval` already gives `radius` per axis).
Per part, not per node: a 100 u antenna on a 39,000 u hull leaves c4, the hull stays. The main-view
`--cull-small-parts-px` cannot be reused: it is an instruction patch at the engine's node-level
cull (`0x0047d2a2`, projected radius at 640 wide) and runs before any draw exists; the shadow gate
is a proxy-side per-draw compare in light space on data already in hand. Hot path: two float
compares per active cascade per z-writing draw (≈ 5 per draw at 300 draws: nanoseconds), no
allocation, no device call; option off byte-identical. Visual risk at P = 8: none by
construction (a removed part could darken ≤ 8 px on the nearest receiver its cascade serves,
≤ 2.5 px at 22.5 km); a part just above the bound casts an 8–16 px blob at 29 m texels either
way. The risk is a *thin* long part: a 1,000 × 50 u strut passes (largest lateral side), so
nothing long disappears. Retained records pass through the same gate (they carry `half`).
Cost: ≈ 4–6 h: the law in `shadow_replay_projection.h`, the two compares, launcher option and
env (`--shadow-cascade-min-px P[,P...]`, `X3M_SHADOW_CASCADE_MIN_PX`), `footprint_refused<i>=` on
`shadow_replay_candidates`, host cases in `test_shadow_replay_projection` / `test_shadow_retention`
(a caster one unit under and over each bound; width/m00 variation; ladder-slid set), the
projection twin. Proof: host cases; the retention seam case unchanged with the option off; in
flight, `footprint_refused4=` > 0 at a traffic stand with the shadow hotkey A/B showing no
difference. Expected at the run239 stand: 42 issues; not a frame-time change.

**(b) Cascade distances.** c4's receivers are 7.1–30 km away; a 2 km station is 45 px tall at
22.5 km, an M7 arm's shadow band ≈ 16 px, sampled from a 29 m texel with back faces (a body
thinner than 29 m casts nothing there). Beyond 22.5 km the emissive gain is at its floor and
the brief's fog fade is complete; a shadow there is an 8–16 px low-contrast band on a fogged
receiver. Shortening c4 to 112,500 keeps every receiver inside the fog/gain fade, improves the
texel to 110 u (22 m) and the back-face thickness floor with it, and removes 6–40 % of the c4
records here (0.03–0.2 ms). 75,000 gives a 73 u (15 m) texel and halves c4 (≈ 0.2 ms), at the
price of no shadows on receivers 15–30 km away (stations at 15–22 km are 32–45 px tall: their
inter-part shadows are a visible detail at that size). Dropping c4 (four cascades, reach 7.5 km)
is what shadow-cascade-extents.md §3 originally recommended and the user later overrode with the
fifth cascade; not proposed again. c3 (7.5 km, 7.3 m texel) and c2 stay: their texels already
give 1–3 px per texel at their near ends. Cost: launcher values, 0 h; the cascade seam fixture
accepts any ascending set. Proof: the at-rest shadow hotkey A/B at a 15–25 km station view with
each E4; `draws4=` and `c4=` on the depth/candidates lines.

**(c) Per-cascade replay cap.** Exists: `--shadow-cascade-caps` with `drop_order importance`
keeps the `cap` largest projected casters with 0.8 hysteresis; at this stand c4 = 352 and c3 =
110 are under their 1,024 caps, so a cap of 384 / 512 changes nothing here and a cap of 256 on c4
drops 96 issues (0.12 ms; 0.03 ms under the 1/4 refresh) of the smallest-projected casters with
the pop the hysteresis leaves. Deferring the dropped casters to the staggered frames means
publishing a partial map, which the design forbids ("a partial map pops shadows; never publish
one"); the refresh already amortises the full map. Recommend caps `128,512,1024,512,384` as a
ceiling only: a full complex (c3 + c4 at 1,024 each, 2.7 ms) becomes ≈ 1.2 ms; no code.

**(d) Receivers.** `receiver_draws = 296 = zwrite`: the game's own z-writing scene draws routed
through the shadow-sampling variant; the proxy adds no draw for them, and their per-draw route
work is inside the 5 µs. A routed receiver cannot be outside every cascade (every on-screen
pixel within 30 km is inside c4), and off-screen receivers are the engine's node-level frustum
cull (parts of a passing node are drawn regardless; the store's "wholly inside" count, 200 of
352, is not an off-screen count). Refusing a game draw in the proxy saves 3.2 µs plus part of the
5 µs, never the 21 µs the engine spent before calling. Nothing to do on the receiver side; the
visible-set lever is the engine's cull site, already patched (`cull_small_parts px=2 threshold=3`).

**(e) Retention ageing per cascade.** 188 retained issues (0.24 ms; ≈ 0.06 ms under the
refresh); 18 of the 19 unseen nodes are outside the frustum, `age_max = 3,258` frames under the
7,200 cap, `unseen_in_frustum = 1`. Ageing far tiers faster re-creates the parked-station
pop-out retention exists to remove (directional-shadows.md, "Run 62 fix", keeps the cap) for
≤ 0.06 ms. Retained records are static by construction, so the held maps of the refresh are
exact for them; they take the (a) gate like live records. No change; keep the
`shadow_retention_resight` buckets as the calibration.

## Native Windows

All of the above is CPU bookkeeping and launcher values on documented D3D9 replay calls already
in use; no new device calls, formats or vendor paths. Unverified natively, as the cascades are.

## Alternatives that lose

- *Skip c4 when nothing moved* (a caster-change hash on a held centre): the refresh in flight
  delivers most of that saving at fixed cadence; a hash adds a per-frame walk of the record list
  for ≤ 0.1 ms more here. Revisit only if the refresh's held frames prove too stale visually.
- *Node-level gate (whole node by its largest part)*: keeps every antenna of a big hull in c4;
  the per-part law is free since the box is per draw already.

## Unknown, and what settles it

- The split of the 21 µs per draw between engine CPU and the proxy's state-call hooks:
  `X3M_FRAME_TIMING_STATE_STAMPS=N` at this stand (`frame_timing.h`), one run.
- The visible-set lever itself: `cull_census` was off in run239 (0 lines); a census at this stand
  gives the projected-size histogram of the 368 draws and sizes a `--cull-small-parts-px` step
  from 2 to 3–4 px, the only lever that removes the engine's 21 µs per draw. Outside this note.
- The fog transmittance at 22.5 km (taken from the brief) decides between E4 = 112,500 and
  75,000; the hotkey A/B at a 15–25 km station view settles it.
- Whether the engine's own distance/LOD fade bounds c4 anywhere: at this stand the engine drew
  parts to 43 km, so not here.

## User verdict 2026-09-22

The user rejects shortening the farthest cascade to 22.5 km ("too low") and would accept 90-100 km. Any A/B of the c4 extent uses 100 km (500,000 units), not 22.5 or 75 km; the per-part minimum footprint and the caps remain the levers for c4 cost.
