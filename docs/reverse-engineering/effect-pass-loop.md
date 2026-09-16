# Effect pass loop `0x004c3ff0`: stamp sites for `--pass-phases`

2026-09-16. Static study of X3AP.exe,
SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`
(the bottle copy). Nothing here was observed at runtime. Control flow, callers
and the routine's bounds are from Ghidra headless on
`/tmp/x3-ghidra-research/X3Render` (`X3SampleFunctions.java`); every byte,
span and edge claim is from `i686-w64-mingw32-objdump` on the file bytes and is
re-checked mechanically by `verification/probe/verify_pass_phase_sites.py`.
Inferences are marked. Raw decompiler output stayed local and untracked.

**Status (2026-09-16).** Implemented as `--pass-phases`
(`src/proxy/pass_phase_sites.h`, `pass_phases.cpp`, `pass_phases_core.h`) on
the four spans of §3 as proposed, with the accumulate-only lean stub of §4.
The CPU fixture measured the stub at 90.5 ns per dispatch under the X3 bottle
(364 µs implied per busy frame at 4,024 dispatches, against the 1.5 ms
ceiling), so the two-stamp fallback of §4.4 was not needed. The shipped line
differs from the §5 draft in field names (`end_*` for `endpass_*`,
`self_p50_us` for `stamp_overhead_us`, p95 columns added, no `residual` field:
the reader subtracts `sum` from `view_submit`); the schema and reading guide
are in [sampling-profiler.md](../verification/sampling-profiler.md), "Pass
phases". No game run yet.

**Question.** [engine-state-filter.md](../architecture/engine-state-filter.md)
ratified "no state filter; measure the D3DX pass loop instead" and proposed four
accumulating stamps. Are those four spans safe trampoline sites, how often do
they fire, and what does a `pass_phases` line report? This note answers that;
[frame-loop-phases.md](frame-loop-phases.md) §2 remains the owner of the
submission call chain and of the state-manager findings.

## 1. The routine, its callers and its rate

`0x004c0150` (`0x004c0150`–`0x004c40fb`, four `ret`s, int3 padding from
`0x004c40fc`; the same bounds `src/proxy/point_light_admission_core.h` uses)
is the per-node material submission routine: `push ebp; mov ebp,esp;
and esp,0xfffffff0`, an SEH record (handler `0x005305b1`), `sub esp,0x4c8`,
then `push ebx/esi/edi`. Call it **frame base E** = ESP after the prologue;
all locals below are `E+disp`, and the SEH link sits at `E+0x4d4`.

Callers (Ghidra references): `0x004c0150` has exactly one, `0x004c5228` in
`0x004c4fc0`; `0x004c4fc0` has two, `0x0047e769` (`0x0047e6e0`, the sorted-queue
walk) and `0x0047e076` (`0x0047d9c0`, the recursive traversal); those two are
reached only from the frame routine `0x00471f50` (`0x004722b5`, `0x00472491`,
`0x00472295`, `0x004722a8`, `0x00472484`) and from the env-map driver
`0x0047e820` (`0x0047e8e0/e9/f6`), itself called from `0x00472210` inside
`0x00471f50`; `0x00471f50` has one caller, `0x00403f34` in the main loop.
**So the whole chain runs on the main-loop thread, once per frame, and is not
recursive**: no site below can be entered from a second thread or re-entered,
except by a D3DX callback, and D3DX only calls the game's state manager
(`0x00562a8c`), never back into `0x004c0150`.

Loop nesting inside `0x004c0150`:

- sub-mesh loop `0x004c0223` … `0x004c4082` (`jl 0x4c0223`), count
  `movsx edx,WORD PTR [ecx+8]` with `ecx = [ebp+8]`, index at `E+0xa0`.
- per sub-mesh: `ID3DXEffect::Begin(&passes,1)` at `0x004c1ebe` (slot 63,
  `+0xfc`; `lea eax,[esp+0x88]` one push deep = the pass-count slot `E+0x84`;
  the flag `1` is `D3DXFX_DONOTSAVESTATE`).
- two engine render-state writes through `[[0x00608b3c+0x1c]]` slot 7 (`+0x1c`)
  at `0x004c3fae` (`0xa8`,`7`) and `0x004c3fc5` (`0xce`,`0`), then a geometry
  guard: `esi = [[E+0x28]+0x14]`, `call [[esi]+0x10](esi)`; zero skips the loop.
- **pass loop `0x004c3ff0` … `0x004c405b`** (`jb 0x4c3ff0`), index at `E+0x74`,
  bound at `E+0x84`; `ID3DXEffect::End` (slot 67, `+0x10c`) at `0x004c4066`.

One pass-loop iteration issues exactly one draw, so the loop body executes once
per material-path draw (busy run91 frame: `draws_p50` 1,006, which also counts
overlay and particle draws, so passes ≤ 1,006). **The four stamps are therefore
per-draw sites, not per-material sites**: ~4,024 dispatches per busy frame.

## 2. What the loop body does

```
4c3ff0  mov eax,[esp+0x74]      ; pass index                  <- pass_begin
4c3ff4  mov edx,[ebx]           ; ebx = ID3DXEffect
4c3ff6  mov ecx,[edx+0x100]     ; slot 64 BeginPass
4c3ffc  push eax / push ebx
4c3ffe  call ecx                ; BeginPass(effect, i)
4c4000  mov eax,[esp+0x28]                                    <- pass_applied
4c4004  mov ecx,[eax+0x14]      ; the sub-mesh geometry object
4c4007  mov edx,[esp+0xcc]      ; the IDirect3DDevice9
4c400e  mov edi,[edx]           ; device vtable
4c4017  push esi / add edi,0x148 / call [ ] ; geometry slot 4 -> primCount
4c4025  push eax / push 0       ; DrawIndexedPrimitive args, pushed early
4c4028  push esi / call [ ]     ; geometry slot 5 -> NumVertices
4c402b  mov ecx,[edi]           ; device slot 82 DrawIndexedPrimitive
4c402d  push eax / push 0 / push 0 / push 4 / push device
4c403c  call ecx                ; DrawIndexedPrimitive(dev,4,0,0,nv,0,prim)
4c403e  mov edx,[ebx]                                         <- pass_drawn
4c4040  mov eax,[edx+0x108]     ; slot 66 EndPass
4c4046  push ebx / call eax
4c4049  mov eax,[esp+0x74]                                    <- pass_end
4c404d  add eax,0x1
4c4050  cmp eax,[esp+0x84] / mov [esp+0x74],eax / jb 0x4c3ff0
```

- **`CommitChanges` is never called**: no `+0x104` dispatch exists anywhere in
  `0x004c0150`. The pass-apply interval is `BeginPass` alone.
- **The draw is a direct `IDirect3DDevice9::DrawIndexedPrimitive`**, not a game
  wrapper. `E+0xcc` is written once at `0x004c01d3` from
  `[[0x00608b3c+0x18]]` — the same pointer the frame routine calls `BeginScene`
  on at `0x004720c8` (slot 41, `+0xa4`) and that this routine calls
  `SetSoftwareVertexProcessing(this,TRUE)` on at `0x004c0205` (slot 77,
  `+0x134`); `+0x148` is slot 82. The 7 stdcall dwords are supplied by two
  early pushes (`primCount`, `startIndex=0`) that straddle the two geometry
  calls plus the five at `0x004c402d`–`0x004c403b`; type `4` is
  `D3DPT_TRIANGLELIST`. **Inference** (from the push/pop accounting, not from a
  signature): the two geometry methods take `this` only.
- ESP accounting: the loop reads `E+0x74`/`E+0x84` at the same displacement
  before the calls (`0x004c3fde`, `0x004c3fe6`), between them (`0x004c4000`)
  and after them (`0x004c4050`, `0x004c4057`), and `Begin`'s out-parameter at
  `0x004c1eb5` writes the same `E+0x84`. Every callee therefore restores ESP,
  and **ESP equals the frame base E at all four sites**, with no pending
  argument pushes. Nothing of the routine's state lives below E, so the stub's
  `pushfd/pushad`, 0x80-byte XMM area and call frame cannot clobber anything.

## 3. Chosen stamp sites

All four proposed spans are suitable **as proposed**; none needed moving. Every
span is a plain copy (no relative control transfer inside), so the arena tail is
byte-identical at any address and there is no rel32 to re-base — one contract
less than the frame-phase sites.

| # | Name | Address | Bytes | Len | Semantics |
| --- | --- | --- | --- | --- | --- |
| 0 | `pass_begin` | `0x004c3ff0` | `8b 44 24 74 8b 13` | 6 | loop head: opens pass-apply |
| 1 | `pass_applied` | `0x004c4000` | `8b 44 24 28 8b 48 14` | 7 | `BeginPass` returned: closes apply, opens draw |
| 2 | `pass_drawn` | `0x004c403e` | `8b 13 8b 82 08 01 00 00` | 8 | `DrawIndexedPrimitive` returned: closes draw, opens `EndPass` |
| 3 | `pass_end` | `0x004c4049` | `8b 44 24 74 83 c0 01` | 7 | `EndPass` returned: closes `EndPass` |

Validation (mechanised in `verification/probe/verify_pass_phase_sites.py`,
result `PASS` against the bottle EXE):

- Bytes match; every span starts and ends on a decoded instruction boundary of
  the gap-free decode of the whole routine (`0x004c0150`–`0x004c40fc`, 4,695
  instructions, no `(bad)`); all spans ≥ 5 bytes and inside the loop body.
- Incoming edges: exactly one, `jb 0x004c3ff0` at `0x004c405b`, landing on the
  start of `pass_begin`; the other three sites have no incoming edge (they are
  reached by fall-through from the call that precedes them). No direct branch
  in the routine targets any span interior, the routine has no indirect `jmp`,
  and a raw-encoding sweep of **every** byte offset of `.text` (1,246,762
  offsets, all `e8/e9/0f 8x/eb/7x/e0–e3` forms) finds no encoding whose target
  is inside a span.
- Data references: one dword `0x004c3ff1` exists in the file, at file offset
  `0x1f7281` — inside `.rsrc` and not 4-byte aligned, i.e. resource payload, not
  a pointer. No reference in `.text`, `.rdata` or `.data`.
- Registers and flags: `EBX` (the effect), `ESI`, `EDI`, `EBP`, `ESP` and (for
  `pass_end`, whose `add eax,0x1` feeds `0x004c4050`/`0x004c4057`) `EAX` are
  live across the spans, and incoming flags are dead at all four (the next
  consumer is the `cmp` at `0x004c4050`, after `pass_end`). The shared stub's
  `pushfd/pushad` … `popad/popfd` before `jmp [next]` makes this a non-issue,
  and the displaced instructions run in the tail at the game's exact ESP —
  the contract already proven for frame sites 4 and 8.
- Re-entrancy: none (§1). A foreign-thread hit is impossible by construction but
  is still counted and dropped by the owner check, as in `frame_phases`.
- Conflicts: the point-light admission patch owns `0x004c27af`–`0x004c27b5` in
  the same routine and `object_trace` owns `0x004c5228` in `0x004c4fc0`; both
  are disjoint from all four spans, and neither lies inside the pass loop.

Not covered by the four stamps: the `Begin` at `0x004c1ebe`, the `End` at
`0x004c4066`, the two engine state writes at `0x004c3fae`/`0x004c3fc5` and the
loop's own 4-instruction tail (`pass_end`'s span plus `cmp/mov/jb`).

## 4. Rate, cost budget and the accumulate-only stamp

Four stamps × ~1,006 passes = **~4,024 dispatches per busy frame**. The shared
`game_phases` stub is too expensive at that rate: it wraps every dispatch in
`PreserveCpuState` (`FNSAVE`/`FNINIT`/`FRSTOR`, 108-byte x87 image) plus a
second `fninit`/`ldmxcsr` and then does the frame-phase tracker's per-stamp
bookkeeping. At the brief's ~0.6 µs estimate that is 2.4 ms per frame — 6 % of
the 37.3 ms busy frame, larger than some of the intervals it measures.

A `--pass-phases` diagnostic therefore needs its own **accumulate-only** stamp:

1. A dedicated stub, same shape as `game_phases::emit` (`pushfd; pushad; cld;
   sub esp,0x80`; 8 `movups`; `push index`; `call`; restore; `jmp [next]`) — the
   XMM save stays, because the game may hold live XMM values across an injected
   site — but the handler uses `LightCallBoundary` (MXCSR + `LastError` only)
   instead of `PreserveCpuState`, which is admissible exactly when the handler
   executes no x87 opcode, returns no float and never logs
   (`src/proxy/cpu_state.h`; audited by `verification/probe/check_no_x87.py`).
2. The handler body is: one `QueryPerformanceCounter`, one 64-bit subtract
   against the previous stamp's timestamp, one 64-bit add into
   `accum[interval]`, one store of the timestamp, and `++passes` on index 3.
   No tracker, no ordering state machine, no window arithmetic, no formatting.
   The window logic runs once per frame from the existing frame boundary
   (`frame_phases::detail::frame_impl`), not per stamp.
3. Budget. Measured proxy evidence bounds a QPC-plus-bookkeeping span at
   0.26 µs for two reads (`docs/verification/iteration-09-cost.md`) and ≤ 0.33 µs
   per stamp (`docs/verification/route-cost-run1.md`), i.e. ~0.10–0.17 µs per
   QPC read under this bottle; the stub envelope itself is **unmeasured**,
   estimated at 0.05–0.15 µs without the x87 save. Expected
   **0.6–1.3 ms per busy frame (1.6–3.5 %)**, against ~2.4 ms for the shared
   stub. Ceiling to accept before installing: 1.5 ms.
4. If a calibration run puts it above that ceiling, drop to two stamps
   (`pass_begin`, `pass_applied`): the apply interval is the number option B
   needs, and the draw time is already measured by the proxy's hooked
   `DrawIndexedPrimitive`. Halving the dispatches halves the cost. `rdtsc`
   instead of QPC is the other lever and is **not** qualified under FEX.
5. Bias. Each stamp's own cost lands in the interval that follows it, so the
   three intervals are each inflated by about one dispatch and `view_submit`
   by all four. Report the raw numbers plus `stamp_overhead_us` computed from a
   measured per-dispatch cost (one fixture row, not a game run); do not
   silently subtract it.

## 5. What a `pass_phases` line reports

One line per 300-frame window, like `frame_phases`, with `--telemetry`
required, per-frame accumulation and the window's p50 (p95 for the same
fields is optional but cheap, since it is window work, not stamp work):

```
pass_phases frame=<n> frames=300 passes_p50=<count per frame>
  apply_p50_us=<Σ pass_applied-pass_begin>       # BeginPass only; no CommitChanges
  draw_p50_us=<Σ pass_drawn-pass_applied>        # 2 geometry calls + hooked DrawIndexedPrimitive
  endpass_p50_us=<Σ pass_end-pass_drawn>         # EndPass
  sum_p50_us=<apply+draw+endpass>
  view_submit_p50_us=<from --frame-phases>       # the same window's site 8->9 interval
  residual_p50_us=<view_submit - sum>            # engine per-object setup, sort, ~75 parameter
                                                 # writes, Begin/End, overlays in the same interval
  stamp_overhead_us=<passes*4*measured dispatch> incomplete=<n> early=<n> foreign=<n>
```

Reading it: `apply_p50_us` is what option B of the state-filter note can
attack. Below ~3 ms it kills option B; at ≥ 10 ms it is the only lever on this
frame short of drawing fewer objects. `residual_p50_us` is the engine's own
per-object cost and points at the O(n²) queue sort and the by-name parameter
setters instead. `passes_p50` must track the telemetry `draws_p50` (passes ≤
draws); a large gap means multi-pass techniques are active and the per-draw
arithmetic above must be redone.

## 6. What this does not establish

- No runtime measurement of any kind; the cost budget in §4 is bounded by
  proxy-side evidence, not by a dispatch measurement of this stub.
- The two geometry methods (`[[E+0x28]+0x14]` slots 4 and 5) are identified
  only by their role in the draw arguments; their class is unnamed.
- Whether Microsoft's `d3dx9_37.dll` or Wine's builtin serves `BeginPass`
  remains open (one `GetModuleFileNameW` line settles it, state-filter note).
- The steady-state pass count per sub-mesh (1 in the common case) is an
  inference from the measured draw count, not from the technique data.
