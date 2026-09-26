#pragma once
// Capture-only game-layout readers. No hook, native call, floating arithmetic,
// allocation or lock. Device's existing capture lock owns Cache. See
// docs/reverse-engineering/station-material-distance.md for field proofs/limits.
#include <cstdint>
#include <cstddef>

namespace x3m::object_capture {
enum class Status : unsigned { Ready, ReadFailure, Malformed, Cycle, Limit, Missing, NoTarget, Match, End, Capacity };
inline const char* name(Status s) {
    switch (s) {
    case Status::Ready: return "ready";
    case Status::ReadFailure: return "read_failure";
    case Status::Malformed: return "malformed";
    case Status::Cycle: return "cycle";
    case Status::Limit: return "limit";
    case Status::Missing: return "missing";
    case Status::NoTarget: return "no_target";
    case Status::Match: return "match";
    case Status::End: return "end";
    case Status::Capacity: return "capacity";
    }
    return "invalid";
}
template <class Read, class T> bool field(Read& read, std::uint32_t base, unsigned offset, T& out) {
    return base && !(base & 3) && offset <= UINT32_MAX - base && sizeof out <= UINT32_MAX - base - offset &&
           read(std::uintptr_t(base) + offset, &out, sizeof out);
}
struct Target {
    Status status = Status::ReadFailure;
    std::uint32_t registry = 0, handle = 0, cockpit = 0, camera = 0, target = 0, target_id = 0, root = 0,
                  root_handle = 0;
};
// Same active-cockpit registry layout as chase_lead::active; independent of
// whether chase camera/transition hooks are enabled. Missing/partial is unknown.
template <class Read> Target target(Read& read, std::uint32_t slot) {
    Target out;
    std::uint32_t table = 0, bucket[2]{}, link = 0;
    if (!field(read, slot, 0, out.registry) || !field(read, out.registry, 0, table) ||
        !field(read, out.registry, 0x10, out.handle) || !field(read, table, 0, bucket))
        return out;
    const auto n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1))) {
        out.status = Status::Malformed;
        return out;
    }
    if (!field(read, bucket[0], 4 * ((n - 1) & out.handle), link)) return out;
    std::uint32_t seen[32]{};
    unsigned count = 0;
    while (link) {
        for (unsigned i = 0; i < count; ++i)
            if (seen[i] == link) {
                out.status = Status::Cycle;
                return out;
            }
        if (count == 32) {
            out.status = Status::Limit;
            return out;
        }
        seen[count++] = link;
        std::uint32_t row[3]{};
        if (!field(read, link, 0, row)) return out;
        if (row[1] == out.handle) {
            out.cockpit = row[2];
            if (!field(read, out.cockpit, 0x58, out.camera) || !field(read, out.cockpit, 0x1e0, out.target)) return out;
            if (!out.target) {
                out.status = Status::NoTarget;
                return out;
            }
            if (!field(read, out.target, 8, out.target_id) || !field(read, out.target, 0x70, out.root) ||
                !field(read, out.root, 0x28, out.root_handle))
                return out;
            out.status = Status::Ready;
            return out;
        }
        link = row[0];
    }
    out.status = Status::Missing;
    return out;
}
// The player's ship as the chase camera resolves it (chase_camera.cpp,
// docs/reverse-engineering/chase-camera-first-flight.md): the active control
// cockpit of the same registry walk as `target`, its ref object (`cockpit+0xc`,
// the object the cockpit belongs to) and that object's root render node
// (`ref+0x70`) with the node's handle (`node+0x28`, as `ancestry` reads it).
// Missing/partial is unknown (status), never a guess. Six to eight small reads.
struct OwnShip {
    Status status = Status::ReadFailure;
    std::uint32_t registry = 0, handle = 0, cockpit = 0, object = 0, node = 0, node_handle = 0;
};
template <class Read> OwnShip own_ship(Read& read, std::uint32_t slot) {
    OwnShip out;
    std::uint32_t table = 0, bucket[2]{}, link = 0;
    if (!field(read, slot, 0, out.registry) || !field(read, out.registry, 0, table) ||
        !field(read, out.registry, 0x10, out.handle) || !field(read, table, 0, bucket))
        return out;
    const auto n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1))) {
        out.status = Status::Malformed;
        return out;
    }
    if (!out.handle) {
        out.status = Status::NoTarget;
        return out;
    } // no active control: a stale row with id 0 must not match
    if (!field(read, bucket[0], 4 * ((n - 1) & out.handle), link)) return out;
    std::uint32_t seen[32]{};
    unsigned count = 0;
    while (link) {
        for (unsigned i = 0; i < count; ++i)
            if (seen[i] == link) {
                out.status = Status::Cycle;
                return out;
            }
        if (count == 32) {
            out.status = Status::Limit;
            return out;
        }
        seen[count++] = link;
        std::uint32_t row[3]{};
        if (!field(read, link, 0, row)) return out;
        if (row[1] == out.handle) {
            out.cockpit = row[2];
            if (!field(read, out.cockpit, 0xc, out.object)) return out;
            if (!out.object || (out.object & 3)) {
                out.status = Status::NoTarget;
                return out;
            }
            if (!field(read, out.object, 0x70, out.node)) return out;
            if (!out.node || (out.node & 3)) {
                out.status = Status::Malformed;
                return out;
            }
            if (!field(read, out.node, 0x28, out.node_handle)) return out;
            out.status = Status::Ready;
            return out;
        }
        link = row[0];
    }
    out.status = Status::Missing;
    return out;
}
// Whether `node` is `root` or descends from it through the parent links
// (`node+0x18`), the root confirmed by its handle. Bounded (16 links); an
// unreadable link is "no".
template <class Read>
bool own_ship_descends(Read& read, std::uint32_t node, std::uint32_t root, std::uint32_t root_handle) {
    for (unsigned depth = 0; depth < 16 && node && !(node & 3); ++depth) {
        if (node == root) {
            std::uint32_t handle = 0;
            return field(read, node, 0x28, handle) && handle == root_handle;
        }
        std::uint32_t parent = 0;
        if (!field(read, node, 0x18, parent)) return false;
        node = parent;
    }
    return false;
}
struct Link {
    std::uint32_t node = 0, handle = 0;
};
struct Ancestry {
    Status status = Status::ReadFailure;
    unsigned count = 0;
    Link links[16]{};
};
template <class Read>
Ancestry ancestry(Read& read, std::uint32_t node, std::uint32_t handle, std::uint32_t parent, const Target& selected) {
    Ancestry out;
    if (!node || (node & 3)) {
        out.status = Status::Malformed;
        return out;
    }
    for (;;) {
        for (unsigned i = 0; i < out.count; ++i)
            if (out.links[i].node == node) {
                out.status = Status::Cycle;
                return out;
            }
        if (out.count == 16) {
            out.status = Status::Limit;
            return out;
        }
        if (out.count) {
            std::uint32_t words[11]{};
            if (!field(read, node, 0, words)) return out;
            parent = words[0x18 / 4];
            handle = words[0x28 / 4];
        }
        out.links[out.count++] = {node, handle};
        if (selected.status == Status::Ready && node == selected.root && handle == selected.root_handle) {
            out.status = Status::Match;
            return out;
        }
        if (!parent) {
            out.status = Status::End;
            return out;
        }
        node = parent;
    }
}
struct Fade {
    std::uint32_t valid = 0, context = 0, flags = 0, position[3]{}, near_bits = 0, far_bits = 0, scale_bits = 0,
                  config = 0;
};
// Every field remains raw. No CPU approximation of native x87 distance/fog.
// Valid bits: 1 camera block; 2 context scale; 4 distance configuration.
template <class Read> Fade fade(Read& read, std::uint32_t camera, std::uint32_t config_slot) {
    Fade out;
    std::uint32_t words[0x374 / 4]{}, config = 0;
    if (field(read, camera, 0, words)) {
        out.valid |= 1;
        out.context = words[0x1c / 4];
        out.flags = words[0x270 / 4];
        for (unsigned i = 0; i < 3; ++i) out.position[i] = words[0x30 / 4 + i];
        out.near_bits = words[0x36c / 4];
        out.far_bits = words[0x370 / 4];
        if (field(read, out.context, 0x2c, out.scale_bits)) out.valid |= 2;
    }
    if (field(read, config_slot, 0, config) && field(read, config, 0x768, out.config)) out.valid |= 4;
    return out;
}
// Once-per-unique-node emission: entries retain only keys, not long ancestor
// tapes. IDs are 1-based within the frame/reset token. Overflow is explicit;
// it never triggers uncached pointer walking or silently reuses an old record.
struct Cache {
    static constexpr unsigned node_capacity = 512, camera_capacity = 16;
    struct NodeKey {
        std::uint32_t node = 0, handle = 0, parent = 0;
    };
    struct CameraKey {
        std::uint32_t camera = 0, handle = 0;
    };
    NodeKey nodes[node_capacity]{};
    CameraKey cameras[camera_capacity]{};
    unsigned node_count = 0, camera_count = 0;
    std::uint64_t frame = 0, reset = 0, epoch = 0;
    bool valid = false;
    Target selected{};
    void invalidate() {
        valid = false;
        node_count = camera_count = 0;
        selected = {};
    }
    template <class Read> bool begin(std::uint64_t f, std::uint64_t r, Read& read, std::uint32_t slot) {
        if (valid && frame == f && reset == r) return false;
        invalidate();
        frame = f;
        reset = r;
        ++epoch;
        valid = true;
        selected = target(read, slot);
        return true;
    }
    unsigned node(std::uint32_t p, std::uint32_t h, std::uint32_t parent, bool& fresh) {
        fresh = false;
        for (unsigned i = 0; i < node_count; ++i)
            if (nodes[i].node == p && nodes[i].handle == h && nodes[i].parent == parent) return i + 1;
        if (node_count == node_capacity) return 0;
        nodes[node_count++] = {p, h, parent};
        fresh = true;
        return node_count;
    }
    unsigned camera(std::uint32_t p, std::uint32_t h, bool& fresh) {
        fresh = false;
        for (unsigned i = 0; i < camera_count; ++i)
            if (cameras[i].camera == p && cameras[i].handle == h) return i + 1;
        if (camera_count == camera_capacity) return 0;
        cameras[camera_count++] = {p, h};
        fresh = true;
        return camera_count;
    }
};
}
