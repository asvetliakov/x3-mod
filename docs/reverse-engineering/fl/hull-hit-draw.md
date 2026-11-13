# FL hull hit: what the engine draws at a bullet's impact, how it lives and how it is drawn

Bounded offline contract, 2026-11-13, for re-shading the game's own impact effect in the proxy (the user's rule:
the game's draw with our pixel shader, never custom geometry). Inputs: `X3FL.exe` SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` (`/mnt/c/X3FL/X3FL.exe`) and the FL catalogues
(`/mnt/c/X3FL`, `addon2` layered). **Every FL address is an RVA** (VA at the preferred base `0x400000` minus
`0x400000`). **[s]** static instruction or data evidence, **[i]** inference, **[u]** not established. No production
edit, no build, no Wine, no game. Builds on [weapon-effect-spawn.md](weapon-effect-spawn.md) (the impact site and its
frame), [muzzle-flash-draw.md](muzzle-flash-draw.md) (the element machinery for a CUT element),
[bullet-draw-batch.md](bullet-draw-batch.md) sections 6-7 and [beam-bullet-draw.md](beam-bullet-draw.md) section 5
(the two program pairs and the common fade block), [texture-lookup.md](../texture-lookup.md) section 10 (negative
texture ids, AP).

**Answer.**

1. **One impact row per weapon, for every target.** The engine reads exactly one column, TBullets col 20 (`+0x78`),
   from the projectile's type (TMissiles `+0x68` for a missile). It never selects by the hit object's class or
   shields (`0x014550`, `0x01461d`). So shield hit, hull hit, ship and station all get the same Effects row. All 45 FL rows
   have one; 26 distinct rows plus 5 PROPAGATE children draw **28 bodies**. Most of them are
   **camera-facing quads (`I3DF_2D`) with an effect material** (`standard_lighting.fx` or `effects.fx`, ONE / ONE).
   Twelve of them are 4 × 4-cell **flipbooks** (a negative texture id, so a `types/Animations` row). Eight carry
   classic MATERIAL3/5/6 materials. **v/00587** (classic 3D mesh, tex/true/380) and **v/10663** (the sparks flipbook,
   Animations row 73) cover **11 TBullets rows each, disjoint, 22 of 45 together**. After them come v/00469 (8),
   v/00564 and v/12000 (7 each) and v/10658 (6).
2. The effect is spawned by **the impact call our shield-hit hook already intercepts** (`0x053390` → `0x014440`).
   The routine places the effect at the **contact point in the hit object's frame**, with no surface normal. It
   runs the row's PROPAGATE children, then the kind-3 instance keyed on (hit object, 3, projectile id). Each element
   node is a sibling of the hit object's node, and every update places it again from the hit object's position and
   rotation, so it moves with the ship or station. Each element gets its size from its own Effects row (GROW,
   GROWANDSHRINK, random size, a size flicker every update). The size never depends on the target. The life is the
   element timeout or the end of a one-shot flipbook. FADEOUT ramps node `+0x13c` from 254 to 1. FADEOUTBODY does
   nothing here. LIGHT elements add a point light. There is no particle path: the "sparks" are flipbook sprites.
3. Each body is drawn alone, through the traversal and the existing object-trace seam `0x0e7142` → `0x0e0f30`, with
   node `+0x144` = body id. The program pair is **effects / effects2s DEFAULT** (`d5e1c753…` / `8360f422…`) or
   **standard_lighting DEFAULT** (`494fe349…` / `ced7edef…`, 2s `0d5848c8…`). The blend is the material's own
   (ONE / ONE for most). The animation lives in **per-draw constants**: `g_TexMatrix` = the flipbook cell offset
   (identity for a static texture), the frame's texture bound as `t_DiffuseTexture`, and `g_AlphaValue` = node
   `+0x13c` / 255 for fading elements. A pixel-shader re-shade keyed on the body id controls colour, falloff, grain
   and the shape inside the quad. It can rebuild the quad-local coordinate from the cell UV, and it gets the exact
   life fraction from `g_AlphaValue` on FADEOUT elements. Position, size curve, orientation, life, element timing,
   blend and Z state stay the engine's.

## Reproduce

```sh
python3 -I verification/results/fl-hull-hit-draw/fl_hull_hit_draw_anchors.py \
  > verification/results/fl-hull-hit-draw/fl_hull_hit_draw_anchors_out.txt   # 251 checks, 0 failures
python3 -I verification/results/fl-hull-hit-draw/fl_hull_hit_census.py \
  > verification/results/fl-hull-hit-draw/fl_hull_hit_census_out.txt         # FL; deterministic (md5 stable over two runs)
```

The [anchor script](../../../verification/results/fl-hull-hit-draw/fl_hull_hit_draw_anchors.py) reuses the
hash-checked PE/Capstone reader of `fl_weapon_effect_sites.py`. It asserts 227 whole instructions, 8 strings, the
1/255 constant, and 14 entries of the loader's EEDF name table at RVA `0x174398`, plus the absence of
`EEDF_ACTUALSIZE`. Every instruction this note cites is in it. The
[census](../../../verification/results/fl-hull-hit-draw/fl_hull_hit_census.py) reads TBullets, Effects (with the
PROPAGATE children), the element bodies through `bob1.resolve_body`, the CUT scene parts, the Materials rows,
`types/Animations` (`addon2/04.cat`, 106 rows), and the TMissiles, TShips, TDocks, TFactories and TAsteroids columns
that name a row. It prints ids, flags, counts and names only. Raw listings stayed in the session scratchpad.

## 1. Rows, bodies and materials (question 1)

**Which columns.** TBullets has a single impact column. The impact function reads col 20 (`+0x78`) of the
projectile's TBullets row (`0x01454b`/`0x014550`, or `0x01461d` when the victim is the projectile) or TMissiles
`+0x68` (`0x01455c`/`0x014561`). The other side is only the frame and the parent [s]. No column selects a shield
variant, a hull variant, a "big" variant or a station variant. The hit object's class is read only to tell which side
is the projectile (`0x0144d1..0x0144f5`); its subtype and shields are never read (`0x014440..0x014696`) [s]. The engine's only data switch between shield and hull is the `EEDF_HULLDAMAGE`
gate in the element start: it waits while the hit object holds 100 or more shield points (`0x0155ae..0x0155c0`).
No FL impact row or child uses it [s code, census]. So a "hull hit" is this row reaching the screen after a hit
that the shield stage did not take: our stage skips the native spawn for its own shield hits
([weapon-effects.md](../../architecture/weapon-effects.md) 2.2). Every other hit (hull, unowned frame, missile)
still draws this row.

**Census** ([`_out.txt`](../../../verification/results/fl-hull-hit-draw/fl_hull_hit_census_out.txt)). 45 of 45
rows have an impact row; there are 26 distinct rows. The PROPAGATE children are 143 (of 58), 145 (of 57), 146 (of
16), 147 (of 155 and 156) and 184 (of 186). The bodies are 28: 24 element bodies plus 4 scene parts (582 of scene
04274, 10388 of 04791, 10656 of 04262, 12007 of 08000). By kind:

* **2D billboards (`I3DF_2D`)**: 18 element quads. Scenes 04791 and 04262 have the flag on the scene root (the
  CUT start ORs it into the root only, `0x0156a3`); whether their parts face the camera is [u].
* **3D meshes**: v/00564, 00587, 00469, 00201, 00589, `bullet_ElectroSpike`, scene 04274 (101 quads of 582) and
  scene 08000.

| Body | Geometry | Material → effect / technique | Blend | Texture (flipbook: Animations row, frames × ms, pages) | TBullets rows (via Effects rows) | Also drawn by (non-impact rows) |
| --- | --- | --- | --- | --- | --- | --- |
| **v/00587** | 3D, 40 points, 20 faces | MAT3 tex 380, `DESTINATIONBLEND\|2SIDED\|BESTQUALITY` → standard_lighting 2s DEFAULT | ONE / ONE | tex/true/380, static | **11**: EMP, CIG, IPG, PAL, PPC, IONC, Tri-Beam, Fusion, Mining, EMP_1, IPG_FRAG (23, 53, 69, 70, 178, 180) | muzzle 179 (IPG), 181 (IONC); 187 |
| **v/10663** | 2D quad | standard_lighting.fx → standard_lighting DEFAULT | ONE / ONE | **row 73**: TAGONESHOT 32 × 25 ms = 800 ms, 4 × 4 cells, `exp_sparks1_diff` / `exp_sparks2_diff` | **11**: IRE, PRG, PBC, Tug, DUMMY2, Kyon α/β/γ, Repair, FLAK_ALPHA_FRAG, TAF_FRAG (15, 18, 182) | none |
| v/00469 | 3D, 40 points | MAT3 tex 380, as 587 | ONE / ONE | tex/true/380 | 8: EMP, CIG, PAL, PPC, Tri-Beam, Fusion, Mining, EMP_1 (23, 53, 69, 70) | none |
| v/00564 | 3D, 80 points | MAT3 tex 383, `DESTINATIONBLEND\|2SIDED` → 2s | ONE / ONE | tex/true/383 | 7: PAC, EMP, PAL, PPC, Tri-Beam, Fusion, EMP_1 (16, 23, 53) | muzzle 179, 181; 187 |
| v/12000 | 2D quad | effects.fx, cull 1 → effects2s DEFAULT | ONE / ONE | row 98: 48 frames, 1070 ms, `fx_blueexplosion_01..03` | 7: FBL, EMP, PAL, Tri-Beam, Fusion, FBL_FRAG, EMP_1 (23, 184) | none |
| v/10658 | 2D quad | standard_lighting DEFAULT | ONE / ONE | `exp_impact_glow_diff`, static | 6: Tug, DUMMY2, Kyon α/β/γ, Repair (18) | none |
| v/10661 | 2D quad | standard_lighting DEFAULT | ONE / ONE | `exp_glow_diff`, static | 5: IRE, PBE, PBC, 2 frags (15, 50) | missile impact/death 158; 36, 41, 142 |
| v/10664 | 2D quad | standard_lighting DEFAULT | ONE / ONE | `exp_glow2_diff`, static | 5 flak rows (155, 156) | **ship and station deaths 37, 201**; flak muzzle 157; 21, 56, 150 |
| v/10656 (scene 04262) | 2D quad | effects DEFAULT | ONE / ONE | row 82: 128 × 30 ms = 3840 ms, `fx_exp_A_p1..p8` | 5 flak rows (147 under 155/156) | **deaths 37, 48, 201, 149**; missile death 39; 20, 38, 47, 51, 142, 144, 148 |
| v/00582 (scene 04274, 101 parts) | 3D quads | MAT3 tex 380, 2s | ONE / ONE | tex/true/380 | 4: CIG, PPC, IONC, Mining (53, 69, 70, 180) | none (row 180 is also DUMMY2's muzzle) |
| v/12001 | 2D | effects2s DEFAULT | ONE / ONE | row 96: 48 × 30 ms = 1440 ms, `fx_electric_01..03` | 3: IPG, IONC, IPG_FRAG (178, 180) | none |
| v/00589 | 3D, 46 points | MAT3 tex 131, `DESTINATIONBLEND` → standard_lighting DEFAULT | ONE / ONE | tex/true/131 | 2: MASS, PRG (19, 182) | 55 |
| v/10388 (scene 04791) | 3D, 36 points, scene 2D | body record tex 873, `DESTINATIONBLEND\|2SIDED` → 2s | ONE / ONE | id 873 | 2: IPG, IPG_FRAG (178) | 46, 187 |
| v/10667 | 2D | standard_lighting DEFAULT | ONE / ONE | `fx_Lightning2_diff` | 2: IonD, ESR (145, 151) | 153 |
| v/10668 | 2D | standard_lighting DEFAULT | ONE / ONE | `fx_wave_diff` | 2: PD, PSG (143) | none |
| v/12004, 12005, 12008 | 2D | effects2s DEFAULT | ONE / ONE | rows 102 (MAM, 1296 ms), 103 (`fx_smallGreenExplosion`, 1680 ms), 105 (starburst, 960 ms) | MAM ×2 (196); EBC, ESR (192, 151); TAF ×2 (199) | none |
| v/12007 (scene 08000) | 2D quad parts | effects2s DEFAULT | **ONE / INVSRCCOLOR** | `fx_sphereshockwave_diff` | TAF ×2 (199) | none |
| `bullet_ElectroSpike` | 3D bolt | effects DEFAULT | ONE / INVSRCCOLOR | `fx_bullets2_diff` | ESR (151) | **it is the ESR projectile's own body** (TBullets col 0, row 13) |
| v/00201 | 3D | MAT3 tex 376, 2s | ONE / ONE | tex/true/376 | HEPT (17) | none |
| v/10659 | 2D | effects DEFAULT | ONE / INVSRCCOLOR | `exp_PL_imp_diff` | HEPT (17) | none |
| v/10653 | 2D | body record `ALPHATEST\|2SIDED` → standard_lighting 2s | **[u]** (alpha test, the blend branch not traced) | row 71: 16 × 50 ms = 800 ms, `exp_small_diff` | PBE (50) | none |
| v/10666 | 2D-flagged, 12 points (3 quads) | standard_lighting DEFAULT | ONE / ONE | `fx_Lightning_diff` | PAC (16, 146) | none |
| v/10670 | 2D | effects2s DEFAULT | ONE / ONE | row 83: 128 frames, 4900 ms, `fx_exp_B_p1..p8` | IBL (198) | **ship deaths 40**; missile impact/death 158-161 |
| v/12002, 12003, 12006 | 2D | effects2s DEFAULT | ONE / ONE | rows 100 (PSP, 1920 ms), 101 (Gauss, 1296 ms), 104 (`fx_surfacefire`, 2240 ms) | PSP (189), Gauss (194), IBL (198) | none |

Notes on the table:

* **Data sources.** Materials, blends, culls and flipbook rows are [s data]. The effect and technique column follows
  the material-draw rule of section 3 [s code].
* **Texture paths.** "tex/true/N" is the texture-lookup rule for a MATERIAL3 id with an empty Materials name [i].
* **"Named by none".** Rows 20, 21, 36, 38, 41, 46, 47, 51, 55, 56, 142, 144, 148, 150, 153 and 187 are named by no
  type column. Who spawns them is [u].
* **Element facts.** Per element (start, timeout, size a..b, `rndsize`, offsets, flags), see the census. One example
  is the IRE's row 15: element 0 is v/10663 (GROW from 0.04 to 0.06, RANDOMROT, FADEOUT, 800 ms) and element 1 is
  v/10661 (GROW from 0.01 to 0.2, FADEOUT, 250 ms).
* **Classic blend.** The classic MATERIAL3 blend follows the muzzle note's rule: `MPF_DESTINATIONBLEND` gives
  ONE / ONE [s AP, i FL beyond that flag].
* **Unknown flag.** `EEDF_ACTUALSIZE` (row 151, element 0) is not in the loader's name table (RVA `0x174398`, 30
  names, [s]). How the loader treats an unknown name is [u].

## 2. Spawn, placement, scale and life (question 2)

| Step | RVA | What happens |
| --- | --- | --- |
| site | `0x053390` → `0x014440` | the impact call inside the damage routine, the one our shield-hit hook already intercepts ([weapon-effect-spawn.md](weapon-effect-spawn.md) 2.1). A forwarded call is the hull-hit spawn |
| contact point | `0x01448a..0x0144cc` | the **smaller** body by radius `+0xa4` gives `p = node+0x30 + R(node+0x40) · node+0x180` (`0x0144a6` → `0x50e0c0`, `0x0144cc` → `0x412b80`). The meaning of node `+0x180` is still [u] |
| projectile side | `0x0144d1..0x0144f5` | the victim if it is class 0 or 10, else the source; neither → no effect |
| hit frame | `0x0144fb..0x014530` | `pos = Rᵀ(hit node) · (p − hit node +0x30)` (`0x50dfa0`). **No normal is computed or read**: the routine touches only object `+0x70` (node), `+0xa4`, `+0x48/+0x4a`, `+8`, node `+0x1c`, `+0x30`, `+0x40`, `+0x180` and the projectile node's `+0x40..+0x6c` [s] |
| children | `0x01456d` → `0x014c10` | PROPAGATE children are spawned first (AP `0x00414410`) |
| instance | `0x014583` → `0x015270` → `0x0150e0` | kind 3, k = projectile id `[proj+8]` (`0x014572`), eff = the row, obj = hit object, &pos, t0 0, no orientation (`0x0152dd..0x015323`). `EEDF_PUSH` resolves a source first (`0x0152a6`) |
| key | `0x015114..0x01511e` | (obj, kind, k). A new bullet makes a new instance (`0x015205` → `0x0153a0`: nodes cleared, clock base `inst+0x28` = now (`0x0153e0`), first update). The same projectile hitting again (a beam) finds its instance. It replaces `inst+0x30` with the new contact (`0x015211`) and re-arms (`0x015253` → `0x015400`): with no `EDF_REINIT` (none of the impact rows has it) the nodes are kept and the clock base is reset (`0x015478`), so the elements restart at the new point [s; i: the projectile id is unique per bullet] |
| bullet rotation | `0x014595..0x0145b7` | instance `+0x70..+0x9c` = projectile node rotation, `+0x14` = projectile (for BULLETROT) |

**Element start** (`0x015490`, body branch `0x015c34..0x015dff`, the CUT branch as in the muzzle note):

* **Node and body.** A node is made by `0x0a2200` with `+0x12c |=` the element's I3DF word (`0x015c5f`). The body
  is set by `0x0a35e0` (node `+0x144` = body id, `0x0a365d`). Setting the body also builds the node's
  texture-animation instances: the group walk `0x0a2eb0` (`0x0a36d4`) writes count `+0x1a8` (`0x0a305f`) and array
  `+0x1ac` (stride `0x80`, `0x0a308c`/`0x0a3094`) for every negative group index [s].
* **Start size.** `RELATIVESIZE` gives the radius; otherwise size a + the `RANDOMSIZE` offset (random in
  [0, b − a), `0x015632..0x015654`), set by `0x0a3920` (`0x015cb9`).
* **Orientation.**
  - `RANDOMROT` on a 2D node: a random roll (`0x015cd7`, `0x015d06`).
  - `RANDOMROT` on a 3D node: three random angles (`0x015d77` → `0x0a3cd0`).
  - `BULLETROT`: the projectile rotation (`0x015d82..0x015da7`).
  - Otherwise: the instance orientation (identity, `0x0151d7`) × the hit object's rotation (`0x015db9`).
* **Parent.** The node is attached under the hit node's `+0x1c` (`0x015dfc`/`0x015dff`), the same as the muzzle
  root: **a sibling of the hit object's node, not a child**. A LIGHT element gets a light node attached the same way
  (`0x015be5`/`0x015be8`).
* **PUSH.** A class-7 ship gets an impulse along the bullet (`0x015e75..0x015e9f`, then the position update). This
  is not a look.

**Every update** (manager `0x016f40`, called at `0x0782c5`, walks every instance through `0x015490` at `0x0172a2`;
once per game update [i]):

* **Position.** Node `+0x30` = hit position + R_hit · (contact `inst+0x30` + element offset) (`0x01626b..0x0163bd`;
  offset × radius for RELATIVESIZE, `0x016292`). So **the effect moves and turns with the hit ship or station** [s].
  The element offsets (row 23 element 4 z −500, rows 17 and 182 z −500 or −200, row 198 element 2 (0, 1000, 1000))
  are in the **hit object's axes**, not along the bullet [s].
* **Orientation.** A 2D node keeps its start roll (`0x0163f0`). BULLETROT gets the projectile rotation again
  (`0x0163c6..0x0163ea`). Every other node gets (identity × hit rotation) again (`0x01640c`; `0x50ebe0` reads only
  its two inputs). The 3D random rotation set at the start is therefore replaced in the same call, before the first
  draw: the 3D impact meshes (587, 564, 469, 201) are drawn aligned with the hit object's axes [i: strong, from the
  call order `0x015e04` → `0x016258`]. v/00589 in row 182 is the same; in row 19 it is BULLETROT.
* **Camera facing.** At the view transform a 2D node's `+0xc0` = its own `+0x40` (the roll) × the camera rotation
  (`0x094b72..0x094b8b`) [s]. The node therefore faces the camera [i].
* **Size.**
  - GROWANDSHRINK: `b − (b − a)·t²`, t = clamp(2·age/timeout − 1), as on the muzzle (`0x016600..0x016828`).
  - GROW: `a + (b − a)·min(age/timeout, 1)` (`0x01682d..0x016967`).
  - Neither: a.
  - All three add the random-size offset.
  - **Then, every update, size × (1 − random[0, `rndsize`))** when 0 < `rndsize` < 1 (`0x01678b..0x0167c4`), which
    is a per-frame size flicker. Elements of rows 16, 23, 53, 69, 70, 145, 151, 178 and 180 carry 0.1 or 0.2, row
    18's v/10658 carries 0.3 [s code, census; i "flicker"].
  - Set by `0x0a3920` (`0x0169ae`).
  - **The size is per element of the weapon's row, never per target class**: none of the impact elements is
    RELATIVESIZE [s census].
* **NOZTEST.** Node `+0x130 |= 8 | 0x10` (`0x016770`, `0x01677e`). A node with `+0x130 & 0x10` always goes to the
  deferred traversal queue (`0x097027`). Its depth state at the draw is [u].
* **FADEOUT** (element `+0x3c & 0x4400 == 0x400`: FADEOUT and not PROPAGATE, `0x0169bd`/`0x0169c2`). The node alpha
  is `254 − round(255 · age/timeout)`, floored at 1 (`0x016a0f..0x016a38`). It is written to node `+0x13c` and to
  every child (`0x016a3e`, `0x016a50`) [s]. (The muzzle note's anchor comment "FADEOUT without 2D" at `0x0169bd`
  should read "FADEOUT without PROPAGATE": bit `0x4000` of the element flags is `EEDF_PROPAGATE`, name table
  `0x174398`.)
* **FADEOUTBODY** acts only for a class-`0x1b` carrier (`0x015579`). On an impact (a ship, station or other hit
  object) it is inert [s].

**Life.**

* **Element end.** An element node is destroyed when its `TIMEOUT` passes (start + timeout, `0x017302..0x017335`)
  or when the node is flagged finished `0x2000` (`0x0172eb`). The traversal sets that flag when the node's
  texture-animation stepper `0x1139a0` returns true (`0x096fcd`/`0x096fd6` immediate, `0x0976b7`/`0x0976c0`
  deferred) [s]. That this means "one-shot flipbook ended" is [i].
* **Flipbook versus timeout.** Every impact flipbook is `TAT_TAGONESHOT`, so a flipbook element lives for the
  shorter of its timeout and the animation:
  - v/12000 in rows 23 and 184 ends at 1070 ms, not 1440 or 1295 ms;
  - v/10663 ends at 800 ms in rows 15 and 18 and at its 360 ms timeout in row 182 [i].
* **Instance end.** The instance is freed when no element is alive.
* **Clocks.** The stepper and every element clock use the game clock cfg `+0x708` (`0x113aa7`, `0x015535`), so
  SETA shortens an impact in real time exactly like every other effect [s clock; i SETA].

**Visual consequence of FADEOUT.**

* **effects bodies:** the fade reaches the frame (RGB × `g_AlphaValue`).
* **standard_lighting bodies:** the fade reaches only the output alpha, so under ONE / ONE it is invisible. This
  covers v/10663, 10661, 10664, 10658, 10666, 10667, 10668 and the classic meshes. Their vanilla "fade" is the
  end of the flipbook or of the timeout ([beam-bullet-draw.md](beam-bullet-draw.md) section 5 PS writes) [i].

**Companions.** The companions are the row's own extra elements:

* LIGHT elements: point lights that light the hull and the standard_lighting sprites (rows 16, 17, 23, 53, 70, 178,
  180, 182, 184, 189, 192, 194, 196, 198, 199, 151);
* CUT scenes (04274's 101 quads, 04791, 04262, 08000);
* PROPAGATE children (delayed copies: 57 → 2-4 × row 145 at 50-100 ms gaps, 58 → 1-2 × 143, 16 → 146,
  155/156 → 1-3 × 147, 186 → 1-3 × 184).

**No particle emitter and no instanced batch is involved.** No impact element is `PARTICLEGENERATOR`. The batch path
needs node `+0x130 & 0x800000` (`0x08573a`), which only the bullet node builders set (`0x0836cd` and twins), or node
`+0x12c & 0x800`/`0x4000000` (`0x08571a`, `0x085732`), which no impact element sets [s; i: no register-OR writer
outside the scanned immediates].

## 3. The draw and what a pixel-shader re-shade can reach (question 3)

**Submit path** (as for the muzzle flash): view submit `0x0978e0` → traversal `0x0968c0`. The part is submitted
through `0x0e6ef0` either immediately (`0x097035`) or through the deferred queue (`0x097702`). Part `+0x60 & 0x8000`
skips the part in this pass (`0x09700a`). Part `+0x60 & 0x20`, node `+0x12c & 0x800` or node `+0x130 & 0x10` sends it
to the queue (`0x097016..0x097027`), which first takes a view-space point for its sort (`0x09703f`) [i "sort"].
Either way it reaches **`0x0e7142` → `0x0e0f30`** and then `DrawIndexedPrimitive` (`0x0e604d`), one draw per
material group (a 2D quad is 2 triangles) [s path; i which queue for a given body beyond the NOZTEST rule].
The FL object-trace scope open in `0x0e0f30` sees the node `[0, 0x154)`, so **node `+0x144` = the body id** at the
draw [s writer `0x0a365d`; i scope, as for body 445].

**Effect and technique** for an effect material (`0x0e175a..0x0e18ad`) [s]:

* **Effect.** `standard_lighting.fx` and `max_standard_lighting.fx` become `standard_lighting`; any other file
  keeps its own effect.
* **Two-sided.** `g_CullMode` = 1 (parameter semantic `0x11`, `0x0e1677`) appends `"2s"` (`0x0e17ce`).
* **Technique.** `BUMPMAP` for a bump id > 0, else **`DEFAULT`** (`0x0e1880`, `0x0e18ad`). The material file's
  technique word (1 in every impact body) is not what selects it.
* **Classic bodies.** These take the classic branch: standard_lighting, the two-sided instance for `MPF_2SIDED`,
  DEFAULT (`0x0e1a03..0x0e1a58`).

Resulting pairs (FL 3_0; [`lone_inputs_out.txt`](../../../verification/results/projectile-reshade/lone_inputs_out.txt),
[`beam_program_inputs_out.txt`](../../../verification/results/fl-beam-bullet-draw/beam_program_inputs_out.txt)):

| Bodies | VS | PS | What the pair passes the PS |
| --- | --- | --- | --- |
| effects / effects2s (v/10656, 10659, 10670, 12000-12008, ElectroSpike) | `d5e1c75351ed3f04` | `8360f422de08b5bd` (2_0 and 3_0, base and 2s; 2_b `f0c91793a75e1203`) | `oT0.xy` = (u, v, 1) · `g_TexMatrix` (c10/c11); `oD0.x` = `g_AlphaValue` (c12.x) × fog. PS: texel through the colour matrix c0..c2, × v0.x; alpha = texel.a |
| standard_lighting effect material (v/10658, 10661, 10663, 10664, 10666, 10667, 10668; all cull 2) | `494fe349b8bc12ec` | `ced7edef48059e84` | TEXCOORD = (u, v, 1) · `g_TexMatrix` (c37/c38); COLOR.xyz = point lights + `g_MatEmissiveColor` (white (1, 1, 1) in these materials [s data]); COLOR.w = `g_AlphaValue` (c39.x) × fog. PS: lit texel; alpha = lerp(texel.a, light.a, `g_EnableGlow`) × v0.w |
| classic meshes (587, 469, 564, 582, 201, 10388 2s; 589 1s; 10653 2s) | `494fe349b8bc12ec` | `0d5848c8489b6630` (2s) / `ced7edef48059e84` | as above, with the classic constant block |

**Per-draw constants set by the material draw** [s]:

* **`g_TexMatrix`.** Taken from the node's animation instance whose row equals the negated group index
  (`0x0e10ca..0x0e1162`: frame texture `inst+0x70`, UV matrix `inst+0x40`). It is uploaded as 16.16 through
  `0x4d9580("g_TexMatrix", …)` (`0x0e3e2b..0x0e3e8b`), else as identity (`0x0e3e67..`).
  - **Cells.** Every impact flipbook steps `TATF_COORDS` cell offsets u, v ∈ {0, 0.25, 0.5, 0.75} over a quad
    whose UV spans one cell (0..0.25 or 0.002..0.247) [s data].
  - **Pages.** The frame texture changes between pages (`exp_sparks1` / `exp_sparks2`, `fx_exp_A_p1..p8`, …).
  - **Upshot.** The animation is entirely in `g_TexMatrix` plus the bound `t_DiffuseTexture`, both written per draw.
* **`g_AlphaValue`.** The classic blended branch writes 1.0 (`0x0e2eca`); an effect material keeps its own 1.0
  (parameter `g_AlphaValue` 65536 in every impact effect material [s data]). When node `+0x13c` ≠ 0, the common block
  (`0x0e4abd..0x0e4b99`) sets ZWriteEnable 0, AlphaBlendEnable 1 and **`g_AlphaValue` = `+0x13c` / 255**
  (`0x0e4ad9`, `0x191a88`).
* **SrcBlend / DestBlend in the fade block.** These change only when the material blended neither through the
  classic flags (`+0x1a4`, set at `0x0e2ef2`) nor by its own `g_AlphaBlendEnable` (GetBool at `0x0e4a86`, tested
  at `0x0e4b17`). Every impact body blends, so **the blend stays the material's**: ONE / ONE, or ONE / INVSRCCOLOR
  for v/10659, v/12007 and `bullet_ElectroSpike` [s code; i ordering of the material's parameter writes before
  `0x0e4a86`; 10653 [u]].
* **Separate alpha.** The FL 68 effects DEFAULT rows measured separate alpha on
  ([bullet-draw-batch.md](bullet-draw-batch.md) section 7), so a re-shade that changes state must restore it itself
  (`D3DXFX_DONOTSAVESTATE`).

**Hook-site suitability.** A body-id re-shade needs **no new hook**. It uses the existing seam
`[0x0e7142, 0x0e7147)` under its documented contract ([material-submit.md](material-submit.md) section 1):

* one whole `E8 rel32` and no HIGHLOW;
* ECX, EDX and the four stack words are live;
* all GPRs, EFLAGS, LastError and FP/SIMD are preserved;
* the scope nests per invocation and is SEH-safe.

The draw runs on the render thread inside the engine's own call, so the scope adds no reentrancy. The recognition
is: the bound VS (`d5e1c753…` or `494fe349…`, decided at SetVertexShader), the blend shadow, and node `+0x144` in
an impact body set.

**What the re-shade can and cannot change.**

* **Ours.**
  - Colour, falloff, grain and output alpha.
  - The shape inside the quad. For a flipbook, the cell-local coordinate is `frac(4·uv)` (UV spans one 0.25 cell
    after `g_TexMatrix`) [i: every impact atlas is 4 × 4], so a procedural sprite can replace the texel.
  - The animation phase:
    - FADEOUT elements give the life fraction, f ≈ 254/255 − `g_AlphaValue` (to 1/255), read from COLOR0.x
      (effects) or COLOR0.w (standard_lighting), both times the fog factor when `g_EnableFog` is on [s ramp];
    - other elements carry only the flipbook frame, as the cell plus the bound page [i].
  - The ONE / ONE standard_lighting sprites' invisible vanilla fade can be made visible by the PS (RGB × alpha) [i].
* **The engine's.**
  - Position (the contact point, moving with the hit object), the element offsets, start delays, the number of
    elements and PROPAGATE copies.
  - The size curve and the per-frame size flicker.
  - Orientation: billboard roll, 3D meshes aligned with the hit object, BULLETROT.
  - Life (timeout or flipbook end).
  - Blend, Z test and Z write, unless the route also writes device state.
  - Draw order (deferred queue) and the LIGHT elements' point light on the hull.

**Collisions to handle.**

* **Shared bodies.** Node `+0x144` does not tell an impact from another row that draws the same body, so:
  - v/10656, 10664 and 10670 are also **ship and station death explosions** (rows 37, 40, 48, 201);
  - v/10661 and 10670 are missile impacts and deaths (158-161);
  - v/00564 and 00587 are the IPG and IONC muzzle rows 179 and 181.

  An element node carries no back-pointer to its Effects row [u]. Keyed on body id alone, a re-shade of these
  bodies applies to those effects too.
* **Impact-only bodies.** 10663, 10658, 10659, 10653, 10666, 10668, 00469, 00201, 00582 and 12000-12008 are drawn
  by no non-impact row the census found [s data; rows named by no table: [u]].
* **ESR.** The ESR impact draws `bullet_ElectroSpike`, which the lone bullet route's body map already matches (VS
  `d5e1c753…`, ONE / INVSRCCOLOR) [i].
* **Muzzle route.** The muzzle route's seven bodies (`muzzle_reshade_core.h`) contain no impact body [s].

## AP

Same machinery by the AP twins in [weapon-effect-spawn.md](../weapon-effect-spawn.md) sections 3 and 10: impact
`0x00449d85` → `0x00413c40` → `0x00414a70` → `0x004148a0`, element update `0x00414cf0`, texture animation
`0x00487810` / `0x004f66e0` / `0x004c0150`. Not re-traced here; the AP data were not censused [i].

## Unknown

- The meaning of node `+0x180` (the contact offset), so whether p lies on the hull surface.
- The depth state a NOZTEST node is drawn with, and which traversal queue each non-NOZTEST body takes.
- The blend of v/10653 (classic `ALPHATEST|2SIDED`).
- That the stepper's true return (`0x1139a0`) is exactly "one-shot ended".
- The loader's handling of `EEDF_ACTUALSIZE`.
- The spawners of the impact bodies' unnamed rows.
- A per-node discriminator between an impact node and a death node of a shared body.
- Absolute sprite sizes in metres.
- Runtime confirmation: nothing here has run in the game.
