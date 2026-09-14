# Outstanding user gameplay runs

Updated 2026-09-14. Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. The installed build is described in [status](../status.md).
From the repository root, paste a `./x3run` command below. The executable
[launcher script](../../x3run) handles the shared lock and log snapshots; no shell
function setup is needed. Runs 1, 3, 5, 6, 7, 8 and 9 are complete; reader/DAT/adjacency
fast co-activation passed as run 19. Run 9 is saved as run 28; its reported issues are being investigated.
Close X3 between runs and report completed numbers. After exit, the helper prints
a fresh `/tmp/x3-bottleX3-run<N>/` path containing that session’s log and referenced
captures, so later A/B runs cannot overwrite them. Vanilla/dry-run creates no snapshot.

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
| 11 | Fade region route and alpha-tested cutout, combined | 1 | Pending candidate |

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

The distance-fade capture that was noted here is now run 11 below.

## 11. Fade region route and alpha-tested cutout, combined — Pending candidate

This run needs a candidate DLL built from current `main` and installed by the
orchestrator per `AGENTS.md`; the installed build `8442f43` does not contain
these options, so do not start it until the queue says the candidate is in
place. The command adds the distance-fade region route and its witness to the
run-10 enhanced set (no `--voice-decoder`). The alpha-tested cutout runtime has
no flag of its own: it arms whenever linear materials are requested, the cutout
capability is Ready, HDR is enabled and the mip bias is zero
([alpha-tested materials](../architecture/alpha-tested-materials.md)), which is
why `--taa-mip-bias` must not be added here.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --linear-distance-fade --fade-witness \
  --capture-start 999999 --capture-frames 1
```

`--linear-distance-fade` composes admitted distance-fade draws through an
in-place region bracket; `--fade-witness` (K=30) reads the M coverage target
back every 30th frame and logs `fade_witness` lines counting covered pixels
outside the derived rectangles. Emission stays off.

Load the usual save, fly near asteroids and distant objects, then approach an
Argon station. Report:

1. any visual difference on fading asteroids and distant objects, and whether
   the frame rate is acceptable;
2. one F8 capture during the Argon station approach with the docking port in
   view;
3. whether the docking-port darkening seen in run 28 changes (the cutout
   runtime is not expected to fix it);
4. the session log path printed by `./x3run`.

Acceptance needs zero outside pixels in every `fade_witness` line; analysis
reads those lines and the fade `f` histogram with
`verification/probe/run_linear_distance_fade_live.py`
([region note](../architecture/linear-distance-fade-region.md)) before any
acceptance claim.

## 4. Vanilla window/cursor comparison — Ready after any enhanced run

```sh
./x3run --direct --vanilla
```

Alt-tab out and back once. Report whether both the macOS arrow and game cursor
appear, whether their positions differ, and whether the macOS menu bar overlaps
the game. Compare the same screen as the enhanced run; load the save if the
problem only appears during gameplay. No F8 capture is needed.
