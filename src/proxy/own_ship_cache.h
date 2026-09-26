#pragma once
// Own-ship membership cache of the adaptive near cascade
// (docs/architecture/shadow-cascade-extents.md, section 5). Pure: no D3D, no
// engine access; the parent walk is the caller's (object_capture::own_ship_descends
// over engine_memory::read in production, a synthetic image on the host).
// A verdict is keyed on the node address AND its handle (a freed part address
// reused by another node carries another handle: never a stale "own"), lives
// cache_frames frames, and the whole table is flushed when the root, its
// handle or the load / registry epoch changes. At most walks_per_frame walks
// per frame: a further miss is counted deferred and answers "not own" for
// this frame without being cached (a thrashing scene of hundreds of new nodes
// never pays more than 64 walks a frame).
#include <cstdint>

namespace x3m::own_ship {
constexpr unsigned cache_size = 64, cache_frames = 256, walks_per_frame = 64;
struct Entry {
    std::uintptr_t node = 0;
    std::uint32_t handle = 0, stamp = 0;
    bool own = false;
};
struct Cache {
    Entry entries[cache_size]{};
    std::uintptr_t root = 0;
    std::uint32_t root_handle = 0;
    std::uint64_t load_epoch = 0, registry_epoch = 0;
    std::uint32_t frame = 0;
    unsigned walks = 0, deferred = 0, hits = 0; // this frame
    unsigned flushes = 0, walks_total = 0;      // since attach
    void flush() noexcept {
        for (auto& e : entries) e = Entry{};
        ++flushes;
    }
    // The frame's root and epochs; any change flushes. A new frame resets the walk budget and the counters.
    void bind(std::uint32_t now, std::uintptr_t r, std::uint32_t rh, std::uint64_t le, std::uint64_t re) noexcept {
        if (r != root || rh != root_handle || le != load_epoch || re != registry_epoch) {
            flush();
            root = r;
            root_handle = rh;
            load_epoch = le;
            registry_epoch = re;
        }
        if (now != frame) {
            frame = now;
            walks = 0;
            deferred = 0;
            hits = 0;
        }
    }
    // Whether (node, handle) is the root or descends from it; `walk(node, handle)` decides a miss.
    template <class Walk> bool own(std::uintptr_t node, std::uint32_t handle, Walk walk) noexcept {
        if (!node || !root) return false;
        if (node == root) return handle == root_handle;
        Entry& e = entries[(node >> 4) % cache_size];
        if (e.node == node && e.handle == handle && frame - e.stamp < cache_frames) {
            ++hits;
            return e.own;
        }
        if (walks >= walks_per_frame) {
            ++deferred;
            return false;
        }
        ++walks;
        ++walks_total;
        e.node = node;
        e.handle = handle;
        e.stamp = frame;
        e.own = walk(node, handle);
        return e.own;
    }
};
}
