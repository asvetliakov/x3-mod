# The capital gate queue in `x3story.obj`: bytecode, patch sites and a file-level patch

2026-10-12, static study of `X3AP.exe` (SHA-256 `fdbf3418…`) and the two installed `L/x3story.obj` builds; nothing
was launched. **Stock AP 3.3**: `addon/04.cat`, 2,312,979 B, decoded SHA-256 `ed5786a0…faff7a`, build id
1510658513. **Mayhem 4** (the copy the game loads on this install, the newest catalogue carrying the file):
`addon/07.cat`, 2,182,503 B, SHA-256 `2c3101a8…199298`, build id 1462141839. Behaviour of the queue is in
[ship-navigation-ai.md](ship-navigation-ai.md) §10.1–10.2; this note covers what a patcher needs. Scripts and outputs
are under [`verification/results/ship-navigation-ai/gate-queue/`](../../verification/results/ship-navigation-ai/gate-queue/):

| Script | Output | What it establishes |
| --- | --- | --- |
| `kc_obj.py`, `gate_sites.py` | — | file reader, instruction widths, decoder, symbolic stack tracker, pattern-located patch sites |
| `gate_queue_listing.py` | `gate_queue_listing.txt` | per build: header, member map, decoded listings of the slot methods (stock), `__EnterHyperspace` windows and suspend points, patch-site table; Mayhem compared opcode by opcode |
| `gate_queue_patches.py` | `gate_queue_patches.txt` | options A, A5, A6, A7, B, C built in memory for both builds, with the byte changes and 7–8 structural checks each, `RESULT PASS` |
| `story_signature.py` | `story_signature.txt` | which `x3story.obj` the signature in `addon/x3files.xml` covers |
| `exe_checks.py` | `exe_checks.txt` | 64 EXE byte strings and table entries quoted below, `RESULT PASS` |

Decompiler output stayed in the session scratchpad. Listings carry decoded instructions, not script bytes; the only
bytes printed are the mod-authored replacements.

**Answer.**

* **A constants-only patch is viable** for the cooldowns and the waits, in both builds, at the same offsets for every
  instruction: `pushb 60` → `pushb N` (1 byte each, two sites), the `SA_Idle` and poll intervals (`pushw`, 2 bytes
  per value, three call sites). It does not change what dominates the in-sector wait: one capital per gate pair holds
  the through slot from its grant to the end of its 900 ms warp, including its whole order-5 leg **[m]** mechanism.
  The 60 s cooldown applies in the player's sector only after a **direction change**, and to every out-of-sector
  transit **[m]** code, **[i]** that a native object (`m3`) exists only in the player's sector.
* **A clearance-test patch needs no new native.** `SA_Distance`, `SE_ObjectExists`, `SE_GetExprType`, the KC method
  `GetObjectID` and the gate's own arguments suffice. Option B (§6) rewrites `__requestWarpThroughSlot` (205 of
  210 B) and `ReleaseSlot` (172 of 197 B) in place; every other byte and offset is unchanged.
* **Save compatibility is by offset, not by checksum.** Saves store task IPs and return addresses as raw CODE offsets
  and the object's build id; with an unchanged build id they are used as is, otherwise the loader needs a `PTCH`
  remap table for the saved id (stock has 29, Mayhem none) or the load fails **[m]**. All options keep the build id,
  the length and every instruction start outside their windows; the suspend points of `__EnterHyperspace` keep their
  IPs and stack depths (C removes one and keeps its continuation) **[m]** checks.
* **There is no CODE checksum.** The only integrity test is an RSA signature over the file listed in
  `addon/x3files.xml`. It covers the stock AP object only. Mayhem's object fails it, so this install is already
  "modified" **[m]** digests, **[i]** runtime flag. A patched object of either build fails it too. The test refuses
  nothing: it sets the KC "modified" state and blocks `SE_SignGameStats`.
* **File placement (correction to the brief).** The story path is `L\x3story` + `.obj` relative to the working
  directory, with no `addon` prefix. A loose file wins over every catalogue. Statically, the override must be
  `<game dir>\L\x3story.obj` (plain bytes, not XOR-encoded). An `addon\L\x3story.obj` is not looked up **[m]**
  static, not verified at run time.
* **Stock and Mayhem need different patches.** The opcode sequences of every involved method are equal, but the method
  offsets and member numbers differ (class 2014: stock m14..m18 = Mayhem m16..m20, GetAge global g8 = g9) **[m]**.
  The site locator and the B assembler derive both from the build.

## 1. Encoding rules for a patcher (Q1)

**File** (loader `0x0049d030`, `exe_checks.txt`) **[m]**:

* `STOR`, BE32 version (must be 801, `0x0049d276 cmp eax, 0x321`), BE32 build id (stored in `VM+0x143c` at
  `0x0049d2a7`, written into every save). Then the tagged sections `CODE` (BE32 size, bytes), `SYMB`, `VARS`, `CLAS`
  and `PTCH` (BE32 size, BE32 count, records), and `STRG` (BE32 size, chained-difference encoded). The file ends with
  `/STO`. Both builds have CODE at file offset 20: **file offset = CODE offset + 20**. CODE byte 0 is unused, and
  instructions start at 1.
* In a catalogue the bytes are XOR 0x33. The reader XORs only for catalogue handles (`0x004e922b..0x004e9260`), so
  a loose file holds the plain bytes.

**Instructions** (widths from the byte-swap pass `0x0049e1a0`; the interpreter `0x004a26a0` dispatches through
`0x004a4688` → `0x004a4490`) **[m]**:

| Width | Opcodes |
| --- | --- |
| 1 | everything not listed (e.g. `01`–`04` push 0..3, `10` index, `17` indexed store, `24` drop, `2e` this, `46` add, `4b` sub, `5a`–`5f` ne/eq/lt/gt/le/ge, `64` not, `83` return) |
| 2 | `05` (s8), `47`, `4c` |
| 3 | `06` (s16), `0d` load stack slot, `0e` global, `0f` member, `11`–`16` (`14` store slot, `16` store member), `18`–`1d`, `1f`, `20`, `21`, `23` drop n, `28` make array n, `29`, `2c`, `30`, `48`, `4d`, `6e` frame |
| 5 | `07` (s32), `0b` string (STRG offset), `32` jmp, `33` jump-if-true, `34` jump-if-false, `35`–`3a`, `49`, `4e`, `82` native, `84`–`88` calls |
| 9 | `0a` |
| variable | `78`: 3 + (n + 2)·4; `79`: 5 + ((a + b)·2 + 1)·4 |

* **Immediates:** `01`–`04` push 0..3, `05 <s8>`, `06 <BE s16>`, `07 <BE s32>`. Keep the opcode when patching a value,
  so `pushb` stays within −128..127 and `pushw` within ±32767. Comparisons work on int/double cells only. The helpers
  `0x004a8b40`/`0x004a8bb0` return false for any other cell type, so `le` (`!(a > b)`) is true for an array operand.
* **Jumps** `32/33/34 <BE32>` are **absolute CODE offsets** (`0x004a317a`: `edi = [edi] + [VM+8]`). `33`/`34` pop
  the condition.
* **Stack:** `0d n` pushes a copy of the cell n−1 below the top, and `14 n` stores the top there (it keeps the value).
  Locals are therefore addressed relative to the current depth. Below the first local sit the context cell, the
  return cell and argc; argument 0 is slot −4 in the tracker's numbering. A call pushes the arguments
  last-to-first, then argc; `85` (method by name on an object) pushes the object last; `86` pushes a class id.
  `82 <BE32 STRG offset of the name>` calls a native; `85`/`87` carry a STRG name; `84`/`86`/`88` carry a CODE entry. Return (`0x004a3986`) takes the top cell as the
  value with the context (tag 10) and return offset (tag 3) directly below it, so `drop` to depth 0, push the
  value, `83`. `6e N` at entry grows the stack to used + N + 4 cells (`0x004a35c8`). The compiler sets N to the
  method's maximum depth (3,950 of 5,098 methods checked exactly equal).
* **Load-time rewriting, in memory only** **[m]**. `0x0049e1a0` swaps every multi-byte operand to little endian and
  replaces each native name with u16 group + u16 command (0 if unresolved). It ends by calling the peephole pass
  (`0x0049e426` → `0x0049e4f0`). That pass rewrites the **first opcode byte** of fixed patterns (push-const + jump,
  `not` + jump, load + index, store + drop, increment, `0d 1 14 2 24 83`), keeping the summed widths, and threads
  jump operands through chains of `jmp` and constant conditionals. All of it is semantics-preserving and never
  touches the file. The patcher writes big-endian file bytes and need not model it. A patch should only use native
  names that already occur in STRG, so that they resolve; every name the options below use does.
* **No checksum or size check on CODE** beyond the section bounds; integrity is only the signature of §8.

## 2. The slot methods (class 2014) **[m]**

Entries, stock / Mayhem: `Create` 906ee / a6559, `RequestWarpInSlot` 90a59 / a68c4, `RequestWarpThroughSlot`
90b6e / a69d9, `__requestWarpThroughSlot` 90bca / a6a35 (210 B), `ReleaseSlot` 90c9c / a6b07 (197 B),
`GetWarpInPos` 90d61 / a6bcc, `GetDestinationGate` 9139b / a7206, `2004::ReleaseFromSlot` aae41 / c22e7. Full
decoded listings of the stock methods are in `gate_queue_listing.txt`. Mayhem's opcode sequences are identical; only
member, global and offset operands differ.

**Fields** (stock numbering): `m14` = `SE_ArrayAlloc(40)` in `Create` (`pushb 40` at 907cd): ships in [0..19],
release stamps in [20..39]. `m15` = through-slot holder (a KC ship object; 0 when free). `m16` = cooldown deadline
(age seconds). `m17` = side of the last grant (1 = requested at the controller gate, 0 = at its partner, −1 = reset).
`m18` = "is the controller" (set by the first gate of the pair that handles a request). Only these methods touch
m14–m18, and class 2014 has no subclasses (`gate_queue_listing.txt`).

* **`RequestWarpInSlot(ship)`**: `now = g8.GetAge()`; loop `i < 20` (`pushb 20` at 90a70). It clears dead entries
  and the ship's own entry, then takes the first empty slot with `m14[i + 20] <= now` (90b09..90b16; the `+20` at
  90adc, 90b0c, 90b41). On success it calls `ReleaseSlot(ship)`, stores the ship and returns i, else −1.
* **`RequestWarpThroughSlot(ship)`**: on the controller (`m18`) it calls `__requestWarpThroughSlot(ship, 1, m3)`.
  Otherwise, if the destination gate `IsWarpController()`, it forwards `(ship, 0, this.m3)` there. Otherwise it
  becomes the controller (`m18 = 1`) and calls with side 1. The third argument is **the requesting (entry) gate's
  `m3`**.
* **`__requestWarpThroughSlot(ship, side, gm3)`**:
  1. `now = GetAge`.
  2. If `m15` is dead or is the requester: `m15 = 0; m16 = 0; m17 = −1`.
  3. Grant if `m15 == 0` and (`m16 <= now` (90c2b) or (`gm3 != 0` (jf at 90c3a) and `side == m17`)): call
     `ReleaseSlot(ship)`, set `m15 = ship; m16 = 0; m17 = side`, return 0 (the grant jf is at 90c6a). Else return −1.
* **`ReleaseSlot(ship)`**: a non-controller forwards to a controller destination. Then, if `ship == m15`:
  `m15 = 0` (90cf3), `m16 = now + 60` (**`pushb 60` at 90cfb**, file +0x90d0f). The ship's warp-in entry is
  cleared with `m14[i + 20] = now + 60` (**`pushb 60` at 90d44**, file +0x90d58). Mayhem: a6b66 and a6baf.

## 3. The waits in `2004::__EnterHyperspace` **[m]**

Entry bca8e / d524d, loop head bcb21 / d52e0. Every failed request ends with `jmp <loop head>`, i.e. the ship goes
through the whole loop again (attack, gate and collision checks) after each wait.

| Step | Stock CODE (Mayhem) | Decoded | Constants |
| --- | --- | --- | --- |
| warp-in request | bcc2b (d53ea) | `this; push1; ldloc 13 (gate); callobj RequestWarpInSlot` | — |
| refused, ship in sector | bcc55..bcc84 (d5414..) | `SA_SetSpeedLimit(m3, m93)`, `SA_SetFlightMode(m3, 1)`, `SA_Idle(m3, 1000 + SE_Random(10000))` | `pushw 1000` at bcc6d, `pushw 10000` at bcc70 |
| refused, out of sector | bcc89..bcc98 | `TI_WaitRandomForInterrupt(1000, 10000, this)`, `jmp bcb21` | `pushw 10000` at bcc8a, `pushw 1000` at bcc8d |
| through request | bcd05 (d54c4) | `callobj RequestWarpThroughSlot` (big ship and `slot_type == 1`) | — |
| refused | bcd2c..bcd3b (d54eb..) | `TI_WaitRandomForInterrupt(1000, 10000, this)`, `jmp bcb21` | `pushw 10000` at bcd2d, `pushw 1000` at bcd30 |
| rotate | bce4d..bce67 (d560c..) | `SA_RotateToAngles(m3, SA_GetRotAlpha(gate), SA_GetRotBeta(gate)) == 1` → phase 2, `slot_type 1` | — |
| out-of-sector warp-out | bd0cb (d588a) | `TI_WaitRandomForInterrupt(100, 1000, this)` while `!SA_IsWarpOutSlotAvailable(gate.m3)` | `pushw 1000`, `pushb 100` |

* **`TI_WaitRandomForInterrupt(a, b, obj)` waits `a + rand(b)` ms** (TI case 6, `0x004b13ee..0x004b1401`). The polls
  are therefore **1–11 s**, not 1–10 s as §10.2 had it. `SA_Idle` gets the same 1–11 s.
* **The half speed is not a script constant.** Order 0x1d moves at vmax/2 in the engine (`0x00453ec1`/`0x00453eea`,
  §10.2). The script only sets `SA_SetSpeedLimit(m3, m93)` (Mayhem m96) and `SA_SetFlightMode(m3, 1)` before
  `SA_Idle`.
* **Suspend points** (the saved task IP when the task blocks; `gate_queue_listing.txt`): bcc83 `SA_Idle`, bcc96 and
  bcd39 `TI_WaitRandomForInterrupt`, bcda7 `SA_AvoidObjectCollision`, bcddf/bcdf8 order 4, bce68 `SA_RotateToAngles`,
  bcec1 order 5, bcf09 `SA_WarpObjectAway`, bd0d0 the out-of-sector wait. There are none in the class-2014 methods:
  they call only `GetAge`, `SE_ObjectExists` and each other, none of which blocks.
* **Other users of the through slot:** `2004::JumpToSector` (bd7a3) requests it for class-2073 ships before a jump
  into a gate. It retries with `TI_Delay(1000)` and `TI_Delay(5000)` and releases through `ReleaseFromSlot`. Any
  change to the slot logic applies there too.
* The interpreter has **no op-count preemption**: `SE_SetMaxOpCount` writes `VM+0x1604/0x1608`, which nothing reads
  (`text` sweep). A task's IP at save time is therefore a method entry, a blocking-native continuation, or a return
  offset of a frame below one **[m]** absence of readers.

## 4. Natives and KC methods for a clearance condition (Q2) **[m]**

| Call | Group/id, case | Convention at KC call sites | Result |
| --- | --- | --- | --- |
| `SA_Distance(a, b)` | SA 272, `0x004632c4` | `ldloc b; ldmem 3 (a); push2; native` (in `1000::GetDistance` 2e03b) | `|pos a − pos b|` in units via `0x00419220`; 0 if either id is unknown (`0x004607d4`); **no sector test** |
| `1000::GetDistance(other)` | KC 2e018 / 2d812 | `ldloc other; push1; <object>; callobj GetDistance` | `SA_Distance(m3, other.m3)` when both have `m3`, else `SE_DistanceAA(GetPos(), other.GetPos())` on KC positions |
| `GetObjectID()` | KC (1000) | `push0; <object>; callobj GetObjectID` | the object's `m3` (engine id, 0 out of sector **[i]**) |
| `GetEnvironment()` / `GetSector()` | KC (1000) | `push0; <object>; callobj …` | `m1` / the sector of `m1` |
| `GetPos()` | KC (1000; 2004 refreshes) | `push0; <object>; callobj GetPos` | `m0` array, refreshed from `SA_GetPosAndRot(m3, m0)` when `m3` |
| `SE_ObjectExists(obj)` | SE 55 | `ldmem 15; push1; native` | KC liveness |
| `SA_FindObject(id)` | SA 2 | `ldmem 3; push1; native` (`__LeaveHyperspace`) | engine liveness |
| `SA_GetTotalSize(id)` / `SA_GetSize` | SA 58 / 57 | `ldmem 3; push1; native` | radii in units |
| `SE_DistanceAA(p, q)`, `SE_Distance3`, `SE_IsPosInRangeA/3` | SE 72, 70, 73/74 | arrays / triples | KC-side geometry |
| `SE_GetExprType(v)` | SE 60, `0x004ae474` | `<v>; push1; native` | the cell tag (`0x004ae477`); 8 = array (`mkarr` result, `0x004a882e`), 11 = string as the KC compares it |
| `SA_IsWarpOutSlotAvailable(gate id)` | SA 325, `0x00465ab4` | `ldloc id; push1; native` | 1 if any of the gate's **6 warp-out slots** is free; `SA_StartInHangar` into a gate reserves one for 20,000 ms (`0x00443b53`). Time based, not a clearance test |

The script already uses the idiom: `__LeaveHyperspace` polls
`SA_Distance(m3, gate.GetObjectID()) < 1750000 + 2·SA_GetTotalSize(m3)` with `TI_Delay(100)` (bd49a). **The gate
stores no "last ship through" in stock:** `ReleaseSlot` sets `m15 = 0`, so the holder's identity is lost at release.
Option B keeps it.

## 5. Where the exit is known (Q3)

* KC objects are global. The controller gate's methods can read any ship or gate object, including its partner
  (`GetDestinationGate`). Distance between engine objects only means something when both are in the same, player-held
  sector **[i]**. `1000::GetDistance` falls back to KC positions otherwise.
* The holder transits atomically from the other tasks' view **[m]** calls, **[i]** step 2:
  1. `__EnterHyperspace` returns 3 after `ReleaseFromSlot` and `Remove()`.
  2. `__FlyToNextSector` calls `__LeaveHyperspace(gate)` (bd16d) in the same task, with no blocking call before
     `AddShip` **[i]** (`Activate()` not traced).
     That sets `m0` to the exit gate position, `Activate()`s a ship without `m3`, adds it to the destination sector
     and calls `SA_StartInHangar(m3, gate, −1)`.
  3. Then `__LeaveHyperspaceWarpIn` (bd671) runs `SA_WarpObjectIn(m3, gate)` (order 0x21) and
     `SA_ClearObjectFromHangar(m3, gate, −1)` (order 0x1e). The end of 0x1e is the explicit "cleared the exit" event.
* **The requester side can measure it; no arriving-side signal is needed.** For an opposite-direction request the
  holder's exit gate is the requester's entry gate, whose `m3` is already the third argument `gm3`. So
  `SA_Distance(holder.GetObjectID(), gm3)` is the clearance distance whenever that gate is in the player's sector. When
  `gm3 == 0` (out of sector) only a timer applies. Same-direction traffic exits in the other sector and never needs
  the test. Signalling from `__LeaveHyperspaceWarpIn` would mean patching code that contains suspend points
  (bd6ec, bd71e).

## 6. Patch options (Q4)

All changes and checks are in `gate_queue_patches.txt` (`RESULT PASS`). Each option keeps the length, header, build id
and every instruction start outside its window. The whole CODE still decodes to its end, and all 6,818 / 6,815 method
entries stay instruction starts. Patched methods track with 0 stack conflicts and no jump leaves the method; the max
depth stays within `frame`. Every `__EnterHyperspace` resume IP is still reached with the same depth. Offsets: stock
(Mayhem); file offset = CODE + 20.

| Option | Sites (CODE) | Change | Size |
| --- | --- | --- | --- |
| **A1** through cooldown | 90cfb (a6b66) | `pushb 60` → `pushb N` (`05 3c` → `05 NN`; N = 0 gives the "release at launch" rule of [capital-gate-transit.md](../architecture/capital-gate-transit.md) §5.4) | 1 B |
| **A2** warp-in stamp | 90d44 (a6baf) | `pushb 60` → `pushb N` | 1 B |
| **A3** idle wander | bcc6d (d542c) | `pushw 1000; pushw 10000` → e.g. `06 00 fa 06 03 e8` (250 + rand(1000) ms) | 4 B |
| **A4** slot polls | bcc8a, bcd2d (d5449, d54ec) | `pushw 10000; pushw 1000` → e.g. `06 03 e8 06 00 fa` (250 + rand(1000) ms) | 2 × 4 B |
| A5 always grant (optional) | jf operand at 90c6b (a6ad6) | target 90c92 → 90c6f (next instruction) | 4 B |
| A6 exemption out of sector (optional) | jf operand at 90c3b (a6aa6) | target 90c51 → 90c3f: same direction is exempt without `gm3` | 4 B |
| A7 hold instead of wander (optional) | bcc7a..bcc82 (d5439..) | `ldmem 3; push2; native SA_Idle` → `push1; native TI_Delay; push0; drop; not` (`02 82 <TI_Delay> 01 24 64`); old continuation bcc83 kept | 9 B |
| **B** clearance | 90bca..90c9b, 90c9c..90d60 (a6a35.., a6b07..) | both methods rewritten in place, padded with `01` | 377 B used of 407 |
| **C** no rotate | bce4d..bce67 (d560c..) | `push1; jmp bce68` + 21 × `01` (`02 32 00 0b ce 68 …`): the code after the call sees result 1 | 27 B |

**Slot counts are not constants-only.** The through slot is three scalars (`m15`–`m17`), so "k per pair" needs
arrays and a rewrite of both methods. Gates are created once per universe, so existing saves keep the scalars. The
warp-in count can only go down by constants: the loop bounds `pushb 20` at 90a70 and 90d0b, with the `+20` stamp
offsets unchanged. Going up needs `SE_ArrayAlloc(40)` in `Create` (new gates only) and a larger `GetWarpInPos` grid
(5 × 4). Neither helps the wait, which is the through slot **[m]** code, **[i]** effect.

**B, behaviour.** `ReleaseSlot` keeps the stock forward and warp-in release. For the holder it sets
`m16 = {ship, now + 60}` (`mkarr 2`) and `m15 = 0`. `__requestWarpThroughSlot` keeps the stock reset of a held slot.
If `m16` is an array (`SE_GetExprType == 8`), the slot is cleared (`m16 = 0`) when any of these holds:

* the deadline passed;
* the request goes in the same direction as the holder's;
* the holder is gone;
* `gm3 != 0` and `SA_Distance(m16[0].GetObjectID(), gm3) > D`, with D = 1,500,000 units (3 km **[i]**, a parameter).

Otherwise it denies the request. A held slot (`m15 != 0`) denies; a free one grants as in stock. Stock's comparison
reads the array as "cooldown passed" (`le` of an array is true, §1). So a save made with B and loaded without it sees
a free slot, and an int `m16` from a stock save is simply ignored by B **[m]** helper code, **[i]** not run.

**Effect and risk** **[i]** unless marked:

* **A1–A4** shorten the 60 s cooldown and the hand-over latency (mean ≈ 6 s per poll → 0.75 s with the example
  values). They do not shorten the holder's occupancy; the serial rate stays about one capital per order-5 leg plus
  warp. The waiting capital polls more often; each poll is a few KC calls.
* **A5** removes the serialisation: two capitals can fly order 5 to the same gate origin with `CollisionWarn`
  suppressed (`m45 = 0xA0001`, ship-navigation-ai.md §10.5) **[m]**, so expect overlaps or collisions. Not
  recommended.
* **A7** leaves the waiting ship in whatever order state it had; that state was not checked.
* **B** replaces the timer for opposite-direction traffic in the player's sector with the exit-area test. Out of
  sector it keeps the 60 s timer.
* **C** removes the stationary turn. Order 5 then turns at 500 units/s while the ship is more than 18.2° off
  (§10.5), so C only pays off when the mod's capital lead-in already arrives aligned
  ([capital-gate-transit.md](../architecture/capital-gate-transit.md) §2–3).
* **Combinations:** A1–A4 combine with any of the others. B replaces both A1 and A2: its rewritten `ReleaseSlot`
  carries the deadline (60) and the warp-in stamp (`STAMP_B`, 60) as its own `pushb` operands.

**Save compatibility, all options [m]:**

* Build id and offsets are unchanged, so saves load in both directions without a `PTCH` remap.
* No suspend point lies inside the B methods.
* The A3/A4/A7 windows only change values before a call. A7 adds a `TI_Delay` continuation (bcc80) that an
  unpatched object would see as the middle of an instruction. A save taken while a ship waits there, loaded into an
  unpatched object, is the one unsafe case **[i]**.
* C's continuation bce68 is kept with the same depth. A task saved inside `SA_RotateToAngles` resumes correctly
  under C.
* **Do not change the build id.** A different id makes the loader look for a `PTCH` table under the saved id. Mayhem
  has none, so every existing save would fail to load (`0x004a09a4`).

## 7. Save loading in detail **[m]**

`0x004a0880` reads the story name and calls `0x0049d030` again, so the object is reloaded from disk on every save
load. It then compares the saved build id with `VM+0x143c` (`0x004a0973`). If they are equal, nothing is remapped. If
they differ, it sets `VM+0x12f0 |= 8` and looks up `VM+0x1440` with `0x00469a00` (`0x004a0997`); the tables come
from the `PTCH` section via `0x0049dc71`. A missing table aborts the load (`0x004a09a4` → `0x004a1421`). With a table,
each task IP (`task+0x1c`) and each tag-3 return cell is shifted by the delta of the range it falls in. The shifted
IP must follow a native call (`0x82` five bytes before) and a return cell a call (`0x84`..`0x88`); otherwise the task
is flagged (what the flag then does was not traced). Stock carries 29 tables (old build ids
1323793106..1486632380); Mayhem carries none.

## 8. The "modified" flag and the file location (Q5) **[m]** unless marked

* **Engine flag:** `0x0049d030` sets `VM+0x12f0 |= 0x10` for `x3story` (`0x0049d199`) and copies the file with NUL
  bytes replaced by spaces. It calls `0x004cae90`: verify `addon\x3files.xml`'s `<data>` block, then verify that
  copy against the `L\x3story.obj` entry. Only success clears the bit (`0x0049d221`). The single reader of bit 0x10 is
  `SE_SignGameStats` (SE 151, `0x004af539`), used only by `150::ExportStatistics`. Bit 8 marks a save
  from another build.
* **KC flag:** `150::CheckSignatures` (from `150::InitClient` and `202::Patch32`) runs a KC function at CODE 0x2a46,
  only if not already modified. That function reads `addon/x3files.xml`, verifies its `<data>` signature, then for
  each of the 58 (file, signature) pairs runs `SE_ReadFile(name, 0, 32)` and `SE_VerifyData` with the 276-byte RSA
  key built at CODE 0x25a5. On failure it calls `150::SetModified("x3files verification failed")` → `__SetModified`:
  `m14 = 1`, `X2_SetModified(1)`. It appends to `modified.txt` only when that file already exists, and none exists on
  this install.
* **Measured digests** (`story_signature.txt`). `addon/x3files.xml` verifies (`<data>` MD5 `9dcbe362…`). The signed
  digest for `L\x3story.obj` is `67841644…`, which equals MD5 of **stock `addon/04.cat`'s object with NUL → space**.
  None of the other 14 catalogue copies matches, Mayhem's included.

  **Consequences [i]:** with Mayhem's override this install already fails the check, i.e. it is "modified". A patched
  stock object makes a stock install "modified". Nothing refuses to load.
* **Which file the game opens** (`0x004e7590`, `0x004e8780`):
  1. The path is `L\x3story` (`0x0057c008`+0x20 = `L\%s`, set at `0x004ecab2`) with extension `obj`.
  2. The loose search lists `<dir>\*.*` under the working directory. It accepts `x3story.obj` and prefers the
     language variant `x3story-L<lang>.obj`, case-insensitively.
  3. If a loose match was found, the catalogue scan is skipped (`0x004e7ce1`/`0x004e7ce6`).
  4. Otherwise catalogues are scanned from the last registered one down (`0x004e7d0c`), which is how `addon/07.cat`
     wins today.

  The override therefore belongs at **`<game dir>\L\x3story.obj`**. There is no `addon` variant of this path, unlike
  `addon\x3files.xml`, `addon\types\…` and the KC's `addon/scripts` and `addon/director`. `X3TC.exe` in the same
  folder may pick the file up too **[i]**.
* **Mayhem** ships its object inside `addon/07.cat`, not loose. The patcher reads the newest catalogue copy, checks its
  SHA-256 against the two known builds, applies that build's sites and writes the result loose.

## 9. Inferred or open (Q6)

* The loose-file location is a static reading of `0x004e7590`, not observed. One launch with the file in place (and
  none in `addon\L`) settles it.
* `m3 != 0` ⟺ the object has an engine instance in the player's sector. This is consistent with
  `__EnterHyperspace`'s out-of-sector branch and `__LeaveHyperspace`'s `Activate()`, but was not traced.
* Units: age = game seconds (§10.2), TI waits = game milliseconds (SETA scaling not traced), 500 units/m.
* The runtime "modified" state of this install, and that `SE_ReadFile(…, 32)` hashes the NUL→space form (the
  matching digest implies it).
* All traffic effects of A–C, the collision risk of A5, the value of D, and the waiting ship's order state under A7.
* B was assembled and checked structurally (stack depths, jumps, offsets), not executed. A fixture that runs the KC
  methods does not exist. The first flight with B should log the grant/deny decisions (a Run row at the SA dispatcher
  for `SA_Distance` calls from the gate, or KC-side counters).

```sh
python3 -I verification/results/ship-navigation-ai/gate-queue/gate_queue_listing.py > …/gate_queue_listing.txt
python3 -I verification/results/ship-navigation-ai/gate-queue/gate_queue_patches.py > …/gate_queue_patches.txt   # RESULT PASS
python3 -I verification/results/ship-navigation-ai/gate-queue/story_signature.py > …/story_signature.txt
python3 -I verification/results/ship-navigation-ai/gate-queue/exe_checks.py > …/exe_checks.txt                   # RESULT PASS (64 checks)
```
