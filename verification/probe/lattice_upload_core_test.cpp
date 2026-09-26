#define X3M_LATTICE_UPLOAD_FIXTURE
#include "../../src/ownership/clone_upload_core.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace x3m::ownership::clone_upload;
namespace {
unsigned checks = 0, cases = 0;
void check(bool value) {
    ++checks;
    if (!value) {
        std::cerr << "check failed: " << checks << '\n';
        std::abort();
    }
}
struct Fixture {
    std::unique_ptr<Store> store{new Store};
    DeviceGuard device{10, 4, 5, false, false, false, true};
    Identity sources[2]{{100, 2}, {101, 3}};
    Identity targets[2]{{200, 1}, {201, 1}};
    std::vector<unsigned char> payload[2];
    std::uint64_t invocation = 0;
    unsigned slot = 0;
    explicit Fixture(unsigned value = 0)
        : slot(value) {
        for (unsigned k = 0; k < 2; ++k) {
            payload[k].resize(bytes_for(slot, static_cast<Buffer>(k)));
            for (std::size_t i = 0; i < payload[k].size(); ++i)
                payload[k][i] = static_cast<unsigned char>((i * 37u + k * 13u + 19u) & 255u);
        }
    }
    void begin() {
        invocation = store->begin(slot, device, sources[0], sources[1]);
        check(invocation != 0);
        for (unsigned k = 0; k < 2; ++k) {
            Creation c{
                {targets[k].allocation, 0}, static_cast<std::uint32_t>(payload[k].size()), 0, 8, true, k == 1, true};
            check(store->created(invocation, static_cast<Buffer>(k), device, c));
        }
    }
    void map(Buffer kind) {
        auto k = static_cast<unsigned>(kind);
        check(store->mapped(invocation, kind, device, targets[k], payload[k].data(), k + 7, 0, 0, 0x800, true));
        check(store->source_lock(invocation, kind, device, sources[k], 0, 0, 0x810, true));
    }
    MapGuard guard(Buffer kind) const {
        auto k = static_cast<unsigned>(kind);
        return {device, targets[k], payload[k].data(), k + 7, 1, 0, 1, true, true, false};
    }
    FinalGuard final() const { return {device, {targets[0], targets[1]}, true, true, true}; }
    void close(Buffer kind) {
        auto k = static_cast<unsigned>(kind);
        // Actual D3DX closes the index source before destination, but closes
        // the vertex source after destination. Exercise both exact orders.
        if (kind == Buffer::Index) check(store->source_unlock(invocation, kind, device, sources[k], true));
        check(store->stage(invocation, kind, guard(kind)));
        check(!store->record(slot).producer_payload_valid);
        std::vector<unsigned char> out(payload[k].size(), 0x55);
        check(!store->copy_retained(slot, kind, final(), out.data(), out.size()));
        check(std::all_of(out.begin(), out.end(), [](unsigned char c) { return c == 0x55; }));
        check(store->unlocked(invocation, kind, device, targets[k], true, true));
        if (kind == Buffer::Vertex) check(store->source_unlock(invocation, kind, device, sources[k], true));
    }
    void all() {
        begin();
        map(Buffer::Index);
        close(Buffer::Index);
        map(Buffer::Vertex);
        close(Buffer::Vertex);
    }
    void erased() {
        check(!store->record(slot).producer_payload_valid);
        check(!store->active());
        check(store->discarded_arena_is_zero());
    }
};
void success() {
    for (unsigned slot = 0; slot < 2; ++slot) {
        Fixture f(slot);
        f.all();
        check(f.store->finish(f.invocation, true, f.final()));
        const auto r = f.store->record(slot);
        check(r.producer_payload_valid);
        check(r.invocation == f.invocation);
        check(r.buffers[0] == f.targets[0]);
        check(r.buffers[1] == f.targets[1]);
        check(f.store->statistics().staged_bytes == f.payload[0].size() + f.payload[1].size());
        for (unsigned k = 0; k < 2; ++k) {
            std::vector<unsigned char> out(f.payload[k].size() + 2, 0x55);
            check(f.store->copy_retained(slot, static_cast<Buffer>(k), f.final(), out.data() + 1, f.payload[k].size()));
            check(out.front() == 0x55 && out.back() == 0x55);
            check(std::equal(f.payload[k].begin(), f.payload[k].end(), out.begin() + 1));
            // Later native source storage changes cannot mutate retained CPU bytes.
            std::fill(f.payload[k].begin(), f.payload[k].end(), 0);
            std::vector<unsigned char> again(out.size() - 2);
            check(f.store->copy_retained(slot, static_cast<Buffer>(k), f.final(), again.data(), again.size()));
            check(std::equal(again.begin(), again.end(), out.begin() + 1));
        }
        f.store->invalidate(f.targets[1].allocation);
        f.erased();
        ++cases;
    }
}
void incomplete_oom() {
    Fixture f;
    f.begin();
    f.map(Buffer::Index);
    // Model the observed private attribute OOM after both Lock successes,
    // before first producer store. Safe raw access does not certify payload.
    std::fill(f.payload[1].begin(), f.payload[1].end(), 0xa7);
    f.close(Buffer::Index);
    check(f.store->statistics().staged_bytes == f.payload[1].size());
    check(!f.store->finish(f.invocation, false, f.final()));
    f.erased();
    check(f.store->statistics().publications == 0);
    check(f.store->statistics().wiped_bytes >= f.payload[1].size());
    ++cases;
}
void source_failure() {
    for (bool observed : {false, true}) {
        Fixture f;
        f.begin();
        check(f.store->mapped(f.invocation, Buffer::Index, f.device, f.targets[1], f.payload[1].data(), 8, 0, 0, 0x800,
                              true));
        if (observed)
            check(!f.store->source_lock(f.invocation, Buffer::Index, f.device, f.sources[1], 0, 0, 0x810, false));
        check(!f.store->stage(f.invocation, Buffer::Index, f.guard(Buffer::Index)));
        check(f.store->statistics().staged_bytes == 0);
        check(!f.store->finish(f.invocation, false, f.final()));
        f.erased();
        ++cases;
    }
}
void stage_discriminators() {
    // Each changes one live CPU fact AFTER successful map/source callbacks.
    for (unsigned variant = 0; variant < 15; ++variant) {
        Fixture f;
        f.begin();
        f.map(Buffer::Index);
        auto g = f.guard(Buffer::Index);
        switch (variant) {
        case 0: ++g.device.generation; break;
        case 1: g.device.resetting = true; break;
        case 2: g.device.lost = true; break;
        case 3: g.device.retiring = true; break;
        case 4: ++g.device.thread; break;
        case 5: ++g.identity.allocation; break;
        case 6: ++g.identity.revision; break;
        case 7: g.pointer = f.payload[0].data(); break;
        case 8: ++g.lock_serial; break;
        case 9: ++g.pending; break;
        case 10: ++g.in_flight_locks; break;
        case 11: ++g.in_flight_unlocks; break;
        case 12: g.own_dispatch = false; break;
        case 13: g.authenticated = false; break;
        case 14: g.ambiguous = true; break;
        }
        check(!f.store->stage(f.invocation, Buffer::Index, g));
        check(f.store->statistics().staged_bytes == 0);
        check(!f.store->finish(f.invocation, true, f.final()));
        f.erased();
        ++cases;
    }
}
void creation_discriminators() {
    for (unsigned variant = 0; variant < 8; ++variant) {
        Fixture f;
        auto id = f.store->begin(0, f.device, f.sources[0], f.sources[1]);
        check(id != 0);
        Creation c{{f.targets[1].allocation, 0}, bytes_for(0, Buffer::Index), 0, 8, true, true, true};
        switch (variant) {
        case 0: c.actual_usage = 8; break;
        case 1: c.actual_usage = 0x200; break;
        case 2: c.managed = false; break;
        case 3: c.index16 = false; break;
        case 4: c.own_dispatch = false; break;
        case 5: --c.bytes; break;
        case 6: c.identity.allocation = f.sources[0].allocation; break;
        case 7: c.identity.revision = 1; break;
        }
        check(!f.store->created(id, Buffer::Index, f.device, c));
        f.store->abort(id);
        f.erased();
        ++cases;
    }
}
void closure_discriminators() {
    for (unsigned variant = 0; variant < 8; ++variant) {
        Fixture f;
        f.all();
        auto g = f.final();
        switch (variant) {
        case 0: ++g.buffers[0].allocation; break;
        case 1: ++g.buffers[1].revision; break;
        case 2: ++g.device.generation; break;
        case 3: g.authenticated = false; break;
        case 4: g.quiet = false; break;
        case 5: g.own_dispatch = false; break;
        case 6: f.store->refuse(Refusal::Reentry); break;
        case 7: f.store->reset(); break;
        }
        check(!f.store->finish(f.invocation, true, g));
        f.erased();
        ++cases;
    }
    Fixture f;
    f.begin();
    f.map(Buffer::Index);
    check(f.store->stage(f.invocation, Buffer::Index, f.guard(Buffer::Index)));
    check(!f.store->unlocked(f.invocation, Buffer::Index, f.device, f.targets[1], false, false));
    // Real D3DX ignores this Unlock error, so the original Clone may return S_OK.
    check(!f.store->finish(f.invocation, true, f.final()));
    f.erased();
    ++cases;
}
void nested_retry_abort() {
    Fixture f;
    f.begin();
    f.map(Buffer::Index);
    f.close(Buffer::Index);
    check(f.store->begin(1, f.device, f.sources[0], f.sources[1]) == 0);
    check(!f.store->finish(f.invocation, true, f.final()));
    f.erased();
    const auto previous = f.invocation;
    f.all();
    check(f.invocation > previous);
    f.store->abort(previous);
    check(f.store->active() == f.invocation); // stale unwind cannot touch successor
    f.store->abort(f.invocation);
    f.store->abort(f.invocation);
    f.erased();
    f.all();
    check(f.store->finish(f.invocation, true, f.final()));
    check(f.store->begin(0, f.device, f.sources[0], f.sources[1]) == 0);
    f.erased();
    check(f.store->begin(0, f.device, f.sources[0], f.sources[1]) == 0);
    f.erased();
    ++cases;
}
void registry_reset() {
    Fixture f;
    f.begin();
    f.map(Buffer::Index);
    std::mutex registry;
    std::atomic<bool> reset_attempt{false}, reset_started{false};
    std::thread resetter;
    {
        std::lock_guard<std::mutex> lock(registry);
        resetter = std::thread([&] {
            reset_attempt.store(true, std::memory_order_release);
            std::lock_guard<std::mutex> reset_lock(registry);
            reset_started.store(true, std::memory_order_release);
            f.store->reset();
        });
        while (!reset_attempt.load(std::memory_order_acquire)) std::this_thread::yield();
        check(!reset_started.load(std::memory_order_acquire));
        check(f.store->stage(f.invocation, Buffer::Index, f.guard(Buffer::Index)));
        check(!reset_started.load(std::memory_order_acquire));
    }
    resetter.join();
    check(reset_started.load(std::memory_order_acquire));
    check(!f.store->finish(f.invocation, true, f.final()));
    f.erased();
    ++cases;
}
}
int main() {
    const auto start = std::chrono::steady_clock::now();
    success();
    incomplete_oom();
    source_failure();
    stage_discriminators();
    creation_discriminators();
    closure_discriminators();
    nested_retry_abort();
    registry_reset();
    std::cout << "{\"status\":\"PASS\",\"cases\":" << cases << ",\"assertions\":" << checks
              << ",\"arena_bytes\":" << arena_bytes << ",\"store_bytes\":" << sizeof(Store)
              << ",\"seconds\":" << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << ",\"native_com_tested\":false,\"seh_tested\":false}\n";
}
