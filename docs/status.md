# Project status

Updated 2026-09-13. This is the current handoff; detailed prior checkpoints are
preserved in [status history](status-history-2026-09-13.md). Read history only
for a relevant unresolved question. The [goal checklist](goals.md) retains the
full scope; the [original objective](user-objective.md) is unchanged.

## Installed build and current camera work

Bottle **X3**, **CrossOver Preview.app**. Installed source checkpoint `dac2994`:
DLL SHA-256 `2981bf032be8c7e91013e1de7f355d83fb49f4b2ba778b2b5917787f48a9297c`,
11,709,039 bytes. The [install record](../verification/results/chase-elevated-install.json)
binds the reviewed candidate and rollback copy; EXE/configuration are unchanged.
[Review 49](verification/review-49-chase-aim-trace.md) records the scoped host,
x86 hook, camera-math and DLL-load evidence. Native Windows remains untested.

The installed chase defaults are **20° pitch, distance 0.6, rotation/position
response 0.22/0.30 s, offset 0.45, lag limits 8°/0.10**. Vanilla is the default
camera mode. Chase plus telemetry enables four read-only cursor/fire observation
sites; they do not change weapon behavior. The compiler CPU-state boundary fix
is included. Gameplay frame cost is not established by diagnostic timings.

The user completed the first-person left/centre/right firing sweep and repeated
it in third person. The [third-run analysis](verification/chase-third-run.md)
isolates the fault: first person admitted 229 cursor rays; external view rejected
all 180 attempts because the native cursor writer set cursor-active to zero.
A chase-only firing correction is being investigated. Earlier anchor correction removed the reported
trembling in the second flight. The user now requests **13° pitch and distance
0.85**; those source defaults pass 56 focused checks but are not installed.
Batch any justified firing correction with the next camera build.

Use the [run plan](verification/next-runs-2026-09-13.md) for remaining acceptance,
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
- **Bloom components are reviewed and fixture-qualified but unlinked.** Next:
  integrate the [compositor boundary](architecture/hdr-bloom-boundary.md), including
  device ownership/lifetime, Reset, state restoration and recovery. The production
  bridge, owner helper, shader bundle and BloomPass evidence are linked from the
  [history](status-history-2026-09-13.md#bloom-preparation-checkpoint-2026-09-13).
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
