# Project status

Updated 2026-09-25 21:40: Run89 DLL `0f6acab4…` from e2db7813 installed (single shadow map removed, cascades only; `--no-shadow-cascades` = no replayed shadows; cascade 3 at 4096). Run 89 A queued. The `--taa-k` / `--taa-sentinel` removal is in flight for the next candidate. Earlier: Run88 6fe194bd (shadow pop fix, adjacency fast without telemetry, promoted launcher defaults) accepted in Run 88 A. Run56 accepted for media stability: the user
reports no crash and no media-related stutter. The accepted production baseline
is merged to main. Run57 accepts the station-flash default correction. Fog-range and moving-lattice
work remain open. The agent never launches the game. See the [run queue](verification/user-runs.md) and the current
[handoff](handoff-2026-09-24-run79.md).

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run89 DLL SHA-256:
`0f6acab4ddce2f20101ab768725080b65a703488865567c3cd3718e7d6126ea8`
(56,806,407 bytes), built once from clean reviewed main `e2db7813` in a detached worktree (`/tmp/x3-run89-candidate/src`).
Retained DLL: `/tmp/x3-run89-candidate/build/d3d9.dll`. Installed 2026-09-25 21:40
([qualification](../verification/results/run89-candidate-qualification.json), [install](../verification/results/run89-candidate-install.json)).
Rollback: Run88 `6fe194bd…` at `/tmp/x3-run88-candidate/build/d3d9.dll` (accepted, Run 88 A), then Run87 `94c4ef42…`.

Changes against Run88: the **single shadow map removed** (user decision 2026-09-25: the cascades are the only replay geometry;
`--no-shadow-cascades` now means no map, no lease, no replay and the sun apply configured off with one
`sun_shadow_apply_mode ... enabled=0 ... cascades=0` row, the lighting stays the lane's own; the four
`--shadow-replay-size/-extent/-depth-half/-cap` options are gone, the launcher refuses them and the DLL logs one
`shadow_replay_config ... single_map=removed` row when a variable is inherited; the caster counter is idle without cascades;
the single-map apply program is deleted; e2db7813, [design](architecture/directional-shadows.md) "Single map removed",
[ledger](verification/directional-shadows.md)) and the **launcher default `--shadow-cascade-sizes 2048,4096,4096,4096,2048`**
(cascade 3 to 4096 after Run 88 A; ecd2aaae; launcher only, unflown). Not in this build: the `--taa-k` and `--taa-sentinel`
removal (decided after the freeze; next candidate). Game data unchanged.

Qualification at `e2db7813` (21 min wall): build 0 warnings (44 s); x87 119 roots / 687 reachable / zero violations; imports 232
from 15 DLLs unchanged (the crt `strcat` stays; native Windows unverified), 17 exports; host suite 272 modules / 2,824 tests /
0 failing; motion output 233 cases (234 - 2 deleted single-map cases + `seam-ownership-shadow-replay-no-cascades`), checks
345,941 -> 346,838, no case below Run88 (12 moved cases 2,021 -> 2,345, 6 extended 20,778 -> 21,600, 175 unchanged); sun share live
25 cases (`shadow_apply_no_cascades`: 0 apply attempts, no map); sun occlusion 74 / 128; fog route bridge 41,877 + 4; ownership
integration 26/26; four dry runs (default 176 variables with the new sizes; `--no-shadow-cascades` sends `X3M_SHADOW_CASCADES=0`
and no cascade tuning; short stand = default + telemetry; vanilla 121); the temporal, bloom, fog, gpu-sync and loading closures are
comment-only or byte-identical to Run88 and reused. Note: `--no-shadow-cascades` also leaves volumetric fog, caster retention and
the sun poll off because the launcher requires cascades for them (pre-existing rule). Open: the ownership fallback runner is
stale; no Wine case for the telemetry-off adjacency install path. Not a native Windows execution. The install record verifies
installed bytes and unchanged X3AP.exe `fdbf3418…` and cxbottle.conf `cc5d6c00…`.

## Current work and pending acceptance

- **Launcher defaults (2026-09-25, launcher only, no DLL change):** `manage.py launch` with no options now gives the Run 84 A stand environment minus its telemetry/debug options, plus music keep and alpha-tested casters; the stand command is `x3run --direct` plus the telemetry flags ([inventory](verification/launcher-options-inventory.md#defaults-promoted-2026-09-25)).
- **Media accepted:** Run200 identifies the intended DLL and enables the ID2 skip.
  The user reports no crash or media-related stutter. Preserved logs have no
  crash markers; per-call skip counts are intentionally not logged. Speech/music
  were not separately reported in this flight. Owned playback is retired.
  [Omission ledger](verification/media-cues.md#id2-video-omission-and-owned-playback-retirement-2026-09-21).
- **Fog:** the user confirms Run197 F8 was first-person. All 32 captured matrices
  failed the old fog-only tolerance despite passing the camera reader; corrected
  helper and D3D-route checks pass. Run199 confirms the visible fix by user report, with F8 in first person. Density
  remains 0.03 (the user's 1.50× preference); fog remains opt-in/off by default.
  Fourteen-family visuals, shafts and clear-sector travel retain their flight
  acceptance gaps. Camera-cut native-card replacement protection remains enabled.
- **New fog-range request:** the user wants 30–40 km patch visibility without
  abrupt appearance. Current support is 12,000 render units (2.4 km). Native
  object fading is separate. Filtered-far integration failed the offline comparison; the accurate reference
  converges, but the tested macro mask removes all nearby fog in the four views.
  The fixed coarse depth-prefix reconstruction also fails all four endpoint
  comparisons and is closed. The finite-bank preview is reviewed but not selected: the user prefers broader,
  connected clouds. Its fixed 500-unit sampler also fails accuracy gates.
  The user likes the broader interior but rejects the distant bulb. A subsequent
  sector-wide modulation also converges but leaves almost no clear sightlines,
  so it is not selected. The target is varied cloud shapes, sizes and internal
  density with more clear space throughout the sector. The mass/detail reference
  is now close to the user’s target; one modest increase in patchiness/internal
  contrast now has a reviewed two-view preview, with modestly clearer weak edges.
  The fixed global 64-step transport screen fails accuracy/temporal gates; its
  expensive corner-cache route is closed. The cheaper stored-density experiment ran once after review: strictly better
  than global64 (T p99 .00197, temporal .00184, 132 reads per ray). The user
  accepts its appearance from the fixed images; the sub-display .001 gate is
  rescaled to half a code (.002) and the route was reopened and is now
  implemented in four reviewed checkpoints behind `--volumetric-fog-range stored`
  (default legacy, bit-identical); it is in the Run60 DLL awaiting its first
  flight. GPU cost in game is unmeasured.
  No production integrator or new fog build is selected.
- **Station flash accepted:** Run205 confirms both global TAA heuristics disabled;
  the user reports no flash or save-load/sector-travel ghosting. Launcher defaults
  now apply this to the unchanged installed DLL; native fallback defaults match
  for the next build. Camera cuts, chase snaps and recovery remain enabled.
  [Evidence](verification/temporal-resolve.md#run57-global-heuristic-cuts-disabled-by-default-2026-09-21).
- **Lattice:** Run54 B returned run201 with three capture bursts. Guarded state
  and fixture comparison pass within their sampled scope. The frozen-subset CPU
  qualifier remains UNKNOWN; the calibrated standalone GPU point probe passes its measurement checks but
  measured vertex Z/W still does not qualify the CPU interpolation model. Further
  arithmetic exploration is closed. Targeted disassembly and deep review support private raw staging from original
  upload mappings, with valid payload only after successful construction. The portable
  staging core passes 39 host cases / 596 assertions and deep review; ownership
  readable-metadata prerequisite also passes 157 standalone D3D checks.
  The scoped ABI boundary passes 158 checks; the actual manual CloneMesh
  observer now passes 421 checks and independent review. The isolated game-call
  adapter passes 389 checks plus the 158-check ABI regression. Ownership pin lifecycle and paired copying pass 386 checks plus the 421-check
  manual regression. The F8 payload reader/collector passes 47 host tests and review. The B2b
  geometry packet writer is merged default-off and unwired (25 host tests, both
  state-only cross-builds). The ownership-on Capture fixture exposed a real
  production shader-shadow lifetime bug; the ratified scoped-getter fix passes
  the previously crashing routed case (159 checks, 2 getters + 2 Releases per
  injected draw) and is under independent review for merge. **Decision
  2026-09-21 (user agreed):** the upload-payload diagnostic is held, not wired,
  built or flown; an independent design review found the geometry capture is
  not on the path to rotation crawl and the thin-region gate closes under
  rotation. The camera-relative gate with a 7×7 box clip was accepted for pans
  in Run 59 and is the default; Run60 makes it depth- and translation-aware for
  forward flight and adds the default-off `--taa-unmatched-static` fallback for
  the one-frame approach flash. Roll residual is physical (gate fully open).
  The user tentatively prefers thin-region weight `0.94,1`; the 0.97 default
  stands until they confirm on Run60. Run 60 is queued.
  [Design review](architecture/lattice-approach-review-2026-09-21.md).
  [Owning note §31](architecture/taa-lattice-crawl.md#31-close-arithmetic-exploration-investigate-final-upload-observation).
- **Collision:** paused by user request; no moving-collision test is queued.
- **Engine/proxy timing:** lazy RT remains accepted. Corrected attribution
  does not justify another engine patch or busy-view timing flight.
  [Decision](architecture/engine-frame-time.md#run52-corrected-attribution-and-remaining-optimization-scope-2026-09-20).

Other defaults remain lazy motion RT binding, original hull shading, 2 px/all
small-parts culling, SSE2 collision plus memoization and light-map fade **80,220,1**.
Far stabiliser 0.985/thin region 0.97 remain explicit; stationary lattice
improvement is accepted and moving crawl is open. Native Windows runtime remains
unverified. Accepted production source and its evidence are on main; new fixes remain separate.
