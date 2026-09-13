# Project status

Updated 2026-09-13. This is the current handoff; detailed prior checkpoints are
preserved in [status history](status-history-2026-09-13.md). Read history only
for a relevant unresolved question. The [goal checklist](goals.md) retains the
full scope; the [original objective](user-objective.md) is unchanged.

## Installed build and current camera work

Bottle **X3**, **CrossOver Preview.app**. Installed source checkpoint `1b39130`:
DLL SHA-256 `5a7e849f9873f3f491f0dc67710c1afc150d7fd40276c5b5903bb2e759a8610e`,
11,763,091 bytes. The [install record](../verification/results/chase-fire-install.json)
binds the reviewed candidate and rollback copy; EXE/configuration are unchanged.
[The current review](reverse-engineering/chase-mouse-fire.md) records scoped
x86 hook, CPU-state, runtime and performance evidence; the install record
includes the candidate load and actual CMake object checks. Native Windows remains untested.

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
- **Bloom live integration is reviewed and fixture-qualified.** The
  [compositor boundary](architecture/hdr-bloom-boundary.md) passes 714 combined
  X3 checks, with device lifetime, Reset/ResetEx and original exception cleanup.
  [Review 50](verification/review-50-hdr-bloom-live-integration.md) has no open
  findings. Clean candidate build/install is next; it is not gameplay-ready yet. The production
  bridge, owner helper, shader bundle and BloomPass evidence are linked from the
  [history](status-history-2026-09-13.md#bloom-preparation-checkpoint-2026-09-13).
  The exact AgX/writeback snapshot is implemented and reviewed. The synchronous
  scene handoff and owner/frame/Reset-qualified invocation are now connected in
  the working tree; focused scene-hook checks pass 43/43 in X3. The reviewed
  chase candidate above remains installed while integration is completed.
- Loading fell from 87 s to roughly 34–38 s in recorded X3 runs. [Run 17](verification/run17-crypto-loading.md)
  accepts crypto reuse; reader verification and adjacency gameplay acceptance
  remain. Fast modes require meaningful verification, not merely fixture success.
- Materials and lighting replacement precede real HDR/clustered lighting. AO,
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
