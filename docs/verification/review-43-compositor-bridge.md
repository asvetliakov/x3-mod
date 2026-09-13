# Review 43: production compositor CPU/SEH bridge

2026-09-13. Independent Sol high review approved the isolated production
bridge, packaging helper and synthetic fixture with no blocking findings.
This advances the [review-39 prototype](review-39-bloom-return-bridge.md) to
production source; CMake, capture, the game callsite and the installed DLL are
unchanged. Ownership, Reset, GPU recovery and native Windows remain unverified.

The assembly captures actual caller ESP/PC, supports four-byte incoming stacks,
and snapshots the immutable binding into per-invocation storage. It captures
original CPU outputs immediately, before any C/API work, then restores GPRs,
flags, x87 state, MXCSR, all XMM registers and LastError in the reviewed order.
The compiler-generated SEH scope spans pre/original/post. Cleanup and active
count retirement occur on normal return and exceptional unwind. Pre refusal
still calls original once; exceptional pre propagates after cleanup without
calling original. Cleanup must not raise or throw.

The reviewer inspected the emitted Clang registration/scope/finally code and
independently rebuilt the helper. A narrow import archive introduces only
`msvcrt.dll!_except_handler3`; it does not rebind unrelated CRT functions. The
GNU-linkable copy explicitly removes only unsupported `.sxdata` metadata,
preserving other section bytes and relocations. This retains the current
project's lack of SafeSEH support. The raw object is preserved, and final proxy
imports/load configuration still require integration review.

Both Steam and X3/FEX passed **393 checks, zero failures**, using the identical
EXE and twelve matching before/after/current inputs. Six host verdict controls
passed. The inventory includes eight normal, sixteen exceptional, four
continued-exception and four declined-admission cases over four stack
alignments, plus caller metadata, opaque storage and binding/active-count
checks. The [summary](../../verification/results/compositor-bridge-summary.json)
binds both retained terminal transcripts, their runtime records, inputs and
isolated packaging evidence.

Zero active invocations is necessary but does not prove patch/module
quiescence: counter operations have entry/exit instruction windows. Binding
storage requires external admission stop and actual quiescence. The cleanup
contract begins once the compiler SEH frame is established; stack corruption,
binding lifetime violations and cleanup raising are outside the qualification.

The bridge uses fixed stack storage and two counter updates per compositor,
with no heap allocation or per-draw work. Callback-empty cost is not measured.
See [packaging and reproduction](../architecture/compositor-bridge-packaging.md).
