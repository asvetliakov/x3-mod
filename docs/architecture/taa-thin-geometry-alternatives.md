# TAA thin geometry: fewer full-screen passes on D3D9

Question: can the thin-geometry handling (the fragmented-region mask, its dilations, the 7x7 box and the region
arithmetic in the resolve) be redesigned so that it needs fewer or no extra full-screen passes on Shader Model 3
(512 slots, no compute, no shared memory, one wined3d device), while keeping the accepted look: no lattice crawl at
rest, bounded trails under motion, no return of the distant-station pan flicker (Run 62)?

Inputs: `src/temporal/README.md`, [taa-lattice-crawl.md](taa-lattice-crawl.md) sections 13-15 and 32,
[taa-high-resolution.md](taa-high-resolution.md) (cost model, S1-S5), the shaders `line_mask_ps.hlsl`,
`thin_box*_ps.hlsl`, `resolve.hlsl`, the pass order in `src/renderer/temporal_pass.cpp`, the Run 280 split
([gpu-sync-timing.md](../verification/gpu-sync-timing.md)) and the mask bench
([engine-frame-time.md, "Mask draws"](engine-frame-time.md)). For option B the upload path in
[mesh-buffer-rewrite.md](../reverse-engineering/mesh-buffer-rewrite.md), the node fields in
[render-node-bounds.md](../reverse-engineering/render-node-bounds.md), the ownership layer (`src/ownership/`) and the
per-draw ABI upload (`src/proxy/motion_output.cpp:5523`, `src/renderer/material_motion.h`). Arithmetic:
`verification/results/taa-thin-geometry-alternatives/cost_model.py` (output beside it). **[M]** measured in the
cited ledgers, **[I]** inferred here; nothing in this note was flown or run under Wine.

## 1. Decision

**Superseded in part 2026-09-24:** the 512-slot premise fell (`platform-portability.md` "Shader slot budget"); the
sequence and the slot gating of A' are re-planned in [taa-plan-lifted-slot-cap.md](taa-plan-lifted-slot-cap.md)
(ratified). The pass analysis, the look constraints and option B stand.

**Recommendation.** Keep the full-resolution tests draw and replace the two dilation draws by temporal propagation of
the region flag and of the gate closure through the age target inside the resolve (option A', section 3.1), after S3
(the 5-tap history) has freed the slots it needs; take S4 (half-resolution box) as the orthogonal cut; add the
per-draw thickness flag of option B as a zero-cost vote into the tests draw once its upload-time statistic exists,
not as a replacement for the per-pixel test. The stage goes from six full-screen draws to four (tests, box rows,
box columns, resolve); "none" is not reachable: 1-px struts need a full-resolution depth test or a per-pixel flag,
and the unrouted far stations (the pan-flicker fix) have no draw-time identity at all.

Ranking, busy 1080p frame net of floors (stage 4.78 ms **[M]**), 5120x1440 scaled by the measured 2.75x and the
linear 3.56x (all **[I]**, `cost_model.py`):

| option | 1080p saving, ms | 5120x1440, ms | look risk | implementation risk |
| --- | ---: | ---: | --- | --- |
| A' temporal propagation, drop mask x and y | 0.28-0.67 | 0.8-2.4 | medium: region reach becomes 3 px + frames instead of 8 px | medium: needs S3 first (slots), age target gains a lane, new oracle |
| G mask chain every other frame | 0.32-0.84 | 0.9-3.0 | high under pans (one-frame-stale gate and region) | low (pass-order only) |
| C = S4 half-res box | 0.40-0.50 | 1.1-1.8 | low (superset box) | low |
| C = S5 half-res dilations | 0.10 | 0.3-0.4 | low | low; moot beside A' |
| E box columns folded into the resolve | 0.05-0.12 | 0.1-0.4 | none | slot-bound; competes with A' for S3's slots |
| B per-draw flag (as a vote) | 0.00-0.04 | 0.0-0.1 | improves coverage on hull-backed lattices (unmeasured) | medium: upload observer, transformer, lane-only |
| B as the only classifier (drops the tests draw) | up to 0.35 more | up to 1.2 more | high: loses unrouted stations and any unflagged draw | high |
| D coverage re-render of tagged draws | -0.1 to -0.3 (a cost) | -0.4 to -0.8 | n/a | closed: no geometry stage, per-draw CPU on wined3d |

A' + S4 together: 0.7-1.2 ms of the 4.78 ms busy 1080p stage, 1.9-4.2 ms at 5120x1440 **[I]**.

**Ratified 2026-09-24 (main session):** A' plus S4 in that order after S3, with option B's per-draw thickness
flag added as a vote in the tests draw (not a replacement), the offline table not pursued; D and G recorded as losing;
E deferred behind A'. Sequence: S3 first (its slot yield gates A'), the Run 77 C flight attributes the busy-sector mask
excess with the three-draw split, then A' with the age-target hold, then S4, then B's statistic in the CloneMesh
Unlock observer with the one-line census of 0x440 clones and INDEX32 meshes. Fixture: the temporal pass lattice mode
and thin_region_cases oracle with new references, plus the run161 uncovered-background case for the closure hold.

## 2. What each pass buys for the accepted look

| pass (Run 280 net, busy still **[M]**) | computes | accepted behaviour that depends on it | replaceable by |
| --- | --- | --- | --- |
| mask tests, full res (bench 0.27-0.36 per draw **[M]**) | FRAGMENTED flag from four 7-tap depth lines (28 R32F taps, stop at the first fragmented line); the pixel's screen and camera-relative openness; far weight; emissive vote; sentinel class code | the crawl fix itself: a 1-px strut is seen only by a per-pixel test at full resolution ("tests draw at half resolution: loses 1-px struts", high-res note) | a per-draw flag for routed modelled struts (B); nothing for unrouted sentinel pixels |
| mask x, mask y + composition (bench 0.135 + 0.11 **[M]**) | 11x11 maximum of the flag (the region), 17x17 minimum of both openness values (the gate closed by the fastest pixel within 8 px), sentinel term `S * min17(camera openness)` | at rest: "region present in every phase on 0.63 of the hot px, in any phase 1.000" (section 13 **[M]**), grow 5 took the arm from 3.20 to 2.34 rms; under motion: run161's 57-code trail is what the 17x17 minimum removes; a routed mover closes the stabiliser within 8 px of itself | temporal OR of the flag and temporal minimum of the openness along reprojection (A') |
| box rows + columns (0.92 **[M]**) | 7x7 min/max of the current colour where `b > a`, emitter bound over sky | ghost bound where only the camera term opens the region (74 vs 237 codes on the stale patch, section 32 **[M]**); the sentinel stabiliser's bound (Run 62) | S4 half resolution; folding into the resolve after S3 (E) |
| resolve `far_camera` (1.82 still / 2.24 moving **[M]**, 505-508 of 512 slots) | the blend with clip-off on the region, weights 0.97 / 0.985, age count | everything | not replaceable; the constraint every option runs into |

Two facts drive the ranking. First, the tests draw is cheap and bandwidth-bound (0.27-0.36 ms at 1080p; its 28
depth taps cost at most 0.03 **[M]**), while the dilations are tap-issue-bound and the in-game `taa_mask` (1.87 ms)
carries 0.9 ms the bench does not attribute to any draw. Second, the resolve has no slots: any option that moves work
into it is gated on S3 (5-tap bilinear Catmull-Rom, expected 30-50 slots back, unmeasured **[I]**).

## 3. Options

### 3.1 A: classification in the resolve, region grown over frames instead of pixels

As briefed (the tests inside the resolve): not feasible. The four 7-tap lines with the class test and the two
openness values are of the order of a third of `line_mask_camera`'s 427 slots (**[I]**: the program is not split per
mode, so no per-mode count exists), and the resolve is at 505-508. Even after S3 the resolve cannot take them, and the 28 taps would walk the R32F depth the resolve
already reads 9 times (bandwidth: 0.03 ms, harmless) but with the loop control the resolve cannot afford. So the
tests draw stays.

**A' (recommended): keep the tests draw, drop the x and y draws, propagate temporally.** The tests draw's output is
already the per-pixel material the composition consumes: r = screen openness, g = far weight, b = flag (with the
sentinel class code), a = camera openness. The resolve reads that target at s8 directly (4 B, the same fetch as
today's final mask) and carries two hold counters through the age target, read at the same reprojected texel the
age read already fetches (`previousAge` in `resolve.hlsl`, one fetch):

- region: `hold_r = flag ? 8 : max(hold_r_prev - 1, 0)`; the region is `hold_r > 0`. 8 is the jitter period
  (`X3M_MOTION_JITTER_SAMPLES` default 8), so a pixel fragmented in any phase of the cycle stays in the region for
  the whole cycle: exactly the "any phase 1.000" coverage that grow 5 approximated spatially (section 13 **[M]**),
  and the reason the residual rms at rest should not be worse than the 11x11 region's 2.34 (**[I]**, to be replayed).
- closure: `open = min(own openness, carried openness)`, `carried = max(open_prev - 1/4, 0)` (a 4-frame hold), for
  the screen and the camera gate separately (two 4-bit fields). The uncovered-background case that broke the
  per-pixel gate (run161: "the background pixel a moving edge has just uncovered carries the background's speed")
  is covered because that pixel's reprojected texel was the moving edge on the previous frame, whose openness was 0:
  the closure arrives from the history, not from a spatial window, and it does so at any speed, whereas the 17x17
  window stops covering an edge above 8 px/frame.
- sentinel stabiliser: the resolve already reads the centre depth and motion alpha, so the class "unrouted sentinel
  pixel" is one compare; its strength `S * carried camera openness` replaces `S * min17(camera openness)`.

Encoding: the age target is R32F with counts 1..64 and a sign; the three counters (4 + 4 + 4 bits) go either into a
second lane (`G32R32F` age, +4 B write and +4 B read per pixel, about 0.05 ms at 1080p **[I]**) or into the fraction
of the R32F count (exact in FP32, two `frc` slots to keep `age <= 64` and `min(age + 1, 64)` on integers). The
two-lane form is the simpler contract; the fixture's age readback changes format either way.

Slots: about 15-25 on `far_camera` (**[I]**: two fetches replaced by one, three field extractions, three
`max`/`min`); available only after S3. Cost: the two dilation draws (bench 0.245 + 2 x 0.04 fixed at 1080p; up to
0.67 if the unattributed busy excess is proportional over the three draws) minus the resolve's extra ALU and lane:
0.28-0.67 ms at 1080p, 0.8-2.4 at 5120x1440 **[I]**. Unique bytes per pixel fall from 228 to 212.

Look risk, by accepted behaviour:

- crawl at rest: the region is now the tested pixels (the 7-tap lines already reach 3 px) held over the cycle; the
  spatial margin beyond 3 px goes. Section 13 measured the undilated test at 0.47 of the hot pixels per phase and
  1.000 over any phase; the temporal OR gives the any-phase set at every phase (**[I]**). Where a hot pixel is never
  tested in any phase (more than 3 px from a class change), the 11x11 region covered it and A' does not; section 13
  attributes the residual after grow 3 to phase toggling, not to reach, so this should be small; the replay decides.
- trails: the hold is 4 frames; a trail can only form where the gate is open, and a pixel is closed from the frame
  after the mover covered it. The 4-frame reopen after the mover leaves is a bound the fixture's motion-start case
  checks today (uncovered background at most 0.04 above the installed resolve from frame 8).
- pan flicker: the stabiliser keeps its box and its weight; only the closure's shape changes (temporal instead of
  8 px). A ship crossing the sky closes the pixels it covers one frame late instead of 8 px early; those pixels
  are routed while covered and take the ordinary resolve, so the late closure affects the one-frame silhouette
  band only, bounded by the 7x7 box. (**[I]**; fixture: the bright independent mover case.)
- popping shards: section 13 found the 3-px closure "leaking at popping shards"; a shard that appears has no
  history to carry a closure. The disocclusion proof still rejects history in front; what leaked was the 0.97
  blend fading the shard in over the region. A' keeps the same exposure as today's region at the shard's own
  pixels (they are in the region either way) and loses only the 8-px halo. Fixture case exists (lattice mode).

Verification (no flight needed to decide): (1) `tools/analysis/taa_lattice_gate_replay.py` with the propagation rule
patched into the oracle over the same eight bursts as section 32: run177 stationary rms must stay at or below 2.34
/ 22 / 0 (the 13.1 ceilings), run161 and run148 background trails p99 within 0.1 codes of the installed rule, run177
rotation and run209 forward unchanged within 0.02 in rms and gradient; (2) `run_temporal_pass.py` lattice mode:
`thin_region_cases` with the CPU mask oracle extended by the two temporal rules (region hold, closure hold), the
21 thin-region cases as bounds (shard ripple x 0.086-0.095, square bit-identical, motion start 0.04, popping shards,
bright mover), the sentinel `facets` oracle unchanged, plus a new exact identity: with the hold counters forced to 0
the output equals the installed screen-gate program on the undilated mask; (3) one flight at the run175 stand, a
pan, and the moving truss, with `--gpu-sync-timing` to read `taa_mask` at the tests draw alone.

### 3.2 B: tag thin draws at draw time, classification inside the DLL

The proxy already transforms every reviewed material pixel shader (`material_motion.cpp`), uploads two pixel
constants per routed draw (`c216` = 1/size, 0, 0; `c217` = mode, widen scales, fade gain: `motion_output.cpp:5523`),
sees every VB/IB creation, Lock and Unlock through the ownership wrappers (`src/ownership/d3d9_classes_inc.h`), and
reads the render node and mesh-part descriptor per draw (`object-identity.md`; the small-parts cull site reads the
engine's `s = r * 640 / D`, computed from the node radius at `+0xa0`, and the proxy publishes its pixel threshold from
the live `m00` and back-buffer width). RT2 `.a` carries the clip w twice (`current_depth_ps.hlsl`), no
reader (high-res note section 6).

**Measurement at upload (recommended form).** Every drawn subgroup buffer is written exactly once, inside
`0x004bb470` per LOD record (mesh-buffer-rewrite.md sections 1-3 **[M]**): `D3DXCreateMesh`, the game's own
`LockVertexBuffer`/`UnlockVertexBuffer` fill from its int16 array (x 2^-14), the index fill, tangents, then
`CleanMesh`/`OptimizeInplace` (D3DX reorders and splits, never alters positions) and `CloneMesh` into fresh VB/IB
created through the wrapped device, filled by D3DX through the public `Lock(0,0,&p,0x800)`/`Unlock` of those wrapped
buffers, published to the subset record (`+0x0c` VB, `+0x10` IB) only after success. So the measurement runs inside
the proxy's wrapped `Unlock`, in one of two places:

- the CloneMesh destination pair (the drawn buffers): the qualified observer (section 31, 421 checks **[M]**,
  default-off, no game hook installed; the callsite adapter was removed 2026-09-22) stages exactly these bytes
  before publication. For a statistic no exact-byte claim is needed, only initialized geometry, which the
  successful-clone condition gives. This is the principled seam and keys the statistic to the final allocation
  with no linkage step.
- the initial fill pair (`0x004bc5ea`, `0x004bc1ac`): the `capture_finite_positions` path already copies positions at
  this Unlock; the statistic is invariant under the later permutation/duplication, but must be carried to the clone
  through the CloneMesh scope (source READONLY locks on this pair, destination Unlocks on the new pair). More
  bookkeeping, no benefit; not selected.

D3DX: the game uploads through D3DX, and D3DX dispatches the exact wrapper interfaces the wrapped device returned
(mesh-buffer-rewrite.md, "Linkage and ordering" **[M]**), so the locked buffers are visible to the proxy without any
D3DX-internal access. Readable backing is a creation-time decision (`portable_managed_upload`: WRITEONLY removed on
eligible MANAGED buffers); it must be on from `wrap_factory`, i.e. from `Direct3DCreate9`. Nothing can be uploaded
before that (the proxy is the d3d9 module), so "meshes uploaded before the hooks are armed" reduces to buffers
created while the option was off or that the policy excludes (DEFAULT, DYNAMIC, shared, clone option `0x440`, which
is not MANAGED; INDEX32 meshes above 65,535 vertices, whose D3DX path is unqualified): they get no statistic and
stay unflagged, i.e. they keep the per-pixel path. A Reset does not touch MANAGED buffers or the allocation ids the
statistic is keyed by; teardown/rebuild produces new allocations (section 3 of the RE note).

**The statistic.** Per material group (one VB/IB pair = one subset = one draw), over its triangles: the triangle
height `h = 2 * area / longest edge` in object units (for the long thin quads a modelled strut is made of, `h` is the
strut's width; for a panel it is the panel's size), binned into 8 log2 bins. Stored as 8 counts per subset record
(32 B) beside the allocation metadata the ownership layer already owns. Cost: three FLOAT16_4 or FLOAT4 position
reads, two cross products and three edge lengths per triangle, about 60 flops: 275k triangles for the vanilla bodies
is about 17 Mflop, 10-30 ms over a session under FEX (**[I]**), incurred lazily at each body's first build (at most
about 10k triangles, so about 1 ms per new body on the render thread, or deferred to a worker since a missing
statistic only means "unflagged"). Memory: 32 B per subset.

**Per draw per frame.** The proxy has the bound VB (its own wrapper, so the metadata is one pointer away, no map),
the node's world scale (`+0x70`, `+0x80..+0x88`, 16.16) and the view-space depth `D` of the node (the camera-relative
coordinates at `+0xf0..+0xf8`, or the world-view matrix the route already builds for the motion rows). Pixels per
object unit is `m00 * W / 2 / D` times the node scale (the same `m00` and `W` the small-parts cull reads). The flag is
the fraction of the group's triangles whose `h` in pixels falls in [0.5, 3] px: a shift of the 8-bin histogram by
`log2(px per unit)` and a sum, under 20 scalar operations; written into `c216.z`, which the route uploads on every
routed draw already. Zero extra API calls; about 448 draws x under 100 ns = under 0.05 ms per frame (**[I]**). The
depth fragment writes `float4(z/w, z/w, w, c216.z)` instead of `w` in `.a`: a transformer change (the fragment now
reads a register of the pixel ABI range, which the route uploads for every row: `rows_use_public_abi`), re-baselining
the 190-case motion-output fixture's RT2 `.a` rows. The flag reaches the resolve through the tests draw: since S1 the
tests draw reads the lane texel itself at s1 (`centreTexel`), so `b |= centreTexel.a > 0` (or a graded strength) is
one compare with no fetch. Lane-only: the R32F RT2 configuration has no `.a`; the flag is silently absent there.

**Compared with an offline table** (material name tables, the merged-LOD baker's class census, screen size of the
bounding sphere):

| | in-DLL upload statistic | offline table |
| --- | --- | --- |
| accuracy | actual distance per draw per frame; the flag turns on where the struts fall under 3 px and off on a near truss (6-px struts get ordinary TAA, today's region marks them) | the same per-frame distance if the table stores the `h` histogram per (model id, subset) and the proxy keys by node `+0x140`; a table of static flags applies at every distance |
| mod support | any mesh the engine loads passes `0x004bb470`; no table, no bake | needs the baker run over every mod tree; missing rows are unflagged |
| overlay bodies (record C) | the merged group's histogram is the union of its parts (the baker copies part ints, nothing recomputed), so the thin fraction is the body's; C draws at distances where struts are under a pixel anyway | the baker can write the same histogram for its own record C, and only for it |
| loading cost | 10-30 ms per session, lazy, on the render thread or deferred | none at runtime; a bake step |
| flag transport | `c216.z` per draw, depth fragment, RT2 `.a`, tests draw centre texel | identical |
| bounding-sphere screen size alone | selects distance, not thinness: the run175 arm is a large near station whose struts are 1-2 px | same; useless alone |
| material names | the ships' "lattice" materials are texture lattices on large quads (`t_AlphaTexture`, flags off, merged-lod-feasibility.md); they have uniform depth and are not the modelled-strut problem; a name table would flag the wrong class | same |

Keep the offline table as the fallback variant only if the upload observer has a blocker (the unqualified
INDEX32 path, the `0x440` non-MANAGED clones if a census shows them common, or native Windows behaviour of the
readable-MANAGED creation policy); its shape would be the same histogram keyed by model id and subset index.

**Why B is a vote, not a replacement.** Three classes stay outside any draw-time flag: the unrouted, depthless,
blended far stations of the pan-flicker fix (no reviewed pair, no depth, no draw identity that reaches the resolve),
draws without a statistic (above), and edges between routed and unrouted geometry. And the flag is per group: a
group mixing struts and panels flags the panels too (clip-off on a static panel is harmless at rest and gated under
motion, so the cost is the region's share, not a ghost). What B adds that the depth test cannot: a lattice in front
of its own hull at similar depth fails the line test's background margin (`(1 - q) * 1.1 < 1 - d`) and is not
classified today; whether such lattices crawl is unmeasured (open). With A' the composed effect is one tests draw
carrying both classifiers and no dilation draws.

**Implemented (2026-09-24, opt-in, default off, not flown).** `--taa-thin-vote on|off` (`X3M_TAA_THIN_VOTE`; on needs
`--taa --motion-output --ownership --sun-shadow-lane`, refused under `--vanilla`); ledger:
[temporal-resolve.md](../verification/temporal-resolve.md#2026-09-24-thin-vote-b-opt-in-fixture-not-flown). The
departures from the text above, each forced by the code or the review:

- *Where the statistic is measured.* The wrapper's `Unlock` alone cannot name the CloneMesh destination pair or its
  vertex declaration: that needs the CloneMesh scope of the callsite adapter removed on 2026-09-22 (cleanup batch 7).
  The histogram is instead read once per subset (VB, IB, draw range) at the first scene end after the subset's first
  routed draw, through the application's own wrapper exactly as the flown caster-extent reader does (lock bookends
  quiet, the native storage MANAGED and readable, one READONLY `Lock` of the vertex window and one of the index range), and
  cached by allocation ids (`src/proxy/thin_vote_core.h`, `MotionOutput::read_thin_votes`). This is the "deferred"
  form of the text; INDEX32 subsets are covered (a public `Lock`), DEFAULT-pool clones (`0x440`) are not, and the
  census found none: 0 non-MANAGED routed VB/IB over 218 flown sessions, while 11 to 265 of 1,634 bodies can hold an
  INDEX32 subset ([census](../../verification/results/thin-vote/census_out.txt)).
- *The transport.* `c216.zw` is not free: the motion fragment subtracts it as the previous-row jitter. The vote
  travels in `c218.x`, the third register of the same per-draw upload (12 floats instead of 8; `c216`/`c217` unchanged).
  `c218`-`c220` held the motion fragment's three DEFs, which would shadow the upload, so with the option on the
  transformer repacks its seven literals into two DEFs at `c219`/`c220` (same values, same instructions: RT1 and RT2
  `.rgb` byte-identical in the fixture) and appends a depth-fragment twin writing `.a = c218.x`.
- *The encoding and the vote.* RT2 `.a = 1 - thin` on an opaque routed row, `1` elsewhere (the fill keeps `-1`; the
  pending fade owners will write `1`), where thin is the fraction of the subset's triangles 0.5-3 px tall at the draw's
  scale when at least half are, else 0 (a panel with a few sliver triangles does not vote). The tests draw's twin
  (`line_mask_ps.hlsl`, `X3M_THIN_VOTE`) sets the flag on a valid depth whose `.a` is in [0, 1) and skips the line search
  there. The per-draw scale is `|row 0 xyz| * W / 2 / row 3 .w` of the draw's own submitted rows (`m00` times the node
  scale over `D`), one `log2` and two cumulative reads of the 8-bin histogram. With 8 log2 bins the window edges are
  resolved to the bin: a pure strut subset votes when its bin's centre lies in [0.5, 3] px, so the effective edges
  are fuzzy by half an octave (at 2.9 px a subset whose bin reaches 4.5 px reads 0.41 and does not vote).
- *Readable buffers and freshness* (review, 2026-09-24): the game's clones are MANAGED and WRITEONLY (`0x660`), so the
  option arms the readable-MANAGED creation policy at `Direct3DCreate9` (WRITEONLY stripped at creation, the requested
  Usage kept for `GetDesc`; [platform-portability.md](platform-portability.md), "TAA thin vote") and the reader refuses,
  without a Lock, every buffer whose native storage is still WRITEONLY or not MANAGED. The policy is armed only when the
  vote can run (one gate computed from the environment: the option with the route, TAA, HDR, the lane and the ownership
  wrapper). A measured buffer is watched (one-shot): its next write (the Unlock of a writable Lock, ProcessVertices, a
  native-mutation notice) or release queues an invalidation in the ownership layer that drops its histogram before the
  next draw looks it up, so a rewritten MANAGED buffer is read again and the draw path does no revision lookup. The
  drain touches only the queued wrappers' entries (an index from wrapper pointer to cache slots); a wrapper rewritten
  four times after reads is volatile and its subsets are no longer read (no Lock), so a per-frame rewrite costs no
  per-frame read.
- *What the scale measures*: `D` is the view depth of the draw's object ORIGIN and the scale is the clip-x row's alone
  (`m00` times the node scale). A subset near the camera on a large station whose origin lies far behind it reads too
  small a scale, so its near panels can vote as thin (a larger region there, clip-off at rest); a non-uniform node scale
  or projection is resolved along x only. No per-triangle depth enters the vote.

### 3.3 C: half-resolution mask and box (S4 / S5), the baseline

Costed in the high-res note: S4 -0.4 to -0.5 ms at 1080p and -1.4 to -1.8 at 5120x1440, S5 -0.1 and -0.35; both
conservative (superset windows, containment oracles). S4 is orthogonal to A' and B and stays in the plan. S5 halves
the draws A' removes; if A' is taken, S5 is not. As a baseline, C alone leaves six draws and the 17x17 reach tuned in
pixels at 1280x768; A' beats it on the mask side by 0.2-0.6 ms at 1080p **[I]** and by construction on reach.

### 3.4 D: coverage or ID target from re-rendered thin draws

No geometry stage on SM3; point size applies to point primitives only; a vertex shader cannot widen a triangle
because it sees one vertex and no adjacency (a conservative extrusion needs the triangle's other vertices). It also
needs B's classification to know which draws to re-issue, then a second `DrawIndexedPrimitive` per tagged draw on
the submitting thread, where wined3d costs about 4.8 us per draw end to end (gpu-backend A/B **[M]**): 448 draws x
20 % tagged = 0.43 ms of CPU per frame before any GPU work, in exchange for skipping a 0.03 ms depth-line search.
Closed.

### 3.5 E: fold the box columns into the resolve

After S3, the resolve could take the 7 column taps of the row targets (2 x 8 B) instead of the two box texels
(16 B) and drop the columns draw: -0.05 to -0.12 ms at 1080p **[I]**; the emitter bound's inner 3x3 adds 9 colour
taps and about 40-50 slots on the open pixels. It competes with A' for the same freed slots and saves a fifth of what
A' saves; after S4 the columns draw costs about 0.2 ms and the fold saves less. Not taken while A' is open.

### 3.6 G: run the mask chain every other frame (found)

Pass-order only: on odd frames skip the three mask draws and keep the previous final mask bound (the copy draw
returns on those frames because S1 folds it into the tests draw). Saves (mask - 0.19) / 2 = 0.32-0.84 ms at 1080p,
0.9-3.0 at 5120x1440 **[I]**. At rest the mask is phase-dependent but the region is a cycle-wide OR, so a stale mask
is nearly exact; under a pan at 5-9 px/frame the region and the gate sit 5-9 px behind the lattice on odd frames, so
the camera-gate fix of Run 59 applies on alternate frames only: a 2-frame flicker of the treatment, which is the
symptom the user rejected. Loses on pan look; recorded because it is the cheapest code change if a diagnostic flight
ever needs the mask cost halved.

## 4. (F) Resolution: 1920x1080 minimum, and 5120x1440

The thin-feature problem is in pixels, the cost is per pixel, and they move in opposite directions. A 1080p minimum
fixes the design's floor: struts at the run175 stand are 1-2 px there, so the full-resolution tests draw and the
7-tap windows are required by the minimum target, whatever the maximum. The reach constants (8 px, the 11x11 grow)
were chosen at 1280x768 and flown at 1080p: 1.5x smaller in angle already. A' replaces the reach in pixels by a hold
in frames and a closure carried by reprojection, which is resolution-independent; that is the design consequence of
the 1080p minimum: do not tune pixel windows per resolution, remove them.

At 5120x1440 (3.56x the pixels): a strut that is 1 px at 1080p is 2.7 px; the lattice pitch grows the same way, so
the 7-tap line sees two class changes on fewer pixels and the region shrinks on its own (fewer pixels flagged, less
box work), while the crawl itself weakens because a 3-px strut's edge pixels are a third of its width instead of all
of it. Distant lattices remain sub-pixel and the tests draw still finds them. What changes in cost: every
full-screen draw removed is worth 2.75-3.56x more, so A' (0.8-2.4 ms) and S4 (1.1-1.8 ms) matter most there, and
S5's angular-reach argument disappears under A'. What changes in the flag of B: nothing; it is in pixels per frame.
Unknown at 5120x1440: whether the 7-tap line is still the right window when a lattice pitch exceeds 7 px (a
2-changes test needs strut plus gap within 7 px); the first 5120x1440 lattice flight settles it, and a wider line
would cost taps only in the tests draw.

## 5. Recommended path

Order: S3 (slots) -> A' -> S4 -> B as a vote, each with its own fixture reference and one flight, S5 dropped.

Hot path of the game: A', S4 and E add nothing per draw. B adds under 100 ns per routed draw (a metadata read, a
divide, an 8-bin shift-sum, one float into an existing upload) and 10-30 ms of one-time upload work per session,
lazily per body; no new Lock, draw or query on the game's path (the observer reads the mapping D3DX already holds).

Native Windows: A' uses the same MRT and formats as today (`G32R32F` beside FP16 needs `MRTINDEPENDENTBITDEPTHS`,
already required by the age MRT; an R32F fractional encoding needs nothing new); B uses documented COM (public
`Lock`/`Unlock` observation through the proxy's own wrappers, `D3DUSAGE_WRITEONLY` removal on MANAGED buffers, a
per-draw constant). Both are source-compatible and unverified on Windows; the readable-MANAGED policy is the one
piece whose native behaviour (Windows drivers may keep a system-memory copy regardless) is not the same question as
on wined3d and needs its own run.

Proof: section 3.1's three steps for A'; for B, a host test of the histogram on authored meshes (a strut box, a
panel, a mixed group, the FLOAT16_4 and FLOAT4 declarations), the CloneMesh observer fixture extended with the
statistic (no new Wine fixture: the existing manual-upload fixture already runs real d3dx9_37 through the wrapper),
the motion-output fixture's RT2 `.a` rows, and the lattice mode with a flagged draw over a plain hull (the region must
appear on the flagged pixels with the tests draw's own flag forced off).

## 6. Unknown, and what settles it

- Where the 0.9 ms busy-sector mask excess sits (tests or dilations): the `taa_mask_*` split is built (indices 22-24)
  and unflown; it moves A' between 0.28 and 0.67 ms at 1080p.
- S3's slot yield (30-50 assumed): `fxc` on the 5-tap program; A' needs 15-25 of them.
- Whether the undilated-plus-temporal region covers the hot pixels at rest as the 11x11 did: the section 32 replay
  with the propagation rule (host only).
- Whether hull-backed lattices crawl today (B's coverage gain): a dump at a station arm seen against its own hull,
  through `tmask.py`.
- The share of clones built with option `0x440` (not MANAGED, unreadable) and of INDEX32 meshes: a one-line census
  in the ownership layer on a loading run.
- The 7-tap line at 5120x1440: the first lattice flight there.

## 7. Considered and why they lose

| option | loses because |
| --- | --- |
| A as briefed (tests inside the resolve) | 130 slots of tests into a 505-slot program; no S3 yield covers it |
| B as the sole classifier | no flag for unrouted far stations (the pan-flicker class), for unflagged buffers, or for routed/unrouted edges; per-group granularity flags panels with their struts |
| B by material names or bounding-sphere size | names select texture lattices (uniform depth, not the problem); sphere size selects distance, not thinness |
| offline table instead of the upload statistic | no mods without a bake, model-id keying, same transport; kept only as the fallback if the observer has a blocker |
| C alone (S4 + S5) | conservative but leaves six draws and pixel windows; S5 halves what A' removes |
| D | no geometry stage on SM3; adds draws on the wined3d submitting thread (about 4.8 us each) |
| E | competes with A' for S3's slots for a fifth of the saving |
| G | one-frame-stale gate and region under pans: the Run 59 fix on alternate frames |
| a 2-D single dilation draw (5x5 or 7x7) | the gate needs 17x17 today and at least 13x13 with a 3-px region; 169 taps in one draw cost more than the two separable draws |
| computing the 17x17 speed minimum inside the tests draw | 289 motion texels of 16 B per pixel; bandwidth alone about 1 ms |
