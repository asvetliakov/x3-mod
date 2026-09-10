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
