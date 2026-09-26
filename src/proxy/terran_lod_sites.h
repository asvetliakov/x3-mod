#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the Terran-station LOD patch (docs/reverse-engineering/
// lod-selection.md, "Terran stations and bit 31 of node+0x12c", section 3,
// "Reader"): the verified 17-byte window in the cull/LOD pass 0x0047cfe0, the
// two-byte site, its replacement and the X3M_TERRAN_STATION_LOD parser. No
// Windows dependency so the host tests and the site verifier compile it
// directly.
//
// A parentless node with bit 31 of +0x12c set (only Terran TDocks/TFactories
// roots: constructor 0x00441644, restored verbatim from saves at 0x0047a005)
// sets the distance-branch flag at 0x0047d01e; the flag goes to every child and
// selects the fixed-distance LOD branch 0x0047d36d instead of the screen-size
// loop 0x0047d429. `je` -> `jmp` (same length, same target) skips the flag
// store, so every node takes the size loop; saves and the bit are untouched.
//
// 0047d00b  75 16                         JNE  0x0047d023   ; propagated flag already set
// 0047d00d  39 57 18                      CMP  [EDI+0x18],EDX
// 0047d010  75 11                         JNE  0x0047d023   ; node has a parent
// 0047d012  f7 87 2c 01 00 00 00 00 00 80 TEST dword [EDI+0x12c],0x80000000   <- window
// 0047d01c  74 05                         JE   0x0047d023                     <- site: eb 05 (JMP 0x0047d023)
// 0047d01e  c6 44 24 18 01                MOV  byte [ESP+0x18],1              ; unreachable once patched
// 0047d023  8b 87 2c 01 00 00             MOV  EAX,[EDI+0x12c]                ; flags dead: 0x0047d029 TEST writes them
// first
//
// No branch or data word targets 0x0047d012..0x0047d022; 0x0047d023 is the
// target of 0x0047d00b, 0x0047d010 and 0x0047d01c. No register or flag is
// read differently. The two bytes lie inside one aligned 8-byte word
// (0x0047d018..0x0047d01f), so engine_patch::write_code stores them with one
// lock cmpxchg8b and the instruction boundaries (0x0047d01c, 0x0047d01e) are
// the same before and after: a racing fetch sees the old or the new jump.
namespace x3m::terran_station_lod::sites {
constexpr std::uintptr_t function_va = 0x0047cfe0, function_end_va = 0x0047d552;
constexpr std::uintptr_t window_va = 0x0047d012, site_va = 0x0047d01c, target_va = 0x0047d023;
constexpr unsigned window_length = 17, site_offset = 10, site_length = 2;
constexpr unsigned char expected_window[window_length] = {0xf7, 0x87, 0x2c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                          0x80, 0x74, 0x05, 0xc6, 0x44, 0x24, 0x18, 0x01};
constexpr unsigned char expected_site[site_length] = {0x74, 0x05}; // JE  rel8 +5
constexpr unsigned char patched_site[site_length] = {0xeb, 0x05};  // JMP rel8 +5
constexpr std::uint32_t root_flag = 0x80000000u, flags_offset = 0x12c;

// X3M_TERRAN_STATION_LOD: size = the patch (every node selects by screen size;
// the DLL default when the variable is unset), distance = the engine's
// fixed-distance branch for Terran stations (nothing patched).
enum class Mode : unsigned char { size = 0, distance = 1 };
constexpr Mode default_mode = Mode::size;
constexpr unsigned setting_capacity = 32; // 1..31 characters; 32 or more is too_long
inline const char* mode_name(Mode m) {
    return m == Mode::distance ? "distance" : "size";
}
// Exactly "size" or "distance" (lower case, nothing else).
template <class Char> inline bool parse_mode(const Char* text, Mode* out) {
    if (!text) return false;
    auto equals = [text](const char* word) {
        unsigned i = 0;
        for (; word[i]; ++i)
            if (text[i] != Char(word[i])) return false;
        return text[i] == Char(0);
    };
    if (equals("size")) {
        *out = Mode::size;
        return true;
    }
    if (equals("distance")) {
        *out = Mode::distance;
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
// The write, verify, rollback and restore sequence, independent of the memory
// API so the host test drives it against a buffer and production against the
// engine's code page. Ops provides:
//   bool read(std::uintptr_t at, unsigned char* out, unsigned n);             // bounded, fault-free
//   bool protect(std::uintptr_t at, unsigned n, std::uint32_t protection, std::uint32_t* previous);
//   bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic); // store + flush
//   static constexpr std::uint32_t writable;                                  // the protection that allows the store
// install() returns "ok" or the reason; *live says whether the patched bytes
// may be in place (true after "ok" and after "rollback_failed", so the caller
// keeps the site registered for restore()); *protection is the page protection
// found before the write, which restore() puts back.
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
    const std::uintptr_t site = window + site_offset;
    if (!ops.protect(site, site_length, Ops::writable, protection)) return "protect_failed";
    *live = true; // ownership published before the mutation, so a failed rollback stays registered
    const bool flushed = ops.write(site, patched_site, site_length, atomic);
    unsigned char readback[site_length]{};
    const bool verified = flushed && ops.read(site, readback, site_length) &&
                          !std::memcmp(readback, patched_site, site_length);
    std::uint32_t unused = 0;
    const bool protected_again = ops.protect(site, site_length, *protection, &unused);
    if (verified && protected_again) return "ok";
    // Rollback: the page is still writable when the re-protect failed; otherwise make it so. The
    // outcome is judged by a read-back of the original bytes, not by the protection calls.
    std::uint32_t writable = 0;
    bool reprotected = false, ignored = false;
    if (!protected_again || ops.protect(site, site_length, Ops::writable, &writable)) {
        ops.write(site, expected_site, site_length, &ignored);
        reprotected = ops.protect(site, site_length, *protection, &unused);
    }
    unsigned char back[site_length]{};
    if (ops.read(site, back, site_length) && !std::memcmp(back, expected_site, site_length)) {
        *live = false; // vanilla bytes are back: nothing is live, whatever the protection calls returned
        return reprotected ? "patch_rolled_back" : "rollback_unprotected"; // the latter: original bytes, page left
                                                                           // writable
    }
    return "rollback_failed";
}
// Puts 74 05 back at a site install() registered, whatever it holds now
// (after rollback_failed it may be neither 74 05 nor eb 05). `found` receives
// the two bytes read before any write (*found_read false when unreadable).
// Returns "restored" when the site held eb 05 (or already 74 05, no write) and
// reads 74 05 afterwards with the protection put back; "restore_not_owned"
// when it held other bytes (or was unreadable) and 74 05 was written and read
// back all the same; "restore_failed" otherwise, and the caller keeps the site
// registered.
template <class Ops>
inline const char* restore(Ops& ops, std::uintptr_t site, std::uint32_t protection, unsigned char found[site_length],
                           bool* found_read) {
    *found_read = ops.read(site, found, site_length);
    if (*found_read && !std::memcmp(found, expected_site, site_length)) return "restored";
    const bool owned = *found_read && !std::memcmp(found, patched_site, site_length);
    std::uint32_t previous = 0, unused = 0;
    bool ignored = false;
    if (!ops.protect(site, site_length, Ops::writable, &previous)) return "restore_failed";
    const bool flushed = ops.write(site, expected_site, site_length, &ignored);
    unsigned char back[site_length]{};
    const bool verified = flushed && ops.read(site, back, site_length) &&
                          !std::memcmp(back, expected_site, site_length);
    const bool protected_again = ops.protect(site, site_length, protection, &unused);
    if (!verified || !protected_again) return "restore_failed";
    return owned ? "restored" : "restore_not_owned";
}
constexpr bool window_holds_site() {
    for (unsigned i = 0; i < site_length; ++i)
        if (expected_window[site_offset + i] != expected_site[i]) return false;
    return true;
}
static_assert(window_va + site_offset == site_va, "site offset");
static_assert(window_holds_site(), "the window carries the original site bytes");
static_assert(site_va + site_length + expected_site[1] == target_va && patched_site[1] == expected_site[1],
              "same rel8 target");
static_assert((site_va & 7u) + site_length <= 8u, "the site lies inside one aligned 8-byte word (atomic write)");
static_assert(site_va + site_length + 5 == window_va + window_length, "the skipped flag store ends the window");
static_assert(function_va < window_va && target_va < function_end_va, "inside the cull/LOD pass");
}
