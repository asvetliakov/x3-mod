# Project status

Updated 2026-09-23 (night): the Run75 candidate is installed (fog hand-over fixes from Run 73 B with sector-id transit identity, bolt visibility rule 3,12, TAA shader cuts, timing sub-boundaries for the TAA span and the fog route, repair census); Run 75 A/B/C queued, Run 74 A open; earlier: the Run73 candidate is installed (fog hand-over R1-R3 with the docked walk, bolt footprint 3,8 default, on top of everything in Run72); Run 73 A flew on the Run72 DLL with the batch overlay (overlay bodies 2-4 draws; the remaining 200+ draws are refused texel_floor bodies, single-LOD pipes and signs; baker fix in progress); Run 73 B/C queued; earlier: the Run72 candidate is installed (box cull, forward reticle, 1.05 boom, pause key-only, decoder discovery, identity without a file hash, music trace/keep opt-in, data-driven fog families); Run 72 A/B queued; Run 71 accepted the merged-LOD atlas overlay built from the LOD 0 meshes (no visible transition; ships 2 draws, outpost 4); next is the fleet batch with mod support; earlier the Run70 candidate was installed (motion weight 0.7,2,8 and dust motes 1300,3 as defaults, both accepted in Run 70 A/B); Run 68 and Run 69 A-D flew: the SETA exit reset accepted and made default with strict sky history, the fog shadow pass stays off, the merged-LOD pilot overlay works (engine glows and lighting recovered step by step, atlas build next); earlier, Run 67 flew: footprint 8 accepted as the default, the SETA residual diagnosed and fixed behind an option, no cullable draw bucket left, fog pass needs its A/B; the merged-LOD pilot is tooled (bob1.py, lod_overlay.py); lod-scale stays off by user decision; Run 65 accepted the sun edge fix, core dimming, widening 4 with the emissive vote and the fog regression; Run 59 accepted the camera-relative gate for pans. Run56 (run200) is accepted for media stability: the user
reports no crash and no media-related stutter. The accepted production baseline
is merged to main. Run57 accepts the station-flash default correction. Fog-range and moving-lattice
work remain open. The agent never launches the game. See the [run queue](verification/user-runs.md) and the current
[handoff](handoff-2026-09-24.md).

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run75 DLL SHA-256:
`56d6863ebecef6d696b574f8e5436a922b0bbeb5d7919a224f9e4b167dc91bbb`
(55,944,691 bytes), built once from clean reviewed main `db13d929` (attempt 2; attempt 1 from `72645b5e` failed the
route bridge). Retained DLL: `/tmp/x3-run75-candidate/build/d3d9.dll`. Installed 2026-09-23 night.

Changes against Run73 (`3eadf8e5…`): fog hand-over fixes ([note](architecture/fog-handover.md), "Run 73 B findings and
fixes" and "Transit identity"): no stale first fill at the source sector's camera on arrival, a confirmed prefill is
adopted, the fill starts during the loading stall, same-family transits keyed on the sector id are cold transits, the
`volumetric_fog_cards` line names the first failing gate/readiness component (`refusal=gate:…|ready:…`) for the still
unpinned docked-load case; bolt footprint `--bolt-footprint 3,12` ([note](architecture/bolt-footprint.md): minimum width
and length in the camera plane, chase-view gate, `bolt_footprint_hist`); TAA stage shader cuts, output byte-identical
([note](architecture/engine-frame-time.md#taa-stage-cost)); `--gpu-sync-timing` now 22 passes (taa_* and fog_march /
fog_composite / fog_repair sub-boundaries, `volumetric_fog_repair_census`), so a timed `taa` reads ~1.3 ms and
`fog_route` ~0.8 ms higher than Run 274 for the same work. Game data: the re-baked 22-body overlay in `addon/05`
([record](../verification/results/lod-overlay-batch/install-run74/install.json)); fog families file absent.

[Qualification](../verification/results/run75-candidate-qualification.json): x87 116 roots / 673 reachable / zero
violations; imports and exports unchanged vs Run73; temporal pass RESULT PASS 744 / 278, 546 samples (report
byte-identical); motion output 190 cases / 271,369 checks identical; fog pass 123 checks (census check added), 30/30
gates, HANDOVER ready 575.9 ms; route bridge 110 names PASS (36,585 checks; the per-frame count varies with fill
timing); fog family 67 cases; GPU sync fixture 32/32, 44 queries + 1 census; identity 23/23 on four EXE variants;
pause/music verifiers PASS (music keep writes 48). Not a native Windows execution. The
[install record](../verification/results/run75-candidate-install.json) verifies installed bytes and unchanged X3AP.exe
`fdbf3418…` and cxbottle.conf `cc5d6c00…`. Rollback: Run73 `/tmp/x3-run73-candidate/build/d3d9.dll`, then Run72. The
dry run after the install passed; no game was launched by the agent. Known: Wine fixtures occasionally hang at exit
after a full PASS (fog pass in attempt 1, GPU sync in attempt 2; cause unknown, rerun clean).

## Current work and pending acceptance

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
