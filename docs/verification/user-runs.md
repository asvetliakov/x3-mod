# Outstanding user gameplay runs

Updated 2026-09-14. Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. The installed build is described in [status](../status.md).
From the repository root, paste a `./x3run` command below. The executable
[launcher script](../../x3run) handles the shared lock and log snapshots; no shell
function setup is needed. Runs 1, 3, 5, 6, 7, 8, 9 and 11 are complete; reader/DAT/adjacency
fast co-activation passed as run 19. Run 9 is saved as run 28; its reported issues are being investigated.
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
| 4 | Vanilla double-cursor/menu-bar comparison | 1 | After any enhanced run |
| 5 | Reader/adjacency fast modes | 0 | Accepted as run 19 |
| 6 | Linear hull materials off/on plus sharpen/cuts at fixed exposure | 0 | Completed: A run 20, B runs 21–23; analysis/quality follow-ups remain |
| 7 | Fixed/automatic exposure and bloom toggles, central chase HUD and selection timing | 0 | Completed as run 26; follow-ups combined into run 8 |
| 8 | Restored glow, milder exposure and native selection-stutter trace | 0 | Completed as run 27 |
| 9 | Stronger glow and selection/voice timing | 0 | Completed as run 28 on source `d9413fc` |
| 10 | Target-name speech with the opt-in WMA decoder | 1 | On hold (load hang, root cause open) |
| 11 | Fade region route and alpha-tested cutout, combined | 1 | Completed as user run 11, snapshot run36 |
| 12 | Voice load-hang Wine trace witness (no new build) | 0 | Completed as user run 12, snapshot run37 (trace `/tmp/x3-witness-quartz.log.z`, 5.0 GB) |
| 13 | Target-name speech with the decoder plugin and the DMO fallback hook | 1 | Attempted as run 38: execute-access fault at the loading screen; hook fix in progress |
| 14 | Station source-over linear route, fade region and shimmer trace, combined | 0 | Completed as user run 14, snapshot run39 (witness clean, port composed, darkening persists) |
| 16 | Run 13 retry: target-name speech with the decoder plugin and the fixed DMO fallback hook | 0 | Completed as user run 16, snapshot run41: loads, speech works, crackle under investigation |
| 17 | Bullet bound after near-plane clipping, packed_sample brightness | 0 | Completed as user run 17, snapshot run42: 100 % bound, witness clean, dimmer unresolved (centre sample) |
| 15 | Screen emission on bullets (packed policy 8 in the region bracket) | 0 | Completed as user run 15, snapshot run40: witness clean, 50 % of bullet draws refused (w ≤ 0), near-fullscreen brackets; bound fix in progress |

**Run 10 attempted and failed to load** (runs 29–31, 2026-09-14): with
`--voice-decoder` the game stops on the loading screen at session frame 3 with no
sound; the same command without the option loads. Run 31 captured a GStreamer log
(`/tmp/x3-gst-run31.log`: plugin loaded, third ASF stream connected the decoder,
last event an unhandled `convert` query) and a process sample
(`/tmp/x3-run31-sample-game.txt`); triage is in progress. Run 10 stays on hold
until the [decoder adapter note](../architecture/voice-decoder-adapter.md) records a root cause and fix. Run 28
analysis and the next combined changes are underway. Run 4 remains the optional vanilla cursor comparison.
Emission stays off for this comparison; its twenty-pair live route is qualified,
but gameplay appearance and cost will need separate acceptance.

## 12. Voice load-hang Wine trace witness — Completed

Purpose: capture CrossOver's own quartz/amstream trace of the loading-screen
hang with the decoder plugin, to see which filter fails `Pause`/`Run` inside
`SetState(RUN)` (`docs/reverse-engineering/voice-startup-sequence.md` §11). The
installed build already contains the default-off witness sites; no new DLL.
The game is expected to hang on the loading screen: wait about 30 s after the
loading screen stops progressing, then force-quit X3 (Cmd-Option-Esc). Do not
load a save if the main menu does appear; just quit and report.

```sh
CX_LOG=/tmp/x3-witness-quartz.log.z \
CX_DEBUGMSG='-all,trace+quartz,trace+amstream,warn+winegstreamer,+timestamp,+loaddll' \
./x3run --direct --telemetry --game-phases --audio-sites \
  --voice-decoder /tmp/x3-wma-plugin-v3
```

Report: whether it hung or reached the menu, the session path the launcher
prints, and the size of `/tmp/x3-witness-quartz.log.z`. Analysis greps that
trace for the failing stream's graph composition and the filter that returned
`80004005`; the log is never read whole.

## 13. Target-name speech with the decoder plugin and the DMO fallback hook — Ready

The candidate `76d7750` (DLL `608b35d8…`, record
`verification/results/screen-emission-install.json`) is installed; run 13 first, then 14, then 15. The hook acts only when the game's speech-decoder `Init`
fails with class-not-registered, so the load hang of runs 29–36 should be gone
(`docs/architecture/voice-decoder-adapter.md`, "DMO fallback hook"). Keep the
run short: load the usual save, select five or six different targets (ships and
stations), listen for the target-name speech, note whether selection still
pauses, open one NPC comm dialogue, then quit. If the loading screen hangs again
for more than 30 s, force-quit and report; do not retry.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --game-phases --audio-sites --voice-decoder /tmp/x3-wma-plugin-v3 \
  --capture-start 999999 --capture-frames 1
```

Report: hang or not, speech heard or not (and whether it starts at the right
word), selection pauses, comm video/audio, and the session path. Analysis reads
the `voice_dmo_fallback` activation lines, the `game_phase_audio` counters and
the selection timing.

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md); they are provenance,
not rerun requests.

## 10. Target-name speech with the opt-in WMA decoder — On hold (load hang, root cause open)

The process-local decoder plugin is delivered by environment only, to this one
game process; nothing is written into the game, the bottle or any global
configuration. The plugin lives in `/tmp/x3-wma-plugin`; if `/tmp` was cleared,
copy the backup back first:

```sh
cp -R /Users/asvetl/x3-mod-resume-2026-09-14/artifacts/wma-plugin /tmp/x3-wma-plugin
```

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --voice-decoder /tmp/x3-wma-plugin \
  --capture-start 999999 --capture-frames 1
```

Load the usual save and select several different targets (ships and stations,
including repeats of the same name). Report:

1. whether target-name speech is heard at all, and for which selections;
2. whether the pause on selection changed compared with run 28 (longer, shorter
   or the same);
3. whether spoken lines start clipped (first syllable missing) or run into
   the start of the following line; the [cue-timing note](../reverse-engineering/voice-cue-timing.md)
   predicts an onset error of up to about 0.7 s from the decoder's sample
   timestamps, so this run tests restored speech, not cue timing.

No F8 capture is needed. If the game fails to start, rerun the same command
without `--voice-decoder` and report which of the two failed.

The distance-fade capture that was noted here is completed run 11; its command and
instructions are in [the completed-run archive](../archive/user-runs-completed.md).

## 4. Vanilla window/cursor comparison — Ready after any enhanced run

```sh
./x3run --direct --vanilla
```

Alt-tab out and back once. Report whether both the macOS arrow and game cursor
appear, whether their positions differ, and whether the macOS menu bar overlaps
the game. Compare the same screen as the enhanced run; load the save if the
problem only appears during gameplay. No F8 capture is needed.
