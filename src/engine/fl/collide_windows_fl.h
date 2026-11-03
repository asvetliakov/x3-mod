#pragma once
#include <cstdint>
#include <cstring>

// Identity windows of the FL collision trio (collide_sat_sse2_fl.cpp, collide_memo_fl.cpp, collide_box_cull_fl.cpp):
// whole-instruction spans of X3FL.exe identified by FNV-1a 32 over their bytes with every base-relocation HIGHLOW dword
// zeroed; at install each such dword must equal base + its target RVA (the loader rebased it), the rest must hash to the
// recorded value. The tables are generated from the exe (collide_windows_fl_inc.h,
// verification/results/fl-collision/generate_windows.py; checked by verify_collide_sat_fl.py,
// verify_collide_memo_sites_fl.py and verify_collide_box_cull_sites_fl.py). The shape is code_window_fl.h's; the memo
// pins whole engine bodies up to 0x1430 bytes, so the decision hashes in place (no window-sized copy). Cold install path
// only. No Windows dependency: the host tests and the native fixtures compile it.
namespace x3m::fl::collide_windows {
struct CodeWindow {
    const char* name; // the refusal: <name>_mismatch in the rows
    std::uint32_t rva;
    unsigned length;
    unsigned first, count; // its fixups: table[first .. first+count)
    std::uint32_t fnv;
};
struct Fixup {
    unsigned offset;   // in the window
    std::uint32_t rva; // the dword holds base + rva
};
#include "collide_windows_fl_inc.h"
constexpr unsigned max_window = 0x1430;

inline std::uint32_t le32(const unsigned char* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
// The decision over the bytes read at base + w.rva: true when every fixup dword is base + its target and the bytes,
// those dwords counted as zero, hash to w.fnv. The fixups of a window are sorted by offset (the generator emits them so).
inline bool matches(const CodeWindow& w, const Fixup* fixups, const unsigned char* current, std::uintptr_t base) {
    if (w.length > max_window) return false;
    std::uint32_t h = 0x811c9dc5u;
    unsigned next = 0;
    for (unsigned i = 0; i < w.length;) {
        if (next < w.count && fixups[w.first + next].offset == i) {
            const Fixup& f = fixups[w.first + next];
            if (f.offset + 4 > w.length || le32(current + i) != std::uint32_t(base + f.rva)) return false;
            for (unsigned k = 0; k < 4; ++k) h = (h ^ 0u) * 0x01000193u;
            i += 4;
            ++next;
            continue;
        }
        h = (h ^ current[i]) * 0x01000193u;
        ++i;
    }
    return next == w.count && h == w.fnv;
}
// Every window of a table: nullptr when all match, else the first window's name. `read(at, out, n)` reads code bytes
// into a buffer of at least max_window bytes.
template <class Read, unsigned N>
inline const char* first_mismatch(Read&& read, std::uintptr_t base, const CodeWindow (&windows)[N],
                                  const Fixup* fixups, unsigned char* buffer) {
    for (const CodeWindow& w : windows)
        if (w.length > max_window || !read(base + w.rva, buffer, w.length) || !matches(w, fixups, buffer, base))
            return w.name;
    return nullptr;
}
template <unsigned N> constexpr const CodeWindow* find(const CodeWindow (&windows)[N], const char* name) {
    for (const CodeWindow& w : windows) {
        const char* a = w.name;
        const char* b = name;
        while (*a && *a == *b) ++a, ++b;
        if (*a == *b) return &w;
    }
    return nullptr;
}
}
