#pragma once
#include <cstdint>
#include "engine_patch.h"

// The four byte-verified sites of the smooth ship steering (X3M_SHIP_STEERING=1;
// docs/architecture/ship-steering-and-avoidance.md section 1; engine facts
// docs/reverse-engineering/ship-movement.md sections 5, 6 and 11,
// ship-navigation-ai.md section 6). Every byte here is re-derived from the
// installed EXE by verification/probe/verify_ship_steering_sites.py, which
// also proves whole-instruction boundaries, no branch into a span interior,
// no data dword into any window, the branch census of the loop continue and
// the getter conventions below.
//
// A1-A3: the three steering leaves, entry span `sub esp,0x18` | `mov eax,[edx]`
// (5 B, two whole instructions). Contract at entry [m]: EDX = const int32[3]*
// target (world units), [esp+4] = obj, [esp] = return; EAX/ECX dead (written at
// +3/+5 before any read); flags dead; EBX/EBP/ESI/EDI callee-saved by the leaf;
// `ret 4` (0x0044d011, 0x0044c960, 0x0044ccb2). The stub saves EAX/ECX/EDX and
// XMM0-7, calls the handler with a Frame*, and either restores and `ret 4`
// (handled: obj+0x30/+0x34/+0x38 written) or restores and continues into the
// claim tail (pass-through: the original leaf runs; EDX may then point at the
// module's target ring, the step-2 seam).
//
// B: the object-loop continue of the command pass 0x00452ad0,
// `mov ebx,[ebx]` | `cmp dword [ebx],0` (5 B, two whole instructions), followed
// by `jne 0x00452b60`. EBX = the object whose order step just ran; all GPRs
// live (ESP-relative locals: ESP exact); the flags are dead (the replayed
// `cmp` in the tail rewrites every arithmetic flag before the `jne` reads
// them, and `mov` reads none), so the stub needs no pushfd. The stub filters
// class 7 + autopilot in asm before any C++ call.
//
// Pinned by listing (ratification amendment 3), 2026-10-12, EXE fdbf3418...:
// (a) Getter conventions. 0x00450980 max speed: `sub esp,8; push ebx; push esi;
//     mov esi,eax` ... `pop edi; pop esi; pop ebx; add esp,8; ret` -> obj in
//     EAX, result EAX, EBX/ESI/EDI preserved, ECX/EDX clobbered (it calls
//     0x0043a690 and 0x0042fb20). 0x00450b80 acceleration: `movsx eax,word
//     [ecx+0x48]` ... `ret` -> obj in ECX, result EAX, EDX clobbered.
//     0x00450d60 (yaw), 0x00450cf0 (pitch), 0x00450dd0 (roll): `movzx edx,word
//     [eax+0x48]` ... `ret` -> obj in EAX, result EAX, ECX/EDX clobbered. Call
//     sites: 0x00456519 `mov eax,ebx; call 0x00450d60`, 0x00456544 `mov eax,ebx;
//     call 0x00450980`, 0x00456550 `mov ecx,ebx; call 0x00450b80` (MovePos,
//     EBX = obj). 0x0043a6a6 is the class-7 case of the record max-speed switch
//     0x0043a690 (`ax` = class, `cx` = type index): record +0x44. No stack
//     argument anywhere; no fallback needed.
// (b) Slew order. The speed slew 0x004594ef.. is the body of a second bucket
//     loop (0x00459490..0x004595bf) that the pass runs after the order loop
//     ends (0x004593fd `jl 0x00452b42` advances the bucket; fall-through at
//     0x00459403): no branch targets the slew block from outside it. So for
//     every object site B runs before the slew in the same frame, and a clamp
//     written at B is consumed by that frame's slew (zero-frame latency).
// (c) Axis sign conventions from the leaves' fpatan operands (0x0044cee8:
//     fild -R; fild F; fpatan -> yaw = atan2(-t0, t2); 0x0044cf95: fild U;
//     fild F'; fpatan -> pitch = atan2(t1, t2'); 0x0044c823 in the roll leaf:
//     fild t1-hat; fild t0-hat ... fpatan -> roll = atan2(t1, t0)), with
//     t = (d.row0, d.row1, d.row2) over the flight basis rows f+0x870.
// (d) The "large" predicate 0x0043fd00: record +0x14 <= 0xf, byte table at
//     0x0043fd3c {0,1,0,0,0,1,1,1,0,1,1,0,1,0,0,0} indexing {true,false} ->
//     mask 0xe91d over the SG_SH code (core::kLargeMask).
//
// Step 2 (avoidance), pinned by listing 2026-10-12:
// (e) Site E, the warn test `0x0044dd90(A, B, d, &out)` (stdcall, `ret 0x10`, prologue `push ebp; mov ebp,esp;
//     and esp,-16` = 6 B, three whole instructions). Callers: `0x0044e522` and `0x0044e540` in the proximity
//     handler `0x0044e450` (the second on the order-0x12 refresh path), and `0x00456c02` in the avoid order's
//     handler. At all three the result is consumed by `test eax,eax; je` immediately after the call (EFLAGS
//     dead after the call) and `&out` is read only on a true return: `0x0044e450` initialises its local
//     `[esp+0x18] = 0` at `0x0044e460` and never reads it (the true path appends `d` = `[esp+0x1c]`);
//     `0x00456c02`'s caller reads `[esp+0x18]` only after `test eax,eax; je`. ECX/EDX are reloaded before any
//     use after the call. So a false return needs no `&out` write: the stub answers a resolved pair with
//     `xor eax,eax; ret 0x10`. The callee's early true return `0x0044dec7` (close range) writes nothing to
//     `&out` either; the mesh path passes it to `0x0044d9c0`.
// (f) Sector buckets (2026-10-12 review, measured at 0x00452b4d, 0x0045d275, 0x0045cbf4): `table = [sector+0x50]`
//     is a pointer, the list heads are `[table + 0xc*k]` (`lea edx,[eax+eax*2]; mov eax,[ebp+8]; mov ecx,[eax+0x50];
//     mov ebx,[ecx+edx*4]`), two dereferences; each list is linked at `[obj+0]` and ended by a node whose first
//     dword is 0 (the loops run while `[node] != 0`); the bucket index is the class word, so the snapshot walks
//     only the buckets 5/6 stations, 7 ships, 17 asteroids, 18 gates, 28 debris (sector-collide.md,
//     sector-path-planning.md 3.1).
// (g) Station body (measured): the render node is `[obj+0x70]` (position `+0x30..`, basis `+0x40`, 16.16 scale
//     `+0x70`, own radius `+0xa0`, cached subtree radius `+0xa4` from `0x00488170`, recursive over the children);
//     the child list is `c = [node+0xc]; while [c] != 0; c = [c]` (0x0048aa46 `mov edi,[ebx+0xc]; cmp [edi],0`,
//     0x00488248 `mov ebp,[ebp]; cmp [ebp],0; jne`): the node whose first dword is 0 is the sentinel, not a part;
//     each child's local offset `+0x30/+0x34/+0x38` is multiplied by the parent's `+0x70` when `[child+0x12c] & 0x40`
//     (0x004881a9 `test byte [ebp+0x12c],0x40`, 0x004881c4..0x004881e3 `imul; add 0x8000; adc; shrd 16`), else used
//     as is; the child's cached subtree radius is `+0xa4`. The part model (ship_avoidance_walk_core.h) applies the
//     scale under that flag and keeps the model only when every part sphere lies inside the root sphere.
//     Run151 follow-up (listing): `+0xa4` is a lazy cache. `0x004880e0` (init / set-scale) writes `+0x70` = the scale
//     argument, `+0x80/+0x84/+0x88` = 0x10000, `+0xa0` = `+0x70`, `+0xa4` = -1; `0x00488270` (per-axis scale) writes
//     `+0xa0` = `+0x70` x max axis >> 16 and `+0xa4` = -1 (recursing into flagged children); `0x00488170` (thiscall,
//     ECX = node) returns `+0xa4` when >= 0, else the max over the children of |offset x (flag ? parent +0x70 : 1)| +
//     child radius seeded by `+0xa0`, and stores it. Resolution (decision 2026-10-12): the walk replicates that
//     getter read-only (ship_avoidance_walk_core.h subtree_radius: a filled cache is used as is, an unfilled one is
//     recomputed from `+0xa0`, the children's `+0x30` offsets scaled by the parent's `+0x70` under flag 0x40, the
//     per-axis max; depth 3, 64 nodes per station), never calls 0x00488170 and never writes `+0xa4`; a station the
//     replication cannot settle uses the fallback body and is retried. The engine's radius is per axis, so the
//     part validation admits sqrt(3) x the root radius in the Euclidean metric (core::kRootSlack).
//
// Step 3 (gate approach, amendment 7), pinned by listing 2026-10-12 (ship-navigation-ai.md section 10.8; every byte
// re-derived by verify_ship_steering_sites.py):
// (h) P2 `0x00454b91`: the order-4 gate arrival writes `mov [ebx+0x14],eax` | `mov [ebx+0x10],eax` (6 B, two whole
//     instructions), then `call 0x00460560` (the wake). EAX = 0 (`xor eax,eax` at 0x00454b84), EBX = ESI = obj
//     (`mov esi,ebx` at 0x00454b8f), ECX = flight (`mov ecx,[ebx+0x50]` at 0x00454b81); `push 1` already on the
//     stack; flags dead. The stub (live) calls the handler, which writes `[obj+0x14] = 0` and `[obj+0x10] = 0` unless
//     the ship is not capital (record +0x14 code outside kCapitalMask, the predicate 0x0043fd50) and `[flight+0x224]`
//     is class 0x12; then `jmp 0x00454b97` with EAX/ECX/EDX/ESI preserved and ESP exact. Not live: the claim tail.
// (i) P4 `0x00454700`: the order-5 arrival writes the same 6 bytes; EAX = 0 (used at 0x00454717 for the order word),
//     EBX = obj, ECX = flight; flags dead; the join 0x00454710 (from 0x00459191) lies after the window. The handler
//     writes `[obj+0x14] = 0`, keeps `[obj+0x10]` and records it for P5 when the target is a gate and the ship is not
//     capital, else writes both; `jmp 0x00454706`.
// (j) P5 `0x0045526f`: the order-8 launch law `mov eax,0x1967e8` (5 B); EAX = t ms (`lea eax,[ecx+0x384]` at
//     0x00455260), ECX = t^2 and `[esp+0x10]` = t^2 (both rewritten after the window: 0x00455291, 0x00455284),
//     EBX = obj (`or [ebx+0x40],0x400` at 0x00455259); EDX is overwritten at 0x00455281. The stub returns with
//     EAX = v0 t / 1000 + t^2 x 1665000 / 1000000 (units) and `jmp 0x00455281`, skipping the engine's
//     `imul edx; idiv ecx`.
// (k) The capital predicate 0x0043fd50: class word 7, record +0x14 <= 0xd, byte table at 0x0043fd8c
//     {0,1,0,0,0,1,1,1,0,1,1,1,1,0} indexing {true, false} -> mask 0x211d (core::kCapitalMask).
// Step 4 (amendment 8), pinned by listing 2026-10-12:
// (l) The warp-in `SA_WarpObjectIn` case 0x00465b22 (order 0x21): EBX = the ship (class 7 checked at 0x00465b55),
//     EAX = the target object (`[esp+0xc]`); it sets order 0x21, `f+0x218 = 1`, `f+0x214 = now + 900`, copies the
//     ship's position `phys+0x30..+0x3c` into `f+0x240..+0x24c` (0x00465bb2..0x00465bd1, with `test eax,eax` at
//     0x00465bbe feeding `je 0x00465c91` at 0x00465bd4) and, for a class-0x12 target (0x00465bda), writes
//     `phys+0x30 = f+0x240 + 1348650 x gate row 2` (0x00465bec..0x00465c88: 1665000 x 810000 / 10^6 units along
//     `[gate+0x70]+0x60`, 0x40e780 scale, 0x40e720 add). The handler 0x0045533b then writes
//     `phys+0x30 = f+0x240 + 1.665 t_left^2 x row 2` every frame (0x004553cb..) and `phys+0x30 = f+0x240` at the end
//     (0x0045537c..0x004553a5): every position of the warp-in lies on the line through the ship's position at the
//     call, along the gate axis. Site W `0x00465bac`: `mov ecx,[ebx+0x70]` | `mov edx,[ebx+0x50]` (6 B, two whole
//     instructions, before the copy); EAX = target, EBX = obj, ECX/EDX dead (rewritten by the span), ESI dead
//     (rewritten at 0x00465bb2), flags dead (`test eax,eax` follows before any read). The stub offsets `phys+0x30..`
//     laterally in the gate's x-y plane for a non-capital ship and continues into the claim tail: the copy, the
//     case's write and the handler's writes all carry the offset; nothing else reads the position before the copy.
// (m) `SA_RotateToAngles` 0x004653d1 writes `f+0x240/+0x244` = its alpha/beta arguments (0x00465404..0x00465410;
//     the KC passes `SA_GetRotAlpha/Beta(gate)`) and `+0x248` = gamma or 0x80000000; the handler 0x0045920e builds
//     the rotation matrix of those angles (0x004f0270 at 0x0045923a), takes the ship's flight basis row 2
//     (0x00450520, +0x20..+0x2c) and compares its dot with the matrix's forward row against 0xfd70 (0x004592d9,
//     cos 8.1 deg): the target heading is the gate's forward axis (row 2 of its frame; roll leaves it unchanged).
namespace x3m::ship_steering::sites {
enum Index : unsigned { LeafYawPitch = 0, LeafRollYaw = 1, LeafRollPitch = 2, LoopContinue = 3, Count = 4 };
// Steps 3 and 4: the four gate sites, claimed as a group after the steering sites (all or nothing; the gate policy is
// off when any of them refuses, steering stays on).
enum GateIndex : unsigned { GateStopA = 0, GateStopB = 1, GateLaunch = 2, GateWarpIn = 3, GateCount = 4 };
constexpr engine_patch::SiteSpec kGateSites[GateCount] = {
    {"ship_gate_stop_a", 0x00454b91, {0x89, 0x43, 0x14, 0x89, 0x43, 0x10}, 6, 0, 0},
    {"ship_gate_stop_b", 0x00454700, {0x89, 0x43, 0x14, 0x89, 0x43, 0x10}, 6, 0, 0},
    {"ship_gate_launch", 0x0045526f, {0xb8, 0xe8, 0x67, 0x19, 0x00}, 5, 0, 0},
    {"ship_gate_warp_in", 0x00465bac, {0x8b, 0x4b, 0x70, 0x8b, 0x53, 0x50}, 6, 0, 0},
};
constexpr std::uintptr_t kGateStopAResume = 0x00454b97, kGateStopBResume = 0x00454706, kGateLaunchResume = 0x00455281;
// Site E, claimed separately (avoidance runs without it when its bytes mismatch).
constexpr engine_patch::SiteSpec kWarnSite = {"ship_avoid_warn_test", 0x0044dd90, {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0}, 6, 0x10, 0};
constexpr std::uintptr_t kWarnTestEnd = 0x0044e445; // after the last `ret 0x10` at 0x0044e442
constexpr std::uintptr_t kWarnCallSites[3] = {0x0044e522, 0x0044e540, 0x00456c02};
constexpr std::uintptr_t kWarnCallerOutInit = 0x0044e460; // mov dword [esp+0x18], 0
constexpr std::uintptr_t kSectorBuckets = 0x50, kBucketStride = 0xc;
constexpr unsigned kBucketCount = 32;
constexpr std::uint32_t kObjSector = 0x54;
constexpr std::uint32_t kPhysVelocity = 0x100, kNodeChildren = 0xc, kNodeOffset = 0x30, kNodeScale = 0x70, kNodeOwnRadius = 0xa0,
                        kNodeRadius = 0xa4, kNodeFlags = 0x12c, kNodeFlagScaled = 0x40;
constexpr std::uintptr_t kBucketTableSites[3] = {0x00452b4d, 0x0045d275, 0x0045cbf4}; // table = [sector+0x50]; head = [table + 0xc*k]
constexpr std::uintptr_t kChildWalkSites[2] = {0x0048aa46, 0x00488248};                // c = [node+0xc]; while [c] != 0; c = [c]
constexpr std::uintptr_t kChildScaleSite = 0x004881a9;                                 // test byte [child+0x12c], 0x40 -> offset x [parent+0x70]
constexpr std::uint16_t kClassStationA = 5, kClassStationB = 6, kClassAsteroid = 17, kClassDebris = 28;
constexpr std::uint32_t kFlags2NoPair = 0x20000;
constexpr engine_patch::SiteSpec kSites[Count] = {
    {"ship_steer_leaf_yaw_pitch", 0x0044ccc0, {0x83, 0xec, 0x18, 0x8b, 0x02}, 5, 4, 0},
    {"ship_steer_leaf_roll_yaw", 0x0044c620, {0x83, 0xec, 0x18, 0x8b, 0x02}, 5, 4, 0},
    {"ship_steer_leaf_roll_pitch", 0x0044c970, {0x83, 0xec, 0x18, 0x8b, 0x02}, 5, 4, 0},
    {"ship_steer_loop_continue", 0x004593dd, {0x8b, 0x1b, 0x83, 0x3b, 0x00}, 5, 0, 0},
};
static_assert(sizeof(kSites) / sizeof(kSites[0]) == Count, "four sites");
// Function extents (Ghidra listings, exclusive ends after the final ret).
constexpr std::uintptr_t kLeafYawPitchEnd = 0x0044d014, kLeafRollYawEnd = 0x0044c963, kLeafRollPitchEnd = 0x0044ccb5;
constexpr std::uintptr_t kCommandPass = 0x00452ad0, kCommandPassEnd = 0x0045963c; // then the order jump table
constexpr std::uintptr_t kLoopContinueJne = 0x004593e2, kLoopBody = 0x00452b60;
constexpr std::uintptr_t kOrderJumpTable = 0x0045963c, kOrderByteTable = 0x00459694, kOrderByteTableCount = 0x25;
// Engine getters and predicates (conventions above).
constexpr std::uintptr_t kGetMaxSpeed = 0x00450980;   // EAX = obj -> EAX units/s
constexpr std::uintptr_t kGetAccel = 0x00450b80;      // ECX = obj -> EAX units/s^2
constexpr std::uintptr_t kGetYawCap = 0x00450d60;     // EAX = obj -> EAX binary angle/s
constexpr std::uintptr_t kGetPitchCap = 0x00450cf0;   // EAX = obj -> EAX
constexpr std::uintptr_t kGetRollCap = 0x00450dd0;    // EAX = obj -> EAX
constexpr std::uintptr_t kLargePredicate = 0x0043fd00, kLargeTable = 0x0043fd3c, kLargeJumpTable = 0x0043fd34;
constexpr std::uintptr_t kCapitalPredicate = 0x0043fd50;
constexpr std::uintptr_t kGetterCallSites[3] = {0x0045651b, 0x00456546, 0x00456554}; // yaw cap, max speed, accel (MovePos)
constexpr std::uintptr_t kShipTypeTableSlot = 0x00606fd4; // *0x00606fd4 + [obj+0x4a] * 0xdb8 = the TShips record
constexpr std::uintptr_t kGameClock = 0x00606f34;         // [*0x00606f34 + 0x718] = game time, ms
constexpr std::uint32_t kGameTimeOffset = 0x718, kRecordStride = 0xdb8;
// Object fields (ship-movement.md section 2).
constexpr std::uint32_t kObjNext = 0x00, kObjSpeed = 0x10, kObjDesiredSpeed = 0x14, kObjRates = 0x24, kObjRemaining = 0x30;
constexpr std::uint32_t kObjFlags = 0x40, kObjFlags2 = 0x44, kObjClass = 0x48, kObjType = 0x4a, kObjFlight = 0x50;
constexpr std::uint32_t kObjPhys = 0x70, kObjRadius = 0xa4, kObjDocked = 0xf8;
constexpr std::uint32_t kObjSlip = 0x60; // the integrator's slip velocity, units/s (ship-movement.md section 13.2, 0x0045a610..0x0045a63b); amendment 14 reads it
constexpr std::uint32_t kFlagAutopilot = 0x80000, kFlagDeleted = 0x8000000, kFlagAttached = 0x200;
constexpr std::uint32_t kPhysPosition = 0x30, kPhysBasis = 0x40;
constexpr std::uint32_t kFlightTarget = 0x224, kFlightPoint = 0x240, kFlightOrder = 0x258, kFlightDistance = 0x260;
constexpr std::uint32_t kFlightPortIndex = 0x250; // dword, -1 = no port (a gate); 0x00453fd9, 0x00454706 (ship-movement.md section 13.1)
constexpr std::uint32_t kFlightRotAccel = 0x2b0, kFlightBasis = 0x870;
// The engine's own obstacle list of the collision pass (ship-navigation-ai.md section 3.1, 0x0044e552): count at f+0x1e0,
// up to kFlightObstacleMax objects at f+0x1e4[] and their centre distances (units) at f+0x1f8[]; read only, by the
// ship_trace at a followed ship's first order-0x12 row (capital-gate-transit.md section 6). Pinned by the verifier's
// WALK_FACTS (0x0045d298 `mov [ebx+0x1e0],edi`; 0x0044e54c `mov eax,[ecx+0x1e0]; cmp eax,5; jge; mov [ecx+eax*4+0x1e4],ebp`;
// 0x0044e565 `mov [edx+eax*4+0x1f8],ecx`).
constexpr std::uint32_t kFlightObstacleCount = 0x1e0, kFlightObstacleList = 0x1e4, kFlightObstacleDist = 0x1f8;
constexpr std::uint32_t kFlightObstacleMax = 5;
// Amendment 15 (capital-gate-transit.md section 15.4): the warn bits of [obj+0x40]: the warn caller sets 0x1000 on a true
// verdict (0x0044e52b, E_CALLER_TRUE_PATH of the verifier) and both bits stand on the frame the KC's CollisionWarn ran, until L0
// of the next collision pass clears them (ship-navigation-ai.md section 3.2); read only, by the warn_pre row of a traced ship.
constexpr std::uint32_t kFlagWarnBits = 0x3000;
// The engine's native object id `ship+8` (docs/reverse-engineering/chase-view-transition.md, the SA_GetEventObject row: the
// native body ID 0x004607dc looks up through 0x0043a4f0 before 0x004607f4 reads +0x94; ship+8 must resolve to the exact ship;
// chase_transition_identity_core.h reads it the same way): the identity of the membership memory (section 15.2), read only.
constexpr std::uint32_t kObjId = 0x08;
constexpr std::uint32_t kRecordClassCode = 0x14, kRecordRotAccel = 0xd0;
constexpr std::uint32_t kClassShip = 7, kClassGate = 0x12;
// The stub frame the A handler receives (ship_steering.cpp, the stubs).
struct Frame {
    float xmm[8][4];
    std::uint32_t edx, ecx, eax; // edx = the target pointer; the handler may redirect it (step 2)
    std::uint32_t ret, obj;
};
static_assert(sizeof(Frame) == 0x94, "stub frame layout");
constexpr unsigned kSettingCapacity = 128; // X3M_SHIP_STEERING_CLASSES: at most 127 characters
constexpr std::uint32_t kClampMaxAgeMs = 250; // a decision older than this no longer clamps at B
constexpr unsigned kSnapshotSlots = 4;      // sectors snapshotted per frame (active sectors are few)
constexpr unsigned kBucketWalkLimit = 8192; // objects visited per sector snapshot at most (a corrupt list ends the walk)
}
