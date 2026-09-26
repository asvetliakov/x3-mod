# Project status

Updated 2026-09-26 04:00: Run90 DLL `6f6be732…` from f9cceb17 installed (logging tiers: `--debug` / `--perf`, `x3m.log` in the game directory; obsolete options and code removed; `--taa-k` / `--taa-sentinel` removed; plus Run89's single shadow map removal and cascade 3 at 4096, never flown). Run 90 A queued (supersedes Run 89 A). In flight for the next candidate: the developer-option trim, inventory reorganisation, section-5 variable removal. Earlier: Run88 6fe194bd accepted in Run 88 A. Run56 accepted for media stability: the user
reports no crash and no media-related stutter. The accepted production baseline
is merged to main. Run57 accepts the station-flash default correction. Fog-range and moving-lattice
work remain open. The agent never launches the game. See the [run queue](verification/user-runs.md) and the current
[handoff](handoff-2026-09-24-run79.md).

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run90 DLL SHA-256:
`6f6be732bfd5a8b7b0819ae233d42128e55af5f31e42f727abc4272c54dffb98`
(56,825,745 bytes), built once from clean reviewed main `f9cceb17` in a detached worktree (`/tmp/x3-run90-candidate/src`).
Retained DLL: `/tmp/x3-run90-candidate/build/d3d9.dll`. Installed 2026-09-26 04:00
([qualification](../verification/results/run90-candidate-qualification.json), [install](../verification/results/run90-candidate-install.json)).
Rollback: Run89 `0f6acab4…` at `/tmp/x3-run89-candidate/build/d3d9.dll` (unflown), then Run88 `6fe194bd…` (accepted, Run 88 A).

Changes against Run88 (the last flown build): the **single shadow map removed** (cascades only; `--no-shadow-cascades` = no
replayed shadows; e2db7813), **cascade 3 at 4096** (`--shadow-cascade-sizes 2048,4096,4096,4096,2048`, launcher default),
**`--taa-k` and `--taa-sentinel` removed** (k derived from the exposure, sentinel policy auto; ae16da06), the **obsolete
options and their DLL code removed** (39 option strings, 24 variables off the default launch; --lod-scale, --mesh-cache,
--loading-intervals, --audio-sites, --fog-shadow-pass, the far24 and 16-tap programs, the screen thin-region gate; a device
without FP16/R32F filtering turns TAA and jitter off; 9a668e81; [inventory](verification/launcher-options-inventory.md)
section 4) and the **logging tiers** (`X3M_DEBUG` / `X3M_PERF` expanded in the DLL; the always tier is bounded, about
0.2 MB per hour: a no-option launch used to write about 750 MB per hour from seven ungated rows; `x3m.log` in the game
directory, the previous launch's as `x3m.prev.log`, `%LOCALAPPDATA%\x3-modern-renderer` fallback; a 4 MiB buffer drained by a
writer thread, no file I/O on game threads; a chained unhandled-exception filter writes one `exception` row; exit path:
device contexts alive at ExitProcess are abandoned and the writer parks on the last device destroy; launcher `--debug` /
`--perf` replace 19 telemetry options, the stand command is `x3run --direct --debug --perf`; f9cceb17,
[design](architecture/logging-tiers.md)). Game data unchanged.

Qualification at `f9cceb17` (48 min wall): build 0 warnings; x87 122 roots / 709 reachable / zero violations; imports 232 -> 236
(kernel32 MoveFileExW, DeleteFileW, FindFirstFileW/FindNextFileW/FindClose, GetFileAttributesExW, CompareFileTime,
FreeLibraryAndExitThread, TryAcquireSRWLockExclusive, SetUnhandledExceptionFilter added; the crt file-lock functions gone;
`strcat` stays; native Windows unverified), 17 exports; host suite 268 modules / 2,792 tests / 0 failing; motion output 231
cases / 346,382 checks, every case at its committed count (vs Run89: 5 deleted, 3 added); sun share live 25; sun occlusion
74 / 128; fog route bridge 32,039 (15 shadow-pass names gone); fog shader fixture 28/28 gates with identical accepted hashes;
temporal 744/278 and 584/90; gpu sync 32/32; bloom 47/47; loading trace, crypt cache 237, resource reader 4,721, cursor 42,
exports, game phase, FOV/window/sun-flare/LOD/cull/chase/collide fixtures at their committed counts; ownership integration
26/26; dry runs: default 124 variables (no X3M_DEBUG/PERF; cascade sizes 2048,4096,4096,4096,2048), `--debug --perf` 126,
`--no-shadow-cascades` 118, `--vanilla` 73, the old Run 84 A stand command exits 2 (its options are gone); dry_run_tiers 8/8,
compare_dry_runs 5/5. Incident: the standalone fog shader fixture (builtin d3d9, proxy not loaded) stalled once at process
exit after all 146 images, a known Wine teardown stall (triage 2026-09-26); retry passed. Open: the ownership fallback
runner is stale; research runners with changed closures (media playback, light/submit phase, voice, linear, rigid) not run.
Not a native Windows execution. The install record verifies installed bytes and unchanged X3AP.exe `fdbf3418…` and
cxbottle.conf `cc5d6c00…`.

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
