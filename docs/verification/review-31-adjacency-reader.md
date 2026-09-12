# Review 31: adjacency parity and resource-reader cursor

Independent review of main checkpoint `1c8322e` (documentation-only HEAD
`f4d2384`), 2026-09-13. Production source was frozen. This review did not run
Wine, launch the game, install a DLL, or modify production source. Review 30
owns the contemporaneous native fixture runs; their results are separate.

## Adjacency findings (binary_research)

**Not accepted as a general fast-mode parity claim.** The generic dispatch
correction has useful game-bottle evidence, but the admitted SSE2 path has
known differing adjacency output, and a registry-type mismatch can select the
wrong normalizer. Fixes below are deferred until root releases the next
checkpoint; they are not implemented by this review.

### A1 — admitted SSE2 competing normals can return wrong adjacency

`src/proxy/loading_trace.cpp:255` chooses the SSE2 policy, and
`adjacency_fast_service` at line 298 returns `S_OK` whenever computation
succeeds. There is no refusal for the known competing-normal discrepancy.
The inherited, local replay evidence in
[the parity handoff](handoff-adjacency-parity.md) reports:

| Exact local game-dump collection | Native vs module |
|---|---:|
| X3/FEX, corrected generic normalizer | 37 / 37 equal |
| Steam/Rosetta, SSE2 normalizer | 33 / 37 equal |

These are the same geometry inputs, not two interchangeable native oracles.
Native Steam output itself differs from the original X3 dump for 36 / 37.
Three failing dumps (3, 13, 34) contain the same mesh and seven differing
entries; dump 9 has four. Thus this is four files, not four independent mesh
shapes. The inherited minimal reproduction is
`/tmp/x3-adj-bisect2/mesh-adjacency-3faces-9v.bin`: despite its historical
filename it has three faces and seven vertices, derived from faces
103/446/789. Steam native pairs face 0 with 2; the module pairs 0 with 1.
Those raw game-derived inputs remain untracked. This review has not rerun them.

**Bounded fix:** retain the generic path, and refuse publication of the module
result for SSE2 meshes requiring competing-normal selection until the residual
is resolved. One small implementation is to compute into private adjacency
scratch, inspect `report.multi_candidates`, and fall back to the original if
nonzero; copy to application output only after acceptance and successful input
unlock. A core preflight that refuses before its first output write can avoid
that scratch, but must preserve the documented output-untouched-on-refusal
contract. Merely setting `normal_selection=false` changes which face wins and
is not a correction. Verification mode should retain both computations so the
residual remains visible. Count this refusal separately from unsupported CPU
math tables. The existing whole-result cache must cache the actual selected
service result, not a rejected partial candidate.

A more conservative all-SSE2 fallback is also correct but gives up more work.
The proposed multi-candidate fallback addresses this known counterexample;
it does not independently prove every remaining input or native Windows.

### A2 — registry type is not mirrored

`src/proxy/loading_trace.cpp:159–162` reads a four-byte value without requesting
its type. D3DX's helper at VA `0x00587aa0` does request `lpType` and checks it
against the caller's expected `REG_DWORD` (4); the decisive comparison is at
`0x00587aec–0x00587af2`. For example, a four-byte `REG_BINARY` value containing
integer 1 is accepted by the module and ignored by native D3DX. On an ordinary
SSE2 machine that can select Generic in the module while native uses SSE2.

**Fix:** require `type == REG_DWORD` as well as successful read and the expected
size. Add controlled query-result cases for absent values, valid DWORD 0/1/2,
wrong type with four bytes, wrong length, and the D3DX-specific override.
No change to the user's registry is necessary. The real-bottle absent-key
observations do not test this case. The native helper itself does not explicitly
compare returned size; rejecting a malformed DWORD length is a conservative
boundary, not evidence that the two parsers accept all malformed data equally.

### A3 — floating-point and dispatch scope must be enforced or qualified

The production normal/cross/dot model uses SSE2 doubles and float stores;
`loading_trace.cpp:300` forces MXCSR `0x1f80`. Native cross/dot and the generic
normalizer execute x87 under the caller's precision and rounding controls.
The model is derived for 53-bit, round-to-nearest x87 operation, as in the game
inputs (`0x023f` or `0x027f`). A 24-bit or 64-bit precision caller, or directed
rounding, is outside that derivation. The fast service currently checks neither.
The SSE2 native normalizer also uses the caller's MXCSR, whereas the model
always uses nearest with FTZ/DAZ disabled. Six selected meshes agreeing across
seven environments cannot establish equality for all cancellation/near-tie
inputs. This review identifies an unsupported arithmetic domain, not a newly
measured additional output mismatch.

**Small safe admission change:** fall back to native outside the explicitly
supported precision/rounding domain before publishing computed output. For
Generic, 53-bit round-to-nearest is the arithmetic premise; record exception
mask/status preservation separately. For SSE2, either preserve its actual
rounding/denormal semantics with corresponding proof or refuse arithmetic-
dependent competing-normal work as in A1. Do not infer an FP-domain proof from
an FP save/restore test: the latter establishes state transparency only.
The permanent lookup of registry/CPU inputs also assumes those inputs describe
the D3DX instance's startup dispatch; changing registry settings after D3DX
initializes can invalidate the mirror. Normalizer capability validation should
be process/module scoped, use narrow behavioral or local dispatch evidence,
and retain native fallback on unknowns. No global DLL hash gate is recommended.

## Independent disassembly result

Fresh read-only Ghidra decompilation compared the 16-bit converter
`0x00597e88` with the previously analyzed 32-bit converter `0x00598226`, and
their shared lookup `0x00590630`. Both converters pass the reverse edge's
endpoints followed by the query's third representative to the same lookup;
both retire the selected candidate and querying entry, reject duplicate
neighbors against earlier slots, and write the same reciprocal corner.
The 16-bit path changes index loads and index stride, not these rules.
Consequently the suggested 16-bit argument-order/duplicate-rule explanation
for the residual is not supported by this inspection. It does not establish
why the native Steam three-face case chooses a different candidate.

The shared lookup recomputes the current winner's and candidate's normal
scores and rounds scores to float before comparing. The module's cached
normal order and strict comparison agree structurally. A future bounded trace
should capture the *actual* lookup's two score stores, x87 condition/status and
chosen entry for that three-face case; calling the score helper separately
has already failed to reproduce the choice. Do not change tie-breaking based
on a guessed Rosetta comparison bug.

The generic interpolation rule, 512-entry mathematical table construction,
near-unit bypass and generic/SSE2 branch distinction agree with the retained
normalizer analysis. Actual Windows has not been tested. The dispatch uses
public registry/processor APIs and portable x86 instructions, but that is source
portability, not verified native-Windows adjacency parity. SSE and 3DNow are
explicitly refused; this review does not propose implementing them.

## Retrospective verification limits and next acceptance

The earlier large synthetic suite exercised authored geometry and did not
include the minimal game-derived ambiguous fan. Its previous zero mismatch
count therefore cannot overrule run 11 or the residual Steam replay. The new
near-unit-normal negative control usefully distinguishes generic from SSE2,
but does not close all edge-ranking behavior. Likewise 37 / 37 on X3 is exact
for the retained collection and environment, not a universal algorithm proof.

After the separate review-30 install, the next fix checkpoint should combine
A1, A2 and an explicit FP-domain policy; retain a native fallback witness for
the minimal residual and all 37 dumps, plus authored nonmanifold/near-tie,
16/32-bit, malformed registry-result, non-default rounding and unchanged-output
refusal cases. Confirm native is invoked with original incoming FP/LastError
and no locks held after rejection. Keep fast mode off for unqualified domains;
do not silently equate the user requesting `fast` with parity certification.
No runtime tests are claimed by this independent review.

## Review provenance

Reviewed source SHA-256 (not runtime version gates):

| File | SHA-256 |
|---|---|
| `src/proxy/loading_trace.cpp` | `2647a35804140affaeaf98f503d61fb8fa3b7a13e382b3c88546f0d0aa9a51df` |
| `src/proxy/mesh_adjacency_fast.cpp` | `b96b0ad2a870bb304454aed6fd6a2b5220364f427664add99f4b83b3e9a8c78f` |
| `src/proxy/mesh_adjacency_fast.h` | `3dc326c9d28ed55fe4f483d4eec8c4f4b1f701fa2d6676bc3ff029ebf0d52106` |
| `verification/probe/mesh_adjacency_fast_fixture.cpp` | `248ab24d91ec073c619de67b6e2e0f49f3f0b68e04dcc22a6e3c161ccce3c2f6` |

Target D3DX research provenance is SHA-256
`c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8`,
preferred image base `0x00400000`; VAs above refer to this image.
Fresh private output `/tmp/x3-adj-review31-converters.c` has SHA-256
`7b27ce4e25630a3ec03d7bb4aa35908cb1cb3355b711f8b3cfa52d3039046901`.
It was produced with `X3DecompileFunctions.java` for the three listed functions
using Ghidra `-noanalysis -readOnly` on `/tmp/x3-ghidra-d3dx/D3DX`.
Registry type comparison was additionally checked against the retained local
Intel disassembly, `/tmp/x3-native-d3dx37-disassembly.txt`.

## Resource reader review (2026-09-13)

**Three concrete source findings remain open.** The root agent accepted them
for a separate reader checkpoint after review 30's first install. No reader
production or fixture edits were made during this independent review, and the
reviewer did not run Wine or launch the game. The scheduled X3 fixture belongs
to the review-30 runtime chain.

Reviewed `resource_reader.{h,cpp}`, `resource_reader_core.cpp`, the production
build flags and emitted core object, the reader fixture/runner/host tests, and
[Run C](resource-reader.md#run-c-2026-09-12-bottle-x3-review-29-build-the-cursor-class)
against the [decompiled reader contract](../reverse-engineering/resource-reader.md).
The local disassembly confirms a 1,024-byte request at `0x004e8d55` and exit on
`Z_STREAM_END` at `0x004e8db4`/`0x004e8db7` before requesting another chunk.

### Findings and bounded closure tests

1. **Mismatch logging shifts the argument list** (`resource_reader.cpp`,
   `verify_sink`). The format string omits `cursor_ok=%u`, but the argument is
   supplied before `position_ok`. Subsequent integer fields and x86 variadic
   double reads consequently use the wrong positions. This is reproduced in
   the retained deliberate-tamper line: `cursor=1` and `catalogue=20113`, rather
   than a valid cursor and boolean catalogue flag. Add the missing placeholder
   and assert the authored mismatch line's complete typed fields in the fixture
   report parser. Successful parity counters are unaffected, but the current
   mismatch line cannot diagnose the next game run reliably.
2. **The short-record rewind can fail after success is published**
   (`resource_reader_core.cpp`, `decode`). The guard is disarmed and counters,
   globals and cursor updated before the final `_fseek`, whose return is
   ignored. A failed reposition therefore returns `Handled` while the stream
   remains at the extent end. Perform and check the rewind before publishing;
   on failure free the output and restore entry position for original fallback.
   Add a one-shot final-seek failure seam checking allocation release, unchanged
   counters/globals/cursor and entry position. Permanent inability to restore a
   broken stream is outside the successful-restoration guarantee and must not
   be described as repaired.
3. **Verify mode does not transport the original's incoming LastError**
   (`resource_reader_core.cpp`, `x3m_resource_read_entry`). Scratch decode and
   bookkeeping run between saving entry error and calling the original, without
   resetting it before the call. Also the outgoing error is sampled after QPC.
   Restore the saved entry error immediately before `call_original`; capture
   its outgoing error immediately on return. Use distinct scratch-clobber,
   original-input and original-output sentinels in the fixture. The ordinary
   fallback branch already restores entry LastError.

### Accepted scope and remaining verification gaps

The cursor arithmetic uses the bytes consumed by raw inflate before
`inflateEnd`, rounds to the original chunk boundary, and clamps to the record
length. The fixture independently runs the original chunk loop and checks its
position against a formula from each synthetic gzip extent. Twenty sources
cover remainders 0 through 9 at one and three chunks, scrambled/plain and FNAME
headers. Both loose and catalogue reads are compared; a short-class record is
last in the catalogue without padding, the next dispatcher read is compared,
and the pool reopens/positions/reads the class record and earlier records.
These controls address the reported cursor class; they are not a game-run
acceptance result.

Allocation ownership is consistent on the inspected normal decode/refusal
paths: compressed scratch uses process HeapAlloc/HeapFree; successful fast
output uses the game's malloc and is returned to the caller; rejected output
uses the matching free; verify output uses the process heap and is freed after
comparison while the original's allocation is returned. `inflateEnd` occurs
after successful init even when inflate or produced-size validation rejects.
The pool has 32 fixed slots, does not share an in-use handle, leaves real
fopen/fclose outside the lock on ordinary paths, closes errored streams, and
falls through for full/long-path/untracked handles. Drain closes only kept
handles under the lock at shutdown. The FILE flag offset belongs to the
byte-qualified game's static CRT, not a Wine-private layout.

Performance review found no per-draw work here. The core allocates one
compressed scratch and one output per resource, performs one whole-extent read
and inflate, and adds one rewind only for the short class. The retained core
object contains 2,327 decoded instructions and no x87/SIMD instructions, matching
the no-floating-point/no-boundary implementation. Existing warm-cache fixture
measurements are inflate-bound (96–98% of the fast path, approximately 1.11x
against the reference); they do not establish a game load-time improvement or
native-Windows runtime behavior.

The existing runner accepts duplicate scalar/record fields and incomplete
cursor statistics; bounded host counterexamples confirmed this. Tighten unique
records/keys and the exact cursor inventory (`verify_files=40`,
`verify_equal=60`, `cursor_short=32`, one authored mismatch). The actual fixture
currently checks the complete inventory, so this is an acceptance-parser gap,
not evidence that its executed cases were absent. Complete the source manifest
with `capture.h`, `cpu_state.h`, and `object_trace.h`: compiler `-MM` with the
fixture's actual macros confirms these three missing C++ dependencies.
`mesh_adjacency_cache.h` is conditionally excluded from this target. Record the
runner helpers and host parser tests, verify the copied runtime DLL after the
run, and retain a stdout digest.

### Evidence checked before the fixes

At review time the retained X3 summary reports **4,707 checks, zero failures**;
all 14 declared source hashes and the current fixture executable match. The
cursor inventory is 40 verify calls, 32 short cursors, 60 cumulative equal calls
and one deliberate tamper mismatch. All four existing host tests pass. This is
current evidence for its declared inputs, with the omitted dependencies and
logging/error-path gaps above; it must not be relabelled post-fix evidence.

- Summary: `verification/results/bottle-X3/resource-reader-summary.json`,
  SHA-256 `d1215a03dcfa16c0d628e6208f15c4edfacf10f3dff0c9e95dd7bd9101b37a1e`.
- Fixture EXE SHA-256:
  `999b1f0bf870dd61b88e847aab1aa6ac0d1d245a9bda416596f9b60704e1bb7c`.
- Reviewed `resource_reader.cpp` SHA-256:
  `a0ba9c82792563fbf23bc2b7cdeecde2ce8d92c61b41cd55e8094a6b6f7686e2`.
- Reviewed `resource_reader_core.cpp` SHA-256:
  `39e876e8dd6ac329ccdb85d81be18d2397c27ddf5503c96de88abb64285969ff`.

Closure requires the targeted fixes, independent delta review, expanded host
parser tests and one freshly built reader fixture under the coordinated Wine
lock. The user-managed verify run still must show zero mismatches and the
expected short-cursor class before fast mode is accepted for gameplay.
