#pragma once
#include "../engine/fl/inline_patch_fl.h"
#include <cstdint>
#include <cstring>

// Collision damage (X3M_COLLISION_DAMAGE: 1 or unset = vanilla, 0 = no hull or shield damage from collisions, every ship;
// docs/reverse-engineering/collision-damage.md sections 4 and 5, "Implementation"): the two sites per edition, the 13 byte
// windows the installer compares whole before any write, the response routines' provenance hashes (the EXE file hashes
// stay in the verifier and the test: no production gate holds a file hash), the contact patch bytes, the two-site
// install/rollback sequence over an Ops policy, and the flight row's cadence. Pure code: no Windows header, no allocation,
// no floating point. The DLL (src/proxy/collision_damage.cpp) and the fixture
// (verification/probe/collision_damage_fixture.cpp, host and native) include it; the windows equal
// verification/results/collision-damage/verify_collision_damage.py's (verification/analysis/test_collision_damage.py).
//
// Site 1, contact: the join of the response routine's contact block, where both damage values are final (only bodies
// that are neither bullets nor armed missiles pass the block's class gate). `jmp rel32` to the edition's stub plus NOPs
// over the rest of the two displaced whole instructions; the stub zeroes both values and replays them.
// Site 2, planet atmosphere: `test` -> `xor` of the damage register at the gate, so vanilla's own "damage <= 0" branch
// (the following, unchanged `jle`) is taken.
// Windows recorded on X3AP.exe SHA-256 fdbf3418…f8ab and X3FL.exe SHA-256 e7f3aad8…dfbb8 (provenance, never a gate).
namespace x3m::collision_damage::core {
constexpr std::uint32_t preferred_base = 0x00400000;

struct Window {
    const char* name;
    std::uint32_t va; // at the preferred base; compared at base + (va - preferred_base)
    const unsigned char* bytes;
    unsigned length;
};

// ---- AP (X3AP.exe, non-relocatable, base 0x00400000) ----
// The two AP sites, also read by the other AP features' disjointness checks (verify_collide_sites.other_claims scans
// `*site_va` constants); the FL sites are module-relative and stay out of that AP registry.
constexpr std::uint32_t ap_contact_site_va = 0x0045eaf4, ap_planet_site_va = 0x0045dbaf;
constexpr unsigned char ap_contact_gate[46] = {
    0x0f, 0xb7, 0x43, 0x48, 0x66, 0x85, 0xc0, 0x0f, 0x84, 0xa2, 0x05, 0x00, 0x00, 0x66, 0x3d, 0x0a, 0x00, 0x0f, 0x84,
    0x98, 0x05, 0x00, 0x00, 0x0f, 0xb7, 0x46, 0x48, 0x66, 0x85, 0xc0, 0x0f, 0x84, 0x8b, 0x05, 0x00, 0x00, 0x66, 0x3d,
    0x0a, 0x00, 0x0f, 0x84, 0x81, 0x05, 0x00, 0x00};
constexpr unsigned char ap_strength_and_floor[9] = {
    0x8b, 0xc3, 0xe8, 0x92, 0x01, 0xff, 0xff, 0x03, 0xc0};
constexpr unsigned char ap_contact_law[17] = {
    0x8b, 0x44, 0x24, 0x0c, 0x8b, 0x54, 0x24, 0x1c, 0xb9, 0xa0, 0x86, 0x01, 0x00, 0xf7, 0xea, 0xf7, 0xf9};
constexpr unsigned char ap_contact_join_site[11] = {
    0x8b, 0xc3, 0xe8, 0x85, 0x14, 0xfe, 0xff, 0x84, 0xc0, 0x75, 0x0f};
constexpr unsigned char ap_contact_damage_calls[24] = {
    0x8b, 0x44, 0x24, 0x10, 0x6a, 0xff, 0x50, 0x56, 0x53, 0xe8, 0xa3, 0xae, 0xfe, 0xff, 0x6a, 0xff, 0x57, 0x53, 0x56,
    0xe8, 0x99, 0xae, 0xfe, 0xff};
constexpr unsigned char ap_planet_gate_site[12] = {
    0x85, 0xf6, 0x0f, 0x8e, 0xd9, 0x03, 0x00, 0x00, 0x8b, 0x7c, 0x24, 0x1c};
constexpr unsigned char ap_planet_damage_call[14] = {
    0x8b, 0x54, 0x24, 0x24, 0x6a, 0xff, 0x56, 0x52, 0x57, 0xe8, 0x18, 0xbe, 0xfe, 0xff};
constexpr Window ap_windows[] = {
    {"contact_gate", 0x0045e6b0, ap_contact_gate, sizeof ap_contact_gate},
    {"strength_and_floor", 0x0045e9f7, ap_strength_and_floor, sizeof ap_strength_and_floor},
    {"contact_law", 0x0045ea6f, ap_contact_law, sizeof ap_contact_law},
    {"contact_join_site", 0x0045eaf4, ap_contact_join_site, sizeof ap_contact_join_site},
    {"contact_damage_calls", 0x0045ebaf, ap_contact_damage_calls, sizeof ap_contact_damage_calls},
    {"planet_gate_site", 0x0045dbaf, ap_planet_gate_site, sizeof ap_planet_gate_site},
    {"planet_damage_call", 0x0045dc3a, ap_planet_damage_call, sizeof ap_planet_damage_call},
};

// ---- FL (X3FL.exe, relocatable: every address is module base + RVA; no window holds a base relocation) ----
constexpr unsigned char fl_contact_gate[46] = {
    0x0f, 0xb7, 0x46, 0x48, 0x66, 0x85, 0xc0, 0x0f, 0x84, 0xce, 0x05, 0x00, 0x00, 0x66, 0x83, 0xf8, 0x0a, 0x0f, 0x84,
    0xc4, 0x05, 0x00, 0x00, 0x0f, 0xb7, 0x47, 0x48, 0x66, 0x85, 0xc0, 0x0f, 0x84, 0xb7, 0x05, 0x00, 0x00, 0x66, 0x83,
    0xf8, 0x0a, 0x0f, 0x84, 0xad, 0x05, 0x00, 0x00};
constexpr unsigned char fl_contact_law[17] = {
    0x8b, 0x44, 0x24, 0x50, 0x8b, 0x54, 0x24, 0x5c, 0xb9, 0xa0, 0x86, 0x01, 0x00, 0xf7, 0xea, 0xf7, 0xf9};
constexpr unsigned char fl_contact_join_site[17] = {
    0x8b, 0x46, 0x40, 0x25, 0x00, 0x00, 0x0c, 0x00, 0x89, 0x4c, 0x24, 0x20, 0x3d, 0x00, 0x00, 0x04, 0x00};
constexpr unsigned char fl_contact_damage_calls[28] = {
    0x6a, 0xff, 0xff, 0x74, 0x24, 0x14, 0x8b, 0xce, 0x57, 0xe8, 0x55, 0xa5, 0xfe, 0xff, 0x6a, 0xff, 0xff, 0x74, 0x24,
    0x24, 0x8b, 0xcf, 0x56, 0xe8, 0x47, 0xa5, 0xfe, 0xff};
constexpr unsigned char fl_planet_gate_site[15] = {
    0x89, 0x4c, 0x24, 0x24, 0x85, 0xc9, 0x0f, 0x8e, 0xbb, 0x05, 0x00, 0x00, 0x8b, 0x46, 0x40};
constexpr unsigned char fl_planet_damage_call[18] = {
    0x8b, 0x4c, 0x24, 0x24, 0x6a, 0xff, 0x51, 0xff, 0x74, 0x24, 0x28, 0x8b, 0xce, 0xe8, 0xee, 0xb6, 0xfe, 0xff};
constexpr Window fl_windows[] = {
    {"contact_gate", 0x0046859e, fl_contact_gate, sizeof fl_contact_gate},
    {"contact_law", 0x004689cb, fl_contact_law, sizeof fl_contact_law},
    {"contact_join_site", 0x00468a58, fl_contact_join_site, sizeof fl_contact_join_site},
    {"contact_damage_calls", 0x00468b0d, fl_contact_damage_calls, sizeof fl_contact_damage_calls},
    {"planet_gate_site", 0x004678b7, fl_planet_gate_site, sizeof fl_planet_gate_site},
    {"planet_damage_call", 0x00467970, fl_planet_damage_call, sizeof fl_planet_damage_call},
};

struct Edition {
    const char* name;
    std::uint32_t response_va;   // the collision response routine, provenance
    unsigned response_size;
    const char* response_sha256; // verify_collision_damage_output.json, provenance
    const Window* windows;
    unsigned window_count;
    // contact join: the displaced whole instructions, the stub's return (site + length) and, AP only, the leaf the
    // displaced `call rel32` reaches (0x0043ff80, manual control: reads EAX only)
    std::uint32_t contact_va;
    unsigned contact_length;
    const unsigned char* contact_original;
    std::uint32_t leaf_va;       // 0: the displaced instructions call nothing
    // planet atmosphere gate: one opcode byte, the operand byte unchanged; the `jle` after it goes to continue_va
    std::uint32_t planet_va;
    unsigned char planet_original, planet_patched;
    std::uint32_t planet_continue_va;
};
constexpr Edition ap = {"ap",
                        0x0045e130,
                        0x0045eea5 - 0x0045e130,
                        "3384552fe8c327d116cf7040b4ae1b8a967a3b49c44878d6ee72c8dcea2abab5",
                        ap_windows,
                        sizeof ap_windows / sizeof ap_windows[0],
                        ap_contact_site_va,
                        7,
                        ap_contact_join_site,
                        0x0043ff80,
                        ap_planet_site_va,
                        0x85,
                        0x31,
                        0x0045df90};
constexpr Edition fl = {"fl",
                        0x00467f10,
                        0x00468dad - 0x00467f10,
                        "2fa0c2a6e60e2831fd995596b34ee5303141b5e80f572307659d6864d8e746df",
                        fl_windows,
                        sizeof fl_windows / sizeof fl_windows[0],
                        0x00468a58,
                        8,
                        fl_contact_join_site,
                        0,
                        0x004678bb,
                        0x85,
                        0x31,
                        0x00467e7e};
constexpr unsigned max_contact = 8;
constexpr unsigned window_max = 46;

constexpr std::uint32_t rva(std::uint32_t va) { return va - preferred_base; }
constexpr const Window* find(const Edition& e, std::uint32_t va, unsigned length) {
    for (unsigned i = 0; i < e.window_count; ++i)
        if (e.windows[i].va <= va && va + length <= e.windows[i].va + e.windows[i].length) return &e.windows[i];
    return nullptr;
}
// Both patched spans lie inside a compared window with the same bytes; the planet gate's jle (rel32 after the opcode
// pair) lands on continue_va; each window fits the comparator's buffer.
constexpr bool well_formed(const Edition& e) {
    if (e.contact_length < 5 || e.contact_length > max_contact) return false;
    const Window* c = find(e, e.contact_va, e.contact_length + 2); // the span plus two bytes after it (note 4.3)
    if (!c) return false;
    for (unsigned i = 0; i < e.contact_length; ++i)
        if (c->bytes[e.contact_va - c->va + i] != e.contact_original[i]) return false;
    const Window* p = find(e, e.planet_va, 8);
    if (!p) return false;
    const unsigned char* g = p->bytes + (e.planet_va - p->va);
    if (g[0] != e.planet_original || g[2] != 0x0f || g[3] != 0x8e) return false;
    const std::int32_t rel = std::int32_t(std::uint32_t(g[4]) | std::uint32_t(g[5]) << 8 | std::uint32_t(g[6]) << 16 |
                                          std::uint32_t(g[7]) << 24);
    if (e.planet_va + 8 + std::uint32_t(rel) != e.planet_continue_va) return false;
    if (e.planet_original != 0x85 || e.planet_patched != 0x31) return false; // test r,r -> xor r,r (same ModRM)
    for (unsigned i = 0; i < e.window_count; ++i)
        if (e.windows[i].length > window_max) return false;
    return true;
}
static_assert(well_formed(ap) && well_formed(fl), "both sites inside their windows with the patched bytes");

// ---- the comparison (every window whole, before any write) ----
constexpr unsigned shown_bytes = 8;
struct Mismatch {
    const Window* window = nullptr; // the first window that differs or cannot be read; null when all match
    bool readable = false;
    unsigned shown = 0;
    unsigned char got[shown_bytes]{};
};
// Read: bool(std::uintptr_t at, unsigned char* out, unsigned n), a fault-free read of code bytes.
template <class Read>
bool compare_windows(Read&& read, std::uintptr_t base, const Edition& e, Mismatch& out) noexcept {
    out = Mismatch{};
    for (unsigned i = 0; i < e.window_count; ++i) {
        const Window& w = e.windows[i];
        unsigned char actual[window_max];
        const bool readable = read(base + rva(w.va), actual, w.length);
        if (readable && !std::memcmp(actual, w.bytes, w.length)) continue;
        out.window = &w;
        out.readable = readable;
        out.shown = w.length < shown_bytes ? w.length : shown_bytes;
        if (readable) std::memcpy(out.got, actual, out.shown);
        return false;
    }
    return true;
}

// ---- the patch ----
// `jmp rel32` from the site to the stub, then NOPs to the end of the displaced instructions (never executed: the stub
// returns to site + length).
inline void contact_patch(std::uint32_t site, std::uint32_t stub, unsigned length, unsigned char* out) noexcept {
    const std::uint32_t rel = stub - (site + 5);
    out[0] = 0xe9;
    out[1] = static_cast<unsigned char>(rel);
    out[2] = static_cast<unsigned char>(rel >> 8);
    out[3] = static_cast<unsigned char>(rel >> 16);
    out[4] = static_cast<unsigned char>(rel >> 24);
    for (unsigned i = 5; i < length; ++i) out[i] = 0x90;
}
struct Installed {
    bool contact = false, planet = false; // registered (may be live): shutdown() puts these back
    bool contact_atomic = false, planet_atomic = false;
    std::uint32_t contact_protection = 0, planet_protection = 0;
    unsigned char contact_patched[max_contact]{};
};
// Ops: read(at, out, n), protect(at, n, protection, &previous), write(at, bytes, n, &atomic), `writable`
// (inline_patch_fl.h). The caller compared every window and bound the stub's return first. Contact, then planet; a
// planet failure takes the contact site back. "ok" or the reason; `in` says what stays registered.
template <class Ops>
const char* install(Ops& ops, std::uintptr_t base, const Edition& e, std::uint32_t stub, Installed& in) {
    namespace I = x3m::fl::inline_patch_fl;
    in = Installed{};
    const std::uintptr_t contact = base + rva(e.contact_va), planet = base + rva(e.planet_va);
    contact_patch(std::uint32_t(contact), stub, e.contact_length, in.contact_patched);
    bool live = false;
    const char* reason = I::install(ops, contact, e.contact_original, in.contact_patched, e.contact_length, &live,
                                    &in.contact_atomic, &in.contact_protection);
    in.contact = live;
    if (std::strcmp(reason, "ok")) return live ? "contact_rollback_failed" : reason;
    reason = I::install(ops, planet, &e.planet_original, &e.planet_patched, 1, &live, &in.planet_atomic,
                        &in.planet_protection);
    in.planet = live;
    if (!std::strcmp(reason, "ok")) return "ok";
    if (live) return "planet_rollback_failed";
    if (std::strcmp(I::restore(ops, contact, e.contact_original, in.contact_patched, e.contact_length,
                               in.contact_protection),
                    "restored"))
        return "contact_rollback_failed";
    in.contact = false;
    return reason;
}
// Planet, then contact, each only over this feature's bytes. "restored" or the first failure; `in` keeps what is not
// confirmed back.
template <class Ops> const char* restore(Ops& ops, std::uintptr_t base, const Edition& e, Installed& in) {
    namespace I = x3m::fl::inline_patch_fl;
    const char* result = "restored";
    if (in.planet) {
        const char* r = I::restore(ops, base + rva(e.planet_va), &e.planet_original, &e.planet_patched, 1,
                                   in.planet_protection);
        if (!std::strcmp(r, "restored"))
            in.planet = false;
        else
            result = r;
    }
    if (in.contact) {
        const char* r = I::restore(ops, base + rva(e.contact_va), e.contact_original, in.contact_patched,
                                   e.contact_length, in.contact_protection);
        if (!std::strcmp(r, "restored"))
            in.contact = false;
        else if (!std::strcmp(result, "restored"))
            result = r;
    }
    return result;
}

// ---- the flight row (section 5): one row per second at most, only when contacts were suppressed since the last ----
constexpr unsigned long long row_interval_ms = 1000;
constexpr bool row_due(std::uint32_t events, std::uint32_t logged, unsigned long long now_ms,
                       unsigned long long last_ms) {
    return events != logged && (last_ms == 0 || now_ms - last_ms >= row_interval_ms);
}
}
