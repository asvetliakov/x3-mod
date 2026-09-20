# Run54 Session B — completed as run201

The user supplied `/tmp/x3-bottleX3-run201` (1,111 referenced files).
The following command is historical, not a rerun request.

## 54. Corrected lattice state observation — Session B pending

Session A returned run195/run196/run197 with repeated crashes and first-person
fog failure. Its [instructions are archived](../archive/run54a-completed-2026-09-21.md).
Run56 subsequently passed the media stability check. The lattice capture can
resume on the accepted baseline; the reference command below is ready
for the next convenient flight. No CrossOver debug tracing is requested.

B. Corrected lattice state observation: same solar-plant arm view, F8 at rest, during camera rotation and during ship translation. This candidate protects the observation from changing MRT bindings; it does not claim a motion-crawl correction. Keep the arm visible.

```sh
X3M_FIXTURE_BOTTLE=X3 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --lattice-state run177_panel_position_v1 --taa-debug --capture-start 999999 --frame-end-stride 10 --capture-frames 32
```


