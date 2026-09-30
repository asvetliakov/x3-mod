# Generated bitmap fonts for sharp text under the UI scale

The UI-scale patch ([ui-scale.md](ui-scale.md)) renders text at integer density `d = ceil(s)`
through the engine's native `-fontscale` path, which asks for fonts `d` times larger than stock
([font-rendering.md](../reverse-engineering/font-rendering.md) §3 (a) item 3, §4). The stock
catalogues only hold the 1× fonts, so `tools/fonts/generate_fonts.py` builds the `d`× fonts from
bundled open-licence TrueType fonts. This note covers what is generated and how; the format is
specified in font-rendering.md §1 and not repeated here.

## Output

`python3 tools/fonts/generate_fonts.py` writes, for each density in `--density` (default 2 and 3),
eight files under `--out` (default `assets/fonts/generated`), in the folder layout the install
step copies into the game's `f/` folder unchanged:

```text
assets/fonts/generated/F/Tahoma{13d}.abc/.tga    HUD font, all languages     (Tahoma26, Tahoma39)
assets/fonts/generated/F/Zekton{26d}.abc/.tga    LARGE, default               (Zekton52, Zekton78)
assets/fonts/generated/F/ZektonES{26d}.abc/.tga  LARGE, -L034 Spanish         (ZektonES52, ZektonES78)
assets/fonts/generated/F/Harrier{24d}.abc/.tga   LARGE, -L007 Russian         (Harrier48, Harrier72)
```

Since 0.9.0 the 16 files (about 23 MB) are committed as ordinary repository files (no LFS,
marked `binary` in `.gitattributes`): `tools/manage.py install` copies them from there by
default (`--fonts-dir`), and the release zip (`tools/release/package.py`) ships them as `f/`,
to be extracted next to `X3AP.exe`. A default regeneration rewrites them in place;
`test_font_generator` (`CommittedFontsTest`) regenerates into a temp dir and requires every
committed file to match by sha256.

Measured at d = 2 / 3: atlases 256×1024 / 1024×1024 (Tahoma), 512×1024 / 1024×1024 (the LARGE
fonts); TGA 1.0–4.2 MB each; the `.abc` files are byte-for-byte the size of their stock
counterparts (17,054 / 7,628 / 24,750 / 10,102 B), since the code map and glyph count equal the stock
ones. The generator prints glyph count, image size and bytes per file.

## Format decisions

- **Code set.** Each file maps exactly the codes its stock font maps (611 / 273 / 302 / 314,
  highest code U+0491 / U+0200 / U+2215 / U+04E9) and has the stock glyph count: glyph 0 is the
  space, which every unmapped code up to the highest code also draws, as in the stock files. The
  set comes from `tools/fonts/stock_fonts.json` (code ranges and layout targets only), written by
  `tools/fonts/extract_stock_codes.py` from the installed catalogues, so generation does not need
  the game. Harrier's C1 codes 0x82–0x9f are drawn as their cp1252 characters (0x8d, 0x8f, 0x90,
  0x9d are undefined there and stay blank with the space advance) [i: the stock glyphs were not
  compared]. A character missing from the source font is drawn from its NFKC equivalent (U+037E
  → `;`), U+00AD as `-`, otherwise from Noto Sans; with the bundled fonts nothing falls back to
  Noto and nothing is blank except the four Harrier codes [m].
- **Band.** Cells are `yoff·d + S·d − 1` rows; ink sits only in rows `yoff·d … yoff·d + S·d − 2`
  (the blit is `S·d − 1` rows from `v0 + yoff·d`, font-rendering.md §1.3). The rows above the
  band stay empty so the band check is exact per cell. Cells are shelf-packed in code order into
  the smallest power-of-two atlas, with a 1-texel gap when it costs no area; `u0 = x/W`,
  `v0 = y/H` are exact floats.
- **Vertical metrics.** The source is scaled so the H cap height is `d ×` the stock cap height
  (Tahoma 8 px, Zekton/ZektonES 16, Harrier 14) and the baseline sits at `d ×` the stock baseline
  row, both on whole rows, so flat tops and bottoms are crisp. Marks that would leave the band
  (accents on capitals, low descenders) are squeezed vertically into the rows between band edge
  and cap line or baseline instead of being cut; the letter body is never resampled. Measured
  squeezed glyphs at d = 2: Tahoma 155 (capitals with accents, and g j p q y by under half a pixel),
  Zekton/ZektonES 64, Harrier 22. The stock Tahoma13 cuts 71 glyphs at the band top instead.
- **Horizontal metrics.** Per glyph: `A` = left edge of the ink (snapped to the nearest texel,
  ≤ 0.5 px shift, so left stems land on whole texels), `B` = ink width including the stroke
  floor and the shadow,
  `C` = advance − `A`. The advance comes from the unhinted 1000-px outline. The mean a–z advance
  is fitted to `d ×` the stock mean on the rounded advances: Noto Sans through its width axis
  (wdth 80 at SemiBold), then a residual horizontal scale; Exo 2 has no width axis, so its outline is
  scaled horizontally (0.864–0.866 for Zekton/ZektonES, 1.005–1.006 for Harrier). Glyphs are rendered at
  4×4 supersampling, passed through the family's stroke floor below and box-filtered (17 coverage levels; the stock files have 15–16).
- **Stroke-width rule.** Every stroke, vertical and horizontal, has at least `d` texels of
  white coverage: a 1-px stock stroke is `d` texels at density `d`, and a thinner stroke vanishes
  where the text texture is minified with nearest sampling (`s = 1.25`, `d = 2`: screen samples
  every 1.6 texels; run 392, [font-rendering.md](../reverse-engineering/font-rendering.md) §5).
  Two floors exist, chosen per family in `SOURCES` (`floor`). The LARGE family uses the binary
  floor: `_stroke_floor` works on the supersampled mask binarised at half coverage. A horizontal ink run that continues a straight
  edge in the neighbouring row and is narrower than `d + 1` texels is redrawn as
  `max(d, round(width))` whole columns from the nearest column boundary (stem hinting). A
  diagonal or curved run narrower than `d` is widened to `d` in place. Vertical runs (bars) get
  the same treatment in whole rows, growing downwards, or upwards when they sit on the baseline
  or would leave the band, so cap line and baseline do not move. Strokes of `d + 1` texels or
  more are left as drawn, so Exo 2's 4-texel stems are unchanged. The HUD family uses the grey
  floor (`_grey_floor`): the grey coverage is dilated by the width that the thinnest stem
  (`l i ! |`) or bar (`- T`) lacks, with no binarisation, so edges stay antialiased. At Noto
  SemiBold nothing lacks width (thinnest stem 2.18 texels at d = 2), so the floor is a guard
  and does not change the glyphs. The check is
  `verification/results/font-rendering/stem_coverage.py`: the median white coverage over a
  glyph's inked rows (for `i l r ! | I t j і ї г`) or inked columns (for `- T`) must be ≥ `d` for all eight
  generated fonts; `test_font_generator` runs the same check. `A`, `C`, the band and the
  advances are unchanged by the rule.

  The run 392 fonts had `i l r` at 1.15 texels because of a sign error in the left-edge snap:
  the pen origin was moved by +2·shift instead of −shift, so up to one texel of ink left of
  column `A` fell outside the resample box and was cut. The source stems are 1.91 px (`l`) and
  1.97 px (`I`) at d = 2. The wdth 87 instance and hinting at the 4× render size were not the
  cause: the rendered stems at 89.6 px match the 1000-px outline to within 1 %.
- **Weight.** HUD: Noto Sans SemiBold (600), heavier than stock Tahoma's stem/cap of 1/8 on
  purpose (stems about 2.65 texels at d = 2; see "Readability under minification"). LARGE: stem
  width over cap height matches the stock fonts after the horizontal fit, Exo 2 Medium (0.145 × 0.866 ≈ 0.126 vs Zekton 2/16;
  0.145 vs Harrier 2/14).
- **Image.** TGA type 2, 32 bpp, top-left origin (descriptor 0x28), no footer. RGB 255 and alpha
  = coverage, except Tahoma, which bakes the stock black drop shadow: coverage convolved with a
  3×3 kernel solved from stock Tahoma13's H, g and o cells (right of a stem 0.65 / 0.45 alpha,
  under a stem end 0.16 / 0.27 / 0.16), magnified to `d` by block replication, clamped, and
  composited under the glyph (RGB = 255 · glyph / total alpha). Zekton, ZektonES and Harrier
  carry no baked shadow, as in stock (§1.4).
- **Header.** The four unread floats hold cell height, 0, the shadow reach `2d` (Tahoma) or 0,
  and `S·d`; the record pad is 0.

## Metric ratios versus stock (measured)

Ratio = generated value / (`d` × stock value); advance = `A + C`. From
`verification/results/font-rendering/font_generator_report.json`, produced by the
regeneration command below with `--report`.

| Font (d = 2 / 3) | mean a–z advance | H advance | H `B` | g advance | g `B` | space |
| --- | --- | --- | --- | --- | --- | --- |
| Tahoma26 / 39 | 1.000 / 1.000 | 1.071 / 1.048 | 1.000 / 1.042 | 1.000 / 1.000 | 1.071 / 1.095 | 0.833 / 0.778 |
| Zekton52 / 78 | 1.000 / 1.000 | 1.038 / 1.026 | 0.955 / 0.939 | 0.958 / 0.944 | 1.050 / 1.033 | 0.900 / 0.867 |
| ZektonES52 / 78 | same as Zekton | | | | | |
| Harrier48 / 72 | 1.000 / 1.000 | 0.900 / 0.911 | 0.955 / 0.939 | 0.958 / 0.944 | 1.167 / 1.148 | 0.900 / 0.933 |

The mean advance is fitted, so a line of lower-case text has the stock width; single letters
keep the substitute's proportions (within ±15 %), and the space is 7–17 % narrower than
`d ×` stock. Line pitch is the engine's `S·d` and unaffected.

## Licences

Noto Sans (Version 2.015) and Exo 2 (Version 2.010), both SIL OFL 1.1, from the Google Fonts
repository at commit `23e54b51`; files, hashes and licence texts in
[assets/fonts/README.md](../../assets/fonts/README.md). The generated fonts are OFL derivatives:
ship the two `OFL-*.txt` files with them. The release zip does: it carries `OFL-NotoSans.txt`
and `OFL-Exo2.txt` beside `f/`, and its README.txt names the licence. Neither reserved font name is used for the generated
files (`Tahoma`, `Zekton`, `ZektonES`, `Harrier` are the names the engine asks for).

## Regenerate

```sh
python3 tools/fonts/generate_fonts.py            # 16 files, d = 2 and 3, into assets/fonts/generated/F
python3 tools/fonts/generate_fonts.py --density 2 --font Tahoma \
    --preview /tmp/font-preview --report /tmp/fonts.json          # subset, PNG previews, ratios
python3 tools/fonts/extract_stock_codes.py [game_root]             # only if the stock set changes
PYTHONPATH=verification/probe:verification/analysis python3 -m unittest test_font_generator
```

Needs Pillow with FreeType (variable-font axes) and numpy. The output is deterministic for a
given Pillow/FreeType build (measured with Pillow 11.3.0, FreeType 2.13.3). The test regenerates
Tahoma and Zekton at d = 2 and checks format, band, ranges, code set, shadow and the ratios, and
regenerates the full default set to compare it with the committed files byte for byte (about 12 s,
measured). A Pillow/FreeType build that changes the bytes fails that comparison: regenerate, check
the stem coverage (`verification/results/font-rendering/stem_coverage.py`) and commit the new files
together.

## Readability under minification (experiment, 2026-09-30)

Run 393 (d = 2, `ui_scale` 1.25, bilinear 1.6× minification) showed every letter but read "a
little blurry". A 2-texel stem covers 1.25 screen pixels, so neighbouring stems alternate
between one heavy and one faint column. `tools/fonts/preview_minified.py` reproduces the game
path: the pen rule, the `0x004b2730` blit into a transparent text texture, an explicit 2×2
bilinear tap at each screen-pixel centre, and a SRCALPHA blend onto the panel colour and text colour
sampled from `screenshots/text4.png` ((37,37,37), (191,198,229)). It writes 4× nearest-zoom PNGs per
variant and scale. `--make-variants DIR` builds the standard set with the generator's experiment
switches, which default to the shipped behaviour (default output is byte-identical):
`--weight`, `--hinted` (FreeType hinted at the target size, no supersampling, image not
horizontally scaled, so it only suits Noto), `--floor-mode binary|grey|none` and `--gamma`.

Measured median white coverage at d = 2 (`verification/results/font-rendering/variant_coverage.py`):

| Tahoma26 variant | i l r | \| | - T | stroke rule | advance |
| --- | --- | --- | --- | --- | --- |
| A (wght 400, binary floor; shipped until D) | 2.00 | 2.00 | 2.00 | pass | 1.000 |
| B wght 600, binary floor | 3.00 | 2.00 | 2.00 | pass | 1.000 |
| C400 hinted, no floor | 1.91 | 1.66 | 1.79 / 1.75 | fail | 1.000 |
| C600 hinted wght 600, no floor | 2.66 | 2.17 | 2.41 / 2.38 | pass | 1.000 |
| **D wght 600, grey floor (shipped)** | 2.65–2.66 | 2.18 | 2.40 / 2.37 | pass | 1.000 |
| E gamma 1.45 (else A) | 2.00 | 2.00 | 2.00 | pass | 1.000 |

In the simulated 1.25 view, B keeps every stem at least one full bright screen pixel and has the
most even stem weight. D and C600 are close, with softer antialiased edges. E and A keep the
heavy/faint alternation, and C400 reads thin and grey [i: judged from the simulation, not the
game]. With the grey floor, D does not dilate at wght 600 because its thinnest stroke is already
≥ d, so D equals an unfloored wght 600. Hinting at 22.4 px changes the stem coverage by
≤ 0.01 texel (C600 against D).

**Decision (2026-09-30):** the HUD family ships variant D at both densities: Noto SemiBold, advance
re-fitted through wdth (80), grey floor, no gamma. After the 1.6× and 1.33× bilinear
minification its stems keep an even weight, where A's 2-texel stems alternate between a heavy and
a faint column, and its edges stay antialiased, unlike B's binarised stems. The LARGE family keeps
A (Exo 2 Medium, binary floor): its stems are already 4 texels and its bytes are unchanged. Measured
at d = 2 / 3: `i l r` 2.65–2.66 / 3.98–3.99, `|` 2.18 / 3.26, `-` 2.40 / 3.60, `T` 2.37 / 3.55
(`stem_coverage.py`, PASS). The switches remain for experiments. `--weight` and `--floor-mode`
override every family generated in one call, so run them per family.

## Visual match limits

Noto Sans is a humanist sans like Tahoma but with rounder bowls and a wider default
width. At SemiBold and wdth 80 its advances match Tahoma's, but letters are slightly wider,
word spaces narrower, and strokes about a third heavier than stock Tahoma13 ×d (a deliberate
choice for readability after minification). Stems are antialiased, not snapped to whole texels
[i: from the previews, not the game].
Exo 2 is a geometric techno face close to Zekton in spirit (squarish rounds, flat terminals),
but Zekton is effectively a pixel font with 2-px stems on whole texels and very compact
accents; Exo 2 is compressed to 87 % width to match Zekton's set width, its curves stay
antialiased, and accented capitals are flattened where they are squeezed into the band. The
shadow under the HUD font is a fitted approximation of the stock bake, not the same filter.
None of this has been judged in the game yet.
