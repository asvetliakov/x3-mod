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
