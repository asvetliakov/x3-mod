# Configuration file `x3m.ini`: the proxy's user-facing settings, one schema, and the launcher

**Ratified 2026-09-26 (orchestrator, after the user's request):** INI `x3m.ini` next to the DLL, keys = the lower-cased
environment names without `X3M_`, values = the environment text; default < file < environment; `X3M_CONFIG` unset = the
game-directory file, `none` = ignore, `bare` = no file and the off column, path = that file; the launcher sends
`X3M_CONFIG=none` by default and `--config [PATH]` opts in; one schema file `tools/config/schema.py` generates the C++
table, the template `assets/x3m.ini` (every line commented, plain-words descriptions) and the schema host test; steps 1
and 2 are implemented together in the first candidate (the bare DLL must equal the launcher's default flight, otherwise a
player's install does not get the accepted look); steps 3 and 4 follow later; the zip is built by `tools/release/package.py`.


Design note, 2026-09-26, read-only survey at `da84d232` (nothing built, no Wine run). Decision: how the
release scenario works (the player unpacks `d3d9.dll` and a config file into the game directory, opens the
file and uncomments or edits a value), how every default moves into one place, what is generated from it,
and how `tools/manage.py` (developer-only) keeps reproducible launches. User requirements of 2026-09-26:
(1) INI or TOML "whatever is easier", found and loaded by default from the game directory; (2) all default
values in one config object; (3) a committed template with every value present but commented out, each with a
plain-words description; (4) the launcher can ignore or override the game directory's file.

## Recommendation

**INI, `x3m.ini` next to `d3d9.dll`, flat keys named after the environment variables** (`X3M_TAA_HISTORY_WEIGHT`
becomes `taa_history_weight`), **the value text identical to the environment value the launcher sends today**,
so the file is a third way of setting the same 165 variables the DLL already parses. A single schema file
(`tools/config/schema.py`, one entry per setting) generates the C++ table (`src/config/config_schema_inc.h`),
the template (`assets/x3m.ini`) and the fixture/test views; the launcher keeps its hand-written parser and a
host test proves it agrees with the schema. Precedence is **schema default < file < environment**;
`X3M_CONFIG` selects the file (`none` ignores it, `bare` is the fixtures' "absent means off" profile, a path
overrides). The launcher sends `X3M_CONFIG=none` unless `--config [PATH]` is given. The DLL's built-in
defaults become the launcher's promoted set (the Run 84 A stand) so the bare DLL flies the same configuration
as the launcher's default launch, which one host test enforces. Implementation lands in four steps, each
keeping the 229 committed motion cases and the two dry-run pins green; step 1 alone delivers the user-facing
feature through a drop-in resolver behind the 212 existing read sites; typed fields come family by family.

One-line answers to the brief's questions: (1) INI, `x3m.ini`, `[section]` headers for grouping only, flat
unique keys = lower-case env names without `X3M_`, values = env value text. (2) `tools/config/schema.py` is
the single source; generated now: the C++ table, the template, a `--check` host test and the launcher
cross-check; generated later: inventory tables; never: the launcher's argparse. (3) file < environment;
`X3M_CONFIG=none|bare|<path>`; the launcher ignores the file by default and `--config [PATH]` opts in; one
`config_open` row plus bounded `config_key` rows, never fatal. (4) Template sections graphics, hdr, shadows,
fog, camera, window, audio, loading, engine, logging; developer keys (tier internals, `--draw-trace`-class
attribution, fixture seams, marker variables, the ownership/motion-output prerequisites) are parseable but not
in the template; bottle, dll-source, dry-run, vanilla, install and voice-decoder setup have no key. (5)
`src/proxy/config.{h,cpp}` with a resolver `config::get` that is a drop-in for `GetEnvironmentVariableW`,
loaded first thing in `initialize_log` (inside `load_backend`, before any device), immutable afterwards;
renderer tunables take their constants from the generated header; four migration steps below. (6) Zip =
`d3d9.dll`, `x3m.ini`, `x3m-regenerate.exe`, `README.txt`; the DLL runs without the file; unknown keys are
logged once and ignored, so an old file keeps working under a new DLL.

## Current state (measured at `da84d232`)

| Fact | Value | How measured |
|---|---|---|
| Distinct `X3M_*` names read through `GetEnvironmentVariable[AW]` literals in `src/` | 165 | `grep -rhoE --exclude-dir=build 'GetEnvironmentVariable[AW]\(L?"X3M_[A-Z0-9_]+"' src/ \| sort -u \| wc -l` |
| Distinct `X3M_*` string literals anywhere in `src/` (comments, names passed to helpers) | 230 | same grep on `"X3M_[A-Z0-9_]+"` |
| `GetEnvironmentVariable` call sites in `src/` | 212 | `grep -rc` summed |
| Files with reads | capture.cpp (110 names), motion_output.cpp (11), loader.cpp (6), 36 more files with 1-5 each | per-file grep |
| Read sites by enclosing function | `initialize_log` 91, `hook_device` 24, `load_backend` 6, one `initialize`/`requested`/`wanted` per feature module; the only reads outside initialisation are the `X3M_FIXTURE_SUN_LANE_FAULT` fault injections under `#ifdef X3M_MOTION_OUTPUT_FIXTURE` | Python scan of enclosing function names |
| Helpers that read a name passed by the caller | `log_tiers.h env_flag`, `session_log.cpp environment`, `chase_camera.cpp`, `cull_small_parts.cpp read_setting`, `sampling_profiler.cpp`, `music_keep.cpp requested`, `sun_light_poll.cpp flag`, three lambdas in capture.cpp (`flag`, `flicker_setting`, `fog_env`) | grep of non-literal first arguments |
| "Absent means on unless exactly 0" keys | 4 (`X3M_FOG_DOCKED`, `X3M_FOG_HANDOVER_STEP/COLDFILL/PREFILL`) | `fog_default_on(` grep |
| Launcher | 182 `add_argument` calls, `REMOVED_VARIABLES` 48, `TIERED_VARIABLES` 41, `apply_promoted_defaults` fills 36 functional options with prerequisite chains (`fill(dest, value, when=...)`), env assembled imperatively (`env = os.environ.copy()` at `:1529`, then per-option `env.pop`/set through `:1800+`) | manage.py grep |
| `--vanilla` | sends explicit `=0` (not absence) for the modded switches and `camera=vanilla`, `resource_read=native`, `mesh_adjacency=native`; no proxy loads | `test_launcher_defaults.test_vanilla_sends_nothing_modded` |
| Inventory rows | section 1: 75, section 2: 8, section 3: 10, section 4 (removed): 15, section 5 (env-only): 20 | awk over `^\| \`` |
| Where the DLL learns its directory | `session_log::open` (`GetModuleFileNameW`, `session_log.cpp:299-303`), before the first log row | read |
| When reads happen | `initialize_log` runs from `load_backend` under `INIT_ONCE` on the first `Direct3DCreate9`, not in `DllMain`; `log_tier::init()` is its first statement | `loader.cpp:45-46`, `capture.cpp:2833-2835` |
| Existing "what is in force" rows | `proxy_options` and `proxy_environment` (one environment scan at attach, `proxy_identity.cpp:286-287`) | read |
| Fixture DLL placement | runners copy `build/d3d9.dll` or the seam DLL into a per-run directory (`run_motion_output.py:183-184, :6519`); the seam build links `build/` objects by glob except the recompiled sources (`build_motion_output.sh:46`) | read |
| Runners sharing `fixture_log.session_log_env` | 13 of 113 `run_*.py` | grep |
| Generated header precedent | `x3m_source_commit_inc.h` written by `tools/build/write_source_commit.py` at configure and build (`CMakeLists.txt:18-32`); the `motion_output_*_inc.h` fragments under `verification/probe/` are committed | read |
| Existing table-shaped tunable | `fog_mote_fields[]` (`{name, member pointer, min, max}` in `fog_mote_math.h:23-27`) | read |
| Packaging | no `assets/` directory, no zip script; `tools/regenerate/build.py --windows` builds `x3m-regenerate.exe` (PyInstaller); user docs live under `docs/user/` | ls, grep |

Two facts drive the design. First, the DLL's own defaults are the *off* set (`X3M_TAA` absent = off,
`capture.cpp:2942`), while the configuration a player is meant to get is the launcher's promoted set
(`apply_promoted_defaults`); the release scenario has no launcher, so the promoted values must become the
DLL's defaults, and the fixtures, which rely on "absent = off", need a profile that preserves that. Second,
the 212 read sites each carry their own parser, range check and log row, and the launcher's env assembly is
imperative with prerequisite chains, refusals and side effects (voice decoder, fog families), so neither can
be replaced by a table in one step without behaviour risk; the resolver must slot in beneath them.

## 1. Format and grammar

**INI.** The values already have a grammar: the environment value text (`1`, `0.9`,
`250,1500,7500,37500,150000`, `camera`/`screen`, `agx`/`identity`, `stored`, a path). An INI line
`shadow_cascades = 250,1500,7500,37500,150000` carries exactly the string the launcher sends and every
existing validator accepts, so the 212 sites, the inventory tables and the launcher help text stay true
without translation. TOML would impose a second value grammar (`[250, 1500, ...]`, typed booleans, quoted
strings with escapes) that the DLL must map back to the env text, and a conforming TOML reader is far above
100 lines (vendored header-only readers are 10-20 k lines); every deviation from the spec would be a
user-visible bug in a file players edit by hand. The INI reader is about 100 lines of C++ over documented
Win32 (`CreateFileW`, `ReadFile`) and needs no third-party code.

File: `<directory of d3d9.dll>\x3m.ini` (the module directory `session_log::open` already resolves; the same
place as `x3m.log`). Grammar, kept deliberately small:

- Encoding UTF-8, optional BOM skipped, LF or CRLF; hard bounds 64 KiB and 512 bytes per line (a longer file or
  line is refused with one row and the defaults stay).
- Whole-line comments start with `;` or `#` after optional blanks. No trailing comments: a `;` inside a value is
  part of the value (paths and lists never need one; this removes the only ambiguity in INI).
- `[section]` lines group keys for reading; the lookup ignores them (keys are globally unique), so a key in the
  wrong section still works and a user can drop the headers.
- `key = value`: key `[A-Za-z0-9_-]+`, lower-cased, `-` mapped to `_` (so `taa-history-weight` copied from a
  launcher option name still hits); value = text after `=`, trimmed, one matching pair of double quotes stripped
  (a path with spaces), no escapes. Empty value = the key is present with an empty string, which every site treats
  as invalid (logged, default kept).
- Key to environment name: `X3M_` + upper-case key. Bool-typed keys also accept `on/off/true/false/yes/no`,
  normalised to `1`/`0` before the site's parser; every other type is passed through verbatim.
- Duplicate key: the last wins and one row says so. Unknown key: one row, ignored.

Keys are the env names, not the launcher option names: the DLL never sees launcher names, an option is not
one variable (`--shadow-cascades` sets `X3M_SHADOW_CASCADES` and `X3M_SHADOW_SUN_POLL`, `--fov game` maps to a
value), and one derived name means one table, not two.

## 2. Single source of truth: the schema and what it generates

One file, `tools/config/schema.py`, a list of plain entries (Python data, importable by the launcher, the
generator and the host tests without a parser). One entry per setting:

| Field | Meaning |
|---|---|
| `key` | INI key, lower-case (`taa_history_weight`); the env name is derived (`X3M_` + upper) and an explicit `env` overrides only where they differ (expected: none) |
| `type` | `bool`, `int`, `float`, `int_list`, `float_list`, `enum`, `string`, `path` |
| `default` | the release value as env text (`"1"`, `"0.9"`, `"250,1500,7500,37500,150000"`); the value the DLL uses with no file and no variable |
| `off` | the neutral value = the DLL's pre-config "absent" behaviour (`"0"`, `"vanilla"`, `"native"`, `"1"` for `capture_frames`); used by the `bare` profile and by the template's "to turn this off" line |
| `range` / `choices` | numeric bounds or the enum set, as the site validates them today |
| `requires` | keys that must be on for this one to take effect (`volumetric_fog` requires `taa`, `hdr`, `shadow_replay_depth`, `shadow_cascades`); documentation and template only, the DLL's own gates stay the truth |
| `section` | template section (`graphics`, `hdr`, `shadows`, `fog`, `camera`, `window`, `audio`, `loading`, `engine`, `logging`) |
| `description` | one or two plain sentences for the template (style in section 4) |
| `developer` | `True`: parseable from file and environment but not in the template (tier internals, fixture seams, markers) |
| `launcher` | the launcher option that sends it, or `None` (cross-check only) |
| `since` | date the key appeared, for the upgrade story |
| `aliases` | old keys accepted for this one after a rename (empty until a rename happens) |

Generated by `tools/config/generate.py` (committed outputs; `--check` fails when regeneration would change a
byte, run by a host test like `inventory_scan.py` is today):

| Output | Path | Now or later |
|---|---|---|
| C++ table `constexpr config::Entry config_schema[] = {{key, L"X3M_...", type, default, off, min, max, choices}}` plus typed `constexpr` defaults (`config_default::taa_history_weight = 0.9f`) | `src/config/config_schema_inc.h` (under `src/config/` so renderer headers can include it without Windows headers) | now |
| Template, every key as `;key = default`, sections, descriptions, `requires` lines | `assets/x3m.ini` | now |
| Fixture/test view: the set of user-facing keys, defaults, off values | imported from `schema.py` directly (no generation) | now |
| Host cross-check of the launcher: every `X3M_*` in a default dry run is a schema key (or on a short launcher-only list), each value equals the schema default; every schema entry with `launcher` set is a registered option | `verification/analysis/test_config_schema.py` | now |
| Inventory sections 1, 3 and 5 as tables | generated into `docs/verification/launcher-options-inventory.md` between markers | later (after step 2, when the columns settle) |
| Launcher argparse registrations | never as a whole: the launcher's value is its prerequisite chains, refusals and side effects, which are code, not rows. Plain value knobs (`--taa-history-weight`-class, about 40 options whose only effect is one variable) can be registered from the schema in a later step, with `apply_promoted_defaults`, `--no-<name>` and the refusals staying hand-written | later, optional |

Why the launcher keeps its table: its 182 registrations include 36 promoted fills with `when=`
prerequisites, `--no-<name>` opt-outs that set `PROMOTED_OFF`, refusals of a dependant without its
prerequisite, and options that are not variables at all (bottle, dll source, voice decoder, fog families).
Generating that from rows would move the logic into the schema, which then stops being data. The
cross-check test gives the guarantee the user wants (one place for every default) without the rewrite: a
default that differs between the launcher and the schema fails the suite.

## 3. Precedence, `X3M_CONFIG`, the launcher, the log rows

Order: **schema default, then the file, then the environment**; a variable set in the environment always
wins, whatever the file says, so a fixture or the launcher is never surprised by a file. `X3M_CONFIG`:

| Value | Effect |
|---|---|
| unset | `<module dir>\x3m.ini` if it exists, else defaults only |
| `none` | no file; schema defaults plus the environment (the launcher's default) |
| `bare` | no file; the `off` column as the base layer, i.e. exactly today's "absent = off" (the fixture runners' setting) |
| a path | that file, exactly; a relative path is resolved against the module directory; a missing file is one row, not an error |

`bare` is the one production branch added for the fixtures, and it is one line in the resolver (which column
is the base); the alternative (a generated "explicit off" environment sent by every runner) would have to
reach 113 runners of which 13 share a helper, and would drift with the schema.

**The launcher ignores the game directory's file by default** and sends `X3M_CONFIG=none`. Reasons: the two
dry-run pins (`compare_dry_runs.py`, `dry_run_tiers.py`) and every flight record must not depend on a file in
the bottle; the launcher already sends explicit values for its 75 section-1 options, so a file could only act
on the keys the launcher does not send, i.e. invisibly. `--config` (no argument) leaves `X3M_CONFIG` unset so
the game directory's file is used (the release-scenario test), `--config PATH` sends the path (a scratch file
under `/tmp` for an A/B). An inherited `X3M_CONFIG` is popped like the tiered variables. `--vanilla` needs
nothing: no proxy loads.

Rows, all in the always tier, all at attach:

- `config_open file=<redacted path> source=game|override|none|bare|missing|refused keys=N unknown=N invalid=N duplicate=N bytes=B` — exactly one row per session; `refused` names the bound that was exceeded.
- `config_key key=<k> problem=unknown|invalid|duplicate|too_long value=<text>` — at most 32 rows, then `config_more=N`; `invalid` is reported by the site's parser through the resolver (a value that failed the site's range check), so the log names the key the user mistyped. Never fatal: the default stays.
- `config_file <k>=<v> ...` — the keys whose value came from the file, so a gameplay log shows what the file changed; formatted like `proxy_options` (that row already handles long lines). `proxy_options` keeps listing the environment as today, so the two rows together show the effective sources.

Values in the file are logged verbatim except the `path` type (`log_file`, `capture_dir` if it becomes a key),
which goes through `session_log::redact_path` like every other path.

## 4. What the template contains

Sections and the keys that belong in them (the exact membership is settled when the schema is authored; the
classification rule is what matters: a key is user-facing when a player can want to change it and its wrong
value is harmless):

| Section | Keys (env names lower-cased, examples) |
|---|---|
| `[graphics]` | `taa`, `taa_history_weight`, `taa_sharpen`, `taa_mip_bias`, `motion_jitter`, `taa_far_stabiliser`, `taa_thin_region`, `taa_thin_region_emissive`, `cull_small_parts` |
| `[hdr]` | `hdr`, `hdr_tonemap`, `hdr_look`, `hdr_exposure`, `hdr_ev`, `hdr_ev_manual`, `hdr_key`, `hdr_ev_min/max`, `hdr_adapt_up/down`, `hdr_dither`, `hdr_bloom`, `bloom_source_clamp`, `emission_source_gain`, `screen_emission_additive` |
| `[shadows]` | `sun_shadow_lane`, `shadow_replay_depth`, `sun_shadow_apply`, `shadow_cascades`, `shadow_cascade_sizes`, `shadow_cascade_records`, `shadow_cascade_drop_order`, `shadow_cascade_adaptive_c0`, `shadow_alpha_casters`, `shadow_caster_retention`, `sun_shadow_bias_*` |
| `[fog]` | `volumetric_fog`, `volumetric_fog_anisotropy`, `volumetric_fog_cards`, `volumetric_fog_range`, `volumetric_fog_march_scale`, `fog_motes_max_px`, `fog_docked`, `fog_handover_step/coldfill/prefill` |
| `[camera]` | `camera`, `fov`, `chase_pitch_down_deg`, `chase_offset_y`, `chase_distance_scale`, `chase_view_restore`, `sun_occlusion` |
| `[window]` | `window_monitor_rect`, `cursor_reassert`, `pause_key` |
| `[audio]` | `music_keep`, the voice-DMO fallback switch |
| `[loading]` | `crypt_cache`, `gz_buffer`, `resource_read`, `dat_handles`, `mesh_adjacency` |
| `[engine]` | `collide_sat_sse2`, `collide_memo`, `collide_box_cull`, `lod_occlusion`, `terran_station_lod`, `sun_flare_fix`, `light_map_far_fade` |
| `[logging]` | `debug`, `perf`, `capture_frames`, `log_file` |

Description style: one sentence saying what it does, one saying what the values mean, in words a player
without the project's vocabulary can follow; no internal names, no evidence pointers, no run numbers. Two
examples as they would appear in the template:

```ini
[graphics]
; Temporal anti-aliasing. Smooths edges and flicker by blending each frame with the
; previous ones. 1 = on, 0 = off. Needs hdr = 1 for the full effect.
;taa = 1

; How much of the previous frames the anti-aliasing keeps. Higher is smoother but
; moving objects leave slightly longer trails. 0.5 to 0.98.
;taa_history_weight = 0.9
```

```ini
[shadows]
; Distances (in game units) at which the five shadow maps hand over to the next one,
; nearest first. Larger numbers push the shadows farther out at lower detail.
; Five numbers separated by commas, each larger than the one before.
;shadow_cascades = 250,1500,7500,37500,150000
```

Developer-only (`developer=True`): parseable from file and environment, absent from the template, listed in
a `docs/` table instead: the tier internals (`telemetry`, `frame_timing`, `frame_phases`, the 41
`TIERED_VARIABLES`), `draw_trace` and its families (`telemetry_draw`, `game/pass/residual/light/loop_phases`),
`gpu_sync_timing`, `taa_debug`, `profile*`, `sun_occlusion_log/radius/curve`, the capture controls
(`capture_start`, `capture_dir`), the marker variables (`window_monitor_rect_default`), the ownership and
motion-output prerequisites (`ownership`, `object_trace`, `object_lifetime`, `motion_output`, `depth_copy`,
`finite_positions`, `shadow_replay_candidates`, `sun_shadow_lane` stays user-facing as the shadow switch), and
every `X3M_FIXTURE_*` seam (section 5 of the inventory), which stay **environment-only**: the resolver refuses
them from a file (one `config_key problem=env_only` row) so a player's file cannot arm a seam.

No key at all (launcher concepts): `--bottle`, `--dll-source`, `--dry-run`, `--vanilla`, `--direct`,
`--install`/rollback, the voice-decoder plugin setup (GStreamer, CrossOver-only), the fog-family
regeneration, `--capture-frames`' F8 trigger is a key but the capture arming that the launcher's
`X3M_CAPTURE_START=999999` expresses is developer-only.

## 5. The `Config` object and the migration

`src/proxy/config.h` / `config.cpp` (compiled into `d3d9` in `CMakeLists.txt:75`; the seam build picks the
object up from `build/` by glob):

```cpp
namespace x3m::config {
void load(HMODULE module) noexcept;                 // once, first statement of initialize_log
DWORD get(const wchar_t* env_name, wchar_t* out, DWORD capacity) noexcept; // drop-in for GetEnvironmentVariableW
const char* source(const wchar_t* env_name) noexcept; // "env" | "file" | "default" | "bare" | "" for the rows
struct Values;                                      // step 3: typed fields generated from the schema
const Values& values() noexcept;
}
```

`get` has the Win32 contract the sites already handle (length written, required size when the buffer is
short, 0 when nothing is set): environment first (`GetEnvironmentVariableW` as today), else the file's value,
else the schema default (or the `off` value under `bare`), else 0. It is a binary search over the generated
table (165 entries, sorted at generation) and a `wcscpy`-class copy, no allocation, no lock: `load` fills a
fixed array of value pointers into one heap block read once, and after `load` returns nothing is written
again. `load` runs inside `load_backend` (`INIT_ONCE`, before any device, before the first render call), so
the reads in `hook_device` and in every module `initialize` see the loaded state; `log_tier::init()` moves
after `load` so `debug = 1` in the file switches the tier (the "enable logging, reproduce, upload the log"
instruction of `logging-tiers.md` then works without the launcher). An entry read before `load` (there is
none today; the fixture fault reads are under `#ifdef`) falls through to the environment, never crashes.

The renderer tunables (`fog_strength_default`, `FogMoteTuning` members, `FogLookTuning`, the cascade bounds)
stay where they are but take their literal from `config_default::` in the generated header, so a default
exists in one place and the renderer's validation code is untouched. Ranges in the schema are the ranges the
sites validate; the generator emits them so the typed parse of step 3 and the site parser cannot disagree.

Migration, each step a checkpoint with its own acceptance:

| Step | Change | Acceptance (all must hold) |
|---|---|---|
| 1. Resolver and file, behaviour-neutral | `schema.py` with every read name (165), `default` = the current absent behaviour for this step, `off` = the same; generator, `_inc.h`, `assets/x3m.ini`; `config.{h,cpp}`; the 212 sites and the nine helpers call `config::get`; `X3M_CONFIG` (`none`, `bare`, path); rows; launcher sends `X3M_CONFIG=none`, `--config [PATH]`; `fixture_log.session_log_env` also returns `X3M_CONFIG=bare` and the 100 runners that build their environment by hand get the same line | build 0 warnings; `check_no_x87` 709/0 unchanged; host suite; `compare_motion_counts.py` 229 cases / 0 changed; `compare_dry_runs.py` and `dry_run_tiers.py` updated to expect exactly one new variable (`X3M_CONFIG=none`) and otherwise no change; new seam case `seam-config-file`: a file with every user-facing key at a valid non-default value plus 3 unknown, 2 invalid, 1 duplicate and 1 env-only key, run with `X3M_CONFIG=<path>`, asserting `config_open keys= unknown=3 invalid=2 duplicate=1` and the `*_mode` rows reflecting the file, and a second run with one key also in the environment asserting `source=env` for it; host test: `generate.py --check` clean, template keys = schema user-facing keys, every template value = schema default, no `GetEnvironmentVariableW(L"X3M_` outside `config.cpp` and the `#ifdef` fault reads |
| 2. Release defaults | `default` column set to the promoted values (the stand), `off` unchanged; the bare DLL now flies the launcher's default configuration | host test: the launcher's default dry-run environment, resolved through the schema, equals the schema defaults key by key (every sent variable is a schema key and equal to its default, or on the launcher-only list); motion cases unchanged (they run under `bare`); one flight of the bare DLL by the user (`--config` launch of the shipped template with nothing uncommented) whose `proxy_options`/`config_file` rows and look match a default launch |
| 3. Typed fields | `config::Values` generated (`bool taa; float taa_history_weight; ...`), one generated parse with the schema ranges logging `config_key problem=invalid`; sites migrate family by family (fog tunables first, then shadows, TAA, HDR, chase, loading), deleting their parsers and fallbacks | per family: that family's fixtures unchanged, the seam case's expected rows updated only for row wording; the "no site fallback" guard is the host test that every default literal in the family's source is gone (grep list per family) |
| 4. Generated views | inventory sections 1/3/5 generated between markers; optionally the plain value-knob options registered from the schema | `inventory_scan.py` replaced by `generate.py --check`; dry-run pins unchanged |

Step 1 is the whole user-facing feature; steps 2-4 are ordered by value over risk, and the project can stop
after 2 without leaving the file half-working.

## 6. Release packaging and the upgrade story

The zip: `d3d9.dll`, `x3m.ini` (the generated template, all values commented, the header line naming the
version/commit it was generated from), `x3m-regenerate.exe` (from `tools/regenerate/build.py --windows`), and
`README.txt` (a short version of `docs/user/`: unpack next to `X3AP.exe`, run `x3m-regenerate.exe` once,
edit `x3m.ini` to change a setting, send `x3m.log` when reporting a problem). The DLL must run without the
file, and does: every default is compiled in from the schema; the template only documents them. A packaging
script `tools/release/package.py` (new) assembles the zip from `build/d3d9.dll`, `assets/x3m.ini` and the
regenerate bundle, records the DLL hash and source commit in the zip's `README.txt`, and refuses when
`generate.py --check` fails, so a template can never ship stale.

Upgrade: the template is regenerated every release; the player's edited `x3m.ini` is never rewritten by the
DLL (the DLL only reads it). A newer DLL under an older file: new keys take their defaults, removed keys are
`config_key problem=unknown` rows, a renamed key is accepted through `aliases` with a row naming the new
key. An older DLL under a newer file: unknown keys are ignored the same way. Nothing is fatal, and the
`config_open` row tells support what the file contributed.

## Cost on the hot path

None per frame: every production read happens in `initialize_log`, `load_backend`, `hook_device` or a
module's `initialize`, all at attach or device creation (measured by the enclosing-function scan above), and
the tier flags stay cached in `log_tier::init()`. Attach: one `CreateFileW`/`ReadFile` of at most 64 KiB, a
single-pass parse, and 212 binary-search lookups in a 165-entry table, inferred well under 1 ms against an
attach that already hashes the 56 MB DLL (`proxy_identity attach_us`). Memory: one block of at most 64 KiB
plus the pointer table. No lock after `load`; the sites keep their existing `CaptureLock` context.

## Native Windows behaviour

Documented Win32 only: `GetModuleFileNameW` (already used for the log), `CreateFileW`, `ReadFile`,
`GetEnvironmentVariableW`; UTF-8 decoded by the parser itself (keys are ASCII, values are copied as-is; a
path value goes through `MultiByteToWideChar(CP_UTF8)`). The game directory may be read-only on Windows
(the `%LOCALAPPDATA%` fallback of the log), which does not affect reading the file; no fallback location
is searched for the config (one place, next to the DLL, is the contract the README states). Not verified
natively, like the rest of the proxy (`platform-portability.md`); the parser is plain C++ with no Wine
assumption, and the seam case exercises the same code under Wine.

## Verification that would prove it

- The seam case `seam-config-file` above (file precedence, unknown/invalid/duplicate/env-only handling, environment wins), run through `run_motion_output.py` under the Wine lock.
- `verification/analysis/test_config_schema.py`: `generate.py --check`; template keys equal the schema's user-facing keys; every template value equals the schema default; the launcher's default dry run resolves to the schema defaults (step 2); every schema `launcher` field is a registered option; no `GetEnvironmentVariableW(L"X3M_` outside `config.cpp`.
- `compare_dry_runs.py` / `dry_run_tiers.py` with the single expected delta `X3M_CONFIG=none`.
- `check_no_x87` unchanged (the parser uses `wcstof`/`wcstoul` like the sites; no x87 in the new object).
- A user flight of the bare template through `--config` after step 2, compared to a default launch by its `proxy_options`, `config_open`, `config_file` rows.

## Alternatives considered

- **TOML.** Loses on the second value grammar and parser size (section 1); "whatever is easier" is INI.
- **JSON.** No comments; the template's whole point is commented values.
- **Keys named after launcher options.** Not a bijection with the variables the DLL parses; a second name table to maintain; the DLL never sees option names.
- **File over environment.** Would let a file in the bottle change a fixture or a launcher flight silently; the launcher already drops stale exports itself, and the developer channel must always win.
- **Launcher uses the game-directory file by default.** Breaks the dry-run pins' meaning and makes a flight depend on a file the record does not capture; `--config` gives the release-scenario test on demand.
- **Ship `x3m.ini` with values uncommented and keep the DLL's off defaults.** Violates requirements 2 and 3 and makes the file load-bearing (a deleted file changes the game); defaults belong in the DLL.
- **Typed `Config` first, one big step.** 212 sites with bespoke validators and rows; a behaviour-neutral drop-in resolver delivers the feature in one checkpoint and the typed fields follow per family with their fixtures.
- **Generating the launcher's argparse from the schema.** The launcher's logic (prerequisite fills, refusals, side effects) is code; the cross-check test gives the one-place guarantee without turning the schema into a program.
- **A generated explicit-off environment for the fixtures instead of `bare`.** Would have to reach 113 runners (13 share a helper) and drift with the schema; `bare` is one variable and one line in the resolver.
- **Registry or `%LOCALAPPDATA%` for the file.** The user asked for a file in the game directory; a second location makes "which file is in force" a support question.

## Unknowns

- The final split of the 165 names into user-facing and developer keys is settled while authoring the schema; the count above is measured, the section membership is proposed. Two names may turn out to be the same setting under two spellings (helper-routed reads); the generator's uniqueness check will show it.
- Whether any read site depends on `GetEnvironmentVariableW` returning 0 to mean "not the launcher" in a way a schema default would change before step 2 (the `*_DEFAULT` marker pattern: `X3M_WINDOW_MONITOR_RECT_DEFAULT` tells the row whether the value was a default). Step 1's `default = off` column keeps them neutral; step 2 must give such markers `source`-aware handling (the row can ask `config::source`). A grep for `_DEFAULT"` sites during step 1 settles the list.
- Parse cost is inferred, not measured; the `config_open` row can carry `us=` like `proxy_identity attach_us` at no cost.
- Native Windows: unverified, as everywhere.
