# Hull emissive widening: footprint-scaled light-map sampling in the hull pixel programs

Status: ratified 2026-09-22 and built the same day behind `--hull-emissive-widening K,Q0,Q1` (default off; see
"As built" at the end and the ledger [hull-emissive-widening.md](../verification/hull-emissive-widening.md)). Tags:
**[M]** measured this session (archive census, source reading), **[C]** measured earlier and cited, **[I]** inferred,
**[A]** assumed. Owner of the problem: [thin-glow-lines.md](thin-glow-lines.md) (run225 §2, run227 §7). The transform
host and its proofs: [hull-self-illumination.md](../reverse-engineering/hull-self-illumination.md) §5,
`original_fill_transform` in `src/renderer/linear_material.cpp` (`lightmap_term`, `lightmap_gain_instruction`),
`verification/analysis/test_hull_lightmap_gain.py`. The far fade: [taa-distant-line-fade.md](taa-distant-line-fade.md)
§11-13. User decision being designed: widen the light-map emissive term in the hull shader, scaled by distance, for
every family that carries the term.

## 0. Decision in one paragraph

Replace the pinned light-map fetch `texld rL, v1, s{2|3}` of every gained hull variant by a `texldd` whose screen-space
UV gradients are the pixel's own `dsx`/`dsy` of `v1` multiplied by a per-draw scale `k` (c217.z). The sampler then
filters the light map over a `k x k` pixel footprint instead of `1 x 1`: the LOD rises by `log2 k`, the anisotropic tap
line stretches by `k`, and the mip chain does the energy accounting itself (a strip narrower than `k` px comes back
`w/k` as bright and `k` px wide; a panel wider than `k` px comes back unchanged). **No intensity rescale is added**: the
averaged fetch already conserves energy, and a `1/footprint` factor would attenuate twice (§1.3). The distance scaling
is in two places: per pixel, the gradients grow with distance, so the same `k` means "k pixels" at every range; per
draw, `k` ramps from 1 (bit-identical bytes bound, §2.4) inside a near band to its full value beyond it, using the
far fade's existing per-draw footprint. Cost per hull pixel: +3 ALU instructions (+7 weighted slots, one temp, one
constant lane already uploaded), one fetch as before. Covers all 100 SM3 programs that have the term, which is every
race and the XT families (§2.5); nothing else in the archive's SM3 set has one except the moon.

## 1. The law

### 1.1 What the hardware already measures

The hull programs are all `ps_3_0` **[M]**: the 100 light-map programs carry version word `0xffff0300` and the
transformer refuses anything else (`original[0]!=0xffff0300u`). `dsx`, `dsy`, `texldd`, `texldl` and `texldb` are
legal; no promotion question exists. The `2_0`/`2_a`/`2_b` directories hold ps_2_x fallbacks the game selects only on a
lower-caps device; the route never registers them, so they are outside this design.

For the light-map fetch the sampler computes, per 2x2 quad, the texel densities `rho_x = |d(uv*size)/dx|`,
`rho_y = |d(uv*size)/dy|` (texels per pixel), takes `N = min(ceil(rho_max/rho_min), maxaniso)` taps along the major
axis and selects level `lambda = log2(rho_max / N) + bias`, clamped to `[0, levels-1]`. That is the per-pixel "light-map
texels per screen pixel" the brief asks for, at zero ALU cost and with anisotropy handled: a CPU constant from view
distance x texture size x UV density cannot match it (UV density differs per part of a model and is not known to the
route without reading meshes; error of 2-4x across one station **[A]**, and no anisotropy at all).

### 1.2 Why a LOD bias alone is not enough (run227) and gradient scaling is

A bias of `b` moves only `lambda`: the reconstruction texel grows to `2^b` px along the **minor** axis of the
footprint, while the `N` anisotropic taps still span the same 1-px major extent. A strip whose width lies along the
major axis (a vertical window column on an oblique drum) is not widened by any bias. run227 **[C]** flew a global
`+0.5` (one stop above the installed `-0.5`): horizontal 1-px-run share fell 15-22 %, vertical did not move. That is
the anisotropy signature, and it is why the stage-only sampler bias (thin-glow option b') is the fallback here, not
the design.

`texldd rL, v1, s, k*dsx(v1), k*dsy(v1)` scales both gradients: `rho_x`, `rho_y` and therefore `lambda` (+`log2 k`)
and the tap spacing all scale by `k`, `N` is unchanged. The filtered footprint is the pixel's own ellipse enlarged
`k` times in both screen axes.

### 1.3 What the widened fetch returns (energy) and why no rescale is added

Take a strip of authored width `w_t` texels and radiance `I` on a dark background, screen width `w = w_t / rho` px
without widening. Every level of a normalised mip chain preserves the strip's integral; trilinear blending and bilinear
reconstruction are partitions of unity. So for any LOD the emissive energy per unit strip length is `I * w` px,
unchanged. At the widened level the reconstruction texel is `T = k` px, and the strip comes back with

- peak `I * min(1, w / k)`,
- width about `max(w, k) + k` px (box of the level texel convolved with the bilinear tent).

The `1/footprint` factor is *inside* `min(1, w/k)`: the averaged texel already holds the strip's radiance diluted by
its coverage. Multiplying the term by another `1/k` would take the same energy out twice and dim every wide panel by
`k`. **Therefore the emissive multiply is untouched** and the widening is exactly the fetch replacement.

The wide-panel case falls out of the same identity: a panel wider than `k` px has interior texels equal at every
level up to `log2 k`, so its interior value is unchanged and only its edges soften over `k` px. No gate on local
strip width is needed; the mip chain is that gate. (Non-normalised mips would break this; §6 says how to check the
game's DDS chains.)

### 1.4 Per-phase stability, and the value of k

The tearing (thin-glow §2) is the phase dependence of sampling a reconstruction tent of half-width `T` px at 1-px
pitch: for a line narrower than the pitch the sampled peak ranges over `[1 - 0.5/T, 1]` of its maximum. With the
installed `-0.5` bias `T = 0.71` px (ratio floor 0.29; run225 measured mean/max p10 0.52, p50 0.76 **[C]**); at bias
`0`, `T = 1` (floor 0.5); run227's `+0.5` gives `T = 1.41` (floor 0.65: "modest"); `k = 2` gives 0.75, `k = 3`
0.83, `k = 4` 0.875. After the resolve, mechanism (ii) of the thin-glow note (a 1-px line resampled by Catmull-Rom into a
2-3 px band at half peak) also fades once the band is 3 px wide before the resolve. So the emulated option (a) of the
thin-glow note (3x3 tent, `k` about 1.5) predicted a modest gain and run227 confirmed it; **`k` must be 2-4, and the
fixture sweep (§3) chooses it**. First flight `k = 3`: a strip of screen width 0.76 px returns at 0.25 of its peak
(gain 4 x 0.25 = the game's own authored peak) over 3-4 px; a strip wider than 3 px is unchanged.

### 1.5 Composition with the gain and the far fade; near, grazing, animated

Per pixel the term is `sample_k(uv) * G_eff(draw)`, with `G_eff` the far fade's per-draw gain (`gain` to `P0`,
linear to `G` at `P1`; c217.w). Every factor is continuous, so the composition is. With widening on, the far fade's
original reason (a x4 point sample the jitter switches on and off) is gone for footprints the widening covers; what
remains of the fade is a dimming of far windows below their energy-conserved value (`4 -> 1` between 80 and 220
units/px). Keep `80,220,1` unchanged for the first flight (one variable), then A/B `--no-light-map-far-fade`.

**Near.** For `k * rho_max / N <= 1` the hardware clamps to level 0 in both the original and the widened fetch; the
band `1/k < rho' <= 1` is where the widened fetch first blends toward level 1 while the original is still at level 0,
i.e. the light map softens up to `log2 k` levels at distances where its texel is 1-1.5 px. That is also where 1-texel
art first tears, so the per-pixel law must be active there; but it softens all light-map content in that band (panel
stripes, decals), not only strips. The per-draw ramp `k_draw = 1 + (k - 1) * sat((f - Q0) / (Q1 - Q0))`, with `f` the
far fade's per-draw footprint in units/px (already computed in `evaluate_draw`), keeps near draws bit-identical: at
`k_draw = 1` the route binds the **un-widened** gained variant (the existing object), so near bytes and near images
are the game's plus gain, exactly, and no `texldd(k=1) == texld` assumption is needed. Between `Q0` and `Q1` the
fixture's near cases bound the visible change. Both `Q` are per draw, not per pixel, with the seam caveat of the far
fade (§11 of its note); the run225 strips sit at 13 units/px, so `Q1 <= 10` puts them at full `k`. First flight
`Q0,Q1 = 2,8` (at 1280 px / p00 0.8: about 200 m to 820 m of view depth, [I] from the far fade's conversion).

**Grazing.** Handled by §1.2: the ellipse scales uniformly. Where `rho_max/rho_min` exceeds `maxaniso` (16) the
hardware already over-filters the minor axis; widening adds `k` on top. Whether CrossOver's backend honours
anisotropy for explicit-gradient fetches is a fixture question (§3, tilted quad), not a law question.

**Animated / blinking light maps.** The law is stateless and per fetch: a texture swap or UV scroll changes the
sample, not the footprint. A scrolled strip is widened identically every frame; a blink is widened and dimmed by the
same `min(1, w/k)` on and off. Nothing temporal is added.

## 2. The transform

### 2.1 Site and instruction patterns [M]

Every one of the 100 programs fetches the light-map stage **exactly once**, with coordinate register `v1` (source
token `0x90e40001`), at stage s2 (44 programs: DEFAULT layouts and 4 XT) or s3 (56: BUMPMAP layouts and 10 XT); the
fetch is the last `texld` and sits after the last flow-control token in all 14 XT programs (`rep`/`if` blocks end
26-30 DWORDs before it), so `dsx`/`dsy` are outside dynamic flow control. The tails are those of hull-self-illumination
§1: `texld rL, v1, s{2|3}` · (`mad rL.xyz, r2, r3.w, rL` on the six XT terra rows) · `lrp .w` · `add`/`mad oC0.xyz`.
The gained variants add `mul rL.xyz, rL, c223.x | c217.w` directly after the fetch.

### 2.2 Bytecode edit

At `lightmap_site` (the profile-pinned fetch, `Pixel::texture[bump?3:2]` / `XtPixel::texture[stage]`), the 4-DWORD
`texld` is replaced by 16 DWORDs; everything else of the gained variant is byte for byte as today:

```
dsx    rG.xy,  v1               ; opcode 91, 2 slots
dsy    rG.zw,  v1.xyxy          ; opcode 92, 2 slots   (both gradients in one temp)
mul    rG,     rG, c217.z       ; 1 slot, per-draw k (c217.z is uploaded 0 today and read by no program)
texldd rL{_pp}, v1, s{2|3}, rG.xy, rG.zw   ; opcode 93, 3 slots, destination mask/pp as the original texld
mul    rL.xyz, rL, c217.w | c223.x         ; the existing gain MUL, unchanged
```

- `rG`: one temporary above every register the combined program uses (originals reach r4-r6 **[M]**; the motion,
  fill and share bodies allocate above `temporary_base`; the final `structure()` walk gives the maximum; refuse at
  32). Its `.zw` write by `dsy` with source swizzle `.xyxy` is legal ps_3_0.
- Constant: `c217.z`. The route uploads `c216-c217` on every routed draw (`pixel[8]`, `motion_output.cpp`) and the
  motion fragment reads `c217.x` only, the far fade `c217.w`; `.y/.z` are zero and unread **[M]**. The proof asserts
  exactly one read of lane `z` (the same lane-count check the far fade uses for `.w`). No static `def` form is needed
  because the per-draw ramp already requires the upload; with the option's ramp off the route uploads the constant
  `k`. A shader-local DEF would shadow the upload, as the far fade note records for c223.
- Slots: +2 +2 +1 +3 -1 = **+7 weighted slots, +3 instructions, +12 DWORDs** per variant (texldd 3, dsx/dsy 2 each
  per the D3D9 ps_3_0 instruction table **[A: confirm by CreatePixelShader acceptance in the fixture]**). Largest
  gained variant today 264-311 of 512 **[C]**; worst case 318. Temps: one more.
- `structure()`/`body_shape()` must learn opcodes 91, 92 (2 operands, cost 2) and 93 (5 operands, sampler check as
  texld, cost 3); `lightmap_term()` must accept a `texldd` whose src0 and sampler equal the original fetch's and
  whose gradient sources are the one temp written by the two preceding derivative instructions; the emitted-program
  re-proof runs as today (fetch, gain MUL, rL.xyz untouched to the final, c217 lane reads).
- `rL.w` is now the widened alpha too. Opaque hull draws (blend off, alpha test off) discard `oC0.w`; for a draw with
  blend or alpha test on the route binds the un-widened gained variant (the state is in the shadow; `motion_route`
  logs `blend`/`atest`). This keeps the two-fetch form (original `texld` for `.w`, `texldd` for `.xyz`) unnecessary.

### 2.3 Per-draw upload and variant choice

`evaluate_draw` already computes the far fade's footprint `f` for pairs with a gain variant and writes `pixel[7]`.
Add `pixel[6] = k_draw(f)` (one multiply-add and a clamp), and choose the widened variant only when
`k_draw > 1 && blend off && atest off && levels(stage) > 1` (the mip-bias shadow keeps per-stage level counts, so the
32x32 single-level placeholder of the ONE/ONE emitter props never binds it; the LOD clamp would make it identical
anyway). No new `SetPixelShaderConstantF`, no `Get*`, no allocation per draw. Reset re-creates the variants through the
existing registration path; Ctrl+Shift+F4 keeps toggling the gain, and the widened variant follows the gain variant's
selection.

### 2.4 Proof method

1. **Byte-exact Python reference** in `test_hull_lightmap_gain.py`'s oracle: for each of the 100 programs x
   (2 fills x 2 depth modes + 2 share), the widened variant equals the gained (dynamic) variant with its 4-DWORD `texld`
   replaced by the 16 DWORDs above, `rG` = the oracle's own maximum temp + 1, and nothing else changed; c217.z read
   once, c223 defined zero times; the 8 non-term programs (glass, asteroid) untouched byte for byte; slot total per
   variant = gained + 7 and <= 512.
2. **Disassembly diff**: `D3DXDisassembleShader` (`tools/analysis/disassemble_shaders.cpp`) of gained vs widened for one
   program per family (17 rows of §2.5 + XT default/bump/terra): the diff is exactly the four replaced/added lines.
3. **Fixture parity** (§3): near case hash-identical to the gained variant; energy conserved within 1 % on the
   uncompressed strip; wide-panel interior within 1 FP16 code.

### 2.5 Family enumeration and the coverage test [M]

Archive census (local sweep cache, 751 programs, manifest effect index; script in the session scratchpad): 495 pixel
programs, 392 declare `LightMapTexSampler`; by model 1_1 16, 1_4 18, 2_0 80, 2_1 177, **3_0 101**. Of the 101 SM3
programs, **100 are the light-map programs of the 108 selected pixel originals** (90 race/standard + 14 XT, minus the
8 without the term) and 1 is `shader/3_0/moon_0000.fb` / `moon_0001.fb` (270 words, stage s2, coordinate `v2`, no
motion row: out of scope, listed so it is not forgotten; a moon's night lights would be the first candidate if they
shimmer). All 28 `3_0` effect names are accounted for: the remaining ones (adeffects, asteroid, bloom, effects, engine,
glass, gui2d, nebula, nebulafog, particles, planet_haze, planet_v, stardust, z_only) have no SM3 light-map program.
There is no pirate, Yaki, ATF or Goner effect file: those factions' hulls are drawn by the race files (ATF by
`terran`), so the table below is the complete family set.

| family (profile table) | effect files (`shader/3_0/`) | programs | light-map stage | gained today | widened |
|---|---|---:|---|---|---|
| argon / argon_bump | argon, argon2s, argon_0000/0001 | 6 + 6 | s2 / s3 | 12/12 | 12 |
| boron_default / boron_bump | boron, boron2s, boron_0000/0001 | 4 + 4 | s2 / s3 | 8/8 | 8 |
| paranid_default / paranid_bump | paranid, paranid2s, paranid_0000/0001 | 6 + 6 | s2 / s3 | 12/12 | 12 |
| split_default / split_bump | split, split2s, split_0000/0001 | 6 + 6 | s2 / s3 | 12/12 | 12 |
| shared_default / shared_bump | khaak, teladi, teladi_nodiff, xenon (+2s, _0000/0001) | 6 + 6 | s2 / s3 | 12/12 | 12 |
| terran_default / terran_bump | terran, terran2s, terran_0000/0001 | 6 + 6 | s2 / s3 | 12/12 | 12 |
| standard_default / standard_bump / standard_bump_low | standard_lighting (+2s, _0000/0001) | 6 + 6 + 6 | s2 / s3 / s3 | 18/18 | 18 |
| XT default / bump (incl. damage, terraformer) | xt_standard_lighting, xt_standard_lighting_damage, xt_terraformer (+2s) | 4 + 10 | s2 / s3 | 14/14 | 14 |
| glass (4), asteroid (4) | glass, asteroid | 8 | none | 0 (no term) | 0 |
| moon | moon_0000/0001 | 1 | s2 | not registered | 0 (out of scope) |

"gained today" is `test_hull_lightmap_gain`'s `(supported, applied, untouched) = (108, 100, 8)` **[C]**. The coverage
test to add: walk the local corpus, decode each ps CTAB, and assert that every `ps_3_0` program declaring
`LightMapTexSampler` either receives the widened variant or is in the explicit exclusion list `{moon}`; emit the
hash -> family -> stage -> transformed table as a compact tracked JSON (`verification/results/hull-emissive-widening-
coverage.json`, 101 rows) so a future archive or profile change fails loudly instead of silently dropping a race.

## 3. Fixture plan

Host: a new `run_motion_output.py` mode (`lightmapwiden`), on the far-fade seam skeleton (hull pair
`494fe349b8bc12ec / 7c83ed50c9894e44`, gain 4, fixture camera), Wine under the lock:
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py
seam-lightmap-widen-*`. Extracted game programs stay local and untracked (`X3M_SHADER_PROGRAM_DIRECTORY`); the record
tracks hashes and numbers only.

1. **Synthetic strip light map**: 1024x1024, strips of 1, 2 and 4 texels (horizontal and vertical), one 64-texel
   panel, black elsewhere; full box-filtered mip chain; two copies, `A8R8G8B8` and `DXT1`. Routed quad frontal and at
   60 deg / 75 deg tilt; camera distances for `rho` = 0.5, 1, 1.4, 2, 4, 8, 40 texels/px; `k` in {1 (un-widened variant
   bound), 1.5, 2, 3, 4}; the quad drifts 6 px/frame across 8 jitter phases (the TAA's sequence).
   Metrics per strip and phase (the run225 metrics): per-phase peak mean/max, frame-to-frame peak change p50/p90
   along the drift, integrated strip energy, panel interior value; then the frames through the host `resolve.hlsl`
   replay for post-resolve peak change. Expected: energy equal across `k` within 1 % (uncompressed) and the DXT1
   deviation reported; peak ratio ordering `1 - 0.5/k`; panel interior identical; `rho <= 1/k` frames hash-identical
   to `k = 1`; tilted 75 deg vertical strip widened (the run227 axis) — if it is not, the backend ignores gradients for
   anisotropy and §6 applies.
2. **Real-program case per family**: the same quad bound with one actual pixel program per row of §2.5 (18 programs
   including XT terra): `CreatePixelShader` accepts every widened variant (slot check), near hash = gained variant,
   strip energy conserved, `hull_lightmap_variant` log rows show `widen_applied=1`.
3. **Host**: transformer oracle (§2.4.1), coverage test (§2.5), launcher test for the option, `test_motion_wrap_states`
   and `test_comparison_hotkeys` for the F4 path; no full suite.

Append results to `docs/verification/motion-output.md` under a "Hull emissive widening" heading; compact JSON under
`verification/results/bottle-X3/`.

## 4. Cost

- Per hull pixel of a widened draw: +1 `dsx`, +1 `dsy`, +1 `mul` (quad-rate derivative ops; +7 slots on programs of
  34-71 original slots, so at most +10-20 % of the original ALU count, a few % of the combined variants), and the
  same single light-map fetch: `N` taps as before at a coarser level (same or better cache behaviour). One extra
  temporary. Zero extra fetches.
- Per draw: one multiply-add and clamp on the footprint the route already computes; no extra API call.
- In flight: stations cover 2-5 % of the frame at 1-2 km **[C]** and can fill it at dock; measure with the existing
  Ctrl+Shift+F4 gain toggle (which now also drops the widened variant) against the frame-time telemetry
  (`docs/architecture/engine-frame-time.md`) at the run225 station and again docked, 30 s each state; the capture log
  carries CPU submit times only, so the A/B is on frame time, not per-pass GPU time.

## 5. Risks

- **Over-softening of large panels**: none in the interior (§1.3); edges soften over `k` px; energy conservation must
  not add any scale (the double-attenuation trap). The fixture's panel case asserts it.
- **Near light-map softening** in the `1/k..1` texel/px band: the per-draw ramp keeps it off near; between `Q0` and
  `Q1` the change is per draw (seams between separately drawn parts of one station, as with the far fade). If a per-
  pixel gate is ever wanted: `k_pixel` from the gradient magnitude (`dp2`, `rsq`, `mad`, `max`; about +6 slots), not
  built now.
- **DXT light maps at higher mips**: a widened strip of 1/8-1/16 intensity sits near the 5:6:5 endpoint quantum
  (8/255): channel error up to 25 %, so very dim far windows can shift hue, and below about 4/255 a strip can vanish
  from a level. The far fade already lowers those windows; the DXT1 fixture copy measures the shift. Not controllable
  by the proxy (the chains are authored in the DDS).
- **Backend behaviour of `texldd`**: anisotropy with explicit gradients and the `dsx`/`dsy` quad convention are
  documented D3D9 but implementation-defined in detail; the tilted-quad fixture case is the check. `texldd(k=1)` is
  never relied on to equal `texld`: `k_draw = 1` binds the original gained variant.
- **Native Windows**: the variant is bytecode built at create time from documented ps_3_0 opcodes and reads only the
  constant range the route already uploads; identical on Windows and CrossOver. The derivative convention and
  anisotropic `texldd` are unverified natively (add to `platform-portability.md`'s list, as with the shadow pass).
- **Alpha lane**: `rL.w` widened; harmless on opaque draws, and blended/alpha-tested draws bind the un-widened variant.
- **Option and default plan**: `--hull-emissive-widening K[,Q0,Q1]` (env `X3M_HULL_EMISSIVE_WIDENING`), requires HDR and
  the gained hull path like `--hull-lightmap-gain`, `K` in `[1, 8]`, `Q1 > Q0 >= 0`, `K = 1` or absent = off, default
  **off** for the first flight; after the flight verdict the launcher selects `3,2,8` (or the flown value) whenever it
  selects the light-map gain, with `--no-hull-emissive-widening` to clear it, following the far fade's pattern.

## 6. Unknown, and what settles it

- Whether the game's DDS light-map mips are normalised averages (energy conservation with real art): decode one
  bound light map (run225 identity 2353, frame 6516 draw 206) locally and compare the per-level means; a chain built
  with a sharpening filter would show level means drifting. Untracked decode, numbers only.
- Whether CrossOver's D3D backend applies anisotropy to explicit-gradient fetches: fixture case 1, 75 deg tilt.
- The light-map UV density on real hulls (where `rho = 1` falls in metres, hence where the `Q` band should sit): the
  same decode plus the model's UVs, or simply the flight at two `Q` settings. The law does not need it.
- Whether `dsx`/`dsy` may source `v1` directly on every backend (the assembler allows it; if a backend rejects the
  variant at create time, the fallback is one `mov rT, v1` first, +1 slot); `CreatePixelShader` in fixture case 2.
- Visual verdict of `k = 3` versus `k = 2` (soft glow versus sharper but less stable dashes): flight only.

## 7. First flyable step and what to capture

Build the option behind default off, qualify with §3 cases 1-2 and the host tests, install as a candidate, then at the
run225 station (spar and drum in view, 1-2 km; the same save as run225/run227):

1. `--hull-emissive-widening 3,2,8` (everything else as installed: gain 4, far fade 80,220,1, mip bias -0.5), still,
   then pitching as in run225; F8 burst with `--taa-debug` (32 frames, `color_1` / `taa_1` / `taa_mask`) in each state.
2. Same with `2,2,8`.
3. Optional third: `3,2,8 --no-light-map-far-fade`, still only, for the far-window brightness question of §1.5.

Judge: gaps in the spar dashes and drum columns while still; their flicker while pitching; whether near hull light
maps (within about 200 m) look unchanged and mid-range ones (200-800 m) acceptably soft. The triage scores the
tracked-emitter metrics with the jitter-aware decode (`analyze_motion_readback.py`'s `analyze_pixels`), not the
run227 quick tracker.

## Alternatives considered

- **Stage-only sampler LOD bias (thin-glow b')**: zero shader change, per-draw `SetSamplerState`; widens the minor axis
  only, so the oblique columns run227 showed unchanged stay torn. Kept as the no-build fallback and as an A/B control.
- **Per-draw CPU footprint + `texldb`**: no derivatives (`texldb` costs 6 slots and needs the bias in the coordinate's
  `.w`, so two `mov`s: about the same slots as this design); loses on accuracy (UV density unknown per part) and on
  anisotropy (a bias again).
- **`texldl` with an ALU-computed LOD**: +6 slots for the magnitude and log, and explicit-LOD fetches drop anisotropy on
  most hardware, re-introducing aliasing along the major axis. Loses to `texldd`.
- **3-tap or 2x2 supersampled light-map fetch**: 3-4 fetches per hull pixel, isotropic in texel space unless the same
  gradients are computed anyway. Superseded.

## As built (2026-09-22, WIP on the worktree branch, not installed)

Implemented as designed with these concrete choices and measured facts:

- **Transform** (`linear_material.cpp`): `lightmap_widen_fetch` emits the 16-DWORD block of §2.2 in place of the
  texld; `rG` is one above the highest temporary of the *combined* program (walked after assembly, the block itself
  excepted; r9-r11 in the plain variants, r24 in the share variants), refused at 32. `structure()`/`body_shape()`
  admit 91/92 (2 operands, 2 slots) and 93 (5 operands, 3 slots, 2D sampler only) in emitted programs and refuse
  them in originals and vertex programs. The proof re-reads the block word for word (`lightmap_widen_site`), counts
  the six `rG` references and exactly one `c217.z` read. `widen` composes with the static (`def c223`) and the
  dynamic (`c217.w`) gain alike; `widen` with G = 1 is InvalidConfig. Coverage: 100 of the 101 SM3 light-map programs
  (the moon excluded), +7 slots each, largest 271 of 512 (share) / 192 (plain).
- **Route** (`motion_output.cpp`): `configure_hull_emissive_widening(K, Q0, Q1)` (needs the gain; 1 < K <= 8,
  0 <= Q0 < Q1 <= 1e6); the widened variants are created beside a *created* gained variant (both lanes), the
  light-map stage stored per program (`linear_material_hull_lightmap_stage`); `evaluate_draw` uploads
  `k_draw = lightmap_widen_scale(footprint)` in `pixel[6]` (c217.z; 0 without the option or a gain pair; 1 without a
  camera); `bind_variant_pair` selects the widened variant when `k_draw > 1`, the draw is not alpha tested (blending
  is already refused by the opaque chain) and the stage's level count exceeds 1. The `SetTexture` hook installs for
  the option as for the mip bias so the level counts exist without `--taa-mip-bias`. Frame line
  `hull_lightmap_widen_frame` (widened / unity counts, k range, camera), detach line `hull_lightmap_widen_summary`
  (session k range). Ctrl+Shift+F4 drops the widened variant with the gain. No new API call or allocation per draw.
- **Option**: `X3M_HULL_EMISSIVE_WIDENING=K,Q0,Q1` parsed in `capture.cpp` (logs `hull_emissive_widening_mode` /
  `_configured`), launcher `--hull-emissive-widening K,Q0,Q1` (requires the active light-map gain: `--hdr`, not
  `--linear-materials`, gain above 1; no `--taa` requirement: the option latches the camera projection itself, as
  the far fade does); no default (first flight off).
- **Measured** (fixture, CrossOver/FEX, ledger): `CreatePixelShader` accepts every widened program with `dsx`/`dsy`
  on `v1` (no `mov` fallback needed); `texldd` with k = 1 is bit-identical to `texld` on 11 family programs;
  D3DX slot counts +7 on each; the 0.5-px strip's per-phase peak ratio 0.500 (off) -> 0.750 / 0.867 / 0.875
  (K = 2 / 3 / 4), the design's `1 - 0.5/k`; strip energy within 0.4 % and a 64-texel panel within 0.007 % of the
  un-widened image (no rescale); anisotropic filtering is applied to the explicit-gradient fetch (§6 question 2:
  yes, on this backend). Open from §6: the game's DDS mip normalisation, the real hulls' UV density (hence the
  `Q` band), the post-resolve metric, and the flight verdict on K (§7 unchanged).
