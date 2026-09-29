# GUI scale: how the 2D UI is sized and drawn, and where a global scale fits

Static study of the installed `X3AP.exe` (SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, base
`0x00400000`), the KC script objects `addon/04.cat:L/x3story.obj` (stock AP,
decoded SHA-256 `ed5786a0…faff7a`), `addon/07.cat:L/x3story.obj` (installed
override, `2c3101a8…199298`) and `addon/04.cat:L/x3intro.obj` (`29795739…0d14e`),
and the `types/Fonts`, `types/Materials` tables and `f/` font files in the
installed catalogues, 2026-09-29. Ghidra 12.1.3 headless on a fresh local
import (`-readOnly -noanalysis` for all queries) with
`tools/analysis/X3DecompileFunctions.java`, `X3ListRange.java`,
`X3XrefsTo.java`, `X3GrepInsns.java`, `X3FunctionContext.java` and the two
helpers added for this note, `X3ScreenReaders.java` (every read of the display
record `*0x00606f38` followed by a width/height/plane access) and
`X3FieldAfterGlobal.java`. Raw listings stayed in the session scratchpad. No
game or Wine process was started. Marks: **[s]** static reading of code or
data, **[m]** measured (a byte read of the installed files or a script output
kept under `verification/results/gui-scale/`), **[i]** inferred.

Builds on [field-of-view.md](field-of-view.md) (view plane, projection),
[camera-state-and-frame-routine.md](camera-state-and-frame-routine.md) (the
`*0x00608a38..0x00608a48` matrix scratch and the `0x004c0150` WVP selector),
[chase-target-indicator.md](chase-target-indicator.md) (target overlay
`0x0042a2d0`, texture text) and [chase-mouse-fire.md](chase-mouse-fire.md)
(script-owned cursor).

## Answer in brief

1. **Two projection paths, one of them resolution-proportional.** A GUI
   instance without node flag `0x200` goes through the ordinary perspective
   projection `0x004be460`, whose view plane is pinned to height 0.75 for every
   display at least 4:3 wide (`0x004db167..0x004db1ef`). Its on-screen size is
   therefore proportional to the screen **height**: pixels per world unit at
   depth `z` are `(H/2)·cot(F/2)/(0.75·z)`, i.e. `H/768` relative to a
   1024×768 design (1.875 at 1440 lines) [s]. The pre-game controller in
   `x3intro.obj` works in an **800×600 reference grid** (units of 1/64 px:
   `0xc800` = 51200 wide, `0x9600` = 38400 high, class `0x1f6` `Init@0x14c04`,
   used by `CheckMouseZone@0x18b69`) [s]; the argument order of the two
   `SE_DivFix` calls, and hence the exact normalisation `u = x·51200/W`, is
   inferred from the zone constants lying inside 0..51200 / 0..38400 [i].
   Which of these the main menu the user sees actually uses is **not
   established** statically (open item 1).
2. **In-game text is blitted from fixed-size bitmap fonts into textures.**
   Fonts are pre-rasterised atlases `F\<name><size>.abc/.tga`; the pixel size is
   data. `types/Fonts` lists `LARGE;Zekton;26;8` and `HUD;Tahoma;13;1` [m];
   KC `OpenFonts@0xb6ca` reads it (defaults 26/8/13) and calls
   `B3D_OpenFont(name, size, …)` (case `0x6e` of `0x00493b40`) →
   `0x0048cdc0` builds `"%s%d"` → `0x004f8120` (10 font slots at `0x00606f0c`)
   → loader `0x004f7830` with path format `F\%s` [s]. The native cockpit
   opens `"Tahoma"` size 13 as an immediate (`0x0041c9ab`, `0x0041f7a7`) [s].
   Neither path reads the resolution; `OpenFonts` has no `ScreenGet*` call [m].
   Text goes into a texture through `0x0048b4b0` (wrap, lines) →
   `0x0048b2d0` → glyph rasteriser `0x004f8600` [s].
3. **No pretransformed vertices for the in-game UI.** Menus, HUD panels and
   overlay icons are scene instances (bodies with the `gui2d` effect) carrying
   node flag `0x200`. For those, `0x004bdee0` builds a **pixel-space
   orthographic projection** into `*0x00608a3c` and an identity view; the
   material path `0x004c0150` selects that matrix at `0x004c2293` [s]. This is
   the single native point where a global 2D scale fits. Layout itself is
   spread: KC positions instances in pixels (`B3D_InstSetPos`) and sizes menus
   against `B3D_ScreenGetWidth/Height`, and the native target overlay
   `0x0042a2d0` places projected icons in pixels. `DrawPrimitiveUP` with
   `D3DFVF_XYZRHW` exists only for a full-screen colour overlay, the loading
   screen and debug text (§3.3).
4. **One hidden switch exists, but it is texel density, not layout.** The
   command-line switch `-fontscale N` (retail-reachable) sets `cfg+0x784`,
   which supersamples textures flagged `MPF_FONTSCALE` and rescales text
   metrics so logical size is unchanged. The shipped data does not support it
   (no `1xxxx` hi-res textures, no N× font atlases) [m]. There is no registry
   or config UI-scale value. The display record also carries an unused
   normalised **border** pair (`B3D_ScreenGetBorderX/Y`, `+0x30/+0x34`, always
   0) that KC honours in 32 + 4 viewport computations [s][m] (§4).
5. **Ranking:** (b) scale the pixel ortho at `0x004bdee0` together with a
   virtual screen size at the KC boundary (cases `0x71/0x72`) and scaled mouse
   deltas, with the native target overlay handled separately; (a) as the
   sharpness complement (`-fontscale s`, s× font atlases, `MPF_FONTSCALE` on
   every writable text texture); (c) proxy-side scaling only as a fallback,
   because the UI is not an XYZRHW stream and hit-testing cannot follow (§5).

## 1. Display record and the resolution-proportional path

`*0x00606f38` (display record) holds `+0x00` → front surface record with width
`+0x4` and height `+0x6` (int16), and four 16.16 values set at device init
(`0x004dac90`, `0x004db167..0x004db1ef`) [s]:

```text
r = H·65536 / W                      (0x00412450, arguments pushed W then H)
if r > 0xc000:  AspectX = 0x10000, AspectY = 0x10000·H/W    (narrower than 4:3)
else:           AspectY = 0xc000,  AspectX = 0xc000·W/H     (4:3 and wider)
BorderX = BorderY = 0               (+0x30, +0x34)
```

At 5120×1440 this gives AspectX = 2.667 (`0x2aaaa`), AspectY = 0.75 [i:
arithmetic]. The projection `0x004be460` uses the camera plane `+0x300/+0x304`
or, if either is ≤ 0, AspectX/AspectY: `m00 = cot(F/2)/W_plane`,
`m11 = cot(F/2)/H_plane`, `F` = camera `+0x298` (65536 = 360°) [s]. Viewports
are **normalised** 16.16 (`0x00489180` clamps to 0..`0xffff`; `+0x288` y0,
`+0x28c` y1, `+0x290` x0, `+0x294` x1) [s], so nothing on this path is in
pixels: a perspective GUI scene grows with the screen height and keeps its
4:3-centred horizontal layout (Hor+).

`x3intro.obj` class `0x1f6` `Init@0x14c04` stores `SE_DivFix` of
`B3D_ScreenGetWidth` with `0xc800` and of `B3D_ScreenGetHeight` with `0x9600`
in members `0x36/0x37`; `CheckMouseZone@0x18b69` multiplies `GetCursorX/Y` by
them and compares against constants such as `0x640`, `0xfa0`, `0x29cc`,
`0x4f4c`, `0x6978` [s]. x3intro also computes a 1024/576 (16:9) ratio against
`B3D_ScreenGetAspectX/Y` in `SetSceneParameters@0x87d9` (`0x8cec..0x8cff`),
a scene-aspect fit rather than a UI scale [s][i]. x3intro nevertheless creates
2D pixel instances too (`Init` `0x200|0x4000|0x1000`, `OpenCustom` `0x5200`,
`AddWaterMark` `0x200|0x10000|0x4000|0x1000`) [m].

## 2. Fonts and the text rasteriser

| Step | Address | Behaviour |
| --- | --- | --- |
| KC font table | `OpenFonts@0xb6ca` (x3story) | `SE_ReadFile` of `types/Fonts`, split, `SE_StringToInt`; defaults 26, 8, 13 [s] |
| `B3D_OpenFont` | case `0x6e` of `0x00493b40` (`0x00496112..0x0049616d`) | name = arg0, size = arg1 (`ECX`), flags args 2/3 → `0x0048cdc0` [s] |
| font file name | `0x0048cdc0` | `sprintf("%s%d", name, size)` (`0x005611c0`) → `0x004f8120` [s] |
| font slot | `0x004f8120` | 10 slots `0x00606f0c`; when `cfg+0x784 > 1` it doubles three 10-short rows of each of 11 records of the built-in table `0x005748b8` into `0x00606c20` (rows with id 1 or 4 excepted) [s]; meaning of the table not established |
| loader | `0x004f7830` | path format `F\%s` (static `0x0057c008+0x3c`), `"tga bmp"` image then `"abc siz"` metrics [s] |
| native HUD font | `0x0041c960` at `0x0041c9ab`, `0x0041f720` at `0x0041f7a7` | `"Tahoma"` (`0x0055b4c8`), size `0xd` = 13, immediate [s] |
| text block | `0x0048b4b0` | wrap width `(right−left)·cfg+0x784` (`0x004f9080`), line height `/cfg+0x784`, one call of `0x0048b2d0` per line [s] |
| text line | `0x0048b2d0` | on an `MPF_FONTSCALE` texture the right edge ×`cfg+0x784`; glyphs by `0x004f8600` [s] |
| measure | `0x0048b990` | `0x004f8f30` width `/ cfg+0x784` [s] |

Shipped files: `f/Tahoma13`, `f/Zekton26`, `f/ZektonES26`, `f/HarRier24`,
`f/HarRier TYGRA_16` (`.abc`+`.tga`), `f/bz22` (`.abc`+`.bmp`) [m]. The `.abc`
headers are version 5 with float metrics (Tahoma13: 15.0/13.0; Zekton26:
33.0/33.0) [m]. A sharper or larger font therefore needs a new atlas pair at
the target pixel size; the engine never rasterises from a vector font.

## 3. How 2D geometry reaches Direct3D

### 3.1 The pixel orthographic projection (`0x004bdee0`, flag `0x200`)

`0x004bdee0` (EAX = object with `+0x3c`, cdecl args instance, camera; returns
0/1) builds world/view/projection per instance. When
`[instance+0x130] & 0x200` (test at `0x004bdf27`) it writes [s]:

```text
Wvp = ((cam+0x294 − cam+0x290)·screenW + 0x8000) >> 16      pixels
Hvp = ((cam+0x28c − cam+0x288)·screenH + 0x8000) >> 16
P = *0x00608a3c:
  P[0]  = 2 / Wvp                                           (0x004be0e4)
  P[5]  = 2 / Hvp                                           (0x004be129)
  P[12] = 2·(x − 0.25)/Wvp  + ax,  x = inst+0x30            (0x004be17e/0x004be190/0x004be19e)
  P[13] = 2·(−y − 0.25)/Hvp + ay,  y = inst+0x34            (0x004be1fa/0x004be20d/0x004be21c)
  P[14] = P[15] = 1, rest 0
  ax = −1 if +0x130 & 0x1000 (left edge), +1 if & 0x2000 (right), else 0 (centre)
  ay = +1 if +0x130 & 0x4000 (top edge),  −1 if & 0x8000 (bottom), else 0
view  *0x00608a40 = identity
world *0x00608a44 = rotation × scale (+0x70 16.16 × +0x80/+0x84/+0x88),
                    or uniform (float)+0x70 when +0x130 & 0x10000
```

Constants: 2.0 at `0x005655d0`, 0.25 at `0x005655a4` [m]. One body unit is one
pixel and positions are pixel offsets from the anchored screen edge (or the
centre), y down. That is why the in-game UI keeps a fixed pixel size at any
resolution. The 2D branch has a single exit: `0x004be246 MOV EAX,1` …
`0x004be252 RET` [s]. `0x004c0150` then forms WVP from `*0x00608a44 ×
*0x00608a40 × *0x00608a3c` for `0x200` nodes (`0x004c2293`) [s]; the frustum
cull `0x004c6aa0` returns 1 for `0x200` nodes without a plane test
(`0x004c6ab0` → `0x004c6e98`) [s].

Writers of `0x200` [m]: KC `B3D_InstSetFlags2` (case `0x2b`, stores the whole
word to `+0x130`): x3story 33 sites (stock) / 34 (override) in 11/12 methods,
with anchor sets such as `Show`/`Open`/`OpenCustom`/`ShowWindow`
`0x200|0x4000|0x1000`, `SpecialMenu` `0x200|0x10000|0x4000[|0x2000]`,
`StartOverlayInst` / `StartOverlayCockpitInst` with every anchor combination;
native `0x00426230` (target-overlay icon place, 7 callers in `0x0042a2d0`) and
7 direct `OR [reg+0x130],0x200` in `0x0042a2d0` (`0x0042acd8` … `0x0042c130`).
Readers: `0x004bdee0`, `0x004c0150`, `0x004c6aa0` only [m: instruction grep;
register-split tests would escape it].

### 3.2 Who lays out

- KC (x3story) calls `B3D_ScreenGetWidth` at 58 sites in 20 methods and
  `B3D_ScreenGetHeight` at 64 sites in 19 methods, among them
  `NotifyMouseMove` (8/8), `NotifyClick` (6/6), `GetMenuMaxWidth` (6/6),
  `GetMenuMaxHeight` (6/8), `GetCursorIcon` (4/4), `IsMaximizePossible`,
  `PrepareDropdownMenu`, `MenuAction`, `SetClipping` [m]. Menu size limits,
  clipping and mouse hit-testing are computed in KC in the same pixel space as
  the 2D instances.
- `B3D_TexTextBlock` has 109 KC sites in 26 methods (124/28 in the override)
  [m]; text layout inside a window is KC arithmetic on text textures.
- The native cockpit HUD places its central group at constant screen-node
  offsets from the centre (see
  [chase-view-restore-and-hud-anchor.md](../architecture/chase-view-restore-and-hud-anchor.md))
  and the target overlay places projected icons in pixels (`0x00426230`).

### 3.3 The XYZRHW paths (not the in-game UI)

| Draw | Owner | FVF / stride | Role |
| --- | --- | --- | --- |
| `0x004c55b4` | `0x004c53d0` (from `0x004723d5`, `0x004724a7`) | `0x44` XYZRHW\|DIFFUSE / 20 | untextured full-screen quad; fade/colour overlay [s: FVF][i: role] |
| `0x004c57ff` | `0x004c55d0` (from `0x0049774c`) | `0x104` XYZRHW\|TEX1 / 24 | loading-screen image (`0x004974c0` `true/LoadScr*`) [s] |
| `0x004c5d46`, `0x004c5d8b` | `0x004c5830` (from `0x0047253f`) | `0x104` / 24 | on-screen debug text [s] |

### 3.4 Mouse

The engine keeps an absolute cursor in the input context `*0x00606f3c`
`+0x410/+0x412` (int16 pixels) with deltas `+0x414/+0x416`, clamped to
`0..W`, `0..H` in the WndProc `0x004d3620` (`0x004d38ef..0x004d3934`) and in the
DirectInput poll `0x004d5a90` (`0x004d6027..0x004d6075`) [s]. KC receives the
**deltas** as events `0x11d`/`0x11e` through `0x0049f570` (main loop
`0x00403d6d`, `0x00403dae`; `0x00410100` at `0x00411b40/0x00411b57`) and keeps
its own cursor [s]; it returns it to the engine through
`X2_UpdateCursorSteering` (`0x00607cec/0x00607cf0`, see chase-mouse-fire.md).
Native consumers of that cursor are pixel based: unprojection `0x00489780`
(`x·65536/W`) and cursor aim `0x00425410` → overlay icon hit test `0x004299a0`
against icon `+0x30/+0x34` [s].

## 4. Existing scale-like variables

- **`-fontscale N`** [s][m]: parsed in `0x004ec9e0` (`__stricmp` at
  `0x004ecfd3`, `atol` store `0x004ecff8` → `*0x00606f34+0x784`), default 1 at
  `0x004ecae3`. The parse block is gated by
  `((static+4 & 1) == 0 || (static+4 & 2) != 0)` with static `0x0057c008`
  whose `+4` is `0xc`, so it runs in retail [m]. 44 further instructions in 26
  functions reference `+0x784` [m: grep of `+ 0x784]`, base registers not
  traced one by one]. Effects [s]: `MPF_FONTSCALE` (Materials flag `0x10000`)
  generated textures are created `N×` larger (`0x004f4160`, sizes ×N before
  `0x004f3950`); file-backed ones load `true\<id+10000>` or `<name>_l`
  instead; KC texture blits into such textures are multiplied by N (B3D
  dispatcher `0x004960ab..0x004960f0`); text metrics as in §2. The logical
  (on-screen) size stays the same: it is a supersampling switch. Shipped data
  [m]: 18 `MPF_FONTSCALE` rows (ids 46, 47, 56, 61–65, 158, 219, 236, 327,
  340, 568, 579, 705, 922, 923; 10 generated, 8 file-backed), **0** of the
  `1xxxx` variants present, no N× font atlases. With N > 1 as shipped, file
  textures would miss and text drawn into non-`MPF_FONTSCALE` textures would
  shrink, because the wrap/line-height factors in `0x0048b4b0` are
  unconditional [i].
- **Border** `B3D_ScreenGetBorderX/Y` (cases `0x75/0x76`, display `+0x30/+0x34`,
  16.16 normalised, zeroed at `0x004db1ec/0x004db1ef`): KC uses BorderX at 32
  sites in 6 methods (`Init`, `StartOverlay`, `CheckSubTitleInst`,
  `SetViewPort`, `Menu`, `Run`) and BorderY at 4 sites, always in viewport
  arithmetic next to `0x10000` [m]. Setting it would inset KC viewports (a
  safe-area / centred-16:9 option for 32:9), not scale them [i]. Native readers
  of `+0x30/+0x34` were not traced.
- `B3D_SceneSetSystemScale` (case `0x4b`, scene `+0x2c`) multiplies world
  positions and the camera translation in the 3D branch of `0x004bdee0` and in
  `0x004be520`; the 2D branch ignores it [s]. KC uses it at 2 sites
  (`Create`, `Activate`) [m].
- Registry/config: the value names are `Video*`, `Audio*`, `Input*`,
  `GameStarts`, `GameState` [m]; none is a UI scale. `x3config.obj` was not
  decoded.

## 5. Patch strategies, ranked

### (b) Scale the pixel ortho, with a virtual screen for KC — first choice

Sites and contract:

| Part | Site | What |
| --- | --- | --- |
| 2D scale | `0x004bdee0` 2D exit or its two callers | `P[0]·=s`, `P[5]·=s`, `P[12] = (P[12]−ax)·s + ax`, `P[13] = (P[13]−ay)·s + ay` — equivalent to a `W/s × H/s` virtual viewport |
| KC screen size | case `0x71` `0x00496190` (`MOVSX EAX,word [ECX+4]`), case `0x72` `0x004961b7` (`MOVSX EAX,word [EAX+6]`) | return `W/s`, `H/s` |
| KC mouse | events `0x11d/0x11e` at `0x00403d45..0x00403dae` and `0x00411b40/0x00411b57` | deltas `/s` with a remainder accumulator |
| native cursor users | `0x00607cec/0x00607cf0` consumers (`0x00489780` callers, `0x00425410`) | cursor `×s` back to real pixels |
| native overlay | `0x0042a2d0` (`0x00426230` and the 7 direct ORs), lead marker `0x0042aaae` | exclude from the scale, or divide projected positions by `s` about the centre |

What follows automatically: every KC menu, window, ticker and sidebar (pixel
layout via `ScreenGet*`, positions via `InstSetPos`, anchors via flags) and KC
hit-testing, since KC tests its own rectangles against its own cursor in the
same virtual space; screen-edge clamping and `GetMenuMaxWidth/Height` limits,
because they read the virtual size; the native central HUD group, which uses
centre-relative constants and would scale about the centre (the
`0x0042aae0` gate and `0x42aed6` restore are unaffected; they choose whether
the group is drawn, not where). What does not: the target overlay (projected
3D positions in real pixels, hit-tested natively by `0x004299a0`), the mod's
lead marker, and cursor fire/steering (`0x00489780`, dead zone `0x0040e8c0`),
unless handled as in the table. Text is magnified by `s` with the texture
filter, so it blurs; `MPF_NOFILTERING` textures (7 of the 18 fontscale rows)
become blocky.

Hook-site suitability [s]:

- **Caller form.** `0x0047e002` and `0x0047e70c` are each one whole
  `E8 rel32` (5 bytes) to `0x004bdee0`; the next instructions `0x0047e007`
  and `0x0047e711` start at the boundary. Inputs are EAX plus two stack
  arguments; the caller pops them later together with `0x004f66e0`'s
  (`ADD ESP,0x10` at `0x0047e01a`, `0x0047e727`), so a replacement must be a
  `CALL` to a thunk that calls the original with the same stack and EAX. Both
  callers overwrite EAX before any use and set flags before any test, so the
  return value and flags are dead. The thunk must preserve EBX/ESI/EDI/EBP,
  leave the x87 stack empty and use SSE for the arithmetic. It runs on the
  render thread inside the per-instance visit, once per instance; the next
  reader of `*0x00608a3c` is the `0x004c4fc0`/`0x004c0150` submission that
  follows (`0x004f66e0`, the texture-animation advance, sits between; it is
  not known to touch the matrices [i]).
- **In-function form.** `0x004be246 B8 01 00 00 00` is reached only by the 2D
  branch; there EBX already holds the flag word (`0x004be1fd`), EBP the camera,
  the instance is at `[ESP+0x1c]`, EAX is dead (overwritten by the `MOV`). A
  5-byte detour here needs no anchor re-derivation from memory.
- **KC size.** `0x00496190..0x0049619a` (MOVSX 4 + `MOV EDX,[0x006085e4]` 6)
  and `0x004961b7..0x004961c0` are whole-instruction 10-byte spans; EAX/ECX/EDX
  are reloaded afterwards and no flag is consumed before the next `CALL`.
- The scale must be applied per camera/scene, not globally, if the target
  overlay scene is to be excluded; the camera is the second argument.

### (a) Sharp text at the larger size — complement to (b)

Launch with `-fontscale s` (integer only: `atol`), ship s× font atlases and
redirect the font open (`0x0048cdc0` size ×s, or ship
`F\Tahoma26`/`F\Zekton52` under the names KC asks for through a `types/Fonts`
overlay), add `MPF_FONTSCALE` to every writable text texture in a
`types/Materials` overlay, and provide the eight `true\1xxxx` variants (or
drop the flag on them). Then text is rasterised at s× texel density and (b)
magnifies it back to 1:1 texels: sharp. Unknowns: the doubled built-in table
in `0x004f8120`, whether every KC text texture is writable and listed in
Materials, and KC code that measures text in one texture and draws in another.
Hit-testing is unaffected (logical sizes do not change). Integer `s` only;
`1.25x`/`1.5x` would need a fractional replacement of the `IDIV`/`IMUL`
sites (44 references) [i].

### (c) Proxy-side scaling — fallback only

There is no XYZRHW UI stream to rescale: the UI is `gui2d` instance draws with
a WVP built on the CPU. A proxy can only rewrite the WVP/positions of draws it
classifies as UI (the `0x200` WVP product is a constant, see
camera-state-and-frame-routine.md), which blurs text like (b) but cannot move
KC's cursor, clamping or hit-test rectangles, so clicks would land on the
unscaled layout. Not recommended except as a display-only experiment.

## 6. Click selection under a scaled UI

Added 2026-09-30 after run390 (5120×1440, `ui_scale` 1.25, virtual 4096×1152):
clicking a ship's selection bracket in the 3D view (first person and chase)
no longer selects it, while the brackets draw at their real-pixel positions
because the cockpit HUD camera (`cockpit+8`) is excluded from the projection
scale ([ui-scale.md](../architecture/ui-scale.md)). Static reading of the same
EXE and `x3story.obj` (stock `addon/04.cat` and override `addon/07.cat`), Ghidra
listings in the session scratchpad; evidence scripts
`verification/results/gui-scale/gui_scale_static_checks.py` (site bytes, call
targets) and `kc_gui_calls.py` (KC call sites).

### 6.1 The click path reads the script's virtual cursor [s][m]

```text
KC Click@0xe6b39 (class of GetCursorX@0xe6ab3 / GetCursorY@0xe6abc = members 0/1, the script cursor)
  -> by-name NotifyClick(listener, x, y, …)            (0xe6be9, 0xe6c5a)
  -> NotifyClick@0xf2456 -> GetObjectAtScreenPos(x, y) (0xf28ea; also GetCursorIconForSteering 0xf3406)
  -> GetObjectAtScreenPos@0xf34e0:
       INS_CockpitGetObjectByTargetOverlayIconPos(cockpit, x, y)   (0xf3502)
       else INS_CockpitGetCursorAim(cockpit, x, y, 0)              (0xf3514)
```

Override `addon/07.cat` has the same shape (`GetObjectAtScreenPos@0x109833`,
one site each). The script cursor is accumulated from the mouse-delta events
that D1..D4 now divide by `s`, so `x, y` are **virtual pixels** (0..W/s).

Native side, INS dispatcher `0x0042d340` [s]:

| Native | Case | Point load (script arguments, virtual px) | Consumer |
| --- | --- | --- | --- |
| `INS_CockpitGetObjectByTargetOverlayIconPos` | `0x64` (entry `0x0042ecc5`) | `0x0042ecdd mov ecx,[esi+0xb]` (y), `0x0042ece0 mov esi,[esi+6]` (x) | `push ecx; push esi; push cockpit+0x3ac; call 0x004299a0` at `0x0042eceb` |
| `INS_CockpitGetCursorAim` | `0x28` (entry `0x0042ddc0`) | `0x0042ddf1 mov esi,[ebx+6]` (x), `0x0042ddf4 mov edi,[ebx+0xb]` (y); `-1,-1` when fewer than 3 arguments (`0x0042dddf`, `0x0042dde2`) | `call 0x00425410(cockpit, x, y, flag)` at `0x0042de11` |

Inside `0x00425410` the point is `EBX = [ebp+0xc]` (`0x0042541d`),
`ESI = [ebp+0x10]` (`0x00425457`); with both non-negative it goes to the icon
test `0x004299a0` (`0x0042546b..0x00425474`) and, when no icon is hit, to the
3D ray pick through `0x00489780(x, y, 500000, &v)` at `0x004254c9`: both in
the argument's units [s].

The icon test `0x004299a0` (stdcall, `ret 0xc`) loads the point at
`0x00429a32 mov ebp,[esp+0x28]` (x) and `0x00429a36 mov ebx,[esp+0x2c]` (y),
makes it centre-relative by subtracting half of the overlay camera's viewport
in **real** pixels (`((x0+x1)·screenW + 0x8000) >> 16` from
`*(*0x00606f38)+4` at `0x004299c1`, height at `0x004299fb`; subtraction at
`0x00429a3c..0x00429a50`), then compares against each icon node
(overlay `+0x348` list): centre `+0x30/+0x34` (`0x00429b52`, `0x00429b57`,
real centre-relative pixels as written by `0x00426230`), half-extents
`+0x70·+0x80` and `+0x70·(+0x84/2)` (16.16, `0x00429b49..0x00429ba2`), test
`0x00429ba6..0x00429bb8`; a second pass tests bracket-corner nodes
(`puVar[0x16]/[0x1b]/[0x20]` `+0x30/+0x34`) as an inclusive rectangle
(`0x00429c76..0x00429c99`) [s]. Rectangles and centre are real pixels, the
point is virtual: a click at real screen pixel `X` arrives as `X/s`, so the
tested offset misses by `X·(1 − 1/s)`: at `s = 1.25` on 5120×1440 that is
0 px at the left edge, 512 px at the centre, 1024 px at the right edge, and up
to 288 px vertically [i: arithmetic]. That is the defect.

### 6.2 The other direction [s][m]

No native real-pixel position or rectangle is handed back to the script on
this path: `INS_CockpitGetMenuPosByTargetOverlayIconPos` (case `0x65`,
`0x00429cf0`, returns five values) and `INS_CockpitProjectPosition` (case
`0x66`, `0x004899f0`) have **0** KC call sites in the stock, override and
intro objects [m]; the two natives above return an object handle only
(`0x0042de16..` pushes `[obj+8]`); `INS_CockpitTargetOverlayTrackPrevNext`
(case `0x63`) takes a direction only. The cursor goes native-ward once, at the
E store (script ×s → `0x00607cec/0x00607cf0`), and is never read back by the
script. `GetCursorIconForSteering@0xf33e8` uses the same
`GetObjectAtScreenPos`, so the hover cursor icon over brackets is wrong in the
same way and is fixed by the same sites [i]. A sweep of every native that
takes a screen point from the script was not made beyond the overlay natives.

### 6.3 Minimal fix: scale the two script-point loads by `s`

Apply the E rounding (`v·fixed256 + 0x80) >> 8`) to the arguments at the two
dispatcher cases, so `0x004299a0`, `0x00425410` and its `0x00489780` fallback
all receive real pixels. Not inside `0x004299a0` or `0x00425410`: those are
also reached with real pixels from the fire path (`0x00445ac0/0x00445ac6`
load `0x00607cf0/0x00607cec` → `0x00425410` at `0x00445ad0`) and with the real
viewport centre (`0x0042a55a` passes `-1,-1`; `0x004255aa`).

| Claim | Span (whole instructions) | Bytes | Stub | Registers / flags | Qword |
| --- | --- | --- | --- | --- | --- |
| G, case `0x64` | `0x0042ece0..0x0042ece4`: `mov esi,[esi+6]; push ecx; push esi` (5) | `8b 76 06 51 56` | `mov esi,[esi+6]`; scale ECX (y, loaded at `0x0042ecdd`) and ESI with `imul r,[fixed256]; add r,0x80; sar r,8`; `push ecx; push esi`; `jmp 0x0042ece5` | in: ESI = argument block, ECX = y, EAX = cockpit (kept, `add eax,0x3ac` follows). EFLAGS dead (`0x0042ece5 add` writes them). EDX untouched and dead (`0x004299a0` writes EDX at `0x004299bf` before reading). Stack: the stub's two pushes replace the displaced ones; `0x004299a0` pops 12 | inside the aligned word `0x0042ece0..0x0042ece7`: one `cmpxchg8b` |
| H, case `0x28` | `0x0042ddf1..0x0042ddf6`: `mov esi,[ebx+6]; mov edi,[ebx+0xb]` (6) | `8b 73 06 8b 7b 0b` | both loads, scale ESI and EDI as above, then **`cmp ecx,4`** and `jmp 0x0042ddf7` | in: EBX = argument block, ECX = argument count (untouched), EAX = cockpit (untouched). **EFLAGS are live**: `0x0042ddf7 jl` consumes `0x0042ddee cmp ecx,4`; re-executing `cmp ecx,4` restores them exactly (or `pushfd/popfd`). EDX untouched | first five bytes inside `0x0042ddf0..0x0042ddf7`: one `cmpxchg8b`; the sixth byte `0x0042ddf6` becomes dead padding |

No reference lands inside either span (`X3XrefsTo` on `0x0042ddf1..f7` and
`0x0042ece0..e5`: none) [m]; the jump-table entries are the case starts
`0x0042ddc0` and `0x0042ecc5`. The obvious alternative G' at `0x0042ecdd`
(both loads, 6 bytes) straddles the qword boundary `0x0042ece0` and would fall
back to a plain copy. No conflict with the eight ui_scale claims (A `0x004be246`,
B `0x00496194`, C `0x004961bb`, D1 `0x00403d36`, D2 `0x00403d7a`, D3
`0x00411b40`, D4 `0x00411b57`, E `0x004074ec`, F `0x004bdee0`) or the other
dispatcher claims (`fov` `0x0042dbf8`, `chase_mode_script` `0x0042e742`) [m:
grep of `src/proxy`]. Both run on the script VM's thread (the main/render
thread), no call, integer only; at `fixed256 = 256` both are the identity.
`-1` stays negative at every `s` in [1, 3] (`(−256·s + 128) >> 8 ≤ −1`), so
the "no point" sentinel of case `0x28` survives even if the scale were applied
on that path [i: arithmetic]; it is not, because the sentinel is set on the
branch that skips the loads.

### 6.4 Why the 35-pixel box and cursor fire work [s]

They never see a script argument: the 35-pixel selection box `0x004257f0`
reads `0x00607cf0` (`0x004257f0`) and `0x00607cec` (`0x00425801`) and centres
them with the real viewport; the steering dead zone `0x0040e8c0` reads them at
`0x0040e8d7/0x0040e8dd` into `0x00489780`; the fire path passes them to
`0x00425410` at `0x00445ac0..0x00445ad0`. Stub E has already multiplied the
script cursor by `s` into those globals, so all three are in real pixels. G
and H change only the two dispatcher cases, which the fire, box, dead-zone
and overlay-update (`0x0042a55a`, `-1,-1`) paths do not pass through, so the
fix cannot double-scale them. Both conversions use the same rounding, so a
click and a shot at the same cursor resolve the same pixel.

## Open items

1. Which instances the main menu the user sees uses (non-`0x200` perspective
   scene, or `0x200` pixel instances with KC-side scaling). One diagnostic row
   per frame at `0x004bdee0` counting `0x200` vs other instances per camera,
   in the main menu and in flight, settles it.
2. Gravidar, message ticker, target and property windows were not tied to
   specific KC classes; their `0x200` status is inferred from the KC flag
   sites, not observed.
3. The anchor mapping (`0x1000` left, `0x2000` right, `0x4000` top, `0x8000`
   bottom) is read from the matrix formula; not checked against a capture.
4. Native readers of display `+0x30/+0x34` (border) and the meaning of the
   `0x005748b8` table doubled under `-fontscale` are not established.
5. The `SE_DivFix` operand order in `x3intro` `Init@0x14c04` is inferred.
6. §6: only the overlay natives were checked for script-supplied screen
   points; other natives that take a pixel point from the script (none known)
   would need the same treatment. G/H are static proposals, not flown.

## Reproduce

```sh
python3 verification/results/gui-scale/gui_scale_static_checks.py   # EXE/catalogue facts -> gui_scale_static_checks.json
python3 verification/results/gui-scale/kc_gui_calls.py              # KC native call sites -> kc_gui_calls.txt
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless <proj-dir> <proj> \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3ScreenReaders.java /tmp/screen-readers.txt
```

The project must first be created with `-import X3AP.exe` (about 90 s). Raw
decompiler output stays local.
