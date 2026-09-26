#pragma once
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>

namespace x3m::ownership {
// Serial identifies a canonical wrapper lifetime, not a resurrected native
// allocation. Zero is never valid. Snapshotting this identity does not pin it.
struct SurfaceLeaseIdentity {
    std::uint64_t device_serial = 0;
    std::uint64_t device_generation = 0;
    std::uint64_t surface_serial = 0;
    bool valid() const noexcept { return device_serial && device_generation && surface_serial; }
};
namespace detail {
inline bool same_surface_identity(const SurfaceLeaseIdentity& a, const SurfaceLeaseIdentity& b) noexcept {
    return a.device_serial == b.device_serial && a.device_generation == b.device_generation &&
           a.surface_serial == b.surface_serial;
}
// Exhaustion permanently refuses new IDs/epochs rather than allowing ABA.
inline std::uint64_t next_surface_serial(std::uint64_t& last) noexcept {
    return last == UINT64_MAX ? 0 : ++last;
}
inline void advance_surface_generation(std::uint64_t& generation) noexcept {
    if (generation) generation = generation == UINT64_MAX ? 0 : generation + 1;
}
enum class SurfaceLookup { Ready, Invalid, Unavailable, Overflow };

// Registry keys are compared before inspecting any object. Describe sees only
// live, related nodes under the caller's registry mutex and MUST be CPU-only.
// Null expected means snapshot only; a retain always requires an exact identity.
// No allocations, COM calls or vtable reads occur here. Publication/removal and
// final logical decrement must use this same mutex.
template <class Mutex, class Registry, class Key, class Kind, class Describe>
SurfaceLookup lookup_surface(Mutex& mutex, Registry& registry, Key device_key, Key surface_key, Kind device_kind,
                             Kind surface_kind, Describe describe, const SurfaceLeaseIdentity* expected,
                             SurfaceLeaseIdentity& identity) {
    identity = {};
    if (expected && !expected->valid()) return SurfaceLookup::Invalid;
    std::lock_guard<Mutex> lock(mutex);
    const auto d = registry.find(device_key), s = registry.find(surface_key);
    if (d == registry.end() || s == registry.end()) return SurfaceLookup::Invalid;
    auto* device = d->second;
    auto* surface = s->second;
    if (device->kind != device_kind || surface->kind != surface_kind || !device->refs || !surface->refs ||
        surface->parent != device)
        return SurfaceLookup::Invalid;
    const auto current = describe(device, surface);
    if (!current.valid()) return SurfaceLookup::Unavailable;
    if (expected) {
        if (!same_surface_identity(*expected, current)) return SurfaceLookup::Invalid;
        if (surface->refs == std::numeric_limits<decltype(surface->refs)>::max()) return SurfaceLookup::Overflow;
        ++surface->refs;
    }
    identity = current;
    return SurfaceLookup::Ready;
}

// Owns exactly one already retained logical COM reference. Clear before Release
// so reentrant cleanup cannot release it twice. Borrowed get() expires at reset.
// Single-owner/single-thread object; moving transfers, never AddRefs.
template <class Interface> class LogicalSurfaceLease {
public:
    LogicalSurfaceLease() noexcept = default;
    ~LogicalSurfaceLease() noexcept { reset(); }
    LogicalSurfaceLease(const LogicalSurfaceLease&) = delete;
    LogicalSurfaceLease& operator=(const LogicalSurfaceLease&) = delete;
    LogicalSurfaceLease(LogicalSurfaceLease&& other) noexcept
        : value_(std::exchange(other.value_, nullptr)) {}
    LogicalSurfaceLease& operator=(LogicalSurfaceLease&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    Interface* get() const noexcept { return value_; }
    void reset() noexcept {
        if (auto* value = std::exchange(value_, nullptr)) value->Release();
    }
    // Internal ownership transfer only; caller guarantees empty and +1 retained.
    void adopt_retained(Interface* value) noexcept { value_ = value; }

private:
    Interface* value_ = nullptr;
};
}
}
