# Iteration 9 run 4: the FP16 HDR scene path in gameplay (stage 1)

First gameplay use of the FP16 HDR scene path
([architecture](../architecture/hdr-scene-path.md), "Stage 1 implementation";
synthetic evidence in [hdr-scene-path verification](../verification/hdr-scene-path.md)). The
build is the stage-2 binary with the tonemap left at its default, so what ran
is the **stage-1 topology with the identity write-back**: own
`A16B16G16R16F` target, redirect at the latching `Clear`, write-back at the
engine scene-end hook, then the 8-bit TAA resolve. No agent launched the game
and no `src/` change was made for this report.

**Verdict.** The redirect is healthy: 3,888 latches, one write-back each, zero
unwinds, zero rechecks, zero suspensions, nothing pending at Present, no
Reset. The write-back is **bit-exact** on all 16 captured frames — every one of
15,728,640 pixels, all four channels — so the identity tonemap adds no error of
its own in the live route. The FP16 target holds almost no headroom above 1.0
today (0.08% of pixels over all frames, 70% of those below 1.25, maximum 3.26
in one pixel), which is the honest bound on what stage 4 can change. HDR costs
74 µs per ordinary frame (CPU-inclusive), 0.24% of a 31 ms frame. Nothing found
here blocks run 5; §7 lists what to watch.

## Provenance

| | run 4 |
| --- | --- |
| log | `session-20260912-165021-2204.log`, 152,719,267 B, 2,777,786 lines |
| sha256 | `c739e095d2accce5098258fb66d9c71567b25fffbc79000e37709f30647fa28d` |
| installed DLL | `db63e120afcbb38e1382f22dffb96fb6030bbab50d1c3faf2b5587e055ce7e3d` (`1d36c29`) |
| flags | `--direct --ownership --object-trace --object-lifetime --motion-output --taa --taa-debug --telemetry --scene-hook --hdr` |
| mode witness | `taa=1 taa_debug=1 jitter=1 jitter_samples=8 rt_mode=perdraw frame_log=60 sentinel=auto camera_cut_deg=20.00 state_shadow=1 scene_hook=1 hdr=1` |
| tonemap witness | `hdr_tonemap ... requested=identity tonemap=0 tonemap_reason=off meter=0 meter_reason=off decode=gamma2.2 exposure=auto` |
| captured frames | 16 (four bursts of 4), 80 readbacks (16 each of `hdr` FP16, `color` 8-bit, `taa` FP16, `depth` R32F, `motion` RGBA32F), 1280×768, every `result=00000000` |
| path | start → menu (two alt-tabs) → load save → flight → sector change → menu |

Baselines: run 2 `session-20260912-162050-2040.log` (same route and TAA,
`hdr=0`, but **also** `--profile` and `--mesh-cache`) and run 3
`session-20260912-164116-2548.log` (route off). Logs and readbacks stay
untracked.

### What the readback files are on this build

This matters for §2 and is specific to commit `1d36c29`
(`src/proxy/motion_output.cpp`):

| file | when it is taken | what it holds |
| --- | --- | --- |
| `hdr_1_<f>.rgba16f` | in `hdr_writeback`, **before** the frame's first write-back | the FP16 scene exactly as the game's shaders left it |
| `color_1_<f>.bgra8` | in `resolve`, which the scene-end hook calls **after** `end_redirect` | the A8R8G8B8 main target **after** the write-back — the identity tonemap of the file above, not an independently rendered 8-bit scene |
| `taa_1_<f>.rgba16f` | after `TemporalPass::run` | the resolved image, still produced from the 8-bit `color` input (stage 3 moves TAA onto the FP16 image) |

So with `--hdr` the `color` dump changes meaning: in run 2 it is the game's own
8-bit scene, in run 4 it is our write-back's output. §2 therefore compares
`color` against `hdr` of the same frame and never against run 2's `color`.

## Reproduction

```sh
L=/tmp/x3-iteration09-run4/session-20260912-165021-2204.log
C=/tmp/x3-iteration09-run4

# frame-time regimes and the route/HDR cost attribution (existing tool)
python3 tools/analysis/analyze_iteration09_cost.py \
  --run run4=$L --run run2=/tmp/x3-iteration09-run2/session-20260912-162050-2040.log \
  --run run3=/tmp/x3-iteration09-run3/session-20260912-164116-2548.log --baseline run3 \
  --json verification/results/iteration-09-run4-cost.json \
  --text verification/results/iteration-09-run4-cost.txt

# the HDR path, the identity check, the headroom, TAA, cost and focus/cursor
python3 tools/analysis/analyze_iteration09_run4.py --log $L --captures $C \
  --baseline /tmp/x3-iteration09-run2/session-20260912-162050-2040.log \
  --cost-json verification/results/iteration-09-run4-cost.json \
  --json verification/results/iteration-09-run4.json \
  --text verification/results/iteration-09-run4.txt

python3 verification/analysis/test_iteration09_run4.py   # 21 synthetic checks
```

## 1. HDR path health

`hdr_device device=1 enabled=1 reason=ok`: `A16B16G16R16F` as a render target,
with post-pixel-shader blending, with filtering and as a sampled texture all
`00000000`; `CheckDeviceFormatConversion(A16B16G16R16F → A8R8G8B8)`
`00000000`, so the emergency `StretchRect` rung exists; `main_format=21`
(A8R8G8B8, matching the windowed 1280×768 device with swap effect 1/discard);
`mrt_blending=1`; the attach self test passed on three formats
(`self_test_targets=3`, `scene_errors=0 sum_errors=0 motion_errors=0
depth_errors=0 copy_errors=0 stretch_errors=0`), including the emergency
`StretchRect` rung. The stage-2 fragments were **not** created — `hdr_tonemap
... tonemap=0 tonemap_reason=off meter=0 meter_reason=off tonemap_shader=00000001
meter_shader=00000001` (`S_FALSE`: not attempted) — so run 4 exercises no
stage-2 code beyond its defaults, and `tonemap_errors=0 meter_errors=0` on the
`hdr_device` line means "nothing ran", not "checked".

One `hdr_target device=1 frame=0 width=1280 height=768 format=A16B16G16R16F
bytes=7864320 create=00000000 chain_levels=0 chain_bytes=0 meter=0
meter_reason=off`: 7.5 MiB, created once and never re-created — no Reset and no
dimension change in the whole session (`reset_count=0`).

### Per-frame record

`hdr_frame` is emitted in capture frames and every 60th frame, so the 92 lines
describe the logged frames, not all ~4,549 of them. Distribution:

| field | values |
| --- | --- |
| `redirected` | 80 × 1, 12 × 0 |
| `end` | 79 `hook`, 12 `none`, **1 `present`** |
| `writebacks` | 79 × 1, 1 × 2, 12 × 0 |
| `flushes` | 91 × 0, 1 × 2 |
| `writeback_source` | 80 `shader`, 12 `none` |
| `suspended` / `resumed` | 0 / 0 on all 92 |
| `dirty_at_present` | 0 on all 92 |
| `unwind` / `blocked` / `refused_msaa` | 0 / 0 / 0 on all 92 |
| `caps` / `tonemap` | `ok` / `identity` on all 92 |

No `hdr_unwind` and no `hdr_recheck` record exists anywhere in the log, and
`hdr_bind` was charged only twice in the whole run, so rung 2 and rung 3 of the
must-unwind ladder were never exercised: every ended frame took rung 1, the
identity copy draw, and the draw's own restoration left RT0 at the caller's
binding without an extra bind.

The cumulative `telemetry_metric` counters cover **every** frame, not only the
logged ones, and they are the stronger health statement:

| metric | count | mean | max | buckets (≤10 µs / ≤100 µs / ≤1 ms / ≤10 ms / ≤100 ms) |
| --- | ---: | ---: | ---: | --- |
| `hdr_redirect` | 3,888 | 6.4 µs | 252.2 µs | 3792 / 95 / 1 / 0 / 0 |
| `hdr_writeback` | 3,889 | 84.6 µs | 25,047 µs | 0 / 3665 / 216 / 7 / 1 |
| `hdr_writeback_draw` | 3,889 | 83.7 µs | 25,046 µs | 0 / 3671 / 210 / 7 / 1 |
| `hdr_bind` | 2 | 12.9 µs | 16.3 µs | 1 / 1 / 0 / 0 / 0 |
| failures | 0 on every metric | | | |

3,888 latches and 3,889 write-backs is exactly one write-back per latched frame
plus the one extra flush of frame 0 (below): **no latched frame in the session
failed to write back**, which is the property whose failure mode is a black
frame. One write-back took 25 ms; §5 treats it as a hitch, not a pattern.

### The 12 `end=none` frames: the menu, correctly untouched

All 12 have `redirected=0`, and their `motion_output_frame` twins say why:
`latched=0`, `routed=0`, `hook_signals=1` with `hook_outside_scene=1` (the
engine hook fired but not in the Scene phase) and `taa_skip=not_reached`.
Eleven report `selector_state=9` / `hook_state=9` (`Rejected`): the pre-load
menu (frames 60–600, 638–703 draws each) and frame 4500, the menu the session
ended in. Frame 3120 reports `hook_state=0` (`AwaitInitialClear`) with 10
draws — the blank frame of the sector change, which never cleared a scene at
all. The selector never reaches `Background`, so
`begin_redirect` returns before creating or binding anything and the frame runs
exactly as with `X3M_HDR=0`. This is the designed behaviour, and it also means
**the menu is not tonemapped**; see §7.

### The one `end=present` frame: frame 0, and it was correct

```
hdr_frame  frame=0 redirected=1 end=present writebacks=2 flushes=2
           writeback_source=shader dirty_at_present=0 suspended=0 resumed=0
           unwind=0 latch_bind=00000000 redirect_us=8.5 writeback_us=279.0
motion_output_frame frame=0 latched=1 draws=0 routed=0 selector_state=9
           hook_signals=0 bloom_copy_seen=0 taa_skip=2 (NotReached)
```

Frame 0 is the device's first presented frame and it **renders nothing**:
`draws=0`. The initial `Clear` latched the redirect (so the game's Clear cleared
our FP16 target), the selector then rejected the frame's pattern
(`selector_state=Rejected`), the engine scene-end hook never signalled
(`hook_signals=0`, so the compositor was never reached) and there was no bloom
copy (`bloom_copy_seen=0`). What ended the redirect was therefore the
documented terminal end, Present.

The presented image was correct, and **not** because of a write-back at
Present:

* `flushes=2` and `writebacks=2` — both write-backs were `EndScene` flushes.
  `flush_redirect` is a no-op unless the target is dirty, so the frame had two
  scene brackets: latch-Clear → `EndScene` (flush 1), then a second
  `D3DCLEAR_TARGET` Clear marking the target dirty again → `EndScene`
  (flush 2). Each flush copied the target into the main surface and left the
  FP16 target bound, exactly as designed for a frame without a recognized
  scene end.
* `dirty_at_present=0` — nothing was drawn after the second flush, so at
  Present `end_redirect(Present)` called the ladder with `write=false`. **No
  copy happened at Present**; the ladder only handed RT0 back to the
  application's main surface. The main target already held the complete frame
  content (a cleared screen), which is what the game presented.
* `suspended=0 resumed=0 unwind=0`, and the post-write-back `taa_skip` is
  `NotReached`, so the resolve correctly did not run on a frame with no scene.

This is the same pattern the synthetic suite records for the plain HDR script
("end point `present` for the plain script (the `EndScene` flush wrote the
frame, `dirty_at_present=0`)", [hdr-scene-path.md](../verification/hdr-scene-path.md)), now
observed in the game. The anomaly counter the design defines —
`dirty_at_present` — stayed 0 for all 92 logged frames, so no frame ever
reached Present with unwritten content.

## 2. Identity of the write-back on the captured frames

For every captured frame the FP16 readback was converted the way the write-back
draw's fixed-function output conversion does — NaN to 0, clamp to [0, 1],
multiply by 255, round to nearest — and compared byte for byte against the
8-bit readback of the same frame, B/G/R/A against R/G/B/A.

| frame | pixels | exact | max difference | differing channels | tie candidates |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1092–1095 | 983,040 each | 983,040 (100.000000%) | 0 | 0 | 0 |
| 1284–1287 | 983,040 each | 983,040 (100.000000%) | 0 | 0 | 0 |
| 1645–1648 | 983,040 each | 983,040 (100.000000%) | 0 | 0 | 0 |
| 4215–4218 | 983,040 each | 983,040 (100.000000%) | 0 | 0 | 0 |

**15,728,640 of 15,728,640 pixels exact, on all four channels, in every pixel
class** (sentinel, routed interior and routed edge — the class totals per burst
are in the JSON; e.g. burst 1 is 836,692 sentinel / 86,866 interior / 59,482
edge). Maximum difference 0, so there is nothing to report per class or per
channel: no one-code deviation anywhere, alpha included, and no saturated pixel
misconverted.

Two notes on why this is stronger than it looks and where it is weaker:

* It is **rounding-rule independent**. For a binary16 source `v · 255` is a
  half-integer only for `v = 0.5`, and both round-half-up and round-half-even
  send 127.5 to 128, so the two tables are identical on all 65,536 bit
  patterns (proved in the paired test against exact `Fraction` arithmetic).
  `tie_candidates=0` on every frame confirms it on the data.
* It is a **self-consistency check of one write-back**, not the synthetic
  suite's twin comparison. The ~1% one-code differences recorded in
  [hdr-scene-path.md](../verification/hdr-scene-path.md) §"Case 1" come from double rounding
  between an unquantized shader output and its FP16 image; that term cannot
  appear here because the FP16 image *is* the comparison's input. What this
  measurement does rule out, on real game content, is any bias, half-pixel
  sampling offset, filtering, channel swizzle, alpha loss or clamp error in the
  write-back draw — the failure modes the fixture can only probe with synthetic
  colours.

## 3. FP16 headroom: what the game's shaders produce above 1.0 today

Stage 4 ("HDR radiance") removes clamp sites in the game's material programs;
risk 3 of the design says additive stacks that used to clip at white now
accumulate. This is the measurement of how much of that is already happening
with **unpatched** shaders.

| burst | over-1 pixels (4 frames) | fraction per frame | max channel | octave histogram |
| --- | ---: | ---: | ---: | --- |
| 1092–1095 | 7,419 | 0.188% – 0.190% | 1.676 | all in [1, 2) |
| 1284–1287 | 796 | 0.017% – 0.024% | 3.260 | [1,2) except 2 px in [2,4) |
| 1645–1648 | 807 | 0.020% – 0.021% | 2.078 | [1,2) except 1 px in [2,4) |
| 4215–4218 | 3,640 | 0.087% – 0.100% | 1.512 | all in [1, 2) |

Over all 16 frames: **12,662 of 15,728,640 pixels (0.0805%) exceed 1.0**, 70.2%
of them lie in [1.0, 1.25), exactly **3 pixels in the whole capture reach 2.0**
and the single largest value is 3.26. No channel is negative and no value is
NaN or infinite anywhere, so no depth/motion sentinel leaks into RT0 and the
FP16 image is safe input for a tonemap. Pixels sitting exactly at 1.0 are rare
(1–8 per frame), so the values are not the product of a saturate; the content
simply does not go far above white.

Where the over-1 pixels are (largest 4-connected components, from the JSON):

* One cluster around `x∈[604,685], y∈[578,612]` in every burst, on **routed**
  pixels with depth 0.830–0.888 (very near the camera), mean colour
  `(0.90, 1.15, 1.05)` — green-dominant near-white: the cockpit/HUD overlay
  drawn as scene geometry. In burst 1 it is joined by a 1,078-pixel cluster at
  `x∈[640,675], y∈[504,559]` (depth 0.85–0.89, mean `(0.97, 1.23, 0.98)`) and
  bottom-edge clusters at `y∈[714,767]`.
* A background cluster in burst 4 at `y≈365–380` travelling left across the
  four frames (`x` 100→19), entirely **sentinel**-class (depth −1, no routed
  geometry): an unrouted sprite/glow, peak 1.38.
* The three pixels above 2.0 are isolated single pixels (e.g. frame 1285
  `(506,111)` = `(3.26, 3.20, 1.94)` and `(515,112)` = `(2.12, 2.03, 1.29)`):
  warm-white fireflies, the case `X3M_HDR_CLAMP` exists for.

**Consequence for stage 4.** Additive stacks do exceed 1.0 today, but by less
than one stop on less than a tenth of a percent of pixels, and almost all of it
is the near-field HUD/cockpit overlay rather than engines, suns or particles.
Removing the material clamp is therefore *necessary* for the HDR look but will
change nothing by itself on content like this: until the material and light
programs stop saturating, an AgX curve has essentially no highlight range to
roll off, and what it will visibly do is re-map the existing 0–1 range. The
same numbers also bound risk 3 of the design: all 12,662 over-1 pixels convert
to 255 on the FP16 path and would have clipped to 255 on the direct 8-bit path
as well, so today's accumulation is invisible in the presented frame, and the
only residual difference from vanilla is the FP16 double rounding of sub-1
values that the synthetic suite bounds at one code.

## 4. TAA with the redirect on

Still the 8-bit path in this build: the resolve reads the main target *after*
the write-back and copies its FP16 result back into it.

| | run 4 (`hdr=1`) | run 2 (`hdr=0`) |
| --- | --- | --- |
| logged frames | 92 | 94 |
| attempted / resolved | 79 / 79 | 89 / 89 |
| used history | 76 | 83 |
| `taa_skip` | 79 `none`, 13 `not_reached` | 89 `none`, 5 `not_reached` |
| `scene_end_source` | 79 `hook`, 13 `none` | 89 `hook`, 5 `none` |
| `scene_end_check` | 79 `agree`, 13 `none` | 89 `agree`, 5 `none` |
| camera policy | 79 × 2, 13 × 1 | 89 × 2, 5 × 1 |
| camera reason | 79 `camera_path`, 13 `switch_off` | 89 `camera_path`, 5 `switch_off` |
| `camera_cut` | 0 on all 92 | 0 on all 94 |
| `taa_result` / `taa_copy` / `taa_restore` | `00000000` on all 79 | `00000000` on all 89 |
| apply / restore failures | 0 / 0 | 0 / 0 |

Every frame that latched a scene resolved, at the engine hook, with the hook
and the bloom copy agreeing, and the camera far-plane reprojection active on
every one of them (`camera_policy=2`, `camera_reason=camera_path`). The 13
non-resolved frames are the 12 unlatched menu frames plus frame 0. **No
difference from run 2's health**: the extra `not_reached` frames are the longer
menu phase of this session, and no new skip reason, restore failure or
disagreement appeared. The redirect therefore does not disturb the resolve —
expected, because `scene_end_hook` writes back and rebinds RT0 before it
queries RT0 for the resolve, and the 79 `taa_copy=00000000` results show the
copy-back found the application's surface where it belongs.

## 5. Cost

CPU-inclusive QPC spans of the calling thread; never GPU time.

### The HDR work itself

Per logged, latched frame (`hdr_frame`), split because a capture frame's
write-back draw is submitted behind that frame's five readbacks:

| group | frames | `redirect_us` median/max | `writeback_us` median/max | `bind_us` | total median/max |
| --- | ---: | --- | --- | --- | --- |
| ordinary | 64 | 6.1 / 11.8 | 67.6 / 279.0 | 0.0 / 9.4 | **74.2 / 296.9** |
| capture | 16 | 7.7 / 12.6 | 368.3 / 626.9 | 0.0 | 379.1 / 632.7 |

The capture-frame inflation is entirely `writeback_draw_us` (the readbacks
serialize the draw's submission), so the ordinary column is the honest cost:
**74 µs per frame**, which is 0.24% of the 30.6 ms fast-regime frame and 1.9%
of the route's own 3.95 ms. The whole-run metrics agree (mean 84.6 µs over
3,889 write-backs, 94.2% of them ≤ 100 µs). `writeback_stretch_us` and
`recheck_us` are 0 everywhere — the fallback rungs never ran. The single 25 ms
`hdr_writeback` outlier (one of 3,889; the next-largest bucket holds 7 between
1 and 10 ms) is not visible in any logged frame and is consistent with a
driver-side hitch around a sector change; it is a watch item, not a regression
signal.

### Frame time

From `analyze_iteration09_cost.py`
(`verification/results/iteration-09-run4-cost.{json,txt}`), median over
one-second telemetry windows, capture and loading windows excluded:

| run | flags | fast regime | slow regime | menu |
| --- | --- | --- | --- | --- |
| run 4 | route + TAA + hook + **hdr**, no profiler | 30.64 ms (85 w, 231 draws/f) | 67.11 ms (24 w, 735 draws/f) | 38.49 ms (26 w) |
| run 2 | route + TAA + hook, `hdr=0`, **+ profiler + mesh cache (`ready=1`)** | 34.24 ms (81 w, 227 draws/f) | 64.43 ms (56 w, 471 draws/f) | 39.89 ms (10 w) |
| run 3 | route off | 24.05 ms (90 w, 229 draws/f) | 47.69 ms (17 w, 720 draws/f) | 34.97 ms (10 w) |

Run 4 against run 2 at matched draw counts: **−17.1 µs/draw (IQR −43.8 … +0.2),
sign 3+/8−, p = 0.23** — i.e. the run with HDR on is, if anything, slightly
faster, and the difference is not significant. That is not a claim that HDR is
free; it is that HDR's 74 µs/frame is far below the noise between two different
flight paths, and run 2 additionally carried the sampling profiler (1.005
ms/frame, 2.37% of wall) and the mesh-adjacency cache. Against the route-off
run 3, run 4 is +6.59 ms (+27.4%) in the fast regime and +19.42 ms (+40.7%) in
the slow one, of which the route's own metrics explain 0.60 and 0.69; HDR is
0.07 ms of that. **This is a like-for-like comparison only up to those flag
differences** — an `--hdr` on/off pair with everything else identical has not
been run, and the 74 µs direct measurement is the number to quote, not the
frame-time delta.

## 6. Alt-tab, focus and cursor

Two sampled departures, both in the **menu phase before the save was loaded**,
and the pattern matches [cursor-observations.md](../reverse-engineering/cursor-observations.md)
exactly:

| episode | away | back | foreground while away | GUI active/focus | clip while away | cursor samples |
| --- | ---: | ---: | --- | --- | --- | --- |
| 1 | frame 407 | frame 420 | `00030020` | `00000000` / `00000000` | whole virtual desktop `-1512,0,5120,1440` | frame 407 `flags=1 cursor=00010022 (2125,1153)`, frame 414 `flags=1 cursor=00010022 (2125,878)`, frame 420 `flags=0 cursor=00000000` |
| 2 | frame 528 | frame 534 | `00030020` | `00000000` / `00000000` | whole virtual desktop | frame 528 `flags=1 cursor=00010022 (2125,878)`, frame 534 `flags=0 cursor=00000000` |

Both returns restore `foreground`, `thread_focus`, `gui_active`, `gui_focus`
and the client clip rectangle `1920,111,3200,879`; `iconic=0` and
`window_rect=1917,82,3203,882` throughout (never minimized, never resized);
`gui_capture` null throughout. There is **no device Reset in the session**
(`reset_count=0`, one `hdr_target` line), so the redirect's Reset path was not
exercised. Of 232 `telemetry_cursor_poll` records, 228 report `flags=0` with a
null handle; the four with `flags=1` are the session's first present and the
three inside the two episodes — the host arrow appears exactly while the game
is not foreground, which is normal and is not the reported double cursor. No
D3D cursor call, `SetCursor` or `SetCursorPos` metric was emitted.

**Coverage gap:** frames 407–534 are all in the unlatched menu phase, so the
redirect was *not active* during either alt-tab: the first logged frame with
`redirected=1` after them is 660, and frames 60–600 all report
`redirected=0`. Run 4 therefore says nothing about alt-tab with the FP16 target
bound.

## 7. What run 5 (tonemap on) should carry

Nothing in this run blocks enabling `--hdr-tonemap`. Watch items, in order:

1. **The tonemap has almost no headroom to work with** (§3). With 0.08% of
   pixels above 1.0 and a maximum of 3.26, AgX plus auto exposure will re-map
   the existing 0–1 range rather than roll off highlights, and the design's own
   caveat about gamma-space blending applies. Judge run 5 on the tone curve,
   not on highlight recovery, and consider running an `X3M_HDR_EV_MANUAL` A/B
   so the image change is separable from the adaptation.
2. **The menu is not tonemapped** (§1): the 12 unlatched frames per logged
   sample are the menu and the sector-change blank, where nothing is
   redirected. Expect a tone discontinuity between menu and flight, and do not
   read it as a bug.
3. **New per-frame device traffic.** Stage 2 adds the meter reduction chain,
   a 1×1 readback (`hdr_meter_readback`) and the AgX draw inside the same
   write-back. The capture-frame column of §5 shows the write-back draw's
   submission is sensitive to pending readbacks (67 µs → 368 µs), so the meter
   readback is the thing to measure: compare `writeback_us`, `meter_us` and
   `readback_us` on **ordinary** frames against this run's 74 µs, and check
   `stepped`, `dt_ms`, `ev_adapted`, `ev_target` and `avg_log_l` for
   adaptation stability across the menu↔flight boundary and across a focus
   loss (where `dt_ms` can be large).
4. **Keep the identity baseline.** §2 is now a bit-exact reference for the
   write-back on live content. Run 5 should keep `--taa-debug` so
   `hdr`/`color`/`taa` are captured again; with the tonemap on, `color` becomes
   `AgX(decode(hdr))` and the same comparison turns into a shader-against-
   reference check (`tools/analysis/agx_reference.py`) on real content, with
   alpha still required to be exact.
5. **Two things this run could not exercise**: a device Reset with the redirect
   active, and an alt-tab with the redirect active (§6). If the user can
   alt-tab *during flight* in run 5 and change the resolution once, the ladder
   and the target re-creation get their first gameplay coverage.
6. **The 25 ms write-back outlier** (§5). One in 3,889. Re-check the
   `hdr_writeback` bucket tail in run 5; if the ≥10 ms bucket grows with the
   tonemap on, it is the meter readback and not a hitch.

## Anomalies

* Frame 0 ends at `present` with two `EndScene` flushes — explained and correct
  (§1); no write-back at Present, nothing pending.
* One `hdr_writeback` of 25 ms out of 3,889 (§5).
* Run 4 is *faster* than run 2 in the fast regime (§5): a flag and path
  difference, not an HDR effect.

## Limits

* One session, one build (`1d36c29`), CrossOver Preview only; no native
  Windows evidence.
* With `--hdr` the `color` readback is the post-write-back main target, so §2 is
  a self-consistency check of one write-back and not a comparison against an
  independently rendered 8-bit frame.
* The FP16 readback is taken before the frame's first write-back and the 8-bit
  readback after it; a frame with more than one write-back would compare the
  first FP16 state against the last 8-bit state. All 16 captured frames had
  exactly one.
* `hdr_frame` and `motion_output_frame` are emitted in capture frames and every
  60th frame; the distributions of §1 and §4 describe those 92 frames. The
  `telemetry_metric` counters of §1 and §5 cover every frame.
* All timings are CPU-inclusive QPC spans; nothing here is GPU time, and no
  in-game FPS was read.
* The window and cursor records are change-only and at most 4 Hz: an
  unobserved transition between two records is not excluded.
