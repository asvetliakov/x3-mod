# Text density: sharp in-game text under the UI scale

`--text-density auto|1|2|3` / `X3M_TEXT_DENSITY` / `text_density` in `x3m.ini` (default
`auto`). Implements strategy (a) of [font-rendering.md](../reverse-engineering/font-rendering.md)
section 3: under [`ui_scale`](ui-scale.md) `s` the engine draws its text into generated
textures at the integer density `d = ceil(s)` through its own `-fontscale N` supersampling,
and the projection minifies `d/s` with the bilinear filter instead of magnifying a `1x`
bitmap. `auto` is 1 when `s` is 1 (no UI scale), so nothing changes for users without a
UI scale; `1` is off; `2` and `3` force a density. Source: `src/proxy/text_density.cpp`,
`src/proxy/text_density_sites.h` (portable core), `src/proxy/text_density.h`; verifier
`verification/probe/verify_text_density_sites.py`; host test
`verification/analysis/test_text_density.py`; ledger
[verification/text-density.md](../verification/text-density.md); assets
`tools/fonts/generate_fonts.py` (the `d x` fonts, a separate tool) installed by
`tools/manage.py install` as loose `<game>/f/<Name><S*d>.abc/.tga` files with a manifest.

## What the engine does with N = d

`cfg+0x784 = N` (the `-fontscale` integer; [font-rendering.md](../reverse-engineering/font-rendering.md)
section 2): a Materials row flagged `MPF_FONTSCALE` is created `N x` its generated size;
text-line, rect, line and circle coordinates into such a row are multiplied by `N`; the
text-block wrap width and line step are scaled unconditionally; every measurement
returned to the script is divided by `N`; a blit into a flagged row multiplies the
destination coordinates by `N` and, when the source row is flagged too, the source
rectangle. Font opens are not scaled, the style-offset factor is 2 for any `N > 1`, and
the 8 file-backed flagged rows ask for `tex\true\1<id>` files that do not exist. The
module supplies what is missing:

| # | Change | Where | How |
| --- | --- | --- | --- |
| 1 | `cfg+0x784 = d` | the config object `*0x00606f34`, at `CreateDevice` | a plain store into the engine's heap object after a validated read; the previous value (1, or the user's own `-fontscale`) is logged and put back on unload. The object is built by the constructor `0x004ec9e0` (called at `0x0040283a`, its two `+0x784` stores `0x004ecae3` default and `0x004ecff8` `-fontscale`), before `Direct3DCreate9` at `0x00402edc`; the first font open is in the main loop (`0x00403a26` in `0x00403840`, called at `0x0040373a`), so the DLL, which loads inside `Direct3DCreate9`, writes the field before any of the 43 readers runs |
| 2 | font request `x d` | entry `0x0048cdc0` (6 bytes `51 53 56 57 8b f9`) | the thunk saves every register and EFLAGS and hands the C side the frame: EAX = name, ECX = size, `[esp+4]` cell width, `[esp+0xc]` y offset. When both loose files `f\<name><S*d>.abc` and `.tga` exist under the game directory (`GetModuleFileNameW` of the EXE, `GetFileAttributesW`), the saved ECX and the two stack arguments are multiplied by `d`; otherwise the request passes unchanged and one `text_density_font … status=missing` row names the file (a failed open would make `0x0048cdc0` return -1, which KC stores unchecked). Covers the native cockpit (`0x0041c9ab`, `0x0041f7a7`) and KC's `B3D_OpenFont` (`0x00496131`, `0x0049615b`), the four direct callers |
| 3 | text targets | the Materials load call `0x0048af71` (`call 0x004f44a0`, redirected) | the thunk pops the game's return address, calls the original with the stack exactly as the game's call left it, then edits the rows: every `MPF_GENERATED|MPF_WRITEABLE` row (35 in the shipped table: ids 5–22, 61–66, 90, 236, 340, 342, 579, 922, 923, 1230, 1235–1237) gains `MPF_FONTSCALE`, and loses `MPF_NOFILTERING` when `s != d` (the texture is then minified and must filter); every file-backed `MPF_FONTSCALE` row (46, 47, 56, 158, 219, 327, 568, 705) loses the flag so it loads its stock file. Both copies are edited: the row (`*0x00608db0 + id*0x3c + 0x10`, read by the text and blit functions) and the texture-table entry (`*0x006069ac + id*0x10 + 4`, read by the texture creator and the draw path's filter test `0x0047231a`). Refused (`objects_exist`) when a row to flag already has a texture object. Runs once per process on the init thread; `CreateDevice` applies the edit instead when it comes after the load |
| 4 | blit sources | entries `0x0048c090` `TexBltBlock` and `0x0048c460` `TexBltBlockAlpha` (7 bytes `8b 4c 24 08 83 ec 24`) | when the destination is a flagged row and the source is a static unflagged texture (a named atlas such as `gui_master`, or a file-backed row), the C side runs the engine's own leaf with a `d x` shadow of the source and every coordinate in texels: the destination clip in layout units as `0x0048c2c8..0x0048c32c` does it (only with the clip argument), then `0x004dbbe0` (plain copy, colour < 0), `0x004ee990` (colour copy) or `0x004efa70` (alpha) with EAX = destination object, EDI = shadow. The thunk returns the destination object in EAX (dead at both entries) and `ret`s; a zero result continues into the original. Composite (negative) destinations recurse through the same entry per part and pass; a generated unflagged source (dynamic content) passes and is reported once under debug. Shadows are built once per source id (`0x004f3950(d*W, d*H, 0x402c)`, 32 bpp, the flags of a generated row; `d*W` column copies from the source, then `d*H` in-place row copies from the last row up through the plain-copy leaf: nearest neighbour), capped at 16 sources, 96 MB and 4096 texels per side, freed with `0x004f38d0` before every device `Reset` and rebuilt on demand |
| 5 | style factor | imm32 at `0x004f813b` (`mov edi,2`) | `d` when `d = 3` (one `lock cmpxchg8b` inside the qword `0x004f8138`); stock 2 is right for `d = 2`; put back on unload |
| 6 | diagnostics (`--debug`) | entries `0x0048b2d0` text line (6 bytes) and `0x0048b0b0` rect fill (8 bytes), plus the two blits | one `text_density_draw fn= src= dst= dst_flagged= src_flagged= src_generated= handled=` row per distinct (function, src, dst), 96 at most, printed on first sight: settles open item 1 of the note (which ids KC and native code draw text and blit into) |

## Transaction, rollback, threading

Every window is byte-compared at `initialize()` (fail closed: the sites, the helper
functions' prologues and the whole lookup and plain-copy bodies, the config stores, the
draw path's flag read); the four production claims (font open, the two blits, the
Materials call) and, under `--debug`, the two diagnostic entries are claimed on the
backend-load path in that order, all or none, and stay inert until `device_created`
raises `active_`. The density is resolved right after `ui_scale::device_created` from
`ui_scale::scale()` (1 when `ui_scale` is off or refused): `d = 1` restores the claims
and logs `status=off reason=density_1`. The font gate comes next: every one of the four
families (`Tahoma{13d}`, `Zekton{26d}`, `ZektonES{26d}`, `Harrier{24d}`, `.abc` and `.tga`
each) must exist as a loose file, otherwise nothing is flagged, the field is written 1,
the claims go back and the install row says `status=refused reason=fonts_missing
missing=<names>` (with `N > 1` and 1x fonts the engine would draw 1/d text at a 1/d
line step). Then the config field, the style imm32 (`d = 3`) and the rows (when the
Materials load already ran) go in, in that order, and a failure of any of them undoes
the earlier ones and every claim (`disable()`), so the engine never runs with `N > 1`
and unflagged text targets. Before the rows are flagged the largest row to flag (64x2048
row 8 in the shipped table, 6144 texels high at `d = 3`) is checked against the device's
`D3DCAPS9 MaxTextureWidth/Height` (documented `GetDeviceCaps`; 0 = unknown, no limit):
the density drops to the largest one that fits (re-gated against the fonts, the style
imm32 put back when it leaves 3), logged as `caps_limited_d=`; when even 2x does not
fit the option is refused (`caps`). The check runs at `CreateDevice` when the Materials
load already ran, else in the Materials thunk (before any font open or texture). A
`Reset` under `ui_scale auto` that changes `s` does not change `d`: fonts and textures
are already built at the old density (logged once by the ui_scale row; the projection
still minifies correctly since `d >= s` for every step below 3).

Threading: the font opens, the Materials load and the blits run on the engine's main
thread (the KC VM and the native cockpit are on the same thread as the render loop;
[gui-scale.md](../reverse-engineering/gui-scale.md)); the shadow cache takes an SRW lock
anyway. Every thunk saves and restores EFLAGS (DF cleared for the C ABI) and all eight
registers around one cdecl call; the blit thunks return through the saved-EAX slot and
end with `test eax,eax`, whose flags are dead (the displaced `sub esp,0x24` writes them
and `test ecx,ecx` at +7 rewrites them before any reader). The displaced instructions
are ESP-relative loads and pushes that the tail replays with the same ESP (a `jmp` moves
no stack). LastError is saved and restored around every entry point. `shutdown()`
(dynamic unload only) puts the config field, the imm32 and every claim back and writes
one `text_density_restore` row to the log handle without the capture lock; the rows
stay flagged and the fonts already opened stay at `d` (the engine's own state, not
undone), so a game that keeps running after a dynamic unload draws its text broken.
Blit-path reads of the three table slots (`*0x00608dac`, `*0x00608db0`, `*0x006069ac`)
happen on every call as the engine's lookup does them: the texture table is reallocated
in 1000-entry steps when named textures are added, so no pointer is kept. A source whose
shadow was refused (budget, slots, size, allocation) is remembered (32 ids) so later
blits from it pass straight through.

Per-call cost: a blit into a flagged row costs the row-flag reads, one texture lookup
(the original does two) and a 16-entry probe; a text line or rect fill costs nothing
outside `--debug`. A shadow build is `d*(W+H)` leaf calls (about `d*W*H` four-byte
copies; a 512x512 atlas at `d = 2` in a few ms, once per session per source, logged
with its duration).

## Assets and the install step

`d = 2` needs `f\Tahoma26` and the LARGE font of the language (`Zekton52`, `ZektonES52`
for `-L034`, `Harrier48` for `-L007`); `d = 3` `Tahoma39` / `Zekton78` / `ZektonES78` /
`Harrier72`. `tools/manage.py install` copies every `.abc`/`.tga` from `build/fonts/F`
(`--fonts-dir`) into `<game>/f` before the DLL goes in, creating the folder, and records
the copied names and hashes in `<game>/f/x3m-fonts.json` (rewritten after every copy,
so a copy that fails half-way leaves nothing unowned) and in the install record
(`density_fonts`); the names and hashes are printed. A file already there that the
manifest does not own (or whose bytes changed since) is left alone and reported.
`uninstall` removes the owned, unchanged files and the manifest; `rollback` leaves them
(the previous DLL does not ask for them). The install row lists which of the four
families the game directory holds at the resolved density (`fonts=`) and, when the gate
refused, the missing names (`missing=`); with the gate the per-open guard only matters
for a font added by a mod, which is opened at `1x` and logged.

## Known limitations and open items

- Fonts packed into a catalogue are not seen by the existence check (loose `f\` files
  only), so the request stays unscaled.
- `B3D_TexBltImage` (two sites in the KC dispatcher, x3intro only) scales its source
  rectangle too and is not intercepted; icons the intro draws into flagged rows would be
  small.
- A source whose content changes under the same id would keep its first shadow (the
  cache is by id; only file-backed sources are shadowed).
- The engine's blit trace (`[0x00608518]+0x24 & 1`, a developer switch) is not called
  for the shadowed blits.
- What the first flight must show (`--ui-scale 1.25 --debug`, `d = 2`): the
  `text_density_install … status=patched rows=35/8/29 fonts=Tahoma26,Zekton52,…` row,
  `text_density_font … status=scaled` for Tahoma and the LARGE font, sharp menu and HUD
  text, icons in menus at their usual size (the shadow path: `text_density_shadow …
  status=built` and `text_density_draw … handled=1` rows), the text of texture 15
  (native HUD), 61 (`0x00426e10`) and 1230 (tooltip) laid out as before, no black
  squares where the 8 unflagged file-backed rows draw, and the `text_density_draw` set
  (which ids text and blits really reach: if a row outside the 35 receives text, or a
  generated unflagged source feeds a flagged row, the set needs widening).
