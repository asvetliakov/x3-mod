# Farnham's Legacy ordinary angular motion

Bounded static reconstruction, 2026-10-30. This note answers the angular
numerics needed by the full navigation port; it does not qualify a new
controller, motion allowance or native trajectory. No production edit, build,
native/Wine execution, game launch or install occurred. The AP engineering
plant remains an engineering approximation whose intended native discrepancy
must be measured over the actual accepted domain.

Original image `/mnt/c/X3FL-orig/X3FL.exe`, preferred base `400000`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
The [static producer](../../../verification/results/fl-angular-motion/verify.py)
and [receipt](../../../verification/results/fl-angular-motion/evidence.json)
retain **54 instruction assertions, seven gap-free ranges / 1,426 instructions,
and twelve derived scalar witnesses** (measured static counts). Run:
`python3 verification/results/fl-angular-motion/verify.py`.
The producer uses the project's PE/Capstone mapper, verifies instructions in
memory, and emits derived meanings/hashes rather than listings or image bytes.
Raw targeted listings remain untracked under `/tmp/x3m-fl-angular-motion/`.
The scalar transcription is evidence, not production simulation or runtime proof.

Reuse [gate-motion §2/3](gate-motion.md#2-current-position-and-the-next-native-translation)
for dt, physical velocity, carry and translation. Reuse the
[first general producer](lane-command-transaction.md#first-general-finite-command-producer--implementation-native-witness-pending-2026-10-30)
for its existing finite scope and unresolved native acceptance. Existing local
`/tmp/fl-lane-step-reviewed/lane-step-spans.local.json` identifies matching
scalar/Euler/compose/normalize ranges; that metadata describes a native slice,
not mixed-axis coverage. The earlier native-reviewed receipt's turn case is a
diagnostic failure witness and did not observe a curved first pose.

## 1. Per-axis cap, acceleration, time and scalar update

All equations below describe the ordinary autopilot, class-7, zero current and
target flight-offset branch, with representable intermediate native integers.
They do not establish attached/player/docked alternatives. Let
`T(n,d)` mean signed division truncated toward zero,
`R(n)=floor((n+32768)/65536)`, and `Q(n,d)=T(65536*n,d)`.
The helper `419e10` returns zero for zero divisor; explicit `idiv` operations
elsewhere do not generally add that protection. Dword additions, negations and
narrowed products follow native wrapping, not checked arithmetic. The numerical
witnesses deliberately avoid overflow, INT_MIN negation and division exceptions.

| Axis | Rate / remaining in object | Getter / type field | Consumer |
| --- | --- | --- | --- |
| yaw | `+24 / +30` | `45a830 / +08` | `463536` |
| pitch | `+28 / +34` | `45a7d0 / +0c` | `463433` |
| roll | `+2c / +38` | `45a890 / +10` | `463331` |

For class 7, each getter uses signed type index `obj+4a`, stride `db8`,
ship table `[5a83e4]`, and its field above. If `flight+270 != 0`,
`W_i=R(type_axis_i*(65536+flight270))`; otherwise the unmodified type value
is returned. Getter instructions are at `45a7da..45a80f`,
`45a83a..45a86f`, `45a89a..45a8cf`. Nonship fallback uses the class table
array `[5a83c8 + class*4]` and the same field/stride; it is not a zero rate.
These getters take ECX=object and return EAX.

`4632eb..463327` obtains rotation percent `type+d0` and raw Q16
`flight+2b0`, forming **`K=T(flight2b0*typeD0,100)`** with signed 64-bit
multiply and quotient. Each axis forms **`A_i=R(W_i*K)`**, then obtains
`W_i` again for the helper. Thus the two getter calls are separate reads;
the static trace does not make them atomic against external writers. The
non-class-7 multiplier is `65536` (`463329`). The ordinary native law does
not impose AP's nonnegative-dynamics admission.

The helper receives the same clamped object dt as translation: `4631bf/1c3`
store it in locals `+48/+44`. Calls at `463359`, `46345c`, `46355f` (and
negative equivalents) push local `+44`. Inside `444500`,
**`h=T(dt*65536,1000)`** is the Q16 time, not raw command step, and not an
unquantized floating `dt/1000`. Negative or oversized object deltas become
1000 upstream; zero remains zero as already established in gate-motion §2.

The outer consumers apply the following symmetric wrapper to scalar inputs
`d=abs(remaining)`, `v=sign(remaining)*old_rate`, `W`, `A`, `dt`:

- A zero remaining command sets current rate to zero and emits zero angular
  displacement: roll `463430`, pitch `46352f`, yaw `463779`. A nonzero
  old rate does not coast or perform a finite stop turn here.
- A nonzero command with `A==0` skips the helper, leaves remaining/rate
  unchanged and emits zero displacement. A negative nonzero `A` reaches the
  helper, which coerces its acceleration argument to one (`44454c`).
- Otherwise the scalar returns `(v1,s)`. If `d-s<=0`, clamp displacement
  to `d` and set rate/remainder to zero. Otherwise subtract the signed
  displacement from the signed remaining command and restore the signed rate.
  The negative command path negates **both** incoming rate and outgoing rate/
  displacement; it does not assume that current motion agrees with the command.

The scalar `444500..444818` is the following exact piecewise contract on this
no-overflow domain. Its inputs are `(d,v,W,A,*new_rate,*displacement,dt)`;
it preserves EBX/ESI/EDI/EBP, returns with `ret 1c`, and clobbers caller-saved
registers/flags. EAX is not an additional defined scalar result. Let
`a=max(A,1)`. If `W==0`, both outputs are zero (`44451a..44453a`). Otherwise:

1. **Reversal, `v<0`** (`444553..4445e5`). Set `t=-Q(v,a)`.
   If `t<h`, return `(0,R(t*v))`; else return
   `(v+R(h*a),R(h*v))`. The early-stop branch consumes no leftover time
   accelerating toward the new command. Its negative displacement grows the
   remaining distance in the original wrapper.
2. **Same-direction stopping decision, `v>=0`** (`4445e8..444621`). Set
   `t_stop=Q(v,a)` and **`b=R(t_stop*v)`**. If `b>d`, braking at
   `4447bf..444815` returns `(0,b)` when `t_stop<h`, otherwise
   `(v-R(h*a),R(h*v))`. The wrapper may then clip this displacement to the
   remaining command. There is **no division by two** in `b`.
3. **Acceleration/cap, `b<=d` and `t=Q(d-b,W)>=h`**
   (`444627..4446ba`). Set `t_cap=Q(W-v,a)`. If `t_cap>h`, return
   `(v+R(h*a),R(h*v))`. Otherwise return
   `(W,R((h-t_cap)*W)+R(t_cap*v))`.
4. **Acceleration then coast/possible braking, `b<=d` and `t<h`**
   (`4446bd..4447bc`). Set `u=v+R(t*a)`. If `u>=W`, set `u=W` and
   replace `t` by `Q(W-v,a)`. Set `d0=R(t*v)`,
   `c=max(d-d0-b,0)`, `tc=Q(c,u)` (zero when `u==0`). If `t+tc>=h`,
   return `(u,d0+R((h-t)*u))`. Otherwise set `tb=h-t-tc` and return
   `(u-R(tb*a),d0+c+R(tb*u))`.

These are piecewise constant-rate displacement segments, with separately
updated rates and quantized event times. “Trapezoid” in older prose describes
the speed profile loosely; it does not justify substituting
`0.5*(old_rate+new_rate)*dt` or the continuous stopping distance `v*v/(2*a)`.
The explanatory producer retains this branch derivation explicitly.

There is no fractional angular-displacement carry in the selected consumers:
integer displacement is subtracted from integer remaining; current rate is
retained separately. At dt=1 ms, `h=65` rather than 65.536. For a long
positive command, `v=1024,W=A=4096` emits one binary-angle unit and rate 1028.
From rest with the same plant at dt=16 ms, displacement is zero and new rate
66. These are derived instruction arithmetic, not measured native updates.

## 2. Mixed-axis composition, signs and normalization

The consumers run roll, pitch, yaw, using the same dt. They obtain independent
scalar increments; no cross-axis acceleration term occurs in these consumers.
When current offsets `flight+8a0/+8a4/+8a8` and desired offsets
`+8b8/+8bc/+8c0` are all zero (hence no offset evolution), the common path
supplies yaw in ECX, pitch in EAX,
roll in ESI (`463d26..463d45`). `50d400` receives ECX=output matrix,
EDX=yaw, stack pitch then roll, caller cleanup. Each angle is reduced modulo
65536 by unsigned word conversion (`50d409/43a/46f`); one full turn is 65536.
The float32 radians constant at `5919a8` is
`9.58738019107841e-05`. Float sine/cosine wrappers `419d80/90` feed
float32 operations, followed by scaling by 65536 and **truncation toward zero**
with `cvttss2si`; no nearest-integer conversion applies to this Euler matrix.

For real-valued sine/cosine notation `cy,sy,cp,sp,cr,sr`, the matrix is:

```text
D = [ cr*cy+sr*sp*sy,  sr*cp,  cr*sy-sr*sp*cy ]
    [ cr*sp*sy-sr*cy,  cr*cp, -sr*sy-cr*sp*cy ]
    [        -cp*sy,     sp,          cp*cy   ]
```

This identifies **`D=Roll(r)*Pitch(p)*Yaw(y)`**, with single-axis matrices:

```text
Yaw   = [ cy 0 sy; 0 1 0; -sy 0 cy ]
Pitch = [ 1 0 0; 0 cp -sp; 0 sp cp ]
Roll  = [ cr sr 0; -sr cr 0; 0 0 1 ]
```

For row bases `(right,up,forward)`, positive yaw turns forward toward
**-right**, positive pitch toward **+up**, positive roll turns right toward
**+up**. At `463de3`, `50ebe0` takes ECX=D, EDX=old root basis,
stack=destination root basis. It computes **`C[i,j]=sum_k R(D[i,k]*B[k,j])`**,
rounding each product separately and then adding with dword arithmetic.
The result is `D*B`, not `B*D`. Its temporary output makes the observed
old-basis/destination alias safe on this path; caller cleans the destination
argument. The analytic product above does not replace native float32 operation
order, trig implementation or integer rounding.

If all three total increments are zero, **`463d34 -> 463e38` bypasses
Euler creation, root composition, normalization and Euler-angle publication**;
it can still copy the existing root basis to flight. Thus a zero update does
not promise to repair an already imperfect basis. For a nonzero increment,
`463df1 -> 50e8f0(ECX=root+40)` normalizes in place in this order:

1. Normalize **forward** (row 2) to Q16 unit length using double squared
   length/sqrt/division, then truncate each scaled component toward zero.
2. Compute **up = forward × old composed right** (row 1); each cross-product
   multiplication uses `R` separately before subtraction. Normalize up with
   the same double/truncation rule.
3. Compute **right = up × forward** (row 0), with the same Q16 cross-product
   arithmetic; normalize right with the same rule.

Each zero row bypasses its own length division; this does not establish a
repair guarantee for a degenerate input basis. Row 1 from the original
composition is overwritten. Padding in the 16-byte row layout is not an
orientation coordinate. `463df6..463e0c` then derives object Euler fields
`+18/+1c/+20` from the updated matrix. The selected next update consumes
current basis/rates/remainders; those display angles are not a fractional
angular accumulator.

With **current** flight offsets `+8a0/+8a4/+8a8` all zero, the root basis
rows copy directly to flight `+870/+880/+890` at `463e63..463e7d`.
Nonzero offsets additionally use the offset composition/inverse route and
flight normalization (`463d75..463dd3`, `463e86..463ebe`); their coupling is
outside this zero-offset contract. This is a concrete scope predicate for a
first mixed-axis witness, not a statement that offsets can be ignored generally.

## 3. Mapping to AP and the minimum future numerical witness

AP `core::ship_angular_velocity` in `ship_steering_core.h` computes
**`omega_world=-w_yaw*up-w_pitch*right+w_roll*forward`**.
The infinitesimal generator of the three FL single-axis matrices above has
exactly those signs on a proper right-handed basis. The mapping is established
for instantaneous angular velocity and the single-axis differential; it does
not make a finite simultaneous exponential identical to FL's ordered Euler
increment. Mixed steps contain noncommuting cross terms and the root/flight
normalization and per-product rounding above.

AP `read_dynamics` maps native caps into radians/s, and computes
`k=float(flight2b0)/65536 * float(typeD0)*0.01` when its inputs are admitted.
Thus `W[i]*k` is the native acceleration **scale** in radians/s². Native first
truncates `K` by integer `/100`, then rounds `W_i*K` to an integer acceleration;
AP's float factor/product does not include those exact quantizations. Nor do
`ship_angular_velocity` and `W*k` by themselves specify the displacement law.
The current AP planar `plant_step_to_impl` updates rate first and advances
angle using **new_rate*dt**. FL's long-command no-cap branch advances with
**old_rate*quantized_dt**. This numerical difference does not establish that
the AP engineering envelope is insufficient; its full native discrepancy
inside the unchanged tracking/drift budget is still an acceptance obligation.
The horizontal producer also has no mixed pitch/roll composition, so its
single-axis matches cannot establish the general 3D predictor contract.

A minimum future original-instruction witness can reuse the existing slice
without a game launch. Parent owns execution and safety/queue decisions:

- Use class 7, ordinary autopilot, proper identity root and flight bases, zero
  current/target offsets, `flight270=0`, `flight2b0=65536`, `typeD0=100`.
  Make getter-model returns equal the type fields; keep their provenance
  explicit because the existing slice models getters instead of executing them.
- Set yaw/pitch/roll caps to `(4096,3072,2048)`, hence the same integer
  accelerations. Set remaining commands `(8192,-6144,4096)`, current rates
  `(1024,-768,512)`, clamped object dt=500 ms. Exact derived increments are
  **`(512,-384,256)`**, new rates **`(3072,-2304,1536)`**, remaining
  **`(7680,-5760,3840)`**. All three rotations survive integer quantization.
- Capture pre-Euler increments, raw increment matrix, post-compose matrix,
  post-normalize root and flight matrices, new rates/remainders. Compare each
  original phase to the established arithmetic, and separately compare the
  shared AP 3D engineering actuator prediction and whole-hull discrepancy.
  A mismatch must identify the failing phase; do not enlarge the allowance.
- Add only two necessary branch controls: zero all remaining commands while
  keeping nonzero rates (zero increment and cleared rates); then reverse pitch
  using remaining `-6144`, old rate `+768`, dt=250 ms, same pitch plant.
  Derived pitch increment is **+192**, new rate zero, remaining **-6336**.
  This checks initial braking against command direction. If it is refused by
  production admission, retain it as a scalar diagnostic rather than force it
  into the accepted lane domain.

The receipt's analytic matrix uses real double sine/cosine only to exhibit
rotation order/coupling; it deliberately does **not** claim exact native
float32-libm or normalized integer output. Additional axes also change forward
velocity and the native lateral carry; a useful mixed whole-hull witness must
measure those existing downstream translation effects, not only a nose angle.
First-step orientation alone is not an all-approach envelope proof.

No hook boundary is proposed or qualified here. The scalar and selected matrix
helpers use caller scratch registers/flags and SSE registers, and the caller's
locals remain live across calls as shown above; do not treat them as preserved
instrumentation points. Ordinary selected scalar/normalize paths contain no
callback or persistent pointer retention. This bounded numeric trace does not
establish broader reentrancy, SEH/unwind or general hook safety, nor arbitrary
native writers between later command/integration visits. Those contracts remain
with the existing owners. Native mixed-axis execution, exact trig/normalization
outputs, full-domain predictor discrepancy, useful passage and native Windows
behavior of the port remain unqualified.

Independent static review accepted this contract after checking the original
scalar/getter/axis/matrix instructions and regenerating an identical parsed
receipt. Review added the all-zero composition/normalization bypass and its
zero-dt witness. This acceptance does not replace native motion validation.

## 4. Order-zero and RotateToAngles speed holds: native command chronology

Read-only follow-up, 2026-10-31, against the same original FL image and SHA-256
named above. Targeted `python3`/Capstone inspection used the project's
`verification/probe/compare_fl_engine.py` `PE32`, `read` and `MD` helpers:
order-switch tables `462f5c/462fb4`, command bodies `462896..462a4a` and
`462af1..462c72`, completion `4619c1..4619db`, basis selector
`459fc0..459fd7`, vector normalization `432b30..432bb9`, and the existing
angular/translation consumers. Raw listings stayed untracked at
`/tmp/fl-hold-*.local.asm`. This establishes static behavior and fixture gaps;
no build, test, native execution, game launch or implementation acceptance
result is claimed.

The native switch `45db2f..45db46` selects order 0 at **`462896`** and order
`0x1f` at **`462af1`**. For autopilot order 0, `4628a5` zeros desired speed
unless `obj+44 & 4`; **`4628af` clears `flight+224` unconditionally on this
autopilot branch**. It leaves all three angular remainders untouched. The
shared cleanup at `4629d2` can call `4a3bd0` when `flight+218` requires it;
the tail clears `+218/+28c`, stamps `flight+210=now` at `462a3f`, then jumps
to `462c6e`. Nonautopilot order 0 instead has its own rate-decay branch;
that is outside the ordinary autopilot contract here.

Order `0x1f` interprets `flight+240/+244/+248` as target yaw/pitch/roll,
with `INT_MIN` denoting no roll target. It creates a target basis through
`462b15 -> 50d400`, obtains current flight basis for class 7 through
`462b1f -> 459fc0`, and normalizes its forward vector through
`462b31 -> 432b30`. At `462b8d` the Q16 forward dot product is compared
against `fd70`:

- Aligned, no roll target: `462ba2` clears only roll remainder. Yaw/pitch
  remainders survive. The completion at `4619c1` writes order 0 and calls
  `46a550(1)` at `4619d1`, then jumps to the common exit.
- Aligned, explicit roll: `462bcb..462bd9` clears yaw/pitch and writes
  wrapped target roll minus `obj+20` to roll remainder. Within `5b`
  binary-angle units it clears roll and takes the same completion path;
  otherwise it continues with order `0x1f`.
- Misaligned: `462c3c` calls native yaw/pitch leaf `456450` toward physical
  position plus target forward. An explicit roll target then overrides its
  roll remainder at `462c6b`. The shared `core::order_policy` excludes 0 and
  `0x1f`, so the current A adapter refuses replacement and this native leaf
  remains the supplier of the angular command.

Both command paths reach **B at `462c72` after these writes and any completion
callback**, through the common reload at `462c6e`. B's existing nine-byte
span ends at `462c7b`; it precedes command speed slew, desired-speed read
`462de0` and current-speed store `462ec7`. Ordinary angular integration runs
later in the sector driver's second loop (`443b76 -> 463000`). Thus a B
`lane_site_b` kept/hold speed replaces the post-command desired speed for the
same pass; it does not freeze angular motion. The completion callback may
change order, target or other inputs before B: sample the actual post-callback
state, without retaining a borrow across native continuation. This reuses the
existing B ABI/envelope and `HelperBorrow`; it establishes no new hook or
broader callback/reentrancy guarantee.

The ordinary autopilot angular consumers in section 1 do not select by order
word: they consume each remaining command, subtract its emitted displacement,
retain the new rate, and clear the rate when the remainder is zero. Order 0
therefore keeps turning through a stale nonzero remainder, then stops turning
on this zero-offset path. Order `0x1f` can supply another remainder on its next
command visit. A derived small counterexample is yaw remainder 8192, rate
1024, cap/effective acceleration 4096, dt 250 ms: increment **256**, new rate
**2048**, remainder **7936**. Zero remainders with nonzero rates are the paired
zero-increment control. These are instruction-derived arithmetic, not executed
hold witnesses. Both orders use the ordinary non-order-5 lateral-carry and
translation path of gate-motion section 2.

`fl_lane_step_original.py::RANGES` already extracts speed selection/slew,
object dt, `4632b3..464110` ship integration, scalar/matrix helpers and
root/child publication; `ls_slew`, `ls_integrate` and `publish` enter those
unchanged slices. It does **not** extract the order-0 or `0x1f` command bodies,
their completion branch at `4619c1`, or the latter's `459fc0/432b30` helpers.
`DUE_RANGES` supplies only the separate order-4 due/completion/wake closure.
The shared 3D `selected_angle`, `command_basis` and `command_translation`
operations can consume actual supplied remainders on these ordinary paths;
the order-4 `command_path` forecast cannot be reused unchanged, because its
later steps call `lane_steer/core::steer`. Native remainder depletion and the
actual held-order command visits must not be replaced by invented order-4
steering calls. Order-31 angle fields also cannot be admitted as order-4
KC/completion coordinates.

**Synthetic phase admission is not command-body reachability.**
`fl_lane_command_join_host.cpp::aligned_phase_checks` directly changes the
order to zero after bootstrap, retaining the target gate, and invokes B/E
without executing native order 0. Its handover positive and generic order-0
admission positive are synthetic admission-boundary evidence only. On a visit
that actually executes the native autopilot order-0 body, the target clear at
`4628af` precedes that visit's B. The current `aligned_motion` requirement
`state.target == observation.gate` consequently rejects that cleared-target
state. A transition from another order can leave order 0 and a retained target
at B without executing the order-0 body on that visit (the `0x1f` completion
above is a concrete path, subject to its callback). Collision/E also precedes
commands in the sector driver. These transient possibilities do not turn the
synthetic test into evidence for the native order-0 body or subsequent holds.

Gate identity need not come from that cleared native target: existing
`ctl_lookup` locates the saved board/member by object, and
`ctl_fl_observation` joins a committed member's table identity and validated
`GateState` object/gate/id/root before copying the current gate. Existing
`validity` explicitly retains target-zero order 0 for a fresh reissue gap and
target-zero 0/`0x1f` for a bounded handover. Those are existing source routes
for recovering and validating identity, not proof that finite hold admission
or transport already supports them. A useful future witness must execute the
missing command body, real B selection, and existing slew/3D/publication
slices; include cleared-target order 0, active/native-completing `0x1f`, and a
completion callback that changes order as a refusal control.

### Held-command fixture extraction and ABI closure

Read-only closure follow-up, 2026-10-31. Project `compare_fl_engine.py`
`PE32/read/MD` inspection of `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`,
establishes the following **exclusive-end** spans. Counts are measured static
decodes, not native execution. Extend the existing command fixture and local
`fl_lane_step_original.py` extraction; retain existing math, leaf, slew,
integration and publication slices.

| Original span | Purpose | Bytes | Instructions |
| --- | --- | ---: | ---: |
| `462896..462a4a` | Order 0 | 436 | 126 |
| `462af1..462c72` | Rotate, including common reload | 385 | 92 |
| `4619c1..4619db` | Rotate completion | 26 | 7 |
| `459fc0..459fd7` | Basis selector | 23 | 8 |
| `432b30..432bb9` | Forward normalization | 137 | 38 |
| `4a3bd0..4a3c56` | Scene cleanup | 134 | 33 |
| `46a550..46a59e` | Wake | 78 | 24 |
| **Total** | | **1,219** | **328** |

Euler `50d400` and leaf `456450` already have the extracted closure. The
order-0 call `4628e4 -> 41e420` and its three existing getter calls belong to
the excluded nonautopilot branch: require `obj+40 & 80000`. Cleanup's indirect
switch at `4a3bde` cannot execute with the actual cleanup argument zero;
its `.text` table `4a3c58..4a3c78` need not be copied for this admission.
Wake's indirect call is the controlled callback boundary described below.
Uncopied alternatives remain unavailable; this is not a complete dispatcher.

**Entry and B continuation.** Let `S` be the command body's ESP. The native
`45c810` prologue establishes `S % 16 == 0`, saves the sector-list owner at
`S+24`, and sets `S+10c/S+168` to `min(raw_ms,1000)`. A bounded fixture entry
must supply EBX=object, ESI=`[object+50]`, that saved owner, and at least
**`0x600` bytes of aligned scratch**, with a canary outside used locals.
Rotate's target basis reaches `S+5db`; its `movaps` source at `S+500` requires
the stated alignment. Existing `LS_BEGIN`'s `0x240` bytes are insufficient.

One finite continuation can reuse the existing B fixture's `ret` at
`462c7b`: put the fixture's return bridge address at `[S]`, retain original
`462c6e` (`ESI=[S+24]`), and fall into the installed B at `462c72`. B's
displaced instructions load the object's original next pointer into EBX and
store it at `[S+108]`; the fixture return then reaches its bridge with ESP
`S+4`. Check that store and stack delta before restoring the wrapper frame
and invoking existing `ls_slew/ls_integrate/publish`. This is a proposed
entry/return bridge, not an executed witness. The due fixture's `EB20`
common-exit detour must not coexist with this mode: retain the reload and
the complete B span, rather than placing a five-byte jump in the four bytes
before B. No new motion harness or arithmetic substitute is required.

**Cleanup side effects and liveness.** `4a3bd0` takes ECX=node and one stack
integer, returns with `ret 4`, preserves EBX/EBP/ESI/EDI, and also leaves EDX
untouched. EDX preservation matters: `462a09` advances the caller's sibling
walk through the EDX it supplied before the call. With argument zero the
callee clears node `+1a4/+13c`, recursively follows child `+c` and sibling
`+0`, and stops at a sentinel whose first DWORD is zero. It makes only its
recursive call; there is no allocation, free, foreign callback or transform
write. Keep these original bytes instead of bridging their mutations.

When `flight+218 != 0`, the order-0 caller clears root `+1a4/+13c`, performs
that child cleanup and clears object flag `400`. Its subsequent unconditional
`flight+218/+28c` clears and `flight+210=now` store join the target/desired
writes already established above. These are the actual native state at B;
do not replace them with fixture assignments. The scene clears are observable
side effects even though the extracted integration/root/child-publication
spans contain no direct accesses to those four cleanup offsets. A valid,
acyclic child/sibling graph and live sentinels are fixture input obligations.

**Rotate completion and callback boundary.** Completion writes only WORD
`flight+258=0` before `46a550(1)`; unlike order-4 completion, it does not
clear current speed, desired speed, target or port. Wake is ECX=object,
one stack reason, `ret 4`. Its callback is cdecl `(context,reason)`;
`46a586` removes eight argument bytes, then wake clears object `+7c/+80`.
Those clears also discard replacement tokens installed by callback reentry.
For admitted positive callback index 1, the modeled callback table's function
slot is `+0x4c` (general slot `0x34 + index*0x18`), with its enable DWORD
`+4` nonzero.
The existing due callback's zero-speed/desired and `port=UINT_MAX` assertions
therefore cannot be reused unchanged.

A controlled callback may record the actual pre-B state and replace its
order/target, preserving the cdecl callee-saved registers; B must classify
the result afresh. Preserve the callback's LastError result through B, rather
than demanding the pre-command value after an intentional callback change.
This bridge is not the real script dispatcher: the fixed stock callback
`4c1a40` publishes task result/readiness, with interpreter execution later
([handover contract](lane-handover-contract.md#1-the-actual-completion-issuer-and-removal-sequence)).
Object/flight/table/scene storage must remain live throughout the invocation.
Arbitrary destruction, throwing callbacks and nested command execution are
outside this closure; it establishes no new reentrancy or exception guarantee.

**Smallest closure checks.** Execute cleared-target order 0 with mixed
surviving remainders; cleanup with `+218` zero/nonzero and a child/sibling
branch; active rotate with absent/explicit roll; aligned no-roll completion
retaining yaw/pitch; and explicit-roll completion at `+/-5b` versus continued
rotation outside it. Compare native order/target/remainders/stamp/desired,
cleanup fields and unchanged geometry, then actual B selection/store and the
existing native transport/publication. Add callback order/foreign-target
replacement refusal and LastError propagation, aligned scratch/canaries,
balanced arguments and outer callee-saved registers. Retain the existing B
CPU envelope checks against its immediate native entry state: the preceding
command math legitimately changes XMM registers and flags. SSE2, the existing
finite-math/MXCSR assumptions and existing fixture fault handling still apply.
These are additional closure checks within the ratified held-command acceptance
scope, not a replacement for its policy/geometry cases. No production source,
extraction, build, test, native execution or acceptance state changed in this
follow-up.

### Held-command B flags and native closure witness (2026-10-31)

The original FL successor at `462c7b` is `cmp dword ptr [ebx],0`, before
the conditional branch at `462c7e`. B's displaced loads/store do not consume
arithmetic flags, and this comparison overwrites them before use. The shared
B hook therefore correctly treats arithmetic flags as dead; DF must remain
clear. The closure fixture substitutes a return at that successor and must not
mistake the missing comparison for a production flags-preservation obligation.

The first native closure run failed 11 of 105 checks because its aggregate ABI
check required exact EFLAGS equality. The corrected fixture reports separate
outer-register, frame, reload, next-store, GPR, XMM, MXCSR, DF and scratch-guard
checks, and records the complete pre/post flags. All twelve cases pass those
checks (105 checks, zero failures). No production hook changed. Original
order-zero cleanup, RotateToAngles thresholds and controlled callback effects
also pass; this does not yet qualify the held-command motion policy or the
real script dispatcher. Receipts and the retained failure are in the
[gate-clearance ledger](../../verification/farnhams-legacy-gate-clearance.md#held-command-native-closure-2026-10-31).

### Mixed O31 numeric handover contract (2026-10-31)

Focused static follow-up for the ratified
[navigation activation checkpoint](../../architecture/farnhams-legacy-navigation-activation.md).
The original image/hash above and project PE/Capstone reader were reused;
`fl_lane_step_original.extract_held` regenerated its seven spans to
`/tmp/fl-mixed-o31-contract/`: **1,219 bytes / 328 instructions**, with six
external direct edges (measured static counts). No production edit, build,
native/Wine execution or acceptance change occurred. Existing held native
closure remains the evidence for its CPU/callback envelope; it is not yet a
mixed timed-motion witness. Raw listings remain local `/tmp/fl-mixed-o31-*.local.asm`.

**Exact command writes.** Target yaw/pitch/roll are signed DWORDs at
`flight+240/+244/+248`. `INT_MIN` means absent roll; `462aff..462b15`
substitutes zero only for target Euler creation. Euler consumes unsigned WORD
angles. The target forward is the resulting Q16 matrix's row 2; it is not
subsequently normalized. The class-7 current forward comes from flight
`+890` through `459fc0` and is normalized in a local copy by `432b30`.
The latter computes binary64 square/add/sqrt, multiplies the length by
`1/65536`, divides one by that value, then truncates each scaled component.
It leaves a zero vector unchanged. No current basis is modified by this test.

`462b36..462b8d` forms `dot=R16(Tx*Fx)+R16(Ty*Fy)+R16(Tz*Fz)` in that
addition order, where each product is separately rounded as in section 1.
The signed branch at `462b93` selects active rotation for **dot < 0xfd70
(64,880)**; equality takes the aligned branch. Bounds on target float Euler,
current normalization and each integer product must participate in any future
phase decision. A nominal dot alone is insufficient near the boundary.

- Aligned, absent roll: `462ba2` clears only `obj+38`, then completes.
  Existing yaw/pitch remainders and all rates survive.
- Aligned, explicit roll: subtract signed DWORD `obj+20` from the signed target
  roll. If the difference is greater than `+32768`, subtract 65536 once;
  if less than `-32768`, add 65536 once. The two equal half-turn values remain
  distinct. `462bcb/462bd2/462bd9` write yaw/pitch zero and this roll remainder.
  `abs(remainder) <= 0x5b` completes after clearing roll; `+/-0x5c` continues.
  This is one correction, not a general modulo operation on arbitrary DWORDs.
- Misaligned: `462bf7..462c3c` calls `456450(ECX=object, stack=&point)`
  with point = physical root position plus target forward, in native raw units.
  The existing A0 leaf writes yaw/pitch and **zero roll**, preserving rates.
  Absent roll retains that zero; explicit roll overwrites it at `462c6b` using
  the same signed difference/correction. There is no roll completion threshold
  on this misaligned branch. Coordinate additions and leaf projection retain
  the existing checked nonoverflowing domain.

The completion span `4619c1..4619db` writes only WORD `flight+258=0`, then
calls wake(1) and reaches `462c6e -> B`. It preserves the adjacent WORD,
current/desired speed, target, port, physical velocity, slip, pose and every
remaining/rate field other than the branch writes above, before callbacks.
The order-four `departure_completion` helper is therefore inapplicable: its
speed/desired zero and port reset are not rotate completion. This visit does
not dispatch the order-zero body; a subsequent O0 command visit does, including
its target clear, flag-dependent desired zero, cleanup and timestamp writes.

**Current roll is an additional live operand.** Native explicit rotation reads
`obj+20`, the display Euler roll DWORD; it reads neither roll rate `+2c` nor
remaining roll `+38`. `observe_traffic` already copies object `0..ff`, so
transporting this value needs no additional native read. The invocation-owned
state and numeric model must carry it. Future integration writes it only after a
nonzero total angular increment: `463df6..463e0c` calls `50d8e0` with
ECX=root+40, EDX=&obj+18, stack=&obj+1c, then &obj+20, caller cleanup.
The all-zero increment branch skips this conversion and retains the previous
DWORD, even if the stored matrix would produce a different Euler roll.

The roll conversion has two identical nonpole routes
`50d99a..50da2e` and `50db99..50dc2d`. Let integer root entries
`y=forward.y`, `a=right.y`, `b=up.y`, and `Q=65536`. For `y=+/-Q`,
`50d8f6/50d95f -> 50d908` writes roll zero. Otherwise its positive denominator
is `Q` for `y=0`, or the WORD lookup `D[abs(y)]` at `5fb408`.
It forms signed truncations `u=trunc(Q*a/D)` and `v=trunc(Q*b/D)`, computes
`atan2(u,v)`, adds `2*pi` for negative radians, multiplies by the native
`10430.378350470453`, and truncates. An output >=65536 subtracts 65536 once.
The stored display roll is thus **unsigned 0..65535** on the admitted numeric
domain. Preserve this representation before the command's signed subtraction;
substituting a signed principal angle changes the one-correction contract.

The original table initializer is `50d310..50d3b1` (**161 bytes / 28
instructions**, measured static decode). Its denominator writer at `50d39c`
implements, for `n=0..Q`,
`D[n]=min(65535,trunc(Q*sqrt(1-(n/Q)^2)))`.
Constants `591a00/591c90` are exactly `1/Q` and `Q`; all operations before
sqrt are exact binary64 for this integer domain. Therefore the table is exactly
`min(65535,isqrt(Q*Q-n*n))`. A noninteger square root is at least
`1/(2*Q)` from the nearest lower/upper integer boundary, far above one binary64
ulp here; integer square roots are exactly representable. This equivalence
also survives the directed hardware rounding modes. For nonpoles, `D>=362`.
A derived host check of all **65,537** rows found zero differences between the
sqrt expression and integer square root; reproducible expression:
`all(int(sqrt(1-(n/65536)**2)*65536)==isqrt(65536**2-n*n) for n in range(65537))`.
This is a static/host derivation, not execution of the original initializer.

The same initializer writes the pitch display table at `50d345` using the
math leaf at `5494a0`, times `1/(2*pi)` and `Q`, narrowed to WORD. That pitch
output and the separate nearpole yaw tables do not feed the roll result.
Existing extraction copies `.rdata` constants, but these tables are runtime
`.data`/BSS and have no corresponding raw PE backing; merely extracting
`50d8e0` does not initialize them. A fixture exercising original roll publication
must initialize the exact denominator table from this derived integer law,
state which unrelated display fields remain modeled, and avoid attributing
uninitialized display output to the game. This fixture issue supplies no reason
for an arbitrary production pitch restriction. Pole, zero-y and nonpole roll
branches have the contract above; out-of-range or overflowing numeric input
remains a refusal. No general trig-table subsystem is required by O31.

**Minimum numeric join and temporal order.** Reuse scalar/WORD transport,
`arrival_angles` A0 projection/enclosures, float Euler/error recurrence,
`command_basis`, basis normalization, slip, full hull/gate queries and
`timed_pair`. Reuse `HELD_RANGES`, the installed B, existing `hs_*` or larger
`o5_*` aligned scratch wrapper, original full speed prefix/slew, and
integration/publication extractors. `fl_mixed_motion_original` supplies the
existing full prefix; the joined two-ship fixture supplies both stock bodies,
arrival commands, both E operand orders, first-state and through-H containment.
No new original instruction span or hook is needed for this join.

A private rotate command update must re-evaluate the copied target against the
then-current covered pose before each declared command/B step; active rotation
must not repeat the first remaining angles or use `core::steer`/order-four A.
Its forward branch, roll atan wrap, one-correction delta and completion threshold
must be enclosed over the full supplied uncertainty. An unresolved branch may
refuse; nominally choosing completion loses residual motion. For future roll,
use integer basis-component intervals, monotonic denominator bounds from the
integer-sqrt law and the existing atan enclosure; retain fragments across the
unsigned 0/65536 cut or refuse that unresolved cut. Exact poles force zero;
a possible pole must not silently select a nonpole nominal formula.

The real driver is collision/E, then all command bodies/B, then the second
speed-slew bucket walk, then sector post-pass; integration occurs in the later
sector loop (`443b42`, `443b49`, `443b50`, `443b76`). Other objects/callbacks
can intervene before slew. At B, consume actual post-command/post-callback
words; at E, include the upcoming native rotate command in the conditional
current-phase forecast. Make the entry phase explicit or prove first-step
idempotence and matching B selection; an O31 completion can change which O0
stop/kept/hold branch selects. Future completion goes directly to B with the
surviving desired/target and angular residuals; only later O0 visits execute
O0 clearing. Publication expiry supplies no new B writer, refresh or A call;
the exact 250-ms admission described by the original mixed witness remains
historical evidence of that FL wrapper. The
[parent-ratified current-authority correction](../../architecture/farnhams-legacy-navigation-activation.md#o31-current-authority-and-the-a-publication-clock--parent-ratified-implementation-pending)
retains this lease for A-derived/O4 writes while admitting O0/O31 held writes
only through fresh current acquisition, shared validity/selection and geometry.
Its numeric B law must follow the same distinction: expired held authority
makes no write and preserves native continuation to H. Implementation and
requalification remain pending; no timestamp or A call is synthesized.

Keep the previously established ABI, lifetime and callback boundaries: EBX=
object/ESI=flight at command entry; aligned >=0x600 usable scratch and guarded
return bridge; native common reload; B's complete `462c72..462c7b` displaced
span. E reuses its qualified six-byte `457600..457606` entry and
`ECX=A`, stack B/distance/output, `ret 12` ABI; its ordinary collision caller
is `457db5`. Verify GPR/XMM/MXCSR and LastError against **immediate B entry**;
arithmetic flags are dead at the successor CMP, DF remains clear. A controlled
wake callback's LastError replaces the pre-command value and must survive B.
All source/scene/table storage stays live; the existing owner/HelperBorrow
refuses nested scratch use. Do not retain a native borrow across wake or
continuation. Arbitrary destruction, foreign throwing callbacks and nested
native command execution remain outside the controlled closure; no new
reentrancy or exception guarantee follows from this analysis.

Acceptance must add useful held rotate mixed passage on both stock rows and
both E operand orders; absent and explicit roll; active re-evaluation and
completion with nonzero surviving translation/angular/slip state through H;
current order/target/angle replacement and invalid identity refusal. Native
commands and B/E must execute in their real roles; A may prepare the original
publication but must not be fabricated per rotate frame. Reuse existing covers,
reserves, numeric domains and the declared 250/500-ms future schedule. Open
until implementation/evidence review: first-phase/B-selection join, covered
forward/roll branch handling, denominator fixture initialization, actual
post-callback re-acquisition, both-row useful witnesses and measured added cost.

### Roll publication dependency and off-shell atan2 closure (2026-10-31)

The mixed rotate follow-up needs two additional distinctions: initializing a
fixture's display-angle dependencies, and extending the scalar atan budget
beyond the old exhaustive normalized-integer shell. Neither is established just
by having extracted the original instructions.

For a representative tilted fixture with `abs(root.forward.y)<=0xe666`, the
complete original `50d8e0` path can safely execute with only the denominator
lookup initialized. Positive-y pitch lookup `50d97f` writes only the supplied
pitch output at `50d987`; its result is discarded when `50d99a` loads root
right.y. Negative-y `50db7f/50db8d` similarly writes pitch, and `50db8f`
overwrites EAX with the denominator table base. Neither loaded pitch value is
subsequently a branch operand, address or divisor. ESI retains the original
forward.y for `50da30`/`50dc2f`; ordinary positive/negative values take
`50db2a`/`50dd61`. Those yaw suffixes divide root forward.x/z by the same
positive denominator and call the already extracted `50f490..50f4d2`, a
standalone integer-to-double atan2/turn wrapper with no runtime-table dependency.
Thus an uninitialized but **mapped** pitch-table WORD affects the unused
`obj+1c` output only; it cannot affect roll, subsequent addresses, branch
selection or fault behavior on these paths. The arithmetic and supplied output
pointers retain their existing representability/liveness obligations.

Exact forward.y poles `+/-65536` take `50d8f6/50d95f`, write pitch `+/-16384`
and roll zero through `50d908`, compute yaw directly from root right.x/z by
atan2, and return at `50d95e`: they read no runtime tables. Nonpole nearpole
yaw branches outside the representative tilted fixture's range do read the
other trig tables. This is a fixture dependency boundary, not a new production
pitch restriction or a claim that those tables have been reconstructed.

The native atan2 leaf is `549afb..54a011` (exclusive end); the larger existing
`RANGES['atan2']` also contains a separate float-atan routine. Its ABI is
XMM0=y, XMM1=x, result XMM0, plain RET, no integer stack arguments. There are
no calls or nonvolatile-GPR writes in the leaf. Roll converts its signed integer
quotients with CVTDQ2PD, so those argument conversions are exact. For the checked
basis domain, quotient magnitude is below `2^24` because each basis entry is at
most 65537 and the nonpole denominator is at least 362. Nonzero axis pairs
retain the native signed-zero/quadrant contract. At `(0,0)`, CVTDQ2PD supplies
positive zeros and native/shared return zero by convention; the real angle is
undefined, so this is not a geometric error-bound case.

Nonzero integer arguments in this domain satisfy both fast-entry exponent
checks `549b0e/549b33`; no exceptional/subnormal dispatch is required. The
bit construction before RCPSS at `549b5d` is an exact float representation of
abs(x) for integers up to `2^24`. Intel's documented RCPSS relative-error
bound is `1.5*2^-12`, independent of rounding control
([Intel SDM, RCPSS](https://cdrdv2-public.intel.com/671110/325383-sdm-vol-2abcd.pdf)).
The high-word rounding/mask at `549b88..549ba0` retains four fraction bits;
its relative bin error is at most `1/32`. Their combined error is below 0.04.
For the moderate ratio route, the residual is `(r-k)/(1+r*k)`, hence less
than `0.04/(2*sqrt(0.96)) < 1/16` in magnitude. The small-ratio route and the
ratio-above-32 route also have residual magnitude below `1/16`. This bound
uses the actual approximation/binning contract, not a claim that native atan2
is perfectly homogeneous or the shell sweep covers every integer ratio.

The integer-double low sixteen mantissa bits cleared at `549be9` are zero on
this domain. Bin k is dyadic with five significant bits, so its integer input
products, split subtraction and residual numerator/denominator sums are exact
binary64 before division. The small-ratio route instead forms `1/abs(x)`
and multiplies by abs(y). Its reciprocal is at most one, and the product is
below `1/16`; despite the possibly large raw y, the two directed operations
introduce less than `2^-52` absolute residual error. The reduced polynomial
in both ordinary routes is
`z + z^3*(A*z^2+B)*(z^4+C*z^2+E)`, with the four binary64 coefficients at
`594000/594008/594010/594018`. At `abs(z)<=1/16`, its error against real atan
is below **3.821e-7 radians** by the cubic Taylor remainder plus an absolute
coefficient expansion. Decimal reference checks cover all **163** moderate
hi/lo atan table pairs and the extra pi/2 pair, plus quadrant constants;
the maximum reference discrepancy is below **7.04e-32**. A deliberately loose
`2^-30` directed-rounding allowance covers the reduced divide, polynomial and
compensated table/quadrant arithmetic in all four SSE modes. The resulting
native-to-real bound is below **2^-20 radians** for nonzero input pairs.
Adding the existing shared atan2 float evaluation bound remains below
**2^-15 radians** native-to-shared over those nonzero signed integer pairs
with magnitude up to `2^24`, not merely the old normalized shell. The separate
`(0,0)` zero-return convention does not assert a real angle. The extra native
turn conversion/wrap rounding and CVTT
integer uncertainty remain separate caller obligations.

The source-bound static/analytic producer is
[`rotate_atan_contract.py`](../../../verification/results/fl-angular-motion/rotate_atan_contract.py);
its compact [receipt](../../../verification/results/fl-angular-motion/rotate-atan-contract.json)
contains instruction-anchor counts, leaf/table hashes and derived bounds only.
Run `python3 verification/results/fl-angular-motion/rotate_atan_contract.py`.
It reuses `arrival_a0_intervals.py`'s Decimal atan reference and shared helper
bound. This adds no original-code execution witness and no floating-point
exception/unwind guarantee; it supplies the missing numerical-domain argument.
The older exhaustive shell sweep remains unchanged evidence for native ABI,
MXCSR/LastError restoration and integer turn conventions. The mixed original
rotate/completion/integration fixture remains the acceptance join.

## 5. Float Euler and a proper-frame error recurrence

Static derivation, 2026-10-31. The producing inspection/Decimal check is
[`basis_bridge.py`](../../../verification/results/fl-angular-motion/basis_bridge.py),
with compact [receipt](../../../verification/results/fl-angular-motion/basis-bridge.json).
Run `python3 verification/results/fl-angular-motion/basis_bridge.py`. It binds
the original image, ten instruction anchors, the reduction/polynomial constants
and all 64 sine/cosine table pairs. This section supplies conditional numerical
bounds; useful whole-motion containment still requires the coupled native
witness. The existing double-A0 scalar oracle does not qualify these float
Euler calls.

### 5.1 Float arguments, leaves and matrix coefficients

At `50d409/43a/46f`, each increment is reduced to an unsigned word m before
`cvtdq2ps` and `mulss` by the float at `5919a8`,
c=9.58738019107841e-5. The conversion of m<=65535 is exact. Against the
ideal angle m*2*pi/65536, all native float rounding modes satisfy

```text
argument_error <= 65535*abs(c-2*pi/65536) + 2^-21
               < 6.516801e-7 radians.
```

The final term is one directed float multiplication ulp below eight radians.
The wrapper calls `419d86 -> 54a352` and `419d96 -> 54be84` select the float
leaves, rather than the double leaves used by the A0 oracle. Their main paths
multiply by float 32/pi, select n using `cvtss2si` and matching
`addss/subss 12582912`, and use the 64-pair table at `598350`.
The cosine table index has the additional quarter-turn offset. For this Euler
argument domain, n is in [0,65]. Under RN/RU/RZ, the encoded n is correct
and the residual magnitude is below 0.1. The producing script checks this using the actual
float reciprocal-pi coefficient and a directed multiplication ulp.

The encoded split constants at `594698/594690` (identical to
`595678/595670`), scaled by 2^-896, subtract pi/32 with error below 5.528e-30.
The high-only residual used by the polynomials differs from the corrected
residual by at most 65 times the low part, below 4.37e-12. The cosine
polynomial is `1-r^2/2+r^4/24`; the sine quotient is
`1-r^2/6+r^4/120`. Their Taylor remainders on |r|<=0.1 are bounded by
0.1^6/720 and 0.1^7/5040. Polynomial derivative bounds charge the split-residual
effect below 5e-13. Sixty-four directed binary64 error factors with expanded
absolute magnitude sum at most 64 charge evaluation/reduction below 1e-12.
The table's maximum absolute discrepancy is below 4.834e-17, checked against
80-digit Taylor references with a combined roundoff/tail allowance of 1e-70.
Final `cvtsd2ss` adds at most 2^-23. Together these give a float-leaf error
below 1.25e-7 relative to the true sine/cosine of its float argument.
The tiny-input paths (`1-x*x` and `(1-2^-30)*x`) also fit that absolute bound;
Euler's unsigned-word inputs cannot reach the large-argument fallback.

Under **RN/RU/RZ**, native float trig versus the ideal word angle is below
7.767e-7; use
**e_native=2^-20**. For shared `sincos_f` at the same wrapped float argument,
the previously derived source bound, at most 4.699e-6, plus the argument
allowance fits **e_nominal=2^-17**. This is an additive float-path derivation,
not an inheritance of the double-leaf oracle's discrepancy budget. The nominal
implementation must reduce the word before making its float argument;
`wrap_pi`'s bounded subtraction loop is not a general reduction of arbitrary
signed32 scalar displacement words.

Each Euler entry is a single trig value, a pair product, or a pair plus a
triple product. For a per-trig absolute error e, coefficient perturbation is
at most `5*e*(1+e)^2`. Each expanded term passes through at most three float
roundings; `2*(1+e)^3*gamma_3(2^-23) < 2^-20` bounds the native float Euler
evaluation. Scaling by Q=65536 is exact, and coefficient CVTT contributes
less than 1/Q. Consequently a conservative matrix Frobenius error is

```text
delta_D(e) = 3*(5*e*(1+e)^2 + 2^-20 + 1/Q).
delta_D(e_native) < 0.000062943
delta_D(e_nominal) < 0.000163081
```

Zero factors, exact +/-Q factors, roll=0 and interval-separated coefficient
roundings can tighten these bounds. Those tightenings require their actual
premises; the displayed common bound does not assume a single-axis pose.

**Round-down correction (original native witness).** The earlier static receipt
[`basis-bridge-before-rd.json`](../../../verification/results/fl-angular-motion/basis-bridge-before-rd.json)
incorrectly applied the residual premise to RD. In RD, exact cancellation at
`54a391/54bec3` can produce float negative zero (`80000000`). The following
shift at `54a3d3/54bf05` then produces double bits `1000000000000000`, not
zero. Multiplication by the encoded split constant gives residual bits
`47c921fb54444000` (about 6.68e37). The actual original float-leaf
[witness](../../../verification/results/fl-arrival-entry/float-leaf-native4.json)
executes 16 calls: RD cosine is positive FLT_MAX, sine is negative infinity;
the other twelve RN/RU/RZ calls remain tight. These are executed results;
the instruction trace explains their cause. The argument multiplication bound
above still holds in RD, but the leaf/reduction and Euler bounds do not.
The double A0 and integer angular proofs are separate and remain all-mode.

Arrival authority therefore carries the original invocation MXCSR, captured
before its helper Envelope normalizes arithmetic. RD or unknown mode refuses
any required step with a possibly nonzero Euler increment, including reserves
and a zero nominal increment with a nonzero interval. All required exact-zero
updates can retain the bypass proof. This does not change game MXCSR; the
existing boundary restores its caller state. RN/RU/RZ remain admitted under
the stated finite-domain predicates.

### 5.2 Separate rotation uncertainty from Q16 shape

Define exact real G(B) by the forward/right Gram-Schmidt order: normalize B2,
normalize `forward cross B0` as up, then form right=`up cross forward`.
Let F=G(B) be proper, S=B*transpose(F), and rho=||S-I||_F. Then G(S)=I.
For a proper world rotation O, `G(A*O)=G(A)*O`. For an ideal left Euler
rotation R, this gives

```text
||R*S*transpose(R)-S||_F <= 2*||R-I||_2*rho
                         <= 2*min(2,T)*rho,
```

where T bounds the sum of absolute principal Euler angles for this step.
This commutator term vanishes with the step's rotation; rho is the current
Q16 shape defect, not accumulated orientation uncertainty. Using
`4*sin(T/2)` without restricting T is unsafe when a sum exceeds pi. The
`2*min(2,T)` bound needs no additional trig approximation.

Native composition `50ebe0` rounds three products separately. Each component
therefore has error at most 3/(2Q); the full matrix Frobenius error is at most
9/(2Q). Every rounded product and each native dword partial sum must first
be representable. With D differing from ideal R by at most delta_D, a local
matrix perturbation around S is bounded by

```text
epsilon = 2*min(2,T)*rho + delta_D*(1+rho) + 9/(2Q).
```

For `L(x)=x/(1-x)`, lower forward length m_f, lower normalized-forward/right
cross length m_c, and upper right length k, exact G has local angle bound

```text
alpha = L(epsilon/m_f)
angle_G <= alpha + L((epsilon+k*alpha)/m_c).
```

Both L arguments must be below one. To see the second term, minimally rotate
the old forward to the new forward: it moves the reference right by at most
k*alpha. The remaining right perturbation is at most epsilon, and its
cross-product normalization has denominator at least m_c. This compares two
proper frames locally. It does not repeatedly apply a row-normalizer
derivative to the accumulated orientation error. Since G(S)=I, usable bounds
are `m_f,m_c>=1-rho` and `k<=1+rho`.

### 5.3 Quantized normalization and the additive update

For a nonzero native integer vector, the binary64 normalizer error and three
component truncations are enclosed by
`delta=(sqrt(3)+2^-20)/Q`. Each rounded Q16 cross product has vector error
at most sqrt(3)/Q. Require the composed matrix to have forward length and
normalized-forward/right cross length at least 0.9, and right length at most
1.01. These predicates exclude every zero-row/cross bypass and the associated
undefined orientation; overflow remains a separate checked predicate.

The producing script evaluates the following conservative chain. Set
`lambda=1-delta`, `a=L(delta)`,
`u=L((1.01*a+delta/lambda)/0.9)`, `b_u=L(u+delta)`,
`g=lambda*sqrt(lambda^2-(u+delta)^2)`,
`r=L(delta/g)+delta`, and `b_r=L(r)`.
Here a bounds forward direction quantization; u includes up-cross rounding;
b_u projects the rounded up into the forward-perpendicular plane; g is the
lower final cross length; and b_r accounts for the final right normalization
and its canonical projection. The canonical proper-frame angle error is at
most `a+b_u+b_r < 6.235*delta`. Raw forward/right/up distances from that frame
are bounded by `delta`, `r+b_r`, and `u+delta+b_u+b_r`; their Frobenius norm
is below `9.419*delta`. **12*delta bounds both quantities**, including room
for outward binary64 bound evaluation. Normalizer dot/sqrt/reciprocal inputs
stay normal; delta's extra term dominates the eight-factor normalizer error.

After each nonzero native update, reset its shape bound to at most 12*delta.
The all-zero increment branch skips Euler, composition and normalization:
preserve the old raw basis/shape and add zero numerical noise. If a family
can take either branch, retain the union, including the larger shape bound.
The initial shape is computed from the actual copied basis, without assuming
that an imperfect zero-update basis will be repaired.

Let Theta bound geodesic distance between native and nominal proper frames.
For one step, add the local conditioned-G and quantization terms for each
implementation, plus the sum of per-axis circular Euler displacement errors.
This follows the bi-invariant rotation distance triangle inequality. Then
each raw row differs by at most
`min(2,Theta) + rho_native + rho_nominal` (per-row shape bounds may tighten it).
Nominal proper-frame corrections may instead be computed with outward bounds
from its known matrices. A simple sufficient conditioning check is
`rho+epsilon<=0.01`, followed by the displayed denominator checks; this also
provides the 1.01 composed-right upper bound. Keep the native and nominal
shape values separate from Theta in every later step.

These conservative constants can cost about 1e-3 radians per possible turning
step. Charge only steps that can actually turn. A few depletion steps can
remain useful for a capital hull; fifty such charges can be too wide and must
be reported against the actual failed leg/solid. Scalar q/rate correlation
loss must not keep a depleted family turning indefinitely. The short-brake
48-state cancellation witness is recorded separately under
[`angular_completion_witness.py`](../../../verification/results/fl-arrival-entry/angular_completion_witness.py).

This basis argument does not bridge `command_translation`'s exponential slip
decay to native linear word decay. Native translation, carry and decay retain
their [gate-motion contracts](gate-motion.md). For either
bounded translation recurrence, position error accumulates as
`e_X += dt*(e_forward_velocity+e_slip+e_carry)` plus its own native rounding
terms; products require `|a|*e_b+|b|*e_a+e_a*e_b`. Whole-hull continuous leg
covers and actual completion/phase horizons remain separate obligations.


For the arrival leg's intermediate shape, reuse the
[declared ordered-Euler interpolation](lane-command-transaction.md):
sweep the old raw basis through the three ordered increments, then include the
normalized endpoint. `arrival_motion` unions both endpoint support boxes and
adds the old-shape corner radius times the Euler excursion bound. Its published
nose cover measures from the segment's starting raw forward and includes both
endpoint forward changes plus their row-error balls. Thus endpoint normalization
is included explicitly; the local `normalization=0` term is not a claim that an
imperfect initial basis remains unchanged. This is the supplied conditional
forecast's interpolation premise, not a claim about arbitrary intervening engine
motion or qualification of the future command cadence.

### 5.4 Arrival word motion, generated velocity and coast

Arrival now uses the checked native-word angular adapter, including exact Q/R,
integer displacement/remainder, zero-command reset and reversal growth beyond
half a turn. Completion is a separate zero-rate/zero-remaining atom; unfinished
negative and positive envelopes come directly from the scalar leaves. They are
not reconstructed from a merged completion/moving rate hull. The ephemeral
rate slabs tighten those leaves without constructing Cartesian trajectories.
The first copied object timestep is exact. Later integer steps of at most
500 ms are the declared conditional forecast cadence, not a prediction of the
VM scheduler. Pending A0 uses actual flight `+870` rows. Covered future A0
checks the common positive scale, world non-near condition, point difference
not INT_MIN, point addition, R16 product and dword partial-sum domains before
consuming every returned yaw fragment and the full pitch hull.

Translation replays the native per-product Q16 velocity/carry and signed
integer position/slip operations. The initial copied physical velocity is
independent input. After a real modeled translation publication, however,
`V_old = R(v_generated*F_old)` is an available algebraic relation. The
invocation-local generated flag and prior scalar speed describe that relation;
a new command may force a different speed without changing the prior value.
Integration skip preserves it without pretending to publish. Prior generated
speed zero gives exact zero velocity and carry.

Let kappa=65537/65536, deltaF bound this step's native raw-forward change, and
rho bound the current/new within-frame Frobenius shape defect. Orthogonality of the
proper frame and the row shape perturbations give a projection bound

```text
L_bound = abs(v_generated)*(kappa*deltaF + 2*rho + rho*rho)
          + kappa*sqrt(3)/2 + 1.5
C_native_norm <= 2*kappa*L_bound + sqrt(3).
```

The rounding terms cover generated velocity and separately rounded projection
products. Carry discrepancy can use `C_native_norm + norm(C_nominal)`, taking
the minimum with the independent generic bound. Native word/intermediate
range checks remain in force. deltaF includes this step's possible rotation,
normalization and old/new shape, even when its nominal displacement is zero;
it never recharges the accumulated proper-frame orientation uncertainty as
new physical lateral velocity. The generic first-copied-velocity path remains.
For the exact fixed-basis path, the zero-error generic replay wins the minimum;
its unused proper-frame shape field need not supply this correlated proof.

The rejected analytic coast shortcut used exponential decay and did not carry
its endpoint/slip discrepancy into the terminal reserve. The actual source
host [failure witness](../../../verification/results/fl-arrival-entry/motion-coast-before-fix.json)
records the source patch and producing command: at H=3.5 s, slip -2048 and
60000 raw speed, position differs by 88.699 raw units and terminal slip by
44.849 with zero claimed errors. This was a host recurrence witness, not an
original-instruction execution. The new coast shortcut requires zero slip,
zero carry, constant speed and basis, and physical velocity exactly equal to
the native rounded `speed*F`. It sums exact integer 500 ms advances and one
final integer-ms advance after outward duration rounding. Its chord cover
includes the exact final short-piece corner. All other cases keep bounded
stepping and carry endpoint uncertainty into subsequent reserves. Time and
measured root-chord travel floors are checked independently before completion.

There are two distinct native flags2 `02000000` skips. The speed-list guard
`462d4d` branches to `462eca`, before max/optional cap/suppression and scalar
slew. The object integration guard `4632b3` branches to `464d66`, skipping the
ordinary angular/basis/translation/publication path. The static integration
[inspection](../../../verification/results/fl-arrival-entry/integration-skip.json)
and coupled native fixture retain both. The motion-only prefix extraction is
52 bytes/11 instructions (`462d26..462d5a`, SHA256
`ddaed5fcffbe117e9c3b3b5f0a772cdea28914009a56ed243ae849c357419171`).
Entering at `462d5a` bypassed the first guard and caused native4's two synthetic
skip mismatches; the corrected native5 fixture starts before it. A skipped
forecast does not permit the coast shortcut to invent movement, and a turning
synthetic reserve still requires the supported float-Euler mode premise.

### 5.5 Full-body and continuous leg covers

One query validates the gate Frame and hoists the canonical support-corner
radius. Each leg transforms all eight corners of the full acquired canonical
support box; all 146 nodes/122 bodies and publication error are retained. No
body or engine data is reread per step. Root-relative samples stay physical
root positions. The root-centred capsule remains the separate conservative
pair shape; transformed support dimensions do not silently replace it.

For proper-frame orientation distance Theta, the full matrix operator error is
bounded by `min(2,Theta)+rho_native+rho_nominal`. Multiplying the hoisted corner
radius bounds every body's matrix perturbation. The row bound capped by
2*kappa is used only for a row/nose, never as a matrix operator bound. For the
whole ordered-Euler sweep, including normalized endpoints, use

```text
matrixDelta = min(2,turn+local_native) + rho_old + rho_new.
```

Here rho_new>=rho_old and turn bounds the sum of absolute principal increments.
The unnormalized intermediate excursion satisfies
`min(2,turn)*(1+rho_old) <= min(2,turn)+2*rho_old <= matrixDelta`;
the local normalization term covers the endpoint union. Exactly zero updates
have zero excursion. Leg support unions add this continuous rotation cover,
position errors and all unchanged publication, numerical, drift, tracking and
50 m margins. The nose cover remains relative to the **segment-start** raw
forward, as required by `lane_geometry.h`; it is not endpoint interpolation.
Current, first and terminal complete runout paths are queried separately and
are never advertised as timed pair endpoints.

The [final host receipt](../../../verification/results/fl-arrival-entry/motion-general-host.json)
contains optimized and sanitizer checks, copied-packet failures/recovery,
phase-band refusal, actual history E without a Ship table, both gate solids,
full-H low-rate turning stock cases and mixed-axis order-zero motion. Its
small-repeat full query/E timings include the high-rate conservative Argon
limit and label enclosure slabs separately from excluded nominal replays.
The [compact limit witness](../../../verification/results/fl-arrival-entry/motion-high-argon-limit.json)
records the first blocked leg; it does not assert that this pose must always
be blocked or that the numerical cover proves a physical collision. Original
higher-rate spawn positions remain blocked/unknown diagnostics, and a phase
uncertainty band remains unknown. Nearby low-rate positives do not solve
arbitrary phase ambiguity.

The separately owned [native5 receipt](../../../verification/results/fl-arrival-entry/motion-native5.json)
executes the coupled original arrival/A0/word/Euler/translation paths: 80 cases,
720 motions, 719 retained forecast comparisons, six full-horizon cases and
both coast controls, with zero reported failures. It also exercises installed
E mode preservation/refusal. This is declared-cadence qualification with
modeled getter/scene inputs; future script/flag changes remain conditional.
Mixed arrival/departure pair authorization and small-occupancy context remain
separate required work.
