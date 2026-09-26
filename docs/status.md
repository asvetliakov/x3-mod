# Project status

The single current-state file (updated 2026-09-26). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run92 DLL SHA-256
`f6c687cb3d154fc4677313506da81ca71e3eb4a7b8fc82c7fdc25fa7e4d8844a` (57,180,234 bytes), built once from clean
reviewed main `335c9250` in a detached worktree (`/tmp/x3-run92-candidate/src`). Retained DLL:
`/tmp/x3-run92-candidate/build/d3d9.dll`. Installed 2026-09-26 18:15
([qualification](../verification/results/run92-candidate-qualification.json),
[install](../verification/results/run92-candidate-install.json)). The shipped template `x3m.ini` (every key commented)
sits next to the DLL.

Rollback chain: Run91 `25adddf8…` at `/tmp/x3-run91-candidate/build/d3d9.dll` (accepted in Run 91 A), then Run88
`6fe194bd…` at `/tmp/x3-run88-candidate/build/d3d9.dll` (accepted in Run 88 A).

Run92 carries, beyond Run91: the **TAA history weight default 0.85** (was 0.9; from the run340 pan replay,
[temporal-resolve.md](verification/temporal-resolve.md); sharpen, thin-region and far weights unchanged), the
**`bolt_copy` capture diagnostic** (one row per bullet draw on F8 frames with hash / qsum / bbox of the drawn positions,
to prove whether the game's two bullet copies hold the same bolts, [lod-overlay.md](verification/lod-overlay.md)), the
whole-tree clang-format pass (whitespace only) and the version define from the CMake project version. Qualification at
`335c9250`: build 0 warnings, x87 0 violations, host suite 270 modules / 2,810 tests / 0 failing, motion output 230 cases
at their committed counts plus the new bolt-copy case, temporal pan row inside its bounds, dry runs identical to Run91;
two earlier attempts failed on a stale 0.9 literal in the pass header and on the seam build lacking the version define
(both fixed and pinned by host tests).

## Main beyond the installed build

Nothing: main `335c9250` is the installed commit (documentation and the run queue follow it).

## Run queue

Run 92 A is queued (0.85 default with rest, pan and firing captures; short 0.9 and 0.8 comparisons):
[run queue](verification/user-runs.md).

## Open items

- From Run 91 A: bullets behind distant objects = the game's early bullet copy overpainted by later opaque draws while
  the additive route brightens both copies (fix pending the `bolt_copy` proof in Run 92 A); station blur under a pan =
  history weight (0.85 default now, flight comparison against 0.9 and 0.8 in Run 92 A).

- Native Windows runtime behaviour is unverified; the source cross-compiles, gaps are tracked in
  [platform portability](architecture/platform-portability.md).
- Config design steps 3 (typed values per family) and 4 (generated inventory tables) are still to come
  ([config-file.md](architecture/config-file.md)).
- MetalSharp: `~/.metalsharp/sharp-library/library.json` is absent, so `prepare_metalsharp.py --check` fails
  ([experiment](verification/metalsharp-experiment.md)).
- `x3m-regenerate` has never run on a real mod tree (synthetic trees only;
  [LOD overlay for mods](architecture/lod-overlay-mods.md)).

History: handoffs, status history and completed runs are in [docs/archive/](archive/).
