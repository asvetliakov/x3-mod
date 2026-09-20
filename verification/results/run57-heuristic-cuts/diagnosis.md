# Run57 normal-speed A/B: five reported flash witnesses

## Outcome

The five inspected native MOV flash peaks all match game frames with global cut=1 and taa_history=0. Four are median-motion cuts; one is a missing-key-fraction cut. The B setting successfully disables missing-fraction cuts, but leaves the 48px median cut active. The two reported B flashes therefore do not establish a flash with retained TAA history. This rejects the narrower explanation that the missing branch alone causes all reported flashes, and strengthens the explanation that either heuristic can expose bright current-frame detail when it discards history. Correlation with the reset is strong; a MOV cannot prove that upstream lightmap sampling itself is stable or assign the source texture/shader.

## Proven settings and identity

A is run203/session-20260921-025852-212.log and screenshots/lightmap2.mov (1465 frames, 26.196667s). B is run204/session-20260921-030355-220.log and screenshots/lightmap3.mov (2145 frames, 38.311667s). Both movies are 1280x768. Camera-pose sequences establish these identities independently of filenames/metadata.

Headers show A missing=.25, B missing=1; both median48px, camera20deg, MOTION_FRAME_LOG=1, FRAME_END_STRIDE=1, CAMERA_LOG=1, telemetry1/draw0, volumetric fog0, gain4, fade80/220/floor1, TAAdebug0/jitter8/historyweight.9, mipbias-.5, sharpen.75. Both use DLL a51d1e75fa80d7d07bab7ab66004291f7248bc5693e6585171e564a90fa96e56 / source85da89a8.

## Exact witnesses

MOV frame indices are zero-based. HUD healthbar right edge minus73px gives the projected target centre X even when the left side is clipped. Centre Y is barY+75; intrinsics use cx640,cy384,f512. Camera rotation transforms those rays into an approximately constant world direction. Fit native MOV PTS, not a resampled video's nominal timestamps. A native-event fit: offset224.021s,94samples,RMS.686deg; B:239.262s,72samples,RMS.578deg. Offsets map video start into each session's frame_end elapsed clock and are not wall-clock differences. Independently rank candidate game poses ±4 frames at each peak; pose matching does not use brightness or cut flags.

| Run | MOV time / frame | Game frame | Pose error / next candidate | cut median px | missing fraction | history / camera cut |
|---|---|---|---|---|---|---|
| A | 12.391667 / 693 |17503|.165deg /1.689deg|58.4597|0|0 /0|
| A | 14.641667 / 819 |17669|.179deg /1.242deg|56.6742|0|0 /0|
| A | 17.025000 / 953 |17838|.121deg /2.524deg|26.9889|.3000|0 /0|
| B | 19.450000 /1087 |17812|.221deg /1.331deg|49.3434|0|0 /0|
| B | 23.966667 /1341 |18070|.147deg /1.344deg|53.7962|0|0 /0|

All five have apply_failures=restore_failures=0 and camera_valid1. Camera rotations are 2.3187,1.7012,2.6023,1.8353,1.6821deg, below the genuine20deg camera-cut threshold. B also has a separate genuine camera cut at frame18009 (mapped~23.193s), preceding the selected24s flash by~.77s; do not conflate it with frame18070.

Mapped recording intervals contain A1805 game frames/27global cuts (23median,4missing), B2285/22global cuts (all median), with one B camera cut overlapping a globalcut. B has12 rows with missing>.25 and median<=48, each cut0/history1, proving the flag operated. Apply/restore failure totals are0 in both recording intervals. `movie_cuts.json` enumerates every cut/history-invalid row with approximate video time.

Exposure and minimum admitted fade gain are smooth at each peak. B17811→17812→17813 minimum gain2.81565→2.80086→2.79155, adaptedEV1.300→1.300→1.29642. B18069→18070→18071 gain2.73959→2.72582→2.71646, EV1.21446→1.21366→1.21253. These aggregate measurements do not identify the target's own per-draw gain, but show no global exposure or reported minimum-gain jump.

## Reproduction and artifacts

- `parse.py` streams both logs; `run203.json` and `run204.json` retain selected per-frame fields.
- `A_pts.csv` / `B_pts.csv`: ffprobe original best_effort_timestamp_time. Native image extraction used ffmpeg select between event timestamps, without fps resampling; A_selected_info.log/A_selected_indices.json preserve two floating-endpoint exclusions.
- `native_match.py` / `map_events.py`: native PTS camera fit and brightness-independent per-peak game-frame ranking; output `mapped_events.json`.
- `peak_decay.png`: five rows, each peak plus the next3 native MOV frames, nearest-neighbour2x crops, preserving source pixels. Individual A_693/A_819/A_953/B_1087/B_1341_peak_decay.png files are easier to inspect. Older *_strip.png includes unreliable pre-peak tracking at a clipped HUDbar and should not be used as an aligned measurement.
- Re-run: python3 /tmp/x3-run57-flash/native_match.py; python3 /tmp/x3-run57-flash/map_events.py; python3 /tmp/x3-run57-flash/witnesses.py. Witness assertions verify all5 mapped peaks cut1/history0; JSON parses, native frame counts match PTS selection.

## Next bounded discriminator

Keep production policy unchanged. An existing-build diagnostic can set missing1 and median above the measured normal-pan maxima while preserving the genuine20deg camera cut; normal-speed movie+frame logs must prove the same target flashes with history retained or stops flashing during equivalent pans. A retained-history flash would shift investigation to source sampling/per-pixel rejection. This is a diagnostic proposal, not authorization to weaken sector-change history invalidation in production. The source contract still globally invalidates in temporal_pass.cpp on motion_output.cpp's median/missing heuristic, with per-pixel rejection separately handling missing draw keys. Preserve accepted80/220/floor1.
