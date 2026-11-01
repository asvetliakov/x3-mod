# Run216 sun occlusion triage (AP native 5120x1440, 1.8.2 DLL 4ccbc880, log /mnt/c/X3/x3m.log)

Scripts (numpy/stdlib only, no capture bytes kept):
- `sun_chain_frames.py LOG`: frames with the sun record (group 174, main view) vs frames per lens pair.
  Result: group 174 in 4113 frames; `494fe349/7c83ed50` drawn in 4113 frames, 4112 of them sun frames
  (one draw per frame); `d5e1c753/8360f422` in 638 frames, 592 of them sun frames, the rest of its frames
  coincide with groups 36 (273/273), 37 (409/409), 4 (287/301).
- `match_lens_to_records.py LOG`: each rows_known=1 lens centre vs every main-view record's
  u = 0.5 + x/65536, v = 0.5 - y/65536. 4624 rows: 4620 are the core of group 37 (2088), 36 (1376) or 4 (1156),
  median distance 2e-6 u; 4 unmatched; 0 belong to group 174. The 20 `body=ghost` rows are group-37 cores
  (frames 2042-2046, verdict applied).
- `parse_lens_rows.py LOG OUT.json`: sun_visibility / sun_lens_draw rows to JSON (scratch input).
- `find_sun_blob.py`, `local_centroid.py`: bright-pixel centroids in bgra8 captures.
  lens_1_6229 saturated core u=0.5866 v=0.4576; lens_1_6236 u=0.5880 v=0.4452; record 6229 u=0.58533 v=0.43983,
  6236 u=0.58524 v=0.43985; lane - record over 4113 rows: du median +0.00083 (p95 |0.0022|), dv median -0.00039 (p95 |0.0013|).
- `lens_pair_inventory.py` (offline, `fl-shader-coverage/inventory.py`'s `effective_programs`; output
  `lens_pair_inventory.json`): `vs_494fe349b8bc12ec` and `ps_7c83ed50c9894e44` are both in `/mnt/c/X3AP-orig` and
  `/mnt/c/X3` (`addon/01.cat`; the ps only in the four `shader/3_0/**/standard_lighting.fb`); both installs resolve to
  2352 effects / 751 programs (Mayhem's `addon/05..15` add no program); `/mnt/c/X3FL` has the vs, not the ps.
- `fl_object_compare.py [--count SYMBOL] BASE_TREE NEW_TREE BUILD_FL OBJ...` (the reviewer's script, generalised):
  main `bf6238ee` vs the review-fix commit, FL objects motion_output, sun_occlusion, sun_occlusion_pass, loader,
  capture: 5 of 5 identical (stripped). `--count decide_probe` with `build-ap`: 605 -> 613 instructions.
- `oscillation.py TREE...`: review F1's scenario on each tree's host driver. First fix
  `answers=01101010101 blocks=5 logs=5 releases=4`; review fix `answers=01100000000 blocks=1 logs=1 releases=0`.
