# Outstanding user gameplay runs

Archive with `python3 tools/analysis/archive_user_runs.py`.

Updated 2026-09-21 (Run56 accepted for media stability; Run57 station-flash correction accepted; fog range remains under investigation). Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. Only open runs keep their instructions here; a completed
run keeps only its row in the table below. The installed build is described in [status](../status.md).
Use the exact launcher paths in the commands below. Use the main-checkout launcher for the accepted baseline; no video-package
preflight is required. It handles the shared Wine lock and snapshots; no shell
function setup is needed. Runs 1–3 and 5–20 are complete (queue numbers; reader/DAT/adjacency fast
co-activation passed as snapshot run 19). Run 9 is saved as snapshot run 28.
Close X3 between runs and report completed numbers. After exit, the helper prints
a fresh `/tmp/x3-bottleX3-run<N>/` path containing that session’s log and referenced
captures, so later A/B runs cannot overwrite them. Vanilla/dry-run creates no snapshot.
Since 2026-09-14 the linear distance-fade route is on by default whenever
`--linear-materials --taa` are present ([region note](../architecture/linear-distance-fade-region.md),
"Default"); the queue commands below keep `--linear-distance-fade` spelled out,
which is the same resolved setting, and `--no-linear-distance-fade` opts out.

| Run | Purpose | Sessions | Status |
| --- | --- | ---: | --- |
| 80 A | A'-only build (hold-off chain removed) with the opt-in `--lod-occlusion all`, `--taa-thin-vote on`, `--fade-rt2-owner on` A/Bs, `--lod-switch-log` on the ODS while turning, bolt shape telemetry; Terran colours after the rebake | 1 | Queued 2026-09-24 evening; candidate pending the Run80 gate |
| 79 A | A' region hold on/off look + gpu-sync cost, Terran station LOD patch bursts (USC dock, SPP XL) size/distance, slot-06 control, at 5120x1440 (candidate df01f23b) | 4 | Completed 2026-09-24 (run299/300/302/303; run301 aborted): A' accepted (no visible difference on the lattice stand, pans, silhouettes, shards; mask 2.93 -> 1.54 ms with the x/y draws gone, box +0.23 ms, net -1.1 ms at 5120x1440); Terran patch works (status=patched, all 16 slot-06 bodies flag31=1, lod 1 below s/T_pad 1.0, coarse ODS confirmed); the coarse record lost its red plates (baker alpha rule, fixed 5aa645e3, rebake pending) and its ambient occlusion (engine LOD-0 gate: opt-in `--lod-occlusion all` ac381be7); one ODS part flickers under motion on both hold settings (inferred LOD pop; `--lod-switch-log` e8af9e46); `--terran-station-lod distance` not flown |
| 78 A | Dither A/B, S3 5 vs 16 taps, clean exit, slot-06 burst, scale 4 default at 5120x1440 (candidate ee3bbf88) | 1 | Completed 2026-09-24 (run295-298): dither accepted (rings gone on, back off, no frame-time cost); exit fixed (no fault, four exits; the refused row never written, expectation withdrawn); slot-06 bodies never switch to their coarse record (USC dock, Terran SPP XL at s/T_pad 0.17-0.36) while slot-05 bodies do: triage-deep open; TAA taps 5, no refusals, look accepted; bolts ok (84 shape refusals, 1.1 %, open); Run 77 D closed by this session |
| 74 A | Re-baked overlay: aspect thresholds + area-weighted texel rule, 22 bodies, busy sector, Run73 DLL | 1 | Completed 2026-09-23 (run277, flown on the Run75 DLL): accepted, FPS much better, almost no visible transition; one visible switch on the solar-panel arms of the plasma thrower factory (F8 bursts 35568–35575 coarse, 36781–36788 fine), triage in progress; overlay stays installed. |
| 77 B | Docked save load in fog with the alpha-test admission, Run77 DLL | 1 | Completed 2026-09-24 (run288): our fog after the fill latch, 357 ms from the menu (prefill adopted) / 608 ms on a same-sector reload (prefill not adopted); all 3,983 card rows admitted, no drop-out across the undock; closed at the accepted cold-start trade-off ([ledger](volumetric-fog.md#run-288-run-77-b-2026-09-24-docked-save-load-shows-our-fog-after-the-fill-latch-admission-fixed)). |
| 77 C | First 5120x1440 sessions under --gpu-sync-timing, --fog-march-scale 2 (run289) vs 4 (run290) | 2 | Completed 2026-09-24: fog_march 8.99 -> 2.68 ms (-6.3 ms), fog_route 13.6 -> 7.2, serialised dt 45.9 -> 39.0 ms, TAA stage 8.8 ms, frame GPU-bound at scale 2; the user sees transparent moving "oil rings" in the fog at scale 4: triage attributes them to the shaft-offset noise keyed per 4x4 march cell and shifted per frame (lattice vectors are multiples of 4 px, 2 px at scale 2), not to the 4-px interpolation (no seam at the sample columns); scale 2 stays the default, C2 queued ([triage](../verification/results/run289-290-march-scale/)). |
| 77 A | Fleet overlay across addon/05 + 06 (591 bodies), race sectors and a shipyard, Run76 DLL | 1 | Completed 2026-09-24 (run287, flown on the Run77 DLL): no issues, FPS good, no transitions seen; 14 slot-05 bodies drew their merged record (draws per frame equal the marker, ladders equal the overlay thresholds); busy greenvoid sector dt p50 14.2 ms / 156 draws against 14.7 ms / 160 in Run 74 A; no overlay, fog or cull errors; the TAA fold reports active; slot 06 unproven (no slot-06 body in view), A2 queued ([triage](../verification/results/run287-fleet-overlay/)). |
| 76 B/C/D | Bolts with the projectile cull exemption (B); docked save load in fog (C); --fog-far-bins 40 vs 24 under --gpu-sync-timing (D), Run76 DLL | 3 | B completed 2026-09-24 (run282): bolts fixed, accepted (54 exempt bullet nodes per frame while firing). C completed (run283): still engine fog when docked at load; the diagnostic names ALPHATESTENABLE=1 as the only differing state, fix in progress ([ledger](volumetric-fog.md#run-283-run-76-c-2026-09-24-docked-load-still-engine-fog-the-state-is-named)). D completed (run284/285/286): 24 far bins save only 0.2–0.5 ms of the 4.8 ms march (cost model said 0.8–1.15), no visible difference; 40 stays the default; step C (quarter-res march) is the lever ([note](../architecture/fog-gpu-cost.md#run-76-d-run284-40-bins-run285-24-bins-run286-default-2026-09-24-step-b-in-flight)). |
| 76 A | Overlay re-baked with the light-atlas bleed guard (panel tint fix), 22 bodies | 1 | Completed 2026-09-24 (run281, flown on the Run76 DLL with the share-gate re-bake): no transition seen at all, the factory panels keep their colour across the switch; accepted ([record](../verification/results/lod-overlay-batch/install-run76b/install.json)). |
| 75 A/B/C | Fog hand-over fixes + docked-load diagnostic (A); bolt visibility 3,12 (B); --gpu-sync-timing with the TAA and fog sub-boundaries (C), Run75 DLL | 3 | A completed 2026-09-23 (run278): new game and transits between fogged sectors show our fog immediately (accepted); the docked save load still shows engine fog, now pinned to `refusal=gate:states` (1,115 frames), fix in progress ([ledger](volumetric-fog.md#run-278-run-75-a-2026-09-23-hand-over-fixes-accepted-docked-load-pinned)). B completed (run279): bolts still vanish in third person; cause found: our 4 px small-part cull removes ~94 % of bullet nodes one frame after the muzzle (fix: exempt projectiles, in progress; [triage](../verification/results/run279-bolts/)). C completed (run280): TAA split = mask 2.1 / resolve 2.1 / box 1.2 / copy 0.45 ms in the busy sector, motion adds 0.4 ms in the resolve only; fog march 4.5–4.7 ms net, repair writes < 0.01 % of pixels ([ledger](gpu-sync-timing.md#run-280-run-75-c-2026-09-23-the-taa-and-fog-splits-at-19201080)). |
| 74 B | Fog shadow pass GPU cost with --gpu-sync-timing, Ctrl+Shift+F11 on/off/on | 2 | Completed 2026-09-23: run275 flew with the pass disabled at launch (toggle inert by design; reproduces the Run 274 route figures); run276 with `--fog-shadow-pass`: the pass costs about 0.3 ms GPU (fog_route 4.79 on vs 4.42–4.53 off), dt unchanged; stays off ([ledger](fog-shadow-pass.md#run-276-run-74-b-2026-09-23-fog-shadow-pass-gpu-cost-at-19201080)). |
| 73 B/C | Fog hand-over, --music-keep alt-tab, bolt footprint (B); --gpu-sync-timing stand (C), Run73 DLL | 2 | B completed (run273): music keep accepted (6 alt-tabs, all skip_all); docked view in flight shows our fog; open: same-family gate jump 3.5 s of engine fog (residency drop + ramp), confirmed prefill still re-filled after arrival (1.15 s), docked save load keeps engine fog until undock (cards armed but refused, 4.5 s), new-game cold start 1.34 s (0.7 s ordering + 0.6 s fill latch); bolts unchanged (rule leaves half-length >= 3 px untouched; no additive admission after the new game) ([triage](../verification/results/run273-fog-bolts/), fixes in progress). C completed (run274): per-pass GPU table ([note](../architecture/engine-frame-time.md#run-274-gpu-per-pass-cost-at-19201080-run-73-c-2026-09-23)); proxy 11.7 ms serialised vs engine 4.7 ms, fog_route 4.4 / taa 2.9 ms. |
| 73 A | Merged-LOD batch overlay of the flown sectors (19 bodies) in a busy sector, Run72 DLL | 1 | Completed 2026-09-23 (run272): FPS better, no oddity reported; overlay bodies draw the merged record at 2–4 draws; 234/217 draws per burst come from texel_floor-refused tech stations and the gate (36–38 / 14–32 each), single-LOD pipes (42), refused signs (~35), the spacedock above its switch and the outpost with the camera inside its sphere ([triage](../verification/results/run272-batch-busy/burst_draws_out.txt)); baker per-tile clamp in progress. |

## Run 80 (open)

**Run 80 A (queued 2026-09-24 evening; Run80 candidate pending: A'-only build bb3a691f or later).** One session at
5120x1440, several launches, the stand command below unchanged (`--taa-region-hold` no longer exists; the launcher
refuses it). Please report per launch:

1. **Baseline** (launch 1, defaults): the A'-only build on the lattice stand and under a slow pan: same look as Run 79 A's
   hold-on launch; nothing new expected. One F8 burst at the stand.
2. **Occlusion patch** (launch 2, add `--lod-occlusion all`): the Terran Orbital Defence Station at the run297 "a little
   far" distance (coarse record): expect the ambient-occlusion shading back on the coarse record (darker panels, closer to
   the fine model), the log row `lod_occlusion site=004c34f7 status=patched write=atomic`; watch for a hitch or stutter the
   first time a distant station comes into view (the occlusion map loads at draw time), and look at any distant vanilla
   station for misplaced occlusion (their UV2 is about 1 % off). One F8 burst on the coarse ODS.
3. **LOD switch log** (same launch, add `--lod-switch-log`): near the ODS, turn the ship slowly with the station in the
   right third of the screen and say whether the part flicker is a single pop per crossing or continuous. The
   `lod_switch` rows settle whether the flicker is the record 0 <-> coarse pop.
4. **Thin vote** (launch 3, add `--taa-thin-vote on`; needs the lane, already in the command): the lattice stand at rest
   and under a slow pan, then a large near station: any change in crawl on hull-backed arms, any ghosting on voted
   panels during pans, loading time on sector entry; `--gpu-sync-timing` optional for the `taa_mask` row. One F8 burst.
5. **Fade owner** (launch 4, add `--fade-rt2-owner on`): the run214 stand (two distant stations in the fade band), slow
   vertical pan 3-9 px/frame: flicker on the stations under the pan, trail behind the silhouette, a pop at the band
   edges on approach. One F8 burst at rest and one during the pan.
6. If bolts are fired anywhere: nothing to do; the `bolt_footprint_refused` rows now carry the sub-clause.
7. **After the rebake** (if done before the flight): the coarse ODS red plates; if not, ignore colours.
8. Exit through the menu.

Stand command (Run 73 A's, unchanged):

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --object-bounds-log --cull-census
```

To remove the overlay: delete `addon/05.cat`, `05.dat`, `05.x3m-lod.json` and `x3m-lod-batch*.json/txt`; the originals are untouched.

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md) and the
[run 48 archive](../archive/run48-completed-2026-09-20.md); they are provenance,
not rerun requests.


<a id="51-media-retry-counter--same-view-longer-diagnostic-interval"></a>
<a id="53-spatial-fog-and-moving-lattice-state--ready-for-flight"></a>
Run51/53 instructions are [archived](../archive/run53-completed-2026-09-20.md); they are not rerun requests.

Run 80 A is the only queued run. Completed instructions for Runs 73-78 are in the [archive](../archive/user-runs-completed.md).
