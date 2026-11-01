# FL combat-music alert trigger and hold contract

2026-10-29. Bounded static study of `music_alert` only. **FL can reuse the
existing engagement/hold policy behind a small layout and register adapter.**
The proven filter seam is **`0x0042fada`**, six bytes, with **EBP=registry and
EBX=player**. The configuration clock fields moved independently of the object
fields. No production code, build, install, Wine command or game execution is
part of this checkpoint. Music keep/queue and save pumping are outside scope.

Addresses below are preferred x86 VAs, image base `0x00400000`; FL has HIGHLOW
relocations. Resolve image slots/sites from the admitted loaded-module base.
Hashes identify these evidence inputs, not an executable admission policy.

| Measured input | Bytes | SHA-256 |
| --- | ---: | --- |
| `/mnt/c/X3AP-orig/X3AP.exe` | 2,153,984 | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `/mnt/c/X3FL-orig/X3FL.exe` | 2,804,736 | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |

The [producer](../../../verification/results/fl-port/music-alert-contract/reproduce.py)
and [record](../../../verification/results/fl-port/music-alert-contract/summary.json)
are reproduced with:

```sh
python3 verification/results/fl-port/music-alert-contract/reproduce.py
```

It uses project `inspect_x3.PE32`/`compare_fl_engine.relocations` and Capstone,
rereads both files, verifies **158 FL semantic anchors**, the **19 current AP
source byte windows**, **7 gap-free routines**, **10 SA and 6 TI named native
bindings**, complete immediate caller sets and every-byte hook-edge scans.
**16 memory-only mutations are rejected**, including independently inserted
extra caller/interior-edge encodings. Raw bytes/listings remain unretained;
the six hook bytes below are the bounded patch specification. All counts are
measured static facts. Cost bounds and port recommendations are source-derived.

## 1. Alert routine, callers and hook ABI

| Contract | AP | FL |
| --- | --- | --- |
| Native alert | `0x42c890`, stack registry, `RET 4` | `0x42f910..0x42fbe7` exclusive, ECX=registry, plain `RET` |
| Immediate call | `0x41cdea` | **`0x41e55b`**, inside registry update `0x41e550` |
| Registry-update caller | `0x403f2f` | **`0x4782ca`**, immediately before render call `0x4782cf` |
| Hook / continuation | `0x42c974` / `0x42c97a` | **`0x42fada` / `0x42fae0`** |
| Displaced bytes | `8b4e088b4604` | **`8b4d088b4504`** |
| Displaced semantics | `mov ecx,[esi+8]; mov eax,[esi+4]` | `mov ecx,[ebp+8]; mov eax,[ebp+4]` |
| Registry / player / walk level at seam | ESI / EDI / BP | **EBP / EBX / DI** |

FL alert is **727 bytes, 235 instructions**. It saves EBX/EBP/EDI, reserves
`0x14` local bytes, and loads `P=[*0x5a82ec+0x38]` into EBX at `0x42f91b`.
It conditionally pushes ESI during the walk and pops it at `0x42face` before
all paths join `0x42facf`. At the seam ESP is native entry ESP minus `0x20`;
the conditional ESI save has already been balanced. EBP is restored from its
local after temporarily carrying an axis distance; it is always R at the seam,
including the null-player and non-sector branches.

`0x42fad5` branches onto the **first** hook byte; otherwise execution falls
through the max-level store at `0x42fad7`. The two displaced instructions are
3 bytes each, with no relative operand or relocation. The only raw encoded
incoming edge to the six-byte interval is `0x42fad5 -> 0x42fada`; **zero** land
inside either instruction. All internal branch targets in the full decoded
alert body land on instruction boundaries. The five-byte entry jump fits
inside one aligned eight-byte block here; installer write/rollback behavior
still belongs to the shared patch protocol and must be qualified, not inferred
from this geometry.

The appropriate cdecl handler arguments are pushed **EBX, then EBP**, yielding
`handler(registry,player)`. Preserve ESP exactly; retain EFLAGS and scratch
EAX/ECX/EDX, clear DF for validated `rep movsb` reads, and restore DF/flags before
replay. EAX/ECX are overwritten by replay and flags by the following `cmp`;
EDX need not be declared dead to use the preserving AP envelope. EBX/EBP remain
the native player's and registry's live values; preserve ESI/EDI as well.
Use the project's four-byte incoming stack ABI and SSE2/no-x87 audited handler,
MXCSR/LastError boundary, and qualified logging boundary. The decoded alert
contains no SIMD/x87 instruction; arbitrary SIMD liveness beyond this bounded
native path is not established. An emitted FL stub/CPU-state fixture remains
implementation acceptance work.

After replay `cmp ecx,eax` at `0x42fae0` selects the unchanged native
`NotifyAlert` edge logic. Each changed-level arm copies `[R+8]` into `[R+4]`
(`0x42fb31`, `0x42fb8b`, `0x42fbd6`) and clears `[R+8]`; the equal-level arm
also clears it at `0x42fbda`. Therefore suppression only needs the existing
one DWORD write **`[R+8]=0` before replay**. It must never raise a level.

The every-byte direct-call census yields one caller each for alert, registry
update, sector pass and collision pass. Neither alert nor registry-update entry
has a raw dword reference in the file. These establish immediate static entry
sets, not a proof against every computed indirect entry.

## 2. Layout and native policy witnesses

| Core requirement | Established FL value and independent evidence |
| --- | --- |
| Image slots | R=`*0x5a82e4` (`0x41e552`), M=`*0x5a82ec` (`0x42f913`), cfg=`*0x5a83b0` (`0x50b608`). All slots are in `.data` virtual extent, including zero-initialized storage; do not require them to be file-backed DWORDs. |
| Alert record | committed DWORD `R+4`, pending DWORD `R+8`; constructor zeroes at `0x41e068/0x41e06f`. Publication `0x477e37`; active handle DWORD `R+0x10`. |
| Player | DWORD `M+0x38`; live EBX already supplied at hook. It is the simulation player ship, not cockpit reference/control/render-root identity. |
| Object header | next DWORD `+0`, group WORD `+0x3c`, Flags1 DWORD `+0x40`, Flags2 DWORD `+0x44`, class WORD `+0x48`, flight/data pointer `+0x50`, parent pointer `+0x54`, render pointer `+0x70`. Alert, stance, hit handler and native setters independently consume these. |
| Parent/list geometry | parent class=1 at `0x42f939`; dereferenced list array `[sector+0x50]` at `0x42f944`; 32 headers at stride12 (`0x42f94b`, `0x42fabc`); `[header]=first`, `[node]=next`, `[node]==0` terminates. |
| Distance | `[obj+0x70]+0x30/+0x34/+0x38` signed axes, Chebyshev with two's-complement absolute differences, inlined at `0x42f9fa..0x42fa9f`. Strict range `<2,500,000` with `[R+4]==0`, otherwise `<5,000,000`. |
| Relation | native stance `0x45a300..0x45a38f` (48 instructions), ECX=subject, stack other, `RET 4`; relation `0x45a390..0x45a3ef` (38 instructions), ECX=subject, stack other. `SA_IsEnemy` binding case `0x4711b8` calls the latter at `0x4711e6`. |
| Relation fields | classes5/6/7, signed group check, neutral Flags2 `0x40000000`, flight friend/foe DWORDs `+0x1cc/+0x1d0`, enemy-of-enabled DWORD `+0x1d4`. Native setter `0x4711a2` independently writes the enable field. Missile class10, Flags1 `0x01000000`, launcher `[data+0xc]` keep the special hostility rule. |
| Player ownership | group WORD `+0x3c`; `SA_SetGroup` case `0x46ce31 -> 0x45a1d0`, write `0x45a1d8`, player-group10 special branch `0x45a1dc`; separate ship policy compares group10 at `0x45a577`. Preserve the AP group10 policy constant. |
| Ship attack | class7 only; flight target DWORD `+0x224`, order WORD `+0x258`; named `SA_AttackObject`/`Big` cases `0x46b53a/0x46b752` write orders `0x10/0x11` at `0x46b588/0x46b7a0`, targets at `0x46b592/0x46b7aa`. Reference retirement clears a matching target at `0x453ef5`. |
| Active cockpit | R+0=`map`; map+0=`buckets`, map+4=`count`; index `(count-1)&handle`; link `{next,key,value}` at `+0/+4/+8`. Native resolver `0x41e420..0x41e456` (21 instructions), target selection uses active handle at `0x44e3c2` and resolver at `0x44e3c5`. |
| Selected target | cockpit DWORD `+0x1e0`, copied to firing target at `0x44e3f7/0x44e3fd`, also `0x44ed1e`. Keep bounded lookup and list-membership validation before dereferencing it. |
| Fire counters | M DWORDs `+0x40/+0x44`, incremented at `0x44f7ac/0x452961`; mark the created projectile Flags2 `0x100`. FL also propagates this bit at `0x4535af` from an existing projectile, without another shot-counter increment. |
| Hit counters | M DWORDs `+0x48/+0x4c`, independently named SA getters at `0x46ee8f/0x46eea8`; symmetric hit paths increment at `0x46830c/0x468323` and `0x468572/0x468589`. |
| Hit lifetime | victim Flags2 `0x40` indicates player projectile hit; victim `0x80000` indicates hit. Both hit paths require projectile `(Flags2 & 0x900)==0x100` before player accounting, preventing duplicate accounting via bit `0x800`. Collision pass clears both per-frame bits at `0x466e27` before simulation. |
| Missile pending2 | alert walk checks missile `[data+0x10]==P` at `0x42f9f1` and sets EDI=2 at `0x42fab3`; simulation tracks the homing target's player Flags1 bit2 at `0x45d315..0x45d32b`, then maximizes `[R+8]` with 0/2 at `0x462ce6..0x462cfe`. |

FL inlines the same relation orientation as AP: with player enemy-of-enabled,
test candidate's stance toward player first, then player's stance toward
candidate. Its native walk excludes ship subtypes `0x10f/0x140/0x141/0x13f`,
as AP already does. Those are native vanilla preconditions, not new terms for
the shared engagement policy. The pending2 maximum from simulation survives a
later walk level1; the walk-local level itself can fall from2 to1 on a later
list, preserving AP's quirk.

## 3. Clock, lifecycle, thread and reentrancy

**Clock layout differs:** cfg step **`+0x704`**, absolute game milliseconds
**`+0x708`**, SETA operands **`+0xbc/+0xc0`**. The named TI table at `0x5a6510`
is independently bound to dispatcher `0x4cdf30`, implementation `0x4cdf50`,
case table `0x4ce1e4`. `TI_GetAbsTime` case `0x4ce057` reads `+0x708`;
`TI_GetLastFrameDelay` case `0x4ce077` reads `+0x704`; `TI_SetAbsTime`
case `0x4ce140` rewrites `+0x708` and clears step. Native clock frame routine
`0x50b600..0x50b666` (28 instructions) calculates bounded1..`0x7fff` step.

Tick `0x4ffe30` adds to `+0x708` through `0x4fff67`, using the signed Q16
product of `+0xbc` and `+0xc0` and clamped1..200 wall-ms step. Its fixed-point
products include `+0x8000` rounding. The core's `(a*b)>>16` value is diagnostic
SETA telemetry, not the hold clock calculation; keep actual `+0x708` for hold
expiry so SETA and time rewinds retain native semantics. A separate override
at `0x4fff08` can also advance the clock; don't synthesize time from steps.

Normal main-loop order is frame/script work `0x477ebf -> 0x4788b0`, including
`0x4788b1 -> 0x4cdaa0 -> 0x50b600`, then sector pass
`0x477ed2 -> 0x443b10`: collision `0x443b42 -> 0x466de0` clears/hits,
simulation `0x443b49 -> 0x45c810` updates missiles/orders, then registry update
`0x4782ca -> 0x41e550 -> alert` before rendering. Thus native alert frequency
is **once per eligible main-loop iteration**, independent of cockpit count,
including zero-player/non-sector cases. Paused loop paths skip the sector pass
at `0x477ecb` while reaching later cockpit/render work; stale hit flags can
therefore repeat stamps, just as the AP note warns. No new pause policy is
established by this mapping.

This is the same synchronous update/render owner characterized by the
[own-ship lifetime study](own-ship.md), sections1/3. It supports
one owner for policy state under the healthy native path; it is not a measured
OS thread-ID census or an engine-wide cross-thread exclusion proof. Reacquire
objects every invocation and retain no cockpit/object/flight/render pointer
in policy state. Validated memory reads reject unreadable spans but do not pin
a page or protect against a concurrent free. The established native entry and
callback closure identifies no new FL cross-thread/reentrant path relative
to AP; it does not justify adding a separate owner/load framework to this
synchronous, no-engine-call handler. Section5 closes the integration question.

The filter itself needs **no native engine call, script, allocation or callback**,
so there is no added synchronous engine reentry window between reading R and
writing R+8. `NotifyAlert` and its callback-capable VM work remain after replay;
don't extend the filter across them or sample retained objects afterward.
The native alert contains no SEH registration and no indirect call; this does
not prove that VM callees cannot throw. A same-stack synchronous handler leaves
native return/exception routing intact after it returns; unlike a return-slot
substitution, there is no pending native-return observer to unwind. An exception
inside added reads/logging itself still needs the shared handler boundary;
VirtualQuery validation alone is not exception recovery.

## 4. Shared policy and smallest adapter

All six existing reasons retain their inputs: **a** rising fire counters plus
selected hostile found in-sector; **b** player hit bit; **c** nearby group10
object hit or class7 attack on hostile; **d** vanilla pending2; **e** rising hit
counter plus hostile player-hit victim; **f** hostile class7 attack on P or
nearby group10 object. Preserve first-level2,500,000 range for c/f, not vanilla's
doubled stay range. Stamps and strict `age < hold_ms`, no-stamp/rewound-clock
expiry, and lowering-only output require no FL-specific policy copy.

Recommended implementation boundary: shared `State`, relation/walk and frame
policy; an edition layout/binding supplies the **two resolved image loads**
(cfg/M), four clock/SETA offsets, and hook spec/register envelope. All object,
list, registry, cockpit, flight, counter and bit constants above match. Avoid
a generic Mem address-rewriter that compares every read address; specialize
layout constants or pass the small binding directly. Do not add a second
sector traversal or per-frame engine-call capture to recover P: EBX already
carries the native value. This is a recommendation, not a production edit.

Source bounds remain **six validated spans/two image loads on a quiet valid
sector frame**, one lazy cached header/flight/position per needed object,
one shared walk only for nonzero vanilla level or rising fire/hit counters,
lists5/6/7/10, total3600 nodes, per-list4096, cockpit chain32. Native FL already
walks all32 lists unbounded each iteration. Config/edition selection belongs
at admission, not inside every node. No FL wall-time/FPS or object-count
measurement is claimed.

Retain counter baselining on first sample, player-pointer change, time rewind
and the existing shared cadence marker; a falling counter itself is not a
shot/hit. The Present path already calls `music_alert::loaded()` for both
editions and consumes an interlocked pending flag in the handler. No separate
FL native load marker is required for AP-equivalent behavior; see section5.

Production acceptance still requires the FL binding and stub, exact-ESP and
CPU/LastError checks, hook install/rollback and shared read-refusal checks, the
affected shared policy tests, and user combat/pause/SETA/load captures. Native
Windows behavior is unverified. This note establishes a static port contract,
not gameplay acceptance or an installed feature.

## 5. Existing load marker and synchronous entry: integration clarification

`capture.cpp::present` already invokes
`game_phases::loading_phase_present(device,reset,frame)` after native Present,
then calls `music_alert::loaded()` on its true result, alongside FOV and music
keep notifications. This call is not edition-gated. Despite its directory/name,
`game_phases::loading_phase_present` requires neither the optional AP diagnostic
hook group nor any engine address: it uses QPC and value-only `LoadingPhases`.
**There is no FL native marker to relocate.** Preserve this existing path.

The actual marker contract is narrower than its name: on one device/reset
generation, the first Present gap >=3s emits `menu_shown`; the second emits the
paired `save_load_begin/save_load_complete`. Each emits once per process;
device/Reset changes only re-anchor it. Later loads do not re-emit. Fast loads,
unrelated long gaps or alternating devices can omit/misclassify it. These are
the existing AP limitations documented in
[loading observations](../loading-observations.md#phase-markers), not an FL-specific
load defect. FL's actual gap/cadence classification remains user-run evidence.

`music_alert::loaded()` only sets the existing interlocked pending bit. The
next alert frame consumes it and establishes counter baselines. Independently,
the common frame policy establishes a baseline on first sample, changed player
pointer or a backward game clock; falling counters cannot themselves trigger
fire/hit. These work on FL with the mapped player and clock inputs exactly as
on AP. Neither edition's code proves that a later load can never reuse the same
player address with a forward clock and larger restored counters after the
one-shot marker is spent. That conditional residual is shared AP behavior,
not evidence requiring an FL load observer or a new acceptance prerequisite.
Do not strengthen the port silently or touch music keep/save pumping here.

For entry context the producer now compares **all raw relative entry edges**
for alert and registry update in both editions: AP has only
`0x41cdea -> 0x42c890` and `0x403f2f -> 0x41cde0`; FL has only
`0x41e55b -> 0x42f910` and `0x4782ca -> 0x41e550`. Neither edition has a raw
function-pointer reference to these entries. Reuse the existing FL
[native callback closure](ui-render-exclusion.md#1-native-owner-thread-and-queued-location-changes)
and [own-ship/main-loop evidence](own-ship.md#3-why-the-existing-material-seam-can-own-this-copy):
native callbacks queue location/result/input state, rather than invoking another
outer loop, and normal simulation/cockpit updates are synchronous. The filter
adds no native call, callback, pump, allocation or return-slot substitution.
No additional reachable FL cross-thread or recursive entry was identified.
The interlocked load notification remains sufficient for its existing
cross-thread signal contract; policy state stays on the native alert owner.
Deliberate foreign private-entry calls remain outside both supported profiles.

Consequently the smallest supported adapter remains the six-byte hook/register
mapping plus resolved globals and clock/SETA offsets, sharing policy and the
unchanged `loaded()` signal. Actual gates are exact profile/claim and emitted
CPU/stack/state qualification, affected policy/read-refusal checks and the
ordinary user combat/load/pause/SETA run. New owner admission, reentry machinery
or native load hooks are not gates absent a concrete new failure witness.
