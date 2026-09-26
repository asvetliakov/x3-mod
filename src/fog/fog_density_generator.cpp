// Stored-density fog generator: see fog_density_generator.h for the contract.
// Reference: tools/analysis/fog_density_runtime_screen.py (field, LazyStore.get,
// window_origin, pack_address) and fog_sector_patches_preview.py (value_noise).
#include "fog_density_generator.h"

#include <cmath>
#include <cstring>

namespace x3m {
namespace fog {
namespace {

// --- Value-noise lattice (fog_sector_patches_preview.value_noise) --------

// 32-bit finaliser (mix_array): all products wrap modulo 2^32 like the
// masked uint64 arithmetic of the reference.
inline std::uint32_t mix32(std::uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

// Corner value 2*((h>>8)+.5)/2^24-1 in (-1,1): one rounding, as the reference.
inline double corner_value(std::uint32_t h) {
    return 2.0 * (static_cast<double>(h >> 8) + .5) / 16777216.0 - 1.0;
}

// Per-axis lattice terms of one coordinate: the hashed cell products of the
// two bracketing integer planes and the quintic weights (1-q, q). Shared by
// the eight corners of a cell and, for unrotated octaves, by every prefilter
// point that has the same coordinate on that axis.
struct Axis {
    std::uint32_t h0, h1;
    double w0, w1;
};

// floor(u) as an int32 for |u| < 2^31: SSE2 cvttsd2si plus a sign fix, so the
// i686 build never enters x87 (std::floor and double->int64 both do there).
// Lattice coordinates are world units over >= 2048, far inside that domain.
inline std::int32_t floor_i32(double u) {
    const std::int32_t i = static_cast<std::int32_t>(u); // truncation toward zero
    return static_cast<double>(i) > u ? i - 1 : i;
}

inline Axis axis_terms(double u, std::uint32_t multiplier) {
    const std::int32_t cell = floor_i32(u);
    const double f = u - static_cast<double>(cell);
    const double q = ((f * f) * f) * ((f * (f * 6.0 - 15.0)) + 10.0);
    // NumPy floors into int64 then masks to 32 bits: the same two's complement
    // low word as this int32 cell (wrapping uint32 products).
    Axis a;
    a.h0 = static_cast<std::uint32_t>(cell) * multiplier;
    a.h1 = (static_cast<std::uint32_t>(cell) + 1u) * multiplier;
    a.w0 = 1.0 - q;
    a.w1 = q;
    return a;
}

constexpr std::uint32_t kMulX = 0x9e3779b9u;
constexpr std::uint32_t kMulY = 0x85ebca6bu;
constexpr std::uint32_t kMulZ = 0xc2b2ae35u;
constexpr std::uint32_t kSalt = 0x58434647u;

// Octave seed mix_array(((octave+1)*0x9e3779b9) & MASK) folded with the salt.
inline std::uint32_t octave_base(int octave) {
    return kSalt ^ mix32(static_cast<std::uint32_t>(octave + 1) * kMulX);
}

// Trilinear quintic lattice value in double, corners accumulated in the
// reference order (dz outer, dy, dx inner; weight product ((wx*wy)*wz)*v),
// then rounded once to float32 (`astype(F)`).
inline float lattice(const Axis& x, const Axis& y, const Axis& z, std::uint32_t base) {
    // wx*wy is the same product for both z planes; computing it once is bit-identical.
    const double w00 = x.w0 * y.w0, w10 = x.w1 * y.w0, w01 = x.w0 * y.w1, w11 = x.w1 * y.w1;
    const std::uint32_t xy00 = x.h0 ^ y.h0 ^ base, xy10 = x.h1 ^ y.h0 ^ base;
    const std::uint32_t xy01 = x.h0 ^ y.h1 ^ base, xy11 = x.h1 ^ y.h1 ^ base;
    double out = 0.0;
    out += (w00 * z.w0) * corner_value(mix32(xy00 ^ z.h0));
    out += (w10 * z.w0) * corner_value(mix32(xy10 ^ z.h0));
    out += (w01 * z.w0) * corner_value(mix32(xy01 ^ z.h0));
    out += (w11 * z.w0) * corner_value(mix32(xy11 ^ z.h0));
    out += (w00 * z.w1) * corner_value(mix32(xy00 ^ z.h1));
    out += (w10 * z.w1) * corner_value(mix32(xy10 ^ z.h1));
    out += (w01 * z.w1) * corner_value(mix32(xy01 ^ z.h1));
    out += (w11 * z.w1) * corner_value(mix32(xy11 ^ z.h1));
    return static_cast<float>(out);
}

// --- Frozen field constants (fog_mass_column_screen / fog_density_runtime_screen)

constexpr double kPeriod = 32768.0; // fog_distance_replay.PERIOD
// Octave scales: 2P, P (rotated by R), P/4 (rotated by R²), P/16; exact powers of two.
constexpr double kInvScaleMass = 1.0 / (2.0 * kPeriod);
constexpr double kInvScaleMassRotated = 1.0 / kPeriod;
constexpr double kInvScaleDetailLow = 1.0 / (kPeriod / 4.0);
constexpr double kInvScaleDetailHigh = 1.0 / (kPeriod / 16.0);
constexpr int kOctaveMass = 3, kOctaveMassRotated = 4, kOctaveDetailLow = 5, kOctaveDetailHigh = 6;

// Proper rotation R = [[1,2,2],[2,1,-2],[-2,2,-1]]/3 as float64.
constexpr double kR[3][3] = {
    {1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0}, {2.0 / 3.0, 1.0 / 3.0, -2.0 / 3.0}, {-2.0 / 3.0, 2.0 / 3.0, -1.0 / 3.0}};

// R² exactly as the reference forms it: Python sum() starting at 0 over k.
// constexpr: evaluated at compile time in IEEE double (no static-init order).
struct RotationSquare {
    double m[3][3];
    constexpr RotationSquare()
        : m{} {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                double s = 0.0;
                for (int k = 0; k < 3; ++k) s = s + kR[i][k] * kR[k][j];
                m[i][j] = s;
            }
    }
};
constexpr RotationSquare kR2{};

struct Vec3d {
    double x, y, z;
};

// Component rotation (x0*M[i,0] + x1*M[i,1]) + x2*M[i,2], no contraction.
inline Vec3d rotate(const Vec3d& p, const double (&m)[3][3]) {
    Vec3d r;
    r.x = (p.x * m[0][0] + p.y * m[0][1]) + p.z * m[0][2];
    r.y = (p.x * m[1][0] + p.y * m[1][1]) + p.z * m[1][2];
    r.z = (p.x * m[2][0] + p.y * m[2][1]) + p.z * m[2][2];
    return r;
}

// float32 combination of the four octave values, reference float32 order.
inline float combine(float mass, float mass_rotated, float detail_low, float detail_high) {
    const float f = (2.0f * mass + mass_rotated) / 3.0f;
    float t = (f - 0.1f) / 0.3f; // smoothstep(.10,.40): NumPy casts the scalars to float32
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float B = (t * t) * (3.0f - 2.0f * t);
    const float dlo = (detail_low + 1.0f) / 2.0f;
    const float dhi = (detail_high + 1.0f) / 2.0f;
    const float rho = B * (0.5f + 0.5f * dlo) - (0.2f * (1.0f - B)) * dhi;
    return rho > 0.0f ? rho : 0.0f; // np.maximum(0, rho): -0 becomes +0
}

// The eight prefilter offsets in itertools.product((-.25,.25), repeat=3) order.
constexpr double kPrefilterOffsets[8][3] = {{-.25, -.25, -.25}, {-.25, -.25, .25}, {-.25, .25, -.25}, {-.25, .25, .25},
                                            {.25, -.25, -.25},  {.25, -.25, .25},  {.25, .25, -.25},  {.25, .25, .25}};

// NumPy float32 mean of eight: pairwise sum, then true_divide by 8.
inline float mean8(const float (&a)[8]) {
    const float s = ((a[0] + a[1]) + (a[2] + a[3])) + ((a[4] + a[5]) + (a[6] + a[7]));
    return s / 8.0f;
}

// Eight field evaluations of a node with the unrotated octaves' axis terms
// shared across the two coordinates each axis takes (the performance pass:
// 6 instead of 24 axis preparations per node for octaves 3 and 6; the rotated
// octaves see eight distinct triples and keep the plain path).
inline float node_mean(double delta, const NodeKey& key, const WorldOffset& offset) {
    // Node keys are floor(world/delta) +- 128 and fit int32 for any reachable
    // world; the int32 conversion keeps the i686 build on SSE2 (int64 would go x87).
    const double kx = static_cast<double>(static_cast<std::int32_t>(key.x));
    const double ky = static_cast<double>(static_cast<std::int32_t>(key.y));
    const double kz = static_cast<double>(static_cast<std::int32_t>(key.z));
    double px[2], py[2], pz[2];
    for (int s = 0; s < 2; ++s) {
        const double o = s ? .25 : -.25;
        px[s] = (kx + o) * delta + offset.x;
        py[s] = (ky + o) * delta + offset.y;
        pz[s] = (kz + o) * delta + offset.z;
    }
    Axis mx[2], my[2], mz[2], hx[2], hy[2], hz[2];
    for (int s = 0; s < 2; ++s) {
        mx[s] = axis_terms(px[s] * kInvScaleMass, kMulX);
        my[s] = axis_terms(py[s] * kInvScaleMass, kMulY);
        mz[s] = axis_terms(pz[s] * kInvScaleMass, kMulZ);
        hx[s] = axis_terms(px[s] * kInvScaleDetailHigh, kMulX);
        hy[s] = axis_terms(py[s] * kInvScaleDetailHigh, kMulY);
        hz[s] = axis_terms(pz[s] * kInvScaleDetailHigh, kMulZ);
    }
    const std::uint32_t base_mass = octave_base(kOctaveMass);
    const std::uint32_t base_rot = octave_base(kOctaveMassRotated);
    const std::uint32_t base_low = octave_base(kOctaveDetailLow);
    const std::uint32_t base_high = octave_base(kOctaveDetailHigh);
    float rho[8];
    for (int i = 0; i < 8; ++i) {
        const int sx = kPrefilterOffsets[i][0] > 0 ? 1 : 0;
        const int sy = kPrefilterOffsets[i][1] > 0 ? 1 : 0;
        const int sz = kPrefilterOffsets[i][2] > 0 ? 1 : 0;
        const Vec3d p = {px[sx], py[sy], pz[sz]};
        const Vec3d r1 = rotate(p, kR);
        const Vec3d r2 = rotate(p, kR2.m);
        const float mass = lattice(mx[sx], my[sy], mz[sz], base_mass);
        const float mass_rotated = lattice(axis_terms(r1.x * kInvScaleMassRotated, kMulX),
                                           axis_terms(r1.y * kInvScaleMassRotated, kMulY),
                                           axis_terms(r1.z * kInvScaleMassRotated, kMulZ), base_rot);
        const float detail_low = lattice(axis_terms(r2.x * kInvScaleDetailLow, kMulX),
                                         axis_terms(r2.y * kInvScaleDetailLow, kMulY),
                                         axis_terms(r2.z * kInvScaleDetailLow, kMulZ), base_low);
        const float detail_high = lattice(hx[sx], hy[sy], hz[sz], base_high);
        rho[i] = combine(mass, mass_rotated, detail_low, detail_high);
    }
    return mean8(rho);
}

inline void store_half(std::uint8_t* at, std::uint16_t word) {
    at[0] = static_cast<std::uint8_t>(word & 0xff);
    at[1] = static_cast<std::uint8_t>(word >> 8);
}

} // namespace

// --- Field --------------------------------------------------------------

float density_field(double x, double y, double z) {
    const Vec3d p = {x, y, z};
    const Vec3d r1 = rotate(p, kR);
    const Vec3d r2 = rotate(p, kR2.m);
    const float mass = lattice(axis_terms(x * kInvScaleMass, kMulX), axis_terms(y * kInvScaleMass, kMulY),
                               axis_terms(z * kInvScaleMass, kMulZ), octave_base(kOctaveMass));
    const float mass_rotated = lattice(axis_terms(r1.x * kInvScaleMassRotated, kMulX),
                                       axis_terms(r1.y * kInvScaleMassRotated, kMulY),
                                       axis_terms(r1.z * kInvScaleMassRotated, kMulZ), octave_base(kOctaveMassRotated));
    const float detail_low = lattice(axis_terms(r2.x * kInvScaleDetailLow, kMulX),
                                     axis_terms(r2.y * kInvScaleDetailLow, kMulY),
                                     axis_terms(r2.z * kInvScaleDetailLow, kMulZ), octave_base(kOctaveDetailLow));
    const float detail_high = lattice(axis_terms(x * kInvScaleDetailHigh, kMulX),
                                      axis_terms(y * kInvScaleDetailHigh, kMulY),
                                      axis_terms(z * kInvScaleDetailHigh, kMulZ), octave_base(kOctaveDetailHigh));
    return combine(mass, mass_rotated, detail_low, detail_high);
}

float node_density(double delta, const NodeKey& key, const WorldOffset& offset) {
    return node_mean(delta, key, offset);
}

std::uint16_t node_word(double delta, const NodeKey& key, const WorldOffset& offset) {
    return float_to_half_rne(node_mean(delta, key, offset));
}

// --- FP16 law -----------------------------------------------------------

std::uint16_t float_to_half_rne(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    const std::uint32_t exponent = (bits >> 23) & 0xffu;
    const std::uint32_t mantissa = bits & 0x7fffffu;
    if (exponent == 0xffu) {
        if (mantissa == 0) return static_cast<std::uint16_t>(sign | 0x7c00u);
        // NaN: keep the top payload bits, force quiet so the result stays NaN.
        return static_cast<std::uint16_t>(sign | 0x7c00u | 0x200u | (mantissa >> 13));
    }
    const int half_exponent = static_cast<int>(exponent) - 127 + 15;
    if (half_exponent >= 0x1f) return static_cast<std::uint16_t>(sign | 0x7c00u); // overflow
    if (half_exponent <= 0) {
        // Subnormal or zero result: the value is (mantissa|implicit) * 2^(exponent-150);
        // one half subnormal step is 2^-24, so shift by 126-exponent (>= 14).
        if (exponent == 0 && mantissa == 0) return sign;
        const std::uint32_t full = mantissa | (exponent ? 0x800000u : 0u);
        const int shift = 126 - static_cast<int>(exponent);
        if (shift > 25) return sign; // below half the smallest subnormal: rounds to zero
        const std::uint32_t half = full >> shift;
        const std::uint32_t remainder = full & ((1u << shift) - 1u);
        const std::uint32_t halfway = 1u << (shift - 1);
        std::uint32_t result = half;
        if (remainder > halfway || (remainder == halfway && (half & 1u))) ++result;
        return static_cast<std::uint16_t>(sign | result);
    }
    std::uint32_t result = (static_cast<std::uint32_t>(half_exponent) << 10) | (mantissa >> 13);
    const std::uint32_t remainder = mantissa & 0x1fffu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (result & 1u))) ++result; // may carry into inf
    return static_cast<std::uint16_t>(sign | result);
}

float half_to_float(std::uint16_t half) {
    const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000u) << 16;
    const std::uint32_t exponent = (half >> 10) & 0x1fu;
    const std::uint32_t mantissa = half & 0x3ffu;
    std::uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            // Subnormal: normalise.
            int e = -1;
            std::uint32_t m = mantissa;
            do {
                ++e;
                m <<= 1;
            } while (!(m & 0x400u));
            // m was shifted e+1 times: value = 1.frac * 2^(-15-e), float exponent 127-15-e.
            bits = sign | (static_cast<std::uint32_t>(112 - e) << 23) | ((m & 0x3ffu) << 13);
        }
    } else if (exponent == 0x1f) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    }
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// --- Address law --------------------------------------------------------

namespace {
// floor(p/delta) through the SSE2 int32 path (keys fit int32 for any reachable
// world: |p| < 2^31 * 512); widened to the signed 64-bit key type.
inline std::int64_t floor_key(double q) {
    return static_cast<std::int64_t>(floor_i32(q));
}
} // namespace

NodeKey node_key(double delta, double px, double py, double pz) {
    NodeKey k;
    k.x = floor_key(px / delta);
    k.y = floor_key(py / delta);
    k.z = floor_key(pz / delta);
    return k;
}

NodeKey window_origin(double delta, double cx, double cy, double cz) {
    NodeKey o = node_key(delta, cx, cy, cz);
    o.x -= kWindowHalf;
    o.y -= kWindowHalf;
    o.z -= kWindowHalf;
    return o;
}

bool window_contains(const NodeKey& origin, const NodeKey& base) {
    const std::int64_t lx = base.x - origin.x, ly = base.y - origin.y, lz = base.z - origin.z;
    return lx >= 0 && lx <= kWindowLastBase && ly >= 0 && ly <= kWindowLastBase && lz >= 0 && lz <= kWindowLastBase;
}

int storage_index(std::int64_t k) {
    return static_cast<int>(k & (kWindowNodes - 1)); // Euclidean mod 128 for two's complement
}

std::int64_t node_for_storage(std::int64_t origin, int storage) {
    return origin + ((static_cast<std::int64_t>(storage) - origin) & (kWindowNodes - 1));
}

AtlasTexel atlas_texel(int sx, int sy, int sz) {
    const int group = sz / kLanes;
    AtlasTexel t;
    t.x = (group % kGroupsPerRow) * kTileTexels + sx;
    t.y = (group / kGroupsPerRow) * kTileTexels + sy;
    t.lane = sz % kLanes;
    return t;
}

std::size_t atlas_offset(const AtlasTexel& texel, std::size_t pitch) {
    return std::size_t(texel.y) * pitch + std::size_t(texel.x) * kTexelBytes;
}

namespace {
inline double smoothstep(double lo, double hi, double x) {
    double t = (x - lo) / (hi - lo);
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return t * t * (3.0 - 2.0 * t);
}
} // namespace

LodWeights lod_weights(double distance) {
    LodWeights w;
    w.lambda = 1.0 - smoothstep(kLodStart, kLodEnd, distance);
    w.taper = 1.0 - smoothstep(kTaperStart, kTaperEnd, distance);
    w.fine_level = w.lambda > 0.0;
    w.far_level = w.lambda < 1.0;
    return w;
}

Address address(int level, const NodeKey& origin, double px, double py, double pz) {
    Address a{};
    if (level < 0 || level >= kLevelCount) {
        a.level = -1; // refused: no such level; nothing is contained
        return a;
    }
    const double delta = kLevelDelta[level];
    a.level = level;
    const double qx = px / delta, qy = py / delta, qz = pz / delta;
    const std::int32_t bx = floor_i32(qx), by = floor_i32(qy), bz = floor_i32(qz);
    a.key.x = bx;
    a.key.y = by;
    a.key.z = bz;
    a.fx = qx - static_cast<double>(bx);
    a.fy = qy - static_cast<double>(by);
    a.fz = qz - static_cast<double>(bz);
    a.local.x = a.key.x - origin.x;
    a.local.y = a.key.y - origin.y;
    a.local.z = a.key.z - origin.z;
    a.texel = atlas_texel(storage_index(a.key.x), storage_index(a.key.y), storage_index(a.key.z));
    a.contained = window_contains(origin, a.key);
    return a;
}

// --- Slab generation ----------------------------------------------------

void generate_brick(double delta, const NodeKey& origin, const WorldOffset& offset, int brick_x, int brick_y, int group,
                    std::uint8_t* out, std::size_t pitch) {
    NodeKey key;
    std::int64_t kz[kLanes];
    for (int lane = 0; lane < kLanes; ++lane) kz[lane] = node_for_storage(origin.z, kLanes * group + lane);
    for (int row = 0; row < kBrickTexels; ++row) {
        key.y = node_for_storage(origin.y, kBrickTexels * brick_y + row);
        std::uint8_t* texel = out + std::size_t(row) * pitch;
        for (int column = 0; column < kBrickTexels; ++column, texel += kTexelBytes) {
            key.x = node_for_storage(origin.x, kBrickTexels * brick_x + column);
            for (int lane = 0; lane < kLanes; ++lane) {
                key.z = kz[lane];
                store_half(texel + 2 * lane, float_to_half_rne(node_mean(delta, key, offset)));
            }
        }
    }
}

void generate_tile(double delta, const NodeKey& origin, const WorldOffset& offset, int group, std::uint8_t* out,
                   std::size_t pitch) {
    const int bricks = kWindowNodes / kBrickTexels;
    for (int by = 0; by < bricks; ++by)
        for (int bx = 0; bx < bricks; ++bx)
            generate_brick(delta, origin, offset, bx, by, group,
                           out + std::size_t(by) * kBrickTexels * pitch + std::size_t(bx) * kBrickPitch, pitch);
    // Duplicate border: column 128 <- column 0 (rows 0..127), row 128 <- row 0 (columns 0..128).
    for (int row = 0; row < kWindowNodes; ++row)
        std::memcpy(out + std::size_t(row) * pitch + std::size_t(kWindowNodes) * kTexelBytes,
                    out + std::size_t(row) * pitch, kTexelBytes);
    std::memcpy(out + std::size_t(kWindowNodes) * pitch, out, std::size_t(kTileTexels) * kTexelBytes);
}

void duplicate_tile_border(int group, std::uint8_t* atlas, std::size_t pitch) {
    const AtlasTexel corner = atlas_texel(0, 0, kLanes * group);
    std::uint8_t* tile = atlas + atlas_offset(corner, pitch);
    for (int row = 0; row < kWindowNodes; ++row)
        std::memcpy(tile + std::size_t(row) * pitch + std::size_t(kWindowNodes) * kTexelBytes,
                    tile + std::size_t(row) * pitch, kTexelBytes);
    std::memcpy(tile + std::size_t(kWindowNodes) * pitch, tile, std::size_t(kTileTexels) * kTexelBytes);
}

} // namespace fog
} // namespace x3m
