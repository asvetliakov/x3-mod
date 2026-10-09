#pragma once

// address_space row (--perf / --debug; docs/architecture/logging-tiers.md): the 32-bit process
// address space, for the DXVK 4 GB wall (host-visible 16 MiB chunks that Wine's wow64 path places
// below 4 GB). One row right after CreateDevice (when=create, one walk) and one per complete pass
// of the Present walk (when=present), on the calling thread:
//   address_space device= frame= when= total_virtual= avail_virtual= span= regions= free_total=
//     free_largest= reserved= committed_private= committed_mapped= committed_image= committed_other=
//     chunk16_count= chunk16_bytes= big_private_count= big_private_bytes= capped= ticks= us=
// total_virtual/avail_virtual from GlobalMemoryStatusEx at emission; the rest from a VirtualQuery walk
// over 0..0xFFFEFFFF (address_space_core.h): free_total + reserved + committed_* == span, which is
// 0xFFFF0000 unless VirtualQuery refused a query. The Present walk is resumable: a tick every 30 frames
// walks at most tick_budget_us from where the previous tick stopped, and the row is emitted when the
// pass reaches the end (ticks= ticks it took, us= summed walk time; regions seen on different ticks
// are from moments up to a pass apart). 0.46 us per region on the X3 fixture (measured), so a tick
// covers about 4,300 regions and a pass of N regions takes ceil(N / 4,300) x 30 frames: the fixture's
// 96,090 regions took 22-23 ticks = 660-690 frames, about 11.5 s at 60 fps (for that size; the game near
// the wall may hold more regions, the time scales linearly). Without a QPC frequency the budget falls
// back to tick_region_limit / create_region_limit regions. capped=1 only on a create row that hit
// create_budget_us; present rows always say capped=0. chunk16_* are committed MEM_PRIVATE regions of
// 16 MiB..16 MiB+64 KiB; big_private_* the other committed private regions >= 1 MiB. Bytes as unsigned
// 64-bit decimals. Documented kernel32 only, no allocation, LastError preserved.
namespace x3m::address_space {
constexpr unsigned long tick_budget_us = 2000, create_budget_us = 200000;
constexpr unsigned tick_region_limit = 4096, create_region_limit = 409600;
void report_create(unsigned long long device, unsigned long long frame);
// One Present tick of the resumable pass; true when it emitted the pass's row.
bool tick(unsigned long long device, unsigned long long frame);
}
