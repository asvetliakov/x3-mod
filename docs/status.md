# Project status

The single current-state file (updated 2026-09-26). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run93 DLL SHA-256
`5bee0e8a78d6fd631a655e1b435fd34a3dabe92b7c5d656d7222f79a4228e281` (57,199,261 bytes), built once from clean
reviewed main `92f8223d` in a detached worktree (`/tmp/x3-run93-candidate/src`). Retained DLL:
`/tmp/x3-run93-candidate/build/d3d9.dll`. Installed 2026-09-26 21:14
([qualification](../verification/results/run93-candidate-qualification.json),
[install](../verification/results/run93-candidate-install.json)). The shipped template `x3m.ini` (every key commented)
sits next to the DLL.

Rollback chain: Run92 `f6c687cb…` at `/tmp/x3-run92-candidate/build/d3d9.dll` (accepted in Run 92 A), Run91
`25adddf8…` (accepted in Run 91 A), then Run88 `6fe194bd…` (accepted in Run 88 A).

Run93 carries, beyond Run92: **single-copy bullets** on by default (the game draws its bullet batch twice; the early copy,
overpainted by every later opaque draw, is dropped when the same buffer had a late copy in the previous frame, so a bolt
keeps one brightness over a distant station and over empty space; `--bolt-single-copy off` for A/B;
[bolt-footprint.md](architecture/bolt-footprint.md) "Single copy") and the **opt-in rotation-aware TAA motion weight**
(`--taa-motion-weight-rotation F[,V0,V1]`: the history weight of ordinary pixels drops toward F with the screen motion
from camera rotation; off by default, off path bit-identical; [taa-motion-history-weight.md](architecture/taa-motion-history-weight.md)
section 10). Qualification at `92f8223d`: build 0 warnings, x87 0 violations, host suite 270 modules / 2,812 tests /
0 failing, motion output 230 committed cases at their counts plus the six single-copy cases, temporal pass byte-identical
to the committed record, dry runs: one delta vs Run92 (`X3M_BOLT_SINGLE_COPY=1`).

## Main beyond the installed build

Nothing: main `92f8223d` is the installed commit (documentation and the run queue follow it).

## Run queue

No run is queued. Run 93 A (run346/347): the single-copy rule works but the TAA resolve still erases bolts over far
geometry (design note in progress); the rotation-aware weight stays off by the user's decision (shimmer under pan):
[run queue](verification/user-runs.md).

## Open items

- From Run 91 A: bullets behind distant objects = the game's early bullet copy overpainted by later opaque draws while
  the additive route brightens both copies (Run93's single-copy rule removed the double brightness, but the TAA resolve keeps ~0.99 history at bolt pixels that carry a far station's depth: fix design in docs/architecture/bolts-through-taa.md); station blur under a pan =
  history weight (0.85 accepted in Run 92 A; the rotation-aware weight stays opt-in and off after Run 93 A: the user prefers blur to shimmer).

- Native Windows runtime behaviour is unverified; the source cross-compiles, gaps are tracked in
  [platform portability](architecture/platform-portability.md).
- Config design steps 3 (typed values per family) and 4 (generated inventory tables) are still to come
  ([config-file.md](architecture/config-file.md)).
- MetalSharp: `~/.metalsharp/sharp-library/library.json` is absent, so `prepare_metalsharp.py --check` fails
  ([experiment](verification/metalsharp-experiment.md)).
- `x3m-regenerate` has never run on a real mod tree (synthetic trees only;
  [LOD overlay for mods](architecture/lod-overlay-mods.md)).

History: handoffs, status history and completed runs are in [docs/archive/](archive/).
