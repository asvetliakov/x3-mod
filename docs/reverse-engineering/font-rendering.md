# Font rendering: the `F\` font format, `-fontscale`, and what sharp text under the UI scale needs

Static study of the installed `X3AP.exe` (SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, base `0x00400000`), the
`f/` font members and `types/Fonts*` tables of the installed catalogues, and the KC objects
`L/x3story.obj`, `L/x3intro.obj`, `L/x3galedit.obj`, 2026-09-30. Ghidra 12.1.3 headless on the
local import used for [gui-scale.md](gui-scale.md) (`-readOnly -noanalysis`;
`X3DecompileFunctions.java`, `X3GrepInsns.java`, `X3XrefsTo.java`), capstone for exact operand
order, and two scripts kept with their outputs:

- `verification/results/font-rendering/font_files.py` → `font_files.json`: sizes, SHA-256 of the
  decoded bytes, header fields, glyph counts, cell/band statistics of every font member. No
  glyph image or metric table is written.
- `verification/results/font-rendering/font_static_checks.py` → `font_static_checks.json`: hook
  site bytes, the decoded text-style table `0x005748b8`, every `[reg+0x784]` access, the callers
  of `0x0048cdc0`.

Raw decompiler output and the decoded font bytes stayed in the session scratchpad. No game or
Wine process was started. Marks as in gui-scale.md: **[s]** static reading, **[m]** measured by
the scripts above, **[i]** inferred. This note owns the font questions; gui-scale.md §2/§4 keeps
the summary and points here. The UI-scale patch it serves is
[../architecture/ui-scale.md](../architecture/ui-scale.md).

## Answer in brief

1. **Format.** An `F\<name><size>` font is an image `.tga` (or `.bmp`) plus a metrics file
   `.abc` (or a `.siz` width table for grid fonts). The `.abc` is version 4 or 5; version 5 (all
   fonts the game opens) is a 0x14-byte header whose four floats the engine never reads, a
   `u16` code→glyph map, a `u32` glyph count and 24-byte glyph records
   `{f32 u0, v0, u1, v1; i16 A, B, C; u16 pad}`. The engine draws glyph `g` from texel
   `(trunc(W·u0), trunc(H·v0) + yoff)`, `B` columns wide and **`S − 1` rows high**, where `S` is
   the *requested size* (not a file field) and `yoff` the fourth `types/Fonts` column; the pen
   moves by `A` before and `C` after the glyph (`C` includes the ink width). Line pitch is `S`
   plus the text-style extent. The image is a 32-bit uncompressed TGA with white RGB and
   coverage in alpha (Tahoma13 also bakes a black drop shadow into it) [s][m] (§1).
2. **`-fontscale N`** is a supersampling switch implemented in 43 reads of `cfg+0x784` (plus the
   default and the switch store). Layout→texel conversions multiply by `N` only when the
   *target texture* has `MPF_FONTSCALE`; the text-block wrap width and line step are scaled
   **unconditionally**; every measurement returned to KC (`TexTextWidth/Height/BlockHeight`,
   `TexGetWidth/Height`, `TexGetTextArray`) is divided by `N`, so KC layout stays in layout pixels.
   Font opens are **not** scaled: `0x0048cdc0` still asks for `Zekton26`/`Tahoma13`, so an N×
   font must be supplied under the same name or the request scaled. The `0x005748b8` table is the
   **text-style offset table** (8-way outline, eight 1-px drop shadows, bold); `0x004f8120`
   doubles its shadow/outline offsets whenever `N > 1` — ×2, not ×N. File-backed `MPF_FONTSCALE`
   textures ask for `tex\true\1<id>`, which do not exist, and get the `NONE_BLACK` placeholder for
   the session. As shipped, 25 of the 35 writeable generated rows lack `MPF_FONTSCALE`, among
   them the native HUD text texture 15, the tooltip 1230 and (inferred) the KC menu surfaces, so
   `N > 1` breaks their text (wrapped at N× width, lines overlapped at 1/N pitch). Nothing in the code is
   specific to `N = 2` except the style factor; `N ≤ 7` keeps the 16-bit surface pitch of the
   1024-wide rows in range [s][m] (§2).
3. **Sharp text at fractional `s`:** render at integer density `d = ceil(s)` through the native
   path and let the projection minify `d/s`. Minimal set: `-fontscale d`; a font-open hook
   (`0x0048cdc0` entry, size and y-offset ×d); generated `F\<name><S·d>` pairs; a runtime flag
   patch right after the Materials load (`0x0048af71`) that sets `MPF_FONTSCALE` on the text
   targets (and clears `MPF_NOFILTERING` when `s ≠ d`); and a blit hook (`0x0048c090`,
   `0x0048c460` entries) that upscales non-`MPF_FONTSCALE` sources blitted into scaled targets,
   because native and KC code blit named GUI atlases (`gui_master`) into text textures. A
   fractional replacement of the 43 sites is ranked last: fonts, line pitch and blit sources
   cannot follow a non-integer ratio without seams or overlapping lines (§3).
4. **Assets.** `types/Fonts` rows: `LARGE;Zekton;26;8` and `HUD;Tahoma;13;1` (default),
   `LARGE;Harrier;24;5` (`-L007`, Russian), `LARGE;ZektonES;26;0` (`-L034`, Spanish); Mayhem does not
   override them. KC opens exactly LARGE and HUD (x3story, x3intro and x3galedit, each once per
   row), the native cockpit opens `Tahoma` 13 twice. `F\HarRier TYGRA_16` and `F\bz22` are never
   opened. For density `d` the files are `Tahoma{13d}` and, per language, `Zekton{26d}`,
   `Harrier{24d}` or `ZektonES{26d}`. `types/Fonts` can be overlaid by a loose file or a higher
   catalogue, but with the font-open hook it does not need to be (§4).

## 1. The font file format (`0x004f7830`)

### 1.1 Open sequence [s]

`B3D_OpenFont` (case `0x6e` of `0x00493b40`, `0x00496112`) and the native cockpit call
`0x0048cdc0` (`EAX` = name, `ECX` = size `S`, stack `a1` = cell width, `a2` = flags, `a3` =
y offset; cdecl, caller pops 12). It formats `"%s%d"` (`0x005611c0`) and calls
`0x004f8120(file, a1, S, a2, a3)`, which returns an existing slot of the 10 at `0x00606f0c`
when the file name matches (so the first opener of a name fixes its parameters) or calls the
loader `0x004f7830(file, cellW=a1, cellH=S, flags=a2, yoff=a3)`. `0x0048cdc0` returns the slot
index or `0xffffffff` on failure. The script arguments map as `B3D_OpenFont(name, a1, S, a2, a3)`
(script record offsets `+1`, `+6`, `+0xb`, `+0x10`, `+0x15`; `a3 = 0` when four arguments).

| Opener | Site | Name | `S` | `a1` | `a2` flags | `a3` y offset |
| --- | --- | --- | --- | --- | --- | --- |
| KC `OpenFonts` (x3story, x3galedit), `Set` (x3intro) | native call, 2 per object | Fonts row col 2 | col 3 | `0x10` | `0xa1` | col 4 |
| native cockpit init `0x0041c960` | `0x0041c9ab` | `"Tahoma"` `0x0055b4c8` | `lea ecx,[ebx+0xd]` = 13 | `0x10` | `0x21` | 1 |
| native cockpit loader `0x0041f720` | `0x0041f7a7` | `"Tahoma"` | `lea ecx,[edi+0xd]` = 13 | `0x10` | `0x21` | 1 |

KC reads `addon\types\fonts.txt` (STRG `0x2fef` in x3story) with defaults LARGE = `Zekton`,
26, 8 and HUD = `Tahoma`, 13 [s]. Loader steps:

1. `F\<file>` (path format `*(0x0057c008+0x3c)` = `F\%s`) with extension list `"tga bmp"`
   through the resolver (loose tree first, then catalogues from the highest,
   [loading-orchestration.md](loading-orchestration.md)). A `.bmp` hit (`0x00469700` compares the
   found extension with `"bmp"`) is read whole and parsed by `0x004f94d0` after a `"BM"` check;
   a `.tga` hit loads through the texture path `0x004f3510` with flags `cfg+0xe8 | 0x8602c`. A
   placeholder result (`+0x10 & 0x20000`) fails the open. Font `+0x14 |= 0x8000` when the
   image record `+8` (bits per pixel) is above 1: the glyph blit then alpha-blends with the
   text colour (`0x004efa70` → `0x004b2730`) instead of the 1-bit mask path (`0x004b1ca0`).
2. `"abc siz"`: an `.abc` hit (`0x00469700` against `"abc"`) switches the font to proportional
   ABC mode (`+0x14 |= 0x20020`); a `.siz` hit is kept as a per-glyph width byte table for a
   fixed grid (`+0x14 |= 0x20`, cells `a1 × S`, first code 0x20). Neither → grid font without
   widths. All shipped fonts use `.abc`; the grid/`.siz` path is only described here.

### 1.2 `.abc` layout [s][m]

Little-endian. The loader reads only the fields marked *read*; the buffer is freed at the end of
the load, so nothing else can read the rest.

| Offset | Type | v4 (`bz22`) | v5 (every opened font) |
| --- | --- | --- | --- |
| `+0x00` | `u32` | version 4 (*read*) | version 5 (*read*); other values fail the open |
| `+0x04..+0x13` | 4 × 32 bit | ints `22, 256, 256, 16` (size, atlas W, H, ?) [m] | floats, unread: `+4` = atlas cell height (Tahoma 15, Zekton 33, ZektonES 24, Harrier 32), `+8` = 0, `+0xc` = 2.0 (Tahoma) else 0, `+0x10` = nominal height (13 / 33 / 24 / 32) [m] |
| `+0x14` | `u16` | first code (*read*) | `n` = highest mapped code (*read*) |
| `+0x16` | `u16` | last code (*read*); glyphs = last − first + 1 | map `u16[n+1]`: code → glyph index (*read*); unmapped codes hold 0 |
| `+0x18` / `+0x18+2n` | `u32` | count (unread; equals last − first + 1) | glyph count (*read*) |
| `+0x1c` / `+0x1c+2n` | records | 24 bytes × count | 24 bytes × count |

Glyph record (24 bytes), and what the loader keeps per glyph:

| Offset | Field | Engine use |
| --- | --- | --- |
| `+0x00` | `f32 u0` | src x = `trunc(W·u0)` (x87 with RC = truncate) → font `+0x44[g]` (`u16`) |
| `+0x04` | `f32 v0` | src y = `trunc(H·v0 + yoff)` → font `+0x48[g]` |
| `+0x08` | `f32 u1` | unread |
| `+0x0c` | `f32 v1` | only a discarded maximum-height computation |
| `+0x10` | `i16 A` | low byte, signed: pen advance **before** the glyph → `+0x50[g]` |
| `+0x12` | `i16 B` | low byte, unsigned: blit width in texels → `+0x2c[g]` |
| `+0x14` | `i16 C` | low byte, signed: pen advance **after** the glyph, ink included → `+0x4c[g]` |
| `+0x16` | `u16` | unread (non-zero in 5 of the 6 shipped `.abc` files, zero in Zekton26) [m] |

So only the low bytes count: `B ≤ 255`, `−128 ≤ A, C ≤ 127`. In every shipped file
`B = W·(u1 − u0)` and `W·u0`, `H·v0` are exact integers (power-of-two atlases), 1,504 of 1,504
opened glyphs [m]; a generator should keep `u0 = x/W`, `v0 = y/H` with power-of-two `W`, `H` so
the truncation is exact.

### 1.3 How a glyph is placed [s]

Per character (`0x004f8600`, UTF-8 decoded up to three bytes by `0x004f9460`; `0x1b` + letter
switches the colour; codes above `n` are skipped with no advance):

```text
g      = map[code]                      (v5; v4: code - first)
pen   += A[g]
blit B[g] x (S - 1) texels from (x[g], y[g]) to (pen + dx_k - min_dx, line_y + dy_k - min_dy)
        for every style pass k (outline/shadow passes in the shadow colour, then the foreground)
pen   += C[g]                           (C - B is the right bearing)
width  = sum(A + C) - (C_last - B_last) + (max_dx - min_dx)          (0x004f8f30)
line   = S + (max_dy - min_dy)                                        (0x0048b9c0, 0x004f9370)
```

The blit height `S − 1` comes from the requested size (`font+0x0e`), not from the file
(`0x004f88dc..0x004f88f6`, `movsx edi,[ebp+0xe]; sub edi,1`, passed as the row count to
`0x004b2730`). A glyph whose `pen + B` passes the right edge ends the line. The drawable band of
each cell is therefore rows `yoff … yoff + S − 2` below `v0`; ink outside it is cut. Measured
bands [m]:

| Font | Atlas | Cell rows | `yoff`, `S` → band rows | `H` ink rows | `g` ink rows | glyphs | mean a–z advance `A+C` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Tahoma13 | 256×512, TGA origin bottom-left | 15 | 1, 13 → 1..12 | 3..10 | 5..12 | 612 (codes ≤ U+0491) | 5.35 |
| Zekton26 | 256×512, top-left | 33 | 8, 26 → 8..32 | 12..27 | 17..31 | 274 (≤ U+0200) | 10.50 |
| ZektonES26 | 256×512, top-left | 24 | 0, 26 → 0..24 | 4..19 | 9..23 | 303 (≤ U+2215) | 10.50 |
| HarRier24 | 512×512, top-left | 32 | 5, 24 → 5..27 | 9..22 | 13..25 | 315 (≤ U+04E9) | 10.69 |

(ink rows counted at alpha > 127; Tahoma's baked shadow extends two rows lower and is cut at
the band edge.)

### 1.4 Glyph image [s][m]

TGA image type 2 (uncompressed true colour), 32 bpp, 8 alpha bits; both origins occur (Tahoma13
bottom-left `0x08`, the others top-left `0x28`) and both render correctly in game, so the loader
honours bit 5 [i]. The blit `0x004b2730` computes, per channel,
`dst = (src_rgb · a · colour + dst · (255 − a) · 256) >> 16` and `dst_a = dst_a·(255 − a)/256 + a`:
RGB is multiplied by alpha and the text colour. Zekton, ZektonES and Harrier are white wherever
alpha > 0 with 16 alpha levels; Tahoma13 has 15 levels and 72 % of its inked texels are black
(pure black: a baked drop shadow down-right; the rest are pure white) [m]. A generator writes RGB = 255, A = coverage, top-left
origin; the engine's own shadow/outline passes (§2.3) are the alternative to a baked shadow.

### 1.5 Measured files [m]

All from `01.cat`, decoded (XOR `0x33`) SHA-256 prefixes: `Tahoma13.abc` 17,054 B `19a8b253…`,
`.tga` 524,332 B `ac90ecef…`; `Zekton26.abc` 7,628 B `6b161f83…`, `.tga` 524,306 B `893ed062…`;
`ZektonES26.abc` 24,750 B `3fc139ae…`, `.tga` `2dec949d…`; `HarRier24.abc` and
`HarRier TYGRA_16.abc` identical 10,102 B `86d833ba…`, `.tga` identical 1,048,594 B `5ff1501c…`;
`bz22.abc` 2,332 B `acaa3aab…` (v4), `bz22.bmp` 8,254 B 256×256 1 bpp. Full records in
`font_files.json`.

## 2. The `-fontscale N` mechanism

### 2.1 Switch and readers [s][m]

Default 1 at `0x004ecae3`, `atol` store at `0x004ecff8` (integer only; `-fontscale 1.5` is 1),
parse block reachable in retail (gui-scale.md §4). A byte search finds **46** `[reg+0x784]`
operands in `.text`, identical to the Ghidra listing; 45 are the config (`*0x00606f34`), the
other (`0x00489014`, `lea edx,[edi+0x784]`) is a list head in an unrelated object. Of the 45:
two `MOV` writes, 20 `IDIV` (texel → layout), 3 `CMP` (`> 1` tests) and 20 loads (18 `MOV`,
2 `IMUL` memory operands) that feed register multiplies (layout → texel) or a later division
[m]. By function:

| Function | B3D native / role | `+0x784` use |
| --- | --- | --- |
| `0x004f8120` | font open | style table factor: 2 if `N > 1` (§2.3) |
| `0x004f4160` | texture create | `MPF_FONTSCALE` + `N > 1`: generated size ×N; file id +10000 (§2.4) |
| `0x0048b2d0` | one text line | target `MPF_FONTSCALE`: x, y, right edge ×N (`0x0048b463..0x0048b474`) |
| `0x0048b4b0` | `B3D_TexTextBlock` | wrap width `(right−left)·N` and line step `/N` **always**; auto width `W/N` if `MPF_FONTSCALE` |
| `0x0048b770` | `B3D_TexTextBlockCentered` | wrap ×N, line step /N, centring `width/N` (always) |
| `0x0048cb10` | `B3D_TexGetTextArray` | wrap width ×N (always) |
| `0x0048b990`, `0x0048b9c0`, `0x0048ba10` | `TexTextWidth`, `TexTextHeight`, `TexTextBlockHeight` | result `/N` (always) |
| `0x0048cd20`, `0x0048cd70` | `TexGetWidth`, `TexGetHeight` | result `/N` if `MPF_FONTSCALE` |
| `0x0048b0b0`, `0x0048ba70`, `0x0048bbc0`, `0x0048bd30`, `0x0048bee0` | `TexRectFill`, pixel plot (native caller not mapped), `TexLine`, `TexCircle`, `TexCircleFill` | coordinates ×N if `MPF_FONTSCALE` |
| `0x0048c090`, `0x0048c460` | `TexBltBlock`, `TexBltBlockAlpha` (and `TexBltTiles*` via `0x0048c800`/`0x0048c930`) | dest clip bounds `/N`; dest `MPF_FONTSCALE`: dest x, y ×N, and **only if the source is also `MPF_FONTSCALE`** source x, y, w, h ×N |
| `0x00493b40` `0x004960ab..0x004960f0` | `B3D_TexBltImage` | dest `MPF_FONTSCALE`: all six coordinates ×N (16-bit `imul`), source image included |
| native `0x0041cfd0` | icon blits into a target texture (argument 2) | texture size `/N`; source `0x0046a600("effects/menugfx/gui_master")`, a named texture [i] |
| native `0x00424e00` | HUD text into texture 15 | measure `/N`, text blocks |
| native `0x00426e10`, `0x004273d0` | HUD text into texture 61 (as does `0x00426d00`, which reads no `+0x784`) | texture size `/N` |
| native `0x0042cc10`, `0x0042ce80` | text box sizing; tooltip into texture 1230 (`0x0042ce80`) | measure and line height `/N`; `0x0042ce80` blits a GUI texture into 1230 |

Every `MPF_FONTSCALE` test reads the Materials row flags `*(0x00608db0 + id·0x3c + 0x10)` and
requires `0 ≤ id < rows` (`0x00608dac`), so named textures (ids after the rows, e.g. the
`effects/menugfx/…` atlases) are never scaled [s].

### 2.2 What `N` does to a text texture and its quad

- **Texture:** a generated `MPF_FONTSCALE` row is created `N·w × N·h` (`0x004f4160`, before
  `0x004f3950`) [s]. `TexGetWidth/Height` return `w, h`.
- **Glyphs:** text-line x, y and right edge ×N, then the font's own texel metrics; wrap in
  texels at `(right−left)·N`; line step `(S + ext)/N` layout px = `S + ext` texels. With an
  N× font (`S·N`, `yoff·N`) the text lands at the same layout position and size, N× denser [s].
- **Quad:** nothing in the texture or text path touches instance geometry; the `gui2d` quad is
  sized by KC in layout pixels (`B3D_InstSetSize`) and samples the texture with normalised
  coordinates, so it stays at layout size with an N× denser texture. Under the mod's projection
  scale `s` the texture is then sampled at `N/s` texels per screen pixel: exactly 1:1 when
  `s = N` [i: UV normalisation not traced in the body data].
- **Measurement for KC** stays in layout pixels (all B3D measure/size natives divide by `N`,
  truncating) [s].

### 2.3 The `0x005748b8` table: text-style offsets [s][m]

11 records of `0x44` bytes: `i32 flag`, `i16 count`, `i16 dx[10]`, `i16 dy[10]`,
`i16 pass[10]`. `0x004f8120` copies it to `0x00606c20` on **every** font open, multiplying all
three rows by 2 when `cfg+0x784 > 1`, except the records with flag 1 or 4
(`mov edi,1 / cmp [eax+0x784],edi / jle / mov edi,2` at `0x004f812d..0x004f813a`).
`0x004f8470` builds the active pass list for the style bits of a draw call (`flags & 0x3fcf`)
and its extents (`0x00606c16..0x00606c1c`); `0x004f8600` draws the `pass = 0` entries in the
shadow colour (font `+0x34`, `B3D_TexSetTextBGColor`, or automatic `0x444444`/`0xcccccc`) and
then the `pass = 1` entries in the text colour.

| Flag | Passes (dx, dy) | Meaning |
| --- | --- | --- |
| `0x2` | 8: (0,−1) (1,−1) (1,0) (1,1) (0,1) (−1,1) (−1,0) (−1,−1) | 1-px outline, shadow colour |
| `0x40 … 0x2000` | one each: (0,−1) (1,−1) (1,0) (1,1) (0,1) (−1,1) (−1,0) (−1,−1) | 1-px drop shadow in one of 8 directions |
| `0x1` | (0,0), foreground | the glyph itself |
| `0x4` | (1,0), foreground | bold (second foreground pass) |

Under `N > 1` outlines and shadows move 2 texels (1 layout px at N = 2), the glyph and bold
shift stay 1 texel. The factor is 2 for any `N > 1`.

### 2.4 The `1xxxx` texture variants [s][m]

`0x004f4160`: `N > 1` and `MPF_FONTSCALE` on a non-generated row → id + 10000 before the path
build; unnamed rows then load `tex\true\1<id>` (named rows would try `<name>_l` first; none of
the flagged rows has a name). The 8 file-backed flagged rows ask for `tex\true\10046`, `10047`,
`10056`, `10158`, `10219`, `10327`, `10568`, `10705`; none exists [m]. A miss ends in the
path→file placeholder chain of [texture-lookup.md](texture-lookup.md) §5: the path ends in a
digit, so `NONE_BLACK`, flagged `0x20000` and cached for the session. Whether a blit from it
draws black or nothing is not established. Rows 56, 568 and 705 have no `tex\true` member at
`N = 1` either [m].

### 2.5 Does `N = 3` or `4` work?

Every conversion site uses `N` generically; the only `N`-specific constant is the style
factor 2 (§2.3) [s]. Limits [s][i: arithmetic]: the surface pitch of `0x004f3950` is a
signed 16-bit value `((w·N + 15) >> 3 & ~1) · 32`, so the 1024-wide rows (6, 12, 20, 1236) stay
valid up to `N = 7`; the tallest row (8, 64×2048) is 8192 texels high at `N = 4`. The 35
writeable generated rows total 25.7 MB at `N = 1`, ×N² (102.7 MB at 2, 231 MB at 3, 411 MB at 4)
[m], held as a CPU surface plus a D3D texture [i], inside a 32-bit process. As shipped, `N > 1`
is broken for other reasons at any value: no N× fonts, no `1xxxx` files, and the KC menu,
native HUD text (texture 15) and tooltip (1230) targets lack `MPF_FONTSCALE` while the wrap
and line-step factors are unconditional.

## 3. Sharp text at a fractional scale `s` in (1, 3]

The mod's projection scale magnifies every `0x200` instance by `s` (ui-scale.md). Text stays
sharp only if the texture it is drawn into has at least `s` texels per layout pixel, so the
density must come from `-fontscale`-style supersampling of the text textures; a sharper font
alone cannot help.

### (a) Integer density `d = ceil(s)`, native path — recommended

| # | Change | Site / boundary | Contract |
| --- | --- | --- | --- |
| 1 | `cfg+0x784 = d` | launcher passes `-fontscale d` (retail parse `0x004ecff8`) | integer; set before fonts and textures load; the DLL reads `*(*0x00606f34+0x784)` |
| 2 | font request ×d | detour at `0x0048cdc0`, 6 bytes `51 53 56 57 8B F9` (`push ecx/ebx/esi/edi; mov edi,ecx`), resume `0x0048cdc6` | inputs `EAX` name, `ECX` size, `[esp+4]` cell width, `[esp+8]` flags, `[esp+0xc]` y offset; thunk multiplies `ECX`, `[esp+4]`, `[esp+0xc]` by `d` (cdecl, the four callers `0x0041c9ab`, `0x0041f7a7`, `0x00496131`, `0x0049615b` only pop), replays the six bytes. `EDX` and flags are dead at entry (`xor edx,edx` at `0x0048cdcd` before any read); no relative instruction is displaced. Startup, main thread, not reentrant. Covers KC (all three objects, all languages) and the native cockpit in one place, so no `types/Fonts` overlay and no immediate patches are needed. |
| 3 | fonts | ship `F\Tahoma{13d}`, `F\Zekton{26d}` (+ language variants), §4 | band rows `yoff·d … yoff·d + S·d − 2`; metrics ≈ d × stock so KC layout keeps its proportions |
| 4 | text targets scaled | wrap the `CALL 0x004f44a0` at `0x0048af71` (5 bytes, whole instruction; next `0x0048af76` is another `CALL`, so flags are dead; forward `EAX`) | after the Materials load and before any texture object exists (`0x004f4160` creates lazily): set `MPF_FONTSCALE` in the row flags `0x00608db0+id·0x3c+0x10` **and** the texture-table copy `0x006069ac+id·0x10+4` for every text target; clear `MPF_NOFILTERING` on them when `s ≠ d`; clear `MPF_FONTSCALE` on the 8 file-backed rows so they load their stock files |
| 5 | blit sources | detours at `0x0048c090` and `0x0048c460`: first two instructions `8B 4C 24 08` + `83 EC 24` (7 bytes), cdecl, 10 dword arguments `(src, dst, sx, sy, dx, dy, w, h, colour, clip)`, void, both recurse into themselves for composite (negative) destinations | when `dst` is scaled and `src` is not (named atlas, or a row without the flag): blit an N× upscaled copy of the source rectangle (e.g. a cached scratch surface made once per source id, then the original function with the source treated as scaled). Stateless per call; KC script thread [i] |
| 6 | style factor (only `d ≠ 2`) | imm32 of `mov edi,2` at `0x004f813a` (5-byte instruction, patch bytes `0x004f813b..0x004f813e`) | factor `d`; the table is rebuilt on every font open, so patch before the first open |

Which rows are text targets is **not** established statically: KC passes texture ids in
variables. Known: native HUD 15, 61 and tooltip 1230 [s]; the KC menu, info and tooltip
surfaces are among the 35 writeable generated rows (5–22, 61–66, 90, 236, 340, 342, 579, 922,
923, 1230, 1235–1237; 10 already flagged) [m]. Flagging all 35 is the conservative choice; its
cost is the ×d² memory and upload of §2.5. One diagnostic flight settles it (open item 1).
`B3D_TexBltImage` (two sites, x3intro only) scales its source rectangle too, so it needs the
same treatment as item 5 if its destination is flagged.

Visual result: `s = d` (1, 2, 3) gives 1:1 texels; `s = 1.25` or `1.5` with `d = 2` minifies
1.6 or 1.33 with bilinear filtering and no mips — sharp strokes with mild unevenness, far better
than the current 1.25× magnification of a 1× texture [i]. Risk: medium (hook count, the
unknown target set, native code that writes into text textures without the `0x0048…` wrappers,
memory at `d = 3`).

### (b) Fractional `s` through the 43 sites — not recommended

A fixed-point `s` would replace the 20 `IDIV` and the 20 loads feeding multiplies (18 `MOV`,
2 `IMUL`) plus the 3 `CMP` gates (sites in §2.1 and
`font_static_checks.json`) with rounded products and quotients. It fails on three points no
site edit fixes [i]: the line step `(S·s)/s` is exact only when `13·s` and `26·s` are integers
(`s = 1.25` gives 16.25 and 32.5 px fonts), so lines gap or overlap by a pixel; adjacent rects,
lines and blits round independently and leave 1-texel seams; and every blit source still needs
an `s×` resampled copy (item 5 of (a) anyway). Risk: high, for a gain only at `s` values where
(a) already minifies gracefully.

## 4. Font assets and `types/Fonts`

Rows [m] (`font_files.json`; the same rows in `03.cat types/` and `addon/01.cat addon/types/`,
no override in any later catalogue, Mayhem's `addon/07.cat` included):

| File | Rows |
| --- | --- |
| `Fonts` | `1;` · `#LARGE;Arial;24;0;` · `LARGE;Zekton;26;8;` · `HUD;Tahoma;13;1;` |
| `Fonts-L007` (Russian) | `LARGE;Harrier;24;5;` · `HUD;Tahoma;13;1;` (Zekton and Arial commented) |
| `Fonts-L034` (Spanish) | `LARGE;ZektonES;26;0;` · `HUD;Tahoma;13;1;` |

Columns: key; font name; size `S` (height and name suffix); y offset. Opened: LARGE and HUD by
each of x3story (`OpenFonts`), x3intro (`Set`) and x3galedit, all reading
`addon\types\fonts.txt`; `"Tahoma"` 13 natively (twice). Slots are shared by file name, so a
session opens two font files (`Tahoma13` and one LARGE font). Files to generate for density `d`
(item 3 of (a)):

| Language | Files | `S·d` / y offset `·d` at d = 2, 3, 4 |
| --- | --- | --- |
| all | `F\Tahoma{13d}.abc/.tga` | 26/2, 39/3, 52/4 |
| default | `F\Zekton{26d}` | 52/16, 78/24, 104/32 |
| L007 | `F\Harrier{24d}` (lookup is case-insensitive) | 48/10, 72/15, 96/20 |
| L034 | `F\ZektonES{26d}` | 52/0, 78/0, 104/0 |

Generator constraints (§1): v5 `.abc`; map every code the stock font maps (Tahoma up to U+0491
incl. Cyrillic, Zekton up to U+0200, ZektonES up to U+2215, Harrier up to U+04E9); `B ≤ 255`, `A`, `C` in
`[−128, 127]`; glyph ink inside the band; power-of-two 32-bit TGA, RGB 255, alpha = coverage;
a missing file makes `0x0048cdc0` return −1, which KC stores as the font handle and later
indexes the slot table with (`(&0x00606f0c)[-1]`), so the launcher must pass `-fontscale d`
only when every required pair is present [i: KC does not check the handle in `OpenFonts`].

Overlaying `types/Fonts` is possible but unnecessary with item 2: the resolver takes a loose
`addon\types\Fonts.*` before any catalogue and otherwise the highest catalogue that holds the
member, and ranks the `-L<lang>` variant above the plain name within one source
([loading-orchestration.md](loading-orchestration.md) steps 3–8) [s]. That KC's
`SE_ReadFile("addon\types\fonts.txt")` reaches this resolver with the `.pck` fallback and the
language suffix is inferred from the shipped `-L007`/`-L034` files, not traced [i]. The fonts
themselves can ride in the same mod catalogue the LOD overlay tooling writes
([../architecture/lod-overlay-mods.md](../architecture/lod-overlay-mods.md)) or as loose
`f\` files.

## Open items

1. **Text target set.** Which texture ids KC and native code draw text into and blit into, in
   the menus and in flight: one diagnostic row per distinct `(function, src, dst)` at
   `0x0048b2d0`, `0x0048c090`, `0x0048c460`, `0x0048b0b0` settles items 4 and 5 of (a).
2. **Direct writers.** Native code that writes into a generated texture without the
   `0x0048…` wrappers would ignore `N`; none was found among the `+0x784` readers, but other
   writers of the texture records were not enumerated.
3. **Quad UVs.** The claim that `gui2d` quads sample with normalised UVs (§2.2) is inferred.
4. **Placeholder blits** from `NONE_BLACK` (§2.4) and the `SE_ReadFile` path (§4) are not traced.
5. `.siz` grid fonts and the flag-`0x10` glyph cache path of the loader were read only as far as
   needed to rule them out for the shipped fonts.

## Reproduce

```sh
python3 verification/results/font-rendering/font_files.py          # -> font_files.json
python3 verification/results/font-rendering/font_static_checks.py  # -> font_static_checks.json (capstone)
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless <proj-dir> <proj> \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3DecompileFunctions.java /tmp/font.c 0x004f7830 0x004f8120 0x004f8600 0x004f8470 0x004f8f30
```
