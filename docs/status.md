# Project status

The single current-state file (updated 2026-09-29, Run108). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**, game tree modded with **Mayhem 3** since 2026-09-28 (user install,
`addon/05..12.cat` plus loose `addon/` files; 0.7.0 LOD overlay and fog families regenerated 2026-09-29 03:33).
Run108 = candidate DLL SHA-256 `c57556bbfc6d4e5feee7299a32291237643ad75eefa265bc54662ece4ef2fd0f` (57,518,024 bytes,
RelWithDebInfo, unstripped), built once from clean main `21726898` (host suite 275/2,905/0, 0 warnings, x87 0, census
fixture 28 checks; [build](../verification/results/run108-candidate-build.json)), installed 2026-09-29
([install](../verification/results/run108-candidate-install.json)). `X3AP.exe`, `cxbottle.conf`, the user's `x3m.ini`
and the 0.8.0 `x3m-regenerate` binaries unchanged.

Rollback chain: Run107 `b9e8793c…` at `/tmp/x3-run107-candidate/build/d3d9.dll`, then Run106 `d07848e8…`, then Run104 = release 0.8.0 `f34d3ab4…` (zip at `/tmp/x3m-release-0.8.0/`, entry `d3d9.dll`, extracted copy
at `/tmp/x3m-release-0.8.0/zip-extract/d3d9.dll`), then Run103 `1f3ad3db…` at `/tmp/x3-run103-candidate/build/d3d9.dll`.

Run108 over 0.8.0: the `scene_graph_census` row (engine node/scene/cut/task counts, newest unattached nodes by body,
insert-caller histogram; [note](reverse-engineering/object-lifetimes.md) Run382); four `loop_phases` stamps around the three calls of the main loop's `input_part=0` region
(`cutevent`, `containers`, `sweep`, `region`; [note](reverse-engineering/main-loop-input-region.md) §6); `lens_flare_gain 0` skips the admitted lens-flare draws instead of drawing them invisibly, and the
opt-in `cull_small_props` (proxy-side skip of `ships\props\` draws under the small-parts pixel threshold, own ship and
target exempt, no engine write; since Run106 the size test uses the draw's own vertex extent, the engine's part box
being 4.7x+ the drawn geometry on Mayhem turrets, and `frame_end` carries `issued=`) ([cull-small-parts](verification/cull-small-parts.md),
[sun occlusion](verification/sun-occlusion.md)). Motivation: run375 draw attribution
([results](../verification/results/run375-draw-attribution/)): 352 game draws at a Mayhem battle group = 169 lens-flare
sprites (stock bodies `objects/v/00752..00766`, ~8 per engine) + 82 Split turret props (76 under 4 px) + 28 ship hulls +
25 carrier + 14 station + 10 engine glows + 5 sky + 9 HUD.

Last release: **0.8.0** from `a3c85e37` ([release record](../verification/results/release-0.8.0.json)).

## Main beyond the installed build

Nothing beyond the installed Run105 (`99734bdb`); release 0.8.0 (`a3c85e37`) is the last packaged build.

## Run queue

Run 108 A queued (Run108 installed): a 2-3 minute stay in any Mayhem
sector under `--perf` to read `scene_graph_census` (engine node count against the proxy's registry, the newest
unattached nodes' bodies, the insert-caller histogram). Diagnosis so far ([note](reverse-engineering/object-lifetimes.md)
Run382, [scheduler note](reverse-engineering/script-task-scheduler.md)): the engine registry gains 27+ scene nodes per
frame on Mayhem 3 (0.2 on stock) and removes none; `0x0048f550` is the scene-graph animation tick that walks every node
each frame (66-92 ns per accumulated node), so one leak explains the fps decline, the registry overflow at any capacity
(run382: `capacity_exhausted` at frame 10079 with live=262144) and the shimmer's return after ~5 minutes; a reload
clears it.

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
