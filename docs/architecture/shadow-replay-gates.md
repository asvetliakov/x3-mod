# Shadow replay gates: one-cascade depth replay without shading

**Ratified 2026-09-15 (orchestrator):** the one-cascade depth-replay fixture is
not funded yet. The lane-independent caster-candidate counter
(`shadow_replay_candidates` / `shadow_replay_lock_witness`, default off) is
implemented so it rides the next candidate; the four predicates below decide
whether the fixture is funded after that run.

Design note for the second ratified route-B step ([directional-shadows.md](directional-shadows.md)
§4 staged plan, §9 feasibility boundary): after the integrated, default-off sun-lit-share
lane, prove that the proxy can replay the frame's own opaque casters into a sun-space
depth map at scene end, with no shading and no consumer. It depends on nothing in the
linear-material path: the user runs original hull shading, and depth replay reads
geometry and matrices only. Applying the map is a later step with its own contract
([material-investment.md](material-investment.md), sun-shadow row).

## Decision

**Do not fund the depth-replay fixture yet. Fund the small caster-candidate counter
integration on the motion route (lane-independent), take one gameplay run, and decide
from its numbers.** The depth math and the exclusive transaction are the well-understood
part; the unknowns that can change the design are (a) how many routed casters per frame
fall in cascade 0 and satisfy the managed-buffer boundary, (b) whether X3 locks any of
their buffers between the draw and scene end, and (c) which depth-writing populations the
route does not see. §9 already states that serialized synthetic geometry passing proves
nothing about live feasibility; a fixture built before those numbers risks a rebuild if the
managed-only boundary turns out empty. The counter integration is one short bookkeeping
path on lane-on frames and produces a log line whose predicates are listed in §3.

## 1. Remaining gates, as checkable predicates

The hard gate `motion_live_replay_available=false` (`src/proxy/capture.cpp:138`) stays
until every predicate below holds. Each is a check with a source of evidence, not a
manually set flag.

### 1.1 Replay entry coverage

Only one entry may start a replay: the sole outer application root at the scene-end
boundary of the main scene. Today that boundary is reached by two paths, the engine
callsite hook `0x004721b1` → `scene_end_signal` (`capture.cpp:2250`, site contract in
`src/proxy/scene_hook.h`) and `compositor_pre` (`capture.cpp:689`). Both acquire the capture
mutex without an application ticket (§9 item 2). The Clear-side motion boundary, Present,
Reset and any generated method are not replay entries.

| Predicate | Evidence that exists | Missing |
| --- | --- | --- |
| E1: `scene_end_signal` and `compositor_pre` take an outer `ApplicationAdmission` before the capture mutex, and `ReplayAdmission` is declared after it (`src/ownership/application_admission.h`: sole root, no nesting, no waiting roots, no veto). | Monitor core, LIFO scopes, nonblocking promotion, permanent vetoes exist and are host-tested; VB/IB Lock/Unlock wrappers enter it before native dispatch (§9). | Neither boundary path has a ticket; none of the 30 capture hooks or 11 loader exports enters before `HookGuard` ([motion-replay-exclusion.md](motion-replay-exclusion.md) "Concrete portable entry coverage"). |
| E2: all 297 generated methods, the 6 D3DX graphics hooks and 24 mesh thunks carry admission from entry through output adoption; Release ends its child ticket before the parent capture dispatch. | Generator is the inventory; forwarder Lock/Unlock already covered. | Inventory is "a patch boundary, not implemented coverage". |
| E3: permanent veto latched before native AddRef on all seven `SetPrivateData(D3DSPD_IUNKNOWN)` routes, on unknown QI success, Ex/9On12 fallback, late adoption, external/shared resources. | `Direct3DCreate9Ex/9On12[Ex]` veto and log `unproxied=1` ([platform-portability.md](platform-portability.md)); sidecar authentication accepts only the mod's own counter sidecar. | The seven-route latch and second-device case are unimplemented; the replay-native-callbacks note gives the concrete reentry chain (buffer decref `0x1a9e0` → cleanup `0x3410` → application COM slot 2 at `0x345a`). |
| E4: outer window-chain admission installed on focus/device windows before the device is returned. | WndProc `0x4d3620` audited: deactivation and audio branches call application callback tables, so it is not callback-free. | No window wrapper exists. |
| E5: every mod-owned raw-native helper (RESZ depth copy, lease inspection, finite evidence, bookend snapshot) is inventoried and carries either application admission or a narrow replay token. | Bookend snapshot is registry-only, no native call; lease inspection API exists. | Token plumbing and the RESZ inventory are unimplemented; the depth replay in §2 avoids RESZ entirely. |

Evidence from the lane and the lock observation adds nothing to E1–E5 by itself: the lane
runs inside the existing capture bracket and the bookends observe Lock/Unlock without
excluding them. Their value is for the window and concurrency predicates below.

### 1.2 Replay window

Where: inside the existing scene-end bracket, after `scene_end_hook` finishes lazy
bindings and composition and before AO/TAA, before Present (§9 item 2). All main-scene
depth writers precede that callsite by construction (the engine's compositing call is
the hook site), so the map covers the frame's own casters and no application depth
writer runs after the replay. Nothing is replayed at Clear or from the previous frame.

| Predicate | Source |
| --- | --- |
| W1: the frame has a valid `CameraState` latch (`src/renderer/camera_reprojection.h`: orthonormal `r`, `t`, `m00`, `m11`) for the main scene epoch. | Route already validates it per frame; a failed latch refuses the producer. |
| W2: the frame has a sun direction from a reviewed source. | **Unknown today.** §7 leaves two routes: the per-pair PS-constant column (CTAB, offline) or the light record `+0xb0/b4/b8` read in the Clear hook ([camera-and-lights.md](../reverse-engineering/camera-and-lights.md) notes the records are mutated during submission). One capture check of the first routed draw's constant against the run-39 world fit settles which is used; the counter run in §3 does not need it. |
| W3: candidate set = routed (`RigidDrawKey`) indexed opaque draws of the scene epoch whose VB and IB are readable MANAGED, non-DYNAMIC, known revision zero-established, not pending, not in flight; glass, cutout, fade arm, UP, instancing excluded (§9 selected minimum). | `RigidDrawKey` fields (`src/renderer/motion_history.h:21`), `get_buffer_content_view`, `get_buffer_lock_view`. |
| W4: for every admitted record, the bookend view at scene end equals the view at capture in `attempt_serial`, `revision`, `pending_locks`, `in_flight_locks`, `in_flight_unlocks`, and `quiet()` is true (`src/ownership/buffer_lock_observation.h`). A READONLY Lock after capture also refuses (serial changed). | Bookends committed as `4a708af`; no per-draw caller yet. |
| W5: caster coverage: the share of depth-writing scene draws in cascade 0 that are candidates is known and above a stated floor before the map is called anything but diagnostic. | Run60 refused all 8,950 lane frames with 3–99 untracked writers per frame (`verification/results/run60-observations.json`, `sun_lane`). Those are refused *receivers* (color writers after a receiver); they do not veto a depth-only replay, but the merged `sun_shadow_lane_writer` signatures (vs, ps, declaration, stride, z, zwrite, gate) name populations the route does not see: `unregistered`, `pair`, `rows`, `geometry` writers with z-write on are missing casters; `no_zwrite`/`blended` are not casters. |

What the run60 populations imply once the buckets land: if the writers with `zwrite=1`
are mostly `unregistered`/`pair` (programs outside the registry), the cascade-0 map will
have holes from real hull geometry and the answer is registry coverage, not replay work.
If they are `no_zwrite`/`blended` (HUD, effects, glass), the caster set is intact and
the receiver veto is a shading-side problem only. This is why the writer lines matter
for replay even though the lane applies nothing.

### 1.3 Concurrency

| Predicate | What can be promised | What cannot |
| --- | --- | --- |
| C1: promotion succeeds only with `active_roots==1`, `waiting_roots==0`, no nested boundary, no veto, `replay_active==false`; source capture and submission are bracketed by admission snapshots with unchanged `admitted_roots` (§9 item 1). | The monitor core does this atomically and holds its mutex only for transitions. | Nothing about unwrapped callers. |
| C2: inside the segment no call can reach application code: all original and injected resources are retained (no final Release), no allocation, no `DrawPrimitiveUP`, no stateblock recapture of a populated block, no Present/Reset/query wait/message pump/diagnostic callback. Same-thread application entry during replay is an invariant failure and permanently vetoes. | The demonstrated reentry chain is final-release driven; retention removes it. | Windows/Preview driver-internal callbacks are not proven absent; the finite acceptance list in motion-replay-exclusion bounds the obligation to concrete controls. |
| C3: cold native thread. A Lock or setter from another thread after promotion must wait at entry and never dispatch natively early; a thread already inside native Lock when promotion is attempted refuses promotion (`in_flight_locks>0`) rather than being waited on. | Run60's loading thread record (`loading.thread`: `tid 220`, `presenting_tid_match true`, 11,867/11,867 records) and device flags `0x40` (no `D3DCREATE_MULTITHREADED`, [native-windows-audit](native-windows-audit-2026-09-12.md)) say X3 renders from one thread in the observed window; that is evidence, not a contract. | Writes through an already returned Lock pointer are invisible to any mutex; hence `pending_locks>0` refuses. Window threads waiting at entry must never be waited on or messaged by replay. |
| C4: a Reset attempt in flight refuses; a Reset after promotion waits at entry until restore and token release; the bookend generation advances on every attempt, so a lease never survives Reset or the frame. | Implemented in the bookends and lease tables (4096/frame ceilings). | — |

A portable critical section (`std::mutex` plus the monitor) promises exactly this on
Windows and Preview: no *wrapped* entry dispatches natively while replay is exclusive,
and replay refuses when it cannot see everyone. It cannot stop unwrapped native pointers
(veto on escape), mapped writes (refuse on pending), or runtime-internal threads. The
native D3D9 runtime without `MULTITHREADED` does not serialize application threads either,
so the monitor is stricter than the runtime; the Wine-side mutex facts in
[replay-native-callbacks.md](../reverse-engineering/replay-native-callbacks.md) are not
relied on.

## 2. Minimal one-cascade depth-only replay

**Casters.** All routed opaque depth writers whose fade-route AABB (model-local box ×
node scale × submitted rows) meets cascade 0, the own-ship slice 6–250 units; not the
sun-share receivers. Receivers are a shading concept and require the linear lane, which
the user's configuration disables. Expected 5–20 draws per frame (§4); cap the record
array at 64 and report overflow.

**Source capture without copying.** Per candidate, at the original draw and only after
it succeeds: the `RigidDrawKey` fields already read (VB/IB allocation ids, declaration,
stream offset/stride, topology, base vertex, ranges, index format), the unjittered
`c24–27` rows (the route restores jitter after the draw, so the pre-jitter rows are the
retained ones), cull mode, the geometry lease (native AddRef on VB, IB, declaration; the
existing lease ceilings), the bookend view of VB and IB, and the admission snapshot. No
byte copy, hash or Lock of application buffers; DISCARD/NOOVERWRITE/DYNAMIC buffers are
rejected and counted. Roughly 150 bytes per record in a preallocated array.

**Target and projection.** One mod-owned `R32F` 1024² render target plus one `D24X8`
1024² depth-stencil (8 MiB), created outside exclusivity, qualified by
`CheckDeviceFormat`/`CheckDepthStencilMatch`, rebuilt on Reset. VS: `clip = rows·pos`;
`p_view = (clip.x/m00, clip.y/m11, clip.w)` (exact, linear, valid behind the camera);
`world = V⁻¹·p_view` from the `CameraState` (orthonormal, so `V⁻¹` is `Rᵀ`, `−Rᵀt`);
`out = SunProj₀·SunView·world`. `SunView` looks along the world sun direction (W2) with a
fixed up vector; `SunProj₀` is orthographic, centered on the camera position plus
forward × 128 units, half-extent 250 in x/y, z ±512 in sun space, texel-snapped in sun
space (the sun is world-fixed, so snapping removes camera-translation swim). PS writes
sun-space `z/w` to `.r`; the hardware depth attachment resolves occlusion. Keep the
application's cull mode per record; bias is an apply-time concern. No RESZ, no sampled
depth FOURCC, no application shader.

**Transaction.** §9 items 2–5 as written: promote on the sole root, revalidate every
record (owner/reset generation, bookend view equality, lease), capture a fresh stateblock
plus explicit RT/DS/viewport references, bind, Clear, replay with the authored pair
(`SetStreamSource`, `SetIndices`, `SetVertexDeclaration`, one `SetVertexShaderConstantF`,
`DrawIndexedPrimitive`), restore fully, release the token, then retire state and leases.
Any refusal refuses the whole producer for the frame; the map is never published as
valid on a partial or failed restore.

**Cost model.** Off: zero. On, hot path: per candidate two short admission snapshots,
three native AddRefs, one registry snapshot; no allocation. Scene end: fixed ≈ 10 native
calls (stateblock capture/apply, two `SetRenderTarget`, `SetDepthStencilSurface`,
viewport, Clear, shaders) plus 5 calls per replayed draw at the measured 0.3–0.45 µs per
call (fade bracket: 350 calls in 100–160 µs), i.e. ≈ 2 µs per draw: 10–40 µs at 5–20
draws, ≤ 130 µs at the 64 cap, plus GPU vertex work × 1 for those draws. Well under the
§4 three-cascade line (0.7–1.1 ms at p50) because cascade 0 is a bounded list; the
unmeasured part is stateblock capture/apply on this backend, which the fixture times.

**Fixture that proves it without shading.** Extend the existing component-fixture
pattern (serialized wrapper transaction, real route seam, as `motion_output_fixture`):
draw two synthetic managed meshes (a box and a tetrahedron) at known world transforms
with a known camera and a fixed sun direction; read back the `R32F` map; compare against
a CPU projection of the same vertices through the same `V⁻¹`, `SunView`, `SunProj₀`
(per-texel min depth over covered triangles; tolerance 1e-4 in normalized depth and one
texel at edges; require covered-texel count within 2 % of the CPU rasterization).
Cases: clean replay; Reset before promotion (generation refuses, targets rebuilt, next
frame replays); READONLY Lock between draw and scene end (serial changed → refuse);
DISCARD Lock (refuse, counted); Lock held across the boundary (pending → refuse);
in-flight Lock stopped before native return on another thread (in_flight → refuse, no
early dispatch, no wait); nested boundary (refuse); another root waiting (refuse); restore
failure injected (safety response, no valid map); 64-record overflow. Every case checks
HRESULT/output/x87/MXCSR/LastError preservation and full state restoration.

## 3. What the next user run must measure

The merged lane grammar (`sun_shadow_lane_refusals`, `sun_shadow_lane_writer`; ledger
[directional-shadows.md](../verification/directional-shadows.md)) only appears with
`--sun-shadow-lane`, which requires linear materials. For the user's original-shading
configuration the run needs either a temporary linear-on diagnostic session, or the
lane-independent counter below. Recommend both in one run: the writer lines answer W5,
the counter answers W3/W4/C3.

Counter integration to request (small, motion-route only, no lane, no replay draw), once
per frame at scene end, only when `X3M_SHADOW_REPLAY_COUNT=1`:

`shadow_replay_candidates device=%llu frame=%llu routed=%u zwrite=%u slice0=%u managed=%u
dynamic=%u default_pool=%u excluded=%u unknown=%u leased=%u serial_changed=%u
readonly_after=%u writable_after=%u pending=%u in_flight=%u quiet=%u cold_thread=%u
roots=%llu waiting=%llu nested=%u overflow=%u`

plus at most 16 witness lines per device, `shadow_replay_lock_witness device= frame=
allocation=%llu flags=%08x offset=%u size=%u thread=%u serial_delta=%llu revision_delta=%llu`,
for the first candidates whose view changed between draw and scene end.

Decision predicates on that run (each answerable from the analysis script, not by reading
the 13 MB log): `slice0` p50 ≥ 5 and `managed == slice0` on ≥ 95 % of frames (else the
managed-only boundary is empty and a copy-based snapshot design is needed first);
`quiet == leased` on ≥ 99 % of frames and `writable_after == 0` over the run (else §9's
lease contract cannot admit these buffers); `cold_thread == 0` (all Lock `last_thread`
equal the presenting thread; a nonzero count means C3 must be designed for real, not
just refused); `waiting == 0 && nested == 0` at every scene end (else E1 promotion would
never succeed). From the writer lines: the count of `zwrite=1` signatures in
`unregistered`/`pair`/`rows`/`geometry` versus `no_zwrite`/`blended` decides W5.

### Implemented (2026-09-15)

Option `--shadow-replay-candidates` (`X3M_SHADOW_REPLAY_CANDIDATES=1`; default off),
requiring `--motion-output --ownership` only: no TAA, HDR, linear-material or lane
prerequisite, so it rides the user's original-hull-shading configuration. The launcher
refuses the option without both prerequisites; the DLL logs
`shadow_replay_candidates_mode requested=1 enabled=<0|1> motion_output= ownership=` once
at device creation. Source: `src/proxy/shadow_replay_candidates.h` (bookkeeping core,
CPU only), the route sites in `src/proxy/motion_output.cpp` (`note_candidate_distance`,
`note_candidate_draw`, `publish_shadow_replay_candidates`, `candidate_pool_of`), the
switch in `src/proxy/capture.cpp` and, in `src/proxy/loader.cpp`, the ownership option
`track_buffer_lock_attempts` (with `track_buffer_writes`) that the committed bookends
(`src/ownership/buffer_lock_observation.h`, `4a708af`) needed to be connected.

Grammar, once per frame at the scene end (engine hook or bloom copy; a frame without a
scene end logs nothing):

```
shadow_replay_candidates device=%llu frame=%llu routed=%u zwrite=%u slice0=%u managed=%u dynamic=%u default_pool=%u excluded=%u unknown=%u shadow_mismatch=%u leased=%u serial_changed=%u readonly_after=%u writable_after=%u pending=%u in_flight=%u quiet=%u cold_thread=%u stale=%u roots=%llu waiting=%llu nested=%u overflow=%u
shadow_replay_lock_witness device=%llu frame=%llu allocation=%llu flags=%08x offset=%u size=%u thread=%u serial_delta=%llu revision_delta=%llu
```

Field definitions (all per frame, from the route's own draw record; identities
`routed ≥ zwrite ≥ slice0 = managed + dynamic + default_pool + excluded + unknown + shadow_mismatch`,
`leased + overflow ≤ managed`, `quiet + stale ≤ leased`, every other bookend bucket
`≤ leased − stale`). The frame line is emitted at most once per frame (a frame serial
guards the hook and bloom-copy sites, so a second qualifying copy never emits a second
all-zero line):

- `routed`: routed draws whose native draw succeeded. `zwrite`: those not on the fade-band
  arm (gate 4 requires `ZENABLE=1 ZWRITEENABLE=1` for every other routed draw).
- `slice0`: z-writing routed draws whose rows' origin distance
  (`fade_route::origin_distance`, the fade route's existing helper on the frame's
  `CameraState` latch) lies in the own-ship slice 6–250 units. **Assumption:** the note's
  §2 casters are defined by the fade-route AABB meeting cascade 0; the counter uses the
  origin distance because the AABB is only known for fade-table meshes and the origin
  test costs nothing. A frame without a valid camera latch counts `slice0=0` (W1).
- `excluded`: slice-0 draws on the alpha-test (cutout) arm (W3). `managed`: both buffers
  (VB, and IB when indexed) `D3DPOOL_MANAGED` without `D3DUSAGE_DYNAMIC`; `dynamic`: any
  `D3DUSAGE_DYNAMIC` buffer; `default_pool`: any other non-managed pool; `unknown`: pool
  not known; `shadow_mismatch`: the shadowed `SetStreamSource`/`SetIndices` allocation
  ids differ from the route key's, so neither the pool class nor the bookends are
  attributed and the draw is not recorded. The route has no pool/usage shadow: the class comes from one documented
  `GetDesc` of the application's own live `SetStreamSource`/`SetIndices` argument inside
  that setter hook (`CpuCallBoundary`, LastError kept), cached per allocation id in a
  128-entry direct-mapped table, so a repeated binding costs a table probe.
- `leased`: managed candidates whose buffer-lock views were `known` at the draw and were
  recorded (64 records per frame; `overflow` counts managed candidates beyond them).
  `managed − leased − overflow` is the count with an unknown bookend.
- At the scene end each record's views are read again: `serial_changed` (attempt serial
  moved on any buffer), `readonly_after` (moved with only READONLY attempts),
  `writable_after` (writable attempts moved), `pending` (a Lock still open),
  `in_flight` (a Lock/Unlock inside native), `quiet` (every buffer `quiet()` with
  unchanged attempt/unlock serial and revision), `cold_thread` (a buffer whose last
  Lock thread is not the thread at the scene end). Before any comparison the scene-end
  view's `allocation_id` and `BufferLockView::generation` must equal the draw-time
  ones; otherwise the record is `stale` (identity reused by a new wrapper, or a Reset
  in between) and is neither compared nor witnessed. A record whose wrapper is no
  longer registered is not quiet and yields no witness.
- `roots`/`waiting`: the admission monitor snapshot when `X3M_ADMISSION=1`, else 0.
  `nested`: scene-end signals this frame that found no open boundary (the observable
  C1 refusal; the capture-side second-signal counter).
- Witness lines: the first 16 changed buffers per device (attempt serial or revision
  moved between draw and scene end), with the last Lock's flags/offset/size/thread.

Cost: off, nothing runs (every site tests one bool). On: per routed draw one
`origin_distance` evaluation and integer classification; per managed slice-0 candidate
two registry snapshots (`get_buffer_lock_view`, registry mutex, no native call); at
the scene end the same snapshots again for at most 64 records, one admission snapshot,
one log line and at most 16 witness lines per device. No allocation, no device calls;
the only added native calls are the cached `GetDesc` in the two setter hooks.

Analysis: `tools/analysis/shadow_replay_candidates.py <session log>` streams the log,
refuses malformed lines and broken identities, enforces the witness cap and prints the
four predicates (`managed_boundary`, `lease_contract`, `single_thread`,
`promotion_possible`) with the numbers behind them. Host tests:
`verification/analysis/test_shadow_replay_candidates.py`; live case
`seam-ownership-taa-camera-candidates-on` in `run_motion_output.py` (the seam DLL lowers
the slice near bound through `X3M_FIXTURE_SLICE_NEAR` for the fixture's unit-distance
triangles; production keeps 6). Evidence: [directional-shadows.md](../verification/directional-shadows.md).

## 4. Non-goals and recommendation

Non-goals: shadow application or any consumer of the map; cascades 1–2 and culling;
receiver contracts or the legacy-shading sun response; removing the hard live gate;
sampling last frame's map; scene-graph traversal for engine-culled casters; RESZ or
vendor depth formats; any Wine-private synchronization; native Windows runtime evidence
(source is documented D3D9 and portable C++; R32F render targets, `D24X8` matching,
stateblock semantics and the single-thread assumption remain unverified natively and go
in [platform-portability.md](platform-portability.md) when implemented).

Recommendation, restated: fund the §3 counter integration now (it reuses committed
bookends and the route's existing draw record, adds no per-draw allocation, and is off
by default), request one run, and fund the §2 fixture only when the four predicates pass.
If `managed` is near zero or `writable_after` is nonzero, return to design for an
immutable copy boundary before any fixture. Independently of these numbers, E1–E5 remain
the activation gates and are the larger cost; the counter run tells whether that cost is
worth paying for route B at all.

Alternatives considered: (a) build the analytic depth fixture first, in parallel with the
run: rejected because it cannot fail on the questions that matter (populations, Lock
interval, thread) and would be rebuilt if the managed boundary is empty; (b) replay
sun-share receivers only: rejected because receivers require the linear lane the user
has off and are the shading side, not the caster side. (c) previous-frame map sampled in
the material shaders: rejected in §4 (one-frame lag, 30 slots in 168 pairs, needs the lane).

Unknowns and what settles them: sun-direction source (one capture check per §7); the
distribution of X3 mesh buffer pool/usage and the Lock interval (the §3 run); stateblock
capture/apply cost on this backend (the fixture timing); native Windows behavior (no
instrument available; tracked as a gap).
