# Handoff 2026-09-22 evening — Run 66 flown, Run67 installed, Run 67 queued

Replaces the [2026-09-22 handoff](archive/handoff-2026-09-22.md); its operating rules
still bind (never launch the game; every Wine command through
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py`; one candidate owner;
install only with `tools/manage.py install --bottle X3 --dll-source <dll>`; native
Windows is a required, unverified target — [portability](architecture/platform-portability.md)).
Installed-build identity lives only in [status](status.md). Main is clean at `b671c5ec` (production source last changed at `1b4d745c`).
Run authority: [user-runs.md](verification/user-runs.md), now archived by
[`tools/analysis/archive_user_runs.py`](../tools/analysis/archive_user_runs.py)
(126 → 57 lines; `--check` is idempotent).

## State

- **Installed:** Run67 DLL `621cad63…` from `1b4d745c` ([status](status.md),
  [qualification](../verification/results/run67-candidate-qualification.json),
  [install](../verification/results/run67-candidate-install.json)). Rollback Run66
  `/tmp/x3-run66-candidate/build/d3d9.dll` (`1f9a85f5…`), then Run65 and older.
- **Run 67 is queued** ([user-runs.md](verification/user-runs.md)): A = SETA approach, slow pan
  and a close flyby with `--taa-sky-history strict` (band term default 3 px;
  `--taa-sky-history-band-px 2` if a faint band remains); B = the run248 stand with
  `--lod-scale 0.5 --object-bounds-log` (then `python3 tools/analysis/draw_accounting.py <run dir>`);
  C = fog with `--fog-shadow-pass on --shadow-cascade-min-footprint 8` at a shaft-rich spot,
  overlay against `--fog-shadow-pass off`. All three dry-runs passed.
- **Run 66 is flown and triaged**: session A as **run244** (SETA + pan with
  `--taa-sky-history strict`), session B as **run245/246/247** (lod-scale 0.25 / 0.5 /
  vanilla ladder) and **run248** (draw accounting). No agent, Wine process or build is running.

## Accepted since the previous handoff (all on main, all flown or measured)

1. **Strict sky history in flight** (run244): strict cut SETA dark-sky pixels **42 %**
   overall and **63 %** in the 1–2 px band; the residual is the far-scattered
   `motion_unmatched_static` population (67 % of what remains, −22 %), and the station
   softening is pre-existing history reprojection, not strict
   ([ledger](verification/temporal-resolve.md), "Run 244"; [note](architecture/seta-motion.md)).
2. **Band term** on top of strict (`--taa-sky-history-band-px`, default 3): the 1-px
   dilated sky border beside a fast-sliding silhouette refuses hull history, gated on
   translation parallax and unrouted pixels only, so pans and static edges stay
   bit-identical (fixture `adjacent_max` 0.738 → 0.000; [seta-motion.md](architecture/seta-motion.md) §4).
3. **Launcher defaults from Run 65**: widening `4` with the emissive vote
   ([ledger](verification/hull-emissive-widening.md)) and core dimming with
   `--sun-occlusion` ([ledger](verification/sun-occlusion.md)).
4. **Stored fog, one look**: L0/L1/L3, the look selector and `--volumetric-fog-look`
   are gone, output bit-identical ([ledger](verification/volumetric-fog.md), "Single look").
5. **Shadow cascade cost policy** decided and the first lever built:
   `--shadow-cascade-min-footprint P` (default off), per-part light-space minimum
   footprint per cascade, live and retained paths measuring the same sun-space AABB side
   ([policy](architecture/shadow-cascade-cost-policy.md),
   [law](architecture/shadow-cascades.md), [evidence](verification/directional-shadows.md)).
6. **Frame-time evidence at the stand**: the plateau is engine state submission, not the
   replay (0.7 ms) and not fog; `--lod-scale` accepts 0.25..4, and 0.25/0.5 both move the
   heavy station body to LOD 3 (19× fewer primitives), ~275 draws against vanilla 394,
   dt 17.4 vs 21.7 ms; run248 counts **367 draws/frame**, one scene pass, camera-local
   4 %, tiny-primitive draws 19 %
   ([engine-frame-time.md](architecture/engine-frame-time.md), Runs 245–248).
7. **Per-draw bounds**: `--object-bounds-log` logs the routed draw's own projected screen
   box on F8 frames (no second transform), and
   [`draw_accounting.py`](../tools/analysis/draw_accounting.py) buckets a frame into
   offscreen / occluded / tiny / partial / visible / no_box.
8. **Merged-LOD feasibility**: run-time batching is measured dead (444 of 448 draws
   unmergeable); coarser LOD records carry far fewer subsets (17 → 1 draws), so the
   recommendation is an asset-side coarse-LOD pilot
   ([merged-lod-feasibility.md](architecture/merged-lod-feasibility.md)).

## In flight, default-off, awaiting a verdict

- **Fog sun-visibility grid pass** (`--fog-shadow-pass on`, requires
  `--volumetric-fog-range stored`; off is bit-identical): quarter-res 64-slice RGBA8
  grid, three-cascade cross-fade, four stratified taps with a blocker-distance penumbra;
  march 425 → 352 and repair 510 → 453 slots, 19 extra device calls, grid failure falls
  back to the in-march lookup. GPU time is unmeasured (no timestamp queries on this
  bottle), so an at-rest frame-time A/B in flight decides
  ([design + as built](architecture/fog-shadow-pass.md),
  [ledger](verification/volumetric-fog.md)).
- **Minimum caster footprint** (`--shadow-cascade-min-footprint`, default off):
  fixture-qualified, never flown; the cap ceiling needs no code.
- **Strict sky history and the band term** are both still opt-in, and the widening Q band
  and real DXT1 mip normalisation remain unverified in flight
  ([thin-glow-lines.md](architecture/thin-glow-lines.md) §8).

## Decisions (user, 2026-09-22)

- Widening **K=4 with the emissive vote** as launcher defaults; **core dimming** is the
  default with `--sun-occlusion`.
- The farthest shadow cascade **stays ≥ 90–100 km**; 22.5 km is rejected ("too low").
- **State-call elision is not adopted**, now measured: a redundant native
  `SetRenderState` is 10 ns under FEX, there is no per-call emulation boundary, and
  eliding would cost ~1.1 ms more than it saves
  ([state-call-fast-path.md](architecture/state-call-fast-path.md), "Elision revisited").
- The **GPU timer is kept on branch `worktree-agent-a2b067e4005541a7a`** (commit
  `423bd098`), not merged: bottle X3's D3D9 device refuses all timestamp query types.
- **lod-scale 0.5** "helps, few parts pop" — the default decision is pending; 0.7 was
  offered as the next A/B point.
- The fog **single look** is accepted by construction (the L2 law was already accepted).
- **Strict sky history stays opt-in** pending the Run 67 flight: band term, threshold
  3 px, watch close flybys.
- The run queue is archived with `tools/analysis/archive_user_runs.py`.

## Next steps

- **Triage Run 67 when reported** (triage agent per session): A against the run244 method
  in [temporal-resolve.md](verification/temporal-resolve.md) (dark-sky band, ring flicker,
  flyby border aliasing) — clean means strict + band become defaults; B with
  `draw_accounting.py` (which bucket is large decides the next per-node cull at the census
  site); C look + overlay numbers — a win makes the fog pass the default, and the footprint
  P=8 the default if no shadow is missed.
- **Pending user decision**: `--lod-scale 0.5` (or 0.7) and `--cull-small-parts 4` as launcher
  defaults (0.5 "helps, few parts pop"; at the stand 394 → ~275 draws, 21.7 → 17.4 ms).
- **Merged-LOD asset pilot**, narrowed to bodies that stay at LOD 0 while small on
  screen (run245–247 showed the heavy station body already has a usable ladder, so this
  is a threshold question for most nodes).
- **Tiny-subset cull idea**: 70 of the stand's 367 draws carry < 20 primitives (0.13 % of
  its primitives, ~1.9 ms of per-draw overhead) and already pass the 4 px size cull, so
  the lever would have to be engine-side.
- **Remaining cleanup batches** 3 (retired host tests), 5 (GTAO/SSAO chain) and 6 (dead
  TAA resolve variants, last because it edits the flown resolve)
  ([inventory](architecture/cleanup-inventory-2026-09-22.md)); batch 4 (`--lod-scale`
  removal) is superseded — lod-scale is now a live lever.
- **Rerun two host modules when the game is down**: `test_sun_share_lane` and
  `test_launcher_stderr_tee` both launch `manage.py`, which refused while the user's
  session was up ([host-suite.md](verification/host-suite.md)).
- Carried over: `run_motion_output.py`'s fresh-worktree build (missing
  `-DPython3_EXECUTABLE`), `generate_bloom_programs.py` `--d3dx` default still pointing
  at the Steam bottle, and the one-off Wine fixture stall at 0 % CPU that passed on retry.

## Worktrees

Twenty-nine `.claude/worktrees/agent-*` trees were created today; their work is
squash-merged into main and they can be removed with `git worktree remove`, **except
`.claude/worktrees/agent-a2b067e4005541a7a`**, which holds the unmerged GPU timer and
must be preserved. From the previous handoff: `/tmp/x3-lattice-capture-lifecycle` and
`/tmp/x3-lattice-payload-writer` still hold the uncommitted lattice diagnostic work that
is now deleted from main — archive their diffs to a git branch before removing them (not
done, [verdict](architecture/cleanup-inventory-2026-09-22.md)); `/tmp/x3-fog-finite-banks`
may go (its two files are on main); the `/tmp/x3-run5x/6x-candidate` directories hold the
retained DLLs.
