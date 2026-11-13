# FL muzzle flash: how the game's flash body is built and drawn, and the state at its draw

Bounded offline contract, 2026-11-09, for re-shading the game's own muzzle flash in the proxy (position, life,
animation and scale stay engine-owned). Inputs: `X3FL.exe` SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` (`/mnt/c/X3FL/X3FL.exe`), the FL installation's
catalogues (`/mnt/c/X3FL`, `addon2` layered) and `/mnt/c/X3AP-orig` for the AP data twin. **Every FL address is an
RVA** (VA at the preferred base `0x400000` minus `0x400000`). **[s]** static instruction or data evidence, **[i]**
inference, **[u]** not established. No production edit, no build, no Wine, no game. The muzzle site and spawner ABI
are in [weapon-effect-spawn.md](weapon-effect-spawn.md) section 2.5; the material submit ABI is in
[material-submit.md](material-submit.md) section 1; the classic state block is in
[classic-materials.md](classic-materials.md) and its AP twin [non-effect-materials.md](../non-effect-materials.md).

**Answer.** The muzzle flash is **not a bullet-batch body and not a sprite**. TBullets column 21 names an Effects row
(the IRE's is **row 22**). Its first element is a `CUT` element: a **scene** (`objects/cut/04251`) with one part, body
**445** (`objects/v/00445`, part name `B_Muendung`). That body is a fixed **3D mesh** of 50 textured quads. It is
drawn as an ordinary scene node through the traversal and the material submit, `0x0968c0` → `0x0e6ef0` →
**`0x0e7142` → `0x0e0f30`** → `DrawIndexedPrimitive` at `0x0e604d`. It uses the **classic `MATERIAL3` path**:
`standard_lighting` technique `DEFAULT`, texture `tex/true/384.jpg` (a 256×256 atlas of five flash sprites; body 445
maps its top-left cell), and the Materials row 384 flags `0x8012`, which give **additive ONE/ONE, z-write off, z-test
on, no culling**. The engine animates only the node transform: each update re-places the scene root from the ship
node and sets its uniform scale on a grow-and-shrink parabola over 150 ms. There is no alpha fade, no vertex
colour and no UV animation. At the draw the proxy's existing FL object-trace scope (open inside `0x0e0f30`) sees
the part node, and **node `+0x144` = 445** identifies it. Body 445 is used only by scene 04251. That scene is also
a flash element of five other muzzle rows (flak, PPC, PSP, MAM, CIG) and of Effects rows 26 and 54. The AP engine uses the same
path and the same data.

## Reproduce

```sh
python3 -I verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_draw_anchors.py \
  > verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_draw_anchors_out.txt   # 83 checks, 0 failures
python3 -I verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_census.py \
  > verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_census_out.txt         # FL then AP; deterministic
```

The [anchor script](../../../verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_draw_anchors.py) reuses the
hash-checked PE/Capstone reader of `fl_weapon_effect_sites.py` and asserts 77 whole instructions and 6 strings. The
[census](../../../verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_census.py) reads TBullets, Effects, the
`objects/cut` scenes, the part bodies and the Materials rows of both editions. It prints ids, flags, counts and UV
rectangles only. Raw listings stayed in the session scratchpad.

## 1. Draw path (question 1)

| Step | RVA | What happens |
| --- | --- | --- |
| muzzle site | `0x04f639` → `0x0150e0` | spawner, ECX = 4, EDX = gun slot; instance keyed (ship, 4, slot) ([weapon-effect-spawn.md](weapon-effect-spawn.md) 2.5) |
| re-arm | `0x015253` → `0x015400`, reached for a new instance and for a found one (key compare `0x015114..0x015121`, `0x015156`) | row 22 has `EDF_REINIT` (bit 4, `0x015406`). **Every shot destroys the element nodes** (`0x01543b`, `0x0a3300`), then sets the clock base `inst+0x28` = now + t0 (`0x015478`) and runs the element update `0x015490` |
| CUT start | `0x015673..0x015a99` | root node `0x0a2200` with `+0x144 = −1` (`0x0156d8`) and scale = size (`0x0156fa`). The scene record comes from `0x0aeaf0(element +0)` (`0x015834`). Per part (stride `0x58`, body `+0x30`): node `0x0a2200`, body `0x0a35e0(445)`, which writes **node `+0x144`** (`0x0a365d`), attached under the root (`0x0158b9`). Then `+0x258` = scene id, `+0x25c` = part index, `+0x12c \|= 0x40`, and the scene key animation `0x0aa950` (`0x015a6b`) |
| root attach | `0x015dfc..0x015dff` | `0x0a56b0(root, parent = ship node +0x1c)`: the root is a **sibling of the ship's node**, not a child of the ship or gun |
| batch pass 1 | `0x097973` → `0x085700` | rejects the root at once: `+0x144 < 0` (`0x085706`). The batch path also needs `+0x12c & 2` (`0x085715`), which neither node carries [i: I3DF 0 on row 22, parts get only `0x40`] |
| traversal | `0x097aa1` / `0x097ac5` → `0x0968c0` | recurses into the children (`0x097583`, `0x097594`) and submits the part immediately (`0x097035`) or through the deferred queue (`0x097702`); which of the two a blended classic takes is **[u]** |
| material submit | `0x0e6ef0` → **`0x0e7142` → `0x0e0f30`** | the existing FL object-trace seam: ECX = descriptor, **EDX = node** at entry; inside, the node is at `[EBP-0x1c]` ([material-submit.md](material-submit.md)) |
| draw | `0x0e603b..0x0e604d` | `DrawIndexedPrimitive` (device vtable `+0x148`), `D3DPT_TRIANGLELIST`, one per technique pass ([pass-phases.md](pass-phases.md)) |

**Identity at the draw.** At `0x0e7142` and throughout `0x0e0f30` the object-trace scope's `current()` holds node
`[0, 0x154)`, which includes **`+0x144` = 445** [s: writer `0x0a365d`; i: the scope is the one the bullet traversal
fallback already relies on, [bullet-draw-batch.md](bullet-draw-batch.md) section 3]. So the D3D9
`DrawIndexedPrimitive` hook can match on the CPU-side body id with no new hook site. The scene id (`+0x258`) and part
index (`+0x25c`) lie outside the scope's window. **Hook-site suitability:** no new site is needed. The existing seam
`[0x0e7142, 0x0e7147)` keeps its documented contract: one whole `E8 rel32`, no HIGHLOW, ECX, EDX and the four stack
words live, all GPRs, EFLAGS, LastError and FP/SIMD preserved, and per-invocation SEH-safe scope nesting
([material-submit.md](material-submit.md) section 1).

## 2. Geometry (question 2)

| Item | Value | Source |
| --- | --- | --- |
| kind | **a fixed 3D mesh**, neither billboard nor shader-expanded. Its orientation is the instance orientation × the ship rotation every update (`0x01640c`; no `I3DF_2D` on row 22) | [s] code + data |
| body 445 | 203 coordinate vertices; 100 textured triangles (50 quads, all with the same UV quad) plus one untextured degenerate face; model z from 1196 to 99768 (the flash extends along +z, forward from the gun); one material | [s] data |
| UV | u 0.003..0.412, v −0.002..0.511: the top-left cell of `tex/true/384.jpg` (orange lightning streak) | [s] data |
| vertex format | the classic non-tangent body declaration. AP twin `0x0054e1d0`, stride `0x28` ([body-text-loader.md](../body-text-loader.md)); not re-derived on FL | [i] |
| draw | `DrawIndexedPrimitive(TRIANGLELIST, 0, 0, NumVertices, 0, PrimitiveCount)` from the body's static buffers; about 100 primitives; the runtime vertex count after the loader's split is **[u]** | [s] call; [i] counts |

## 3. Bound state, animation and life (question 3)

| Item | Value | Source |
| --- | --- | --- |
| effect / technique | `standard_lighting`, `DEFAULT` (no bump map), the two-sided instance (`flags >> 4 & 1`). `"effects"` replaces it only when node `+0x1c` is the cockpit view (`0x0e1a03`) | [s] `0x0e1a0f`; technique by the classic rule |
| shaders | the FL `standard_lighting` DEFAULT pair: PS FNV `ced7edef48059e84`, VS `494fe349b8bc12ec` ([classic-materials.md](classic-materials.md)). It is **shared with every opaque classic body**, so the shader alone does not identify the flash | [i] by technique |
| texture | `tex/true/384.jpg`, 256×256 JPEG (no alpha), diffuse slot. Also used by body 450 (PAC/MASS muzzle, the next cell) and body 314 (IonD scene 04261, scene 749) | [s] data |
| material flags | MAT3 → global Materials row = texture id 384 (`[0x21b418] + row·0x3c + 0x10`, `0x0e2d5b..0x0e2d63`): `MPF_DESTINATIONBLEND \| MPF_2SIDED \| MPF_BESTQUALITY` = `0x8012` | [s] code + data |
| blend | `g_AlphaBlendEnable 1`, **`g_SrcBlend 2` / `g_DestBlend 2` (ONE/ONE)** (`0x0e33a0..0x0e3427`), `g_BlendOp 1` (add) | [s] |
| depth | `g_ZWriteEnable 0` (`0x0e2f2a`), `g_ZEnable 1` (`0x0e2e8d`) | [s] |
| cull | `g_CullMode` 1 (none) from `MPF_2SIDED` | [i: AP rule; FL write at `0x0e2e0b` not value-traced] |
| fog | whatever `standard_lighting` DEFAULT applies to every classic body; not traced | [u] |
| colour | lit by the classic constant block (Materials row 384 emissive `0x7f` grey, diffuse 0); the exact constants for this row were not traced | [u] |
| state lifetime | the effect `Begin` uses `D3DXFX_DONOTSAVESTATE` ([pass-phases.md](pass-phases.md)), so a re-shade that changes device state must restore it itself | [s] |

**Animation.** Row 22 element 0 is `GROWANDSHRINK | TIMEOUT | CUT`, size a = 0.001 and b = 0.031, timeout 150 ms,
start 0, offset (0, 0, 150). Each update computes the size as follows:

* age = clock (cfg `+0x708`) − `inst+0x28` − element `+0x40` (`0x01661f`, `0x016623`);
* **t = clamp(2·age/timeout − 1, −1, 1)** (`0x01662a..0x01665e`);
* **size = b − (b − a)·t²**, floored at a (`0x0167c9..0x016828`).

It applies the size through `0x0a3920` (`0x0169ae`). That routine sets the root's `+0x70` and scales every child's
`+0x70` in proportion (`0x0a3955`). So the flash peaks at 75 ms and returns to a at 150 ms. Then the timeout ends the
element and its nodes are destroyed (liveness as on AP). `FADEOUT` is not set, so the node alpha `+0x13c` is not
ramped (`0x0169bd` requires `0x400`). There is no vertex colour (the classic body vertex carries none [i]) and no UV
or texture-frame animation: `MATERIAL3` has no `TexAnim` parameters. The scene part also has its own key track
(scene 04251: one position key and two `{18; 0.5; 0; 0; 1.0; 75}` keys, applied by `0x0aa950`). Reading those keys
as a roll about the part's z axis is **[i]**; the interpreter was not traced.

**Where the life fraction lives.** It is not on the node. It is (clock − `inst+0x28` − element `+0x40`) / element
`+0x10` on the effect instance, and the node has no back-pointer to the instance **[u]**. A proxy can recover the
phase from the part node's `+0x70` (inside the scope window). Its ratio to the body's base scale traces the parabola,
so it gives |t| but not the sign. The two halves are symmetric [i].

**Placement.** Every update re-writes the root's `+0x30` as ship position + R_ship·(instance position + element
offset) (`0x01626b..0x0163bd`), so the flash moves with the ship although it is not parented to it [s].

## 4. Sharing and draw count (question 4)

* **Body 445 is reached only through scene 04251**: no other `objects/cut` scene and no non-CUT Effects element names
  it [s data]. Ship and station scenes were not scanned for numeric body 445 [u].
* Scene 04251 is used by Effects rows **22** (element 0), **157** (flak muzzle, element 0, size 0.0053..0.55, 575 ms),
  **52, 190, 195, 202** (element 3 of the PPC, PSP, MAM and CIG muzzle rows, 775 ms), and **26, 54**. No TBullets
  column names rows 26 and 54; who spawns them is **[u]**. No impact, flight or death row uses the scene. Row 22 is the
  muzzle of TBullets rows 0 IRE, 9 PD, 18 PSG, 25 PBC and the fragments 37-40.
* So a re-shade keyed on node `+0x144 == 445` hits **only muzzle flashes** among the TBullets-reached rows, but
  across the eight row-22 weapons, the three flak rows (15, 16, 30) and the four element-3 rows. The per-body flash bodies of the other muzzle families are
  444 (row 24 HEPT/PAL/TRIBEAM/FUSIONBEAM, scene 04252; rows 152/191/193), 446 (scene 04253, nine rows), 447, 450 (PAC/MASS) and 314 (IonD) [s data].
* **One flash per gun**: one instance per (ship, kind 4, gun slot), and each shot tears down and rebuilds its nodes
  (REINIT). It is drawn once per traversal of its layer. `0x0978e0` has five callers (views and layers) filtered by
  `+0x1c0`, so the count is once per frame in the main view **[i]**.
* **Not parented** to the gun or ship node. It is a sibling of the ship's node, re-placed from the ship every update.

## 5. AP (question 5)

Same path: AP `0x00446268` → `0x00413c00` → `0x004148a0`(kind 4) → element start `0x00414cf0` CUT branch
(`0x00414e76`) → traversal → classic `0x004c0150`. The data are identical: row 22, scene 04251, body 445, texture
384, flags `0x8012` (census AP block) [s data; i: code by the twin addresses of
[weapon-effect-spawn.md](../weapon-effect-spawn.md) section 10].

## 6. Body 12009 (PRG) and the effects DEFAULT vertex shader (2026-11-13)

Scene 08001 (Effects row 183, TBullets row 3 PRG, three CUT elements) has one part, body **12009**. FL loads
`addon2/01.cat` `objects/v/12009.pbb`; 01.cat's text record has the same geometry, scaled to max |coordinate| 65536
[s data, `verification/results/muzzle-reshade/muzzle_bodies.py` `effect_material`]. It is MATERIAL6 `effects.fx`:
`t_DiffuseTexture fx_bullets2_diff`, `g_AlphaBlendEnable 1`, `g_BlendOp 1`, `g_SrcBlend 2`, `g_DestBlend 4` (ADD ONE /
INVSRCCOLOR), `g_CullMode 1`, `g_ZWriteEnable 0`, `g_AlphaValue 1` [s data]. Its 50 textured quads all map
fx_bullets2_diff's cell u 0.406..0.570, v 0.241..0.447 (the gun edge at v 0.447). They form three cones: 8 quads along
the axis, 98,800 units long; 18 tilted 41.6°, 54,390 long; 24 tilted 77.9°, 40,640 long. Each quad's gun edge lies 2
to 10 thousand units from the body origin [s data].

The engine binds the effects DEFAULT pair for it: VS `d5e1c75351ed3f04`, PS `8360f422de08b5bd`. Both come from
`shader/3_0/effects.fb` DEFAULT, the same as 2_0 [s]. The bind itself is [i]: the material submit's technique rule. The
VS, read token by token [s, `verification/results/muzzle-reshade/effects_vs_read.py`; the listing stays local]:

- **Constants (CTAB):** `g_mWorldViewProjection` c0..c3, `g_mWorld` c4..c6, `g_mViewInverse` c7..c9, `g_TexMatrix`
  c10..c11, `g_AlphaValue` c12, `g_FogClip` c13, `g_EnableFog` b0. Inputs `dcl_position v0`, `dcl_texcoord v1`.
- **oPos** = four dp4 of (v0.xyz, 1) with c0..c3.
- **oT0.xy** = dp3 of (u, v, 1) with c10 and c11.
- **oD0.xyz** = (f, 0, 0), oD0.w not written. f = c12.x without fog. With b0 set, f =
  saturate(c13.x − c13.y · |cam − P|) · c12.x, where P is (v0.xyz, 1) through c4..c6, cam = (c7.w, c8.w, c9.w), and the
  distance is computed as dp3, rsq, rcp, per vertex.
- **No other output.** The pixel program sees no position, so a re-shade that needs the distance from the gun must
  also bind its own VS.

The PS: texld s0 with t0, then the colour matrix c0..c2 applied to (rgb, 1) (dp4 per channel), then × v0.x; alpha is
the texel's. The CTAB names only `DiffuseTexSampler`; c0..c2 are filled by the effect's preshader [s].

## Unknown

- Immediate or deferred submit for this blended classic (`0x097035` vs `0x097702`), and the draw order against other
  additive effects.
- The vertex declaration and runtime vertex count on FL; the `standard_lighting` fog and the classic colour constants
  for row 384.
- The scene key interpreter `0x0aa950` (the meaning and rate of the 75-frame keys).
- The spawners of Effects rows 26 and 54.
- Runtime confirmation: nothing here has run in the game.
