# Source anti-aliasing under the TAA: MSAA, thin-geometry alternatives

**Ratified 2026-09-19 (orchestrator): MSAA no-go, SSAA dropped by the user.** The bottle would accept a
multisampled FP16 + MRT + depth set (probe: `verification/results/bottle-X3/msaa-caps-probe.txt`), but the
box-filter resolve corrupts the sentinel depth and the motion target on exactly the strut pixels, the owned
multisampled depth-stencil sits on the must-unwind HDR path, MRT + MSAA is outside the documented D3D9 contract
for the Windows target (wording to be verified before it is quoted elsewhere), and the modelled gain is partial
(block bands x 0.3-0.86) with nothing for alpha-tested cutouts. Correction to the note: `--cull-small-parts 2`
(scope `all`) is already the flown default (runs 136, 147); the struts survive it. Next, in order: (1) fly the
adaptive history weight (near-static case); (2) settle by capture rows whether the struts are triangles or
alpha-test cutouts; (3) a motion-compensated shimmer metric on run148 to size the drift artefact; (4) only then
the coverage-gated current filter. The luminance clamp is rejected (struts are mid-tones, kL p99 4.5).

Design note for ratification, 2026-09-19. Question: can the proxy add geometric anti-aliasing at the source,
underneath the existing TAA, to remove the shimmer of sub-pixel geometry (1 px sunlit lattice struts, antennas)
that the resolve cannot remove while the ship drifts
([taa-flicker-suppression.md](taa-flicker-suppression.md) section 10.1,
[motion-output.md](../verification/motion-output.md) "Run 44 B")? Tags: **[M]** measured this session or in the
cited record, **[I]** inferred/modelled, **[A]** assumed (memory of documentation or driver behaviour, not checked).

Supersampling (option B) is dropped by user decision (2026-09-19): too costly; not evaluated.

## 1. Decision

- **MSAA: no-go now.** It works on the bottle **[M]**, but (a) D3D9 documents MRT as "no antialiasing" **[A]**,
  so MSAA + the three-target MRT is outside the documented contract the native-Windows rule requires; (b) the
  averaged resolve corrupts the sentinel-coded motion/depth targets at exactly the pixels it is meant to fix
  **[M, probe]**; (c) it needs an owned multisampled depth-stencil swapped at every target change on the
  must-unwind HDR path; (d) the modelled gain under the TAA is x 0.5-0.75, not a kill **[I]**, and zero on
  alpha-tested cutouts. Revisit only if the three measurements of section 6 all come out in its favour; the
  ordered plan for that case is section 7.
- **Cheap alternatives (section 5):** fly the already-built `--cull-small-parts 2` (separate tiny nodes only);
  implement the flip-pixel-gated current-sample filter (small, measured -12..-16 % per px, no global blur);
  reject the luminance clamp (the struts are not bright: nothing to clamp **[M]**).
- **Before any further shimmer work:** size the artefact with a motion-compensated metric and classify the
  strut draws (geometry vs alpha test). Both are offline/triage tasks on existing or one new capture.

## 2. Facts established this session

Probe: `verification/probe/msaa_caps_probe.cpp` (throwaway, documented D3D9 only), run twice as
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py wine --bottle X3 --no-update build/msaa_caps_probe.exe`;
record `verification/results/bottle-X3/msaa-caps-probe.txt` (53 lines). All **[M]**:

- The bottle's D3D9 is **wined3d** (adapter string `NVIDIA GeForce 8800 GTX` / `nvd3dum.dll`, wined3d's
  fallback identity; the game log shows the same; `cxbottle.conf` `CX_GRAPHICS_BACKEND=dxmt` concerns
  D3D10+). Which wined3d back end (GL or Vulkan over Metal) is **unknown**; no stderr line names it.
- `CheckDeviceMultiSampleType` (HAL, windowed) succeeds for X8R8G8B8, A8R8G8B8, A16B16G16R16F, A32B32G32R32F,
  G32R32F, R32F, G16R16F, D24S8, D24X8 at 2x, 4x and 8x (quality levels 1) and NONMASKABLE (3): 36/36 `S_OK`.
- `NumSimultaneousRTs=4`, `MRTINDEPENDENTBITDEPTHS=1`, `MRTPOSTPIXELSHADERBLENDING=1`.
- A multisampled FP16 + 2x A32B32G32R32F + D24X8 set creates, binds, clears, draws (ps_2_0 writing oC0-oC2)
  and resolves by `StretchRect` into render-target textures at 2x and 4x, every call `S_OK`. The resolve is
  real: 3 distinct edge values at 2x, 5 at 4x; 566 / 667 partially covered px on the test hypotenuse.
- **`StretchRect` with `D3DTEXF_POINT` averages exactly like `D3DTEXF_NONE`** on the 32F targets (same 566 /
  667 partial px). "Point resolve = sample 0" does not exist here, and D3D9 does not define it anywhere **[A]**.
- wined3d **accepts a multisampled colour target with the non-multisampled auto depth** (Set and Draw `S_OK`).
  Native D3D9 requires equal multisample type and fails the draw **[A]**; the bottle will not catch this bug.
- User resolution in run148: 1280x768 windowed, `msaa=0`, auto depth format 77 (D24X8) (`create_device` and
  `telemetry_presentation` rows of the session log).
- Strut brightness, run148 frames 4411-4426, 39 149 coverage-toggling px, `taa_k` 2.17: covered `kL` p50 0.77 /
  p90 1.26 / p99 4.49 / p99.9 6.74, none above 16; uncovered p50 0.30. The sunlit struts are **mid-tones against
  a darker body**, not HDR outliers.

## 3. D3D9 rules that bind MSAA

1. A multisampled render target is a surface from `CreateRenderTarget`, never a texture; it is read only after
   `StretchRect` into a non-multisampled render-target texture of the same format and size.
2. Every simultaneously bound render target and the depth-stencil must have the same multisample type and
   quality; the depth-stencil must be at least as large as RT0.
3. The documented MRT restrictions list "no antialiasing is supported" **[A, MSDN "Multiple Render Targets
   (Direct3D 9)"]**. DX10-class Windows drivers are believed to allow it anyway **[A]**; that is driver
   latitude, not contract. Under AGENTS.md this can only be an optional capability behind a run-time self
   test (the probe's draw/resolve/readback), with "no MSAA" as the portable path. Since the feature's whole
   purpose is the enhancement, Windows would have it only where an unverifiable self test passes.
4. Support is per format: `CheckDeviceMultiSampleType` for each RT format and the depth format, taking the
   minimum quality level.
5. The pixel shader runs once per pixel; its outputs are replicated to the covered samples of **every** RT.
   Only coverage and depth are per sample. Texture/alpha-test aliasing inside a primitive is untouched.
6. No per-sample read, no custom resolve, no `SV_Coverage`/`oMask`, no documented alpha-to-coverage.

## 4. MSAA in this renderer

### 4.1 Depth-stencil ownership

Today the HDR path never rebinds depth: the game's D24X8 auto surface stays bound under the owned FP16 RT0
(hdr-scene-path.md section 1). Production consumers of the *game's* depth: the rasteriser's own Z test only.
`copy_auto_depth` (`src/ownership`) is called from `scene_capture.cpp` in capture mode only; the shadow lane,
TAA depth tests and depth history read **RT2**, not the depth surface; the RESZ adapter is not in production
(native-windows-audit D4) **[M, source/docs]**. The game samples no depth texture (auto depth is a plain surface).

With MSAA the proxy must own a multisampled D24X8 and bind it together with RT0-2 at the latching `Clear`
(which then clears it), and restore the game's depth whenever RT0 leaves the owned set: each application
`SetRenderTarget(0, other)` (the six env-map faces are outside the scene bracket, but any mid-scene
render-to-texture is not), each proxy pass, and the write-back. `GetDepthStencilSurface`, the state shadow and
state blocks must keep reporting the application's surface, as they already do for RT0. After write-back the
game's depth buffer is **empty** (cleared, never drawn). Whether anything the game draws into the main target
after the scene-end hook tests depth (HUD target boxes, cockpit, lens flares' occlusion) is **unknown**; if so,
it needs a depth transfer that D3D9 cannot do between multisample types (`StretchRect` on depth-stencil
surfaces needs matching types and is refused inside a scene). Settle: triage one `--capture` frame for draws
after the hook callsite `0x004721b1` with `ZENABLE != 0` on the main target.

### 4.2 What the box resolve does

- **(i) HDR colour.** A linear average followed by display compression over-weights bright samples. For the
  measured struts (`kL` about 0.8 on 0.3) the error is small: one of four samples covered displays at 0.31
  of the full step instead of the ideal 0.25 **[I]**. For a hypothetical `kL` 30 strut the model gets
  *worse* than 1x in three of four drifting cases (slow block band x 1.4-2.0) and a pre-resolve clamp at `kL` 4 repairs it **[I]**; the dumps
  show no such pixels on the lattice. Per-sample tonemap before resolve is impossible in practice:
  compressing in the variants breaks every blended and additive draw and the 7.3 % unrouted SM1/SM2 draws.
  Accept the box resolve; the TAA's own luminance weighting follows it.
- **(ii) Motion, RT1.** Edge pixels become coverage-weighted means of foreground and background vectors and
  of the validity lane. For far static geometry against the background both are the same camera motion, so
  the mean is harmless; at a near-over-far silhouette the vector is a phantom, and the fractional validity
  needs a defined meaning in `resolve.hlsl`. No closest/dominant selection is available; a second
  non-MSAA pass doubles the draw submission that already owns the 20 ms frame: excluded.
- **(iii) Depth, RT2.** The sentinel is -1. One covered sample of four over an empty background resolves to
  `(0.99 - 3) / 4 = -0.5`; the linear lane `.b` mixes 30 188 with its clear value the same way. Every
  consumer breaks on exactly the strut pixels: validity (`depth >= 0`), dilation, the far-plane path, the
  depth history, and the shadow apply (receiver position from `.b`). Required redesign: clear RT2 to the far
  value, carry validity as a 0/1 lane so the resolve yields **coverage** `c`, un-mix `d = (avg - (1-c) far)/c`
  for geometry-over-background, and accept phantom depths on geometry-over-geometry edges (one-pixel
  shadow fringes on near silhouettes, softened by the TAA) **[I]**. This touches the variants' RT2 writer,
  `resolve.hlsl`, the shadow apply, any other RT2 reader (screen-emission not checked) and every fixture
  oracle that knows the sentinel.

### 4.3 Alpha-tested cutouts

The plant has 11.5 % alpha-tested prims. MSAA gives them nothing (rule 5). Without SSAA the only source-side
fix is alpha-to-coverage, which in D3D9 is a vendor hack: NVIDIA `ATOC` via `D3DRS_ADAPTIVETESS_Y`, AMD `A2M1`
via `D3DRS_POINTSIZE`, detected by `CheckDeviceFormat` on the FOURCC; wined3d is believed to honour both
**[A]**. It is admissible only as an optional adapter on top of an MSAA path that already exists, and the
variants would also have to rescale alpha to a one-pixel ramp (`(a - ref) / fwidth(a) + 0.5`, ps_3_0 has
derivatives) for it to help minified cutouts. A stochastic per-frame alpha reference resolved by the TAA adds
noise in the same bands the user already sees and is not proposed. The dumps cannot tell whether the lattice
lines are thin triangles or cutouts (Run 44 B, section B); **if they are cutouts, MSAA is pointless for the
reported symptom.** Settle: one capture with the per-draw `ALPHATESTENABLE` state joined to the draws that
write depth on flip px (the capture rows already carry `state`; it needs the draw id per flip px, i.e. a
draw-index dump target or a triage pass that replays the scissored region).

### 4.4 Interaction with the TAA and sharpen

Keep the 8-sample jitter: MSAA + TAA is standard, the sample pattern moves rigidly with the jitter and the
resolve sees a pre-filtered current sample. The variance clip sees smoother neighbourhoods (fewer collapses,
already rare: the clamp moves 2-3 % of strut px-frames **[M]**). RCAS keeps raising the presented residual by
40-80 % **[M, Run 44 B]**; MSAA lowers what it amplifies but does not change the ratio.

### 4.5 Expected gain (what can and cannot be predicted offline)

`tools/analysis/taa_resolve_replay.py` replays run148's single-sample dumps; extra geometric samples cannot
be synthesised from them, so MSAA is predictable only on a synthetic lattice.
`tools/analysis/msaa_lattice_model.py` (oblique 0.8-1.0 px lines, pitch 2.37-3.1 px, measured `kL` 0.77 on
0.30, Halton-8, box resolve, w 0.9 Catmull-Rom history, no clip) **[I]**:

| drift px/frame | 2x: resolved 8-px block bands vs 1x | 4x | 4x per-px p2-4 / p4-8 vs 1x |
|---|---|---|---|
| 0 | 0.36-0.81 | 0.44-0.67 | 0.77-0.88 / 0.79-0.91 |
| 0.29 | 0.49-0.78 | 0.47-0.81 | 0.63-0.77 / 0.64-0.76 |
| 0.5 | 0.40-0.79 | 0.30-0.86 | 0.68-0.74 / 0.60-0.88 |

About the square-root-of-samples law: **4x removes a third to a half of the geometric shimmer, 2x a quarter
to a third.** Limits: the model's absolute block band (0.05-0.2 codes) is far below run148's 6.76, because
that metric on real captures is dominated by true scene motion through fixed blocks (section 10.1's own
inference); the size of the *artefact* in the real captures is unknown, so the ratio cannot be turned into
codes. A per-pixel slow band is real motion in both and does not move.

### 4.6 Cost and risk

- **CPU (the bound resource):** no per-draw work; per frame one depth swap per RT0 transition and three
  `StretchRect` resolves. Negligible against 20 ms **[I]**.
- **VRAM at 1280x768, 4x:** samples 4 x (8 + 16 + 16 + 4) B = 176 B/px = 165 MiB, plus the existing resolved
  textures (about 41 MiB); 2x: 83 MiB. RT1/RT2 at 128 bit are 73 % of it; the compact motion encoding
  (hdr-scene-path.md section 6) would be a prerequisite, not an option.
- **GPU:** unmeasured. On a tile-based GPU every mid-scene target change stores and reloads the full
  multisampled set (2 x 165 MiB per break); the number of breaks per frame in lazy-RT mode is not logged.
  The three resolves read the 165 MiB once per frame. Could be 1 ms or 10 ms **[I]**; see section 6.
- **Reset / device loss / ownership:** four more `D3DPOOL_DEFAULT` objects in the release-before-Reset set
  (mechanical). The real risk is the depth swap inside the must-unwind redirect: a missed restore leaves the
  game drawing with our depth (or, on Windows, failing every draw on a type mismatch that wined3d silently
  accepts, section 2). Every degradation rung of the write-back ladder gains a depth restore.
- **Native Windows:** compiles against documented calls, but relies on undocumented MRT + MSAA latitude
  (rule 3); unverifiable by the user. Would be recorded as a gap in platform-portability.md.
- **Size:** about 600-900 lines in `motion_output.cpp` (targets, depth swap, shadow, ladder), RT2 re-encoding
  across the variant generator, `resolve.hlsl`, shadow apply, screen-emission, plus new fixture twins for
  every existing HDR/TAA/shadow case. The largest change to the route since the HDR redirect.

## 5. Cheaper source-side alternatives for thin geometry

| Option | Expected effect on the struts | Cost | Risk / Windows | Verdict |
|---|---|---|---|---|
| `--cull-small-parts 2` (built, unflown; engine-frame-time.md 2.3) | None on the lattice: the cull is per **node** by bounding radius, the struts are sub-features of a large plant node **[I]**. Removes separately-noded antennas/greebles under 2 px, which then pop instead of shimmer | Saves about 9.6 ms CPU at the station view **[M, census]** | Exact-EXE trampoline, same on Windows | **Fly it** for the frame time; judge antennas by eye |
| Luminance clamp of sub-pixel brights before the TAA | About zero: covered strut `kL` p99 4.5, none above 16 **[M]**; the resolve already luminance-weights every tap | one `min` | none | **Reject** |
| Wider current-sample filter **only on flip/thin px** (the existing thin mask is true on 83 % of flip px-frames **[M]**) | Replay of run148 with filter 1.0 everywhere: per-px 3.57 / 5.37 / 17.62 -> 3.00 / 4.46 / 15.42 (-16 / -17 / -12 %), block -1.6 %, at sharpness 0.53; gating keeps stable-pixel sharpness near 1 and the same gain on flip px **[I from M]** | a few taps on masked px only; resolve is 0.4-1.2 ms today | shader-only, portable | **Do it** (small); predictable exactly with the replay oracle before flying |
| Lower mesh LOD for distant stations (inverse of `--lod-scale`) | Unknown: needs the LOD census to show whether a plant LOD without the lattice exists | none | pops | Check in the LOD records before anything else is built |

The replay oracle can predict rows 2 and 3 exactly on run148/run142 (they only re-filter existing samples);
it cannot predict row 1 or 4 (geometry changes) or MSAA (section 4.5).

## 6. Measurements that would reopen MSAA

1. **Artefact size.** Motion-compensated shimmer on run148: warp `hdr_1`/`present_1` along the dumped motion
   (remove the logged jitter delta first: far-geometry `dx` spans -0.28..+0.38 px around 0 in a single frame
   **[M]**, i.e. the dump includes jitter), then the section-10.1 band metric in the object frame. If the
   stabilised residual is already about 1 code, no source AA is worth its cost; if it is 3+ codes, continue.
2. **Struts: triangles or cutouts** (section 4.3). Cutouts end the MSAA case.
3. **GPU headroom in one user flight.** `D3DQUERYTYPE_TIMESTAMP` is `D3DERR_NOTAVAILABLE` on this D3D9
   (ambient-occlusion.md; the `gpu_us` plumbing in `motion_output.cpp:1964-2017` therefore reports -1).
   Use an event fence instead: issue `D3DQUERYTYPE_EVENT` `End` just before the game's `Present`, and on the
   next frame's first call measure the QPC time `GetData(D3DGETDATA_FLUSH)` takes to signal; log
   `gpu_drain_us` and the frame `dt` per telemetry interval. Drain about 0 with 20 ms frames means the GPU
   idles (headroom at least the CPU frame); drain near `dt` means GPU-bound already. One diagnostic flag,
   off by default, batched into the next diagnostic build; with wined3d CSMT the fence also includes the
   command-stream lag, so treat it as an upper bound of GPU busy time. Also log RT0 transitions per frame
   (the tile store/load count of section 4.6).
4. Post-hook depth-tested draws (section 4.1).

## 7. Plan if 6.1-6.3 favour MSAA (each step verifiable alone, all default off)

1. Compact RT1/RT2 (G16R16F motion + validity, R32F/G32R32F depth with far-clear and a coverage lane);
   fixtures unchanged in meaning; worth having without MSAA (bandwidth).
2. `resolve.hlsl`, shadow apply and any other RT2 reader consume coverage + un-mixed depth; replay oracle and
   fixture twins prove bit-equality at coverage 0/1 (no behaviour change at 1x).
3. Capability gate + run-time self test (the probe's MRT draw/resolve/readback, plus a mismatched-depth
   draw that must *fail* to detect permissive back ends); refusal path = today's route.
4. Owned multisampled depth and target set, depth swap on every RT0 transition, ladder rungs; fixture:
   drifting-lattice case at depth 0.99 (section 10.1's correction) comparing 1x / 2x / 4x band rms, Reset and
   fault twins. Start with **2x** (half the memory, most of the first-step gain).
5. One user flight A/B on the plant view with `--taa-debug` dumps; judge on the stabilised metric of 6.1.
6. Optional `ATOC`/`A2M1` adapter only if 6.2 found cutouts that matter and step 5 showed a visible gain.

## 8. Unknowns

- wined3d back end of the bottle (GL vs Vulkan); irrelevant to the decision, relevant to GPU cost.
- Native MRT + MSAA behaviour and the exact MSDN wording (**[A]**; check the SDK page before quoting it in
  platform-portability.md).
- Whether the visible drift shimmer is mostly the fast per-pixel bands after RCAS or a block-scale crawl;
  6.1 answers it.
- Whether a plant LOD without the lattice exists (section 5, row 4).
