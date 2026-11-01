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
