# Run56 completed flight instructions

Run200 completed this flight. The user reports no crash and no media-related
stutter and accepts the ID2 omission. CrossOver debug tracing is retired from
future runs at the user’s request. The command below is historical only.

## 56. ID2 video omission — completed

Use the same save/view and sequence that crashed in Run55. Move the camera and
remain for several minutes, covering the previous failure interval. The ID2
billboard/animated texture is intentionally absent. Check that speech and music
still work, first-person fog remains correct, and whether any periodic freeze or
crash occurs. Report the preserved session path and those observations.
No additional F8 is required unless you see a new rendering issue. Keep the
lattice Session B held until this stability check.

This candidate removes the replacement playback runtime and refuses ID2 silent
video before graph construction. Existing diagnostic/module tracing is retained
for this counter flight. Qualification and rollback are in [status](../status.md).

```sh
CX_DEBUGMSG=+timestamp,+tid,+seh,+loaddll X3M_FIXTURE_BOTTLE=X3 /tmp/x3-run56-integration/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --media-cue-trace --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --volumetric-fog 0.03 --volumetric-fog-cards replace --sector-background --taa-debug --capture-start 999999 --frame-end-stride 10 --capture-frames 32
```
