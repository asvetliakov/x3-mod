# Account-switch pause

Historical pause snapshot, 2026-09-13. **The user subsequently asked to resume;
work is active again.** At the pause, all eight running secondary agents were interrupted; the final
agent inventory contains only completed/interrupted secondary agents. A process
inventory found no project Wine runner, fixture, compiler, CMake/Ninja build or
Ghidra headless process. No installation is in progress.

Read this file, `AGENTS.md`, `docs/status.md`, and the relevant owning design
when resuming. The full objective remains `docs/user-objective.md`; the current
goal checklist is `docs/goals.md`. Never launch the game; X3 bottle only, every
Wine fixture through `verification/probe/wine_lock.py`. Use Astra for
implementation and Sol/high for review/analysis. Commit reviewed checkpoints.

## Preserved state

- Latest implementation/research checkpoint before this pause: `2bde645`,
  camera lead projection and reset-origin disassembly findings. Previous
  `d3d142e` accounts for all necessary archive shader families; `c7eaf91`
  accepts run18 loading verification and softer chase defaults.
- Installed DLL remains source `c663b03`, SHA-256
  `b11bff61b2a2f798b565969d8684923781d2b1bec0e3b22cf0cbe2f024d45702`.
  Canonical record: `verification/results/linear-material-install.json`.
  No new camera tuning, live emissions or expanded materials are installed.
- Uncommitted source, tests and documentation remain in the worktree. They
  are intentional in-flight work, not disposable changes. Do not blindly
  commit all files, restore them, or rebuild/install the mixed tree.
- `docs/verification/user-runs.md` is the authoritative brief run queue;
  executable `./x3run` handles launch and snapshot. Run18 is complete; no
  repeat requested. Fast reader/adjacency co-activation is now admitted.

## Emission live integration: first resume priority

Owners: `linear_material_gpu_fixture` (fixture), `linear_material_live_route`
(production, completed), `bloom_integration_review` (same independent reviewer).
Production source/host review passed after fixes; actual integrated GPU
qualification has **not passed**. Five focused host modules previously passed
3,872 assertions. The detached component evidence is already committed and
need not be rerun without a relevant change.

Pending shared-tree files include CMake, capture, MotionOutput, manage.py,
build_motion_output.sh, live fixture and tests; the new emission fixture is
`verification/probe/motion_output_emission_inc.h`, runner
`run_linear_emission_live.py`, parser test `test_linear_emission_live.py`.

Root built an isolated reviewed snapshot before material expansion:

`/var/folders/l6/0sdq5b49401b_4m_26gsl1f00000gn/T/x3-emission-live-build-flyzvee_/source`

Base `177013f1d0e9e3add1a0701ae57536e88a407ee6`, parent-directory
`reviewed.patch` SHA-256
`13d2be0e87cd23af2ae345ad18c2f54580600df88e9637c018af5a98e6b5d80f`.
The untracked fixture fragment was copied separately. Production DLL
`build/d3d9.dll` SHA-256
`fce9bfe882ec1aa497eedc1c2ad703bbb128e6e8a43a979339118856a5907a2e`,
12,688,047 bytes; no-x87 audit passed 215 reachable functions / 63 roots.
Seam DLL `verification/probe/build/motion-output-seam/d3d9.dll` SHA-256
`f45a07f9e3ebfface1d37e5b3602c3218df95f49da23b97d704ee88b52f8465e`.
These binaries do not contain the new 40 material pairs.

Two runs stopped in the first feature-off configuration:

1. Raw directory `.../T/x3-linear-emission-live-bvpbmmvf`: ordinary draw B
   retained the emitter's vertex declaration. Fixture correction explicitly
   restores/validates its ordinary declaration; numeric gates unchanged.
2. Raw directory `.../T/x3-linear-emission-live-t7nnq1kw`: R2 EXE
   `verification/probe/build/motion_output_fixture-r2.exe` used the same DLL.
   The second source's strict alpha oracle expected 1.01513671875, which is
   between adjacent FP16 values. Actual failing alpha was not saved. This
   proves an impossible fixture expectation, not duplicate source submission.

Here `...` means `/var/folders/l6/0sdq5b49401b_4m_26gsl1f00000gn`.
Both runner sessions exited; their Wine leases are closed. Preserve these
failures; never read their large stdout/log files whole into agent context.

Consolidated fixes agreed but **not completed/reviewed at pause**:

- Use exactly representable source-2 alpha 0.25, retaining strict equality.
- Add failure-only actual pixel/value diagnostics and bounded failed
  readbacks; aggregate successful pixel checks instead of huge PASS output.
- `X3M_CAPTURE_FRAMES=0` disables capture, so the planned 12 exact TAA file
  comparisons cannot currently complete. Add a test-seam-only forced TAA
  readback at successful resolve, independent of capture admission and Reset
  capture windows. Functional emissions only; benchmark off. Keep all 12
  exact comparisons and uncaptured lazy-route coverage. Coordinate with
  production owner; reviewer approved this design, not a finished patch.
- Ordinary fixture textures have one mip / MIPFILTER_NONE, so configured mip
  bias is not exercised. Use a constant two-level ordinary texture and POINT
  mip filtering, identical texels in both levels, restoring source state;
  assert actual bias/restoration counters in both lazy and per-draw modes.
- Audit the remaining 12-frame state assumptions together before rerunning.

After consolidated review, rebuild only the affected isolated seam/EXE inputs
(production DLL need not change for test-only work), then run the explicit
fixture and DLL with `X3M_FIXTURE_BOTTLE=X3` under the Wine lock. Four functional
configurations and two descriptive completion comparisons are planned. Do not
weaken failed-source, ordering, state/Reset or temporal gates. One reviewer
closes source and evidence; no redundant broad suite or nested manifests.

## Complete material coverage and next 40 pairs

Owner `material_conversion_sites`; proof helper completed; independent reviewer
`chase_fire_review` was interrupted before returning a verdict. Numerical
reference/GPU fixture owner `scene_linear_material_plan` was also interrupted.

`docs/architecture/material-coverage.md` inventories 751 programs / 817 pass
identities across all families and quality/toggle variants. User explicitly
wants all necessary shaders, including uncaptured ones; 30 pairs installed
are not the completion target.

New pure transformer/proof/reference group is frozen at pause:
Split DEFAULT 10 plus standard_lighting DEFAULT/BUMPMAP/BUMPMAP_LOW 30,
adding 24 PS and one VS, for **49 originals / 70 pairs** total. Owning note:
`docs/architecture/linear-standard-materials.md` (uncommitted).
Focused checks: profiles 30, transformer 8, reference 37. All 192 previous
transformed outputs remain byte-exact. Maximum VS slots 85/87, PS 178/180
(depth off/on). Strict x86 core compilation passes. These are host/static
results, not GPU/live qualification.

Root approved the explicit depth TEXCOORD7 exception for standard base VS
`494fe349b8bc12ec` and its two PS. Preserve the existing temporal bytes,
validate its row, and prove no collision with new RGB TEXCOORD6. Standard
application c8/c9/c10/c11 scalar coefficients remain unchanged; LOW uses
signed XYZ normals, unlike the retained alpha/green reconstruction.

`scene_linear_material_plan` was assigned the existing pure GPU fixture
`linear_material_fixture.cpp` and `run_linear_material.py` plus focused tests
to cover the whole new group together, including variable coefficients and
LOW normals. It must consume explicit prebuilt EXE and not run Wine/build a
production DLL. Inspect actual files before assuming implementation began.
The live-fixture expansion must coordinate with existing emission changes.

Next coherent candidate, not started: shared hull BUMP 10, Split BUMP 10,
Terran DEFAULT/BUMP 20 (24 new PS, no new VS). Do not mix new production edits
into the current group's review before its checkpoint.

## Camera follow-ups

Owner `bloom_lifetime_design`, child `chase_transition_impl`, child RE agent
`chase_reticle_re`; all interrupted. Static findings committed in `2bde645`:
`chase-lead-reticle.md`, `chase-view-transition.md`, `elevated-chase-camera.md`.
No new camera production files existed in the final pause git inventory;
verify this on resume rather than assuming completed implementation.

Root approved implementing the predictive marker with three narrow sites:
gate `0x42a6fe`, native publication `0x42aaae`, final FOV `0x4213dd`.
Keep the native solver/icon/depth gates; use actual chase origin/basis and
write visible/stored coordinates consistently. Finalize before synchronous
laser callbacks. Main guns, currently applied mode 258, same-sector/type-1
owner only. Same-update/thread/lifetime/target/marker guards and native hide
semantics on owned failure are required. No new solver, replay or global hook.

Transition persistence remains diagnostic-only: script writer `0x42e742`
and save-deserialization writer `0x419e06` can both change mode. Observe both
with exact VM method/PC provenance; do not force mode 258 each pose or assume
a save load is a transition. Consolidate bounded change-only telemetry with
the reticle work before another user run.

Proposed shared module tracks constructor entry/completion, destructor,
updater entry/terminal, both mode writers and actual connect (eight sites).
Mandatory lifetime/update observation should be distinguished from optional
diagnostic sites; failure must preserve existing chase/aim behavior. Keep
always-on work bounded and cheap. Root owns loader/CMake/capture/no-x87 wiring
after APIs settle, avoiding concurrent emission edits. Lead owner may edit
chase_camera.{h,cpp} and new scoped modules. Review, host failure/geometry
tests and EXE site verification remain before installation.

User accepted aiming and 13-degree pitch, requested distance 0.90 and softer
response (reviewed defaults 0.28/0.38 s), and reported no trembling. Those
default changes are committed but not installed. Missing predictive reticle
and native view reset are separate remaining issues.
