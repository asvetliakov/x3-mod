# Engine effects census (stock X3AP vs installed Mayhem 3)

2026-10-01. Read-only data census of ship engine glows and trails, bottle X3. No game launch, no Wine,
no build; the game tree is only read through `tools/analysis/sector_fog_census.Assets`.

```sh
python3 verification/results/engine-effects-census/engine_effects_census.py   # ~80 s, rewrites the outputs below
```

Views: `stock` = `STOCK_AP_CATALOGUES` (01..13 + addon/01..04, no loose files); `mayhem` = every installed
catalogue and loose file except the x3m LOD overlay slots addon/13..15 (`lod_overlay.original_assets`).

| file | content |
| --- | --- |
| `ships.csv` | one row per TShips row and view: class, race, columns 11/12/49, scene, main-glow / emitter / RCS part counts, glow bodies, glow tint (mean of the bodies' diffuse), glow length/width, nozzle z, hull length and LOD0 value, part `C` words, frame flags |
| `engine_bodies.csv` | one row per engine-part body and view: source, sha, format, LOD0 value, bbox and visible bbox (faces >= 0.1 % of the largest), blend, Z write, diffuse, resolved members, mean / alpha-weighted / p95 / peak colour of level 0, cluster |
| `class_body.csv` | class x main glow body x ships |
| `class_tier.csv` | class x Mayhem glow size tier (body name suffix) with the tier's LOD0 value |
| `summary.json` | counts, column validation, Effects / Particles3 / LensFlares chains, colour clusters by race, size ranges by class, Mayhem-vs-stock diff, trail search (query with `jq`) |
| `engine_bodies.{mayhem,stock}.json` | untracked (game-derived): `tools/effects/engine_bodies.py --out …` (`--stock-only` for stock), the per-body table of every loadable types/Bodies JET / SMALLJET body (rules in the tool's docstring) |
| `engine_bodies_counts.py` | counts of the two tables and their overlap with the scene-referenced bodies of `engine_bodies.csv` (2026-10-01: Mayhem 253 loaded / 16 missing, scene-referenced 80 `engine.fx` in 11 clusters + 2 `standard_lighting` incl. `v\00566`; stock 224 / 23, scene-referenced 140 = 110 cyan + 28 legacy grey + 2 near-black `standard_lighting`) |

Column choice (X3 Editor 2 TShips layout, 0-based): 11 engine effect -> `types/Effects`, 12 engine glow,
16 ship scene, 45 race, 46 hull strength, 49 engine trail -> `types/Particles3`, 52 class. Validated by
data: every col 11 value of Mayhem names an Effects row "Engine, Emitter - <colour>, <size>" except 5 / 390;
col 49 values 1..40 name Particles3 rows commented by race in four tiers; col 12 matches no table (open).
Engine parts = scene parts whose body is listed under `types/Bodies` `SBTYPE_JET` or lives under
`objects/effects/engines/`; role by the part's `C` word: bit 0 clear = RCS jet (direction masks
0x10/0x20/0x44/0x48/0x84/0x88, body `v/00566`), `fx_engine_emitter*` = emitter anchor, else main glow
(`C` 0x7001 in 2,919 of 2,923 Mayhem main parts). Body sizes: normalised positions x LOD0 value / 65536
(the text loader's scaling, body-text-loader.md; consumer of the value untraced, so world size is inferred).
