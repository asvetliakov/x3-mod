# Fixture runners and CrossOver bottles

Status 2026-09-12 (evening): the runner-side switch is implemented and the
five suites were run once under the X3 bottle (records under
`verification/results/bottle-X3/`, capture logs over 1 MB ignored via
`.gitignore`). Two FEX-specific limitations were found (the sampler's thread
context and the CRT's text formatting of floats); see the record below.

## Two bottles

| Bottle | WineArch | Wine | Environment lines recorded | Game files |
|---|---|---|---|---|
| `Steam` | `win64` | x86_64 Wine under Rosetta | `WINEMSYNC=1` | `drive_c/X3/X3AP.exe`, `d3dx9_37.dll` (same bytes as X3) |
| `X3` | `arm64` | CrossOver Preview native arm64 Wine, FEX x86 emulation | `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1` | same |

The game now runs in `X3` (`tools/manage.py` defaults to it, `X3M_BOTTLE`
overrides). The fixtures keep `Steam` as their default so every record under
`verification/results/` stays comparable with the committed evidence.

## Runner-side selection (`verification/probe/bottle.py`)

- `bottle.BOTTLE = os.environ.get('X3M_FIXTURE_BOTTLE', 'Steam')`; every runner
  passes `'--bottle', bottle.BOTTLE, '--no-update'` (48 scripts: the `run_*.py`
  runners, `mesh_run.py`, `temporal_run.py`, and the three `verify_*.py` readers).
  `wine_args()` returns the same triple for new code.
- `bottle.results_dir(root)`: `verification/results` for Steam, otherwise
  `verification/results/bottle-<name>/` (created on demand). Runners write and
  read back their own records there. Generated *inputs* that are not per bottle
  stay on the shared path (`rigid-motion-pixel-program.json`,
  `shader-profile-registry.json`).
- `bottle.bottle_dir()` / `bottle.game_dir()` replace the hard-coded
  `Bottles/Steam/...` paths (native `d3dx9_37.dll`, `drive_c/windows/syswow64`,
  `dosdevices`), so the native module hashes recorded by a run come from the
  bottle that ran it.
- `bottle.describe()` is stored as `bottle` in every summary JSON the runners
  write: `{name, wine_arch, environment{FEX_X87REDUCEDPRECISION, WINEMSYNC},
  cxbottle_conf}`, read from the bottle's `cxbottle.conf`. `bottle.label()` is
  the text form used by `sampling-profiler.txt`'s header. Records without the
  key predate this change and were all produced in the Steam bottle.
- `run_sampling_profiler.py`'s idle-wait now skips `pgrep -fl` continuation
  lines of multi-line commands (they are not `pid args` rows and raised
  `ValueError` before any fixture started).
- The fixture logic, expectations and `src/` are unchanged.

## X3 validation plan and record

Run one suite at a time with `X3M_FIXTURE_BOTTLE=X3`, every Wine command
wrapped in `verification/probe/wine_lock.py`; results land in
`verification/results/bottle-X3/`. The Steam references are the HEAD records
under `verification/results/` (the working-tree copies of
`temporal-pass-summary.json` and `loading-trace-mesh-summary.json` are mid-edit
by other agents). Runs: 17:53-18:10 on 2026-09-12, while three other agents
rebuilt `build/d3d9.dll` concurrently.

| Order | Suite | What it stresses under FEX/arm64 | X3 result | Steam reference |
|---|---|---|---|---|
| 1 | `run_sampling_profiler.py` | thread suspension, `GetThreadContext`, TEB reads from the sampler thread | **FAIL 17/22**: the five attribution checks (`a_leaf_attribution`, `a_caller_pair`, `b_frame_wait_b`, `c_leaf_attribution`, `c_scan_caller_pair`) fail because every sample is the creation-time context (limitation 1 below); the mechanics pass (1173 ticks, 3092 samples, 0 dropped, 2 suspend failures, 0 context failures, stop bounded, handles/threads released). Tick mean 282 us, max 10744 us, report 2963 us, refresh 10501 us; overhead a 3.9 %, c 3.7 %, main 8.0 %; walls 6.92 s off (first Wine start of the bottle: wineserver + msync bootstrap) / 3.27 s on | pass, 22/22 checks; overhead a 10.5 %, c 5.4 %, main 0.9 %; tick mean 558 us, max 9144 us, 940 ticks, 2536 samples, 0 dropped; walls 4.19 s / 3.43 s |
| 2 | `run_loading_trace.py` | IAT + vtable hooks, x87 `CpuState` under reduced precision | **pass for the committed cases**: loading-trace 85/85, loading-mesh 123/123, admission witness `0,0,0,0,0,0,0,1` as on Steam; every check line identical, the only differing lines are `coverage_begin` and the benchmark. The suite as a whole exits failed on the uncommitted fourth case `mesh-adjacency-cache-off` (2179 checks; `Default policy differs from native on tie evidence`, `nan-position` mismatches=6) exactly as the Steam working-tree record does: the mesh-adjacency agent's in-flight work, not a bottle difference. FEX cost: `hooked_us_per_call` 1.364 vs 0.378 (3.6x; raw 0.0028 vs 0.0035), `MESH_COUNT` inclusive 100 ns ticks op14 2155 vs 54, op21 2630 vs 505 (first-call JIT), op20 729 vs 1189; native adjacency `quad-16` 490 vs 394 us | pass, loading-trace 85 checks, loading-mesh 123 checks (HEAD) |
| 3 | `run_motion_output.py` | route, scene-hook callsite patch on the fixture exe, HDR FP16 MRT on D3DMetal | **65/78 cases pass, suite aborted** at `seam-hdr-exposure` frame 35: the fixture's `EXPOSURE_BLOCKS` text prints the NaN poison block as a finite `1.78e4932` (limitation 2), the runner parses it as `inf` and the `isnan` assert fires. All 65 recorded cases (route, seam, ownership, HDR, ramp, TAA hook) pass their checks; the eight recorded benches, median of 24 `samples_ms`: 1280x768 TAA off 0.325 / on 0.747 ms, 5120x1440 0.688 / 2.468 ms, HDR 1280x768 0.366 / 0.740 ms, HDR 5120x1440 0.852 / 1.948 ms. Per-case `dll_sha256` is `141f372c` for the 46 seam cases and `29b7fd08` for the 19 production cases (the seam DLL is a separate build by design; no DLL changed mid-run, no provenance assertion tripped). The three `hdrexposure` cases cannot pass on X3 until the fixture stops printing non-finite floats with `%g`; the 10 tonemap cases and 4 tonemap benches after the abort: see the partial run below | pass, 78 cases; same benches 0.427 / 0.833, 0.565 / 2.319, 0.408 / 0.807, 0.951 / 1.961 ms |
| 3b | `run_motion_output.py <14 names>` (partial, `motion-output-partial.json`) | the 10 tonemap cases and 4 tonemap benches the abort skipped | **all 10 cases pass with the Steam check counts** (`seam-hdr-tonemap-fault` 59, `shader-absent` 23, four `seam-taa-hdr-tonemap-*` 140 each, `seam-ownership-taa-hdr-tonemap-on` 140, `production-taa-hdr-tonemap-on` 59, `seam-taa-hook-hdr-tonemap-on` 127 with `hook_status=active`, `seam-taa-hdr-tonemap-fault` 132); tonemap benches, median of 24: 1280x768 TAA off 0.719 / on 1.224 ms, 5120x1440 1.386 / 2.166 ms. A partial run is never a suite pass (no cross-case comparisons); its first attempt also died in the `gz_buffer.cpp` build break. So on X3 75 of the 78 cases pass and the 3 `hdrexposure` cases are blocked by limitation 2 | same benches 1.073 / 1.244, 1.464 / 2.273 ms |
| 4 | `run_temporal_pass.py` | SM3 resolve on FP16 targets, device Reset | **pass**, 386 samples, 204 state restorations, 2 generations; the three negative controls exit 1 with identical oracle errors / drift (0.4229/0.4900, 0.4541/0.9117, 0.2866/0.5570). Six `SAMPLE ... 1px horizontal` lines differ only in the 9th significant digit of the printed centroid (`expected=6.870000000` for `6.869999981`; limitation 2), all PASS | pass, 386 samples, 448/204/2 (working tree) |
| 5 | `run_ownership_integration.py` | 26 isolated proxy cases with `d3d9=n,b` | **PASS**, 26/26 cases exit 0, one DLL (`62318808`) for all cases, source tree and binaries unchanged during the run; `verify_ownership_integration.py` (host-side reader, `X3M_FIXTURE_BOTTLE=X3`) PASS, 26 cases. First attempt (18:08) died in `cmake --build build-ownership` on the gz-buffer agent's untracked `src/proxy/gz_buffer.cpp` (`SEEK_SET` undeclared, fixed by that agent at 18:08:33); the rerun is the record. Wall about 70 s including the clean build | PASS, 26 cases |
| - | `check_no_x87.py` | host-only | skipped by design | - |

### FEX-specific limitations found

1. **`GetThreadContext` of a suspended thread returns its creation-time
   context.** In the profiler fixture every sample of all four threads (two
   spinning in the exe, one waiting, main) reports leaf `ntdll.dll+0x4dd1c`,
   which is the `RtlUserThreadStart` export of
   `lib/wine/i386-windows/ntdll.dll`, with `frame rva=0x0` and
   `stack_unknown=0`: `Esp` is inside the TEB stack bounds (at the initial
   stack top, so neither the EBP chain nor the return-address scan finds a
   frame). The game run in
   `../reverse-engineering/loading-profile-bottle-x3.md` saw the sibling form
   (`Eip=0x10000`, usable `Ebp`). In both forms the context is not the live
   guest state, so leaf/frame attribution is void on X3 while tick timing,
   thread discovery, module tables and overhead remain valid. No safe fallback
   exists inside `sampling_profiler.cpp` (the EBP chain needs the live `Ebp`);
   a guest-PC source other than `CONTEXT` is the orchestrator's call. The
   Steam bottle remains the profiler's validation environment.
2. **The CRT prints non-finite floats as finite numbers and rounds the 9th
   digit** (`FEX_X87REDUCEDPRECISION=1`; Wine's msvcrt formats through the
   80-bit long double path). A guest probe (`nan_probe.cpp`, scratch, run under
   both bottles the same minute) printed `quiet_NaN()` as
   `1.78459724e+4932` and `infinity()` as `1.1897315e+4932` on X3 (`nan`,
   `inf` on Steam) while `std::isnan`, `std::isinf`, `fpclassify`, `x != x`,
   `fucompp` (C0=C2=C3=1) and `fucomip` (ZF=PF=CF=1) classify the NaN
   correctly on both bottles: the arithmetic and compares are right, only the
   text channel is wrong. Consequences: the `hdrexposure` hazard blocks
   (+-inf) happen to parse as +-inf again and pass, the NaN poison parses as
   `inf` and fails; `%.9f` output loses the 9th significant digit
   (`temporal-pass.txt`). Fix belongs in the fixtures (print the IEEE bits of
   hazard/poison values), not in the runners' parsers. **Fixed (pre-review
   28):** `EXPOSURE_BLOCKS` prints every non-finite value as its IEEE bits
   (`0x%08x`; finite values keep `%.9g`) and `run_motion_output.py`'s
   `parse_float` decodes that form, so the three `hdrexposure` cases no longer
   depend on the CRT's text channel; production is untouched. Verified: the
   full `run_motion_output.py` with `X3M_FIXTURE_BOTTLE=X3` passes, 97 cases +
   26 benches, the three `hdrexposure` cases 85 checks each
   (`bottle-X3/motion-output-summary.json`).
3. **Per-call cost of tiny cross-DLL hooks is 3-4x higher** (loading trace
   1.364 vs 0.378 us per hooked call; consistent with the 3.2x `gzread` finding
   in the game). GPU-bound boundaries (motion-output benches) are within +-25 %
   of Steam either way.

### Other observations

- The concurrent agents' rebuilds of `build/d3d9.dll` did not trip any
  provenance assertion (`run_motion_output.py` and
  `run_ownership_integration.py` build their own DLL at the start and copy it
  per case; `run_loading_trace.py`/`run_temporal_pass.py` hash their own
  fixtures). What did interfere was an uncompilable in-flight source
  (`src/proxy/gz_buffer.cpp`, 18:04-18:08): both runners that rebuild the DLL
  failed at `cmake --build` and were rerun once it compiled. `run_sampling_profiler.py`'s idle wait now also skips
  `wine_lock.py` wrappers that are only waiting for the lock (they execute
  nothing under Wine; without this, other agents' queued runners deadlocked the
  30 minute wait).
- Capture logs over 1 MB under `bottle-X3/` (`motion-output-*-capture.log`,
  6.5 MB each) are ignored through `.gitignore`; the JSON/text records stay.
- The Steam bottle stays the default for the fixtures: on X3 the profiler and
  any fixture that prints non-finite floats cannot be validated.

## 2026-09-14: native Windows Media codecs in bottle X3 (user-authorized)

Goal: replace Wine's builtin `wmvcore`/`wmasf`/`wmadmod` WMA decoding in bottle
X3 with Microsoft's native codecs, to see whether the voice timing and the
DirectShow route behave like Windows.

**What was installed.** `winetricks --unattended wmp11` (exit 0). `wmp10` is
not usable here: the bottle is `WineArch=arm64` / `#arch=win64`, winetricks
refuses it (`This package (wmp10) does not work on a 64-bit installation`), and
under `--force` `MP10Setup.exe`'s `setup_wm.exe /Quiet` runs but installs
nothing (it sees WinXP x64). `wmp11` has a win64 branch and installed the
XP x64 package, whose WOW64 half is the 32-bit codec set the game would use.

- Installer: `wmp11-windowsxp-x64-enu.exe`,
  sha256 `5af407cf336849aff435044ec28f066dd523bbdc22d1ce7aaddb5263084f5526`
  (matches winetricks' expected hash), cached under `~/.cache/winetricks/wmp11`.
- winetricks 20260125, sha256
  `431f82fc74000e6c864409f1d8fb495d696c03928808e3e8acffc45179312a7b`.
- Dependency verbs pulled in: `wsh57` (already installed) and `gdiplus`, which
  replaced `gdiplus.dll` in both `system32` and `syswow64` with the Win7 SP1
  native build and set `gdiplus=native`. That is wider than WMA and is the one
  unrelated setting this change touched.

**Invocation.** CrossOver's `bin/wine` is a Perl wrapper that selects a bottle
with `--bottle` and ignores `WINEPREFIX`; with plain `WINE=.../bin/wine`
winetricks fails with `Unable to find the 'default' bottle`. It also resolves a
bare `setup.exe` argument against its own working directory, giving
`winewrapper.exe:error: cannot execute L"setup_wm.exe /Quiet"`. Both are solved
by a two-line shim that runs
`wine --bottle X3 --no-update --workdir "$PWD" "$@"` and is passed as `WINE`
and `WINE64`. Everything ran under
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py --timeout 60`.

**Overrides added** (`HKCU\Software\Wine\DllOverrides`, diffed against the
pre-change `user.reg`): `wmvcore`, `wmasf`, `mfplat`, `wmp`, `wmpnssci`,
`wmplayer.exe`, `l3codeca.acm`, `gdiplus` = `native`; `jscript`, `vbscript`,
`scrrun`, `cscript.exe`, `wscript.exe` = `native,builtin` (from `wsh57`).
No override touches `d3d9`, `d3dx9_37`, `dsound` or any project DLL.

**Files.** `syswow64` now holds the Oct-2006 native `wmvcore.dll` (2450944),
`WMASF.dll` (222208), `wmadmod.dll` (757248), `MFPLAT.dll`, `WMSPDMOD.dll`,
`l3codecp.acm`. winetricks deletes the builtin placeholders in *both*
`system32` and `syswow64` but the x64 package installed nothing 64-bit, so the
64-bit `wmvcore/wmasf/mfplat/wmp` were restored by hand from the pre-change
copies. Game files untouched: `X3AP.exe` sha256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` before and
after, and all 28 `drive_c/X3/*.cat|*.dat` names and sizes identical.

**Rollback record**: `/tmp/x3-bottleX3-pre-wmp/` holds `system.reg`,
`user.reg`, `userdef.reg`, `cxbottle.conf`, `system32.ls`, `syswow64.ls`,
`gamedata.ls`, `x3ap.sha256`, the pre-change WMP DLLs under `dlls/`, and the
four winetricks transcripts.

**Probe outcome: the sync-reader path gains, the DirectShow path breaks.**

- `run_sync_probe.py` (IWMSyncReader, so `wmvcore` directly): both archives
  `open_hr=00000000`, metadata read, one output of PCM 44100 Hz / 1 ch /
  16 bit. stderr is 76 bytes (the two msync lines only) with
  `WINEDEBUG=-all,+quartz,+wmvcore,+wmadec,+winegstreamer,+winediag`: no
  `winegstreamer` or builtin-`wmvcore` trace at all, i.e. the native DLL now
  serves the reader. Open cost 47.8 ms (144) and 1.5 ms (244) wall.
  `/tmp/x3-voice-sync-wmp-r1`.
- `run_voice_native_update.py --mode actual`: every graph now fails at
  `open_file` with `hr=80040217` (`VFW_E_CANNOT_CONNECT`), on both archives and
  all three repeats, so reads=0, bytes=0, eos=0, duration 0.000 s against the
  declared lengths, no reopen ever succeeds and the runner aborts on
  `VOICE_CAPTURE bytes=0`. No anchor error or span can be computed, so there is
  nothing to place next to v1 (-701 ms) and v3 (0.02 ms).
  `/tmp/x3-voice-native-wmp-r1`,
  `verification/results/bottle-X3/voice-native-actual-wmp.json`.
- `run_voice_startup_replica.py`: `outcome=completed`, `hung_step=null`, 5.05 s
  wall, so the load hang still does not reproduce - but all three streams show
  `fatal=open_file hr=80040217`, `created=0`. `/tmp/x3-voice-startup-wmp-r1`.

The native codecs therefore serve `IWMSyncReader` but leave quartz unable to
build a graph for the `.dat` voice archives; in that state the game's own voice
route would not play.

**Reverted the same day.** The overrides are the only thing that made the
installed files live, so the revert is registry plus one file pair:

- `wine --bottle X3 reg delete 'HKCU\Software\Wine\DllOverrides' /v "*<name>" /f`
  for all thirteen values (the Wine override values carry a leading `*`; without
  it `reg delete` answers `Unable to find the specified registry value`).
- `gdiplus.dll` was overwritten in place by the `gdiplus` verb, so both copies
  were restored from the CrossOver install: `system32` from
  `lib/wine/aarch64-windows/gdiplus.dll` (1646144 bytes, sha256 `7b50754f...ae0d`)
  and `syswow64` from `lib/wine/i386-windows/gdiplus.dll` (589376,
  `b2a62069...4982`), both matching the pre-change listing sizes and byte-identical
  to the untouched Titan Quest bottle.
- The `DllOverrides` values now diff **identical** to the pre-change `user.reg`
  (5 values before, 5 after). `X3AP.exe` sha256 and all 28 `*.cat`/`*.dat`
  entries unchanged again.
- The installed WMP11 files stay on disk under `syswow64` and
  `Program Files (x86)\Windows Media Player`; without an override Wine prefers
  its builtins, so they are inert. Removing them is a separate cleanup.

**Full revert to the pre-change state (same day).** The override removal above
left the installed files and the WMP11 COM/DMO registrations behind, so the
bottle was taken the rest of the way back under one lock hold (`wmp-revert2`),
with no Wine process holding the bottle (`lsof +D drive_c` empty; the two
`winedevice.exe` that `lsof` appears to place in X3 started 2026-09-10, before
the bottle existed, and their cwd inode is not X3's `system32`):

- Current registry saved to `/tmp/x3-bottleX3-post-wmp/`, then the key diff
  classified: system.reg +4303 keys / -56, user.reg +130 / -1, userdef.reg 0/0.
  Every addition is `Software\Classes\{Wow6432Node,Interface,CLSID,Typelib}`,
  `Microsoft\{MediaPlayer,Windows Media,Multimedia,Updates,SystemCertificates}`
  and media file associations; every removal is a scripting or WMP CLSID under
  `Software\Classes\Wow6432Node\CLSID` plus five WMP/eventlog/certificate
  parents - all attributable to the `wsh57` and WMP `regsvr32` runs. All three
  hives were then restored from `/tmp/x3-bottleX3-pre-wmp/` and now compare
  byte-identical.
- 51 files and directories absent from the pre-change listing were removed from
  `syswow64` (the WMP11/WMDM/DRM set, `l3codecp.acm`, `scripten.inf`,
  `spuninst.exe`, `update/`), plus `Program Files (x86)\Windows Media Player`
  and the `windows\temp\_wmp10`, `_wmp11`, `_gdiplus`, `_wsh57` staging
  directories. 15 files that winetricks overwrote in place (`wmvcore`, `wmasf`,
  `mfplat`, `wmadmod`, `wmp`, `wmvdecod`, `qasf`, `jscript`, `vbscript`,
  `scrrun`, `scrobj`, `dispex`, `wshom.ocx`, `cscript.exe`, `wscript.exe`) were
  restored from `lib/wine/i386-windows` in the CrossOver install, and
  `gdiplus.dll` from `aarch64-windows` / `i386-windows`. `drive_c/X3` was never
  touched.
- Verified: `system32` 837 entries and `syswow64` 875 entries match the
  pre-change listings with no missing, extra or size-mismatched name; all three
  `.reg` files byte-identical; `X3AP.exe` sha256 unchanged and the 28
  `*.cat`/`*.dat` entries identical.

Post-revert sync probe, twice under the lock:

- plain: both archives `open_hr=80004005`, `metadata=0`, `outputs=0`,
  `fatal=sync_open`, stderr 16422 bytes with `winegstreamer` trace - the
  pre-install R3 behaviour (the ASF is recognised, the Open fails for lack of a
  WMA decoder). `/tmp/x3-voice-sync-revert-r1`.
- with `GST_PLUGIN_PATH_1_0=/tmp/x3-wma-plugin-v3/runtime/plugins` and
  `GST_REGISTRY_1_0=/tmp/x3-wma-plugin-v3/registry/x3-arm64.bin`: both archives
  `open_hr=00000000`, metadata read, one output PCM 44100/1/16 - the libav
  decoder path is intact. `/tmp/x3-voice-sync-revert-r2`.

Repeated after the full revert with the same results: `-r3` plain, both
`open_hr=80004005`, `fatal=sync_open`, stderr 16422 bytes; `-r4` with the v3
plugin, both `open_hr=00000000`, PCM 44100/1/16. The probe runs leave no
registry key added or removed. The bottle is back to its pre-2026-09-14 state.

Addendum (same day, after the fade/cutout candidate install): `cxbottle.conf` still
differed from the pre-experiment copy by one line, `"WindowsVersion" = "win10"`
uncommented by the winetricks attempt (registry and template already said win10).
Restored from `/tmp/x3-bottleX3-pre-wmp/cxbottle.conf`; SHA-256 `cc5d6c00…` again.
