# The `cutevent` interval `0x0048f550`, the script task scheduler, and probes for the Mayhem 3 growth

2026-09-29. Static study of X3AP.exe, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` (the bottle
copy). Nothing here was observed at runtime. Decompiles came from Ghidra headless
(`tools/analysis/X3DecompileFunctions.java`, `X3XrefsTo.java`, `X3GrepInsns.java`,
`X3FunctionContext.java`) and stayed under `/tmp/x3-script-sched/`, untracked; every
byte, span, edge and caller claim below was re-read with `i686-w64-mingw32-objdump`
and the site table is re-checked by
`verification/results/script-task-scheduler/check_cutevent_sites.py` (`RESULT PASS`,
output `check_cutevent_sites_out.txt` beside it). "Established" means read from the
code; "inferred" is marked.

**Question.** Run 381 (`verification/results/run381-input-region/region_vs_phases_out.txt`)
puts the growing part of `pre_render` in the `cutevent` interval, the `call 0x0048f550`
at `0x00403b12`: 0.05 ms at frame 300, 12.3 ms at frame 6,600 (Mayhem 3 battle sector),
linear, `region_max` close to the p50 in every window (a smooth ramp, not spikes), while
the container passes stay at 0.7 ms and the sweep at 0 (all measured, run381). The
question was whether `0x0048f550` is a script task scheduler whose task set grows.

**Answer, in one line.** It is not a script scheduler. `0x0048f550` is the per-frame
**scene-graph animation tick**: it walks every scene node of the body/render
manager (recursively) and advances node animations ("cuts"), and it reaches the script
VM only through `CutEvent` callbacks fired from animation tracks. The script task
scheduler is `0x0049f770`, called three times earlier in the main loop
(`0x00403a7a`, `0x00403a93`, `0x00403aff`), outside the `cutevent` interval.

## 1. What `0x0048f550` runs each frame (established)

`R = *0x00608518` is the body/render manager ([body-format-bob1.md](body-format-bob1.md),
[camera-and-lights.md](camera-and-lights.md)). Lists are intrusive Amiga-style lists:
header `{head, 0, tailpred}`, node `+0` next, `+4` prev; a walk stops at the node whose
`[+0]` is 0. Hash tables are `{+0 buckets, +4 bucket count (power of 2), +8 next
auto-id, +0xc entry count}`: insert `0x004efbf0` does `add [tbl+0xc],1` at `0x004efc9e`
and rehashes to 4x buckets when count >= buckets; remove `0x004efd30` (EDI = table,
EDX = key) does `add [tbl+0xc],-1` at `0x004efd8f`; auto-id register `0x004efcc0`
(EAX = table).

`0x0048f550` (`0x13c` bytes, cdecl, no argument, plain `ret` at `0x0048f692`, one caller
`0x00403b12`) does four things in order:

| # | Code | Walk | Per item |
| --- | --- | --- | --- |
| A | `0x0048f550`-`0x0048f579` | list at `R+0x28`: nodes **not attached to any scene** (every node is created there, `0x00486d10`/`0x004885a0` AddTail to `R+0x28`; `0x00489e90`/`0x00489ff0` move a detached node back) | `0x0048f2b0(ECX = node)` |
| B | `0x0048f57a`-`0x0048f5c0` | every scene on `R+0x18` (0x58-byte objects made by `0x00489070`, registered in hash `R+0x10`), then the scene's root-node list `scene+8` and its second node list `scene+0x1c` (filled by `0x00489fa0`, which can also set `[node+0x270] |= 1`) | `0x0048f2b0(ECX = node)` |
| B' | `0x0048f5c1`-`0x0048f615` | the nodes `0x0048f2b0` collected into `0x00590908[]` (count `*0x006085d4`, capped at 0x400) | copy node `+0x30..0x6f` (position + 3x4 matrix) into `[node+0x1f8]` |
| C | `0x0048f616`-`0x0048f68f` | every entry of hash `R+0x84` (cut instances), iterated with `0x004efda0` ("next key after EDX" over table EDI) | `0x00493540(instance)`: for each of its `[+0x10]` nodes (array `[+0x14]`), two `0x004f0da0` + one `0x004f17f0` matrix update; if `[inst+0x74] == 2` (playing) also `0x004934b0` (counts its still-animating nodes; none → `0x004933e0` stops it: state 1, owner callback `[VM+0x34+group*0x18](arg, 1)`) |

`0x0048f2b0` (fastcall, ECX = node, SEH frame, plain `ret` at `0x0048f54b`, callers
`0x0048f569`, `0x0048f592`, `0x0048f5a8` and itself at `0x0048f2d8`) is a **post-order
recursive walk**: it first calls itself for every child on `[node+0xc]`, then:

1. `cut = [node+0x258]`; if 0, or `[node+0x1f4] & 0x200` (finished), skip to 4.
2. Look `cut` up in hash `R+0x80` (cut definitions); track `[node+0x25c]` must be in
   `[0, [cut+4])`; track record = `[cut+0x50] + track*0x58`.
3. `prev = [node+0x1e4]`, `now = [*0x00606f34+0x718]` (game ms clock), store
   `[node+0x1e4] = now`; with `start = [node+0x1e0]`:
   * if the track has events (`[trk+0x44]` count, `[trk+0x48]` array of 8-byte
     entries): **a per-millisecond loop** `for i in [prev-start, now-start)`
     (`0x0048f3c0`-`0x0048f4ac`, magic `0x3e0f83e1` = divide by 33): when
     `i % 33 == 1` and `[node+0x1f0]` (a script object id) is set, it calls the
     script method `"CutEvent"` with `("frame", i/33+1)` through `0x0049f680`
     (site `0x0048f452`); then every event whose time is in `(prev, now]` and
     `<= [node+0x1ec]` goes to `0x0048ee90(node, trk, idx)`, which parses the event
     text and may call `"CutEvent"` again (`0x0048f0e2`, `0x0048f1db`; `"endspeak"`
     handling);
   * clear `[node+0x1f4] & 0x4000`; if `& 1` (animating) and not `& 0x200`,
     `0x0048dc20(node, cut, now-start)`: keyframe interpolation (binary search
     `0x00496d30`, matrix build, 64-bit divides); at the end it either rewinds a
     looping animation (`[node+0x1f4] & 0x800`, `[cut+0x54] & 4` or track flag
     `0x100`) or sets `0x200` and notifies the owner through `[VM+0x34+[node+0x250]*0x18]`.
4. If `[node+0x1f4] & 0x1000`, append the node to `0x00590908[]` (for B').

So the per-frame work is **O(all scene nodes)** for the walk, plus **O(animated nodes
× (dt + events))** for step 3, plus **O(cut instances × their nodes)** for C. Nothing in
it walks a list of script tasks, and no per-node step grows with elapsed time: the
millisecond loop spans one frame's `dt`, the keyframe search is binary, looping
animations rewind their start. Growth must therefore come from **more items**
(nodes, animated nodes, cut instances) or from **costlier `CutEvent` script calls**.

Scene-node facts used above (established): nodes are `malloc(0x270)` (`0x00486d10`,
`0x004885a0`; 0x790 bytes when `[node+0x12c] & 1`, freed size in `0x00487be0`), zeroed,
registered with an auto-id in hash **`R+0xc`** (`0x00486d78`, `0x00488605`,
`0x00488cd6`; id at `node+0x28`), and destroyed only by `0x00487be0`, which removes the
id from `R+0xc` (`0x00487d70`). The create/attach/detach code read here keeps a node
on exactly one list: `R+0x28`, a
parent's child list `+0xc` (`0x00489f20`), `scene+8` (`0x00489da0`) or `scene+0x1c`
(`0x00489fa0`). Hence (inferred) **nodes visited per frame by `0x0048f2b0` =
`[[R+0xc]+0xc]`**, the live node count. Cut instances are 0x80-byte objects made by
`0x00493280` (registered in `R+0x84` at `0x004932e6`), created by the B3D command
group (`0x004958d4`) and by the SA group through `0x0045f180` (`0x00464588`, the same
dispatcher `0x00460630` that contains `0x00464420`), and removed only by `0x00493330`
(`0x004933b6`; callers `0x0049590b`, `0x0045f23e`).

## 2. The script task scheduler (established unless marked)

`VM = *0x006085e4`.

| Field | Meaning | Evidence |
| --- | --- | --- |
| `[VM+0]` | hash table of **all live tasks** (key = task id at `task+8`); `[[VM+0]+0xc]` = live task count | alloc `0x004a21e0` (`0x004a2207`-`0x004a220f`), free `0x004a2420` (`0x004a247c`) |
| `[VM+8]` | runtime CODE base; task IP is relative to it | `0x004a26cd` |
| `VM+0x12d8` | list header of **runnable** tasks (`task+0x3a` = 1) | `0x0049f430`, `0x004a373b`-`0x004a3754` |
| `VM+0x12e4` | list header of **suspended** tasks (`task+0x3a` = 2): a native returned with `task+0x20 != 1` | `0x004a437e`-`0x004a43d7` |
| `[VM+0x1434]` | the task being run | `0x0049f4a6`, `0x0049f770` |
| `VM+0x30+g*0x18` | native group record `g`: `+0` dispatch function, `+4` owner-notify callback | `0x004a38e4`-`0x004a38ed`; `0x004933e0`, `0x00487be0` |
| `[VM+0x12d0]` | hash of script-created objects (negative object ids) | `0x0049f1e0` |
| `[VM+0x1454]` | context passed as first argument to every native dispatcher | `0x004a38ff` |

**Scheduler.** `0x0049f770` sets `[VM+0x1434]` to the head of the runnable list and calls
the interpreter on it, re-reading the head each time, until the list is empty or the
interpreter returns 0. Callers: main loop `0x00403a7a`, `0x00403a93`, `0x00403aff` (all
before the `input_part=0` region `0x00403b09`) and `0x00497296`. It never scans the
suspended list; only the task-kill helper `0x004a2520` (from opcode handler
`0x004a3009`) and a task-dump native in `0x004ab880` (`0x004ae0fc`, `0x004ae233`) walk
both lists. So tasks are **not** all ticked every frame: runnable tasks run, suspended
ones sit on `VM+0x12e4`. How a suspended task is made runnable again (due times, which
native wakes it) was **not** traced.

**Synchronous calls.** `0x0049f4c0`, `0x0049f570` and `0x0049f680` (cdecl; `0x0049f680`
takes `(0, 0, object_id, method_name, 1, argc, cell...)`, 8 dwords at the CutEvent
sites, caller pops `0x20`) resolve the method with `0x0049f330` (object → context via
`0x0049f1e0`, class chain `[ctx+8]`, parent `+0x14`, method lookup `0x004b0740`),
allocate a task (`0x004a21e0`), push the arguments on its cell stack, and hand it to
`0x0049f430`, which pushes the two return cells (tags 3 and 10), links the task at the
**tail of the runnable list**, sets `[VM+0x1434]` and runs the interpreter at once.

## 3. Interpreter and native-dispatch ABI (established unless marked)

**Interpreter `0x004a26a0`.** ECX = VM, one stack argument = task, `ret 4`
(`0x004a437b`); EAX = 0 when a native returned 0 (`0x004a390e` → `0x004a4368`), which
also stops the scheduler loop. Callers: `0x0049f4ac`, `0x0049f799`, `0x004a3760`
(nested call opcode). Opcodes: byte at `[VM+8]+[task+0x1c]`, two-level table
`0x004a4688` → `0x004a4490` (126 entries, as in
[selection-native-vm.md](selection-native-vm.md)).

Task layout: `+0/+4` list links, `+8` task id, `+0x10` stack capacity (cells),
`+0x14` cell stack base (5-byte cells: tag byte + dword), `+0x18` stack index,
`+0x1c` IP relative to `[VM+8]` (at creation: the method's CODE entry, from the
method row `[0]` in `0x0049f330`), `+0x20` u16 native continue flag (1 = continue),
`+0x22` u16 native argc, `+0x24` native argument pointer, `+0x28` return cell,
`+0x30/+0x34/+0x38` owner callback id/argument/word (notified by `0x004a2420` through
`0x0049f230`), `+0x3a` u16 state (1 runnable, 2 suspended), `+0x3c` object context.
**Names (inferred from the task-dump native at `0x004ae112`-`0x004ae1ca`):**
`class = [[task+0x3c]+8]`, class chain `+0x14`, method count `[class+0x28]`, method
rows `[class+0x2c]` (16 bytes: `+0` CODE entry, `+4` name), class name `[class+0]`; the
method is the row with the greatest entry `<= [task+0x1c]`. A probe should record the
CODE entry offset and map it to `class.method` offline with the loader-based decode of
[selection-native-vm.md](selection-native-vm.md) (6,818 method entries), not search
names at run time.

**Native call.** Every native command of every group goes through one site,
`0x004a3907 call ecx` (continuation `0x004a3909`). Operands: group = u16 `[edi-4]`,
command id = u16 `[edi-2]` (EDI was advanced by 4 at `0x004a388d`/`0x004a3898`);
at `0x004a38ff` three arguments are already pushed: `[esp]` = command id
(zero-extended), `[esp+4]` = argc, `[esp+8]` = argument cells (`EBX+5`); task = ESI,
VM = EDX. The
dispatcher is `int __cdecl dispatch(ctx = [VM+0x1454], task, u16 cmd, int argc,
cell* args)`, caller pops `0x14` (`0x004a3909`); EAX = 0 aborts the task step, and
after return `task+0x20 != 1` suspends the task. `0x00493b40` (B3D group, `switch` on
the third argument) and `0x00460630` (SA group, 35,513 bytes; `0x00464420` is an
instruction inside it, not an entry) have this signature (the latter assumed from the
shared call site, its body was not decoded here).

## 4. What decides "more items" versus "costlier items"

No code patch is needed for the first split: four counts are plain fields the Present
hook can read. The Present hook runs on the main-loop thread between frames
([main-loop-input-region.md](main-loop-input-region.md) §6), and all these structures
are mutated by main-loop code (the only cross-thread mutation `engine_memory.h`
documents is a registry rehash, which replaces a bucket array, not a table header).
Each value is two validated `engine_memory::read`s of image globals and a heap table
header; a stale or refused read costs one row, never a fault.

| Read at Present | Meaning | Growth would mean |
| --- | --- | --- |
| `[[*0x00608518+0x0c]+0x0c]` | live scene nodes = nodes walked by `0x0048f2b0` per frame (inferred equality, §1) | leaked or accumulating nodes; walk cost ∝ count |
| `[[*0x00608518+0x10]+0x0c]` | live scenes (each walked twice) | scene leak |
| `[[*0x00608518+0x84]+0x0c]`, `+0x04` | live cut instances, bucket count (interval C iterates buckets + entries) | cut instances created by scripts (B3D, SA via `0x0045f180`) and never destroyed |
| `[0x006085d4]` | nodes with `+0x1f4 & 0x1000` in the last walk (capped 1024) | bounded; sanity value |
| `[[*0x006085e4+0x00]+0x0c]` | live script tasks (runnable + suspended) | tasks that never finish (e.g. a `CutEvent` handler that suspends) |

Requirements: refuse when `*0x00608518` or `*0x006085e4` is 0 (before load), when a
table pointer is 0, or when a count is negative or above 10^7; log once per 300-frame
window next to `cutevent_p50_us`. Counting the length of `R+0x28` or of the two task
lists needs a pointer walk; at the likely sizes (tens of thousands of nodes, inferred)
that is milliseconds of validated reads, so it belongs in the in-code counters of §5,
not at Present.

Per-item cost, for reading the fit (inferred from instruction counts, not measured):
a plain node visit is ~34 instructions including two `fs:` accesses and touches three
cache lines of a 0x270-byte node; an animated node adds ~`dt` iterations of a ~15-
instruction loop, the event scan and `0x0048dc20` (hundreds of instructions); a
`CutEvent` call adds method resolution, task allocation, hash insert/remove and the
interpreter. If `cutevent_us` rises with live nodes at a slope of tens of ns per node,
the walk owns it; a slope in the microseconds per item points at animated nodes,
`CutEvent` or cut instances.

## 5. Proposed probe set (one diagnostic build)

All sites are main-loop-thread only by construction: `0x0048f550` has the single
caller `0x00403b12`, `0x0048f2b0` is reached only from it and itself, `0x0048ee90`
only from `0x0048f4da` and `0x0048eb2b`. The stamps add no logic: they replay the
displaced instructions exactly. Mechanisms already in the tree: the lean
accumulate-only loop-phase stub (`src/proxy/loop_phases*.{h,cpp}`, 92.3 ns per stamp,
measured in its CPU fixture), counter-only `inc dword [abs32]` tails
(`collide_narrow_census`, about 1.5 ns per hit under FEX, measured,
[sector-collide.md](sector-collide.md) §11.7) and the site-5 call bracket of the same
census for timing a displaced call.

| # | Name | Site | Bytes | Kind | Incoming edges onto start | Flags / registers | Rate (inferred) and cost |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `cutevent_views` | `0x0048f57a` | `8b 0d 18 85 60 00` (`mov ecx,[0x608518]`) | lean stamp, closes A, opens B+B' | `0x0048f565` (`je`, empty `R+0x28` list) | flags dead (`cmp [edi],0` at `0x0048f583`); EBX (collected count) live; stub saves all | 1/frame, 0.09 us |
| 2 | `cutevent_cuts` | `0x0048f616` | `8b 15 18 85 60 00` (`mov edx,[0x608518]`) | lean stamp, closes B+B', opens C | `0x0048f5c5` (`jle`, nothing collected) | flags dead (`xor edx,edx` at `0x0048f622`) | 1/frame, 0.09 us |
| 3 | `node_visit` | `0x0048f2e4` | `8b 8d 58 02 00 00` (`mov ecx,[ebp+0x258]`) | counter | `0x0048f2d4` (`je`, no children); fall-through from the child loop | flags dead (`test ecx,ecx` next); `inc` changes only flags | once per node per frame; ~1.5 ns × live nodes (0.15 ms at 10^5) |
| 4 | `node_track` | `0x0048f366` | `8b 0d 34 6f 60 00` (`mov ecx,[0x606f34]`) | counter | none | flags dead (`imul` at `0x0048f36c`) | once per node with a live cut track |
| 5 | `node_animate` | `0x0048f50d` | `e8 0e e7 ff ff` → `0x0048dc20` | counter on `claim_call` (`inc; jmp 0x0048dc20`) | none | callee's flags; return address unchanged | once per animating node |
| 6 | `cut_frame_call` | `0x0048f452` | `e8 29 02 01 00` → `0x0049f680` | timed call bracket, key `[esp+8]` = object id and `EBP` = node (`[ebp+0x258]` cut id) | none | EAX (bool result) and the 8 pushed arguments preserved; caller pops `0x20` at `0x0048f457` | per scripted animated node every 33 game ms; ~0.3 us each |
| 7 | `cut_event_call1` | `0x0048f0e2` | `e8 99 05 01 00` → `0x0049f680` | as 6, key `[esp+8]`, node = ESI | none | as 6 (`add esp,0x20` at `0x0048f0e7`) | per fired track event |
| 8 | `cut_event_call2` | `0x0048f1db` | `e8 a0 04 01 00` → `0x0049f680` | as 6, key `[esp+8]` | none | as 6 (`add esp,0x20` at `0x0048f1e0`) | per fired track event |
| 9 | `native_call` | `0x004a38ff`-`0x004a3909` | `8b 82 54 14 00 00 56 50 ff d1` | timed bracket around the replayed `call ecx`, key (group u16 `[edi-4]`, command `[esp]` at the span start) | none (no decoded edge, no opcode-table entry, no raw encoding) | flags dead (callee clobbers; `test eax,eax` after); ESI/EDI/EBX/EBP live and preserved; must be reentrant (natives re-enter the interpreter) | every native call of every phase; ~0.2 us each |

Intervals: `region_cutevent` (installed site 6, `0x00403b12`) → 1 = A (unattached
nodes), 1 → 2 = B + B' (scene trees + transform copy), 2 → `region_containers`
(`0x00403b17`) = C (cut instances). Sites 1-2 reuse the `--loop-phases` group and
cost 0.2 us per frame. Sites 3-5 give per-frame counts (p50 per window) that, next to
the §4 reads, turn the three intervals into per-item costs by a two-regressor fit over
the 22 windows of a run. Sites 6-8 give `CutEvent` count and time per frame with a
top-N by object id per window (map the id to a class through
`0x0049f1e0`'s rules offline or at Present: `id >= 0` → game object, `id < 0` →
`[VM+0x12d0]`); their bracket must tolerate nesting: a `CutEvent` handler cannot re-enter `0x0048f550`
(single caller), but through B3D natives it can reach `0x0048eab0` → `0x0048ee90`
(sites 7-8 again, and `0x0048dc20` from `0x0048ea9d`/`0x0048ebaf`, which site 5 does
not count) and it re-enters the interpreter and site 9.

Site 9 is the only one whose rate is unknown: it fires for every native call in every
phase, not only inside `cutevent`. Carry it only if sites 6-8 show that `CutEvent`
time owns the growth; accumulate only while a `cutevent` bracket is open (the stub
still pays its dispatch everywhere), key by (group, command), and handle an SEH unwind
through the interpreter (frame `0x0052f298`) as an orphaned token dropped by ESP.

All nine spans pass `check_cutevent_sites.py`: exact bytes, whole instructions of the
decoded routine (`0x0048ee90`, `0x0048f2b0`, `0x0048f550`, `0x004a26a0` up to its jump
table), no decoded or opcode-table edge into an interior, no raw `.text` encoding into
an interior that starts on a real instruction (three byte coincidences inside other
instructions' operands are listed and excluded), the rel32 targets of the four call
sites, and no aligned data reference to a span byte outside `.rsrc`. Conflicts with
installed sites: none of these addresses is in `loop_phase_sites.h` or the game-phase
table (not re-checked mechanically here; the production verifier should add them to
`verify_loop_phase_sites.py`).

## 6. Levers (none built; each depends on what §5 finds)

Behaviour-preserving, exact:

* **The millisecond loop** `0x0048f3c0`-`0x0048f4ac`. It iterates every game
  millisecond of the frame per animated node with events only to find `i % 33 == 1`.
  Stepping straight to the next such `i` (with C truncating `%` for negative `i`)
  issues the same `CutEvent("frame", i/33+1)` calls in the same order and then the
  same event scan. Worth it only if sites 4-5 show thousands of animated nodes.
* **Interval C iteration.** `0x004efda0` re-looks-up the current key and scans empty
  buckets: O(buckets + entries) per frame, and no shrink of the bucket array was
  found (inferred: rehash `0x004efeb0` is reached from the insert path).
  Iterating the bucket array directly is equivalent while no callee mutates the table
  (`0x00493540` does not; `0x004934b0` → `0x004933e0` changes state, not membership;
  its owner callback could, which must be excluded first).

Structural, needs a proof of equivalence:

* **Skipping inert subtrees** in `0x0048f2b0` (no cut, no `0x1000` flag anywhere
  below). Post-order and the `0x00590908[]` order must be kept, and `+0x258`/`+0x1f4`
  are written by many natives, so an index would have to hook every writer.
  Not a candidate before the counts exist.

Changing logic (out of scope): destroying orphaned nodes on `R+0x28` or stopped cut
instances, rate-limiting `CutEvent` callbacks or animation updates, skipping the walk
for off-screen scenes. If the growth is a leak (nodes, instances or tasks rising
without bound), the lever is at the creator, which the same counts would name.

## 7. Not established

* Which of nodes, animated nodes, cut instances or `CutEvent` cost grows in run381;
  §4-§5 decide it in one flight.
* That every live node is on one of the walked lists (the equality in §1 is inferred
  from the create/attach/detach/destroy code; site 3 checks it).
* How suspended tasks are woken; per-task wait/due fields.
* The names behind `[class+0]` and row `+4` are char pointers (inferred from the task
  dump's conversion calls).
* `0x00460630`'s signature and the owner of `0x0045f180` cut instances in a battle.

## Reproduce

```sh
cd /tmp/x3-script-sched   # scratch; decompiles stay here
JAVA_HOME=.../openjdk analyzeHeadless <project dir> X3Render -process X3AP.exe -readOnly \
  -noanalysis -scriptPath tools/analysis -postScript X3DecompileFunctions.java out.txt \
  0048f550 0048f2b0 0048dc20 0048ee90 00493540 004934b0 004933e0 0049f770 0049f430 0049f330
python3 verification/results/script-task-scheduler/check_cutevent_sites.py
```
