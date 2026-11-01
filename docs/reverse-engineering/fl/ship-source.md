# FL loaded ship source records and bounded reader joins

2026-10-30. Focused follow-up to the
[ship-profile proposal](../../architecture/farnhams-legacy-ship-profile.md),
[enclosure numerical contract](ship-enclosure.md), and
[copied-value core](../../../src/engine/fl/ship_enclosure.h). This establishes
native layouts and necessary eligibility checks; it does not implement or
authorize a live reader. Current node pose, mesh name, and the core's default
policy fields are not independent source authority.

The [producer](../../../verification/results/fl-ship-source/verify.py) and
[receipt](../../../verification/results/fl-ship-source/evidence.json) read the
original executable through the project PE/Capstone tools. Reproduce with:

```sh
python3 verification/results/fl-ship-source/verify.py
python3 -m py_compile verification/results/fl-ship-source/verify.py
```

Ghidra used the existing read-only `FLRegistry/X3FL.exe` project and project
scripts `X3DecompileFunctions.java`, `X3FunctionBody.java`, and `X3XrefsTo.java`.
Raw output remains local in `~/.cache/x3m-port/fl-ship-source/`. No PE/native
execution, Wine, game, build, install or production change occurred. Synthetic
native-layout records below are host policy witnesses, not captured loaded
records or an implemented reader.

## 1. Source join is scene ID, array index, then a counted part

`4aa750` publishes scene ID at node `+258` (`4aa76d`), source **array index**
at `+25c` (`4aa77a`), and source part `+38` at node `+260` (`4aa787/791`).
Initial binding `4aa950` does the same and calls `4a98b0` synchronously.
The updater calculates `scene.parts + node.index*0x58` at `4a98d0/4a98d9`.

The read-only lookup half of allocating loader `4aeaf0` supplies the exact
existing scene-cache join; **do not call the loader**. Read engine global
`5aa100`, hash owner `[engine+80]`, bucket array `owner+0`, capacity `owner+4`,
and bucket `(scene_id & (capacity-1))`. Links are `{next+0,key+4,payload+8}`.
Check a bounded complete chain, exact key, nonzero payload, and
`payload.scene_id == node.scene_id`. A miss is unknown, not permission to load.
`4aeb1d/36/43` check those cache fields; `4aa8d0..4aa910` independently repeats
the same join for source events. Validate capacity/checked spans, chain cycles
and the existing engine/borrow tokens; readable memory does not create a lease.

| Loaded record | Field | Evidence |
| --- | --- | --- |
| Scene, 0x60 bytes | `+0` signed scene ID; `+4` signed part count | Loader allocations/publication `4acf60`; cache join above |
| Scene | `+50` part-array pointer, `+54` scene transform flags | `4a98d9`, `4a8a24`, `451034..45104b` |
| Part, stride 0x58 | `+0/+4` position count/pointer; `+8/+c` rotation count/pointer | `4ac1a0` copies independently allocated arrays; `4a98b0` consumes them |
| Part | `+10/+14` look-at count/pointer; `+18/+1c` roll; `+20/+24` colour; `+28/+2c` alpha | `4a8aa0` and `4a98b0` consume these six separate channels |
| Part | `+30` requested model/dummy ID; `+34` authored P number | `446780`, `4a2584`, `450ff2` |
| Part | `+38` automatic animation controls; `+3c` authored parent P | `4aa787`, `4646b6..4649c7`, `44674f` |
| Part | `+44/+48` event count/array, event stride 8 | `4aa913..4aa93c -> 4aacb0` |
| Position/rotation key, stride 0x50 | `+0` time; `+4` raw flags; `+20..28` vector/angle | `4ac750`, `4a8aa0`, `4a98b0` |
| Finalized rotation key | `+40..4c` loaded `(w,x,y,z)` Q16 quaternion | `4ab5b0..4ab64d`, `4a9d43..4a9d54` |

Counts are signed native values; bound and check them before multiplication
or dereference. Copy every key in each admitted array, not only the currently
selected key. Position/rotation keys are 80 bytes, not the part's 88 bytes.
The three other scalar/vector channel strides are separately 12/20/12 bytes;
the initial bounded policy refuses their nonzero counts, plus look-at/roll.

**Node `+260` is not `source_channels=0x2002`.** It is copied from part `+38`
and the simulation consumes it as automatic animation-control bits. Preserve
and compare that raw value separately. Source channel policy must be derived
from the independently copied channel arrays and their key flags.

The source array index is also **not the authored P number**. Parent resolution
`4a2550` finds a node in the same source scene whose indexed part's `+34`
matches the requested authored P (`4a2584`). `446720` reads authored parent
`+3c` and reparents after child construction. An adapter must resolve these
author numbers to its own parent-before-child indices; stock sequential
numbers cannot justify assuming equality for ordinary other scenes.

## 2. Loaded quaternion flags differ from authored flags

`4ac750` splits authored keys into independently counted arrays and retains
their flags. Finalization `4ac1a0` ends by jumping to shared tail
`4ab5b0..4ab64d`; this discontiguous function body matters. For rotation keys
whose `(flags & 0x26)==2`, the tail builds/normalizes the quaternion through
`50fde0`, carries the relative-rotation state when `0x2000` is absent, and
**ORs flag 4 into every rotation key** at `4ab637/4ab63a`. Thus ordinary
authored `0x2002` has loaded position flags **0x2002** and loaded rotation
flags **0x2006**. Reject unfinished/raw rotation `0x2002` for this loaded
policy; do not indiscriminately clear flag 4 on arbitrary modes.

A qualified canonical policy can require exact position `0x2002`, exact
rotation `0x2006`, zero unsupported channel/event counts, and all supported
key values equal. Only then can it represent this authored policy as the
core's `source_channels=0x2002` and `quaternion_mode=2`. Retain raw flags,
counts, times and values in exact stamps. These core fields are qualified
policy values, not aliases for a native node word.

Current node quaternion mode is **node `+1f4` bit 0x10**. Native quaternion
publication at `4a9d43/51/54` copies key `+40` to node `+200`, rebuilds local
basis and sets that bit at `4a9d59`; Euler paths clear it. Validate the current
mode and quaternion against the independently copied source. Repeated exact
identity keys retain the existing bounded interpolation proof. Repeated
nonidentity interpolated keys are not covered; admit one constant nonidentity
key only, with the copied loaded quaternion and its proven writer result.

There is **no independent loaded per-key matrix field**. For a constant key,
derive `Part.basis` from the source quaternion with the exact integer
`50f4e0` formula, rather than copying `Node.local_basis` into both arrays.
For `r(a,b)=floor((a*b+32768)/65536)`, `(w,x,y,z)` produces rows:

```text
(Q-2(r(z,z)+r(y,y)), 2(r(x,y)-r(w,z)), 2(r(x,z)+r(w,y)))
(2(r(x,y)+r(w,z)), Q-2(r(z,z)+r(x,x)), 2(r(y,z)-r(w,x)))
(2(r(x,z)-r(w,y)), 2(r(y,z)+r(w,x)), Q-2(r(y,y)+r(x,x)))
```

Use checked integer intermediates and validate the derived basis domain. This
is a small pure calculation, not a native helper call or runtime vertex scan.
The current node basis remains an independent publication check. Identity and
rotor authored quaternions must be exact +/-identity under their existing
rules; single nonidentity keys require the existing local Frobenius bound.

## 3. Construction, scale and source lifetime

Builder `446780` consumes one source part, attaches its new node, calls
`4aa950` at `446999`, and sets node `+196` bit 1 at `4469a6` **before** body
assignment and recursive substituted children. That bit is not a construction
completion seal. `445f80` searches five dummy tables by requested ID, returning
a 0x60-byte row and kind; `4469cd -> 445f80`, `446a02 -> 4aeaf0`, and
`446bca -> 446780` build substituted scenes synchronously. `446720` repairs
authored parent links afterward. Failed construction/body loads can leave
partial trees or default model -1/scale zero. A current child count alone is
not source completeness, and no dedicated completed-construction bit was
established by this study.

Useful admission remains possible without a registry: under the existing
completed owner borrow, resolve each distinct source scene, inspect its full
bounded part set, model/dummy branch, authored parent links and every applicable
substitution, then reconcile them against the complete current tree. Refuse
unsupported special kinds, incomplete/missing model-bearing parts, unresolved
parents, duplicates or excess budgets. The previously qualified helper role
does not grant arbitrary failed model assignments a nonbody exemption. Root
policy comes from the existing physical-root owner and normalized writer;
do not invent a source key for its model-zero body.

The independent body-scale input is the current joined model's loaded LOD0
source: body setter `4a365d..4a366b` stores requested model ID then reads
`model+0c -> LOD pointer array[0] -> LOD+14` into node `+70`; axes are reset
to Q. Model membership/requested-ID qualification remains the existing hash
join. LOD selection does not itself rewrite node raw scale. However two
ordinary paths defeat inferring future constant scale from current equality:

* `464664` tests node `+12c & 0x800`; `4646a1` invokes recursive scale setter
  `4a3920`, which rescales descendants and writes raw `+70` plus unity axes.
* `4646a6` tests node `+130 & 1`; `4649be` invokes `4a3ae0` with a varying Z
  axis. It changes axes and propagates to children with offset-scale bit 0x40.
  Classification `43de93..43deb3` enables this mode from body-ID table
  `5a8314`, setting node `+130 |=0x4000001`.

The bounded ordinary adapter must exclude these writer modes through their
actual source/classification joins and current state, including ancestors,
or qualify their behavior separately. The present core's field equality and
flags mask do not establish that eligibility. This is a structural per-read
condition, not a ship-class or name rule. Arbitrary script setter closure is
not established; the enclosing accepted ordinary writer scope is still needed.

Loaded part/key arrays are separately allocated during parsing and are read
by ordinary constant-key replay. They are not immortal: scene destructor
`4aeeb0..4af19f` removes the scene hash entry and frees these arrays; cache
clearer `4af1a0` walks `[engine+80]`. It is called by common cleanup `48a894`,
by `48aa95`, and by `4a1e76` in the native cache-clear path. Therefore name
revision alone is not a source-payload lease. Use only a synchronous borrow
that excludes those native cleanup/load callbacks, release every pointer
before continuation, and rejoin later reads. Existing lifetime/resource
owners must explicitly cover these spans; this note creates no new authority.

## 4. Complete turret selection and event/scene controls

Gun dummy rows live at `*[5a8488]`, count `[5a83c0]`, stride **0x60**. Each
row has requested dummy ID `+0`, flags `+4`, substituted scene ID `+c`, and
three `(model ID, rotation flags)` slots at `+20/+24`, `+28/+2c`, `+30/+34`.
The updater scans **all** matching rows (`450f60..4513c6`), all source parts
of the selected scene in reverse, and all three slots (`451075/45107f`,
`45137b` adds eight). It then locates a live mesh by requested model ID at
`4510a7`. A first-match dummy lookup or unique current mesh name is insufficient;
duplicate rows/slots and every applicable selected writer must be accounted for.
The ship's selected gun-instance/type relation (`44c010`, source/type fields
around `450ef6..450f35`) is an additional join, not supplied by matching a
node's model ID. Copy that actual selected relation or conservatively consider
every applicable candidate; do not claim this receipt supplies a live selector.

Require eligible dummy bit 2, source identity quaternion, no flag-4/gamma
slot, and no conflicting applicable slot. The native quaternion branch at
`451252/451269` and no-gamma rebuild remain as qualified in the enclosure note.
The routine changes an existing mesh attitude; its observed scene-loader call
can populate a **resource cache**, and is not evidence that new instantiated
ship children are deferred into this update.

There is also an ordinary **future scene-transform writer**, even when the
current scene flag is zero. If any part's requested model `+30` equals the
selected dummy ID, `450ff2..45104e` writes the scene translation to minus that
part's first position and sets scene `+54` bit 1, with identity scene basis.
`4a8a10` later applies this transform to local position/basis. A cheap immutable
exclusion is to inspect the complete selected scene part set and prove **no
such requested-ID equality** for every applicable selected dummy row. This
uses IDs and complete records, not names. Otherwise refuse the ordinary
untransformed profile or separately qualify the actual nonzero transform.

For events, require source part `+44` count zero, not merely current node
`+1f4 & 0x40 ==0`. `4aa8c0..4aa94d` independently scans source event records
and calls `4aacb0`; that function can dispatch script events and enable camera
target bit `0x1000`. Also require the existing no-completion-callback/no-target
node modes, and exclude unsupported scene/source controls. The full source
writer contains an indirect callback at `4aa737`; it is not generally
reentrant or safe to call while borrowing. The adapter calls none of these
functions. Existing CPU/LastError/MXCSR and foreign-exception refusal contracts
remain unchanged; no new hook boundary, stack or SEH authority is introduced.

## 5. Bounded checkpoint and remaining evidence

The host record witnesses admit three necessary-layout cases (generic identity,
one constant nonidentity quaternion, identity no-gamma rotor) and reject twelve
changes, including unprocessed quaternion flags, source position changes,
both varying-scale controls, source events without a current callback flag,
future scene-transform ID equality, and gamma in a duplicate gun row. They
have no current pose input, so source admission cannot be made tautological.
They do not prove complete live selector, construction or lifetime authority.

Proposed finite scratch budgets are 256 live nodes, 32 distinct source scenes,
512 distinct source parts, 2048 total copied keys and 512 total hash links,
with overflow refusing. These are proposed limits, not measured runtime needs.
Native node copies total at most 159744 bytes; source parts add 45056 bytes,
and 80-byte keys at most 163840 bytes, before headers/stamps/selector records.
Within one borrow, reuse exact copied repeated scene/key records; do not add
a persistent registry. The existing stock source receipt has three distinct
scenes and 41 source parts; its authored key count is recorded separately from
the live expanded tree. Intended cost is bounded copies plus scalar checks,
with no runtime vertex/triangle scan, file access, allocation or game callback.

Before implementation admission, still bind the completed owner borrow to
these source payload lifetimes; finish the actual selected gun-instance/type
join and prove its stability over the admitted interval; reconcile complete
source construction/substitution against the live tree, including partial
failure and helper roles; and implement exact raw-stamp/canonical-policy and
source-quaternion matrix derivation. The scale/classification and future scene
writer exclusions above must be exercised by adapter refusal witnesses.
No blanket ban on ordinary ships or M2-only roster is implied. Native-record
capture, complete live reader, collision corroboration and flight remain
unperformed.

Independent review reproduced all 61 anchors, 5,825 instructions and the
3 positive / 12 negative synthetic witnesses, and inspected the relevant
instruction contracts. It found no substantive errors in this bounded
checkpoint. The open joins above remain prerequisites; this review does not
grant live-reader authority or qualify native execution.
