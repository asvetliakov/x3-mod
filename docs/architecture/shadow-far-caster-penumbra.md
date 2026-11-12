# Sun-shadow casters far toward the sun: sub-penumbra detail

Design note, 2026-11-13, for the FL 71 A "hexagonal blanket" (triage in
`verification/results/fl71a-shadow-lattice/`; owning pass notes
[shadow-cascades.md](shadow-cascades.md), [shadow-replay-gates.md](shadow-replay-gates.md)
"Alpha-tested casters", [shadow-caster-retention.md](shadow-caster-retention.md) "Alpha-tested
casters retained"). Question: how should the pass treat a caster far toward the sun so that
detail smaller than the sun's penumbra at that distance (the 83 u lattice at 137,000 u, and in
general) never reaches the hull, while near shadows stay exactly as they are. No production code
was changed for this note.

## Decision

**Test an alpha-tested caster's alpha at the penumbra scale.** The lattice is not geometry and
not an apply-filter effect: it is the station hull's alpha-tested grille texture, whose cut-outs
the caster program resolves at the fine cascades' mip levels and fills at the coarse ones. The
texture's own mip chain is already a prefiltered coverage map, so the fix is a per-pixel mip bias
in the alpha caster pixel program (`shadow_replay_pass.cpp`, `alpha_pixel_words`):

    delta  = (z_cam - v0.x) * range_k            caster depth toward the light from the camera plane, world u
    p      = 0.0093 * max(0, delta - 2,000)      penumbra of the 0.53 deg sun, with a 2,000 u dead zone
    bias   = log2(max(1, p / texel_k))           mips to coarsen so the alpha footprint is the penumbra
    alpha  = texldb(s0, uv, bias).a ;  texkill(alpha - threshold)   (today's test, today's threshold)

Per cascade three constants (`z_cam` normalized, `range_k * 0.0093 / texel_k`,
`2,000 * 0.0093 / texel_k`), uploaded with the alpha pair's existing threshold constant; five
arithmetic instructions and `texldb` for `texld` (ps_3_0 program 5 -> about 10 instructions).
The apply program, the cascade set, retention and the depth-only path are untouched. Inside the
dead zone (own ship, docking range) every map is bit-identical to today's; beyond it hole
visibility falls with distance exactly as the penumbra law says, in every cascade alike, so the
lattice/speckle seam that swept the hull with camera motion is gone with the lattice.

Backstop, designed and parameterised but not shipped now: a per-tap far-blocker cut in the apply
(§ Alternatives, A') for *geometric* holes (girders, open frames) if a flight shows one.

**Implemented 2026-11-13** (not flown; evidence in
[../verification/directional-shadows.md](../verification/directional-shadows.md), "FL 71 A: far-caster
penumbra mip bias"). Layout as built: PS c0 the threshold (unchanged), c1 = (z_cam, a, b, z_cam a - b)
per map with a = range_k 0.0093 / texel_k / 2^1.5, b = 2,000 0.0093 / texel_k / 2^1.5 (`shadow_replay_penumbra`,
`shadow_replay_projection.h`; `ShadowReplayMapList::penumbra`, filled by the owner for a cascade with
alpha issues), def c2 = (0, 1, 15, 0); program `mad r1.x, -v0.x, c1.y, c1.w` / `max 1` / `log` /
`min 15` / `mov r1.xy, v1` / `texldb`, then today's test: ten slots. Review fix (same day): a 1.5-mip support
offset, folded into a and b, so bias = max(0, log2(p / texel_k) - 1.5): the trilinear fetch of a box-filtered
chain averages over about 2^1.5 of the level's texel, and without the offset the filtered footprint was 2-3x the
penumbra (at ref 1 the 33 u holes went at 3,000 u, where p is 9 u). The bias is bounded to 15, the end
of texldb's documented range; the sampled level stays inside the texture's chain by the sampler's
documented LOD clamp, so a texture magnified in a fine cascade still reaches its last level. The
threshold and the map's c1 go up in one `SetPixelShaderConstantF` when either changes: a map's first
alpha issue costs the call it cost before, each further map with alpha issues one call more.

## Units

The pass notes use 5,000 u = 1 km ([shadow-cascade-cost-policy.md](shadow-cascade-cost-policy.md),
[space-shadow-survey.md](space-shadow-survey.md)): the render/world space of the camera rows, 5.07 u per
metre measured against a 16.0 km HUD readout
([station-material-distance.md](../reverse-engineering/station-material-distance.md)); the engine's
int32 object positions are a different space, 500 per metre
([game-scale-and-ship-classes.md](../reverse-engineering/game-scale-and-ship-classes.md), measured from
the EXE). The triage and the brief read units as metres;
their "137 km, 83 m, c0 texel 0.244 m" are 137,000 u (27 km), 83 u (17 m) and 0.244 u. Every
figure below is in units. The decision does not depend on the conversion: the penumbra is an
angle times a distance in the same units, and the ratio penumbra / pitch is 0.0093 x 137,000 / 83
= 15 either way.

## What the capture shows (frame 9607, measured)

Scripts beside the triage results; captures and the session log read by field, never whole.

| Measurement | Result |
| --- | --- |
| Station records at frame 9607 (`shadow_replay_caster`, origin 134,963-136,321 u along -forward, 27 records within 20,000 u lateral, all retained) | cascade masks: 16 records in all five cascades, 5 in c1-c4, 2 in c2-c4, 4 in c3-c4; 168,911 primitives; biggest record 26,884 primitives (the alpha-tested hull part of FL run 44, retention note) |
| Alpha casters that frame (`shadow_alpha_casters`, `shadow_retention_frame`) | ready=1 seen=20 tested=20 opaque=0, no refusals; retained_alpha=33 issues |
| Receivers within 6,000 u view depth, first-containing cascade, nearest texel (`station_share.py 0 6000 9607`) | c1: 37,554 px (step 4), station-occluded 31,478, lit 3,334; c2: 112,587 px, station 68,982, lit 26,179 |
| Hole share inside the station footprint (`hole_share.py 0 6000 100 9607`) | c0 0.316, c1 0.159, c2 0.030 |
| One 220 u world window around a c1-first receiver (-509.7, 100.3, 2980.6), every cascade (`same_window.py 9607 110`) | station share c1 0.894, c2 0.986, c3 1.000, c4 1.000; the c1 "holes" hold a nearer caster's depth (the hull), not empty |
| c2 depth at the c1-hole texels of that window (`hole_depth.py`) | median -3 u from the c1 plate's median, p5/p95 -29/+32 u: the same surface, rendered solid at 3.66 u texels and gapped at 0.73 u |
| Replay and apply CPU that frame (`shadow_replay_depth`, `sun_shadow_apply_frame`) | replay 232.7 us for 599 issues (0.39 us each); apply issue 30.9 us (median 21.5 us over the last 2,000 applied frames) |

The last two rows are decisive. Real holes would show the hull *behind the plate* in every cascade
at every texel size; here the coarser map fills them with the plate itself. The alpha caster pair
samples the material's diffuse alpha under its own sampler state, not the engine's (`bind_alpha`
sets WRAP, LINEAR min/mag, LINEAR mip, MIPMAPLODBIAS 0, MAXMIPLEVEL 0, no sRGB), so each cascade
tests the alpha at the mip its own texel footprint selects:
c1 (0.73 u) resolves the grille's cut-outs (16 % share), c2 (3.66 u) sees the intermediate mip
where the averaged alpha crosses the threshold in three-armed stars (3 %, the triage's 260-307 u
stars), c3 (18 u) and c4 (146 u) average the cut-outs above the threshold and draw a solid plate.
Camera motion changes which cascade is first-containing per hull pixel, so the boundary between
lattice, stars and solid sweeps across the hull. The apply filter is not involved (the triage's
nearest-texel reconstruction reproduces the lattice from the maps alone).

Physically the cut-outs are real (the game's alpha test discards them, one sees through the
grille), so the brief's penumbra argument stands: at 137,000 u the sun's 0.0093 rad disc smears
them over 1,274 u, fifteen pitches, and nothing of them can reach the hull. The mip bias puts the
alpha test at that footprint. With the texture's alpha binary and 16 % open, the biased level's
alpha is about 0.84, above any sane GREATEREQUAL threshold (the c3/c4 maps already prove the plate
survives the coarse levels for this texture), so the plate casts a solid hard-edged shadow.

## The rule in detail

- `v0.x` is the caster pixel's normalized sun depth (`o1.x` of the alpha vertex program); the
  camera plane is at `L / (L + B)` of each cascade (0.9983 / 0.9901 / 0.952 / 0.80 / 0.50 for the
  flown set: receiver medians 0.9984 / 0.9988 / 0.958 / 0.806 / 0.504 in `project.py`). `delta`
  is the caster's distance toward the light from that plane, not from the receiver: exact for
  receivers near the camera (own ship, the hull around it). Two limits. (i) A receiver itself far
  toward the light: delta over-estimates the caster-receiver distance, the bias is too large and
  holes the receiver should still show are erased (the error is at most p / 2 on a shadow outline
  that the far cascades' texel already quantises coarser: at 25,000 u the c4 texel is 146 u, the
  over-blur at most 116 u). (ii) A caster near the camera with its receiver far behind it: delta
  under-estimates, the bias stays 0 and the lattice stays sharp on that far receiver, exactly as
  before this change.
- Dead zone 2,000 u: the own-ship column, docking range and a complex's mutual shadows within
  400 m display are bit-identical to today (bias 0 whenever `p <= 2^1.5 texel_k`, i.e. up to
  delta = 2,000 + 2^1.5 texel_k / 0.0093: 2,074 / 2,223 / 3,114 / 7,569 / 46,551 u per cascade, with
  the support offset). The law is continuous at the dead zone's edge.
- Beyond it the footprint equals the penumbra in every cascade, so the cascades agree on the
  alpha test wherever `p` exceeds their texel; the remaining per-cascade differences are the maps'
  own point sampling (as today for opaque geometry). Transition zone (measured on the fixture's
  grille, 83 u pitch, 33 u holes, 16 % open; `docs/verification/directional-shadows.md`, "FL 71 A:
  far-caster penumbra mip bias"): at the engine's usual GREATEREQUAL ref 1 the holes shrink from
  3,000 u and are gone at 4,500 u (physically a 33 u hole lasts until p reaches ~33 u, delta
  ~5,550 u); at ref 128 they last to 8,000 u and are gone at 10,000 u. Inside it the binary test
  yields consistent partial holes of one size in every cascade: the physical soft lattice rendered
  as a hard one, without the sweep. The flown station's transition depends on its texture's hole
  size, threshold and mip alignment. A texture whose
  open share exceeds `1 - threshold` (a filigree more than ~50 % open with ref 128) vanishes
  entirely once averaged, as it already does in c3/c4 today; a binary test cannot cast a partial
  shadow. Both are limits, recorded, part of the kill criterion.
- `texldb`'s bias is clamped by the runtime to [-16, 16); the largest value here is
  log2(1,274 / 0.244) = 12.3 (c0, 137,000 u). Beyond the texture's last level the hardware clamps
  to the smallest mip, which is the intended "average alpha".

## Cost on the hot path

- GPU: five ALU instructions per alpha-caster pixel in the replay, trilinear fetch as today. The
  alpha issues are 20 live + 33 retained per frame here across five maps; even a part filling a
  4096² map costs 16.8 M x 5 ALU, well under 0.05 ms [I]. The apply is unchanged at 1.57 ms
  (5120x1440, run325 [M], [../verification/frame-cost.md](../verification/frame-cost.md)); the
  cascade replay stays at its 1.25 ms class [M, run325].
- CPU: one `SetPixelShaderConstantF` per cascade bind of the alpha pair (five per frame, under
  2 us total [I]) on top of the pass's 232.7 us replay issue [M, frame 9607].
- Memory, resources, Reset: nothing new; the constant rides the alpha pair that exists.

## Cascade selection, TAA, retention

- Cascade selection is unchanged (first-containing, 10 % band, far fade). The sweep disappears
  because the alpha test's world footprint no longer depends on the cascade once `p > texel_k`.
- TAA: the maps are jitter-independent and the bias is a deterministic function of depth; no
  stochastic term, no per-frame kernel change. As the ship closes on a far caster the bias falls
  with log2(delta): the holes appear continuously, never pop. SETA does not enter (a distance law,
  not a time bound).
- Retention: retained alpha records carry texture and threshold already; they take the same
  program and constants. Fade-owner casters (the engine's 950-999 permille band) are untouched.
  The minimum-footprint gate and the importance order are untouched.

## Native Windows

`texldb` and `log` are ps_2_0+ documented instructions; the sampler state is the one the pass
sets today; no capability query, no new surface. The constant layout change is inside our own
authored pair (`alpha_pixel_words`, bound by `bind_alpha`). Behaviour on a native device is the
same arithmetic; only the mip-selection rounding (trilinear weights) is vendor-specific, which the
threshold test absorbs as it does today. Not verified natively (the user cannot test Windows);
the WSL native-validator harness can at least validate the new bytecode.

## Verification

1. Program: the hand-assembled pair validated by the native SM3 validator harness
   (`verification/results/hull-widen-native-validator/`) and the fixture's `shadow-alpha-casters`
   case (`run_motion_output.py`, mode `shadowalpha`), whose `opaque_alpha_pass == opaque_depth_only`
   hash identity must hold unchanged.
2. New fixture case in the same mode: an alpha-tested plate with a hexagonal cut-out texture
   (pitch 83 u, 16 % open, full mip chain box-filtered on the host) over the flat receiver at
   delta = 500 u (inside the dead zone), 10,000, 50,000 and 137,000 u, cascades 1-3 of the flown
   set. Acceptance per map: the share of plate texels killed, measured as `hole_share.py` does,
   must be (a) equal to the current program's map at 500 u (hash-identical map), (b) within 2 %
   of the host's prediction from the texture's mip chain at the biased level at the other three
   distances, and (c) monotone falling with distance, 0 at 137,000 u in every cascade. (d) The
   per-cascade shares at 50,000 and 137,000 u agree within 2 % (no seam). This is the brief's
   "hole visibility falls with distance per the penumbra law; near frames pixel-identical".
3. Flight (FL): at the FL 71 A stand, F8 frames with the Titan under the station;
   `same_window.py` shares agree across cascades within 2 % and the hull shows one solid shadow;
   docked/near-station frames compared with the previous build's maps by hash.
4. Cost: `shadow_replay_depth us=` unchanged within noise on the same stand.

## Kill criterion

Drop the mip law (and go to the backstop A') if any of these holds after the fixture passes: a
lattice or blanket still sweeps the hull from a far caster whose c1/c2 maps agree
(`same_window.py` shares within 2 %: the holes are then geometry, not alpha); a far alpha
caster's shadow pops on and off between frames (coverage near its threshold; the law has no
partial shadow); at ref 1, holes wider than the penumbra erased (a near caster's cut-outs gone
while p is still under their size: the support offset too small for that texture); or a near
frame's map hash differs from the previous build's. Two flights decide.

## Alternatives considered, and why they lose

- **(A) Caster-receiver depth cap, per record or per tap.** Per record (CPU admission) pops a
  whole part at the box face and gives each cascade its own answer (the sweep returns). Per tap
  (**A'**, the designed backstop): in the apply loop a tap whose blocker is more than D sunward of
  the receiver counts as lit, with a band: `w = (d >= ref) ? 1 : saturate((d - (ref - cut_k)) *
  inv_band_k)`, `cut_k = 40,000 / range_k`, band 20,000 u, one float4 per cascade at c38-c42,
  about +5 of the five-cascade program's 499 slots, +3 ALU per tap, under 0.05 ms on the 1.57 ms
  apply [I]; identical in every cascade (no sweep), deterministic (TAA-neutral), near frames
  bit-identical. It loses as the primary rule because it removes a shadow that physically exists
  (a 13,000-17,000 u station at 137,000 u subtends 0.1 rad, eleven suns: a real 84 % eclipse) and
  leaves every lattice inside 40,000 u, where the penumbra (up to 372 u) already exceeds this
  station's 83 u pitch from about 9,000 u on. It stays the backstop for geometric holes, which the
  mip law cannot touch.
- **(B) PCSS-lite in the apply, blocker from the first-containing cascade.** The 3x3 kernel of c1
  spans 2.2 u; inside a 36 u cut-out all nine taps see the hull, find no blocker and leave the cell
  lit: the lattice survives exactly where it is seen. A blocker search wide enough for a 1,274 u
  penumbra mixes the Titan's own masts and hull (every hull pixel is within 2,000 u of a near
  blocker) with the station: the mean blocker depth blurs near shadows away, the nearest blocker
  reverts to the hard path and the lattice. A clean split needs a depth cut (A'), and then the
  far layer costs nine more taps per covered pixel, point-samples a 146 u-texel map (a mosaic of
  146 u blocks at +-12 % per frame, +-4 % after eight kernel rotations [I]) and needs the far
  caster's depth from the CPU. It also contradicts the 2026-09-17 rejection of PCSS for near
  casters, which remains right. Deferred, not rejected on physics: it is what would render the
  84 % eclipse with its soft 1,274 u outline if the user ever wants it.
- **(C) Prefiltered occlusion (min/average or ESM) per cascade.** Correct and noise-free but a new
  render-target chain per map (downsample passes on 2048²-4096² R32F, filtering caps, Reset,
  about 0.3-0.5 ms [I]) and a fixed-reference problem for receivers beyond the plane. The texture
  mip chain is this map for free, which is the decision. The caster-size gate at lease time is
  not derivable: the holes live inside the 26,884-primitive record.
- **(D) Umbra fade by caster angular size.** Needs a per-record size against delta; the
  records that carry the holes subtend eleven suns, so it never fires on the problem. As a cost
  complement (drop records under 0.0093 x delta: a 500 u part at 137,000 u) it is cheap but the
  minimum-footprint gate already removes sub-557 u parts from c4; not now.
- **Clamp the alpha test to mip 0 (no mips) in every cascade.** Removes the sweep by making every
  cascade show the lattice hard; physically wrong beyond 9,000 u and the coarse maps would alias
  the 83 u pattern into moire. Loses.
- **A lower cut (<= 20,000 u) for A'.** Touches complexes' mutual shadows (parts 10,000-30,000 u
  apart) and still leaves this station's lattice hard from 2,000 to 20,000 u.

## Unknown, and what settles it

- Alpha chains the bias cannot use: a single-level texture (no mips: the bias has nothing to reach)
  or a DXT chain whose levels were thresholded per level (each mip binary, not a box average) keep
  the cut-outs at every bias. Nothing detects either today; a per-record `alpha_levels=` row and a
  level-1 alpha mean against level 0 would.
- Whether the station's threshold and alpha texture are as assumed (binary alpha, ref <= 0.84):
  inferred from the solid c3/c4 maps. A per-record `shadow_replay_caster` field `alpha_ref=` and
  `alpha_levels=` (one row per frame, F8 only) would settle it from the next capture.
- Whether other FL stations carry geometric lattices (open girders) that need A': only a flight
  shows it; the F8 pair `same_window.py` + `hole_depth.py` tells alpha from geometry in minutes.
- How large the transition-zone stars are on screen for a caster at 5,000-18,000 u: the fixture's
  10,000 u plate measures it before any flight.
