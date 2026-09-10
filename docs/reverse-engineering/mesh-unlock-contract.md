# Exact Preview SYSTEMMEM buffer unlock contract

Independent read-only review of the installed 32-bit binaries supports a narrow matched-lock/unlock contract for the proposed opt-in adjacency cache. It does not establish safe recovery for arbitrary COM implementations or third-party hooks. The production adapter must verify the actual buffers held by the mesh, including any recognized ownership forwarding wrapper, and the exact native modules/endpoints. A successful public `Unlock` HRESULT alone is insufficient evidence.

The module digests and selected disassembly are retained in `verification/results/mesh-unlock-contract-review.json`. Preferred image base is `0x10000000`; all addresses below are RVAs. The independent native fixture is `verification/probe/mesh_unlock_probe.cpp` with output `verification/results/mesh-unlock-probe.txt`.

| Installed PE32 module | SHA256 | Relevant endpoints |
|---|---|---|
| d3d9.dll | `58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf` | VB Lock `1f90`, Unlock `2060`; IB Lock `2a70`, Unlock `2b40` |
| wined3d.dll | `f4997bc0465de7e87bac9921bf0274db00ac3b3ba0754fa03f1f33e309a8e863` | buffer unmap `1ca60`; context emit-unmap `37b40`; ST require/submit `395d0`/`39670`; MT require `3c2d0`; ring require `3c530` |

The runtime reports `C:\windows\system32\d3d9.dll` from the Win32 process. Its host counterpart is the bottle's `windows/syswow64/d3d9.dll`, matching CrossOver Preview's `lib/wine/i386-windows/d3d9.dll`. The host `system32` file is PE32+ and has different bytes; do not use that file's digest for this contract.

The critical D3D9 IAT entries are `0x23460` for `wined3d_buffer_get_resource`, `0x235b8` for `wined3d_resource_map`, and `0x235c4` for `wined3d_resource_unmap`. The corresponding WineD3D export RVAs are `0x1ac00`, `0x7b690`, and `0x7b700`. Comparing the live imports to the pinned module exports additionally detects foreign replacement of this dispatch boundary.

The exact native D3DX mesh Unlock methods forward directly to the held VB/IB's slot 12, with no mesh-owned lock bookkeeping. D3D9's verified methods call `wined3d_resource_unmap(resource, 0)` and then unconditionally return zero. WineD3D buffer unmap rejects only nonzero subresource indices; with subresource zero it decrements the map count, performs final buffer unmapping when needed, and returns zero. The map count must describe the cache's successfully acquired locks, with callers serialized and no outstanding foreign mutation.

There is an important intermediate failure path: context emit-unmap requests a 16-byte command and returns `E_OUTOFMEMORY` if its allocator returns null. D3D9 would discard that error. The exact immediate command-stream implementations narrow its reachability:

* The multithreaded application path uses a preallocated 4 MiB ring. Its only null return is an oversized packet, impossible for the 16-byte unmap command. Full queues wait for space; this is not a bounded-latency guarantee.
* Single-threaded command-stream creation allocates a 4096-byte work buffer and fails device creation if allocation fails. Ordinary top-level submission restores its start/end cursor after executing the command. A subsequent top-level 16-byte unmap therefore uses existing storage and cannot reach the growth allocation. Nested backend execution is outside this contract; emit-unmap explicitly asserts if called on the command-stream thread.
* D3D9 resource unmap obtains the device's immediate command stream. The deferred-context allocator's separate failure behavior is not a D3D9 route.

Accordingly, activation is defensible only for live devices and ordinary serialized, non-reentrant application mesh preparation through the verified implementations. Module identity, buffer ownership, native Lock/Unlock endpoints, and the mesh methods returning those buffers must be checked. Recognized wrappers must retain their original forwarding slots and keep tracking intact; call through the wrapper, not through its borrowed native pointer. In-module code patching, corrupted objects, asynchronous destruction, external concurrent locks, backend reentry and process faults are excluded assumptions, not repaired conditions. If the adapter depends on this chain, it must also pin/check the WineD3D module, rather than checking D3D9 alone.

A cache acquisition cleanup failure still means that the contract was violated or an operational fault occurred. Disabling hits does not release a lock. Rejecting this project's hooked preparation methods cannot contain unobserved native/internal calls or guarantee safe continuation; the diagnostic must retain the restart-required scope without claiming repair or universal containment.

The upstream [Wine D3D9 buffer implementation](https://github.com/wine-mirror/wine/blob/master/dlls/d3d9/buffer.c), [WineD3D command stream](https://github.com/wine-mirror/wine/blob/master/dlls/wined3d/cs.c), and [WineD3D buffer implementation](https://github.com/wine-mirror/wine/blob/master/dlls/wined3d/buffer.c) corroborate the function roles. These upstream sources are not claimed to be the matching CrossOver build source; the installed-binary addresses, branches and digests above are the version-specific evidence.
