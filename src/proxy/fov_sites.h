#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Portable core of the field-of-view option (X3M_FOV; docs/reverse-engineering/
// field-of-view.md sections 5 and 7.3): the verified 28-byte window in the
// cockpit registry constructor 0x0041c960 and its four-byte immediate write,
// the verified INS_SetFocus case body and the remap stub claimed at
// 0x0042dbf8, the reader contract, the registry base field, the remap
// F -> F' and the X3M_FOV parser. No Windows dependency so the host tests and
// the site verifier compile it directly.
//
// The engine's FOV is one binary angle F (65536 = 360 deg) at registry+0x24,
// set to 0x4000 by the constructor and copied every cockpit update (divided by
// the zoom) into the sector/galaxy/dust cameras' +0x298. F is the horizontal
// FOV of the central 4:3 area (view plane H = 0.75 for every display at least
// as wide as 4:3). The game's own number N (script default 90, in-game menu
// 70..100, passed as F = (N << 16) / 360) is reinterpreted as "N degrees
// horizontal on 16:9": the engine gets F' with tan(F'/2) = 0.75 * tan(F/2),
// which is exactly a 16:9 horizontal of N and a vertical of
// 2*atan(0.5625*tan(N/2)); wider displays get more width (Hor+). Two write
// sites carry it: the constructor's immediate (F'(N_default), N_default the
// launcher's --fov) and the INS_SetFocus store, whose incoming F is remapped
// through an 81-entry table (N = 50..130: the menu's 70..100 plus room for a
// mod-widened menu or script values outside it) by a generated stub. A third
// site, the registry serializer's load store (section 7.4.4), replaces a
// savegame's vanilla-unit focus with the same table's value on load.
//
// 0041c9cc  89 5e 20                 MOV  [ESI+0x20],EBX              <- window
// 0041c9cf  c6 46 19 01              MOV  byte [ESI+0x19],1
// 0041c9d3  88 5e 1a                 MOV  [ESI+0x1a],BL
// 0041c9d6  89 5e 1c                 MOV  [ESI+0x1c],EBX
// 0041c9d9  c7 46 24 00 40 00 00     MOV  dword [ESI+0x24],0x4000     <- site; imm32 at 0041c9dc -> F'
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
//
// INS_SetFocus = script dispatcher 0x0042d340 case 0x21 (jump table
// 0x0042f064), entered only at 0x0042dbed:
//
// 0042dbed  8b 45 18                 MOV  EAX,[EBP+0x18]      ; marshalled args  <- case window
// 0042dbf0  8b 48 01                 MOV  ECX,[EAX+1]         ; F from the script, no clamp
// 0042dbf3  a1 e4 85 60 00           MOV  EAX,[0x006085e4]    ; VM (live: pushed at 0042dc00)
// 0042dbf8  8b 15 04 85 60 00        MOV  EDX,[0x00608504]    <- claimed: jmp (5 bytes, one aligned qword)
// 0042dbfe  6a 00                    PUSH 0
// 0042dc00  50                       PUSH EAX
// 0042dc01  8b 45 0c                 MOV  EAX,[EBP+0xc]
// 0042dc04  89 4a 24                 MOV  [EDX+0x24],ECX      ; the base
// 0042dc07  e8 e4 6b 07 00           CALL 0x004a47f0          ; (case window ends at 0042dc0c: JMP 0x0042f04c)
// 004a47f0  56 8b f0 57 8d 7e 28 66 c7 46 20 01 00 80 3f 08 72 07 8b cf e8 37 3a 00 00 8b 4c 24 0c
//           callee prefix: CMP writes EFLAGS before the JB reads them, and ECX is written on both paths
//           (8b cf / 8b 4c 24 0c) before any read, so EFLAGS and ECX are dead after 0x0042dc04.
//
// The stub runs in place of the displaced MOV EDX: EDX is dead at entry (the
// tail reloads it), EFLAGS are dead (PUSH/MOV/CALL follow, and the callee
// writes them first), EAX, EBX, ESI, EDI, EBP and ESP are not touched; ECX is
// the output. No FPU/SSE, no call, no stack or memory write; the table sits in
// the same executable arena block and is immutable after install:
//
//   cmp ecx,remap_focus_min ; jb done ; cmp ecx,remap_focus_max ; ja done   ; outside N 50..130: pass through
//   imul edx,ecx,360 ; add edx,0x8000 ; shr edx,16                          ; N = round(F*360/65536)
//   movzx ecx,word [edx*2 + table - 2*50]                                   ; F' = table[N - 50]
//   done: jmp [slot]                                                        ; -> tail: MOV EDX,[0x00608504]; JMP 0x0042dbfe
//
// The script passes F = (N << 16) / 360 (truncated); rounding F*360/65536
// recovers N exactly for every N (verify_fov_site.py), so the table holds the
// remap of that exact F. Any other F in [remap_focus_min, remap_focus_max]
// maps to the nearest integer N's value; values outside pass through unchanged.
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

// Bounds of the option in the game's degrees N: the in-game menu's own clamps
// SG_MIN_FOV / SG_MAX_FOV. F'(70) = 0x2768 stays above 0x2147, the engine's
// near-plane switch (zn stays 6, which the fog march and the sun shadow apply
// assume).
constexpr double setting_min = 70.0, setting_max = 100.0, setting_default = 90.0;
constexpr std::uint32_t near_plane_focus = 0x2147;
constexpr double plane_height = 0.75;  // the default view plane H for every display at least as wide as 4:3
// The remap F -> F' with tan(F'/2) = H * tan(F/2), unrounded; 0 outside (0, 0x8000).
inline double remap_exact(std::uint32_t focus) {
    if (!focus || focus >= 0x8000u) return 0.0;
    const double pi = 3.14159265358979323846;
    return 65536.0 / pi * std::atan(plane_height * std::tan(focus * pi / 65536.0));
}
// The same, rounded; 0 outside (0, 0x8000).
inline std::uint32_t remap_focus(std::uint32_t focus) {
    if (!focus || focus >= 0x8000u) return 0;
    return static_cast<std::uint32_t>(std::floor(remap_exact(focus) + 0.5));
}
// The script's F for N degrees: (N << 16) / 360, truncated as SetFocus does; decimals truncate the same way.
inline std::uint32_t game_focus(double degrees) {
    if (!std::isfinite(degrees) || !(degrees > 0.0) || !(degrees < 180.0)) return 0;
    return static_cast<std::uint32_t>(std::floor(degrees * 65536.0 / 360.0));
}
// F' for the game's N degrees (the INS_SetFocus table's value for an integer N).
inline std::uint32_t focus_for_degrees(double degrees) { return remap_focus(game_focus(degrees)); }
// Whether `focus` is a vanilla-unit value (M << 16) / 360, M 0..180: what the load stub's exact-match
// rule reads as the script's number (it remaps the ones of M 50..130).
inline bool vanilla_focus(std::uint32_t focus) {
    for (std::uint32_t m = 0; m <= 180; ++m) if (((m << 16) / 360u) == focus) return true;
    return false;
}
// The constructor's immediate for the script focus g = game_focus(--fov): F'(g), moved by one unit when
// it equals a vanilla value, so a savegame written by that session (which stores it unchanged) loads as
// saved instead of being remapped a second time. No integer N collides (every F'(N) is at least one
// unit from every vanilla value); a decimal --fov can: 29 of the 5,462 g in game_focus(70) ..
// game_focus(100) (every g the launcher's four decimals reach), e.g. 78.5 -> 0x2ccc = vanilla N 63
// (verify_fov_site.py). Both neighbours are one unit from the colliding value and at least 181 from any
// other vanilla value, so the side is chosen by the unrounded F': the unit towards it, which keeps
// the error below one unit (1/65536 of a turn) and never lands on another vanilla value.
inline std::uint32_t constructor_focus_for(std::uint32_t g) {
    const std::uint32_t f = remap_focus(g);
    if (!f || !vanilla_focus(f)) return f;
    return remap_exact(g) >= double(f) ? f + 1u : f - 1u;
}
inline std::uint32_t constructor_focus(double degrees) { return constructor_focus_for(game_focus(degrees)); }
// The vertical FOV in degrees that a binary angle gives at H = 0.75.
inline double vertical_for_focus(std::uint32_t focus) {
    const double pi = 3.14159265358979323846;
    return 360.0 / pi * std::atan(plane_height * std::tan(focus * pi / 65536.0));
}

// ---- INS_SetFocus: the case window, the claimed MOV EDX, the callee prefix, the remap stub ----
constexpr std::uintptr_t setfocus_case_va = 0x0042dbed, setfocus_site_va = 0x0042dbf8, setfocus_return_va = 0x0042dbfe;
constexpr std::uintptr_t setfocus_callee_va = 0x004a47f0;
constexpr unsigned setfocus_case_length = 31, setfocus_site_offset = 11, setfocus_site_length = 6, setfocus_callee_length = 29;
constexpr unsigned char expected_setfocus_case[setfocus_case_length] = {
    0x8b,0x45,0x18, 0x8b,0x48,0x01, 0xa1,0xe4,0x85,0x60,0x00, 0x8b,0x15,0x04,0x85,0x60,0x00,
    0x6a,0x00, 0x50, 0x8b,0x45,0x0c, 0x89,0x4a,0x24, 0xe8,0xe4,0x6b,0x07,0x00};
constexpr unsigned char expected_setfocus_site[setfocus_site_length] = {0x8b,0x15,0x04,0x85,0x60,0x00};  // MOV EDX,[0x00608504]
constexpr unsigned char expected_setfocus_callee[setfocus_callee_length] = {
    0x56, 0x8b,0xf0, 0x57, 0x8d,0x7e,0x28, 0x66,0xc7,0x46,0x20,0x01,0x00, 0x80,0x3f,0x08, 0x72,0x07, 0x8b,0xcf,
    0xe8,0x37,0x3a,0x00,0x00, 0x8b,0x4c,0x24,0x0c};
// The table: N = remap_first .. remap_first + remap_count - 1.
constexpr unsigned remap_first = 50, remap_count = 81;
// The F range whose rounded N lies in the table: round(F*360/65536) = (F*360 + 0x8000) >> 16.
constexpr std::uint32_t remap_focus_min = ((remap_first << 16) - 0x8000u + 359u) / 360u;                  // 0x2334
constexpr std::uint32_t remap_focus_max = (((remap_first + remap_count) << 16) - 0x8000u - 1u) / 360u;   // 0x5ccc
constexpr unsigned remap_index(std::uint32_t focus) { return (focus * 360u + 0x8000u) >> 16; }
inline void build_remap_table(std::uint16_t out[remap_count]) {
    for (unsigned i = 0; i < remap_count; ++i) out[i] = static_cast<std::uint16_t>(remap_focus(((remap_first + i) << 16) / 360u));
}
// What the stub computes, for the host tests: the table value for F in range, else F unchanged.
inline std::uint32_t remap_lookup(std::uint32_t focus, const std::uint16_t table[remap_count]) {
    if (focus < remap_focus_min || focus > remap_focus_max) return focus;
    return table[remap_index(focus) - remap_first];
}
constexpr unsigned stub_length = 45, stub_done = 39;
// The stub bytes (position independent apart from the two absolute operands): `table` is the
// address of the 81 uint16 entries, `slot` the 4-aligned continuation word (the previous chain head).
inline void encode_setfocus_stub(std::uint32_t table, std::uint32_t slot, unsigned char out[stub_length]) {
    const unsigned char code[stub_length] = {
        0x81,0xf9, 0,0,0,0,              //  0 cmp ecx,remap_focus_min
        0x72, stub_done - 8,             //  6 jb done
        0x81,0xf9, 0,0,0,0,              //  8 cmp ecx,remap_focus_max
        0x77, stub_done - 16,            // 14 ja done
        0x69,0xd1, 0x68,0x01,0x00,0x00,  // 16 imul edx,ecx,360
        0x81,0xc2, 0x00,0x80,0x00,0x00,  // 22 add edx,0x8000
        0xc1,0xea, 0x10,                 // 28 shr edx,16
        0x0f,0xb7,0x0c,0x55, 0,0,0,0,    // 31 movzx ecx,word [edx*2 + disp32]
        0xff,0x25, 0,0,0,0};             // 39 done: jmp [slot]
    std::memcpy(out, code, stub_length);
    const std::uint32_t operands[4][2] = {{2, remap_focus_min}, {10, remap_focus_max}, {35, table - 2u * remap_first}, {41, slot}};
    for (const auto& o : operands)
        for (unsigned k = 0; k < 4; ++k) out[o[0] + k] = static_cast<unsigned char>((o[1] >> (8 * k)) & 0xff);
}

// ---- The registry serializer's load store (section 7.4.4): the savegame's focus, remapped once ----
//
// The serializer 0x0041c6e0 (callers 0x0041f684 save, 0x0041f790 load) stores
// the saved focus into the fresh registry after the patched constructor ran:
//
// 0041c8b4  e8 67 cb 0c 00           CALL 0x004e9420          ; big-endian read  <- load window
// 0041c8b9  89 45 20                 MOV  [EBP+0x20],EAX
// 0041c8bc  e8 5f cb 0c 00           CALL 0x004e9420          ; the saved focus in EAX
// 0041c8c1  89 45 24                 MOV  [EBP+0x24],EAX      <- claimed (six bytes, jmp in the qword 0x0041c8c0)
// 0041c8c4  5e                       POP  ESI
// 0041c8c5  b0 01                    MOV  AL,1
// 0041c8c7  5d                       POP  EBP
// 0041c8c8  c2 08 00                 RET  8                   ; (window ends at 0041c8cb)
// 0041f790  e8 4b cf ff ff 84 c0 74 da   the load caller: CALL 0x0041c6e0; TEST AL,AL; JE (reads AL only)
//
// The stub runs in place of the displaced store with the saved focus in EAX:
// EDX is scratch (set by 0x004e9420, not read after it by 0x0041c6e0, and the
// caller's next callee 0x0048cdc0 writes it first), EFLAGS are dead
// (pop/mov/pop/ret, then TEST AL,AL), EBX, ECX, ESI, EDI, EBP and ESP are not
// touched; EAX is the output (the tail then sets AL = 1 and the callers read
// only AL). No FPU/SSE, no call, no stack or memory write. The rule is an exact
// match, not the INS_SetFocus stub's nearest N: only a vanilla-unit value
// (N << 16) / 360, N 50..130, is replaced with F'(N) from the INS_SetFocus
// table (whose N = 90 slot is F'(90): a savegame's 90 means 90, as the menu's
// does, whatever --fov the launcher carries); remapped values written by a
// patched session and anything else pass through. No F'(N) equals any
// (M << 16) / 360, M 0..180 (verify_fov_site.py), so the two kinds of savegame
// never collide:
//
//   cmp eax,remap_focus_min ; jb done ; cmp eax,remap_focus_max ; ja done
//   imul edx,eax,360 ; add edx,0x8000 ; shr edx,16                          ; N = round(F*360/65536)
//   cmp ax,word [edx*2 + vanilla - 2*50] ; jne done                         ; only the exact (N<<16)/360
//   movzx eax,word [edx*2 + table - 2*50]                                   ; F'(N)
//   done: jmp [slot]                         ; -> tail: MOV [EBP+0x24],EAX; POP ESI; MOV AL,1; JMP 0x0041c8c7
constexpr std::uintptr_t load_window_va = 0x0041c8b4, load_site_va = 0x0041c8c1, load_return_va = 0x0041c8c7;
constexpr std::uintptr_t load_caller_va = 0x0041f790, load_function_va = 0x0041c6e0;
constexpr unsigned load_window_length = 23, load_site_offset = 13, load_site_length = 6, load_caller_length = 9;
constexpr unsigned char expected_load_window[load_window_length] = {
    0xe8,0x67,0xcb,0x0c,0x00, 0x89,0x45,0x20, 0xe8,0x5f,0xcb,0x0c,0x00,
    0x89,0x45,0x24, 0x5e, 0xb0,0x01, 0x5d, 0xc2,0x08,0x00};
constexpr unsigned char expected_load_site[load_site_length] = {0x89,0x45,0x24, 0x5e, 0xb0,0x01};  // MOV [EBP+0x24],EAX; POP ESI; MOV AL,1
constexpr unsigned char expected_load_caller[load_caller_length] = {0xe8,0x4b,0xcf,0xff,0xff, 0x84,0xc0, 0x74,0xda};
// The vanilla units the exact-match rule accepts: (N << 16) / 360 for N = remap_first ..
inline void build_vanilla_table(std::uint16_t out[remap_count]) {
    for (unsigned i = 0; i < remap_count; ++i) out[i] = static_cast<std::uint16_t>(((remap_first + i) << 16) / 360u);
}
// What the load stub stores, for the host tests: F'(N) for the exact vanilla F of N in the table, else F unchanged.
inline std::uint32_t load_lookup(std::uint32_t focus, const std::uint16_t vanilla[remap_count], const std::uint16_t table[remap_count]) {
    if (focus < remap_focus_min || focus > remap_focus_max) return focus;
    const unsigned i = remap_index(focus) - remap_first;
    return focus == vanilla[i] ? table[i] : focus;
}
constexpr unsigned load_stub_length = 53, load_stub_done = 47;
// The load stub bytes: `vanilla` and `table` the addresses of the two 81-entry uint16 tables,
// `slot` the 4-aligned continuation word.
inline void encode_load_stub(std::uint32_t vanilla, std::uint32_t table, std::uint32_t slot, unsigned char out[load_stub_length]) {
    const unsigned char code[load_stub_length] = {
        0x3d, 0,0,0,0,                   //  0 cmp eax,remap_focus_min
        0x72, load_stub_done - 7,        //  5 jb done
        0x3d, 0,0,0,0,                   //  7 cmp eax,remap_focus_max
        0x77, load_stub_done - 14,       // 12 ja done
        0x69,0xd0, 0x68,0x01,0x00,0x00,  // 14 imul edx,eax,360
        0x81,0xc2, 0x00,0x80,0x00,0x00,  // 20 add edx,0x8000
        0xc1,0xea, 0x10,                 // 26 shr edx,16
        0x66,0x3b,0x04,0x55, 0,0,0,0,    // 29 cmp ax,word [edx*2 + disp32]
        0x75, load_stub_done - 39,       // 37 jne done
        0x0f,0xb7,0x04,0x55, 0,0,0,0,    // 39 movzx eax,word [edx*2 + disp32]
        0xff,0x25, 0,0,0,0};             // 47 done: jmp [slot]
    std::memcpy(out, code, load_stub_length);
    const std::uint32_t operands[5][2] = {{1, remap_focus_min}, {8, remap_focus_max}, {33, vanilla - 2u * remap_first},
                                          {43, table - 2u * remap_first}, {49, slot}};
    for (const auto& o : operands)
        for (unsigned k = 0; k < 4; ++k) out[o[0] + k] = static_cast<unsigned char>((o[1] >> (8 * k)) & 0xff);
}

// X3M_FOV: `game` = the engine's own model (nothing patched; also the DLL
// default when the variable is unset or empty); otherwise the game's degrees N
// `[+]digits[.digits]` or `.digits` (locale independent), accepted in
// [setting_min, setting_max].
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
inline bool in_range(double degrees) { return std::isfinite(degrees) && degrees >= setting_min && degrees <= setting_max; }
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
static_assert(setfocus_case_va + setfocus_site_offset == setfocus_site_va && setfocus_site_va + setfocus_site_length == setfocus_return_va, "the claimed MOV EDX");
static_assert(setfocus_site_va + 12 == setfocus_va, "the store follows the claimed MOV and the two pushes");
static_assert(setfocus_case_va + setfocus_case_length + 0x00076be4u == setfocus_callee_va, "the call rel32 at the window's end reaches the callee");
static_assert((setfocus_site_va & 7u) + 5u <= 8u, "the five jmp bytes lie in one aligned 8-byte word (atomic write)");
static_assert(expected_setfocus_site[2] == (registry_slot_va & 0xff) && expected_setfocus_site[3] == ((registry_slot_va >> 8) & 0xff) &&
              expected_setfocus_site[4] == ((registry_slot_va >> 16) & 0xff), "MOV EDX,[registry slot]");
static_assert(remap_index(remap_focus_min) == remap_first && remap_index(remap_focus_min - 1) == remap_first - 1, "table floor");
static_assert(remap_index(remap_focus_max) == remap_first + remap_count - 1 && remap_index(remap_focus_max + 1) == remap_first + remap_count, "table ceiling");
static_assert(remap_focus_min == 0x2334 && remap_focus_max == 0x5ccc, "F range of N 50..130");
static_assert(load_window_va + load_site_offset == load_site_va && load_site_va + load_site_length == load_return_va, "the claimed load store");
static_assert(load_window_va + 5u + 0x000ccb67u == 0x004e9420u && load_window_va + 13u + 0x000ccb5fu == 0x004e9420u, "both calls reach the stream reader");
static_assert(load_caller_va + 5u - 0x000030b5u == load_function_va, "the load caller's rel32 (ff ff cf 4b) reaches the serializer");
static_assert((load_site_va & 7u) + 5u <= 8u, "the five jmp bytes lie in one aligned 8-byte word (atomic write)");
static_assert(expected_load_window[load_site_offset] == expected_load_site[0] && expected_load_window[load_site_offset + 5] == expected_load_site[5], "window holds the site");
}
