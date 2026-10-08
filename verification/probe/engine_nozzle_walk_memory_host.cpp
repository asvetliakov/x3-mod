// Host cost of the node walk through the production reader (src/proxy/engine_memory.cpp compiled on the host against the
// mock <windows.h> of verification/analysis/test_engine_memory_shutdown.py, as engine_memory_host.cpp is; review S1 of
// docs/architecture/engine-nozzle-source.md): VirtualQuery over one committed region covering the image, GetTickCount
// constant (no stall), the copy a memcpy. The walk's 32-bit addresses are offset onto the host arena by the reader
// adapter (one add); engine_memory's region cache, span checks and tick sampling are the production code. Prints one
// JSON object: the per-root and per-part cost of 256 roots of 108 parts (4 jets each, scattered blocks) and the
// reader's query count (one per frame: the region re-validated once per next_frame()).
#include "engine_memory_under_test_inc.h"
#include "../../src/proxy/engine_nozzle_walk_core.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
namespace nz = x3m::engine_nozzle::core;
namespace fj = x3m::engine_far_jets::core;
namespace em = x3m::engine_memory;

namespace {
std::uintptr_t arena_begin = 0, arena_end = 0;
unsigned long long vq_calls = 0, copies = 0;
DWORD last_error = 0;
// The image at a 32-bit virtual base, its blocks a fixed permutation (every hop a cache miss).
constexpr std::uint32_t base = 0x10000000u;
constexpr unsigned ships = 256, parts = 108;
} // namespace

SIZE_T VirtualQuery(const void* address, MEMORY_BASIC_INFORMATION* info, SIZE_T length) {
    ++vq_calls;
    const auto a = reinterpret_cast<std::uintptr_t>(address);
    if (length < sizeof *info) {
        last_error = 24;
        return 0;
    }
    *info = {};
    if (a >= arena_begin && a < arena_end) {
        info->BaseAddress = reinterpret_cast<void*>(arena_begin);
        info->RegionSize = arena_end - arena_begin;
        info->State = MEM_COMMIT;
        info->Protect = PAGE_READWRITE;
        return sizeof *info;
    }
    last_error = 87;
    return 0;
}
DWORD GetTickCount() {
    return 1000;
}
DWORD GetLastError() {
    return last_error;
}
void SetLastError(DWORD value) {
    last_error = value;
}
void host_copy(void* out, const void* in, std::size_t size) {
    ++copies;
    std::memcpy(out, in, size);
}

static bool reader(void*, std::uint32_t address, void* out, unsigned size) {
    return em::read(arena_begin + (address - base), out, size);
}

int main() {
    const unsigned blocks = ships * (parts + 2);
    std::vector<std::uint8_t> bytes(std::size_t(blocks) * nz::node_bytes + 4096, 0);
    arena_begin = reinterpret_cast<std::uintptr_t>(bytes.data());
    arena_end = arena_begin + bytes.size();
    std::vector<std::uint32_t> slots(blocks);
    for (unsigned i = 0; i < blocks; ++i) slots[i] = base + 16 + i * nz::node_bytes;
    std::uint32_t state = 0x2545f491u;
    for (unsigned i = blocks - 1; i > 0; --i) {
        state = state * 1664525u + 1013904223u;
        const unsigned j = (state >> 8) % (i + 1);
        std::swap(slots[i], slots[j]);
    }
    const auto at = [&](std::uint32_t address) { return reinterpret_cast<std::uint32_t*>(bytes.data() + (address - base)); };
    std::vector<std::uint32_t> roots;
    unsigned slot = 0;
    for (unsigned s = 0; s < ships; ++s) {
        const std::uint32_t root = slots[slot++];
        std::vector<std::uint32_t> nodes;
        for (unsigned i = 0; i < parts; ++i) {
            const std::uint32_t a = slots[slot++];
            std::uint32_t* b = at(a);
            b[nz::parent_offset / 4] = root;
            b[nz::handle_offset / 4] = 0x1000 + s * parts + i;
            b[nz::scale70_offset / 4] = 40;
            b[nz::scale80_offset / 4] = b[nz::scale84_offset / 4] = nz::scale_one;
            b[nz::scale88_offset / 4] = 0x4000;
            b[0xc0 / 4] = b[0xd0 / 4 + 1] = b[0xe0 / 4 + 2] = nz::scale_one;
            b[nz::flags130_offset / 4] = i % 27 == 0 ? nz::jet_pair : 0u;
            b[nz::model_offset / 4] = 20000;
            nodes.push_back(a);
        }
        const std::uint32_t sentinel = slots[slot++];
        *at(sentinel) = 0;
        *at(root + nz::child_offset) = nodes[0];
        for (unsigned i = 0; i + 1 < parts; ++i) *at(nodes[i]) = nodes[i + 1];
        *at(nodes[parts - 1]) = sentinel;
        roots.push_back(root);
    }
    volatile nz::Reader opaque = &reader;
    auto seen = std::make_unique<fj::Seen>();
    const unsigned rounds = 40;
    double best = 1e30, total = 0;
    unsigned long long emitted = 0, children = 0, unreadable = 0;
    for (unsigned r = 0; r < rounds; ++r) {
        em::next_frame(); // a Present: the region is re-validated at its first touch of the frame
        seen->begin();
        const auto t0 = std::chrono::steady_clock::now();
        for (std::uint32_t root : roots) {
            nz::WalkStats st;
            nz::walk(root, 9, opaque, nullptr, &st, [&](const fj::Raw& raw) { emitted += seen->insert(raw.handle, raw.view_handle); });
            children += st.children;
            unreadable += st.unreadable;
        }
        const double dt = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        total += dt;
        best = dt < best ? dt : best;
    }
    const em::Stats st = em::stats();
    const bool ok = emitted == rounds * ships * 4ull && children == rounds * ships * parts && unreadable == 0 && st.rejected == 0;
    std::printf("{\"ok\":%u,\"roots\":%u,\"children\":%u,\"jets_per_root\":4,\"us_per_root\":%.3f,\"us_per_root_mean\":%.3f,\"ns_per_part\":%.1f,"
                "\"us_per_frame_5_ships\":%.3f,\"reads\":%llu,\"queries\":%llu,\"vq_calls\":%llu,\"copies\":%llu,\"rounds\":%u}\n",
                unsigned(ok), ships, parts, best / ships, total / rounds / ships, best / ships * 1000. / parts, 5 * best / ships,
                static_cast<unsigned long long>(st.reads), static_cast<unsigned long long>(st.queries), vq_calls, copies, rounds);
    return ok ? 0 : 1;
}
