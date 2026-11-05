# Host suite runner

Ledger for the host test suite itself: how it is run and which
linear-named modules stay. Feature results stay in their own ledgers.

## Running

```sh
/usr/bin/python3 verification/probe/run_host_suite.py            # parallel, default jobs
/usr/bin/python3 verification/probe/run_host_suite.py --jobs 8
/usr/bin/python3 verification/probe/run_host_suite.py --serial   # reference ordering
```

The runner starts one `python -m unittest <module>` worker per discovered
module, longest module first from `verification/probe/host_suite_durations.json`
(a scheduling hint only; refresh with `--write-durations`). Workers run with cwd
at the repository root and `verification/probe` plus `verification/analysis` on
`PYTHONPATH`, which is what the canonical discover command gives a test module,
so `verification.analysis.*` and probe imports resolve identically. Use the
interpreter that has NumPy: `/usr/bin/python3` on the project Mac (`sys.executable`
is what the workers inherit). Exit code is non-zero on any failure, any error,
or any module that runs zero tests.

The reference command stays

```sh
PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_*.py'
```

and discovers the same modules.

Parallel safety audit (2026-09-21): no module under `verification/analysis`
writes into the repository tree, into `verification/results`, or to a fixed
temp path; every host fixture is compiled and run inside its own
`tempfile.mkdtemp`/`TemporaryDirectory`. The runner therefore needs no serial
group today; `SERIAL_GROUP` in the runner is the place to add one if a module
ever acquires a shared output path.

Microsoft Defender on this Mac quarantines synthetic PE files: a vanished temp
file or `objdump: Operation not permitted` in a worker is Defender, not the
runner.

## WSL2 host (since 2026-10-11)

The suite runs on the WSL2 Ubuntu host that builds the DLL; the game is native
Windows at `C:\X3` (`/mnt/c/X3`). What the suite needs there:

- `verification/probe/bottle.py` `game_dir()` falls back to `X3_DIR` (default
  `/mnt/c/X3`) when no CrossOver bottle exists; `exe_identity.DEFAULT_EXE` and
  the chase-camera / sun-occlusion verifiers take their EXE from it, so the
  identity, site-claim and anchor modules read the shipped `X3AP.exe` there.
- `/tmp` is a 5.9 GB tmpfs. A full `/tmp` fails about 30 modules at once with
  `No space left on device` (compiles, links, temp copies); release staging
  copies (`/tmp/x3m-release-*`) and old session scratchpads are the usual
  culprits. Check `df -h /tmp` before blaming a module.
- The local shader corpus `/tmp/x3-shader-sweep/programs` (751 programs,
  untracked, game-derived) is rebuilt from the installed archives in about
  10 s and cross-checked against the tracked inventory
  ([shader-sweep.md](../reverse-engineering/shader-sweep.md), "Restoring the
  local corpus"):

  ```sh
  python3 tools/analysis/index_shaders.py /mnt/c/X3 --output /tmp/x3-shader-index-expanded.json
  python3 tools/analysis/sweep_shaders.py extract /mnt/c/X3 --index /tmp/x3-shader-index-expanded.json \
      --raw-directory /tmp/x3-shader-sweep/programs --manifest /tmp/x3-shader-sweep/manifest.json
  python3 tools/analysis/restore_shader_sweep.py --dry-run '/nonexistent/*'   # 751/751 inventory SHA-256s, pins ok
  ```

  Without it eight corpus modules run zero tests, which the runner counts as
  failing. The native captures folder `/mnt/c/X3/x3-modern-captures` holds 47
  of the same programs; the variant generators count a program once across
  both folders.
- `tools/manage.py launch --dry-run` needs no Wine binary; a real launch still
  refuses without CrossOver.
- Host-only fixtures that the x86_64 compilers broke (GCC 15 `-Werror`
  indentation and `scanf` results, clang 21 `<initializer_list>`,
  LeakSanitizer on an intentional leak, `rsqrtss` selected by `__SSE__` on the
  host) were fixed on 2026-10-27 together with the fixtures that had fallen
  behind production while the EXE path did not resolve
  (commits 82368d79..b331737a).

State on 2026-10-27 (measured): 306 modules, 3,250 tests, 2 failing modules.
`test_engine_ribbons` and `test_engine_effects` bind their retained fixture
records to production source hashes (`engine_ribbons_core.h`,
`engine_ribbons_pass.{h,cpp}` and the engine-effects set changed since the
last Wine run); they stay red until `run_engine_ribbons.py` and
`run_engine_effects.py` run again, which needs a D3D9 fixture host (Wine on the
Mac, or a native runner that does not exist yet); accepted beside their
records on 2026-11-03 (next section).

## Wine-record-bound tests (2026-11-03)

These modules bind a retained Wine fixture record (`verification/results/bottle-X3/`)
to the SHA-256 of its production sources. Since 2026-10-11 there is no Wine host,
so the records cannot be re-recorded while the sources keep changing natively:

| Module | Record (Wine run) | Sources drifted |
| --- | --- | --- |
| `test_engine_effects` | `engine-effects/summary.json` (cd479d025, 2026-10-09) | 12 of 21 |
| `test_engine_plumes` | rebound 2026-11-05 to `native-windows/engine-plumes/summary.json` (exact hashes; Wine record kept as provenance) | none |
| `test_engine_ribbons` | `engine-ribbons/summary.json` (0cf873a6b, Run140) | 5 of 14 |
| `test_engine_shimmer` | `engine-shimmer/summary.json` (0cf873a6b, Run140) | 1 of 9 |
| `test_fov_patch_result` | `fov-patch.json` (ecf0215ee, 2026-09-26) | 1 of 7 |
| `test_engine_effects_sites` | `engine-effects-patch.json` (203c5e984, Run124) | 1 of 7 (`engine_effects_option.h`, 17ab85a84) |
| `test_occlusion_cull` | occlusion records (2026-10-08) | accepted 2026-11-02 |

Pattern (as `test_occlusion_cull`'s 2026-11-02 constants): a dated constant
`SOURCES_SHA256_2026_11_03` names the current hash of each drifted file, and the
check accepts either the record's own hash or that one; the record stays as
provenance and nothing is skipped. A further native change fails the test again
and needs a new dated entry with its commit range. The engine-effects entries for
`engine_far_jets.{h,cpp}` and `motion_output_engine_plumes_inc.h` were taken while
those files were under concurrent change and move on the next merge. The next
re-record (and removal of the constants) needs a Wine or native D3D9 fixture run
of `run_engine_effects.py`, `run_engine_plumes.py`, `run_engine_ribbons.py`,
`run_engine_shimmer.py` and `run_fov_patch.py`.

## Native Windows fixture host (2026-11-05)

`verification/probe/native_host.py` runs a MinGW-built D3D9 fixture on the
Windows side of the WSL2 host through interop (binfmt `WSLInterop`): the real
D3D9 runtime and GPU driver, no Wine. It checks for `i686-w64-mingw32-g++` and
interop, refuses while `X3AP.exe` / `X3FL.exe` shows in `tasklist.exe` (failing
closed when the list cannot be read) and refuses to execute a game image. It
passes paths as Windows paths (`wslpath -w`), forwards fixture variables via
`WSLENV`, and writes records under `verification/results/native-windows/<fixture>/`.
The record's `host` block holds the name `native-windows`, the Windows version
(`cmd.exe /c ver`), the adapter as the fixture's runtime reports it through
`GetAdapterIdentifier` (vendor, device, subsystem, revision, WHQL level, driver
version, description, driver DLL), and the path, size, SHA-256 and PE file version
of the `d3d9.dll` and `d3dx9_37.dll` the fixture loaded (`GetModuleFileNameA`;
D3DX is the game's `C:\X3\d3dx9_37.dll`). The pilot is `run_engine_plumes.py`:

```sh
python3 verification/probe/wine_lock.py python3 verification/probe/run_engine_plumes.py --host native
```

It builds the same binary with the same flags as the bottle path, and the bottle
stays the default (`--host bottle`; it still requires `X3M_FIXTURE_BOTTLE=X3`), so
neither host falls back to the other. The record has the Wine schema with `bottle: null` plus
`host`. `--disc-ab` writes `native-windows/engine-plumes/plume_disc_ab.json`;
`--dump-images` and `--wine-env` are bottle-only. The fixture prints `ADAPTER`,
`ADAPTER_DESC`, `ADAPTER_DRIVER`, `D3D9_MODULE` and `D3DX_MODULE` rows on both
hosts, and the case parser ignores them. Unit test: `test_native_host` (inline fixtures, no interop).

Identity (measured, 2026-11-05):

| Field | Value |
| --- | --- |
| Windows | 10.0.26300.9818 |
| Adapter | NVIDIA GeForce RTX 5070 Ti, vendor 0x10de, device 0x2c05, subsys 0x276219da, rev 161, WHQL 0 |
| Driver | `nvldumd.dll` 32.0.16.2114 |
| `d3d9.dll` | `C:\WINDOWS\SYSTEM32\d3d9.dll`, 10.0.26100.9818, 1,799,736 B, sha256 `b5fe0ccd…` |
| `d3dx9_37.dll` | `C:\X3\d3dx9_37.dll`, 9.22.949.2248, 3,786,760 B, sha256 `c2ccb84c…` |

The first native run stopped at the first rendered case with `FATAL quad draw
8876086c`. The fixture's `Scene::frame` (and the dump mode's sky quad) drew
outside `BeginScene` / `EndScene`, which Wine accepts and native D3D9 refuses
with `D3DERR_INVALIDCALL`. Both now open their own scene; no caller has one open
at that point. The UNC working directory (`\\wsl.localhost\...`) worked; no
Windows-local copy is needed.

Outcome (measured; `native-windows/engine-plumes/summary.json`, commit 0e90a8f46,
1 min 43 s including the build; compared by
`verification/results/native-windows/engine-plumes/compare_wine.py`): 322 checks,
the same labels as the Wine record (Run140, 0cf873a6b), **5 failed, all in the flow
case**. The world displacement of the flow at value 1,500 has ratio 0.853 at 1920
and 0.862 at 5120 (gate 1 ± 0.05; Wine 1.000). At value 10,000 the ratio is
1.256 at 1920 (Wine 1.000); 0.957 at 5120 passes. Peak correlation is 0.95–0.98
against 0.999 under Wine, and the two `flow_same_world_speed_600_1500` checks
fail as a result. Values 100 and 600 match Wine to four digits, and a
`--only flow` rerun gave the same digits. Every other check passes as on Wine:
core survival min 0.9781 (Wine 0.9783), dark trail 0 px, ps slots 924, colour
error 9e-05. Stage GPU cost in ms, Wine → native:

| Case | 30 nozzles | 100 | 300 (250 far) |
| --- | --- | --- | --- |
| 1920x1080 | 0.155 → 0.013 | 0.240 → 0.023 | 0.489 → 0.035 |
| 5120x1440 | 0.021 → 0.019 | 0.038 → 0.023 | 0.139 → 0.042 |

CPU build cost in µs is close (100 records: 12.99 → 10.68; 1,024: 140.1 → 146.0).
The record does not pass, so `test_engine_plumes` stays bound to the Wine record
and to `SOURCES_SHA256_2026_11_03`. The flow failure at high values is open: it
is deterministic on this GPU and needs a diagnosis, not a looser gate.

Fixed 2026-11-05: the flow failure was the plume value noise's lattice index under a fused `mad`; with the source fix the native record passes 322/322 and `test_engine_plumes` is bound to it ([engine-effects.md](engine-effects.md), "Native flow defect").

## Tests that read production source

The C/C++ tree is clang-formatted (README, "Formatting"), so a test that pins
production spelling reads the file with `source_text(path)` from
`verification/analysis/source_text.py`, not `Path.read_text()`. The returned text
matches fragments whitespace-insensitively (`in`, `count`, `find`/`index`,
`split`, `replace`; `end(fragment, start)` for the raw end of a match), keeps raw
positions for slicing, and matches a fragment holding a newline against the raw
layout first. Regexes and line-based checks run on the raw text and must allow
the formatted spacing themselves.

## Retired modules (deleted)

The 23 retired modules (`--linear-materials`, the converted hull material law,
and the full-surface `--linear-emissions` bracket rejected 2026-09-15) and their
skip hook `verification/analysis/retired_tests.py` were deleted on 2026-09-22 in
cleanup batch 3 (`docs/architecture/cleanup-inventory-2026-09-22.md`, section G).
Both options are still compiled into the DLL behind default-off flags. The
runner keeps `--include-retired` as a no-op. Host suite before and after: 233
modules / 2,311 tests.

Linear-named modules that stay, because they cover flown or compiled behaviour:

| Module | Subject |
| --- | --- |
| test_linear_emission_pass_programs | byte-exactness of `linear_emission_copy_clear_inc.h` transfer arrays compiled into the DLL |
| test_linear_emission_pass_host | `linear_emission_pass.cpp` still compiles against the public host D3D interfaces |
| test_linear_emission_sm1_transformer | the nine SM1 screen pairs: `--screen-emission` / `--screen-emission-additive`, flown |
| test_linear_emission_sm1_report | SM1 parity/capability gates of the same flown pairs |
| test_linear_emission_sm1_packed_report | packed SM1 ordering/domain/resource gates, flown |
| test_linear_emission_source_gain | `--emission-source-gain`, installed and flown |
| test_linear_emission_hull_gain | hull guide-light gain (`--hull-emitters`), flown |
| test_linear_sun_share | the share-lane generator over the 108 opaque hull programs; the sun-shadow lane is flown on original shading |
| test_linear_cutout_contract | cutout identity plus live capture/TAA contracts (depth-surface rebind, history union, retry) |
| test_linear_cutout_live_report | cutout live report; the cutout pairs carry the flown lane share |
| test_alpha_test_caps_probe | public capability query arguments, not a linear-material subject |

## Timing

Repeated work removed on 2026-09-21 (no assertion changed):
`verify_submit_phase_sites.decode`, `raw_scan` and `data_reference_hits` are
memoised on the image bytes / file identity. The site tests call `inspect()`
about seventy times on the same 55 MB image, and each call re-scanned the whole
`.text` section and re-decoded the EXE through `objdump`.

`test_fog_density_cache.test_window_recentre_readiness_logic` is host compute
(two 128³ grids filled node by node in the deterministic stepped worker), not
waiting; its two binaries are already compiled once per class in `setUpClass`.
It was left as it is.

## Wine lock: orphaned fixtures

A fixture that crashes under Wine gets `winedbg --auto` attached; if its runner
then times out, the fixture can survive as an orphan and `wine_lock.py`'s
preflight refuses every later Wine command (exit 75). The preflight names such
an orphan (PPID 1, image path under a `verification/probe/build/` directory on
any drive letter, worktree trees included, not `X3AP.exe`) and how to end it,
but never ends it, since its owner may still be examining it. A native
command (`less`, `tail`, `python3`) naming a fixture path never matches: the
line must start with a drive-letter DOS path or be a Wine launcher line. In the
2026-09-24 incident the fixture showed as a `Y:\` worktree path and its
`winedbg --auto` had PPID 1 as well, not a Unix child of the fixture (ps rows
quoted in `verification/analysis/test_wine_lock.py`).
`python3 verification/probe/wine_lock.py --clean-orphans` takes the lease (so no
live runner owns the orphan), sends SIGTERM to exactly those orphans, their
`winedbg --auto` descendants and, only while no `X3AP.exe` runs, every PPID-1
`winedbg --auto`, then SIGKILL to any still alive after 10 s; it never signals
PID 1 or less, itself or its ancestors, and reports what remains. Given alone
it exits 0 when the preflight then passes and 75 otherwise; given before a
command, a refused preflight exits 75 and otherwise the exit status is the
command's. Ending every PPID-1 `winedbg --auto` while the game is down is by
design under the lock contract: every Wine command runs under `wine_lock.py`,
so while the lease is held no other fixture can own such a debugger; a Wine
program started outside the lock is not protected. The game, a debugger while the game is up and fixtures
outside the build tree are reported or refused, never killed. Runners
that start fixtures through `fixture_process.run` (today `run_motion_output.py`)
end the case's own fixture processes (the same SIGTERM, then SIGKILL) on a
per-case timeout, plus any PPID-1
`winedbg --auto` that was not up when the case started (none while the game
runs), and put the cleanup line into the case's failure output (`verification/probe/fixture_process.py`).
