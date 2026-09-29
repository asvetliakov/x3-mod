# Generated bitmap fonts for sharp text under the UI scale

The UI-scale patch ([ui-scale.md](ui-scale.md)) renders text at integer density `d = ceil(s)`
through the engine's native `-fontscale` path, which asks for fonts `d` times larger than stock
([font-rendering.md](../reverse-engineering/font-rendering.md) §3 (a) item 3, §4). The stock
catalogues only hold the 1× fonts, so `tools/fonts/generate_fonts.py` builds the `d`× fonts from
bundled open-licence TrueType fonts. This note covers what is generated and how; the format is
specified in font-rendering.md §1 and not repeated here.

## Output

`python3 tools/fonts/generate_fonts.py` writes, for each density in `--density` (default 2 and 3),
eight files under `--out` (default `build/fonts`), in the folder layout the install step copies
into the game's `f/` folder unchanged:

```text
build/fonts/F/Tahoma{13d}.abc/.tga    HUD font, all languages     (Tahoma26, Tahoma39)
build/fonts/F/Zekton{26d}.abc/.tga    LARGE, default               (Zekton52, Zekton78)
build/fonts/F/ZektonES{26d}.abc/.tga  LARGE, -L034 Spanish         (ZektonES52, ZektonES78)
build/fonts/F/Harrier{24d}.abc/.tga   LARGE, -L007 Russian         (Harrier48, Harrier72)
```

Measured at d = 2 / 3: atlases 512×512 / 1024×1024 (Tahoma), 512×1024 / 1024×1024 (the LARGE
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
  squeezed glyphs at d = 2: Tahoma 142 (capitals with accents, and g j p q y by under half a pixel),
  Zekton/ZektonES 64, Harrier 22. The stock Tahoma13 cuts 71 glyphs at the band top instead.
- **Horizontal metrics.** Per glyph: `A` = left edge of the ink (snapped to the nearest texel,
  ≤ 0.5 px shift, so left stems land on whole texels), `B` = ink width including the stroke
  floor and the shadow,
  `C` = advance − `A`. The advance comes from the unhinted 1000-px outline. The mean a–z advance
  is fitted to `d ×` the stock mean on the rounded advances: Noto Sans through its width axis
  (wdth 87), then a residual horizontal scale; Exo 2 has no width axis, so its outline is
  scaled horizontally (0.864–0.866 for Zekton/ZektonES, 1.005–1.006 for Harrier). Glyphs are rendered at
  4×4 supersampling, binarised at half coverage, passed through the stroke floor below and
  box-filtered (17 coverage levels; the stock files have 15–16).
- **Stroke-width rule.** Every stroke, vertical and horizontal, has at least `d` texels of
  full coverage: a 1-px stock stroke is `d` texels at density `d`, and a thinner stroke vanishes
  where the text texture is minified with nearest sampling (`s = 1.25`, `d = 2`: screen samples
  every 1.6 texels; run 392, [font-rendering.md](../reverse-engineering/font-rendering.md) §5).
  `_stroke_floor` works on the supersampled mask. A horizontal ink run that continues a straight
  edge in the neighbouring row and is narrower than `d + 1` texels is redrawn as
  `max(d, round(width))` whole columns from the nearest column boundary (stem hinting). A
  diagonal or curved run narrower than `d` is widened to `d` in place. Vertical runs (bars) get
  the same treatment in whole rows, growing downwards, or upwards when they sit on the baseline
  or would leave the band, so cap line and baseline do not move. Strokes of `d + 1` texels or
  more are left as drawn, so Exo 2's 4-texel stems are unchanged. The check is
  `verification/results/font-rendering/stem_coverage.py`: the median white coverage over a
  glyph's inked rows (for `i l r ! | I t j і ї г`) or inked columns (for `- T`) must be ≥ `d` for all eight
  generated fonts; `test_font_generator` runs the same check. `A`, `C`, the band and the
  advances are unchanged by the rule.

  The run 392 fonts had `i l r` at 1.15 texels because of a sign error in the left-edge snap:
  the pen origin was moved by +2·shift instead of −shift, so up to one texel of ink left of
  column `A` fell outside the resample box and was cut. The source stems are 1.91 px (`l`) and
  1.97 px (`I`) at d = 2. The wdth 87 instance and hinting at the 4× render size were not the
  cause: the rendered stems at 89.6 px match the 1000-px outline to within 1 %.
- **Weight.** Stem width over cap height matches the stock fonts after the horizontal fit: Noto
  Sans Regular (0.122 vs Tahoma 1/8), Exo 2 Medium (0.145 × 0.866 ≈ 0.126 vs Zekton 2/16;
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
| Tahoma26 / 39 | 1.000 / 1.000 | 1.071 / 1.048 | 1.000 / 1.042 | 1.083 / 1.056 | 1.071 / 1.095 | 0.833 / 0.889 |
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
ship the two `OFL-*.txt` files with them. Neither reserved font name is used for the generated
files (`Tahoma`, `Zekton`, `ZektonES`, `Harrier` are the names the engine asks for).

## Regenerate

```sh
python3 tools/fonts/generate_fonts.py --out build/fonts            # 16 files, d = 2 and 3
python3 tools/fonts/generate_fonts.py --density 2 --font Tahoma \
    --preview /tmp/font-preview --report /tmp/fonts.json          # subset, PNG previews, ratios
python3 tools/fonts/extract_stock_codes.py [game_root]             # only if the stock set changes
PYTHONPATH=verification/probe:verification/analysis python3 -m unittest test_font_generator
```

Needs Pillow with FreeType (variable-font axes) and numpy. The output is deterministic for a
given Pillow/FreeType build (measured with Pillow 11.3.0, FreeType 2.13.3). The test regenerates
Tahoma and Zekton at d = 2 and checks format, band, ranges, code set, shadow and the ratios.

## Visual match limits

Noto Sans is a humanist sans like Tahoma but with rounder bowls and a wider default
width; at wdth 87 its advances match Tahoma's, but letters are slightly
wider and word spaces narrower. The stroke floor snaps thin straight stems and bars to
exactly `d` whole texels, like stock Tahoma13's 1-px stems ×2. Curves and diagonals stay
antialiased, and small marks can come out a texel wider than the stem (the `i` dot is 3×2 at
d = 2) [i: from the previews, not the game].
Exo 2 is a geometric techno face close to Zekton in spirit (squarish rounds, flat terminals),
but Zekton is effectively a pixel font with 2-px stems on whole texels and very compact
accents; Exo 2 is compressed to 87 % width to match Zekton's set width, its curves stay
antialiased, and accented capitals are flattened where they are squeezed into the band. The
shadow under the HUD font is a fitted approximation of the stock bake, not the same filter.
None of this has been judged in the game yet.
