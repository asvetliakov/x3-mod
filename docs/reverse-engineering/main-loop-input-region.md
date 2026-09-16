# Main-loop `input_part=0`: the per-sector update and its stamp sites

2026-09-16. Static study of X3AP.exe, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` (the bottle
copy). Nothing was observed at runtime. Sizes, call lists and string references
are from Ghidra headless on `/tmp/x3-ghidra-research/X3Render`
(`tools/analysis/X3CallTree.java`); every byte, span, edge and caller claim is
from `i686-w64-mingw32-objdump` on the file bytes and is re-checked by
`verification/probe/verify_loop_phase_sites.py` (`PASS`). Inferences are marked;
raw decompiler output stayed local and untracked.

**Question.** Run 32 session C (run94, `docs/verification/sampling-profiler.md`)
puts 95.7 % of a sustained 390 ms frame in `game_phase_input`
(`[0x00403b09, 0x00403f2a)`), and inside it `input_part=0` alone carries p50
391,500 us with no named call in the tape. What is in it, which call can own
390 ms, and where can the next diagnostic build split it?

## 1. `input_part=0` is 49 bytes, with three calls

The installed markers `game_phase_input` (`0x00403b09`) and
`game_phase_input_body` (`0x00403b3a`, `src/proxy/game_phase_sites.h` indices 6
and 23) bracket exactly `[0x00403b09, 0x00403b3a)` — 0x31 bytes of the main loop
`0x00403840` (size `0xa38`). Decoded gap-free:

```
403b09  test BYTE PTR [esi+0x4a0],1   ; esi = *0x0057fc60, bit 0 = paused/menu
403b10  jne  403b3a                   ; the whole sub-region is skipped when set
403b12  call 0048f550                 ; cut-scene / CutEvent update
403b17  call 0043a360                 ; per-sector update driver
403b1c  mov  eax,ds:0x0060850c        ; universe root
403b21  mov  edi,[eax+8]              ; head of the universe container list
403b24..403b38                        ; walk it; for class word [edi+0x48] == 1
403b2f  call 0045b660                 ;   deferred-delete sweep for that container
403b3a  (game_phase_input_body)
```

`ebx = 0` and `ebp = 1` come from `0x00403869` and `0x00403aba`. The region
holds exactly **three** top-level calls, each with exactly one caller image-wide
(raw `e8` scan of all 1,246,762 `.text` offsets).

| Callee | Size | Purpose | Evidence |
| --- | --- | --- | --- |
| `0x0048f550` | `0x13c` | cut-scene / cut-event driver: 3× `0x0048f2b0` (strings `"frame"`, `"CutEvent"`, script call `0x0049f680`), `0x00493540`, `0x004934b0`, 2× `0x004efda0` | call list + string xrefs |
| `0x0043a360` | `0x75` | **per-sector update driver**, §2 | disassembly |
| `0x0045b660` | `0xb5` | deferred-delete sweep: 32 buckets of `[container+0x50]` (stride `0xc`), per entry `[obj+0x40] & 0x08000000` → unlink from root `+0x38`, `0x0043f990`, string destroy `0x004a8240`, `free` | disassembly |

## 2. `0x0043a360`: two passes over the universe container list

`0x0043a360`–`0x0043a3d5` (single `ret`, cdecl, **no argument, no stack frame**;
`push ebx/esi/edi` prologue, `pop` epilogue, one `lea esp,[esp+0x0]` alignment
no-op at `0x0043a37c`, no `enter/leave`, no indirect call, no indirect jump).

```
edi = *0x0060850c                    ; universe root
if ([edi+4] != 0)                    ; pass A gate
  for (esi = [edi+8]; [esi]; esi = [esi])      ; loop A, back edge 0043a3a5
     if ([esi+0x48] == 1 && !([esi+0x148] & 1))
        0045d250(esi); 00452ad0(esi); 0045b720(esi)
for (esi = [edi+8]; [esi]; esi = [esi])        ; loop B, back edge 0043a3cf
   if ([esi+0x48] == 1 && !([esi+0x148] & 1))
      004596e0(esi); 004526b0(esi)
```

The entries are the sector-level containers (**inference** from the layout, not
from a symbol): each owns a 32-entry table of object lists at `+0x50`, stride
`0xc`, which four of the five callees walk with a `0x20` counter. All five take
one stack argument, are callee-pop (`ret 4`: no `add esp` exists anywhere in
`0x0043a360`) and have exactly one caller image-wide.

| Pass | Callee | Size | Calls | What it does | Scales with |
| --- | --- | --- | ---: | --- | --- |
| A | `0x0045d250` | `0xe61` | 43 | clears `[obj+0x40] &= 0xff9fcfff` / `[obj+0x44] &= 0xfff7ffbc` on all 32 buckets, then collision detect/respond on bucket 0: `0x0045cab0` (swept query, `0x4a7`) → `0x0045e130` (response, `0xd75`); script notifications through `0x0049f4c0` with `"CanWarp"`, `"CanLand"`, `"NotifyPlanetCollision"`, `"MakeDamage"`, `"KilledBy"`, `"CollisionWarn"` | objects in the sector; collision pairs |
| A | `0x00452ad0` | **`0x6afe`** | 401 direct, 0 indirect | the per-object simulation body over all 32 buckets; clamps its dt from `*0x00606f34+0x714` to 1000 ms; strings `"NotifySelfDestruct"`, `"KilledBy"`, `"NotifyDockingAbort"`; hottest callees `0x00450980` ×34, `0x0042fb20` ×27, `0x0049c8b0` ×22, `0x0044ccc0` ×16 | objects in the sector |
| A | `0x0045b720` | `0x1057` | 49 | walks a **global** object chain seeded by `0x0044e600`, filtered on `[obj+0x40] & 0x08000000`, bit 20 and class `5/6/7/0x12`; `0x0044b750` (`0xed0`), 12× `0x005112c4`, 7× `free`, 3× `memset` | global object count |
| B | `0x004596e0` | `0x1f47` | 90 | timed tick with its own accumulator `[sector+0x16c]`/`[+0x170]` and a 1000 ms clamp from `*0x00606f34+0x718`; 14× `0x0043ad20` (`0x387`), ware/slot accessors `0x00450cf0`/`0x00450d60`/`0x00450dd0`, 4× matrix `0x004f17f0` | objects and their per-object tick rate |
| B | `0x004526b0` | `0x411` | 13 | attach/transform pass over the 32 buckets: `0x004f17f0` matrix concat, `0x004f0640`, `0x004f0da0`; 2× `0x0049f4c0` with `"MakeBreak"` | objects in the sector |

## 3. Which callee owns 390 ms

**Established:** only `0x0048f550`, `0x0043a360` and `0x0045b660` execute in
`input_part=0`. `0x0048f550` is a fixed-size cut-event driver and `0x0045b660`
only frees objects already flagged `0x08000000`, so both are bounded by work the
frame created; `0x0043a360` is the only item running five routines totalling
~61 KB of code over every object of every active sector, every frame. **That
makes `0x0043a360` the owner with high confidence, and it is the level the
stamps below measure.**

**Not established:** which of the five. The leading static candidate is
`0x00452ad0` (27,390 bytes, 401 calls, per object, per frame); `0x0045d250`'s
collision pass is second (its `0x0045cab0` query is the only place an O(n²)
object-pair cost could hide); `0x004596e0` is the only one with its own time
accumulator, so it can do catch-up work independent of frame rate. Choosing
between them needs the run, not more static work.

**Trampoline feasibility.** All five mutate simulation state (flags, positions,
damage, docking) and four dispatch script callbacks through `0x0049f4c0` /
`0x0049f680`. Skipping, caching or rate-limiting any of them changes game logic,
not presentation: none is a safe "bound it" target as a whole. The only
behaviour-preserving lever is below this level — a specific redundant scan
inside one callee, once the run names it.

## 4. Proposed stamp sites (six, accumulate-only)

Same mechanism as [effect-pass-loop.md](effect-pass-loop.md) §4: an
accumulate-only stub (`LightCallBoundary`, QPC + 64-bit add, no tracker, no x87),
because these sites fire once per active sector per frame. Ledger format matches
`src/proxy/game_phase_sites.h` (`name, address, bytes, length, ret_pop,
rel32_offset`); `ret_pop` is 0 for all six and the containing routine is
`0x0043a360`–`0x0043a3d5`.

| # | Name | Address | Bytes | Len | rel32 off / target | Interval it opens |
| --- | --- | --- | --- | ---: | --- | --- |
| 0 | `sector_collide` | `0x0043a38e` | `56 e8 bc 2e 02 00` | 6 | 2 / `0x0045d250` | collision detect + respond |
| 1 | `sector_simulate` | `0x0043a394` | `56 e8 36 87 01 00` | 6 | 2 / `0x00452ad0` | per-object simulation body |
| 2 | `sector_post` | `0x0043a39a` | `56 e8 80 13 02 00` | 6 | 2 / `0x0045b720` | global object pass |
| 3 | `sector_pass_a_end` | `0x0043a3a0` | `8b 36 83 3e 00` | 5 | — | closes pass A for this sector |
| 4 | `sector_economy` | `0x0043a3be` | `56 e8 1c f3 01 00` | 6 | 2 / `0x004596e0` | opens pass B |
| 5 | `sector_pass_b_end` | `0x0043a3ca` | `8b 36 83 3e 00` | 5 | — | closes pass B (`0x004596e0` **and** `0x004526b0`) |

Four accumulated intervals: 0→1, 1→2, 2→3, 4→5. Splitting `0x004596e0` from
`0x004526b0` would need a seventh site at `0x0043a3c4` (`56 e8 e6 82 01 00`,
rel32 off 2 → `0x004526b0`), over the budget; pass B is therefore one interval
and the two smaller top-level calls of §1 fall into a residual:

`residual = input_part[0] - Σ(0→1) - Σ(1→2) - Σ(2→3) - Σ(4→5)` =
`0x0048f550` + the `0x0045b660` sweep + both list walks. No extra site is needed
for it: run94's command already enables `--game-phases`, which publishes
`input_part` 0.

### Validation performed

Mechanised in `verification/probe/verify_loop_phase_sites.py` (`PASS` against
the bottle EXE, `source_present: false` until `src/proxy/loop_phase_sites.h`
exists; the installed 47-site table is read for conflict checking).

- **Instruction boundaries.** Every span starts and ends on a decoded boundary
  of the gap-free decode of `0x0043a360`–`0x0043a3d5`; the `input_part=0` region
  `0x00403b09`–`0x00403b3a` decodes gap-free too. All spans ≥ 5 bytes, inside a
  loop body, non-overlapping.
- **Incoming edges.** Exactly `0x0043a384`, `0x0043a38c` → `sector_pass_a_end`
  and `0x0043a3b4`, `0x0043a3bc` → `sector_pass_b_end`, all on the span start;
  the four call sites have none. No direct branch targets any span interior, the
  routine has no indirect jump or call, and a raw-encoding sweep of every
  `.text` offset (`e8/e9/0f 8x/eb/7x/e0–e3`) finds no encoding into a span. No
  aligned dword reference to a span byte exists outside `.rsrc`.
- **rel32 contract.** Sites 0, 1, 2, 4 carry exactly one direct `call`, its
  field resolves to the documented callee, and the re-based arena copy at
  `0x10000000`/`0x71000000`/`0xf1000000` resolves to the same absolute target.
  Sites 3 and 5 are plain copies. The displaced `call` runs in the arena tail,
  so the callee sees a return address in the arena — the contract the existing
  `game_phase_clock`/`pump`/`channels` sites already rely on.
- **ESP.** No stack frame, no argument, no `add esp`; the only ESP writes are
  the three prologue pushes, the three epilogue pops, the alignment no-op and
  the `push esi` inside sites 0, 1, 2, 4. ESP is the routine's frame base at all
  six sites with no pending argument push, the `push esi` runs in the tail at
  the game's exact ESP, and all five callees are callee-pop, so the tail
  returns balanced.
- **Registers and flags.** `EBX` (= 1), `EDI` (= `*0x0060850c`), `ESI` (the
  current container), `EBP`, `ESP` are live across every span; sites 3 and 5
  write `ESI` themselves, in the tail after `popad`. Incoming flags are dead at
  all six. Sites 3 and 5 leave flags live to `jne 0x0043a380` / `jne 0x0043a3b0`,
  produced by the span's own `cmp DWORD PTR [esi],0x0` executing in the tail
  after `popfd` — the contract proven for frame site 5.
- **Re-entrancy.** `0x0043a360` has exactly one caller (`0x00403b17`) on the
  main-loop path and no callee calls back into it; all six sites are
  main-loop-thread only. A foreign-thread hit is impossible by construction but
  should still be counted and dropped.
- **Conflicts.** No span overlaps any of the 47 installed `game_phase` sites
  (checked mechanically), nor `X3M_SCENE_HOOK` (`0x004721b1`), the point-light
  patch (`0x004c27af`), `object_trace` (`0x004c5228`) or the chase/aim sites.

**Rate and cost.** Four stamps for pass A, two for pass B, per active sector
container per frame. If only the player's sector is active that is ~6 dispatches
per frame — three orders of magnitude below the `pass_phases` rate, so the
shared `game_phases` stub would also be affordable; the accumulate-only stub is
still preferable if the container count is the full sector list.

## 5. What this does not establish

- No runtime measurement; §3 ranks candidates from code size and structure only.
- How many containers loop A/B visit per frame, i.e. whether
  `[container+0x148] & 1` excludes all but the player's sector, is unresolved;
  that dword is written at `0x00464420` (script native-command dispatcher),
  `0x0043054e`, `0x0047a060` and `0x00486dbb`.
- "Sector" for the class-1 container, and "collision query"/"collision response"
  for `0x0045cab0`/`0x0045e130`, are inferences from layout and string xrefs.
  Whether `0x0045cab0` is O(n) or O(n²) in sector objects was not decided; that
  is the first thing to decompile if site 0's interval wins. `0x00452ad0` was
  surveyed by call histogram and string xrefs only; it was not decompiled.

## Reproduce

```sh
JAVA_HOME=.../openjdk analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3CallTree.java /tmp/out/mainloop_tree.txt 2 \
     00403840 0048f550 0043a360 0045b660
python3 verification/probe/verify_loop_phase_sites.py
```
