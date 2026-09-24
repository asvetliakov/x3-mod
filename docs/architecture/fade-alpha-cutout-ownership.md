# Fade-band alpha-tested cutouts as RT2 owners (design, 2026-09-24)

Design note for the orchestrator to ratify. Question: how the fade RT2 owner
([fade-rt2-ownership.md](fade-rt2-ownership.md), default on since Run 81) takes ownership of the fade-band stations'
alpha-tested cutout draws, so that the sentinel stabiliser (`X3M_TAA_SENTINEL_STABILISER`, 0.7) can be retired.
**Implemented 2026-09-24 as sketched in section 2.2** (fixture evidence and flight rows:
[linear-distance-fade.md](../verification/linear-distance-fade.md), "Fade-band alpha-tested cutouts"). One departure from
step 3: the opaque chain stops at Z-write off before it reads ALPHATESTENABLE, so its `test` is the unread initial 1 on
every fade-arm row; `route.alpha_tested = test != 0` would mark every fade row alpha tested. The arm stores its own read
in `route.fade_tested` and the line is `route.alpha_tested = route.fade_arm ? route.fade_tested : test != 0`.
[M] measured or read in code this session, [S] from reverse-engineering notes, [I] inferred.

## Decision

**Recommended: option A, the fade arm admits the engine's alpha-tested fade-band state.** `fade_route::state` accepts
`alpha_test == 1` beside `alpha_test == 0` for a fade pair (a `fade_route::registers` row) when the RT2 owner is on and
under original shading, and nothing else changes on the device: no texkill, no texture lease, no new program, no second
draw. The D3D9 alpha test compares `oC0.a` and discards the fragment before the depth write and before every target
write (`alpha-tested-materials.md:23`, the documented MRT contract; the tested-opaque arm and the exact cutout arm
already route alpha-tested draws on exactly this rule, `motion_output.cpp:5476-5480` [M]). The routed variant's `oC0.a`
is the original's (material_motion alpha identity), so the cutout's RT1/RT2 coverage is the engine's colour coverage:
the owner lane `.a = 1` lands only where the texture alpha passes the engine's own ALPHAFUNC/ALPHAREF, and the holes keep
the sentinel or the hull that the engine's Z test let through. The brief's texkill is what the fixed-function test already
does; adding it would be a second, redundant coverage rule that could only disagree with the first.

## 1. What the evidence says (run311, [M])

- Draw table of burst 16568 (`verification/results/run311-run81a-stabiliser-off/draw_table_16568_out.txt`): on every
  fade-band station node the hull rows `4944d81d/ca6bfa4a` and `53a0a641/8759c783` are `b1:5/6 z0 at0 R1 arm1 p960-962`
  (routed, owner); the cutout rows `4944d81d/5e0a10fe` and `53a0a641/63f96eba` are `b1:5/6 z0 at1 R0 arm0 no_zwrite`.
  35 such rows in the frame (`grep -c "5e0a10fe\|63f96eba"`), 16 of them on stand nodes per the ledger.
- The same cutout pair `4944d81d/5e0a10fe` on the near node `1b19edd0` is `b0:5/6 z1 at1 R1 arm0` - routed through the
  tested-opaque arm with the alpha test on. So gate 3 already accepts the pair, its motion/depth variant already exists on
  the device, and alpha-tested routing of this very program is in production (Run 81). Option A changes the state
  admission only.
- The node's depth prepass is two aliases: `c78b4c68/00000000 z1 at0` (hull parts, no PS) and `803ebfd1/652a7c5d z1 at1`
  (the textured z_only alias, `shader-family-review.md:56` "PS returns a texture sample", alpha test on). The Z buffer
  therefore already holds the panel depth only where the texture alpha passes; the holes hold whatever is behind (hull
  or far plane). Both aliases are in `depth_prepass_profiles.h:41-42` and jittered by the route.
- Station detail pixels on 16568: 3,144 owned / 3,673 sentinel (valid depth 0.46); 16126: 2,288 / 10,859 (0.17). The
  shimmer classes at rest (ledger): owned thin parts 26.2 codes, unowned panels 5.35; under the 4-6 px pan both classes
  are 9-15 codes mean per pair (`shimmer_16568_out.txt`). Ownership of the cutouts is what conditions 2 and 4 of
  fade-rt2-ownership.md section 5 are missing.

## 2. Option A in depth

### 2.1 Correctness

- **Holes.** The alpha test runs on `oC0.a` before the depth write and every target write; a failed fragment writes no
  RT0, RT1 or RT2. The transformed pixel program keeps the original colour computation and its alpha (the variant
  appends the motion/depth epilogues after the original's final write; `material_motion_pixel_writes_depth`); the route
  never touches ALPHAFUNC/ALPHAREF. So the RT2 write set is exactly the RT0 write set of the engine's own draw: opaque
  texels own, holes do not, with no threshold of the route's own. This is the same law the tested-opaque arm relies on for
  the XT class-C hulls (ALPHAREF 1 GREATEREQUAL, mask 7) and the exact cutout arm proved with its coverage oracle
  (`seam-taa-cutout-opaque`: pass/fail coverage identical to the native draw).
- **Depth in RT2.** The owner fragment writes `.a = 1` from `c218.y`, so the cutout's SRCALPHA/INVSRCALPHA blend stores
  `src * 1 + dst * 0` on RT2, the exact `z/w`, as for the hull (fade-rt2-ownership.md section 2). RT0 still blends with
  the engine's `oC0.a` (fade x diffuse alpha) - the blend factor of each target is that target's own output alpha under
  `MRTPOSTPIXELSHADERBLENDING`, already required by the probe verdict.
- **Depth test against the prepass.** The cutout draw runs under the engine's Z test (LESSEQUAL against the prepass,
  Z-write off, untouched). The textured prepass alias is itself alpha tested, so at an opaque texel the Z buffer holds the
  panel's own depth (equal: passes) or a nearer surface (fails: correctly not owned); at a hole the colour draw is already
  discarded by its own test before the Z test matters. Whether the prepass and the colour draw use the same ALPHAREF is
  inferred (same material, same texture); if they differed, the intersection still holds: RT2 coverage is a subset of the
  colour coverage, never a superset.
- **Sorting against the blended hull.** Same argument as the owner note's section 3: with the prepass the nearest
  band fragment at a pixel is the only one that passes LESSEQUAL, so which of hull or panel wins RT2 is decided by depth,
  not by draw order (the hull rows 44/47-50/52 and the cutout rows 45/46/51 interleave on node 29c0fbf0 [M]). RT2 stores
  `src * 1 + dst * 0`, so the last passing writer wins and the passing writer is the nearest. Without a prepass (blended
  subsets the walk skips, `distance-fade.md` section 8 [S]) the last band fragment drawn wins, bounded by the parallax
  between two band surfaces of one node, a small fraction of a pixel per frame [I].
- **Motion.** The cutout draws the node's rows through the same VS as the hull (`4944d81d` / `53a0a641`, the registers
  rows), so RT1 carries the node's rigid motion at the panel's own depth; `rows_known[window]` is already true on the
  frame because the hull draw of the same window routed. The fraction (`g_AlphaValue` c39, `g_FogClip` c41, b0) is read
  from the same registers; the hysteresis key is the node identity, so hull and cutouts change class on the same frame.
- **Coverage shrink near the band's outer edge.** The engine compares `fade x diffuse.a` with the ref, so a panel loses
  texels as the fade drops; RT2 follows the colour exactly. At permille < 400 the node is refused and hull and cutouts go
  sentinel together, as today.
- **Thin vote.** `thin_vote_alpha` returns 1 for fade-arm rows (`motion_output.cpp:7369` [M]); an owner carries no
  thickness vote, unchanged. Panels are quads with texture-defined openings, not thin triangles, so the vote had nothing
  to say about them anyway; the truss lines are classified by the line test against the sentinel like the hull struts.

### 2.2 Implementation sketch (done 2026-09-24; step 3 departure in the header)

1. `src/proxy/fade_route_core.h` `state(...)`: add a `bool tested_ok` argument, `alpha_test == 0 || (tested_ok &&
   alpha_test == 1)`; every other term unchanged. The host driver `--fade-route-state` gains the eleventh field;
   `test_fade_region.py` adds the `(2,1)` variant as accepted with `tested_ok = 1` and refused with 0.
2. `MotionOutput::fade_arm_admits`: `tested_ok = !overlay && fade_rt2_owner_ && !linear_material_requested_`. Not the
   overlay arm: an unrecognised reviewed pair drawn alpha-tested after a routed draw is material transparency of unknown
   alpha law and the existing `seam-taa-cutout-blended` refusal (`motion_output_cutout_inc.h:146`, `!script_blended`)
   must stay byte-identical. Not without the owner: a routed-but-masked alpha-tested draw is RT1-only, today's flicker
   class. Original shading only, the widening's own boundary (the linear bracket's composition of an alpha-tested blended
   row is unverified).
3. `motion_output.cpp:5576`: `route.alpha_tested = test != 0` (drop `!route.fade_arm`). The flag gates the hull light-map
   widening (`:5048`, which rewrites `rL.w` and would change the alpha-tested coverage; `hull-emissive-widening.md:158`)
   and the replay exclusion W3; a Z-write-off draw is never a caster candidate anyway (`:7907` requires `zwrite`), so
   the only behavioural effect is the widening skip on a cutout that also binds a light map.
4. Counters: `fade_tested` on the `fade_route_frame` line (fade-arm rows admitted with the alpha test on) beside
   `fade_routed`. With the owner off (or under linear materials) an alpha-tested fade-band draw fails `state` before
   the arm counts anything: an uncounted gate-4 refusal labelled `unmatched=no_zwrite`, as before the change. One
   row `fade_rt2_owner_configured ... tested=1`.
5. Program count: +0. The variant of `4944d81d/5e0a10fe` exists (routed at node 1b19edd0 [M]); `53a0a641/63f96eba`
   reaches gate 4 today (reviewed pair) and gets the same variant every other routed pair has. Slots: unchanged (the
   owner fragment is already +2 on every depth-writing variant).
6. Per-draw device state: nothing new. `bind_targets` sets `COLORWRITEENABLE2 = 15` on an owner row as today; the
   alpha test, ALPHAFUNC/ALPHAREF, blend triple and Z states are the engine's and are not read beyond the existing
   `render_state` reads (the test value is already read on every gate-4 draw).

### 2.3 Cost

- CPU: a stand cutout draw goes from the `no_zwrite` refusal (two Z reads and the fade arm's eight state reads, about
  1 us [I]) to a routed owner draw, 7.49 us lazy ownership per routed draw (`route-per-draw-cost.md` [M]). 16 draws per
  frame on the stand: at most 0.12 ms; 40 draws: 0.3 ms [I]. This equals what the same draws already cost when the
  station is inside FogNear and routes through the tested-opaque arm, so no new peak.
- GPU: the transformed variant instead of the original on 16-40 small draws (a few thousand covered pixels at band
  distance), plus the RT1/RT2 writes on the passing texels. Below the measurement floor of the frame timers [I].
- No allocation, no lease, no extra draw call.

### 2.4 Fixture

`run_motion_output.py`, `faderoute` family, one new script `cutout` (two cases):

- `seam-taa-fade-route-cutout-owner`: the fixture's fade pair drawn as a quad with a 2D managed texture whose alpha is 1
  on the opaque texels and 0 in a centred hole (as the cutout fixture's textures), the engine's band state plus
  `ALPHATESTENABLE` on, GREATEREQUAL ref 1, over the fill at fraction 1000; before it the z_only prepass of the same quad
  with the alpha test on (null PS, texture stage 0 `ALPHAOP SELECTARG1 TEXTURE`, so the fixed-function alpha is the
  texture's; the route jitters the alias by VS identity, `depth_prepass_profiles.h` [M]). Asserts per frame:
  `fade_routed = 1`, `fade_tested = 1`, `fade_owner_masked = 0`; RT2 `.r` equals the quad's z/w within
  `FADE_ZONLY_DEPTH_TOLERANCE` and `.a = 1` on every opaque texel; RT2 still the fill `-1` on every hole texel; colour
  coverage and RT2 coverage agree per pixel (the zonly parity oracle, `validate_fade_zonly` shape); RT1 carries the quad's
  motion on the opaque texels and the fill in the hole. A second quad, a blended hull without alpha test behind the
  panel, checks sorting: at hole pixels RT2 holds the hull's depth where the hull passes the prepass, the fill where it
  does not.
- `seam-taa-fade-route-cutout-refused`: the same draw with the owner off (`X3M_FADE_RT2_OWNER=0`): the panel fails
  `fade_route::state` before the arm counts it, so it is an uncounted gate-4 refusal (`unmatched=no_zwrite`,
  `fade_routed` and `fade_refused` unchanged by it), RT1/RT2 untouched - the pre-change behaviour, byte-identical.
- Regression: `seam-taa-cutout-blended` unchanged (the overlay arm and the cutout pairs are out of scope);
  `seam-taa-fade-route-*` owner cases unchanged; `test_fade_region` state table.

### 2.5 Flight (the run214 / run311 stand, S = 0)

Rows: `fade_route_frame` (`fade_routed`, `fade_tested` >= 16 on the stand, `fade_refused`, `fade_owner_masked = 0`,
`overlay_refused = 0`), `motion_route` (no `unmatched=no_zwrite` row for `5e0a10fe` / `63f96eba` on a stand node),
`unjittered_depth_writers = 0`. Burst: station detail pixels valid depth at or above 0.9 (section 5 condition 2; the
remaining sentinel detail pixels should be the panel holes and the edge halo only), `taa_mask` stabiliser class 0 on
them; the shimmer script's unowned class near empty and the owned class at or below the current owned figure. User:
no shimmer on the fade-band plants with S = 0 (condition 4). Condition 3 (replay rms) stays undecidable until a
5120x1440 replay exists; the note recommends flipping the default on conditions 1, 2 and 4 with the input-side shimmer
script as the stand-in for 3.

### 2.6 Risks

- Any other SRCALPHA/INVSRCALPHA, alpha-tested, Z-write-off draw on a registers-row VS at fraction >= 500 is now routed
  (asteroid pairs, station effects). The fade pairs' HUD sprites are ONE/ONE and still fail `state`. `fade_tested` and a
  `motion_route` census of the first flight name any newcomer; none is expected (the seven asteroid pairs were never seen
  alpha tested in run214/run307/run311 [I, not counted]).
- Native drivers: the alpha test's discard before MRT writes is the documented contract and the exact cutout arm's
  basis; unverified natively like every MRT identity of the route (`platform-portability.md`).
- A cutout whose prepass uses a different ALPHAREF than its colour draw: RT2 coverage stays a subset of colour coverage
  (section 2.1); no wrong ownership possible, only fewer owned texels.

## 3. Alternatives considered

- **B, RT2-only replay of the cutout after the node** (the alpha-caster shape): one extra draw per cutout, the stage-0
  texture lease and its release before Reset, a VS twin per position profile plus the texkill PS (two more programs),
  replay bookkeeping, and a Z test at replay time against a Z buffer that later draws have changed (a nearer object drawn
  after the node hides the panel correctly, but an RT2 `.a = 1` overwrite at equal depth reorders owners). It reproduces
  by hand the coverage the fixed-function test gives for free and adds about 16-40 draws and leases per frame. Loses on
  cost and adds a second alpha law that can drift from the engine's.
- **C, thin-vote candidates**: the vote is a triangle-height histogram of a routed opaque subset carried in RT2 `.a`
  by the depth fragment; it needs the draw routed (so it needs A anyway) and says nothing about texture-defined openings
  in a quad; it widens the resolve's thin region and supplies no depth for the far gate or the line test. Loses.
- **D, keep the stabiliser**: the run311 flight shows the shimmer with S = 0 and the class can never empty over the
  panels without ownership; the stabiliser's 36 slots and its box behaviour stay for good. Loses because A is a one-term
  admission change with an existing variant and an existing coverage law.
- **A with a texkill in the owner fragment** (the brief's wording): a second coverage rule on the diffuse alpha inside
  the depth fragment would need the texture, its stage, the ref and any texture matrix of every fade pair, and could only
  disagree with the fixed-function test that already discards the fragment for every target. Loses.

## 4. Native Windows

Documented D3D9 only: the alpha test's fragment discard before all MRT writes, per-target blend alpha and independent
write masks already required by the cutout probe; no new cap, constant or program. Unverified natively as the rest of
the route; the fixture's RT2 readback is the parity check for a native machine.

## 5. Unknown, and what settles it

- Whether the textured prepass alias and the colour cutout share ALPHAREF: an `object_context` / render-state census of
  one node's draws 45 and 10 in a burst log, or a `disassemble` look at the material's z_only submission. Only the owned
  texel count depends on it, not correctness.
- Whether any fade-pair VS draws alpha-tested band-state geometry other than the station cutouts: the first flight's
  `fade_tested` and `motion_route` rows.
- Condition 3 of the owner note (replay flicker rms at 5120x1440): a replay tool at the stand's resolution, or the
  input-side shimmer script as the accepted stand-in.
