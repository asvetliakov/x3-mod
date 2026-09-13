# Project status

Updated 2026-09-13. This is the current handoff; detailed prior checkpoints are
preserved in [status history](status-history-2026-09-13.md). Read history only
for a relevant unresolved question. The [goal checklist](goals.md) retains the
full scope; the [original objective](user-objective.md) is unchanged.

## Installed build and current camera work

Bottle **X3**, **CrossOver Preview.app**. Installed source checkpoint `c663b03`:
DLL SHA-256 `b11bff61b2a2f798b565969d8684923781d2b1bec0e3b22cf0cbe2f024d45702`,
12,368,396 bytes. The [install record](../verification/results/linear-material-install.json)
binds the reviewed clean candidate, load check and previous build rollback
pair; EXE/configuration are unchanged. [Review 50](verification/review-50-hdr-bloom-live-integration.md)
records the bloom integration's scoped verification. The material candidate retains the previous import inventory and passes the
light-hook x87 audit. The [BUMPMAP runtime review](architecture/linear-bump-materials.md#runtime-review-verdict)
approves the expanded implementation and its structural/GPU/live evidence. The
[temporal qualification](architecture/linear-emission-composition.md#supplemental-consumer-qualification)
closes the shared shader's static-budget issue: 507 slots, exact paired images
and Reset coverage. The embedded bytes match the qualified program; the fresh
build and explicit-DLL load check pass. Native Windows
remains untested. Bloom (`--hdr-bloom`) and materials (`--linear-materials`) are opt-in; existing
commands leave each off unless requested.

The installed chase defaults are **13° pitch, distance 0.85, rotation/position
response 0.22/0.30 s, offset 0.45, lag limits 8°/0.10**. Vanilla is the default
camera mode. Chase plus telemetry enables four read-only cursor/fire observation
sites. The separate chase-fire branch hook works without telemetry and
changes cursor-fire admission only for the active chase view. The compiler CPU-state boundary fix
is included. Gameplay frame cost is not established by diagnostic timings.

Run 18 confirms the chase aiming correction works, the 13° angle is good, and
no trembling or other visible camera problem was noticed. The user requested
0.9 distance and a smoother response; revised defaults 0.28/0.38 s pass review
and 56 focused checks, but are not yet installed. Two follow-ups remain: the predictive aim
indicator is absent in chase view, and sector travel switches away from chase.
Docking behavior is not yet tested. Targeted game-code analysis is in progress.
The [earlier diagnostic](verification/chase-third-run.md) and
[admission-branch study](reverse-engineering/chase-mouse-fire.md)
retain the firing correction's evidence. [Run 18](verification/run18-camera-loading.md)
accepts reader/adjacency verification and admits the fast-mode co-activation
check. No adjacency mismatch dumps were written; verification/capture overhead
is a plausible contributor to pauses, not a proven cause of individual stutters.

Use the [brief user run queue](verification/user-runs.md) for remaining acceptance,
including other views, aiming, TAA cuts, menus and resolution changes. The agent
never launches the game. New Wine fixtures use X3 only and the shared lock.

## Rendering and loading

- **TAA is verified in game**, with motion produced by transformed material
  shaders during the game's own draws. Geometry replay/admission modules are
  reference code, not required by the live renderer. Sharpen/mip bias were tested;
  candidate sharpen 0.75 still needs its own measured capture.
- **AgX is implemented and retained.** FP16 scene redirection exists, but its
  content remains decoded gamma-space game lighting. Scene-referred HDR lighting
  and HDR display output are incomplete. The replacement space-aware exposure
  meter is installed and awaits gameplay acceptance.
- **Bloom live integration is installed and awaits gameplay acceptance.** The
  [compositor boundary](architecture/hdr-bloom-boundary.md) passes 714 combined
  X3 checks across both reference models, including Reset/ResetEx and original
  exception cleanup. Review 50 has no open findings. The scene handoff retains
  exact display parameters and adds no resolve or exposure-meter pass. Existing
  component image/state/recovery evidence remains linked from the design.
  Run 3 in the [brief queue](verification/user-runs.md) combines exposure and
  bloom A/B acceptance; game image quality and frame cost remain unverified.
- Loading fell from 87 s to roughly 34–38 s in recorded X3 runs. [Run 17](verification/run17-crypto-loading.md)
  accepts crypto reuse; [run 18](verification/run18-camera-loading.md) verifies
  6,958 reader outputs and 15,354 meshes exactly with zero mismatches or faults.
  Fast-mode co-activation is now ready as run 5; its gameplay acceptance remains.
- [Linear materials](architecture/scene-linear-materials.md) are implemented,
  independently reviewed and installed: **24 original shaders / 30 pairs**,
  including DEFAULT Argon/shared hulls and [Argon BUMPMAP](architecture/linear-bump-materials.md).
  Qualification passes 512 GPU cases and 2,416 live-route checks across 192
  frames, ownership/TAA, five-sampler state, Reset and reference retirement.
  All previous DEFAULT variants remain byte-exact; BUMPMAP uses a separate
  varying ABI and cached sampler admission. The feature stays opt-in; gameplay
  acceptance remains run 6 in the brief queue, with no additional run required.
  [Blended emission work](architecture/linear-emission-composition.md) now has
  independently qualified original-shader augmentation, same-draw native color
  and supplemental coverage, and a temporal consumer. The latest producer run
  passes 81 X3 cases; 30 paired temporal frames are pixel-exact after reducing
  the shared shader from 1,261 to 507 instruction slots. The smaller program
  meets the standard static limit but has a modest measured completion-time
  cost. Review is closed and the regenerated embedded bytes match the qualified
  program, now installed. The existing TAA gameplay run also covers this update.
  The target ownership exchange is reviewed and passes 1,343 host checks;
  default-off shader-cache preparation passes 3,540 host assertions.
  The single-draw pass now passes independent review and 60 X3 cases, including
  state recovery and same-instance Reset. Its measured added completion cost is
  about 0.49 ms at 1280×768 and 0.76 ms at 1080p per source draw, excluding the
  frame mask clear and live target exchange. Live integration is in progress
  behind a separate default-off flag; no live emission route is enabled.
  Copy/composition cost remains a
  concern, and historical adjacent draws do not yet qualify batching. The
  [background study](reverse-engineering/background-temporal-coverage.md) retains
  the existing native temporal limitations; global writer classification is not
  a prerequisite for supplemental coverage of enhanced emissions.
  The [sun resource study](reverse-engineering/sun-material-identity.md) identifies
  the late TSuns lens-flare path; a pre-bloom sun material remains unproved.
  Targeted disassembly established native color/emissive scaling. Covered
  materials evaluate in linear light and compatibility-encode into the existing
  FP16 target; this is not whole-scene linear blending or HDR display support.
  Materials and lighting replacement precede real HDR/clustered lighting. AO,
  shadows, reflections, particles, volumetrics and lens effects remain on the
  [roadmap](architecture/roadmap.md). The macOS menu bar and double cursor remain
  open. Native Windows/D3D feature support is required; see
  [portability](architecture/platform-portability.md).

## Workflow

The [cross-workstream audit](verification/workflow-audit-2026-09-13.md) is complete.
`AGENTS.md` now specifies proportional checks, one candidate owner/record,
focused agent briefs and short current status. The Wine lock provides a shared
preflight and optional timings; the export runner consumes an explicit DLL
without rebuilding production. Both review findings were fixed and affected
host checks pass; see the audit's adoption record. Review, relevant failure/performance checks, Wine serialization
and reversible installation remain; no historical results or commits were deleted.

The executable [x3run](../x3run) launches with the shared Wine lock and preserves
each new session and only its referenced captures after exit, before the next
comparison launch. The [run queue](verification/user-runs.md) uses `./x3run`
directly; no shell function setup is needed.
The [streaming snapshot helper](../tools/analysis/snapshot_x3_run.py) uses a fresh
numbered `/tmp` directory, reports stale/missing files and preserves the game
exit status. Independent review found no blocker; 13 focused host tests pass.
No additional manifest, gameplay step or production DLL change is involved.
