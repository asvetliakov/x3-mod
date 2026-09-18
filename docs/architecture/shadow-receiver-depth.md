# Sun-shadow receiver depth: a precise channel in the lane's RT2

Design note, 2026-09-18, **ratified by the orchestrator 2026-09-18** (gated first flight, default `device`; flip after the flight). **Flipped 2026-09-18 after run 41 A2** (§5): `linear` is the only encoding. Question: how the apply pass's
receiver depth is made precise enough that the cascade PCF at 20–40 km no longer re-rolls
from one fp32 ULP of RT2 (`docs/verification/directional-shadows.md`, "Run 40 A (run117)
diagnosis"; `shadow-cascade-extents.md` §6 "Depth precision"), without breaking the other
consumers of RT2. Inputs: the two notes, `legacy-sun-application.md` (RT2 contract),
`src/temporal/current_depth_ps.hlsl`, `src/renderer/material_motion.cpp` (vertex export
`:633–640`, `depth_fragment` `:401–445`), `sun_shadow_apply_ps.hlsl`,
`sun_shadow_cascade_apply_ps.hlsl`, `ao_linearize_ps.hlsl`, `resolve.hlsl`,
`temporal_pass.cpp`, `ambient_occlusion_pass.cpp`, `sun_shadow_apply_pass.cpp`,
`shadow_replay_projection.h:112`, `verification/probe/sun_shadow_apply.py` and the committed
apply records. Host reading only: no build, no Wine, no source edit. **M** measured, **I**
inferred, **A** assumed.

## Decision

**Widen the lane's RT2 from `G32R32F` to `A32B32G32R32F` and write the interpolated clip
`w` (view depth, linear) into `.b`; `.r` stays device `z/w` and `.g` the share, bit for bit.**
The apply quads read `z = RT2.b` instead of `m32 / (RT2.r − m22)`. Nothing else reads `.b`.
TAA, AO, the depth histories and the ZFUNC-EQUAL depth evidence keep reading `.r` unchanged,
so their outputs stay byte-identical; only format gates and readback labels change. Gate it
by one device-creation option for the first flight (§5), then make it the only path.

Why this and not linear depth in `.r`: precision is identical, but `.r` is the TAA resolve's
device-depth input to the clip-space reprojection (`resolve.hlsl:190`, `:201`, `:258`,
`validDepth` in [0, 1] `:78`, far-plane policy `depth = 1` `:151`, RT1 `motion.z` `:208`, the
D24X8 snapshot path) and AO's linearize law. A third channel costs 8 B/px of bandwidth; a
new `.r` costs a rework of the renderer's primary feature and its byte-exact records.

## 1. Encoding and precision

The vertex variant already exports `(z, w, z, w)` of the rasterized position
(`material_motion.cpp:633–640`, two `dp4` against WVP rows 2 and 3); the pixel fragment is
`rcp r, v.y; mul oC2, v.x, r` (`current_depth_ps.hlsl`, shape enforced at `:401–445`). The
change appends `mov oC2.zw, v.yyyy` (3 slots, no literal, no constant; a partial `oC#` mask is
already proven by the share producer's `mov oC2.g`, `linear_material.h:112`). The share
fragment writes `.g` after, as today. With the lane off RT2 stays `R32F` and the extra lanes
are dropped by the format: one fragment for both modes, so lane-off output is unchanged.

View depth `w` in fp32 has relative error 2^-24; device depth `d = m22 + m32 / w` has
`dw = w² / |m32| · dd = w² · 1e-8` per ULP (m32 = −6.0000184, m22 = 1.000003,
`motion_output.cpp:1817`). Owning cascade texels at 4096²: C1 0.73 u, C3 18.3 u, C4 73.2 u.

| Range | Old: 1 ULP of z/w | Old, texels | New: 1 ULP of w | New, texels | Cascade |
|---|---|---|---|---|---|
| 1 km | 0.010 u | 0.014 (C1) | 6.1e-5 u | 8.3e-5 | C1 |
| 10 km | 0.99 u | 0.054 (C3) | 9.8e-4 u | 5.3e-5 | C3 |
| 37 km | 13.6 u | 0.74 (C3) | 3.9e-3 u | 2.1e-4 | C3 |
| 92 km | 84 u | 1.15 (C4) | 7.8e-3 u | 1.1e-4 | C4 |

Noise budget after the change (**I**): the VS `dp4` rounds `w` to ≈ 1 ULP of its largest
partial (the translation term ≈ w), the interpolator adds a few ULP (**M** 2026-09-18,
`run_material_motion.py`: ≤ 3.41 ULP at 1280×768 and ≤ 0.41 at 5120×1440 against the analytic
`w`; the fixture's 32×32 configurations show up to 52.6, the rasterizer's sub-pixel vertex
snapping of a 64-pixel triangle, which scales with 1 / width and does not apply at game
resolution; 0.016 u at 37 km, 0.03 u at 92 km at 4 ULP), the map's normalized depth is stored in R32F over R =
375–600 km (0.02–0.04 u, **M**, diagnosis §1). Combined ≤ 0.07 u = 4e-3 C3 texel, 1e-3 C4
texel, against the diagnosis' requirement of ≤ 0.05 texel (4 u at 92 km): a 50× margin.

Predicted residual re-roll. The twin measured |Δf| ≥ 2/9 on 9.1 / 14.2 / 15.7 % of owned
pixels (24624 C3, 14780 C3, 14780 C4) under ±13.6 / 13.6 / 85 u; the margin distribution on
those faces has p25–p75 of −41…+36 u and −49…+80 u, so for quanta far below that spread the
flip fraction scales with the quantum (**I**): 9.1 % × 0.02/13.6 = 0.013 %, 14.2 % × 0.02/13.6 =
0.02 %, 15.7 % × 0.03/85 = 0.006 %. **Predicted < 0.05 %, target < 1 %.** Not covered by this
prediction: the geometric term, i.e. the jitter moving each pixel's surface point by up to
1.5–2.4 map texels on girder-scale structure; the twin's kernel-rotation test changed 0
pixels, and this term is the same self-shadow comparison the bias law already handles, but
only the flight's burst measures it (§4).

## 2. Alternatives and why they lose

- **Linear `w` in `.r`, same `G32R32F`.** Same precision, zero bandwidth, one slot fewer per
  routed program (`mov` replaces `rcp, mul`). Loses on blast radius: the resolve reprojects
  `.r` as device depth through `clip_to_previous` and compares it with the previous RT2 and
  with RT1's `motion.z` in device units, with `rejection` in device-depth units and the [0, 1]
  validity, far-plane and D24X8-snapshot paths; every site needs `m22 + m32 / w` (2 per
  pixel, 4 per disocclusion tap, one new constant), the R32F lane-off path changes too, AO's
  linearize changes, `run_material_motion.py`'s ZFUNC-EQUAL replay comparison against the
  hardware depth stops being the same quantity, and every TAA byte-exact record is
  invalidated. The shadow lane would be re-qualifying the primary feature.
- **Reversed `(w − z) / w` in `.r`** (the diagnosis' option a). Precise only when `w − z` is
  formed in the VS from a *new* per-draw constant row (row 3 − row 2, +1 vertex constant in
  the ABI window, +1 `dp4`), because `w − z` from the two interpolated fp32 values carries
  ULP(37,000) = 0.004 u → 1e-7 of `d`, the same noise as today. It then still changes `.r` for
  TAA and AO exactly as the linear variant, with `1 − d` inversions on top. No advantage.
- **Share + depth packed in `.g`** (option c). Keeps 8 B/px, but quantizes the share to 8
  bits, needs `frc` and an unwrap from `.r` in the apply (+≈ 6 slots on a 499-slot program),
  constants and a `frc` in every share producer, and a decode in every tool that reads `.g`.
- **Apply-side** (bias, kernel, stencil, footprint selection): measured floor 2–6 % (diagnosis
  §2); not a fix.

Budgets: the transformed programs are all ps_3_0 (`material_motion.cpp:333, :406, :721`
refuse anything but `0xffff0300`; ps_2_0 never enters the lane), the largest original share
variant ≈ 230 of 512 slots, +1 here. The cascade apply is 499 of 512 (`shadow-cascades.md
:280`); this change removes its `rcp` (−1) and, while gated, adds a `cmp` (+2): ≤ 501
(**M**: 500 gated; the single-map apply 225 → 226; the depth fragment 2 → 3).

## 3. Consumers and what each changes

All ps_3_0 unless noted; formats and calls are documented D3D9 (`A32B32G32R32F` render
target, point-sampled texture, MRT with independent bit depths — RT0 `A16B16G16R16F` and RT1
`A32B32G32R32F` already require `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`; RT2 at 128 bits adds
no cap and is already in the lane's `CheckDeviceFormat` list, `sun_share_lane_inc.h:26, :55`).
No wined3d assumption; native behaviour unverified like the rest of the renderer
(`platform-portability.md` row to update).

| Consumer | Reads | Change |
|---|---|---|
| Depth fragment (`current_depth_ps.hlsl`, `depth_fragment`) | – | `mov oC2.zw, v.yyyy`; MOV whitelisted, 3-instruction shape, `outputs == 2` writes to the relocated `oC2` |
| Route (`motion_output.cpp:2168` creation, `:1770` format field, `:5860` F8 readback) | – | lane RT2 `A32B32G32R32F` (gated, §5); readback 16 B `rgba32f`, log format `rgba32f_row_major`; sentinel fill draw unchanged (format-agnostic, `.b = −1`) |
| Lane qualification (`sun_share_lane_inc.h:86`) | – | the probe's RT2 format follows the option |
| Apply, single map and cascades (`sun_shadow_apply_pass.cpp` two `desc.Format != G32R32F` gates; both `.hlsl`) | `.r .g` → `.b .g` | `z = ds.b`; `valid` keeps `d >= 0` on `.b` (sentinel −1); `m22/m32` no longer enter `z` (they stay for `steady`) |
| TAA (`temporal_pass.cpp:63, :270, :274, :322–329`; `resolve.hlsl`) | `.r` | admit `A32B32G32R32F` as depth input; `depth_draw = format != R32F`; the identity copy already point-samples `.r` into R32F histories; resolve untouched, bits identical |
| AO (`ambient_occlusion_pass.cpp:291`; `ao_linearize_ps.hlsl`) | `.r` | admit the format; output identical |
| `ShadowReplayPass` | – | not a consumer (no RT2 read in `shadow_replay_pass.{h,cpp}`; its `depth` is the map attachment) |
| Twins (`sun_shadow_apply.py` `unpack_rt2` 2→4 floats, `depth_%d_%d.rgba32f`, `z = b` behind a `depth_encoding` parameter; `analyze_sun_share_lane.py:85` format check) | dump | old captures and records stay loadable under `device` |
| Fixtures (`motion_output_sun_apply_inc.h:82, :84, :233` synthetic RT2; `linear_material_fixture.cpp:1197, :1208` lane target; `run_material_motion.py validate_depth`) | – | synthetic `.b` = view depth; the share fixture proves `.b` survives the `.g` write; nine new `.b` samples against the analytic `w` |
| Docs (`legacy-sun-application.md` RT2 contract, `shadow-cascades.md` §2, `platform-portability.md`) | – | contract line, format row |

## 4. Verification: provable before a flight

Host, no Wine, in `verification/analysis/test_sun_shadow_apply.py` (new case
`ReceiverDepthPrecision`): a single-sided plate with a girder pattern (300 u depth spread
over 3×3 texels, the diagnosis' p50) under C3 rows at 37 km (18.3 u texel, 2.9-texel pixel
footprint) and C4 rows at 92 km; the map is the plate's own depth at texel centres, the
receivers the analytic surface on the pixel grid. Encode the receiver as fp32 `z/w` and as
fp32 `w`, run `expected_factor_cascades` at ±1 ULP of each encoding: the old encoding must
show ≥ 5 % of owned pixels with |Δf| ≥ 2/9 (the run117 class reproduced), the new ≤ 1 %
(expected ≈ 0.02 %), and the new-encoding `f` must equal the float64-exact `f` on ≥ 99.9 %
of pixels. **M** 2026-09-18 (`ReceiverDepthPrecision`, 25,600 owned pixels per cascade, a
sinusoidal 300 u girder relief, 87 % / 75 % of the plate self-shadowed): device 18.8 % (C3) /
83.5 % (C4) re-roll at ±1 ULP, linear 0.004 % / 0 %, linear `f` equal to the float64 `f` on
99.996 % / 99.992 %. Second, on captured data: `tools/analysis/shadow_receiver_reroll.py` takes an F8
burst directory (untracked) and repeats the diagnosis' ±1 ULP test on 24624 / 14780
(baseline 9.1 / 14.2 / 15.7 %), then with ±ULP(w) applied to the reconstructed view depth;
the result goes into the ledger and is the witness on real geometry. Both run before any
build. Host structure tests (`test_material_motion_structure`, `test_original_sun_share`):
fragment shape, lane off ⇒ byte-identical to the motion variant.

Wine, queue owner, in order: `generate_rigid_motion_pixel.py --shader sun_shadow_apply
--shader sun_shadow_cascade_apply --check` (slots ≤ 512, `sun-shadow-*-program.json`);
`run_material_motion.py` (`.r` analytic within 4e-6 and ZFUNC EQUAL unchanged, `.b` within
4 ULP of `w`); `run_linear_material.py --original-sun-share` with the wide lane target;
`run_motion_output.py` apply cases, regenerating the bottle-X3 apply records
(`sun-shadow-apply`, `-wide`, `-cascades`: 6 + 6 + 29 frames, and `-cascades-5`, `-5-faces`:
15 + 16) against the twin under `linear`; `run_ambient_occlusion.py` (bytes identical to the
`G32R32F` record); `run_sun_share_live.py` `shadow_apply` (TAA output equal to the CPU
reference, Reset at frame 4, unavailable frames byte-identical) and the TAA fixture that
exercises the depth-draw copy. TAA and AO frames are expected byte-identical because `.r`
is produced by the same two instructions; any difference is a defect, not a tolerance.

Flight: two launches at the run117 spots (outpost 37 km, station 21 km), option off and on,
F8 bursts; the frame-to-frame |Δf| ≥ 2/9 fraction on C3/C4-owned pixels (the diagnosis'
method) < 1 %, and the `frame_end` median delta (§6).

## 5. ABI, gate, Reset, rollback

**Flipped 2026-09-18 after run 41 A2** (`/tmp/x3-bottleX3-run124`: the distant-station flicker
gone under `linear`, still present under `device` in A). The lane's RT2 is `A32B32G32R32F`
unconditionally (`lane_depth_format()`: wide on the lane, R32F off it); the quads read
`z = ds.b` with no `cmp` (`limits.z` / `select.w` uploaded as 0, reserved; `terms.x/.y`
uploaded, unread); the apply gates require the wide format (R32F or `G32R32F` ⇒
`skip("format")`; the attach gate checks `A32B32G32R32F` instead of `G32R32F`); the F8 RT2
readback is always 16 B/px `rgba32f`; the DLL reads no `X3M_SUN_SHADOW_RECEIVER_DEPTH`.
`manage.py` keeps `--sun-shadow-receiver-depth linear` as a deprecated no-op (the queued run-41
lines stay valid) and refuses `device`. The twin (`sun_shadow_apply.py`) keeps `depth_encoding`
so old device records and F8 dumps (`.rg32f`) still load; the fixtures and runners run only the
linear cases and the committed apply records are the former `-linear` siblings under the base
names. Slots after the flip: single-map 226 → 221; cascades 509 → 509: the `cmp`, the
divide and their moves leave (−5 instructions) but the compiler now forms `p` with one more
`mad` and hoists one more `dsx`/`dsy` pair (2 slots each) before the first `ifc`/`rep`, where
derivatives must sit, so the count is unchanged (2173 words, headroom 3 of 512). The paragraph
below is the gated design as flown.

RT2's format was fixed at creation (`sun_lane_active_ ? G32R32F : R32F`) and recreated by
the route after Reset; the apply, TAA and AO passes gate the format per frame. One option,
`--sun-shadow-receiver-depth {device,linear}` (`X3M_SUN_SHADOW_RECEIVER_DEPTH`, read once at
device creation, default `device` for the first candidate), selects `G32R32F` or
`A32B32G32R32F` and uploads `select.w = 1` (c3.w, unused today) so the apply computes
`z = select.w > 0 ? ds.b : terms.y / (ds.r − terms.x)`. With the option absent the candidate
is behaviour-identical to today (same format, same bits, +1 dead slot per routed program).
It cannot toggle at runtime (Ctrl+Shift+F12 does not change the format): A/B is two
launches. After the flight ratifies it, flip the default and delete the device path, the
`cmp` and the `G32R32F` gates in the next candidate; the twin keeps `depth_encoding` for old
records. Reset: nothing new (RT2 rebuilt by the route, pass gates re-check). Rollback: the
previous DLL and manifest kept by `manage.py`.

## 6. Cost

Per routed fragment: +1 ps slot (≈ 200 → 201 of 512, the fill twin's worst case 230 → 231),
+8 B of RT2 write. Per frame at 1280×768 (**I**, 0.98 M px): RT2 memory +7.5 MiB; sentinel
fill +7.5 MB; routed fragments +8 B × ≈ 1.5 M (overdraw, **A**) ≈ 12 MB; apply read +7.5 MB;
TAA depth-copy read +7.5 MB; AO linearize at half resolution +2 MB; ≈ 45 MB/frame extra
traffic, ≈ 0.2 ms at the 250 GB/s model of `shadow-cascade-extents.md` §6 (**A**), ≈ 1 % of a
16 ms frame. The apply quad loses its `rcp` (499 → 498; 501 while gated). F8 dumps double the
RT2 file (capture only). CPU: none per draw. Acceptance number: the flight's `frame_end`
median, option on versus off at the same spot; if it exceeds 0.5 ms the fallback is the
linear-in-`.r` variant of §2 with the resolve conversions, not a bias or kernel change.

## Depth origin snapping (`shadow_replay_projection.h:112`)

Leave it. The basis snaps the centre in x/y only; the depth origin follows the camera, so
every map texel's stored depth drifts by (Δcentre · forward) / R per frame. The apply uses
the same rows, so the drift is exact for it (diagnosis §1, re-aligned maps: 0–5 texels
> 1e-3); the only re-roll it leaves is one fp32 ULP of normalized map depth (0.02–0.04 u),
below the new receiver noise and 20× below the 0.05-texel target. Snapping it would change
every fixture's rows and confound the flight's A/B; it is a separate one-line follow-up if
bit-stable maps are wanted for offline diffs.

## Unknown, and what settles it

1. Interpolator precision of a `w`-valued attribute on this backend (FEX / wined3d / Metal):
   measured ≤ 3.41 ULP at 1280×768 and ≤ 0.41 at 5120×1440 (`run_material_motion.py`
   `DEPTH_W_SAMPLE`, 2026-09-18; the 32×32 configurations' 52.6 ULP is the rasterizer's
   sub-pixel vertex snapping of a 64-pixel triangle, ∝ 1 / width). Natively: unknown.
2. The bandwidth cost of a 16 B/px RT2 on the hot path: the flight's `frame_end` delta.
3. The geometric jitter term of the residual, not in the twin: the flight's F8 burst.
4. Native Windows execution of the wide MRT: unverified, as the rest of the renderer.
