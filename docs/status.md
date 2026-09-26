# Project status

The single current-state file (updated 2026-09-26). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run98 = **release 0.5.2 (stripped)** DLL SHA-256
`d6f6b47a4ee1d6cb6f1e45e802f82e730e72869ddd7b96d19fcd5969469f8abd` (39,442,996 bytes; the unstripped build
`889e7cb1…` 57,269,113 bytes and `d3d9.debug` `7fd45bd0…` stay beside it), built once from clean main `ce87ec99` by
`tools/release/release.py` (the DLL inside `x3m-0.5.2.zip`, sha256 `76af86c3…`, 58,242,564 bytes, at
`/tmp/x3m-release-0.5.2/`; contents d3d9.dll, x3m.ini, x3m-regenerate.exe, README.txt;
[release record](../verification/results/release-0.5.2.json)). Retained: `/tmp/x3-run98-candidate/build/`.
Installed 2026-09-27 03:01 ([qualification](../verification/results/run98-candidate-qualification.json): the
run-in-background patch fixture 47/47 and the site verifier 17/17, host suite 272 / 2,834 / 0;
[install](../verification/results/run98-candidate-install.json)). The bottle's
`cxbottle.conf` now carries the two GStreamer variables for the game-directory voice decoder
(`manage.py voice-decoder --bottle-env apply`, deliberate, sha256 `b06979d1…`, backup `cxbottle.conf.x3m-bak`), so
speech works from a plain CrossOver launch too (to be confirmed by the next launch). The shipped template `x3m.ini`
sits next to the DLL.

Rollback chain: Run97 `6e0bda57…` at `/tmp/x3-run97-candidate/build/d3d9.dll` (0.5.1, flown from CrossOver: speech,
small log, stripped DLL loads), Run94 `338b00d7…` (accepted in Run 94 A).

Run98 carries, beyond Run97, **`run_in_background`** (default on; [RE note](reverse-engineering/run-in-background.md)):
the DLL sets the game's run-in-background bit once as `-runinbg` would, so a plain CrossOver launch no longer freezes
the game while its window is inactive. Run96 carried, beyond Run95: the always-tier fix (six per-frame emitter rows and the resource identity rows moved under
`--perf`/`--debug`: a player-mode log was 159 MB/h, now about 3 MB/h), the stripped release DLL, and the voice decoder
drop-in with the bottle setting.

## Main beyond the installed build

Nothing: main `ce87ec99` is the installed and released commit (documentation follows it).

## Run queue

Run 98 A is queued (a plain CrossOver launch: alt-tab out and back, the `run_in_background` rows, cursor and Reset
rows): [run queue](verification/user-runs.md).

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
