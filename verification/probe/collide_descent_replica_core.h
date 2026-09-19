#pragma once
#include "collide_sat_sse2_core.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// Portable core of the SSE2 replacement of the engine's RAPID OBB-tree descent
// 0x004e2530 (docs/reverse-engineering/sector-collide.md section 13). No
// Windows dependency: the host tests compile it.
//
// 004e293e  33 c0 / d9 1c 24 / 51 / 52 / 53 / 57        XOR EAX,EAX; FSTP [ESP] (s); PUSH ECX (T); PUSH EDX (R); PUSH EBX (b); PUSH EDI (a)
// 004e2947  a3 44 85 60 00 / a3 48.. / a3 4c..          visit, triangle-test and contact counters = 0
// 004e2956  e8 d5 fb ff ff                              CALL 0x004e2530        <- the site (sole external caller)
// 004e295b  83 c4 14 / 5f 5e 5d 5b / 81 c4 98 00 00 00 / c3
//
// int __cdecl 0x004e2530(BV* a, BV* b, float* R, float* T, float s). Per entry:
//   contacts = [0x60854c]; if ([0x596934] && contacts > 0) return 0; if ([0x608534] & 4 && contacts >= [0x608538]) return 0;
//   ++[0x608544]; bs = float32(b.d * s); if (SAT(R, bs, T, a.d)) return 0;
//   both leaves: return leaf(ESI = a, EAX = b)                  (0x004e2190, which always returns 0)
//   split a when b is a leaf, or when neither is and b.d[0] < a.d[0] (ordered); otherwise split b.
//   split b: child = b[+0x40] then b[+0x3c]: R' = R x child.R (0x004e1ff0), T' = (R . child.c) * s + T (0x004e20d0)
//   split a: child = a[+0x40] then a[+0x3c]: R' = child.R^T x R (0x004dfd80), T' = child.R^T . float32(T - child.c) (0x004dfe60)
//   a non-zero result of the first child is returned at once; the second child's result is returned as is.
// Every product of two float32 values is exact in double and the sums keep the
// engine's association order, so with the x87 computing in double (FEX reduced
// precision) each float32 store below is bit-identical to the engine's.
namespace x3m::collide_descent_sse2::core {
constexpr std::uintptr_t descent_site_va = 0x004e2956, descent_target_va = 0x004e2530, descent_return_va = 0x004e295b, descent_target_end_va = 0x004e2777;
constexpr std::uintptr_t leaf_va = 0x004e2190, leaf_end_va = 0x004e252e;
constexpr std::uintptr_t contacts_va = 0x0060854c, first_contact_va = 0x00596934, flags_va = 0x00608534, cap_va = 0x00608538, visits_va = 0x00608544;
constexpr unsigned call_length = 5, descent_pre_length = 24, descent_post_length = 14, descent_body_length = 0x247, leaf_body_length = 0x39e;
constexpr unsigned char descent_pre_window[descent_pre_length] = {
    0x33,0xc0, 0xd9,0x1c,0x24, 0x51, 0x52, 0x53, 0x57, 0xa3,0x44,0x85,0x60,0x00, 0xa3,0x48,0x85,0x60,0x00, 0xa3,0x4c,0x85,0x60,0x00};
constexpr unsigned char descent_post_window[descent_post_length] = {0x83,0xc4,0x14, 0x5f, 0x5e, 0x5d, 0x5b, 0x81,0xc4,0x98,0x00,0x00,0x00, 0xc3};
// Bytes of the descent body other modules may have rewritten before this one looks: the census's entry claim
// (site 7, 0x004e2530 +0..+4) and the SAT module's rel32 (0x004e25a3 +1..+4). They are zeroed before hashing;
// the leaf's entry (census site 8, +0..+4) likewise.
constexpr unsigned entry_hole = 5, sat_rel32_offset = 0x74, sat_rel32_length = 4;
// FNV-1a 64 of the pinned image with those holes zeroed: the descent 0x004e2530..0x004e2776, the leaf
// 0x004e2190..0x004e252d, and the four transform helpers 0x004e1ff0 (223 B), 0x004e20d0 (95 B), 0x004dfd80 (223 B), 0x004dfe60 (75 B).
constexpr std::uint64_t descent_body_fnv1a = 0xcef4870863cdd1deull, leaf_body_fnv1a = 0xa9766de75c6ecfcaull, helpers_fnv1a = 0xac374990f77face7ull;
struct Range { std::uintptr_t va; unsigned length; };
constexpr Range helper_ranges[4] = {{0x004e1ff0, 223}, {0x004e20d0, 95}, {0x004dfd80, 223}, {0x004dfe60, 75}};

// The engine's BV node (0x48 bytes on x86; the host tests build the same struct at the host's pointer width).
struct Node {
    float R[9];            // +0x00 rotation in the parent's frame, row-major
    float c[3];            // +0x24 centre
    float d[3];            // +0x30 half-extents
    const Node* first;     // +0x3c
    const Node* second;    // +0x40 (descended before +0x3c)
    const void* triangle;  // +0x44
};
static_assert(sizeof(void*) != 4 || (sizeof(Node) == 0x48 && offsetof(Node, first) == 0x3c && offsetof(Node, second) == 0x40), "BV node layout");

// The four engine transforms. P and T are the parent's R and T, converted to Real once per descending visit and shared
// by both children; n is the child's rotation, k its centre.
// 0x004e1ff0: R' = P x n.
template <class Real> inline void mul_rr(const Real* c, const float* n, float* o) {
    const Real n0 = n[0], n1 = n[1], n2 = n[2], n3 = n[3], n4 = n[4], n5 = n[5], n6 = n[6], n7 = n[7], n8 = n[8];
    o[0] = float((c[1] * n3 + c[0] * n0) + n6 * c[2]);
    o[3] = float((c[4] * n3 + c[5] * n6) + n0 * c[3]);
    o[6] = float((c[6] * n0 + n6 * c[8]) + c[7] * n3);
    o[1] = float((n7 * c[2] + c[1] * n4) + n1 * c[0]);
    o[4] = float((n1 * c[3] + c[5] * n7) + c[4] * n4);
    o[7] = float((n7 * c[8] + n4 * c[7]) + n1 * c[6]);
    o[2] = float((c[1] * n5 + n8 * c[2]) + n2 * c[0]);
    o[5] = float((c[5] * n8 + c[4] * n5) + n2 * c[3]);
    o[8] = float((n8 * c[8] + n5 * c[7]) + n2 * c[6]);
}
// 0x004e20d0: T' = (P . k) * s + T.
template <class Real> inline void mul_rc_scaled(const Real* P, const float* k, const Real* T, Real s, float* o) {
    const Real k0 = k[0], k1 = k[1], k2 = k[2];
    o[0] = float(((P[2] * k2 + P[1] * k1) + P[0] * k0) * s + T[0]);
    o[1] = float(((P[5] * k2 + P[3] * k0) + P[4] * k1) * s + T[1]);
    o[2] = float(((P[8] * k2 + P[6] * k0) + P[7] * k1) * s + T[2]);
}
// 0x004dfd80: R' = n^T x P.
template <class Real> inline void mul_trr(const float* n, const Real* r, float* o) {
    const Real n0 = n[0], n1 = n[1], n2 = n[2], n3 = n[3], n4 = n[4], n5 = n[5], n6 = n[6], n7 = n[7], n8 = n[8];
    o[0] = float((n3 * r[3] + n0 * r[0]) + r[6] * n6);
    o[3] = float((n4 * r[3] + n7 * r[6]) + r[0] * n1);
    o[6] = float((n2 * r[0] + r[6] * n8) + n5 * r[3]);
    o[1] = float((r[7] * n6 + n3 * r[4]) + r[1] * n0);
    o[4] = float((r[1] * n1 + n7 * r[7]) + n4 * r[4]);
    o[7] = float((r[7] * n8 + r[4] * n5) + r[1] * n2);
    o[2] = float((n3 * r[5] + r[8] * n6) + r[2] * n0);
    o[5] = float((n7 * r[8] + n4 * r[5]) + r[2] * n1);
    o[8] = float((r[8] * n8 + r[5] * n5) + r[2] * n2);
}
// 0x004e26c2..0x004e26ed: v = float32(T - k) per component, then 0x004dfe60: T' = n^T . v.
template <class Real> inline void mul_trv(const float* n, const Real* T, const float* k, float* o) {
    const Real v0 = float(T[0] - Real(k[0])), v1 = float(T[1] - Real(k[1])), v2 = float(T[2] - Real(k[2]));
    o[0] = float((Real(n[6]) * v2 + Real(n[3]) * v1) + Real(n[0]) * v0);
    o[1] = float((Real(n[7]) * v2 + Real(n[1]) * v0) + Real(n[4]) * v1);
    o[2] = float((Real(n[8]) * v2 + Real(n[2]) * v0) + Real(n[5]) * v1);
}

// A node pair about to be entered, with b's frame in a's.
struct Pair { const Node* a; const Node* b; float R[9]; float T[3]; };
// Pending second children. One frame covers `stack_entries` levels of combined tree depth (about 4 KB); a deeper
// (degenerate, unbalanced) pair continues in a nested frame, so the descent is never capped, and it needs less stack
// per level (56 B) than the engine's own recursion (0x6c B plus the helpers' return addresses).
constexpr unsigned stack_entries = 64;

// The child pair of `parent` in which `child` replaces a (split_a) or b.
template <class Real> inline void compose(Pair& out, const Pair& parent, const Real* P, const Real* T, Real s, bool split_a, const Node* child) {
    if (split_a) { out.a = child; out.b = parent.b; mul_trr<Real>(child->R, P, out.R); mul_trv<Real>(child->R, T, child->c, out.T); }
    else { out.a = parent.a; out.b = child; mul_rr<Real>(P, child->R, out.R); mul_rc_scaled<Real>(P, child->c, T, s, out.T); }
}

// Env supplies the engine state: contacts(), first_contact(), flags(), cap() (read at every entry, as the engine
// does), add_visits(n) / add_entries(n) (counters, flushed before every leaf call and at the end), leaf(a, b) and
// visit(pair, bs) (a no-op in production; the fixture records the sequence).
template <class Real, class Env> int descend_pair(const Pair& start, float s_in, Env& env) {
    Pair stack[stack_entries];
    unsigned depth = 0;
    std::uint32_t visits = 0, entries = 0;
    Pair now = start;
    const Real s = s_in;
    for (;;) {
        ++entries;
        const std::int32_t contacts = env.contacts();
        const bool stop = (env.first_contact() != 0 && contacts > 0) || ((env.flags() & 4u) != 0 && contacts >= env.cap());
        if (!stop) {
            ++visits;
            const float bs[3] = {float(double(now.b->d[0]) * double(s_in)), float(double(now.b->d[1]) * double(s_in)), float(double(now.b->d[2]) * double(s_in))};
            env.visit(now, bs);
            if (collide_sat_sse2::core::obb_disjoint(now.R, bs, now.T, now.a->d) == 0) {
                const bool a_leaf = now.a->first == nullptr && now.a->second == nullptr, b_leaf = now.b->first == nullptr && now.b->second == nullptr;
                if (a_leaf && b_leaf) {
                    env.add_visits(visits); env.add_entries(entries);
                    visits = entries = 0;
                    const int result = env.leaf(now.a, now.b);
                    if (result != 0) return result;   // the engine returns a non-zero result through every level at once
                } else {
                    const bool split_a = b_leaf || (!a_leaf && now.b->d[0] < now.a->d[0]);   // unordered: split b, as `fcomp; test ah,5; jnp`
                    const Node* const split = split_a ? now.a : now.b;
                    Real P[9], T[3];
                    for (unsigned i = 0; i < 9; ++i) P[i] = now.R[i];
                    for (unsigned i = 0; i < 3; ++i) T[i] = now.T[i];
                    Pair next;
                    compose<Real>(next, now, P, T, s, split_a, split->second);   // +0x40 is entered first
                    if (depth == stack_entries) {   // out of slots: the first child's subtree in a nested frame, then go on with the second
                        env.add_visits(visits); env.add_entries(entries);
                        visits = entries = 0;
                        const int result = descend_pair<Real>(next, s_in, env);
                        if (result != 0) return result;
                        compose<Real>(next, now, P, T, s, split_a, split->first);
                    } else {
                        compose<Real>(stack[depth++], now, P, T, s, split_a, split->first);
                    }
                    now = next;
                    continue;
                }
            }
        }
        if (depth == 0) break;
        now = stack[--depth];
    }
    env.add_visits(visits); env.add_entries(entries);
    return 0;
}
template <class Real, class Env> int descend(const Node* a, const Node* b, const float* R, const float* T, float s, Env& env) {
    Pair start;
    start.a = a; start.b = b;
    std::memcpy(start.R, R, sizeof start.R);
    std::memcpy(start.T, T, sizeof start.T);
    return descend_pair<Real>(start, s, env);
}
}
