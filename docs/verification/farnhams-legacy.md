# Farnham's Legacy port ledger

One ledger for the FL port, restarted 2026-11-01 after the first attempt
(branch `fl-codex`, 580 commits, no feature accepted in game) was set aside.
Rules for this port: one feature per step, built as the `fl` edition of the
same tree ([build note](../architecture/farnhams-legacy-build.md)), installed
into `C:\X3FL`, flown by the user, then recorded here. Inherited disassembly
notes: [docs/reverse-engineering/fl/](../reverse-engineering/fl/README.md).

Target: `C:\X3FL\X3FL.exe`, SHA-256 `e7f3aad8…dfbb8`, 2,804,736 B,
PE timestamp 2021-11-04, version 1.3.0.0, ASLR-relocatable (`.reloc`,
`DYNAMIC_BASE`), Control Flow Guard metadata (`.gfids`). Stock AP copy for
comparisons: `C:\X3AP-orig`.

## FL run 0 — accepted AP DLL under X3FL.exe (2026-11-01)

Purpose: prove the proxy loads under the relocatable FL EXE and measure the
pass-through baseline before any FL code exists.

Install: `C:\X3\d3d9.dll` (release 1.7.3, `bde29ab1…`, 42,535,121 B) and the
user's `x3m.ini` (one key, `hull_lightmap_gain=2`) copied to `C:\X3FL`. No
regenerated assets, no fonts.

Result (user: game loads, save loads, TAA and HDR visibly inactive; log
`C:\X3FL\x3m.log`, session 20261101-134116, measured):

| item | value |
| --- | --- |
| frames / elapsed | 36,259 / 189.7 s, no device reset, no exception |
| device | 5120x1440 fullscreen, RTX 5070 Ti, backend system32 d3d9.dll |
| engine modules | 39 `executable_mismatch` refusals, 1 `foreign_executable`, 117 skipped |
| providers | `object_trace`, `object_lifetime`, `camera_state`, `sun_light_poll`, `scene_hook` all `active=0 status=executable_mismatch` |
| loading hooks | Crypt* hooks `installed=0`; gz buffer served without patching |
| renderer devices | `hdr_device enabled=1`, `motion_output_device enabled=1`, TAA history unavailable (requires object_trace, object_lifetime) |

Conclusion: the identity gate fails closed cleanly on FL; nothing patched, no
crash over 36k frames. The renderer's engine inputs are the port order:
camera matrices (TAA reprojection, shadow cascades), object identity (TAA
history), scene hook (bloom/compositor), sector background (fog), sun poll
(shadows). Rollback: delete `C:\X3FL\d3d9.dll` and `x3m.ini`.

## FL run 1 — first `fl` edition DLL, identity line only (installed 2026-11-01)

Candidate: commit `7c038797`, `build-fl/d3d9.dll` SHA-256 `144a023b…`, 55,307,009 B
unstripped, 0 warnings, 41 s wall ([record](../../verification/results/fl-run1/candidate.json)).
Installed to `C:\X3FL\d3d9.dll`, bytes verified; rollback
`C:\X3FL\rollback-d3d9\d3d9.dll.run0` (the run 0 AP DLL). Expected: one
`fl.identity ok base=0x… ` line in `x3m.log`, no `executable_mismatch` rows
(the AP modules are not in this DLL), otherwise run 0 behaviour.

Result (user: behaves like run 0; log session 20261101-141549, measured):
`fl.identity ok base=0x004d0000 timestamp=0x618416c8 image_size=0x00391000`;
25,176 frames / 126 s, no reset, no exception; zero `executable_mismatch` rows;
the only engine refusal left is `volumetric_fog_sector reason=foreign_executable`
from the kept sector reader. **Accepted.** The live base 0x4d0000 confirms that
the inherited preferred-base addresses must be used as RVAs.

## FL run 2 — camera_state provider (installed 2026-11-01)

Candidate: commit `8438025e`, `build-fl/d3d9.dll` SHA-256 `e589ebeb…`,
unstripped, 0 warnings ([record](../../verification/results/fl-run2/candidate.json)).
Installed to `C:\X3FL\d3d9.dll`, bytes verified; rollback
`C:\X3FL\rollback-d3d9\d3d9.dll.run1`. Design:
[camera note](../architecture/farnhams-legacy-camera.md). Expected in `x3m.log`:
`fl.camera status=active` with the two anchors matched at the live base, then
every 10 s two `fl.camera_sample` rows (`k=0` sky read, `k=1` scene read) with
`read_failure=0 shape=persp aspect=3.556`, `ortho_dot`/`ortho_len` below 1e-3,
`t_len` > 0 on `k=1`, `changed=1` while flying. Image still vanilla (no TAA
history yet).

Result (log session of 2026-11-01, 19,115 frames / 105 s, no reset, no exception;
measured): `fl.camera status=active` with both anchors matched at base 0x4d0000,
slots 0x67ae68/0x67ae78; buffers null in the menu (`read_failure=2`, one burst),
then `proj=0x04c86108 view=0x04c85d60` for the whole flight. 16 of 18 sample rows
`shape=persp m00=0.375 m11=1.3333 aspect=3.5555 m23=1 m22=1.000003 m32=-6`
(FOV_y 73.7°, near ≈ 6), `ortho_dot`/`ortho_len` ≤ 2.3e-4, `changed=1`; `k=0` (sky
read) `t_len` ≈ 11.3, `k=1` (scene read) `t_len` 112,765 → 139,542 while flying.
**Accepted:** the inherited slots and the row-major D3D layout are confirmed in game.

## FL run 3 — step 3a, FL scope hook (installed 2026-11-01)

Design: [object identity note](../architecture/farnhams-legacy-object-identity.md). Measured on the WSL2 host,
uncommitted worktree on 171e5477: clean rebuilds of both editions, 0 warnings each; the fl link has
`object_trace_fl.cpp.obj` and no `object_trace.cpp.obj`; `build-fl/d3d9.dll` SHA-256 `e1af4af2…f38888f2`.
`fl_object_trace_constants.py --check` matches X3FL.exe (0xe7142 the only direct caller of 0xe0f30, wrapper
callers 0x97035/0x97702, one relocation at the engine operand 0xa2292, eleven field operands). Native fixture
(`verification/results/fl-object-trace/run_fixture.py` → `fixture.json`): `RESULT PASS checks=328 failures=0`,
`ANCHOR flips=21 refused=21` (production gate on a synthetic image), `XMM_AFTER_VIRTUALQUERY changed=6 zero=6`;
hook 18.7 ns per entry, `current()` 67.5 ns. Dispatch-path audit (`hook_cpu_state_audit_dll.json`): 75
functions, no x87, XMM only in three save/restore envelopes. AP-address rescan (`ap_address_scan_fl.json`): 20
reachable functions, the real addresses only in `engine_effects::seta_read` and `x3m_lifetime_enter`. Schema:
`fl_trace_log` added (`schema_semantic_compare.txt`: added=1 removed=0 changed=0). Fenced for FL until step
3b: ownership buffer-write tracking (loader) and the fade-region scope (full viewport).

Expected in game: `fl.trace status=active base=0x004d0000 … write=atomic`, then every 10 s one
`fl.trace_sample` row. Correct: thousands of `entries`, `depth_max=1`, `null_node=0`, `null_camera=0`,
`camera_unreadable=0`, `field_failed=0`, `nodes` tens to `256+`, a few `cameras`, small handles, `lod` 0..3,
non-negative `model` keys, a constant nonzero `registry` (`registry_changed=1` only around a load). Wrong ABI
(descriptor/node swapped, camera one word off): `null_camera`, `camera_unreadable` or `field_failed` near the
first-sighting counts, handles 0. Wrong node offsets: `lod`/`model` pointer-like or −1 throughout. Image
still vanilla (`object_lifetime` off).

Install: commit `295cc894` (the A/B-selected implementation, see
[model routing](model-routing.md)), `build-fl/d3d9.dll` SHA-256 `66998d98…`
built from main, 0 warnings, FL host tests 21 pass
([record](../../verification/results/fl-run3/candidate.json)); `C:\X3FL\d3d9.dll`
bytes verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run2`.

Result (log session of 2026-11-01 16:07, base 0xfd0000, about 50 s of flight,
measured): `fl.trace status=active … write=atomic`; four sample rows with
`entries` 33k–156k per 10 s, `depth_max=1`, `null_node=0 null_camera=0
camera_unreadable=0 field_failed=0`, `nodes` 107–256+, `cameras` 8–13, handles
24,139–62,212, `lod` 0–3, `registry=0x06684af0 registry_changed=0`. The hook
behaves as designed. The run ended in an unhandled exception `c0000095` at
X3FL.exe RVA 0x12dbf (`idiv ecx`) on the main thread while the user switched
views with F1. Minidump analysis
([scripts](../../verification/results/fl-run3-crash/)): the engine's own
cockpit update computes the sector camera velocity as `(pos − prev) × 1000 / dt`
in 32-bit integers (helper RVA 0x12d10, caller 0x22d0a–0x22dd4, guard
|Δ| ≤ 4,999,999 and 1 ≤ dt ≤ 3000); here Δz = 4,460,464 and dt = 2 ms, so the
quotient overflowed. No d3d9.dll frame is on the crashing stack, the frame runs
before the render pass that holds the hooked call, and both FL modules only read
engine memory. **Assessment: FL engine bug, reachable whenever a view switch
moves the camera millions of units within a 1–2 ms clock tick (about 240 fps
here); not caused by the hook (confidence high).** Pending: the user's stock-FL
F1 check for confirmation. Candidate later FL feature: a guard patch for this
overflow. Hook rows accepted; the lifetime step proceeds.

## Step 3b — FL registry observer (host evidence; not installed)

Design: [FL lifetime note](../architecture/farnhams-legacy-lifetime.md). Measured on the WSL2 host, uncommitted
worktree on 3fbbef65: clean builds of both editions, 0 warnings each; the fl link has `object_lifetime_fl.cpp.obj`
and no `object_lifetime.cpp.obj`; `strings build-fl/d3d9.dll | grep -c '^fl\.lifetime'` = 2; `build-fl/d3d9.dll`
SHA-256 `c2e254d8…7f9dacc6`. AP: 119 of 121 objects identical to a pre-change build
(`verification/results/fl-object-lifetime/ap_object_compare.json`), the others `config.cpp` (one schema entry)
and `proxy_identity.cpp` (`-dirty`). `fl_object_lifetime_constants.py --check` matches X3FL.exe (42/37/33 direct
callers of insert/remove/destroy, one of the load target; every `ret` bare); `load_target_stack_scan.py`: the
load target reads no stack argument over 990 instructions. Native fixture (`run_fixture.py` → `fixture.json`):
`RESULT PASS checks=257 failures=0`, `ANCHOR flips=84 refused=84`, TIMING hook 188.5 ns per helper call,
`current()` 99.2 ns, post-load re-baseline of 20,002 entries 1,356 µs. Dispatch-path audit
(`hook_cpu_state_audit_dll.json`): 85 functions, no x87 or XMM outside the two FXSAVE/FXRSTOR envelopes.
AP-address rescan (`ap_address_scan_fl.json`): 20 functions, `x3m_lifetime_enter` gone. Schema: `fl_lifetime_log`
added (`schema_semantic_compare.txt`: added=1 removed=0 changed=0). FL host tests 28 pass.

Expected in game, correct: `fl.lifetime status=active_without_baseline … engine=0x00000000 registry=0x00000000
baseline=no_engine baseline_entries=0 capacity=262144 write=atomic,atomic,atomic,plain failed_site=none`;
`motion_output_device … history_available=1`; `fl.lifetime_sample` rows with `dormant>0` only before the engine
exists, then `binds=1`, `inserts` in the tens of thousands during a load, `loads=1` per save load with
`rebaselined` ≈ the entries that survived it, `rebaseline_rejected=0 rebaseline_skipped=0`, `rebaseline_us_max`
a few ms, `published` ≈ `inserts`, `unpublished_nested≈0`, `unpublished_lookup=0`, `unwinds=0`, `live` tens of
thousands, one constant `registry`, `registry_changed=0 rebind_without_destroy=0 registry_dead=0`; in flight
`current` near the `fl.trace_sample` entries and `known` ≥ 95 % of it (AP iteration05: 98 %).
`motion_output_frame` `routed`, `matched` and `history_current` > 0 in flight; `sun_shadow_lane_refusals`
`history=` near 0. Image: anti-aliased edges that hold still, no ghost trails behind moving ships or on view
switches. Wrong: a refusal status names the gate (`*_entry_mismatch`, `engine_anchor_mismatch`, `failed_site=`);
`top_reason=unknown_camera, known≈0` after `loads=1` (survivors not re-adopted: `rebaseline_rejected` or
`rebaseline_skipped` > 0); ABI or key mix-up gives `published≈0`, `unpublished_lookup≈inserts`,
`top_reason=unknown_node`; a wrong registry gives `other_maps≈all, inserts=0`; a stuck scope gives `in_flight>0`
in every row and `top_reason=mutation_in_progress`; table loss gives `object_lifetime_disabled reason=…`;
history keyed wrongly shows as smearing or ghosting on moving objects.

## Scale overflow fix, host evidence (2026-11-01, not installed)

Design: [scale overflow note](../architecture/farnhams-legacy-scale-overflow.md). Measured on the WSL2 host,
uncommitted worktree on 3fbbef65: clean fl and ap builds, 0 warnings each; the fl link has
`scale_overflow_fix_fl.cpp.obj`, ap does not; `build-fl/d3d9.dll` SHA-256 `e2677f13…ecb6e04b`.
`fl_scale_overflow_constants.py --check` matches X3FL.exe (helper 0x12d10, 0xbc bytes, eleven direct callers, no
relocation in or pointing into the body, no interior branch). Native fixture
(`verification/results/fl-scale-overflow/run_fixture.py` → `fixture.json`): `RESULT PASS checks=121 failures=0`;
gate flips 188/188 refused; the original bytes trap on the FL run 3 vector with c0000095 at offset 0xaf (RVA
0x12dbf); the replacement returns (683350000, −346272000, 2147483647); 20,000-case sweep (13,744 fit, 6,256
clamped) with 0 mismatches against the model and, where it fits, the original (out, EAX, ECX, EDX, CPU state);
six alias layouts equal to the original; per call 3.7 ns original, 6.7 ns replacement (fixture, not game).
CPU-state audit (`cpu_state_audit_dll.json`): no x87/XMM/libgcc on the per-call path. Schema: two keys added, none
changed (`schema_semantic_compare.txt`); AP-address rescan unchanged (20 reachable functions). Expected in game:
`fl.scale_fix status=active … write=atomic`, one `fl.scale_fix_sample … saturated=0` after the first 10 s, then a
row only when a clamp happened (`last_site=0x22dd4` for the F1 camera crash path). Rebased onto 04d463ac (step
3b); the rebased build's hash and test counts are in the commit's report.

## FL run 4 — lifetime observer + scaler overflow fix (installed 2026-11-01)

Candidate: commit `22507889` (steps 3b `04d463ac` and the scale fix), `build-fl/d3d9.dll`
SHA-256 `2dc5c82b…`, 0 warnings, FL host tests 35 pass
([record](../../verification/results/fl-run4/candidate.json)); `C:\X3FL\d3d9.dll` bytes
verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run3`. First FL build with TAA
history available. Expected: `fl.lifetime status=active_without_baseline …
failed_site=none`, `motion_output_device … history_available=1`, `fl.lifetime_sample`
with `published≈inserts`, `binds=1`, `known ≥ 95 %` of `current`, `rebaselined>0`
after a save load; `fl.scale_fix status=active`, a `fl.scale_fix_sample` row at the
first period and again only when `saturated` grows (F1 view switches no longer crash).
Image: TAA on for programs the AP tables know; **FL hull programs (34 new ps_3_0,
18 seen in run 3) have no motion rows yet, so moving ships may ghost or smear**;
hull lightmap gain and widening stay off until the FL shader coverage step
([shader finding](#fl-shader-coverage-finding-2026-11-01)).

Result (log session of 2026-11-01, base 0x230000, 48,681 frames / 249 s, no reset,
no exception; measured): `fl.lifetime status=active_without_baseline …
write=atomic,atomic,atomic,plain`, `motion_output_device … history_available=1`
(first time under FL). 23 lifetime rows: first period `inserts=54544
published=54544 loads=1 binds=1 live=5999 rebaseline_us_max=1424`, afterwards
`known` = 100 % of `current` on every row (e.g. 5046/5046, 4800/4800),
`unpublished_nested=0 unknown=0 rebind_without_destroy=0 in_flight=0`, live
objects 6k–9k, `max_probe=1`. `fl.scale_fix status=active`, one sample row
`calls=6144 saturated=0` (about 600 calls/s; no overflow occurred this flight, so
the fix is install-proven, not yet exercise-proven). Shadow-lane refusals are all
`pair` (82 untracked at the last row), i.e. the FL hull programs without motion
rows, as predicted. **Accepted on the log side: the lifetime observer and TAA
history are live under FL.** Visual report from the user: stations more or less fine; a few wheat farms shimmered at distance in the last sector (specular shimmer or missing hull motion rows; to recheck after the shader step); small ships showed no visible issue, big ships not checked. **Run 4 accepted.**

## FL shader coverage finding (2026-11-01)

From the run 3 log and the inherited notes (`fl/shaders.md`, `fl/materials.md`):
all 256 vertex programs are byte-identical to AP; FL replaces 34 ps_3_0 hull
programs (22 conventional, 12 XT) and drops 30. Run 3 created 55 distinct programs,
21 vs all known, 18 of 34 ps unknown (all FL hulls). Exact-hash consumers that stay
off for them: TAA motion pair (jitter applied, pair refused → camera-only
reprojection), hull lightmap gain and widening (built on the motion variant), HDR
additive hull gain (12 AP rows, all replaced), exposure/strength twins, pixel
coverage, radiance, distance fade. Unaffected: fog, bloom, emission, lens, fade VS,
position programs, sun register (by constant name). Porting needs: motion rows for
the 34 programs, the transformer's register layout read from each program's constant
table (FL: glow c4, hue c1–c3, XT c7/c9, c12), variant limit above 1911 DWORDs,
native validator rerun, `addon2` added to the archive scans. Order: motion rows
(enables gain and widening), then HDR hull gain and exposure rows.

## FL shader coverage 4a — archive inventory and motion rows (host evidence, 2026-11-01, not installed)

Offline only: no game launch, no Wine. Design and rule:
[FL shader tables](../architecture/farnhams-legacy-shaders.md). Results and their
producers are in `verification/results/fl-shader-coverage/`.

- **Mount order.** `tools/analysis/catalogue_mounts.py` (root, then `addon` for AP
  or `addon2` for FL, first gap ends a series) serves the four archive scans. Each
  scan takes `--edition auto|ap|fl`. Test: `test_catalogue_mounts` (5 tests).
  `ap_census_regression.py` → `ap-census-regression.json` runs the pre-change tool
  tree (`c87d81b8`) and the current one over `/mnt/c/X3AP-orig`. With the same
  registry, all 14 keys apart from the tool hash are identical. With the compiled
  table, only `registry` differs: 171 → 205 rows, 34 absent from the AP archive.
  The pair count is 817.
- **Inventory** (`inventory.py` → `summary.json`; the per-program `inventory.json`
  is local and git-ignored). FL has 755
  programs and AP 751: 721 shared, 34 FL-only, 30 AP-only, all ps_3_0. All 256 VS
  are shared. The 34 split into 22 conventional and 12 XT, 1,370–1,911 DWORDs; 30
  exceed 1,392 (the note's 18/22 conventional plus all 12 XT) and 8 exceed 1,791.
  This confirms the inherited 721/34/30. All 57 dumps and every logged program are
  inventory members. Runs 3 and 4 logged 18 and 20 FL-only programs; the 18 are a
  subset of the 34.
- **Rows.** `motion_output_profiles_fl_inc.h` comes from
  `inspect_motion_output_profiles.py --base-profiles`; the exact commands are in
  `regenerate.sh`, and rerunning it is byte-identical except for the generator
  hash. The generator refuses before writing anything if an AP row would move. It
  adds 34 rows (6 A, 16 B,
  12 C) on the four AP VS `37c34a74`, `4944d81d`, `494fe349`, `53a0a641`. The
  generator accepted all 34 unchanged: none needed a size or class change. The
  depth plan over the union leaves all 171 AP rows unchanged, and every row writes
  depth. No VS or jitter row was missing: the 29 jitter gaps of `fl/shaders.md` are
  AP-identical SM1/SM2 programs and the 9 bloom pairs. The compiled table has 205
  rows (AP +34 inert). FL census (`census.py`): 175 registered pairs, previously 141.
- **Proof.** `run_material_motion_structure.py` over the AP and FL corpora, release
  and ASan/UBSan: 205/205 rows transformed. The 34 FL rows pass 340 checks, 139,264
  sampled bit mutations and 1,222 program perturbations.
  `test_motion_output_profiles` has 44 tests, including the FL extension's
  byte-identical regeneration and a refusal when a base row would move.
  `validate_variants.py` ran the native SysWOW64 validator over 110 FL variants (34
  PS × motion, motion-depth and sun-lane; 4 VS × 2) and 38 originals: all S_OK with
  0 messages. A two-constant negative control is refused with `80004005`. The fill,
  gain and share variants return `UnsupportedShader` for all 34 FL PS (part 4b).
  Both editions build with 0 warnings. The capture paths in
  `rerun_round3.sh`/`twin_on_captures.py` now take `--captures`;
  `twin_on_captures.json` regenerates byte-identically through it. The full host
  suite ran 312 modules and 3,296 tests in 142 s. `test_motion_wrap_states` read FL
  rows from the AP corpus only; it now also reads the FL corpus and passes, with or
  without that corpus. The two remaining failures are the known
  `test_engine_ribbons`/`test_engine_effects` source-binding records, which this
  change does not touch. The D3D fixture runner `run_material_motion.py` parses both
  includes, but it was not run (it needs a D3D9 fixture host).
- **Expected in the next FL flight once installed.** `gate3-baseline.json`: in runs
  3 and 4 every `sun_shadow_lane_refusals` sample is pair-only (`pair` = `untracked`,
  up to 856). All 27 `sun_shadow_lane_writer … reason=pair gate=3` rows are FL rows.
  Next flight: `motion_output_variant kind=ps original=<FL id> transform=0
  create=00000000 depth=1` rows appear, and `pair`/`untracked` falls to about 0 on
  hull scenes. Moving FL ships get per-object motion, so they no longer ghost or
  smear. Hull light-map gain and widening stay off until 4b. Result: pending.

## FL run 5 — shader coverage 4a, hull motion rows (installed 2026-11-01)

Candidate: commit `400def01`, `build-fl/d3d9.dll` SHA-256 `41d2cff0…`, 0 warnings,
FL host tests pass ([record](../../verification/results/fl-run5/candidate.json));
`C:\X3FL\d3d9.dll` bytes verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run4`.
Expected: `motion_output_variant kind=ps … create=00000000` rows for the FL hull
programs, `sun_shadow_lane_refusals pair=` near 0 (run 4: 82 at the last row), no
hull shimmer on distant stations, no smear on large moving ships. Hull lightmap
gain and widening still off (part 4b).

Result (log session of 2026-11-01, 63,717 frames / 355 s, no reset, no exception;
measured): 24 `motion_output_variant kind=ps` rows created (run 4: 1), all
`create=00000000 depth=1`; no `sun_shadow_lane_refusals` row at all (run 4: 82
pair refusals); lifetime `known` 475,919 / 475,919; `hull_lightmap_variant
gain_applied=0` (4b pending). User: stations fine, ships including big ships fine;
distant sun-lit wheat farms still shimmer. **Accepted.** The shimmer is no longer
a missing-motion-row symptom; candidates are specular on the FL hull programs
without the sun-share/LOD-strength exposure twins (4b) or plain specular aliasing;
F8 captures of a shimmering farm are requested for the 4b build.

## FL shader coverage 4b — hull material rows (host evidence, 2026-11-01, not installed)

Offline only: no game launch, no Wine. The base is `531d0f70`. Design and what stays
off: [FL shader tables](../architecture/farnhams-legacy-shaders.md) "Material rows".
Results and their producers are in `verification/results/fl-shader-coverage/`.

- **Rows.** `tools/analysis/inspect_fl_linear_materials.py` runs as the last step of
  `regenerate.sh`, takes 0.2 s and writes `linear-materials-fl.json`. Each value comes
  from the program's instruction stream and CTAB register names: texture sites by
  sampler, the COLOR0 clamp, `oC0`, the glow LRP, three consecutive `dp4` affine rows,
  light/palette uses by CTAB name and RGB sites by branch-aware taint. The diffuse join
  is the MAD with both light colours on both sides and the specular sample on one. The
  temporary base and depth texcoord come from the 4a motion row. Output: 22 `Pixel`
  rows, 22 pairs, 12 XT rows with FL roles, 34 joins (30 product, the 4 Terran
  addend) and 12 HDR hull rows. The hull rule (an effect basename
  `standard_lighting`/`xt_standard_lighting`, optionally `2s`) reproduces the 12 AP rows.
  The generator refuses any FL hash already in an AP table (tested) and writes only
  `*_fl_inc.h`.
- **Independent cross-checks.** `crosscheck_codex.py` → `codex-crosscheck.json`
  compares field by field with the rows of the archived fl-codex commit `636258da`
  (a separate DAG analyzer): 22/12/34/12 rows, all equal, same hull set. The joins and
  light-map sites also equal the J/L columns of `fl/materials.md`.
  `inspect_xt_materials.py --fl-report` (new FL mode with CTAB roles; AP output
  byte-identical) → `xt-materials-fl.json`: 12 XT rows, 318 RGB sites, 0 mismatches.
- **Transformer generalisations** (data, AP defaults unchanged): `Pixel.glow` (c4),
  `affine_first` (c1) in `pixel_sites`; `Pixel.temporary_base` before the family rule;
  XT roles `light0/light1/palette_first/temporary_base` in `xt_pixel_sites`, the fill
  light and the share seeds. `original_only` makes the converted `transform`/
  `xt_transform` and the XT-default repair refuse FL rows. The input bound becomes
  `original_pixel_max_words = 1911`, `static_assert`-ed equal to the largest row. The
  output bound stays the final structure walk against the device slot budget.
  `linear_emission_hull_program_count` goes 12 → 24.
- **Proof.** `test_fl_linear_materials` (20 tests, 32 s) covers report and wiring,
  byte-identical regeneration and AP-collision refusal. It reruns the AP light-map
  gain and widening oracles over the FL rows, subclassed with FL counts; the widening
  bytes are rebuilt in Python, 204 checks. It runs the fill (34/34, 3,153 checks),
  share (34/34, 3,558 checks; the reported light is the row's c6/c7), strength (510
  twins, none refused), engine-light (272 twins) and HDR hull-gain (12 programs, 48
  variants, 1,684 checks) drivers. The gain/widen driver: 34/34, 9,493 checks, at
  most 274/311 slots. `validate_variants.py`: 1,256 FL variants and 38 originals S_OK
  with 0 messages. They are 110 from 4a plus 7 forms × with/without engine light, a
  strength twin of each (952), static gain (68), depth-off (102) and HDR hull gain
  (24). The `c216, c217` negative control is refused `80004005`. The largest variant
  is 5,000 DWORDs and 883 slots (`validator.json` `largest_variant_words/_slots`,
  `ps3_program_slots` over every saved variant).
- **AP unchanged.** `git diff` is empty on every generated AP table include (the changed
  `linear_xt_material_inc.h` is transformer code). `ap_identity.py`
  builds a digest probe of every touched public API against `531d0f70` and the tree:
  751/751 AP programs and 180 AP pairs identical. On the FL corpus exactly the 34
  FL-only programs change. The AP gain/widening oracles pass (17 tests) after the
  expectation refactor. The host suite ran 313 modules and 3,317 tests in 120 s. Only
  the known `test_engine_effects`/`test_engine_ribbons` source-binding records fail;
  none of their sources changed. `test_shader_population` now expects 176 pairs and
  the two FL tables. AP and FL release builds: 0 warnings. `lookup_bench.py` →
  `lookup-bench.json` (x86-64 host, best of 5): the pair-contract plus hull-program
  lookup at the shader setter goes 43.7 → 62.7 ns for an unknown pair and 6.0 → 11.0 ns
  for an AP pair. `summarize_hull_emitters.py` reads the hull program list from
  `linear_emission.cpp` and its FL include, so F8 tables keep `program=12–23`.
- **Not applicable.** Pixel coverage (capture reader only; open issue), radiance (no
  production consumer), cutout (converted path only).
- **Expected in the next FL flight once installed** (launcher defaults: fill 0.01, gain
  4 with far fade, widening 4,4, lane on). The 34 FL programs log `original_fill_variant
  … transform=0 … fill_applied=1`, `hull_lightmap_variant … transform=0
  create=00000000 … gain_applied=1` and `hull_lightmap_widen_variant … transform=0
  create=00000000 … stage=2|3 widen_applied=1`. They also log
  `sun_shadow_original_lightmap[_widen]_variant … share_applied=1` and
  `engine_light_variant … created=7f`. Before this change none of these rows
  existed for FL programs. Visible: lit windows and hull lights on FL ships and
  stations at the AP brightness, and widened thin emissive strips at distance. The
  HDR hull gain (`hull_emission_variant … program=12–23`) appears only with an
  emitter gain above 1. Result: pending.

Per-program acceptance, from `linear-materials-fl.json`: every program is accepted in
every applicable family (row, pair or XT pair, join, all original-shading forms and
twins, plus HDR hull gain where "yes"). Row: D/B/L = DEFAULT/BUMPMAP/BUMPMAP_LOW.
r-base: temporary base. LM: light-map stage @ fetch DWORD. Join: diffuse-join DWORD
and form.

| FL PS | Row | Words | r-base | LM | Join | HDR hull |
| --- | --- | ---: | ---: | --- | --- | --- |
| `0d5848c8489b6630` | Pixel D | 1450 | 6 | s2 @1432 | 1387 prod | yes |
| `1455079b3f18aa88` | Pixel D | 1412 | 5 | s2 @1394 | 1312 prod | n/a |
| `16d4af171035acd1` | Pixel B | 1448 | 7 | s3 @1430 | 1331 prod | n/a |
| `1835f5e1ba07b1aa` | Pixel D | 1386 | 5 | s2 @1368 | 1286 prod | n/a |
| `1886829a571cf2db` | Pixel D | 1376 | 5 | s2 @1358 | 1276 add | n/a |
| `1b54cbfa2155b61c` | Pixel B | 1486 | 7 | s3 @1468 | 1365 prod | yes |
| `20c3851a4cac4213` | Pixel B | 1452 | 7 | s3 @1434 | 1331 prod | n/a |
| `2a7f1c93d4fd081c` | Pixel B | 1476 | 7 | s3 @1458 | 1359 add | n/a |
| `33826e49b53b88dc` | Pixel D | 1390 | 6 | s2 @1372 | 1327 prod | n/a |
| `3c39d45636d95744` | Pixel L | 1475 | 7 | s3 @1457 | 1354 prod | yes |
| `3ff64d33ff2782ab` | Pixel B | 1432 | 7 | s3 @1414 | 1315 prod | n/a |
| `5831fb1d488e089e` | Pixel D | 1408 | 5 | s2 @1390 | 1308 add | n/a |
| `5fb88b63dd609443` | Pixel D | 1416 | 6 | s2 @1398 | 1353 prod | n/a |
| `67f738c3b080164a` | Pixel B | 1484 | 7 | s3 @1466 | 1363 prod | n/a |
| `6f745daaf928a297` | XT L | 1900 | 6 | s3 @1881 | 1771 prod | yes |
| `740346590fe7bff5` | Pixel B | 1518 | 7 | s3 @1500 | 1397 prod | yes |
| `7d11d532dedaf1d9` | XT terra B | 1804 | 7 | s3 @1781 | 1602 prod | n/a |
| `87ccd863f19254b2` | XT terra D | 1681 | 6 | s2 @1658 | 1544 prod | n/a |
| `96238a8b2cee4f6e` | Pixel B | 1464 | 7 | s3 @1446 | 1347 prod | n/a |
| `9db5eebae86ce142` | XT terra L | 1793 | 7 | s3 @1770 | 1591 prod | n/a |
| `ac429ac9cd658e64` | Pixel B | 1444 | 7 | s3 @1426 | 1327 add | n/a |
| `b2d8f40cc7a03904` | XT B | 1879 | 6 | s3 @1860 | 1750 prod | yes |
| `b5af46aebb231ba2` | Pixel D | 1370 | 5 | s2 @1352 | 1270 prod | n/a |
| `ba08f36b160527a5` | XT L | 1868 | 6 | s3 @1849 | 1739 prod | yes |
| `c17114ca9752ab62` | XT D | 1756 | 6 | s2 @1737 | 1627 prod | yes |
| `c79c3b2c6749df10` | Pixel B | 1480 | 7 | s3 @1462 | 1363 prod | n/a |
| `cca466f41a0de457` | XT terra D | 1713 | 7 | s2 @1690 | 1568 prod | n/a |
| `ced7edef48059e84` | Pixel D | 1418 | 6 | s2 @1400 | 1355 prod | yes |
| `db375f72fe3cdf5e` | Pixel D | 1396 | 5 | s2 @1378 | 1296 prod | n/a |
| `df146f0ed69e1acf` | XT terra L | 1825 | 7 | s3 @1802 | 1627 prod | n/a |
| `e326b2b1363c9886` | Pixel L | 1507 | 7 | s3 @1489 | 1386 prod | yes |
| `e57c48fc06198e20` | XT B | 1911 | 6 | s3 @1892 | 1782 prod | yes |
| `f06b626708db3c7e` | XT D | 1788 | 6 | s2 @1769 | 1659 prod | yes |
| `f083a2be99470f83` | XT terra B | 1836 | 7 | s3 @1813 | 1638 prod | n/a |

## FL run 6 — shader coverage 4b, hull gain/widening/exposure rows (installed 2026-11-01)

Candidate: commit `879f3e8c`, `build-fl/d3d9.dll` SHA-256 `e91ee801…`, 0 warnings, FL host
tests pass ([record](../../verification/results/fl-run6/candidate.json)); `C:\X3FL\d3d9.dll`
bytes verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run5`. `debug = 1` set in
`C:\X3FL\x3m.ini` for F8 captures. Expected rows for FL program ids:
`hull_lightmap_variant … gain_applied=1`, `hull_lightmap_widen_variant … widen_applied=1`,
`original_fill_variant … fill_applied=1`, `sun_shadow_original_lightmap[_widen]_variant …
share_applied=1`, `engine_light_variant … created=7f`. Image: hull windows and lights at AP
brightness, widening on emissive strips at distance; recheck the distant sun-lit wheat
farms (shimmer) with F8 captures.

Result (log session of 2026-11-01 19:0x, 30,422 frames / 216 s, no exception;
measured): `hull_lightmap_variant` 24 rows (23 `gain_applied=1`, all
`fill_applied=1`), `hull_lightmap_widen_variant` 23 `widen_applied=1`,
`original_fill_variant` 24, `sun_shadow_original_lightmap_variant` 24
`share_applied=1`, `hull_emission_variant` 15; `engine_light_variant` 0 rows (to
explain). User: stations and ships fine; distant sun-lit wheat farms shimmer less
than before but still a little; three F8 captures taken (far, mid, close; shimmer
in the far two). **Accepted.** Shimmer triage from the captures:
`verification/results/fl-run6-shimmer/` (`summary.json`, measured on the three
captures): the farms are models 556e/556f/5570 drawn alpha-tested (`ALPHAREF=1`,
cull none, up to 18.8k primitives per draw) with ps `c79c3b2c…`/`16d4af17…`, all
variants applied, LOD 2 at 130–230k view units; pre-TAA frame-to-frame change
23–26 display codes against 6–7 on a station and 1–2 on the own hull; surface
switching on 56–70 % of farm pixels (stations 3–32 %); sun-lit pixels flicker more
(35–40) but are only 5–15 % of the farm. A CPU model of the resolve puts the luma
lock on 34–52 % of farm pixels, output ≈ 1.8–2.0 codes; full 0.985 hold ≈ 0.34.
**Cause: alpha-tested sub-pixel coverage aliasing in the shared TAA stage, not an
FL program difference and not mainly specular**; the AP ledger
`taa-seta-shimmer-run179` records the same class on distant AP hulls. Proposal
(not implemented): far-eligible alpha-tested draws with the camera gate open take
the thin-region hold (0.97, 7x7) through the existing thin-vote lane. Decisive
diagnostic for the next flight: `taa_debug = 1` and one F8 at a distant farm in
normal flight (lock/mask/age readbacks).

## Step 5 — scene hook, host evidence (2026-11-01)

Design: [FL scene note](../architecture/farnhams-legacy-scene.md). Measured on the WSL2 host, the step-5 commit rebased onto
5fec7535, fresh worktree builds: fl and ap 0 warnings each; the fl link has `scene_hook_fl.cpp.obj` and `compositor_owner_fl.cpp.obj`
and neither ap object; `strings | grep -c '^fl\.scene'` = 2 (install hash: `verification/results/fl-run7/candidate.json`); no
`0x208b3c`/`0x206f34` (AP owner/glow RVAs) in the fl disassembly, 2 in ap's. AP: 119 of 121 objects identical to a fresh
build of main 5fec7535 (`verification/results/fl-scene-hook/ap_object_compare.json`; `config.cpp` one schema key,
`proxy_identity.cpp` the source-commit string). `fl_scene_hook_constants.py --check` matches X3FL.exe (`constants.json`: window and
call as documented, the compositor's only E8 caller `0x8b929`, complete pass caller `0x782cf`, compositor 553
instructions, RETs `0xe6887/0xe69b3/0xe6e86` all `c3`, no stack-argument operand, branches into the window only to
`0x8b932`). Native fixture (`run_fixture.py` → `fixture.json`): `RESULT PASS checks=304 failures=0`, `ANCHOR
flips=61 refused=61`, TIMING per compositor call: signal path +94 ns, bridge path +195 ns (fixture, once per frame;
not game FPS). Mutations (`mutation_check.py` → `mutation_check.json`): without the XMM save 4 failures, without
`PreserveCpuState` 31, unmutated PASS (source restored byte for byte).
Audit (`hook_cpu_state_audit_dll.json`): 2 functions, no x87/XMM outside one FNSAVE/FNINIT/FRSTOR and one XMM
envelope. AP-address rescan of the fresh fl build (`ap_address_scan_fl.json`): the same 20 reachable functions as step 3b. Schema:
`fl_scene_log` added (`schema_semantic_compare.txt`: added=1 removed=0 changed=0). FL host tests (7 `test_fl_*` modules) 62 pass.

Expected in game with the user's route (bloom + HDR AgX): `fl.scene status=active base=0x… site=base+0x8b929
target=base+0xe6770 path=bridge bridge_requested=1 renderer=0x00000000 write=atomic` (installed before the engine
builds its renderer; `path=signal` without bloom/HDR AgX). Every 10 s `fl.scene_sample`: `entries` ≈ `frames` in
flight, `frames_without_call` ≈ 0 in flight (menus, loading screens and the map may count), `frames_multi_call=0`,
`nested=0` (bridge entries only; the signal path cannot see nesting), `compositor_active=1`, `owner_reads` > 0 with `owner_failed=0`, `glow=1` when the game's glow option is
on, one constant nonzero `renderer`. Capture rows: `bloom_attach … result=00000000`, `bloom_prepare … ready=1`,
`bloom_commit … committed=1`, no `bloom_refusal reason=caller|owner|device|glow`; `motion_output_device … scene_hook=1`;
`motion_output_frame … scene_end_source=hook scene_end_check=1` (agree: the compositor's own copy follows; `2` with glow off);
`hdr_frame … end=hook`. Wrong: a `*_mismatch` status names the gate; `owner_failed` ≈ `owner_reads` or
`bloom_refusal reason=owner` (owner chain); `glow=0` with glow on or `reason=glow` (glow byte); `frames_without_call`
≈ `frames` in flight (the boundary never reached: no camera above layer 17 in FL's normal view); a
`motion_output_scene_hook_disagreement` row per frame. Image: bloom halo and AgX tonemap as on AP, TAA unchanged.
Assumption (bridge on by default for this flight): the bridge replaces the native glow with the AP bloom commit
without an FL shader-profile or `bloom.fb` check; high shader quality with stock `bloom.fb` is the flown case,
medium/low quality or a modified `bloom.fb` is unverified; the runtime guard is `validate_boundary` (`reason=boundary`,
no draw).

## FL run 7 — step 5 scene hook, bloom bridge and compositor owner (installed 2026-11-01)

Candidate: commit `c931a86c`, `build-fl/d3d9.dll` SHA-256 `c91ce71c…` (55,437,053 B), 0 warnings,
FL host tests 62 pass ([record](../../verification/results/fl-run7/candidate.json));
`C:\X3FL\d3d9.dll` bytes verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run6`
(`e91ee801…`). `debug = 1` and `taa_debug = 1` stay set in `C:\X3FL\x3m.ini`. Expected
rows and image: the Step 5 section above. Also open from run 6: the readback captures
(frames 13630–13637 and 13940–13947, taken on run 6 with `taa_debug = 1`) are under
triage in `verification/results/fl-run6-shimmer/readback/`.

## Run 6 readback captures — farm shimmer hold state (2026-11-01)

Two F8 captures on run 6 with `taa_debug = 1` (frames 13630–13637 and 13940–13947,
distant farm selected, measured; scripts and `summary.json` in
`verification/results/fl-run6-shimmer/readback/`). Lane decoding from
`src/temporal/resolve.hlsl`. Farm alpha-tested pixels: luma lock on 41–54 %, thin
region on 9–61 % (85–100 % on the 556f farm, 6–16 % on the two 556e farms), age median
64, none at age ≤ 1. TAA output change per frame (motion-compensated, jitter-corrected
display codes): held farm pixels 1.3–1.6, unheld 3.3–3.4 (the method floor is 1.1–1.2),
whole alpha-tested farm 1.9–2.4 against the station's 1.4; pre-TAA 28–34. The unheld
pixels are 27–36 % of the farm and carry about half its residual. **Conclusion: the hold
removes the flicker where it applies; the residual is on the farm pixels the lock and the
thin region miss, so the approved rule (far alpha-tested draws take the thin hold) is
supported** (bringing unheld pixels to the held figure puts the farm at ≈ 1.4–1.7 codes,
inferred). Limits: the capture frames after the first run at dt 200 ms (8x normal
motion), so the residual at normal-flight motion is not measured; the two LOD-3 farms
(ps `1835f5e1…`, no alpha test) are unheld on 30–37 % of pixels with the highest residual
(3.0–3.6) and the alpha-tested rule does not cover them; pixel-to-draw assignment is by
node box, not draw id.

## TAA far alpha-tested thin hold, host evidence (2026-11-01, not installed)

Shared TAA stage, both editions. An alpha-tested routed draw (not the fade or overlay arm) writes RT2 `.a = 0`
(thin = 1) through the existing `c218.x` thin-vote upload when its origin's view depth reaches the far gate's F0
depth (`F0 * m00 * W / 2`, the depth where `farw` leaves 0), so the camera-gate resolve gives its pixels the 0.97 /
7x7 thin-region hold where the camera gate is open; the far depth is set once per frame at the scene camera read and
only with the vote, the camera-gate resolve, the thin region and a far weight on. Setting `X3M_TAA_THIN_ALPHA_FAR` /
`taa_thin_alpha_far` (Bool, builtin 1, developer key; 0 switches it off); `thin_vote_frame alpha_far=` counts the
rows that voted (design and limits:
[taa-thin-geometry-alternatives.md](../architecture/taa-thin-geometry-alternatives.md) section 3.2, "Alpha-tested far
rows"). Host evidence (measured): `test_taa_thin_vote` 18 OK (two new cases: the per-draw predicate at D 130000 /
76800 votes, 5000 / behind / NaN / setting 0 do not, an opaque far panel keeps its histogram verdict; the draw site),
`tools/config/generate.py --check` clean through `test_config_schema`, `build-fl` and `build-ap` 0 warnings;
`check_no_x87.py` reports the same 180 functions as `main` on AP (no new x87 on the draw path). **Kill criterion:** a
ghosting trail behind a moving alpha-tested ship, to be checked in both editions. Open: the LOD-3 farms (ps
`1835f5e1…`, no alpha test, highest residual 3.0–3.6) are not covered; the test is per draw origin, so a large
alpha-tested draw with a far origin also votes its near pixels; the residual at normal-flight motion is unmeasured;
the Wine motion-output fixture was not rerun.

## FL run 7 result — scene hook accepted, bloom refused on every frame (2026-11-01)

Log of 2026-11-01 19:4x, 35,196 frames / 224 s, no exception (measured): `fl.scene
status=active path=bridge`, `fl.scene_sample … frames_without_call=0 frames_multi_call=0
owner_failed=0 glow=1`, `scene_end_source=hook` on 34,211 frames (`none` on 985 menu
frames), `hdr_frame … redirected=1 end=hook writeback_source=shader`, TAA `committed=1`
in flight. 15 `motion_output_scene_hook_disagreement` rows on frames 5–19 (loading, before
the first scene). **Hook accepted.** Bloom: `bloom_attach … reason=ok`, but all 122
`bloom_prepare` rows `ready=0 reason=invalid operation=80070057` and no `bloom_commit`;
admission counters show the boundary admitted (`scene_handoff=873 glow_off=15`, others 0).
Triage (`verification/results/fl-run7/bloom_prepare_history.sh`, measured): every
native-Windows AP flight log in the repo (25 logs, 2026-10-13 to 10-23, 4,984 rows) fails
the same way; the last `bloom_commit` rows are from Wine run 83 (2026-09-25). **Not
FL-specific: a shared bloom regression between 2026-09-26 and 10-13** (bolts through TAA,
HDR meter rework, or the native-Windows move; not separable from logs). `validate_inputs`
(`src/renderer/bloom_pass.cpp:548-610`) returns a bare E_INVALIDARG; a diagnostic row
naming the clause and the descriptors is being added for the next build.

## FL run 8 — far alpha-tested thin hold + bloom diagnostic row (installed 2026-11-01)

Candidate: commit `b156415a` (thin hold `6cdaf384`, reviewed Opus high, docs-only
findings; bloom diagnostic `b156415a`, delta reviewed by the main session: 27 numbered checks,
conditions and order unchanged), `build-fl/d3d9.dll` SHA-256 `22b67304…`, RelWithDebInfo, 0
warnings, host tests 37 + 69 OK ([record](../../verification/results/fl-run8/candidate.json));
`C:\X3FL\d3d9.dll` bytes verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run7` (`c91ce71c…`).
Expected: `thin_vote_mode … alpha_far=1`, `thin_vote_frame … alpha_far=N` with N > 0 near farms;
distant farm shimmer reduced to the station level; no trail behind slow-moving distant
alpha-tested ships (kill criterion, both editions). First 8 `bloom_prepare` failure rows carry
`clause=N` and the main/scene descriptors, which names the refusing check of
`validate_inputs` (7 = main descriptor, 18 = scene descriptor, 24–27 = device identity).

Result (log of 2026-11-01 20:1x, 26,600 frames / 199 s, no exception; measured):
`thin_vote_mode … alpha_far=1`; `thin_vote_frame … alpha_far>0` on 22,713 of 26,600 frames.
User: distant farm shimmer gone, no trails or smearing on ships. **Accepted.** Bloom
diagnostic: 8 rows `clause=7 main_fmt=15 main_type=3 main_usage=800001 main_pool=0 main_ms=0
main_w=5120 main_h=1440 scene_fmt=71 … thread_ok=1`: `main_descriptor()` requires
`Type == D3DRTYPE_SURFACE`, native D3D9 reports the texture-level render target as
`D3DRTYPE_TEXTURE` with usage bit `0x800000` set, Wine reported a plain surface. **Cause of the
shared bloom regression since the native-Windows move (2026-10-11); fix in progress.**

## FL run 9 — bloom fix, native texture-level main target (installed 2026-11-01)

Candidate: commit `bad55d33` (fix reviewed by the main session: `main_descriptor` accepts
`D3DRTYPE_SURFACE` or `D3DRTYPE_TEXTURE`, all other conditions unchanged, pinned by static
asserts; `test_bloom_main_target.py` covers the native and Wine descriptors and 7 refusals),
`build-fl/d3d9.dll` SHA-256 `c7ceeb02…` (55,452,232 B), 0 warnings, `test_bloom*` 71 OK
([record](../../verification/results/fl-run9/candidate.json)); `C:\X3FL\d3d9.dll` bytes
verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run8` (`22b67304…`). Expected:
`bloom_prepare … ready=1` (no `clause=7`), `bloom_commit … committed=1`, no
`bloom_refusal reason=boundary`; image: bloom halo around lights as AP had under Wine. The
same commit is release 1.8.0 for AP ([releases](releases.md)).

Result (log of 2026-11-01 20:4x, 35,987 frames / 236 s, no exception; measured): 117
`bloom_prepare … ready=1 reason=ok`, 117 `bloom_commit … committed=1 reason=ok`, no
`clause=` rows, `bloom_refusal` only `glow_off` and `scene_handoff` once each (menus),
admission `boundary=0`. **Bloom draws on native D3D9 for the first time; accepted.** User:
bloom is too strong, halos too prominent (same wanted for AP): a `bloom_strength` setting
with a lower default is in progress (the gain at `capture.cpp:1585` was a hard-coded 1).

## FL run 10 — bloom_strength 0.7 (installed 2026-11-01)

Candidate: commit `b420dc42` (`bloom_strength` setting `5cd520bd`, delta reviewed by the main
session; version 1.8.1), `build-fl/d3d9.dll` SHA-256 `ef209a18…` (55,453,827 B), 0 warnings,
`test_bloom*` 71 OK, `test_config*` 19 OK ([record](../../verification/results/fl-run10/candidate.json));
`C:\X3FL\d3d9.dll` bytes verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run9` (`c7ceeb02…`).
Expected: `bloom_strength_mode requested=0 strength=0.7 value_valid=0`, bloom commits as in run 9,
halos at 70 % of run 9. The same commit is release 1.8.1 for AP (Run214, [releases](releases.md)).

Result (log of 2026-11-01 20:4x, 47,999 frames / 305 s, no exception; measured):
`bloom_strength_mode requested=1 strength=0.7 value_valid=1`, 157 `bloom_prepare ready=1`,
157 `bloom_commit committed=1`, one `scene_handoff` refusal (menu). User: good. **Accepted.**

Shadows on FL (2026-11-01, user report after run 10): sun shadows visible, no issue noticed.
The direction comes from the portable light-constant latch (`sun_light_poll` is AP-only until
step 7); `sun_shadow_apply_frame applied=1` on 46,973 frames of the run 10 log (measured).

## FL run 11 — bloom_strength default 0.25 (installed 2026-11-01)

Candidate: commit `9813e2c0` (default 0.25 in schema, launcher, DLL constant and tests;
version 1.8.2), `build-fl/d3d9.dll` SHA-256 `eafc5b0d…` (55,453,827 B), 0 warnings,
`test_config*` 19 OK, `test_launcher_defaults` 10 OK
([record](../../verification/results/fl-run11/candidate.json)); `C:\X3FL\d3d9.dll` bytes
verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run10` (`ef209a18…`). The user's explicit
`bloom_strength = 0.3` line in `C:\X3FL\x3m.ini` was commented out so the default applies.
Expected: `bloom_strength_mode requested=0 strength=0.25`, halos at a quarter of run 9. The same
default ships for AP as release 1.8.2 (Run215, [releases](releases.md)).

## Step 7 — sun light poll, host evidence (2026-11-01, not installed)

Design: [FL sun light note](../architecture/farnhams-legacy-sun-light.md); contract:
[sun-light.md, "2026-11-01: poll contract"](../reverse-engineering/fl/sun-light.md). Measured on the WSL2 host, the
step-7 commit on `68a23daf` (main's later `e8f0e522` changes docs and results only), `-j4`, one build at a time. Clean-first builds: fl 94 and ap 136 C++ objects compiled (79 and
121 in d3d9), 0 warnings each. The fl link has `sun_light_poll_fl.cpp.obj` and no `sun_light_poll.cpp.obj`, with
`strings | grep -c '^fl\.sun_light'` = 2 and no `608518` in the provider's disassembly. AP: 119 of 121 objects match a
fresh build of main `68a23daf` (`farnhams-legacy-build/cmp_objs.py` →
`verification/results/fl-sun-light-poll/ap_object_compare.json`). The two that differ
are `config.cpp` (one schema key) and `proxy_identity.cpp` (source-commit string). Schema: `fl_sun_light_log` added,
`generate.py --check` PASS, `schema_semantic_compare.txt` added=1 removed=0 changed=0.

The native fixture (`verification/probe/fl_sun_light_poll.cpp`, run by
`verification/results/fl-sun-light-poll/run_fixture.py` → `fixture.json`) gave `RESULT PASS checks=76 failures=0`.
It uses a synthetic relocated image holding the 50 anchors. `ANCHOR flips=491 refused=491` covers every anchor byte
outside the ten relocated operands. A foreign-base relocation and a check at another base are refused, and so is an
unreadable slot (`slot_unreadable`). Selection results:
* A dim directional light (100,50,20) beats point and spot lights of 30000+: score `0x33d` = 0x300 + trunc(61.53).
* With no directional light, the range rule admits a point light at `0x256251` but not one at `0x256250`: score `0xb7`.
* Truncation decides: (0,2,0) `0x301` ranks over a farther (3,0,0) `0x300` (rounding would tie and pick the farther),
  and (128,128,128) scores `0x37f` (127.99999999999999, rounded 128).
* When the engine rule admits nothing, the admission rule's winner is returned: `engine_rule=0`, `0x328`/`0x30a`.
* Range words -1 and INT32_MIN on bright point and spot lights are not above the threshold (signed compare): the dim
  directional light wins, `slot_admitted=1`.
* Equal scores (`0x364` both) go to the farther node, in either array order and under both rules.

Refusals: `layout` for slot counts 0, 7, 9 and 0xffffffff, for a non-light entry and for 255 entries without
terminator (254 are accepted); `unreadable` for the context and for a node; `null_context`; `no_directional`.
`BOUND tested=4 accepted=2 refused=2`: the node read is exactly +0xb0..+0x173 (0xc4 B). LastError is kept on every
poll, and the forced `fl.sun_light_sample` row carries the documented fields. Mutations (`mutation_check.py` → `mutation_check.json`,
source restored byte for byte, unmutated PASS), each failing: rounding instead of truncation 6 failures, a
0xc8-byte node read 1, the light-bit check removed 1, an unsigned range compare 1, the tie rule removed 1. Per poll, measured
natively on fake memory (fixture, not game FPS): 16 candidates 324 ns warm and 1.78 us after a frame epoch
(`engine_memory` revalidates its regions after each Present, the in-game case); 254 candidates 3.95 / 5.57 us.
FL host tests (`test_fl_*`, 10 modules): 59 run, OK, 4 skipped (the shader-corpus oracles; `/tmp` corpus absent).
`test_config_schema`, `test_exe_identity` and `test_volumetric_fog`: 62 OK.

Expected in game with cascades on: `fl.sun_light status=ok reason=none base=0x… slot=0x… context=0x00000000
anchors=50 anchor_rva=0x00000000 log_s=10`. Every 10 s, `fl.sun_light_sample status=ok` with `ok` ≈ `reads`,
`layout=0`, `unreadable=0`, `engine_rule=1`, `score` ≥ 0x300, `flags` & 0x800000, one stable `pos` per sector and
`record=1`. Downstream, `shadow_replay_sun_source … source=point reason=point poll=ok`, and
`volumetric_fog_frame … sun=tracked` with no `volumetric_fog_sun … source=fallback` row. Perf tier:
`shadow_replay_sun_point … record_scale` about 0.01; the shared `point_sun_context_scale` (0.01, AP) is applied to
FL positions, which sun-light.md flags as unverified for FL, so another value blocks the poll from driving shadows. Sun occlusion and the lens flare do not
consume this poll; they stay off on FL.

## FL run 12 — step 7 sun light poll (installed 2026-11-01)

Candidate: commit `232c34ae` (step 7 `4a9b36ba` + review fixes `232c34ae`, reviewed Opus
high: install, no blocking finding), `build-fl/d3d9.dll` SHA-256 `26c677ef…` (55477549 B), 0 warnings,
FL host tests pass ([record](../../verification/results/fl-run12/candidate.json)); `C:\X3FL\d3d9.dll` bytes
verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run11` (`eafc5b0d…`). Expected rows and the flight
checks (`fl.sun_light status=ok anchors=50`, `fl.sun_light_sample … engine_rule=1 score>=0x300`,
`shadow_replay_sun_source … source=point poll=ok`, `shadow_replay_sun_point … record_scale` ≈ 0.01,
`volumetric_fog_frame … sun=tracked`): the Step 7 section above. Also carries `bloom_strength` 0.25.

## Step 6 — sector background, host evidence (2026-11-01, not installed)

Design: [FL sector note](../architecture/farnhams-legacy-sector.md); layout proof
[FL sector fog §6](../reverse-engineering/fl/sector-fog.md#6-verification-pass-for-the-step-6-reader-2026-11-01).
Measured on the WSL2 host, worktree builds (`-j4`, fl then ap): fl and ap 0 warnings each; the fl DLL carries
2 `fl.sector*` row strings. AP: 119 of 121 objects identical to a fresh build of the base commit `68a23daf`
(`verification/results/fl-port/sector-background/ap_object_compare.json`; `config.cpp` one schema key,
`proxy_identity.cpp` the source-commit string). `capture.cpp` and `seta_restore.cpp` (the reader's two AP
instantiations) are identical because the layout is a type parameter (`ApLayout` with the literal addresses).
AP-address rescan of the fl DLL (`ap_address_scan_fl.json`): 20 reachable functions with AP constants, as
before; none is a sector reader (the remaining FL hits of `0x608504`/`0x606f34` are the AP-only chase/effects
modules). `fl_sector_background_constants.py --check` matches X3FL.exe. Schema: `fl_sector_log` added
(`schema_semantic_compare.txt`: added=1 removed=0 changed=0); `generate.py --check` PASS (291 settings).
Native fixture (`run_fixture.py` → `fixture.json`): `RESULT PASS checks=102 failures=0`, `ANCHOR anchors=22
bytes=1112 flips=1112 refused=1112` (each flip names its anchor), identity refusal of the fixture exe
(`executable_mismatch`), an image relocated for another base refused at `registry_lookup`, null/wrapping base
refused; Ready with dust 50, near 1.5 M, far 18.5 M, stardust 7, rates, `config758=3` with a decoy at `+0x768`,
far floors 500 M/100 M; a 21-character name whose NUL is the last committed byte read (an over-read would fail)
and 26 bytes without NUL refused; docked walk Found at depth 1, OtherSector refused; NoCockpit (handle, registry),
Missing, NoSector, BadClass, BadIndex (count, −1), BadCount (5000, −3), Loading, BadDust, CameraMismatch, Cycle;
LastError preserved on every path. TIMING per sample (Ready, docked walk on): 2.77 µs with the name, 2.16 µs
without (fixture, not game FPS). Host tests: `test_fl_*` 10 modules 60 tests OK (4 skips: local shader corpus),
`test_fog*` 27 modules 235 OK (2 skips), `test_sector*` 1 module 4 OK. Fogged FL sectors
(`fogged_sectors.py` → `fogged_sectors.txt`): 298 sectors, 48 with dust, all in 11 compiled fog families.
Expected in game: the note's "Expected rows"; not flown.

Review fixes (2026-11-01, rebased onto main `600a15ff`; measured). F1: the FL edition's `fog_card_match.h`
admits X3FL.exe's second exact card pair `ececc28e00592d35/214fc10d3672ab92` and treats both pixel programs as
card sources and latch signals; AP keeps its single pair. Offline inventory
(`python3 verification/results/fl-port/sector-background/fog_card_pairs.py` → `fog_card_pairs.txt`, through
`fl-shader-coverage/inventory.py`'s `effective_programs` over `/mnt/c/X3FL` and `/mnt/c/X3AP-orig`, 2,352 effects
each): `vs_ececc28e00592d35` 128 DWORDs and `ps_214fc10d3672ab92` 76 DWORDs, both SM1.1, paired in 48 effects
(`nebulafog_0000`, `nebulafog_0001`); the AP pair in 48 (`nebulafog`, `nebulafog2s`); all four programs are also in
AP's resolved set. `test_fog_cards.py` compiles the matcher as both editions. AP objects: 119 of 121 identical to
main `600a15ff` built in this worktree (`ap_object_compare.sh` → `ap_object_compare.json`; `config.cpp`,
`proxy_identity.cpp`), `motion_output.cpp.obj` included. Reviewer's independent figure (not re-measured here):
135 of 138 objects identical after `objcopy --strip-debug`, `capture.cpp.obj` identical in disassembly, sections and
relocations, differing only in 8 compiler-numbered local symbol names. AP-address scan of the fl DLL: 22 reachable
functions; the two new ones (`set_pixel_shader`, `set_vertex_shader`) hold `0x00592d35`, the low word of the FL VS
hash `ececc28e00592d35`, not an address. F5: the fixture adds ReadFailure (sector and table on an uncommitted
page), Malformed (registry, cockpit, table misaligned; capacity 12), Limit (33 links) and BadFog (near > far,
far > 2e9, near 0): `RESULT PASS checks=133 failures=0`, anchors as before, TIMING 2.66 µs / 2.05 µs. F4:
`test_sector_background.py` also builds the extracted production wrapper with `X3M_EDITION_FL=1` against a double
of the FL provider's contract (`checks=22 failures=0` both editions); the FL layout itself stays covered by the
native fixture. Builds fl/ap 0 warnings (`-j4`); `generate.py --check` PASS (292 settings); `test_fl_*` OK
(4 skips), `test_sector*` 4 OK, `test_fog_cards` + `test_volumetric_fog` 23 OK, `test_config*` OK.

## FL run 13 — step 6 sector background, fog (installed 2026-11-01)

Candidate: commit `ad792ade` (step 6 `12b6d364` + review fixes `ad792ade`, reviewed Opus high:
install; the FL fog-card pair `ececc28e…/214fc10d…` added to the classifier under the FL fence from the
offline inventory), `build-fl/d3d9.dll` SHA-256 `1651118e…` (55,541,724 B), 0 warnings, `test_fl_*` and
`test_fog*` pass ([record](../../verification/results/fl-run13/candidate.json)); `C:\X3FL\d3d9.dll` bytes
verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run12` (`26c677ef…`). Also carries `lens_flare_gain`
default 1. Expected: `fl.sector status=ready anchors=22`, `fl.sector_sample … status=ready count=110
name_valid=1`, in a fogged sector `volumetric_fog_frame applied=1 reason=ok`; wrong: native fog cards
visible under the medium (stacked cards), `camera_mismatch` on most frames. Fogged FL sectors
(`verification/results/fl-port/sector-background/fogged_sectors.txt`, 48 of 298): e.g. Queen's Space,
Rolk's Fate, The Hole, Atreus' Clouds (foggreenoutlands, dust 16), Menelaus' Paradise (fogcyancorner).

## AP sun through the hull — triage (2026-11-01)

With `sun_occlusion_log = 1` (Run215 flight, measured; scripts in
`verification/results/run216-sun-occlusion/`): the sun's lens chain on the Mayhem install is one
vs_3_0/ps_3_0 draw `494fe349…/7c83ed50…` per frame (4,112 of 4,113 sun frames); the lens scan refuses
every SM3 pair (`no_free_texcoord`), so it is `Body::Other`, drawn untouched with Z off, while the
override answers the engine probe Visible on 4,111 frames. Sun position, depth lane and classifier
maths are all correct (lane vs record du 0.0008, dv 0.0004; drawn disc within 0.003 u). The dim sun was
`lens_flare_gain` 0.3 (user-confirmed; default now 1). Fix in progress: block the override for a sun
whose body is unclassifiable so the vanilla probe hides it; SM3 body support is the follow-up.

Result, runs 11–13 flown together (log of 2026-11-01 22:5x, 29,482 frames / 196 s, no exception;
measured): `fl.sector status=ready anchors=22`, `sector_background … status=ready` on 160 of 172 samples
(12 `no_cockpit`, menus); `fl.sun_light status=ok anchors=50`, sample `engine_rule=1 score=0x3ff`,
`shadow_replay_sun_source source=point poll=ok`; `volumetric_fog_frame applied=1 reason=ok sun=tracked` 13
rows, `card_refused` 40 (clear sectors), `density_filling` 2; handover at arrival in The Hole
`coldfill=1 frames=45 fill_ms=471`. User: fog works. **Steps 6 and 7 accepted.** Two issues: (1) the
vanilla fog cards stayed visible for 3–4 s after arriving in The Hole (the transit prefill is AP-only, so
FL fills cold); (2) the sun / sun lens is very visible through the fog (one F8 capture). Both under
triage; the FL fog-prefill addresses are being established from X3FL.exe.

## Step 8 — fog transit prefill, host evidence (2026-11-01, not installed)

Design: [FL prefill note](../architecture/farnhams-legacy-prefill.md); layout proof
[FL fog prefill §7](../reverse-engineering/fl/fog-prefill.md#7-verified-constants-2026-11-01). `fog_prefill::walk`
takes a layout type (`ApLayout`: the literal `0x0060850c`/`0x607040`/`0x606fc0`, no guard read, AP's record layout;
`FlLayout` in `fog_prefill_fl.cpp`: module base + generated RVAs and the recovery-guard read first); the new
`Walk::Guarded` is named only under the FL fence so AP's name table is unchanged. Measured on the WSL2 host,
worktree builds (`-j4`, RelWithDebInfo, ap then fl): 0 warnings each. Objects against main `daa2776c` built in the
same worktree first (`verification/results/fl-port/fog-prefill/object_compare.sh` → `ap_object_compare.json`,
`fl_object_compare.json`): AP 119 of 121 identical (`config.cpp` one schema key, `proxy_identity.cpp` the
source-commit string), `capture.cpp`, `motion_output.cpp` and `seta_restore.cpp` included; FL 77 of 80 identical,
differing `capture.cpp` (the FL poll), `motion_output.cpp` (the `guarded` name), `config.cpp`, plus the new
`fog_prefill_fl.cpp.obj`. Schema: `fl_prefill_log` added; `generate.py --check` PASS (293 settings).
Native fixture (`run_fixture.py` → `fixture.json`): `RESULT PASS checks=118 failures=0`, `ANCHOR anchors=18
bytes=617 flips=617 refused=617` (each flip names its anchor); identity refusal of the fixture exe
(`executable_mismatch`, walk not run, LastError kept), an image relocated for another base refused at
`manager_publish`, null/wrapping base refused; Found: the new sector behind a ship (class) and a cut-scene space
(subtype 3) at step 3, 36 reads, record of index 14 (dust 50, near 1.5 M, far 18.5 M, a 21-character name whose
NUL is the last committed byte); guard set (1 or 0xffffffff) with a partially constructed sector at the tail:
`guarded` after one read, nothing of the node reported; guard clear: scene 0 (index 0 or set) `no_scene`, garbage
index `bad_index`; `same_id`, `dead` (stops at the newest sector), subtype 2 skipped to the old sector at step 4,
class skipped; 9 ships at the tail `bound` (8 types, 7 links, 18 reads), 7 ships found at step 8; `head` (empty
list), `no_manager`, `malformed` (manager, prev link), `read_failure` (prev link, tailpred, manager and table on
an uncommitted page), `loading`, `bad_count`, `bad_index`, `bad_record`; the `fl.prefill_sample` row at the first
poll and per period. TIMING per poll: 3.9 µs found with the name, 0.42–0.46 µs refused at the guard (fixture, two
runs, not game FPS). Host tests: `test_fl_*` 13 modules 73 tests OK (4 skips: local shader corpus; 14 modules 79 OK after rebasing onto `3cbb394d`, which adds `test_fl_sun_occlusion_constants` and no compiled source), `test_fog*`
27 modules 235 tests OK (2 skips: local data), `test_sector*` 4 OK, `test_config*` 19 OK. Expected in game: the note's "Expected rows"; not flown.

## FL run 14 — step 8 fog transit prefill (installed 2026-11-02)

Candidate: commit `95c06506` (step 8 `87caaa56` + review doc fixes `95c06506`; reviewed Opus high: install,
no production defect; fresh-tree parity AP 120 of 121 objects identical to `3cbb394d`, only `config.cpp`; FL 77
of 80 plus the new `fog_prefill_fl.cpp.obj`), `build-fl/d3d9.dll` SHA-256 `cc8e50c0…` (55,603,866 B), 0
warnings, `test_fl_*` 79 OK and `test_fog*` 235 OK ([record](../../verification/results/fl-run14/candidate.json));
`C:\X3FL\d3d9.dll` bytes verified, rollback `C:\X3FL\rollback-d3d9\d3d9.dll.run13` (`1651118e…`), X3FL.exe
unchanged (`e7f3aad8…`). Expected rows: `fl.prefill status=ready anchors=18`; in a transit
`volumetric_fog_prefill … walk=found … action=started`; on arrival `event=confirmed … same_id=1 … adopted=1`,
then `volumetric_fog_handover … coldfill=1`. Acceptance is visual: no vanilla fog cards on arrival into a fogged
sector (The Hole, Atreus' Clouds, Menelaus' Paradise). Review open items: the lead between the found poll and the
arrival is unmeasured on FL (below ~0.5 s the cards may still flash); fixture gaps `sector_refused`, cycle,
page-straddling node, unreadable guard, count 0 (handled by structure, untested).

**Run 14 accepted (2026-11-02).** User: "fine now", no vanilla cards on arrival. Log (`C:\X3FL\x3m.log`, measured):
`fl.prefill status=ready anchors=18 failed=none`; 104 `volumetric_fog_prefill` polls: `walk=found` 44 (one
`action=started`, 5 `already_started`, the rest `action=clear`/`none` outside a transit), `walk=same_id` 10,
`walk=head` 50 (no newer sector on the list, i.e. stalls that were not sector transits); one `event=confirmed
lead_ms=1353.4 adopted=1`; handovers `coldfill=1` with `frames=` 38/108/110 (depends on the arrival point, as
the prefill note says). The found-to-arrival lead of 1.35 s answers the open question: the found poll is early
enough.

## Step 9 — sun occlusion hooks, host evidence (2026-11-01, not installed)

Design: [FL sun occlusion note](../architecture/farnhams-legacy-sun-occlusion.md); sites and ABI
[FL sun occlusion, "Verified hook sites and ABI"](../reverse-engineering/fl/sun-occlusion.md#verified-hook-sites-and-abi-2026-11-01).
`sun_occlusion_fl.cpp` replaces `sun_occlusion.cpp` in the fl link (the ap target keeps it); the `Deferral` and the
consumer's deferral calls are no longer fenced out of FL; capture installs the bracket on both editions, also for
the volumetric fog, whose lens gain `G × T_sun` is shared ([sun-occlusion ledger, 2026-11-02](sun-occlusion.md)).
Measured on the WSL2 host after rebasing onto main `317383e5` (release 1.8.4, fade-band fill), clean-first builds (`-j4`, RelWithDebInfo, fl then
ap): fl 96 and ap 136 C++ objects compiled, 0 warnings each; the fl link has `sun_occlusion_fl.cpp.obj` and no
`sun_occlusion.cpp.obj`, `strings | grep -c '^fl\.sun_occlusion'` = 2, no AP address (`606f34`, `608504`,
`471630`, `472491`) in the provider's disassembly. AP against a fresh build of main `317383e5`
(`farnhams-legacy-build/cmp_objs.py <main-ap> build-ap` → `fl-sun-occlusion/ap_object_compare.json`): 117 of 121
identical, `sun_occlusion.cpp.obj` among them. Differing: `capture.cpp` (the bracket also for the fog,
`lens_flare_fog`, the `fog=` field, the FL Present/Reset calls fenced in), `motion_output.cpp` (the fog gain in
`sun_occlusion_begin`, the BLENDFACTOR cap kept, the row fields), `config.cpp` (one schema key), `proxy_identity.cpp`
(source commit). Schema: `fl_sun_occlusion_log` added, `generate.py --check` PASS (294 settings, after main's
`fl_prefill_log`).

Native fixture (`verification/probe/fl_sun_occlusion.cpp`, `fl-sun-occlusion/run_fixture.py` → `fixture.json`):
`RESULT PASS checks=906 failures=0`. The fake walk carries the generated 26-byte probe window with the call at
`% 8 = 2` (`probe_write=atomic`), the fake lens pass `mov edx,esi; call` at `% 8 = 7` (`lens_write=plain`), both
claimed by the production `claim_call`. Checked on every call: ESP balance, EBP/EBX/ESI/EDI, x87 value, CW and MXCSR;
answer path EAX 0/1 without the original, ECX/EDX as at the call, DF from `popfd`, LastError and XMM0-7 unchanged,
record+0x30 cleared exactly on 1 by the window; forward path ECX = record, EDX = view, ESP at the return address
(`[ESP]` = site + 5), the site's EFLAGS, XMM/x87/CW/MXCSR into the original, its EAX, LastError and XMM back. Gates:
flares bit clear at +0xec with AP's +0xfc set → 1; set at +0xec only → 0; x outside the rect with the view flag set
→ 0; inclusive left edge with `0x8000000` → 1; y above the rect → 0. Ineligible probes forward (another view, a
main-view-owned record, an owner without `0x400000`); two records → vanilla for the next frame. Lens: begin/end once,
bracket open only inside the original, EDX view and ECX padding into it, its EAX/ECX/EDX/EFLAGS/LastError/XMM back;
the listeners run on an initialised x87 while XMM, x87, MXCSR and LastError are clobbered. Deferral: an
unclassifiable body starts it (row), the vanilla probe hides (no lift), a vanilla-visible core body lifts it
(`sun_occlusion_released … lifts=1 starts=1`, record+0x30 read), then answered again. SEH raise in the original:
outer handler reached, no `end`, bracket open until Present closes it, answering again. Foreign thread → original,
counted, one row. Registry walk (FL slot layout) finds the cockpit camera, refuses no sector bit / capacity 3 /
missing handle / no slot; scale from `[mode slot] + 0x28`; log mode calls the original through ECX/EDX (`fastcall`)
once and answers itself. Install refusals: target mismatch on the probe after the lens claim (lens rolled back byte
for byte), lens target mismatch, a site one byte off, override without config; non-X3FL process
`executable_mismatch` (also for the bracket alone); after `close_install_window` both claims `late_claim`, nothing
written, also `claim_call` on the straddling site directly. `ANCHOR anchors=17 flips=822 refused=822
operand_flips=52 operand_refused=52` (each naming its anchor), another base and a foreign operand refused, an
uncommitted anchor page refused. `fl.sun_occlusion_sample` deltas checked; `X3M_FL_SUN_OCCLUSION_LOG=0` writes none.

CPU audit (`fl-sun-occlusion/hook_cpu_state_audit.py`, on the fixture and on `build-fl/d3d9.dll` →
`hook_cpu_state_audit_dll.json`): 71 reachable functions, no x87 outside the transport, XMM only in the three
16-`movups` envelopes, `fnsave/fninit/frstor/stmxcsr/ldmxcsr` once each in lens begin and end, none in the probe
handler; the walk stops at `x3m::log` (session log, SSE with `call_preserved` around the formatter), reached only
from `lens_end` and the probe handler inside their envelopes (an unpruned walk: 91 functions, no x87). Mutations
(`mutation_check.py` → `mutation_check.json`, source restored, unmutated PASS 906): no XMM save 54 failures (the
forward path's XMM too: the handler's Win32 calls change XMM on this host), no PreserveCpuState 160, flags word at
+0xfc 67; the first two also fail the audit. Cost per call (fixture, measured, not game FPS): probe forwarded
+39 ns over a 32 ns native call, answered 71 ns total; lens bracket +222 ns per lens call (once per frame) with
trivial listeners. Host tests: `test_fl_*` 84 OK (4 skips: shader corpus); `test_sun_occlusion`,
`test_logging_tiers`, `test_config_schema`, `test_launcher_defaults`, `test_lens_flare_cull`, `test_fog_cards`,
`test_sector_background` 90 OK; `test_fog*` 27 modules 235 OK. Pre-existing on main `181b050a`, unchanged (the other host modules were run on `181b050a`; after the rebase onto `317383e5` `test_fl_sun_occlusion`, `test_sun_occlusion`, `test_logging_tiers`, `test_config_schema` and the fixture were rerun, all OK):
`test_engine_effects` and `test_engine_ribbons` fail their source-bound fixture records (Wine runners, not rerun).
Expected in game: the note's "Expected rows"; not flown.

First-flight checks (beyond the note's expected rows): (a) `starts` and `lifts` in `fl.sun_occlusion_sample` stay
small and stop growing; both rising in every sample means the deferral is cycling (start, lift, start). (b) No
`lens_flare_gain_refused … reason=blend_law` in The Hole. The gain is applied only to ONE/ONE/ADD draws; if the FL SM3
sun body (`ced7edef…`) is drawn with another blend law, the gain does not reach the sun itself, only its other lens
draws, and the user's sun-through-fog report is not fixed by this step (the row's `src`/`dst`/`op` give the law).

## FL run 15 — step 9 sun occlusion hooks, fog lens gain; fade-band fill (installed 2026-11-02)

Candidate: merge `59944c32` (step 9 `5f0d0ac9` + review fixes `b9d5f1fa`, reviewed Opus high: install after
rebase; AP objects 117 of 121 identical to `317383e5`, `sun_occlusion.cpp.obj` among them), also carrying the AP
1.8.4 fade-band original fill (shared code; 133 FL forms pass the native validator). `build-fl/d3d9.dll` SHA-256
`c48809c8…`, 0 warnings, `test_fl_*` OK, seven affected host modules 102 tests green
([record](../../verification/results/fl-run15/candidate.json)); `C:\X3FL\d3d9.dll` bytes verified, rollback
`C:\X3FL\rollback-d3d9\d3d9.dll.run14` (`cc8e50c0…`), X3FL.exe unchanged. Expected rows: `fl.sun_occlusion
status=active … anchors=17`, `lens_flare_gain … bracket=active fog=1`, in The Hole `lens_flare_gain_frame … fog=1
fog_t≈0.58`; wrong: `fl.sun_occlusion_sample` with `starts`/`lifts` rising every sample (deferral cycling),
`lens_flare_gain_refused … reason=blend_law` (the SM3 sun body is not ONE/ONE/ADD, so the gain does not reach the
sun and the sun-through-fog report stays open). Interim by design, removed by the own sun pass
(`docs/architecture/own-sun-pass.md`): the gain uses the sky-cap mean column, not the sun ray, and dims every
additive lens draw in the bracket. Flight: The Hole, sun through fog should be dimmed; a hull in front of the sun
should hide it as vanilla does; nothing else should change.

**Run 15 flown (2026-11-02).** User: the sun is dimmed in The Hole; the lens chain's ghosts and rays read as
flat grey discs over the fog ("turned black"); a station in front of the sun hides it as vanilla does. Log
(measured, `verification/results/fl-run15-black-lens/`): 11 lens draws per frame, 7 additive gained ×0.577
(`fog_t=0.5771`), 4 SRCALPHA/INVSRCALPHA refused `blend_law`; no lens pixel darker than its fog ring in capture
12637; `sun_occlusion_blocked reason=create_clip` from frame 1232 (the ps_2_0 clip program fails native
creation, the known follow-up), so the FL visibility pass never ran and the hull hide was vanilla's. Decision:
no further triage; the additive-over-bright-fog look is an interim artefact of the lens gain and the engine
chain is removed by the own sun pass (`docs/architecture/own-sun-pass.md`). Run 14 remains the rollback.

## Own sun step 3 — FL hidden answer, host evidence (2026-11-02, not flown)

Design and as-built: [own-sun-pass.md](../architecture/own-sun-pass.md) section 8 and section 14, "Step 3 as built".
Base main `c6b232a09` (release 1.8.5). The FL probe answers "hidden" for the eligible record in a latched frame and
the shared stage draws on FL; the lens call is claimed only for the bracket. All measured on the WSL2 host, native
through interop, no Wine, no game.

- **Builds.** `build-fl` and `build-ap` (RelWithDebInfo, `cmake/mingw-i686.cmake`, `-j8`): 0 warnings each. The fl DLL
  carries `own_sun status=%s edition=fl` and the stage's `own_sun_device` row, no `fl_step_pending`.
- **AP objects.** `capture.cpp`, `motion_output.cpp`, `own_sun_hook.cpp`, `own_sun_pass.cpp` objects of `build-ap`
  at `c6b232a09` against the rebuilt tree, debug sections stripped (`own-sun-pass/compare_objects.py`,
  [`step3/ap_object_compare.txt`](../../verification/results/own-sun-pass/step3/ap_object_compare.txt)): the step-3 code
  commit 4 compared, 0 differ; after the review fixes (the no-probe guard, `main_view_known`/`bracket_installed`, the
  two capture rows) 3 differ (`capture`, `motion_output`, `own_sun_hook`; intended), `own_sun_pass` identical. AP hook
  fixture (`own-sun-pass/step2/run_native.sh`): `OWN SUN HOOK checks=46 failures=0`, gain fixture 8 / 0.
- **FL fixture** (`verification/probe/fl_sun_occlusion.cpp` via `test_fl_sun_occlusion.build_and_run`):
  `RESULT PASS checks=766 failures=0` (step 2: 464). New cases: latch without `request_own_sun()` forwards; latched +
  eligible → EAX 1 without the original, record+0x30 cleared by the caller's window, ECX/EDX as at the call, DF back
  from `popfd`, LastError 0x145 and XMM0-7 unchanged, ESP balanced; counters probes +1, own +1, own_sun_hidden +1,
  original +0; `own_sun_hidden()` false before the frame's probe, true in its frame, false after Present; latched +
  another view / main-view-owned record / owner without `0x400000` / main view 0 → forwarded with the forward path's
  full contract, not hidden; a foreign thread in a latched frame runs the original; an SEH unwind through the bracket
  keeps the answer, Present closes the bracket, the next frame answers again; `fl.sun_occlusion_sample … probes=1 own=1
  hidden=1 original=0`; latch cleared → forwards; after `shutdown()` the latch is refused (the handler, called
  directly, forwards before any read: probes +1, original +1, own and hidden +0, `own_sun_hidden()` false, LastError
  kept); `bracket_installed()`/`main_view_known()` true/true with both claims, false/true probe-only, false/false
  unpatched; `initialize(false)` with the
  own sun alone writes one row (`bracket=0 own_sun=1`); probe-only `install_at` leaves the lens call byte for byte
  vanilla (`lens_write=none`, no bracket), half-given lens addresses refused `invalid_site`; log mode in a latched frame
  answers hidden with no row and no original, another view still logged with the original's answer; late window
  refuses the probe-only claim too.
- **Mutations** ([`step3/fl_fixture_mutations.py`](../../verification/results/own-sun-pass/step3/fl_fixture_mutations.py)
  → `fl_fixture_mutations.json`, source restored): no hidden branch 50 failures, answer ignores the latch 5, ignores
  eligibility 37, answers 0 instead of 1 19, answer not recorded 4, latch without the request 8, lens claimed without
  the bracket 12, early exit ignores the latch 48, latch accepted while unpatched 2; unmutated PASS 766.
- **CPU audit** (`fl-sun-occlusion/hook_cpu_state_audit.py`): fixture exit 0 (72 reachable); `build-fl/d3d9.dll` exit 0,
  71 reachable, no x87 outside the transport, no SIMD outside the envelopes, transport ok
  ([`step3/fl_dll_audit.json`](../../verification/results/own-sun-pass/step3/fl_dll_audit.json)).
- **Cost** (fixture, per call, not game FPS; two runs, the spread is host noise): native probe 32.7-33.8 ns;
  forwarded 70.5-94.8 ns; eligible forwarded 73.2-76.4 ns; hidden answer 69.5-76.3 ns; another view in a latched frame
  75.4-75.5 ns; lens bracket +212.8-214.9 ns per lens call.
- **No-probe fail-safe** (both editions, [own-sun-pass.md](../architecture/own-sun-pass.md) section 14): host frame
  model `own_sun_host guard` in `test_own_sun` (`test_no_probe_guard_frame_model`): misses from frame 10 forever →
  the eighth (frame 17) withholds the latch, 600 frames of the engine's sun, retries let through at 617, 1218, 1819
  (3), 20 active frames in 2000; misses in [10, 700) → the first retry misses, the second (frame 1219) hits and the
  latch holds to the end, 2 retries; seven misses never withhold it.
- **Host tests.** `run_host_suite.py --modules test_fl_sun_occlusion test_own_sun test_own_sun_hook test_logging_tiers
  test_config_schema`: 5 modules, 78 tests, 0 failing; also `test_launcher_defaults`, `test_own_sun_lab`,
  `test_comparison_hotkeys` OK; `tools/config/generate.py --check` PASS (293 settings).
- **Builds after the review fixes.** `build-ap` / `build-fl` 0 warnings; FL DLL CPU audit exit 0, 71 reachable, no
  x87/SIMD ([`step3/fl_dll_audit.json`](../../verification/results/own-sun-pass/step3/fl_dll_audit.json)).
- **Expected rows in the first FL flight** (`own_sun` default on, route on): `fl.sun_occlusion status=active …
  probe_write=atomic lens_write=plain|none … own_sun=1 main_view=registry`; `own_sun status=ready edition=fl
  requested=1 reason=ok probe=active`; per device `own_sun_device attached=1`, `own_sun_state armed=1 reason=armed`;
  `fl.sun_occlusion_sample … own=N hidden=N` with N close to `frames=` while a sun is in view (inferred: one
  eligible main-view probe per frame, as on AP). `own_sun_no_probe`
  (or `no_probe=` growing on the `--debug` `own_sun_frame` row) means the main view's probe did not precede the
  stage: the main view's layer above 17 (section 14); after 8 such frames the fail-safe returns the engine's sun
  (`own_sun_frame … starved=1 retries=`, with `--debug` one `own_sun_state … reason=no_probe` row). With the registry
  not admitted (`fl.sun_occlusion … main_view=none`) the own sun stays off: `own_sun status=unavailable
  reason=main_view`.

## FL run 16 — own sun step 3 installed (2026-11-02)

Merge `1c885bc44` (code `7997ebdf6`, docs `69eae3449`, review fixes `91eccff4b`/`0f5774a1b`; review Opus high, no
blocker). `build-fl` 0 warnings, `generate.py --check` PASS, eight host modules 99 tests 0 failing, native FL fixture
766/0 with 9/9 mutations failing (measured). Installed `C:\X3FL\d3d9.dll` = `ed8d50c7…` (55,327,198 B, bytes verified),
rollback `rollback-d3d9\d3d9.dll.run15` = `c48809c8…`; `X3FL.exe` unchanged (`e7f3aad8…`); `x3m.ini` unchanged (no
`own_sun` key, so default on; `debug = 1`). Record: `verification/results/fl-run16/candidate.json`. Also carries the
no-probe fail-safe for AP (not installed on AP; AP stays on release 1.8.5).

**FL run 16 A (open), The Hole:** the same procedural sun as AP 1.8.5 should replace the engine sun and its lens chain;
the grey lens over fog of run 15 should be gone. Fly the hull and a station across the sun once; cross into fog.
Expect `own_sun status=ready edition=fl … reason=ok`, `own_sun_frame … drawn>0 starved=0`, no `starved=1`. If the sun
is the engine's instead, the fail-safe fired (`own_sun_state reason=no_probe` once, `starved=1` on the frame rows):
the FL main view is probed after the resolve, and step 3 needs the probe order fixed.
