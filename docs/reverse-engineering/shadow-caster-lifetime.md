# Shadow-caster lifetime: submission gates, buffer release, load and sector boundaries

Read-only static analysis, 2026-09-17, answering RE questions 1, 2, 3, 5 and 6 of
[shadow-caster-retention.md](../architecture/shadow-caster-retention.md) (stage 2 of
proxy-side caster retention). It establishes **every gate that can suppress a render node's
submission while the object still exists**, whether the engine observes the return value of
`Release` on mesh buffers, the ordering of node retirement against buffer release, and what a
save load and a sector change do to nodes and buffers.

The headline correction is in §0: **`0x004f66e0` is not the view cull**. The view/LOD cull is
`0x0047cfe0`, already described in [render-node-bounds.md](render-node-bounds.md) §1; this note
completes its gate list and adds the gates above and below it.

## Provenance

Installed `X3AP.exe`, 2,153,984 bytes, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, preferred base
`0x00400000`. Ghidra 12.1.3 headless on the existing read-only project
`/tmp/x3-ghidra-research X3Render` (`-noanalysis -readOnly`) with
`tools/analysis/X3DecompileFunctions.java`, `X3CallTree.java` and the new
`tools/analysis/X3FunctionContext.java` (containing function, size, callers), cross-checked
against `i686-w64-mingw32-objdump -d` of the same file. Raw decompilation and the instruction
listing stay local under `/tmp/x3-caster-lifetime/`. No game, Wine, build or install was run.
Every structural claim below is checked against the instruction listing, not the decompiler's C
alone; where only the decompiler supports a claim it is marked.

Layout names (`node+0x12c`, `part+0x60`, LOD record `+0x3c`, descriptor `part+0x64`, subset
record stride `0x1a8`) are those of [render-node-bounds.md](render-node-bounds.md); registry and
epoch facts are those of [object-lifetimes.md](object-lifetimes.md).

## 0. `0x004f66e0` is an animation stepper, not a visibility predicate

[frame-loop-phases.md](frame-loop-phases.md) §2 labels `0x004f66e0` a "visibility/bounds
predicate; true -> node[0x12c] |= 0x2000", and the design note inherited that label. The body
disagrees.

`0x004f66e0(count, array)` iterates `count` records of stride `0x80` at `array`, comparing each
record's start/period fields against the global clock `*(DAT_00606f34 + 0x718)`, advancing a
frame cursor (`rec+0x3c`, `rec+0x70`), building a 3×3+translation `0x10000`-scaled 2-D transform
into `rec+0x40..`, and on some record kinds calling `0x004f5330` and the track/emitter play
helper `0x004f65f0` at `0x004f6836` (media-cue path, [media-cue-playback.md](media-cue-playback.md),
[voice-startup-sequence.md](voice-startup-sequence.md) §3). It reads no bounds, no camera, no
frustum and no view. It is the **per-node animated-texture / sprite-sequence stepper**; the
returned byte means "at least one record changed its frame this tick", and the caller's
`node[0x12c] |= 0x2000` is a *refresh* flag, not a visibility flag.

Consequences for the design note: option B1's cost argument is unaffected (the side effects are
real and `0x004f65f0` really is reached), but "relax the cull at `0x004f66e0`" was never
possible — there is no cull there. The genuine cull sites are in §3.

`frame-loop-phases.md` §2 should be corrected; this note does not edit it.

## 1. ABI of the three submission-path callees (Q1)

Taken from the instruction listing at the two callsites, `0x0047e002`–`0x0047e01a` (traversal)
and `0x0047e709`–`0x0047e769` (deferred drain).

| Function | `ECX` | `EAX` | Stack (low→high) | Return | Cleanup |
|---|---|---|---|---|---|
| `0x004f66e0` | — | — | `+4` = `node+0x1a8` (record count), `+8` = `node+0x1ac` (record array) | `AL`, nonzero = a record advanced | caller (part of `add $0x10,%esp` at `0x0047e01a`; `add $0x10,%esp` at `0x0047e727`) |
| `0x004bdee0` | — | LOD record (`model+0xc [node+0x14c]`, or drain record `+0x0c`) | `+4` = node, `+8` = view | `EAX` 1, or **0 without writing anything** if `LODrecord+0x3c == 0` | caller |
| `0x004c4fc0` | mesh part | — | `+4` = node, `+8` = view | `EAX` 1 drawn / 0 refused | caller (`add $0x8`) |
| `0x0047cfe0` | node | — | `+4` = view, `+8` = `char` forced-far flag | void | **callee**, `ret $0x8` at `0x0047d04b` |
| `0x0047d9c0` | node | — | `+4`, `+8` unused by the body, `+0xc` = view | void | caller |

Side effects of `0x004f66e0`: it writes only into its own `0x80`-byte records and the two
helpers above; it does not touch the node outside `node+0x1a8/0x1ac`'s array. The
`node[0x12c] |= 0x2000` write is in the callers (`0x0047e021`, `0x0047e72e`), not in it.

**Child recursion.** `0x0047d9c0` begins `testb $0x2,0x12c(%ebx); je 0x47e5b6` at
`0x0047d9d0`–`0x0047d9db`, and `0x0047e5b6` is the **child loop**. A node that fails the
renderable flag, or whose model id `node+0x140` is negative, or whose model is not in the cache,
therefore still has **all of its children traversed and submitted**. The same is true of
`0x0047cfe0`, whose recursion at `0x0047d528`–`0x0047d546` is reached from every rejection
branch — with two exceptions, both full returns that skip the subtree: `node+0x130 & 0x20`
(`0x0047d039`) and `node+0x12c & 0x4000000` (`0x0047d0a9`). Articulated casters (station arms,
turrets) are separate child nodes, so a parent's rejection does not by itself remove them.

## 2. Where `0x004bdee0` puts the world transform (Q2)

`0x004bdee0` is called **before** `0x004f66e0` on both paths (`0x0047e002` then `0x0047e015`;
`0x0047e70c` then `0x0047e722`). It writes **engine-global scratch matrices**, never a node
field:

| Global | Content |
|---|---|
| `*DAT_00608a48` | node basis rows `+0xc0..+0xe8 × 2^-16`, unscaled (or a scaled copy in the `node+0x130 & 0x200` screen-node branch) |
| `*DAT_00608a44` | the **world matrix**: basis rows × `((node+0x70 × node+0x80/84/88) >> 16) × context scale`, translation `node+0xb0/0xb4/0xb8 × context scale` |
| `*DAT_00608a40`, `*DAT_00608a3c` | identity and the screen-space placement matrix for the `+0x200` branch |

These are overwritten on the next node, so a culled node has **no stored world transform
anywhere**. What persists is the node's own fixed-point state: position `+0xb0/+0xb4/+0xb8` and
basis rows `+0xc0..+0xe8`.

For stage 3 this splits into two cases:

- **Ordinary nodes.** `0x0047cfe0`'s frustum test and `0x0047e920`'s top-level distance test both
  read `node+0xb0..` for nodes they are about to reject or never traverse, so the position is
  maintained outside the render walk. That is *evidence of use*, not a located writer: the
  simulation-side writer of `+0xb0`/`+0xc0..` was not identified here. A read-only refresh through
  `engine_memory` is therefore plausible but needs the writer (or a live A/B against a visible
  node, which the design already specifies as the self-validation).
- **Camera-facing and screen classes.** `0x0047d9c0` *rewrites* `node+0xc0..+0xe8` itself for
  `node+0x12c & 0x20` (billboard toward camera), `& 0x4000`, `& 0x10000000` and `& 0x200`
  (the branch chain `0x0047da04`–`0x0047dfdc`, after the model lookup), and only when the node
  passed the renderable gate. For these
  classes the stored basis of a culled node is **stale by construction**; they must never be
  retained with a refreshed transform.

`0x004bdee0` also returns 0 without writing when the LOD record has no GPU build record
(`+0x3c == 0`) — see §4, this is the same record whose refcount governs buffer release.

## 3. Every gate between traversal and `0x004c4fc0` (Q3)

The chain per view, per layer is
`0x0047e780 → 0x0047cfe0` (cull/LOD, whole tree) … `0x0047e920` (traversal driver) →
`0x0047d9c0` (recursive) → `0x004c4fc0` → `0x004c0150`, with the deferred branch
`0x0047d9c0 → queue → 0x0047e620 (sort) → 0x0047e6e0 (drain) → 0x004c4fc0`.

### 3a. `0x0047cfe0` — the per-node cull and LOD pass (writes `node+0x12c & 2`, `node+0x14c`)

Entry clears `node+0x130 & 0x180000` and sets `node+0x14c = 0`. Rejections, in order, each
clearing bit `0x2` of `node+0x12c` unless stated:

| Site | Test | Note |
|---|---|---|
| `0x0047d029` | `node+0x12c & 0x100000` set | unconditional hide latch |
| `0x0047d039` | `node+0x130 & 0x20` set | sets `+0x14c = 0` and **returns**; whole subtree skipped and bit `0x2` left at its previous value |
| `0x0047d04e`–`0x0047d076` | `node+0x18` (parent) non-null **and** `node+0x12c & 0x40000` **and** (parent's bit `0x2` clear **or** parent `+0x14c > 0`) | attached/subordinate node hidden when its parent is hidden or is past LOD 0 — the docking / sub-assembly case |
| `0x0047d082` | bit `0x2` already clear | falls to children |
| `0x0047d091` | `node+0xf8 + node+0xa0 < 0` | behind the eye (camera-space forward + own radius) |
| `0x0047d0a9` | `node+0x12c & 0x4000000` | **returns**, subtree skipped, bit `0x2` left set |
| `0x0047d0b9`–`0x0047d0e4` | `view+0x270 & 0x400` **and** `0x00469bb0(node+0xf0) < node+0xa0` **and** `node+0x140 > 0` **and** `node+0x12c & 0x80` | near/inside-radius rejection |
| `0x0047d0ff` | `0x004c6aa0(node, view, ctx+0x2c) == 0` | **the view cull**: world-space sphere vs the six frustum planes, radius from LOD 0 only (render-node-bounds.md §1) |
| `0x0047d134`–`0x0047d195` | `view+0x270 & 0x10000` **and** `node+0x12c & 0x2000000` clear **and** `distance(node+0xb0, view+0x30) > view+0x370 (clamped by detail level `*(0x00606f34+0x768)` to 1e8 / 5e8) + subtree radius` | **distance cull** |
| `0x0047d282`–`0x0047d29b` | `view+0x270 & 0x1000000` **and** projected size `< 0x14` | size cull in the env-map/cube view |
| `0x0047d2a2`–`0x0047d2c3` | projected size `< max(node+0x1d8, parent+0x1d8)`, both `> 0` | **per-node minimum-projected-size threshold** |
| `0x0047d2cc`–`0x0047d2e1` | projected size `< 1` and `node+0x12c & 0x4000000` clear | degenerate size |
| `0x0047d4e1`–`0x0047d4fc` | selected LOD `== lodCount-1` **and** `node+0x12c & 0x8000` | **fade-out to nothing at the last LOD** |

The projected size is `0x00469a30(node+0xa0, 0x280 or *(0x00608518+0x5c), d)` with `d` from
`0x00469ab0()` scaled by `view+0x298`; `node+0x1dc` and `node+0x130 & 0x100000/0x180000` are
detail-reduction flags, not suppressors. `node+0x13c` (the field the design guessed at as a
fade) is **not read anywhere in `0x0047cfe0`, `0x0047d9c0`, `0x0047e6e0`, `0x004c4fc0` or
`0x004c0150`** — every `0x13c(%reg)` access in the image is in the simulation range or in the
per-view state build `0x0047bc20`.

### 3b. `0x0047e920` — the per-(view, layer) driver, two gates above the traversal

First loop: registration of batch candidates through `0x0046ca80` at `0x0047e9da`. Second loop,
per root node, before `0x0047d9c0` is called at all:

| Site | Test | Effect |
|---|---|---|
| `0x0047ea06`, skip at `0x0047ea12` | `node+0x1c0 != layer id` | node belongs to another layer; not traversed |
| `0x0047ea1b`, skip at `0x0047ea22` | `0x0046cef0(list, view, node) != 0` | **the node was fully consumed by the instanced-batch path**; `0x0047d9c0` is skipped for it entirely |
| `0x0047ea28`, accept at `0x0047eaac`, reject at `0x0047eb0f`–`0x0047eb11` | `view+0x270 & 0x10000` and `node+0x12c & 0x2000000` clear, and `d - r > view+0x370` (same detail clamp as §3a) | **subtree distance cull**; `0x0047d9c0` never runs, so *no* child of this root is submitted |

The batch path is real and is a genuine "visible but absent from the managed draw stream" case.
`0x0046cef0` succeeds only when `0x0046ce20` (called at `0x0046cf15`) accepts the node — `node+0x12c & 2` and
(`node+0x12c & 0x800`, or `view+0x270 & 0x8000` with `node+0x12c & 0x4000000`, or
`node+0x130 & 0x800000` with `node+0x140 != 0xe1` and `0x0046cd40()` nonzero) — and the per-model
batch record `0x0046ca80` (called at `0x0046cf27`, keyed on `node+0x140`, `0x3c` bytes) has
`+0x10 & 1` and `+0x20 >= +0x2c`. Its geometry is built by `0x004bf960` per sub-group and drawn
by `0x0046d080` at `0x0047eb7a`, after the loop. Whether those draws reach the proxy as managed z-writing draws was **not**
established here.

### 3c. `0x0047d9c0` — per-node and per-part gates

| Site | Test | Effect |
|---|---|---|
| `0x0047d9d0` | `node+0x12c & 2` clear | node body skipped, **children still traversed** |
| `0x0047d9e1`, `0x0047d9f0` | `node+0x140 < 0`, or `0x004863c0(node+0x140) == 0` | no model / model not loadable: no draw, children still traversed |
| `0x0047e04c` | `part+0x60 & 0x8000` | **part skipped entirely** (per-part hide) |
| `0x0047e05b`–`0x0047e072` | `node+0x12c & 0x800`, or `part+0x60 & 0x20`, or `node+0x130 & 0x10` | **not a suppressor**: routes the part to the sorted deferred queue instead of the immediate call |
| `0x0047e0e2` | `0x0047b2e0()` returned 0 | deferred record could not be allocated: **the part is silently dropped for this frame**. `0x0047b2e0` is a free list with a `malloc(0x1c)` fallback and a `0x004b8b60` retry, so this is an out-of-memory path, **not** a per-frame budget. There is no draw-count cap anywhere on this path |
| `0x0047e5be` | `node+0x130 & 0x20000` | propagated to each child during its call, then cleared; a flag, not a gate |

### 3d. `0x0047e6e0` — the deferred drain adds no suppressor

It walks `*(0x00608518+0x40)` in sorted order; on a node change it calls `0x004bdee0` and
`0x004f66e0`; it sets or clears `part+0x60 & 0x20000` from `record+0x18 & 1` and forces it when
`*(0x00606f34+0xfc) & 4` is clear; then it calls `0x004c4fc0` **unconditionally** for every
record. Anything queued is submitted.

### 3e. `0x004c4fc0` and `0x004c0150` — the last gates

`0x004c4fc0` (`ECX` = part, SEH frame at entry):

| Site | Test | Result |
|---|---|---|
| `0x004c4fdc`–`0x004c4fe8` | `part+0x64 == 0` (no material/geometry descriptor) | returns 0 at `0x004c4fea`, no draw |
| `0x004c4ffe`–`0x004c501d` | `part+0x60 & 0x40` and (`node+0x140 < 0` or `0x004863c0` lookup fails) | returns 0; otherwise clears `0x40` at `0x004c501f` and continues |
| `0x004c5023`, branch at `0x004c502a` | `part+0x60 & 0x10000` | returns **1** without drawing — the per-part "present but suppressed" bit |

`0x004c0150` entry (`0x004c017f`–`0x004c01b5`): returns immediately if the descriptor, node or
view is null, or if `node+0x12c & 0x4000000`, or if `node+0x12c & 2` is **clear**, or if
`node+0x12c & 0x100000`. Note the asymmetry: `0x0047cfe0` lets `0x4000000` nodes keep bit `2`,
but `0x004c0150` refuses them, so such nodes never produce a managed draw. Below that the loop is
per subset record (stride `0x1a8`); its per-subset skips were not enumerated.

### 3f. What this means for "expected visible but absent"

Safe to treat as expiry evidence (the object really is hidden or gone, not merely out of view):
`node+0x12c & 0x100000`, `node+0x130 & 0x20`, the parent/docking gate, `part+0x60 & 0x8000`,
`part+0x60 & 0x10000`, the last-LOD fade `node+0x12c & 0x8000`, and a missing descriptor.

Will fire as a **false positive** on a healthy static caster that is inside the frustum:

1. the per-node minimum-projected-size threshold `node+0x1d8` (and the inherited parent value)
   and the `< 1` degenerate case — a distant but in-frustum caster is dropped by size, not by
   view;
2. the two distance culls (`0x0047d134` per node and `0x0047eb38` per root subtree), whose
   clamp depends on the global detail level `*(0x00606f34+0x768)` — a caster inside the outermost
   cascade (25,000 units) can be beyond `view+0x370`;
3. the instanced-batch takeover of §3b, which removes a node's normal draws entirely;
4. the engine's cull radius is derived from **LOD 0 only** and is an L2 radius about the node
   origin (render-node-bounds.md §1), so engine-visible and proxy-AABB-visible do not coincide at
   the margin;
5. `node+0x12c & 0x4000000`: culled subtree at `0x0047d0a9`, refused at `0x004c0150`.

The census counter must therefore separate "absent and outside the frustum" from "absent while
inside", and the rule should stay conservative (drop the record) rather than assume a ghost:
2 and 3 both mean the caster is still there. The eight-frame delay does not help against any of
these — they are steady states, not transients.

## 4. Mesh buffer release (Q5)

### 4a. The engine never reads the return value of `Release`

Measured over the whole image. Two call shapes exist for COM vtable slot 2
(`mov 0x8(%reg),%reg2; push obj; call *%reg2` and `call *0x8(%reg)`): **137 + 7 = 144 sites**. A
liveness scan of the instructions following each call (stop at the first redefinition or use of
`EAX`, or at the next control transfer) finds **zero** sites where `EAX` is read before being
redefined. The three candidates the naive scan produced are not COM `Release`: `0x0040a173` and
`0x0040aa72` are `__thiscall` virtuals with `ECX = this` and a `bool` result, and `0x0050f69f`
is `call *0x8(%ebp)`, a function pointer in a stack frame.

At the mesh teardown site specifically (`0x004bcdae`, `0x004bcdc0`, `0x004bcdd2`, `0x004bcde3`,
`0x004bce0a`) every `call *%edx` is followed immediately by `mov %edi,<field>` with `EDI = 0`.

**A foreign long-lived reference on the engine's VB/IB is therefore not observable to the engine
by refcount.** The design's `AddRef`-across-frames plan is safe from that angle; the fallback
vertex copy is not needed for this reason.

### 4b. Where mesh buffers are released

One teardown site: `0x004bccc0(descriptor, meshPart)`, reached only from `0x004bda60` at
`0x004bdad1`. It walks the descriptor's subset records (stride `0x1a8` at `descriptor+0x0c`) and
releases `record+0x0c` (`IDirect3DVertexBuffer9`), `record+0x10` (`IDirect3DIndexBuffer9`) and,
when present, `record+0x14` (`ID3DXMesh`); then frees the subset array. The other `Release`
sites on the same triple are inside the builders `0x004bb470`, `0x004bc9c0` and `0x004bcb60`
(`CloneMesh`), i.e. build-time rollback, not runtime churn.

The lifetime is refcounted twice:

- **Per LOD-GPU-record**, `LODrecord+0x3c`, field `+0x44`: created with `1` at `0x004bda2e`,
  incremented at `0x004bda3a`, decremented in `0x004bdb40`; at `1 → 0` it calls `0x004bda60`
  (unless `model+0x50 & 0x20`), which releases the buffers. `0x004bdb40` takes the node in
  `EAX`, reads `node+0x140`, looks the model up in the model cache `*(0x00608518+0x14)` and only
  touches the LOD whose record's `+0x40` equals its index.
- **Per descriptor**, `descriptor+0x06`: decremented in `0x004bda60`; at `1 → 0` the descriptor
  is freed after `0x004bccc0`.

`0x004bdb40` is called from `0x00486ba0` (`0x00486c53`), which is reached from the node
model-change routine `0x00487e30` (`0x00487e7c`, ~66 callers in the simulation), from
`0x00489e90` (`0x00489ed8`), from `0x0048a060` and from `0x00486ca0`.

### 4c. Ordering: buffers are released **before** the node leaves the registry

In the node destructor `0x00487be0`:

1. `0x00487c93` — recursive `0x00487be0` on every child, so children retire first;
2. `0x00487cab` — `0x00489e90(node+0x1c)`, which reaches `0x00486ba0 → 0x004bdb40` and therefore
   the `Release` calls;
3. `0x00487d70` — `0x004efd30` (the per-key registry removal the lifetime observer hooks);
4. then `memset` of `0x270`/`0x790` bytes and `free`.

So **the engine's own references are dropped a few hundred instructions before the retirement
signal the proxy observes.** A retained record that holds no reference of its own would, in that
window, have a dangling VB/IB whose memory the allocator can hand to the next
`CreateVertexBuffer`; the retirement signal arrives only afterwards. With the design's
`AddRef`-per-record the D3D objects cannot be destroyed or their addresses recycled, so the
window is benign and the ordering merely means "release the retained record at the retirement
signal, and never key anything on a raw buffer pointer that the record does not hold a reference
to". That constraint is now measured, not assumed.

The reverse direction also exists: `0x00487e30` changes a node's model **without** retiring the
node, decrementing the LOD refcount first (`0x00487e7c`) and loading the new model after
(`0x00487e51`). A retained record for a live node can thus lose its buffers with no lifetime
event at all. The design's per-buffer Lock/revision check does not cover this; the record must
also be re-validated when the node's submitted `(model, lod)` changes — which the route key
already carries, and which the "node submitted this frame supersedes its whole retained set"
rule handles for *resubmitted* nodes. For an **unseen** node whose model is swapped, nothing in
the current rule set expires the record. This is a new stage-2 requirement, see §6.

## 5. Save load and sector change (Q6)

### 5a. Save/scene load: epoch and a full registry sweep

`0x00404cc0` is the scene/save loading routine. Its single caller is `0x00403840` at
`0x004038ba` — and `0x00403840` is the **session function**: it calls `0x00404cc0` once behind
the `"true\LoadScrHD"` loading screen and the `"Restart"` cue, then runs the frame loop
(`0x00416750` simulation, `0x0041cde0` cockpits, `0x00471f50` render, `0x004e3e70` Present) and
the `"BeforeSave"` / `"SaveFinished"` paths. So the renderer-load epoch seam `0x0040508d`
(object-lifetimes.md) fires **once per game/save load**, not per sector.

Inside the same load, after the scene deserialisation succeeds, `0x00405167` calls
**`0x004872c0`**: it enumerates the whole render registry `*(0x00608518+0xc)` and, for each
node, either clears `node+0x130 & 0x180` or calls the general destructor `0x00487be0` when
`(node+0x130 & 0x100) != 0 && (node+0x130 & 0x80) == 0`. That is the bulk retirement of every
node the new scene did not claim, and it goes through the observed per-key removal helper
`0x004efd30`, one key at a time. The order is: deserialise and insert the new nodes
(`0x0047a6ad`), *then* sweep the survivors. A retained store keyed on
`(load_epoch, registry_epoch, node_serial)` is invalidated by the epoch bump before either.

### 5b. Sector change / gate jump: no epoch, no bulk destruction

None of the three bulk boundaries fires on a sector transition:

| Boundary | Only reachable from |
|---|---|
| Renderer-load epoch `0x0040508d` | `0x00404cc0`, once per session start (§5a) |
| Registry map destruction `0x004efe10` | complete engine teardown `0x004710f0` at `0x004712e1` (object-lifetimes.md) |
| Registry sweep `0x004872c0` | `0x00405167`, inside the same load |
| Unload every model `0x00486920` | `0x00471067` / `0x004711fd` (engine init/teardown) and two script-VM opcode handlers, `0x00495118` inside `0x00493b40` and `0x004650b8` inside `0x00460630`, both preceded by `0x0048a060` and followed by `0x0048a1e0`; both dispatchers are reached only as data (jump-table entries at `0x0046a2e5` and `0x004344c7`) |

This matches object-lifetimes.md's conclusion that there is no universal sector-transition hook.
The old sector's nodes are therefore retired **individually** through `0x00487be0 → 0x004efd30`,
exactly the boundary the existing observer hooks, and the new sector's nodes are created by the
ordinary allocators through `0x004efbf0`. Retention needs no new sector signal: per-node
retirement plus the existing `mutation_revision` gate covers it. The cost is that a gate jump
produces a burst of removals rather than one epoch change, so the store's retire path must be
able to absorb a few hundred removals in one frame, and `mutation_revision` will move on
essentially every frame during the transition (the "revalidate only when the revision moved"
optimisation degrades to "every frame" for the duration).

### 5c. Are buffers pooled across sectors?

Partly, and not at the level the question assumes:

- The **model cache** `*(0x00608518+0x14)`, keyed by `model id + 1` and filled by `0x004863c0`,
  is never evicted during rendering: the only teardown inside `0x004863c0` is the load-failure
  path at `0x00486844` (a model that parsed to zero LODs). CPU-side model data therefore survives
  a sector change and is re-used if the new sector uses the same ship or station type.
- The **GPU buffers** are not pooled: they hang off the per-LOD build record `LODrecord+0x3c`
  under the `+0x44` refcount of §4b. When the last node using a (model, LOD) retires — which is
  exactly what happens to types that exist only in the old sector — the record's buffers are
  released and the record is freed. If the same model is used again later, `0x004bd830`/
  `0x004bb470` build a **new** record with **new** `CreateVertexBuffer`/`CreateIndexBuffer`
  allocations.
- Consequently a VB pointer observed before a sector change can be re-issued by the runtime to a
  different mesh afterwards. Only the proxy's own `AddRef` prevents that aliasing; a
  pointer-keyed store without a held reference would be unsafe across a gate jump. The design
  already holds references, so the requirement is that the reference is taken at record creation
  (not at replay time) and released only on expiry.

## 6. What this changes for stage 2, and what is still unknown

New requirements established here:

1. Expire a retained record when the node's submitted **model or LOD identity** changes, and
   accept that an *unseen* node whose model is swapped by `0x00487e30` produces no signal at all
   — the store must also drop a record whose buffers were released, which is only detectable by
   the proxy's own reference being the last one (`Release` return is invisible to the engine,
   §4a, but visible to the proxy).
2. "Expected visible but absent" must exclude the size and distance culls of §3f, or it will
   evict healthy static casters at range — precisely the casters retention exists for.
3. Retention must not apply to the camera-facing / screen node classes (`node+0x12c & 0x20`,
   `0x4000`, `0x10000000`, `0x200`), whose stored basis is only refreshed while visible (§2).

Unknown, and what would settle it:

- Whether the instanced-batch path (§3b) produces managed z-writing draws that the proxy already
  records, or draws the proxy never sees. Settled by a census counter that compares the set of
  nodes the proxy routes against the nodes a frame's `0x0046ca80` records cover — or, cheaply, by
  checking whether the `0x0046d080` draws appear in an existing capture with a distinct
  declaration; not attempted here.
- The simulation-side writer of `node+0xb0/+0xc0..` (stage 3's read-only refresh).
- The per-subset skips inside `0x004c0150`'s `0x1a8`-stride loop (16 KB body, not enumerated).
- Whether `0x004872c0`'s keep condition (`node+0x130 & 0x100` without `& 0x80`) can spare a node
  across a load; if it can, an epoch-keyed store is still correct because the epoch changes
  regardless.
- Actual gate-jump behaviour was not observed. Cross-check on run111
  (`/private/tmp/x3-bottleX3-run111/session-20260917-073417-212.log`, queried with `grep`, never
  opened whole): the token `load_epoch=N registry_epoch=M` occurs 56,996 times, 56,279 of them
  `(2, 2)` and 717 `(0, 0)` — one renderer-load epoch and one registry epoch for the whole
  session, with no transition. §5b therefore rests on the static call graph, not on a measured
  sector transition.
