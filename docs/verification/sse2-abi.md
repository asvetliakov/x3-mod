# SSE2 arithmetic and legacy x86 stack entry

The supported CPU arithmetic baseline is SSE2, while the game's existing Win32
calling ABI remains intact. The recommended explicit proxy/fixture flags are:

```text
-msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2
```

The incoming boundary exponent `2` means **four bytes**, not two. This records
the legacy caller contract; stack slots that need 16-byte alignment are aligned
inside our function. Keep ordinary calling conventions, structure packing and
floating-point return conventions. Do not add global fast-math, AVX/FMA,
`-mno-fp-ret-in-387`, or a 16-byte incoming-stack assumption.

Microsoft documents the x86 stack's four-byte alignment and the standard
argument/register preservation conventions. GCC documents stack realignment for
mixed legacy/SSE callers and the incoming-boundary option.
[Microsoft alignment](https://learn.microsoft.com/en-us/cpp/build/x64-software-conventions?view=msvc-170#x86-alignment),
[Microsoft calling conventions](https://learn.microsoft.com/en-us/cpp/cpp/argument-passing-and-naming-conventions?view=msvc-170),
[GCC x86 options](https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html).

## Installed compiler evidence

Tested the installed `i686-w64-mingw32-g++` GCC 16.2.0 on CrossOver Preview,
using an original console executable. No game, DLL installation or bottle
setting change was involved.

The existing `-msse2 -mfpmath=sse` compiler configuration **already realigns the
tested callbacks**. Although `-Q --help=target` reports `-mstackrealign` disabled,
the generated stdcall, thiscall and cdecl functions contain:

```asm
pushl %ebp
movl  %esp, %ebp
andl  $-16, %esp
subl  $32, %esp
```

Thus this investigation did not reproduce a failure in the original SSE2 flag
pair. The extra flags make the external stack assumption explicit; they are not
evidence that every default-built proxy callback was previously unsafe.

The [fixture](../../verification/probe/sse2_abi.cpp) uses naked assembly to enter
each callback with pre-call ESP modulo 16 equal to 0, 4, 8 and 12. Each callback
performs packed SSE2 arithmetic through volatile vector stack locals. Checks
cover exact stack cleanup, integer return, both stack arguments, thiscall ECX,
EBX/ESI/EDI preservation, 16-byte local alignment, and numeric results. EBP anchors
the caller's restoration frame, so successful return also exercises it.

| Flags added to SSE2 baseline | Result |
| --- | --- |
| None | 12/12 callback cases; scalar return passes |
| `-mstackrealign` | 12/12 callback cases; scalar return passes |
| `-mstackrealign -mincoming-stack-boundary=2` | 12/12 callback cases; scalar return passes |
| `-mno-stackrealign -mincoming-stack-boundary=4` | Negative control: SIMD local at address modulo 16 = 4 |
| `-mstackrealign -mincoming-stack-boundary=4` | Same negative control: the explicit incoming assumption wins |

The negative controls are intentionally incompatible build configurations, not
the original baseline. They emit aligned SSE memory operations against a stack
that the legacy caller did not align. This runtime completed the `movaps`
instruction instead of raising an alignment access violation; the fixture still
detects the address violation and exits 1. It also recognizes the corresponding
specific misaligned `movaps` exception on runtimes that trap. Arithmetic output
alone would falsely pass this test.

GCC's `ix86_minimum_incoming_stack_boundary` prioritizes an explicitly supplied
incoming boundary before its `-mstackrealign` handling. This corroborates why
adding `-mstackrealign` does not rescue the contradictory incoming-16 flag.
[GCC implementation](https://github.com/gcc-mirror/gcc/blob/master/gcc/config/i386/i386.cc).

## Floating-point returns and naked SEH thunk

The separate scalar function `a * b + 0.5f` uses `mulss` and `addss`. Its final
store and `flds` move the result into ST0 to satisfy the i686 floating-point
return ABI. This is an ABI transfer, not x87 arithmetic. The runner inspects this
function's disassembly and rejects x87 arithmetic instructions. Removing every
`fld` from the module would be the wrong acceptance criterion; external/runtime
code and required return adaptation may still contain x87 instructions.

The runner separately compiles the actual production
`src/proxy/object_trace.cpp` under the default, realignment, and explicit-legacy
configurations. The normalized instructions of naked `x3m_object_dispatch` are
identical in all three. Its handwritten EBP frame and FS:0 SEH registration are
not replaced by a compiler prologue. Existing `force_align_arg_pointer`
annotations on enter/leave/unwind helpers should remain. This comparison does
not replace the full object-trace exception/rollback fixture after changing
production build flags.

## Reproduction and limits

Run `python3 verification/probe/run_sse2_abi.py`. It freshly builds all variants,
checks source/executable stability, retains selected disassembly, and writes the
[numeric output](../../verification/results/sse2-abi.txt) and
[commands, hashes and results](../../verification/results/sse2-abi-summary.json).
Build products remain untracked under `verification/probe/build/sse2-abi/`.

This is a focused calling-ABI and code-generation proof, not a complete ABI audit,
gameplay regression, performance measurement or promise that arbitrary inline
assembly becomes safe under compiler flags. Production rebuild and affected
integration/temporal/object-trace regressions remain the integration owner's
responsibility.
