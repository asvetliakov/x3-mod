# Project status

The single current-state file (updated 2026-09-26). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run94 DLL SHA-256
`338b00d70677be2ddcb50ce1363132d84a83d0c52511e1dcc9911f96615984fc` (57,247,306 bytes), built once from clean
reviewed main `2a75e2c3` in a detached worktree (`/tmp/x3-run94-candidate/src`). Retained DLL:
`/tmp/x3-run94-candidate/build/d3d9.dll`. Installed 2026-09-27 00:47
([qualification](../verification/results/run94-candidate-qualification.json),
[install](../verification/results/run94-candidate-install.json)). The shipped template `x3m.ini` (every key commented)
sits next to the DLL.

Rollback chain: Run93 `5bee0e8a…` at `/tmp/x3-run93-candidate/build/d3d9.dll` (flown in Run 93 A: single-copy rule
proven, not accepted as a fix), Run92 `f6c687cb…` (accepted in Run 92 A), Run91 `25adddf8…` (accepted in Run 91 A).

Run94 carries, beyond Run93: **bolts through the TAA** ([design](architecture/bolts-through-taa.md), on by default): the
late bullet draw flags its pixels, the resolve keeps the station's history untouched and only raises the flag in its
output alpha, and the tonemap and bloom stages add the pre-resolve bolt back at `--bolt-far-show` (default 0.5) where
far or thin-region history is held; the sun-shadow apply treats a flagged texel as unshadowed for that frame;
`--bolt-far-composite off` for A/B. Qualification at `2a75e2c3`: build 0 warnings, x87 0 violations, host suite 270
modules / 2,813 tests / 0 failing, motion output 230 committed cases at their counts plus the three far-flag cases,
temporal pass byte-identical (lattice 654/90), both shader provenance checks PASS, bloom 47/47, dry runs: two deltas vs
Run93 (the two new settings).

## Main beyond the installed build

Nothing: main `2a75e2c3` is the installed commit (documentation and the run queue follow it).

## Run queue

No run is queued. Run 94 A (run348/349) accepted the bolt composite; the user chose `--bolt-far-show 1` over 0.5, now the
default: [run queue](verification/user-runs.md).

## Open items

- From Run 91 A: bullets behind distant objects = the game's early bullet copy overpainted by later opaque draws while
  the additive route brightens both copies (fixed: Run94 composites the bolt back over held far/thin pixels, accepted in Run 94 A at W 1); station blur under a pan =
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
