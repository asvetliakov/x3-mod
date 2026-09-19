# `view_submit`: where the engine's own CPU time goes, and what is patchable

2026-09-19. Static study of X3AP.exe, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` (the bottle
copy, `shasum -a 256`, re-checked this session). Nothing was launched, no Wine
command was run, no source was edited. Method: `i686-w64-mingw32-objdump -d -M
intel` over the whole image (401,904 decoded lines, kept local and untracked)
with a Python pass that counts instruction classes per address range; Ghidra
headless (`/tmp/x3-ghidra-research/X3Render`, `-readOnly -noanalysis`,
`X3CallTree.java`, depth 2) for function bounds, callee lists and call sites;
the PE import table parsed from the file to name the `d3dx9_37` thunks; a
raw-encoding sweep of `.text` (`e8`/`e9`, `0f 8x`, `70–7f`/`eb`/`e0–e3`) plus a
whole-file abs32 dword scan for the hook-site edge checks. Marks: **[m]**
measured in a named flight or fixture, **[s]** static reading of the image,
**[i]** inference, **[e]** estimate with no measurement behind it.

Owner of the frame-time budget: [engine-frame-time.md](../architecture/engine-frame-time.md).
Owner of the submission call chain and the stamp sites:
[frame-loop-phases.md](frame-loop-phases.md) §2–§4 and
[effect-pass-loop.md](effect-pass-loop.md). This note does not repeat them; it
answers one question: **inside `view_submit`, which engine-side costs are real
and patchable, and how large can each be.**

## 1. What is already attributed, and what is not

Run 46 D (run162, busy station, `frame=4200`, `draws_p50=510`,
`dt_p50_us=27237`) is the only session with `frame_phases`, `pass_phases` and
`frame_timing` together **[m]**:

| Bucket | ms/frame | per draw | Source |
| --- | --- | --- | --- |
| `view_submit` | 18.316 | 35.9 µs | `frame_phases` sites 9→10 |
| — `apply` (`BeginPass` only; no `CommitChanges` exists in `0x004c0150`) | 9.371 | 18.6 µs | `pass_phases`, `passes_p50=503` |
| — `draw` (2 geometry vtable calls + hooked `DrawIndexedPrimitive`) | 5.298 | 10.5 µs | `pass_phases` |
| — `end` (`EndPass`) | 0.062 | 0.12 µs | `pass_phases` |
| — **residual = engine `prepare` + D3DX `setup`** | **3.483** | **6.83 µs** | `view_submit − Σ pass_phases` |
| hooked native state calls inside `apply` | 9.028 | 17.7 µs, 68.6 calls/draw | `frame_timing state_p50_us`, `state_calls_p50=34983` |
| native `DrawIndexedPrimitive` | 1.274 | 2.50 µs | `frame_timing draw_native_p50_us` |

Run113 splits the residual one level further (`--residual-phases`, 827 draws):
`prepare` 6.36 µs/draw, `setup` 1.63 µs/draw (`Begin` + ~75 parameter setters +
2 render-state writes) **[m]**. Run162 did not carry `--residual-phases`, so its
3.483 ms is the two together.

**The correction this note makes to the ranking.** Run 46 D's "engine-between-
calls, 9.95–10.02 ms" is `dt` minus *every hooked D3D9 call* over the **whole
frame** (`frame_timing.cpp:282–295`: `gap = elapsed − hooked ticks` per region),
so it contains `pre_render` (3.495), `view_setup` (1.652), the views residual
(2.931), the overlay/cockpit tail, and every unhooked instruction inside
`d3dx9_37.dll` and `wined3d`. Split by region the same line gives
`gap_pre 3.781 / gap_draw 5.755 / gap_post 0.481` **[m]**. **The engine's own
per-object time inside `view_submit` is the 3.483 ms residual, not 10 ms** —
that is the whole budget every candidate below competes for.

Not attributed anywhere yet: the split of that 3.483 ms between traversal, the
queue sort, the world-matrix build, the per-draw matrix/light block and D3DX's
`End`/`GetTechniqueByName`/`SetTechnique`; the queue and per-view node counts;
and whether any view other than the main one contributes materially.

## 2. The chain, measured by instruction class

Every function on the per-node or per-draw path, with its static instruction
count, x87 density and expensive-opcode content **[s]**. "Rate" is per frame at
the run162 window; `N` = nodes traversed per (view, layer), `D` = 510 draws.

| Address | Size | Role | Rate | insns | x87 | x87 % | fsqrt/fdiv/fptan | calls |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `0x0047e920` | `0x266` | traversal driver for one (view, layer) | per view·layer (5 call sites) | 177 | 13 | 7.3 % | — | 11 |
| `0x0047d9c0` | `0xc54` | recursive scene-graph traversal | per node | 864 | 0 | 0 % | — | 32 |
| `0x0047e620` | `0xc0` | draw-queue sort | per view·layer (3 call sites) | 82 | 0 | 0 % | — | 0 |
| `0x0047e6e0` | `0x9d` | sorted-queue drain | per view·layer (3 call sites) | 54 | 0 | 0 % | — | 3 |
| `0x004bdee0` | `0x503` | world matrix for node × camera | per node **and** per queue entry | 368 (116 on the common path) | 174 (79) | 47 % (68 %) | 3 fdiv/fidiv | 0 |
| `0x0047d5e0` | `0x3d4` | per-node light selection + `qsort` | per node, gated | 280 | 14 | 5 % | — | 4 (incl. `_qsort`) |
| `0x004f66e0` | `0x8d2` | texture-animation stepper | per node and per queue entry | 685 | 5 | 0.7 % | — | 7 |
| `0x0047cfe0` | `0x572` | cull / LOD pass (site `0x0047d2a2`) | per node, once per view | 373 | 4 | 1.1 % | — | 10 |
| `0x004c4fc0` | `0x287` | per-node submit wrapper | per draw | 187 | 37 | 20 % | 1 fdiv | 5 |
| `0x004c0150` | `0x3fa3` | material submission | per draw | 4,691 | 434 | 9.3 % | 2 fsqrt, 5 fdiv, **1 fptan** | 332 |
| `0x0042f970` | `0x66` | int-vector normalise + scale | 3 sites in `0x0047d9c0`, 15 image-wide | 43 | 26 | 61 % | 1 fsqrt, 1 fdiv | 3 (`ftol`) |
| `0x00412440` | `0xf` | out-of-line `sqrtf` | 3× per draw in `0x004c0150` | 7 | 2 | — | 1 fsqrt | 0 |
| `0x0052b5d0` | `0x91` | `_ftol2`: `cvttsd2si` when `[0x006619ec]` is set, else `fnstcw`/`fistp` fallback | 9× per draw | — | — | — | `fnstcw` on the slow path only | 0 |

Regions inside `0x004c0150`, on the steady-state path **[s]**:

| Region | Span | insns | x87 | calls | Contents |
| --- | --- | --- | --- | --- | --- |
| prologue | `0x004c0150`–`0x004c0223` | 56 | 0 | 1 | SEH record, `sub esp,0x4c8`; `SetSoftwareVertexProcessing` is gated on `[[[0x00608b3c]+0x18]+4]+0x6d8 == 2` and is **not** on the normal path (corrects the reading in effect-pass-loop.md §1) |
| sub-mesh head → guard 1 | `0x004c0223`–`0x004c0c6d` | 764 | 0 | 28 | three engine-wrapper binds, `GetTechniqueByName` (`0x004c0bfa`), `FindNextValidTechnique` on a miss, `SetTechnique` (`0x004c0c34`) |
| material init block | `0x004c0c6d`–`0x004c1eab` | 1,368 | 99 | 161 | skipped in steady state by the two guards |
| **per-draw D3DX setup** | `0x004c1eab`–`0x004c3ff0` | 2,420 | 335 (13.8 %) | **136** (71 vtable + 65 direct) | `Begin`, the matrix block, the light block, ~75 parameter setters |
| pass loop | `0x004c3ff0`–`0x004c4070` | 45 | 0 | 6 | `BeginPass` → `DrawIndexedPrimitive` → `EndPass` |

Direct callees of the per-draw setup block, by count **[s]**: cached texture
setter `0x004b9ed0` ×22, `_ftol2` `0x0052b5d0` ×9, texture-handle validity
`0x004f5280` ×9, **`D3DXMatrixMultiply` ×4**, `sqrtf` `0x00412440` ×3, `_memset`
×2, **`D3DXMatrixInverse` ×2**, **`D3DXMatrixTranspose` ×1**.

Import thunks resolved from the PE import table this session **[s]**:
`0x004faf00` `D3DXMatrixMultiply`, `0x004faf06` `D3DXVec3Transform`,
`0x004faf0c` `D3DXMatrixInverse`, `0x004faf1e` `D3DXMatrixTranspose`,
`0x004faef4` `D3DXVec3Normalize`, `0x004faefa` `D3DXVec3Project`.
Image-wide call sites: `MatrixMultiply` 8 (four of them at `0x004c21ce`,
`0x004c21f2`, `0x004c22b5`, `0x004c22db`, all inside the per-draw block),
`MatrixInverse` 2 (`0x004c2251`, `0x004c2316`, **both per draw**),
`MatrixTranspose` 1 (`0x004c2266`, per draw).

## 3. The FEX cost model this note uses

The project's own measurements are the only anchor, and they cut against the
brief's premise **[m]**:

- `sector-collide.md` §13.6: with `FEX_X87REDUCEDPRECISION=1` the engine's x87
  helper chains "run as host double arithmetic and cost what the same sums cost
  in scalar SSE2"; the SSE2 rewrite of the whole BVH descent measured **31.0 ns
  per visit against 31.4 ns** for the engine's own descent, i.e. no gain.
- The 6.1×/9.1× that the SAT replacement did win (§12.8: 121.5 → 19.8 ns full
  overlap, 53.9 → 5.9 ns early separation) came from **doing ~6× fewer
  instructions** — packed axes, `andps` instead of 24 out-of-line `call fabs`,
  no `fcompp`/`fnstsw` — not from a per-instruction x87 penalty.
- Implied rate for x87-dense engine code: 121.5 ns / ~556 instructions ≈
  **0.22 ns per instruction**; 53.9 / ~149 ≈ 0.36 **[i]**.

Applying that rate to §2: the engine executes roughly **2,500–3,500 x86
instructions per draw** across `0x004bdee0`, `0x004c4fc0`, the traversal step
and the non-D3DX parts of `0x004c0150` **[s]**, i.e. **0.6–1.3 µs/draw =
0.3–0.65 ms/frame** of pure instruction retirement **[i]**. Against the measured
3.483 ms residual that leaves **2.8–3.2 ms/frame that is not the engine's
arithmetic**: the 136 out-of-line calls per draw (each a cross-module dispatch
FEX must route through another JIT block), D3DX's own `End`,
`GetTechniqueByName` and `SetTechnique`, dependent-load list walks, and the
`0x4c8`-byte frame + SEH link per node.

**Consequence for the brief's question: an SSE2 replacement of any single x87
routine on this path is bounded, in total, by the 0.3–0.65 ms that all engine
arithmetic in `view_submit` costs.** The levers with real headroom are call
count, redundant recomputation and list-walk complexity.

## 4. Redundant or superlinear work found

| # | Site | What is redundant | Rate |
| --- | --- | --- | --- |
| R1 | `0x004c2316` | `D3DXMatrixInverse(scratch, NULL, *0x00608a40)` — the **view** inverse, a per-view constant, recomputed per draw (already named in [camera-state-and-frame-routine.md](camera-state-and-frame-routine.md) §1) | ≤ 1 per draw, gated on `[edi+0x50] != 0` |
| R2 | `0x004c3bcc`–`0x004c3c8c` | a projected-size parameter: `sqrtf` (out-of-line `0x00412440`) of an int triple, `_ftol2`, one `fdivr`, then `fild [edi+0x298] (camera FOV) → ×2⁻¹⁶ → ×const → fptan → fdivp` and a clamp, then `SetFloat`. The `tan(fov/2)` factor depends only on the camera FOV — **a per-view constant recomputed per draw**, and it is the only transcendental on the whole per-draw path | ≤ 1 per draw, gated on `[esi+0x150] != 0` |
| R3 | `0x004c0bfa` / `0x004c0c34` | `ID3DXEffect::GetTechniqueByName` (slot 13) followed by `SetTechnique` (slot 58) **unconditionally per draw**, before the two guards that skip the material-init block; consecutive draws of the same material re-set the same technique | 1 per draw each |
| R4 | `0x0047e26e`–`0x0047e283` | the per-node cache lookup is a **linear walk** of `view[0x2a0]` comparing `[eax+0xc]` to the node pointer; a miss does `malloc(0x70)` (`0x0047e285`, retried at `0x0047e29c`), four read-modify-writes on the allocator counters `0x006089fc`/`0x006085f4`/`0x00608600`/`0x006089f8`, `_memset`, then links — **and there is no `free` in the function**. Cost is O(list length) per traversed node, O(N²) per view | per node |
| R5 | `0x0047e620` | draw-queue sort. Decoded this session: a **bubble sort with a last-swap sentinel** (`edx` holds the last swapped node; the next pass restarts from the head `[edi+0x40]` but stops at `esi = [edx]`), not the plain restart-from-head sort described in engine-frame-time.md §2.5. **The comparator is a plain signed 32-bit compare of `[entry+0x14]`** — no call, no float — ascending when `view[0x270] & 0x80`, descending otherwise. `[entry+0x14]` is written at `0x0047e0ff` from the part's `[part+0x28]` depth key | per view·layer, 3 call sites (`0x004722af`, `0x0047248b`, **`0x0047e8f0`** — the env-map driver, which §2.5 does not list) |
| R6 | `0x004c23af`, `0x004c2568` | two integer-triple normalisations per draw, each `fild`×3 → dot → `fsqrt` → `fld1; fdivrp` → three `_ftol2` calls. Same shape as the standalone helper `0x0042f970` | 2 per draw, each gated |
| R7 | `0x0047d5e0` | per-node light selection: walks the light chain from `[0x00608518+0x5e8c]`, per light a 3-term x87 dot plus `_ftol2`, fills 12-byte records at `[0x00608518+0x628c]`, then **`_qsort` with comparator `0x0047d5b0`** — a CRT `qsort` with an indirect comparator per node. Gated by `[node+0xa0] >= 2` **and** `0x00488170(node,0) >= 2`; how often that passes is unknown | per node when the gate passes |
| R8 | `0x004bdee0` | the world matrix is rebuilt from the node's 16.16 fixed-point basis on **both** paths — per traversed node (`0x0047e002`) **and** again per queued draw entry (`0x0047e70c`) for deferred parts. 11 `imul`/`shrd`/`adc` fixed-point scalings, 31 `fild`, 37 `fmul`, 62 `fst`/`fstp`, one `fidiv`, one `fdivrp` | ≈ N + D per frame |

`0x0047b2e0`, the deferred-entry allocator, is a **free-list pool** that calls
`_malloc(0x1c)` only when the list is empty, so it is not a per-draw allocation
after warm-up **[s]**. The only per-node allocation on this path is R4's
`malloc(0x70)`.

## 5. Ranked candidates

Estimates are per busy frame at 510 draws. Every ms figure marked **[e]** has no
measurement behind it; §6 says what would supply one.

### 5.1 R4 — replace the per-node linear cache walk — 0.3–2 ms [e], engine trampoline

Largest plausible single item, and the only superlinear one that grows with the
scene. At `N` traversed nodes the walk costs `N²/2` dependent pointer loads of
scattered `0x70`-byte heap records; at `N ≈ 900` that is ~405,000 iterations ×
5 instructions, 0.45 ms at the §3 rate and more if the chase misses cache
**[e]**. The fix is a side hash keyed on the node pointer, or a generation-
stamped slot in the node, maintained by a trampoline that answers the lookup and
falls through to the engine on a miss.

**Hook site.** `0x0047e264`, bytes `8b 87 a0 02 00 00 89 4c 24 18` — two whole
instructions (`mov eax,[edi+0x2a0]`; `mov [esp+0x18],ecx`), 10 bytes, a plain
copy with no relative transfer, so the arena tail is byte-identical at any
address. Exactly **one** inbound edge, `jmp 0x0047e264` at `0x0047e253`, landing
on the span start; nothing targets `+1..+4` in the raw-encoding sweep of
`.text`; no abs32 reference to the address anywhere in the file. Live across the
span: `EBX` (the node), `EDI` (the view), `ESI`, `EBP`, `ESP`; `ECX`/`EAX` are
produced inside the span. Flags dead on entry (`mov`), and the next consumer is
the `test ecx,ecx` after the span. The displaced `mov [esp+0x18],ecx` writes the
game's frame, so the tail must run at the game's exact ESP — the contract
already proven for frame sites 4 and 8. Re-entrancy: `0x0047d9c0` **is**
recursive (self-calls `0x0047e5e5`, `0x0047e600`), so the handler must be
re-entrant on the same thread; it is single-threaded (frame-loop-phases.md §1).
Do **not** claim at `0x0047e26e`: those bytes (`8b 08 85 c9 74 11`) contain a
rel8 `je` that would need re-basing.

### 5.2 R3 — skip `SetTechnique` when the technique is unchanged — 0.05–1.5 ms [e], engine trampoline

The technique lookup itself is measured and negligible (8.5 ns per draw,
offline; engine-frame-time.md §4 puts a handle cache at 0.007 ms/frame), but
`SetTechnique` is a *different* call and has never been measured. It is issued
once per draw, before the guards, and native D3DX re-resolves the active
technique on it. A compare-and-skip trampoline is exact: call
`GetTechniqueByName`, compare the handle with the last one set on that effect,
and jump over the `SetTechnique` dispatch when equal.

**Hook site — preferred: `0x004c0c2a`**, bytes `8b 0b 8b 91 e8 00 00 00` — two
whole instructions (`mov ecx,[ebx]`; `mov edx,[ecx+0xe8]`, the `SetTechnique`
vtable load), 8 bytes, a plain copy. Exactly **one** inbound edge, the `jne
0x004c0c2a` at `0x004c0c05` (the "name hit" path), landing on the start; nothing
into `+1..+4`; no abs32 reference. `EAX` already holds the technique handle at
this point (either from `GetTechniqueByName` or from `[esp+0x98]` after
`FindNextValidTechnique`), `EBX` is the effect, so the stub has both operands it
needs: compare against the per-effect cache and, when equal, jump to
`0x004c0c36` instead of falling through to the two pushes and the `call edx` at
`0x004c0c34`. Live across: `EBX`, `EDI` (the material block), `ESI` (0), `EBP`,
`EAX`. Flags dead on entry (the `jne` that lands here consumed them) and dead
out (the next consumer is `cmp [edi+0x1a4],esi` at `0x004c0c36`, after the
span). Not re-entrant: `0x004c0150` has one caller and D3DX only calls the
game's state manager, never back into it (effect-pass-loop.md §1).
The alternative `0x004c0c34` (`ff d2 39 b7 a4 01 00 00`, `call edx`;
`cmp [edi+0x1a4],esi`, 8 bytes, two whole instructions) displaces the dispatch
itself and needs the tail to re-issue it at the game's exact ESP with both
arguments already pushed — strictly harder for no benefit. Conflicts: none;
`0x004c0150` already hosts the point-light claim (`0x004c27af`–`0x004c27b5`)
and, when enabled, the four pass stamps and the residual stamp, all disjoint.

### 5.3 R1 + R2 — cache the two per-view constants — 0.08–0.35 ms [e], engine trampoline

`D3DXMatrixInverse` of the view matrix (R1) and `tan(fov/2)` (R2) are both
per-view constants evaluated per draw. Both are exact to cache: the view matrix
buffer `*0x00608a40` is written only by `0x004be520` (camera-state note §1), and
the FOV is `camera+0x298`. A 64-byte compare (4 SSE2 loads) or a generation
counter set from the view builder decides validity.

**Hook sites.** `0x004c2316`, bytes `e8 f1 8b 03 00` — a plain `call rel32` to
the `D3DXMatrixInverse` thunk `0x004faf0c`, the ideal `claim_call` shape; no
inbound edge, nothing into `+1..+4`, no abs32 reference. Stdcall: three
arguments already pushed (`out = esp+0x418` before the pushes, `NULL`,
`src = *0x00608a40`), callee pops 12, so a replacement must pop 12 as well.
**The x87 stack is empty at the site**: the routine keeps `2⁻¹⁶` live in `st(0)`
between blocks and pops it with `fstp st(0)` at `0x004c2309`, reloading it with
`fld [0x005654e0]` at `0x004c2332`; a replacement must leave the stack empty.
Live across: `EBX` (effect), `EDI` (material block), `EBP`, `ESI`; `EAX`/`ECX`/
`EDX` are dead (all reloaded at `0x004c231b`). Flags dead. `0x004c2251` (the
world-basis inverse feeding `D3DXMatrixTranspose` at `0x004c2266`) has the same
shape (`e8 b6 8c 03 00`, no inbound edge, no abs32) but is **not** cacheable —
its source `*0x00608a48` changes per node.
For R2 the site is `0x004c3bcc`, bytes `8b 45 0c 8b 7d 10` — two whole
instructions, 6 bytes, plain copy; four inbound edges (`0x004c3b95`,
`0x004c3bac`, `0x004c3bb0`, `0x004c3bb7`), **all landing on the span start**;
nothing into the interior; no abs32. Live: `EBX`, `ESI`, `EBP`; `EDI`/`EAX`/
`ECX`/`EDX` are reloaded inside the block. The block's own x87 use is balanced —
traced instruction by instruction from `0x004c3be8` to the `fstp [esp]` at
`0x004c3c87`, every push is matched — but **whether a caller-held x87 value is
live in `st(0)` across the site was not established** (the routine does keep
`2⁻¹⁶` live in `st(0)` between other blocks and reloads it from `0x005654e0`
after each `SetMatrix`). A claim here must therefore preserve the x87 stack, not
merely leave it balanced, which argues for caching only the `fptan` result
rather than replacing the block.

### 5.4 R5 — replace the draw-queue sort — 0–1 ms [i], engine trampoline

Unchanged in size from engine-frame-time.md §2.5, but now implementable: the
comparator is `signed int32 [entry+0x14]`, two directions by `view[0x270] &
0x80`, and the list is doubly linked through `[entry]`/`[entry+4]` with a
sentinel at `view_root+0x44`. A merge sort on the same links is a drop-in. The
sentinel-bounded bubble sort is already near-linear on a nearly sorted queue, so
the win only appears when the queue arrives badly ordered.

**Hook site.** `0x0047e620`, bytes `f6 80 70 02 00 00 80` — one whole
instruction (`test byte ptr [eax+0x270],0x80`), 7 bytes, plain copy. Three
inbound edges, all `call rel32` (`0x004722af`, `0x0047248b`, `0x0047e8f0`), all
on the start; the one raw-encoding hit inside the span (`0x0047e622` from
`0x0047e626`) is a false positive — `0x0047e626` is not an instruction start, it
is the `0x80` immediate of the `test`. No abs32 reference. ABI: `EAX` = the view
on entry (`test byte ptr [eax+0x270],0x80` is the first instruction), no stack
arguments and no `ret n`, `EBX`/`ESI`/`EDI` pushed and popped by the routine
itself, **no x87 instruction and no call of any kind in the body**, two exits
(`0x0047e68f`, `0x0047e6df`), each `pop edi; pop esi; pop ebx; ret`. Not
re-entrant and not recursive.

### 5.5 R8 — SSE2 world matrix `0x004bdee0` — **closed at ≤ 0.08 ms [e]**

The routine is the most x87-dense on the path (47 % overall, 68 % on the common
`0x004be253` path) and it is called ≈ `N + D` times per frame, which is why it
looks like the obvious SSE2 target. It is not: the common path is 116
instructions, so at the §3 rate the whole routine costs ~25–40 ns per call and
**0.03–0.08 ms per frame in total** even before a replacement. §13.6 of the
collide note measured exactly this class of code (`fild`/`fmul`/`fstp` chains
with no `fabs` calls and no `fcompp`) at parity with scalar SSE2. Recorded here
so it is not re-proposed.

**If it is built anyway**, the site is the entry `0x004bdee0`, bytes
`83 ec 08 83 78 3c 00` — two whole instructions (`sub esp,8`;
`cmp [eax+0x3c],0`), 7 bytes, plain copy; exactly two inbound edges, both
`call rel32` (`0x0047e002`, `0x0047e70c`), both on the start; no abs32
reference. ABI, from the two call sites and the epilogue: `EAX` = context/scale
object, two stack arguments (node, view) with the **caller** popping 8 (`add
esp,0x10` at `0x0047e01a` covers four pushes, `add esp,0x10` at `0x0047e727`
likewise), `EBX`/`EBP`/`ESI`/`EDI` saved and restored, returns `EAX` = 0 or 1,
writes only the five matrix buffers `*0x00608a38`…`*0x00608a48`, and leaves the
x87 stack empty on both exits.

### 5.6 R6, R7 — the per-draw normalisations and the per-node light `qsort` — unsized

R6 is two ~20-instruction x87 blocks per draw, ≈ 0.01 ms/frame at the §3 rate
**[e]** — not a lever. R7 cannot be sized at all without knowing how often its
two gates pass and how long the light list is; a `qsort` with an indirect
comparator per node would be worth attention if it fires on every node, and
worth nothing if it fires on a handful. One counter settles it (§6).

### 5.7 What this leaves as the real shape of `view_submit`

Of the 18.3 ms: 9.0 ms is native state calls the proxy owns
(`state-call-fast-path.md`), 1.3 ms is the native draw, 3.9 ms is the proxy's
per-draw hook, and **3.5 ms is engine plus unhooked D3DX**, of which at most
0.65 ms is engine arithmetic. Draw-count reduction (`--cull-small-parts`, 403
sub-2 px draws = 9.6 ms at the run131 census) remains an order of magnitude
larger than every candidate in this section.

## 6. What run 47 session B3 would confirm

`--profile --profile-interval-us 500` at the busy view gives ~54 samples per
27 ms frame per thread; over 60 s that is ~120,000 main-thread samples, enough
to resolve a 0.3 ms/frame item at ~1 % of samples. **But the profiler has
already been measured blind under FEX once** (run84: "every leaf is the ntdll
syscall thunk or an unattributed sentinel", sampling-profiler.md), so each row
below names a stamp/counter fallback that does not depend on it.

| Candidate | Profiler evidence that confirms it | Fallback if the leaves are again unattributed |
| --- | --- | --- |
| R4 cache walk (5.1) | `profile_leaf` RVAs in `0x7e26e`–`0x7e283` (the walk) and `0x7e285`–`0x7e2d5` (the miss path) as a share of main-thread `leaf_x3ap`; `profile_frame` RVA `0x7d9c0` carrying them | a two-counter stub at `0x0047e264`: nodes looked up and total iterations walked, one `frame_end` row — gives `N` and the mean walk length directly |
| R3 `SetTechnique` (5.2) | `profile_leaf` in the `d3dx` module with `profile_frame` RVA `0xc0c36` (the return address of the `SetTechnique` dispatch) or `0xc0bfc` (`GetTechniqueByName`'s) — the frame table is exactly designed to charge DLL time to the calling engine function | a stamp pair `0x004c0bf3 → 0x004c0c36` on the existing lean stub (2 dispatches per draw, ~91 ns each, ≈ 0.09 ms/frame self-cost) |
| R1/R2 per-view constants (5.3) | `profile_leaf` in `d3dx` with `profile_frame` RVA `0x0c231b` (after `D3DXMatrixInverse`); for R2 a leaf at `0x0c3c5c` (the `fptan`) at all — a single sample there is already informative, since one instruction is a wide target only if it is slow | `--residual-phases` in the same session: `setup` per draw against run113's 1.63 µs bounds the whole per-draw D3DX block, R1+R2 included |
| R5 queue sort (5.4) | `profile_leaf` RVAs inside `0x7e640`–`0x7e6dc` / `0x7e6a0`–`0x7e6d2` (the two direction loops) | the `sort_us` stamp pair on `0x004722af` that engine-frame-time.md §2.5 already asks for, plus a queue-length counter at the sort entry |
| R7 per-node light `qsort` (5.6) | `profile_frame` RVA `0x7d5e0` and `profile_pair` (`0x7d5e0`, `0x7d9f6`); leaves inside `_qsort` `0x110510` | one counter at `0x0047d5e0` for entries and one at `0x0047d9a3` for `qsort` calls, with the element count |
| R8 world matrix (5.5) | `profile_leaf` share inside `0xbdee0`–`0xbe3e3`. **If this is below ~2 % of main-thread samples the candidate is closed for good** | `--residual-phases` `prepare` per draw; the routine is inside it |

**The fallback column is implemented as `--submit-phases`**
(`X3M_SUBMIT_PHASES=1`, default off, requires `--telemetry --frame-phases`):
twenty-two byte-verified stamps on R5 (sort time, calls, queue length), R4 (walk
time, misses, sampled iterations), R3 (`SetTechnique` and `End`), the
`0x004c1eab`-`0x004c3ff0` block, both `D3DXMatrixInverse` calls, `0x004c0150`
and `0x004bdee0`, one `submit_phases` row per 300 frames. Sites, row format,
dispatch budget (~10,800 per busy frame, ~1.1 ms) and qualification are in
[sampling-profiler.md](../verification/sampling-profiler.md), "Submit phases".
Three sites differ from section 5's assessment because the real bytes forced
it: the sort closes at its callers' returns (4-byte epilogues), the walk closes
at `0x0047e285` / `0x0047e315` (`0x0047e350` is 4 bytes with a rel8 jump), and
`0x004c0150` closes at `0x004c5230` (the caller's edge `0x004c502a` lands behind
the `add esp,0x18`). R7 is not stamped.

Two things to read from the same log regardless of which candidate survives:
`profile_thread leaf_x3ap` vs `leaf_d3dx` vs `leaf_wine` for the main thread —
that single ratio decides whether the 3.5 ms residual is engine code or
`d3dx9_37` at all, and it is the cheapest possible test of §3's conclusion — and
`frame_phases` + `pass_phases` + `residual_phases` together, so the residual
splits into `prepare` and `setup` in the same window as the profiler samples.

## 7. Not established

- No runtime measurement of any kind in this note. Every ms figure marked
  **[e]** is arithmetic on the §3 rate, which is itself derived from a
  *different* routine's fixture.
- The executed (as opposed to static) instruction count per draw: the
  `0x004c0150` regions in §2 are branchy and the counts are upper bounds. How
  often each of the gated blocks (R1, R2, R6, the WVP path at `0x004c21a2`)
  actually runs depends on which parameters each material declares, which is
  effect data, not code.
- `N`, the number of traversed nodes per (view, layer), and the queue length per
  layer. Every superlinear claim (R4, R5) is structural until those are counted.
- Whether `0x0047d5e0`'s two gates pass often; what `[node+0xa0]` and
  `0x00488170`'s return value count.
- The cost of `ID3DXEffect::SetTechnique`, `End` and `GetTechniqueByName` in
  `d3dx9_37.dll` under FEX; only the by-name lookup has ever been measured.
- Which `d3dx9_37` serves the calls (Microsoft's or Wine's builtin) — still open
  from effect-pass-loop.md §6.
- The classes behind `[[E+0x28]+0x14]` slots 4 and 5 in the pass loop, and
  `0x00488170`'s role; unchanged from the earlier notes.

## 8. Run 47 B — session B3 profiler results (2026-09-19)

`/tmp/x3-bottleX3-run167`, `--motion-rt-mode lazy --profile --profile-interval-us 500` (no
`--frame-timing`), busy station view, ~60 s held, user-observed 45-48 fps. `frame_end` confirms
9254 frames / 197,770 ms = 21.37 ms/frame = 46.8 fps, matching the user's report.

**Profiler health.** `profile_report` deltas: 39 reports, 1,497,596 samples over 195.19 s = 7,672
samples/s; `ticks=` sum / elapsed = 487 ticks/s against the nominal 2,000 (`interval_us=500`), i.e. the
sampler is running at about a quarter of its requested rate as the discovered-thread count grows (9 to 19
threads; `tick_us_mean` 252-882 us, `tick_us_max` up to 65,130 us). `dropped=0` in every report (no leaf-table
overflow). `suspend_failures` sums to 8,041 across `profile_thread` deltas, concentrated on a few worker
threads (e.g. tid 452 saw 362 failures in one delta among threads with otherwise clean rows); `context_failures`
is 0 throughout.

**Leaf table is blind under FEX again, exactly as run84 found.** Summed over every `profile_thread` delta
(1,497,596 samples total): `leaf_x3ap=0`, `leaf_wine=0`, `leaf_d3dx=0`, `leaf_proxy=0`, `leaf_zlib=0`,
`leaf_xml=0`, `leaf_ntdll=1,218,757` (81.4%), `leaf_other=278,839` (18.6%, unresolved/no pinned module) — this
holds for every thread including tid 216 (`init_tid`, `start_module=0` = X3AP.exe, the process's first D3D
thread). `profile_leaf` lines corroborate this directly: aggregated by module, every leaf line is either
`module=1 name=ntdll.dll` (3,367,565 summed counts across delta reports) or `module=65535 name=-` (792,597,
unresolved). **Zero leaf samples landed in X3AP.exe, d3dx9_37.dll, or the d3d9 proxy** — the module-split and
per-leaf-RVA request from the brief cannot be answered from `profile_leaf`/`profile_thread` in this run; EIP at
every FEX-emulated tick sits in an ntdll syscall thunk or an address the sampler cannot resolve to any pinned
module, not in application code.

**Frame table (stack-scan return addresses inside X3AP.exe) is partly informative.** `profile_frame` totals
4,157,658 samples; 3,901,493 (93.84%) are `rva=0x0` (no main-module frame found on the walked/scanned stack —
also mostly blind). Of the 256,165 non-zero-RVA samples (6.16% of all `profile_frame` samples, ~1.7% of total
1.5M profiler samples), the top 25 RVAs (`base=0x400000`):

| addr | share of non-zero frame samples | named region (view-submit-hot-path.md) |
| --- | --- | --- |
| 0x4722ad | 15.86% | inside/adjacent to R5's sort call site `0x004722af` |
| 0x4721b6 | 12.46% | same function as above, a few bytes earlier |
| 0x4c403e | 11.49% | inside the pass loop `0x004c3ff0`-`0x004c4070` (`BeginPass`->`DrawIndexedPrimitive`->`EndPass`) |
| 0x4c4000 | 5.93% | pass loop, same range |
| 0x4c4068 | 1.06% | pass loop, same range |
| 0x4c522d | 1.25% | inside `0x004c0150` (material submission), no named sub-range |
| 0x4c0483 | 1.15% | inside `0x004c0150`, before the labeled material-init block (`0x004c0c6d`) |
| 0x51c84e, 0x51133e, 0x51aab3, 0x50e21e, 0x517bdc, 0x4bcc30, 0x4b9214, 0x4e25a8, 0x4e1a18, ... | ~2% each or less | **none of R1-R8's named ranges; not documented anywhere in this note** |

`0x4722ad`/`0x4721b6` together are 28.3% of resolved frame samples (~1.72% of all profiler samples) and are
the single strongest signal in this run: they sit at the R5 draw-queue-sort call site, consistent with R5
being a real caller-frame while something underneath it (D3DX/CRT/syscall) runs. The pass-loop cluster
(`0x4c4000`-`0x4c4068`, 18.5% of resolved samples) is the ordinary per-draw `BeginPass`/`DrawIndexedPrimitive`
path, not one of the R1-R8 candidates. None of the top-25 addresses fall inside R8's `0x4bdee0`-`0x4be3e3`,
R3/R1/R2's `0x4c1eab`-`0x4c3ff0`, or R4/R7's `0x47d9c0`/`0x47e620` proper (only the *caller into* the sort at
0x4722ad, not the sort body). The `0x50xxxx`/`0x51xxxx` addresses are real X3AP.exe text (module text extends
to `0x531000`) but outside every range this note names — an unidentified hot caller-frame family worth a
disassembly pass of its own.

**R1-R8 disposition.** None of R1, R2, R3, R4, R6, R7, R8 is confirmed or refuted by this run: the leaf table
that would show their code directly is 100% blind (0 x3ap leaf samples), and none of their named ranges appear
among the frame table's resolved addresses. R5 (draw-queue sort) has directional, not conclusive, frame-table
support (a caller-frame share, not a leaf/time measurement) — consistent with, but not proof of, its being
costly. The `--profile` leaf/RVA method itself is closed out for this FEX build the same way run84 closed it:
the brief's requested "top 25 leaf RVAs in X3AP.exe with sample shares" cannot be produced because there are no
such leaf samples. The note's own fallback column (stamp pairs and counters at each candidate's site, section
6) remains the only path to a real measurement; this run supplies none of those stamps.

## Reproduce

```sh
JAVA_HOME='/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home' \
  '/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless' \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -readOnly -noanalysis \
  -scriptPath tools/analysis \
  -postScript X3CallTree.java /tmp/out/hotchain.txt 2 \
  0047e920 0047d9c0 0047e6e0 0047e620 004bdee0 004c4fc0 0047cfe0 0047e780 004bfd40 0047d5e0

i686-w64-mingw32-objdump -d -M intel '<bottle>/drive_c/X3/X3AP.exe' > /tmp/x3-text.asm
```

Instruction-class counts, call histograms, the inbound-edge sweep and the abs32
scan were done with short local Python over that listing and over the PE section
table; the listing and the scripts are game-derived and stay untracked.


## Run 48 C: measured candidate disposition (2026-09-20)

Run181, steady frames 3000–5400: all 22 sites active, no clock/failure/unmatched
errors and no steady dropped stamps. Median 5,390 stamps/frame, self cost 0.549 ms
at 102 ns/dispatch. These are diagnostic CPU timings in bottle X3/arm64 Wine+FEX
(`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), not native Windows timings.

| candidate | measured span per frame | decision for this view |
| --- | --- | --- |
| R3 technique / End | 65 / 64 microseconds, 503–504 calls | close: too small |
| R5 queue sort | 5 microseconds across 30 calls; summed nodes 11–12 | close: too small |
| R4 cache walk | median 0; no sampled miss/iteration | no pressure; do not build cache |
| R1 view inverse | 71 microseconds (world inverse 79) | no patch justified |
| R8 world matrix | 19 microseconds | no patch justified |
| R2 / R6 | only bounded by unsplit prep net 0.988 ms | no specific patch justified |
| R7 light-list/qsort | not stamped | remains unmeasured, not closed |

Material submission net 11.620 ms is large, but does not establish a safe call-elision
patch. Preserve existing closed decisions. No engine patch is selected from this
flight. The user's 40 FPS report and pre-render delta are evaluated in
[engine frame time](../architecture/engine-frame-time.md#run-48-bc-first-view-freezes-and-the-40-fps-busy-view-2026-09-20).
Local reproduction: `verification/results/run48c-triage/reproduce.py`.

## R7 whole-call timing boundary qualification (2026-09-20)

**Disposition:** two entry/exit stamps are statically qualified for a future
consolidated diagnostic. No timing hook was implemented or executed here. R7
remains unmeasured; Run 48 C's other candidate decisions remain view-specific.
This section supersedes section 6's erroneous `0x0047d9f6` caller return and its
suggestion to start with a separate `qsort` counter.

### ABI and the two callers

The exact X3 EXE SHA-256 remains
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
`0x0047d5e0` is a callee-cleaned, one-stack-argument routine: at native entry
ESP=`E`, `[E]` is the return address and `[E+4]` the node. It saves EBP, aligns
ESP down to 16, reserves `0x34` bytes and saves EBX/ESI/EDI. The node is loaded
from `[EBP+8]` at `0x0047d5fb`. No incoming EAX/ECX/EDX value is an argument:
the first helper `0x0047d560` initializes all three before using them, and
R7 supplies ECX=node and a pushed zero to `0x00488170`. Ghidra's inferred
`params=0/conv=unknown` is insufficient here; the instruction evidence proves
the stack contract. Every normal path reaches `0x0047d9ab`, restores
EDI/ESI/EBX, restores ESP from EBP, pops EBP, and executes `ret 4` at
`0x0047d9b1`. The caller resumes with ESP=`E+8`.

| Native call / exact rel32 bytes | Return | Classification and argument |
| --- | --- | --- |
| `0x0042173b`: `e8 a0 be 05 00` | `0x00421740` | cockpit display-node update `0x004216e0`; pushes `[[cockpit+0x10]+0x70]`, then consumes the selected slot table. Its caller is `0x0042101a` in cockpit update `0x004205e0`; this is pre-render cockpit work, not necessarily cockpit-view draw submission. |
| `0x0047dff1`: `e8 ea f5 ff ff` | **`0x0047dff6`** | recursive scene traversal `0x0047d9c0`; pushes EBX=node. Next calls `0x004bdea0` to publish selected light slot indices, then builds the world matrix. This can cover multiple views/layers; return-address classification alone does not identify the main view. |

Ghidra references and an all-offset `.text` E8/E9 sweep agree on exactly these
two direct callers, with no tail jump to the entry. No absolute dword encoding
of any byte in either proposed span occurs anywhere in the file. This supports
the known direct-call coverage; it cannot exclude a computed runtime address.
Neither caller consumes R7's EAX/ECX/EDX or arithmetic flags: the cockpit path
overwrites EAX/EDX then ECX before use; `0x004bdea0` initializes EDX/EAX and
assigns ECX before use on the traversal path. There is no established semantic
return value. Preserve native output registers/flags nevertheless, rather than
turning this observation into an ABI assumption for unknown callers.

The whole body includes slot reset `0x0047d560`, both early gates, candidate
scoring, selection and optional `qsort`. The second gate helper `0x00488170`
recurses only into itself (`0x004881a4`), returns cached nonnegative
`node+0xa4`, or derives that cache from base `node+0xa0` and child positions
`+0x30/+0x34/+0x38`, with a `node+0x70` scale branch. This is an
extent/radius-like quantity by arithmetic, **not an established light count**.
Its semantic units remain unresolved. The candidate storage is an array of
node pointers, not a linked chain; layout and eight 12-byte selected records
are established in [camera-and-lights.md](camera-and-lights.md#directional-lights-source-space-and-count).
`qsort` at `0x0047d9a3` receives the populated count in EDX, stride 12 and
fixed comparator `0x0047d5b0`; it is skipped for zero populated records.

### Two plain-copy spans and CPU preservation

| Stamp | Exact displaced bytes / boundaries | Resume / direct incoming edges |
| --- | --- | --- |
| entry `0x0047d5e0` | `55 8b ec 83 e4 f0`: `push ebp`; `mov ebp,esp`; `and esp,-16` (6 bytes) | `0x0047d5e6`; the two calls above |
| exit `0x0047d9ab` | `5f 5e 5b 8b e5`: `pop edi`; `pop esi`; `pop ebx`; `mov esp,ebp` (5 bytes) | `0x0047d9b0` (`pop ebp; ret 4`); branches from `0x0047d605`, `0x0047d617`, `0x0047d97c`, `0x0047d998`, plus fall-through after qsort stack cleanup |

Both spans contain whole instructions, no relative transfer and no x87. The
all-offset scan of near/short call/jump/conditional/loop encodings found no
interior targets. They are disjoint from the current SiteSpec and fixed-claim
inventory. Use length 6/5, `ret_pop=0`, `rel32_offset=0`: the exit trampoline
must replay the five bytes and jump to the original remaining epilogue; it
must not append its own `ret 4`. Replay at the exact native site ESP is vital
for both the alignment and the three pops. Do not move the exit stamp to a
later byte merely to avoid these stack operations.

Reuse the context-stub contract in `lean_stub.h`: pushfd/pushad, preserve all
XMM0–7, clear DF for C++, restore the entire saved context before replay.
EBX/ESI/EDI/EBP and the original stack are live; at exit the saved native
nonvolatile registers are still on the native stack. Entry arithmetic flags
are killed by `and esp,-16`, but the exit span does not set flags. Preserve
all flags including DF at both sites. At entry, native ESP is
`saved[SavedEsp]+4` (pushad saved ESP after pushfd), so the return address is
at that address. A numerical frame token `saved[SavedEsp]` at entry equals
`saved[SavedEbp]` at exit; `[EBP+4]` is then the same return address.
Do not retain or later dereference the node or any selected-light pointer.

Wrap each stamp handler's own work in `LightCallBoundary` before any Win32
call: preserve site-local LastError and MXCSR, then restore those values before
the native tail. Entry preserves the native input state; exit preserves the
native output state, which need not equal entry. R7 executes x87 and may change
its status; do not restore the entry x87 image over that output. The lean
envelope is valid only after the built handler/callee graph passes the existing
no-x87 audit. No logging, formatting, floating return values or allocation in
the handler. Four-byte incoming stack alignment remains required. These
contracts retain documented Win32 QPC/GetLastError/SetLastError APIs on native
Windows; no native execution qualification is supplied by this analysis.

### Lifetime, nesting, exceptions and the minimal measurement

R7 itself does not recurse. Its four calls are reset `0x0047d560`, the recursive
extent helper, `_ftol2` `0x0052b5d0` and CRT qsort. Reset and `_ftol2` have no
calls; the extent helper has only its self-call. Normal qsort calls only its
swap/shortsort helpers and the fixed, call-free integer comparator (shortsort's
indirect call is `0x005104b2`). The normal chain has no engine callback that can
re-enter R7. The surrounding traversal's self-calls `0x0047e5e5`/`0x0047e600`
occur after R7 has returned, so traversal recursion is not overlapping R7.
CRT invalid-argument branches reach `0x00515cd3`/`0x0050ea39`; with the native
non-null table, positive stride 12 and fixed comparator those branches are
excluded. Corruption, asynchronous exception handlers and external hooks are
outside that normal-path proof.

The known caller chains belong to the main-loop cockpit and render phases;
this is static thread ownership evidence, not an observed thread census. Admit
only the existing frame/Present owner TID, count and reject foreign/early hits,
and classify return addresses into cockpit/traversal/unknown. Never let a
foreign hit mutate owner timing state. An unknown caller must be surfaced as
an unsupported-coverage counter rather than silently assigned to traversal.

Measure entry to common epilogue, including reset and both gates and excluding
only the few final restore/return instructions. Use one QPC read at each stamp,
an entry count and a matched-completion count, plus accumulated ticks split
by the two callers; retain raw elapsed and separately reported measured probe
cost. No per-light traversal, qsort stamps or live node validation is needed.
Use constant-size owner state with the native frame token to match ends. A
bounded nesting mechanism must reject/poison unexpected overlapping entries
without overwriting the outer clock or double-counting nested elapsed time;
count nesting/overflow/mismatch failures. A single unchecked global open clock
does not meet that requirement even though normal nested calls are excluded.

No SEH frame is installed by R7. Memory access, integer division and x87/CRT
paths can still fault; a nonlocal unwind can bypass the exit. The instrumentation
must not catch, swallow or replace native exceptions, and must not splice the
native return address. Invalidate unclosed records at frame/discard boundaries,
report unmatched intervals and avoid carrying their duration into the next
frame. Handler code itself must not throw. Pair token mismatch, clock failure,
clock reversal, unknown caller, nesting and foreign-thread counters qualify
coverage; a nonzero failure count prevents treating that window as complete.
Use the shared preflight/install transaction for both spans and rollback on
either failure; disabled/inert stubs must still replay the original bytes.

Two stamps per call cost approximately `2 × calls × dispatch_ns`; the current
102 ns submit-stamp figure is only a planning estimate for these changed
handlers. Re-measure the actual added handler before interpreting small R7
times. Keep cockpit and traversal timings separate in the 300-frame report so
cockpit work is not charged to view submission. No R7 speedup or bottleneck
conclusion follows from the static body or the profiler's lack of samples.

### Evidence and implementation acceptance still required

Local evidence is `/tmp/x3-r7-abi/`: `qualify.py`, validated
`qualification.json`, bounded listings, and Ghidra outputs. Reproduction:
`python3 /tmp/x3-r7-abi/qualify.py` uses project PE mapping, gap-free objdump
parsing and the current claim inventory from `verify_chase_aim_sites.py` and
`verify_submit_phase_sites.py`. It decoded 280 R7 instructions, 106 cockpit
helper instructions and 864 traversal instructions; both spans matched;
two direct callers, zero interior branch encodings, zero absolute references,
zero claim overlaps. Ghidra ran `X3CameraState.java` and `X3CallTree.java` with
`-readOnly -noanalysis` on `/tmp/x3-fog-sizing-ghidra FogSizing`, program
`X3AP.exe`. The older `/tmp/x3-ghidra-research X3Render` project no longer
contained the requested program in this session. No raw output is tracked.

An implementation should extend the existing named checks, not claim this
documentation run as CPU-fixture acceptance:

- `verification/probe/verify_submit_phase_sites.py` and
  `verification/analysis/test_submit_phases.py`: pin these two spans, caller
  addresses, four exit branches, stack-token layout, complete regions and
  claim/absolute-reference refusal. Include the final native `ret 4` contract.
- `verification/probe/submit_phases_host.cpp`: caller buckets, matched count,
  unknown/foreign/early admission, nesting poison/overflow, stale entry,
  token mismatch, failed/backward clocks and discarded-frame recovery.
- `verification/probe/submit_phase_cpu_fixture.cpp`: extend synthetic bodies
  with the real aligned prologue and pop/mov/ret4 epilogue, both caller classes,
  each early exit and a populated fall-through path. Compare native versus
  hooked GPR/EFLAGS/DF, ESP, XMM0–7, MXCSR, live x87 stack/control/status and
  LastError, including deliberately distinct entry/output state; verify
  all four incoming ESP residues modulo 16, partial-install rollback and
  disabled forwarding. Exercise aborted/unmatched and nested accounting in
  controlled synthetic paths without invoking game code.
- Run the affected host module with
  `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_submit_phases`;
  the install owner then builds/audits through `build_game_phase_cpu.py` and
  `check_no_x87.py`, and runs `run_submit_phase_cpu.py` only under
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py ...`.
  A future user flight supplies actual caller rates and elapsed cost;
  native Windows execution remains a separate unverified target.

Only static qualification and documentation review were performed here: no
production edit, build, Wine command, install or game launch.
