# FP16 HDR scene path, stage 1: verification

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

## Limits

- 64×64 (and 48×40, 1280×768, 5120×1440 bench) synthetic frames on the
  CrossOver Preview backend; native Windows is cross-compiled only.
- The identity write-back is proven against the direct 8-bit path to one
  code; no tonemap, exposure or HDR output exists yet (stage 2), and the
  presented picture is unchanged by design.
- The game's compositor (`GetRenderTarget(0)` then `StretchRect`), its glow
  option, the HUD and text draws after the scene end, and the environment-map
  excursion after the compositor are modelled by the fixture scripts, not
  exercised in gameplay.
