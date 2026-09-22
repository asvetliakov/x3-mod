# Cleanup inventory: obsolete, rejected and never-flown options (2026-09-22)

Read-only inventory answering the user's question "can unused or rejected
experimental flags be cleaned up". Nothing is deleted here. Enumerated: every
`add_argument('--…')` in `tools/manage.py` (**184** options) and every
`X3M_*` name read or documented in `src/` (**199**; 172 mirror a launcher
option, 27 are source-only).

Flown baseline used for classification: the two **Run 62** commands in
`docs/verification/user-runs.md` (queued 2026-09-22; the queue advanced from
Run 61 to Run 62 while this inventory was being written). Those commands carry
**51** of the 184 options; **133** are not in them.

Classes: **FLOWN** (in the queued commands), **DEFAULT-ON** (behaviour needs no
flag), **DIAGNOSTIC** (capture/log/telemetry tooling still useful),
**EXPERIMENT-OPEN** (awaiting a verdict), **HELD/PAUSED** (owner paused it;
keep), **SUPERSEDED** (named accepted successor), **REJECTED/RETIRED** (user or
ledger rejected it), **DEAD** (no reachable behaviour at all), **UNSURE**.

Line counts are `wc -l` over the files listed; object sizes come from the
existing scratch debug build at
`/private/tmp/claude-501/-Users-asvetl-x3-mod/e4819d6e-52de-4adf-9383-1583146b5cc2/scratchpad/main-audit`
(`d3d9.dll` 55,174,409 B). That build carries full debug info, so its `.obj`
sizes are an ordering hint, **not** a linked DLL delta; no link map exists and
nothing was rebuilt for this inventory. Every source file in the inventory is in
the unconditional `CMakeLists.txt` source list (lines 85–87): the code below is
compiled into the DLL whether or not its option is ever passed.

## 1. Summary: removal candidates

| # | Item | Class | Evidence | Removable lines | Risk |
|---|---|---|---|---|---|
| A1 | `--effect-source-gain` | DEAD, removed 2026-09-22 | `tools/manage.py:399` `argparse.SUPPRESS`, comment "removed 2026-09-16" | ~10 (launcher) | low |
| A2 | `--sun-shadow-receiver-depth` | DEAD, removed 2026-09-22 | `tools/manage.py:371` "Deprecated no-op"; goals #8 (linear is the only encoding) | ~12 (launcher) | low |
| B | `--d3dx`, `--fex-tso`, `--wined3d` | REJECTED (closed experiments), removed 2026-09-22 | goals.md #14: run 36 closed D3DX (builtin +32 %), run 37 closed FEX TSO and wined3d CSMT | ~70 (launcher only) | low |
| C | GTAO/SSAO chain: `--ambient-occlusion`, `--ao-radius`, `--ao-strength`, `--ao-debug`, `--ao-timing`, hotkey Ctrl+Shift+F11 | REJECTED | goals.md #7 "Closed … the user closed it on 2026-09-15"; user-runs rows 19/21 (invisible at radius 100) | src 1,212 + tests 385 + probe 1,313 + 5 result manifests ≈ **2,910** | medium |
| D | `--lod-scale` | REJECTED (closed) | goals.md cross-cutting: "`--lod-scale` is closed default-off"; `docs/architecture/lod-scale.md` | src 248 + probe 111 + tests 193 = **552** | low‑medium |
| E1 | `--taa-current-filter` | REJECTED | `taa-flicker-suppression.md:184` "the global form was flown and rejected"; `taa-lattice-crawl.md:138` "filter A = 1 everywhere (rejected by the user)" | see E‑total | medium |
| E2 | `--taa-line-filter` | SUPERSEDED by `--taa-thin-region` + camera gate | user-runs row 46 B (run157–159) "filter engaged, beads ×0.32, but the user sees no change"; `taa-lattice-crawl.md:357` "keep … as an option, not a default"; `:1819` the camera gate silently forces `screen` when the line filter is on | see E‑total | medium |
| E3 | `--taa-adaptive-weight` | REJECTED | user-runs row 45 B (run153/154) "adaptive weight does not fix the lattice crawl … **rejected**, stays default-off" | see E‑total | medium |
| E4 | `--taa-thin-clip` | SUPERSEDED by `--taa-thin-region` / `--taa-far-stabiliser` | `taa-lattice-crawl.md:603` "refused beside the far stabiliser or the thin region, deliberately"; both successors are flown in Run 62 | E‑total src **2,679** (12 `*_inc.h` + 6 HLSL) + 7 result manifests | medium |
| F1 | Owned media playback probe/fixture residue | RETIRED, removed 2026-09-22 | `docs/verification/media-cues.md:1842` "ID2 video omission and owned playback retirement"; 31 production files already removed | probe **9,744** (34 unreferenced files) + `verification/historical/owned_media` 601 | low |
| F2 | `test_media_connected.py`, `test_media_worker_clock_fixture.py`, `test_media_shared_transport_evidence.py` | RETIRED, removed 2026-09-22 | same ledger; they test the removed owned runtime's evidence parsers | **895** | low |
| F3 | `tools/media_transcode.py`, `tools/prepare_media_package.py` (+ `test_media_transcode.py` 248) | RETIRED, removed 2026-09-22 | same ledger ("Launch no longer requires old LAV payloads"); H.264 cue transcoding belonged to replacement playback | 618 + 248 = **866** | low |
| G1 | 23 retired host test modules (`retired_tests.py`) | RETIRED | `docs/verification/host-suite.md` retired table; `--linear-materials` and the `--linear-emissions` bracket | **8,131** | low (already hidden) |
| G2 | Retired-only linear-material/exposure probe assets (11 files) | RETIRED | referenced only by G1 modules | **~4,100** of 7,640 (see §3 G2) | medium |
| H | `--linear-materials` and dependents (`--material-fill`, `--material-direct-gain`, `--material-emissive-gain`, `--lightmap-emissive-gain`, `--linear-distance-fade`/`--no-linear-distance-fade`) production code | RETIRED subject, **entangled** | host-suite retired table; goals #3/#6 "the user plays original hulls" | ~900–1,000 inside `linear_material.cpp` only | **high** — see §3 H |
| I | `--linear-emissions` full-surface bracket production code | REJECTED | goals.md cross-cutting: "`--linear-emissions` in its full-surface bracket shape rejected 2026-09-15" | ~0 cleanly; `linear_emission_pass.cpp` (1,211) + `linear_emission.cpp` (509) are load-bearing for the flown SM1/source-gain path | **high** |
| J | `--volumetric-fog-anisotropy` | SUPERSEDED (inert), removed 2026-09-22 | its own help: "no effect under `--volumetric-fog-look 1-3`"; look default is 2 and Run 61 B accepted the looks with **L2 preferred** | ~15 (launcher + one constant path) | low |

Clean, low-risk total (A+B+C+D+F+G1): **≈ 15,300 lines**, of which only
≈ 1,460 are production `src/` lines (AO chain + lod\_scale). Adding E (dead TAA
variants) brings production removal to ≈ 4,140 lines. H and I are not
recommended without a deliberate refactor.

## 2. Full classification

### FLOWN (51, Run 62 commands)
`--direct --camera --chase-view-restore --ownership --object-trace
--object-lifetime --motion-output --taa --telemetry --camera-log --hdr
--hdr-tonemap --hdr-exposure --hdr-bloom --bloom-source-clamp --crypt-cache
--gz-buffer --resource-read --dat-handles --mesh-adjacency --voice-decoder
--screen-emission-additive --screen-emission-additive-alpha
--emission-source-gain --loading-intervals --sun-shadow-lane
--shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply
--shadow-sun-poll --fps-overlay --shadow-cascades
--shadow-cascade-drop-order --shadow-cascade-records --shadow-cascade-sizes
--shadow-retention-census --shadow-caster-retention
--shadow-cascade-adaptive-c0 --taa-far-stabiliser --taa-thin-region
--taa-sentinel-stabiliser --light-map-far-fade --motion-rt-mode
--frame-end-stride --taa-debug --capture-start --capture-frames
--volumetric-fog --volumetric-fog-cards --volumetric-fog-range
--volumetric-fog-timing` plus the env prefix `X3M_MOTION_FRAME_LOG=1` and
`X3M_FIXTURE_BOTTLE=X3`.

### DEFAULT-ON / opt-out arms (keep; each is a documented bit-identical rollback)
`--taa-thin-region-gate` (default `camera`, Run 59; `screen` opts out),
`--taa-unmatched-static` (default `node`, run212; `off` opts out),
`--media-cue-cache` / `--media-cue-retry-s` (default on since run 34),
`--motion-rt-mode perdraw` (A/B for the accepted `lazy`),
`--scene-hook off`, `--state-shadow on|off`, `--no-collide-memo`,
`--no-collide-sat-sse2`, `--no-light-map-far-fade`, `--motion-jitter`
(`--taa` implies it, `motion-output.md:735`), `--cull-small-parts` (launcher
default 2), `--cull-small-parts-scope`, `--taa-mip-bias`, `--taa-sharpen`,
`--taa-history-weight`, `--taa-k`, `--taa-sentinel`, `--camera-cut-deg`,
`--hull-lightmap-gain`, `--hull-emitters`, `--hull-emission-gain`,
`--original-fill`, `--screen-emission`, `--screen-emission-gain`, the chase
defaults (`--chase-pitch-down-deg`, `--chase-offset-y`, `--chase-hud-anchor`,
`--chase-distance-scale`, `--chase-combat-tightness`, `--chase-*-tau`,
`--chase-*-clamp*`), the HDR meter/exposure knobs (`--hdr-ev*`, `--hdr-look`,
`--hdr-clamp`, `--hdr-decode`, `--hdr-edge-weight`, `--hdr-key-pull`,
`--hdr-meter-bg`, `--hdr-white-target`), the shadow tuning knobs
(`--shadow-cascade-{budget,caps,ladder-ratio,large-min,static-from,backface-from}`,
`--shadow-caster-retention-{age,eps}`, `--shadow-replay-{cap,extent,size,depth-half}`,
`--sun-shadow-bias-*`), `--gz-buffer-kb`, `--bottle`, `--game-dir`,
`--dll-source`, `--dry-run`, `--vanilla`.

### DIAGNOSTIC (keep)
`--frame-timing --frame-timing-state-stamps --frame-phases --pass-phases
--residual-phases --light-phases --submit-phases --loop-phases --game-phases
--game-phase-threshold-ms --telemetry-draw --audio-sites --media-cue-trace
--loading-probes --profile --profile-interval-us --profile-raw
--shadow-sun-trace --shadow-retention-timing --cull-census
--collide-narrow-census --collide-query-phases --collide-memo-verify
--mesh-adjacency-dump --mesh-cache --depth-copy --scene-depth-capture
--sector-background --screen-emission-timing --fade-witness --shimmer-trace
--motion-capture --finite-positions --volumetric-fog-everywhere
--volumetric-fog-timing` (`--lattice-state` removed 2026-09-22, batch 7).
Two are stale but harmless: `--shimmer-trace` is documented against the retired
fade-region note, and `--motion-capture` / `--finite-positions` predate the
accepted same-draw motion route.

### EXPERIMENT-OPEN
`--volumetric-fog-range stored` (implemented in four checkpoints, first flight
Run 61 B: "looks accepted, L2 preferred, ribs from axis-aligned coverage
waves"), `--volumetric-fog-look` (L0–L3, same run; **retired in batch 8**, 2026-09-22), `--taa-sentinel-stabiliser`
(Run 61 A traced the residual distant-station pan flicker to far-plane history
rejection, fixed for Run 62), `--chase-scene-fix` (waits for a run showing an
external-view HUD element).

### HELD / PAUSED (keep, do not delete)
- Collision: `--collide-box-cull`, `--collide-memo`, `--collide-sat-sse2`,
  `--collide-narrow-census` — status.md "Collision: paused by user request".
- Lattice upload/geometry diagnostic: **removed 2026-09-22**, see batch 7 below.
  The ownership `clone_upload*` / `portable_managed_upload.cpp` /
  `surface_lease_core.h` part is still in the tree; the inventory's attribution of
  `X3M_ADMISSION` to this chain was wrong (see batch 7).

### SUPERSEDED / REJECTED / RETIRED / DEAD
Items A–J of §1: `--effect-source-gain`, `--sun-shadow-receiver-depth`,
`--d3dx`, `--fex-tso`, `--wined3d`, `--ambient-occlusion` + `--ao-*`,
`--lod-scale`, `--taa-current-filter`, `--taa-line-filter`,
`--taa-adaptive-weight`, `--taa-thin-clip`, `--linear-materials` +
`--material-fill` + `--material-direct-gain` + `--material-emissive-gain` +
`--lightmap-emissive-gain` + `--linear-distance-fade` +
`--no-linear-distance-fade`, `--linear-emissions` + `--emission-gain`,
`--volumetric-fog-anisotropy`.

### UNSURE
- `--point-light-root-admission` (453 src lines, 267 test lines): flown once in
  run68 with a negative observation ("docking modules partly dark with the
  point-light option"), then goals #6 scheduled it default-off after the replay
  fixture; no verdict since 2026-09-16. **Question for the user: is the
  root-object point-light admission still wanted, or closed?**
- `--taa-alpha-history`: flown once inside Run 45 B's command; the ledger
  records a verdict for adaptive weight but none for alpha history. It is the
  bloom authored-glow weight on the HDR route
  (`docs/architecture/bloom-authored-glow.md:112`). **Question: was the bloom
  pulse ever judged?**
- `X3M_ENGINE_READS=rpm`: **removed 2026-09-22** (batch 7); validated direct
  reads are the only mode.

### Source-only `X3M_*` (27)
Fixture fault injection, keep with the fixtures: `X3M_FIXTURE_AO_FAULT`,
`X3M_FIXTURE_FADE_RECT`, `X3M_FIXTURE_MOTION_DEPTH`, `X3M_FIXTURE_QUAD_FVF`,
`X3M_FIXTURE_SCREEN_CAPS_FAULT`, `X3M_FIXTURE_SCREEN_RECT`,
`X3M_FIXTURE_SHADOW_CASCADES`, `X3M_FIXTURE_SHADOW_EXTENT`,
`X3M_FIXTURE_SLICE_NEAR`, `X3M_FIXTURE_STRETCH_FAULT`,
`X3M_FIXTURE_SUN_LANE_FAULT` (`X3M_FIXTURE_AO_FAULT` goes with batch C).
Tuning knobs, keep: `X3M_HDR_ADAPT_DOWN/UP`, `X3M_HDR_DT_MS`,
`X3M_HDR_EV_OFFSET`, `X3M_HDR_KEY`, `X3M_HDR_METER_MIN_LIT`,
`X3M_CHASE_MAX_DT`, `X3M_MOTION_JITTER_SAMPLES`, `X3M_PROFILE_REPORT_S`,
`X3M_FOG_LOOK_AMBIENT_SUN/AWAY`. Diagnostics: `X3M_LOCKED_PREFIX_LOG`,
`X3M_MOTION_FRAME_LOG` (FLOWN, in the run commands). A/B fallback:
`X3M_ENGINE_READS` (removed 2026-09-22). Ownership application-admission
monitor: `X3M_ADMISSION` (kept; it is not part of the removed lattice chain).
Retired subject:
`X3M_FADE_ROUTE` — but see the shared-code note below, it also gates the flown
screen-emission arm.

## 3. Per-item detail

### C — GTAO/SSAO chain
Files only this feature uses: `src/renderer/ambient_occlusion_pass.{cpp,h}`
(498), `ambient_occlusion_{gtao,blur,apply,linearize}_program_inc.h` (~500),
`src/temporal/ao_{gtao,blur,apply,linearize}_ps.hlsl` (~214); tests
`verification/analysis/test_ambient*.py` (385); probe
`ambient_occlusion_fixture.cpp`, `ambient_occlusion_reference.h`,
`build_ambient_occlusion.sh`, `run_ambient_occlusion.py`,
`run_ambient_occlusion_live.py` (1,313); result manifests
`ambient-occlusion-{apply,blur,gtao,linearize}-program.json` plus
`ambient-occlusion-gpu1.*`. Debug object size 147,610 B.
**Shared code that must stay:** `src/renderer/ambient_occlusion_caps.h` (62) —
`ambient_occlusion_program_slots()` is the ps\_3\_0 slot counter reused by
`fog_pass.cpp` (4 call sites) and `sun_shadow_apply_pass.cpp` (2). Rename it out
of the AO namespace rather than deleting it.
Wiring to edit: `motion_output.{cpp,h}`, `capture.cpp`,
`comparison_controls.h` (Ctrl+Shift+F11), `tools/manage.py`,
`tools/shaders/generate_rigid_motion_pixel.py`.
Risk **medium**: the removal touches the scene-end pass order in
`motion_output.cpp`, which is the flown TAA/fog/shadow composition point.

### D — `--lod-scale`
`src/proxy/lod_scale.{cpp,h}` + `lod_scale_core.h` (248, object 41,491 B),
`verification/probe/verify_lod_scale_site.py` (111), 2 test modules (193).
Touches `loader.cpp` and `capture.cpp`. `src/proxy/cull_small_parts.h:20`
references lod\_scale's patch site `0x0047d44b` in a comment only ("all three
coexist"), so there is no compile dependency; `--cull-small-parts` (a launcher
default) is unaffected. Risk **low‑medium**, only because the loader installs
both trampolines from the same site-validation path.

### E — dead TAA resolve variants
Removable generated includes: `temporal_resolve_filter`, `_line`,
`_thin_filter`, `_thin_line`, `_age`, `_age_filter`, `_age_line` (7 files,
2,234 lines) and their HLSL sources `resolve_filter.hlsl`, `resolve_line.hlsl`,
`resolve_thin_filter.hlsl`, `resolve_thin_line.hlsl`, `resolve_age.hlsl`,
`resolve_age_filter.hlsl`, `resolve_age_line.hlsl`; 7 result manifests
`temporal-resolve-{filter,line,thin-filter,thin-line,age,age-filter,age-line}-program.json`.
**Shared code that must stay:** `temporal_line_mask_program_inc.h` (202) and
`src/temporal/line_mask_ps.hlsl` — `temporal_pass.cpp:244` creates the screen
line mask for the **far stabiliser** as well as for the line filter, and
`--taa-thin-region-gate screen` is the documented bit-identical opt-out. Also
keep `temporal_resolve_far{,_camera}`, `temporal_resolve_thin`,
`temporal_thin_box{,_rows,_columns}`, `temporal_line_mask_camera`,
`temporal_resolve_snapshot` (the reactive mask the route uses).
Wiring: `temporal_resolve_program.h`, `temporal_pass.{h,cpp}` (variant
selection, `line_masks_failed_` fallback, age R32F pair), `motion_output.{cpp,h}`,
`capture.cpp`, `tools/manage.py` (four options plus their mutual-refusal
`parser.error` lines), `verification/probe/run_temporal_pass.py`,
`verification/analysis/test_taa_image_defaults.py`.
Risk **medium**: `temporal_pass.cpp` is the flown resolve; the age R32F target
pair also feeds `--taa-far-stabiliser` (`taa-distant-line-fade.md:85`: "Reuses
the age pair of `--taa-adaptive-weight`"), so the **targets** stay even though
the option goes.

### F — media residue
No production code left (31 files already removed). Removable: 34 unreferenced
`verification/probe/media_{owned,destination,startup,connected,worker,presentation,root,services,engine_adapter,clock,lav}*`
and `build_media_*` / `run_media_*` files (9,744 lines),
`verification/historical/owned_media` (601), `tools/media_transcode.py` +
`tools/prepare_media_package.py` (618) and the four test modules that only
exercise them (895 + 248).
**Keep:** `verification/probe/media_startup_loader_fixture.cpp` (used by
`test_loader_factory.py`), `media_cue_host.cpp`, `media_cue_skip_fixture_inc.h`,
`verify_media_cue_site.py`, `media_package_config_*`, and all of
`tools/media_package.py` — it is the install/uninstall/rollback transaction used
by `manage.py` (`MODULES`/`MANIFESTS` still describe retained LAV rollback
payloads, per the ledger's "without deleting its rollback payloads").
Risk **low** (no production code, no flown fixture).

### G — retired linear-material tests and probes
G1: the 23 modules in `verification/analysis/retired_tests.py` (8,131 lines) are
already hidden from default discovery and pass under `--include-retired`.
Deleting them is a documentation decision, not a behaviour change; risk **low**.
G2: probe assets referenced **only** by those modules:
`linear_material_live_fixture.cpp` (1,478), `run_linear_material_live.py` (648),
`run_linear_distance_fade.py` (461), `run_linear_distance_fade_live.py` (1,585,
unreferenced), `linear_material_structure.cpp` (353),
`linear_distance_fade_structure.cpp` (85),
`linear_distance_fade_composite_inc.h` (79), `material_exposure_structure.cpp`
(94), `material_exposure_probe.cpp` (32), `material_exposure_reference.py` (31),
`linear_emission_structure.cpp` (155) — **5,001**; of these
`run_linear_distance_fade_live.py` and the two `*_live*` files are the GPU
runners, so a conservative first pass removes ≈ 4,100 and leaves the live
runners for the owner to confirm.
**Shared harness that must stay** (a *kept* test, `test_linear_emission_hull_gain`
for the flown `--hull-emitters`, pulls all of these in):
`linear_material_fixture.cpp` (1,579), `run_linear_material.py` (1,800),
`linear_material_reference.py` (888), `linear_distance_fade_fixture_inc.h`
(1,556), `linear_alpha_test_fixture_inc.h` (256),
`linear_xt_fixture_reference.py` (222), `linear_glass_fixture_reference.py`
(101), `sun_share_material_inc.h` (70). Risk of touching them: **high**.

### H — retired `--linear-materials` production code (not recommended)
`src/renderer/linear_material.cpp` is 1,982 lines and hosts **both** subjects.
Retired: the converted-material transformer and its site proofs
(`glass_color_sites`, `pixel_sites`, `palette_sites/definitions`, `transform`,
`linear_material_{vertex,pixel}_variant*`, `linear_distance_fade_*`),
roughly lines 550–1,550. Flown and living in the same file: `--original-fill`
(`original_fill_transform`, `linear_material_original_fill_pixel_variant`),
`--hull-lightmap-gain` (`lightmap_gain_*`, `lightmap_term`) and the
original-program sun-share lane (`original_sun_plan`,
`linear_material_original_sun_share_pixel_variant`) used by
`--sun-shadow-lane`, which is in every Run 62 command. All of them reuse the
same private shader walker (`structure`, `body_shape`, `sanitize`, `transfer`,
`constant_uses`, `reads_rgb`/`writes_rgb`, `motion_insertions`) and the same
`linear_material_shader_tables()` registration in `shader_population.cpp`.
Byte-exact tables other features reuse and that must stay:
`linear_sun_share_inc.h` (198), `src/proxy/sun_share_lane_inc.h` (308),
`linear_xt_material_inc.h` (334, also drives `test_xt_material_transformer` /
`test_xt_pixel_execution`), `motion_output_profiles_inc.h` (1,070 — the
same-draw motion profiles, the core flown TAA input),
`pixel_coverage_profiles_inc.h` (497) and `position_path_profiles_inc.h`.
Estimated cleanly removable: **900–1,000 lines inside one file**, requiring a
split of the shared walker into its own header plus regeneration of the retired
profile tables' provenance. Risk **high**, benefit low relative to effort.
Debug object size for reference: `linear_material.cpp.obj` 1,177,151 B — the
fourth largest in the build.

### I — `--linear-emissions` bracket (not recommended)
`linear_emission_pass.cpp` (1,211) and `linear_emission.cpp` (509) carry the
rejected full-surface bracket, but the host-suite table keeps
`test_linear_emission_pass_programs` and `test_linear_emission_pass_host`
precisely because `linear_emission_copy_clear_inc.h`'s transfer arrays are
compiled into the DLL and the pass still compiles against the public host D3D
interfaces; `linear_emission_pass_host.cpp` is shared with the flown
`--emission-source-gain` and `--screen-emission` tests. `fade_region*` +
`fade_route_core.h` (980, `X3M_FADE_ROUTE`) look like fade-only code but serve
the **flown** `--screen-emission-additive` region bracket as well. Leave I
alone; it is the clearest case where "the flag is rejected" does not mean "the
code is dead".

### J — `--volumetric-fog-anisotropy`
Inert whenever `--volumetric-fog-look` is 1–3, and the look default is 2 with
L2 the user's stated preference after Run 61 B. Keeping the option costs one
launcher block and one Henyey-Greenstein constant path in `fog_pass`; removing
it changes the L0 legacy look. Low value either way — listed for completeness.

## 4. Proposed removal order

Each batch is independent and should leave the host suite
(`/usr/bin/python3 verification/probe/run_host_suite.py`) and the affected
fixtures green on its own.

1. **Batch 1 — dead launcher surface** (A1, A2, B, J) — **done 2026-09-22.**
   Six options gone from `tools/manage.py` (`--effect-source-gain`,
   `--sun-shadow-receiver-depth`, `--d3dx`, `--fex-tso`, `--wined3d`,
   `--volumetric-fog-anisotropy`) with their refusals, the `FEX_TSOENABLED` /
   `WINE_D3D_CONFIG` experiment block, the `d3dx9_37=b` override arm, the
   `d3dx=/fex_tso=/wined3d=` header and dry-run JSON fields, and the
   `X3M_VOLUMETRIC_FOG_ANISOTROPY` forward (the DLL keeps its own g = 0.3
   default, so a modded launch is unchanged). Removed with them:
   `test_d3dx_override_launch.py` and `test_env_experiment_launch.py`, whose
   three `launch_teed` header tests moved into `test_launcher_stderr_tee.py`.
   **361 lines deleted, 70 re-added (net 291)**; each removed option now fails
   with argparse's `unrecognized arguments`.
2. **Batch 2 — media residue** (F1, F2, F3) — **done 2026-09-22.**
   49 files, **9,256 lines**: 33 probe files, `verification/historical/owned_media`
   (12 files), `tools/media_transcode.py`, `tools/prepare_media_package.py` and
   the four test modules. **Kept against the inventory's list:**
   `verification/probe/media_lav_evidence.py` and `run_media_playback_fixture.py`
   are imported by `run_lav_packet_fixture.py`, which stays. Two tests in the
   kept `test_media_package.py` used the deleted legacy tool: the legacy-refusal
   test went with it and the native `tasklist` guard test was rewritten without
   it. Host suite after both batches: **229 modules / 2,259 tests green**,
   `--include-retired` 252 / 2,635 green.
3. **Batch 3 — retired host tests** (G1, G2 conservative pass) — **done
   2026-09-22.** 28 files, **8,817 lines**: the 23 modules and
   `retired_tests.py` (8,183) and four probe assets (634):
   `linear_material_structure.cpp`, `linear_emission_structure.cpp`,
   `material_exposure_structure.cpp`, `material_exposure_probe.cpp`. **Kept
   against the inventory's list:** the three GPU/live runners, for the owner
   to confirm; `run_linear_distance_fade.py` (imported by the kept
   `run_linear_distance_fade_live.py`); `linear_distance_fade_structure.cpp` and
   the probe copy of `linear_distance_fade_composite_inc.h` (built by
   `build_linear_distance_fade.sh`); `material_exposure_reference.py` (imported
   by the kept `test_xt_material_reference.py`). Two kept files depended on
   deleted ones: `test_linear_emission_sm1_transformer.py` now carries its own
   `f32`, and `run_linear_emission.py` no longer hashes the six deleted report
   modules. `run_host_suite.py --include-retired` stays as a no-op; the 23
   stale duration hints are gone. Host suite before and after: **233 modules /
   2,311 tests green**; the seven harness-sharing modules 68 tests green.
4. **Batch 4 — `--lod-scale`** (D). Check: `test_cull_small_parts*`,
   `verify_lod_scale_site.py` removal, one dry-run. ~550 lines.
5. **Batch 5 — GTAO/SSAO chain** (C) — **done 2026-09-22.**
   24 files deleted: the pass, its four programs (HLSL and `*_inc.h`), the four
   host test modules, the five probe files, the four program provenance JSONs and
   `bottle-X3/ambient-occlusion-gpu1.txt`. Unwired: `--ambient-occlusion`,
   `--ao-radius`, `--ao-strength`, `--ao-debug`, `--ao-timing` with their refusals
   and `X3M_AMBIENT_OCCLUSION` / `X3M_AO_*` forwards (each option now fails with
   argparse's `unrecognized arguments`), the DLL's AO environment reads and
   `ambient_occlusion_mode` line, the Ctrl+Shift+F11 key and its fixture export,
   the scene-end AO step at the hook and at the bloom-copy fallback with its
   state, Reset and shutdown handling, and the fixture `aohook` script. The
   scene-end order is otherwise unchanged: lane publication, replay candidates,
   sun-shadow apply, fog, then the resolve (FP16 resolve and write-back, or the
   8-bit resolve on RT0). The slot counter
   is now `ps3_program_slots()` in `src/renderer/ps3_program_slots.h` (fog,
   sun-shadow-apply and sun-occlusion passes); the AO capability gate went with
   the pass. The shared projection constants are `projection_default_m22/m32`.
   **Kept against the inventory's list:** `bottle-X3/ambient-occlusion-gpu1.json`,
   cited by the AO ledger (`docs/verification/ambient-occlusion.md`).
   **3,849 lines deleted, 132 re-added (net 3,717)**, 74 files outside `docs/`, including a
   one-line tool-source digest refresh in the 19 bloom and stored-fog program
   records that hash `generate_rigid_motion_pixel.py` (bytecode unchanged).
   Wine fixtures run 2026-09-23 on main 775dd59f, all PASS: run_temporal_pass.py (570 numerical checks, adjacent_max 0.000, pans strict == loose), fog_density_shader_run.py check 28/28 gates, run_motion_output.py sun-shadow-apply cases (checks 175 identical) and the full motion-output suite (190 cases PASS); results under verification/results/bottle-X3/.
6. **Batch 6 — dead TAA resolve variants** (E1–E4), keeping the screen line
   mask, the age R32F target pair and every far/thin/box program. Check:
   `run_temporal_pass.py` gates, `test_taa_image_defaults.py`,
   `test_temporal_*`, and a byte-comparison that the flown
   `--taa-far-stabiliser 0.985 --taa-thin-region 0.97` resolve program hashes
   are unchanged. ~2,700 lines. Do this one last of the code batches: it is the
   only batch that edits the flown resolve. **Exit reset dependency (2026-09-22,
   `seta-sky-hull-share-decay.md`):** the fixture's `SETA_EXIT` rows run on the
   `age` program (thin clip 0.7, WMAX 0.9; the off / slow / loose rows and the
   adaptive on rows) as well as on `far` and `far_camera`, so batch 6 must
   either keep one age variant (`resolve_age.hlsl` and its include) or move the
   age-program rows to far / far_camera first (the runner's counts 672 / 488
   change); and its "flown hashes unchanged" gate must be rebased onto the
   hashes that change records (`temporal-resolve-far{,-camera}-program.json`:
   the term is compiled into every age-writing variant), not the pre-exit ones.
7. **Not scheduled** — H (retired converted-material law inside
   `linear_material.cpp`) and I (`--linear-emissions` bracket). Both need a
   deliberate split of shared code first; propose separately if the user wants
   the DLL smaller.
8. **Verdicts received 2026-09-22** — `--point-light-root-admission` and
   `--taa-alpha-history` are **kept**; `X3M_ENGINE_READS=rpm` and the lattice
   state/geometry/upload-hook diagnostic are **removed** (batch 7).
9. **Batch 7 — lattice state/geometry/upload-hook diagnostic and the
   `X3M_ENGINE_READS=rpm` fallback** — **done 2026-09-22.**
   36 files deleted (**4,243 lines**) plus 53 added / 524 removed inside 27 edited
   files: `src/proxy/lattice_state_capture.{cpp,h}`, `lattice_state_policy.h`,
   `lattice_geometry_packet.h`, `lattice_geometry_windows.h`,
   `lattice_upload_hook.{cpp,h}`; 5 host test modules; 24 probe files
   (`lattice_state_*`, `lattice_observer_*`, `lattice_capture_lifecycle_*`,
   `lattice_upload_hook_*`, `lattice_geometry_packet_host.cpp`). `--lattice-state`
   and `X3M_LATTICE_STATE` are gone from `tools/manage.py`; the F8 arm/publish, the
   `lattice_query_depth` Present/Reset refusal, the `x3m_lattice_observer_fixture_*`
   exports and the whole compile-time observer specialization of `draw_indexed`
   (`ObserverPin`, the `dispatch(auto*)` lambda and its `QueryScope`) are gone from
   `capture.cpp`, which now matches the other three draw hooks. `snapshot_x3_run.py`
   no longer collects lattice packets. In `src/proxy/engine_memory.{h,cpp}` the
   `ReadProcessMemory` mode, `configure()`, `mode()`, the `Mode` enum and the
   `syscalls` counter are gone; the `engine_memory` log line drops `path=` (now the
   literal `path=direct`) and `rpm_calls=`, and the object trace/lifetime fixtures
   lost their A/B loops.
   **Kept deliberately:** `X3M_ADMISSION` is the ownership *application admission
   monitor* (`src/ownership/application_admission*`, used throughout `capture.cpp`
   and by the flown ownership integration runners), not part of this diagnostic;
   the inventory misattributed it. The ownership `clone_upload*`,
   `portable_managed_upload.{cpp,h}` and `surface_lease_core.h` (≈1,290 lines) and
   their probes stay: `prepare_readable_managed_uploads` has 47 call sites inside
   the flown `d3d9_ownership.cpp` wrapper, so removing them is a separate batch
   needing the ownership fixtures under Wine. The three `lattice-*` result JSONs and
   `verification/results/run201-lattice/` stay as evidence cited by
   `taa-lattice-crawl.md`, `motion-output.md` and `platform-portability.md`.
   The `object_trace`/`object_lifetime` runners' read-path gates were rewritten for
   the direct-only output (one `TIMING mode=direct` line carrying the folded records
   instead of two lines plus an `IDENTITY … equal=1`), and both were rerun under the
   Wine lock on bottle X3: `passed: true`, 153 checks / 0 failures (trace) and
   657 checks / 0 failures (lifetime).
   Build after, measured before merging main's sun-occlusion commit so the delta is
   the cleanup alone: production `d3d9.dll` 54,873,122 B against 55,174,921 B before
   (−301,799 B), same 15 imported DLLs, 8 imports gone (`DeleteFileW`, `MoveFileW`,
   `MoveFileExW`, `_fdopen`, `_close`, `_open_osfhandle`, `ferror`, `ftell`),
   `check_no_x87.py` clean over 596 reachable functions. After merging main
   `855fc1bc` the same build is 55,279,273 B with 617 reachable functions, clean.

10. **Batch 8 - stored fog look presets L0/L1/L3** - **done 2026-09-22.**
   The user accepted L2 (runs 231-237), so the preset machinery went and the L2 law became the single
   stored-range look. Removed: `fog_density_march_look1_ps.hlsl`, `fog_density_repair_look1_ps.hlsl`
   with their generated headers and provenance JSONs; the `look2` name (the kept programs are
   `fog_density_{march,repair}_look_ps.hlsl` + `fog_density_composite_look_ps.hlsl`); the `#if FOG_LOOK >= 2`
   level in `fog_density_field_inc.h`; `fog_look_count`, `fog_look_default`, `fog_look_next` and the
   L3-only `JITTER_NEAR`/`JITTER_FAR` tunables in `fog_look_math.h`; `FogFrame::look`, `FogResult::look`,
   `FogDensityStatus::looks/look_reason` and the `look_march_/look_repair_/look_composite_` arrays in
   `fog_pass.{h,cpp}`; `MotionOutput::volumetric_fog_look_step/volumetric_fog_look`, `fog_look_`, the
   `volumetric_fog_look_refused` log and the overlay `L%d` readout; the `fog_look` key/action of
   `comparison_controls.h` (Ctrl+Alt+F11); `--volumetric-fog-look` (now refused by name) and the
   `X3M_VOLUMETRIC_FOG_LOOK` forward in `tools/manage.py`. The DLL logs one
   `volumetric_fog_look_ignored` line if the variable is inherited, whatever the fog state is. Every `X3M_FOG_LOOK_*` tuning
   variable the look reads is kept (list in the ledger).
   **Stored-range ps_3_0 slots: 3104 over 8 created programs -> 1145 over 3** (march 425, repair 510,
   composite 210); the three unshaped programs and the texel-exact march stay as the shader fixture's
   parity reference and are no longer created by the renderer. The pass fixture's CPU twin
   (`fog_density_cpu_march.h`) gained the look law, since every stored draw now uses it.
   **Bit-identity:** the six reused L2 fixture cases and the repair-with-shafts image are byte-for-byte
   equal before and after, and all seven stored-density programs keep their bytecode hashes.
   Evidence: `docs/verification/volumetric-fog.md`, "Single look" section (2026-09-22).

## 5. Caveats

- No build, no Wine run and no link map was produced for this inventory; DLL
  size impact is not measured. The 55.2 MB DLL is dominated by debug info, so
  removing ~15 k lines of mostly non-production code will barely move it; the
  honest statement is "source-tree cleanup, not a size win".
- Line counts are whole-file `wc -l` for exclusively-owned files and estimates
  where a file is shared (flagged as such).
- Retired tests currently keep passing under `--include-retired`; deleting them
  removes that historical oracle. If the user wants the record kept, move them
  to `verification/historical/` as was done for `owned_media` instead.

## User verdicts (2026-09-22)

- `--point-light-root-admission`: **keep** (verified inert when unset:
  `point_light_admission.cpp` `initialize()` returns before any patch).
- `--taa-alpha-history`: **keep** (plain default-off flag).
- `X3M_ENGINE_READS=rpm`: **remove**; direct reads are the only mode flown.
- Held lattice upload/geometry-capture chain (~8,900 lines): **remove**; the crawl
  it was built to explain is fixed and accepted (taa-lattice-crawl.md §32.5–§32.6).
  Keep the ownership/shader-shadow lifetime fix. Archive the uncommitted
  `/tmp/x3-lattice-capture-lifecycle` and `/tmp/x3-lattice-payload-writer` work to a
  git branch before removing those worktrees. Own production batch with review,
  production build and `check_no_x87.py`, after batches 1–2.
