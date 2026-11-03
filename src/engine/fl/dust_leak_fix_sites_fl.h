#pragma once
#include <cstdint>
#include <cstring>

// FL sites of the dust-scene leak fix (src/engine/fl/dust_leak_fix_fl.cpp; the AP policy and parser are
// src/proxy/dust_leak_fix_sites.h, shared unchanged). docs/reverse-engineering/fl/dust-light.md, sections "1. Dust fill,
// failure ownership and release" and "Port (2026-11-03)". X3FL.exe is relocatable: every value here is an RVA (preferred
// base 0x400000), added to x3m::fl::module_base() at install. Windows are identified by their bytes with every
// base-relocation HIGHLOW dword zeroed (FNV-1a 32, the convention of fov_sites_fl.h); at install each such dword must
// equal base + its target RVA. No Windows dependency (the native fixture and the host tests compile it).
//
// The dust updater 0x020660 (three stack words, `ret 0xc` at 0x020c38) fills the dust scene in the loop 0x020950..
// 0x020ace. It allocates the node before it validates the dust body; three body failures skip the bind and the attach
// and land on the loop's common tail with the registered, never-attached node in ESI (the manager's R+0x28 list owns it
// for ever); the null allocation lands there with ESI = 0:
//
// 020950  43                      inc  ebx                            <- alloc window (16 bytes)
// 020951  e8 aa 18 08 00          call 0x0a2200                       ; allocate + register + link onto R+0x28
// 020956  8b f0                   mov  esi,eax
// 020958  85 f6                   test esi,esi
// 02095a  0f 84 64 01 00 00       je   0x020ac4                       ; null allocation (ESI = 0)
// 020a17  85 ff                   test edi,edi                        <- body window (53 bytes)
// 020a19  0f 88 a5 00 00 00       js   0x020ac4                       ; invalid body id
// 020a1f  81 ff a0 86 01 00       cmp  edi,100000 / jge 0x020a44
// 020a27  57 / e8 -> 0x086910     push edi / call (body failed flag)
// 020a2d  85 c0 / 0f 85 8f 00 00 00   test eax,eax / jne 0x020ac4    ; body already failed
// 020a35  8b cf / e8 -> 0x0a1a40  mov ecx,edi / call (body load)
// 020a3c  85 c0 / 0f 84 80 00 00 00   test eax,eax / je 0x020ac4     ; body load returned 0
// 020a44  57 / 8b ce / e8 -> 0x0a35e0 push edi / mov ecx,esi / call (bind)
// 020aba  8b ce                   mov  ecx,esi                        <- tail window (28 bytes)
// 020abc  ff 70 1c                push dword [eax+0x1c]               ; the dust scene
// 020abf  e8 ec 4b 08 00          call 0x0a56b0                       ; attach: +0x18 = 0, +0x1c = scene (ret 4)
// 020ac4  3b 5c 24 18             cmp  ebx,[esp+0x18]                 <- site (10 bytes: two whole instructions)
// 020ac8  0f 8c 82 fe ff ff       jl   0x020950                       ; loop while EBX < the target count
// 020ace  8b 6c 24 5c             mov  ebp,[esp+0x5c]                 ; the loop exit (reloads EBP, then EBX)
// 020ad2  8b 5c 24 54             mov  ebx,[esp+0x54]
//
// The claim displaces CMP and JL into the engine_patch tail (ten bytes; rel32_offset 6 re-bases the JL; the five-byte
// jump crosses the aligned qword 0x020ac0, so it is a plain write inside the install window and is never rewritten
// after it closes). The stub runs first with the tail's register state: ESI = the node or 0, EBX = the incremented fill
// count, ESP = the updater's frame (`sub esp,0x40` plus four pushes, [ESP+0x18] = the target count). When ESI != 0 and
// node+0x1c == 0 (never attached: the three failure branches; attach writes the scene there at 0x0a56f6; the
// constructor zeroes it) the stub calls the engine's own node release 0x0a3300 (ECX = node, no stack argument, plain
// RET; what the manager clear runs per R+0x28 head at 0x08a880/0x08a882: unlink, Remove from R+0xc, the external-
// reference scan, free) and sets EBX = [ESP+0x18]: the displaced CMP finds EBX == count, the JL falls through and the
// updater leaves the loop through its own exit (at most one failed attempt per invocation). Every other arrival (ESI =
// 0, or an attached node) changes nothing. The stub keeps EFLAGS (PUSHFD/POPFD), EAX/ECX/EDX/EBP (push/pop), x87/MMX/
// XMM/MXCSR across the release (FXSAVE/FXRSTOR, 16-aligned area on the stack) and LastError (GetLastError before,
// SetLastError after); EBX/ESI/EDI/EBP are callee-saved through the release (its epilogues pop them). EBX is the one
// intended output; the exit writes EBP and EBX before reading them, and ESI (the freed node) is not read before
// 0x020b28 redefines it (verify_dust_leak_fix_site_fl.py).
namespace x3m::fl::dust_leak_fix_fl {
constexpr std::uint32_t updater_rva = 0x020660, updater_ret_rva = 0x020c38, updater_end_rva = 0x020c3b; // ret 0xc
constexpr std::uint32_t loop_rva = 0x020950, site_rva = 0x020ac4, jl_rva = 0x020ac8, exit_rva = 0x020ace;
constexpr std::uint32_t alloc_rva = 0x0a2200, body_failed_rva = 0x086910, body_load_rva = 0x0a1a40,
                        bind_rva = 0x0a35e0, attach_rva = 0x0a56b0, attach_scene_store_rva = 0x0a56f6;
constexpr std::uint32_t release_rva = 0x0a3300, release_exit_rvas[2] = {0x0a359a, 0x0a35cf};
constexpr std::uint32_t clear_rva = 0x08a875, clear_call_rva = 0x08a882, manager_slot_rva = 0x1aa100;
// The four branches to the site and the attach call's fall-through: the five arrivals at the tail.
constexpr std::uint32_t failure_sources[4] = {0x02095a, 0x020a19, 0x020a2f, 0x020a3e};
constexpr std::uint32_t attach_call_rva = 0x020abf;
// Interior starts that must never be claimed (docs/reverse-engineering/fl/dust-light.md, "Evidence"): the second byte
// of the site's CMP and the second byte of the light decision's CVTTSS2SI.
constexpr std::uint32_t never_claim[2] = {0x020ac5, 0x0e4908};
constexpr unsigned site_length = 10, site_rel32_offset = 6, site_ret_pop = 12, patch_length = 5;
constexpr std::uint32_t scene_offset = 0x1c, count_slot = 0x18, node_size = 0x270;

constexpr unsigned alloc_length = 16, body_length = 53, tail_length = 28, release_length = 15, release_exit_length = 8,
                   attach_length = 73, clear_length = 31;
constexpr std::uint32_t alloc_window_rva = 0x020950, body_window_rva = 0x020a17, tail_window_rva = 0x020aba;
constexpr unsigned tail_site_offset = 0x0a; // site_rva - tail_window_rva
constexpr unsigned char expected_alloc[alloc_length] = {0x43, 0xe8, 0xaa, 0x18, 0x08, 0x00, 0x8b, 0xf0,
                                                        0x85, 0xf6, 0x0f, 0x84, 0x64, 0x01, 0x00, 0x00};
constexpr unsigned char expected_body[body_length] = {
    0x85, 0xff, 0x0f, 0x88, 0xa5, 0x00, 0x00, 0x00, 0x81, 0xff, 0xa0, 0x86, 0x01, 0x00, 0x7d, 0x1d, 0x57, 0xe8,
    0xe3, 0x5e, 0x06, 0x00, 0x85, 0xc0, 0x0f, 0x85, 0x8f, 0x00, 0x00, 0x00, 0x8b, 0xcf, 0xe8, 0x04, 0x10, 0x08,
    0x00, 0x85, 0xc0, 0x0f, 0x84, 0x80, 0x00, 0x00, 0x00, 0x57, 0x8b, 0xce, 0xe8, 0x94, 0x2b, 0x08, 0x00};
constexpr unsigned char expected_tail[tail_length] = {0x8b, 0xce, 0xff, 0x70, 0x1c, 0xe8, 0xec, 0x4b, 0x08, 0x00,
                                                      0x3b, 0x5c, 0x24, 0x18, 0x0f, 0x8c, 0x82, 0xfe, 0xff, 0xff,
                                                      0x8b, 0x6c, 0x24, 0x5c, 0x8b, 0x5c, 0x24, 0x54};
constexpr unsigned char expected_release[release_length] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec,
                                                            0x08, 0x53, 0x55, 0x56, 0x57, 0x8b, 0xe9};
// Both exits: pop edi; pop esi; pop ebp; pop ebx; mov esp,ebp; pop ebp; ret (plain).
constexpr unsigned char expected_release_exit[release_exit_length] = {0x5f, 0x5e, 0x5d, 0x5b, 0x8b, 0xe5, 0x5d, 0xc3};
constexpr unsigned char expected_attach[attach_length] = {
    0x53, 0x55, 0x8b, 0xe9, 0x56, 0x57, 0x8b, 0x45, 0x1c, 0x85, 0xc0, 0x74, 0x06, 0x50, 0xe8, 0x7d, 0x00, 0x00, 0x00,
    0x8b, 0x4d, 0x04, 0x8b, 0x45, 0x00, 0x8b, 0x5c, 0x24, 0x14, 0xc7, 0x45, 0x18, 0x00, 0x00, 0x00, 0x00, 0x89, 0x01,
    0x8b, 0x4d, 0x00, 0x8b, 0x45, 0x04, 0x89, 0x41, 0x04, 0x8b, 0x43, 0x10, 0x89, 0x45, 0x04, 0x8b, 0x43, 0x10, 0x89,
    0x28, 0x8d, 0x43, 0x0c, 0x89, 0x45, 0x00, 0x89, 0x6b, 0x10, 0x8b, 0x7d, 0x0c, 0x89, 0x5d, 0x1c};
// Fixup zeroed: +0x13 = base + manager_slot_rva (MOV EAX,[manager] after the release call).
constexpr unsigned char expected_clear[clear_length] = {0x8b, 0x42, 0x28, 0x83, 0x38, 0x00, 0x74, 0x17, 0x0f, 0x1f, 0x00,
                                                        0x8b, 0xc8, 0xe8, 0x79, 0x8a, 0x01, 0x00, 0xa1, 0x00, 0x00, 0x00,
                                                        0x00, 0x8b, 0x40, 0x28, 0x83, 0x38, 0x00, 0x75, 0xec};
constexpr unsigned char expected_site[site_length] = {0x3b, 0x5c, 0x24, 0x18, 0x0f, 0x8c, 0x82, 0xfe, 0xff, 0xff};

constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
constexpr std::uint32_t u32_at(const unsigned char* bytes) {
    return std::uint32_t(bytes[0]) | std::uint32_t(bytes[1]) << 8 | std::uint32_t(bytes[2]) << 16 |
           std::uint32_t(bytes[3]) << 24;
}
constexpr std::uint32_t alloc_fnv1a = 0xea6c5d54, body_fnv1a = 0x11cd18f1, tail_fnv1a = 0xfda11cf4,
                        release_fnv1a = 0x267af6be, release_exit_fnv1a = 0xefe79144, attach_fnv1a = 0xb3a1cfde,
                        clear_fnv1a = 0x6bcaf233;

// One verified span: its bytes with every HIGHLOW dword zeroed, and where each of those dwords points (an RVA).
struct Window {
    const char* name; // the refusal reason when it differs
    std::uint32_t rva;
    unsigned length;
    const unsigned char* bytes;
    unsigned fixups;
    unsigned fixup_offset[1];
    std::uint32_t fixup_rva[1];
};
constexpr unsigned max_window = 73;
constexpr Window windows[] = {
    {"alloc_mismatch", alloc_window_rva, alloc_length, expected_alloc, 0, {0}, {0}},
    {"body_mismatch", body_window_rva, body_length, expected_body, 0, {0}, {0}},
    {"tail_mismatch", tail_window_rva, tail_length, expected_tail, 0, {0}, {0}},
    {"release_mismatch", release_rva, release_length, expected_release, 0, {0}, {0}},
    {"release_exit_mismatch", release_exit_rvas[0], release_exit_length, expected_release_exit, 0, {0}, {0}},
    {"release_exit_mismatch", release_exit_rvas[1], release_exit_length, expected_release_exit, 0, {0}, {0}},
    {"attach_mismatch", attach_rva, attach_length, expected_attach, 0, {0}, {0}},
    {"clear_mismatch", clear_rva, clear_length, expected_clear, 1, {0x13}, {manager_slot_rva}},
};
constexpr unsigned window_count = sizeof windows / sizeof windows[0];
// The install decision on the bytes read at base + w.rva: nullptr when every fixup dword equals base + its target and
// the rest is exactly the engine's, else w.name.
inline const char* plan(const Window& w, const unsigned char* current, std::uintptr_t base) {
    unsigned char masked[max_window];
    if (w.length > max_window) return w.name;
    std::memcpy(masked, current, w.length);
    for (unsigned k = 0; k < w.fixups; ++k) {
        if (u32_at(masked + w.fixup_offset[k]) != std::uint32_t(base + w.fixup_rva[k])) return w.name;
        std::memset(masked + w.fixup_offset[k], 0, 4);
    }
    return std::memcmp(masked, w.bytes, w.length) ? w.name : nullptr;
}

// The stub pushed in front of the chain: 75 bytes with five fields filled at emission (three rel32 calls: GetLastError,
// the release, SetLastError; the abs32 of the hit counter and of the continuation slot). The slot lives in the arena
// after the stub and is only read (JMP [slot]); the counter is written (INC) and so lives outside the arena (a module
// global, as on AP; counter_outside() in dust_leak_fix_sites.h checks the encoded address).
//   +00 9c                      PUSHFD
//   +01 85 f6                   TEST ESI,ESI
//   +03 74 3f                   JE   +0x44            ; no node (allocation failed): unchanged
//   +05 83 7e 1c 00             CMP  dword [ESI+0x1c],0
//   +09 75 39                   JNE  +0x44            ; attached (the dust scene): unchanged
//   +0b 50 51 52 55             PUSH EAX; PUSH ECX; PUSH EDX; PUSH EBP
//   +0f 8b ec                   MOV  EBP,ESP
//   +11 81 ec 00 02 00 00       SUB  ESP,0x200
//   +17 83 e4 f0                AND  ESP,-16
//   +1a 0f ae 04 24             FXSAVE [ESP]          ; x87/MMX/XMM/MXCSR
//   +1e e8 <rel32>              CALL GetLastError
//   +23 50                      PUSH EAX              ; SetLastError's argument, popped by it (stdcall)
//   +24 8b ce                   MOV  ECX,ESI          ; the node: the release's only argument
//   +26 e8 <rel32>              CALL release          ; base+0x0a3300, plain RET
//   +2b e8 <rel32>              CALL SetLastError
//   +30 0f ae 0c 24             FXRSTOR [ESP]
//   +34 8b e5                   MOV  ESP,EBP
//   +36 5d 5a 59 58             POP  EBP; POP EDX; POP ECX; POP EAX
//   +3a 8b 5c 24 1c             MOV  EBX,[ESP+0x1c]   ; the tail's [ESP+0x18] (target count) past the PUSHFD
//   +3e ff 05 <abs32>           INC  dword [hits]
//   +44 9d                      POPFD
//   +45 ff 25 <abs32>           JMP  [slot]           ; tail: CMP; JL 0x020950; JMP 0x020ace
constexpr unsigned stub_length = 75;
constexpr unsigned get_error_rel32_offset = 0x1f, release_rel32_offset = 0x27, set_error_rel32_offset = 0x2c;
constexpr unsigned hits_abs32_offset = 0x40, slot_abs32_offset = 0x47, stub_done = 0x44;
constexpr unsigned char stub_code[stub_length] = {
    0x9c, 0x85, 0xf6, 0x74, 0x3f, 0x83, 0x7e, 0x1c, 0x00, 0x75, 0x39, 0x50, 0x51, 0x52, 0x55, 0x8b,
    0xec, 0x81, 0xec, 0x00, 0x02, 0x00, 0x00, 0x83, 0xe4, 0xf0, 0x0f, 0xae, 0x04, 0x24, 0xe8, 0x00,
    0x00, 0x00, 0x00, 0x50, 0x8b, 0xce, 0xe8, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x00, 0x00, 0x00, 0x00,
    0x0f, 0xae, 0x0c, 0x24, 0x8b, 0xe5, 0x5d, 0x5a, 0x59, 0x58, 0x8b, 0x5c, 0x24, 0x1c, 0xff, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x9d, 0xff, 0x25, 0x00, 0x00, 0x00, 0x00};
inline void put32(unsigned char* at, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) at[i] = static_cast<unsigned char>(v >> (8 * i));
}
// The stub as emitted at `stub_at`: each rel32 reaches its target from the byte after its field, the two abs32 name
// the counter and the slot.
inline void encode_stub(std::uint32_t stub_at, std::uint32_t get_error, std::uint32_t release, std::uint32_t set_error,
                        std::uint32_t hits, std::uint32_t slot, unsigned char out[stub_length]) {
    std::memcpy(out, stub_code, stub_length);
    put32(out + get_error_rel32_offset, get_error - (stub_at + get_error_rel32_offset + 4));
    put32(out + release_rel32_offset, release - (stub_at + release_rel32_offset + 4));
    put32(out + set_error_rel32_offset, set_error - (stub_at + set_error_rel32_offset + 4));
    put32(out + hits_abs32_offset, hits);
    put32(out + slot_abs32_offset, slot);
}
// The counter address the stub's INC writes.
inline std::uint32_t encoded_hits(const unsigned char stub[stub_length]) {
    return u32_at(stub + hits_abs32_offset);
}
// The ten live bytes after the claim: the jump's five, then the JL's last five unchanged.
inline bool claimed_span(const unsigned char now[site_length], const unsigned char jump[patch_length]) {
    return !std::memcmp(now, jump, patch_length) &&
           !std::memcmp(now + patch_length, expected_site + patch_length, site_length - patch_length);
}

constexpr bool tail_holds_site() {
    for (unsigned i = 0; i < site_length; ++i)
        if (expected_tail[tail_site_offset + i] != expected_site[i]) return false;
    return true;
}
static_assert(fnv1a(expected_alloc, alloc_length) == alloc_fnv1a, "alloc window is the measured one");
static_assert(fnv1a(expected_body, body_length) == body_fnv1a, "body window");
static_assert(fnv1a(expected_tail, tail_length) == tail_fnv1a, "tail window");
static_assert(fnv1a(expected_release, release_length) == release_fnv1a, "release entry");
static_assert(fnv1a(expected_release_exit, release_exit_length) == release_exit_fnv1a, "release exits");
static_assert(fnv1a(expected_attach, attach_length) == attach_fnv1a, "attach entry .. scene store");
static_assert(fnv1a(expected_clear, clear_length) == clear_fnv1a, "manager clear (fixup zeroed)");
static_assert(tail_window_rva + tail_site_offset == site_rva && tail_holds_site(), "the tail window carries the site");
static_assert(site_rva + 4 == jl_rva && jl_rva + 6 == exit_rva && site_rva + site_length == exit_rva &&
                  tail_window_rva + tail_length == exit_rva + 8,
              "CMP (4) + JL (6) end at the exit; the window ends after the exit's two reloads");
static_assert(exit_rva - 0x17e == loop_rva && expected_site[6] == 0x82 && expected_site[7] == 0xfe,
              "JL rel32 -0x17e reaches the loop head");
static_assert(site_rel32_offset + 4 == site_length && expected_site[site_rel32_offset - 2] == 0x0f &&
                  expected_site[site_rel32_offset - 1] == 0x8c,
              "the one relative branch in the span is the JL's rel32");
static_assert((site_rva & ~std::uint32_t(7)) != ((site_rva + patch_length - 1) & ~std::uint32_t(7)),
              "the five-byte jump crosses a qword: plain write, install window only");
static_assert(site_rva != never_claim[0] && site_rva != never_claim[1] && never_claim[0] == site_rva + 1,
              "the claim starts at the CMP, never at an interior byte");
static_assert(expected_site[3] == count_slot && expected_tail[4] == scene_offset && stub_code[7] == scene_offset &&
                  stub_code[0x3d] == count_slot + 4,
              "count slot and scene offsets");
static_assert(attach_scene_store_rva + 3 == attach_rva + attach_length && expected_attach[attach_length - 1] == scene_offset,
              "the attach window ends with MOV [EBP+0x1c],EBX");
static_assert(expected_alloc[11] == 0x84 && alloc_window_rva + 10 + 6 + 0x164 == site_rva, "null allocation reaches the site");
static_assert(body_window_rva + 2 + 6 + 0xa5 == site_rva && body_window_rva + 0x18 + 6 + 0x8f == site_rva &&
                  body_window_rva + 0x27 + 6 + 0x80 == site_rva,
              "the three body failures reach the site");
static_assert(failure_sources[0] == alloc_window_rva + 10 && failure_sources[1] == body_window_rva + 2 &&
                  failure_sources[2] == body_window_rva + 0x18 && failure_sources[3] == body_window_rva + 0x27 &&
                  attach_call_rva + 5 == site_rva,
              "the five arrivals");
static_assert(clear_call_rva == clear_rva + 13 && clear_call_rva + 5 + 0x18a79 == release_rva, "the clear calls the release");
static_assert(stub_code[3] == 0x74 && 5 + stub_code[4] == stub_done && stub_code[9] == 0x75 && 11 + stub_code[10] == stub_done &&
                  stub_code[stub_done] == 0x9d,
              "both skips reach the POPFD");
static_assert(stub_code[get_error_rel32_offset - 1] == 0xe8 && stub_code[release_rel32_offset - 1] == 0xe8 &&
                  stub_code[set_error_rel32_offset - 1] == 0xe8 && stub_code[hits_abs32_offset - 2] == 0xff &&
                  stub_code[hits_abs32_offset - 1] == 0x05 && stub_code[slot_abs32_offset - 2] == 0xff &&
                  stub_code[slot_abs32_offset - 1] == 0x25 && slot_abs32_offset + 4 == stub_length,
              "field positions");
}
