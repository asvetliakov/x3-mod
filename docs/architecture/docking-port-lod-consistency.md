# Docking port across the engine LOD 2/3 boundary


Status (2026-09-14 late evening): NOT ratified; decision put to the user after run 19. The finding stands:
the far LOD never draws the lattice material, so the near/far step is the asset's own and exists in the
vanilla game; the current linear rule makes the near port ≈16 % brighter (encoded) than native, which
narrows the step slightly. Choices for the user: (a) native parity for the port (this note's
recommendation), (b) keep the linear rule (brighter near port, smaller step), (c) nothing until the
run-19 far/near pair is decoded. No source change until decided.

Design note, 2026-09-14, for ratification. Owns the decision for the docking-port pair
`4944d81dfe531b37/64bac8bb307eb896` at the LOD 2/3 switch. Facts from
[station-material-distance.md](../reverse-engineering/station-material-distance.md);
current route in [linear-station-source-over.md](linear-station-source-over.md), composite
contract in [linear-distance-fade.md](linear-distance-fade.md). Read-only: no build, no game,
no Wine; the run-36/39 logs and the untracked DDS were queried with bounded scripts.

**Ratified 2026-09-15** (orchestrator) after run 21 session B confirmed the step in vanilla: implement the native-parity blend-domain composite for the port pair in a later candidate (low priority; it removes the renderer's +16 % near-side excess, not the asset's LOD step).

## Decision

Leave the engine's LOD selection alone and keep the port on the fade route, but compose this
pair in the **native blend domain**: E carries `encode(L)` instead of `L`, and the composite
is `C = Q + (1 − q)·A` with no decode/encode (A is already the compatibility-encoded FP16
target). The near-side port then equals native's authored gamma-space source-over with
converted lighting, and the step at the boundary reduces to the asset's own LOD content step,
which native Windows shows identically. No option can make the two sides equal: at LOD 3 the
port material does not exist and its screen area is hull-textured (below), so "match the LOD-3
opaque look of the port texels" has no referent. The linear-domain rule ratified for the
asteroid fade stays for the six asteroid pairs; this is a per-pair blend-domain flag.

## What the captures establish

- **LOD 3 draws the port area with the hull material, not the lattice.** Run 36, node
  `2afb76f0` (model `5427`): frame 1699 (LOD 3) is one DEFAULT draw (index 212, 2592 tri)
  binding s0 `1776` (DXT1 1024²/11), s1 `1778` (DXT1 1024²/11), s2 `1740` (32² dummy), s3
  `1742` (cube). Frame 1917 (LOD 2) is 14 draws; the port draw (269, 6 tri) binds
  `1884/1885/1886` (DXT5/DXT5/DXT1 1024²/11) + `1740` + `1742`, and the largest sibling
  (279, BUMP `ca6bfa4a…`, 8569 tri) binds `1776/1777/1778/1740`. So the merged LOD-3 body is
  the main hull's diffuse and specular through the no-bump DEFAULT pair (`8759c783…` CTAB:
  Diffuse s0, Specular s1, LightMap s2, CubeMap s3), and the lattice triple is absent.
  (Record order in these logs: `draw`, `object_context`, then that draw's seven `texture`
  rows; attributing the rows to the preceding draw gives the wrong answer.)
- Run 39 frames 9163/11940: the lattice diffuse `1850` is bound by four draws per frame, all
  LOD 0–2 (`f1b0e820…` 8 tri LOD 0, two-sided DEFAULT `63f96eba…` 16 tri LOD 1 with
  `1850/1852` and no bump, port `64bac8bb…` 12 tri LOD 2, `e6794b6e…` 8 tri LOD 2); never by a
  LOD-3 draw. An opaque lattice look exists natively only on those small other parts.
- **What alpha < 1 encodes.** Mip-0 classes of `metal_argon_lattice_windowedgrid_diff`:
  `α = 1` 50.8 % of texels, mean RGB (93, 93, 95); `α ∈ [0.43, 0.63]` 48.0 %, mean RGB
  (58, 68, 82), bluish; `α ∈ (0.63, 0.99)` 1.2 %. Row/column profiles have 30/8 transitions
  and a mean alpha-class run of 147 px: large window panes in a metal frame, glass, not holes
  (no zero texels). Opaque linear luma 0.0846 versus source-over-black 0.0729 (ratio 0.862):
  the glass texels are both darker and more transparent, so alpha × colour is not separable.
- **Blend-domain arithmetic at ᾱ = 0.757 (port UV domain, detached fixture).** Over a black
  interior, native gives encoded `0.757·p`, linear luminance `0.757^2.2 = 0.542·p_lin`; the
  current linear rule gives linear `0.757·p_lin`, encoded `0.757^(1/2.2) = 0.881·p`. The
  current route therefore reads the near port **+16 % encoded (+40 % linear)** brighter than
  native over dark backing, converging to native only where the backing matches the port.
  The LOD-3 hull pixel has no such term (opaque converted DEFAULT, native-parity). The
  linear rule thus adds a near-bright/far-dark step on top of the authored one — the sign the
  user reports. Over a hull of equal encoded brightness the two rules agree, so the size of
  the current excess depends on what is behind the port (unknown, below).
- Boundary bracket unchanged: `D ∈ (118 833, 129 090]` for model `5427`, ≈52–57 px port
  width, Euclidean range constant to 0.18 %, selection at `0x0047cfe0` per node per frame with
  strict `<` and no hysteresis. A 48² HDR window at the projected node centre gives mean luma
  0.0091 (1699) vs 0.0090 (1917): the body as a whole does not step; only the port area can.

## Options compared

1. **Port composite with α forced to 1 on lattice texels.** Its premise fails: LOD 3 never
   renders those texels, so an opaque lattice is not the far look; it also destroys the
   authored glass at 170 px, where native and the mod must agree. Loses.
2. **Promote the pair to an opaque converted draw (window mask for later glass/emission).**
   Same false premise, same near-side breakage, plus a new material role, a Z-write change
   (the native draw writes no depth) and a new fixture family. Loses; the mask idea can
   return with a glass/emission feature, not as a LOD fix.
3. **Hysteresis or bias at the selection.** Site is known (`0x0047d429..0x0047d46e`, threshold
   `LODrec[+0x34] × float[*(0x606f34)+0x760]`, result `node+0x14c`); a patch there touches
   every node of every model, private layouts, and the renderable-bit side effects at
   `0x0047d4d7`. It moves or steadies the flip and removes nothing: both sides remain what
   they are. The quality float at `+0x760` and the level at `+0x768 ≥ 3` already bias
   thresholds without any patch; if the user wants the flip farther away that is a game
   setting, not renderer work. Loses.
4. **Draw both LODs and blend.** The engine submits one LOD; the other's VB/IB are not
   submitted that frame, so the renderer would have to replay retained geometry with its own
   world — a new subsystem for a two-frame effect. Rejected.
0. **Do nothing, verify per side.** Cheapest, but keeps the +16 % near-side excess, which is a
   renderer-introduced departure from native and the likeliest amplifier of the complaint.

## Cost, portability, TAA, the flip frame

- Hot path: none per draw. Producer: one `encode` per port fragment (the opaque route already
  applies the same safe transfer at `final_rgb`); composite: two transfers per composed pixel
  removed. Net at or below today's `48f` B/px region cost; bracket cost unchanged.
- Windows: documented D3D9 fixed-function blend and MRT only; identical on native and Wine.
- TAA: unchanged. Port pixels stay Z-write off, M marks them current-only; the first LOD-3
  frame writes an opaque hull with depth, and the history-rejection outcome for those pixels on
  that one frame is untested (one-frame ghost possible, same as any authored LOD pop).
- The flip frame: the port draw is either submitted or not; no route state spans frames, so
  the mod's frame sequence at the boundary equals native's up to per-side parity.

## Verification

- **Detached** (extend `verification/probe/port_distance_fixture.cpp`, real pair, real DDS,
  captured constants): draw an opaque backing first (black; hull stand-in once `1776/1778`
  are identified), then the port three ways — native fixed-function source-over of the
  native PS, the current linear rule, the native-domain rule — at 170/85/43 px; and the
  LOD-3 stand-in (backing through the DEFAULT pair, no port). Pass: native-domain composite
  equals the native reference within FP16 tolerance at every distance; report the linear
  rule's excess (predicted 1.16 encoded over black, 1.00 over equal backing).
- **Run 19 pair** (same port, F8 at ≥ 100 px and below 52 px): decode the near frame's
  `fade_region rect` for the port and a routed sibling's rect on the same model; project the
  same part `aabb` (logged with the rect) into the far frame with the far camera. Acceptance is
  **per side**: near port/hull encoded ratio within tolerance of the fixture's native-domain
  prediction; far ratio recorded as the authored LOD step. Depth at the near rect answers what
  is behind the port (far plane ⇒ black interior; station depth ⇒ hull).

## Unknowns

- What lies behind the port at LOD 2 and whether the LOD-3 mesh has a surface at the port's
  location: run-19 depth at the rect, both frames.
- The LOD-3 merged material and its files (`1776` DXT1 diffuse, `1778` DXT1 spec): a bounded
  archive read of each `argon_sy*.pbb` LOD-3 `PART` first group (single group, so the
  unknown face stride does not block) → material index → `MAT6` names.
- Whether the asteroid fade should move to the same domain: separate decision, its own fixtures.

## Band-limited cross-fade toward the LOD-3 look

Design note, 2026-09-15, for ratification. Two options the user asked for, evaluated together:
**A** — on the fade route, drive the port pair's composite alpha toward 1 and its colour toward
a hull-like target as the port shrinks from `W_hi` to the LOD boundary `W_lo`; **B** — keep
drawing the LOD-2 port when the engine has switched the node to LOD 3, by replaying the last
seen port draw with the node's current rows. Read-only: no build, no game, no Wine. The run-36,
run-39, run-47 and run-48 logs and the RGBA16F captures under `/tmp/x3-bottleX3-run*/` were
queried with bounded scripts (scratchpad only; nothing tracked).

### Recommendation

**Ship neither now.** Both options remove a near/far step that the captures do not contain:
the two same-node LOD 2→3 crossings that can be measured show no radiance or pattern step in
the port area, and the one port that is genuinely dark inside (model `543f`) is dark at
109–146 px, far above any boundary, with its LOD-3 side uncaptured. If the user still wants an
experiment after the run below, A is the one to build (default-off launcher flag, driven by
the node's LOD ratio rather than screen width); B is a new cross-frame replay subsystem whose
depth policy cannot be chosen without LOD-3 depth data, and it loses on cost and risk.

### Evidence correction first: run 20 measured asteroids

The run-20 table in [station-material-distance.md](../reverse-engineering/station-material-distance.md)
("a genuine far port", part `0f768ad8`, 37 px far / 238 px near, luma 0.216 / 0.203) is not a
port. In `session-20260915-002408-212.log` the `fade_region index` is the draw index, and at
frame 43990 the three `0f768ad8` lines (indexes 24–26) sit inside draws 24–26:
`vs=167eb2d5629ab9d3 ps=d44db87778a43b61`, 1712 triangles — the reviewed **asteroid** pair of
[linear-distance-fade.md](linear-distance-fade.md). Frame 44490's six `0f768ad8` rectangles are
the same pair (1712/3552/3066/5106 tri). The port pair's draws are index 57 (43990; node 51860,
model `546b`, LOD 2, 120 tri, no fade rectangle) and index 161 (44490; node 51770, model
`5471`, LOD 2, 12 tri, rectangle 17×14 px). The run-21 sentence "the port radiance moves 6 %
between the far LOD 3 hull-textured area and the near LOD 2 lattice draw" therefore has no
support; both run-20 frames are LOD 2 and neither rectangle is a port. The run-47 pair is
correctly identified (2-triangle port draws, model `542a`; handles 52262 and 51982 are two
station instances of that model, not one node re-created). The depth captures of runs 36 and 48
are `-1` sentinels (no depth readback), so nothing below uses depth.

The `draw` line plus its `object_context` line (same index) carry node handle, model and LOD for
every port draw; the "no lod on fade_region" gap of the run-20 note is closed by that join.

### What the eye sees across the flip (measured, same node both sides)

Port-area rectangles are the port part's box (`fade_region aabb`, centre/half-extent in
1/65536 POSITION0 units) projected through the draw's own `c24..c27` (the profile's matrix
window; logged after the `draw` line). Self-check: run 39 frame 1812 draw 267 projects to
(1105,280,1161,312) against the logged padded rect (1103,278,1164,314). Luma is linear, from
the RGBA16F scene captures; "dark" is the fraction of pixels below 0.05.

| Node (model) | Side | Frames | Port area | `w` | mean | median | p5 | p95 | dark |
|---|---|---|---|---:|---:|---:|---:|---:|---:|
| 52721 (`542b`), lit | LOD 2, 12-tri port drawn | run 48 8791–8798 | 47×38 px | 106–117 k | 0.107 | 0.105 | 0.058 | 0.144 | 0.03 |
| 52721 (`542b`), lit | LOD 3, one 2517-tri body | run 48 4817–4824 | 36×27 px | 122–134 k | 0.101 | 0.103 | 0.070 | 0.125 | 0.04 |
| 52536 (`5427`), dark | LOD 2, 6-tri port drawn | run 36 1917 | 47×37 px | 116–125 k | 0.0214 | 0.0096 | 0.009 | 0.079 | 0.92 |
| 52536 (`5427`), dark | LOD 3, one 2592-tri body | run 36 1699 | 41×32 px | 126–135 k | 0.0206 | 0.0097 | 0.009 | 0.074 | 0.93 |

The centre of the rectangle (the interior, away from the frame; middle 50 % on `542b`,
middle 60 % on `5427`) is 0.114 → 0.093 on `542b` and 0.036 → 0.032 on `5427`; the local contrast (mean |∂x luma| / mean) is 0.63 → 0.69
on `5427`. Across the flip the port area keeps its mean, its median, its dark fraction and its
texture contrast to within the frame-to-frame noise of the same node (±5 %). The LOD-2 lattice
at 47 px is not "a lattice over a black interior": its 5th percentile is 0.058 on the lit
station, above the hull's own shadowed pixels. The `5427` node reads dark on both sides in
every capture (run 39 1812 at 56 px: 0.024, dark 0.90; run 48 LOD 3 at 28–45 px: 0.022–0.090)
because that station's port face is in shadow, not because of the LOD.

The one dark port interior in the data is model `543f` (node 52029): 109–146 px wide at
`w` = 64–92 k, mean 0.048–0.083, **dark fraction 0.61–0.81**, interior mean 0.055. That is the
authored bay (170×73 px in run 39 on the same part `1a6958b0`) and it is dark at three times
the boundary width. No capture has this node at LOD 3, so the step the user describes — if it
is this asset — is unmeasured on both sides, and its LOD-3 hull brightness is unknown.

Flip widths are per model, not a screen constant: `5427` flips at 52–57 px, `542b` between
47 and 36 px (`w` 117 k → 122 k), `5471` is still LOD 2 at 17 px (`w` 443 k, run 39). The
engine's rule is `D < LODrec[+0x34] × quality`, a view-space max-norm — a port's screen width
depends on the port's size within its model, so `W_lo` cannot be one number.

### Option A: band-limited cross-fade on the fade route

Rule as asked: `t = smoothstep(W_hi, W_lo, width)`, `alpha' = lerp(alpha, 1, t)`,
`colour' = lerp(colour, target, t)`; frames at width ≥ `W_hi` are bit-identical to today.

- **Target.** The hull look at the port position is the node's own LOD-3 pixels, which the
  table shows are already what the LOD-2 lattice averages to (0.101 vs 0.107 on `542b`). A
  flat tint at the pair's lattice colour with alpha 1 is the wrong target: the measured
  LOD-3 area on `542b` has cv 0.25 (textured hull), and on `5427` it is 90 % dark. A colour
  drift toward a hull mean needs that mean per node per frame; the route has no hull sample
  for the port rectangle before the port draws (B backs up A before the source and could
  average the pre-draw rectangle, which at that point contains the hull the port is drawn
  over — that is the only cheap target: the pre-draw mean of B|R).
- **Where.** In the composite (`finish`, region-scissored, program `source_over_composite`):
  it already reads `s0 = B` (pre-draw A) and `s1 = E` with the source alpha in `E.a`; `t` is a
  per-DIP constant, the target is a per-DIP constant (mean of B|R, one extra small readback or
  a 1-pixel downsample pass — the readback route is the expensive part, a mip-chain of the
  region is not available on a render target without an extra pass). The producer side is
  unchanged; the marking stays current-only.
- **Driver.** Use the node's LOD ratio, not screen width: the route already reads the part's
  box from private game memory behind the exact-executable gate; the same gate can read the
  node's selection input and `LODrec[+0x34]` so that `t` is a function of `D / D_switch`
  (`t = 0` at 0.7, `t = 1` at 1.0). That makes the band model-independent and avoids fading
  `5471`'s port, which never flips in the captured range. If screen width must be used, the
  only defensible values are `W_hi` = 120 px and `W_lo` = 57 px for `5427`-class models,
  and they are wrong for `542b` (already LOD 3 at 36 px) and `5471`.
- **Cost.** Per composed port pixel: two `lerp`s and one constant read, on a rectangle of
  0.2–11 ‰ of the viewport (run 48 ports 17–146 px wide; 1–2 port draws per frame, up to 14
  fade rectangles of all pairs in a captured frame). If the target is the pre-draw mean: one extra `24f`-class pass per port DIP
  (region-scissored reduction) or a small readback, the latter a pipeline sync and not
  acceptable on the hot path; the reduction pass is bounded by the same `f ≤ 0.011`. Nothing per
  draw outside the fade bracket.
- **TAA.** Port pixels are M-marked current-only today and stay so; a changing `alpha'`
  across frames does not cause history rejection because those pixels have no history to
  reject. The hull pixels under a shrinking port keep their history (they are not in M unless
  covered). At the flip frame the composite is already opaque (`t = 1`), so the LOD-3 hull
  replaces an opaque lattice-with-target-tint: the residual step is texture pattern only, and
  the first LOD-3 frame is the one-frame history miss any LOD pop has.
- **Failure modes.** Oblique ports (narrow while near) fade early under a width driver — a
  second argument for the ratio driver. Several ports on one node take the same `t`, fine.
  Ships: the pair is used by station models only in run 48 (`542a`, `542b`, `543f`, `546b`,
  `5471`, all LOD 2); the ship the user saw (run 47 node 53194, model `4fef`, LOD 0) draws
  opaque, is not on the fade route and is a different mechanism. Resolution: with a width
  driver `W_lo`/`W_hi` are screen-pixel constants only if the game's LOD thresholds are in
  screen space; they are not (view-space max-norm), so the widths drift with FOV and
  resolution — another reason for the ratio.
- **Windows.** Composite-program constants and a scissored pass only; documented D3D9.
  Reading the LOD threshold is private-layout, in scope under the same exact-executable gate
  as the bound, and fails closed to `t = 0` (today's image).
- **What it cannot do.** On both measured crossings the fade would replace a port area that
  already matches the far look with a tinted one that does not (0.107 → forced toward a tint,
  then the hull at 0.101). On `543f`, if its LOD-3 hull is brighter than the 0.055 interior,
  A would brighten the bay on approach toward a hull mean it must first measure — that is a
  visible change of an authored dark bay at 60–120 px, before the boundary.

### Option B: replay the LOD-2 port draw at LOD 3

- **Node identity survives the switch.** Run 36 node `2afb76f0`, handle 52536, model `5427`,
  is the same handle at LOD 3 (frame 1699) and LOD 2 (1917), and the same handle in run 39
  (1812) and run 48 (4817–10670). `node_handle` is the per-instance key; `part` is the mesh
  key (same part pointer across nodes of one model within a session; different across
  sessions). The proxy sees both in `object_context` for every draw, including the LOD-3 body.
- **Transform.** All parts of a node share one world matrix: in run 36 frame 1917 the port
  draw (269), the 8569-tri BUMP sibling (279) and the LOD-3 body (1699/212) carry identical
  `c28..c30` (scale 5701.4, translation (119750, 0, −100500)); the projections above use
  the LOD-3 draw's rows for the port box and land on the visible port. So the replay's WVP
  and world can be taken from the LOD-3 body draw's `c24..c30` verbatim.
- **Depth.** Unknown. The depth captures are sentinels, so whether the LOD-3 body has a
  surface at the port position (coplanar → z-fight; in front → occluded; recessed → fine) is
  not established. The replay must be Z-tested against the body (drawing it with Z off paints
  it over foreground objects), so it needs `D3DRS_DEPTHBIAS`/`SLOPESCALEDEPTHBIAS` (documented,
  caps-gated) with a value chosen from data that does not exist yet.
- **Geometry retention.** The port draw's VB/IB are the model's LOD-2 buffers
  (`motion_input vb=4748 ib=4747`, `lifetime_verified=1`). The geometry-lease layer
  ([geometry-leases.md](../verification/geometry-leases.md)) can hold native references across
  frames legally (documented COM AddRef, revision-checked, 64 frames / 8192 leases / 512 MiB)
  and refuses replay under a newer revision; it does not qualify live replay today and keeps
  the whole LOD-2 VB alive while the engine has dropped to LOD 3. Textures (the three lattice
  DDS), the declaration, the sampler state, the PS constants and the VS light constants of the
  last port draw would also be retained; the VS is `4944d81d…`, which no LOD-3 draw uses, so
  its camera-dependent constants (`g_mViewInverse` c34..c36, lights) must be synthesised from
  the DEFAULT body draw's registers through a per-pair mapping — a new ABI surface.
- **Cost.** 1–2 port draws per frame in run 48 (2–12 triangles each): the GPU cost is nil; the
  CPU cost is one state save/set/restore per replayed port, comparable to one fade bracket,
  plus the retained buffers. Stop replaying below ~24 px: the engine's own 17-px LOD-2 port on
  `5471` measures mean 0.078 with dark fraction 0.39 (a dark dot), and the lattice's 1024² grid
  has < 4 texels per pixel there.
- **TAA.** The replayed draw takes the fade route (source-over pair, gate 4 on the motion
  route), so its pixels are M-marked current-only every frame, over hull pixels that would
  otherwise keep history: a permanently unaccumulated patch on every far station.
- **Windows.** The draw itself is documented D3D9; the trigger (node at LOD 3 that had a port
  at LOD 2) is private-layout but read through the existing `object_context` scope, so no new
  gate. Unverified natively like the rest of the route.
- **Why it loses.** It draws the lattice onto a hull that, on both measured nodes, already
  reads as the lattice did; it needs depth data that has not been captured; it is a
  cross-frame replay subsystem (retention, constant synthesis, bias, stop width) for an effect
  the fixtures cannot bound without a LOD-3 depth capture. Option 4 of this note ("draw both
  LODs and blend") was rejected for the same reason; B is that option without the blend.

### Verification that would prove A (if built)

Detached: extend `verification/probe/port_distance_fixture.cpp` (real pair, real DDS,
captured constants) with widths 40, 57, 80, 120, 170 px over two backings (black; a flat
0.10-luma hull stand-in), the composite three ways — native fixed-function, today's route,
the band rule with `t` from the width and from an injected ratio — and assert: `alpha'` and
composite luma monotone in `t`; frames at ≥ 120 px (and any frame with `t = 0`) bit-identical
to today's composite; at `t = 1` the composite equals the target within FP16. Host: a
`test_fade_region` case for the ratio read that fails closed to `t = 0` on any layout doubt.

User run (needed before either option, and cheaper than both): the station that shows the
blackening, three F8 captures of the **same** node — at ~150 px port width, at ~80 px, and
after the flip — then join `draw`/`object_context` (node handle, model, LOD) with
`fade_region` (box, rect) and project the box through the LOD-3 draw's rows as above. If that
node is `543f`-class and its LOD-3 port area is markedly brighter than the 0.055 interior, the
step is real for that asset and A with the ratio driver is the mitigation; if not, the
perception is exposure/tonemap and neither option applies. Acceptance for A in game: the user
sees no tint change above the band and no pop at the flip on that node.

### Unknowns

- LOD-3 brightness at the `543f` bay (no capture) and LOD-3 depth at any port (sentinel
  captures): the run above with depth readback enabled settles both.
- The LOD threshold read (`LODrec[+0x34]`, quality at `*(0x606f34)+0x760`, `node+0x14c`) as a
  fail-closed proxy read: a bounded disassembly note before any A build.
- Whether the `543f` darkening the user sees is the bay's authored interior under auto
  exposure (AgX, +1.5 EV cap) rather than any LOD: the vanilla comparison was eyes-only.
