# Pinned Preview managed-buffer write mapping

Read-only investigation, 2026-09-11. The installed x86 Preview backend supports a
bounded way to inspect the game's **existing successful writable VB mapping
before its normal Unlock**, without requesting another lock. For ordinary
managed buffers, that pointer addresses an initialized, pinned CPU heap shadow.
The shadow is the source of the subsequent GPU upload. This is an
implementation-specific proof, not permission to treat every D3D9 WRITEONLY
mapping as readable or every successful Unlock as sufficient certification.

No game, GPU probe, mapping or production change was performed for this review.
The proposed consumer is described in
[finite-position evidence](../architecture/finite-position-evidence.md).

## Exact provenance

Freshly hashed both Preview's `lib/wine/i386-windows/` modules and the Steam
bottle's `drive_c/windows/syswow64/` copies; the corresponding files match:

| PE32 module | SHA-256 |
| --- | --- |
| d3d9.dll | `58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf` |
| wined3d.dll | `f4997bc0465de7e87bac9921bf0274db00ac3b3ba0754fa03f1f33e309a8e863` |

All addresses below are RVAs relative to the preferred base `0x10000000`.
Local disassemblies are `/tmp/x3-installed-d3d9-disasm.txt` and
`/tmp/x3-installed-wined3d-disasm.txt`. The whole-module digests are the primary
version gate. Selected file-backed byte-range digests, with exclusive ends:

| Module/range | SHA-256 |
| --- | --- |
| D3D9 `[1f90,204e)` VB Lock | `08e8c0326129b2b28e6e12509e999c9c0a65aa419946a5273e42bef80fcc4acb` |
| D3D9 `[4160,41db)` lock flags | `78c3719e598cd5f7f54a708eb42fab757c777d8832ff470ca7b77c92ca1714f2` |
| WineD3D `[1b4a2,1b523)` managed pinning | `299311634de6d036ba1fcfe128c6a08952238f47181c1f20866bc2fbff98e46b` |
| WineD3D `[1c4f0,1ca60)` buffer map | `cb4fea429141c2f7eab44c02c57bf13d26af108539322e3214721dd799dac10e` |
| WineD3D `[7b780,7b7e4)` heap allocation | `d42ce96644df82eac2460d689000d43aea796645fe8b668b423095f77327bac9` |
| WineD3D `[1ca60,1cc00)` buffer unmap | `3517da465370475c7735600c669073d0c1fd400a4a80ceccb25574ec7cc4caca` |

Upstream [Wine buffer source](https://github.com/wine-mirror/wine/blob/master/dlls/wined3d/buffer.c)
and [command-stream source](https://github.com/wine-mirror/wine/blob/master/dlls/wined3d/cs.c)
help name the routines. They are corroboration only; they are **not matching
Preview source**. The instructions and data tables in the pinned binaries are
the evidence for this contract. The public
[D3D9 Lock contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dvertexbuffer9-lock)
does not authorize a new READONLY lock on a WRITEONLY buffer.

## CPU shadow and upload chain

1. D3D9 buffer initialization at `1300–137c` translates pool `1` to internal
   MANAGED usage bit `0x20000000`. It also sets both map access bits for non-default
   pools, even when the application's usage is WRITEONLY `8`.
2. WineD3D buffer initialization tests that managed bit at `1b4ab`, sets
   `pin_sysmem` at `1b502`, sets the valid location to SYSMEM at `1b506`, and
   prepares storage at `1b513`. The allocator `7b780` calls imported
   `ucrtbase!calloc` through IAT `29777c`, allocating `size + 16`, and stores the
   aligned pointer at resource offset `0x58`. This is ordinary initialized CPU
   heap memory, not a write-combined GPU mapping. A consumer must still bound
   resource sizes well below overflow and validate its own byte spans.
3. VB Lock `1f90` constructs `{left=offset,right=offset+size}` and calls resource
   map. Flags conversion `4160` maps WRITEONLY plus ordinary flags `0` or
   `D3DLOCK_NOSYSLOCK (0x800)` to internal WRITE `0x40000000` or `0x40000800`.
   Resource map `7b690` obtains the device's immediate command stream and calls
   context map `4c190`.
4. Map emission `37940` waits for earlier resource use (`379f8–37a8f`), emits the
   map command, and submits/finishes the map queue (`37adf–37af2`) before returning
   the result. The buffer resource-ops table at `1e7c90` names map `1c4f0` and
   unmap `1ca60`; those are actual table entries, not nearest-symbol guesses.
5. Buffer map increments its map count and takes the SYSMEM path for an ordinary
   WRITE, or independently because `pin_sysmem` is set (`1c57d–1c5af`). It
   materializes SYSMEM if needed, invalidates other locations/records the dirty
   range at `1c61e–1c632`, and returns `heap_memory + offset` at
   `1c638–1c64e`. Managed pinning prevents the GL buffer-object mapping path.
6. Upload of the BUFFER location builds its source address directly from the
   same heap pointer at `1a578–1a582` and passes the dirty ranges to the backend
   copy at `1a618–1a648`. No pre-Unlock private staging copy substitutes different
   bytes. Eviction `1a710` tests `pin_sysmem` at `1a71b` and returns without
   freeing the pinned shadow. Unmap `1ca60`, for a heap mapping with no GPU map
   pointer, decrements the map count and returns without changing/freeing the
   shadow (`1cadd–1cb2a`, `1cbd9`).

Thus, under serialized ordinary application use, the bytes read from the live
mapping immediately before Unlock are the CPU bytes subsequently uploaded for
that dirty window. Untouched bytes in a valid window remain the actual initialized
or previously stored shadow bytes; the observer need not infer that the game
wrote every byte. This does **not** establish finite position values before those
bytes are actually classified, and does not certify bytes outside the observed
window merely because the allocation was originally zeroed.

## Range, flags and ordering boundaries

Use the successfully returned application pointer, the observed offset/size and
the verified native allocation length. For nonzero size, require
`offset <= length` and `size <= length - offset`, plus nonwrapping pointer spans.
For `(offset,size) == (0,0)`, this implementation returns the full shadow and
invalidates the entire buffer (`1c100–1c10f`, `1c18e–1c1ad`). A nonzero offset with
zero size still returns `heap + offset`; the dirty-range helper does not expand
that case to the remaining length. Reject that ambiguous form in the first
producer. Do not use successful native map status as a bounds check: this backend
can tolerate invalid map boxes for buffer resources.

The first producer should admit only the observed ordinary writable flags
`0`/`0x800`, MANAGED pool `1`, exact supported usage, one outstanding observed
map, same-thread Lock/Unlock, and verified unmodified wrapper/native dispatch.
Nested mappings, unknown prior pending state, unknown flags/ranges, failed calls,
cross-thread handoff, reset/release races, native writes bypassing ownership and
ProcessVertices writes invalidate eligibility. No application or backend work
may concurrently mutate or use that mapping while it is classified.

Managed pinning also stops accelerated DISCARD/NOOVERWRITE upload mapping at
`39743–39747`; non-dynamic resources have those flags sanitized by context map
`4c270–4c303`. These observations are **not** a reason to admit those cases now.
The first producer rejects DISCARD/NOOVERWRITE; a later producer must invalidate
old allocation-wide finite evidence on DISCARD and independently qualify its
coverage and synchronization rules. DEFAULT/dynamic GPU mappings are outside
this managed-shadow proof.

Before observer loads, execute an SSE2 **MFENCE** with a compiler memory barrier,
unless a separately proved caller/store path supplies an equivalent guarantee.
Same-thread serialization alone does not finish possible non-temporal stores;
SFENCE alone does not supply the required following-load ordering. Cross-thread
writers require an explicit proven handoff and are excluded initially. Integer
classification should preserve floating-point state and the original API's
HRESULT, output slots and LastError. A readable `VirtualQuery` page by itself is
neither the allocation/coherence proof above nor an adequate replacement for it.

## Unlock completion is a bounded backend property

D3D9 Unlock calls WineD3D then always returns S_OK. WineD3D emit-unmap has a
generic allocation-failure branch for its 16-byte command, so public success
alone would not prove native map closure for arbitrary implementations. The
existing [immediate command-stream proof](mesh-unlock-contract.md) applies here:
the application MT route uses a preallocated 4 MiB ring, whose null return is
oversize only; the ST route has preallocated 4096-byte storage and restores its
top-level cursor after each command. A normal serialized, non-reentrant 16-byte
unmap cannot reach the generic allocation failure. Rechecked require/submit RVAs
`395d0`/`39670` and MT/ring RVAs `3c2d0`/`3c530`. Managed usage changes the
dispatched buffer operation, not the immediate packet allocator. The ordinary
heap unmap then decrements the single map count to zero without GPU unmapping.

The consumer must retain those exact backend, immediate-context, lifetime and
non-reentrancy preconditions, stage classifications before Unlock, and publish
only after its matching successful normal Unlock and matching known revision.
Foreign hooks, code patches, corrupted objects, backend reentry and process
faults remain outside this proof. No production observer or finite certificate
has been implemented or verified by this document.

Independent static review by the platform agent rechecked managed initialization,
the map/pinned-heap branch, calloc allocation, upload source and pin-aware eviction,
and found no contradiction in this bounded contract. That review did not validate
a future observer implementation or independently retrace every dirty-range helper.
