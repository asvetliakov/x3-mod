# Cross-frame motion-history key: empirical validation

The proposed live history key `K1` — `(load_epoch, registry_epoch, node_serial,
camera_serial, model, lod, VB id, IB id, stream offset, stride, declaration id,
topology, start index, primitive count, base vertex, min vertex, vertex count)` —
is **unique within every captured Scene phase** (0 duplicate keys in 8,957
draws across 24 gameplay frames) and pairs **6,698 of 6,700** eligible draws
across the 18 adjacent frame pairs (99.97%). Both unmatched draws are genuinely
new node serials. No pairing is contradicted by the submitted transform rows.

This validates the key as a *candidate correspondence* between two recorded draw
submissions on this capture. It is not proof of engine object identity, of stable
vertex payloads, of a correct motion vector, or of temporal-history eligibility;
buffer contents are still not captured and an allocation identity can be
rewritten in place.

## Evidence and reproduction

Immutable input: `/tmp/x3-iteration05-completed-snapshot.log`, **216,605,445
bytes**, SHA-256 `e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8`
(the same completed-session snapshot as the [lifetime](iteration05-lifetimes.md)
and [depth/motion-input](iteration05-depth-motion.md) audits). The log stays local
and untracked.

```sh
python3 tools/analysis/analyze_motion_history_key.py \
  /tmp/x3-iteration05-completed-snapshot.log \
  --expected-sha256 e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8 \
  --session-complete --output verification/results/motion-history-key-summary.json
python3 -m unittest discover -s verification/analysis -p test_motion_history_key.py
```

- [Derived report](../../verification/results/motion-history-key-summary.json)
  (per-frame brackets and key counts, per-pair match rates, row-delta quantiles,
  bursts, shader-pair census, provenance hashes).
- [Test results](../../verification/results/motion-history-key-tests.txt):
  33 original synthetic fixtures, no captured game data embedded.

The [analyzer](../../tools/analysis/analyze_motion_history_key.py) reads the file
once, hashes its bytes, requires the expected hash plus stable size/mtime, and
bounds frames (1,024), draws (100,000) and device events (200,000). Positional
records (`stream`, `indices`, `draw_args`, `constant*`) are attributed only to
their enclosing draw block; coordinate-bearing records (`object_context`,
`motion_input`, `motion_lifetime`, `draw_result`) must match their own
device/frame/index. Duplicate or missing records fail closed and never become
zero-valued facts.

**Submitted rows are never guessed.** The four registers are located from the
tracked [shader profile registry](../../verification/results/shader-profile-registry.json)
(`matrix_register`, 24 for every reviewed SM3 material program here), and the
decoded sixteen little-endian words must reproduce the producer's own
`motion_input rows_hash` (FNV-1a-64) exactly. All 8,957 keyable Scene draws pass
that check, which independently confirms both the register choice and the parse.
Rows omitted under a successful `encoding=sparse_zero` query read as zeros only
because the same hash then has to agree.

Shader model is *not* recorded by the capture log. The table below derives it from
the tracked registry's `shader_version` / `coverage.shader_model` for the same
FNV-1a-64 program identities.

## Scene-phase bracket

Reusing the existing boundary evidence: a frame counts as gameplay only when it
retains a confirmed scene-depth boundary (`scene_depth_copy valid=1` and
`scene_depth_boundary confirmed=1`). The Scene phase then starts after the first
full depth-only `Clear` (`flags=2`, `rect_count=0`, `z=1`) of the latched main
color/depth pair that follows at least one background draw, and ends at the draw
before the next non-`draw_begin` device event.

That end event is `SetDepthStencilSurface` in **all 24** gameplay frames,
immediately followed by the three `ColorFill`s and the `StretchRect` of the bloom
group. Note that this is *not* the later Clear the depth selector copies before:
in frame 1794 the Scene phase is draws 6–697, the bloom group begins at event 700,
and the selected depth Clear is event 714 after draw 701.

The four menu frames 120–123 reject the pattern at their first color-only Clear
and are excluded, leaving **24 gameplay frames in six four-frame bursts and 18
adjacent pairs**.

## 1. Scene draws, scope and key uniqueness per frame

| Frames | Scene draws | Known lifetimes | Node serials | Max draws / node | Distinct VBs | Distinct K1 | Duplicate K1 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1794–1797 (6–697) | 692 | 690 | 141 | 38 | 441 | 690 | **0** |
| 1975–1978 (6–271…273) | 266–268 | 264–266 | 75–77 | 36 | 203–204 | 264–266 | **0** |
| 2435–2438 (6–375) | 370 | 368 | 85 | 33 | 296 | 368 | **0** |
| 2806–2809 (6–256) | 251 | 249 | 49 | 33 | 214 | 249 | **0** |
| 3047–3050 (6–247…210) | 205–242 | 203–240 | 47–50 | 34 | 168–205 | 203–240 | **0** |
| 4096–4099 (6–452) | 447 | 446 | 41 | 22 | 129 | 446 | **0** |

Totals over the 24 frames: **9,001 Scene draws, 8,957 (99.51%) scoped with known
consistent lifetimes**, 44 unscoped placeholders (two per frame in bursts 1–5, one
per frame in the reloaded burst). All 8,957 also have complete buffered geometry
and verified rows, so `keyable = known_lifetime` everywhere.

**K1 has no within-frame duplicates anywhere.** That is not a trivial consequence
of any single field: 7,846 of the 8,957 draws share their node serial with another
Scene draw in the same frame (one node serial submits up to 38 draws in a frame),
and 3,734 share a vertex-buffer identity. The disambiguation comes from the
combination of buffer/declaration identity with the draw range.

The near-duplicates are visible in **K2** (K1 without VB/IB/declaration):
**108 duplicated keys covering 232 draws**, 1–10 per frame. Every one of the 108
groups is the same `(node serial, model, LOD)` submitting two or three sub-meshes
with equal topology/primitive/vertex counts but different buffers; the draw
indices are never adjacent; 88 of 108 sit on the Argon vertex program
`53a0a641107ed76c` with four different pixel shaders, and LOD 2 dominates (72).
**All 108 groups carry bit-identical submitted rows** (0 groups with differing
rows), so these are per-material sub-mesh splits of one rigid node, not two-sided
passes with distinct transforms.

## 2. Adjacent-pair match rates, K1 vs K2 vs K3

Over the 18 adjacent pairs (6,700 keyable current-frame draws, 6,733 Scene draws):

| Key | Matched | of keyable | of Scene draws | Ambiguous in N-1 | Pairings differing from K1 |
| --- | ---: | ---: | ---: | ---: | ---: |
| **K1** (full) | **6,698** | **99.97%** | 99.48% | **0** | — |
| K2 (no VB/IB/declaration) | 6,524 | 97.37% | 96.90% | 174 | 0 |
| K2b (K2 without stream offset/stride) | 6,524 | 97.37% | 96.90% | 174 | 0 |
| K3 (node serial + ordinal in node) | 6,698 | 99.97% | 99.48% | 0 | 0 |

K1 reaches 100% of keyable draws in 16 of 18 pairs. The only two misses are
1975→1976 and 1976→1977, where one new node serial enters the scene each frame —
correct fail-closed behaviour, not a key defect. K2 and K2b are identical on this
capture because the stream offset is always 0 and the stride always follows the
declaration; both lose exactly the 174 draws poisoned by previous-frame ambiguity.

K3 ties K1 numerically **only because no shared node serial changed its Scene-draw
count in any of the 18 pairs** (0 of 1,319 shared node observations). It is an
ordering key with no fail-closed property: the fixture
`test_ordinal_key_silently_repairs_when_a_submission_disappears` shows that when a
node drops one of three submissions, K3 pairs both survivors with the *wrong*
previous rows while K1 pairs them correctly. The capture already shows the draw
stream is not stable — **226 K1 matches change their draw index**, 109 and 108 of
them in 3047→3048 and 3048→3049 where whole nodes disappear — so order-derived
identity is not safe even though it happened to hold here.

**K1 is the best key: the same match rate as the ordinal key, with zero ambiguity,
zero order dependence, and an explicit failure rather than a silent mis-pairing.**

## 3. Row sanity on matched pairs

All 6,698 K1 matches have verified rows on both sides. Deltas are taken on the
submitted `c24–27` rows: `linear` is the 3×3 block, `translation` the fourth
column. Absolute values carry the projection scale and are only meaningful next to
the relative figures.

| Quantity | Median | p95 | Max |
| --- | ---: | ---: | ---: |
| 3×3 max abs delta | 0 | 133.28 | 461.10 |
| 3×3 max relative delta | 0 | 0.0215 | **0.1119** |
| Translation max abs delta | 2.287 | 1,843.1 | 3,196.9 |
| Translation max relative delta | 1.397e-4 | 0.0212 | 0.0363 |

**1,952 of 6,698 matches (29.1%) have bit-identical rows** (static); 4,746 (70.9%)
changed. Per burst the static fraction is 62.5%, 0%, 57.9%, 2.7%, 0%, 0% — matching
the lifetime audit's observation that the dominant camera's view bits are unchanged
across bursts 1794–1797 and 2435–2438 and different in the rest.

**Zero matches are flagged as looking like a different object.** The flag fires
when a match's relative 3×3 change exceeds 0.25 while the same pair's median
relative 3×3 change is below 0.02 (the key already pins `camera_serial`, so this is
not a camera change). The largest relative 3×3 change anywhere is 0.1119, in the
otherwise static burst 1794–1797, and it sits inside the same pair's distribution
of genuinely rotating objects rather than as an isolated outlier.

## 4. LOD / vertex-buffer switches on a stable node serial

**Zero observed.** No node serial appears with a different set of vertex-buffer
identities across any of the 18 adjacent pairs, and no shared node serial changed
its Scene-draw count. Object churn in this capture is whole-node: 2 node serials
appear and 3 disappear across all 18 pairs (the 3047→3048 and 3048→3049 losses of
2 and 35 draws are 1 and 2 whole node serials leaving, not LOD changes).

So the capture provides **no evidence either way** about LOD switching, and the
question "would K2 have matched them" cannot be answered empirically here. Note
that K2 could not rescue a LOD change by construction: `lod` and the draw
arguments (primitive count, vertex range) are part of K2, and a LOD change alters
both. A LOD switch must therefore be treated as a history miss (sentinel), not as
something a weaker key recovers.

## 5. The reviewed Argon pair and the Scene-phase shader census

VS `53a0a641107ed76c` / PS `8759c7838bbc86c2` (`matrix_register` 24,
`homogeneous_row_dots`, vs_3_0 / ps_3_0):

| Frames | Argon Scene draws | Of all Scene draws | K1 match rate per adjacent pair |
| --- | ---: | ---: | ---: |
| 1794–1797 | 282 | 40.8% | 1.000 (3 pairs) |
| 1975–1978 | 56 | 20.9–21.1% | 1.000 (3 pairs) |
| 2435–2438 | 121 | 32.7% | 1.000 (3 pairs) |
| 2806–2809 | 43 | 17.1% | 1.000 (3 pairs) |
| 3047–3050 | 43 | 17.8–21.0% | 1.000 (3 pairs) |
| 4096–4099 | **0** | 0% | not applicable |

Total **2,180 of 9,001 Scene draws (24.2%)**. Every Argon draw that has a previous
frame is matched by K1 — 15 of 18 pairs at 1.000, and none of the three remaining
pairs contains an Argon draw at all. This refines the "about 109 of the several
hundred scene draws per gameplay frame" estimate in
[the route design](../architecture/live-motion-route.md): the real coverage ranges
from 282 draws per frame down to **none at all** in the reloaded save, so
single-pair coverage is strongly scene-dependent.

Top 15 VS/PS pairs by Scene-phase draw count across all 24 gameplay frames:

| # | VS | PS | Scene draws | VS / PS model | VS matrix reg |
| ---: | --- | --- | ---: | --- | ---: |
| 1 | 53a0a641107ed76c | 8759c7838bbc86c2 | 2,180 | 3_0 / 3_0 | 24 |
| 2 | 37c34a7478544c14 | 5f82ecacd39529cd | 1,564 | 3_0 / 3_0 | 24 |
| 3 | 4944d81dfe531b37 | ca6bfa4a6cca7e2a | 1,440 | 3_0 / 3_0 | 24 |
| 4 | 494fe349b8bc12ec | fffdabd910793aba | 1,328 | 3_0 / 3_0 | 24 |
| 5 | 4944d81dfe531b37 | 5e0a10fe752b6140 | 456 | 3_0 / 3_0 | 24 |
| 6 | 37c34a7478544c14 | f1b0e820c7b488c3 | 348 | 3_0 / 3_0 | 24 |
| 7 | 53a0a641107ed76c | 63f96eba9eea7880 | 332 | 3_0 / 3_0 | 24 |
| 8 | 494fe349b8bc12ec | e6794b6ec37ff71a | 296 | 3_0 / 3_0 | 24 |
| 9 | 53a0a641107ed76c | 3b94320087e81945 | 236 | 3_0 / 3_0 | 24 |
| 10 | 53a0a641107ed76c | 462342e3e5781384 | 233 | 3_0 / 3_0 | 24 |
| 11 | 4944d81dfe531b37 | 64bac8bb307eb896 | 148 | 3_0 / 3_0 | 24 |
| 12 | 494fe349b8bc12ec | 7c83ed50c9894e44 | 104 | 3_0 / 3_0 | 24 |
| 13 | d5e1c75351ed3f04 | 8360f422de08b5bd | 103 | **2_0 / 2_0** | 0 |
| 14 | 4944d81dfe531b37 | 0c1f3f0f440e4a0c | 88 | 3_0 / 3_0 | 24 |
| 15 | c30104cb0efb6675 | a66fb1981ba755b2 | 77 | 3_0 / 3_0 | 24 |

Five vertex programs (`53a0a641107ed76c`, `37c34a7478544c14`, `4944d81dfe531b37`,
`494fe349b8bc12ec`, `c30104cb0efb6675`) account for the great majority of Scene
draws, all with `matrix_register` 24. The one SM2 entry uses register 0 and stays
outside this route, consistent with the coverage plan.

## 6. Camera-serial and epoch changes inside bursts

**None.** In every one of the 24 gameplay frames the entire Scene phase submits
draws under exactly one camera serial, and it is unchanged across every adjacent
pair. Load/registry epochs are likewise constant within every burst.

| Burst | Camera serial | Load / registry epoch |
| --- | ---: | --- |
| 1794–1797, 1975–1978, 2435–2438, 2806–2809, 3047–3050 | 29921 | 1 / 2 |
| 4096–4099 | 35766 | 2 / 2 |

The only changes are across the *nonadjacent* gap between frames 3050 and 4096:
camera serial 29921 → 35766 and load epoch 1 → 2, the renderer reload already
documented in the [lifetime audit](iteration05-lifetimes.md). No history may be
carried across that gap, and the key folds both epochs in, so it cannot be.

## Implications for the live key

1. **Keep K1 as specified.** It is unique within every observed Scene phase and
   pairs 99.97% of eligible draws, so the design's "matched exactly one previous
   entry" rule costs essentially nothing in coverage on this capture.
2. **Keep the VB/IB/declaration identities.** Dropping them (K2/K2b) poisons 108
   keys per capture and costs 174 of 6,700 draws (2.6%) with no compensating gain.
   Stream offset and stride contributed nothing here but cost nothing either.
3. **Do not replace the key with a draw ordinal.** K3 matched the same draws only
   because no node changed its submission count in these 18 pairs. It has no
   failure mode short of a wrong transform, and 226 matches already prove the draw
   stream reorders. K1's poisoning rule fails closed; an ordinal cannot.
4. **Duplicate handling can stay strict.** K1 never duplicates, and the K2-level
   near-duplicates all carry identical rows, so the fail-closed rules will be
   exercised rarely and cost nothing when they are.
5. **Expect the sentinel on entry and exit, not on drift.** All history misses
   observed are new/removed objects (2 new, 3 removed node serials over 18 pairs)
   plus a reload boundary. A LOD switch would also be a miss; none occurred here,
   so the sentinel path for it remains unverified against live data.
6. **Locate rows by profile register and verify them.** The producer's `rows_hash`
   over the four registers reproduced on 8,957 of 8,957 draws; the live route
   should keep an equivalent check rather than assuming c24–27 for every program
   (the SM2 program in the census uses register 0).
7. **Camera continuity is not decided by this key.** One camera serial per Scene
   phase, unchanged across every adjacent pair, means the key gives no camera-cut
   signal inside a burst; the separate conservative cut policy is still required.
8. **Coverage, not correspondence, is the limiting factor.** The reviewed Argon
   pair covers 24.2% of Scene draws overall and 0% of the reloaded burst, so the
   per-profile insertion work for the other four dominant SM3 vertex programs
   determines how much of the frame gets real motion.

## Pass field (2026-09-12)

`RigidDrawKey` gained a `pass` field (`MotionPass` in
`src/renderer/motion_history.h`: `PassMainScene = 1`; `PassDepthOnly = 2`,
`PassShadow = 3` and `PassEnvironmentMap = 4` reserved; `PassUnknown = 0`
never keys — `key_valid` rejects it). It is hashed and compared like every
other field. The live route sets it to `PassMainScene` at gate 2, which has
already established the selector's Scene phase on the latched main
color/depth pair; environment-map faces are never keyed because the selector
rejects those frames before any draw reaches the gate. The analyzer's K1
carries the same constant as `render_pass` (`PASS_MAIN_SCENE = 1` in
`tools/analysis/analyze_motion_history_key.py`): every draw it keys lies
inside the Scene-phase bracket of section "Scene-phase bracket", which is the
main scene pass by definition; the capture format itself has no pass field,
so a future capture of a depth-only or shadow pass must bracket those draws
separately before they can be keyed apart. K2/K2b/K3 are derived from K1 and
inherit the field. A constant field changes no match: the 33 analyzer tests
pass unchanged, and the live fixtures' matched pixels are the values recorded
before the field existed (10,261 regular, 10,348 jittered/TAA, 14,906 in the
hook script) with the row-history unit fixture (`motion_row_history.cpp`)
setting `pass = PassMainScene` on every key. The field exists so a second
pass drawing the same node/buffers/range in one frame (a depth prepass or a
shadow pass, the assessment's "no pass discriminator" finding) can be keyed
apart before it exists, rather than after.

## What remains unproven

Vertex and index contents are still not captured, so a unique key match cannot
exclude an in-place buffer rewrite or shader-side vertex animation. Row deltas are
algebraic differences between recorded constants, not measured pixels. This
analysis covers one user session of 24 frames in six bursts with a single dominant
camera; it does not cover LOD switching, instancing, docking/menu transitions,
resolution changes or device loss. Nothing here changes the separate finite-vertex,
coverage, jitter and resolve obligations.
