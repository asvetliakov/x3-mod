# Experimental canonical D3D9 ownership verification

This fixture links `src/ownership/d3d9_ownership.cpp` directly into a standalone
Windows executable. It does not replace the installed proxy, alter CMake, launch
X3, or edit a bottle. Run only through **CrossOver Preview.app**, Steam bottle.

```sh
sh verification/probe/build_ownership.sh
python3 verification/probe/run_ownership.py
python3 verification/probe/verify_ownership.py
```

The build produces separate baseline and wrapped executables, with SDK warnings
promoted to errors and static MinGW runtime linkage. The runner copies each into
an isolated verification directory and records executable, source and generated
header hashes in `verification/results/ownership-build-verification.json`.

## Contracts exercised

- Normal and pure devices, sequential lifetime, factory/device canonical
  IUnknown and parent identity. A factory is recovered from a device after the
  caller releases its own factory reference.
- Texture/base/resource QueryInterface identity; texture surface, cube face and
  volume GetContainer; all their child GetDevice identities.
- Bound resources whose caller references reach zero, followed by valid getter
  reacquisition: textures, vertex/index buffers, render targets, depth surfaces,
  pixel shaders and vertex declarations. Canonical identity is checked while
  getter references overlap, including a held surface's texture parent.
- Stateblock retention and restoration after unbinding and dropping caller
  references; implicit/additional swapchains and their backbuffer containers.
- Event query identity, Issue/GetData, and the legitimate null-output
  `CreateQuery(EVENT, nullptr)` capability query.
- Retained DEFAULT texture makes Reset fail; releasing it permits retry.
- Renderer-only native FP16 history uses a private-data IUnknown marker to
  observe destruction. History is released before Reset and final logical device
  teardown, and stays alive while a public child still retains the device.
  Failed Reset disables renderer adoption without consuming the caller's
  reference; successful retry enables it again. A separate marker on the implicit
  backbuffer verifies actual native backend teardown after final logical release,
  without retaining a resource/device reference.
- Renderer seam rejects null/foreign wrapper lookups and application wrappers
  passed as renderer resources. Factory adoption failure preserves caller
  ownership. A real native Ex factory is rejected without consuming its ref.
- Exact backend HRESULT and output mutation for invalid CreateTexture
  dimensions/format, GetTexture stage, GetRenderTarget/GetSwapChain index and
  unsupported GetContainer IID, and invalid GetStreamSource with pointer and
  offset/stride sentinels. Output sentinels are valid separately held
  objects; failed output slots are never dereferenced or blindly released.

A wrapper can be destroyed after all its public references disappear and
re-created by a later getter while the backend binding/stateblock still retains
the native object. The fixture does not compare an expired wrapper address to a
new one or impose native numeric reference counts on the wrapper's logical
ownership model.

## Measured Preview backend behavior

The baseline observed:

- A retained DEFAULT resource causes Reset to return `0x8876086c`; retry after
  release succeeds.
- Implicit and additional backbuffers return their owning swapchain through
  GetContainer.
- CreateAdditionalSwapChain succeeds, but GetNumberOfSwapChains remains one and
  GetSwapChain(1) returns `0x8876086c` on this runtime. The wrapper must preserve
  this observation, rather than substitute a different enumeration behavior.
- GetTexture(200) returns success with null output on this runtime.
- Invalid CreateTexture dimensions/format and GetSwapChain index clear their
  output. Unsupported GetContainer clears its output. Invalid GetRenderTarget
  index returns `0x8876086c` while preserving the caller's existing output value.
  Invalid GetStreamSource preserves the pointer, offset and stride sentinels.

The last case exposed an actual discrepancy in the first wrapper draft: its
shared output helper cleared output unconditionally on failure. The verifier
reported a mismatch even though the lifetime checks passed. After correcting
all generated and handwritten object-output paths with a private untouched-slot
marker, the complete baseline/wrapped comparison passed. Individual CHECK counts
alone are not sufficient evidence of matching backend behavior.

## Final recorded result

The final run passed **370 baseline checks and 431 wrapped checks**, with all
shared HRESULTs and backend output/Reset/swapchain observations equal. This
includes actual backend destruction, FP16 renderer history cleanup, failed Reset
recovery, Ex rejection and output-preserving invalid calls. Both executables
built cleanly with `-Werror`.

The tested ownership implementation SHA-256 is
`485281da1a7be9533684de296d65f73d0cde0cdb3e0c44afeb63789db35f4497`.
The manifest also records the public header, both generated `.h` files,
executables and report hashes. See `verification/results/ownership-verification.json`
and `ownership-build-verification.json`, alongside the baseline/wrapped text
reports. These are standalone results; the layer remains outside the installed
capture proxy.

## Limits

This is a synthetic, single-threaded, normal-D3D9 ownership fixture, with explicit
rejection of Ex adoption. It is not a gameplay/image-equivalence test, a complete
invalid-argument conformance suite, a threaded stress test, or proof that every
backend-specific interface can be supported. Native pointer access stays inside
the renderer-only fixture seam. Renderer resources are unbound before app calls;
retaining injected state/bindings across game calls is outside this test.
