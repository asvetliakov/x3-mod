#include "../../src/ownership/surface_lease_core.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unordered_map>
#include <type_traits>

using namespace x3m::ownership;
using detail::SurfaceLookup;
static unsigned checks = 0;
static void check(bool ok) { ++checks; if (!ok) std::abort(); }
struct TrackedMutex {
    std::recursive_mutex mutex;
    static thread_local unsigned depth;
    void lock() { mutex.lock(); ++depth; }
    void unlock() { --depth; mutex.unlock(); }
};
thread_local unsigned TrackedMutex::depth = 0;
enum class Kind { Device, Surface, Other };
struct Registry;
struct FakeCom {
    Kind kind;
    std::uint32_t refs = 1;
    FakeCom* parent = nullptr;
    Registry* registry = nullptr;
    void* key = this;
    std::uint64_t serial = 0, generation = 1;
    bool available = true, alive = true;
    unsigned backend_releases = 0, backend_addrefs = 0;
    detail::LogicalSurfaceLease<FakeCom>* reentrant_lease = nullptr;
    void Release();
};
struct Registry {
    TrackedMutex mutex;
    std::unordered_map<void*, FakeCom*> nodes;
    std::uint64_t serial = 0;
    void insert(FakeCom& n, Kind kind, FakeCom* parent = nullptr, void* key = nullptr) {
        std::lock_guard<TrackedMutex> lock(mutex);
        n.kind = kind; n.registry = this; n.parent = parent;
        n.key = key ? key : &n;
        n.serial = detail::next_surface_serial(serial);
        nodes.emplace(n.key, &n);
        if (parent) ++parent->refs;
    }
    SurfaceLookup lookup(void* device, void* surface, const SurfaceLeaseIdentity* expected,
            SurfaceLeaseIdentity& out) {
        return detail::lookup_surface(mutex, nodes, device, surface, Kind::Device, Kind::Surface,
            [](FakeCom* d, FakeCom* s) {
                if (!d->available) return SurfaceLeaseIdentity{};
                return SurfaceLeaseIdentity{d->serial, d->generation, s->serial};
            }, expected, out);
    }
};
void FakeCom::Release() {
    { std::lock_guard<TrackedMutex> lock(registry->mutex);
      if (!refs) std::abort();
      if (--refs) return;
      registry->nodes.erase(key); }
    if (TrackedMutex::depth) std::abort(); // backend dispatch must be outside registry lock
    ++backend_releases;
    alive = false;
    if (reentrant_lease) reentrant_lease->reset();
    if (parent) parent->Release();
}
using Lease = detail::LogicalSurfaceLease<FakeCom>;
static_assert(!std::is_copy_constructible_v<Lease>);
static_assert(!std::is_copy_assignable_v<Lease>);
static void validation_and_lifetime() {
    Registry r;
    FakeCom d{}, other_device{}, s{}, wrong_kind{};
    r.insert(d, Kind::Device); r.insert(other_device, Kind::Device);
    r.insert(s, Kind::Surface, &d); r.insert(wrong_kind, Kind::Other, &d);
    SurfaceLeaseIdentity id, out;
    check(r.lookup(&d, &s, nullptr, id) == SurfaceLookup::Ready);
    check(s.refs == 1 && d.refs == 3); // snapshot owns nothing
    const auto expected = id;
    check(r.lookup(reinterpret_cast<void*>(1), &s, &id, out) == SurfaceLookup::Invalid);
    check(r.lookup(&d, reinterpret_cast<void*>(1), &id, out) == SurfaceLookup::Invalid);
    int native_surface = 0;
    check(r.lookup(&d, &native_surface, &id, out) == SurfaceLookup::Invalid);
    check(r.lookup(&other_device, &s, &id, out) == SurfaceLookup::Invalid);
    check(r.lookup(&d, &wrong_kind, &id, out) == SurfaceLookup::Invalid);
    for (unsigned field = 0; field != 3; ++field) {
        auto stale = id;
        if (field == 0) ++stale.device_serial;
        if (field == 1) ++stale.device_generation;
        if (field == 2) ++stale.surface_serial;
        check(r.lookup(&d, &s, &stale, out) == SurfaceLookup::Invalid);
    }
    SurfaceLeaseIdentity zero{};
    check(r.lookup(&d, &s, &zero, out) == SurfaceLookup::Invalid);
    check(!out.valid() && s.refs == 1);
    d.available = false;
    check(r.lookup(&d, &s, &id, out) == SurfaceLookup::Unavailable);
    d.available = true;
    detail::advance_surface_generation(d.generation);
    check(r.lookup(&d, &s, &id, out) == SurfaceLookup::Invalid);
    check(r.lookup(&d, &s, nullptr, id) == SurfaceLookup::Ready);
    s.refs = UINT32_MAX;
    check(r.lookup(&d, &s, &id, out) == SurfaceLookup::Overflow && s.refs == UINT32_MAX);
    s.refs = 0;
    check(r.lookup(&d, &s, &id, out) == SurfaceLookup::Invalid);
    s.refs = 1;
    const auto device_refs = d.refs;
    d.refs = 0;
    check(r.lookup(&d, &s, &id, out) == SurfaceLookup::Invalid);
    d.refs = device_refs;
    check(r.lookup(&s, &s, &id, out) == SurfaceLookup::Invalid);
    check(r.lookup(&d, &s, &id, out) == SurfaceLookup::Ready);
    Lease lease;
    lease.adopt_retained(&s);
    s.Release(); wrong_kind.Release(); d.Release();
    check(s.alive && d.alive && s.refs == 1 && d.refs == 1);
    check(lease.get() == &s && lease.get()->alive); // borrowed pointer remains usable
    Lease moved(std::move(lease));
    check(!lease.get() && moved.get() == &s);
    Lease assigned;
    assigned = std::move(moved);
    check(!moved.get() && assigned.get() == &s);
    s.reentrant_lease = &assigned;
    assigned.reset(); assigned.reset();
    check(!assigned.get() && !s.alive && !d.alive);
    check(s.backend_releases == 1 && d.backend_releases == 1 && s.backend_addrefs == 0);
    check(r.lookup(&d, &s, &expected, out) == SurfaceLookup::Invalid);
    other_device.Release();
}
static void address_reuse_and_exhaustion() {
    Registry r;
    FakeCom d{}, s{};
    r.insert(d, Kind::Device); r.insert(s, Kind::Surface, &d);
    SurfaceLeaseIdentity old, out;
    check(r.lookup(&d, &s, nullptr, old) == SurfaceLookup::Ready);
    s.Release();
    FakeCom replacement{};
    r.insert(replacement, Kind::Surface, &d, &s); // exact public address reused
    check(r.lookup(&d, &s, &old, out) == SurfaceLookup::Invalid);
    replacement.Release(); d.Release();
    FakeCom replacement_device{}, replacement_surface{};
    r.insert(replacement_device, Kind::Device, nullptr, &d);
    r.insert(replacement_surface, Kind::Surface, &replacement_device, &s);
    check(r.lookup(&d, &s, &old, out) == SurfaceLookup::Invalid);
    check(r.lookup(&d, &s, nullptr, out) == SurfaceLookup::Ready);
    replacement_device.generation = 0;
    check(r.lookup(&d, &s, nullptr, out) == SurfaceLookup::Unavailable && !out.valid());
    replacement_device.generation = 1;
    replacement_surface.serial = 0;
    check(r.lookup(&d, &s, nullptr, out) == SurfaceLookup::Unavailable && !out.valid());
    replacement_surface.Release(); replacement_device.Release();
    std::uint64_t serial = UINT64_MAX - 1;
    check(detail::next_surface_serial(serial) == UINT64_MAX);
    check(detail::next_surface_serial(serial) == 0 && detail::next_surface_serial(serial) == 0);
    std::uint64_t generation = UINT64_MAX;
    detail::advance_surface_generation(generation);
    check(generation == 0);
    detail::advance_surface_generation(generation);
    check(generation == 0);
}
static void move_assignment_cleanup() {
    Registry r;
    FakeCom d{}, a{}, b{};
    r.insert(d, Kind::Device); r.insert(a, Kind::Surface, &d); r.insert(b, Kind::Surface, &d);
    { Lease first, second;
      first.adopt_retained(&a); second.adopt_retained(&b);
      first = std::move(second);
      check(a.backend_releases == 1 && b.backend_releases == 0);
      check(!second.get() && first.get() == &b); }
    check(a.backend_releases == 1 && b.backend_releases == 1);
    d.Release(); check(d.backend_releases == 1);
}
static void release_acquire_race() {
    unsigned acquired = 0, refused = 0;
    for (unsigned i = 0; i != 512; ++i) {
        Registry r;
        FakeCom d{}, s{};
        r.insert(d, Kind::Device); r.insert(s, Kind::Surface, &d);
        SurfaceLeaseIdentity id, out;
        check(r.lookup(&d, &s, nullptr, id) == SurfaceLookup::Ready);
        std::atomic<bool> start{false};
        std::thread final_release([&] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            s.Release();
        });
        start.store(true, std::memory_order_release);
        if (i % 2) std::this_thread::yield();
        const auto result = r.lookup(&d, &s, &id, out);
        Lease lease;
        if (result == SurfaceLookup::Ready) { lease.adopt_retained(&s); ++acquired; }
        else { check(result == SurfaceLookup::Invalid); ++refused; }
        final_release.join();
        if (lease.get()) check(s.alive && s.refs == 1 && s.backend_releases == 0);
        lease.reset();
        check(!s.alive && s.backend_releases == 1 && s.backend_addrefs == 0 && d.refs == 1);
        d.Release(); check(d.backend_releases == 1);
    }
    check(acquired + refused == 512);
    // Scheduler-dependent winner counts are reported, never acceptance criteria.
    std::printf("surface_lease_host races=512 acquired=%u refused=%u checks=%u failures=0\n", acquired, refused, checks);
}
int main() {
    validation_and_lifetime(); address_reuse_and_exhaustion(); move_assignment_cleanup(); release_acquire_race();
}
