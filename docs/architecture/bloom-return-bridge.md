# Bloom original-call return bridge prototype

2026-09-13. **Verification-only; no production integration, game patch, install,
GPU work or game launch.** Native Windows has not been tested. This qualifies a
possible CPU/SEH transport for the invocation lifetime contract in
[hdr-bloom-boundary.md](hdr-bloom-boundary.md#ownership-lifetime-and-return-bridge),
not the bloom renderer or its device/Reset ownership.

## Prototype and evidence

Sources are `verification/probe/bloom_return_bridge.h`, `.S`, `_seh.c`,
`_fixture.c`, `_build.py` and `_run.py`. Build without Wine:

```sh
python3 verification/probe/bloom_return_bridge_build.py
```

Run only when the game is closed and the shared fixture lease is available:

```sh
python3 verification/probe/wine_lock.py --holder bloom-return-bridge --timeout 60 python3 verification/probe/bloom_return_bridge_run.py
```

The runner also checks game/other-fixture processes and has a 60-second fixture
timeout. Its default remains the `Steam` bottle. Source/EXE SHA-256 provenance,
bottle architecture/environment, exact command, outcome and raw output stay in
untracked `verification/probe/build/bloom_return_bridge_Steam.{json,txt}`.
The X3 run uses the identical EXE and records
`build/bloom_return_bridge_X3.{json,txt}`: `WineArch=arm64`,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`. Steam records `WineArch=win64`,
`WINEMSYNC=1`. Both are CrossOver Preview; neither is native Windows.
The compiler assembly is `build/bloom_return_bridge_seh.s`; the linked EXE
listing is `build/bloom_return_bridge_disassembly.txt`. These are synthetic
listings, without game bytes. The JSON was checked by the runner's terminal
inventory parser, not accepted on executable exit status alone. It requires
exactly `EXPECTED_CHECKS=240`, one correctly formed case inventory, zero fixture
failures, no FAIL lines and a zero exit status. Source/EXE hashes are captured
before and after execution, and a changed input set or hash fails the verdict.
Six host controls in `verification/analysis/test_bloom_return_bridge.py` cover
valid output, under/over counts, failing exit status, FAIL lines, duplicate
records (including a malformed duplicate) and missing/changed/failed inventories.
They pass with `python3 -m unittest verification/analysis/test_bloom_return_bridge.py`.

Recorded Steam and X3 executions: **240 checks, zero failures per bottle**; four ordinary returns,
16 abnormal exits and four continued exceptions, each across all four incoming
ESP residues permitted by four-byte alignment. Specifically:

- Pre deliberately damages x87, XMM, MXCSR and LastError. The original sees the
  direct-call input GPR/flags/FP/LastError values.
- Original deliberately changes every GPR, including ABI callee-saved registers,
  flags including DF, live x87 data/tags/control/status, every XMM register,
  MXCSR rounding/status and LastError. Post and finally both damage FP and
  LastError. The final continuation inherits direct-call outputs; post also
  receives the original output snapshot captured before C executes.
- Exact harness caller ESP is restored. PUSHAD's ESP field is informational,
  not restored with POPAD; original necessarily runs at a deeper stack address.
- Exceptions raised in pre, original and post, plus an original-body hardware
  write access violation, reach the compiler-generated outer filter with their
  code and parameters intact. Search sees the live ticket and no cleanup;
  unwind releases it exactly once before the outer handler executes. Exceptions
  in pre/original never enter post.
- A continuable original exception whose outer filter requests
  `EXCEPTION_CONTINUE_EXECUTION` resumes original, executes post and normal
  cleanup. Merely visiting an exception filter does not revoke the ticket.

x87 comparison covers control/status/tag words and each tagged live 80-bit
register. It excludes reserved words, instruction/data pointers and contents of
empty slots. Full 108-byte FNSAVE/FRSTOR transport is used in the implementation;
this test does not claim instruction/data-pointer equivalence. Fixtures use
only original synthetic state patterns and code.

## CPU and frame structure

`bloom_return_bridge` is an assembly no-stack-argument entry. Its first two
instructions capture flags and all GPRs. It allocates a 572-byte invocation on
the stack, with bounded alignment padding; captures x87, MXCSR and XMM before
calling `GetLastError`; clears DF and establishes default injected FP state;
then calls `bloom_return_seh(record)` through a plain C ABI. No fixed TEB offset,
Wine export, backend-private lock/layout, process-global invocation token or
heap allocation is involved.

The C wrapper is exactly a compiler-supported `__try` containing pre,
`bloom_return_invoke`, and post, followed by `__finally` calling cleanup with
`__abnormal_termination()`. The finally does not return, catch, translate or
rethrow an exception. The original exception continues through normal Windows
SEH dispatch. Production cleanup would need to be nonthrowing and idempotent;
the fixture's integer ticket only represents that release action.

`bloom_return_invoke` separately saves its C caller's GPRs/flags. It restores
original input LastError through `SetLastError`, then restores FP/XMM, then
POPad/flags, and executes a real indirect CALL to the no-argument original.
The target and record addresses survive in invocation-local stack scratch,
independent of whatever values the original leaves in any GPR.

The instruction immediately following that CALL is PUSHFD, followed by PUSHAD.
Only MOV/LEA instructions precede FNSAVE, STMXCSR and the eight XMM stores.
Only after those stores does it clear DF, load injected default MXCSR and call
`GetLastError`. No callback, C epilogue, logging, destructor or lock occurs
before capture. It then returns to the SEH wrapper with its C caller's saved
GPRs, while original output remains in the record. Normal post, finally and
the compiler's SEH unlink/epilogue all complete before entry assembly restores
captured output LastError, x87/MXCSR/XMM and GPR/flags and returns to the actual
caller continuation.

During abnormal unwind, the compiler's SEH registration spans the entire
original call even though the original is reached through assembly. The stack
record remains live until cleanup finishes. The normal restoration epilogue
is not reached on unwind, so it cannot overwrite the exception context with a
normal-call output or run post after an original exception.

## Toolchain feasibility and limitations

The installed i686 MinGW GCC 16.2.0 uses its normal GCC exception model, not
Microsoft `__try`/`__finally`. C++ RAII is not a substitute. Apple Clang 21.0.0
(`clang-2100.1.1.101`) with `i686-w64-windows-gnu` accepts the syntax but fails
COFF assembly because its generated `L__ehtable$wrapper` is undefined.
The **`i686-pc-windows-msvc` target** emits the registration, scope table and
finally funclet successfully and interoperates with this MinGW plain-C caller.

The build uses Clang only for `_seh.c`, with SSE2, SSE arithmetic, stack
realignment and a four-byte stack alignment contract
(`-mstack-alignment=4`; Clang does not accept GCC's incoming-boundary switch).
Other C/assembly is built with the project's
`-msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2` flags. The C SEH
object uses no C++ exceptions, C++ library objects or shared CRT ownership.

Inspection of the generated assembly and linked disassembly confirmed a
compiler-generated `_except_handler3` registration before pre, active scope
state covering pre/invoke/post, a null-filter termination scope referring to
the finally funclet, and normal unlink after cleanup. The abnormal funclet
passes `1`, whereas the normal cleanup call passes `0`. The handler is imported
from `msvcrt.dll` using the MinGW `-lmsvcrt-os` import library. This is compiler
runtime support, not a manually written FS-chain handler. The fixture's remaining
CRT startup/stdio imports include UCRT API sets; it is not a qualified production
CRT-link recipe.

GNU PE ld does **not** consume MS COFF `.sxdata` SafeSEH symbol-index metadata.
Leaving it in produced a section-below-image-base warning. The build preserves
the original compiler object, creates a separate link object with only `.sxdata`
removed using objcopy, and links without that malformed section. The runtime
scope table and generated code are untouched. **This executable has no SafeSEH
load-configuration table, and no SafeSEH support is claimed.** A compatible
linker/runtime packaging decision, a production import audit and native Windows
execution remain integration gates. Do not copy the build workaround into
production without reviewing that security/linker contract.

[Microsoft's termination-handler contract](https://learn.microsoft.com/en-us/windows/win32/debug/termination-handler-syntax)
establishes finally execution on unwind before the selected outer handler.
[Clang's SEH compatibility documentation](https://clang.llvm.org/docs/MSVCCompatibility.html)
describes partial asynchronous-exception support and the limitation for faults
in the catching frame itself. Here original and callbacks are out-of-line
calls; the hardware-fault fixture is in a separate original-body frame. This
does not promise cleanup after stack corruption, exhausted stack, process
termination, fail-fast, or an exception thrown by cleanup itself.

## Integration and performance boundaries

The existing `scene_hook.cpp` tail-jumps original; no production file uses this
prototype. [The game ABI notes](../reverse-engineering/bloom-compositor-skip.md)
establish a no-stack-argument original ending with plain RET. A focused read of
local `/tmp/x3-bloom-skip/compositor-ins.txt` also confirms the original installs
its own SEH registration and allocates its own stack locals. Those observations
are compatible with an extra wrapper CALL, but this synthetic fixture is not
proof that all transitive game code tolerates a changed immediate return PC or
stack depth. Game admission still needs the validated callsite and continuation,
patch ownership/rollback, original ABI and targeted transitive-stack review.

There is no per-draw work, heap allocation, mutex, repeated device validation or
image readback. Each invocation has fixed stack storage and four full x87
transport operations (two FNSAVE, two FRSTOR), four Win32 LastError calls and
bounded XMM/register copies. This is intended for the once-per-compositor
boundary. No diagnostic CPU timing or game FPS improvement is claimed; measure
its empty-callback incremental cost when qualifying integration.

Still absent: real owner/thread/generation admission; retained COM references;
Reset/destruction revocation releasing all default-pool references before native
Reset; reentrancy/device selection; actual post-GPU state restoration; and
successful/failed renderer invocation behavior. Stack-local transport does not
supply those lifetime rules. The next architecture layer must register/revoke
its resources and make cleanup nonthrowing/idempotent before this can be used
by production bloom.
