# Bloom invocation owner and wrapper-call ABI

2026-09-13. Derived from the verified X3AP image, existing capture source and
read-only Ghidra 12.1.3 inspection. The selected capture integration below is
now implemented behind opt-in `X3M_HDR_BLOOM=1`; the combined X3 synthetic-owner
fixture passes as recorded below. The disassembly was read-only and no game was launched.
This narrows [the compositor contract](bloom-compositor-skip.md); live game
owner observations and native Windows execution remain unverified.

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
RPM reads; capture now uses it for admitted pre/post owner qualification. Settings/glow diagnostics and
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
unknown/wrong thread declines. Capture now records this identity once per
successful BeginScene, not per draw, and clears it before Reset/ResetEx.

Before integration, `capture.cpp` owned contexts in
`map<IDirect3DDevice9*, unique_ptr<Device>>`. `scene_end_signal` broadcasts to
all entries. A recursive mutex permits same-thread device callbacks;
`release_device` can erase that map entry on native Release returning zero.
Consequently neither a raw map pointer nor merely retaining the recursive
mutex pinned the context across original.

### Selected integration policy (2026-09-13)

The first live integration uses an **unlocked original and deferred device
retirement**. This replaces the earlier proposal to infer terminal application
ownership while invocation aliases are retained. The policy is implemented in capture and qualified by the combined synthetic
fixture below; live game acceptance remains pending.

1. Change map values to `shared_ptr<Device>`. Under the capture mutex, copy one
   CPU context pin into explicitly constructed invocation storage; take one
   native device `AddRef` through the saved original slot and register the
   invocation before injected COM preparation. One invocation per device is
   admitted; nested entries call original without creating another ticket.
2. Pre and post each serialize with the capture mutex. Release it before
   original. Keep the native pin, CPU pin and registered invocation across
   original. All invocation mutation, including Reset revocation, uses the
   mutex. Post requires the same map entry/id, owner snapshot, frame, Reset
   generation, scene thread and still-registered unrevoked ticket.
3. While an invocation is registered, `release_device` forwards native Release
   **without the owned-resource final-reference heuristic**. The explicit pin
   keeps the native object alive. Ordinary D3DX reference traffic does not revoke
   the ticket. If another thread releases the final application reference,
   Release returns the actual positive count reflecting our outstanding pin;
   destruction waits for cleanup. No count is fabricated, and post admission
   does not claim to prove that an application reference still exists.
4. Cleanup drops every separately retained invocation reference under internal
   release suppression, clears registration, then releases the explicit native
   pin **through `release_device`**. At that point only the persistent renderer
   resources need accounting, so the existing terminal heuristic can release
   those before the pin's final native Release. The CPU pin survives map erasure
   and is explicitly destroyed last. Bridge cleanup runs exactly once and
   handles partial pre and an exception escaping original. Reference detachment
   is idempotent, so Reset revocation followed by final cleanup is safe; the
   callback must not destroy the invocation object a second time.

This avoids measuring invocation alias deltas. An AddRef of an already-owned
surface can produce no native device-count delta, yet keep that object's device
reference alive after another owner releases it during original; a pre-only
measurement is not a general solution. Persistent BloomPass ownership instead
uses its surface-only `references()` contract, qualified together with
MotionOutput under both native and optional ownership reference models.

The combined release probe must be suppressed while **either** component is
performing internal resource operations. In particular,
`MotionOutput::device_references()` returning zero during `releasing_` or
`taa_busy_` must suppress the whole combined probe: adding nonzero bloom
references must not accidentally re-enable it. Expose that busy state explicitly.
Use a bounded internal-operation depth around bloom preparation, reference
cleanup and combined resource destruction; clear member pointers before Release
so nested child callbacks see detached state. The ordinary final-release path
releases both components before native object destruction.

Before **any** native Reset or ResetEx, increment a capture-owned generation and
mark Reset active, revoke the registered candidate, detach and release its
DEFAULT-pool scene/candidate/main/depth references, then release BloomPass
DEFAULT resources and execute `MotionOutput::before_reset()`. The explicit
native pin and CPU pin remain until bridge cleanup; they do not retain DEFAULT
resources. Reset failure leaves the invocation revoked. The 134-slot Ex table
must hook ResetEx at slot 132 as well as Reset at slot 16. Reentrant Reset while
original runs follows this same revocation path. Reentrant Reset from inside
injected GPU work is outside the first integration's supported callback scope;
its policy must not destroy live BloomPass stack-local transactions.

### Lock and exception scope

Do not hold a GCC RAII lock across original. Original game-helper exceptions
escaping after normally completed device hooks reach the compiler-supported
bridge finally with no capture lock outstanding; it explicitly cleans the
registered invocation. A normal pre/post callback releases its own lock before
returning. Cleanup itself must not throw or raise. Continued SEH that resumes
original does not revoke merely because exception search visited the frame.

The supported GPU error model remains HRESULT failures with the existing
BloomPass state restoration/recovery. Arbitrary backend SEH through injected
D3D calls, or through an active GCC device-hook RAII frame, is **not** newly
claimed recoverable. BloomPass also has stack-local SavedState/TextureViews
whose GCC destructors are not Windows-SEH cleanup. Supporting such faults would
require a separate cleanup transport; the bridge does not silently supply one.
The optional replay/admission monitor is not a prerequisite for this policy.

The exact scene-end signal is delivered only after a safe owner and scene
thread are selected. A glow-off or candidate-preparation refusal on that owner
still calls its ordinary MotionOutput scene-end hook, which performs the
resolve/writeback when its existing route state is eligible. Unknown
owner, wrong thread or invalid caller declines injected GPU work and does not
broadcast to other devices; original executes once and the existing StretchRect,
selector and EndScene fallback chain remains available. That fallback can resolve
at a different time and is not an identical exact-boundary signal claim. Bounded
refusal diagnostics distinguish those cases with `ordinary_signal=0`.

### Hook lifetime and focused qualification

Retain the production scene patch and immutable bridge binding for the process
lifetime, including periods with zero devices; an unmatched owner simply takes
original. Remove automatic last-device scene-hook restoration. A callback's
cleanup still runs while bridge active count is one and assembly return code
remains pending, so cleanup is not a valid patch/unbind point. Explicit fixture
shutdown retains actual external quiescence plus active-count checks. Do not
infer module-unload safety from an active count alone. Profiler shutdown and
last-device reports must run with the capture mutex released.

The combined fixture should cover: ordinary nonterminal GetDevice/Release;
final Release from original and another thread; nested child releases during
prepare, Reset and final shutdown; CPU context survival after map erase;
Reset and ResetEx success/failure revoking before native entry while retaining
only the device pin; owner/frame/thread/generation mismatch; nested bridge
admission; normal cleanup and escaping/continued original SEH; and final device
recreation with the scene patch still installed. Qualify persistent reference
accounting with bloom plus TAA/HDR in the native and optional ownership models.
Existing isolated bridge CPU/SEH and BloomPass HRESULT-recovery evidence can be
reused; arbitrary injected-COM crashes are outside that evidence.

## Capture integration evidence (2026-09-13)

The [combined X3 record](../../verification/results/bottle-X3/capture-bloom-x3-summary.json)
binds the explicit seam DLL/EXE and per-mode environments. **714 checks passed: 357 in each
reference setting, 20 scenarios, zero skips.** Runs took 12.10 s and 8.61 s;
these are fixture wall times, not game FPS or per-frame rendering benchmarks.
The bottle was X3, WineArch `arm64`, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`,
under CrossOver Preview. Native Windows was not run.

The seam uses actual capture Release/Reset/ResetEx/cleanup, the production CPU/SEH
bridge and actual BloomPass GPU preparation/commit with nonzero sharpen and an
exact c23 block. It supplies a synthetic retained scene and original function,
not the game's owner globals or selector. Every case observed **8 actual
MotionOutput-held device references**, HDR enabled and TAA configured; actual
motion/HDR shader ownership was combined with the pass's surface/shader
ownership. Lazy TAA histories/resolve objects were not allocated by this fixture;
their existing reference-delta evidence and the host combined busy-state controls
remain relevant. The ownership=1 setting exercises the wrapped ordinary D3D9
route. Genuine Ex devices are explicitly adopted only by the fixture seam;
this does not qualify production CreateDeviceEx enhancement support.

Both settings pass ordinary commit, nonterminal GetDevice/Release, final
application Release from original and from a worker while original waits,
Reset and ResetEx success/failure, an escaping original SEH exception and a
continued original exception. Exception sites follow a normally returned
SetRenderState hook. Every Reset witnesses all invocation DEFAULT references
gone and the explicit native pin still alive before native entry. Terminal
cases keep the native/CPU context alive during original, erase the map during
cleanup and destroy the CPU pin afterward. Normal and continued cases commit;
Reset/escaping cases do not. Existing isolated bridge CPU-state and BloomPass
HRESULT recovery evidence is reused; arbitrary injected-COM SEH is outside
this result.

Reproduce against a root-owned explicit seam DLL, without rebuilding production:

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py --timeout 60 python3 verification/probe/capture_bloom_x3_run.py --dll /absolute/path/to/seam/d3d9.dll
```

The runner builds only its standalone EXE, or accepts `--exe` to retain a built
one, and runs the two reference settings sequentially. The paired parser has
nine host tests. The separate host lifetime fixture compiles extracted current
capture function bodies with scripted COM aliases and real mutex/shared_ptr,
covering reference interleavings, busy suppression and admission changes:
**29 scenarios, 111 checks pass**. It also verifies fresh post-original glow,
pre refusal without a broadcast/pin, and exactly one ordinary null-callback
scene-end signal for a safe-owner glow-off invocation.

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_capture_bloom_lifetime verification.analysis.test_capture_bloom_x3
```

The source performance pass found no new per-draw refcount, allocation or lock
work. A context shared_ptr is copied once per admitted compositor; successful
BeginScene records one thread identity. Invocation scratch is bounded within
512 bytes and BloomPass reuses its surfaces/programs. Qualification makes three
four-read owner snapshots per admitted frame, with fresh glow checks; diagnostics
log first events and a 300-compositor summary. No rendering FPS claim follows
from these checks.

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
