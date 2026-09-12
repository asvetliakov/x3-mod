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

## Adjacency implementation follow-up

A1–A3 are implemented after the crypto integration. This subsection records
source/host verification; it does not supersede the native evidence limitations
above or the independently owned reader section.

- Fast SSE2 preflights the completed welded directed-edge table before its first
  output write. A key with two or more entries and an existing reverse key
  returns `CompetingNormals`. Every later competing lookup is covered: its
  candidates were in this completed table, and traversal only retires entries.
  Some refused candidates could have been consumed earlier; conservative
  refusal is intentional. Duplicate keys without a reverse and manifold meshes
  remain eligible. Generic production retains normal selection.
- There is no extra output allocation or copy. Duplicate-free inputs pay one
  boolean update per insertion; only duplicate-edge inputs scan existing slots,
  with expected linear cost. Refusal precedes normal-cache allocation and math.
  Input unlocks complete before restoring incoming FP/LastError and calling native.
- Registry reads require `REG_DWORD`. Successful malformed short DWORDs select
  `Unknown` and native fallback, rather than being mistaken for absent overrides.
  Wrong-type values are ignored as native's type test specifies.
- Fast arithmetic requires x87 53-bit/nearest, all exceptions masked, an empty
  register stack, and MXCSR controls exactly `0x1f80` or game `0x9fc0`. Sticky
  status flags do not affect admission. Generic native scoring/welding is x87;
  admitted SSE2 inputs never compute normals. Thus FTZ/DAZ does not change
  admitted native scoring; module arithmetic retains default MXCSR. All other
  control combinations use native with the exact original FP state and LastError.
- Verify still computes rejected-domain candidates. New cumulative fields are
  `verify_admitted`, `verify_admitted_mismatched`, `verify_refused_fp`,
  `verify_refused_competing`, `fallback_fp_domain`, `fallback_competing_normals`,
  and `module_competing_normals`. Diagnostic refusal counts can overlap;
  `verify_admitted` counts successful comparisons with neither refusal.

`python3 -m unittest verification.analysis.test_mesh_adjacency_fast` passes
**26 tests**. Additions cover 16/32-bit unchanged-output refusal, duplicates
without a reverse, manifold inputs, 100 authored random meshes checking that
preflight covers later normal selection, and the FP-domain predicate.
The compiled native fixture adds eight registry-result controls and 13 requested
FP domains. Its failing callback witnesses untouched output before native dispatch,
incoming FP/LastError and exact failure/output/LastError forwarding. Delivered
domain coverage and the final passes are recorded below.

The new `run_mesh_adjacency_replay.py --dump-dir /tmp/x3-bottleX3-run11/dumps`
witness must run through `wine_lock.py`, after that bottle's fresh
`run_loading_trace.py`. It requires the successful suite's exact source/native/
executable provenance and reuses its binary, avoiding an overwrite of the suite
evidence. Its `replay-safe` path seeds recorded dump FP controls, compares
native/verify/fast results, records refusal coverage, and requires every served
result to equal that bottle's native result. Raw output remains in a new `/tmp`
directory; tracked summaries contain metadata and derived counters only. The
unresolved Steam diagnostic mismatches remain visible even when safely served
by native. No main DLL build or install is claimed here.

### Independent adjacency delta review (2026-09-13)

The A1–A3 production delta and authored native/replay harness are accepted at
source level. Independent inspection confirmed that the conservative complete-table
preflight runs before the first application-output write, malformed successful
DWORD results select native fallback, and rejected FP domains reach the original
with their incoming computational state and LastError restored. The extra edge
scan reuses existing storage and runs only when duplicate directed keys exist;
its cost on refused meshes still needs the scheduled diagnostic measurements.

The native callback controls check untouched output before fallback, a distinct
native failure/output/LastError result, canonical hardware FP state, and separate
refusal counters. The replay runner requires the fresh loading suite's unchanged
source, native DLL and executable provenance, records diagnostic mismatches, and
requires served output to equal the same bottle's native output. Its native/served
QPC samples are fixed-order diagnostics, not an unbiased benchmark or game FPS.
No independent build or Wine run was performed for this review. Fresh scheduled
native controls and local-dump replay remain required before fixture acceptance;
this source acceptance does not establish universal native-Windows parity.

### Final loading and local-mesh qualification

Fresh `run_loading_trace.py` passes **86 / 123 / 36,093 / 36,150 checks** on
both Steam and X3. Each adjacency variant includes 54 authored mesh cases,
2,000/2,000 equal random comparisons, and 109 admission-control checks. Steam
records 13 requested / 12 distinct delivered / 1 canonicalized domain; X3 records
13 / 13 / 0. The real nonempty x87 stack is delivered and rejected on both.
The ordinary authored fast-service phase records 80 computed / 28 fallback
calls on Steam and 104 / 4 on X3, with zero faults; these counts include verify
and fast phases and are separate from the recorded-game replay below.

| Same-bottle replay of 37 local run-11 meshes | Steam / SSE2 | X3 / Generic |
|---|---:|---:|
| Served output equals native | 37/37 | 37/37 |
| Fast computed | 0 | 37 |
| Native competing-normal fallback | 37 | 0 |
| FP-domain fallback | 0 | 0 |
| Verify admitted | 0 | 37 |
| Seeded diagnostic mismatches | 0 | 0 |
| Fixture checks / faults | 642 / 0 | 642 / 0 |

Steam's all-fallback replay is preservation evidence, not fast-path qualification.
All dumps seed their recorded computational controls; this differs from the old
unseeded residual reproduction. The old 4/37 SSE2 mismatch cause remains open,
and the conservative refusal remains enabled. Neither these 37 meshes nor the
authored sweep proves universal native-Windows parity. The next game verify run
must report meaningful admitted coverage on X3 before accepting game fast mode.

Source, native DLL, exact executable and input-dump hashes are checked before
and after the suite/replay. Derived reports are
`verification/results/loading-trace-mesh-summary.json` and
`mesh-adjacency-replay-summary.json`, plus the same filenames in `bottle-X3/`.
Each replay reuses that bottle's freshly qualified suite executable without a
rebuild. To retain both snapshots despite the shared build directory, the three
fixture folders were copied unchanged into ignored
`verification/probe/build/review31-{Steam,X3}/`. Adjacency executable SHA-256:
Steam `02043108c10d2f420c58622c53aca1ed90d834fd24159c19508d828d38664f43`;
X3 `835e65f2ede5fe02db2c4bee84d2f53fad265f31498698aba8d43ccace4d90b3`.
Raw replay output remains outside the repository at the temporary paths in each
summary. No main DLL build, installation, gameplay or native-Windows run occurs
in this qualification.

Performance inspection finds no added allocation or output copy; the complete
edge-table scan is conditional on duplicate directed edges and stops at the first
conservative refusal. Across the 37 fixed-order replay samples, total unhooked
native / served time was **2,408,841 / 2,434,642.9 µs** on Steam (all fallback)
and **196,330.8 / 57,618.5 µs** on X3 (all computed). The approximately 25.8 ms
aggregate Steam difference includes preflight and hook work, but also fixed-order
cache/scheduling effects; it is not an isolated overhead estimate. These are
one-pass diagnostic CPU timings, not unbiased performance trials or game loading
savings. The Generic path retains a useful measured result without broadening
SSE2 eligibility.

### Independent final adjacency artifact acceptance

The fresh loading and local-mesh qualification above is accepted. Independent
recalculation matched both current source maps, native DLL hashes, all eight
loading raw-result/Wine-log pairs, retained Steam and X3 executables and sibling
DLLs, and both replay runner/suite bindings, input hashes and raw-output hashes.
The 74 replay rows reproduce the reported admission counts and timing sums.
The replay summary SHA-256 values are
`17acd28a6f9fbc5d2248c7477a63b91641a6b3965dd044f730b466026b28e224`
(Steam) and
`3475a3eb577b0b0d306a2af3690ac63ec49d0fd87a2e592e382db9e744d38906`
(X3).

The recorded performance scope is appropriate: Steam's roughly 1.07% aggregate
increase is a fixed-order observation with all calls falling back, while X3's
computed path is faster in this sample. Neither number isolates the preflight
cost or establishes game loading savings. This closes the scoped A1–A3 fixture
review, without closing the historical unseeded mismatch investigation, actual
game fast-mode acceptance, or native-Windows runtime qualification. The reviewer
performed no additional native run. Separate cache/hook regressions and the final
integrated DLL remain their own checkpoint gates.

### Qualification control setup corrections

The first Steam refresh failed a newly added refusal-counter assertion and was
not accepted. Cold fixture-only TLS access also changed LastError before the
service; the fixture entry now uses the same `CpuCallBoundary` ordering as the
real hook. The counter failure was a separate issue: requested MXCSR `0x1f00`
was immediately read back as `0x1f80` on Steam, before any API call. A standalone
`LDMXCSR`/`STMXCSR` witness confirms that particular mask canonicalization.
The initial suspicion that a nonempty x87 tag could not be delivered was wrong;
a separate `FLD1`/`FLDZ` witness and the corrected control deliver a real occupied
stack. The fixture creates a physical stack value instead of relying only on
an environment tag write.

All 13 requested controls still test the pure predicate and execute the native
callback preservation checks. A new assertion requires requested computational
CW bits and stack occupancy to survive, and accepts only the observed
`0x1f00`→`0x1f80` MXCSR canonicalization. The expected refusal follows the actual
incoming state; that row is not evidence of an unmasked-exception runtime test.
The marker separately records requested, distinct delivered and canonicalized
domains. The isolated Steam run passed **109 checks**, with **13 requested / 12
distinct delivered / 1 canonicalized**. Both fixture corrections and this scope
were independently reviewed; the production admission predicate did not change.

Rejected evidence remains under `/tmp/x3-adjacency-review31-rejected-steam-1/`
and `/tmp/x3-review31-admission-only-Steam-*.txt`. Local standalone source,
executable, disassembly and output digests are in
`/tmp/x3-review31-control-diagnostics-provenance.json`; the MXCSR witness source
SHA-256 is `4df1a79e493973cb96f1cb3e3866eda6071c09dc3241e4ee028af24aa5a78cf8`,
output SHA-256 `319ff2d8c3057b4e5189ca567a7c49df02566f0ed4825bab65be8c36b57863ac`.
These are observed Steam-runtime facts, not native-Windows qualification.

### Detached cache fixture portability correction

The unchanged detached cache fixture first stopped on X3 at check 303 because it
required native D3DX to alter x87 status on its chosen mesh. All five native
adjacency/clean/optimize parity cases had passed. Native D3DX may legally leave
that status unchanged. The replacement keeps actual-native fill/hit parity and
adds an original callback that performs an equal x87 comparison (`FLDZ` twice,
`FCOMPP`) after native computation. It must produce C3=1/C0=C2=0 and an observed
status change while preserving native control/tag/MXCSR/LastError; miss and hit
must exactly replay that outgoing state and native output. The runner now
requires the explicit authored witness and exact 770-check inventory.

An initial authored environment-write attempt showed requested precision sticky
flags were not observable in this X3 runtime (SW `0x4120` read as `0x4100`,
MXCSR `0x1fa0` as `0x1f80`). The final comparison witness does not claim coverage
of creating those sticky bits. Rejected source/EXE/report snapshots remain local
under `/tmp/x3-review31-cache-X3-rejected/` and
`/tmp/x3-review31-cache-X3-authored-rejected/`. Only the detached fixture and its
runner change; the already qualified loading/replay source map is unaffected.
The later variant sweep now compares the first recorded FP state with the
independently applied MXCSR, and logs requested and applied status separately.
Its key-partition witness uses an independently observed C0 change rather than
an unobservable precision request. These were reviewed as fixture corrections;
production key/FP behavior and all actual-native parity assertions stay intact.
Fresh detached runs pass **770 checks on each bottle**, including the deliberate
outgoing comparison status, real cache miss/hit replay and incoming-status key
partition. Steam's natural native status change is observed; X3's is absent on
the chosen mesh, while its authored comparison change is present.

### Independent cache regression acceptance

The corrected detached cache and unchanged hook regression are accepted on
both bottles: **770** detached checks and **1,714 / 2,003 / 2,011 / 2,189 /
2,673 / 2,681** hook checks (**13,271**) per bottle. Independent inspection
matched all recorded current source/native hashes, retained Steam and X3
executables and sibling DLLs, detached raw reports and parsed result groups,
and all twelve hook result/Wine-log pairs. Before/after source and binary maps
are equal. Retained binaries are under
`verification/probe/build/review31-{Steam,X3}/`; summary records are
`mesh-adjacency-cache-summary.json` and `mesh-cache-hook-summary.json` under
`verification/results/` and `verification/results/bottle-X3/`.

The genuine-comparison control supplies the positive status-replay witness on
both runtimes. It does not turn X3's undelivered precision flags into tested
coverage. These fixture-only corrections preserve the already accepted
loading/replay provenance. No additional reviewer Wine run or production change
was required; final integrated DLL checks and user game acceptance remain
separate.

## Resource reader review (2026-09-13)

**Three concrete source findings were accepted and are now fixed in source;
independent delta review accepted the source/host checks; the fresh full X3 fixture passes 4,721 checks. Independent artifact review accepted the current result.** The initial
independent review made no source edits and ran no Wine. After review 30's first
install, the root agent released the narrowly scoped reader fixes below. The
reviewer then became the fix author; platform_architecture independently reviews
that delta. No game run is claimed.

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

The required closure was the targeted fixes, independent delta review,
expanded host parser tests and one freshly built reader fixture under the
coordinated Wine lock; these are completed below. The user-managed verify run still must show zero mismatches and the
expected short-cursor class before fast mode is accepted for gameplay.

### Reader fix delta (independently accepted source, full X3 fixture passed)

After review 30 was committed and installed, all three fixes landed together:
`verify_sink` supplies the missing field; the short rewind is checked before
publishing the output/counters/cursor, with matching allocation cleanup on
failure; verify mode restores incoming LastError immediately before the original
and saves outgoing LastError before QPC. No per-draw work or additional allocation
was introduced. Only the existing short-class seek is checked; the LastError
transport adds one SetLastError on the diagnostic verify path.

The fixture adds twelve checks across loose and catalogue rewind failures and
two LastError checks in the existing deliberate-tamper call. The latter uses
distinct entry (`0x31415926`), scratch-clobber (`0x27182818`) and original-output
(`0x16180339`) values. Cursor statistics remain 40/32/60/1; the expected full run
is 4,721 checks with the current deterministic payload search. The parser also
checks both named fault-case summaries and the actual deliberate mismatch line,
including equal cursor/position pairs, boolean fields and finite timing values.

Ten host test methods pass. They cover the original acceptance cases plus
duplicate records/keys, missing or altered cursor fields, rewind/error witnesses,
shifted diagnostic fields, nonfinite timing, repeated terminals, exact fast/probe/mode records and the full 4,721-check terminal inventory. Quick mode remains explicitly structural-only for its terminal count until separately qualified; it is not the planned checkpoint run.
The standalone fixture builds with warnings as errors. The runtime runner now
records 21 source/helper/test hashes including all fourteen compiler-reported
local C++ dependencies, before/after copied zlib and executable hashes, and
stdout/stderr hashes. Older summaries remain explicitly historical and are not
accepted by the new schema-2 parser as post-fix proof.

No Wine execution was performed while the crypto or exposure agents held the
runtime slot. After their suites finished and the root agent granted the slot,
one full X3 reader run executed under `wine_lock.py`; no quick run was used.

Independent delta closure: platform_architecture accepted the three production
fixes, fourteen new fixture checks and the parser follow-up; independently reran
all ten host tests successfully. No remaining source findings or added production
allocation/locking cost. The fresh runtime artifacts below bind this source freeze; independent artifact
review accepted every recorded/current hash and the exact parsed result.

### Fresh reader runtime closure (2026-09-13)

One full X3 fixture run passed **4,721 checks, zero failures**, followed by all
ten host tests against the new report. `game_guard` returned `[]` before/after,
and no other fixture/runner process was present. The root-granted Wine slot was
released for the binary agent's loading/replay queue immediately after terminal
completion. No game was launched, no main DLL was built, and no install occurred.

Both authored rewind failures release the output, retain unchanged bookkeeping,
restore entry state and allow the original retry. The deliberate mismatch now
has `cursor=expected_cursor=19654`, `position=expected_position=20113`, valid
boolean fields and finite timings. Original incoming/outgoing LastError matches
the two authored sentinels. Cursor statistics remain 40 verify calls, 32 short
cursors, 60 cumulative equal calls and one deliberate mismatch. Warm-cache
fast/reference ratios are 1.11x for the 3 MB loose payload, 1.11x for 31 KB,
and 1.12x for the 3 MB catalogue payload; these are synthetic diagnostic timings.

All 21 source/helper/test hashes match the runner's before/after maps and the
current files. The native input zlib, copied zlib, executable and stdout/stderr
were rehashed after completion, and the strict parser reproduced the stored
report exactly. Current schema-2 artifacts replace the earlier pre-fix evidence
at the same result paths; the old hashes above describe only the reviewed
pre-fix snapshot.

- Summary `verification/results/bottle-X3/resource-reader-summary.json`:
  `43fc03ab8b57a53cb067bd249ca46b8521d57e6c593f338d2fa7a2989c602f33`.
- Stdout `resource-reader-fixture.txt`:
  `df14b2cf96bf9f6787daea9719e17fb60331e4d2ef6c975942fe23879d5a76b9`.
- Wine stderr `resource-reader-fixture-wine.log`:
  `06bb0bf4cb250155ea8c208eb08f13662e526c8f474265eac9a41e565eafaf31`.
- Fixture EXE:
  `7bf746d206535796602bdc9ac46da12813abbdc986f5b9773c83c3addc80d5d0`.
- Native and copied zlib:
  `27b95a87be89090df67f5f1e7fc88437da400b7c3b6728418d97eca71882cad5`.
- Reader/core source:
  `e3d38b15f96d1d421691192d8f57d597806bf8d3c8449feb1b122e1ffba5b86a` /
  `43eaf23f72236c569a4c5316ae06ab2d1ad186833453c43e84997882ca48437e`.

This closes the authored fixture controls; the user-managed game verify run
and native-Windows runtime behavior remain separate acceptance work.

Final independent artifact closure: platform_architecture recomputed all 21
current/before/after source hashes, native/copied zlib, current EXE and raw
stdout/stderr hashes; reparsed the report with the exact 4,721-check requirement;
and independently reran all ten host tests. No remaining reader source or
artifact findings, and no additional native rerun requested.
