# Logging tiers: always, `--perf`, `--debug`, and `x3m.log`

Design note, 2026-09-25 (read-only survey at `ae16da06`); implemented 2026-09-26 on `9a668e81`, see "Implemented" below for what differs from this design and the measured figures. Decision: how the proxy's
telemetry/debug options collapse into a release-ready scheme where a player without the launcher can be told
"enable logging or a performance trace, reproduce, upload the log". Evidence scripts and their outputs are under
`verification/results/logging-tiers/` (`row_volume.py` → `run337-rows.json`, `run337-classes.json`;
`tier_volume.py` → `tier_volume.json`). Row sizes are measured from the Run 89 A stand log
(`/tmp/x3-bottleX3-run337/session-20260925-202714-216.log`, 104,651,234 B, 264,038 lines, 11,073 frames in
234.4 s, 253 distinct row names); every tier total is inferred from those sizes and the gates read in the source.

## Recommendation

**Ratified 2026-09-25 (orchestrator, after the user's request):** three tiers as below, `--debug` and `--perf` as the only
launcher logging options, the explicit list kept as developer options (they are not log switches: GPU serialisation, a
sampling thread, dumps, per-draw clock reads, engine stamp patches, verification code paths), `x3m.log` in the game
directory with `x3m.prev.log` kept, groups expanded inside the DLL, the seven ungated per-frame shadow/sun rows moved
under the tiers (the no-option launch must be bounded), the redaction helper for the always tier. Implementation follows
the obsolete-options cleanup on main. The config file stays a later step.

Three tiers, two launcher options, one file:

- **Always** (no environment at all): one-per-process and one-per-device rows, mode/config rows, errors,
  refusals, first-N failure witnesses, Reset/recovery, `frame_end` about once a minute (stride 3600; amended 2026-09-26, 300 in this design), a
  `volumetric_fog_cards` heartbeat every 600 frames, one `session_end` row at process detach and one `exception`
  row on the first fatal exception. Bounded by construction: about 0.2 MB per hour at 60 fps plus the
  per-session caps (inferred from measured row sizes, `tier_volume.json`).
- **`--perf`** (`X3M_PERF=1`): the per-frame cost rows at stride 1 (`frame_end`, `volumetric_fog_frame` with its
  cache/cards companions, `shadow_replay_depth`, `sun_shadow_apply_frame`), the 300-frame windows
  (`frame_timing`, `frame_phases` and their `_slow` rows, `draw_pairs`, `draw_batch`), the 1 Hz telemetry
  summaries, the loading metrics, the FPS overlay, and the feature-state family block at its 60-frame cadence.
  About 1.7 KB per frame, about 360 MB per hour at 60 fps, 21x smaller gzipped (measured ratio on run337).
- **`--debug`** (`X3M_DEBUG=1`): everything `--perf` logs except the engine stamps and state hooks, plus every
  per-frame diagnostic row at stride 1 (the family block, `camera_state`, the shadow/sun rows), the F8-frame
  censuses, and the traces (window, music, media cue, sun, sector background, LOD switch, collide census). About
  9.3 KB per frame, about 2.0 GB per hour at 60 fps (run337 measured 1.6 GB per hour at 47 fps with the stand
  set, which is `--debug --perf`).
- **Explicit, never in a group**: `--gpu-sync-timing` (serialises the GPU), `--profile` (a sampling thread),
  `--taa-debug` (about 40 MB of dumps per F8 frame), `--telemetry-draw` (two QPC reads per draw), the finer
  engine-stamp families (`--game-phases`, `--pass-phases`, `--residual-phases`, `--light-phases`,
  `--submit-phases`, `--loop-phases`; `--submit-phases` also disables the sun-occlusion default), the verify
  modes (`--mesh-adjacency verify`, `--resource-read verify`, `--collide-memo-verify`), `--sun-occlusion-log`
  (a 1x1 readback per frame), `--frame-timing-state-stamps N`, and `--volumetric-fog-everywhere` (an A/B
  switch misfiled in section 2, not a log option).

`--debug` does not imply `--perf`; the stand becomes `x3run --direct --debug --perf`. The groups are expanded
inside the DLL (`X3M_DEBUG=1` alone on a bare DLL gives the same rows as the launcher's `--debug`); the
individual `X3M_*` variables stay readable so the fixtures do not change. The log is `<game dir>\x3m.log`,
truncated at each launch after the previous one is renamed to `x3m.prev.log`; captures, readbacks and dumps stay
in `x3-modern-captures`.

The rule for group membership: `--debug` turns on every diagnostic whose only effects are log rows and reads on
F8 frames, using hooks that have already flown in the stand or in a ledgered run; anything that patches engine
code beyond that, adds GPU synchronisation, adds per-draw clock reads, writes dumps, or takes a verification code
path stays an explicit developer option. `--perf` is the subset a stutter report needs.

## Current state (measured)

- `x3m::log()` (`src/proxy/capture.cpp:3907-3913`) takes the capture lock, saves and restores the x87/MXCSR
  state (`call_preserved`) and `vfprintf`s into a 1 MiB stdio buffer (`setvbuf`, `:3011`); the buffer is
  flushed once per frame at frame begin (`:1755`, measured cost 0.1-0.6 us per flush, `log_flush` metric in
  run337). `log_handle()` (`:3914`) hands the OS handle to writers that must not take the lock: the DllMain-time
  restore rows (`sun_flare_fix.cpp:140`, `lod_occlusion.cpp:102`, `terran_station_lod.cpp:98`,
  `music_keep.cpp:123`, `fov.cpp:351`, `media_cue.cpp:122,140`) and the voice DMO vectored fault witness
  (`voice_dmo_fallback.cpp:160`). Nothing is logged at `DLL_PROCESS_DETACH` (`loader.cpp:375-395`).
- `initialize_log` (`capture.cpp:2979-3021`) opens `<module dir>\x3-modern-captures\session-YYYYMMDD-HHMMSS-<pid>.log`,
  falling back to `%LOCALAPPDATA%\x3-modern-renderer\captures` when the game directory is not writable (the W3
  audit, `docs/architecture/platform-portability.md:348-351`); the first row is `capture_dir=<path> source=game|localappdata`.
- The stand log's bytes split (run337-classes.json): 148 once-per-session names, 32,470 B; 78 event names,
  995,295 B; 27 per-frame names, 103,623,469 B (99.0 percent).
- The largest per-frame rows are not section-2 options. `motion_output_frame` (1,765 B), `hdr_frame` (1,161 B),
  `thin_vote_frame` (588 B), `fade_route_frame`, the hull/emission rows and `shadow_lease_retirement` are the
  family block gated by `telemetry_ && frame_ % frame_log_interval_ == 0` (`motion_output.cpp:7083`,
  `:7199-7202`; the interval is `X3M_MOTION_FRAME_LOG`, default 60). But `shadow_retention_frame` (1,153 B,
  `motion_output.cpp:8479`), `shadow_replay_candidates` (886 B) with `shadow_replay_sun` (218 B) and
  `shadow_alpha_casters` (158 B) (`:1977` → `publish_shadow_replay_candidates`), `shadow_replay_depth` (505 B,
  `motion_output_shadow_replay_inc.h:455`), `sun_shadow_lane_frame` (394 B, `:2104`) and `sun_shadow_apply_frame`
  (148 B, `:8639`) are written every frame with no telemetry gate at all. A launch with no option therefore
  writes about 3.5 KB per frame today, about 750 MB per hour at 60 fps (inferred from the gates and the measured
  row sizes; no option-free snapshot exists to measure it directly).
- `X3M_TELEMETRY=1` is more than rows: it enables the QPC counters around Present/Reset/resource calls, the
  4 Hz window/cursor polls (`telemetry.cpp:99-125`), the 1 Hz summaries (`:94`), and the full loading-trace
  hook set (`loading_trace.cpp:1046-1052`; without it only the two mesh-import rows of `--mesh-adjacency fast`
  are patched). `--frame-timing` installs the state hooks; `--frame-phases` patches ten engine sites.
- `X3M_SHADOW_RETENTION_TIMING` is set by the launcher (`tools/manage.py:1935`) but no DLL site reads it (grep of
  every `GetEnvironmentVariable` call under `src/`, measured): a dead option; drop it with this change.
- The launcher sends an explicit off value for every tiered variable (`tools/manage.py:1765-1772, 1786-1792,
  1822, 1871, 1926, 1940, 1974-1975, 2045-2049`), so an inherited value cannot change a flight; the dry-run
  pins in `verification/analysis/test_launcher_defaults.py:36-91` and
  `verification/results/launcher-defaults/compare_dry_runs.py` (`STAND_SHORT`, `TELEMETRY`) record those values.
- 28 files under `verification/` set `X3M_TELEMETRY` directly, 11 set `X3M_MOTION_FRAME_LOG`, 9 `X3M_TAA_DEBUG`,
  8 each `X3M_FRAME_TIMING`, `X3M_PASS_PHASES`, `X3M_SUBMIT_PHASES` (measured file counts). 15 probe runners glob
  `session-*.log` under `x3-modern-captures` (for example `run_motion_output.py:3989`); 20 analysis scripts and
  10 host tests name `session-` or `x3-modern-captures`.
- Consumers of the per-frame rows (files naming the row): `motion_output_frame` 51, `frame_end` 39,
  `telemetry_metric` 23, `camera_state` 21, `telemetry_summary` 21, `hdr_frame` 12, `frame_phases` 9,
  `shadow_replay_depth` 7, `frame_timing` 7, `shadow_retention_frame` 6, `volumetric_fog_frame` 0. Row names and
  formats do not change under this design; only gates, cadences and the file name do.

## 1. Tier of every section-2 row

Sizes are measured bytes per line in run337 unless marked inferred. "F8" means capture frames only.

| Option (variable) | Rows | Volume | Tier | Reason |
|---|---|---|---|---|
| (none) config, mode, device, `create_device*`, `adapter`, `proxy_identity`, `proxy_options`, `proxy_environment`, `clock_anchor`, `*_mode`, `*_device`, `*_configured` | 148 once-per-session names | 32,470 B per session | always | What every bug report needs first |
| (none) errors, refusals, `*_refused*`, `bloom_commit` failures (first 8), `motion_unmatched_static` (first 256), `volumetric_fog_prefill` (first 512), `shadow_replay_depth_refused`, `voice_dmo_fallback`, `taa_flicker_setting` | event | bounded by their own caps (fog prefill ≤ 88 KB, unmatched ≤ 67 KB per session, inferred) | always | Already first-N capped in the source |
| (none) Reset: `presentation_parameters` rows, `telemetry_*` reset summaries, `hdr_device`, `motion_output_device` re-creation | per Reset | ~1-2 KB per Reset (inferred) | always | Recovery evidence |
| (new) `log_open`, `session_end`, `exception` | 1 each | < 300 B | always | File provenance, teardown summary, crash witness |
| `--frame-end-stride 1` (`X3M_FRAME_END_STRIDE`) | `frame_end` | 113 B per frame at 1; default 300 | always at 300; perf and debug at 1 | The only per-frame timeline row without telemetry (dt_ms, draws, elapsed_ms) |
| (none) `volumetric_fog_cards` heartbeat | on change or every 600 frames (`fog_inc.h:731`) | 203 B | always | Sector/fog state for a fog report; already rate-limited |
| `--telemetry` (`X3M_TELEMETRY`) | `telemetry_start`, `telemetry_summary` + `telemetry_metric` (1 Hz), `engine_memory`, `loading_metric`, `mesh_adjacency_metric`, `resource_reader_metric`, `dat_handle_pool_metric`, `telemetry_window*`, `telemetry_cursor_poll` | 2.7 KB/s measured (44 B per frame at 60 fps) | perf and debug | Counters and loading metrics; prerequisite of the family block and of every other row below. Installs the full loading-trace hook set (flown in every stand run) |
| `--frame-timing` (`X3M_FRAME_TIMING`) | `frame_timing` (938 B), `frame_timing_slow` (274 B × 4 per window), `draw_pairs`, `draw_batch` (388 B) per 300 frames | 3.5 KB per 300 frames | perf | dt/present percentiles and the slow frames: the core of a stutter report. State hooks installed |
| `--frame-timing-state-stamps N` | sampled state-call stamps | unmeasured | explicit | Sampling knob for the developer |
| `--frame-phases` (`X3M_FRAME_PHASES`) | `frame_phase_mode`, `frame_phase_site` × 10, `frame_phases` (632 B), `frame_phases_slow` (242 B × 4) per 300 frames | 1.6 KB per 300 frames | perf | Attributes a slow frame to an engine phase; ten byte-verified stamps, flown in every stand run |
| `--fps-overlay` (`X3M_FPS_OVERLAY`) | `fps_overlay_mode`, `fps_overlay_toggle` | on-screen only | perf | The player sees the number while reproducing; cost is one text draw per frame (inferred small) |
| `--volumetric-fog-timing` (`X3M_VOLUMETRIC_FOG_TIMING`) | `volumetric_fog_frame` (330 B), `volumetric_fog_cache_frame` (200 B), `volumetric_fog_cards` at stride 1 (203 B) | 733 B per frame | perf | cpu_us and device calls of the pass on the exact frame of a stutter |
| (new) `X3M_SHADOW_TIMING` | `shadow_replay_depth` (505 B, has us=), `sun_shadow_apply_frame` (148 B, has us=) at stride 1 | 653 B per frame | perf | Today unconditional per frame; under the always tier they move into the 60-frame family block |
| (none today) `shadow_retention_frame`, `shadow_replay_candidates`, `shadow_replay_sun`, `shadow_alpha_casters`, `sun_shadow_lane_frame` | 2.7 KB per frame at stride 1 | today every frame, ungated | family block: perf at 60, debug at 1 | State rows, not cost rows; the same gate as `hdr_frame` |
| `X3M_MOTION_FRAME_LOG=1` (shell prefix today) | the family block: `motion_output_frame`, `hdr_frame`, `thin_vote_frame`, `fade_route_frame`, `hull_*_frame`, `emission_source_gain_frame`, `original_fill_frame`, `screen_emission_additive_frame`, `shadow_lease_retirement` | 4.6 KB per frame at 1; 77 B per frame at 60 | debug at 1; perf at 60 | Rendering-state rows per frame for a rendering bug; the stutter report only needs the cadence |
| `--camera-log 1` (`X3M_CAMERA_LOG`) | `camera_state` (587 B) | 587 B per frame at 1; default 300 | debug at 1; off outside F8 otherwise | TAA camera diagnostic, 21 consumers; F8 frames always log it |
| `--shadow-retention-census` (`X3M_SHADOW_RETENTION_CENSUS`) | `shadow_retention_probe`, `_resight`, `_summary` | 36-37 rows per 4 min (window) | debug | Calibration rows; the retention default overrides it |
| `--object-bounds-log` (`X3M_OBJECT_BOUNDS_LOG`) | `object_bounds_mode`, bounds rows on F8 | F8 only, hundreds of rows per press (inferred) | debug | LOD overlay tooling reads it; no per-frame cost |
| `--cull-census` (`X3M_CULL_CENSUS`) | `cull_census*` on F8 | F8 only | debug | Two read-only stubs armed on F8 frames; flown in every stand run |
| `--lod-switch-log [N]` (`X3M_LOD_SWITCH_LOG`) | `cull_census_lod_switch` rows | F8 / bounded table | debug | Same tooling |
| `--media-cue-trace` (`X3M_MEDIA_CUE_TRACE`) | media record rows | per cue, bounded | debug | One byte-verified gate, flown (`docs/verification/media-cues.md`) |
| `--music-trace` (`X3M_MUSIC_TRACE`) | music state rows | per transition | debug | Three trampolines the `--music-keep` default already shares |
| `--window-trace` (`X3M_WINDOW_TRACE`) | `window_msg` rows | bursty on alt-tab, per message | debug | Cursor reports; flown Run 84 A |
| `--shadow-sun-trace` (`X3M_SHADOW_SUN_TRACE`) | per-frame sun rows | unmeasured (inferred ~200 B per frame) | debug | Trace only |
| `--sector-background` (`X3M_SECTOR_BACKGROUND`) | sector rows | per sector | debug | Read-only |
| `--loading-probes` (`X3M_LOADING_PROBES`) | `loading_probe_site` × 12, `loading_probe_path` | loading only | debug | Twelve IAT rows, flown (`docs/verification/loading-probes.md`); `loading_probe_path` prints game-file paths (relative to the game, no user name) |
| `--collide-narrow-census`, `--collide-query-phases` | census / timing windows | per 300 frames | debug | Windows, no engine change beyond the collide defaults |
| `--collide-memo-verify` | verify rows | per memo | explicit | A verification code path |
| (none today) 300-frame windows: `chase_*_window`, `chase_*_timing`, `chase_native_timing_*`, `bloom_commit` periodic, `bloom_admission`, `bloom_prepare`, `bolt_footprint`, `media_cue_window`, `collide_memo`, `collide_census`, `shadow_retention_summary` (non-final), `screen_emission_additive_refused_window` | ~20 rows × 150-520 B per 300 frames | ~5 MB per hour ungated (inferred from run337 counts) | debug | Health windows the always budget cannot carry; the first-applied and failure rows of the same subsystems stay always |
| (none today) `taa_invalidate` | per invalidation | 53 B × 1.8/s measured (0.35 MB per hour) | debug | Diagnostic event, unbounded |
| `--telemetry-draw` (`X3M_TELEMETRY_DRAW`) | cost fields on `motion_output_frame` | none extra | explicit | Two QPC reads per draw |
| `--game-phases`, `--game-phase-threshold-ms`, `--pass-phases`, `--residual-phases`, `--light-phases`, `--submit-phases`, `--loop-phases` | stamp windows | per window | explicit | Engine stamps beyond the frame family; `--submit-phases` conflicts with the sun-occlusion default (`tools/manage.py:1572`) |
| `--profile`, `--profile-interval-us` (`X3M_PROFILE*`) | `profile_*` reports every 5 s | per report | explicit | Sampling thread; native Windows only |
| `--mesh-adjacency verify`, `--mesh-adjacency-dump` | difference rows, `.bin` dumps | dumps | explicit | Self-check that slows loading |
| `--resource-read verify` | difference rows | | explicit | Same |
| `--taa-debug` (`X3M_TAA_DEBUG`) | `taa_age` and raw resolve dumps on F8 | ~40 MB per F8 frame | explicit | Dumps, support-directed |
| `--sun-occlusion-log`, `--sun-occlusion-radius/-curve` | probe rows, `lens` dumps on F8 | 1x1 readback per frame | explicit | Readback per frame; the two others are knobs |
| `--gpu-sync-timing` (`X3M_GPU_SYNC_TIMING`) | `gpu_sync_*` rows | | explicit | Halves fps |
| `--volumetric-fog-everywhere` | none (renders fog everywhere) | | explicit A/B | Not a log option |
| `--shadow-retention-timing` (`X3M_SHADOW_RETENTION_TIMING`) | none: no DLL read | | drop | Dead variable (measured) |
| `X3M_LOCKED_PREFIX_LOG` (section 5) | `locked_prefix` per draw | fixtures only | unchanged | Fixture diagnostic |

## 2. `--perf` stays separate

Recommended, for three reasons. Cost: `--perf` needs the telemetry counters, the state hooks and the ten phase
stamps; `--debug` needs none of them, and a rendering-bug reproduction should not carry engine patches it does
not need (and `--debug` should not carry per-draw timing risk that would make its numbers suspect). Volume:
`--perf` is 1.7 KB per frame against 9.3 KB (5.5x); a 10-minute stutter reproduction is about 60 MB raw and
about 3 MB gzipped, which a player can upload; the same reproduction under `--debug` is 330 MB raw. Content: a
stutter report needs per-frame dt, draws, fog and shadow pass cost, the slow-frame breakdowns and the loading
metrics, all of which are in `--perf`; a rendering report needs the state rows (`motion_output_frame`,
`hdr_frame`, `camera_state`, shadow state) at stride 1 and the traces, none of which help a stutter. The two are
composable: `--debug --perf` is today's stand set. The one shared cost, `X3M_TELEMETRY=1` (both groups set it,
because the family block is gated on it), is accepted: it is what the stand has flown at 50-60 fps since Run 84.

## 3. Log file policy

- **Name and location**: `<game dir>\x3m.log`, the game directory being the proxy module's directory as today
  (`GetModuleFileNameW`). Captures, readbacks, shader and TAA dumps stay in `<game dir>\x3-modern-captures\`;
  the `capture_dir=` row stays and a new first row `log_open file=<path> source=game|localappdata|override
  previous=renamed|absent|busy session=YYYYMMDD-HHMMSS-<pid>` records what happened.
- **Rotation**: before opening, `MoveFileExW(x3m.log, x3m.prev.log, MOVEFILE_REPLACE_EXISTING)`; then
  `_wfopen(L"w")`. One previous log is kept; a player who launched twice after the bug still has the log. If the
  rename fails with a sharing violation (a second instance still running, or an editor holding the file on
  Windows), open `x3m-<pid>.log` instead and say so in `log_open`; never fail to log because of the name.
- **Override**: `X3M_LOG_FILE=<absolute path>` opens exactly that path (no rotation). It is for the fixture
  runners (below) and for a support case whose game directory is unwritable.
- **Size bound**: none. The always tier is bounded by construction (about 0.2 MB per hour plus per-session caps);
  `--perf` and `--debug` are opt-in and the player is told the size. A size cap would drop the rows that matter
  most (teardown, crash), and rotation by size adds a second file the player has to find; revisit only if a
  `--perf` upload proves too large, with a stride knob rather than a cap.
- **Flush**: keep the 1 MiB buffer and the per-frame `fflush` (measured 0.1-0.6 us). Rows written outside the
  frame loop (device creation, Reset, loading) reach the OS at the next frame begin or at the next telemetry
  summary. Consequence on a crash: at most the current frame's rows are lost from the stdio buffer. The crash
  row itself does not go through stdio: one process-wide vectored exception handler registered last
  (`AddVectoredExceptionHandler(0, ...)`, `EXCEPTION_CONTINUE_SEARCH`, never handles anything), which for the
  first fatal code only (access violation, illegal instruction, stack overflow, privileged instruction, integer
  divide) writes one `exception code= address= eip= esp= thread= frame= module=` row through `log_handle()`
  with `WriteFile`, with no allocation and no lock, exactly as `voice_dmo_fallback.cpp:160` does today. The
  same handler exists there for the DMO fault, which shows that first-chance exceptions of other codes do
  arrive under Wine (`other_first_chance` counter), hence the code filter and the one-shot latch.
- **Teardown**: one `session_end frames= elapsed_ms= devices= resets= exception=0|1` row at
  `DLL_PROCESS_DETACH` (process exit only, `reserved != nullptr`) through the OS handle, without the capture lock
  and without `fflush`, following the rule already stated at `capture.cpp:3891-3896` (a killed thread may hold a
  lock). The normal exit path still flushes stdio through the CRT; the row is unbuffered so it always lands.
- **Unwritable game directory (native Windows)**: the existing W3 fallback stays, moved to
  `%LOCALAPPDATA%\x3-modern-renderer\x3m.log` (captures keep `...\captures`), recorded in `log_open`. Two native
  behaviours are not verified and should be settled before release: whether `X3AP.exe` carries a manifest with
  `requestedExecutionLevel` (without one, a 32-bit process writing under Program Files is redirected by UAC file
  virtualisation to `%LOCALAPPDATA%\VirtualStore\...`, the write succeeds, and the player does not find the file
  next to the game); and whether the Steam and GOG installs grant users write access to the game directory
  (Steam normally does). The first is a PE resource question (`RT_MANIFEST` in the EXE; a host script over the
  resource directory settles it); the mitigation is cheap either way: after opening, call
  `GetFinalPathNameByHandleW` and log the resolved path, so the `log_open` row tells the support instruction
  where the file really is.
- **Launcher tee and snapshot**: the launcher's `launcher-stderr.log` stays in `x3-modern-captures` (the tee
  and `test_launcher_stderr_tee.py:138` do not change); it is the developer's own stderr, and a player has no
  launcher. `snapshot_x3_run.py` selects `<captures>.parent / 'x3m.log'` instead of the `session-*` regex
  (`:22-23`), keeps the `--since-ns` birth-time check (a renamed-then-created file has a fresh birth time on
  APFS; if the rename failed and `"w"` truncated in place, the birth time is stale and the check must fall back to
  mtime, which the `log_open` row's `previous=busy` makes visible), and copies the file into the run directory
  under its session name `session-YYYYMMDD-HHMMSS-<pid>.log` taken from the `log_open` row. Every analysis script
  that globs `session-*.log` in a run directory then works unchanged, and `x3run` needs no edit.

## 4. Volume at 60 fps

From `tier_volume.json` (row sizes measured in run337; membership and cadence per this note, so the totals are
inferred). Per-session once rows: 32,470 B measured.

| Tier | Bytes per frame | MB per hour at 60 fps | 10-minute reproduction (raw / gzip at the measured 21.2x) |
|---|---|---|---|
| Today, no option (ungated shadow/sun rows) | 3,464 | 748 | 125 MB / 6 MB |
| Always | 0.72 (+ once rows and caps) | 0.19 (+ < 0.3 per session, inferred) | < 0.1 MB |
| `--perf` | 1,679 (frame rows 1,499; family at 60: 121; summaries 44; windows 15) | 363 | 60 MB / 3 MB |
| `--debug` | 9,293 (the 27 per-frame rows at stride 1) plus windows, traces and F8 bursts | 2,007 + traces | 335 MB / 16 MB |
| `--debug --perf` (the stand) | as `--debug` plus the perf windows | about 2,010; run337 measured 1,607 at 47 fps | |

The gzip ratio is measured on the whole run337 log (104,651,234 B → 4,936,520 B); a `--perf` log has less
repetition per byte than the family block, so its ratio is inferred to be lower.

## 5. Migration

DLL (the only place groups are expanded):

- One helper reads `X3M_DEBUG` and `X3M_PERF` once at `initialize_log` and each existing read site becomes
  `individual == "1" || group`; the ~40 sites listed in the survey (`capture.cpp:2703, 2820, 3079-3108, 3549,
  3684, 3782`; `telemetry.cpp:33-35`; `frame_timing.cpp:113`; `frame_phases.cpp:110`; `cull_census.cpp:306-320`;
  `loading_probes.cpp:112`; `window_trace.cpp:189`; `media_cue.cpp:282`; `music_keep.cpp:593`;
  `collide_narrow_census.cpp:180`; `collide_query_phases.cpp:75`; `sun_occlusion.cpp:240` stays explicit). For
  the cadence knobs (`X3M_FRAME_END_STRIDE`, `X3M_CAMERA_LOG`, `X3M_MOTION_FRAME_LOG`): an explicitly set valid
  value wins; otherwise the group gives 1, otherwise the default (300, 300, 60). An explicit "0" of a boolean
  does not defeat its group (OR semantics): nothing needs the opposite, and it keeps the fixture rule trivial.
- Gate changes: the five shadow/sun state rows join the family block gate; `shadow_replay_depth` and
  `sun_shadow_apply_frame` get the new `X3M_SHADOW_TIMING` stride-1 gate (perf) and otherwise the family
  cadence; `camera_state` logs on F8 frames always and per `camera_log_interval_` only when the interval was
  set or `--debug` is on; the 300-frame windows and `taa_invalidate` take the debug gate; new rows `log_open`,
  `session_end`, `exception`; `x3m.log` naming, rotation, `X3M_LOG_FILE`; the `X3M_SHADOW_RETENTION_TIMING`
  launcher line goes.
- Fixtures: the individual variables stay, so the 28 `X3M_TELEMETRY` files, 11 `X3M_MOTION_FRAME_LOG` files and
  the rest set nothing new. The 15 runners that glob `session-*.log` under the fixture's `x3-modern-captures`
  keep working if each sets `X3M_LOG_FILE=<fixture dir>/x3-modern-captures/session-<stamp>-<pid>.log` in the
  child environment; there is no shared launch helper under `verification/probe/` (measured: none found), so
  this is a per-runner one-liner, or the fixture build (`X3M_MOTION_OUTPUT_FIXTURE`) keeps the session naming
  as its default. The first is preferred: it keeps the production open path under test.
- One new fixture case asserts group equivalence: a bare DLL run with `X3M_DEBUG=1` yields the same set of row
  names as the run with the individual variables of the debug group set; likewise `X3M_PERF=1`.

Launcher (`tools/manage.py`):

- Add `--debug` and `--perf` (env `X3M_DEBUG=1`, `X3M_PERF=1`); stop sending the explicit off values for the
  tiered set (the lines cited above) and instead pop any inherited value of that set, which preserves the
  intent of the `:1770` and `:1772` comments (an inherited stride or GPU-sync flag cannot change a flight);
  send an individual variable only when its option was passed. The `requires --telemetry` checks
  (`:963-1023`) accept `--debug` or `--perf` as satisfying them. The dry-run's `log_directories` (`:1727`)
  becomes `log_file` plus the fallback. The old Run 84 A stand command keeps working (explicit individuals).
- Stand command in `docs/verification/user-runs.md`: `env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3
  /Users/asvetl/x3-mod/x3run --direct --debug --perf` (the `X3M_MOTION_FRAME_LOG=1` prefix goes; the DLL's
  debug group sets it).

Host tests and scripts:

- `verification/analysis/test_launcher_defaults.py`: `EMPTY` loses the ~30 tiered keys (`X3M_TELEMETRY`,
  `X3M_TELEMETRY_DRAW`, `X3M_FRAME_*`, `X3M_FPS_OVERLAY`, `X3M_GPU_SYNC_TIMING`, `X3M_CAMERA_LOG`,
  `X3M_GAME_PHASE*`, `X3M_*_PHASES`, `X3M_PROFILE*`, `X3M_TAA_DEBUG`, `X3M_MEDIA_CUE_TRACE`,
  `X3M_SHADOW_RETENTION_CENSUS`, `X3M_SECTOR_BACKGROUND`, `X3M_VOLUMETRIC_FOG_TIMING`, `X3M_LOADING_PROBES`,
  `X3M_MESH_ADJACENCY_DUMP` ...; `X3M_RESOURCE_READ=fast` and `X3M_MESH_ADJACENCY=fast` stay, they are
  functional defaults); `STAND_TELEMETRY` becomes `{X3M_DEBUG: 1, X3M_PERF: 1}`; a new test pins that the old
  stand command still produces the individual variables and that `--debug --perf` produces only the two group
  variables on top of the empty command.
- `verification/results/launcher-defaults/compare_dry_runs.py`: `STAND_SHORT` and `TELEMETRY` updated the same
  way; the expected difference "empty vs stand = the telemetry/debug variables" becomes the two group keys.
- `verification/analysis/test_snapshot_x3_run.py` (session names at `:33, :145, :147, :292`) follows the
  snapshot change; `test_proxy_identity.py:259, 301` keep the `capture_dir` line and gain the `log_open` line;
  `test_telemetry_summary.py`, `summarize_telemetry.py`, `analyze_camera_state.py`, `cull_census.py`,
  `shadow_retention.py` and the other 20 analysis scripts do not change (row formats unchanged, snapshot keeps
  the session file name).
- Docs: inventory section 2 (rows regrouped by tier), `docs/verification/user-runs.md` stand command,
  `docs/architecture/platform-portability.md` W3 paragraph, the player-facing instruction (enable, reproduce,
  zip `x3m.log`, and `x3m.prev.log` if the game was relaunched).

## 6. Player-specific data in the always tier

Measured in run337's header rows:

- `proxy_environment` (`proxy_identity.cpp:182, 282`) echoes every `FEX_*`, `WINE*` and `CX_*` variable with its
  value; the values of `CX_BOTTLE_PATH`, `CX_HOME`, `WINEPREFIX`, `WINECONFIGDIR`, `WINEHOMEDIR`,
  `WINE_HOST_HOME`, `WINE_HOST_PWD`, `WINE_HOST_PATH`, `WINEDLLPATH` carry the macOS home path (`/Users/asvetl/...`
  in run337) and `WINEUSERNAME` the account name. On native Windows none of these variables exist, so the row
  is empty there; under CrossOver it is the one real leak. Trim: keep the names, replace the home-directory
  prefix (the `HOME` value, case-insensitively) with `~` in every value, and drop the `WINEUSERNAME` value.
- `capture_dir=` and the new `log_open file=` on the `%LOCALAPPDATA%` fallback contain `C:\Users\<name>\...`;
  `proxy_identity path=` contains the game directory, which is under the profile only for a non-default
  install. Trim with one helper that replaces the `%USERPROFILE%` prefix (and under Wine the `Z:\Users\...` /
  `Z:\home\...` host prefix) with the token, applied to those three rows.
- `loading_probe_path` (debug tier) prints game-relative file names; `loading_intervals_file` is gone with its
  option; readback rows print basenames only. Adapter description, driver, display modes, HWND values, the DLL,
  manifest and EXE hashes, and the `proxy_options` echo of the `X3M_*` values (none of which carries a path) are
  not personal and stay.

## Cost on the hot path

- Always: one `frame_end` row per 3600 frames (300 in this design; amended 2026-09-26) and one QPC read per frame that
  already exists (`capture.cpp:1698-1707`); the per-frame `fflush` stays (0.1-0.6 us measured; replaced by the writer
  thread in the implementation, see "Implemented"). The seven ungated shadow/sun rows stop being formatted
  every frame, which removes about 3.5 KB of `vfprintf` work per frame from every player launch (a saving,
  size measured; time inferred).
- `--perf`: about six formatted rows per frame plus the family block every 60 frames; the state hooks and ten
  phase stamps of `--frame-timing` / `--frame-phases`; the telemetry QPC reads around Present/Reset/resource
  calls and the 4 Hz polls. The stand has flown this at 50-60 fps at 5120x1440 (`docs/verification/user-runs.md`).
- `--debug`: 27 rows, about 9.3 KB of formatting per frame under the capture lock and one x87 save/restore per
  row. The per-row cost of `log()` under FEX is not measured; inferred at 1-5 us per row from the formatter
  (MinGW `pformat`, `%g`/`%f` fields), so 30-150 us per frame, 0.2-0.9 percent of a 16.7 ms frame. What settles
  it: a fixture case in the motion-output harness that logs 10,000 `motion_output_frame`-sized rows and reports
  QPC per row, run once under Wine and (when available) natively.

## Native Windows behaviour

`_wfopen(L"w")` on msvcrt opens with `_SH_DENYNO`, so the player can copy `x3m.log` while the game runs;
`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING` is the documented rename; `WriteFile` on the CRT's OS handle,
`AddVectoredExceptionHandler`, `GetFinalPathNameByHandleW` and `GetEnvironmentVariableW` are all documented
Win32 APIs, nothing Wine-specific. Unverified natively: UAC virtualisation of the game directory (manifest
question above), Steam/GOG directory ACLs, and whether an antivirus holds `x3m.log` open at launch (the
`x3m-<pid>.log` fallback covers the sharing violation). None of the tiers changes rendering; `--perf`'s hooks
are the same on both platforms.

## Implemented (2026-09-26)

Uncommitted worktree on `9a668e81`. The ratified shape, with the two amendments of 2026-09-26 (the always-tier heartbeat is
`frame_end` about once a minute, stride 3600, `--perf`/`--debug` stride 1, an explicit `X3M_FRAME_END_STRIDE` wins; the
fixture seams keep their `X3M_FIXTURE_*` names and every individual `X3M_*` variable stays a DLL read, ORed with its group,
an explicit cadence value winning) and the orchestrator's writer-thread requirement (below), which replaces "keep the
per-frame `fflush`".

**Groups** (`src/proxy/log_tiers.h`, header-only so the fixture builds that compile single sources link nothing new):
`X3M_DEBUG=1` / `X3M_PERF=1` are read at each switch's own read site; `log_tier::telemetry()` is `X3M_TELEMETRY=1` or
either group. Membership as implemented:

| Group | Switches (individual variable, read site) |
|---|---|
| both | `X3M_TELEMETRY` (telemetry.cpp, loading_trace.cpp: counters, 1 Hz summaries, loading metrics, the loading-trace hook set, the family block's gate); `X3M_FRAME_END_STRIDE` 1 (capture.cpp) |
| `--perf` | `X3M_FRAME_TIMING` (state hooks, 300-frame windows), `X3M_FRAME_PHASES` (ten stamps), `X3M_FPS_OVERLAY`, `X3M_VOLUMETRIC_FOG_TIMING`, `X3M_SHADOW_TIMING` (new: `shadow_replay_depth` and `sun_shadow_apply_frame` every frame) |
| `--debug` | `X3M_MOTION_FRAME_LOG` 1 (family block every frame), `X3M_CAMERA_LOG` 1, `X3M_SHADOW_ROWS` (new: the five shadow/sun state rows every frame), `X3M_SHADOW_RETENTION_CENSUS`, `X3M_OBJECT_BOUNDS_LOG`, `X3M_CULL_CENSUS` with `X3M_LOD_SWITCH_LOG` 16, `X3M_MEDIA_CUE_TRACE`, `X3M_MUSIC_TRACE`, `X3M_WINDOW_TRACE`, `X3M_SHADOW_SUN_TRACE`, `X3M_SECTOR_BACKGROUND`, `X3M_LOADING_PROBES`, `X3M_COLLIDE_NARROW_CENSUS`, `X3M_COLLIDE_QUERY_PHASES`; since the second step (below) `X3M_FRAME_PHASES` (the frame boundary, also a `--perf` member; its ten stamps are the only engine patches of the group) |
| `--draw-trace` (second step, not a group of the stand) | `X3M_TELEMETRY_DRAW` (telemetry.cpp: per-draw route cost fields), `X3M_GAME_PHASES` (game_phases.cpp, tape threshold at its 20 ms default unless `X3M_GAME_PHASE_THRESHOLD_MS` is set), `X3M_PASS_PHASES`, `X3M_RESIDUAL_PHASES`, `X3M_LIGHT_PHASES`, `X3M_LOOP_PHASES` (each read through `log_tier::draw_trace_flag`), `X3M_FRAME_PHASES`; expanded from `X3M_DRAW_TRACE=1`; needs telemetry and the frame boundary from `--perf` or `--debug` |

**Second step (user and orchestrator decisions 2026-09-26): developer options trimmed to five.** Section 2 of the launcher
inventory is now `--debug`, `--perf`, `--draw-trace`, `--gpu-sync-timing`, `--taa-debug`, `--profile` (+
`--profile-interval-us`) and `--sun-occlusion-log` (+ `--sun-occlusion-radius`, `--sun-occlusion-curve`), plus the F8
capture controls. The three logging levels: `--perf` = the cheap cost rows; `--debug` = the per-frame diagnostic rows and
traces, with no engine patch beyond the frame boundary (`X3M_FRAME_PHASES` joins it); `--draw-trace` (`X3M_DRAW_TRACE`,
expanded in `src/proxy/log_tiers.h`) = the heavy attribution: `X3M_TELEMETRY_DRAW` (two QPC reads per routed draw, about 1 ms
per frame by `docs/verification/route-cost-run1.md`, inferred) and the engine-stamp families `X3M_GAME_PHASES` (20 ms segment
tape), `X3M_PASS_PHASES`, `X3M_RESIDUAL_PHASES`, `X3M_LIGHT_PHASES`, `X3M_LOOP_PHASES` (byte-verified trampolines, each
failing closed per site). The heavy set is in neither group because the stand is `--debug --perf` and runs on every
flight; a stutter report starts with `--perf`, adds `--debug`, and uses `--draw-trace` for one attribution flight. The
launcher refuses `--draw-trace` without `--perf` or `--debug` (the telemetry counters and the frame boundary) and sends
nothing under `--vanilla`; the DLL also turns the frame phases on for `X3M_DRAW_TRACE=1`. `X3M_SUBMIT_PHASES` is in no
group: its stamp at 0x00472490 claims the lens traversal call the sun-occlusion default patches, so it stays a
fixture-only read (`run_submit_phase_cpu.py`, the site verifiers); the launcher's `--submit-phases` conflict check and the
default's suppression went with the option, the DLL still refuses the pair. Fixture-only (options removed, reads kept):
`X3M_FRAME_END_STRIDE` (an explicit value still wins over 3600 / 1), `X3M_FRAME_TIMING_STATE_STAMPS`,
`X3M_COLLIDE_MEMO_VERIFY`, `X3M_MESH_ADJACENCY=verify`, `X3M_GAME_PHASE_THRESHOLD_MS`; `--resource-read verify` left the
launcher (the fixture binds the verify mode directly). Removed with their DLL code: `X3M_MESH_ADJACENCY_DUMP` (the in-game
dump path and call; the writer stays for the fixture self-test) and `X3M_VOLUMETRIC_FOG_EVERYWHERE` (the forced bluewell
profile and the latch's forced target). Dry runs: 123 variables without an option, 124 with one group, 125 with both or
with `--perf --draw-trace`, 126 with all three (the members are expanded by the DLL; measured by
`verification/results/logging-tiers/dry_run_tiers.py`). Fixture case `seam-log-tiers` now compares three groups with their
individuals (`LOG_TIERS_DRAW_TRACE` in `run_motion_output.py`, run on top of `X3M_PERF=1`).

Volume of `--debug` after the second step (re-estimated from the measured run337 row sizes, the sum inferred;
`verification/results/logging-tiers/debug_volume.py`): 8.0 KB per frame (7,968 B of per-frame rows, 73 B of 1 Hz
telemetry at 60 fps, 7 B of windows), about 1.7 GB per hour at 60 fps; with `--perf` 9.4 KB per frame, about 2.0 GB per
hour; F8 capture frames add their burst (about 31 KB per frame in the fixture). The 9.3 KB of section 4 summed every
per-frame row of run337, the perf-only fog and shadow cost rows included. Buffer headroom: `--debug --perf` produces
about 113 KB per 200 ms writer interval at 60 fps against the 2 MiB half of the 4 MiB buffer, a factor of 18.6 (inferred).

**Gates.** The seven formerly ungated shadow/sun rows: `shadow_replay_depth` and `sun_shadow_apply_frame` on
`X3M_SHADOW_TIMING` or the family cadence; `shadow_retention_frame`, `shadow_replay_candidates`, `shadow_replay_sun`,
`shadow_alpha_casters`, `sun_shadow_lane_frame` on `X3M_SHADOW_ROWS` or the family cadence (capture frame, or telemetry on
and `frame % X3M_MOTION_FRAME_LOG == 0`); the per-frame state they carry (flip tracker, counters, retention session totals)
is still updated every frame, only the formatting is skipped. `camera_state`: capture frames always, otherwise only with an
interval (explicit or `--debug`). The 300-frame health windows and `taa_invalidate` are gated on telemetry
(`X3M_TELEMETRY=1` or either group), not on `--debug` alone: `bloom_admission`, the periodic `bloom_prepare` /
`bloom_commit` (their first success and first eight failures stay in every tier), `chase_camera_window`,
`chase_fire_window`, `chase_view_restore_state`, `collide_census`, the periodic `collide_memo`, `bolt_footprint` with its
histograms, `screen_emission_additive_refused_window`, `shadow_retention_resight` and the non-final
`shadow_retention_summary`, `taa_invalidate` (`chase_aim`, `chase_lead`, `chase_transition` and `media_cue_window` were
already telemetry or trace rows). Reason for the deviation: every fixture runner that reads them sets `X3M_TELEMETRY=1`,
so they keep working unchanged, and `--perf` stays exactly equivalent to its individual variables; the cost for `--perf` is
the ~5 MB per hour estimated in section 1 (inferred), 1.4 % of its 360 MB.

**Log file** (`src/proxy/session_log.cpp`): as section 3, with `CreateFileW` (`CREATE_ALWAYS`, shared read/write, not
delete) instead of `_wfopen`; any rename failure other than file/path-not-found opens `x3m-<pid>.log` (`previous=busy`);
after the open, `x3m-<pid>.log` files of that directory last written before the current `x3m.prev.log` are deleted
(only names matching the pattern; `log_open stale_removed=`); an unknown module path never opens `\x3m.log` at a drive
root but goes to the `%LOCALAPPDATA%` fallback; `X3M_LOG_FILE` gives `source=override previous=none` (a failed override
falls back to the policy and appends `override=failed`); `file=` is the `GetFinalPathNameByHandleW` path, redacted. First
rows: `log_open`, `capture_dir`. Redaction (section 6), applied before `sanitize` so a profile path with a space or a
non-ASCII byte still matches: `capture_dir`, `log_open file=`, `proxy_identity path=`, `loaded_module path=` and
`backend path=` through
`redact_path` (`%USERPROFILE%` prefix, else a `<drive>:\Users\<name>` / `<drive>:\home\<name>` prefix to `~`);
`proxy_environment` values through `redact_value` (every occurrence of the host home, `HOME` or `WINE_HOST_HOME`, in both
separator forms, and of `%USERPROFILE%`), `WINEUSERNAME` without its value.

**Rows added**: `log_open` (first row), `session_end frames= elapsed_ms= devices= resets= exception=0|1 dropped=
filter=ours|replaced|none` (at process exit, or at a dynamic unload, through the OS handle), `exception code= address= eip=
esp= thread= frame= module= base= offset= access_kind= access= unhandled=1` (the first exception nobody handled; `module=`
is the executable's file name, `proxy`, `system_d3d9`, `other` or `none`, from `VirtualQuery` and a table filled at arm
time), `log_dropped n=`, `log_writer` (telemetry: every 10 s and at the last device release), `log_writer_parked reason=
exited=`, `sun_shadow_lane_refusals_skipped device= frame= skipped= since_frame=`. No existing row name or field format
changed; values that changed: `motion_output_mode ... camera_log=` reads 0 by default (capture frames only),
`frame_end_stride_mode` appears for any stride but 3600, and `sun_shadow_lane_refusals` (one row per frame with an
untracked writer, in every tier) is written every frame only with the family rows every frame (`X3M_SHADOW_ROWS`,
`--debug`, or `X3M_MOTION_FRAME_LOG=1` with telemetry), otherwise for the first 16 frames per device and then once per 600
frames, the skipped frames counted by `sun_shadow_lane_refusals_skipped`. The telemetry metric `log_flush` is renamed
`log_wake`: it times the `SetEvent` a summary uses to wake the writer.

### Writer thread

`x3m::log()` formats one row behind `call_preserved` into 8 KiB of stack scratch (a longer row, such as a
`proxy_options` with many variables, is formatted straight into the buffer under the lock; rows over 256 KiB are cut and
marked ` truncated=1`) and copies it into a 4 MiB buffer of two 2 MiB halves under its own SRW lock (not the capture lock).
One writer thread (`CreateThread`, 64 KiB stack reservation) drains every 200 ms, when a half fills, and on a telemetry
summary, with `WriteFile` in 64 KiB chunks; the lock is never held across a write. No `WriteFile`, `fflush` or
`MoveFileExW` runs on a game thread in play after `log_open`: the rows that used to be written straight to the handle
from game threads (media cue enter and video-blit lines, music trace lines) go through the buffer too (a hung game thread
does not stop the writer, which is what the media cue's direct write was for). Direct writes left: the voice DMO
fallback's fault witness, in a vectored handler on a faulting thread that may hold the buffer lock and is about to die (it
fires only on that hook's execute fault; `report()` logs the record through the buffer as well), and the four patch
restore rows (`fov`, `lod_occlusion`, `sun_flare_fix`, `terran_station_lod`), written only inside DllMain on a dynamic
`FreeLibrary`, never during play (their fixture records are bound to those sources; `test_logging_tiers.py` pins the
list). Full buffer:
the row is dropped and counted, never blocking and never growing; one `log_dropped n=` row precedes the next row that
fits, and `session_end dropped=` carries the session total.

The writer holds a module reference (`GetModuleHandleExW`, released by `FreeLibraryAndExitThread`) only while a device
exists: the last device's release parks it (it drains, ends and drops the reference; the releasing thread waits at most
1 s, once, at teardown) and the next device creation starts a new one; rows logged while no writer runs stay in the
buffer for the next writer or for detach. So an application's `FreeLibrary` after its last device unloads the proxy.

Crash row: a filter installed with `SetUnhandledExceptionFilter` at attach, chained to the filter it replaced (called
after ours; its answer is returned). It runs only for an exception nobody handled, so a probe read or a driver's own
`__try` never burns the one-shot row or stalls a thread. It writes its row with integers only (a stack buffer, no CRT, no
allocation) and restores the last error; the rows logged before the fault go first: it wakes the writer and waits until
they are written, bounded by `GetTickCount64` at 500 ms (not on the writer thread, not for a stack overflow), and without a
running writer it writes them itself under a try-lock of the buffer. A filter installed later by the game or a runtime
replaces ours: the crash row is then absent, `session_end filter=replaced` says so at a clean exit, and a crash still shows
as a log that ends without `session_end`. At detach the filter it replaced goes back unless another took over.

### Exit path

`DLL_PROCESS_DETACH` first makes `log()` try the buffer lock once (never wait). At process exit (`lpReserved != NULL`)
Windows has already ended every other thread, the D3D runtime's own included (wined3d's command stream): the proxy's
static teardown must not call into D3D, so every device context still alive (the application never released its device,
or an exception left a frame) moves into storage that is never destroyed (`abandon_devices_at_exit`, after
`abandon_fog_density_workers`) and `~Device` / `~MotionOutput` never run; the process's memory and handles go with it
(documented DllMain rule: at process exit only release what is safe without other threads, anything else is left to the
OS). Before this, a device left alive at exit hung the process: `release_resources` waited on wined3d's dead thread (the
2026-09-26 `seam-thin-vote-hostile` failure; before the writer existed, the fixture's `FreeLibrary` had unloaded the proxy
while the runtime's threads still ran, and the pinned writer moved the same teardown to `ExitProcess`). Then the writer is
signalled and waited for at most 1 s (at process exit it is already gone; on a dynamic unload it has either parked or this
is its own `FreeLibraryAndExitThread`, where nothing waits), and the rows still buffered and `session_end` are written:
lock-free at process exit, under a try-lock on a dynamic unload. `seam-exit-path` (motion runner) proves both: the hostile
script thrown out of frame 4 after `BeginScene` (the device is never destroyed) exits within its 90 s bound (it hung until
the 90 s timeout before the fix), and the same script run to completion, device destroyed and writer parked, lets the
fixture's `FreeLibrary` unload the proxy (`GetModuleHandle` of its full path returns NULL; before the fix it stayed
loaded).

Measured on the seam DLL under CrossOver Preview (bottle X3, FEX; case `seam-log-tiers` of `run_motion_output.py`,
final sources, the accepted full run): `X3M_FIXTURE_LOG_BENCH=10000` formats 10,000 rows of 1,634 B (a
`motion_output_frame`-sized row) through `log()` on the calling thread while the writer drains: mean 2.64 us per call,
worst call 52.5 us, 0 calls over 100 us, 0 over 1 ms, 27.1 ms for the burst (16.7 MB, four times the buffer), 0 rows
dropped; the writer made 336 `WriteFile` calls (2,400 us in all, worst 162.5 us, histogram 311 / 24 / 1 / 0 / 0 / 0 at the
10 us, 100 us, 1 ms, 10 ms, 100 ms edges) off the render thread. In the ordinary fixture runs with telemetry on (the
`log_writer reason=last_device` row): `--debug` 5,132 rows, 3,444 us, 0.67 us per row mean, worst call 57.5 us; `--perf`
5,063 rows, 3,138 us, 0.62 us per row, worst call 40.9 us. The worst single call seen in any run of this change was
315.1 us (one call of the `--perf` run in a partial run before the review fixes; not isolated: a preemption of the writer inside its short
lock section, or a first touch of a buffer page, are the candidates). The render-thread cost per frame in flight is
inferred from these per-row costs (0.60 us per row plus 1.25 ns per byte, fitted to the bench and `--debug` points of the final run) and run337's
row mix (section 4): always tier about
0.0002 us per frame (one `frame_end` row per 3,600 frames), `--perf` about 6 us per frame (six rows, 1.7 KB), `--debug`
about 28 us per frame (27 rows, 9.3 KB), about 34 us with both; the former per-frame `fflush` (0.1-0.6 us measured in
run337, but a disk sync, an antivirus scan or a page-in on the render thread) is gone.

### Evidence

- Build: `cmake --build build` 0 warnings; `check_no_x87.py build/d3d9.dll` PASS, 0 violations, 709 functions
  reachable (685 before), with the new roots `_x3m_session_log_open`, `_x3m_exception_witness@4`, `_x3m_session_end`
  (log()'s append is reached from the existing light-hook roots).
- `run_motion_output.py` (full, bottle X3, final sources after review): PASS, 231 cases, 346,382 checks: the 229
  committed cases at their counts (346,327 checks, none changed) plus `seam-log-tiers` at 47 and `seam-exit-path` at 8.
  An earlier full run stopped in `seam-thin-vote-hostile` (`RESTORE_DIFF fill indices`, then no exit): the fixture compared
  an index buffer pointer it had already released (the snapshot now holds its reference) and the pinned writer moved the
  D3D teardown to `ExitProcess` (the exit-path rule above); after both fixes the case passed its 95 checks in five of five
  separate runs. Per-case comparison
  `verification/results/logging-tiers/compare_motion_counts.py` against `../launcher-defaults/motion-cases-2026-09-25.json`
  (inputs committed as `motion-cases-2026-09-26.json`). The runner pins `X3M_SHADOW_TIMING=1 X3M_SHADOW_ROWS=1
  X3M_CAMERA_LOG=300 X3M_FRAME_END_STRIDE=300` so every oracle reads the rows at their pre-tier cadence.
- `seam-log-tiers` (new, 47 checks; 57 since the second step of 2026-09-26, which added the `--draw-trace` pair: 131 row names in both, ten stamp-family mode/site rows beyond `--perf`, and the debug group 137 with `X3M_FRAME_PHASES`; measured): the debug group and its 17 individual variables log the same 135 row names,
  the perf group and its 7 individuals the same 121 (unchanged by the second step; clocked rows excluded: none differed); the always tier without F8
  captures: 98 rows, 17,873 B for an 8-frame script, of which 69 rows / 12,363 B header before the first `frame_end` and
  28 rows / 5,421 B lifecycle events (Reset, release, destroy, resource identities, shadow refusals, the writer's park), no per-frame row, no
  telemetry row, no window; `log_open source=override previous=none` first in every run and `session_end exception=0` in every other run;
  the exception run (a continuable access violation of 0x0badf00d raised by the seam at attach, handled by nobody; the
  crash filter chains to the seam's resuming filter): one row `exception
  code=c0000005 ... module=other ... access_kind=0 access=0badf00d unhandled=1`, after the marker row logged before
  it and before the one logged after it, and `session_end exception=1`.
- Launcher: `manage.py --help` exit 0; `verification/results/launcher-defaults/compare_dry_runs.py` 5 PASS (default 124
  variables, stand 126, recorded stand 181); `verification/results/logging-tiers/dry_run_tiers.py` 8 PASS (default 150 →
  124, `--vanilla` 99 → 73, `--debug` / `--perf` 125, both 126; the 26 variables no longer sent are all logging variables,
  no functional variable changed); `options_tiers.py` 209 → 194 registered option strings.
- Host: `run_host_suite.py` 268 modules, 2,792 tests, 0 failing (267 / 2,782 at `9a668e81`); new `test_logging_tiers.py` (launcher groups, the 19 replaced options exit 2,
  inherited logging variables dropped, developer options only when given, every group switch read through
  `log_tiers.h`); `test_snapshot_x3_run.py` three new cases for the `x3m.log` layout.
- Other Wine runners: `run_d3d9_exports.py --dll build/d3d9.dll` PASS (writable: `log_open ... source=game
  previous=renamed`, the planted `x3m.log` moved to `x3m.prev.log`, a planted
  `x3m-<pid>.log` older than it deleted, `stale_removed=1`; read-only: `source=localappdata previous=absent`,
  `file=%USERPROFILE%\AppData\Local\x3-modern-renderer\x3m.log`, captures under `...\captures`, nothing written in the
  read-only directory; 8 checks each); `run_loading_trace.py` PASS (112 / 123 / 36,091 checks); `run_crypt_cache.py` PASS;
  `run_cursor_reassert.py` PASS (42 checks); `run_game_phase_cpu.py` PASS
  (12,025 checks, the 11 media cue cases reading the buffered rows); `run_temporal_pass.py` PASS and unchanged (546 samples, 278 restorations, the
  far-stabiliser and thin-region tables identical to the committed summary). Adapted but not rerun: the other eleven runners
  that now set `X3M_LOG_FILE` (`verification/probe/fixture_log.py`) and the two collide runners (`X3M_TELEMETRY=1`).

## Verification that would prove it

1. Host: `test_launcher_defaults.py` (new pins), `compare_dry_runs.py` (empty vs `--debug --perf` vs the old
   stand), `test_snapshot_x3_run.py`, `test_proxy_identity.py`; a host test over the DLL source asserting every
   tiered `GetEnvironmentVariableW` site goes through the group helper (the pattern the inventory grep used).
2. Fixture (Wine, one queue): `run_d3d9_exports.py`'s read-only-directory case extended to the rename, the busy
   fallback and `X3M_LOG_FILE`; the group-equivalence case (row-name set of `X3M_DEBUG=1` equals the union of
   the individual debug variables; same for `X3M_PERF=1`); the `log()` micro-benchmark; a crash case that raises
   an access violation in a fixture thread and checks the `exception` row and that the previous frame's rows are
   present.
3. Flights (user): one `x3run --direct` with no option, expected under 0.5 MB after ten minutes with no
   per-frame row but `frame_end` at 300; one `--perf`, expected about 60 MB per ten minutes with
   `frame_timing_slow` rows present; one `--debug --perf`, whose row-name set must equal run337's minus the
   removed options' rows and plus the three new rows. The row-name comparison is `row_volume.py` on both logs.

## Alternatives considered

- **One option, `--debug` with perf folded in.** Loses the small stutter report (2 GB per hour instead of
  360 MB) and puts the state hooks and phase stamps into every rendering-bug reproduction. Rejected.
- **Numeric levels (`--log-level 0/1/2`).** The two groups are not ordered: perf without debug is the common
  stutter case and debug without perf the common rendering case. Rejected.
- **Launcher expands the groups, DLL unchanged.** A player without the launcher cannot enable anything, which
  contradicts the requirement that the DLL defaults correctly with no environment. Rejected.
- **Always tier with a 1-per-minute telemetry summary** (the starting proposal). It needs `X3M_TELEMETRY`'s
  machinery on every player launch: the full loading-trace hook set, QPC reads around every instrumented call,
  the 4 Hz polls. `frame_end` at 300 gives the timeline for free. Rejected; revisit if a counter path is ever
  split out of telemetry.
- **Keep `session-*.log` names and add a `latest` copy or link.** Two files or a symlink (privileged on
  Windows); players copy the wrong one. Rejected; the snapshot restores the session name on the developer side.
- **Size-capped or size-rotated log.** The always tier is bounded by design; a cap drops the rows a crash report
  needs most. Rejected for now.
- **`--perf` per-frame rows at a stride (10) instead of 1.** About 50 MB per hour, but the fog and shadow cost of
  the exact stutter frame is lost (the `_slow` rows cover frame and phase time only). Rejected; a stride knob is
  the fallback if uploads prove too large.
- **`--taa-debug`, `--sun-occlusion-log` inside `--debug`.** 40 MB per F8 press and a readback per frame change
  what the reproduction measures. Kept explicit.

## Unknowns

- Per-row `log()` cost under FEX (fixture micro-benchmark above).
- `X3AP.exe` manifest / UAC virtualisation and install-directory ACLs on native Windows (PE resource read; a
  native run).
- Row volumes of `--shadow-sun-trace` and `--window-trace` (not in run337; inferred small and bursty).
- Whether any fixture depends on `camera_state` outside F8 frames at the 300 cadence (one file sets
  `X3M_CAMERA_LOG`; the 21 consumers read F8 or stride-1 logs); settled by the group-equivalence fixture run.
