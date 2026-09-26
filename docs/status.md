# Project status

The single current-state file (updated 2026-09-26). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run90 DLL SHA-256
`6f6be732bfd5a8b7b0819ae233d42128e55af5f31e42f727abc4272c54dffb98` (56,825,745 bytes), built once from clean
reviewed main `f9cceb17` in a detached worktree (`/tmp/x3-run90-candidate/src`). Retained DLL:
`/tmp/x3-run90-candidate/build/d3d9.dll`. Installed 2026-09-26 04:00
([qualification](../verification/results/run90-candidate-qualification.json),
[install](../verification/results/run90-candidate-install.json)).

Rollback chain: Run89 `0f6acab4…` at `/tmp/x3-run89-candidate/build/d3d9.dll` (unflown), then Run88
`6fe194bd…` (accepted in Run 88 A, the last accepted build).

## Main beyond the installed build

Three DLL changes on main are not in Run90:

- `bec1afb5`: the developer logging options trimmed to five (including `--draw-trace`) and the
  [option inventory](verification/launcher-options-inventory.md) reorganised.
- `da84d232`: every in-game hotkey removed except F8, which captures only under `--debug`;
  `--capture-start` and `--capture-delay` removed.
- `204d3a09` and `44c60abb`: the settings file `x3m.ini`, steps 1 and 2 of the
  [config design](architecture/config-file.md) (schema, generated template, parser, resolver, launcher
  `--config` player mode, `tools/release/package.py`), with the patch fixture records re-recorded.

A Run91 candidate from `759c2ac7` is in qualification; see the run91 records under
`verification/results/` when they exist.

## Run queue

Run 90 A is queued (it supersedes Run 89 A): [run queue](verification/user-runs.md).

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
