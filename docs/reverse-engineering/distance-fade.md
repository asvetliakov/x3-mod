# Distance fade per object class: inputs, thresholds and a station opt-out

Static study of X3AP.exe (preferred base `0x00400000`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`), 2026-09-24.
No game, Wine or build was run. Raw Ghidra listings and decompiler output stayed
in the session scratchpad. The engine calls this mechanism "fog"; it is the alpha
fade captured in Run 27 ([linear distance fade](../architecture/linear-distance-fade.md)).
This note extends the [native fade analysis](asteroid-fog-temporal.md) and the
[sector fog record](sector-fog.md). It adds the per-class answer, the two other
consumers of the same near/far pair, and a patch-site assessment.

Marks: **[s]** read from the image (listing, decompile or bytes), **[m]** computed
or counted by the reproduction script, **[i]** inferred.

## 1. Answer

- **No per-class near/far distances exist.** Every render node drawn through
  the sector camera uses one pair: `N` = the sector background's `FogNear`
  and `F` = its `FogFar`, raised to a floor that depends on the View Distance
  setting. Stations, ships, asteroids and gates fade identically **[s]**.
- The only class distinction is a **per-node exemption bit, `node+0x12c &
  0x02000000`**. The object constructor sets it for class 4 (TPlanets) only.
  The cut-scene loader sets it on scene parts that carry part flag `0x20`.
  Exempt nodes skip all three consumers: the material fade, the far cull and
  the fog-band walk **[s]**.
- Stations do fade before the far plane. How much depends on the sector and
  on View Distance (section 4). At "Very High" alpha at 100 km is still
  0.94–0.95 in the common sectors. At "Low"/"Medium" it goes from 1 to 0 over
  5–20 km (FogFar − FogNear), and the object is then culled [m/i].
- A single-constant or single-compare patch cannot exempt one class. The draw-time
  decision has no class input. The per-class change is a 7-byte trampoline at the end of the
  object constructor. For classes 5/6 it sets the engine's own exemption bit
  on the station's node tree, the way the constructor already does for
  planets (section 6).

## 2. The producer in material submission `0x004c0150`

All addresses **[s]**. `EDI` = per-subset descriptor (cached effect handles),
`[EBP+0xc]` = submitted render node, `[EBP+0x10]` = camera, `EBX` = effect.

| Address | Operation |
| --- | --- |
| `0x004c2b43..0x004c2b50` | `TEST [camera+0x270],0x10000` / `JZ 0x004c2e08`: bypass when the camera has no fog. |
| `0x004c2b56..0x004c2b63` | `TEST [node+0x12c],0x02000000` / `JNZ 0x004c2e08` (bytes `f7 80 2c 01 00 00 00 00 00 02 0f 85 9f 02 00 00`): per-node exemption. |
| `0x004c2b69..0x004c2bb9` | `F_eff`: `cfg=*0x00606f34`, `v=[cfg+0x768]`. If `v>=3`: `max([camera+0x370], 0x1dcd6500)` (500,000,000). If `v==2`: `max(…, 0x05f5e100)` (100,000,000). Otherwise the raw value. |
| `0x004c2bbf..0x004c2c15` | `D`: integer differences node `+0xb0/+0xb4/+0xb8` minus camera `+0x30/+0x34/+0x38`, x87 squared sum, `sqrtf` `0x00412440`, float to int `0x0052b5d0`. |
| `0x004c2c1a..0x004c2c6f` | `ECX = F_eff − D` (stored `[ESP+0x4c]`), `F_eff` recomputed, `EAX = F_eff − [camera+0x36c]`, `CMP ECX,EAX` / `JLE 0x004c2cbc` (fade on). Bytes at `0x004c2c63`: `2b 86 6c 03 00 00 3b c8 89 44 24 18 7e 4b`. |
| `0x004c2c71..0x004c2cb7` | Fade off (`D < N`): `g_FogClip` (handle `+0x140`) `= (1,0,0,0)` through effect vtable `+0x88`, then `g_EnableFog` (handle `+0x158`) false at `0x004c2e4e..0x004c2e59`. |
| `0x004c2cbc..0x004c2cd5` | `IDIV` of `(F−D)<<16` by `(F−N)`, guarded by `F−D >= 0` and `F−N != 0`. The quotient is overwritten at `0x004c2cd7` without being read (dead code). |
| `0x004c2cd7..0x004c2cff` | `g_ZWriteEnable` (`+0x148`) false, `g_AlphaBlendEnable` (`+0x170`) true, through vtable `+0x58` (SetBool). |
| `0x004c2d01..0x004c2d3c` | Only if descriptor `+0x1a4 == 0` and `[ESP+0x80] == 0` (blending was not already on): `g_SrcBlend` (`+0x178`) 5, `g_DestBlend` (`+0x17c`) 6, through vtable `+0x68`. |
| `0x004c2d3e..0x004c2df2` | x87: `Ns = [camera+0x36c]·s`, `Fs = F_eff·s`, `s = *(float*)(ctx+0x2c)`. Writes `g_FogClip = (Fs/(Fs−Ns), 1/(Fs−Ns), 0, 0)`; the reciprocal is computed first and multiplied by `Fs`. |
| `0x004c2dfa..0x004c2e06`, `0x004c2e52..0x004c2e59` | `g_EnableFog` true (`PUSH 1`, SetBool through vtable `+0x58`); this is VS `b0`. |

`c39.x` is `g_AlphaValue` (handle `+0x188`). It is set by other steps, not by
the fade block. The parameter block sets it to 1 at `0x004c1771`. A nonzero
node `+0x13c` overrides it as value/255 at `0x004c2a72..0x004c2b41`. The
opaque no-glow path writes 0 at `0x004c3801`, but only when blending and alpha
test are both off, so never on the fade path
([asteroid note](asteroid-fog-temporal.md), [station note](station-material-distance.md)).
On a faded draw `c39.x` is 1 unless the node has an alpha override.

A node with `node+0x130 & 0x20000` takes the depth-only branch
`0x004c219d..0x004c2211` and never reaches this block. That is the fog-band
prepass in section 5.

## 3. Where N and F come from

**Camera copy [s].** In `0x004205e0`, at `0x00421533..0x004215aa`, the
sector index comes from `*(cockpit+0x54)+0x13c`. It selects row
`*0x00606fc0 + index·0xdb8`, the TBackgrounds table (class 2). The row
supplies `+0x148` FogNear (`0x00421548`, bytes `8b 94 08 48 01 00 00`) and
`+0x14c` FogFar. These go to sector camera `+0x36c`/`+0x370`, with `+0x368 =
FogNear ? 0xffffff : 0`. The `JL 0x004215a0` at `0x0042157a` reads flags
from `AND EAX,0xffffff`, whose sign flag is always clear. The branch is
therefore never taken, and `OR [camera+0x270],0x10000` runs every time this
block runs **[s]**. FogNear 0 does not occur in shipped data (census below),
so this has no visible effect.

**View Distance floor [s].** `cfg+0x768` is the launcher's "View Distance"
(`VideoViewDistance`; [LOD selection](lod-selection.md)). The same clamp is
repeated at five sites: `0x004c2b69`, `0x004c2c1a`, `0x004c2d3e`,
`0x0047d14e` and `0x0047eacf`. `N` is never clamped.

**Units.** 500 native units = 1 m. Shader/view units are native × `s`, with
`s` = float bits `0x3c23d708` ≈ 0.01 on every gameplay camera measured on
`object_fade` rows ([camera numerics](camera-state-and-frame-routine.md)). So 1 view unit =
0.2 m. The Run 27 constants are in **view** units. They decode to `N·s` =
250,000 and `F·s` = 5,000,000, which is N = 25,000,000 native (50 km) and
F = 500,000,000 native (1,000 km). They are not 0.5 km and 10 km.

**Run 27 reproduced [m].** `fogclip(25e6, 500e6, 0x3c23d708)` gives
`(1.0526316166, 2.1052636612e-7)`. Against the captured
`(1.0526316166, 2.10526366e-7)` the relative errors are 7e-12 and 6e-10.
Scale `0x3c23d70a` misses `y` by 2e-7. Run 27 was therefore flown at View
Distance ≥ 3 (`F` floored to 500 M), in a sector with FogNear 25,000,000
[i]. That is the 61-sector (25 M, 30 M) group; which of those sectors it was
is not identified.

**Effective pairs of the 239 shipped sectors [m]** (from
[sector-fog-census.csv](sector-fog-census.csv); km = native/500,000). FogNear
is inside the sector bounds in 51 sectors.

| Sectors | FogNear km | FogFar km | F_eff km, View Distance < 2 | = 2 (High) | ≥ 3 (Very High) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 66 | 100 | 110 | 110 | 200 | 1,000 |
| 61 | 50 | 60 | 60 | 200 | 1,000 |
| 32 | 45 | 50 | 50 | 200 | 1,000 |
| 25 | 1,000 | 1,000 | 1,000 | 1,000 | 1,000 |
| 10 | 90 | 110 | 110 | 200 | 1,000 |
| 7 | 22 | 34 | 34 | 200 | 1,000 |
| 6 | 23 | 25 | 25 | 200 | 1,000 |
| 4 / 4 / 4 | 6 / 17.4 / 31 | 7 / 20.4 / 39 | same as FogFar | 200 | 1,000 |
| 20 (9 pairs) | 4.7–200 | 7.7–250 | same as FogFar | 200 (250 for the 200/250 pair) | 1,000 |

When `F_eff == N`, as in the 1,000/1,000 sectors, the fade condition is
`D ≥ F` and the far cull removes the node at `D > F + r`. The block never
divides by zero inside a sector, because `D` never reaches 1,000 km there [i].

## 4. Per-class result

The constructor `0x0043ffa0` has one caller, the allocation wrapper
`0x0043f900`, which has 20 call sites [s]. It is the only place where class
identity meets the render node. Its root-flag word goes into `node+0x12c` at
the end of the pre-switch: `0x2000020` for class 4 (`0xa000020` for subtype
`0x8b`), `0x20000` for classes 5, 6 and 7, `0x104000` for a sun whose TSuns
first field is below 1, and 0 otherwise [s]. In the post-switch, class 4 alone calls the tree
visitor `0x00487150(0x00487190, node, 0x02000000)`: bytes `68 00 00 00 02 50
68 90 71 48 00` at `0x004418d7`, the only `PUSH 0x2000000` in the function
[s]. A program-wide scan of immediate operands found only these other writers
of the bit [s]. The
cut-scene part loader `0x0045f270` sets it at `0x0045f74d`, `0x0045f9aa` and
`0x0045fa96`, plus children through `0x0047c600(0)`, when part byte `+0x4c &
0x20` is set. The per-object attached-effect instancer `0x00414cf0` ORs
data-driven record flags into new effect nodes. `0x0047c600` would clear the
bit only if called with a nonzero argument, and every call passes 0. Most
register-operand writers of `+0x12c` were decompiled and OR other bits.
Three operands stay unresolved: `0x004517a0` (`OR [ESI+0x12c],EDI` from an
argument), `0x00489bf0` (`OR [EDI+0x12c],EBX`), and the wholesale stores in
`0x00479d10`/`0x00493b40`.

| Class (type table) | Exempt bit | N | F | Fades before the far plane |
| --- | --- | --- | --- | --- |
| 5 TDocks, 6 TFactories (stations) | no [s]; root gets `0x20000`, and `0x80000000` when type `+0x5c == 0x12` (`0x00441639..0x00441644`) | sector FogNear | F_eff | yes [s] |
| 7 TShips | no [s] | same | same | yes [s] |
| 17 TAsteroids | no [s] (`+0x1dc = 10`, `+0x130 |= 0x4000000`) | same | same | yes; Run 27 captured it [m] |
| 18 TGates | no [s] | same | same | yes [s] |
| 4 TPlanets | whole tree [s] | — | — | never; also never distance-culled |
| cut-scene parts with part flag `0x20` | subtree [s] | — | — | never |
| attached effects (`0x00414cf0`) | from effect data | same unless the data sets the bit | | not surveyed |
| 3 TSuns, 2 TBackgrounds | not set; suns build light and flare nodes | — | — | not determined; the background view uses its own camera [i] |

**Station magnitude [i].** With vertex distance `d`, alpha is `saturate((F_eff − d)/(F_eff − N))`.
In a 50/60 km sector:

- Very High: 1.0 at 50 km, 0.989 at 60 km, 0.947 at 100 km. The draw
  still switches to blended, depth-write-off state as soon as the part origin
  passes 50 km.
- High: 0.667 at 100 km.
- Low/Medium: from 1 at 50 km to 0 at 60 km, then the part is culled at
  60 km + r.

Earlier captures saw no station draw with `b0 = 1` (Runs 11, 28, 48, 49;
[station note](station-material-distance.md)). This fits stations lying
inside `N` in those sectors; it does not show that stations are exempt.

## 5. The three consumers of the pair

| Consumer | Distance and radius | Test | Effect |
| --- | --- | --- | --- |
| Per-node cull in `0x0047cfe0`, `0x0047d117..0x0047d195` [s] | `D` = helper `0x0042f850` (node `+0xb0` to camera `+0x30`); `r` = cached subtree radius `0x00488170` (`node+0xa4`) | skipped if the camera has no fog flag or the node is exempt (`0x0047d146`, bytes `f7 c2 00 00 00 02 75 4d`); else unsigned `D > F_eff + r` | clears the renderable bit `+0x12c & 2` |
| Scene walk `0x0047e920`, `0x0047ea28..0x0047eb63` [s] | own `sqrtf` `D` and `r = 0x00488170` for each top-level node of the layer | same fog and exemption test (`0x0047ea38`); near test `D + r < N` (signed, `0x0047eaac`); far test `D − r > F_eff` (signed, `0x0047eb0f`) | Frame routine `0x00472280..0x004722a8` runs the walk twice when camera `+0x270 & 0x40000` is set. First with flag 4, which submits only fog-band nodes with `node+0x130 |= 0x20000` (depth-only prepass). Then with flag 0, which submits everything except nodes beyond `F_eff + r`. |
| Material block (section 2) [s] | `D` of the **submitted** node origin, no radius | `D ≥ N` (as `F−D ≤ F−N`) | fade state and constants |

## 6. The threshold test (node origin) and patch sites

**Test [s].** The origin decision is `CMP ECX,EAX` / `JLE` at
`0x004c2c69`/`0x004c2c6f`, with `ECX = F_eff − D` and `EAX = F_eff − N`. It
is signed 32-bit arithmetic. For in-sector distances it is exactly `D ≥ N`,
where `D` is the truncated float distance of the submitted node's origin.
The threshold comes from the sector background row (section 3). There is no
radius, class or LOD term, and each child part node decides separately.

**No single constant, table entry or compare is per class [s].** The draw-time
decision reads only camera fields, the configuration word and node flags, and
no node flag identifies a station. Three single-site changes exist, and none
is a class opt-out:

- `0x004c2b63` `JNZ rel32` → `E9 A0 02 00 00 90` (JMP `0x004c2e08`). This
  disables the material fade for **every** class. The far cull and the
  band prepass stay: nodes still disappear at `F_eff + r`, and the prepass
  still costs its draws. The instruction starts at `0x004c2b63` and is 6
  bytes. No reference into `0x004c2b59` or `0x004c2b63` exists. Flags come
  from the `TEST` before it and die at the target, whose `CMP EAX,ESI` sets
  them again.
- Editing the TBackgrounds rows, or the five floor constants, moves N/F for
  every class in a sector.
- The `OR [EAX+0x12c],0x80000000` immediate at `0x00441644` covers only
  types with `+0x5c == 0x12`, and only the root node. Bit `0x80000000` also
  selects the fixed-constant LOD branch ([LOD selection](lod-selection.md)).

**Per-class site: the constructor tail `0x00441d22` [s].** The instruction
there is `OR dword ptr [EBP+0x40],0x80000004`, 7 bytes `81 4d 40 04 00 00
80`. A 5-byte JMP to a proxy trampoline would do the following:

1. If class word `[EBP+0x48]` is 5 or 6 (optionally also the wreck classes
   `0x1d`/`0x1e`) and `node = [EBP+0x70]` is nonzero, call
   `0x00487150(0x00487190, node, 0x02000000)`. Load ECX from `*0x00608518`
   as the engine does before that call; the callee is callee-cleaning
   (`RET 0xc`).
2. Execute the displaced OR.
3. Jump to `0x00441d29`.

- **Boundary [s].** The site is reached by fall-through from the visitor
  call at `0x00441d1d` and by `JZ` at `0x00441cfc` and `0x00441d0d`.
  Nothing references `0x00441d23..0x00441d28`. Six early-failure jumps go to
  `0x00441d29` and skip the site; they are argument range checks where no
  object was built.
- **Liveness [s].** EAX, ECX and EDX are dead. The epilogue overwrites ECX
  at `0x00441d29`. The caller `0x0043f93b..0x0043f946` overwrites EAX, ECX
  and EDX before reading them. EBX, ESI and EDI are restored by the POPs,
  so their live values are dead too. EBP must survive, because the displaced
  OR reads it. ESP must be unchanged at `0x00441d29`, because
  `[ESP+0x14c]` is read there. EFLAGS are dead: the displaced OR writes
  them, then `ADD ESP` at `0x00441d3b` and `RET 0xc` follow. The caller runs
  only MOV/LEA/CALL/ADD before its next flag writer.
- **Callee [s].** `0x00487150` preserves EBX, EBP, ESI and EDI. The visitor
  `0x00487190` reads its two cdecl stack arguments, clobbers EAX and ECX,
  and does `OR [node+0x12c],value`. It recurses over `node+0xc` children.
  It takes no locks and makes no D3D or proxy calls.
- **Reentrancy [i].** The constructor runs on the game thread for each new
  object: universe build, script `SA_AllocObject` (`0x00460630`) and
  cut-scene instancing, all through `0x0043f900`. The trampoline touches only
  the new object's tree. It is not reentered.
- **Effect.** An exempt station takes the planet contract. It draws with the
  opaque, depth-writing pair: `g_FogClip = (1,0,0,0)`, `b0 = 0`, baseline
  Z-write on and blend off, and mask 15 with `g_AlphaValue = 0` from the
  no-glow path. That holds until the frustum, the projection far plane and
  the small-object `+0x1d8`/`+0x1dc` culls remove it. It is no longer
  culled at `F_eff + r` and gets no band prepass. At View Distance < 2 in
  the 51 sectors with FogNear inside the bounds, distant stations therefore
  stay drawn and cost draws that the engine used to cull [i].
- **Portability.** This is an in-process code patch: `VirtualProtect` plus
  `FlushInstructionCache`, guarded by the site bytes above. The same code works on
  native Windows. It must be installed before the first object is built
  (at proxy attach); objects built earlier keep their flags.

## 7. Unknowns

- Whether nodes attached to a station after construction get the bit. The
  attach path `0x00489da0` does not propagate `+0x12c` [s], so such children
  would still fade [i].
- Whether any attached-effect record (`0x00414cf0`) or loaded scene stream
  (`0x00479d10`, script `0x00493b40` case `0x3b` wholesale flag writes)
  carries `0x02000000` in shipped data.
- Where camera `+0x270 & 0x40000` (the band prepass) is set, and what the
  backgrounds camera carries.
- Which sector Run 27 was flown in, beyond the FogNear 25 M group.
- The visual acceptability of the station opt-out: no flight has tested it.

## Reproduce

```sh
python3 verification/results/distance-fade-decode.py   # 8/8 site bytes, Run 27 decode, census table
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -noanalysis -readOnly -scriptPath tools/analysis \
  -postScript X3ListRange.java <out> 004c2b20:004c2e70 004214f0:004215c0 0047e920:0047ecb0 \
  0047cfe0:0047d1b0 004418b0:004418f0 004415b0:00441660 00441d00:00441d50 0047c600:0047c640
# refs: -postScript X3CameraState.java <out> txt:0x2000000 data:0047c600 data:0047e920 \
#   data:0043ffa0 data:0043f900 data:00441d22 data:00441d23 ... data:00441d29
# decompile (local only): -postScript X3DecompileFunctions.java <out> 0043ffa0 0045f270 00488170
```
