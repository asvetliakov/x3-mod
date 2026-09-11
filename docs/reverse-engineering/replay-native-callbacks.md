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
