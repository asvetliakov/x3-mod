# Outstanding user gameplay runs

Updated 2026-09-16. Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. The installed build is described in [status](../status.md).
From the repository root, paste a `./x3run` command below. The executable
[launcher script](../../x3run) handles the shared lock and log snapshots; no shell
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
| 1 | Chase aiming/framing + reader/adjacency verification | 0 | Accepted as run 18 |
| 2 | Sharpen/shimmer + camera cuts with TAA | 0 | Merged into run 6 |
| 3 | Automatic exposure + bloom off/on | 0 | Completed: A run 24; B run 25 exposed bloom initialization failure |
| 4 | Vanilla double-cursor/menu-bar comparison | 0 | Completed 2026-09-14 (vanilla, no snapshot): the double cursor reproduces in vanilla after alt-tab when the desktop cursor is moved outside the window before returning; vanilla behaviour, not a proxy regression |
| 5 | Reader/adjacency fast modes | 0 | Accepted as run 19 |
| 6 | Linear hull materials off/on plus sharpen/cuts at fixed exposure | 0 | Completed: A run 20, B runs 21–23; analysis/quality follow-ups remain |
| 7 | Fixed/automatic exposure and bloom toggles, central chase HUD and selection timing | 0 | Completed as run 26; follow-ups combined into run 8 |
| 8 | Restored glow, milder exposure and native selection-stutter trace | 0 | Completed as run 27 |
| 9 | Stronger glow and selection/voice timing | 0 | Completed as run 28 on source `d9413fc` |
| 10 | Target-name speech with the opt-in WMA decoder | 0 | Superseded: load hang fixed by the DMO fallback hook; speech accepted as run 18 |
| 11 | Fade region route and alpha-tested cutout, combined | 1 | Completed as user run 11, snapshot run36 |
| 12 | Voice load-hang Wine trace witness (no new build) | 0 | Completed as user run 12, snapshot run37 (trace `/tmp/x3-witness-quartz.log.z`, 5.0 GB) |
| 13 | Target-name speech with the decoder plugin and the DMO fallback hook | 0 | Attempted as run 38 (execute-access fault, fixed); retried and completed as run 16 |
| 14 | Station source-over linear route, fade region and shimmer trace, combined | 0 | Completed as user run 14, snapshot run39 (witness clean, port composed, darkening persists) |
| 16 | Run 13 retry: target-name speech with the decoder plugin and the fixed DMO fallback hook | 0 | Completed as user run 16, snapshot run41: loads, speech works, crackle under investigation |
| 17 | Bullet bound after near-plane clipping, packed_sample brightness | 0 | Completed as user run 17, snapshot run42: 100 % bound, witness clean, dimmer unresolved (centre sample) |
| 18 | Voice crackle fix: decoder plugin v4 (no new DLL) | 0 | Completed as user run 18, snapshot run46: no crackle, voice fine |
| 19 | Combined: AO off/on (Ctrl+Shift+F11), bullets at gain 1, cutout shimmer fix, same-port far/near pair | 0 | Completed as user run 19, snapshot run47: cutout exemption holds (`reason=3` 0.01 %), bolts accepted at gain 1; AO runs but is invisible at 2 m, port darkening and asteroid triangle dropout still open |
| 15 | Screen emission on bullets (packed policy 8 in the region bracket) | 0 | Completed as user run 15, snapshot run40: witness clean, 50 % of bullet draws refused (w ≤ 0), near-fullscreen brackets; bound fix in progress |
| 20 | Asteroid prepass jitter, port and ship far/near pairs, step D bullets, AO radius 20, loading markers | 0 | Completed as user run 20, snapshot run48: asteroid triangle dropout fixed and accepted, step-D brackets no longer fullscreen, loading markers fired; AO ran in debug view only, port darkening still open |
| 21 | AO appearance at a readable footprint (`--ao-radius 100`, no debug view), bullet witness every frame, vanilla port approach | 1 | Completed as user run 21, snapshot run49 (session A only): the fade witness is clean on the firing frames and the bolts are accepted, AO is invisible at radius 100 and is now default-off, a new station-section jitter at ~4.7 km is under diagnosis; session B (vanilla port approach) has not been reported |
| 22 | LOD scale 2×, fade-band trembling fix, docking-port screenshot pair | 0 | Completed as user run 22, snapshot run51: trembling gone, LOD 2× applied, module darkening owned by point-light range |
| 23 | Material fill 0.06 at the run-51 station | 0 | Completed as snapshot run54; fill visibly works. User subsequently chose default 0.03 and Auto EV ceiling +1.0; defaults selected and applied |
| 24 | Chase reset-writer telemetry: gate jump and jumpdrive | 0 | Gate portion completed as run56; user confirms reset. Jumpdrive not run (no suitable save); cross-recreation diagnostics prepared for run25 |

| 25 | Consolidated loading, gate identity, forward HUD and sun-lane diagnostics | 0 | Completed as run60: forward HUD aligned, centre preferred; gate reset; trace analysis in progress |
| 26 | Original hulls with cheap HDR emitters, chase view restore, elevated-camera reticle, replay candidates, loading intervals (A); optional linear sun-lane refusal buckets (B) | 1 | Session A completed as run65 (60 files): bolts brighter and liked; engines unchanged; loading possibly faster; **camera still reset at the gate**. Session B completed as run66 (55 files). Run65 triage: restore refused at the consume seam on the ref cell (2/2), engine gain refused on separate-alpha blend, loading stall 21.5 s again, replay predicates all pass; follow-ups in flight |
| 27 | Next candidate: corrected chase restore, engine gain on separate-alpha, point-light root admission, original-program fill baseline, depth replay, mip bias/sharpen trial (A); linear sun lane after the state-gate fix (B) | 1 | Session A completed as run68 (108 files): engines brighter, halo around bolt impacts (weapon-effect sprites now gained), mip bias/sharpen kept as defaults, chase restore consumed on transits 1–2 but the view still reset on the return transit and no transfer on 3–4, docking modules partly dark with the point-light option (no per-node telemetry yet); A2 without the option in progress |
| 28 | Next candidate: restore re-arm fix, emitter gain split (engines vs weapon effects), original-program fill A/B, point-light telemetry (opt-in) | 0 | Completed as snapshots run74–run80 (session A) and run81 (session B), DLL `2b0969e5…` from `a26eb9b`: halo identified as bloom of the >1.0 bolts (bloom working-set 21 vs 13 MB is the sharpen-stage buffer, not resolution); chase restore consumed on both run78 transits, the centre-then-jump was the camera hook refusing the unbound view phase (37 and 13 frames), fixed `e4fd22a`; original fill 0.05 accepted visually, no clean frame-time A/B; engine-family gain admits draws, effect family 0; session B sun lane `available=0` on all 2,123 frames, fixed `e62722a` |
| 29 | Emitter hotkeys, bolt alpha, chase pose, frame timing, sun lane | 3 | Completed as run83 (A), run84 (A2 profiler), run85 (B): engines unchanged (gain refused screen blend), halo persisted (bloom amplitude), chase pose fixed, profiler blind under FEX, sun lane available 4577/4695 |
| 30 | Single emission gain, bloom source clamp, frame-time split | 2 | Session A run87 and B run88 received: engines respond, halo accepted at clamp 1.0, no cutout draws yet (B to repeat at an Argon industrial station), frame split: state hooks 8.9 ms of a 28.5 ms busy frame |
| 31 | Frame split, engine phases, lighter proxy | 2 | Session A run89 and B run90 received: busy frame 37.5 ms at 987 draws is 87 % engine view submission (63 state calls per draw), scene update 65 µs; lane available on all 16,041 frames, still zero cutout draws (third time) |
| 32 | Hybrid unhook, draw and state counters | 4 | A1 run91 (redundancy 95/99/40 %, batchability 5 %), A2 run92 (busy frame 37.5 to 26.5 ms unhooked), B run93 (cutout pairs draw everywhere, lane admission unobservable; slow sector is `pre_render` 98 % of a 420 ms frame); C queued with `--game-phases` |
| 33 | Pass phases, loop-region split, cutout lane telemetry | 3 | A run95 (view submit: draw 43 %, BeginPass 33 %, engine between passes 22 %), B run96 (stall 99.8 % in the per-sector object pass `0x0045b720`, one sector; GStreamer criticals repeat during it), C run97 (cutout draws admitted through the tested-opaque arm, ~90/frame with the lane share; linear materials + lane +3 ms) |
| 34 | Stall evidence: stderr capture, module identity, media-cue trace and cache | 3 | Drafted; candidate pending |

## 34. Stall evidence: stderr capture, module identity, media-cue trace and cache — drafted

Installed: DLL `RUN34HASH` from `RUN34COMMIT` (see [status](../status.md)). This
build tees Wine's stderr into the session directory (`launcher-stderr.log`,
UTC-prefixed) with a `clock_anchor` and `qpc=` on every window line so a
GStreamer burst maps to a frame, logs `loaded_module` for the D3D9 backend and
`d3dx9_37.dll`, `--media-cue-trace` (one line per media-cue graph build attempt at
`0x00498140`: cue id, file, result, attempts per frame) and the media-cue
negative cache (a cue whose graph build failed is not retried every frame;
retried on sector change and after a fixed interval; default on after this
run, `--media-cue-cache on|off`). Appearance unchanged.

**Session A1** (the slow sector, trace only):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --frame-phases --pass-phases --game-phases --loop-phases --media-cue-trace --media-cue-cache off --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --shadow-replay-candidates --shadow-replay-depth --loading-intervals --capture-start 999999 --capture-frames 8
```

Fly to the stalling sector, stay 30 s while it is slow, quit. The trace names
the cue, its file and the result of every build attempt; the stall should
still be there. Wine's stderr is now in the session directory.

**Session A2** (the slow sector, cache on): the same command with
`--media-cue-cache on` (the default once this run confirms it). Same sector,
30 s, quit. The stall should be gone and the trace should show one failed
attempt per cue followed by cached refusals; say whether the sector's music
or ambient sound is missing and whether anything else changed.

**Session A3** (decode fix, only if the v5 runtime is reported built): session
A1's command with `--voice-decoder /tmp/x3-wma-plugin-v5` instead of v4 and
`--media-cue-cache off`. Same sector, 30 s, quit. If the runtime decodes the
cue, the trace shows the build succeeding, the stall is gone without the
cache, and the sector's music or ambient sound plays; say whether speech
still works.

**Session B** (DXVK experiment, no proxy change): in CrossOver, enable the
DXVK backend for the X3 bottle (bottle settings, D3D9 via DXVK), then run
run 33 session A's command in the busy view for 30 s and quit. The
`loaded_module` line says which D3D9 backend the proxy actually forwarded to,
and the `pass_phases` draw µs per pass says whether the draw call got cheaper.
Switch DXVK back off afterwards unless it wins.

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md); they are provenance,
not rerun requests.


## 27. Corrected restore, engine gain, point-light admission, depth replay — session A completed (run68)

Installed: DLL `215d8fbe…` from `46dc822` (see [status](../status.md)). Original hulls, new defaults (camera 0.5°/0.50, EV ceiling 1.3).

**Session A** (from the repository root):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --taa-mip-bias -0.5 --taa-sharpen 0.75 --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --emission-source-gain 2 --point-light-root-admission --shadow-replay-candidates --shadow-replay-depth --loading-intervals --capture-start 999999 --capture-frames 8
```

1. Load the usual save. Engines: are they now visibly brighter and bloomed
   (the gain admitted no draws in run 26; that is fixed)? Bolts as before?
2. Mip bias -0.5 and sharpen 0.75 are on for the first time on this build:
   sharper textures at distance, any shimmer or over-sharpening? Say keep/drop.
3. Rear chase view, fly through a gate: does the view stay in rear chase?
4. Fly to the station of run 22/51 (docking modules that went black at range)
   and press F8 once at about 1.2 km facing the modules, once close. With the
   point-light option on the modules should stay lit at range.
5. Optional short session A2: same command without `--point-light-root-admission`,
   same station spot, one F8 at the same distance, for the frame-time and
   appearance comparison.

**Session B (optional, linear materials, diagnostics only):** the same command
plus `--linear-materials --linear-distance-fade --sun-shadow-lane`, a minute of
flight near a station. The sun-lane frames should now report `available=1`.

## 26. Cheap HDR emitters, chase view restore and replay candidates — completed (run65/run66)

Installed: DLL `5726a37b…` from `f2b7406` (see [status](../status.md)). No linear
materials: this is the user's preferred original hull look. One session A covers
everything except the sun-lane buckets; session B is optional.

**Session A** (from the repository root):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --emission-source-gain 2 --shadow-replay-candidates --loading-intervals --capture-start 999999 --capture-frames 8
```

Please do, in this order, and report what you saw:

1. Load the usual save (the one from run 20/48 if possible, for the loading
   stall attribution). Note whether loading felt like the 21.7 s stall.
2. Fire at something and look at engines and bolts: are they clearly brighter
   and bloomed than before, and does anything look wrong over a bright
   background (sun, nebula)? Say whether gain 2 is too much, right, or too little.
3. In rear chase view, fly through a gate. Does the view stay in rear chase on
   the other side (no reset to the default view)? If it resets, say so; the log
   tells us why.
4. Optional second short session with a raised camera instead of a tilted one:
   add `--chase-pitch-down-deg 0.5 --chase-offset-y 0.50`. Does the centre
   reticle now sit on where the bolts go, and is the top view still acceptable?

**Session B (optional, linear materials only for the diagnostic):** the sun-lane
refusal buckets need converted materials; if you have time, run the same
command plus `--linear-materials --linear-distance-fade --sun-shadow-lane` for
a minute of ordinary flight near a station and quit. Nothing to look at.

## 23. Material fill at the run-51 station — completed

Completed as `/tmp/x3-bottleX3-run54`. The user reports brighter hulls; fixed-EV0
threshold evidence supported 0.06 provisionally. The user subsequently chose
fill **0.03** and Auto EV ceiling **+1.0** as defaults; the defaults are now applied. No further preference bracket is requested. Full instructions are preserved under
[Run 23 in the completed-run archive](../archive/user-runs-completed.md#23-material-fill-at-the-run-51-station--completed).

## 24. Chase reset-writer telemetry — completed gate portion

Received as `/tmp/x3-bottleX3-run56`; camera reset confirmed. No jumpdrive save
is available. The shared static warp path is documented; another jumpdrive
attempt is not a prerequisite for run25. Original instructions are archived
under [Run 24](../archive/user-runs-completed.md#24-chase-reset-writer-telemetry--gate-completed-as-run56).

## 25. Consolidated diagnostic — completed

Received as `/tmp/x3-bottleX3-run60` (60 referenced files). The user reports
that the forward crosshair/distance group is aligned, but prefers the screen
centre default and may revisit forward anchoring after future camera tuning.
Centre remains the default; forward remains opt-in. The gate transition still
reset the view; this build records diagnostics and contains no restoration.
Gate identity, loading intervals and sun-lane evidence are under analysis.
The original command is preserved in the
[Run 25 archive](../archive/user-runs-completed.md#25-consolidated-diagnostic--completed-as-run60).
No repeat is requested at this checkpoint.
