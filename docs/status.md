# Project status

The single current-state file (updated 2026-09-26). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run95 = **release 0.5.0** DLL SHA-256
`25cba24802207e4ab871428ca922c6a933c7d9a14bd78efe50d6b27255a7a84b` (57,247,320 bytes), built once from clean main
`35c83e26` by `tools/release/release.py` (the same build that is in `x3m-0.5.0.zip`, sha256 `38c0cdb8…`, 76,901,445 bytes,
at `/tmp/x3m-release-0.5.0/`; [release record](../verification/results/release-0.5.0.json)). Retained DLL:
`/tmp/x3-run95-candidate/build/d3d9.dll`. Installed 2026-09-27 01:05
([qualification](../verification/results/run95-candidate-qualification.json), reduced scope by the user's instruction:
build, x87, config check, host suite 270 / 2,813 / 0, dry-run evidence; no Wine fixtures, the sources Run94 qualified are
unchanged; [install](../verification/results/run95-candidate-install.json)). The shipped template `x3m.ini` sits next
to the DLL.

Rollback chain: Run94 `338b00d7…` at `/tmp/x3-run94-candidate/build/d3d9.dll` (accepted in Run 94 A), then Run92
`f6c687cb…` (accepted in Run 92 A).

Run95 carries, beyond Run94: `bolt_far_show` default 1 (the user's choice in Run 94 A) and the version 0.5.0. Run94
brought the **bolts through the TAA** composite ([design](architecture/bolts-through-taa.md)), accepted in Run 94 A.

## Main beyond the installed build

Nothing: main `35c83e26` is the installed and released commit (documentation follows it).

## Run queue

No run is queued: [run queue](verification/user-runs.md).

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
