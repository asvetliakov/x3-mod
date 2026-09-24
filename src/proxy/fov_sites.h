#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Portable core of the field-of-view option (X3M_FOV; docs/reverse-engineering/
// field-of-view.md section 5): the verified 28-byte window in the cockpit
// registry constructor 0x0041c960, the four-byte immediate write, the reader
// contract, the registry base field, the vertical-degrees -> binary-angle
// conversion and the X3M_FOV parser. No Windows dependency so the host tests
// and the site verifier compile it directly.
//
// The engine's FOV is one binary angle F (65536 = 360 deg) at registry+0x24,
// set to 0x4000 by the constructor and copied every cockpit update (divided by
// the zoom) into the sector/galaxy/dust cameras' +0x298. F is the horizontal
// FOV of the central 4:3 area (view plane H = 0.75 for every display at least
// as wide as 4:3), so a requested vertical v maps to
// F = round(65536/pi * atan(tan(v/2) / 0.75)) and the horizontal grows with
// the aspect (Hor+). The patch replaces the constructor's immediate, so every
// registry creation starts from the user's F; zoom and the in-game FOV menu
// (INS_SetFocus, focus 70..100) still act on it.
//
// 0041c9cc  89 5e 20                 MOV  [ESI+0x20],EBX              <- window
// 0041c9cf  c6 46 19 01              MOV  byte [ESI+0x19],1
// 0041c9d3  88 5e 1a                 MOV  [ESI+0x1a],BL
// 0041c9d6  89 5e 1c                 MOV  [ESI+0x1c],EBX
// 0041c9d9  c7 46 24 00 40 00 00     MOV  dword [ESI+0x24],0x4000     <- site; imm32 at 0041c9dc -> F
// 0041c9e0  89 7c 24 30              MOV  [ESP+0x30],EDI
// 0041c9e4  89 5c 24 2c              MOV  [ESP+0x2c],EBX              ; (window ends at 0041c9e8)
//
// Only the immediate of one `MOV m32, imm32` changes: same length, same
// operands, same instruction boundaries, no register, flag, x87/SSE state or
// LastError touched before or after. ESI = the registry (`mov esi,[esp+0x38]`
// at 0x0041c97b, not written again before the site). The written span
// 0x0041c9dc..0x0041c9df is the upper half of the aligned 8-byte word
// 0x0041c9d8..0x0041c9df ((0x0041c9dc & 7) + 4 == 8), so
// engine_patch::write_code stores it with one lock cmpxchg8b and a racing
// fetch decodes the old or the new immediate, never a mix. No direct branch
// lands inside the window, no raw branch encoding in the image lands on the
// immediate and no dword points into the window (verify_fov_site.py).
namespace x3m::fov::sites {
constexpr std::uintptr_t function_va = 0x0041c960, function_end_va = 0x0041cc14;
constexpr std::uintptr_t window_va = 0x0041c9cc, site_va = 0x0041c9d9, write_va = 0x0041c9dc;
constexpr unsigned window_length = 28, site_offset = 13, site_length = 7, write_offset = 16, write_length = 4;
constexpr unsigned char expected_window[window_length] = {
    0x89,0x5e,0x20, 0xc6,0x46,0x19,0x01, 0x88,0x5e,0x1a, 0x89,0x5e,0x1c,
    0xc7,0x46,0x24,0x00,0x40,0x00,0x00, 0x89,0x7c,0x24,0x30, 0x89,0x5c,0x24,0x2c};
constexpr unsigned char expected_site[site_length] = {0xc7,0x46,0x24,0x00,0x40,0x00,0x00};  // MOV dword [ESI+0x24],0x4000
constexpr unsigned char expected_write[write_length] = {0x00,0x40,0x00,0x00};              // imm32 0x4000
// The reader contract (checked before the write, fail closed): the per-frame
// cockpit update reads the base as `mov edx,[0x00608504]; mov esi,[edx+0x24]`
// and INS_SetFocus stores it as `mov [edx+0x24],ecx`.
constexpr std::uintptr_t reader_va = 0x00421148, setfocus_va = 0x0042dc04;
constexpr unsigned reader_length = 9, setfocus_length = 3;
constexpr unsigned char expected_reader[reader_length] = {0x8b,0x15,0x04,0x85,0x60,0x00, 0x8b,0x72,0x24};
constexpr unsigned char expected_setfocus[setfocus_length] = {0x89,0x4a,0x24};
// The live base: *(*0x00608504 + 0x24).
constexpr std::uintptr_t registry_slot_va = 0x00608504;
constexpr unsigned registry_focus_offset = 0x24;
constexpr std::uint32_t engine_focus = 0x4000;                        // 90 deg of the 4:3 horizontal, 73.74 deg vertical
constexpr std::uint32_t focus_floor = 0x106, focus_ceiling = 0x8000;  // the engine's post-zoom floor; 180 deg
inline bool plausible_focus(std::uint32_t f) { return f >= focus_floor && f <= focus_ceiling; }

// Bounds of the option in vertical degrees: 36 keeps the sector F >= 0x2147,
// the engine's near-plane switch (zn stays 6, which the fog march and the sun
// shadow apply assume); 120 keeps F well below 0x8000.
constexpr double vertical_min = 36.0, vertical_max = 120.0;
constexpr std::uint32_t near_plane_focus = 0x2147;
constexpr double plane_height = 0.75;  // the default view plane H for every display at least as wide as 4:3
inline double focus_exact(double vertical_degrees) {
    const double pi = 3.14159265358979323846;
    return 65536.0 / pi * std::atan(std::tan(vertical_degrees * pi / 360.0) / plane_height);
}
// The binary angle for a vertical FOV in (0, 180) degrees; 0 outside.
inline std::uint32_t focus_for_vertical(double vertical_degrees) {
    if (!std::isfinite(vertical_degrees) || !(vertical_degrees > 0.0) || !(vertical_degrees < 180.0)) return 0;
    return static_cast<std::uint32_t>(std::floor(focus_exact(vertical_degrees) + 0.5));
}
// The vertical FOV in degrees that a binary angle gives at H = 0.75.
inline double vertical_for_focus(std::uint32_t focus) {
    const double pi = 3.14159265358979323846;
    return 360.0 / pi * std::atan(plane_height * std::tan(focus * pi / 65536.0));
}

// X3M_FOV: `game` = the engine's own 0x4000 (nothing patched; also the DLL
// default when the variable is unset or empty); otherwise decimal vertical
// degrees `[+]digits[.digits]` or `.digits` (locale independent), accepted in
// [vertical_min, vertical_max]. A value whose F is 0x4000 (73.74) patches
// nothing either.
enum class Parse : unsigned char { game = 0, degrees = 1, invalid = 2 };
constexpr unsigned setting_capacity = 32;  // 1..31 characters; 32 or more is too_long
template <class Char>
inline Parse parse_setting(const Char* text, double* degrees) {
    if (!text || !*text) return Parse::game;
    {
        const char* word = "game";
        unsigned i = 0;
        for (; word[i] && text[i] == Char(word[i]); ++i) {}
        if (!word[i] && text[i] == Char(0)) return Parse::game;
    }
    const Char* p = text;
    if (*p == Char('+')) ++p;
    double value = 0; unsigned digits = 0;
    for (; *p >= Char('0') && *p <= Char('9'); ++p, ++digits) value = value * 10.0 + double(*p - Char('0'));
    if (*p == Char('.')) {
        double scale = 0.1;
        for (++p; *p >= Char('0') && *p <= Char('9'); ++p, ++digits) { value += double(*p - Char('0')) * scale; scale *= 0.1; }
    }
    if (*p != Char(0) || digits == 0) return Parse::invalid;
    *degrees = value;
    return Parse::degrees;
}
inline bool in_range(double degrees) { return std::isfinite(degrees) && degrees >= vertical_min && degrees <= vertical_max; }
inline void encode(std::uint32_t focus, unsigned char out[write_length]) {
    out[0] = focus & 0xff; out[1] = (focus >> 8) & 0xff; out[2] = (focus >> 16) & 0xff; out[3] = (focus >> 24) & 0xff;
}

// The install decision on the bytes read at the window: nullptr when the
// window is exactly the engine's, else the refusal reason (an already patched
// or otherwise changed window is refused).
inline const char* plan(const unsigned char current[window_length]) {
    return std::memcmp(current, expected_window, window_length) ? "bytes_mismatch" : nullptr;
}
// The write, verify, rollback and restore sequence over the four immediate
// bytes, independent of the memory API so the host test drives it against a
// buffer and production against the engine's code page. Ops provides:
//   bool read(std::uintptr_t at, unsigned char* out, unsigned n);             // bounded, fault-free
//   bool protect(std::uintptr_t at, unsigned n, std::uint32_t protection, std::uint32_t* previous);
//   bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic); // store + flush
//   static constexpr std::uint32_t writable;                                  // the protection that allows the store
// install() returns "ok" or the reason; *live says whether the patched bytes
// may be in place (true after "ok" and after "rollback_failed", so the caller
// keeps the site registered for restore()); *protection is the page
// protection found before the write, which restore() puts back. `window` is
// the window's address; the store goes to window + write_offset. `focus` must
// be plausible and not the engine's 0x4000 (invalid_value otherwise).
template <class Ops>
inline const char* install(Ops& ops, std::uintptr_t window, bool window_open, std::uint32_t focus, bool* live, bool* atomic, std::uint32_t* protection) {
    *live = false; *atomic = false;
    unsigned char current[window_length]{};
    if (!window) return "invalid_site";
    if (!plausible_focus(focus) || focus == engine_focus) return "invalid_value";
    if (!window_open) return "late_claim";
    if (!ops.read(window, current, window_length)) return "unreadable";
    if (const char* refused = plan(current)) return refused;
    unsigned char patched[write_length];
    encode(focus, patched);
    const std::uintptr_t at = window + write_offset;
    if (!ops.protect(at, write_length, Ops::writable, protection)) return "protect_failed";
    *live = true; // ownership published before the mutation, so a failed rollback stays registered
    const bool flushed = ops.write(at, patched, write_length, atomic);
    unsigned char readback[write_length]{};
    const bool verified = flushed && ops.read(at, readback, write_length) && !std::memcmp(readback, patched, write_length);
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
        *live = false; // the engine's bytes are back: nothing is live, whatever the protection calls returned
        return reprotected ? "patch_rolled_back" : "rollback_unprotected"; // the latter: original bytes, page left writable
    }
    return "rollback_failed";
}
// Puts 00 40 00 00 back at a write span (`at` = window + write_offset) that
// install() registered with `focus`, only when it holds that focus (the rule
// of engine_patch::restore). `found` receives the four bytes read before any
// write (*found_read false when unreadable). Returns "restored" when the span
// held our value and reads 00 40 00 00 afterwards with the protection put
// back, or already held 00 40 00 00 (no write); "restore_not_owned" when it
// was unreadable or held other bytes (someone else's, or a failed rollback's):
// nothing is written; "restore_failed" when the protection or the store
// failed. Only "restored" releases the registration.
template <class Ops>
inline const char* restore(Ops& ops, std::uintptr_t at, std::uint32_t focus, std::uint32_t protection, unsigned char found[write_length], bool* found_read) {
    unsigned char ours[write_length];
    encode(focus, ours);
    *found_read = ops.read(at, found, write_length);
    if (*found_read && !std::memcmp(found, expected_write, write_length)) return "restored";
    if (!*found_read || std::memcmp(found, ours, write_length)) return "restore_not_owned";
    std::uint32_t previous = 0, unused = 0;
    bool ignored = false;
    if (!ops.protect(at, write_length, Ops::writable, &previous)) return "restore_failed";
    const bool flushed = ops.write(at, expected_write, write_length, &ignored);
    unsigned char back[write_length]{};
    const bool verified = flushed && ops.read(at, back, write_length) && !std::memcmp(back, expected_write, write_length);
    const bool protected_again = ops.protect(at, write_length, protection, &unused);
    return verified && protected_again ? "restored" : "restore_failed";
}
constexpr bool window_holds_site() {
    for (unsigned i = 0; i < site_length; ++i) if (expected_window[site_offset + i] != expected_site[i]) return false;
    for (unsigned i = 0; i < write_length; ++i) if (expected_window[write_offset + i] != expected_write[i]) return false;
    return true;
}
static_assert(window_va + site_offset == site_va && site_va + 3 == write_va && window_va + write_offset == write_va, "offsets");
static_assert(write_offset + write_length == site_offset + site_length, "the write is the MOV's imm32");
static_assert(window_holds_site(), "the window carries the original site bytes");
static_assert(expected_write[0] == (engine_focus & 0xff) && expected_write[1] == (engine_focus >> 8), "imm32 0x4000");
static_assert((write_va & 7u) + write_length <= 8u, "the imm32 lies inside one aligned 8-byte word (atomic write)");
static_assert(function_va < window_va && window_va + window_length < function_end_va, "inside the registry constructor");
static_assert(expected_reader[2] == (registry_slot_va & 0xff) && expected_reader[3] == ((registry_slot_va >> 8) & 0xff) &&
              expected_reader[4] == ((registry_slot_va >> 16) & 0xff) && expected_reader[8] == registry_focus_offset, "the reader reads registry+0x24");
}
