# Completed Run52 instructions and outcome

A/run187: user48–50 FPS; B/run188:49–51; C/run189:52–53, no visible issues.
Lazy accepted as launcher default; see [motion-output ledger](../verification/motion-output.md#run52-lazy-render-target-binding-accepted-as-launcher-default-2026-09-20).
Commands below are provenance, not another flight request.

## 52. Busy-station attribution and retained-target comparison

Ready: the qualified candidate is recorded as installed in [status](../status.md). This test uses only the busy-station save,
with the same stationary camera/view for all three sessions. Let each settle,
then hold the view for about 30 seconds and note the FPS. No F8 or other saves
are needed. Preserve each session normally.

A measures the remaining engine submission interval with corrected cross-view
accounting, and separates HDR transfer, extraction and statistics cost plus
shadow lease retirement. The diagnostic adds overhead, so its FPS is not the
performance baseline.

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --frame-phases --residual-phases --collide-memo --frame-end-stride 10 --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode perdraw --capture-start 999999 --capture-frames 2
```

B removes the per-draw phase instrumentation. This is the matched FPS baseline;
ordinary telemetry is retained.

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --collide-memo --frame-end-stride 10 --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode perdraw --capture-start 999999 --capture-frames 2
```

C changes only the existing render-target binding policy to `lazy`. Compare FPS
against B and watch for missing geometry, flicker or corrupted effects. This is
a counter for an existing opt-in proxy optimization, not a new default.

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --collide-memo --frame-end-stride 10 --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --capture-start 999999 --capture-frames 2
```

Report the three preserved directories and FPS. The analysis must compare
matched steady intervals, retain diagnostic self-cost and sparse-log limits,
and check visual correctness before considering a default change. For B/C,
require matched draw counts and `state_hooks installed=0 reason=none`; otherwise
the lazy comparison is not eligible. This build
does not contain the spatial-fog prototype, moving-lattice correction, media
decoder repair or moving-collision optimization. Run51's media counter remains
independent and is not a prerequisite for this work.
