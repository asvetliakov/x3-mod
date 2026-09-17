# Outstanding user gameplay runs

Updated 2026-09-17 (run 38 queued). Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. Only open runs keep their instructions here; a completed
run keeps only its row in the table below. The installed build is described in [status](../status.md).
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
| 34 | Stall evidence: stderr capture, module identity, media-cue trace and cache | 4 | A1 run98: the stalling cue is id 2 = `mov\00002.dat` (MPEG-1 video), one failed build per frame at ~390 ms, GStreamer bursts aligned within 16 ms; native d3dx9_37 loads. A2 run99: cache on, one real attempt per 30 s, frames back to 7–9 ms p50 in the sector (stall gone, user confirmed); the exit-time "zero area" warning is pre-existing teardown noise (31 lines in run98 too). A3 run100/101: the v5 decoder runtime hangs the main loop on the first comm dialog (avatar video graph now builds and blocks in Wine's video path); v5 parked, cache is the fix. B pending |
| 35 | D3DX builtin vs native (no new DLL) | 1 | Completed as run103: visuals unchanged; identity line could not prove the builtin loaded; BeginPass 8.90 vs 6.64 µs/pass confounded by build; repeat as run 36 on one build |
| 36 | D3DX builtin vs native on one build (A1/A2); first sun shadows on original shading (B) | 0 | A1 run104 / A2 run105: builtin D3DX confirmed loaded (`wine_builtin=1`) and costs +2.1 µs per pass (BeginPass 8.71 vs 6.58 µs); native stays, experiment closed. B run106: no visible shadow; apply path proven correct offline (backlit view, only the own ship cast because casters were chosen by origin); fixed for run 37 |
| 37 | FEX memory-order relaxation (A1), wined3d command stream off (A2), station shadows side-lit with geometry casters (B), comm dialog with the H.264 avatar file and the blit witness (C) | 0 | A1 run107: FEX TSO off no change (within 1 %); A2 run108: CSMT off doubles the draw call (17.6 vs 8.8 µs), +46 % frame; both closed. B run109: first visible shadows (hull from station, ship on station when close); station-on-station missing because the 250-unit box around the camera admits only nearby casters (casters median 8 / max 49 of 93–930 routed, cap never binding); extent/depth/cap/bias options for run 38. C run110: comm dialog froze after a successful H.264 graph build with zero blits (witness), same stage as the MPEG-1 attempt; codec-independent Wine amstream/DirectDraw block; video parked; original file restored |
| 38 | Wide single shadow map at 4096 (A), own-ship near map baseline (A2), residual phases at the busy view (B), hull emitters bracket (C) | 0 | A run111 / A2 run112: shadows popped with camera pitch, wrong direction, own-ship flicker — root cause the sun read from PS c4 regardless of program (42 % of frames no sun ⇒ 250-unit fallback, 57 % a bogus (1,0,0) sun, 789 flips); plus extent-cache thrash, near-plane clipping, half-texel lookup; all fixed on main. B run113: one stamp only, `prepare` 6.36 µs/draw bundled; the technique lookup measured offline at 0.007 ms/frame, trampoline dropped. C run114: 2 of 12 programs fired; 71 % refusals were opaque routed hull draws (never ONE/ONE), F6 shared with the effects gain; own toggle and gain option on main |
| 39 | Cascades with positional sun and caster census (A), 50k far cascade (A2), retained casters live (B), hull emitters at gain 4 (C) | 0 | A run115 only: sun poll on every frame, no cap hit, replay 1.19 µs/draw, shadows 1–3 ms at rest, retention census clean through a gate jump and a load; a camera-following serrated shadow band — root cause the apply quad reconstructing receivers half a pixel off the RT2 sample (C1 over-bias 71.9 % → 1.3 % corrected), fixed on main; stations at 5.6/12 km unshadowed (C3 = 5 km reach). A2/B/C not flown, carried into run 40 |
| 40 | 30 km five-cascade set (A), 2048² maps (A2), corvette with the adaptive ladder + retained casters (B), hull emitters at gain 4 (C) | 4 | Queued: run40 candidate `d4d824a4…` from `d415264f` installed 2026-09-18 |

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md); they are provenance,
not rerun requests.


## 40. Five cascades to 30 km, 2048² A/B, corvette ladder, retained casters, hull emitters — queued (run40 candidate)

Installed: run40 candidate `d4d824a4…` from `d415264f` (see [status](../status.md)). Original hull
shading. New since run 39: the apply-quad half-pixel fix (the moving serrated band), the run116
flicker fix (back-face far cascades, verdict cycle), five cascades with a 30 km reach, caster pool control (importance drop order, per-cascade records; the
static-only rule stays available but is off after run116), own-ship-adaptive C0 with the
sliding ladder. Hotkeys: **Ctrl+Shift+F12** shadows at rest, **Ctrl+Shift+F4** hull
emitters, F6 effect gains, F8 capture.

Common prefix:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --shadow-sun-trace --frame-end-stride 1
```
Shadow set (add to every shadow session):
```sh
--shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096
```

**Session A** (fighter save, 4096² maps, 5–8 minutes):
```sh
<prefix> <shadow set> --shadow-cascade-sizes 4096,4096,4096,4096,4096 --shadow-retention-census
```
Same station as run115. Check: the serrated band is gone; shadows correct in direction and
placement near and far; stations at 5–12 km now shadowed (fly out to the distance of your
distance1/distance2 screenshots and look again); seams between cascades. Ctrl+Shift+F12
off/on twice at rest. F8 twice (one close, one on a distant station). Report frame-rate feel.

**Session A2** (same save and spot, 2048² maps, 2–3 minutes):
```sh
<prefix> <shadow set> --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census
```
Compare the hull self-shadow and the nearest station wall against A; one Ctrl+Shift+F12 A/B;
one F8. Say which you prefer.

**Session B** (corvette save; retained casters drawn; 3–5 minutes):
```sh
<prefix> <shadow set> --shadow-cascade-sizes 4096,4096,4096,4096,4096 --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5
```
Look at the corvette's self-shadow and its shadow on a station. Turn the camera so a
shadow-casting station part leaves the screen: its shadow must stay. Report any lingering
shadow after a caster moves or is destroyed, any crash/hang, and the frame-rate feel. The log
gives the measured hull radius, the slid cascade set and the chase-camera distance.

**Session C** (hull emitters, any station with position lights and signs, 1–2 minutes):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --frame-phases --hull-emitters --hull-emission-gain 4
```
Ctrl+Shift+F4 toggles only the hull emitters; one F8 on the station. Report whether position
lights and signs read as lights at gain 4 and whether anything else brightened.

