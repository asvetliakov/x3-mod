# Ambient occlusion at X3's scale: keep, drop or opt-in

Design note for goal 7, written 2026-09-15 after runs 19 and 20, for ratification by the main
session. Owning implementation note: `ambient-occlusion.md` (steps 1, 1b, 2); ledger:
`../verification/ambient-occlusion.md`. Nothing here is implemented.

**Ratified 2026-09-15 (orchestrator) with one amendment:** the appearance question is answered
before the footprint rule is implemented. Run 21 uses the existing `--ao-radius 200` (no debug
view) as the footprint proxy: 64 px (capped) under 800 m, 25.6 px at 2 km, 10 px at 5 km, which
sits in the readable band at the distances run 20 measured. If the user finds the effect worth
keeping, the `R(d)` rule below is implemented as the next candidate; if not, AO leaves the
default path and the chain stays only as the base for v2.

## Decision

**Keep AO, default-off, and replace the one-metre radius by a constant screen footprint clamped
between two world radii** (`R(d) = clamp(f·d/256, R_min, R_max)` at 1280×768, proposed defaults
f = 16 half-res px, R_min = 2 m, R_max = 300 m, strength 0.5 unchanged), then test the appearance
in run 21 without `--ao-debug`. Ship this v1 rule first because it is a four-slot shader change and
two launcher options, and it is the first configuration whose footprint lies in the readable
8–32 px band at the distances X3 actually shows (run-20 depth: 300 m–1.4 km on the station
approach, 1–4 km on the asteroid field, 5–6 m for the own ship). v2 (sun-direction weighting) stays
the next step and is the physically consistent form for X3, which has no ambient term; if run 21
shows nothing worth keeping at the new footprint, drop AO from the default path and keep the
chain only as the base for v2.

## 1. Evidence from the two runs

- Run 19 (`radius_m=2`): the pass ran (12,163 frames `reason=ok`, `cpu_us` median 136 µs) and
  the user saw no difference. Run 20 (`radius_m=20`) was launched with `--ao-debug` for the whole
  session (`ambient_occlusion_mode … radius_m=20 strength=0.5 debug=1`; 4,464 attached frames,
  `cpu_us` median 135 µs, mean 211 µs, p90 391 µs, n = 4,464), so "mostly white, twin gray lines
  when close" describes the *factor buffer*, not AO applied to the image. All capture groups fell
  in `reason=disabled` gaps; no capture in either run shows AO.
- The per-pixel law (`ao_gtao_ps.hlsl:82`, constants `ambient_occlusion_pass.cpp:329–335`) is
  `radius_px = clamp(R_units·m11·(h/4)/z, 2, 64)` in half-res pixels. With m11 = 4/3
  (`camera-numerics.md`), h = 768 and 5 units/m this is **`radius_px = 256·R_m/d_m`**
  (360·R/d at 1080p). The logged `radius_px` is the same law at a fixed 20 m and says nothing
  about the scene.
- Scene distances, from the run-20 `depth_1_<frame>.r32f` captures (RT2, linearized with
  m22/m32; only routed pixels count):

  | frame | routed px | d p10 / p50 / p90 (m) | footprint at R = 20 m, p10/p50/p90 (half px) | px at the 2 px floor |
  | --- | ---: | --- | --- | ---: |
  | 23902 (station approach) | 66.7 % | 312 / 468 / 1,435 | 3.6 / 10.9 / 16.4 | 1.5 % |
  | 10663 (close to a station) | 53.0 % | 10 / 36 / 72 | 64 / 64 / 64 (cap) | 1.8 % |
  | 8791 (field, own ship in view) | 16.0 % | 5 / 385 / 3,163 | 2.0 / 13.3 / 64 | 14.6 % |
  | 4817 (zoomed asteroid field) | 12.0 % | 5 / 1,092 / 3,883 | 2.0 / 4.7 / 64 | 17.1 % |
  | 44490 (own ship, far object) | 6.7 % | 4.7 / 6.1 / 43,483 | 2.0 / 64 / 64 | 19.5 % |

  One frame spans 5 m (the own ship in the chase camera) to 4 km. A single metre value cannot
  put the footprint in the readable band on both ends: 20 m is 64 px (capped, an effective
  1.5 m) on the own ship, 11 px at 470 m and 2 px beyond 2.5 km; 100 m is 51 px at 500 m and
  8.5 px at 3 km. That is why run 19 was invisible (2 m: 1 px at 500 m) and why a fixed 20 m
  would still be marginal on the station approach.

## 2. What open-space games do (training knowledge, not verified online)

From training data, unverified against current builds; treat as background, not as facts of
record. Elite Dangerous exposes an SSAO quality option; X4: Foundations exposes an SSAO
Off/Low/Medium/High option; Star Citizen's engine runs SSDO (directional screen-space occlusion)
permanently; Everspace 2 uses Unreal's SSAO/GTAO; Homeworld 3 is on Unreal with its default AO;
No Man's Sky gained an ambient-occlusion option in a later update (least certain). The common
pattern I am confident of: every one of them has an indirect/sky term for AO to modulate, and
the radius is human-scale (roughly 0.5–3 m world) with a screen-space cap, so in open flight the
effect reads in cockpits, hangars, on planet surfaces and on structures within a few hundred
metres, and is minor beyond that; what makes ships and stations read at distance in those games is
directional shadowing (shadow maps, screen-space shadows) and environment specular, not AO. The
scale rule these engines share is the one proposed here: AO reads when a crease's occlusion
footprint is on the order of 8–30 px and there is non-directional light to attenuate.

X3 is the degenerate case: `g_LightAmbientIntensity` has no consumer and `D3DLIGHT9.Ambient` is
stored as zero (`camera-state-and-frame-routine.md`, "Ambient occlusion inputs"), and the
converted material is `L = A·(P + M + D) + R + E` with no ambient term. A physically correct AO
would therefore darken nothing. What step 2 ships is a full multiply with a 0.5 floor, i.e. a
directionless contact-shadow proxy that only shows on sunlit faces near creases; the honest
long-term form is v2, sun-weighted occlusion, which is screen-space self-shadowing and serves
objective 8 as much as objective 7.

## 3. Scale rule and defaults

**Derivation.** For a footprint of 8–32 half-res px, `R/d` must be 1/32–1/8: at 500 m
R = 16–63 m, at 3 km R = 94–375 m, on the own ship at 6 m R = 0.2–0.75 m. No fixed R serves two
of these. A constant footprint `f` gives `R(d) = f·d/256`; with f = 16: 0.4 m at 6 m
(R_min = 2 m and the 64 px cap bind: 64 px = 1.5 m), 6 m at 100 m, 31 m at 500 m, 62 m at 1 km,
187 m at 3 km, 300 m (R_max) beyond 4.8 km, 7.7 px at 10 km, 3.8 px at 20 km. The horizon taps
sit at 2, 6, 10 and 14 half px per side (`(step + noise)·0.25·radiusPx`), inside the quincunx
blur's reach; f = 32 would put them 8 px apart and needs a run to justify.

**Why the rule beats a metre value.** (a) One frame spans three decades of distance (table
above). (b) The footprint no longer depends on the 0.2 m/unit calibration, which the step-2
AABB check left inconclusive: the calibration enters only through R_min/R_max. (c) It is
resolution-honest: the 1080p factor is 360, not 256, so a metre default tuned at 768p is 40 %
larger on screen at 1080p; a footprint in pixels is not. (d) The cost is nil (below). Its known
downside is that it is image-space obscurance, not sized AO: the same strut is darkened over the
same 16 px at 500 m and at 3 km, and at R = 300 m a ship 100 m in front of a hull darkens the
hull within 300 m of it (a contact shadow, not AO). The distance falloff (0.615 R) bounds that
to R; the run decides whether it reads as depth or as a halo.

**Strength.** Keep s = 0.5 (floor 0.5) for run 21. With no ambient term the multiply is the
whole effect; a larger footprint raises the number of partially occluded pixels rather than the
per-pixel depth, so the run's hotkey A/B is the right instrument and 0.5 the right starting point.
Do not raise it before the footprint has been seen.

**Scope.** No change: the multiply already applies only where RT2 is not the sentinel, i.e. the
169 routed SM3 rows (the 168 converted material pairs plus the routed glass), and native-path
pixels, backgrounds, particles, HUD and compositing are untouched (`ambient-occlusion.md` §1,
§3). "Whole scene" is not on offer without a depth for the SM2/SM1 draws; the converted-surface
scope is what exists.

**v1 first, v2 later.** v2 needs the world-space `LightDir_Dir0` rotated into view space per
frame, read from whichever of 11 registers the bound program declares (507 of 751 programs),
plus a new horizon weighting; that is a step with its own fixture. The footprint rule is a
per-pixel `R = clamp(f·zc/k, R_min, R_max)` in the horizon search, the falloff constants
(`radius.y`, `radius.z`) folded per pixel with one `rcp`, and two launcher options
(`--ao-footprint <half-res px>` replacing the metre semantics of `--ao-radius`, plus
`--ao-radius-min/--ao-radius-max <m>`), with the frame line reporting the three. Ship v1 to answer
the appearance question before paying for v2.

## 4. Materials

**X3's BUMPMAP families are tangent-space normal maps, not bump/height maps.** The reviewed
BUMPMAP contract (`linear-bump-materials.md` §"Sampler"): s1 is a DXT5nm normal (alpha → x on
the binormal, green → y on the tangent, z reconstructed as `RCP(RSQ(1 − x² − y²))`, then
`N = normalize(y·T + x·B + z·Ng)`); the vertex declarations carry BINORMAL0/TANGENT0 FLOAT3
(`0x0054e210`, stride 0x40, `mesh-buffer-rewrite.md` §5) and the loader `0x004bbb10` fills them;
the same reconstruction is documented for the Boron/Paranid and remaining-hull BUMPMAP paths.
DEFAULT families (3,600 pass occurrences against 1,776 BUMPMAP) use the geometric normal only. No
height channel exists anywhere in the reviewed contracts: the red/blue lanes of the normal texture
are unused, and the other samplers are diffuse, the red specular mask, the additive lightmap and
the cube. So the user's premise inverts: there is nothing to Sobel, and normal mapping is already
in the converted shaders.

**For occlusion the material question is moot.** GTAO reconstructs normals from the half-res
depth (5 taps), and normal maps do not perturb depth, so bump detail can neither help nor hurt
the occlusion term; occlusion from normal-mapped bumps would need bent normals at texel scale,
which is invisible at X3 distances for the same reason the 2 m radius was. For lighting the
material normal does matter (D and R lobes); v2's sun weighting would use the depth normal,
consistent with DEFAULT and slightly off the bumped normal on BUMPMAP, negligible at a 16 px
footprint.

**Sobel-to-normal at upload: unnecessary** (no height input). **Parallax: defer.** It needs a
height map that does not exist; integrating one from the normal map at load (Frankot–Chellappa)
requires decompressing and recompressing DXT5 blocks in an upload hook, and DXT5nm's unused R/B
lanes cannot hold it without degrading G (the BC1 sub-block shares endpoints), so it means a new
texture and a sampler (s5+ is free), a tangent-space view vector (T, B, Ng, V are all in the
BUMPMAP PS, so feasible) and 8–16 fetches per pixel on 320 BUMPMAP pass identities. It would read
only where texels are ≥ 1 px, i.e. on the own ship in the chase view (3 cm texels at 6 m are
~2.6 px), and nowhere on a station at 500 m. Negative cost/benefit for this game; not on the AO
path.

## 5. Cost, native Windows, verification

**Hot path.** Zero per-draw work, unchanged. The chain stays four quads at half res; the rule
adds one `mad`, one clamp and one `rcp` per half-res pixel to a 397/512-slot program (≤ 1 % of the
GTAO quad's ALU), two constants, no allocation, no new state. Measured in game: `cpu_us` median
135 µs (run 20, n = 4,464) and 136 µs (run 19); GPU bounded by the detached fixture at 0.51–0.78 ms
(768p) and unaffected by the radius. Disabled frames cost the log line only.

**Native Windows.** Shader arithmetic and `SetPixelShaderConstantF`; documented D3D9 only,
cross-compiled under the SSE2/stack contract; runtime unverified, as for the whole pass
(`platform-portability.md` bullet to extend when implemented).

**Fixture proof (extend `run_ambient_occlusion.py`).** (1) Regression: the five scenes with clamps
that pin R at the old value reproduce the current oracles (contact ring 0.957, crease 0.920, step
far side 0.822, plane and sentinel exact). (2) The property: the sphere-on-plane scene at z and
4z has a contact ring of the same half-pixel width (±1 px) under the footprint rule, and widths
in the ratio 1:4 under the fixed rule. (3) Clamp edges: R_min and R_max reached with the folded
falloff consistent (no discontinuity across the clamp). (4) A far tilted-plane scene at
z = 15,000–50,000 units to show the horizon is still 1 where depth quantization
(dz ≈ z²·10⁻⁸) meets a 300 m radius. Host: option parsing and the frame-line grammar.

**Run 21 acceptance.** Launch with `--ambient-occlusion --ao-timing` and the new defaults, **no
`--ao-debug`**. Views: an Argon station at 300–800 m (module joints, docking clamps), a capital
hull at 200–500 m with the sun at a grazing angle, an asteroid at 100–300 m, the own ship at rest.
At each, at rest: F8 with AO on, Ctrl+Shift+F11, wait two seconds, F8 again (the pair must be
seconds, not minutes, apart; run 19's pairs were 236 and 287 frames apart and measured camera
motion). Numbers: every enabled frame `attached=1 ran=1 applied=1 reason=ok` (no `failed`,
`attach`, `failed_limit`); `cpu_us` median ≤ 250 µs; from each capture pair, over pixels with
`depth ≥ 0`, the fraction whose on/off luminance ratio is < 0.9 is ≥ 5 % in the station view, and
the fraction darkened by > 2 % farther than 2·f full-res px from any depth discontinuity is < 1 %
(no halo). The user reports the on/off difference on the station and the hull, no crawling in
motion, HUD unchanged, and frame rate by feel. Optional two-minute session B with `--ao-debug`
only to explain the run-20 gray lines.

## 6. Alternatives considered

- **Fixed larger radius (50–200 m).** Serves one distance band: 50 m is 26 px at 500 m but 2.6
  px at 5 km; 200 m is capped (an effective 125 m smear) at 500 m and 1.5 m on the own ship. Loses
  to the footprint rule on the same shader cost.
- **Drop AO now.** Saves nothing on the default path (off costs one log line) and gives up the
  chain that v2 builds on before any run has seen a footprint in the readable band. Loses.
- **Opt-in only, no change.** Leaves a metre default that is provably below the readable band at
  X3's distances; the option would not be used. Loses.
- **v2 first.** Physically the right form for a no-ambient scene, but its cost and the register
  question are not worth paying before v1 shows the footprint reads at all.

## 7. Unknowns and what settles them

- The "twin gray lines when close" of run 20 are unexplained: expected contact bands on both
  sides of a strut, or the four-tap ring at the 64 px cap. An AO-on F8 in debug mode settles it;
  the `ao_<device>_<frame>.r16f` readback named in `ambient-occlusion.md` §6 does not exist in
  `motion_output.cpp` and would make it a numeric check.
- Horizon quality at 2–5 km on hull panels with sub-pixel detail is not covered by the fixture
  scenes; item (4) above adds it.
- The 0.2 m/unit calibration stays a working value (step-2 check inconclusive); it now affects
  only the clamps.
- In-game frame cost with AO on is unmeasured: `frame_end` windows in runs 19/20 were confounded
  by loads and captures; timestamp queries return `D3DERR_NOTAVAILABLE` on this backend. The
  at-rest A/B in run 21 is the first clean number.
