# Object lifetimes and temporal-history reset boundaries

There is a concrete renderer-scene load boundary and there are concrete node and
camera retirement boundaries. A render-node handle is **not a lifetime token**:
automatic allocation can wrap, deserialization restores saved handles, and map
insertion can replace an existing value. The implementable next step is an
explicit renderer-load epoch plus observed retirement tokens, combined with the
existing device/frame validity gates. This investigation does not establish a
universal sector-transition or camera-cut hook.

## Provenance

Read-only analysis of `X3AP.exe`, 2,153,984 bytes, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
x86 PE32, preferred image base `0x00400000`. All addresses below are preferred
VAs for this exact build; use checked module-relative addresses at runtime.
Ghidra 12.1.3 targeted xrefs/instructions/decompilation were checked independently
against MinGW objdump and installed PE bytes. Raw outputs remain local under
`/tmp/x3-lifetime-*`; no game implementation is redistributed here.

The original read-only [fingerprint tool](../../tools/analysis/inspect_object_lifetimes.py)
verifies the full executable hash/size, PE architecture/base, five call opcodes
and their relative targets. The [derived report](../../verification/results/object-lifetime-sites.json)
includes five-byte call fingerprints and SHA-256 digests of 32-byte context and
callee-prefix windows. These are validation data, not an implemented detour.

```sh
python3 tools/analysis/inspect_object_lifetimes.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/X3AP.exe" \
  --output verification/results/object-lifetime-sites.json

JAVA_HOME='/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home' \
  '/opt/homebrew/Cellar/ghidra/12.1.3/libexec/support/analyzeHeadless' \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -noanalysis -readOnly \
  -scriptPath tools/analysis -postScript X3ObjectContext.java \
  /tmp/x3-lifetime-context.txt 004efcc0 004efd30 00479d10 00487be0 00608518
```

Additional targeted runs used `X3ObjectContext.java` and
`X3DecompileFunctions.java` for `0047a720`, `00470d80`, `00488de0`, `004efbf0`
and parent loader `00404cc0`. Static xref completeness means references resolved
in this Ghidra program, not proof that computed/indirect calls cannot exist.

## Renderer-scene deserialization: a narrow explicit epoch

`0x0047a720` parses a renderer-state stream containing `B3D `, `NAME`, `INST`
and `SCEN` sections. It rebuilds render nodes, cameras and spatial-context
records, then reconnects their stored references. Its node reader `0x00479d10`
allocates fresh storage, restores node `+0x28` from serialized data at
`0x00479dd2`, and installs that handle into the live registry at `0x0047a6ad`.
The node reader also recurses into children. This is direct evidence that a saved
handle can outlive the allocation which previously carried it.

The single statically resolved direct caller of `0x0047a720` is:

| Property | Verified fact |
| --- | --- |
| Caller | `0x00404cc0`, a larger stream/file loading routine |
| Callsite | **`0x0040508d`**, RVA `0x508d` |
| Original call bytes | `e8 8e 56 07 00` |
| Target | `0x0047a720` |
| Entry stack | `ESP+4`: pointer to caller-owned stream state |
| Cleanup/result | Caller adds 4 bytes at `0x00405092`; tests EAX at `0x00405095` |
| Callee evidence | Loads its sole argument from `EBP+8`; initializes its working registers, preserves EBX/ESI/EDI/EBP; ordinary RET |

A callsite wrapper can increment an independent 64-bit `renderer_load_epoch`
**before** forwarding this call. Invalidate histories even if loading fails or
unwinds; never restore the prior epoch on failure. A surrounding load-in-progress
scope should reject history publication until normal completion, with explicit
unwind cleanup if the scope is used. The return only establishes this renderer
subsection's result: the caller subsequently loads other state. It is not a
global “game load succeeded” notification.

This five-byte call seam avoids relocating a function prologue. It still requires
the existing exact-executable/in-memory fingerprint gates, quiescent install and
rollback ownership, and original-code forwarding/unwind verification. No live
patch was attempted here. Incrementing only on successful return would miss
partial mutation; calling it a universal scene generation would overstate the
identified coverage. Sector travel or new-game construction may use other paths.

## Handles, insertions and retirement

The render registry is the pointer at `(*0x00608518)+0xc`. Ordinary node allocator
`0x00486d10`, related allocator `0x004885a0` and camera allocator `0x00488c70`
call `0x004efcc0`. This helper takes the map in EAX and a pointer on the stack,
increments map `+8`, wraps above `0x7ffffffe` to one, and searches occupied keys.
The counter is not monotonic indefinitely and is not a load generation.

Insertion helper `0x004efbf0` takes **EDI = map**, stack `ESP+4 = key`,
`ESP+8 = value`. It returns zero for key zero; an existing key is overwritten
and also returns zero. A fresh insertion updates the map's entry count. Therefore
“zero means no mutation” is false. The automatic allocator normally avoids the
existing-key case, but explicit restore does not provide that guarantee.

Deletion helper `0x004efd30` takes **EDI = map, EDX = key**, no stack arguments.
It returns the removed value in EAX or zero if absent. It does not receive a
normal C++ `this` in ECX; Ghidra's inferred fastcall label is insufficient ABI
evidence. The instructions use EDI directly for buckets/count and EDX for key.

| Candidate callsite | Target | Purpose and extra caller evidence |
| --- | --- | --- |
| `0x004efd09` | `0x004efbf0` | Central insertion within automatic handle allocation; EDI is its map, stack key/value, caller cleans 8 bytes. Generic to many maps: filter the verified render map. |
| `0x0047a6ad` | `0x004efbf0` | Restored render-node insertion; EBX is new node, EDI is render map, stack contains restored handle/node. Caller cleans 8 bytes later at `0x0047a6d0`. |
| **`0x00487d70`** | `0x004efd30` | General node retirement, including recursively released children. EBX is node; EDX is node `+0x28`; EDI is render map. |
| **`0x00488efd`** | `0x004efd30` | Separate camera retirement path. ESI is camera; EDX is camera `+0x28`; EDI is render map. |

The last two occur after list unlinking but **before zeroing and freeing the node
storage**. General release chooses size `0x270` or `0x790` from the camera flag;
the camera-specific path clears/frees `0x790`. Hooking only the general destructor
would miss this separate camera path. A retirement observation can erase the
specific `(registry, pointer, handle)` entry before reuse, and invalidate the
camera's entire temporal history when its camera entry retires. A conservative
global epoch bump is simpler but could reset accumulation on ordinary unrelated
object churn; its cost must be measured rather than assumed negligible.

These callsites are useful observation candidates, **not a proof of all registry
mutations**. Other direct insertion sites include `0x00473672` and `0x004770d1`
with dynamically supplied owner maps; this bounded investigation did not prove
those maps cannot alias the render registry. Generic registry deletion also has
many callers. Full lifetime certification needs either complete audited map
mutation coverage or a separately verified central insertion/removal observer,
plus bulk map reset/destruction coverage. A central function detour would require
instruction relocation and a different verification burden from the call seams
listed here. Do not silently promote partial retirement coverage to uniqueness.

## Existing signals that help, and signals that do not prove a lifetime

- Device lifetime ID, resource generation, Reset attempt/loss, failed Present,
  missing/nonadjacent successful frames, failed capture/context reads and scene
  boundary rejection are valid conservative history-invalidating events already
  available to the adapter. They do not by themselves identify every game load.
- A changed engine or registry pointer is cause to reset. Equality is not proof
  of continuity: the two statically resolved writes to global `0x00608518` are
  initialization in `0x00470d80` and its caller `0x00402780`; renderer loading
  mutates structures beneath the same engine. Sampling this pointer is not a
  reload detector.
- Camera pointer/handle, owning coordinate context, viewport/projection regime,
  and matrix validity must be checked. Camera storage lifetime is different from
  camera mode lifetime. In [iteration 0.4](iteration04-camera-motion.md), the
  user-reported third-person burst retains the same main camera pointer and
  handle as preceding gameplay bursts. No exact in-place camera-mode/cut setter
  or authoritative serial was established here.
- Large view/projection changes, elapsed-time gaps and focus/menu transitions can
  conservatively reject history. Numerical thresholds are renderer policy, not
  a proven engine cut event. Smooth motion and discontinuous teleports cannot be
  universally distinguished from two matrices alone.

## Bounded implementation recommendation

Keep the history API's externally supplied epoch mandatory. Record its provenance
explicitly: a verified renderer-load observation, device/reset generation, manual
test epoch, or unavailable. Missing lifetime evidence must not become epoch zero
with implied stability. Publish history only after a complete successful frame;
require the immediately preceding eligible frame and reject any mid-frame epoch
change. Per-object matching still needs geometry revisions, shader/range checks,
unique submitted transforms and conservative treatment of unscoped effects.

For the next mechanism checkpoint, implement and synthetically verify only the
`0x0040508d` load wrapper and the two retirement call wrappers, with counters and
explicit scope/coverage labels. The retirement wrappers require custom x86
register forwarding; a normal cdecl function pointer is wrong. Preserve original
stack cleanup, EAX result, all original callee-preserved machine state and
LastError; injected bookkeeping must not disturb x87/SSE state or exception
unwind. Test nested calls, failed load, foreign unwind, unreadable registry
metadata, same-address/handle retirement and reappearance, camera retirement,
epoch changes inside a frame and failed rollback using original synthetic code.

Do not enable lifetime-dependent object history solely because those wrappers
pass synthetic tests. A later user-controlled capture must establish the load,
travel and camera-switch coverage, and unresolved mutation paths must retain an
explicit rejection policy. There is no gameplay launch, hook installation or
production edit in this checkpoint.
