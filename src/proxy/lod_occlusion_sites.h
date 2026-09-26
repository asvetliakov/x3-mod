#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the LOD occlusion patch (docs/reverse-engineering/
// texture-lookup.md section 12): the verified 31-byte window in the material
// submission 0x004c0150, the four-byte write, its replacement and the
// X3M_LOD_OCCLUSION parser. No Windows dependency so the host tests and the
// site verifier compile it directly.
//
// For a node whose final LOD index node+0x14c is not 0 the engine binds the
// global NONE_OCCL_DECAL placeholder (the dword at 00606f74, its +0x34) to
// t_OcclusionTexture instead of the material's occlusion map. The patch sets
// the jne's rel32 to 0: `jne 0x004c34fd`, the next instruction, so both
// outcomes continue on the LOD-0 path and every record binds its material's
// map exactly as record 0 does.
//
// 004c34e7  8b 4d 0c                 MOV  ECX,[EBP+0xc]            ; node      <- window
// 004c34ea  83 b9 4c 01 00 00 00     CMP  dword [ECX+0x14c],0      ; LOD index
// 004c34f1  8b 15 74 6f 60 00        MOV  EDX,[00606f74]           ; placeholder entry
// 004c34f7  0f 85 c9 00 00 00        JNE  0x004c35c6               <- site; rel32 at 004c34f9 -> 00 00 00 00 (JNE
// 0x004c34fd) 004c34fd  83 7c 24 74 00           CMP  dword [ESP+0x74],0       ; occlusion id; writes every flag the
// cmp above set 004c3502  89 54 24 18              MOV  [ESP+0x18],EDX           ; placeholder kept as the fallback
// 004c3506  0f 8c ca 00 00 00        JL   0x004c35d6               ; (after the window)
//
// The placeholder bind 0x004c35c6 is reached only from this jne. A patched
// arrival at 0x004c34fd (LOD index != 0) executed the same four instructions as
// an unpatched LOD-0 arrival, so every register and the stack are the same;
// only the flags the gate's cmp wrote (ZF, and SF/PF by value) and node+0x14c
// differ. Those flags are dead (the cmp at 0x004c34fd rewrites them all), and
// no instruction of the LOD-0 path reads +0x14c: the path is every instruction
// reachable from 0x004c34fd before the join at 0x004c35d6, which includes the
// placeholder-bind tail 0x004c35c9..0x004c35d3 (the failed-bind fallback); its
// callees 0x004f5280 and 0x004b9ed0 are the ones LOD 0 already calls. No direct branch in
// the function targets the window's interior, no raw branch encoding in the
// image lands on 0x004c34f8..0x004c34fc and no dword points into the window
// (verify_lod_occlusion_site.py). The written span 0x004c34f9..0x004c34fc lies
// inside the aligned 8-byte word 0x004c34f8..0x004c34ff, so
// engine_patch::write_code stores it with one lock cmpxchg8b; the opcode bytes
// 0f 85 and every instruction boundary stay as they are, so a racing fetch
// decodes either jne and either one leaves the flags and registers alone.
namespace x3m::lod_occlusion::sites {
constexpr std::uintptr_t function_va = 0x004c0150, function_end_va = 0x004c40fc;
constexpr std::uintptr_t window_va = 0x004c34e7, site_va = 0x004c34f7, write_va = 0x004c34f9;
constexpr std::uintptr_t lod0_va = 0x004c34fd, placeholder_va = 0x004c35c6;
constexpr unsigned window_length = 31, site_offset = 16, site_length = 6, write_offset = 18, write_length = 4;
constexpr unsigned char expected_window[window_length] = {
    0x8b, 0x4d, 0x0c, 0x83, 0xb9, 0x4c, 0x01, 0x00, 0x00, 0x00, 0x8b, 0x15, 0x74, 0x6f, 0x60, 0x00,
    0x0f, 0x85, 0xc9, 0x00, 0x00, 0x00, 0x83, 0x7c, 0x24, 0x74, 0x00, 0x89, 0x54, 0x24, 0x18};
constexpr unsigned char expected_site[site_length] = {0x0f, 0x85, 0xc9, 0x00, 0x00, 0x00}; // JNE 0x004c35c6
constexpr unsigned char expected_write[write_length] = {0xc9, 0x00, 0x00, 0x00};           // rel32 +0xc9
constexpr unsigned char patched_write[write_length] = {0x00, 0x00, 0x00, 0x00};            // rel32 0: JNE 0x004c34fd
constexpr std::uint32_t lod_offset = 0x14c;

// X3M_LOD_OCCLUSION: record0 = the engine's bytes (only LOD record 0 binds the
// material's occlusion map; the DLL default when the variable is unset), all =
// the patch (every record binds it).
enum class Mode : unsigned char { record0 = 0, all = 1 };
constexpr Mode default_mode = Mode::record0;
constexpr unsigned setting_capacity = 32; // 1..31 characters; 32 or more is too_long
inline const char* mode_name(Mode m) {
    return m == Mode::all ? "all" : "record0";
}
// Exactly "record0" or "all" (lower case, nothing else).
template <class Char> inline bool parse_mode(const Char* text, Mode* out) {
    if (!text) return false;
    auto equals = [text](const char* word) {
        unsigned i = 0;
        for (; word[i]; ++i)
            if (text[i] != Char(word[i])) return false;
        return text[i] == Char(0);
    };
    if (equals("record0")) {
        *out = Mode::record0;
        return true;
    }
    if (equals("all")) {
        *out = Mode::all;
        return true;
    }
    return false;
}
// The install decision on the bytes read at the window: nullptr when the
// window is exactly the engine's, else the refusal reason (an already patched
// or otherwise changed window is refused).
inline const char* plan(const unsigned char current[window_length]) {
    return std::memcmp(current, expected_window, window_length) ? "bytes_mismatch" : nullptr;
}
// The write, verify, rollback and restore sequence over the four rel32 bytes,
// independent of the memory API so the host test drives it against a buffer
// and production against the engine's code page. Ops provides:
//   bool read(std::uintptr_t at, unsigned char* out, unsigned n);             // bounded, fault-free
//   bool protect(std::uintptr_t at, unsigned n, std::uint32_t protection, std::uint32_t* previous);
//   bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic); // store + flush
//   static constexpr std::uint32_t writable;                                  // the protection that allows the store
// install() returns "ok" or the reason; *live says whether the patched bytes
// may be in place (true after "ok" and after "rollback_failed", so the caller
// keeps the site registered for restore()); *protection is the page protection
// found before the write, which restore() puts back. `window` is the window's
// address; the store goes to window + write_offset.
template <class Ops>
inline const char* install(Ops& ops, std::uintptr_t window, bool window_open, bool* live, bool* atomic,
                           std::uint32_t* protection) {
    *live = false;
    *atomic = false;
    unsigned char current[window_length]{};
    if (!window) return "invalid_site";
    if (!window_open) return "late_claim";
    if (!ops.read(window, current, window_length)) return "unreadable";
    if (const char* refused = plan(current)) return refused;
    const std::uintptr_t at = window + write_offset;
    if (!ops.protect(at, write_length, Ops::writable, protection)) return "protect_failed";
    *live = true; // ownership published before the mutation, so a failed rollback stays registered
    const bool flushed = ops.write(at, patched_write, write_length, atomic);
    unsigned char readback[write_length]{};
    const bool verified = flushed && ops.read(at, readback, write_length) &&
                          !std::memcmp(readback, patched_write, write_length);
    std::uint32_t unused = 0;
    const bool protected_again = ops.protect(at, write_length, *protection, &unused);
    if (verified && protected_again) return "ok";
    // Rollback: the page is still writable when the re-protect failed; otherwise make it so. The
    // outcome is judged by a read-back of the original bytes, not by the protection calls.
    std::uint32_t writable = 0;
    bool reprotected = false, ignored = false;
    if (!protected_again || ops.protect(at, write_length, Ops::writable, &writable)) {
        ops.write(at, expected_write, write_length, &ignored);
        reprotected = ops.protect(at, write_length, *protection, &unused);
    }
    unsigned char back[write_length]{};
    if (ops.read(at, back, write_length) && !std::memcmp(back, expected_write, write_length)) {
        *live = false; // vanilla bytes are back: nothing is live, whatever the protection calls returned
        return reprotected ? "patch_rolled_back" : "rollback_unprotected"; // the latter: original bytes, page left
                                                                           // writable
    }
    return "rollback_failed";
}
// Puts c9 00 00 00 back at a write span (`at` = window + write_offset) that
// install() registered, only when it holds the patched 00 00 00 00 (the rule
// of engine_patch::restore). `found` receives the four bytes read before any
// write (*found_read false when unreadable). Returns "restored" when the span
// held 00 00 00 00 and reads c9 00 00 00 afterwards with the protection put
// back, or already held c9 00 00 00 (no write); "restore_not_owned" when it
// was unreadable or held other bytes (someone else's, or a failed rollback's):
// nothing is written; "restore_failed" when the protection or the store
// failed. Only "restored" releases the registration.
template <class Ops>
inline const char* restore(Ops& ops, std::uintptr_t at, std::uint32_t protection, unsigned char found[write_length],
                           bool* found_read) {
    *found_read = ops.read(at, found, write_length);
    if (*found_read && !std::memcmp(found, expected_write, write_length)) return "restored";
    if (!*found_read || std::memcmp(found, patched_write, write_length)) return "restore_not_owned";
    std::uint32_t previous = 0, unused = 0;
    bool ignored = false;
    if (!ops.protect(at, write_length, Ops::writable, &previous)) return "restore_failed";
    const bool flushed = ops.write(at, expected_write, write_length, &ignored);
    unsigned char back[write_length]{};
    const bool verified = flushed && ops.read(at, back, write_length) &&
                          !std::memcmp(back, expected_write, write_length);
    const bool protected_again = ops.protect(at, write_length, protection, &unused);
    return verified && protected_again ? "restored" : "restore_failed";
}
constexpr bool window_holds_site() {
    for (unsigned i = 0; i < site_length; ++i)
        if (expected_window[site_offset + i] != expected_site[i]) return false;
    for (unsigned i = 0; i < write_length; ++i)
        if (expected_window[write_offset + i] != expected_write[i]) return false;
    return true;
}
static_assert(window_va + site_offset == site_va && site_va + 2 == write_va && window_va + write_offset == write_va,
              "offsets");
static_assert(write_offset + write_length == site_offset + site_length, "the write is the jne's rel32");
static_assert(window_holds_site(), "the window carries the original site bytes");
static_assert(site_va + site_length == lod0_va && lod0_va + expected_write[0] == placeholder_va,
              "jne rel32 +0xc9 reaches the placeholder bind");
static_assert(patched_write[0] == 0 && patched_write[1] == 0 && patched_write[2] == 0 && patched_write[3] == 0,
              "rel32 0: the next instruction");
static_assert((write_va & 7u) + write_length <= 8u, "the rel32 lies inside one aligned 8-byte word (atomic write)");
static_assert(function_va < window_va && placeholder_va < function_end_va, "inside the material submission");
}
