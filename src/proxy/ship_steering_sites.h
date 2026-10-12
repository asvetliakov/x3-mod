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
namespace x3m::ship_steering::sites {
enum Index : unsigned { LeafYawPitch = 0, LeafRollYaw = 1, LeafRollPitch = 2, LoopContinue = 3, Count = 4 };
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
constexpr std::uint32_t kFlagAutopilot = 0x80000, kFlagDeleted = 0x8000000, kFlagAttached = 0x200;
constexpr std::uint32_t kPhysPosition = 0x30, kPhysBasis = 0x40;
constexpr std::uint32_t kFlightTarget = 0x224, kFlightPoint = 0x240, kFlightOrder = 0x258, kFlightDistance = 0x260;
constexpr std::uint32_t kFlightRotAccel = 0x2b0, kFlightBasis = 0x870;
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
}
