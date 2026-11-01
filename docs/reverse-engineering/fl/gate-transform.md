# FL gate bounds to live instance coordinates

2026-10-29. This receipt narrows the remaining transform requirement for the
[gate clearance profiles](gate-clearance.md). It reuses the
[arrival boundary and width contracts](gate-traffic.md), including
`0x44d729 -> 0x44d72f`; it proposes no hook, controller implementation, native
call, general geometry service, or traffic admission. Addresses are preferred
FL VAs, base `0x400000`. The executable and stock catalogues were read only.

**Subsequent parent decision:** the [FL numerical acceptance boundary](../../architecture/gate-traffic-controller.md#fl-numerical-acceptance-boundary-2026-10-29-parent-ratified)
supersedes this receipt's proposed universal native-predicate certificate as an
activation prerequisite. Conservative geometry and bounded implementation error
remain required; native3 supplies sampled corroboration, not that certificate.

```sh
python3 verification/results/fl-gate-transform/verify.py
python3 -m py_compile verification/results/fl-gate-transform/verify.py
```

The [producer](../../../verification/results/fl-gate-transform/verify.py) and
[compact result](../../../verification/results/fl-gate-transform/verification.json)
reproduce **89 frozen operand anchors, nine gap-free regions / 739 instructions,
16 host checks, 24 proper cardinal rotations and 901 integer warp times**
(measured). The existing effective-resource snapshot is checked afresh through
`Resources` and `snapshot_current`; 11 authored scene parts across seven scenes
are retained as derived mappings. No PE, native program, Wine command, game,
build or install was executed. Host arithmetic witnesses are not FL execution.
The result records the exact EXE hash and input JSON hash; neither admits a
runtime geometry profile by hash whitelist. Raw listings remain local/untracked.

## 1. Established instance fields and transform arithmetic

| Input / consumer | Established contract and witnesses |
| --- | --- |
| Gate and root | Existing gate class `+0x48=18`, subtype `+0x4a`, root `[gate+0x70]`, identity `+8`. Arrival reads root position `+0x30..38` and basis rows `+0x40/+0x50/+0x60`, each with 16-byte stride. `0x44d663/674/677/67c` rotates a gate-local displacement and adds root position. |
| Local versus resolved world pose | Native construction `0x447693..6bf` copies root `+0x30 -> +0xb0` and `+0x40/+0x50/+0x60 -> +0xc0/+0xd0/+0xe0`, then calls child publication `0x494370`. Scene update `0x495220..24e` repeats this for roots. These are distinct storage locations; arrival reads the first copy and collision reads the second. Static code does not prove their equality at every possible caller/time. |
| Model identity and scale | Node model key `+0x144`. Setter `0x4a3663/666/668/66b` reads model LOD-array entry zero, then LOD `+0x14`, and stores that **raw integer engine-unit length** at node `+0x70`. Axis values `+0x80/+0x84/+0x88` initialize to `65536` at `0x4a366e/678/682`. Do not confuse object `[gate+0x70]` (pointer) with node `+0x70` (scale). |
| Child identity | Animation bind `0x4aa750..791` stores scene ID at node `+0x258`, part index at `+0x25c`, authored part flags at `+0x260`; the equivalent bind at `0x4aa950..975` does likewise. These identify a source part, not a lifetime lease or proof of its current pose. Model IDs/names still require a cold matching step; this receipt does not qualify every AP tree-reader field for FL. |
| Child position | `0x49439d/3a0`: rotate child's local `+0x30` by parent's resolved world basis `+0xc0`, storing a temporary/world displacement in child `+0xb0`. Unless parent `+0x12c & 0x10000`, multiply each **already rotated world component** by parent's corresponding axis scale, with rounded 16.16 products (`0x4943b4..40c`). If child `+0x12c & 0x40`, multiply that result by parent's `+0x70/65536`, then add parent world position; otherwise just add it (`0x49440f..4cb`). Rotation and nonuniform axis scale do not commute. |
| Child basis | Flag test `child+0x12c & 0x10004020` at `0x4944ce` chooses copying the child's local basis to world basis. Otherwise `0x4944fd..50d` composes child local and parent world bases through `0x50ebe0`, then recurses at `0x494517`. The ordinary supported identity-local scene part uses the composition branch. Unexpected copy-mode flags require refusal, even if a pose happens to coincide now. |
| Native tree instance | Pair adapter `0x4982d0..554` reads model tree `+0x5c`, node world basis `+0xc0..e8` times float `1/65536` (`0x591994`), world position `+0xb0..b8`, and node **raw** `+0x70` for each operand. It passes these to `0x51aae0` at `0x49853a`. That kernel rotates tree coordinates, multiplies by the raw scale, and adds world position (`0x51ab7a..ac2d`, second operand `0x51ac38..ad08`). Tree emission stores engine X at stack `+0x58`, Y at `+0x54`, Z at `+0x5c` (`0x497d25/40/61`); the adapter correspondingly supplies Y/X/Z positions and the conjugated basis. This consistent X/Y library convention introduces no additional engine-space scene rotation. Axis scale fields are not arguments of this adapter. |

Tree vertices from the previous receipt are exact binary fractions
`q/16384`, with `q=signed16(file_position >> 2)`. The ideal affine world point
for an individual body is therefore

`W = C + (q * S / 16384) B`,

where `C=node.world_position`, `S=node+0x70`, and `B` has the raw resolved
basis rows divided by 65536. Scene offset is already included in `C`.
The profile's body point is `x=o+q*S_source/16384`. **Do not multiply the
profile again by `S/65536`, by gate radius, or by an object-radius ratio.**
Argon scene scale is 678369 while root body 0 has scale 47; both are ordinary
supported stock source values. Catapult scale is 1797099 and its authored
offset is `(0,0,1793560)`. A single gate-wide model scale loses this distinction.

All integer multiplies above use signed 64-bit products and
`r16(a*b) = floor((a*b + 32768)/65536)`, then signed DWORD storage/addition.
`0x50e0c0..1dd` rounds each of a matrix-vector product's **three terms
separately**, not once after summing: three half-unit terms give 3 rather than
2. Negative half ties round toward positive infinity. Product/sum overflow is
not saturation; the proposed contract refuses inputs that cannot be represented.
The output copy is 16 bytes although only three coordinates are geometry.

`0x50e0c0` uses ECX=input point, EDX=basis, one caller-cleaned stack output
pointer, plain `ret`. Scalar-vector `0x412c00..5f` uses ECX=input vector,
two stack pointers (output, scalar), `ret 8`. These ABIs explain arithmetic;
the contract below makes **no call** to either native function.

## 2. Bounded source and live validation contract

Stock scene scaling can be admitted by a fixed, verified source mapping, with
the following per-instance guards. This is a geometry input contract usable
by the existing board's two scalar solids; the runtime reader is not implemented.

1. Match the current effective resource generation and gate row/profile. Keep
   the root and each authored part's role, part index, resolved body identity,
   exact positive LOD0 source scale, static offset, identity-local policy, and
   conservative body-centred maximum vertex norm. The existing per-part envelope
   and offset suffice to derive that norm without retaining vertices. Root 477
   stays unknown; hsap's scene meshlessness does not remove root body 0.
2. In the existing synchronous simulation-thread read scope, validate the gate
   ID/root and complete bounded descendant mapping, including model identity,
   parent links and expected source part. Require node scale **equal** to its
   matched source scale, all three node axis scales exactly 65536, and ordinary
   static local offset/identity basis. Refuse offset flag `0x40`, copy-world-basis
   flags `0x10004020`, nested/moving/unmatched collidable parts, incomplete walks,
   stale generations and unknown geometry. Extra proved noncolliding effects
   may be omitted only with the independently established collision masks;
   an early-return "some tree exists" walk cannot establish completeness.
3. Require gate root local and resolved world position/basis to agree at the
   read. Verify each expected child's resolved position/basis against the
   native publication formulas above. This catches unpublished/stale world
   transforms. Preserve these copied scalars with a gate/native-ID, root,
   profile-generation and pose-validation stamp. Invalidate/revalidate when
   those inputs change. Do not retain node/model pointers as board geometry.
4. Use a genuinely orthonormal, right-handed comparison frame `Q` at the gate
   position `G`, and account for its difference from raw `B`. A proper cardinal
   raw basis is a particularly simple exact subcase (24 rotations verified).
   General rotated gates need a checked orthonormal construction and the
   residual bounds below. Merely normalizing three rows independently does
   not repair shear; reflected/singular or numerically ill-conditioned frames
   refuse. The current `gate_frame()` returns raw rows, with squared norms
   0.98..1.02 and cross dots below 0.02. Those tests **are not an exact
   inverse/clearance certificate**: a row 65208/65536 passes, yet dotting a
   600000-unit displacement out and back changes it by about 5990 units.

For a matched body `j`, let `o_j` be its authored offset and `R_j` enclose the
length of every **body-centred** source vertex after its source model scale.
Root has `o=0`. From copied live scalars calculate an upward-rounded bound

`E_j >= |C_j - G - o_j Q| + ||B_j - Q||_F R_j + E_float,j`.

The first term includes actual child-position rounding. The second includes
raw basis length, orthogonality and composition error. This is a derived
geometric inequality: every transformed triangle point differs from the ideal
profile transformed by `Q` by at most `E_j`. Convex interpolation preserves
the bound. Use separate maxima for root and scene, then inward/outward round:

`h' = max(0, h-E); outer' = outer+E; zmin' = zmin-E; zmax' = zmax+E`.

An actual nonzero residual must never be dropped merely because a loose frame
guard passed. Unknown/nonuniform live scale refuses instead of borrowing the
unit-axis source profile. A later general rescaling design would have to handle
body scale and scene offsets separately; it is unnecessary for these stock
source values. The board can retain the resulting two solids, generation and
validation stamp; it needs neither a mesh pointer nor a new resource service.

`E_float` must cover FP32 conversion/evaluation and the chosen implementation's
projection error. The adapter converts position/scale to FP32 and performs
SSE scalar arithmetic, so integer profile rounding alone is insufficient.
As a **derived forward-affine-map bound only**, under `|C_i|<=2^30`,
`0<S<2^24`, `|B_ij|<=2` and `|q_i|<=32768`, IEEE round-to-nearest has per-axis
error below `64 + gamma(8)*(2^30+3*2^26)`, about 672.01 engine units; its
Euclidean bound is below 1164.01. An outward 2048-unit allowance therefore
encloses this particular forward evaluation. The producer checks that numerical
inequality. This is **not** a measured collision tolerance or an end-to-end
proof of `0x51aae0`'s relative-frame intersection arithmetic/MXCSR modes. Until
that bound and the implementation's Q/projection rounding are qualified, use
unknown for a runtime safety certificate; do not treat 2048 as a magic complete
margin. Integer proper-cardinal mapping alone does not remove FP32 translations.

## 3. Final P and the native warp/clear path

At the accepted seam the ship's actual integer endpoint P is already stored
at ship phys `+0x30..38`; native copies it to flight `+0x240..248` at
`0x44d72f/733`. Certify the **final integer words after the proposed write**,
including cast/add rounding and range checks. Project `P-G` into the comparison
frame Q; an added lateral delta by itself is not the final centre. The native
fourth DWORD remains native. Existing width-map/lifetime safeguards are reused.

The launch continuation takes raw gate basis row 2 at `0x44d75e` and forms

`P_initial[i] = P[i] + r16(1348650 * F_raw[i])`.

Order `0x21` (`0x45f10d..1c4`) computes
`d(t)=trunc(1665000*t*t/1000000)`, then calls the rounded scalar-vector helper
at `0x45f166` with the **current raw gate forward row**, and adds the same
flight P. For the ordinary fresh 900 ms deadline, `0<=t<=900` and
`0<=d<=1348650` engine units. The final snap copies flight P at
`0x45f0fd/104`. The host check evaluates all 901 integer t values and verifies
the rounded position lies within `sqrt(3)/2` units of its unrounded affine
segment. This bound is about native per-component rounding, not timestep
continuity, hull motion or a recorded FL flight.

For a fixed validated gate pose, test the whole segment
`P -> P + 1348650*(F_raw/65536)`, enlarged for that rounding, after projection
by Q. Do not silently replace raw F with normalized Q-forward: the residual
can introduce lateral movement. For changed gate pose, stale/reset clocks,
deadline outside the established interval, or changed launch state, revalidate
or refuse. Order `0x1e`'s established clear leg uses **current ship position
plus an F-only displacement** and does not restore a central gate point; its
current/horizon segment must use the same solids and frame. Endpoint-only
checks, or assuming that the full path is axial in a loosely accepted frame,
are insufficient, particularly for the long stock catapult tube and root solid.

Use a proven sphere when launch attitude is unavailable. A lateral hull width
is roll-invariant only around its own forward axis; a box or width-plus-length
certificate additionally needs the ship's actual attitude over the certified
path. This receipt does not establish that attitude invariant or small/capital
separation. Root and scene checks remain separate; combining them by minimum
hole would replace the useful scene hole with root hole zero.

## 4. Caller scope, lifetime, cost and remaining evidence

The inspected native pair adapter has two direct E8 call witnesses:
`0x4a5ff7` in recursive tree collision `0x4a5ef0`, and `0x4a6395` in the
two-tree pair path `0x4a6270`. Recursion calls itself at `0x4a6099`; its outer
entry is called at `0x4a623d` from `0x4a61a0`. Those operand anchors establish
these callers, not an exhaustive computed-call census. The native adapter and
kernel write global scratch (`0x4984bf..4ff`, `0x51ab01..1f`); do not call them
from the injected helper or assume they are reentrant/read only. Native model
lookup `0x4a1a40` is also a mutable loader, not a safe read accessor.

Reuse the arrival seam's complete GPR/XMM/MXCSR/LastError envelope and exact
instruction boundary; no new hook liveness or unwind guarantee is introduced.
All engine/model reads are temporary synchronous borrows, with no engine call
while borrowed and no lock/allocation. Return and release those borrows before
native launch can reach callbacks. A foreign exception must not cross the
helper/naked frame; the earlier receipt's refusal, rollback and exception
limitations remain. Mathematical host checks introduce no native exception path.

The proposed cost is a bounded source/node validation pass plus scalar copies,
then scalar clearance work. Deduplicate source scales and body norms in cold
preparation. Reuse existing board storage/generation; avoid resource file reads,
loads, model-pointer retention or full descendant validation per warning.
Pose changes must still invalidate the certificate. No game-time cost or
loading-time benefit was measured.

Still required before this becomes an operational certificate: cold canonical
body/scene identity to live integer-ID matching and refresh/publication;
complete FL tree-reader field/flag qualification; a concrete checked Q and
projection implementation with numerical evidence; the relative collision
kernel's numerical allowance/MXCSR assumptions; launch hull attitude and actual
warning/avoidance horizons; native/callback/refusal fixtures, useful placement
and user-supplied FL flight evidence. Stock exact scale support is established
as a bounded source contract; universal transformed geometry, native Windows
execution and live FL traffic acceptance are not established.

## 5. Native numerical prerequisite and frozen root-owned probe

The next bounded trace follows `0x4982d0 -> 0x51aae0 -> 0x51a6c0` (OBB walk)
`-> 0x51a260` (relative leaf preparation) `-> 0x51b2a0` (triangle SAT), whose
projection helper is `0x51b160`. The independently decoded closure is nine
functions / **2594 instructions, 27 direct call sites and 36 absolute data
operands** (measured). There are no external/indirect calls, allocation,
callbacks, x87 operations, MXCSR-control instructions, or FS/SEH references in
that closure. The [same receipt producer](../../../verification/results/fl-gate-transform/verify.py)
records the additional scoped regions and dependencies without replacing the
earlier 89 anchors or claiming native execution.

The top kernel computes a relative rotation from the two raw basis matrices,
relative position via the second basis transpose, and reciprocal model scales
(`0x51ae9d`, `0x51af60`). The leaf transforms the first triangle into the
second model's coordinates using these globals (`0x61b988..b8`). This is not a
general inverse for a sheared/short raw matrix. The existing Q residual is
therefore material in both operand orders, including when the small root scale
47 is the denominator. The single-precision reciprocals also inherit native
rounding mode; nominally coincident zero-margin planes are not a reliable
positive control under every directed mode.

Triangle SAT performs **17 axes: five inline projection tests and 12 calls to
`0x51b160`**, with strict projection separation. `0x51b778/7b`, for example,
compares min/max projections and branches to miss only for `min > max`.
There is no early degeneracy guard, axis normalization or fixed distance
epsilon in `0x51b2a0..0x51c6cb`. The apparent constant at `0x591fa0` is the
`0x80000000` sign mask, not an epsilon. Post-hit division at `0x51a57c` belongs
to reporting/filtering when adapter flags include `0xc`; the ordinary probe
uses flags 2 and bypasses it.

The preceding OBB test `0x51c6d0` **does** pad absolute relative rotation by
float `1e-6` (`0x591988`, loaded at `0x51c6e5`, additions `0x51c70a..790`).
Its first six axes and nine cross axes can reject a pair before SAT. The walk
calls it at `0x51a744`, returns on nonzero separation (`0x51a74c/74e`), and
reaches the triangle leaf only after both child-pointer pairs are zero
(`0x51a750..76a`). A zero normal in the triangle test consequently does **not**
prove an actual all-space collision or that a stock BVH reaches that leaf.

A fresh host reconstruction of exact signed-word source coordinates finds the
following exact 3D degenerates in the possible-cache triangle union. These are
integer cross-product tests, not a floating near-zero threshold.

| Source | Candidate triangles | Exact degenerates |
| --- | ---: | ---: |
| Root body 0 | 60 | 0 |
| Argon | 42859 | 0 |
| Destroyed Argon | 20478 | 2 |
| Catapult | 57775 | 137 |
| Terraformer active / inactive, each | 37782 | 16 |
| Terran | 120174 | 88 |

The concrete unresolved operation is the **raw, unnormalized SAT projection
comparison after reciprocal/transpose relative transformation**, conditioned
by the OBB guard. A length-only forward-map allowance does not yet bound loss
of a separating axis for a degenerate/poorly conditioned pair. The scalar
profile alone contains neither an axis-conditioning certificate nor the actual
native-built leaf bounds. The trace has not established a sufficiently small
uniform kernel allowance; it has also not established that the degenerates
produce a reachable false hit. Do not silently drop these stock triangles,
restrict admission to a cardinal rotation, or relabel 2048/8192 as a completed
kernel error budget.

### Probe input and exact source/build/run contract

The prepared [input producer](../../../verification/probe/fl_gate_kernel_inputs.py),
[fixture](../../../verification/probe/fl_gate_kernel_fixture.cpp),
[builder](../../../verification/probe/fl_gate_kernel_build.py) and
[native runner](../../../verification/probe/fl_gate_kernel_run.py) remain verification
assets. **This agent generated/validated inputs and source metadata only; it
did not compile the PE or execute native code.** The root owns those operations.

The [compact input receipt](../../../verification/results/fl-gate-transform/kernel-inputs.json)
records **10363 cases**: all 259 exact degenerate source candidates, four distinct
nearest-hole nondegenerate witnesses from each of seven root/scene sources,
12 faces of a sphere-enclosed box at native remaining times 0/450/900, and
three canonical hit/miss/degenerate controls. The source triangles are freshly
recovered through the accepted effective-resource resolver/oracle. Raw geometry,
case lookup metadata and binary input stay under the local cache, outside the
repository. Case payload SHA-256 is
`074af38bca943a6ffd37ed8c5d20a9d8966728a13a4d8510912b2d7bc7c342af`.

The synthetic pose has nonzero 23/17/11-degree rotations rounded to Q16 and
translation `(100000000,-200000000,300000000)` engine units. The box has 50000
unit half-edges and lies inside a 90000-unit sphere. The entire component sweep
passes the existing pure checker with 8192 units of geometric margin. This is
a **positive stock physical-clearance control**, not an established native
error budget. Destroyed-gate cases use exterior separation for its scene only;
the missing root is still unknown. Root and other stock scenes use their own
known component profiles. No source family is silently removed from the probe.

The fixture maps only the nine original function spans at their original VAs;
its own PE links at `0x10000000`. Thus relative calls and absolute operands need
no relocation. Named constant/scratch pages are `0x591000`, `0x5a9000`,
`0x5aa000`, `0x61b000`; virtual-only BSS is zero-filled. All other reserved
image pages stay NOACCESS, unused code bytes are INT3, and code/constant pages
are protected after copying. It reads the actual EXE at initialization without
loading imports or its entry point; no copyrighted executable bytes are tracked.

For each source triangle it constructs an enclosing identity-basis **single-leaf
AABB**, not a claimed reconstruction of the stock native-built BVH. Native node
layout is float basis `+0..20`, centre `+24..2c`, half extents `+30..38`, null
children `+3c/+40`, triangle pointer `+44`. The triangle record is 40 bytes,
ID then Y/X/Z vertices at `+4/+10/+1c`; tree root is `+0`, built state `+14=3`.
Two fake models point from `+5c` to these trees. The actual adapter receives
ECX=node A, EDX=node B and six caller-cleaned stack DWORDs:
`(modelA,modelB,flags=2,maxcontacts=1,float_metric=0,null_output)`.
Offsets `0x4982d0/2db`, `0x4984a6/4bb/4ce/4f8` establish those inputs.

After the full adapter call, the fixture retains the **actual native-produced
relative transform globals** and calls actual leaf wrapper `0x51a260` with
ECX/EDX pointing at the two leaf nodes. That wrapper prepares points and calls
actual `0x51b2a0`; no second implementation of relative arithmetic is compared
with itself. Resetting only the hit count distinguishes a leaf-only positive
from a full adapter positive. A full hit without the same leaf hit is a fixture
invariant failure. Both operand orders are required.

Supported probe controls mask all six FP exceptions, preserve/restore exact
incoming MXCSR, and test **all four rounding directions times all four DAZ/FTZ
combinations**, subject to the CPU's FXSAVE MXCSR mask. Sticky exception status
may change in native work and is captured separately; no round-to-nearest
assumption is made for the tested functions. Unsupported DAZ bits yield explicit
skips. Unmasked precision/other exceptions remain outside a supported runtime
certificate. Native stack cleanup, EBP/EBX/ESI/EDI preservation and MXCSR control
preservation are checked after each call. There are no engine borrows, hooks,
callbacks, installation, game loading, or cross-frame state in this fixture.

Frozen local inputs are `/home/asvetl/.cache/x3m-port/fl-gate-kernel-inputs-v2`.
The root's exact commands are:

```sh
python3 verification/probe/fl_gate_kernel_build.py --output-dir /home/asvetl/.cache/x3m-port/fl-gate-kernel-native-v1
python3 verification/probe/fl_gate_kernel_run.py --build-dir /home/asvetl/.cache/x3m-port/fl-gate-kernel-native-v1 --inputs-dir /home/asvetl/.cache/x3m-port/fl-gate-kernel-inputs-v2 --output verification/results/fl-gate-transform/kernel-native1.json
```

The build command uses `i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra
-Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static
-Wl,--image-base,0x10000000,--disable-dynamicbase,--disable-large-address-aware`.
The builder freezes all four probe source hashes, `tools/media_package.py` and
`verification/probe/game_guard.py`, and the binary/toolchain; the runner
checks those plus input producer/payload/metadata and original EXE before/after
execution. It uses native WSLInterop only, checks that the game is closed, and
never searches for Wine. Full 16-mode coverage gives **331616 paired leaf/full
observations** (inferred target count). Exit 1 means setup/invariant failure;
exit 2 retains a material expected-clear/hit counterexample for review. A native
observation remains a bounded witness, not a proof over every time, pose, hull,
tree or coordinate within a proposed range.

The runner requires exactly one complete summary line, the frozen 10363-case
count, all 16 FP modes and 331616 observations. The canonical positive must
produce **32 hits** (both orders in every mode); the two canonical negative
controls must stay clear. Stock clear controls must stay clear for a bounded
pass; any stock full-path positive is retained as exit-2 counterexample, with
the local case ID and corresponding leaf result. The 28 stock zero-margin
overlap controls are observations, not required hits in all directed modes.
Duplicate/truncated/extra summaries, incomplete modes, failed canonical controls
or mismatched return status are setup/coverage failures. Host checks cover the
summary acceptance and counterexample cases; the retained
[parser producer](../../../verification/results/fl-gate-transform/check_kernel_parser.py)
and [receipt](../../../verification/results/fl-gate-transform/kernel-parser.json)
cover two accepted and eight rejected summaries. Runtime is unmeasured; the native
runner has a 60-second bound, so a timeout requires reporting/profiling rather
than guessing completion or silently lowering coverage.

### Native1 setup failure and mapping correction

The root compiled the first fixture successfully. The retained
[`kernel-native1.json`](../../../verification/results/fl-gate-transform/kernel-native1.json)
and local log report `SETUP_FAIL reserve original base error=487`, exit 1,
**before any cases/native collision calls**. Source, original image and payload
hashes stayed stable. This is mapping/setup evidence, not a kernel counterexample.
Host header inspection confirms that fixture's preferred base `0x10000000`,
image size `0x172000` and disabled DYNAMIC_BASE matched its link command; a
wrong linker base is not supported by that evidence. Error 487 alone does not
identify which runtime region conflicted.

The first fixture unnecessarily tried to reserve all `0x391000` bytes from
`0x400000` through `0x791000`. The actual nine code/constant/scratch pages
occupy only five Windows 64 KiB allocation banks: **`0x490000`, `0x510000`,
`0x590000`, `0x5a0000`, `0x610000`**. The correction reserves those banks only
(327680 bytes total), commits/protects the same nine needed pages, and leaves
the other pages in each acquired bank NOACCESS. It never writes or replaces an
existing allocation. Native bytes, calls, input cases and acceptance counts
are unchanged. A bounded read-only VirtualQuery walk of the old extent prints
occupied regions and the actual fixture module address before reservation;
a failed named bank prints its current region/base/type. Native2 must establish
the actual runtime conflict or successful mapping; that observation is pending.

The six first-build source inputs were copied after matching the retained
build hashes to
`/home/asvetl/.cache/x3m-port/fl-gate-kernel-native1-source/`; the first executable,
build record, failed run and log remain untouched. The corrected freeze and
commands in `verification.json.kernel_source_freeze` use a fresh build/result:

```sh
python3 verification/probe/fl_gate_kernel_build.py --output-dir /home/asvetl/.cache/x3m-port/fl-gate-kernel-native-v2
python3 verification/probe/fl_gate_kernel_run.py --build-dir /home/asvetl/.cache/x3m-port/fl-gate-kernel-native-v2 --inputs-dir /home/asvetl/.cache/x3m-port/fl-gate-kernel-inputs-v2 --output verification/results/fl-gate-transform/kernel-native2.json
```

The child reran only host receipt/source-freeze, page/bank arithmetic, Python
compilation and factual/link/diff checks. It did not compile or execute the
corrected PE. The numerical prerequisite and runtime acceptance remain open.

### Native2 address conflict and audited fixture-only relocation

The retained [`kernel-native2.json`](../../../verification/results/fl-gate-transform/kernel-native2.json)
and local log again record an exit-1 setup failure before cases. Its VirtualQuery
output establishes an existing **PRIVATE** allocation at `0x00400000`, reserved
through `0x005f9000`, followed by committed guard/RW regions. The required
`0x00590000` bank overlaps that existing reservation; the fixture refused to
replace it. The TEB was not inspected, so this receipt does not name that
allocation a stack. Source/image/payload were stable. All six native2 source
inputs were preserved after hash comparison at
`/home/asvetl/.cache/x3m-port/fl-gate-kernel-native2-source/`; its binary/build
record/failed result/log remain untouched.

The next fixture uses a single explicit native-code base **`0x20000000`**,
common delta **`0x1fc00000`**. Its five acquired banks become `0x20090000`,
`0x20110000`, `0x20190000`, `0x201a0000`, `0x20210000`. It attempts those banks
once and refuses a conflict; there is no address search, original-allocation
write, game-file patch or broad PE loader. The fixture entry addresses and
reads of native scratch globals use the same common delta. Source tree/triangle
argument pointers remain ordinary fixture pointers. Floating coordinates,
scales, bases, thresholds and FP control inputs are unchanged.

The [relocation producer](../../../verification/probe/fl_gate_kernel_relocate.py),
[derived fixup header](../../../verification/probe/fl_gate_kernel_fixups_inc.h) and
[audit result](../../../verification/results/fl-gate-transform/kernel-relocation.json)
classify every image address in the nine function spans. The earlier **36**
figure counts distinct absolute memory targets, not encoded occurrences.
There are **80 absolute-memory displacement sites plus one immediate pointer
load** (`mov ecx,0x61b994` at `0x51ae61`): **81 four-byte fixups** referring to
the same 36 distinct targets. Omitting the immediate pointer would leave the
relative-matrix output writing to the old image address.

Every header entry holds derived instruction VA/length, operand offset, expected
original target and full-instruction FNV fingerprint. No executable bytes are
stored in the header. Before any patch, the fixture validates all 81 complete
original instructions and four-byte operands, source-span containment and named
data-page membership. It then adjusts exactly those operand fields. The builder
binds the producer/header/audit hashes and original EXE provenance; the runner
requires exactly one successful relocation marker in addition to the existing
summary and control checks. Opcode/operand mutation witnesses are rejected by
the host audit and runtime verification contract.

The host audit decodes **all 2594 original and relocated instructions**, checks
every mnemonic/operand against exactly the expected address delta, and verifies
that **96 relative control operands** need no encoded-byte change. Both code
streams have identical bytes outside the 81 four-byte address fields. Their
SHA-256 after masking only those fields is
`bee3e6fd68e1297a8dc5b14c4072bfb8f3224e9a7051e9ba385b38267078e4be`.
Thus arithmetic instructions, registers, branch conditions, scale/threshold
constants and relative control flow are unchanged. The data pages are copied
unchanged (BSS zero-filled); only their addresses move. This is an explicitly
**relocated standalone execution of the native kernel**, not evidence that the
running game's executable relocates or that a production hook may rewrite it.

The source freeze now binds eight files (four probe files, relocation producer
and generated header, plus the two game-closed guard dependencies). Inputs and
expected native observations remain unchanged. Root-only commands are:

```sh
python3 verification/probe/fl_gate_kernel_build.py --output-dir /home/asvetl/.cache/x3m-port/fl-gate-kernel-native-v3
python3 verification/probe/fl_gate_kernel_run.py --build-dir /home/asvetl/.cache/x3m-port/fl-gate-kernel-native-v3 --inputs-dir /home/asvetl/.cache/x3m-port/fl-gate-kernel-inputs-v2 --output verification/results/fl-gate-transform/kernel-native3.json
```

Host relocation/decode/byte audits, frozen source checks, Python compilation and
link/diff checks passed. The child did not compile or execute this correction;
relocated native execution and the numerical prerequisite remain pending.

### Native3 bounded result and interpretation

The root's relocated native build/run succeeded. The retained
[`kernel-native3.json`](../../../verification/results/fl-gate-transform/kernel-native3.json)
binds the eight frozen inputs, executable, original FL image, relocation audit
and local case payload. All remained stable. The
[`kernel-interpretation.json`](../../../verification/results/fl-gate-transform/kernel-interpretation.json)
is derived by the existing `verify.py`; it contains counts and attribution,
not raw geometry or a copied log. Root process wall time was **0.272639605 s**
(measured, including fixture setup; not a game or per-frame timing).

| Native observation | Count (measured unless marked inferred) |
| --- | ---: |
| Cases / MXCSR modes / paired leaf/full observations | 10363 / 16 / 331616 |
| Required canonical positive hits / failures | 32 / 0 |
| Expected-clear observations / full-path hits | 330688 / 0 |
| Stock component clear observations | 330624 |
| All full-path hits | 168 |
| Zero-margin stock overlap observations | 896 |
| Their full-path hits / misses (inferred by accounting) | 136 / 760 |
| Leaf-only positives / full-hit versus leaf mismatch | 32 / 0 |

The 168 full hits consist of 32 required canonical hits plus 136 observations
from the 28 stock zero-margin overlap cases. The latter were deliberately not
required to hit in every directed mode. Nearest/down/up each produced 40 full
hits across their four DAZ/FTZ combinations; toward-zero produced 48. This
variation confirms that zero-margin coincidence cannot establish a numerical
allowance. There were no expected-clear hits to classify as stock collisions.

Each mode reported two leaf-only positives. The input's case 2 has two repeated
point triangles, identity bases, scales 65536 and an exactly representable
1000000-unit separation. Both operand orders produce finite repeated relative
points at distance `1000000/65536 = 15625/1024`; all edges and all 17 SAT axes
are exactly zero, so leaf projections overlap. Their enclosing OBBs are instead
separated on the first axis. That control therefore accounts for all **32**
leaf-only observations. This attribution is **inferred from exact control
algebra plus the measured per-mode aggregates**; the native log does not record
leaf-only case IDs. Under that accounting no stock leaf-only positive remains.
The result establishes the expected distinction between degenerate SAT and
the preceding OBB guard, not an actual stock all-space collision.

The actual stock geometry scope is seven root/scene contexts, representing
316910 possible-cache source triangle instances: all 259 exact degenerate
candidates plus four distinct nearest-hole nondegenerate witnesses per context
were exercised. Active/inactive Terraformer contexts share a model payload;
those context counts do not imply distinct meshes. hsap contributes no scene
triangles and still has the tested root body 0. Destroyed-gate controls certify
individual scene exterior separation only; its missing root stays unknown.

Each tested pair uses the original quantized source triangle, a synthetic
enclosing **single-leaf AABB**, one noncardinal rounded Q16 pose, one world
translation, and one of 12 sphere-enclosed box faces at three native warp times.
This is actual native adapter/relative preparation/OBB/SAT execution on those
inputs. It does not reproduce the native-built BVH, every gate/source triangle,
continuous centre/hull/basis domains, arbitrary ship geometry, actual arrival
attitude or warnings. It does not admit a runtime profile or close the uniform
numerical-allowance requirement.

The next parent-owned decision can stay finite: choose a **gate-only policy
domain**, then require one proof-producing numerical certificate for the exact
relative preparation and primitive exclusion in this receipt. A concrete
candidate proof target is all 25 currently complete stock profiles, exact source
scales/unity axes, `|world coordinate|<=2^30`, relative node displacement
`<=2^24` units, positive model scales `47..2^24-1`, a copied bounding-sphere reach
`<=1000000` units, raw bases with checked `||B-Q||_F<=2^-12` and a right-handed
orthonormal Q, and the same
16 masked-exception FP modes. These are proposed proof inputs, **not admitted
runtime ranges**; the parent must ratify or adjust the residual/placement bounds.

Acceptance must cover every possible-cache source triangle and the entire
native coefficient interval `0..1348650`, both operand orders, and either prove
SAT exclusion or prove the native OBB guard excludes a degenerate case. It must
produce one outward scalar allowance and retain the existing radius-100000,
lateral-200000 stock positive sweeps after inflation; **8192 units is a proposed
maximum useful proof target, not an established margin**. A concrete unresolved
feature/OBB-frame condition or counterexample is a failed certificate with its
source identity, not permission to drop a source family. The present fake-leaf
run cannot supply authentic native leaf-frame bounds if that proof needs them.
This defines the next bounded evidence obligation without implementing an
interval framework, adding runtime mesh work or expanding this checkpoint.

Independent bounded RE review reproduced all evidence fields except timing and
accepted the stated static contract. It also checked all six solid-part norm
bounds with 60-digit arithmetic. The forward-transform bound remains separate
from unqualified native collision-kernel/MXCSR and live identity admission; no
full geometry-safety certificate follows from this checkpoint.

The subsequent independent native-probe review accepts the bounded execution
evidence, relocation and ABI checks. It reproduced 2,594 decoded instructions,
81 fixups, 36 targets and 96 relative controls, verified the native3 source,
binary, input and log hashes, and checked all 10,363 input records and 16 unique
MXCSR modes. Its sole evidence-retention finding is closed by the linked
10-case parser producer. No native rerun or production change was required.
The numerical-policy proposal above remains a parent-owned design decision.
