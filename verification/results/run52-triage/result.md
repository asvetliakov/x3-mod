# Run 52 triage

## Observation

All sessions report source `976307f24d08e76c511ca4eaffcd018e5f0ff376` and DLL SHA-256 `4bee98b40420ff8a7ddc433435a1ee6727d72c586f26f169b23d492f628df685` (consistent=True).
The isolated source checkout is `446ec38f22e2d33ecb89e67615ddf0dc7dcd1bef`; installed source is its ancestor and all 15 checked attribution/HDR/readback paths are unchanged from installed source=True.
Session timestamps (as encoded in log names): A 2026-09-20T05:19:31, B 2026-09-20T05:22:14, C 2026-09-20T05:24:43.
The maximal shared post-transition (frame >=3000) 478-draw interval is frames 3610-3700: A/B/C have 10/10/10 ten-frame samples.
Frame-time medians are A 20.10 ms (49.75 reciprocal FPS), B 19.75 ms (50.63), C 19.00 ms (52.63).
B/C mode evidence is perdraw/lazy respectively; both state_hooks rows are installed=0, reason=none.
B/C proxy_options records match after excluding only X3M_MOTION_RT_MODE=True; the A frame/pass/pass-attribution/residual/residual-attribution windows have aligned ends=True.
P95 uses nearest rank: sorted[ceil(0.95*n)-1].
B/C have 10 overlapping post-transition exact draw-count strata. The substantial shared strata are 478 draws (B/C 63/239 samples; median/p95/raw range 19.70/20.90/19.30-21.00 vs 18.90/19.70/18.50-21.70 ms; >30ms samples 0/0), 510 draws (13/15; 20.80/20.10 ms), and 514 draws (8/7; 20.70/20.00 ms). The remaining seven strata have at most seven samples on one side.
At camera_reason=0/cut=0, the 478-draw route rows agree: B {'rows': 11, 'routed_values': [453], 'matched_values': [453], 'depth_routed_values': [453]}; C {'rows': 39, 'routed_values': [453], 'matched_values': [453], 'depth_routed_values': [453]}.
All selected 478-route rows have B/C route health {'routed_values': [453], 'matched_values': [453], 'depth_routed_values': [453]}/{'routed_values': [453], 'matched_values': [453], 'depth_routed_values': [453]}. B/C set_rt medians are 1812/4; lazy_flushes 0/1; apply failures 0/0; restore failures 0/0.
A has 9 retained 300-frame phase windows (ends 3000-5400). Direct window-median fields: submit 11229 us; pass apply/draw/end 3530/4798/55 us; pass scoped/outside/complement 8389/0/2825 us; corrected prepare/setup 1947/867 us; direct complements between-prepare/outside-setup 4518/0 us.
Pass-attribution health totals outside-passes/crossing-passes/scope-errors/complement-underflow are 90/0/0/0; those totals do not alter the reported window medians.
A diagnostic self estimates are pass 192 us and residual 51 us; frame-phase self cost is not emitted. HDR has 40 sparse nonzero rows: writeback/transfer-lock/extract-unlock/statistics-adapt medians 448.2/154.9/8.4/85.2 us. Lease has 41 rows: calls/records/refs/us 2/342/1026/63.4.

## Inference

C is faster than B in the equal-draw evidence, consistent with the user FPS observations and with no reported visual issue. It is an eligible unhooked production lazy counter for the root's cumulative-evidence default-policy decision alongside prior fixtures. B/C lacks a same-frame diagnostic-overhead or causal subtraction.

A pass_attribution complement and the corrected residual complement are direct per-frame measurements before their window reductions. They correct attribution scope, but are not B/C causal comparisons or independent addends. HDR buckets and lease retirement are direct CPU spans, not GPU time.

## Decision scope

This result supports the root's default-policy decision. Intrusive frame timing, state filtering, pass replay, sorting and lease-lifetime changes remain outside its scope.

## Owning locations

Corrected partition: `/tmp/x3-submission-attribution/src/proxy/residual_phases_core.h:11-19`; emitted complement: `src/proxy/residual_phases.cpp:42-50`. HDR three buckets and lease logging: `src/proxy/motion_output.cpp:6146-48, 6725-27`. RT mode/state-hook configuration: `src/proxy/capture.cpp:2255-57, 2465-67, 3063-65`.
