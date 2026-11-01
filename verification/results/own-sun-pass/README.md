# Own sun pass: offline fact check (2026-11-02)

Derived counts only; decoded game tables stay in the session scratchpad. Scripts here reproduce every number
in `docs/architecture/own-sun-pass.md`, "Fact check". `S` is any scratch directory.

```sh
python3 verification/results/own-sun-pass/extract_sun_tables.py /mnt/c/X3AP-orig ap $S/ap
python3 verification/results/own-sun-pass/extract_sun_tables.py /mnt/c/X3 ap $S/mayhem
python3 verification/results/own-sun-pass/extract_sun_tables.py /mnt/c/X3FL fl $S/fl
python3 -I verification/results/own-sun-pass/universe_suns.py $S/ap/addon_maps_x3_universe.pck $S/ap/addon_types_TSuns.pck
python3 -I verification/results/own-sun-pass/sun_colors.py $S/ap/addon_maps_x3_universe.pck $S/ap/addon_types_TSuns.pck
python3 -I verification/results/own-sun-pass/body_materials_lines.py /mnt/c/X3AP-orig ap 719 720 721 722 31 11000 752 766
python3 -I verification/results/own-sun-pass/body_materials_lines.py /mnt/c/X3 ap 996 650 997
```

Tables (mount order per `tools/analysis/catalogue_mounts.py`; the winning catalogue in brackets):

| Edition | TSuns rows | Lensflares groups | Map | Sectors | Sun objects (type 3) | Lens groups referenced | Distinct `color` | Distinct (group, scale) sets per sector |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| AP stock (`/mnt/c/X3AP-orig`) | 11 (addon/01) | 35 (addon/01) | addon/02 | 239 | 250 | 8 (groups 0-7) | 132 | 17 |
| FL (`/mnt/c/X3FL`, addon2) | 11 (addon2/04, same SHA as AP) | 35 (same SHA) | addon2/04 | 298 | 312 | 8 (groups 0-7) | 112 | 14 |
| Mayhem (`/mnt/c/X3`) | 219 (addon/11) | 182 (addon/11) | addon/07 | 256 | 263 | 91 | 236 | 101 |

Suns per sector: AP {0: 2, 1: 227, 2: 8, 3: 1, 4: 1}; FL {0: 3, 1: 284, 2: 7, 3: 2, 4: 2}; Mayhem {0: 1, 1: 251, 2: 1, 3: 2, 4: 1}.
TSuns field 9 histogram: AP {65536: 51, 98304: 199}; FL {65536: 82, 98304: 230}; Mayhem {3275: 3 ... 163840: 5}.

Bodies: 719-722 (the core glow of every stock group) carry two MATERIAL3 lines each, untextured `0;0` and `1;360`
(tex/true/360.jpg, 1024x1024), identical across the four; 752 -> 362, 766 -> 363 (1024x1024); body 31 `/Sonne`
-> legacy MATERIAL texture 55 (128x128); 11000/11004/11008 MATERIAL6 `effects.fx`; Mayhem 996/997/650 (XRM suns,
groups 173-181, one row each `1.00; 200; 250; 0`) MATERIAL6 `standard_lighting.fx`.
