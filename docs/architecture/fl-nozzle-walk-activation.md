# Activating the engine node walk on Farnham's Legacy

2026-11-03, design only (no source edit, no launch, no Wine, read-only on the game dirs and on worktree
`agent-a00088c5ba134496e` at `454e4e470`). Decision for the main session to ratify. Inputs:
[fl/nozzle-scene-window.md](../reverse-engineering/fl/nozzle-scene-window.md), [fl/engine-effects.md](../reverse-engineering/fl/engine-effects.md)
(§4, "Verified 2026-11-02/03"), [fl/cull-chain.md](../reverse-engineering/fl/cull-chain.md) ("Verified 2026-11-03"),
[engine-effects-modern.md](engine-effects-modern.md), [engine-nozzle-source.md](engine-nozzle-source.md),
[plumes-no-glow-hulls.md](plumes-no-glow-hulls.md) (step 1 "As built", worktree), `src/proxy/engine_nozzle_walk_core.h`,
`engine_node_layout.h`, `engine_far_jets.cpp`, `motion_output_engine_plumes_inc.h`, `src/engine/fl/object_trace_fl.cpp`,
`scene_hook_fl.cpp`, the FL ledger runs 7, 18–20, 27–28. Tags: **[m]** measured (a producer, fixture or flight row cited
here or read in source this session); **[i]** inferred; **[u]** not established.

## Decision

**Option (b): run the existing walk on FL under the synchronous native borrow the far route already uses, at the
boundary it already runs at.** On FL the plume stage's append (`run_engine_plumes` → `engine_far_append`,
`engine_node_append`) executes inside `scene_end_hook` → `resolve_hdr(SceneEndSource::Hook)`, i.e. inside the
`0x48b929` pre-compositor callback that [nozzle-scene-window.md](../reverse-engineering/fl/nozzle-scene-window.md)
qualified as the permitted root/list copy boundary for roots the accepted main Scene submitted [m, source
`motion_output.cpp:2876–2917`, `scene_hook_fl_inc.h`; i for the permission]. Two changes make the inherited note's three
open items concrete: (1) the context scale becomes an **owned** value copied at the gather, inside the draw's material
scope (camera `+0x1c` → context `+0x2c`, the same FL-anchored fields the far handler owns at the cull site), so the
append never dereferences a camera later; (2) on FL the append runs **only** when this frame's scene end came from the
hook: the Present-time `engine_light_frame` fallback and the `StretchRect` resolve path do not walk (counted
`node_no_boundary`), which is the policy for frames that miss the boundary. The reads themselves stay
`engine_memory::read` (VirtualQuery-validated, no engine call), the same reader and the same frame window the FL draw
route (`object_trace_fl::current`, 0x154 bytes per draw) and the far route's stage-time parent `+0xa4` read already use
on FL since run 18 / run 28. Rollback is one ini line, `engine_nozzle_source = draw`, which restores today's
`fl_draw_only` behaviour read for read.

## 1. Why the walk is fenced on FL, and what draw cannot give

The fence (`fl_draw_only`, `motion_output_engine_plumes_inc.h` configure) cites fl/engine-effects.md rule 4: "the
missing read-window guarantee". Every **field** the walk reads already has a decoded X3FL.exe anchor; what has no
verified FL contract is the **window**, i.e. that the list and the camera are live and unmutated while the append
reads them, and the **placement** of the three later reads. Per read [m: anchors from `read_set.py` (47 anchors, 18
fields), `hidden_emitter_reads.py` (20 PASS), fl/engine-effects.md §2, nozzle-scene-window.md §1]:

| Walk read | FL anchor (VA at `0x400000`) | Covered by an FL contract today? |
| --- | --- | --- |
| Root `+0xc` head, child `+0` next, sentinel (`[+0] == 0`) | constructor `0x4a22ab..b8`, emitter list `0x414eff`, drive list `0x464630`, next `0x464bc4`, destructor `0x4a33b0` | layout yes; **traversal window [u]** (the 2026-10-29 note's "zero-call copy at the scene boundary" item) |
| Child `+0x18` parent, `+0x28` handle | reparent `0x4a5809`, allocation `0x4a22a8` | yes (object_trace FL, culls, far handler) |
| `+0x12c` hidden `0x100000`, `+0x130` JET pair `0x4000001` | drive `0x46484b..d8`, classifier `0x43deb3` | yes (recogniser, far handler) |
| `+0x70`, `+0x80/84/88`, `+0xb0..b8`, `+0xc0..c8`, `+0xe0..e8` | setter `0x4a3af8/b09/b28`, world matrix `0x4de59a`, `0x4deb15..`, `0x4de9ef..`, `0x4dea66..` | yes (far handler, same offsets as AP) |
| Model `+0x144`; node block 0x150 of 0x270 bytes | `0x49621c`, `0x4a365d`; allocation `0x4a2203` | yes (`engine_node_layout::fl`) |
| Step 1: C word `+0x260` (hidden emitters) | drive `0x4646b6`; transform walks `0x494370/0x4947b0/0x494b00` have no hidden test | yes for the field and for hidden-node currency [m, `hidden_emitter_reads.py`] |
| Context scale: camera `+0x1c` → context `+0x2c`, read **at the append** | `0x48ba48`, `0x4def18`, `0x495fc7/ce` | field yes; **the later dereference is what §4 rules out [u]** ("owned context scale") |
| Parent `+0xa4` radius at the stage (plume floor) | `0x4a39c3`, dirty `0x4a3b1b/0x4a580f` | ratified for the far route 2026-11-03 (same read, same reader) |
| Lifetime serial (`object_lifetime::current`, registry walk at the append) | FL observer, step 3b | the draw route's contract (validated reads, fail closed) |

So the fence protects against three things: a node unlinked or freed between the hull draw that named the root and
the append; a camera/context pointer stale at the append; and an append that runs outside any native frame (the
Present-time light fallback). Nothing in the fence is about layout.

**What draw misses.** The draw source derives anchors from the glow draws (`c4–6` rows of the effect pair). A hidden
`fx_engine_emitter` is never submitted: the drive sets `+0x12c |= 0x100000` on a main jet whose C lacks `0x1000`
[m, FL `0x46484b..d8`], the selector clears renderable bit 2 and skips its material work, and the emitter body carries
no sprite (col 11 = 5, light only) [m, plumes-no-glow-hulls.md §1]. It has no draw, by construction; the walk is the
only in-process source for its `+0xb0`/basis/`+0x88`. The same holds for the walk's original purpose, a nozzle the
frustum test culled while the hull stayed on screen (fl/engine-effects.md §3: rejected at `0x495fd3..e3`, before the
size claim). Draw also cannot feed the light's hold (`hold_ships` takes the walked roots).

## 2. Options

Common to every option: the per-read cost on FL is ~10 ns validated (run 18 bench, 10.5 ns) [m]; a walk is
`2C+J+2` reads (`12C+336J+8` bytes) per root [m, fl/engine-effects.md §5]; the host fixture measures 1.21 µs per
108-part root, 1.18 with the anchor rule [m, As built]. FL frame p50 4 ms (run 27, measured). The RootSet caps at 256
roots/frame and the ShipRoots filter admits only roots a record named within two frames plus the own ship, plus at
most 4 anchor probes per frame [m, source]. At 200 roots of FL stock size (p50 28 parts, max 108 on AP; FL list sizes
unmeasured [u]) the append is 0.1–0.25 ms per frame [i], 2.5–6 % of the frame; `engine_stage node_walk_us` measures it.

| | (a) zero-call copy at `0x48b929` into owned storage, consumed later | **(b) existing walk at the stage append under the hook's synchronous borrow** | (c) hybrid: draw anchors, walk only probe roots (no-glow hulls) | (d) keep draw-only, FL anchor from the emitter's own draw |
| --- | --- | --- | --- | --- |
| Reads to verify on X3FL.exe | the table above, plus a raw-dereference discipline (no VirtualQuery): the copy must itself prove page validity by the borrow alone | the table above; new: camera `+0x1c`/context `+0x2c` **at the gather** (anchors `0x48ba48`, `0x4def18`, camera block 0x790 `0x4a45d6`) | the same reads as (b) but only on probe roots | none new; **the emitter never draws** (above), so there is no draw to anchor on. The only FL-native substitute is a site-A stub recorder `(obj, k, pos)`, whose FL argument ABI differs from AP's and which gives no world transform [m, engine-effects-modern.md §2, fl/engine-effects.md §1] |
| Node freed mid-walk | same borrow argument as (b); a raw read of a decommitted page faults (no validation) | the drive and the deferred frees run in the sector update before render preparation (`0x443b76 → 0x463000`, then `0x48ba3a/43/4c`) [m, §4]; between submission and `0x48b929` no named path unlinks a submitted main-scene root or child [s/i, nozzle-scene-window §1]; a reused block fails the `+0x18 == root`, JET-pair and range guards; `engine_memory::read` refuses a decommitted page | same as (b), fewer lists | n/a |
| Scene boundary missed (menu, load, a camera list without layer > 17) | needs the EndScene `0x4e7189` fallback the note leaves [u] | **no walk**: `engine_node_append` is a no-op unless this frame resolved with `SceneEndSource::Hook`; draw and far records stand (today's behaviour). Measured: `frames_without_call=0` in flight, hook on 34,211 of 35,196 frames, `none` on 985 menu frames (run 7) [m] | same | n/a |
| Frame without the boundary under SETA | SETA changes game time per frame, not the frame routine's shape; the probe clock already scales by `seta_rate_` (FL cfg `+0xbc/+0xc0`); no new fixed time bound is added by any option [m, source] | same | same | n/a |
| Per frame at 200+ roots | (b)'s cost plus a second copy into an owned ring (72 B × ≤1,024) | 0.1–0.25 ms [i]; one extra camera read per new root per frame, memoised over 4 cameras | ≤ 4 probe walks + anchored roots (16 FL ships carry emitters [m]): tens of µs | 0 |
| Fixture | a new owned-ring module and tests; the walk host fixture does not apply | `engine_nozzle_walk_host.cpp` (FL layout already runs), a `X3M_MOTION_OUTPUT_FIXTURE` FL case for the gate and the owned scale, a `read_set.py`-style producer for the walk's anchors | (b)'s fixtures plus a new source mode and its tests | none |
| Why it loses / wins | the stage already consumes at `0x48b929`; owning a second copy there and consuming it later adds storage and a Present-time consumer for data the stage uses at the boundary. Dropping VirtualQuery for "zero calls" removes the one check that turns a stale page into a refusal. **Loses** | the code exists and is flown on AP (runs 139+); FL differences are the model offset (done), the owned scale and the Hook-only gate. **Wins** | lowest exposure, but gives up the frustum-culled nozzle and the light hold, needs a third `Source` mode, and it is (b) with a smaller root set: the right **cost fallback** if `node_walk_us` measures high, not the first step | cannot give the hidden emitter (no draw) and cannot give step 2's capitals either. **Loses** |

The "zero-call" discipline of the 2026-10-29 note is kept where it matters: the walk calls no engine function
(never the radius writer `0x4a39b0`), allocates nothing and takes no observer lock across native loads; `log()` rows
emitted inside the root loop (`engine_plumes_hidden_emitter_hull`, at most 32 per configure) should move after the
loop so no file write sits between two list reads. VirtualQuery is a kernel call, not an engine re-entry; the FL
draw route makes the same call pattern per draw on every frame since run 18 [m].

## 3. Recommendation, first flown step, kill criterion, brief

**Recommended: (b)**, with the owned scale at the gather and Hook-only appends on FL. Minimal first flown step: one FL
build with `engine_nozzle_source` honoured on FL (`status=ok source=node`), jets **and** anchors on (the existing
default), flown in a clear sector and at an Argon shipyard with a Heavy Centaur (`SS_SH_A_M6M`, 4 emitters).

Kill criterion (one flight, any one): `engine_stage node_unreadable > 0` or an exception row with the stage armed;
`node_walk_us` p99 above 0.5 ms (or above 10 % of `frame_end dt_ms` p50) in the heavy fogged sector; a plume
floating off a hull or an anchor glowing while parked. Then `engine_nozzle_source = draw` in `x3m.ini` (no rebuild) and
option (c) becomes the next shape.

**Brief for `implement-deep`** (Opus 5.5 high; hook-adjacent lifetime change on FL):

- Goal: `engine_nozzle_source = walk/node` runs on FL. Files: `src/proxy/motion_output_engine_plumes_inc.h` (configure:
  drop the `fl_draw_only` force, keep `invalid_setting/too_long`; `engine_node_gather`: on FL own the scale per inserted
  root; `engine_node_append`: FL gate on the frame's `SceneEndSource::Hook`, new counter `node_no_boundary`, hull rows
  after the loop), `src/proxy/engine_nozzle_walk_core.h` (`RootSet` gains a per-slot `scale_bits` on both editions or FL
  only; no walk change), `src/proxy/motion_output.h` (counters), `src/proxy/motion_output_engine_light_inc.h` (the
  `engine_light_frame` append refused on FL when the frame did not resolve from the hook), `engine_nozzle_source`
  schema text (`tools/config`), `docs/architecture/plumes-no-glow-hulls.md` "As built" and this note.
- Owned scale: at `engine_node_gather`, when `RootSet::insert` admits a new root, read camera `+0x1c` then context `+0x2c`
  through `engine_memory::read` (the draw scope: the engine is using that camera for this call, `object_trace_fl.cpp`),
  memoise per camera over the frame's 4 slots, store the float bits in the slot; `engine_node_append` uses the slot's
  value and never calls `engine_node_context_scale` on FL (`node_no_scale` counts a zero). AP may keep its memo or take
  the same path; the AP objects must compare identical if it does not (`ap_object_compare.py`).
- Sites to verify (static, `python3 -I verification/results/fl-nozzle-walk/read_set.py` reusing `fl-far-jets/read_set.py`'s
  `Image`/`Cfg`): root `+0xc` / sentinel init `0x4a22ab..0x4a22b8`; next `+0` at `0x464bc4` and `0x4a33b0`; hidden writer
  `0x100000` in `0x46484b..0x4648d8`; node size `0x270` at `0x4a2203`; C `+0x260` at `0x4646b6`; camera `+0x1c` at
  `0x48ba48`; context `+0x2c` at `0x4def18`; the far read set's 47 anchors re-asserted. The scene-window producer
  (`verification/results/fl-port/nozzle-scene-window/produce.py`, 20 regions) is not in the tree on `main`; recover it from
  `276bd2d62` and rerun, or state that the §1 window argument rests on the committed note.
- Acceptance: `cmake --build build-fl` 0 warnings; `test_engine_nozzle_walk` and `test_engine_plumes` host modules 0
  failing; a new FL `X3M_MOTION_OUTPUT_FIXTURE` case (scene-end source Hook → append walks and uses the owned scale;
  StretchRect/None → `node_no_boundary` counts, zero reads; a root whose camera `+0x1c` is unreadable → `node_no_scale`);
  `read_set.py` PASS on the installed exe; `ap_object_compare.py` identical for `motion_output.cpp` if AP is untouched.
  Flight rows: `engine_nozzle_source … status=ok source=node edition=fl`, `engine_plumes_hidden_emitters … rule=on
  edition=fl`, one `engine_plumes_hidden_emitter_hull … anchors=4` for the M6M, `engine_stage node_roots>0 node_walked>0
  node_unreadable=0 node_no_boundary` ≈ menu frames only, `node_walk_us` p99 under the kill bound; one F8 at the
  shipyard for the offline anchor-origin cross-check against the hull rows.

## 4. Native Windows

Shared code only: `engine_memory::read` (VirtualQuery + copy), the FL scene hook already installed and flown on FL
runs 7–31, the far handler's integer copy. No Wine-specific export or layout. The game runs natively on the user's
host (C:\X3FL), so the flight is the native check; nothing here is verified on Windows before that flight.

## 5. Unknown, and what settles it

- `node+0x1c == camera+0x1c` on the main view [i, reparent copy `0x4a5824/27` + walker argument]: the owned scale at
  the gather reads the camera's own context, so the walk does not depend on it; `far_dropped` ≈ `far_jets` in run 28
  would still say the far route's equality fails.
- FL child-list sizes and the walk's real per-frame cost at 200+ ships [u]: `node_walk_us` and `node_children` in the
  first flight; a `node_children_max` field is cheap if wanted.
- Whether any FL path unlinks a scene node inside the frame routine between submission and `0x48b929` [s/i for the
  named paths, u for computed calls]: `node_unreadable` and the guard counters over one flight; the scene-graph census
  diagnostic before/after the append would settle it offline.
- Docked ships as direct children of a carrier root [u]: the `node_children` count against the offline maximum.

## Ratification (2026-11-03, main session)

Option (b) is ratified: the node walk runs on FL under the synchronous native borrow already ratified for the far route,
at the pre-compositor hook boundary the stage already uses; the context scale is owned at the gather; on FL the append
runs only when the frame's scene end came from the hook. Rollback `engine_nozzle_source = draw`. Flown as two FL steps:
first the walk alone (`engine_plume_emitters = 0` in the FL ini for that run, so jets come from the walk and nothing
else changes), then the hidden-emitter anchors of `plumes-no-glow-hulls.md` step 1 (ini line removed). `fl/engine-effects.md`
§4 ("only for Edition::AP") is corrected by the implementer.
