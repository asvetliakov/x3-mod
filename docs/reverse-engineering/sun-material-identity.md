# Sun resources, effects and lens-flare ordering

2026-09-13. Bounded offline follow-up to
[the next-slice inventory](../architecture/material-next-slice.md#3-particle-billboards-sun-identification-still-missing).
No runtime change, Wine run, capture, build or game launch.

**The ordinary TSuns lens-flare path is identified: it uses a dedicated late
scene and the shared `effects` family, including legacy materials. It does not
establish a pre-bloom sun emitter.** Named sun planet models are a separate
TPlanets path. Shader identity alone still cannot distinguish suns, engine
effects, weapons and overlays.

## Resource and constructor chain

The complete 17-catalogue inventory contains `types/TSuns.pck` in root `03.cat`
and `addon/types/TSuns.pck` in `addon/01.cat`. Their decoded contents are
identical: 11 entries, SHA-256
`23bf07ef8efd04f101b4486acf299cd5ebd136957cc182802d13a58668593214`.
The corresponding Lensflares tables are also identical, with 35 groups,
SHA-256 `c98373778d2046e78f74d5fcc0a891e966bafc981ac1e15f58b9b5c94ea2b0a3`.
Thus root/addon precedence does not affect these two tables' recovered values.

Do **not** interpret the repeated TSuns value 884 as a body ID. The common
table parser stores that seventh field at record `+0x18`; the model selector
is the first field, stored at `+0`. Every installed sun row has model -1.
No body 884 exists in the complete catalogue inventory. The precise semantic
name of field `+0x18` is unnecessary here and is not inferred from its value.

| Site | Recovered connection |
| --- | --- |
| `0x00434e40`, TSuns selection at `0x004362bc` | Type 3 selects TSuns; records have stride `0xdb8` in the table rooted at `0x00606fc4`. The compact parser switch maps type 3 to case 2, not decompiler case 3. |
| `0x00438173–0x00438182` | Stores the sun resource field at record `+0x44` and the following integer lens group at `+0x48`. |
| `0x0043ffa0`, type-3 constructor | Reads record `+0` for the model; nonpositive selects fallback body 31 and flags `0x104000`. This is separate from the lens bodies below. |
| `0x00441712–0x004417c8`, within that constructor | Allocates the second sun node, sets node `+0x12c` bit `0x20000000`, copies TSuns record `+0x48` to node `+0x1a0`, and retains the resource from `+0x44` separately. |
| `0x0047e129–0x0047e143` | Collection admits a lens source only when node `+0x12c & 0x20000000` and view `+0x270 & 0x100` are both set. |
| `0x0047e264–0x0047e30f` | Finds/allocates a 0x70-byte record in the view's `+0x2a0` list. Copies source node `+0x1a0` into record `+0x38` at `0x0047e2ed/0x0047e2f3`; records retain their source node and view. |
| `0x0046f5c0` | Loads Lensflares: 8-byte group entries, with 20-byte body records containing body resource, position factor, two size values and rotation factor. Group storage is renderer root `+0xb0`. |
| `0x00471660` | Indexes that table using lens record `+0x38`; instantiates each body through `0x00486d10` / `0x00487e30`, attaching it to the dedicated lens scene with `0x00489da0`. |

The first nine TSuns entries select groups 0–8. Together these have 103 body
occurrences and 36 unique body resources. Entries 10/11 also carry table group
0; their special semantic comments are not proof that all their runtime
suppression rules have been recovered. This note does not certify those rules.

## Actual material resources

All 36 bodies are present in root `01.cat`, under `objects/v/NNNNN.pbd`.
Twenty-four use legacy MATERIAL3 with nonzero texture IDs among
357, 358, 360, 361, 362 and 363; the matching assets are
`tex/true/<ID>.jpg`. Some bodies also declare an unused-looking texture-0
material; declaration alone is not proof that a face submits it.

Twelve newer bodies use MATERIAL6 with explicit `effects.fx`:

| Body IDs | Diffuse texture | Sun group |
| --- | --- | --- |
| 11000–11003 | `fx_yellowflare.dds` | 1 |
| 11004–11007 | `fx_redflare.dds` | 2 |
| 11008–11011 | `fx_greenflare.dds` | 3 |

Each specifies technique 0, `colormatrix=false`, `Tex2=false`, material alpha
1, RGB/alpha color mask 15, blending enabled with ADD / SRCALPHA /
INVSRCALPHA, Z enabled, Z writes disabled, alpha test disabled and cull NONE.
These are **asset parameter values**, not a claim that no later view/material
override changes the effective device state. They do establish that the
authored sun materials are not uniformly additive. The three packed texture
assets are `dds/fx_yellowflare.pck`, `dds/fx_redflare.pck` and
`dds/fx_greenflare.pck` in root `01.cat`.

Legacy materials have a stronger connection than a filename guess:
`0x004c0b06–0x004c0b2b` compares the object's scene pointer with renderer
root `+0x64`. Equality selects the literal effect name `effects`; other
ordinary legacy geometry selects `standard_lighting`. This occurs in the
legacy branch of material submission `0x004c0150`, before technique lookup.
Explicit MATERIAL6 resources take the named-effect path instead.

The already inspected stock `effects` DEFAULT base pair is VS
`d5e1c75351ed3f04` / PS `8360f422de08b5bd` (VS2/PS2); its INSTANCE VS is
`89193868c61c3846`. The stock `shader/3_0/effects.fb` and its hueshift-off,
hue-lights-off and vertex-lights-off aliases all contain these same base
programs. Other explicit permutations are documented in
[the emission design](../architecture/linear-emission-composition.md).
This proves the family/resource connection. It does **not** assign one exact
runtime pair to every flare: technique selection, effect variation and
material/view overrides remain inputs to the material routine. In particular,
this investigation has no draw-to-resource identity observation.

## View, visibility and bloom boundary

`0x004714c0` creates the explicitly named `Lensflare Scene`, retained at renderer
root `+0x64`, and an auxiliary body-195 object at `+0x6c`. Ordinary scene loops
explicitly skip that scene. `0x004715d0`, called at `0x00472370`, performs
cross-view visibility checks through `0x00488720` unless the current view's
`+0x270 & 0x1000` suppresses that step. The latter helper uses screen bounds,
view orientation and scene intersection helpers; it is not a shader-time
visibility calculation.

`0x00471660` changes each lens record's visibility accumulator by 100 and
clamps it to 0–200, deleting bodies at zero. Active bodies are positioned from
screen coordinates and lens-table position factors, scaled by viewport size,
and rotated from the lens-table factor. This is a camera/view-dependent lens
effect, not established world-geometry motion suitable for a camera-only
temporal rule.

After the entire sorted ordinary-view loop and its secondary geometry pass,
the enabled lens scene executes:

| Site | Operation |
| --- | --- |
| `0x00472442` | Update/instantiate lens bodies (`0x00471660`) |
| `0x00472451`, `0x00472461` | Select lens view and camera/state setup |
| `0x00472471`, `0x00472484` | Visibility preparation and geometry collection |
| `0x0047248b`, `0x00472491` | Sort and traverse ordinary material dispatch |

Stock bloom `0x004c4750` is conditionally called at `0x004721b1` on crossing
the ordinary view-order boundary. Therefore this lens-scene traversal is
**after any such invocation**, not a pre-bloom emission bucket. If that branch
does not execute, the claim is only the static late ordering; it does not
invent a bloom call. The shared material routine can also override Z and
blending from view/node state and can inherit unassigned pass state, as covered
by the [late-state study](bloom-late-view-state.md).

## Separate planet, nebula and stardust paths; remaining edge

`TPlanets` independently maps `SS_PL135` to
`environments/planets/planet_sun_scene` and `SS_PL147` to
`environments/planets/planet_sun2_scene`. Those mappings agree in the three
catalogue versions inspected. Four named sun planet scene resources exist;
bounded embedded material-string inspection finds `XT_standard_lighting.fx`
in all four, and `effects.fx` additionally in `planet_sun_scene` and
`planet_sun3_scene`. That is not a decoded submesh/material binding or a proof
that every named resource is instantiated in the user's sector.

The historical background VS `7b6393fe2d3e1d85` / PS `6109cf64c03529dd`
is a GUI/nebula alias; stardust uses PS `0a523f33ac47ae05`, also shared with
GUI. Neither is identified as a sun by this resource/code chain. Existing
[background evidence](background-temporal-coverage.md) remains unchanged.

The unresolved edge is now bounded: **which submeshes of the separate
TPlanets sun scene, or other sector-instantiated resources, produce the visible
pre-bloom sun in a particular scene, with which effective shader pair and
blend state?** The ordinary TSuns flare chain above cannot answer that question
because its proven draw is late. Resolving it offline requires decoding the
relevant binary scene's material-to-submesh bindings and its sector placement;
runtime eligibility would additionally need a concrete draw/resource link.
No broader shader allowlist or pre-bloom sun support is justified by this note.

## Local evidence

Executable preferred base `0x00400000`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Catalogue decoding uses the existing `tools/analysis/inspect_x3.py` reader;
selected DAT slices use XOR 0x33 followed by gzip. The complete existing
shader manifest is `/tmp/x3-shader-sweep/manifest.json`.
Private extraction, derived material queries, bounded instruction listings and
targeted decompilation remain under `/tmp/x3-sun-re/`. Ghidra used the existing
`/tmp/x3-ghidra-research/X3Render` project with `-readOnly -noanalysis`.
Only this technical finding is tracked; no game bytes or decompiled code are
included.
