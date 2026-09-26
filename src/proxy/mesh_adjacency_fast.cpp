#include "mesh_adjacency_fast.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
// The normal arithmetic below reproduces D3DX operation by operation (products
// and differences rounded exactly where D3DX stores a float); a fused
// multiply-add would change the rounding, so contraction must be off in this
// unit: GCC's ISO modes (-std=c++17, every build script) disable it, clang
// needs the pragma (the host test also passes -ffp-contract=off).
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

namespace x3m::mesh_adjacency_fast {
namespace {
// One malloc per call: every array is carved from a single arena whose size is
// computed up front (phase scratch is rewound and reused). The arena is kept
// per thread between calls up to retained_scratch_limit, so a burst of meshes
// on the loading thread pays the allocation and the first-touch page faults
// once; release_scratch() or the thread's exit frees it.
struct Arena {
    unsigned char* base = nullptr;
    size_t capacity = 0;
    ~Arena() { std::free(base); }
};
thread_local Arena arena;
constexpr size_t retained_scratch_limit = size_t(16) << 20;
constexpr size_t align = 8;
inline size_t padded(uint64_t bytes) noexcept {
    return size_t((bytes + (align - 1)) & ~uint64_t(align - 1));
}
struct Bump { // bump allocator over the arena; mark()/rewind() reuse phase scratch
    unsigned char* base;
    size_t capacity, used = 0;
    template <class T> T* array(size_t count) noexcept {
        T* p = reinterpret_cast<T*>(base + used);
        used += padded(uint64_t(count) * sizeof(T));
        return p;
    }
    bool overflow() const noexcept {
        return used > capacity;
    } // the layout computed the size; a mismatch is a bug, reported as Allocation
    size_t mark() const noexcept { return used; }
    void rewind(size_t m) noexcept { used = m; }
};
struct Key {
    uint32_t x, y, z;
};
inline uint32_t normalize_zero(uint32_t bits) noexcept {
    return bits == 0x80000000u ? 0u : bits;
}
inline uint64_t mix(uint64_t h) noexcept {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}
// Position key: the three bit patterns of the 2^-14 grid share their low mantissa
// bits (zero) and differ in a few high bits; each component is spread by its own
// odd multiplier before the 64-bit finalizer (fmix64), whose low bits select the slot.
inline uint64_t hash_key(const Key& k) noexcept {
    return mix((uint64_t(k.x) << 32 | k.y) * 0x9e3779b97f4a7c15ull ^ (uint64_t(k.z) * 0xbf58476d1ce4e5b9ull));
}
inline uint64_t hash_cell(int32_t x, int32_t y, int32_t z) noexcept {
    return mix(uint64_t(uint32_t(x)) * 0x9e3779b97f4a7c15ull ^ mix(uint64_t(uint32_t(y))) ^
               mix(uint64_t(uint32_t(z)) * 0x94d049bb133111ebull));
}
inline uint64_t table_size(uint64_t count) noexcept {
    uint64_t n = 16;
    while (n < count * 2) n <<= 1;
    return n;
} // load factor <= 1/2
// Bit-level helpers keep the module free of CRT floor/frexp/ldexp and 64-bit
// integer conversions, which the i386 compiler would route through x87.
inline bool is_integer(double t) noexcept {
    uint64_t bits;
    std::memcpy(&bits, &t, sizeof bits);
    const int exponent = int((bits >> 52) & 0x7ff) - 1023;
    if (exponent < 0) return (bits & 0x7fffffffffffffffull) == 0;
    if (exponent >= 52) return true;
    return (bits & ((uint64_t(1) << (52 - exponent)) - 1)) == 0;
}
inline double power_of_two(int exponent) noexcept { // |exponent| < 1023
    const uint64_t bits = uint64_t(1023 + exponent) << 52;
    double v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}
inline int32_t floor_to_int(double q) noexcept { // |q| < 2^30 guaranteed by the caller
    int32_t i = int32_t(q);
    if (double(i) > q) --i;
    return i;
}
inline bool finite_bits(uint32_t c) noexcept {
    return (c & 0x7f800000u) != 0x7f800000u;
}
struct Vec {
    float x, y, z;
};
// D3DXVec3Normalize of the SSE dispatch table (d3dx9_37 FUN_00756732): the squared
// length in single precision, vectors below 2^-46 become zero, one Newton step
// on rsqrtss: r = ((3 - (r*len2)*r) * r) * 0.5, then the components times r.
inline float rsqrt(float x) noexcept {
#if defined(__SSE__)
    return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
#else
    return float(1.0 / std::sqrt(double(x)));
#endif
}
inline Vec normalize_sse2(const Vec& v) noexcept {
    const float len2 = (v.x * v.x + v.y * v.y) + v.z * v.z;
    const uint32_t threshold_bits = 0x28800000u;
    float threshold;
    std::memcpy(&threshold, &threshold_bits, sizeof threshold);
    if (!(threshold <= len2)) return {0.f, 0.f, 0.f};
    float r = rsqrt(len2);
    r = ((3.f - ((r * len2) * r)) * r) * 0.5f;
    return {v.x * r, v.y * r, v.z * r};
}
// D3DXVec3Normalize of the generic table (d3dx9_37 FUN_005881fc, the table D3DX
// keeps when it takes the 3DNow branch of its dispatch but the CPUID 3DNow bit is
// absent, as under FEX): x87 at the game's 53-bit precision, so every operation
// is a double operation on float inputs. len2 = (x*x + y*y) + z*z; a zero float
// gives the zero vector; |float(len2 - 1)| <= 0x3727c5ac (1e-5) copies the vector
// unnormalized; otherwise the float bits of len2 select one of 512 linear
// segments (the exponent's low bit and the top eight mantissa bits), the mantissa
// is re-exponented to [0.5, 2), and r = (m * a + b) * 2^(-e/2) with the scale
// formed by the integer trick ((0xbeffffff - bits) >> 1) & 0xff800000.
inline double sqrt_double(double x) noexcept {
#if defined(__SSE2__)
    return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_set_sd(x), _mm_set_sd(x)));
#else
    return std::sqrt(x);
#endif
}
struct RsqrtTable {
    float a[512], b[512];
};
// The DLL's static table is reproduced from its generating rule (every entry
// equal, docs section 4): a secant of 1/sqrt through the float-rounded values at
// the segment ends, the intercept from the upper end.
const RsqrtTable& rsqrt_table() noexcept {
    static const RsqrtTable table = []() noexcept {
        RsqrtTable t;
        for (unsigned k = 0; k < 512; ++k) {
            const double s = (k >> 8) ? 1.0 : 0.5, lo = s * (1.0 + double(k & 255u) / 256.0),
                         hi = s * (1.0 + double((k & 255u) + 1u) / 256.0);
            const float r0 = float(1.0 / sqrt_double(lo)), r1 = float(1.0 / sqrt_double(hi));
            const float a = float((double(r1) - double(r0)) / (hi - lo));
            t.a[k] = a;
            t.b[k] = float(double(r1) - double(a) * hi);
        }
        return t;
    }();
    return table;
}
inline Vec normalize_generic(const Vec& v) noexcept {
    const double len2 = (double(v.x) * double(v.x) + double(v.y) * double(v.y)) + double(v.z) * double(v.z);
    const float len2f = float(len2);
    uint32_t bits;
    std::memcpy(&bits, &len2f, sizeof bits);
    if (bits == 0) return {0.f, 0.f, 0.f};
    const float d = float(len2 - 1.0);
    uint32_t dbits;
    std::memcpy(&dbits, &d, sizeof dbits);
    if ((dbits & 0x7fffffffu) <= 0x3727c5acu) return v;
    const RsqrtTable& t = rsqrt_table();
    const unsigned k = (bits >> 15) & 0x1ffu;
    const uint32_t mbits = (bits & 0xffffffu) | 0x3f000000u, sbits = ((0xbeffffffu - bits) >> 1) & 0xff800000u;
    float m, s;
    std::memcpy(&m, &mbits, sizeof m);
    std::memcpy(&s, &sbits, sizeof s);
    const double r = (double(m) * double(t.a[k]) + double(t.b[k])) * double(s);
    return {float(double(v.x) * r), float(double(v.y) * r), float(double(v.z) * r)};
}
inline Vec normalize_d3dx(const Vec& v, Normalize normalize) noexcept {
    return normalize == Normalize::Generic ? normalize_generic(v) : normalize_sse2(v);
}
// D3DX's face normal for the corner order (p1, p2, p3): the edge vectors p1-p2
// and p1-p3 stored as floats, the cross product formed in extended precision
// and stored as floats (exact in double for products of 24-bit values), normalized.
inline Vec face_normal(const Vec* p, uint32_t v1, uint32_t v2, uint32_t v3, Normalize normalize) noexcept {
    const Vec& a = p[v1];
    const Vec& b = p[v2];
    const Vec& c = p[v3];
    const float e1x = float(double(a.x) - double(b.x)), e1y = float(double(a.y) - double(b.y)),
                e1z = float(double(a.z) - double(b.z));
    const float e2x = float(double(a.x) - double(c.x)), e2y = float(double(a.y) - double(c.y)),
                e2z = float(double(a.z) - double(c.z));
    const Vec n = {float(double(e1y) * double(e2z) - double(e1z) * double(e2y)),
                   float(double(e1z) * double(e2x) - double(e1x) * double(e2z)),
                   float(double(e1x) * double(e2y) - double(e1y) * double(e2x))};
    return normalize_d3dx(n, normalize);
}
// The x87 dot product of two normals (z, x, y order) rounded to float for the comparison.
inline float score(const Vec& n, const Vec& m) noexcept {
    return float((double(n.z) * double(m.z) + double(n.x) * double(m.x)) + double(n.y) * double(m.y));
}
// D3DX's vertex sort (FUN_0058c02c): a binary min-heap over the index array with
// the comparisons `key[right] <= key[left]` (choose the right child) and
// `key[element] < key[child]` (stop), then repeated extraction to the end, so
// the array ends in descending key order with the heap's permutation among
// equal keys. Each element carries its key in the upper 32 bits as an integer
// code with the float order of finite keys (-0 == +0) and its vertex index
// below, so a comparison reads only the heap array (the indirection through the
// vertices cost most of the sort). Le/Lt are the two comparisons.
inline uint32_t order_code(uint32_t bits) noexcept {
    bits = normalize_zero(bits);
    return (bits & 0x80000000u) ? ~bits : (bits | 0x80000000u);
}
template <class Le, class Lt> void heapsort(uint64_t* a, uint32_t n, Le le, Lt lt) noexcept {
    if (n < 2) return;
    auto sift = [&](uint64_t element, uint32_t pos, uint32_t child, uint32_t size) noexcept {
        while (child < size) {
            uint32_t chosen = child;
            if (child + 1 < size && le(a[child + 1], a[child])) chosen = child + 1;
            if (lt(element, a[chosen])) break;
            a[pos] = a[chosen];
            pos = chosen;
            child = chosen * 2 + 1;
        }
        a[pos] = element;
    };
    for (uint32_t i = (n >> 1); i-- > 0;) sift(a[i], i, 2 * i + 1, n);
    for (uint32_t m = n; m-- > 0;) {
        const uint64_t element = a[m];
        a[m] = a[0];
        sift(element, 0, 1, m);
    }
}
// A directed edge is identified by its id = face * 3 + point; its corners are
// corners[face*3 + point], corners[face*3 + (point+1)%3] and the third corner,
// so the edge table stores only the chain links and each slot only an anchor
// edge (whose corners are the slot's key; it stays valid after its chain
// empties, as a tombstone) and the chain head.
struct Slot {
    uint32_t anchor, head;
};
struct NormalCache { // lazily malloc'd only when a chain offers several candidates; without it normals are recomputed
                     // per candidate
    Vec* normals = nullptr;
    unsigned char* ready = nullptr;
    bool tried = false;
    ~NormalCache() {
        std::free(normals);
        std::free(ready);
    }
    void acquire(size_t count) noexcept {
        if (tried) return;
        tried = true;
        normals = static_cast<Vec*>(std::malloc(count * sizeof(Vec)));
        ready = static_cast<unsigned char*>(std::malloc(count));
        if (!normals || !ready) {
            std::free(normals);
            std::free(ready);
            normals = nullptr;
            ready = nullptr;
            return;
        }
        std::memset(ready, 0, count);
    }
};
}
const char* status_name(unsigned status) noexcept {
    static constexpr const char* names[] = {"ok",          "input",
                                            "index_range", "non_finite",
                                            "magnitude",   "epsilon_neighbour",
                                            "allocation",  "competing_normals"};
    return status < status_count ? names[status] : "unknown";
}
const char* rsqrt_implementation() noexcept {
#if defined(__SSE__)
    return "rsqrtss";
#else
    return "portable";
#endif
}
const char* normalize_name(Normalize normalize) noexcept {
    return normalize == Normalize::Generic ? "generic" : "sse2";
}
Mode parse_mode(const wchar_t* value, uint32_t length) noexcept {
    if (!value || !length || length >= mode_capacity) return Mode::Native;
    auto equals = [&](const char* name) {
        uint32_t i = 0;
        for (; i < length && name[i]; ++i) {
            wchar_t c = value[i];
            if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a');
            if (c != wchar_t(name[i])) return false;
        }
        return i == length && !name[i];
    };
    return equals("fast") ? Mode::Fast : equals("verify") ? Mode::Verify : Mode::Native;
}
Mode armed_mode(Mode requested, bool telemetry) noexcept {
    return requested == Mode::Fast || (requested == Mode::Verify && telemetry) ? requested : Mode::Native;
}
const char* mode_name(Mode mode) noexcept {
    return mode == Mode::Fast ? "fast" : mode == Mode::Verify ? "verify" : "native";
}
bool supported_fp_domain(uint32_t control, uint32_t tag, uint32_t mxcsr) noexcept {
    // 53-bit x87, nearest, all exceptions masked, empty register stack. The
    // reserved CW bit 6 differs between 023f (game) and 027f (CRT) and is ignored.
    // Admit only the ordinary or observed game MXCSR controls. In Generic the
    // native arithmetic is x87; admitted SSE2 meshes never evaluate a normal.
    // Thus game FTZ/DAZ does not alter the admitted native arithmetic. Other
    // combinations remain unproved, even if a selected fixture happens to agree.
    const uint32_t mx_controls = mxcsr & ~uint32_t(0x3f);
    return (control & 0x0f3f) == 0x023f && (tag & 0xffff) == 0xffff && (mx_controls == 0x1f80 || mx_controls == 0x9fc0);
}
void release_scratch() noexcept {
    std::free(arena.base);
    arena.base = nullptr;
    arena.capacity = 0;
}
Report generate(const Input& in, uint32_t* adjacency, const Policy& policy) noexcept {
    Report report;
    const uint32_t V = in.vertex_count, F = in.face_count;
    if (!in.vertices || !in.indices || !adjacency || V < 3 || !F || in.stride < 12 ||
        in.position_offset > in.stride - 12 || F > unused / 3 || !(in.epsilon >= 0.f) || std::isinf(in.epsilon))
        return report; // Status::Input (D3DX needs V/3 edge buckets)
    // D3DX's 16-bit class treats a face whose first index is 0xffff as absent in
    // the adjacency stage; with more than 65,535 vertices that index names a real
    // vertex, which the module would pair. Such meshes are left to native.
    if (!in.indices_32bit && V > 0xffffu) return report;
    // The 4x squared-distance margin of the gate needs a normal float epsilon^2.
    if (in.epsilon > 0.f && !std::isnormal(in.epsilon * in.epsilon)) return report;
    // Arena layout (bytes): persistent arrays, then the larger of the two phase
    // scratches (representative table; unquantized-gate cell hash).
    const uint64_t E = uint64_t(F) * 3, vertex_slots = table_size(V), edge_slots = table_size(E);
    const uint64_t persistent = padded(uint64_t(V) * sizeof(Vec)) * (in.position_offset ? 2 : 1) +
                                padded(uint64_t(V) * 4) * 5 + padded(E * 4) * 4 + padded(F) + padded(E) +
                                padded(edge_slots * sizeof(Slot));
    const uint64_t phase_rep = padded(vertex_slots * 4) > padded(uint64_t(V) * 8)
                                   ? padded(vertex_slots * 4)
                                   : padded(uint64_t(V) * 8); // representative table; heapsort elements
    const uint64_t phase_gate = padded(table_size(V) * 4) + padded(uint64_t(V) * 4) + padded(uint64_t(V) * 12);
    const uint64_t total = persistent + (phase_rep > phase_gate ? phase_rep : phase_gate);
    if (total > uint64_t(SIZE_MAX / 2)) {
        report.status = Status::Allocation;
        return report;
    }
    if (arena.capacity < size_t(total)) {
        std::free(arena.base);
        arena.base = static_cast<unsigned char*>(std::malloc(size_t(total)));
        arena.capacity = arena.base ? size_t(total) : 0;
        if (!arena.base) {
            report.status = Status::Allocation;
            return report;
        }
    }
    struct Retain {
        ~Retain() {
            if (arena.capacity > retained_scratch_limit) release_scratch();
        }
    } retain;
    Bump scratch{arena.base, arena.capacity};
    // D3DX reads the sweep key and the vertices of the normal score at byte 0 of
    // each vertex, whatever element lives there; with the position first (the
    // engine's layout) that is the position itself and the two arrays are one.
    auto* positions = scratch.array<Vec>(V);
    auto* head = in.position_offset ? scratch.array<Vec>(V) : positions;
    auto* cls = scratch.array<uint32_t>(V);
    auto* rep = scratch.array<uint32_t>(V);
    auto* order = scratch.array<uint32_t>(V);
    auto* class_next = scratch.array<uint32_t>(V);
    auto* corner_head = scratch.array<uint32_t>(V);
    auto* raw = scratch.array<uint32_t>(size_t(E));
    auto* corner_next = scratch.array<uint32_t>(size_t(E));
    auto* corners = scratch.array<uint32_t>(size_t(E));
    auto* next = scratch.array<uint32_t>(size_t(E));
    auto* active = scratch.array<unsigned char>(F);
    auto* retired = scratch.array<unsigned char>(size_t(E));
    auto* slots = scratch.array<Slot>(size_t(edge_slots));
    const size_t persistent_mark = scratch.mark();
    if (scratch.overflow()) {
        report.status = Status::Allocation;
        return report;
    }
    const auto* bytes = static_cast<const unsigned char*>(in.vertices);
    for (uint32_t v = 0; v < V; ++v) {
        Key k;
        std::memcpy(&k, bytes + size_t(v) * in.stride + in.position_offset, sizeof k);
        Key h;
        std::memcpy(&h, bytes + size_t(v) * in.stride, sizeof h);
        if (!finite_bits(k.x) || !finite_bits(k.y) || !finite_bits(k.z) || !finite_bits(h.x) || !finite_bits(h.y) ||
            !finite_bits(h.z)) {
            report.status = Status::NonFinite;
            return report;
        }
        std::memcpy(&positions[v], &k, sizeof k);
        if (head != positions) std::memcpy(&head[v], &h, sizeof h);
    }
    auto key_of = [&](uint32_t v) noexcept {
        Key k;
        std::memcpy(&k, &positions[v], sizeof k);
        return Key{normalize_zero(k.x), normalize_zero(k.y), normalize_zero(k.z)};
    };
    // Exact-equality classes: cls[v] is the first vertex with the same three bit patterns.
    uint32_t classes = 0;
    {
        auto* table = scratch.array<uint32_t>(size_t(vertex_slots));
        if (scratch.overflow()) {
            report.status = Status::Allocation;
            return report;
        }
        std::memset(table, 0xff, size_t(vertex_slots) * sizeof(uint32_t));
        for (uint32_t v = 0; v < V; ++v) {
            const Key k = key_of(v);
            size_t slot = size_t(hash_key(k)) & size_t(vertex_slots - 1);
            for (;;) {
                const uint32_t occupant = table[slot];
                if (occupant == unused) {
                    table[slot] = v;
                    cls[v] = v;
                    ++classes;
                    break;
                }
                const Key o = key_of(occupant);
                if (o.x == k.x && o.y == k.y && o.z == k.z) {
                    cls[v] = occupant;
                    break;
                }
                slot = (slot + 1) & size_t(vertex_slots - 1);
            }
        }
        scratch.rewind(persistent_mark);
    }
    // Equivalence gate: distinct positions must be further apart than 2*epsilon.
    if (in.epsilon > 0.f) {
        // eps = 1.m * 2^E (normal): 2*eps < 2^(E+2), so the grid 2^(E+2) exceeds 2*eps.
        uint32_t epsilon_bits;
        std::memcpy(&epsilon_bits, &in.epsilon, sizeof epsilon_bits);
        const int exponent = int((epsilon_bits >> 23) & 0xff) - 127 + 2;
        const double grid_inverse = power_of_two(-exponent); // exact scaling
        bool quantized = true;
        for (uint32_t v = 0; v < V && quantized; ++v) {
            if (cls[v] != v) continue;
            for (float c : {positions[v].x, positions[v].y, positions[v].z})
                if (!is_integer(double(c) * grid_inverse)) {
                    quantized = false;
                    break;
                }
        }
        report.quantized = quantized;
        if (!quantized) {
            const double cell = 4.0 * double(in.epsilon), threshold = 4.0 * double(in.epsilon) * double(in.epsilon);
            const size_t cells = size_t(table_size(classes));
            auto* heads = scratch.array<uint32_t>(cells);
            auto* chain = scratch.array<uint32_t>(V);
            auto* coords = scratch.array<int32_t>(size_t(V) * 3);
            if (scratch.overflow()) {
                report.status = Status::Allocation;
                return report;
            }
            std::memset(heads, 0xff, cells * sizeof(uint32_t));
            constexpr double limit = 1073741824.0; // 2^30: int32 cell coordinates with neighbour headroom
            for (uint32_t v = 0; v < V; ++v) {
                if (cls[v] != v) continue;
                const double q[3] = {double(positions[v].x) / cell, double(positions[v].y) / cell,
                                     double(positions[v].z) / cell};
                for (double value : q)
                    if (!(value > -limit && value < limit)) {
                        report.status = Status::Magnitude;
                        return report;
                    }
                for (unsigned i = 0; i < 3; ++i) coords[size_t(v) * 3 + i] = floor_to_int(q[i]);
                size_t slot = size_t(hash_cell(coords[size_t(v) * 3], coords[size_t(v) * 3 + 1],
                                               coords[size_t(v) * 3 + 2])) &
                              (cells - 1);
                // Chains share a slot on equal cell; probing keeps distinct cells apart.
                for (;;) {
                    const uint32_t head = heads[slot];
                    if (head == unused) {
                        heads[slot] = v;
                        chain[v] = unused;
                        break;
                    }
                    if (coords[size_t(head) * 3] == coords[size_t(v) * 3] &&
                        coords[size_t(head) * 3 + 1] == coords[size_t(v) * 3 + 1] &&
                        coords[size_t(head) * 3 + 2] == coords[size_t(v) * 3 + 2]) {
                        chain[v] = heads[slot];
                        heads[slot] = v;
                        break;
                    }
                    slot = (slot + 1) & (cells - 1);
                }
            }
            for (uint32_t v = 0; v < V; ++v) {
                if (cls[v] != v) continue;
                const int32_t cx = coords[size_t(v) * 3], cy = coords[size_t(v) * 3 + 1],
                              cz = coords[size_t(v) * 3 + 2];
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dz = -1; dz <= 1; ++dz) {
                            const int32_t qx = cx + dx, qy = cy + dy, qz = cz + dz;
                            size_t slot = size_t(hash_cell(qx, qy, qz)) & (cells - 1);
                            for (;;) {
                                const uint32_t head = heads[slot];
                                if (head == unused) break;
                                if (coords[size_t(head) * 3] == qx && coords[size_t(head) * 3 + 1] == qy &&
                                    coords[size_t(head) * 3 + 2] == qz) {
                                    for (uint32_t o = head; o != unused; o = chain[o]) {
                                        if (o == v) continue;
                                        const double ex = double(positions[o].x) - double(positions[v].x),
                                                     ey = double(positions[o].y) - double(positions[v].y),
                                                     ez = double(positions[o].z) - double(positions[v].z);
                                        if (ex * ex + ey * ey + ez * ez <= threshold) {
                                            report.status = Status::EpsilonNeighbour;
                                            return report;
                                        }
                                    }
                                    break;
                                }
                                slot = (slot + 1) & (cells - 1);
                            }
                        }
            }
            scratch.rewind(persistent_mark);
        }
    }
    // Raw indices and D3DX's per-vertex corner chains (head insertion), which the
    // weld refusal walks: a vertex never welds to a representative when some
    // face references both raw indices.
    std::memset(corner_head, 0xff, size_t(V) * sizeof(uint32_t));
    for (uint32_t f = 0; f < F; ++f)
        for (unsigned k = 0; k < 3; ++k) {
            const size_t i = size_t(f) * 3 + k;
            uint32_t index;
            if (in.indices_32bit)
                std::memcpy(&index, static_cast<const unsigned char*>(in.indices) + i * 4, 4);
            else {
                uint16_t narrow;
                std::memcpy(&narrow, static_cast<const unsigned char*>(in.indices) + i * 2, 2);
                index = narrow;
            }
            if (index >= V) {
                report.status = Status::IndexRange;
                return report;
            }
            raw[i] = index;
            corner_next[i] = corner_head[index];
            corner_head[index] = uint32_t(i);
        }
    auto shares_face = [&](uint32_t v, uint32_t w) noexcept {
        for (uint32_t c = corner_head[v]; c != unused; c = corner_next[c]) {
            const uint32_t* face = raw + size_t(c / 3) * 3;
            if (face[0] == w || face[1] == w || face[2] == w) return true;
        }
        return false;
    };
    // Point representatives.
    std::memset(rep, 0xff, size_t(V) * sizeof(uint32_t));
    if (in.epsilon == 0.f) {
        // D3DX hashes the exact position; among equal positions the bucket lists
        // the representatives most recent first, and a vertex takes the first one
        // that shares no face with it or becomes a new representative.
        auto* chain_head = order;
        auto* chain_next = class_next; // reused: no sweep on this path
        std::memset(chain_head, 0xff, size_t(V) * sizeof(uint32_t));
        for (uint32_t v = 0; v < V; ++v) {
            uint32_t r = chain_head[cls[v]];
            for (; r != unused; r = chain_next[r])
                if (!policy.weld_refusal || !shares_face(v, r)) break;
            if (r != unused) {
                rep[v] = r;
                continue;
            }
            chain_next[v] = chain_head[cls[v]];
            chain_head[cls[v]] = v;
            rep[v] = v;
        }
    } else {
        // D3DX sweeps the vertices in descending order of the byte-0 float with a
        // window of keys within epsilon below the current one; under the gate the
        // vertices that pass its distance test are exactly the class members, so
        // each class is threaded in sweep order and walked from the representative.
        // Under the gate the sweep order decides only which member of a class is
        // its representative and how a class splits when a weld is refused. With
        // the position at byte 0 (class members share the key, so the window
        // never cuts a class) and no face referencing two distinct vertices of one
        // class (so no refusal is possible), every class welds whole onto whichever
        // member comes first, and the output does not depend on which one: face
        // adjacency only compares representatives for equality and the score
        // normals read identical positions. The sort is then skipped and the
        // class's lowest index stands in for the heap's choice.
        bool order_matters = head != positions;
        for (uint32_t f = 0; f < F && !order_matters; ++f) {
            const uint32_t* r = raw + size_t(f) * 3;
            order_matters = (r[0] != r[1] && cls[r[0]] == cls[r[1]]) || (r[1] != r[2] && cls[r[1]] == cls[r[2]]) ||
                            (r[0] != r[2] && cls[r[0]] == cls[r[2]]);
        }
        if (!order_matters) {
            for (uint32_t v = 0; v < V; ++v) rep[v] = cls[v];
        } else {
            auto* packed = scratch.array<uint64_t>(V);
            if (scratch.overflow()) {
                report.status = Status::Allocation;
                return report;
            }
            for (uint32_t v = 0; v < V; ++v) {
                uint32_t bits;
                std::memcpy(&bits, &head[v].x, sizeof bits);
                packed[v] = uint64_t(order_code(bits)) << 32 | v;
            }
            if (policy.heap_order)
                heapsort(
                    packed, V, [](uint64_t a, uint64_t b) noexcept { return (a >> 32) <= (b >> 32); },
                    [](uint64_t a, uint64_t b) noexcept { return (a >> 32) < (b >> 32); });
            else
                heapsort(
                    packed, V,
                    [](uint64_t a, uint64_t b) noexcept {
                        return (a >> 32) < (b >> 32) || ((a >> 32) == (b >> 32) && uint32_t(a) >= uint32_t(b));
                    },
                    [](uint64_t a, uint64_t b) noexcept {
                        return (a >> 32) < (b >> 32) || ((a >> 32) == (b >> 32) && uint32_t(a) > uint32_t(b));
                    });
            for (uint32_t i = 0; i < V; ++i) order[i] = uint32_t(packed[i]);
            scratch.rewind(persistent_mark);
            // Thread each class in sweep order: walking the order backwards and
            // prepending makes class_next[v] the member that follows v; rep[] holds
            // the class heads meanwhile and is reset afterwards.
            std::memset(class_next, 0xff, size_t(V) * sizeof(uint32_t));
            for (uint32_t i = V; i-- > 0;) {
                const uint32_t v = order[i], c = cls[v];
                class_next[v] = rep[c];
                rep[c] = v;
            }
            std::memset(rep, 0xff, size_t(V) * sizeof(uint32_t));
            const double eps = double(in.epsilon);
            for (uint32_t i = 0; i < V; ++i) {
                const uint32_t v = order[i];
                if (rep[v] != unused) continue;
                rep[v] = v;
                for (uint32_t w = class_next[v]; w != unused; w = class_next[w]) {
                    if (eps < double(head[v].x) - double(head[w].x))
                        break; // outside D3DX's key window (only possible when the key is not the position)
                    if (rep[w] != unused) continue;
                    if (policy.weld_refusal && shares_face(v, w)) {
                        ++report.refused_welds;
                        continue;
                    }
                    rep[w] = v;
                }
            }
        }
    }
    for (uint32_t v = 0; v < V; ++v) report.representatives += rep[v] == v;
    report.welded = V - report.representatives;
    // Face corners as representatives; a face whose representatives repeat
    // contributes no edge and receives no neighbour (raw repeats are counted too).
    for (uint32_t f = 0; f < F; ++f) {
        const uint32_t* r = raw + size_t(f) * 3;
        uint32_t* c = corners + size_t(f) * 3;
        for (unsigned k = 0; k < 3; ++k) c[k] = rep[r[k]];
        const bool raw_degenerate = r[0] == r[1] || r[1] == r[2] || r[0] == r[2];
        const bool degenerate = c[0] == c[1] || c[1] == c[2] || c[0] == c[2];
        report.degenerate_faces += raw_degenerate;
        report.welded_degenerate_faces += degenerate && !raw_degenerate;
        active[f] = !degenerate;
    }
    // Directed-edge table keyed by the exact (v1,v2) pair, one chain per key
    // (open addressing; a chain that empties keeps its anchor as a tombstone).
    // D3DX chains by v1 modulo V/3 and filters on the pair, so the relative order
    // of the matching entries is the same: most recent insertion first.
    std::memset(slots, 0xff, size_t(edge_slots) * sizeof(Slot));
    const size_t edge_mask = size_t(edge_slots - 1);
    auto edge_v1 = [&](uint32_t id) noexcept { return corners[id]; };
    auto edge_v2 = [&](uint32_t id) noexcept {
        const uint32_t f = id / 3, k = id - f * 3;
        return corners[size_t(f) * 3 + (k + 1) % 3];
    };
    auto edge_other = [&](uint32_t id) noexcept {
        const uint32_t f = id / 3, k = id - f * 3;
        return corners[size_t(f) * 3 + (k + 2) % 3];
    };
    auto slot_of = [&](uint32_t a, uint32_t b) noexcept { // slot holding key (a,b), or its first free slot
        size_t s = size_t(mix((uint64_t(a) << 32 | b) * 0x9e3779b97f4a7c15ull)) & edge_mask;
        for (;;) {
            const uint32_t anchor = slots[s].anchor;
            if (anchor == unused || (edge_v1(anchor) == a && edge_v2(anchor) == b)) return s;
            s = (s + 1) & edge_mask;
        }
    };
    bool duplicate_edges = false;
    auto insert = [&](uint32_t id) noexcept {
        const size_t s = slot_of(edge_v1(id), edge_v2(id));
        if (slots[s].anchor == unused) slots[s].anchor = id;
        duplicate_edges |= slots[s].head != unused;
        if (policy.head_insertion || slots[s].head == unused) {
            next[id] = slots[s].head;
            slots[s].head = id;
        } else {
            uint32_t tail = slots[s].head;
            while (next[tail] != unused) tail = next[tail];
            next[tail] = id;
            next[id] = unused;
        }
    };
    for (uint32_t f = 0; f < F; ++f) {
        if (!active[f]) continue;
        for (uint32_t k = 0; k < 3; ++k) insert(f * 3 + k);
    }
    // No output has been touched. A single candidate needs no normal score;
    // duplicate keys without a reverse also need none. Only ambiguous meshes
    // pay this scan, and rejection happens before any normal allocation/math.
    if ((policy.refuse_competing_normals || policy.normalize == Normalize::Sse2) && duplicate_edges) {
        for (size_t s = 0; s < size_t(edge_slots); ++s) {
            const uint32_t first = slots[s].head;
            if (first == unused || next[first] == unused) continue;
            if (slots[slot_of(edge_v2(first), edge_v1(first))].head != unused) {
                report.potential_competing_normals = true;
                if (policy.refuse_competing_normals) {
                    report.status = Status::CompetingNormals;
                    return report;
                }
                break;
            }
        }
    }
    // D3DX unlinks a matched entry inside the lookup and removes the querying
    // edge's own entry (FUN_0058a8ff, a chain walk) after a successful lookup. A
    // retired flag hides an entry from the scans instead: the remaining entries
    // keep their relative order, the own entry needs no second hash lookup, and
    // clearing the flag puts a refused entry back at its old chain position, which
    // is where D3DX relinks it.
    std::memset(retired, 0, size_t(E));
    for (size_t i = 0; i < size_t(E); ++i) adjacency[i] = unused;
    // Face normals are needed only where a chain offers several candidates; they
    // are then computed once per edge (from the edge's own corner order, as D3DX).
    // The output contract (written whole only on Ok) forbids failing here, so a
    // cache that cannot be allocated only costs the recomputation.
    NormalCache cache;
    auto normal_of = [&](uint32_t id) noexcept -> Vec {
        if (!cache.normals) return face_normal(head, edge_v1(id), edge_v2(id), edge_other(id), policy.normalize);
        if (!cache.ready[id]) {
            cache.normals[id] = face_normal(head, edge_v1(id), edge_v2(id), edge_other(id), policy.normalize);
            cache.ready[id] = 1;
        }
        return cache.normals[id];
    };
    for (uint32_t f = 0; f < F; ++f) {
        if (!active[f]) continue;
        uint32_t* row = adjacency + size_t(f) * 3;
        for (uint32_t k = 0; k < 3; ++k) {
            const uint32_t own = f * 3 + k;
            if (row[k] != unused) continue;
            const uint32_t vb = corners[own], va = edge_v2(own); // the reverse edge (va, vb) is looked up
            const size_t s = slot_of(va, vb);
            uint32_t found = unused;
            if (slots[s].anchor != unused) {
                unsigned candidates = 0;
                float best = 0.f;
                bool scored = false;
                for (uint32_t cur = slots[s].head; cur != unused; cur = next[cur]) {
                    if (retired[cur]) continue;
                    ++candidates;
                    if (found == unused) {
                        found = cur;
                        continue;
                    }
                    if (!policy.normal_selection) continue;
                    cache.acquire(size_t(E));
                    if (!scored) {
                        best = score(normal_of(found), normal_of(own));
                        scored = true;
                    }
                    const float candidate = score(normal_of(cur), normal_of(own));
                    if (best < candidate) {
                        best = candidate;
                        found = cur;
                        ++report.normal_selected;
                    }
                }
                if (candidates > 1) ++report.multi_candidates;
            }
            if (found == unused) {
                if (policy.retire_own_entry) retired[own] = 1;
                continue;
            }
            // D3DX: the selected entry is unlinked inside the lookup and the querying
            // edge's own entry is removed after a successful lookup; the
            // single-adjacency check then compares the earlier slots of this face.
            retired[found] = 1;
            retired[own] = 1;
            const uint32_t g = found / 3;
            bool repeated = false;
            for (uint32_t j = 0; j < (policy.later_slot_check ? 3u : k); ++j)
                if (j != k && row[j] == g) repeated = true;
            if (repeated) {
                ++report.repeated_neighbours;
                if (!policy.unlink_refused) retired[found] = 0;
                continue;
            }
            row[k] = g;
            adjacency[found] = f; // the found face's slot is its corner whose representative starts the reverse edge:
                                  // the entry's own point
        }
    }
    for (size_t i = 0; i < size_t(E); ++i) report.unmatched += adjacency[i] == unused;
    report.status = Status::Ok;
    return report;
}
}
