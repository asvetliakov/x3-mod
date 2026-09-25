# Project status

Updated 2026-09-25 20:25: Run88 DLL `6fe194bd…` from 79aafc8b installed (shadow pop fix: the sun-occlusion bracket drifted the TAA reference count and flushed the caster retention store every frame; adjacency fast without telemetry; launcher defaults promoted with cascade sizes 2048,4096,4096,2048,2048); Run 88 A queued (one launch, short stand command). Also today: launcher options inventory, x3m-regenerate bundled tool in the game root, MetalSharp entries prepared (not launched), the single shadow map removal in flight. Earlier 18:06: Run87 DLL `94c4ef42…` from 22593133 installed (Run85 + fog loader; the two dropped opt-ins removed); nothing queued. Earlier afternoon: Run 86 A flown: plants sparkles fixed (run333, accepted); the effects modernisation (run334) and the chase view across docking (run335: selection boxes vanished after undock) were **dropped by the user** and removed from production on main ; Run87 candidate installed 18:06. Earlier 12:37: Run86 DLL `27881669…` from 9375a1e7 installed (all opt-in: effects stage phase 1, chase view across docking, fog family empty table; launcher report lines); fog family file installed under x3m/ (the effect key table removed again). Earlier ~12:00 (night): merged since the Run85 install: fog families empty-table loader + launch line (28cbd43e, DLL change), --chase-view-restore-dock opt-in (40cd3023, unflown), LOD overlay for selected mod packages + launch line (b36925d0), effects modernisation design ratified (93739047; phase 1 in flight; Run 85 A launch 2 = combat capture). Earlier 09:25: Run85 DLL `c430294a…` from 53e7aba5 installed (far clip 7x7 + far ramp 60/68: the fog-band plants' sparkles at rest and under pans; design taa-thin-classification.md ratified); Run 85 A queued (one launch with --taa-debug). Run 84 A flown (run329/330/332) and triaged: rows ok; the cursor at launch is a Cocoa-driver matter, parked by the user; the sparkles occur at rest too (partial far weight + 3x3 erasure, not the far gate). Night work in flight (user asleep): LOD overlay with selected mods (design), chase view restored after docking (RE), effects modernisation (two designs), fog families for mods (merged 89f4b014). Earlier 07:15: Run84 DLL `189a34f0…` from 46cd4f9d installed (far stabiliser gate on the camera-relative openness `--taa-far-gate camera` default, `--sun-occlusion` default with the core dimming, cursor re-assert arming at launch, fill default 0.01); Run 84 A queued (two launches, no timing flag). Run 83 A/B flown (run323-327) and triaged: FOV load remap, window move and the TAA saving (-1.6 to -1.8 ms) confirmed; the sun pop at half cover is the vanilla probe (partial occlusion accepted, default); the fog-plant sparkles under a pan are the far weight's screen-speed gate (switchable now). Earlier 04:30: Run83 DLL `f0259ceb…` from 80394271 installed (FOV savegame load remap, TAA mask fold with the sentinel class retired and the screen search off by default, window monitor rect default on + window trace / cursor re-assert opt-in, original-fill launcher default 0.01) on the install-fleet4 overlay (Terran louvre recipe, accepted run321); Run 83 A queued (three launches). Run 82 A flown (run312-318) and triaged: FOV menu/sun/chase accepted; a pre-patch save loaded vanilla (fixed); stabiliser 0 accepted then retired; vote source no saving; the lattice crawl was the coarse record's louvre gaps (baker fix); --gpu-sync-timing doubles the frame (real fps 50-60). Earlier 00:30: Run82 DLL `cd8ef8e4…` from d1a4e360 installed (FOV remap in game units, sun flare fix, chase compensation, S4 half default, cutout ownership, thin-region source opt-in); Run 82 A queued (three launches). Run 81 A flown (run309-311) and triaged: alpha casters and S4 half accepted; the menu overrode --fov and F 100 hid the sun (both fixed); stabiliser 0 shimmered on unowned cutouts (owned now). Earlier 22:20: Run81 DLL `b4e945ed…` from 9e1645be installed (defaults: thin vote, fade owner, occlusion all, FOV 58.7155, age programs; opt-in S4 half box and alpha-tested casters); Run 81 A queued (three launches). Run 80 A flown (run304-308) and triaged: occlusion patch, thin vote and fade owner accepted; ODS flicker = LOD pop without hysteresis; red plates brighter = baker constants (accepted); underside transition = alpha-tested casters. Earlier 19:50: Run80 DLL `593112dc…` from 9cf5decf installed (A' only + opt-in occlusion patch / thin vote / fade owner / lod-switch log + bolt telemetry); Run 80 A queued; fleet overlay rebaked and installed (install-fleet3, 620 bodies); Run 80 A ready to fly. Earlier evening (handoff-2026-09-24-run79.md authoritative): Run 79 A flown (run299-303): A' accepted (net -1.1 ms at 5120x1440, no visible difference), the hold-off chain removed on main (bb3a691f; `--taa-region-hold` no longer exists in the next DLL; the installed Run79 DLL still has it); Terran station LOD patch works; the coarse record lost its red plates (baker alpha rule fixed 5aa645e3, full rebake pending the user's go) and its ambient occlusion (engine LOD-0 gate; opt-in `--lod-occlusion all` patch ac381be7); one ODS part flickers under motion (inferred LOD pop; `--lod-switch-log` e8af9e46). Run 80 A queued; Run80 candidate pending. Earlier afternoon: on main beyond the installed Run79 DLL, all opt-in or tooling: TAA option B thin vote (`--taa-thin-vote`, default off), fade RT2 ownership (`--fade-rt2-owner`, default off), Terran patch Wine fixture, bolt shape-refusal telemetry, texture lookup RE + baker animation rule (next `--sync` rebakes every body), Wine-queue orphan cleanup; Run 79 A still queued and unflown. Earlier 09:00: Run79 DLL `d3683ced…` from df01f23b installed (TAA A' region hold, Terran station LOD patch), Run 79 A queued; Run 78 A accepted dither/exit/S3, found the Terran distance branch. Earlier 07:00: Run78 DLL `d4ba9f05…` from ee3bbf88 installed (exit fix, scale 4 default, dither, TAA S3) with the 611-body fleet overlay; Run 78 A queued; the ps_3_0 slot budget measured (512 is a floor; plan against 32768, AGENTS.md); lifted-cap TAA plan ratified, A' in flight. Earlier 04:40: scale 4 default + crash fix + baker on main beyond the DLL; dither and TAA S3 agents in flight; fleet rebake running. Earlier 03:10: Run77 DLL `268db207…` from bc47873b installed (fog step C, TAA S1 + mask split, alpha-test admission, mask cut) with the two-slot fleet overlay; Run 77 A-D queued; translation no-go measured, DXVK fork blocked on MoltenVK; thin-geometry design ratified (A' + S4 after S3). Earlier: Run76 installed; Run 76 A-D flown (overlay, bolts accepted; docked load pinned to alpha test, fix on main; far bins 24 not worth it); fleet overlay bake running; D3D11 post chain closed by probe, TAA high-res plan ratified; three agents in flight (fog step C, translation design, TAA S1/S2); see the [handoff](handoff-2026-09-24-run79.md); earlier: the Run76 candidate is installed (projectile cull exemption, docked-load fog card admission with the state diagnostic, --fog-far-bins 40|24 variant, fog sub-boundaries and repair census); Run 76 A-D queued; earlier: the Run75 candidate is installed (fog hand-over fixes from Run 73 B with sector-id transit identity, bolt visibility rule 3,12, TAA shader cuts, timing sub-boundaries for the TAA span and the fog route, repair census); Run 75 A/B/C queued, Run 74 A open; earlier: the Run73 candidate is installed (fog hand-over R1-R3 with the docked walk, bolt footprint 3,8 default, on top of everything in Run72); Run 73 A flew on the Run72 DLL with the batch overlay (overlay bodies 2-4 draws; the remaining 200+ draws are refused texel_floor bodies, single-LOD pipes and signs; baker fix in progress); Run 73 B/C queued; earlier: the Run72 candidate is installed (box cull, forward reticle, 1.05 boom, pause key-only, decoder discovery, identity without a file hash, music trace/keep opt-in, data-driven fog families); Run 72 A/B queued; Run 71 accepted the merged-LOD atlas overlay built from the LOD 0 meshes (no visible transition; ships 2 draws, outpost 4); next is the fleet batch with mod support; earlier the Run70 candidate was installed (motion weight 0.7,2,8 and dust motes 1300,3 as defaults, both accepted in Run 70 A/B); Run 68 and Run 69 A-D flew: the SETA exit reset accepted and made default with strict sky history, the fog shadow pass stays off, the merged-LOD pilot overlay works (engine glows and lighting recovered step by step, atlas build next); earlier, Run 67 flew: footprint 8 accepted as the default, the SETA residual diagnosed and fixed behind an option, no cullable draw bucket left, fog pass needs its A/B; the merged-LOD pilot is tooled (bob1.py, lod_overlay.py); lod-scale stays off by user decision; Run 65 accepted the sun edge fix, core dimming, widening 4 with the emissive vote and the fog regression; Run 59 accepted the camera-relative gate for pans. Run56 (run200) is accepted for media stability: the user
reports no crash and no media-related stutter. The accepted production baseline
is merged to main. Run57 accepts the station-flash default correction. Fog-range and moving-lattice
work remain open. The agent never launches the game. See the [run queue](verification/user-runs.md) and the current
[handoff](handoff-2026-09-24-run79.md).

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run88 DLL SHA-256:
`6fe194bdcd83e39fb45e71196c8c1815c030f8e05474aa7866264a510b3a07be`
(56,886,757 bytes), built once from clean reviewed main `79aafc8b` in a detached worktree (`/tmp/x3-run88-candidate/src`).
Retained DLL: `/tmp/x3-run88-candidate/build/d3d9.dll`. Installed 2026-09-25 20:25
([qualification](../verification/results/run88-candidate-qualification.json), [install](../verification/results/run88-candidate-install.json)).
Rollback: Run87 `94c4ef42…` at `/tmp/x3-run87-candidate/build/d3d9.dll` (unflown; = Run85 + fog loader), then Run86 `27881669…`.

Changes against Run87: **shadow pop fix** (the sun-occlusion bracket, a default since Run84, drifted the TAA reference count by one
per frame so the caster retention store was flushed every frame after a few hundred frames in every session since; the
acquisition now sits under `taa_call`, the counter clamps at 0 with one log row, the final-Release probe sums in 64 bits and logs
a rate-limited `shadow_retention_probe` row; 79aafc8b), the **mesh-adjacency fast path arming without `--telemetry`** (30eecc51),
and the **launcher defaults promoted** (the stand set is the default on a modded launch; `--shadow-cascade-sizes
2048,4096,4096,2048,2048`, `--music-keep`, `--shadow-alpha-casters on`, capture off by default; the short stand command in
[user-runs](verification/user-runs.md); 2356ba68). Game data unchanged.

Qualification at `79aafc8b`: build 0 warnings (45 s); x87 119 roots / 690 reachable / zero violations; imports 231 -> 232 (one new:
crt `strcat`; native Windows unverified), 17 exports; host suite 272 modules / 2,824 tests / 0 failing; motion output 234 cases (the
four shadow-retention cases grew: 12,055 / 12,052 / 8,304 / 12,202 checks, `n_route_lifecycle` sub-case; the rest 0 differing vs
Run87); temporal pass 744 / 278, lattice 598 / 90 identical rows; loading trace 112/123/36,093/36,150 (+ the
`mesh_adjacency_config` row), mesh cache hook, crypt cache 237, resource reader 4,721, loading intervals 163, cursor 42/42, ownership
integration 26/26 (the only fixtures that compile capture.cpp; the gpu-sync/cursor/lod/cull/chase/window/fov/sun-flare/sun-occlusion
closures are byte-identical to Run87 and reused); four dry runs (A = the short default command: 180 variables; B = the old stand;
C = A + `--taa-debug`; D = vanilla). Open: the ownership fallback runner is stale (does not read `x3m_add_fog_field_assets`);
no Wine case for the telemetry-off adjacency install path. Not a native Windows execution. The install record verifies installed
bytes and unchanged X3AP.exe `fdbf3418…` and cxbottle.conf `cc5d6c00…`.

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
