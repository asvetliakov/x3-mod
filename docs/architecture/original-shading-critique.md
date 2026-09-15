# Original hull shading: fill, selective exposure and the point-light cull

**Ratified 2026-09-16 (orchestrator):** Q1 no code-value fill; the linear-light
fill inside the original programs (§1a, option C) is the funded design, built
after Q3 and enabled on the F8 trigger or user request; Q2 selective
exposure closed, milestone-1 source kept as a checkpoint; Q3 root-object
point-light admission implemented bounded and default-off after the replay
fixture, enabled for a run only when the user confirms the module cliff in play.

Independent critique, 2026-09-16. Pending orchestrator ratification. Inputs:
[material-investment.md](material-investment.md), [fill-light.md](fill-light.md),
[material-selective-exposure.md](material-selective-exposure.md),
[linear-emission-cost.md](linear-emission-cost.md),
[camera-and-lights.md](../reverse-engineering/camera-and-lights.md) "Point-light admission site",
[station-material-distance.md](../reverse-engineering/station-material-distance.md) "Run 21 B" and
"Run 22", [goals.md](../goals.md), [status.md](../status.md), `src/renderer/linear_material.cpp`
(`linear_material_fill_sum`, `fill_instruction`), `src/proxy/engine_patch.cpp`. No code, Wine or
game. Facts about other games are recalled from general knowledge and marked *(recalled)*.

Context the verdicts assume: original hull shading (no `--linear-materials`), FP16 scene, AgX,
Auto ceiling **+1.3 EV** (main since 2026-09-16, `fdc85e4`; the installed build still has +1.0),
bloom, `--emission-source-gain 2`, `--screen-emission-additive 2`, raised chase camera. Linear
hulls and their fill (`.05`) remain an optional launch configuration.

## Verdicts

| # | Question | Verdict | Trigger to revisit |
|---|---|---|---|
| 1 | Fill on original shading | **Not as a code-value constant; do later as option C** (the fill decoded/encoded in linear light at the located lobe-sum site of the original programs, §1a; ≈8 slots approximate, ≈29 exact, zero per draw). | After #3, when the baseline F8 at the run-51 spot on the installed build shows a far-module dark fraction ≥ 0.2, or the user asks for readable shadow sides. |
| 2 | Selective (per-material) exposure | **Don't; close it.** Retain the milestone-1 source as a checkpoint; no runtime integration. | User wants hulls *darker* than the Auto ceiling gives while emitters and nebula must stay, and `--hdr-ev` / `--hdr-ev-max` / the two emitter gains cannot satisfy both. |
| 3 | Patch the point-light cull | **Do, bounded and default-off**, root-object admission at `0x004c27af`; not range widening. Schedule after the run-26/65/66 shadow-candidate and loading analysis. | Gate on one user sentence: the docking-module cliff is visible in ordinary play under the accepted look (run 21 B already saw it in vanilla). |

## 1. Fill on original shading

**What X3's own art already supplies on a sun-averted face.** The engine has no ambient term
(`g_LightAmbientIntensity` never uploaded, `D3DLIGHT9.Ambient` zero; fill-light §1), but it has
three floors: the nebula cube reflection through the specular mask (measured on the run-51
far module under the linear route: darkest quarter 0.025–0.05 exposed-linear, chroma equal to the
background's teal), the authored second directional light D1 = (33,66,55)/256 in two-light
sectors (a cool directional back light, 7.8 % of the sun's decoded luma), and lightmap/emissive
windows (s2/s3 roles, linear-bump-materials §sample contract). Original shading adds the cube
term in code values, so its displayed floor is at least the linear route's for the same inputs
(the decode/encode round trip on a single term is the identity; the code-space `m·A` tint is the
larger factor). Inference; the original-route floor is unmeasured (unknown 1).

**What the accepted look already does.** Auto at +1.3 EV multiplies the scene by up to 2.46. The
run-51 darkest quarter, 0.025–0.05 at EV0, sits at 0.06–0.12 exposed-linear at the ceiling: dark
grey with nebula tint, not black. The user chose this ceiling with original hulls after run 26;
that is the fill decision already made, paid for on the sun side as well, and accepted.

**Why a code-value fill is the wrong tool.** The insertion site is known: `linear_material_fill_sum`
runs on the *original* bytecode and resolves the lobe-sum/albedo multiply in all 94 hull,
asteroid, palette and glass pixel programs, `xt_fill_site` in the 14 XT ones (108 PS; the brief's
137 is stages, the vertex half carries no fill). A variant that skips the transfer and adds
`k·LightDir_Color0` there is create-time only, zero per draw, and two slots rather than one: the
light colour is a constant register (c5/c5/c2), and ps_3_0's single constant read port forbids
`mad sum, c5, c215.x, sum`, so it is `mul rT, c5, c215.x` + `add`. Cheap. The arithmetic is the
problem: added *before* encoding, the fill is a gamma-space lift. With `L = c^2.2`,
`dL/dc = 2.2·c^1.2`: at hull code 0.40 (0.13 linear, the run-51 lit level) a code delta adds 0.73×
its size in linear light, at code 0.10 only 0.14× — a constant code lift gives a sun-lit face five
times the linear light it gives a shadow face. To reach the linear fill's displayed floor
(`.03` → 0.011 linear per A≈0.7 face; code 0.129) the code constant must be ≈0.20
(0.129 / (Color0 code luma 0.74 × A code 0.85)); at that value a shadow face at 0.03 rises to
0.087 and a sun-lit face from 0.13 to 0.24 linear (+0.9 EV). That is a global hull brightening,
lit/shadow ratio 4.3 → 2.75, and the meter's lit-median key then moves the other way (the
linear fill re-keyed ≤0.25 EV, fill-light §3; a +0.9 EV hull lift is proportionally larger — inference),
which darkens the nebula the user likes. Smaller constants (≤0.08) add nothing visible to a black
face (code 0.05 → 0.0014 linear). A fill that behaves like light has to be added in linear light,
which is the `--linear-materials` route already built; a "screen"-shaped lift
(`sum + kC0·(1−sum)`, MUL+LRP, 3 slots) halves the lit-side lift but is still a lift, and it is
new arithmetic on 108 programs to approximate a feature that exists behind a flag.

**Art direction.** *(recalled)* No shipped space game renders true black hulls: Elite Dangerous
keeps night sides very dark and relies on ship lights and skybox ambient; Everspace 2, Homeworld 3
and No Man's Sky use stylised background-tinted ambient and strong rim/back light; Star Citizen,
EVE and X4 take image-based ambient and Fresnel reflections from the skybox, so unlit sides read as
the nebula's colour; Freelancer, X3's contemporary, used a per-system ambient plus directional
lights. The common thread is that the fill is *environment-tinted and colder than the key*; a
sun-tinted constant floor is the one shape none of them use, because warm-key/warm-fill removes the
colour separation that lets a form read at equal luminance, i.e. it flattens. X3's authored idiom is
already the genre one: warm sun key, cool D1 back light where authored, nebula cube as reflection
floor, emissive windows. If a fill is ever revived, tint it from `Color1` (the D1 register, present in
the two-light PS variants of each family) with `Color0` as the single-light fallback, and use the
linear route. A per-frame background-reduction ambient ("hemisphere from the sky") stays rejected on
its mid-scene bracket (0.2–0.47 ms fenced per quad) and its dark-sector failure (fill-light §4 B).

**Cost / Windows / proof if it were built anyway.** Create-time variants, +2 PS slots (DEFAULT 178
→ 180, BUMP 190 → 192 of 512, extrapolated from the linear fill's +1), zero per draw, new artifact
hashes at k>0 and byte identity at k=0; ps_3_0 `def`/`mul`/`add` only, portable; verification is the
run-51 reduction at the same spot (module `frac<0.05`, p10, cylinder mean, same-surface reprojection)
on an original-shading capture. None of this is recommended now.

### 1a. Option C: the fill in linear light inside the original programs

The option §1 did not cover, and the one that resolves the tension with "no shipped space game
renders black hulls": keep every original instruction, and at the located lobe-sum site insert
`sum' = encode(max(decode(sum), 0) + k·decode(LightDir_Color0))`, nothing else converted. Because
the round trip is the identity and `decode(a·b) = decode(a)·decode(b)` for a pure power, the
displayed diffuse term becomes exactly `A_lin·(S_lin + k·C0_lin)`: run 54's accepted term
(fill-light §2, `g_direct` = 1 folded into k) on original shading, with the original diffuse/specular
mix, cube, lightmap and alpha untouched.

**Placement.** Before the albedo multiply, as run 54 had it. `linear_material_fill_sum` already
runs on the *original* bytecode (`linear_material.cpp:1108`, `:1333–1380`) and returns the sum
register and the albedo MUL/MAD; `xt_fill_site` does the same ahead of XT's albedo branch. The sum
there is the directional lobes plus the vertex-carried P+M (linear-bump law); all of it rides the
identity round trip. Coverage: all 94 hull/asteroid/palette/glass and 14 XT pixel programs resolve
(fill-light "Implementation"); the other stages of the 137 are vertex programs, which carry no fill.

**Lift, A≈0.7, sun luma 0.524** (F·A = 0.011 at k .03, 0.018 at k .05; same arithmetic as §1):

| face (code) | k .03 → code / EV | k .05 → code / EV | code-value fill matched at black |
|---|---|---|---|
| black 0.00 | 0.129 | 0.162 | 0.129 |
| 0.10 | 0.158 (+0.058) / +1.46 | 0.186 (+0.086) / +1.97 | 0.229 / +2.63 |
| 0.40 (sun-lit hull) | 0.415 (+0.015) / +0.11 | 0.424 (+0.024) / +0.19 | 0.529 / +0.89 |

A fill: the shadow face gains 1.5–2 EV, the lit face a tenth of that. The code-value constant is a
lift; doing nothing keeps the black quarter at 0.025–0.05 × 2.46 at the ceiling.

**Cost per covered pixel.** Exact: three scalar POWs per RGB conversion (3 slots each, the
transformer's `transfer` rate): decode sum 9 + decode C0 9 + MAD 1 + MAX 1 + encode 9 ≈ 29
weighted slots (20 with a host-uploaded `k·decode(C0)` in c215, a new constant hook; not first).
Gamma-2 approximation: `mul rS,sum,sum`, `mul rC,c5,c5`, `mad rS,rC,c215.x,rS`, `max rS,rS,c212.y`,
3× scalar `rsq` + `mul` (sqrt as x·rsq(x); the MAX keeps rsq(0) finite) ≈ 8 slots. With k' matched
at black (`F2 = F^(2/2.2)`) its error against exact pow-2.2 over code 0.05–0.60 is +0.002…+0.008
code (≤3.1 %, ≤+0.10 EV) at k .03 and .05: a slightly stronger fill on mid faces, inside run 54's
tolerances. Zero per draw, no upload, byte identity at k=0; originals sit well below the converted
programs' 178/190-of-512 slots. ps_3_0 `pow`/`rsq`/`mad` only, portable.

**Risks.** (1) Specular sits inside the sum on every family; exact form: untouched by the identity
round trip; approximate form: the same ≤3 % mid-range error, i.e. marginally warmer glints. (2) Cube
and lightmap terms are added after the albedo multiply and stay outside the fill, as in run 54;
Shared BUMPMAP's shared half coefficient is not touched. (3) Clamps: VS `MOV_SAT COLOR0` keeps
P+M ≤ 1; XT's two reviewed COLOR0 clamps are after the branch the block precedes; a later `_sat`
clips the fill only where native highlights already clip. (4) `_pp` sums and negative/NaN input:
FP16 resolution at code 0.05 is ~1e-5; the MAX excludes the rest. (5) Constant port: `c5` and
`c215` never in one instruction. (6) `transform`/`xt_transform` assume the transfer: a fill-only
config path that skips `transfer`, `gain` and the point/emissive sites is new code in those two
functions, reusing the site finders, `fill_definition`, `fill_constant_free`, the r12+ temporaries
and the combined motion/depth pipeline. (7) The meter sees the fill: ≤0.25 EV re-key, as run 54.

**Q1 verdict, restated.** *Do later, as option C only*, after #3 and on trigger: the baseline F8 at
the run-51 spot on the installed original-hull build shows a far-module dark fraction (< 0.05
scene-linear) ≥ 0.2, or the user asks for readable shadow sides. Start with the gamma-2 form at k
.05 tinted by `Color0` (`Color1` where present is the v2 tint); take the exact form only if the ≤3 %
mid-range error is seen. Acceptance, same reductions as fill-light §5: baseline F8 pair (≈350 m /
≈210 m) on the installed build, then a matched pair with the option — far-module `frac<0.05`
to ≤ 0.10 and p10 ≥ 0.045, cylinder mean rise ≤ 0.03, module chroma within 0.03, far-dark
same-surface median gain ≤ 2.0, `frame_end` unchanged; and the user's read at Auto +1.3.

## 2. Selective exposure

The contract (B/e + H, background-metered Auto, exposed-linear TAA history Z = D/4, exposed-
destination packed-screen law, fade scratch normalisation, VS c250 / PS c222 shadowing, Reset and
recovery, sun-lane invalidation) was written to solve one problem: converted hulls looked bleak, and
Auto lifted them further while the user wanted highlights and emitters to keep the exposure. Three
facts have removed the problem rather than solved it:

- The base is now original shading, which the user prefers *at* Auto +1.3, hulls included. Pinning
  it to EV0 would make the hulls darker than the accepted look.
- Emitter/hull separation is delivered by `--emission-source-gain 2` and
  `--screen-emission-additive 2`: one MUL per emitter fragment, no bracket, independent of
  exposure and of linear materials (linear-emission-cost §4 "Implemented"; status 2026-09-15). That
  is the cheap form of "expose H, not B", on the terms the user chose (cost over exactness).
- The design depends on linear materials structurally, not by a gate: compensation is inserted at
  the *decoded* diffuse/point/fill seeds of transformed programs (§4 "Placement"), and milestone 1
  is a transformer feature. On original code-value programs the seed scale would be `e^(1/2.2)`
  instead of `e`, a new per-family proof for fused/packed instructions (Terran MAD, Split lanes),
  and the runtime — the part that was held for cost — is unchanged: a once-per-frame meter bracket
  at the depth-only Clear, gamma decode plus e/4 on every TAA current read (centre + nine taps), a
  specialised screen composite, expanded constant shadowing across five source files. None of that
  is smaller under original shading.

Cost of continuing: the five-file integration in §9 of the contract, a GPU pass over the material
fixtures, a TAA fixture in the new history domain, and one A/B run, for a mode whose visible effect is
now to darken the hulls the user just accepted. Verdict: close the allocation gate (material-
investment "Critic reassessment") as *not pursued*; keep the reviewed source and the ledger
(`docs/verification/material-exposure.md`) as the checkpoint; amend goals.md row 6 ("the user also
approved keeping base hull shading at EV0…") to "approved in principle for converted hulls; not
pursued under original shading". Windows portability is unaffected either way. The reopen trigger in
the table is the only one that a cheaper knob cannot serve: `--hdr-ev` and `--hdr-ev-max` change
hulls and nebula together, the two gains change emitters alone, so only "hulls down, nebula and
emitters up" needs per-material exposure.

## 3. The point-light cull

**Difficulty: low–medium, and the RE has done the hard part.** The decision is the six-byte `JG`
at `0x004c27af` inside `0x004c0150`, predicate `round(|node − light|) ≤ light[+0x158] + node[+0x70]`
in raw render-domain integers, run per submitted mesh part per view (camera-and-lights "Point-light
admission site"). At the site `EAX` and `EFLAGS` are dead-out, `ECX`/`EDX` scratch, the x87 stack
empty, `ESP` 16-aligned, nothing branches into `0x004c27a1`–`0x004c27b5`. The proposed change keeps
the node-origin distance already in `EAX` and substitutes the **root's** `+0x70` for the node's:

```
detour:  MOV  ECX,[EBP+0xc]        ; submitted node
         ADD  EAX,[ECX+0x70]       ; undo the node scale
walk:    MOV  EDX,[ECX+0x18]       ; parent link (0x00489f20 attach, 0 at root)
         TEST EDX,EDX
         JZ   root
         MOV  ECX,EDX
         JMP  walk
root:    SUB  EAX,[ECX+0x70]       ; root scale
         TEST EAX,EAX
         JG   0x004c29f5           ; reject
         JMP  0x004c27b5           ; admit
```

Integer only, three scratch registers, no stack, no flags to reproduce, ~12 instructions plus the
`JMP rel32 + NOP` at the site. It uses the same in-process mechanism as `--lod-scale` and the seven
chase-restore sites: `engine_patch` with `VirtualProtect`, byte-verified original bytes, read-back
compare, rollback (`lod-scale.md` "Write"; `src/proxy/engine_patch.cpp:126–135`). On the run-22
geometry it admits all ten clamp nodes in both frames (root radius 19 536 ≫ 1 843) and still rejects
the 16 km outpost (81 192 > 1 000 + its root scale).

**Cost on the hot path.** Per light test: a pointer chase of hierarchy depth (station modules:
one or two loads) added to a ~30-instruction test that already calls `sqrtf` and float→int; two of
eight slots were populated in run 22, so the addition is a few thousand loads per frame. GPU side:
parts that were `i0 = 0` now run one vertex-loop iteration per vertex, the same iteration the station
body already runs at 3 461 units. Neither is measurable at frame scale (inference; the `frame_end`
window median of the acceptance run is the check).

**Risk, honestly.** (a) Over-lit faces: impossible by construction — attenuation `1/(1+0.01d)` is
untouched, so any newly admitted node receives ≤ 0.09 of the light beyond the old 1 000-unit edge
and 0.03 at 3 000; the *pop* moves from ~240 m to where the whole object leaves range
(≈20 km for a station, contribution 0.005, invisible) and for multi-node ships it stays at the same
distance but becomes whole-ship consistent. (b) It changes lighting for every multi-node object and
every point light, not the observed station; the only point light found is the player headlight
(slot 1 was a stale entry the count never reached), and other lights would keep their own range
and attenuation. (c) `+0x18` as the parent link is established from the attach/detach helpers and
the cull pass (`0x00489f20`, `0x00489dbe`, `0x0047cfe0`), not from a capture, for every node class
that reaches this path; a node with a non-node value there would chase garbage — the detour should
bound the walk (e.g. 16 steps, then fall back to the node's own scale) so an unexpected class
degrades to native behaviour instead of faulting. (d) It runs under two SEH frames in the hottest
submission routine; an integer-only detour with a bounded walk raises no exception. (e) The site
mutates shared light state (`[light+0x16c]+0x68 = slot`) for admitted lights — unchanged by the
patch. (f) Native Windows: the EXE is non-relocatable and identical (`fdbf3418…` gate); the patch is
documented Win32 (`VirtualProtect`, `FlushInstructionCache`), process-local, file untouched, and
integer-only, so the FEX x87 caveat does not apply. Unverified on Windows like every other patch;
add the row to `platform-portability.md`.

**Value.** The cliff is a discontinuity on the most-watched geometry in the game — docking rings and
clamp arms pointing at the camera at 200–350 m — and it was reported by the user as a suspected mod
bug before run 22 attributed it, and seen again in vanilla (run 21 B). Under original shading there is
no fill to halve it; the patch removes its cause for all point lights at once. Against fill as a
paint-over: the linear fill halves the pop (2.8× → ≈2×) and does nothing under original shading; the
patch removes the pop within an object and leaves the shading law alone. They are complementary, but
only the patch applies to the accepted configuration.

**Alternative rejected: widen `+0x158`** (two immediates at `0x0044ae70`/`0x0044ae4e`, no
trampoline). It moves the per-node pop outward (at 2 000 units the light is still 0.048, a visible
step) and, unless `+0x160` is scaled with it, softens the falloff everywhere; it touches only the
headlight, which is a smaller blast radius but leaves the per-node inconsistency that is the actual
defect. Also rejected: the proxy-side forced `i0 = 1` with a synthetic light (station-material-distance
"Fix direction"), which duplicates the engine's decision with a guessed light.

**Verification.** (1) Host: a CPU harness in the chase-restore pattern
(`verification/results/chase-restore-cpu.json`, 722 checks) that executes the detour bytes over
synthetic node chains (root, depth 1–3, bounded-walk overflow) and light records, asserting
`ESI`/`EBX`/`EDI`/`EBP`/`ESP`-locals preserved, x87 stack empty, and the 22 run-22 outcomes
reproduced with the root rule (ten clamps admitted in both frames, body admitted, outpost rejected).
(2) Game, one F8 pair at the run-51 spot (≈350 m / ≈210 m) on the next run the shadow/loading work
already needs: `i0.x = 1` on all ten clamp nodes in both frames; same-surface far→near median gain
of the far-dark points from 2.8 to ≤ 1.7 (the smooth-attenuation-only prediction 1.46–1.71,
run 22); `frame_end` window median unchanged; `patch` lines showing the site verified and applied.
No Wine fixture is needed beyond the CPU harness.

## Ranking against current goals and the work in flight

1. **Finish what is in flight**: the run-65/66 receipts, the `--shadow-replay-candidates` predicates
   (goal 8, shadow-replay-gates §1) and the loading attribution (goal 14). No material question here
   pre-empts them; nothing above needs a new user run of its own.
2. **#3, root-object admission**: small, default-off, cause-fixing, the only item whose value survives
   the switch to original shading; one CPU fixture and a piggy-backed F8 pair. Do it after 1.
3. **#1, fill on original shading**: option C (§1a) when its trigger fires; the baseline F8 pair
   rides the same run as #3's acceptance, so the trigger costs no run of its own.
4. **#2, selective exposure**: close; checkpoint retained.

## Do not

- Do not re-enable or extend linear hull conversion to obtain a fill, the sun lane or selective
  exposure; the user's base is original shading.
- Do not build a code-value constant fill of the 108 programs (gamma-space lift; §1); if a fill is
  built, it is option C (§1a), not a lift and not a full conversion.
- Do not integrate the selective-exposure runtime (meter move, Z history, exposed-destination screen
  law); do not port its seeds to original shading.
- Do not widen `+0x158`/`+0x160`, and do not force `i0 = 1` from the proxy.
- Do not add a background-reduction ambient bracket; D1 is the authored proxy for a nebula tint.
- Do not start PBR/roughness heuristics, per-material review, cube-response calibration or a
  nebula-clamp survey on the back of any of this.
- Do not queue a user run for these questions; attach the F8 pair to the next shadow/loading run.
- Do not revive AO to modulate a fill (ambient-occlusion-scale.md decision stands).

## Unknown, and what settles it

1. The shadow-side floor under original shading is unmeasured; run 51 is a linear-route capture. One
   F8 at the run-51 spot on the installed original-hull build, reduced with the fill-light §1 script,
   settles #1's premise (and can share the #3 acceptance frames).
2. Whether `+0x18` is a parent pointer for every node class reaching `0x004c27af`: static from three
   sites. The bounded walk makes the failure mode "native behaviour", and the `i0.x` table of the
   acceptance run is the positive witness; a disassembly of `0x0047d9c0`/`0x0047e6e0` callers listing
   the node classes submitted would close it without a run.
3. Whether the two-light PS variants are selected per sector (so a `Color1` tint is sector-
   consistent): irrelevant unless #1 is revived; the shader-set log of any two-light sector answers it.
4. Meter re-key under a hull lift (§1) is extrapolated from the linear fill's ≤0.25 EV; only matters
   if #1 is revived.
5. Native Windows execution of any of the three: unverified, as for the whole project.
