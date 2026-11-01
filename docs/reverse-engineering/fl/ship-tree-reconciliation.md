# FL loaded ship source and complete tree reconciliation

2026-10-30. Bounded follow-up to [loaded source records](ship-source.md),
[turret membership](ship-turret-selection.md), and
[source lifetime](ship-source-lifetime.md). These findings concern
construction completeness for a synchronous reader. They do not replace the
accepted key, scale, gamma, scene-transform, event, owner or 900 ms numerical
conditions. Existing reviewed notes and production files were left untouched.

**Result:** a complete source-driven expected graph can be reconciled against
the finite live graph without a generic construction-completed bit or an
observer. A useful first subset admits ordinary parts and single-scene gun
substitutions, unique backward authored parents, and independently qualified
nonbody helpers. The stock M4 and M2 structures fit this subset and the existing
256-node limit. Loaded native records have not been captured; these are static
construction/resource controls, not proof that their current live writer modes
pass every reader condition.

The [producer](../../../verification/results/fl-ship-tree-reconciliation/verify.py)
and [receipt](../../../verification/results/fl-ship-tree-reconciliation/evidence.json)
are reproducible with:

```sh
python3 verification/results/fl-ship-tree-reconciliation/verify.py
python3 -m py_compile verification/results/fl-ship-tree-reconciliation/verify.py
```

The producer checks 57 instruction anchors across 1,415 instructions, models
30 admission/refusal witnesses, and reads twelve effective resource inputs.
Ghidra used the existing read-only FLRegistry project and project analysis
scripts. Raw output is local under `~/.cache/x3m-port/fl-ship-tree-reconciliation/`.
No PE/native execution, Wine, game, build, install, commit or production change
occurred. The producer's symbolic resource identities corroborate stock
structure; a live reader must join actual numeric IDs through the accepted name,
model-cache, scene-cache and resource-generation contracts.

## 1. Construction inputs and native branches

`45a140` supplies actual ship `+98` as the main scene ID (`45a150`) and ship
`+70` as the physical root (`45a147`) to `4463f0`. This is an independent
construction descriptor; choosing a scene from the current child nodes is not
equivalent. A first ordinary reader requires that descriptor to be positive,
resolves its complete already-loaded scene, and qualifies the physical root
through its existing object/body owner. No allocating scene/model lookup is
called. Alternate attachment/docking builders are outside this first subset.

| Routine | ABI and established behavior |
| --- | --- |
| Main scene builder `4463f0..44671e` | Five stack arguments, `ret 0x14`; the ordinary ship entry supplies scene ID, existing root, context, ship, zero preload argument. Iterates source array order. |
| Part builder `446780..446ec3` | Native context in ECX, six stack arguments `(parent, scene, array_index, ship, preload, animation_collector)`, `ret 0x18`, node/null in EAX. The unsupported inactive-effect branch can return the incoming ECX context rather than a newly allocated node. |
| Dummy selector `445f80..445fe7` | Stack `(requested_id, kind_filter, out_kind)`, `ret 0xc`; EAX is row/null. Construction uses kind filter −1. |
| Parent repair `446720..44677d` | Stack `(owner, scene)`, `ret 8`; processes a saved-next traversal of direct children. |
| Authored-P lookup `4a2550..4a25af` | Stack `(owner, scene, authored_P)`, `ret 0xc`; depth-first descendants, first matching scene ID and indexed source part's authored P. It does not test the owner itself. |
| Attach `4a57d0..4a5858` | ECX is node, stack argument is new parent. Unlinks, appends to the parent's child-list sentinel, stores node `+18`, and updates binding/cache state. It does not convert the copied local transform to a new coordinate frame. |

The part builder reads requested model/dummy at part `+30`, authored parent P
at `+3c`, binds scene/index/source controls through `4aa950`, and then attempts
body/subscene work. Its early node `+196 |=1` is not completion evidence.
The ordinary native branch decisions are:

1. Requested ID −1 creates no node. ID −2 takes a special effect constructor;
   IDs 1000..1999 use a mutable engine effect table and may return the incoming
   context when disabled. Refuse those special constructors in this subset.
2. For other IDs, scan the complete counted ID array at `*[5a831c]`:
   first DWORD count, following DWORDs requested IDs. A match returns null at
   `446948 ->4467a5`, before ordinary node allocation. This is an actual
   BodyDummies omission, not a model −1 node inferred from its name.
   The special −2 and 1000..1999 refusals precede this omission even when
   the same ID also appears in BodyDummies; two intersecting negative controls
   exercise that native branch order.
3. Otherwise `446952 ->4a2200` allocates an ordinary node, resolves an already
   available authored parent if requested, attaches it, and publishes its source
   binding. An allocation failure returns null. Positive body request is attempted
   later at `446ab8..446abf`; its Boolean result is not used to remove the node.
4. `445f80` scans five tables in kind order, then row order, returning the first
   equal requested ID. Pointer base `5a847c`, signed-count base `5a83b4`, row
   stride `0x60`. Read all five complete bounded tables before declaring no
   substitution; a missing/capped table is not a proven nonmatch.

The selected row has state count `+8` and scene entries at `+c`. The immutable
choice-index table at `578cec` is `{0,0,3,0,0}` for kinds 0..4; construction's
kind-mask table `578cd8` is `{0x40,0x20,4,8,0x20}`. Kind 2 therefore selects
entry 3, not entry zero. Kind 0 flags, kind 1, kind 2 doorway and kind 4
connection paths have distinct suppression/collector behavior. The minimal
policy refuses their occurrence rather than pretending they are gun rows.
It also refuses duplicate applicable dummy IDs across or within tables, zero
or multiple gun states, absent selected scenes, recursive substitutions, and
unsupported source controls. This is a structural policy, not a ship roster.

For kind 3 with exactly one state and a nonzero scene ID, create the wrapper
and recursively expand its complete selected scene in source-array order.
Part ID −1 is skipped. A child part whose requested model equals the wrapper's
requested dummy is also skipped and causes `446bad ->4af240` scene centering.
The untransformed policy refuses that equality. All remaining parts take the
same builder, and `446d4f ->446720` repairs the subscene's authored parents.
Failed loads can leave the wrapper without some or all expected children;
the complete expected graph must reject that result.

## 2. Physical root, wrapper bodies and helpers are different roles

The existing physical root may have **model ID zero**, positive raw scale,
and an ordinary loaded body. Model zero is not absence. Include it once using
the existing signed16 fallback (stock raw scale 47 gives radius 163), actual
body/scale joins and normalized-root writer policy. It has no invented source
quaternion or part-zero key. Require its actual native parent pointer null;
an attached ship root cannot be canonicalized into an independent root.
Main construction publishes its skipped matching
source index at root `+25c` (`4466f3`); for physical model zero the test at
`4465a7/5a9` always builds scene parts instead of treating a matching requested
zero as the root. With no skipped root part, `+25c` is −1. Main construction
does not publish root `+258` through `4aa950`. Preserve those raw words in
stamps; any canonical no-source root fields and root-motion policy must be
explicitly distinguished from the child source-record layout.

A source part requesting zero is another case: part-builder body assignment
requires **strictly positive** ID, so it does not get physical root semantics.
Refuse such parts in this minimal subset. A nonzero physical root whose model
appears in the main source invokes a distinct skip before the part builder
(`4465a7..4465b5`), followed by main-scene centering (`446685..4466d2`).
The source expansion explicitly refuses this equality before consulting
BodyDummies or taking part-constructor branches. Negative controls cover the
matching root both with and without an intersecting BodyDummies entry.

**Correction to the earlier broad gun-wrapper reconstruction:** `446a57`
zeros the wrapper's body request only for kind 1, or kind 0 with nonzero low
flag word. Kind 3 takes `446a48 ->446b42` and retains its positive requested
dummy ID until `446abf`. A rotating gun row subsequently sets node collision
exclusion `+12c |=0x100000` at `446b29`, but this does not undo successful
model assignment or turn it into a model −1 helper. Both stock big-turret dummy
body assets exist: LOD0 scales 17920 and 20692. A conservative reader includes
successful wrapper bodies, even though their native collision path excludes
them. The core's helper/excluded roles require model −1 and scale zero, so
putting a successful wrapper in either role would violate its contract.

The constructor initializes model −1 at `4a22cc`, scale zero at `4a2308`,
and own radius zero at `4a22fe`. `4a35e0` returns false before publishing a
model/scale when the model resolver fails (`4a3603..4a3616`). Therefore
**model −1 / zero scale is also an ordinary failed-body state**. An ordinary
required body that ends there refuses; it is not an automatic helper exemption.

The stock substituted part numbered `B 100001` is such a positive-ID source
request, not a distinct native loaded nonbody-kind field. Numeric conversion
`486c60` returns a parsed number unchanged (`486d13..486d28`). Consequently
the number alone, an effect-looking name, current flags or an out-of-domain
name slot does not prove permanent meshlessness: `4a1a40` can fall back to
loading `v\\100001`. A useful precise helper predicate is:

* independently prepared absence of `objects/v/100001` for the native
  `pbb,bob,pbd,bod` request under the current cold resource generation;
* complete current native model hash proves that exact requested ID absent;
* the ID is outside the complete current indexed-name domain, so it cannot
  alias a named body: native slot is `id−20000+fixed_count` for IDs ≥20000,
  and the domain is `[0,fixed_count+dynamic_count)`;
* the exact expected helper node is present with source scene/index/parent,
  model −1, scale zero and no body contribution, with complete descendants
  and the same qualified key/ordinary writer conditions as other nodes.

Use checked wide arithmetic for the name-domain test and distinguish a proven
hash miss from unavailable/corrupt/capped lookup. If a helper resolves to a real
loaded body, include it using its actual requested-ID/scale/source joins and
fallback radius, or refuse; do not retain the nonbody classification. A failed
ordinary hull cannot acquire this exemption from its current node values.

This needs **no new negative-resource proof mechanism**. Existing
[`native::Transaction`](../../../src/engine/fl/gate_clearance_native.h) already
selects an unselected `Winner`, accepts its absent payload, and rechecks recorded
absences against loose and mounted winners after hashing. The concrete remaining
integration is to include this specific helper request in the existing prepared
ship inputs and retain its qualified absent result, then combine it with the
current slot/hash checks. The offline control measures that absence; the current
gate-only prepared publication does not yet export this ship-helper fact.

Unlike earlier permissive source-only upper counts, the proposed reconciliation
requires **every builder-created helper node present**. A missing helper can be
allocation failure, not native intentional omission. Only the actual ID −1 and
complete BodyDummies branches above authorize absence in this subset.

## 3. Parent repair and a finite independent expected graph

Authored P (`part+34`) and source array index are separate. Parent lookup
recursively scans current descendants for the same scene ID and then compares
the indexed source part's authored P. It can find nodes that were reparented
under earlier parts; it is not a flat immediate-child search. Duplicate authored
P values, a skipped/missing parent, self-reference, cycles and a same-scene
recursive substitution can make a general replay ambiguous or unsafe. Refuse
them before trying to match native memory.

Part construction's first parent lookup sees only already constructed nodes.
Subscene repair walks each then-direct child with next saved at `446739`,
resolves `part+3c` against the owner's descendants, and appends it under the
result through `4a57d0`. The node's local offset/basis remain the source-local
values; repair is not a world-pose-preserving reparent operation. **Main scene
builder `4463f0` never calls this repair**; the only measured direct call is
`446d4f` inside recursive substitution. Thus a top-level forward reference can
remain under the root even when a later authored parent exists.

A minimal deterministic reader need not implement every native first-match
case. Require unique authored P values in each scene, and each nonnegative
authored parent to name an earlier, instantiated ordinary part in the same
scene instance. Reject unresolved/forward parents in both main and substituted
scenes. This still supports nested parents and nonsequential authored numbers;
source array equality is never assumed. Since every admitted parent already
exists uniquely, initial attachment and the later subscene repair select the
same expected parent. Extra transient native attachment order cannot conceal a
missing model or introduce a new expected node.

Construct the expected graph **from the complete source descriptor and tables**:

1. Add the independently qualified physical root. Resolve every distinct source
   scene once, copy every counted part and all admitted keys/channels, and copy
   complete dummy/BodyDummies tables under the accepted navigation borrow.
2. Expand every source scene occurrence, applying the actual skip branches and
   strict substitutions above. Resolve parent P through an independent
   per-instance authored-P map. Mark bodies, wrappers and independently proven
   helpers before examining matching native model/pose values.
3. Identify an occurrence by its **source instance path**, scene ID and array
   index. The path is the chain of substituting wrapper-part occurrences from
   the physical root. `(scene ID,index)` alone is not unique: the stock M2 has
   24 instances of two turret source scenes. Reject recursive scene paths.
4. Independently walk every actual child/sibling list through its real sentinel,
   checking predecessor/successor, parent, cycles, pointer arithmetic and caps.
   Do not skip an excluded parent. Match exact scene/index/requested-model
   obligations within each expected occurrence and expected owner/parent;
   duplicates, unmatched nodes or missing expected nodes refuse. Never use a
   first model-name match as source completeness. Native DFS/list order may
   differ from source creation order: reorder copied values into the matched
   parent-before-child profile order for `ship::enclose`.
5. Validate actual body/cache/name/scale joins, independent quaternion-derived
   source bases, source controls and conservative all-applicable turret rows.
   Preserve exact raw stamps alongside qualified canonical policy fields.
   Then use the existing current-publication checks, all-node no-wrap interval
   guards, final borrow/resource/name checks, and erase transient pointers.

The model tests source-led graph coverage independently of current node values,
including changed/missing ordinary models, duplicates, wrong parents, an extra
node, a source-part-zero false root, source zero/effect constructors, unsupported
substitutions, centering, helper-fact withdrawal and truncation. The graph
fixtures and Boolean helper-prerequisite self-controls test the proposed
host policy only. They do not validate a live-memory reader, execute the native
branches, or establish actual borrow/helper admission. The accepted
ordinary source replay and turret paths change existing local state; in this
bounded constructor cone there is no queue promising later instance children.
The selected gun updater's resource-cache load is not deferred instantiation.
The actual navigation owner excludes synchronous construction/free/reload
interleaving during this call-free copy. This is not a guarantee against arbitrary
script setters or unsupported attachment modes over the later interval.

## 4. Measured controls, bounded work and remaining integration

| Stock source control | M4 | M2 |
| --- | ---: | ---: |
| Distinct scenes / unique parts | 1 / 25 | 3 / 41 |
| Expanded part visits | 25 | 151 |
| Scene occurrences / maximum source depth | 1 / 1 | 25 / 2 |
| Expected nodes | 26 | 146 |
| Conservative model-bearing bodies | 26 | 122 |
| Independently empty helper nodes | 0 | 24 |
| Intentionally omitted BodyDummies parts | 0 | 6 |
| Node / unique-part copy bytes | 16224 / 2200 | 91104 / 3608 |
| Simple all-row membership comparisons | 1725 | 10005 |

Counts are inferred by the static model from measured effective sources and
the native branches above, not live captures. Five dummy categories have
22/8/12/26/1 rows, 69 total (6624 bytes); BodyDummies contains one ID (8 bytes
including count). Both controls have no F-parent references and use only the
supported gun substitution structure where applicable. The M2 includes root,
hull, 24 successfully model-bearing dummy wrappers and 96 substituted meshes,
plus 24 helpers. Its six camera parts take the actual BodyDummies omission.
The earlier 152-node upper arithmetic witness included all six cameras; this
receipt makes their native absence explicit.

Conservatively adding signed16 spheres for the 24 wrapper bodies gives maximum
wrapper bounds **840868** at root norm `113514/65536`, or **970929** at root
norm 2. Both remain below the earlier full ship radii **1024872 / 1183395**,
so those full radii and their ten outward placement controls remain valid for
these added bodies. Existing all-node center bounds already cover wrapper
positions and a superset of these nodes. Reserve **8224** is unchanged. No
vertex-derived tightening or smaller numerical allowance was used.

Proposed explicit caps: 256 actual/expected nodes, 32 distinct scenes, 512
unique source parts, 512 expanded part visits, 2048 copied keys, 512 total
dummy rows, 512 BodyDummies IDs, 512 hash links, and depth 16. Overflow refuses.
Node/part/key/table/BodyDummies copies total at most **419844 bytes**, before
headers, names, stamps and graph scratch. Linear membership costs at most
262144 row comparisons; an owner-local temporary index can reduce that if
needed without a persistent registry. Stock controls use 1725/10005 comparisons.
There is no runtime vertex/triangle scan or disk/resource callback in the live
copy. Static host timing is recorded in the receipt and is not game timing.

There is no remaining unknown native construction-completion flag to find for
this strict subset. The next reader must implement the explicit graph and
sentinel checks, match actual numeric joins, publish the small helper absence
through the existing cold transaction, and exercise live-layout refusal cases.
Current E navigation borrowing supplies lifetime authority; it does not itself
qualify this graph. Source-key/scale/writer exclusions remain the prior notes'
conditions, so structural M4/M2 controls are not claims of complete live
admission. Native collision corroboration and user flight remain unperformed.
