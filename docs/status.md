# Project status

Updated 2026-09-21 (night): the Run60 candidate is installed and Run 60 is queued; Run 59 accepted the camera-relative gate for pans. Run56 (run200) is accepted for media stability: the user
reports no crash and no media-related stutter. The accepted production baseline
is merged to main. Run57 accepts the station-flash default correction. Fog-range and moving-lattice
work remain open. The agent never launches the game. See the [run queue](verification/user-runs.md).

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run64 DLL SHA-256:
`e839dc7c3018d572582628318723f72a70f8d640c6b12205de63014e66ad0b1b`
(55,470,739 bytes), built once from clean reviewed main `6837a968`.
Retained DLL: `/tmp/x3-run64-candidate/build/d3d9.dll`. Installed 2026-09-22.

Changes against Run63 (`b0cde491…`; user-accepted shaft fix and retention), both
default-off: (1) sun occlusion now eligible on the main view's re-probe of the
background-owned sun record, absolute radius, and step 2: the core disc is clipped
per pixel against scene depth, glare scaled by the visible fraction
([design](architecture/sun-partial-occlusion.md), [ledger](verification/sun-occlusion.md));
(2) `--hull-emissive-widening K,Q0,Q1`: distance-scaled gradient widening of the
light-map fetch in all 100 gained SM3 hull programs, for torn/flickering thin glow
strips ([design](architecture/hull-emissive-widening.md),
[ledger](verification/hull-emissive-widening.md), [problem](architecture/thin-glow-lines.md)).

[Qualification](../verification/results/run64-candidate-qualification.json): host
suite 2,243 tests pass; linked audit 100 roots / 636 reachable / zero violations;
imports identical to Run63; every Run63 fixture case identical; new widening cases
(strip floor 0.50/0.75/0.867/0.875 at K=off/2/3/4, 11 real family pairs bit-identical
at k=1) and the sun fixtures (hook 74, GPU 122) pass. Not a full renderer-suite pass
or native Windows execution. The
[install record](../verification/results/run64-candidate-install.json) verifies
installed bytes and unchanged X3AP.exe `fdbf3418…` and cxbottle.conf `cc5d6c00…`.
Rollback: Run63 `/tmp/x3-run63-candidate/build/d3d9.dll`, then Run62 and older.
All three Run 64 dry-runs passed; no game was launched by the agent.

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
