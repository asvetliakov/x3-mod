# Project status

Updated 2026-09-21: Run56 (run200) is accepted for media stability: the user
reports no crash and no media-related stutter. The accepted production baseline
is merged to main. Run57 accepts the station-flash default correction. Fog-range and moving-lattice
work remain open. The agent never launches the game. See the [run queue](verification/user-runs.md).

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run56 DLL SHA-256:
`a51d1e75fa80d7d07bab7ab66004291f7248bc5693e6585171e564a90fa96e56`
(54,386,310 bytes), built once from clean reviewed source
`85da89a8955a72b20d92f2309e4b9a4cb4dd325e` on **`fix/id2-video-omission`**,
checkout `/tmp/x3-run56-integration`. Retained DLL:
`/tmp/x3-run56-candidate/build/d3d9.dll`. This accepted production baseline is now on main; the installed bytes remain
the previously qualified build, with no rebuild or reinstall for the merge.

The user accepts missing ID2 animated textures. The candidate skips their silent
video construction through the existing allocator gate and removes the owned
playback runtime, startup/consumer/destination hooks, LAV SDK dependency and
media-package launch prerequisite. Speech, music and unrelated media keep their
existing paths. The first-person fog correction, fourteen spatial profiles/shafts
and protected lattice observer remain. **Run200 is accepted by the user for no crash and no media-related stutter.**

[Qualification](../verification/results/run56-candidate-qualification.json): full
host discovery completed **2,627 tests in 735.394 s**, two skips, one stale
CreateDevice fixture failure. That sole failure was corrected and independently
reviewed; its focused rerun passes **19 scenarios / 202 checks**. The initial
full run was not an all-pass run. The focused x86 omission fixture passes
**3,904 checks**, including 96,000 foreign-thread calls. Linked audit passes
**95 roots / 542 reachable functions / zero violations**. Two selected checks
on the actual DLL pass: ownership/HDR **43 checks / 39 restorations**, TAA/HDR
**83 / 51**; this is not a full renderer-suite pass or native Windows execution.

Installation used `python3 tools/manage.py install --bottle X3 --dll-source
/tmp/x3-run56-candidate/build/d3d9.dll` from the new checkout. The
[install record](../verification/results/run56-candidate-install.json) verifies
installed bytes and unchanged X3AP.exe, cxbottle.conf and original `mov/00002.dat`.
Active media selection is removed; the exact Run55 DLL/manifest and its provider
files remain valid for rollback under
`drive_c/X3/x3-modern-media/rollback/fb0fda8949bf4d6c8cb91f6a055361f8`.
The affected launch dry-run passed; no game was launched by the agent. The
completed Run56 command is archived. Future runs omit CrossOver debug tracing.

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
  expensive corner-cache route is closed. A cheaper stored-density representation
  is being designed.
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
  manual regression. The F8 payload reader/collector passes 47 host tests and review. Capture
  Release/Reset accounting and F8 attachment remain to integrate. No repeat
  flight or renderer correction is selected.
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
