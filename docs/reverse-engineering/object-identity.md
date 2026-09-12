# Render-node identity and transform source

The material path exposes a real render-node pointer and a registered numeric
handle separately from shared geometry. This is a concrete candidate for instance
correspondence. It has **not been captured in the game yet**, and no previous-world
matrix or complete lifetime-safe temporal association is claimed.

## Provenance and method

Read-only analysis of installed `X3AP.exe`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
2,153,984 bytes, preferred base `0x00400000`. Addresses below are preferred VAs for
that exact executable. Ghidra 12.1.3 in `/tmp/x3-ghidra-research/X3Render.gpr`
provided targeted xrefs/decompilation, checked against x86 instructions and the
installed bytes with MinGW objdump. Raw outputs remain under `/tmp/x3-object-*`;
no raw game implementation is committed.

The original [analysis script](../../tools/analysis/X3ObjectContext.java) emits
xref neighborhoods and explicitly selected function instructions. Existing
`X3DecompileFunctions.java` emits local decompilation. Example:

```sh
JAVA_HOME='/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home' \
  '/opt/homebrew/Cellar/ghidra/12.1.3/libexec/support/analyzeHeadless' \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -noanalysis -readOnly \
  -scriptPath tools/analysis -postScript X3ObjectContext.java \
  /tmp/x3-object-context.txt 004c0150 004c4fc0 0047e6e0 00486d10 00487be0 004efcc0
```

## Submission: verified call chain and ABI

| Anchor | Observation |
|---|---|
| `0x0047d9c0` | Visits a render node and its children. Looks up a model with node `+0x140`; chooses a geometry/LOD record with `+0x14c`. Some branches update the node's basis for camera-facing geometry before drawing. |
| `0x0047e002` | Calls world-setup `0x004bdee0` for the current node and camera. |
| `0x0047e076` | Immediate material-submission call to `0x004c4fc0`; ECX is the mesh-part descriptor, stack arguments are node and camera. |
| `0x0047e0f6`–`0x0047e105` | Deferred record receives mesh part at `+8`, geometry/LOD at `+0xc`, node at `+0x10`, sorting value at `+0x14`, flags at `+0x18`. |
| `0x0047e6e0` | Drains the deferred list; record `+0x10` remains the render node. Recomputes world when this pointer changes (`0x0047e70c`). |
| `0x0047e769` | Deferred call to the same material-submission routine, with ECX=`record+8`, stack node/camera. |
| `0x004c4fc0` | Reads mesh-part `+0x64` to obtain its material/geometry descriptor; also edits projection coefficients for the selected camera regime. |
| `0x004c5228` | Single direct `CALL rel32` to `0x004c0150`; both branches above converge here. |

At **entry to `0x004c0150`**, arguments are six 32-bit stack slots:

| Entry stack offset | Meaning supported by use |
|---|---|
| `ESP+4` | Material/geometry descriptor; subset count at `+8`, records pointer at `+0xc`, record stride `0x1a8` |
| `ESP+8` | Render node; instance transform, visibility/material flags and per-node parameters |
| `ESP+0xc` | Camera/render context |
| `ESP+0x10` | Flags (call site supplies zero or two); the callee consumes the low byte |
| `ESP+0x14`, `ESP+0x18` | Optional directional-light render-node pointers |

Caller removes `0x18` bytes after return; callee returns a 32-bit integer in EAX.
The callee establishes EBP, aligns ESP and saves nonvolatile registers. The
examined entry initializes its temporary EAX/ECX/EDX/EBX/ESI/EDI uses rather than
requiring an additional hidden register argument. Ghidra's `undefined(void)`
signature is not the source of this ABI claim; the pushes, frame accesses and
caller cleanup are.

This callsite can delimit a thread-local context around the original call. All
D3D draws made synchronously underneath it can carry that context without pairing
draws by index. It does not cover every renderer path; particles and other
submission routines need explicit unknown-identity handling.

## Numeric identity and lifetime

Observed node fields:

| Node offset | Evidence / interpretation |
|---|---|
| `+0`, `+4` | Intrusive next/previous links, not identity |
| `+0xc` | Child-list head/sentinel |
| `+0x1c` | Owning spatial/render context; used elsewhere for context scale |
| **`+0x28`** | **Registered render-node handle** |
| `+0x140` | Shared model/resource lookup key; not an instance ID |
| `+0x14c` | Selected geometry/LOD array index; can change for one node |

`0x00486d10` allocates/initializes a `0x270`-byte node. At `0x00486d78`, it calls
`0x004efcc0` with the node pointer and the map found at
`*(*0x00608518 + 0xc)`. At `0x00486d88`, it stores the returned handle at `+0x28`.
The related allocator `0x004885a0` and camera allocator `0x00488c70` use the same
handle system; cameras allocate `0x790` bytes.

`0x004efcc0` increments the map counter at `+8`, wraps after `0x7ffffffe` to one,
and checks occupied keys before inserting a pointer. Thus it is a live map key,
not a never-reused globally unique number. `0x00487be0` recursively releases
children, unregisters node `+0x28` through `0x004efd30` at `0x00487d70`, then zeros
and frees the node. This substantiates a lifetime boundary, but no destructor hook
is implemented by the diagnostic module.

The load/deserialization path `0x00479d10` allocates a fresh node and restores
`+0x28` from serialized data (`0x00479dd2`). Therefore a handle can survive a
save/load while its runtime lifetime changes. A prospective history key must
include a tracked scene/load epoch and validate handle, pointer and owning map;
those values alone do not prove a new lifetime if everything is reused. A future
lifetime hook or conservative invalidation policy remains necessary.

Deferred queue entries are explicitly unsafe identities: `0x0047b2e0` reuses
pooled `0x1c`-byte entries, `0x0047e920` recycles the previous list, and
`0x0047e620` sorts by `+0x14`. This directly explains reordered draws without
changing the underlying instance node. The node handle is the useful new signal.

## Current transform: exact source versus previous state

`0x004bdee0` accepts a geometry record in EAX, node and camera on the stack. For
the ordinary branch where node `flags(+0x130) & 0x200 == 0`, it constructs:

| Field | Representation / consumption |
|---|---|
| Node `+0xb0/+0xb4/+0xb8` | Three signed integer current translation components |
| Node `+0xc0/+0xc4/+0xc8`, `+0xd0/+0xd4/+0xd8`, `+0xe0/+0xe4/+0xe8` | Three basis rows, signed fixed point divided by 65,536 |
| Node `+0x70`, `+0x80/+0x84/+0x88` | Base and per-axis scale used in the fixed-point multiply before conversion |
| `*(camera+0x1c)+0x2c` | Floating conversion scale for this coordinate context |
| `*0x00608a44` | Pointer to the resulting 64-byte float world matrix |
| `*0x00608a48` | Pointer to the related basis matrix used for inverse transpose |
| `*0x00608a40` | Pointer to the current float view matrix |
| `*0x00608a38` | Pointer to the current projection matrix (can be adjusted per submission) |

The conversion constant at `0x005654e0` is exactly `1/65536`. Instructions
`0x004be253`–`0x004be3e2` establish the ordinary conversion, world writes and
homogeneous terms. In material rendering, the `g_mWorld` effect parameter is fed
from `*0x00608a44`; WVP uses world/view/projection paths around `0x004c21c0`–
`0x004c2303`. Effect upload/transposition must still be compared numerically to
captured shader-register rows; do not equate raw memory layout with CTAB layout.

These globals are scratch storage for the **current** submission. Retaining their
addresses does not retain old values. The node's earlier `+0x30` translation and
`+0x40` basis are not a validated previous-frame transform: allocation copies the
basis into `+0xc0`, scene traversal updates render-ready coordinates, and
`0x0047d9c0` can replace the basis with camera-facing values. The scoped analysis
found no trustworthy previous-world buffer. A temporal implementation should
save the actual submitted matrix in its own bounded history only after identity,
coordinate regime and lifetime rules are established.

## Implemented diagnostic seam (not installed or game-validated)

`src/proxy/object_trace.{h,cpp}` implements `X3M_OBJECT_TRACE=1` behind an exact
on-disk SHA-256/size gate, PE/base checks and the expected five callsite bytes.
It redirects only the in-memory relative call at `0x004c5228`. No prologue is
relocated; the original callee receives all six slots and its EAX result is
returned. Install during the first proxy backend initialization, outside loader
lock and before any render submission. Shutdown requires the same quiescence.
No thread suspension or arbitrary concurrent instruction patching is supported.

`current(Snapshot*, matrices)` performs bounded self-process reads through
`src/proxy/engine_memory.h` (validated direct reads with a per-frame region
cache; `X3M_ENGINE_READS=rpm` restores `ReadProcessMemory`): four reads (node
block, `camera+0x28`, engine slot, `engine+0xc`, 344 bytes) on the route's
per-draw path with `matrices=false`, twelve (620 bytes, plus the four matrices)
on capture frames and for diagnostics. Individual validity bits prevent
unreadable fields becoming evidence; a decommitted node page clears the Node
bit without faulting (fixture case).
 The wrapper
itself performs no payload reads, per-draw logging or history lookup. The session
number identifies this module's installation, **not** an observed game load epoch.
A null/unknown scope is explicit. Snapshot overhead should be measured separately
from rendering; leave this switch off outside diagnostic captures.

A stack scope and explicit x86 SEH registration restore nested context on normal
return and foreign game exception unwind. Failed TLS updates disable observation
so no stale stack pointer is exposed. The module keeps code/protection ownership
until rollback or shutdown fully restores bytes, instruction cache and page
protection. Observation can be disabled while `recovery_required()` remains true;
the module must remain loaded until recovery succeeds. It never overwrites code
that is neither its original nor replacement call. The wrapper may still forward
the original target while a failed installation awaits recovery.

Original synthetic fixture: `verification/probe/object_trace.cpp`, built/run by
`build_object_trace.sh` / `run_object_trace.py`. Tests cover exact argument/EAX/
LastError forwarding; nested scope; unreadable pointers; foreign SEH unwind;
default-off/non-game rejection; failed patch stages and failed rollback recovery;
shutdown retry; TLS enter/leave failure; read-path identity (hashed records under
`rpm` and `direct`), per-call cost of both paths, and a fake node page decommitted
between frames, recommitted, and read across a reserved-only page edge. The runner fresh-builds and records
pre/post source and executable hashes plus exact report bytes. This verifies
mechanism with original code, not executable call coverage in X3.

The fresh run passed **«OT_CHECKS» checks / «OT_CALLS» backend calls** (the read-path
loops account for the calls); see
[exact results](../../verification/results/object-trace-summary.json), whose
`read_path` field carries the `TIMING`/`IDENTITY` lines: «OT_RPM_ROUTE» µs per
route-path call over `ReadProcessMemory` against «OT_DIRECT_ROUTE» µs direct, identical
record hashes. The standalone executable hash remained identical before and after
execution. Earlier run (2026-09-11): 139 checks / 9 backend calls, source cpp
SHA-256 `22b7e2a7e24e5c6b4db2421ef9c3a8fce80dd0940e41b245bf26c58c452cb5ba`.

## One consolidated validation capture

Add the new scope snapshot to existing captured draws, alongside the existing
resource/range/write-generation evidence, shader constants, device lifetime,
frame, camera regime, depth epoch and pass classifier. Record the diagnostic
install status and observed covered/uncovered draw counts. In the same user test:

1. Stationary camera then camera turning, with repeated instances and a moving
   object visible. Compare node handle/pointer/map across reordered draw ranges.
2. Verify one node can produce multiple mesh parts and LODs; do not collapse draw
   material ranges into a single geometry record. Report same-frame conflicting
   world matrices for the same node/camera as ambiguity.
3. Compare captured raw world/view/projection against actual VS constants using
   the already established transpose/multiplication convention. Correlate raw node
   coordinates to current world under the measured context conversion scale.
4. Include visibility loss/reappearance and, if part of the user's test, a scene
   load. Explicitly detect changed map/node/handle tuples and invalidate history;
   absence of a destructor signal is a limitation, not evidence of survival.
5. Report coverage gaps for particles, UP draws, scene mirrors/environment maps,
   billboards and overlays. Unknown identity gets history rejection until another
   validated path exists.

Acceptance is correspondence evidence, not merely a successful hook: no unsafe
handle aliases within an epoch, consistent current-world uploads, reproducible
moving-instance changes independent of camera motion, and explicit rejection of
uncovered or conflicting cases. Production object motion vectors remain gated.
