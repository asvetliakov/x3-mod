#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the SSE2 replacement of the engine's RAPID OBB-OBB
// separating-axis test 0x004e3280 (docs/reverse-engineering/sector-collide.md
// sections 12.5 and 12.8). No Windows dependency: the host tests compile it.
//
// 004e2584  d9 c9 / 83 c0 30 / d9 5c 24 20 / 50        FXCH; ADD EAX,0x30 (&a.d); FSTP [s*b.d0]; PUSH EAX
// 004e258e  d9 45 34 / 53 / d8 c9 / 8d 7c 24 28        FLD [b+0x34]; PUSH EBX (T); FMUL; LEA EDI,[ESP+0x28] (&s*b.d)
// 004e2598  d9 5c 24 2c / d8 4d 38 / d9 5c 24 30       FSTP; FMUL [b+0x38]; FSTP  -> x87 stack empty
// 004e25a3  e8 d8 0c 00 00                             CALL 0x004e3280        <- the site (sole caller in the image)
// 004e25a8  83 c4 08 / 85 c0 / 75 9a                   ADD ESP,8; TEST EAX,EAX; JNE 0x004e2549 (return 0: pruned)
//
// 0x004e3280: ESI = R (9 floats, row-major, b's frame in a's), EDI = b's
// half-extents (already scaled), [ESP+4] = T, [ESP+8] = a's half-extents; the
// caller pops 8. Returns EAX = 0 (no separating axis: descend) or 1..15, the
// first separating axis in RAPID's order A0 B0 A1 A2 B1 B2 A0xB0..A2xB2. The
// original writes EAX, EFLAGS, x87 state and its own second argument slot only
// (`push ecx` is a stack reservation): EBX/EBP are saved, ECX/EDX/ESI/EDI are
// never written.
namespace x3m::collide_sat_sse2::core {
constexpr std::uintptr_t sat_site_va = 0x004e25a3, sat_target_va = 0x004e3280, sat_return_va = 0x004e25a8;
constexpr std::uintptr_t sat_target_end_va = 0x004e38ae, sat_function_va = 0x004e2530, sat_function_end_va = 0x004e2780;
constexpr unsigned call_length = 5, sat_pre_length = 31, sat_post_length = 7, sat_callee_length = 1582;
constexpr unsigned char sat_pre_window[sat_pre_length] = {
    0xd9, 0xc9, 0x83, 0xc0, 0x30, 0xd9, 0x5c, 0x24, 0x20, 0x50, 0xd9, 0x45, 0x34, 0x53, 0xd8, 0xc9,
    0x8d, 0x7c, 0x24, 0x28, 0xd9, 0x5c, 0x24, 0x2c, 0xd8, 0x4d, 0x38, 0xd9, 0x5c, 0x24, 0x30};
constexpr unsigned char sat_post_window[sat_post_length] = {0x83, 0xc4, 0x08, 0x85, 0xc0, 0x75, 0x9a};
// FNV-1a 64 of the 1,582 bytes 0x004e3280..0x004e38ad of the pinned image (the body this module replaces).
constexpr std::uint64_t sat_callee_fnv1a = 0xad8a2cb66c0bf30cull;
inline std::uint64_t fnv1a(const unsigned char* p, unsigned n, std::uint64_t h = 0xcbf29ce484222325ull) {
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

// The engine adds reps = 1e-6f (0x00565600) to |R| and stores each entry back as float32.
constexpr float reps = 1e-6f;
// The engine's compare is `fcompp; fnstsw; test ah,1`: the axis separates when C0 is set, i.e.
// when ra + rb < |T.L| OR the compare is unordered. Unordered must separate here too: a kept
// NaN pair would descend to the leaf triangle test, whose own NaN compares read "not separated"
// and would report a contact the engine never reports. So: separated = !(t <= limit), which is
// the engine's predicate bit for bit on NaN, infinities and negative radius sums, with
// limit = (ra + rb) * (1 + 2^-20). The margin only ever keeps a finite pair the engine prunes
// (cost: visits); 2^-20 exceeds what an x87 at 24-bit precision control can lose over these four
// sums (~2^-22), so no x87 mode can keep a finite pair that this prunes.
constexpr double slack = 1.0 + 0x1p-20;
inline bool separates(double t, double radius_sum) {
    return !(t <= radius_sum * slack);
}
// |v| by clearing the sign bit in the integer domain: with -mfpmath=sse GCC still emits x87 `fld; fabs; fstp` for
// std::fabs of a value it loads from memory, and this code must not touch the x87 state.
inline float abs_f(float v) {
    std::uint32_t u;
    std::memcpy(&u, &v, 4);
    u &= 0x7fffffffu;
    std::memcpy(&v, &u, 4);
    return v;
}
// The engine passes every projected distance through a float32 stack slot before `fabs` (0x0040e710 takes a float).
inline double through_float(double v) {
    return static_cast<double>(abs_f(static_cast<float>(v)));
}
inline double bf(float r) {
    return static_cast<double>(static_cast<float>(static_cast<double>(abs_f(r)) + static_cast<double>(reps)));
}

// Same operands in the same association order as 0x004e3280 (all products of
// two float32 values are exact in double). Bf rows are computed when first used.
[[gnu::always_inline]] inline int obb_disjoint(const float* R, const float* bx, const float* Tx, const float* ax) {
    const double a0 = ax[0], a1 = ax[1], a2 = ax[2], b0 = bx[0], b1 = bx[1], b2 = bx[2], T0 = Tx[0], T1 = Tx[1],
                 T2 = Tx[2];
    const double t0 = abs_f(Tx[0]), t1 = abs_f(Tx[1]), t2 = abs_f(Tx[2]);
    const double f0 = bf(R[0]), f1 = bf(R[1]), f2 = bf(R[2]);
    if (separates(t0, ((b2 * f2 + b1 * f1) + b0 * f0) + a0)) return 1; // A0
    const double R0 = R[0], R3 = R[3], R6 = R[6], f3 = bf(R[3]), f6 = bf(R[6]);
    if (separates(through_float((T1 * R3 + T2 * R6) + R0 * T0), ((a2 * f6 + a1 * f3) + a0 * f0) + b0)) return 2; // B0
    const double f4 = bf(R[4]), f5 = bf(R[5]);
    if (separates(t1, ((b2 * f5 + b1 * f4) + b0 * f3) + a1)) return 3; // A1
    const double f7 = bf(R[7]), f8 = bf(R[8]);
    if (separates(t2, ((b2 * f8 + b1 * f7) + b0 * f6) + a2)) return 4; // A2
    const double R1 = R[1], R4 = R[4], R7 = R[7];
    if (separates(through_float((T2 * R7 + R4 * T1) + R1 * T0), ((a2 * f7 + a1 * f4) + a0 * f1) + b1)) return 5; // B1
    const double R2 = R[2], R5 = R[5], R8 = R[8];
    if (separates(through_float((T0 * R2 + T1 * R5) + R8 * T2), ((a2 * f8 + a1 * f5) + a0 * f2) + b2)) return 6; // B2
    if (separates(through_float(T2 * R3 - T1 * R6), ((b1 * f2 + b2 * f1) + a1 * f6) + a2 * f3)) return 7;  // A0 x B0
    if (separates(through_float(R4 * T2 - T1 * R7), ((b2 * f0 + a1 * f7) + a2 * f4) + b0 * f2)) return 8;  // A0 x B1
    if (separates(through_float(T2 * R5 - R8 * T1), ((b1 * f0 + a1 * f8) + a2 * f5) + b0 * f1)) return 9;  // A0 x B2
    if (separates(through_float(T0 * R6 - T2 * R0), ((b1 * f5 + b2 * f4) + a0 * f6) + a2 * f0)) return 10; // A1 x B0
    if (separates(through_float(T0 * R7 - T2 * R1), ((b2 * f3 + a0 * f7) + a2 * f1) + b0 * f5)) return 11; // A1 x B1
    if (separates(through_float(R8 * T0 - T2 * R2), ((b1 * f3 + a0 * f8) + a2 * f2) + b0 * f4)) return 12; // A1 x B2
    if (separates(through_float(T1 * R0 - T0 * R3), ((b1 * f8 + b2 * f7) + a0 * f3) + a1 * f0)) return 13; // A2 x B0
    if (separates(through_float(T1 * R1 - R4 * T0), ((b2 * f6 + a0 * f4) + a1 * f1) + b0 * f8)) return 14; // A2 x B1
    if (separates(through_float(T1 * R2 - R5 * T0), ((b1 * f6 + a0 * f5) + a1 * f2) + b0 * f7)) return 15; // A2 x B2
    return 0;
}
}
