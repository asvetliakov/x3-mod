# Handoff 2026-09-21 — lattice upload integration and fog runtime design

This replaces the operational instructions in [the September 20 handoff](handoff-2026-09-20.md).
Read `AGENTS.md`, [status](status.md), [goals](goals.md), [user objective](user-objective.md),
this file and the relevant owning notes. Do not reread archives unless a specific
unresolved question requires them. Installed-build identity and rollback details
remain **only in status.md**. Source main at the work checkpoint is `36860cb4`;
the subsequent handoff commit contains documentation/inventory only.

## Stop state and immediate priorities

The user requested a clean session handoff. All task agents were asked to freeze
and stop. No Wine, game, build, numerical experiment or candidate qualification
is in progress. No game was launched by an agent. No new user flight is queued;
[user-runs.md](verification/user-runs.md) remains the run authority. Agent handles
from this conversation are historical: start fresh bounded agents in the next
session rather than assuming they survive.

1. **Resolve the real ownership/shader-shadow lifetime bug** exposed by the new
   actual Capture fixture. Keep its failing routed case unchanged. This blocks
   candidate integration; it is not a newly reported game crash.
2. Finish the **unmerged F8 geometry writer**: two standalone cross-builds still
   need confirmation, then integrate after its existing clean deep review.
   Qualify actual late-COM-release → paired-copy integration separately.
3. Resume **stored-density fog screen**: source is written and frozen, but
   independent pre-run source review has NOT happened. No numerical result exists.
4. Only after integration is ready: consolidated diagnostic wiring, appropriate
   fixtures, **full host suite**, clean reviewed candidate, protected install and
   dry-run; then ask the user for a single useful flight. Do not ask for another
   lattice capture using the old state-only observer.

## Binding operating rules and preferences

- Never launch X3, even menu-only. `--dry-run` is allowed. User flies and reports
  `/tmp/x3-bottleX3-runNN` plus F8/video observations.
- Every Wine execution is serialized through
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py <command>`.
  Use CrossOver Preview, X3, arm64 Wine/FEX, `FEX_X87REDUCEDPRECISION=1`,
  `WINEMSYNC=1`; one queue/candidate owner, no concurrent Wine.
- Root owns integration/build/install. Source agents use isolated checkouts;
  one writer per file set; up to eight independent children, no child spawning.
  Deep independent review for hook/ABI/lifetime/draw changes; reviewed checkpoint
  commits. Never merge to main while a candidate qualifies.
- Install only with `python3 tools/manage.py install --bottle X3 --dll-source <dll>`.
  Retain rollback DLL/manifest; bind reviewed source/toolchain/DLL/results, verify
  installed bytes and unchanged EXE `fdbf3418…` / bottle config `cc5d6c00…`.
  Full host discovery before the next candidate; no routine full reruns per edit.
- Native Windows enhancement remains required. Documented Windows/D3D APIs;
  CrossOver execution and cross-compilation are not native Windows verification.
- User prefers original hull shading and low cost, needs TAA, dislikes repeated
  permission questions and work that stops after a status message. Continue
  autonomously until a useful build or a genuine evidence dependency. Prefer
  real dumps, screen recordings and counter flights over new static models.
- User visuals: 1280×768 external OLED; 1 metre = 5 render units. F8 capture;
  fog Ctrl+Alt+F9/F10, FPS Ctrl+Alt+F7, lightmaps Ctrl+Shift+F4.

## Current source and accepted decisions

Accepted production baseline is on main; no new DLL was installed during this
work. Media ID2 omission is accepted (Run56/run200: no crash/media stutter);
owned video playback is retired. Do not revive old media branches. First-person
fog correction is accepted. Run57/run205 accepts disabling both global TAA
missing-history/displacement heuristics; camera cuts/chase snaps/recovery remain.
Light-map fade default is `80,220,1`, lazy motion RT binding is accepted, original
hulls/cull 2 px scope all/mip bias −0.5 remain. Far stabiliser .985 and thin region
.97 remain explicit; stationary lattice improvement is accepted, moving crawl open.

Useful recent main checkpoints (already integrated; do not reapply dirty copies):

| Commit | Scope |
| --- | --- |
| `c404e979`, `73080396` | Accepted baseline merge; Run57 default correction |
| `97e8074c`, `c147cd44`, `79a6b732` | Upload staging core, readable backing prerequisite, manual ABI |
| `157aa489` | Actual manual Clone upload observer, 421 checks |
| `89e035fc` | Saved-target callsite adapter, 389 + 158 ABI checks; not wired into startup |
| `e85ecc41` | B1 pin lifecycle/atomic pair, 386 checks + 421 regression |
| `75f04dde` | B2 Python geometry reader/collector, 47 host tests |
| `0fd59455` | Reviewed modest fog detail refinement |
| `36860cb4` | Valid negative global64 fog transport experiment |

## Unmerged work: preserve these exact working trees

All pending changes below are **uncommitted working-tree edits**, not commits
waiting to cherry-pick. Branch HEAD alone does not contain the implementation.
[Pending file hashes and backup identities](../verification/results/session-handoff-2026-09-21/pending.json)
identify the exact files. Local source-only backup tarballs are under
`/tmp/x3-session-handoff-2026-09-21/`. Do not reset, clean, remove or blindly merge
these trees. Compare/copy reviewed deltas into main at a checkpoint.

| Worktree / branch | Base | Pending scope / owner history |
| --- | --- | --- |
| `/tmp/x3-lattice-capture-lifecycle` — `investigate/lattice-capture-lifecycle` | `e85ecc41` | B2a `capture.cpp/h` + three `lattice_capture_lifecycle_*` fixture files. Author `lattice_subset_qualifier`, reviewer `review_lattice_point_probe`. Runtime **failed**, not accepted. |
| `/tmp/x3-lattice-payload-writer` — `investigate/lattice-payload-writer` | `75f04dde` | B2b Capture state writer, two geometry headers, host fixture/test, two standalone builders + gated stubs (nine files). Author `lattice_upload_abi`, reviewer `review_lattice_writer`. Source/host review clean; cross-build closure and actual integrated callback evidence pending. |
| `/tmp/x3-fog-finite-banks` — `investigate/fog-finite-banks` | `e2270076` | Only NEW `fog_density_runtime_screen.py` and `test_fog_density_runtime_screen.py` are pending. Many other untracked preview scripts here are already copied to main. Author `run57_fog_range_replay`; next review needed. |

The [full worktree inventory](../verification/results/session-handoff-2026-09-21/worktrees.json)
records every branch/HEAD/dirty entry/ancestry at handoff. Most old worktrees are
historical evidence, not live pending tasks. In particular, flight-hook,
flight-lifecycle, payload-reader, upload-ABI and upload-prototype dirty copies
are already integrated and retained for evidence; do not reapply them.
Collision experimental branches are paused, media playback branches retired.
No worktree cleanup was performed here. The old locked Claude checkout must not
be unlocked/removed on the strength of its old PID alone; recheck ownership.

## Lattice: exact next technical state

Owning [lattice architecture §31 onward](architecture/taa-lattice-crawl.md) and
[mesh rewrite reverse engineering](reverse-engineering/mesh-buffer-rewrite.md)
carry the evidence. Arithmetic exploration is closed: calibrated GPU point
observations still did not qualify the CPU interpolation model. Need original
uploaded geometry, not further rounding/jitter speculation.

Two selected meshes: 9680 vertices/3784 faces and 1267 vertices/940 faces,
stride 40, INDEX16. Total payload 466224 bytes. Preallocated 2 MiB Store;
duplicate selected shape poisons its slot until reset/disarm. Capture original
readable MANAGED upload mappings before original Unlock, with no added native
Lock/Unlock/draw. Raw staging is private until successful Clone, original
Unlock closures and final identities/revisions validate. Failure wipes storage.

Durable contracts (retain their scope distinctions; some original proposal
headings predate ratification):

- [Flight integration stages](architecture/lattice-upload-flight-integration.md):
  A accepted; B1 accepted; B2a/B2b incomplete; startup/CLI/build graph still unwired.
- [Lifecycle contract](architecture/lattice-upload-lifecycle-contract.md): B1
  accepted; actual Capture accounting still blocked by the routed fixture.
- [Packet contract](architecture/lattice-upload-packet-contract.md): root ratified
  this schema; reader accepted, writer pending integration. New identity metadata
  is pointer-free; legacy state fields retain their existing bounded semantics.

B1 has a unique arm token, CPU-only pin query, Deferred/Released close and one
registry-guarded pair copy. One wrapper device pin, process-lifetime Store;
no COM callback while copying or detaching under registry. Query/final pair copy
is after all guarded getter Releases. Never claim simultaneous immutable draw
input or texture coherence: `draw_input_coherence=unqualified` is mandatory.
Native bypass and inherited arbitrary callback SEH are not magically repaired.

### B2a failure and design decision still required

Frozen witness: `/tmp/x3-lattice-capture-run-v1/{build,run,lock}.json`,
`stdout.log`, `stderr.log`; compact `/tmp/x3-lattice-capture-failure-v1.json`.
151 assertions pass before crash; exit 5, 23.982 s. Direct/child/recreated
retirement, explicit/deferred close, Reset and unrouted guarded draw pass.
Routed draw with actual renderer refs faults restoring PS:
`Device::SetPixelShader → MotionOutput::undo → after_draw → Capture`.
Proxy DLL SHA `f5aa5e61f2dc264358f97e7f7b898ebe9a7fd5bfd279991b494eff37147a329f`.
No Wine/debugger process remains from this run.

[Confirmed diagnosis / pending proposal](architecture/ownership-shadow-lifetime-diagnosis.md):
MotionOutput stores weak application VS/PS wrapper pointers. Application releases
its valid creation references after binding; native backing remains bound but
ownership destroys the wrapper. Undo later passes the stale wrapper through the
legitimate foreign-pointer unwrap fallback into native SetPixelShader.
**Do not weaken the fixture by retaining its creation refs.**

Permanent strong shader pins are NOT solved by adding two to device_references:
a shared child wrapper owns only one parent pin, and later app ref drops need a
transition notification to avoid cycles/premature cleanup. Final designer proposal,
NOT yet ratified/implemented: acquire scoped public shader getter restoration
references before injection, hold through restore, release under the existing
draw/device pin. First inspect existing snapshot reuse and measure per-routed-draw
cost; no GetShader overhead on ordinary draws. The diagnosis lists exact null,
failure, state-block, Reset and retained-child/GetDevice acceptance cases.

B2a source review requested a no-EH arm shell and removal of duplicate pin query;
author fixed both before run v1. The final emitted/runtime review remains open
because of the crash. Do not treat the older reviewer message as an unfixed source
finding. Coordinator allocates Store only when explicitly armed; no startup caller.

### B2b writer and remaining checks

`Capture::arm(device,frame,generation,scope_active,bool upload_requested=false)`
keeps old callers state-only. Optional packet allocates at F8, not per draw;
retain final VB/IB references, collect IDs, release them inside QueryScope, then
use B1 atomic pair copy with no later observer COM call. Reset/late callback or
failed submission cannot revive the packet. Both pairs required; no partial binary.
Present uses SHA-256, exclusive temps, binary-first/JSON-last/no-replace rename,
owned-file cleanup and explicit failure JSON. Reader validates exact ranges,
identity/hash/size/INDEX16 and uses separate `--require-payload`.

Clean deep source/helper review: 7 tests / 44 shared-production-helper scenarios,
19 combined host tests; final source cross-compile passed at
`/tmp/x3-lattice-payload-compile-v2/`. Nine hash/I/O failures and four collisions
are covered. CryptoAPI/native file execution and actual late-COM callback→pair
path are **not** established by this host fixture.

New linkage closure (also reviewed clean; 23 host tests pass): both standalone
state-only builders link three **fail-if-called** upload stubs, gated by
`X3M_LATTICE_STATE_ONLY_FIXTURE`, plus advapi32. These must never be linked into
ownership-on/production fixtures. Parent still needs these cross-builds:

```sh
python3 /tmp/x3-lattice-payload-writer/verification/probe/lattice_state_fixture_build.py --output <fresh-state-dir>
python3 /tmp/x3-lattice-payload-writer/verification/probe/lattice_observer_release_build.py --output <fresh-release-dir> --inputs /tmp/x3-lattice-gpu-inputs-v5
```

Confirm the input directory before use. Neither command launches the game.
After accepted integration, wire opt-in startup/CLI/CMake and split ABI objects;
main CMake does not yet list Clone ABI/A adapter sources. Do not infer a normal
candidate build is ready from isolated object compilation.

### Reusable local build/evidence locations

- B1 `/tmp/x3-lattice-lifecycle-{build,run}-v2`; manual regression
  `/tmp/x3-lattice-lifecycle-regression-{build,run}-v1`; final no-fixture object
  `/tmp/x3-lattice-lifecycle-production-v3/`. Committed record:
  `verification/results/run201-lattice/upload-lifecycle.json`.
- A `/tmp/x3-lattice-flight-{build,run}-v1`, ABI regression
  `/tmp/x3-lattice-flight-abi-{build,run}-v1`; committed upload-hook record.
- B2a clean support `/tmp/x3-lattice-capture-support-build` from `e85ecc41`,
  locally supplied qualified B1 `clone-shell.o`/`clone-eh.o` through linker flags.
  This is a **fixture support build, not an install candidate**.
- Root local scripts `/tmp/x3-build-lattice-capture-seam.sh` and
  `/tmp/x3-lattice-capture-run.py`; run script hardcodes v1 output, so use a fresh
  path and retain old artifacts. New fixture EXE/DLL live under B2a
  `verification/probe/build/`; preserve frozen run1 copies.
- No current fixture/build process is running. Reuse unaffected evidence;
  rerun the exact failing case after the scoped lifetime fix, then affected cases.

## Fog: user preference, closed approaches, frozen next experiment

User wants broad connected irregular clouds throughout a sector or substantial
parts, diverse sizes/densities, internal variation and clear gaps. Rejects a
uniform wash, repeating carpet and single distant bulb. Likes refined mass/detail
preview; latest request: **“close! little more patches and varying density.”**
The modest refinement keeps broad mass and increases internal modulation/edge
opening. Cloud-only previews are not final game lighting. Strength preference
is 1.50× (current .03); fog remains opt-in/off by default.

Owning [architecture](architecture/volumetric-fog.md) and
[verification ledger](verification/volumetric-fog.md). Latest liked direction:
`/tmp/x3-fog-mass-detail-refinement/green-poseB-refined-detail-cloud-only.png`
(column1 previous, column3 refined full-distance; bottom white = clearer).
Two-view numerical reference reviewed; no production integrator selected.

Closed with evidence: old-field far filtering, coarse prefix reconstruction,
finite/paired-lobe bulb, blanket-like sector macro modulation, and fixed global
64-sample exact-field/corner-cache route. Last has nine failed gates despite
converged references, T p99/max .002622/.003182 and temporal .004867; ~359 mean
texture reads versus current48. Do not rerun it or silently sweep step counts.
Current installed range is still in status/owning notes, not changed by previews.

New [stored-density plan](architecture/fog-density-runtime-plan.md) is ratified
for ONE host experiment only. Two anchored128³ scalar grids (102.4m/819.2m),
fixed eight-point final-density prefilter, 4–6km LOD blend, unchanged 30–40km
range taper and field/strength. Fixed24 near +40 far samples. Intended8MiB
resident payload/~132 sky reads, **not measured GPU performance**. Lazy touched
nodes only, no complete cache/manager/shader. Separate representation/filter
appearance difference from quadrature error. Explicit rotations avoid the known
NumPy/Accelerate matmul warning path; do not suppress warnings blindly.

Frozen pending source hashes:
- `fog_density_runtime_screen.py`: `488954aafdd6460708de9cb3037024692a77eab9a920a3a265f110a9665414bc`
- test: `2079753bf908deff2705714a47c25d74125cca59aa51f28beccdb4013400e32a`
- plan: `ca7674296bb28ad26c7c043a7ca812dc4f0f68c592e1fd54f383fa2ccf8aea38`

Author reports 8 focused tests pass and one single-ray smoke; neither is the
experiment. Reviewer read plan but **did not source-review or clear execution**.
Next: bounded pre-run review (gates, exact field, signed absolute node keys,
FP16/lane/bounds/shift laws, component rotations, clipping/composition, costs),
then one frozen run into `/tmp/x3-fog-density-runtime-screen` (does not exist at
handoff), inspect fixed images/metrics and obtain combined review. No automatic
parameter tuning. GPU/repair/state/Reset/loading and all-family spatial chroma
remain later evidence gaps. Original engine fog census/sector chain and fourteen
family technical work are complete; do not restart stage1/count-only scaling.

## Other work and old handoff reconciliation

- Collision is **paused by user**; no moving-collision capture queued. Preserve
  experiments, do not restart stale agent tasks or weaken collision tests.
- Engine/proxy timing: corrected attribution does not justify another patch or
  busy-view flight. Lazy binding is accepted; the old “never flown” entry is stale.
- Media playback branches/agents are retired historical evidence. No decoder,
  EOF retry, worker or package work remains authorized as an active feature.
- Housekeeping: original eight-worktree/11GB cleanup and later25 stale-registration
  pruning are done. New temporary trees are separate; inventory now records them.
  Do not repeat destructive cleanup from the old count. Historical result pins
  retain their provenance, not a newly qualified full suite.
- Host closure: `lattice_observer_probe` previously fixed four host-test files;
  54 focused tests passed (one existing skip), accepted in current source.
  **New full discovery still required before candidate.** Last installed-candidate
  full-suite result/follow-up limitation is recorded accurately in status.md.
- Keep lower-priority evidence gaps in their owning notes: native Windows;
  sun-lane stamp/occlusion-query assumptions; renderer Reset/failure variants;
  loading-time coverage; chase/docking acceptance. They are not new flight requests.
  Double cursor was reproduced in vanilla; verify it if window/presentation changes.
- Do not reopen: original hull choice, no engine state filter/instancing/draw
  sorting/pass replay, no MSAA/SSAA/global TAA blur/jitter reshuffle/luminance clamp,
  no collision capping/striding, SSR/flare/DOF/depth-aware bloom closed,
  soft particles unwanted, clustered lighting deferred.
