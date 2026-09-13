# Renderer device creation policy

The renderer needs an ordinary, state-readable D3D9 device. On the captured
`Direct3DCreate9` → `IDirect3D9::CreateDevice` route, startup
`X3M_MOTION_OUTPUT=1` clears only `D3DCREATE_PUREDEVICE` from the application's
creation flags before forwarding. With motion output off, all flags pass
through unchanged. Hardware, mixed and software vertex-processing choices,
FPU preservation, multithreading, window behavior and all other bits retain
the application's values. This does not repair other invalid flag combinations
or change vertex-processing mode.

The concrete trigger is run25: the game requested `0x00000052`
(`FPU_PRESERVE | PUREDEVICE | HARDWARE_VERTEXPROCESSING`), and bloom attach
refused it with `D3DERR_NOTAVAILABLE`, reason `vertex_processing`. With this
policy, that request becomes `0x00000042`. Removing only BloomPass's check
would rely on unsupported pure-device getters. Microsoft's
[D3DCREATE contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dcreate)
explicitly excludes getters for state that can be stored in state blocks when
PUREDEVICE is selected. The optional pure-device optimization is therefore
incompatible with the current renderer's state-preservation contract.

## Scope and forwarding

`capture.cpp::create_device` derives effective flags from the already parsed
startup `motion_output_requested` value, before invoking the saved factory
slot 16. It emits one `device_creation_policy` line per creation attempt with
`requested`, `effective` and `state_reads`. The older `create_device flags=`
field continues to mean requested flags. Public `GetCreationParameters`
reports the real device's effective flags; the proxy does not fake a pure
creation result.

The decision intentionally covers motion output itself, not just bloom:

- `MotionOutput::render_state`, state resynchronization and sentinel-state
  capture read render state, viewport, shader bindings/constants and stream
  state. Setter shadowing still needs initial reads and resynchronization.
- `HdrPass::save` reads viewport/scissor, vertex state, samplers, texture-stage
  state, render state and constants.
- `TemporalPass::SavedState::capture` uses a state block plus explicit
  viewport/scissor reads; live TAA also depends on the motion route above.
  This is not a claim that every Get* method rejects pure devices:
  [GetViewport explicitly supports them](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-getviewport),
  whereas [GetRenderState does not](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-getrenderstate).
- `BloomPass::save` reads state explicitly; its pure-device refusal remains
  intact for standalone/unmanaged callers, as does its software-VP refusal.

HDR, TAA, materials and bloom are startup dependents of the motion route.
Their later runtime enabled/disabled state does not alter an existing device's
creation contract. In particular, turning bloom off cannot recreate a pure
device. Capability refusal after creation also does not retroactively restore
the pure flag; device capabilities are still independently checked as before.

The capture hook surrounds both direct native factories and the optional
ownership factory wrapper. With ownership enabled, the normalized flags reach
`ownership::create_device` before its native creation and wrapper adoption.
Ownership identity, adoption/refcount rules and failure cleanup are unchanged.
The capture hook makes exactly one saved-factory call. There is no retry with
pure flags after failure, extra device reference, output preclear or private
copy of presentation parameters. Native HRESULTs and parameter/output writes
retain the existing direct or ownership-wrapper behavior; a device is hooked
only after successful creation with a non-null returned interface.

Reset and ResetEx do not accept creation behavior flags. Existing devices keep
their effective creation policy across loss/reset, while the existing renderer
resource revocation/recreation and ownership cleanup run unchanged. A fresh
CreateDevice attempt independently applies the same startup policy.

The separate `Direct3DCreate9Ex` export forwards without capture adoption, and
same-pointer Ex factories detected by `hook_direct3d` still have only ordinary
CreateDevice slot 16 hooked, not CreateDeviceEx slot 20. This change does not
claim enhancement support or flag normalization for that bypass. The existing
test-only Ex adoption seam qualifies ResetEx cleanup, not production Ex factory
support; see [platform portability](platform-portability.md). Diagnostic-only
capture with motion output off likewise retains the requested flags; this is
not a new claim that all diagnostic getters work on pure devices.

## Verification and performance

The focused host fixture extracts the actual capture creation function and
executes it against scripted factory/device doubles. It checks direct and
ownership forwarding, flag preservation, one-call behavior, native failures,
null/untouched outputs, presentation-parameter mutation and hook admission.
The existing capture bloom lifetime fixture covers ordinary Reset, ResetEx,
loss/failure revocation and final-reference interleavings. These are host
control-flow tests, not native D3D behavior or CPU/SEH ABI execution.

Focused results: creation **18 scenarios / 218 assertions**, existing bloom
lifetime/Reset **33 scenarios / 139 assertions**, both passing. Reproduce with:

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_capture_device_creation verification.analysis.test_capture_bloom_lifetime
```

The changed capture translation unit also cross-compiles with strict project
warnings and the x86 SSE2/four-byte stack contract:

```sh
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Wno-cast-function-type -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -c src/proxy/capture.cpp -o /tmp/x3-bloom-pure-device-capture.o
```

Independent source/fixture review approved the five-file change after fixing
the policy log's unsigned argument type; no concrete findings remain.
Diff checking passes. No DLL build, Wine execution, install or gameplay was
performed for this source change. Native Windows runtime behavior remains
unverified; these checks remove the known creation-policy blocker without
establishing that a live bloom frame now prepares or commits successfully.

The new production work is one flag selection/bit clear and one existing-log
call per device creation attempt, under the already-held hook lock. There is
no added per-frame/per-draw work, allocation, capability query or device call.
The backend may perform more state bookkeeping without the pure optimization;
its cost is not measured by a host bit-operation benchmark and remains a
runtime performance item. This change does not claim a gameplay speedup.
