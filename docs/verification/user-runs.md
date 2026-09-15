# Outstanding user gameplay runs

Updated 2026-09-15. Run 17 crypto acceptance and the first-person/chase
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
| 26 | Original hulls with cheap HDR emitters, chase view restore, elevated-camera reticle, replay candidates, loading intervals (A); optional linear sun-lane refusal buckets (B) | 0 | Open: candidate `5726a37b…` installed 2026-09-15 evening; command below |

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md); they are provenance,
not rerun requests.

## 26. Cheap HDR emitters, chase view restore and replay candidates — open

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
