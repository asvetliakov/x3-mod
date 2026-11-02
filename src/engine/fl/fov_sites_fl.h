#pragma once
#include <cstdint>
#include <cstring>
#include "../../proxy/fov_sites.h"

// FL edition of the field-of-view sites (the AP twin is src/proxy/fov_sites.h, whose remap F -> F', 81-entry table,
// vanilla table, exact-match load rule, parser and constructor collision rule are shared unchanged). Verified facts:
// docs/reverse-engineering/fl/field-of-view.md, "Verified 2026-11-02 against X3FL.exe" (verifier
// verification/probe/verify_fov_site_fl.py). X3FL.exe is relocatable: every value below is an RVA added to
// x3m::fl::module_base(), or a field offset. Windows are identified by their bytes with every base-relocation HIGHLOW
// dword zeroed (FNV-1a 32, the convention of engine_effects_sites_fl.h); at install each such dword must equal
// base + its target RVA. No Windows dependency (the native fixture and the host tests compile it).
//
// Registry constructor 0x01df90 (ECX = registry, `mov esi,ecx` at 0x01dfac; ESI not written again before the site):
// 01e076  c7 46 20 00 00 00 00   MOV dword [ESI+0x20],0                <- constructor window (34 bytes, no fixup)
// 01e07d  66 c7 46 19 01 00      MOV word [ESI+0x19],1
// 01e083  c7 46 1c 00 00 00 00   MOV dword [ESI+0x1c],0
// 01e08a  c7 46 24 00 40 00 00   MOV dword [ESI+0x24],0x4000           <- site; imm32 at 01e08d -> F'
// 01e091  c7 45 e8 0f 00 00 00   MOV dword [EBP-0x18],0xf              ; (window ends at 01e098)
// The imm32 01e08d..01e090 starts at qword offset 5 and crosses into the next aligned qword: no single lock
// cmpxchg8b covers it. It is written with a plain copy inside the engine_patch install window (before the first
// Present: no registry can be under construction; the constructor runs only on a new game or a load), read back,
// and put back the same way on rollback. Same length, same operands: only the immediate changes.
//
// INS_SetFocus = script dispatcher 0x030540 (opcode [EBX+0x10], `cmp eax,0x70; ja; jmp [eax*4+0x432488]`) case 0x21:
// jump-table entry RVA 0x03250c (a HIGHLOW) holds base+0x030e9f; the name table entry RVA 0x1a3e1c names
// "INS_SetFocus". EBX = the dispatcher's saved incoming frame.
// 030e9f  8b 43 18               MOV  EAX,[EBX+0x18]                   <- case window (35 bytes, fixups +0x0c, +0x15)
// 030ea2  6a 00 / ff 73 0c       PUSH 0 / PUSH [EBX+0xc]               ; the callee's two stack arguments
// 030ea7  8b 48 01               MOV  ECX,[EAX+1]                      ; F from the script, no clamp
// 030eaa  a1 <base+0x1a82e4>     MOV  EAX,[registry slot]              <- claimed (5 bytes, qword offset 2)
// 030eaf  89 48 24               MOV  [EAX+0x24],ECX                   ; the base
// 030eb2  8b 0d <base+0x1aa174>  MOV  ECX,[VM]                         ; ECX dead after the store
// 030eb8  e8 83 0a 09 00         CALL 0x0c1940                         ; ECX = VM, two stack args, RET 8
// 030ebd  e9 ad 15 00 00         JMP  0x03246f                         ; (case window ends at 030ec2)
// 0c1940  53 56 57 8b 7c 24 10 b8 01 00 00 00 8b d9 66 89 47 20 80 7f 28 08 72 08   callee prefix: CMP writes EFLAGS
//         before the JB reads them, nothing reads them earlier: EFLAGS are dead after the claimed MOV.
// The stub runs in place of the displaced MOV EAX: EAX is scratch (the tail reloads it), ECX is the remap's input and
// output, EFLAGS dead; EDX, EBX, ESI, EDI, EBP, ESP and the two pushed arguments are not touched. No FPU/SSE, no call,
// no stack or memory write; LastError untouched. The displaced MOV's absolute operand is a HIGHLOW the loader already
// rebased: the tail copies the live five bytes (expected = a1 + base+0x1a82e4) and replays them once.
//
// Registry serializer 0x01d9a0 (ECX = registry, stack+4 save flag, stack+8 stream, RET 8; callers 0x020c62 save,
// push 1, and 0x020db6 load, push 0, each followed by TEST AL,AL). Load branch end:
// 01dedc  e8 1f 88 0e 00         CALL 0x106700                         <- load window (56 bytes, no fixup)
// 01dee1  8b 4c 24 10 ...        the four stream bytes from [ESP+0x10], byte-reversed into EDX (01dee1..01df07)
// 01df09  b0 01                  MOV  AL,1                             <- claimed (5 bytes, qword offset 1)
// 01df0b  89 57 24               MOV  [EDI+0x24],EDX                   ; EDI = the registry
// 01df0e  5f 5e 5b c2 08 00      POP EDI; POP ESI; POP EBX; RET 8      ; (window ends at 01df14)
// The stub runs in place of the two displaced instructions with the saved focus in EDX: ECX is scratch (not read
// again by the serializer; the load caller overwrites it at 0x020dcb before any read, its failure exit reloads it from
// the SEH slot at 0x020da1), EFLAGS are dead (POP/RET, then TEST AL,AL), EAX is preserved for the replayed MOV AL,1
// (the callers read AL only), EBX, ESI, EDI, EBP and ESP are not touched; EDX is the output.
//
// The cockpit update's reader 0x022a1c (`mov eax,[slot]; mov [esp+0x1c],edi; mov esi,[eax+0x24]`, fixup +1) is checked
// (fail closed) as the proof that registry+0x24 is the focus every update reads; it is not patched.
namespace x3m::fl::fov_fl {
namespace ap = x3m::fov::sites;

constexpr std::uint32_t constructor_rva = 0x01df90, constructor_esi_rva = 0x01dfac;
constexpr std::uint32_t window_rva = 0x01e076, site_rva = 0x01e08a, write_rva = 0x01e08d;
constexpr unsigned window_length = 34, site_offset = 0x14, site_length = 7, write_offset = 0x17, write_length = 4;
constexpr std::uint32_t setfocus_case_rva = 0x030e9f, setfocus_site_rva = 0x030eaa, setfocus_return_rva = 0x030eaf;
constexpr std::uint32_t setfocus_callee_rva = 0x0c1940, setfocus_exit_rva = 0x03246f;
constexpr std::uint32_t dispatcher_rva = 0x030540, jump_table_entry_rva = 0x03250c, name_entry_rva = 0x1a3e1c;
constexpr unsigned setfocus_case_length = 35, setfocus_site_offset = 11, setfocus_site_length = 5,
                   setfocus_callee_length = 24;
constexpr std::uint32_t load_window_rva = 0x01dedc, load_site_rva = 0x01df09, load_return_rva = 0x01df0e;
constexpr std::uint32_t serializer_rva = 0x01d9a0, stream_reader_rva = 0x106700;
constexpr std::uint32_t load_caller_rva = 0x020db6, save_caller_rva = 0x020c62;
constexpr unsigned load_window_length = 56, load_site_offset = 45, load_site_length = 5, load_caller_length = 9;
constexpr std::uint32_t reader_rva = 0x022a1c;
constexpr unsigned reader_length = 12;
constexpr std::uint32_t registry_slot_rva = 0x1a82e4, vm_slot_rva = 0x1aa174;
constexpr unsigned registry_focus_offset = 0x24;

constexpr unsigned char expected_window[window_length] = {
    0xc7, 0x46, 0x20, 0x00, 0x00, 0x00, 0x00, 0x66, 0xc7, 0x46, 0x19, 0x01, 0x00, 0xc7, 0x46, 0x1c, 0x00,
    0x00, 0x00, 0x00, 0xc7, 0x46, 0x24, 0x00, 0x40, 0x00, 0x00, 0xc7, 0x45, 0xe8, 0x0f, 0x00, 0x00, 0x00};
// Fixups zeroed: +0x0c = base + registry_slot_rva, +0x15 = base + vm_slot_rva.
constexpr unsigned char expected_setfocus_case[setfocus_case_length] = {
    0x8b, 0x43, 0x18, 0x6a, 0x00, 0xff, 0x73, 0x0c, 0x8b, 0x48, 0x01, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x89, 0x48,
    0x24, 0x8b, 0x0d, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x83, 0x0a, 0x09, 0x00, 0xe9, 0xad, 0x15, 0x00, 0x00};
constexpr unsigned char expected_setfocus_callee[setfocus_callee_length] = {
    0x53, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10, 0xb8, 0x01, 0x00, 0x00, 0x00,
    0x8b, 0xd9, 0x66, 0x89, 0x47, 0x20, 0x80, 0x7f, 0x28, 0x08, 0x72, 0x08};
constexpr unsigned char expected_load_window[load_window_length] = {
    0xe8, 0x1f, 0x88, 0x0e, 0x00, 0x8b, 0x4c, 0x24, 0x10, 0x8b, 0xd1, 0x8b, 0xc1, 0x81, 0xe2, 0x00, 0x00, 0xff, 0x00,
    0xc1, 0xe8, 0x10, 0x0b, 0xd0, 0x8b, 0xc1, 0xc1, 0xe0, 0x10, 0x81, 0xe1, 0x00, 0xff, 0x00, 0x00, 0x0b, 0xc1, 0xc1,
    0xea, 0x08, 0xc1, 0xe0, 0x08, 0x0b, 0xd0, 0xb0, 0x01, 0x89, 0x57, 0x24, 0x5f, 0x5e, 0x5b, 0xc2, 0x08, 0x00};
constexpr unsigned char expected_load_caller[load_caller_length] = {0xe8, 0xe5, 0xcb, 0xff, 0xff, 0x84, 0xc0, 0x74, 0xde};
// Fixup zeroed: +1 = base + registry_slot_rva.
constexpr unsigned char expected_reader[reader_length] = {0xa1, 0x00, 0x00, 0x00, 0x00, 0x89,
                                                          0x7c, 0x24, 0x1c, 0x8b, 0x70, 0x24};
constexpr unsigned char expected_load_site[load_site_length] = {0xb0, 0x01, 0x89, 0x57, 0x24}; // MOV AL,1; MOV [EDI+0x24],EDX
constexpr unsigned char expected_write[write_length] = {0x00, 0x40, 0x00, 0x00};                // imm32 0x4000

constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
constexpr std::uint32_t u32_at(const unsigned char* bytes) {
    return std::uint32_t(bytes[0]) | std::uint32_t(bytes[1]) << 8 | std::uint32_t(bytes[2]) << 16 |
           std::uint32_t(bytes[3]) << 24;
}
constexpr std::uint32_t window_fnv1a = 0x3edcd964, setfocus_case_fnv1a = 0x234b68a8, setfocus_callee_fnv1a = 0xd3cdd4e6,
                        load_window_fnv1a = 0xa2a1cf6b, load_caller_fnv1a = 0x4130196b, reader_fnv1a = 0x5f1bce8a;

// One verified span: its bytes with every HIGHLOW dword zeroed, and where each of those dwords points (an RVA).
struct Window {
    const char* name; // the refusal reason when it differs: <name>_mismatch
    std::uint32_t rva;
    unsigned length;
    const unsigned char* bytes;
    unsigned fixups;
    unsigned fixup_offset[2];
    std::uint32_t fixup_rva[2];
};
constexpr unsigned max_window = 56;
constexpr Window windows[] = {
    {"constructor_mismatch", window_rva, window_length, expected_window, 0, {0, 0}, {0, 0}},
    {"setfocus_mismatch", setfocus_case_rva, setfocus_case_length, expected_setfocus_case, 2, {0x0c, 0x15},
     {registry_slot_rva, vm_slot_rva}},
    {"callee_mismatch", setfocus_callee_rva, setfocus_callee_length, expected_setfocus_callee, 0, {0, 0}, {0, 0}},
    {"load_mismatch", load_window_rva, load_window_length, expected_load_window, 0, {0, 0}, {0, 0}},
    {"load_caller_mismatch", load_caller_rva, load_caller_length, expected_load_caller, 0, {0, 0}, {0, 0}},
    {"reader_mismatch", reader_rva, reader_length, expected_reader, 1, {1, 0}, {registry_slot_rva, 0}},
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
// The five live bytes the INS_SetFocus claim displaces: MOV EAX,[base + registry_slot_rva].
inline void setfocus_site(std::uintptr_t base, unsigned char out[setfocus_site_length]) {
    const std::uint32_t slot = std::uint32_t(base + registry_slot_rva);
    out[0] = 0xa1;
    for (unsigned k = 0; k < 4; ++k) out[1 + k] = static_cast<unsigned char>((slot >> (8 * k)) & 0xff);
}

// ---- The INS_SetFocus stub, FL registers (ECX in/out, EAX scratch): AP's layout with EAX in place of EDX ----
//   cmp ecx,remap_focus_min ; jb done ; cmp ecx,remap_focus_max ; ja done
//   imul eax,ecx,360 ; add eax,0x8000 ; shr eax,16                          ; N = round(F*360/65536)
//   movzx ecx,word [eax*2 + table - 2*50]                                   ; F' = table[N - 50]
//   done: jmp [slot]                                     ; -> tail: MOV EAX,[base+0x1a82e4]; JMP base+0x030eaf
constexpr unsigned stub_length = 45, stub_done = 39;
inline void encode_setfocus_stub(std::uint32_t table, std::uint32_t slot, unsigned char out[stub_length]) {
    // clang-format off
    const unsigned char code[stub_length] = {
        0x81,0xf9, 0,0,0,0,              //  0 cmp ecx,remap_focus_min
        0x72, stub_done - 8,             //  6 jb done
        0x81,0xf9, 0,0,0,0,              //  8 cmp ecx,remap_focus_max
        0x77, stub_done - 16,            // 14 ja done
        0x69,0xc1, 0x68,0x01,0x00,0x00,  // 16 imul eax,ecx,360
        0x81,0xc0, 0x00,0x80,0x00,0x00,  // 22 add eax,0x8000
        0xc1,0xe8, 0x10,                 // 28 shr eax,16
        0x0f,0xb7,0x0c,0x45, 0,0,0,0,    // 31 movzx ecx,word [eax*2 + disp32]
        0xff,0x25, 0,0,0,0};             // 39 done: jmp [slot]
    // clang-format on
    std::memcpy(out, code, stub_length);
    const std::uint32_t operands[4][2] = {
        {2, ap::remap_focus_min}, {10, ap::remap_focus_max}, {35, table - 2u * ap::remap_first}, {41, slot}};
    for (const auto& o : operands)
        for (unsigned k = 0; k < 4; ++k) out[o[0] + k] = static_cast<unsigned char>((o[1] >> (8 * k)) & 0xff);
}

// ---- The load stub, FL registers (EDX in/out, ECX scratch, EAX untouched): AP's exact-match rule ----
//   cmp edx,remap_focus_min ; jb done ; cmp edx,remap_focus_max ; ja done
//   imul ecx,edx,360 ; add ecx,0x8000 ; shr ecx,16                          ; N = round(F*360/65536)
//   cmp dx,word [ecx*2 + vanilla - 2*50] ; jne done                         ; only the exact (N<<16)/360
//   movzx edx,word [ecx*2 + table - 2*50]                                   ; F'(N)
//   done: jmp [slot]                 ; -> tail: MOV AL,1; MOV [EDI+0x24],EDX; JMP base+0x01df0e
// In range, EDX <= 0x5ccc, so the 16-bit compare against the 16-bit vanilla entry is the exact 32-bit compare.
constexpr unsigned load_stub_length = 55, load_stub_done = 49;
inline void encode_load_stub(std::uint32_t vanilla, std::uint32_t table, std::uint32_t slot,
                             unsigned char out[load_stub_length]) {
    // clang-format off
    const unsigned char code[load_stub_length] = {
        0x81,0xfa, 0,0,0,0,              //  0 cmp edx,remap_focus_min
        0x72, load_stub_done - 8,        //  6 jb done
        0x81,0xfa, 0,0,0,0,              //  8 cmp edx,remap_focus_max
        0x77, load_stub_done - 16,       // 14 ja done
        0x69,0xca, 0x68,0x01,0x00,0x00,  // 16 imul ecx,edx,360
        0x81,0xc1, 0x00,0x80,0x00,0x00,  // 22 add ecx,0x8000
        0xc1,0xe9, 0x10,                 // 28 shr ecx,16
        0x66,0x3b,0x14,0x4d, 0,0,0,0,    // 31 cmp dx,word [ecx*2 + disp32]
        0x75, load_stub_done - 41,       // 39 jne done
        0x0f,0xb7,0x14,0x4d, 0,0,0,0,    // 41 movzx edx,word [ecx*2 + disp32]
        0xff,0x25, 0,0,0,0};             // 49 done: jmp [slot]
    // clang-format on
    std::memcpy(out, code, load_stub_length);
    const std::uint32_t operands[5][2] = {{2, ap::remap_focus_min},
                                          {10, ap::remap_focus_max},
                                          {35, vanilla - 2u * ap::remap_first},
                                          {45, table - 2u * ap::remap_first},
                                          {51, slot}};
    for (const auto& o : operands)
        for (unsigned k = 0; k < 4; ++k) out[o[0] + k] = static_cast<unsigned char>((o[1] >> (8 * k)) & 0xff);
}

// The constructor literal: write, read-back and rollback over the four immediate bytes at base + write_rva (AP's
// fov::sites::install with the FL window; Ops as there: read, protect, write(at, bytes, n, &atomic), writable). The
// caller has verified every window already; the constructor window is read and compared once more right before the
// write. Returns "ok" or the reason; *live as in the AP install (true after "ok" and after "rollback_failed").
// fov::sites::restore() puts 00 40 00 00 back (the same four original bytes).
template <class Ops>
inline const char* install_literal(Ops& ops, std::uintptr_t base, bool window_open, std::uint32_t focus, bool* live,
                                   bool* atomic, std::uint32_t* protection) {
    *live = false;
    *atomic = false;
    unsigned char current[window_length]{};
    if (!base) return "invalid_site";
    if (!ap::plausible_focus(focus) || focus == ap::engine_focus) return "invalid_value";
    if (!window_open) return "late_claim";
    if (!ops.read(base + window_rva, current, window_length)) return "unreadable";
    if (const char* refused = plan(windows[0], current, base)) return refused;
    unsigned char patched[write_length];
    ap::encode(focus, patched);
    const std::uintptr_t at = base + write_rva;
    if (!ops.protect(at, write_length, Ops::writable, protection)) return "protect_failed";
    *live = true; // ownership published before the mutation, so a failed rollback stays registered
    const bool flushed = ops.write(at, patched, write_length, atomic);
    unsigned char readback[write_length]{};
    const bool verified = flushed && ops.read(at, readback, write_length) &&
                          !std::memcmp(readback, patched, write_length);
    std::uint32_t unused = 0;
    const bool protected_again = ops.protect(at, write_length, *protection, &unused);
    if (verified && protected_again) return "ok";
    std::uint32_t writable = 0;
    bool reprotected = false, ignored = false;
    if (!protected_again || ops.protect(at, write_length, Ops::writable, &writable)) {
        ops.write(at, expected_write, write_length, &ignored);
        reprotected = ops.protect(at, write_length, *protection, &unused);
    }
    unsigned char back[write_length]{};
    if (ops.read(at, back, write_length) && !std::memcmp(back, expected_write, write_length)) {
        *live = false;
        return reprotected ? "patch_rolled_back" : "rollback_unprotected";
    }
    return "rollback_failed";
}

constexpr bool window_holds_site() {
    for (unsigned i = 0; i < write_length; ++i)
        if (expected_window[write_offset + i] != expected_write[i] || ap::expected_write[i] != expected_write[i])
            return false;
    for (unsigned i = 0; i < site_length; ++i)
        if (expected_window[site_offset + i] != ap::expected_site[i]) return false; // the same MOV dword [ESI+0x24]
    return true;
}
constexpr bool load_window_holds_site() {
    for (unsigned i = 0; i < load_site_length; ++i)
        if (expected_load_window[load_site_offset + i] != expected_load_site[i]) return false;
    return true;
}
static_assert(fnv1a(expected_window, window_length) == window_fnv1a, "constructor window is the measured one");
static_assert(fnv1a(expected_setfocus_case, setfocus_case_length) == setfocus_case_fnv1a, "case window (fixups zeroed)");
static_assert(fnv1a(expected_setfocus_callee, setfocus_callee_length) == setfocus_callee_fnv1a, "callee prefix");
static_assert(fnv1a(expected_load_window, load_window_length) == load_window_fnv1a, "load window");
static_assert(fnv1a(expected_load_caller, load_caller_length) == load_caller_fnv1a, "load caller");
static_assert(fnv1a(expected_reader, reader_length) == reader_fnv1a, "update reader (fixup zeroed)");
static_assert(window_holds_site() && load_window_holds_site(), "the windows carry the sites");
static_assert(window_rva + site_offset == site_rva && site_rva + 3 == write_rva && window_rva + write_offset == write_rva,
              "the imm32 is the MOV's last four bytes");
// The module base is 64 KB aligned, so a span's qword geometry is its RVA's.
static_assert((write_rva & 7u) + write_length > 8u, "the imm32 crosses an aligned 8-byte word: plain write");
static_assert(constructor_esi_rva < window_rva && constructor_rva < constructor_esi_rva, "inside the constructor");
static_assert(setfocus_case_rva + setfocus_site_offset == setfocus_site_rva &&
                  setfocus_site_rva + setfocus_site_length == setfocus_return_rva,
              "the claimed MOV EAX");
static_assert(expected_setfocus_case[setfocus_site_offset] == 0xa1 && windows[1].fixup_offset[0] == setfocus_site_offset + 1,
              "MOV EAX,[moffs32]: its operand is the case window's first fixup");
static_assert(expected_setfocus_case[setfocus_site_offset + 5] == 0x89 && expected_setfocus_case[setfocus_site_offset + 6] == 0x48 &&
                  expected_setfocus_case[setfocus_site_offset + 7] == registry_focus_offset,
              "MOV [EAX+0x24],ECX follows the claimed MOV");
static_assert(expected_setfocus_case[19] == 0x8b && expected_setfocus_case[20] == 0x0d, "MOV ECX,[VM] after the store");
static_assert(setfocus_case_rva + 30 + u32_at(expected_setfocus_case + 26) == setfocus_callee_rva,
              "the call reaches the callee");
static_assert(setfocus_case_rva + setfocus_case_length + u32_at(expected_setfocus_case + 31) == setfocus_exit_rva,
              "the case jumps to the dispatcher's exit");
static_assert((setfocus_site_rva & 7u) + 5u <= 8u, "the five jmp bytes lie in one aligned 8-byte word (atomic)");
static_assert(load_window_rva + load_site_offset == load_site_rva && load_site_rva + load_site_length == load_return_rva,
              "the claimed load store");
static_assert(load_window_rva + 5 + u32_at(expected_load_window + 1) == stream_reader_rva, "the stream read");
static_assert(std::uint32_t(load_caller_rva + 5 + u32_at(expected_load_caller + 1)) == serializer_rva,
              "the load caller reaches the serializer");
static_assert((load_site_rva & 7u) + 5u <= 8u, "the five jmp bytes lie in one aligned 8-byte word (atomic)");
static_assert(expected_reader[0] == 0xa1 && expected_reader[9] == 0x8b && expected_reader[10] == 0x70 &&
                  expected_reader[11] == registry_focus_offset,
              "the update reads registry+0x24");
static_assert(registry_focus_offset == ap::registry_focus_offset, "the focus field is AP's");
}
