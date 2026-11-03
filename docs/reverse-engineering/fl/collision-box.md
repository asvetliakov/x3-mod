# FL collision broad-phase box-cull contract

Static reconstruction and conservative numerical proof, 2026-10-29. Both FL
sites, complete local control-flow graphs, integer/flag/stack liveness and the
256-unit bound are established below. The FL profile and emitted stubs now use
[local FP admission](#local-fp-admission-decision-2026-10-29) rather than a
game-wide FP premise: a prospective integer reject requires precision masking
and an already-set precision sticky bit. Native synthetic qualification of the
original distance bytes and emitted stubs is recorded in the
[box-cull ledger](../../verification/collide-box-cull.md). Startup integration has
reviewed synthetic native fixture evidence there; actual FL flight and overall
performance benefit remain unverified.

The AP implementation is `src/engine/ap/collide_box_cull.{cpp,h}` and
`collide_box_cull_core.h`. Its accepted behavior and limitations remain in
[sector collision](../sector-collide.md#10-implemented---collide-box-cull-2026-09-18)
and the [box-cull ledger](../../verification/collide-box-cull.md). The FL contract
must not reuse AP's numerical margin, register extraction or helper checks by
substituting addresses.

## Images, complete functions and entry boundaries

All VAs use preferred image base `0x400000`. The host producers read the clean
images at `/mnt/c/X3AP-orig/X3AP.exe` and `/mnt/c/X3FL-orig/X3FL.exe`:

| Image | SHA-256 |
| --- | --- |
| AP | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| FL | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |

The [static result](../../../verification/results/fl-port/collision-box-discovery/contract.json)
pins complete function hashes, nine exact windows and the following measured
counts. No raw game bytes or decompiler listing is stored in the result.

| Contract | FL P1 all-pairs | FL P2 swept scan |
| --- | --- | --- |
| Complete code interval | `[0x466de0,0x467e9c)`, 1,177 instructions | `[0x466660,0x466b4a)`, 349 instructions |
| Entry calling convention | sector/list owner in ECX; aligned local frame; callee preserves EBX/ESI/EDI/EBP | reference object in ECX; aligned local frame; callee preserves EBX/ESI/EDI/EBP |
| Returns | plain RET at `0x4674ed/0x46760d/0x467a12/0x467e4b` | plain RET at `0x466b40/0x466b49` |
| Six-byte hook | `0x46723f`, two complete position-pointer loads into ECX from EDI and EDX from EBX | `0x466859`, two complete position-pointer loads into EAX from EBX and EDX from EDI |
| Displaced continuation | `0x467245` | `0x46685f` |
| Direct inbound edges to site | `0x4671bb`, plus fall-through from `0x467239` | `0x46683b/0x46684d`, plus fall-through from `0x466853` |
| Native comparison/reject | signed CMP `0x4672a7`, JG `0x4672a9 → 0x4673ae` | signed CMP `0x4668c3`, JG `0x4668c5 → 0x466a1b` |
| Early-out continuation | `0x467e7e` | `0x466a1b` |

P1 has three computed jump tables: `0x466ee2 → 0x467e9c` (2 entries),
`0x466f5f → 0x467ec0` (4), `0x466fa7 → 0x467eec` (2). Their eight targets
are recorded and checked; each table DWORD has a PE HIGHLOW relocation.
The complete reachable local CFG includes all targets and early returns.
The returns before the final epilogue belong to post-callback object-liveness
checks, rather than extra callable entry points. P1/P2 have no decoded branch
into displaced bytes `+1..+5`; whole-image, every-byte rel32 and abs32 superset
scans also find none. This establishes the clean-image entry contract; arbitrary
computed third-party entry remains outside it.

## Arithmetic, fields and class 7

Both sites read position blocks through object `+0x70`, then signed integer
coordinates `+0x30/+0x34/+0x38`. Each subtraction wraps in 32 bits before any
conversion. FL converts the three differences with `CVTDQ2PS`, squares them with
three `MULSS`, and adds with two `ADDSS` in x/y/z order. The lanes not holding
the scalar are zeroed by their preceding MOVD. The calls at P1 `0x467280` and
P2 `0x4668b2` target helper `0x412a80..0x412a8e`: aligned EBP/ESP prologue,
`SQRTSS XMM0,XMM0`, matching epilogue, plain RET. It leaves GPR inputs intact,
returns the scalar in XMM0 and touches no x87 state. `CVTTSS2SI` produces the
signed truncated distance independently of MXCSR rounding control. There is no
FL `_ftol2` call or AP x87 helper in this path.

P1 loads radius A from `[EDI+0xa4]` at `0x46703d`, adds `[EBX+0xa4]` with
32-bit wrapping at `0x467043`, and stores the wrapped sum `r` in `[ESP+0x34]`
at `0x46704d`. Nothing rewrites that slot before this distance test. The native
threshold is the low signed DWORD of `(int64(r)*0x1028f+0x8000)>>16`, from
`0x467285..0x4672a4`. Native distance goes to `[ESP+0x14]` at `0x4672a0`.

Class 7 must be exempted **on either object**. The class words are `+0x48`.
At `0x4671b1/0x4671b7`, a class-7 pair first gets the native loose per-axis box
`3*r + 250000` with native 32-bit wrapping, then can fall through to this same distance site. After a
native distance rejection, `0x4673ae..0x4673c3` tests both classes again;
only when neither is 7 does `0x4673bd` branch directly to `0x467e7e`. Class-7
pairs enter `0x4673c3`, and the stored distance is read, for example, by the
proximity call `0x46740d → 0x456de0` (argument loaded at `0x467406`) and the
alternate class-7 path at `0x467529`. Thus a class-7 early-out would suppress
observable behavior even if the distance comparison rejected. It is not safe
to infer exemption solely from one incoming branch or from the earlier box.

P2 compares against candidate radius `[EBX+0xa4]` plus sweep half-length
`[ESP+0x2c]`, added at `0x4668b7`. The reference is EDI. The sweep local is
stored at `0x4666e2`; P2 has already temporarily moved the reference position
and later restores it, so the hook must use the current native position fields
and must not reconstruct a prior reference position. P2 has no class-7 exemption
in this comparison; a class-7 candidate is governed by the same bound.

## Keep/reject state and lifetime

The hook is an interior JMP with the site's exact ESP and no new return
address. Function return-pop metadata for FL is zero, whereas AP P1's enclosing
function uses RET 4. A future stub can use only integer instructions and balanced
push/pop operations, with no allocation, locks, helper calls or retained object
pointers. Read class/radius/position only while the original pair is live; the
stub may not persist those pointers across a callback or Present.

| State | P1 | P2 |
| --- | --- | --- |
| Registers available before original site | EAX/ECX/EDX are written before read; preserve object EDI/EBX, frame EBP/ESP; preserve incoming ESI on the keep path | EAX/ECX/EDX/ESI are written before read; preserve EDI/EBX/EBP/ESP |
| Reject continuation | reloads EBX from `[ESP+0x44]`, EAX from EBX, CMP at `0x467e84`, then loops to `0x466fd1` or outer-loop `0x467d4b` | advances EBX through `[EBX]`, CMP at `0x466a1d`, then loops to `0x4667f0` or outer-loop handling at `0x466a26` |
| Required reject mirror | none if ESI is preserved; native does not assign AP's EDI=7 | **ESI must be restored from `[ESP+0x14]`**, matching `0x4668bb`; otherwise `0x466a49` reads the last dy as candidate count |
| Incoming EFLAGS | dead before SUB `0x467248`; reject-side first writer CMP `0x467e84` | dead before SUB `0x466869`; reject-side first writer CMP `0x466a1d` |
| Omitted stack writes | distance `[ESP+0x14]`; first writes before reuse are `0x4672a0/0x46762c/0x467ac5` | reference physics `[ESP+0x3c]`, dy `+0x20`, dz `+0x50`, dx `+0x40`; next candidate overwrites at `0x46685f/0x466875/0x466886/0x46688a` |
| x87 | native path and proposed stub both leave x87 data stack untouched | same |

The local-slot check follows all three jump tables and tracks ESP changes.
The apparent later read `[ESP+0x20]` at P2 `0x466b27` occurs after PUSH EBX:
it reads original local `+0x1c`, not omitted dy. Candidate array accesses start
at `+0xa0`, with count/index bounded to 0..32. Calls beyond a skipped visit
receive object/physics pointers, body IDs, array pointers or integer cells, not
addresses of these omitted scratch slots. Native distance rejection writes no
object, collision list, contact counter or callback argument on either site.
Class-7 P1 paths are retained; P2 survivor array insertion is retained.

XMM0/1/2 are completely defined before the original kept computation reads
them. At the reject continuation there is no caller-side read of their incoming
values before a definition or one of these explicit call boundaries:

- P1 `0x4676d5 → 0x412c60`: integer fixed-point vector helper, no XMM reads;
  P1 `0x467e22 → 0x4bb500`: established cdecl integer-cell named dispatch,
  six caller-cleaned DWORDs ([SETA wrapper contract](seta.md#named-calls-and-results)).
- P2 `0x466a5e → 0x52b510`: CRT qsort with stack array `+0xa0`, count ESI,
  stride 8 and comparator `0x466650`; comparator reads two pointers from the
  stack and subtracts integer `+4` fields. Its pointer/int callback contract
  does not make incoming XMM scratch into an argument.
- P2 `0x466a87 → 0x4a61a0`: private collision helper. Actual definitions are
  ECX=`[EDI+0x70]` at `0x466a77`, EDX=`[candidate+0x70]` at `0x466a80`, and
  stack pushes zero / original sweep `+0x2c` at `0x466a7a/0x466a7c`.
  Body `0x4a61a0..0x4a6264` contains no XMM use. Its ID lookup receives ECX
  from physics `+0x144`; recursive collision `0x4a5ef0..0x4a6199` takes
  physics/model pointers and stack integers. The first local XMM definitions
  are memory/constant loads at `0x4a5fc4` (XMM0), `0x4a5fb0` (XMM1),
  `0x4a5fb8` (XMM2), before their arithmetic reads. The preceding extent
  helper `0x4a39b0..0x4a3ad3` is integer-only; the
  [body-ID/resource input](resource-callers.md) is established.
- P2 `0x466b33 → 0x466b50`: integer fixed-point helper, no incoming XMM use.

This is a call-input contract, not equality of volatile XMM registers at every
later callback. It reuses named-dispatch and CRT interfaces and the targeted
private pointer/ID evidence above; it does not claim a recursive proof of every
indirect script/CRT target. No concrete caller-side XMM read-before-definition
was found. Changes to those private input interfaces would invalidate this
boundary premise and require renewed analysis.

The skipped native path calls only the non-reentrant sqrt helper before
rejection. A call-free integer stub cannot invoke a game callback or reenter
itself. Later native callbacks and their liveness checks remain in the native
continuations. The proposed stub changes no LastError, TEB or error state.
Read faults or asynchronous unwind while a stub push is outstanding require a
future exact-trampoline fixture: this static proof does not establish equivalent
fault IP/stack contents, guard-page behavior or arbitrary invalid pointers.
Optional counters inherit AP's unsynchronized increment limitation: ownership
of the loop and Present must be verified before claiming lossless telemetry.

## Conservative FL bound, including directed rounding

Let `d` be each native signed 32-bit wrapped difference and
`m = max(unsigned_abs(dx),unsigned_abs(dy),unsigned_abs(dz))`. An INT_MIN
magnitude remains `0x80000000`, so an unsigned cap check rejects admission.
The stub must fall through whenever `m >= 2^30`; no world-coordinate or radius
reachability assumption is used for the proof below. Wrapped coordinates need
not describe the shortest real geometric displacement: the native path uses
exactly these wrapped differences too.

Use a **FL-only margin 256**. P1 tries a box only when neither class is 7,
`r >= 0`, and the two signed additions in
`T = r + (r >> 5) + 256` do not overflow. P2 tries only when both signed
additions in `R = candidate_radius + sweep; T = R + 256` do not overflow.
P2 may admit a negative R; its native distance remains nonnegative, so that
case is conservative too. Never widen the native wrapped radius addition and
then silently compare against a different unwrapped value on an overflow.

**Proof (derived, not sampled).** Let `u=2^-23`, a conservative relative bound
for one correctly rounded binary32 operation under any of the four MXCSR
rounding modes. For the component attaining m, conversion magnitude `a` is
at least `m−64` and at most `2^30`: below the cap the largest binary32 spacing
is 64, and directed rounding of a negative component may increase its
magnitude but cannot exceed the exactly representable cap. A nonzero integer
input, its square and every positive sum are normal; DAZ and FTZ cannot affect
these values.

The rounded square `q` obeys `q >= a²(1−u)`. Adding other nonnegative,
representable squared components cannot decrease q, under any rounding mode.
Correctly rounded sqrt is monotone, so the actual native rounded root is at
least `a*(1−u)^(3/2)`. Since `(1−u)^(3/2) >= 1−3u/2`, and truncation loses
strictly less than one,

`D > a − (3u/2)*a − 1 >= m − 64 − 192 − 1 = m − 257`.

For m=0 the conclusion is immediate. Each rounded square is at most `2^60`;
the two sums are bounded by the exactly representable `3*2^60`. The rounded
sqrt stays below `2^31`, so CVTTSS2SI cannot produce INT_MIN through invalid
conversion in the admitted domain. No NaN, infinity, underflow or float
multiplication overflow can arise there from these integer inputs.

For P2, `m > R+256` implies the integer gap `m−R >= 257`; the strict lower
bound gives `D > R`. For P1, write nonnegative `r=32q+s`, `0<=s<=31`.
The native threshold is `R=r+floor((655r+32768)/65536)`. Since
`655*(32q+31)+32768 < 65536*(q+1)`, the floor is at most q.
Thus `R <= r+(r>>5) = T−256`, and the same strict integer argument applies.
The admission additions ensure no signed threshold or scaled-result wrap in
this P1 case. Therefore both boxes reject a subset of native rejected pairs.
256 is sufficient; no minimal-margin claim is needed.

This proof needs no per-pair MXCSR rounding/DAZ/FTZ gate. It assumes normal
masked SSE exception execution. Precision status is a separate issue: skipping
inexact arithmetic leaves different MXCSR sticky flags, and unmasking precision
can make the native path trap when the integer stub would continue. The host
witness `(65537,0,0)` with radius/sweep zero sets precision status under all 16 rounding/DAZ/FTZ
combinations, whereas an integer early-out does not. No established game-wide
invariant proves these flags unobservable or precision always masked across
all callbacks. Consequently **numerical conservatism is established across
all four rounding controls; exact FP-status/trap equivalence is not**. AP's
prior fixture checked x87 depth/data, not equality of skipped x87/MXCSR status;
its accepted limitations do not prove this stronger FL premise.

## Witnesses and checks

The earlier [rounding witness](../../../verification/results/fl-port/collision-box-discovery/rounding.json)
calls the actual AP `p2_box_reject` predicate with `(1073741823,0,0)`, radius
`1073741758`, sweep zero. AP's 64-unit margin rejects; FL returns distance
`1073741696` under MXCSR downward `0x3f80` and toward-zero `0x7f80`, so the
native comparison keeps. This refutes importing AP's predicate over its full
admitted synthetic integer/control domain; it does not establish those values
occur in a shipped sector or invalidate AP's different arithmetic proof.

The [boundary result](../../../verification/results/fl-port/collision-box-discovery/boundary.json)
is measured host SSE execution of the FL sequence. It checks 22,891 distinct
axis values around powers of two, both signs, cap/INT_MIN/INT_MAX, six vector
arrangements, all four RC × DAZ × FTZ combinations: **2,197,536 vectors**, of
which **2,197,152** satisfy the cap; **21,975,360 P1** and **153,827,520 P2**
predicate checks, **36** coordinate-wrap checks, **0** subset failures. The
largest sampled m−D is **127**. Class-7 exemptions, negative thresholds,
threshold-add overflow and cap fallback are checked. The old-margin witness
fails on eight of the 16 controls; precision sticky changes on all 16. These
checks support the implementation model; the inequality above covers the
unsampled domain. They are neither engine traversal nor game reachability.

Reproduce from the checkout:

```sh
python3 verification/results/fl-port/collision-box-discovery/produce.py
python3 verification/results/fl-port/collision-box-discovery/close_contract.py
python3 verification/results/fl-port/collision-box-discovery/run_rounding.py
python3 verification/results/fl-port/collision-box-discovery/run_boundary.py
```

`close_contract.py` also checks a synthetic interior rel32 edge, mutated P1/P2
and helper windows, and omission of P2's ESI mirror: five negative checks.
Host witness executables stay under `~/.cache/x3m-port/`. No Wine, PE execution,
game launch, hook installation, candidate build or performance fixture ran.

## Minimal AP/FL binding plan and remaining qualification

Keep one shared feature lifecycle and integer-box policy with explicit edition
bindings. AP keeps its current 64-unit margin, register extraction, original
seven windows, x87 sqrt/_ftol2 checks and accepted limitations. FL supplies
margin 256, object pairs EDI/EBX and EBX/EDI, radius slots `+0x34/+0x2c`,
its two whole-MOV spans, zero return-pop metadata, sqrt-only helper check and
P2's candidate-count ESI mirror. A simple P1 encoder can preserve ESI with a
balanced save/restore instead of borrowing AP's free EDI; radius reads must
account for any outstanding pushes. Do not insert AP's EDI=7 or P2's physics
pointer restore into FL.

The exact FL window profile is below; full hashes and instruction counts are
in `contract.json`. Each interval excludes its end and has **zero** HIGHLOW
operands, so its bytes/hash are unchanged at a different loader base.

| Window | Preferred interval | Bytes |
| --- | --- | --- |
| Radius source | `0x46703d..0x467051` | 20 |
| Class-7 entry/context | `0x4671b1..0x46723f` | 142 |
| P1 distance/compare | `0x46723f..0x4672af` | 112 |
| P1 class-7 rejection context | `0x4673ae..0x4673c3` | 21 |
| P1 continuation/outer edge | `0x467e7e..0x467e92` | 20 |
| P2 inbound context | `0x466833..0x466859` | 38 |
| P2 distance/compare/ESI restore | `0x466859..0x4668cb` | 114 |
| P2 continuation/outer-loop locals | `0x466a1b..0x466a49` | 46 |
| Scalar sqrt helper | `0x412a80..0x412a8e` | 14 |

Resolve every site/helper/continuation as loaded-base + preferred RVA, not a
fixed AP/FL delta. The result explicitly derives preferred base `0x400000` and
synthetic base `0x10400000` (delta `0x10000000`): P1 `0x1046723f`, P2
`0x10466859`, sqrt `0x10412a80`. The eight jump-table pointers must receive
HIGHLOW rebases if copied into a fixture; relative calls/branches retain their
displacements when source and target move together. New stub-to-engine jumps,
absolute continuation slots and DLL enabled/counter pointers must instead be
encoded from their actual runtime addresses. These are static relocation
inputs, not verified loader execution. Whole-function hashes are research
provenance, not an additional production renderer prerequisite.

Retain the existing executable identity/capability boundary, pinned DLL and
engine-patch claim lifecycle, install-before-Present window, foreign-byte
rejection, all-or-none P1/P2 rollback and LastError preservation. The next
implementation checkpoint needs exact native bytes versus emitted stub
execution at both bases, kept/rejected/class-7/cap/overflow exits, ESI count,
GPR/ESP/flags and x87 data preservation, FP status measured separately, enabled
and disabled counter modes, foreign/helper/late-install/partial-claim negatives,
and shutdown rollback. Trampoline fault/unwind behavior and native Windows
remain unverified. This note supplies no activation authorization or code.

A pure integer, call-free stub avoids added FP-state saves, allocations and
locks. FL already computes its distance in scalar SSE with one short helper;
AP's historical x87/FEX savings cannot predict its benefit. Kept pairs add box
loads/subtractions/branches and repeat native work; the wider margin also
keeps a few more near-threshold pairs. The future fixture must measure rejected
saving, kept overhead and loading/install cost for FL on the real target.
No FPS, game benefit or native performance figure is inferred from instruction
counts or host arithmetic-witness timings.

## Independent contract review (2026-10-29)

Independent review reproduced the static contract record and retained host
boundary witness, and checked the conservative inequality, P2 ESI reload and
private-call inputs against FL disassembly. No blocking finding was reported
within this conditional contract. The witness covers 2,197,536 vectors,
21,975,360 P1 and 153,827,520 P2 comparisons with zero failures. Floating-point
sticky-status and unmasked-exception equivalence remain unproved; this review
does not accept production implementation, activation or game performance.

### FP setup follow-up (2026-10-29)

Targeted independent inspection leaves the status/exception premise open.
The [bounded setup check](../../../verification/results/fl-port/collision-box-discovery/check_fp_setup.py)
reproduces ten instruction anchors: the native setup ORs MXCSR with 0x800,
0x100, 0x8000 and optionally 0x40, preserving incoming bits. These operations
establish underflow/denormal masking and FTZ/DAZ, not precision masking
(0x1000). The status reader at 0x543be1 reads MXCSR and tests precision bit
0x20. Its existence does not establish gameplay observation of that bit.

The review found no local FP control/status envelope around the collision
callers and no concrete gameplay failure. Existing AP runtime observations
cannot establish FL's invariant. Keep this optimization conditional; neither
another whole-game audit nor a per-pair save/restore envelope is justified by
this evidence. If runtime evidence is needed, collect bounded entry/callback
control samples with the next consolidated FL diagnostic capture, without
claiming those samples prove universal status non-observation.

### Local FP admission decision (2026-10-29)

Use a local predicate on prospective box rejects instead of depending on
game-wide precision-status non-observation. After the existing integer domain
and clearance checks establish a possible rejection, read MXCSR and require
`(mxcsr & 0x1020) == 0x1020`: precision exceptions are masked and the precision
sticky bit is already set. Otherwise execute the original distance path.
Pairs kept by the integer box do not need this additional read.

Within the proved domain `m < 2^30`, integer-to-float conversion yields zero or
normal finite values of magnitude at most `2^30`. Squares are at most `2^60`,
their sum at most `3 * 2^60`, and the square root remains below `2^31`.
The final signed conversion is valid. These operations can set precision only;
they cannot introduce invalid, denormal, divide-by-zero, overflow or underflow
status. Existing other status bits remain unchanged. Requiring precision
masking as well as an already-set status bit is necessary because an unmasked
inexact operation can trap even when the sticky bit was previously set.

The proposed stub reads MXCSR into a balanced four-byte stack scratch slot and
uses an available integer scratch register. It performs no FP arithmetic or
MXCSR write and preserves XMM/x87 state. Restore the original ESP before either
continuation and retain the established P1 class-7 exclusion and P2 ESI mirror.
Recheck emitted capacity and branch reach; AP encoder sizes are not an FL bound.

This ratifies the implementation approach, not production or performance
acceptance. Native qualification must compare original and patched exits and
full MXCSR state across rounding/DAZ/FTZ combinations, both precision status
values and masked/unmasked precision, including native trap forwarding.
Measure prospective-reject savings, kept-pair overhead and fallback overhead
separately. No FP observer, whole-game audit or per-pair state-restoration
framework is required. Actual game benefit remains unmeasured.

## Port (2026-11-03): FL box-cull implemented

The binding plan above is implemented: `src/engine/fl/collide_box_cull_fl.cpp` provides
`collide_box_cull::initialize()/install_base()/shutdown()/present()/set_enabled()/take_counters()` for the FL edition,
reusing AP's integer `box_reject`, the 2^30 cap, the `Counters` and the 300-frame `Window` from
`src/proxy/collide_box_cull_core.h` unchanged; `collide_box_cull_sites_fl.h` carries the FL margin 256, the two sites,
the stub encoders and the host models. On the backend-load path it verifies the nine windows (`box` table of
`collide_windows_fl_inc.h`), pins the DLL, emits both stubs and claims P1 `base+0x06723f` (plain write, crosses a qword)
and P2 `base+0x066859` (atomic), both or none, each read back. The P1 stub preserves the live ESI across its box test
and jumps to `base+0x067e7e`; the P2 stub restores ESI = the candidate count `[ESP+0x14]` and jumps to `base+0x066a1b`.
Class 7 on either object at P1 always takes the native path. On a prospective reject both stubs read MXCSR and require
`(mxcsr & 0x1020) == 0x1020` (precision masked and already sticky) before skipping the inexact SSE distance
("Local FP admission decision"); otherwise the native path runs. One row `collide_box_cull … edition=fl … margin=256`.

Verifier `verification/probe/verify_collide_box_cull_sites_fl.py` (PASS, 18 checks): the nine windows, both sites as two
whole MOVs, the native SSE distance (3 CVTDQ2PS / 3 MULSS / 2 ADDSS / the sqrt helper / CVTTSS2SI, no x87), the native
thresholds and compares, the class-7 exemption on either object at `0x0673ae`, the sqrt helper, the interior-entry
exclusions, the reject-continuation liveness over every path (EAX/ECX/EDX/EFLAGS, XMM0-2 and the omitted stack slots
written before any read; the first calls reached exactly the contract's), the qword geometry and the AP addresses
refused. Native fixture `verification/probe/fl_collide_box_cull.cpp` (PASS, 301887 checks): it maps a relocated private
X3FL.exe, installs the real module and drives the two real sites with a full CPU image, the disarmed run of each pair
(pure native) being the ground truth. 4015 pairs per site (15 deterministic edge cases, 4000 random with independent
coordinate, radius and class draws: 955 P1 pairs carry class 7 on either object) under eight MXCSR modes: precision
masked and sticky with each rounding control (0x1fa0/3fa0/5fa0/7fa0), DAZ+FZ (0x9fe0), all at once (0xffe0), and the two
modes the FP gate refuses (0x1f80 sticky clear, 0x0fa0 precision unmasked). Measured per admitting mode: P1 3548 native
rejects, 2476 culled; P2 3541-3542 native rejects (directed rounding moves one verdict), 3518 culled; **0 over-culls**
in all 16 site/mode runs (a cull of a pair the native distance keeps), 0 class-7 culls at P1, 0 culls in the refused
modes; the hardware verdict matches the stub model in every mode and the native verdict matches the host model under
nearest rounding. So the every-rounding-mode bound is now executed on the real bytes for these pairs, not only analytic.
Plus claim/read-back/rollback, register/x87/MXCSR/LastError/XMM preservation on skip and full CPU-image parity with the
disarmed run on keep, the counters/census row, shutdown and late_claim. Timing (performance pass, diagnostic, not game
FPS): every sample carries the harness's FXSAVE/FXRSTOR and frame copies (harness alone 109-139 ns over two runs);
against the disarmed run of the same pair the stub saves 9.8-10.3 ns on a reject and adds 1.0-4.0 ns on a keep,
measured over two runs (200,000 iterations each; the spread is run-to-run noise). No FL gameplay speedup is claimed.
