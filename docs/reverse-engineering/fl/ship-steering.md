# FL base ship-steering hook contract

2026-10-29; read-only comparison at production source checkpoint `014201db`.
The next bounded adapter is the existing **four-site base
`X3M_SHIP_STEERING` group**: A yaw/pitch, A roll/yaw, A roll/pitch, B speed
clamp. This establishes its native seams and selected data bindings; no FL
steering code, installation or runtime qualification was added.

The AP algorithm and earlier AP findings remain owned by
[ship movement](../ship-movement.md), [navigation AI](../ship-navigation-ai.md), and
[steering architecture](../../architecture/ship-steering-and-avoidance.md).
Avoidance E, gate stop/launch, traffic, lane and docking-approach enhancements
are separate groups and are not admitted by these findings.

## Evidence and selected source group

Clean inputs, measured again from the named originals:

| Image | Bytes | SHA-256, provenance only |
| --- | ---: | --- |
| `/mnt/c/X3AP-orig/X3AP.exe` | 2,153,984 | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `/mnt/c/X3FL-orig/X3FL.exe` | 2,804,736 | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |

[`verify.py`](../../../verification/results/fl-ship-steering-entry/verify.py)
uses the project's PE mapper and objdump instruction parser. Its
[compact result](../../../verification/results/fl-ship-steering-entry/verification.json)
records eight spans, AP/FL whole-function fingerprints, return sites, direct
call counts, operand assertions and selected order tables. Eight altered-hook
cases are rejected, and the unsafe shorter FL B candidate is rejected for a
real incoming branch. Run from the repository root:

```sh
python3 verification/results/fl-ship-steering-entry/verify.py
```

Ghidra 12.1.4 independently corroborated the FL function extents and leaf
references, using existing `FLRegistry/X3FL.exe` with `-readOnly -noanalysis`.
The bounded [script](../../../verification/results/fl-ship-steering-entry/SteeringEntry.java)
accepts a local output pathname. Raw results stayed outside the worktree at
`~/.cache/x3m-port/fl-steering-entry-local/`; they are not tracked. Example:

```sh
~/ghidra_12.1.4_PUBLIC/support/analyzeHeadless \
  "$PWD/verification/results/fl-port/registry-writers/ghidra" FLRegistry \
  -process X3FL.exe -readOnly -noanalysis \
  -scriptPath "$PWD/verification/results/fl-ship-steering-entry" \
  -postScript SteeringEntry.java /tmp/x3-fl-steering.local.c
```

Source inventory: `src/engine/ap/ship_steering_install.cpp::install_all`
claims exactly four base sites, preflights all four, pins the module, chains
tails before publication, rolls back in reverse and leaves failed restores
passing through while `installed_` is false. `_decide.cpp` supplies site A,
`_clamp.cpp` supplies B, `_sites.h` holds the native bindings and stub Frame,
`_internal.h` the getter wrappers/CPU envelope. `_core.h` contains shared
arithmetic and bounded ship state. The optional groups are not independent
substitutes for the base: all require this group first.

`initialize` makes the base option on for unset/empty input at this checkpoint,
`0` off, `1` on; invalid text refuses it. The empty-input behavior comes from
`initialize`'s `on=true` and length test, despite the older generic parser
comment saying unset is off. `X3M_SHIP_STEERING_CLASSES` excludes the named
SG_SH codes, rather than selecting classes to enable. Turning on the base must
not accidentally turn on the AP-bound optional groups in FL.

## Four hook spans and continuations

All addresses below are preferred VAs at image base `0x400000`. For FL use the
already admitted loaded image base plus RVA; AP absolute addresses are not FL
bindings. None of the displaced instructions below embeds an absolute address
or relative control transfer, so each uses plain-copy relocation (`rel32=0`).

| Site | AP span / tail continuation | FL span / tail continuation | FL instructions and short bytes |
| --- | --- | --- | --- |
| A0 yaw/pitch | `0x44ccc0`, 5 B / `0x44ccc5` | `0x456450`, **7 B** / `0x456457` | `sub esp,0x10; mov eax,[esp+0x14]`; `83ec108b442414` |
| A1 roll/yaw | `0x44c620`, 5 B / `0x44c625` | `0x455cd0`, **7 B** / `0x455cd7` | `mov eax,[esp+4]; sub esp,0x10`; `8b44240483ec10` |
| A2 roll/pitch | `0x44c970`, 5 B / `0x44c975` | `0x456090`, **7 B** / `0x456097` | same two instructions/bytes as A1 |
| B object continue | `0x4593dd`, 5 B / `0x4593e2` | `0x462c72`, **9 B** / `0x462c7b` | `mov ebx,[ebx]; mov [esp+0x108],ebx`; `8b1b899c2408010000` |

Measured FL leaf function extents, exclusive end: yaw/pitch
`0x456450..0x456803`, roll/yaw `0x455cd0..0x45608e`, roll/pitch
`0x456090..0x45644e`. Each has one `ret 4`, respectively `0x456800`,
`0x45608b`, `0x45644b`. The dispatcher is `0x456810..0x4569f8`.
The command pass is `0x45c810..0x462efe`.

Every proposed span starts and ends on independently decoded whole instructions.
The global linear `.text` control scan and unaligned dword scan of
`.text/.rdata/.data` find **zero interior references** for all eight spans.
This is a bounded static census, not a universal proof against computed jumps
or foreign patches. Ghidra leaf references agree: AP has 17/2/2 direct calls
to A0/A1/A2; FL has 16/2/2, each including one dispatcher call. Both have 15
command-pass calls to their dispatcher. Therefore the original AP total of 33
command-pass steering/dispatcher call sites becomes **32 in FL**, not 33.

For B, the FL census finds 18 direct branches and one `.text` table dword at
`0x462f1c` landing on `0x462c72`; AP has 100 direct branches and two table
dwords (`0x4595fc`, `0x459690`) landing on `0x4593dd`. The FL predecessor
`0x462c6e` reloads sector into ESI from `[esp+0x24]` and has 78 direct
branches. A six-byte candidate beginning there would contain the separately
entered `0x462c72` at offset four. **Do not claim that shorter window.**

B runs once per object visit, after the native order step or its skip path and before advancing
EBX. The replay writes the next object into the native stack local before
`cmp [ebx],0` at `0x462c7b`; `jne 0x45c911` follows. The speed-slew bucket
walk begins at `0x462d13`, reads desired speed at `0x462de0` and writes current
speed at `0x462ec7`. Thus a B clamp is consumed in the same command pass,
retaining AP's zero-frame scheduling relationship.

## ABI, CPU state, lifetime and reentrancy

At **AP A entry**, EDX is target `int32[3]*`, `[entry ESP+4]` is obj.
At **FL A entry**, **ECX is obj, `[entry ESP+4]` is target `int32[3]*`**.
The target is a read-only borrowed buffer, often a caller's local; object is
not a stack argument in FL. Both clean one four-byte argument with `ret 4`.
EAX/EDX are scratch in FL; ECX is an input and is then reused. EBX/EBP/ESI/EDI
are saved by each native leaf. Incoming arithmetic flags are dead: the entry
`sub` overwrites them before a native reader, or follows only `mov`.

A handled replacement writes obj `+0x30/+0x34/+0x38` and returns with the
original stack plus `ret 4`. A refused replacement restores incoming state and
runs the claim tail with ESP exactly as at entry. AP's `Frame` cannot simply
be reinterpreted for FL: AP Frame `edx` is the point and Frame `obj` is the
stack argument, whereas the FL saved ECX is obj and the stack argument is the
point. Normalize those two scalar inputs in the FL boundary. If target
redirection is added in a later group, change the FL stack argument supplied
to the native tail, not EDX and not the caller's point contents.

At **FL B**, EBX is the just-processed object and all GPRs/ESP must survive the
helper. The two replayed instructions do not consume flags; `cmp` at the
continuation replaces arithmetic flags before `jne`. The AP asm class-7 /
autopilot-`0x80000` filter can retain its shape because obj `+0x48/+0x40`
are established FL fields. Preserve XMM0–7 around injected work at A and B;
FL is extensively SSE2-native and cannot inherit AP's old “game uses x87”
reason as an excuse to clobber its XMM state. The current AP envelope saves
140 stack bytes (128 XMM + three GPRs), uses unaligned moves, clears DF and
restores LastError/MXCSR. The FL boundary must retain those obligations and
the four-byte incoming stack contract. DF=0 is the compiled-call precondition;
none of the selected native functions contains `std`.

The five FL getters and their complete transitive native helper graph contain
**zero x87 instructions, zero indirect calls and no recursive edge** in the
selected decode. The graph is max-speed → `0x443ee0`, `0x4581c0`,
`0x419fc0` → `0x412b50`; the other four getters are leaves. These are integer
return paths, so the light MXCSR/LastError boundary remains a viable design.
An eventual compiled adapter still needs its own transitive no-x87 audit;
source flags alone cannot establish this. Keep SSE2 math, stack realignment,
exact incoming MXCSR restoration, and no float/double ABI return helper.

Native scheduling is corroborated by `0x443b10`'s two sector loops:
`0x443b49` calls the command pass with ECX=sector; `0x443b76` calls the
integrator `0x463000` with ECX=sector in the later loop. The driver is called
at `0x477ed2`. The leaf/dispatcher graph is nonrecursive and only enters
math code; the new base helper need not call back into navigation/KC.
That establishes a short native borrow for the current object and target,
not a general cross-thread lifetime guarantee. Shared ship-table/ring state
assumes the admitted simulation thread and must not span an introduced callback,
wait or lock. Existing FL ship `+8` identity evidence is reused from
[chase transition](chase-transition.md#4-roots-public-apis-and-shareable-policy).
Persist copied scalar policy state; do not retain a target-buffer pointer.

Neither native leaf nor command entry installs an SEH/C++ exception frame.
Their normal paths establish no cleanup to reproduce in the displaced spans.
The injected naked frame has no new unwind guarantee: a synchronous memory
fault or foreign unwind must not be described as a safe pass-through return.
No native unwind or runtime reentrancy was tested here. Installation/refusal/
rollback remain an implementation acceptance obligation, including a failed
restore's inert helper and the existing process-lifetime DLL pin.

## Bindings and concrete differences

| Binding | AP | Established FL binding / evidence |
| --- | --- | --- |
| maximum speed getter | `0x450980`, obj EAX | `0x45a450`, **obj ECX**, integer EAX, plain `ret` `0x45a5b1` |
| acceleration getter | `0x450b80`, obj ECX | `0x45a610`, obj ECX, integer EAX; record `+0x48`, tuning flight `+0x274` |
| pitch / yaw / roll caps | `0x450cf0 / 0x450d60 / 0x450dd0`, obj EAX | `0x45a7d0 / 0x45a830 / 0x45a890`, **obj ECX**; record `+0xc/+8/+0x10`, flight tuning `+0x270` |
| TShips pointer slot | `0x606fd4` | `0x5a83e4`, repeated at `0x45a64c`, `0x4632f5`, `0x4496a1` |
| record stride / class / rotation acceleration | `0xdb8 / +0x14 / +0xd0` | same; `0x45a646`, `0x4496a6`, `0x4632fa` |
| clock slot / game time / frame delta | `0x606f34 / +0x718 / +0x714` | **`0x5a83b0 / +0x708 / +0x704`**, `0x45c81c`, `0x45f224`, `0x45c82a` |
| ship speed / desired / remaining angles | `+0x10 / +0x14 / +0x30..38` | same; native slew and all three leaf stores |
| obj class/type, flags, flight, phys, docking | `+0x48/+0x4a`, `+0x40`, `+0x50`, `+0x70`, `+0xf8` | same; leaf prologues, command filters and integrator `0x4632cc..0x4632e4` |
| position / flight basis / slip | phys `+0x30..38`, flight `+0x870`, obj `+0x60..68` | same; `0x456468..0x456470`, `0x4564aa`, `0x463f90`, `0x463fa4`, `0x463fb5` |
| native angular plant | flight `+0x2b0` × record `+0xd0 / 100` | same at `0x4632fa..0x463323`; autopilot flag remains `0x80000` |
| large-ship code mask | `0xe91d` | same measured mask from `0x449690`'s class-7 predicate and table `0x4496cc` |

The FL max-speed getter additionally applies a timed factor at flight
`+0x914/+0x918` (`0x45a524..0x45a574`). Calling the native FL getter retains
that policy; replacing it with an AP-style direct `record+0x44` read would
lose it. The getter also mutates expiry state: with a nonzero dword factor at
`flight+0x914`, it subtracts the dword deadline at `+0x918` from game time at
`0x45a539`. A signed result at most zero retains the factor; otherwise
`0x45a543` clears DWORD `[ecx+0x914]`, reloads flight from `obj+0x50`, and
`0x45a550` clears DWORD `[eax+0x918]`. Calling the native getter preserves
these side effects. This is a concrete reason to bind the existing getter boundary.
Getter nonvolatile preservation is visible in the full functions; EAX result,
ECX/EDX/flags scratch is a safe caller contract. All five FL calls can use the
existing `call_ecx` shape; AP keeps its current EAX/ECX choices.

## Order policy and minimum next acceptance

The FL native order switch still reads flight `+0x258` and bounds it by
`0x24` at `0x45db2f..0x45db46`. Its tables are `0x462f5c` and `0x462fb4`.
The result records the entries for all 14 steered order words, corresponding
to AP's existing policy: 4/5/6, `0x10/11/12/14/15/18/19/1d/22/23/24`.
The shared core continues to decide which have arrival ramps, roll zero and
vanilla pass-through; no new navigation state machine follows from this RE.

Selected FL semantic witnesses: MovePos (`0x19`, entry `0x46009d`) supplies
flight `+0x240` to the leaf/dispatcher, compares arrival against `+0x260` and
computes braking speed; MovePosContinue (`0x23`, entry `0x460157`) retains the
alignment speed law. Order 4 (`0x45e70b`) retains target-class gate/ship tests,
3/4 gate radius and 111,000-unit carrier threshold, or radius bounded by
22,200..111,000 (`0x45e851..0x45e8bf`). Order 5 (`0x45dd7f`) retains flight
port index `+0x250`, writes roll after the yaw/pitch leaf, and checks each
remaining angle against `0x51e` (`0x45e4d1`, `0x45e4e4`, `0x45e4f3`). Those
facts support the existing base ramp/roll/port-hold policy. AP's 500 units/m
conversion and controller tuning are inherited policy, not FL flight measurements.

There is sufficient static evidence for the four-site base boundary and the
five getters. The minimal implementation work is to edition-bind those sites,
entry argument normalization, roots/clock offsets and getter ABI while keeping
`core::steer` / clamp arithmetic shared. Use existing executable/profile
admission rather than AP-only `object_trace::executable_verified()` as FL
proof. Keep separately unqualified avoidance/gate/traffic/lane/dock branches
inactive; their defaults in the current AP initializer cannot serve as FL
admission. No new generic hook registry or lifetime framework is required.

Remaining acceptance is concrete: a focused synthetic fixture must exercise all
three FL entry frames and `ret 4`, refusal tails including the B stack store,
register/XMM/MXCSR/LastError preservation, relocation of the FL bindings,
all-or-nothing install rollback, and reentry/exception refusal contracts of the
actual helper. Audit the built call graph for x87. Runtime frequency, cost and
flight behavior remain unmeasured; user flight is required after implementation.
No PE build, native fixture, Wine command, game launch or install ran for this RE.

Performance pass (source/control flow, inferred cost only): A runs at native
steering calls; B visits every native object, so retain the two class/autopilot
asm tests before any C++ envelope. Current AP hot work is fixed ship-table
lookup (at most eight probes), scalar input sampling, five native getters and
SSE2 steering; B samples current state and clamps. Timing/debug counters are
already gated; formatting runs at the existing frame boundary. The FL boundary
adds only argument/root/getter selection and a larger replay span, not a new
per-object allocation, scan, lock, registry walk, signature validation or log.
Preserve that shape. Static call-site counts are not dynamic call frequency,
and no FL time/FPS claim is made.

Independent consequential review accepted this four-site contract as implementation
input, including targeted instruction/getter verification. The expiry-write
clarification above records the review finding; runtime qualification remains open.

## Port (2026-11-03)

`ship_steering` (+ `ship_steering_classes`) now runs on FL: `src/engine/fl/ship_steering_fl.cpp` with
`ship_steering_sites_fl.h` and the state header `ship_steering_fl_internal.h`, linked in place of the AP
`src/proxy/ship_steering_*.cpp` (AP only). The FL glue is the AP base path of `decide()` / `clamp_apply()` on the FL
bindings; the decision `core::steer`, the clamp `core::clamp_for` / `clamp_units`, the servo `core::servo_tick`, the
class parser and every policy constant are the shared `ship_steering_core.h`. The gate, capital, traffic, lane and
dock groups are deliberately **not ported**: each logs one row `<group> installed=0 edition=fl status=ap_only
reason=not_ported` at initialize (`ship_steering_gates`, `_gates_capital`, `_gates_traffic`, `_gates_lane`,
`ship_steering_dock`), nothing read or claimed; their ini text says "X3: Albion Prelude only for now". `X3M_SHIP_TRACE`
logs the same `ap_only` row when set.

Bindings as established above: the four spans at base + RVA (A0 `0x056450`, A1 `0x055cd0`, A2 `0x056090`, 7 B; B
`0x062c72`, 9 B), each claimed through `engine_patch::claim` inside the install window, read back and its write path
checked (all four `lock cmpxchg8b`); obj in ECX and the target at the entry's stack argument (a detour redirects that
argument to the module's ring, never EDX or the caller's buffer); the five getters called with obj in ECX; the TShips
slot `base+0x1a83e4`; the clock `base+0x1a83b0` with game time `+0x708` and frame step `+0x704`. Before any write the
install compares 24 identity windows (FNV-1a with the relocated dwords zeroed and their targets bound to the loaded
base): the leaf entries and their remainder stores, the loop continue, the slew's read and write, the five getters, the
two class predicates and tables, the order switch and its byte table, the command pass's clock read and the integrator's
plant. All four or none: a failure rolls the claimed sites back in reverse. The FL loader has no unload path; `shutdown()`
serves the fixture.

Stub envelope (every stub, A/B/E): PUSHFD, EAX/ECX/EDX, XMM0–7 (`movdqu`), FNSAVE (the whole x87 image; the handler
runs on a reset FPU), STMXCSR, CLD; the C handler under `LightCallBoundary` (LastError and MXCSR, MXCSR 0x1f80 inside)
and `force_align_arg_pointer` at four-byte incoming alignment; then LDMXCSR, FRSTOR, XMM, GPRs, POPFD. B tests class 7 and
the autopilot bit under its own PUSHFD/POPFD before the envelope. The built objects contain x87 only outside or inside
that FNSAVE boundary (6 `fildll`, 6 `fstpl`, 4 `fadds`, in the row formatting and float returns; measured with objdump).

**Deliberate FL differences** (measured by the native fixtures; AP is unchanged):

1. **Game-time scaled bounds.** Site B's decision age, the order-5 port hold's gap and the leader's lookahead age use
   `max(250 ms, 4 × frame step)` (step capped at 250 ms, `av::fresh_window_ms`), so a SETA frame step does not leave every
   B call stale. AP keeps the fixed 250 ms. With steps ≤ 62 ms both are 250 ms.
2. **Recycled pointer.** A table entry carries the native id `obj+0x08` of its object. When a decision finds another id
   behind the same address, the ship's steering and avoidance state are reset (`recycled=` in the summary); B does not
   clamp from a decision of another id, and E does not answer from it (`e_recycled_a`); a resolved B whose id changed
   since the resolve is not suppressed (`e_resolved_id_mismatch`). AP has no such reset.

Known cap, shared with AP's site E: `av::fresh_window_ms` caps the step at 250 ms (`ship_avoidance_core.h`), so no
window exceeds 1 s; at game-time frame steps above 1 s every B clamp, port hold and leader lookahead is stale on the
next frame (the engine's own speed and leaf law then stand), as AP's E window already is. The shared core is unchanged
(AP objects identical).

Bindings no window or verifier check covers, resting on the earlier RE above and on the X3FL.exe identity (timestamp and
image size at load): the docked word `obj+0xf8`, the flight target `f+0x224`, port index `f+0x250` and arrival distance
`f+0x260`, and the rotation rates `obj+0x24..+0x2c`. The windows pin the spans, the getters, the predicates, the order
switch, the clock reads, the slew and the integrator plant; these other fields are plain loads of the AP layout.
The max-speed getter's equipment branch (`0x4581c0`, `0x419fc0`; taken when `obj+0x40` lacks `0x40000000` and the
flight override `+0x27c` is negative) runs in game but is not executed by the fixture: it reads equipment tables the
mapped image leaves empty, so the fixture sets that flag. The verifier walks it statically (no x87, SIMD or indirect call).

Verified (host, measured 2026-11-03): `verify_ship_steering_sites_fl.py` PASS; the native fixture
`verification/probe/fl_ship_steering.cpp` replays the 7,093 `ship_trace` rows the engine-model harness emits
(`ship_steering_host.cpp --emit-trace`) through the claimed stubs and the real FL getters against the shared core with the
AP glue's input construction: 5,689/5,689 A calls equal (remainders and the 64-byte ship entry), 7,093/7,093 B calls equal
(373 clamps), the class filter `TS` (3,961 rows excluded, all equal), 16 SETA cases, a recycled pointer, CPU state 32/32,
24/24 window flips and 4/4 read-back faults refused without a write. Evidence:
[farnhams-legacy.md](../../verification/farnhams-legacy.md), "ship_steering and ship_avoidance on FL (host evidence,
2026-11-03)". Not verified: in game (not flown), native Windows game behaviour, the max-speed getter's equipment path
(`0x4581c0`/`0x419fc0`, skipped in the fixture by flag `0x40000000`), dynamic call frequency and FPS cost.

Per-call cost (inferred, not measured): the envelope adds PUSHFD/POPFD, three push/pop pairs, 16 unaligned XMM moves,
FNSAVE + FRSTOR, STMXCSR/LDMXCSR and the LastError pair, about 300–450 cycles (≈0.1 µs at 3.5 GHz) per A, steered B or
E call, dominated by FNSAVE/FRSTOR; B's filtered path (every non-ship or manual object) costs PUSHFD, two compares and
POPFD, about 25 cycles. The handler's own work is the AP base path (five getter calls, the core, no allocation, no
lock). At 200 AI ships with three leaf calls each that is roughly 60–90 µs per frame of envelope (inferred).
