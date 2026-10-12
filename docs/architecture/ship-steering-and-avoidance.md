# Ship steering and predictive avoidance: replacing the steering leaves at site A

Design note, 2026-10-12. Decision: how the proxy replaces X3: Albion Prelude's ship steering with a
smooth, curvature-limited law (item 1) and adds predictive collision avoidance (item 2) for every ship
that flies under an engine order, on native Windows and under Wine, without touching docking, gate
transit, combat balance or the savegame format. Sector path planning (item 3) is a separate note; §9
states the interface this layer offers it. Grounding: [ship-movement.md](../reverse-engineering/ship-movement.md)
(integrator, per-axis controller, leaves, hook sites), [ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md)
(order model, CollisionWarn loop, candidate sites), [game-scale-and-ship-classes.md](../reverse-engineering/game-scale-and-ship-classes.md)
(units, per-class dynamics, §10 proposal) and the hook infrastructure (`src/proxy/engine_patch.h`,
`fov_sites.h`, `media_cue.cpp`, `chase_transition_restore_core.h`, `log_tiers.h`,
[logging-tiers.md](logging-tiers.md), [platform-portability.md](platform-portability.md) "Writes into the
`engine_patch` arena"). Nothing was launched. Markers: **[m]** a byte, table or script output quoted
from those notes; **[i]** inferred here; **[p]** a design choice. Derived numbers:
[`verification/results/ship-steering/design_numbers.py`](../../verification/results/ship-steering/design_numbers.py)
(output beside it).

## Recommendation

**Ratified 2026-10-12 (main session)** with three amendments: (1) `ship_steering` and `ship_avoidance`
default off in the schema and template; a run candidate enables them in the user's `x3m.ini` for the
acceptance flights, and the default is revisited after those flights. (2) Step 1 claims A1–A3 and B only;
site E is claimed in step 2 and only after the EFLAGS/`&out` facts of the last section are read from the
listing, otherwise avoidance ships without E. (3) The four open facts of the last section that gate a claim
(getter conventions, slew-block order, E's caller contract, the station node root) are pinned by the
implementing agent's own listings before the corresponding site is claimed; a fact that cannot be pinned
takes the documented fallback, never a guess.
**Amendment 4 (2026-10-12, step-1 review):** order 0x23 MovePosContinue gets the cornering clamp only, no arrival
ramp: the engine's 0x23 case writes `[obj+0x14] = vmax·f(alignment)` with no distance term (`0x00456705`–`0x0045671b`)
and completes at `f+0x260`, so the ramp of §2.5 (which listed 0x23 by mistake) would stop patrol ships at every
waypoint. Also from that review: orders 4 and 5 write roll `obj+0x38 = 0` (the vanilla leaf's value; the docking match
`0x00454767`–`0x004547d6` runs only when all three angles are below `0x51e`), the heading is held inside the order's
arrival distance (§2.1), a record with `v_max = 0` or `a = 0` gets no speed clamp at all (§6.2), and the linear tail's
τ is 0.15 s (0.3 s made the final approach of fighters slower than the vanilla leaf by 3–5 %; the host fixture now
asserts "no slower than vanilla").
**Amendment 5 (2026-10-12, step 2 as built; details and the fixture findings in the ledger's Step 2 section):** the
lookahead's reaction term is 1 s and L adds the rate-ramp distance `2·v·ω/α`; the detour offset is 1.3 C for bodies
and 1.75 C for ships; a detour in progress keeps its conflict to 2 C and never changes side; the side memory is a
world direction; the plane of least turn uses the body's current direction; the vertical family is world up with an
identity tie-break for symmetric head-on pairs; the station broad phase is a reach test and the hit part is the body
detoured and every part of a body is tested; separating conflicts are ignored; a pair is suppressed at site E only when
its detour is flyable (the body beyond the turn-in and the stopping distance) and the straight path clears the whole
body by C. Site E is claimed: at all three callers `&out` is unread on a false return and EFLAGS/ECX/EDX are dead after
the call. The sector snapshot reads the bucket table through `[sector+0x50]` (a pointer; heads at `+0xc·k`), only the
six obstacle buckets, and the station part walk ends on the 0-dword sentinel and applies the parent's `+0x70` scale
under `[child+0x12c] & 0x40` (open fact (g) settled by listing).

**Amendment 6 (2026-10-12, Run151 follow-up; the numbers in the ledger's Run151 section):** the servo horizon is
the measured interval since the ship's previous call, clamped to [one frame, 50 ms], once the ship's cadence is
established (8 consecutive gaps within 1.5 frames); a gated order keeps the 50 ms worst case. The handled orders call
the leaf every frame (the think gates gate the order step, not the leaf). The detour point sits at least 1 s of
flight ahead along the heading, the lateral offset unchanged, so a point abeam never asks for a near-right-angle aim.
`unresolved` is reported per call and per episode with a reason; the 5 % gate applies to episodes. A station child
whose subtree radius `+0xa4` is -1 (never queried by the engine's getter `0x00488170`) holds the station on the
fallback body until it is computed; a flagged child under a non-unit node scale falls back; the parts must reach
0.9 x the root radius. (Superseded the same day for the station part model: the walk replicates the engine's
subtree-radius getter 0x00488170 read-only, depth 3 and 64 nodes per station, a filled cache used as is; the flagged
child under a non-unit scale follows the engine's rule; the validation admits sqrt(3) x the root radius, the per-axis
metric of that getter.)

**Amendment 7 (2026-10-12, step 3: the gate approach; ship-navigation-ai.md section 10.7 P1 + P2 + P4 + P5, the
site facts of section 10.8; the ledger's Step 3 section has the fixture numbers):** behind the sub-key
`ship_steering_gates` (bool, default on, effective only with `ship_steering`; `--no-ship-steering-gates`), for
ships that are not capital per 0x0043fd50 (`kCapitalMask` 0x211d; capitals keep every vanilla step, their next
order 0x1f expects a stationary ship). P1 (no new site): order 4 with a class-0x12 target keeps the engine's point
A = G + R(0, -0.8, -1) r in f+0x240 (validated within 2 % of r, else the vanilla point) and is steered through the
ring-buffer redirect at the carrot on the lead-in line through A with direction h = sin 23.6 deg row1 + cos 23.6
deg row2 (0.75 s of flight, at least 0.3 r, ahead of the ship's projection, never beyond A). The lead-in applies
only in the window in front of the crossing plane, -3 r <= s <= 0 along h from the crossing point X = A - 0.75 r h:
a ship beside, above or behind the gate or already past the crossing (s > 0), or farther than 3 r before it, aims
at A as the engine does (the review's failure: a carrot beyond such a ship's own projection drove it away from A;
the disc lies at s >= 1.6 r, so no path from the window to a point at or before A crosses it); between 3 r and 2 r
before the crossing the carrot slides from A to the near carrot, so the switch is continuous. Envelope (fixture):
an approach that enters the window with its lateral offset up to 3 r crosses within 10 deg of the line (the
pursuit turns toward the line first); a ship that reaches the arrival sphere from s > 0 crosses as vanilla does
(the 1 m/s turn of order 5 follows). No arrival ramp; the
speed at site B is vmax (100 m/s for large hulls, the engine's order-4 cap) lowered by the cornering term and
written in both directions, replacing the engine's ramp to 5000 units/s at A (the one case where site B raises);
an avoidance emergency ramp still wins over it.
P2: the new 6-byte site 0x00454b91 skips `[obj+0x10] = 0` at the order-4 arrival for such a ship (`[obj+0x14] = 0`
kept). P4: the 6-byte site 0x00454700 keeps `[obj+0x10]` at the order-5 arrival and records it. P5: the 5-byte
site 0x0045526f launches order 8 with s = v0 t / 1000 + 1.665 t^2 (v0 kept within 5 s, else the engine's law).
The three sites are one group: all or none, claimed after site E, rolled back in reverse on the first failure,
restored first at shutdown; a refusal reports `ship_steering_gates_refused` and leaves steering and avoidance on.
Rows: `ship_steering_gates installed=`, `ship_steering_gate_site`, the `gate_*` summary fields, `ship_gate_approach`
(`--debug`: crossing speed, heading error to the pass point, crossing-to-warp time, v0).

**Amendment 8 (2026-10-12, step 4: gate crowding and the capital approach, engine side only; the Run152
follow-up folded in; the ledger's Step 4 section has the numbers):**
(1) *Approach spread.* A non-capital ship's lead-in line is shifted in the gate's x-y plane by a per-ship offset on
an 8-point ring of radius 0.25 r (the ring index from a multiplicative hash of the object pointer, stable for the
ship's life); the direction h and the window rule of amendment 7 are unchanged and f+0x240 (the engine's A, its
arrival test) is untouched. The shifted line still enters the 0.75 r completion sphere (margin 0.5 r); the worst
heading error to a pass point on the 0.55 r / +-45 deg arc is 13.9 deg geometrically at 0.25 r (12.1 at 0.20 r, 15.8 at
0.30 r, 17.8 at 0.35 r; `verification/results/ship-steering/spread_bound.py`), under the 18.2 deg creep threshold;
adjacent ring positions cross 211 m apart. (2) *Exit spread.* Site W, the `SA_WarpObjectIn` case 0x00465bac (6 B,
`mov ecx,[ebx+0x70]` | `mov edx,[ebx+0x50]`): before the case copies the ship's position into f+0x240 the stub
offsets `phys+0x30..` laterally by the ship's ring position at 0.3 r_gate (never along the axis) for a non-capital
ship warping in at a class-0x12 target; the case's own write (`f+0x240 + 1348650 x row 2`), the handler's per-frame
writes and its final snap all derive from that copy, so the whole warp-in carries the offset (ship_steering_sites.h
(l)). The site joins the gate group (all four or none). (3) *Capital alignment.* The lead-in (window rule, no
spread) extends to capitals toward their grid point A = G - 2 (r_gate + r_ship) F + 666 m (a R + b U) (validated
against that grid within 10 % of the depth and 20 m of the pitch), with direction F = row 2: `SA_RotateToAngles`
writes the gate's alpha/beta to f+0x240/+0x244 and the 0x1f handler compares the ship's forward with the forward row
of the matrix of those angles against 0xfd70 = cos 8.1 deg (0x004592d9, untouched), so the target heading is the
gate axis (ship_steering_sites.h (m)). A capital's stop at A (P2), its arrival ramp and its launch from rest stay;
site B writes the lower of the gate speed and the ramp in both directions for it, because the engine's own
alignment rule (1 m/s beyond 45.6 deg off A) would stall the lateral join. (4) *Order 5 to a gate (Run152).* The
engine's misaligned branch (500 units/s while > 18.2 deg off B, 0x004548a5) is replaced at site B by the cornering
speed: min(the engine's own ramp r/5 -> vmax/2, r -> vmax, omega d / (2 sin theta)) (the chord rule: the turning
circle passes through B), never below the floor, never above the aligned ramp, only when the chord arc and the
straight chord to B neither cross the gate plane inside the ring band (a crossing is accepted inside 0.55 r, the
pass arc, for the arc and 0.5 r for the chord: a pass point on the plane itself keeps the engine's turn in place)
nor require more than 100 deg of turn, and only when B validates as the pinned pass point of a gate whose frame is
orthonormal (subtypes 5..8 vanilla); otherwise the engine's value stands (`gate_dock_blocked`, `gate_dock_invalid`).
The approach verdict (`validated`) is per approach: recomputed on every order-4 call, reset on a target change or
when the ship leaves the gate's 2 r sphere; P2 and P4 keep the speed only on a validated approach. Site W's rule
(`core::gate_warp_in_offset`) requires the autopilot flag, no deleted/attached flag, a class not excluded by
`ship_steering_classes`, no order 0x21 in progress and an orthonormal gate frame. (5) *The crossing is recorded at P2*: the engine's order-4 arrival test
runs before the frame's leaf call, so a per-call distance test never sees d <= 0.75 r (Run152: 0 crossings over
59,836 lead-in calls); P2 marks the crossing from the last decision's heading error. (6) *Diagnostic* (`--debug`,
<= 8 per boundary): `ship_gate_transition` at the order 4 -> 5 transition with dist(A)/r at the last order-4 call,
the crossing flag, the heading error to B at the order-5 start, `[obj+0x10]`, `[obj+0x14]`, B and A in the gate
frame / r, the validation outcome and the ring verdict; `ship_gate_approach` gains the ring index and the offset;
the summary gains `gate_spread_offsets`, `gate_exit_offsets`, `gate_capital_aligned`, `gate_warp_calls`,
`gate_lead_validated/fallback/window_rejected`, `gate_dock_writes/blocked` and the P2/P4 vanilla-stop reasons
(`no_gate_target`, `capital`, `no_state`).

**Amendment 9 (2026-10-12, the gate hole; ship-navigation-ai.md section 10.9):** the order-5 cornering override
applies over the whole pass arc, B on or behind the gate plane included, with the same speed law (the chord rule,
floor 20 m/s capped by the engine's aligned ramp). The chord test against the ring's hole bound 0.60 r (the stock
ring hole's lower bound; stock 0.605-0.654 r, the installed Terran gates 0.663 r; the installed Mayhem gates are a
blended effect bubble with no collision tree, their hole unbounded; every pass point at 0.55 r lies inside) is a
diagnostic only and never lowers the speed: the engine's collision pair filter (0x0045c7b3..0x0045c7e3) skips the
ship-gate pair while the ship is on order 5 or 0x1e to that gate, so contact on that leg is impossible. Gate
subtypes 5..8 (the same bubble scene in the installed game; only the pass offset -0.05 r and the 0.14 r warp
trigger differ) get the same policy with their own pass vector in the validation. The order-4 lead-in and the exit
spread are unchanged: orders 4, 8 and 0x21 are not exempt from the pair filter, the bubble has no collision tree,
and the Terran ring at 0.663 r still bounds the 0.3 r exit offset.


Claim the three steering-leaf entries (`0x0044ccc0`, `0x0044c620`, `0x0044c970`, site A) with one
shared handler that, for class-7 ships under the autopilot flag and a point-steering order, computes a
pursuit point (optionally moved by the avoidance layer), a shortest-arc rotation and a curvature-limited,
smoothed rate command, and writes it as the three remaining angles so that the engine's own per-axis
controller `0x0043ad20` produces the rate profile (a "rate servo through the remaining angle", §2.4).
Add one per-object per-frame site at the object-loop continue `0x004593dd` (site B) that only lowers
`[obj+0x14]` to the arrival/cornering schedule, and, in step 2, the warn-test entry `0x0044dd90` to
suppress the reactive `CollisionWarn` round trip for pairs the predictive layer has resolved. No hook on
the dispatcher `0x0044d020`, the integrator or the controller. Everything is behind
`ship_steering = on|off` (`X3M_SHIP_STEERING`), avoidance behind `ship_avoidance`, with a per-class
mask; option off leaves every byte untouched.

## 1. Hook strategy (question 1)

### 1.1 Sites claimed

| Site | Address, window | Role | Why this one |
| --- | --- | --- | --- |
| **A1–A3** | `0x0044ccc0`, `0x0044c620`, `0x0044c970`; `83 ec 18 8b 02` (5 B, two instructions) each **[m]** | steering replacement + avoidance point adjustment | the only routines that derive the remaining angles from a target; 33 call sites in `0x00452ad0` plus 3 in the dispatcher, so every steering decision of every order passes exactly one of them **[m]**. Hooking the leaves, not the dispatcher, means the leaf choice (yaw+pitch vs bank-then-turn) is irrelevant: the handler writes all three angles itself. |
| **B** | `0x004593dd`; `8b 1b 83 3b 00` (`mov ebx,[ebx]; cmp [ebx],0`, 5 B) followed by `jne 0x00452b60`; 104 incoming jumps, all to the span start **[m]** | per-object, per-frame speed clamp | `[obj+0x14]` is written by the order case *after* its steering call (MovePos: leaf at `0x00456527`, speed map at `0x00456568` **[m]**), so a desired speed written at A is overwritten in the same tick. B sees `[obj+0x14]` final for the frame **[m]**. |
| **E** (step 2) | `0x0044dd90`; `55 8b ec 83 e4 f0` (6 B, three instructions), stdcall `(A, B, d, &out)`, `ret 0x10` **[m]** | suppress the reactive warn for resolved pairs | the native source of the `CollisionWarn` → `SA_StopObject` → order 0x12 churn; returning 0 here leaves the KC scripts untouched (stock and Mayhem) **[m]** recommendation of the navigation note §6. |

Not claimed: the dispatcher `0x0044d020` (covers only 15 of the 36 steering calls **[m]**), the integrator site
C `0x00459926` (per object per frame for every integrated object; not needed, §2.6) and the controller entry D
`0x0043ad20` (20 callers, six in the cockpit transition **[m]**; not needed, §2.4). Each `SiteSpec` carries the
exact bytes above; a mismatch fails closed per site and the module stays off if any of A1–A3 or B refuses
(`ship_steering_refused reason=bytes_mismatch_<site>`), because a partial claim would steer some orders and not
others. E is optional: avoidance runs without it, with the reactive loop as it is today.

### 1.2 What the handler reads

All on the engine's main-loop thread inside `0x00452ad0` (A, B) or the collision pass `0x0045d250` (E), the
thread that owns these objects; no object is freed on this path (the sweep is elsewhere), so the reads are
direct, as `engine_effects_patch` reads its routine's own argument **[m]** precedent. Validation is
structural: class word `[obj+0x48] == 7`, `f = [obj+0x50] != 0`, `p = [obj+0x70] != 0`, finite floats.

| Field | Use |
| --- | --- |
| `[obj+0x40]` bit `0x80000` autopilot, bit `0x40000` player control **[m]** | filter (§1.4) |
| `f+0x258` order word, `f+0x224` target object, `f+0x240..+0x248` point, `f+0x260` radius/distance **[m]** | order filter, arrival distance, escort/follow reference (§2.5, §4) |
| `p+0x30/+0x34/+0x38` position (integer units), `f+0x870..+0x89c` flight basis (row 2 forward) **[m]**, rows 0/1 right/up **[i]** | geometry |
| `[obj+0x10]` speed, `[obj+0x14]` desired speed, `[obj+0x24/+0x28/+0x2c]` current rates **[m]** | rate servo, schedule |
| `[obj+0xa4]` collision radius **[m]** | clearance, arrival |
| rate caps: record `+0x08/+0x0c/+0x10 × (1 + f+0x270)` (the getters `0x00450d60/cf0/dd0`); angular acceleration `cap × f+0x2b0 × record+0xd0/100`; max speed `0x00450980` (record `+0x44 × (1 + f+0x274)`, `f+0x27c` override, hull ramp); acceleration `0x00450b80` **[m]** | the ship's dynamics with tunings and damage |
| record `+0x14` `SG_SH_*` code; predicates large `0x0043fd00`, capital `0x0043fd50` **[m]** | class policy (§5) |

The handler calls the four engine getters (`0x00450980`, `0x00450b80`, `0x00450d60`, `0x00450cf0`,
`0x00450dd0`) rather than re-deriving the hull ramp and overrides **[p]**: they are leaf `__stdcall`/`__thiscall`
routines the command pass itself calls on this thread; their exact convention is read by the implement
agent from the call sites quoted in ship-movement.md §6 (`0x0045651b`, `0x0045655c`) and pinned by the site
verifier. Record pointer: `*0x00606fd4 + [obj+0x4a] × 0xdb8` **[m]**. Five getter calls per steering call
is the cost (§7.4); the fallback when a getter contract cannot be pinned is the record fields times the
tuning fractions, without the hull ramp (documented difference, not a failure).

### 1.3 What the handler writes

* **Site A: exactly `obj+0x30`, `obj+0x34`, `obj+0x38`** (signed binary angles), the leaf contract **[m]**.
  Every call writes all three (the original `0x0044ccc0` writes zeros inside the 100-unit dead band; the roll
  leaves return without writing **[m]**; we always write). The written value is never larger in magnitude
  than the geometric remaining angle on that axis (§2.4), which keeps the property the docking code tests
  afterwards: all three small when the ship faces the target (`0x00454760`–`0x00454790` **[m]**).
* **The target buffer.** Avoidance replaces `EDX` with a pointer to a DLL-global `int32[3]` (a 4-deep ring in
  the module's `.data`, one slot per nested call; the leaves are not recursive **[s]**). Never the caller's
  buffer (often a stack local **[m]**), never `f+0x240` (order state **[m]**), never the arena
  (`PAGE_EXECUTE_READ`, the 2026-10-11 native crash).
* **Site B: `[obj+0x14] = min([obj+0x14], v_cmd)`**, lowering only, floored at the engine's own 5000 units/s
  (10 m/s) so arrival is never prevented (§2.5). Never raises, never writes `[obj+0x10]`, never `f+0x278`
  (script-owned, `SA_SetSpeedLimit` has 26 KC call sites **[m]**).
* **Site E:** `EAX = 0`, `ret 0x10`, nothing else, for a resolved pair; otherwise falls through to the original.
* No write to any flight-block field, no flag bit, no script native (re-enters the VM **[m]**), no log call
  on the hot path (the formatter is x87).

### 1.4 Filter

Steering runs when all hold: class 7; `[obj+0x40] & 0x80000` (autopilot: the integrator only consumes the
remaining angles under that bit **[m]**; the player under manual control has `0x40000` without `0x80000` **[m]**,
so manual flight is never touched; the player's autopilot sets `0x80000` through `SA_SetAutopilot` **[m]**
and is steered like an AI ship); order in the **steer set** {0x19 MovePos, 0x23 MovePosContinue, 0x14 Escort,
0x15 Follow, 4 FlyBeforeContainer, 0x10/0x11/0x18 attack/capture, 0x22 DefensiveMove, 0x24 CollectDebris,
0x1d Idle, 6 TurnTo}; class code not in the per-class exclusion mask; dynamics sane (§6.2). Order 0x12
(native avoid) gets the smooth law but no avoidance adjustment; order 5 (dock approach) gets the law with the
level roll policy and no avoidance against its own container; orders 8, 0x21, 0x1e, 0x1b, 0x1f, 0x20 pass
through untouched (§6.1). Missiles (class 10) and class 0x13 also enter `0x0044ccc0` **[m]** and pass through.
Pass-through = the stub continues into the tail with `EDX` unchanged: the original leaf runs.

### 1.5 Register, flag and x87 contract; stub shape

**A1–A3 [m]:** at entry `EDX` = `const int32[3]*` target, `[esp+4]` = obj, `[esp]` = return; `EAX`/`ECX` dead
(overwritten at `0x0044ccc3/c5` before any read); flags dead; `EBX/EBP/ESI/EDI` callee-saved by the leaf;
`ret 4`. The x87 stack is the caller's (the leaf uses `fpatan/fsin/fcos` and `0x0052b5d0` internally and
leaves it as found); our code executes no x87 instruction, so only MXCSR and the last error are saved and
restored (the light envelope of `cpu_state.h`; the handler is added to `check_no_x87.py`'s `LIGHT_HOOKS` and
returns nothing by value in `st(0)`).

Stub (one per leaf, emitted with `engine_patch::Emitter` on the `media_cue.cpp` gate pattern, continuation
slot in the arena read only):

```
push eax; push ecx; push edx            ; edx = live input, eax/ecx dead but the handler clobbers them
sub esp,0x80; movups [esp+16i],xmm0..7   ; the game is x87 code, but keep the established envelope
push esp                                ; Frame* {xmm[8], edx, ecx, eax, ret, obj}
call handler_k                          ; cdecl, k = leaf index; returns 0 = pass through, 1 = handled
add esp,4; test eax,eax; jz pass
<restore xmm, esp, edx, ecx, eax>; ret 4          ; handled: obj+0x30/34/38 written, pop the arg as the leaf does
pass: <restore>; jmp [next_k]                     ; tail: 83 ec 18 8b 02 ; jmp leaf+5 (edx may now point at the ring slot)
```

Flags: dead at entry and the leaf writes them before any read, so no `pushfd`. The handler saves/restores
MXCSR and `GetLastError` itself (`LightCallBoundary` shape). Return address `[esp]` is used only for the
diagnostic histogram (which order case called).

**B [m]:** `EBX` = the object whose order step just ran; all GPRs live (ESP-relative locals, keep ESP exact);
flags dead before the replayed `cmp` (the tail re-executes `mov ebx,[ebx]; cmp [ebx],0` and jumps to
`0x004593e2`, the `jne`, with the `cmp`'s flags). Stub:

```
cmp word [ebx+0x48],7; jne tail         ; class filter in asm: every bullet and asteroid costs ~1 ns
test dword [ebx+0x40],0x80000; jz tail
pushfd; push eax; push ecx; push edx; <xmm save>; push ebx; call speed_clamp; add esp,4; <xmm restore>; pop edx; pop ecx; pop eax; popfd
tail: jmp [next_b]
```

(`pushfd` only because the asm filter itself writes the flags before the C++ call; the replayed `cmp`
re-establishes them anyway, so it may be dropped once the verifier proves no path reads the flags between
`0x004593dd` and the replayed `cmp`.)

**E [m]:** stdcall, `[esp+4]` = A, `[esp+8]` = B; `EAX` = result; the caller `0x0044e450` acts on it
(`0x0044e52b` sets the warn bit on true **[m]**). Stub: look up (A, B) in A's resolved set (DLL `.data`, §3.6);
hit → `xor eax,eax; ret 0x10`; miss → `jmp [next_e]`. Whether `&out` must be written on a false return and
whether EFLAGS are dead after the call are two facts the implement agent reads at `0x0044e4xx` before claiming
E (§10).

All counters, rings, the state table and the target ring are pinned DLL globals; the emitter's
counter-in-arena refusal (`dust_leak_fix.cpp`) is reused for every encoded memory operand of the three stubs.

## 2. The steering law (question 2)

Inputs per call (one ship, one call every `rand(50 × type[+0x50])` ms, i.e. 0–50 ms for all 521 rows **[m]**):
position `x`, basis `{right r̂, up û, forward f̂}` (`f+0x870` rows; row 2 = forward **[m]**, rows 0/1 **[i]**),
speed `v = [obj+0x10]`, rates `w = [obj+0x24..]`, caps `W_y, W_p, W_r`, angular accelerations
`a_y, a_p, a_r` (cap × k), linear `v_max`, `a`, radius `r_own`, the order's point `P` (after avoidance, §3) and
distance parameter `D = f+0x260`. Units: 500 units = 1 m **[m]**; angles binary (65536 = turn) **[m]**; the
core works in float32 metres and radians (SSE2) and converts at the boundary.

### 2.1 Pursuit point

`d⃗ = P − x`, `d = |d⃗|`. The aim point is `P` itself when `d ≤ L_p`, else the point at distance `L_p` along
`d⃗`: `L_p = max(2R, v·0.5 s)` with `R = v/ω`, `ω = min(W_y, W_p)` **[p]** (the game-scale §10 turn
radius). With a single point this equals aiming at `P` (same direction); the pursuit distance matters only
when the planner supplies a polyline (§9), where the aim point slides along the path. The 100-unit dead band
is replaced by the arrival rule: inside `d_arr` (§2.5) the heading command is held (angles written 0).

### 2.2 Shortest-arc rotation into body axes

`t̂ = d⃗/d`; bearing error `θ = acos(clamp(f̂·t̂))`; rotation axis `n̂ = f̂ × t̂ / |f̂ × t̂|` (for `θ < 0.5°`
or `θ > 179.5°` use `n̂ = û`, the yaw axis). Body components `n_y = n̂·û`, `n_p = n̂·r̂` (yaw about up, pitch
about right **[i]** axis naming; the first flight's `ship_steering_decision` row settles the sign
convention against the next frame's `obj+0x18..+0x20` delta, ship-movement.md §12). Per-axis remaining
angles `θ_y = θ·n_y`, `θ_p = θ·n_p` (the great-circle turn executes both axes in proportion, instead of
yaw-then-pitch **[m]** sequencing of `0x0044ccc0`).

### 2.3 Rate command

Scalar rate along `n̂`:

```
ω_cmd = min( W_n,  sqrt(a_n · θ),  θ / τ )          τ = 0.3 s  [p]
W_n   = min over axes with n_axis ≠ 0 of W_axis / |n_axis|      (the cap of the slowest axis in this plane)
a_n   = the same for a_axis
```

* `sqrt(a_n·θ)` is the engine controller's own braking boundary (it brakes when `e < w²/a` **[m]**), so the
  command never asks for a rate the controller would immediately brake from; it is the time-optimal profile
  with the record's angular acceleration, non-overshooting.
* `θ/τ` adds the linear tail: below `θ_lin = a_n·τ²` (0.1–6.6° for the five ships, design_numbers.txt) the
  approach is exponential with time constant τ instead of the square-root's infinite gain at zero. This is the
  critically-damped approach the brief asks for; no bang-bang and no dead band.
* `W_n` is the curvature limit in this engine: the ship moves along its nose at `v` with only the decaying slip
  term **[m]** (no lateral dynamics), so any rate up to the cap is "holdable"; the path's curvature is
  `ω/v`. What must not happen is commanding a turn the *point geometry* cannot absorb, which is a speed
  constraint (§2.5 cornering), not a rate constraint.

Per axis: `ω_y = ω_cmd·n_y`, `ω_p = ω_cmd·n_p`, roll from §2.6.

### 2.4 Writing the command as remaining angles (shape, not bypass)

The controller `0x0043ad20(e, w, W, a, …)` accelerates at `a` when `e ≥ w²/a` and brakes at `a` when
`e < w²/a` **[s]**. Writing

```
e_axis = sign(ω_axis) · min( |θ_axis|,  ω_axis²/a_axis + |ω_axis|·T_tick )        T_tick = 0.05 s
```

makes it a rate servo: with `w < |ω_axis|` the written `e` exceeds `w²/a` and the controller accelerates;
with `w > |ω_axis|` it brakes; between calls the integrator subtracts the turned angle each frame, and the
`|ω|·T_tick` term keeps `e` above the braking threshold for one worst-case think interval so the rate does not
sag before the next call. The rate error is bounded by `a_axis × 50 ms`: 3.7°/s for the Discoverer
(2.5 % of its cap), 0.07°/s for the Boreas (0.5 %) (design_numbers.txt, k assumed, §5). In the braking phase
`ω = sqrt(a·θ)` gives `e = θ`: the written angle *is* the geometric remainder, exactly the original law, so
the final approach is the engine's own profile. The cap `|e| ≤ |θ_axis|` means the controller's own
`delta = min(delta, e)` clamp still guarantees no overshoot past the target.

Why shape rather than bypass: the controller is time-optimal, non-overshooting and already uses the record's
`+0xd0` angular acceleration, `f+0x2b0` and the tuning bonus **[m]**; bypassing it needs site D (20 callers,
six in the cockpit transition **[m]**) or per-frame writes to `obj+0x24..+0x2c` under autopilot semantics it
does not have, and it shares the controller with the attitude-offset path `f+0x8a0..` and the player's
auto-level **[s]**. The only thing shaping cannot do is cruise *exactly* at a rate below the cap, and the
bound above says that error is a few per cent of the cap for 50 ms.

### 2.5 Speed: arrival schedule and cornering (site B)

```
v_arr    = v_ref + sqrt( a · max(0, d − d_arr) )          d_arr = the order's own arrival distance (below)
v_corner = ω · d / (2 sin θ)        for sin θ > 0.05, else ∞   (turning circle R = v/ω must pass through P: d ≥ 2R sin θ)
v_cmd    = max( 5000 units/s,  min(v_arr, v_corner) )
[obj+0x14] = min([obj+0x14], v_cmd)                         at site B, every frame
```

* `sqrt(a·(d − d_arr))` is the ramp the engine already uses (`v²/a`, "twice the physical stopping distance"
  **[m]**): it demands a deceleration of `a/2` along the path, half of what the slew delivers, so the ship
  follows it. It reaches the floor at `d_arr`, where the order case itself declares arrival.
* `d_arr` is the engine's: `f+0x260` for 0x19/0x23/0x15; `clamp(k·r_own, 22200, 111000)` for order 4 (k = 3,
  or 1 for "large"; 111000 for a ship target; ¾ r for a gate) **[m]**; the slot for escort (`v_ref` = the
  leader's speed projected on our heading, so the follower closes at a relative ramp instead of braking toward
  a moving slot, §4); 0 for attack orders (never slow for a ring point; the cornering term still applies).
* `v_corner` is the curvature-limited cornering: with `θ = 90°` at 1 km the Discoverer is limited to
  1289 m/s (no limit), the Mercury to 184 m/s (no limit at its 124 m/s), the Boreas to 118 m/s (none);
  it bites on fighters approaching a slot or a point from the side at short range (an M5 at 100 m, 90°:
  129 m/s instead of 532) and so removes the orbiting the game-scale note predicts **[i]**.
* Lowering only, so the case's own 0 at arrival, the docking speed caps, the gate ramp and the escort speed
  match are never exceeded; the 5000 floor equals the engine's MovePos `y0` **[m]**, so the ship always
  crosses the arrival boundary.

Open: whether the slew block (`0x004594ef..`) runs before or after `0x004593dd` for the object. Either way
the clamp written at B is consumed by the next slew at the latest (≤ 1 frame), which the schedule tolerates
(§10).

### 2.6 Roll policy

* **Bank** (classes not in large `0x0043fd00`: M3, M4, M5, TS, TP, GO): `φ_cmd = −φ_max · ω_y / W_y`
  with `φ_max = 35°` **[p]**, rate-limited to `W_r`, written through the same servo on `obj+0x38`. Since the
  heading law re-solves in the current (banked) basis every call, banking is cosmetic and does not divert the
  turn. The engine's own roll leaves bank up to 90° and only then turn **[m]**; ours banks while turning.
* **Level** (large set): `φ_cmd = −atan2(r̂_y, û_y)` (roll that brings the right axis horizontal) **[i]**
  axis naming, rate-limited, so capitals, TS and M6 fly level and settle level.
* **Dock and gate legs** (orders 4 and 5, any class): roll command 0 outside the final zone; inside the
  docking code's own roll match (after the three angles fall below `0x51e` **[m]**) is untouched because we
  write `|e| ≤ |θ|` and the engine's test runs on the values we wrote. Order 4 therefore hands over a level,
  aligned ship. Roll alignment to the port's roll *before* the hand-over needs the port's roll from
  `f+0x254` port data, which was not decoded; it stays with the engine (§10).

### 2.7 Cadence: per call is enough

The leaves run every 0–50 ms **[m]**; the integrator runs the controller every frame on the `e` we wrote, so
the rotation between calls is the controller's own smooth trapezoid, not a hold. The only quantities that
are quantised at 50 ms are the pursuit direction (a few metres of travel) and the rate command (error
≤ `a × 50 ms`, §2.4). Docking (no think gate **[m]**) and the escort close-in branch call every frame
anyway. Per-frame access is needed only for the speed clamp, because the case rewrites `[obj+0x14]`; that is
site B, which is cheap (asm class filter). Site C (integrator) is not needed; it would add a per-object
per-frame C++ call and see the angles after the pass, where nothing of ours needs changing.

## 3. Predictive avoidance (question 3)

### 3.1 Where it sits

In the site-A handler, before §2: it takes the order's point `P` and returns the point `P'` that the
steering uses, in the ring buffer (§1.3). The order's own distance tests keep running on `f+0x240`, so arrival,
docking stages and slot keeping are unchanged **[m]** (ship-movement.md §11). Avoidance is skipped for orders
5, 0x12, 8, 0x21, 0x1e, 0x1b and for the own container in order 4 inside `2·r_container` (the engine detours
natively there **[m]**).

### 3.2 Obstacle set and budget

One **sector snapshot per frame**, built lazily by the first steering call of a frame in that sector (epoch =
game time `[*0x00606f34+0x718]` **[m]**; sector `[obj+0x54]`): walk the 32 bucket lists at `[sector+0x50]`
(stride `0xc`, next at `[o+0]`, sentinel first dword 0 **[m]**) for the obstacle classes — ships (7), stations
(5, 6), gates (0x12) and asteroids (class word to be read from the bucket census on the first flight, §10) —
into a flat SoA array: position (float m), radius, velocity (`p+0x100..` **[m]**), class, flags, pointer;
capacity 1024 per sector (beyond it: the nearest are not guaranteed; a `snapshot_overflow` counter, and the
reactive loop stays). Cost: ~20 ns per object, 420 objects (200 ships, 23 stations, 194 asteroids, 4 gates:
stock station and Mayhem asteroid medians **[m]**) = 8 µs per sector per frame **[i]**.

Per steering call: an SSE2 pass over the snapshot with the pre-filter `|Δ|_∞ ≤ L + r_obs + r_own` per axis,
then time-to-closest-approach on the survivors; keep the **8 candidates** with the smallest `t_cpa` inside the
horizon. Cost: 420 × 1.5 ns = 0.63 µs per call; 200 ships × 40 calls/s = 5 ms/s = 0.084 ms per frame at
60 fps **[i]** (design_numbers.txt). Excluded: self, the own leader and wingmates (§4), the order's target
object `f+0x224` for orders 4/5/0x14/0x15/0x10 (we are flying *to* it), docked/attached objects (`0x200`,
`[obj+0xf8]` **[m]**), objects flagged `[obj+0x44] & 0x20000` (out of the pair loop by the engine's own rule
**[m]**).

### 3.3 Geometry

Relative motion: `Δx = x_obs − x`, `Δv = v_obs − v·f̂`; `t_cpa = clamp(−Δx·Δv / |Δv|², 0, T)`,
`T = L/v`; miss distance `m = |Δx + Δv·t_cpa|`. A conflict exists when `m < C` with

```
C = r_own + r_eff + max(25 m, 0.5·r_own)                                   (game-scale §10 [p])
L = clamp( 0.5 s·v + 1.25·min( sqrt(C(C + 2R)), v²/2a ),  C + r_own,  30 km )
```

both per ship from the live getters (so tunings and hull damage are included) and per obstacle (`C` depends
on `r_eff`). This is the swept sphere of radius `C` along the predicted straight path of length `L`; the
`sqrt(C(C+2R))` term is the distance at which a constant-rate turn of radius `R` clears a sphere of radius
`C` **[i]** (game-scale §10). Values for the five ships: §5.

### 3.4 Steering response: tangent detour

For the earliest conflict: `ê = Δx_cpa/|Δx_cpa|` is the direction from the obstacle centre to the closest
approach point. Detour point `P' = x_obs + ê'·C·1.1` where `ê'` is `ê` rotated into the **plane of least
turn**: the plane containing `f̂` and the obstacle direction, choosing the side (`±`) that minimises the
turn from `f̂`; for capitals (`0x0043fd50`) the vertical component of `ê'` is preferred (pass over/under, where
other traffic is sparse and the slow yaw of a capital does not have to fight the lateral geometry) **[p]**.
If `P'` lies behind the ship relative to `P` or the detour would re-enter another candidate's sphere, the
next candidate side is tried; after 4 tries the call keeps `P` and reports `unresolved`, which also leaves
the reactive warn unsuppressed (§3.6). Hysteresis: the chosen side per (ship, obstacle) is stored in the
ship's state and reused until `Δx·f̂ < 0` (the obstacle is behind), so the detour side never flips between
calls.

Speed: a conflict within `D_stop = v²/2a` of the braking distance where no detour exists (the obstacle is
larger than the turn geometry allows, `d < sqrt(C(C+2R))`) lowers `v_cmd` at site B to
`sqrt(a·(d − C))` (emergency ramp), the one case where avoidance brakes instead of turns; fighters never
reach it (D_turn ≪ D_stop **[m]** game-scale §5), capitals do.

### 3.5 Stations: the sphere is not the shape

The §2 sphere of a station is 1.8–52 km **[m]** and includes far-flung parts; using it as `r_eff` would make
every ship detour kilometres around a dock. Decision in two layers **[p]**:

1. **Broad phase** uses the sphere to *select* a station as a candidate (never to steer).
2. **Narrow phase: per-part oriented boxes.** For a candidate station, the proxy builds once per station
   (cached by node address and lifetime generation, flushed on load) the set of its hull parts' boxes from the
   fields the engine computes at load: part centre `part+0x40..` and half-extent `part+0x50..` (4 × int16
   units, `/65536` = object space, [render-node-bounds.md](../reverse-engineering/render-node-bounds.md) §2
   **[m]**), transformed by the node's world matrix (`node+0x70` scale, basis rows **[m]**). The swept sphere
   is tested against each box (≤ 64 parts; a station with more keeps the largest 64 by extent); `r_eff` for
   the detour is then the distance from the station centre to the hit box's far face along `ê`, so the detour
   hugs the real shape. Cost ≤ 64 × 30 ns = 2 µs per (ship, station) pair, only for stations that pass the
   broad phase (rare per call) **[i]**.
3. **Fallback** when the part walk is unavailable (node not resolved, > 64 parts, unreadable): `r_eff =
   0.35 × r_sphere` floored at 500 m **[p]**, and the pair is *not* suppressed at E, so the engine's reactive
   warn and its mesh test `0x0044d9c0` **[m]** remain the backstop.

The engine's own mesh warn test `0x0044d9c0(B.phys, |vA|+b, out, 1.2, 0)` was considered for the narrow
phase and loses: its internals and cost are not established **[m]** (navigation note §7), the collision
narrow phase in the same family costs 230,000 BVH visits per station query **[m]** (sector-collide.md §12),
and calling it from the command pass is a reentrancy question nobody has answered. It stays what it is: the
reactive fallback.

What the part walk needs that is not yet established: how to reach the station's render-node root from the
sector object (`[obj+0x70]` is the physics block with the orientation at `+0x40`, and `0x00488170(root)` fills
`[obj+0xa4]` from a node tree **[m]**; whether that root is `[obj+0x70]` itself or a field of it is **[i]**),
and the part list layout under the node, which `engine_nozzle_walk_core.h` already walks for ships. This is
the one disassembly step of step 2 (§10).

### 3.6 Interaction with the reactive loop

* **Suppression (site E):** after a call that resolved every conflict (a detour exists and the predicted path
  clears every candidate by ≥ `C`), the ship's state stores the resolved obstacle pointers (≤ 8) with the
  call's game time. E returns 0 for (A, B) when B is in A's resolved set and the stamp is younger than
  100 ms (two think intervals); otherwise the original warn test runs. Unresolved pairs, pairs inside
  `D_stop`, station pairs without a part model, and any ship not under our steering are never suppressed: the
  existing loop (`CollisionWarn` → `SA_StopObject` → order 0x12) stays the fallback exactly as today.
* **No oscillation between the two systems:** (1) while order 0x12 runs, the avoidance layer does not move
  the point (the engine owns the evasion), and the smooth law steers the engine's escape point; (2) the
  detour side hysteresis (§3.4) prevents side flips; (3) suppression is per pair and per call, so a conflict
  our layer loses (it reports `unresolved`) is handed to the engine within one collision pass (next frame),
  and the engine's avoid order is not interrupted by us; (4) the resolved set expires in 100 ms, so a stale
  decision cannot mask a new conflict for longer than the engine's own 200 ms avoid cadence **[m]**.
* Mayhem's `Bounce` thread (polls `SA_GetCollisionObjects` every 250–5000 ms **[m]**) reads the obstacle list
  the collision pass fills *after* E; a suppressed pair is simply not listed, which is the intended effect.

## 4. Formations and groups (question 4)

* **The leader is never an obstacle.** For orders 0x14/0x15 the target `f+0x224` is excluded; so is every ship
  whose `f+0x224` equals ours with order 0x14/0x15 (a wingmate), while within the **formation envelope**
  `10 × f+0x260` of the leader (the engine's own catch-up threshold **[m]**). Outside the envelope (a
  straggler rejoining) wingmates are ordinary obstacles.
* **Slot keeping.** The slot is recomputed each tick in the leader's yaw/pitch frame **[m]**; our law aims at
  it with `v_ref` = the leader's speed along our heading (§2.5), so a follower approaches at a relative ramp
  and the cornering clamp removes the full-speed overshoot of the catch-up branch. The engine's close-in
  branch (aligned within 25.8°: aims 20·r ahead, matches speed **[m]**) is kept; our law sees its point like
  any other.
* **Lookahead of a follower** uses its own velocity but a horizon no shorter than the leader's (`L_follower =
  max(L_own, L_leader)` read from the leader's state when present), so the follower sees what the leader
  sees before the slot drags it into it.
* **Crossing an obstacle: the formation detours on the leader's adjusted path, followers break and re-form
  per ship.** When the leader's point is detoured, its heading changes and every slot rotates with it
  **[m]**, so the group bends as one; a follower whose own swept test still hits (outer slot) detours its slot
  point by its own `C`, which temporarily breaks its slot, and re-forms as its conflict ends. No group-level
  coordination is needed because slot geometry is the leader's frame, not a fixed world offset.
* **Capital leader with fighter escorts.** The capital's lookahead is size-driven (Boreas 2.5–3.3 km, §5)
  and its detours are gentle (13.5°/s); escorts at 4–30× its turn rate follow a slot that moves slowly, so
  they stay coherent by the speed match. Nothing of ours raises a follower's speed above the script's
  formation limit (`GetMaxFormationSpeed` → `f+0x278` **[m]**, applied in the slew after our clamp).

## 5. Ship size classes (question 5)

All parameters are formulas of the live `v_max, ω, a, r_own` (§2, §3); no per-class table. Class only
selects the roll policy and the capital preference in the detour plane. Predicates: **large**
`0x0043fd00` = {TL, M0, M1, M2, TL_P, M6, M7, TM, M8} → level roll; **capital** `0x0043fd50` = {TL, M0, M1,
M2, TL_P, M7} → prefer over/under detours **[m]** predicates, **[p]** use. Values for the five representative
installed ships (inputs **[m]** from `recommend.txt`; `k` assumed per the sample rows of ship-movement.md §4,
to be replaced by the record's `+0xd0` on the first flight; all outputs **[i]**, design_numbers.txt):

| Ship | ω °/s | a_ang °/s² (k) | rate error at 50 ms | θ_lin ° | R m | d_sched = v²/a m | L vs same class m | L vs 1.5 km station m | L vs 60 m asteroid m | pursuit/arrival floor max(2r, R/4) m | roll |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M5 Discoverer | 147.7 | 73.8 (0.5) | 3.7°/s (2.5 %) | 6.6 | 206 | 5146 | 442 | 2426 | 539 | 52 | bank |
| M3 Nova | 92.8 | 46.4 (0.5) | 2.3°/s (2.5 %) | 4.2 | 153 | 1531 | 288 | 1549 | 371 | 38 | bank |
| M6 Centaur | 33.8 | 10.1 (0.3) | 0.5°/s (1.5 %) | 0.9 | 306 | 1620 | 531 | 1661 | 523 | 129 | level |
| TS Mercury | 21.1 | 6.3 (0.3) | 0.3°/s (1.5 %) | 0.6 | 337 | 513 | 382 | 1602 | 382 | 84 | level |
| M2 Boreas | 13.5 | 1.4 (0.1) | 0.07°/s (0.5 %) | 0.1 | 297 | 327 | 2469 | 3264 | 1824 | 1411 | level |

Reading: fighters have `d_sched` (the distance at which the arrival ramp starts from full speed) far longer
than their turn radius, so the schedule, not the turn, shapes their arrival; capitals are wider than their
turning circle (Boreas r = 705 m, R = 297 m **[m]**), so `C + r_own` dominates `L` and the detour is set by
geometry. The arrival floor is only the pursuit/cornering reference; the arrival *decision* stays the
engine's `f+0x260`.

## 6. Hand-overs and safety (question 6)

### 6.1 Left to the engine

| What | Rule |
| --- | --- |
| Docking final zone, `d ≤ 2·[obj+0xa4] + 25000` **[m]** | the leaves are not called there; nothing of ours runs. Order 5 outside it: smooth law, level roll, no avoidance against the container. |
| Orders 8 (warp away), 0x21 (warp in), 0x1e (undock), 0x1b (player land), 0x1f (rotate to angles), 0x20 (glide) | pass through at A (original leaf), no clamp at B. |
| Gate plane: order 5 with a gate target (`[gate+0x40] \|= 0x2000000`, arrival `\|v\|/10 + r` **[m]**) | smooth law only; the gate is excluded from the obstacle set as the order's target; speed never lowered below the engine's gate ramp (lowering-only clamp with the 5000 floor cannot stall the crossing, since the engine's own ramp is at least that). |
| Order 0x12 (native avoid) | steering yes (the escape point), avoidance no, suppression no. |
| Order 4 inside `2·r_container` | the engine's tangent detour **[m]**; our avoidance excludes the container there. |
| Player under manual control (`0x40000` without `0x80000`) | never touched; `CollisionWarn`'s player branch is unchanged. |

### 6.2 Degradation on odd records

* Any axis with cap `W = 0` (the controller then turns nothing **[s]**): that axis is dropped from `n̂`
  (`W_n` over the remaining axes); both yaw and pitch zero → pass through (the ship cannot steer anyway).
* Angular acceleration 0 (record `+0xd0 = 0`, 12 rows **[m]**; the integrator skips the axis): pass through
  with reason `no_angular_accel`, counted.
* `v_max = 0` or `a = 0` from the getters: no speed clamp (B does nothing), steering still runs.
* Extreme values (a 720°/s M5, `k` ≫ 1 from a mod): every command is clamped to the record's caps and the
  written `|e| ≤ |θ|`; the controller's own `delta ≤ e` makes overshoot impossible regardless.
* Non-finite intermediate, unreadable field, class/flight mismatch with the ship's stored state: pass through
  this call, reset the state, count the reason. Pass-through is always the original leaf, never a frozen ship.

### 6.3 Kill switches and diagnostics

* `ship_steering = on|off` (default **off** for the first release, on once Run A accepts it; `X3M_SHIP_STEERING`);
  `ship_avoidance = on|off` (requires steering; default off in step 1); `ship_steering_classes` = list of
  `SG_SH_*` names to exclude (e.g. `M1,M2,TL`), parsed once at load into a 16-bit mask over the record
  `+0x14` code. Off = no claim, bytes untouched; refused bytes = module off with one `always`-tier row.
* Diagnostic rows under the existing tiers (`log_tiers.h`), emitted from the frame boundary out of a ring,
  never from the stubs: `--perf`: `ship_steering_summary` at 1 Hz (calls, ships, pass-through counts by
  reason, handled by order, avoidance conflicts/detours/unresolved, E suppressions, snapshot size, handler
  µs max/p99); `--debug`: `ship_steering_decision` (one sampled ship per second: obj, order, class, θ, n̂,
  ω_cmd, e written, w, v, v_cmd, P, P′) and `ship_avoid_detour` (each new detour: obj, obstacle class and
  pointer, `t_cpa`, `m`, `C`, side), capped at 32 rows per second; `always`: the claim rows and a
  `ship_steering_stuck` witness (first 8 per session: a steered ship whose distance to its MovePos point did
  not decrease for 30 s).

## 7. Verification plan (question 7)

### 7.1 Host fixture for the portable core

`verification/probe/ship_steering_host.cpp`, built and run by `verification/analysis/test_ship_steering.py`
(the `test_chase_transition.py` pattern: `c++ -std=c++17 -O2 -Wall -Wextra -Werror`, assert `checks PASS`),
discovered by the host suite. It includes `ship_steering_core.h` (and `ship_avoidance_core.h` in step 2)
and a **kinematic model of the engine**: the per-axis controller in the integer 16.16 arithmetic of
`axis_controller_sim.py` (ported; its twelve settle times in `axis_controller_sim.txt` are the model's
regression oracle), the per-frame rotation and position update (`speed × forward × dt/1000`, slew at
`a × dt/1000` with the 1-unit minimum **[m]**, the slip term optional), the think-tick sampling
`rand(0..50 ms)` and, for the arrival scenario, MovePos's own ramp and `f+0x260` test. Scenarios on the
five ships of §5, at 30, 60 and 144 fps and three tick-sequence seeds each:

| Scenario | Assertion |
| --- | --- |
| 180° turn to a point 20 km away | heading overshoot ≤ 1°; the rate never changes sign after its first zero crossing; settle (< 0.5°) ≤ 1.15 × the model's single-axis time for the same angle (the engine's own profile is the bound); both axes execute proportionally when `n̂` is diagonal (each engaged while the bearing is large, no vanilla-style axis sequencing; finish times may differ because the roll policy re-levels late in the turn, amendment 4) |
| arrival at a MovePos point with `f+0x260 = 2·r_own` from full speed, approach from 45° | the ship enters the radius exactly once, speed at entry ≤ 20 m/s above the floor, no pass beyond `P` by more than `f+0x260`, at most one heading sign reversal in the last 5 s (no orbiting) |
| station flyby (1.5 km sphere whose centre is 600 m off the straight line; narrow phase as a 1.5 × 0.4 × 0.4 km box in step 2) | minimum clearance ≥ `C`; detour side constant; the path length ≤ 1.3 × the straight line; the ship returns to the line within `L` after passing |
| crossing traffic (one ship of the same class at 90°, collision course, `t_cpa = L/v`) | both detour, min distance ≥ `C`, no side flip |
| formation slot (leader on a 5 km circle at full speed, follower in a 4·r slot) | slot error RMS over the last 20 s ≤ 2·r_follow; error monotone non-increasing after the first entry into 4·r_follow |
| degraded record (`W_p = 0`; `a_ang = 0`; `v_max = 0`) | the documented pass-through / axis-drop behaviour, no NaN |

### 7.2 Site verifier

`verification/probe/verify_ship_steering_sites.py` (the `verify_chase_restore_sites.py` shape, objdump of
the containing functions): the three leaf prologues and the `ret 4`s, `0x004593dd`'s span and the 104
jumps to its exact start **[m]**, `0x0044dd90`'s prologue and `ret 0x10`, whole-instruction boundaries, no
direct branch into a span interior, no data dword into any window, the `SiteSpec` table in
`ship_steering_sites.h` equal to the independently derived bytes, and the getter call sites' conventions
(§1.2). `test_ship_steering_sites.py` runs it against `/mnt/c/X3/X3AP.exe` (`RESULT PASS`).
`check_no_x87.py` gets the three handlers in `LIGHT_HOOKS`; the audit must stay at 0.

### 7.3 Flight runs that decide acceptance

Native Windows, the user's flights, same save twice (`ship_steering=off` baseline, then on; step 2 adds
`ship_avoidance=on`), a busy sector with a station and traffic, `--perf`:

1. `ship_steering_summary`: pass-through reasons other than `order_not_steered`/`class_excluded` = 0 for
   class-7 ships; handler p99 ≤ 5 µs, max ≤ 50 µs; calls per ship per second 20–60 (confirms the cadence).
2. `ship_steering_stuck` = 0 rows over the flight.
3. Step 2: `CollisionWarn` dispatches per minute (the existing collide census row, or the summary's E counter
   vs its miss counter) at least 50 % below the baseline in the same sector; `unresolved` ≤ 5 % of conflicts.
4. The user's report: turns without the yaw-then-pitch kink, no orbiting at MovePos arrival, formations
   holding slots, no ship scraping a station; docking and gate transit as before (a dock and a gate jump by
   an AI ship observed).
5. One `ship_steering_decision` sample per ship class confirms the axis sign convention (§2.2) and `k`.

### 7.4 Cost

Per steering call (inferred; to be measured by the summary's QPC fields): five getter calls (~20 ns each),
the geometry (one normalisation, one `acos`, two `sqrt`, one `atan2` for the roll; SSE2 libm) ≈ 150–300 ns;
avoidance ≈ 0.6 µs scan + ≤ 8 × 50 ns candidates (+ ≤ 2 µs per station pair with a part model); state
lookup ≈ 20 ns; the envelope (MXCSR, last error) ≈ 20 ns. **≈ 0.4 µs (step 1) / ≈ 1.5 µs (step 2) per
call**, 40 calls/s per ship: **200 ships ≈ 3 ms/s (step 1) or 12 ms/s (step 2) = 0.05 / 0.2 ms per frame at
60 fps**, 0.3 / 1.2 % of a 16.7 ms frame **[i]**. Site B: 1 ns per non-ship object plus ≈ 50 ns per steered
ship per frame = ≈ 12 µs per frame with 2000 objects. Site E: ≈ 10 ns per pair reaching the warn test.
Snapshot: 8 µs per active sector per frame. No allocation, no lock (single engine thread; the frame boundary
reads the ring with the existing acquire/release pattern).

## 8. Implementation plan (question 8)

**Step 1: steering, behind `ship_steering`.**

| File | Content |
| --- | --- |
| `src/proxy/ship_steering_core.h` (new, portable, no Windows) | the law (§2.1–2.6), the rate servo, the schedule, the roll policy, the ship state table (1024 entries, open addressing on obj; 64 B each), the option/mask parser, pass-through reasons, the summary counters |
| `src/proxy/ship_steering_sites.h` (new) | `SiteSpec` for A1–A3 and B with the exact bytes, the register contracts of §1.5 as comments and `static_assert`s on the byte strings, the getter addresses, the field offsets |
| `src/proxy/ship_steering.cpp` / `.h` (new) | option read, claims (install window), the stub emitter (A and B shapes), the handlers (light envelope), the per-frame ring → log rows, restore at detach; `initialize`/`frame_boundary` called where `chase_transition` is |
| `tools/config/schema.py`, `assets/x3m.ini` | `ship_steering` (bool, default 0), `ship_steering_classes` (string, default empty) in a `ships` group; launcher flags `--ship-steering`, `--ship-steering-classes` in `tools/manage.py` with `test_launcher_defaults.py`/`test_config_schema.py` updated |
| `verification/probe/ship_steering_host.cpp`, `verification/analysis/test_ship_steering.py` | §7.1 model and scenarios (steering rows) |
| `verification/probe/verify_ship_steering_sites.py`, `verification/analysis/test_ship_steering_sites.py` | §7.2 |
| `verification/probe/check_no_x87.py` | the three handlers in `LIGHT_HOOKS` |
| docs | this note (amendments), `docs/verification/ship-steering.md` ledger (new), `docs/status.md` one line |

**Step 2: avoidance, behind `ship_avoidance`.** `src/proxy/ship_avoidance_core.h` (snapshot, pre-filter,
CPA, detour, hysteresis, the station part-box model and its fallback), site E added to
`ship_steering_sites.h` and the suppression stub to `ship_steering.cpp`, the asteroid class word and the node
root from the first flight's census, `ship_avoidance` in the schema/ini/launcher, the flyby/crossing
scenarios in the host fixture, the verifier extended to E and the warn-test caller's EFLAGS/`&out` facts.

Acceptance (every step):

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_ship_steering verification.analysis.test_ship_steering_sites verification.analysis.test_config_schema verification.analysis.test_launcher_defaults
python3 verification/probe/verify_ship_steering_sites.py --exe /mnt/c/X3/X3AP.exe     # RESULT PASS
python3 verification/probe/check_no_x87.py build/d3d9.dll                              # violations 0
python3 tools/manage.py launch --dry-run --ship-steering                               # the option reaches X3M_SHIP_STEERING=1
```

then the flight runs of §7.3 recorded in the ledger.

## 9. Interface for a path planner

The steering layer takes a **polyline, not a point**: `const int32 (*points)[3], unsigned count`, the last
point always the order's own `f+0x240` so the order's arrival test is unchanged. A planner registers, per
ship and per re-plan, up to 16 points through `ship_steering::route_set(obj, points, count, generation)`
into the ship's state (the slot is cleared when the order word, the order's point or the target object
changes, so a stale route never survives a new order). At site A the pursuit point (§2.1) is taken at
distance `L_p` along the polyline from the ship's nearest point on it, intermediate points are passed when
within `max(2·r_own, R)`; avoidance adjusts the pursuit point, never the list; the speed schedule uses the
remaining path length to the last point, not the straight distance. With no route the list is `[f+0x240]`
and everything above reduces to the single-point law.

## Native Windows behaviour

No new API: `engine_patch` (VirtualProtect, `lock cmpxchg8b` within an aligned qword; A1–A3 and B are
5-byte spans whose alignment the verifier reports, E's 6 bytes may straddle and then rely on the install
window as the other 6-byte claims do), the existing emitter and writer thread. The stubs write only DLL
globals (the target ring, counters, the state table): nothing into the arena, the lesson of 2026-10-11. The
user plays natively, so every flight row above is native evidence; Wine is covered by the same bytes and
has no separate fixture on this host (`platform-portability.md`).

## Alternatives considered

| Alternative | Why it loses |
| --- | --- |
| Hook the dispatcher `0x0044d020` only | misses MovePos with a yaw rate, docking, undocking and the escort close-in branch (21 of 36 steering calls) **[m]** |
| Hook the controller `0x0043ad20` (site D) to shape the rate directly | 20 callers including six in the cockpit transition **[m]**; per-frame C++ on every axis of every object; the e-servo of §2.4 reaches the same profile within `a × 50 ms` without it |
| Per-frame steering at the integrator (site C) | per-object per-frame cost for every integrated object and no benefit: the controller already integrates smoothly between think ticks |
| Write `[obj+0x14]` at site A | overwritten by the order case in the same tick **[m]** |
| Write `f+0x278` (speed limit) for the schedule | script-owned (26 KC call sites, reset to −1 by the avoid order **[m]**); an ownership protocol against the script is fragile; the B clamp is one frame late at worst and never conflicts |
| Replace the dynamics (write `obj+0x24..+0x2c`, bypass the controller) | needs autopilot-clear semantics the AI does not use, loses the record's angular acceleration and `f+0x2b0`, interacts with the auto-level and attitude-offset paths |
| Engine mesh warn test `0x0044d9c0` as the station narrow phase | internals, cost and reentrancy unknown; the collider family costs 230k visits per station query **[m]** |
| Shrink station spheres by a factor only | a 0.35 factor on a 52 km base is still 18 km; without a shape model the detour is either absurd or unsafe; kept only as the fallback behind the reactive warn |
| Suppress `CollisionWarn` by flags (`0x8000000`, `Flags2 0x20000`) | the first disables the fallback, the second removes the ship from collision response **[m]** |
| Replace the KC `CollisionWarn`/wrappers | script content differs between stock and Mayhem; the engine-side E hook needs no script change |
| Per-class parameter tables | mods change every number; formulas on the live getters include tunings and hull damage for free |
| Waypoint lists inside the steering layer | belongs to the planner note; the layer only consumes a polyline (§9) |

## Unknown, and what settles it

| Open point | Settles it |
| --- | --- |
| Axis naming (`+0x30` yaw about up vs pitch about right) and the sign of the body components | the first flight's `ship_steering_decision` row against the next frame's `obj+0x18..+0x20` delta (ship-movement.md §12); the core keeps the mapping in one table |
| `k` (record `+0xd0`) per class in the installed tables, for the rate-error bound | read in the summary from the record (one `tships_columns.py` column) |
| Whether the slew block runs before or after `0x004593dd` for the object | the site verifier's listing of the handler exits (`jmp 0x004593dd` from `0x004594ef..`); only the one-frame latency statement depends on it |
| Calling convention of the four getters when called from our handler | the call sites `0x0045651b`, `0x0045655c`, `0x00456568` and `0x0043a6a6` in the verifier; fallback: record fields × tuning fractions |
| EFLAGS and `&out` after `0x0044dd90` returns 0 | one listing of `0x0044e4f0..0x0044e52b` before step 2 claims E |
| The asteroid class word; the station render-node root from the sector object; the part list under it | the first flight's bucket census (class histogram per bucket) and the `engine_nozzle_walk_core.h` walk applied to a station pointer in a `--debug` row |
| Whether NPC ships carry `0x80000` at all times (the filter assumes it, as the integrator's rotation path does **[m]**) | the summary's pass-through reason `no_autopilot` count must be ~0 for ordered ships |
| The port's roll for an earlier dock alignment (`f+0x254`) | not needed for the recommendation; a later disassembly of `0x00454770..0x00454790` |
