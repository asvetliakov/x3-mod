// Micro-benchmark of the material-strength hot-path helpers (src/proxy/lod_texture_mark.h), i686 MinGW build run
// natively through WSL interop (docs/architecture/overlay-material-strength.md 8; the ledger's performance pass):
// the DDS header parse the texture hook does per load (unmarked and marked headers), the pointer-table probe the
// SetTexture hook does per bind (empty table, populated table miss, populated table hit) and the one-byte stage
// compare the route does per draw, each over 10^7 calls, QueryPerformanceCounter, ns per call. No device, nothing
// launched. build: i686-w64-mingw32-g++ -O2 -msse2 -mfpmath=sse -static mark_table_bench.cpp -o mark_table_bench.exe
#include "../../../src/proxy/lod_texture_mark.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace x3m::lod_texture_mark;
static LONGLONG freq;
static double ns(LONGLONG t0, LONGLONG t1, unsigned n) {
    return double(t1 - t0) * 1e9 / double(freq) / double(n);
}
static LONGLONG now() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}
volatile unsigned sink = 0;
int main() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    freq = f.QuadPart;
    const unsigned n = 10000000u;
    unsigned char unmarked[256] = {}, marked[256] = {};
    const auto put = [](unsigned char* h, unsigned at, std::uint32_t v) {
        for (unsigned i = 0; i < 4; ++i) h[at + i] = (v >> (8 * i)) & 255;
    };
    put(unmarked, 0, 0x20534444u);
    put(unmarked, 4, 124);
    std::memcpy(marked, unmarked, sizeof marked);
    put(marked, 32, magic);
    put(marked, 36, 1);
    put(marked, 40, 3u | 3u << 8 | 2u << 16 | 1u << 24);
    // 1024 header copies indexed per call so the compiler cannot hoist the parse out of the loop (throughput of
    // cache-resident calls, as the hook sees them).
    std::vector<unsigned char> headers(1024 * 256);
    for (unsigned i = 0; i < 1024; ++i) std::memcpy(&headers[i * 256], i & 1 ? marked : unmarked, 256);
    volatile unsigned size = 256;
    Mark m;
    LONGLONG t0 = now();
    for (unsigned i = 0; i < n; ++i) sink += parse(&headers[(i & 1022) * 256], size, m);
    LONGLONG t1 = now();
    std::printf("parse unmarked header: %.2f ns/call\n", ns(t0, t1, n));
    t0 = now();
    for (unsigned i = 0; i < n; ++i) sink += parse(&headers[((i & 1022) | 1) * 256], size, m) + lane_of(m);
    t1 = now();
    std::printf("parse marked header + lane_of: %.2f ns/call\n", ns(t0, t1, n));
    static Table<4096> table;
    std::vector<int> objects(4096);
    const void* probe = &objects[4000];
    t0 = now();
    for (unsigned i = 0; i < n; ++i) sink += table.count == 0 ? 0u : unsigned(table.find(probe) != nullptr);
    t1 = now();
    std::printf("set_texture probe, empty table (count check): %.2f ns/call\n", ns(t0, t1, n));
    for (unsigned i = 0; i < 1500; ++i) table.insert(&objects[i], 100 + i, lane_strength_alpha, 3);
    t0 = now();
    for (unsigned i = 0; i < n; ++i) sink += table.count == 0 ? 0u : unsigned(table.find(&objects[2000 + (i & 1023)]) != nullptr);
    t1 = now();
    std::printf("set_texture probe, 1500 entries, miss: %.2f ns/call\n", ns(t0, t1, n));
    t0 = now();
    for (unsigned i = 0; i < n; ++i) sink += table.count == 0 ? 0u : unsigned(table.find(&objects[i & 1023]) != nullptr);
    t1 = now();
    std::printf("set_texture probe, 1500 entries, hit: %.2f ns/call\n", ns(t0, t1, n));
    t0 = now();
    for (unsigned i = 0; i < 1500; ++i) table.erase(&objects[i]);
    t1 = now();
    std::printf("erase (final release) with 1500 entries: %.2f ns/call, left %u\n", ns(t0, t1, 1500), table.count);
    struct Shadow { std::uint8_t stage; std::uint8_t lane[16]; } shadow{0, {}};
    volatile std::uint8_t* stage = &shadow.stage;
    t0 = now();
    for (unsigned i = 0; i < n; ++i) sink += (*stage && shadow.lane[*stage] == lane_strength_alpha) ? 1u : 0u;
    t1 = now();
    std::printf("route gate (stage byte, 0 for a program without twins): %.2f ns/call\n", ns(t0, t1, n));
    std::printf("sink=%u\n", sink);
    return 0;
}
