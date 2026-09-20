# Staged media presentation gate

2026-09-20. The gate is a qualified synthetic prerequisite for a future owned
media copy interval. No production caller, CMake wiring, game detour or copy
admission is enabled. Binding/table exclusion, actual engine/device ownership,
complete Reset-route coverage and exceptional cleanup remain prerequisites to
integration. Qualification is in the [media ledger](../verification/media-cues.md#staged-presentation-deferral-gate-2026-09-20)
and [compact record](../../verification/results/media-presentation-gate-2026-09-20.json).

## Engine boundary and implementation

The reviewed EXE entry `0x4dac30` begins with the complete five-byte
`MOV EAX,[0x608b3c]`; forwarding continues at `0x4dac35`. The existing deferred
return at `0x4dac6b` stores unavailable zero at `0x608ae0` and returns without
Present, TestCooperativeLevel or Reset preparation. Both branches require the
original entry ESP. Static encoded-entry qualification is retained in
`/tmp/x3-media-reset-gate-site.md` and its paired check JSON: four relative
references to the helper, zero scanned references into overwritten interiors,
zero whole-file entry/interior literals. Arbitrary computed entry or concurrent
installation is not proved by that scan.

[`media_presentation_gate.cpp`](../../src/proxy/media_presentation_gate.cpp)
emits 27 bytes: PUSHFD, compare an aligned owned-copy word against zero, JNE,
and POPFD on each arm. Forwarding executes the displaced MOV once and jumps to
the continuation; deferral jumps directly to the existing store/RET. There is
no C++ helper, external call, allocation, lock, logging, FP instruction or
LastError operation in the gate. It uses one counter read and four temporary
stack bytes. This is an instruction-cost assessment, not a measured game
performance result. The gate does not suppress other work in the wider rendering
helper and does not itself resolve or retain a surface.

The portable encoder/state/transaction API is in
[`media_presentation_gate.h`](../../src/proxy/media_presentation_gate.h).
The [Win32 adapter](../../src/proxy/media_presentation_gate_win32.cpp) uses
VirtualAlloc, VirtualProtect, VirtualQuery, ReadProcessMemory,
InterlockedCompareExchange64 and FlushInstructionCache. Its production
qualification predicates call the existing executable identity verifier and
shared install-window check. No backend-private layout, export or lock is used.

## Future API ordering

The sequence below describes the contract an integration must satisfy; it does
not authorize enabling the currently unqualified engine acquisition path.

1. In a serialized, qualified startup window, obtain `process_runtime()` and
   establish the actual designated engine/device thread with `qualify_owner`.
   The runtime and its aligned atomic counter occupy process-lifetime allocated
   storage. `stage(platform, transaction, game_site(), admission)` verifies the
   executable/site/targets, emits and seals the gate, and leaves the site intact.
2. `install` rechecks identity, window and target anchors. It binds the exact
   Admission instance staged into the emitted predicate, compares/exchanges the
   aligned eight-byte site word, preserving the three neighboring bytes, and
   checks readback, instruction-cache flush and original protection restoration.
   Successful installation still leaves admission disabled. `enable(thread)`
   requires this counter's successful installation and a qualified owner; it
   cannot establish the external engine binding/lifetime proofs.
3. Before acquiring any temporary destination reference or lock, construct a
   `CopyScope` with a `noexcept` cleanup callback and its already-live context.
   Enter external acquisition/copy work only if the scope is valid. Nested and
   foreign copies are refused; the active word is a zero/one state, not an
   incrementing count. Safely resolve/retain the destination only under the
   separately qualified engine mutation/binding exclusion, then release that
   short exclusion before external work.
4. Cleanup must handle partial acquisition, unlock every successful lock and
   release every temporary/default-pool reference. `CopyScope::close()` invokes
   that callback while depth remains one, then clears depth. Destruction calls
   the same idempotent close. The context must outlive the scope; destruction
   must occur on the creating thread. A nested presentation through this exact
   helper defers even during final Release. No persistent lease is retained for
   a future presentation or Reset.
5. The opposite direction requires `begin_reset(thread, Reset::engine)` before
   the whole engine helper's precleanup, with nested `Reset::native` admission
   around the actual native Reset. Every admitted reset needs its matching
   `end_reset` on all exits. Failed native Reset remains sticky even if an outer
   engine exit reports success. Only independently observed actual recovery may
   call `observe_recovery`. A refused reset admission requires an already
   qualified pre-Reset deferral route: do not forward public Reset with a live
   default-pool lease or invent its HRESULT. These observers are not installed
   by this module, and arbitrary bypass routes remain outside its protection.
6. To detach, disable new admission, complete cleanup of all active scopes and
   establish a separately proved execution/quiescence window. `restore` also
   clears installation readiness before any removal attempt. It refuses an
   active scope or absent quiescence. A begun removal is one-way for that
   transaction: merely calling `enable` after a refused or completed removal
   cannot revive it. Never free the emitted code or counter; a fetched redirect
   or previously entered stub may still reference them.

`CopyScope` provides ordinary C++ scope cleanup, not a Win32 SEH/longjmp or other
nonlocal-unwind guarantee. The fixture does not qualify fault cleanup. The
current default-off state must remain until the actual integration's relevant
exit routes are accounted for; a depth bit must never be cleared while resources
remain live merely to recover presentation.

## Checked transaction and retained ownership

Emission must pass write/readback, cache flush and RX protection checks before
it becomes ready. Each transaction makes at most one allocated emission attempt;
reserved pages are process-lifetime and never reused, including failed emission.
Installation is permitted only while the shared startup window and caller-owned
execution exclusion hold. An aligned atomic swap adds protection against mixed
bytes; it does not make a late or concurrent install safe.

The transaction separately retains possible executable redirection, instruction
cache debt and original-protection debt. Failed rollback does not discard that
state. After disabling admission, a retained gate forwards safely with zero copy
depth. `restore` retries original/owned bytes, never overwrites an unexpected
third-party replacement, and clears debts only on successful corresponding
checks. A failed atomic comparison never claims redirect ownership. The
`ever_published` marker remains after removal, and the same transaction cannot
be reinstalled. Allocation lifetime is independent of DLL data lifetime; emitted
code reads only its allocated counter and EXE addresses.

## Qualification limits

The fixture executes the production emitted gate and real successful Win32
allocation/protection/swap/flush/restore operations against relocated synthetic
helpers. Its executable-identity and install-window predicates are stand-ins.
The production transaction's API failures are exercised through a mock Platform;
this is not native fault injection into VirtualProtect or FlushInstructionCache.

A real D3D9 default-pool surface is locked and written while the synthetic helper
is deferred, then unlocked and finally released while depth is still one. A
later ordinary helper call invokes native Present, TestCooperativeLevel and a
**forced** native Reset, which succeeds. The synthetic continuation deliberately
forces Reset rather than reproducing the game's lost-device/HRESULT branches;
this does not prove an actual game loss/recovery cycle, canonical surface-wrapper
acquisition or engine Reset-observer integration.

All four four-byte stack residues, seeded GPRs/flags/LastError, three live x87
values, XMM0–7 and the full represented MXCSR word are checked. X3/FEX applies
requested MXCSR `0x3fa5` as `0x3f80` before entry. Every applied/entry/return/output
word matches exactly; no status-bit mask weakens preservation checks. Nonzero
requested sticky-status preservation cannot be exercised on this runtime and
remains explicitly unverified. Native Windows compilation compatibility is not
native Windows execution evidence. There was no game launch or game hook enable.
