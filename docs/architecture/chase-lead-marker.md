# Chase lead marker and transition observation

2026-09-13. The implementation follows the [native marker study](../reverse-engineering/chase-lead-reticle.md)
and [view transition study](../reverse-engineering/chase-view-transition.md).
It is a chase-camera component, enabled after the existing camera initializes.
It is installed in the [combined build](../../verification/results/linear-material-install.json);
visual gameplay alignment and native Windows remain unverified.
Sector-travel view restoration is **not implemented**; the transition component
observes both the script and save-deserialization mode writers to distinguish
why the engine selects internal view.

## Ownership and scope

`chase_transition` installs six mandatory sites: constructor entry/completion,
destructor entry, updater entry and both updater exits. A bounded 64-cockpit /
8-thread table assigns a lifetime generation only to a completed constructor.
An unknown, partial, destroyed or overflowing lifetime yields no valid token.
Each native updater has a serial, thread and original native frame witness.
A nested update revokes its parent; returning from it never revives the parent.
The module observes both script-created and loaded cockpits.

`chase_lead` binds its first valid applied camera thread and admits only that
thread's current update: active registry cockpit, view/ref object equality,
ordinary rear chase mode 258, connect zero, main guns, tracking mode 2/3,
matching ship identity, and direct target/view objects owned by the same
native type-1 sector. Matching HUD/sector normalized viewports, integer
viewport extents and effective view-plane dimensions bound the projection
scope. Other views retain native behavior.

The three lead sites override only the native view-bit rejection at
`0x42a6fe`, correct successful marker publication at `0x42aaae`, and update
that publication after the final camera FOV at `0x4213dd`. Prediction,
weapon eligibility, icon selection and native depth/visibility checks remain
in the original code. The point uses the sector camera's actual position and
basis. It is captured once from the native successful publication, with no
extra ballistic solver call. New arithmetic uses SSE2; x87 is transported
only for caller preservation and the Windows x86 floating-return ABI.
Viewport extents retain native half-up rounding and integer half extents; final
pixels truncate toward zero, matching the signed native pixel-division
convention. The new double projection avoids the intermediate fixed-point
rounding losses of the original gun-origin calculation.

Eight fixed pending records are keyed by the original overlay invocation frame.
They retain scalar point data and lifetime/update/native object identities,
never owning engine pointers. A nested update can invalidate an admitted
parent, whose later publication then hides its still-owned marker instead of
using the stale point. A skipped publication cannot reuse a previous point.
The final-FOV hook consumes a publication once, and recomputes only when the
validated projection changed. It precedes the synchronous laser notification
callbacks and ordinary HUD draw submission.

Every write revalidates the lifetime and native marker's scene link, screen
flags, identity and writable spans. Projection failure hides only a still-owned
marker through the byte-verified native `0x426280` helper (ESI entry, no stack
arguments), which detaches the scene node; invalid screen coordinates become
`-1,-1`. No module lock is held across this helper. A reused or foreign owner
is never modified. The original game's own subsequent stores remain original
behavior when ownership has already become invalid; the extension cannot make
such an engine-invalid continuation safe.

## Installation, diagnostics and cost

The lifetime/update group, optional diagnostic group and lead group are separate
transactions. Failure rolls back only the affected group, disabling it without
changing the already-qualified camera or cursor-fire component. Lead requires
the mandatory lifetime group; it does not require successful diagnostics.
Installed sites stay for process lifetime; explicit shutdown is quiescent
fixture teardown only.

With telemetry enabled, the transition group observes actual script mode writes
`0x42e742`, deserialized mode writes `0x419e06` and connect setter `0x422cd0`.
It records changed update snapshots plus constructor/destructor events. Script
origins carry validated runtime CODE-relative PC, raw dispatch context and up to four
bounded saved-context/return-offset candidates. No global VM opcode hook is installed. Values
come from actual writes; load-origin records are not mislabeled as script input.
The [transition study](../reverse-engineering/chase-view-transition.md) explains
why native return-address or elapsed-time heuristics cannot authorize a restore.

Fixed first/last rings and counts are flushed on the existing frame-report
cadence. Lead records retain a bounded first/last set of representative
successful projections, early/final FOV, camera basis/position, point and pixel
pairs. Refusal counters distinguish lifetime, identity, read, scope, projection
and marker failures. Callbacks perform no file I/O or allocation. There is no
new per-draw work. Mandatory lifetime/update callbacks and three lead seams
still pay their full CPU-state boundary cost; handler timings explicitly exclude
that stub/preservation overhead. No gameplay FPS improvement is claimed.

## Focused verification

The production geometry and actual scope/publication/finalization helpers are
compiled by `verification/analysis/test_chase_lead.py` with synthetic bounded
32-bit memory. `test_chase_transition.py` exercises the actual portable lifetime,
update, ring and provenance code. Installed-EXE probes decode every complete
containing function, check exact spans and relocation, and reject direct
branches into overwritten instruction interiors. They also qualify the native
hide helper's documented ABI.

The combined CPU fixture passes **12 stubs / 216 checks / zero failures** in
the X3 bottle under the shared Wine lease. It exercises actual generated
callback stubs with hostile GPR/flags/DF, live x87 stack, MXCSR, all XMM
registers, LastError and a four-byte incoming stack. The [compact CPU record](../../verification/results/bottle-X3/chase-lead-transition-cpu.json)
binds the source, executable, static audit and runtime result. The game was not
launched. Native-Windows execution and gameplay marker alignment remain
unverified.

Initial lead/transition checkpoint: **76 host tests pass**, including **26 lead scenarios /
104 assertions** and **114 lifetime/provenance assertions**. Both installed-EXE
probes pass all twelve hook sites and the complete hide helper. Lead, transition,
and the scoped camera/wiring translation units cross-compile with the project's
strict x86/SSE2/four-byte stack flags. The lead object's only x87 instructions
are caller save/restore/reset and one `fstpl` transporting `tan`'s x86 ABI return;
projection and counter arithmetic use SSE2. Review mutations cover overflowing
viewport origins, wrapped cockpit/marker addresses, and diagnostic first/last
sample omission counts.

Reproduce the affected host checks with:

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_chase_lead verification.analysis.test_chase_lead_sites verification.analysis.test_chase_transition verification.analysis.test_chase_transition_sites verification.analysis.test_chase_camera verification.analysis.test_chase_camera_site
python3 verification/probe/verify_chase_lead_sites.py
python3 verification/probe/verify_chase_transition_sites.py
python3 verification/probe/build_chase_transition_cpu.py
```

The last command only cross-builds and statically audits the fixture. Its twelve
emitted stubs run against a fixture `RET` continuation. Transition handlers are
enabled on guarded fixture inputs; the three lead handlers are disabled. The
recorded X3 execution therefore qualifies CPU preservation for those paths,
while host tests cover active lead gate/register policy. It does not execute
the native hide helper or displaced engine continuation. Those limits remain
separate from actual game and native-Windows acceptance.

The recorded runtime invocation (after building the fixture) was:

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py --holder chase-cpu-boundary --timeout 60 '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine' --bottle X3 --no-update /Users/asvetl/x3-mod/build/verification/chase-transition/chase_transition_cpu_fixture.exe > /tmp/x3-chase-lead-transition-cpu-r1.txt 2>&1
```

The same independent review covered source and focused evidence; its concrete
address-validation and diagnostic-count findings are fixed, with no open
findings at this checkpoint.

The later [central-instrument and native-timing change](chase-central-hud.md) has
separate focused evidence. The twelve-stub runtime result above predates its
additional sites and does not qualify them.
