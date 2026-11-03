#pragma once
#include <cstdint>
#include <cstring>

// Identity windows of the FL music family (media_cue_fl.cpp, music_keep_fl.cpp, music_alert_fl.cpp): whole-instruction
// spans of X3FL.exe identified by FNV-1a 32 over their bytes with every base-relocation HIGHLOW dword zeroed; at
// install each such dword must equal base + its target RVA (the loader rebased it), the rest must hash to the recorded
// value. The tables are generated from the exe (music_windows_fl_inc.h,
// verification/results/fl-music/generate_windows.py; checked by verify_{media_cue,music_keep,music_alert}_sites_fl.py).
// No Windows dependency: the host tests and the native fixtures compile it.
namespace x3m::fl::music_windows {
struct CodeWindow {
    const char* name; // the refusal: <name> (the rows print <name>_mismatch)
    std::uint32_t rva;
    unsigned length;
    unsigned first, count; // its fixups: table[first .. first+count)
    std::uint32_t fnv;
};
struct Fixup {
    unsigned offset;    // in the window
    std::uint32_t rva;  // the dword holds base + rva
};
#include "music_windows_fl_inc.h"
constexpr unsigned max_window = 0x300;

constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n, std::uint32_t h = 0x811c9dc5u) {
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
inline std::uint32_t le32(const unsigned char* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
// The decision over the bytes read at base + w.rva: true when every fixup dword is base + its target and the masked
// bytes hash to w.fnv.
inline bool matches(const CodeWindow& w, const Fixup* fixups, const unsigned char* current, std::uintptr_t base) {
    if (w.length > max_window) return false;
    unsigned char masked[max_window];
    std::memcpy(masked, current, w.length);
    for (unsigned k = 0; k < w.count; ++k) {
        const Fixup& f = fixups[w.first + k];
        if (f.offset + 4 > w.length || le32(masked + f.offset) != std::uint32_t(base + f.rva)) return false;
        std::memset(masked + f.offset, 0, 4);
    }
    return fnv1a(masked, w.length) == w.fnv;
}
// Every window of a table: nullptr when all match, else the first window's name. `read(at, out, n)` reads code bytes.
template <class Read, unsigned N>
inline const char* first_mismatch(Read&& read, std::uintptr_t base, const CodeWindow (&windows)[N],
                                  const Fixup* fixups) {
    for (const CodeWindow& w : windows) {
        unsigned char now[max_window];
        if (w.length > max_window || !read(base + w.rva, now, w.length) || !matches(w, fixups, now, base))
            return w.name;
    }
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
// A five-byte `call rel32` at `at` targeting `target` (both absolute).
inline bool calls(const unsigned char* code, std::uint32_t at, std::uint32_t target) {
    return code[0] == 0xe8 && std::uint32_t(at + 5 + le32(code + 1)) == target;
}
}
