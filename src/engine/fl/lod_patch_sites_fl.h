#pragma once
#include <cstdint>
#include <cstring>

// FL edition of the two one-byte LOD patches (the AP twins are src/proxy/terran_lod_sites.h and
// src/proxy/lod_occlusion_sites.h, whose X3M_TERRAN_STATION_LOD / X3M_LOD_OCCLUSION parsers the FL modules reuse).
// Verified facts: docs/reverse-engineering/fl/cull-chain.md, "Verified 2026-11-02 against X3FL.exe", sections 2 and 3
// (producer verification/results/fl-cull-chain/produce.py, contract.json windows terran_root_flag and
// lod_occlusion_gate, edits terran and occlusion). X3FL.exe is relocatable: every value below is an RVA added to
// x3m::fl::module_base(). No Windows dependency (the native fixture compiles it).
//
// Terran station LOD, selector root path (window 31 bytes at 0x095e82, no fixup):
// 095e82  84 c0                 TEST AL,AL
// 095e84  75 1b                 JNE  0x095ea1                    ; propagated flag already set
// 095e86  83 7e 18 00           CMP  dword [ESI+0x18],0
// 095e8a  75 15                 JNE  0x095ea1                    ; node has a parent
// 095e8c  83 be 2c 01 00 00 00  CMP  dword [ESI+0x12c],0         ; bit 31 = Terran TDocks/TFactories root (0x04ab6a)
// 095e93  0f b6 c0              MOVZX EAX,AL
// 095e96  0f 4c c1              CMOVL EAX,ECX                    <- ModRM 0x095e98: c1 -> c0 = CMOVL EAX,EAX
// 095e99  89 44 24 1c           MOV  [ESP+0x1c],EAX              ; local distance flag
// 095e9d  88 44 24 24           MOV  byte [ESP+0x24],AL          ; propagated flag
// ECX = 1 (0x095e69). On this path AL is 0 (the JNE above left for a non-zero flag), so the patched CMOVL keeps EAX = 0
// and both slots get 0: every node selects by screen size. Same length and boundaries; no flag write (CMOVcc); nothing
// enters 0x095e97..0x095e98; the byte lies in the aligned qword 0x095e98: one lock cmpxchg8b.
//
// LOD occlusion gate, material submission (window 20 bytes at 0x0e56b1, one fixup at +0x0b -> base + 0x1a9014):
// 0e56b1  8b 45 e4              MOV  EAX,[EBP-0x1c]              ; node
// 0e56b4  83 b8 50 01 00 00 00  CMP  dword [EAX+0x150],0         ; selected LOD (AP +0x14c)
// 0e56bb  a1 <0x5a9014>         MOV  EAX,[placeholder entry]     ; NONE_OCCL_DECAL, relocated
// 0e56c0  75 6f                 JNE  0x0e5731                    <- rel8 0x0e56c1: 6f -> 00 = JNE 0x0e56c2
// 0e56c2  89 45 d4              MOV  [EBP-0x2c],EAX              ; LOD-0 path: placeholder kept as the fallback
// 0x0e5731 (the placeholder bind) is reached only from this JNE; the flags are dead at 0x0e56c2 (first reader behind
// 0x0e56cb TEST); the byte lies in the aligned qword 0x0e56c0: one lock cmpxchg8b.
namespace x3m::fl::lod_patch_sites_fl {
struct Window {
    std::uint32_t rva;
    unsigned length;
    const unsigned char* bytes; // relocated dwords zeroed
    unsigned fixup_offset;      // 0 = none, else the window offset of the one relocated dword
    std::uint32_t fixup_rva;    // its value at the live base = base + fixup_rva
    unsigned edit_offset;
    unsigned char original, patched;
    std::uint32_t fnv1a; // of `bytes` (fixups zeroed), contract.json
};
constexpr unsigned max_window = 32;
// clang-format off
constexpr unsigned char terran_bytes[31] = {
    0x84,0xc0, 0x75,0x1b, 0x83,0x7e,0x18,0x00, 0x75,0x15, 0x83,0xbe,0x2c,0x01,0x00,0x00,0x00,
    0x0f,0xb6,0xc0, 0x0f,0x4c,0xc1, 0x89,0x44,0x24,0x1c, 0x88,0x44,0x24,0x24};
constexpr unsigned char occlusion_bytes[20] = {
    0x8b,0x45,0xe4, 0x83,0xb8,0x50,0x01,0x00,0x00,0x00, 0xa1,0x00,0x00,0x00,0x00, 0x75,0x6f, 0x89,0x45,0xd4};
// clang-format on
constexpr Window terran{0x095e82, 31, terran_bytes, 0, 0, 0x16, 0xc1, 0xc0, 0x0e8e0d62};
constexpr Window occlusion{0x0e56b1, 20, occlusion_bytes, 0x0b, 0x1a9014, 0x10, 0x6f, 0x00, 0x3105ccaa};
constexpr std::uint32_t terran_exit_rva = 0x095ea1, occlusion_lod0_rva = 0x0e56c2, occlusion_placeholder_rva = 0x0e5731,
                        occlusion_after_rva = 0x0e56c5;

constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
constexpr std::uint32_t u32(const unsigned char* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
// The install decision on the bytes read at base + w.rva: nullptr when they are exactly the engine's (the fixup equal to
// base + fixup_rva, the other bytes as listed), else "bytes_mismatch".
inline const char* plan(const Window& w, const unsigned char* current, std::uintptr_t base) {
    for (unsigned i = 0; i < w.length; ++i) {
        const bool fixup = w.fixup_offset && i >= w.fixup_offset && i < w.fixup_offset + 4;
        if (!fixup && current[i] != w.bytes[i]) return "bytes_mismatch";
    }
    if (w.fixup_offset && u32(current + w.fixup_offset) != std::uint32_t(base + w.fixup_rva)) return "bytes_mismatch";
    return nullptr;
}
// The write, verify, rollback and restore sequence over the one byte at base + w.rva + w.edit_offset, independent of the
// memory API (the AP sites headers' shape). Ops provides read/protect/write and `writable`, as there. install() returns
// "ok" or the reason; *live says whether the patched byte may be in place (true after "ok" and "rollback_failed": the
// caller keeps it registered); *protection is the page protection found before the write.
template <class Ops>
inline const char* install(Ops& ops, const Window& w, std::uintptr_t base, bool window_open, bool* live, bool* atomic,
                           std::uint32_t* protection) {
    *live = false;
    *atomic = false;
    unsigned char current[max_window]{};
    if (!base || (base & 0xffff)) return "invalid_site";
    if (!window_open) return "late_claim";
    if (!ops.read(base + w.rva, current, w.length)) return "unreadable";
    if (const char* refused = plan(w, current, base)) return refused;
    const std::uintptr_t at = base + w.rva + w.edit_offset;
    if (!ops.protect(at, 1, Ops::writable, protection)) return "protect_failed";
    *live = true; // ownership published before the mutation, so a failed rollback stays registered
    const bool flushed = ops.write(at, &w.patched, 1, atomic);
    unsigned char readback = 0;
    const bool verified = flushed && ops.read(at, &readback, 1) && readback == w.patched;
    std::uint32_t unused = 0;
    const bool protected_again = ops.protect(at, 1, *protection, &unused);
    if (verified && protected_again) return "ok";
    std::uint32_t writable = 0;
    bool reprotected = false, ignored = false;
    if (!protected_again || ops.protect(at, 1, Ops::writable, &writable)) {
        ops.write(at, &w.original, 1, &ignored);
        reprotected = ops.protect(at, 1, *protection, &unused);
    }
    unsigned char back = 0;
    if (ops.read(at, &back, 1) && back == w.original) {
        *live = false;
        return reprotected ? "patch_rolled_back" : "rollback_unprotected";
    }
    return "rollback_failed";
}
// Puts the original byte back only over the patched one (engine_patch::restore's rule): "restored" when it held the
// patched byte and reads the original afterwards (or already held the original: no write), "restore_not_owned" when it
// held anything else or was unreadable (nothing written), "restore_failed" when the protection or the store failed.
template <class Ops>
inline const char* restore(Ops& ops, const Window& w, std::uintptr_t at, std::uint32_t protection, unsigned char* found,
                           bool* found_read) {
    *found_read = ops.read(at, found, 1);
    if (*found_read && *found == w.original) return "restored";
    if (!*found_read || *found != w.patched) return "restore_not_owned";
    std::uint32_t previous = 0, unused = 0;
    bool ignored = false;
    if (!ops.protect(at, 1, Ops::writable, &previous)) return "restore_failed";
    const bool flushed = ops.write(at, &w.original, 1, &ignored);
    unsigned char back = 0;
    const bool verified = flushed && ops.read(at, &back, 1) && back == w.original;
    const bool protected_again = ops.protect(at, 1, protection, &unused);
    return verified && protected_again ? "restored" : "restore_failed";
}
static_assert(fnv1a(terran_bytes, 31) == terran.fnv1a && fnv1a(occlusion_bytes, 20) == occlusion.fnv1a,
              "the verified window identities (contract.json; the occlusion fixup zeroed)");
static_assert(terran_bytes[terran.edit_offset] == terran.original && terran_bytes[terran.edit_offset - 2] == 0x0f &&
                  terran_bytes[terran.edit_offset - 1] == 0x4c,
              "the edit is CMOVL's ModRM");
static_assert(occlusion_bytes[occlusion.edit_offset] == occlusion.original &&
                  occlusion_bytes[occlusion.edit_offset - 1] == 0x75,
              "the edit is JNE's rel8");
static_assert(terran.rva + terran.edit_offset == 0x095e98 && occlusion.rva + occlusion.edit_offset == 0x0e56c1 &&
                  occlusion.rva + occlusion.fixup_offset == 0x0e56bc,
              "the verified RVAs");
static_assert(occlusion.rva + occlusion.edit_offset + 1 + occlusion.original == occlusion_placeholder_rva &&
                  occlusion.rva + occlusion.edit_offset + 1 + occlusion.patched == occlusion_lod0_rva,
              "jne 0x0e5731 -> jne 0x0e56c2");
static_assert(terran.rva + terran.length == terran_exit_rva && occlusion.rva + occlusion.length == occlusion_after_rva,
              "window ends");
}
