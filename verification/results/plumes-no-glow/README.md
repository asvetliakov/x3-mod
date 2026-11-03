# Plumes for hulls without glow parts: census (2026-11-03)

Owning note: `docs/architecture/plumes-no-glow-hulls.md`. Read-only over `C:\X3` (AP installed = Mayhem 3, overlay
slots `addon/13..15` excluded; plus the `STOCK_AP_CATALOGUES` view) and `C:\X3FL` (installed, `addon2/06..07`
excluded); no launch, no Wine.

```sh
python3 verification/results/plumes-no-glow/census.py   # 2 min 13 s, rewrites census_out.json / census_out.txt
```

Result (measured): ships without a main glow part: AP installed 6 (3 OWP at speed 0, 2 starbase "M1", 1 TS Nexus
miner), AP stock 104, FL 105 (every M1 and TL, 13/16 M2, 27/31 resp. 15/15 M7, 18/28 resp. 21/33 M6, FL's 8/8
missile frigates). 13 / 11 of them carry `fx_engine_emitter` dummies (the M6M family, stock Terran drones); the rest
have no JET part at all. `calibration` in the JSON: per class, the real main jets' rear z fraction, lateral fraction
and value / hull half-width, from the ships that do have glow parts (the synthetic nozzle law's inputs).
