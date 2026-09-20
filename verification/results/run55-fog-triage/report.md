# Run55 F8 fog admission witness

## Observation

- Session `session-20260921-005922-212.log` starts at 2026-09-21 00:59:22
  by filename; its filesystem mtime is 2026-09-20T21:02:01.328747Z.  The
  F8 capture is frames 2188--2219 (32 frames).
- All 32 F8 `camera_state` rows are `valid=1 reason=0 camera_cut=0`.
  The 32 matching `motion_output_frame` rows also have `camera_valid=1` and
  `camera_cut=0`.  TAA's separate image cut was 0 for 24 rows and 1 for 8;
  it is not a camera admission refusal.
- Fifteen sampled sector rows inside F8 are `ready`, `bluewell`, with both
  `camera_check=match` and `anchor_check=match`.
- The actual current helper at
  `src/renderer/fog_volume_math.h:15-55`, commit `54b48c36`, admits all 32
  logger-precision F8 rotations: Gram maximum `0.000150185144342`, determinant
  range `0.999793621746..0.999869539026`.  This exercises its 1e-3 Gram,
  determinant, finite, inverse, origin and sun checks with the real header.
- One in-range card row (frame 2197) reports `applied=1 ready=1 mode=2`,
  `observed=3 suppressed=3 refused=0 fault=0`.  Across the session, 10 sparse
  fog rows report `ok`; the one `card_refused` row is frame 1534, before F8.

## Inference

The Run55 camera admission change covers every logged F8 camera sample, and
the sampled card state in the first-person F8 interval is live with no refusal
or fault.  Together with the user's visual confirmation, this is compact
admission evidence for first-person fog.  It does not establish shafts, spatial
bank appearance, or a per-frame FogPass trace.

## Evidence limits

`volumetric_fog_frame` is sampled (nearest rows 1800 and 2400), so none falls
inside F8.  The one F8 card row does not enumerate all 32 transactions.  Camera
values are logger precision, rather than raw D3D bytes.  No pixels were queried:
these composited captures cannot isolate fog contribution from scene content.

Reproduce: `python3 /tmp/x3-run55-fog-triage/triage.py`; it validates
`result.json` construction and compiles/runs `admission_witness.cpp` with
`clang++ -std=c++17 -O2 -Wall -Wextra -Werror`.
