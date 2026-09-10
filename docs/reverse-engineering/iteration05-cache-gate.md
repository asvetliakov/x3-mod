# Iteration 5: why the requested mesh cache did no work

The completed user run recorded **7,199 adjacency calls and 7,199 cache preflight
rejections**, with zero cache acquisitions, misses or hits. This was an eligibility
failure, not evidence that all mesh inputs were unique. Native adjacency continued
successfully and took 23.518 seconds of aggregate intercepted service time.

Evidence is the immutable completed snapshot
`/tmp/x3-iteration05-completed-snapshot.log`, 216,605,445 bytes, SHA-256
`e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8`.
The earlier running snapshot ended at 5,812 calls and is superseded here.
`verification/results/iteration05-cache-gate-analysis.json` retains final aggregate
counts, exact binary hashes and interpreted address references. Full disassembly
stays in local-only `/tmp` artifacts. Analysis used only
saved files and binaries; it did not attach, inject, change the installed DLL or
launch the game.

## Observed activation

The requested switch was on, the exact native D3DX SHA passed, and both observed
native tables installed all three timing methods. Cache fixed metadata was
allocated (20,696 bytes in this installed build). The final report shows
`dispatch_enabled=1`, `backend_verified=0`, no fault, and no retained entries or
acquired bytes. There is no `mesh_cache_backend` verification record at all.
All calls reached the saved native method. Total preflight time was 21.309 ms,
about 2.960 microseconds per rejection; this does not include native adjacency.

The installed diagnostic schema did not record the rejected branch or mesh options.
Consequently the log alone cannot label every rejection. The following static
path identifies a concrete, deterministic incompatibility in the actual game’s
mesh creation route, consistent with those runtime totals.

## Exact game options and rejection

For X3AP SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
preferred addresses show:

| Address | Meaning |
|---|---|
| `0x004bb6a8` | Initializes mesh creation options to `0x990`. |
| `0x004bb6b2` | Uses `0x991` when the vertex count is greater than 65,535. |
| `0x004bb6cf` | Optional software-processing branch ORs `0x18000`. |
| `0x004bb71d` / `0x004bb727` | Loads then pushes these options as the third argument. |
| `0x004bb72a` | Calls thunk `0x004faf12`, which jumps through IAT `0x00532360`, named `D3DXCreateMesh`. |
| `0x004bb996` | Calls the preparation routine `0x004bc680`. |
| `0x004bc76a` | Calls mesh vtable slot 22, GenerateAdjacency, with approximately `1e-6` epsilon. |

The local SDK’s named bit values decode `0x990` as **SYSTEMMEM (`0x110`) plus
DYNAMIC (`0x880`) for both buffers**. `0x991` also selects 32-bit indices.
The optional values are `0x18990` and `0x18991`. These are not WRITEONLY meshes:
WRITEONLY is a distinct `0x440` mask.

The exact native D3DX constructors at preferred `0x005a25f2` / `0x005a28a9` retain
the supplied options in the mesh’s `+0x214` field. Its GetOptions implementation
at `0x0058c297` reads that field. The installed adapter explicitly rejects
`D3DXMESH_DYNAMIC` immediately after checking SYSTEMMEM and before getting or
verifying the held buffer backends. The core also independently rejects dynamic
options, and the buffer-descriptor gate rejects `D3DUSAGE_DYNAMIC`.

The ordinary initialization path writes and unlocks the vertex buffer at
`0x004bc1f6`/`0x004bc5ea`, then writes and unlocks the index buffer at
`0x004bc0bd`/`0x004bc1ac`, before entering preparation. This matches fully
initialized, unlocked fixture inputs; it does not prove every optional path has
known tracker state.

Thus changing only one mask would leave two other rejection points and would not
establish safe operation. The correct next step is a reviewed **exact
SYSTEMMEM+DYNAMIC readonly acquisition contract**, followed by real native and
wrapped parity tests for all four game option variants. Full current bytes and
options must remain in the cache key. Dynamic use is not permission to reuse a
previous revision or omit buffer acquisition.

## Next bounded change

The uninstalled extension now has a reviewed [exact dynamic readonly contract](mesh-dynamic-contract.md)
and [native/wrapped parity evidence](../verification/mesh-cache-hook.md). It
requires a positive runtime contract for dynamic SYSTEMMEM readonly acquisition;
other pools, write-only/shared buffers, unknown endpoints, unsafe tracking state
and unsupported FP behavior must continue to bypass. Tests include a write
between requests and prove it causes a miss, preserving input/output bytes,
HRESULT, LastError, computational FP, references and actual tracking diagnostics.

The extension also exposes cumulative rejection reasons and one bounded
numeric detail per reason (options, endpoint slot, descriptor pool/usage/size or
tracking status). These are fixed-size diagnostics without per-mesh content.
Core bypass/admission refusals now have reasons too, so an FP or budget limitation
cannot become another opaque zero-hit result after the outer gate is fixed.

No speedup or hit rate follows from this trace. The adjacency total measures
continued native work; it is neither cache savings nor an apples-to-apples
comparison with the earlier run’s different loading sequence.
