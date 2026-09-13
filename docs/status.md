# Project status

Updated 2026-09-13. This is the current handoff; detailed prior checkpoints are
preserved in [status history](status-history-2026-09-13.md). Read history only
for a relevant unresolved question. The [goal checklist](goals.md) retains the
full scope; the [original objective](user-objective.md) is unchanged.

## Installed build and current camera work

Bottle **X3**, **CrossOver Preview.app**. Installed source checkpoint `24930b5`:
DLL SHA-256 `24edac6c2c913e7994f2669f8d295ca0d56f79dd0bb9ef1b23f36fabf4efd724`,
12,347,043 bytes. The [install record](../verification/results/linear-material-install.json)
binds the reviewed clean candidate, load check and previous Argon-material rollback
pair; EXE/configuration are unchanged. [Review 50](verification/review-50-hdr-bloom-live-integration.md)
records the bloom integration's scoped verification. The material candidate retains the previous import inventory and passes the
light-hook x87 audit. The [shared-material review](architecture/material-next-slice.md#5-shared-default-runtime-checkpoint-review)
approves the expanded implementation and its structural/GPU/live evidence. Native Windows
remains untested. Bloom (`--hdr-bloom`) and materials (`--linear-materials`) are opt-in; existing
commands leave each off unless requested.

The installed chase defaults are **13° pitch, distance 0.85, rotation/position
response 0.22/0.30 s, offset 0.45, lag limits 8°/0.10**. Vanilla is the default
camera mode. Chase plus telemetry enables four read-only cursor/fire observation
sites. The separate chase-fire branch hook works without telemetry and
changes cursor-fire admission only for the active chase view. The compiler CPU-state boundary fix
is included. Gameplay frame cost is not established by diagnostic timings.

The user completed the first-person left/centre/right firing sweep and repeated
it in third person. The [third-run analysis](verification/chase-third-run.md)
isolates the fault: first person admitted 229 cursor rays; external view rejected
all 180 attempts because the native cursor writer set cursor-active to zero.
A chase-only firing correction is implemented and independently reviewed from the
[verified admission branch](reverse-engineering/chase-mouse-fire.md#third-run-finding-and-proposed-scoped-correction).
All four X3 fixture modes passed 307 checks. The candidate is built and
installed; gameplay acceptance remains pending.
Earlier anchor correction removed the reported
trembling in the second flight. The requested **13° pitch and distance
0.85** pass 56 focused checks and are included in this install.

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
  accepts crypto reuse; reader verification and adjacency gameplay acceptance
  remain. Fast modes require meaningful verification, not merely fixture success.
- [Linear DEFAULT materials](architecture/scene-linear-materials.md) are
  implemented, independently reviewed and installed: fifteen original shaders /
  twenty Argon and shared Khaak/Teladi/Xenon pairs. Qualification passes 313 GPU
  cases and 1,228 live-route checks across ownership/TAA, Reset, state blocks
  and reference retirement. The feature stays opt-in; gameplay acceptance is
  run 6 in the brief queue. The [Argon BUMPMAP design](architecture/linear-bump-materials.md)
  now has reviewed production transformation and live-routing source for the
  next ten pairs, five sampler roles and a separate varying ABI. Its detached
  GPU qualification passes 512 cases; all previous DEFAULT variants remain
  byte-exact. The clean candidate build and 24-frame live state/Reset twins are
  next; these BUMPMAP shaders are not in the installed build yet.
  Blended emissions need a separate linear-composition and temporal-reactivity
  design, as the [next-slice study](architecture/material-next-slice.md) explains.
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
