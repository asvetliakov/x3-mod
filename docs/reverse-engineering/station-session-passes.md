# Render/depth boundaries in the station-test session

The user completed `session-20260910-214701-212.log` and reported docking at a
station during the run. The process exited before analysis. Three four-frame F8
bursts contain **3,131 successful draws**, with complete frame terminators,
matching draw counts and contiguous event sequences. All 48 session shader hashes
match local archive effects. The trace does not mark docking time, so individual
bursts are not asserted to show the docked state.

## New evidence from the combined trace

Every captured frame follows the same observed resource flow:

1. Clear color and depth on main color allocation 1 and depth allocation 2.
2. Draw eight background/planet candidates, then clear depth allocation 2 again.
   Seven named matrices use the small-coordinate camera; the subsequent named
   scene draws use the approximately 10,000× larger translation regime.
3. Render the main scene and a depth-disabled stardust/effect draw.
4. Unbind depth. StretchRect copies the 1280×768 main color surface into surface
   231, whose texture container is allocation 230. The next bloom draw samples
   that same texture identity.
5. Four exact bloom shader pairs draw to 640×384 surface 233, surface 236,
   surface 233 again, and finally main surface 1. All surfaces have logical
   A8R8G8B8 format, and the observed resources are single-sampled.
6. Rebind depth allocation 2 and clear its contents immediately after bloom.
7. Draw ten depth-disabled overlay candidates, clear depth again, then draw one
   final depth-disabled candidate.

All four clears reference the **same D24X8 allocation identity 2**. Allocation
continuity does not imply content continuity. Main scene depth is destroyed before
Present. A future depth consumer must preserve/use the correct content epoch;
querying a matching surface at Present would retrieve cleared depth in these
frames. The background depth is also cleared before the main coordinate regime,
so one undifferentiated camera/depth assumption is insufficient.

| Frame | Draws | Depth clears after draw | Bloom draws |
| --- | ---: | --- | --- |
| 2754 | 103 | 0, 8, 92, 102 | 89–92 |
| 2755 | 103 | 0, 8, 92, 102 | 89–92 |
| 2756 | 108 | 0, 8, 97, 107 | 94–97 |
| 2757 | 108 | 0, 8, 97, 107 | 94–97 |
| 2934 | 295 | 0, 8, 284, 294 | 281–284 |
| 2935 | 389 | 0, 8, 378, 388 | 375–378 |
| 2936 | 412 | 0, 8, 401, 411 | 398–401 |
| 2937 | 454 | 0, 8, 443, 453 | 440–443 |
| 3104 | 411 | 0, 8, 400, 410 | 397–400 |
| 3105 | 351 | 0, 8, 340, 350 | 337–340 |
| 3106 | 243 | 0, 8, 232, 242 | 229–232 |
| 3107 | 154 | 0, 8, 143, 153 | 140–143 |

The table reports observations, not index-based injection rules. Bloom recognition
uses exact shader pairs, A/B/A/main target order, the preceding full-surface copy,
and the copy destination's texture identity actually sampled by the first bloom
draw. In these frames all eleven trailing draws are depth-disabled. Their shader
hashes remain ambiguous with GUI/nebula/stardust families, so ordering alone does
not prove each is HUD. Earlier captures also contained other final effects.

## Camera and lights

The named matrix convention continues to hold, with maximum relative transform
reconstruction error **1.41e-7**. All captured frames have two named camera regimes;
there is no third near-identity named camera group in these bursts. Candidate
resource/range correspondence remains a diagnostic, not engine instance identity.

All **2,914** shader-stage observations with the named point-light array and count
report explicit `i0.x=0`; none is rejected for missing or failed queries. This does
not exclude emissive station materials, directional lights, or lights appearing
outside the F8 bursts. The station report does not justify activating stale entries
in the eight-light array.

## Reproduction and artifacts

Raw trace SHA-256:
`81cd598428312b709889b0fb892c6243a20818f20d0a3bf400eacdc33af017f0`.
Raw captures/bytecode remain local. Derived artifacts:

- `verification/results/game-station-session-boundaries.json`
- `verification/results/game-docking-camera.json`
- `verification/results/game-docking-lights.json`
- `verification/results/game-docking-motion.json`
- `verification/results/game-docking-shader-registers.json`

The `game-docking-*` filenames label this user test session; they do not establish
that every captured frame is after docking. Metadata is separate from earlier
shader-register reports so their original hash provenance remains reproducible.

```sh
python3 tools/analysis/analyze_pass_boundaries.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures/session-20260910-214701-212.log" \
  --output verification/results/game-station-session-boundaries.json
```

The analyzer rejects incomplete/failed draw evidence, broken event sequences,
ambiguous or failed copy sources, and incorrect target/texture flow. Synthetic
negative cases verify those gates. No depth substitution, jitter, TAA or material
replacement was enabled in this session.
