# FL live gate mapping and ship collision enclosures

2026-10-29, bounded read-only follow-up at parent checkpoint `8275eee4`.
This establishes reader inputs for the reviewed [value math](gate-transform-math.md),
not traffic activation. It reuses [gate transforms](gate-transform.md),
[arrival and width](gate-traffic.md), [avoidance](ship-avoidance.md),
and [body-name lifetime](body-name-lifetime.md). No production,
math, runtime-owner or existing probe file changed. No PE build, native/Wine
execution, game launch or install ran. Preferred FL VAs below use base `400000`;
live readers use the admitted image base plus RVA.

The [producer](../../../verification/results/fl-gate-live-bounds/verify.py) and
[receipt](../../../verification/results/fl-gate-live-bounds/evidence.json) reproduce
96 frozen operand anchors in 16 gap-free regions, 889 instructions, seven
prepared scene mappings / 11 source parts, and two actual stock hull-body
examples. They bind the existing receipts instead of repeating their text-wide
caller censuses. Raw listings remain local under
`~/.cache/x3m-port/fl-gate-live-bounds/`. Reproduce with:

```sh
python3 verification/results/fl-gate-live-bounds/verify.py
python3 -m py_compile verification/results/fl-gate-live-bounds/verify.py
```

## 1. Gate reader: identity before geometry

The live binding is **gate/native ID + root + row + resource generation**, then
root and each prepared part, not one gate-radius ratio. Gate class/type/ID are
`+48/+4a/+8`, root `+70`, scene ID `+98`. Node identity is **`+144`**, not AP
`+140`. Body setter `4a365d` stores the requested integer ID; `4a3663..3682`
copies LOD0 `+14` into raw scale `+70` and initializes all three axes to 65536.
Node scene ID/part index/source flags are `+258/+25c/+260`, published by
`4aa76d/77a/791`. These identify a source part; they are not a borrow or pose
version.

Prepared `NodeProfile.model` is an effective **resource name**. Match it against
the native ID/name relation, with bounded copied strings and exact namespace
preservation. Do not compare it with node `+20` or model `+4c`: the latter is
an optional **NAME inside the body payload**, parsed through `507720` at
`49b0a4`, stored at `49b0cd`, and copied to node `+20` at `4a36a9..36b1`.
It can be absent or unrelated to the selected filename.

Body ID mapping is the existing one, rechecked at `4867b0..6855`:

- Engine `*5aa100`, fixed/dynamic counts `+b4/+b8`, records `+bc`, stride `1c`,
  name pointer `record+c`. Fixed count is 11000 on ordinary completed startup.
- Slot is ID for ID<1000, ID−9000 for ID<20000, otherwise ID−20000+fixed.
  Check nonnegative slot below fixed+dynamic before forming a record address.
- A named slot supplies its full bounded ASCII name. An unnamed valid slot
  formats `v\\%05d` using the **requested ID**, not the canonical slot ID.
  Thus aliases 61 and 9061 share a slot but have different unnamed names.
- Dynamic registration order is not an offline constant. Match actual names;
  do not hardcode stock named-body IDs. Never call allocating getter `4867b0`
  or mutable model lookup `4a1a40` from the injected helper.

Scene identity can similarly be copied, without calling getter `486af0`:
engine fixed/dynamic counts `+c0/+c4`, record array `+c8`, **eight-byte stride**,
name pointer at record+0. ID<50000 maps directly; otherwise ID−50000+fixed.
Check range first. Unnamed names use the requested ID and the getter's
`cut\\%05d` form; named stock scenes use their stored full name. A gate row's
subtype alone does not establish this scene match.

The read-only body-model hash is `[engine+14]`: buckets/count at `+0/+4`,
key ID+1, bucket `key & (count−1)`, entry next/key/model `+0/+4/+8`
(`4a1a80..1aee`). Require a nonzero power-of-two bounded count, 64-link cap,
checked spans and a hit with `model+8 == requested ID` (`49afea` producer).
A miss is unavailable, not permission to load. Model `+60` is not the built
collision LOD: `497ba9` rewrites it even when `+5c` already contains an old
tree. The prepared possible-cache union remains required.

**Name/cache generation limit.** Current body names plus model+8 do not prove
which name was effective when that cached model was loaded. The accepted name
study gives the concrete replacement route: `487740` restore calls dynamic
reset `486450`, can reuse IDs, and can run at playback render tail. Append-only
registration does not change an existing ID/name binding; restore does. Existing
body-metadata snapshots retain scalar classes/flare bits, not the complete
ID/name map, and their public lookup API grants no simulation/resource borrow.
A live adapter needs an owned matching name revision and exclusion of an
intervening reset/restore, or an independently qualified native cache-clear
boundary. A current string reread or registry serial alone cannot repair this.
This study does not prove stale cached geometry actually occurs in normal FL.

## 2. Complete static body mapping and cheap invalidation

Read root as a body **including ID zero**. Prepared root `v\\00000` has raw
scale 47 and a nonempty collision mesh. Root 477 / row 4 remains unknown from
the existing source receipt; it is not reclassified here. All other prepared
root/scene distinctions, including hsap meshless scene with solid root, survive.

Walk the complete bounded descendant list: child head `+c`, sibling `+0`,
parent `+18`, exact sentinel parent+10 with first word zero. Reuse the existing
256-node / depth-three / bounded-sibling policy, with checked overflow and
cycle/refusal handling. The current `gate_tree_read` returns on its first
collision tree; it **cannot** establish profile completeness. Match every
prepared body once by scene ID/part index/name, reject duplicates and unmatched
collision bodies, and verify the expected parent. Do not assume part zero is
the only body. Missing source meshless parts require an explicit nonbody
classification, not a successful solid-body match elsewhere.

Native collision relevance is `(flags+12c & 09000000) == 01000000`
(`4a5f7b..5f93`). For completeness also account for model-bearing nodes whose
tree may not yet be built: no-tree-yet is not permanent meshlessness. A source
meshless body remains a matched source body with its own scale/pose checks. Its
prepared union covers every possible cached LOD; a tree-present flag alone does
not prove that an empty selected mesh gained triangles. Extra effect
nodes may be omitted only when their actual native exclusion is established;
scene ID/part both zero, an effect-looking name, or animation alone is not an
exclusion rule.

Terran source part `100000` is **kind 2, nonbody helper**, not a body-profile
scale/name. ID 100000 is outside the valid stock body-slot domain. A new node
starts with model ID −1, scale zero, own radius zero (`4a22cc/2308/22fe`),
and a body setter failure can leave that state; native scene builder attempts
positive IDs at `446ab8..abf`. Therefore do not demand that a helper node's
model ID equal 100000, or invent a source scale. Match any present helper by
scene/part/parent and require no collision contribution, with complete children
accounted for. If absent, permit absence only for this explicit nonbody source
role after the complete live walk found no substituted/unmatched body. This
receipt does not claim one fixed live helper representation has been captured.

For each ordinary body copy local position `+30..38`, basis rows `+40/+50/+60`,
raw scale `+70`, axes `+80..88`, flags `+12c`, world centre `+b0..b8`, world
basis `+c0/+d0/+e0`. Require exact source scale, unity axes, source static local
offset/identity attitude and no `40` scaled-offset or `10004020` copy-basis mode.
Verify root local/world agreement and child publication using the accepted
rounded transform formulas. Pass **copied world C/raw B** and source body norm
to `part_residual`; retain separate root/scene residual maxima. Never retain
model, LOD, string, vertex or child-buffer pointers as board geometry.

A cached gate certificate needs resource retirement generation, engine/load
identity, current native gate ID, root, row/scene identity, and a **complete
per-part scalar stamp**. There is no established native pose generation word.
Checking only root position/basis/radius misses changed children. On use, compare
the bounded copied part identities/parents/flags/scales/axes/local and world
poses; rebuild residuals only on change. For the 11 authored source parts this
is small. Tree topology changes require a complete bounded rewalk. A root-head
or child-count equality alone does not detect replacement. No per-frame file,
triangle or whole-model-cache scan is needed. Counter/hash equality must not
stand in for exact equality if it authorizes suppression.

## 3. Ship bounds: established inputs and exact rounding witness

Object `+a4` is an existing avoidance **collision radius input**, consumed by
native warn at `457694/69a`; it is not independently proved to enclose all
quantized collision geometry. Node `+a0` is initialized from raw LOD0 scale,
not from the maximum Euclidean norm over all LODs (`4a368f`). Node `+a4` is a
cached **Chebyshev subtree expansion**: seed own `+a0`, recursively maximize
child radius ± each local coordinate; child bit40 applies parent raw scale
with r16 (`4a39b0..3ad3`). It ignores child attitude in that recurrence.

This does not make those values useless. If a complete current subtree really
lies in the cube `[-r,r]^3`, `ceil(sqrt(3)*r)` encloses that cube. The missing
premises are that own LOD0 scale encloses **every possible cached collision
LOD after quantization**, and that cached subtree publication covers the
relevant rotated/animated placements throughout the interval. Neither follows
from positivity or a clean cache bit. A later proof may convert this bound;
this receipt does not discard it merely because its metric is Chebyshev.

The shared `hull_width` already reads useful **all-LOD origin AABBs**:
model `+c/+10` array/signed-short count; each LOD `+20/+24/+28`. Fold
`498560..8618` is `max(abs(part centre)+part half extent)`, part fields
`+40..48/+50..58`. This includes all groups/parts; it is conservative with
respect to material/tree filtering when its metadata encloses loaded vertices.
Use all records, not the drawn LOD, LOD0 alone or mutable model+60.

However, the existing returned width is clamped to object radius and the walk
omits root/testbody. Node-budget or read failure returns width=r, but the
depth-three cutoff silently omits deeper descendants and can return success.
A complete reader must refuse any unvisited descendants; neither existing
behavior establishes completeness. Its
approximate identity-basis tolerance and truncating scale/placement arithmetic
are policy choices, not a new exact sphere proof. `sqrt(w*w+half_len*half_len)`
from that result is **not** an admitted new ship sphere.

There is also a measured stock rounding issue. The producer enumerates every
referenced position of every LOD/group in the two actual hull bodies, applies
native signed16(position>>2), and compares existing BOB extent metadata:

| Stock hull body | LODs | metadata max extents | loaded vertex max ×4 | needed component cushion | body-only sphere with +4 |
| --- | ---: | --- | --- | --- | ---: |
| Argon M4, SS_SH_A_M4 | 4 | 65536,24328,57919 | 65536,24328,57920 | 0,0,1 | 4582 units |
| Argon M2, SS_SH_A_M2 | 4 | 18781,20634,65536 | 18784,20636,65536 | 3,2,0 | 574367 units |

These are measured **body-origin** source examples, not complete live ships.
They cover 20,826 and 108,651 referenced vertices respectively, with raw scales
3307 and 528422. Four normalized units cover both examples before multiplying
by raw scale; no rounding-down or clamp to object radius is allowed. A general
+4 claim additionally requires trustworthy source AABBs: right-shifting a
bounded source coordinate can extend its negative magnitude by at most three
units, but arbitrary authored stored AABBs can already be wrong. Native source
publication alone does not verify all mod/roster AABBs.

An independent, coarse **per-body** fallback needs no AABB trust: every native
loaded q component is signed16, so body-centred norm is at most
`2*sqrt(3)*S`, for positive raw scale S. The corresponding unrotated source
bounds are 11456 units (M4) and 1830508 units (M2). Apply the actual raw basis
operator norm, or a conservative Frobenius norm, and actual body-centre offset.
This bound covers all cached LODs/material modes and retains useful small-ship
cases even before tighter metadata has roster-wide corroboration. It is not an
object-radius substitution and does not by itself bound future child motion.

## 4. Minimal sphere reader, attitude and call-site lifetime

For an **instantaneous** sphere, reuse the existing bounded all-LOD model/hash
walk but include root zero and every collision-contributing descendant. Copy
its actual resolved centre Cj and raw basis Bj. With a qualified body-centred
box ej, the maximum of `|(sign(ej)) Bj|` over eight corners bounds the complete
body; add `|Cj − ship centre|`, then take the maximum over bodies and outward
round in engine units. Alternatively use the signed16 per-body fallback above.
No runtime triangle/vertex walker or native mesh query is needed. Retain only
copied dimensions/identities; require the same scale/basis magnitude domain as
the reviewed forward math. Unexpected nonuniform scaling requires separate
qualification, not a guessed radius multiplier. Sphere never clamps to obj+a4.

This sphere removes the need to know ship roll for the **current** pair. A
whole 900 ms arrival certificate also needs a bound for every future relative
body offset/scale and raw-basis distortion. The accepted order21 window writes
position, not attitude; it does **not** prove a frozen attitude. Independently,
normal integrator `463d45/3de3` composes attitude, `463df1` reorthogonalizes,
and `463e63..3e7d` copies phys attitude to flight basis when its angular-offset
state is zero. Other paths compose flight angular offsets at `463ead`, then
normalize at `463ebe`. Native normalizer `50e8f0..ebda` normalizes row2,
rebuilds/normalizes row1 by cross product, then row0, using binary64 SSE and
CVTT truncation. No call from an injected helper is proposed. A final-angle
sample at launch or a forward-row sample is insufficient for a box certificate.

**No concrete stock collidable animated-child witness was established here.**
The M4 and M2 source scenes have 25 and 31 parts; the M2 has bigturret_dummy
references which may substitute child scenes. This study has not classified
those substituted nodes' native tree/material masks or transform channels, and
does not assert that turret/nozzle animation contributes to collision. The
remaining condition is specifically **a collision-contributing part whose
relative translation/scale can change during the certified warp**, or an
unbounded basis stretch. Static hull body plus excluded moving effects needs
no general animation framework. Rotation-only colliding children can use a
constant enclosing sphere once their pivot/offset and basis-norm bound are
qualified. Do not permanently exclude animated ships as a class.

At arrival the original caller supplies current ship/gate borrows at
`44d729`; at E the actual native caller supplies A/B. Existing simulation-thread
admission and ID/root checks remain required; release all borrows before
resuming native callbacks. New readers make no native call, allocation, wait,
message pump or file read while borrowed. `wk::ptr_ok` is only arithmetic
plausibility, not mapped/readable memory proof. Use a bounded validated copy
transport under that call-site scope for node 270-byte spans, bounded object
fields, checked model/LOD headers, arrays, hash links and capped NUL strings.
Observed epochs detect stale values; they cannot authorize an otherwise
unsynchronized dereference. The renderer's camera-dependent `fl_owned_nodes`
API is not a new simulation/resource lease. A table binding/current engine
identity can contribute copied tokens, but grants no native read by itself.

Cross-call board/E state must compare current ship/gate IDs, roots, source/load
and shape stamps, current pose, deadline/order/target/phase and existing age
windows. E can run before A in a frame. Root/radius-only width caching does not
invalidate changed parts. At E, revalidate the bounded copied shape/pose or
use a certificate whose shape invariant is established; an old pointer and a
fresh clock are insufficient. Preserve the existing complete CPU/LastError/
MXCSR envelope and normal refusal tail; this note creates no new SEH authority.

## 5. Resource reset premise and finite next checkpoint

A truly new native engine constructor allocates a new `engine+14` model hash
(`48263f..2656`), and new factory BodyData records are initialized. That is
positive evidence for these particular allocations. It is **not** proof that
every relevant process-global type/resource/collision cache is reset. The
accepted resource-lifetime note already leaves this recovery unproved. Keep
resolver-mutation retirement **sticky for the process until restart**; do not
clear it at `factory_begin`, on a changed engine token, restored CWD/bit0 or a
save-load generation. Constructor/hash recreation is narrower than complete
resource identity recovery.

The finite useful implementation is a fixed-size gate reader plus a sphere
reader using the existing model/header/hash machinery: all prepared source
parts, copied poses, exact stamps, root zero, outward sphere and native fallback
on an individual unavailable read. Qualify positive M4 and M2 main-hull inputs,
noncardinal gate poses, root/child replacement, extra/missing bodies, bad scales,
nonterminated names, all-LOD extent underflow and stale E certificates. Preserve
the full traffic feature scope; this receipt supplies no permanent roster ban.

Before whole-arrival suppression, finish **one targeted stock ship witness**:
classify the M2's actual collision-contributing substituted parts, and determine
whether those parts' offset/scale is static over order21 (rotation-only can use
a pivot sphere). Also qualify AABB+quantization padding for that complete set,
or use its independent signed16 scale bound. This resolves a useful capital
case without a generic animation service. Separately bind ID/name matching to
existing restore/load observation or prove the relevant model-cache reset at
that same boundary. These are explicit remaining witnesses, not claims that
all stock geometry is unsafe. Native hook/reader acceptance and live FL flight
acceptance remain parent-owned and have not occurred in this checkpoint.

Independent consequential review reproduced the receipt (apart from elapsed
time), checked the five reused evidence hashes, six source hashes and seven
links, and accepted the bounded findings after correcting the depth-cutoff
description above. No runtime reader or whole-arrival ship bound is admitted
by this RE checkpoint.

## 6. Finite M2 turret and requested-name follow-up

2026-10-29, follow-up to checkpoint `2219389c`. The new
[producer](../../../verification/results/fl-gate-live-bounds/verify_remaining.py)
and [receipt](../../../verification/results/fl-gate-live-bounds/remaining-evidence.json)
record 41 instruction anchors in 11 gap-free regions / 2,667 decoded
instructions, three native flag-table records, 15 effective resource inputs,
six body measurements / 123,486 referenced vertices and five pure source-space
outward-clearance controls. These are measured host/static results, not a live
M2 capture. Source hashes bind the relevant current readers/owners; previous
receipts are bound separately. Reproduce with:

```sh
python3 verification/results/fl-gate-live-bounds/verify_remaining.py
python3 -m py_compile verification/results/fl-gate-live-bounds/verify_remaining.py
```

Raw resource excerpts, disassembly and Ghidra output remain local in
`~/.cache/x3m-port/fl-gate-live-bounds/`. Ghidra queries used the existing
read-only `FLRegistry/X3FL.exe` database and `X3DecompileFunctions.java`,
`X3FunctionBody.java` and `X3XrefsTo.java`; their output is not in the receipt.
No production edit, PE/native/Wine execution, game launch or install occurred.

### Collision parts and the bounded motion witness

The stock `SS_SH_A_M2` scene has 31 parts: one main hull, 24 gun dummy mounts
and six camera dummies. Sixteen mounts select `ships\\props\\bigturret_scene`;
eight select `ships\\props\\bigturret2_scene`. Both substituted scenes contain
socket, body, left weapon and right weapon meshes, plus helper `100001`.
Every source position/attitude key was inspected: keys are constant, use only
`0x2002` position/attitude channels and contain no scale channel. The four
substituted body attitudes are identity. The enclosing M2 mount attitudes are
constant but need not be identity. The two scene variants differ slightly in
socket/body offsets; the receipt retains both.

The four meshes are collision contributors, not excluded moving effects:
their native-selected material groups are opaque, no BodyDummies exclusion
applies, and their union of possible cached collision LODs contains respectively
134, 786, 1,983 and 1,982 triangles (socket/body/left/right). These counts are
unions across all possible selected LODs, not simultaneous live triangle counts.
Builder `4469cd -> 445f80` selects the dummy record; `446a02 -> 4aeaf0` loads
its child scene. The wrapper receives `100000` at `446a57`, sets its body request
to zero, and reaches the `8000000` collision-exclusion path at `446e90`.
Ordinary body children still run the setter at `446abf` and the tree builder
at `446e89`. The camera dummies are excluded by the existing BodyDummies path.

The expected reconstructed tree therefore has **98 candidate collision bodies**:
root body zero, main hull and 96 turret meshes. With 24 bodyless wrappers,
24 helpers and six excluded cameras it has **152 nodes if all helpers are
present**. This is source/native reconstruction, not a measured live count.
Walk through excluded wrappers to their children. Helper `100001` is not a
stock body profile: the ordinary body-ID slot mapping is out of range for the
stock table, and failed body assignment can leave constructor ID -1 / zero
scale / no tree. Match a present helper by scene/part/parent, require no body
contribution, and account for all its children; do not demand a live model ID
of 100001. Permit absence only for this explicitly classified nonbody role
after the complete walk finds no unmatched body. An actually model-bearing
replacement must be included or refuse the individual read.

Native gun update is `450800..451422`, ECX=ship, plain `ret`; its prologue
realigns its own stack and preserves EBX/ESI/EDI/EBP. Its surveyed caller is
`46413e`. Flag records `5763b0/5763b8` map GUNPART alpha/beta to 1/2;
`578cb0` maps GUNF_ROTATE to 2. The stock body uses alpha and the weapons
alpha|beta. At `451070..1087` the update selects the gun-part body ID/flags;
`4510a7` matches node `+144`. The stock no-gamma path at `45130d..1333`
builds a rotation through `50d400`, and `451338..136b` rebuilds the authored
basis and composes into **node+40**, with destination formation at `451359`.
It does not integrate the previous animated basis repeatedly. This path
writes attitude, not these nodes' position, raw scale or axis scales. Thus
the concrete stock turret motion requires a rotation enclosure around its
constant pivot/offset, not a general moving-translation/scale service.

This is a finite writer/source witness, not closure over arbitrary script node
setters, modded keys, nested reconfiguration or replacement. A live reader must
validate the expected subtree, source scales, unity axes, local offsets,
ordinary transform flags and current resolved publication. The 900 ms radius
must include a qualified bound on root/parent and generated raw-basis norms,
plus native integer publication error. The receipt does not silently promote
an ideal orthonormal rotation to that native numerical invariant. No new hook
on the gun update or native matrix call is proposed; existing borrowed-call
boundaries and complete CPU/LastError/MXCSR preservation remain binding.

### Metadata counterexample and useful conservative bounds

The complete set demonstrates why arbitrary cached AABBs plus four cannot be
the universal reader contract. **The socket has authored bounds present in all
four parts, but every bound is zero**. Its loaded quantized component maxima
times four are `(65536,21204,56676)`, from 402 references. Padding that metadata
by four does not enclose even this stock colliding body. Root `v\\00000`, by
contrast, has no authored part bounds. The distinction is retained explicitly.
Do not infer what the live loader recomputed from either source condition.

| Body | Raw scale | All-LOD AABB+4 radius when corroborated | Signed16 fallback radius |
| --- | ---: | ---: | ---: |
| root `v\\00000` | 47 | unavailable | 163 |
| Argon M2 hull | 528422 | 574367 | 1830508 |
| bigturret socket | 5141 | **invalid zero metadata** | 17809 |
| bigturret body | 12337 | 16843 | 42737 |
| bigturret left weapon | 17920 | 18880 | 62077 |
| bigturret right weapon | 17920 | 18719 | 62077 |

All radii are outward integer engine units. The hull needs component cushions
`(3,2,0)`; body/weapons need zero in this complete reference enumeration. The
socket's directly enumerated referenced-quantized AABB gives radius 6,998,
but that is prepared measured geometry, not a safe value inferred from its
authored metadata. The universal signed16 fallback requires neither its AABB
nor a particular tree/material/LOD choice. Use a verified prepared per-body
norm, or this fallback with actual raw scale and a qualified basis norm.
Do not trust uncorroborated runtime AABB+4 or clamp to object `+a4`.

For comparison, the largest mount-offset norm is 361,307 and the weapon local
offset norm is 6,603. A deliberately loose **assumed** turret rotor Frobenius
bound of three gives `361307 + 6603 + 3*62077 = 554141`, below the hull's
`574367 + 17329 = 591696`. These source-space figures show the headroom for a
bounded pivot implementation; neither includes native root/parent distortion
or rounding allowances and neither is an admitted 900 ms live sphere.

The hull-derived 591,696 sphere already exceeds the stock scene apertures
411,590 / 454,759 / 412,052 / 467,046 (ordinary/Terran/hub/Terran jumpgate).
It therefore cannot certify a Titan through those openings. This geometric
refusal is not a capital-ship policy ban. Outward axial placement remains
useful: with margin one, row 0's source-space sweep beginning at z=1,037,565
and moving outward clears both root and scene. The receipt has five such pure
controls (rows 0,5,9,21,25); their live use needs the additional native residual,
movement and step allowances. It makes no claim of 25 positive actual-Titan
gate certificates or useful through-aperture clearance from this sphere alone.

### Indexed names, native cache teardown and ordinary restoration

None of the five inspected hull/turret payloads has a NAME section. Model+4c
is consequently not a resource-name source here. `49afea` stores the requested
ID at model+8; that is **not a body-table slot**. Read a cache hit through
engine+14 / key ID+1, verify model+8 equals the node's requested ID, then use
the existing `4867c7..6812` mapping to index one body record: ID below 1000
maps to itself, ID below 20000 to ID-9000, otherwise ID-20000+fixed. Check
signed/range/overflow before forming `table + slot*1c`; copy its name at +c.
Unnamed valid rows format the requested ID, preserving numeric alias behavior.
This costs one indexed row/name copy per relevant new or changed binding,
not a complete table scan per decision. Compare a capped complete NUL string
against the prepared profile name with the existing namespace-preserving
normalization. The metadata owner's 96-byte classification prefix is not a
full-name equality proof. Cache only copied IDs/name/profile tokens, not native
table/model/string pointers; table reallocation can occur on registration.

The previous warning about stale cache/name identity has a concrete normal-load
recovery route. Cleanup `48a770` walks the complete engine model list and calls
`4993b0` at `48a8b2` until its sentinel is empty (`48a899..a8c1`). Model free
destroys collision tree +5c at `49981a`, then removes requested ID+1 from
engine+14 at `499826..9833`, unlinks and frees the model. Hash insertion
increments header+c at `50cfb5`; removal decrements it at `50d090`. The
ordinary outer game loop `477ba0` has its single surveyed load call at
`477c52` and calls this cleanup at `478639` on its common normal exit, before
the next ordinary game-loop load. Its normal return is `478687`. This clears
these model and collision caches; it does not prove every process-global
resource/type cache was cleared after a resolver mutation.

Restore `487740`, including normal renderer call `49363f`, does not itself
perform that model cleanup. Its first work resets dynamic names at
`487754 -> 486450`. Playback calls `48f1ee` / `48f5d9` can reach restore without
the ordinary game-loop cleanup. Consequently current-name equality on a newly
encountered cached model is not universally proof of the name used to load it.

The implementable normal-load contract is to **retain the verified cold
resource table**, invalidate live name/shape certificates at the existing
name-restore boundary, and permit fresh indexed name/model joins after a
verified normal matching restore when the existing scope established an
actually empty cache before reset. Check both the complete model-list empty
sentinel and the model-hash header+c count under the admitted engine borrow;
pointer/header equality alone is not emptiness. Native callback/reentrant,
foreign-thread, nested-mutation and exception paths must not manufacture this
provenance. Checking actual emptiness on normal return as well is a cheap
stronger witness when those routes have not been excluded. The existing scope
must keep publication disabled through the whole call; EAX=1 alone does not
establish successful name/table allocation or reader completion.

Handling a **nonempty** restore, especially playback remapping, remains a
parent-owned decision. This receipt does not propose a retained complete-model
map, additional observer or automatic readmission from a fresh clock/name
revision. It also does not require process-sticky retirement for an ordinary
name restore: the independent resolver-mutation retirement remains sticky,
while normal live bindings can be reconstructed after the qualified empty
cache/restore boundary. Existing metadata hooks must actually be demanded and
active for this dependency; their present startup demand is lens/classes, and
their snapshot API supplies classifications rather than a simulation/resource
lease. Retain the established no-callback/no-allocation borrowed reader and
abnormal-exit refusal contract from section 4.

Independent review reproduced this follow-up receipt exactly apart from elapsed
time: 41 anchors, 11 regions, 2,667 instructions, 15 resource inputs, six bodies,
123,486 vertex references and five source-space axial controls. It independently
checked turret material eligibility and the zero socket bounds. No correctness
finding was reported within this bounded RE scope. The inferred subtree counts,
native 900 ms allowances, runtime reader and nonempty playback handling remain
unqualified; this review is not live navigation acceptance.

## 7. Bounded live gate reader implementation

The [reader](../../../src/engine/fl/gate_clearance_live.h) now copies gate native
ID/class/row/root/scene, the root body including requested ID zero, and every
prepared scene component. It joins requested body IDs through the existing
read-only model hash (model+8 check) and one indexed body record, copies the
complete capped name, and preserves namespace spelling through the same
ASCII case/slash normalization as cold preparation. Scene names use their
separate eight-byte record mapping. No model+4c NAME field, 96-byte prefix,
native allocating getter, runtime resource lookup, triangle walk or retained
model/table/string/child pointer supplies admission.

`runtime::copy_profile/copy_part` expose only owned values from the verified
cold scratch, with the same owner-thread/resource-generation checks as row
copies. The live reader's caller supplies a separately qualified synchronous
`Borrow` and caller-owned `Scratch`; a readable page or a value token does not
create that borrow. Output is copied only after final scope/name/resource
checks; refusal leaves it unchanged. Transient traversal pointers are erased
on every return. The output's root token is an opaque instance identity for
later comparison, never a resource pointer or an authorization to dereference.

The reader walks every child list to its exact parent+0x10 sentinel, checks
parent links and enforces the 256-node total budget without a silent depth
cutoff. Every source body must match once, even a prepared meshless body.
Source helper 100000 may be absent; a present helper requires constructor
model ID -1 and zero scale, so a model-bearing replacement cannot inherit its
nonbody role. Unmatched model-bearing or collision-relevant nodes refuse.
All matched bodies validate exact source scale, unity axes, ordinary flags,
source local offset and identity local attitude. Root local/world agreement
and child separately-rounded Q16 publication are checked exactly. The copied
world poses feed the reviewed frame and per-part residual math; root and scene
maxima remain separate. Source annular/axial envelopes plus authored offset
give a conservative body-centred norm by triangle inequality. There is no use
of uncorroborated ship AABB metadata or object radius in this gate reader.

Name restore invalidates the live name revision while retaining the verified
cold table. The normal route is the original restore return address 0x493644,
inside the existing common outer Load and lifetime renderer Load wrappers.
`fl_render_admission::current_load` and
`object_lifetime::fl_loading_engine_identity` return narrow value identities;
normal renderer and owned-binding readiness still deny reads during Load.
The wrapper-qualified endpoint borrow additionally checks the actual engine
global against the owned engine at both ends, exact matching scope/load tokens,
owner thread and unpoisoned outer restore. The model list must actually be
`{engine+4,0,engine}` and model-hash membership count zero at entry and normal
return. An allocated bucket array is permitted. Expected nested name
registration is allowed; nested restore, foreign notification, changed engine
or load identity, exception search/unwind, unsuccessful return and nonempty
cache refuse readmission. Resolver/lifetime-loss retirement remains separately
sticky for the process. No complete retained model registry or observer is
introduced. Gate-only startup demand uses `request_gate_startup(true)` and
does not run the renderer classification baseline when lens/classes are off.

The [host producer](../../../verification/results/fl-gate-live/verify_host.py)
and [receipt](../../../verification/results/fl-gate-live/host-evidence.json)
measure the implementation with synthetic borrow/memory/profile authority.
The [frozen native builder](../../../verification/probe/build_fl_gate_live_fixture.py)
prepares three separately scoped fixtures: actual metadata hook/reader,
actual common owner/adapters, and actual lifetime owner helpers. It uses the
existing strict native runner contract. Native execution remains parent-owned;
the verification ledger records its acceptance separately. This checkpoint
does not implement the 900 ms ship sphere, arrival placement, board/E plumbing
or the full controller. Future root/parent basis norm and integer publication
allowances remain necessary for a whole-arrival ship enclosure; an
instantaneous gate copy does not supply them.

**Initial new-game gap at the original reader checkpoint.** Live Names began unavailable and only the
qualified restore completion admits it. The saved-game branch at
`477bf9/477c00` reaches `477c52 -> 479430`; the initial-session branch instead
starts at `477c9c`, calls initial story `4b8e20` at `477d15`, and rejoins at
`477e6d`. This agrees with [initial-story ordering](music-keep.md#65-initial-session-and-restored-story-ordering).
The [bounded producer](../../../verification/results/fl-gate-live/verify_bootstrap.py)
and [receipt](../../../verification/results/fl-gate-live/bootstrap-evidence.json)
check five native operands and the single current readiness assignment. They
do not claim a transitive call census. A new game **without a restore** had no
implemented initial name provenance and the reader remained unavailable. The
existing qualified factory completion is a potential finite bootstrap seam,
but initial admission there has not been implemented or qualified. Ordinary
empty-cache restore recovery does not close this new-game gap.


## 8. Factory bootstrap and registration continuity implementation

2026-10-30, implements the reviewed
[new-game provenance contract](gate-bootstrap.md). The existing
qualified factory endpoint now seals the actual empty model-list/hash under its
still-active cold owner token, after cold resource publication. It retains only
engine/incarnation, resource generation and revocation scalars, with an explicit
pending state. It creates no ready ticket or guessed load. The first qualified
simulation Borrow consumes this seal, validates its authority before and after
activation and binds its actual load, including zero. A populated cache at that
first use is permitted. A later load cannot rebind a consumed seal. Restore,
dependency loss, foreign/abnormal mutation and resource/revocation mismatch
make pending or ready provenance unavailable. Qualified ordinary empty-cache
restore recovery remains independent and retains the cold table.

The existing registration whole-call Scope now copies before fixed/dynamic
counts and performs a prospective-ID hash lookup with distinct present, absent
and unavailable outcomes. It validates the actual synchronous ECX manager
against the sealed engine and the scope/owner before and after copying; it does
not use renderer admission or `fl_owned_binding`. Checked counts stop at the
live reader's dynamic maximum 32767. A real append requires unchanged fixed
count, exactly one new dynamic row and the captured prospective returned ID.
An existing future-ID cache hit then revokes; a proven miss preserves the seal
or ready names. Numeric/duplicate/empty-name returns and rolled-back insertion
with unchanged counts preserve provenance even after a speculative hit.
Unreadable/capped lookup, mismatched counts/result, nesting outside restore,
foreign mutation and exception paths revoke conservatively. Direct expected
registration inside restore leaves that endpoint's independent readmission
intact. No consumed seal is restored on an outer normal return.

The [host producer](../../../verification/results/fl-gate-live/record_bootstrap_host.py)
and [receipt](../../../verification/results/fl-gate-live/bootstrap-host-evidence.json)
measure 291 passing actual-reader/facade checks with synthetic native authority
and transport. This includes stateful load-zero consumption, no rebinding,
populated-cache activation, revocation races, restore nesting and tri-state
lookup controls. The affected live/runtime/metadata host suite passes eight
tests; the metadata core contributes 60 checks. The unchanged positive gate
copy uses 29 copies / 2304 bytes / two residuals. A simple registration miss
uses five modeled data copies (measured) plus four production scope/global
identity reads (source-derived): nine bounded `ReadProcessMemory` calls, without
name/table scans, allocation, locking or per-draw work. The capped lookup adds
at most 64 link reads and one matching-model ID read. Host nanosecond timings
are transport-model diagnostics, not native or game performance.

The [native producer](../../../verification/results/fl-gate-live/record_bootstrap_native.py)
and [receipt](../../../verification/results/fl-gate-live/bootstrap-native-evidence.json)
bind **3,055 passing native Windows x86 checks**: 2,950 across the existing
actual metadata-wrapper fixture's classes/lens cases, and 105 across two focused
actual factory/runtime/live-join cases (empty 54, nonempty 51). Parent-owned
execution used WSL interop, without Wine or a game launch. The wrapper cases
cover all four incoming stack skews, future-ID collision, unchanged-count
numeric return, unexpected append result, nested registration, capped chains,
populated-cache load-zero activation and revocation without resurrection.
They retain the unchanged full GPR/flags/x87/XMM/MXCSR/LastError and callee-pop
oracle; no new hook or adapter instruction changed.

The focused factory cases enable the actual live-name dependency, create real
native empty/nonempty list/hash storage before the actual lifetime factory
hook and traverse `qualified_factory` into the production pending seal. They
verify that factory output alone has no ready ticket. After native cache
population, a synthetic validated simulation Borrow binds actual load zero
only for the empty-factory case, with two authority validations. A second load
cannot rebind. A nonempty factory cache refuses, and later cache clearing and
a repeated one-shot factory notification cannot manufacture the missing seal.
Cold verification is injected in these two focused cases; they establish the
factory/provenance join rather than repeat full cold-resource qualification.
The 71-file current combined closure is bound by the receipt. All gate compiled
inputs remain current; its historical digest-provider builder differs only by
the two new runtime case names and scope text, with exact old/current hashes
and reconstruction recorded explicitly.

The prior runtime2 attempt failed during compilation under `-Werror=clobbered`
on a temporary pointer in the `setjmp` frame. Its local build directory and
compiler log are retained, with no executable or native result. Moving the
post-factory borrowed controls into a dedicated noinline helper kept those
automatic aliases outside that frame; warnings remained enabled. Runtime3 is
the accepted 105-check run. The receipt binds both the accepted artifacts and
this compact compiler-only failure witness. No compiled production source
changed for that correction.

Native engine/cache allocation, caller bodies and simulation Borrow authority
remain synthetic. These receipts do not qualify game borrowing, startup or
controller activation, a ship sphere, installation or flight behavior.
