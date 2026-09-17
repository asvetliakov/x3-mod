# Sun-shadow cascades: from the own-ship map to a whole complex

Ratified by the orchestrator 2026-09-17 as the direction (three camera-centred texel-snapped cascades, asymmetric depth range towards the light, per-pixel selection by sun-space extent with a blend band, C2 on alternate frames). Implementation starts after run 38 calibrates the single wide map (4096² at 1500 units); the run-38 numbers set the caster budgets.

Design note for ratification, written 2026-09-17 after run 37 session B (run109: first
visible shadows; casters median 8 / max 49 of 93–930 routed depth writers per frame because
only geometry inside the 250-unit box is admitted; map 20 % occupied at the station). Owning
notes it extends: [directional-shadows.md](directional-shadows.md) §4 (the route-B cascade
sketch), [legacy-sun-application.md](legacy-sun-application.md) §2 (the apply quad),
[shadow-replay-gates.md](shadow-replay-gates.md) ("Casters by bounds"); ledger
[../verification/directional-shadows.md](../verification/directional-shadows.md). Units: 1 m =
5 units; screen pixel at view distance `z` is `z/512` units at 1280 wide (`m00 = 0.8`),
`z/768` at 1920. Nothing here is implemented; the run-38 options (`--shadow-replay-extent`,
`--shadow-replay-depth-half`, `--shadow-replay-cap`, `--shadow-replay-size` ≤ 4096, bias in
world units with a texel term) are assumed present.

## Decision

Three camera-centred, texel-snapped cascades in one replay transaction, each its own `R32F`
map, one shared 4096² depth attachment; every cascade's depth range extends the full far
distance towards the light; the apply quad selects per pixel by sun-space extent with a 10 %
blend band; the far cascade is replayed on alternate frames when the frame's draw issues
exceed a budget, and the apply composes each map's retained basis with the current camera.

| Cascade | Half-extent | Map | Texel | Screen parity (1280 / 1920 wide) | Casters (run 37, expected) | Update |
| --- | --- | --- | --- | --- | --- | --- |
| C0 own ship | 250, centre camera + 128 forward (as today) | 1024² (option 2048²) | 0.49 u (0.24) | 250 / 375 u | median 8, max 49 | every frame |
| C1 near station | 1500, centre camera | 2048² (option 4096²) | 1.46 u (0.73) | 750 / 1125 u | unknown; run 38 measures | every frame |
| C2 complex | 7500 (3 km across) | 4096² | 3.66 u | 1875 / 2812 u | ≤ the whole pool, max 930 | every frame if the budget allows, else even frames |

Memory, lean tier: maps 4 + 16 + 64 MiB plus one shared `D24X8` 4096² (64 MiB) = **148 MiB**
default pool (today 8 MiB); with the two options 208 MiB. `D16` for the shared attachment
would save 32 MiB (step 0.24 units over the far range, below every texel) but `D24X8` is the
qualified format; keep it unless memory is measured to matter. A depth-stencil surface larger
than the render target is documented D3D9 (the DS must be at least as large as the RT; the
pass already unbinds the application's smaller depth). Separate maps beat an atlas: the quad
has samplers to spare (s0 RT2, s1–s3 maps), an 8192-wide atlas would exceed `MaxTextureWidth`
on some devices, and separate sizes per cascade are the point.

Why these extents. The ratio 6 : 5 puts each cascade's texel at 1–3 screen pixels at the near
end of its use range (C0 8 px per texel on the own ship at 30 units, as run 37 showed and the
user accepted; C1 3.0 px at 250 units; C2 1.25 px at 1500) and below one pixel over the rest,
so the apparent softness is roughly uniform across the seams. C2 at 7500 covers a 2–3 km
complex from the ship at 73 cm per texel, adequate for structure tens of metres across;
[directional-shadows.md](directional-shadows.md) §4's 5,000–50,000 slice is dropped: beyond 3 km
a station's shadow is sub-pixel and the texel would be 12 units.

**Camera-centred, not a held sun-space centre.** Texel snapping already removes translation
swim (the sun basis is world-fixed, so there is no rotation swim), and a map replayed from a
snapped centre is the previous map shifted by whole texels. Holding C2's centre with hysteresis
buys nothing while the apply composes the *retained* basis with the *current* camera
(`shadow_replay_view_rows(camera_now, basis_retained, cascade)` is already the API shape, so a
map from frame N is sampled correctly on frame N+1 after camera motion). A held centre plus a
caster-change hash would let C2 skip replays entirely while nothing moves; deferred until the
cost line says it is needed.

**Depth range towards the light (new, required).** The smallest cascade containing a pixel
is the one sampled, so it must contain every occluder of that pixel: a station arm 2,000 units
towards the sun over the ship shadows it, and today's ±512 range excludes that caster from C0
(the pixel would be lit while C1 knows better). Each cascade's sun-space z range becomes
asymmetric, `[−7,500·2, +E_behind]`: C0 `[−15,000, +512]`, C1 `[−15,000, +3,000]`, C2
`[−15,000, +15,000]`. `R32F` over 15,500 units resolves 0.002 units, `D24X8` 0.001. The bounds
verdict is unchanged (`z ∈ [0, 1]` in normalized coordinates); the projection helper takes
`depth_toward_light`/`depth_behind` instead of `depth_half_range`. The C0 caster set grows by
the sun column above the ship (a handful of draws). This is why bias moves to world units: 1
unit is 6.4e-5 normalized over 15,500 versus today's 0.001 over 1,024.

## 1. Per-cascade caster selection and the draw budget

One record list, one bounds pass. The draw's eight object-space AABB corners go through its
rows to view space once (today's 200 flops); with the shared sun basis the corners' sun-space
AABB in world units is computed once and tested against three intervals (`|xy − c_i| ≤ E_i`,
`z` in the range) for a 3-bit cascade mask on the record. Storage grows 512 → 1024 records
(≈ 150 B each, 150 KiB, preallocated); per-cascade caps C0 128, C1 512, C2 1024, drop order
submission order as today; `capped` becomes per cascade. Hot-path delta: two interval tests
and a mask per z-writing managed draw, no allocation, no device call.

Issues per frame is the cost driver: a record replays once per cascade it meets. At the
ledger's 1.3 µs per draw (run81/run109; 2.5 µs pessimistic from six calls × 0.42 µs):

| Frame | Issues | CPU replay | Share of a 22 ms busy frame |
| --- | --- | --- | --- |
| median (8 + 93 + 93) | 194 | 0.25 ms (0.48) | 1.1 % (2.2 %) |
| C0 + C1 only, worst (49 + 300) | 349 | 0.45 ms (0.87) | 2.1 % (4.0 %) |
| C2 frame, realistic worst (49 + 300 + 930) | 1,279 | 1.66 ms (3.20) | 7.6 % (14.5 %) |
| hard bound at the caps (128 + 512 + 1024) | 1,664 | 2.16 ms (4.16) | 9.8 % (18.9 %) |

Plus the transaction's fixed ≈ 40 µs (one stateblock capture/apply for all cascades, three
`SetRenderTarget` + `Clear` pairs; not three transactions) and the GPU fill: a trivial
four-`dp4` VS, depth-only raster, clears of 84 MiB of colour and 64 MiB of depth. GPU cost is
not measurable by query on this backend (`D3DERR_NOTAVAILABLE`); the at-rest hotkey A/B is
the instrument.

**Budget policy.** `B = 640` issues per frame (0.83 ms at 1.3 µs, 1.6 pessimistic). C0 and C1
replay every frame (their sum is bounded by 640 by the caps). C2 replays every frame while
`C0 + C1 + C2 ≤ B`; otherwise on even frame numbers only, always in full (a partial map pops
shadows; never publish one). Averaged, the realistic worst becomes ≈ 1.06 ms (2.0) per frame,
≈ 5–9 % of the busy frame, against ≈ 0.3 ms at the median. The off-frame apply samples the
retained C2 map with its retained basis and the current camera; a moving caster's shadow in
C2 is then one frame stale on odd frames (16 units at 1,000 units/s = 4 texels; ≈ 1.6 px at
5,000 units). Camera motion never stales it. A Reset, a refused frame or a lost device
invalidates the retained C2 (absent cascade = lit) until the next full replay.

## 2. The apply quad

Per pixel: RT2 read and view position as today; three sun-space projections (9 `dp4`, all
cascades, before any branch); `ddx/ddy` of the *view position* once (6 derivatives) instead
of per-cascade `muv`/`z` derivatives — the sun rows are affine in `p_view`, so each cascade's
`duvdx/duvdy/dzdx/dzdy` is arithmetic on those, safe after a branch. Selection: the first
cascade `i` with `max(|x_i|, |y_i|) ≤ 0.95` (two texels of margin for the kernel) and
`z_i ∈ [0, 1]`; blend weight `t = saturate((max(|x_i|, |y_i|) − 0.85) / 0.10)`; in the band
(`t > 0`) a ps_3_0 dynamic branch also runs cascade `i+1`'s 3×3 and `f = lerp(f_i, f_{i+1}, t)`.
Outside every cascade, lit. Deterministic and twin-checkable; the dithered alternative
(stochastic cascade choice by `jitter_index`, one PCF) is cheaper but rests on the resolve's
history to hide the seam and loses. Selection by view depth loses outright: the cascades are
camera-centred cubes in sun space, not frustum slices, so a pixel at depth 200 but 300 units
sideways would be assigned C0 and be outside it (a lit hole).

Bias and kernel per cascade: the kernel stays 3×3 texels of the sampled map (its world
footprint scales with the texel: 1.5 / 4.4 / 11 units), rotated by `jitter_index` as now;
receiver-plane gradient `g` from that cascade's derivatives; constants per cascade in
normalized depth from world units, `bias_i = (b_const + b_texel · texel_i) / range_i` and
`bias_max_i` likewise (`c1.w`, `c6.x` become per-cascade float4s; ≈ 16 constants in all).
Slot estimate ≈ 110–120 (today ≈ 50), four samplers, one quad; expected < +0.1 ms on the
0.3–0.5 ms class quad at 768p.

## 3. TAA, Reset, F8

TAA: the quad still runs before the resolve; static geometry yields an identical map on every
frame, so alternate-frame C2 changes nothing temporally except the one-frame staleness of
moving far casters (above), a sub-2-px edge oscillation the history absorbs like the rotated
kernel's dither. Maps are rendered from pre-jitter rows and are jitter-independent.

Reset: `ShadowReplayPass` grows to N maps plus the shared attachment (or N instances borrowing
one attachment); `before_reset` releases all maps, the attachment and every retained basis;
`after_reset` clears the pending flag; the retained-C2 validity flag is per device and is
cleared with the maps, so a stale map can never be applied. Cascade validity per frame:
C0/C1 valid only when replayed this frame; C2 valid when replayed this frame or retained
from a frame after the last Reset/refusal.

F8: one `readback_surface` per cascade (`shadow_map0..2`), one `shadow_replay_map_basis` line
per cascade with `cascade= extent= depth_light= depth_behind= center= replayed_frame=` (the
C2 line's `replayed_frame` may lag the capture frame), and `sun_shadow_apply_params` carrying
the three row sets and per-cascade bias/texel. Size: 4 + 16 + 64 = 84 MiB per F8 frame (32
frames ≈ 2.7 GiB, local and untracked); the twin needs the exact texels, so no downsample.

## 4. Fixture and twin

Extend `sun-shadow-apply` (the analytic box-on-plane case) and `sun_shadow_apply.py`
(per-cascade parameters, `cascade` in `frame_params`) with: (a) far plane — receiver plane at
1,000 units, box caster, footprint from the analytic projection, C1 only, edge within one C1
texel; (b) seam — a plane and a caster whose shadow edge crosses the C0/C1 band, asserting
the edge position on both sides within one C1 texel and the factor monotone through the
band (no lit gap, no double darkening); (c) sun column — an occluder inside C0's `xy` but
2,000 units towards the light shadows the receiver (the asymmetric range); (d) retained C2 —
replay on frame N, move the camera on N+1 with C2 skipped, the world-space edge unchanged
within one C2 texel; (e) Reset between N and N+1 — C2 absent, the pixel lit, the next frame
replays. Counters: `shadow_replay_candidates` gains `c0= c1= c2= capped0/1/2=`,
`shadow_replay_depth` gains `draws0/1/2= far_replayed= far_frame=`; the fixture asserts them
per frame as the existing L/F case does. Fixture map sizes stay 256² with the extents
narrowed by the seam variables.

## 5. What run 38 must measure (single map 4096² at extent 1500, the baseline)

1. `shadow_replay_depth us=` against `draws=` over the session: slope (µs per draw at 50–500
   draws) and intercept (fixed cost with a 4096² clear) — settles 1.3 versus 2.5 µs.
2. `shadow_replay_candidates bounds=`/`capped=` at extent 1500 (the C1 set: median, max)
   and, in a second segment, at extent 7500 with cap 512 (the C2 set; `capped` shows the
   excess over 512).
3. `frame_end` median with the apply on versus off at the station (hotkey A/B at rest): the
   GPU fill and clear of a 4096² map, the only instrument for it.
4. F8 map occupancy at the station (20 % at 1024²/250) and the twin's `f<0.9` fraction on the
   hull with the world-unit bias (acne check at texel 0.73).
5. The user's verdict on the own-ship self-shadow at texel 0.73 versus 0.49, on the shadow
   cut at the 300 m box border, and on station-on-station within 300 m.

**Cheapest interim.** One 4096² map at extent 1500, camera-centred and snapped (no further
stabilisation needed while it replays every frame): acceptable as the run-38 baseline and for
one station's structure within 300 m of the ship. It breaks at (1) the box border — shadows
end in a hard cut 300 m from the ship on a big deck, and nothing beyond casts; (2) the own
ship — 12.5 px per texel at 30 units versus 8.3 today, 1.5× blockier self-shadow on a fighter
(a 10 m hull is 68 texels across); (3) memory 128 MiB with `D24X8`, 96 with `D16`; (4) the
512 cap on a pool up to 930, dropping last-submitted casters (popping); (5) a 4096² clear and
fill every frame for a map whose near quarter is what matters. Two maps (today's C0 plus a
4096² at 1500) fix (2) at once and are one cascade-selection step from the full design; that
is the recommended first implementation checkpoint, C2 the second.

## 6. Native Windows

Documented D3D9 only: several `R32F` render-target textures and a shared `D24X8`
depth-stencil at least as large as each target, `MaxTextureWidth/Height ≥ 4096` checked at
attach (halve the far sizes otherwise), four samplers and a dynamic `if` in ps_3_0, one
`D3DSBT_ALL` block, the application's own buffers and declaration. No Wine export, layout or
hash. Unverified natively like the rest; add the row to
[platform-portability.md](platform-portability.md).

## 7. Alternatives that lose

- **One big map** (the interim above): self-shadow resolution and the hard border.
- **Two cascades 250 / 7500**: C2's texel is 7.5 px at 250 units, exactly where the ship's
  shadow lands on a deck (50–400 m); too coarse.
- **Minimum over every containing cascade** (27 taps for near pixels): triples the near
  quad cost and the coarse cascades' bias produces acne on the hull; the asymmetric depth
  range gives the same occluder coverage for one interval test per draw.
- **Atlas in one texture**: no sampler pressure to relieve, caps risk at 8192.
- **Frustum-fitted slices instead of camera-centred cubes**: sharper theoretical texel use
  but the map changes with every camera rotation (no reuse, swim through the rotation);
  the sun-fixed cube is what the snapping and the retained-basis reuse rely on.

## Unknown, and what settles it

- C1/C2 caster counts and the per-draw slope at hundreds of draws: run 38 (§5.1–2).
- GPU fill/clear cost of 4096² on wined3d: run 38 (§5.3).
- `MaxTextureWidth` and `CheckDepthStencilMatch` for a DS larger than the RT on this backend:
  the attach log at first run; the fixture's caps line.
- Whether a held C2 centre plus a caster-change hash is worth a per-frame hash of ≤ 1024
  records (≈ 10–20 µs): only if run 38 shows C2 dominating at rest.
- Native Windows behaviour: untestable here; source is documented-API only.
