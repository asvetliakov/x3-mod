# Run 407 / 408 engine plumes: who removes the distant jets

Inputs (untracked, local): `/tmp/x3-bottleX3-run407/session-20261003-163637-212.log` (Run 122 A, floor 1.0),
`/tmp/x3-bottleX3-run408/session-20261003-164314-212.log` (ini `engine_plume_floor = 0.5`), DLL 4d5cbb9c…, bottle X3.

Reproduce: `python3 triage.py <session.log> > run40N.txt` (streams the log once, prints compact tables only).

Result (measured): every far engine jet in the census is removed by the proxy's small-parts cull
(`cull_census verdict=culled_small`, projected s = 1..2 below threshold 3 at px=4, scope=all), before the
engine submits the draw. run408: 15 / 12-13 jet nodes culled per F8 frame (models 20400/20401/20402/20403,
d 1.34M..2.79M), 12 kept (d <= 1.33M, s >= 3); engine_frame records = kept jet count on every F8 frame.
run407: 0 jets culled_small (only 2 ships carry jets in the census); the one far ship hull in the census
(split_tp_nomad, d ~19.4M) is itself culled_small. Plume-stage culls (culled_small/behind/idle) are 0;
floor_unknown 0. engine_draw rows carry no `lod` field.
