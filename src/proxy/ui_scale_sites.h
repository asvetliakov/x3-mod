#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Portable core of the UI scale option (X3M_UI_SCALE=auto|1..3; docs/architecture/ui-scale.md,
// docs/reverse-engineering/gui-scale.md section 5 strategy (b)): the verified byte windows of the ten
// engine sites, the scale's three fixed-point forms the stubs read, the pixel-orthographic projection
// transform, the mouse-delta remainder accumulator, the virtual screen size, the auto mapping, the
// X3M_UI_SCALE parser and the stub encoders. No Windows dependency: the host tests and the site verifier
// compile it directly.
//
// The in-game 2D UI (menus, sidebars, HUD panels, ticker) is drawn as scene instances carrying node flag
// 0x200, for which 0x004bdee0 builds a pixel-space orthographic projection P into *0x00608a3c (one body
// unit = one pixel, positions are pixel offsets from the anchored screen edge or the centre). Scaling the
// UI by s is exactly a virtual viewport of W/s x H/s pixels: P[0] *= s, P[5] *= s, and the translation
// about its anchor P[12] = (P[12] - ax) * s + ax, P[13] = (P[13] - ay) * s + ay. The script (KC) lays the
// UI out against B3D_ScreenGetWidth/Height and keeps its own cursor from the engine's mouse deltas, so it
// is told the virtual size (W/s, H/s) and gets the deltas divided by s (with a remainder, so slow motion
// is not lost); the one native store of the script's cursor (X2_UpdateCursorSteering) multiplies it back
// to real pixels for the native aim/fire consumers. The native cockpit HUD scene (cockpit+4, rendered with
// the camera at cockpit+8: crosshair group, text panels, target icons, the mod's lead marker) is excluded
// from the projection scale by camera identity so its projected pixel positions stay real pixels.
//
// Ten sites, all or none (a failure rolls the earlier ones back; every stub is the identity while the
// data cells hold s = 1, so even a failed rollback leaves vanilla behaviour):
//
//  A projection  0x004be246  b8 01 00 00 00            mov eax,1        the 2D branch's single exit
//  B width       0x00496194  8b 15 e4 85 60 00         mov edx,[VM]     KC case 0x71 after movsx eax,word [ecx+4]
//  C height      0x004961bb  8b 0d e4 85 60 00         mov ecx,[VM]     KC case 0x72 after movsx eax,word [eax+6]
//  D1 main x     0x00403d36  0f b7 80 14 04 00 00      movzx eax,word [eax+0x414]   main loop, event 0x11d
//  D2 main y     0x00403d7a  0f b7 80 16 04 00 00      movzx eax,word [eax+0x416]   main loop, event 0x11e
//  D3 menu x     0x00411b40  0f bf ba 14 04 00 00      movsx edi,word [edx+0x414]   0x00410080 loop, axis 0
//  D4 menu y     0x00411b57  0f bf ba 16 04 00 00      movsx edi,word [edx+0x416]   0x00410080 loop, axis 1
//  E cursor      0x004074ec  a3 f0 7c 60 00            mov [0x00607cf0],eax   X2_UpdateCursorSteering y store
//  G click icon  0x0042ece0  8b 76 06 51 56            mov esi,[esi+6]; push ecx; push esi   INS case 0x64
//  H cursor aim  0x0042ddf1  8b 73 06 8b 7b 0b         mov esi,[ebx+6]; mov edi,[ebx+0xb]   INS case 0x28
//  F diagnostic  0x004bdee0  83 ec 08 83 78 3c 00      sub esp,8; cmp [eax+0x3c],0   entry counter (--debug only)
//
// A: the 2D branch 0x004bdf3b..0x004be252 ends
//   004be1eb  a1 3c 8a 60 00     mov eax,[0x00608a3c]   ; P, not written again before the exit
//   ...       (P[13] and the anchor tests: mov ebx,[ebx+0x130] at 004be1fd keeps the flag word in EBX)
//   004be243  d9 58 20           fstp dword [eax+0x20]  ; the last x87 store: the stack is empty here
//   004be246  b8 01 00 00 00     mov eax,1              <- claimed (5 bytes; the tail: mov eax,1; jmp 004be24b)
//   004be24b  5f 5e 5d 5b        pop edi; pop esi; pop ebp; pop ebx
//   004be24f  83 c4 08           add esp,8              ; writes EFLAGS before any read
//   004be252  c3                 ret
// The stub runs with EAX = P, EBX = the instance's flag word (bits 0x1000/0x2000/0x4000/0x8000 = left/
// right/top/bottom anchor, 0x1000 before 0x2000 and 0x4000 before 0x8000 as the engine tests them), EBP =
// the camera (second argument, callee-saved and still the caller's value). EAX is dead (the tail loads 1),
// ECX and EDX are dead (caller-saved; the function writes both before the exit and both callers reload
// them after the call), EFLAGS are dead (add esp,8 writes them), the x87 stack is untouched, XMM0..XMM2
// are scratch (volatile across every x86 call boundary, and neither this function nor its two callers
// touches an XMM register; verify_ui_scale_sites.py). Both callers (0x0047e002 and 0x0047e70c) discard the return value:
// the next test is `test al,al` on the following call's result after `add esp,0x10`. Per 2D instance the
// stub is 31 instructions on the scaled path: one compare and branch, 11 movss (the scale, two table
// loads, four loads and four stores of P), 8 mulss/subss/addss, three mov/shr/and pairs for the two table
// indices, one inc and the jmp: no call, no push, no x87. At s = 1 (the identity cells after a Reset to
// 1080 lines under `auto`; the claim itself is never installed at 1) the transform is exact for P[0]/P[5]
// and within one ulp for P[12]/P[13] ((P - a) * 1 + a), invisible at pixel scale.
//
// B/C: 0x00493b40's cases 0x71/0x72 (jump table 0x0049679c, entries 0x00496181/0x004961a8):
//   00496181  a1 38 6f 60 00           mov eax,[0x00606f38]    ; display record
//   00496186  85 c0 0f 84 dd e8 ff ff  test eax,eax; je 0x00494a6b
//   0049618e  8b 08                    mov ecx,[eax]
//   00496190  0f bf 41 04              movsx eax,word [ecx+4]   ; W
//   00496194  8b 15 e4 85 60 00        mov edx,[0x006085e4]     <- claimed (6 bytes, tail: the mov + jmp 0049619a)
//   0049619a  50 52 8b c7 e8 4d e6 00 00   push eax; push edx; mov eax,edi; call 0x004a47f0 (cmp before jb)
// The stub scales EAX in place ((W * inverse16 + 0x8000) >> 16 = round(W/s)); EDX (C: ECX) is dead (the
// tail loads it), EFLAGS are dead (push/push/mov, then the callee's cmp), no other register is touched.
//
// D1..D4: the mouse deltas the engine keeps at input context (*0x00606f3c) +0x414/+0x416 (int16) are
// read once per reader and forwarded to the script as events 0x11d/0x11e. The stub performs the displaced
// load itself with the delta divided by s through a per-site 16.16 accumulator (acc += delta * inverse16;
// out = acc >> 16 arithmetic; acc &= 0xffff; the fraction starts at 0x8000 so the running sum rounds to
// nearest symmetrically instead of flooring), then `jmp [slot]` with the slot word holding the instruction
// after the load (the claim's tail is emitted but never entered). D1/D2: EAX = the context at entry, EAX = the zero-extended 16-bit result
// as the displaced movzx left it (the next instructions are add esp,0x24 / cmp ax,bx, EFLAGS dead). D3/D4:
// EDX = the context, EDI = the sign-extended result as the displaced movsx left it; the following
// push esi; mov ecx,ebx; xor eax,eax (D3) or mov eax,1; call 0x0040fec0 (D4, whose first instruction
// cmp [esp+4],0 writes EFLAGS) leave EFLAGS dead. Integer only, EAX/EDI and EFLAGS, one memory cell.
//
// E: case 0x1f of 0x00406de0 (X2_UpdateCursorSteering) stores the script's cursor:
//   004074cd  8b 47 01 8b 4f 06 8b 57 0b   mov eax,[edi+1]; mov ecx,[edi+6]; mov edx,[edi+0xb]  ; x in EDX
//   004074d6  a3 64 7c 60 00               mov [0x00607c64],eax
//   004074db  8b 47 10                     mov eax,[edi+0x10]                                   ; y in EAX
//   004074de  89 0d e8 7c 60 00            mov [0x00607ce8],ecx      (the chase-fire claim chase_cursor_write lives here)
//   004074e4  8b 0d e4 85 60 00 6a 00      mov ecx,[0x006085e4]; push 0
//   004074ec  a3 f0 7c 60 00               mov [0x00607cf0],eax      <- claimed (5 bytes, tail: the mov + jmp 004074f1)
//   004074f1  51 8b c3                     push ecx; mov eax,ebx
//   004074f4  89 15 ec 7c 60 00            mov [0x00607cec],edx
//   004074fa  e8 f1 d2 09 00               call 0x004a47f0
// The stub multiplies EAX (y) and EDX (x) by s in place ((v * fixed256 + 0x80) >> 8), so every native
// consumer of 0x00607cec/0x00607cf0 (unprojection 0x00489780, cursor aim 0x00425410, the fire gate) sees
// real pixels. ECX (the VM, pushed after) and EBX are untouched; EFLAGS are dead (push/mov/mov/call, the
// callee's cmp). The window compared at install skips the six bytes of the chase-fire claim.
//
// G, H (gui-scale.md section 6): the script also hands its cursor as arguments to two natives of the
// INS dispatcher 0x0042d340 (jump table 0x0042f064, 0x71 entries) whose icon hit test 0x004299a0 and
// cursor aim 0x00425410 measure the overlay brackets in real pixels; a click on a bracket missed by
// X(1 - 1/s). Both cases get the E rounding on the script's point; the natives themselves are not
// touched, because the fire path reaches them with real pixels through E.
//   0042ecc5  case 0x64 INS_CockpitGetObjectByTargetOverlayIconPos: ... call 0x0041cd20; test eax,eax; je
//   0042ecdd  8b 4e 0b           mov ecx,[esi+0xb]      ; y
//   0042ece0  8b 76 06 51 56     mov esi,[esi+6]; push ecx; push esi   <- claimed (5 bytes, one aligned qword)
//   0042ece5  05 ac 03 00 00 50  add eax,0x3ac; push eax               ; EFLAGS written before any read
//   0042eceb  e8 b0 ac ff ff     call 0x004299a0   (stdcall, ret 0xc; EDX written at 0x004299bf before read)
// The stub performs the load, scales ECX and ESI in place ((v * fixed256 + 0x80) >> 8), pushes both and
// continues at 0x0042ece5 through its slot word (the tail is never entered). EAX (the cockpit) and EDX
// are untouched; EFLAGS are dead.
//   0042ddc0  case 0x28 INS_CockpitGetCursorAim: ... or esi,-1; or edi,esi; cmp ecx,3; ...; jl 0042de09
//   0042ddee  83 f9 04           cmp ecx,4              ; ECX = the argument count
//   0042ddf1  8b 73 06 8b 7b 0b  mov esi,[ebx+6]; mov edi,[ebx+0xb]   <- claimed (6 bytes; jmp in the qword 0042ddf0)
//   0042ddf7  7c 10              jl 0x0042de09          ; reads the cmp's EFLAGS: LIVE across the span
//   0042ddf9  ... call 0x0042de11 -> 0x00425410(cockpit, x, y, flag)
// The stub performs both loads, scales ESI and EDI in place, then re-executes `cmp ecx,4` (ECX untouched,
// so EFLAGS are exactly the engine's) and continues at 0x0042ddf7 through its slot word. EAX, EBX, ECX and
// EDX are untouched. The -1,-1 sentinel of the short-argument path never passes through the span.
//
// F (X3M_DEBUG=1 only, independent of the scale): the function entry counts, per camera (a table of eight
// {camera, n2d, nother} rows, linear probe, the last row takes overflow), the instances with flag 0x200
// and the others; the Present path prints and clears it every frame. At entry EAX = the object (kept),
// [esp+4] = the instance, [esp+8] = the camera; ECX/EDX are dead (the function writes both before any
// read on every path, the callers reload them), EFLAGS are dead (the displaced cmp writes them).
namespace x3m::ui_scale::sites {
// ---- the scale ----
constexpr double scale_min = 1.0, scale_max = 3.0, scale_off = 1.0;
constexpr unsigned auto_reference_height = 1080, auto_steps_per_unit = 4; // quarter steps of height/1080
inline bool in_range(double s) {
    return std::isfinite(s) && s >= scale_min && s <= scale_max;
}
// auto: the back-buffer height over 1080, snapped down to a quarter step, clamped to [1, 3]
// (1080 -> 1, 1440 -> 1.25, 2160 -> 2, 4320 -> 3).
inline double auto_scale(unsigned height) {
    if (!height) return scale_off;
    const double steps = std::floor(double(height) * auto_steps_per_unit / auto_reference_height);
    const double s = steps / auto_steps_per_unit;
    return s < scale_min ? scale_min : s > scale_max ? scale_max : s;
}
// The three forms the stubs read: 65536/s rounded (B, C, D), s*256 rounded (E), s as float (A).
inline std::uint32_t inverse16(double s) {
    return static_cast<std::uint32_t>(std::floor(65536.0 / s + 0.5));
}
inline std::uint32_t fixed256(double s) {
    return static_cast<std::uint32_t>(std::floor(s * 256.0 + 0.5));
}
constexpr std::uint32_t identity_inverse16 = 65536, identity_fixed256 = 256;
// B/C: what the script is told, round(W/s) as the stub computes it (W <= 32767).
inline std::uint32_t virtual_size(std::uint32_t real, std::uint32_t inv16) {
    return (real * inv16 + 0x8000u) >> 16;
}
// D: one delta through the accumulator: the emitted delta and the remainder left in *acc (0..0xffff). The
// remainder starts at accumulator_start (one half): the emitted total is the running sum rounded to nearest,
// symmetric for both directions (ten +1 or ten -1 at s = 1.5 give +7 / -7).
constexpr std::int32_t accumulator_start = 0x8000;
inline std::int32_t mouse_step(std::int32_t delta, std::uint32_t inv16, std::int32_t* acc) {
    const std::int32_t sum = static_cast<std::int32_t>(delta * static_cast<std::int32_t>(inv16)) + *acc;
    const std::int32_t out = sum >> 16; // arithmetic: floor
    *acc = sum & 0xffff;
    return out;
}
// E: the script's virtual cursor coordinate back to real pixels.
inline std::int32_t cursor_real(std::int32_t v, std::uint32_t s256) {
    return (v * static_cast<std::int32_t>(s256) + 0x80) >> 8;
}
// A: the anchor terms by flag bits, in the engine's precedence (0x1000 over 0x2000, 0x4000 over 0x8000):
// index (flags >> 12) & 3 -> ax, (flags >> 14) & 3 -> ay.
constexpr float anchor_x_table[4] = {0.0f, -1.0f, 1.0f, -1.0f};
constexpr float anchor_y_table[4] = {0.0f, 1.0f, -1.0f, 1.0f};
inline float anchor_x(std::uint32_t flags) {
    return anchor_x_table[(flags >> 12) & 3u];
}
inline float anchor_y(std::uint32_t flags) {
    return anchor_y_table[(flags >> 14) & 3u];
}
// The projection transform the stub applies to P (row-major float[16] as the engine stores it).
inline void scale_projection(float p[16], std::uint32_t flags, float s) {
    const float ax = anchor_x(flags), ay = anchor_y(flags);
    p[0] *= s;
    p[5] *= s;
    p[12] = (p[12] - ax) * s + ax;
    p[13] = (p[13] - ay) * s + ay;
}
// The engine's own P for a 2D instance (section 3.1 of the note), for the host tests.
inline void engine_projection(float p[16], int wvp, int hvp, int x, int y, std::uint32_t flags) {
    for (unsigned i = 0; i < 16; ++i) p[i] = 0.0f;
    p[0] = 2.0f / float(wvp);
    p[5] = 2.0f / float(hvp);
    p[12] = 2.0f * (float(x) - 0.25f) / float(wvp) + anchor_x(flags);
    p[13] = 2.0f * (float(-y) - 0.25f) / float(hvp) + anchor_y(flags);
    p[14] = p[15] = 1.0f;
}

// ---- X3M_UI_SCALE ----
// Unset, empty, `1` = off (nothing patched); `auto` = from the back-buffer height at device creation;
// otherwise `[+]digits[.digits]` or `.digits` (locale independent) in [1, 3].
enum class Parse : unsigned char { off = 0, automatic = 1, value = 2, invalid = 3 };
constexpr unsigned setting_capacity = 32; // 1..31 characters; 32 or more is too_long
template <class Char> inline Parse parse_setting(const Char* text, double* scale) {
    *scale = scale_off;
    if (!text || !*text) return Parse::off;
    {
        const char* word = "auto";
        unsigned i = 0;
        for (; word[i] && text[i] == Char(word[i]); ++i) {}
        if (!word[i] && text[i] == Char(0)) return Parse::automatic;
    }
    const Char* p = text;
    if (*p == Char('+')) ++p;
    double value = 0;
    unsigned digits = 0;
    for (; *p >= Char('0') && *p <= Char('9'); ++p, ++digits) value = value * 10.0 + double(*p - Char('0'));
    if (*p == Char('.')) {
        double step = 0.1;
        for (++p; *p >= Char('0') && *p <= Char('9'); ++p, ++digits) {
            value += double(*p - Char('0')) * step;
            step *= 0.1;
        }
    }
    if (*p != Char(0) || digits == 0) return Parse::invalid;
    *scale = value;
    return Parse::value;
}

// ---- the sites ----
constexpr std::uintptr_t projection_function_va = 0x004bdee0, projection_function_end_va = 0x004be3e3;
constexpr std::uintptr_t projection_site_va = 0x004be246, projection_return_va = 0x004be24b;
constexpr std::uintptr_t projection_tail_va = 0x004be1eb; // the verified end of the 2D branch, through the ret
constexpr unsigned projection_site_length = 5, projection_tail_length = 104;
constexpr unsigned char expected_projection_site[projection_site_length] = {0xb8, 0x01, 0x00, 0x00, 0x00};
constexpr unsigned char expected_projection_tail[projection_tail_length] = {
    0xa1, 0x3c, 0x8a, 0x60, 0x00, 0xde, 0xca, 0xdb, 0x44, 0x24, 0x14, 0xde, 0xfa, 0xd9, 0xc9, 0xd9, 0x50, 0x34, 0x8b,
    0x9b, 0x30, 0x01, 0x00, 0x00, 0xf7, 0xc3, 0x00, 0x40, 0x00, 0x00, 0x74, 0x07, 0xd8, 0xc2, 0xd9, 0x58, 0x34, 0xeb,
    0x11, 0xf7, 0xc3, 0x00, 0x80, 0x00, 0x00, 0x74, 0x07, 0xd8, 0xe2, 0xd9, 0x58, 0x34, 0xeb, 0x02, 0xdd, 0xd8, 0xd9,
    0xc9, 0xd9, 0x50, 0x38, 0xd9, 0x58, 0x3c, 0xd9, 0x50, 0x0c, 0xd9, 0x50, 0x08, 0xd9, 0x50, 0x04, 0xd9, 0x50, 0x1c,
    0xd9, 0x50, 0x18, 0xd9, 0x50, 0x10, 0xd9, 0x50, 0x2c, 0xd9, 0x50, 0x24, 0xd9, 0x58, 0x20, 0xb8, 0x01, 0x00, 0x00,
    0x00, 0x5f, 0x5e, 0x5d, 0x5b, 0x83, 0xc4, 0x08, 0xc3};
// The two callers' call and what follows it (the return value and EFLAGS die before any read).
constexpr std::uintptr_t caller1_va = 0x0047e002, caller2_va = 0x0047e70c;
constexpr unsigned caller1_length = 31, caller2_length = 34;
constexpr unsigned char expected_caller1[caller1_length] = {0xe8, 0xd9, 0xfe, 0x03, 0x00, 0x8b, 0x8b, 0xac, 0x01, 0x00, 0x00,
                                                            0x8b, 0x93, 0xa8, 0x01, 0x00, 0x00, 0x51, 0x52, 0xe8, 0xc6, 0x86,
                                                            0x07, 0x00, 0x83, 0xc4, 0x10, 0x84, 0xc0, 0x74, 0x0a};
constexpr unsigned char expected_caller2[caller2_length] = {0xe8, 0xcf, 0xf7, 0x03, 0x00, 0x8b, 0x7e, 0x10, 0x8b, 0x8f, 0xac, 0x01,
                                                            0x00, 0x00, 0x8b, 0x97, 0xa8, 0x01, 0x00, 0x00, 0x51, 0x52, 0xe8, 0xb9,
                                                            0x7f, 0x07, 0x00, 0x83, 0xc4, 0x10, 0x84, 0xc0, 0x74, 0x0a};
// F: the entry (the diagnostic claim; the submit-phase fixture group claims the same bytes, so one of them refuses).
constexpr std::uintptr_t entry_site_va = 0x004bdee0, entry_return_va = 0x004bdee7;
constexpr unsigned entry_site_length = 7;
constexpr unsigned char expected_entry_site[entry_site_length] = {0x83, 0xec, 0x08, 0x83, 0x78, 0x3c, 0x00};
// B, C: the KC dispatcher's cases 0x71 and 0x72 (whole case bodies, 39 bytes each).
constexpr std::uintptr_t dispatcher_va = 0x00493b40, dispatcher_end_va = 0x0049679c, jump_table_va = 0x0049679c; // code ends at the table
constexpr unsigned jump_table_entries = 0xa8, width_case = 0x71, height_case = 0x72;
constexpr std::uintptr_t width_case_va = 0x00496181, width_site_va = 0x00496194, width_return_va = 0x0049619a;
constexpr std::uintptr_t height_case_va = 0x004961a8, height_site_va = 0x004961bb, height_return_va = 0x004961c1;
constexpr unsigned size_case_length = 39, size_site_length = 6, size_site_offset = 19;
constexpr unsigned char expected_width_case[size_case_length] = {
    0xa1, 0x38, 0x6f, 0x60, 0x00, 0x85, 0xc0, 0x0f, 0x84, 0xdd, 0xe8, 0xff, 0xff, 0x8b, 0x08, 0x0f, 0xbf, 0x41, 0x04, 0x8b,
    0x15, 0xe4, 0x85, 0x60, 0x00, 0x50, 0x52, 0x8b, 0xc7, 0xe8, 0x4d, 0xe6, 0x00, 0x00, 0xe9, 0xd9, 0x05, 0x00, 0x00};
constexpr unsigned char expected_height_case[size_case_length] = {
    0xa1, 0x38, 0x6f, 0x60, 0x00, 0x85, 0xc0, 0x0f, 0x84, 0xef, 0xd9, 0xff, 0xff, 0x8b, 0x00, 0x0f, 0xbf, 0x40, 0x06, 0x8b,
    0x0d, 0xe4, 0x85, 0x60, 0x00, 0x50, 0x51, 0x8b, 0xc7, 0xe8, 0x26, 0xe6, 0x00, 0x00, 0xe9, 0xb2, 0x05, 0x00, 0x00};
constexpr unsigned char expected_width_site[size_site_length] = {0x8b, 0x15, 0xe4, 0x85, 0x60, 0x00};  // mov edx,[0x006085e4]
constexpr unsigned char expected_height_site[size_site_length] = {0x8b, 0x0d, 0xe4, 0x85, 0x60, 0x00}; // mov ecx,[0x006085e4]
// D1..D4: the four delta reads (7-byte loads), each inside its verified window.
constexpr std::uintptr_t main_loop_va = 0x00403840, main_loop_end_va = 0x00404278;
constexpr std::uintptr_t menu_loop_va = 0x00410080, menu_loop_end_va = 0x00412250; // code ends at its first jump table
constexpr std::uintptr_t main_x_site_va = 0x00403d36, main_y_site_va = 0x00403d7a, menu_x_site_va = 0x00411b40,
                         menu_y_site_va = 0x00411b57;
constexpr unsigned mouse_site_length = 7;
constexpr unsigned char expected_main_x_site[mouse_site_length] = {0x0f, 0xb7, 0x80, 0x14, 0x04, 0x00, 0x00}; // movzx eax,word [eax+0x414]
constexpr unsigned char expected_main_y_site[mouse_site_length] = {0x0f, 0xb7, 0x80, 0x16, 0x04, 0x00, 0x00}; // movzx eax,word [eax+0x416]
constexpr unsigned char expected_menu_x_site[mouse_site_length] = {0x0f, 0xbf, 0xba, 0x14, 0x04, 0x00, 0x00}; // movsx edi,word [edx+0x414]
constexpr unsigned char expected_menu_y_site[mouse_site_length] = {0x0f, 0xbf, 0xba, 0x16, 0x04, 0x00, 0x00}; // movsx edi,word [edx+0x416]
constexpr std::uintptr_t main_x_window_va = 0x00403d31, main_y_window_va = 0x00403d75, menu_x_window_va = 0x00411b36,
                         menu_y_window_va = 0x00411b51;
constexpr unsigned main_x_window_length = 20, main_y_window_length = 17, menu_x_window_length = 27,
                   menu_y_window_length = 26;
constexpr unsigned char expected_main_x_window[main_x_window_length] = {0xa1, 0x3c, 0x6f, 0x60, 0x00, 0x0f, 0xb7, 0x80, 0x14, 0x04,
                                                                        0x00, 0x00, 0x83, 0xc4, 0x24, 0x66, 0x3b, 0xc3, 0x74, 0x30};
constexpr unsigned char expected_main_y_window[main_y_window_length] = {0xa1, 0x3c, 0x6f, 0x60, 0x00, 0x0f, 0xb7, 0x80, 0x16,
                                                                        0x04, 0x00, 0x00, 0x66, 0x3b, 0xc3, 0x74, 0x3f};
constexpr unsigned char expected_menu_x_window[menu_x_window_length] = {0x8b, 0x15, 0x3c, 0x6f, 0x60, 0x00, 0x8b, 0x74, 0x24,
                                                                        0x0c, 0x0f, 0xbf, 0xba, 0x14, 0x04, 0x00, 0x00, 0x56,
                                                                        0x8b, 0xcb, 0x33, 0xc0, 0xe8, 0x6f, 0xe3, 0xff, 0xff};
constexpr unsigned char expected_menu_y_window[menu_y_window_length] = {0x8b, 0x15, 0x3c, 0x6f, 0x60, 0x00, 0x0f, 0xbf, 0xba,
                                                                        0x16, 0x04, 0x00, 0x00, 0x56, 0x8b, 0xcb, 0xb8, 0x01,
                                                                        0x00, 0x00, 0x00, 0xe8, 0x55, 0xe3, 0xff, 0xff};
// D4's callee: cmp [esp+4],0 first (EFLAGS written before any read).
constexpr std::uintptr_t menu_callee_va = 0x0040fec0;
constexpr unsigned menu_callee_length = 11;
constexpr unsigned char expected_menu_callee[menu_callee_length] = {0x83, 0x7c, 0x24, 0x04, 0x00, 0x53,
                                                                    0x56, 0x8b, 0xf1, 0x74, 0x09};
constexpr std::uintptr_t input_context_slot_va = 0x00606f3c;
constexpr unsigned delta_x_offset = 0x414, delta_y_offset = 0x416;
// E: the cursor store and its window in two parts around the chase-fire claim at 0x004074de.
constexpr std::uintptr_t cursor_function_va = 0x00406de0, cursor_function_end_va = 0x0040770c; // code ends at its jump table
constexpr std::uintptr_t cursor_site_va = 0x004074ec, cursor_return_va = 0x004074f1;
constexpr std::uintptr_t cursor_pre_va = 0x004074cd, cursor_post_va = 0x004074e4, cursor_foreign_claim_va = 0x004074de;
constexpr unsigned cursor_site_length = 5, cursor_pre_length = 17, cursor_post_length = 27, cursor_foreign_claim_length = 6;
constexpr unsigned char expected_cursor_site[cursor_site_length] = {0xa3, 0xf0, 0x7c, 0x60, 0x00}; // mov [0x00607cf0],eax
constexpr unsigned char expected_cursor_pre[cursor_pre_length] = {0x8b, 0x47, 0x01, 0x8b, 0x4f, 0x06, 0x8b, 0x57, 0x0b,
                                                                  0xa3, 0x64, 0x7c, 0x60, 0x00, 0x8b, 0x47, 0x10};
constexpr unsigned char expected_cursor_post[cursor_post_length] = {0x8b, 0x0d, 0xe4, 0x85, 0x60, 0x00, 0x6a, 0x00, 0xa3,
                                                                    0xf0, 0x7c, 0x60, 0x00, 0x51, 0x8b, 0xc3, 0x89, 0x15,
                                                                    0xec, 0x7c, 0x60, 0x00, 0xe8, 0xf1, 0xd2, 0x09, 0x00};
constexpr std::uintptr_t cursor_x_va = 0x00607cec, cursor_y_va = 0x00607cf0;
// G, H: the INS dispatcher's cases 0x64 and 0x28 (whole case bodies) and the icon test's prefix.
constexpr std::uintptr_t ins_dispatcher_va = 0x0042d340, ins_dispatcher_end_va = 0x0042f064, ins_jump_table_va = 0x0042f064;
constexpr unsigned ins_jump_table_entries = 0x71, overlay_case = 0x64, aim_case = 0x28;
constexpr std::uintptr_t overlay_case_va = 0x0042ecc5, overlay_site_va = 0x0042ece0, overlay_return_va = 0x0042ece5;
constexpr std::uintptr_t aim_case_va = 0x0042ddc0, aim_site_va = 0x0042ddf1, aim_return_va = 0x0042ddf7, aim_flags_va = 0x0042ddee;
constexpr std::uintptr_t icon_test_va = 0x004299a0, cursor_aim_va = 0x00425410;
constexpr unsigned overlay_case_length = 48, overlay_site_length = 5, aim_case_length = 94, aim_site_length = 6, icon_test_length = 33;
constexpr unsigned char expected_overlay_site[overlay_site_length] = {0x8b, 0x76, 0x06, 0x51, 0x56}; // mov esi,[esi+6]; push ecx; push esi
constexpr unsigned char expected_aim_site[aim_site_length] = {0x8b, 0x73, 0x06, 0x8b, 0x7b, 0x0b};   // mov esi,[ebx+6]; mov edi,[ebx+0xb]
constexpr unsigned char expected_overlay_case[overlay_case_length] = {
    0x8b, 0x75, 0x18, 0x8b, 0x56, 0x01, 0xa1, 0x04, 0x85, 0x60, 0x00, 0xe8, 0x4b, 0xe0, 0xfe, 0xff, 0x85, 0xc0, 0x0f, 0x84,
    0x11, 0xea, 0xff, 0xff, 0x8b, 0x4e, 0x0b, 0x8b, 0x76, 0x06, 0x51, 0x56, 0x05, 0xac, 0x03, 0x00, 0x00, 0x50, 0xe8, 0xb0,
    0xac, 0xff, 0xff, 0xe9, 0x21, 0xf1, 0xff, 0xff};
constexpr unsigned char expected_aim_case[aim_case_length] = {
    0x8b, 0x5d, 0x18, 0x8b, 0x53, 0x01, 0xa1, 0x04, 0x85, 0x60, 0x00, 0xe8, 0x50, 0xef, 0xfe, 0xff, 0x85, 0xc0, 0x89, 0x44,
    0x24, 0x10, 0x0f, 0x84, 0x12, 0xf9, 0xff, 0xff, 0x8b, 0x4d, 0x14, 0x83, 0xce, 0xff, 0x0b, 0xfe, 0x83, 0xf9, 0x03, 0xc6,
    0x44, 0x24, 0x0c, 0x00, 0x7c, 0x1b, 0x83, 0xf9, 0x04, 0x8b, 0x73, 0x06, 0x8b, 0x7b, 0x0b, 0x7c, 0x10, 0x8d, 0x43, 0x0f,
    0xe8, 0x6f, 0xab, 0x07, 0x00, 0x88, 0x44, 0x24, 0x0c, 0x8b, 0x44, 0x24, 0x10, 0x8b, 0x54, 0x24, 0x0c, 0x52, 0x57, 0x56,
    0x50, 0xe8, 0xfa, 0x75, 0xff, 0xff, 0x85, 0xc0, 0x0f, 0x84, 0xd0, 0xf8, 0xff, 0xff};
constexpr unsigned char expected_icon_test[icon_test_length] = {0x83, 0xec, 0x10, 0x53, 0x55, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x24,
                                                                0x8b, 0x87, 0x48, 0x03, 0x00, 0x00, 0x85, 0xc0, 0x0f, 0x84, 0x24,
                                                                0x03, 0x00, 0x00, 0x8b, 0x0d, 0x38, 0x6f, 0x60, 0x00, 0x8b, 0x11};
// The excluded scene's camera: the active cockpit's HUD camera at cockpit+8 (the HUD scene is cockpit+4).
constexpr std::uintptr_t cockpit_registry_slot_va = 0x00608504;
constexpr unsigned cockpit_hud_camera_offset = 8, cockpit_hud_scene_offset = 4;

// ---- the stub encoders (position independent apart from their absolute operands) ----
namespace detail {
inline void put32(unsigned char* out, std::uint32_t v) {
    for (unsigned k = 0; k < 4; ++k) out[k] = static_cast<unsigned char>((v >> (8 * k)) & 0xff);
}
}
// A. cells: scale (float), excluded (camera pointer), scaled/excluded counters, the two 4-float anchor
// tables; slot = the 4-aligned continuation word (the tail).
constexpr unsigned projection_stub_length = 144, projection_stub_excluded = 132;
inline void encode_projection_stub(std::uint32_t scale, std::uint32_t excluded, std::uint32_t scaled_count,
                                   std::uint32_t excluded_count, std::uint32_t anchor_x, std::uint32_t anchor_y,
                                   std::uint32_t slot, unsigned char out[projection_stub_length]) {
    // clang-format off
    const unsigned char code[projection_stub_length] = {
        0x3b,0x2d, 0,0,0,0,                    //   0 cmp ebp,[excluded]
        0x74, projection_stub_excluded - 8,    //   6 je excluded
        0xf3,0x0f,0x10,0x05, 0,0,0,0,          //   8 movss xmm0,[scale]
        0xf3,0x0f,0x10,0x08,                   //  16 movss xmm1,[eax]          P[0]
        0xf3,0x0f,0x59,0xc8,                   //  20 mulss xmm1,xmm0
        0xf3,0x0f,0x11,0x08,                   //  24 movss [eax],xmm1
        0xf3,0x0f,0x10,0x48,0x14,              //  28 movss xmm1,[eax+0x14]     P[5]
        0xf3,0x0f,0x59,0xc8,                   //  33 mulss xmm1,xmm0
        0xf3,0x0f,0x11,0x48,0x14,              //  37 movss [eax+0x14],xmm1
        0x8b,0xcb,                             //  42 mov ecx,ebx
        0xc1,0xe9,0x0c,                        //  44 shr ecx,12
        0x83,0xe1,0x03,                        //  47 and ecx,3
        0xf3,0x0f,0x10,0x14,0x8d, 0,0,0,0,     //  50 movss xmm2,[ecx*4+anchor_x]
        0xf3,0x0f,0x10,0x48,0x30,              //  59 movss xmm1,[eax+0x30]     P[12]
        0xf3,0x0f,0x5c,0xca,                   //  64 subss xmm1,xmm2
        0xf3,0x0f,0x59,0xc8,                   //  68 mulss xmm1,xmm0
        0xf3,0x0f,0x58,0xca,                   //  72 addss xmm1,xmm2
        0xf3,0x0f,0x11,0x48,0x30,              //  76 movss [eax+0x30],xmm1
        0x8b,0xcb,                             //  81 mov ecx,ebx
        0xc1,0xe9,0x0e,                        //  83 shr ecx,14
        0x83,0xe1,0x03,                        //  86 and ecx,3
        0xf3,0x0f,0x10,0x14,0x8d, 0,0,0,0,     //  89 movss xmm2,[ecx*4+anchor_y]
        0xf3,0x0f,0x10,0x48,0x34,              //  98 movss xmm1,[eax+0x34]     P[13]
        0xf3,0x0f,0x5c,0xca,                   // 103 subss xmm1,xmm2
        0xf3,0x0f,0x59,0xc8,                   // 107 mulss xmm1,xmm0
        0xf3,0x0f,0x58,0xca,                   // 111 addss xmm1,xmm2
        0xf3,0x0f,0x11,0x48,0x34,              // 115 movss [eax+0x34],xmm1
        0xff,0x05, 0,0,0,0,                    // 120 inc dword [scaled_count]
        0xff,0x25, 0,0,0,0,                    // 126 jmp [slot]
        0xff,0x05, 0,0,0,0,                    // 132 excluded: inc dword [excluded_count]
        0xff,0x25, 0,0,0,0};                   // 138 jmp [slot]
    // clang-format on
    std::memcpy(out, code, projection_stub_length);
    detail::put32(out + 2, excluded);
    detail::put32(out + 12, scale);
    detail::put32(out + 55, anchor_x);
    detail::put32(out + 94, anchor_y);
    detail::put32(out + 122, scaled_count);
    detail::put32(out + 128, slot);
    detail::put32(out + 134, excluded_count);
    detail::put32(out + 140, slot);
}
// B, C. EAX = the real size in, round(EAX/s) out; inverse16 = the 65536/s cell.
constexpr unsigned size_stub_length = 21;
inline void encode_size_stub(std::uint32_t inverse16, std::uint32_t slot, unsigned char out[size_stub_length]) {
    // clang-format off
    const unsigned char code[size_stub_length] = {
        0x0f,0xaf,0x05, 0,0,0,0,               //  0 imul eax,[inverse16]
        0x05, 0x00,0x80,0x00,0x00,             //  7 add eax,0x8000
        0xc1,0xe8,0x10,                        // 12 shr eax,16
        0xff,0x25, 0,0,0,0};                   // 15 jmp [slot]
    // clang-format on
    std::memcpy(out, code, size_stub_length);
    detail::put32(out + 3, inverse16);
    detail::put32(out + 17, slot);
}
// D1, D2. EAX = the input context in; EAX = the zero-extended scaled delta out (as the displaced movzx
// left it); slot = the 4-aligned continuation word, which install_site fills with site + 7 (the tail is
// bypassed). offset = 0x414 (x) or 0x416 (y).
constexpr unsigned main_mouse_stub_length = 47;
inline void encode_main_mouse_stub(unsigned offset, std::uint32_t inverse16, std::uint32_t acc, std::uint32_t slot,
                                   unsigned char out[main_mouse_stub_length]) {
    // clang-format off
    const unsigned char code[main_mouse_stub_length] = {
        0x0f,0xbf,0x80, 0,0,0,0,               //  0 movsx eax,word [eax+offset]
        0x0f,0xaf,0x05, 0,0,0,0,               //  7 imul eax,[inverse16]
        0x03,0x05, 0,0,0,0,                    // 14 add eax,[acc]
        0xa3, 0,0,0,0,                         // 20 mov [acc],eax
        0xc1,0xf8,0x10,                        // 25 sar eax,16
        0x81,0x25, 0,0,0,0, 0xff,0xff,0x00,0x00, // 28 and dword [acc],0xffff
        0x0f,0xb7,0xc0,                        // 38 movzx eax,ax
        0xff,0x25, 0,0,0,0};                   // 41 jmp [slot]
    // clang-format on
    std::memcpy(out, code, main_mouse_stub_length);
    detail::put32(out + 3, offset);
    detail::put32(out + 10, inverse16);
    detail::put32(out + 16, acc);
    detail::put32(out + 21, acc);
    detail::put32(out + 30, acc);
    detail::put32(out + 43, slot);
}
// D3, D4. EDX = the input context in; EDI = the sign-extended scaled delta out (as the displaced movsx
// left it); slot = site + 7.
constexpr unsigned menu_mouse_stub_length = 45;
inline void encode_menu_mouse_stub(unsigned offset, std::uint32_t inverse16, std::uint32_t acc, std::uint32_t slot,
                                   unsigned char out[menu_mouse_stub_length]) {
    // clang-format off
    const unsigned char code[menu_mouse_stub_length] = {
        0x0f,0xbf,0xba, 0,0,0,0,               //  0 movsx edi,word [edx+offset]
        0x0f,0xaf,0x3d, 0,0,0,0,               //  7 imul edi,[inverse16]
        0x03,0x3d, 0,0,0,0,                    // 14 add edi,[acc]
        0x89,0x3d, 0,0,0,0,                    // 20 mov [acc],edi
        0xc1,0xff,0x10,                        // 26 sar edi,16
        0x81,0x25, 0,0,0,0, 0xff,0xff,0x00,0x00, // 29 and dword [acc],0xffff
        0xff,0x25, 0,0,0,0};                   // 39 jmp [slot]
    // clang-format on
    std::memcpy(out, code, menu_mouse_stub_length);
    detail::put32(out + 3, offset);
    detail::put32(out + 10, inverse16);
    detail::put32(out + 16, acc);
    detail::put32(out + 22, acc);
    detail::put32(out + 31, acc);
    detail::put32(out + 41, slot);
}
// E. EAX = y and EDX = x (virtual) in, both times s out; fixed256 = the s*256 cell; slot = the tail.
constexpr unsigned cursor_stub_length = 37;
inline void encode_cursor_stub(std::uint32_t fixed256, std::uint32_t slot, unsigned char out[cursor_stub_length]) {
    // clang-format off
    const unsigned char code[cursor_stub_length] = {
        0x0f,0xaf,0x05, 0,0,0,0,               //  0 imul eax,[fixed256]
        0x05, 0x80,0x00,0x00,0x00,             //  7 add eax,0x80
        0xc1,0xf8,0x08,                        // 12 sar eax,8
        0x0f,0xaf,0x15, 0,0,0,0,               // 15 imul edx,[fixed256]
        0x81,0xc2, 0x80,0x00,0x00,0x00,        // 22 add edx,0x80
        0xc1,0xfa,0x08,                        // 28 sar edx,8
        0xff,0x25, 0,0,0,0};                   // 31 jmp [slot]
    // clang-format on
    std::memcpy(out, code, cursor_stub_length);
    detail::put32(out + 3, fixed256);
    detail::put32(out + 18, fixed256);
    detail::put32(out + 33, slot);
}
// G. ESI = the argument block, ECX = y (already loaded) in; both pushed scaled; the slot word holds 0x0042ece5.
constexpr unsigned overlay_stub_length = 43;
inline void encode_overlay_stub(std::uint32_t fixed256, std::uint32_t slot, unsigned char out[overlay_stub_length]) {
    // clang-format off
    const unsigned char code[overlay_stub_length] = {
        0x8b,0x76,0x06,                        //  0 mov esi,[esi+6]              x
        0x0f,0xaf,0x0d, 0,0,0,0,               //  3 imul ecx,[fixed256]
        0x81,0xc1, 0x80,0x00,0x00,0x00,        // 10 add ecx,0x80
        0xc1,0xf9,0x08,                        // 16 sar ecx,8
        0x0f,0xaf,0x35, 0,0,0,0,               // 19 imul esi,[fixed256]
        0x81,0xc6, 0x80,0x00,0x00,0x00,        // 26 add esi,0x80
        0xc1,0xfe,0x08,                        // 32 sar esi,8
        0x51,                                  // 35 push ecx
        0x56,                                  // 36 push esi
        0xff,0x25, 0,0,0,0};                   // 37 jmp [slot]
    // clang-format on
    std::memcpy(out, code, overlay_stub_length);
    detail::put32(out + 6, fixed256);
    detail::put32(out + 22, fixed256);
    detail::put32(out + 39, slot);
}
// H. EBX = the argument block, ECX = the argument count in; ESI = x, EDI = y scaled out; `cmp ecx,4` last so the
// engine's jl reads the flags it expects; the slot word holds 0x0042ddf7.
constexpr unsigned aim_stub_length = 47;
inline void encode_aim_stub(std::uint32_t fixed256, std::uint32_t slot, unsigned char out[aim_stub_length]) {
    // clang-format off
    const unsigned char code[aim_stub_length] = {
        0x8b,0x73,0x06,                        //  0 mov esi,[ebx+6]              x
        0x8b,0x7b,0x0b,                        //  3 mov edi,[ebx+0xb]            y
        0x0f,0xaf,0x35, 0,0,0,0,               //  6 imul esi,[fixed256]
        0x81,0xc6, 0x80,0x00,0x00,0x00,        // 13 add esi,0x80
        0xc1,0xfe,0x08,                        // 19 sar esi,8
        0x0f,0xaf,0x3d, 0,0,0,0,               // 22 imul edi,[fixed256]
        0x81,0xc7, 0x80,0x00,0x00,0x00,        // 29 add edi,0x80
        0xc1,0xff,0x08,                        // 35 sar edi,8
        0x83,0xf9,0x04,                        // 38 cmp ecx,4                    the displaced compare's flags
        0xff,0x25, 0,0,0,0};                   // 41 jmp [slot]
    // clang-format on
    std::memcpy(out, code, aim_stub_length);
    detail::put32(out + 9, fixed256);
    detail::put32(out + 25, fixed256);
    detail::put32(out + 43, slot);
}
// F. table = 8 rows of {camera, n2d, nother, unused} (16 bytes each, camera 0 = free); slot = the tail.
constexpr unsigned entry_stub_length = 79, camera_rows = 8, camera_row_bytes = 16;
inline void encode_entry_stub(std::uint32_t table, std::uint32_t slot, unsigned char out[entry_stub_length]) {
    // clang-format off
    const unsigned char code[entry_stub_length] = {
        0x8b,0x4c,0x24,0x08,                   //  0 mov ecx,[esp+8]            camera
        0x33,0xd2,                             //  4 xor edx,edx
        0x39,0x8a, 0,0,0,0,                    //  6 probe: cmp [edx+table],ecx
        0x74, 0x1d,                            // 12 je found (43)
        0x83,0xba, 0,0,0,0, 0x00,              // 14 cmp dword [edx+table],0
        0x74, 0x0e,                            // 21 je claim (37)
        0x83,0xc2,0x10,                        // 23 add edx,16
        0x81,0xfa, 0x80,0x00,0x00,0x00,        // 26 cmp edx,128
        0x72, 0xe4,                            // 32 jb probe (6)
        0x83,0xea,0x10,                        // 34 sub edx,16                 overflow: the last row
        0x89,0x8a, 0,0,0,0,                    // 37 claim: mov [edx+table],ecx
        0x8b,0x4c,0x24,0x04,                   // 43 found: mov ecx,[esp+4]     instance
        0xf7,0x81, 0x30,0x01,0x00,0x00, 0x00,0x02,0x00,0x00, // 47 test dword [ecx+0x130],0x200
        0x74, 0x08,                            // 57 jz other (67)
        0xff,0x82, 0,0,0,0,                    // 59 inc dword [edx+table+4]
        0xeb, 0x06,                            // 65 jmp done (73)
        0xff,0x82, 0,0,0,0,                    // 67 other: inc dword [edx+table+8]
        0xff,0x25, 0,0,0,0};                   // 73 done: jmp [slot]
    // clang-format on
    std::memcpy(out, code, entry_stub_length);
    detail::put32(out + 8, table);
    detail::put32(out + 16, table);
    detail::put32(out + 39, table);
    detail::put32(out + 61, table + 4);
    detail::put32(out + 69, table + 8);
    detail::put32(out + 75, slot);
}
}
