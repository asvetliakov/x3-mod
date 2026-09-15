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
| 23 | Material fill 0.06 at the run-51 station | 1, optional brackets | Candidate pending; replaces the withdrawn LOD comparison (no visible LOD difference; default-off retained) |
| 24 | Chase reset-writer telemetry: gate jump and jumpdrive | Can share run23 | Queued after fill captures; prerequisite to view-restoration implementation |

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md); they are provenance,
not rerun requests.

## 23. Material fill at the run-51 station — candidate pending

This reuses the withdrawn LOD comparison number for the requested fill run.
**Wait for the candidate installation checkpoint before running this command.**
Fill stays default-off; this run selects 0.06 explicitly. Keep AO off and use the
same save, station, approach and sun direction as snapshot run51.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-bloom --linear-materials --material-fill 0.06 \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --fade-witness 1 --screen-emission \
  --voice-decoder /tmp/x3-wma-plugin-v4 \
  --capture-start 999999 --capture-frames 8
```

1. At the run-51 docking ring, take a screenshot and press F8 at about **350 m**
   (far, sun-averted clamps), then again at about **210 m**. Report both distances.
2. Say whether the far arm tips now read as surfaces, the cylinder's night side
   stays dark, and any adjacent material looks conspicuously different.
3. Auto exposure keeps its +1.5 EV ceiling. Run51 actually used **fixed EV 0**;
   for an appearance comparison at the same spot, Ctrl+Shift+F9 selects fixed
   EV 0, then capture once more. Tell us which screenshot uses fixed exposure.
4. If 0.06 is clearly too weak or too strong, close X3 and repeat the same
   far/near pair with only `--material-fill` changed to **0.04** or **0.10**.
   Report the snapshot path and the value for each session.

Analysis follows [fill acceptance](../architecture/fill-light.md#5-acceptance-run):
startup fill and routed-pair evidence; far-module dark fraction ≤0.10 and p10
≥0.045; cylinder mean ≤0.165; chroma difference ≤0.03; matched far-dark points'
near/far gain ≤2.0. Scene-linear readbacks precede exposure. Auto exposure must
be evaluated separately from the fixed-EV run51 baseline; no auto-EV difference
against that baseline is an adaptation measurement. The result informs the fill
default decision; it does not change the default automatically.

## 24. Chase reset-writer telemetry — after the fill captures

Can share run23's session **after** both station captures: start in rear chase
view, make one gate jump, reselect rear chase if it resets, then make one
jumpdrive jump. Do not press a view key during either transition. After each
arrival wait a few seconds, report whether it reset, then verify the normal view
keys still work. Report the snapshot path and the order of the two jumps.
The run23 command already enables the required transition telemetry; no view
restoration option is enabled.

Analysis: identify the mode-1 writer's `next_pc`, the update ordering against
cockpit `+0x1fc`, lifetime changes, and deltas in `+0x130`, `+0x160`, `+0xa8`
and `+0x1c0`. [The ratified restore policy](../architecture/chase-view-restore-and-hud-anchor.md)
is implemented only after these observations settle its prerequisite.
