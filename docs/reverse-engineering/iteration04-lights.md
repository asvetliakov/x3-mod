# Iteration 0.4: active point-light shader inputs

The new gameplay captures contain active point lights. Across 20 complete frames,
the named shader count is zero in 12,731 observations, two in 64 observations and
one in 32 observations. No named-light observation is rejected by the existing
typed-layout/query validator. These are per-draw shader-stage observations, not
12,827 distinct scene lights or an inventory of all engine lights.

The source is `session-20260910-234001-212.log`, SHA256
`f8a9f43e18c16d132e5337ba9dd3f69aabc54343c71b4ef57532d8125d9e5196`.
The compact [report](../../verification/results/iteration04-light-summary.json)
retains each frame's counts and distinct decoded active payloads. It uses CTAB
metadata parsed from the local runtime shader dumps and the existing
`analyze_lights.py` validation, which ignores stale array entries above the
explicit integer count.

| Burst | Active inputs per frame |
| --- | --- |
| 120–123 | None |
| 2775–2778 | Four observations of the same two-light payload |
| 2878–2881 | Four observations of the same two-light payload |
| 3241–3244 | Eight observations of the same two-light payload |
| 3462–3465, user-reported third person | Eight observations of one light |

The first light has RGB `(0.99609375, 0.99609375, 0.99609375)` and attenuation
coefficients approximately `(1, 0.01, 0, 0)`. The second, when present, has RGB
`(0, 0.48828125, 0.5703125)` and coefficients approximately
`(1, 0, 0.00008888889, 0)`. Positions change across gameplay frames; exact values
are retained in the report. Earlier instruction inspection establishes the
constant/linear/quadratic use of the first three attenuation components on the
observed common material paths; the fourth component remains unused there.

This is useful input evidence for replacement lighting. It does not yet identify
the corresponding engine light objects, prove world-space units for every effect,
recover lights omitted by the original selection policy, or establish physical
luminance/color encoding. The change from two selected lights to one must not be
attributed solely to the camera switch: intervening frames and scene state are
not captured. Clustered lighting still needs the engine's broader light set.

To reproduce full per-draw decoding with existing tools, first run
`tools/analysis/shader_constants.py` on the local capture directory, then pass that
metadata JSON and this session to `tools/analysis/analyze_lights.py`. Its full
output includes observation indices; the tracked compact report aggregates counts
and deduplicates identical active payloads within each frame.
