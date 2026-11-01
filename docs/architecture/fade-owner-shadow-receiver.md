# Fade-band RT2 owners as sun-shadow receivers and casters (design, 2026-11-02)

Question: should a distance-fade-band RT2 owner (fade-rt2-ownership.md) also be a sun-shadow receiver, so the sun
shadow term does not pop in when a station leaves the engine's fade band and turns opaque? Evidence: AP Run 218
(`verification/results/run218-outpost-switch/`, native Windows, dev DLL 9be4c8a4). Markers: [M] measured this
session or in the triage, [C] read in code, [I] inferred. Ratified; implemented in fbe0c137 and its review commit
(the guard `950 <= permille <= 999`: exactly 1000 is a material draw at full alpha, not a distance fade; the
`fade_receiver=` / `fade_caster=` counts on `fade_route_frame`); ledger `docs/verification/fill-light.md`
"Fade-owner shadow receiver". Section 8's first unknown is settled (the share forms carry the owner fragment); the
GPU check of section 6 was not run.

## Decision

**Option B with P = 950, extended to the caster side: a fade owner whose fraction is at or above 950 permille is
treated as opaque by the whole sun-shadow system.** It binds the share-producing forms its opaque draw binds
(`ps_sun_original`, `ps_sun_original_lightmap`, `ps_sun_original_lightmap_widen`: no new program), counts as a
receiver (`route.sun_receiver`, the eligible-receiver and depth-writer arguments of `sun_frame_.draw`) and enters
`note_candidate_draw` as a caster (`zwrite = !route.fade_arm` becomes `!route.fade_arm || permille >= P`). Below P
nothing changes: the invalid-share owner forms, no receiver, no caster. A fade owner at or above P whose share form is
missing keeps today's owner form (RT2 `.g = -1`, counted), not the opaque path's frame veto.

Why the caster side is part of the decision [M]: in the opaque frame 4148 the station's three draws (vb 986/988/990)
are replayed as casters into C4 (`shadow_replay_caster ... cascades=16`); in the band frame 3571 no caster row carries
those vbs, because `note_candidate_draw` admits only `zwrite = !route.fade_arm` draws [C `motion_output.cpp:11662`,
`:11725`]. A receiver-only B would therefore give the band station shadows from other casters but not its own
self-shadow, and the self-shadow is the term that pops (the station is alone at 35-40 km).

## 1. What the evidence fixes

- Every routed fade-arm row of the Run 218 session is at permille 981-999 (VS `167eb2d5` 668 rows, min 981;
  `37c34a74` 464, min 984; `b0602757` 358, min 982; `4944d81d` 3 rows at 1000) [M, `permille_hist.py` ->
  `permille_hist.txt`]. The engine culls these nodes before their alpha falls further, so in this content the band
  is the cull-in edge: a station is 98-100 % opaque for its whole routed life. The brief's "500-999" is the
  admission range, not the observed one.
- Band pixels: RT2 `.g = -1` on all 13,906 / 13,367 station pixels; opaque: receivers, 83 % at share exactly 0
  (f = Y(S)/Y(L) = 0, no sun term), 8 % at 0.8-1.0 (the sunlit highlights, L_mean 0.30) [M, `share_bins.txt`].
- The apply (`sun_shadow_cascade_apply_ps.hlsl:87,124-127`) takes `valid = d >= 0 && s > 0 && covered > 0`,
  `s = saturate(ds.g)` (g > 1 is the bolt lane), factor 1 otherwise; it never reads `.a` [C]. So an owner pixel with
  `.g = -1` is untouched, and an owner pixel with `.g = f` is shadowed exactly like an opaque receiver.
- The owner fragment forces RT2 alpha 1 on a fade-arm row (`current_depth_owner_ps.hlsl`: `.a = max(w*c218.z +
  c218.x, c218.y)`, `c218.y = 1`) [C]. D3D9 blends target n with target n's own alpha
  (fade-rt2-ownership.md section 2, `MRTPOSTPIXELSHADERBLENDING`), so a share f written by an owner lands as
  `f * 1 + g_prev * 0 = f`, not as `p*f + (1-p)*g_prev`: the brief's blend-error premise does not apply. The `.r`
  depth already relies on this on native Windows (Run 218 is native) [M].
- Five cascades, extents 250 / 1,500 / 7,500 / 37,500 / 150,000 [M, log `extents=`]; the station's view depth is
  177-198 k units [M, `regions.txt` z_p50] and it is admitted to C4 by bounds when opaque, so the receiver pixels are
  covered there; coverage in the band frame (40 km) is inferred from the same geometry [I].

## 2. The share value for a blended pixel (option B's semantics)

The owning colour is `C = p*c + (1-p)*bg`. The exact share is `p*Y(S)/Y(C)`. Writing the draw's own f = Y(S)/Y(c)
(what the existing share forms produce; the extraction reads rgb only, so the fade alpha does not enter) is exact
against black (`Y(bg) -> 0` gives `Y(C) = p*Y(c)`, share f) and over-shadows the background part by at most
`(1-p)*Y(bg)*f*(1-vis)` over a bright nebula. At P = 950 that is 5 % of the background's luminance times the shadow
depth; at the observed 981-999 under 2 %. Scaling to `f*p` (one MUL by `oC0.a`, a new form) is exact when
`Y(bg) = Y(c)` and under-shadows against black by the factor p; both errors are below 5 % at P, so the new form is not
worth its 3 x 23 programs (section 5, alternatives). Below P the owner form's `.g = -1` keeps the pixel out of the
receiver set, as today.

"Does the shadow fade with alpha when p falls 999 -> 500?" No: the term is on at `p >= P` and off below, for the
receiver (f unscaled) and the caster (a depth map has no partial occlusion). With the measured distribution the P
crossing does not occur (min 981): the shadow is on from the first routed frame and there is no band/opaque switch at
all. Content that does cross P would show the switch at 95 % opacity instead of at 100 %, the same contrast as today
one step earlier; the kill criterion (section 6) covers it.

## 3. Interaction with the RT2 validity contracts (directional-shadows.md sections 8 and 10)

| Consumer | Today for an owner | Under B (owner at p >= P) |
| --- | --- | --- |
| `validDepth` (`.r >= 0`) | the band's own depth, exact (alpha 1) | unchanged: the receiver's depth is the owner's, which is right (the fragment that owns the colour) |
| Thin vote (`validDepth && 0 <= a < 1`) | owner carries `a = 1`: no vote | unchanged, `.a` is the owner fragment's |
| TAA fade-owner history class (`resolve.hlsl`: routed alpha-1 correspondence) | keyed on `.r`/`.a` | unchanged; the resolve reads `.g` only for the bolt lane `g > 1` (`resolve.hlsl:1220`), f <= 1 never trips it [C] |
| Depth-history copy | point copy of `.r` | unchanged |
| Apply `valid` | `s = -1`: factor 1 | `s = f`: shadowed like the opaque receiver; an owner with `.a = 1` is not special to the apply (it does not read `.a`) |
| Invalid -1 vs proved 0 | owner writes -1 | the share form writes exact 0 where S = 0 (83 % of the station) and f elsewhere; -1 only from the extraction's domain checks, as for opaque |
| Scene-end exclusion M | `exclusion_required=0` in all four frames [M]: the XT row is not in `distance_fade_rows`, no bracket, no M | unchanged; where a bracket produces M its pixels stay excluded (conservative, right: the bracket re-composes the colour) |
| Veto after an eligible receiver | the owner is "a plain depth writer with explicit invalid `.g`", no veto (`failed=0` [M]) | the owner is a proved same-draw receiver update; later draws over it behave as over any receiver |
| Receiver over an earlier receiver's share | `.g` replaced by -1 | `.g` replaced by f (alpha 1, no leak into `.g`); the colour leak `(1-p)*bg` is section 2's bound |

Caster side [C `note_candidate_draw`]: the replay re-issues the route's recorded VB/IB and matrix rows through the
pass's own depth program; the draw's blend / Z-write state does not enter, so a fade-arm candidate takes the same
box, near-bound (W1) and alpha (W3) rules as its opaque twin. The station's 20 k primitives into C4 while in the band
cost what they cost when opaque (`draws4=` on the depth line; C4 already alternates over budget).

## 4. Cost on the hot path

Receiver: one integer compare (`fade_permille >= P`) and the existing pointer selection in `bind_variant_pair`; zero
new programs (the 23 share forms exist, `original_variants=23` [M]; their fill-K, gain and widen compositions are the
opaque draw's). Caster: one compare in `note_candidate_draw`; `sample_candidate_sun` (one register read) and the
box test for each admitted band draw, exactly as for its opaque twin; replay primitives as above. No allocation, no
upload, no getter. Slots: none added (the owner forms stay for p < P; the `fade_owner_forms` rows show 101-212 slots
per form today [M]).

## 5. Alternatives considered

- **A, keep the rule.** The user keeps seeing, at the band edge, a one-frame drop of the sunlit faces and highlights:
  hi2 % p50 0.745 -> 0.603 (-19 %) and 0.703 -> 0.619 (-12 %), p90 -16..-33 % [M], on a 98-100 % opaque station.
  Loses because the "band" here is the cull-in edge and the switch is unconditional in this content.
- **B as briefed (new share twins of the owner forms, receiver only).** 3 x 23 new programs with the strength twins
  (kinds 8-10) to regenerate, native validation, and it leaves the self-shadow out (section Decision). Loses on cost
  and because it does not remove the pop.
- **B with `f*p` (shadow fades with alpha).** Exact only when `Y(bg) = Y(c)`; needs a new form per share program;
  gains under 5 % at P over unscaled f. Loses on simplicity.
- **C1, approximate the shadow statistically in the band's fill.** The apply has the real visibility; a constant
  darkening of the sunlit term would be wrong on every unshadowed face and still pop. Loses.
- **C2, admit only at p >= 990.** Leaves 881 of 1,490 routed rows (59 %, at 981-989) unadmitted [M] and moves the
  switch to 990. Loses.
- **P = 900 or 500.** The leak bound grows to 10 % / 50 % and a half-transparent caster writes a full-depth shadow;
  nothing observed needs it. P = 950 covers every observed row with a 31-permille margin and a 5 % bound.

## 6. Verification and kill criterion

- Host: `fade_band_fill_selection` (`motion_wrap_state_fixture.cpp`) gains rows: fade owner at 950 / 999 with the
  lane -> share form (plain / gain / widen) and `sun_receiver = 1`; at 949 -> owner form, `sun_receiver = 0`; owner
  at 999 with the share form missing -> owner form, no frame veto. Shadow-replay candidate test: a fade-arm route at
  >= P is admitted by bounds like the opaque draw, below P refused.
- GPU (motion-output runner, lane on): one blended draw at alpha 0.998 over an earlier receiver: RT2 `.r` the draw's
  depth, `.g` the draw's f exactly (not `0.998*f + 0.002*g_prev`), `.a = 1`.
- Flight (the first flyable step): the Run 218 stand, F8 once in the band and once opaque. Pass: `share_bins.py` on
  the band frame shows `.g` in [0, 1] with the opaque frame's bin fractions (83 % at 0, 8 % at 0.8-1.0, within
  a few points); hi2 % and p90 band/opaque ratios within +-5 % (today 1.18-1.24 / 1.16-1.33); `shadow_replay_caster`
  rows for the station's vbs with `cascades=16` in the band frame; `sun_shadow_lane_frame failed=0`.
- Kill: a visible shadow switch at the P crossing in any flight, shadow acne or a dark halo on band stations that the
  opaque draw does not show, or `failed=` rising on `sun_shadow_lane_frame` -> one flag back to A (the owner forms and
  the `zwrite` rule are untouched for p < P, so the revert is the compare's constant).
- Documents to amend on landing: directional-shadows.md sections 8 and 10 ("fused fade-band arm ... not
  first-version receivers", "Fused fade keeps its existing RT2 mask"), linear-distance-fade-region.md "Fade-band fill"
  ("never the share producer"), fill-light.md's "never a receiver", fade-rt2-ownership.md departure (3).

## 7. The 20 % darker mid-tones (upper half, band)

Not the shadow term: the apply only darkens, and 83 % of the opaque station's pixels have f = 0, which the apply
leaves untouched; a missing shadow cannot make the band darker. Against the opaque frame's sun-free pixels the band's
whole-station median is within 2 % (band p50 0.0439 / 0.0444 vs opaque share-0 p50 0.0447 / 0.0449 [M]), while the
whole-station opaque median (0.0469, which includes sunlit pixels) is 6 % higher; the lower half is equal. So the
upper-half -20 % is a region/view effect of two frames taken 4 km apart (13.9 k vs 17.2 k pixels, different mips and
grazing angles) [I], not a program difference. Two program-side differences do exist and should be ruled out cheaply:
the engine light is off on every fade-arm row (`engine_light_prepare` returns on `route.fade_arm` [C]; relevant only
if a lit node's radius reaches the station) and the light-map far-fade gain is distance-driven (`min_gain` 1.25 band
vs 1.37 opaque [M], which would brighten the opaque frame's emissive pixels, not its mid-tones). The triage's
proposed row (bound role, c217.w gain, engine light admitted, strength twin bound) is right, but as four fields on the
existing per-draw `motion_route` row rather than a new row; it is not a prerequisite for this decision.

## 8. Unknowns

- That the share forms carry the owner depth fragment (c218 lane, `.a = 1`): they come from the same
  `original_fill_transform(..., current_depth)` path as the fill variant the owner forms are built on [C
  `linear_material.cpp:2820-2860`], and the ledger measured +2 slots on every depth-writing variant; the host
  fixture's RT2 `.a` check settles it [I until then].
- Cascade coverage of the station at 40 km in the band frame: inferred from C4's 150,000 half-extent and the opaque
  frame's bounds admission; the flight's caster rows and share bins settle it.
- Whether any vanilla content routes a fade owner below 950 (sectors with other fog distances): the
  `permille_hist.py` one-liner on any later log answers; a min under 950 reopens the P choice, not the design.
