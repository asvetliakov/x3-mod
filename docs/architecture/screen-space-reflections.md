# Screen-space reflections: value assessment and a gated design

Status: design for ratification (2026-09-19). Nothing is implemented. (M) = measured in this
session, (D) = documented in the cited note, (I) = inference, (K) = author's own knowledge,
not verified against a source.

## 0. Recommendation

**NO-GO for general hull SSR. Do not build the GPU pass now.** The only scope that survives
the measurements is a *near-field, specular-masked, additive* SSR for opaque hulls, and its
measured reach is 0.3–1.5 % of the frame in the two station scenes available (§1). Spend one
cheap offline step first (stage 1, §6: a CPU composite on existing dumps that the user can
look at); build shaders only if the user judges that picture worth ≈0.6–0.9 ms and a fourth
render target. If stage 1 is not convincing, close the topic: the game's cube term already
delivers the dominant reflection content (sky), and the budget is better spent elsewhere.

Reasons, in order of weight:

1. Reflected content is mostly off-screen or sky (M): of the reflection rays of opaque
   pixels, 75–80 % leave the screen or end over sky, where the game's nebula cube is already
   the right answer. About half of the remaining hits land within 8 px of their origin
   (crevice self-reflection, indistinguishable from AO-scale detail).
2. The most reflective surfaces are not receivers: glass and the solar-panel surfaces write
   no depth (sentinel in RT2; 5.6 % of the frame in the panel scene, M) and are blended.
   Vanilla AP has no cockpit glass in front of the camera (K). SSR on them needs a separate
   mechanism (§2.4) that is out of scope here.
3. Original hull shading is kept. Its reflectivity is `k·m·albedo` with `m` the specular map
   red channel and the power-5/6/10 lobe (D, `remaining-hull-materials.md`): a broad, dim
   reflection. A mirror-sharp SSR term would look foreign on it; a correctly blurred and
   masked one is faint.
4. The failure modes of SSR (disocclusion at screen edges, flicker on thin lattices, ghosting
   under TAA) land exactly on what the TAA work of runs 142–153 has been removing.

Comparable games (K): X4: Foundations ships an SSR option and it is commonly turned off for
noise and cost; Star Citizen uses SSR mainly for interiors/hangars plus probes; Elite
Dangerous and EVE Online rely on environment cubes/probes for ship exteriors; Everspace 2
inherits UE4 SSR and it reads mostly on wet/planar station floors. The genre answer for
exteriors is an environment cube plus strong emissive bloom, which X3 plus this mod has.

## 1. Measured value (offline experiment, run153 dumps)

Tool: `tools/analysis/ssr_offline_probe.py` (numpy; depth-reconstructed normals, mirror ray,
perspective-correct pixel-space march, front-to-behind crossing test, 2 % relative
thickness). Reference march: stride 1 px, 1600 steps. Frames: 4950 (solar power plant close,
player hull in chase view) and 7750 (open sector, station at mid distance), 1280×768. No
close docking or capital-ship fly-by exists in any local dump (runs 130–154 checked by
contact sheet); that is the scene class most favourable to SSR and it is unmeasured.

| Quantity (M) | 4950 | 7750 |
| --- | ---: | ---: |
| Opaque (depth-writing) pixels, % of frame | 19.8 | 9.0 |
| Sentinel-depth pixels flagged in motion alpha (glass/lattice, no depth), % of frame | 5.6 | 0.0 |
| Rays that hit on-screen geometry, % of rays | 25.3 | 21.1 |
| Rays that leave the screen | 40.6 | 62.9 |
| Rays that end over sky / near plane (cube is correct) | 34.0 | 16.0 |
| Hits, % of frame | 4.5 | 1.6 |
| Hits within 8 px of origin, % of hits | 50.4 | 44.3 |
| Hits beyond 8 px, % of frame | 2.24 | 0.91 |
| ... whose term exceeds 10 % of receiver luma, Schlick F0 = 0.04, % of frame | 0.68 | 0.30 |
| ... same, flat reflectivity 0.25, % of frame | 1.51 | 0.59 |
| Near receivers (view z < 3000): hit rate / beyond-8-px hit rate, % | 12.3 / 8.5 | 16.3 / 11.1 |
| Median luma: hit vs receiver (FP16 code values) | 0.34 / 0.31 | 0.036 / 0.030 |
| Sky luma mean vs geometry luma mean | 0.057 / 0.365 | 0.040 / 0.184 |

Reading: the large near hull (player ship) classifies almost entirely as "sky" (blue in the
class image); hits concentrate in panel seams, the station core and lattice booms, where
they are speckle rather than recognisable reflections. Reflected objects are about as
bright as receivers, so with a 4–25 % reflectivity the term is a few percent of the pixel
except where the hit is an emitter (engine glow, lights, lasers): that is the only content
class that would be clearly visible, hence the "emissive-weighted" scope.

Budgeted marches against the reference (M): 32 steps × 8 px recalls 36 % of reference hits
with 12–20 % false hits; 48 steps × 4 px recalls 50–54 % with 10–17 % false hits and
66–82 % position agreement. A linear march without refinement is not good enough; §3 adds a
binary refinement and jitter.

## 2. Normals and reflectivity without a material rewrite

### 2.1 Depth-reconstructed normals (stage A source)

Measured on frame 4950: a best-of-two-neighbours reconstruction is available for 93.6 % of
opaque pixels (98.9 % in 7750); only 68 % / 87 % have continuous neighbours on all four
sides, i.e. a third of the panel-station pixels are edge or lattice pixels with a one-sided
estimate. The depth source matters: median angle between horizontally adjacent normals is
7.7° (z 3000–15000) and 10.3° (z > 15000) from device `z/w` (`RT2.r`), against 0.04° and
2.9° from the linear clip `w` the lane already writes to `RT2.b`
(`shadow-receiver-depth.md`). `z/w` differs from `.b` by 1.1e-4 median, 1.5e-3 p99 relative.
**Any SSR or future normal consumer must read `RT2.b`**, which exists only while the sun
lane is active (`lane_depth_format()`); off the lane SSR is refused.

Depth normals are geometric: faceted, no normal-map detail, wrong at silhouettes and on
sub-pixel lattices. Acceptable for a broad, blurred, near-field term; not for sharp
reflections.

### 2.2 Fourth MRT output from the transformed shaders (stage B source)

Feasible with the existing machinery, per family, not generically:

- The hull PS keep a live normalized per-pixel normal (BUMPMAP families: `r0.xyz`,
  reconstructed from the AG normal map; DEFAULT: normalized `v3` with VFACE sign) and the
  view vector up to the cube fetch; the reflectivity is `k·m·albedo` with `m =
  specular_texture.r` and a family constant `k` (0.5 or 1) (D,
  `remaining-hull-materials.md`, `glass-materials.md`). The linear-material profiles already
  carry per-family sites and coefficients (`linear-material-profiles.json`: 17 families,
  `coefficients.cube`), and the cube-scale site is identified per family.
- What is missing in the profiles: the normal register and the DWORD offset at which it is
  final, and the register holding `m` at that point. This is a bounded disassembly task
  (one row per family, same census method), not a new rewriter.
- Output: `oC3 = (N_view.xy·0.5+0.5, k·m, valid=1)` into an `A8R8G8B8` RT3 (view-space
  normal needs the view rotation rows in three reserved PS constants, or write world-space
  octahedral and rotate in the post pass; the latter costs no per-draw constants and is
  preferred). Appended cost ≈ 6–9 slots; the 71-slot hull maximum leaves room, the
  XT/damage programs must be checked against 512 individually.
- MRT limits: D3D9 allows 4 simultaneous targets on all SM3 hardware of interest
  (`NumSimultaneousRTs`, checked at attach like the existing three-format self test); RT0
  FP16, RT1 RGBA32F, RT2 RGBA32F already require `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`, so
  an 8-bit RT3 adds no new capability. Blended draws must mask RT3
  (`COLORWRITEENABLE3 = 0`), as RT2 is masked today.
- Cost: +1 `SetRenderTarget` and +1 write-mask per routed draw in `perdraw` mode ≈ the
  measured 0.64 µs per bind (D, `route-per-draw-cost.md`) → ≈ 0.3–0.5 ms at 480–830 routed
  draws on wined3d (I); near zero in `lazy` mode. Bandwidth 4 B/px, below the 8 B/px the
  `.b` channel added for ≈ 0.2 ms (D). Clearing RT3 needs one more target in the sentinel
  fill draw.
- Alternative packing without RT3: `RT2.a` duplicates `.b` today (M: equal on 100 % of
  pixels) and is free. A float32 holds 24 exact bits (10+10 octahedral normal, 4 mask), but
  encode/decode is ≈ 10 slots each of `floor/frac` arithmetic whose exactness under
  wined3d→Metal is unproven. Keep as the fallback if RT3 proves expensive.

Recommendation inside the gated plan: stage A on depth normals with a constant
reflectivity is enough to judge the look; add RT3 only if the feature is kept, because the
specular mask is what stops painted/matte hull areas from mirroring and the normal map is
what makes the term match the game's own cube reflection.

### 2.3 Rejected: specular mask from colour heuristics

Deriving gloss from scene luminance or saturation: no physical basis in code-value
original shading, and it would brighten emissive windows. Not considered further.

### 2.4 Glass and panels

They write no depth and are blended, so they are neither receivers nor reliable
occluders. A per-draw SSR inside the glass PS (sample a copy of the scene colour along the
interpolated cube direction) is the only route; it needs a mid-frame colour/depth copy and
is a separate decision. Not part of this design.

## 3. Trace (if built)

- **Resolution and budget.** Half-resolution trace into an FP16 target (rgb = hit radiance
  × confidence, a = confidence), like the AO chain. Linear march in pixel space with
  perspective-correct `1/z`, **24 steps in a ps_3_0 `[loop]`** (the GTAO program already
  sits at 512 slots with `[loop]` + `[unroll]`, so the march cannot be unrolled), stride
  scaled so the reach is ≈ 200 full-res px, then 4 binary refinement steps (unrolled).
  `tex2Dlod` only. Estimated 120–160 slots. No Hi-Z: building a min-depth pyramid costs 5–6
  quads at ≈ 0.2 ms CPU floor each on this backend (D, AO ledger) for a scene where the
  median hit is < 25 px away (M).
- **Depth.** `RT2.b` (view depth, float32). All tests relative: hit when the ray crosses
  from in front (`rz < sz(1−1e-3)`) to behind within `thickness = 2 % · sz` plus the step's
  own z span. Relative tests make the 6 … 2e6 range harmless; stations at 1e5 units are
  out of scope anyway (receiver gate below). Sentinel (`< 0`) is "no surface": the march
  continues; a ray that ends over sentinel is a miss.
- **Receiver gate.** Trace only pixels with valid depth, a reconstructable normal, view
  z < ≈ 3000 units fading to 5000, and (stage B) mask > 0. This removes the distant speckle
  of §1 and most thin-lattice pixels, and cuts the traced area to ≈ 3–8 % of the frame (M:
  near receivers are 30–38 % of 9–20 % opaque coverage).
- **Jitter and accumulation.** Offset the march start by an interleaved-gradient value
  rotated by the TAA jitter index; no private history. The term is composited before the
  TAA resolve, which accumulates it. A private reprojected history is rejected: it needs
  the reflected surface's motion, which RT1 does not describe.
- **Confidence.** Product of: screen-edge fade (hit uv within 10 % of a border), ray
  facing fade (`R.z < 0` toward-camera rays fade out; 30–50 % of rays, M), step-count
  fade near the end of the budget, receiver distance fade, and back-face rejection at the
  hit when RT3 exists.
- **Roughness.** One separable depth-aware blur of the half-res result (reuse the AO blur
  program shape, 93 slots), fixed radius; a per-pixel roughness cone is not justified for
  the lobe exponents 5–10 of the original shading.
- **Fallback and double reflection.** The game's cube term is already in the colour and is
  *not* removed on a miss: it is also the only ambient floor on sun-averted faces (D,
  `original-shading-critique.md`). On a hit the exact result would replace `ρ·C_cube` by
  `ρ·L_hit`; `C_cube` is not available at the scene end. Sky luma is 0.04–0.06 against
  0.18–0.37 for geometry (M), so the additive composite `colour += conf · ρ · L_hit`
  over-counts by at most ≈ 15–30 % of the SSR term, on ≤ 1.5 % of the frame. Accept the
  error; do not store the cube colour. Stage B may subtract a constant sector-mean sky
  estimate (the exposure meter's unlit-tile mean) if the error is visible.
- **Emissive weighting.** Weight the hit radiance by `smoothstep` on its luma above the
  frame's adapted key so that lights, engine glows and lasers reflect at full strength and
  ordinary lit hull at a reduced one. This keeps the visible part of §1 and suppresses the
  speckle part.

## 4. Placement, HDR, TAA, bloom

Scene-end hook, step 1 of `hdr-scene-path.md` §4, after AO and the sun-shadow apply,
**before the TAA resolve**. All scene draws including late additive glows are in the FP16
target by then, which is what makes emitters reflectable; the 7–21 blended Z-write-off
draws that overlie hulls (D, `ambient-occlusion.md`) receive the term underneath them as
they do AO. A mid-scene bracket before the transparent draws (AO keeps one as a v2 option) is rejected
for SSR: the glows would be missing from the reflected source, and they are the content
that matters.

HDR: the FP16 target holds gamma-2.2 code values under original shading (M: `hdr_frame
decode=gamma2.2`). Composite in linear: `out = enc(dec(c) + conf·ρ·dec(L))`, two `pow`
pairs in the apply quad; clamp `L` to the bloom extract ceiling before the blur to stop a
single laser texel from flooding the blurred term. Bloom then sees the reflections (wanted:
reflected lights glow slightly). TAA: the term is jittered noise at half resolution; the
adaptive-weight and thin-clip paths see it as colour change on static pixels. Risk: more
clip rejections on lattice pixels; the receiver gate excludes most of them, and stage 2
checks the run153 band metrics with the term injected.

## 5. Cost

| Item | Estimate | Basis |
| --- | --- | --- |
| CPU submit, 3 quads (trace, blur, apply) | 0.3–0.5 ms | AO chain 0.51–0.78 ms for 4 quads, ≈ 0.2 ms fence floor per quad in isolation (D); TAA run 0.33 ms (M, run153) |
| GPU, half-res 24+4 taps on ≤ 8 % of pixels (early-out by gate) | < 0.3 ms | GTAO 35 reads per half pixel whole-frame ≈ 0.3 ms (D) |
| RT3 per-draw (stage B only) | 0.3–0.5 ms perdraw, ≈ 0 lazy | 0.64 µs per bind (D) |
| Memory at 1280×768 | 2.0 MB trace + 2.0 MB blur scratch (FP16 half-res); RT3 3.9 MB | computed |
| Slots | trace 120–160, blur ≈ 93, apply ≈ 40 | AO programs (D) |

Total ≈ 0.6–0.9 ms of a 20 ms frame, 3–4.5 %, for ≤ 1.5 % of the frame's pixels. Hot-path
cost when disabled: zero (no RT3 bound, no quads). Native Windows: only documented D3D9
(`CreateTexture` FP16/A8R8G8B8 render targets, `SetRenderTarget(3)`, ps_3_0 `loop`,
`tex2Dlod`); compiled-source compatible, unverified natively; the 4-MRT and
independent-bit-depth caps are checked at attach with a refusal path.

## 6. Staged plan

| Stage | Work | Gate |
| --- | --- | --- |
| 0 (done) | `ssr_offline_probe.py`, §1 numbers | – |
| 1 | Extend the probe to write a composite PNG pair (off/on) through the AgX replay of `taa_resolve_replay.py`: gate, emissive weight, blur, linear add; frames 4950/7750 plus one user capture of a docking approach or capital fly-by (`--taa-debug` F8 burst with the sun lane on) | User looks at the pairs. No visible gain → close the topic. |
| 2 | HLSL trace/blur/apply + host reference test against the numpy march on a synthetic depth oracle (as `test_ambient_occlusion_reference.py`); detached Wine fixture with EVENT-fenced timing at 768p/1080p; TAA band metrics with the term injected in the replay | ≤ 0.8 ms chain, hit recall ≥ 70 % of the reference inside the gate, no band-rms rise > 5 % on the run153 lattice crop |
| 3 | Live pass behind `--ssr` (default off), depth normals, constant ρ | Paired on/off flight, +≤ 0.6 ms median |
| 4 | Disassembly census of normal/mask registers per family; RT3 output; fixture proving RT0–RT2 bit-identical with RT3 bound | Only if stage 3 is kept |

Every Wine fixture runs as `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py ...`.

## 7. Alternatives that lose

- **Full-frame hull SSR with Hi-Z and a roughness cone.** 2–3× the cost for hits that are
  half self-reflection and mostly beyond the distance where depth normals are clean.
- **Planar or probe reflections (re-render).** Draw count doubles for the reflected set at
  ≈ 9.7 µs per routed draw (D); out of budget, and there are few planar surfaces.

## 8. Unknowns

- Scene coverage: no dump of a docking bay or a capital hull at close range. One F8 burst
  there settles whether the 0.3–1.5 % figure is representative (stage 1).
- Whether the sentinel pixels with motion alpha 1 in frame 4950 are the panel surfaces
  themselves (I, from the class image) and which PS draws them: a `draw` / `motion_route`
  query on that frame's log by PS hash settles it.
- Normal/mask register and final-offset per family (stage 4 disassembly).
- `[loop]` with 24 iterations and `break` under wined3d→Metal: the GTAO `[loop]` has 2
  iterations only; the stage 2 fixture settles it.
- RT3 bind cost under wined3d is inferred from the RT1/RT2 bench, not measured.

## Ratification (orchestrator, 2026-09-19)

NO-GO on general hull SSR is accepted: on real frames 0.3–1.5 % of the frame would
visibly change, and the glass/panel surfaces write no depth. No shader work is scheduled.
The near-field emissive-weighted variant stays a proposal for the user: it needs one F8
burst of a docking bay or a capital hull at close range (sun lane on) for the stage 1
offline composite; without that capture nothing further is done.
