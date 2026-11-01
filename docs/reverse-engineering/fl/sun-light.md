# FL sun-light values and bounded ownership

Offline study, 2026-10-28. Image `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses below are preferred VAs, base `0x400000`; implementation descriptors
must use RVAs and the admitted loaded base. This qualifies a native data contract
and proposed adapter, not an implemented or accepted shadow/fog feature.

**Copy scalar light values immediately after the ordinary candidate collector
call at `0x495783`, under the existing complete-pass and camera-origin admission.**
The collector's actual entry is `0x495450`; `0x495466` is its engine spill.
Do not enable the AP raw-memory poll for FL. Although engine array offsets match,
FL RGB, range and attached-record fields have moved, and native ranking differs.
The smallest adapter preserving AP's program-independent candidate source needs
one ordinary call seam, a bounded call-free copy and an owned activation record.
Cube, auxiliary and special views cannot fill the main-scene sun latch.

## Evidence and reproduction

Run from the repository root:

```sh
python3 verification/results/fl-port/sun-light-contract/reproduce.py
```

The [producer](../../../verification/results/fl-port/sun-light-contract/reproduce.py)
uses the project's PE reader, fixed-image identity verifier and Capstone.
[Derived summary](../../../verification/results/fl-port/sun-light-contract/summary.json):
**17 regions, 958 instructions, 66 operand anchors, 40 independently decoded
region/seam boundary edges, three rejected AP-layout mutations** (measured).
The collector contains **zero CALL instructions**. The direct-edge census finds
three collector callers and two selector callers, zero edges into the three
surveyed seam interiors and zero HIGHLOW operands in those seams. Constants
are read from the image, not inferred from AP. The producer validates its JSON and records its own SHA-256 in the summary.
Raw listings remain outside the checkout in
`/home/asvetl/.cache/x3m-port/sun-light-contract/`.

These are offline anchor/boundary checks, not execution, a new complete CFG proof,
or exclusion of arbitrary computed private calls. Reuse the fixed healthy-native
callback/owner closure below. No production change, build, native/Wine execution,
installation, game launch or commit occurred in this study.

## 1. Identification, fields and ranking

| Item | Established FL evidence |
| --- | --- |
| Engine identity | Native collector reads `0x5aa100` at `0x495457`. Future adapter obtains authority from `object_lifetime::fl_owned_binding`, not this global, which can remain stale after destruction. At both collector returns ECX still holds the engine used for this collection; compare it to the fresh binding before any copy. |
| Candidate array | Engine `+0x5e8c`, 255 DWORD slots, null terminated. Append guards `0x49548f/0x49557f` cap count at 254; final zero stores `0x49563d/0x495652` end at or before `+0x6284`. |
| Light classification | Both collector loops require node `+0x12c & 4`. `!(flags & 0x400010)` bypasses camera-distance admission (`0x4954ef/0x4955df`). Point/spot candidates use signed range `+0x15c` and a distance/fraction test. This classification is a light type, not proof of an astronomical sun. |
| Raw world position | Three signed DWORDs `+0xb0/+0xb4/+0xb8`; collector subtracts camera `+0x30/+0x34/+0x38`. Native material subtracts submitted node position from light position at `0x4e4512..0x4e4539`. |
| RGB | Signed 16-bit words **`+0x154/+0x156/+0x158`**, set by `0x4a3ee0` at `0x4a3eec/0x4a3ef8/0x4a3f04`. AP `+0x150` is FL selected LOD, not red. |
| Range | Signed DWORD **`+0x15c`**, setter store `0x4a3f2b`; both collector and material slot chooser consume it. |
| Attached record | DWORD **`+0x170`**; consumed at `0x4de1c0`, `0x4de416`, `0x4e4503/0x4e4696`. FL `+0x16c` is a scalar initialized to `0x20000` at `0x4a3e81` and overwritten from a type-table field at `0x4a3fa4`; it is not the AP record pointer. |
| Record shape | Allocation size `0x6c` at `0x4de371`: D3DLIGHT9-shaped data through `+0x64` plus slot index at `+0x68`. Diffuse RGB floats at `+4/+8/+0xc`; fill converts signed RGB words with double **1/256** at `0x591a80`. Position floats `+0x34/+0x38/+0x3c` are raw node position multiplied by node scene `+0x2c` (`0x4de429..0x4de46e`). |
| Configured selection capacity | Engine `+0x6288` initialized from renderer capability `+0x4f4` at `0x4da1b1/0x4da1b7`, signed-upper-clamped to 8 at `0x4da1bd/0x4da1c2`; no lower clamp. It is not an unconditional eight-entry layout signature. |

`0x4de1c0` fills record type 2 when flags include `0x10`, type 1 for
`0x400000`, otherwise type 3 and lazily sets `0x800000` (`0x4de1fc`). The
flag is **also explicitly set by type selection** at `0x4a3f81`; it is not
exclusively lazy or exclusively a sun marker. Therefore the AP diagnostic
admission rule remains useful before record filling, but a claimed sun still
needs scene-role and submitted-direction agreement. No unique sun ID, guaranteed
single-sun sector or qsort tie order is established here.

Native selector entry is **`0x4964c0`**, not its spill `0x4964d1`. It is run per
submitted object (`0x496fa7`) and also by separate path `0x423012`. It initializes
selection records at engine `+0x628c`, stride 12: candidate index, score, distance.
The score is **truncation toward zero** of double
`0.299*R + 0.587*G + 0.114*B`, computed in the observed G/R/B addition order at
`0x496540..0x496587`, then `+0x300` for `0x800000`. The constants are image
`0x591b10/0x591b38/0x591ae0`. AP's integer rounded luma is not exact FL ranking.
Point/spot ranking also depends on submitted-object distance/range; spot ranking
has a further multiplier. A candidate with neither directional nor point/spot
bits receives score -1 at `0x496646` until its type is filled. The initial score
threshold is 15. Candidate-only ranking cannot promise exact per-object selection.

Material wrapper `0x4e6f6d..0x4e6fc8` walks sorted selected records, admitting
**`flags & 0x800000` OR signed range `>0x256250`**, and supplies the first two
such lights to `0x4e0f30` at existing seam `0x4e7142`. Its first Dir vector is
light position minus submitted node position, normalized/scaled before upload;
Dir color comes from attached record Diffuse. An owned world-point candidate plus
per-draw direction cross-check is consequently appropriate; treating the raw
position as a fixed unit vector loses the native per-object relation.

The [retained-alias note](registry-retained-aliases.md) establishes
a conditional selector index -1 write into engine `+0x6280/+0x6284/+0x6288`.
This study does not prove healthy producer constraints eliminate it. Copying the
fresh candidate array before selection avoids depending on mutable selection
records or corrupted capacity. Always bound the copied array and validate every
entry's light flag. Do not claim this protects native code from its own selector
bug, or that a later raw poll is safe.

## 2. Initialization and lifetime interval

Related/light birth `0x4a3da0` allocates and zeros `0x270` bytes, registers its
handle at `+0x28`, sets light bit 4 at `0x4a3e5b`, and links into the engine
list. Zero-initialized RGB is not ready illumination. RGB/range/type setters
populate scalar fields and can call the record filler when a record exists.
The attached record is separately lazy: `0x4de360` allocates/zeros 108 bytes,
publishes `node+0x170` at `0x4de3e9`, initializes slot index -1 and calls the
filler. Allocation can invoke registered recovery and can fail. Release
`0x4a2095..0x4a20d5` frees it at `0x4a20a9`, then clears `node+0x170` at
`0x4a20bf`. Node registry membership does not lease this attachment.

Current world positions are established by ordinary scene propagation before
camera activation: `0x48ba3a → 0x494b00`, then `0x48ba43 → 0x495670`, then
`0x48ba4c → 0x497720`. The latter begins selection/submission preparation.
Propagation's light branch calls record-position update at `0x494aa3 → 0x4de410`.
Within ordinary activation, camera setter `0x495687 → 0x4ded90` publishes the
existing owned camera record before collection **`0x495783 → 0x495450`**.
The array thus is not ready at the earlier camera-publication seam itself.

Collector entry receives **stack+4=scene, stack+8=camera** and returns `RET 8`.
It walks the scene root list and one level of children, admits eligible lights,
terminates the engine array and returns without any callback. These live node
values have just been consumed by native code. A scalar copy at its return has
a finite synchronous borrow under the supported complete-pass admission; a
later page check, pointer token or registry lookup alone does not confer it.

The collector has three direct callers (measured):

* `0x495783`, ordinary camera activation, including its auxiliary/special origins.
* `0x497857`, cube face. Here collection precedes propagation at `0x497866`;
  an ordinary post-collection world-position-readiness claim cannot be reused.
* `0x423007`, a separate call outside the qualified camera-origin transport.
  It is not admitted by this proposed adapter.

Reuse [material submission's borrow and callback contract](material-submit.md#3-actual-borrow-readiness-writers-and-reentrancy),
[load/full-pass readiness](ui-load-readiness.md),
[close/native callback exclusion](ui-render-exclusion.md), and
[recovery/continuation closure](ui-render-mutation.md).
The [value-lifetime note](registry-value-lifetime.md) explains why
membership cannot replace these prerequisites. No new arbitrary whole-engine
thread audit is required for this fixed closure. Native light changes may occur
between samples; a copy describes that activation, not immutable sector state.

## 3. Smallest owned adapter contract

Extend the existing FL snapshot family with an ordinary light-candidate record:

1. Replace only CALL **`[0x495783,0x495788)`**, forwarding its two arguments and
   native `RET 8` cleanup exactly once. On successful native return, before
   continuation `0x495788`, require the live common borrow, matching ordinary
   `camera_state_fl::current_origin/current` scene/camera and a fresh owned
   lifetime binding. Compare returned ECX engine to `binding.engine`. Deny
   unsupported nesting/origin or outstanding Reset/invalidation before reading.
2. In a **call-free interval**, copy the 255-word array and each nonzero node's
   handle `+0x28`, flags `+0x12c`, three raw positions, three RGB words and range.
   Require a terminator within 255 entries, at most 254 nodes, overflow-safe
   address bounds and light bit on every entry. Zero storage and acquire owned
   metadata before this interval. Do not dereference the scene, parent or
   attached record, call `engine_memory::read`, allocate, log, lock or invoke
   engine/OS/COM helpers while reading native payload.
3. Rank owned values with explicit FL luma semantics. Preserve both directional
   slot-rule and type-admission candidates, runner-up/tie information and counts.
   A farthest-position tie break may reproduce AP policy, but label it proxy
   policy, not native qsort order. Negative/invalid illumination must not become
   an unsigned high score. After copying, recheck binding revision, borrow,
   camera identity/serial and request epoch; validate any retained winner's
   handle/serial with `fl_owned_current`. Failed validation publishes no value.
4. Publish only owned scalars and identity labels keyed by the existing camera
   key: observer/renderer incarnation, load/registry/revision, complete pass,
   scene/camera serial, origin and activation, Reset/request generations. Add a
   monotonic publication identity if needed. Consumer lookup requires the live
   matching domain and compositor main-scene/target correlation. Retain no
   dereferenceable node, array or D3D-record pointer. Clear on failed collection,
   changed activation, load, Reset, teardown, ownership loss or epoch exhaustion.
5. At the selected main-scene draw, compare the candidate-derived direction with
   submitted Dir constants using the existing shadow point-source policy before
   using position/color for shadows or fog. Also require the selected candidate
   color to agree with the actual first directional slot color; direction alone
   cannot establish which of multiple collinear lights supplied radiance. Exact
   per-material upload identity can be supplied by the optional argument-copy
   extension described below. Absence, disagreement or a different
   origin denies the sample. Do not silently stamp a last-frame or background
   light as current. Current camera scale is owned data; do not hardcode AP's
   gameplay scale `0.01` for FL world-unit conversion.

An implementation can reduce the 254 copied records to the required ranked
outputs during the call-free interval; it need not retain a large record array.
The complete scalar read budget, including every candidate handle, is **at most
8,640 bytes** (inferred: 1,020 array bytes + 254 × 30 scalar bytes), once per
ordinary activation. Exact emitted copy cost requires implementation measurement.
Per-draw consumers should perform owned-key lookup only, with no allocation,
page validation or native light traversal. Existing observer metadata validation
happens outside the native copy interval; retain no lock across engine/D3D calls.

A smaller *code delta* could extend `material_submit_fl` at `0x4e7142` to copy
its two optional light arguments' scalar fields. This directly identifies the
native chosen Dir slots and would be useful for validation. However, selection
is per object and can supply no Dir light; this does not provide the same
program-independent full candidate source as AP's poll. Existing
`material_submit_fl::Record.light1/light2` are identity tokens only: they do not
authorize a deferred dereference. Choosing that alternative requires explicitly
accepting the narrower availability contract, not declaring AP equivalence.

## 4. Hook preservation, reentrancy and remaining acceptance

The ordinary seam is one five-byte relative CALL with no HIGHLOW operand or
surveyed direct edge into its interior. Its displaced target is `0x495450`.
Before forwarding, both stack words are live, ESI is camera and EDI is scene;
EBP/ESP belong to the aligned ordinary wrapper. After native return, ECX is the
collector's engine, ESI/EDI retain camera/scene; EAX and native flags/results
must survive bookkeeping. Continuation immediately loads EDX from camera+0x378
and tests it. Preserve all GPRs, EFLAGS, LastError and FP/SIMD state, including
MXCSR/x87 state, with four-byte incoming-stack compatibility and separately
aligned helper storage. Do not infer scratch permission from the collector's
lack of SIMD: the enclosing renderer has live state. Forward native arguments,
stack cleanup and results exactly once; no native pointer read during unwind.

The collector has no native SEH prologue and no calls. Its surrounding camera
invocation already has per-invocation scope and foreign-SEH cleanup. An adapter
must compose with that scope: nested admitted activation gets its own record,
unwind removes only its invocation, Search/ContinueExecution preserves it, and
return never revives an outer record whose activation/device generation changed.
No C++-destructor-only unwind assumption. Quiescent exact-profile installation,
complete rollback, ownership-checked uninstall and request-only foreign-thread
invalidation follow the existing FL adapter contracts. The separately surveyed
cube seam `[0x497857,0x49785c)` and existing material seam
`[0x4e7142,0x4e7147)` are not authorization to install either here.

Still required: implement and review the chosen adapter; compiled call-free-copy
and forwarding/state-preservation evidence; zero/254/unterminated array and field
mismatch cases; nested/foreign-SEH/Reset/failed-binding denial; immutable copied
values across subsequent native mutation; actual scene-role and submitted-Dir
correlation; multi-light/tie policy; measured sampling cost and user FL flight
validation. Cube world-position readiness needs its later propagation boundary
if ever supported. No runtime evidence yet proves FL fog radiance, scale,
shadow direction or the presence of a unique sun. Windows-compatible source and
synthetic fixtures would remain distinct from verified native game behavior.

## 2026-11-01: poll contract

Offline, X3FL.exe SHA-256 `e7f3aad8…dfbb8` at `/mnt/c/X3FL/X3FL.exe`, matched
function by function against `/mnt/c/X3AP-orig/X3AP.exe` (`fdbf3418…f8ab`) with
Capstone. Addresses below are **RVAs** (FL is ASLR-relocatable; add the loaded
module base); AP addresses are absolute VAs. Everything in the table is
re-derived on every run by
[`tools/analysis/fl_sun_light_constants.py`](../../../tools/analysis/fl_sun_light_constants.py),
which writes `src/engine/fl/sun_light_poll_fl_inc.h`. It decodes **50 runtime
anchors**, whole instructions of at most 31 bytes, each relocated at most at its
own disp32: six name the context global and four name `.rdata` doubles. It also
decodes **16 census shapes**, the direct-caller census of seven functions, and a
linear-sweep census of the nine instructions that name the array.
[`verification/analysis/test_fl_sun_light_constants.py`](../../../verification/analysis/test_fl_sun_light_constants.py)
mirrors the in-memory check at three bases. It refuses every single-bit flip, a
foreign base, nine AP-layout or foreign-value mutations, a moved relocation and
a fourth collector caller (measured: 6 tests pass). No raw listing is kept in
the repository; no game, Wine or build run was involved.

**Outcome: the AP hook-free poll ports to FL unchanged in logic.** It needs the
context slot as an RVA, colour, range and record each 4 bytes later, a
`0xc4`-byte node block (`+0xb0..+0x173`) and the FL anchors as the identity gate.

| Contract item | FL (RVA) | AP | Status |
| --- | --- | --- | --- |
| Render context R | `*(base+0x1aa100)` | `*0x608518` | verified: collector `0x95457`, selector `0x964c9`, Dir rule `0xe6f6d`, slot-count init `0xda1a5` read it; published at `0x77697`/`0x8a5f1` (AP `0x403367`/`0x470df8`). It is the engine slot of `object_trace_fl_inc.h`, not a camera or renderer slot |
| Candidate array | R+`0x5e8c`, 255 words, append cap `0xfe` | same | verified: `0x95472`, `0x9555c` (`0x17a3*4`), caps `0x9548f`/`0x9557f`, terminators `0x9563d`/`0x95652` |
| Slot count | R+`0x6288` = min(caps `[[[renderer]+0x18]+4]+0x4f4`, 8) | same code at `0x4b9ba4` | verified `0xda1a5..0xda1cc`. Expected 8 on hardware reporting at least 8 (the AP reader's assumption on both editions); no lower clamp. The field is most likely D3DCAPS9.MaxActiveLights (inferred, not traced) |
| Selection records | R+`0x628c`, stride 12 | same | verified (Dir rule `0xe6f82`, `0xe6fb8`) |
| Flags | node+`0x12c`: 4 light, `0x10` spot, `0x400000` point, `0x800000` directional | same | verified; the D3DLIGHTTYPE written by `0xde1c0` (2/1/3) names `0x10` and `0x400000` |
| Position | int32 node+`0xb0/b4/b8` | same | verified (collector `0x9549f..0x954cf`) |
| Colour | int16 node+`0x154/156/158` | `+0x150/152/154` | verified (selector `0x96540..0x9655a`, setter `0xa3eec..0xa3f04`, record filler `0xde206`). FL `+0x150` is the LOD index |
| Range | int32 node+`0x15c` | `+0x158` | verified (collector `0x954f7`, selector `0x9660f`, Dir rule `0xe6fa5`, setter `0xa3f2b`) |
| Light record | node+`0x170`, position floats at record+`0x34` | `+0x16c` | verified (`0xde1c0`, `0xde416`, `0xde43c`). FL `+0x16c` is a scalar |

Functions, each matched by instruction shape against its AP counterpart:
collector `0x95450` (AP `0x47c640`, `RET 8`); selector `0x964c0` (AP
`0x47d5e0`; the score block AP `0x47d641` is FL `0x96523..0x96596`); Dir-slot
rule `[0xe6f6d, 0xe6fc8)` (AP `0x4c5030..0x4c508f`) inside wrapper `0xe6ef0`;
lazy directional flag `0xde1c0` (AP `0x4bdbf0`, setter instruction `0xde1fc`).

**Score.** `trunc((0.587·G + 0.299·R) + 0.114·B)` in SSE2 double precision
(`cvttsd2si` at `0x96587`; weights at `.rdata` RVAs `0x191b38/0x191b10/0x191ae0`),
then `+0x300` when the node has `0x800000`. AP computes the same sum in the same
order on x87 and converts through `0x52b5d0`, which also **truncates**
(`cvttsd2si`). The formula is therefore the same in both editions. The AP
reader's `(x + 500) / 1000` rounding is an approximation on both, off by one
only near a tie. Lights with `0x400010` (point or spot) lose
`luma·d/(2·range)`, where d is the Chebyshev distance to the submitted node minus
node `+0x70`. They score 0 out of range, and spot lights take a further cone
factor (`0x96649..0x96733`). An untyped light (no `0x800000`, no `0x400010`)
scores -1. The Dir-slot threshold is confirmed: `flags & 0x800000` **or**
signed range `> 0x256250` (`0xe6f99`, `0xe6fa5`).

**Thread and timing (verified by call tree; not a whole-engine thread audit).**
The main loop calls the cockpit updater's registry walk at `0x782ca` and the
complete pass `0x8b6d0` at `0x782cf`, in that order (AP `0x403f2f`/`0x403f34`).
Inside the complete pass every rendered view runs propagate `0x94b00`, then
activation `0x95670` (which collects at `0x95783`), then prepare `0x97720`. Its
layer draws `0x978e0`, `0x975c0` and `0x97680` follow, and the selection records
are reset at `0x8ba9b` before the next camera. The Dir rule's two callers,
`0x97035` and `0x97702`, lie inside that draw cone. The auxiliary-view loop
(`0x8bbef`) and the engine+0x64 view (`0x8bc87`) collect again in their own
activations. Cube rendering `0x4977c0` collects at `0x97857` for its own faces.
The only instructions that write the array are the collector's
(linear-sweep census). A poll taken inside a routed main-scene draw therefore
sees the main view's array, rebuilt on the same thread, as on AP. A poll at
Present sees the last view rendered.

**Cockpit HUD light scene (the AP "secondary scene", settled for both
editions).** Cockpit init `0x21b60` (AP `0x420260`; FL thiscall, AP stack
argument) creates the HUD scene at cockpit+4 and the HUD camera at cockpit+8
(`+0x270 |= 0x24`). It also creates 16 lights at cockpit+0x14 (`0x21c00` →
light birth `0xa3da0`) with colour `0x80` and a forced `0x800000`
(`0x21c49`). That init state does not persist. Every frame before the complete
pass, cockpit light refresh `0x22fb0` (AP `0x4216e0`; called at `0x2292a` from
the cockpit updater) detaches all 16 lights (`0xa5740`). It collects and selects
for the ship node (`0x23007`, `0x23012`; AP `0x42172f`, `0x42173b`), which
overwrites the array before the pass. For each selected non-spot light it then
copies the colour, range, `+0x160..+0x168` and flags & `0xc00000` (so the forced
bit is replaced by the source's own bit). It also copies the camera-relative
position (`0x494240` → `+0xf0` → local `+0x30`). Finally it re-attaches the copy
to the HUD scene (`0xa56b0`; AP `0x489da0`). In the HUD view's own activation
the array therefore holds copies of the ship's chosen lights, the sun included,
with the sun's colour. Inside a main-scene draw they are not in the array.

### Corrections to sections 1 to 4

* "Native ranking differs" from AP: it does not. Both editions truncate the same
  weighted sum. What differs from both is the AP reader's rounding.
* The type setter's explicit `0x800000` (`0xa3f81`) is not FL-specific: AP has
  it at `0x4886de`.
* `+0x6288` is not a layout signature: AP initialises it with the same code.
  The AP reader's `== 8` check carries the same hardware assumption on both
  editions. It also catches the selector's index -1 overwrite of `+0x6288`
  described in [registry-retained-aliases.md](registry-retained-aliases.md).
* Section 3's call-seam adapter at `0x95783` is not required for a
  main-scene sample. The bounded poll at a routed main-scene draw has the same
  timing guarantee on FL as on AP. The engine-pointer staleness caveat of
  section 1 still applies: the global is read fresh on every poll and stays
  behind the FL anchor gate.

Remaining unknowns: the exact meaning of renderer caps `+0x4f4`; the qsort tie
order (comparator `0x96490`); whether any computed or third-party path writes
the array (outside the linear-sweep census); FL behaviour in flight (no runtime
sample).
