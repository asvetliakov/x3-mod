Run 203 A triage (2026-10-25). Log /mnt/c/X3/x3m.log, captures /mnt/c/X3/x3-modern-captures (not copied).
- pass1.py LOG OUT.json: one pass, frame_end dt_ms per frame, row kinds per frame, lod_strength_frame field-change counts.
- spikes.py OUT.json N: top-N dt spikes with non-routine rows within +-2 frames (spikes_out.txt).
- region_stats.py CAPTURE_DIR: freighter_base region luminance far vs close (region_stats_out.txt).
- Twin-frame phases: grep "frame_phases_slow" x3m.log | grep -E "frame=(7926|27489|35392|4752|9782) "
- no_twin_vs_fade.py LOG: lod_strength_frame no_twin frames against fade_route_frame / sun_shadow_lane_frame of the same frame (no_twin_vs_fade_out.txt).
- frame_row_volume.py LOG: the Run 203 A lod_strength_frame rows replayed through the follow-up row rule (change / onset / 10 s cadence; frame_row_volume_out.txt).
