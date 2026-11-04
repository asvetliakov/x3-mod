Sector background textures: measurements and retained evidence (docs/architecture/sector-background-modern.md).

nebula_texture_sizes.* and sky_texture_stats.*: stock texture census (AP and FL) behind the design note.

The nebula part re-authoring recipe (deband, 2x Catmull-Rom, dithered DXT1 re-encode; Decision B) was removed on
2026-11-04 after the on-screen check: on the bluewell part 01 preview at 4x crop the stock part magnified bilinearly
by the GPU showed 9.8 mean colours per 8x8 block against 4.9 for the bake, plateau share 3.4 % against 9.3 %; the
ordered dither shows as a dot pattern once each 4096 texel covers about 1.75 screen pixels. The offline gate had
measured the box-downsampled bake and did not predict that. Its scripts (sky_bake_metrics.py, sky_contact_sheet.py,
sky_metrics_noop_check.py, sky_encoder_bound.py) imported the removed code and were deleted; their outputs below are
the retained evidence:

- sky_bake_metrics_fl.txt: offline metrics of the final bake (bluewell, avdustysol), 10 of 10 under the gate.
- sky_encoder_bound_fl.txt: PCA encoder PSNR and refinement cost; the RGB565 dither bound on run and hue.
- sky_metrics_noop_check_fl.txt: untouched and nearest-neighbour bakes against the gate.

What remains of the sky group is the star-texture mark (tools/analysis/sky_textures.py, payload 2).
