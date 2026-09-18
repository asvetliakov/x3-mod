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
