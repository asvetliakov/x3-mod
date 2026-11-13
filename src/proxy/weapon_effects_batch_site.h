#pragma once
#include <cstdint>

// The AP bullet batch submit call, the bullet re-shade's scope site (X3M_WEAPON_EFFECTS=reshade;
// docs/reverse-engineering/ap-bullet-batch-site.md section 1, the twin of FL's 0x097af8 -> 0x0e0b40 in
// docs/reverse-engineering/fl/bullet-draw-batch.md sections 5 and 6). X3AP.exe SHA-256 fdbf3418...f8ab, preferred VAs
// (the EXE is not relocatable). No Windows dependency: the host tests and the hook fixture compile it. The window is
// compared whole before any write (fail closed); bytes read from the installed EXE by
// verification/results/ap-bullet-batch-site/ap_bullet_batch_site.py (FNV-1a 32 0x949efb9a) and again by the hook
// fixture's build audit (verification/probe/build_weapon_effects_hook.py batch_window_exe).
//
// The flush 0x0046d080 (stdcall scene, view; EBX = view, EBP = scene, EDI = batch, ESI = material), its inner loop:
// 0046d0a3  53                 PUSH EBX                        ; view     ([esp+0x10] at the callee)
// 0046d0a4  56                 PUSH ESI                        ; material ([esp+0x0c])
// 0046d0a5  57                 PUSH EDI                        ; batch    ([esp+0x08]; its body id at +0x0c)
// 0046d0a6  55                 PUSH EBP                        ; scene    ([esp+0x04])
// 0046d0a7  e8 94 2c 05 00     CALL 0x004bfd40                 <- the site (cdecl, 4 dwords, plain ret; no register input)
// 0046d0ac  8b 36              MOV ESI,[ESI]                   ; the next material: ESI live across the call
// 0046d0ae  83 c4 10           ADD ESP,0x10                    ; the caller pops the four words (EFLAGS rewritten)
// 0046d0b1  83 3e 00           CMP [ESI],0
// 0046d0b4  75 ed              JNE 0x0046d0a3                  <- end of the window (19 bytes)
// EBX, ESI, EDI and EBP are live after the call and the callee's to preserve; EAX, ECX and EDX are dead (no reader in
// the flush, ECX reloaded by its pop ecx). The only direct branch into the window targets its first byte (the jne); no
// image dword names the call bytes. The callee has no SEH frame, uses x87 and `rep movsd`, and runs on the render
// thread only. The five bytes 0x0046d0a7..ab straddle the aligned qword 0x0046d0a8: claim_call's plain copy, inside the
// install window only (it closes at the first Present, before any device or flush exists).
namespace x3m::weapon_effects::batch_site {
constexpr unsigned call_length = 5;
constexpr std::uintptr_t window_va = 0x0046d0a3, site_va = 0x0046d0a7, target_va = 0x004bfd40;
constexpr std::uintptr_t return_va = 0x0046d0ac, window_end_va = 0x0046d0b6;
constexpr unsigned window_length = 19, site_offset = 4;
constexpr unsigned char expected_window[window_length] = {0x53, 0x56, 0x57, 0x55, 0xe8, 0x94, 0x2c, 0x05, 0x00, 0x8b,
                                                          0x36, 0x83, 0xc4, 0x10, 0x83, 0x3e, 0x00, 0x75, 0xed};
constexpr std::uint32_t window_fnv1a32 = 0x949efb9au;
// The callee's stack words at stub entry ([esp + 4 * slot]): the batch record is word 2; its body id at +0x0c (the
// record the lookup 0x0046ca80 keys on node +0x140).
constexpr unsigned scene_slot = 1, batch_slot = 2, material_slot = 3, view_slot = 4, words = 4;
constexpr unsigned batch_body = 0x0c;

constexpr std::uint32_t rel32_at(const unsigned char* b) {
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}
constexpr std::uint32_t fnv1a32(const unsigned char* p, unsigned n, std::uint32_t h = 0x811c9dc5u) {
    return n ? fnv1a32(p + 1, n - 1, (h ^ p[0]) * 0x01000193u) : h;
}

static_assert(window_va + site_offset == site_va && site_va + call_length == return_va &&
                  window_va + window_length == window_end_va,
              "the window holds the four pushes, the call, the next-material load, the pop and the loop");
static_assert(expected_window[0] == 0x53 && expected_window[1] == 0x56 && expected_window[2] == 0x57 &&
                  expected_window[3] == 0x55,
              "push ebx (view); push esi (material); push edi (batch); push ebp (scene): four stack words, no register");
static_assert(expected_window[site_offset] == 0xe8 &&
                  std::uint32_t(return_va + rel32_at(expected_window + site_offset + 1)) == target_va,
              "CALL 0x004bfd40");
static_assert(expected_window[9] == 0x8b && expected_window[10] == 0x36,
              "mov esi,[esi]: ESI live after the call (the callee preserves it)");
static_assert(expected_window[11] == 0x83 && expected_window[12] == 0xc4 && expected_window[13] == 0x10,
              "the caller pops the four words (cdecl)");
static_assert(expected_window[17] == 0x75 && std::uint32_t(window_end_va + std::int8_t(expected_window[18])) == window_va,
              "jne 0x0046d0a3: the loop's branch targets the window's first byte, not the call");
static_assert(fnv1a32(expected_window, window_length) == window_fnv1a32, "the window's FNV-1a 32 (the producer's)");
static_assert(((site_va & 7u) + call_length) > 8u, "the call straddles a qword (plain write, install window)");
} // namespace x3m::weapon_effects::batch_site
