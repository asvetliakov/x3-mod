# Project status

The single current-state file (updated 2026-09-26). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run96 = **release 0.5.0 (stripped)** DLL SHA-256
`33f815375d573d3075744b26aedbf473c0d1023473ca4314f0a42106bdfbc49f` (39,439,629 bytes; the unstripped build
`045f4f3b…` 57,248,980 bytes and `d3d9.debug` `3fc4b48a…` stay beside it), built once from clean main `00d85785` by
`tools/release/release.py` (the DLL inside `x3m-0.5.0.zip`, sha256 `e9d1d84a…`, 58,241,441 bytes, at
`/tmp/x3m-release-0.5.0/`; contents d3d9.dll, x3m.ini, x3m-regenerate.exe, README.txt;
[release record](../verification/results/release-0.5.0.json)). Retained: `/tmp/x3-run96-candidate/build/`.
Installed 2026-09-27 02:09 ([qualification](../verification/results/run96-candidate-qualification.json), reduced scope
by the user's instruction; [install](../verification/results/run96-candidate-install.json)). The bottle's
`cxbottle.conf` now carries the two GStreamer variables for the game-directory voice decoder
(`manage.py voice-decoder --bottle-env apply`, deliberate, sha256 `b06979d1…`, backup `cxbottle.conf.x3m-bak`), so
speech works from a plain CrossOver launch too (to be confirmed by the next launch). The shipped template `x3m.ini`
sits next to the DLL.

Rollback chain: Run95 `25cba248…` at `/tmp/x3-run95-candidate/build/d3d9.dll` (unstripped 0.5.0), Run94 `338b00d7…`
(accepted in Run 94 A).

Run96 carries, beyond Run95: the always-tier fix (six per-frame emitter rows and the resource identity rows moved under
`--perf`/`--debug`: a player-mode log was 159 MB/h, now about 3 MB/h), the stripped release DLL, and the voice decoder
drop-in with the bottle setting.

## Main beyond the installed build

Only tooling and tests after `00d85785` (the bottle-env command `a4899fd1`, a test fix `8dc1f9b8`).

## Run queue

No run is queued: [run queue](verification/user-runs.md). The next launch from CrossOver (no launcher) should confirm:
the stripped DLL loads, `x3m.log` stays small, and speech plays (`voice_dmo_fallback ... init_hr=00000000`).

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
