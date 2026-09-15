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
