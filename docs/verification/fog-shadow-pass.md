
## Run 276 (Run 74 B, 2026-09-23): fog shadow pass GPU cost at 1920×1080

`--fog-shadow-pass --gpu-sync-timing`, Ctrl+Shift+F11 on → off (frame 751) → on (1324) → off (1948), 300-frame
windows (`verification/results/run276-fog-shadow-gpu/windows.txt`, toggles in `toggles.txt`). Clean windows,
fog_route serialised GPU median (measured): on W6 4.79 ms; off W4 4.53, W8 4.47, W9 4.42 ms. The pass costs about
0.3 ms GPU (~7 % of the route, inferred from the window difference); taa, engine and dt are unchanged (dt 23.3 vs
23.4 ms). Run 275 (the same flight with the pass disabled at launch, toggle inert by design) reproduces the Run 274
route figures (`run275_windows_pass_off.txt`). Decision stands: the pass stays off (no visible difference in Run 68 C);
it is cheap enough to enable per sector if a look case appears.

**Removed 2026-09-25** (user decision): `--fog-shadow-pass` (the visibility-grid pass, its programs and the Ctrl+Shift+F11 toggle); `docs/verification/launcher-options-inventory.md`, "4. Removed 2026-09-25".
