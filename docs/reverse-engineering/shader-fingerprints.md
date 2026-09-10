# Shader fingerprints and runtime classification

`tools/analysis/index_shaders.py` reads local CAT/DAT effects and emits only metadata. It recognizes SM1–3 instruction streams; preserves version, comments and END; excludes effect-container padding; and computes the same FNV-1a 64-bit byte hash used by the proxy (`14695981039346656037`, prime `1099511628211`). SHA-256 is also included for research validation.

```sh
python3 tools/analysis/index_shaders.py '/path/to/X3' --output /tmp/x3-shader-index.json
python3 -m unittest discover -s verification/analysis -v
```

The inspected installation yielded 3,480 compiled effects, 11,431 parsed shader occurrences, and 523 distinct FNV hashes. 260 effects had no recognized streams; this may indicate different content or parser limitations, so the index is not asserted complete. Effects share shaders across profile directories and material variants. A hash can have several candidate labels.

Selected `addon/01.cat` `shader/3_0/` fingerprints:

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
| bloom | PS 3.0 | 1100 | `b40d09effa812ec8` |
| bloom | PS 3.0 | 7532 | `241c3fa33270f58e` |
| bloom | PS 3.0 | 7532 | `f3172baa8dd19a40` |
| bloom | PS 3.0 | 920 | `1c90e79667bdaddf` |
| bloom | PS 3.0 | 404 | `ff6eed5a5ddf3a3a` |

**Runtime verification pending:** these are archive fingerprints, not observed live `GetFunction` matches. A D3DX implementation may transform a stream when creating the shader. Confirm byte counts and hashes against at least one runtime capture before using a fingerprint to replace or suppress a draw. HUD classification also requires checking geometry, render state and ordering; a shader family alone is not a complete UI mask.

Seven synthetic verification cases cover known FNV vectors, comment payloads containing END tokens, SM1 immediate data containing END, truncated instruction rejection, adjacent streams and padding, stale catalogue header names, and invalid PE input. No test fixture contains game data.
