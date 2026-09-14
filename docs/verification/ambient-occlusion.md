# Ambient occlusion verification ledger

Owning design: `docs/architecture/ambient-occlusion.md`. One entry per run of
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_ambient_occlusion.py`
(record `verification/results/bottle-X3/ambient-occlusion-gpu1.json`).

| Date | Commit | Checks | Reference (max abs, five scenes) | Apply law | Chain 1280×768 / 1920×1080 | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-09-14 | step 1 (review fixes) | 99 / 0 failures (list, labels repeat for recovery and Reset reruns) | 0, 0, 3.7e-4, 3.7e-4, 3.7e-4 (plane, tilted, sphere, corner, step) | ≤ 1 FP16 ulp; plane bit-exact; FP16 store probe: truncate | 0.83 ms / 1.35 ms (submit 0.11); per-quad breakdown in the record | PASS; cost over the 0.8 ms cap, reported |
| 2026-09-14 | step 1b (cost reduction) | 109 / 0 failures | 0, 0, 2.4e-4, 2.4e-4, 4.9e-4 (plane, tilted, sphere, corner, step) | ≤ 1 FP16 ulp; plane bit-exact; FP16 store probe: truncate | 1.06 ms first block / 0.51 ms repeat block (floor 0.50) · 1.33 ms (floor 1.23) | PASS; warm 1280×768 chain within the 0.8 ms cap and at the 0.5 ms acceptance, cold block above it |
| 2026-09-14 | step 1b independent review (Opus, read-only rerun) | 109 / 0 failures | 0, 0, 2.4e-4, 2.4e-4, 4.9e-4 | ≤ 1 FP16 ulp; plane bit-exact; truncate | 0.78 ms first block / 0.53 ms repeat (floor 0.51) · 1.33 ms (floor 1.22) | PASS; math, c0..c7 layout, state/Reset and ledger numbers confirmed; no check removed vs step 1 (+8 per-quad counts, +2 sphere linearize); five low findings listed below, none blocking step 2 |
| 2026-09-14 | step 1b review fixes (five findings) | 112 / 0 failures (+3 over 109: the 397-slot boundary attach and its detach, the denormal-m32 refusal) | 0, 0, 2.4e-4, 2.4e-4, 4.9e-4 (plane, tilted, sphere, corner, step) | ≤ 1 FP16 ulp; plane and tilted bit-exact; truncate | 1.11 ms first block / 0.75 ms repeat (floor 0.74) · 1.33 ms (floor 1.23) | PASS; bytecode unchanged (blur program sha256 cb9fd497..., 490 words), host tests 4 OK |
| 2026-09-14 | step 2 (scene-end hook, launcher, timing; debug-view flag added to the pass) | 112 / 0 failures (detached rerun) | 0, 0, 2.4e-4, 2.4e-4, 4.9e-4 | ≤ 1 FP16 ulp; plane and tilted bit-exact; truncate | 0.78 ms first block / 0.59 ms repeat (floor 0.51) · 1.39 ms (floor 1.27) | PASS; live fixture `run_ambient_occlusion_live.py` PASS, 5 twins (ao-on 111 checks, 8/8 frames ran; ao-off 111; ao-fault 111, reason ps_3_0; ao-debug 58; ao-hdr 105, format 113), record `ambient-occlusion-live1.json`; crease law frames 1097/1148 px darkened, max drop 15/17, 1408 sentinel unchanged; timestamp queries return D3DERR_NOTAVAILABLE on CrossOver Preview's D3D9, so the frame line carries CPU wall time (median 292-652 us at 64x64; GPU cost bounded by the detached fixture's event-query fencing, in game by the F11 off/on A/B); `check_no_x87.py build/d3d9.dll` PASS (224 functions, 0 violations); host tests 34 OK; launcher dry run shows `X3M_AMBIENT_OCCLUSION=1` |
| 2026-09-14 | step 2 review fixes + Ctrl+Shift+F11 toggle | detached fixture not rerun (pass unchanged since the row above) | — | — | — | PASS; live fixture 7 twins: ao-on 112 checks, ao-off 112, ao-fault 112 (ps_3_0), ao-debug 58, ao-hdr 106, ao-toggle 125 (enabled 1,1,1,0,0,1,1; frames 3-4 byte-exact with the reference at reason=disabled, frame 5 crease 1115 px darkened, 2 toggle lines, 1 attach), ao-pollfault 112 (3 `queries=lost` notices, every line `gpu_timing=lost`, 8/8 ran); flat frame 1 `source=copy` in every non-debug twin; `check_no_x87.py` PASS (224, 0); host tests 41 OK; launcher dry run `X3M_AMBIENT_OCCLUSION=1` |

Host tests: `test_ambient_occlusion_caps.py`, `test_ambient_occlusion_reference.py`,
`test_ambient_occlusion_report.py` (4 tests; the reference test records the view-angle-horizon plane
measurement 0.9969 mean / 0.8510 minimum). `check_no_x87.py build/d3d9.dll`: PASS with the pass
linked (224 reachable functions, 0 violations); no other object references the pass symbols.

Step 1b notes: four quads (linearize, horizon search, one 2D quincunx blur, apply); largest program 397
of 512 slots (GTAO; linearize 8, blur 93, apply 70). Oracle means moved by at most 0.002 (contact ring
0.9569 → 0.9567, step far side 0.82 → 0.8215; crease near 0.9199, crease floor 0.9639) and the flat-plane
and sentinel identities are exact. Folding the linearization into the horizon search was measured and
rejected on cost (1280×768 chain 1.10–1.18 ms); the record of that variant is not retained, its numbers
are in `docs/architecture/ambient-occlusion.md`, "Step 1b". `check_no_x87.py build/d3d9.dll`: PASS
(224 reachable functions, 0 violations). Host tests: 4, OK.

Step 1b review findings (2026-09-14, none blocking; all five fixed in the "step 1b review fixes" row above):
`ambient_occlusion_fixture.cpp` includes `hdr_writeback_program_inc.h`, missing from the runner's SOURCES
provenance list; the apply-law comment at the reference half-depth feed is stale (GPU half depth only bounded
to 2e-3 relative); the "Bayer noise still averages out" claim in `ao_blur_ps.hlsl` is unverified (the quincunx
covers 7 of the 16 residue cells mod 4 — recounted as 7 while fixing — oracle means moved ≤ 0.002); `finite_params` accepts a denormal m32 that would
overflow the folded 1/|m32|; the twin slot probe fell to 300 against a 397-slot program. The review rerun
did not replace the committed record (restored after the run).

Step 2 host tests: `test_ambient_occlusion_live_report.py` (6 tests: the fixture and trace parsers, the
`ambient_occlusion_frame` grammar, the multiply/fault/off verdicts) and the launcher case
`test_ambient_occlusion_cli_dependencies_and_defaults` in `test_linear_material_live.py`; the
linear-material harness gained the AO lifetime stubs (`ao_`, `ao_timing_release`) so
`test_production_control_flow` still compiles the production `release_resources`/`before_reset`/`after_reset`.
Live record: `verification/results/bottle-X3/ambient-occlusion-live1.json` (bottle X3, arm64,
FEX_X87REDUCEDPRECISION=1, WINEMSYNC=1). The run-39/40 capture query and the view-unit check are recorded in
`docs/architecture/ambient-occlusion.md`, section 8.

Step 2 review (Opus, 7afff50) fixes: the timing-poll failure path re-checks the created state before
issuing (fixture twin ao-pollfault), `ao_adapter_format_` cleared at Reset, the chain gated on the
resolve's preconditions and run at the bloom-copy fallback too (`source=copy`, flat frame 1 of every
non-debug twin), sticky refusal after three consecutive chain failures and a 60-frame re-attach
hysteresis, overlong AO environment values rejected. Ctrl+Shift+F11 toggle added (twin ao-toggle).
The toggle twin's disabled frames are flat frames (the factor is the identity there anyway), so the disabled proof is carried by the `reason=disabled ran=0 applied=0` assertions on those frames' lines, not by the pixels. Second look (3 low): attach verdict cleared at Reset, the AO gate mirrors `ensure_taa()` and the strict-sentinel transform skip, dead field removed.
Informational, no code: the route fixture's state snapshot around the hook covers render targets,
depth, viewport, scissor, shaders, streams, the watched render states, samplers 0-7 and PS c0-7; vertex
samplers and texture-stage states rest on step 1's detached evidence (`D3DSBT_ALL` block, bindings by
pointer).

Re-attach hysteresis after Reset (2026-09-14). Candidate `740a6dd7` failed qualification on the
`ao-on` twin: `before_reset` cleared `ao_attach_failed_`/`ao_target_format_` but left
`ao_attach_count_`/`ao_attach_frame_` armed, so the first post-Reset frame was refused by the
60-frame hysteresis (`ambient_occlusion_frame attached=0 ran=0 reason=attach`, `AO_CREASE frame=3
darkened=0`). `before_reset` now also clears the count and frame; the hysteresis still holds within a
device lifetime for the format-alternation case. `run_ambient_occlusion_live.py` gained the missing
check: every post-Reset frame (`RESET_FRAMES`: default 3 and 6, debug 3, toggle 5 - frame 3 is the
disabled toggle frame) must report `attached=1 ran=1 applied=1 reason=ok`, and the device-line count
is now `1 + one attach per Reset` (ao-on/ao-hdr/ao-pollfault 3, ao-debug/ao-toggle 2) instead of a
flat 1. Host test `test_multiply_twin_needs_the_post_reset_frames_to_reattach` covers the parser
verdict. Evidence: `run_ambient_occlusion_live.py` PASS, 7 twins, post_reset_frames [3, 6] for ao-on;
the pre-fix build reproduces `AO_CREASE frame=3 darkened=0` (fixture exit 1); `check_no_x87.py
build/d3d9.dll` PASS (224 reachable functions, no violations); host tests
`test_ambient_occlusion_live_report`, `test_linear_material_live`, `test_motion_hdr_scene` 26 tests OK.
The linear-material harness stub gained the two fields so `test_production_control_flow` still
compiles the production `before_reset`.

## User run 19 (run47, 2026-09-14): the pass runs, the appearance is invisible

First gameplay run of AO step 2 (snapshot `/tmp/x3-bottleX3-run47/`, log
`session-20260914-225307-216.log`, installed DLL `ab6e17ba…` from `5d06316`,
`--ambient-occlusion --ao-timing`; the log was queried, never read whole).

The pass behaves exactly as the fixture predicts. `ambient_occlusion_toggle`:
21 lines, strictly alternating, so every Ctrl+Shift+F11 press was seen.
`ambient_occlusion_frame`: 24,199 lines, `attached=1 reason=ok` on 12,163 and
`attached=0 reason=disabled` on 12,036 — no `attach`, `format`, `sticky` or
other reason appears at all. `radius_px=25.60` whenever the pass is on; the
mode line reports `radius_m=2 strength=0.5`. `cpu_us` when on: mean 203.1 µs,
median 136 µs, max 26,002 µs (a loading outlier); 0 when off. No Reset occurred
in the session, so the post-Reset re-attach path is still fixture-only evidence.

The user sees no difference on or off, near a station and near an asteroid. The
two on/off capture pairs cannot settle it: 21950/22186 and 23946/24233 are 236
and 287 frames apart, so their pixel differences (26.6 % and 77.8 % of pixels
changing by more than 1 %) are camera motion, not AO. `frame_end` windows give
4,546 ms with AO on against 4,017 ms off (mean per ~300-frame window), confounded
by load and capture windows; not a cost verdict.

Conclusion: the run passes as a functional check and fails as an appearance
check — a 2 m occlusion radius is below the visible scale of X3 geometry. Run 20
repeats it with `--ao-radius` and `--ao-debug`.

## User run 20 (run48, 2026-09-15): the debug view was on, and the radius law

Second gameplay run of AO (snapshot `/tmp/x3-bottleX3-run48/`, log
`session-20260915-002408-212.log`, 335 MB, installed DLL `39b090d0…` from
`77a649b`, record `verification/results/run20-candidate-install.json`;
`--ambient-occlusion --ao-timing --ao-radius 20 --ao-debug`; log queried, never
read whole).

`ambient_occlusion_mode` reports `radius_m=20 strength=0.5 debug=1`: the debug
factor view was on for the *whole* session, not only for an optional second
short run. That is what the user saw and described — "mostly white, twin gray
lines when close" — so this run says nothing about the appearance of AO applied
to the image; it shows the factor buffer.

`ambient_occlusion_frame`: 50,236 lines, `attached=1` on 4,464 (8.9 %) in about
26 toggle bursts; `cpu_us` mean 211.5 µs while attached (run 19: 203.1 µs), and
GPU timing is unavailable in this build. All ten F8 capture groups landed in
`attached=0` gaps, so no capture in this run shows AO at all.

`radius_px=64.00` on every attached line. That is not a per-pixel value: it is a
reference-distance diagnostic evaluated at 100 units = 20 m and clamped by the
cap (`src/proxy/motion_output.cpp` near lines 1471 and 1621). The per-pixel law
is in `src/proxy/ambient_occlusion_pass.cpp:329–335`,
`radius_px = 256·radius_m/distance_m` at 1280×768 with a 64 px cap:

| `radius_m` | 500 m | 2 km | 5 km |
| ---: | ---: | ---: | ---: |
| 20 | 10.2 px | 2.6 px | 1.0 px |
| 50 | 25.6 px | 6.4 px | 2.6 px |
| 200 | 64 px (capped) | 25.6 px | 10.2 px |

At `radius_m=20` the cap binds only closer than about 80 m, so the run-20 radius
is effectively uncapped at gameplay distances and shrinks to about one pixel at
5 km; `strength=0.5` caps the darkening at factor 0.5 regardless. Choosing the
scale (fixed world radius, distance-dependent radius, or a screen-space floor)
is a design decision pending in `docs/architecture/ambient-occlusion-scale.md`;
a rerun without `--ao-debug` is what settles the appearance question.
