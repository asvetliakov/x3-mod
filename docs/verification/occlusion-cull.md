# Occlusion cull: verification ledger

Feature: [occlusion-cull.md](../architecture/occlusion-cull.md). Bottle X3 (CrossOver Preview, arm64 Wine with FEX,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`); every Wine command ran alone under `verification/probe/wine_lock.py`.

## 2026-10-08: step 0, the per-query cost gate

Command (backend smoke fixture, new `--occlusion-cost` mode; wined3d with `--d3d9 builtin --wine-env
CX_GRAPHICS_BACKEND=wined3d`, DXVK with CrossOver's bundled `lib/dxvk/i386-windows/d3d9.dll --d3d9-order b
--wine-env CX_GRAPHICS_BACKEND=dxvk`):

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_d3d9_backend_smoke.py \
  --d3d9 <builtin|dxvk d3d9.dll> --name occlusion-cost-<backend> --no-sweep --occlusion-cost ...
python3 verification/results/occlusion-cull/gate.py   # -> gate.json
```

Records: `verification/results/bottle-X3/d3d9-backend-smoke/occlusion-cost-{wined3d,dxvk}.{json,txt}`,
`verification/results/occlusion-cull/gate.{py,json}`. All figures measured.

| per test (DXVK) | CPU (issue + GetData) | pipeline (event query, with vs without) | lag-1 ready |
| --- | --- | --- | --- |
| production shape (rectangle in c252/c253, ZERO/ONE blend) | 3.06 us p50, 3.37 max | 5.2 us p50, 5.44 max | 100 % at 13 and 20 ms pacing, N 128-512 |
| same with COLORWRITEENABLE 0 | 2.2-2.6 us | 55-79 us | 23-100 %, collapsing with N |
| 12-triangle box with a per-test dynamic-buffer Lock | 2.3-2.6 us | not measured | as above |

| per frame (DXVK, production shape) | CPU | pipeline |
| --- | --- | --- |
| N = 128 | 0.39 ms | 0.67 ms |
| N = 512 | 1.56 ms | 2.73 ms |

wined3d: production shape 1.8 us CPU, 20-23 us pipeline per test, 100 % lag-1 ready; the Lock variant 50-80 us CPU per
test. Unpaced (no frame pacing) neither backend has lag-1 results on DXVK: the fixture's hidden window presents at the
display rate, the GPU queue backs up; at 20 ms pacing every result was ready one frame later.

Gate: production CPU per test 3.37 us max < 5 us on DXVK: **passed**. Policy (user 2026-10-08): no budget setting;
every candidate tested every frame, limited by the fixed pool (512 per frame). At the close views (130-190 candidates,
67-91 hidden) the CPU cost is 0.44-0.64 ms against a 1.74-2.37 ms saving (inferred from the estimate's 26 us per draw).

## 2026-10-08: fixture, host tests, build

D3D fixture `verification/probe/occlusion_cull_fixture.cpp` (the production pass on a real device; runner
`run_occlusion_cull.py`, CMake target `occlusion_cull_fixture`):

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_occlusion_cull.py --backend wined3d --exe <build>/occlusion_cull_fixture.exe
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_occlusion_cull.py --backend dxvk --exe <build>/occlusion_cull_fixture.exe
```

Records `verification/results/occlusion-cull/fixture-{wined3d,dxvk}.{json,txt}`. Both backends 24/24 checks, for each
of A16B16G16R16F and A8R8G8B8 (measured):

- three hidden parts skipped in all 36 frame-part slots from frame 1 on (the frame after the Reset excluded);
- visible, in-front and alpha-tested parts never skipped (0 of 14 frames);
- the teleported part drawn the frame it left the hull (stability guard);
- the part revealed by the hull moving away skipped at frame K and drawn from K + 1 (`drawn_late` 1);
- the frames with the cull on and off byte-identical except frame K, whose 1,444 differing pixels all lie inside the
  revealed part;
- the application's shaders, declaration, stream, render states and c252/c253 back after every test;
- after the Reset the pool is 1,024 again and the first frame skips nothing; the device's reference count is the same
  after detach as before attach (1,028 references while attached);
- six back-to-back frames without readback: 45 of 54 reads not ready, 0 skipped.

The first fixture run caught a wrong winding choice (the CW and CCW frames culled the test rectangle, so visible parts
read hidden); fixed before the records above.

Host: `test_occlusion_cull` (core driver 49 checks: classification, rectangle, guard, ring, skip rule, classifier over
a synthetic image; draw-site order; lifetime under the accounting; programs; schema; gate record), `test_config_schema`,
`test_motion_wrap_states`, `test_exe_identity`, `test_motion_hdr_scene` OK. Full suite: 290 modules, 3,055 tests; the
only failure was `test_engine_effects.test_bound_to_its_production_sources` (its record binds `motion_output.h` and
`capture.cpp` hashes), resolved by the rerun below.

Build: fresh CMake configure, `d3d9` target 0 warnings; `check_no_x87.py` PASS (782 reachable functions, 0
violations). `tools/config/generate.py --check` PASS (258 settings, 108 in the template).

Not verified: a flight (in-game candidates, readiness and frame time); native Windows execution.

## 2026-10-08: review fixes (route's lazy RT1/RT2, bound formats, ready age, bounds source, stale extents, fill mode, FVF, hull owners)

Blocker: in the lazy RT mode the route keeps RT1 (A32B32G32R32F) and RT2 (R32F) bound between routed draws and the test's
ps wrote oC0 only. Protections measured with RT1/RT2 bound (smoke fixture `occlusion_mrt_cost`, 60 frames after 4
warm-ups, N = 128; records `occlusion-cost-{wined3d,dxvk}.json`, `gate.json` "mrt"; measured):

| protection | DXVK CPU / pipeline per test | wined3d CPU / pipeline per test | RT1 / RT2 texels changed |
| --- | --- | --- | --- |
| none (oC0 only) | 2.85 / 1.37 us | 1.52 / 64.4 us | 0 / 0 here, but undefined by D3D9 |
| flush and rebind RT1/RT2 | 3.56 / 67.5 us | 1.84 / 57.9 us | 0 / 0 |
| COLORWRITEENABLE1/2 = 0 | 2.84 / 63.9 us | 1.61 / 66.5 us | 0 / 0 |
| every output written 0 + ZERO/ONE blend (chosen) | 2.86 / 1.31 us | 1.54 / 17.1 us | 0 / 0 (-0.0 lane kept) |

Both backends report MRT post-pixel-shader blending and blendable A32B32G32R32F and R32F. The pass now writes 0 to every
output the device has, reads the bound targets from the device per test (each must blend; several need MRT blending),
which also fixes the format check under the HDR redirect. Other items: the cull uses the most recent ready result up to
two frames old (`ready_age=` per frame), refuses extents of an earlier buffer revision and non-solid fill, logs
`reason=no_bounds_source` and stays off without the candidate counter, restores the declaration before `SetFVF`, and
registers a hull's parent as owner only when it is a top-level ship root (`ancestry.{py,txt}`: hull ancestry 2 in every
run14/run15 hull draw with a known ancestry, measured).

Rerun (measured): the DXVK gate rows moved within run-to-run spread (production CPU p50 3.21 us, max 4.72 us < 5; the
first run 3.06 / 3.37); `test_occlusion_cull` (core driver 56 checks) OK; fixture 26/26 on wined3d and DXVK: the
fp16_mrt shape keeps RT1/RT2 byte-identical on and off except frame K inside the revealed part (1,444 texels each), all
earlier assertions unchanged; back-to-back frames on DXVK: 45 of 54 one-frame reads not ready, 27 decisions on age-2
results, 15 skips, every one on a ready hidden result (wined3d: 0 age-2 results ready). `generate.py --check` PASS; fresh
`d3d9` build 0 warnings; `check_no_x87.py` PASS (784 functions, 0 violations); `run_engine_effects.py --wine-env
CX_GRAPHICS_BACKEND=wined3d` 8 modes, 297 checks, 0 failed (its record rebinds to this tree).

## 2026-10-08: batched tests per ship, staggered re-test (after Run 137 A)

Run 137 A (DXVK, `run137-dxvk-triage/`, measured): the cull acted correctly but cost 10-25 us per test in flight (two
pipeline rebinds per part) against ~10 us saved per skipped draw; `view_submit` 6.4 -> 8.1 ms at the close capital
views. Change: one block per ship per frame at its first part draw (after its hull), last frame's parts of the ship
reprojected through the ship's hull rows, one state swap, a query per rectangle, one restore; a part last read visible
is re-tested once every `occlusion_cull_retest` frames (default 8) on a fixed staggered phase; hidden, unknown, moved,
hull-changed and post-Reset parts at every block. Rectangle supply (coordinator decision): `constants`, the rectangle in
`c252-c253` per test over a static strip, no Lock on the game thread; the buffer modes are fixture-measured
alternatives. Design: [occlusion-cull.md](../architecture/occlusion-cull.md) ("Mechanism", "Cost").

Production default, realistic state (3 ships x 50 parts, fp16 + RT1/RT2, game vs/ps bound; medians of five interleaved
repeats; measured; tracked records `verification/results/occlusion-cull-batched/fixture-*.json` and `legacy-*.json`):

| backend | per test | per block | 150 tests: test time / frame CPU | K = 8 cull: tests / test time / frame CPU |
| --- | --- | --- | --- | --- |
| DXVK, constants | 1.21 us | 7.4 us | 204 / 317 us | 116 / 174 / 199 us |
| wined3d, constants | 0.73 us | 8.9 us | 136 / 208 us | 116 / 124 / 134 us |
| DXVK, Run137 per part | 6.20 us | - | 930 / 1,063 us | 150 / 931 / 908 us |
| wined3d, Run137 per part | 2.12 us | - | 318 / 326 us | 150 / 295 / 230 us |

The buffer alternatives and the wined3d Lock stall are tabled in the design note ("Measured alternatives"). Before the
per-frame target-check cache (review F6) the constants mode measured 8.3 (DXVK) and 10.4 us (wined3d) per block in a
superseded record (not kept); after it 7.4 and 8.9 us, a change of the size of the run-to-run spread, in the direction
the stage measurement predicts (the target check was 2.5-4.2 us per block on wined3d, `stages-wined3d.txt`).

Commands (bottle X3, each alone under the Wine lock; the fixture from the fresh CMake build, copied beside no d3d9.dll):

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_occlusion_cull.py --backend wined3d --repeats 5 --exe <dir>/occlusion_cull_fixture.exe
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_occlusion_cull.py --backend dxvk --repeats 5 --exe <dir>/occlusion_cull_fixture.exe
sh verification/results/occlusion-cull-batched/legacy_build.sh <out>      # the Run137 pass (833f1ac7) on the same scene
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_occlusion_cull.py --backend <b> --repeats 5 --name legacy-<b> --exe <out>/legacy_cost.exe
python3 verification/results/occlusion-cull-batched/summary.py           # -> summary.json
```

Records `verification/results/occlusion-cull-batched/{fixture,legacy}-{wined3d,dxvk}.{json,txt}`, `summary.{py,json}`,
`stages-wined3d.txt` (+ `stages_instrument.py`). The fixture records carry the executable of the fresh build
(4d4e0d48...). The legacy records carry `built_from: 833f1ac7` and `built_sources` (the hashes of the pass and core it
was built from, equal to `git show 833f1ac7:<file>`); their `sources` are the worktree files at run time (runner, scene,
driver). Build flags: the legacy driver is built by `legacy_build.sh` with `-O2` and the same SSE2/stack options as the
CMake fixture, which adds `-g -DNDEBUG` (RelWithDebInfo); neither pass contains an assert. The first wined3d legacy run
printed its complete output but did not exit within the runner's 600 s (`passed: false`); the rerun passed in 62 s.

Functional, both backends 76/76 checks (the functional scene with the dynamic, managed, discard and constants geometry
on fp16 + RT1/RT2 and the production one on A8R8G8B8, then the realistic state on the production one):

- functional scene: hidden parts skipped in all 33 expected frame-part slots from frame 2 on, visible/in-front/alpha
  parts never skipped, teleport drawn at T, the revealed part late exactly one frame (`drawn_late` 1), frames and
  RT1/RT2 identical on and off except frame K inside the part, state back after every part (c252/c253 untouched, or put
  back in the constants mode), every part tested at the first block after the Reset, pool 1,024 back, device references
  back after detach; six back-to-back frames: DXVK 28 of 34 one-frame reads not ready, 23 skips on 32 ready decisions,
  wined3d 24 of 28 not ready, 6 skips on 9 ready decisions, none without a ready hidden result;
- realistic state (32 frames, cull on and off interleaved, every target read back): hidden parts skipped in 3,270 of
  3,270 expected slots from frame 2, visible parts never skipped, the 10 revealed parts drawn from X + 1 (frame 27),
  every visible part tested exactly 3 times over 3K = 24 frames (39/39) and every hidden part every frame (111/111),
  visible re-tests per frame 4..5 against N/K = 4.88 (within +-50 %), RT0/RT1/RT2 identical except frame X inside the
  revealed parts, no state failure, no query error, no failed block.

Cadence refinement (coordinator, 2026-10-08): a plain `hash(node, model) mod K` phase spread 39 visible parts 0..10 per
phase against a mean of 4.9 (host simulation of the fixture's node ids), outside the asked +-50 %; the phase is
therefore chosen at the part's first visible result as the least-loaded phase, searched from that hash seed, then
fixed. Host: 40 parts turning visible together get 5 per phase.

Review fixes (2026-10-08): F7, `note_part` searched only up to the first expired slot of its probe chain and could take a
second copy of a live draw behind it; it now searches the whole chain before reusing an expired slot (host check: two
keys on one chain, the first expired, the second keeps its slot, phase and result, one copy; the expired slot is reused
fresh for its own key). F6, the bound-target check is cached per frame and target binding (the route's key: lazy
RT1/RT2 bits and the HDR redirect state). The reprojection's round-trip test accepts rel when `hull rows x rel`
reproduces every entry of the part's rows within 1e-4 of their largest entry (about 0.2 for rows whose largest entry,
the depth translation of a ship 2 km away, is ~2,000); measured reprojection error 0.00012 px and 6e-8 depth against
14.8 px for the unprojected rectangle (host case: camera 40 m, ship 30 m and 0.02 rad between the frames at 2 km).

Host: `test_occlusion_cull` 10 tests (core driver 90 checks: classification, rectangle, guard, ring, skip rule,
classifier with ship roots, reprojection, retest parsing, blocks per ship with interleaved ships, cadence on phase,
phase balance, hull signature, movement, reset, stale rectangles, depth direction, stale records, the probe chain;
source contracts for the block; the batched records), `test_logging_tiers`, `test_engine_effects`,
`test_config_schema`: OK. `generate.py --check` PASS (259 settings, 109 in the template). Fresh CMake configure and
full build 0 warnings, `d3d9.dll` 90e823a0846bd68b... (59,418,945 B); `check_no_x87.py` PASS (793 functions, 0
violations; an earlier build had four: double `std::fabs` and int64/double QPC conversions, replaced by a
compare-and-negate and integer arithmetic). `run_engine_effects.py --wine-env CX_GRAPHICS_BACKEND=wined3d`: 8 modes,
297 checks, 0 failed (record `verification/results/bottle-X3/engine-effects/summary.json`, still bound to this tree).

Inferred net at the close capital views (110 hidden, 150 candidates, DXVK, constants default): the K = 8 cull costs
~0.2 ms of frame CPU in the fixture; scaled by Run137's flight/fixture ratio (10-25 us in flight against 7.1 us per test
of frame CPU in this fixture, 1.4-3.5x) about 0.28-0.7 ms in flight, against ~1.1 ms saved (110 draws x ~10 us): a net
gain of roughly 0.4-0.8 ms per frame, where Run137 lost ~1.7 ms. Not verified in flight.

Not verified: a flight; native Windows execution; the batched tests' pipeline (GPU) cost.

**2026-10-08, same-build A/B (Run138, run19 off vs run17 on, DXVK): parity.** At matched close-capital windows (237-262 draws) `view_submit_p50` 6.63 ms off vs 6.54 ms on, dt p50 15.5 vs 15.2 ms, both inside the 6.3-6.8 ms spread of the no-cull runs 14/15; issued draws 236 vs 126 at the same draw count, the cull's tests 132 us per frame. A skipped sub-part draw is nearly free on this backend's game thread (inferred). Decision: `occlusion_cull` default off, code kept as an opt-in and as the proxy's only hidden-part oracle; records under `verification/results/run138-dxvk-triage/ab-off-run19/`.

## Engine-side skip (`occlusion_cull = engine`), 2026-10-08/09: built as the duty-cycle probe, fixture-proven, not flown

The smallest test build of [engine-side-occlusion-cull.md](../reverse-engineering/engine-side-occlusion-cull.md) §5
([architecture, "Engine-side skip"](../architecture/occlusion-cull.md)): a third stub (285 bytes, integer only, EAX/ECX
only, no call) chained on the shared `0x0047d2a2` claim, a 512-slot verdict table published at the sector view's Clear
from the previous frame's per-node ledger (every scene draw counted at the scene gate, before the small-prop cull and
every refusal), guards model id / frame stamp / camera-space window (+-1/256 of the largest |component| of
`+0xf0..+0xf8`, floor 1 unit; integer fixed point per chase-lead-reticle.md, about 10 px at a 2560 px focal length), the
skip withheld once per `occlusion_cull_retest` frames on the node's phase. Mode `engine` of the `occlusion_cull` enum
(builtin stays `off`); `on` and `off` patch nothing.

Host: `PYTHONPATH=verification/probe:verification/analysis python3 -m unittest verification/analysis/test_occlusion_cull.py
verification/analysis/test_config_schema.py`: 30 tests OK (core driver 129 checks: the engine table's publish rule,
guards, the withheld phase, overflow, ledger; the stub bytes against the Python twin; the wiring; the records).
`tools/config/generate.py --check` PASS (259 settings, 109 in the template). `test_logging_tiers`, `test_lens_flare_cull`,
`test_cull_small_parts`: OK (58 tests). Clean worktree CMake configure and `d3d9` build: 0 warnings, `d3d9.dll`
157ff9325d6afd4a… (59,494,316 B); `check_no_x87.py` 795 reachable functions, 0 violations.

Stub executed (review 2026-10-08, blocker: the emitted bytes had only been compared): `cull_small_parts_fixture.cpp`
`engine_section`, the stub chained on the synthetic pass alone, first (production order) and last; both exits, every
register, ESP, x87, LastError, the six counters, the stamp guard, another view, `take()`, refusals; 285 checks, 0
failures (engine 28) through `run_cull_small_parts.py` (record `verification/results/cull-small-parts-cpu.json`, row in
[cull-small-parts.md](cull-small-parts.md)). Execution found two defects the byte comparison could not: `install_at`
refused on the site check's "ok" status (the stub was never chained in production) and the stub's hash multiplier
(0x9e3779b9) differed from the table's (0x9e3779b1: no entry was ever found). Both fixed before the records below.

Wine (bottle X3, `run_occlusion_cull.py --repeats 5`, `occlusion_cull_fixture` part 1b: the verdict table driven by the
production pass's verdicts on the part-1 scene through the table's C++ mirror, 30 frames, K = 4): DXVK 86 checks, 0
failed; wined3d 86 checks, 0 failed (records `fixture-{dxvk,wined3d}.json`, summary
`verification/results/occlusion-cull-batched/engine_summary.py`). Both backends identical on the rule: 42 engine
skips, every one following a frame where the proxy skipped every draw of the node (`e_after_full=42`); visible,
in-front and alpha-tested parts never engine-skipped; the node with a second, visible draw never listed (partial
counted 27); hidden1's duty cycle 16/16 frames as predicted (proxy-skip, engine-skip, withheld on its phase frame and
at the stale-stamp frame, `withheld=7`); position guard: after its position leaves the window hidden1 is never
engine-skipped again and is rejected on every listing (7 of 10 frames, the other 3 its phase); model guard 1 rejection
at M and the new draw key drawn untested; stamp guard: all 3 listed nodes rejected at S; the revealed mover drawn from
H + 1 here (one frame late; two when the reveal lands on a proxy-skip frame, three with an age-2 result, see the
architecture note); no overflow, state back after every part, no query error; `skipped_draws` = one per engine-skipped
node. The realistic and part-1 checks are unchanged (constants buffer about 1.2 / 0.7 us per test, 7.5-9.5 us per
block on DXVK / wined3d, measured).

Cost: host microbenchmark `engine_bench.cpp` (clang -O2, arm64 native, measured): ledger of 300 scene draws plus the
publish 0.73-0.85 us per frame, 400 lookups (the stub's logic in C++) 0.41 us; the stub itself is 2 instructions per
pass visit disarmed, 5 outside the sector view, about 13-31 on a sector-view visit (210-400 visits per frame measured
in run14/run15): under 10 us per frame inferred, not measured in flight. CPU fixture bench per 12-node pass with the
chain live: native 0.227 us, patched disarmed 0.252, armed 0.234 (Wine/FEX, harness included, not game FPS).

Not verified: a flight (the saving, the reveal pop, `engine_visits`/`engine_view_changes` on the real view pointer);
native Windows execution.


### 2026-10-09 Run 139 A (run20 engine / run21 off, DXVK, same build d48d2bab…): engine-side skip is a net loss

Triage [run139-triage](../../verification/results/run139-triage/) (`run139.py`, measured). The stub installed (`status=patched reason=ok`) and ran in 97 % of in-flight frames: engine_skipped_parts p50 18 / p95 33 / max 47, engine_skipped_draws p50 19 / max 81, guards 0 model / 0 stamp / 22,709 position, engine_visits p50 198, view changes 0, no errors. Frame time binned by scene draws (app draws + engine-skipped draws): off is 1-2 ms faster per frame below ~200 draws (140: 12.7 vs 10.7 ms p50; 160: 12.9 vs 11.1), level at 200, and engine is only slightly better at 220-260 draws, mostly in p95 (240: 15.0/19.6 vs 15.1/20.5). Overall dt p50 13.53 vs 13.64 ms. The engine run carries the batched D3D cull as well (27 skipped draws p50), so the low-draw loss is the cull's own cost with little to skip; the gain where 30-46 draws were skipped is small. User impression confirmed ("off generally better except some close views"). Decision: `occlusion_cull` stays off by default, `engine` kept as an opt-in; the occlusion-cull line is closed on this backend (both the draw-level and the engine-side skip). Open: an isolated A/B (batched-only vs engine on one route) was not flown and is not planned.
