# Camera and lighting register evidence

Read from CTAB metadata in the **runtime-captured** shader bytes using
`tools/analysis/shader_constants.py`. Full metadata is in
`verification/results/shader-registers.json`. No binary shader assets are stored
in this repository. Names/register ranges alone do not prove engine matrix layout,
handedness, camera jitter behavior or light units.

## Common material vertex shaders

Observed hashes include `53a0a641107ed76c`, `37c34a7478544c14`,
`4944d81dfe531b37`, `494fe349b8bc12ec`, `b0602757fce6e870`,
`c30104cb0efb6675`, and `167eb2d5629ab9d3`.

| Parameter | Register range | Metadata |
| --- | --- | --- |
| g_LightPoint | float c0–c23 | Structure array, 24 registers total |
| g_mWorldViewProjection | float c24–c27 | Matrix, parameter class 3 |
| g_mWorld | float c28–c30 | Matrix, 3 registers |
| g_mWorldIT | float c31–c33 | Matrix, 3 registers |
| g_mViewInverse | float c34–c36 | Matrix, 3 registers |
| g_nNumLightPoint | integer i0 | Separate integer register namespace |

The historical version 1 game captures omit the integer count. Capture version 2
now records the separate integer and boolean namespaces; its standalone fixture
verifies live stateblock-restored values and explicit zeros. Do not mistake i0 for
float c0. A new version 2 game capture is needed to inspect the actual light count.

Nested CTAB metadata describes `g_LightPoint` as eight structures, each with
`pos` float3, `color` float3 and `atten` float4, using three float4 registers per
entry (24 total). These are compiler names and layout evidence, not validated
physical units, color space or an attenuation equation.

## Other camera paths

- Particle VS `36f98d151fd6b0c6`: `g_mView` c0–c3, `g_mProj` c4–c7.
- Shared GUI/nebula VS `7b6393fe2d3e1d85`: WVP c0–c3. This shared shader reinforces
  that shader hash alone cannot decide which draws receive temporal jitter.
- VS `be199829a9bb78db`: WVP c0–c3, world c4–c6, world inverse transpose c7–c9,
  view inverse c10–c12, directional light `LightDir_Dir0` c13.
- VS `89193868c61c3846`: view-projection c0–c3, world c4–c6, view inverse c7–c9.

## Temporal implementation gate

Capture at least two controlled frames with camera movement and stationary
geometry. Decode the relevant float bit patterns and verify multiplication/order
against known screen motion. Then separate per-object world change from camera
change, identify stable geometry identity, and locate depth before postprocessing.
Test jitter on one known scene path and confirm GUI/particles/sky requirements
individually. A single full-screen history blend without this evidence is not the
required TAA implementation.

## Sources

The local MinGW `d3dx9shader.h` defines CTAB's 28-byte header, 20-byte constant
records, 16-byte type records and register-set enums. The analysis tool validates
all offsets against the enclosing comment block. Production code does not rely
on these metadata offsets; this is an offline research tool.

See [numerical camera evidence](camera-numerics.md) for the verified cross-draw
matrix factorization and the three camera coordinate regimes in the flight logs.

## Point-light admission site

**Ratified 2026-09-16 (orchestrator, critique Q3):** root-object admission at
`0x004c27af` implemented bounded and default-off; see "Implementation" below.
Earlier ruling, 2026-09-15: no engine patch now. Note that the fill
term only exists inside converted (linear) materials, which the user no longer
uses; with original hull shading the per-node cull is native behaviour. If the
docking-module cliff is reported again under original shading, root-object
admission at the six-byte site `0x004c27af` is the preferred bounded change, not
a range widening.

Read-only Ghidra pass, 2026-09-15, on the EXE identified in
[executable.md](executable.md) (`fdbf3418…`, preferred base `0x00400000`), project
`/tmp/x3-ghidra-research X3Render`, scripts `tools/analysis/X3CameraState.java` plus
a local byte-printer. Raw listings stayed under `/private/tmp/x3-plight-re`
(untracked). No Wine command, no launch, no patch. This closes the "Open" item of
[station-material-distance.md](station-material-distance.md) run 22.

### Where the decision is made

`g_nNumLightPoint` is produced by the point-light loop inside the material
submission routine `0x004c0150`, span **`0x004c26af`–`0x004c2a34`**:

| Address | Role |
| --- | --- |
| `0x004c26af`–`0x004c26df` | Loop setup: registry `*0x00608518`, slot count `+0x6288` (set to **8** at `0x004b9bbb`), admitted counter `[ESP+0x50] = 0`, source cursor `[ESP+0x24] = descr+0xe0`, destination cursor `[ESP+0x38] = descr+0x108` |
| `0x004c26ed` | Slot index `*(0x00608518 + i*12 + 0x628c)`; negative = empty, skip |
| `0x004c2703` | Light node `*(0x00608518 + index*4 + 0x5e8c)` |
| `0x004c270e`, `0x004c2719` | Skip the node already bound as `LightDir_0`/`LightDir_1` (params 5/6, valid flags `[ESP+0x37]`/`[ESP+0x67]` set at `0x004c246a`/`0x004c2621`) |
| `0x004c272d` | `TEST [light+0x12c],0x800000` — a directional light **bypasses the range test** (`JNZ 0x004c27b7`) |
| `0x004c273d`–`0x004c2791` | `delta = node[+0xb0/b4/b8] − light[+0xb0/b4/b8]` as three 32-bit ints, `FILD`, squared and summed |
| `0x004c2794` | `CALL 0x00412440` (sqrtf; `FLD`+`FSQRT`, result left in ST0) |
| `0x004c279c` | `CALL 0x0052b5d0` (float→int, `FSTP`/`CVTTSD2SI`; **pops ST0**, so the x87 stack is empty afterwards) |
| **`0x004c27a1`–`0x004c27af`** | **The comparison** |
| `0x004c27b7`–`0x004c29e9` | Admitted: `SetVector` of `pos`, `color`, `atten` into slot `j` |
| `0x004c29eb` | `[ESP+0x50] += 1` (the admitted count) and `[ESP+0x38] += 4` |
| `0x004c2a1c`–`0x004c2a34` | `SetInt(g_nNumLightPoint handle, [ESP+0x50])` via effect vtable `+0x68` |

The comparison, with bytes:

```
004c27a1  2b 86 58 01 00 00   SUB  EAX,[ESI+0x158]   ; − light node range
004c27a7  8b 4d 0c            MOV  ECX,[EBP+0xc]     ; the submitted node (arg 2)
004c27aa  2b 41 70            SUB  EAX,[ECX+0x70]    ; − node base scale
004c27ad  85 c0               TEST EAX,EAX
004c27af  0f 8f 40 02 00 00   JG   0x004c29f5        ; reject
```

So the admitted predicate is

```
trunc(|node[+0xb0..] − light[+0xb0..]|) <= light[+0x158] + node[+0x70]
```

(`0x0052b5d0` truncates with `CVTTSD2SI`; the earlier reading "round" was
imprecise). With `T = light[+0x158] + node[+0x70]` that is `d < T + 1` on the
real distance `d`, i.e. `d² < (T+1)²`.

entirely in raw render-domain integers: **node origin distance**, not a bounding
sphere and not a projected size. The "radius" term is the submitted node's
**`+0x70` base scale**, not the scaled world radius `+0xa0`
([render-node-bounds.md](render-node-bounds.md)) and not the cached subtree radius
`+0xa4`, so per-axis scale is ignored here. The range source `+0x158` is a field of
the **light's own node**, not of the lit node; it is per light object and there is
one such value per light.

Descriptor layout used by the loop (same object as the handle cache in
[camera-state-and-frame-routine.md](camera-state-and-frame-routine.md)):
`+0xb8+4i` = `pos`, `+0xe0+4i` = `color`, `+0x108+4i` = `atten` (strings
`0x563784`, `0x555908`, `0x563788`; enumerated for **10** elements at
`0x004c1e29`–`0x004c1ea5` although the CTAB declares 8), `+0x130` =
`g_nNumLightPoint` (`0x5634e4`, cached at `0x004c1c41`). The source cursor advances
per slot and the destination cursor per admitted light, i.e. admitted lights are
compacted into consecutive shader slots. Directional lights that are *not* already
bound as `LightDir_0/1` are uploaded into the point array with `atten = (1,0,0)`
(`0x004c28f9`–`0x004c2969`); everything else uses the record's
`Attenuation0/1/2` at `rec+0x54/0x58/0x5c`.

### Quantitative check against run 22

`0x004bdbf0` derives the record from the light node (`0x004bdcb2`–`0x004bdcee`):
`Range = float(node+0x158) × contextScale(*(node+0x1c)+0x2c)`,
`Att0 = node+0x15c / 65536`, `Att1 = (node+0x160 / 65536) / Range`,
`Att2 = (node+0x164 / 65536) / Range²`.

The player-ship headlight is authored by **`0x0044ad90`** (toggle; callers
`0x00463e14`/`0x00463e20` in `0x00460630`) on the node at `*(obj+0x50)+0x268`:
RGB words `+0x150/0x152/0x154 = 0xff`, `+0x12c |= 0x50`, `+0x15c = 0x10000`,
`+0x160 = 0xa0000`, `+0x164 = 0`, **`+0x158 = 100000`** (`0x0044ae6a`). With the
session's context scale 0.01 that is `Range = 1000` world units and
`atten = (1, 10/1000, 0) = (1, 0.01, 0)` — exactly the run-22 capture value.

Substituting `R = 1000` and the run-22 per-node world scales (`node+0x70 × s`:
174–221 on the clamps, 19 536 on the body) into `d ≤ R + node+0x70` reproduces
**all 22 per-node `i0.x` outcomes** of the run-22 table, including the edge between
1148 (admitted) and 1232 (rejected) and the body admitted at 3 461; the 16 km
outpost at 81 192 is rejected in both frames. The run-22 bounds constrain `R` to
`[884, 1058)` independently of the disassembly, and 100 000 native is the only
authored constant in that window.

### Per node or per root object

Per node, and per draw: `0x004c0150` is called from exactly one site
(`0x004c5228` in `0x004c4fc0`), which is called per submitted mesh part from
`0x0047e076` (`0x0047d9c0`, render-node visit) and `0x0047e769` (`0x0047e6e0`,
deferred drain). `[EBP+0xc]` is the node argument of `0x004c4fc0`, so every part of
one node repeats the same test with the same result.

The link a root-object rule would need exists: **node `+0x18` is the parent node**,
written by the attach helper `0x00489f20` (`child[+0x18] = parent`, together with
the intrusive relink through `parent+0x0c/+0x14` and `parent+0xa4 = -1`), cleared to
0 at `0x00489dbe` (detach), and read as a parent in the cull/LOD pass `0x0047cfe0`
(it clears the node's own renderable bit from the parent's `+0x12c & 2` / `+0x14c`
and raises its LOD threshold to the parent's `+0x1d8`). A root is the node whose `+0x18` is 0. Node `+0x00`
is the sibling link, `+0x04` the back link, `+0x0c/+0x14` the child list head/tail
(`0x004885a0`), so a root walk is a bounded pointer chase.

### Hook-site ABI

Smallest whole-instruction site that can change the decision:
**`0x004c27af`, `0f 8f 40 02 00 00`, six bytes**, a `JG rel32` whose targets are
`0x004c29f5` (reject) and the fall-through `0x004c27b5` (admit).

- Nothing branches to `0x004c27a1`, `0x004c27ad`, `0x004c27af` or `0x004c27b5`
  anywhere in the `0x004c0150` listing; the only label in the neighbourhood is
  `0x004c27b7` (from `0x004c2737`). A five-byte `JMP rel32` plus one `NOP` fits the
  six bytes and splits no instruction; six `NOP`s turn the site into "always admit".
- **Dead-out at the site**: `EAX` (redefined by `MOV EAX,ESI` at `0x004c27b5` and by
  `MOV EAX,[ESP+0x5c]` at `0x004c29f5`) and `EFLAGS` (no reader before `CMP` at
  `0x004c27c8` / `0x004c2a0c`). `ECX` and `EDX` are also scratch here. A detour
  therefore has three free GPRs and does not have to reproduce the flags.
- **Live-in and must be preserved**: `ESI` (the light node), `EBX` (the effect
  wrapper), `EDI` (the parameter descriptor), `EBP` (frame: `+0xc` node, `+0x10`
  view, `+0x18/+0x1c` the two directional lights) and every `ESP`-relative local
  (`+0x24`, `+0x37`, `+0x38`, `+0x50`, `+0x58`, `+0x5c`, `+0x67`).
- **x87**: the stack is empty at this point — `0x00412440` pushes one value and
  `0x0052b5d0` pops it — so a detour may use x87 or SSE2 provided it leaves the
  stack empty. `0x004c0150` aligns `ESP` to 16 at `0x004c0153`, so an SSE2 detour
  gets an aligned stack.
- **Reentrancy / lifetime**: `0x004c0150` installs its own SEH record
  (`0x004c0156`–`0x004c0167`, handler `0x005305b1`) inside the SEH frame of
  `0x004c4fc0` (`0x004c4fc0`–`0x004c4fce`, handler `0x00530668`); an exception in a
  detour unwinds through both. The routine runs for every view submitted in a frame
  (sector, background, cockpit scene), not once per frame. The loop also writes the
  slot index into shared light state (`[light[+0x16c]+0x68] = i` at `0x004c27c5`),
  so the light records are mutated during submission and are not read-only.

The alternative, a pure range widening, needs no trampoline at all: the two 4-byte
immediates inside `0x0044ad90` — `+0x158` at VA `0x0044ae70` (inside the `MOV` at
`0x0044ae6a`) and `+0x160` at VA `0x0044ae4e` (inside the `MOV` at `0x0044ae48`) —
scaled by the same factor keep `atten` identical while extending the range. Scaling
`+0x158` alone would also soften the falloff, because `Att1` divides by `Range`.

### Cost model

Per submitted mesh part, the loop iterates `*0x00608518 + 0x6288` slots (8).
A slot with a negative index costs two loads and a branch. A live non-directional
light costs six loads, three subtracts, three `FILD`, three `FMUL`, two `FADD`, one
`FSTP` and the two helper calls (`sqrtf`, float→int), i.e. roughly thirty
instructions plus two calls. In the run-22 session only two slots were populated
(the headlight and one stale entry), so the per-draw cost is eight slot iterations
of which two are full tests — a few thousand tests per frame at the observed draw
counts, not a measurable share of submission. A root-object rule that walks `+0x18` adds a
short pointer chase per test and could be hoisted, since the result depends only on
the node argument and the light.

### Recommendation

**Do nothing in the engine for now; the ratified converted-material fill term
addresses the cause.** The per-node flip is native behaviour and the shader's
`1/(1+0.01d)` term already makes an admitted far node nearly black
(0.028 at the body's 3 461 units), so root-object admission would remove the visible
*discontinuity* rather than add light. If the discontinuity is still objectionable
after the fill term ships, prefer **root-object admission at `0x004c27af`** over a
range widening: it keeps the authored range and attenuation, it is a six-byte
whole-instruction site with `EAX`/`EFLAGS` dead-out and an empty x87 stack, and on
the run-22 geometry it admits the ten clamp nodes (root radius 19 536 ≫ 1 843)
while still rejecting the 16 km outpost (81 192 > 1 000 + its root scale). Risks:
it runs inside the hottest submission routine and under two SEH frames; it changes
lighting for every multi-node object in the game, not only the observed station; and
it depends on `+0x18` being a parent link for every node class reaching this path,
which is established from `0x00489f20`/`0x00489da0`/`0x0047cfe0` but not from a
capture. A range widening is the cheaper patch (two immediates, no trampoline) but
is the hack the station note already warned about: a headlight that never ends.

*Uncertainty*: everything above is static on `fdbf3418…` plus the existing run-22
capture table; no new capture was taken. The run-22 "world scale" column is read as
`node+0x70 × contextScale`, which matches the world-matrix row scale but was not
re-derived here. Whether `0x0044ad90`'s object is the player ship specifically is
inferred from the constants matching light 0 of run 22, not from a live read.

### Implementation

Built 2026-09-16 as the default-off `--point-light-root-admission`
(`X3M_POINT_LIGHT_ROOT_ADMISSION=1`), `src/proxy/point_light_admission.cpp` with
the portable core `point_light_admission_core.h`; ledger
[point-light-admission.md](../verification/point-light-admission.md).

- **Site write.** The six bytes at `0x004c27af` become `jmp detour; nop`
  (`e9 <detour − 0x004c27b4> 90`); both outcomes enter the detour, which reads
  the engine's `TEST` flags first: a node that passes the per-node test takes
  one `JLE`, one in-place `inc dword [fast_admit]` and the jump back to
  `0x004c27b5` (the per-frame telemetry needs that count; run 27 showed that a
  build without it cannot attribute a dark module). The write is one plain six-byte copy
  (the span straddles an 8-byte word, so `write_code` cannot use `cmpxchg8b`),
  made inside the `engine_patch` install window after `executable_verified()`,
  the 28-byte window `0x004c27a1..0x004c27bc` and the reject target's first
  instruction (`8b 44 24 5c`) are byte-verified, with this DLL pinned; read-back
  compare, protection restored, rollback on any failure; `shutdown()` puts the
  original bytes back on a dynamic unload only.
- **Detour** (43 bytes in the patch arena): `jle counted; push eax; push esi;
  push [ebp+0xc]; call handler; add esp,12; test eax,eax; jnz 0x004c27b5;
  jmp 0x004c29f5; counted: inc dword [fast_admit]; jmp 0x004c27b5` (`EAX` is the
  engine's remainder `d − range − node scale`, passed so the sample can report
  the node's distance). It
  relies on the site facts above: `EAX`/`EFLAGS` dead-out, `ECX`/`EDX` scratch,
  the cdecl handler preserving `EBX`/`ESI`/`EDI`/`EBP`, the ESP locals above the
  pushes, and the empty x87 stack (the handler unit is built without SSE/MMX
  and contains no floating point; `check_no_x87.py` walks it).
- **Handler** `x3m_point_light_root_admits(node, light, remainder)`: saves LastError, walks
  `node+0x18` through `engine_memory::read` (bounds-checked, at most 8 reads;
  null ends the walk; a link back to the node or to itself, an unreadable link
  or an exhausted bound fail closed), then applies the same predicate to the
  root: `|root+0xb0.. − light+0xb0..|² ≤ (light+0x158 + root+0x70)²` in 64-bit
  integers with the engine's 32-bit wrapped deltas (no sqrt, no float). The
  handler's rule is exactly `d_root² ≤ T_root²` with
  `T_root = light[+0x158] + root[+0x70]` (reject when `T_root < 0`); the engine's
  per-node rule is `d² < (T+1)²`, so for the same inputs the handler is stricter
  by under one raw unit (it rejects `T ≤ d < T+1`, which the engine admits) and
  never looser. A node that is itself a root keeps the native rejection without
  re-evaluation. Nine outcome counters are kept for the summary; nothing is
  logged on the hot path.
- **Memo.** The reject path runs per submitted mesh part per populated light
  slot per view, and every part of one node repeats the same (node, light)
  test, so the handler memoises the root verdict in a 256-entry direct-mapped
  table keyed by (node pointer, light pointer) and valid for one frame serial;
  `next_frame()` bumps the serial at Present and at Reset (one relaxed
  increment). No allocation, written only by the submission thread; a collision
  or a stale entry costs one walk. The fixture proves a same-frame repeat does
  not walk, that another node or light does, that a new frame re-walks and
  observes a moved root, and that rejections are memoised too.
- **Telemetry** (only while the patch is live). `present()` logs one
  `point_light_admission_frame device= frame= tests= fast_admit= reject= walks=
  memo_hits= root_admit= root_reject= chain_unreadable= chain_too_deep=
  chain_cycle= node_is_root= root_unreadable= light_unreadable= reach_negative=
  samples=` line per frame from the frame's counters (`tests = fast_admit +
  reject`, `reject = walks + memo_hits`, `walks =` the sum of the nine
  outcomes) and resets them; on a capture frame (`begin_frame(true)`, the F8 /
  `capture_start` mechanism of the motion route) the first 64 walked nodes are
  sampled into fixed storage and logged as `point_light_node device= frame=
  node= root= depth= dist= reach= root_dist= root_reach= verdict= node_scale=
  root_scale=` (`dist` is the engine's `trunc(sqrt)` value reconstructed from
  the remainder, `reach = range + node scale`, `root_dist` an integer square
  root of the walk's 64-bit sum, `verdict` one of the outcome names). Memo hits
  are not sampled. Parsers: `verify_point_light_site.py`
  `parse_frame_line`/`parse_node_line`.
- **Cost.** Per-node admit: the taken `JMP`, the taken `JLE`, one memory
  increment and the taken jump back (the fixture measures 21.7 ns native vs
  21.9 ns patched per harness call, within noise). Per-node reject,
  first test of a (node, light) in a frame: the `JMP`, the not-taken `JLE`,
  three pushes, the call, the memo probe, `Get/SetLastError`, `hops+1` parent
  reads plus four field reads through the region cache (a spin lock and a scan
  of up to 32 cached regions each; one `VirtualQuery` per new region per
  frame), the 64-bit compare, two relaxed counter increments, the memo store
  and the return jump (plus two more reads on a capture frame while the sample
  has room): 87.7 ns vs 21.7 ns native at chain depth 1. Every further test of
  the same (node, light) in the frame: the `JMP`, three pushes, the call, the
  memo probe hit, one relaxed increment and the return jump: 23.2 ns
  (Wine/FEX, harness included, not game FPS). Per-part
  model at run-22 counts, about 5 700 parts × 2 populated slots per frame,
  worst case all rejected: without the memo ≈ 11 400 walks × 66 ns ≈ 0.75 ms
  per frame plus `VirtualQuery` churn when the parts' nodes span more than the
  32 cached regions; with the memo the walks are bounded by the distinct
  (node, light) pairs per frame (hundreds, not thousands: ≈ 0.03 ms at 400
  pairs) and the remaining ≈ 11 000 tests add ≈ 1.5 ns each (≈ 0.02 ms).
  Cross-view repeats (sector, background, cockpit scene) hit the memo as well.
  The frame line is one `log()` per Present.
- **Acceptance evidence** for the game itself (the `i0.x` table and the far→near
  median gain of the critique's verification item 2) is still pending: run 27
  had the option on and the user saw some docking-module parts still dark, but
  that build logged nothing per frame or per node; the next capture's
  `point_light_node` lines say per module whether its root was too far, its
  root scale small, or its chain ended at a non-body root. Static and fixture
  evidence is in the ledger.

## Directional lights: source, space and count

Read-only Ghidra pass, 2026-09-17, on the EXE of [executable.md](executable.md)
(`fdbf3418…`, preferred base `0x00400000`), project `/tmp/x3-ghidra-research
X3Render`, script `tools/analysis/X3CameraState.java`, specs
`range:004c5030:48 range:004c2330:140 range:004c4fc0:40 range:004c5200:24
range:004c26df:20 range:0047d641:44 range:0047d5b0:14 ins:00420260 dec:0047d5e0
dec:0047d560 dec:0047c640 dec:004bdea0 dec:004bdbf0 dec:00488170 data:0047d5e0
data:0047c640 data:0047e820 data:004bdbf0 data:00608518 load:0x5e8c load:0x6288
load:0x628c ptr:00565578:6`. Raw listings stayed under the session scratchpad
(untracked). No Wine command, no launch, no patch. This is the
program-independent sun source the run-38 A diagnosis
([directional-shadows.md](../verification/directional-shadows.md), "Run 38 A
(run111) diagnosis") asked for; it does **not** replace the per-program CTAB fix,
it validates it.

### The chain, end to end

Everything hangs off the one global render context `R = *(void**)0x00608518`,
written once at startup (`0x00470df8` in the constructor `0x00470d80`, stored
again by main init at `0x00403367`; those are the only two `WRITE` references in
the image). Three arrays inside `R` matter, and their displacements are adjacent
and self-checking:

| Span | Meaning | Established at |
| --- | --- | --- |
| `R+0x5e8c` … `R+0x6287` | **Light candidate array**: 255 dwords = up to 254 node pointers plus a `0` terminator | built by `0x0047c640`, capacity test `(int)n < 0xfe` |
| `R+0x6288` | Slot count, set to **8** | `0x004b9bbb` `MOV [ECX+0x6288],8` |
| `R+0x628c` … `R+0x62eb` | **Per-node slot table**, 8 records × 12 bytes `{light index (−1 = empty), score, distance}` | reset `0x0047d560`, filled `0x0047d5e0` (`+0x628c`, `+0x6290`, `+0x6294`), read `0x004c5050` and `0x004c26ed` |

`0x6288 − 0x5e8c = 0x3fc` = exactly 255 dwords, which corroborates the 254-entry
capacity independently of the `0xfe` immediate.

**1. Candidate array — per view.** `0x0047c640(scene, ref)` walks the scene's
light node lists, admits a node on `+0x12c & 4` (the "is a light" bit), applies a
distance/range cull **only** to point/spot lights (`+0x12c & 0x400010`; the cull
uses the Chebyshev distance to `ref+0x30/34/38` against the light's range
`+0x158`, with the `0xccc` ratio test), appends the pointer, and null-terminates
at `R+0x5e8c+4n`. A **directional light is never culled**: the first disjunct
`(flags & 0x400010) == 0` admits it outright. Three call sites: `0x0047c8ad` (in
`0x0047c840`) and `0x0047e8ae` (in `0x0047e820`), both of which are called only
from the view render `0x00471f50` (`0x00472260`, `0x004723c8`, `0x00472461` and
`0x00472210` respectively), plus `0x0042172f` in `0x004216e0`, which immediately
follows it with `0x0047d5e0` and a slot-table walk. So the array is rebuilt once
per rendered view, before submission, and once more on the `0x004216e0` path.

**2. Slot table — per submitted node.** `0x0047d5e0(node)` resets all 8 slots to
−1 (`0x0047d560`), returns immediately unless `node+0xa0 ≥ 2` **and**
`0x00488170(node) ≥ 2`, then scores every candidate and keeps the best 8. The
score (`0x0047d641`–`0x0047d6ac`) is

```
luma = round(0.299·R + 0.587·G + 0.114·B)      ; node words +0x150/+0x152/+0x154
score = luma + 0x300                            ; if +0x12c & 0x800000  (directional)
score = luma − luma·d/(2·range)                 ; point/spot, d = Chebyshev distance − node+0x70
```

The three doubles at `0x00565578`/`0x00565580`/`0x00565588` decode to
0.114 (B), 0.299 (R), 0.587 (G) — Rec.601 luma, exactly. Finally the table is
`qsort`ed (`0x0047d94d`+) with the comparator at `0x0047d5b0`, which compares
record `+4` and returns 1 when the second is larger, i.e. **descending by score**.
A directional light scores ≥ 768 and a plain point light ≤ 255, so directional
lights always sort to the front. Caller: `0x0047dff1` inside the render-node visit
`0x0047d9c0`, immediately followed by `0x004bdea0` (which writes each selected
light's slot index back into its D3DLIGHT record at `[light+0x16c]+0x68`).

**3. Selection of the two Dir slots — per node.** `0x004c4fc0`, `0x004c5030`–
`0x004c508f`, with `EBP = 0` from `0x004c4fdf`:

```
004c5030  MOV  EDI,[0x00608518]
004c5036  MOV  EDX,[EDI+0x6288]        ; 8
004c5046  LEA  ESI,[EDI+0x628c]
004c5050  MOV  EAX,[ESI]               ; slot index
004c5054  JL   0x004c508f              ; negative index ends the scan
004c5056  MOV  EAX,[EDI+EAX*4+0x5e8c]  ; the light node
004c5061  TEST [EAX+0x12c],0x800000    ; directional?
004c506d  CMP  [EAX+0x158],0x256250    ; or range > 2 450 000 native
004c5079  TEST EBP,EBP                 ; first found -> EBP, second -> [ESP+0xc], stop
```

and `0x004c5202`–`0x004c5215` passes them as arguments 5 and 6 of `0x004c0150`
(`PUSH [ESP+0xc]; PUSH EBP; PUSH 2; PUSH EDI; PUSH ECX; PUSH EDX`), i.e.
`[EBP+0x18]` = Dir0 and `[EBP+0x1c]` = Dir1. The other branch, `0x004c5217`,
passes `0, 0, 0` — no directional light at all. **Exactly two** lights can reach
the `LightDir_*` parameters; the loop terminates on the second.

**4. Upload — per submitted node, by handle, `SetVector`.** Inside `0x004c0150`.
Handles are cached at `0x004c1a3e`–`0x004c1a9d`: descriptor `+0x54`
`LightDir_Dir0`, `+0x58` `LightDir_Color0`, `+0x5c` `LightDir_Dir1`, `+0x60`
`LightDir_Color1`. Dir0 is written at `0x004c234d`–`0x004c245e`:

```
004c2361  MOV  EDX,[EAX+0xb0]      ; light node world position, raw ints
004c2367  MOV  ECX,[EBP+0xc]       ; the submitted node (argument 2)
004c2370  SUB  EDX,[ECX+0xb0]      ; delta = light − node,  x/y/z
004c23af  FILD ... FSQRT           ; length
004c23d6  FMUL double [0x00565510] ; 2^-16
004c23e4  CALL 0x0052b5d0          ; float -> int, x65536 fixed point, per component
004c2415  MOV  EDX,[EBX] / MOV ECX,[EDX+0x88]   ; ID3DXEffect::SetVector (slot 34)
004c242e  MOV  EAX,[EDI+0x54]      ; the LightDir_Dir0 handle
004c2455  FLDZ / FSTP [ESP+0x1ec]  ; w = 0
004c245e  CALL ECX
```

Dir1 is the same code at `0x004c2507`–`0x004c2621` on `[EBP+0x1c]`, descriptor
`+0x5c`. `LightDir_Color0` (`0x004c246f`–`0x004c24b7`) and `LightDir_Color1` copy
three floats from `[light+0x16c]+0x04/0x08/0x0c` — the `D3DLIGHT9.Diffuse` of the
record, written by `0x004bdbf0` as the node's colour words × `1/256`
(`0x00565568`) — again with `w = 0`, again `SetVector`. When the light argument is
null the engine writes an all-zero float4 instead (`0x004c24bb`, `0x004c2674`).

So the update frequency is **per `0x004c0150` invocation**, that is per submitted
mesh part per view (the single caller `0x004c5228` in `0x004c4fc0` runs per part,
existing section above) — never per frame and never per material. Every part of
one node recomputes the same vector from the same node origin.

### Answers

**Space and sign.** World space, and **object-relative**: the uploaded vector is
`normalize(light[+0xb0/b4/b8] − node[+0xb0/b4/b8])`, both raw render-domain
integers, with **no matrix anywhere** in `0x004c234d`–`0x004c245e`. It points
**from the shaded node towards the light**; the direction light travels is its
negation. There is no untransformed "world sun" vector in memory — the engine
stores only the light node's world **position**, and the direction is synthesised
per node. To recover a world direction from a captured register you need that
draw's node position: `L = node_pos + |L − node_pos| · dir`; conversely, to
produce one, read the light node position and pick any origin.

Because the sun sits ~1.57e9 native units away (run-39 fit, below) and scene
nodes are within ~1e6 of the camera, the per-node variation is small but real:
0.6° scene-wide in run 39 (`c4.x` from −0.310425 to −0.300079). A single frame sun
computed from the camera position is therefore accurate to well under a degree,
which is adequate for a shadow basis and *not* adequate for a bit-exact match to
any one draw's register.

**Count and ordering.** Up to 254 lights can be in the candidate array, 8 in the
per-node slot table, and **exactly 2** in `LightDir_Dir0/1`. Ordering is
**brightest first**, by Rec.601 luma of the light node's colour words, not by
sector-file order: the slot table is sorted descending by score and the selector
takes the first two admitted entries. Two caveats, both measured: (a) admission is
`+0x12c & 0x800000` **or** `+0x158 > 0x256250` (2 450 000 native ≈ 24 500 world
units at the session context scale 0.01), so a point light with a huge range can
occupy a Dir slot; (b) the score of a point/spot light is distance-dependent, so
in principle the *identity* of Dir0/Dir1 is per node, although a directional
light's +0x300 bonus makes a flip between two ordinary lights and a sun
impossible. Directional lights beyond the second are uploaded into the
`g_LightPoint` array with `atten = (1,0,0)` (`0x004c28f9`–`0x004c2969`, existing
section above), so a third sun still lights the scene — just not through
`LightDir_*`. `LightDir_Dir1` is declared by 44 of the 751 archive programs and in
run 22 carried a dim bluish colour (luma 0.227 against the sun's 0.742): the
engine's "fill" is that second directional light, not an ambient term —
`g_LightAmbientIntensity` has no writer, no consumer and no register
([camera-state-and-frame-routine.md](camera-state-and-frame-routine.md)).

How many suns a sector actually has is a scene-file question and is **not**
answerable from the EXE. What the EXE fixes is the ceiling (2 in the Dir slots)
and the rule for which two.

**Directional flag provenance.** `+0x12c & 0x800000` is not authored; `0x004bdbf0`
sets it lazily when it fills the `D3DLIGHT9` record — spot on `& 0x10`, point on
`& 0x400000`, otherwise type 3 and `+0x12c |= 0x800000`. A reader that wants to be
independent of whether the record has been built yet should test
`(flags & 4) != 0 && (flags & 0x400010) == 0`, which is exactly `0x0047c640`'s own
admission logic.

### Safe read contract for the proxy (hook-free)

No hook is required. Once per frame, from the render thread:

1. `R = *(uint32_t*)0x00608518`. Refuse if null (it is null before
   `0x00470d80` runs) or unreadable.
2. Validate the layout before trusting it: `*(int32_t*)(R+0x6288) == 8`. This is
   the cheapest single check that the context is the one this note describes.
3. Walk `p = (uint32_t*)(R + 0x5e8c)` until `*p == 0` or 254 entries, reading
   through the proxy's bounds-checked `engine_memory::read`. For each node `n`:
   `flags = *(uint32_t*)(n+0x12c)`; keep it when `(flags & 4) && !(flags &
   0x400010)`. Its world position is the three `int32_t` at `n+0xb0/0xb4/0xb8`;
   its colour is the three `int16_t` at `n+0x150/0x152/0x154` (divide by 256 to
   get `LightDir_Color0`).
4. Rank the kept nodes by `0.299·R + 0.587·G + 0.114·B` descending; the first is
   the engine's Dir0 for any node that reaches `0x0047d5e0`. Sun direction for a
   receiver at `P`: `normalize(light_pos − P)`; use the view position for a
   frame-wide basis. Light-travel direction is the negation.

*Validity and change points.* The array contents change at every
`0x0047c640` call, i.e. once per rendered view inside `0x00471f50` — so between
two Presents it is rewritten for the sector view, the background view and any
secondary scene. A read taken at Present therefore reflects the **last** view
rendered, not necessarily the sector. The concrete hazard: `0x00420260` builds a
secondary scene (camera `+0x270 |= 0x24`, the cockpit/HUD-class marker) whose 16
light nodes (`[ESP+0x14] = 0x10` loop at `0x004202f5`–`0x00420355`) are each given
colour words `0x80`, direction `+0x30/34/38 = (0,0,0x10000)` and a **forced**
`+0x12c |= 0x800000` at `0x00420333`. Whether those nodes are attached into a
list `0x0047c640` walks is *not* established here; treat it as possible. Three
defences, cheapest first: (a) prefer the directional candidate with the largest
`|position|` (the sun is at ~1.5e9 native, a cockpit light is not) or the largest
luma × distance; (b) take the read while the sector view is current, which the
camera-state note's `cam+0x270 & 0x810000` test identifies; (c) cross-validate
against the shader: on the first draw of a program whose CTAB declares
`LightDir_Dir0`, compare the register with `normalize(light_pos − node_pos)` and
refuse the engine-memory sun if they differ by more than a few degrees. (c) is the
check the in-flight per-program fix already makes possible and is the one that
turns this into ground truth rather than a second guess.

Other validity conditions, all measured: the slot table is empty (all −1) for any
node with `+0xa0 < 2` or `0x00488170(node) < 2`, and then `0x004c4fc0` passes no
lights and the shader gets an all-zero `LightDir_Dir0` — a zero register is a
legitimate engine state, not a capture error. Sector transit, menu and cutscene
change the scene and hence the candidate array wholesale; nothing caches a light
pointer across that, so a per-frame re-read is required and a cached node pointer
must never outlive a frame. The EXE is non-relocatable and the proxy already gates
patches on the `fdbf3418…` hash; the same gate covers these absolute addresses,
and step 2 is the runtime layout assertion.

*If a hook is ever preferred over polling*, the natural site is the selector tail
`0x004c508f` (`MOV EDI,[ESP+0x44]`, 4 bytes) where `EBP` holds Dir0 and
`[ESP+0xc]` Dir1 — but it is inside `0x004c4fc0`'s SEH frame, runs per submitted
node (thousands of times per frame), and `EDI` is being reloaded there, so a
detour must preserve `EBP`, `ESI`, `EBX` and every `ESP`-relative local and would
need its own rate limiting. Polling `R+0x5e8c` costs one pointer chase and a short
scan once per frame and has none of that exposure. **Recommendation: poll, do not
hook.**

### Cross-check against captures

Cheap numeric check, from numbers already in the notes rather than a new capture:
the run-39 least-squares fit of 615 (direction, `object_position`) pairs to a
single world point gave `L = (−4.708e8, +7.144e8, −1.3151e9)` native, `|L| =
1.5689e9` (`camera-state-and-frame-routine.md`, round 2). Normalised that is
`(−0.300079, +0.455345, −0.838220)`. The run-111 A-frame replay basis measured on
all 38 A frames was `forward = (0.2965, −0.4568, 0.8387)`
(`directional-shadows.md`). `−forward` is **0.2496°** from the run-39 unit vector,
and run 22's captured `c4 = (−0.304886, 0.455627, −0.836319)` is **0.3882°** from
it. All three sessions agree to within the 0.6° per-node spread this section
predicts, which is what "the sun is one world *point* and the register is
`normalize(L − node)`" requires and what a view-space or per-draw-rotated vector
could not produce.

*Uncertainty.* Everything above is static on `fdbf3418…` plus previously measured
capture numbers; no new capture was taken and no value was read from a live
process. Not established: whether `0x00420260`'s 16 forced-directional nodes ever
enter the candidate array; the identity of `0x00488170`'s return (used only as the
`≥ 2` gate); the exact semantics of the spot-light (`+0x12c & 0x10`) score
multiplier inside `0x0047d5e0`, which can scale a score up and is the one path
that could in principle put a non-sun into a Dir slot; and how many directional
lights a real X3 sector authors, which
lives in the scene files, not the EXE.
