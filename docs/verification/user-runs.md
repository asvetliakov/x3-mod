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
| 82 A | FOV remap (menu in game units), sun flare fix, chase compensation, S4 half default, cutout ownership: defaults look + menu/sun/chase checks; stabiliser 0 retry; thin-region source vote A/B, at 5120x1440 (candidate d1a4e360) | 0 | Queued 2026-09-25 00:26; Run82 DLL `cd8ef8e4…` installed 00:26, fleet overlay install-fleet3 unchanged |
| 81 A | Run81 defaults (thin vote, fade owner, occlusion `all`, FOV 58.7155 vertical, age programs) + alpha casters look; S4 half box A/B; sentinel stabiliser off with the owner on, at 5120x1440 (candidate 9e1645be) | 3 | Completed 2026-09-24 (run309 defaults + alpha casters + gpu-sync, run310 S4 half, run311 stabiliser 0): ODS underside transition fixed, alpha casters clean (refused_pool 0); S4 half accepted (box 2.36 -> 1.40 ms, TAA -1.2 ms; default from Run 82); FOV in effect until the in-game menu (starts from its own 90, left at 100) overrode it -> the remap model (Run 82); at F 100 the sun vanished near the view centre = engine 32-bit overflow in the lens-flare collector (fixed in Run 82); chase ship 1.333x larger (compensated in Run 82); the cull read a HUD projection (fixed); stabiliser 0 shimmers on the fog-band plants: their alpha-tested cutouts were unowned (owned from Run 82; stabiliser stays 0.7 until the retry) |
| 80 A | A'-only build (hold-off chain removed) with the opt-in `--lod-occlusion all`, `--taa-thin-vote on`, `--fade-rt2-owner on` A/Bs, `--lod-switch-log` on the ODS while turning, bolt shape telemetry; Terran colours after the rebake | 5 | Completed 2026-09-24 (run304 baseline, run305 occlusion + switch log, run306 thin vote, run307 fade owner, run308 gpu-sync baseline): occlusion patch works (every LOD>0 ODS draw binds the map, no hitch; made the default `all`); thin vote and fade owner accepted as defaults (no visible issue, no GPU pass cost within 50 us; A'-only TAA 6.78 ms at 5120x1440, mask 1.49 / box 2.41 / resolve 2.68); ODS flicker = one LOD pop per crossing without hysteresis; coarse red plates 1.41x brighter = baker's synthesised material constants (user accepts, no shading classes); underside transition = alpha-tested casters excluded from the shadow replay (opt-in `--shadow-alpha-casters` in Run81); sentinel stabiliser removal needs the S = 0 launch (Run 81 A) |
| 79 A | A' region hold on/off look + gpu-sync cost, Terran station LOD patch bursts (USC dock, SPP XL) size/distance, slot-06 control, at 5120x1440 (candidate df01f23b) | 4 | Completed 2026-09-24 (run299/300/302/303; run301 aborted): A' accepted (no visible difference on the lattice stand, pans, silhouettes, shards; mask 2.93 -> 1.54 ms with the x/y draws gone, box +0.23 ms, net -1.1 ms at 5120x1440); Terran patch works (status=patched, all 16 slot-06 bodies flag31=1, lod 1 below s/T_pad 1.0, coarse ODS confirmed); the coarse record lost its red plates (baker alpha rule, fixed 5aa645e3, rebake pending) and its ambient occlusion (engine LOD-0 gate: opt-in `--lod-occlusion all` ac381be7); one ODS part flickers under motion on both hold settings (inferred LOD pop; `--lod-switch-log` e8af9e46); `--terran-station-lod distance` not flown |
| 78 A | Dither A/B, S3 5 vs 16 taps, clean exit, slot-06 burst, scale 4 default at 5120x1440 (candidate ee3bbf88) | 1 | Completed 2026-09-24 (run295-298): dither accepted (rings gone on, back off, no frame-time cost); exit fixed (no fault, four exits; the refused row never written, expectation withdrawn); slot-06 bodies never switch to their coarse record (USC dock, Terran SPP XL at s/T_pad 0.17-0.36) while slot-05 bodies do: triage-deep open; TAA taps 5, no refusals, look accepted; bolts ok (84 shape refusals, 1.1 %, open); Run 77 D closed by this session |
| 77 B | Docked save load in fog with the alpha-test admission, Run77 DLL | 1 | Completed 2026-09-24 (run288): our fog after the fill latch, 357 ms from the menu (prefill adopted) / 608 ms on a same-sector reload (prefill not adopted); all 3,983 card rows admitted, no drop-out across the undock; closed at the accepted cold-start trade-off ([ledger](volumetric-fog.md#run-288-run-77-b-2026-09-24-docked-save-load-shows-our-fog-after-the-fill-latch-admission-fixed)). |
| 77 C | First 5120x1440 sessions under --gpu-sync-timing, --fog-march-scale 2 (run289) vs 4 (run290) | 2 | Completed 2026-09-24: fog_march 8.99 -> 2.68 ms (-6.3 ms), fog_route 13.6 -> 7.2, serialised dt 45.9 -> 39.0 ms, TAA stage 8.8 ms, frame GPU-bound at scale 2; the user sees transparent moving "oil rings" in the fog at scale 4: triage attributes them to the shaft-offset noise keyed per 4x4 march cell and shifted per frame (lattice vectors are multiples of 4 px, 2 px at scale 2), not to the 4-px interpolation (no seam at the sample columns); scale 2 stays the default, C2 queued ([triage](../verification/results/run289-290-march-scale/)). |
| 77 A | Fleet overlay across addon/05 + 06 (591 bodies), race sectors and a shipyard, Run76 DLL | 1 | Completed 2026-09-24 (run287, flown on the Run77 DLL): no issues, FPS good, no transitions seen; 14 slot-05 bodies drew their merged record (draws per frame equal the marker, ladders equal the overlay thresholds); busy greenvoid sector dt p50 14.2 ms / 156 draws against 14.7 ms / 160 in Run 74 A; no overlay, fog or cull errors; the TAA fold reports active; slot 06 unproven (no slot-06 body in view), A2 queued ([triage](../verification/results/run287-fleet-overlay/)). |
| 76 B/C/D | Bolts with the projectile cull exemption (B); docked save load in fog (C); --fog-far-bins 40 vs 24 under --gpu-sync-timing (D), Run76 DLL | 3 | B completed 2026-09-24 (run282): bolts fixed, accepted (54 exempt bullet nodes per frame while firing). C completed (run283): still engine fog when docked at load; the diagnostic names ALPHATESTENABLE=1 as the only differing state, fix in progress ([ledger](volumetric-fog.md#run-283-run-76-c-2026-09-24-docked-load-still-engine-fog-the-state-is-named)). D completed (run284/285/286): 24 far bins save only 0.2–0.5 ms of the 4.8 ms march (cost model said 0.8–1.15), no visible difference; 40 stays the default; step C (quarter-res march) is the lever ([note](../architecture/fog-gpu-cost.md#run-76-d-run284-40-bins-run285-24-bins-run286-default-2026-09-24-step-b-in-flight)). |
| 76 A | Overlay re-baked with the light-atlas bleed guard (panel tint fix), 22 bodies | 1 | Completed 2026-09-24 (run281, flown on the Run76 DLL with the share-gate re-bake): no transition seen at all, the factory panels keep their colour across the switch; accepted ([record](../verification/results/lod-overlay-batch/install-run76b/install.json)). |
| 75 A/B/C | Fog hand-over fixes + docked-load diagnostic (A); bolt visibility 3,12 (B); --gpu-sync-timing with the TAA and fog sub-boundaries (C), Run75 DLL | 3 | A completed 2026-09-23 (run278): new game and transits between fogged sectors show our fog immediately (accepted); the docked save load still shows engine fog, now pinned to `refusal=gate:states` (1,115 frames), fix in progress ([ledger](volumetric-fog.md#run-278-run-75-a-2026-09-23-hand-over-fixes-accepted-docked-load-pinned)). B completed (run279): bolts still vanish in third person; cause found: our 4 px small-part cull removes ~94 % of bullet nodes one frame after the muzzle (fix: exempt projectiles, in progress; [triage](../verification/results/run279-bolts/)). C completed (run280): TAA split = mask 2.1 / resolve 2.1 / box 1.2 / copy 0.45 ms in the busy sector, motion adds 0.4 ms in the resolve only; fog march 4.5–4.7 ms net, repair writes < 0.01 % of pixels ([ledger](gpu-sync-timing.md#run-280-run-75-c-2026-09-23-the-taa-and-fog-splits-at-19201080)). |

## Run 82 (open)

**Run 82 A (queued 2026-09-25 00:26; Run82 DLL `cd8ef8e4…` from d1a4e360 installed 00:26; fleet overlay install-fleet3 unchanged).**
New in this build: FOV remap (`--fov N` in game units, default 90 = "90 horizontal on 16:9", the in-game menu 70..100 now works in
the same units; `--fov game` = vanilla), sun lens-flare overflow fix (`--sun-flare-fix`, on), chase camera FOV compensation
(`--chase-fov-compensate`, on), S4 half box as the default, the small-parts cull reading the scene projection, fade ownership of
alpha-tested station cutouts (with the owner, on), and the opt-in `--taa-thin-region-source both|screen|vote`. Three launches at
5120x1440; each command is the stand command below plus the flags shown. Please report per launch, and name the sector of each stand:

1. **Defaults, FOV, sun, chase** (launch 1: stand command + `--gpu-sync-timing`). In order:
   a. Chase view: the fighter should sit at its Run 80 size again (the boom is 1.33x longer); then open the in-game FOV menu: it starts
      at 90 (= today's look); step to 80 and 100 and back to 90: each step must change the view smoothly and 90 must return the same
      picture; the ship keeps its size at every setting.
   b. Sun: at the menu's 100, look straight at the sun and around it: the disc and flare must stay visible near the view centre (Run 81
      A launch 1 lost it there); then back to 90 and the same check.
   c. TAA: the lattice stand at rest and under a slow pan (the S4 half box is now the default; any halo on a laser or engine trail over
      sky beyond 2 px); the two solar plants in the fog band (run311's stand) under a slow vertical pan with the stabiliser at its
      default 0.7: no change expected.
   F8 bursts: one at the lattice stand, one on the fog-band plants at rest. Rows: `fov ... status=patched`, `fov_confirm ... match=1`,
   `sun_flare_fix site=0047e391 status=patched`, `chase_fov_compensate factor=1.333`, `motion_output_taa_box_resolution ... default=1`,
   `cull_small_parts_value ... source=scene`, `fade_route_frame ... fade_tested=` > 0 on the plants, `fade_owner_masked=0`.
2. **Stabiliser off, retry** (launch 2: stand command + `--taa-sentinel-stabiliser 0`): the same two solar plants in the fog band, slow
   vertical pan 3-9 px/frame, then at rest: the Run 81 A launch 3 shimmer on the panels and trusses should be gone now that the
   cutouts are owned; then the lattice. One F8 burst at rest and one during the pan on the plants. Decides the stabiliser's removal.
3. **Thin-region source** (launch 3: stand command + `--gpu-sync-timing --taa-thin-region-source vote`): the lattice stand at rest and
   under a pan, hull-backed arms and thin masts: any crawl or shimmer that launch 1 did not have. One F8 burst at the stand. The
   `taa_mask_tests` row against launch 1 (about 0.1 ms less expected) and the look decide whether the screen-space test can go later.
4. Exit through the menu after each launch.

Stand command (Run 73 A's minus the two age-program flags, which are defaults now):

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --object-bounds-log --cull-census
```

To remove the overlay: delete `addon/05.cat`, `05.dat`, `05.x3m-lod.json` and `x3m-lod-batch*.json/txt`; the originals are untouched.

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md) and the
[run 48 archive](../archive/run48-completed-2026-09-20.md); they are provenance,
not rerun requests.


<a id="51-media-retry-counter--same-view-longer-diagnostic-interval"></a>
<a id="53-spatial-fog-and-moving-lattice-state--ready-for-flight"></a>
Run51/53 instructions are [archived](../archive/run53-completed-2026-09-20.md); they are not rerun requests.

Run 82 A is the only queued run. Completed instructions for Runs 73-81 are in the [archive](../archive/user-runs-completed.md).
