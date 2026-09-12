# Handoff: engine reads (validated direct reads, matrix gate, X3M_TELEMETRY_DRAW)

Paused 2026-09-12 17:45 on the orchestrator's request (account switch). Nothing
is committed; the tree compiles; `build/d3d9.dll` =
`4362249e3313ee3eaf930d98939ae62bb24791acecb373b6aef1700f7e1d1427` (not installed).

## Done

* `src/proxy/engine_memory.{h,cpp}` (new): validated direct reads with a
  32-entry VirtualQuery region cache, re-validated on the first touch of each
  frame (`next_frame()` from `MotionOutput::begin_frame`) or after 100 ms
  without a frame advance; copy via `rep movsb` (no XMM/x87);
  `X3M_ENGINE_READS=rpm` forces `ReadProcessMemory`. Policy and residual risk
  are written up in `docs/verification/route-cost-run1.md` "Implemented".
* `object_trace.cpp` and `object_lifetime.cpp` read through it;
  `object_trace::current(out, matrices)` skips the four engine matrices when
  `matrices=false` (the route passes `capture_`).
* `telemetry.{h,cpp}`: `X3M_TELEMETRY_DRAW=1`, `draw_enabled()`,
  `enabled(Metric)`; per-draw metrics (`draw_backend`, `route_gate`,
  `route_draw`, `route_set_rt`, `route_jitter`, `route_lazy_flush`) are dropped
  without it. `motion_output.cpp`: per-draw sites use `draw_stamp()`; gate
  bracket checks `telemetry::draw_enabled()`; `begin_frame` advances the
  read epoch. CMakeLists and both fixture build scripts list `engine_memory.cpp`.
* Fixtures: both extended (record hash per mode, 20k-call timing, decommit
  cases); runners parse `TIMING`/`IDENTITY` into `read_path` and require
  `equal=1`; `verification/analysis/test_object_lifetime_runner.py` updated
  (7 tests pass with `PYTHONPATH=verification/probe`, needed because another
  agent's runner switch added `import bottle`).
* Docs drafted with `«PLACEHOLDER»` tokens still to fill:
  `docs/verification/route-cost-run1.md` (Implemented section),
  `docs/reverse-engineering/object-identity.md`, `object-lifetimes.md`,
  `docs/architecture/live-motion-route.md` (cost paragraph, switches).

## Suite results so far

| suite | result |
| --- | --- |
| `run_object_trace.py` | PASS, 166 checks / 120017 backend calls; identity equal (5d9c86811e5cd583); route path 3.369 µs/call rpm vs 1.417 direct (baseline 0.045), capture path 8.273 vs 1.837; 0.0040 VirtualQuery per call |
| `run_object_lifetime.py` | FAIL 1/574: `output x87/SSE/MXCSR matches original`; identity equal (853bfaca11e07f83); `current` 7.060 µs rpm vs 0.706 direct (12 reads, 0.0197 queries/call); decommit and retirement cases pass |
| `check_no_x87.py build/d3d9.dll` | PASS (130 reachable, no violation) on the previous link; rerun on the final DLL |
| `python3 -m unittest discover -s verification/analysis` | 673 tests; 1 ERROR is the other agent's `import bottle` (runner switch), my lifetime-runner failure is fixed |
| `run_motion_output.py`, `run_scene_capture.py` | not yet run |

## The open failure

The lifetime fixture's in-mutation probe (`inside()` with `check_inside`) calls
`lt::current()` from inside the original insert and then compares the FX state
after the wrapped call with the unwrapped baseline. `current()` now reaches
`engine_memory.cpp`, where GCC (-O2 -msse2) zeroes `MEMORY_BASIC_INFORMATION`
and copies `Region` with `pxor/movups/movq` (see
`objdump -d build/CMakeFiles/d3d9.dir/src/proxy/engine_memory.cpp.obj | grep xmm`),
so XMM0/XMM1 differ from the seeded baseline. Not a production ABI issue for the
D3D-hook callers (XMM is volatile on x86), but `finish()` inside the game-side
wrappers also reads now: confirm the wrappers save/restore FX around their C++
(`object_lifetime.cpp` dispatch, lines ~189–267; the object already had 93 XMM
references, so they must) before choosing the fix. Options: (a) keep
`engine_memory.cpp` XMM-free (write the struct fields explicitly / no `{}`
zeroing, verify with objdump), (b) fxsave/fxrstor around the probe's
`snapshot()` in `verification/probe/object_lifetime.cpp::inside()` (fixture-only).

## Next commands (one Wine runner at a time; gate script in the scratchpad or
re-create: no game, no executing `python3 …/run_*.py`, no fixture `.exe` under wine)

```
cmake --build build -j8
python3 verification/probe/run_object_lifetime.py      # after the fix above
python3 verification/probe/run_object_trace.py
python3 verification/probe/run_motion_output.py         # hashes must be identical
python3 verification/probe/run_scene_capture.py
python3 verification/probe/check_no_x87.py build/d3d9.dll
PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis
```
Then replace the `«…»` tokens in the four docs with the summary numbers, update
the object-identity checks/calls line, report the DLL SHA-256, commit nothing.
