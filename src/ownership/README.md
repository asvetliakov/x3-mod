# Experimental D3D9 ownership boundary

This layer forwards normal D3D9 through canonical COM wrappers. It is deliberately
not connected to `loader.cpp`, the capture proxy, the production CMake target, or
the installed game. It establishes the ownership needed for persistent renderer
resources; it does not substitute scene depth, implement TAA or enable HDR.

`d3d9_ownership.h` exposes:

- `wrap_factory(owned_native, out)`: consumes one owned native factory reference
  only on success. On failure the caller retains it. Ex factories are rejected.
- `borrowed_native_device(wrapped)`: renderer-only access while a caller holds a
  live wrapper reference. It does not add a reference and must not escape into
  application code.
- `retain_renderer_resource(wrapped, owned_resource)`: adopts one owned native
  reference, releasing it before Reset or backend-device destruction. Failure
  leaves ownership with the caller. Known application wrappers are rejected.

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
from an unknown COM pointer.

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
Native HRESULTs, swap-chain enumeration peculiarities, descriptors and resource
contents are preserved unless wrapper allocation itself fails. There is no
descriptor spoofing or scene-resource replacement in this checkpoint.

On Reset, renderer-owned resources are released first. A failed reset keeps the
logical wrapper alive but disables further renderer-resource adoption until a
successful reset. Present/TestCooperativeLevel loss results also retire renderer
resources. This layer does not manufacture successful resets or release the
application's child references.

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

Before connecting this to the game's loader, review the standalone ownership
fixture and its baseline/wrapped results. Actual game compatibility, concurrency
under native multithreaded callers, allocation-failure injection, Ex, and visual
resource substitution remain separate gates.
