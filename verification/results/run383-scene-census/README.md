# run383 — Run 108 A, scene_graph_census flight (Mayhem 3)

Launch (user, 2026-09-29): `env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 ./x3run --direct --perf --config`, Run108 DLL c57556bb… (commit 21726898).
Source log (local, untracked): /tmp/x3-bottleX3-run383/session-20260929-101037-212.log (8,539 frame_end rows, 29 census rows).
Reproduce: `python3 extract.py [LOG]` -> summary.json (census rows, 300-frame deltas, loop_phases, frame-time p50, conclusions).
Note: `inserts` and `i0..i7` are per 300-frame window, not cumulative.
Headline (measured): sector entered at frame 3000, left by 8400 (engine_nodes back to 196); +27.17 nodes/frame net;
insert pair 00486d7d/0041f332 = exactly 8,100 per window (27.0/frame, 93.8% of sector inserts); registry_live == engine_nodes on all rows;
model-1 = 96.0% of 92,170 sampled unattached nodes; cutevent_p50 55 us before entry -> 555 us at 3000 -> 10,836 us at 8100 (fit 76.5 ns/node);
frame p50 8 ms (125 fps) before -> 18 ms (55.6 fps) at 3000 -> 30 ms (33.3 fps) at 8100; back to 8 ms after leaving.
No object_lifetime_stats/disabled row; the only object_lifetime row is the start-up active=1 line.
