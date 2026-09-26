# FP16 HDR scene path, stages 1 and 2: verification

Synthetic verification of the stage-1 topology of
[hdr-scene-path.md](../architecture/hdr-scene-path.md) ("Stage 1
implementation") through the actual proxy DLL under CrossOver Preview's Steam
bottle with the process-local `d3d9=n,b` override, as part of the motion-output
suite. No game launch; no gameplay claim; the game's compositor, HUD and glow
setting are not exercised here. Run 2026-09-12:

```sh
python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py
# Recorded stage-1 run: 63 DLL runs, 16 with X3M_HDR=1
```

Binaries of the recorded run (after the review-22 fixes): `build/d3d9.dll`
`4f46feee7d9204bd7fbae55378d6e15fb0c2bfe81fddb7e4c64fa0ca84388a8d`, seam DLL
`40345231c2f388df3a58638ec4202ead47b8304b6e0de86b9b829c2c921a2f17`, fixture
`e46e4a4a6f7f815dfdbc0cfd0ff4a44655860be6657974230ecf0a3a95fb3017`; summary
`verification/results/motion-output-summary.json` (`hdr` and `bench` keys),
per-case capture logs `verification/results/motion-output-<case>-capture.log`.

## Space-aware meter follow-up (2026-09-13)

The stage-2 exposure figures below describe the historical 1×1 mean meter.
The branch follow-up carries mean/max tile images, a weighted lit median,
a p99 tile-maximum ceiling, EV −3…+2 and a held-target dead band. Complete
Steam and X3 motion-output suites pass: each has 98 validation cases and
16 benchmark invocations, with additional derived comparisons in the summary.
Supporting suites also pass in both bottles; the final independent verdict
is recorded in review 32. See the
[handoff](handoff-exposure-meter.md) and [independent review](review-32-exposure.md).
The current source uses `chain_target`/`chain_sampling` and `chain_format`
for its G32R32F or RGBA32F meter capability, and the self-test verifies
`meter_max` as well as `meter_value`. Historical `r32f_*` records below do
not describe this new chain.

The standalone `run_exposure_statistics.py --host` benchmark records the
production statistic with original deterministic varied tile values. Data,
allocation and cosine weights are prepared before timing; 20 warmup calls
precede 41 batches of 40 calls. The time includes small result checks and
excludes GPU reduction, copy, lock, adaptation and game rendering. On this
native arm64 host, the current compact source/compiler-bound record is
`verification/results/exposure-statistics-host.json`:

| Tile image | 10% lit median / p95 | Dense median / p95 |
| --- | ---: | ---: |
| 80×48 | 5.99 / 11.66 µs | 36.35 / 44.93 µs |
| 80×23 | 2.83 / 2.91 µs | 14.43 / 14.78 µs |
| 128×128 | 29.34 / 31.74 µs | 483.84 / 534.53 µs |

These are per-call statistics derived from batch timings, not single-frame
tail latencies or game FPS. Full sorting dominates dense input; the maximum
size is a supported worst case, rather than the tile size at either tested
game resolution. The x86/SSE2 measurements also pass in both bottles. Their medians / p95
values (microseconds per call, from batches) are:

| Tile image / lit coverage | Steam / Rosetta | X3 / FEX |
| --- | ---: | ---: |
| 80×48 / 10% | 8.20 / 8.40 | 7.48 / 8.53 |
| 80×48 / dense | 42.90 / 50.05 | 43.25 / 59.50 |
| 80×23 / 10% | 4.55 / 4.75 | 3.95 / 4.15 |
| 80×23 / dense | 19.18 / 21.23 | 16.48 / 19.65 |
| 128×128 / 10% | 36.40 / 39.15 | 32.53 / 34.68 |
| 128×128 / dense | 427.78 / 500.98 | 448.80 / 486.58 |

Records are `verification/results/exposure-statistics.json` and
`verification/results/bottle-X3/exposure-statistics.json`. Each retains its
own executable, source/compiler/output hashes and bottle configuration;
the workload allocates nothing inside timing. The dense worst case costs
about 0.45 ms on FEX, while the tested game resolutions' tile images cost
16–43 µs when dense. Keep the bounded sort: these diagnostics do not justify
a more complex weighted-selection algorithm. The runner must be wrapped
with `wine_lock.py`; this is portable x86 source executed under Preview,
not a native-Windows performance measurement.

The boundary benchmark still uses 24 frames with the first four omitted and
EVENT-synchronized QPC. It measures the final scene write-back boundary,
including the meter GPU draws, **not** the next frame's readback/statistic at
the scene latch. Historical medians come from the committed pre-meter summaries
at `ee7d2dc`; they are separate runs, not a paired causal measurement:

| Scene / TAA | Steam old → tile median | X3 old → tile median | Tile-chain payload |
| --- | ---: | ---: | ---: |
| 1280×768 / off | 0.722 → 0.724 ms | 0.748 → 0.674 ms | 614,400 B |
| 1280×768 / on | 1.256 → 1.202 ms | 1.194 → 1.429 ms | 614,400 B |
| 5120×1440 / off | 1.902 → 1.331 ms | 1.404 → 1.527 ms | 3,975,680 B |
| 5120×1440 / on | 2.426 → 2.361 ms | 4.206 → 2.897 ms | 3,975,680 B |

The new X3 min/max ranges are respectively 0.542–1.039, 0.898–2.136,
1.364–2.103 and 2.147–3.635 ms; this spread rules out reading small median
differences as an isolated improvement/regression. The smaller draw count
trades against a larger download and CPU statistic. At logged frame 20, X3
readback/statistic brackets were 117.5/116.0 µs at 1280×768 and
152.2/102.7 µs at 5120×1440 (TAA off/on). These are individual diagnostic
samples, not a tail-latency measurement. No game FPS claim follows.

Payload counts cover intermediate levels, two ring targets and two system-memory
readbacks at eight bytes/texel, excluding driver padding/metadata. RGBA32F fallback
doubles those counts. Cached host arrays consume 20 bytes/tile: 76,800 bytes at
1280×768 and 36,800 at 5120×1440, bounded by 327,680 bytes at 128×128 tiles.

## Final follow-up checks

Both bottle-specific reports pass: TemporalPass 386 samples, 278 state
restorations and two device generations; SceneCapture 4,908 checks / 16
samples; ownership integration 26 environments. Full motion-output reports
include the existing TAA/HDR, reset, restoration and fault cases, in addition
to the new exposure controls. The analysis suite passed 814 tests; the
configuration parser's 21 negative/valid controls run in both optimized and
ASan/UBSan host builds. Later fixture-only corrections were verified by the
complete runtime suites.

`verification/results/exposure-final-checks.json` retains the final DLL/tool
hashes and static SSE2 audit (195 reachable functions, no prohibited x87
arithmetic), plus the completed `generate_rigid_motion_pixel.py --check`
result for all ten embedded programs, including 1,996-word mean/max level
zero and 462-word reduction shaders. The reference module's later wording
correction changed only its docstring; old/new hashes and executable-AST
equality are in `exposure-reference-doc-only.json`. No GPU rerun was used
or needed to imply a numerical change from that wording correction.

The branch motion runner's before/after source map omits imported numerical
references and helper modules. The final-checks record lists their explicit
**post-run** hashes; it does not retrofit a before/after snapshot. Root will
include those imports in the main integration manifest. Production and fixture
source maps and retained per-case binaries/traces were verified independently.

Reproduction uses `wine_lock.py` around every Wine-executing runner, first
with `X3M_FIXTURE_BOTTLE=Steam`, then `X3M_FIXTURE_BOTTLE=X3`:
`run_motion_output.py`, `run_temporal_pass.py`, `run_scene_capture.py`,
`run_ownership_integration.py`, and `run_exposure_statistics.py`. The shader
generator's `--check` also runs under the lock (its compiler bottle is Steam).
The game guard was clear throughout; no game was launched or bottle install
performed by this branch. Native Windows behavior and the new exposure
policy's game appearance remain unverified.

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

### Gate (every AgX run, updated for the tile meter)

`hdr_tonemap … tonemap=1 tonemap_reason=ok` and, with auto exposure,
`meter=1 meter_reason=ok chain_target=00000000 chain_sampling=00000000`
with `chain_format=G32R32F` in both tested bottles;
the self test's stage-2 checks `tonemap=00000000 tonemap_errors=0
meter=00000000 meter_errors=0 meter_value=6.00000 meter_expected=6.00000`
and a matching maximum-channel result of 6 log2 units
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

### Space-aware exposure (`hdrexposure`, 120 frames, fixed `dt`)

This replaces the historical 40-frame/1×1-meter exposure acceptance script.
The three cases pass in both complete Steam and X3 suites after the fixture
corrections below:
`seam-hdr-exposure`, `seam-hdr-exposure-offset` and
`seam-ownership-hdr-exposure`, each **245 checks / 120 frames**. The offset
twin uses `dt` 33 ms, EV offset +1, adaptation τ 0.2/0.6 s and the golden
look; the default and ownership twins use 16 ms and the default parameters.
Each full suite records PASS with fresh source and binary provenance.

Frames 0–39 retain the dark/bright/sun-block and exceptional-value controls.
Frames 40–119 add a small lit object against black sky, a full white image,
bright sparks, grey requesting the +2 clamp, levels that exercise the held
target dead band, and a central grey object overlapping a white edge emitter.
The runner rebuilds each original image from the logged rectangles, including
last-draw ownership where patches overlap. The independent Python chain and
statistic reference checks lit count, median, weighted mean, p99 maximum,
fresh target and adaptation, then compares presented RGB/alpha to AgX at
the consumed EV. The exposure stimuli are original synthetic rectangles;
local shader qualification inputs remain untracked.

The 64×64 target reduces in one draw to 16×16 G32R32F tiles; two ring
targets plus two readback surfaces account for **8,192 bytes**. Across all six
exposure cases, maximum statistic disagreement is **0.002585 log2
units**, reflecting the FP16 input conversion. Recomputed fresh-target
error is at most **4.82e-6 EV**, adaptation replay error **2.94e-6 EV**,
held-target replay error zero at log precision, and presented RGB error
**0.568 code**; alpha stays within one code. Infinite/NaN input arithmetic
remains a recorded backend observation, not a cross-platform shader guarantee.
Finite host state and in-range consumed statistics are required in those frames.

Representative default-case targets (fresh rule, before adaptation):

| Scene | Measured result |
| --- | --- |
| Lit object against sky | 16/256 lit tiles; key +1.349 EV, limit +7.697 EV |
| Full white image | Key −0.618 EV from quarter-strength darkening |
| Sparks | P99 maximum 6 log2; limit −2.126 EV selects the target |
| Grey | Key +2.970 EV, clamped to +2 EV |
| Small level changes | Held +0.598 EV while fresh key moves +0.435 / +0.683 EV |
| Larger change | Held target moves to −0.213 EV |
| Central object plus emitter | Weighted key about −0.00057 EV; unweighted emitter would request −0.618 EV |

The final emitter's held target remains −0.213 EV because the new key lies
inside its dead band; the fixture proves the statistic, not instant convergence
to zero EV. The clamp and adaptation likewise prevent treating the p99-derived
limit as an absolute bound on every currently displayed highlight.

Three fixture/validator corrections were needed before this pass. A patch
centre covered by a later draw is not a valid color witness: each region now
requires an actually visible sample and every pixel is checked against its
final geometric owner. The large target-move equality compares distinct held
and fresh values serialized at the same precision. Finally the level inputs
are .38/.40/.37/.6, so the first level leaves the previous +2 clamp even in
the +1-offset twin, two levels stay inside the band, and the last leaves it.
Both independent reference margins and observed transitions are asserted.
These corrections did not change production exposure behavior.

The [offline run-16 counterfactual](run16-exposure-baseline.md) used seven unresolved
`hdr_` images, not the actual post-TAA meter input. All seven new-policy
targets hit the +2 cap: lit-key requests were +4.04…+5.22 EV and ceiling
requests +4.16…+4.49 EV, versus +5.66…+8 for the old whole-image rule.
That sample is therefore controlled by the cap; it cannot establish tuning
or appearance of the live lit-key/ceiling policy. Keep the reviewed policy
unchanged until actual resolved captures can be compared with the user's
preferred fixed-EV-zero appearance.

### Tonemap ladder (`seam-hdr-tonemap-fault`, 59 checks, 9 frames)

| frame | fault | `hdr_frame` | presented (vs the FP16 readback) |
| ---: | --- | --- | --- |
| 0 | none | `tonemapped=1 unwind=0 meter=00000000` | AgX |
| 1 | 11: the tonemap draw fails | `tonemapped=0 unwind=1 unwind_reason=tonemap fallback=1 writeback_source=shader tonemap_draw=80004005` (`hdr_unwind=tonemap … draw=00000000 restore=00000000`) | identity, max 0.5 code |
| 2 | 15: readback unlock reports failure after actual cleanup | `recheck=pass readback=80004005 stepped=0`; all exposure state held | AgX at held EV |
| 3 | 13: the meter chain fails | `tonemapped=1 unwind=0 meter=80004005` | AgX |
| 4 | none | `stepped=0`, EV equal to frame 3's (the exposure held) | AgX, max 0.5 code |
| 5, 6 | 11 twice | two more `hdr_unwind=tonemap`; `hdr_tonemap_disabled … reason=draw_failures` at frame 6 (the third failure) | identity |
| 7, 8 | none | `recheck=pass` at 7, then `tonemap=identity tonemapped=0 meter=00000001 stepped=0` | identity, max 0.5 code |

The updated targeted ladder passes 59 checks / 9 frames. Frame 3 must
resume adaptation after frame 2’s failed readback; its meter-chain failure
then makes frame 4 hold. A separate attach-time fault-16 case passes
23 checks / 3 frames: correct meter pixels with failed unlock produce
`meter=0 meter_reason=self_test`, while AgX output continues with EV zero.
No failed readback can publish a new exposure state.

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

## Stage 3: TAA on HDR (2026-09-12)

Mechanism, weighting and the derivation of `k` in
[temporal-integration.md](../architecture/temporal-integration.md#stage-3-of-the-hdr-scene-path-taa-on-hdr-2026-09-12);
the resolve's own cases (k = 0 identity, firefly, gradient, edge) in
[temporal-resolve.md](temporal-resolve.md#stage-3-of-the-hdr-scene-path-luminance-weighting-2026-09-12).
Everything below is `run_motion_output.py` on the synthetic 64×64 scene,
seam DLL unless stated; nothing is gameplay-verified.

### Case 6: the presented frame is tonemap(resolve(HDR))

The fixture's reference `TemporalPass` (second device, system d3d9) now
receives the FP16 scene read through the seam before the boundary (the
same bytes the DLL resolves) and the `k` the DLL derived at the latch
(exposure export); the DLL's resolved FP16 image (`taa_1_<frame>.rgba16f`)
must equal the reference's output byte for byte in every frame, and the
presented 8-bit frame is compared against the AgX reference
(`agx_reference.tonemap_engine`) of that resolved image at the consumed EV.

| Case | k per frame | max / mean code error vs AgX(resolved) | history frames | fixture 8-bit compare |
| --- | --- | ---: | --- | --- |
| `seam-taa-hdr-tonemap-on` (manual EV 0) | 1.0 | 0.500 / 0.269 | 1, 2, 4, 7 | alpha exact |
| `seam-taa-hdr-tonemap-ev1` (manual EV 1) | 2.0 | 0.500 / 0.240 | 1, 2, 4, 7 | alpha exact |
| `seam-taa-hdr-tonemap-auto` (auto exposure, dt 16 ms) | 1.084 → 1.626 (adapting) | 0.500 / 0.286 | 1, 2, 4, 7 | alpha exact |
| `seam-taa-hdr-tonemap-k0` (`X3M_FIXTURE_TAA_K=0`, seam only since 2026-09-25; was `X3M_TAA_K=0`) | 0.0 | 0.500 / 0.269 | 1, 2, 4, 7 | alpha exact |
| `seam-ownership-taa-hdr-tonemap-on` (wrapper) | 1.0 | 0.500 / 0.269 | 1, 2, 4, 7 | alpha exact; zero final references |
| `seam-taa-hook-hdr-tonemap-on` (engine hook, glow on/off frames) | 1.0 | — (hook validator: FP16 byte-exact, presented per fixture) | 6 of 7 | alpha exact |
| `production-taa-hdr-tonemap-on` | 1.0 | — (no seam: current-only, hdr/taa lines checked) | — | — |

A maximum error of 0.500 code is the rounding of the reference itself (the
comparison is against the unrounded 255·AgX value): the tonemap draw of the
resolved image reproduces the double-precision reference to the last bit
that an 8-bit code can hold, in every frame, at every `k`. The frame lines
carry `taa_hdr=1 taa_copy=00000001` (no copy-back) and `taa_k`, the
`hdr_frame` lines `k=` equal to `exp2(ev)` (the fixture seam's `X3M_FIXTURE_TAA_K` overriding; production `X3M_TAA_K` removed 2026-09-25), the
history pattern is the seam script's (frames 1, 2, 4, 7: the same as on the
8-bit path). `k` under auto exposure follows the adapted EV frame by frame
(1.084, 1.171, 1.261, 1.354, 1.394, 1.491, 1.591, 1.626) while the resolved
image stays byte-exact against a reference fed the same `k`: the weighting
tracks exposure with the history left in engine radiance.

### The k = 0 identity in the live route (the stage-1 TAA twins)

The stage-1 twins with TAA (`production-taa-hdr-on`, `seam-taa-hdr-on`,
`seam-taa-hook-hdr-on`, `seam-taa-envmap-hdr`) now resolve on the FP16
scene with the identity write-back (`k = 0`). Their resolved FP16 images
equal the reference pass fed the FP16 scene byte for byte (every frame,
`taa_reference_frames` 12 / 7), their RT1/RT2 readbacks are identical to
the twins', and their presented frames stay within **one code** of the
8-bit twins with background, flat and alpha exact — but fewer pixels are
exact than in the TAA-off twins: seam script 97.6% (1,163 of 27,421
material pixels over 12 frames, 6 with history), hook script 92.4% (2,172
of 18,714 over 7 frames, 6 with history), against ≥ 98% / ≤ 2% for the
TAA-off twins. Cause, by construction: the 8-bit route re-quantizes its
history through the 8-bit copy-back every frame, the HDR route accumulates
unquantized FP16 values, and the two histories drift by up to half a code
before the final rounding, so a fraction of the material pixels that grows
with the number of history frames lands one code apart. The runner's TAA
twin acceptance is therefore ≤ 1 code, background/flat/alpha exact, ≥ 90%
exact, ≤ 15% of material pixels (`HDR_TWIN_*_TAA`); the stage-1 TAA-off
bounds are unchanged.

### Failure, unwind and Reset (`seam-taa-hdr-tonemap-fault`, 132 checks, 12 frames, 1 Reset)

Script `0, 14, 0, 11, 0, 13, 0, 14, 0`, Reset, `0, 0, 0` (fault 14 = the
resolve on the FP16 scene fails, `HdrFault::Resolve`, consumed before the
pass runs). Frames 1 and 7: `taa_resolved=0 taa_result=80004005`,
`motion_output_taa_failed … hdr=1`, the write-back presents the unresolved
scene (the presented frame equals the AgX reference of the FP16 readback
of the unresolved scene within one code; the fixture's "unchanged" check
holds because the flush before the boundary wrote the same image) and the
history drops: frames 2 and 8 resolve current-only, 3–6 accumulate again.
Frame 3 (fault 11): the tonemap draw failure is consumed by the flush the
fixture's pre-boundary read triggers, the identity fallback lands there
(`hdr_unwind=tonemap`, one line), the end write-back of the same frame
tonemaps the resolved image again (`tonemapped=1`), the recovery self test
passes at frame 4's latch. Frame 5 (fault 13): the flush's meter chain
fails, the end's succeeds (`meter=00000000`), no visible effect. After the
Reset (`motion_output_reset … 00000000`, `RESET PASS`, a second
`hdr_target` creation) frame 9 resolves current-only on the re-created
target and 10–11 accumulate; every presented frame with a readback is
within one code of its reference.

### Cost (bench, 24 frames, 20 timed, EVENT-synchronized QPC, CPU-inclusive; median / min ms)

Three configurations per size: the 8-bit route (stage-1 twins' reference),
the FP16 path with the identity write-back (`k = 0`), and the FP16 path
with AgX, auto exposure (chain every frame) — each with the resolve off
and on.

| Size | 8-bit, TAA off | 8-bit, TAA on | HDR identity, TAA off | HDR identity, TAA on | HDR AgX + meter, TAA off | HDR AgX + meter, TAA on |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1280×768 | 0.356 / 0.263 | 0.699 / 0.679 | 0.380 / 0.358 | 1.268 / 0.781 | 0.831 / 0.706 | 1.377 / 1.244 |
| 5120×1440 | 0.629 / 0.484 | 2.248 / 2.222 | 0.866 / 0.841 | 1.920 / 1.872 | 1.448 / 1.407 | 2.301 / 2.194 |

The table is the tracked run (`motion-output-summary.json`). Two earlier
complete runs of the same suite the same day gave, in the same column
order, medians 0.328 / 0.726 / 0.413 / 0.753 / 0.813 / 1.263 and 0.328 /
0.760 / 0.419 / 0.745 / 0.803 / 1.227 at 1280×768 and 0.570 / 2.250 /
0.890 / 1.915 / 1.479 / 2.356 and 0.563 / 2.204 / 0.864 / 1.923 / 1.546 /
2.325 at 5120×1440: every cell agrees within about 0.1 ms except the
tracked run's 1280×768 HDR-identity TAA-on median (1.268 against 0.753
and 0.745; its minimum 0.781 against 0.639 / 0.649 is in line), a
20-sample outlier of the kind review 23 met. Resolve increments (on − off,
median, tracked run / the two earlier runs): 8-bit +0.34 / +0.40 / +0.43
ms at 1280×768 and +1.62 / +1.68 / +1.64 ms at 5120×1440; FP16 identity
(+0.89 outlier) / +0.34 / +0.33 and +1.05 / +1.03 / +1.06; FP16 AgX +0.55 /
+0.45 / +0.42 and +0.85 / +0.88 / +0.78. At 5120×1440 the HDR resolve — no
8-bit→FP16 input copy, no FP16→8-bit copy-back, 535 more shader words — is
**cheaper than the 8-bit resolve** in all three runs: 1.920 / 1.915 / 1.923
ms against 2.248 / 2.250 / 2.204 ms for the whole boundary with the
identity write-back (about −0.3 ms), and the AgX + meter boundary with TAA
(2.301 / 2.356 / 2.325 ms) costs about what the 8-bit TAA boundary did.
HDR + TAA over HDR-only at 5120×1440: +1.05 ms (identity), +0.85 ms (AgX).

**Stage-2 re-measure** (review 23, observation 9: 2.653 vs 0.936 ms at
5120×1440 with the resolve off). Tracked run: AgX + meter 1.448 / 1.407 ms
against identity 0.866 / 0.841 ms — **+0.582 median / +0.566 min** — and
with the resolve on 2.301 / 2.194 against 1.920 / 1.872, +0.381 / +0.322;
the two earlier runs gave +0.589 / +0.578 and +0.682 / +0.480 (off), +0.441
/ +0.422 and +0.402 / +0.339 (on). All three reproduce the stage-2 record
(+0.500 / +0.475 and +0.227 / +0.302) within 0.1–0.2 ms; the +1.72 ms
figure did not recur in three runs and stands as an outlier of that
20-sample run. The chain is the whole increment and it is nearly
size-independent (+0.45 at 1280×768 with the resolve off), so it is the
per-draw overhead of six small draws, not bandwidth. Proposal, not
implemented (it is not trivial: a 64-tap reduce program, its generator
entry, the self-test expectation and the chain-level bookkeeping change):
an 8×-per-axis chain — 1280×768 → 160×96 → 20×12 → 3×2 → 1 (four draws
instead of six, 64 taps each; 5120×1440 → 640×180 → 80×23 → 10×3 → 2×1 →
1, five instead of seven) — saves a third of the draws for the same reads;
the exposure reference's `reduce_chain` takes the factor as a parameter.

### Other suites after the stage-3 change (same day)

`run_motion_output.py` PASS (90 runs: 78 cases and 12 benches; the eight
stage-3 cases above added to stage 2's list), `run_temporal_pass.py` 448 /
204 / 386 (the k = 0 identity first: 416 / 164 / 386, reports byte-identical;
444 / 196 before review 24's negative-channel case),
`temporal_run.py` 78 / 78, `run_ownership_integration.py` PASS (every
environment exit 0), `run_scene_capture.py` PASS (4,908 checks),
`check_no_x87.py build/d3d9.dll` PASS (129 reachable functions, no
violation), `generate_rigid_motion_pixel.py --check` PASS (the resolve at
4,487 words after review 24, 4,375 before; the six other bytecodes
unchanged, their provenance carrying the edited generator's hash), `unittest discover -s verification/analysis`
OK (588 tests).

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

## Display dither (`X3M_HDR_DITHER`, 2026-09-24)

Cause (Run 77 C2, `verification/results/run291-293-rings/`, measured there):
the AgX write-back stored `saturate(v)` into the A8R8G8B8 target undithered;
the fogged sky's FP16 gradient is 0-1 FP16 ulp per pixel (one ulp = 1/22 of an
8-bit code), so the store drew contour lines 14-15 px apart (fog march scale 2)
that the auto exposure (|Δev| p50 0.004-0.007 per frame) moved 4-6 px per
0.01 EV: the moving rings.

Change: `src/temporal/display_dither.hlsl` adds a static ±0.5 code offset
(interleaved gradient noise of `floor(VPOS) + 0.5`, one offset for the three
channels) to the saturated display value and saturates again; alpha untouched.
Static, not per-frame: the write follows TAA, so a varying pattern would
flicker by one code at rest. Every write of the FP16 image into the 8-bit
target takes it: `agx.hlsl` and `agx_sharpen_ps.hlsl` (c8.z),
`bloom_agx_ps.hlsl` (c8.z; its FP16 staging write gets 0 and the following
`taa_sharpen_ps.hlsl` applies it via c23.w), `taa_sharpen_ps.hlsl` (c23.w: the
HDR identity+RCAS write-back; the 8-bit route keeps 0) and the new
`hdr_writeback_dither_ps.hlsl` (the identity write-back, amplitude fixed). The
plain identity copy stays the self test's and every 8-bit-to-8-bit copy's
program; the self tests upload c8.z = 0. At amplitude 0 the store equals the
former one mathematically (inputs already in [0,1]); the dither-off cases
reproduce the recorded figures. DLL: `X3M_HDR_DITHER=1|on`, off when unset; launcher `--hdr-dither on|off`,
default on with `--hdr`, always exported. `hdr_tonemap` log line: `dither=
dither_reason= dither_shader=`.

Instruction slots (D3DX `D3DXDisassembleShader` of the embedded programs,
measured; `verification/results/hdr-display-dither/measure.py bins|slots`):

| program | before | after | ps_3_0 guaranteed |
| --- | ---: | ---: | ---: |
| `hdr_tonemap` (agx.hlsl) | 66 | 76 | 512 |
| `hdr_tonemap_sharpen` | 392 | 402 | 512 |
| `taa_sharpen` | 91 | 101 | 512 |
| `bloom_agx` | 105 | 115 | 512 |
| `hdr_writeback` (unchanged) | 1 | 1 | 512 |
| `hdr_writeback_dither` (new) | – | 13 | 512 |

Cost: 10 arithmetic slots per presented pixel, no texture (inferred < 0.1 ms
at 5120×1440; not timed).

CPU reference (`agx_reference.dither_noise/dither_display/store_code`,
`test_agx_reference.DitherTests`, measured): per pixel the stored code is
`floor(255 c + noise)`; a constant input stores at most two adjacent codes and
the mean code equals `255 v` within 0.00055 code (128×128) and 0.00029 code
(1920×1080) over seven fractions (`measure.py reference`); black and white
stay exact.

Wine fixture (`run_motion_output.py`, selected cases, PARTIAL by design, X3
bottle, measured; the per-case figures, RESULT lines and twin history
equality are kept in `verification/results/hdr-display-dither/motion-output-cases-2026-09-24.json`
by `measure.py keep` and printed by `measure.py fixture` into
`fixture-2026-09-24.txt`; same figures in two runs, before and after the
review fixes):

| case | result |
| --- | --- |
| `seam-hdr-ramp-none`, `-identity`, `seam-taa-sharpen-on`, `seam-taa-hdr-sharpen-on`, `seam-taa-hdr-tonemap-sharpen-on` (dither off) | reproduce the recorded figures: ramp max 0.498 / 0.497 code, mean 0.183 / 0.082; sharpen max 0.498 / 0.500 / 0.500, mean 0.088 / 0.326 / 0.385 |
| `seam-hdr-ramp-dither` (AgX) | every pixel of the 780 cells against reference + pattern: max 0.500, mean 0.201 code; 99.99 % of pixel-channels equal the double-precision prediction; mean signed error against the undithered reference −0.002 code; 180 of 260 cells non-uniform; frames 1 and 2 give equal per-channel statistics (the runner asserts it) |
| `seam-hdr-ramp-identity-dither` | max 0.500, mean 0.163; 100 % exact prediction; signed mean −0.0006; 148 cells non-uniform |
| `seam-taa-hdr-sharpen-dither` (identity+RCAS, c23.w) | max 0.500, mean 0.353 against RCAS + pattern; 0 channels outside the 3×3 bound widened by one code; 100 % exact; FP16 history files identical to `seam-taa-hdr-sharpen-on` (8 of 8) |
| `seam-taa-hdr-tonemap-sharpen-dither` (AgX+RCAS, c8.z) | max 0.500, mean 0.373; 0 outside; ≥ 99.98 % exact; history identical to `seam-taa-hdr-tonemap-sharpen-on` (8 of 8) |

The fixture skips its raster-colour coverage oracle on dithered frames, as on
AgX frames.

Bloom candidate (`run_bloom_pass.py`, X3 bottle, measured; input format
`X3BP0004` carries the per-case c8.z; `measure.py bloom` →
`verification/results/hdr-display-dither/bloom-pass-2026-09-24.json`): PASS,
47 images, max 1 code against the fused reference (bound 3 unchanged). The 45
existing cases (c8.z = 0) keep max 1 code. Two new cases, the constant 8×6
gamma2.2 image with bloom 0.5 and c8.z = 1/255, the oracle adding the same
pattern once after RCAS:

| case | max / mean code error | mean signed error vs undithered oracle | codes per channel (R/G/B) | max vs undithered |
| --- | --- | ---: | --- | ---: |
| 45, unsharpened (direct A8R8G8B8 draw dithers) | 1 / 0.007 | −0.026 | 1 / 2 / 2 | 0.83 |
| 46, sharpen 0.75 (FP16 staging gets 0, `taa_sharpen` c23.w dithers) | 1 / 0.063 | −0.082 | 1 / 2 / 2 | 0.83 |

(R saturates at 255, so one code.) Not covered: native Windows
(cross-compiled) and the in-game rings (needs a user flight at 5120×1440 with
`--hdr-dither on` and `off`).

**In flight 2026-09-24 (Run 78 A, run295 dither on vs run296 `--hdr-dither off`, 5120x1440, same fog spot as the run291
rings):** the user sees the moving rings gone with the dither on and present with it off. Modelled 8-bit sky from the
pre-tonemap bursts ([dither_contours.py](../../verification/results/run295-298-run78a/dither_contours.py)): pixels equal to
their right neighbour 91.4-96.8 % off vs 49.9-50.7 % on; run length of equal codes p90 20-47 px vs 4 px; low-frequency
banding p99 0.12-0.27 codes vs 0.021-0.026 codes (about 10x lower). Frame time p50/p95 18.91/21.19 ms on vs 18.98/21.31
off (no measurable cost). Logs carry `hdr_tonemap dither=1 dither_reason=ok` / `dither=0 dither_reason=off`. Accepted;
`--hdr-dither on` stays the default.

**Ctrl+Shift+F9 / F10 removed 2026-09-26** (user decision; `docs/architecture/comparison-hotkeys.md`, "Removed 2026-09-26"): exposure follows `--hdr-exposure`, bloom runs at full strength; the notice, the `renderer_comparison` rows, `MotionOutput::comparison_toggle_exposure` and `HdrPass::comparison_exposure` are gone (`HdrConfig::allow_auto_toggle` stays, so a fixed-EV launch still prepares the meter).
