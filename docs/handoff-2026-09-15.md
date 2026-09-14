# Resume handoff — 2026-09-15 (early morning, after run 19 and the run-20 install)

Written for the next orchestrator session. Read this file, `AGENTS.md`, `CLAUDE.md`,
`docs/status.md`, `docs/goals.md`, then `git log --oneline -80`, then only the owning notes named
per task. `archive/handoff-2026-09-14d.md` is historical; everything it listed is run, merged or
decided.

## Installed build and bottle

- Installed DLL `53a0d8a7f76e89836a66068ce166af095058fd3e10b6cbb5693a2b0027d35c5b` (14,299,619
  bytes), clean build of `509a273` (fade-band motion arm, `--lod-scale`); record
  `verification/results/run22-candidate-install.json`. Candidate dir
  `/var/folders/l6/…/T/x3-run22-candidate-h8l9/` holds the DLL, the rollback `39b090d0…`
  (`77a649b`) DLL and its manifest (`rollback-*`); the older run-20 dir `x3-run20-candidate-dEIr/`
  keeps `ab6e17ba…` (`5d06316`).
- EXE `fdbf3418…` and `cxbottle.conf` `cc5d6c00…` unchanged. Voice plugin `/tmp/x3-wma-plugin-v4`.
- Launcher ownership manifest `drive_c/X3/x3-modern-install.json` names the installed hash.
- `./x3run` takes the Wine lock itself: never wrap it in `wine_lock.py` (self-deadlock, 600 s).

## Runs 20–21 (snapshots run48, run49) and decisions (do not reopen)

- Asteroid triangle dropout fixed and accepted in game (run 20). Step D accepted; the "slightly
  dimmer" bolts were refuted by the every-frame witness (run 21). Loading markers work (menu
  12.9 s, save-load stall 21.7 s = attribution target).
- AO closed: invisible at 2/20/100 m at X3 distances; the user decided default-off, no AO v2;
  cost when off is zero. `docs/architecture/ambient-occlusion-scale.md` ratified.
- Directional shadows: `docs/architecture/directional-shadows.md` recommends screen-space sun
  shadows first with a sun-lit-share lane in the converted materials; shadow maps later. Not yet
  ratified (user has not said "go shadows").
- Trembling "station section" (run 21) = a fading asteroid seen through the hangar gap, resolved
  current-only via the reactive mask; fixed by the fade-band motion arm (installed).
- Docking-port darkening: vanilla shows it too (run 21 B); the LOD-step and distance-fade
  explanations are both refuted by captures; the run-20 "port" measurement was an asteroid part.
  Unexplained; run 22 collects the user's screenshot pair with F8s. `--lod-scale` (user's idea)
  is installed default-off and tested at 2× in run 22 as a detail enhancement.

## Run 19 (snapshot run47) outcomes and the decisions taken (do not reopen)

- Cutout-miss exemption holds (reason 3 at 0.01 %); the remaining asteroid shimmer was triangles
  vanishing per frame, owned by the unjittered depth-only prepass (`asteroid-fog-temporal.md`,
  "Run 47"). Fixed: z_only programs jittered with the scene, `unjittered_depth_writers` counter.
- Bolts accepted at gain 1; `--screen-emission-gain` 1 stays default, no gain-2 run.
- AO runs and toggles (≈200 µs CPU) but is invisible at 2 m; stays default-off; run 20 tests
  `--ao-radius 20` (optional session B with `--ao-debug`). No code change.
- Docking-port design note stays unratified and the linear rule unchanged: run 19's pair landed at
  57/54 px on the same draw path (radiance within 3 %), and the user sees the darkening on a ship
  too; run 20 retries with a real far state and a ship pair.
- Run 4 done: the double cursor after alt-tab reproduces in vanilla (recipe in
  `architecture/window-and-cursor.md`); not a proxy regression.

## Pending

1. **User run 22** (`docs/verification/user-runs.md` §22): port screenshot pair + F8s, `--lod-scale 2`
   appearance/frame cost (`lod_scale … patched=1`, `lod_scale_value … applied=2`), trembling gone
   (`fade_routed`, `fade_held` on the frame lines).
2. After run 22: the port owner from the screenshots; the shadows decision; loading stall
   attribution (21.7 s save load) with the markers.
3. Minor open: exposure meter self-test under `X3M_HDR_DECODE=none`; other unjittered depth
   writers if the run-20 counter is nonzero (review note in `asteroid-fog-temporal.md`).

## Local artefacts that a `/tmp` cleaner removes

`/tmp/x3-shader-sweep/programs/` (the extracted reviewed shader binaries every live fixture
needs) and the Ghidra project under `/tmp/x3-ghidra-research/` were emptied by a `/tmp` cleaner
on 2026-09-15 00:00. Regenerate the programs without Wine (about a minute):

```sh
X="$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3"
python3 tools/analysis/index_shaders.py "$X" --output /tmp/x3-shader-index-expanded.json
python3 tools/analysis/sweep_shaders.py extract "$X" --index /tmp/x3-shader-index-expanded.json \
  --raw-directory /tmp/x3-shader-sweep/programs --manifest /tmp/x3-shader-sweep/manifest.json
```

(751 programs; `run_motion_output.py` checks the hashes it needs.) The run snapshots
`/tmp/x3-bottleX3-run*` and the voice plugin `/tmp/x3-wma-plugin-v4` (backup under
`~/x3-mod-resume-2026-09-14/artifacts/`) are exposed to the same cleaner.

## Worktrees and artefacts

No agent worktrees remain (`.claude/worktrees/` empty); main clean. Older `/tmp/x3-*` and
`…/T/x3-*-candidate-*/source` worktrees predate 2026-09-15 and are inert. Untracked runner
outputs under `verification/results/bottle-X3/` are local evidence, not for commit.
