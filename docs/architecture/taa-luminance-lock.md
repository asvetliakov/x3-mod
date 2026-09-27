# TAA luminance lock: a per-pixel hold in place of the blanket far weight

Design note, 2026-09-28. Status: ratified 2026-09-28 (orchestrator; user chose the lock over the baker widening); implementation pending (not built, not flown). Owner of the flown state: `docs/verification/temporal-resolve.md`
("Far stabiliser weight A/B in flight: 0.95 and 0.90"). Evidence scripts and outputs for this note:
`verification/results/taa-luminance-lock/lock_share_model.py` (`*_rho0.25_out.txt`, `*_rho0.5_out.txt`).
Tags: [M] measured in this session or in the ledger, [I] inferred from measured figures, [A] assumed.

## Decision in one paragraph

Replace the blanket far weight with a per-pixel **temporal flip lock** on the camera-gate resolve (`resolve.hlsl`,
X3M_REGION_HOLD, one new variant `X3M_LUMA_LOCK`): a far-eligible pixel (`farw > 0`, the 60/68 footprint ramp kept as the
eligibility gate only) whose current jittered sample, compared with the previous two samples of the same world point,
shows a sign-alternating luma change larger than a quarter of its own 3x3 luma range, is locked for `T` frames (default 16 =
two jitter periods, refreshed by every further flip while the screen gate is open, carried across a pan by reprojection with
the lifetime frozen while the screen gate is closed, released by disocclusion, a 25 % jump of the 3x3 mean luma against the
luma stored at creation, or expiry). A locked pixel takes the far weight (`min(ramp, 0.985)`, on `openC` as today) and the
7x7 far clip; every other far pixel takes the base weight 0.85 and the 3x3 variance clip, i.e. exactly the ordinary-hull
path. The thin region (0.97, its own vote and hold) stays and composes by `max` as today. State: one A8R8G8B8 lane at
COLOR3 (previous luma, luma before that, lifetime, reference luma), read at the age lane's nearest reprojected texel. On the
raster model of the three stations the flip rule locks 18-47 % of a station's covered pixels but 0.0-2.4 % of its plate
pixels, captures 92-95 % of the 8-phase ripple energy, and predicts a rest ripple 1.4-1.8x today's blanket 0.985 (against
2.5x measured at 0.95 and 4.8x at 0.90) [I from M]; plates under a pan get the base-weight sharpness. Cost about +60 slots on
the 1,038-slot hold program and one 4-byte lane [I]. Opt-in for the first flight (`--taa-luma-lock 16`).

## 1. The detector

**Signal.** Per pixel, in the hold program after the history taps and the 3x3 statistics (which exist for the variance clip):

- `Lc = q(luma(color))`, the tone-mapped luma of this frame's jittered sample, `q(L) = L / (1 + L)` quantised to 1/255
  (FSR2's HDR compression and UNORM rounding in `ComputeLumaInstabilityFactor`, `ffx_fsr2_accumulate.h`; our `color` is the
  cleaned raw current, HDR, so `q` keeps it in [0, 1) whatever `k`).
- `P1`, `P2`: the same quantity the previous frame and the frame before, read from the lock lane at the age lane's texel
  (`tap + round(f)`, the nearest reprojected texel, `resolve.hlsl:842`): the samples of the same world point at the two
  previous jitter phases (plus the sub-pixel residual of the pan, see creation below).
- `d0 = Lc - P1`, `d1 = P1 - P2`. **Flip** = `d0 * d1 < 0 && |d0| >= tau && |d1| >= tau1`, with
  `tau = max(TAU_ABS, RHO * range3)`, `range3 = luma(high3) - luma(low3)` of this frame's 3x3 (`low3 / high3` are in
  registers on the hold program, `resolve.hlsl:905`), `TAU_ABS = 2/255`, `RHO = 0.25`; `tau1` is the previous frame's
  threshold, which is not stored: use this frame's (the raster model uses each phase's own; at rest the 3x3 range of a
  world-static neighbourhood is nearly phase-invariant [I]).
- Neighbourhood normalisation is what keeps a textured plate out: a sub-pixel strut sampled and not sampled swings by the
  order of its 3x3 range; a plate at C sizes is at its 1x1-4x1 mip level (`lod-strut-widening.md` 11.1: 526 texels per pixel
  at s 65 [M]) and its jitter response is far below a quarter of the range, and a seam or edge that steps once (content
  change, a mover entering) has no sign alternation.
- Why no comparison against the *history* luma: the history is what the lock is deciding about; `|Lc - old|` is large on
  every young pixel and every edge and says nothing about oscillation. The history enters through the clip only.

**State, one A8R8G8B8 target (COLOR3 of the hold program; previous one read at s13).**

| channel | content | why 8 bits suffice |
|---|---|---|
| r | `P1` = previous `q` | FSR2 stores its luma history at 1/255 and thresholds at 1/255 |
| g | `P2` = the previous `P1` (shifted every frame) | same |
| b | lifetime `t` in 0..63 | `T <= 64`; `floor(b * 255 + 0.5)` exact |
| a | `R` = `q(mean3 luma)` at creation, the release reference | 25 % relative test |

The existing age lane cannot take it: the count (7 bits) and the 16-bit hold fraction (`resolve.hlsl:181`) fill 23 of the 24
FP32 mantissa bits at count 64, one bit spare [M by arithmetic]. Alternatives considered for the state are in section 8.
Every current-only return writes the lane "fresh" (`P1 = Lc`, `P2 = 0`, `t = 0`, `R = 0`), exactly as the age count
writes 1, so a disocclusion or a rejected history clears the lock in the frame it happens. Under a valid history the lane
shifts every frame whatever the gates (`P2 <- P1`, `P1 <- Lc`) so the chain is ready two frames after a pan stops.

**Creation, carry, release.**

- *Create* (`t <- T`, `R <- q(mean3)`) when eligible (`farw > 0` or `region > 0`), the flip holds, and the screen gate is
  open (`openS >= 0.5`, i.e. this pixel's own screen speed at or below about 0.14 px/frame under the flown LO/HI 0.03/0.25).
  Refresh is the same event on a locked pixel. The screen gate is required because the lane is point-read at the nearest
  texel: under a pan the rounding side of `f` changes frame to frame, so `P1` alternates between two neighbouring world
  points and any gradient reads as a sign flip; a creation under a pan would lock plates.
- *Carry*: the lifetime is read at the reprojected texel, so a lock made at rest follows its strut through a pan. While the
  screen gate is closed the lifetime is **frozen** (no decrement, no creation): a strut under a three-second pan would
  otherwise expire after `T` frames and shimmer for the rest of the pan. While open it decrements by 1 per frame.
- *Release*: (a) the current-only returns above (disocclusion, depth proof, reactive, sentinel, exit reset); (b) a shading
  change: `min(R, q(mean3)) / max(R, q(mean3)) < 0.75` kills the lock (FSR2 kills at a 10 % change of a Lanczos-filtered
  6th-root luma mip; we have only the 3x3 mean, which a 0.3-px strut moves by about a tenth of one pixel's contrast, so the
  threshold is coarser [A, the fixture tunes it]); (c) `t` reaches 0; (d) the camera gate: a locked pixel whose
  correspondence moves against the camera path (`openC` closed, a mover) drops to the base weight through `farOpen` without
  losing the lock, as the far weight does today.
- *Lock strength* `l = saturate(t / 4)`: full for `t >= 4`, fading over the last four frames of an unrefreshed lock so the
  release is 0.985 -> 0.951 -> 0.918 -> 0.884 -> 0.85 rather than a step.

**Against FSR2 (`ffx_fsr2_lock.h`, `ffx_fsr2_postprocess_lock_status.h`, `ffx_fsr2_accumulate.h`, MIT; fetched read-only
2026-09-28, GPUOpen-Effects/FidelityFX-FSR2 master).** FSR2 creates locks *spatially*: `ComputeThinFeatureConfidence` marks a
pixel whose luma is above every dissimilar 3x3 neighbour (or below every one; similar = within a 1.05 ratio) and that
belongs to no 2x2 quad of similar pixels; a new lock gets lifetime 1, a re-lock 2, the lifetime decays by about one jitter
period per unit, and only lifetime above 1 contributes (`saturate((t - 1) * 4)`): a pixel must be detected twice to hold.
The lock's effect is to exempt the history from rectification (`RectifyHistory`: `lerp(clamped, history, max(luma
instability, lock contribution))`) and its lifetime is killed by reactive, accumulation mask, depth clip (disocclusion) and a
10 % change of the shading-change luma; the reprojected lock status is bilinear-sampled. Separately, `ComputeLumaInstability
Factor` keeps four frames of quantised luma and flags a pixel whose current luma differs from N-1 by at least 1/255 while
an older frame on the same side is closer (an oscillation), gated by the clip box size; that flag also exempts the history
from the clip. **We keep**: the two-event rule (our sign flip is FSR2's "detected twice"), quantised luma history, the
kill on disocclusion and on a coarse luma change, a lifetime with a soft tail, the lock as an exemption from the tight clip
(our 7x7 box in place of the 3x3 variance clip). **We drop**: the spatial thin-feature detector as the creator (section 3:
on the raster it locks 3-6 % of plate pixels and captures 30-86 % of the ripple energy against 0-2.4 % and 92-95 % for the
flip), the bilinear lock reprojection (point read at the age texel, the lane's contract), continuous re-creation under
motion (our creator needs a stable previous sample; the frozen lifetime substitutes), and FSR2's full clip exemption (a
locked pixel stays bounded by the 7x7 box, section 2).

## 2. The weight law

On the hold program, after the motion cap and the rotation cap on the base weight (`resolve.hlsl:1031`):

```
lockKeep = keep + l * farOpen * (min(ramp, W_lock) - keep)      // was: keep + stabilise.g * farOpen * (min(ramp, flicker.y) - keep)
keep     = stabilise.b > 0 ? max(lockKeep, keep + stabilise.b * (min(ramp, history.x) - keep)) : lockKeep
```

- `W_lock` is `flicker.y`, the far weight register (0.985 default): with the lock on, the far weight *is* the lock weight
  and `stabilise.g` (= `farw * c11.z`) becomes eligibility only (it gates creation, section 1) and no longer scales the
  blend. `l = 0` is the base path bit for bit (`0 * finite`), the same exactness the far blend has today with `g = 0`.
- `farOpen` stays `openC` (camera gate) so a locked world-static strut holds under a pan and a locked mover does not.
- `min(ramp, W_lock)` keeps the age contract: a locked pixel two frames after a cut blends at 0.67, not 0.985. Note that
  today's far weight also ramps by age and reaches 0.985 only at count 64, so the lock's formation delay (section 3) is
  never later than the blanket weight's own approach to its target.
- **Thin region: keep.** It is a positive geometric witness with its own gate and L-frame hold, flown and accepted; the
  lines that shimmered at 0.90 in run352 were shimmering *with* the region at 0.97, so they are pixel-tier far lines, which
  is what the lock addresses. Folding the region into the lock would lose the vote's phase-independence (a strut the jitter
  does not sample for a whole cycle never flips). A later A/B may test region off once the lock is flown.
- **Far ramp: eligibility only.** `farw > 0` (footprint 60 units per pixel and beyond) says a feature smaller than 68 units
  is sub-pixel by geometry; the lock says this pixel actually holds one. Near plates (LOD 0 at 1.6 km, textured at levels
  3-5) are never eligible, which removes the texture-noise failure at its source.
- **Clip.** `farClip = region <= 0 && (l > 0 || |d0| >= tau) && farw * openC > c13.z`: locked pixels, and pixels whose
  sample just changed by more than `tau` (a stateless candidate, so a sub-pixel line sampled this frame is not cut back to the
  background by the 3x3 clip in the frames before its lock forms), take the 7x7 box; every other far pixel takes the 3x3
  variance clip like an ordinary hull pixel. A locked pixel is always bounded by the box: the lock raises the weight, it
  never exempts the history from the neighbourhood, so a lock on a genuinely moving edge ghosts at most within the 7x7
  range of the current frame and for the `T` frames until the shading-change kill or disocclusion clears it.

## 3. Expected result on the three stations

Raster model (`lock_share_model.py`: the 8 Halton phases of the strut-widening raster as 8 consecutive frames at rest,
cyclic, three axis views, pixel-weighted; "plate" = always covered with 8-phase std under 2/255; gains from the closed-form
8-phase fundamental of `taa-distant-line-fade.md`: 0.020 at 0.985, 0.208 at 0.85) [M on the raster; the raster itself is a
model of the game, so the station figures are I]:

| station, size | plate px | flip share | rule | lock share | ripple energy captured | plate px locked | refresh >= 2 / cycle | predicted rest ripple vs 0.985 today |
|---|---|---|---|---|---|---|---|---|
| outpost s 110 (25.7 km) | 75 % | 0.085 | flip rho 0.25 | 0.230 | 0.952 | 1.47 % | 0.68 | 1.45x |
| | | | flip rho 0.5 | 0.217 | 0.936 | 1.43 % | 0.66 | 1.60x |
| | | | FSR2 ridge | 0.111 | 0.414 | 3.07 % | 0.71 | 6.51x |
| | | | flip and ridge | 0.047 | 0.265 | 0.00 % | 0.42 | 7.91x |
| outpost s 147 (19.3 km) | 81 % | 0.065 | flip rho 0.25 | 0.186 | 0.954 | 1.21 % | 0.71 | 1.43x |
| solar plant s 65 | 60 % | 0.125 | flip rho 0.25 | 0.329 | 0.920 | 0.03 % | 0.69 | 1.76x |
| | | | FSR2 ridge | 0.367 | 0.863 | 3.27 % | 0.79 | 2.29x |
| spacedock s 188 | 47 % | 0.170 | flip rho 0.25 | 0.466 | 0.942 | 2.37 % | 0.70 | 1.55x |
| | | | FSR2 ridge | 0.258 | 0.408 | 6.03 % | 0.64 | 6.56x |
| any, all pixels at the base weight | | | | 0 | 0 | | | 10.4x |

Reading: the lock share exceeds the flip share because always-covered pixels whose luma alternates under jitter (plate
seams, struts over hull) flip too; those are real ripple sources (the model's energy split confirms it: 92-95 % of the
summed std lies on the locked set). The plates proper stay unlocked (97.6-100 % of plate pixels).

**Which pixels lock.** Outpost: 19-23 % of its covered pixels at 19-26 km (the 100-unit plating strips of materials 16/39,
bevels, seams); plant: 33 % (the lattice cards and their edges); spacedock: 47 %. Plates: 75-81 % of the outpost's pixels,
of which 1.2-1.5 % lock (false locks, chatter candidates), 60 % of the plant (0.03 % lock), 47 % of the spacedock (2.4 %).

**Blur of the unlocked plates under a pan.** They are on the ordinary-hull path (0.85, 3x3 clip): the Run 91 A replay gives
E / E_ideal 0.831 at 0.85 against 0.755 at 0.9 at 5.9 px/frame [M, ledger 3278], and the flight trend of the far blob's
presented sharpness in a ~73 px/frame pan is 0.171 at 0.95 -> 0.227 at 0.90 [M, run351/352]; at 0.85 on the plates
expect 0.26-0.30 [I, extrapolated; the near LOD-0 hull at 0.85 measured 0.29-0.31 at rest in the same runs]. Locked pixels
(struts, seams) blur under a pan as today at 0.985: the plates' seams are part of the locked set, so "plate sharpness" in the
flight means the plate interiors and the panel texture, and the seams are the first thing to inspect (section 5).

**Shimmer of the locked struts vs 0.985.** After formation the locked pixels have today's weight and today's clip:
identical ripple gain 0.020 [I]. The whole-blob rest ripple is predicted at 1.4-1.8x today's (the unlocked 5-8 % of the
energy at gain 0.208), i.e. below the 2.5x measured at 0.95 (run351 rms 0.989 against 0.422 codes) and well below the 4.8x of
0.90 that the user rejected; in codes, about 0.6-0.75 rms on the outpost [I].

**Failure modes.**
- *A locked pixel that is a moving edge*: a mover's edge at rest that starts moving is caught by the camera gate (`openC`
  closes on the correspondence against the camera path, weight to base) and by disocclusion (current-only return, lock
  cleared); a world-static edge that changes shading (a light, a shadow) is caught by the 25 % mean-luma kill within one
  frame; what remains is a slow shading drift under 25 % on a locked pixel, bounded by the 7x7 box: a lag of at most the
  box width, as today on every far pixel.
- *Texture noise*: excluded by eligibility (`farw > 0`, features under 68 units) and, on far textures with detail (light-map
  rows, windows), by the quarter-range threshold; the raster's false-lock rate on plates is 0.03-2.4 %. On real far textures
  (which the raster models as flat mean luminance) this is unmeasured (section 9).
- *Chatter at the lock boundary*: a lock refreshes on 66-71 % of locked pixels at least twice per 8-frame cycle in the
  model; with `T = 16` a pixel that flips once per cycle stays locked; the 4-frame tail makes an expiry a fade, not a
  toggle. Expected residual: isolated plate pixels (1-2 %) that lock for 16 frames and fade, invisible at the base-weight
  ripple of a flat plate (their std is under 2/255 by definition) [I].
- *Formation delay*: three samples are needed (`P2`, `P1`, `Lc`), so the earliest lock is the third frame after a cut or
  after the disocclusion band of a pan uncovers the pixel; frames one and two run at the age ramp (0.5, 0.67) with the
  stateless 7x7 candidate clip. Today's far weight sits at the same ramp for those frames (it reaches 0.85 at count 6 and
  0.985 at 64), so the flash after a cut is not new; the visible difference is the pan's trailing disocclusion band, where
  today's pixels ramp toward 0.985 and the lock's unlocked ones stay at 0.85: a 1-2 px band at the edge the pan uncovers,
  at the ordinary-hull shimmer level.

## 4. Cost

- Hold program: one `texld` (the lane), luma + tone map + quantise (about 8 slots), `range3` from `low3 / high3` (4), the
  flip test (about 10), lifetime decode / update / encode (about 12), the release test (about 6), `l` and the blend (about 6),
  the clip predicate (3), `oC3` (about 4): about **60 slots, 1,038 -> about 1,100** [I; `RESOLVE_BUDGET` measures it], within
  the low-thousands plan; about 60 us per frame at 5120x1440 by the 1 us per slot planning figure [I]. The other programs
  and the un-locked hold variant keep their bytes (a compile-time variant, so the flown default stays word for word).
- One A8R8G8B8 lane: 7.37 Mpx x 4 B = 29.5 MB written and 29.5 MB read per frame at 5120x1440 [I, arithmetic], the same as
  the age lane costs today; the first estimate is 0.1-0.3 ms at the unified-memory bandwidths of this machine [I]. No extra
  pass, no extra draw: the lane is a fourth MRT of the existing resolve draw (`NumSimultaneousRTs` is 4 on every SM3 part [A,
  caps-gated at the pass] and the pass already requires `MRTINDEPENDENTBITDEPTHS` for R32F beside FP16).
- Total: about 0.2-0.4 ms on a 20-21 ms frame (1-2 %) [I]. The far-clip 7x7 loop now runs on fewer pixels (locked and
  candidate instead of every far pixel), a small saving in the other direction.

## 5. Verification

**Host fixture** (`temporal_pass_fixture.cpp`, new row family `LUMA_LOCK` parsed by `run_temporal_pass.py`, the
`MOTION_WEIGHT_ROTATION` pattern: 512x16 stripe, hold program, camera gate). Content: a plate of luma 0.5 with a 0.1 per px
texture ramp, a 0.4-px world-static strut of luma 1.5 whose analytic coverage flips with the 8-phase jitter (the
`FAR_JITTER_LINE` line), both beyond the far ramp; a 0.37 px/frame camera pan from frame 16 (above HI 0.25: the screen gate is
closed, creation must have happened at rest and the lock must carry); a cut at frame 40 (history restart); a 3-px mover
crossing the strut at frames 56-64 (disocclusion). Rows and numeric acceptance:

| row | measure | acceptance |
|---|---|---|
| `form` | first frame with lock share of strut pixels >= 0.9, after frame 1 and after the cut | <= frame 3 (N = 3) |
| `plate` | lock share of plate pixels, every frame | 0.000 (< 0.5 % allowed for the texture ramp) |
| `plate_sharp` | plate E / E of the same fixture with the far stabiliser off (base 0.85) | within 2 % (X = 2 %); plate interior >= 3 px from any locked pixel bit-identical |
| `strut_ripple` | `ripple_rms` on strut pixels from frame 8 vs the `FAR_STABILISER` 0.985 reference | within 10 % (Y = 10 %) |
| `carry` | lock share of strut pixels through the pan, frames 16-40 | >= 0.9 every frame |
| `chatter` | share of locked strut pixels toggling per frame after formation | <= 5 % |
| `mover` | locks on pixels the mover covered, the frame they are uncovered | 0; re-formed within 3 frames |
| `budget` | `RESOLVE_BUDGET` of the new variant; dwords of every existing variant | new <= 1,200; existing unchanged (hold 3,968 dwords) |

Also the raster model's acceptance in flight terms: outpost rest ripple <= 1.6x today's.

**Flight** (the run340/351 stand, `./x3run --direct --debug --perf --taa-luma-lock 16`; far stabiliser default untouched):
F8 at rest on the 25.7-km outpost instance, then F8 mid-pan across it at the run351 speed (~73 px/frame), then stop and
F8 again within a second. Scripts: `rest_flicker.py` on the far blob (accept rms <= 0.65 codes, > 4 codes <= 0.1 %; run351's
0.95 was 0.989 / 1.12 %), `sharpness_measured.py` in the pan (accept present/current >= 0.26 [I target], above run352's
0.227). What to look at: plate interiors and panel texture crisp under the pan like the near hull; plate seams (locked)
still soft under the pan, which is the first tuning target if it bothers (raise `RHO` to 0.5 or `TAU_ABS` to 4 codes: lock
share -1 to -6 points at -1 to -4 points of energy captured, model); struts steady at rest as today; when the pan stops, no
sparkle burst longer than a few frames; no speckle on plates.

## 6. Config

Schema first (`tools/config/schema.py`, then `generate --check` regenerates `config_schema_inc.h` and the launcher table):

- `entry('taa_luma_lock', 'float_list', 'graphics', 'Holds a long history only on distant details that actually flicker
  (struts, antennas, seams) and keeps distant hull plates sharp under a pan. The first number is how many frames a
  detected flicker keeps the hold (1 to 64); the others tune the detector. 0 = disabled.', '0', counts=(1, 3),
  elements=((0, 64), (0, 1), (0, 32)), requires=('taa', 'taa_far_stabiliser'), launcher='--taa-luma-lock')` —
  `T[,RHO,TAU_codes]`, defaults 16, 0.25, 2. Environment name `X3M_TAA_LUMA_LOCK` (generated; never a bare env read).
- `dev('taa_luma_lock_release', 'float', ...)`, the mean-luma kill ratio (0.75), and `dev('taa_luma_lock_gate', 'enum',
  choices=('screen', 'always'))` for the creation gate A/B in the fixture only.
- The far stabiliser keeps its entry and default `0.985,0,60,68,0.03,0.25`: with the lock on, W is the locked pixel's
  weight, F0/F1 the eligibility ramp, LO/HI the creation gate; its description gains one sentence saying so. W = 0 with the
  lock on refuses the run at prepare (as invalid far settings do today), never a silent fallback.
- First flight: opt-in, builtin '0', launcher `--taa-luma-lock 16`. After acceptance: builtin '16'; the far stabiliser's
  default string does not change (its weight is then applied through the lock only), so `x3m.ini` files written before the
  change keep their meaning.

## 7. Against the baker widening (parked)

Widening changes the source: a 1-px strut is sampled every phase, so its per-pixel ripple falls and the first frame after a
cut is already quiet; it costs nothing under a pan. The lock only holds: a locked pixel still needs 66 frames of history to
reach the 0.020 gain, flickers for three frames after every cut or disocclusion, and blurs under a pan as today. But
section 11 of `lod-strut-widening.md` measured that at C sizes the widening moves the outpost's flip share by 2-8 % and the
plant's ripple amplitude not at all (translucent 1-1.4 px bands are all edge pixels); the far-range shimmer is a weights
question. So: the lock is wanted now (no bake, addresses the user's complaint, the plates) and the widening stays parked for
the LOD-0-to-C switch range (0.5-1 px struts at 9 km) where it does bite; both are complementary, neither replaces the other.

## 8. Risks and rejected alternatives

Risks: (1) seams lock and stay soft under a pan (section 3); (2) the frozen lifetime under a pan carries a stale lock onto
content that changed under 25 % of mean luma, bounded by the 7x7 box; (3) real far textures are not the raster's flat
luminance: false-lock rates above the model's 0.03-2.4 % would show as plate speckle (section 9); (4) the lane is a fourth
MRT: documented D3D9, caps-gated (`NumSimultaneousRTs >= 4`, `CheckDeviceFormat` A8R8G8B8 render target); a device without
it logs one row and runs the hold program without the lock (the far weight then applies as today), no second program
set; (5) slots 1,038 -> about 1,100, measured by the fixture before install.

| alternative | why it loses |
|---|---|
| Lower blanket far weight (0.95 / 0.90) | flown: rest ripple 2.5x / 4.8x, "thin lines are starting to shimmer" at 0.90; pan sharpness still 0.17-0.23 [M] |
| Velocity-based weight (far weight on the screen gate) | measured and rejected 2026-09-25: the 0.4-px line at 10.5 px/frame 19.17 codes vs 4.93 on the camera gate; a sub-pixel line at the base weight leaks 10 % per frame under a pan [M, `taa-mask-fold.md` 4.2] |
| Rotation-aware weight | flown, rejected by the user (shimmer under pan); it is per-pixel by speed, not by content |
| FSR2's spatial ridge as the creator | model: locks 3-6 % of plate pixels and captures 30-86 % of the ripple energy; along-strut and seam pixels fail the ridge; combined with the flip it captures 17-55 % (table, section 3) |
| Luminance-variance-only (spatial, no temporal sign) | flags every edge and every textured pixel: the plates' seams and texture lock and the pan blur returns; cannot tell a stable 1-px line from an unstable one |
| Lock stored in the age R32F | one spare mantissa bit at count 64 [M]; a G32R32F age target would fit `t + 256 P1 + 65536 P2` exactly (22 bits) but drops the release reference and costs the pack/unpack arithmetic; the A8R8G8B8 lane quantises for free and is the fallback if a fourth MRT is refused |
| Higher-resolution history (2x) | 4x history bandwidth and filter taps (several ms at 5120x1440 [I]); reduces resampling blur for all pixels but a 0.3-px strut is still sub-pixel at 2x and still flickers; `taa-high-resolution.md` steps are a separate, larger programme |
| FSR2's clip exemption for locked pixels | a lock on a moving or shading-changing pixel would ghost unbounded until the kill; the 7x7 box costs nothing extra (it is today's far clip) |

## 9. Unknown, and what settles it

- The false-lock rate on real far textures (light-map rows, windows, specular) [not verified]: a `--taa-debug` burst on the
  run340 stand dumping the lock lane (`b > 0`) beside the depth lane gives the locked share per depth band and its overlap
  with `lightmap`-lit pixels; the fixture's plate row has only a synthetic ramp.
- Whether `RHO = 0.25` and `TAU_ABS = 2/255` transfer from the raster's flat luminances to the game's HDR values under
  `q = L / (1 + L)`: the same burst, comparing the locked set with the flip set computed from two consecutive `hdr_` frames.
- The 25 % release ratio on the 3x3 mean [A]: the fixture's `mover` and a shading-step row (a plate whose luma steps 20 %
  and 40 % at frame 30) settle the two sides.
- The creation gate under slow pans (0.03-0.14 px/frame): a strut that never rests never locks; the run351 stand is at rest
  between pans, a strafing flight is not. The `always` gate A/B in the fixture (section 6) measures what the nearest-texel
  read does to plate locks under a 0.37 px/frame pan.
- The pan-sharpness target 0.26 [I] is an extrapolation of two points; the flight measures it.
