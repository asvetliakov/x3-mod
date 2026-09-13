# Bloom invocation owner and wrapper-call ABI

2026-09-13. Derived from the verified X3AP image, existing capture source and
read-only Ghidra 12.1.3 inspection. **No production change or game/Wine run.**
This narrows [the compositor contract](bloom-compositor-skip.md) and provides an
implementation plan; it does not establish live owner observations or native
Windows execution.

## Exact owner at the callsite

At preferred image base `0x00400000`, the call is `0x004721b1 → 0x004c4750`.
The caller registers immediately before it mean:

| Register/value | Derived meaning and evidence |
| --- | --- |
| ECX | Sorted view-pointer array, loaded from caller `[ESP+0x14]` at `0x0047219b`. |
| EDX | Index into that array, loaded from `[ESP+0x1c]` at `0x0047219f`. |
| ESI | Current view, loaded from `[ECX+EDX*4]` at `0x004721a3`; its layer at `+0x29c` is compared with `0x11` at `0x004721a8`. |
| Caller `[ESP+0x13]` | This-frame compositor-done byte. The continuation at `0x004721b6` sets it to one. |

None of ECX/EDX/ESI is the compositor device. The original independently obtains
its owner from game globals:

```text
renderer = read_u32(image_base + 0x208b3c)
record   = read_u32(renderer + 0x18)
device   = read_u32(record)
manager  = read_u32(renderer + 0x1c)
settings = read_u32(image_base + 0x206f34)
glow_on  = (read_u8(settings + 0x100) & 0x80) != 0
```

The first renderer load is `0x004c4781`, record load `0x004c4787`, and device
load into EBP `0x004c47c9`. Subsequent direct D3D calls push EBP as `this`, for
example GetRenderTarget at `0x004c4817`, StretchRect at `0x004c4c8c`,
SetRenderTarget at `0x004c4e3b`, DrawPrimitive at `0x004c4e54`, and the final
SetDepthStencilSurface at `0x004c4f14`.

This owner is not entirely latched inside the original. It reloads renderer at
`0x004c4c4d` for the software-VP branch and at `0x004c4d3a/58/6f` for manager
calls. The viewport helper `0x004c6300` reloads renderer at `0x004c6323`, then
record at `0x004c633e` and device at `0x004c6341` before SetViewport. Therefore
matching only the pre-call device pointer is insufficient to authorize post
work after a renderer/record change.

The inspected base/pure state-manager implementations store their device at
`manager+4`. In particular the pure declaration setter `0x004b5060`, index
setter `0x004b50a0` and stream setter `0x004b51b0` use it for the corresponding
D3D methods. This supplies a useful **consistency check** that manager and record
name the same device; manager identity is not an alternative owner selector.

Direct references to the renderer global show three writers: `0x004d848a` in
`0x004d8470` and `0x004dae6b` in `0x004dac90` publish an allocated renderer;
`0x004dbfb0` in `0x004dbd50` clears it during teardown. This is not a proof of
cross-thread exclusion for renderer members or all indirect writes.

## Caller continuation and stack qualification

Admission must retain the current exact-game identity gate, original call
encoding/displacement, current patch ownership, and quiescent rollback. Extend
the site contract to include the adjacent layer test/branch and continuation:
`0x004721a8` compares `[ESI+0x29c]` with `0x11`; the branch at `0x004721af`
lands at `0x004721bb`; after CALL, `0x004721b6` sets the compositor-done byte;
`0x004721bb` reloads EAX from `[ESI+0x270]`.

The assembly entry must save the actual incoming stack address and read its
return PC before doing injected work. Require that PC to equal the admitted
image's `base+0x721b6`. Check incoming four-byte alignment and retain the actual
continuation in the invocation. Do not substitute a fabricated continuation,
recover it from a C callback's `_ReturnAddress`, or choose a device using ESI.
An unexpected caller gets no replacement ticket and must take a CPU-preserving
original fallback. Whether that fallback uses the existing tail-jump or a
separately qualified wrapper is an integration choice, not permission to skip
original.

A focused control-flow stack audit covers **653 original instructions, 72 CALL
sites (55 indirect), and 110 ESP-relative memory/address operands**. With the
public COM/stdcall argument sizes and disassembled game-helper RET cleanup:

- Every reachable control-flow merge agrees on ESP depth; no merge conflicts.
- Every explicit ESP-relative memory access or address passed outward lies
  between **entry ESP−180 and entry ESP−4**. None names the original return PC
  at entry ESP or any caller argument above it.
- The sole normal RET at `0x004c4f60` has entry ESP restored exactly, with no
  immediate stack cleanup. The function's main local/saved-register baseline
  is entry ESP−180. Its saved SEH predecessor lives at entry ESP−12.
- Incoming EBP is pushed/popped, then EBP is used for the device. The body does
  not traverse an incoming EBP frame chain. Its stack-derived pointers name
  its own locals, not the caller's frame or return PC.

The audit is a local explicit model, with indirect call purges checked against
the actual call argument setup and COM methods, rather than a decompiler
prototype. Ghidra's unannotated stack-depth analysis becomes unknown after the
first indirect COM call; its guessed stack parameters are not proof. The
explicit model reaches all 653 instructions and checks all normal paths,
including the early glow/resource/error returns.

Immediate helper return PCs remain unchanged when a wrapper calls original:
they still lie after the CALLs inside `0x004c4750`. Inspected direct helpers are
`0x004bb0f0` (effect lookup/load), `0x004b8b60` (error handling), `0x00408b60`
(string copy), `0x004b9ed0` (effect texture tracking), `0x00469700` (local name
comparison), `0x004c6300` (viewport), and vector construction/destruction
helpers `0x0050e527/0x0050f2bd`. Their ordinary parameters are passed explicitly
by original. The manager's stream setter really consumes five stdcall words
and returns `RET 0x14`; its decompiler's `unaff_retaddr` label is a wrong stride
label, not evidence of return-address introspection.

The original installs its own SEH registration and uses handler thunk
`0x005304f9`, which passes its function-info pointer to the image's
`__CxxFrameHandler3` at `0x0051305e`. The vector helpers use
`__SEH_prolog4/__SEH_epilog4` at `0x00518998/0x005189dd`; those manipulate the
helpers' own frames and SEH chain. They do not compare the compositor's
caller PC with `0x004721b6`. A new compiler-supported outer SEH frame must remain
a valid predecessor, as qualified synthetically in
[the return-bridge prototype](../architecture/bloom-return-bridge.md).

**Conclusion:** the verified compositor has no implicit stack arguments or
return-PC input; an extra CALL is compatible with its inspected normal ABI.
No inspected game path imposes a special immediate compositor caller PC.
This is not an exhaustive proof about arbitrary installed effects/plugins,
all exceptional CRT paths, or deeper-stack diagnostics in third-party code.
Public D3D/D3DX interfaces provide the supported API boundary; their normal
operation must not require an exact game caller stack. Live game acceptance of
the wrapper remains pending, and no private Wine/backend-layout check is needed
or justified by this remaining acceptance work.

## Concrete lookup and lifetime integration plan

Use a small synchronous owner snapshot containing renderer,
record, device, manager and manager-device. The isolated production helper
`compositor_owner::read` implements that identity lookup with four exact-size
RPM reads; it is not yet wired into capture. Settings/glow diagnostics and
invocation eligibility are separate integration reads/checks, not additional
identity fields or capabilities established by this helper. Read
only after executable/site admission. For this once-per-compositor lookup,
use bounded `ReadProcessMemory(GetCurrentProcess(), …)` reads with exact byte
counts, overflow/null rejection and CPU/LastError transport around the callback.
Do not use a VirtualQuery-then-unprotected-copy sequence for these potentially
retired game pointers. The existing `engine_memory::read` defaults to a cached
direct copy; use an explicit RPM helper here rather than assuming that mode is
always safe for renderer teardown.

Under the capture mutex, perform exactly one `devices.find(snapshot.device)`.
Require an existing, live entry and matching manager-device pointer. First
integration should decline unmatched aliases; it has no verified need for a
canonical-IUnknown fallback. This avoids calling QI on an untrusted game pointer
and avoids scanning all devices. Cache no renderer heap pointer across frames.
Take a second snapshot after pre preparation and before original,
then another after original before post; require the same renderer, record,
manager and exact device. Re-reading detects change, but is not a substitute
for a thread/lifetime contract. Record the successful BeginScene thread in
`Device` and require the invocation to run on that observed scene thread;
unknown/wrong thread declines. This requires one update per BeginScene, not
per draw. The present source has no stored render-thread identity.

`capture.cpp` currently owns contexts in
`map<IDirect3DDevice9*, unique_ptr<Device>>`. `scene_end_signal` broadcasts to
all entries. A recursive mutex permits same-thread device callbacks;
`release_device` can erase that map entry on native Release returning zero.
Consequently neither a raw map pointer nor merely retaining the recursive
mutex pins the context across original.

A concrete option is to make map values `shared_ptr<Device>` and take one
strong **CPU context pin** into the invocation before preparation can reenter
COM. Add a live/retiring flag and an owner-local pointer to the single registered
bloom invocation. Keep the pin until explicit bridge cleanup, including SEH
cleanup. Post requires the same map entry/id and a live, unrevoked invocation;
a retired context may remain allocated but must never call its destroyed
native device. The pin itself must not AddRef the native device: doing that
silently changes the existing final-Release heuristic. One shared-pointer pin
per frame has no new per-draw ownership operation.

The present `release_device` asks `motion_output.device_references()` how many
native device references its owned objects represent, probes native AddRef /
Release, drops resources when only application+owned references remain, and
finally calls native Release. Child destruction reenters the hook;
`MotionOutput::releasing_`/`taa_busy_` suppress existing reference accounting
during those internal releases. New invocation-held surface/state-block/texture
references cannot simply be added to this count: aliases and texture-level
references do not necessarily create one distinct native device reference per
COM AddRef, and the optional ownership wrapper has its own logical children.

The selected integration direction is a CPU context pin plus one explicit native
`AddRef` invocation pin, with actual invocation-induced device references
included in the final-reference accounting. Allocate/reuse the bloom pass under
a measured reference-delta bracket, as the existing TAA integration does, and
measure separately retained invocation references under the same serialization.
Do not count surface/texture aliases as independent device references. The
explicit native pin contributes exactly one; it does not replace accounting
for resources. This policy still requires combined runtime qualification.

Define a capture-wide internal-operation/retirement depth before entering any
injected COM work. Nested parent Release callbacks during that work bypass the
final-reference probe and cannot recursively revoke partially detached state.
A normal nonterminal application or D3DX `GetDevice`/`Release` pair must retain
the ticket. When the qualified probe identifies only application+owned
references (`after == held + 1`, including the invocation pin), atomically mark
retirement and detach the invocation, drop its measured references and the pin,
then release owned pass resources under nested-release suppression before
forwarding the final application Release. Its actual return can then reach zero.

Blind revocation on every device Release is rejected as the default policy:
child destruction and ordinary D3DX reference traffic also enter this hook and
could otherwise suppress bloom every frame. Conversely, adding a native pin
without changing the final-reference accounting would hide teardown. Fixtures
must cover nonterminal traffic, terminal Release, nested child callbacks and
both native/ownership reference models before this policy is integrated.

Before **any** native Reset or ResetEx, including one reentered from the same thread:
mark/reset-generation invalidation first, revoke the registered invocation,
detach and release all of its DEFAULT-pool scene/candidate/main/depth,
saved-state and recovery references, then execute the existing
`MotionOutput::before_reset()` resource/unbind path and native Reset. The
current generation increment is in `after_reset`; it is too late by itself.
The existing 134-slot Ex device table only hooks Reset at slot 16; integration
must also cover ResetEx at slot 132 before admitting that path. Reset failure
also leaves the old invocation revoked. Explicit later bridge
cleanup sees empty references and only releases its CPU pin. The same registered
revoke primitive belongs in resource shutdown/owner retirement.

Avoid a GCC RAII lock or smart-pointer destructor whose only cleanup path is
unwinding through Windows SEH. If the new bridge holds a lock across original,
its owned lock state must be released explicitly by the compiler-supported
finally, as must the CPU pin. Alternatively, release the capture mutex for
original and reacquire it for the admitted post transaction while keeping the
CPU pin and registered revocation pointer. The latter avoids holding a global
lock over original, but still requires synchronization around all invocation
state. Existing device hooks have GCC RAII guards; arbitrary SEH through one
of those guards is a separate lock-unwind qualification concern and is not
fixed by a CPU pin. The main integration should choose and fixture-test the
lock/SEH policy explicitly.

In particular, an unlocked original may still be executing when another thread
releases its final application reference. That thread must not drop the
invocation's native pin or destroy the device. Either exclude that transition
until original exits, or mark retirement and defer the owned-reference cleanup
until the invocation finishes while preserving the native pin. Returning a
still-positive native count in that case reflects the outstanding invocation
reference; never manufacture a zero result or destroy the device early. The
combined lifetime fixture must cover this interleaving before selecting the
unlocked-original option.

Last-device `release_device` currently shuts down `scene_hook` under the
assumption that absence of devices implies a quiescent frame. With an active
wrapper, a CPU invocation may still exist after map erasure. Defer hook teardown
until the outer invocation count is zero; otherwise “last device” is no longer
sufficient evidence of quiescence. Store a pending last-device transition and
drain it from invocation cleanup; the existing one-shot branch cannot do this.
Audit the profiler, chase notification and final resource/crypto reports in
that branch too, and defer any action that assumes quiescence. No post draw
is allowed while waiting. Zero invocations is necessary but not sufficient
for cross-thread patch/module quiescence; retain the install-window contract.

## Local evidence and reproduction

Raw game listings remain untracked in `/tmp/x3-bloom-skip/` and
`/tmp/x3-bloom-owner/`. The latter holds `direct.txt`, `eh-and-manager.txt`,
`stack-depth.txt`, `stack-audit.json` and the small local stack-audit script.
Only derived addresses, counts and findings appear here. The stack JSON was
validated by script assertions, including full instruction reachability, zero
merge conflicts and zero caller-frame operands.

Targeted Ghidra invocations used existing `X3CameraState.java` with `-readOnly
-noanalysis`, the `X3Render` project and these bounded groups:

```text
ins:004bb0f0 ins:004b8b60 ins:00408b60 ins:004b9ed0
ins:00469700 ins:004c6300 ins:0050e527 ins:0050f2bd
range:005304f9:18 range:00472197:13 data:00608b3c
ins:00518998 ins:005189dd ins:0051305e
ins:004c4f70 ins:004c4f90 ins:004b5060 ins:004b50a0 ins:004b51b0
dec:004bae10
```

No fixture build, native Windows run, game launch, COM mutation or production
source change was performed for this research task.
