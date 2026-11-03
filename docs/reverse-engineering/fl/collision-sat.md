# Farnham’s Legacy native collision SAT contract

Static investigation, 2026-10-29. Scope: AP’s OBB separating-axis test and its
FL counterpart, its direct caller, ABI, arithmetic and admission dependencies.
No production changes, build, fixture execution, Wine run or game launch.
The recommended FL route is to **retain the engine’s native scalar SSE SAT**.
It already performs the intended conversion away from AP’s x87/helper calls.
The AP replacement is neither ABI-compatible nor numerically identical to FL.
This establishes a source/binary contract, not runtime collision acceptance or a
measured FL performance improvement.

## Inputs and reproduction

Run from the repository root:

```sh
python3 verification/results/fl-port/collision-sat-contract/produce.py
```

The producer uses the project’s `compare_fl_engine.py` PE32/Capstone utilities,
`verify_collide_sites.py` AP reference and its scalar numerical twin. It reads
the two executables without modifying them, and writes only derived
[contract.json](../../../verification/results/fl-port/collision-sat-contract/contract.json).
No raw game bytes or disassembly are included in that evidence.

| Input | Bytes, measured | SHA-256 |
| --- | ---: | --- |
| `/mnt/c/X3AP-orig/X3AP.exe` | 2,153,984 | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `/mnt/c/X3FL-orig/X3FL.exe` | 2,804,736 | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |

All addresses below are preferred VAs, with image base `0x00400000`.
FL supports base relocation; translate VAs to RVAs and resolve against the
recognized loaded image if adding a future binding. The two absolute memory
operands in its SAT body have HIGHLOW relocations at `0x0051c6e1` and
`0x0051c6e9`. A raw disk-body hash cannot directly admit a rebased mapped body.

## 1. Mapping, caller and result semantics

| Role | AP | FL |
| --- | --- | --- |
| Recursive OBB descent | `0x004e2530..0x004e2780` | `0x0051a6c0..0x0051aad8` |
| SAT call / return | `0x004e25a3` / `0x004e25a8` | `0x0051a744` / `0x0051a749` |
| SAT body, exclusive end | `0x004e3280..0x004e38ae` | `0x0051c6d0..0x0051cd4b` |
| Body bytes, measured | 1,582 | 1,659 |
| Body SHA-256 | `5167e784d126610a63b7562889e7402670cfaae6aa63900bcc2ad0b486ca56e6` | `7f685e99cad483ba86780d867269aa2be9bc5001eeca33d3428afe4084a12897` |
| Body FNV-1a64 | `ad8a2cb66c0bf30c` | `120a9bf92c38dfef` |

The mapping follows the nine `abs(R)+1e-6f` computations, fifteen SAT axes and
axis-number returns, then the caller’s scaled box dimensions and recursive
child descent. It is not an address delta or an identical-byte match: FL was
compiled to scalar SSE with a different register convention and association.

A conservative bytewise rel32 scan of `.text` finds exactly one encoded
direct call to FL SAT, at `0x0051a744`; a bytewise abs32 scan of every
file-backed section finds no absolute pointer to its entry. The same scanner finds AP’s named sole caller. No encoded rel32 or
absolute pointer targets FL call-operand bytes `0x0051a745..0x0051a748`; the
decoded descent also has no direct short branch into those bytes. Rel32 scanning is limited to `.text`; absolute-pointer scanning covers all
file-backed sections. These scans do not prove the absence of an arbitrary
computed indirect target. For the
recommended native route no hook or indirect-entry exclusion is required.

At FL descent entry ECX is node A and EDX node B. Four-byte stack arguments
are R, T and scale at entry `ESP+4`, `+8`, `+0xc`. The head checks the stop/contact
count at `0x0061b980`/`0x005aa0a0`, and the optional count limit selected by mask
`0x04` at `0x005a9044` against `0x005aa0e0`. These can return zero before SAT.
At `0x0051a6f8..0x0051a73e`, the head computes scaled B extents from node B
`+0x30/+0x34/+0x38`, points A extents at node A `+0x30`, loads R/T and
increments the visit counter `0x005a9048` once. The counter is outside SAT.

FL return `0x0051a749` drops eight stack bytes; `0x0051a74c` tests EAX.
Any nonzero result branches at `0x0051a74e` to `0x0051a6dd`, returning zero
from the descent: the node pair was rejected. Zero means no separating axis;
the descent inspects child links `+0x3c/+0x40`, reaches the triangle leaf at
`0x0051a76a -> 0x0051a260`, or descends recursively at `0x0051a866`, `0x0051a9f5` and
`0x0051aac8`. The caller consumes only zero/nonzero, never the axis number.
It uses the native SAT verdict to decide whether triangle work is reachable.

## 2. ABI, layout, lifetime and hook boundaries

At FL SAT entry:

| Item | Established contract |
| --- | --- |
| ECX | R: nine row-major float32 entries, 36 readable bytes |
| EDX | T: three float32 entries, 12 readable bytes |
| `[entry ESP+4]` | A half-extents, three float32 entries |
| `[entry ESP+8]` | Already scaled B half-extents, three float32 entries |
| Return | EAX = 0 for overlap; 1..15 for first separating axis |
| Stack cleanup | Plain `ret`; caller drops 8; incoming alignment only four bytes required |
| Preserved GPRs | EBX, EBP, ESI, EDI; ECX remains R |
| Clobbers | EAX, EDX, arithmetic flags; XMM0..XMM7 |
| Floating state | No x87/MMX instruction; no MXCSR/control-word load/store; SSE can update exception status |
| Writes | Local stack only, including spills/saved registers; no input or global writes |

FL’s prologue at `0x0051c6d0` saves EBP, aligns ESP down to eight bytes,
reserves `0x28`, saves ESI/EDI, copies T to ESI and takes A/B from EBP-relative
argument slots. Fourteen early returns name axes 1..14; the last compare
constructs 0 or 15 and shares its final return. There are **15** plain RETs,
not AP’s 16. The epilogues restore ESP through EBP. LastError is untouched. No callee, API, FS-based
SEH setup, allocation, lock, callback or global scratch state occurs in this
leaf. Readable inputs remain borrowed only for this synchronous invocation;
no pointer or result is retained. The SAT leaf is reentrant. The containing
descent also updates shared engine counters, so this does not establish
thread safety of the complete collision subsystem.

The FL caller and SAT use SSE and no x87 instructions. Native SAT leaves any
incoming x87 stack untouched; the replacement must do likewise if ever needed.
EFLAGS are dead after the call because the caller’s stack adjustment and TEST
rewrite them. The engine already permits all eight XMM registers to be
clobbered. ECX is preserved by the original even though the subsequent descent
reloads its working state. EDX is a genuine FL scratch register, unlike AP’s
preserved EDX.

SSE exception behavior is part of this contract: FL uses COMISS, including its
invalid-operation behavior for NaNs if exceptions are unmasked. Unordered sets
CF and clears the `setae` keep flag, so masked-NaN input rejects the pair.
Denormal handling, rounding direction, DAZ/FZ and exception masks follow the
incoming MXCSR. Normal engine-state assumptions are separate from this static
observation. Invalid memory or unmasked FP faults propagate naturally; no local
SEH cleanup or borrowed-resource release exists. The retained native route
preserves these paths without adding an exception boundary.

AP instead takes R in ESI, B extents in EDI, T/A extents on stack. Its thunk
therefore cannot redirect FL’s call by changing addresses alone. AP also
normalizes nondefault MXCSR around its double-precision core; imposing that
policy on FL would change the engine’s native floating-state contract.

## 3. Fifteen axes and exact numerical association

Define `R0..R8` row-major; `a0..a2`, `b0..b2`, `T0..T2` float32 inputs and
`Fi = round32(abs(Ri) + eps)`, where `eps` is float32 `1e-6`, bits `0x358637bd`,
at FL `0x00591988` (AP `0x00565600`). FL loads its four-lane absolute mask
`0x7fffffff` at `0x00591f70`. Absolute value is a sign-bit clear.

Every `*`, `+` and `-` in the table below is a **separate scalar SSE float32
operation with rounding immediately afterward**. Parentheses specify the
actual association, not an algebraic simplification. Under default MXCSR this
is round-to-nearest-even; the native code follows other incoming MXCSR modes.
At each row FL keeps the pair iff `radius >= distance` is ordered; otherwise
it returns that row’s index. Equality keeps. There is no extra AP-core slack.

| Index / axis | Compare VA | Radius | Distance |
| --- | --- | --- | --- |
| 1 / A0 | `0x51c7bd` | `((((F0*b0)+a0)+(F1*b1))+(F2*b2))` | `abs(T0)` |
| 2 / B0 | `0x51c829` | `((((F0*a0)+b0)+(F3*a1))+(F6*a2))` | `abs((((T1*R3)+(T0*R0))+(T2*R6)))` |
| 3 / A1 | `0x51c879` | `((((F3*b0)+a1)+(F4*b1))+(F5*b2))` | `abs(T1)` |
| 4 / A2 | `0x51c8c5` | `((((F6*b0)+a2)+(F7*b1))+(F8*b2))` | `abs(T2)` |
| 5 / B1 | `0x51c933` | `((((F1*a0)+b1)+(F4*a1))+(F7*a2))` | `abs((((T1*R4)+(T0*R1))+(T2*R7)))` |
| 6 / B2 | `0x51c99a` | `((((F2*a0)+b2)+(F5*a1))+(F8*a2))` | `abs((((T1*R5)+(T0*R2))+(T2*R8)))` |
| 7 / A0xB0 | `0x51ca03` | `((((F3*a2)+(F6*a1))+(F2*b1))+(F1*b2))` | `abs(((T2*R3)-(T1*R6)))` |
| 8 / A0xB1 | `0x51ca64` | `((((F4*a2)+(F7*a1))+(F2*b0))+(F0*b2))` | `abs(((T2*R4)-(T1*R7)))` |
| 9 / A0xB2 | `0x51cacc` | `((((F5*a2)+(F8*a1))+(F1*b0))+(F0*b1))` | `abs(((T2*R5)-(T1*R8)))` |
| 10 / A1xB0 | `0x51cb36` | `((((F0*a2)+(F6*a0))+(F5*b1))+(F4*b2))` | `abs(((T0*R6)-(T2*R0)))` |
| 11 / A1xB1 | `0x51cba0` | `((((F1*a2)+(F7*a0))+(F5*b0))+(F3*b2))` | `abs(((T0*R7)-(T2*R1)))` |
| 12 / A1xB2 | `0x51cc0b` | `((((F2*a2)+(F8*a0))+(F4*b0))+(F3*b1))` | `abs(((T0*R8)-(T2*R2)))` |
| 13 / A2xB0 | `0x51cc73` | `((((F0*a1)+(F3*a0))+(F8*b1))+(F7*b2))` | `abs(((T1*R0)-(T0*R3)))` |
| 14 / A2xB1 | `0x51ccdb` | `((((F1*a1)+(F4*a0))+(F8*b0))+(F6*b2))` | `abs(((T1*R1)-(T0*R4)))` |
| 15 / A2xB2 | `0x51cd35` | `((((F2*a1)+(F5*a0))+(F7*b0))+(F6*b1))` | `abs(((T1*R2)-(T0*R5)))` |

AP’s original uses x87 intermediates, float32 stores for Fi and the projected
distance before its `fabs(float)` calls. Its association differs too: axis 1
adds B2, B1, B0 then A0, while FL adds B0, A0, B1 then B2. The AP SSE2 core
reproduces AP’s association in double and adds a radius multiplier
`1 + 2^-20`. Both differences preclude strict FL verdict/axis identity.

Two deterministic numerical negative controls are in `contract.json`:

- A valid identity-frame degenerate box case, A=(1,0,0), B=(0,0,0),
  T=(1+2^-21,0,0): FL returns axis 1 while the AP core returns 0 because of
  its conservative margin. This adds FL descent work; exact verdict equivalence
  is false even with otherwise matching arithmetic.
- A synthetic finite matrix case at FL’s rounded axis-1 boundary: native
  float32 radius is `1.169252872467041`; AP’s associated double radius is
  `1.1692528365166295`. FL passes axis 1 and later returns 2, while the AP
  engine would return 1. This isolates operation precision/order without the
  AP core’s slack. It is an arithmetic control, not a captured engine pair.

These differences prove a SAT verdict/control-flow change, not a changed
final triangle-contact answer; no such contact change is claimed. These are
evaluations of instruction-derived trees, not execution of the FL machine code. The producer follows the all-overlap control path, models every
SSE arithmetic operation, rejects unmodelled operations and emits the exact
fifteen trees. A future FL replacement would still need runtime differential
acceptance across tangent, hostile and real node pairs, all relevant floating
modes, register/stack state and faults. The AP fixture’s measured acceptance
in [sector-collide.md §12.8](../sector-collide.md)
does not establish those FL results.

## 4. Minimal edition binding and performance implications

The existing feature’s purpose is faster collision SAT through modern processor
instructions. FL already provides scalar SSE SAT natively. Therefore the minimal
FL route needs **no SAT hook, thunk, callee copy, matrix translation, object
layout accessor or per-query edition test**. If the feature is exposed in FL,
select and report an engine-native implementation once at the cold recognized
FL boundary. Recognition should use the existing `engine_profile` FL build/base
contract, not broaden `object_trace::executable_verified()` (an AP-specific
fixed-base admission). The setting cannot disable the engine’s built-in SSE;
any UI/log semantics should make that explicit. This recommendation does not
implement that reporting route or change the acceptance state.

Current AP installation depends on its fixed call/target VAs, 31 pre-call bytes,
7 post-call bytes, full callee FNV pin, AP executable admission, DLL pinning,
`engine_patch` open window and the claim registry. Changing just `Addresses`
is insufficient: initialization still verifies AP’s body and windows, and the
thunk reads AP’s register/stack contract. On FL the AP gate currently refuses
installation. Existing startup invokes this initializer after narrow census;
shutdown restores only an actually installed claim. Retaining native FL requires
none of the AP mutation/rollback machinery on the FL hot path.

Measured static counts (not timings): FL body has 465 decoded instructions,
including mutually exclusive epilogues. Its full-overlap route executes 381;
axis-1 rejection executes 70. It has 81 MULSS, 60 ADDSS (nine epsilon additions
and 51 radius/projection additions), nine SUBSS, 24 ANDPS and 15 COMISS, with
14 early-out branches. AP’s 556 decoded instructions include 24 calls to the
float `fabs` helper, 81 x87 multiplies and 15 x87 compares. FL’s native body
eliminates those helper calls and all x87 work already. The AP core still has
81 mathematical products but uses double and extra slack multiplications;
static counts do not establish a further FL benefit.

A possible residual optimization is computing Fi lazily (AP core uses this to
avoid six epsilon additions on axis-1 exits); FL eagerly computes all nine.
There are also boolean accumulation/spill instructions that might be simplified.
Their actual benefit depends on FL’s axis distribution, cache/compiler/runtime
and first-call cost. None is necessary for native SSE support; neither justifies
a behavior-changing replacement without measured benefit and exact FL tests.
No FL FPS, latency or speedup claim is made.

Dependency boundary: AP no-contact memo explicitly pins SAT plus descent and
masks its redirected call’s rel32 hole (`collide_memo_core.h`). A future FL memo
port must admit the native FL SAT/descent and its relocation-normalized code,
not inherit AP’s hash or expect an AP replacement claim. Box cull and census
remain separate engine dependencies; nothing in this finding ports or admits
them. Keeping native SAT introduces no new overlap with their future claims.

## Validation and limits

The evidence producer passes with 15 derived axes and seven negative controls:
wrong AP addresses, wrong AP argument window, changed callee body, an injected
second direct caller, an injected branch into the call operand, AP slack and
float32 precision/order. The mutations are in-memory copies only. It verifies
the two constant operands’ relocation entries, full instruction boundaries,
sole encoded direct caller and absence of encoded references into the call.
Two focused existing AP host checks passed:

```sh
PYTHONPATH=verification/probe python3 -m unittest \
  verification.analysis.test_collide_sat_sse2.SatSite.test_source_constants_and_parsers \
  verification.analysis.test_collide_sat_sse2.SatSite.test_twin_semantics
```

Those checks validate the reused AP oracle/parser semantics. Existing broader
AP tests include byte-site refusals, competing claims/coexistence, compiled
portable-core agreement, production wiring and launcher dry-run behavior;
`collide_sat_sse2_fixture.cpp` separately exercises actual AP bytes under its
own runner. None was repurposed as FL runtime proof. FL runtime/native Windows
and CrossOver acceptance, actual loaded MXCSR and timing remain open.

## Port decision (2026-10-29)

Root independently reviewed the producer and key native instruction paths and
reproduced the 15-axis/seven-control result. Retaining FL's native SSE SAT is
ratified: do not install the AP SAT replacement on FL. This preserves FL's
existing numerical and exception behavior with no added per-query work. Cold
feature reporting is implemented below; memo and box cull remain separate port
work, and runtime collision acceptance is still open.


## Cold native reporting (2026-10-29, reimplemented 2026-11-03)

The 2026-10-29 reporting change was written against the Codex-era `engine_profile` layer (`src/engine/ap/`,
`engine_profile::recognized_image()`, state `engine_native`), which is archived on `fl-codex` and is not in the current
tree. The current implementation is the one in "Port (2026-11-03)" below: the FL edition is selected at compile time
(`X3M_EDITION_FL`, the `fl` CMake target links `src/engine/fl/collide_sat_sse2_fl.cpp` in place of the AP module), the
image is admitted through `x3m::fl::module_base()` (`executable_identity_fl.h`), and the module confirms the two SAT
windows at base + RVA before it reports. It reports once per load, including when `X3M_COLLIDE_SAT_SSE2` is absent or
`0`, with `requested=0|1`, `patched=0`, `implementation=engine_native_sse` and `setting_controls_native=0`; `state()` is
`engine_already_sse` when the bytes match, else the refusing window's reason (`sat_body_mismatch`,
`sat_call_mismatch`) or `executable_mismatch`. `initialize()` returns true only for native SSE; `shutdown()` has nothing
to restore. LastError is preserved. No SAT byte is written, nothing is claimed, no DLL pin and no per-query work; the
AP route keeps its own setting gate, admission, patch and restore.

## Port (2026-11-03): native-SSE decision applied

The ratified decision above is now implemented: `src/engine/fl/collide_sat_sse2_fl.cpp` provides
`collide_sat_sse2::initialize()/shutdown()/state()` for the FL edition (CMakeLists `fl` target; the AP
`src/proxy/collide_sat_sse2.cpp` is AP-only). On the backend-load path it reads `X3M_COLLIDE_SAT_SSE2`, confirms once that
the native SAT body `base+0x11c6d0` (0x67b bytes, its two HIGHLOW operands at +0x11/+0x19 bound to base) and its sole
call window are the measured bytes, and logs one row
`collide_sat_sse2 requested=0|1 patched=0 edition=fl status=native_sse reason=engine_already_sse implementation=engine_native_sse setting_controls_native=0`
whatever the setting says; a byte mismatch logs `status=refused reason=<window>_mismatch`. Nothing is patched, no claim,
no DLL pin, no per-query work. `collide_sat_sse2_sites_fl.h` carries the RVAs; the windows are in the `sat` table of
`collide_windows_fl_inc.h` (generated by `verification/results/fl-collision/generate_windows.py`).

The verifier `verification/probe/verify_collide_sat_fl.py` (PASS on X3FL.exe SHA-256 e7f3aad8…, 7 checks) re-establishes
the body is scalar SSE only: 465 instructions, 81 MULSS / 60 ADDSS / 9 SUBSS / 24 ANDPS / 15 COMISS / 15 plain RETs, no
x87/MMX, no LDMXCSR/STMXCSR/FXRSTOR/FLDCW, the `and esp,-8` frame, ECX/EDX as R/T; the sole encoded direct caller at
`base+0x11a744` with `add esp,8; test eax,eax` after it; and that the AP SAT/call RVAs are refused (also on the AP exe).
The ini text for `collide_sat_sse2` now says the setting changes nothing on Farnham's Legacy
(`tools/config/schema.py`). The native fixture `verification/probe/fl_collide_sat.cpp` (host test
`verification/analysis/test_fl_collide_sat.py`) executes the production initialize() against a relocated private
X3FL.exe: the `status=native_sse` row for a set, unset and off setting, one row per load, the whole image byte-identical
afterwards and no arena use (nothing claimed), LastError kept, and the refusals (`sat_body_mismatch` for a body byte and
for a shifted relocated operand, `sat_call_mismatch`, `executable_mismatch`). This is a source/binary contract and cold
reporting; no FL runtime speedup is claimed.
