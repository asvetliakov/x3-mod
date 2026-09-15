# Native replay callbacks: pinned Preview observations

This is a backend-specific static investigation, not the portable replay admission
contract. The renderer must also work on Windows with native D3D9. Its shared
production admission mechanism must not depend on Wine exports, object layouts or
the Wine global mutex. The proposed portable contract is tracked separately in
[motion replay exclusion](../architecture/motion-replay-exclusion.md).

The useful result is a concrete callback hazard: **a native resource or stateblock
final release can synchronously invoke application `IUnknown::Release` while the
Wine mutex is held.** A recursive mutex does not make that callback safe. The
existing motion pass must separate restoration from resource retirement before
enclosing it in any admission mechanism that cannot tolerate application reentry.

## Provenance and scope

Read-only inspection on 2026-09-11 used the installed x86 modules in
`~/Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/syswow64/`:

| Module | SHA-256 | Image size |
|---|---|---:|
| `d3d9.dll` | `58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf` | `0x2c000` |
| `wined3d.dll` | `f4997bc0465de7e87bac9921bf0274db00ac3b3ba0754fa03f1f33e309a8e863` | `0x2d0000` |

Addresses below are RVAs, not process addresses. Both files have preferred image
base `0x10000000`. PE import/export tables, native vtable pointers and local
`objdump -d` output establish the paths; upstream source is not substituted for
installed-byte evidence. Raw disassembly remains local and untracked. The motion
pass inspected was `src/renderer/rigid_motion.cpp` SHA-256
`6e2720b27908658e02226dcb730d0c78d0b6bfed7ef7c24d6d4ff6f7ca1eeb4f`.
No game or native GPU probe was run for this note.

## Synchronous application callback chain

The installed instructions establish the following chain:

1. Wine `wined3d_buffer_decref`, RVA `0x1a9e0`, atomically decrements the backing
   reference count. On zero it acquires the Wine mutex at `0x1aa28`, loads the
   parent and parent-operations pointers from offsets `0x8c` and `0x90`, and calls
   the first parent callback at `0x1aa3c`. Unlock is at `0x1aa53`.
2. D3D9's buffer parent-operations table at `0x191ec` selects callback `0x21e0`.
   Buffer initialization supplies that table for the applicable backing object
   (`0x13b7` and `0x13fc`); other internal backing objects can use a no-op parent.
   The callback invokes resource cleanup `0x3410` at `0x21fe`.
3. Cleanup traverses private-data entries. At `0x344c` it tests flags bit 0
   (`D3DSPD_IUNKNOWN`), then calls the stored object's COM slot 2 at `0x345a`.
   This target is application-provided, not a closed set of renderer functions.

Therefore a legal private-IUnknown value on a resource provides a concrete
reentry opportunity during final backing destruction. This is a static
counterexample to treating all native releases as callback-free; no assertion
that X3's captured resources actually contain such values is needed.

Stateblocks retain backing resources and can reach that destruction path:
Wine `wined3d_stateblock_decref` (`0x899a0`) locks at `0x899dc`, calls cleanup
`0x89a10`, frees the block and unlocks at `0x899f6`. Cleanup decrements vertex
declaration, stream/index buffers, VS, PS and texture references, including stream
buffer decrement at `0x89a5a` and index-buffer decrement at `0x89a79`.
D3D9 stateblock `Release` (`0x104a0`) invokes that export at `0x104ef`, then invokes
its parent device's COM `Release` at `0x104fc`.

This also explains why merely moving the explicit resource releases while
leaving the saved-stateblock destructor inside admission is insufficient.
Recapturing an existing stateblock is not automatically safe either: it replaces
previous retained state. This note does not establish a callback-free recapture
contract.

## Limited native mutex coverage already established

This table preserves the investigated backend facts. It is neither a complete
API audit nor a proposed production dependency.

| Operation | Installed scope observed |
|---|---|
| VB `Lock` / `Unlock` (`0x1f90` / `0x2060`) and corresponding IB resource-map path | D3D9 delegates to Wine resource map/unmap; there is no D3D9 mutex-import call in these two VB methods. Resource map `0x7b690` supplies the device's immediate context (`device+0x12d0`). Context map `0x4c190` compares against that immediate context at `0x4c3c3`, locks at `0x4c3cb`, emits the map and unlocks at `0x4c428`. Context unmap locks at `0x4c4c1`, emits unmap at `0x4c4c9`, unlocks at `0x4c4de`. Earlier map validation reads descriptions/ranges. |
| `ProcessVertices` (`0xa4f0`) | Locks at `0xa56f`; temporary source rebinding, the native processing call (`0xa64b`) and restoration precede unlock `0xa6eb`. |
| `StateBlock::Apply` (`0x10650`) | Locks at `0x10687`, calls native apply at `0x106df`, updates cached stream/index/texture flags while locked, unlocks at `0x107b7`. |
| `StateBlock::Capture` (`0x105b0`) | Locks at `0x105e5`; recording check and native capture precede unlock `0x1063b` on success. |
| `SetTexture` (`0x9080`) | Locks at `0x90e7`, updates the backing state and cached autogen flags, unlocks at `0x9144`. |
| `SetStreamSourceFreq` (`0xb470`) | Native state update lies between lock `0xb4b8` and unlock `0xb4cc`. |
| `SetIndices` (`0xb550`) | Both null/non-null branches acquire the lock before backing-state and cached index flags change; unlock `0xb5ef`. |

A successful map returns an application-writable pointer **after** native unlock.
Holding the native mutex cannot stop writes through an already returned pointer.
Nor does an ownership pending-map check alone close the interval between native
map return and publication of the wrapper's pending state. The portable admission
design still needs to account for entry, native execution, bookkeeping and the
whole mapped-write interval.

Mutex coverage was not extended to every query, setter or destruction path after
the portability requirement ruled out this route. In particular the observed
Wine texture final-decrement routine (`0x9d8f0`) has a command-stream-thread branch
that skips the global mutex. Its complete parent-callback/destruction timing was
not proven here. Neither universal mutex coverage nor absence of asynchronous
callback hazards follows from the table.

## Consequences for a portable replay critical section

These are implementation requirements/proposals, not an accepted proof that the
current pass can execute under an exclusive token.

| Current work | Required separation or remaining proof |
|---|---|
| `RigidMotionPass::initialize`, motion-target allocation, shader/declaration creation | Prepare outside exclusive replay. `initialize` starts by destroying old resources; even a successful fast path must not hide that retirement inside admission. Revalidate the prepared generation and device before using it. |
| `same_device` and descriptor queries | These currently acquire and release temporary device references. Validate stable native identity outside replay where possible, retaining explicit owners until retirement; any revalidation inside replay needs a defined getter contract. A getter is not automatically callback-free merely because it is read-only. |
| Full-state snapshot and RT/DS getters | Capture before mutation while application state is excluded. Keep all displaced application binding references alive through restore. A stateblock alone does not replace explicit RT/DS retention. Partial-capture failures must also defer cleanup until outside admission. |
| Unbind/bind operations, constants, ordinary state setters, draws and `StateBlock::Apply` | These are the smallest candidate native execution region after preparation and validation. Keeping original and replay resources alive prevents the particular final-reference callback chain demonstrated above. It does **not** by itself prove all driver/runtime operations callback-free on Windows or Preview. |
| Invalid-motion initialization using `DrawPrimitiveUP` | The current path can cause native upload/allocation work. A precreated immutable triangle VB would remove that particular UP upload from the region, but is a proposed simplification, not a proven callback or performance fix. |
| `SavedState` destruction, pass `shutdown`, temporary target references, geometry-lease retirement | Restore under exclusion, then release the token before final COM/native retirement. Today `SavedState` destruction occurs inside `run`; `shutdown` releases shader/declaration objects. An enclosing admission guard must not silently include these destructors, including early-return and loss paths. |

An admission protocol must additionally handle synchronous same-thread reentry
without waiting on its own token and without letting an ordinary application
mutation impersonate renderer work. That behavior cannot be supplied by an
unchecked thread-local bypass. For now the hard live replay gate remains required;
this backend note does not change it.

## Optional allocation Lock bookends — source checkpoint, 2026-09-15

The counter-first step of [directional shadows §9](../architecture/directional-shadows.md#counter-first-observe-attempts-completion-and-the-draw-to-replay-interval)
now has a separate `Options::track_buffer_lock_attempts` switch, requiring existing
write tracking. `get_buffer_lock_view` takes a single registry snapshot entirely
from CPU metadata: allocation ID, device Reset generation, Lock/Unlock serials,
in-flight calls, successful pending maps/content revision, last Lock range/flags/
thread, Unlock thread and saturating flag/failure totals. Zero-size ranges retain
the actual API argument (whole/remainder semantics are not expanded by a getter).
READONLY advances the attempt serial and READONLY total but preserves the existing
content revision. DISCARD/NOOVERWRITE totals are independent flag counts, including
failed calls; ordinary means none of READONLY/DISCARD/NOOVERWRITE. A successful
writable Lock still advances the existing revision before return; the counter
change does not redefine an observed write as a byte comparison or hash.

An allocation-owned private-IUnknown counter sidecar is created only at successful
native resource creation, with a process-unique nonzero ID. It owns no native or
device reference and has no geometry payload. Wrapper adoption authenticates that
sidecar once through an allocation-local atomic thread/count AddRef witness,
then retains only the CPU record. No compiler TLS is used by the new callbacks. Canonical native identity keys the creation/
adoption table, so aliases and recreated application shells share counters.
Creation is capped at 8192 living/deferred sidecars; failed allocation,
authentication, missing metadata, exhausted IDs or tampering never produces a
known snapshot. A retained old sidecar cannot erase a replacement at the same
identity: deferred deletion erases the table entry only when it still names that
exact sidecar. Native final Release enqueues CPU retirement without acquiring the
registry; creation, adoption and ordinary final-release cleanup drain it later.

Lock and Unlock entry bookends run before the existing native dispatch; completion
runs after native result observation, pending/revision publication, finite evidence
and temporary-sidecar cleanup. The native Lock/Unlock calls and application map
interval remain outside the registry mutex. No new native call, allocation, hash,
logging, payload read or sentinel write occurs in a bookend. Existing content
private-data operations remain unchanged and mirror into the CPU record while
holding their existing registry lock. Registration/authentication native private-
data calls happen only at creation/adoption; this does not certify all private-
data/raw-helper operations for future replay authority.

Every Reset attempt advances the device-local generation before native Reset,
including failed attempts. Reset/loss/retirement makes views unavailable; a later
successful Reset retains allocation counters and publishes the new generation.
Overflow permanently vetoes known state. The native-input and native-output
x87/MXCSR/LastError envelope still surrounds the entire helper, including the new
bookends; generated application-admission shells and native Lock arguments/output
slots remain unchanged. No engine instruction patch or replay draw is introduced.

Focused host production-core checks cover 28 assertions, including barriers before
native completion and after result publication, overlapping failed Lock, pending
maps, READONLY classification and every new counter's saturation. The actual
production-wrapper fixture is `verification/probe/buffer_lock_bookends_fixture.cpp`;
it cross-compiles with GCC 16.2 for x86 Windows using SSE2 and the four-byte incoming
stack contract. It adds native-entry/result-publication barriers, HRESULT/output/
CPU/LastError checks, failed Unlock, wrapper recreation/allocation retirement,
failed/successful Reset, metadata tamper, disabled allocation cost and IB coverage.
The parent ran the frozen standalone executable under the serialized X3 Wine queue:
80 checks passed, exit 0, child 4.933053 s and lock wait 0.000003083 s. Provenance:
X3, WineArch arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`,
`X3M_ADMISSION=0`, `WINEDLLOVERRIDES=d3d9=b`. Executable SHA-256:
`71eb7d475c4386a35e1baa8c441c30d1c224c1610925adb64628649b3333410b`;
local report/timings: `/tmp/x3-lock-check/owner-report.txt` and
`/tmp/x3-lock-check/owner-timings.json`. Final source guards skip adoption private-data authentication after a known
device tracking failure and skip the bookend helper calls when disabled. The
latter avoids GCC SJLJ exception registration before an internal option check;
emitted-code inspection confirms both disabled branches jump over the helper.
The final scoped fixture adds a retained-old-sidecar/foreign-IUnknown replacement
case that proves recreation makes no foreign AddRef after known tampering, plus
entry counters proving disabled Lock/Unlock bypass both helpers. The parent ran
that final executable with the same X3 environment: 88 checks passed, exit 0,
child 4.327074416 s, lock wait 0.000003500 s. Final executable SHA-256:
`2075fd8442ac9cdad7a54c540b6dc52906a6a9ee3f13b168fff43c75cf6527da`;
local report/timings: `/tmp/x3-lock-final1/owner-report.txt` and
`/tmp/x3-lock-final1/owner-timings.json`. Native Windows runtime remains unverified.

Cost inspection: disabled Lock/Unlock adds option branches with no new metadata
mutex/native calls or allocation; enabled calls add two short CPU registry sections
and fixed integer work. Successful content publication copies three existing
metadata values through the cached sidecar pointer. Snapshots add one registry
lookup and a fixed-size copy. Each wrapper gains one CPU pointer and each device
one generation value. No per-draw caller exists yet and no game-FPS claim follows.
Complete entry/window/native-helper coverage, draw-to-replay source integration,
exclusive replay races and native Windows runtime qualification remain separate
gates. Neither a quiet interval nor this API changes the hard live replay gate.


### Review correction: cold native callback and snapshot boundaries

Independent deep review found that the first counter implementation reused a
compiler-TLS AddRef witness, whose emitted `__emutls_get_address` path could lock,
allocate or abort on allocation failure on a new thread. Its Release and direct
snapshot guards also had GCC SJLJ setup/teardown outside the saved CPU envelope.
No runtime corruption was observed; this was a substantive emitted-code finding.

The new counter's QI/AddRef/Release methods now have narrowly scoped GCC
`no-exceptions` definitions. Their dedicated, always-inlined `CounterAbiState`
saves x87/MXCSR before any call and restores after `SetLastError`. Reusing the
old `ExecutionState` helpers was rejected by transitive inspection: those
out-of-line helpers themselves register SJLJ frames. All ownership containers,
creation rollback and snapshot registry work retain ordinary exception support.
The public snapshot is a no-EH outer shell around a core that catches C++ failures
and returns `S_FALSE`, `known=false`, `status=E_FAIL`; nothing unwinds through the
outer saved state. These promises concern ordinary returns, not arbitrary SEH
faults, illegal COM use or failures inside the C++ runtime's own exception system.

Authentication no longer needs any TLS initialization or cold allocation. The
adoption caller, under the registry, publishes the sidecar's expected Win32 thread
ID and clears its atomic AddRef count before native GetPrivateData. The callback
increments that count only for the matching thread; adoption clears both fields
afterward and accepts only exact identity/size/result plus one witnessed AddRef.
An already-active authentication refuses before native dispatch and leaves the
outer witness intact. Native AddRef/Release use only integer atomics and the
saved CPU state; final Release only enqueues deferred CPU retirement. There is no
new callback allocator whose cold failure could terminate the application.

`verify_buffer_lock_callbacks.py` checks the final fixture's emitted call closure,
including imports, the snapshot core's catch and the absence of compiler warmup
in the raw CreateThread entry. The review-fix executable has three callback
closures whose only external calls are documented GetLastError, SetLastError and
GetCurrentThreadId (QI also calls the audited AddRef). Its snapshot shell adds
only the contained registry-core call. The seven fresh-thread cases independently
exercise AddRef, Release, QI, nonmatching authentication thread, direct snapshot,
caught snapshot failure and final Release, with x87/MXCSR/LastError comparisons.
Nested and successful witness publication plus the earlier tamper controls remain
in the same scoped fixture. The earlier 88-check witness remains valid for its
unchanged paths. The parent ran the frozen review-fix executable under the same
serialized X3 environment (arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`,
`X3M_ADMISSION=0`, `WINEDLLOVERRIDES=d3d9=b`): **124 checks passed**, exit 0,
child 4.734419708 s, lock wait 0.000002959 s. Executable SHA-256:
`1b9ee3a48844f56cc6505c8df10481a1f3377bacd564a9477a9eb794b6e56945`.
Local evidence is `/tmp/x3-lock-reviewfix/owner-report.txt`,
`owner-timings.json` and `callback-audit.json` in the same directory. Final source
also passes strict x86 compilation and the 28-assertion host core check. Native
Windows runtime remains unverified. The same independent deep reviewer accepted
the correction after re-running the emitted callback/snapshot audit and checking
the frozen binary and patch hashes. This acceptance covers counter observation
only; replay entry coverage and activation remain separate gates.
