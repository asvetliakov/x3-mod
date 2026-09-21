// Stored-density fog: CPU density generator, node address law, FP16 law and
// atlas slab packing (docs/architecture/fog-density-runtime-integration.md §1–2).
//
// Portable C++17, no D3D and no engine memory. Every constant mirrors the
// validated reference tools/analysis/fog_density_runtime_screen.py (`field`,
// `LazyStore.get`, `window_origin`, `pack_address`); nothing here is tunable.
//
// Numerics contract: the noise lattice is evaluated in IEEE double with the
// reference's operation order, the mass/detail combination in IEEE float32
// with the reference's (NumPy weak-scalar) float32 order, the eight-point
// prefilter mean is the float32 pairwise sum ((a0+a1)+(a2+a3))+((a4+a5)+(a6+a7))
// divided by 8, and the stored word is round-to-nearest-even binary16. Build
// with FP contraction disabled (-ffp-contract=off) and SSE2 arithmetic on x86
// (-msse2 -mfpmath=sse); no fast-math. Under those rules the FP16 words are
// bit-identical between the reference and both native and x86 builds
// (verification/analysis/test_fog_density_generator.py).
#pragma once
#include <cstddef>
#include <cstdint>

namespace x3m {
namespace fog {

// Level spacing in render units (0.2 m each): fine 102.4 m, far 819.2 m.
constexpr double kFineDelta = 512.0;
constexpr double kFarDelta = 4096.0;
constexpr int kLevelCount = 2;
constexpr double kLevelDelta[kLevelCount] = {kFineDelta, kFarDelta};

// Window: 128 nodes per axis, origin floor(camera/delta)-63, trilinear corners
// valid for local indices 0..126 (both corners resident).
constexpr int kWindowNodes = 128;
constexpr int kWindowHalf = 63;
constexpr int kWindowLastBase = 126;

// Atlas: 32 Z groups of four nodes (RGBA lanes) arranged 8×4 tiles of 129×129
// texels; texel column/row 128 of a tile duplicates storage index 0 so hardware
// bilinear filtering crosses the toroidal seam correctly. RGBA16F, 8 B/texel.
constexpr int kGroupCount = 32;
constexpr int kGroupsPerRow = 8;
constexpr int kLanes = 4;
constexpr int kTileTexels = 129;
constexpr int kAtlasWidth = kGroupsPerRow * kTileTexels;              // 1032
constexpr int kAtlasHeight = (kGroupCount / kGroupsPerRow) * kTileTexels; // 516
constexpr int kTexelBytes = 8;
constexpr std::size_t kAtlasPitch = std::size_t(kAtlasWidth) * kTexelBytes; // 8256
constexpr std::size_t kAtlasBytes = kAtlasPitch * kAtlasHeight;             // 4,260,096
constexpr int kBrickTexels = 32;   // one generation unit: 32×32 texels = 4096 nodes
constexpr std::size_t kTilePitch = std::size_t(kTileTexels) * kTexelBytes;
constexpr std::size_t kTileBytes = kTilePitch * kTileTexels;
constexpr std::size_t kBrickPitch = std::size_t(kBrickTexels) * kTexelBytes;
constexpr std::size_t kBrickBytes = kBrickPitch * kBrickTexels;

// LOD blend and horizon taper (render units), from the plan.
constexpr double kLodStart = 20000.0;
constexpr double kLodEnd = 30000.0;
constexpr double kTaperStart = 150000.0;
constexpr double kTaperEnd = 200000.0;

// Signed absolute world node coordinates (integer lattice of one level).
struct NodeKey {
    std::int64_t x, y, z;
};

// Per-sector world translation O_s (§1); zero for a stationary anchored field.
struct WorldOffset {
    double x, y, z;
};
constexpr WorldOffset kNoOffset = {0.0, 0.0, 0.0};

// --- Field --------------------------------------------------------------

// Frozen refined density rho(x) in float32, exact reference law, in [0,1].
float density_field(double x, double y, double z);

// Eight-point prefilter of the field around node k of a level: float32 mean of
// rho(delta*(k + s/4) + offset), s in {-1,+1}^3, in the reference's order.
float node_density(double delta, const NodeKey& key, const WorldOffset& offset);

// The stored word: RNE binary16 of node_density.
std::uint16_t node_word(double delta, const NodeKey& key, const WorldOffset& offset);

// --- FP16 law -----------------------------------------------------------

// Round-to-nearest-even float32 -> binary16, identical to NumPy astype(float16)
// for finite values (subnormals and overflow to infinity included); NaN keeps
// its sign and quiet payload bits. Software only: F16C is outside SSE2.
std::uint16_t float_to_half_rne(float value);
float half_to_float(std::uint16_t half);

// --- Address law --------------------------------------------------------

// floor(p/delta) per axis as signed 64-bit; exact for |p| < 2^52.
NodeKey node_key(double delta, double px, double py, double pz);

// Plan window origin floor(camera/delta) - 63 per axis.
NodeKey window_origin(double delta, double cx, double cy, double cz);

// True when base node and its +1 trilinear corner both lie inside the window.
bool window_contains(const NodeKey& origin, const NodeKey& base);

// Storage position node mod 128 (Euclidean, always 0..127), independent of the origin.
int storage_index(std::int64_t k);

// The unique node in [origin, origin+127] whose storage index is `storage`.
std::int64_t node_for_storage(std::int64_t origin, int storage);

struct AtlasTexel {
    int x, y, lane; // texel column/row in the 1032×516 atlas; RGBA lane 0..3
};

// Storage (sx, sy, sz) -> atlas texel and lane: group = sz/4, tile (group%8, group/8),
// texel = tile*129 + (sx, sy). Storage 0..127 only; the border copy is a write-side rule.
AtlasTexel atlas_texel(int sx, int sy, int sz);

// Byte offset of a texel inside a 1032×516 RGBA16F atlas with the given pitch.
std::size_t atlas_offset(const AtlasTexel& texel, std::size_t pitch = kAtlasPitch);

struct LodWeights {
    double lambda; // fine weight 1-smoothstep(20000,30000,s)
    double taper;  // horizon window 1-smoothstep(150000,200000,s)
    bool fine;     // lambda > 0: fine level sampled
    bool far;      // lambda < 1: far level sampled
};
LodWeights lod_weights(double distance);

// Full address of a world point at one level relative to a window origin.
// A level outside {0,1} is refused: level = -1, contained = false, rest zero.
struct Address {
    int level;        // 0 fine, 1 far, -1 refused
    NodeKey key;      // base node floor(p/delta)
    NodeKey local;    // key - origin (valid 0..126 when contained)
    AtlasTexel texel; // storage texel of the base node
    double fx, fy, fz; // trilinear fractions in [0,1)
    bool contained;
};
Address address(int level, const NodeKey& origin, double px, double py, double pz);

// --- Slab generation ----------------------------------------------------

// One 32×32-texel brick (4096 nodes) of Z group `group` (0..31) for the window
// at `origin`: storage x in [32*brick_x, +32), y in [32*brick_y, +32), lanes
// z storage 4*group..4*group+3. Writes RGBA16F texels at out + row*pitch.
void generate_brick(double delta, const NodeKey& origin, const WorldOffset& offset,
                    int brick_x, int brick_y, int group, std::uint8_t* out, std::size_t pitch);

// One 129×129 tile of a Z group: the 16 bricks plus the duplicate border
// (column/row 128 copy storage 0). Writes into out + row*pitch (pitch ≥ 129*8).
void generate_tile(double delta, const NodeKey& origin, const WorldOffset& offset,
                   int group, std::uint8_t* out, std::size_t pitch);

// Copies the border texels of one tile inside a full atlas (after bricks that
// touched storage 0 changed): column 128 <- column 0, row 128 <- row 0.
void duplicate_tile_border(int group, std::uint8_t* atlas, std::size_t pitch = kAtlasPitch);

}  // namespace fog
}  // namespace x3m
