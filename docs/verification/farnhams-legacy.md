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
  `RESULT PASS checks=806 failures=0` (step 2: 464; 766 before the late-probe rule). New cases: latch without `request_own_sun()` forwards; latched +
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
  → `fl_fixture_mutations.json`, source restored; after the late-probe rule): no hidden branch 58 failures, answer
  ignores the latch 14, ignores eligibility 37, answers 0 instead of 1 22, answer not recorded 5, latch without the
  request 8, lens claimed without the bracket 12, early exit ignores the latch 56, latch not closed at the resolve 9,
  late not counted 1, latch accepted while unpatched 2; unmutated PASS 806.
- **CPU audit** (`fl-sun-occlusion/hook_cpu_state_audit.py`): fixture exit 0 (72 reachable); `build-fl/d3d9.dll` exit 0,
  71 reachable, no x87 outside the transport, no SIMD outside the envelopes, transport ok
  ([`step3/fl_dll_audit.json`](../../verification/results/own-sun-pass/step3/fl_dll_audit.json)).
- **Cost** (fixture, per call, not game FPS; three runs, the spread is host noise): native probe 32.7-35.0 ns;
  forwarded 70.5-94.8 ns; eligible forwarded 73.2-85.2 ns; hidden answer 69.5-76.3 ns; another view in a latched frame
  75.4-82.8 ns; lens bracket +212.8-226.6 ns per lens call.
- **Late-probe rule, after FL run 16** ([own-sun-pass.md](../architecture/own-sun-pass.md) section 14). Run 16 showed
  the review's no-probe fail-safe withholding the latch whenever the sun left the screen (the engine does not probe an
  off-screen sun), so the engine's sun showed for 3-4 s after it returned; the guard is deleted. Instead the resolve
  clears the latch until Present when no hidden answer came before it. Host: `test_own_sun.test_late_probe_rule`
  (`own_sun_host resolve`: latched without the answer → `after_resolve=0`, with it → 1, the next frame latched again;
  the wiring and the deleted guard pinned). FL fixture: after `close_own_sun_frame()` an eligible probe forwards with
  the full forward contract and counts `late` +1, another view's does not, Present answers hidden again.
  After the change (measured): `build-ap` / `build-fl` 0 warnings; `generate.py --check` PASS; the five host modules
  78 tests 0 failing; FL fixture 806 / 0 and 11 of 11 mutations failing (above); FL DLL CPU audit exit 0, 71
  reachable, no x87/SIMD; AP `run_native.sh` hook 46 / 0, gain 8 / 0 (the AP hook fixture has no late case); AP objects
  `capture`, `motion_output`, `own_sun_hook` differ from `c6b232a09` as after the review fixes, `own_sun_pass`
  identical.
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
  stage: the main view's layer above 17 (section 14); the late-probe rule then keeps the engine's sun (`late>0` on
  `own_sun_frame`). With the registry
  not admitted (`fl.sun_occlusion … main_view=none`) the own sun stays off: `own_sun status=unavailable
  reason=main_view`.

## FL run 16 — own sun step 3 installed (2026-11-02)

Merge `1c885bc44` (code `7997ebdf6`, docs `69eae3449`, review fixes `91eccff4b`/`0f5774a1b`; review Opus high, no
blocker). `build-fl` 0 warnings, `generate.py --check` PASS, eight host modules 99 tests 0 failing, native FL fixture
766/0 with 9/9 mutations failing (measured). Installed `C:\X3FL\d3d9.dll` = `ed8d50c7…` (55,327,198 B, bytes verified),
rollback `rollback-d3d9\d3d9.dll.run15` = `c48809c8…`; `X3FL.exe` unchanged (`e7f3aad8…`); `x3m.ini` unchanged (no
`own_sun` key, so default on; `debug = 1`). Record: `verification/results/fl-run16/candidate.json`. Also carries the
no-probe fail-safe for AP (not installed on AP; AP stays on release 1.8.5).

**FL run 16 A flown (2026-11-02).** User: "It works but one issue": after looking away from the sun and back, the
engine's sun shows for 3–4 s before ours replaces it, in foggy and clear sectors. Log (session 05:2x, 29,681 frames,
225 s, exception 0): `own_sun status=ready edition=fl reason=ok probe=active`; with the sun on screen the stage drew
every active frame (936/936), so the FL probe order is correct; `own_sun_state reason=no_probe sun=engine misses=8
retry_frames=600` 12 times, `starved=1` on most frame rows, 35 retries (measured). Cause: the engine does not probe an
off-screen sun, so the no-probe guard (review finding F3) counted the normal off-screen case as a misorder, withheld
the latch and let it through only every 600 frames (about 4.5 s at the flight's frame rate). Fix in FL run 17.
## FL run 17 — late-probe rule replaces the no-probe guard (installed 2026-11-02)

Merge `692ee5e7d` (`2ab985419`): `own_sun::core::ProbeGuard`, the `starved=`/`retries=` fields and the
`own_sun_state reason=no_probe` row deleted on both editions; at the scene-end resolve a latched frame without a hidden
answer clears the latch until Present (`own_sun_hook::close_own_sun_frame()`), so a probe after the resolve forwards
to the engine; eligible probes forwarded that way are counted as `late=` on `own_sun_frame`. Build 0 warnings,
`generate.py --check` PASS, five host modules 78 tests 0 failing, native FL fixture 806/0 with 11/11 mutations failing
(measured). Installed `C:\X3FL\d3d9.dll` = `afd77c47…` (55,324,774 B), rollback `rollback-d3d9\d3d9.dll.run16` =
`ed8d50c7…`; `x3m.ini` unchanged. Record: `verification/results/fl-run17/candidate.json`. AP is not reinstalled
(release 1.8.5 never carried the guard; the late rule reaches AP with its next build).

**FL run 17 A (open), The Hole:** look away from the sun and back several times, in a clear and a foggy sector: ours
must be there on the first frame the sun re-enters the view, with no engine sun in between. Expect `own_sun_frame …
drawn>0 late=0`; `late>0` with the engine's sun visible would mean a probe after the resolve.

**FL run 17 A accepted (2026-11-02).** User: "looks like it's good/working now." Log: 34,182 frames, 219 s, exception 0,
resets 0; `own_sun status=ready edition=fl reason=ok probe=active`; over the 20 frame rows drawn 6,821, `late=0`
throughout, no `own_sun_state` beyond the armed row (`no_probe` 24,540 is now only the off-screen count) (measured).
Own sun step 3 (FL port) accepted; FL run 15's grey lens over fog is closed by it.

## Engine effects FL port, step 1 — host evidence (2026-11-02, not flown)

Design and as-built: [engine-effects-modern.md](../architecture/engine-effects-modern.md), "FL port, step 1"; sites:
[fl/engine-effects.md](../reverse-engineering/fl/engine-effects.md), "Verified 2026-11-02". Base main `4af1e4521`. All
measured on the WSL2 host (native through interop), no Wine, no game.

- **Builds.** `build-fl` and `build-ap` (RelWithDebInfo, `cmake/mingw-i686.cmake`, clean `-j8`): 0 warnings each. The fl
  DLL carries the `engine_effects_patch … edition=fl` format and `fl_draw_only`; `tools/config/generate.py --check` PASS.
- **Stub audit** ([`stub_cpu_state_audit.py`](../../verification/results/fl-engine-effects/stub_cpu_state_audit.py)) on
  the fl DLL ([json](../../verification/results/fl-engine-effects/stub_cpu_state_audit_dll.json)) and the fixture: each
  stub is exactly `push eax; mov eax,[esp+0xc|0x10]; cmpw $7,0x48(eax); pop eax; je; jmp *continue; xor eax,eax;
  ret|ret 0x10` (8 instructions): no call, no x87/MMX/XMM, no ECX/EDX/EBX/ESI/EDI/EBP operand.
- **Native fixture** (`verification/probe/fl_engine_effects.cpp` via `test_fl_engine_effects.build_and_run`):
  `RESULT PASS checks=69 failures=0`. Windows laid into a 64 KB-aligned image, B's fixup relocated to it:
  `a_fnv1a=5a69143c b_fnv1a_masked=601f528c`. Native image: both callees run for classes 7 and 10. Redirected: class 7
  skipped on A (EAX 0, callee not entered, ECX/EDX untouched, ESP balanced by the caller's `add esp,0x20`) and on B
  (`ret 0x10`); class 10 forwarded with ECX, EDX, EAX, EBX, ESI, EDI, EBP, ESP, the return address (`base+0x01507a` /
  `base+0x0150af`), the eight / four stack dwords and DF exactly as at the call; LastError sentinel and
  EBX/ESI/EDI/EBP/ESP/DF kept at the caller on every path. Write paths B `atomic`, A `plain`; only the two calls change.
  Every single-bit change of either window refused before any write: `FLIPS a=328 refused_a=328 b=336 refused_b=336
  fixup=32 fixup_refused=32`; B's fixup zeroed or rebased to another base refused. Rollback: A's read-back fault rolls
  back A and restores B; B's read-back fault rolls back B, A not attempted; A fails and B's restore is refused →
  `rollback_failed`, B registered (`already_installed`), ships lose only the trail, `shutdown()` restores. `shutdown()`
  restores both; `late_claim` after `close_install_window`; `initialize()` rows (10) with `edition=fl` for unset/off
  (`executable_mismatch`: the fixture is not X3FL.exe), `native`, `invalid_setting`, `too_long`, nothing written; and
  with the fixture image as base (seam `fixture_base`) unset reaches `install_base`: `site=A … state=active reason=ok
  mode=plumes setting=- write=plain`, `site=B … write=atomic`, a second call logs nothing, shutdown restores.
  Timing (fixture loop, harness included): skip A 37 ns, forward A 50 ns per call.
- **Mutations** ([`mutation_check.py`](../../verification/results/fl-engine-effects/mutation_check.py),
  [json](../../verification/results/fl-engine-effects/mutation_check.json)): 10 / 10 killed. The two offset mutations
  (AP's A offset `0x14`, B offset `0xc`) are killed by the fixture's crash on the wrong stack slot plus the stub audit,
  without a `FAIL` row; the other eight by `FAIL` rows (ECX or EDX clobbered on A's forward, A `ret 0x20`, B plain
  `ret` and class 10 skipped also by the audit; no B rollback, the late window and B's fixup unchecked by the fixture
  alone).
- **AP objects** ([`ap_object_compare.py`](../../verification/results/fl-engine-effects/ap_object_compare.py),
  [json](../../verification/results/fl-engine-effects/ap_object_compare.json)): `engine_effects.cpp`,
  `engine_effects_patch.cpp`, `motion_output.cpp`, `capture.cpp` compiled with build-ap's flags without `-g`, base
  sources from `git show 4af1e4521`, debug stripped: 4 compared, 4 identical.
- **FL body table** (`tools/effects/engine_bodies.py --game-root C:\X3FL`, edition auto → fl; written outside the game
  tree): sha256 `b350c323fd9ac0995fd979c8ac09927e0f323e74ee9bdc4a333af855a21d94b8`, Bodies `addon2/types/Bodies.pck`
  from `addon2/04.cat` (decoded `c7226417…`), lists 249 JET + 1 SMALLJET (`v\00566`), 18 catalogues (root + addon2, no
  `addon/`), 226 bodies (cyan 123, grey 103), 23 missing (13 not_found, 10 format, among them
  `fx_engine_supplydrone`, `fx_engine_teladi_M4`, `fx_engine_xtc_lightblue_big`/`_nor` and 19 numeric `v\…`). The two FL-only bodies load:
  `p39\effects\engines\playeryacht_engine` (value 18022, cyan, big3) and `p39\effects\engines\terran_ts_xl_engine`
  (8384, cyan, big2). `test_engine_bodies.EngineBodiesFlInstalled` pins the spelling and counts;
  [`install_paths_check.py`](../../verification/results/fl-engine-effects/install_paths_check.py): `manage.py`'s
  `install_engine_bodies` and `x3m_regenerate.engine_step` write `x3m/engine_bodies.json` (edition fl) on a synthetic
  FL layout.
- **Review fixes (own ship, effect pair).** Own-ship walk offsets
  ([`own_ship_offsets.py`](../../verification/results/fl-engine-effects/own_ship_offsets.py),
  [json](../../verification/results/fl-engine-effects/own_ship_offsets.json)): 6 / 6 anchors decode as expected in
  X3FL.exe's cockpit updater `0x421f50` (cockpit in EBX, the function that reads the verified `+0x58`):
  `0x42241c mov esi,[ebx+0xc]`, `0x422441 mov ecx,[esi+0x70]`, `0x42246f mov esi,[esi+0x70]`, stores
  `0x430911`/`0x41a727 mov [edi+0xc],eax`, `0x449bdf mov [esi+0x70],eax`; registry slot/layout and node `+0x18`/`+0x28`
  are generated constants (static_asserts). Effects pair
  ([`effect_pair_check.py`](../../verification/results/fl-engine-effects/effect_pair_check.py),
  [json](../../verification/results/fl-engine-effects/effect_pair_check.json), over a fresh `inventory.py` run: 755
  FL programs): VS `d5e1c753…`, `89193868…` and PS `8360f422…` are FL members (`addon2/01.cat`, `shader/2_0/effects.fb`,
  shared with AP); all 16 FL engine/effects pixel shaders are shared with AP. No recogniser change. Rebuilt clean: 0
  warnings each; AP objects 4 / 4 identical again (the own-ship branch is `#if X3M_EDITION_FL`).

## FL run 18 — engine plumes port step 1 (installed 2026-11-02)

Merge `6ede84be1` (`a354e156e` port, `f13aa725b` review fixes; review Opus high, no blocker; RE
`docs/reverse-engineering/fl/engine-effects.md` "Verified 2026-11-02"). `build-fl` 0 warnings, `generate.py --check`
PASS, nine host modules 120 tests 0 failing, native FL fixture 69/0, 10/10 mutations killed, AP objects 4/4 identical
(measured). Installed `C:\X3FL\d3d9.dll` = `d89a1666…` (55,341,954 B), rollback `rollback-d3d9\d3d9.dll.run17` =
`afd77c47…`; new `C:\X3FL\x3m\engine_bodies.json` = `b350c323…` (226 bodies, 23 missing, FL edition); `x3m.ini`
unchanged (`engine_effects` default `plumes`). Record: `verification/results/fl-run18/candidate.json`.

**FL run 18 A (open), any sector with traffic:** ships must show the mod's plumes (as on AP) and no engine sprite,
engine lens flare or exhaust trail; missiles keep theirs; the own ship has no ribbon; look at a ship on the target
monitor while others fly in the main view (the own-ship tag replaces the majority rule there). Expect
`engine_effects_patch site=A … edition=fl state=active reason=ok write=plain` and `site=B … write=atomic`,
`engine_effects_mode status=armed`, `engine_effects_bodies event=loaded bodies=226`, `engine_effects_device redirects=1`,
`engine_plumes_state`, no `own_ship status=unavailable`. A refusal is `engine_effects_patch … reason=<…>` with
`redirects=0` and `engine_plumes_state … suppression_off` (ships then keep the vanilla effects).

## FL run 18 performance — flag-first recogniser and GPU timing keys (2026-11-02, not flown)

From the run 18 triage and the code reviews; design paragraph in `docs/architecture/engine-frame-time.md`, "FL run 18:
per-frame CPU mitigations". No look change. Evidence: `verification/results/fl-run18-triage/perf_mitigations.txt`
(with `candidate_counts.py`, `read_cost.py`, `verification/probe/fl_run18_read_bench.cpp`).

| Run 18, per frame (measured) | p50 | p99 | mean |
| --- | --- | --- | --- |
| recogniser candidates | 51 | 995 | 88.3 |
| of them `not_jet` | 45 | 977 | 75.5 |
| FL trace `current()` calls | | | 440.9 |
| FL lifetime `current()` calls | | | 328.2 |
| `engine_memory` reads / `VirtualQuery` | | | 5,170 / 63.2 |

Taken: (1) flag-first recogniser (`object_trace::scope_flags`, both editions): the not-jet path 67.6 → 20.0 ns and
4 → 1 validated reads (measured natively), 2.1 µs a frame at p50 and 46.5 µs at p99 (inferred); a jet pays one extra
node read (~20 ns). (4) The ini template carries `gpu_sync_timing` (`gpu_sync_*` rows, halves fps) and
`volumetric_fog_timing` (`volumetric_fog_frame` `cpu_us`) in `[logging]`, both off, read through `config::get`.
Evaluated and not taken: (2) a per-frame FL engine/registry pointer cache (21.1 → 4.4 ns per call, ~11.6 µs a frame)
because an unhooked registry rebind would be seen one frame late; (3) spin backoff with `VirtualQuery` outside the
`engine_memory` lock (one read 10.5 → 9.8 ns, noise; contention not measured) because nothing measured justifies it
and it would void the Wine-bound `fov-patch` record. Log rows: `fl.trace_sample current_calls` / `current_scoped`
drop by the `not_jet` count (`scope_flags` is not counted); `engine_memory reads=` grow more slowly; `engine_frame`
census fields unchanged.

Checks (measured): `build-ap` and `build-fl` RelWithDebInfo 0 warnings; `generate.py --check` PASS (293 settings, 138
in the template); 54 affected host modules, 625 tests: only the stale-record failures known on main
(`test_engine_effects`, `test_engine_plumes`, `test_engine_ribbons`), `test_fov_patch_result` passes; native FL
fixtures `fl_object_trace` (new `scope_flags` agreement checks) and `fl_engine_effects` pass; `motion_output.cpp` under
`X3M_MOTION_OUTPUT_FIXTURE` compiles `-Werror` in both editions; AP objects 120, differ 4 (motion_output, object_trace,
config, the `-dirty` stamp in proxy_identity), all intended.

## FL run 18 result and FL run 19 — measurement build (installed 2026-11-02)

**Run 18 flown.** User: plumes and trails work; capital ships and the Teladi freight transporter show no plume; a
lower steady frame rate. Triage (`verification/results/fl-run18-triage/`): the capital in capture frame 15972 drew
32 hull draws and no JET-flagged or effect-pair draw at all (not a recogniser refusal; upstream in FL's engine glow
creation, under offline investigation); frame time follows the game's draw count (r 0.87), fog adds no residual, run
18 is +0.2 ms (quiet) to +1.3–3.7 ms (busy) per frame over run 17 at equal draw count, growing with engine-light lit
draws (~8–10 µs each, inferred) and plume records; the shadow lane's five cascades (three 4096²) correlate r 0.45; CPU
costs of the ported stages are all < 0.1 ms. Three code reviews (engine effects, fog/prefill, sun/TAA/providers/log)
found no frame-path allocation, lock wait or readback stall; their CPU items were measured at 14 µs/frame total
(`perf_mitigations.txt`) and only the recogniser flag-first read was kept.

**Run 19** merge `0da44e3e7`: flag-first read, `gpu_sync_timing` and `volumetric_fog_timing` ini keys. `build-fl`
0 warnings, seven host modules 80 tests 0 failing (measured). Installed `C:\X3FL\d3d9.dll` = `2171cdb2…`
(55,346,639 B), rollback `rollback-d3d9\d3d9.dll.run18` = `d89a1666…`; ini copy `x3m.ini.run18`, then
`gpu_sync_timing = 1` and `volumetric_fog_timing = 1` added under `[logging]` for this flight only. Record:
`verification/results/fl-run19/candidate.json`.

**FL run 19 A (open), measurement:** two launches in the same busy sector (many ships), about two minutes each,
same spot and heading: (a) as installed; (b) with `engine_light = off` added under `[engine]`. GPU sync timing halves
the frame rate by design; judge nothing by eye. Afterwards remove the two timing keys (or restore `x3m.ini.run18`
and keep `engine_light` as wanted). Rows: `gpu_sync_*` per pass, `volumetric_fog_frame cpu_us`, `engine_light_frame`,
`shadow_replay_*`, `hdr_frame dt_ms`.

**Capital plumes, resolved offline (2026-11-02).** The capital in capture frame 15972 is the Argon Colossus (M1;
matched by draw fingerprint, `hull_fingerprint.py`). Its scene has no engine glow parts, as do almost all stock capital
scenes on FL (5 of 79 capital rows carry engine parts) and on stock AP (9 of 95); the Mayhem-modded AP install swaps
in capital scenes with glow parts (126 of 131), which is why AP capitals have plumes there. FL's few capitals with jets
(Goner MSY, Boron/USC/Xenon M2+, TS+) use bodies that are on the FL JET list. Nothing to fix in the recogniser, table
or redirects: vanilla FL capitals have no engine glow, and giving them plumes needs a new nozzle source (offline
per-hull nozzle table), a design item, not a port gap (`capital_scenes.py`, `capital_scenes_summary.txt`; measured).
The Teladi freight transporter is likely the same case (the four new FL heavy TS have 0 engine parts) but was not in
the capture.

**User decision (2026-11-02):** "Yes i miss them": plumes are to be added to ships whose scenes carry no glow parts
(stock capitals and heavy TS on both editions), from an offline per-hull nozzle table; queued after the frame-rate
work and the lens bracket cleanup. Design item: `docs/architecture/engine-effects-modern.md` phase 4 ("emitter-site
anchor records") is the nearest planned shape; stock capital scenes have no emitter sites either, so the table must
come from hull geometry (rear-facing nozzle meshes / engine dummies) scanned offline.

**FL run 19 A/B flown (2026-11-02; logs `/home/asvetl/x3m-logs/fl-run19a-engine-light-on.log`, `fl-run19b-engine-light-off.log`;
`verification/results/fl-run19-timing/compare.py`, `summary.txt`).** Same sector, 18,205 / 18,374 frames, draws per frame
p50 235 / 242 (comparable). Engine light off changes nothing beyond noise: `engine` pass +128 µs against a 2.7 ms
window spread, frame p50 9.1 vs 8.8 ms (sync timing on, so absolute times are inflated). Per-pass session medians (A):
`scene` 6,016 µs, of which the game's own `engine` draws 2,496 (41 %, incl. ~640 µs HDR flushes), `fog_route` 1,632 on
fogged frames (march 688, repair 472, composite 148, motes 28; fog applied on 18 % / 28 % of frames), `taa` 1,248
(resolve 976), `hdr_writeback` 488, `bloom` 472, shadows 376 (`shadow_depth` 164 + `sun_apply` 212, 6.3 %), own sun
inside `sun_apply`, `engine_plumes` 60, meter/readback/copy ~230. Ribbons and shimmer have no timing pass (measured).
Conclusion: neither the engine light nor the shadow cascades explain the run 17→18 regression; the unmeasured
candidates are the ribbons and the shimmer (inside the resolve bracket; run 18's `taa_run_us` CPU doubled over run 17).
Next: segment C with `engine_effects = native` at the same spot (whole plumes package), then timing passes for ribbons
and shimmer if it is large.

**FL run 19 C and AP fogged comparison (2026-11-02).** Segment C (`engine_effects = native`, same spot) was not faster
(dt p50 11.2 vs 8.9 ms, more fogged frames), so the plumes package is not the cost. Split by window, FL clear frames run
7–9 ms and fogged 11–27 ms (sync on), with the game-draw `engine` pass at 1.1–1.5 ms clear vs 2.8–15 ms fogged while
`fog_route` is a steady 1.6–1.9 ms. An AP 1.8.5 segment in a fogged Mayhem sector (`ap-run227-fog-timing.log`) shows
every proxy stage at the same cost as FL (TAA 1.25, bloom 0.48, writeback 0.48, sun 0.21, fog 1.3 vs 1.7 ms) and the
`engine` pass at 1.6–2.0 ms for ~105 draws; FL's fogged sector draws ~1,200 per frame. FL is not slower per draw
(~4–12 µs vs AP ~16 µs); the sector draws about ten times more (`ap_fl_fogged_compare.txt`, measured). The steady
mod cost per frame at 5120x1440 is therefore ~4 ms (TAA 1.25, fog 1.3–1.7 on fogged frames, bloom + writeback 1.0,
shadows 0.35, sun 0.2, present/meter) on both editions, plus the per-draw additions inside `engine`. Candidates for a
real gain, both editions: the TAA resolve (0.98 ms), the fog march/repair (1.1 ms), and the per-draw hull work (shadow
receiver sampling of five cascades, material rows) in draw-heavy sectors; the last needs a shadows-off A/B segment to
size (`sun_shadow_lane = 0`).

**FL run 19 D, `sun_shadow_lane = 0` (2026-11-02; `fl-run19d-shadows-off.log`, 8,233 frames).** With the shadow lane off the
fog was refused on the fogged frames (`volumetric_fog_frame applied=0 reason=linear_depth`: the fog's linear depth comes
with the lane; user-visible dependency, documented here). The game-draw `engine` pass still scales with the draw count
without any shadow receiver work: 1,866 draws → 9.3 ms, 1,044 → 1.4–5.2 ms, 80–140 → 1.1–1.6 ms (sync on; ~5 µs per
draw at the heavy end), the same per-draw cost as segment C with shadows on. So the shadow receiver sampling is not
the per-draw cost either; what remains inside `engine` is the game's own draws into the FP16 target plus the material
rows, which this instrument cannot separate from vanilla. Conclusion of the run 18/19 frame-rate work: no ported stage
is anomalous; the fixed mod cost is ~4 ms per fogged frame at 5120x1440 on both editions (TAA resolve 1.0, fog 1.1–1.7,
bloom + writeback 1.0, shadows 0.35, sun 0.2) and the heavy FL sectors are draw-bound. Levers if wanted later: TAA
resolve, fog march/repair resolution, per-draw material cost (needs a vanilla-comparable instrument).

**Draw census (2026-11-02; `verification/results/fl-run18-triage/draw_census/`).** FL ship/station LOD data is identical
to stock AP's (732 of 737 ladders byte-equal, same switch thresholds); stock LODs cut triangles, not draws (an M1 draws
33/32/29/24 across its four levels; the AP merged-LOD overlay makes it 3). FL is unoptimised only relative to Mayhem plus
the overlay. Frame 15972's five identified objects: 115 draws, ~47 with the AP overlay rule (inferred). Heavy windows
(1,742 draws median): 825 opaque + 906 blended object draws; the four fogged-background sectors' 46 stations total up
to ~540 draws at LOD 0 per sector, the rest ships, debris (230 one-draw objects in one sector) and blended parts
(inferred). The AP overlay already bakes 317 of FL's 559 ship stems and 115 of 189 station stems. An FL overlay is
game data in `addon2/06.cat` (no DLL change); the bake tooling (`tools/analysis/lod_overlay.py`, mounts, census,
regenerate) is AP-only and needs an addon2 mount and the FL slot rule; three cosmetic gaps need DLL sites later
(occlusion placeholder texture, Terran station distance branch, material strength texture hook). The run 18 capture
log was lost to rotation; the next heavy-sector F8 must be copied before relaunch.

## Lens-flare bracket deleted (2026-11-02, FL and AP; host evidence, not flown)

After FL runs 17 (own sun) and 18 (engine plumes remove the small engine lens flares), the lens bracket, the
lens-flare gain and its fog-transmittance share are deleted on both editions
([own-sun-pass.md](../architecture/own-sun-pass.md) section 14, "Lens-flare bracket deleted"). FL now claims only the
probe call `base+0x8af0a`; `base+0x8bcb7` is never written; the 17-anchor identity (the `lens_block` anchor included)
is unchanged. FL fixture through `test_fl_sun_occlusion.build_and_run` (measured): `RESULT PASS checks=739 failures=0
probe_calls=800029` (806 checks before, same probe calls; every lens call checked byte-for-byte vanilla), anchors
`17 flips=822 refused=822 operand_flips=52 operand_refused=52`, two `fl.sun_occlusion_sample` rows (no `brackets=`
field), forward 37.3 ns over the native probe; the CPU-state audit of the fixture and of `build-fl/d3d9.dll` passes
(68 reachable, one XMM envelope, no x87/MXCSR transport, no lens symbol). FL build 0 warnings. The probe thunk's object
code is the same instruction sequence as before
([thunk_compare.txt](../../verification/results/own-sun-pass/step2/thunk_compare.txt)). Rows: `fl.sun_occlusion status=
base= probe= probe_write= log= own_sun= main_view= anchors= failed_anchor= log_s=`; the `lens_flare_gain` start row is
gone. Full host suite (`run_host_suite.py`, measured): 326 modules, 3,370 tests, 6 failing modules: test_engine_effects, test_engine_plumes, test_engine_ribbons, test_hdr_dither_launch and test_motion_hdr_scene fail on main too; test_engine_shimmer fails because its Wine record (`verification/results/bottle-X3/engine-shimmer/summary.json`) pins the sha256 of `engine_effects_core.h`, changed only by the resolver move (include, alias, comment): it needs a Wine re-record, not a code fix.

## Cull/LOD selector ports — host evidence (2026-11-02, not flown)

`cull_small_parts` (with dock ports and the projectile exemption; the far-jet hand-off fenced, `far_jets=fenced_fl`),
`terran_station_lod` and `lod_occlusion` on X3FL.exe, AP defaults (4 px / projectiles on / dock 12 px, `size`, `all` from the launcher). Design
and rows: [cull-small-parts.md](cull-small-parts.md), "FL port (2026-11-02, not flown)"; sites:
[fl/cull-chain.md](../reverse-engineering/fl/cull-chain.md), "Verified 2026-11-02 against X3FL.exe". Base main
`d8b71cbfe`. All measured on the WSL2 host (native through interop), no Wine, no game.

- **Providers.** `src/engine/fl/cull_small_parts_fl.cpp` (+ `cull_small_parts_sites_fl.h`, `cull_small_parts_fl.h`),
  `terran_station_lod_fl.cpp`, `lod_occlusion_fl.cpp` (+ `lod_patch_sites_fl.h`, `lod_patches_fl.h`) provide the AP
  headers for the fl target; `cull_small_parts.cpp` left `X3M_AP_ENGINE_LINKED_SOURCES` (AP only now). capture.cpp:
  the three `initialize()` calls in the FL engine-effects block, and an `#else` beside each AP frame call
  (`present`, `begin_frame`, `after_reset`, `set_backbuffer_width`). Far jets: `initialize()` installs the stub without
  its far-copy block on FL (review 2026-11-02: the FL far route needs fresh `fl_render_admission` and an owned context
  scale; dock cut-scene ids and the far-copy read set unchecked on FL, [cull-small-parts.md](cull-small-parts.md) "FL
  port"). `engine_far_jets.cpp` reads the model at `+0x144` under `X3M_EDITION_FL`, unreached while fenced.
- **Builds.** `build-fl` and `build-ap` (RelWithDebInfo, `cmake/mingw-i686.cmake`, `-j8`): 0 compiler warnings each; the
  fl DLL carries the four `edition=fl state=%s reason=%s` formats (engine_effects_patch and the three ports);
  `tools/config/generate.py --check` PASS (293 settings). Far-jet handler in the fl DLL
  ([`far_jet_no_x87.py`](../../verification/results/fl-cull-chain/far_jet_no_x87.py),
  [json](../../verification/results/fl-cull-chain/far_jet_no_x87.json)): no x87/SIMD, reads `0x144(…)` only.
- **Native fixture** (`verification/probe/fl_cull_chain.cpp` via `test_fl_cull_chain.build_and_run`):
  `RESULT PASS checks=184 failures=0`. Measured window identities `cull_fnv1a=9df9cf9e terran_fnv1a=0e8e0d62
  occlusion_fnv1a_masked=3105ccaa marker_store=7fd6e34b marker_or=147c2006 jet_writer=4d34a219` (= contract.json). The
  real window bytes run from the site with the selector's frame (ESP = align_down(EBP,8) − 0x28, both EBP alignments):
  80 decisions over 20 node cases × 4 states (full, disarmed, projectiles off, far off): 20 culled, 10 dock, 6 exempt,
  3 far, 41 native. Continue and exempt paths equal the unpatched image in every register, the arithmetic flags and DF
  at `0x0961fc` / `0x096201`, ESP/EBP, locals and the node; culled paths leave EAX/EDX as the native max-threshold block
  (parent threshold / max), ECX = the input measure, `+0x12c` with bit 1 cleared and the AND's flags, identical to a
  native cull of the same node apart from ECX; the far handler gets (node, ECX, EBX) at site ESP − 20 with DF clear and
  ECX survives its clobber. Every single-bit change refused before any write: `FLIPS cull=440 refused=440`,
  `terran=248 refused=248 occlusion=160 refused=160 fixup=32 fixup_refused=32`; the occlusion fixup zeroed or rebased
  refused. Faults: read-back → `readback_rolled_back`, chain push → `chain_failed`, read-back + restore →
  `rollback_failed` registered (the jump runs the tail only; re-install refused; `shutdown()` restores). One-byte
  sequences (buffer Ops): `patch_rolled_back`, `rollback_failed` registered, `rollback_unprotected`, `protect_failed`,
  `restore_not_owned`, `restored`. Executed LOD paths: Terran root with bit 31 stores 1 natively, 0 patched; LOD 2
  takes the placeholder bind natively, the LOD-0 path patched. Rows: 8 `cull_small_parts` rows (`off`, `invalid_px` ×2,
  `invalid_projectiles`, `invalid_dock_px`, `executable_mismatch`, `marker_mismatch`, `active … far_jets=fenced_fl`
  with the stub's far block patched out),
  value and frame rows with `edition=fl`, the terran/occlusion rows `off`/`refused`/`executable_mismatch`/`patched
  write=atomic`. Late window: all three refused, no arena growth, windows unchanged. Timing (fixture loop, harness
  included, noisy): 11–14 ns native, 13–22 ns disarmed, 11–21 ns armed continue, 11–14 ns armed cull per site pass.
- **Stub audit** ([`stub_cpu_state_audit.py`](../../verification/results/fl-cull-chain/stub_cpu_state_audit.py), over
  the bytes the module emitted): full 44 reachable instructions / 1 call, projectiles off 40 / 1, far off 34 / 0; no
  x87/SIMD, no EBX/ESI/EDI/EBP write, ECX written only by the `pop ecx` of its own push, memory written only by the three
  counters, stack slot read only `[ESP+0x10]`, every exit at the site ESP (`jmp [slot]` or `jmp base+0x0961f3`).
- **Mutations** ([`mutation_check.py`](../../verification/results/fl-cull-chain/mutation_check.py),
  [json](../../verification/results/fl-cull-chain/mutation_check.json)): 21 / 21 killed by `FAIL` rows (far block
  unfenced, AP s slot,
  ECX clobbered / not restored, AP model offset, no `cmovg` replay, cull past the engine's AND, far arguments swapped,
  inclusive upper / threshold, marker never set, far bit 0 unchecked, no late window, no window check, no read-back,
  chain failure not rolled back, marker unpinned, occlusion fixup unchecked, Terran ModRM `c8`, no one-byte rollback
  write, no one-byte late window).
- **AP objects** ([`ap_object_compare.py`](../../verification/results/fl-cull-chain/ap_object_compare.py),
  [json](../../verification/results/fl-cull-chain/ap_object_compare.json)): `cull_small_parts.cpp`,
  `terran_station_lod.cpp`, `lod_occlusion.cpp`, `capture.cpp`, `engine_far_jets.cpp` against `d8b71cbfe`, build-ap
  flags without `-g`, debug stripped: 5 / 5 identical. AP small-parts CPU fixture (`build_cull_small_parts.py`, run
  natively): 285 checks; its two `engine: engine stub first/last` checks failed intermittently (3 / 12 runs on the base
  sources, 4 / 12 with this change: the pass exit's PF differed, dead after RET 8), a pre-existing flake. Fixed
  test-only (PF and AF masked in that comparison): 10 / 10 runs `checks=285 failures=0`.

## FL run 20 — bracket deleted, engine-side culls, timing passes (installed 2026-11-02)

Merge `33bf9d045` (bracket deletion `aa99fe173`, culls `cffb327f2`, gpu sync passes `b07192ecc`, flag-first read
`0da44e3e7`). Two reviews (Opus high), no blocker. `build-fl` 0 warnings, ten host modules 103 tests 0 failing, FL
cull fixture 184/0 with 21/21 mutations, FL sun fixture 739/0 (measured). Installed `C:\X3FL\d3d9.dll` = `af2d404f…`
(55,376,665 B), rollback `rollback-d3d9\d3d9.dll.run19` = `2171cdb2…`; ini unchanged. The merged-LOD overlay
catalogue (`addon2\06.cat`) joins this run when the bake lands. Record: `verification/results/fl-run20/candidate.json`.

**FL run 20 A (flown 2026-11-02, accepted):** four F8 captures (four distant capitals; near one capital; twice in the heavy
fogged sector); the user judged the fogged frame rate "somehow better". Log `/home/asvetl/x3m-logs/fl-run20-culls.log`
(507 MB), triage `verification/results/fl-run20/triage_log.py` → `triage_run20.txt`, `triage_run19.txt` (Opus low,
measured unless marked): status rows `terran_station_lod … edition=fl state=patched write=atomic`, `lod_occlusion …
state=patched`, `cull_small_parts … edition=fl state=active write=atomic projectiles=on dock_px=12 far_jets=off`; no
cull row with a fault, rollback, disarm or mismatch. Fogged frames: draws median 468 (p90 545, max 570, n 3,517; span
reconstructed from the 600-frame `volumetric_fog_frame` cadence, inferred) against run 19's 1,317 (p90 1,533, max 1,622,
n 3,001), 2.8x fewer. Capture frames: (1) 354 draws, 403 parts culled; (2) 130→72 draws, 2 culled; (3) 544–555 draws,
610 culled; (4) 426–429 draws, 549 culled; `dock_culled=0 exempt_bullet=0 source=scene fallback=none` on all 32
`cull_small_parts_frame` rows. Fogged dt median 7 ms / p90 12 ms against run 19's 14 / 18 ms (run 19 carried the GPU
sync timing cost, so context only). Plumes armed on every capital capture frame (`records=1`, the own ship; capitals
have no glow parts, known). The split between the three culls is not measurable: the two LOD patches have no per-frame
counters and `cull_small_parts_frame` logs only on capture frames. Terran stations not visually checked; folded into run 21.

Original brief: the heavy fogged sector of runs 18/19, same spot: judge the frame rate by eye against run 18/19
(no timing keys this time), take one F8 there and **tell me before relaunching** (log rotation). Expect
`cull_small_parts … edition=fl state=active reason=ok write=atomic far_jets=fenced_fl`, `terran_station_lod … state=patched`,
`lod_occlusion … state=patched`, `cull_small_parts_frame culled=… dock_culled=…` on the capture frame, `frame_end draws=`
lower than run 19's 1,500–1,980 at the same spot, and no `state=refused`. Visual check: no missing parts on nearby
ships, projectiles still visible, Terran stations drop detail by size. The engine lens flares of vanilla ships are gone
anyway (plumes); nothing else should change.
## Merged-LOD overlay, host evidence (2026-11-02; installed as FL run 21, below)

Tooling: the overlay family gains an FL (addon2) mode ([lod-overlay-mods.md](../architecture/lod-overlay-mods.md),
"FL (addon2) mode"); AP behaviour unchanged (7 host modules test_bob1, test_catalogue_mounts, test_game_guard, test_lod_overlay_batch, test_regenerate, test_lod_overlay_check, test_engine_bodies: 186 tests, 0 failing, measured by the reviewer). Bake: `lod_overlay.py
--game /mnt/c/X3FL --batch --mod none --out OUT` with the AP regenerate defaults (rule ships T_class = min(200, max(80,
2.5 T_1)), stations 150, aspect caps 1.5/2.0, reference width 1800, atlases 1024–2048 DXT, specular and bump on), the
same rule set as AP with no FL-only refusal (Terran stations: the FL DLL patches the distance branch since run 20).
48:42 wall, census 115 s, bake 2,755 s for 637 bodies, at most 9 at once (measured). Two earlier bakes with the same
rule set wrote byte-identical catalogues (measured), so the bake is deterministic. Scripts and outputs:
`verification/results/fl-lod-overlay/` (`commands.txt`). The catalogues stay in the session scratchpad; the user
installs them.

| Item | Ships | Stations | Source |
| --- | --- | --- | --- |
| enumerated (winning bodies) | 556 | 576 | `bake_counts_out.txt` (m) |
| baked | 317 (36 single-LOD) | 320 (28 single-LOD) | (m) |
| not baked and single-LOD (no draw gain, nothing to atlas, no opaque material, format) | 128 | 64 | (m) |
| not baked: unreadable (ships mat3 4; stations parse_error 1, text_parse_error 1) | 4 | 2 | (m) |
| draws below T_pad, summed over the overlay bodies (record 0 → merged record) | 10,415 → 1,633 together | | (m) |

The rows overlap; a single-LOD ladder is not a skip reason as such: the AP rule adds the merged record to it when that
saves draws (64 baked).

- **Output.** Two slots, not one: the fleet's dat is 2,751,698,102 B (atlases 543,110,388 B), above the engine's
  2^31 − 1 member offset limit, so the default cap splits it: `addon2/06` 506 bodies 1,996,148,766 B and `addon2/07`
  131 bodies 755,549,336 B (measured). sha256 `06.cat` `c7a362a5…`, `06.dat` `aeafd068…`, `07.cat` `69c1e0ca…`,
  `07.dat` `6eca6e0b…`; `tool_sha256` `d736dc9f…` equals the committed tool sources (full hashes in
  `bake_counts_out.txt`).
- **Pre-install check** (`lod_overlay_check.py --candidate`): ok — edition fl, slots 06/07 free (game addon2 run
  01..05), no member collision (637 body members, every one an intended override), sources unchanged (measured).
- **Fogged-background sectors** (TBackgrounds row 14, the four sectors of the draw census; `overlay_sectors.py`, the
  `station_sectors.py` method): 46 stations, 383 parts, station draws at LOD 0 **1,934 → 511** with the overlay's
  merged records (−1,423, −74 %); 63 parts covered, 99 parts unread by the method, 212 single-LOD parts (inferred:
  sums over parts, no distance bands, culling or hidden parts modelled; none of the four sectors has a Terran
  station). Equal to the installed AP overlay's 511 on the same parts (`coverage_diff_out.txt`: no part covered by
  one and not the other, measured). The stock coarsest records sum to 474 but are drawn only past the last stock
  threshold; the merged record replaces LOD 0 from T_pad (80–200 px ship radius, 150 px stations) on.
- **Format refusals** (`format_refusals_out.txt`): 183 bodies refused for a format reason (mat3 158,
  material_outside_table 16, text_parse_error 13, classic_unsupported 10, parse_error 1, texture_generated 1;
  177 category other, mostly `v/NNNNN` and menu effects). 170 are refused identically in the installed AP batch;
  7 are FL-only (`others/hsap`, a station: "text line 11: expected parameter value, got an empty value"; the six
  `nebula_kfs_dust_part0N`: "expected vertex z, got '0.000500'"), 6 differ from AP (two FL `fx_engine_xtc_lightblue`
  engine effects mat3 in FL vs no_opaque in AP; `v/00094`, `v/00096`, `v/00127`, `v/00779`). The only ships are the
  four `ships/props/extern_*_dock` mat3 props, refused on AP too.
- **Regenerate.** `x3m_regenerate` on `C:\X3FL` runs the same `--batch --sync --mod auto --install` call into
  `addon2/` (fog family steps skipped, logged), so with unchanged inputs and tool sources it writes the same pair
  (inferred from the identical call and the measured determinism; the synthetic FL run passes, the real-folder
  regenerate was not run because it writes into `C:\X3FL`).
- **Install** (user): copy all six files `addon2/06.cat/.dat/.x3m-lod.json` and `addon2/07.cat/.dat/.x3m-lod.json`
  into `C:\X3FL\addon2\`. Afterwards `python3 tools/analysis/lod_overlay_check.py --game /mnt/c/X3FL` should read
  `lod overlay: ok (637 bodies in slots 06/07, 2.75 GB; sources unchanged; …)`. Flight check: a heavy fogged sector
  (run 19 D windows) against its 1,742-draw median; draws, not frame time, are the first number to compare.

## FL run 21 — merged-LOD overlay catalogue installed (2026-11-02)

Game data only; the DLL stays run 20's `af2d404f…`. Tooling merge `41443182a` (worktree commit `10dd4a40d`, review Opus
medium, no blocker; fixes folded: size figure, test count, marker-type guard in `lod_overlay_check.py`). Installed
`C:\X3FL\addon2\06.cat/.dat/.x3m-lod.json` and `07.*` (637 bodies: 317 ships, 320 stations incl. the 63 Terran-only
ones; 2.75 GB; hashes in `verification/results/fl-lod-overlay/bake_counts_out.txt`, re-verified with `sha256sum -c`
after the copy and the move, measured). Installed-state check `lod_overlay_check.py --game /mnt/c/X3FL`: ok (measured).
Rollback: delete the six files. Record `verification/results/fl-run21/candidate.json`. Expected (inferred from the census):
stations in the fogged background sectors 1,934 → 511 draws, so the fogged `frame_end draws=` median should fall again
from run 20's 468. Fleet coverage (`coverage_fleet.py`): every body the AP batch bakes is baked on FL; FL-only skips are
`others/hsap` (text parse, empty parameter) and effect parts; 187 `*_wreck` bodies are refused no_opaque on both editions
(alpha-flagged materials; whether their textures are opaque is unexamined; candidate for shared baker work).

**FL run 21 A (flown 2026-11-02, accepted):** two F8 captures (clear sector with capitals; fogged sector with stations); the
user: "FPS is much much better now". Log `/home/asvetl/x3m-logs/fl-run21-overlay.log` (466 MB), triage
`verification/results/fl-run21/triage_log.py` → `triage_run21.txt`, `triage_run20.txt` (Opus low, measured unless marked):
fogged frames draws median 191 (p90 212, max 255, n 6,259) against run 20's 468 (p90 545; same DLL), 2.4x fewer; clear
frames (draws > 20) median 61 against 123.5; fogged dt median/p90 4/5 ms against 7/12 ms (different flight paths, so the
span comparison is inferred); fogged capture 190–192 draws, clear capture 56. No new refused/failed/missing row kinds; the
load reached the first drawn frame 0.4–0.6 s sooner. The DLL writes no overlay load row on either edition, so the overlay's
presence is inferred from the draws alone. Shading rows confirm the fade-band fill and the fade-owner shadow receivers and
casters run on FL (`fade_fill` non-zero on 15,717 frames, `fade_receiver` 14,136, `fade_caster` 10,004; `original_fill`
fill 0.01 applied on all 24 variants). `lod_material_strength_configured enabled=1 texture_hook=0`: the material-strength
twin's texture mark is not read on FL (the D3DX texture-row IAT patch is AP-only; X3FL.exe imports the same
`D3DXCreateTextureFromFileInMemoryEx` from `d3dx9_43.dll` instead of `d3dx9_37.dll`), so merged materials draw at the
class's averaged strength on FL; port queued. Terran stations/ships: the user is checking in a new game.

Original brief: the same fogged spot as runs 19/20, one F8 there, and if convenient a look at a Terran station and
a wreck at distance; tell me before relaunching. Expect `lod_overlay` rows at load naming slots 06/07 and 637 bodies,
no `lod_overlay … refused`, fogged draws below run 20's 468 median; visual check: distant stations and ships keep their
silhouette with no missing or untextured parts (an untextured distant body means its atlas failed to load), close-up
unchanged, loading time not noticeably longer.

## Material-strength texture hook on FL, host evidence (2026-11-02, not installed, not flown)

FL logged `lod_material_strength_configured enabled=1 texture_hook=0`: the D3DX texture row lived only in AP's
`loading_trace.cpp`, so the `addon2` atlases' X3ML marks were never read and merged materials drew at the averaged
strength (the third cosmetic gap of the run 18 study above). Now FL patches X3FL.exe's
`D3DXCreateTextureFromFileInMemoryEx` import slot with a forwarding stub of AP's semantics; design, rows and refusal
reasons: [overlay-material-strength.md](../architecture/overlay-material-strength.md), section 13. AP keeps its
`loading_trace` row; both call the shared `d3dx_texture_hook::observe_created`. Base `e6c9d03b5`. All measured on
the WSL2 host (native Windows through interop), no Wine, no game.

| Check | Command | Result |
| --- | --- | --- |
| FL import census | `python3 -I verification/results/fl-material-strength/import_census.py` (`import_census_out.txt`) | X3FL.exe (SHA-256 `e7f3aad8…`, DYNAMIC_BASE) imports 22 names from `d3dx9_43.dll`, one by-name `D3DXCreateTextureFromFileInMemoryEx` at slot RVA `0x150374`, `resolvable=1`; X3AP.exe: `d3dx9_37.dll`, slot RVA `0x132334` |
| Native fixture | `python3 -I verification/results/fl-material-strength/run_fixture.py` (`fixture_out.txt`; `verification/probe/d3dx_texture_hook_fixture.cpp`) | `RESULT PASS checks=61 failures=0` (skips, not fails, without MinGW or WSL interop). The executable imports the system `d3dx9_43.dll`; the hook resolves its `__imp_` slot (`dll=d3dx9_43.dll`, RVA equal). Real D3DX on a hidden-window device (D3D9Ex: X3FL.exe was running in exclusive fullscreen, plain HAL `D3DERR_NOTAVAILABLE`): marked DDS → observer once, lane 1, right device/bytes/size/texture; marked, unmarked and null-device calls return the direct original's HRESULT, LastError (`00c0ffee`), MXCSR and x87 CW. Probe original: entry sees the caller's LastError `00c0ffee`, MXCSR `7f80`, CW `0e7f`, x87 top 7; the caller leaves with `beef`, `9fc0`, `037f`, top 6 (S_FALSE and E_FAIL; observer only on success). Rollback: `restore()` back to the export, the restored stub still forwards; one and two failed protection restores → `protect_restore_failed`, slot original; a writer chaining on top inside the failed restore → `foreign_chained`, no undo, the foreign pointer kept and the stub it chained to still forwards with the observer detached. Refusals: `no_observer`, `foreign_redirect` (slot untouched), `already_installed`, synthetic section views `slot_not_writable` (read-only view), `import_absent` (absent, `d3dx9.dll`/`xd3dx9_43.dll`, no name table), `ambiguous_import`, `dll_not_loaded`, `slot_misaligned`, `foreign_redirect`, `bad_image`; `D3DX9_43.DLL` (upper case) resolves and its slot round-trips byte-identically; `late_claim` after `close_install_window`. LastError kept across every `install()` |
| Mutation | `cpu.after_original()` removed from the stub, fixture rerun | 2 failures (both probe-original exit checks); restored |
| Host modules | `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_material_strength test_fl_material_strength_hook` | 11 tests, 0 failing; neighbours (logging_tiers, exe_identity, fl_identity, fl_cull_chain, music_keep, runner_housekeeping, fl_engine_effects, fl_scene_hook) 90 tests, 0 failing |
| Builds | `cmake --build build-fl -j`, `cmake --build build-ap -j` (fresh trees, RelWithDebInfo, mingw-i686) | 0 warnings each; FL stub ends in `ret 0x3c` (15 stdcall arguments); `check_no_x87.py build-ap/d3d9.dll` PASS; `tools/config/generate.py --check` PASS (no setting changed) |

Not verified: the in-game FL path (rows `lod_material_strength_texture_hook state=installed dll=d3dx9_43.dll
slot_rva=0x00150374 edition=fl`, `texture_hook=1`, then the debug-tier `lod_strength_texture` rows for the `addon2`
atlases and `lod_strength_frame` marked draws) waits for an FL run with the overlay catalogue installed.

## Proxy culls on FL (small props, GPU occlusion), host evidence (2026-11-02, not flown)

Before: FL run 21 logged `cull_small_props_frame … draws=0 culled=0` on all 133 rows (2,463,523 draws `no_scope`) and
`occlusion_cull_config configured=1` with no `occlusion_cull_device`/`occlusion_cull_session` row (measured,
[run21_baseline.sh](../../verification/results/fl-proxy-culls/run21_baseline.sh)): both draw-hook culls were gated on
`object_trace::executable_verified()`, constant false on FL. AP run 227 for scale: `draws=15154 culled=12154` and
`candidates=44329 tested=24542 hidden=21576` per 300 frames (from the brief, not re-measured).

Change (base `e6c9d03b5`): gate `object_trace::scope_available()` (AP `executable_verified()`, FL `active()`);
`engine_node_layout.h` (AP model `+0x140`, FL `+0x144`, parent `+0x18`, handle `+0x28`) read through
`Culler::layout` / `Classifier::layout`; FL globals at attach: body manager `base+0x1aa100`, cockpit registry
`base+0x1a82e4`. Without the FL scope seam (object trace off or refused) each cull logs one
`… attached=0 edition=fl reason=scope_unavailable` device row and stays off on that device (small props at its first
scene draw, occlusion at its first candidate draw); small props also refuse with `reason=registry_not_admitted`. FL
rows end in `edition=fl`; the descriptor part box (F8 diagnostic) and the engine-side skip stay AP only
(`occlusion_cull = engine` on FL logs `engine=0 engine_status=ap_only`). Defaults unchanged
(4 px, pool 1024, retest 8). Every FL read and its source:
[fl/cull-chain.md](../reverse-engineering/fl/cull-chain.md), "Proxy culls' engine reads (2026-11-02)".

| Check | Command | Result (measured) |
| --- | --- | --- |
| FL offsets not generated elsewhere, decoded from `C:\X3FL\X3FL.exe` (`e7f3aad8…`) | `python3 -I verification/results/fl-proxy-culls/offsets.py` | 19 / 19 anchors ([offsets.json](../../verification/results/fl-proxy-culls/offsets.json)) |
| Builds | `cmake --build build-fl -j`, then `cmake --build build-ap -j` (worktree, not candidates) | 0 warnings each; FL `eb1f007d…` 55,472,094 B, AP `473fddf4…` 62,700,157 B (after review fixes) |
| x87 | `python3 verification/probe/check_no_x87.py build-ap/d3d9.dll` | PASS |
| x87, FL (tool gap) | `python3 verification/probe/check_no_x87.py build-fl/d3d9.dll` | not checkable: the tool requires the AP-only gz hook symbols (`gz_read: 0 symbols`); not a pass |
| Config | `python3 tools/config/generate.py --check` | PASS, 292 settings |
| Host | `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_cull_small_parts test_occlusion_cull test_fl_object_trace test_exe_identity test_fl_identity test_logging_tiers` | 6 modules, 89 tests, 0 failing |
| Cores under both layouts | `test_cull_small_parts.CullSmallPropsCore.test_core_compiled`, `test_occlusion_cull.test_core_driver`: each core compiled without a define (AP) and with `-DX3M_EDITION_FL=1`; every node carries a decoy model id at the other edition's offset | both editions 0 failures; occlusion driver 133 checks per edition |
| Mutation: the FL build forced onto AP's layout | scratch copy of `src/proxy` with `edition = ap` under `X3M_EDITION_FL` | props harness 15 failures, occlusion driver 17 failures (killed) |
| Wiring | `test_edition_gate_and_addresses` in both modules | gate, attach addresses, AP-only ledger and part box, `edition=fl` rows pinned |

The occlusion GPU fixture records (`occlusion-cull-batched/fixture-*.json`) ran the 2026-10-08 `occlusion_cull_core.h`;
`test_batched_records` now accepts that recorded hash or the 2026-11-02 one (AP offsets unchanged, proven by the AP
build of the host driver); a rerun needs a Wine host. Not verified: either cull on FL in game; the FL saving.

## FL run 22 — material-strength hook and proxy culls on FL (installed 2026-11-02)

Merge `e1a43c4ab` (hook `9446e7a3b`, culls `e1a43c4ab`), two Opus-high reviews with no blocker (fixes folded, sections
above). Clean `build-fl` from main, 0 warnings; 12 host modules 140 tests 0 failing; `generate.py --check` PASS (measured).
Installed `C:\X3FL\d3d9.dll` = `81dd4fb5…` (55,536,571 B), rollback `rollback-d3d9\d3d9.dll.run21` = `af2d404f…`
(runs 20–21); overlay `addon2/06+07` and ini unchanged. Record `verification/results/fl-run22/candidate.json`.

**FL run 22 A (flown 2026-11-02, accepted):** two F8 (clear with ships; fogged with stations); user: nothing wrong by eye.
Log `/home/asvetl/x3m-logs/fl-run22-culls-strength.log`, triage `verification/results/fl-run22/triage_log.py` (Opus medium,
measured): hook `state=installed dll=d3dx9_43.dll slot_rva=0x00150374`, `texture_hook=1`, 328 marked atlas loads (82 per
slot; the 16 `lod_strength_texture` rows are the log cap), 14 twins, `marked=366,984 no_twin=0` (same shape as AP run
204); small props `attached=1 model_offset=0x144`, 69,733 prop verdicts, 2,089 culled, 0 unresolved (two own-ship props
at 173 px kept every frame, inferred); occlusion `attached=1`, 180,272 candidates, 15,881 tested, 2,101 hidden, 1,601
skipped, 48 % `no_hull` (station dock/turret-named parts or parts drawn before their hull, inferred, open); fogged
frames: neither proxy cull skipped anything. `frame_end draws=` counts skipped draws too; the proxy culls show in
`issued=` (fogged not-issued median 1). Fogged draws median 187 vs run 21's 191 (same content). One device Reset at
frame 33,249, recovered. Bolt features: `screen_emission_additive … pair_draws=0` over the whole flight (AP flights:
thousands), chase camera not ported (`bolt_footprint view_gate=chase_camera`): whether the bolt recogniser matches FL
bullets is open until a flight with weapons fired.

Original brief: the fogged spot of runs 19–21 plus a clear sector with ships, one F8 each; tell me before
relaunching. Expect `lod_material_strength_texture_hook state=installed dll=d3dx9_43.dll … edition=fl`,
`lod_material_strength_configured enabled=1 texture_hook=1`, `lod_strength_texture`/`lod_strength_frame seen>0`
(atlases marked at load), `cull_small_props_device … attached=1 edition=fl model_offset=0x144`,
`cull_small_props_frame … draws>0 culled>0`, `occlusion_cull_device … attached=1`, `occlusion_cull_session … hidden>0`,
fogged `frame_end draws=` below run 21's 191 median, no `attached=0`/`refused` rows. Visual: distant station trims no
longer pale/flat (material strength), nothing visible missing near ships or stations (small props, occlusion), own
ship and target intact in chase view, no pop-in when turning (occlusion retest).

## FL run 23 — occlusion_cull default off (installed 2026-11-02)

Commit `ba32f497f`: unset/empty `X3M_OCCLUSION_CULL` now means off, the documented default (the code said on since 2026-10-08:
every AP flight since then and FL run 22 ran the draw-level cull; `docs/verification/occlusion-cull.md`, 2026-11-02
section; user decision 2026-11-02 "leave it off by default too"). Host: 4 modules 72 tests 0 failing; `build-fl` 0
warnings (measured). Installed `C:\X3FL\d3d9.dll` = `647e8ebc…` (55,536,571 B), rollback `rollback-d3d9\d3d9.dll.run22` =
`81dd4fb5…`. Record `verification/results/fl-run23/candidate.json`.

**FL run 23 A (flown 2026-11-02, accepted):** three F8 (clear with distant capitals; same while firing; fogged sector).
Log `/home/asvetl/x3m-logs/fl-run23-occlusion-off.log`, triage `verification/results/fl-run23/triage_log.py`, `bolts.py`
(Opus low, measured): `occlusion_cull_config requested=unset configured=0 reason=off edition=fl`, no device row.
Occlusion A/B at matched load (run 22 on vs run 23 off, `dt_ms` binned by issued draws in 20s, clear and fogged apart):
p50 4 ms in every bin of both runs; p90 4–7 (on) vs 4–6 (off) clear, 5–7 (on) vs 4 (off) fogged at 160–240 issued:
indistinguishable at the log's 1 ms resolution, if anything on is one quantum worse at p90 (inferred), the same verdict
as the AP A/B of 2026-10-08; decision: default stays off, `occlusion_cull = on` remains the opt-in. Draws: fogged median
176 (p90 297, n 14,108), clear 83. Bolts: `pair_draws=15,158 admitted=13,455 refused=1,703` (all `state`, 11 % vs AP's
49 %), `bolt_flag=13,455`, `bolt_dropped=0` (AP 0 too), `bolt_footprint … gated=13,453` (chase camera not ported, so the
footprint never writes), `bolt_copy` rows on the firing capture: the bolt pipeline behaves as on AP apart from the chase
gate (inferred). No new fault, invalid or `attached=0` rows.

Original brief: any flight; fire a few shots at a distant target in open space (bolt recogniser check:
`screen_emission_additive_refused_window pair_draws>0`, `bolt_far_composite` rows). Expect `occlusion_cull_config
requested=unset configured=0 reason=off edition=fl` and no `occlusion_cull_device` row. Tell me before relaunching.
## FOV on FL, host evidence (2026-11-02, not installed, not flown)

`fov` (`X3M_FOV`, schema default 90) now runs on FL: `src/engine/fl/fov_fl.cpp` provides `fov.h` for X3FL.exe (AP keeps
`fov.cpp`, unchanged): the constructor imm32 at base+`0x01e08d` (plain write, crosses a qword), INS_SetFocus's
`mov eax, [base+0x1a82e4]` at base+`0x030eaa` and the load store at base+`0x01df09` (lock cmpxchg8b each), with AP's
remap table and exact-match load rule in FL register variants; all three or none. Sites, ABI and what held:
[FL field of view](../reverse-engineering/fl/field-of-view.md), "Verified 2026-11-02". Rows: one
`fov site=… rva=01e08d edition=fl status=patched reason=ok value=0x3470 … write=plain registry=none setfocus=active
setfocus_write=atomic load=active load_write=atomic`, then up to 16 `fov_confirm frame=… edition=fl registry=… focus=…
expected=… match=… kind=constructor|remapped|vanilla|other …` rows, one per change of the admitted registry's +0x24
(FL has no save_load_complete marker). `fov::present()` samples the admitted registry's +0x24 once every 64 Presents
(Present thread only) and `fov::current_focus()` returns that sample, else the configured value, with no read of its own;
the small-parts cull's fallback uses it (the chase camera reads camera +0x298 and does not call it). The imm32 plain
write runs at backend load inside the install window, not in the note's static-load transaction (the note's
"Verified 2026-11-02" section: why it is safe, inferred). All measured on the WSL2 host, no Wine, no game.

| Check | Command | Result |
| --- | --- | --- |
| Sites on the installed exe | `sh verification/results/fl-fov/reproduce.sh` (`verify_fov_site_fl.json`) | PASS, 31 checks: six windows (FNV-1a with fixups zeroed, `.reloc` census = declared fixups), decodes, liveness, anchors, callers, interior entries (0 into the detours), the shared table (70/80/90/100 → `0x2768`/`0x2dc5`/`0x3470`/`0x3b6f`) |
| Native fixture | `test_fl_fov` (`verification/probe/fl_fov.cpp`, `fl_fov_shim.h`) | full run `RESULT PASS checks=49 failures=0`: one sample per 64 `present()` calls, a change seen only at the next sample, 16-row cap (`SAMPLES interval=64 frames=2817 rows=16`); 1360/1360 window bits and 96/96 fixup bits refused with nothing emitted; 196 SetFocus vectors (86 remapped) and 358 load vectors (83 remapped) through the real bytes, every register as documented, non-arithmetic flags, DF, XMM7, x87 CW, MXCSR, LastError kept; 9 seam faults (literal read-back, both read-backs, both chains, a refused SetFocus rollback, an unreadable literal restore) all-or-none or registered; `initialize()` rows; `late_claim`. `retain` run 6/6: after the window closes the jumps restore, the literal stays (`literal_retained`) |
| Stub audit | `test_fl_fov.FlFovNativeFixture.test_emitted_stubs` (Capstone) | SetFocus 9 instructions writing only EAX, ECX, EFLAGS; load 11 writing only ECX, EDX, EFLAGS; no memory write, push/pop or call |
| Mutations | `test_fov_site.FovSiteFl.test_mutated_copies_fail` | 5/5 mutated copies (imm32, SetFocus opcode, load store, caller TEST, callee CMP) fail their specific check |
| Host modules | `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_fov_site test_fl_fov test_config_schema` | 38 tests, 0 failing; neighbours (engine_effects_sites, ui_scale, fl_cull_chain, logging_tiers, runner_housekeeping and eight test_fl_*) 95 tests, 0 failing |
| Builds | `cmake --build build-fl -j`, `cmake --build build-ap -j` (fresh trees, RelWithDebInfo, mingw-i686) | 0 warnings each; `check_no_x87.py build-ap/d3d9.dll` PASS (1110 reachable functions); `tools/config/generate.py --check` PASS (`fov` described for both editions) |

User-run note: `fov` is on by default (schema 90), so the first FL launch with this build narrows the view: the
constructor's focus becomes `0x3470` instead of the engine's `0x4000` (90° horizontal on 16:9 instead of 90° on 4:3),
unless the ini says `fov = game`.

Not verified: the in-game FL path. A user run should show the `fov … edition=fl status=patched` row, a first
`fov_confirm … kind=constructor` row on a new game (or the save's value, `kind=remapped` for a vanilla save, after a
load), `kind=remapped` rows on menu steps, and the wider view; mode 6 and the saved cockpit+0x240 cache stay unpatched
(note section 3).
## Chase camera pose hook on FL, host evidence (2026-11-02, not installed, not flown)

`camera = chase` is the FL ini default, but FL logged no `chase_camera` row: the module's initializer and report sat
in AP-only fences. Now the FL build installs the same pose seam in X3FL.exe's cockpit updater at base + RVA `0x0227ad`
(ten bytes `cmp dword [ebx+0x54],0; je`, rel32 at +6, continuation `0x0227b7`, plain copy inside the install window
because the jump crosses a qword), with the shared spring/elevated handler (`offset_y`, `pitch_down_deg`,
`distance_scale`, `fov_compensate`). Sites, field anchors and the dependency boundary:
[fl/chase-camera.md](../reverse-engineering/fl/chase-camera.md), section 6. `chase_transition`, `chase_lead`,
`chase_fire` and `chase_aim_trace` stay AP-only: compiled out of the FL handler, one
`<module> installed=0 status=ap_only edition=fl reason=ap_only` row each; the unbound (`+0x10 == 0`) phase is never
admitted on FL. AP object code of `chase_camera.cpp` is unchanged (objdump `.text` and `.rdata` diff against the base
build: 0 lines). Base `9446e7a3b`. All measured on the WSL2 host (native Windows through interop), no Wine, no game.

| Check | Command | Result |
| --- | --- | --- |
| FL site | `python3 verification/probe/verify_chase_camera_site.py --edition fl --json` (`verification/results/fl-chase-camera/verify_fl_site.json`) | PASS: bytes, Capstone `[4, 6]`, no HIGHLOW in the span (38,327 total), the seven inbound edges and no interior edge (72,230 direct edges), sole updater caller `0x01e5b0`, qword crossing (`& 7 == 5`), EBX = cockpit to the seam, 43 field anchors, three window FNV-1a values, plane fixup `0x005a82f8`; AP mode still PASS |
| Native fixture | `python3 -I verification/results/fl-chase-camera/run_fixture.py` (`fixture_out.txt`; `verification/probe/fl_chase_camera.cpp`) | `RESULT PASS checks=41 failures=0`: both outcomes of the replayed `je` (re-based rel32) for ESP skews 0/4/8/12, every GPR, EFLAGS (= the native compare's), ESP, XMM0-7, x87 CW `007f` with one loaded register, MXCSR `bfe0` and LastError intact at the continuation/target; pose written through EBX (vanilla `0,200,-2000` → `0,808,-3778`, row padding kept), `frames=9 applied=8 refused=1 inactive=2 unbound=0`; 528/56/88 window bit flips refused before any write; read-back faults written into the synthetic span: a byte inside the jump → original ten bytes, `installed=0 … rollback=restored`; bytes inside and beyond it → `installed=1 status=readback_mismatch rollback=failed`, site registered and inert, `installed()` agrees; shutdown restores and a re-claim works; `late_claim` after `close_install_window` |
| Mutations | `python3 -I verification/results/fl-chase-camera/mutation_check.py` (`mutation_check.txt`) | 4 / 4 killed: no `PreserveCpuState` (11 failed checks), unbound admitted (2), read-back check skipped (5), rollback through `engine_patch::restore` only (5: the not-owned refusal) |
| Host modules | `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_chase_camera test_chase_camera_site test_fl_chase_camera test_config_schema` | 97 tests, 0 failing; `test_bolt_footprint` pin updated to the fenced inactive return (25 tests, 0 failing) |
| Builds | `cmake --build build-fl -j`, then `cmake --build build-ap -j` (fresh worktree trees, RelWithDebInfo, mingw-i686) | 0 warnings each; `check_no_x87.py build-ap/d3d9.dll` PASS (1,110 functions); `tools/config/generate.py --check` PASS (`camera` and the four geometry keys marked for both editions; `chase_hud_anchor`, `chase_view_restore` and `chase_view_restore_retries` open with "X3: Albion Prelude only, ignored on Farnham's Legacy."; template text only) |

Expected in game (not verified): `chase_camera requested=1 installed=1 status=active site=0x<base+0227ad> length=10
rel32_offset=6 atomic_write=0 … edition=fl rva=0x0227ad rollback=none unbound_phase=ap_only dependencies=none`, then
the four `ap_only` rows, `chase_camera first_applied …` at the first applied frame and `chase_camera_last_device kept=1`
at exit. The raised rear framing, boom scaling and smoothness on FL need a flight.

Known FL gaps to expect in this flight (not defects of the pose hook):

- After each gate transit (and jumpdrive) the FL chase view shows vanilla framing for a short while, then jumps to the
  chase pose with a TAA history cut. A fresh cockpit generation reaches the seam with `+0x10 == 0` for tens of updates
  (AP Run 78: 12–36 updates, 0.4–1.1 s; the FL figures are inferred from that, not measured), and FL never admits that
  unbound phase because its proof is `chase_transition`'s registry walk, AP-only. This is the known gap the
  `chase_transition` FL step closes.
- The native centre crosshair is a false aim cue on FL: with `chase_offset_y = 0.5` the ship sits halfway down the
  screen, so the screen centre is not where the ship points, and the true-forward reticle (`chase_hud_anchor`) is
  `chase_lead`, AP-only until it is ported. `chase_hud_anchor`, `chase_view_restore` and `chase_view_restore_retries` are
  ignored on FL (the template says so on their first line).
- The bolt footprint's view gate (`chase_camera::pose_applied_since`) can open on FL for the first time with this build,
  so its `plan_draw`/substitute-buffer path runs on FL for the first time (inferred from the source: the gate was always
  closed while the FL site was never installed; unflown).

## FL run 24 — field of view and chase camera pose hook on FL (installed 2026-11-02)

Merges `17f8ef034` (FOV, worktree commit `be484d7c8`) and `caae7e209` (chase camera, `4490db3e6`), two Opus-high reviews
with no blocker, fixes folded (sections above). Clean `build-fl` from main `caae7e209`, 0 warnings; 11 host modules 188 tests
0 failing; both FL site verifiers PASS on the installed exe (measured). Installed `C:\X3FL\d3d9.dll` = `440dbe52…`
(55,579,145 B), rollback `rollback-d3d9\d3d9.dll.run23` = `647e8ebc…`; overlay and ini unchanged (template defaults: `fov = 90`,
`camera = chase`). Record `verification/results/fl-run24/candidate.json`.

**FL run 24 A (flown 2026-11-02, accepted for FOV and the camera pose):** user: "fov and chase camera works"; a sector
change switched the view back to first person and did not restore; the HUD elements of the chase view (crosshair, distance
icon, target overlay, possibly the lead reticle) are not shown. Both are the documented AP-only gaps (`chase_transition`,
`chase_lead`), now **user decision 2026-11-02: port the view restore and the central HUD / `chase_hud_anchor` and the other
UI elements so the chase view shows them as on AP**. Log `/home/asvetl/x3m-logs/fl-run24-fov-chase.log` (measured):
`fov … edition=fl status=patched value=0x3470 write=plain setfocus=active setfocus_write=atomic load=active
load_write=atomic`; `fov_confirm` rows `match=1 kind=constructor` at frames 64 and 2240, then `focus=0x351f
kind=remapped` (a script/menu SetFocus remapped through the table, so the setter site works); `chase_camera installed=1
status=active site=0x00dd27ad` with 198 window/timing rows; `chase_transition`/`chase_lead`/`chase_fire`/`chase_aim_trace
installed=0 status=ap_only edition=fl`; `bolt_footprint session_draws=8274 written=5660 expanded=42,158 gated=1,355`
(the footprint path ran on FL for the first time; no refusal). No `installed=0`, `refused` or `rollback=failed` on the
FOV or camera rows.

Original brief: load a game, fly in open space and through one gate, dock once if convenient, take one F8 in flight;
tell me before relaunching. Expect `fov … edition=fl status=active` with the constructor literal, SetFocus and load sites
claimed (`write=plain setfocus=atomic load=atomic` or the row's equivalents), `fov_confirm` rows showing `0x3470`
(90 on 16:9) after load, `chase_camera requested=1 installed=1 status=active edition=fl rva=0x0227ad`, four
`… status=ap_only edition=fl` rows (chase_transition, chase_lead, chase_fire, chase_aim_trace), `bolt_footprint …
written>0` when firing, no `installed=0`/`refused` on these. Visual: the view is wider than before (90 horizontal on
16:9; set `fov = game` to compare), the ship is seen from behind and slightly above with the spring lag as on AP;
**known gap:** after a gate transit or jump the first ~0.4–1.1 s show vanilla framing, then the chase pose snaps in
(view restore is the next step); the centre crosshair is not the aim point under the raised camera until the lead marker
is ported. Report anything that looks like a camera jump, wrong FOV in menus or on load, or missing HUD.

## Chase HUD on FL, host evidence (2026-11-02, not installed, not flown)

Run 24's missing chase HUD (crosshair, speed/distance panels, lead reticle) is AP's `chase_lead` module, AP-only on FL
until now. Ported as `src/engine/fl/chase_lead_fl.cpp` + `chase_lead_sites_fl.h` (AP `src/proxy/chase_lead.cpp`
unchanged and no longer linked into FL): the HUD gate (the five central nodes: crosshair, target speed, own speed,
target distance, status icon), the lead gate, publish and final FOV (the predictive reticle at the chase-projected
point, and the true-forward placement of the five nodes for `chase_hud_anchor = forward`). The target brackets need no
hook (native mode test `& 3`, which chase mode 258 passes). The update binding is the chase camera's pose seam, closed at
the final FOV (no `chase_transition` dependency); the five native-timing sites are not ported (telemetry only). Facts:
[chase-lead.md](../reverse-engineering/fl/chase-lead.md), section 7. The template's `chase_hud_anchor` now reads
"X3: Albion Prelude and X3: Farnham's Legacy."

| Check | Command | Result (measured) |
| --- | --- | --- |
| FL sites | `python3 verification/probe/verify_chase_lead_sites_fl.py --out verification/results/fl-chase-lead/verify_fl_sites.json` | PASS: 12 windows (823 bytes) FNV-1a with 9 HIGHLOW dwords at their targets (distance 1, texture 6, projection 2), whole instructions; 4 seams exact bytes and Capstone decode, no fixup in any span, no direct edge into any interior (72,230 edges), qword fits/crosses as declared; overlay sole caller `0x0229c2`; pose seam `0x0227ad` dominates `0x0229c2` and `0x022cf5` (1,082-instruction CFG, no unresolved edge); overlay ESP = P at every seam (3,061 instructions, no conflict); 82 field/ABI anchors; header bytes = executable |
| Native fixture | `python3 -I verification/results/fl-chase-lead/run_fixture.py` (`fixture_out.txt`; `verification/probe/fl_chase_lead.cpp`) | `RESULT PASS checks=79 failures=0`: the four thunks for ESP skews 0/4/8/12 with every GPR, EFLAGS incl. DF, ESP, XMM0-7, x87 CW `007f` + one register, MXCSR `bfe0`, LastError intact except ZF (gates) and ECX/EDX (publish); publish `231,-65` from P+0xe0 (decoy at AP's P+0xc0 would give `0,0`), node and O+0x3e0 written; zoom finalization `316,-89`; forward anchor `-232,0` added to all five native positions; failed projection hides through the real 31-byte helper (detacher got ECX = marker, argument = scene), ECX/EDX = -1; HUD refusals (extra entry active, texture missing, 2D off, foreign backpointer) leave the native branch; pose closed after final FOV; 6,584 / 6,584 window bit flips refused before any write (288 in relocated dwords); fault in each marker seam → all three restored; HUD seam fault keeps the marker; unrecoverable fault → `rollback=failed`, inert; flags off: each stub's byte test forwards with the native branch and full CPU state, nothing counted; shutdown and re-install; `late_claim`. `fixture_out.txt` is normalised (addresses, thread ids, µs) and byte-stable across reruns |
| AP unchanged | `python3 -I verification/results/fl-chase-lead/ap_objects_identical.py chase_camera.cpp loader.cpp capture.cpp chase_lead.cpp` (`ap_objects.txt`; `--base` defaults to the parent `f8b255b17`, `--head` to the working tree) | identical AP disassembly against the parent (6,660 / 3,070 / 41,169 / 6,614 lines) |
| Cost (diagnostic) | the fixture's `TIMING` row (`run_fixture.py` prints it; 2,000 calls each, this host, not game FPS) | harness alone 0.13 µs; final-FOV visit of another cockpit with the module on 0.45–0.47 µs (≈0.33 µs over the harness: what each non-chase cockpit of the registry walk pays per update); same visit with the flags off 0.14–0.26 µs; one admitted chase update (pose, gate, publish, HUD gate, final FOV with anchor) 9.4–9.8 µs including four harness calls, once per frame |
| Host modules | `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_chase_lead test_chase_lead_sites test_chase_central_hud_install test_fl_chase_lead test_config_schema` | 47 tests, 0 failing; plus 14 related modules (`test_fl_chase_camera`, `test_chase_camera`, `test_bolt_footprint`, `test_launcher_defaults`, …) 174 tests, 0 failing |
| Builds | `cmake --build build-fl -j`, then `cmake --build build-ap -j` (fresh worktree trees, RelWithDebInfo, mingw-i686) | 0 warnings each; `check_no_x87.py build-ap/d3d9.dll` PASS (1,110 functions); `tools/config/generate.py --check` PASS (292 settings, 137 in the template) |

Expected in game (not verified), with `camera = chase`: `chase_camera requested=1 installed=1 status=active … edition=fl
rva=0x0227ad rollback=none unbound_phase=ap_only dependencies=chase_lead`, then `chase_native_timing installed=0
status=not_ported sites=0 edition=fl reason=telemetry_only`, `chase_central_hud installed=1 status=active
cleanup=native_when_extra_active hud_anchor=forward edition=fl rva=0x02cc1e rollback=none`, `chase_lead installed=1
status=active scope=applied_main_chase_same_sector native_solver=retained telemetry=<0|1> edition=fl base=0x<base>
rva=0x02c7b9,0x02cbe7,0x022cf5 window=- rollback=none update=pose_seam`, and `chase_fire`/`chase_aim_trace` (and
`chase_transition` unless its port lands first) `installed=0 status=ap_only edition=fl`. With telemetry on, every 300
frames: `chase_central_hud_window … admitted>0 … anchor_applied>0 … edition=fl` in the chase view and, with a target
selected in gun range, `chase_lead_window … committed>0 …` plus `chase_lead_sample` rows. Visual: in the chase view the
crosshair and the speed panels appear where the ship points (not at screen centre), the distance panel appears when an
object is selected, the predictive reticle appears on a targeted ship in range and bolts fly to it; zoom keeps the
reticle on target; first-person view unchanged; no `installed=0`, `window_mismatch`, `slot_mismatch` or
`rollback=failed` on these rows.

Open items: once `chase_transition` is on FL (a parallel worktree, same build), `current_update()`/`generation()` in
`chase_lead_fl.cpp` should take the cockpit-lifetime generation and update serial from it as AP does
(`src/proxy/chase_lead.cpp`, `camera_context`/`scope`), which also closes the destroy-and-rebuild-within-one-update gap
of the pose-seam binding; that integration is a follow-up, not in this commit. The path `0x022a0d je 0x022fa9`
(cockpit+0xc == 0 after the HUD calls) skips the final-FOV seam: that pose stays open until the next pose-seam visit,
and a leftover ticket fails at `scope`/`owned_marker`, so nothing is written from it.

## Chase view restore on FL, host evidence (2026-11-02, not installed, not flown)

FL run 24 A: a sector change switched the chase view back to first person and nothing restored it, because
`chase_transition` was AP-only. Now the FL build installs the AP restore in X3FL.exe: six cockpit lifetime/update
witnesses, the script/load mode-store observers, and the input-worker injection plus three VM cancel sites, twelve
spans bound to the module base after 26 identity windows match, each group both or none
([fl/chase-transition.md](../reverse-engineering/fl/chase-transition.md), "Verified 2026-11-02"). The portable state
machine is unchanged and single-sourced. FL native return addresses are normalized to preferred-base VAs (alloc
`0x4305ab`, load `0x420e12`, free `0x430612`). The FL register ABI is local to the handler: ECX cockpit, the EBX
dispatcher frame, ECX as the controlled object. The engine call uses selector 32. The chase camera's FL handler now
hands every visit to `restore_observe_frame` and, as on AP, admits the unbound `+0x10 == 0` phase of a fresh cockpit
on `active_control_cockpit` (the run78 pose gap; previously never admitted on FL). Diagnostics (connect observer, event/identity readers) stay off on FL.
AP object code is unchanged. Base `f8b255b17`. All measured on the WSL2 host (native Windows through interop), no
Wine, no game.

| Check | Command | Result |
| --- | --- | --- |
| FL sites | `python3 verification/probe/verify_chase_transition_sites_fl.py --json` (`verification/results/fl-chase-transition/verify_fl_sites.json`, written by `run_fixture.py`) | PASS: 12 spans (bytes, Capstone lengths, no relative flow, no interior edge in 72,230 direct edges, no relocated pointer into an interior, only the EH HIGHLOW of 38,327), 26 windows, 63 anchors, caller sets, case-`0x30` table entry, EH publication `0x0bed96`, dispatcher publication `0x01df32` |
| Native fixture | `python3 -I verification/results/fl-chase-transition/run_fixture.py` (`fixture_out.txt`; `verification/probe/fl_chase_transition.cpp`) | `RESULT PASS checks=402 failures=0`. CPU contract, 96 runs: 12 thunks × ESP skews 0/4/8/12 × flags+DF set/clear, each against a reference copy of the original span (GPRs, ESP, EFLAGS, XMM0-7, x87 CW/ST0, MXCSR `ffc0`, LastError, pushed words, the stored mode). The injection also ran with a hostile engine call inside, 4 runs. Scenarios through the thunks: gate (restored, `hud_refresh=ok`), dock (restored, `skipped`), undock (stays armed, no call), jump (36 unbound updates, then restored on the new ship), retries (`no_effect cancel=retries attempts=3` after 124 updates), refused call, and six cancels (load, VM clear, VM load, EH epoch, load constructor, foreign frame → `no_monitor`). Install: 52/52 window flips refused with nothing written, unrelocated EH immediate refused, read-back faults roll back per group, `late_claim`. Arena 1,840 of 24,576 bytes; 9 cmpxchg8b + 3 plain |
| Restore core under FL constants | `test_fl_chase_transition.FlRestoreCoreHarness` (the AP harness `chase_transition_restore_host.cpp` compiled with `-DX3M_EDITION_FL=1`, its two selector-0 checks and case 22's load return substituted) | `checks PASS` |
| Mutations | `python3 -I verification/results/fl-chase-transition/mutation_check.py` (`mutation_check.txt`) | 8 / 8 killed: constructor cockpit from `[ESP+4]` (28 failed checks), mode-store frame from EBP (14), controlled object from `[ESP+4]` (14), selector 0 (2), callers not normalized (25), read-back skipped (3), observers kept after a refused restore group (1), windows not checked (50) |
| FL camera, unbound phase | `python3 -I verification/results/fl-chase-camera/run_fixture.py` and `mutation_check.py` (`fixture_out.txt`, `mutation_check.txt` regenerated) | `RESULT PASS checks=44 failures=0`: a `+0x10 == 0` visit without the proof is inactive and asks the proof once; with it the visit is admitted and the pose written (`frames=10 applied=9 refused=1 inactive=2 unbound=1`); 5 / 5 mutations killed (the former `unbound_admitted_on_fl` replaced by `unbound_admitted_without_proof` and `unbound_never_admitted`, 3 failed checks each) |
| AP objects | `python3 verification/results/fl-chase-transition/ap_object_compare.py <build-ap Makefiles tree> <scratch>` (`ap_object_compare.json`) | 4 / 4 identical against `f8b255b17` (`chase_transition.cpp`, `chase_camera.cpp`, `loader.cpp`, `capture.cpp`, debug stripped) |
| Host modules | `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_chase_transition test_chase_transition_sites test_chase_restore_sites test_fl_chase_transition test_config_schema`; also `test_fl_chase_camera test_chase_camera test_launcher_defaults test_chase_native_timing_install test_chase_central_hud_install` | 44 tests, 0 failing; 75 tests, 0 failing |
| Builds | `cmake --build build-fl -j`, then `cmake --build build-ap -j` (fresh worktree trees, RelWithDebInfo, mingw-i686) | 0 warnings each; `check_no_x87.py build-ap/d3d9.dll` PASS (1,110 functions); `tools/config/generate.py --check` PASS (`chase_view_restore` and `chase_view_restore_retries` no longer say "Albion Prelude only") |

Expected in the flight log (not verified): `chase_camera requested=1 installed=1 status=active … edition=fl rva=0x0227ad
rollback=none unbound_phase=active_control dependencies=chase_lead,chase_transition`, then:

```
chase_transition installed=1 status=active edition=fl base=0x<base> window=- window_closed_by=- diagnostics=0 diagnostics_reason=ap_only observers=1 mandatory_sites=6 observer_sites=2 lifetime_capacity=64 thread_capacity=8 timing=<0|1>
chase_transition_site index=0 site=0x<base+020ec0> rva=0x020ec0 patched=1 atomic_write=1 status=active edition=fl   (indices 0-7; atomic_write=0 at 1, 6, 7)
chase_view_restore installed=1 requested=1 status=ok edition=fl path=engine sites=1 cancel_sites=3 inject_site=0x<base+0cfd00> inject_rva=0x0cfd00 call=0x<base+0bb5b0> call_rva=0x0bb5b0 callback=32 method=SelectMode mode=258 refresh=UpdateVisibility refresh_scan_cells=128 observers=1 retries=3 retries_valid=1 expiry_updates=600 due_frames=2 retry_spacing=60
chase_view_restore_site index=0 site=0x<base+0cfd00> rva=0x0cfd00 patched=1 atomic_write=1 status=active edition=fl   (indices 0-3)
chase_fire installed=0 status=ap_only edition=fl reason=ap_only   (and chase_aim_trace; chase_lead and chase_central_hud log their FL rows, section above)
```

After each gate transit, jumpdrive jump and docking taken in the rear chase view, one row:
`chase_view_restore result=restored cancel=none attempts=1 monitor=0x<negative id> … hud_refresh=ok|skipped … mode_seen=258 …`.
Undocking writes no view mode, so no row is expected. If the view is not restored, report the row. Each pattern points
to one cause:

- `result=no_effect cancel=retries`: the store did not happen inside the call.
- `result=cancelled cancel=mode_left` right after a call, with the chase view actually back: the store was deferred
  (the script task yielded and stored 258 after the call returned), so the attempt read as `no_effect` and the next
  candidate frame at 258 cancelled the ticket. The restore landed, but each earlier retry may have left a deferred
  `SelectMode` task behind (at most `retries` + 1 calls per ticket); report it with the rows around it.
- `cancel=no_monitor`: the mode store was not captured.
- `result=call_refused`: the method or the receiver is missing.
- `chase_view_restore_arm_refused reason=…`: the rear frame did not arm.
- `chase_transition installed=0 status=… window=<name>`: a window mismatch.

With `--perf`, `chase_view_restore_state` every 300 frames carries the counters. The former gap (vanilla framing for
~0.4–1.1 s after a gate transit or jump, then a snap to the chase pose) closes: the unbound phase is admitted on the
active-control proof as on AP, so the chase pose should apply from the fresh cockpit's first updates (inferred from AP
Run 78 and the fixture; not flown on FL). `chase_camera … unbound=` in the report rows counts those visits.

## FL run 25 — chase view restore and chase HUD on FL (installed 2026-11-03)

Merges `7e0a02cb8` (chase_lead FL port) and `f8bdad9c8` (chase_transition FL port merged with it), two Opus-high reviews
with no blocker, fixes folded (sections above). Clean `build-fl` from main `f8bdad9c8`, 0 warnings; 17 host modules 206 tests
0 failing; the four FL site verifiers PASS on the installed exe (measured). Installed `C:\X3FL\d3d9.dll` = `413eb49b…`
(55,597,305 B), rollback `rollback-d3d9\d3d9.dll.run24` = `440dbe52…`; overlay and ini unchanged (template defaults:
`camera = chase`, `chase_hud_anchor = forward`, `chase_view_restore = 1`). Record `verification/results/fl-run25/candidate.json`.

**FL run 25 A (flown 2026-11-03, accepted):** user: "chase view restore is okay, tested gate and dock; crosshair is okay".
Log `/home/asvetl/x3m-logs/fl-run25-restore-hud.log` (692 MB, measured): `chase_transition installed=1 status=active
edition=fl … diagnostics=0 diagnostics_reason=ap_only observers=1 mandatory_sites=6 observer_sites=2`, 8
`chase_transition_site` rows, 1,768 `chase_transition_timing` rows; `chase_lead installed=1 status=active
scope=applied_main_chase_same_sector native_solver=retained edition=fl rva=0x02c7b9,0x02cbe7,0x022cf5 update=pose_seam`,
221 `chase_lead_window` and 125 `chase_lead_sample` rows; no `installed=0`, `refused` or `rollback=failed` on the chase
rows. The restore's per-event rows (`chase_transition_event`/`_detail`) are not emitted on FL (diagnostics are AP-only),
so the restore is accepted on the user's report alone; the SelectMode-store assumption therefore holds in practice but
is not witnessed in the log (open: enable the event rows on FL or add one FL counter row). Still AP-only: `chase_fire`
(cursor fire in the chase view), `chase_aim_trace` (its diagnostic).

Original brief: load a game in the chase view; fly through two gates, jump once if you have a jumpdrive, dock and
undock once, select a target and fire at it; one F8 in flight with a target selected; tell me before relaunching. Expect
`chase_transition installed=1 … edition=fl` (three groups), `chase_lead installed=1 … edition=fl` (HUD gate, lead gate,
publish, final FOV), `chase_camera … unbound_phase=active_control dependencies=chase_lead,chase_transition`,
`chase_transition_event … result=restored` after each gate/jump/dock, no `cancel=retries`/`no_effect` (that would mean
the script setter did not land, the one unverified assumption), and `chase_lead` rows with marker/anchor writes. Visual:
the chase view returns by itself after a gate, jump or undock with no second of first-person framing; crosshair, speed
and distance panels, status icon and the lead reticle visible in the chase view and anchored where the ship points;
target brackets as before. Report: any first-person fallback that stays, a HUD element still missing, a reticle that
does not match where bolts go, or a camera jump outside sector changes.

## Chase cursor fire on FL, host evidence (2026-11-03, not installed, not flown)

Run 25's remaining AP-only chase gap was `chase_fire`: right-mouse cursor fire in the chase view aims along the ray from
the chase camera through the cursor. Ported as `src/engine/fl/chase_fire_fl.cpp` + `chase_fire_sites_fl.h` (AP
`src/proxy/chase_fire.cpp` no longer linked into FL; its decision moved into the shared `src/proxy/chase_fire_core.h`
with an identical AP object). One relocated `je` at RVA `0x04ecad` in the native fire function; only the saved ZF
changes, and only for the player's main-group cursor-flag call (`[EBX+0xc]`, `[EBX+0x10]`, ship `[EBP−0x3c]`) with a
fresh applied chase witness from the pose seam, the same cockpit/ship/camera, mode 258, and the real cursor inside
the camera's viewport. Gun, range, cone and the unprojection stay native. `chase_aim_trace` stays AP-only
(`reason=diagnostic_not_ported`): not needed for the first flight (fire row counters plus the visual check). Facts:
[chase-fire-aim.md](../reverse-engineering/fl/chase-fire-aim.md), section 9.

| Check | Command | Result (measured) |
| --- | --- | --- |
| FL sites | `python3 verification/probe/verify_chase_fire_sites_fl.py --out verification/results/fl-chase-fire/verify_fl_sites.json` | PASS: 5 windows (481 bytes) FNV-1a with 11 HIGHLOW dwords at their targets, whole instructions; seam exact bytes and decode, no fixup, no direct edge to its start or interior (72,230 edges), qword crosses; fire function 1,843 instructions, EBX/EBP writers only prologue/epilogue, `[EBP−0x3c]` sole writer `0x04e350`, only `[EBX+8]` stored, four callers, no outside edge into it, no HIGHLOW dword pointing into it, EH FuncInfo `0x19c8b8` (magic `0x19930522`, maxState 1) with no try block; cursor-active HIGHLOW refs exactly `0x04eca8`, `0x07bc49`; 62 anchors; header bytes = executable |
| Native fixture | `python3 -I verification/results/fl-chase-fire/run_fixture.py` (`fixture_out.txt`; `verification/probe/fl_chase_fire.cpp`) | `RESULT PASS checks=89 failures=0`: thunk entered through the native `cmp [cursor active],0` for ESP skews 0/4/8/12, every GPR, EFLAGS incl. DF, ESP, XMM0-7, x87 CW `007f` + one register, MXCSR `bfe0`, LastError intact except ZF (admitted only); no witness / natively active cursor → original branch, no override (the stub's `lock inc` counts the natively active visits: 4; early filters by reason: no witness 5, group 1, flags 2, other ship 1); cursor cells, steering and stack words never written; refusals: flags `0x02`/`0x28`, group 1, other ship in `[EBP−0x3c]` with the player ship as decoy in AP's `[EBX+8]`, view 1/257, connect, flag 4, camera/ref/view-object/registry mismatch, zero active handle, new ship id, stale/future/other-thread witness, cursor off screen/negative/outside viewport, invalid/unreadable screen (counts stale 3, identity 5, view 4, cursor 4, read 2); witness cleared by an unapplied update, a zero stamp and the same cockpit's invalidation, kept by another's; 3,600 / 3,600 cursor positions agree with the fixture's model of the native unprojection's first stage (pixel·65536/dimension inside the normalized bounds; the native code has no in-viewport test, so this checks the handler's reads and arithmetic, not native behaviour; off-centre viewport, both axes); aim model check (production forward projection plus a fixture pinhole inverse, not a test of the native ray): a target 14.4° off boresight in a rotated chase camera, projected through `chase_lead_core::project`, gives cursor `1271,409` and a camera ray 0.08° (≈2 px) from the target; 3,848 / 3,848 window bit flips refused before any write (352 in relocated dwords); read-back fault → `rollback=restored`, unrecoverable → `rollback=failed`, inert; shutdown, re-install, `late_claim` |
| AP unchanged | `python3 -I verification/results/fl-chase-fire/ap_objects_identical.py chase_fire.cpp chase_camera.cpp loader.cpp capture.cpp` (`ap_objects.txt`; base `549aba334`) | identical AP disassembly (1,238 / 6,660 / 3,070 / 41,169 lines) |
| Cost (diagnostic) | the fixture's `TIMING` row (5,000 calls each, this host, not game FPS) | harness alone 0.14 µs; natively active cursor 0.13 µs (the stub's byte tests and one `lock inc`); non-player call 0.33 µs; admitted call 0.51 µs (≈0.37 µs over the harness: lock, QPC, registry walk, fields); per fire call, no per-draw work |
| Host modules | `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_chase_fire_site test_fl_chase_fire test_fl_chase_camera test_fl_chase_lead test_config_schema` | 40 tests, 0 failing; plus `test_bolt_footprint`, `test_game_phase_sites`, `test_chase_aim_sites` 57 tests, 0 failing |
| Builds | `cmake --build build-fl -j --clean-first`, then `cmake --build build-ap -j --clean-first` (worktree trees, RelWithDebInfo, mingw-i686) | 0 warnings each; `check_no_x87.py build-ap/d3d9.dll` PASS (1,110 functions); `tools/config/generate.py --check` PASS (292 settings, 137 in the template) |

Expected in game (not verified), with `camera = chase`: `chase_camera requested=1 installed=1 status=active … edition=fl
rva=0x0227ad rollback=none unbound_phase=active_control dependencies=chase_lead,chase_fire,chase_transition`, the
`chase_lead`/`chase_central_hud`/`chase_transition` rows as in run 25, then `chase_fire installed=1 status=active
scope=applied_chase_258_player_main_cursor lifetime=process telemetry_required=0 native_input_writes=0 edition=fl
base=0x<base> rva=0x04ecad window=- rollback=none` and `chase_aim_trace installed=0 status=ap_only edition=fl
reason=diagnostic_not_ported`. With telemetry on, every 300 frames: `chase_fire_window frame=… native_inactive_override=<n>
refused_stale=… refused_identity=… refused_view=… refused_cursor=… refused_read=… … native_active=<n> early_no_witness=<n>
early_group=<n> early_flags=<n> early_other_ship=<n> early_read=<n> edition=fl`, with `native_inactive_override > 0` in
windows where you fired with the right mouse in the chase view. Reading the fire row: `native_active > 0` with `native_inactive_override = 0` means FL already treats the cursor as active in the chase view (the native cursor ray runs without the hook); `early_no_witness`, `early_other_ship` or the `refused_*` counts rising instead mean the call reached the seam but was never admitted (no applied chase witness, another ship, or a stale/identity/view/cursor refusal). No `installed=0`,
`window_mismatch`, `slot_mismatch` or `rollback=failed` on the `chase_fire` row.

User flight check: in the chase view, put the mouse cursor on a target away from the screen centre and fire with the
right mouse; the bolts should go to the cursor (toward the target), not straight ahead. Also fire with the right mouse
in first-person view (unchanged) and once with the normal fire key in the chase view (straight ahead, unchanged).
Report where the bolts went; if they do not follow the cursor, the `chase_fire_window` rows of that stretch tell which
case it was (reading above). Bolts come from the guns, not the camera, so a target very close behind the
cursor shows a small offset (the camera-to-gun parallax, as on AP).

Open items: when the active cell is 0 the cursor x/y cells may be stale or meaningless (the cursor is hidden during
mouse steering); (0,0) lies inside a full viewport, so an admitted shot could go toward the top-left corner. Same
exposure as AP; the flight check covers it (report any bolts that go to a screen corner). The witness is bound by the
two-second QPC window, not by `chase_transition`'s cockpit-lifetime generation (as on AP). `check_no_x87.py` cannot
check the FL DLL (it roots at the AP-only `gz_read`).

## FL run 26 — chase cursor fire on FL (installed 2026-11-03)

Commit `b3b0e30c0` (fast-forward), Opus-high review with no blocker, fixes folded (section above); the transition wiring
test accepts the fire report line (`b4ea9ff1d`). Clean `build-fl` from main `b4ea9ff1d`, 0 warnings; 10 host modules 147 tests
0 failing; `verify_chase_fire_sites_fl.py` PASS on the installed exe (measured). Installed `C:\X3FL\d3d9.dll` =
`3851f91d…` (55,621,875 B), rollback `rollback-d3d9\d3d9.dll.run25` = `413eb49b…`; overlay and ini unchanged. Record
`verification/results/fl-run26/candidate.json`.

**FL run 26 A (open):** in the chase view, select a target well off-centre, hold the right mouse button with the cursor on it
and fire a few bursts; also fire once with the cursor hidden (mouse steering) to probe the (0,0) exposure; one F8 while
firing; tell me before relaunching. Expect `chase_fire installed=1 … edition=fl`, the fire row's `native_inactive_override`
rising while firing at the cursor (or `native_active > 0` with override 0 if FL already aims at the cursor natively),
`early_*`/`refused_*` not rising during those bursts, `chase_camera … dependencies=chase_lead,chase_fire,chase_transition`.
Visual: bolts go to the cursor, not straight ahead; with the cursor hidden nothing aims at the top-left corner. Report a
shot that goes somewhere other than the cursor or the boresight.
