# Experimental D3D9 ownership boundary

This layer forwards normal D3D9 through canonical COM wrappers. The loader offers
it through the process-local `X3M_OWNERSHIP=1` opt-in; default behavior stays on the
native capture path. `X3M_SAMPLEABLE_DEPTH=1` additionally requests experimental
automatic-depth substitution only when ownership is enabled. Ownership establishes
safe persistent resource lifetime; neither mode implements TAA or HDR.

**Automatic-depth substitution is not ready for game enablement.** Numeric depth
sampling works, but depth copies to/from ordinary D24X8 surfaces are incompatible
with the physical INTZ format on this backend. The limits below remain gates.

`d3d9_ownership.h` exposes:

- `wrap_factory(owned_native, out, options = {})`: consumes one owned native factory reference
  only on success. On failure the caller retains it. Ex factories are rejected.
  `Options::sampleable_auto_depth` defaults false and is copied into created
  devices before their first application clear/draw. Rewrapping a live factory
  with conflicting options fails rather than changing existing policy.
- `borrowed_native_device(wrapped)`: renderer-only access while a caller holds a
  live wrapper reference. It does not add a reference and must not escape into
  application code.
- `retain_renderer_resource(wrapped, owned_resource)`: adopts one owned native
  reference, releasing it before Reset or backend-device destruction. Failure
  leaves ownership with the caller. Known application wrappers are rejected.
- `get_depth_view(wrapped, out)`: exposes a renderer-only borrowed INTZ texture
  and its allocation generation, clear epoch, logical descriptor, status and
  available/bound flags. Valid wrappers return `S_OK`; consumers must inspect
  both `status` and `available`. Sampling requires unbinding the depth target.

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
With depth substitution off, native HRESULTs, swap-chain enumeration
peculiarities, descriptors and resource contents are preserved unless wrapper
allocation itself fails. The enabled substitution path has the limits below;
there is no blanket enabled-mode API-equivalence claim.

On Reset, renderer-owned resources are released first. A failed reset keeps the
logical wrapper alive but disables further renderer-resource adoption until a
successful reset. Present/TestCooperativeLevel loss results also retire renderer
resources. This layer does not manufacture successful resets or release the
application's child references.

## Experimental automatic depth substitution

The new station trace establishes repeated clears of a single automatically
allocated D24X8 scene surface. The opt-in targets only automatic D24X8,
single-sample, default-pool surfaces matching the initial color target dimensions.
D16, stencil formats, lockable formats, multisampling, no-auto-depth devices and
Ex remain ineligible. INTZ format/depth-color matching and actual allocation/bind
must succeed. Native `GetRenderState(STENCILENABLE)` must work even on a pure
device; otherwise the feature declines before application rendering starts.

The device keeps only native references to the original automatic surface and
the private INTZ texture/surface. `GetDepthStencilSurface` maps the physical INTZ
result back to the original before normal canonical wrapping. Setting that
logical original routes to INTZ; null and unrelated surfaces retain their native
meaning. The real original supplies its descriptor, private data and container
semantics, with no hidden INTZ texture escaping through application getters.
Native state blocks still control their normal state; no logical depth-binding
state is added to them.

Every successful Z clear while INTZ is bound advances `clear_epoch`, including
partial clears. Allocation creation/retirement advances `generation`. These are
resource/content-interval markers, not proof that a full scene was captured.
The source trace clears the same scene depth before Present, so a consumer must
sample/copy the needed epoch before the next destructive clear. This layer does
not select that scene boundary or make a depth snapshot itself.

Before Reset or normal teardown, the implementation restores the original native
binding when INTZ is bound, then releases all substitution references. A still
externally held original surface continues to make native Reset fail. Successful
reset allocates a fresh generation before returning; selection failure leaves the
original allocation authoritative for that whole generation. No allocation retry
or stale-original fallback occurs during rendering. During already-known loss,
only owned references are released, without calling otherwise-invalid Get/Set
methods. A lost getter for a formerly active mapping returns `D3DERR_DEVICELOST`
rather than exposing a backend-retained INTZ surface; held original wrappers keep
their own identity and metadata.

INTZ has physical stencil bits while logical D24X8 does not. Initialization
measures a stencil-only Clear on the still-bound original before any application
rendering, touching no color or Z data. It records whether this backend accepts
the no-op or rejects it. Active clears reproduce that decision and do not clear
INTZ's physical stencil. On Preview the original accepts the stencil-only clear.
Draws with live stencil enabled temporarily disable it, render and restore the
native value. A failed bound-state query/stencil guard latches failure status and
does not draw with unintended physical stencil; reset is required to recover.

GPU-content calls (`StretchRect`, `UpdateSurface`, `GetRenderTargetData`,
`GetFrontBufferData`, `ColorFill`) map the selected logical depth surface to its
physical INTZ representation so they never read or write stale original content.
**This does not make INTZ/D24X8 copies compatible.** Preview accepts full-surface
ordinary D24X8-to-D24X8 depth copies but rejects INTZ-to-D24X8 and the reverse.
The wrapper forwards that physical failure and does not claim enabled-mode parity.
A verified original-depth resolve route such as RESZ is under investigation and
could remove the need to substitute the application's depth surface at all.

Another unresolved case is drawing during `BeginStateBlock`/`EndStateBlock`
recording with live stencil enabled. Native Wine records SetRenderState changes
without replacing the current live state; a temporary draw mask can therefore
affect recorded state instead of the draw. Completed-state-block Apply behavior
does not prove this case. Resolve it or avoid substitution before game enablement;
no shadow state-block model is claimed here. These limitations are separate from
the verified ordinary ownership, reset and numeric sample tests.

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

Review standalone ownership, actual-DLL integration and auto-depth fixture results
before opting into a game test. Actual game compatibility, concurrent lifecycle
calls, the depth-copy/recording limitations above, Ex and visual acceptance remain
separate gates. Generated build products and raw runtime captures stay untracked.
