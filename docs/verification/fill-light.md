# Material fill-light verification ledger

Final review PASS, 2026-09-15. This is evidence for the fill transform and its
explicit K=0 parity mode. A later user decision selects 0.03 as the production
default with linear materials; this ledger does not establish screenshot
acceptance for 0.03 or native-Windows behaviour.

## Provenance and scope

- Initial implementation: `294c155` (Constant hemispherical fill in the
  converted material law). Its follow-up corrections and candidate
  qualification are recorded in the contemporaneous handoff below.
- Reported host build: `cmake --build build -j4` passed with zero warnings
  ([`/tmp/x3-fill-qualification-build.log`](/tmp/x3-fill-qualification-build.log)).
  `build_motion_output.sh` also passed
  ([`/tmp/x3-fill-live-build.log`](/tmp/x3-fill-live-build.log)).
- All detached fixture results below used bottle **X3**, WineArch **arm64**,
  `FEX_X87REDUCEDPRECISION=1`, and `WINEMSYNC=1`; none launched the game.

## Host checks

Reported author checks passed: fill/live, 21 tests and the retained 20,490
seam checks; fade, 8 tests covering 130 originals in three exact
configurations; and the corrected reference/report suite, 115 tests in
59.591 s. The targeted duplicate-validator check passed one test in 4.893 s. MinGW
syntax compilation reported zero warnings. After the review corrections, the
affected live suite passed 17 tests and 20,495 checks.

Review corrections: Auto exposure is no longer compared with the fixed-EV0
run-51 baseline; failed insertion reports `fill_applied=0`; the luma metric is
the pre-RT Rec.709 scene-linear value decoded from the RGBA32F compatibility
output; and the report validator rejects missing or duplicate creation and
sample evidence. Final review passed with no blockers.

## Detached results

| Slice | Result | Evidence |
| --- | --- | --- |
| K=0 live route | PASS | 8 configurations, 32,516 checks. The corpus covers actual `evaluate_draw` execution across 148 exact pairs / 115 originals, material on/off, TAA on/off, ownership, Reset, state blocks, cached gains and shader retirement. `linear-material-fill-live.json` (`../../verification/results/bottle-X3/linear-material-fill-live.json`) |
| K=0 detached parity | PASS | The new corpus has 4,177 cases and 37,593 samples across 168 pairs / 137 originals, including 14 XT and six SM3 glass pairs. Against the retained 3,923-case baseline, all 42,596 shared `SAMPLE`/`BASELINE`/`INVARIANT` rows are equal: 0 missing and 0 different. The 4,826 additions are the review-confirmed 254 glass cases × 19. `linear-material-fill-k0-gpu.json` (`../../verification/results/bottle-X3/linear-material-fill-k0-gpu.json`), [`/tmp/x3-fill-k0-parity.json`](/tmp/x3-fill-k0-parity.json) |
| K=0.06 fill oracle | PASS | 23 sun-averted FP16 cases, 23 pairs, 207 FP16 samples and 207 RGBA32F twins. The 17 hull/asteroid/palette families, four XT standard/damage techniques, and glass agree with the one-FP16-code oracle: encoded FP16 RGB tolerance 1, observed maximum 1; alpha is exact. Pre-RT Rec.709 scene-linear luma has tolerance 1 and maximum 0. The separately reconstructed FP16 image has luma maximum 3; it does not strengthen the pre-RT image claim. The 54 ordinary-black samples are included. `linear-material-fill-gpu.json` (`../../verification/results/bottle-X3/linear-material-fill-gpu.json`) |

The K=0 detached run took 28.184 s inside its child process (lock wait
0.000003 s); the corrected oracle run took 5.970 s (lock wait 0.000004 s).
[`/tmp/x3-fill-k0-timings.json`](/tmp/x3-fill-k0-timings.json) and
[`/tmp/x3-fill-oracle-corrected-timings.json`](/tmp/x3-fill-oracle-corrected-timings.json)
retain the measurements. These are fixture timings, not GPU timestamps or
gameplay FPS.

The hardened duplicate validator post-validated the retained GPU artifact and
its raw report without a GPU rerun: 23 filled pixel-shader creates, 207 sample
and 207 luma rows, with the retained artifact and runtime hashes bound to the
new validator. [`post-validation-2026-09-15.json`](/tmp/x3-linear-material-fill-gpu-corrected/post-validation-2026-09-15.json)
records that binding.

## Boundaries and open evidence

The K=0.06 oracle excludes the XT terraformer: its occlusion RGB also feeds an
additive-emission source, so it is outside this isolated fill-law comparison.
The K=0 live and detached checks do not prove a user-visible fill change.
Run 23 supplies limited CrossOver gameplay evidence below; gameplay performance
and native Windows runtime remain unverified. The current installed-build
description and compact install record are linked in the candidate handoff below.
The change adds no per-draw work; this evidence includes no benchmark or FPS
claim.


### Candidate handoff

The clean reviewed candidate has passed its host audits and X3 load check and
was installed by the main session. [Status](../status.md) is the authoritative
installed-build description; [the compact install record](../../verification/results/run23-candidate-install.json)
binds source, toolchain, hash, scoped results and rollback. Run23 later
completed; its evidence supports the fill law at explicit K=0.06. The later
0.03 default is the user's appearance decision, not a result claimed by this record.

### Run 23 / run54 gameplay evidence

The user completed Run 23 in `/tmp/x3-bottleX3-run54` with `--material-fill
0.06`. The analysis maps screenshots/readbacks as Auto far/near frames
3481/4887 and fixed-EV0 far/near frames 7060/7687. The user reports brighter
hulls and asked about 0.04; at that checkpoint **0.06 was retained provisionally**
and 0.04 was an optional preference bracket. The later user decision selects
0.03 as the production default; there is no 0.03 screenshot acceptance in this run.

The fixed-EV0 far frame passes the stated numeric thresholds: module p10
0.0672 (minimum 0.045), dark fraction 0.0115 (maximum 0.10), and cylinder mean
0.1519 (maximum 0.165). The approximate reprojected far-dark gain is 1.781,
which is supportive only: it has 172 dark samples and no root mask. The poses
differ, and unmatched dark-chroma difference is about 0.081, so that result is
neither a passing chroma check nor a controlled comparison. Auto exposure is a
global pre-tonemap multiplier: +1.5 EV gives 2.828×. The analysis result is
[`results.json`](/tmp/x3-run54-fill-analysis/results.json).

### Selected defaults checkpoint

The user selected fill 0.03 and Auto exposure ceiling +1.0 on 2026-09-15.
Source/default review passed; the three focused host modules passed 34 tests
(independent run 9.298 s), and capture syntax compilation passed with the
project ABI flags. Main's affected `./x3run --dry-run` emitted Auto, EV max 1.0
and fill 0.03 without explicit value options; vanilla dry-run also passed.
The current launcher applies these defaults to the existing DLL; its direct
initialization defaults update with the next candidate. Explicit fill 0 and
EV ceiling 1.5 remain supported. This preference choice does not establish
new 0.03 image/GPU acceptance or change the retained 0.06 law oracle.

### 2026-09-16 raised defaults (fill 0.05, Auto ceiling +1.3 EV)

The user raised the two appearance defaults: `X3M_MATERIAL_FILL` 0.03 → 0.05
and `X3M_HDR_EV_MAX` 1.0 → 1.3, in the launcher and in the DLL's direct
fallback (`src/proxy/capture.cpp`). Explicit `--material-fill 0` still disables
the term and keeps byte-identical programs; fill still requires
`--linear-materials`. The motion-output runner's mirror of the DLL ceiling
(`HDR_EV_MAX_DEFAULT`) moved with it so the reference adaptation still clamps
like the DLL. Host/default evidence only; this does not establish new image or
GPU acceptance at 0.05/+1.3.

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_linear_material_fill verification.analysis.test_linear_material_live verification.analysis.test_comparison_hotkeys verification.analysis.test_motion_output_runner verification.analysis.test_screen_emission_live   # OK
cmake -S . -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -B build && cmake --build build -j4   # 0 warnings
python3 verification/probe/check_no_x87.py build/d3d9.dll                        # result PASS, reachable_functions=225, violations {}
./x3run --camera chase --motion-output --object-trace --ownership --object-lifetime --taa --hdr --hdr-tonemap --linear-materials --dry-run   # X3M_HDR_EV_MAX=1.3, X3M_MATERIAL_FILL=0.05
```

### Original fill (option C), 2026-09-16

`--original-fill K` (`X3M_ORIGINAL_FILL`; docs/architecture/original-shading-critique.md 1a
"Implemented"): the exact-power fill block inside the ORIGINAL pixel programs' motion variants,
no linear materials. Worktree commit rebased on `48d70c3` (renderer, fixture and runner inputs
unchanged between `444478a` and `48d70c3`); scratch clean DLL `build/d3d9.dll` sha256 `e70c3d1c52555a3d…`
(0 warnings; `check_no_x87.py` PASS, reachable_functions=230, violations {}); not a candidate.
Bottle **X3**, WineArch arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`; no game launch.

| Slice | Result | Evidence |
| --- | --- | --- |
| Host oracle | PASS | `test_original_fill.py`, 6 tests: 108/108 reviewed PS covered (hull 66, asteroid 4, palette 20, glass 4, XT 14), 29 VS refused; K=0 byte-identical to the motion variant for every program and both depth modes; K 0.03/0.05 add exactly `def c215` + the 14-instruction block before the site (albedo MUL/MAD, or XT `if`), +32 weighted slots and +14 instructions each, largest variant 183/512; c215 read by no original; launcher gate (requires `--hdr`, excludes `--linear-materials`, 0..0.5, default `0.0`) and DLL gate substrings. |
| Detached GPU, K 0.03 and 0.05 | PASS | `run_linear_material.py --original-fill`: 23 fill pairs × 4 faces (calibration M=1, black, 0.10, 0.40) = 92 cases per K, 621 RGB samples per K. Baseline = the original's plain motion PS; hull/asteroid/palette/glass faces prove `base32 = S·A_eff` to 4.7e-8 relative; XT faces take S from the original's own readback. K variant vs `encode(decode(S)+K·decode(C0))·A_eff`: FP16 code distance ≤ 1 (max 1), RGBA32F pre-store relative error ≤ 1e-4 (max 4.8e-7); alpha, motion and depth identical; the K=0 variant bit-exact against the baseline (measured: 92 compared cases per K, 0 differing pixels). Note `g_direct` is folded into K, so K is not numerically the run-54 `--material-fill` constant (equivalence unverified); with K>0 the site MAX floors a negative/NaN lobe sum at ε (~0), a behavioural difference from the unfilled program; the XT faces are validated against a site sum read back from the original (weaker than the hull families' strict `base32 = S·A_eff`). Child 9.17 s, lock wait 4 µs. [`original-fill-gpu.json`](../../verification/results/bottle-X3/original-fill-gpu.json) |

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_original_fill   # OK, 6 tests
sh verification/probe/build_linear_material.sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_linear_material.py --exe verification/probe/build/linear_material_fixture.exe --original-fill --raw-dir <scratch>   # passed: true
cmake -S . -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -B build && cmake --build build -j4 && python3 verification/probe/check_no_x87.py build/d3d9.dll
./x3run --camera chase --motion-output --object-trace --ownership --object-lifetime --taa --hdr --hdr-tonemap --original-fill 0.05 --dry-run   # X3M_ORIGINAL_FILL=0.05, X3M_LINEAR_MATERIALS=0, X3M_MATERIAL_FILL=0.0
```

Open: the F8 pair acceptance of the critique's verdict (user run on an installed candidate), native
Windows, and the performance pass, deferred to that run. Run acceptance adds to the critique's 1a
criteria: the `frame_end` median delta with `--original-fill 0.05` on versus off in the same sector
(9 POW per pixel on every routed opaque hull draw) must be reported and acceptable to the user. `test_linear_material_live.test_production_control_flow` fails to compile its extracted
`composition_blend_count` snippet identically on the pristine base `444478a`; unrelated to this change.

### Run 28 original-fill A/B, 2026-09-16

Installed candidate `2b0969e5…` from `a26eb9b`. Snapshots run78 (`X3M_ORIGINAL_FILL=0.0`),
run79 (`0.05`) and run80 (`0.02`); run79/80 log
`original_fill_mode requested=1 enabled=1 hdr=1 linear_materials=0 fill_valid=1`.

Appearance: the user reports the option works visually. Screenshots
`original_fill_0.png` (`../../screenshots/original_fill_0.png`),
`original_fill_05.png` (`../../screenshots/original_fill_05.png`),
`original_fill_02.png` (`../../screenshots/original_fill_02.png`); the orchestrator's read is that
K=0.05 makes the unlit docking modules and the asteroid legible at the cost of a mild flattening of
hull contrast, and that K=0.02 buys little.

Cost: **not measured**. The three sessions are not like-for-like — run78 ran ~9,300 frames including
two sector transits, run79 ~2,100 frames with an 8-frame F8 capture that stalls 400–800 ms on each
captured frame, run80 ~5,100 frames clean (steady 15.9–18.3 ms/frame over its 300-frame windows) —
and `frame_end` is emitted only every 300 frames, so no per-frame series exists. The critique's 1a
acceptance criterion (median `frame_end` delta with 0.05 on versus off in the same sector) stays open
and will be measured over a like-for-like window with `--frame-timing`
([sampling-profiler.md](sampling-profiler.md), commit `8954cff`), which reports per-window frame and
Present time with the slowest frames.

### Original fill 0.02 as the launcher default, 2026-09-25

User decision 2026-09-25, accepted in flight: `--original-fill 0.02` is the launcher default on every
modded `--hdr` launch, following the `--taa-box-resolution` pattern (commit `c2aa8d4e`). Omitted with
`--hdr` (and without `--linear-materials` or `--vanilla`), the launcher sends `X3M_ORIGINAL_FILL=0.02`
with `X3M_ORIGINAL_FILL_DEFAULT=1`; an explicit value is sent with marker 0, and explicit
`--original-fill 0` stays the opt-out (byte-identical original programs). Under `--linear-materials`
(its `--material-fill` applies), without `--hdr` and under `--vanilla` no default applies: the variable
stays an explicit `0.0` against a stale shell value and no marker is sent. Parser errors are unchanged.
The DLL default when the variable is unset stays 0 (fixtures unchanged); it reads the marker (exactly
`1`, counted only with an enabled fill) and logs it as `default=` on the `original_fill_mode` row;
`run_motion_output.py` drops an inherited marker. Host test `test_original_fill_default` (8 tests);
dry runs (`verification/results/original-fill-default/dry_runs.py`): modded `--hdr` 0.02/1,
`--original-fill 0` 0.0/0, `--hdr --linear-materials` 0.0/none (material fill 0.05), `--vanilla`
0.0/none. No Wine run (DLL default unchanged).

**Same day, later:** the user lowered the default to `0.01` (`ORIGINAL_FILL_DEFAULT` in `tools/manage.py`; test updated: the modded `--hdr` dry run sends `0.01`/`1`). The DLL is untouched: the value is a launcher default, so the Run83 candidate's dry-run record still shows `0.02` for that field and the main checkout's launcher, which every flight uses, sends `0.01`.

### Fade-band fill, 2026-11-01

Run 216 (1.8.3, AP native) report "distant objects are dark and light up on approach". Triage on two F8 captures
(`verification/results/run216-distant-dark/README.md`, measured): the big asteroid node 4a9e8058 (model 509e, LOD 0,
PS `d44db87778a43b61` in both frames) is drawn in the engine's fade band at frame 12898 (SRCALPHA/INVSRCALPHA,
z write 0, `fade_arm=1`, permille 997, view depth 149 km) and opaque at 15576 (118 km); its night side has HDR
luminance p50 0.0000 far and 0.0701 near at the same EV (1.18961). Shadows, far fade and exposure ruled out. Cause:
`bind_variant_pair` withheld the original fill and the hull light-map gain from every fade-arm row (`!route.fade_arm`),
so the fill switched on with the engine's opaque draw; stations lost their light-map gain the same way (from the code).

Change: a fade-arm row of a fade pair now takes the fill and the gain (and the widened gain) its opaque draw takes —
the plain fill-family program when RT2 is masked, the program's invalid-share form (new, `sun_fill_variant` /
`sun_lightmap_variant` / `sun_lightmap_widen_variant`) for an owner on the sun lane; never the share producer, never a
receiver; the overlay arm, the engine light and every opaque row unchanged
([linear-distance-fade-region.md](../architecture/linear-distance-fade-region.md) "Fade-band fill"). No settings key.
Evidence: host fixture `fade_band_fill_selection` (24 rows) in `test_motion_wrap_states` passes (34973 checks, 0
failures); against the pre-change `bind_variant_pair` (c2838446) the same fixture fails exactly the 12 fade rows that
now take the fill / gain family (masked, owner, fill 0 with the light-map flag on, owner withdrawn, owner fill form
refused) and passes the 7 opaque, 1 overlay, 2 fill-less flag-off and 2 refused-form flag-off rows
(`verification/results/run216-distant-dark/fade_fill_selection_ab.py` → `fade_fill_selection_ab.txt`).
Follow-up after review (2026-11-02): `fade_route_frame` gains `fade_fill= fade_gain= fade_widen=` and registration
logs one `fade_owner_forms` row per program with created owner forms, so a flight log shows the fix directly. Native D3D9
validator (`gen_fade_owner_forms.cpp` + `hull-widen-native-validator/native_validate.cpp`, `summarize_native.py`):
AP captures 22 programs, 62 owner forms + 60 strength twins, 122/122 pass, the asteroid PS's `ofill` included; FL
captures 23 programs, 133/133 pass. Focused host suite (14 modules: original fill, fill default, fade region, sun
share lane, light-map gain, widening, cutout contract, thin vote, FL materials, shader population, logging tiers,
motion-output runner, wrap states, material strength): 135 tests OK, 10 corpus skips. AP and FL builds 0 warnings.

Flight check (open): fly toward a large asteroid (and a station) from beyond its fade distance; F8 once in the fade
band and once after it turns opaque. Pass: the asteroid's dark side has the same HDR luminance in both captures (no
step; previously 0.000 vs 0.070) and the station's windows keep their brightness; the only visible transition is
the engine's own alpha ramp. Log: `fade_owner_forms` rows at load and `fade_fill` > 0 on `fade_route_frame` while
the asteroid is in the band.

### XT BUMP stations in the fade band (Run 217), 2026-11-02

Run 217 (1.8.4, AP native) report after the asteroid fix: distant stations are still dark in the fade band and light
up when they turn opaque. Triage (`verification/results/run217-outpost-dark/README.md`, measured): station
`stations\sadiablo\freighter_base` (node 4bb6a2f8, model 5c09, LOD 1 in both frames) draws VS `37c34a7478544c14` with
PS `5f82ecacd39529cd` / `f1b0e820c7b488c3`; at frame 5845 the three draws are in the engine fade-band state (blend
5/6, z write 0; one alpha tested) and refused `overlay_node` / `no_zwrite`, at 7063 they are opaque and routed. The VS
had no `fade_route` registers row (excluded as the "damage" VS, `fade-rt2-ownership.md` section 3), so the arm, and
with it the fade-band fill and the owner forms that exist for both PS, never saw the draws.

Rule: the VS is row 10 of `fade_route_core.h` (c40 / c44 / b0 from the inventory CTAB), flagged `fog_branch`.
`fade_arm_admits` reads b0 once after the state check: TRUE continues as a fade pair (fraction, 500 permille
threshold, hysteresis, RT2 owner, tested cutouts, the fill), and the fraction reuses the read; FALSE hands the draw to
the overlay arm exactly as before the row. The fraction alone does not separate the two: an `MPF_ALPHABLEND` material
draws the same state with AlphaValue 1 and b0 FALSE, so its fraction is 1000 (measured on a table VS: the docking-port
pair `4944d81d/64bac8bb`, frames 5849-5852, `fade_arm=1 fade_permille=1000` beside opaque draws of its node). Material transparency at or beyond N also carries b0 TRUE (fade-rt2-ownership.md §3), so XT windows beyond N take the
fill as distance-fraction owners and drop it nearer than N: watch window sub-meshes as a station crosses N. b0 is
the engine's own branch bit: TRUE only on the distance-fade branch (`0x004c2e04`, static). In Runs 216/217 every
fade-arm row of the asteroid programs (1,126 rows) has permille below 1000 with no node alpha override (alpha13c 0),
which with AlphaValue 1 requires b0 TRUE (inferred); all 464 fade-state draws of `37c34a74` are on nodes at cull
distance 16.1-20.3 M with no opaque draw of the same node, every opaque one at 15.8 M or nearer
(`fade_state_census.py` -> `fade_state_census.txt`). The glass VS `c30104cb0efb6675` stays out.

Evidence: host fixture `fade_arm_selection` in `motion_wrap_state_fixture.cpp` runs the production `fade_arm_admits`
(11 cases: the 5845 hull and tested rows routed at 997 and bound to the fill, the band end refused at 144, material
rows with b0 FALSE refused `overlay_node` / admitted at 1000 after a routed draw of the node / tested refused
uncounted, b0 read failure, owner off, glass, asteroid); 35,076 checks, 0 failures. Against b1ec6b0f
(`fade_arm_ab.py` -> `fade_arm_ab.txt`) the band, band-end and read-failure rows fail on their outcome and the three
material rows only on the added b0 getter. Host modules `test_fade_region` (10-row table, `fog_branch` only on the XT
row, XT pairs fade pairs only with the owner), `test_original_fill`, `test_shader_population`,
`test_motion_wrap_states`, `test_sun_share_lane`, `test_hull_lightmap_gain`: 62 tests OK. AP and FL builds 0
warnings (AP 136, FL 96 C++ objects). FL objects against a clean build of b1ec6b0f
(`farnhams-legacy-build/cmp_objs.py` -> `run217-outpost-dark/fl_object_compare.json`): 77 of 81 identical; different
`motion_output.cpp`, `shader_population.cpp` (the table), `proxy_identity.cpp` (source commit) and `capture.cpp`,
whose disassembly differs only in MotionOutput member displacements and its size (0x16ae00 -> 0x16ae08: the
`Registers` flag). Cost: one bool test per fade-band draw; one b0 getter
moved earlier for this VS's fade-band draws and one added for its material draws (none observed in two flights).

Flight check (open): approach an XT station (the sadiablo freighter base, the USC mine modules) from beyond its fade
distance; F8 once in the band and once after it turns opaque. Pass: the hull's lit and dark sides keep their
brightness across the switch (no step), windows unchanged. Log in the band: `motion_route` rows with
`vs=37c34a7478544c14` in the fade state show `fade_arm=1 routed=1`, permille 500-999 (the alpha-tested sub-mesh with
`atest=1` routed too) instead of `unmatched=overlay_node` / `no_zwrite`; `fade_route_frame` `fade_fill` and
`fade_tested` rise accordingly; near XT draws unchanged (opaque, routed).

**Run 218 (2026-11-02, dev candidate, no release).** The XT BUMP row (`d812702c`, reviewed Opus medium: merge; doc
fix `4e6b16a5`; merge `793a2443`) built unstripped in `build-ap` (RelWithDebInfo, 0 warnings, x87 PASS, 62 affected
host tests green) and installed into `C:\X3` (SHA-256 `9be4c8a4…`, rollback `rollback-d3d9\d3d9.dll.run217` = release
1.8.4; [record](../../verification/results/run218-candidate-install.json)). Flight: approach the remote outpost (XT
`freighter_base`) from beyond its fade distance; F8 in the band and after it turns opaque; pass = no brightness step
on the hull; rows `motion_route … vs=37c34a7478544c14 fade_arm=1 routed=1` at permille 500–999 in the band,
`fade_route_frame … fade_fill`/`fade_tested` rising; watch window sub-meshes as the station crosses N (review F1);
a lit-side step is read against the receiver difference (review F5), not the guard.

**Run 218 flown (2026-11-02).** User: the outpost is the correct colour at distance now; a residual switch remains
between the band and the opaque state, suspected specular or material (four F8 captures: distant, closer, distant,
closer). Triage under `verification/results/run218-outpost-switch/`.

### Fade-owner shadow receiver (Run 218 outpost switch), 2026-11-02

Cause ([run218-outpost-switch](../../verification/results/run218-outpost-switch/README.md)): the band station is an RT2
owner with `.g = -1` on every pixel and casts nothing, so its sunlit faces and highlights drop 11-33 % and its
self-shadow pops in when it turns opaque. Change ([fade-owner-shadow-receiver.md](../architecture/fade-owner-shadow-receiver.md),
option B, P = 950): a fade owner on the sun lane at or above `fade_route::owner_shadow_permille` (950, a named
constant in `src/proxy/fade_route_core.h`, not a setting) and at or below `owner_shadow_permille_max` (999) binds the share form its opaque draw binds
(`ps_sun_original`, `_lightmap`, `_lightmap_widen`), counts as a receiver in `sun_frame_.draw` and is admitted by
`note_candidate_draw` as a caster (`zwrite = !route.fade_arm || fade_route::owner_shadow(...)`). This supersedes the
Fade-band fill entry's "never the share producer, never a receiver" for those rows only; below 950, with the owner off
or withdrawn, on the overlay arm, at exactly 1000, or without a share pair (the owner form, no frame veto) nothing
changes. The upper bound (review F1): the per mille is `g_AlphaValue` x the fog factor only, so a blended material
draw at full alpha (b0 FALSE, AlphaValue 1) reads exactly 1000 and is not a distance fade. Census of the session in
`/mnt/c/X3/x3m.log` (20261102-011747-31008; `verification/results/run218-outpost-switch/permille_1000_census.py` ->
`permille_1000_census.txt`, measured): 672 routed fade-arm rows, 16 at exactly 1000, all VS `4944d81d` (the
docking-port glass pair, which has no row below 1000); the other 656 (VS `167eb2d5`, `37c34a74`, `b0602757`) at
979-996, none below 950. Without the bound those 16 glass rows would have cast full-depth shadows and received with
their own f. Frame evidence: `fade_route_frame` gains `fade_receiver=` (of `fade_routed`, rows that bound a share
form) and `fade_caster=` (of `fade_routed`, rows that became managed caster candidates in `note_candidate_draw`). Zero new
programs; per draw one integer compare in the bind and one in the caster admission, no allocation, no getter; an
admitted band caster costs what its opaque twin costs (the same box test, `sample_candidate_sun` and replay
primitives). The shadow pass, the apply and the resolve are unchanged.

Evidence (host, measured): `fade_band_fill_selection` 39 rows (24 + 15: owner at 998 plain / gained / widened / fill
off and at 950 -> share form, receiver, caster; 949, 940 and exactly 1000 (plain / gained / widened) -> owner form,
neither; 998 without a share pair -> owner form, caster, no veto; 998 withdrawn, owner off, no lane, overlay ->
unchanged) and `fade_arm_selection` on the lane (the admitted 997 rows bind the share form; the 1000 row
`asteroid_no_fog` keeps the owner's motion form) in `test_motion_wrap_states`: 35264 checks, 0 failures. Kill switch
(`owner_shadow_permille` 1001): 7 cases fail (the 5 receiver rows and the 2 caster-only rows at 998); with
`owner_shadow_permille_max` at 1000 the three 1000 rows and the constant's source pin fail. The seam gained
`own_sun_` (main's own-sun step 1 forwards `after_reset` to it; the fixture no longer compiled on main). Open issue 1
of the note (do the share forms carry the owner depth fragment?) is settled by
`test_original_sun_share.FadeOwnerShareFormTests` (driver `verification/probe/fade_owner_share_forms.cpp`, local
corpus): 108 programs, 308 share forms (100 x 3 + 8 plain), owner transform on vs off, 1848 float64 executions: no
`def c218`; RT2 `.a = 1` exactly with `c218 = (0|0.37, 1, 0, 0)` (owner row), `.a = w` with `(0, 0, 1, 0)`; `.r`,
`.g` (share), `.b` and `oC0` identical to the owner-off form. With the owner transform left off the test fails.
`run_host_suite.py --modules test_fade_region test_original_fill test_motion_wrap_states test_sun_share_lane
test_hull_lightmap_gain test_shadow_replay_candidates test_shadow_replay_depth test_shadow_cascades test_taa_thin_vote
test_original_sun_share`: 10 modules, 122 tests, 0 failing. `build-ap` and `build-fl` (RelWithDebInfo, MinGW i686):
0 warnings. Not run: the GPU check of the design's section 6 (one blended draw at alpha 0.998 over an earlier
receiver through the motion-output runner: RT2 `.g` the draw's f exactly, `.a = 1`); the store-not-blend behaviour
of the owner's `.g` on a device rests on the host execution above and the Run 218 `.r` precedent, not a readback.

Flight check (open): the Run 218 stand (the remote outpost, XT `freighter_base`), F8 once in the band and once opaque.
Pass: `share_bins.py` on the band frame shows `.g` in [0, 1] with the opaque frame's bin fractions (83 % at 0, 8 % at
0.8-1.0, within a few points); hi2 % and p90 band/opaque ratios within +-5 % (Run 218: 1.18-1.24 / 1.16-1.33);
`shadow_replay_caster` rows carrying the station's vbs with `cascades=16` in the band frame;
`sun_shadow_lane_frame failed=0`. Kill: a visible shadow switch at the 950 crossing, shadow acne or a dark halo on band
stations that the opaque draw does not show, or `failed=` rising -> `owner_shadow_permille` back to 1001. Without a
capture, `fade_route_frame` shows the rule firing: `fade_receiver` and `fade_caster` near `fade_routed` while the
station is in the band (0 for the glass rows at 1000).

**Run 220 flown (2026-11-02).** User: the outpost is "much better now"; a residual transition remains that he
attributes to the alpha-tested parts, accepted as the known trade-off (not much visible). Fade-owner shadow
receiver/caster rule accepted.
