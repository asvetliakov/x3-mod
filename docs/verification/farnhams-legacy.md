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
