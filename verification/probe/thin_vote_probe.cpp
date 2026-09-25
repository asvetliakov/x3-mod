// Thin-vote probe (docs/architecture/taa-thin-geometry-alternatives.md section 3.2, "Implemented"): one Wine run of the
// production arithmetic built with the production flags (i686, SSE2, 4-byte incoming stack), no device.
//  - SLOTS: D3DXDisassembleShader's "instruction slots used" of every program the option changes: the two depth-folding
//    tests draws and their thin twins, the current-depth fragment and its twin, and the reviewed pair's pixel variant
//    (ps 8759c7838bbc86c2, with the depth fragment) with the option off and on.
//  - MEASURE: thin_vote::measure on a 10,000-triangle indexed grid (FLOAT16_4 stride 24 and FLOAT3 stride 40 positions),
//    the first call (cold: FEX translates the code) and the median of 200 warm calls.
//  - DRAW: the per-draw work of MotionOutput::thin_vote_alpha's core (key, cache lookup over 448 distinct subsets, the
//    projected scale from the rows, the window, the alpha) timed over 4,480,000 lookups.
// usage: thin_vote_probe.exe <d3dx9_37.dll> <ps_8759c7838bbc86c2.bin>
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../src/proxy/thin_vote_core.h"
#include "../../src/renderer/material_motion.h"
#include "../../src/renderer/temporal_resolve_program.h"
#include "../../src/renderer/current_depth_pixel_program.h"

namespace {
using Disassemble = HRESULT(WINAPI*)(const DWORD*, BOOL, LPCSTR, LPD3DXBUFFER*);
Disassemble disassemble = nullptr;
unsigned slots(const std::uint32_t* words, std::size_t count, const char* label, bool keep = false) {
    LPD3DXBUFFER text = nullptr;
    if (FAILED(disassemble(reinterpret_cast<const DWORD*>(words), FALSE, nullptr, &text)) || !text) throw std::runtime_error(label);
    const std::string listing(static_cast<const char*>(text->GetBufferPointer()), text->GetBufferSize());
    text->Release();
    if (keep) { // the whole listing beside the executable (untracked: it holds the game program); the runner keeps our fragments' lines only
        FILE* file = std::fopen((std::string(label) + ".asm").c_str(), "wb");
        if (!file) throw std::runtime_error(label);
        std::fwrite(listing.data(), 1, std::strlen(listing.c_str()), file); std::fclose(file);
    }
    const auto at = listing.find("instruction slots used");
    unsigned n = 0;
    if (at != std::string::npos) {
        auto begin = listing.rfind("approximately", at);
        if (begin != std::string::npos) n = unsigned(std::strtoul(listing.c_str() + begin + 13, nullptr, 10));
    }
    std::printf("SLOTS program=%s dwords=%u instruction_slots=%u\n", label, unsigned(count), n);
    return n;
}
double now_us() { LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return double(t.QuadPart) * 1e6 / double(f.QuadPart); }
unsigned short to_half(float f) {
    unsigned bits; std::memcpy(&bits, &f, 4);
    const unsigned sign = (bits >> 16) & 0x8000; const int e = int((bits >> 23) & 255) - 127 + 15;
    if (f == 0.f || e <= 0) return static_cast<unsigned short>(sign);
    return static_cast<unsigned short>(sign | (unsigned(e) << 10) | ((bits >> 13) & 1023));
}
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        if (argc != 3) throw std::runtime_error("usage: thin_vote_probe.exe <d3dx9_37.dll> <ps_8759c7838bbc86c2.bin>");
        HMODULE d3dx = LoadLibraryA(argv[1]);
        if (!d3dx) throw std::runtime_error("d3dx9_37.dll");
        disassemble = reinterpret_cast<Disassemble>(reinterpret_cast<void*>(GetProcAddress(d3dx, "D3DXDisassembleShader")));
        if (!disassemble) throw std::runtime_error("D3DXDisassembleShader");
        namespace r = x3m::renderer;
        slots(r::temporal_line_mask_depth_program(), std::size(r::temporal_line_mask_depth_program()), "line_mask_depth");
        slots(r::temporal_line_mask_depth_thin_program(), std::size(r::temporal_line_mask_depth_thin_program()), "line_mask_depth_thin");
        slots(r::temporal_resolve_far_camera_hold_program(), std::size(r::temporal_resolve_far_camera_hold_program()), "resolve_far_camera_hold"); // the camera gate's tests since the mask fold
        slots(r::current_depth_pixel_program(), std::size(r::current_depth_pixel_program()), "current_depth");
        slots(r::current_depth_thin_pixel_program(), std::size(r::current_depth_thin_pixel_program()), "current_depth_thin");
        {
            FILE* file = std::fopen(argv[2], "rb");
            if (!file) throw std::runtime_error("reviewed pixel program");
            std::vector<std::uint32_t> original(65536);
            const std::size_t count = std::fread(original.data(), 4, original.size(), file) ; std::fclose(file);
            original.resize(count);
            std::vector<std::uint32_t> off, on;
            r::material_motion_configure_thin_vote(false);
            if (r::material_motion_pixel_variant(original.data(), original.size(), off, true) != r::MaterialMotionResult::Applied) throw std::runtime_error("variant off");
            r::material_motion_configure_thin_vote(true);
            if (r::material_motion_pixel_variant(original.data(), original.size(), on, true) != r::MaterialMotionResult::Applied) throw std::runtime_error("variant on");
            r::material_motion_configure_thin_vote(false);
            slots(original.data(), original.size(), "ps_8759c7838bbc86c2_original", true);
            slots(off.data(), off.size(), "ps_8759c7838bbc86c2_variant_off", true);
            slots(on.data(), on.size(), "ps_8759c7838bbc86c2_variant_thin", true);
        }
        // MEASURE: a 100 x 50 quad grid = 10,000 triangles over 5,151 vertices.
        using namespace x3m::thin_vote;
        const unsigned nx = 100, ny = 50, vx = nx + 1, vy = ny + 1;
        std::vector<unsigned short> indices;
        for (unsigned y = 0; y < ny; ++y) for (unsigned x = 0; x < nx; ++x) {
            const unsigned short a = static_cast<unsigned short>(y * vx + x), b = static_cast<unsigned short>(a + 1), c = static_cast<unsigned short>(a + vx), d = static_cast<unsigned short>(c + 1);
            const unsigned short t[6] = {a, b, c, c, b, d}; indices.insert(indices.end(), t, t + 6);
        }
        for (unsigned layout = 0; layout < 2; ++layout) {
            const unsigned stride = layout ? 40u : 24u, type = layout ? 2u : 16u;
            std::vector<unsigned char> vertices(std::size_t(vx) * vy * stride);
            for (unsigned y = 0; y < vy; ++y) for (unsigned x = 0; x < vx; ++x) {
                const float p[3] = {float(x) * .01f, float(y) * .5f, float(x % 7) * .001f};
                unsigned char* v = &vertices[(std::size_t(y) * vx + x) * stride];
                if (type == 16) { const unsigned short h[4] = {to_half(p[0]), to_half(p[1]), to_half(p[2]), to_half(1.f)}; std::memcpy(v, h, 8); }
                else std::memcpy(v, p, 12);
            }
            Histogram h;
            const double cold_begin = now_us();
            const bool ok = measure(vertices.data(), vx * vy, stride, 0, type, indices.data(), false, 0, nx * ny * 2, h);
            const double cold = now_us() - cold_begin;
            std::vector<double> warm;
            for (unsigned i = 0; i < 200; ++i) {
                const double begin = now_us();
                measure(vertices.data(), vx * vy, stride, 0, type, indices.data(), false, 0, nx * ny * 2, h);
                warm.push_back(now_us() - begin);
            }
            std::sort(warm.begin(), warm.end());
            std::printf("MEASURE layout=%s triangles=%u vertices=%u ok=%d total=%u cold_us=%.1f warm_median_us=%.1f warm_p90_us=%.1f ns_per_triangle=%.1f\n",
                        type == 16 ? "float16_4_stride24" : "float3_stride40", nx * ny * 2, vx * vy, ok, h.total, cold, warm[100], warm[180], warm[100] * 1000.0 / double(nx * ny * 2));
        }
        // DRAW: 448 subsets, each looked up 10,000 times with a scale that moves per lookup.
        static Cache cache;
        std::vector<Key> keys(448);
        Histogram strut{};
        strut.e0 = -14; strut.total = 12; for (unsigned b = 0; b <= bins; ++b) strut.cumulative[b] = b == bins ? 1.f : 0.f;
        for (unsigned i = 0; i < keys.size(); ++i) {
            keys[i].vb = 1000 + i; keys[i].ib = 5000 + i; keys[i].stride = 24; keys[i].position_type = 16; keys[i].first = 6 * i; keys[i].primitives = 12;
            keys[i].vertex_count = 24; cache.store(keys[i], &strut);
        }
        float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 100};
        volatile float sink = 0.f; unsigned voted = 0;
        const unsigned rounds = 10000;
        const double begin = now_us();
        for (unsigned round = 0; round < rounds; ++round) {
            cache.begin_frame();
            rows[15] = 50.f + float(round % 200);
            for (const auto& k : keys) {
                Key key = k; // the per-draw key copy the route builds
                const Entry* e = cache.find(key);
                float alpha = 1.f, scale = 0.f;
                if (e && e->state == State::Known && log2_pixels_per_unit(rows, 5120.f, scale)) alpha = rt2_alpha(thin_fraction(e->histogram, scale));
                voted += alpha < 1.f; sink = sink + alpha;
            }
        }
        const double elapsed = now_us() - begin;
        std::printf("DRAW lookups=%u elapsed_us=%.1f ns_per_draw=%.2f voted=%u per_frame_448_us=%.2f\n", rounds * unsigned(keys.size()), elapsed,
                    elapsed * 1000.0 / double(rounds * keys.size()), voted, elapsed / double(rounds));
        // DRAW_COLD: each lookup timed alone (two QPC reads, as the DLL's draw_us) after 2 MiB of other memory was streamed
        // (untimed), so the table is cold as between two routed draws; the same with the stream skipped (warm).
        {
            std::vector<unsigned char> other(2u << 20, 1);
            unsigned sum = 0; const unsigned lookups = 4000;
            LARGE_INTEGER a{}, b{}, frequency{}; QueryPerformanceFrequency(&frequency);
            for (int pass = 0; pass < 2; ++pass) {
                long long ticks = 0;
                for (unsigned i = 0; i < lookups; ++i) {
                    if (!pass) for (std::size_t j = 0; j < other.size(); j += 64) sum += other[j]++;
                    Key key = keys[(i * 97u) % keys.size()];
                    QueryPerformanceCounter(&a);
                    const Entry* e = cache.find(key);
                    float alpha = 1.f, scale = 0.f;
                    if (e && e->state == State::Known && log2_pixels_per_unit(rows, 5120.f, scale)) alpha = rt2_alpha(thin_fraction(e->histogram, scale));
                    QueryPerformanceCounter(&b);
                    sink = sink + alpha; ticks += b.QuadPart - a.QuadPart;
                }
                std::printf("DRAW_COLD cache=%s lookups=%u ns_per_draw_with_qpc=%.1f qpc_frequency=%lld checksum=%u\n", pass ? "warm" : "cold", lookups,
                            double(ticks) * 1e9 / double(frequency.QuadPart) / double(lookups), (long long)frequency.QuadPart, sum & 0xffu);
            }
        }
        // DRAIN: a full cache (8,192 entries read through 2,048 VB wrappers and 512 IB wrappers), then one wrapper's
        // invalidation through the identity index (the drain's per-queued-wrapper work), re-stored and repeated; against
        // one whole-table scan (the former per-drain cost).
        {
            static Cache full;
            full.clear(); full.begin_frame();
            std::vector<Key> filled;
            for (std::uint64_t vb = 1; filled.size() < Cache::size && vb < 4000000; ++vb) {
                Key key; key.vb = vb; key.stride = 24; key.position_type = 16; key.primitives = 12;
                const std::uintptr_t vb_id = 0x100000 + 16 * (vb % 2048), ib_id = 0x900000 + 16 * (vb % 512);
                if (full.store(key, &strut, vb_id, ib_id)) filled.push_back(key);
                if ((vb & 4095) == 0) full.begin_frame();
            }
            unsigned live = 0; for (unsigned i = 0; i < Cache::size; ++i) live += full.entries[i].state != State::Empty;
            const unsigned rounds = 2000; unsigned dropped = 0;
            const double begin = now_us();
            for (unsigned r = 0; r < rounds; ++r) dropped += full.invalidate(0x100000 + 16 * ((r * 97u) % 2048));
            const double indexed = now_us() - begin;
            const double scan_begin = now_us(); unsigned scanned = 0;
            for (unsigned r = 0; r < 200; ++r) for (unsigned i = 0; i < Cache::size; ++i) scanned += full.entries[i].vb_identity == std::uintptr_t(0x100000 + 16 * r);
            const double scan = now_us() - scan_begin;
            std::printf("DRAIN entries=%u index_live=%u index_full=%d invalidations=%u dropped=%u ns_per_invalidation=%.1f full_scan_us=%.2f sink=%u\n", live,
                        full.index.live, full.index.full, rounds, dropped, indexed * 1000.0 / double(rounds), scan / 200.0, scanned);
        }
        // QPC: the fixture's draw_us brackets each routed draw's thin work with two QueryPerformanceCounter reads.
        {
            LARGE_INTEGER t{}; const unsigned calls = 1000000; const double qpc_begin = now_us();
            for (unsigned i = 0; i < calls; ++i) QueryPerformanceCounter(&t);
            const double qpc = now_us() - qpc_begin;
            std::printf("QPC calls=%u ns_per_call=%.2f\n", calls, qpc * 1000.0 / double(calls));
        }
        std::puts("THIN_VOTE_PROBE PASS");
        return 0;
    } catch (const std::exception& e) {
        std::printf("THIN_VOTE_PROBE FAIL %s\n", e.what());
        return 1;
    }
}
