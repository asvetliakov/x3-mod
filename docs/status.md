# Project status

The single current-state file (updated 2026-09-29, Run105). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**, game tree modded with **Mayhem 3** since 2026-09-28 (user install,
`addon/05..12.cat` plus loose `addon/` files; 0.7.0 LOD overlay and fog families regenerated 2026-09-29 03:33).
Run105 = candidate DLL SHA-256 `5de8ed5d352f195981c28bcf012f3bf64242fa2711f8318e0d7814f5f9d1fc76` (57,430,813 bytes,
RelWithDebInfo, unstripped), built once from clean main `99734bdb` (host suite 274/2,898/0, 0 warnings, x87 0;
[build](../verification/results/run105-candidate-build.json)), installed 2026-09-29
([install](../verification/results/run105-candidate-install.json)). `X3AP.exe`, `cxbottle.conf`, the user's `x3m.ini`
and the 0.8.0 `x3m-regenerate` binaries unchanged.

Rollback chain: Run104 = release 0.8.0 `f34d3ab4…` (zip at `/tmp/x3m-release-0.8.0/`, entry `d3d9.dll`, extracted copy
at `/tmp/x3m-release-0.8.0/zip-extract/d3d9.dll`), then Run103 `1f3ad3db…` at `/tmp/x3-run103-candidate/build/d3d9.dll`.

Run105 over 0.8.0: `lens_flare_gain 0` skips the admitted lens-flare draws instead of drawing them invisibly, and the
opt-in `cull_small_props` (proxy-side skip of `ships\props\` draws under the small-parts pixel threshold, own ship and
target exempt, no engine write) ([cull-small-parts](verification/cull-small-parts.md),
[sun occlusion](verification/sun-occlusion.md)). Motivation: run375 draw attribution
([results](../verification/results/run375-draw-attribution/)): 352 game draws at a Mayhem battle group = 169 lens-flare
sprites (stock bodies `objects/v/00752..00766`, ~8 per engine) + 82 Split turret props (76 under 4 px) + 28 ship hulls +
25 carrier + 14 station + 10 engine glows + 5 sky + 9 HUD.

Last release: **0.8.0** from `a3c85e37` ([release record](../verification/results/release-0.8.0.json)).

## Main beyond the installed build

Nothing beyond the installed Run105 (`99734bdb`); release 0.8.0 (`a3c85e37`) is the last packaged build.

## Run queue

Run 105 A queued (2026-09-29): draw-cost measurement at the battle group, two launches from the launcher (the ini is not
read): `--lens-flare-gain 0 --cull-small-props on` versus the defaults, same spot, frame times compared; turrets watched
for normal behaviour: [run queue](verification/user-runs.md). Run 104 A (plain flight on the 0.8.0 defaults) folds into
it: the baseline launch is that flight.

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
