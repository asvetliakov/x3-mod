# Cull census: verification ledger

Feature: `--cull-census` / `X3M_CULL_CENSUS=1`, `src/proxy/cull_census.cpp`,
sites `0x0047d258` (measure) and `0x0047d528` (exit) of the cull/LOD pass
`0x0047cfe0` ([lod-selection.md](../reverse-engineering/lod-selection.md),
"Cull census sites"; [engine-frame-time.md](../architecture/engine-frame-time.md)
2.3). Default off; nothing is written unless the variable is exactly `1`, the
executable hash matches and both window bytes verify. Read-only: the stubs
record and never change a verdict.

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-18 | Site qualification on the installed EXE (`fdbf3418…`): 31-byte and 27-byte windows, one whole 6-byte `mov` at the measure site followed by the flag writer `test eax,eax`, two whole instructions at the exit site followed by the flag consumer `je 0x0047d548`, no direct branch into either displaced span, incoming sources exactly `0x0047d231`/`0x0047d24e` and the eight documented exit sources, `ret 8` at `0x0047d54f`, source constants and windows, stub encoders | `python3 verification/probe/verify_cull_census_sites.py` | PASS, 16/16 checks, 373 instructions decoded in `0x0047cfe0..0x0047d551` |
| 2026-09-18 | Host tests: verifier on a synthetic image (changed window/site/next-instruction bytes, an interior branch, an extra exit source and a changed `ret` refused), constants, encoders, install/frame/row parsers, `classify`/`size_limit` and both encoders compiled with the host compiler, `tools/analysis/cull_census.py` bucket table and draw join on a synthetic log (view selection, pixel buckets at `m00 = 0.8`, savings, JSON), launcher gate | `python3 -m unittest discover -s verification/analysis -p 'test_cull_census.py'` | 12 tests OK |
| 2026-09-18 | X3 CPU fixture: synthetic pass with both windows byte-exact, the production module patching it; native-vs-patched identity of every node (renderable bit, LOD, flags) and of EAX/ECX/EDX/EFLAGS at the return over a 12-node tree in the main view, the env-map view (`< 20` zeroing, +1 LOD) and view-distance 4 (LOD forced to 0); callee-saved registers, ESP and empty x87 preserved; rows in traversal order with the engine's `s`/measure/`D`/radius/thresholds/limit/LOD and verdicts `kept`/`culled_size`/`culled_min`/`culled_other` (parent threshold as the limit, own threshold above the parent's, `D < 640` saturation); two early exits `unmeasured`; disarmed frame records nothing; 8,201 nodes keep 8,192 rows with `overflow=9` and the pass unaffected; LastError preserved; exact rollback and native pass back; option off/`0`/engine sites absent untouched; changed measure/exit window bytes and a null site refused; closed window `late_claim` | `python3 verification/probe/build_cull_census.py` (module audit: 12 functions, no x87/MMX/XMM, no EH symbols, both handlers present) then `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_census.py` | 71 checks, 0 failures; `verification/results/cull-census-cpu.json`; bench per 12-node pass (10 measured): native 0.234 µs, patched disarmed 0.244 µs, patched armed 0.311 µs (Wine/FEX, harness included, not game FPS) |
| 2026-09-18 | Clean DLL build and the no-x87 walk including both handlers | `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j4` (fresh directory); `python3 verification/probe/check_no_x87.py build/d3d9.dll` | 0 warnings; PASS, 539 reachable functions, `_x3m_cull_census_measure` and `_x3m_cull_census_exit` walked, no violations; `build/d3d9.dll` sha256 `6b86763e15d0e9a62e31c60fcfa7f41fd79f462550e1c46c9c40b7e2cbd2a16b` (worktree build, not a candidate) |
| 2026-09-18 | Launcher dry run | `python3 tools/manage.py launch --cull-census --dry-run` | env carries `X3M_CULL_CENSUS=1`; no launch |
| 2026-09-18 | Unaffected fixtures rerun (engine_patch and capture-path neighbours) | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_object_lifetime.py`; `… run_ownership.py` | object lifetime 674 checks, 0 failures, 13 cases PASS; ownership baseline and wrapped fixtures exit 0 (`verification/results/bottle-X3/`) |
| 2026-09-18 | Verdict `culled_small` for the small-parts stub (`classify(e, small_threshold)`, named after the engine's own `culled_size`/`culled_min`; `note_small_threshold` stores the frame's threshold, 0 without the option): host tests and the CPU fixture rerun with the changed module | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_cull_census`; `python3 verification/probe/build_cull_census.py` then `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_census.py` | 12 tests OK; 71 checks, 0 failures (13 functions in the module audit, no x87/MMX/XMM); both stubs armed beside the small-parts stub: [cull-small-parts.md](cull-small-parts.md) |

## Open

- Not yet run in the game: the first capture with `--cull-census` at the run124
  station view gives the bucket table (`tools/analysis/cull_census.py
  <session.log> --frames <F8 window>`); expect one `cull_census_frame` row per
  captured frame per Present and up to 8,192 rows each, so keep the F8 window
  to a few frames.
- The rows are emitted with one `log()` call each at Present: a full ring is
  8,192 lines (~1.5 MB) per captured frame, which lengthens that frame only.
- The ring is read at Present on the assumption that the cull pass and Present
  run on the same thread (the frame routine `0x00471f50` issues both); a pass
  on another thread would at worst tear one frame's rows, never the engine's
  verdicts.
