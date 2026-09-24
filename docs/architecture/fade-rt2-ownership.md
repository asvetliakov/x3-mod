# Fade-band draws as RT2 owners (design, 2026-09-24)

Design note for the orchestrator to ratify. Question: how a fade-band draw above the alpha threshold writes exact depth
and motion into RT2 so the resolve treats it as geometry, and under what condition the sentinel stabiliser
(`X3M_TAA_SENTINEL_STABILISER`, default 0.7 with the camera gate) can then be removed. Implemented opt-in (see the
Decision block), not flown. [M] measured or
read in code this session, [S] from disassembly notes, [I] inferred.

## Decision

**Default on since Run 81** (launcher, whenever `--taa --motion-output --hdr` are given, else not sent with one launcher line; `X3M_FADE_RT2_OWNER_DEFAULT=1`, row `fade_rt2_owner_configured ... default=1`; Run 80 A launch 4).

**Ratified 2026-09-24 (orchestrator)** with three conditions: (1) the ownership lands behind an opt-in option
(`--fade-rt2-owner on|off`, default off) until its first flight, so the installed A' behaviour and every pinned fixture
stay byte-identical with it off; (2) the flag lane uses the c216-c218 upload of section 2 and lands after option B's
register change (B was told to move its flag to c218.x and encode `1 - thin`, vote on `validDepth && a < 1`); (3) the
prepass unknown of section 10 is being settled by a disassembly task on the `+0x270 & 0x40000` writer before the
fixture plan is finalised. The identity widening (section 3) gets its own review.

**Implemented 2026-09-24 (opt-in, not flown)** as `--fade-rt2-owner on|off` (`X3M_FADE_RT2_OWNER`, DLL and launcher
default off; requires `--taa`, on also `--motion-output --hdr`; refused under `--vanilla`); ledger
`docs/verification/temporal-resolve.md` "fade owner". Departures from the text below: (1) the lane is three lanes of
`c218` and one fragment for both B states: `current_depth_owner_ps.hlsl` writes `.a = max(w * c218.z + c218.x,
c218.y)`, the route uploads `c218.y = 1` on a fade-owner row, `c218.x` = B's `1 - thin` (0 with B off) and `c218.z = 1`
on any other row with B off, so every non-owner row keeps exactly the value the plain (`w`) or thin (`1 - thin`)
fragment writes and B's `.x` upload and DEF repack are unchanged; (2) the identity widening applies under original
shading only (the overlay arm's own boundary: under linear materials a blended hull pair may belong to the composition or
glass bracket), and `fade_route::arm_pair` is the one identity function; (3) on the four-channel lane an owner writes
`.g` too, so it binds the motion variant's invalid-share twin (`.g = -1`, not a receiver); a row without one (material,
XT) stays masked and is counted (`fade_owner_masked` on `fade_route_frame`, with `fade_evicted` and `fade_owner`); (4)
the tests draw is unchanged: B votes on `validDepth && 0 <= a < 1`, an owner carries `a = 1` exactly. Slots: depth
fragment 3 -> 5, every depth-writing pixel variant +2 (measured). The lattice case shows the class change opens the
region once per pixel and adds no hold beyond an always-owner run; an owner holds the region on 52 pixels of an 8 x 8
square in steady state (inferred: its corners, flagged by the diagonal line test like any routed geometry against the
sentinel), which section 4 did not state. History resets at the class changes, measured against option-off twins
(ledger table): over the fill the owner adds exactly one current-only frame at each falling edge (owner -> sentinel,
effect (b) below); the rising edge (sentinel -> owner) is current-only with or without the owner because a node routed
again after a refusal has no matched rows (the one-frame row history), so effect (a) does not occur in practice. Both
are bounded to one frame per band crossing. Follow-up if the flight shows the falling-edge frame: the resolve-side
acceptance of section 4 (a history depth against a far-plane current where the history's motion alpha was 1).

Every draw the fade-band arm routes becomes the RT2 owner of the pixels it covers: `COLORWRITEENABLE2` goes from 0 to
15 on fade-arm rows, and the current-depth fragment emits alpha exactly 1 (from a route-uploaded lane) so the draw's own
`SRCALPHA/INVSRCALPHA` blend stores `src * 1 + dst * 0`, the exact depth, in RT2 - the same identity RT1 already relies on.
Ownership has no threshold of its own: routed is owner (500 permille, 100 permille hysteresis, unchanged). The arm's
pair identity is widened from the seven `distance_fade_rows` pairs to every reviewed pair whose vertex program declares
`g_AlphaValue` / `g_FogClip` / `g_EnableFog` (the run214 station families `494fe349b8bc12ec` and `53a0a641107ed76c` do,
at the same registers c39 / c41 / b0 as the seven), so the "unrouted, depthless, blended far stations" of the pan-flicker
fix are decided by their real fade fraction instead of the overlay witness that refuses them today. The engine's
Z-write stays off; RT2 is written under the engine's own Z test. The stabiliser stays in the build at its default until
one pan flight on the run214 stand shows its class empty over the stations and the flicker no worse with S = 0 than
with S = 0.7 today; then its default becomes 0 and the code is retired in a later cleanup.

## 1. Current state, verified in code

- Arm identity: `shadow_.fade_route_pair` = threshold <= 1000, VS and PS registered, `linear_distance_fade_pair(vs, ps)`
  (the seven rows of `src/renderer/linear_material.cpp:2185-2193`: six asteroid pairs plus the station BUMPMAP pair
  `4944d81dfe531b37/64bac8bb307eb896`) and `fade_route::registers(vs)` (`src/proxy/motion_output.cpp:4889-4891`,
  `src/proxy/fade_route_core.h` seven VS rows) [M].
- Gate 4: `fade_arm_admits` (`motion_output.cpp:1061-1124`) - exact fade-band state (`fade_route::state`: Z on,
  Z-write off, alpha test off, blend 5/6/ADD, mask 7, sRGB off, separate alpha off), cutout probe verdict Ready
  (`probe_cutout_caps`, `:940`: `NumSimultaneousRTs >= 3`, `MRTINDEPENDENTBITDEPTHS | INDEPENDENTWRITEMASKS |
  MRTPOSTPIXELSHADERBLENDING`, `AlphaCmpCaps GREATEREQUAL`, `QUERY_POSTPIXELSHADER_BLENDING` on FP16 / RGBA32F / R32F),
  TAA, FP16 HDR active, then three `GetVertexShaderConstant*` reads, the origin distance and the keyed hysteresis. A
  reviewed pair that is not a fade pair takes the overlay path (`overlay = !shadow_.fade_route_pair`) and is admitted at
  1000 only as the very next draw after a routed draw of the same node; otherwise `unmatched=overlay_node` [M].
- Route: `bind_targets` (`:506-556`) binds RT2 for a depth row and sets `COLORWRITEENABLE2 = route.fade_arm ? 0 : 15`
  in both binding modes; the undo restores the saved mask (`:3911`) [M].
- Depth fragment `src/temporal/current_depth_ps.hlsl`: `float4((z/w).xx, w, w)`; the fill program writes
  `(-1, -1, -1, -1)` to RT2 (`motion_output.cpp:171-178`) [M]. Pixel ABI upload `pixel[8]` (`:5548`): c216 = (1/w, 1/h,
  0, 0), c217 = (previous-rows mode, widen x, widen y, fade gain); the motion fragment SUBTRACTS c216.zw as the
  previous-row jitter (`rigid_motion_ps.hlsl:18`, relocated c0 -> c216 by `material_motion.cpp:314-325`) [M].
- Tests draw (`line_mask_ps.hlsl`): `centreTexel` is the whole RT2 texel at s1 (`:106`, `:217`); a valid depth takes the
  camera path with `viewZ = centreTexel.b` when the lane is bound (`:193`); the stabiliser class is
  `sentinelDepth(depth) && motion.w == -1` (`:322`, composed at `:243-248`, strength `S * cameraOpen` at `:290`); a
  routed sentinel pixel (alpha 1) is outside the class and casts no camera-gate vote (`:175-177`) [M].
- Resolve (`resolve.hlsl:350-372`, `:455-480`): a sentinel-depth pixel takes the far-plane camera path under policy 2;
  a routed alpha-1 correspondence over it supplies the previous UV; a valid depth is geometry (dilation, disocclusion,
  far gate) [M].
- run214 class (`temporal-integration.md` "What the stations are"): 62 `overlay_node` refusals per frame, all
  `494fe349/fffdabd9`, `53a0a641/8759c783`, `4944d81d/ca6bfa4a`, `53a0a641/63f96eba`, src 5 / dst 6, zwrite 0, atest 0
  [M, run214]. Shader-sweep inventory: `494fe349b8bc12ec` and `53a0a641107ed76c` (vs_3_0, runtime captured) declare
  `g_AlphaValue` c39, `g_FogClip` c41, `g_EnableFog` b0 - identical to the five loop programs of the arm's table; 199
  vertex programs in the inventory declare `g_FogClip` [M, this session]. So the pan-flicker stations are fade-band
  draws of the engine's producer (`distance-fade.md` section 2: one N/F pair for every class, `g_ZWriteEnable` false,
  `g_AlphaBlendEnable` true, `g_FogClip = (Fs/(Fs-Ns), 1/(Fs-Ns))` [S]) that the arm cannot judge only because their
  pairs are not in `distance_fade_rows`.
- Engine depth in the band: the frame routine walks the scene twice when camera `+0x270 & 0x40000` is set, first
  submitting fog-band nodes as a depth-only prepass (`node+0x130 |= 0x20000`, the `z_only` aliases), then everything
  (`distance-fade.md` section 5 [S]); run 47 witnessed that prepass for every fogged asteroid, drawn LESSEQUAL before the
  blended draw, and the route now jitters the two `z_only` aliases with the scene and counts `unjittered_depth_writers`
  (`asteroid-fog-temporal.md` "Fix: the z_only prepass is jittered") [M]. Where the flag is set is an open unknown of
  the RE note (section 7).

## 2. Exact depth through the blend (question 1)

D3D9 contract (documented, `alpha-tested-materials.md:23` cites the MRT page): with `D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING`
the post-pixel-shader operations apply to every bound target, and blending on target n uses target n's own output alpha;
`D3DPMISCCAPS_INDEPENDENTWRITEMASKS` makes `COLORWRITEENABLE1..3` per-target; the format must answer
`D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING`. The fade-band draw's factors are fixed by the engine (SRCALPHA /
INVSRCALPHA / ADD, separate alpha off), so RT2 stores `src.rgb * src.a + dst.rgb * (1 - src.a)`. The only way to store
the exact depth is `src.a = 1`: the fragment emits `float4(z/w, z/w, w, lane)` with `lane = 1` on every blended row.
`dst * 0` is exact because RT2 holds only the fill `-1` or a finite routed depth (a NaN `dst` would poison the pixel;
it can arise only from a routed opaque owner with `w = 0`, which `validDepth` already rejects to current-only). Mask
15, not 7: `.b = w` must land for the tests draw's `viewZ` read and `.a` for the vote below.

Reconciliation with B (`taa-thin-geometry-alternatives.md` 3.2, concurrent): RT2 `.a` is defined as the owner's blend
weight. Opaque owners may carry any value in it (no blend), blended owners must carry 1. So B's flag is encoded as
`a = 1 - thin` on opaque rows (B on), `a = 1` on fade-arm rows and everywhere with B off; the tests draw votes on
`validDepth(centre.r) && centre.a < 1` with strength `1 - centre.a` (one compare more than B's `a > 0`; the
`validDepth` gate is needed anyway because the fill's `.a` is -1). If B lands first with `a = thin` and `a > 0`, the
equivalent reconciliation is: fade-arm rows upload 1, B clamps its fraction to <= 255/256 and votes on `0 < a < 1`.
Either way a fade owner carries no thickness vote: a far station's struts are classified by the line test against the
sentinel behind them, which is the case B does not add anything to (B's own note: it adds lattices in front of their
own hull at similar depth). Lane-off R32F RT2 stores no `.a`; the shader's output alpha still drives the blend factor,
so depth is exact there too and B's vote is absent, as B already accepts. The sun-share RGBA32F lane reads `.r/.g/.b`
only. The lane constant: c216.zw is not free (the motion fragment subtracts it as jitter [M]) and c217 is fully used,
so the upload grows from 8 to 12 floats (c216-c218, one `SetPixelShaderConstantF`) and the depth fragment reads
relocated constant index 2 (`relocate_register` accepts index <= 4). This also affects B: `c216.z` as written in its
note would shift RT1's previous UV by the flag.

Cap absent: the probe verdict is `Unsupported` and the arm never admits (today's behaviour): the draw is a plain gate-4
refusal on the native path, RT2 keeps the sentinel and the stabiliser (while present) covers it. No second fallback is
needed or possible: without per-target blending there is no documented way to write an unblended value from a blended
draw in one pass. wined3d (CrossOver) reports the three caps and the FP32 blend queries on the X3 bottle (the probe's
verdict is Ready in every fixture run of the `faderoute` family [M]); native D3D9 hardware of the ps_3_0 class reports
them as well [I, unverified natively, as RT1's alpha-1 blend already is].

## 3. Ownership rule, Z and draw order (question 2)

Routed is owner. The 500 permille threshold with the 100 permille keyed hysteresis is the ownership rule; no separate
RT2 threshold. A second threshold would create a third state (RT1 routed, RT2 masked) that is exactly today's flicker
class, and the argument for a low ownership bar is the same as for routing: a fade owner's history mixes the
background behind it, but under a pan (rotation) every depth reprojects identically and under translation the parallax
between a band object (>= FogNear, tens of kilometres) and infinity is a small fraction of a pixel per frame [I], so the
mixed history is on the owner's path within the neighbourhood clip. Below 400 the pixel goes back to the sentinel /
far-plane path as today.

Identity: the arm keys on `fade_route::registers(vs)` alone (gate 3 still requires the reviewed pair), with the table
extended by `494fe349b8bc12ec` and `53a0a641107ed76c` (registers 39/41, the inventory [M]); `distance_fade_rows` stays
the bracket's identity (`fade_sampler_mask`) so the linear-material bracket admits nothing new. Not added in this step:
the glass VS `c30104cb0efb6675` (39/41) and the damage VS `37c34a7478544c14` (40/44), whose fade-band-state draws are
material transparency at any distance; they keep the overlay path. HUD sprites on `494fe349` are ONE/ONE and fail
`fade_route::state`. The overlay arm stays as the second chance for a reviewed pair without a registers row (with the
extension it should be empty on vanilla content; the `overlay_node` count of the flight tells). The `g_AlphaValue`
node override (`+0x13c`, value/255 [S]) is part of the fraction, so a node the engine fades by alpha is refused below
500 as today.

Z: `ZWRITEENABLE` is never touched (the engine's later blended draws depend on it); RT2 is written under the draw's own
Z test, like RT1 today. With the fog-band depth prepass in effect the LESSEQUAL test admits only the nearest fragment of
the band at each pixel, so RT2 gets an order-independent depth; the prepass is jittered with the scene and
`unjittered_depth_writers` must stay 0, so RT2 ownership inherits exactly the coverage the colour has (run 47's parity
holes would show in both or neither). Where the prepass is absent (the `0x40000` flag unknown), the last band fragment
drawn wins: within a node the rows are the same and the depth spread is a small part of the distance; between two band
objects the wrong owner differs by their parallax, bounded as above [I].

## 4. Interaction with A' holds and the box programs (question 3)

An owner pixel is a routed valid-depth pixel: the tests draw's camera path uses its own depth and lane `.b`, its camera
gate vote is its RT1 correspondence against the camera path at that depth (0 for a static station), the line test
classifies its thin parts against the sentinel behind them, and the region / closure holds and the box twins treat it
as any far routed lattice - the accepted lattice-crawl behaviour. Class changes happen only at the two hysteresis edges
(500 rising, 400 falling), once per band crossing per node, so the region hold is charged at most L frames per crossing;
a node hovering between 400 and 500 stays in one class. Two bounded effects: (a) sentinel -> owner keeps history (a
history depth of 1 is behind the new surface, sentinel taps never reject); (b) owner -> sentinel at 400 falling rejects
the history once (a history depth in front of the far plane), one current-only frame on a 40 %-alpha object; today's
routed-but-masked draw had no such reset. The hover fixture measures (b) at its 390 frame; if the flight shows it, the
resolve could accept a history depth against a far-plane current when the history alpha was 1, at a few slots, but not
in this step. The stabiliser class becomes empty over owners by definition (depth valid, alpha 1), so the box's
sentinel-class open condition no longer fires there; the box still opens by the region rule. `Hysteresis::capacity`
is 64 keys (nodes, not draws): a stand with many band nodes evicts the oldest, which only restarts that node at the
threshold; add a `fade_evicted` counter to the `fade_route_frame` line before raising the capacity.

## 5. Removal condition for the stabiliser (question 4)

Keep `X3M_TAA_SENTINEL_STABILISER` at its default in the ownership build; fly the run214 stand once (two distant
stations, slow vertical pan 3-9 px/frame, F8 `--taa-debug` bursts at rest and during the pan), twice: S = 0.7 (the
default) and S = 0. The stabiliser's default becomes 0 when all of the following hold:

1. Log: on the stand frames `overlay_refused = 0` and no `unmatched=overlay_node` row for the four run214 families;
   `fade_routed` at least the station draw count (62 on frame 24630); `unjittered_depth_writers = 0`.
2. RT2 dump (`taa_depth` or the lane readback of the burst): valid depth on at least 0.9 of the station crop's detail
   pixels (run214 crop 540 180 700 340, 4325 luma-detail pixels), and the `taa_mask` b code carrying the sentinel class
   (1/255 or 1) on 0 of them - the class is empty where it used to fire.
3. Replay (`tools/analysis/taa_resolve_replay.py` on the true `color_*` input of the burst): the station crop's flicker
   rms with S = 0 on the new build at or below 1.20 codes (what S = 0.7 delivered on run214's proxy input) and the
   gradient at or above the installed 9.44; the S = 0.7 run differs from S = 0 on the station crop by at most the
   fixture's oracle bound (the class is empty there, so any difference is the box on neighbouring sky).
4. User: no visible flicker on the stations under the pan with S = 0, no trail behind the station silhouette wider
   than a pixel, and no visible pop at the band edges on approach (the one-frame reset of section 4).

Sky-only classes (lasers, trails, dust, unreviewed distant ships) keep the pre-Run62 behaviour when S = 0, which was
accepted then; the run216 laser report was a null result for S = 0.7, not a requirement. After the default flips, the
code (the class code in the tests draw, c6.x, the s6 fallback fetch, the emitter bound and c23 in the columns program,
the launcher option) is retired in a cleanup batch, about 36 slots back on the tests draw (281 - 245 at 2026-09-21).

## 6. Cost (question 5, per draw)

- Already-routed fade draws: no new CPU work; in lazy mode the mask no longer differs from the application's 15, so the
  per-draw mask write and its undo disappear (two `SetRenderState` fewer per fade draw). The upload grows by 4 floats.
- Newly admitted hull draws: from an `overlay_node` refusal (eight state reads, the frequency getter, one identity read;
  about 1 us [I]) to a routed draw, 7.49 us lazy-ownership per routed draw (`route-per-draw-cost.md` [M]): 62 draws on
  the run214 stand are at most 0.46 ms per frame [I], the same cost those draws pay when the station is inside FogNear
  and opaque; no new peak, the far case now equals the near case.
- GPU: the depth fragment already runs on fade draws (RT2 bound, masked); the write adds 4 B (R32F) or 16 B (lane) per
  covered pixel of a far object. Resolve: no change, RESOLVE_BUDGET 2048 untouched. Tests draw: one to three slots for
  the `.a` vote form [I]; the 512 figure is a floor, the program is at 281 (ledger 2026-09-21; the fixture's
  RESOLVE_BUDGET rows carry the current count).

## 7. Verification (question 5, fixtures)

- Host: `verification/analysis/test_fade_region.py` - the register table (7 -> 9 rows, the static assertion), the
  classifier's identity change (a reviewed pair with a registers row and no `distance_fade_rows` row is a fade pair; the
  bracket's sampler mask stays 0 for it), the hysteresis eviction counter.
- `run_motion_output.py`, `faderoute` family: on routed frames of `routed`, `routed-perdraw`, `sentinel`, `original`,
  `behind` and the held frames of `hover`, the depth target over the quads carries the quads' z/w from the fixture's own
  rows within FP32 raster tolerance, `.b = w` and `.a = 1` (lane), the sentinel elsewhere; refused frames leave the
  sentinel. `hover` adds the age readback at its 390 frame (the one reset) and asserts no region reopen on the 449
  frames (the hold state in the age target). `overlay` becomes a fade-arm case (`fade_routed = 2`, `overlay_routed = 0`,
  RT2 written); `foreign` keeps its meaning by running the same pair at a fraction below 500 (`unmatched =
  fade_threshold`), and a new `hull` case draws `494fe349b8bc12ec/fffdabd910793aba` on its own node with no routed draw
  before it, fraction 1000: routed, owner, resolved shift a fraction of the raw shift. `production-zonly` / `seam-zonly`
  unchanged (the prepass parity oracle now also covers RT2 holes: add the RT2 interior-hole count to the check).
  **Done 2026-09-24** as two new `faderoute` owner cases, because the pinned zonly material draw is never routed:
  `seam-taa-fade-route-zonly-owner` (the z_only alias as a depth prepass of both quads, jittered by the route, then
  the routed fade pair at fraction 1000 over the fill, with a depth slope that makes a sub-pixel offset decide
  LESSEQUAL) counts 0 colour and 0 RT2 interior holes on all 12 frames; `seam-taa-fade-route-zonly-unjit-owner`
  (the same prepass through an unreviewed vs_1_1 the route leaves unjittered, `unjittered_depth_writers = 2` per frame)
  drops all 392 interior pixels in both colour and RT2 on the four jx > 0 frames and none elsewhere; colour and RT2
  coverage agree per pixel in both (ledger `temporal-resolve.md`, "fade owner: prepass parity over RT2").
- `run_temporal_pass.py`, lattice mode: one case with a far routed square whose depth switches sentinel -> valid once:
  the region opens once for L frames and the output is within the existing thin-region bounds; the "routed sentinel
  (glass)" row stays bit-identical; the `.a` vote rows are B's, re-baselined once with the encoding above.
- `run_linear_distance_fade.py` and the live fade script: unchanged and bit-identical (`fade_routed = 0` on the live
  script; the bracket's oracle does not see the arm).
- Flight: section 5. The `fade_route_frame` line and the `--taa-debug` burst are the evidence; no benchmark.

## 8. Native Windows

Documented D3D9 only: per-target write masks, MRT post-pixel-shader blending with per-target alpha, FP32 blend format
queries (all already required by the probe), one pixel-constant upload. No new cap, no wined3d-specific behaviour. The
exactness of `src * 1 + dst * 0` on R32F / RGBA32F under native drivers is unverified, as RT1's alpha-1 blend is; the
`faderoute` fixture's RT2 readback is the parity check to run on a native machine when one exists
(`platform-portability.md` gap list).

## 9. Alternatives considered

- **Second depth-only pass** (blend off, mask RT0/RT1 = 0, the same rows): exact and keeps B's flag on fade owners.
  Loses: a second draw call and vertex pass per fade draw (62 x about 9 us CPU plus the GPU vertex work on the run214
  stand), a depth-only variant per pair, and nothing gained - B's flag has no measured value at band distances.
- **Separate RT2 threshold** (route at 500, own at a higher value): the three-state middle band is today's flicker
  class; the background-mix argument does not distinguish 500 from 700 at band distances.
- **Route the run214 draws without depth** (option (c) of the stabiliser note, the overlay witness relaxed): the note
  itself found that a routed sentinel pixel gets neither the far weight nor a class change; the flicker stays.
- **Resolve-side owner** (treat a routed alpha-1 correspondence over the sentinel as far-plane geometry with the far
  stabiliser): no depth for the far gate, the line test or the fog march; slots in the resolve for a class that
  ownership removes.
- **Flag in RT2 `.g`** to keep `.a = 1` and B's flag: `.g` is the sun-share lane's channel in the RGBA32F
  configuration and a z/w duplicate elsewhere; two encodings by lane mode, and B's value at band distances is nil.
- **Z-write on for owners**: changes the engine's blending of every later band draw; not a route concern.
- **Drop the stabiliser without ownership**: the run221 flicker returns.

## 10. Unknown, and what settles it

- Settled 2026-09-24 (distance-fade.md section 8 [S]): the KC script `ShowSpace` sets `0x40000` unconditionally on the sector camera, the prepass walk has no class test (a tree whose root sphere reaches N is submitted depth-only, whole tree), so section 3's order-independent case holds in the sector view; exceptions without prepass depth are blended-material subsets, instanced bodies and cross-layer ordering. Superseded text follows.
- Whether stations in the band get the depth-only prepass in the sector view (camera `+0x270 & 0x40000`): a census of
  the `object_context` log of the run214 stand for `c78b4c68a87fce74` / `803ebfd17f79e413` draws with station node
  identities, or a `disassemble` task on the flag's writer in the frame routine `0x00472280`. Without the prepass the
  bound of section 3 holds; with it the depth is order-independent.
- Whether any vanilla far station family draws in the fade-band state with a VS outside the extended table: the
  flight's `overlay_node` count after the change (expected 0; a nonzero count names the VS).
- The visibility of the one-frame reset at the band's lower edge (section 4): the flight on approach.
- Native FP32 blend exactness (section 8).
- B's constant lane (c216.z is read by the motion fragment): to be settled with the B implementer before either
  change lands.
