#pragma once
// Draw-time thin vote of the TAA thin region (docs/architecture/taa-thin-geometry-alternatives.md
// section 3.2, "B"; X3M_TAA_THIN_VOTE). Pure CPU arithmetic and fixed storage, no D3D and no
// allocation, host-testable:
//  - measure(): the triangle-height histogram of one subset (one VB/IB range = one draw),
//    h = 2 * area / longest edge in object units, 8 log2 bins anchored at the tallest triangle
//    (bin 7 holds floor(log2 h) = e_max, bin 0 everything at or below e_max - 7), kept as the
//    cumulative fraction at the 9 bin edges;
//  - thin_fraction(): per draw, the fraction of the subset's triangles whose height in pixels
//    falls in [0.5, 3] px at the draw's projected scale (the histogram shifted by log2 of the
//    pixels per object unit, the cumulative read at the two window edges, linear inside a bin);
//  - Cache: set-associative (subset key -> histogram), an entry returned this frame is never
//    evicted this frame; an index from the wrappers an entry was read through to its slots
//    (a write invalidation drops only that wrapper's entries) and per-wrapper write counts
//    (a wrapper rewritten volatile_after times is no longer read).
// Everything on the draw path is SSE scalar or integer work (no libm, no x87: the draw hooks
// run under the light CPU boundary and check_no_x87.py walks them).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "sse_scalar.h"

namespace x3m::thin_vote {

constexpr unsigned bins = 8;
constexpr float window_low_px = 0.5f, window_high_px = 3.f;
// log2 of the window edges: -1 and log2(3).
constexpr float window_low_log2 = -1.f, window_high_log2 = 1.5849625007f;
// A subset votes when at least this fraction of its triangles falls in the window (a panel
// group with a few sliver triangles stays unflagged); the vote carries the fraction itself.
constexpr float vote_fraction = .5f;
// Triangles measured per subset: larger subsets are sampled at a fixed stride.
constexpr std::uint32_t sample_cap = 16384;
// Reads per scene end and the soft triangle budget of those reads (a read starts while fewer than
// triangles_per_frame were measured, so the frame's bound is triangles_per_frame - 1 + sample_cap
// = 81,919 sampled triangles); a failed or not-quiet read is retried at later scene ends this many times.
constexpr unsigned reads_per_frame = 16;
constexpr std::uint32_t triangles_per_frame = 65536;
constexpr unsigned read_attempts = 8;

// Position element types (documented D3DDECLTYPE values): FLOAT3 2, FLOAT4 3, FLOAT16_4 16.
inline bool position_type_supported(std::uint32_t type) noexcept {
    return type == 2 || type == 3 || type == 16;
}
inline std::uint32_t position_bytes(std::uint32_t type) noexcept {
    return type == 2 ? 12u : type == 3 ? 16u : type == 16 ? 8u : 0u;
}
inline float half_to_float(std::uint16_t h) noexcept {
    const std::uint32_t sign = std::uint32_t(h & 0x8000u) << 16, exponent = (h >> 10) & 0x1Fu, mantissa = h & 0x3FFu;
    std::uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0)
            bits = sign;
        else {
            std::uint32_t m = mantissa, e = 127 - 15 + 1;
            while (!(m & 0x400u)) {
                m <<= 1;
                --e;
            }
            bits = sign | (e << 23) | ((m & 0x3FFu) << 13);
        }
    } else if (exponent == 31)
        bits = sign | 0x7F800000u | (mantissa << 13);
    else
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    float out;
    std::memcpy(&out, &bits, 4);
    return out;
}
inline void read_position(const unsigned char* p, std::uint32_t type, float out[3]) noexcept {
    if (type == 16) {
        std::uint16_t h[3];
        std::memcpy(h, p, 6);
        for (unsigned i = 0; i < 3; ++i) out[i] = half_to_float(h[i]);
    } else
        std::memcpy(out, p, 12);
}
inline bool finite(float x) noexcept {
    return x == x && x <= 3.4028235e38f && x >= -3.4028235e38f;
}
// floor(log2 x) for a finite x > 0 from the exponent bits (subnormals normalised), no libm.
inline int exponent_of(float x) noexcept {
    std::uint32_t bits;
    std::memcpy(&bits, &x, 4);
    const int e = int((bits >> 23) & 0xFFu);
    if (e) return e - 127;
    std::uint32_t m = bits & 0x7FFFFFu;
    int k = -1; // a subnormal is m * 2^-149: floor(log2) = highest set bit - 149
    while (m) {
        m >>= 1;
        ++k;
    }
    return k - 149;
}
// log2 x for a finite x > 0: exponent bits plus a quadratic in the mantissa m in [1, 2)
// (absolute error below 0.005, i.e. under 0.4 % of a pixel scale).
inline float fast_log2(float x) noexcept {
    std::uint32_t bits;
    std::memcpy(&bits, &x, 4);
    const int e = int((bits >> 23) & 0xFFu) - 127;
    const std::uint32_t mbits = (bits & 0x7FFFFFu) | 0x3F800000u;
    float m;
    std::memcpy(&m, &mbits, 4);
    return float(e) + ((-.34484843f * m + 2.02466578f) * m - 1.67487759f);
}

struct Histogram {
    std::int32_t e0 = 0;          // bin 0's lower edge is 2^e0 object units (bin b: [2^(e0+b), 2^(e0+b+1)))
    std::uint32_t total = 0;      // measured nondegenerate triangles (after sampling)
    float cumulative[bins + 1]{}; // fraction of the measured triangles below edge b; [0] = 0, [bins] = 1
};

// One subset's histogram from its mapped bytes. vertices: the mapped vertex window (vertex k of
// the window at k * stride); vertex_window: how many vertices it holds; indices: the mapped index
// range (null for a non-indexed list: triangle t is window vertices 3t..3t+2), index32 its width,
// index_bias subtracted from every index (the draw's MinVertexIndex: the window starts at
// BaseVertexIndex + MinVertexIndex, and the device adds BaseVertexIndex to each index).
// False: an index outside the window, a nonfinite position, an unsupported type or no
// nondegenerate triangle (the subset stays unflagged).
inline bool measure(const unsigned char* vertices, std::uint32_t vertex_window, std::uint32_t stride,
                    std::uint32_t position_offset, std::uint32_t position_type, const void* indices, bool index32,
                    std::uint32_t index_bias, std::uint32_t triangles, Histogram& out) noexcept {
    out = Histogram{};
    if (!vertices || !triangles || !stride || !position_type_supported(position_type)) return false;
    const std::uint32_t step = triangles > sample_cap ? (triangles + sample_cap - 1) / sample_cap : 1u;
    // Exponent histogram over the whole float range, then folded into the 8 bins below e_max.
    constexpr int lowest = -150, span = 300;
    std::uint32_t counts[span]{};
    int top = lowest;
    std::uint32_t measured = 0;
    for (std::uint32_t t = 0; t < triangles; t += step) {
        std::uint32_t v[3];
        for (unsigned k = 0; k < 3; ++k) {
            const std::uint32_t at = 3u * t + k;
            std::uint32_t i;
            if (!indices)
                i = at;
            else if (index32) {
                std::memcpy(&i, static_cast<const unsigned char*>(indices) + std::size_t(at) * 4, 4);
            } else {
                std::uint16_t s;
                std::memcpy(&s, static_cast<const unsigned char*>(indices) + std::size_t(at) * 2, 2);
                i = s;
            }
            if (indices) {
                if (i < index_bias) return false;
                i -= index_bias;
            }
            if (i >= vertex_window) return false;
            v[k] = i;
        }
        float p[3][3];
        for (unsigned k = 0; k < 3; ++k) {
            read_position(vertices + std::size_t(v[k]) * stride + position_offset, position_type, p[k]);
            if (!finite(p[k][0]) || !finite(p[k][1]) || !finite(p[k][2])) return false;
        }
        const float a[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
        const float b[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
        const float c[3] = {p[2][0] - p[1][0], p[2][1] - p[1][1], p[2][2] - p[1][2]};
        const float n[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        const float la = a[0] * a[0] + a[1] * a[1] + a[2] * a[2], lb = b[0] * b[0] + b[1] * b[1] + b[2] * b[2],
                    lc = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
        float longest = la > lb ? la : lb;
        longest = longest > lc ? longest : lc;
        const float area2 = n[0] * n[0] + n[1] * n[1] + n[2] * n[2]; // (2 area)^2
        if (!(longest > 0.f) || !(area2 > 0.f) || !finite(longest) || !finite(area2))
            continue;                                  // degenerate: not counted
        const float h = scalar::sqrt(area2 / longest); // 2 area / longest edge
        if (!(h > 0.f) || !finite(h)) continue;
        int e = exponent_of(h);
        if (e < lowest)
            e = lowest;
        else if (e >= lowest + span)
            e = lowest + span - 1;
        ++counts[e - lowest];
        if (e > top) top = e;
        ++measured;
    }
    if (!measured) return false;
    std::uint32_t folded[bins]{};
    const int e0 = top - int(bins - 1);
    for (int e = lowest; e <= top; ++e) {
        const std::uint32_t n = counts[e - lowest];
        if (!n) continue;
        const int b = e - e0;
        folded[b < 0 ? 0 : b] += n;
    }
    out.e0 = e0;
    out.total = measured;
    std::uint32_t running = 0;
    out.cumulative[0] = 0.f;
    for (unsigned b = 0; b < bins; ++b) {
        running += folded[b];
        out.cumulative[b + 1] = float(std::int32_t(running)) / float(std::int32_t(measured));
    } // counts below 2^31: signed conversions (cvtsi2ss)
    out.cumulative[bins] = 1.f;
    return true;
}

// The cumulative fraction at bin coordinate x (0 .. bins), linear inside a bin.
inline float cumulative_at(const Histogram& h, float x) noexcept {
    if (!(x > 0.f)) return 0.f;
    if (!(x < float(bins))) return 1.f;
    const int i = int(x); // x in (0, bins): truncation is floor
    const float t = x - float(i);
    return h.cumulative[i] + t * (h.cumulative[i + 1] - h.cumulative[i]);
}
// Pixels per object unit at the draw's object origin from its submitted clip rows (row-major,
// row i = clip component i; the w row's .w is the origin's view depth D for a perspective
// projection): |row 0 xyz| * W / 2 / D, which is m00 * node scale * W / 2 / D. Returned as
// log2, from one log of the squared ratio. False when the origin is not in front (D <= 0) or a
// value is not finite (the draw does not vote).
inline bool log2_pixels_per_unit(const float* rows, float target_width, float& out) noexcept {
    const float d = rows[15];
    const float len2 = rows[0] * rows[0] + rows[1] * rows[1] + rows[2] * rows[2];
    if (!(d > 1e-20f) || !(len2 > 0.f) || !(target_width > 0.f)) return false;
    const float q = len2 * (target_width * target_width * .25f) / (d * d);
    if (!(q > 0.f) || !finite(q)) return false;
    out = .5f * fast_log2(q);
    return true;
}
// Fraction of the subset's triangles whose height in pixels lies in [0.5, 3] at log2 scale s.
inline float thin_fraction(const Histogram& h, float log2_scale) noexcept {
    const float base = float(h.e0) + log2_scale; // bin coordinate x -> log2 pixels = x + base
    const float f = cumulative_at(h, window_high_log2 - base) - cumulative_at(h, window_low_log2 - base);
    return f > 0.f ? (f < 1.f ? f : 1.f) : 0.f;
}
// The value the depth fragment writes to RT2 .a on an opaque routed row: 1 - thin, where thin
// is the fraction when it reaches vote_fraction and 0 below (1 = no vote).
inline float rt2_alpha(float fraction) noexcept {
    return fraction >= vote_fraction ? 1.f - fraction : 1.f;
}

// ---- cache ----
struct Key {
    std::uint64_t vb = 0, ib = 0; // allocation ids (0 ib: non-indexed)
    std::uint32_t stream_offset = 0, stride = 0, position_offset = 0, position_type = 0;
    std::uint32_t first = 0, primitives = 0, min_vertex = 0, vertex_count = 0;
    std::int32_t base_vertex = 0;
    bool operator==(const Key& o) const noexcept {
        return vb == o.vb && ib == o.ib && stream_offset == o.stream_offset && stride == o.stride &&
               position_offset == o.position_offset && position_type == o.position_type && first == o.first &&
               primitives == o.primitives && min_vertex == o.min_vertex && vertex_count == o.vertex_count &&
               base_vertex == o.base_vertex;
    }
    std::uint32_t hash() const noexcept {
        std::uint64_t h = vb * 0x9E3779B97F4A7C15ull ^ ib * 0xC2B2AE3D27D4EB4Full;
        h ^= (std::uint64_t(first) << 32 | primitives) * 0x165667B19E3779F9ull;
        h ^= (std::uint64_t(stream_offset) << 32 | std::uint32_t(base_vertex)) * 0x27D4EB2F165667C5ull;
        h ^= std::uint64_t(min_vertex) << 32 | vertex_count;
        h ^= h >> 29;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 32;
        return std::uint32_t(h);
    }
};
// Known: measured. Unreadable: not MANAGED, out of range, nonfinite, no triangle, or
// read_attempts failed reads; never read again, never votes. Retry: counted failed attempts.
enum class State : std::uint8_t { Empty = 0, Known = 1, Unreadable = 2, Retry = 3 };
// Hot fields first: a hit touches the set's tag line, the key line (with state and stamp) and the histogram line.
struct alignas(64) Entry {
    Key key{};
    State state = State::Empty;
    std::uint8_t attempts = 0;
    std::uint32_t used = 0; // frame stamp of the last lookup that returned this entry
    Histogram histogram{};
    // The application wrappers the entry was read through (compared, never dereferenced): a write invalidation or
    // final release of either drops it.
    std::uintptr_t vb_identity = 0, ib_identity = 0;
};
// Wrapper pointer -> the entries read through it, so an invalidation touches only the queued wrappers' entries.
// Open addressing (linear probing, backward-shift deletion), fixed storage. A wrapper with more than per_id entries,
// or one that finds the table three quarters full, is marked; invalidating it scans the whole cache (rare).
struct IdentityIndex {
    static constexpr unsigned capacity = 4096, per_id = 6;
    struct Slot {
        std::uintptr_t id = 0;
        std::uint8_t count = 0;
        bool overflow = false;
        std::uint16_t entries[per_id]{};
    };
    Slot slots[capacity]{};
    unsigned live = 0;
    bool full = false; // an identity could not be indexed: every invalidation scans the cache until clear()
    static unsigned home(std::uintptr_t id) noexcept {
        return unsigned((std::uint64_t(id) * 0x9E3779B97F4A7C15ull) >> 40) & (capacity - 1);
    }
    Slot* find(std::uintptr_t id) noexcept {
        for (unsigned i = home(id), n = 0; n < capacity; i = (i + 1) & (capacity - 1), ++n) {
            if (!slots[i].id) return nullptr;
            if (slots[i].id == id) return &slots[i];
        }
        return nullptr;
    }
    void add(std::uintptr_t id, std::uint16_t entry) noexcept {
        if (!id) return;
        Slot* s = find(id);
        if (!s) {
            if (live >= capacity / 4 * 3) {
                full = true;
                return;
            }
            unsigned i = home(id);
            while (slots[i].id) i = (i + 1) & (capacity - 1);
            s = &slots[i];
            s->id = id;
            ++live;
        }
        for (unsigned k = 0; k < s->count; ++k)
            if (s->entries[k] == entry) return;
        if (s->count < per_id)
            s->entries[s->count++] = entry;
        else
            s->overflow = true;
    }
    void remove(std::uintptr_t id, std::uint16_t entry) noexcept {
        if (!id) return;
        Slot* s = find(id);
        if (!s) return;
        for (unsigned k = 0; k < s->count; ++k)
            if (s->entries[k] == entry) {
                s->entries[k] = s->entries[--s->count];
                break;
            }
        if (!s->count && !s->overflow) erase(unsigned(s - slots));
    }
    void erase(unsigned i) noexcept {
        unsigned j = i;
        for (;;) {
            j = (j + 1) & (capacity - 1);
            if (!slots[j].id) break;
            const unsigned k = home(slots[j].id);
            // Move slot j back to i unless its home lies cyclically in (i, j].
            const bool stays = i <= j ? (k > i && k <= j) : (k > i || k <= j);
            if (!stays) {
                slots[i] = slots[j];
                i = j;
            }
        }
        slots[i] = Slot{};
        --live;
    }
    void clear() noexcept {
        for (auto& s : slots) s = Slot{};
        live = 0;
        full = false;
    }
};
// Buffers rewritten again and again (a per-frame rewrite would cost a Lock pair and a histogram per frame): the
// write invalidations of a wrapper are counted, and past volatile_after of them its subsets are no longer read.
// Direct-mapped; a collision forgets the older wrapper's count (more re-reads, never a wrong vote). A final release
// forgets its wrapper (its pointer may come back as another buffer); a queue overflow (a release may be lost) forgets
// every count.
constexpr unsigned volatile_after = 4;
struct Volatility {
    static constexpr unsigned size = 1024;
    std::uintptr_t ids[size]{};
    std::uint8_t counts[size]{};
    static unsigned slot(std::uintptr_t id) noexcept {
        return unsigned((std::uint64_t(id) * 0x9E3779B97F4A7C15ull) >> 40) & (size - 1);
    }
    unsigned bump(std::uintptr_t id) noexcept {
        const unsigned i = slot(id);
        if (ids[i] != id) {
            ids[i] = id;
            counts[i] = 0;
        }
        if (counts[i] < 255) ++counts[i];
        return counts[i];
    }
    bool is_volatile(std::uintptr_t id) const noexcept {
        const unsigned i = slot(id);
        return id && ids[i] == id && counts[i] >= volatile_after;
    }
    void forget(std::uintptr_t id) noexcept {
        const unsigned i = slot(id);
        if (ids[i] == id) {
            ids[i] = 0;
            counts[i] = 0;
        }
    }
    void clear() noexcept {
        for (auto& i : ids) i = 0;
        for (auto& c : counts) c = 0;
    }
};
struct Cache {
    static constexpr unsigned sets = 2048, ways = 4, size = sets * ways;
    Entry entries[size]{};
    // One 32-bit tag per way (0 = empty), a set's four in one 16-byte group: a lookup compares tags before it reads any
    // key, so a miss touches one cache line and a hit one more for the key and one for the histogram.
    std::uint32_t tags[size]{};
    IdentityIndex index{};
    Volatility volatility{};
    static std::uint32_t tag_of(std::uint32_t h) noexcept { return (h >> 11) | 1u; }
    std::uint32_t stamp = 1;
    std::uint32_t refused = 0; // stores refused: every way of the set was used this frame
    void begin_frame() noexcept {
        if (++stamp == 0) {
            stamp = 1;
            for (auto& e : entries) e.used = 0;
        }
    }
    Entry* set_of(const Key& key) noexcept { return entries + std::size_t(key.hash() % sets) * ways; }
    // The entry of this exact key, any state, or null (a miss).
    Entry* find(const Key& key) noexcept {
        const std::uint32_t h = key.hash(), base = (h % sets) * ways, tag = tag_of(h);
        for (unsigned w = 0; w < ways; ++w) {
            if (tags[base + w] != tag) continue;
            Entry& e = entries[base + w];
            if (e.key == key) {
                e.used = stamp;
                return &e;
            }
        }
        return nullptr;
    }
    Entry* way_for(const Key& key) noexcept {
        Entry* set = set_of(key);
        Entry* empty = nullptr;
        Entry* oldest = nullptr;
        for (unsigned w = 0; w < ways; ++w) {
            Entry& e = set[w];
            if (e.state == State::Empty) {
                if (!empty) empty = &e;
                continue;
            }
            if (e.key == key) return &e;
            if (e.used != stamp && (!oldest || e.used < oldest->used)) oldest = &e;
        }
        return empty ? empty : oldest;
    }
    // Empties one slot and removes it from the identity index.
    void drop(std::size_t i) noexcept {
        Entry& e = entries[i];
        index.remove(e.vb_identity, std::uint16_t(i));
        index.remove(e.ib_identity, std::uint16_t(i));
        e = Entry{};
        tags[i] = 0;
    }
    // vb_identity / ib_identity: the wrappers the histogram (or the refusal) was read through, 0 when not watched.
    bool store(const Key& key, const Histogram* histogram, std::uintptr_t vb_identity = 0,
               std::uintptr_t ib_identity = 0) noexcept {
        Entry* e = way_for(key);
        if (!e) {
            ++refused;
            return false;
        }
        const std::size_t i = std::size_t(e - entries);
        drop(i);
        e->key = key;
        e->state = histogram ? State::Known : State::Unreadable;
        e->attempts = 0;
        e->used = stamp;
        e->histogram = histogram ? *histogram : Histogram{};
        e->vb_identity = vb_identity;
        e->ib_identity = ib_identity;
        index.add(vb_identity, std::uint16_t(i));
        index.add(ib_identity, std::uint16_t(i));
        tags[i] = tag_of(key.hash());
        return true;
    }
    // One failed attempt (a pending Lock, a failed Lock): Retry until read_attempts, then Unreadable.
    void retry(const Key& key) noexcept {
        Entry* e = way_for(key);
        if (!e) {
            ++refused;
            return;
        }
        const unsigned attempts = e->state == State::Retry && e->key == key ? e->attempts + 1u : 1u;
        const std::size_t i = std::size_t(e - entries);
        drop(i);
        e->key = key;
        e->attempts = std::uint8_t(attempts);
        e->used = stamp;
        e->state = attempts >= read_attempts ? State::Unreadable : State::Retry;
        tags[i] = tag_of(key.hash());
    }
    // Drops every entry read through this wrapper (a write or final release of it): the index's slots, or one pass over
    // the table for a wrapper the index could not hold.
    unsigned invalidate(std::uintptr_t id) noexcept {
        if (!id) return 0;
        unsigned dropped = 0;
        IdentityIndex::Slot* s = index.find(id);
        if (!index.full && s && !s->overflow) {
            std::uint16_t victims[IdentityIndex::per_id];
            const unsigned n = s->count;
            for (unsigned k = 0; k < n; ++k) victims[k] = s->entries[k];
            for (unsigned k = 0; k < n; ++k)
                if (entries[victims[k]].state != State::Empty) {
                    drop(victims[k]);
                    ++dropped;
                }
            return dropped;
        }
        if (!index.full && !s) return 0;
        for (std::size_t i = 0; i < size; ++i) {
            const Entry& e = entries[i];
            if (e.state != State::Empty && (e.vb_identity == id || e.ib_identity == id)) {
                drop(i);
                ++dropped;
            }
        }
        // Every entry of this wrapper is gone: its overflowed slot goes too (found again: the drops may have moved it).
        if (IdentityIndex::Slot* t = index.find(id)) index.erase(unsigned(t - index.slots));
        return dropped;
    }
    // The histograms and the index go (a new device at attach, a queue overflow); the write counts stay (the overflow
    // path clears them itself). A Reset does not clear the cache: MANAGED buffers, their wrappers and allocation ids
    // survive it (MotionOutput::before_reset drops only the queued reads).
    void clear() noexcept {
        for (auto& e : entries) e = Entry{};
        for (auto& t : tags) t = 0;
        index.clear();
        stamp = 1;
        refused = 0;
    }
};

} // namespace x3m::thin_vote
