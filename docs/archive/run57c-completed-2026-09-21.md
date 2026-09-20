# Run57 C completed — 2026-09-21

Run205: user reports the flash fixed, with no smearing, old scenery or ghost trails during save loading and sector travel. Both heuristic cuts accepted default-off.

## Original instructions (completed; not a rerun request)


A returned run203 and B returned run204, with flashes reported in both.
B correctly disabled the missing-history trigger, but median-motion resets
remain near the reported events. [A/B instructions are archived](../archive/run57ab-completed-2026-09-21.md).

C disables both tested heuristics for this diagnostic: missing fraction bound 1
(strictly greater cannot occur) and a finite median-motion bound of 1e30 pixels
(effectively unreachable during gameplay; zero or infinity would not be
accepted as an off value). Other reset protections remain, including the
20-degree camera-cut policy, chase snaps and device recovery. TAA and per-pixel
history rejection remain active. This does not change launcher defaults or DLL.

Use the same save/view and repeat the pans from the recordings. Record normal
video, do not press F8, and report the preserved session path, flash timestamps
and any trails/ghosting. Fog stays off; no CrossOver debug tracing. Per-frame
logging remains enabled, so this is not an FPS comparison.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 X3M_MOTION_CUT_MEDIAN_PX=1e30 X3M_MOTION_CUT_MISSING=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --capture-start 999999
```

The affected launch dry-run is verified before this command is queued; no game
is launched by the agent. No rebuild or install is needed.

The separate 30–40 km fog-range work remains offline.
