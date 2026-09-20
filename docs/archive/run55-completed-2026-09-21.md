# Run55 completed flight instructions

Run199 completed this flight. The user reports the first-person fog correction
works and confirms the F8 capture was in first person; the crash recurred.
These commands are historical, not a request to repeat the flight.

Build provenance: [qualification](../../verification/results/run55-candidate-qualification.json)
and [install record](../../verification/results/run55-candidate-install.json).

## 55. First-person fog correction and crash diagnosis — completed

Qualification and installation are linked above.
The media code is unchanged: this is **not a claimed crash fix**. Repeat the
save/sequence that failed in run197. Switch to first person, rotate through the
fog and take one F8; compare chase view. Keep animated objects visible if
practical and remain for a few minutes to cover the previous failure interval.
Report whether fog stays visible in first person and whether animations remain
active, together with the preserved session path after exit or crash.

The command enables qualified exception/module tracing through `CX_DEBUGMSG`;
plain `WINEDEBUG` is overwritten by CrossOver. The launcher preserves both output
streams. Two fixture faults produced useful debugger output, but a corrupted
game process may still fail to provide a stack. No bottle setting is changed.
Keep the lattice Session B held.

```sh
CX_DEBUGMSG=+timestamp,+tid,+seh,+loaddll X3M_FIXTURE_BOTTLE=X3 /tmp/x3-media-production-integration/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --media-cue-trace --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --volumetric-fog 0.03 --volumetric-fog-cards replace --sector-background --taa-debug --capture-start 999999 --frame-end-stride 10 --capture-frames 32
```
