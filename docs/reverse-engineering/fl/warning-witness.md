# FL original close-sphere warning witness

2026-10-30. Bounded static fixture recipe extending
[gate motion, section 6](gate-motion.md#6-native-warning-forecast-and-revised-ordinary-e-policy-scope).
This records a controlled execution closure, not measured live object radii or
native execution results. No production change, build, Wine or game launch.

## Original code and admitted closure

Original `/mnt/c/X3FL-orig/X3FL.exe`, preferred image base `400000`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
The project PE mapper and Capstone completely decode these exclusive spans:

| Function/range | Span | Bytes |
| --- | --- | ---: |
| Complete E | `457600..457ccb` | 1739 |
| E close prefix, through its nonattacking true return | `457600..457772` | 370 |
| E shared false epilogue | `457cc1..457ccb` | 10 |
| Complete acceleration getter | `45a610..45a67c` | 108 |
| Complete capital predicate | `4496e0..449713` | 51 |

A minimal fixture can copy the E prefix and false epilogue plus the two complete
getters to their original VAs in its controlled image. Fill unavailable code
with traps; copying complete E is optional and does **not** supply its remaining
callee closure. `45776f` is a three-byte `ret 12`, so the prefix ends at `457772`,
not `457770`. No relocation is required at the preferred base. The source PE
bytes remain local and untracked; the fixture must extract them locally.

For the admitted inputs below, the only direct calls executed are
`457621 -> 45a610(A)` and `457647 -> 4496e0(B)`. Class-18 B causes the latter to
branch at `4496e5` to `449710` and return **AL=0**, without type-table or jump-table
access. This return is an AL boolean; upper EAX is not defined by its false path.
The radius test at `4576f7` goes directly to `457716`, skipping `45a450(B)`.
Nonattacking state goes from `45771c` to `45773f`, then the non-mesh close sphere
returns full EAX=1 at `457765`. No cone, normalization, square root, recursive
part-box test, import, virtual call, allocation, lock or callback is executed.

## Controlled storage and inputs

Provide independent object, flight and type storage; do not alias flight fields
onto a TShips record. Fields read along this path are:

| Storage | Field | Required controlled value |
| --- | --- | --- |
| A | signed dword `+10`, `+14` | Current speed positive; desired speed may be zero |
| A | signed word `+48`, `+4a` | Class 7; type index 0 |
| A | pointer `+50`, signed dword `+a4` | Independent flight storage; positive radius rA |
| A flight | signed word `+258`, signed dword `+274` | Order 0, 4 or `1f`; acceleration factor 0 |
| Absolute slot | pointer at `5a83e4` | Independent TShips base |
| TShips record 0 | signed dword `+48` | Positive acceleration, e.g. 1 |
| B | word `+48`, signed dword `+a4` | Class 18; radius rB=1000000 |

The getter's type stride is `db8`; the zero flight `+274` factor returns the
record's acceleration directly at `45a679`. Nonzero factor is unnecessary for
this witness. No B type, transform, heading, speed, slip or target field is read;
no A physical pose, heading, slip or target is read. The output argument is
forwarded on the stack but never dereferenced on these returns.

Require positive radii and nonwrapping sums/products. The branch conditions are
`rB >= trunc(3*rA/4)` and
`d <= floor(((rA+rB)*0x13333+32768)/65536)`; d is the explicitly supplied integer
centre distance. These cases are synthetic modeled values, not stock/live radius
measurements:

| rA | rB | d | Relative threshold | Rounded close threshold |
| ---: | ---: | ---: | ---: | ---: |
| 100000 | 1000000 | 141421 | 75000 | 1319997 |
| 1183395 | 1000000 | 1662533 | 887546 | 2620067 |

If geometry scenarios change, recheck both inequalities before native entry.
A radius or distance negative control can leave this closure and must not be
executed unless additional code/callees are supplied. Safe negative controls:
(1) A current and desired speed both zero returns EAX=0 before either callee;
(2) nonzero speed, type acceleration zero and flight factor zero returns EAX=0
immediately after `45a610`. Both use `457cc1..457ccb`, pop 12 argument bytes,
and leave the output sentinel untouched.

## ABI, lifetime and limits

At original entry ECX=A; stack arguments at entry ESP+4/+8/+12 are B, d and
output pointer. Push output, d, B, then call E. E returns an EAX boolean and
pops 12 bytes. It saves/restores ESI, EDI and EBP; EBX is untouched. ECX, EDX and
arithmetic flags are volatile. No incoming flag or XMM value supplies a predicate
input on this path. The prologue aligns its local stack to 16 bytes, accepting
the game's four-byte incoming stack contract. The exercised instructions do not
use XMM or x87. This describes the original callable function, not an independent
proof of a replacement hook's helper-preservation obligations.

All referenced storage must remain valid for the entire synchronous call. These
paths contain no mutable engine writes, registration, retained pointers or
reentry/callback edge. The absolute type-table slot is shared mutable fixture
state: serialize tests and restore it and any modified record/control field.
There is no local SEH handler or recovery path in this closure. Invalid pointers
can fault; native behavior is not a validation mechanism. No OS/API call is
executed, so no LastError-changing call is present in the inspected path; this
is static evidence, not a runtime preservation measurement.

The complete function has further callees (`45a450`, `432b30`, `449690`,
`4572c0`, `412a80`) and cone constants. They are deliberately outside this
admitted execution closure. Its getter also has other-class paths and its capital
predicate has ship-only jump tables. The recipe grants no authority to execute
those paths, arbitrary object classes, attacking orders or a complete engine
collision pass. It verifies the original predicate under modeled state; flight
samples and native Windows behavior remain separate evidence.

## Reproduction

Run `python3 verification/results/fl-warning-witness/verify.py` and
`python3 -m py_compile verification/results/fl-warning-witness/verify.py`.
The [producer](../../../verification/results/fl-warning-witness/verify.py) checks
47 semantic anchors and 5 completely decoded ranges, records range hashes, and
checks 2 inferred arithmetic cases in its
[receipt](../../../verification/results/fl-warning-witness/evidence.json).
It reads but never loads or executes the PE. Counts are measured static results;
branch outcomes are inferred from the checked instructions and controlled inputs.
