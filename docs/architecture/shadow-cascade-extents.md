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
full. Raise nothing.

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

## 5. Own-ship-adaptive C0 and a ratio guard: not needed now

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
- Own-ship node identity for an adaptive C0: a disassembly note on the chase camera's follow
  target and the node's bounding sphere, only if §5's deferral is revisited.
- Resident 272–320 MiB of render targets in the 32-bit process under wined3d: the attach log.
