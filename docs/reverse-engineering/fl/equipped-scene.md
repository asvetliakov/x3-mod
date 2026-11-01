# FL ordinary equipped M5 scene identity

2026-10-31. Bounded static follow-up to the [full stock scene
fixture](../../verification/farnhams-legacy-arrival-body-bank.md#complete-unarmed-m5-scene-preparation),
[source tree construction](ship-tree-reconciliation.md), and
[gun selector](ship-turret-selection.md). No production change,
build, native execution, Wine, game, install or commit occurred.

**Result:** the inspected ordinary class-8 laser installation path does not
replace the stock M5's weapon dummy or append equipment geometry to its source
tree. It links a separate simulation equipment object to the ship, records its
slot, and clears `node+0x12c` bit `0x100000` on the existing source-selected
node. Removal sets that bit. The existing strict reader already permits either
bit value while retaining the dummy's geometry. A replacement-node framework
is not justified by this path. Execution of these original paths and live FL
qualification remain open; this is not blanket admission of arbitrary equipment,
turret, script-generated or additional-child graphs.

## 1. Source and type identity

All addresses below are preferred VAs, image base `0x400000`, for original
`/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Project `PE32`/Capstone tools decoded the actual bytes; read-only Ghidra
`verification/results/fl-port/registry-writers/ghidra/FLRegistry`, program
`X3FL.exe`, supplied function boundaries and cross-references. Raw listings and
decompilations stay untracked at `/tmp/fl-equipped-re/`. The scratch queries use
`verification/probe/compare_fl_engine.py::{PE32,MD,read}` and existing
`tools/analysis/X3{DecompileFunctions,XrefsTo,FunctionContext}.java`.

The effective stock resource reader and existing
`verification/results/fl-arrival-entry/occupancy_full_scene.py::load` resolve
`SS_SH_A_M5` to the already ledgered 23-part, 24-node source scene. Four parts
request `SHIPS/PROPS/WEAPONDUMMY`; all are direct children, authored parent -1:

| Physical gun slot | TShips primary source array index | Authored P | Requested body |
| --- | ---: | ---: | --- |
| 1 | 3 | 5 | WEAPONDUMMY |
| 2 | 5 | 7 | WEAPONDUMMY |
| 3 | 6 | 8 | WEAPONDUMMY |
| 4 | 4 | 6 | WEAPONDUMMY |

These are measured source joins, not loaded pointer captures. Source-array
indices and authored P numbers are different domains. Each slot has one muzzle;
the secondary model field is empty and its index is -1. There is no applicable
effective Dummies row for this body and no BodyDummies omission. The prepared
fixed symbolic mapping identifies WEAPONDUMMY as 19080, but a live reader must
continue to join actual source IDs, current body/name caches and resource/name
generations instead of trusting this constant.

The actual TShips loader corroborates these fields: group base is initialized
to `type+0x18c` at `441496`; per-gun base advances another `0x18` at `4414eb`.
`441533 ->486c60` resolves the primary model and `44153b` stores it at
`type+0x1a4`; `44153d/441545` read/store its source array index at `+0x1a8`.
`441572/44157a` resolve/store secondary model at `+0x1ac`, and
`44157d/441582` store secondary index at `+0x1b0`. Gun stride is `0x18`
(`441585`), group stride `0x138` (`4415a1`). These are source/type descriptors,
not a selected installed laser subtype.

## 2. What model the equipment object receives

The class-8 constructor dispatch tables at `44b20c/44b25c` select `449b80`.
That branch allocates 0x10 bytes of class data (`449b85 ->4d8fc0`), initializes
its slot word `+4` to zero (`449b98`), and uses the laser subtype to select
`[5a83e8] + subtype*0xdb8` (`449bb7/449bbd`). `449bc2` reads that type row's
first model field into DI. The common ordinary-node branch allocates the
equipment object's own node at `44a824 ->4a2200`, stores it at object `+70`,
sign-extends DI and calls the model setter at `44a842 ->4a35e0`.

The type loader selects TLaser at `4404b5/4404b7`; its common first-field
resolver `44068e ->486c60` publishes the model at row `+0` (`440693`). All
**39** effective stock TLaser rows have numeric first model **27** (measured).
Thus this ordinary stock equipment-object construction selects model 27; it
does not select the source dummy or infer a replacement from its name. The
class-8 finalizer selected by `44b27c/44b2b4` is `44ae58`: it initializes three
angular-rate values and joins the common finalization at `44b044`; it does not
call the ship source builder or publish a source scene/index.

The separate model-27 node is not an extra physical ship body in this path.
Allocator `4a2280` zeroes the entire 0x270-byte node, including parent `+18`
and scene `+1c`, then appends it to the manager's **unattached** list at
`4a23ac..4a23be`. No class-8 constructor branch attaches it to a scene or a
ship parent. Installation from an existing sector/class-1 owner instead
explicitly calls `4a5740(node,node.scene)` at `452e51` before changing inventory
ownership. That detach recursively clears scene membership; a parentless root
is removed from the scene list and appended to the manager unattached list
(`4a5792..4a57b7`), then its scene is cleared at `4a57ba`. A freshly created
ordinary laser already has null scene/parent and needs no such detach.

Afterward `452dd0` never calls scene attach `4a56b0` or parent attach `4a57d0`,
never copies the dummy's transform to that equipment node, and only publishes
the **simulation** owner `equipment+54`. That owner field is not node `+18`.
Consequently ordinary installation establishes no scene membership, parent
transform inheritance or ship collision descendant for the model-27 node.
It is an unattached inventory-object node, not overlooked gun geometry to add
to the 24-node ship source tree. Arbitrary preexisting malformed parent/scene
state or later script attachment is not ratified by this constructor/installation
trace; such an actual extra ship descendant still fails complete reconciliation.

BodyData's named entries 19082 `invisible_weapon` and 19083 `boron_weapon`
therefore do **not** establish an equipment replacement relation. Their use in
other paths remains unproven here. The relevant ordinary source-node body
assignment is still `446abf ->4a35e0`, after binding that source node's scene,
index and controls at `446999 ->4aa950`.

## 3. Installation, selection and removal

The actual simulation-dispatch path at `46c612..46c648` requires a class-8
object, passes the target ship to `452dd0` at `46c620`, stores the requested
slot word at equipment class-data `+4` (`46c635/46c63e`), and invokes
`44c150(ship,slot,1)` at `46c648`. The dispatcher stores the attachment result
through the VM helper before continuing; the inspected tail does not test that
result before updating the slot/flag. Do not infer successful inventory
attachment solely from the flag or slot word.

`452dd0..452f4b` has ECX=equipment, one stack target argument, Boolean EAX and
`ret 4`. It inspects optional Components metadata through call-free lookup
`43df30`, checks a maximum equipment count when available, removes the object
from its old simulation list, inserts it into the target class list and writes
equipment `+54=ship` at `452f20`. The ordinary class-8/ship path modifies
simulation links, not the node child list. Its complete function contains no
call to node allocation `4a2200`, body setter `4a35e0`, source binder `4aa950`,
scene builder `4463f0` or node-parent attachment `4a57d0`. Optional old class-1
ownership instead takes detach/cleanup calls; target ship order 0x18 takes
`46a550` at `452f3b`. Those paths are not a call-free borrow boundary.

`44c150..44c207` has ECX=ship, stack `(physical_slot,enable)`, `ret 8`.
It maps the slot through type group start/count at `+184/+188`, subtracts the
group's start, and calls `44c010(ship,group,index)` at `44c1d0`. That selector
uses class-7 subtype and the primary `(model,array_index)` at `+1a4/+1a8`,
compared against node `+144/+25c` (`44c090..44c0a4`). It examines direct
children, not arbitrary descendants; the secondary search is bypassed for
stock M5 secondary index -1 (`44c0c2..44c0c4`). The existing selector note
owns its root shortcuts, secondary fallback and duplicate-order details.

On a successful selection, nonzero enable clears only the flag bit at
`44c1e0`; zero enable sets it at `44c1f4`. There are no further calls or
model/source/scale/basis/parent writes. Native collision traversal explicitly
skips a node with this bit at `498d7e ->498f6f` (then continues traversal).
Thus collision exclusion is established; calling it purely a render-visibility
bit is not established by this analysis. Either flag value is compatible with
conservatively retaining the actual body in the enclosure.

Ship construction first builds the complete source scene at
`44a9e6 ->45a140 ->4463f0`, then iterates slots 1 through `type+74` and calls
`44c150(slot,0)` at `44aae5`, setting the bit on initially empty gun positions.
The removal/cleanup routine's terminal class-8/ship branch at
`4542bb..4542da` retrieves equipment data `+4` and calls
`44c150(ship,slot,0)`. It changes no dummy model or topology. The surrounding
`454190` routine traverses other objects, calls cleanup/script/VM paths and
can recurse; its body is not a lifetime-safe copy interval.

**Fixture interpretation:** the accepted full-scene payload zeroes each node
before `p::node`, which does not set `+12c`. Its four weapon nodes therefore
model bit-clear state, not the exact initial bit-set publication from the
unarmed constructor. This does not remove any of its 24 bodies or invalidate
the measured conservative enclosure; it does mean that the receipt has not
executed either the native unarmed flag initialization or an equipment writer.

## 4. ABI, lifetime and bounded implementation consequence

The selector has no CALL instructions. `44c150` calls only that selector;
both preserve EBX/EBP/ESI/EDI on paths that use them, permit volatile
EAX/ECX/EDX/flags to change, and contain no FS/SEH, allocation, x87/XMM or
MXCSR work. They return existing node pointers without retaining them.
`452dd0` preserves its used EBX/ESI/EDI and cleans one argument; its exceptional
old-owner/target-order call paths prevent treating the general function as
nonreentrant. No hook is proposed, and no native pointer may outlive the
qualified synchronous owner copy. Existing [source lifetime
admission](ship-source-lifetime.md) still owns completion,
same-thread scheduling/callback closure, Load/close exclusion, table reload and
scene destruction; readable/equal pointer words alone do not supply it.

For this ordinary stock M5 relation, `ship_reader.cpp::qualify` already allows
bit `0x100000` (it is absent from refusal mask `0x10004060|0x800`), stamps the
actual flags, joins requested and loaded model IDs, and includes that model's
body bounds in both states. `tree` still checks every source identity, parent,
sentinel, sibling and complete expected cardinality. No equipment-table scan,
optional child, replacement guess, extra resource cache or production reader
adaptation is needed for the **observed flag-only writer**. This is a static
compatibility conclusion; execution evidence remains required before closing
ordinary equipped-M5 acceptance.

Smallest next fixture: reuse the complete 24-node payload and execute original
`44c010`/`44c150` for each stock slot, with source-derived type descriptors and
ordinary empty/installed/removal states. Snapshot every node and require exactly
one permitted flag-bit change, correct index mapping 3/5/6/4, unchanged source,
model, transform, links and 24-body enclosure. Execute `452dd0` with genuine
modeled simulation-list sentinels, copied Components tables and bounded
previous-owner/target-order choices; verify equipment `+54`, list links and slot
state separately from the source tree. Require the equipment's separately
constructed model-27 node to remain parentless, scene-less and outside every
ship child list; separately exercise actual scene detach when testing a former
sector owner. Cover full/failed attachment and the
dispatcher's independent subsequent flag update. Preserve the existing CPU,
LastError, four-byte-stack, exception/refusal envelope. Keep changed model,
extra child, missing part and stale source/name/resource controls as refusals.
Do not execute the large cleanup routine under fabricated callbacks merely to
obtain a nominal removal result: the original flag writer is the bounded node
operation, with surrounding removal ownership established statically.

Turret source substitutions and attitude updates retain their separately
qualified Dummies/selector contracts; docking/attachment, special constructors,
arbitrary scripts and unknown children remain outside this ordinary case.
Stock resource identity, no unexpected live descendants, actual scheduling
admission and loaded source publication still require integration/live evidence.
No in-game performance or native Windows execution was measured here.
