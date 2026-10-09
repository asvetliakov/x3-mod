#pragma once
#include <cstdint>

// Accounting core of the address_space row (address_space.h): one call per region the
// VirtualQuery walk returns. Pure integer arithmetic over the documented MEMORY_BASIC_INFORMATION
// State/Type values, no Windows header, so the host harness compiles it
// (verification/analysis/test_address_space.py).
namespace x3m::address_space::core {
// winnt.h values (MEM_COMMIT, MEM_RESERVE, MEM_FREE, MEM_PRIVATE, MEM_MAPPED, MEM_IMAGE).
constexpr std::uint32_t mem_commit = 0x1000, mem_reserve = 0x2000, mem_free = 0x10000;
constexpr std::uint32_t mem_private = 0x20000, mem_mapped = 0x40000, mem_image = 0x1000000;
// The walk covers 0..0xFFFEFFFF, the user range of a large-address-aware 32-bit process on a
// 64-bit host (lpMaximumApplicationAddress); exclusive end 0xFFFF0000.
constexpr std::uint64_t walk_end = 0xFFFF0000ull;
// DXVK's host-visible chunk: 16 MiB, with up to 64 KiB of allocation-granularity slack.
constexpr std::uint64_t chunk16_min = 16ull << 20, chunk16_max = chunk16_min + (64ull << 10);
constexpr std::uint64_t big_min = 1ull << 20;

struct Totals {
    std::uint64_t span = 0; // bytes covered from 0 (walk_end when the walk completed)
    std::uint64_t regions = 0, free_total = 0, free_largest = 0, reserved = 0;
    std::uint64_t committed_private = 0, committed_mapped = 0, committed_image = 0, committed_other = 0;
    std::uint64_t chunk16_count = 0, chunk16_bytes = 0, big_private_count = 0, big_private_bytes = 0;
};

// Adds the region [base, base + size) clipped to walk_end and returns the next address to query,
// or 0 when the walk is done: end reached, an empty region, or a region that does not start at the
// covered end (the partition would break; the row then reports the shorter span). By construction
// free_total + reserved + committed_* == span.
inline std::uint64_t add(Totals& t, std::uint64_t base, std::uint64_t size, std::uint32_t state, std::uint32_t type) {
    if (size == 0 || base != t.span || base >= walk_end) return 0;
    if (size > walk_end - base) size = walk_end - base;
    ++t.regions;
    t.span = base + size;
    if (state == mem_free) {
        t.free_total += size;
        if (size > t.free_largest) t.free_largest = size;
    } else if (state == mem_reserve) {
        t.reserved += size;
    } else if (state == mem_commit && type == mem_private) {
        t.committed_private += size;
        if (size >= chunk16_min && size <= chunk16_max) {
            ++t.chunk16_count;
            t.chunk16_bytes += size;
        } else if (size >= big_min) {
            ++t.big_private_count;
            t.big_private_bytes += size;
        }
    } else if (state == mem_commit && type == mem_mapped) {
        t.committed_mapped += size;
    } else if (state == mem_commit && type == mem_image) {
        t.committed_image += size;
    } else {
        t.committed_other += size; // an undocumented state or committed type: kept so the sum holds
    }
    return t.span < walk_end ? t.span : 0;
}
}
