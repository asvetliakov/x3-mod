# Experimental D3D9 ownership boundary

This layer forwards normal D3D9 through canonical COM wrappers. The loader offers
it through the process-local `X3M_OWNERSHIP=1` opt-in; default behavior stays on the
native capture path. `X3M_DEPTH_COPY=1` additionally requests private automatic-depth
snapshot storage when ownership is enabled. The original application depth target
remains unchanged. Ownership establishes safe persistent resource lifetime; neither
mode implements TAA or HDR.

`d3d9_ownership.h` exposes:

- `wrap_factory(owned_native, out, options = {})`: consumes one owned native factory reference
  only on success. On failure the caller retains it. Ex factories are rejected.
  `Options::capture_auto_depth` defaults false and is copied into created devices
  before their first application clear/draw. Rewrapping a live factory
  with conflicting options fails rather than changing existing policy.
- `borrowed_native_device(wrapped)`: renderer-only access while a caller holds a
  live wrapper reference. It does not add a reference and must not escape into
  application code.
- `retain_renderer_resource(wrapped, owned_resource)`: adopts one owned native
  reference, releasing it before Reset or backend-device destruction. Failure
  leaves ownership with the caller. Known application wrappers are rejected.
- `copy_auto_depth(wrapped)`: explicitly snapshots the bound original automatic
  depth surface into a private native D24X8 texture when capture mode is enabled.
- `get_copy_depth_view(wrapped, out)`: returns that borrowed snapshot, generation,
  source/copy epochs, validity, availability, binding and last-operation status.
  Native D24X8 comparison sampling requires a separate numeric decode.

The caller of the renderer seam must pass a valid native object created for that
borrowed backend device. It must restore all application-visible bindings before
returning from injected work. This API is an ownership seam, not a draw-state
manager or cross-device resource validator. The latter remains caller responsibility.

## Ownership rules

Each wrapper owns one native interface reference. Its logical application count
can exceed one without incrementing native references. A live device wrapper owns
a logical factory reference; each live child wrapper owns a logical device
reference. Native mod resources retain only the native device, so they cannot
keep the logical wrapper count above zero.

The registries are weak bookkeeping for wrappers with positive application
reference counts. On the final child wrapper release, it is unregistered, its
native reference is released, the wrapper is deleted and its logical parent
reference is released last. Native binding/state-block retention may keep the
backend child alive after that. A later getter wraps that retained backend object
again. A previously released pointer is not valid for identity comparison; while
any application reference survives, repeated queries/getters return the same
canonical wrapper.

This is simpler than retaining zero-external-reference wrapper objects in device
state: no wrapper-owned shadow binding or state-block graph is needed, and no
registry entry retains GPU storage. All native binding and state-block behavior
continues to execute in D3D9. Failed reset with an application-held default-pool
resource still fails in the backend.

Device teardown releases renderer-owned references before releasing its native
root reference. Backend Release calls run without the registry mutex held;
parent wrappers remain alive until child backend cleanup completes. Getter
adoption consumes redundant native references when a canonical wrapper already
exists. The implementation never uses RTTI or reads a guessed wrapper layout
from an unknown COM pointer. The child's final parent reference is released via
the parent's application vtable, so installed capture hooks observe that final
device/factory release rather than retaining stale pointer contexts.

## Interface and forwarding scope

Fifteen concrete interfaces implement all 297 methods declared for those normal
D3D9 interfaces in the local MinGW SDK: factory, device, 2D/cube/volume textures,
surface, volume, vertex/index buffers, vertex declaration, vertex/pixel shaders,
state block, query and swap chain. Resource/base-texture QueryInterface aliases
return the canonical concrete wrapper. Texture levels, volume levels, containers,
backbuffers, device and factory getters cross the same identity boundary.

Known application input interfaces are mapped to their native counterpart before
forwarding. Foreign native inputs pass through unchanged so the backend performs
its usual validation. Every known COM interface output is wrapped. Unknown QI
interfaces fail closed and never return backend objects. Ex factory/device paths
are rejected; Ex forwarding is not implemented.

Null output pointers are passed to the original API rather than universally
rejected: for example, `CreateQuery(type, nullptr)` remains a capability query.
Interface-output temporaries start with a private marker, never the caller's
possibly uninitialized slot. If a native failure leaves that temporary untouched,
the wrapper leaves the caller's slot untouched too; if the backend clears it, the
wrapper clears it. The marker is never dereferenced or released. This distinction
matters on Preview: an invalid `GetRenderTarget` index preserves its output while
several failed creation/getter calls clear theirs.
Application depth, draw and surface APIs use ordinary native forwarding; no depth
format substitution, stencil calibration, draw masking or surface aliasing occurs.
Native HRESULTs, swap-chain enumeration peculiarities and descriptors are preserved
unless wrapper allocation itself fails. Explicit snapshot operations have their own
status and do not replace application method results.

On Reset, renderer-owned resources are released first. A failed reset keeps the
logical wrapper alive but disables further renderer-resource adoption until a
successful reset. Present/TestCooperativeLevel loss results also retire renderer
resources. This layer does not manufacture successful resets or release the
application's child references.

## Original-preserving automatic depth snapshots

`Options::capture_auto_depth` targets an automatic, single-sample, default-pool
D24X8 surface. It retains native references to that original surface and a private
D24X8 depth texture of equal dimensions. It never replaces the application's depth
surface, clears it during setup, or changes its format. Application descriptors,
containers, private data, depth copies and stencil behavior therefore continue to
use the actual original allocation. Unsupported source formats, multisampling,
missing RESZ/D24X8 texture support, unsupported required getters or allocation
failure leave normal rendering intact and report capture unavailable.

The renderer explicitly calls `copy_auto_depth` before the depth interval it needs
is overwritten. The original must currently be bound. The helper saves texture
stage 0 and POINTSIZE, binds the private destination texture, writes RESZ trigger
`0x7fa05000` to POINTSIZE, then restores both values. It performs no dummy draw,
BeginScene/EndScene or target switch. Native D24X8-to-D24X8 copies through this
sequence were numerically verified on the installed Preview backend both outside
and inside an existing scene, including pure-device getters and preservation after
the original surface is cleared. Capability acceptance alone is not that proof.
The private texture uses comparison sampling on this backend; reading its red
channel as raw depth is incorrect. Numeric decoding is a separate renderer step.

Allocation/retirement changes `generation`. Successful Z clears of the original
source advance `source_epoch`, including partial clears. Successful copies store
that value in `copy_epoch` and set `copy_valid`. A later source clear preserves the
saved snapshot and its epoch. These markers describe storage and clear intervals;
they do not select a gameplay scene boundary or prove that its geometry is complete.
Ordinary preflight rejection (wrong/unbound source or state-block recording) keeps
an earlier valid snapshot. Once copy-state mutation begins, validity stays false
unless the trigger and both restorations succeed. Callers inspect `status` as well
as `available` and `copy_valid`; a recognized wrapper alone returns `S_OK` from the
view accessor even when capture is unavailable.

Wrapped BeginStateBlock/EndStateBlock results update the recording guard only on
native success. Copying during recording is rejected before mutation because
state setters would otherwise record commands instead of restoring live state.
Renderer code must not bypass this guard by recording directly through the borrowed
native device. Rendering, snapshot use and Reset must be serialized by the caller.
The snapshot is borrowed without AddRef and is invalid after Reset, observed loss,
or final logical device release. Saved temporary native references are released
before loss retirement. Any observed DEVICELOST/DEVICENOTRESET from initialization,
stateblock operations, copy preflight, mutation or restoration retires the snapshot
and renderer resources; loss takes
precedence over an earlier ordinary mutation error. After known loss the helper
issues no further ordinary state setters. Reset retires all private references
before forwarding, and successful Reset prepares new storage before returning.
An application-held original surface still causes the backend's usual failed
Reset, followed by recovery when that reference is released.

This layer supplies storage and an explicit copy operation. It does not choose a
capture hook, automatically copy every clear, decode depth, or implement temporal
history/reprojection. Game enablement requires those separate integration checks.

## Retired substitution experiment

Commit `22146a1` preserves the earlier INTZ automatic-depth substitution experiment
and its verification history. That path was removed from current production code:
INTZ-to-D24X8 copies failed where ordinary D24X8 copies succeeded, and scoped stencil
masking did not cover draws during state-block recording. Original-preserving RESZ
snapshots avoid both changes to application behavior. Old substitution reports are
historical evidence, not tests of the current API.

## Generated forwarding code

The SDK-derived method declarations and forwarding bodies are checked in as
`d3d9_classes_inc.h` and `d3d9_forwarders_inc.h`. The generator discovers the
MinGW SDK relative to the compiler include directory, or accepts `--header`:

```sh
python3 tools/ownership/generate_d3d9_forwarders.py
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror \
  -c src/ownership/d3d9_ownership.cpp -o /tmp/x3-d3d9-ownership.o
```

Handwritten code owns QueryInterface, lifetime, factory/device parents,
containers, reset/loss and renderer resource retirement. The generator routes
every typed interface output through adoption and every interface input through
unwrapping. Building does not require running the generator. Verification lives
under `verification/`; SDK ABI overrides and concrete class instantiation provide
compile-time coverage for missing or mismatched methods.

Review standalone ownership, actual-DLL integration, copied-depth numeric and
loss-injection fixture results before opting into a game test. Actual game
compatibility, concurrent lifecycle calls, snapshot boundary selection, Ex and
visual acceptance remain separate gates. Generated build products and raw runtime captures stay untracked.
