# Occlusion cull of ship sub-parts

`X3M_OCCLUSION_CULL=on|off` (`occlusion_cull` in `x3m.ini`, default off since the same-build A/B of 2026-10-08: on DXVK the cull halves the issued draws but frame time and `view_submit` do not change, so it is an opt-in; user decision 2026-10-08). Skips the game's
draws of turrets, dock ports, antennas and similar ship parts that the previous frame's occlusion test found completely
hidden behind their hull. Render-only: the engine's state, the cull/LOD pass and the simulation are untouched.

Sources: `src/proxy/occlusion_cull_core.h` (pure rules and bookkeeping), `src/renderer/occlusion_cull_pass.{h,cpp}`
(the D3D side), `src/proxy/motion_output_occlusion_cull_inc.h` (draw-site integration), the call site in
`MotionOutput::evaluate_draw` right after the small-prop cull. Evidence: [ledger](../verification/occlusion-cull.md).

## Why

At close capital-ship views 67-91 sub-part draws per frame are fully hidden behind hulls (run14/run15 captures,
`verification/results/occlusion-cull-estimate/`), at a measured 26 us of game-thread submit cost per draw: 1.7-2.4 ms
per frame. Hull pieces draw before the parts they hide, so a test issued at a ship's first part draw sees the hull's
depth. Run 137 A (the per-part test) showed a skipped draw saves only ~10 us (the 26 us included the proxy's own
per-draw work) while each per-part test cost 10-25 us in flight; hence the batching and the re-test cadence below.

## Candidates

A main-scene draw (gate 2 passed) with z test on and blending off is classified by its scope node's body path through
the engine's body table (the census path, `cull_census_core.h`), cached per model id and memoised per node per frame:

| class | rule (lower-cased path, `/` read as `\`) |
| --- | --- |
| part | a dock cut scene's inline body id (`dock_model`), `ships\props\`, or a path containing `turret`, `weapon`, `gbarrel`, `dock`, `antenna` |
| effect | a path containing `effects\` (engine jets, glows): never a candidate |
| hull | any other `ships\` body |
| other | everything else (stations, environments, menu graphics, unnamed) |

A hull draw records its node as this frame's hull owner, and its parent when that parent is a ship root. A ship root
is a top-level node (null parent): every hull draw with a known ancestry in the run14/run15 captures has ancestry 2
(hull -> root), parts 2 or 3 (`verification/results/occlusion-cull/ancestry.{py,txt}`, measured;
[ship-scene-parts.md](../reverse-engineering/ship-scene-parts.md)). A part's hull "drew this frame" when one of its
ancestors (at most six parent links, ending at its own root) is an owner, so no node above a ship root, and no other
object's root, can carry a hull verdict. Every part draw with a known vertex extent of the buffer's current revision
(the shadow-replay extent cache the small-prop cull and the `object_bounds` rows use; a stale extent of an earlier
revision is treated as no bounds until refreshed) and known clip rows is a candidate; alpha-tested parts are allowed.

The extents exist only while the shadow-replay candidate counter runs (a cascade set, the launcher default, or the
diagnostic counter). Without it the device logs one `occlusion_cull_device attached=0 configured=0
reason=no_bounds_source` row and the cull is off there.

## Mechanism

Batched per ship since 2026-10-08 (Run 137 A: the per-part test cost 10-25 us in flight, two pipeline rebinds per
part). The ship is the top-level root its hull owner hangs off (`Classifier` records it with each owner).

1. Hull draw: a hull node's first draw of the frame adds its draw key to its ship's hull signature (a commutative sum,
   draw order does not matter) and records its clip rows; the ship's first hull node with rows is its reprojection
   reference (`Batcher::note_hull`).
2. Part draw (hull drawn this frame): the draw is entered in a persistent per-draw table (2,048 slots, key: scope node,
   vertex buffer, first vertex, vertex count, model id) and in this frame's list (at most 512): its vertex-extent box,
   its rectangle and its rows relative to the ship's reference hull, `rel = inverse(hull rows) x part rows` (the
   inverse in double; accepted only when `hull rows x rel` gives every entry of the part's rows back within 1e-4 of
   their largest entry: about 0.2 for rows whose largest entry, the depth translation of a ship 2 km away, is ~2,000).
3. At the frame's first part the pass reads the older frames' queries with `GetData(..., 0)`, never a flush: frame
   N-2's tests that were not ready at their first read are polled again before their slot is reused, then frame N-1's.
   Ready and 0 samples = hidden, ready and any sample = visible, not ready or an error = no result. Each result also
   goes to the table (the part's last result, the cadence's input).
4. Block: at a ship's first part draw of the frame the pass issues one block for the ship: the ship's parts of the
   previous frame's list (grouped by ship once per frame, a counting sort), each part's rectangle carried to this frame
   as `hull rows now x rel` (exact for a part rigid with its hull under any camera, ship or projection change; host
   check: 0.00012 px and 6e-8 depth against 14.8 px for last frame's rectangle in a 40 m camera / 30 m ship move at 2 km; a turning
   turret keeps last frame's angle, within the stability guard), filtered by the re-test cadence (below). A part whose
   reference hull did not draw with rows this frame is tested on last frame's rectangle (`stale=`). One state swap,
   then per test the rectangle into `c252-c253`, `Issue(BEGIN)`, a two-triangle strip, `Issue(END)`, then one restore
   (the production constants mode; no Lock on the game thread). A part first seen this frame is drawn untested and joins next frame's block. One block per ship per frame.
5. Decision at every part draw: skip when the same draw's most recent ready test (one or two frames old, `ready_age=`)
   read hidden, its hull drew this frame and its rectangle is stable against that test's. The real draw is then not
   forwarded (the hook returns D3D_OK, as the small-prop cull does).

A part whose hull has not drawn when the part draws (hull after the part, or no hull) is never listed or tested and is
drawn (`no_hull=`); the run14 triage shows hull draws precede their parts, so no per-part fallback remains.

Re-test cadence (`X3M_OCCLUSION_CULL_RETEST`, `occlusion_cull_retest`, 1..64, default 8): a part whose last ready
result was hidden, or which has none (new, not ready, an error, after a Reset), is tested at every block; a part last
read visible only on its phase frame, `frame mod K == phase`, once every K frames; off-phase at once when its hull
signature changed or its rectangle is unstable against its last test's (`forced=`). Phases are staggered so each frame
re-tests about 1/K of the visible parts (coordinator refinement 2026-10-08): the phase is fixed for the part's life,
chosen at its first visible result as the least-loaded phase among the visible parts of the previous frame's list,
searched from a seed `hash(node, model) mod K`, the current frame's phase only when it is strictly the least loaded. A
plain `hash mod K` spread 39 visible parts 0..10 per phase against a mean of 4.9 (host simulation), outside the asked
+-50 % flatness. `cadence=` counts the phase re-tests per frame, `retest_phase_spread=min,max` their range over the last
K frames, `retest_skipped=` the visible parts left untested.

The test draw (production, `OcclusionCullBuffer::constants`, coordinator decision 2026-10-08): vs_3_0 `mad o0, v0, c253,
c252` over a four-vertex MANAGED unit-square strip (FLOAT2), the rectangle in `c252 = (x0, y0, z, 1)` and `c253 = (x1 -
x0, y1 - y0, 0, 0)` per test, the application's `c252-c253` put back from the route's shadow after the block (left as
the last rectangle when the application never wrote them), `D3DCULL_NONE` for the block, a ps_3_0 that writes 0 to every output the device has (oC0..oC3 at
most), z write off, alpha test, stencil and separate alpha off, blend op ADD, and `ALPHABLENDENABLE` with `SRCBLEND
ZERO` / `DESTBLEND ONE`, so every bound target keeps its value. Every output is written because the motion route's lazy
mode keeps its own RT1 (A32B32G32R32F motion) and RT2 (R32F depth) bound between routed draws, and a D3D9 output the
shader does not write is undefined (0 x NaN = NaN under the blend). The targets bound at the block are read from the
device (not the shadow: under the HDR redirect RT0 is the FP16 scene target): each must pass
`CheckDeviceFormat(D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING)` and more than one needs
`D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING`, else the block's parts are drawn untested. With RT1/RT2 bound, over 60 frames
at 128 tests (measured, `gate.json` "mrt"): zeros + blend 1.3 us (DXVK) / 17 us (wined3d) of pipeline per test with
RT1/RT2 byte-identical (a stored -0.0 included); masking RT1/RT2 (COLORWRITEENABLE1/2 0) 64 / 67 us and unbinding them
as the lazy flush does 68 / 58 us, steady-state render-pass splits. The RT0 colour write mask is not touched either
(55-79 us on DXVK). Everything changed is restored from the native getters (shaders, stream 0; the declaration first,
then the FVF when the application's binding was one) and the route's shadow (render states). A restore failure takes the
route's restore-failure path (state invalidated, TAA invalidated).

Fixture-measured alternatives (not production): a rectangle buffer sized to the pool (1,024 rectangles, 64 KB) written
with one Lock per block and drawn by vs_3_0 `mov o0, v0` over FLOAT4 clip-space vertices: DEFAULT pool
`D3DUSAGE_DYNAMIC` with `NOOVERWRITE` appends and `DISCARD` at the wrap (`dynamic`), `DISCARD` every block (`discard`),
or MANAGED with a Lock of the block's region of its frame slot (`managed`). On wined3d over macOS GL every Lock waits for
the command stream (157-272 us per block), hence the constants default (measurements under "Cost").

Refused (drawn untested): no scope node, no extent or rows, a corner at or behind the eye or in front of the near plane,
a depth function other than LESS/LESSEQUAL/GREATER/GREATEREQUAL (or another direction at the block than at the part's
listing), sRGB writes, a fill mode other than SOLID, a depth or slope bias, user clip planes, an application query open,
a stream-0 frequency other than 1, a bound target that cannot blend (or several without MRT blending), a full pool slot,
table or list.

## Pool and lifetime

One pass per device: 1,024 occlusion queries (two frame slots of 512, the ring; no per-frame allocation), created at
the first part draw through the native `CreateQuery` under the route's reference accounting (`taa_call`; every query
holds a device reference: 1,028 references with the programs, measured). `CreateQuery(D3DQUERYTYPE_OCCLUSION, nullptr)`
other than S_OK: one `occlusion_cull_device attached=0 reason=unsupported` row and the cull stays off until a Reset.
`before_reset` releases the queries and empties the ring; a successful Reset arms the recreation, done at the next
part under the accounting; the first frame after it has no previous result and draws everything. A dynamic rectangle
buffer (DEFAULT pool) goes with the queries and comes back with them; the programs, the declaration and a MANAGED buffer
survive Reset. The motion route resets the batcher's results with `before_reset` (`Batcher::reset_results`), so every
part is tested at its ship's next block; the lists survive, so the first frame after the Reset already tests. Teardown
detaches under the accounting. The batcher (per-draw table, lists, ship and hull tables, ~0.6 MB) is one allocation
with the classifier.

No budget (user 2026-10-08): every due part is tested until the frame's 512 queries are used; the rest is drawn
untested and counted `pool_truncated=`.

## Guards

- Stability: the hidden verdict carries over only while the rectangle's centre moved at most a quarter of its size
  (16 px minimum) and its area changed at most (4/3)^2. A turret turning in place keeps it; a teleport, a camera cut or a
  fast pan draws the part that frame (`unstable=`).
- Hull: a part whose hull did not draw before it this frame is drawn, never listed or tested (`no_hull=`).
- Hull change: a ship whose hull signature changed (another hull draw set before the block) re-tests all its parts.
- Reset: the first frame after draws everything; every part is tested at that frame's block.
- Re-emergence: a part revealed with an unchanged rectangle (the hull moved away) is skipped once more and drawn from the
  next frame: one frame late (`drawn_late=`), accepted; two frames late when that frame's decision used an age-2 result
  (its frame N-1 test not ready yet).

## Cost (measured, bottle X3, 2026-10-08)

Realistic state (`occlusion_cull_fixture.exe`, part 2; tracked records `verification/results/occlusion-cull-batched/
fixture-{wined3d,dxvk}.json` and `legacy-{wined3d,dxvk}.json`): 1280 x 720 A16B16G16R16F scene target with the route's
RT1/RT2 bound, a game vs/ps pair with per-draw constants bound before every block, three ships of four hull pieces and
50 parts (111 hidden, 39 visible), 16 ms pacing, no readback; per configuration 200 frames after 20 warm-ups, five
interleaved repeats, medians. Test time = QPC inside the blocks (Run137: around each per-part call); frame CPU =
BeginScene to the return of Present, minus the same scene without the pass. Per test and per block come from two
test-only configurations (K = 1, verdict ignored): every part (150 tests, 3 blocks) and one part per ship (3 tests, 3
blocks). "Run137" is the pass of commit 833f1ac7 on the same scene (`legacy_cost.cpp`, `legacy_build.sh`).

Production default, `constants`:

| backend | per test | per block | 150 tests: test time / frame CPU | K = 8 cull: tests / test time / frame CPU |
| --- | --- | --- | --- | --- |
| DXVK | 1.21 us | 7.4 us | 204 / 317 us | 116 / 174 / 199 us |
| wined3d | 0.73 us | 8.9 us | 136 / 208 us | 116 / 124 / 134 us |
| Run137 per part, DXVK | 6.20 us | - | 930 / 1,063 us | 150 / 931 / 908 us |
| Run137 per part, wined3d | 2.12 us | - | 318 / 326 us | 150 / 295 / 230 us |

The frame CPU of the K = 8 cull also contains the fixture's skipped draws (about 1 us each here, ~10 us in the game).

Measured alternatives (fixture only, not production; the same records):

| geometry | DXVK per test / per block | DXVK 150 tests: test time / frame CPU | wined3d per test / per block | wined3d 150 tests: test time / frame CPU |
| --- | --- | --- | --- | --- |
| dynamic buffer (NOOVERWRITE, DISCARD at the wrap) | 0.69 / 8.1 us | 127 / 276 us | 3.29 / 174 us | 1,018 / 1,129 us |
| managed buffer | 0.76 / 8.5 us | 136 / 313 us | 3.35 / 173 us | 1,027 / 1,127 us |
| discard every block | 0.69 / 7.9 us | 128 / 271 us | 3.02 / 197 us | 1,039 / 1,130 us |

On wined3d over macOS GL every buffer Lock waits for the command stream: per block plan 0.8-8.8, targets 2.5-4.2,
getters 1.5-2.4, Lock 157-272, set 1.6-2.6, query loop 8-25 (3-50 tests), restore 0.8-1.1 us in every buffer mode
(`stages-wined3d.txt`, instrumented copy of the pass from `stages_instrument.py`, taken before the constants mode and
the per-frame target cache existed). The constants default avoids the Lock on every target at about 0.5 us per test
more than a buffer on DXVK; native Windows constant uploads are the documented cheap path. The bound-target check
(GetRenderTarget and GetDesc per target) runs once per frame and target binding, not per block.

The step-0 gate (Run137 per-part shape, `verification/results/occlusion-cull/gate.json`): 3.0-3.4 us CPU and 4.2-5.4 us
pipeline per test on DXVK in the smoke fixture's simple state; every result was ready one frame later at 13 and 20 ms
pacing up to 512 tests per frame. The pipeline cost of the batched tests was not measured separately.

## Logging

`occlusion_cull_config` once at start-up; `occlusion_cull_device` at attach (or `reason=no_bounds_source`); under
`--debug` one `occlusion_cull` row per frame with candidates (`candidates tested hidden skipped pool ready not_ready
ready_lag2 ready_age=age1,age2,none errors drawn_late pool_truncated unstable no_hull refused failed no_bounds unbounded
state retest_skipped blocks stale test_us cadence forced retest_phase_spread=min,max`; `test_us` is the QPC time inside
the frame's blocks, so a flight separates the test cost from the saving; `ready_age` none now also counts the visible
parts the cadence left untested); in every tier one `occlusion_cull_session` row
every 300 frames with the session totals (the same new fields, `test_us` summed, and `retest=`).
`occlusion_cull_config` carries `retest= retest_setting= retest_status=`.

## Known limits

- A skipped part is not a shadow-replay caster that frame: a hidden turret's sun shadow on visible hull disappears
  while it is skipped (the small-prop cull has the same property).
- With `on`, only the part draws themselves are skipped; nothing is skipped on the engine side. `engine` (below) adds
  the engine-side skip studied 2026-10-08 ([engine-side-occlusion-cull.md](../reverse-engineering/engine-side-occlusion-cull.md)),
  as the note's smallest test build (the duty-cycle probe), 0.35-1.0 ms expected at close-capital views (inferred).
- A part first seen in a frame is drawn untested and joins the next frame's block: skipping starts on its third frame
  (frame 0 lists it, frame 1 tests it, frame 2 skips), one frame later than the per-part test of Run137.
- A part last read visible that becomes hidden without moving and without a hull change keeps being drawn until its
  next phase frame (at most K - 1 frames of lost saving, never an artefact).

## Engine-side skip (`occlusion_cull = engine`, 2026-10-08, test build)

The duty-cycle probe of [engine-side-occlusion-cull.md](../reverse-engineering/engine-side-occlusion-cull.md) §5.
Sources: `src/proxy/occlusion_engine_core.h` (pure: mode parser, ledger, verdict table, window, stub encoder),
`src/proxy/occlusion_engine_cull.{h,cpp}` (the claim, the stub's words, publish/take), the ledger and publish calls in
`motion_output_occlusion_cull_inc.h`, the publish site in `MotionOutput::after_clear`.

**Mode.** `X3M_OCCLUSION_CULL=engine` is `on` plus a third stub chained on the shared `cull_small_parts` claim of the
cull/LOD pass `0x0047cfe0` at `0x0047d2a2` (`cull_small_parts::chain_stub`: one `engine_patch` claim, window
`0x0047d294..0x0047d2cc` byte-verified, pushed in front of the lens-flare and small-parts stubs when those are live, so
the chain after install runs engine-skip -> lens-flare -> small-parts -> tail; restored once by
`cull_small_parts::shutdown`). Claimed on the backend-load path inside the install window after
`lens_flare_cull::initialize`; refused (`occlusion_engine_cull status=refused reason=`: `executable_mismatch`,
`late_claim`, `arena_full`, `bytes_mismatch`, `chain_failed`, `rollback_failed`) leaves the draw-level cull alone. With
the mode `on` or `off` nothing is patched and no engine byte changes (`reason=mode`).

**Rule.** At the sector view's Clear (the scene-phase Clear the route already observes for the camera latch; the
engine's `0x00472260`, which precedes the view's pass at `0x0047226b`) the route publishes a table of the nodes whose
every scene draw it skipped in the previous frame (the ledger: every scene draw with a scope node counts at the scene
gate of `evaluate_draw`, before the small-prop cull and before every refusal of the draw-level cull, user memory, an
open application query, a failed pass, z/blend state and missing bounds included; only a proxy skip counts against
the draw, so a node with any other draw is never listed; `engine_partial=` counts those with some but not all draws
skipped). The stub skips a listed node's whole render visit for that view and frame (the engine's own
size-cull instruction `0x0047d2c3`: renderable bit cleared, LOD selection and the render visit skipped, children still
visited). An engine-skipped node has no draws that frame, so it is not listed for the next one, goes through the draw
path there (decided by the existing draw-level rule from the previous block's test) and is listed again: every hidden
node alternates proxy-skip and engine-skip, the listing and testing of the draw-level cull run unchanged. The stub is
armed between the publish and that frame's Present (`occlusion_engine_cull::take` reads its counters and disarms it).

**Guards (per entry, in the stub).** The pass's view argument `[ESP+0x28]` must equal the published sector-view
pointer (the frame's first scene draw's `object_trace` camera; nodes of other views pay one compare); the node's model
id `+0x140` must equal the entry's (a freed address reused by another body is not skipped); the entry's frame stamp
must equal the published one (an entry of another frame is never used); the node's camera-space position
`+0xf0..+0xf8` must lie inside the entry's window: the position at the node's last skipped draw +- 1/256 of the largest
|component| (floor 1 unit), six signed compares. The three words are integer fixed point: the node's `+0x30` origin
relative to the sector camera through its /65536 basis rows
([chase-lead-reticle.md](../reverse-engineering/chase-lead-reticle.md), "the `+0xf0` vector", writers
`0x00420ec8`/`0x00420f60`/`0x00420ff8`; the per-view transform walk `0x0047b800` rewrites them before the pass).
1/256 of the camera-space distance is about 10 px of projected offset for a part on the view axis at the user's
2560 px focal length (5120 px wide at 90 degrees), the order of the draw-level pixel guard; 1/64 (the first cut) was
~40 px. `occlusion_engine_sample` rows (at most eight per publish under `--debug`) carry each entry's bounds. A
position outside the window, a model or stamp mismatch counts in `guard_rejected=model,stamp,position` and the node
goes down the chain (drawn or proxy-skipped as before). A node whose position words are all zero is not published
(`engine_no_position=`).

**Withheld on the phase frame.** A fully skipped node is withheld from the table once every `occlusion_cull_retest`
frames on its own phase (`retest_phase(node, model, K)`, the visible re-test's stagger), `withheld=`; it then goes
through the draw path that frame, where the draw-level verdict still applies (it is not a forced draw: the proxy-skip
frame of the duty cycle already lists the part with its current rows).

**Table.** 512 home slots of 48 bytes plus 4 spare (a linear probe of at most 4 never wraps), multiplicative hash of
the node into the top 9 bits, rebuilt every publish from the previous frame's ledger (the used slots cleared: no
allocation, no memset of the whole table); a chain that is full drops the node (`engine_overflow=`). Single thread: the
Clear hook, the pass and the Present hook all run on the engine's render thread, so no lock; the stub reads only
entries the publish completed before the Clear returned.

**Stub.** 285 bytes, integer only, no call, no Win32, no floating point (LastError and the x87 stack untouched by
construction); writes EAX and ECX only (both dead at the site), EDX/ESI/EDI/EBX/EBP/ESP untouched, EFLAGS dead on every
exit as for the other two stubs; the same replay of `0x0047d2a2..0x0047d2b9` before `jmp 0x0047d2c3`. Disarmed: one
compare and a branch per pass visit; armed outside the sector view: three instructions; in the sector view: the visit
count, the hash (3) and one 5-instruction probe per occupied chain slot (at most 4), a hit nine compares. Bytes proven
against the Python twin in `test_occlusion_cull.py`; executed on the synthetic pass of `cull_small_parts_fixture.cpp`
(`engine_section`: both exits, registers, ESP, x87, LastError, the six counters, the stamp guard, a non-sector view,
chained with the lens-flare and small-parts stubs in both orders; ledger
[occlusion-cull.md](../verification/occlusion-cull.md)).

**Cost (measured on the host, inferred for the game).** Host microbenchmark (clang -O2, arm64 native): the ledger of
300 scene draws over 250 nodes (60 fully skipped) plus the publish 0.85 us per frame; 400 table lookups (the stub's
logic in C++) 0.41 us per frame. Under FEX the x86 stub runs translated; the pass makes 210-400 visits per frame
(measured, run14/run15), so the hook's per-frame cost is a few microseconds, under the 10 us budget (inferred: the
instruction counts above, not a flight measurement). One 12-byte `engine_memory` read per fully skipped node per frame.

**Diagnostics.** The `occlusion_cull` frame row (--debug) and the `occlusion_cull_session` row gain `engine=`,
`engine_published=`, `engine_skipped_parts=`, `engine_skipped_draws=` (from the entries' ledger draw counts),
`guard_rejected=model,stamp,position`, `withheld=`, `engine_partial=`, `engine_no_position=`, `engine_overflow=`,
`engine_dropped=` (ledger chains full), `engine_unarmed=` (frames with nothing published), `engine_visits=` (the
stub's sector-view visits while armed: `engine_published > 0` with `engine_visits = 0` means the pass's view pointer
is not the ledger's camera) and `engine_view_changes=` (the ledger's camera pointer differing from the previous
publish's); `ready_age` and `drawn_late` are unchanged. `occlusion_cull_config` carries `engine= engine_status=`;
`occlusion_engine_cull status=` once. `frame_phases` is untouched.

**Limits.** A skipped part casts no sun shadow (the shadow replay sees no draw), is no lens-flare occluder (it sits
behind its hull, which stays one) and its texture animation and light selection pause while skipped (resumed on
reveal). The duty cycle halves the saving of the full design (every hidden node draws through the proxy every other
frame). Reveal latency: one frame when the reveal lands on an engine-skip frame, two when it lands on a proxy-skip
frame (that frame's skip rests on a test issued before the hull changed, and the next frame's table is built from it;
fixture `mover_drawn_from_h2`), three with an age-2 result; the draw-level cull alone is one (two with age 2). Not
verified in flight; native Windows execution not verified (the patch infrastructure is the dock-port cull's).
