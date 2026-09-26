# Review 42: compositor owner and normal call ABI

2026-09-13. Independent Sol high review approved the isolated
`compositor_owner` helper and [derived study](../reverse-engineering/bloom-invocation-owner.md).
Neither is integrated into capture or an installed DLL. The study also records
lifetime design requirements; their runtime implementation remains pending.

The current view registers are not device pointers. The original compositor
loads renderer, record and device from its own global chain, with independent
renderer/manager reloads. The helper reads that chain and the manager's device
using four bounded, exact-size `ReadProcessMemory` calls. It publishes only a
complete consistent snapshot, clears output on failure and preserves LastError.
It neither calls COM on an untrusted pointer nor scans the device map. Three
planned boundary snapshots cost up to twelve RPM calls per compositor; this
requires measurement, not a claimed frame-time result.

The reviewer reran the local stack audit: 653/653 instructions reachable,
72 calls (55 indirect), 110 ESP operands, zero merge conflicts or caller-frame
accesses, and the sole RET restores entry stack depth. These findings support
an extra wrapper CALL for the inspected normal game ABI, not arbitrary plugins
or all exceptional paths. No raw game listings are tracked.

| Finding | Status |
| --- | --- |
| Helper identity could be mistaken for settings/glow or full invocation eligibility. | Fixed documentation: those are separate checks; the helper establishes only an observation of owner identity. |
| Revoking on every device Release could disable every frame due to ordinary D3DX/child traffic. | Rejected as the default plan; require measured actual reference accounting and a capture-wide internal-operation/retirement scope. Implementation and runtime qualification remain. |
| ResetEx can bypass the current Reset hook. | Explicit integration requirement: cover slot 132 before admitting the Ex path, with revocation before the native call. |
| Last-device actions assume immediate quiescence. | Require a pending transition drained after invocation cleanup; audit all actions in that branch. |
| An unlocked original can overlap a cross-thread final Release. | Explicitly retain its native pin until original finishes or exclude the transition; do not destroy the executing device. Combined lifetime tests remain. |

The 24-check host fixture exercises every read failure, partial-read rejection,
null/wrapping spans, inconsistent devices, each identity change and rebased image
globals. It passed independently, as did the required-flag i686 cross-compilation,
no-x87 inspection and whitespace checks. These do not execute Windows RPM,
prove live ownership/thread exclusion, or qualify the integrated hook.
