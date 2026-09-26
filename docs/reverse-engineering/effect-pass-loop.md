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
   0.26 µs for two reads (`docs/archive/iteration-09-cost.md`) and ≤ 0.33 µs
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

## 7. The residual boundary `0x004c1eab` (`--residual-phases`)

2026-09-17. The pass stamps leave `view_submit − sum` (run95: 4,520 us, 4.6 us
per draw) unattributed, and a stamp pair `pass_end(n) → pass_begin(n+1)`
would only re-measure it. `X3M_RESIDUAL_PHASES=1` adds one stamp inside this
routine at the boundary between the engine's per-object preparation and the
D3DX setup of a sub-mesh, and pairs it with the clocks the pass group already
takes (`src/proxy/residual_phase_sites.h`, `residual_phases_core.h`; the
frame-routine twin is in [frame-loop-phases.md](frame-loop-phases.md) §5d).

**The boundary.** Inside the sub-mesh loop (`0x004c0223` … `0x004c4082`)
the steady-state path is: loop head, `ebx = sub-mesh material` (`0x004c0236`),
the two material-initialisation guards `0x004c0c64`/`0x004c0dea`
(§2 of the frame-loop note; the initialisation block between them runs once
per material and ends with `SetTechnique`/`FindNextValidTechnique`), then
`0x004c1eab`:

```
4c1eab  mov edx,[ebx]                 ; ebx = ID3DXEffect        <- material_setup
4c1ead  mov ecx,[edx+0xfc]            ; slot 63 Begin
4c1eb3  push 1                        ; D3DXFX_DONOTSAVESTATE
4c1eb5  lea eax,[esp+0x88]            ; = E+0x84, the pass-count slot (one push deep)
4c1ebc  push eax / push ebx
4c1ebe  call ecx                      ; Begin(effect, &passes, 1)
4c1ec0  ... 0x2130 bytes of parameter setters, the two engine RS writes,
        the geometry guard (0x4c3fcb..0x4c3fd8), then the pass loop 0x4c3ff0
```

A call histogram by region (objdump on the routine, this session): between
`Begin`'s return `0x004c1ec0` and the pass loop head, 135 calls — 70
register-indirect dispatches (`call edx` 44, `call eax` 15, `call ecx` 11:
the SetInt/SetVector/SetBool/SetFloat/SetMatrix/GetBool/ApplyParameterBlock
callsites the frame-loop note classifies by displacement), the cached
texture setter `0x004b9ed0` 22 times, and helpers; between the second guard
`0x004c0dea` and the span, 153 calls dominated by the by-name setters
`0x004b8f70` (31) and `0x004b9010` (21) — the initialisation block the
guards skip in steady state; before the first guard (`0x004c0223` …
`0x004c0c6d`), 28 calls, among them the six effect-cache lookups `0x004bb0f0`
and six `call eax`/`call edx` dispatches. Those six are **not** an
initialisation block: every non-exit path of the sub-mesh loop joins at
`0x004c0b85` and runs, per sub-mesh, the three engine-wrapper binds
(`0x004c0ba9`, `0x004c0bc0`, `0x004c0be5`, each followed by an exit test),
then `GetTechniqueByName` (`0x004c0bfa`, vtable `+0x34`, slot 13),
`FindNextValidTechnique` only on a name miss (`0x004c0c19`, `+0xf4`, slot
61) and `SetTechnique` (`0x004c0c34`, `+0xe8`, slot 58) before the first
guard at `0x004c0c64`. The slot names are inferred from the `ID3DXEffect`
vtable order of `d3dx9effect.h` (consistent with `Begin` = slot 63 at
`+0xfc` and `BeginPass` = slot 64 at `+0x100`). So `prepare` contains, per
draw, one D3DX technique lookup by name plus `SetTechnique` (and `End`),
never the per-draw parameter-setter traffic, which starts at `0x004c1eab`.
The design note's two candidates (`0x004c0223` loop head, `0x004c1ec0`
Begin returned) differ from this boundary only by the guards (engine side)
and by `Begin` itself (D3DX side, one call), and a stamp here needs no
second stamp because it pairs with the pass group's clocks.

**What the two intervals contain.** `prepare` = last `pass_end` →
`0x004c1eab`: the pass loop's tail, `End` (`0x004c4066`), the SEH unlink and
`ret`, the caller `0x004c4fc0`'s remainder, the queue walk `0x0047e6e0` or the
traversal step of `0x0047d9c0` (world matrix `0x004bdee0`, texture-animation stepper `0x004f66e0`;
not a cull, corrected 2026-09-17, [shadow-caster-lifetime.md](shadow-caster-lifetime.md) §0),
the next node's `0x004c4fc0` entry, this routine's prologue (SEH record,
`sub esp,0x4c8`, `SetSoftwareVertexProcessing`), the sub-mesh head, the
three engine-wrapper binds, `GetTechniqueByName`, `SetTechnique` and the two
guards — engine work, plus `End`, the technique lookup and `SetTechnique`
(D3DX, per draw).
`setup` = `0x004c1eab` → the first `pass_begin`: `Begin`, the parameter
setters, the two RS writes, the geometry guard — D3DX work plus a few engine
instructions. For a sub-mesh whose geometry guard skips the pass loop, no
`pass_begin` follows and the accumulator counts `setup_skipped`; the next
material's `prepare` then has no fresh `pass_end` and counts
`prepare_skipped` (as does the first material of every frame).

**Site validation** (`verification/probe/verify_residual_phase_sites.py`,
`PASS` against the bottle EXE, the same contract as §3):

- Bytes `8b 13 8b 8a fc 00 00 00`, two whole instructions, a plain copy (no
  relative control transfer; byte-identical at any arena address).
- Incoming edges: exactly four, all landing on the span start:
  `jne 0x004c1eab` at `0x004c0c67` and `0x004c0ded` (the two guards),
  `jmp 0x004c1eab` at `0x004c0de5` (the initialisation path's exit) and
  `je 0x004c1eab` at `0x004c1e23` (its parameter-loop skip). No branch in the
  routine targets the span interior; the raw-encoding sweep of `.text` finds
  none; no data reference outside `.rsrc`.
- ESP: the span is at the frame base E. `lea eax,[esp+0x88]` after one push
  writes `E+0x84`, the same slot the pass loop reads at frame depth
  (`0x004c3fde`, `0x004c4050`), and every callee between the guards and the
  span restores ESP (the loop reads `E+0xa0` at `0x004c0223` and
  `0x004c406b`). The displaced instructions run in the claim tail at the
  game's exact ESP; the lean stub restores every register and the flags.
- Flags: dead on entry (the guards' `cmp` results are consumed by the `jne`s
  that land here; the next consumer is the `test` at `0x004c1ec3`, after
  `Begin`). EBX (effect), EDI (`[esp+0x28]`, loaded at `0x004c1ea7`), ESI,
  EBP live across the span and preserved by the stub.
- Disjoint from the four pass spans, the point-light patch
  (`0x004c27af`–`0x004c27b5`) and every installed game/frame site; `Begin`
  follows the span directly (`0x004c1eb3` is the next instruction) and the
  span precedes the pass loop.

**Rate and cost.** One dispatch per sub-mesh, i.e. per material draw in the
common single-pass case (~1,000 per busy frame), through the same lean stub
as the pass stamps (91 ns documented; the fixture measures the residual stub
separately, `RESIDUAL PHASE BENCH`). The pass handler gains one predicted
branch at `pass_begin` and one store at `pass_end` for the retained clocks;
the fixture's `PASS PHASE BENCH` is the check that this stays within noise.
