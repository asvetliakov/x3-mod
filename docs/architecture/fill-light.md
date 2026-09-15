# Fill light for faces that receive no light source

Design note, 2026-09-15. Decision for the orchestrator; implemented as documented below. Inputs:
[station-material-distance.md](../reverse-engineering/station-material-distance.md) "Run 22
(run51)", [camera-state-and-frame-routine.md](../reverse-engineering/camera-state-and-frame-routine.md)
"Ambient occlusion inputs", the converted-material notes, the exposure, shadow and AO notes, and
one new reduction of the run-51 captures (below; no Wine, no launch).

**Ratified 2026-09-15 (orchestrator):** implement as designed; amendment after review: the glass pairs take the fill on their albedo term too (their Fresnel/gloss law is unchanged), so all 108 converted programs carry it, default 0 (off, byte-identical shaders), `--material-fill K`; first user run at 0.06 with 0.04 and 0.10 as brackets at the run-51 spot. The point-light-range patch (option C) is rejected.

## Decision

Add a **constant hemispherical fill to the converted material law**, tinted by the sun colour the
pixel shader already decodes, as a shader-local constant with no per-draw work:

```
L = A · (P + M + D + k_fill · decode(LightDir_Color0) · g_direct) + R + E
```

`k_fill` is one scalar, launcher `--material-fill K` (`X3M_MATERIAL_FILL`, requires
`--linear-materials`, finite 0..0.5, **default 0 = off**: the transformed programs stay
byte-identical to the installed ones and the frozen 192-output hash holds). Recommended value for
the acceptance run **0.06**, bracket 0.04 and 0.10. It is one `mad` per converted pixel on all 168
DEFAULT/BUMPMAP/BUMPMAP_LOW pairs (hull families, asteroid, XT); glass keeps its own law; native
draws are untouched. Option (A) of the brief, with the sector dependence coming from the sun
register instead of a new colour source.

## 1. Evidence

Run 51 far frame 4172 / near frame 4965 (`/tmp/x3-bottleX3-run51`, 1280×768, route depth
linearized with `m22 = 1.00000298`, `m32 = −6.00001812`; module = x 430–800, y ≥ 280, view z
within the note's 1294–1861 / 665–1244 bands ±10 %; cylinder = same x, y < 280). Scene-linear
luma of the HDR target, before tonemap:

| surface | n | mean | p5 | p10 | p25 | p50 | p90 | frac < 0.05 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| far module (no point light) | 15,109 | 0.1014 | 0.025 | 0.032 | 0.049 | 0.089 | 0.182 | 0.254 |
| near module (headlight) | 23,122 | 0.1314 | 0.057 | 0.070 | 0.094 | 0.117 | 0.201 | 0.027 |
| far cylinder (sun-lit) | 20,495 | 0.1345 | 0.027 | 0.050 | 0.081 | 0.110 | 0.246 | 0.100 |

- The "black" faces are not zero: the far module's darkest quarter sits at 0.025–0.05 with
  chroma (0.226, 0.421, 0.346), the same as the background's (0.21, 0.45, 0.34; background mean
  luma 0.0232, RGB (0.0126, 0.0266, 0.0203) over 186,615 sentinel pixels). That remainder is the
  cube reflection `R` of the nebula through the specular mask, not a diffuse term. The headlight
  lifts the same band to 0.057–0.094 and turns it neutral (near dark chroma 0.323/0.340/0.337).
- The engine has no ambient term (`g_LightAmbientIntensity` never uploaded, `D3DLIGHT9.Ambient`
  stored as zero) and the converted law has none. Its authored non-sun light is D1 = (33,66,55)/256,
  decoded luma 0.041 = 7.8 % of the sun's decoded (0.406, 0.581, 0.309), luma 0.524. With the
  captured `g_MatDiffuseStrength = 0.5` (run 39 draw 216, the same `4944d81d` family) D1 delivers at
  most 0.5·0.041 = 0.021 per unit albedo to the one face that points at it; the rest get nothing.
- Hull albedo: the point-light delta on the far-dark points (0.0924 − 0.033 ≈ 0.06) against the
  light's attenuation at the near clamp distances (1/(1+0.01·d), d 753–1148 → 0.117–0.080) gives
  A·cos ≈ 0.5–0.75, i.e. decoded A ≈ 0.7 ± 0.15. Inferred, not measured (unknown 2).

## 2. The law, its constant and where it sits

**Magnitude.** Per unit albedo the fill is `k_fill · 0.524`: 0.021 at 0.04 (equal to D1's peak
Lambert contribution — the engine's own idea of "not the sun"), 0.031 at 0.06, 0.052 at 0.10.
Against a full-sun face (`0.5·A·D0`) the sun-averted side is `k_fill/0.5`: 8 % (3.6 stops) at 0.04,
**12 % (3.1 stops) at 0.06**, 20 % at 0.10. At A ≈ 0.7 and 0.06 the far module's p10/p25 move from
0.032/0.049 to ≈ 0.054/0.071 — three quarters of the headlight level the user saw as the good
state — while a sun-facing hull at 0.18 stays where it is. Night stays night: a 3-stop shadow side
is a shadow side. D1's own shading (±0.021) remains visible on top of a 0.031 floor, which is why
the default is not higher.

**Tint.** `LightDir_Color0` is declared by every converted PS (every transformer row carries a
`light0` register — c5/c5/c2 in the BUMP base, affine and non-affine programs — and the transformer
already decodes it into r12 and applies `g_direct`, `linear_material.cpp:1086`). Using the decoded register makes the fill follow the sector's sun
(per-sector node words `+0x150/+0x152/+0x154`), costs no upload and no host per-draw work, and
scales with `--material-direct-gain` as light should. In this sector it mixes the sun's chroma
(0.327, 0.385, 0.288) into the cube remainder's teal; a D1-tinted variant (`decode(Color1)`,
chroma 0.115/0.53/0.354, nearer the nebula) is the v2 switch if the run finds the shadow side too
warm — it is only available in the two-light programs (4 of the 6 PS per family are single-light
variants with no `Color1` register), so it needs a fallback and is not the first version.

**Insertion.** The transformer isolates the directional lobe multiplies
(`directional == (light1 ? 4 : 1)`, `linear_material.cpp:770–886`) and rewires their colour
operands to r12/r13; the fill goes in at the same point [directional-shadows.md](directional-shadows.md)
§2 reserves for the sun-share lane, after the lobe sum and before the albedo multiply: one
`mad sum, r12, c215.x, sum` (or a `mul` of r12 at the transfer site and a `mul → mad` on the first
lobe; 1–2 weighted slots either way). Constant: a new PS `def c215` — c212.y is the sanitizer's
zero literal, c213 is full, c214 belongs to the detached fade producer, c216–c220 to motion/depth;
c215 is free and is in no CTAB, so the engine never writes it. VS unchanged. When `k_fill = 0` the
insertion is skipped, not emitted with a zero constant.

## 3. Cost and interactions

- **Hot path.** PS weighted slots DEFAULT 178 → 179, BUMP 190 → 191 of 512 (measured with
  depth export on); zero per-draw host work, zero bandwidth, no new resource, no state. Variant creation is the existing create-time
  path (17 µs per program on the host). A fill-off launch is byte-identical to today.
- **Exposure.** The meter reads the composed HDR target, so the fill is seen: the sun-lit hull
  tiles that set the lit median rise by `k_fill·A·D0 ≈ 0.02` (0.13 → 0.15), `ev_key` +0.50 → +0.27,
  i.e. a re-key of at most 0.25 EV, at the dead band. That is correct behaviour — the meter should
  expose what is displayed — and excluding the fill would need a per-pixel lane the meter chain
  does not have; do not exclude it. If the user reads the sun side as darker after the fill, the
  existing EV offset is the knob, not the meter.
- **Bloom.** Threshold is in exposed-linear luminance (default 1); the fill adds ≤ 0.05 exposed.
  No interaction.
- **Sun-lit-share lane.** `f = lum(A·D0)/lum(L)` includes the fill in `L`, so a shadowed face keeps
  `A·(P + M + D1 + fill) + R + E`: a shadow and the night side land at the same 0.05 level, which is
  right for a single sun. The lane design is unchanged; this note supplies the term it assumed.
- **AO.** The fill is the only indirect term, so AO is physically meaningful again *on the fill
  alone*. Keep it off regardless: applying it to the fill only needs a fill-share lane (RT2.g is
  reserved for the sun share; a third lane means `A32B32G32R32F`, +8 B/px), and at 0.06 the fill
  is ≤ 20 % of a lit pixel, so even exact AO at s = 0.5 moves lit pixels ≤ 10 % — below what the
  run-19/20 footprint evidence could show. The scale decision stands.
- **Unconverted draws.** SM2/SM1 (526 identities), background, HUD and the material route's
  refusals get no fill and keep the native black. Run 51's station had 6 of 35 body draws gate-4
  refused (alpha-tested); that seam is the same one the linear conversion already has and is a
  question for the capture (unknown 4).
- **Native Windows.** `def` + `mad` in ps_3_0; documented D3D9 only; the material core already
  cross-compiles under the SSE2 / four-byte-stack contract. Runtime unverified on Windows, as for
  every material change; add the row to `platform-portability.md` when implemented.

## 4. Alternatives

- **(B) Hemisphere from the background.** The sector background mean is available in principle:
  the route knows the Background → Scene transition (gate 2 sets `PassMainScene`), a reduction of
  the back buffer there could reuse the meter's tile chain (2 quads, 167 µs steady median at 768p)
  with a one-frame-late readback and one `SetPixelShaderConstantF` of c215 per frame (0 per draw),
  but it needs a mid-scene state bracket (0.2–0.47 ms fenced per quad, as AO measured). Numerically
  it agrees with (A) here — a uniform sky of radiance 0.023 gives `A·c_sky ≈ 0.016` at A 0.7,
  0.7× the recommended constant — and that is the problem: in a dark sector it goes to zero and the
  black faces return, while the complaint is readability, not physics. "Up" has no meaning in a
  sector, so `k_sky·max(0, N·up)` has no reference, and a plain `c_sky` is a tint the engine already
  authors per sector as D1. Loses on cost and on the dark-sector case; the D1 tint is the v2 route.
- **(C) Widen the point-light admission range.** Needs RE of the site that compares node
  `+0x158` / bounding radius against the light range and writes `g_nNumLightPoint` (probably next to
  the two-light walker `0x004c4fc0`), then a same-length instruction patch like `--lod-scale`. Wrong
  fix: at the cull edge the light's own attenuation is 1/(1+0.01·1200) = 0.077, so widening the
  range only moves a 0.04-luma pop outward and extends a neutral white camera-anchored headlight;
  it lights nothing facing away from the ship, nothing beyond a few hundred metres, no bay
  interior, no night side. The fill also halves the pop's relative size (2.8× → ≈ 2×).
- **(D) Do nothing.** Native parity; the black faces are the reported defect. Loses, but default-off
  keeps parity until the user accepts the term.

## 5. Acceptance run

Switches: the installed set plus `--material-fill 0.06` (AO off; default Auto exposure capped at
+1.5 EV). Spot: the run-51 docking ring with its clamp arms, sun about 45° in front, F8 at
**≈ 350 m** (clamp nodes with `i0 = 0`) and **≈ 210 m**. Capture the far/near appearance first in
Auto, then toggle Ctrl+Shift+F9 at the same spot for a fixed-EV0 comparison with run 51, which used
manual EV0. Proof, on the same reductions as above:

1. Session log: `linear_material_mode … fill=0.06` (startup echo; a shader `def` has no per-frame
   state, so no `fill` field on `linear_material_frame` — its `routed`/`bump_routed` counts on the
   capture frames prove the pairs ran).
2. Scene-linear readbacks, before exposure: far-frame module `frac < 0.05` from 0.254 to ≤ 0.10
   and p10 ≥ 0.045; cylinder mean rises by ≤ 0.03 (0.1345 → ≤ 0.165); module chroma within 0.03
   of (0.226, 0.421, 0.346).
3. Same-surface reprojection (the run-51 method): the far-dark points' near/far median gain from
   2.8 to ≤ 2.0.
4. Default-Auto appearance and exposure lines are evaluated on their own. They are not an
   adaptation comparison against run 51's fixed-EV0 baseline. Use the F9 fixed-EV0 capture for
   the controlled appearance comparison with run 51; no bloom change.
5. In both exposure views, the arm tips at 350 m read as surfaces and the cylinder's night side
   stays dark.

Host proof before the run: `linear_material_reference.py` gains a `fill` term and the numerical
tests cover 0 / 0.06 / 0.5; the transformer test keeps the frozen hash at fill 0 and checks the
slot ceilings at fill > 0; the detached GPU fixture (one Wine command under `wine_lock.py`)
compares the law within one FP16 ulp.

## Implementation

Implemented 2026-09-15 in the transformer (`src/renderer/linear_material.cpp`,
`linear_xt_material_inc.h`), launcher option `--material-fill K` /
`X3M_MATERIAL_FILL`, finite 0..0.5, default 0.

- `LinearMaterialConfig::fill`. At `k = 0` no `DEF` and no instruction are
  emitted, and the whole converted corpus (1388 driver outputs across the hull,
  glass and XT structural drivers, both depth modes, gains 0/1/4/16) is byte
  identical to the fill-less build, verified by digest in
  `verification/analysis/test_linear_material_fill.py`.
- At `k > 0` one `def c215, k, 0, 0, 0` and one
  `mad rSum.xyz, r12, c215.x, rSum` per converted pixel program; `r12` is the
  decoded, `g_direct`-scaled `LightDir_Color0` the transformer already builds.
  The vertex programs are untouched, so is the alpha chain.
- **Lobe-sum destination** (unknown 1, settled by a host pass over the corpus).
  The site is the family's albedo multiply: the unique `MUL`/`MAD` writing
  `r1.xyz_pp` (hull, BUMPMAP, palette) or `oC0.xyz_pp` (asteroid, glass) whose
  two first operands are whole-register RGB reads of distinct temporaries and
  whose multiplier is the decoded albedo or a one-step composite of it
  (`linear_material_fill_sum`, shared by the transformer and the host probe).
  The fill `MAD` goes immediately before it. Zero or several candidates refuse
  (no fill, `fill_applied=0`). All 90 hull/asteroid/palette and 4 glass pixel
  programs resolve; the sum register is r1, r2, r3 or r4 depending on family.
- **XT** multiplies a branch-dependent albedo in the two arms of one `IF`, so
  its single `MAD` sits ahead of that branch, on the lobe sum both arms read
  (`xt_fill_site`): the branch is the one whose `ELSE` separates the two
  reviewed COLOR0 clamps, the sum is the non-clamp operand of each arm's
  vertex-colour `ADD`, it must agree between the arms, survive unwritten from
  the branch (first arm) or the `ELSE` (second arm) to that `ADD`, and feed an
  albedo multiply. All 14 XT pixel programs resolve (r1, r0 or r5).
- **Cost.** Zero per draw, zero uploads, no state, no resource: `c215` is a
  shader-local `DEF` in no CTAB. One weighted slot per program, measured:
  DEFAULT PS 178 → 179, BUMPMAP PS 190 → 191 with depth on, against the 512
  budget. (The note's 168/180 ceilings predate the current depth export; the
  measured delta is the +1 it predicted.)
- **Logging.** `linear_material_mode ... fill=<K>` once at startup;
  `linear_material_variant kind=ps ... fill_applied=0/1` per program, which is
  where a fail-closed refusal is visible. Vertex lines carry `fill_applied=0`.
- **Native Windows.** `def` + `mad` in ps_3_0 only; no new API use. Compiled
  for i686 MinGW with the project's SSE2/four-byte-stack flags; runtime
  unverified on Windows, as for every material change.

## 6. Unknowns

1. One lobe-sum destination per program: the profile rows locate the directional multiplies, not
   the sum register. The implementer's host pass over the 168 PS settles it (no Wine).
2. Hull albedo ≈ 0.7 is inferred from the headlight delta; the mean decoded texel of the
   `4944d81d` draw's s0 texture in a capture dump would fix the predicted p10/p25.
3. Whether the sun tint or a D1 tint reads better on the shadow side: the run decides; v2 only.
4. Whether the gate-4-refused alpha-tested draws of the same station show a seam next to filled
   faces: RT2 sentinel pixels in the far capture identify them.
5. Sectors with one directional light or an unusual sun colour: the fill follows `Color0`, so it
   is never absent, but its strength scales with the sun; a sector with a dim sun gets a dim fill.
