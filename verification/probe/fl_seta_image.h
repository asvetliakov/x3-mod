#pragma once
// Shared by the two FL SETA native fixtures (fl_seta_manual_flight.cpp, fl_seta_restore.cpp): maps X3FL.exe (read
// from the path the host test passes; nothing is written to it) into a private 64 KB-aligned PAGE_EXECUTE_READWRITE
// block at its RVAs and applies its HIGHLOW relocations to that base, so every window, span and relocated operand
// holds the real relocated bytes a loaded X3FL.exe would. No engine code runs except the spans a fixture enters on
// purpose, each ending at a `jmp` the fixture writes after the install. A pristine copy restores fixture edits.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

struct FlImage {
    unsigned char* base = nullptr;
    std::uint32_t size = 0, timestamp = 0, relocated = 0;
    std::vector<unsigned char> pristine;
    std::uintptr_t at(std::uint32_t rva) const { return reinterpret_cast<std::uintptr_t>(base) + rva; }
    std::uint32_t va(std::uint32_t rva) const { return std::uint32_t(at(rva)); }
    void restore(std::uint32_t rva, std::uint32_t n) { std::memcpy(base + rva, pristine.data() + rva, n); }
    void restore_all() { std::memcpy(base, pristine.data(), size); }
    // `jmp target` (E9 rel32) at base + rva.
    void jump(std::uint32_t rva, const void* target) {
        unsigned char* p = base + rva;
        const std::int32_t rel = std::int32_t(reinterpret_cast<std::uintptr_t>(target) - (reinterpret_cast<std::uintptr_t>(p) + 5));
        p[0] = 0xe9;
        std::memcpy(p + 1, &rel, 4);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
    }
    std::uint32_t& u32(std::uint32_t rva) { return *reinterpret_cast<std::uint32_t*>(base + rva); }
};

inline bool map_fl_image(const char* path, FlImage& im) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> file;
    unsigned char chunk[65536];
    for (std::size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) file.insert(file.end(), chunk, chunk + n);
    std::fclose(f);
    if (file.size() < 0x400 || file[0] != 'M' || file[1] != 'Z') return false;
    std::uint32_t pe = 0;
    std::memcpy(&pe, &file[0x3c], 4);
    if (pe + 0xf8 > file.size() || std::memcmp(&file[pe], "PE\0\0", 4)) return false;
    std::uint16_t sections = 0, optional = 0;
    std::memcpy(&sections, &file[pe + 6], 2);
    std::memcpy(&optional, &file[pe + 20], 2);
    std::uint32_t preferred = 0, headers = 0, reloc_rva = 0, reloc_size = 0;
    std::memcpy(&im.timestamp, &file[pe + 8], 4);
    std::memcpy(&preferred, &file[pe + 24 + 28], 4);
    std::memcpy(&im.size, &file[pe + 24 + 56], 4);
    std::memcpy(&headers, &file[pe + 24 + 60], 4);
    std::memcpy(&reloc_rva, &file[pe + 24 + 96 + 5 * 8], 4);
    std::memcpy(&reloc_size, &file[pe + 24 + 96 + 5 * 8 + 4], 4);
    im.base = static_cast<unsigned char*>(VirtualAlloc(nullptr, im.size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!im.base || (reinterpret_cast<std::uintptr_t>(im.base) & 0xffff)) return false;
    std::memcpy(im.base, file.data(), headers);
    for (unsigned i = 0; i < sections; ++i) {
        const std::size_t s = pe + 24 + optional + i * 40;
        std::uint32_t vsize = 0, rva = 0, raw_size = 0, raw = 0;
        std::memcpy(&vsize, &file[s + 8], 4);
        std::memcpy(&rva, &file[s + 12], 4);
        std::memcpy(&raw_size, &file[s + 16], 4);
        std::memcpy(&raw, &file[s + 20], 4);
        const std::uint32_t n = raw_size < vsize || !vsize ? raw_size : vsize;
        if (rva + n > im.size || raw + n > file.size()) return false;
        std::memcpy(im.base + rva, &file[raw], n);
    }
    const std::uint32_t delta = std::uint32_t(reinterpret_cast<std::uintptr_t>(im.base)) - preferred;
    for (std::uint32_t o = reloc_rva; o + 8 <= reloc_rva + reloc_size;) {
        std::uint32_t page = 0, block = 0;
        std::memcpy(&page, im.base + o, 4);
        std::memcpy(&block, im.base + o + 4, 4);
        if (block < 8) break;
        for (std::uint32_t k = 0; k < (block - 8) / 2; ++k) {
            std::uint16_t e = 0;
            std::memcpy(&e, im.base + o + 8 + 2 * k, 2);
            if (e >> 12 != 3) continue;
            std::uint32_t v = 0;
            std::memcpy(&v, im.base + page + (e & 0xfff), 4);
            v += delta;
            std::memcpy(im.base + page + (e & 0xfff), &v, 4);
            ++im.relocated;
        }
        o += block;
    }
    im.pristine.assign(im.base, im.base + im.size);
    return true;
}
