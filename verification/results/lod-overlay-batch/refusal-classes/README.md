# Refusal classes (2026-09-24)

Scripts behind the "Refusal classes" bullet of `docs/architecture/merged-lod-feasibility.md`.
They read the bottle's catalogues and the installed `addon/x3m-lod-batch.json` without writing to them.
The `review_*.py` scripts take the checkout root as their first argument.

- `refusal_classes.py`: the three classes in the installed record (`fleet`); the per-effect light-map declaration (`effects`); NULL-diffuse materials (`nodiff`); unresolved names and their catalogue members (`textures`); the fleet parameter survey (`params`); per-body table of a batch record (`table`); overlay member comparison (`compare`). Its output is in `refusal_classes_out.txt`.
- `review_cls.py`: effect classes of every dominant_slot_missing body, and the full parameter list of each distinct glass / asteroid / planet_haze / adeffects / effects material.
- `review_cls2.py`: the same materials reduced to textures, non-zero TexAnim values and `g_*` render-state parameters.
- `review_bump.py`: source member, format, size and mean RGBA of the asteroid, planet-haze, Stockmarket ad and glass textures.
- `review_bump2.py`: channel layout of asteroids_01_bump against argon_m3_bump (swizzle correlations, normal lengths).
- `review_nd.py`: split_TL m14, teladi_M6 m27 and teladi_trading_station_partA m26: effect, blend state, texture slots and record-0 face counts (76, 40, 48).
- `review_uv.py`: UV ranges of StockmarketBoardS/XL m7, lostcolony_energy m33 and argon_M3 m1 in record 0.
