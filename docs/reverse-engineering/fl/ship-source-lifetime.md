# FL ship-source lifetime during a synchronous navigation copy

2026-10-30. Focused prerequisite to
[loaded source qualification](ship-source.md) and
[conservative turret membership](ship-turret-selection.md).
This concerns lifetime **during one bounded copy**, not complete ship
construction, immutable poses over 900 ms, or a generic completed-object bit.
Reviewed source/selection notes and production files were left untouched.

**Conclusion:** the recognized native scheduling/callback closure and existing
completed common owner can exclude the identified scene frees and table
reloads during an actual A/E helper's call-free copy. No new source observer
or general lease registry is necessary for that finite interval. However,
there is **no production navigation `live::Borrow.current` adapter yet**.
The interface, synthetic simulation fixtures, factory seal, renderer TLS and
`HelperBorrow` boolean do not themselves establish this admission. The missing
integration is the seam-local owner/readiness/domain check described below.

The [producer](../../../verification/results/fl-ship-source-lifetime/verify.py)
and [receipt](../../../verification/results/fl-ship-source-lifetime/evidence.json)
recheck native instruction windows, exact direct-call sets and entry-pointer
publications, bind the relevant current production sources and reuse accepted
callback/owner receipts. Run:

```sh
python3 verification/results/fl-ship-source-lifetime/verify.py
python3 -m py_compile verification/results/fl-ship-source-lifetime/verify.py
```

Raw Ghidra results remain local at
`~/.cache/x3m-port/fl-ship-source-lifetime/`, from the existing read-only
`FLRegistry/X3FL.exe` database and project analysis scripts. No PE/native
execution, Wine, game, build, install or production edit occurred.

## 1. What the existing C++ owners actually establish

`live::Borrow` explicitly delegates payload validity to a synchronous caller's
`current(context,domain)` callback. It creates no lease. Production currently
constructs factory and restore borrows:

* `gate_clearance_runtime.cpp::factory_current` checks cold transaction owner,
  engine/incarnation and published resource generation. It supplies initial
  empty-cache name provenance, not ordinary simulation borrowing.
* `body_metadata_fl.cpp::load_current` checks the exact outer native restore
  scope, common whole-load identity, lifetime Load identity and engine global.
  This deliberately runs during Load and is not a completed navigation owner.
* The simulation borrow in the existing gate-live fixtures is synthetic.

`ship_steering_internal.h::HelperBorrow` only sets a non-atomic
`helper_active_` reentrancy boolean. It supplies neither owner-thread admission
nor scene/type storage exclusion. The existing arrival helper does not acquire
that guard. These implementations must not be relabelled source leases.

The reusable positive prerequisite already exists:
`ui_scale_fl::chase_native_identity`. Before inspecting owner-only state, it
checks healthy publication and `GetCurrentThreadId()==owner`. It then requires
prepared common admission, nonzero incarnation, completed phase, no whole Load,
drain or pending close, and healthy UI coverage when enabled. It returns
`{common incarnation, common load generation}` **without requiring Render TLS**.
Its name does not require enabling chase-camera features. The common group
must still be prepared by the existing startup prerequisites.

By contrast, `fl_render_admission::current()` reads the calling thread's active
Render TLS cell. It is not available at ordinary navigation A/E and must not
be fabricated there. `current_load()` is explicitly a Load identity.

`object_lifetime::fl_owned_binding` supplies the already completed engine/map
binding and its observer/load/registry/mutation epochs. `fl_owned_ready` denies
active factory, bound-map mutation, Load, poisoned/incomplete engine factory,
dead/unbound engine and exhausted counters. It returns values under the
existing observer lock and retains no lock or payload lease. In particular it
does not observe every scene-cache allocation or free, and it does not prove
that an individual ship's construction completed.

Keep identity domains distinct: common-owner incarnation and lifetime registry
incarnation are different counters. For the gate name domain, engine and
incarnation come from the qualified lifetime binding (`engine`,
`registry_epoch`), while `Domain.load` is the common load generation used by
the existing name restoration path. New-game load generation **zero is valid**.
Do not demand equality of unrelated common/lifetime incarnation numbers.

## 2. Native free and reload paths that must be excluded

Scene destructor `4aeeb0..4af19f` removes the scene hash entry from
`[engine+80]` at `4aeec6`, then frees the scene's separately allocated part/key
arrays and record. Cache clearer `4af1a0` walks this same hash and calls the
destructor. A source record is not pinned by its scene ID, name revision,
readable pages, or a matching node-registry binding.

| Native target | Measured direct callers |
| --- | --- |
| Scene destructor `4aeeb0` | `441ba2`, `441c0d`, `442296` in type loading; `469c34`; simulation command `46d2c5`; renderer VM command `4b1b17`; cache loop `4af1f2` |
| Scene cache clear `4af1a0` | common cleanup `48a894`, shutdown cleanup `48aa95`, explicit cache clear `4a1e76` |
| Explicit cache clear `4a1e60` | simulation command `46f8d5`, renderer VM command `4b13ce` |
| Type loader `43e710` | startup `4778ae`, whole-load simulation restore `448bf9` |
| Type cleanup `442b80` | whole-load restore `448bf2`, manager destruction `476af9` |

`469a90` can detach/free instances and then destroy their source scene at
`469c34`. These are not renderer-registry membership mutations alone.
`4a1e60` frees scene caches and model resources; it does not wait for a retained
source ticket. All these paths remain permitted native behavior **outside**
the short copy interval.

The gun table demonstrates both early publication and a free gap. Loader
`43e710` clears gun pointer/count at `43ef51/43ef87`, stores decoded count at
`43efe1`, allocates/zeroes `count*0x60`, and publishes its pointer at `43f071`
**before filling the rows**. Cleanup's five-category loop uses pointer base
`5a847c` and count base `5a83b4`; gun category three is `5a8488/5a83c0`. It
calls free at `442e8b` and only later clears the published pointer at `442e9f`.
The analogous type-table free is `442d11` before pointer clear `442d25`.
Thus positive count/pointer and arithmetic bounds are not initialized/live
payload proof. A freed heap block may remain committed/readable. Equal
before/after common/name/resource/node-binding tokens alone cannot exclude
a scene-cache or gun-table free; no live crash is claimed by this witness.

The accepted [type lifetime cone](sector-fog.md#3-initialization-publication-and-actual-payload-lifetime)
places type loader/replacement in startup or the outer whole Load; cleanup is
in restore or shutdown. The exact direct sets above have no HIGHLOW absolute
entry-pointer publications in this image. These are finite supported-native
ownership facts, not proof against arbitrary computed private calls, injected
workers or a changed callback registration.

## 3. Why an actual owner-thread A/E copy can exclude them

Arrival seam `44d729` belongs to `44d3b0`. Its sole surveyed direct caller is
`4650af` in the sector post-pass `465030`. Driver `443b10` runs synchronous
sector commands at `443b49 -> 45c810`, then post-pass `443b50 -> 465030`,
and later integration `443b76 -> 463000`. Its sole direct caller is the
outer loop's `477ed2`. Existing E caller qualification supplies actual native
A/B objects at `457600`; it is not an arbitrary user callback.

Reuse the already reviewed
[command scheduling closure](material-submit.md#3-actual-borrow-readiness-writers-and-reentrancy),
[typed callback/recovery frontier](ui-render-mutation.md), and
[completed native owner recipe](prefill-ready.md#3-exact-input-read-admission-callbacks-and-exceptions).
Simulation native commands enter synchronously through registered dispatcher
`46a640`; renderer VM resource commands use the existing synchronous
registered dispatch. Suspended/pending tasks do not independently execute a
second command on this owner. Window/input/audio callbacks publish ready/input
work; close follows the deferred common close owner. The qualified frontier
does not enter these resource-free commands from a healthy admitted callback.
Allocation recovery is also a native callback boundary; a source reader must
not allocate or invoke a resolver that could enter it.

At the actual A/E instruction, the native caller is suspended until the helper
returns. Once same completed owner admission is established, a bounded helper
that introduces no game call, allocation, dispatch, message pump, alertable
wait or callback cannot run a second owner-thread free/reload between its
first and last payload copies. The common Load/close state excludes the
destructive outer phases; actual seam provenance excludes calling the reader
from a destructor, partial constructor, restore subsection or recovery hook.
This is the same finite exclusion principle used by the accepted native input
reader, applied to these additional source payloads.

This does not establish general engine-writer thread confinement. Recognized
native scheduling and the reviewed callback registrations are prerequisites;
an arbitrary foreign private call is outside that supported profile. Epoch
checks detect changed observations but cannot replace this pre-read exclusion.

## 4. Minimal sufficient integration contract

The eventual A/E adapter should supply one **seam-local, non-retained** Borrow
context using existing owners; it needs no new observer or source registry:

1. Before any payload dereference, require the recognized actual A/E route and
   complete helper CPU/LastError/MXCSR boundary. Establish healthy common
   native identity on its owner thread **before reading any non-atomic helper
   state**. Reject nested helper use and an active renderer callback route;
   do not treat Render TLS or `helper_active_` alone as permission.
2. Obtain the fresh completed lifetime binding and compare the actual engine
   global with it. Capture the existing common identity, lifetime epochs,
   resource generation and accepted name ticket separately. Bind the Borrow
   domain as described above, allowing new-game load zero. The factory pending
   name seal may be consumed only after this real simulation borrow is valid.
3. Keep an active invocation-local context for this helper. `Borrow.current`
   must verify that same context/thread/domain/common phase remains active,
   the engine global still agrees, and the required existing resource/name
   state has not retired. The context becomes invalid before helper return.
   It must not invoke a native loader, resolver, script, logger or callback.
4. Copy fresh scene hashes, source records, keys, type/gun membership and the
   actual node fields within the finite byte/node/key/link budgets using the
   existing checked bounded transport. Memory transport is not authority.
   The documented process-memory copy transport may reject unreadable spans;
   it must introduce no game callback/reentrant operation. Do not retain an
   observer lock across copying. Existing lock-backed binding snapshots belong
   before the first and after the last payload copy, not as a proposed lock
   protecting source allocations.
5. Recheck those existing identities before publishing only owned values.
   Erase transient native pointers and invalidate the invocation context
   before native continuation. A refusal leaves the public output unchanged.
   No token or root pointer surviving A may authorize a later E dereference;
   E obtains its own current seam borrow.

The implementation of that callback and compiled transitive call/CPU audit
remain missing. This study supplies its sufficient supported-native contract,
not a production reader admission. The host witnesses admit completed-owner
arrival and E (including load zero and unequal common/lifetime incarnations),
and reject sixteen missing seam/thread/scope/readiness/domain/exclusion facts.
They are policy witnesses, not native execution or a grant from a readable
memory test.

Complete source/tree reconciliation, per-object construction completion,
future writer/scale/topology invariants, native collision corroboration and
flight remain separate prerequisites. No generic completed bit, permanent
source pin, rollback/exception recovery promise or new SEH authority follows.
Foreign unwind retains the existing retirement/refusal limits; a later normal
counter check cannot repair an invalid read that already occurred.

Independent source/evidence review accepted this bounded contract and reproduced
the static receipt. The stated runtime integration and completeness gaps remain
open; review grants no native execution or flight acceptance.
