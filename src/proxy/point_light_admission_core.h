#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the root-object point-light admission patch
// (docs/reverse-engineering/camera-and-lights.md, "Point-light admission site";
// docs/architecture/original-shading-critique.md, Q3): the verified byte
// window, the six-byte site and its two branch targets, the detour and site
// encodings, and the integer-only root-admission predicate. No Windows
// dependency so the host tests compile it directly.
namespace x3m::point_light_admission::core {
// Material submission routine 0x004c0150 (ret at 0x004c40a3; the last epilogue
// ends before the int3 padding at 0x004c40fc). The comparison inside its
// point-light loop, with the admit fall-through and the reject target:
//
// 004c27a1  2b 86 58 01 00 00   SUB  EAX,[ESI+0x158]   ; - light node range
// 004c27a7  8b 4d 0c            MOV  ECX,[EBP+0xc]     ; the submitted node
// 004c27aa  2b 41 70            SUB  EAX,[ECX+0x70]    ; - node base scale
// 004c27ad  85 c0               TEST EAX,EAX
// 004c27af  0f 8f 40 02 00 00   JG   0x004c29f5        ; reject   <- replaced by JMP detour; NOP
// 004c27b5  8b c6               MOV  EAX,ESI           ; admit: EAX redefined
// 004c27b7  8b b0 6c 01 00 00   MOV  ESI,[EAX+0x16c]   ; (label from 0x004c2737, directional lights)
// ...
// 004c29f5  8b 44 24 5c         MOV  EAX,[ESP+0x5c]    ; reject: EAX redefined, next slot
// 004c29f9  8b 15 18 85 60 00   MOV  EDX,[0x00608518]
constexpr std::uintptr_t function_va = 0x004c0150, function_end_va = 0x004c40fc;
constexpr std::uintptr_t window_va = 0x004c27a1, site_va = 0x004c27af, admit_va = 0x004c27b5, reject_va = 0x004c29f5;
constexpr unsigned window_length = 28, site_offset = 14, site_length = 6, reject_prefix_length = 4;
constexpr std::int32_t site_rel32 = 0x240;  // reject_va - (site_va + site_length)
constexpr unsigned char expected_window[window_length] = {
    0x2b,0x86,0x58,0x01,0x00,0x00, 0x8b,0x4d,0x0c, 0x2b,0x41,0x70, 0x85,0xc0,
    0x0f,0x8f,0x40,0x02,0x00,0x00, 0x8b,0xc6, 0x8b,0xb0,0x6c,0x01,0x00,0x00};
constexpr unsigned char expected_site[site_length] = {0x0f,0x8f,0x40,0x02,0x00,0x00};
// The in-process check covers the reject target's first instruction; the
// site verifier checks the following `mov edx,[0x00608518]` on the image too.
constexpr unsigned char expected_reject_prefix[reject_prefix_length] = {0x8b,0x44,0x24,0x5c};
// Render-node fields (render-node-bounds.md, camera-and-lights.md): parent link
// written by the attach helper 0x00489f20 and cleared by 0x00489dbe; base
// scale; position triple; the light node's own range.
constexpr unsigned parent_offset = 0x18, scale_offset = 0x70, position_offset = 0xb0, range_offset = 0x158;
// Parent reads per test before the chain is given up (a cycle or an unexpected
// node class then degrades to the native per-node rejection).
constexpr unsigned max_hops = 8;

enum class Outcome : unsigned char {
    admitted = 0,        // the root passes the same predicate: admit
    node_is_root,        // no parent: the per-node rejection stands
    chain_unreadable,    // a parent link could not be read: reject (fail closed)
    chain_too_deep,      // max_hops parents without reaching a root: reject
    chain_cycle,         // a parent link points back at the node or at itself: reject
    root_unreadable,     // the root's scale/position could not be read: reject
    light_unreadable,    // the light's range/position could not be read: reject
    reach_negative,      // range + root scale < 0: reject
    root_rejected,       // the root fails the predicate: reject
};
constexpr unsigned outcome_count = 9;

// The same predicate the engine applies per node, for the root of `node`
// against `light`: |root_pos - light_pos| <= light range + root scale, in raw
// render-domain integers. Deltas wrap in 32 bits exactly as the engine's SUB
// does; the comparison is done on squares in 64 bits (three squares of
// |d| <= 2^31 sum below 2^64), so it needs no square root and no float. It
// differs from the engine's trunc(sqrt) only in the fractional unit. `read`
// is bool(address, void* out, unsigned size) and must fail closed. `detail`,
// when given, receives what the walk established (for the capture-frame
// sample lines); fields it did not reach stay zero.
struct Detail { std::uint32_t root = 0; unsigned depth = 0; std::int32_t root_scale = 0; std::int64_t root_reach = 0; std::uint64_t root_dist_sq = 0; };
template<class Read>
inline Outcome root_admission(std::uint32_t node, std::uint32_t light, Read&& read, Detail* detail = nullptr) {
    Detail local; Detail& d = detail ? *detail : local; d = Detail{};
    std::uint32_t cursor = node, parent = 0;
    for (unsigned hop = 0;; ++hop) {
        if (hop == max_hops) return Outcome::chain_too_deep;
        if (!read(cursor + parent_offset, &parent, 4)) return Outcome::chain_unreadable;
        if (!parent) break;
        if (parent == node || parent == cursor) return Outcome::chain_cycle;
        cursor = parent; d.depth = hop + 1; d.root = cursor;
    }
    if (cursor == node) return Outcome::node_is_root;
    std::uint32_t scale = 0, root_pos[3] = {}, light_pos[3] = {}, range = 0;
    if (!read(cursor + scale_offset, &scale, 4) || !read(cursor + position_offset, root_pos, 12)) return Outcome::root_unreadable;
    if (!read(light + range_offset, &range, 4) || !read(light + position_offset, light_pos, 12)) return Outcome::light_unreadable;
    d.root_scale = std::int32_t(scale);
    const std::int64_t reach = std::int64_t(std::int32_t(range)) + std::int64_t(std::int32_t(scale));
    d.root_reach = reach;
    std::uint64_t sum = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const std::int64_t delta = std::int32_t(root_pos[i] - light_pos[i]);
        sum += std::uint64_t(delta * delta);
    }
    d.root_dist_sq = sum;
    if (reach < 0) return Outcome::reach_negative;
    const std::uint64_t r = std::uint64_t(reach);
    return sum <= r * r ? Outcome::admitted : Outcome::root_rejected;
}
// Integer floor(sqrt(v)) for the sample lines only (bit-by-bit, no float).
inline std::uint32_t isqrt64(std::uint64_t v) {
    std::uint64_t result = 0, bit = std::uint64_t(1) << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= result + bit) { v -= result + bit; result = (result >> 1) + bit; }
        else result >>= 1;
        bit >>= 2;
    }
    return std::uint32_t(result);
}
inline const char* outcome_name(Outcome o) {
    static const char* const names[outcome_count] = {"root_admit", "node_is_root", "chain_unreadable", "chain_too_deep", "chain_cycle",
                                                     "root_unreadable", "light_unreadable", "reach_negative", "root_reject"};
    return unsigned(o) < outcome_count ? names[unsigned(o)] : "?";
}

// The six site bytes: JMP rel32 to the detour and one NOP (whole-instruction
// replacement of the JG; nothing branches into the span). Both outcomes enter
// the detour so the per-frame telemetry can count the tests that the native
// per-node rule admits.
inline void encode_site_patch(std::uint32_t site, std::uint32_t detour, unsigned char out[site_length]) {
    const std::uint32_t rel = detour - (site + 5);
    out[0] = 0xe9; std::memcpy(out + 1, &rel, 4); out[5] = 0x90;
}
// The detour, at `at`, with the flags of the engine's TEST EAX,EAX still live
// (the JMP does not alter them):
//    0  0f 8e rel32         JLE  counted       ; the per-node test passed
//    6  50                  PUSH EAX           ; d - range - node scale (the remainder)
//    7  56                  PUSH ESI           ; light node
//    8  ff 75 0c            PUSH [EBP+0xc]     ; submitted node
//   11  e8 rel32            CALL handler       ; int cdecl(node, light, remainder): non-zero = the root admits
//   16  83 c4 0c            ADD  ESP,12
//   19  85 c0               TEST EAX,EAX
//   21  0f 85 rel32         JNZ  admit
//   27  e9 rel32            JMP  reject
//   32  ff 05 abs32         INC  dword [fast_admit]   ; counted: one memory increment, flags dead-out at admit
//   38  e9 rel32            JMP  admit
// EAX/ECX/EDX are dead or scratch at the site; the cdecl handler preserves
// EBX/ESI/EDI/EBP; the caller's ESP-relative locals sit above the pushes.
constexpr unsigned detour_length = 43, detour_counted_offset = 32;
inline void encode_detour(std::uint32_t at, std::uint32_t handler, std::uint32_t admit, std::uint32_t reject, std::uint32_t fast_admit_counter, unsigned char out[detour_length]) {
    auto rel32 = [&](unsigned offset, std::uint32_t target) { const std::uint32_t rel = target - (at + offset + 4); std::memcpy(out + offset, &rel, 4); };
    out[0] = 0x0f; out[1] = 0x8e; rel32(2, at + detour_counted_offset);
    out[6] = 0x50;
    out[7] = 0x56;
    out[8] = 0xff; out[9] = 0x75; out[10] = 0x0c;
    out[11] = 0xe8; rel32(12, handler);
    out[16] = 0x83; out[17] = 0xc4; out[18] = 0x0c;
    out[19] = 0x85; out[20] = 0xc0;
    out[21] = 0x0f; out[22] = 0x85; rel32(23, admit);
    out[27] = 0xe9; rel32(28, reject);
    out[32] = 0xff; out[33] = 0x05; std::memcpy(out + 34, &fast_admit_counter, 4);
    out[38] = 0xe9; rel32(39, admit);
}
}
