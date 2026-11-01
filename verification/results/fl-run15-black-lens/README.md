# FL run 15, "black lens bodies" triage (capture frame 12637)

Source: `/mnt/c/X3FL/x3m.log` (FL run 15, DLL c48809c8...), captures `/mnt/c/X3FL/x3-modern-captures/*_1_12637.*`
(newest of the capture burst 12630..12637). Slice the frame first:
`awk 'NR>=1685220 && NR<=1888163' /mnt/c/X3FL/x3m.log > f.log` (frame 12637 rows; line numbers of this log).

- `draw_list.py f.log 536` : per-draw vs/ps, blend (rs 27/19/20/171/193), z state, stage-0 texture. State rows follow their draw row.
- `lens_pos.py f.log 572 582` : projects the object origin through VS c0..c3 -> screen centre of each lens-scene draw.
- `lens_pos.py f.log 1 571` + filter x 700..1300, y 750..1100 : draws under the dark disc object (20..48, opaque, zwrite on).
- `lens_px.py <capdir> 12637` : present RGB / HDR luminance at lens centres and the disc object vs a ring 150..300 px away.
- `probe_px.py <capdir> 12637` : point samples on the disc object vs the surrounding fog (present/color/hdr/depth).
- `thumb.py <file.bgra8> out.png [x y w h scale]` : PNG crops (local only, not tracked).

Log rows: `grep -a -n -E "^(fl\.sun|lens_flare_gain|sun_occlusion_)" x3m.log`.
