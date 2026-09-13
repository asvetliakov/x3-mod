# Chase callback compiler boundary

2026-09-13. Independent review of the new firing observer exposed an existing
camera build issue: with MinGW's normal C++ exception policy, compiler-generated
SJLJ registration can precede `PreserveCpuState` construction, and unregistration
can follow its destruction. Neither helper is covered by our CPU-state or
LastError preservation. This is a correctness gap at a mid-function injection,
even when a particular CrossOver runtime happens to preserve the observed state.
It is not an explanation established for any reported gameplay symptom.

The camera has no throwing operation or exception handler of its own. CMake now
builds `chase_camera.cpp` with source-specific `-fno-exceptions`, as it already
does for the admission ABI adapter and now does for `chase_aim_trace.cpp`.
Other translation units keep their normal exception policy. SSE2 arithmetic,
four-byte incoming-stack realignment, the emitted register-saving trampoline
and the native displaced instructions are unchanged. This does not add recovery
from a native access violation or authorize exceptions to cross the injected
callback.

## Object inspection

The existing `build-ownership` camera object contains
`_Unwind_SjLj_Register` before `GetLastError`/`fnsave`, and
`_Unwind_SjLj_Unregister` after `SetLastError`. That object is evidence of the
compiler behavior, not a claim that its bytes are the current installed DLL.
The local disassembly is `/tmp/x3-chase-existing-object.txt`.

A new x86 object compiled with `-O2 -g -DNDEBUG -msse2 -mfpmath=sse
-mstackrealign -mincoming-stack-boundary=2 -fno-exceptions` has no SJLJ or
personality reference. Inspection of `x3m_chase_camera_enter` shows:

- Integer stack alignment and XMM zero/store instructions precede the first
  call, `GetLastError`; XMM registers are already saved by the generated stub.
- `fnsave`/`frstor` and `stmxcsr` save the caller's floating-point state before
  `fninit` and the local round-to-nearest, masked MXCSR load.
- Both timed and untimed paths reach the same final `frstor`/`ldmxcsr` and
  `SetLastError` restoration, followed only by an integer stack/register
  epilogue and return.

The exploratory object and disassembly are under `/tmp/x3-chase-abi-audit/`.
Source work continued after this inspection: final qualification must audit the
actual candidate objects and retained build flags, not reuse this exploratory
object as final provenance. Synthetic runtime preservation checks and native
Windows execution are distinct; native Windows remains untested.

## Performance scope

The correction removes per-callback exception registration/unregistration.
It adds no draw work, memory reads, locks or allocations. No timing improvement
or game-FPS benefit is claimed without measurement. The CPU-state saves and
restores remain required even though the camera's arithmetic uses SSE2, because
the interrupted engine may have live x87 state and its own MXCSR settings.

Independent source review approves this correction. A clean RelWithDebInfo
candidate now reproduces the same boundary audit for both the camera and aim
callback objects, with no exception-runtime symbols and the expected
save/initialize/restore instruction inventory. The 164 production input files
match before and after that build; its DLL is SHA-256
`2981bf032be8c7e91013e1de7f355d83fb49f4b2ba778b2b5917787f48a9297c`.
The records are under `/tmp/x3-chase-elevated-qualification-UwDzq7/`, including
`callback-object-audits.json` and the production source manifests.

Runtime qualification is complete for the consolidated candidate. The first two
fixture attempts stopped before any hook because late address reservation
conflicted with startup allocations; they establish no preservation result.
The corrected loader-owned synthetic PE mapping retains the production site
addresses and passes all six X3 cases / 124 checks, including actual trace-stub
CPU-state preservation. A separate X3 camera-math host and the exact DLL export
load checks also pass. These do not execute the live game camera handler or
verify native Windows. See [review 49](review-49-chase-aim-trace.md) and the
[runtime summary](../../verification/results/chase-elevated-runtime-summary.json).
The installed build remains the version at the top of `docs/status.md`.
