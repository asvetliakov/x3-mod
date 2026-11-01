# FL gate stop and launch boundary

2026-10-29; static comparison at source checkpoint `e9d22405`. This note owns
the next three-site gate group: existing P2 stop A, P4 stop B and P5 launch.
It establishes FL hook inputs, replay and active continuations, and the
native readers those adapters need. It does not admit the traffic, lane,
arrival-placement or docking controllers. No production change, PE build,
native fixture, Wine command, game launch or installation ran.

The [FL base steering contract](ship-steering.md) owns the
four base hooks, five dynamics getters, simulation ordering and common ship
fields. The existing policy remains owned by [capital gate transit](../../architecture/capital-gate-transit.md)
and [navigation AI](../ship-navigation-ai.md). No steering or launch law is
redesigned here. Measurements below mean static measurements of the named
original images; dynamic behavior and performance remain unmeasured.

## Evidence and scope

The producer [verify.py](../../../verification/results/fl-gate-transit/verify.py)
reuses the project's PE mapper, objdump parser and instruction-aware span
checker. The [compact result](../../../verification/results/fl-gate-transit/verification.json)
records six spans, selected region hashes, operand assertions, references,
two synthetic relocated address maps and targeted negatives. Run:

```sh
python3 verification/results/fl-gate-transit/verify.py
```

Inputs are `/mnt/c/X3AP-orig/X3AP.exe` (2,153,984 bytes, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`)
and `/mnt/c/X3FL-orig/X3FL.exe` (2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`).
Hashes are provenance, not admission policy.

Source inventory: `ship_steering_install.cpp` supplies the three stubs and
group transaction; `ship_steering_sites.h` supplies AP spans and resume slots;
`ship_steering_clamp.cpp` supplies the three helpers; the gate predicates and
distance arithmetic are in `ship_steering_core.h`. P2/P4/P5 helpers read memory,
update bounded module state and return; they do not directly call an engine
getter, wake, KC dispatcher or Win32 navigation API. The native wake and vector
scale below are existing continuation code, not new helper calls.

Ghidra 12.1.4, existing `FLRegistry/X3FL.exe`, independently puts all three
spans in `0x45c810` (26,350-address body), wake in `0x46a550` (78-address
body), and the capital predicate in `0x4496e0` (51-address body). The wake has
18 direct callers in that database, including P2/P4. Reproduce the bounded
context query from the main checkout; output stays local:

```sh
~/ghidra_12.1.4_PUBLIC/support/analyzeHeadless \
  "$PWD/verification/results/fl-port/registry-writers/ghidra" FLRegistry \
  -process X3FL.exe -readOnly -noanalysis \
  -scriptPath "$PWD/tools/analysis" \
  -postScript X3FunctionContext.java /tmp/fl-gate-context.local.txt \
  45e8e5 45e455 45eff7 46a550 4496e0
```

Raw objdump/Ghidra material remained local under
`~/.cache/x3m-port/fl-gate-local/`. No raw decompiler output or executable
bytes beyond short hook spans are tracked.

## Three spans and their two continuations

Preferred image base is `0x400000`. FL bindings use the admitted loaded
image base plus the listed RVA, including active resume slots; AP VAs are
not FL fallbacks. All spans contain whole, position-independent instructions
with no relative control transfer: `ret_pop=0`, `rel32_offset=0` throughout.

| Site | AP span / tail / active resume | FL span (RVA), bytes | FL tail / active resume |
| --- | --- | --- | --- |
| P2 stop A | `0x454b91`, 6 B / `0x454b97` / same | `0x45e8e5` (`0x5e8e5`), 6 B; `894b14894b10` | `0x45e8eb` / same |
| P4 stop B | `0x454700`, 6 B / `0x454706` / same | `0x45e455` (`0x5e455`), 14 B; `c7431400000000c7431000000000` | `0x45e463` / same |
| P5 launch | `0x45526f`, 5 B / `0x455274` / `0x455281` | `0x45eff7` (`0x5eff7`), 7 B; `0fafc089442410` | `0x45effe` / `0x45f010` |

P2 replaces `mov [ebx+0x14],ecx; mov [ebx+0x10],ecx`, with **ECX=0**,
not AP's EAX=0. P4 replaces two immediate-zero stores, seven bytes each.
Fourteen bytes is the smallest complete span covering both P4 stores; it fits
the existing 16-byte patch capacity. A seven-byte first-store claim would
also need the unclaimed second store checked and skipped on the active path;
there is no need for that split receipt.

P5 replaces `imul eax,eax; mov [esp+0x10],eax`. Its tail reproduces the
native square and continues at the constant load. Its active helper consumes
**incoming EAX=t milliseconds**, returns the distance in EAX, and jumps to
`0x45f010`, where native code stores that result into `[esp+0x10]`. The active
path skips the native constant/multiply/divide block too. Its full 25-byte
entry-to-active-resume range has a separate fingerprint in the result and
must be preflighted as part of the gate receipt; checking only the claimed
seven bytes does not qualify the skipped arithmetic or active destination.

The AP-shaped five-byte hook at FL `0x45effe` is **not an equivalent input**:
EAX and `[esp+0x10]` already contain t squared, and ECX still denotes the gate.
Taking its square root would lose the direct elapsed-time contract. Starting
at `0x45eff7` retains t without extra state. A five-byte claim at that earlier
site would truncate the stack store and is rejected.

Measured census: zero incoming direct controls and zero unaligned pointer
dwords into all six span interiors, or into the larger AP/FL launch ranges
skipped on the active path. The control census is the global linear `.text`
decode; pointer census covers `.text/.rdata/.data`. Whole instructions are
independently decoded from command-function entry. Computed jumps, foreign
patches and runtime self-modification are outside this bounded static proof.

The producer passes 9 AP and 62 FL operand assertions, 12 AP and 17 FL
negative checks, and two relocated maps (`0x10000000`, `0x50000000`, eight
checks each: three span reads, three active-resume reads, two virtual root
addresses). The roots are zero-filled virtual data; these checks establish
their section/offset mapping, not pointer contents or loaded runtime state.
Negatives exercise
changed opcodes, truncated instructions, interior branch/pointer witnesses,
stale preferred VAs, AP stop encodings, lost t at the late constant hook and
changed skipped arithmetic. These are structural checks, not executed hooks.

## CPU, stack, lifetime and callback boundary

**P2:** EBX is ship. ECX=0 follows `xor ecx,ecx` at `0x45e8d9`; EAX is
flight from `0x45e8db`. Order has already become zero at `0x45e8de`, port
index is already -1, and **`push 1` is already outstanding** at `0x45e8cd`.
Continue with exact ESP to `mov ecx,ebx; call 0x46a550`. Preserve all incoming
GPRs as the AP envelope does; do not substitute AP's EAX-zero assumption.
Incoming arithmetic flags are dead: continuation moves do not read them and
the wake writes flags with `test eax,eax` before its first conditional branch.

**P4:** EBX is ship. There is no EAX-zero or ECX-flight input contract at the
new window. Continuation reloads flight into EAX, clears port index, executes
`xor ecx,ecx` at `0x45e473`, clears the order, sets ECX=ship and pushes reason
1 before wake. Preserve GPRs and exact ESP; incoming flags are dead before a
native reader. Other native paths join later at `0x45e470`, `0x45e473` and
`0x45e475` (five direct branches total), outside the claimed stop stores.
Do not extend this hook through those shared order-clear/wake paths.

**P5:** EBX is ship; EAX=t is formed by `now - f+0x214 + 900` before
`0x45eff7`; ECX is target, ESI is flight. The order-8 issuer's selected native
case sets order 8 at `0x46b2ff`, deadline `now+900` at `0x46b327`, and copies
phys position into flight `+0x240` at `0x46b337`. On the ordinary pre-deadline
path t is 0..899 ms; the completion path skips P5. Restore GPRs except the
active EAX result, and preserve ESP. The active result store does not change
flags; subsequent vector scale `0x412c00` starts with `and esp,-8`, so no
incoming arithmetic flags survive to a native reader. The native tail still
does the original square/multiply/divide, including its original fault behavior.

The existing save envelope reserves 140 bytes (XMM0–7 plus EDX/ECX/EAX), so
the saved EAX result slot remains **`[save ESP+0x88]`**. An FL P5 stub can use
the AP helper argument shape `(ship=EBX, t=saved EAX)`, overwrite that saved
EAX with the returned integer, restore and jump to the FL active destination.
Do **not** write the native `[entry ESP+0x10]` before restoring and then use
AP's resume convention; FL's chosen continuation performs the store itself.

Preserve XMM0–7, exact MXCSR and incoming LastError around injected work;
retain SSE2 arithmetic and four-byte incoming stack support. The existing
light `Envelope` restores LastError before the native continuation; it must
not restore LastError again after the wake, whose native callback is allowed
to change it. None of the selected FL command, predicate, wake or vector-scale
regions contains x87 or `std`. This supports the light boundary for these
native paths; the eventual **built helper's** full transitive no-x87 audit is
still required. Source flags alone do not qualify it.

The current simulation-thread borrow is sufficient for the helpers' direct
ship/flight/target reads. Copy scalars into module state; retain no new engine
pointer across a call, wait or frame. The three helpers currently make no
native callback. The **native wake does**: `0x46a550` takes ECX=ship and one
stack reason, saves ESI, consults ship `+0x7c/+0x80` and registry root
`0x5aa174`, calls a cdecl callback with `(context, reason)` at `0x46a584`,
cleans eight callback argument bytes, clears the ship's wait token/context,
and ends in `ret 4` at `0x46a59b`. No hook needs to call or wrap that API.
The helper has returned before this callback; no helper-local GateState,
flight or target borrow may remain active across the jump to it. This is a
concrete reentrancy boundary, not proof that arbitrary callbacks cannot reenter
navigation. No lock, recursive admission framework or KC rewrite follows from
the three-site contract.

The selected native spans install no SEH/C++ cleanup frame. An injected naked
frame does not gain an unwind guarantee: synchronous memory faults or a foreign
exception crossing it are not safe pass-through returns. Normal helper returns
and refusal tails are established design contracts; runtime unwind behavior
has not been tested. The existing startup transaction, reverse rollback,
inert-live-byte pass-through on failed restore, and process-lifetime DLL pin
remain the implementation's requirements. All three selected FL addresses
have their first five bytes straddling a qword boundary (offsets 5, 5, 7);
the current writer therefore uses its plain-copy path. P4's 14-byte restore
also cannot be an eight-byte atomic restore. Keep the existing closed startup
install window; these spans do not justify live installation or removal.

## Readers, shared policy and bounded remaining work

| Consumer | Established FL contract |
| --- | --- |
| Noncapital/capital gate-target predicate | ship class/type `+0x48/+0x4a`, flight `+0x50`, target `f+0x224`, target class `0x12`; reused base layout |
| Capital classification | TShips root **`0x5a83e4`**, record stride `0xdb8`, class code `+0x14`; native predicate `0x4496e0`, ECX ship, **AL boolean**, no stack args, ordinary ret; table at `0x44971c` gives unchanged **`0x211d`** mask |
| P4 v0 stamp / P5 age | clock root **`0x5a83b0`**, game ms **`+0x708`**; the base `native_` already binds these; AP `0x606f34/+0x718` must not leak into the adapter |
| Preserved speed and optional remainder clearing | ship current/desired `+0x10/+0x14`, yaw/pitch/roll remainder `+0x30/+0x34/+0x38`, slip `+0x60..68`, identity `+8`; base evidence covers these fields |
| Capital attitude sampling | phys `+0x70` position `+0x30..38`, flight basis `+0x870` row 2 `+0x20..28`; base contract; RotateToAngles comparison remains `0xfd70` at `0x462b8d`, aligned path clears roll at `0x462ba2` |
| Launch distance consumption | target phys gate row 2 at `+0x60`; `0x412c00` scales three fixed-point components by distance, ECX vector, stack `(out, scalar*)`, EAX out, **ret 8**, EBX/ESI/EDI/EBP preserved; result added to flight point and written to phys position |

The three helpers' `gate_target_noncapital`, `gate_target_capital` and P4
diagnostic fallback still read **`sites::kShipTypeTableSlot` directly** in
this source checkpoint. A bounded FL adapter must use the existing edition
binding `native_.table` at those reads. The helpers already obtain time through
`game_time_ms()` and need no new clock API. They replicate the existing class
predicate read-only; no new native predicate call or getter binding is needed.

Minimal next work is these three claims/resumes, the earlier FL P5 input seam,
the selected root reads, and focused fixture coverage of their actual assembly
envelopes and group rollback. Reuse the existing base admission, patch manager,
CPU boundary and shared `gate_launch_units(v0,t)` policy. That function's
negative clamping and overflow saturation are existing policy; native
equivalence is asserted only for ordinary 0..899 ms values with v0=0.

The stop helpers only keep speed for a ship with existing **validated gate
state**. The base group's admission alone does not populate or qualify that
state in FL. The approach producer's gate-frame/grid validation, capital join
state and reuse/expiry must be checked before enabling that behavior. This
note does not infer a KC grid contract from unchanged class or matrix offsets.
P2's live-capital branch can also consult traffic membership and lane bounds;
P4/P5 can call `ctl_depart` (module state only). Those calls are guarded by
their existing separate capability flags. They create no reason to qualify
the full traffic/lane controller before a bounded non-traffic gate adapter;
keep those capabilities off until their own evidence exists. The arrival
placement hook and warp-in law (order `0x21`, a different occurrence of the
same constant) are outside these three claims.

Performance pass, inferred from source/control flow: P2/P4 run on arrival
events; P5 runs each command visit during the launch interval. Table lookup
and diagnostic rings remain bounded, direct state reads have no allocation,
and the helpers introduce no native callback, log, wait or lock. Preserve that
shape; preflight image bytes and spans at installation, not each P5 visit.
The change adds no per-object scan or new site-B work. Dynamic frequency,
helper time, FL flight behavior and native Windows behavior remain unmeasured.

## Approach-state closure for the bounded adapter (2026-10-29)

The additional [producer](../../../verification/results/fl-gate-approach/verify.py)
and [derived result](../../../verification/results/fl-gate-approach/verification.json)
close the concrete producer questions above at source `aba7a07a`: **67** native
semantic anchors are independently decoded from FL constructor `0x449910` and
SA dispatcher `0x46a640`; **8** changed-native/KC operand witnesses are rejected.
The initializer's class switch maps class 18 to `0x44a4d1`. Subtype minus five
at `0x44a52c`, compared to three, selects pass y `-0.05` versus `-0.55` at
`0x44a544/550`; pass z is `-0.01` at `0x44a55d`. The approach vector is exactly
`(0,-0.8,-1)` in gate flight `+0x1a0..1a8` at `0x44a56a/577/584`, followed
by the container flag. This is direct FL evidence, not an inferred AP delta.

SA cases 9/10/12/58 resolve through FL's actual dispatcher table
`0x473dc4` to `0x46a887/46acdd/46fba4/46ca5b`. For no-port case 10,
`0x46ad60` branches to `0x46ade8`: rotate gate flight approach vector by
phys rows `+0x40`, scale by gate object radius `+0xa4`, add phys position
`+0x30..38`, and write ship flight `+0x240`; `0x46ae62/6f` publish order 4
and target. Case 12 at `0x46fbf0..46fc2f` takes raw x/y/z cells into those
three point fields, publishes order 4 and target and sets port index -1.
It adds no transform or new size interpretation.

Case 9's gate path checks class `0x12`, uses the admitted capital predicate
`0x4496e0`, and gives capitals the gate position. Noncapitals draw theta
`rand(0x4000)-0x2000` at `0x46a961/96b`; `0x46aa30..46aa5a` computes
`(-y0 sin theta,y0 cos theta,z0)`, with the first product negated into the
local x. It scales by gate radius and rotates by phys rows, then adds gate
position at `0x46aa5d..46aaae`. The result is copied to flight `+0x240` and
`+0x230`; order 5 is published at `0x46aca5`. Thus the shared noncapital
order-4 and subtype-sensitive order-5 geometry validators admit the actual
FL point formulas and fail closed on a point outside their existing tolerance.

The clean FL `addon2/03.cat` member `addon2/L/x3story.obj` is compared with
clean AP `addon/04.cat` `L/x3story.obj` by the existing symbolic KC tracker,
without executing or writing either asset. FL class-2014 `GetWarpInPos`
entry `0x10e340` and AP `0x90d61` have the same **30** derived statements
after masking member/global slots and branch offsets; both decode with zero
stack-depth conflicts. They use depth `-2*(GetSize(gate)+GetSize(ship))`,
a 333000-unit / 666-m pitch and the same five columns by four rows. The
FL transit method at `0x147634` calls this method, raw explicit-point order 4,
noncapital no-port order 4, gate-angle rotation and order 5. Its surrounding
method differs from AP; whole-method equality is not claimed. Both gate and
ship `GetSize` wrappers call `SA_GetTotalSize`; FL case 58 passes obj phys
root to the previously established `0x4a39b0` cached subtree-radius getter.
The production validator still compares to object radii; different cache/object
sizes outside its existing ten-percent depth tolerance retain native behavior.

Ghidra read-only/no-analysis independently places the initializer anchors in
`FUN_00449910`, body size 6,393 addresses. Its current database does not mark
the selected dispatcher cases as functions; the gap-free Capstone decode from
`0x46a640` supplies their instruction starts. Local metadata/logs remain at
`~/.cache/x3m-port/fl-gate-approach-{context.local.txt,ghidra.local.log}`.
No modified-story compatibility, native gameplay, dynamic callback behavior,
thread admission, or in-place gate model lifetime is established by these
static findings. Existing per-call geometric checks remain required.
