# FL new-game gate name provenance

2026-10-30, bounded static/host follow-up to the
[live-bounds study](gate-live-bounds.md) and
[body-name lifetime](body-name-lifetime.md). This establishes an
initial name/model provenance input for the gate reader; it does not implement
the input or admit traffic. The reviewed source input is the frozen
`~/.cache/x3m-port/fl-gate-live-32ce8935` checkout. No production edit,
PE/native/Wine execution, game launch, install or commit occurred.

The [producer](../../../verification/results/fl-gate-live-bounds/verify_bootstrap.py)
and [receipt](../../../verification/results/fl-gate-live-bounds/bootstrap-evidence.json)
bind 45 exact instruction anchors, eight gap-free regions / 825 decoded
instructions, five frozen source hashes and 27 synthetic protocol witnesses.
The existing name-lifetime receipt supplies the checked finite caller sets;
this task does not repeat its whole-image census. Addresses below are preferred
FL VAs at base `400000`, mapped through the admitted loaded image at runtime.
Raw listings remain local under `~/.cache/x3m-port/fl-gate-live-bounds/`.

```sh
python3 verification/results/fl-gate-live-bounds/verify_bootstrap.py
python3 -m py_compile verification/results/fl-gate-live-bounds/verify_bootstrap.py
```

`--source-root PATH` selects another source snapshot explicitly; the receipt
records the selected root and hashes. Host witnesses exercise the proposed
protocol, not the production adapter or an actual game allocation.

## 1. New game bypasses the save-restore provenance boundary

Outer loop `477ba0` compares owner `+4d8` with 7 at `477bf9`. Branch `477c00`
to `477c9c` skips the save-load call `477c52 -> 479430`. The new-game branch
calls native setup `4b30b0` at `477d09`, story loading `4b8e20` at `477d15`,
and joins ordinary continuation at `477e6d`. Therefore an implementation which
only enables Names at the successful `487740` restore endpoint leaves the
initial new-game path unavailable even though no name restoration was needed.

Use the existing observed factory output, not a new game-mode observer. The
constructor initializes model-list words exactly `{engine+4,0,engine}` at
`4825ec..25fc`, and allocates a fresh 16-byte model hash through the existing
zeroing allocator at `48263f`, publishing it at `engine+14` at `482656`.
Hash buckets start at eight; membership count is initially zero. This is
positive initialization evidence, not a reason to infer completion merely from
a readable allocation. The actual list/hash emptiness must still be copied at
the qualified factory endpoint.

Factory `48a5a0` establishes fixed=11000 / dynamic=0 at `48a5f7/601`, calls
BodyData initialization at `48a643`, performs both resource-name suppression
passes at `48a752/757`, and returns normally at `48a76d`. Reuse the existing
`runtime::qualified_factory(engine, incarnation, owner)` notification after
successful cold verification and current resource publication. Its engine
operand comes from the existing outer normal factory owner; the pending token
and SEH envelope cover the unlock-to-cold-verifier interval. Do not chase a
global engine pointer as a substitute or hold the metadata lock while reading.

The established finite caller sets are material: fixed-name initializer
`487100` has sole caller `48a643`; dynamic reset `486450` has sole caller
`487754`, inside the already observed restore. Name setter `4866e0` has only
registration callers `486e79/486ed2` and factory initializer `487372`. Full name
free `486280` has only final-teardown caller `48a968`. Thus existing factory,
registration, restore and dependency/teardown coverage supplies the finite
native mutation boundaries; no second initializer/name observer follows.

## 2. Minimal pending factory seal and first borrowed binding

Add a feature-local pending seal containing **engine, engine incarnation,
verified resource generation and current name-revocation value**, with an
explicit *load not yet bound* state. At the existing factory output:

1. Require active name-hook dependency, the admitted owner thread, successful
   normal outermost factory/cold verification, matching engine/incarnation and
   a still-current resource generation. Refuse exhausted revocation counters.
2. Copy actual model-list and model-hash emptiness using the established
   `empty_models` shape under that factory borrow. Check resource/revocation
   state before and after the finite copy. Discard all native pointers.
3. Publish only the pending scalar seal. It grants neither a simulation borrow
   nor a guessed load generation, and must not make a live NameTicket ready.

On the **first actual qualified simulation Borrow**, validate that Borrow before
and after activation, require the same engine/incarnation, still-current sealed
resource generation, unchanged revocation and active dependency, then bind its
actual `Domain.load` once. Initial load generation **zero is valid** on the
new-game path. A caller-supplied number without Borrow validity never qualifies.
After activation, ordinary exact-domain ticket checks apply; a later different
load must not silently rebind an already consumed seal.

Do not require an empty cache at first simulation use: legitimate new-game
construction loads bodies before then. The continuity premise is the sealed
empty cache followed by covered, non-remapping name registration. The append
guard below closes its one concrete future-ID exception. Cache reclamation
alone is harmless; fresh cache entries still use the current unchanged names.

Restore entry, dependency loss/rollback, observed foreign mutation, abnormal
completion or revocation exhaustion consumes the pending seal and invalidates
live tickets. Existing ordinary-load restore completion can establish its own
fresh provenance; the factory seal must not bypass it or resource retirement.
Cold resource data remains separate from name validity. This supplies a useful
initial new game without imposing a save/load cycle on the user.

## 3. A concrete future-ID hazard in otherwise append-only registration

Registration `486c60` takes ECX=manager and one stack string-object pointer,
returns integer ID in EAX, and uses `ret 4`. At `486c64` it obtains the argument;
`486c68` preserves manager ECX in ESI. Its native prologue saves ECX, EBX, ESI
and EDI. Numeric conversion can return an arbitrary parsed integer without
inserting a row (`486d20`). Existing named hits return the original ID.

The named-miss path increments dynamic count **before commit** at `486d8a`,
may replace the table pointer at `486dd9`, and initializes slot
`fixed + after_dynamic - 1` at `486ddf..6e51`. Setter calls are `486e79/6ed2`.
For the admitted fixed=11000 and nonnegative before count D, the prospective
append has:

| Value | Checked expression |
| --- | --- |
| Appended slot | `11000 + D` |
| Returned requested ID | `20000 + D` |
| Model-hash key | `20001 + D` |
| Committed after count | `D + 1` |

The requested-ID conversion is explicit at `486ec6/6ec8`; the existing-name
dynamic conversion is at `486ea8/6eaa`. Use checked wide arithmetic and the
reader's bounded count domain, never signed wrap. Reallocation failure rolls
the count back at `486eb6` and returns -1 at `486ebc`. Setter/name allocation
failure can still leave a committed new row with a null name and returned ID;
it is not the same operation as that rollback.

Append-only insertion preserves **previous valid slot/name bindings**, but
does not alone prove every previously cached numeric ID remains equivalent.
For a cache miss with an invalid positive ID, exclusion getter `486910`
returns zero at `48697a`; name getter `4867b0` returns an empty string on its
invalid-slot path `486855..6873`. Model loader then chooses numeric fallback
at `4a1ad6..1ae4`, after calls `4a1ab6/1aca`. If such a resource exists, it can
already occupy the future ID's cache key. Later registration of that ID as a
new named row changes its effective resource name while the old model survives.
This is a concrete conditional native route, not a measured stock occurrence.
The protocol must cover it rather than assume all mods lack numeric bodies.

## 4. Close that hazard inside the existing registration scope

While a pending seal or ready name provenance exists, the already installed
registration wrapper records the manager's **before fixed/dynamic counts**,
prospective append ID and whether that ID already has a model-cache entry.
The lookup is a bounded checked read of engine+14, one hash bucket and at most
64 links, with checked key/address arithmetic and a bounded power-of-two bucket
count. Record only a scalar `present / absent / unavailable`, never a retained
model pointer. No name-table scan or per-model observer/map is needed.

The borrowed authority is the actual synchronous registration call's live ECX
manager operand, additionally matched against the already sealed engine,
incarnation, owner and current resources. It is not `fl_owned_binding`, render
admission or page readability. Keep the existing whole-call scope live for the
post-copy. Make no engine call, allocation, wait, log or message pump during
either finite reader. Read fresh table/header fields after native return,
because registration may have reallocated the records.

On normal, unpoisoned matching return, classify an actual append only when fixed
is unchanged, after dynamic equals before+1 **and EAX equals the captured
prospective ID**. If its pre-call cache result was present, revoke pending/ready
name provenance before the caller consumes that ID. A proven pre-call miss
preserves provenance. A numeric return equal to the prospective ID with an
unchanged count is not an append; neither are duplicate hits, empty names or
the count-restored reallocation-failure path. Do not revoke merely because a
speculative future-key lookup found a model when native code inserted nothing.

Unreadable/capped/invalid pre-state, changed owner/domain/resource/revocation,
unexpected count delta or result, a foreign call, nested ordinary registration,
exception search/continued exception or unwind cannot preserve an unqualified
provenance claim. Mark the affected scope unavailable and revoke live name
validity conservatively; do not relabel these as a confirmed cache collision.
Never restore a consumed pending seal on an outer normal return. Registrations
inside an active restore are expected: restore has already consumed the old
name provenance, and its independently qualified endpoint owns readmission.
Do not make those expected nested registrations defeat every normal load.

For the fixed native registration call graph, allocation recovery follows the
existing typed `47b500` reclamation/texture frontier in
[render-mutation](ui-render-mutation.md#allocation-recovery-actual-objects-callbacks-and-finite-closure).
It does not create a body model for the prospective ID before name commit.
This matters to the pre-call miss premise; allocator failure is not assumed
callback-free. A newly installed native callback/private foreign entry is
outside that profile, and observed nesting/exception paths still refuse.

The source-derived hot-cost consequence is finite work per native registration
while this feature needs provenance, with no full table/model-cache scan and no
per-draw work. Actual runtime cost and callback/CPU-envelope acceptance remain
implementation checks. The existing adapter must continue preserving all input
and native output GPRs/flags, x87/XMM/MXCSR and LastError, retain four-byte incoming
stack support, forward once and perform exactly one callee-cleaned stack word.
No new hook or blanket new-game refusal is required.

## Independent review

The bounded RE review reproduced all 45 anchors and 27 schematic cases and
accepted the proposal. Implementation must align its dynamic-count boundary
with the live reader (maximum 32767); the schematic append witness at 32768
does not establish reader admission. Stateful seal consumption, refusal of
subsequent rebinding, revocation races, nested restore registration and actual
wrapper execution remain implementation acceptance checks. No native or flight
acceptance follows from these static/host witnesses.
