# Project status

The single current-state file (updated 2026-09-26). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run91 DLL SHA-256
`25adddf84b3466044f0b387633da64ea77684c5ea3a4cd74c143d0a4e9592233` (57,280,486 bytes), built once from clean
reviewed main `759c2ac7` in a detached worktree (`/tmp/x3-run91-candidate/src`). Retained DLL:
`/tmp/x3-run91-candidate/build/d3d9.dll`. Installed 2026-09-26 08:40
([qualification](../verification/results/run91-candidate-qualification.json),
[install](../verification/results/run91-candidate-install.json)). The shipped template `x3m.ini` (every key commented)
sits next to the DLL for the player-mode flight.

Rollback chain: Run90 `6f6be732…` at `/tmp/x3-run90-candidate/build/d3d9.dll` (unflown), Run89 `0f6acab4…` at
`/tmp/x3-run89-candidate/build/d3d9.dll` (unflown), then Run88 `6fe194bd…` at `/tmp/x3-run88-candidate/build/d3d9.dll`
(accepted in Run 88 A, the last accepted build).

Run91 carries, beyond Run90: the developer logging options trimmed to five including `--draw-trace` (`bec1afb5`,
[option inventory](verification/launcher-options-inventory.md)); every in-game hotkey removed except F8, which captures
only under `--debug`, and `--capture-start` / `--capture-delay` removed (`da84d232`); the settings file `x3m.ini`,
steps 1 and 2 of the [config design](architecture/config-file.md) (`204d3a09`, `44c60abb`). Qualification at
`759c2ac7`: build 0 warnings, x87 0 violations, host suite 269 modules / 2,801 tests / 0 failing, motion output 230 cases
at their committed counts, fog shader hashes identical, imports 236 to 234, no incidents (47 min wall).

## Main beyond the installed build

Main after the install carries only documentation, the results sweep, `.clang-format` and the whole-tree formatting
pass (whitespace only, verified by a build and the host suite); no DLL behaviour change is uninstalled.

## Run queue

Run 91 A is queued (it supersedes Run 90 A, never flown): [run queue](verification/user-runs.md).

## Open items

- Native Windows runtime behaviour is unverified; the source cross-compiles, gaps are tracked in
  [platform portability](architecture/platform-portability.md).
- Config design steps 3 (typed values per family) and 4 (generated inventory tables) are still to come
  ([config-file.md](architecture/config-file.md)).
- MetalSharp: `~/.metalsharp/sharp-library/library.json` is absent, so `prepare_metalsharp.py --check` fails
  ([experiment](verification/metalsharp-experiment.md)).
- `x3m-regenerate` has never run on a real mod tree (synthetic trees only;
  [LOD overlay for mods](architecture/lod-overlay-mods.md)).

History: handoffs, status history and completed runs are in [docs/archive/](archive/).
