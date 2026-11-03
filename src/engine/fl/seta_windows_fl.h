#pragma once
#include <cstdint>
#include <cstring>

// The identity-window form both FL SETA modules check at the loaded base before any write (seta_restore_sites_fl.h,
// seta_manual_flight_sites_fl.h): a span's bytes with every base-relocation HIGHLOW dword zeroed, its FNV-1a 32, and
// each zeroed dword's target RVA, which must read as base + target at install (the convention of
// chase_transition_sites_fl.h). The rows are emitted by the verifiers (--emit-windows) and compared back by their host
// tests. No Windows dependency (the native fixtures and the host tests compile it).
namespace x3m::fl::seta_windows_fl {
constexpr unsigned max_window = 320, max_fixups = 12;
struct Fixup {
    std::uint32_t offset, target_rva;
};
struct Window {
    const char* name;
    std::uint32_t rva, length, fnv1a, fixups;
    Fixup fixup[max_fixups];
    unsigned part; // seta_manual_flight: 1 = needed by part A (mode >= 1), 2 = by part B; seta_restore: 0 = always
    std::uint8_t bytes[max_window]; // relocated dwords zeroed
};
constexpr std::uint32_t fnv1a(const std::uint8_t* p, unsigned n) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
constexpr std::uint32_t le32(const std::uint8_t* b) {
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}
template <unsigned N> constexpr bool measured(const Window (&windows)[N]) {
    for (unsigned w = 0; w < N; ++w) {
        if (windows[w].length > max_window || windows[w].fixups > max_fixups ||
            fnv1a(windows[w].bytes, windows[w].length) != windows[w].fnv1a)
            return false;
        for (unsigned f = 0; f < windows[w].fixups; ++f)
            if (windows[w].fixup[f].offset + 4 > windows[w].length) return false;
    }
    return true;
}
// Whether window w carries a fixup at `offset` that targets `target_rva`.
constexpr bool has_fixup(const Window& w, std::uint32_t offset, std::uint32_t target_rva) {
    for (unsigned f = 0; f < w.fixups; ++f)
        if (w.fixup[f].offset == offset && w.fixup[f].target_rva == target_rva) return true;
    return false;
}
// Every window of the parts <= max_part at the loaded base. Read is bool(std::uintptr_t address, void* out, unsigned n).
// nullptr when all match (each fixup dword equal to base + its target RVA, the rest byte-exact); else the refusal
// (window_unreadable, fixup_mismatch, window_mismatch) with *failed the window index (N when all matched).
template <unsigned N, class Read>
const char* plan(const Window (&windows)[N], unsigned max_part, std::uintptr_t base, Read read, unsigned* failed) {
    *failed = N;
    if (!base || (base & 0xffff)) return "invalid_base";
    for (unsigned w = 0; w < N; ++w) {
        const Window& win = windows[w];
        if (win.part > max_part) continue;
        std::uint8_t now[max_window]{};
        *failed = w;
        if (!read(base + win.rva, now, win.length)) return "window_unreadable";
        for (unsigned f = 0; f < win.fixups; ++f) {
            if (le32(now + win.fixup[f].offset) != std::uint32_t(base + win.fixup[f].target_rva)) return "fixup_mismatch";
            std::memset(now + win.fixup[f].offset, 0, 4);
        }
        if (std::memcmp(now, win.bytes, win.length)) return "window_mismatch";
    }
    *failed = N;
    return nullptr;
}
} // namespace x3m::fl::seta_windows_fl
