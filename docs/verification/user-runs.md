# Outstanding user gameplay runs

Updated 2026-09-14. Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. The installed build is described in [status](../status.md).
From the repository root, paste a `./x3run` command below. The executable
[launcher script](../../x3run) handles the shared lock and log snapshots; no shell
function setup is needed. Runs 1–3 and 5–19 are complete (queue numbers; reader/DAT/adjacency fast
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
| 20 | Step D, loading markers, AO radius/debug, asteroid diagnostic, port far/near retry | 1 | Pending the next candidate |

## 20. Step D bullets, asteroid prepass jitter, loading markers, AO radius 20, port and ship far/near — Pending the candidate

One run on the next candidate (built from main after the z_only prepass jitter, step D and the
`loading_phase` markers; hash recorded in status once installed). Load the usual save.

1. **Asteroids**: zoom on a distant asteroid field as in run 19 and say whether triangles still
   vanish and reappear. While zoomed and at rest on a far asteroid, press F8 once (this launch
   captures 8 consecutive frames). Analysis: `unjittered_depth_writers=0` on every
   `motion_output_frame` line, no jitter-side holes in the 8 pre-resolve frames.
2. **Docking port and ship**: pick one Argon docking port; fly out until the port is clearly small
   (about a thumbnail, under ~30 px), F8; fly back until it fills about a third of the screen, F8.
   Do the same far/near F8 pair on one ship. Say whether each darkens. Analysis: the per-draw path
   and the HDR captures of each pair (`docs/reverse-engineering/station-material-distance.md`).
3. **Bullets**: fire at a target for a few seconds, F8 once while firing. Say whether the bolts
   look like run 19. Analysis: `hull_px`/`aabb_px`, `window_end_scans`/`scans`, `sentinel_us`,
   brackets no longer near-fullscreen (`docs/verification/screen-emission.md`).
4. **Ambient occlusion at radius 20 m**: near a station and near an asteroid press Ctrl+Shift+F11 a
   few times; say whether the darkening in creases and contact areas is visible now, and whether
   it looks wrong anywhere (halos, crawling, HUD), and the frame rate on versus off if you
   notice it. Analysis: `ambient_occlusion_frame cpu_us`, `radius_px`, the on/off captures.
   Optional second short session (B, below): the same launch with `--ao-debug` replaces the
   image by the gray occlusion factor whenever AO is on; load the save, look at a station and
   an asteroid, quit, and say whether the gray view shows creases and contact darkening.
5. **Loading**: nothing to do; the log now carries `loading_phase` markers for menu-shown and
   save-load; report roughly how long the menu and the save load took by feel.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --fade-witness --screen-emission --screen-emission-timing \
  --ambient-occlusion --ao-timing --ao-radius 20 --ao-debug \
  --voice-decoder /tmp/x3-wma-plugin-v4 \
  --capture-start 999999 --capture-frames 8
```

Report: the session path, and the five observations above. Frame rate on versus off for AO is
still useful if you notice it.

