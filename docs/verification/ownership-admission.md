# Ownership application-entry admission

Every one of the **297 normal-D3D9 ownership methods across 15 interfaces** now
creates an `ApplicationAdmissionAbi` before its existing registry access, input
unwrapping, native dispatch or output publication. The process monitor is enabled
only by the immutable diagnostic `X3M_ADMISSION=1` setting. It is disabled by
default. This checkpoint adds ordinary application accounting; it does **not**
enable production replay or establish complete proxy/startup/window coverage.
See the [core](application-admission.md) and
[ABI adapter](../architecture/application-admission-abi.md) contracts.

## Entry and callback rules

Generated methods keep their existing arguments, native calls, outputs and
return handling. Ordinary nested callbacks share the outer root. A waiting root
cannot dispatch while replay holds exclusivity. Admission spans native return
through output adoption/publication and ordinary wrapper cleanup.

Final child Release keeps its ticket through native release, deferred metadata
retirement and wrapper deletion. It explicitly finishes that ticket before
calling the parent's application Release. The parent therefore enters through
its normal application vtable without an obsolete child ticket. Nonfinal Release
uses the usual adapter destruction at return. Same-thread/LIFO completion remains
required; no refusal path invents an HRESULT or bypasses active replay.

All seven application `SetPrivateData` families permanently veto with
`PrivateUnknown` before dispatch when `D3DSPD_IUNKNOWN` is present, including
failed calls and a caller using an internal metadata GUID. Native-only sidecar
registration remains an internal call site, not a GUID exemption. The eight
methods accepting a `HANDLE*` veto `ExternalResource` when that pointer is
nonnull, including failed requests and initially null handle values.
`RegisterSoftwareDevice` vetoes `UnobservedRoute` before nonnull callback
registration. Foreign interface input unwrapping records the same veto and still
passes the original pointer to native validation. It releases the registry mutex
before acquiring the admission mutex.

Trusted renderer inspection APIs do not acquire ordinary application tickets;
adding those indiscriminately would treat internal replay inspection as an
application callback. This is not a claim that all native escapes are observed.
The process monitor being clean is necessary evidence, not replay eligibility.

## Emitted boundary

The generator wraps only its forwarding definitions in GCC's scoped
`optimize("no-exceptions")` option. Unsupported compilers fail explicitly.
Handwritten ownership helpers retain their exception handling. This avoids
compiler SJLJ calls outside the generated method's adapter while retaining the
existing helper error handling. The ABI adapter itself still uses its enforced
translation-unit build option.

The fixture runner matches the exact generated method names against all **297
actual object symbols**, verifies that each contains admission and process-monitor
references, and rejects SJLJ/personality/exception-table references in those
methods. It also verifies that handwritten ownership code still contains EH
registration. Object and per-method hashes bind this audit to the native fixture.

The guarantee remains ordinary-return state transport at the reviewed entry
boundary. Selected runtime witnesses compare complete x87 state, MXCSR and
LastError at actual native entry and application return. This does not prove
whole-call CPU behavior for every handwritten helper or certify outer capture,
loader or window-hook prologues. Exceptional unwinding across these generated
shells has no supported recovery or state-restoration contract.

## Original native fixture

`python3 verification/probe/run_ownership_admission.py` builds the actual
ownership module with the admission core and separately compiled ABI adapter.
Each mode runs in a fresh process, so permanent vetoes and immutable process
configuration cannot conceal a missing first registration. The fixture loads
normal D3D9 and creates its own small hidden device; it does not load the proxy,
launch the game, or install anything.

The twelve modes cover:

- Enabled and disabled wrapper dispatch, exact native scalar result, full incoming
  and outgoing CPU state, and balanced ticket retirement.
- A real wrapper waiting before native dispatch while fixture-only replay holds
  exclusivity, then dispatching once and retaining admission until native return.
- A barrier in an actual returned native object's QueryInterface during wrapper
  adoption, showing admission remains active and the application output remains
  unpublished until adoption finishes.
- Final native child release followed by the parent's application Release,
  including a fresh parent root and balanced state afterward.
- Texture, cube texture, volume texture, surface, volume, vertex buffer and index
  buffer private-IUnknown registration. Each first invokes an injected failing
  native endpoint that calls the supplied IUnknown and checks veto-before-dispatch
  with an internal-GUID spoof. It then performs actual successful native
  registration and cleanup, checking callback retention and release balance.
- Failed shared-handle, foreign-buffer and software-callback requests, checking
  that veto precedes exactly one unchanged native dispatch.

Per-instance native vtable spies and the standalone core's replay promotion are
fixture controls. They do not install production replay hooks. Successful real
private-data registration complements the injected failure paths; the fixture
makes no broader assertion about arbitrary third-party callback behavior.

The retained run passed **147 checks in 12 modes**, with **14 timing samples**.
For a wrapped `GetAvailableTextureMem` forwarding to a fixed native scalar spy,
seven samples of 100,000 calls followed one 10,000-call warmup in each process:

| Mode | Median per call |
| --- | ---: |
| Admission disabled | 5.806 ns |
| Admission enabled | 185.296 ns |

The timed loop includes result accumulation. These diagnostic Preview timings
measure the ownership entry plus its fixed native spy. They exclude real driver
work, outer capture CPU boundaries, contention, and gameplay. They are neither a
whole-proxy hook budget nor a native-Windows result.

`verification/results/ownership-admission-summary.json` retains exact mode/check
inventories, source hashes before and after build/run, executable and report
hashes, observed Wine-launcher stability, all 297 per-method object audit hashes,
and raw timing samples. The full object dump stays local under the ignored probe
build directory. No DLL-version allowlist or private runtime layout is used.
