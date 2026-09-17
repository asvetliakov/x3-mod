# Central object-lifetime observer verification

The opt-in [production observer](../../src/proxy/object_lifetime.cpp) passed
**533 checks / 72 original backend calls** in a fresh 32-bit synthetic executable
under CrossOver Preview's Steam bottle. The six isolated runner-provenance tests
also pass. This proves the tested forwarding/bookkeeping mechanisms, not live
renderer coverage or complete temporal antialiasing. No game was launched and no
running game was patched by this fixture.

## Inputs and reproducibility

```sh
python3 verification/probe/run_object_lifetime.py
python3 -m unittest discover -s verification/analysis -p test_object_lifetime_runner.py
```

The [runner](../../verification/probe/run_object_lifetime.py) builds the original
[fixture](../../verification/probe/object_lifetime.cpp) and production observer
with MinGW x86 C++17, `-O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse
-mstackrealign -mincoming-stack-boundary=2 -static`. The production source also
passes the same strict compilation without the fixture macro. The fixture emits
original registry operations and synthetic instruction layouts; it contains no
copied game implementation or game shader data.

The compact [summary](../../verification/results/object-lifetime-summary.json)
records source/header/fixture/build/runner hashes before build, after build and
after execution, the fresh executable hash before/after execution, exact command,
exit code and report hash. The [terminal report](../../verification/results/object-lifetime.txt)
contains the check totals. The runner clears any old PASS before reading inputs,
catches build/run and provenance failures, applies finite build/run timeouts,
and accepts exactly one terminal RESULT line. It never accepts a stale executable
following a failed build. The six host tests exercise missing inputs, build
exceptions/timeouts, duplicate/nonterminal results, changed sources/executable,
and a single valid stable run.

## Actual boundaries and ABI controls

The four tested original functions use the reviewed custom x86 input conventions:

| Boundary | Original inputs | Forwarding control |
| --- | --- | --- |
| Insert | EDI map; stack key/value | Exact five-byte synthetic prologue; zero-return overwrite and one-return birth |
| Remove nonempty block | EDI map, EDX key, live EAX bucket pointer | Six-byte internal block after the empty-map branch |
| Destroy | Stack map | Exact five-byte synthetic prologue; releases original bucket/link storage |
| Renderer load | One cdecl stack argument | Relative CALL to an original synthetic target, success and zero-return failure |

Each boundary is compared with its own unwrapped original. The checks compare
all output GPRs and EFLAGS, all input GPRs and EFLAGS after the displaced original
instructions, and explicit caller ESP equality. The intermediate PUSHAD ESP
slot is excluded because the forwarding frame has a different address. x87
control/status/tag/register contents, MXCSR and XMM0–7 match exactly. Seeds use
nondefault supported x87 control `0x077f`, masked invalid sticky status and MXCSR
`0x3fa0`, with all x87 storage slots initialized before comparison. The original
callee deliberately changes its FP/register state; the wrapper must preserve
those changes rather than restore the caller's input over them.

LastError survives entry/current queries and reflects the original backend's
chosen value after return. Nested insertions see an unavailable mutation scope.
Each of the four boundaries raises an original foreign Win32 exception which an
outer SEH frame catches using RtlUnwind; all observer scopes are cleared, old
tokens become unavailable, and later observed births can recover. No C++ exception
unwinding is substituted for this foreign SEH test.

## Identity, baseline and failure cases

- Observed node/camera births receive different nonzero serials; repeated
  pointer+handle insertion, zero-return overwrite and removal/reappearance all
  establish fresh serials. Camera removal invalidates camera identity.
- Unrelated generic maps preserve known lifetimes. Absence of the engine during
  startup remains dormant; construction and later observed births become known.
  Loss after a trusted baseline disables observation whether detected by a draw
  snapshot or an unrelated generic-map call; restoration cannot revive tokens.
- Absent-key removal and collision-chain deletion preserve other identities.
  Rehash performed inside the original insertion changes bucket storage while
  retaining existing object lifetimes. Destruction/recreation at the same map
  address advances the registry epoch.
- Successful and failed renderer loads invalidate before the call. Nested and
  foreign-unwind paths cannot publish a partially completed insertion.
- A complete populated initial snapshot includes the already-existing camera.
  Invalid baseline cases cover duplicate pointer, cyclic chain, declared count
  too small/large, unreadable value, handle mismatch, capacity overflow, null
  buckets with nonzero count and non-power-of-two capacity. None publishes a
  partial baseline. Disabling initial snapshot does not permit lazy draw adoption.
- A small capacity seam proves full tables disable rather than evicting/reusing
  live identities. Invalid configured capacities are rejected before patching.

Every patch site is tested against each injected install-protection, instruction
cache flush and protection-restore failure, including second-stage rollback
failures and later recovery. Every site also receives each shutdown failure.
Wrong prologue layouts and wrong call target leave all sites unchanged. A foreign
replacement is not overwritten, and a repaired ownership record can be retried.
Restoring an insertion site externally and performing an unobserved same-pointer
birth makes `current()` unavailable; pointer equality cannot hide lost ownership.

The final production-style retirement case saves an installed dispatcher,
shuts down successfully, then calls that saved dispatcher. It still forwards to
the original while observation stays disabled. Reinstallation is refused. The
fixture's earlier repetitive fault loops explicitly select a synthetic-only
resettable mode whose caller guarantees no retained dispatcher; this mode does
not exist in the production initialization API.

## Limits and live acceptance

Installation, baseline traversal and patch removal require an externally
quiescent point. The two-pass baseline detects inconsistency but is not a lock on
the game's registry. The fixed table admits at most 16,384 tracked entries;
initial traversal admits at most 65,536 buckets, and runtime membership lookup
bounds a collision chain to 512 entries. Exceeding a bound cannot create known
identity. Baseline rejection may leave observation active for later explicitly
observed births; actual tracking-capacity exhaustion disables it.

This observer covers the exact-version normal mutation paths documented in
[object-lifetimes.md](../reverse-engineering/object-lifetimes.md). It does not
prove the absence of arbitrary indirect writes, external memory modification,
or undetectable changes which bypass and later restore every hook between
observations. Map membership alone cannot repair such missed history. It does
not identify in-place camera mode changes, teleports or cuts. The consumer must
retain separate camera/device/frame invalidation and geometry revision policy,
and reject any mid-frame change of epoch, revision or serial.

A user-controlled game capture remains required to measure baseline usefulness,
node/camera birth and retirement coverage, reload epochs and camera-switch
behavior. The mechanism is opt-in and synthetic success is not a live-TAA claim.

## Retirement journal (2026-09-17)

Prerequisite of shadow caster retention
([shadow-caster-retention.md](../architecture/shadow-caster-retention.md),
"Retirement"). API in `src/proxy/object_lifetime.h`: `journal_register`,
`journal_unregister`, `journal_drain(cursor, out, capacity)`, `journal_stats`.
No new EXE patch site and no change to the dispatcher or its envelope.

- Fixed ring of 512 `JournalEntry` (32 bytes each, 16 KB static), 64-bit
  sequence numbers, no allocation. Appended under the observer's existing
  spinlock, so it inherits the observer's threading contract: callable from any
  thread, never from inside an observed engine call (non-recursive lock). The
  journal functions make no Win32 call and leave LastError untouched.
- `Retired` carries `(load_epoch, registry_epoch, node_serial)` and is written
  at the single existing retirement point `retire()`: removal hook, overwrite
  (insert pre-retire) and failed membership in `current()` (node, then camera).
  Untracked keys append nothing. The engine has already released the node's
  buffers when the removal is observed
  ([shadow-caster-lifetime.md](../reverse-engineering/shadow-caster-lifetime.md) §4c).
- `FlushAll` is written with every whole-table clear: load epoch, registry
  rebind/destruction/loss, foreign unwind, ownership loss, capacity exhaustion,
  install and shutdown. Its epochs are those at the append (the clear can
  precede the bump); `JournalDrain` returns the current epochs and revision.
- Nothing is written with no consumer (one integer test per retirement). The
  first registration skips a whole ring of sequence numbers, so a cursor of an
  ended registration, a zero cursor or a backlog above 512 drains as
  `overflow` with the cursor moved to the head; `available=false` when the
  observer is disabled or unregistered. Both mean full revalidation through
  `current()`. A short output buffer returns the oldest entries and `more`.

Fixture (`run_object_lifetime.py`, bottle X3, arm64 Wine, FEX reduced
precision, executable `da690d4c…`): 648 checks (574 before), no journal check
fails: no consumer writes nothing; 7 retirements (4 removals, overwrite, failed
membership node+camera) drained in order with matching keys; absent-key
removal and fresh births append nothing; 2+1 partial drain; load and registry
destruction each one `FlushAll`; 600 retirements between drains give
`overflow` with nothing partial, the next retirement drains normally, exactly
512 drains without overflow; stale cursor after re-registration overflows;
shutdown appends `FlushAll` and reports unavailable; drain preserves LastError.
Measured, 20,000 hooked insert+remove cycles, best of two passes each:
1.2965 µs without a consumer, 1.2926 µs with one draining every 256 cycles
(delta −0.004 µs, below noise); empty drain 0.0058 µs; 40,000 of 40,000 timed
retirements journaled once. The runner requires exactly one `JOURNAL` line.

The run as a whole reports **10 failures, all pre-existing and outside the
journal**: the same ten FX-state comparisons fail in the committed X3 record of
2026-09-12 (574 checks, 10 failures). A scratch diagnostic shows the differing
bytes lie only in the x87 register slots ST0–ST7 of the FXSAVE image (mantissa
top byte and exponent, offsets 39–153 of the slots at 32–159), i.e. FXSAVE/FXRSTOR does not round-trip the 80-bit registers bit-exactly under
`FEX_X87REDUCEDPRECISION=1`; control, status, MXCSR and XMM compare equal.

Host: `test_object_lifetime_runner` 8 tests (new: journal line required) and
the six extracted-snippet modules, 35 tests OK. Scratch clean build of
`d3d9.dll`: 0 warnings; `check_no_x87.py`: 511 reachable, 0 violations.

### Review fixes (2026-09-17, second commit)

Ring raised to 2,048 entries (64 KB static, still allocation-free) so a
gate-jump burst of about 600 retirements drains in one frame. `journal_drain`
with a null buffer or zero capacity returns `invalid` with `count=0`,
`more=false` and an unmoved cursor. Registration at a saturated consumer count
is refused with an invalid cursor (default `JournalCursor` is invalid, never
becomes valid by draining, and owes no unregister). The header now states the
consumer contract: revalidate fully at registration/re-registration, on
`overflow` and on `available=false`; `FlushAll` is an unconditional drop and
epoch comparisons use `JournalDrain`; `Retired` is not proof of death. The
figures of the section above (512, 648 checks, costs) are superseded by these.

Fixture rerun, bottle X3: 673 checks; the runner now requires twelve
`JOURNAL_CASE ... result=PASS` lines and all twelve pass: `no_consumer`,
`retire_in_order`, `partial_drain`, `invalid_drain`, `flush_load_epoch`,
`flush_registry_destroy`, `flush_registry_rebind`, `overflow_and_recovery`
(2,136 lost; one recovers; exactly 2,048 drains; 2,049 overflows), `cost`,
`reregistration_and_shutdown`, `flush_capacity_exhausted`,
`saturated_registration`. Cost, 20,000 hooked insert+remove cycles, best of two
passes: 1.4900 us without a consumer, 1.2392 us with one (the append is below
the run-to-run noise of the hooked cycle under FEX; the first run measured
1.2965/1.2926); empty drain 0.0063 us.

The run still exits failed on **10 FX-state checks, accepted as the baseline**:
the same ten labels fail on main's committed X3 record (x87 register slots do
not round-trip through FXSAVE/FXRSTOR under `FEX_X87REDUCEDPRECISION=1`); no
journal check is among them.
