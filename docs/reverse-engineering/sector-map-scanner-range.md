# Sector map scanner coverage: why the disc turns into a dented polygon

Static study of the installed `X3AP.exe` (SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, base `0x00400000`) and the
KC objects `addon/04.cat:L/x3story.obj` (stock AP, build 1510658513, `ed5786a0…`) and
`addon/07.cat:L/x3story.obj` (Mayhem 4, build 1462141839, `2c3101a8…`, the object this install
loads; [gui-scale.md](gui-scale.md) §7), 2026-10-27, after a user report (X3M 1.7.2, native
Windows, 2560x1440, `ui_scale` auto = 1.5, `text_density` auto = 2, LxXRM 3.1). Ghidra 12.1.4
headless (`X3DecompileFunctions.java` on `~/x3-ghidra-research`, `-readOnly -noanalysis`),
objdump for exact operands, the KC reader `verification/results/ship-navigation-ai/gate-queue/kc_obj.py`.
Evidence script `verification/results/gui-scale/sector_map_scanner_range.py` (output
`sector_map_scanner_range.txt`, 30/30 checks); it also carries an instruction-level port of the
rasteriser used for the emulation below. Raw decompiler output and KC listings stayed in the
session scratchpad. Nothing was launched. Marks: **[s]** static reading, **[m]** measured (byte
read or script/emulation output), **[i]** inferred.

## Answer in brief

1. **Who draws it.** KC class 2216 (the sector map, parent 2200), method `SpecialUpdate`, the
   only `B3D_TexCircleFill` call in either story object (stock CODE `0x135e96`, Mayhem
   `0x14e875`): `TexCircleFill(1236, cx, cy, r, 65536, colour)` once per entry of the range
   list, into texture **1236**, the map window's own texture. 1236 is one of the rows X3M flags
   `MPF_FONTSCALE` ([text-density.md](../architecture/text-density.md) item 3), so under
   `d = 2` every coordinate is doubled into a `2x` texture [s][m].
2. **Cause: candidate (a), but not a clamp or a 16-bit field.** The filled-ellipse rasteriser
   `0x004ef620` is a midpoint algorithm whose decision variables and region-exit products are
   32-bit and grow as about `4r³` and `r³` (`r` in texels). Its spans become wrong from
   **r = 826 texels** (dents of 3–12 %) and the shape collapses to about 0.45 r from
   **r = 1291** (`r³ > 2³¹`) [m: emulation]. Clipping is per span and correct; it only decides
   which of the wrong spans are visible. `d = 2` doubles the texel radius of the same on-screen
   disc, so the defect starts when the disc's layout radius reaches about 413 px, which at
   this window size is the window's half-width. The user's 100 % profile (596/530/584/655 px at
   0/25/45/60 deg) is reproduced by the emulation at r ≈ 1633 texels within 2 px rms [m].
   Candidate (b) is not involved: the disc is not a scene node and reads no scaled quantity;
   `ui_scale` only enters through the virtual screen size, which sets the window's layout size [s].
3. **Stock.** The same routine at `N = 1` distorts every disc with a radius of 826 px or more.
   It is not a clean clip. In the stock game this needs a disc about 1.6x the window
   half-width or more, so it is rare (huge ranges, the advanced satellite, 2x zoom) [m][i].
4. **Fix location (implemented, §5).** Entry claim at `0x004ef620` (7 bytes, two whole instructions, one aligned
   qword, single caller): radius ≤ 813 texels runs the original; larger radii run a 64-bit port
   of the same algorithm through the engine's own span leaf. A configuration change
   (`text_density = 1`) only moves the threshold back to 826 layout px and gives up sharp text.
   It is not a fix (§5).
5. **Mayhem.** `07.cat` keeps the same path: one `TexCircleFill` site, in 2216::SpecialUpdate,
   into 1236, fed by the same `UpdateRangeData` list (`GetObjectsPlayer` × `GetScannerRange`).
   Any other KC-drawn disc would need a second `TexCircleFill` site, and there is none [m]. The
   menu's own placement preview (`SA_AllocObject`, flag 1024, `SA_StartObjectInSpace` in
   `SpecialUpdate`) is the likely carrier of the satellite's disc: if it is player-owned, its
   range enters that list. Its owner was not traced [i].

## 1. KC side: class 2216 [s][m]

| What | Stock 04 | Mayhem 07 |
| --- | --- | --- |
| `B3D_TexCircleFill` sites in the object | 1, `2216::SpecialUpdate@0x135e96` | 1, `2216::SpecialUpdate@0x14e875` |
| `B3D_TexCircle` | name not in STRG (unused) | same |
| range list | `UpdateRangeData` `0x131f2b`: every 1 s, `m126 = [pos, range]` pairs for `m33.GetObjectsPlayer()` with `GetScannerRange() > 0`, then `SE_CompressRangeData(m126, m127)` | `0x14a65b`, same, members `m124/m125` |
| texture | `pushw 1236` at `0x135d6a` | `pushw 1236` at `0x14e6d9` |

Per entry (stock names; `hw = m81/2`, `hh = m80/2` window half-size in layout px, `m99` =
sector extent `m98 · 1.125` (`SpecialMenu`), `m130` = zoom 16.16, `m133/m134` = view centre
16.16):

```text
cx = MulFix(MulDivFix(P.x, hw, m99) + hw - (m133 >> 16), m130) + hw
cy = MulFix(MulDivFix(-YZProjection(P.y, P.z), hh, m99) + hh - (m134 >> 16), m130) + hh
r  = MulFix(MulDivFix(R, hw, m99), m130)
TexCircleFill(1236, cx, cy, r, 65536, 0x102030 + alpha)      alpha 0xC0000000 or 0xD0000000 by m103
```

The window is first filled with `0x0A111A` + an alpha of `0xD0000000` or `0xF0000000`
(`TexRectFill(1236, 0, 0, m81, m80, …)`). Discs are drawn only while `m130 ≤ 131072`. Above 2x
zoom the whole window is filled with the range colour instead (`0x135eaa`). Mayhem has the
same structure, with other alpha constants (`0xEA…`/`0xF2…` alternatives) and optional colours
from text page 8378 when text 68 is set (`0x14e72c..0x14e780`).

**Window size.** 2200::`Resize` sets `m80 = GetMenuMaxHeight()`, `m81 = GetMenuMaxWidth()`.
2216's versions return 512 when not maximised. Maximised they return
`min(SW − 512, SH − 128)` clamped to [512, 1024], with `SW, SH` from
`B3D_ScreenGetWidth/Height`, which X3M's sites B/C make the virtual size `round(W/s)`,
`round(H/s)`. At 2560x1440, `s = 1.5`: `min(1195, 832)` = **832** layout px (1248 real px; the
user measured a 1244 x 1234 interior). At `s = 1`: `min(2048, 1312)` → **1024** [s].
So `r` scales with `hw`: the same sector and zoom give an 832/1024 smaller layout radius at
`s = 1.5` than at `s = 1`.

## 2. Native path [s][m]

1. **Dispatch.** KC native 121 (`B3D_TexCircleFill`, name table `0x0057a420`) → jump table
   `0x0049679c` → case `0x004962e2`. Six dword operands at `[rec+1/6/0xb/0x10/0x15/0x1a]` are
   pushed as `0x0048bee0(id, cx, cy, r, aspect, colour)` (`0x004962fd`). Native 120
   `B3D_TexCircle` is the outline twin (`0x0048bd30` → `0x004ef180`), unused by the scripts.
2. **`0x0048bee0`.** A negative id recurses over a composite's parts (loop at `0x0048bf2c`). For `id ≥ 0`: `0x004f5110` looks the surface
   up (`ecx` = object; `+4` width, `+6` height in **texels**). It rejects a circle wholly
   outside in **unscaled** coordinates against the texel size (`0x0048bfd3..0x0048c007`), which
   is harmless: it only rejects less. If `[*0x00608518+0x24] & 1`, it hands the unscaled call to
   `0x00475800` (16-bit fields; `0x00477cfb` decodes such a record and calls `0x0048bee0`; a
   remote or recording channel, purpose not traced [i]). **Scale block** `0x0048c02d..0x0048c065`: for a row with `MPF_FONTSCALE`
   (`+0x10 & 0x10000`), `cx, cy, r *= cfg+0x784`. It then calls `0x004ef620(cx, cy, r, colour,
   aspect)` with EDI = surface (`0x0048c076`) and tail-jumps to `0x004f5110` with the id.
3. **`0x004ef620`, filled ellipse** (only caller `0x0048c076`):
   - `r` is read as a 16-bit value (`movsx esi,ax`, `0x004ef62a`); `cx, cy` as 16-bit words. A
     clip flag is set when `cx ± r` or `cy ± r` leaves `[0, width) × [0, height)`
     (`0x004ef635..0x004ef66f`).
   - The surface is locked if needed (`[edi+0x30]`, `0x004dcd30`, `+0x10 |= 2`). A span leaf is
     chosen by the pixel class from `0x004ee6e0`: `0x004b32f0`, `0x004b3680` (16-bit),
     `0x004b3580` (24-bit), `0x004b3340`, `0x004b3620` (32-bit). Any other class prints
     "FilledCircle: Unknown Blittype". A leaf is `leaf(surface, x0, y, x1, colour)` (cdecl, 16-bit
     coordinates). The 32-bit leaf `0x004b3620` **stores** the colour converted by `0x004dc0b0`
     over `x1 − x0 + 1` pixels without blending, so the disc's translucency is the texture's alpha
     byte [s]. The other leaves call `0x004dc0b0` or `0x00518de0` (a CRT fill) and were read only
     that far [i].
   - `A = r²`, `B = A / (aspect² in 16.16)` via x87 (`0x004ef703..0x004ef72d`; `B = A` for the
     script's 65536).
   - **Region 1** (top and bottom caps, `x` from 0 up): `d = 2A(1 − B) + 2B·r(r − 1) + B`
     (`0x004ef732..0x004ef76b`). Per step: if `d ≥ 0` (`test ebp,ebp; jl`, `0x004ef7a8`), emit
     rows `cy ± y` from `cx − x` to `cx + x`, `y −= 1`, `d −= 4B·y`; then `d += 2A(2x + 3)`,
     `x += 1`. It continues while `y·B > x·A` (`cmp edx,ecx; jg`, `0x004ef8af`).
   - **Region 2** (the sides, `y` down to 0): `d = 2B((y − 2)y + 1) + 2A·x(x + 1) + A(1 − 2B)`
     (`0x004ef8bb..0x004ef8ea`). Per row: emit; if `d ≤ 0` (`test esi,esi; jg`) then `x += 1`,
     `d += 4A·x`; `y −= 1`, `d += 2B(3 − 2y)`; until `y = −1`.
   - **Clipping** (flag set) is per span: left clamped to 0, right to `width − 1` (signed
     16-bit), empty spans skipped, and each row tested unsigned 16-bit against the height
     (`cmp cx,[edi+6]; jae`). This is exact: it removes only what lies outside.
   - Every `imul` on `d`, `A`, `B` and the products is 32-bit (17 of them). Nothing is 64-bit
     and nothing saturates.

## 3. Why the shape breaks, and why `d = 2` makes it visible

With `A = B = r²`, the decision variables carry `r²` times a distance in pixels, so in exact
arithmetic `|d|` reaches about `4r³`. The terms `2A(1 − B)` (`≈ −2r⁴`) and the products wrap,
but modular arithmetic keeps the sum right as long as the true `d` fits in an `int32`. Once
it does not, the sign tests `d ≥ 0` / `d ≤ 0` read a wrapped value and step `y` or `x` at the
wrong rows. The emulation ports the routine instruction for instruction (32-bit wrap,
16-bit coordinates) and compares it with the same algorithm in unbounded integers [m]:

| Radius (texels) | Result |
| --- | --- |
| ≤ 825 | spans identical to exact arithmetic (every r from 1 to 825 checked) |
| ≤ 813 | every decision value fits in an `int32` (max \|d\| at 813: 2 109 152 079); from 814 a value wraps |
| 826 | first radius where a wrapped value flips a decision, so the spans differ |
| 900 / 1000 / 1100 | boundary/r at 15–75 deg: 0.966 / 0.93–0.95 / 0.89–0.92; 0 and 90 deg stay 1.0: concave dents between the axes |
| 1291 | `r³ > 2³¹`: the region-1 exit product `y·B` wraps negative after the first step, region 2 starts near the top with a decision value far outside `int32`, and the boundary falls to 0.43–0.53 r at 0–60 deg, 0.67 at 75 deg, 1.0 at 90 deg |
| 1640 | 0.488, 0.453, 0.432, 0.455, 0.475, 0.534, 0.669, 1.000 at 0, 15, 25, 35, 45, 60, 75, 90 deg |

**The user's screenshot** (`s = 1.5`, `d = 2`, real px = texel · s/d = 0.75): searching
r = 813…1699 for the measured 596/530/584/655 px at 0/25/45/60 deg gives r = 1633 texels
(598/528/582/656, rms 1.9 px), with 1640 next [m]. That is a layout radius of about 817 px, a
real disc radius of about 1225 px. The polygon is the collapsed regime, about half the true
radius, and above 60 deg it reaches past the window top (the "flat" part). At 50 % zoom the
same disc is about 410 layout px = 820 texels, just under the onset (max boundary deviation
0.12 %): a perfect circle, as reported [m]. Neither the 16-bit fields nor the clip bounds are
involved: r = 1633 and `cx, cy` are far from 32767, and the clip compares texel coordinates
against the texel size of the 2x surface, which is consistent.

**`ui_scale = 1` "fixes" it** (`d = 1`, window 1024): the same zoom gives
r ≈ 817 · 1024/832 ≈ 1006 texels, in the dented regime (boundary ≥ 0.93 r ≈ 935 px). With the
ship near the centre that is beyond the window's half-diagonal (724 px), so the emulation
covers all 1024 rows edge to edge and no edge is visible. The disc is still wrong outside the
window [m for the emulation, i for the ship position].

## 4. Stock behaviour [m][i]

The overflow is in the stock routine and does not depend on `N`. At `N = 1` any disc with a
radius of 826 px or more on the window texture is dented, and from 1291 px it collapses. That
is not a clean clip. In the stock game, layout = real pixels and the maximised window is at
most 1024 px (half 512), so the bug shows only for a disc that reaches far outside the window
with its edge still visible: a ship near the window edge, 2x zoom, or very large ranges such
as the advanced satellite. Nobody has observed the stock case; this is the emulation's
prediction. X3M's `d = 2` halves the layout radius at which it starts (to about 413 px) and
puts the collapse (about 646 px layout) within one or two zoom steps of any disc that just
fills the window.

## 5. Fix location (implemented 2026-10-27)

**Implemented as an entry claim at `0x004ef620`**, on by default (`circle_fill_fix = on|off`, `X3M_CIRCLE_FILL_FIX`, `--circle-fill-fix`): `src/proxy/circle_fill.cpp` (claim, thunk, C side),
`src/proxy/circle_fill_sites.h` (the window, the compared regions, the B quotient, the class map and the 64-bit
rasteriser), `verification/probe/verify_circle_fill_site.py` (24 checks) and `verification/analysis/test_circle_fill.py`
(the port against this note's unbounded emulation, span for span). It is not tied to the text-density transaction; design and behaviour in [text-density.md](../architecture/text-density.md), "Oversized disc", evidence in
[the text-density ledger](../verification/text-density.md). The facts below are the contract it was built on.

| Item | Fact |
| --- | --- |
| Span | `0x004ef620..0x004ef626`: `sub esp,0x4c` (3) + `mov eax,[esp+0x58]` (4), bytes `83 ec 4c 8b 44 24 58`; continuation `0x004ef627 push ebx` [m] |
| Patch | 5-byte `jmp` at `0x004ef620`, inside the aligned qword `0x004ef620..0x004ef627` (one `lock cmpxchg8b`); `0x004ef625..26` become dead padding [m] |
| Inbound | one direct call (`0x0048c076`); no image dword equal to `0x004ef620`; no rel8/rel32 encoding anywhere in `.text` targets `0x004ef621..0x004ef626` [m]. The call site `0x0048c076` itself straddles two qwords (`& 7 = 6`), so it is not used |
| Inputs | `[esp]` return `0x0048c07b`; `[esp+4]` cx, `[esp+8]` cy, `[esp+0xc]` r (already ×N; the routine reads the low 16 bits), `[esp+0x10]` colour, `[esp+0x14]` aspect 16.16; **EDI** = surface object (register argument; `+0` locked pointer, `+4/+6` width/height i16, `+0x10` flags, `+0x30` backing handle) [s] |
| Dead at entry | EAX (written by the displaced `mov` before any read), ECX (caller's copy of the surface; first write `0x004ef62f` precedes any read), EDX (first write `0x004ef644`), EFLAGS (written by `sub esp`) [s] |
| Must preserve | EBX, ESI, EBP: the original saves them and the caller reads EBP after return (`mov ecx,ebp`, `0x0048c080`) before popping. EDI is popped by the caller, but keep it. Return value unused (the caller tail-jumps to `0x004f5110`, which writes EAX first). Caller cleans 0x14 bytes, so the replacement `ret`s plainly [s] |
| Reentrancy | reached only from `0x0048bee0` (KC native 121 on the main thread inside the script VM; its composite recursion; the record replay `0x00477cfb`, thread not traced). The leaves call only `0x004dc0b0` or `0x00518de0`. No state is needed beyond the frame [s] |

Behaviour: the thunk reads `movsx` of the word at `[esp+0xc]`. If it is at most 813 (every decision value
fits in an `int32`, measured for each r; first divergence of the spans 826; negative values keep the stock path), it runs the two displaced
instructions and jumps to `0x004ef627`, which gives stock pixels. Otherwise it calls a C port
of the routine with 64-bit `d`, `A`, `B` and products (the emulation's unbounded variant: it is
identical to the engine for r ≤ 825 and is the exact midpoint disc above). The port keeps the
engine's 16-bit clip and row tests, locks the surface and picks the leaf exactly as
`0x004ef677..0x004ef703` does (`0x004dcd30`, `0x004ee6e0` with ECX = surface and EAX = 0, the
five leaves). Because the 32-bit leaf stores rather than blends, the stock double emission of row
`cy` is harmless. Cost: O(r) integer steps and at most two leaf calls per row, and only rows
inside the texture write pixels. Native parity: engine code and documented calls only, no
Wine dependency. x87: the port is integer only, built -mno-sse; the original's own x87 use (`fild`,
`fdivp`, ftol) stays on the small-radius path.

Configuration alone: `text_density = 1` with `ui_scale = 1.5` keeps texel = layout. The 100 %
disc (about 817 px) then sits just under the onset, but 2x zoom (about 1634) collapses even in
the stock game, and text loses its density. Not a fix.

## Open items

1. The user plays LxXRM 3.1; its catalogues were not read here (this install has Mayhem). The
   brief's figures assume the same class 2216 code, as in both objects read.
2. The mapping of the menu's 50/100/200 % labels to `m130`, the 200 % (Argon Prime) radius, and
   the ship's position in the window are not traced. The fit uses only the user's four radii.
3. That Mayhem's advanced satellite placement reaches the list through the preview object
   (`SA_AllocObject` + `GetObjectsPlayer`) is inferred: the preview's owner and the MSCI script
   that opens the placement were not traced. A disc drawn as a 3D scene object instead would
   not go through `TexCircleFill` at all and would not show this defect.
4. `B3D_TexCircle` (`0x0048bd30` → `0x004ef180`) probably has the same 32-bit midpoint
   arithmetic. It is unused by both story objects and was not examined.
5. Emulation only: no capture of the 1236 texture was taken; the onset, collapse and fit are
   measured on the port, not on the game.

## Reproduce

```sh
python3 -I verification/results/gui-scale/sector_map_scanner_range.py /mnt/c/X3 \
  > verification/results/gui-scale/sector_map_scanner_range.txt     # RESULT PASS, 30 checks
JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64 flock /tmp/x3-ghidra.lock \
  ~/ghidra_12.1.4_PUBLIC/support/analyzeHeadless ~/x3-ghidra-research X3Render \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3DecompileFunctions.java <scratch>/decomp.txt \
  004ef620 0048bee0 0048bd30 004b3580 004b3620 004b3680     # local only, untracked
```
