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
