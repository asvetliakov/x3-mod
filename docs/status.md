# Project status

The single current-state file (updated 2026-09-29, Run104 = release 0.8.0). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**, game tree modded with **Mayhem 3** since 2026-09-28 (user install,
`addon/05..12.cat` plus loose `addon/` files; 0.7.0 LOD overlay and fog families regenerated 2026-09-29 03:33, the
regenerate tool source is unchanged since). Run104 = **release 0.8.0 (stripped)** DLL SHA-256
`f34d3ab424353a3e51e8f5408f125b31f2b7d266604317ee3361024cab4dcb41` (39,487,179 bytes), built once from clean main
`a3c85e37` by `tools/release/release.py` (`x3m-0.8.0.zip`, sha256 `b9a58c8b…`, 58,368,639 bytes, at
`/tmp/x3m-release-0.8.0/`; [release record](../verification/results/release-0.8.0.json)). Installed 2026-09-29 with the
0.8.0 `x3m-regenerate.exe` (`d7f8d687…`) and macOS `x3m-regenerate` (`28d3e915…`); the user's edited `x3m.ini` kept
(the 0.8.0 template is in the zip) ([install](../verification/results/run104-candidate-install.json)). `X3AP.exe` and
`cxbottle.conf` unchanged.

Rollback chain: Run103 `1f3ad3db…` at `/tmp/x3-run103-candidate/build/d3d9.dll`, then Run102 `b7acb91f…` (release
0.7.0) at `/tmp/x3-run102-candidate/build/d3d9.dll`, then Run101 `8b164ee1…`.

0.8.0 over 0.7.0 ([compare](../verification/results/release-0.8.0-dll-compare.json): `.text`/`.data` equal to Run103,
defaults in `.rdata`): the object-lifetime registry at 262,144 entries with backward-shift deletion and the
`object_lifetime_disabled` / `object_lifetime_stats` rows (run358: the observer disabled itself during a Mayhem 3 load
at 16,384 entries, run365 measured `peak_live=44913`, so every TAA draw had been refused at the scope gate and the
jitter showed raw, [note](reverse-engineering/object-lifetimes.md)); `hull_emission_gain` follows `emission_source_gain`
through the config file; `emission_source_clamp` and `lens_flare_gain` (run364: Mayhem nozzle cards at 3.0 engine value
under the hull gain, the red halos are Mayhem's own lens flares, [ledger](verification/screen-emission.md),
[sun occlusion](verification/sun-occlusion.md)); defaults `emission_source_gain 1`, `emission_source_clamp 0.7`,
`lens_flare_gain 0.3` (user decision after Run 103 A); `--vanilla` refuses while the bottle's decoder variables are set
(a vanilla launch stalled at the loading screen, user-observed).

## Main beyond the installed build

Nothing: main `a3c85e37` is the installed and released commit (documentation follows it).

## Run queue

Run 104 A queued (2026-09-29): a plain flight on the 0.8.0 defaults (the explicit `x3m.ini` lines are no longer
needed) to confirm the Run 103 A look: [run queue](verification/user-runs.md). Run 103 A completed and accepted
(run365 `object_lifetime_stats peak_live=44913` confirmed the capacity cause; exhausts accepted at clamp 0.7 / flare
0.5, defaults set to 1 / 0.7 / 0.3 by the user).

## Open items

- From Run 91 A: bullets behind distant objects = the game's early bullet copy overpainted by later opaque draws while
  the additive route brightens both copies (fixed: Run94 composites the bolt back over held far/thin pixels, accepted in Run 94 A at W 1); station blur under a pan =
  history weight (0.85 accepted in Run 92 A; the rotation-aware weight stays opt-in and off after Run 93 A: the user prefers blur to shimmer).

- Run374 performance triage ([results](../verification/results/run374-performance/)): 23 ms p50 at 5120x1440 on Mayhem,
  the proxy's own CPU work about 1.2 ms of it, the rest in the game's pre-render/submit (CPU vs GPU not split without
  `--gpu-sync-timing`); Mayhem draws 1.6x stock; volumetric fog never applied in that sector (`card_refused`), unexplained.
- Native Windows runtime behaviour is unverified; the source cross-compiles, gaps are tracked in
  [platform portability](architecture/platform-portability.md).
- Config design steps 3 (typed values per family) and 4 (generated inventory tables) are still to come
  ([config-file.md](architecture/config-file.md)).
- MetalSharp: `~/.metalsharp/sharp-library/library.json` is absent, so `prepare_metalsharp.py --check` fails
  ([experiment](verification/metalsharp-experiment.md)).
- `x3m-regenerate` 0.7.0 ran on Mayhem 3 (2026-09-29 03:33: 1,157 bodies baked, 2,014 refused, 70 fog families); the visual
  check of the new bodies is still deferred by the user. Sky-derived fog for the 170 `no_dust_bodies` families is
  undecided ([probe](../verification/results/fog-families-mayhem/)).

History: handoffs, status history and completed runs are in [docs/archive/](archive/).
