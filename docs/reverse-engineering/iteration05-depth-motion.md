# Iteration 0.5: scene-depth boundary and motion-input audit

All **24 captured gameplay frames** retain a confirmed scene-depth boundary at
frame end, with a successful pre-Clear copy and the expected depth-epoch
transition. The four initial menu frames reject the scene pattern at their first
color-only Clear and attempt no copy. Of 10,393 gameplay draws, **7,202** have
zero draw-input blockers and all five local proof bits; every one occurs before
the selected depth Clear. However, **all 12,957 draws in the complete capture
have `vertex_finite_verified=0`**. These are useful input diagnostics, not live
TAA eligibility or numerical validation of the game's copied depth.

## Evidence and reproduction

The immutable completed-session snapshot is
`/tmp/x3-iteration05-completed-snapshot.log`, **216,605,445 bytes**, SHA-256
`e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8`.
It contains 28 complete captured frames on device lifetime ID 1. All 12,957
actual draw results and all captured Presents succeeded.

```sh
python3 tools/analysis/analyze_iteration05_depth_motion.py \
  /tmp/x3-iteration05-completed-snapshot.log \
  --expected-sha256 e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8 \
  --output verification/results/iteration05-depth-motion.json
python3 -m unittest discover -s verification/analysis -p test_iteration05_depth_motion.py
```

The [derived report](../../verification/results/iteration05-depth-motion.json)
records per-frame confirmation/clear metadata, blocker/proof counts and source
hashes. The [analyzer](../../tools/analysis/analyze_iteration05_depth_motion.py)
requires the expected snapshot hash and stable size/mtime. It limits frames,
draws, events and relevant metadata records; raw shader bytes and the large trace
are not retained in the report. Local producer-source hashes explain how the
records were interpreted; they do not independently attest the installed DLL.

Eighteen original metadata tests cover successful bookkeeping, duplicate/missing
records and scalar fields, device/frame separation, records outside their frame,
copy/clear order, epoch and generation mismatch, failed draw/Clear/Present/copy,
wrong target or viewport, missing/duplicate/unscoped context, bad shader IDs,
unknown proof/blocker bits, adjacent epoch arithmetic and resource limits.
These are parser tests, not a synthetic execution of the game shader/profile
sequence. The runtime selector has separate production and fixture evidence in
[scene-boundary-selector.md](scene-boundary-selector.md) and
[scene-capture.md](../verification/scene-capture.md).

## What the producer actually confirms

The [adapter](../../src/proxy/scene_capture.cpp) starts only for requested capture
frames after obtaining an available ownership copy source and a nonzero resource
generation. Its critical Clear path is:

1. `before_clear` asks the selector for a candidate. If valid, it calls
   `copy_auto_depth` **before forwarding the application's Clear**. The
   `scene_depth_copy valid=1` record requires successful calls plus available,
   source-bound, copy-valid ownership state, matching generation, and
   `copy_epoch == source_epoch`.
2. The application Clear runs. `after_clear` requires the selector to accept that
   same successful Clear. It retains confirmation only if the ownership copy is
   still available/valid/source-bound, generation is unchanged, copy epoch is
   unchanged, and `source_epoch == copy_epoch + 1`.
3. `scene_depth_frame phase=end` retains confirmation only while the selector
   remains Selected and Present succeeds. This is a retained diagnostic state;
   it is not a fresh GPU sample at Present.

The ownership source epoch counts successful Clears with `D3DCLEAR_ZBUFFER`
while the original auto-depth source is bound. It is **not** a counter of every
depth-writing draw. Matching source/copy epochs identify a depth-clear interval;
they do not independently prove the contents of the GPU copy.

The analyzer checks unique begin/end scope records, unique copy and boundary
records, equal device/frame/event/generation, and actual log ordering:

`scene begin < frame begin < copy < boundary < matching capture_event(Clear) < scene end < frame end`.

The actual application Clear occurs between the copy and boundary records,
according to the reviewed producer. Its `capture_event` and detailed Clear log
are emitted afterward. The matching event must succeed, use a full depth-only
Clear (`flags=2`, `rect_count=0`, `z=1`), and carry the matching color/depth IDs
and full viewport. All ordinary records must fall within their exact frame.
Capture event sequences are contiguous, after-draw counts agree with printed
draws, event counts agree with scene frame-end counts, and each actual draw has
one successful `draw_result`. A `draw_begin` event's success-shaped placeholder
is never substituted for that actual result.

This validates agreement among the producer's selection diagnostics and the
captured calls. It does not independently replay the entire shader-signature
selector from every draw or establish universal command-stream coverage.

## Observed depth evidence

All 24 selected frames use:

- Device lifetime ID **1**, ownership copy-resource generation **1**.
- Selected color identity **1**, `A8R8G8B8` (format 21), **1280 × 768**, no MSAA.
- Original depth identity **2**, `D24X8` (format 77), **1280 × 768**, no MSAA.
- Full viewport `(0,0,1280,768)`, depth range `[0,1]`.
- One copy, one boundary, `attempted=copied=confirmed=1` at frame end, selector
  state **8 (Selected)**, no rejection and successful Present.

| Captured frame range | Draws | Zero-blocker / proof-31 draws | Selected Clear after draw | Copy source epochs |
| --- | ---: | ---: | --- | --- |
| 120–123, initial menu | 2,564 | 2,044 | None; first Clear rejects | None |
| 1794–1797 | 3,200 | 2,276 | 701 each frame | 4087, 4091, 4095, 4099 |
| 1975–1978 | 1,617 | 888 | 275, 276, 277, 277 | 4811, 4815, 4819, 4823 |
| 2435–2438 | 1,672 | 1,228 | 379 each frame | 6651, 6655, 6659, 6663 |
| 2806–2809 | 1,072 | 852 | 260 each frame | 8135, 8139, 8143, 8147 |
| 3047–3050 | 960 | 750 | 251, 249, 214, 214 | 9099, 9103, 9107, 9111 |
| 4096–4099 | 1,872 | 1,208 | 456 each frame | 13291, 13295, 13299, 13303 |

For every selected frame, copy source/copy epochs are equal, and the boundary's
source epoch is exactly one greater. Each frame records four successful Clears
of the same original depth source: initial color+depth Clear, post-background
depth Clear, selected post-scene/bloom depth Clear, and a later depth Clear.
Across each of the **18 adjacent gameplay-frame pairs**, the copied source epoch
advances by **4**, exactly matching the two Clears at/after the previous copy
plus two Clears before the next copy. No continuity is inferred through the gaps
between captured bursts.

The initial four frames consistently reject at event 1: their first Clear has
`flags=1` (color only), so the selector reports Pattern rejection 5 and finishes
in state 9 (Rejected). No copy or boundary is logged; attempted/copied/confirmed
remain zero. This is an observed pattern rejection for this menu burst, not a
universal menu classifier. It also demonstrates why local draw-input proof bits
alone cannot authorize scene-history capture.

These diagnostics do **not** include numerical readback of copied depth, decode
output, or per-frame error against known geometry. Earlier original synthetic
[RESZ](../verification/depth-resolve.md) and
[depth-decoder](../verification/depth-decode.md) tests validate the mechanism on
controlled input, but are not numerical measurements of these game frames.
`color=1` identifies the selected application color target; it is not evidence
that scene color was separately saved before overlays.

## Draw-input blockers and proofs

Only complete frames with unique successful actual draws contribute to these
counts. Motion, object-context and lifetime records must match device/frame/draw
coordinates. A local candidate requires positive bounded shader identities,
zero blockers, proof mask 31, a complete scoped object context and consistent
known before/after lifetime fields. Duplicate, absent or invalid scope records
do not become a match.

The five proof bits are lifetime **1**, geometry revisions **2**, reviewed
position program **4**, supported local coverage state **8**, and successful
submission **16**. They summarize the reader's local checks. They do not include
a finite-vertex-payload attestation or the additional replay/history obligations.

| Gameplay blocker mask | Local proof mask | Draws | Meaning of recorded blocker combination |
| --- | ---: | ---: | --- |
| `00000000` | 31 | **7,202** | Reader's current local checks all pass |
| `00000040` | 23 | **3,003** | RasterState; coverage proof absent |
| `00000341` | 18 | **68** | PositionProgram, RasterState, SubmittedRows, ObjectScope |
| `00000240` | 22 | **24** | RasterState, ObjectScope |
| `00000bc5` | 16 | **96** | PositionProgram, PositionLayout, RasterState, TargetLayout, SubmittedRows, ObjectScope, QueryFailure |

The last three rows account for all **188 unscoped gameplay draws**. The initial
menu burst adds 2,044 zero-blocker/proof-31 draws, 504 RasterState-only/proof-23
draws and 16 unscoped `0xbc5`/proof-16 draws. Across all frames there are **9,246**
zero-blocker/proof-31 draws, but only 7,202 belong to frames with selected scene
depth. All 7,202 precede the selected Clear; no zero-blocker/proof-31 gameplay
candidate follows it.

Every recorded draw, including these candidates, has
`vertex_finite_verified=0`. The live reader checks shader/layout/range metadata,
submitted matrix rows, resource revisions, states and lifetimes; it does not
read and attest every vertex payload. A candidate therefore still needs finite
position data, buffer stability through replay, unambiguous previous-frame
correspondence, whole-scene color/reactive coverage, camera-cut policy and
successful motion/resolve/presentation before live TAA history can be accepted.
There is no finite-payload-qualified candidate in this capture.

## Camera/lifetime relationship and remaining limits

The [lifetime audit](iteration05-lifetimes.md) finds frame 2438's mutation revision
change between draws **387 and 388**. This depth audit places its selected Clear
after draw **379**. The observed mutation therefore occurs **after** the selected
scene boundary. It must not be described as a mutation inside that scene's
selected draw range or used alone to demand blanket scene-history rejection.
The separate object serials and epoch/camera policies remain the relevant facts.

The user supplied five action descriptions, while the capture contains the
initial menu burst and **six gameplay bursts**. The table preserves frame ranges;
it does not force a one-to-one action mapping. The final reload's load epoch
change is established independently by the lifetime audit.

Known coverage limits of the adapter remain: resource-side CPU writes such as
surface LockRect/GetDC and swap-chain Present paths are not universally observed
by this device-call stream. Copy/confirmation success does not erase those
limits. No GPU test, game launch, producer edit or installation was performed
for this read-only analysis.
