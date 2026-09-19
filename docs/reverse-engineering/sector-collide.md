# `0x0045d250`: the sector collision pass, its O(n²) pair loop, and two broadphase hooks

2026-09-18. Static study of X3AP.exe, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` (the bottle
copy, re-hashed this session); nothing was launched. Structure and instruction
text come from Ghidra headless (project `/tmp/x3-ghidra-research`, `X3Render`,
`-readOnly -noanalysis`) on that image; every byte string quoted below was
re-read from the file on disk through the PE section map (`.text` RVA `0x1000`
→ raw `0x400`, image base `0x400000`). Raw decompiler output stayed local and
untracked. Inferences are marked.

**Question.** run 42 stand A (`/tmp/x3-bottleX3-run129`, corvette save, run125
area) measures `loop_phases collide` at 26.1–26.3 ms/frame, 96 % of a 27–29 ms
`pre_render`, flat (p50 span 0.25 ms) for ≈ 66 s at ≈ 450 draws, against 5–9 ms
elsewhere. What in `0x0045d250` scales, and can it be cut without changing game
logic?

**Answer.** The routine contains an explicit, unguarded all-pairs loop over the
sector's class buckets: every non-excluded object is tested against every other
one, every frame, with no spatial structure, no motion test and no cross-frame
state. For all class combinations except a short list, the per-pair test is a
full `FILD×3 / FMUL / FADD / FSQRT / CVTTSD2SI` distance with **no integer
bounding-box early-out** — ~58 x86 instructions, 18 of them x87, two calls, per
pair. Two hook sites (`0x0045d58e`, `0x0045cc7c`) can insert exactly that
missing early-out; on the rejected pairs the arithmetic is provably equivalent
(§6), so the saving is pure. How large it is depends on a pair count that
cannot be derived statically; §7 specifies the read-only census that measures
it, in the pattern of `--cull-census`.

## 1. Signature and place in the frame

`0x0045d250`–`0x0045e0b7`, `0xe68` bytes, one exit `ret 4` at `0x0045e0b5`
(`c2 04 00`), prologue `55 8b ec 83 e4 f0 83 ec 74` (EBP frame, 16-byte align,
`0x74` locals) plus `push ebx/esi/edi`. `void __stdcall sector_collide(sector*)`.
Sole caller `0x0043a38e` (`56 e8 bc 2e 02 00`), the `sector_collide` stamp site
of [main-loop-input-region.md](main-loop-input-region.md) §4 and the `collide`
interval of the `loop_phases` telemetry.

Callees that matter here: `0x0045cab0` (swept query, `0x4a7` B), `0x0045e130`
(collision response, 3445 B), `0x0045cf60` (113 B), `0x0045c780` (806 B,
narrow-phase gate), `0x0048ac80` (103 B), `0x0048a7f0` (156 B), `0x0048a890`
(910 B), `0x0044e450` (308 B, ship proximity), `0x0044d4c0` (gate/dock test),
`0x0045d1b0` (147 B), `0x0045d170` (53 B, post-callback liveness re-check),
`0x0049f4c0` (script notification: `"CanWarp"`, `"CanLand"`,
`"NotifyPlanetCollision"`, `"MakeDamage"`, `"KilledBy"`, `"CollisionWarn"`).

Object layout used throughout (consistent with
[sector-post-pass.md](sector-post-pass.md), which establishes that **the class
word `[obj+0x48]` is the bucket index**): `[obj+0x00]` next in bucket list,
`[obj+0x04]` back-pointer slot, `[obj+0x40]`/`[obj+0x44]` flag words,
`[obj+0x48]` class, `[obj+0x4a]` subtype (index into the `0xdb8`-byte record
table at `*0x00606fb8`), `[obj+0x50]` per-class payload, `[obj+0x54]` parent
sector, `[obj+0x70]` physics block (`+0x30/+0x34/+0x38` = integer world x/y/z,
`+0x3c` w, `+0xb0..+0xbc` saved copy, `+0x180..+0x190` contact scratch),
`[obj+0xa4]` collision radius in the same integer units. Sector bucket table
`[sector+0x50]`, 32 entries of stride `0xc`; each list terminates on a sentinel
whose first dword is 0.

## 2. The four loops

```
L0  0045d275..0045d2aa   for b in 0..31: for o in bucket[b]:
                            [o+0x40] &= 0xff9fcfff; [o+0x44] &= 0xfff7ffbc
                            if class(o)==7: [[o+0x50]+0x1e0] = 0
L1  0045d2b5..0045d313   for o in bucket[0]:                       (swept pass)
                            if !([o+0x40] & 0x8200404) and type-table[subtype]+0x58 & 2:
                               [o+0x40] |= 0x200000
                               h = 0045cab0(o); if h: 0045e130(o,h)
L2  0045d320..0045dfe8   for c1 in 0..31 (skip 3,19,21,27):        (pair pass)
                            for A in bucket[c1]:
                               for c2 in c1..31 (skip 3,19,21,27; skip c1==c2==0;
                                                 skip c1==c2==20; if 4 in {c1,c2}
                                                 skip other in {0,4,10,20}):
                                  for B in (c2==c1 ? A.next : bucket[c2]):
                                     pair_test(A,B)
L3  0045e000..0045e0af   for b in 0..31: for o in bucket[b]:
                            [[o+0x70]+0x190] = 0; class-17 flag fixup;
                            if ([o+0x40] & 0x1000) and ([o+0x40] & 0x2000):
                               0049f4c0(..., "CollisionWarn")
                            [o+0x40] &= 0xfffffffb
```

L0, L1 and L3 are O(N) in sector objects. **L2 is O(N²)**: pairs are visited
once each (same-class inner list starts at `A.next`, `0x0045d3e6 cmp ax,si /
0x0045d3eb mov ecx,[ebx]`), so the pair count is
`Σ_{c1≤c2, allowed} n_c1·n_c2 ≈ N²/2` with N the population of the allowed
classes. The class filters were read out of the jump tables themselves, not
only from the decompiler:

| Table | Index | Contents |
| --- | --- | --- |
| `0x0045e0b8`/`0x0045e0c0` | outer class − 3, range 0..0x18 | classes **3, 19, 21, 27** → `0x0045dfd9` (skip class); all others → `0x0045d339` |
| `0x0045e0dc`/`0x0045e0ec` | inner class, range 0..0x1b | 3, 19, 21, 27 → `0x0045dfad` (skip); 0 → `0x0045d3b1` (skip iff outer is 0); 20 → `0x0045d3b6` (skip iff outer is 20); rest → `0x0045d3c0` |
| `0x0045e108`/`0x0045e110` | `c1+c2−4`, only when 4 ∈ {c1,c2} | other class ∈ **{0, 4, 10, 20}** → `0x0045dfad` |

Classes ≥ 28 and < 3 fall to the default (processed). There is no grid, no
sort, no sleeping/static list, and no per-frame candidate cache anywhere in the
routine: L2's only state is the loop registers, and the flag words it consults
are the ones L0 cleared a few microseconds earlier in the same call.

## 3. Per-pair work, and where the early-outs are missing

Inner-loop head `0x0045d414` (`f7 46 40 04 04 20 08`). Per candidate B, in
order: `[B+0x40] & 0x8200404` reject; `[B+0x44] & 0x20000` reject; optional
`[B+0x44] & 0x400` reject when the sector flag `[sector+0x148] & 4` is set;
parent/child reject via `0x2000000` + `[+0x58]`; `[esp+0x24] = [A+0xa4] +
[B+0xa4]` (radius sum); then a dispatch on the two class words
(`0x0045d48e`, `0x0045d49b`):

| Pair kind | Path | Early-out before the square root |
| --- | --- | --- |
| either class 0 | `0x0045dcdf`/`0x0045dcf9` | **yes** — `abs(dx), abs(dy), abs(dz) ≤ r_sum` on integers (`0x0045dd61`–`0x0045ddbf`, compares at `0x0045dd83`/`0x0045dd9f`/`0x0045ddbb`), then a *squared* x87 compare with no `FSQRT` (`0x0045ddc8`–`0x0045de0e`), then `0048a7f0`/`0048a890` |
| either class 4 (planet) | `0x0045d913`/`0x0045d929` | n/a — one distance, then `"NotifyPlanetCollision"`/`"MakeDamage"` |
| class 20 with subtype `0x5c` | `0x0045d1b0` | n/a |
| either class **7** | `0x0045d518`..`0x0045d58c` | **yes** — `abs(dx), abs(dy), abs(dz) ≤ (r_sum·3 + 250000)` on integers, reject to `0x0045df90` |
| **everything else** | `0x0045d58e` | **none** |

The last row is the whole finding. `0x0045d58e` runs unconditionally for every
pair of classes ∉ {0, 4, 7, 20/`0x5c`} — station × station, station × asteroid,
asteroid × asteroid, wreck, container, class 5/6 — and for every class-7 pair
that survives the 250000-unit box:

```
0045d58e  8b 4b 70 / 8b 51 30 ...   dx,dy,dz = A.pos - B.pos        (3 loads, 3 subs)
0045d5a4  db 44 24 28 (×3)          FILD dx, dy, dz
0045d5c4..0045d5d5                  FMULP/FADDP ×3 → dx²+dy²+dz²
0045d5d7  d9 1c 24                  FSTP float [esp]                 (float32 round!)
0045d5da  e8 -> 00412440            PUSH EBP/MOV/AND/FLD/FSQRT/MOV/POP/RET
0045d5e2  e8 -> 0052b5d0            CMP/JZ/PUSH EBP/MOV/SUB/AND/FSTP qword/CVTTSD2SI/LEAVE/RET
0045d5ed  b8 8f 02 01 00 / f7 ea    R = (r_sum·0x1028f + 0x8000) >> 16   ≈ r_sum × 1.0100
0045d604  3b c8 / 0f 8f c0 00 00 00 if (dist > R) goto 0045d6cc
```

40 instructions in the function plus 8 and 10 in the two helpers = **58 x86
instructions, 18 of them x87/convert, two call/ret pairs and one 32×32→64
`imul`, per pair**, before anything is known about the pair. Compare the
class-7 gate it is missing: three loads, three subtractions, three compares.

`0x00412440` is `FLD [esp+4]; FSQRT` (leaves the result on the x87 stack);
`0x0052b5d0` pops it with `FSTP qword [esp]; CVTTSD2SI` when `*0x006619ec != 0`
(SSE2 path) — so the distance is **truncated toward zero** and the sum of
squares was rounded to float32 at `0x0045d5d7`. Both matter for §6's margin.

What happens after the compare decides the pair is far apart: `0x0045d6cc`
loads both class words and, if **neither is 7**, jumps straight to
`0x0045df90`, the loop-continue label — nothing is written, no flag, no call.
For pairs where one side is class 7 the distance is reused by `0x0044d4c0`
(warp/land gating, `"CanWarp"`/`"CanLand"`) and `0x0044e450` (proximity), which
is why those keep their own gate and must not be short-circuited blindly.

## 4. `0x0045cab0`, the swept query (L1)

Called once per bucket-0 object. It saves the object's position, advances it by
one frame's motion (`0x0040e780`), then scans **all 32 buckets of its own
sector** (`[[obj+0x54]+0x50]`, loop head `0x0045cc14`, continue label
`0x0045ce07`) and applies, per candidate, the same pattern as §3 with no box
test: `0x0045cc7c`..`0x0045ccf2`, ≈ 30 instructions plus the same two helper
calls, rejecting when `dist > [esp+0x20] + [cand+0xa4]` where `[esp+0x20]` is
the swept half-length `(type[0x54]·type[0x50]/1000)/2` for class 0 and 0 for
everything else (**inference**: speed × lifetime, i.e. a projectile sweep).
Survivors go into a 32-entry array (`local_110`, capped at `0x1f`, `qsort` with
comparator `0x004ab160`) and only then into the narrow phase. So L1 is
O(n_bucket0 × N) with a square root per element and a hard cap only on the
*survivors*, not on the scan.

## 5. Why 26 ms, flat, for 66 s

> **Superseded in part by §11 (2026-09-19).** The measured `collide_census` of
> run134 falsifies this section's leading explanation. The §3 generic pair path
> runs ≈ 205 times per frame, not 10⁵, and its count does not move while the
> bracket swings 100×. The claim below that "the cost is set by the sector's
> object population and nothing else" is wrong: it is set by how many pairs pass
> the engine's own `dist ≤ R` compare and enter the narrow phase
> (`0x0048ac80` → `0x0048a890` → `0x0047f1b0` → `0x004e2530`), which depends on
> proximity, not population. The instruction-bracket arithmetic below is left
> intact as the record of what was inferred before the counters existed.

Established: the cost of `0x0045d250` is set by the sector's object population
and nothing else in the routine — no accumulator, no catch-up, no retry, no
cache, no dependence on frame rate, dt, draws or the renderer. That is exactly
the signature run129 shows: a plateau with p50 span 0.25 ms held for 66 s while
draws sit at ≈ 450, and 5–9 ms in the emptier stands (stand b: ~70 draws). A
per-event or per-retry explanation (the `0x0045b720` media-cue pattern) is
ruled out for this site: there is nothing in `0x0045d250` that can be in a
retry state.

Which term dominates is **not** established statically. The bracket, with FEX
x86 throughput taken as 300–800 M instructions/s (**assumption**, not measured
here):

- 26 ms ⇒ 8–21 M x86 instructions per frame in the pass.
- At ~58 instructions per §3 square-root pair: **135 k–360 k** such pairs, i.e.
  N ≈ 520–850 objects in the non-{0,4,7,20} classes.
- At ~15 instructions per pair rejected on flags or on the class-7 box:
  0.55–1.4 M pairs, i.e. N ≈ 1050–1700 objects overall.
- At ~48 instructions per §4 swept candidate: 170 k–440 k candidate visits,
  e.g. 340–880 bucket-0 objects against a 500-object sector.

All three are plausible for a populated X3AP sector (asteroid field plus a
station complex plus traffic), and they are not mutually exclusive. The leading
explanation is the §3 generic pair path, because it is the only one with no
early-out at all and because its cost is independent of whether anything moves:
a pair of immobile rocks 40 km apart is square-rooted every frame forever.
The "stuck state" hypotheses were checked and none is supported: objects
awaiting deletion carry `0x08000000`, which is inside the `0x8200404` skip
mask; the swept-query candidate array is capped at 32; the lists are
null-sentinel terminated and are rebuilt by nothing here.

Unit scale (**inference**, from
[chase-view-restore-and-hud-anchor.md](../architecture/chase-view-restore-and-hud-anchor.md):
≈ 505 units per metre): the class-7 slack `r_sum·3 + 250000` is ≈ 500 m, a
sensible proximity band, and the class-0 box is exactly the radius sum. If the
scale were instead ~5 units/m the class-7 box would be ~50 km and would reject
almost nothing, which would move the leading explanation to the class-7 pairs.
The census of §7 distinguishes the two without settling the unit question.

## 6. Proposed patch: the missing integer box, at two sites

Pattern: the media-cue negative cache ([media-cue-playback.md](media-cue-playback.md))
— one narrow, provably-equivalent stub on a hot redundant computation, not a
skip of engine work.

**P1 — pair loop.** Site `0x0045d58e`, displaced span `8b 4b 70 8b 51 30`
(6 bytes = two whole `mov`s, `jmp rel32` + one `nop`). Verified from the image:
`0x0045d58e` has exactly one inbound reference (`0x0045d516` conditional jump)
plus fallthrough from `0x0045d588`, and `0x0045d58f`..`0x0045d593` have **zero**
references image-wide. Stub:

```
if class(EBX)==7 or class(ESI)==7: fall through           ; distance is reused at 0045d6cc
r = [EBX+0xa4] + [ESI+0xa4]
T = r + (r >> 5) + 64                                     ; > r·1.0100 + rounding margin
if |Ax-Bx| > T or |Ay-By| > T or |Az-Bz| > T: jmp 0045df90
fall through (re-execute the two displaced movs, jmp 0045d594)
```

*Equivalence.* `dist` as the engine computes it is
`trunc(sqrt(fl32(dx²+dy²+dz²)))` ≥ `|dx|·(1−6e−8) − 1`; the engine rejects when
`dist > R = (r·0x1028f + 0x8000) >> 16 < r·1.01`. `T ≥ r·1.03125 + 64` exceeds
`R` by more than the float32 and truncation error for any `|dx| ≤ 6.4e8` units
(≈ 1.3 M m, larger than a sector). So `|dx| > T ⇒ dist > R`: the stub only
rejects pairs the engine's own compare would have sent to `0x0045d6cc`, and for
non-class-7 pairs `0x0045d6cc` immediately jumps to `0x0045df90` writing
nothing. The reject target is the same label the class-7 box already uses
(`0x0045d553`/`0x0045d570`/`0x0045d588` → `0x0045df90`), and `0x0045df90` reads
only `[esp+0x3c]`, so no register contract is created that the engine does not
already rely on. The engine's own class-0 path (§3, `0x0045ddc8`) shows it
already knows the squared-compare idiom that avoids the `FSQRT` entirely; P1
deliberately does **not** replace the surviving pairs' arithmetic, so no
borderline pair changes side.

*State to preserve.* EBX and ESI (the pair), EBP, the whole `esp`-relative
frame. EAX/ECX/EDX are dead at the site (written by the displaced `mov`s and by
`0x0045d594`); EDI is dead (first touch on every path out of the span is the
write at `0x0045d6d0`); flags are dead (last writer before is the `cmp` feeding
`0x0045d516`/`0x0045d588`, next writer `0x0045d597 sub`, next reader
`0x0045d606`) — a `pushfd` is therefore optional, and a stub that only clobbers
EAX/ECX/EDX/EDI/flags needs no save at all. No x87 use: the stub must not touch
the FPU stack, which at this point holds nothing (the engine's own sequence
starts at `0x0045d5a4`), and must not use SSE registers the ABI treats as
volatile without restoring them. Reentrancy: the stub calls nothing, reads only
the two objects, keeps no state, so the script callbacks that `0x0045d250` can
re-enter through (`0x0049f4c0`, guarded by `0x0045d170` liveness re-checks)
cannot observe it.

*Expected saving.* Per rejected pair, ~58 instructions with 18 x87 ops and two
calls become ~18–26 integer instructions with no call: a 2.5–4× factor on that
pair (**estimate**; the x87/FEX component is not measured). If a fraction `f`
of the 26 ms is spent on §3 pairs that the box rejects, the saving is
`26·f·(1 − 1/3)` ms — 8.7 ms at `f = 0.5`, 12 ms at `f = 0.7`. Pairs that pass
the box pay ~20 extra instructions, i.e. a few per cent of their cost.

*Risk.* Missed collisions only if the margin argument is wrong; the failure
mode is quiet (objects interpenetrating) rather than a crash, which is why the
fixture below enumerates the boundary rather than sampling it. No script
callback is skipped: the callbacks live behind `0x0045c780`/`0x0048ac80` and
`0x0045d6cc`, both downstream of the compare the stub replicates. Class-7 pairs
are untouched, so `"CanWarp"`, `"CanLand"` and the proximity path keep their
current behaviour exactly.

**P2 — swept query.** Site `0x0045cc7c`, displaced span `8b 47 70 8b 48 30`
(6 bytes, two whole `mov`s). Verified: two inbound references to `0x0045cc7c`
(`0x0045cc5e`, `0x0045cc70`), zero to `0x0045cc7d`..`0x0045cc81`. Threshold
`T = [esp+0x20] + [EDI+0xa4]` (the stub reads `[esp+0x20]` through its own
fixed push displacement), margin as P1, reject to `0x0045ce07`, which needs EDI
= candidate. Same equivalence argument (`0x0045ccf2 jg 0x0045ce07` is the
compare being anticipated), same preservation set plus EBX and EDI. Worth
installing only if the census shows L1 is a material share.

**Not proposed.** A cross-frame pair memo: the pair count is ~10⁵–10⁶, so the
hash lookup would cost what the box test costs and would need velocity bounds
to stay conservative. A real sweep-and-prune broadphase would cut the pair
count itself (O(N log N)), but it has to be maintained across object
insertion/removal inside `0x00452ad0`, `0x0045b660` and the script VM — a much
larger hook surface; it is the second-stage option if P1 proves the pair count
but not enough of the saving.

## 7. Census that must come first

`--collide-census`, read-only `engine_patch` trampolines, fixed ring, in the
shape of `--cull-census` (`src/proxy/cull_census.{h,cpp}`,
`verification/probe/verify_cull_census_sites.py`, `tools/analysis/cull_census.py`).
Five counter sites, all with a whole-instruction displaced span and all verified
against the image this session:

| # | Address | Displaced bytes | Counts | Inbound refs |
| --- | --- | --- | --- | --- |
| 0 | `0x0045d280` | `81 60 40 ff cf 9f ff` (7, one insn) | objects per class: `ECX/0xc` is the bucket index, `EAX` the object | 1 (`0x0045d2a2`) |
| 1 | `0x0045d2b5` | `8b 46 40 …` | L1 swept queries issued | 1 (`0x0045d313`) |
| 2 | `0x0045cc7c` | `8b 47 70 8b 48 30` (6) | L1 candidates reaching the square root | 2 (`0x0045cc5e`, `0x0045cc70`) |
| 3 | `0x0045d414` | `f7 46 40 04 04 20 08` (7, one insn) | L2 candidate pairs examined | 1 (`0x0045d40a`) |
| 4 | `0x0045d58e` | `8b 4b 70 8b 51 30` (6) | L2 pairs reaching the square root | 1 (`0x0045d516`) |

One line per capture frame: `collide_census frame= n_by_class=[32] pairs=
sqrt_pairs= swept_calls= swept_cands= collide_us=` (the last from the existing
`loop_phases` interval). It answers, without launching anything new beyond the
run the user already performs: whether `pairs ≈ Σ n_c1·n_c2` (if not, the list
walk itself is pathological); what share of 26 ms is `sqrt_pairs × ~58
instructions`; whether L1 or L2 owns the plateau; and the per-pair nanosecond
cost under FEX, which is the one number §6's saving estimate lacks. Sites 3 and
4 fire 10⁵–10⁶ times per frame, so each stub must be a single `inc` on a
fixed slot with no QPC and no tracker — the per-frame self-cost budget is what
decides whether the census can run at all, and it should be gated to every Nth
frame from the frame counter the existing telemetry already keeps.

## 8. Fixture and verifier the patch would need

- `verify_collide_sites.py`, in the shape of `verify_cull_census_sites.py`:
  decode `0x0045cab0`..`0x0045cf5x` and `0x0045d250`..`0x0045e0b7` from the
  installed EXE, assert the displaced spans are whole instructions, that no
  reference lands inside them, the constants `0x1028f`, `0x30000`, `0x3d090`,
  `0x8200404`, `0x20000`, the reject targets `0x0045df90`/`0x0045ce07`/
  `0x0045d6cc`, the single `ret 4`, and the three jump tables of §2.
- A host CPU fixture over the arithmetic only: for ~10⁷ randomized
  `(dx,dy,dz,r)` including the boundary band `|d| ∈ [R−2, T+2]`, magnitudes up
  to ±2³⁰ and `r` up to 10⁶, assert `box_reject(d,r) ⇒ engine_reject(d,r)`
  where `engine_reject` reproduces `trunc(sqrt(fl32(Σd²))) > (r·0x1028f +
  0x8000) >> 16` in float32/double exactly as §3 decodes it. A single
  counter-example fails the build.
- No Wine, no launch, for either.

## 9. Not established

- The pair count and the class histogram at the episode site: §7 exists because
  neither can be derived from the image.
- The meaning of the class numbers beyond what
  [sector-post-pass.md](sector-post-pass.md) fixed (1 = sector/container, 5/6/7
  = scene-node objects, 0x12, 0x14); which class holds asteroids is unknown,
  and it decides whether the excluded set {3, 19, 21, 27} already removes the
  populous one.
- The engine unit scale (§5) is an inference carried over from the chase-camera
  note; it changes which of the two loops leads, not whether the patch is valid.
- FEX per-instruction throughput, hence every ms figure in §5 and §6 is a
  bracket, not a measurement.
- `0x0045e130` (3445 B) and `0x0045c780` (806 B) were not decompiled; they are
  downstream of every early-out discussed and are untouched by P1/P2.
- Whether `[sector+0x148] & 4` (the `[esp+0x13]` predicate) restricts the pass
  to the player's sector in practice — the same open question as
  [main-loop-input-region.md](main-loop-input-region.md) §"Not established".

## 10. Implemented: `--collide-box-cull` (2026-09-18)

`X3M_COLLIDE_BOX_CULL=1` (launcher `--collide-box-cull`, default off), `src/proxy/collide_box_cull.{h,cpp}` and
`collide_box_cull_core.h`: two `engine_patch` trampolines, P1 `0x0045d58e` and P2 `0x0045cc7c`, six displaced bytes
each (two whole `mov`s), stubs of 139 and 117 bytes (127/105 without counters). It replaces the separate census of §7:
the stubs count pairs entering each site and pairs the box rejected.

**Corrections to §6 found while verifying against the image.**

- *The engine does not reject a saturated distance.* `0x0052b5d0` ends in `CVTTSD2SI`, which returns `0x80000000` when
  the value is ≥ 2³¹; `cmp ecx,eax / jg` then reads INT_MIN as "not far" and the pair goes to the narrow phase. That
  needs `sqrt(Σd²) ≥ 2³¹`, i.e. some `|d| ≥ 1.24e9` units (no real sector), but a box that rejected such a pair would
  differ from the engine. The stub therefore rejects only when **every** `|d| < 2³⁰` (then `dist < 1.86e9 < 2³¹`);
  `|d| = INT_MIN` (whose negation is itself) is caught by the same unsigned compare. The fixture exercises both.
- *Margin.* With `m = max|d| < 2³⁰`: `dist ≥ m·(1 − 2⁻²⁵) − 1` (float32 unit roundoff 2⁻²⁴, halved by the square root,
  plus truncation; the x87 intermediates, 64-bit under `FEX_X87REDUCEDPRECISION`, add < 2⁻⁵¹ relative), so
  `dist > m − 34`. P1: `T = r + (r >> 5) + 64 ≥ R + 35` because `R = (r·0x1028f + 0x8000) >> 16 ≤ 1.01·r + 0.5`.
  P2: the engine compares against the raw sum `R = [cand+0xa4] + [esp+0x20]` (no 1.01 factor), `T = R + 64`.
  `m > T ⇒ dist > R` in both. A negative radius sum (P1) or a signed overflow of any threshold add (`jo`) leaves the
  engine path; when `R` would wrap (`r ≥ 2.126e9`), `T` has already overflowed (`r ≥ 2.082e9`).
- *Jump tables* (order as stored): `0x0045e0b8 = [0x45dfd9, 0x45d339]`, `0x0045e0dc = [0x45d3b1, 0x45dfad, 0x45d3b6,
  0x45d3c0]`, `0x0045e108 = [0x45dfad, 0x45d3e6]`.

**Radius source.** P1 reads `[esp+0x24]`, the sum `[A+0xa4] + [B+0xa4]` the engine stored at `0x0045d48a` and scales at
`0x0045d5ed` (nothing writes the slot in between). P2 reads `[EDI+0xa4]` and `[esp+0x20]` exactly as `0x0045cce6` does.
Differences are the engine's own wrapped 32-bit subtractions of `[[obj+0x70]+0x30/0x34/0x38]`; `|d|` uses the
class-7 box idiom (negate when negative).

**State contract, verified on the decoded routines** (CFG with the three jump tables, forward search for a read not
preceded by a write):

| | P1 `0x0045d58e` | P2 `0x0045cc7c` |
| --- | --- | --- |
| Scratch registers | EAX, ECX, EDX, EDI: no read reachable from the site or from `0x0045df90` before a write (EDI: `0x0045d6d0` on both reject paths, reloaded on every survivor path) | EAX, ECX, EDX, ESI: same from the site and from `0x0045ce07` (ESI is written at `0x0045cc82`) |
| Preserved | EBX, ESI (the pair), EBP, ESP (one balanced `push`/`pop`) | EBX (swept object), EDI (candidate), EBP, ESP |
| Reject path mirrors | `mov edi,7` (as `0x0045d6d0`) | `mov esi,[ebx+0x70]` (as `0x0045cc82`) |
| EFLAGS | dead: next writer `0x0045d597 sub`, no reader before; `cmp` at `0x0045df94` on the reject path | dead: `0x0045cc85 sub`; `cmp` at `0x0045ce09` |
| x87 | untouched; the span precedes the first push (`FILD` at `0x0045d5a4`); the engine's own sequence is net zero at its `jg`, so both reject paths leave the same depth | same (`FILD` at `0x0045cc9b`) |
| Locals the engine's reject path writes and the stub's does not | `[esp+0x1c]` (dist), `[esp+0x28]` (dz): not live at `0x0045df90` (`[esp+0x1c]` is read only behind `0x0045d6e4`, class 7) | `[esp+0x30/0x34/0x38]`, `[esp+0x60..0x6c]` (the latter written as `[esp+0x64..0x70]` after `push ecx`): read only on the survivor path of the same visit |
| Inbound references | `0x0045d516` + fall-through; none into `+1..+5` | `0x0045cc5e`, `0x0045cc70`; none into `+1..+5` |

**Gameplay risk.** A pair rejected by the box that the engine would have collided is impossible by construction: the
stub rejects a subset of the pairs for which the engine's own `dist > R` compare is true (margin above; fixture on the
engine's bytes; 10.7 M model pairs on the host), and for those the engine goes `0x0045d6cc → 0x0045df90` (neither class
is 7, which the stub checks first) or `→ 0x0045ce07`, writing nothing that is read again. No script callback is
skipped: `"CanWarp"`, `"CanLand"`, `"NotifyPlanetCollision"`, `"MakeDamage"`, `"KilledBy"` sit behind the narrow phase
or the class-7/class-4 paths, none of which a rejected pair reaches in the unpatched engine either; `"CollisionWarn"`
is in L3. The stub keeps no state but the counters and calls nothing, so re-entry through `0x0049f4c0` cannot observe it.

**Install.** Backend-load path inside the `engine_patch` window, after the exact-executable check, seven byte windows
(site 26/35 bytes, compare 37/18, P1 reject 24, continue 11/9, at offsets relative to each site), the two helper
bodies and the four `call` targets; module pinned; P2 failure rolls P1 back; `late_claim` after the first Present;
LastError preserved. `verification/probe/verify_collide_sites.py` (29 checks) also asserts the claim windows are
disjoint from every other site the proxy patches (119 collected from `src/proxy` and the chase verifiers).

**Fixture.** `verification/probe/collide_box_cull_fixture.cpp`: the fixture cannot execute the engine's loop body from
the EXE in-process, so it carries layout-preserving synthetic copies of both pair tests: every byte from the site to
the `jg`, the P1 reject block and both continue labels is the engine's, at the engine's relative offsets (so the rel32
of `jg +0xc0`, `jne +0x8ac`, `jg +0x10f` are exact); only the four `call` rel32s differ, targeting byte-exact replicas
of `0x00412440` and of the SSE2 path of `0x0052b5d0`. 445,882 P1 pairs (12 class combinations including 7 on either
side, 11 radius sums, magnitudes on `R±1`, `T±1`, `T+40`, 2³⁰±1, 2³¹−1, INT_MIN, random offsets and 100 k random
positions) and 146,064 P2 candidates run through the unpatched and the patched bytes: same exit
(continue/survivor/class-7), same EBX/ESI/EBP/ESP (EDI wherever the engine defines it), same x87 depth and values,
same locals except the dead scratch above; counters equal to the host model pair by pair. Ledger:
[collide-box-cull.md](../verification/collide-box-cull.md).

**Counters.** `inc dword [abs32]` on entry and on reject, four slots, read and zeroed once per Present:
`collide_census frame= frames=300 {p1_pairs,p1_rejected,p2_cands,p2_rejected}_{p50,max,sum}` per 300-frame window and
`collide_census_frame` on F8 frames. Self-cost: below the fixture's resolution (armed with counters 67.0 ns/pair,
without 67.1, harness included); two memory increments per rejected pair, one otherwise.

### 10.1 Reviews (2026-09-19)

Opus review and Fable second review: no correctness defect; spans, liveness, image-wide absence of interior branch
targets (whole-`.text` objdump scan by the reviewer; `verify_collide_sites.py` itself scans only the decoded
function) and the subset proof confirmed independently. Margins: P1 `T - R >= 0.021 r + 63`, P2 `T = R + 64`,
against an engine conversion error under 33 for `m < 2^30` (double intermediates, which is also what
`FEX_X87REDUCEDPRECISION=1` gives). The second conversion path of `0x0052b5d0` at `0x0052b606` (`fistp qword` plus
a truncation correction, taken when `*0x006619ec == 0`) equals truncation for distances below 2^31, so under the
2^30 cap both paths agree; it is not pinned or modelled. No engine write to an object, stamp or list precedes
either site's reject. Accepted open points: under 24-bit x87 precision control (the game's software-vertex-processing
CreateDevice branch omits `FPU_PRESERVE`) the engine error grows to about `2.5 * 2^-24 * m`; P1 stays safe, P2's
fixed margin would fail only for a radius sum above about 4.2e8 units (84,000 km), which no sector object has — if
ever needed, test `m - (m >> 20) > T` at P2 or refuse at install on a non-53/64-bit control word. The
P2-claim-fails-after-P1 rollback is not executed by a fixture; `StubWriter::jcc` has no bound on `fix_[16]` (8 used);
the counters' plain `inc` against `InterlockedExchange` at Present is race-free only if the loop and Present share
a thread (diagnostics only). Pairs the box keeps cost 4-5 % more; run 43 A decides.

## 11. Run 43 A: the cost is not the pair reject

2026-09-19. Same image and method as the rest of this note (nothing launched);
instruction text below re-read from the bottle EXE through the PE section map
(`.text` VA `0x00401000`, raw `0x400`, `0x130630` bytes) with `objdump -d
--x86-asm-syntax=intel` over the file in place. The run-log numbers are from
`grep`/`awk` over the session logs, never read whole.

### 11.1 How the 26 ms was attributed, and why no address histogram exists

The run129 figure was a **bracket, not a leaf-EIP histogram**.
`src/proxy/loop_phase_sites.h` defines `loop_phase_sector_collide` at
`0x0043a38e` (`56 e8 bc 2e 02 00`) and `loop_phase_sector_simulate` at
`0x0043a394`; the `collide` interval is the span between the two stamps, i.e.
the whole `call 0x0045d250` **including every callee**. The sampling profiler
was not enabled in that session: `grep -c profile_ session-20260918-090339-216.log`
returns **0** in run129 and 0 in run130/131/132/133/134 as well. The
address histogram the brief asks for therefore cannot be built from existing
data; §11.2–§11.4 are a static attribution constrained by counters.

### 11.2 The measurement that falsifies §5

From `/tmp/x3-bottleX3-run134/session-20260919-030610-212.log` (option on) and
`.../run133/session-20260919-030256-212.log` (option off), 300-frame windows:

| window (frame) | 599 | 2999 | 5099 | 5399 | 5999 | 6599 |
| --- | --- | --- | --- | --- | --- | --- |
| `collide_p50_us` | 1069 | 1383 | 2379 | 13394 | 21442 | 21667 |
| `p1_pairs_p50` | 200 | 210 | 208 | 208 | 209 | 202 |
| `p1_rejected_p50` | 36 | 36 | 36 | 36 | 36 | 36 |
| `p2_cands_p50` | 0 | 0 | 0 | 0 | 0 | 0 |
| `post_p50_us` | 9 | 5 | 7 | 10 | 9 | 9 |
| `simulate_p50_us` | 35 | 33 | 37 | 43 | 44 | 46 |

`p1_rejected_sum` is **exactly 10800 = 36 × 300** in every window from 899 to
6599: the same 36 pairs, every frame, deterministically. Three consequences:

1. **The §3 generic pair path is ~205 pairs/frame, and it is constant.** The
   broadphase input to the square root does not change at all while the bracket
   goes 1.07 ms → 21.7 ms (20×). Whatever ramps is not the pair loop.
2. **L1 never runs its candidate loop.** `p2_cands = 0` in all 22 windows, so
   bucket 0 is empty or fully gated; `0x0045cab0` contributes nothing here.
3. **The sector population is constant.** `post` is the bracket around
   `0x0045b720` ([sector-post-pass.md](sector-post-pass.md)), an O(N) walk over
   the same 32 bucket lists; it sits at 5–13 µs for the entire session, as do
   `simulate` (`0x00452ad0`, 32–46 µs) and `passb` (90–134 µs). An N² effect in
   the same lists is impossible without `post` moving.

The ramp is also **reversible**, which no accumulating structure explains. In
run129 `collide_p50_us` goes 203 µs (frame 3600) → 26345 µs (7800, plateau to
9000) → **683 µs (9600)** and stays at 400–500 µs for the following 8000 frames.
The "ramp over ~6000 frames" is the approach to a position and the plateau is
holding it; run129 adds the departure, which run133/134 did not capture.

### 11.3 Where the time actually is: the narrow phase on accepted pairs

The uncounted branch is the accepted one. At the compare §3 decodes:

```
0045d604  cmp ecx,eax / jg 0045d6cc       ; dist > R -> reject (uncounted)
0045d60c  push esi / push ebx
0045d60e  call 0045c780                   ; gate, returns 1 = skip pair
0045d615  test eax,eax / jne 0045d6cc
0045d620  or [ebx+0x44],1 / or [esi+0x44],1
0045d665  call 0048ac80(physA, physB, rA, rB)   ; NARROW PHASE
0045d66f  jle 0045d6cc                    ; <=0 -> no contact
0045d691  call 0045e130                   ; contact response
```

`0x0045c780` was decompiled this session: 806 B of class/flag/parent tests, one
bounded parent-chain walk at `0x0045c881`, no geometry — it is a filter, not a
cost. `0x0048ac80` zeroes `[phys+0x180..0x190]` on **both** objects and tails
into `0x0048a890`.

`0x0048a890` is **recursive over the two objects' scene-node part trees**:

- Head `0x0048a890`–`0x0048a928`: Chebyshev distance between the nodes' saved
  positions `[node+0xb0/0xb4/0xb8]` against `0x00488170(node)` radii (itself
  recursive over children, cached in `[node+0xa4]`); far ⇒ return 0.
- `0x0048a929`: both nodes must carry `[node+0x12c] & 0x1000000` and not
  `0x8000000`; otherwise ⇒ `0x0048aa46`, the descent.
- `0x0048aa46`–`0x0048ab7b`: **for each child of A (`[A+0xc]`, `[n] = next`,
  sentinel first-dword 0) recurse (child, B); if none hit, for each child of B
  recurse (A, child)** — an O(parts_A × parts_B) double walk. On a hit it
  transforms the contact point (`0x004f0da0`, `0x0040e780`) into
  `[obj+0x180..0x18c]` and unwinds.
- Leaf × leaf `0x0048a963`–`0x0048a9a5`: `[node+0x140]` model id →
  `0x004863c0` (the body cache: a **hash** lookup, bucket mask `[t+4]-1`, chain
  walk at `0x00486410` — O(1), not a growing scan, with an SEH frame per call)
  → `call 0x0047f1b0(bodyA, bodyB, 2, 1, 0.0f)`.

`0x0047f1b0` (0x191 B) builds two 3×3 matrices plus translations from
`[node+0xb0..0xe8]` (16.16 fixed, 18 `fild`/`fmul`/`fstp` pairs) and calls
`0x004e29f0` → `0x004e2780` → **`0x004e2530`**, the mesh collider:

```
004e2530  mov eax,[0x0060854c]           ; hits so far
004e253d  if ([0x00596934] && hits>0) return 0     ; mode 2 stops at first hit
004e257d  add dword [0x00608544],1       ; GLOBAL node-pair visit counter
004e25a3  call 0x004e3280                ; separating-axis / OBB reject (0x556
                                         ;  insns in the function, x87 + 0x40e710)
004e25cd  call 0x004e2190                ; leaf x leaf (-> 0x004e2ba0, 0x504 insns)
004e264c/004e269f/004e2705/004e2767      ; four recursive calls to 0x004e2530
```

`[node+0x3c]`/`[node+0x40]` are the two children of a **static binary BVH stored
in the model data**, and the recursion descends whichever side has the larger
`[node+0x30]` radius. So per accepted pair the cost is
`parts_A × parts_B × (BVH_A × BVH_B node pairs)`, all of it x87, with
`0x004e3280` (an SAT test) executed at every node pair. A node-pair visit is on
the order of 100–400 executed instructions (**estimate**, the path through
`0x004e3280` is data-dependent); at the §5 FEX bracket of 300–800 M insn/s that
is ≈ 0.2–1.3 µs, so the 21.7 ms plateau is **≈ 20 k–100 k node-pair visits per
frame**. Nothing else in the routine can absorb that budget.

**Why mode 2 makes a near-miss the worst case.** `0x0048a9a5` passes mode `2`,
max-hits `1`, tolerance `0.0f`; `0x004e29f0` stores them in `[0x00608534]` /
`[0x00608538]` and `0x004e27d3` sets `[0x00596934] = 1` for mode 2, which is the
"stop at the first hit" predicate at `0x004e253a`. A pair that **does** collide
therefore aborts early. A pair whose bounding spheres overlap (`dist ≤
r_sum·1.01`) but whose meshes do not touch runs the **full** BVH × BVH descent
to exhaustion, every frame, forever. That is the expensive state.

### 11.4 What this explains, and what it leaves open

Consistent with every number in §11.2: the broadphase counts are constant
because the sector is unchanged; `--collide-box-cull` cannot help because the 36
pairs it removes are ~36 × ~58 instructions ≈ single-digit µs; the "107 µs per
pair" of the profiler note is an artifact of dividing by the broadphase
denominator instead of the accepted-pair one; the cost is reversible because it
is a function of where the objects are, not of anything that accumulates.

**Inferred, not measured:** that the dominant accepted pair or pairs involve the
player's ship parked inside a large object's bounding radius (station, and note
that `[obj+0xa4]` is a single sphere radius, so an elongated station overlaps a
large volume it does not occupy). A static pair of permanently overlapping
sector fixtures would produce a constant cost, which is not what run129 shows,
but a mixture of the two is not excluded.

**Unknown:** the number of accepted pairs per frame (nothing counts the
`dist ≤ R` branch); which objects they are; the part counts and BVH depths of
the models involved; the real per-node-pair instruction count.

**Ruled out as the ramp mechanism, by reading:** there is no list append in
`0x0045d250`, `0x0048ac80`, `0x0048a890` or `0x004e2530`. The only per-pair
writes are `[obj+0x44] |= 1/2` flag bits (cleared by L0 at `0x0045d275` in the
same call and by L3 at `0x0045e000`) and the contact scratch
`[phys+0x180..0x190]`, which `0x0048ac80` **zeroes on entry** for both objects.
The child lists (`[node+0xc]`) and BVH links (`[node+0x3c]`/`[node+0x40]`) are
model data. `0x004863c0` is a hash lookup, not a linear scan of a growing cache.
The `0x0060854c`/`0x00608544`/`0x00608538` globals are reset per top-level query
at `0x004e2947`–`0x004e2951`. An image-wide scan for the four-byte little-endian
address finds references to `0x00608544` only at `0x004e0c01`, `0x004e0c8c`,
`0x004e0cf6`, `0x004e257f`, `0x004e27a2`, `0x004e2948`, `0x004e29b5`,
`0x004e38bb` — all inside this collision subsystem.

### 11.5 Smallest diagnostics (one flight), with hook-site suitability

Three `inc dword [abs32]` counter sites in the `--collide-census` shape, read and
zeroed once per Present alongside the existing four. Bytes below were re-read
from the image this session; no rel32 `call`/`jmp`/`jcc` target and no abs32
pointer anywhere in `.text` lands inside `+1..+4` of any of the three, and the
enclosing decoded regions (`0x0045d5fd`–`0x0045d6d4`, `0x0048a890`–`0x0048ac20`,
`0x004e2530`–`0x004e2780`) contain no short jump into them either; the
whole-function check belongs in `verify_collide_sites.py`.

| # | Site | Displaced span | Counts | Rate/frame |
| --- | --- | --- | --- | --- |
| 5 | `0x0045d665` | `e8 16 d6 02 00` (5, one `call`) | **accepted pairs** — the missing denominator | 0–200 |
| 6 | `0x0048a9a5` | `e8 06 48 ff ff` (5, one `call`) | mesh-pair tests = leaf part-node pairs | 10⁰–10³ |
| 7 | `0x004e2530` | `a1 4c 85 60 00` (5, `mov eax,[0x60854c]`, the function's first instruction; 5 inbound calls `0x004e264c`, `0x004e269f`, `0x004e2705`, `0x004e2767`, `0x004e2956`, all to the entry) | **BVH node-pair visits** — the actual unit of cost | 10⁴–10⁵ |

Site 5 and 6 are `call` redirects: the stub increments and jumps to the original
target, so the callee's `ret`/`add esp` contract is unchanged and no register or
flag state is displaced — the flags at a `call` boundary are dead by the same
argument §10 makes for P1. Site 7 displaces the callee's first instruction, so
the stub must `inc` a slot, re-execute `mov eax,[0x0060854c]` and jump to
`0x004e2535`; EAX is defined by that very instruction and flags are not written
by it, so nothing needs saving; the stub is called on a fresh frame before
`sub esp,0x40`, so ESP handling is a balanced `push`/`pop` as in §10.
Reentrancy: none of the three stubs calls anything or keeps state beyond the
counter, and site 7 is on a recursive path where only a plain increment is
admissible (no QPC, no tracker).

Timing, if the counts alone do not settle it: an accumulate-only `game_phases`
pair around `0x0045d665` gives narrow-phase microseconds per frame directly at
≤ 200 QPC pairs per frame. Do **not** put a clock at site 7.

A cheaper zero-patch alternative for site 7 exists in principle — `[0x00608544]`
is already the engine's own node-pair counter — but it is reset per top-level
query at `0x004e2948`, so reading it once per frame yields only the last mesh
pair's count; accumulating it needs a hook anyway.

### 11.6 Candidate fixes, and their gameplay risk

The shape of the problem changed: at the narrow phase the pair count is ~10²,
not ~10⁵, so a **cross-frame memo is affordable here** — the objection in §6
("Not proposed") applied to the broadphase, not to this site.

1. **Temporal memo on the no-contact result** (leading candidate). Key: the two
   object pointers plus both physics blocks' integer position `[phys+0x30/34/38]`
   and the orientation words the node matrices derive from
   (`[node+0xc0..0xe8]`). If both are bit-identical to the previous frame and
   the previous result was "no contact", return 0 and reproduce the only side
   effect a no-contact call has: `0x0048ac80`'s zeroing of
   `[phys+0x180..0x190]` on both objects. *Equivalence:* `0x004e2530` reads only
   the two node transforms, the static model BVHs, and globals that
   `0x004e2780` resets on entry; it writes `[obj+0x180..0x18c]` **only on a hit**.
   So for identical inputs it is a pure function. *Risk:* a wrong key (missing a
   transform input, or an object mutating its model — `[node+0x140]` and
   `[node+0x12c]` must be in the key) turns a real collision into a silent
   miss. Needs the same enumerate-the-boundary fixture discipline as §8, plus a
   bounded memo (a fixed ring keyed on the pair, no allocation) and a hard
   invalidation on any flag change in `[obj+0x40]`/`[obj+0x44]`.
   *Expected saving:* the whole plateau, if the accepted pairs are static.
2. **Tighter bound before `0x0045d665`.** The engine gates on a single sphere
   radius `[obj+0xa4]`; an AABB/OBB test using the top node's own extents would
   turn many "spheres overlap, meshes far" pairs away before the descent.
   Lower risk than (1) (it is another conservative reject, provable the same way
   as P1) but it only helps when the bound is genuinely loose; if the player is
   truly inside the station's box it saves nothing.
3. **Not proposed.** Capping the descent (`[0x00608534]`/`[0x00608538]`) or
   skipping `0x0045d665` on a frame stride: both drop real collisions and both
   are visible in gameplay.

Measurement (§11.5) must come first: fix (1) is worth its verification cost only
if site 5 shows a small number of accepted pairs holding a large share of
site 7's count.

### 11.7 Implemented: `--collide-narrow-census` (2026-09-19)

`X3M_COLLIDE_NARROW_CENSUS=1` (launcher `--collide-narrow-census`, default off, independent of
`--collide-box-cull`; both may be on), `src/proxy/collide_narrow_census.{h,cpp}` and
`collide_narrow_census_core.h`. It answers §11.4's unknowns in one flight: accepted pairs per frame, BVH work per
pair, which objects, and whether fix 1 of §11.6 would hit.

**Correction to §11.3/§11.5 found at the bytes: both callees take register arguments.** `0x0048ac80` receives
`physA` in **ECX** and `physB` in **EAX** (`0045d65e mov ecx,[ebx+0x70]` / `0045d662 mov eax,[esi+0x70]`, first use
`0048ac84 mov [ecx+0x190],edx`), plus two stack words (`rA`, `rB`; the caller pops 8, the callee ends in a plain
`ret`); `0x0047f1b0` receives the two nodes in ECX/EAX and `EDI = 0` besides five stack words. So `[obj+0x70]`, the
"physics block", **is** the scene node of §11.3 (`[+0x30..0x38]` position, `[+0xb0..0xbc]` saved position,
`[+0xc0..0xe8]` matrix, `[+0x12c]` flags, `[+0x140]` model id, `[+0x180..0x190]` contact scratch), and a stub at
either call may not clobber any register. `0x0048a890` ends at `0x0048ac27` (919 B, not 910).

| Site | Patch | Stub |
| --- | --- | --- |
| 5 `0x0045d665` | `engine_patch::claim_call`: only the rel32 changes, the instruction stays a `call` | 289 B, below |
| 6 `0x0048a9a5` | `claim_call` | `inc dword [mesh]; jmp 0x0047f1b0` (11 B) |
| 7 `0x004e2530` | `engine_patch::claim`, 5 bytes = the one `mov eax,[0x0060854c]` | `inc dword [node]; mov eax,[0x0060854c]; jmp 0x004e2535` (16 B; no clock, no call; reached through the dispatcher's one `jmp [entry]`) |

**Site-5 bracket.** `cmp dword [esp],0x0045d66a; jne foreign` / `cmp byte [busy],0; jne nested` / `mov byte [busy],1`
/ *save*, `push esi; push ebx; call pre`, *restore* / `lea esp,[esp+4]` (drops the engine's return address without
touching EFLAGS) / `call 0x0048ac80` (pushes the stub's return into the same slot, so the callee sees the engine's
exact stack and register arguments and keeps its `ret` contract) / *save*, `push eax; call post`, *restore* /
`mov byte [busy],0` / `jmp 0x0045d66a`. *save/restore* = `pushfd; pushad; cld; sub esp,0x80; movups [esp+16i],xmm_i`
and the reverse, so EAX (the result), ECX/EDX, the callee-saved registers, EFLAGS and XMM0-7 reach the engine exactly
as the callee left them. x87/MMX are never touched: the stubs contain no such instruction (build audit of the exact
instruction sequence) and the two C++ handlers are roots of `check_no_x87.py` and run under `LightCallBoundary`
(MXCSR and LastError; `QueryPerformanceCounter` may set the latter). A re-entered narrow phase (`busy`) or a caller
other than the patched site (`[esp]` mismatch) passes straight through and is counted (`nested`, `foreign`); an
unwind past the stub leaves `busy` set, which fails safe to pass-through.

**Liveness, checked by `verify_collide_sites.py` on the image** (51 checks, 22 new): each site is one whole
instruction with the documented target; the pre/post windows, the 103 bytes of `0x0048ac80` (its rel32 as a target),
the head of `0x0047f1b0` and the first 35 bytes of `0x004e2530`; **image-wide** no rel32 `call/jmp/jcc` (byte scan of
all of `.text`, a superset of the real branches) and no abs32 dword anywhere in the file lands in `+1..+4` of any
span, nor a decoded short jump of the enclosing functions; the inbound references of `0x004e2530` are exactly the
five calls of §11.5; EFLAGS are dead at both callee entries (`xor edx,edx`, `sub esp,0x64` are the first flag
instructions), at site 5's return (`add esp,8`) and after site 7 (`sub esp,0x40`), which is what the stubs' `cmp`
and `inc` rely on; EBX/ESI are still the pair at site 5 (`or [ebx+0x44]` / `or [esi+0x44]` at `0x0045d620`, no
write to either and no call up to the site, used again at `0x0045d676`); the claim windows are disjoint from all 129
other claims, the box cull's seven windows and `cull_small_parts` `0x0047d2a2` included.

**Per pair** (pre handler, before the clock starts): both object pointers, class `[obj+0x48]`, subtype
`[obj+0x4a]`, flags `[obj+0x40]`/`[obj+0x44]`, radius `[obj+0xa4]`, the node pointer, position `[node+0x30/34/38]`,
model id `[node+0x140]`, node flags `[node+0x12c]`, a 32-bit hash of `[node+0xc0..0xe8]` + model id + node flags
(the memo key of §11.6) and, separately, a hash of the saved position `[node+0xb0..0xbc]` that `0x0047f1b0` uses as
the translation (kept apart so that a key component that never repeats is visible on its own). Post handler: QPC
ticks, the result, and the deltas of the site-7 and site-6 counters across the call = BVH node-pair visits and
mesh-pair tests **attributed to this pair**. Entries go into a fixed ring of 256 per frame (three static rings
rotated at Present: writing, this frame, previous frame; no allocation); further pairs are counted in the totals and
as `ring_overflow`. Every field read lies inside a block the engine itself dereferences on the same pair
(`[node+0x190]` is written by `0x0048ac80` unconditionally).

**Threads.** The stubs run on the engine's main-loop thread, which is also the Present thread: `loop_phases`
accumulates its `collide` interval only on the Present thread and measures it in every flight. The module does not
rely on it. The stub counters are monotonic, written by one thread, and Present only takes deltas of aligned 32-bit
loads (it never writes them, so no increment can be lost to a zeroing). The ring and the frame totals sit behind a
try-lock neither side waits on: a contended post handler counts `dropped`, a contended Present defers its frame
into the next. The window line reports `dropped`, `deferred` and `cross_thread_frames`; all three must be 0 in a
normal flight. The fixture runs Present on a second thread against 20,000 pairs and accounts for every one.

**Output.** One `collide_narrow` line per 300 frames: `accepted`, `mesh_pairs`, `node_pairs`, `narrow_us`, each
`_p50/_max/_sum`; `recorded_sum`, `with_previous_sum`, `unchanged_sum` (same pointers, positions, transform hash and
saved hash as the previous frame's entry of the same pair), `memo_would_hit_sum` / `_permille` (unchanged **and**
no contact in both frames, over all accepted pairs), `memo_visits_sum` / `_permille` (the node-pair visits those
pairs cost, over all visits: the saving fix 1 would realise), `memo_unsafe_sum` (unchanged key, no contact before,
contact now: the key misses an input), `memo_visits_differ_sum` (unchanged key and result but another visit count:
the routine is not a pure function of the key, e.g. animated child parts, which the key does not hash),
`changed_pos/xform/saved_sum`, `ring_overflow`, `dropped`, `deferred`, `nested`, `foreign`,
`cross_thread_frames`. On F8 frames one `collide_narrow_pair` row per entry ordered by visits: `a`/`b`, class,
subtype, model, radius, flags, positions, `d_max` (Chebyshev distance), `r_sum`, `visits`, `mesh_pairs`, `us`,
`result`/`contact`, `previous`, `same_pos/xform/saved`, `unchanged`, `memo_hit`, `memo_unsafe`, `visits_differ`.
Parsers: `parse_narrow_window_line`, `parse_narrow_pair_line` in `verify_collide_sites.py`.

**Reading it.** Fix 1 is worth its verification cost if `memo_visits_permille` is high with `memo_unsafe_sum = 0`
and `memo_visits_differ_sum = 0`. A non-zero `memo_visits_differ_sum` means child-node transforms must join the
key; `changed_saved_sum` without `changed_pos_sum` means the saved position moves on its own and must be in (or
provably out of) the key. The pair rows name the objects: a station class with `d_max` well inside `r_sum` and tens
of thousands of visits is the §11.4 inference confirmed.

**Install.** Backend-load path inside the `engine_patch` window after the exact-executable check, the windows, both
callee bodies and both call targets; module pinned; claimed 7, 6, 5 and a failed later claim restores the earlier
ones; `late_claim` after the first Present; LastError preserved.

**Fixture.** `verification/probe/collide_narrow_census_fixture.cpp` (40 checks): the engine's bytes around all three
sites at the engine's offsets, a byte-exact replica of `0x0048ac80`, stand-ins for `0x0048a890`/`0x0047f1b0` that use
x87 and record their register and stack arguments; only rel32/abs32 operands differ. 1,500 scenarios (results
-1/0/1/7, 0-3 mesh pairs, 0-5 node visits each, both paths of the `0x004e2530` head, four x87 control words)
unpatched and patched: same exit, all eight registers, EFLAGS (AF excepted: undefined after the engine's own `test
eax,eax`, and FEX derives it lazily), the whole `fnstenv` image, `st(0)/st(1)`, the engine locals, callee
arguments, zeroed contact scratch and LastError; counters and ring entries exact; overflow 256 + 44; the memo
classes; rows and window line; nested/foreign; cross-thread Present; refusals; a partial install (sites 7 and 6
patched, site 5 refused) rolled back byte-exact; restore; closed window. Self-cost there (harness-inclusive, FEX):
+220 ns per accepted pair, +1.5 ns per node-pair visit, i.e. about 0.04 ms + 0.15 ms per frame at 200 pairs and
10^5 visits. Ledger: [sampling-profiler.md](../verification/sampling-profiler.md), "Collide narrow census".

### 11.8 Reviews (2026-09-19)

Opus review and Fable second review: nothing blocking. Confirmed from the image: the x87 stack is empty at
`0x0045d665` on every path, no function on the narrow-phase call path installs an SEH frame, stack and flags at
`0x0045d66a` are the callee's in both return conventions, the handlers' clock brackets only the engine call
(`annotate()` runs at Present, outside it). **Reading `narrow_us`:** it includes the census's own stub cost inside
the bracket: about 1.5 ns per node-pair visit (sites 6 and 7, about 0.15 ms per frame at 1e5 visits) and one
register/XMM save-restore per accepted pair (about 220 ns); subtract them before judging a fix, and do not compare
frame time with the option on against a run with it off. Accepted low points: the `busy` byte is a non-atomic
global (one thread assumed; `cross_thread_frames`, `nested`, `dropped` expose a violation); an unwind past the
site-5 stub leaves `busy` set for the process (census degrades to pass-through, `nested` grows); stub 7 ignores
`push_front`'s continuation (no second claimer of `0x004e2530` exists); the verifier's rel8 scan for site 7 starts
at the function entry; sites 5 and 6 take a plain 5-byte write inside the install window (expect
`write_n5=plain write_n6=plain` in the flight log).

## 12. The BVH descent and the fix options

2026-09-19, same method as §11: nothing launched, no Ghidra project; every listing below was taken with
`objdump -d --x86-asm-syntax=intel` over the bottle EXE in place (non-relocatable, so file address = VA for `.text`
VA `0x00401000` / raw `0x400` / `0x130800` B), the PE section table parsed from the file, and the run140/run141
figures taken with `grep`/`awk` over the session logs (53/55 MB, never read whole). Marks: **[m]** measured in a
flight, **[s]** static reading of the image, **[i]** inference.

### 12.1 The two objects: `a` is the station, `b` is the ship (corrects the run-44 reading)

The type-table loader at `0x0043626e` clears `[0x00606fb8 + 4*class]` / `[0x00607038 + 4*class]` for
`class = 0..0x1f` and dispatches through the 32-entry jump table at **`0x0043944c`**; each case pushes the type
file's name. Decoding that table gives the engine's class numbering directly **[s]**:

| class | table | class | table | class | table |
| --- | --- | --- | --- | --- | --- |
| 0 | TBullets | 8 | TLaser | 18 | TGates |
| 1 | — (default) | 9 | TShields | 19 | — (default) |
| 2 | TBackgrounds | 10 | TMissiles | 20 | TSpecial |
| 3 | TSuns | 11–16 | TWareE/N/B/F/M/T | 21–24, 26, 27 | — (default) |
| 4 | TPlanets | 17 | TAsteroids | 25 | TCockpits |
| **5** | **TDocks** | | | 28 | TDebris |
| 6 | TFactories | | | 29/30/31 | TDocksWrecks / TFactoriesWrecks / TShipsWrecks |
| **7** | **TShips** | | | | |

The mapping is corroborated by every class-specific path already documented here: class 0 = TBullets is the swept
projectile pass of §4; class 4 = TPlanets is the `"NotifyPlanetCollision"` branch of §3; class 3 (TSuns) is in
L2's skip set with the three empty classes 19, 21, 27; class 20 = TSpecial is the `subtype 0x5c` special case; and
the §3 gate that tests `abs(d) ≤ r_sum·3 + 250000` and then calls `0x0044d4c0` (`"CanWarp"`/`"CanLand"`) and
`0x0044e450` (proximity) is the **class-7 = ship** path, which is what warps and lands.

So the expensive pair is **`a` = a dock/station (TDocks index 26), `b` = ships (TShips indices 211–223)** — the
opposite of the §11.4 guess and of the run-44 A reading. The F8 rows confirm it numerically **[m]**:
`radius_a=9205962` (≈18 km at the §6 scale of ~505 units/m) against `radius_b=43200` (≈85 m) for the dominant
partner and `7753` for the next one; `pos_a=-2500000,0,250000`, three round numbers, i.e. a fixture that never
moves. The class-7 subtypes 211–223 are therefore **ship types, not station modules**: ~20 ships parked around one
station. The station is a single sector object whose *parts* produce the 32 mesh-pair tests per query (§11.3's
`0x0048a890` part-tree walk), not 20 objects.

`b = 0x11b21bb8` is the player's ship **[i]**: it is the only partner whose visit count collapses (232,978 → 3,326,
70×) when the player flies away while the accepted set stays the same 18–20 partners, and it is bit-static
(`unchanged=1 same_pos=1 same_xform=1`) exactly while the player holds still **[m]**. Which dock type index 26 is
(shipyard, equipment dock, trading station…) needs `addon\types\TDocks`, which was not opened here.

### 12.2 The collider is RAPID (UNC), lightly modified

Everything below matches Gottschalk/Lin/Manocha's RAPID 2.01 line for line **[s]**: `obb_disjoint` with 15
separating axes and the `reps` margin (`0x00565600` = `1e-6f`, verified as the float at that address),
`collide_recursive` descending the box with the larger first extent, one triangle per leaf, `tri_contact`, the
global counters, and the flag `RAPID_FIRST_CONTACT = 2` — which is exactly the `2` that `0x0048a9a5` passes.

| Structure | Address / field | Contents |
| --- | --- | --- |
| BV node, `0x48` B (`0x004e0f00` allocates `2N`) | `+0x00..+0x20` | 3×3 rotation of this box in its parent's frame, row-major |
| | `+0x24..+0x2c` | box centre (3 floats) |
| | `+0x30..+0x38` | half-extents (3 floats); `+0x30` is the first/principal axis |
| | `+0x3c`, `+0x40` | children (both 0 = leaf) |
| | `+0x44` | triangle record (`0x34` B, `N` allocated): `+0x04..+0x24` = three vertices |
| model | `[node+0x5c]` | RAPID model; `[model+0x14] == 3` = built; `[model+0x00]` = root box |

Per-query globals, all reset by `0x004e2780` at `0x004e2947`–`0x004e2951` **[s]**:

| Global | Role | Value on the narrow-phase path |
| --- | --- | --- |
| `0x00608534` | flag word, stored by `0x004e29f0` from ECX | `2`; bit 2 (`&4`) = "cap contacts", bit 3 (`&8`) = distance mode — both clear here |
| `0x00608538` | contact cap | `1`, **not consulted** because bit 2 is clear — on the `0x0048a9a5` path only, see below |
| `0x00596934` | first-contact flag, set at `0x004e27e7` when mode == 2 | `1` |
| `0x00608544` | box-test (node-pair visit) counter, `add …,1` at `0x004e257d` | census site 7 counts the same events |
| `0x00608548` | **triangle-test counter**, `add …,1` at `0x004e22a5` inside the leaf | never read by the census — see §12.6 |
| `0x0060854c` | contact counter, incremented at `0x004e24e9`; `0x0047f1b0` returns `!= 0` | 0 for this pair |
| `0x0059692c`…`0x0059695c`, `0x00596938` | top-level R, T and scale used by the leaf test | set at `0x004e28ae`–`0x004e2925` |

**Second query path (review, §12.9) [s].** `0x0047f1b0` has a second caller, `0x0048a69e`, which passes flags `0xc`
(bit 2 = cap contacts, bit 3 = distance mode) and a cap of 8 and keeps a running-minimum contact. "Cap inactive" and
"first contact" above are therefore true only for the `0x0048a9a5` path. The cap at `0x004e2553` compares the
**contact** counter `[0x0060854c]`, not visits, so a box test that visits a superset of node pairs in the same order
reaches the same contacts in the same order and hits the cap at the same one: the superset argument of §12.5 holds
on this path too.

### 12.3 `0x004e2530` exactly

`int __cdecl descend(BV* a, BV* b, float* R, float* T, float s)`, 5 args (`add esp,0x14` at all five call sites),
locals `sub esp,0x40`, pushes EBX/EBP/ESI/EDI, three `ret` exits **[s]**. Five inbound calls only (§11.5), no
abs32 reference to the entry anywhere in the file. `R`/`T` are the relative transform of `b`'s frame in `a`'s
frame at this level; `s` is a per-query relative model scale, composed at `0x004e2914`/`0x004e2925` from the two
per-object scale arguments.

```
004e2530  eax=[0x60854c]; if ([0x596934] && eax>0) return 0      ; first contact already found
004e2553  if ([0x608534]&4 && eax >= [0x608538]) return 0        ; inactive on this path
004e2564  fld [b+0x30]; fld s; fmul …                            ; b's three half-extents x s -> locals
004e257d  add [0x608544],1                                       ; the visit counter
004e25a3  call 0x004e3280(esi=R, edi=&s*b.d, [esp]=T, [esp+4]=&a.d)
004e25ad  if (eax != 0) return 0                                 ; separating axis found -> prune
004e25b3  if (a and b are both leaves) return 0x004e2190(esi=a, eax=b)   ; one triangle x one triangle
004e25da  if (b is a leaf) split a;  else if (a is a leaf) split b
004e25f3  else fld [b+0x30]; fcomp [a+0x30]  ->  split whichever box has the larger +0x30
004e2604  split b: child [b+0x40] then [b+0x3c]; per child 0x004e1ff0 (R') + 0x004e20d0 (T')
004e26af  split a: child [a+0x40] then [a+0x3c]; per child 0x004dfd80 (Rᵀ) + 0x004dfe60 (T')
          after the first child: test eax,eax / jne -> return the hit immediately
```

Answers to the brief's questions **[s]**: the bounding volume is an **OBB** (rotation + centre + half-extents),
not a sphere and not an AABB; the pair test is the 15-axis SAT; the descent splits **one** node — the larger — so
a visit spawns **two** child pairs, never four; the child order is `+0x40` before `+0x3c`; there is an early-out
on the first hit (mode 2 also makes every still-pending visit return 0 at its head); a leaf holds exactly **one**
triangle, and a triangle test happens **only when both** nodes are leaves.

Static instruction counts on the executed paths (function bodies counted to their padding; `0x0040e710` is the
six-instruction `fabs` *function*, called 24 times from the SAT) **[s]**:

| Work | Instructions | Note |
| --- | --- | --- |
| `0x004e3280` separating at axis 1 | 79 + 10 fabs calls ≈ **149** | the nine `Bf = abs(R)+reps` are computed **before** the first axis test |
| `0x004e3280` full 15 axes (overlap) | 481 + 24 fabs calls ≈ **649** | 556 instructions in the function, 350 of them x87 |
| head + tail of `0x004e2530` | ≈ 30 | |
| two child transforms per descending visit | 2 × (83 + 37) = **240** | `0x004e1ff0` 83, `0x004e20d0` 37, `0x004dfd80` 83, `0x004dfe60` 29 |
| leaf triangle test `0x004e2190` → `0x004e2ba0` | 504 + 30 calls | more expensive than a box test |

Because a descending visit spawns exactly two children and every other visit spawns none, with no contact in the
whole query `V = 1 + 2·D` holds exactly, so **half the visits take the full 15-axis path plus both child
transforms** (~920 instructions) and half terminate (~150–650) **[s]**.

### 12.4 Why 230,000 visits, and what the count scales with

The visit count is not a property of the trees but of their overlap: `V = 1 + 2·D` where `D` is the number of
visited node pairs whose (transformed) OBBs overlap **[s]**. Loose volumes are *not* the mechanism — these are
oriented boxes, and the engine already descends only the larger one, which is the standard way of clipping a small
object against a big tree. What produces `D ≈ 1.2e5` is that the ship's boxes are **inside** the station's hull
structure: at the plateau the pair is 3.27 km apart by Chebyshev distance with an 85 m ship, and 15 % more distance
(`d_max` 1,651,365 → 1,893,828) drops the pair from 232,978 to 3,326 visits, a 70× fall, with the **same 32
part-pair queries** in both cases **[m]**. The cost per visit is flat across both regimes — 114.9 ns at the
plateau and 118.5 ns far away (26,766 µs / 232,978 and 394 µs / 3,326) **[m]** — so the lever on the plateau is
the **constant factor per visit**, not the branching.

Per part-pair that is 232,978 / 32 = 7,280 visits, i.e. ≈3,600 overlapping node pairs per part pair **[m+s]**.

**Open tension.** 114 ns/visit against ~550 statically counted instructions implies ≈4.9 G x86 instructions/s
under FEX, 6–16× above the 300–800 M insn/s bracket §5 assumed. One of the two is wrong; §11.3's "0.2–1.3 µs per
node-pair visit" is certainly too pessimistic (measurement beats it by 2–10×). This matters for sizing any fix, so
the first step below measures the replacement against a byte replica instead of predicting it.

### 12.5 Fix candidates

The decisive exactness property, from the code: **the SAT only prunes**. A non-zero result of `0x004e2530`, the
contact counter and the contact point are produced *only* by `0x004e2190` → `0x004e2ba0`, the triangle test.
A box test that is *more permissive* than the engine's therefore explores a superset of subtrees in the same
order, finds exactly the same contacts (a subtree the engine's correct test separated cannot contain one) and
reports the same first contact; it can only cost visits. That makes a conservative replacement **behaviourally
exact**, with one residual: if the engine's own SAT ever errs toward separation, a permissive replacement could
find a real contact the engine misses. *Corrected in review (§12.9):* the engine's `reps = 1e-6` is **not** a ~1e-4
relative bias. It adds about `1e-6·Σb` (or `1e-6·Σa`) to the radius sum, which is negligible against `ra` when
`ra ≫ rb` (a station box against a ship box), so it cannot be relied on to absorb x87-mode differences. The
implementation therefore carries its own relative margin of 2⁻²⁰ (§12.8), larger than what a 24-bit-precision x87
can lose over the four sums. And the argument covers **finite** pairs only: on an unordered compare the engine
*prunes*, and a replacement that kept such a pair would not merely cost visits — the leaf triangle test
`0x004e2ba0`/`0x004e2a50` reads a NaN compare as "not separated" and would report a contact the engine never
reports. NaN must separate exactly as in the engine **[s]**.

| | Candidate | Exactness | Expected reduction | Size / risk |
| --- | --- | --- | --- | --- |
| **(c)** | SSE2 reimplementation of `0x004e3280`, entered through its single call site | exact by the pruning argument; lazy `Bf`, conservative compare | SAT is 83 % of a terminating visit and 71 % of a descending one; 3× fewer instructions and 2–4× faster per instruction (SSE2 is native under FEX, x87 is not) ⇒ **2.5–4× per visit [i]**, to be measured | ~150 lines + fixture; low risk, one 5-byte rel32 |
| **(c+)** | extend to the whole `0x004e2530` (head, both child transforms, recursion), leaf test still the engine's `0x004e2190` | same argument | adds the 240-instruction transform pair and the head ⇒ **5–10× per visit [i]** | ~250 lines, owns the recursion and the counters; medium risk; collides with census site 7 |
| **(b)** | cheap SSE2 sphere/AABB pre-cull before the SAT | exact (a sphere containing the OBB) | `8 + (1−p)·114` ns; even at p = 0.5 only 1.75× | subsumed by (c): put it as the first lines of the replacement, not as a second patch |
| **(e)** | no-contact memo (§11.6 fix 1) | needs the full key; §11.6's risk stands | measured 93–99.9 ‰ of visits at the plateau, `memo_unsafe=0` and `memo_visits_differ=0` in all 58 windows **[m]** — but 0 in every window where the player moves, and `memo_hit=0` in the F8 taken while flying **[m]** | the fix for "parked near a station", not for docking or manoeuvring; combine *after* (c) |
| **(a)** | change the descent policy | — | **nothing to change**: the engine already descends only the larger box (`fcomp` at `0x004e25f3`) and never splits both | — |
| **(d)** | clip the big tree first / one-sided descent | — | **already what happens**: descend-larger walks the station tree down to ship-sized boxes before touching the ship tree; a separate pre-clip would re-do the same work | — |

Hook sites, bytes re-read from the image this session, all three windows checked image-wide for inbound branches
(byte scan of every `e8`/`e9` rel32, `0f 8x` jcc32 and `70–7f`/`eb` rel8 in `.text`, plus every abs32 dword in the
whole file): **no branch and no pointer lands inside `+1..+4` of any of them, and no abs32 reference to any of the
three function starts exists anywhere in the file** — so none is reached other than by the calls below **[s]**.

| Site | Bytes | Shape | Suitability |
| --- | --- | --- | --- |
| `0x004e25a3` (**recommended**) | `e8 d8 0c 00 00` | `engine_patch::claim_call`, rel32 only | **sole** call of `0x004e3280` in the image. In: ESI = R (9 floats), EDI = &(s·b.d) on the caller's frame, `[esp]` = T, `[esp+4]` = &a.d; caller pops 8; out: EAX = 0 or 1..15. Must preserve EBX, EBP, ESI, EDI (EBP and EBX are read again at `0x004e25da`/`0x004e26c2`) and EDX (the original never writes it); EFLAGS are dead (`test eax,eax` follows). The **x87 stack is empty at the site** — the head's `fld/fstp` sequence is balanced at `0x004e259f` — and an SSE2 replacement must leave it empty. No SEH frame on this path (§11.8). Leaf call: recursion depth unchanged |
| `0x004e3280` | `83 ec 24 d9 06` | `claim` of two whole instructions (`sub esp,0x24`; `fld [esi]`) | equivalent, if the entry is preferred to the call site |
| `0x004e2530` | `a1 4c 85 60 00` | census site 7 | needed for (c+); **mutually exclusive with `--collide-narrow-census`** |
| `0x004e2190` | `83 ec 34 53 57` | `claim` of three whole instructions | counter site for leaf triangle tests (§12.6); sole caller `0x004e25cd` |

Numerics for (c) **[s+i]**: under `FEX_X87REDUCEDPRECISION=1` the engine's x87 evaluates in double, so an SSE2
`double` implementation that keeps the same association order *and* reproduces the two float32 roundings the
engine performs (each `Bf` entry is stored back through `fstp dword`, and the scaled `b` extents are stored as
float32 by `0x004e2530`) is bit-identical in its boolean result there. It is **not** bit-identical on native
Windows x87 (80-bit registers, and §6 notes the software-vertex-processing device path can leave precision
control at 24 bits), nor is `fcompp`'s unordered case automatic. Do not aim for bit-equality on finite pairs:
implement the comparison as "separated iff `!(|T·axis| <= (ra + rb)·(1 + 2⁻²⁰))`" — unordered separates, as `fcompp`
does (the first draft of this note said "treat NaN as overlap" with a 2⁻⁴⁵ slack; both were wrong, §12.9) — and rely
on the pruning argument above. The threshold that could differ is exactly the near-tangent box pair, and there the
engine's own `reps` already decides in favour of descending.

### 12.6 First step and the counters that confirm it

1. **Build (c) and measure it in a host fixture before installing anything.** The fixture embeds a byte replica of
   `0x004e3280` (1,582 B, `0x004e3280`–`0x004e38ad`; only the `call 0x0040e710` rel32 and the two abs32 loads of
   `0x00565600` need relocating, the same technique `collide_narrow_census_fixture.cpp` already uses for
   `0x0048ac80`), runs both over ~10⁶ node pairs drawn from real ranges plus near-tangent cases, and asserts
   (i) whenever the replica returns 0 the SSE2 version returns 0, and (ii) the wall-clock ratio under
   `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py …`. That ratio is the whole business case for
   (c)/(c+) and it needs no flight.
2. **One flight, two counters,** both plain increments on the existing census pattern, to decide whether (c+) is
   worth its extra size and whether the leaf tests matter:
   - **leaf triangle tests per accepted pair** — `inc` at `0x004e2190` (`83 ec 34 53 57`, then re-execute the
     three displaced instructions and `jmp 0x004e2195`; the `inc`'s flags are then overwritten by the engine's own
     `sub esp,0x34`, and the next instruction `mov edi,eax` reads none). The engine's own counter `0x00608548`
     holds the same number but is reset per mesh-pair query at `0x004e294c`, so it cannot be read once per frame.
   - **the visit mix** — in the site-7 stub, before `mov eax,[0x0060854c]`, classify the visit by
     `[esp+0xc]`/`[esp+0x10]` (the two node arguments at that point, the stub runs before `sub esp,0x40`) into
     four counters: both-leaf, a-leaf, b-leaf, both-internal. ~12 extra instructions per visit ≈ 0.5 ms/frame at
     2.3e5 visits; EFLAGS are already established dead there (§11.8), EAX/ECX must be saved and restored.
   Together with the static counts of §12.3 these give the split of the 25 ms between SAT, transforms and triangle
   tests directly, which is what sizes (c) against (c+).
   A **descent-depth histogram is not recommended**: it needs a return-intercepting wrapper on a function entered
   ~2.3e5 times per frame at depth, and the leaf/internal mix answers the same question more cheaply.
3. Only then (e), as the complement for the standing-still case.

### 12.7 Not established here

The name of TDocks index 26 and of TShips 221 (needs `addon\types\*`, not opened); the triangle counts and tree
depths of the two models; the number of triangle tests per query (counter `0x00608548` exists but nothing reads
it); whether `[node+0x30]` is provably the *largest* half-extent (RAPID orders the box axes by the covariance
eigenvalues; the build path `0x004dfef0`/`0x004e0e70`/`0x004e1220` was not re-read for a sort) — the descent's
"larger" decision uses only that one component; the real FEX instruction throughput behind the §12.4 tension; and
the identification of `b = 0x11b21bb8` as the player's ship, which is inference from the visit collapse and the
bit-static rows, not a field named "player" in any log.

### 12.8 Implemented: `--collide-sat-sse2` and the triangle-test counter (2026-09-19)

**Status (2026-09-19): launcher default on every modded launch** (`tools/manage.py`, `collide_default`);
`--no-collide-sat-sse2` turns it off, `--vanilla` forwards nothing unless the option is given. The DLL's own default
stays off (no variable = nothing patched). What follows describes the option as first shipped, default off.

`X3M_COLLIDE_SAT_SSE2=1` (launcher `--collide-sat-sse2`, default off, independent of `--collide-box-cull` and
`--collide-narrow-census`; any combination may be on), `src/proxy/collide_sat_sse2.{h,cpp}` and
`collide_sat_sse2_core.h`. Candidate (c) of §12.5: `engine_patch::claim_call` on `0x004e25a3`, only the rel32 changes.

**What `0x004e3280` is, from the bytes [s].** `sub esp,0x24; push ebx; push ebp; push ecx` (the `push ecx` is a stack
reservation for the `fabs` argument, ECX is never written), EBX = `[esp+8]` = `&a.d`, EBP = `[esp+4]` = `T`. All nine
`Bf[k] = (float32)(|R[k]| + reps)` first (**nine** `fadd dword [0x00565600]`, not two; 24 calls of the float `fabs`
helper `0x0040e710`), then the 15 axes in RAPID's order A0, B0, A1, A2, B1, B2, A0xB0 … A2xB2. Every projected
distance goes through a **float32 stack slot** before `fabs` (the helper takes a float), and each compare is
`fcompp; fnstsw; test ah,1`: the axis separates iff `ra + rb < |T·L|`, and **unordered counts as separated** (C0 is
set), so the engine prunes on NaN. Result: EAX = 0 (descend) or the number 1..15 of the first separating axis; 16
plain `ret`s. It also overwrites its own second argument slot with the running flag, which the caller pops unread.
Registers: EBX/EBP saved, ECX/EDX/ESI/EDI never written.

**Replacement.** `core::obb_disjoint`: the same operands in the same association order in `double` (a product of two
float32 values is exact in double), the two float32 roundings reproduced (`Bf`, and the projected distance before
`fabs`), `Bf` rows computed when first needed (axis 1 needs row 0 only), and the compare
`separated = !(t <= (ra + rb)·(1 + 2⁻²⁰))`. That is the engine's predicate (`C0` set: `ra + rb < t` **or unordered**)
bit for bit on NaN, infinities and negative radius sums — same verdict, same axis number — and differs from it only
for finite pairs inside the 2⁻²⁰ margin, which it keeps. `|x|` is a sign-bit mask in the integer domain: GCC emits x87 `fld; fabs; fstp` for `std::fabs` of a loaded
value even under `-mfpmath=sse`, which the build audit caught.

**Thunk** (pure asm, 29 instructions, two paths): `push ecx; push edx; push eax; stmxcsr [esp]`, then if the MXCSR
control bits are the default (`& 0xffc0 == 0x1f80`) push `a`, `T`, EDI, ESI, call the cdecl body, drop, `pop edx; pop
ecx; ret`. Otherwise the same between `ldmxcsr [0x1f80]` and `ldmxcsr [saved]`. ECX/EDX are kept although the caller
does not need them (it reloads ECX at `0x004e2607`), EBX/EBP/ESI/EDI are callee-saved in the body, EFLAGS are dead at
the return (`add esp,8; test eax,eax`), the x87 stack is empty at the site (`sat_x87_empty_at_site`) and no x87/MMX
instruction exists in the module (build audit, `check_no_x87.py` roots `_x3m_collide_sat_thunk`,
`_x3m_collide_sat_sse2`). LastError is never touched (no API call). No lock, no counter, no `LightCallBoundary`.
**XMM0–7 are clobbered**: `0x004e2530`, `0x004e2190`…`0x004e252f` and `0x004e3280` contain no XMM/MMX operand at all
(`sat_no_xmm_in_descent`; 842 lines of the whole image mention `xmm`, none on this path), and XMM registers are
caller-saved in the Win32 ABI, so nothing can be live across the call.

**Why the default path holds no `ldmxcsr` [m].** FEX keeps *one* host rounding mode for x87 and SSE. The fixture
measured both directions: with MXCSR = round-up restored by an `ldmxcsr`, the x87 `1/3` computed after the call rounds
up although the x87 control word says nearest (4,000 of 16,000 bracketed calls, all those whose two modes differ);
and with the x87 control word written last and set to chop, the SSE body chops whatever MXCSR says (16 of 10,000
near-tangent verdicts move). An always-bracketing thunk would therefore re-impose MXCSR's rounding on the engine's
later x87 arithmetic 2.3e5 times a frame. With both modes at nearest — the game's state, D3D9 sets the x87 word and
nothing sets MXCSR — neither effect exists. On the default path only the sticky MXCSR exception flags can accumulate;
nothing in an x87-only process reads them, and FEX does not track them at all (`ldmxcsr 0x3fbf; stmxcsr` reads
`0x3f80`). On hardware with separate rounding state the bracketed path is exact.

**Exactness.** Against the engine as it runs here: FEX's reduced-precision x87 computes in double, so the verdict is
bit-identical except inside the 2⁻²⁰ band, where the replacement keeps the pair (and may then name a later axis). Against geometry, which is what holds
on native Windows (80-bit or 24-bit x87, neither equal to double): the replacement prunes only when
`|T·L| > ra + rb` with `Bf = |R| + 1e-6`, i.e. with RAPID's own bias toward overlap of ~1e-6 × extent against double
rounding of ~1e-16, so every pair it prunes is a pair of disjoint boxes, and disjoint boxes contain no intersecting
triangles. A 24-bit x87 loses at most ~2⁻²² over the four sums of a radius, so with the 2⁻²⁰ margin no x87 mode can
keep a finite pair that the replacement prunes; the replacement can keep a few more, which costs visits only.

**Fixture** `verification/probe/collide_sat_sse2_fixture.cpp` (41 checks). The 1,582 engine bytes are **not tracked**:
`build_collide_sat_sse2.py` reads them from the installed EXE, pins them by SHA-256, relocates the 24 rel32 and 9
abs32 operands at decoded instruction boundaries and writes `build/verification/collide-sat-sse2/sat_replica_inc.h`;
the fixture un-relocates its copy and checks the FNV-1a the production install checks. 2,560,000 node pairs:

| Category | Pairs | both keep | both prune | SSE2 keeps, replica prunes | replica keeps, SSE2 prunes | axis differs |
| --- | --- | --- | --- | --- | --- | --- |
| realistic, model units (station 1e-3…50, ship 1e-4…2, random rotation, 0–2.5 diagonals apart) | 1,200,000 | 161,719 | 1,038,281 | 0 | **0** | 0 |
| realistic, observed world magnitudes (station boxes 9.2e2…9.2e6, ship boxes 4.3…4.3e4 × the descent's scale 0.5…2) | 400,000 | 51,351 | 348,649 | 0 | **0** | 1 |
| near-tangent, model units (bisection on the replica to its own boundary; 0, ±1, 2, 4, 16, 64 float steps of T) | 440,000 | 200,000 | 79,448 | 160,552 | **0** | 1 |
| near-tangent, world magnitudes | 220,000 | 100,001 | 39,938 | 80,061 | **0** | 0 |
| degenerate (identity, signed permutations, single-axis rotations incl. exact 90°, non-rotations, zero extents, T = 0, faces touching exactly) | 150,000 | 60,948 | 88,516 | 536 | **0** | 7 |
| hostile (×1e30, ×1e19, denormal, negative extents, NaN/±inf/FLT_MAX in each of the 18 slots) | 150,000 | 4,970 | 145,030 | **0** | **0** | **0** |

Every one of the 241,149 extra keeps and 9 later axes is the margin and nothing else: with T stretched by 2⁻¹⁸ the
replacement prunes each of them at the engine's axis or an earlier one (`outside_band = 0`, `axis_unexplained = 0`),
and it never names an earlier axis. On random pairs the extra-keep rate is **0 of 1,600,000**; the margin is about 16
float steps of T wide, which is why the bisected set keeps the first steps past the boundary and prunes at 64. NaN,
infinities and negative extents get the engine's verdict and axis number on all 150,000 pairs. The same near-tangent sample under x87 control words `0x037f` and `0x007f`:
0 violations (FEX ignores precision control). State across the call, 20,000 direct calls under five MXCSR values
(default, round-down + sticky, round-up, chop + DAZ, precision exception unmasked) and two x87 control words: all six
preserved registers, the whole 28-byte `fnstenv` image, both live x87 registers, MXCSR; 3,000 scenarios through a
layout-preserving copy of `0x004e2530`…`0x004e25ae` (head, visit counter, argument set-up, the call, the engine's own
return-0 tail at its offset) unpatched and patched: same exit, EAX, ECX/EDX, EFLAGS (AF aside, §11.7), x87
control/tag/TOP, MXCSR, the 0x50-byte engine frame, visit counter, LastError. C0–C3, the sticky x87 exception bits and
the x87 last-instruction pointers differ by construction (the engine's `fcompp` leaves them, the replacement leaves
what was there) and are dead: the next x87 compare at `0x004e25f6` is followed by its own `fnstsw`. Coexistence: the
census's site-7 claim is installed on the same synthetic function first, both run, every visit is counted, either can
be restored first, bytes exact. Refusals: unset / `0` / no engine image (`callee_mismatch`), null site, changed
argument set-up, changed return window, another callee (`target_mismatch`), not a call, second install, closed window.

**Cost [m]** (FEX, direct calls, harness 3.1 ns subtracted; after the review changes; diagnostic timing, not game FPS):

| Mix | replica | SSE2 | ratio |
| --- | --- | --- | --- |
| early separation (mean axis 1.09) | 53.9 ns | **5.9 ns** | **9.1×** |
| full overlap (all 15 axes) | 121.5 ns | **19.8 ns** | **6.1×** |

The 5.9 ns is thunk plus axis 1, so the thunk itself is below that; the MXCSR bracket the default path avoids costs
1.6 ns. Sizing [i]: §12.4's `V = 1 + 2·D` makes about half the visits full-overlap, so the SAT averages ≈ 89 ns of the
measured 114 ns per visit (78 %, inside §12.5's 71–83 %) and ≈ 13 ns after; a visit would cost ≈ 38 ns, **≈ 3× per
visit, 25 ms → ≈ 8 ms** at the plateau. That is a projection from a fixture; the flight below measures it. It also
answers §12.4's open tension: FEX runs this x87 code at ≈ 5 instructions/ns, §5's bracket was too pessimistic.

**Triangle-test counter (§12.6 step 2, first item).** `--collide-narrow-census` gains site 8: `engine_patch::claim` of
`83 ec 34 53 57` at `0x004e2190` (three whole instructions, sole caller `0x004e25cd`, nothing enters `+1..+4`), stub
`inc dword [tri]; sub esp,0x34; push ebx; push edi; jmp 0x004e2195` (16 B; the re-executed `sub` rewrites every flag
the `inc` touched). Claimed first (8, 7, 6, 5), rolled back with the rest. Output: `tri_tests_p50/_max/_sum` after the
`narrow_us` series on the `collide_narrow` line and `tri_tests=` per pair on F8 rows; the install line ends in
`n8_site= write_n8= stub_n8=`. The visit-mix classifier of §12.6 (four counters in the site-7 stub) is **not**
implemented.

**One flight:** `--collide-sat-sse2 --collide-narrow-census --loop-phases` at the station, then the same without
`--collide-sat-sse2`. Compare `narrow_us_p50` per `node_pairs_p50` (subtract the census's 1.5 ns/visit, §11.8);
`node_pairs` must be equal to within the keep band (0 of 1.6e6 random pairs in the fixture) for a parked ship, and `tri_tests` says how much
of what remains is the leaf test. Expect `collide_sat_sse2 requested=1 patched=1 reason=ok … write=plain`.

Verifier: `verify_collide_sites.py`, 72 checks (29 box cull, 26 census, 17 SAT): whole call, **sole reference to
`0x004e3280` image-wide** (rel32 byte scan of `.text` and abs32 scan of the file), nothing into `+1..+4`, both
windows, the body's SHA-256 and FNV, 24 calls all to the `fabs` helper, 16 plain `ret`s, nine `reps` loads and
`[0x00565600] == 1e-6f`, no write to ECX/EDX/ESI/EDI in the body, the argument instructions, x87 depth 0 at the site,
flags written at the return, no XMM/MMX operand on the path, disjoint from 139 other claims including the census's four
sites and compared windows (and the census and box cull see `0x004e25a3` as foreign). Ledger:
[sampling-profiler.md](../verification/sampling-profiler.md), "Collide SAT SSE2".

### 12.9 Reviews of `--collide-sat-sse2` (2026-09-19)

**Opus review:** nothing blocking. Confirmed from the image: the ABI at `0x004e25a3` (ESI/EDI/two stack words, caller
pops 8, EBX/EBP saved, ECX/EDX/ESI/EDI unwritten); axes A0, B0 and A0×B0 operand-exact against the listing; no SIMD
instruction anywhere on the path; the thunk's pushes and pops balanced on both of its paths.

**Fable review:** one blocking finding, fixed. Confirmed: the SAT's result is never consumed beyond zero / non-zero
(the axis number is dead), the visit counter `0x00608544` is never read, the child order does not depend on the SAT.
**Finding:** the first version kept NaN pairs ("NaN = overlap", from this note's own §12.5). The engine prunes them
(`fcompp` unordered sets C0), and a kept NaN pair reaches the leaf triangle test, whose NaN compares read "not
separated": a false **contact**, not extra visits. Fixed by `separated = !(t <= limit)`; the fixture now requires the
engine's verdict and axis on every non-finite input (150,000 pairs, 0 differences). Also from the reviews: the margin
widened from 2⁻⁴⁵ to 2⁻²⁰ so that a 24-bit-precision x87 (native Windows software-VP path) can never keep a pair the
replacement prunes; §12.5's "~1e-4 relative bias" corrected; the observed world magnitudes added to the fixture; the
census's shutdown now runs both entry-site restores unconditionally; the second query path added to §12.2.

## 13. `0x004e2530`, the whole contract, and `--collide-descent-sse2` (2026-09-19)

Method as in §12: `i686-w64-mingw32-objdump -d -M intel` over the bottle EXE in place for `0x004e2530`–`0x004e2776`,
`0x004e2190`–`0x004e252d`, `0x004e1ff0`–`0x004e2186`, `0x004dfd80`–`0x004dfeaa` and `0x004e2780`–`0x004e2968`, plus a
rel32/abs32 byte scan of the image. Nothing launched. Marks as in §12. **Headline [m]: the replacement is behaviourally
identical to the engine on 126,150 tree pairs and brings nothing under FEX — 31.0 ns per visit against 31.4 ns for the
engine's own descent with the SSE2 SAT (§13.5); §13.6 says why the ≤ 15 ns target is not reachable this way.**

**Status: not merged, code dropped.** The module, its launcher option, verifier, fixture, host test and x87-audit roots
named below exist only in commit `c08d750d` (branch `worktree-agent-a9f423b89cda56df8`); the contract in §13.1 and the
measurements stay valid and are what §11.6's memo builds on.

### 13.1 Contract **[s]**

`int __cdecl 0x004e2530(BV* a, BV* b, float* R, float* T, float s)`: five stack arguments, caller pops `0x14`, locals
`sub esp,0x40`, EBX/EBP/ESI/EDI pushed and popped, **four** plain `ret` exits (§12.3 said three), EAX/ECX/EDX and the
x87 stack (empty in, empty out) are what it may change. Reached by its own four recursive calls (`0x004e264c`,
`0x004e269f`, `0x004e2705`, `0x004e2767`) and by **one** external call, `0x004e2956` in the query `0x004e2780`; no abs32
reference to the entry exists in the file. Per entry, in this order:

1. `contacts = [0x0060854c]`. If `[0x00596934] != 0 && contacts > 0` (signed) return 0. If `[0x00608534] & 4` and
   `contacts >= [0x00608538]` return 0. Both are re-read at **every** entry; they are the only early termination there
   is (item 6).
2. `++[0x00608544]` (the visit counter; entries that returned in 1 are not counted).
3. `bs[i] = float32(b.d[i] * s)`, then `SAT(ESI = R, EDI = bs, T, &a.d)`; non-zero returns 0.
4. `a` is a leaf iff `[a+0x3c] == 0 && [a+0x40] == 0`, likewise `b`. Both leaves: `return 0x004e2190(ESI = a, EAX = b)`.
5. Split `a` when `b` is a leaf, or when neither is and `b.d[0] < a.d[0]` — the compare is on the **unscaled** `b.d[0]`
   (`fld [b+0x30]; fcomp [a+0x30]; test ah,5; jnp`), and an unordered or equal compare splits `b`. The child at `+0x40`
   is entered first, then `+0x3c`.
   - split `b`, child `c`: `R' = R × c.R` (`0x004e1ff0`), `T' = (R · c.centre) · s + T` (`0x004e20d0`), recurse `(a, c, R', T', s)`.
   - split `a`, child `c`: `R' = c.Rᵀ × R` (`0x004dfd80`), `v = float32(T − c.centre)`, `T' = c.Rᵀ · v` (`0x004dfe60`, no
     scale), recurse `(c, b, R', T', s)`.
   Each `R'`/`T'` entry is three products summed in a fixed, per-entry association order and stored through
   `fstp dword`; the orders are transcribed in `collide_descent_sse2_core.h` (`mul_rr`, `mul_rc_scaled`, `mul_trr`,
   `mul_trv`). The second child's transform is composed from the parent's unchanged `R`/`T` after the first returns.
6. A non-zero result of the first child is returned at once, the second child's result is returned as is. **But
   `0x004e2190` returns 0 on both of its exits** (`xor eax,eax` before each `ret`), so the descent always returns 0 and
   those branches are dead: a contact stops the descent only through item 1.

`0x004e2190` reads ESI and EAX of its caller's registers and nothing else (first use of EBX is its own `push`, of ECX
and EDX a `lea`, EBP is never named), saves EBX/EDI, and is where every output is written: `++[0x00608548]` per
triangle test, `++[0x0060854c]` per contact, the contact triangles' centroids to `0x0060851c..0x00608530` (overwritten
per contact, so **order decides which contact survives**), and on the `flags & 0xc` path the running minimum through
`[0x00608540]`. The caps named in the brief live here and in item 1: `0x0048a9a5` passes flags 2 / cap 1 (cap unused,
first-contact flag set), `0x0048a69e` flags `0xc` / cap 8. The visit counter `0x00608544` has eight references in the
image, one `add` and seven `mov [m],x` resets; nothing reads it.

The query `0x004e2780(ECX = model b, EDX = model a; R1, T1, s1, R2, T2, s2, mode)` composes the root `R`/`T`, stores
`[0x00596934] = (mode == 2)`, zeroes the three counters at `0x004e2947` and calls the descent with the x87 stack empty
(`fstp [esp]` is the last x87 instruction before the pushes).

### 13.2 Hook

`engine_patch::claim_call` on **`0x004e2956`** (`e8 d5 fb ff ff`), rel32 only, windows `33 c0 d9 1c 24 51 52 53 57 a3 44
85 60 00 a3 48 85 60 00 a3 4c 85 60 00` before and `83 c4 14 5f 5e 5d 5b 81 c4 98 00 00 00 c3` after. The entry
`0x004e2530` is the census's site 7, which is why the call site was chosen: the two claims are disjoint and either can
be installed or restored first. With the hook in, the engine's body is never entered, so the SAT module's claim at
`0x004e25a3` goes quiet and the census's entry stub no longer fires; the replacement adds its entry count to
`x3m_collide_narrow_counters[1]` itself (same number, fixture-checked), and site 8 still counts inside the engine's
leaf. `initialize()` additionally hashes the three bodies it depends on — the descent (with the census's five entry
bytes and the SAT rel32 zeroed, because those modules initialise first), the leaf (entry bytes zeroed) and the four
helpers — and refuses with `body_mismatch`; other refusals `executable_mismatch`, `bytes_mismatch`, `target_mismatch`,
`late_claim`, all leaving vanilla in place. Verifier: `verify_collide_descent_site.py`, 26 checks (whole call, exact
inbound set, nothing into `+1..+4`, the three hashes, the exact list of 14 calls inside the descent, plain-cdecl shape,
leaf register inputs and zero return, helpers x87-balanced and stack empty at the site, flags dead at the return, no
XMM on the path, visit counter never read, disjoint from 127 other claims, source constants).

### 13.3 Replacement

`collide_descent_sse2_core.h` (portable, templated on the arithmetic type and an `Env`) and
`collide_descent_sse2.{h,cpp}`. Iterative: the pair in hand plus a stack of pending `+0x3c` children (56 B each, 64 per
frame); when a frame is full the first child's subtree runs in a nested frame and the loop continues with the second,
so depth is unbounded and stack use per level is below the engine's `0x6c` B (fixture: 45 KB against 75 KB on the
deepest pairs). Both children's transforms are composed when the parent descends — they are pure functions of the
parent's `R`/`T`, so composing the second one early changes nothing observable. Head checks are re-read at every entry
exactly as in item 1, the visit counter is added to `0x00608544` before every leaf call and at the end (so the leaf
sees the engine's value; nothing reads it anyway), the SAT is `collide_sat_sse2::core::obb_disjoint` unchanged (NaN
separates, 2⁻²⁰ margin), the leaf is called through a 13-instruction wrapper (`ESI = a`, `EAX = b`,
`call 0x004e2190`, EBX/EBP/ESI/EDI kept), a non-zero leaf result is returned through every level as the engine would.
The thunk keeps ECX/EDX (the original leaves them alone when the root pair is pruned) and re-pushes the five arguments
for a cdecl C body. MXCSR as in §12.8: read once per query, untouched when its control bits are the default, otherwise
`0x1f80` for the body, the caller's value around every leaf call and at the end. No x87/MMX instruction in the module
(build audit; `check_no_x87.py` roots added), LastError never touched, no lock, no allocation, no log on the path.

**Arithmetic.** `double`, same operands and association order, the engine's float32 stores reproduced (each `R'`/`T'`
entry, `bs`, and `v`). A product of two float32 values is exact in double, so with the x87 computing in double (FEX
reduced precision) every composed transform is **bit-identical**, not merely close — measured, §13.4. On native
Windows the x87 runs at 24- or 64-bit precision control, so the last bit of a transform can differ there; the fixture's
float instantiation quantifies what that does.

### 13.4 Fixture **[m]**

`collide_descent_sse2_fixture.cpp`, 49 checks. The reference is the engine's **own bytes run in place**: the fixture
image is linked at `0x00400000` with a zero-filled section over `0x004d0000..0x0060ffff` and copies in (untracked,
hash-pinned, extracted by `build_collide_descent_sse2.py`) the query, descent, leaf, helpers, SAT and `reps`; only the
SAT's 24 `fabs` calls are re-pointed. So production code runs unmodified with its real addresses, through the real
call site and `initialize()`. The leaf's entry is a five-byte jump to a recorder that decides contacts by hash, the
SAT call is pointed at a recorder of `(a, b, R, T, bs)`, the entry at a counter. 126,150 tree pairs (25,495,277
visits, 6,892,149 leaf calls, 350,959 contacts) through `0x004e2780` per pass:

| Category | Pairs | Notes |
| --- | --- | --- |
| realistic / overlap without contact | 40,000 / 6,000 | balanced and ragged trees, 8–1,024 leaves, flags 2 |
| first contact / cap 8 + distance / cap 1 / all contacts | 15,000 / 15,000 / 10,000 / 10,000 | flags 2, `0xc`, 4, 0 |
| deep unbalanced | 150 | concentric chains, 120–400 levels each: nested frames |
| hostile | 25,000 | NaN, ±inf, ±FLT_MAX, denormals, ±1e30, ±0 in a node field or a query input |
| leaf returns non-zero | 5,000 | not an engine behaviour; the propagation rule |

| Comparison against engine + SSE2 SAT | Differences |
| --- | --- |
| core, recorded: visit sequence **with composed `R`/`T`/`bs` bits**, leaf calls (arguments, visit and contact counter at the call), result, contacts, last contact, visit counter, entry count | **0 / 0 / 0 / 0 / 0** |
| production thunk via `initialize()`: leaf calls, outputs, census entry count | **0 / 0 / 0** |
| engine with its own x87 SAT (what `--collide-sat-sse2`'s margin changes, §12.8) | 15 pairs visit more, 7 of them reach extra leaf pairs (5 + 5 of those in the 150 concentric chains) |
| core in `float` (proxy for 24-bit x87 precision control) | 6 pairs visit differently, 2 with different leaf calls |

Also checked: every refusal leaves the site bytes untouched; `initialize()` succeeds with the SAT module installed and
an entry claim in place; ECX/EDX/EBX/EBP/ESI/EDI, x87 control word and empty tag word, MXCSR and LastError across a
direct call under MXCSR `0x1f80`, `0x5f80`, `0x9fc0` (the leaf saw the caller's MXCSR each time, results identical);
`shutdown()` restores the five bytes; `late_claim` after the window closes.

### 13.5 Cost **[m]**

One deeply overlapping pair, 212,707 visits per query (106,353 descend, 91,448 pruned by the SAT, 14,906 leaf pairs),
fastest of 24 queries, FEX; diagnostic timings, not game FPS.

| Configuration | ns / visit |
| --- | --- |
| engine as shipped | 114.8 |
| engine + `--collide-sat-sse2` (run 45's state) | 31.4 |
| `--collide-descent-sse2` | **31.0** |

Pieces in isolation: full 15-axis SAT 20.4 ns, one child's `R'`+`T'` 8.0–9.2 ns, a pending-entry copy 0.24 ns.

### 13.6 Why there is no gain, and what was tried

§12.8's premise was that the remaining time is x87 code, slow under FEX. It is not: with `FEX_X87REDUCEDPRECISION=1`
the x87 helpers run as host double arithmetic and cost what the same sums cost in scalar SSE2: the replacement's two
`compose` calls come to ≈ 17 ns per descending visit **[m]**, and since the per-visit totals are equal within noise
the engine's two helper pairs cost about the same **[i]**. What is left is dominated by the SAT itself, ≈ 13 ns
averaged over the mix above **[i]** (20.4 ns for the half that overlaps, the early exits for the rest), which run 45
already replaced. Tried in this session and
removed because the fixture measured no benefit (verdicts stayed identical in both): a packed-double SAT evaluating two
axes per operation (19.5 ns against 20.8 ns scalar in isolation, no change per visit), and composing only row 0 of
`R'` and `T'[0]` until SAT axis 1 had been tried (10 % of entries settled that way, no change per visit). The same
query with trees that fit L1 gives the same ns per visit, so the fixture number is compute, not memory. The flight
figure of 66 ns per visit is twice the fixture's 31 ns for the same configuration; that gap is outside this function
(candidates: the per-mesh-pair query set-up, cache behaviour of the real station tree, census brackets) and is the
thing to measure next. On native Windows x87 is not emulated, so no gain is expected there either **[i]**.

## 14. `--collide-memo`: the temporal no-contact memo (fix 1 of §11.6), 2026-09-19

**Status (2026-09-19): launcher default on every modded launch**, with `--no-collide-memo` as the off switch
(refused together with `--collide-memo-verify`); `--vanilla` forwards nothing unless the option is given; the DLL's
own default stays off. Flights and the running-minimum rule they led to are in §14.6.

Flight evidence (run150, census): `memo_would_hit_permille=49` of pairs but `memo_visits_permille=828` of visits,
`memo_unsafe_sum=0`, `memo_visits_differ_sum=0`. Method as in §13; nothing launched. Marks as in §12.

### 14.1 Where the memo sits, and why not at the object pair

The census's `unchanged` is an object-pair predicate over the *root* node. The object-pair routine `0x0048a890` reads
far more than that: it walks both part trees, per part the saved position, a cached radius, `[node+0x12c]`,
`[node+0x140]` through a hash lookup, and every part's own transform. A key at that level would have to hold both part
trees. One level down the query is closed: **`0x0047f1b0`** (two callers, `0x0048a9a5` with flags 2 / cap 1 / EDI = 0
and `0x0048a69e` with flags `0xc` / cap 8 / EDI = a local float) reads of each node exactly 13 words — `+0x70` (scale;
**not** in the census's hash), `+0xb0/b4/b8`, and the nine matrix words `+0xc0..0xe8` — turns them into 26 floats and
calls **`0x004e29f0`**, its only call and that function's only caller **[s]**. The memo is on that call,
**`0x0047f329`**: the key is then the collider's literal input, part animation is covered because every part pair is
its own query, and the census's sites (5, 6 above it; 7, 8 below) are untouched.

At the site: ECX = flags, EAX = cap, nine stack words `&R1, &T1, s1, model a, &R2, &T2, s2, model b, tolerance`, and
a **tenth** the callee reads at `[esp+0x2c]`. It is a pushed argument, not a saved register: EDI is a register
argument of `0x0047f1b0` and `push edi` at `0x0047f1d7` passes it on (the function's exit `pop esi` then takes that
slot). `0x004e29f0` stores it in `[0x00608540]` — the running-minimum pointer the leaf compares against and writes
through in distance mode. `ftol` `0x0052b5d0` reads one global, the process-constant SSE2 flag `[0x006619ec]`
(`cvttsd2si` when set, otherwise an x87 path under the control word), and writes none. `0x004e29f0` stores flags, cap,
`ftol(tolerance)` (`0x0052b5d0`) and that pointer, and calls `0x004e2780(…, mode = flags & ~0xc)`.

### 14.2 Inputs and outputs, enumerated **[s]**

The collider range `0x004e2190..0x004e38ad` names these globals and no others (verifier, `collider_globals_enumerated`):
the constants `0x00565600/04/08`; the root block `0x00596928..0x0059695f`; the state block `0x0060851c..0x0060854f`;
the one-time pair `0x00608d98/9c`. It calls out only to `fabs`, `ftol` and six helpers that name no global.

| | |
| --- | --- |
| **Key** (81 words, compared bit for bit) | 26 transform floats; tolerance; flags; cap; EDI null-ness and the float behind it; both model pointers; both 24-byte model headers (`[+0]` root box, `[+0x14] == 3`); both `0x48`-byte root boxes (content stamp) |
| **Lifetime** | an entry is live only if stored or hit in this frame or the previous one (frame = `Present`); the whole table is also dropped after a device `Reset`, after 100,000 queries without a `Present` (≈ 10³ frames' worth: a simulation running without rendering), and when a `Present` on the owner thread finds a query still marked in flight (`stuck_busy`: an unwind went past the thunk). Invariant: no entry outlives one rendered frame, a Reset or a render-less stretch. No node or object pointer is in the key, so object reuse cannot alias; a model address reused for other content changes header or root box |
| **Threads** | the memo's state belongs to **one** thread, the first through the thunk (the game's main loop, which is also the Present thread). `lookup()` compares `GetCurrentThreadId()` before it reads anything and sets `busy` before the lookup proper; a query from any other thread, and a re-entered one, goes straight to the engine and is counted (`foreign_thread`, `reentered`) |
| **Written by a no-contact query, replayed on a hit** | `0x00608534` flags, `…38` cap, `…3c` tolerance integer, `…40` EDI, `…44` node pairs, `…48` triangle tests, `…4c` = 0, and all 14 words of the root block (T, first-contact flag, scale, R) — each a function of the key, stored with the entry |
| **Written on a contact only, left alone** | contact record `0x0060851c..0x00608533`; the float behind EDI (`fst [ecx]` at `0x004e248d` is on the counted-contact path) |
| **Return** | the caller does `xor eax,eax; add esp,0x28; cmp [0x0060854c],eax; setne al`: nothing else of the query is read; on a hit EAX = 0, ECX/EDX/EFLAGS dead, x87 empty as the engine leaves it |
| **Not memoed** | a contact (never stored); a model not in state 3 (the engine returns early; passed through, `ineligible`); a re-entered thunk |

Assumed, not in the key **[i]**: model data below the root box is immutable once built (verify mode catches a
violation, §14.4); the x87 control word is constant (D3D9 sets it once).

### 14.3 Module

`collide_memo_core.h` (key, 4-way × 256-set static table, expiry, eviction of the entry touched longest ago;
host-tested against a dictionary model) and `collide_memo.{h,cpp}`. `claim_call` on `0x0047f329`; `initialize()` hashes
ten bodies (caller, `0x004e29f0`, query, descent and leaf with the census/SAT holes zeroed, triangle test, the SAT
`0x004e3280`, the matrix helpers `0x004e1ff0`–`0x004e2186`, the vector helpers `0x004dfd80`–`0x004dfee2`, `ftol`) and
refuses with `body_mismatch`; the SAT module's patch is a call-site rel32 inside the descent, already a hole, so the
two coexist in either order. The 28-instruction thunk: `lookup(flags, cap, &args)`; hit → `add esp,8; xor eax,eax; ret`; miss
→ take the engine's return address off, `call 0x004e29f0` on the engine's exact stack (it reads the tenth word), then
`store()` with EAX/ECX/EDX kept, `jmp` back; `lookup()` = 2 (another thread, or re-entered) jumps to the engine with
nothing of the memo touched. The
handlers compare and copy words: no x87/MMX, no floating-point arithmetic, MXCSR never read, LastError never touched
(build audit; three roots added to `check_no_x87.py`). `--collide-memo-verify` (implies the memo) skips nothing: a
would-be hit runs the engine and `store()` compares contact, counters, tolerance integer and root block
(`verified` / `verify_mismatches`, a mismatch drops the entry). Output: one `collide_memo` line per 300 frames —
`queries hits misses stored contacts ineligible evictions skipped_visits skipped_triangles verified verify_mismatches
foreign_thread reentered clears stuck_busy` (LastError kept around the log call).
The census keeps counting pairs and mesh pairs; its node-pair and triangle counts cover only the queries that ran.

### 14.4 Fixture **[m]**

`collide_memo_fixture.cpp`, 55 checks: the engine's bytes in place at their own addresses (image at `0x00340000`, a
zero-filled section over `0x00400000..0x0066ffff`; **no byte changed**, real leaf and triangle tests), a second copy of
`0x0047f1b0` whose call goes straight to `0x004e29f0` as the un-memoed engine. Every query runs through both from the
same global state; result, EBX/EBP/ESI/EDI, x87 control and tag words, MXCSR, both global blocks, the running minimum
and LastError must agree (ECX/EDX too unless it was a hit). **58,250 queries, 44,296 hits, 3,512 contacts: 0
differences, 0 stale hits, 0 contacts answered from the memo.**

| Scenario | Queries / hits / contacts | What it shows |
| --- | --- | --- |
| static | 528 / 427 / 40 | nothing hits in frame 1, every no-contact pair hits afterwards, contacts recomputed |
| one step | 59 / 32 / 0 | ±1 in any of the 26 node words misses once; words the query never reads do not matter |
| approach | 422 / 60 / 2 | close in to a contact, park two steps back (59 of 60 parked frames answered), contact again, leave turning |
| modes | 50 / 25 / 0 | flags 2 / cap 1, `0xc` / 8, running minima, tolerance: separate entries; a changed minimum misses |
| addresses | 9 / 2 / 0 | same inputs at other node/body addresses hit; reversed pair, other model, reused model address, root box one float step, model state ≠ 3 miss |
| expiry / overflow | 4 / 2, 9,000 / 1,538 | one untouched frame expires; 3,000 live keys per frame evict and never answer wrongly |
| random | 48,000 / 42,207 / 3,470 | twelve objects that stay, creep, jump, turn, rescale, change model |
| guards | 8 / 3 / 0 | null model pointers never dereferenced; 50 queries from another thread all run in the engine; re-entered lookup; a query left in flight re-armed by the next Present with the table dropped; device Reset; 100,001 queries without a Present drop the table once |
| verify mode | 170 / 0 / 0 | nothing skipped, 140 confirmed; boxes below the root changed behind the key's back → mismatch reported |

Cost (harness included; diagnostic): one contact-free pair of 21,643 node pairs 982 µs run, **99 ns** answered. A tiny
query (root boxes apart, one node pair): 96 ns in the bare engine, 220 ns as a miss with a new key and a store, 98 ns
answered — **a miss costs 123 ns** (key build with both root boxes, hash, 4-way compare, store), about 0.01 ms per frame
at 10² queries, below the ~150 ns at which a two-stage key would be worth its complexity.
Expected in flight **[i]**: `memo_visits_permille=828` ⇒ the collide phase's narrow part falls by about that share
while the player holds still; nothing while the hot pair moves.

### 14.5 One flight

`--collide-memo-verify --collide-narrow-census --loop-phases` at the station: `verify_mismatches` must be 0 and
`verified` large. Then `--collide-memo --collide-sat-sse2 --collide-narrow-census --loop-phases`: compare the `collide`
phase and `skipped_visits` against run 45 A.

### 14.6 Flights 155/156 and the running-minimum rule (2026-09-19)

Run 155, verify mode **[m]**: 2,009,448 queries, 808,408 would-be hits all confirmed, `verify_mismatches=0`;
`foreign_thread`, `reentered`, `stuck_busy`, `clears` all 0. Run 156, memo on, the 24 fps spot (now ≈ 43 fps) **[m]**:
per 300 frames ≈ 56,000 queries (≈ 187 per frame), ≈ 16,700 hits (30 %), contacts 0, `skipped_visits` ≈ 42.4 M —
≈ 141k of ≈ 225k node pairs per frame, 62 % against the census's 83 %. About 130 queries per frame still ran, with
≈ 85k node pairs between them, no contact and no triangle test.

**The rule [s].** Key word 30 is the float behind the running-minimum pointer, a local of the `0x0048a69e` caller computed from `[node+0x70]`, `[node+0x188]` and its own arguments (`0x0048a673`–`0x0048a693`), so it can change from frame to frame for a part pair whose transforms do not **[i: that this is what makes run 156's misses is for the next flight's `miss_min_value` / `min_relaxed_hits` to show]**. The collider reads
that pointer in exactly one instruction, `mov ecx,[0x00608540]` at `0x004e246e` inside the leaf, after a triangle
pair intersected; and every entry into the leaf runs `add [0x00608548],1` at `0x004e22a5` first, with no branch, call
or return between the leaf's entry and that instruction (verifier: `minimum_read_in_the_leaf_only`). A stored run
whose triangle-test count is 0 therefore never read the float, and such an entry now also answers a query that
differs from it in word 30 alone. The pointer's null-ness, tolerance, flags and cap stay in the key; the count was
already stored with every entry, so nothing new is recorded. An entry that did reach a leaf still needs the exact
float. Hypothesis 2 of the brief, keying on the relative transform, is **not** done: the root transform is composed
in rounded float32 steps from both absolute transforms, so equal relative poses do not give equal bits.

**Miss classes.** On a miss the memo looks up the last entry stored for the same two models with the same transform
of `a` (failing that, of `b`; two 1,024-slot side indices) and names the first differing key group:
`none_found`, `xform_a`, `xform_b`, `scale`, `mode` (tolerance, flags, cap, pointer null-ness), `models` (headers,
root boxes), `min_value`, `expired`. The `collide_memo` row carries `miss_<class>` and `miss_<class>_visits` (node
pairs those misses went on to cost) and `min_relaxed_hits`. After this change `miss_min_value` can only be runs that
reached a leaf; if hypothesis 1 holds in flight it collapses and `min_relaxed_hits` takes its place, and what is left
under `xform_a`/`xform_b` is the share that really moves.

**Fixture [m]**, now 59 checks, 58,410 queries, 37,476 hits, 3,779 contacts, 0 differences, 0 stale hits: a new
`running_minimum` scenario — no leaf reached: 39 of 40 frames answered although the minimum changed every frame;
leaf reached without contact: a changed minimum always misses (0 of 40), the same minimum hits (40); a contact that
the minimum filters (tolerance 25: 11 frames filtered, 49 reported) is never answered from the memo. The random scene
now draws a fresh minimum for half of its distance-mode queries: 18,622 relaxed hits in the run, all equal to the
un-memoed engine. Every miss is classified (`xform_b` 25,046, `min_value` 7,218, `none_found` 2,982, …). A miss now
costs +177 ns on a tiny query (was +123 ns; the classification), ≈ 0.02 ms per frame at 130 misses.

### 14.7 `--collide-memo-advance`: conservative advancement for the moving case (2026-09-20)

**Status: not flown, not merged, code dropped.** The option, its SAT gap reporting (the two globals written per prune), launcher
switch, fixture scenarios and host tests exist only in commit `8a374dc5` (branch `worktree-agent-a9f423b89cda56df8`).
The three facts settled from the bytes below stay as checks of `verify_collide_memo_site.py` (33 checks), since they
hold for the memo as shipped: a contact needs a triangle intersection, the box fit is half the span, the replayed globals are private.

Flights 163/164 **[m]**: verify 0 mismatches; standing still 65 fps, slightly moving 45 fps; while moving
`miss_xform_b` ≈ 30,000 queries per 300 frames carrying 25–48 M node pairs, `miss_min_value = 0` and
`min_relaxed_hits = 0` in every window — hypothesis 1 of §14.6 was wrong (the rule is harmless). What is left is a
really moving `b` against the station's tree: ~10⁵ node pairs per frame, no triangle test, no contact.

**Headline [m]: the accelerator is sound in the fixture (0 differences, 0 contacts answered, verify 452 / 0) and useless
for that case.** It answers when the objects are well apart, where the engine's query costs one node pair anyway (25,035
answers saved 26,803 node pairs). Deep among the boxes (1,037 node pairs, no leaf, no contact) a creep of **one unit** —
0.25 % of the large model's half-extent — per frame was answered 0 times in 40 frames: the smallest of several hundred
pruning gaps is below two units plus the margin. With 5·10⁴ prunes in the flight case it will be smaller still.
Default off, and not worth a flight as it stands; §14.8 names what would be.

**Settled from the bytes [s]** (each is a check of `verify_collide_memo_site.py`):
- *What a contact is.* In every mode the leaf calls the triangle test `0x004e2ba0` and leaves at once when it returns 0
  (`0x004e2343 test eax,eax; je 0x004e2526`); flags, tolerance (`0x0060853c`) and the running minimum are read only
  after that, as filters that can only remove contacts. The triangle test `0x004e2a50..0x004e327f` names no global. So
  "no contact" follows from "no triangle pair intersects", i.e. mesh distance > 0, in flags 2 and flags `0xc` alike;
  the tolerance plays no part (`contact_needs_an_intersection`).
- *Boxes enclose their triangles.* Both box fits (`0x004e1454..0x004e1472`, `0x004e1fbb..0x004e1fdf`) store
  `d = (max − min) · [0x00565508]` and the centre from `(min + max) · [0x00565508]`, the constant being exactly 0.5: the
  tight min/max box, no shrink (float rounding only). That the min/max run over all of a node's triangles is RAPID's
  `fit_to_tris`/`split_recurse`; the loop itself was not re-read **[i]**.
- *What an answer may leave behind.* Outside the collider the image references, of everything a no-contact query
  writes, only the contact counter (`0x0047f335`, plus four one-time initialisers of it and of the node-pair counter).
  The root block, the mode words and both other counters are private to the collider, which rewrites all of them
  before reading any (`replayed_globals_private`). An advance answer therefore replays the *stored* pose's node-pair
  count and root block: unobservable.
- *The relative pose.* `0x004e2780` composes `R = Raᵀ·Rb`, `T = Raᵀ·(Tb − Ta)·(1/s1)`, `s = s2/s1` in float32 with the
  root boxes folded in; the bound uses `M = R1ᵀR2·(s2/s1)`, `T = R1ᵀ(T2 − T1)/s1` in double from the same 26 floats.
  Which of the two scales is inverted was read from the listing's argument offsets, not confirmed numerically **[i]**;
  the fixture's run at scales 2 and 3 ended in a contact at the engine's frame with 0 differences.

**The argument.** A run that reached no leaf (`triangles == 0`) and found no contact visited `1 + 2·descents` node
pairs and pruned exactly `(pairs + 1)/2` of them; every triangle pair lies under one pruned box pair. The SSE2 SAT
(`obb_disjoint_gap`, same decisions: SAT fixture 2,560,000 pairs, 0 violations, +0.1 ns) reports `t − (ra + rb)` of each
pruning axis; the axes are unit vectors or cross products no longer than 1, and `Bf = |R| + 1e-6` only lowers the value,
so the minimum `g` over the run is a lower bound of the distance between the meshes, in `a`'s model units. The memo
accepts `g` only when it saw exactly `(pairs + 1)/2` prunes. For a later query equal in models, stamps, scales,
tolerance, flags, cap and pointer null-ness, with one object's transform changed, any point `y` of `b` moves relative
to `a` by `(M' − M)y + (T' − T)`, so by at most `D = ‖M' − M‖_F·ρ_b + |T' − T|`, `ρ_b = 1.001(|c| + |d|)` of `b`'s root
box. Answered when `D + margin < g/2`, `margin = 1e-5·(|T| + |T'| + ρ_a + s·ρ_b)`: float32 composition over a few dozen
levels errs near 1e-6 of those magnitudes, and the factor 2 also covers box axes that are unit to ~1e-6. Always
measured against the stored pose, never incrementally; re-stored when the test fails. Refused: non-finite values,
scales not in (0, 1e30), a matrix that preserves no lengths (1e-3), MXCSR not at its default. `b` static and `a`
moving is the same formula through the index on `b`'s transform.

**Module.** `X3M_COLLIDE_MEMO_ADVANCE=1`, armed only while the SAT module is installed; the launcher refuses the option
without the memo or the SAT. Row: `advance_hits advance_skipped_visits advance_refused_gap advance_rearm
advance_verified advance_mismatches advance_gap_median_log2 advance_displacement_median_log2` (medians as power-of-two
buckets: a double through varargs would be copied with x87). Verify mode runs the engine on every advance answer; a
contact logs `collide_memo_unsound` with models, gap and displacement in thousandths. The bound is the module's only
floating-point code: SSE2 doubles, square roots by `sqrtsd`, no library call, no x87 (build audit).

**Fixture [m]**, 76 checks, 69,147 queries, 0 differences. Approaches of a ship to a solid model until the engine
reports a contact, a small random turn every frame:

| Run | Frames | Answered by advancement | Far half answered | Contact found at the engine's frame |
| --- | --- | --- | --- | --- |
| 1 unit / frame | 2,621 | 2,344 | 1,297 / 1,300 | yes |
| 3 | 829 | 740 | 432 / 434 | yes |
| 12 | 434 | 320 | 105 / 109 | the engine never sees one (steps through) |
| 60 | 87 | 42 | 10 / 22 | same |
| scales 2 and 3 | 1,613 | 1,471 | 861 / 867 | yes |
| flags `0xc`, tolerance 25 | 865 | 762 | 421 / 434 | yes |
| `a` moves | 863 | 750 | 428 / 434 | yes |
| no turn | 1,245 | 1,144 | 645 / 650 | yes |
| needle (half-length 300) turning 0.0015 rad / frame | 1,500 | 1,462 | — | none |

Hostile poses (scale 0 and −1, a zero or tripled matrix, positions at 2³¹) are never answered. Verify mode: 452
answers, all confirmed. Cost: an advance answer 142 ns against 89 ns for the one-node-pair query it replaces (harness
included), so where it fires it saves nothing either; a miss now costs +185 ns.

### 14.8 What would help the moving case (not done)

The bound fails because one number stands for 10⁴–10⁵ box pairs. The standard remedy is a **front**: keep the pruned
box pairs of the last run (the frontier of the descent) and re-test only those, descending from a pair only when it
stops being separated — generalized front tracking. It removes the descents (half the node pairs and both child
transforms each) but still runs one SAT per frontier pair, so the ceiling is roughly 2–3×, at the price of a frontier of
~5·10⁴ pairs per query to store and of re-deriving each pair's transform from the root; and it changes no answer only
if the frontier is re-validated completely each time. A per-subtree gap (advance whole subtrees of `a` that are far
from `b`, re-run the near ones) is the cheaper variant of the same idea. Neither is a small change.

### 14.9 Front tracking: feasibility measurement (2026-09-20) — not worth building

Fixture-side only (`verification/probe/collide_front_feasibility.cpp`, no production code, no engine bytes): the descent
is the C++ replica of §13 (bit-identical to `0x004e2530`, engine parity per node pair) with the SSE2 SAT. A leaf-free,
contact-free query leaves its visit tree in preorder; the next frame walks that tree: a **descended** pair gets its two
child transforms composed and no SAT, a **pruned** pair one SAT (its last separating axis first, optionally), a pruned
pair that now overlaps is descended as the engine would and spliced in, and a leaf pair anywhere abandons the front for
the full query. *Soundness:* the front covers the pair space by construction (pairs are only replaced by their
children), and every pair's transform is composed along the engine's own path, so when every front pair is disjoint the
engine, which prunes at that pair or above, reaches no leaf: 0 violations in every frame measured (each frame also ran
the full descent). Re-merging parents whose children are all disjoint was not tried.

**Measured [m]** (FEX; tree `a` 131,072 leaves / 262,143 boxes, `b` 512 leaves; the deepest leaf-free placement of 300;
`b` creeps and turns 0.002 rad per frame; medians):

| Layout of `a`'s boxes | Speed (units / frame) | Node pairs | Full query | Front, cached axis | Front, whole SAT | Axis-first hit rate | Front SATs |
| --- | --- | --- | --- | --- | --- | --- | --- |
| contiguous (19 MB, preorder) | 1 | 6,579 | 193 µs (29.3 ns / pair) | 171 µs, **1.13×** | 213 µs, 1.01× | 97.7 % | 3,493 |
| scattered over 151 MB | 1 | 14,445 | 493 µs (34.1 ns / pair) | 426 µs, **1.16×** | 460 µs, 1.04× | 97.9 % | 7,418 |
| scattered | 5 | 14,435 | 527 µs | 471 µs, 1.12× | 518 µs, 0.97× | 90.4 % | 7,725 |
| scattered | 20 | 12,477 | 457 µs | 482 µs, **0.95×** | 541 µs, 0.84× | 86.7 % | 7,946 |

No front was ever abandoned before the scene itself reached a leaf, but the synthetic scenes stay leaf-free for only
2–21 frames and reach 6–14 k node pairs, not the flight's 10⁵; the costs are per node pair, so the ratios carry over,
the absolute sizes do not.

**Why so little [s+m].** Front tracking removes exactly the SATs on descended pairs (half the pairs, 20 ns each), a
ceiling of ≈ 1.47× on a 31 ns pair. It cannot remove the two child transforms per descended pair (8–9 ns each, §13.5):
a pair's `R`/`T` is composed down the engine's path in rounded float32 steps, so exactness needs the whole chain again
every frame. The front's own traffic (12-byte records, ~1.2 MB per frame at 10⁵ pairs) and the splice eat most of the
rest. Without the cached axis there is no gain at all; the cached axis hits 98 % at 1 unit per frame and 87 % at 20.

**Projection for the flight case [i]:** ≈ 7 ms per frame × (1 − 1/1.15) ≈ **0.9 ms** at a creep, perhaps 2 ms with a
tuned iterative walk (1.4×), nothing or a loss at 20 units per frame. **Recommendation: do not build it.**

**The 66 ns (flight) against 31 ns (fixture) per node pair is not the boxes' cache behaviour [m]:** scattering 262,143
boxes at random over 151 MB costs 34–36 ns per pair against 29–33 ns in preorder. What doubles the flight's figure is
outside the descent loop — candidates: the ~187 queries' own set-up (`0x0048a890` part walk, `0x0047f1b0`, memo miss
path), the census brackets when on, and JIT or TLB effects of the full game process — and was not reproduced here.
A sampling profile of the collide phase in flight would settle where the other half goes.

## Reproduce

```sh
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3DecompileFunctions.java /tmp/out/collide.c 0045d250 0045cab0
```

Instruction listings and switch tables were taken with a local
`X3DumpRange.java` / `X3RefsTo.java` (address-range listing, references-to) kept
in the session scratchpad, and cross-checked against the on-disk bytes with the
PE section map; the byte strings in §6 and §7 are the check.

§11 used no Ghidra project. Listings came straight off the bottle EXE (the
image is non-relocatable, so file addresses are virtual addresses):

```sh
objdump -d --no-show-raw-insn --x86-asm-syntax=intel \
  --start-address=0x4e2530 --stop-address=0x4e2780 \
  "$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe"
```

Run-log figures came from `grep`/`awk` over
`/tmp/x3-bottleX3-run{129,133,134}/session-*.log` (116 MB, 26 MB, 27 MB — never
read whole): `collide_census` lines for the pair counts, `loop_phases` lines for
`collide_p50_us`/`post_p50_us`/`simulate_p50_us`, and
`grep -c profile_ …run129/session-20260918-090339-216.log` → 0 for §11.1.
