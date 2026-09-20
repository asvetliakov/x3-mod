# Run53 completed and optional Run51 superseded

Historical instructions only. Run53 A/B were reported as run193/run194; followups are in Run54. Run51 was not flown and is superseded by replacement-media verification.

## 51. Media retry counter — same view, longer diagnostic interval

Run186 attributes periodic stutters to failed selector media-ID-2 construction.
This counter keeps run50's tracing and changes only `--media-cue-retry-s` from
30 to **3600**. The installed DLL is unchanged; launcher `--dry-run` passes.
This is a temporary diagnostic setting, not a new default or a decoder fix:
a failed legitimate selector cue can remain unavailable for up to an hour.
Successful media and other caller classes retain their existing behavior.

Start the same new game in Argon Prime and repeat the camera sweep/hold for
about 90 seconds. An initial failed build can still freeze; the question is
whether the recurring 30-second freezes disappear. Report the preserved run
and any later camera/view stalls. No F8, fog test or collision sequence is needed.

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --loop-phases --game-phases --game-phase-threshold-ms 20 --residual-phases --collide-memo --frame-end-stride 10 --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode perdraw --capture-start 999999 --capture-frames 2 --media-cue-trace --media-cue-cache on --media-cue-retry-s 3600
```

Verify one initial failed selector attempt followed by refusals, with no new
selector failure attempt during the short observation. A success, eviction,
clock error or incomplete tracing can invalidate that simple expectation.
Keep any remaining non-selector/render stalls separate. This counter does not
authorize a permanent hour-long retry policy.


<a id="53-spatial-fog-and-moving-lattice-state--ready-for-flight"></a>

## 53. Spatial fog and moving-lattice state — A and B reported, analysis open

Candidate qualification, installation and rollback are recorded in [status](../status.md).
Close the game between A and B; report the preserved session paths.
No media/collision/FOV experiment is included.

Session A reported as `/tmp/x3-bottleX3-run193` (1,109 referenced files).
All three 32-frame bursts and three selected state packets are present; a reviewed
host-checker correction admits the disabled extra clip-plane capacity. State
interpretation continues; no crawl-fix acceptance is implied.

Session B reported as `/tmp/x3-bottleX3-run194` (1,822 referenced files).
The user prefers density **1.50** over **1.0**, confirmed in logs as numeric
strength **0.03** versus **0.02**. The first three captures were in
Argon Prime, the first with fog off; a possible intervening capture is uncertain.
The last capture is in The Hole while moving the camera and shows native cards
appearing/disappearing, producing flicker. Replacement remains unaccepted pending
that defect's investigation. No repeat flight is requested from this report.
Triage finds five complete 32-frame bursts: 4833–4864 (bluewell, off),
6550–6581 (bluewell, 1.0×), 11077–11108 and 14822–14853 (bluewell, 1.5×),
and 31481–31512 (foggreenoutlands, 1.5×).
The [reproducible fog triage](../../verification/results/run53b-triage/main.md)
identifies camera cuts repeatedly disarming replacement and reopening the native
card warmup path. The final burst has two such windows, 31496–31506 and
31510–31512. A reviewed source correction on the integration branch removes
cut-only disarming; flight confirmation awaits the next qualified candidate.

A. Solar-plant lattice diagnostic: same pinned solar-plant save/view as run177. F8 at rest, then F8 during camera rotation, then F8 while translating the ship. Keep the arm in view during each 32-frame burst. This build measures post-route state; it does not claim a crawl fix.

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --lattice-state run177_panel_position_v1 --taa-debug --capture-start 999999 --frame-end-stride 10 --capture-frames 32
```

B. Spatial fog: begin in a light-fog sector, then a heavy-fog sector, then a clear sector. At 0.02 each family uses its authored density; the old homogeneous 0.01/0.05 anchors no longer apply. Look for shaped banks, gaps, and interior colour rather than whole-screen wash. Hold a view and toggle Ctrl+Alt+F9 to compare native cards with replacement. Observe FPS outside F8 capture bursts. F8 while translating through a visible cloud boundary and another F8 while rotating near thin foreground geometry. Note the sector for each F8 if possible. Check that clear-sector travel removes the volume and that toggling/reloading does not leave a layer behind.

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --volumetric-fog 0.02 --volumetric-fog-cards replace --sector-background --taa-debug --capture-start 999999 --frame-end-stride 10 --capture-frames 32
```

Report any loading or sector-switch hitch separately from FPS while flying. No new busy-view performance repeat is needed for this run.
