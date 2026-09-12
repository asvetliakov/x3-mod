# FP16 HDR scene path, stages 1 and 2: verification

Synthetic verification of the stage-1 topology of
[hdr-scene-path.md](../architecture/hdr-scene-path.md) ("Stage 1
implementation") through the actual proxy DLL under CrossOver Preview's Steam
bottle with the process-local `d3d9=n,b` override, as part of the motion-output
suite. No game launch; no gameplay claim; the game's compositor, HUD and glow
setting are not exercised here. Run 2026-09-12:

```sh
python3 verification/probe/run_motion_output.py   # fresh build + 63 DLL runs, 16 of them with X3M_HDR=1
```

Binaries of the recorded run (after the review-22 fixes): `build/d3d9.dll`
`4f46feee7d9204bd7fbae55378d6e15fb0c2bfe81fddb7e4c64fa0ca84388a8d`, seam DLL
`40345231c2f388df3a58638ec4202ead47b8304b6e0de86b9b829c2c921a2f17`, fixture
`e46e4a4a6f7f815dfdbc0cfd0ff4a44655860be6657974230ecf0a3a95fb3017`; summary
`verification/results/motion-output-summary.json` (`hdr` and `bench` keys),
per-case capture logs `verification/results/motion-output-<case>-capture.log`.

## Device gate and self test (every HDR run)

`hdr_device enabled=1 reason=ok`: `CheckDeviceFormat` for `A16B16G16R16F`
as a render-target texture, with post-pixel-shader blending, with filtering
and as a sampled texture all `00000000`; `CheckDeviceFormatConversion
(A16B16G16R16F → A8R8G8B8)` `00000000`; `MRTPOSTPIXELSHADERBLENDING`
reported. Self test on 4×4 targets: the MRT draw lands `(2, 8, 0.5, 0.25)` as
exact halves in FP16 beside `(1, 2, 3, −1)` in RGBA32F and `0.625` in R32F
(`targets=3`), the additive `ONE`/`ONE` pass sums to `(4, 16, 1, 0.5)` exactly
(FP16 blending exercised, not inferred from the cap), and the write-back
program copies the sum into A8R8G8B8 as `255, 255, 255, 128` (clamp, alpha
carried), and the emergency rung reproduces it after a black `ColorFill`
(`StretchRect(FP16 → A8R8G8B8, POINT)`, `stretch=00000000`; review 22):
`scene_errors=0 sum_errors=0 motion_errors=0 depth_errors=0 copy_errors=0
stretch_errors=0`; the conversion is queried for the back buffer's format
(`main_format=21`, A8R8G8B8). Two runs force a capability absent through the fixture seam
(`X3M_FIXTURE_HDR_FAULT=1`: the render-target format; `=3`: the self test
verdict): `hdr_device enabled=0 reason=fp16_target` / `reason=self_test`, no
`hdr_frame`, `hdr_target` or `hdr_readback` line anywhere, and the presented
frames equal the plain seam run (fail closed).

## Case 1: redirect and identity write-back against the non-HDR twins

Eight scripts run with `X3M_HDR=1` and are compared per pixel (every frame's
`presented_<frame>.bgra8` dump) against their twins without the switch.

| HDR run | twin | pixels | exact | max code diff | differing pixels by class (background / flat / material) | alpha diffs |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| production-hdr-on | production-on | 49,152 | 48,663 (99.01%) | 1 | 0 / 0 / 489 of 27,416 | 0 |
| seam-hdr-on | seam-on | 49,152 | 48,663 (99.01%) | 1 | 0 / 0 / 489 | 0 |
| production-ownership-hdr-on | production-ownership-on | 49,152 | 48,663 (99.01%) | 1 | 0 / 0 / 489 | 0 |
| seam-ownership-hdr-on | seam-ownership-on | 49,152 | 48,663 (99.01%) | 1 | 0 / 0 / 489 | 0 |
| production-taa-hdr-on | production-taa-on | 49,152 | 48,626 (98.93%) | 1 | 0 / 0 / 526 | 0 |
| seam-taa-hdr-on | seam-taa-on | 49,152 | 48,602 (98.88%) | 1 | 0 / 0 / 550 | 0 |
| seam-taa-hook-hdr-on | seam-taa-hook-on | 28,672 | 28,273 (98.61%) | 1 | 0 / – / 399 | 0 |
| seam-taa-envmap-hdr | seam-taa-envmap | 20,480 | 20,270 (98.97%) | 1 | 0 / – / 210 | 0 |

**Finding.** The design's bit-identity criterion does not hold through an
FP16 intermediate: the material program's unquantized output is rounded to
11 significant bits before the 8-bit conversion, so a value near a code
boundary lands one code away from the direct path (double rounding). Every
difference is exactly one code, on a lit material pixel, never on the clear
colour or the flat program's colour, never in alpha (the differing channel
counts are red-heavy because the material's red channel carries the largest
values: `b/g/r/a = 70/171/253/0` in the plain runs). The runner accepts
≤ 1 code with ≥ 98% exact pixels, requires the background, flat and alpha
classes exact and bounds the differing material pixels to 10% of the class
(review 22: the double rounding can touch at most the samples within half an
FP16 ulp of a code boundary, `2^-12 / (1/255)` = 6.2% per channel for values
in [0.5, 1) and half that per octave below, whereas a bias or a sampling
shift would differ on every material pixel; measured 1.6–2.1% of the
class), and records the numbers. The value script below shows the exact
codes of an in-range FP16-exact value, so the deviation is the double
rounding of unquantized outputs and nothing systematic. Bit-exactness
would need an FP32 scene target and is not pursued. Everything else is
identical between the twins: colour hashes before the TAA boundary, the
fixture check and restoration counts (43 / 103 / 83 / 164 / 141 / 67), the
routed-draw decisions, the TAA history frames, the hook script's resolve
sources and cross-check verdicts, and the device reaching zero references in
every environment (plain and ownership wrapper).

Per-frame `hdr_frame` in those runs: `redirected=1` in every latched frame,
`writeback_source=shader`, `unwind=0`, one write-back per frame; end point
`present` for the plain script (the `EndScene` flush wrote the frame,
`dirty_at_present=0`), `bloom_copy` with TAA, `hook` in the hook script's
Scene-phase frames and `bloom_copy` in its outside-Scene frame 2; the
environment-map script suspends and resumes once in frame 1 (faces
mid-scene: two write-backs, one at the switch), leaves frame 3 unredirected
(the faces precede the initial Clear, so the selector never latches) and ends
frames 0, 2, 4 at the bloom copy. Two `hdr_target` lines per regular run
(64×64, 32,768 bytes: the first latch and the first latch after Reset).

## Case 3: four-format MRT

The routed draws of the seam runs write motion (RT1, RGBA32F) and current
depth (RT2, R32F) beside the FP16 RT0. The 16 readback files of
`seam-hdr-on`, `seam-ownership-hdr-on` and `seam-taa-hdr-on`
(`motion_1_<frame>.rgba32f`, `depth_1_<frame>.r32f`, frames 1–8) are
byte-identical to their twins' (SHA-256 per file); the oracle results are
unchanged (44,284 motion pixels, 10,261 matched, 27,170 with depth; 44,279 /
10,348 / 27,293 with jitter). The self test above is the live four-format
check; `seam-hdr-selftest-absent` proves a failing one disables the feature
rather than binding a partial set.

## HDR values (`seam-hdr-values`, 26 checks, 6 frames, 2 Resets)

A drawn with an original `ps_3_0` emitting `(2, 2, 2, 1)`, A again with
`(8, 8, 8, 0.5)` under `ONE`/`ONE`, B (inside A, equal depth, LESSEQUAL) with
`(8, 8, 8, 0.5)` plain in frames 0, 2, 3, 5 and with the in-range
`(0.75, 0.25, 0.375, 0.625)` in frames 1 and 4 (exact in FP16, 8-bit codes
191.25 / 63.75 / 95.625 / 159.375: the presented DWORD must be exactly
`9fbf4060`, i.e. 191, 64, 96, 159 — it is, on all 171 / 77 B pixels, so the
write-back adds no bias and samples no neighbour). FP16 readback through the seam
(`x3m_hdr_fixture_readback`, halves to floats): A = `(10, 10, 10, 1.5)`, B =
`(8, 8, 8, 0.5)`, background = the clear colour, maximum error 3.8e-6 over
3,969 checked pixels at 64×64 (2,457 A, 171 B, 1,341 background; edge pixels
skipped). Presented 8-bit frame: A `ffffffff`, B `80ffffff` (alpha 0.5 → 128
on every B pixel), background `ff203040`. Frames 2–4 after a Reset to 48×40:
`hdr_target` 48×40 / 15,360 bytes, 1,783 checked pixels, same values. Frame 3
binds a 16×16 target mid-scene, clears and draws it, rebinds the main target
and draws B: the application's `GetRenderTarget(0)` reports the other target
while suspended and the main target after the rebind; `hdr_frame
suspended=1 resumed=1 writebacks=2 flushes=2`; the values above hold.
Before frame 5 the fixture latches a frame, draws A, ends the scene and
issues `Reset` while the redirect is still active (no Present in between):
`GetRenderTarget(0)` reports the main target before it, the Reset returns
`00000000` (`motion_output_reset` twice), the next latch creates the target
again (three `hdr_target` lines) and the teardown's final device Release
reaches zero (review 22, finding 1).

## Case 4: must-unwind ladder (`seam-hdr-fault`, 119 checks, 15 frames, 3 Resets)

One injected failure per frame (`x3m_hdr_fixture_fault`); even frames draw A
(material, sentinel-only routed) and B (flat), odd frames A only.

| frame | fault | `hdr_frame` | presented |
| ---: | --- | --- | --- |
| 1 | copy draw fails | `unwind=1 unwind_reason=draw writeback_source=stretch` (`hdr_unwind=draw … draw=80004005 stretch=00000000`) | the scene through the `StretchRect` rung; coverage oracle passes |
| 2 | restoration reported failed after a good draw | `recheck=pass` at the latch, then `unwind_reason=restore writeback_source=shader` | the shader copy; oracle passes |
| 3 | both copy rungs fail | `recheck=pass`, `unwind_reason=stretch writeback_source=restore` | binding restored, image undefined (all zero, see below) |
| 4 | device lost reported by the draw | `recheck=pass`, `unwind_reason=lost writeback_source=restore` (`88760868` on both rungs) | binding restored, image undefined |
| 5 | none | `recheck=pass`, normal | fresh; oracle passes (recovery) |
| 6, 7 | target creation fails after a Reset | `redirected=0 target_create=8007000e`, then still unredirected | LDR frames, oracle passes |
| 8 | none, after a Reset | redirected again | fresh |
| 9 | latch bind fails | `redirected=0 latch_bind=80004005` | LDR frame |
| 11, 12 | draw fails, then the recovery self test fails | `unwind_reason=draw`, then `recheck=fail blocked=1 redirected=0` | frame 12 LDR; the block holds |
| 13 | none, after a Reset | redirected (the Reset cleared the block) | fresh |
| 14 | the latching Clear reports failure (seam kind 10) | `redirected=1 end=clear_failed writebacks=0 flushes=0 writeback_source=none unwind=0` | binding restored without a write-back (the never-cleared target must not reach the main target); LDR frame on the uncleared main target (`black=0`, coverage oracle skipped) |

Exactly one `hdr_unwind=` line per injected write-back failure (five), one
`hdr_recheck` per blocked latch (frames 2–5 passed, 12 failed). **Finding:**
rung 3 (binding restore) guarantees that the FP16 surface never stays bound
past the write-back, but not an image: this fixture presents with
`D3DSWAPEFFECT_DISCARD` like the game, so after Present the main target's
previous content is undefined and frames 3 and 4 came back all zero (4,096
of 4,096 pixels; `previous_equal=0` / `1`). Both copy rungs failing on a
non-lost device is therefore the one residual black-frame case (one frame,
then the block until the recheck passes); a lost device fails both rungs but
its Present fails as well. The fixture's state comparison after every fault
frame confirms the binding and every touched state are back.

## Cost and memory (bench, 24 frames, 20 timed, EVENT-synchronized QPC, CPU-inclusive)

| Size | resolve | boundary, HDR off (median / min) | boundary, HDR on (median / min) | HDR increment (median / min) | FP16 target |
| --- | --- | ---: | ---: | ---: | ---: |
| 1280×768 | off | 0.364 / 0.345 ms | 0.415 / 0.395 ms | **+0.051 / +0.051 ms** | 7,864,320 B |
| 1280×768 | on | 0.738 / 0.673 ms | 0.779 / 0.736 ms | **+0.041 / +0.063 ms** | 7,864,320 B |
| 5120×1440 | off | 0.492 / 0.473 ms | 0.927 / 0.912 ms | **+0.434 / +0.439 ms** | 58,982,400 B |
| 5120×1440 | on | 2.272 / 2.219 ms | 2.561 / 2.502 ms | **+0.289 / +0.283 ms** | 58,982,400 B |

The boundary includes the write-back (inside the `StretchRect` hook, before
the resolve) and the application's copy; the resolve alone is 0.374 /
1.780 ms (median on minus off) in this run (the review-22 rerun; the
author's run measured +0.051 / +0.075 / +0.444 / +0.507 ms, within the same
spread). The increment is within the
design's §6 estimate (+0.2…0.4 ms at 1280×768, +0.8…1.6 ms at 5120×1440),
below it at both sizes. Route-side CPU of the redirect per frame
(`hdr_frame`, 64×64 runs): `redirect_us` 1–6 µs, `writeback_us` median
38–100 µs (the explicit state save/restore plus the draw; the first frame's
lazy creation up to 6.8 ms once), bench frame 0 at 1280×768 139–151 µs and
at 5120×1440 192–246 µs of submission time. Memory: the FP16 target is
`width × height × 8` bytes (`hdr_target … bytes=`), i.e. 7.9 MB and 59 MB at
the two sizes, on top of the route's RT1/RT2 and the TAA histories.

## Other suites after the change (same day)

`run_motion_output.py` PASS (63 runs); `run_temporal_pass.py`,
`temporal_run.py`, `run_ownership_integration.py`, `run_scene_capture.py`,
`check_no_x87.py build/d3d9.dll` (129 reachable functions, no violation) and
`python3 -m unittest discover -s verification/analysis` (560 tests): see
their result files; verdicts are listed in the status record and, for the
rerun after the fixes, in [review-22.md](review-22.md).

## Stage 2: AgX tonemap and exposure (2026-09-12)

Same suite, same day, after the stage-2 build (`run_motion_output.py`: fresh
build + 82 DLL runs, 70 validated cases, PASS; the recorded pass is the
review-23 rerun after its fixes: `build/d3d9.dll`
`db63e120afcbb38e1382f22dffb96fb6030bbab50d1c3faf2b5587e055ce7e3d`, seam DLL
`c0ae7ea45d23524119fb300e8aadbae05dab2755791395e7e77610227981207a`, fixture
`eafb9e6da6b8ab39d29a08dbd5bba2a6ab8bde150fe868005af3946efd96561d`; the
hashes are in `verification/results/motion-output-summary.json`).
Every stage-1 case above was rerun unchanged with the default
`X3M_HDR_TONEMAP=identity`: the eight twins present the same frames as
before (99.01 / 99.01 / 99.01 / 99.01 / 98.93 / 98.88 / 98.61 / 98.97 % exact,
max one code, alpha exact), the value and fault scripts, the forced-absent
runs and the identity bench are as recorded above, so stage 2 leaves the
stage-1 behaviour bit-for-bit in place.

### Gate (every AgX run)

`hdr_tonemap … tonemap=1 tonemap_reason=ok` and, with auto exposure,
`meter=1 meter_reason=ok r32f_target=00000000 r32f_sampling=00000000`;
the self test's stage-2 checks `tonemap=00000000 tonemap_errors=0
meter=00000000 meter_errors=0 meter_value=6.00000 meter_expected=6.00000`
(the one-level chain on the 4×4 additive sum `(4, 16, 1)`: 322.8 clipped
to 64, log2 = 6 exactly). `seam-hdr-tonemap-shader-absent` (fixture fault
12 queued at attach): `tonemap=0 tonemap_reason=shader meter=0
meter_reason=tonemap tonemap_shader=80004005`, `X3M_HDR` stays enabled,
every frame `tonemap=identity`, and the presented frames equal the identity
conversion of the FP16 readback (max 0.5 code, the 8-bit rounding).

### AgX ramp against the reference (`hdrramp`, 64×65, 260 cells × 3 frames)

The fixture draws the 65 ramp rows of `verification/results/agx-ramp.json`
(0.001 … 64, four samples per octave) as neutral / red / green / blue
16-pixel cells with alpha `row/64`; the runner reads the FP16 input from
the DLL's capture-frame readback (`hdr_1_<frame>.rgba16f`: the backend
truncates the fixture's float inputs to FP16, max relative error 8.85e-4,
one FP16 ulp) and compares the presented 8-bit codes with
`agx_reference.tonemap_engine` on those exact inputs. Gate: ≤ 1 code max,
≤ 0.5 code mean per channel (§8 ≤ 1/512 is the shader-vs-reference part;
the 8-bit store rounds by up to 0.5 code on top).

| run | look | decode | clamp | EV | max code error (r / g / b) | mean (r / g / b) | alpha |
| --- | --- | --- | --- | ---: | --- | --- | ---: |
| seam-hdr-ramp-none | none | gamma2.2 | off | 0 | 0.498 / 0.496 / 0.496 | 0.187 / 0.181 / 0.181 | exact |
| seam-hdr-ramp-golden | golden | gamma2.2 | off | 0 | 0.489 / 0.497 / 0.499 | 0.108 / 0.174 / 0.202 | exact |
| seam-hdr-ramp-punchy | punchy | gamma2.2 | off | 0 | 0.493 / 0.497 / 0.495 | 0.218 / 0.242 / 0.231 | exact |
| seam-hdr-ramp-decode-none | none | none | off | 0 | 0.498 / 0.499 / 0.499 | 0.226 / 0.221 / 0.222 | exact |
| seam-hdr-ramp-decode-srgb | none | srgb | off | 0 | 0.499 / 0.490 / 0.494 | 0.201 / 0.185 / 0.181 | exact |
| seam-hdr-ramp-clamp4 | none | gamma2.2 | 4 | 0 | 0.498 / 0.496 / 0.496 | 0.131 / 0.168 / 0.152 | exact |
| seam-hdr-ramp-ev-minus2 | none | gamma2.2 | off | −2 | 0.495 / 0.498 / 0.496 | 0.153 / 0.156 / 0.153 | exact |
| seam-hdr-ramp-ev-plus1-punchy | punchy | gamma2.2 | 16 | +1 | 0.485 / 0.498 / 0.500 | 0.223 / 0.219 / 0.219 | exact |
| production-hdr-ramp-none | none | gamma2.2 | off | 0 | 0.498 / 0.496 / 0.496 | 0.187 / 0.181 / 0.181 | exact |
| seam-hdr-ramp-identity (tonemap off) | – | – | – | – | 0.497 / 0.497 / 0.497 | 0.082 / 0.082 / 0.082 | exact |

The maximum never exceeds 0.5 code, i.e. every presented code is the
correctly rounded reference value: the compiled `ps_3_0` program agrees
with the double-precision reference to better than the 8-bit quantum on
all 780 cells of every configuration (the mean of a pure rounding error is
0.25; the measured means are the rounding plus a sub-0.1-code shader
residual). The production DLL run is identical to the seam run. Each ramp
frame's per-channel statistics repeat exactly in the next frame.

### Exposure (`hdrexposure`, 40 frames, fixed `dt`)

Four 32×32 blocks per frame: frames 0–9 mid-grey `0.18`, 10–19 bright
`(1, .8, .9) / (.9, 1, .8) / (.8, .9, 1) / (1, 1, 1)`, 20–29 mid-grey with
one block at `100` (25,000 after the decode; the meter clips it to 64),
30–34 the hazard blocks `−1 / +Inf / −Inf / 0.18`, 35–39 a NaN block with
three mid-grey ones (added by review 23). The finite negative block is
deterministic and checked (the decode floors it: black, metered at the
floor). The infinite and NaN blocks are **unspecified on this backend** and
are recorded, not compared: measured, `+Inf`, `−Inf` and NaN all present
white (`ffffffff`) and all meter at the floor, i.e. the backend treats an
infinity like a NaN (the reference says `+Inf` → clip and white, `−Inf` →
floor and black; `max(−Inf, 1e-10)` did not floor, so a shader-side clamp
would rest on the same unspecified operations and was not attempted). What
is required and proven: the host-side `isfinite` check on the 1×1 readback
keeps the exposure state finite whether the value is swallowed or reaches
the result, every step consumed an in-range meter, and the presented
finite blocks of those frames match the reference. The stored FP16 values
of the hazard pixels are in the summary (`hazard`). No gamma-space game
content reaches 65504, so no infinity is expected from the scene.
`seam-hdr-exposure`: `dt` 16 ms, defaults; `seam-hdr-exposure-offset`:
`dt` 33 ms, `X3M_HDR_EV=1`, τ 0.2 / 0.6 s, look golden;
`seam-ownership-hdr-exposure`: the first run through the ownership wrapper
(the chain's two level surfaces (16×16, 4×4), two 1×1 ring targets and
two readback surfaces are counted by `references()`; the device reaches
zero at teardown).

- **Meter.** Measured `avg_log_l` of the mid-grey frames −5.4439 against
  the reference −5.4417 (the reference rounds the FP16 inputs to nearest,
  the backend truncated them: 2.2 × log2(0.17993/0.18) ≈ −0.0012), bright
  −0.2464 vs −0.2464, sun frames −2.5829 vs −2.5813: max absolute error
  0.00215 log2 units = **0.063 % relative** (gate 1 %). The chain at
  64×64 is three draws into 16×16, 4×4 and 1×1 R32F levels (`chain_bytes`
  1,096); it reduces exactly to the mean.
- **Clip.** Without the clip the sun frames would meter −0.43; with it
  −2.58: the clip bounds the block's influence by 2.15 stops (the fourth
  block contributes 6.0 instead of 14.6).
- **Adaptation.** The EV sequence against `exposure_reference.simulate`
  replayed on the measured meters: max error **1.2e-6 EV** (offset run
  9.4e-7; gate 1e-3). End to end on the reference meters: 7.2e-4 and
  1.7e-3 EV, the FP16 truncation of the inputs integrated over the frames
  (the faster time constants of the offset run integrate more of it).
  Direction: 0 → 0.116 (frame 1) → 0.898 (frame 9) → 0.979 (frame 10, the
  last dark meter) → 0.617 (frame 19, τ_down) → 0.579 → 0.526 (frame 29,
  towards the clipped target 0.109); the offset run 0 → 0.604 → 3.071 →
  3.208 → 1.476 → 1.331 → 1.245. `dt` on every step 0.016 / 0.033 s
  exactly; `stepped=1 steps=n` on every frame after the first.
- **Presented blocks** against the reference tonemap at the consumed EV:
  max 0.53 code (the 8-bit rounding), alpha exact.
- **Phase cost at 64×64 (median, CPU-inclusive):** meter chain 88–105 µs
  inside the write-back bracket (three draws), the copy and lock of the
  previous frame's 1×1 at the latch 42–46 µs (a Present after it was
  written: never a wait on the current frame), write-back draw 123–147 µs
  including the chain and the AgX draw.

### Tonemap ladder (`seam-hdr-tonemap-fault`, 59 checks, 9 frames)

| frame | fault | `hdr_frame` | presented (vs the FP16 readback) |
| ---: | --- | --- | --- |
| 0 | none | `tonemapped=1 unwind=0 meter=00000000` | AgX |
| 1 | 11: the tonemap draw fails | `tonemapped=0 unwind=1 unwind_reason=tonemap fallback=1 writeback_source=shader tonemap_draw=80004005` (`hdr_unwind=tonemap … draw=00000000 restore=00000000`) | identity, max 0.5 code |
| 2 | none | `recheck=pass tonemapped=1` | AgX at the consumed EV, max 0.5 code |
| 3 | 13: the meter chain fails | `tonemapped=1 unwind=0 meter=80004005` | AgX |
| 4 | none | `stepped=0`, EV equal to frame 3's (the exposure held) | AgX, max 0.5 code |
| 5, 6 | 11 twice | two more `hdr_unwind=tonemap`; `hdr_tonemap_disabled … reason=draw_failures` at frame 6 (the third failure) | identity |
| 7, 8 | none | `recheck=pass` at 7, then `tonemap=identity tonemapped=0 meter=00000001 stepped=0` | identity, max 0.5 code |

Exactly three `hdr_unwind=` lines, rechecks at frames 2, 6 and 7 (all
passed), one disable line; the binding and every touched state are back
after every frame (the fixture's state comparisons).

### Cost (bench, 24 frames, 20 timed, EVENT-synchronized QPC, CPU-inclusive)

`X3M_HDR_TONEMAP=agx`, auto exposure (the chain runs every frame), look
none, decode gamma2.2; the resolve off and on. Stage-1 columns are this
run's identity twins (they reproduce the stage-1 record above within its
spread).

| Size | resolve | boundary, HDR off | stage 1 (identity) | stage 2 (AgX + meter) | stage 2 − stage 1 (median / min) | chain memory |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1280×768 | off | 0.368 / 0.349 ms | 0.424 / 0.395 ms | 0.867 / 0.718 ms | **+0.443 / +0.323 ms** | 262,156 B |
| 1280×768 | on | 0.740 / 0.693 ms | 0.806 / 0.750 ms | 1.196 / 1.077 ms | **+0.390 / +0.327 ms** | 262,156 B |
| 5120×1440 | off | 0.613 / 0.479 ms | 0.928 / 0.911 ms | 1.428 / 1.386 ms | **+0.500 / +0.475 ms** | 1,966,296 B |
| 5120×1440 | on | 2.248 / 2.216 ms | 2.718 / 2.577 ms | 2.945 / 2.880 ms | **+0.227 / +0.302 ms** | 1,966,296 B |

Review 23's rerun reproduced three of the four increments (+0.33 / +0.33 /
+0.36 ms) but measured 2.653 ms against 0.936 ms (+1.72 ms) at 5120×1440
with the resolve off, GPU-side (the CPU phases of its timed frames total
0.7 ms); repeat the two 5120×1440 benches before treating either figure as
the chain's cost at that size ([review-23.md](review-23.md), observation 9).
Two findings, both measured on the bench before this run. (1) The AgX draw
itself costs nothing measurable: with a manual EV (no chain, no readback)
the 1280×768 boundary is 0.429 ms against the identity draw's 0.443 ms
(one 24-frame run each, same spread). (2) The exposure chain is the whole
increment, and where its 1×1 result is copied decides most of it: issuing
`GetRenderTargetData` right after the chain, inside the write-back, cost
1.207 ms against 0.443 ms — the backend waits for the queued frame there —
while copying and locking at the next latch (the committed form) costs
0.799 ms in the same A/B, i.e. +0.36 ms for six chain draws submitted at
~25 µs each (`meter_us` 165–172 µs on the timed frames above, against a
whole identity write-back of 74–158 µs) plus 36–74 µs for the deferred
copy and lock (`readback_us`), the rest being the six small draws' GPU and
switch latency. The increment is nearly size-independent, so it is the
chain's per-draw overhead, not its bandwidth; the lever is a coarser chain
(8× per axis: three draws at 1280×768 with 64 taps each) if a gameplay
profile shows the boundary matters. Memory: the chain is
`Σ ceil(w/4ⁱ) × ceil(h/4ⁱ) × 4` bytes plus 16 for the ring and readback
surfaces (`hdr_target … chain_bytes=`), 0.26 MB and 1.97 MB at the two
sizes on top of the FP16 target.

## Other suites after the stage-2 change (same day)

See the status record: `run_temporal_pass.py`, `temporal_run.py`,
`run_ownership_integration.py`, `run_scene_capture.py`, `check_no_x87.py
build/d3d9.dll`, the generator `--check` (seven programs) and `python3 -m
unittest discover -s verification/analysis` (568 tests, including the
native compile of `src/renderer/exposure.cpp` against the reference in
`test_exposure_port.py` and the manifest pins of the three new programs in
`test_agx_reference.py`).

## Limits

- 64×64 (and 48×40, 64×65, 1280×768, 5120×1440 bench) synthetic frames on
  the CrossOver Preview backend; native Windows is cross-compiled only.
- The identity write-back is proven against the direct 8-bit path to one
  code and the presented picture is unchanged by design; with
  `X3M_HDR_TONEMAP=agx` the presented picture is the AgX transform of the
  gamma-space FP16 scene decoded per §2 (a documented approximation),
  proven against the Python reference on synthetic ramps and blocks, still
  LDR to the game's bloom and GUI, and never seen in gameplay; the
  exposure meter and adaptation are proven on constant blocks with a fixed
  `dt`, not on game content or wall-clock intervals.
- The game's compositor (`GetRenderTarget(0)` then `StretchRect`), its glow
  option, the HUD and text draws after the scene end, and the environment-map
  excursion after the compositor are modelled by the fixture scripts, not
  exercised in gameplay.
