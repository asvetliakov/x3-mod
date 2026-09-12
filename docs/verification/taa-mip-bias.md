# Mip LOD bias of the routed material draws (`X3M_TAA_MIP_BIAS`)

Fixture evidence for the sampler half of the TAA blur fix
([temporal-integration.md](../architecture/temporal-integration.md#mip-lod-bias-for-routed-material-draws-2026-09-12)):
the route applies `D3DSAMP_MIPMAPLODBIAS` to the mip-mapped stages of routed
draws while the jitter is on and puts it back at every restore point. All
runs are `verification/probe/run_motion_output.py` on the synthetic device
program under CrossOver Preview Wine (bottle per `X3M_FIXTURE_BOTTLE`), no
game launch; nothing here is gameplay validation.

## Cases

Ten runs added to the motion-output suite (the whole suite still passes;
counts in [motion-output.md](motion-output.md)):

| Case | Script | DLL | `X3M_TAA_MIP_BIAS` | Proves |
| --- | --- | --- | --- | --- |
| `production-mipbias-off` | mipbias | production | unset | the script's own control: identical routed/unrouted images, no bias anywhere, zero counters |
| `production-mipbias-zero` | mipbias | production | `0` | byte-identical to unset (colour hashes, evidence, counters, readback files) |
| `production-mipbias-on` | mipbias | production | `-0.5` | the bias on exactly stages 0 and 4, every restore point, the counts, the evidence |
| `production-mipbias-on1` | mipbias | production | `-1.0` | the evidence delta doubles (linearity in the LOD) |
| `seam-mipbias-on` | mipbias | seam | `-0.5` | same as production (colour, evidence, counts identical) with the motion/depth oracle live |
| `seam-mipbias-lazy-on` | mipbias | seam, lazy RT mode | `-0.5` | the restore points shared with the lazy binding (identical to per-draw) |
| `seam-taa-mipbias-on` | regular, TAA | seam | `-0.5` | twin of `seam-taa-on`: no mip-mapped texture bound, so nothing changes with the hooks installed |
| `production-taa-mipbias-on` | regular, TAA | production | `-0.5` | twin of `production-taa-on` |
| `seam-taa-lazy-mipbias-on` | regular, TAA, lazy | seam | `-0.5` | twin of `seam-taa-lazy-on` |
| `seam-jitter-mipbias-zero` | regular, jitter | seam | `0` | twin of `seam-jitter-on` |

## The `mipbias` script (`motion_output_fixture.cpp`, `run_mipbias`)

Eight frames, jitter on, capture diagnostics in frame 5 only, a `Reset`
after frame 3. Every frame binds a 1024×1024 texture with a complete
eleven-level chain whose level `i` is the constant grey `16 + 20 i` (the LOD
ramp; object A maps one repeat over 128 raster pixels, 8 texels per pixel,
LOD 3 — at 128 texels the LOD would be 0, where a negative bias is clamped
away and changes nothing) on stage 0 (`MIPFILTER LINEAR`) and stage 4
(`POINT`), the same
texture with `MIPFILTER NONE` on stage 1, an unmipped 2×2 texture with
`LINEAR` on stage 2, the unmipped cube on stage 3 and nothing on stage 5
(`LINEAR`), then reads `MIPMAPLODBIAS` of stages 0–7 back through
`GetSamplerState` (not hooked; the game never reads sampler state) after
every step:

| Step | Draw | Expected bias mask (stages) |
| --- | --- | ---: |
| before any routed draw | — | none |
| routed A, routed B | material pair, sentinel-only | `0x11` (0 and 4) |
| flat A (gate 3) | not routed | none (restored) |
| routed A | | `0x11` |
| stage 4 rebound to the unmipped texture, routed B | | `0x01` (stage 4 restored) |
| stage 0 `MIPFILTER NONE`, routed A | | none |
| stage 0 `LINEAR` and stage 4 ramp again, routed B | | `0x11` |
| application writes `MIPMAPLODBIAS` 0.25 on stage 0 | | `0x10`; stage 0 reads 0.25 |
| routed A | | `0x11` (re-applied over the application's value) |
| flat A | | none; stage 0 reads 0.25 (the application's value is what the restore puts back) |
| application writes 0 on stage 0, routed B | | `0x11` |
| even frames: evidence pair (below), then routed A | | `0x11` |
| `EndScene`, `Present` | | none |

Stages 1, 2, 3 and 5 never carry the bias in any step (the mask covers
stages 0–7). Without the bias (`unset`, `0`, or the route off) every step
must read 0 except the application's own 0.25.

**Evidence pair (even frames).** The same material draw of A, first routed
(biased) and read back, then unrouted (gate 4: alpha blending on with the
`ONE`/`ZERO` factors, so the colour is the unblended material) and read
back, both at the frame's jitter. Over A's interior pixels the fixture
prints the count of differing pixels and the mean RGB of both images
(`MIPBIAS_EVIDENCE`). The ramp's trilinear sample is linear in the LOD, so
a bias of −0.5 moves it half a level toward the darker fine levels and −1.0
a whole level: the runner requires the −1.0 delta to be 1.75–2.25× the −0.5
delta, and the unbiased runs to show zero differing pixels.

**Counts.** The fixture models the DLL's restore points (unrouted draw,
`EndScene`/`Present`, capture-frame diagnostics, the lazy mode's
`GetRenderTargetData`) and prints the sets/restores/draws/reads it expects
per frame (`MIPBIAS_EXPECT`); the runner compares them with the DLL's
per-frame line and with hand-derived anchors: odd frames 9 sets / 8
restores, even frames 11 / 10 (the evidence pair), the capture frame 15 /
14 (every routed draw re-sets after the diagnostics' restore). The saved
value is read natively twice per session start (stages 0 and 4) and twice
again after the `Reset` (the shadow is dropped), never otherwise; the
application's two writes per frame are counted (`mip_bias_game_writes`),
logged (`motion_output_mip_bias_game_write`, capped at 16 lines) and
totalled (`motion_output_mip_bias_summary`).

## Results (2026-09-12, `verification/results/motion-output-summary.json`, key `mip_bias`)

**Where the bias sits.** In the three `-0.5` runs and the `-1.0` run every
one of the 14 `MIPBIAS` verdicts per frame (112 per run) reads the expected
mask: `0x11` after each routed draw, `0x00` after the flat draw, at the
frame start and after `Present`, `0x01` with stage 4 rebound to the
unmipped texture, `0x00` with stage 0's `MIPFILTER` at `NONE`, `0x10` right
after the application's own write on stage 0, and the application's 0.25
is what the following restore leaves on stage 0. Stages 1 (mip chain,
`NONE`), 2 (unmipped, `LINEAR`), 3 (cube), 5 (unbound, `LINEAR`), 6 and 7
never read anything but 0 (`nonzero` ⊆ `{0, 4}` in every verdict). In the
unset and `0` runs every verdict reads 0 except the application's 0.25.

**Counts** (`-0.5`, production DLL; the seam DLL and the lazy RT mode report
the same numbers frame for frame, and the fixture's model agrees with the
DLL in every frame):

| Frame | 0 | 1 | 2 | 3 | 4 | 5 (capture) | 6 | 7 | session |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `mip_bias_sets` | 11 | 9 | 11 | 9 | 11 | 15 | 11 | 9 | 86 |
| `mip_bias_restores` | 10 | 8 | 10 | 8 | 10 | 14 | 10 | 8 | 78 |
| `mip_bias_draws` | 9 | 8 | 9 | 8 | 9 | 8 | 9 | 8 | — |
| `mip_bias_reads` | 2 | 0 | 0 | 0 | 2 (after `Reset`) | 0 | 0 | 0 | 4 |
| `mip_bias_game_writes` | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 16 |

`mip_bias_stages = 0x0011` and `mip_bias_biased_now = 0` on every frame
line, `mip_bias_failures = 0`, 16 `motion_output_mip_bias_game_write` lines
(0.25 and 0 alternating), one `motion_output_mip_bias_summary` line. Route
counters per frame: odd frames 12 draws / 9 routed (gate 2: 1, gate 3: 2,
gate 5: 9), even frames 14 / 10 (plus the gate-4 evidence draw); no apply or
restore failure. Unset versus `0`: colour hashes, evidence, counters, check
counts (165) and the capture-frame readback files identical.

**Mip selection evidence** (mean RGB over A's 2,457 interior pixels — 2,394
in frame 6 — of the same material draw, unrouted versus routed; the ramp
level `i` is grey `16 + 20 i`, LOD 3 unbiased):

| Run | unrouted mean | routed mean | delta (codes) | differing pixels |
| --- | ---: | ---: | ---: | ---: |
| unset / `0` | 31.477 | 31.477 | 0.000 | 0 of 2,457 |
| `-0.5` | 31.477 | 28.378 | 3.099 | 2,457 of 2,457 |
| `-1.0` | 31.477 | 25.575 | 5.902 | 2,457 of 2,457 |

The `-1.0` delta is 1.904–1.906× the `-0.5` delta in each of the four
evidence frames: the routed draw's sample moved half a level toward the
finer, darker levels at `-0.5` and a full level at `-1.0` (the material
output is affine in the diffuse sample: 10 and 20 ramp codes at the sampler
scale to 3.1 and 5.9 presented codes through the lighting terms). The
presented colour of the `-0.5` run differs from the unbiased run in all
eight frames; the production DLL, the seam DLL and the lazy RT mode produce
identical colour hashes and evidence.

**Twins** (`seam-taa-mipbias-on`, `production-taa-mipbias-on`,
`seam-taa-lazy-mipbias-on`, `seam-jitter-mipbias-zero`): colour hashes,
checks, restorations, motion/matched/depth pixels, pre-boundary colour and
readback files identical to `seam-taa-on`, `production-taa-on`,
`seam-taa-lazy-on` and `seam-jitter-on`; their frame lines report
`mip_bias=-0.5` (or `0`) with every count at 0 and the session summary at 0.

**Suite.** The whole motion-output suite (88 cases plus 18 bench runs,
cross-case comparisons included) passes with the ten cases added; scene
capture (4,908 checks), ownership integration (26 runs), `check_no_x87.py`
(nine light hooks, `set_texture` and `set_sampler_state` added, 135
reachable functions, no violation) and the analysis unit tests (712) pass.

## Limits

- Synthetic device program on a 64×64 target; the game's material samplers
  (`ANISOTROPIC` 16× over DXT chains) are represented by `POINT`/`LINEAR`
  minification over an A8R8G8B8 ramp, so the fill-rate cost of the bias
  under anisotropic minification is not measured here.
- The restore before `Reset` is exercised but not separately observable:
  `EndScene` precedes `Reset` and is itself a restore point; the post-Reset
  re-read of the saved values (`mip_bias_reads = 2` in frame 4) is what the
  runner checks.
- `GetSamplerState` is not hooked: an application read between a routed draw
  and the next restore point would see the route's bias. The game never reads
  sampler state (its state manager keeps its own shadow); the fixture relies
  on this to observe the device.
- The capture's own `sampler … state=8 bias=` lines show the application's
  value, because capture frames restore before every draw's diagnostics.
- CrossOver Preview builtin D3D9 only; Windows is cross-compiled, not verified.
