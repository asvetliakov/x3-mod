# Point-light counts in the turning capture

The user's schema-2 capture `session-20260910-210505-212.log` contains 762
successful draws in eight complete frames. All 45 session shaders have exact
archive matches. Camera and candidate resource correspondence are documented in
[turning-camera.md](turning-camera.md).

The named point-light inputs are now readable: **all 550 shader-stage observations
with both `g_nNumLightPoint` and `g_LightPoint` have an explicit integer count of
zero**. Per-frame observation counts are 47, 48, 49, 49, 78, 88, 93 and 98.
No eligible observation was rejected for missing or failed queries.

This does not mean the scene is unlit. It says these named point-light arrays
are inactive for the observed draws; other light sources and emissive inputs
remain separate. Float-array values retained from earlier draws are not evidence
of active lights. There is no justification for treating all eight array entries
as active or for constructing a sector light registry from this capture.

`tools/analysis/analyze_lights.py` validates CTAB structure/member types, the
explicit integer count and namespace bounds, successful draw/query/frame results,
and active float-register coverage. Only active entries are decoded. Unknown
layouts and unavailable snapshots are rejected rather than interpreted as zero.
The synthetic tests cover stale arrays with zero counts, missing counts, invalid
capacity/layout, failed or short queries, nonfinite/malformed values and truncated
frames. Derived observations and exact input hashes are in
[game-turning-lights.json](../../verification/results/game-turning-lights.json).

Reproduce with:

```sh
python3 tools/analysis/analyze_lights.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures/session-20260910-210505-212.log" \
  --metadata verification/results/shader-registers.json \
  --output verification/results/game-turning-lights.json
```

A later consolidated gameplay test should include visible weapons or another
known local-light emitter if available. First inspect the effect instructions and
engine light submission path to determine whether and where this array is used;
repeat loading solely to obtain another empty light capture is unnecessary.
