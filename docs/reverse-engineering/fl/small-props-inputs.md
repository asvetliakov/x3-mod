# FL small-prop classification and exemption inputs

Bounded offline contract study, 2026-10-30, original FL SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`,
2,804,736 bytes. Preferred VAs use base `0x400000`; runtime addresses use the
admitted loaded base plus RVA. Owning feature is **Small props**, not the older
native node-size cull, in [cull-small-parts](../../verification/cull-small-parts.md).
No production changes, build, Wine/native/game execution, installation or commit.

**Reuse the current owned FL material record and the existing cold body-name
pipeline.** No new native detour, target observer, simulation-registry cache,
per-draw body-name read or second ancestry walk is required. The current
implementation lacks target publication and exact prop-name publication; this
note qualifies their bounded inputs, not a working FL small-prop consumer.

## 1. Active cockpit, target and roots

Existing [own-ship evidence](own-ship.md) establishes the active
cockpit lookup, reference root, parent graph and material entry borrow. Existing
[chase camera](chase-camera.md),
[lead](chase-lead.md) and
[fire](chase-fire-aim.md) independently identify target `+0x1e0`.
The following selected original-image operands close the target-specific setter
and invalidation question; they do not add a new hook.

| Link | Established contract and evidence |
| --- | --- |
| Active registry | Slot **`0x5a82e4`** (RVA `0x1a82e4`), already in `engine_profile_fl_material::Pack::cockpit_slot`. Registry map `+0`, active handle `+0x10`; map buckets `+0`, capacity `+4`; link `{next,key,value}` at `+0/+4/+8`. Native resolver **`0x41e420`**, ECX=registry, stack handle, EAX=cockpit, RET4. Existing material copy resolves one bucket without calling native code. |
| Own reference | Cockpit **`+0xc`**, reference object render root **`+0x70`**, render handle **`root+0x28`**. Existing `Record::own_ship` already owns root/handle/serial/descends under the full material ticket. Cockpit `+0x10` is separate view/control identity and must not replace the reference. |
| Selected target | Cockpit **`+0x1e0`**, simulation object pointer. Native command reads it at **`0x43106f`**, then exports object ID **`+8`** at **`0x43107d`**. The pointer is the current selected target irrespective of tracking mode. Requiring tracking 2/3 or a bound chase pose would change small-prop policy. |
| Native target setter | **`0x4273a0`**, ECX=cockpit, two stack words **mode,target**, RET8 at `0x427425/0x4274c2/0x4275b8/0x427624`. Entry reads mode at original ESP+4, preserves receiver in ESI. Mode-3 branch reads target at post-save ESP+0x14 (`0x4275bb`), stores **`[ESI+0x1e0]=ECX` at `0x4275ed`** and tracking=3 at `0x4275f3`. Another selected branch writes target at `0x4275a5`; clearing branches write zero at `0x427407/0x4274a9`. |
| Command publication | Dispatcher **`0x430540`** obtains cockpit with `0x430fef → 0x41e420`. Target ID is resolved using simulation registry **`*0x5a82ec`**, call **`0x43102a → 0x443d30`**; null is rejected. It pushes resolved EAX, mode3 and sets ECX=cockpit, then calls setter at **`0x43103c`**. The material adapter need not repeat that simulation lookup or keep an object-ID registry. |
| Target render root | Native solver **`0x4505a0`** has ECX=ship, five stack words including target as second word, as already established by the lead note. At **`0x4506d1`**, EDX receives target from EBP+0xc; **`0x4506d8`** loads target **`+0x70`**, then `0x4506db` consumes root position `+0x30`. This is independent native target-root evidence. Root handle is the same render-node **`+0x28`** identity used by existing material/observer code. |
| Target retirement | Existing invalidator **`0x427640`**, ECX=cockpit, one stack object pointer, RET4. It obtains object at `0x427641`, compares cockpit **`+0x1e0` at `0x427672`** and clears a match at **`0x42767a`**. The already-established all-cockpit `0x42fc70` calls this helper at `0x42fcda`; object-reference cleanup `0x454190` reaches that enumeration at `0x454206/08`. Target and own-reference cleanup share this path. |

The root-before-clear destruction witness from the own-ship note applies equally
to a selected target: object destructor `0x4491a0` destroys root at
`0x449303 → 0x4a3300`, clears object+0x70 at `0x449308`, and invokes reference
cleanup later at `0x44938e → 0x454190`. An old target pointer, matching object ID,
or post-read observer check cannot authorize reading through that interval.

Dereference permission comes from the existing common complete-pass/owner/load/
close/callback exclusion, at material entry before forwarding. Synchronous native
commands complete before rendering; legitimate callbacks in the admitted copy
interval do not run these setters/destructors. The complete bounded native copy
must remain call-free, with no native resolver, allocation, logging, OS/page query,
COM call or observer lock inside it. Full identity validation follows the final
native read. This reuses the qualified healthy-native profile; arbitrary foreign
mutation/private render calls are outside its contract. No target lifetime serial
is inferred from object address or ID. The existing B3D observer supplies the
**root's** serial; invocation-local copying plus native admission supplies target
pointer lifetime.

## 2. Owned result and ready/absence/unavailable semantics

Keep `Record::own_ship` and add an analogous compact target result under an
immutable startup demand for small props. Do not enable target work for a
shadow-only own-ship demand. The selected cockpit is resolved once, both roots
are copied in that same native interval, and each result contains status,
`root/root_handle/root_serial` and `descends`. Add a small strict-absence reason
or flag for the own result: its current coarse `NoRoot` is insufficient for the
small-prop decision, as the comparison below shows. Never treat either root token
as a later renderer dereference permission.

- **Ready:** nonzero selected root/handle; ancestry completed to a matching root
  or null; every identity needed by the result validated against the same owned
  binding/camera; full borrow and binding still match afterward. A non-descendant
  result is Ready too. Own wins the diagnostic label if both match.
- **Explicit absence:** a null target in a valid selected cockpit is no-target.
  A validated registry/map with zero active selector can publish both roots
  absent without consulting a stale row with key zero. A valid cockpit with null
  reference gives explicit own absence; still inspect its target independently.
  Each empty result freshly carries root/handle/serial zero and descends=false.
  Replacing a nonnull target with zero therefore cannot preserve an old exemption.
- **Unavailable:** malformed/arithmetic/alignment failure, cycle, depth/hash bound,
  stale identity/borrow, invalid observer, failed serial, null registry, missing
  nonzero selected row, or nonnull own/target object with null render root/handle. Conservatively keep the draw whenever either required
  result is unavailable. In particular do not reinterpret an incompletely
  initialized selected target as explicit no-target. The present `Unknown/Limit`
  distinction is adequate for failure, but strict own absence needs the additional
  reason/flag; names may remain `NoRoot` or use target `NoTarget`.

Exact AP comparison, from `object_capture::{own_ship,target}` and
`Culler::resolve_roots` (source-established):

| AP input/state | AP small-prop interpretation | Required FL interpretation |
| --- | --- | --- |
| Both Ready | Both roots usable | Ready only after owned root/node serial validation. |
| Own Ready/NoTarget and target NoTarget | Target known empty; own may still exempt | Same, publish empty target on this invocation. |
| Valid registry/map, active handle zero; target bucket absent | Own NoTarget plus target Missing is explicitly accepted | Both known empty; do not consult a possible stale key-zero row. |
| Nonzero active selector, missing selected row | Own Missing, roots unresolved | Unavailable even though existing FL own adapter labels this NoRoot. |
| Null/unreadable registry/map | ReadFailure, roots unresolved | Unavailable even though existing FL own adapter labels null registry NoRoot. |
| Valid cockpit, reference null | Own NoTarget | Known own absence, target remains independent. |
| Nonnull reference but root null | Own Malformed, roots unresolved | Unavailable even though existing FL own adapter labels this NoRoot. |
| Nonnull target but root null | Target read failure, roots unresolved | Unavailable. |

AP accepts own only for Ready/NoTarget; target only for Ready/NoTarget, or the
special `(own NoTarget, target Missing)` case. All other required root failures
produce `unresolved` and draw. An own absence flag computed in the shared native
copy, plus the new compact target result, is one minimal way to preserve this
policy while leaving adaptive shadows' existing broad NoRoot unchanged. Misaligned
object pointers should remain unavailable, even where the AP own reader currently
uses its broad NoTarget branch. This is a conservative refusal, not an exemption.
No `NoRoot` result alone may authorize an FL skip.

The existing own copy bounds one cockpit bucket to **32 links**, capacity to a
power of two **<=65,536**, and ancestry to **16 visited nodes**. These are policy
bounds, not measured native maxima. The same ancestry loop can compare each
visited `(pointer,handle)` to both roots, using `copy_identity`'s existing parent/
handle cache. Stop once both nonzero roots have matched or null terminates; a
bound/cycle before proving required ancestry yields unavailable, never a skip.

Append each distinct root not already visited, then call the existing
`fl_owned_nodes` once for the union. **Current `OwnedNodeLimit=17` is insufficient
for sixteen visited nodes plus two separate roots: the required bound is 18.**
Deduplicate roots equal to each other or to a visited node. Existing occlusion's
six-parent prefix and nozzle parent remain shared and ordered as today. Native-copy
exemption failures preserve completed unrelated optional payloads. The existing
observer batch is deliberately all-or-nothing: any unproven member invalidates
all optional identities for that invocation, while an independently valid base
material sample survives. No absence is inferred from observer refusal. Review existing
post-failure `count` truncation before extending it: it currently assumes just
one own result. No new observer or independently maintained ancestry cache is
needed.

Publish root serials only after the one owned batch succeeds; recheck the full
borrow/binding as existing material entry already does. Keep ancestry scratch in
the helper, not TLS/history. The existing scope is bounded by **512 bytes**;
adding a target record needs its `static_assert` and assembly reservation checked.
The hook stays the existing whole CALL **`[0x4e7142,0x4e7147)`**, with ECX=
descriptor, EDX=node, stack camera/flags/light1/light2 and caller cleanup of 16
bytes. Preserve the qualified thunk's GPRs/EFLAGS, native input/output LastError,
x87/MXCSR/XMM state, four-byte incoming stack and SEH search/unwind behavior.
Nested invalid children must shadow outer values. No exception path may publish
a partially copied target or restore an outer record while an invalid child is
still active. This study adds no installation/inbound-edge claim for a new hook.

## 3. Exact prop classification from existing cold names

`body_metadata_fl::Class::part` is too broad: the shared occlusion classifier
also recognizes turret, weapon, gun-barrel, dock and antenna names. Small props
requires exactly the **12-byte, ASCII-case-folded and slash-normalized prefix
`ships\props\`**, using the existing `cull_small_props::core::is_prop_name`
semantics. A short or absent name is not a prop. Unnamed numeric defaults and
dock ID ranges do not qualify merely because occlusion classifies them as parts.

Reuse `body_metadata_fl::core::Cache::slot`'s one transient name copy and existing
native-slot mapping. It already services baseline rebuild and committed single
registration; whole-call registration/restore and common retirement revoke the
same revision before mutation. A named alias shares its stored name; unnamed
aliases retain numeric defaults, all outside the prop prefix. The table uses
manager+0xb4/+0xb8/+0xbc and 0x1c-byte rows with name pointer+0xc, as established
by [body-name lifetime](body-name-lifetime.md).

Minimal storage option: retain the existing **one-byte-per-slot allocation**,
encode exact-prop membership in one spare high bit, and mask the low occlusion
class in `class_of`. Expose a separate exact-prop value lookup with the **same
`Snapshot` key/revision/validity**; never expose the encoded high-bit byte as an
occlusion enum. This is an extension of the existing table, not another cache.
Equivalent internal cell representation is possible if measured memory cost
warrants it. Existing allocation capacity is 1,011,000 bytes (11,000 fixed plus
1,000,000 dynamic policy cap); reusing a spare bit adds no per-slot storage.

Add a frozen independent props-name demand: props-on with occlusion-off must
still allocate/populate this shared classification storage and invoke the
existing render-entry rebuild. Current `request_startup`, `render_enter` demand
check and `name` read cap test only lens/classes/effects. All must recognize the
new demand. Existing 96-byte classification reads already cover the 12-byte
prefix; a props-only demand needs at most twelve bytes, while combined demands
keep the maximum needed by the existing consumers. No second table scan or
name read is required. Gate-only demand must not accidentally start a name scan.

A hot lookup checks the current snapshot/domain/revision, maps copied `model`
through the existing fixed/dynamic slot mapper and tests one byte. Invalid
metadata is unavailable and keeps the draw. Compare snapshot key with the
material's incarnation/common-load/observer/load/registry/engine/map before use;
no AP body-table read, periodic class-cache refresh, numeric prop list or
per-draw string read is justified.

## 4. Early renderer scope and decision reuse

Small-prop cull currently runs after `scene_bound()` and before jitter/bindings,
**before `sample_scope` populates route.key and scope_parent**. The current AP
include explicitly calls `object_trace::scope_node`, then invokes AP addresses/
readers. Opening the family gate alone cannot port it. Use existing
`draw_sample(route,true)` / `render_engine_state::current` to obtain the innermost
owned FL `DrawSample`; carry the new target beside existing `own_ship`. The
route-local sample is shared with the later `sample_scope`; FL does not retry
or attach fresh identities to old data. Require the same known Node/Camera/
Registry validity, `fl_scene_ready_`, `fl_motion_scope::agrees` and camera
invalidation-epoch agreement as the existing FL scope gate. `scene_bound()` alone
does not establish material-camera agreement. Do not run AP `engine_node_gather`
or dereference the FL descriptor as a mesh part. The old part AABB is diagnostic
only; the actual decision already uses the portable vertex-extent cache and draw
clip rows.

Retain the first-draw geometry decision only under current FL identity and
exemption inputs. AP's `(node,frame)` memo and multi-frame `(pointer,handle)`
ancestry cache cannot authorize FL: model/root/name revision or ancestry can
change without observer revision. A cached skip must be revoked for a newly
exempt or unavailable owned result. Key/invalidate the memo using full domain,
node serial, copied model, name revision and current root identities/statuses/
descendant verdicts, or recheck those value predicates before reusing geometry.
Never cache an FL ancestry verdict beyond its invocation merely because roots
or handles match. No per-draw native reads are needed for either approach.

## 5. Cost, evidence and remaining acceptance

Inferred cost: one selected cockpit bucket per demanded material invocation,
one extra target word and root/handle copy, one shared ancestry walk (<=16 nodes)
and one union identity-lock acquisition (<=18 nodes). Relative to the existing
own-only path with a valid target, the native additions are at most **three DWORD
loads / twelve copied bytes**: cockpit target, target root and root handle; an
already-cached shared root handle avoids the last load. No target ID read is
needed for the invocation-local B3D identity. An eighteenth `OwnedNode` plus
parent-cache word adds **20 bytes** to x86 local batch scratch (4-byte address,
4-byte handle, 8-byte serial, 4-byte parent). A target result matching OwnShip
adds approximately **20–24 bytes** to both owned Record and Copy scratch, plus
the small strict-absence/demand flags; exact padding and stack reservation must
be checked in the actual x86 compiler. These byte counts are layout-derived, not
measured generated-code costs. This replaces duplicated
own/target walks; it is not a timing measurement. If props is the only demand,
consider using the exact owned classification to avoid non-prop root work only
if the complete native-copy borrow stays explicit; do not introduce an unsafe
late read through a record token as an optimization. Per draw is a route-local
owned sample, snapshot/slot lookup, existing extent probe and corner projection;
no allocation, name walk, fresh native observer or OS page query. Measure actual
material frequency and cold load/name cost in the implementation's existing
fixtures/user-run work, not a separate broad benchmark.

Reproduction (offline, original image):

```sh
python3 verification/results/fl-port/small-props-inputs/verify.py
python3 -m py_compile verification/results/fl-port/small-props-inputs/verify.py
```

Measured static result: **5 regions, 285 selected instructions, 31 exact operand
anchors, 10 selected boundary edges and 31 altered-operand negatives**. The
producer uses project `inspect_x3.PE32` and Capstone, checks original-image
identity, decodes from containing native entries and independently checks each
selected start/end. Setter decode ends before its jump table. Negative tests
bypass image identity. Its parsed `report.json`, producing script and raw local
exploration are ignored under `verification/results/fl-port/small-props-inputs/`.
This is a selected field/ABI/lifetime extension, not a whole-writer census or
proof against arbitrary computed native/foreign mutation. It reuses existing
own-ship, material, load and callback closure evidence. No new hook is proposed.

Implementation acceptance still needs target/root absence versus unavailable,
own/target equal and distinct, root-not-drawn descendants, union capacity18,
reparent/detach with unchanged registry revision, serial/address reuse, target
replacement/retirement, exact props versus other occlusion parts, named aliases,
registration/restore/name revision, nested scopes/unwind and unchanged CPU/error/
FP state. Retain the base sample when optional data fails. Check props-on with
occlusion/adaptive/nozzles all off. Check early FL camera denial and memo revocation
on same-frame identity/class/exemption changes. Inspect the final compiled copy
for no calls and stack bounds. Native Windows runtime behavior and FL game/
visual/performance acceptance remain unverified.
