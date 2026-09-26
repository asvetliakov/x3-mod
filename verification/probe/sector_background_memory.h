#pragma once
#include "sector_background.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>
#include <vector>
namespace sb = x3m::sector_background;
inline unsigned checks = 0;
inline void check(bool v) {
    ++checks;
    if (!v) {
        std::fprintf(stderr, "FAIL check=%u\n", checks);
        std::exit(1);
    }
}
struct Memory {
    std::map<std::uint32_t, unsigned char> bytes;
    std::vector<std::pair<std::uint32_t, std::size_t>> requested;
    void word(std::uint32_t at, std::uint32_t n) {
        for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<unsigned char>(n >> (8 * i));
    }
    void block(std::uint32_t at, unsigned n) {
        for (unsigned i = 0; i < n; ++i) bytes[at + i] = 0;
    }
    bool operator()(std::uintptr_t at, void* out, std::size_t n) {
        requested.emplace_back(std::uint32_t(at), n);
        for (std::size_t i = 0; i < n; ++i)
            if (!bytes.count(std::uint32_t(at + i))) return false;
        for (std::size_t i = 0; i < n; ++i) static_cast<unsigned char*>(out)[i] = bytes[std::uint32_t(at + i)];
        return true;
    }
};
inline Memory setup() {
    Memory m;
    m.word(0x608504, 0x1000);
    m.word(0x1000, 0x2000);
    m.word(0x1010, 7);
    m.word(0x2000, 0x3000);
    m.word(0x2004, 8);
    m.word(0x301c, 0x4000);
    m.word(0x4000, 0);
    m.word(0x4004, 7);
    m.word(0x4008, 0x5000);
    m.word(0x500c, 0);
    m.word(0x5054, 0x7000);
    m.word(0x5058, 0x6000);
    m.word(0x7048, 1);
    m.word(0x713c, 0);
    m.word(0x7140, 12);
    m.word(0x7144, 13);
    m.word(0x606fc0, 0x10000);
    m.word(0x607040, 83);
    m.block(0x10044, 0x120);
    m.word(0x10044, 0x20001); // string need not be aligned
    m.word(0x10134, 8);
    m.word(0x10148, 18000000);
    m.word(0x1014c, 18500000);
    m.word(0x10150, 25);
    for (unsigned i = 0; i < 8; ++i) m.word(0x10114 + 4 * i, i + 1);
    m.block(0x20001, 32);
    const char name[] = "bluewell";
    for (unsigned i = 0; i < sizeof name; ++i) m.bytes[0x20001 + i] = name[i];
    m.word(0x6270, 0x10000);
    m.word(0x636c, 18000000);
    m.word(0x6370, 18500000);
    m.word(0x606f34, 0x30000);
    m.word(0x30768, 3);
    return m;
}
