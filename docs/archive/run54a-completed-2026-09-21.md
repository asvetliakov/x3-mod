# Run54 Session A — completed capture instructions

Historical instructions only; do not rerun. Sessions195/196/197 crashed; the
user confirms Run197 F8 was first-person. See [triage](../../verification/results/run54a-triage/result.md)
and the [current queue](../verification/user-runs.md). The original command is
preserved below; Run55 supersedes this Session A request.

## 54. Media repair, expanded fog/shafts and corrected lattice observation — held

**Hold further flight:** Session A returned run195, run196 and run197 with
repeated crashes. Run197 also reports first-person fog flicker/disappearance
while chase view renders fog. The user confirms the run197 F8 was taken in
first-person mode. Diagnosis is active; Session B is not requested
until the crash is resolved. The commands below are retained as provenance.

Run54 [qualification](../../verification/results/run54-candidate-qualification.json)
and [installation/rollback](../../verification/results/run54-candidate-install.json)
remain recorded separately from the current installed build.
Use the absolute integration-launcher path below. Close the game between A and B,
and report the preserved session path after each exit. No collision or busy-view
benchmark is requested.

A. Media and expanded spatial fog: start in the new-game Argon Prime save where periodic freezes occurred. Observe visible animated objects, pan around once then repeat, and remain at least three minutes to cover the old 30-second retry interval. Note any missing/frozen animations, loading or first-view hitch separately. F8 once with animations visible. Then visit The Hole and rotate through the previously flickering cloud view; F8 there. Compare replacement with native cards via Ctrl+Alt+F9. Strength0.03 is the user's1.50x preference. Check sunlit versus occluded fog for shafts and travel to a clear sector. Report sector names with captures if possible; no busy-view benchmark requested. Check cursor after alt-tab because media adds background window/device activity.

```sh
X3M_FIXTURE_BOTTLE=X3 /tmp/x3-media-production-integration/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --media-cue-trace --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --volumetric-fog 0.03 --volumetric-fog-cards replace --sector-background --taa-debug --capture-start 999999 --frame-end-stride 10 --capture-frames 32
```

