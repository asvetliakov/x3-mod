# D3D9 resource ownership before persistent depth and temporal history

Checkpoint update: the [canonical ownership layer](../../src/ownership/README.md)
is now implemented and has passed [actual-DLL integration](../../verification/probe/ownership_integration.md)
in an opt-in, uninstalled 0.4 build. It uses weak registries for live wrappers
and lets native bindings/stateblocks retain native resources. The original
capture-only hazard below still explains why this ownership boundary is needed.
The later [automatic-depth experiment](../verification/auto-depth.md) was removed
after exposing depth-copy and stateblock-recording compatibility problems. Its
replacement [copies the original D24X8](../verification/copied-depth.md) without
substituting it; borrowed snapshots and renderer resources retire before Reset,
final release, and observed device loss. It is not enabled in the game.
The remaining text preserves the design and acceptance requirements that led to
this work; it is not a claim that substitution has passed all of them.

Investigated 2026-09-10. **Do not add persistent COM resources to the current
`Device` capture context and destroy them only after native `Release()` returns
zero.** This creates an ownership cycle on the actual Preview backend. Attaching
the same resources to a bound game surface through COM private data is not a
general solution either.

The next production choice is a canonical COM ownership layer with separate
application and backend ownership, verified before enabling persistent INTZ/FP16
substitution. The existing native-pointer capture path remains useful while that
layer is built. This is a design decision and baseline evidence, not an implemented
ownership layer or game depth integration.

## Current proxy and observed hazard

`src/proxy/capture.cpp` copies native vtables and preserves native object pointers.
Its `release_device` calls the backend and erases the context only when the result
is zero. `capture_state.cpp` stores POD IDs in private data; these do not own COM
objects and do not introduce the cycle discussed here.

Adding a context-owned texture creates this graph:

```text
capture context --owns--> native texture --owns--> native device
       ^                                           |
       +---- context erased only at device zero ---+
```

`verification/probe/resource_lifetime.cpp` reproduces this without rendering or
launching X3. It creates a hidden 64×64 pure/hardware D3D9 device in the Preview
Steam bottle. `verification/results/resource-lifetime.txt` records zero failures:

| Experiment | Actual result |
| --- | --- |
| Hold one FP16 default-pool texture, release application's device reference | Device `Release` returns **1**, not zero |
| Get device through live texture, release texture, release recovered device | Same device/IUnknown identity; final device reference reaches **0** |
| Bind D24X8 surface with private-data `IUnknown` owning FP16 texture; release application surface reference | Surface `Release` returns **0**, but private owner remains alive |
| Drop local texture reference, leaving private owner as its sole owner; release application's device reference | Device still returns **1**; private owner remains alive |
| Recover device through the still-owned texture, unbind source depth surface, release recovered device | Private owner destroyed; device and factory both reach **0** |

The second case demonstrates a less obvious cycle:

```text
native device --bound state--> source depth backend resource
       ^                                |
       |                                v
       +---- history texture <-- private-data owner
```

COM reference count zero on the source surface did not mean its private-data
destructor ran. Backend binding retained the resource. The fixture recovers via a
borrowed pointer whose sole private-data owner is verified alive; this is a
single-threaded recovery mechanism for the deliberately constructed test, not a
proposed production weak-pointer registry. No leaked test device remains.

The related [INTZ sampling probe](../verification/depth-sampling.md) already proves
that independently allocated INTZ can be rendered/sampled with RGBA8 and FP16
targets and recreated after reset. It does not solve ownership of persistent
replacement surfaces or temporal history.

## Primary-source cross-check

Microsoft documents that `GetDevice` adds a device reference and that
`GetDepthStencilSurface` adds a surface reference. All successful getter paths
must balance those references. [Resource GetDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dresource9-getdevice),
[GetDepthStencilSurface](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-getdepthstencilsurface).

Microsoft describes `D3DSPD_IUNKNOWN` as owning COM private data, with release on
private-data replacement/removal/destruction. This supplies a destruction
notification, not an independent device-lifetime boundary.
[SetPrivateData](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dresource9-setprivatedata).

Upstream Wine's surface implementation distinguishes the public resource
reference from backend texture/view references. Its surface `Release` can reach
zero while backend binding retains storage; texture subresource surface references
also forward to the texture. Texture teardown releases its parent device. These
sources explain the observed shape but are **not** asserted to be the exact
CrossOver Preview build. The fixture above is the local-runtime evidence.
[Wine surface.c](https://github.com/wine-mirror/wine/blob/913e31f201d344223bdf3d13a50a41af35893d12/dlls/d3d9/surface.c),
[Wine texture.c](https://github.com/wine-mirror/wine/blob/913e31f201d344223bdf3d13a50a41af35893d12/dlls/d3d9/texture.c).

ReShade uses a device proxy with its own `_ref`, distinct from its backend device
reference count. At proxy teardown it destroys owned state before the final
backend release; it also accounts for resource callbacks that can occur during
that release. Its reset path tears down owned runtime resources before forwarding
reset. This is useful precedent for separate ownership domains, not a guarantee
that copying its `Release` method alone works with our native-vtable architecture.
[ReShade d3d9_device.cpp](https://github.com/crosire/reshade/blob/358c345ca2fe64f86e67c694f8379c356627adcb/source/d3d9/d3d9_device.cpp).

## Options and decision

| Approach | Assessment |
| --- | --- |
| Persistent native resources in current capture context; cleanup after backend zero | Rejected: first fixture proves cleanup cannot become reachable |
| Private-data owner on game resource | Useful for IDs/notifications; rejected as sole lifetime owner for persistent GPU resources: second fixture proves bound-source cycle |
| Persistent cache cleared before every unclassified native device `Release` | Can avoid the basic cycle if cleanup is reentrancy-safe, but game resource destruction/getter releases can clear it between draws. Invalidating substituted scene depth then changes depth semantics. Not the final substitution/history architecture |
| Scoped native allocations released before returning from one intercepted call | Suitable for bounded diagnostics with complete state restoration. Does not retain temporal history and does not satisfy TAA |
| Canonical application-facing COM wrappers with separate backend ownership | Selected for persistent substitution/history. More interface coverage, but an explicit teardown boundary independent of mod-owned backend references |

Do not derive application ownership by subtracting guessed resource counts from
native `Release` results, inspecting the caller address, or assuming all backend
references pass through our patched vtable. Microsoft's reference-count return is
intended for testing, not a portable ownership algorithm.
[IUnknown::Release](https://learn.microsoft.com/en-us/windows/win32/api/unknwn/nf-unknwn-iunknown-release).

## Concrete ownership model

Use a canonical wrapper registry per backend device and distinguish **external**
references from **internal binding/cache** references. Implement and verify this
model before turning on any visual replacement:

1. A device wrapper owns one backend device root reference. Application
   `AddRef`/`Release` manipulate its logical reference count. Native mod resources
   are created through the backend interface and do not acquire logical wrapper
   references.
2. Each externally referenced child wrapper retains one logical device reference.
   Thus a caller may release its original device pointer and later recover the
   same live device wrapper through the child's `GetDevice`. A child transition
   from no external references to one reacquires that logical parent reference.
3. Device state, wrapper registries and mod caches may retain child wrappers
   internally without adding logical device references. This avoids a new
   `device wrapper -> bound child wrapper -> device wrapper` cycle. These internal
   holds are explicitly drained during reset/teardown. They may own native child
   references; those disappear before the backend root reference is released.
4. On the last logical device release, mark the context as tearing down, prevent
   new mod work, drain mod resources and internal binding/cache ownership, release
   backend state/resources, then release the backend root. Keep CPU callback
   bookkeeping valid until all synchronous teardown callbacks return; free it
   last. Never free a context from an inner resource-release callback while an
   outer cleanup still uses it.
5. The canonical registry is bookkeeping, not an extra external owner. Separate
   refcount operations from registry locks so reentrant backend callbacks cannot
   deadlock or free a wrapper twice. Invalid application lifetime usage is not
   repaired by guessing at native reference totals.

This preserves **application-visible COM identity**, not numerical equality with
the private backend pointer. Every `IUnknown` query for the same logical object
returns its canonical wrapper identity. Device queries through textures, surfaces,
buffers, shaders, declarations, state blocks, queries and swap chains must return
the same device wrapper. Factory `GetDirect3D` and texture `GetSurfaceLevel` /
surface `GetContainer` paths must also return the proper canonical objects.

Wrapping only the device while letting native `resource->GetDevice()` escape is
insufficient. Cover all exposed 9/9Ex interfaces and creation/getter paths before
switching an application device onto the wrapper mode; keep unsupported device
modes entirely on the capture-only path. Unknown interface handling must not
silently return an interface whose identity or lifetime bypasses the registry.

## Depth substitution semantics

The first eligible source should be an explicitly identified, single-sample,
non-lockable D24X8 scene surface. Do not select every depth surface of a matching
resolution, and do not silently extend this to auto-depth, MSAA, stencil-using or
lockable surfaces.

Its app-facing surface wrapper owns the logical source descriptor and its backend
representation. A verified eligible allocation may use an INTZ texture's level-0
surface internally. Keep backend texture/surface references in a defined bundle;
account for their shared lifetime rather than assuming each owns an independent
device reference. Store metadata on the wrapper, not a resource-owned object that
also strongly references that same source.

- `SetDepthStencilSurface(sourceWrapper)` unwraps to the selected backend surface.
  Maintain a logical bound-source handle without an external device-owning cycle.
- `GetDepthStencilSurface` returns that **same logical source wrapper** with one
  external reference, and preserves normal null/error behavior. It never exposes
  the private INTZ surface or texture.
- `GetDesc` returns the source's logical D24X8 descriptor, including dimensions,
  usage, pool and multisample fields; private diagnostics inspect the physical
  INTZ descriptor separately. `GetDesc` must validate the output argument.
  [Surface GetDesc](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dsurface9-getdesc).
- `GetContainer`, private data and supported interfaces reproduce the logical
  source's semantics; a standalone depth surface must not unexpectedly reveal
  an INTZ texture container. Depth-consuming/copy/lock paths must operate on the
  selected representation or make that source ineligible before replacement.
- During sampling, unbind the actual depth target; save/restore logical and
  physical target/viewport/state explicitly. A state block alone is insufficient
  for all target state. Internal operations must bypass app-facing logical hooks
  without hiding actual backend errors.
- Decide allocation fallback before consuming the source's first relevant clear
  or draw. If replacement fails, use the original allocation consistently.
  Never switch mid-frame to a stale original depth buffer. History invalidation
  is safe; losing the active scene's depth content is not.

FP16 source replacement later needs the same descriptor/identity treatment plus
all texture/copy/gamma semantics. Temporal history textures are mod-only backend
resources owned by the device wrapper's renderer context; they never appear in
application getters or retain logical parent references.

## Reset, loss and recreation

Before forwarding `Reset`, stop injected work, invalidate temporal history, release
mod-owned default-pool resources and internal state blocks, and drop internal
holds on reset-sensitive child wrappers. Do not release references owned by the
application or disguise a reset that should fail because it retained a required
resource. Preserve the presentation-parameter in/out contract.

After a failed reset, do not query arbitrary state or create replacement resources.
Stay disabled until a later successful reset; recreation is lazy from the new
dimensions/format and history starts invalid. Keep reset on the device's creation
thread and protect against message-loop reentry. Microsoft requires default-pool
resource release before reset and restricts operations after failure.
[Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset).

On device-loss detection, invalidate history and enter the same resource-release
path. A successful resource creation call while lost does not prove working GPU
storage; the runtime may supply dummy allocations. Treat 9Ex lifecycle separately
before enabling features on Ex devices.
[Lost Devices](https://learn.microsoft.com/en-us/windows/win32/direct3d9/lost-devices).

## Acceptance gate for the ownership checkpoint

Before gameplay installation, a standalone wrapper fixture must establish:

- Device-first and resource-first release orders, bound resources at external zero,
  private-data destruction callbacks and no remaining mod/device ownership.
- Canonical `IUnknown`, all child `GetDevice` paths, repeated getters returning
  one identity, texture/surface container round trips, implicit and additional
  swap chains, state-block-held resources, and supported 9Ex queries.
- The application's original D24X8 descriptor/container identity before/after
  replacement and reset; actual INTZ data still passes the numeric sampling test.
- Failed reset with an externally retained default-pool resource, successful retry
  after releasing it, resized recreation, invalid history after every loss/reset,
  and unchanged backend failure codes when injection is disabled.
- Complete target/state restoration, partial-allocation failure rollback and
  teardown callbacks that reenter reference operations safely.

Only then install the reversible feature build and coordinate a user-loaded game
scene. Passing the current baseline fixture does not prove this future wrapper
layer correct; it establishes why that layer needs these tests.

## Reproduce the baseline lifetime fixture

From the repository root:

```sh
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static \
  verification/probe/resource_lifetime.cpp \
  -o verification/probe/build/resource_lifetime.exe -luser32 -ldxguid
```

Run with a bounded Python invocation (the recorded run uses a 60-second limit):

```python
from pathlib import Path
import subprocess
root = Path.cwd()
binary = root / "verification/probe/build/resource_lifetime.exe"
with (root / "verification/results/resource-lifetime.txt").open("w") as out, \
     (root / "verification/results/resource-lifetime-wine.log").open("w") as err:
    run = subprocess.run([
        "/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine",
        "--bottle", "Steam", "--no-update", "--workdir", str(binary.parent), str(binary)
    ], stdout=out, stderr=err, timeout=60)
    assert run.returncode == 0
```

The probe has no draw or present calls and closes its hidden window. The only new
runtime actions in this investigation were these bounded lifetime experiments.
