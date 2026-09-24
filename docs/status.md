# Project status

Updated 2026-09-24 09:00 (handoff-2026-09-24-run79.md authoritative): Run79 DLL `d3683ced…` from df01f23b installed (TAA A' region hold, Terran station LOD patch), Run 79 A queued; Run 78 A accepted dither/exit/S3, found the Terran distance branch. Earlier 07:00: Run78 DLL `d4ba9f05…` from ee3bbf88 installed (exit fix, scale 4 default, dither, TAA S3) with the 611-body fleet overlay; Run 78 A queued; the ps_3_0 slot budget measured (512 is a floor; plan against 32768, AGENTS.md); lifted-cap TAA plan ratified, A' in flight. Earlier 04:40: scale 4 default + crash fix + baker on main beyond the DLL; dither and TAA S3 agents in flight; fleet rebake running. Earlier 03:10: Run77 DLL `268db207…` from bc47873b installed (fog step C, TAA S1 + mask split, alpha-test admission, mask cut) with the two-slot fleet overlay; Run 77 A-D queued; translation no-go measured, DXVK fork blocked on MoltenVK; thin-geometry design ratified (A' + S4 after S3). Earlier: Run76 installed; Run 76 A-D flown (overlay, bolts accepted; docked load pinned to alpha test, fix on main; far bins 24 not worth it); fleet overlay bake running; D3D11 post chain closed by probe, TAA high-res plan ratified; three agents in flight (fog step C, translation design, TAA S1/S2); see the [handoff](handoff-2026-09-24-run79.md); earlier: the Run76 candidate is installed (projectile cull exemption, docked-load fog card admission with the state diagnostic, --fog-far-bins 40|24 variant, fog sub-boundaries and repair census); Run 76 A-D queued; earlier: the Run75 candidate is installed (fog hand-over fixes from Run 73 B with sector-id transit identity, bolt visibility rule 3,12, TAA shader cuts, timing sub-boundaries for the TAA span and the fog route, repair census); Run 75 A/B/C queued, Run 74 A open; earlier: the Run73 candidate is installed (fog hand-over R1-R3 with the docked walk, bolt footprint 3,8 default, on top of everything in Run72); Run 73 A flew on the Run72 DLL with the batch overlay (overlay bodies 2-4 draws; the remaining 200+ draws are refused texel_floor bodies, single-LOD pipes and signs; baker fix in progress); Run 73 B/C queued; earlier: the Run72 candidate is installed (box cull, forward reticle, 1.05 boom, pause key-only, decoder discovery, identity without a file hash, music trace/keep opt-in, data-driven fog families); Run 72 A/B queued; Run 71 accepted the merged-LOD atlas overlay built from the LOD 0 meshes (no visible transition; ships 2 draws, outpost 4); next is the fleet batch with mod support; earlier the Run70 candidate was installed (motion weight 0.7,2,8 and dust motes 1300,3 as defaults, both accepted in Run 70 A/B); Run 68 and Run 69 A-D flew: the SETA exit reset accepted and made default with strict sky history, the fog shadow pass stays off, the merged-LOD pilot overlay works (engine glows and lighting recovered step by step, atlas build next); earlier, Run 67 flew: footprint 8 accepted as the default, the SETA residual diagnosed and fixed behind an option, no cullable draw bucket left, fog pass needs its A/B; the merged-LOD pilot is tooled (bob1.py, lod_overlay.py); lod-scale stays off by user decision; Run 65 accepted the sun edge fix, core dimming, widening 4 with the emissive vote and the fog regression; Run 59 accepted the camera-relative gate for pans. Run56 (run200) is accepted for media stability: the user
reports no crash and no media-related stutter. The accepted production baseline
is merged to main. Run57 accepts the station-flash default correction. Fog-range and moving-lattice
work remain open. The agent never launches the game. See the [run queue](verification/user-runs.md) and the current
[handoff](handoff-2026-09-24-run79.md).

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run79 DLL SHA-256:
`d3683ced009245e7891ad2da5994d219e33390c1d3b522f179d4bf35ea4d1d0f`
(56,358,862 bytes), built once from clean reviewed main `df01f23b` in a detached worktree. Retained DLL:
`/tmp/x3-run79-candidate/build/d3d9.dll`. Installed 2026-09-24 08:56
([qualification](../verification/results/run79-candidate-qualification.json), [install](../verification/results/run79-candidate-install.json)).
Rollback: Run78 `d4ba9f05…` at `/tmp/x3-run78-candidate/build/d3d9.dll` (accepted in Run 78 A), then Run77 `268db207…`
(exit crash).

Changes against Run78 (`d4ba9f05…`), both unflown: **TAA A'** region hold in the resolve, `--taa-region-hold on|off`
default on, drops the two dilation draws (df01f23b, [plan](architecture/taa-plan-lifted-slot-cap.md),
[ledger](verification/temporal-resolve.md)); **Terran station LOD patch**, `--terran-station-lod size|distance` default
size, a byte-verified 2-byte patch at 0x0047d01c so Terran stations use the screen-size LOD loop and can reach their
merged-LOD record (dd862b9f, [RE](reverse-engineering/lod-selection.md) §5); census rows carry `flag31=`; route bridge
harness fix (026d24ac). Programs above 512 slots by design (AGENTS.md "Shader slot budget"). Game data: fleet overlay
`addon/05` (488 bodies) + `addon/06` (123), 611 bodies ([record](../verification/results/lod-overlay-batch/install-fleet2/install.json));
fog families file absent.

Qualification at `df01f23b`: host suite 251 modules / 2,572 tests; x87 116 roots / 673 reachable / zero violations;
imports/exports unchanged vs Run78; temporal pass 744 / 278 (report byte-identical), lattice 566 / 91; motion output 199
cases (re-pinned; thin-hold on/off cases added); fog pass 52/52 gates; route bridge 110 names PASS (42,273 checks); fog
family 67 cases; GPU sync 32/32; cull census 104 (flag31 rows); cull fixture 132; bloom 47 images max 1 code; identity 41
anchors, corrupt-site cases FAIL with identity true; Terran site verifier 16/16; pause/music/cull site verifiers PASS;
five dry runs exit 0. Not a native Windows execution. The install record verifies installed bytes and unchanged X3AP.exe
`fdbf3418…` and cxbottle.conf `cc5d6c00…`.

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
