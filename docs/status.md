# Project status

The single current-state file (updated 2026-09-29, Run110). Rules: [AGENTS.md](../AGENTS.md). All goals were
marked completed on 2026-09-26 by the user's decision ([goals](goals.md)). The agent never launches the game.

## Installed build

Bottle **X3**, **CrossOver Preview.app**, game tree modded with **Mayhem 3** since 2026-09-28 (user install,
`addon/05..12.cat` plus loose `addon/` files; 0.7.0 LOD overlay and fog families regenerated 2026-09-29 03:33).
Run110 = candidate DLL SHA-256 `e5e7ac1585c5c7bd0a22d5b41b2ef16b40f356dbe368bcc5ac0fe9bf8f02d783` (57,626,713 bytes,
RelWithDebInfo, unstripped), built once from clean main `5ec34559` (host suite 277/2,922/0, 0 warnings, x87 0, cull fixture
221/0 with lens 58, verifiers 20/20, 13/13, 27/27; [build](../verification/results/run110-candidate-build.json)), installed
2026-09-29 ([install](../verification/results/run110-candidate-install.json)). `X3AP.exe`, `cxbottle.conf`, the user's `x3m.ini`
and the 0.8.0 `x3m-regenerate` binaries unchanged.

Rollback chain: Run109 `09f08cff…` at `/tmp/x3-run109-candidate/build/d3d9.dll`, then Run108 `c57556bb…` at `/tmp/x3-run108-candidate/build/d3d9.dll`, then Run107 `b9e8793c…` at `/tmp/x3-run107-candidate/build/d3d9.dll`, then Run106 `d07848e8…`, then Run104 = release 0.8.0 `f34d3ab4…` (zip at `/tmp/x3m-release-0.8.0/`, entry `d3d9.dll`, extracted copy
at `/tmp/x3m-release-0.8.0/zip-extract/d3d9.dll`), then Run103 `1f3ad3db…` at `/tmp/x3-run103-candidate/build/d3d9.dll`.

Run110 over Run109: `lens_flare_gain 0` now culls the lens-flare sprite nodes in the engine's own cull pass (a second
stub chained on the small-parts claim at `0x0047d2a2`; the 42 flare bodies resolved from the body table; the proxy skip
stays as fallback; [ledger](verification/sun-occlusion.md), [note](reverse-engineering/lod-selection.md) "Lens-flare cull"),
and the dock-port cull `cull_dock_parts_px` (default 12 screen px, `--cull-dock-parts`, 0 = off): carrier launch tubes
and hangars, stock dock scenes with inline bodies and a single LOD record ([note](reverse-engineering/ship-scene-parts.md)),
are culled in the same stub below that size (run385 replay: 159/27/27 of the bursts' 186/27/52 dock draws;
[ledger](verification/cull-small-parts.md) "Dock ports"). Purpose: measure whether not issuing the draws recovers the
engine's ~24 us per draw (the proxy-side flare skip of Run 105 A recovered ~1 ms only).
Run109 over Run108: the `dust_leak_fix` engine patch, on by default (`--dust-leak-fix off` / ini `dust_leak_fix = off`;
one 5-byte claim at `0x0041f4d1`, the dust-scene fill loop's common tail: a node whose dust body failed to load is
released through the engine's own `0x00487be0` and the fill ends for that frame; [ledger](verification/dust-leak-fix.md),
[note](reverse-engineering/object-lifetimes.md) Run383 "Patch"), and `cull_small_props` on by default (unset = on,
`off` turns it off; the `no_px` and `route_off` gates unchanged, so `--vanilla` and the bare DLL stay off).
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

Nothing beyond the installed Run110 (`5ec34559`); release 0.8.0 (`a3c85e37`) is the last packaged build.

## Run queue

No run is open. Run 110 A completed (run386-389, [results](../verification/results/run386-389-cull-ab/)): at one carrier
view without `--perf`, the dock-port cull (12 px) saves 2.4-3.7 ms per frame (about 57 fps against 47-50 with it off,
measured from 300-frame clock windows, one run per setting); the engine-side flare cull saves 0 ± 0.5 ms (1.2 ms under
`--perf`, where the flare draws carry the instrument cost). Both culled exactly what they should (166 flares per frame
culled = the 166 drawn before; `dock_culled` equals the census `culled_dock`, largest culled part s=7). Decision: the
dock cull stays on by default; `lens_flare_gain 0` keeps the engine cull but a flare batcher is not worth building.
`v\01009` and `v\10667` are flare-like bodies outside the 42-name set (about 0.2 draws per frame, left alone). Still
open from this pair of runs: frame time creeps with time since load at a constant draw count (run385: 16.4 -> 19.3 ms
over 3,600 frames at 335 draws), the pre-render drift noted below.

Run 109 A completed (run384, [results](../verification/results/run384-dust-leak-fix/)): with
`dust_leak_fix` patched the census stays flat (engine_nodes 4,515 -> 4,853 over 19,826 frames on `litcube75`, hits=300
per row), the animation tick `cutevent` stays at 0.1-0.4 ms (run383: 0.5 -> 10.8 ms) and the shimmer did not return.
Late frames (run384/run385 triage, [attribution](../verification/results/run385-draw-attribution/)): frame time follows
the draw count (engine `views` 24-26 us per issued draw); LOD selection correct in every census row (Raptor switch 5.7 km,
Ocelot 6.4 km); burst A's 642 draws = 186 dock-port parts + 168 flare sprites + 77 props + 48 hulls + 68 asteroids;
a second slow drift in the game's pre-render outside the stamped region (3.6 -> 5.8 ms over the stay) is unexplained.

Run 108 A completed (run383, [census](../verification/results/run383-scene-census/),
[dust leak](../verification/results/run383-dust-leak/)): the Mayhem 3 scene-node leak is the engine's dust-scene fill
`0x0041efc0` (the worker behind `INS_UpdateDustScene`, run every frame by the cockpit update): it allocates each dust
node before loading its body and drops the node on the unattached list when the body is missing, so the next frame
allocates again. Rate = the background's `NumDustInstances` (27 on `litcube75`). ZMap's Fog slider sets dust rates and
density on every row, but only 134 of the 317 nebula families in the Mayhem cats ship dust bodies: 170 of 248 rows
leak (stock: 1 row, `xtmgreenring`). `registry_live == engine_nodes` on every census row; the animation tick
`0x0048f550` costs 76.5 ns per node per frame (56 -> 33 fps over 5,100 frames). Report for the mod's maintainers:
[report-for-mayhem-author.md](../verification/results/run383-dust-leak/report-for-mayhem-author.md). The same 170
families are the fog baker's `no_dust_bodies` refusals, which explains the fog `card_refused` seen in run374; the user
chose to keep fog only where dust bodies exist (sky-derived fog not pursued).

## Open items

- From Run 91 A: bullets behind distant objects = the game's early bullet copy overpainted by later opaque draws while
  the additive route brightens both copies (fixed: Run94 composites the bolt back over held far/thin pixels, accepted in Run 94 A at W 1); station blur under a pan =
  history weight (0.85 accepted in Run 92 A; the rotation-aware weight stays opt-in and off after Run 93 A: the user prefers blur to shimmer).

- Run374 performance triage ([results](../verification/results/run374-performance/)): 23 ms p50 at 5120x1440 on Mayhem,
  the proxy's own CPU work about 1.2 ms of it, the rest in the game's pre-render/submit (CPU vs GPU not split without
  `--gpu-sync-timing`); Mayhem draws 1.6x stock; volumetric fog never applied in that sector (`card_refused`): the family has no dust bodies (Run 108 A).
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
