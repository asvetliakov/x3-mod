# Run57 A/B — returned as run203/run204

The user reports flashes in both runs. Median-motion resets remain in B.
Historical commands below are not rerun requests.

## 57. Station-flash counter flight — ready; fog range remains offline

`lightmap1.mov` visibly records five fine-detail flashes. Camera-motion matching
strongly associates it with run202, although the user has not confirmed that
session identity. The flashes align with sudden additions to the visible draw
population. The leading hypothesis is the global TAA missing-history safeguard;
existing telemetry is too sparse to prove it fired on those exact frames.

Use the current installed build, the same save and mine view as the recording.
For each run, pan left/right several times and record normal-speed video. Report
the preserved session path, whether the flash occurred, and any new trails or
ghosting. Do not press F8. Fog stays off for this comparison. No CrossOver debug
tracing is requested. Both runs log TAA state every frame; compare the visual
flash, not FPS, because the extra logging has unmeasured overhead.

A keeps the normal 25% missing-history threshold:

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 X3M_MOTION_CUT_MEDIAN_PX=48 X3M_MOTION_CUT_MISSING=0.25 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --capture-start 999999
```

B disables only that missing-history threshold for diagnosis. The 48-pixel
motion threshold, camera cuts and other reset protections remain. This is a
process-local test, not a new default; keep this test to the same view rather
than sector travel.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 X3M_MOTION_CUT_MEDIAN_PX=48 X3M_MOTION_CUT_MISSING=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --capture-start 999999
```

A launch dry-run passed without launching the game. Source inspection confirms
the inherited threshold reaches the parser and 1 is accepted; the strict
`missing_fraction > bound` comparison cannot fire at bound 1. No DLL rebuild or
installation is needed.

The separate fog request remains 30–40 km visibility. Accurate offline reference
images now converge; the tested filtered far integrators failed. No production
range change is ready.
