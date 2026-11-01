# FL stock ship enclosure through the 900 ms arrival interval

2026-10-30. This is a finite follow-up to sections 6–7 of
[live bounds](gate-live-bounds.md), scoped to the stock
`SS_SH_A_M2` source and the ordinary arrival, angular integration, source-key,
turret and world-publication writers. It derives a useful geometric sphere;
it does not add a production reader, a transform observer, or a universal
collision-kernel theorem. Actual native collision corroboration and flight
acceptance remain required.

The [host producer](../../../verification/results/fl-ship-enclosure/verify.py)
and [receipt](../../../verification/results/fl-ship-enclosure/evidence.json) bind
the original FL EXE and the existing complete stock geometry receipt. Run:

```sh
python3 verification/results/fl-ship-enclosure/verify.py
python3 -m py_compile verification/results/fl-ship-enclosure/verify.py
```

No PE code, native fixture, Wine process or game was executed. Existing raw
decompiler listings were read from `~/.cache/x3m-port/fl-gate-live-bounds/`;
new instruction checks use the project's `compare_fl_engine` PE/Capstone
reader. No raw executable bytes or source resources are emitted.

## 1. Root attitude has a bounded numerical invariant

Native arrival constructs the initial ship attitude at `44d6b9/44d6d2`, before
the existing `44d729` borrow. Therefore **copy and bound the actual initial
root matrix**; neither a loaded matrix nor the gate's approximate frame guard
is assumed orthogonal. Put `Q=65536`, and, for the nine signed raw root words,

`R = max(ceil_sqrt(sum(Broot_raw[i]^2))/Q, 113514/Q)`.

The root's operator norm is at most this Frobenius upper bound. A practical
reader may admit `R<=2` and refuse this individual read otherwise; the stock
controls below use both `R=113514/Q` and `R=2`. This is a magnitude domain,
not a capital-ship exclusion. Check component limits before integer squares
and sums, or use checked wide arithmetic. Do not square arbitrary signed32
words in a signed64 accumulator.

The normal integrator at `463d26..463e38` either skips a zero angular update
(`463d34`) or composes into root `+40` at `463de3` and immediately normalizes
that same matrix at `463df1`. The angular-offset work at `463d88/463d9b/
463db1/463dc7` precedes this final normalization. Its intermediate matrix is
not a separately published collision pose. Flight basis `shipinfo+870` is
copied or separately normalized at `463ebe`; this does not imply frozen
physical attitude. Order `0x21` writes positions and the final snap, not a
new root basis. The no-angular branch preserves the already bounded matrix.

`50e8f0..50ebd9` converts signed32 row components exactly to binary64,
normalizes row 2, constructs/normalizes row 1, then row 0. Zero input rows
remain zero. Each nonzero normalization computes `v*(1/(sqrt(sum(v*v))/Q))`
and applies `CVTTSD2SI` per component. Three squares, two sums, square root,
power-of-two scaling, division and multiplication have relative norm error
below the deliberately loose `2^-45` for every SSE rounding direction in
this signed32 input domain. There is no underflow/overflow in this floating
normalization. Truncation cannot enlarge a computed component's magnitude.
Thus each stored row has norm below `Q+1`, even if an earlier integer cross
product wrapped before becoming this routine's next signed32 input.

Consequently the normalized matrix has
`||B/Q||_F <= sqrt(3)*(Q+1)/Q < 113514/Q = 1.732086181640625`.
This proof deliberately needs no orthogonality claim. Initial copied bounds
and the normalizer close the invariant under any number of these native
updates during all 900 ms; there is no guessed frame count or accumulated
per-frame epsilon. An unrelated script/setter/reparent/replacement is outside
this finite writer scope and must revoke/refuse through the existing owned
binding and decision validation, not inherit the normal-update certificate.

## 2. Actual stock turret and source-key branches

The prior note correctly found `450800..451422` rebuilds a fresh attitude
into mesh node `+40`; it does not integrate the previous animated matrix.
There is a material branch refinement: `451252` tests the dummy flags' bit 2.
The stock `GUNF_ROTATE` record at `578cb0` is **2**, so stock uses
`45125c -> 50f4e0(node+200, temporary)` and jumps to `451359`, rather than
the authored Euler branch `451338 -> 50d400`. The original blanket statement
that authored reconstruction always passes through `50d400` is too broad.

`50f4e0..50f643` takes ECX=input quaternion, EDX=output matrix. Node `+200`
is `(w,x,y,z)`, four signed Q16 words, on this branch. Both `(Q,0,0,0)` and
`(-Q,0,0,0)` produce the identity matrix **exactly**, including all rounded
products. The stock substituted body/weapon attitudes are identity. Validate
the exact authored quaternion and quaternion mode, rather than interpreting
these four words as Euler angles. Composition with that exact identity through
`50ebe0` reproduces the generated rotor exactly.

The no-gamma rotor at `45130d..451333` passes zero for gamma and uses only
alpha/beta flags 1/2; stock has no flag 4. `50d400` uses the low unsigned16 of
each input. The producer translates its actual CRT cosine/sine reduction,
table and polynomial (`419d80/419d90 -> 54a352/54be84`) into scalar host
arithmetic, outward-enclosing every binary64 operation. It enumerates all
**65,536 angle words × four MXCSR rounding directions**, or **262,144 pairs /
524,288 scalar outputs**. Binary32 rounding and table-index selection are
explicit. Only input zero uses the tiny path; no large-argument/x87 slow path
is reached. Measured outer scalar intervals are:

| Function | Minimum | Maximum |
| --- | ---: | ---: |
| cosine | -1.0000001192092896 | 1 |
| sine | -1.0000001192092896 | 1.0000001192092896 |

For no-gamma Euler matrices, four entries are single trig values, four are
products, one is zero. With the looser scalar cap `t=1.001`, the squared
Frobenius bound before integer truncation is
`4*t^2 + 4*t^4*(1+2^-23)^2 < 8.025 < 9`.
Multiplication by Q is exact and CVTT shrinks magnitudes. Therefore **rotor
Frobenius <=3**, with margin, under all four rounding directions. This bound
applies to every future stock rotor, not only sampled current angles.

The source-key writer is `4a98b0..4aa74e`, ECX=node, two callee-cleaned stack
arguments (`ret 8`). All 24 mount wrappers have exactly one constant key.
Their quaternion branch directly copies the immutable loaded key at
`4a9d43/4a9d51` and rebuilds at `4a9d54`; repeating the same loaded input is
bit exact. Validate their copied local Frobenius norm **<=2**, exact source
offset and ordinary publication. There is no need to reconstruct a source
rotation from rounded decimal authoring values at decision time.

The hull and moving turret meshes also have repeated identical identity keys.
For their plain `0x2002` keys, interpolation uses `4a8f40 -> 50fae0`. Equal
identity quaternions give exact dot 1, native `acos(1)=0` through
`548ffa -> 549029 -> 54906c`, then `sin(0)=0` through `54be23..54be50`, and
the slerp copies the first quaternion exactly at `50fc5b/50fc5e`. Key endpoint
selection also copies it. Thus source-key replay cannot introduce a new
stretch or move these constant pivots. This statement covers these equal
keys; it is not a general interpolation or animation proof.

Do not label the entire source-key writer callback-free. Its completion tail
can call `4aa737` indirectly when node `+1f4` bit `0x40` enables an event;
bit `0x1000` enables copying into another target. `4a8a10` can additionally
apply a scene-level transform when scene `+54` bit 1 is set. The finite ordinary
profile needs those paths disabled, or separately copied and qualified actual
values; default to refusing the individual unsupported path. These are narrow
validation requirements in the existing borrow, not proposed new update hooks.
The source `0x2002` channels and complete constant-key receipt supply no scale,
tracking, target or arbitrary event transform authority.

## 3. Radius recurrence includes native integer publication

For row-vector Q16 composition, native `50ebe0` independently rounds each
product with `r16(x)=floor((x+32768)/Q)` and then sums three signed words.
Each entry differs from the real matrix product by at most `1.5/Q`, so its
operator error is bounded by its Frobenius error `eM=9/(2Q)`.
`50e0c0` similarly gives a vector-position rounding error
`eP=3*sqrt(3)/2 < 2.6` engine units. Under unity axes, unscaled offsets and
the ordinary composition branch at `494370..494533`, use:

`Wroot=R, Droot=0`

`Wchild <= Lchild*Wparent + eM`

`Dchild <= Dparent + |local_offset|*Wparent + 2.6`

`ship_radius = ceil(max_j(Dj + Wj*rho_j))`.

Here L is a copied/qualified local Frobenius bound, W bounds world operator
norm, D bounds distance to the ship root centre, and rho is a prepared outward
body-centred source radius using the actual raw scale. Identity composition
is exact: use `Wchild=Wparent` in that case, with no fictitious matrix error.
Root world basis/position copies at `495220..49524e` and each child's separately
rounded publication must agree with the copied local data. No stale resolved
pose is admitted merely because a magnitude guard passes. In the stated
`R<=2`, mount-L<=2 and rotor-L<=3 domain, raw child basis components are below
13Q; relative body/offset products and their 64-bit intermediates have ample
range. **Absolute 32-bit centre additions require a separate no-wrap
precondition.** Native `4944a2/4944af/4944bc` uses wrapping ADD to combine
rotated local displacement with parent world position; a small relative
offset does not make this safe near a signed32 endpoint.

The producer records the concrete failure: identity root basis,
`root_z=2147483647`, and the stock hull's `local_z=17329` publish child
`z=-2147466320`. The child centre is then **4294949967** units from its root,
outside either proposed sphere. The root trajectory can remain signed32
through all 900 ms by warping along X; this is specifically a child-centre
wrap, not merely a bad root endpoint.

For every visited node j, including bodyless parents, helpers and excluded
cameras, compute `ceil(Dj)` in checked wide arithmetic. Let `root_min[i]` and
`root_max[i]` enclose the actual root coordinate over the complete interval,
**including native root-trajectory rounding and any separately admitted
movement**. Require for every node and axis:

`INT32_MIN <= root_min[i] - ceil(Dj)`

`root_max[i] + ceil(Dj) <= INT32_MAX`.

Apply this guard to **every intermediate parent publication**, not only the
98 model-bearing collision bodies or the final leaf. With root publication
in range, the guard and the D recurrence establish no-wrap inductively down
the tree. A wide guard failure refuses the individual certificate; do not
narrow or wrap the guard arithmetic itself. Existing exact current-publication
checks remain required, and the guarded future interval must be the same
trajectory the controller authorizes.

For the fixed admitted gate forward row F and native scalar range
`0<=d<=1348650`, the coordinate `P[i]+r16(d*F[i])` is monotone. Its exact
wide-integer extrema are the minimum/maximum of `P[i]` and
`P[i]+r16(1348650*F[i])`. These rounded endpoints bound every continuous
coefficient and all 901 native integer-millisecond values. The producer
also checks a noncardinal F so endpoint rounding is exercised. Additional
controller movement must expand these extrema before admission; no new
observer or update hook is proposed.

Reuse six prepared source body facts from the prior complete enumeration:
root signed16 fallback 163; hull corroborated AABB+4 sphere 574367; socket
signed16 fallback **17809** because its authored bounds are all zero; body
16843; left weapon 18880; right weapon 18719. No source vertex payload is
walked at runtime. The socket's measured tighter 6998 is not needed. The
inferred tree remains 98 collision bodies / up to 152 total nodes; helpers
and excluded wrappers still require complete traversal and role validation.

Applying the recurrence to every mount/mesh gives these **derived**, outward
integer radii. The producer also checks the exact count of 98 body cases:

| Root operator bound | Largest turret | Hull and whole-ship maximum |
| --- | ---: | ---: |
| 113514/65536 | 844911 | **1024872** |
| 2 | 975597 | **1183395** |

Unlike the earlier 591696 comparison, these figures include root/mount/rotor
distortion and every applicable native integer position/matrix publication
allowance. They remain compact enough for useful outward placement. They do
not certify passage through the smaller stock gate aperture.

## 4. Forward numerical margin and useful outward controls

For a concrete host geometric control, use the current stock gate source in
a proper cardinal frame and move outward on its positive axis. The finite
forward FP32 domain is full signed32 C, raw scale <=528422, `|B_ij|<=13`, and
signed16 q with native normalization q/16384. Scale and raw-basis conversion
are exact; `2*528422*13 < 2^26` bounds each unrounded body-axis contribution.
With all RC modes, `u=2^-23`, `gamma8=8u/(1-8u)`, position conversion error
<=128, the per-axis forward affine error is bounded by
`128 + gamma8*(2^31+3*2^26)`; its Euclidean bound is below **4102**.
This extends the accepted [transform-math](gate-transform-math.md)
forward proof to this finite ship operand: raw bases can be larger than two,
but conversion is still exact and the same `3*2^26` scaled-product budget
holds. The FP operand proof retains the full signed32 position domain;
the separate absolute integer-publication guard above remains required.

Reuse the existing **8224** numerical reserve unchanged:
`2*4102 + 16 + sqrt(3)/2 < 8224` (two forward operands, comparison allowance,
continuous warp rounding). Child centre-publication rounding is already in
the ship radius above, so it is not added a second time. The warp scalar range is the full native
0..1348650, corresponding to all integer remaining times 0..900; checking
the whole outward segment covers that range. Use
`z_start=max(root_zmax,scene_zmax)+ship_radius+8224+2`, margin one.
All five prior rows remain positive for **both** root domains: ten controls.
Every control additionally passes the no-wrap guard for all **152 possible
node paths / 456 axis intervals**, including intermediate and nonbody nodes,
over its entire rounded root path. The negative wrap witness is rejected.
The receipt retains exact starts/ends, root extrema and producing arithmetic.

These controls include the proposed finite forward/integer margins, but are
still host geometric controls: they do not execute the native SAT, test its
relative-frame numerical behavior, or qualify arbitrary gate poses. The next
native corroboration should use actual stock collision inputs, the resulting
whole-ship radius and outward starts; no universal SAT theorem is a prerequisite
for the ratified conservative geometry + actual collision + flight goal.

## 5. Borrow, ABI, implementation and acceptance limits

This analysis adds no hook site. Reuse the actual arrival/E synchronous
borrows, table/resource provenance, owner thread, incarnation/load tokens,
complete traversal and copied-value publication contract. Copy the current
root matrix, immutable source join, local offsets/scales/axes/transform modes,
authored quaternion mode/value, bounded local bases, animation/source mode
and current resolved publication. Release every borrowed pointer before any
native continuation, callback or message loop. Cross-call values are copied
dimensions/stamps, not live node/model/source pointers.

The math helpers are not new callable services. `50d400` uses ECX=destination,
EDX=alpha, caller-cleaned beta/gamma stack words; `50ebe0` uses ECX/EDX=input
matrices and one caller-cleaned output pointer. Both realign their own stacks.
`50e8f0` is ECX=in/out and preserves EBX/EBP/ESI/EDI; `50f4e0` preserves those
registers and takes EDX=output. Their flags and scratch GPR/XMM state are not
preserved. None of the normalizer/composition/quaternion routines calls a
callback, changes MXCSR, allocates or supplies a new SEH boundary. The whole
turret routine remains ordinary engine execution, not an authority to nest a
reader or invoke collision scratch recursively. Existing complete CPU,
LastError, MXCSR, unwind/refusal and rollback contracts remain unchanged.

Remaining implementation checks are the complete finite source-profile reader
and its refusal witnesses (including optional scene/event/target modes),
checked numerical arithmetic and magnitude domain, board/E stamp/pose/order/
deadline validation, and nonempty native stock collision corroboration followed
by user flight. A changed quaternion, scale, parent, local offset, source mode,
copy-world transform or unmatched body must fail the individual read. No
runtime source fetch, triangle scan, per-warning allocation, general observer
or new gun-update hook is justified. Cold geometry is reused; the reader is
bounded by the existing node budget, with squared-norm checks and a small
fixed number of scalar recurrences. No game performance was measured.
