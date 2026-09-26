# Live motion route (checkpoint B1) review

Independent review of the uncommitted live same-draw motion route:
`src/proxy/motion_output.{h,cpp}`, `src/renderer/motion_row_history.{h,cpp}`,
the `material_motion` split, the capture hooks, the ABI assertions, the
synthetic fixture and its runners. Scope was interposition correctness,
shadow-state correctness, the x86/ABI rules, per-call cost, fixture strength
and documentation accuracy. Result files under `verification/results/` were
queried, not read.

## Interposition and reference counting

Every device call the route makes goes through the native table captured at
hook time (`Hooks::original`): the 39 slots listed in the architecture note
plus native AddRef/Release for the release hook's probe. All indices, the
`IDirect3DStateBlock9` slots (Release 2, Apply 5, six-entry table) and the
119-entry device table are asserted by `verification/probe/abi_check.cpp`,
which compiles cleanly. Because the fill, the self test and the routed draw use
`DrawPrimitiveUP`/`SetRenderTarget`/`SetVertexShader` natively, neither the
route's own shadow nor `SceneCapture` observes them, so the selector state
machines see application calls only.

Restoration after the fill (`draw_quad`) is complete and in a valid order:
RT0 first (which resets viewport/scissor), RT1–3, depth, viewport, scissor,
FVF or declaration, VS, PS, stream 0 (cleared by `DrawPrimitiveUP`), then the
twelve render states, with saved COM references released after restoration.
A failed `save_state` changes nothing; a failure inside the fill still runs the
full restore. A routed draw undoes COLORWRITEENABLE1, RT1, PS, VS and the
reserved constants in reverse; a failure mid-application undoes what was set
and lets the original draw proceed with the application's own HRESULT.

The final-Release path probes the native count and drops the owned variants,
sentinel shader and RT1 only when exactly the caller's and our references
remain; child destruction re-enters the hook through the public vtable, which
`device_references()` reports as zero while `release_resources` runs. If the
count model were wrong on some runtime the application would observe a leaked
device, never a use-after-free, because `~MotionOutput` releases nothing once
the objects are dropped. Native Windows reference semantics for texture levels
are consistent with this model but remain unverified there.

## Findings and fixes

Severity ordering: defects fixed first, then design-level items left as is.

1. **Medium, fixed** – `MotionOutput::readback` (`src/proxy/motion_output.cpp`)
   built the file path with `std::wstring` inside a `noexcept` hook path; an
   allocation failure would have terminated the process. Now uses fixed
   buffers (`wcslen`/`wmemcpy`/`wcscpy`) and fails the readback instead.
2. **Medium, fixed** – the shadow hooks were installed whenever
   `X3M_MOTION_OUTPUT=1`, even when `attach` refused the device
   (`src/proxy/capture.cpp`, `hook_device`). Every setter then paid a full
   CPU-state boundary, admission entry and lock for a route that could never
   run. The route-only slots are now installed after `attach` and only when
   `enabled()`; the private table is already live, so `Hooks::set` takes
   effect immediately.
3. **Medium, fixed (performance)** – all eleven setter hooks used
   `CpuCallBoundary` (four FNSAVE/FRSTOR per call) and `HookGuard`'s
   telemetry. The shader, constant and viewport hooks do only integer/SSE
   memory work on both sides of the native call, so they now use the new
   `LightCallBoundary` (`src/proxy/cpu_state.h`: MXCSR and last error only)
   with a plain lock. The precondition is proven, not assumed:
   `verification/probe/check_no_x87.py` walks the static call graph of the six
   light hooks in the built DLL (125 reachable functions including
   winpthreads lock/unlock, the admission ABI, the shadow updates and the
   libstdc++ throw helpers) and finds no x87 opcode. The first walk caught that
   `set_stream_source`/`set_indices` reach `resource_id` → `GetPrivateData`
   and, on first sight, `log` → `__mingw_pformat` (x87 code); those two, the
   declaration/FVF hooks (foreign getters) and the state block hooks keep the
   full boundary. Lock-wait telemetry no longer counts the six light setters.
4. **Low, fixed** – `MotionRowHistory` reserved two 4,096-entry tables in every
   `Device`, route requested or not. The member now starts at capacity 0 and is
   sized in `attach` when the switch is on.
5. **Low, fixed** – `undo` restored the reserved constant ranges even when the
   failed application step had not reached them; `MotionRoute` now records
   `vs_constants_set`/`ps_constants_set` and restores only what this draw set.
   `rows_hash` (FNV over 64 bytes per candidate draw) is now computed only in
   capture frames, where it is logged.
6. **Documentation, fixed** – the architecture note's hook-install condition,
   native slot list (probe slots 1/2), boundary description and restoration
   wording; the verification note lists the new checker.

Verified correct and left unchanged: constant-range shadowing for partial
overlaps of c24–27, c252–255 and c216–217 (clamped `lo/hi`, count 0 ignored,
`start`/`count` bounded before the addition), i0 tracked only from
`start == 0`, `EndStateBlock`/`Apply` resynchronizing the whole shadow from
the public getters with reserved ranges marked written, Reset clearing the
target/history/selector before the native call and resynchronizing after,
`SetRenderTarget` applying even while a block records, pointer reuse in the
shader registry replacing the entry and clearing the bound-shadow variant,
gate order cheapest first (two render-state getters per tracked draw, five
more plus the observers only for a reviewed pair in the scene phase), no
per-draw heap allocation (`lookup_and_record` pushes into reserved storage;
`std::sort` runs once per frame), and no logging outside capture frames
except the 60-frame telemetry summary.

Design-level observations, not changed: registry entries keyed by object
address are never removed, so a variant whose original the game destroyed
keeps one device reference until address reuse or device release (bounded by
distinct shader addresses; documented). `resync_shadow` costs about fifteen
getters per state block Apply; if the game applies blocks per draw this will
show up in profiling. `hook_stateblock` allocates map nodes inside a hook like
the existing device map, so an allocation failure there would still unwind
through the hook. The ownership wrapper passes unwrapped pointers to the hooked
device, so registry keys and shadow pointers are native identities.

## Fixture assessment

`run_motion_output.py` proves color bit-identity by comparing the twelve
per-frame color hashes of separate off/on processes for both the production
and the seam DLL; restoration by 39 before/after snapshots per run covering
RT0/RT1, depth, viewport, scissor, FVF, declaration, VS, PS, stream 0, fifteen
render states, c24–27, i0 and (once the application has written them) the
reserved ranges; Reset with RT1 owned (second target creation, generation 2,
history restarted in frame 9 checked by the oracle and by the zero-matched
assertion); sentinel-only mode (frame 2, gate 5 twice, all-sentinel readback);
and the application-write restoration of c252/c216 from frame 1 on. The DLL's
per-frame gate histogram and per-draw `motion_route` lines are cross-checked
against the script for frames 1–8. Not exercised: apply/restore failure
injection and the game observers.

## Results after the fixes

Both trees were rebuilt with the documented commands (`build/` RelWithDebInfo
by `run_motion_output.py` with `--clean-first`, `build-ownership/` Release by
`run_ownership_integration.py`); `abi_check.cpp` compiles with the new slots.

| Suite | Result |
| --- | --- |
| `check_no_x87.py` on `build/d3d9.dll` | PASS: 6 light hooks, 125 reachable functions, 0 x87 opcodes |
| `run_motion_row_history.py` | PASS: 8 check groups, release and ASan/UBSan |
| `run_motion_output.py` | PASS: production off/on 6/6 checks, seam off/on 6/30, 39/39 restorations with zero differences in every run, seam-on 44,284 motion pixels / 10,261 matched, max 0.0016 px UV and 3.7e-8 depth, 19 routed-draw decisions matched, color hashes identical off vs on for both DLLs |
| `run_material_motion_structure.py` | PASS: 8 groups, 57,152 mutations, 6 aliases |
| `run_ownership_integration.py` | PASS: 26 cases |
| `run_ownership_integration_fallback.py` | PASS: 23 production objects |
| `python3 -m unittest discover -s verification/analysis` | 388 tests OK |

The motion-output numbers are unchanged from the pre-review run recorded in
[motion-output.md](../verification/motion-output.md), as expected: the fixes alter cost, the
refused-device path and failure handling, not the routed output.
