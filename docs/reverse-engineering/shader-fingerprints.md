# Shader fingerprints and runtime classification

`tools/analysis/index_shaders.py` reads local CAT/DAT effects and emits only metadata. It recognizes SM1–3 instruction streams; preserves version, comments and END; excludes effect-container padding; and computes the same FNV-1a 64-bit byte hash used by the proxy (`14695981039346656037`, prime `1099511628211`). SHA-256 is also included for research validation.

```sh
python3 tools/analysis/index_shaders.py '/path/to/X3' --output /tmp/x3-shader-index.json
python3 -m unittest discover -s verification/analysis -v
```

The expanded inspection yields **3,480 compiled effects, 13,407 shader occurrences,
and 751 distinct full-program FNV hashes**. The original index found only 523
programs and left 260 effects empty because it rejected version 2.1 tokens.
Those are extended Shader Model 2 programs: the native D3DX disassembler identifies
the added 200 pixel and 28 vertex programs as `ps_2_x` and `vs_2_x`. The corrected
parser retains their ordinary encoded instruction lengths. All 3,480 effects now
contain recognized streams, and all 751 programs have a successful disassembly.
Successful disassembly alone does not prove shader creation or runtime use.
Effects share shaders across profile directories and material variants; one hash
can have several candidate labels. See the full archive sweep for purpose and
coverage details.

Selected `addon/01.cat` `shader/3_0/` fingerprints: The z_only rows `4b63594a775cbde0` and
`d2e63b1e5b0e24df` are the `z_only_0000.fb` / `z_only_0001.fb` copies in base `01.cat`
(same code, older compiler string).

| Effect | Stage/model | Bytes | FNV-1a 64 |
|---|---|---:|---|
| gui2d | PS 1.1 | 196 | `0a523f33ac47ae05` |
| gui2d | VS 1.1 | 220 | `f36fc43f30b19d71` |
| gui2d | PS 1.1 | 304 | `6109cf64c03529dd` |
| gui2d | VS 1.1 | 512 | `7b6393fe2d3e1d85` |
| particles | PS 1.1 | 400 | `222bee0defcb1852` |
| particles | VS 1.1 | 512 | `36f98d151fd6b0c6` |
| z_only | VS 1.1 | 356 | `c78b4c68a87fce74` |
| z_only | PS 1.1 | 180 | `652a7c5d1e9909a0` |
| z_only | VS 1.1 | 380 | `803ebfd17f79e413` |
| z_only | VS 1.1 | 356 | `4b63594a775cbde0` |
| z_only | VS 1.1 | 380 | `d2e63b1e5b0e24df` |
| bloom | PS 3.0 | 1100 | `b40d09effa812ec8` |
| bloom | PS 3.0 | 7532 | `241c3fa33270f58e` |
| bloom | PS 3.0 | 7532 | `f3172baa8dd19a40` |
| bloom | PS 3.0 | 920 | `1c90e79667bdaddf` |
| bloom | PS 3.0 | 404 | `ff6eed5a5ddf3a3a` |

The current collection of 57 locally dumped runtime programs maps byte-exactly
to archive programs. That verifies those programs only, not every archived variant
or their draw purpose. A D3DX implementation may transform other streams when
creating shaders. Confirm byte counts and hashes before using a fingerprint to
replace or suppress a draw. HUD classification also requires checking geometry,
render state and ordering; a shader family alone is not a complete UI mask.

Seven synthetic verification cases cover known FNV vectors, comment payloads containing END tokens, SM1 immediate data containing END, truncated instruction rejection, adjacent streams and padding, stale catalogue header names, and invalid PE input. No test fixture contains game data.
