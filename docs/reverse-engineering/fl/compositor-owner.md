# FL compositor device owner

Offline bounded qualification, 2026-10-28. Image `/mnt/c/X3FL-orig/X3FL.exe`,
SHA-256 `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Preferred VAs below use base `0x400000`; a runtime descriptor uses checked
loaded-base RVAs. Owning integration proposal:
[connected renderer](../../architecture/farnhams-legacy-renderer-integration.md).

**The minimal FL owner is `renderer=*0x5aaf94`, `record=[renderer+0x18]`,
`device=[record]`. A synchronous three-DWORD copy at the admitted compositor
boundary is supported under the existing complete-pass, startup/load, and
native callback exclusions.** Compare its device to the actual ordinary-camera
D3D event owner and retain that exact registry device before injected work.
The copied addresses alone grant neither native-memory permission nor a COM
reference. No AP manager agreement is required for this minimal contract.

This closes the targeted static owner prerequisite in the proposal, conditional
on those existing exclusions being active. It does not install or test an FL
owner reader, transport events, qualify bloom shaders, or activate rendering.

## 1. Typed layout and same-device evidence

| Object/field | Established native evidence |
| --- | --- |
| Renderer/cache root, `0x5aaf94` (RVA `0x1aaf94`) | Probe `0x4f13b0` allocates **0x70 bytes**, ECX=size, EDX=0, via `0x4d8fc0` at `0x4f13c1`; publishes at `0x4f13c8`. Display setup has the equivalent null-root branch at `0x4f3bf1..0x4f3c09`, publication `0x4f3c04`. The allocator zeroes the requested block at `0x4d90ad..0x4d90b1`. |
| Renderer `[0]` | Direct3DCreate9 returns the factory stored at `0x4f13d9` / `0x4f3c1c`. This is **IDirect3D9**, not the compositor device. |
| Renderer `+0x18` | Device-record allocation requests **0x1a8 bytes** at `0x4f1e38/0x4f1e75`; zeroes at `0x4f1eac..0x4f1eb4`, publishes ESI at `0x4f1ec1`, then reloads EDI at `0x4f1ec9`. Failure can publish null and returns failure. Publication alone is not readiness. |
| Record `[0]` | CreateDevice at `0x4f2489` receives **EDI as its output pointer**, record+0x164 as presentation parameters, and the factory saved from renderer[0]. `0x4f248e` rejects nonzero HRESULT. Device identity therefore follows an actual API output slot. |
| Record `+0x164` | Presentation parameter block, distinct from device[0]. Native Reset passes this address directly. It may be modified by the API without replacing device[0]. |
| Ordinary camera device | `0x4ded96` loads root, `0x4ded9e` selects record+0x18, `0x4deda8` loads device into EDI. `0x4defac/0x4defad` pass that EDI as explicit COM this to SetViewport (+0xbc). |
| Compositor device | `0x4e67a2` loads root, `0x4e67a8` selects record+0x18, `0x4e67ec` loads device into EBX, `0x4e67f5` retains it in EBP+0x10. The glow-off branch can return **before** these loads; that does not change the scene-end callsite's owner contract. |

The CreateDevice call pushes seven DWORDs including factory this; factory slot
+0x40 is CreateDevice, whereas **device** slot +0x40 is Reset. The receiver type
and output provenance distinguish them. Likewise `0x5a82f8` is the separately
allocated screen container; its `[0]` and `+4` are screen-surface records. It is
not an alternative device-owner root. See [initial publication and Reset](ui-layout-generation.md).

The native state-manager wrapper at renderer+0x1c is typed by the prior
[recovery study](ui-render-mutation.md), but this owner snapshot
does not read it or inherit AP's manager+4 agreement/lifetime requirements.

## 2. Publication, replacement, free and the live interval

The direct root-operand census measures **297 references**, with exactly three
direct stores: initial publications `0x4f13c8`, `0x4f3c04`, and teardown clear
`0x4f4d6d`. An additional **range write** must not be omitted: startup
`0x4ed5e6..0x4ed5f2` zeroes 0x24c bytes at `0x5aaf00`, including this root.
Its only surveyed caller is `0x51ea26`. These counts are a direct operand census,
not a general alias theorem.

The finite owner lifecycle has the following edges:

| Phase | Writes and lifetime effect |
| --- | --- |
| Bootstrap/setup | `0x4772bc → 0x4f13b0`; display `0x5168c5 → 0x4f3a30`, whose fallback also calls probe at `0x4f3c38`. Sole direct device-record creator: `0x4f3e0a → 0x4f1e10`. The record address is published before creation succeeds; admitted rendering must remain unavailable on failure/unwind. |
| Live complete pass | `0x48b6d0` reaches camera activation and `0x48b929 → 0x4e6770`. These consumers select the existing root/record/device. No owner-construction/destruction edge is supplied by the qualified native render callbacks. |
| Allocation recovery | Registered callback `*0x5aae24=0x47b500` at `0x4778f3`; `0x47b506 → 0x485c00`, `0x47b50b → 0x512410`. This reclaims engine resources and textures; it does **not** replace/free the renderer cache, device record or the game's device reference. |
| Lost-device recovery | `0x500c77 → 0x4f39d0 → 0x4f3740` (`0x4f3a18`). Device comes from the same record[0] at `0x4f3754`; Reset is `0x4f37cd`. Old surfaces are released and reacquired, but the helper does not replace root, record or device[0]. |
| Shutdown | Sole direct display-owner destruction path: `0x476a1a → 0x516ab0`, `0x516b2b → 0x4f4b30`. In shutdown `0x4762d0`, renderer/global-buffer cleanup and engine destruction already occur at `0x47640c/0x476411`. This is not a live complete-pass continuation. |
| Device retirement | `0x4f4bc6/0x4f4bc9` select record[0]; `0x4f4bd2` calls its Release (+8), then `0x4f4bdd` clears record[0]. |
| Record retirement | `0x4f4cda` selects renderer+0x18; `0x4f4ce2` frees its **0x1a8-byte** allocation; `0x4f4d03` clears renderer+0x18. |
| Root retirement | Factory reference is released at `0x4f4d3f`; `0x4f4d57` frees the **0x70-byte** renderer; `0x4f4d6d` clears the global. Do not follow stale copied addresses during or after this sequence. |

The recovery statement reuses the actual typed resource-release and demand-load
closure in [render mutation](ui-render-mutation.md), including
[registry/resource reclamation](registry-value-lifetime.md).
Vertex/index buffers, meshes, textures/surfaces and effect parameter blocks are
not the owner record. Device calls on those routes receive the device interface,
not `&renderer+0x18` or `&record[0]` as an output capable of replacing this chain.
Their runtime/API callbacks remain subject to the existing
[window/audio/close exclusion](ui-render-exclusion.md).
Recovery's recursion guard is not itself a lifetime lock.

The conclusion is bounded: bootstrap/teardown are excluded by established
startup and common borrow admission; native render-time recovery has the already
qualified typed leaves. It is **not** inferred merely from no direct teardown
CALL in one function. No whole-engine computed-alias proof, foreign private
address call, externally replaced vtable, or new callback registration is claimed.
A mod callback must not introduce such an unqualified teardown/reinitialization
route while holding a borrow. A new renderer incarnation requires fresh
admission even if allocations reuse all three addresses.

## 3. Boundary ABI, reentrancy and exception safety

Use the existing **full pre/original/post compositor bridge** at
`0x48b929..0x48b92e`, a single five-byte relative CALL. Its sole native target is
`0x4e6770`, which accepts no stack argument and returns with plain RET
(`0x4e6e86`). This contrasts with ordinary-camera origin wrapper `0x495670`,
which returns RET 4. No prologue relocation is needed for the compositor seam.
The census finds no direct edge into its interior and no HIGHLOW operand there;
the seam fits an aligned qword (offset 1, length 5). The existing quiet
installation and rollback protocol remains necessary.

ESI is the next camera, EBX the loop bound and EBP the native exception frame.
The continuation sets the compositor-done byte at `0x48b92e` and reloads camera
flags. Preserve all GPRs, EFLAGS, LastError and full FP/SIMD state around the copy
and callbacks; forward once, preserving actual outgoing native state. Native
compositor FS:[0] setup does not authorize skipping the original or inventing a
success return. Use existing invocation storage and foreign-SEH cleanup; exception
search/ContinueExecution retains the invocation, outward unwind retires it once.
An abnormal construction/teardown does not restore the native owner chain.

The reader is legal only on the common borrow's owner thread, at this admitted
boundary, with startup/load/teardown excluded. Make the three loads and output
copy call-free under that permission; bounded fault handling may refuse but
page readability is not a substitute for permission. Do not retain native
renderer/record dereference permission in a ticket. Each later native reread
needs a still-current common borrow. Deny nested complete-pass/compositor
correlation and revoke the outer ticket; native forwarding still occurs.

Injected D3D work can invoke the ordinary recovery protocol. Copy first, then
acquire the exact selected device's native reference through the registry;
revalidate common/camera/device/Reset generations after any call before acting.
Never hold the common lifetime metadata mutex across COM, original forwarding,
logging or deferred close service. The device/resource pin must survive revocation
until invocation cleanup; permission to inject work may end earlier.

## 4. Minimal integration descriptor and API

A separate immutable, executable-qualified `FlOwnerContract` needs only:

* Renderer root RVA **0x1aaf94**, record offset **0x18**, device offset **0**.
* Compositor call RVA **0x8b929**, continuation **0x8b92e**, original target
  **0xe6770**, and the qualified common borrow/edition identity.
* `FlOwnerSnapshot { uint32_t renderer, record, device; }`, with zero output on
  refusal. A typed FL read takes the contract and a live common borrow; a bare
  image base or AP executable gate is insufficient authorization.

Validate 32-bit nonzero addresses and addition bounds, copy exactly three
DWORDs, and compare all three only within the same admitted incarnation. No
manager field, dimension heuristic, global device broadcast or AP glow-config
read belongs in this API. `same(snapshot)` establishes slot identity only.
Generation/borrow admission establishes the right to read; registry identity
and AddRef establish a distinct native reference.

Before resolving, mark this pass compositor-seen even if owner acceptance fails.
Require one eligible device, successful ordinary activation SetViewport/Clear
correlation, accepted Scene, known main RT/depth/viewport/content IDs, and matching
camera/common/Reset generations. The ordinary activation ignores setter/Clear
return values: only completed public device events establish success.

Compare `snapshot.device` exactly to that event owner's **native interface key**
in capture's device registry. Retain its CPU `shared_ptr` and use its saved native
AddRef entry to pin that same device, following `capture.cpp::compositor_pre`.
Recheck registry membership/incarnation and generations before use; a CPU registry
entry or matching pointer number alone is not a COM pin. Cleanup releases the
native reference outside the capture mutex, preserving the CPU owner until that
Release finishes, as existing `compositor_cleanup` does. Post can compare actual
copy events and state changes, but those events cannot retroactively authorize
an incorrectly selected pre resolve. Glow off still uses this boundary.

Every actual Reset **attempt** must revoke correlation/history/camera tickets,
including failed HRESULT and locally refused reentrant attempts. Numeric owner
identity survives the native Reset and therefore cannot detect this invalidation.
Native success-only code reacquires surfaces; `0x4f3a1d` sets the native readiness
flag without testing Reset's result, so it is not success evidence. Preserve
existing GPU-operation protection and invocation pins. A foreign Reset thread
publishes only an atomic monotonic invalidation request to owner-confined FL
camera state; the owner consumes it before copy/publication/current/read. The
[proposal's Reset handoff](../../architecture/farnhams-legacy-renderer-integration.md)
is still an implementation obligation, not behavior established by this RE.

## Evidence and remaining acceptance

```sh
python3 verification/probe/verify_fl_compositor_owner.py --raw
python3 -m py_compile verification/probe/verify_fl_compositor_owner.py
```

Measured verifier result: **20 bounded regions / 2,053 instructions, 63 operand
anchors, 640 independently bound image operands/edges, 297 root references,
three direct root stores, nine rejected semantic mutations**. The mutations
bypass EXE and region hashes and alter camera/compositor record offsets,
CreateDevice output, record publication, Reset method, device Release, record
free target, root retirement or recovery target. The finite direct-edge census
also finds no relocated publication of the nine selected owner/seam entries.
Full-text direct-edge census size is **428,814 decoded instructions**; it is
not a full-engine lifetime proof. Normalized regions preserve instruction sizes,
registers and field offsets; masked image operands are independently hashed.

The [verifier](../../../verification/probe/verify_fl_compositor_owner.py), JSON
report, producing `reproduce.sh` and optional raw listings are separated:
the compact report and reproducer are retained under
`verification/results/fl-port/compositor-owner/`; raw listings stay ignored.
Independent deep review accepted the contract and verifier, checking all region
signatures, anchors and operand bindings against the qualified executable.
No production source, binary, installation or game state changed. No Wine,
native fixture or game was executed.

There is no additional native owner-publication seam required by this fixed
contract. Concrete remaining work is the typed reader, connected event selection,
real COM pin/cleanup and Reset-request implementation, with synthetic x86 bridge
and device tests for wrong/two-device identity, nesting, failed startup/read,
same-address reuse, failed/reentrant Reset and foreign unwind. Native Windows
and user-controlled FL flight evidence remain unmeasured. A live flight must
show actual successful camera events and same-owner boundary; this static result
cannot supply them or establish FL shader/depth-writer coverage.
