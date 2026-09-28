# Project status

The single current-state file (updated 2026-09-26). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run101 = **release 0.6.0 (stripped)** DLL SHA-256
`8b164ee11446bc3f0c63ff5eb8f9cfb07a18f2526231d2509ffbaec6738b99df` (39,472,075 bytes; `d3d9.debug` and the unstripped
build stay beside it at `/tmp/x3-run101-candidate/build/`), built once from clean main `018d7e49` by
`tools/release/release.py` (the DLL inside `x3m-0.6.0.zip`, sha256 `b52ba96c…`, 58,290,262 bytes, at
`/tmp/x3m-release-0.6.0/`; contents d3d9.dll, x3m.ini, x3m-regenerate.exe, README.txt;
[release record](../verification/results/release-0.6.0.json)). Installed 2026-09-28 05:57 with the 0.6.0 template
`x3m.ini` (`d0f03f88…`, the previous one was the unmodified 0.5.2 template) and the 0.6.0 `x3m-regenerate.exe`
(`050369cf…`) plus the macOS `x3m-regenerate` beside it ([qualification](../verification/results/run101-candidate-qualification.json),
[install](../verification/results/run101-candidate-install.json)). The bottle's `cxbottle.conf` keeps the two GStreamer
variables (`b06979d1…`).

Rollback chain: Run100 `20680908…` at `/tmp/x3-run100-candidate/build/d3d9.dll` (lock section 12 opt-in, accepted in
Run 100 A), then Run99 `1adabd03…`, then Run98 `d6f6b47a…` (release 0.5.2).

0.6.0 carries, beyond 0.5.2, the **TAA luminance lock, on by default** (`taa_luma_lock = 16,0.25,3`; `0` turns it off;
[design](architecture/taa-luminance-lock.md) sections 0-12, ledger [temporal-resolve.md](verification/temporal-resolve.md)):
a far pixel whose residual against the reprojected history flips sign on two consecutive frames, at rest or under a
camera pan, locks for 16 frames at the far stabiliser's weight and is carried across pans; every other far pixel runs
at the base weight, so distant hull plates pan sharp while struts stay held (Run 100 A: 25-50 % sharper under pan,
rest flicker 2.4-2.9x the old blanket, not noticed by the user). The far stabiliser's weight is now the locked weight
and its ramp the eligibility band; the thin region stays. Also since 0.5.2: the baker strut widening as an opt-in
`--widen` flag (parked), and the CrossOver Preview note for starting `x3m-regenerate.exe` by its Z: path (FEX drops
the exe name for `C:\X3\x3m-regenerate.exe`; the name stays by user decision).

## Main beyond the installed build

Nothing: main `018d7e49` is the installed and released commit (documentation follows it).

## Run queue

Run 101 A queued (2026-09-28): one plain flight on release 0.6.0 (lock on by default), stations at rest and under pan,
speech, alt-tab: [run queue](verification/user-runs.md). Run 100 A accepted the lock; Run 99 A found the pan-creation gap
that section 12 closed.

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
