# Project status

The single current-state file (updated 2026-09-29). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**, game tree modded with **Mayhem 3** since 2026-09-28 (user install,
`addon/05..12.cat` plus loose `addon/` files). Run102 = **release 0.7.0 (stripped)** DLL SHA-256
`b7acb91f45ba37382571b7397f749827bd72f1a8c8b3f437809435f00ad51c18` (39,472,075 bytes), built once from clean main
`9edd942b` by `tools/release/release.py` (`x3m-0.7.0.zip`, sha256 `9b1c6883…`, 58,363,052 bytes, at
`/tmp/x3m-release-0.7.0/`; [release record](../verification/results/release-0.7.0.json)). Installed 2026-09-29 02:55
with the 0.7.0 template `x3m.ini` (`14905c80…`; the previous one was the unmodified 0.6.0 template) and the 0.7.0
`x3m-regenerate.exe` (`ea0308fb…`) plus the macOS `x3m-regenerate` (`a3f4759c…`)
([install](../verification/results/run102-candidate-install.json)). `X3AP.exe` unchanged; `cxbottle.conf` unchanged by
the install (`3a70e875…`, modified 2026-09-28 21:35 before this work, both GStreamer variables present).

Rollback chain: Run101 `8b164ee1…` at `/tmp/x3-run101-candidate/build/d3d9.dll` (release 0.6.0), then Run100
`20680908…`, then Run99 `1adabd03…`.

0.7.0 carries no renderer change: the DLL's loaded sections equal 0.6.0 except the version string, the commit marker
and the export timestamp ([comparison](../verification/results/release-0.7.0-dll-compare.json)). The release is the
bake tooling ([ledger](verification/lod-overlay.md)): classic non-effect materials bake
([note](reverse-engineering/non-effect-materials.md)), one merged material per effect file and bound occlusion map,
body members resolve as the engine does ([note](reverse-engineering/body-format-bob1.md) section 7.1), any tail after
`/BOB` is accepted, a NULL diffuse bakes the engine's grey placeholder (23 baked bodies change), and the batch
schedules workers by predicted memory. On Mayhem 3 about 78 formerly refused bodies bake. The installed overlay and
`x3m/fog-families.bin` are still the 0.6.0 tool's output until the user reruns `x3m-regenerate`.

## Main beyond the installed build

Nothing: main `9edd942b` is the installed and released commit (documentation follows it).

## Run queue

Run 102 A queued (2026-09-29): rerun `x3m-regenerate` on the Mayhem 3 install with the 0.7.0 tool, then a plain
flight: [run queue](verification/user-runs.md). The user deferred the visual check of the new bodies (accept, revisit
on a report). Run 101 A (plain flight on 0.6.0) was not reported and is superseded.

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
- `x3m-regenerate` 0.6.0 ran on Mayhem 3 (2026-09-28: 1,079 bodies baked, 70 fog families added); the 0.7.0 rerun and
  the first flight on a modded tree are open (Run 102 A). Sky-derived fog for the 170 `no_dust_bodies` families is
  undecided ([probe](../verification/results/fog-families-mayhem/)).

History: handoffs, status history and completed runs are in [docs/archive/](archive/).
