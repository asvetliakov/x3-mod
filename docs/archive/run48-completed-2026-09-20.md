# Run 48 completed instructions and results — 2026-09-20

Completed: A run176–178, B run180, C run181. Stationary thin-region improvement
is confirmed; moving crawl remains open. The user selected light-map fade
80,220 afterward. Fog strength 0.02 was preferred, with a user-estimated ~2 FPS
cost; no controlled live timing was recorded. Vanilla-card replacement was
requested; advertisement signs were not specifically inspected. Run181 measured
24.297 ms/frame (~41.2 FPS); only the measured submission candidates closed.
Remaining engine/proxy costs and moving collision remain open.

The original committed instructions below are preserved verbatim as historical
provenance, not a rerun request. Their queued/unflown language is superseded by
these results and the owning feature notes.

## 48. Lattice arm fix, distant windows, Argon Prime shadows and fog, submit stamps — A reported; B/C pending

Installed: run48 candidate (hash in [status](../status.md)). New since run 47, all default off unless said:
`--taa-thin-region W` (the lattice ARM crawl: on fragmented thin structure against sky the TAA stops discarding
its history and averages whole jitter cycles; gated off as screen motion rises), `--light-map-far-fade P0,P1`
(the hull light-map gain falls from 4 to the game's own brightness as an object gets small on screen: the
shimmering windows of distant stations), the sun-shadow fix for sectors with advertisement signs (always on),
`--volumetric-fog S` (sun-lit fog with shadow shafts, only in sectors where the game draws its own fog clouds;
**Ctrl+Alt+F9** toggles it, **Ctrl+Alt+F10** steps the strength 0.005 / 0.01 / 0.02 / 0.03 / 0.05, shown on the
FPS overlay) and the `--submit-phases` diagnostic. Every command is complete; run from the repository root.

**Session A reported (2026-09-20):** baseline run176 (plant, then distant-station save);
run177 with the three options (two plant F8 bursts: stationary, then camera moving);
run178 distant-station save only. The user confirms the arm crawl is fixed while the
ship and camera are stationary, but remains during camera rotation. The distant
station is acceptable with minor residual flicker, especially in camera motion;
retain far stabiliser 0.985 and light-map fade 40,110,1 as the recommended defaults
for the next checkpoint. Moving-arm crawl remains open; launcher defaults are unchanged.
Sessions B and C remain queued; the commands below retain the settings actually flown.

**Session A** (lattice arm + distant station; corvette save at the solar plant where the arm crawls, then the
fighter save with the distant station; two launches, one F8 each at the plant ≈ 1.5 GB):
1. Baseline:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --frame-end-stride 1 --capture-frames 32
```
2. Thin region + far stabiliser + light-map far fade (the intended new defaults):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --frame-end-stride 1 --capture-frames 32 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 40,110
```
In each: stand still at the view where the arm crawls, judge, **F8**; then drift slowly and turn the camera
slowly: is the crawl gone, do you see trails or smearing on the lattice or on ships passing in front of or behind
it? In launch 2 also load the distant-station view: are the windows calm now, is the station too dim, any blur
when turning? Report which of the three options (if any) you would not keep.

**Session B** (Argon Prime: shadows and fog; one launch, ≈ 5 minutes, F8 twice):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 40,110 --volumetric-fog 0.02 --taa-debug --capture-start 999999 --frame-end-stride 1 --capture-frames 32
```
In Argon Prime: (1) are sun shadows visible on stations and your ship now (they were off before), and do the
advertisement signs look clean (no dark speckle on them)? (2) Fog: step the strength with Ctrl+Alt+F10 and toggle
with Ctrl+Alt+F9: which strength looks right, do you see light shafts behind stations with the sun behind them,
any flicker/noise/banding in the fog, any glow or laser looking wrong; note the FPS overlay with the fog on and
off at the same view. **F8** once with fog at your preferred strength, sun behind a station; **F8** once more
after toggling fog off at the same view. (3) Fly through a gate into a sector without fog: does the fog fade out
within a couple of seconds? If the hotkeys collide with a game function, say which.

**Session C** (engine draw-submission timing, no judging; fighter save at the busy station ≈ 480-draw view):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 40,110 --submit-phases --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
Hold the busy view 60 s, **F8** once, note the FPS overlay.

Report frame-rate feel per session and the time into the session of each F8. If you have time afterwards: one fog
capture in a dense fog sector (Atreus' Clouds or Great Reef) with the Session B command.
