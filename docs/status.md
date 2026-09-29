# Project status

The single current-state file (updated 2026-09-29, Run103). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**, game tree modded with **Mayhem 3** since 2026-09-28 (user install,
`addon/05..12.cat` plus loose `addon/` files; 0.7.0 LOD overlay and fog families regenerated 2026-09-29 03:33).
Run103 = candidate DLL SHA-256 `1f3ad3dba226dfcb267b8fba2aa1c0eb1879b8adf58fafb2238b5378dfc211ef` (57,381,486 bytes,
RelWithDebInfo, unstripped), built once from clean main `ebfe33e4` (gates: host suite 274/2,895/0, 0 warnings, x87 0;
[build](../verification/results/run103-candidate-build.json)), installed 2026-09-29
([install](../verification/results/run103-candidate-install.json)). `X3AP.exe` unchanged; the user's edited `x3m.ini` kept;
`cxbottle.conf` not touched by the install (the user restored its two GStreamer entries after the vanilla test).

Rollback chain: Run102 `b7acb91f…` (release 0.7.0) at `/tmp/x3-run102-candidate/build/d3d9.dll`, then Run101 `8b164ee1…`,
then Run100 `20680908…`.

Run103 carries, over 0.7.0: the object-lifetime registry at 262,144 entries with backward-shift deletion and the
`object_lifetime_disabled` / `object_lifetime_stats` rows (run358: the observer disabled itself during a Mayhem 3 load,
reason unlogged, capacity inferred, so every TAA draw was refused at the scope gate and the jitter showed raw,
[note](reverse-engineering/object-lifetimes.md)); `hull_emission_gain` follows `emission_source_gain` through the config
file; `emission_source_clamp` and `lens_flare_gain` (both opt-in; run364: Mayhem nozzle cards at 3.0 engine value under
the hull gain, the red halos are Mayhem's own lens flares, [ledger](verification/screen-emission.md),
[sun occlusion](verification/sun-occlusion.md)); `--vanilla` refuses while the bottle's decoder variables are set
(a vanilla launch stalled at the loading screen, user-observed).

## Main beyond the installed build

Nothing: main `ebfe33e4` is the installed commit (release 0.7.0 = `9edd942b` is the last packaged one).

## Run queue

Run 103 A queued (2026-09-29): one flight on the Mayhem save with the Run103 candidate: stations at rest (shimmer gone?),
the new lifetime rows, and the Ocelot exhausts with `emission_source_clamp = 0.7` and `lens_flare_gain = 0.5`:
[run queue](verification/user-runs.md). Run 102 A is superseded (its flights became run357-run364, the shimmer and
engine diagnoses above).

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
- `x3m-regenerate` 0.7.0 ran on Mayhem 3 (2026-09-29 03:33: 1,157 bodies baked, 2,014 refused, 70 fog families); the visual
  check of the new bodies is still deferred by the user. Sky-derived fog for the 170 `no_dust_bodies` families is
  undecided ([probe](../verification/results/fog-families-mayhem/)).

History: handoffs, status history and completed runs are in [docs/archive/](archive/).
