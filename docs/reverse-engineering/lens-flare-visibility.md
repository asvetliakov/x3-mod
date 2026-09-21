# Lens-flare and corona visibility: the vanilla occlusion mechanism

2026-09-19. Offline disassembly of `X3AP.exe` only (Ghidra headless, `-readOnly
-noanalysis`, existing `/tmp/x3-ghidra-research/X3Render` project) plus raw byte
reads of the EXE. No Wine run, no build, no game launch. Extends
[the sun/lens-flare resource chain](sun-material-identity.md), which identified
the lens scene, the record list and the ±100 accumulator without explaining what
sets the record's visible flag.

**Answer: the sun lens-flare chain is gated by a CPU mesh-collision probe, not a
D3D occlusion query, not a readback and not the depth test.** The probe produces
one boolean per lens record (per sun source, per view); the boolean drives a
three-state size ramp (0 / ½ / full over two frames), so the chain **pops**
rather than fades. The flare sprites themselves are drawn at a fixed near depth
in a separate late scene that never clears depth, so the depth test cannot
reject them: the CPU probe is the only occlusion the effect has.

## 1. The executable contains no occlusion query and no flare readback

`X3FindVtableCalls` over the whole program, device vtable slot 118
(`+0x1d8`, `CreateQuery`): **0 call sites**. The same scan run for slot 81
(`+0x144`, `DrawPrimitive`) returns the three known draw sites
`0x004bf81a`, `0x004c008a`, `0x004c4e54`, so the scanner resolves this build's
`MOV reg,[dev+disp]` / `CALL reg` form correctly. A raw byte scan for
`call dword ptr [reg+0x1d8]` also returns 0. X3AP.exe therefore never creates a
D3D9 query of any type — no `D3DQUERYTYPE_OCCLUSION`, no predication.

Slot 32 (`+0x80`, `GetRenderTargetData`) has three call sites
(`0x004d0ce4`, `0x004d14cb`, `0x004dcfbc`), all inside `0x004d0c40` /
`0x004dced0`, neither of which is reachable from the flare path. The flare
decision path in §2 issues **no device call at all**: its whole callee set is
fixed-point math, scene-graph traversal and the collision narrow phase.

## 2. The decision path

| Site | Operation |
| --- | --- |
| `0x0047d9c0` (`0x0047e264`–`0x0047e30f`) | Per-frame traversal finds/allocates the 0x70-byte lens record in view `+0x2a0`; `+0x38` = lens group, `+0x10` = accumulator (0 on allocation) |
| `0x0047e315`–`0x0047e5b0` | Gate: source camera-space `z` (`node+0xf8`) > 100 **and** > 2 × node radius (`+0xa0`), then two FOV/screen-extent tests. On success writes screen position `+0x20/+0x24`, depth constant `+0x28 = 0x8000`, size `+0x34`, and sets **`record+0x30 = 1`** (visible) |
| `0x00471f50` (`0x00472370`) | Per ordinary view, after that view's geometry submission: clears the occluder cache `view+0x37c`, then calls `0x004715d0` unless `view+0x270 & 0x1000` |
| `0x004715d0` (call at `0x00471630`) | For every lens record with `+0x30 != 0`: `if (FUN_00488720(record, view)) record+0x30 = 0` |
| `0x00488720` | The test proper (§3) |
| `0x00488a70` | Recursive scene-graph walk building the candidate occluder array at `view+0x37c`, **capped at 255** entries (`DAT_006085d0`) |
| `0x00488b60` | Per candidate: early-out box test, then probe scaling and the narrow-phase call |
| `0x0048ac30` → `0x0048a890` | The **same narrow-phase mesh collision routine used by the sector collision pass** (see [sector-collide.md](sector-collide.md)); leaf × leaf reaches the mesh test at `0x0047f1b0` |
| `0x00471660` (call at `0x00472442`) | Accumulator update and body instantiation/placement, then the lens scene is drawn |

`0x00488720` has exactly one caller (`0x00471630`); `0x0047e820`, the only
`Clear` call site with flags 3 (target + Z), has exactly one caller
(`0x00472210`), which precedes the lens-scene block at `0x00472442`.

## 3. What `0x00488720` actually tests

Return 1 means "hide this flare chain".

1. `*(0x00606f34)+0xfc & 0x8000` clear ⇒ **return 1 for every record**. `+0xfc`
   is the `VideoD3DFlags` registry word ([compositor-and-glow.md](compositor-and-glow.md)),
   so the user's flare video setting is implemented inside the occlusion test.
   The exact option label is not established here.
2. The record's screen position must lie inside the view rect
   (`view+0x288..+0x294`); otherwise return 0 (the collector already withheld
   `+0x30` in that case).
3. `view+0x270 & 0x8000000` ⇒ return 1.
4. Screen position + view FOV half-angle (`view+0x298`, trig LUTs at
   `0x005?6998`) are converted to a world direction; the shared probe object at
   renderer root `+0x6c` — body **195**, created with the lens scene in
   `0x004714c0` — is given the view's orientation (`view+0x30..+0x3c`) and aimed
   along that direction.
5. Candidate occluders (built once per view per frame by `0x00488a70`): nodes
   whose max-norm distance from the camera, scaled by the FOV term, is under
   64 × the probe's cached subtree radius (`0x00488170`), that carry
   `node+0x12c & 0x1000000` and `& 2` (renderable this frame) and lack
   `node+0x130 & 0x20000000`.
6. For each candidate, `0x00488b60` rejects it unless it lies within ±2r of the
   probe laterally and in front of it, then sets the probe's uniform scale to
   `maxnorm(probe, candidate) + 2r` via `0x004880e0` (which also writes
   `node+0xa0 = scale`, `+0x80/84/88 = 1.0`, invalidates `+0xa4`), clamped so
   the probe does not reach past the sun source, and runs
   `0x0048ac30` → `0x0048a890`. Any hit ⇒ return 1.

So the test is a uniformly scaled body-195 volume swept toward the flare,
intersected against real part meshes — a genuine geometry test, at mesh
granularity, but with a **binary** result. The authored shape of body 195 (and
hence the effective cone/cylinder thickness) was not decoded. Note also that the
early-out in step 6 uses the probe radius left over from the *previous*
candidate, since `0x00488170` runs before `0x004880e0` rescales the probe.

## 4. Boolean → picture: a two-frame size ramp, not a fade

`0x00471660`: `record+0x10 += 100` when `+0x30 != 0`, clamped to 200;
`-= 100`, floored at 0, when clear; at `< 1` every instantiated body of the
chain is destroyed (`0x00487be0`). The collector then computes
`record+0x34 = screen_size_term × record+0x10 / 200` (`0x0047e4xx`), and
`0x00471660` writes each body's scale as `size_lerp × record+0x34 >> 16`.

Consequences, all established from the code:

- The ramp has three states: full, half, gone. At 60 fps a blocked sun removes
  the chain in ~33 ms, and what the player sees in between is a **half-size**
  chain, not a dimmer one — the accumulator never touches colour or alpha.
- The unit is the whole record: one sun source per view. Every body in the group
  (the 20-byte Lensflares rows, up to 36 distinct bodies) switches together.
  There is no per-sprite and no partial occlusion.
- Per-body size still varies with the source colour (`node+0x150/0x152/0x154`)
  and with screen geometry, which is why the chain looks animated even though
  its occlusion state is binary.

## 5. Why the sprites cannot be depth-rejected

The collector always writes `record+0x28 = 0x8000`, and `0x00471660` turns that
into body position `node+0xb8 = 0x8000` (0.5 unit) with `+0xb0/+0xb4` derived
from screen coordinates × viewport size. All flare bodies of all chains
therefore sit at one fixed near depth inside the `Lensflare Scene`, which is
traversed after every ordinary view and after the conditional bloom call at
`0x004721b1`, with **no intervening depth clear** (single `Clear` caller,
§2). The authored MATERIAL6 flare bodies do declare `ZEnable = true`,
`ZWriteEnable = false` ([sun-material-identity.md](sun-material-identity.md)),
but at that depth the test passes against anything already in the buffer. In
effect the flares are drawn over the scene; the CPU probe is the only thing
that hides them.

## 6. Other flare classes are not part of this system

Engine glow/thruster flares, weapon bolts, station and dock glows, position
lights and deco flares are ordinary scene geometry drawn in the main views with
**Z test on, Z write off**, per the blend census in
[effect-shader-users.md](effect-shader-users.md). They are therefore occluded
per pixel by the depth buffer — correct but hard-clipped, with no fade and no
bloom-through. Only sources carrying `node+0x12c & 0x20000000` (the TSuns lens
sources) enter the lens-record path above. The sun disc itself is the separate
TPlanets scene path and is depth-tested normally.

## 7. Visible weaknesses

1. **Pop.** A sun crossing a station strut, a ship hull or a rotating gate ring
   removes the entire chain within two frames and restores it the same way.
   This is the dominant artifact and it is intrinsic to the binary test.
2. **Shine-through by omission.** Candidates are limited to 255 and to the
   near-field distance gate of step 5, and must be renderable this frame (so LOD
   or cull state changes the occlusion result). Anything outside those bounds
   never occludes the flare, and because of §5 the depth buffer will not catch
   it either.
3. **Cost.** Each view rebuilds a candidate list by walking the scene graph and
   then runs the full narrow-phase collision routine per candidate, on the
   render thread, every frame. `0x0048a890` is the routine
   [sector-collide.md](sector-collide.md) measures as the expensive
   near-miss case.
4. The whole effect is also switched off wholesale by the `VideoD3DFlags`
   bit, so a mod-side feature must tolerate the chain simply not existing.

## 8. Recommendation for the proxy: **MINOR**

Depth-buffer-based flare occlusion in the proxy is a polish item, not a
correctness fix, and it is below TAA/HDR in value:

- Vanilla already prevents gross shine-through for the sun chain with a real
  mesh test, so the mod would be buying smoothness and partial occlusion, not
  a missing feature.
- The proxy cannot smooth the transition from outside: by the time the lens
  scene draws, the engine has already decided and destroyed the bodies. A
  proxy-side solution needs two parts: (a) neutralise the engine gate so the
  chain always exists, and (b) modulate the lens-scene draws by a depth-derived
  visibility factor sampled around the sun's screen position from the scene
  depth the AO/TAA path already keeps ([depth-resolve-backend.md](depth-resolve-backend.md)).
  Identifying the draws is feasible — they are the only draws of the
  `Lensflare Scene`, after the ordinary views.
- Part (a) has a clean site. **Hook-site suitability of `0x00488720`:** entry
  bytes `55 8b ec 83 e4 f0 83 ec 44` (`push ebp; mov ebp,esp; and esp,-16;
  sub esp,0x44`), so the entry is an instruction boundary; the single call site
  `0x00471630` is followed by `83 c4 08`, i.e. **`__cdecl`, caller cleans the
  two stack arguments**, result in EAX only. It is called from one thread
  inside `0x004715d0`'s record loop and is *not* reentrant (it mutates the
  shared probe node at renderer root `+0x6c`), but a 3-byte `31 c0 c3`
  (`xor eax,eax; ret`) at the entry never reaches that state and owes nothing to
  the caller for stack or flags. It would also remove the per-frame narrow-phase
  cost of §7.3. Not installed and not tested here.
- Against it: an engine instruction patch plus a shader-side factor for an
  artifact that is visible only while the sun crosses an occluder edge. If we do
  not want the patch, the honest call is **DROP**, because without (a) the proxy
  can only soften a decision already made.

Reopened 2026-09-22 on a user report; the design that follows this section's two-part outline is
[sun-partial-occlusion.md](../architecture/sun-partial-occlusion.md).

## 9. What comparable space games do (author's own knowledge, not verified here)

Modern space titles moved off CPU visibility tests for sun glare and onto
screen-space or GPU-visibility data, which is exactly what removes the pop X3
has. Elite Dangerous and Star Citizen both drive sun glare and flares from
screen-space depth/occlusion sampling around the light's projected position,
combined with temporal smoothing, so a star sliding behind a station edge fades
over several frames; Star Citizen's deferred renderer also feeds the same
occlusion term into its screen-space godrays. X4: Foundations (the successor to
this engine's lineage) drives its sun flare and glare from a depth-sampled
visibility factor and fades it, rather than the all-or-nothing chain used here.
EVE Online's sun glare and No Man's Sky's flares likewise use a soft
screen-space visibility factor with a multi-tap depth comparison and temporal
easing. The common pattern is: a handful of depth taps around the projected sun,
averaged into a 0..1 factor, smoothed over time, multiplying flare intensity —
and not GPU occlusion queries, which are latency-prone and were the
mid-2000s answer. These statements are from general knowledge of those titles,
not measured in this project.

## 10. Local evidence

`X3AP.exe` SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
preferred base `0x00400000`, 2 153 984 bytes. Ghidra 12.1.3 headless with
JDK 21 against `/tmp/x3-ghidra-research/X3Render` (`-readOnly -noanalysis`),
scripts `X3FindVtableCalls.java`, `X3FunctionContext.java`, `X3CallTree.java`,
`X3DecompileFunctions.java` from `tools/analysis/`. Decompiler output and raw
byte dumps stayed in the session scratchpad and are not tracked. No capture log
was needed: the mechanism is entirely CPU-side. `grep -c CreateQuery` over the
two largest session logs (`/tmp/x3-bottleX3-run154/session-20260919-085922-476.log`,
942 MB, and `run153/session-20260919-085407-472.log`, 678 MB) returns 0 in both,
which corroborates §1 without proving it — the proxy does not trace that entry
point, so the static scan is the evidence.

---

# Addendum 2026-09-22: design questions Q2–Q6

Static follow-up for [sun-partial-occlusion.md](../architecture/sun-partial-occlusion.md)
(questions Q2–Q6 and the patch-site claims). Method: `i686-w64-mingw32-objdump -d -M intel`
over the verified `X3AP.exe` (SHA-256 re-checked this session, `fdbf3418…f8ab`), short local
Python over that listing for owner/caller/closure and branch-target scans, plus a read-only
catalogue read of `types/Lensflares.pck` and the `objects/v/*.pbd` bodies it names through
`tools/analysis/sector_fog_census.py`. The stale `/tmp/x3-ghidra-research/X3Render` project no
longer holds the program (idata empty), so Ghidra was not used; all addresses below come from the
raw listing. No Wine, no build, no launch. Listing, row dumps and body text stayed in the
session scratchpad and are untracked.

Confidence is marked per claim: **proven** (bytes read here), **derived** (exact arithmetic
identity from those bytes), **inferred**, **unresolved**.

## 11. Q2 — the lens record: layout, coordinate frame, record → back-buffer UV

**Record layout (0x70 bytes)** — allocation `0x0047e285` (`push 0x70` → `0x005112c4`), fields
written at `0x0047e2e0`–`0x0047e30f` and in the collector tail. **Proven**:

| Offset | Field | Written at |
| --- | --- | --- |
| `+0x00` / `+0x04` | intrusive list next / back-link (head `view+0x2a4`, tail `view+0x2a8`) | `0x0047e2fc`–`0x0047e30f` |
| `+0x08` | **owning view** | `0x0047e2e0` |
| `+0x0c` | source node (the TSuns lens node) | `0x0047e2e3` |
| `+0x10` | accumulator, 0 on allocation, ±100 clamped 0..200 | `0x0047e2e6`, `0x00471717`/`0x0047172a` |
| `+0x20` / `+0x24` | **screen position x / y**, 16.16 (below) | `0x0047e561` / `0x0047e5ac` |
| `+0x28` | depth constant `0x8000` | `0x0047e598` |
| `+0x30` | visible flag | `0x0047e5af` (1), `0x0047163c` / `0x004720a0` (0) |
| `+0x34` | size, 16.16 (below) | `0x0047e4be` |
| `+0x38` | lens group = source `node+0x1a0` | `0x0047e2ed`/`0x0047e2f3` |
| `+0x3c … +0x6c` | **13** instantiated body-node slots (`0x3c + 13×4 = 0x70`) | `0x0047182b`, freed `0x00471d7c`, `0x00471e3c` |

`+0x14…+0x1c` and `+0x2c` are never read or written on this path (**proven** by absence in the
five functions that touch records).

**Units: view rect and view scale are 16.16 normalised, not pixels.** `view+0x288/+0x28c` are
the rect's top/bottom and `+0x290/+0x294` its left/right, in 16.16 where `0x10000` spans the back
buffer and `0x8000` is its centre; `view+0x300/+0x304` are 16.16 *scale* factors, normalised so
that the longer screen dimension is `1.0`. Evidence chain (**proven** + **derived**):

- script opcode `0x00494f9f` writes `view+0x300 = 0x10000` as the default view scale;
- the global fallback pair `*0x00606f38 +0x28/+0x2c` (used when `view+0x300` or `+0x304` is ≤ 0)
  is built at `0x004db180`–`0x004db1e6` from the mode's `int16` width/height as
  `{1.0, h/w}` or `{w/h, 1.0}` in 16.16 (`0x00412450` = `FixDiv(a,b) = (a<<16)/b`,
  `0x00469a30` = `MulDiv(a,b,c) = a*b/c`);
- the collector's two off-screen gates at `0x0047e365`–`0x0047e397` and
  `0x0047e3ca`–`0x0047e3fc` compute `|x|/2 ≥ ((tan·(z/2))>>16 · Wn)>>16` and the same with `Hn`,
  which is the plain frustum test `|x| ≥ z·tan(fov/2)` only if `Wn = Hn = 0x10000` for a
  full view.

**The frame.** `view+0x298` is the full FOV as a 16-bit binary angle; the collector halves it,
masks to 16 bits and reads the quarter-circle table `T[k] = *(0x005b6998 + 4k)` with quadrant
folding (`0x0047e149`–`0x0047e1a5` for `cos`, `0x0047e1a6`–`0x0047e1f3` for `sin`), then forms
`tan = FixDiv(sin,cos)` (`0x0047e20d`) and `cot = FixDiv(cos,sin)` (`0x0047e22e`).

**Position formula** (**derived**, `0x0047e4c1`–`0x0047e5ac`), with source camera-space
`x = node+0xf0`, `y = node+0xf4`, `z = node+0xf8`:

```
hx = MulDiv(FixDiv(x, z), cot, view+0x300) / 2         ; 0x0047e4d5, 0x0047e4de
hy = MulDiv(FixDiv(y, z), cot, view+0x304) / 2         ; 0x0047e507, 0x0047e510
record+0x20 = (v290+v294)/2 + ((hx·(v294-v290))>>16) - 0x8000
record+0x24 = ((hy·(v28c-v288))>>16) - (v288+v28c)/2 + 0x8000
```

For a full-screen view (`v290 = v288 = 0`, `v294 = v28c = Wn = Hn = 0x10000`) this reduces
exactly to `record+0x20 = x/(2·z·tan)` and `record+0x24 = y/(2·z·tan)`, i.e. **half of NDC**, in
16.16, origin at the screen centre, **x right, y up**. Hence

```
u = 0.5 + record+0x20 / 65536          v = 0.5 - record+0x24 / 65536
```

and the sun is inside the view when `v290 ≤ record+0x20 + 0x8000 ≤ v294` and
`v288 ≤ 0x8000 - record+0x24 ≤ v28c`. The **inverse** of that mapping is read back by the probe
itself at `0x00488763`–`0x004887a2` (`esi = record+0x20 + 0x8000` against `+0x290/+0x294`;
`esi = 0x8000 - record+0x24` against `+0x288/+0x28c`), which independently fixes the y flip and
the `0x8000` centre: **proven**.

**Consumer.** `0x00471660` places each body at
`node+0xb0 = ((record+0x20 · row+0x04)>>16 · view+0x300)>>16` (`0x004718ad`),
`node+0xb4` likewise with `record+0x24` and `+0x304` (`0x00471912`), `node+0xb8 = 0x8000`
(`0x0047184f`). The third multiplier the code applies is the frame constant `0x10000` set once at
`0x00471793` and never rewritten in the loop, so it is an identity (**proven**): the lens scene's
own coordinates are 16.16 normalised screen offsets from the centre with `z = 0.5`.
`0x00471660` takes **no arguments**; it re-reads `root+0x64 → +0x1c` for the lens view and walks
every scene holder, view and record itself.

**Size.** `0x0047e402`–`0x0047e4be` (**derived**): with `k = node+0x1b0`,

```
ratio = (node+0x70 >= 0) ? MulDiv(node+0x70, (k+1)<<16, z + k·node+0x70) : 0x10000
avg   = ((v294-v290) + (v28c-v288)) / 2
record+0x34 = ((ratio·avg)>>16) · record+0x10 / 200
```

For `k = 0` this is exactly `r/z` in 16.16: **`record+0x34` is the source's angular half-size
(tangent), scaled by the rect's mean normalised extent and by the accumulator**. Two
consequences for the design: the visibility-pass radius does not have to be a guess — the disc
half-width as a fraction of the back-buffer width is `(record+0x34/65536) · cot(fov/2) / 2 ·
(200/record+0x10)`, using the same `cot` the position uses; and on the frame a record is
allocated `record+0x10 == 0`, so **`record+0x34` is 0 on that frame**. Because the collector runs
during each view's submission but `0x00471660` bumps the accumulator only later, in the lens
block, the size is one frame behind the flag: the observed ramp is 0 → ½ → full over three
collector passes, refining section 4.

The sprite size is a different quantity: `node+0x70 = (sizeLerp · record+0x34) >> 16`
(`0x004719d2`), `sizeLerp` interpolating `row+0x08·100 … row+0x0c·100` by the source colour term
`max_ch((c<<8)|c)` from `node+0x150/0x152/0x154`. Converting `node+0x70` to pixels additionally
needs the lens scene's context scale and the loader's body-vertex normalisation
([render-node-bounds.md](render-node-bounds.md): vertices are `int16 × 2^-14`, world row scale
`((node+0x70 × node+0x80..)>>16) × s`): **unresolved** here, and unnecessary if the radius is
driven from `record+0x34`.

## 12. Q3 — the lens traversal `0x0047e6e0`: synchronous draws, ABI, bracketing

**All mesh draws of the lens scene are issued inside the call before it returns** (**proven** for
direct edges): `0x0047e6e0` walks the global list `root+0x40` and per entry calls
`0x004bdee0` (`0x0047e70c`) and `0x004c4fc0` (`0x0047e769`); `0x004c4fc0 → 0x004c0150` is a
direct call, and `0x004c0150` holds the engine's main material draw at `0x004c403c`
([camera-state-and-frame-routine.md](camera-state-and-frame-routine.md) §9). Neither
`0x0047e6e0` nor `0x004c4fc0` contains a single indirect call. A direct-call closure from
`0x0047e6e0` (1084 functions) contains `0x004c0150`, `0x004c4fc0`, `0x004bdee0` and **none** of
the other six draw owners (`0x004bf4c0`, `0x004bfd40`, `0x004c4750`, `0x004c53d0`,
`0x004c55d0`, `0x004c5830`). The engine has no command buffer of its own, so there is nothing to
flush after the return: **the bracket at `0x00472491` covers every body draw of the chain.**

One caveat, **proven**: the lens block issues one further draw-capable call *after* the
traversal — `0x004724a5..0x004724a7` (`mov ecx,esi; call 0x004c53d0`) with `eax = root+0x6304`,
the fixed-function 2D overlay path whose `DrawPrimitiveUP` is `0x004c55b4`. `root+0x6304` is a
script-settable scalar (written at `0x00496274` from a script opcode, initialised at
`0x0047a7c9`); the per-view analogue is `0x004723d5` with `view+0x77c`. It is not part of the
body traversal and would sit outside the bracket. Whether it ever draws anything in flight is
**unresolved** (script-controlled).

**ABI and stack contract of `0x0047e6e0`** (**proven**): entry `a1 18 85 60 00` / `53` / `55` /
`8b 6c 24 0c` — `__cdecl`, exactly one stack argument (the view) read as `[esp+0xc]` after two
pushes, **caller cleans** (`83 c4 04` at `0x004724a2`, and at `0x004722c0` for the other site).
It pushes and pops `ebx, ebp, esi, edi` (`0x0047e778`–`0x0047e77b`) and uses **`ebp` as a value
register, not a frame pointer**; no locals, no `and esp,-16`, so it assumes only the 4-byte
incoming alignment. `eax/ecx/edx` are clobbered. Its return value is **dead at the site**: `eax`
is reloaded at `0x0047249c` (`mov eax,[ecx+0x6304]`).

**Incoming edges.** `0x0047e6e0` has exactly three direct callers — `0x004722b5` (per ordinary
view), `0x00472491` (the lens scene) and `0x0047e8f6` (inside `0x0047e820`, the `Clear(3)`
helper) — so the function is *not* lens-specific and must be hooked at the call site, not at the
entry. Nothing branches into `0x0047248b..0x00472496` from anywhere in the image, and no 32-bit
absolute reference to `0x00472491..0x00472495`, `0x00471630..0x00471634`, `0x00472442`,
`0x0047e6e0` or `0x00488720` exists in any section (scan over the whole file with the PE section
map): **proven**, no jump table or function pointer to preserve.

**Bracketing verdict: yes, the `compositor_bridge`-style pre/original/post transport used by
`src/proxy/scene_hook.h` at `0x004721b1` applies here**, with three differences from that site:
the callee takes one stack argument that the wrapper must leave in place (the caller cleans it
eleven bytes later, at `0x004724a2`, so the post step runs with the argument still on the stack);
`esi` is live across the call (`0x004724a5` reads it) and `ebx/ebp/edi` must be returned intact
because the original preserves them; and the incoming flags are dead (the first flag consumer
after the call is `add esp,4` at `0x004724a2`, which sets them), so flag preservation is a
courtesy, not a requirement.

## 13. Q4 — every other reader and writer of the probe's side effects

**(a) `view+0x37c`: exclusively the flare probe's candidate cache** (**proven**). It is a
256-entry, NULL-terminated array of node pointers at `view+0x37c … +0x778` filled with the global
counter `0x006085d0` capped at 255. The only references in the whole image are: the per-view
reset `mov [esi+0x37c],ebx` at `0x00472367` (ebx = 0, immediately before the `0x004715d0` call),
the lazy build inside `0x00488720` (`0x004889d9` tests the first slot, `0x00488a1d` writes the
terminator, `0x00488b12` appends) and the read loop `0x00488a31`–`0x00488a58`. No other consumer
exists, so an override that never calls `0x00488720` simply leaves the cache empty for that
frame.

**(b) the body-195 node `root+0x6c`: shared with the cockpit camera, but stateless between
users** (**proven**). References: created in `0x004714c0` (`0x00471539` stores it;
`push 0xc3` = 195 at `0x0047153e`; destroyed at `0x00471584`), zero-initialised at `0x0047a7bf`,
read by the flare probe (`0x0048872f`) and by its candidate loop (`0x00488b7f`), **and read by
`0x004205e0` at `0x00420cd0`** — the cockpit per-frame update
([external-camera.md](external-camera.md) §2). That second user is the camera-boom sweep: gated
on `*(cockpit+0x54)+0x44 & 0x8000` (`0x00420ca4`), it aims the node with `0x00488460`, copies the
basis to `node+0xc0`, then per candidate calls `0x004880e0` (uniform scale) and `0x0048a7f0`,
keeping the nearest hit distance `node+0x188`. Both users set orientation/position and scale
themselves before every narrow-phase call, so **no consumer depends on the node state the other
leaves behind**; skipping `0x00488720` writes strictly less. It also means any patch that kept
the node mutation would still be safe, and that the node's authored shape is a general-purpose
swept probe, not a flare-specific cone (its geometry remains **unresolved**).

**(c) `record+0x30`: five sites, all in the renderer** (**proven**). Writers:
`0x0047e5af` (collector, = 1), `0x0047163c` (probe hit, = 0), `0x004720a0` (the frame routine's
per-frame reset of every record of every view of every holder, `0x00472083`–`0x004720ad`).
Readers: `0x00471625` (the probe loop's skip) and `0x00471712` (the accumulator). `0x00488720`
reads only `record+0x20/+0x24` (and view fields); it never reads `+0x30`.

**(d) no non-renderer consumer of the record list exists.** The list is reachable only through
`view+0x2a0/+0x2a4/+0x2a8`; every reference in the image is `0x0047a60a` and `0x00488d35` (view
construction), `0x0047e264` (collector find/allocate), `0x004715d0`, `0x00471660`, the frame
reset `0x00472090`, the bulk destroy at `0x00471e10`/`0x00471e63` (frees 13 body slots then the
record), and the removal-by-node / removal-by-view paths `0x00486ab0`, `0x00487d10`,
`0x00488e29`/`0x00488e48`. The remaining `+0x2a0` hits (`0x0041a7a9`, `0x0041ba89`, `0x0041fdd2`,
`0x00422ff5`, `0x0042b998`, `0x0042e80e`) are **cockpit** objects at the same offset — checked
individually (`0x0042e80e` resolves its base through the cockpit registry `*0x00608504` and
`0x0041cd20`). **There is no HUD sun indicator, AI or audio reader of the lens record or of the
probe's result**; the probe's only observable effect is `record+0x30`.

## 14. Q5 — which views reach `0x004715d0`, and identifying the main view

**Call shape** (**proven**): `0x0047236f` `push esi` / `call 0x004715d0` / `add esp,4` —
`__cdecl`, one argument, the current view of the frame routine's sorted loop; preceded by the
cache reset `0x00472367` and gated by `0x0047235d` `test [esi+0x270],0x1000` (set ⇒ the whole
flare step is skipped for that view).

**Which views are in that loop** (**proven**, `0x00471f71`–`0x00472191`): the frame routine walks
the scene holders at `root+0x18`, skips `root+0x64` (the lens holder), requires `holder+0x18 & 1`
and then every view in `holder+0x1c` with `view+0x270 & 1`; the pointers are collected into a
heap array and **sorted ascending by `view+0x29c`** with `0x00510510` and the comparator
`0x004715a0`. `+0x29c` is the documented view layer
([camera-state-and-frame-routine.md](camera-state-and-frame-routine.md)); the mod's scene-end
hook fires before the first view with `+0x29c > 0x11`. So monitors, cockpit and overlay views all
reach `0x004715d0` unless they carry `+0x270 & 0x1000`.

**Inside `0x004715d0` the record's view and the probing view can differ** (**proven**, and not
previously recorded): the loop iterates all holders (except `root+0x64`) and all views with
`+0x270 & 1` **and `view+0x29c ≤ arg+0x29c`** (`0x00471609`–`0x00471619`), then probes each record
of *that* view with the **argument** view (`0x0047162a` pushes `[esp+0x14]`, the argument, as the
second parameter). A record belonging to an earlier-layer view is therefore re-probed once per
later view, with a foreign rect and camera; normally the rect test rejects it and returns 0.

**Robust identification at the probe call.** Cheapest exact test, **proven** available from the
two arguments alone: `record+0x8 == view` — the record belongs to the view being probed. That
alone already excludes every cross-view probe. For "this is the main 3D view", three candidates,
in increasing strength:

- `view+0x270 & 0x4000` — the stardust/particle view; the frame routine selects such a view per
  holder at `0x00472123` and draws the dust for it at `0x00472307` (`0x004bf4c0`). Cheap, one
  test; uniqueness across monitors is **inferred**, not proven.
- `view+0x270 & 0x10000` — the sector-camera marker the cockpit update sets every frame at
  `0x0042157c` on `cockpit+0x58`. The proxy already relies on this pair
  (`src/proxy/sector_background.h` reads `cockpit+0x58` then `+0x270 & 0x10000`). Also ORed by
  the generic setter `0x004891fe`, so not exclusive by construction.
- `view == *(cockpit+0x58)` for the active control cockpit (`cockpit+0x10 == cockpit+0xc`,
  registry `*0x00608504` via `0x0041cd20`) — exact, and already implemented in the proxy; it
  costs a cached per-frame lookup rather than a per-call one.

Recommendation for the override: gate on `record+0x8 == view` **and** the mod's cached main-view
pointer (third bullet), refreshed once per frame. Records for monitor views then keep vanilla
behaviour with no extra per-call cost.

Cockpit meshes: records are only created for views with `+0x270 & 0x100` (`0x0047e139`) and for
sources with `node+0x12c & 0x20000000`; whether the probe's candidate walk
(`0x00488a70`, from `view+0x1c`'s `+0x8` list) ever reaches cockpit geometry is still
**unresolved**.

## 15. Q6 — Lensflares rows: the core disc, and the fallback body 31

**Row semantics** (20 bytes, **proven** from `0x00471660`, table `root+0xb0` indexed by
`record+0x38` as `{count, rows}` 8-byte entries; row stride `0x14` from `0x00471d3d`):

| Field | Meaning | Site |
| --- | --- | --- |
| `+0x00` | body resource id (`0x00487e30`) | `0x004717dd` |
| `+0x04` | **position factor**, 16.16, multiplies the record's screen offset | `0x00471855`, `0x004718b3` |
| `+0x08` / `+0x0c` | size at colour 0 / at full colour, ×100, interpolated by the source colour | `0x00471942`–`0x004719ab` |
| `+0x10` | rotation multiplier (word; 0 ⇒ no spin) applied to the sun's screen angle | `0x004719d5`, `0x00471d1f` |
| `+0x12` | padding; the text file has five fields | — |

So **position factor 1.00 means "at the sun", not 0**: factor 0 would be the screen centre,
values in `(0,1)` are ghosts between the sun and the centre, negative values are ghosts on the
opposite side. Per-group counts never exceed 13, exactly the record's 13 body slots.

`types/Lensflares.pck` (`addon/01.cat`, decoded SHA-256 `c98373778d2046e7…`, the same table the
earlier note hashed) has 35 groups; groups 0–8 are the installed TSuns groups with
13,13,13,11,11,10,11,11,10 = **103** rows, matching
[sun-material-identity.md](sun-material-identity.md). Classification from the bodies' own
authored comments (`objects/v/NNNNN.pbd`), **proven** from the assets:

| Body ids | Authored name | Role |
| --- | --- | --- |
| 719–722 | `glow green/blue/yellow/red` | **the core disc**; exactly the factor-1.00 round glow |
| 752, 753, 754, 760 | `5 narrow rays`, `bright center rays`, `6 thick rays`, `a lot of rays` | ray stars, factor 1.00 |
| 761–766 | `purple/green/gray/yellow/blue streak` | streaks, factor 1.00, some with a rotation multiplier |
| 11000–11011 | MATERIAL6 quads (4 vertices), `fx_yellow/red/greenflare.dds` | flare cards, factor 1.00 |
| 548, 549, 550, 740, 741, 744, 745 | `flare ble/brown/red`, `5/8 sided flare`, `faded flare` | ghosts, factor `<1` or negative |
| 735, 739, 778 | `ring violet / bright violet / rainbow` | ring ghosts |
| 61 | auto-generated, unnamed | ghosts (0.3–0.45 and −0.2…−0.28) |

Per group the core disc is: 0 → body 720 (rows 0 and 4), 1 → 721, 2 → 722, 3 → 719, 4 → 720,
5 → 722 **and** 719, 6 → 722, 7 → 721, 8 → 719. Every one of them is a **legacy MATERIAL3**
body, which matters for the design's step 2: per
[sun-material-identity.md](sun-material-identity.md), `0x004c0b06`–`0x004c0b2b` compares the
object's scene against `root+0x64` and selects the literal effect name `effects` for legacy
material in the lens scene, so the core discs go through the `effects` family, not the
fixed-function pipe. The exact runtime VS/PS pair per draw still needs the capture fingerprint.

**Fallback body 31 does draw a visible in-scene disc** (**proven**). `0x00440f0a`–`0x00440f2a`:
when the TSuns record's first field (table `*0x00606fc4`, stride `0xdb8`) is ≤ 0 the constructor
substitutes body id `0x1f = 31` with flags `0x104000`; since every installed TSuns row has model
−1, that path is always taken. `objects/v/00031.pbd` is named `/Sonne`, carries one legacy
`MATERIAL:` line, automatic object size 10 000 000 and 25 vertices forming a 4×4 grid of quads
spanning ±99 998 in x/y with z ∈ {0,1} — a large flat textured card. So in an ordinary sector the
player sees body 31 *plus* the lens chain; the design's phrasing "what the player sees as the Sun
is the lens chain itself" understates the in-scene card, while its claim that any in-scene sun is
ordinary depth-tested geometry that already fades per pixel is confirmed.

## 16. Patch sites: bytes, boundaries, and what a 5-byte call must preserve

All three sites are single `E8 rel32` instructions at instruction boundaries, are not branch
targets, and have no absolute reference anywhere in the image (§12). **Proven** bytes:

| Site | Bytes | Target | Context |
| --- | --- | --- | --- |
| `0x00471630` | `e8 eb 70 01 00` | `0x00488720` (rel32 `0x000170eb`) | preceded by `8b 4c 24 14` (`0x0047162a`), `51`, `56`; followed by `83 c4 08 / 85 c0 / 74 03 / 89 6e 30` |
| `0x00472491` | `e8 4a c2 00 00` | `0x0047e6e0` (rel32 `0x0000c24a`) | preceded by `56` (`0x00472490`); followed by `8b 0d 18 85 60 00 / 8b 81 04 63 00 00 / 83 c4 04` |
| `0x00472442` | `e8 19 f2 ff ff` | `0x00471660` (single caller) | preceded by `8b 70 1c`; `0x00472439` `0f 84 ad 00 00 00` skips the whole block to `0x004724ec` |

**`0x00471630` — confirmation of the design's claim.** `__cdecl (record, view)`, `push ecx`
(view, reloaded from `[esp+0x14]` each iteration) then `push esi` (record), caller cleans 8 bytes
at `0x00471635`, result in `EAX` only: **proven**. Liveness across the call: `ebx` (holder
cursor), `ebp` (**the constant 0**, consumed right after by `mov [esi+0x30],ebp`), `esi`
(record), `edi` (the view whose record list is being walked) must all survive — they are the
`__cdecl` callee-saved set, so a conforming thunk suffices, but note `ebp` is a *value*, so a
thunk must not use it as a frame pointer without restoring it. `eax/ecx/edx` are dead-in and
free. Incoming **flags are dead** (`add esp,8` then `test eax,eax` set them before the first
read), and so are the flags on return. `LastError` is not read anywhere near the site and the
original callee performs no Win32 call, so preserving it is discipline, not a requirement; the
x87 stack and MXCSR must be left untouched because the original uses neither (its whole callee
set is fixed-point: `idiv`, `imul`, `shrd`, LUT reads). Reentrancy: one call per record per view
on the render thread inside `0x004715d0`; the callee is not reentrant (it mutates `root+0x6c`),
but an override that answers without calling it never reaches that state.

**The vanilla gates, in order** (`0x00488720`, **proven**): (1) `0x00488742`
`f7 80 fc 00 00 00 00 80 00 00` — `*(*0x00606f34 + 0xfc) & 0x8000` clear ⇒ **return 1**;
(2) `0x00488763`–`0x004887a2` — the rect test of §11 ⇒ **return 0** when outside;
(3) `0x004887a8` `f7 83 70 02 00 00 00 00 00 08` — `view+0x270 & 0x8000000` ⇒ **return 1**.
The design's "two cheap gates" must be implemented as **all three, in this order**: the rect test
sits *between* them, so an override that tests gate 1 then gate 3 would answer 1 (hidden) where
vanilla answers 0 for a record whose position is outside the probing view's rect while
`0x8000000` is set. Replicating all three costs six loads and six compares and removes the
candidate walk plus the narrow phase entirely.

**`0x00472491`** — see §12 for the ABI, liveness and the bracket verdict. A replacement must
forward the single argument unchanged, must **not** clean it, must preserve `ebx/ebp/esi/edi`,
and may leave `eax/ecx/edx` and the flags in any state.

**`0x00472442`** (the design's fallback bracket start) — `0x00471660` is `void(void)`, no
arguments, no cleanup, single caller; `esi` (the lens view, loaded at `0x0047243f`) is live across
it and used at `0x00472450`. The fallback end `0x004724ec` is a branch target of `0x00472439`
and `0x004724b9`/`0x004724c2`/`0x004724cb`/`0x004724e0`, so it is a *label*, not a patchable call
— an end hook there needs a different mechanism than a call replacement.

## 17. Addendum evidence

`X3AP.exe` SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
2 153 984 bytes, preferred base `0x00400000`, sections `.text 0x00401000+0x130630`,
`.rdata 0x00532000`, `.data 0x00573000`, `.rsrc 0x00663000`. Listing produced with
`i686-w64-mingw32-objdump -d -M intel` (401 904 lines) into the session scratchpad; owner/caller
attribution, the 1084-function direct-call closure from `0x0047e6e0`, the branch-target scan over
`0x00471630..0x00471634` and `0x0047248b..0x00472496`, and the absolute-reference scan were short
local Python over that listing and over the PE section table. Game data was read read-only via
`tools/analysis/sector_fog_census.py` (`types/Lensflares.pck`, decoded SHA-256
`c98373778d2046e7…`; bodies `objects/v/00031`, `00061`, `00548`–`00778`, `11000`–`11011`).
No Ghidra project was available (the `/tmp` project's program store is empty); nothing was
imported or re-analysed. Still open: the authored shape of body 195, the pixel scale of the lens
scene's normalised units, whether `0x004724a7`'s 2D overlay ever draws for the lens view, and
whether the probe's candidate walk can see cockpit geometry.
