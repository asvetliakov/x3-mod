# Frame-loop phases: the render routine's internal partition

2026-09-16. Static study of X3AP.exe,
SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`
(`shasum -a 256` on the bottle copy, matches the identity used by
`verification/probe/verify_game_phase_sites.py`). Nothing here was observed at
runtime; every control-flow, size and offset claim is from Ghidra headless on
`/tmp/x3-ghidra-research/X3Render` plus `i686-w64-mingw32-objdump` on the file
bytes. Inferences are marked.

**Question.** Run 87 shows a 28.5 ms busy frame at 457 draws with roughly 15 ms
(53 %) not inside any D3D call, and the sampling profiler is blind under FEX.
Which top-level phases of the per-frame loop own that time, and where can a
stamp be placed to attribute it on the first instrumented run?

**Scope.** The *main loop* `0x00403840` is already partitioned by the 15 coarse
markers of [selection-frame-phases.md](selection-frame-phases.md) and
`src/proxy/game_phase_sites.h` (`X3M_GAME_PHASES`). `game_phase_render`
(`0x00403f34`) times the **whole** of `0x00471f50` as one opaque interval. This
note partitions that interval and names the render-submission call chain. It
does not repeat the main-loop table, the scene-end callsite `0x004721b1`
([camera-state-and-frame-routine.md](camera-state-and-frame-routine.md) §7–8),
the script VM entry, or the loading phases
([loading-orchestration.md](loading-orchestration.md)).

## 1. Frame routine `0x00471f50` — call tree, depth 1

Size `0x6b9` (`0x00471f50`–`0x0047260b`, one `ret`, cdecl, no arguments,
SEH frame installed at `0x00471f57`/`0x00471f5e`, handler `0x00530668`).
51 `call` instructions. Not recursive.

| Callsite | Callee | Size | Role | How identified |
| --- | --- | --- | --- | --- |
| `0x00471f6c` | `0x004f4fc0` | `0x80` | frame prologue; one indirect call at `0x004f5013` | known site (§7 of the camera note) |
| `0x00471f71`–`0x00471fd1` | — | — | counts views with `view[0x270] & 1` by walking `*0x00608518+0x18` | instruction listing |
| `0x00471fdb`, `0x00472009` | `_malloc` | — | the per-frame view array, `4 * n` bytes; retry after `0x004b8b60` | CRT symbol |
| `0x00472038` | `_memset` | — | zeroes that array | CRT symbol |
| `0x00472044` | `0x004714c0` | `0xdc` | lens-flare scene setup (string `"Lensflare Scene"` at `0x004714ea`) | string xref |
| `0x00472066` | `0x0047b680` | `0x172` | **recursive** per-sector/scene-node transform update (self-call `0x0047b692`); 8 calls to `0x00412450` | self-reference + loop at `0x00472064`–`0x0047206f` |
| `0x0047207c`–`0x004720b3` | — | — | resets `node[0x30]` on every entry of every `view[0x2a0]` list | instruction listing |
| `0x004720c8` | device slot `0xa4` | — | `BeginScene` | vtable displacement 41·4 |
| `0x004720ed` | `0x0047c3d0` | `0x194` | per-view update; fills the view array | known site |
| `0x00472141` | `0x004be7d0` | `0xce7` | view/projection setup for the marked view | known site |
| `0x00472155` | `0x0046c0f0` | `0x71` | fallback for views without the `0x4000` marker | instruction listing |
| `0x00472181` | `_qsort` | — | sorts the view array, comparator `0x004715a0`, element size 4 | CRT symbol + pushes |
| `0x00472197`–`0x00472387` | — | — | **per-view loop** (`[esp+0x1c]` index, `[esp+0x18]` count) | back edge `0x00472387` |
| `0x004721b1` | `0x004c4750` | `0x811` | bloom / scene-end composite — **already patched** by `X3M_SCENE_HOOK` | camera note §8 |
| `0x00472201`/`0x00472210`/`0x0047223d` | `0x004c6280`, `0x0047e820`, slot `0xa4` | — | env-map pass: `EndScene`, six cube faces, `BeginScene` | camera note §7 |
| `0x0047224d` | `0x004892a0` | `0x4d7` | per-view light/environment selection (24 calls to `0x00412450`) | call histogram |
| `0x00472256` | `0x0047bc20` | `0x79f` | per-view state build (`0x004f17f0`, `0x004863c0`, `0x0047b800` ×2) | call list |
| `0x00472260` | `0x0047c840` | `0x8b` | camera / viewport / clear for this view | camera note §8 |
| `0x0047226b` | `0x0047e780` | `0x91` | pass setup: `0x0047cfe0`, `0x0047c8d0`, `0x0047ca40` | call list |
| `0x00472280`–`0x004722c6` | — | — | **layer loop**, `ebp` from `view[0x4c]` to `view[0x50]` | back edge `0x004722c6` |
| `0x00472295`, `0x004722a8` | `0x0047e920` | `0x266` | scene-graph traversal driver for one (view, layer) | see §2 |
| `0x004722af` | `0x0047e620` | `0xc0` | **draw-queue sort** — see §3 | disassembly |
| `0x004722b5` | `0x0047e6e0` | `0x9d` | **draw submission** over the sorted queue — see §2 | disassembly |
| `0x00472307` | `0x004bf4c0` | `0x3c3` | particles / stardust, gated on `view[0x270] & 0x4000` | camera note §9 |
| `0x00472358` | `0x00489bf0` | `0xb9` | post-view fixup | call list |
| `0x00472370` | `0x004715d0` | `0x86` | gated on `view[0x270] & 0x1000` | instruction listing |
| `0x0047238d`–`0x00472427` | — | — | **second view pass** over views with `view[0x77c] != 0`: `0x0047bc20`, `0x0047c840`, 2D overlay `0x004c53d0` | instruction listing |
| `0x0047242d`–`0x004724e9` | `0x00471660` (`0x768`) … | — | the `*0x00608518+0x64` view (cockpit): `0x0047bc20`, `0x0047c840`, `0x0047e780`, `0x0047e920`, `0x0047e620`, `0x0047e6e0`, overlay `0x004c53d0` | instruction listing |
| `0x004724ec`–`0x00472571` | `0x00409a90`, `0x004c5830` (`0x589`), `_free` | — | on-screen text: inline `strlen` at `0x00472510`, `std::string` build, draw, destroy | instruction listing |
| `0x00472574` | `0x004c5250` | `0xda` | frame `EndScene` | camera note §7 |
| `0x00472585` | `0x00473e10` | `0x15d5` | optional, gated `*0x00608518[0x24] & 1`; body is dominated by 16 `_fwrite` and 45 calls to `0x004e59d0` — a file/report path, **not** confirmed as HUD | call histogram + gate |
| `0x004725c4` | `0x00476140` | `0x250f` | optional, gated `& 2`; 18 `_strncmp`, 12 `_malloc` | call histogram + gate |
| `0x004725e5` | `_free` | — | releases the view array | CRT symbol |

Phases the brief asks about that are **not** in this routine (they are main-loop
level and already stamped): input/window messages `0x004d34b0` (`game_phase_pump`),
script VM `0x0049f770 → 0x004a26a0` (`game_phase_pending_vm`), deferred callbacks
`0x004b0e00` (`game_phase_clock`), audio channel maintenance `0x0049a130`
(`game_phase_channels`), media service `0x00498370` (`game_phase_services`),
game-object/AI simulation `0x00416750` (`game_phase_simulation`, reaches
`0x00414cf0`, size `0x1a36`), cockpit/HUD update `0x0041cde0 → 0x004205e0`
(size `0x10f9`, `game_phase_cockpits`), Present `0x004e3e70 → 0x004dac30`
(`game_phase_present_begin/end`).

## 2. Render submission: who iterates and who sets state

```
0x004722b5  0x0047e6e0   walk sorted queue *0x00608518+0x40  (per entry)
                 0x004bdee0   world matrix for the entry
                 0x004f66e0   visibility/bounds predicate; true -> node[0x12c] |= 0x2000
                 0x004c4fc0   submit one node
0x00472295  0x0047e920   traversal driver for one (view, layer)
                 0x0047d9c0   RECURSIVE scene-graph traversal (self-calls 0x0047e5e5, 0x0047e600)
                      0x004f66e0 / 0x004bdee0 / 0x0047d5e0   cull + transform
                      0x004c4fc0 (0x0047e076)                submit one node
0x004c4fc0  (size 0x287)  per-node submit
                 0x004c5228   0x004c0150   <-- object_trace's existing patch site
0x004c0150  (size 0x3fa3, 332 calls, 148 indirect)  per-node material draw
```

`0x0047d9c0` is the object iterator (recursive, two self-calls);
`0x004c0150` is the per-object/per-sub-mesh state setter. Callers were taken
from Ghidra references: `0x004c0150` has exactly one caller, `0x004c5228` inside
`0x004c4fc0`; `0x004c4fc0` has two, `0x0047e769` and `0x0047e076`; `0x0047d9c0`
has four, two of them its own recursion.

### Why ≈66 device state calls per draw

`0x004c0150`'s body is one loop, `0x004c0223` … `0x004c4082`
(`jl 0x4c0223`, span `0x3e5f`), over the node's sub-meshes
(`movsx edx, WORD PTR [ecx+8]` at `0x004c4072`). Inside one iteration:

- `0x004c1ebe` `ID3DXEffect::Begin(&passes, 1)` — slot 63, flag
  `D3DXFX_DONOTSAVESTATE`, so d3dx9 saves no state block.
- `0x004c3ff0` … `0x004c405b`: `BeginPass(i)` (slot 64) → draw at `0x004c403c`
  (five pushes through vtable `+0x148` of an unidentified class, see the camera
  note §9) → `EndPass` (slot 66); `0x004c4066` `End` (slot 67).
- Classified by vtable displacement, the callsites reached **after** the
  material-initialisation guard (`0x004c1eab` onward) are: `SetInt` 15,
  `SetVector` 13, `SetBool` 10, `SetFloat` 10, `SetMatrix` 5, `GetBool` 4,
  `ApplyParameterBlock` 2, `GetParameterByName` 2, `GetInt` 1, plus 22 calls to
  the cached texture setter `0x004b9ed0` (handle argument, epoch compared
  against `*0x00609024`, then slot 52 `SetTexture`).
- The callsites **before** `0x004c1eab` are the material-initialisation path:
  58 `GetParameterByName`, 34 calls to `0x004b8f70`, 21 to `0x004b9010`,
  6 to the effect-cache lookup `0x004bb0f0`, 2 `BeginParameterBlock` /
  2 `EndParameterBlock`, `SetTechnique`, `FindNextValidTechnique`. Two guards
  skip it: `0x004c0c64 cmp [edi+0x34],esi; jne 0x004c1eab` and
  `0x004c0dea cmp [edi+0x2c],esi; jne 0x004c1eab`. **Inference:** in steady
  state this block runs once per material, not per draw. The guards are direct
  evidence; that every path into `0x004c0c51` is guarded was not proven.

`0x004b8f70` and `0x004b9010` are the by-name setters and do **no** caching:
each call is `GetParameterByName(this,0,name)` (slot 9) → `GetCurrentTechnique`
(slot 59, `+0xec`) → `IsParameterUsed` (slot 62, `+0xf8`) → `SetInt` (slot 26,
`+0x68`) or `SetFloat` (slot 30, `+0x78`). ABI: `ECX` = name pointer,
`ESI` = effect, value on the stack.

**The device-level state traffic does not come from X3AP.exe.** The game
installs its own `ID3DXEffectStateManager`: vtable `0x00562a8c`, constructed at
`0x004b4886`/`0x004b48dd` (`[obj] = 0x562a8c`, `[obj+4] = device`,
`[obj+8] = 1`, then device `AddRef`), and handed to the effect by the image's
single `ID3DXEffect::SetStateManager` callsite (slot 71, displacement `0x11c`)
at `0x004bb07e` inside `0x004bae10`, on the effect-load path
`0x004bb0f0 → 0x004bae10`. Slots resolved by their forwarding displacement:

| Slot | Function | Device displacement | Device method |
| --- | --- | --- | --- |
| 3 | `0x004b4b30` | `+0xb0` | `SetTransform` |
| 7 | `0x004b49f0` | `+0xe4` | `SetRenderState` |
| 8 | `0x004b4a50` | `+0x104` | `SetTexture` |
| 9 | `0x004b4a30` | `+0x10c` | `SetTextureStageState` |
| 10 | `0x004b4a10` | `+0x114` | `SetSamplerState` |
| 13 | `0x004b4a70` | `+0x170` | `SetVertexShader` |
| 14 | `0x004b4bd0` | `+0x178` | `SetVertexShaderConstantF` |

Every one of these is the same six-instruction body: load `[this+4]`, overwrite
the `this` slot on the stack with the device pointer, tail-`jmp` through the
device vtable. **No redundancy filter, no cached shadow state.** So each render
state, texture, stage state, sampler state, transform and shader constant that
d3dx9_37 decides to apply for a pass becomes one game thunk plus one proxy
dispatch plus one Wine/D3D call. Combined with the per-sub-mesh
`Begin`/`BeginPass`/`EndPass`/`End` and up to ~75 effect parameter writes above,
that is a sufficient static explanation for ≈66 device state calls per draw; the
exact 66 is a run-87 measurement and has not been reproduced statically.

The engine's *own* direct `SetRenderState` callsites are 17 image-wide
(Ghidra `disp:0xe4` sweep) and **none** of them is on the material path: they
are in the fixed-function overlay routines `0x004c53d0`, `0x004c55d0`,
`0x004c5830` and in the state-manager thunks themselves.

## 3. What scales with object count

| Phase | Interval | Scaling | Evidence |
| --- | --- | --- | --- |
| Scene-node transform update | `0x00472044` → `0x004720b5` | linear in scene nodes; recursion depth = tree depth | `0x0047b680` self-call, driving loop at `0x00472064` |
| View build + view sort | `0x004720b5` → `0x00472186` | linear/`n log n` in **views** (3+), constant in objects | `0x004720ca` loop, `_qsort` at `0x00472181` |
| Traversal + cull | inside `0x00472280`…`0x004722c6` | **super-linear in queued objects** | `0x0047d9c0` walks `view[0x2a0]` linearly per node (`0x0047e26e`–`0x0047e283`) and `malloc(0x70)` on a miss (`0x0047e285`) |
| Draw-queue sort | `0x004722af`, `0x0047248b` | **O(n²)** in queue length | `0x0047e620` is a linked-list bubble sort: adjacent-swap splice at `0x0047e65e`–`0x0047e677`, restart from head `[edi+0x40]` while any swap occurred (`0x0047e682`–`0x0047e68a`); two directions selected by `view[0x270] & 0x80` |
| Submission | `0x004722b5`, `0x00472491` | linear in queue length × sub-meshes × passes | `0x0047e6e0` list walk; `0x004c0150` sub-mesh loop; pass loop `0x004c3ff0` |
| Env-map pass | `0x00472210` | 6 × full traversal when enabled | camera note §7 |
| Particles | `0x00472307` | linear in particles | `0x004bf4c0` |
| Overlays / text / `EndScene` / optional tails | `0x0047238d` → end | constant in object count | fixed-function paths, `0x004c53d0` / `0x004c5830` |

So a busy-scene regression should show up almost entirely in the
`0x00472270` → `0x004722c8` interval (traversal + sort + submission), with the
`0x00472044` → `0x004720b5` interval growing linearly and everything else flat.
That is the first discriminator the instrumented run gives.

## 4. Proposed stamp sites

Same contract as the loading/main-loop markers: mid-function `engine_patch`
claims, whole instructions, ≥ 5 displaced bytes, all incoming edges landing
exactly on the site start, relative branches inside the displaced span re-based.
Bytes below are the file bytes read at the preferred VA; the ledger format
matches `verification/probe/verify_game_phase_sites.py`
(`name, address, bytes, containing routine, rel32 destination`). Containing
routine for every site is `FRAME = (0x471f50, 0x47260b)`.

### Core set — 7 single boundaries, all exactly once per frame

| # | Name | Address | Bytes | rel32 dest / field | Interval to the next boundary |
| --- | --- | --- | --- | --- | --- |
| 1 | `frame_prologue` | `0x00471f6c` | `e84f300800` | `0x4f4fc0` / off 1 | `0x004f4fc0`, view count scan, `malloc`+`memset` of the view array |
| 2 | `frame_scene_update` | `0x00472044` | `e877f4ffff` | `0x4714c0` / off 1 | lens-flare setup, recursive node update `0x0047b680`, node-flag reset loop |
| 3 | `frame_begin_scene` | `0x004720b5` | `a13c8b6000` | none | `BeginScene`, per-view update loop (`0x0047c3d0`, `0x004be7d0`, `0x0046c0f0`), view `_qsort` |
| 4 | `frame_views` | `0x00472186` | `33db83c410` | none | the **whole per-view loop**: env map, per-view setup, traversal, sort, submission, particles, and the existing scene-end hook at `0x004721b1` |
| 5 | `frame_overlays` | `0x0047238d` | `33db395c2418` | none | second view pass (2D overlays) and the cockpit view (`0x00471660` … `0x004c53d0`) |
| 6 | `frame_text` | `0x004724ec` | `a118856000` | none | on-screen text: inline `strlen`, `std::string`, `0x004c5830`, `_free` |
| 7 | `frame_scene_end` | `0x00472574` | `e8d72c0500` | `0x4c5250` / off 1 | frame `EndScene`, then the two option-gated tails and the view-array `free`; closed by the existing `game_phase_post_render` at `0x00403f39` |

The interval before site 1 is closed by the existing `game_phase_render`
(`0x00403f34`), so the seven sites plus two existing markers partition the whole
render phase with no gap.

### Optional second tier — 3 accumulating sites inside the per-view loop

All three fire once per view, not once per layer: sites 9 and 10 bracket the
entire layer loop (`0x00472280`–`0x004722c6`) from outside.
They must be recorded as accumulating begin/end pairs, like the existing
`delayed`/`acquisition`/`cold` intervals, not as ordered boundaries.

| # | Name | Address | Bytes | rel32 dest / field | Meaning |
| --- | --- | --- | --- | --- | --- |
| 8 | `view_setup_begin` | `0x0047224c` | `56e84e700100` | `0x4892a0` / off 2 | begin per-view setup (`0x004892a0`, `0x0047bc20`, `0x0047c840`, `0x0047e780`) |
| 9 | `view_submit_begin` | `0x00472270` | `8b461c8b684c` | none | end of setup, begin the layer loop: traversal `0x0047e920`, sort `0x0047e620`, submission `0x0047e6e0` |
| 10 | `view_submit_end` | `0x004722c8` | `8b1518856000` | none | end of the layer loop |

Interval 9→10 is the candidate owner of the unattributed 15 ms; interval 8→9 is
the per-view fixed cost.

### Validation performed

For all ten sites, from the disassembly of `0x00471f50`:

- Displaced spans are whole instructions and ≥ 5 bytes (sites 5, 8, 9, 10 are
  6 bytes; the rest 5).
- No branch in the routine targets the interior of any span. Incoming edges land
  exactly on a site start: site 2 from `0x00471f8d`, `0x00471fd1`, `0x0047201b`;
  site 3 from `0x00472081`; site 5 from `0x00472191`; site 6 from `0x0047216a`,
  `0x00472439`, `0x004724b9`, `0x004724c2`, `0x004724cb`, `0x004724e0`;
  site 7 from `0x004724fb`; site 10 from `0x00472279`. Sites 1, 4, 8, 9 have no
  incoming edge.
- The routine contains no indirect jump and no jump table
  (`grep -c 'jmp    DWORD PTR'` = 0), and a whole-file dword scan found **no**
  data reference to any site address or to any byte inside any span.
- Flag liveness: only site 5 leaves live flags — its `cmp [esp+0x18],ebx` is
  consumed by the `jle` at `0x00472393`, which is outside the span, so the tail
  copy must be the last thing executed before control returns (the existing
  dispatcher's `popfl` precedes the tail, which re-creates the flags). Sites 1,
  2, 3, 4, 6, 7, 8, 9, 10 have no live incoming or outgoing flag dependency
  across the span (next flag consumer is always a `cmp`/`test` after the span).
- Register liveness: `EBX` (0), `EBP`, `ESI`, `EDI` and `ESP` are live across
  every site; the marker must preserve all of them, which `pushfl/pushal` does.
  Site 4's span contains `add esp,0x10` and site 8's contains `push esi`, so the
  tail copy must execute at the game's exact `ESP` — the standard dispatcher
  does, but this is the one contract to re-check before claiming these two.
- Reentrancy: `0x00471f50` is not recursive and is called once per main-loop
  iteration from `0x00403f34`; sites 1–7 fire exactly once per frame. Sites
  8–10 fire once per view (the loop `0x00472197`–`0x00472387`, at least three
  views per the camera note). **No site is on a per-object path**: the per-object
  functions are `0x0047d9c0`, `0x004c4fc0` and `0x004c0150`, and none of them is
  touched.
- Conflicts: `X3M_SCENE_HOOK` owns `0x004721b1`–`0x004721b5` and `object_trace`
  owns `0x004c5228`. Neither overlaps any proposed span; `0x004721b1` lies
  strictly inside interval 4 and inside interval 9→10, so an installed scene
  hook does not invalidate these stamps.

Not validated here: the arena/dispatcher relative-replay check at alternate x86
addresses, which `verify_game_phase_sites.py` performs for the existing group
and must be extended to these sites before any claim.

## 5. FEX-relevant candidates in the render path (evidence only)

1. **Unfiltered effect state manager.** `0x00562a8c` (§2). Every state d3dx9
   applies is forwarded to the device with no redundancy check. This is a COM
   object the game constructs and hands to each effect; replacing or wrapping it
   needs no code patch, only the object the effect holds. Highest-leverage
   candidate: it converts directly into the 30 000 proxy dispatches per frame.
2. **O(n²) draw-queue sort.** `0x0047e620`, bubble sort with list splicing,
   called at `0x004722af` (per view, per layer) and `0x0047248b` (cockpit view).
   Grows quadratically with queue length in exactly the busy-scene case.
3. **Linear per-node cache lookup with allocation.** `0x0047d9c0`
   `0x0047e26e`–`0x0047e283` walks `view[0x2a0]` comparing `[eax+0xc]` to the
   object pointer; a miss does `malloc(0x70)` (`0x0047e285`, retried at
   `0x0047e29c` after `0x004b8b60`), `memset`, then links the node. There is no
   `free` in this function. Cost is O(list length) per traversed node.
4. **Instrumented allocator.** Every such allocation also does four
   read-modify-writes on globals `0x006089fc` (count), `0x006085f4`,
   `0x00608600`, `0x006089f8` (byte totals) — `0x0047e2ae`–`0x0047e2c6`; the
   frame routine's own array does the same at `0x0047201d`–`0x0047202f`.
5. **SEH frame per material draw.** `0x004c0150` pushes an SEH record
   (`push 0xffffffff; push 0x005305b1; mov eax,fs:0; ...; mov fs:0,esp` at
   `0x004c0156`–`0x004c0167`) and a `0x4c8`-byte stack frame, per node, and
   unlinks it at `0x004c4096`. `fs:0` writes are not free under emulation.
6. **Per-draw string work, bounded.** Two `GetParameterByName` calls survive on
   the steady-state path (`0x004c304e`, `0x004c30d8`). The six effect-cache
   lookups `0x004bb0f0` (hash bucket + `_stricmp`, `0x004874c0`) and the two
   `_stricmp` at `0x004c0886`/`0x004c08a5` are on the initialisation path
   (inference, §2). The per-parameter `std::string` dispatch loop
   `0x004c0ec0`–`0x004c134f` (SSO layout test at `0x004c0ecb`, first-character
   compare at `0x004c0ed8`) is also on the initialisation path.
7. **Per-frame temporaries.** Two `malloc`, one `memset`, two `free` in
   `0x00471f50`, plus the text path's inline `strlen` (`0x00472510`) and
   `std::string` construct/destroy (`0x00472524`, `0x0047255f`). Constant cost;
   listed for completeness, not as a suspect.

## 5b. Implemented sites

All ten sites of section 4 are installed by `X3M_FRAME_PHASES=1`
(`src/proxy/frame_phase_sites.h`, runtime `src/proxy/frame_phases.cpp`,
launcher `--frame-phases`, requires `--telemetry`). The ESP re-check for sites
4 and 8 held: the shared game-phase stub restores all registers and the flags
before its `jmp [next]`, the dispatcher is a plain `jmp [entry]`, and the
displaced instructions therefore execute in the tail at the game's exact ESP;
neither site was dropped or moved. The verifier
`verification/probe/verify_frame_phase_sites.py` binds the ledger above to the
installed EXE and adds the relative-replay check at alternate arena addresses
that this note left open, plus the exact incoming-edge sets of the
"Validation performed" list. Schema, phase names and the scaling column are in
`docs/verification/sampling-profiler.md`, "Frame phases".

## 6. What this does not establish

- No runtime measurement. The 53 % attribution is the open question these stamps
  are meant to answer, not a result.
- The executed (as opposed to static) count of effect parameter writes per draw,
  and therefore the exact 66, is unknown; the split between init and steady-state
  paths in §2 is an inference from two guards.
- `0x00473e10` and `0x00476140` are option-gated and their role is inferred from
  their call histograms (`_fwrite`, `_strncmp`); neither was decompiled.
- The class behind vtable slot `0x148` at `0x004c403c` remains unidentified
  (camera note §9), so the submission interval 9→10 cannot yet be split into
  "engine time" and "d3dx9 time" from the EXE alone.
- Queue lengths, view counts and layer counts are all runtime quantities; the
  scaling claims in §3 are structural.

## Reproduce

```sh
JAVA_HOME='/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home' \
  '/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless' \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -readOnly -noanalysis \
  -scriptPath tools/analysis \
  -postScript X3CallTree.java /tmp/out/frame_tree.txt 2 00471f50 00403840 \
  -postScript X3SampleFunctions.java /tmp/out/callers.txt 004c5228 004c0150 0047d9c0
JAVA_HOME=... analyzeHeadless ... -postScript X3CameraState.java /tmp/out/disp.txt \
  disp:0xe4 disp:0x104 disp:0x10c disp:0x114 disp:0x170 disp:0xb0 \
  disp:0x11c data:00562a8c
```

`tools/analysis/X3CallTree.java` (added for this study) prints, per function,
its body size, every call site with the callee's entry/size/name, and the string
literals it references — addresses, sizes and names only, no decompiled or
disassembled code. Byte-level checks used
`i686-w64-mingw32-objdump -D -b binary -m i386 -M intel` on the mapped VA ranges
of the bottle's `X3AP.exe`. Generated output is game-derived and stays untracked.
