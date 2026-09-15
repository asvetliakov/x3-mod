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
// 004c27af  0f 8f 40 02 00 00   JG   0x004c29f5        ; reject   <- retargeted: JG detour
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
// is bool(address, void* out, unsigned size) and must fail closed.
template<class Read>
inline Outcome root_admission(std::uint32_t node, std::uint32_t light, Read&& read) {
    std::uint32_t cursor = node, parent = 0;
    for (unsigned hop = 0;; ++hop) {
        if (hop == max_hops) return Outcome::chain_too_deep;
        if (!read(cursor + parent_offset, &parent, 4)) return Outcome::chain_unreadable;
        if (!parent) break;
        if (parent == node || parent == cursor) return Outcome::chain_cycle;
        cursor = parent;
    }
    if (cursor == node) return Outcome::node_is_root;
    std::uint32_t scale = 0, root_pos[3] = {}, light_pos[3] = {}, range = 0;
    if (!read(cursor + scale_offset, &scale, 4) || !read(cursor + position_offset, root_pos, 12)) return Outcome::root_unreadable;
    if (!read(light + range_offset, &range, 4) || !read(light + position_offset, light_pos, 12)) return Outcome::light_unreadable;
    const std::int64_t reach = std::int64_t(std::int32_t(range)) + std::int64_t(std::int32_t(scale));
    if (reach < 0) return Outcome::reach_negative;
    std::uint64_t sum = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const std::int64_t d = std::int32_t(root_pos[i] - light_pos[i]);
        sum += std::uint64_t(d * d);
    }
    const std::uint64_t r = std::uint64_t(reach);
    return sum <= r * r ? Outcome::admitted : Outcome::root_rejected;
}

// The six site bytes: the same JG rel32 opcode with the detour as its target.
// A node that passes the per-node test runs the native instruction stream
// unchanged (the same not-taken branch, no extra instruction); only a rejected
// node enters the detour. Whole-instruction replacement; nothing branches
// into the span.
inline void encode_site_patch(std::uint32_t site, std::uint32_t detour, unsigned char out[site_length]) {
    const std::uint32_t rel = detour - (site + site_length);
    out[0] = 0x0f; out[1] = 0x8f; std::memcpy(out + 2, &rel, 4);
}
// The detour, at `at`, entered only when EAX > 0 (per-node rejection):
//   56                  PUSH ESI           ; light node
//   ff 75 0c            PUSH [EBP+0xc]     ; submitted node
//   e8 rel32            CALL handler       ; int cdecl(node, light): non-zero = the root admits
//   83 c4 08            ADD  ESP,8
//   85 c0               TEST EAX,EAX
//   0f 85 rel32         JNZ  admit
//   e9 rel32            JMP  reject
// EAX/ECX/EDX are dead or scratch at the site; the cdecl handler preserves
// EBX/ESI/EDI/EBP; the caller's ESP-relative locals sit above the pushes.
constexpr unsigned detour_length = 25;
inline void encode_detour(std::uint32_t at, std::uint32_t handler, std::uint32_t admit, std::uint32_t reject, unsigned char out[detour_length]) {
    auto rel32 = [&](unsigned offset, std::uint32_t target) { const std::uint32_t rel = target - (at + offset + 4); std::memcpy(out + offset, &rel, 4); };
    out[0] = 0x56;
    out[1] = 0xff; out[2] = 0x75; out[3] = 0x0c;
    out[4] = 0xe8; rel32(5, handler);
    out[9] = 0x83; out[10] = 0xc4; out[11] = 0x08;
    out[12] = 0x85; out[13] = 0xc0;
    out[14] = 0x0f; out[15] = 0x85; rel32(16, admit);
    out[20] = 0xe9; rel32(21, reject);
}
}
