# FL lane defer/replan timing

2026-10-30. Bounded static closure for `ship_steering_clamp.cpp::lane_clamp`
and `ship_gate_lane_core.h::lane_defer_step`. This establishes the FL native
timing prerequisite for the existing lane A/B writes; it does **not** admit
the lane controller or establish its clearance geometry. The
[base steering note](ship-steering.md) owns A/B hook spans,
ABI and simulation ordering; [gate transit](gate-transit.md)
owns explicit-point geometry and KC grid evidence. AP correspondence reuses
[navigation AI §10.13](../ship-navigation-ai.md#1013-the-re-plan-stamp-f0x210-and-the-lane-escorts-tick-deferral).
No production edit, build, native/Wine execution, launch or install ran.

The [producer](../../../verification/results/fl-lane-timing/verify.py) and
[compact receipt](../../../verification/results/fl-lane-timing/evidence.json)
check **64 operand anchors, 8 order-table entries, 5 SA-table entries and
8 rejected mutations**. The two gap-free entry decodes contain **16,764
instructions**. These are measured static counts, not runtime observations.
Run `python3 verification/results/fl-lane-timing/verify.py`.
Input is original `/mnt/c/X3FL-orig/X3FL.exe`, 2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`;
the hash is provenance. Raw listings stay local and untracked at
`~/.cache/x3m-port/fl-lane-deferral-local/`. All addresses below are preferred
VAs; eventual bindings use admitted loaded base plus RVA.

## Native timing contract

FL dispatch reads signed order `f+0x258` at `0x45db2f`, indexes byte table
`0x462fb4` then DWORD table `0x462f5c`. The actual entries select order 0
at `0x462896` and order 4 at `0x45e70b`.

| Event | FL witness | Consequence for the lane |
| --- | --- | --- |
| Enter order 0 | `0x462896` tests ship `+0x40 & 0x80000`; `0x46289f` tests `+0x44 & 4` | Desired speed is zeroed at `0x4628a5` only on the autopilot path with the latter bit clear. That path clears target at `0x4628af`; the remainder-decay path also reaches the common tail. |
| Order-0 tail | `0x4629d2..0x462a3f`: phase cleanup, `f+0x218=0`, `f+0x28c=0`, then `f+0x210=now` | `now` is `[*0x5a83b0+0x708]`, the already bound FL game-ms clock. `0x462a45` jumps to `0x462c6e`, then B `0x462c72`, with no intervening stamp reader or call after the stamp store. Reapply deferral on every eligible order-0 B visit. |
| Order 4 before deadline | Target null check `0x45e715/717`, then clock load `0x45e719/71e`; `0x45e724` subtracts `f+0x210`; `0x45e72a` is **JS** to `0x462c6e` | The sign of wrapped 32-bit `now-stamp` skips the tick. A live-target skipped visit reaches B without a speed write, point test or A call. It is not an unsigned comparison. |
| Order 4 due | `0x45e730/736` passes `f+0x20c` to random helper `0x50b5d0`; `0x45e741/747/74c` writes `now+result` to `f+0x210` | Native cadence supersedes the deferred stamp before the point/distance work. Equality is due. No separate deferral reset runs on the skipped path. |
| First point test | Port-index test `0x45e755`; explicit-point port -1 selects direct `f+0x240` load `0x45e7bf`; distance comparison is `0x45e8c6` | The first arrival test precedes a steering-leaf A call. Rewriting the leaf point at A alone cannot protect a ship already inside its original KC grid sphere. |
| Another order ends into zero | Order-4 completion clears order at `0x45e8de`, wakes at `0x45e8ed`, jumps straight to B at `0x45e8f2`; lost escort target clears at `0x461c89`, wakes `0x461c92`, jumps `0x461c97` | Live order 0 at B does **not** prove the order-0 tail ran. The stamp can still contain an old cadence or the module's previous future value. Use current clock for both defer and restore; never add delay to the stamp read. |

## Which order issuers preserve the stamp

SA table `0x473dc4` selects explicit-point case 12 at `0x46fba4`.
Its successful publication suffix `0x46fbea..0x46fc48` is call-free and has
**zero `+0x210` memory operands**. It writes raw x/y/z into `f+0x240/244/248`
at `0x46fbf3/ff/0x46fc0b`, order 4 at `0x46fc19`, target at `0x46fc26`,
port -1 at `0x46fc2f`, and wait context before jumping to dispatcher exit.
Thus the prior B deferral survives this issuer, as in AP case 12
`0x46533c`. This is the relevant inheritance, not a claim about all order-4
issuers or the entire surrounding FL transit script.

By contrast, case 10 (`0x46acdd`, generic/no-port order 4) stamps current
clock at `0x46ae83`; case 9 (order 5) at `0x46acc3`; escort order `0x14`
(SA case 20) at `0x46b99e`; follow order `0x15` (SA case 27) at
`0x46ba4a`. Their first due test does not inherit the module's future stamp.
Existing `lane_defer_step` correctly drops its saved stamp without restoring
when the field no longer equals the exact module-written value. Equality
alone is not an ownership proof after an unrelated load or identity change;
existing member identity/clock-reset invalidation remains required.

## A/B sequencing and limits

On an eligible order-0 B visit, existing shared policy writes
`f+0x210 = now + 2500 + min(previous_step_ms,5000)` (with its zero sentinel
guard). If explicit-point order 4 arrives while this is still future, the
handler skips; the same visit's B checks order, target, member identity and
KC grid geometry, rewrites only the three point DWORDs to the lane point,
then restores the stamp to **that B visit's current clock**. The next due
handler tests the new field; A can then recognize its saved lane rewrite.
The B desired-speed write is subsequently consumed by the same command
pass's slew (`0x462de0`), as established by the base note. The skipped tick
does not itself stop native motion or clear pending angular commands.

The deferral succeeds only while the first issue-frame clock difference has
the negative sign bit. At ordinary deltas this means the next step is less
than the chosen delay; equality or a larger jump can execute the original
KC point test before B. Restoring `now` makes a later nondecreasing clock
sample due, including an equal sample; no claim about exactly one rendered
frame, SETA ceiling or bounded callback clock advancement follows. The
producer's five arithmetic witnesses include equality, 32-bit wrap and a
long-step failure of the skip. They are inferred instruction arithmetic,
not execution of the native handler or the production helper.

No new instruction patch or native call is needed for this prerequisite.
Reuse the already qualified A and B envelopes, exact stack/GPR/XMM/MXCSR and
LastError preservation, startup-only install and rollback. Direct point and
stamp writes borrow only the current ship/flight on the admitted simulation
owner; retain no engine pointer across frames or native calls. A completion
wake may invoke arbitrary KC work **before** B. B must reread live order,
target and identity; an order-clear witness is not a callback-return state
guarantee. The module helper must finish before native execution resumes;
this note adds no reentrant native call or lock and does not reconstruct a
new lifetime contract.

Smallest remaining assertions for lane integration: bind these FL deadline,
clock and explicit-point publication anchors in its admission/evidence;
exercise the actual A/B implementation's defer/rewrite/restore, changed-stamp
drop and order-ended-to-zero cases under existing CPU-boundary checks; and
qualify the **FL** script/reservation path if relying on its next issuer
being explicit-point case 12. A universal step ceiling, FL save/load stamp
serialization, a broad stamp-reader census and modified-story scheduling
are unproved here. Keep the existing bounded fallback when the future stamp
does not skip; this timing result supplies no clearance or gameplay claim.


## Completion sampling, interval receipt and speed publication (2026-10-30)

**The present `raw_step_ms/1000 + 0.05` tail has no universal native timing
proof for its admitted inputs.** The observer admits neither the live interval
nor the replan stamp, its last raw step is not a future visit bound, and
completion is a sampled predicate. This is an activation/coverage gap in the
model's completion endpoint; a manually sequenced A/B/E or motion-slice fixture
without the actual due handler does not close it. The selected-zero endpoint
has an actual zero writer of its own and is a separate obligation.

This follow-up used clean `/mnt/c/X3FL-orig/X3FL.exe` (same provenance above),
read-only Ghidra `FLRegistry/X3FL.exe`, `X3GrepInsns`,
`X3DecompileFunctions`, `X3XrefsTo`, `X3ListRange`, and the project's
`compare_fl_engine::{PE32,MD,read}`. The direct-displacement instruction-text
sweep for `0x20c|0x210` returned **101 matches** (measured static count,
including unrelated fields, locals and constants). It is a bounded operand
census, not a proof against every computed alias or external writer. A focused
PE/Capstone check matched **25 operand anchors**, and seven arithmetic
examples plus **10,100** interval-threshold values passed (measured host/static
checks; no engine execution). Reproduce with
`python3 verification/results/fl-completion-delay/verify.py`; its
[producer](../../../verification/results/fl-completion-delay/verify.py) and
[compact result](../../../verification/results/fl-completion-delay/evidence.json)
retain these checks, six rejected mutations and the extraction closure below.
Raw output stays at `/tmp/fl-completion-*.local.{txt,c}`; no native/Wine/game/build
execution or production edit ran.

### Exact interval and known flight writers

Let `I = signed32(flight+0x20c)`. Helper **0x50b5d0..0x50b5fc** takes I in
ECX, returns its result in EAX, takes no stack argument and ends in plain RET.
`50b5d6/5d8` tests signed positivity: I <= 0 returns zero at `50b5da`.
For I > 0, `50b5e0` calls **0x532dcf**. That routine advances the current
CRT-thread RNG state at `TLS-record+0x18` using multiplier `0x343fd` and
increment `0x269ec3` (`532dd4..532de1`), then returns
`(state >> 16) & 0x7fff` (`532de4..532ded`). Thus its successful result r is
an integer in **0..32767 inclusive**. `50b5f5` performs the full signed 64-bit
I*r product and `50b5f7` signed division by 32768. Its exact returned interval
is `D(I,r) = floor(I*r/32768)` for I > 0; no low-32-bit multiply approximation
or 50 ms clamp occurs. The arithmetic maximum is

`Dmax(I) = 0` for I <= 0, otherwise `floor(int64(I)*32767/32768)`.

The maximum positive-I product fits signed 64-bit and the quotient fits
signed 32-bit. Inferred arithmetic examples: I=50 gives Dmax=49 ms; I=51
50 ms; I=52 51 ms; I=500 499 ms; I=INT32_MAX 2,147,418,111 ms. Therefore
signed I <= 51 is the exact scalar interval condition for Dmax <= 50 ms,
including the helper's nonpositive-input behavior. It is not an assertion
that these values are the installed type data.

| Writer / reader | Exact FL dataflow | Range consequence |
| --- | --- | --- |
| Ship initialization | `449d7d/80` sign-extends the type and multiplies by TShips stride `0xdb8`; `449d86` loads table `*5a83e4`; `449d8e` computes low32(`type[+0x50]*50`); `449d93/96` stores into `[ship+0x50]+0x20c` | No instruction clamps type data or rejects multiply overflow here. The resulting signed live I, including modified type data, is authoritative for the helper. |
| Initial stamp | `449d9c..449db0` writes `flight+0x210 = now + I` | The initial delay uses full I, rather than random Dmax; I alone does not bound an inherited stamp by Dmax. |
| Save / load | Class-7 flight serializer **0x437670**, called at `44b4e8/44b9f9`: save reads I at `437878`, stamp at `4378b6`; load stores I at `4387ae`, stamp at `4387f4` after endian conversion | Both are raw 32-bit values; this local load sequence has no interval/stamp clamp or recomputation from current type data. A stock-type census cannot admit arbitrary loaded I/stamp. |
| Ordinary due order 4 | `45e730/736` passes I to the helper; `45e741..45e74c` writes current now+D into `flight+0x210` before testing the point | Once this due branch actually ran, its new future residual is at most Dmax, subject to ordinary signed-wrap interpretation. |
| Other apparent `+0x20c` writers | `42140d` belongs to the cockpit constructor; `42ec96` uses another record reached through cockpit `+0x3c4`; `46b1aa` indexes flight with EDI=`0x34,0x38,0x3c` | The latter writes actual point fields `+0x240/+0x244/+0x248`, not the interval. These sweep hits do not provide another direct interval writer. |

The established flight interval writers are initialization and raw load within
this direct-operand scope. This does not infer runtime immutability across a
load, arbitrary native memory editing or a changed identity. The known stamp
writers in the preceding sections also remain relevant: SA case 12 inherits
its stamp, other issuers write now, order 0 refreshes now, and module deferral
writes a longer future value. Do not treat any inherited stamp as a fresh
random cadence without checking it.

### Stock FL type interval census

The read-only [producer](../../../verification/results/fl-completion-delay/stock_intervals.py)
and [receipt](../../../verification/results/fl-completion-delay/stock-intervals.json)
resolve the unmodified `/mnt/c/X3FL-orig` without selected mods and check 28 FL
loader/initializer instruction anchors and the class-7 dispatch. The loader
consumes seven common fields, then its class-7 branch at `0x441159` writes four
successive integer fields to `+0x44`, `+0x48`, `+0x4c`, and `+0x50`; therefore
the last is zero-based TShips column **10**. The initializer computes
`signed32(low32(column10 * 50))`: there is **no added 50**.

The existing FL resource resolver selects `addon2/04.cat` member
`addon2/types/TShips.pck`. Its version-50 header and parsed rows both count
**426**, and all **426** have column 10 equal to **1** (measured). Thus their
initial I is **50 ms**, and the helper's inferred random D is **0..49 ms**.
This is a stock initialization census, not a bound on a live interval or stamp:
raw save/load can replace both, and an inherited future stamp and actual visit
spacing retain the independent limits above. The receipt records input hashes;
no raw asset, native execution or game launch is involved.

### Sampled completion and the publication boundary

`45e8c6/45e8c8` compares integer point distance with rho4 and completes only
on a due visit where distance <= rho4. There is no sphere-entry latch here.
A ship may pass through the sphere between due visits, then fail the next
sample. A modeled vertex inside the sphere proves modeled entry, not native
completion or inevitable completion at the next test. A bound must retain the
same point/target/order and prove residence until the successful sample, or
exercise the actual sample with its intervening motion. A terminal geometric
stop volume does not itself supply that missing writer.

On success, **45e8cf** sets port -1; **45e8d9** zeros ECX;
**45e8de** clears the order WORD; **45e8e5** writes `ship+0x14=0` (desired
speed); and **45e8e8** writes `ship+0x10=0` (**current** speed), before
**45e8ed** calls wake **0x46a550** with reason 1. Thus the current speed does
not need to wait for a later order-0 handler or deceleration slew. These stores
do not clear the slip vector, root's previously published velocity, angular
rates/remainders, or guarantee a stationary whole hull.

Wake's existing ECX=ship, one stack argument, RET 4 contract and callback
cdecl `(context,reason)` at `46a584` remain as in
[handover sections 1–2](lane-handover-contract.md#1-the-actual-completion-issuer-and-removal-sequence).
The zero publication precedes that callback. The fixed normal KC callback
marks a task ready; the healthy next ready scheduler can issue another order
before the next collision pass. A foreign callback can replace intent before
wake returns. Accordingly the established zero writer is a publication and
handover boundary, not a lease on later speed/order. B must reread live state
at `462c72`; no borrowed ship/flight identity or old order may survive such a
native call without revalidation. The interval helper is also a native call
that clobbers caller-saved EAX/ECX/EDX and flags and mutates RNG state; a receipt
must calculate Dmax arithmetically, not call it to sample a new interval.

### Smallest useful receipt and conditional physical bound

`navigation_traffic.cpp::observe_traffic` already copies **flight[0x1e0,0x25c)**
into `Bytes<0x7c>`. I is `word(flight,0x2c)` and actual replan stamp S is
`word(flight,0x30)`. Its current `state.deadline=word(flight,0x34)` is field
**+0x214**, not +0x210. Carrying I and S as owned numeric values costs **zero
additional ReadProcessMemory calls/bytes**, no allocation, no native RNG call,
and a constant amount of integer arithmetic. Read them under the existing
owner/Copy borrow and identity/clock checks; retain no native pointer and add
no lock or reentrant callback. Existing A/B CPU, MXCSR and LastError envelopes
remain required and unchanged by numeric extraction.

For ordinary forward-clock interpretation, define R0=0 if the native
`sign32(now-S)` due test is nonnegative. Otherwise R0 is unsigned `S-now`,
and require `1 <= R0 <= INT32_MAX`; refuse the ambiguous exactly-half-range
case. Let **R=max(R0,Dmax(I))**. This accounts for a currently inherited future
stamp plus subsequent normal cadence writes while I and the same order/point
stay unchanged. To retain a 50 ms cadence term, require R <= 50; checking only I <= 51
misses an initial, loaded or module-deferred stamp. Alternatively account the
actual R in the existing physical cover, with its numeric/horizon limits;
R is a native residual, not a new timeout. Any later noncadence stamp writer,
clock reset, order/target change or identity change invalidates this conditional
argument rather than becoming a selected stop.

The exact physical step relationship is useful but conditional. Integration
samples game now at **46301c**; for each ordinary ship, **46319b..4631bf**
uses signed `now-ship[+0xa8]`, capped at **1000 ms**, with a negative value
also selecting 1000. After its ordinary integration, **464d42/464d5a** stores
that sampled now to `ship+0xa8`. Under a monotone coherent clock, an unchanged
ordinary integration path and a command visit before every such integration,
subsequent integrated steps h satisfy `h <= successive game-clock delta`.
The sum of motion steps on not-due visits is consequently **<= R ms** before
the next due test; a large forward jump that makes the test due contributes
no positive-speed integration after a successful zero publication. If measuring
from a continuous sphere entry during an integration rather than an already
covered post-integration vertex, include that crossing step's remaining
**Hcross <= 1000 ms**: a conservative physical-duration charge is
`(Hcross + R)/1000` seconds. At a covered post-integration endpoint Hcross=0.
This inequality still requires residence and covers only motion before the
publication/handover boundary, not subsequent KC commands.

By contrast the game-clock latency until the first due visit can overshoot R
by its next visit spacing J. The generic sampling bound is R+J, and current
`raw_step_ms` is a **past** delta, not a proof of future J or Hcross. Native
**50b615..50b639** publishes the last observed clock difference as cfg+0x704
only when it is 1..32767, otherwise substitutes 1. Clock advancement
**4fff26..4fff67** depends on current Q16 SETA operands +0xbc/+0xc0 and wall
time capped at 200 ms; the override **4fff08** can advance by another value.
**TI_SetAbsTime 4ce140..4ce158** clears step and rewrites now. Therefore a
small observed step proves neither a future SETA ceiling nor freedom from
clock replacement. Clock rewind or an integration path that leaves +0xa8 stale
breaks the telescoping inequality; do not silently charge it as R milliseconds.

`command_path` also refreshes `lane_steer/core::steer` on every modeled
future **0.25 s** advance. Native not-due visits retain their prior angular
pending commands and desired speed instead. Even a live I=50 receipt bounds
the randomized deadline by 49 ms, not the next command-visit spacing J; it
therefore does not imply a universal <=0.25 s refresh schedule. More frequent
native updates are not automatically equivalent to fixed 0.25 s modeled ones,
either. The broader slow-traffic witness must compare the actual due/skip
schedule with that unchanged rollout and retained-input motion, reporting this
as a model-discrepancy obligation. The stock FL census above is not substituted
for a live FL receipt.

No further broad scheduler RE is needed to reject the present universal tail.
The smallest missing acceptance witness executes the real order-4 due branch,
with I/S receipt, intervening original integration and actual point tests:
configured I=50 (0..49 ms cadence), I>51, inherited future S, skipped A with retained
positive speed, sphere entry followed by successful residence, a sphere
crossing missed by the next due sample, and changed callback/order. It must
observe `+0x10/+0x14` immediately before wake and reread callback-return state,
and distinguish forward SETA steps from a clock reset. The native 1 s clamp
and coherent +0xa8 updates can bound the physical crossing term; the original
current raw step cannot be reused as a universal future bound. A stronger
production clearance claim additionally needs the existing geometry/controller
model's residence and post-publication handover obligations; none is supplied
merely by carrying these two scalar fields.


## Minimal due/sample/brake fixture extraction contract (2026-10-30)

This is a read-only extension brief for the existing
`verification/probe/fl_lane_step_original.py` and
`fl_lane_step_native_inc.h` fixture, not an implementation or a new hook.
The completion-delay producer checks these **seven gap-free half-open spans,
495 bytes / 129 instructions** (measured static), recording only addresses,
counts and hashes. Copy bytes only into the existing build-local header,
keep the original VAs for relative branches, and retain INT3 outside admitted
spans. No full order-switch extraction is needed.

| Span [start,end) | Purpose and edges |
| --- | --- |
| `45e70b..45e75e` | Actual target check, signed due test, interval call and stamp publication. Null target `45e717 ->45e6e7` and nonexplicit-point fall-through `45e75e` remain unavailable. |
| `45e7bf..45e87c` | Actual explicit-point load, SSE distance/sqrt, integer distance conversion, class-18 radius. `45e861 ->45e87c` remains unavailable for any other target class. Class-18 jump `45e87a ->45e8c2` stays native. |
| `45e8c2..45e8f7` | Exact comparison, order/port/speed publication, wake and common exit. `45e8c8 ->45e8f7` is the due-but-outside exit; `45e8f2 ->462c6e` the completion exit. |
| `412a80..412a8e` | Actual `sqrtss` helper, XMM0 input/output, plain RET. |
| `50b5d0..50b5fd` | Actual interval helper, ECX=I, EAX=result, plain RET. |
| `532dcf..532df0` | Actual CRT RNG arithmetic; sole external call `532dcf ->537e72` obtains the current RNG record. |
| `46a550..46a59e` | Actual wake including callback call at `46a584`, argument cleanup and token clearing; ECX=ship, one reason DWORD, RET 4. |

There are **six external direct edges**: two forbidden branches (lost target,
wrong class) and four edges to three bridges (**462c6e**, **45e8f7**,
**537e72**), plus the nonexplicit fall-through exclusion. The indirect callback
at **46a584** is a modeled input to the actual wake body. No other native
callee or global table is needed for these admitted paths.

**Entry ABI / scratch.** A dedicated assembly wrapper enters **45e70b** with
EBX=live modeled ship and ESI=`[ship+0x50]` (flight). Assert current order 4,
non-null class-18 target, explicit point mode -1, stable identity/point and
coherent clock records before entry; the slice bypasses order dispatch and
does not establish those conditions itself. EDI is overwritten with target;
incoming EAX/ECX/EDX, XMM values and arithmetic flags are not inputs. DF must
be clear under the ordinary ABI. ESP must be **16-byte aligned**, with scratch
readable/writable through **ESP+0x37f**. `45e7c6` uses MOVUPS at +0x170;
`45e816` uses MOVAPS to read +0x370, and `45e81e` writes +0x60. There is no
+0x360 access. The existing LS_BEGIN's **0x240-byte scratch is insufficient**.
Zero a dedicated aligned **0x400-byte frame** for deterministic scratch. The
excerpt initializes the meaningful scalar inputs; the unused fourth DWORD at
+0x37c is read/copied without initialization. +0x24 (sector) and +0x108 (ship)
are not read by this excerpt, but may be populated for an explicit future join.
The 16-byte point load also requires flight+0x24c to be readable. Require
nonoverflowing signed coordinate differences and a computed, rounded SSE
`sqrtss` result strictly below **2^31** before `CVTTSS2SI`; finite individual
deltas alone are insufficient. Require positive class-18 radius with 3*radius
fitting signed32, and ordinary masked engine MXCSR controls. For example,
deltas (2000000000,2000000000,0) exceed that conversion range: masked
`CVTTSS2SI` yields INT_MIN, which can falsely satisfy the signed arrival
comparison. This is an inferred numeric counterexample, not native execution.

The wrapper must preserve its caller's EBX/ESI/EDI/EBP and return ESP exactly.
Native excerpt output deliberately clobbers EAX/ECX/EDX, ESI, EDI, arithmetic
flags, XMM0..2 and MXCSR status as its original operations require; do not
misreport that as a production-helper preservation failure. Keep the existing
A/B/E envelope checks at their own boundaries. At **462c6e** return through a
stack-restoring fixture bridge *before* the original sector/next-ship loop;
at **45e8f7** return a distinct due-outside marker. **462c6e has only four
bytes before existing B at 462c72:** the fixture's ordinary five-byte `jump()`
would overwrite the first byte of B's installed stub. Use a two-byte short
jump to a fixture-only exit island, e.g. **462c90** (short displacement +0x20),
then a normal jump from that island to the restoring wrapper exit. Preserve
B's entire 462c72..462c7b span and its fixture continuation RET at 462c7b;
the island is outside all copied spans and is not a production hook. Snapshot
output before
wrapper cleanup where needed. Callback count plus the exit marker distinguishes
skip, due-outside and completion even if the callback replaces order 4.

**RNG and callback inputs.** The smallest fully original arithmetic closure
models only **537e72** as a plain-RET getter returning EAX=fixture RNG record,
readable/writable through +0x1b; its +0x18 seed is consumed and updated by the
original LCG. Preserve normal callee-saved registers/stack in this getter.
Deterministic seeds select ordinary or extreme returned r without replacing
RNG arithmetic; allocation/fatal TLS paths are outside this modeled-record
scope. Point `*5a83b0` at the existing fixture clock and keep its +0x708 equal
to ls_now before commands/integration.

For actual wake dispatch, point `*0x5aa174` at a modeled callback table, set
table+4 nonzero and ship+0x7c=1, ship+0x80=context. Callback entry 1 lives at
**table+0x4c** (`+0x34 + 24*1`). It is cdecl `(context,reason)`; verify reason
1 and read order zero, port -1, desired/current speed zero **inside this
callback before any replacement**. A second callback variant can then write
replacement order/speeds. Actual wake returns to 45e8f2 and clears ship+0x7c
and +0x80 even if the callback installed another token. Use a normal-returning
callback; exceptions, nonlocal exits, pointer retirement and foreign reentry
are excluded rather than treated as a successful current-state borrow.

**Three bounded sequences.** (1) Set a future stamp, seed nonzero desired/current
speed and pending angular input, run the actual due excerpt, verify skip with
no interval draw/sample/wake or A call, then run existing B in a stale/refusal
control and original slew/integration/publication: retained intent must move.
(2) Hold the future stamp across original integration steps that enter and
leave the integer sphere; only then make the real due test run. It must take
the outside exit, retain order and positive speeds and produce no wake. A
cheap synthetic scalar witness is I=500, S=1500, now=1000, zero slip/angles,
rho4=750 units, point z=0, initial z=-1000 and speed=desired=35000 units/s:
16 ms steps advance 560 units, producing inside positions -440/120/680 then
outside 1240 before the delayed sample. These numbers are inferred setup
arithmetic, not measured native output or a whole-hull clearance witness.
(3) Arrange a due sample inside the sphere; observe the zero stores through
the actual callback before allowing the modeled replacement variant, then
reread return state and run the existing original motion slices. Those
integration slices exit before the native +0xa8 tail store: preserve and label
the existing fixture's explicit `ship+0xa8=ls_now` bookkeeping between steps
(as in `lane_command::advance`), or the next step's dt will accumulate wrongly.

Only the due-outside exit may schedule the fixture's existing actual A caller;
skipped visits must not refresh its command. The native order-4 continuation
`45e8f7..45ed7b` computes additional target state before calling `456810` at
45ed89, whereas the current fixture A caller enters its selected leaf through
**456997**. Returning at 45e8f7 and invoking that existing caller explicitly
continues to model this omitted target-selection join; it does not establish
full native order-4 steering equivalence. The useful new evidence is actual
due/skip/sample/zero/wake arithmetic around unchanged original motion slices.
Keep `qualification_pass=0` and the model-discrepancy/handover boundaries above.
No production handler, hook site, frozen candidate or existing native fixture
was edited or executed for this extraction brief.
