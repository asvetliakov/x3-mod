# Handoff: exact-equality adjacency (X3M_MESH_ADJACENCY), 2026-09-12

Paused on the orchestrator's request before the final suite runs. This task committed
nothing, but another session's commits (`4e1aa20`, `a8d4309`) swept up the edited
tracked files (`src/proxy/loading_trace.{h,cpp}`, `src/proxy/mesh_adjacency_cache.{h,cpp}`,
`CMakeLists.txt`, `tools/manage.py`, build scripts, runners, docs) while the new files
remain untracked: `src/proxy/mesh_adjacency_fast.{h,cpp}`,
`verification/probe/mesh_adjacency_fast_fixture.cpp`, `verification/probe/mesh_adjacency_fast_host.cpp`,
`tools/analysis/mesh_adjacency_reference.py`, `verification/analysis/test_mesh_adjacency_fast.py`,
`docs/verification/mesh-adjacency-fast.md`, this note. HEAD therefore does not build
without them: `git add` those files in the next checkpoint.

## Done (tree compiles: production DLL, all three loading/mesh fixtures)

* `src/proxy/mesh_adjacency_fast.{h,cpp}`: pure SSE2 module (no x87 opcode,
  imports only malloc/free/memset), D3DX-equivalent rules established against the
  real d3dx9_37 (head insertion, normal selection, raw-degenerate skip,
  larger-index welded-corner drop, single adjacency after selection), equivalence
  gate (power-of-two grid test, else 27-cell neighbour scan at 2*epsilon).
* `src/proxy/loading_trace.{h,cpp}`: `X3M_MESH_ADJACENCY=native|verify|fast`,
  verify/fast services as the cache's "original" (cache lookup -> compute -> store),
  bounded mismatch lines, `mesh_adjacency_metric`/`mesh_adjacency_gate`/
  `mesh_adjacency_fp_first` telemetry, fault policy as the cache, MXCSR pinned to
  0x1f80 for our arithmetic with the caller's state restored, fixture seams.
* `src/proxy/mesh_adjacency_cache.{h,cpp}`: `supported_fp` now requires only
  masked exceptions and an empty x87 stack; precision/rounding/FTZ/DAZ are keyed
  (the game's 0x027f/0x9fc0 no longer bypasses); first incoming state published
  (`mesh_cache_fp_incoming`).
* `tools/manage.py --mesh-adjacency {native,verify,fast}` (requires --telemetry).
* Fixtures: `verification/probe/mesh_adjacency_fast_fixture.cpp` (37 cases, tie
  evidence, timing, hook path with cache off/on, game FP state, native state
  sweep), built by `build_loading_trace.sh`, run as two cases of
  `run_loading_trace.py` (inventory 2179 / 2219 checks recorded);
  `mesh_cache_hook_fixture.cpp` and `mesh_adjacency_cache_fixture.cpp` updated for
  the FP contract (unsupported state is now an unmasked x87 denormal exception;
  a game-state keyed case added). Host: `verification/analysis/
  test_mesh_adjacency_fast.py` + `tools/analysis/mesh_adjacency_reference.py`
  (12 tests pass; `unittest discover` passed 621 before the last fixture edits).
* Docs: `docs/verification/mesh-adjacency-fast.md` (algorithm, evidence, numbers),
  pointers in `docs/reverse-engineering/loading-performance.md`,
  `docs/verification/motion-output.md` (launch: verify first, then fast),
  `docs/verification/mesh-adjacency-cache.md`, `docs/verification/mesh-cache-hook.md`.

## Passed (direct runs, not yet through the runners)

* `mesh_adjacency_fast_fixture.exe 0` -> `MESH ADJACENCY RESULT cache=0 checks=2179 failures=0`
* `mesh_adjacency_fast_fixture.exe 1` -> `MESH ADJACENCY RESULT cache=1 checks=2219 failures=0`
* Earlier full `run_loading_trace.py`: loading-trace 85 and loading-mesh 123 passed
  with the module linked (before the FP-contract change).

## Not yet run (one Wine runner at a time; check the game guard first)

```sh
python3 -c "import sys;sys.path.insert(0,'verification/probe');import game_guard;print(game_guard.game_running())"   # must print []
pgrep -fl 'run_.*\.py'                                                        # no other runner
python3 verification/probe/run_loading_trace.py          # 4 cases; expects 85 / 123 / 2179 / 2219
python3 verification/probe/run_mesh_adjacency_cache.py   # FP variants 0-7 keyed, 8-9 bypass
python3 verification/probe/run_mesh_cache_hook.py        # its exact check counts (1714/1927/1935/2189/2566/2574) will change by the added GAME_FP_STATE block: update expected_checks from the run and record the new numbers in docs/verification/mesh-cache-hook.md
python3 verification/probe/check_no_x87.py build/d3d9.dll
python3 -m unittest discover -s verification/analysis
```

Then re-record the fixture numbers in `docs/verification/mesh-adjacency-fast.md`
from `verification/results/mesh-adjacency-cache-{off,on}-fixture.txt` and
`loading-trace-mesh-summary.json`, review, and commit (docs/status.md untouched by this task).

## Host checks at pause time

* `build/d3d9.dll` rebuilt (not installed): SHA-256
  `4362249e3313ee3eaf930d98939ae62bb24791acecb373b6aef1700f7e1d1427`;
  `check_no_x87.py build/d3d9.dll`: no violations.
* `unittest discover -s verification/analysis`: 701 tests, 1 error unrelated to
  this task (`test_admission_integration_expectations` imports
  `verification/probe/verify_ownership_integration.py`, which now imports a
  `bottle` module that another agent is adding); the 12 adjacency tests pass.

## Open points

* Normal selection near ties (SSE float versus D3DX x87) is the one unproven
  corner; the game run with `--mesh-adjacency verify` is the acceptance test.
* The cache keys the service pointer, so verify-mode entries never serve fast
  mode (documented; modes do not switch at runtime in production).

## Resumed 2026-09-12 (evening): suites run, performance pass done

All three Wine suites pass through their runners (Steam bottle, under
`wine_lock.py`): `run_loading_trace.py` 85 / 123 / 2179 / 2219;
`run_mesh_adjacency_cache.py` 767; `run_mesh_cache_hook.py` 1,714 / 2,003 / 2,011 /
2,189 / 2,673 / 2,681 (expectations updated in the runner, deltas derived in
`mesh-cache-hook.md`). Fixes on the way: the loading-trace runner's default-policy
assertion (computable cases only), the fixture build scripts link `gz_buffer.cpp`,
the cache fixture's FP variant 9 follows the applied state (Rosetta keeps SSE
exception masks set), the hook fixture's game-state case drives the state through
`seed` and takes its reuse hit on a second mesh (wrapped-ownership tracker
ambiguity after an admitting miss, recorded). Performance pass on the module:
single thread-local arena, next-only edges, anchor slots, retired flags, lazy
normal cache; numbers in `mesh-adjacency-fast.md`. DLL of the working tree
`2d25ec114173d5947a68c52fb539b96eb6e05c259f5363f7b2d07df9b35d7d66`, not installed;
nothing committed. Open: the near-tie normal selection (game run with `verify`).

