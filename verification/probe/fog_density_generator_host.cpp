// Host/x86 witness tool for src/fog/fog_density_generator.cpp.
//
// Modes (text on stdin/stdout unless noted; every number is printed as raw
// hex so the Python test compares words, not decimal renderings):
//   constants                 R and R² float64 words, one row per line
//   field                     stdin "x y z" -> float32 hex of density_field
//   words <delta> [ox oy oz]  stdin "kx ky kz" -> "<half hex> <float32 hex>" of the node
//   half                      stdin float32 hex -> binary16 hex (RNE law)
//   unhalf                    stdin binary16 hex -> float32 hex
//   lod                       stdin distance -> lambda hex, taper hex, fine, far
//   layout                    atlas constants, then stdin "sx sy sz" -> offset, texel x y lane
//   border <group>            binary 1032×516 patterned atlas after duplicate_tile_border
//   address <level> cx cy cz  stdin "px py pz" -> key, local, texel x y lane, fractions, contained
//   tile <delta> ox oy oz group [wx wy wz]   binary 129×129 RGBA16F tile on stdout
//   bench [seconds]           JSON: nodes/s per level for 32×32 bricks, tile checksums
// Build: verification/probe/build_fog_density_generator_host.sh (native and i686).
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "../../src/fog/fog_density_generator.h"

namespace {

using namespace x3m::fog;

std::uint32_t float_bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, sizeof b);
    return b;
}

std::uint64_t double_bits(double d) {
    std::uint64_t b;
    std::memcpy(&b, &d, sizeof b);
    return b;
}

float bits_float(std::uint32_t b) {
    float f;
    std::memcpy(&f, &b, sizeof f);
    return f;
}

// FNV-1a over a byte range, printed with the tile checksums.
std::uint64_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < size; ++i) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

int mode_constants() {
    // R is private to the generator; print it from the same literal law so the
    // test can compare against NumPy's array([[1,2,2],[2,1,-2],[-2,2,-1]])/3 and R@R.
    const double r[3][3] = {{1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0},
                            {2.0 / 3.0, 1.0 / 3.0, -2.0 / 3.0},
                            {-2.0 / 3.0, 2.0 / 3.0, -1.0 / 3.0}};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) std::printf("%s%016llx", j ? " " : "", (unsigned long long)double_bits(r[i][j]));
        std::printf("\n");
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) s = s + r[i][k] * r[k][j];
            std::printf("%s%016llx", j ? " " : "", (unsigned long long)double_bits(s));
        }
        std::printf("\n");
    }
    return 0;
}

int mode_field() {
    double x, y, z;
    while (std::scanf("%lf %lf %lf", &x, &y, &z) == 3) std::printf("%08x\n", float_bits(density_field(x, y, z)));
    return 0;
}

int mode_words(int argc, char** argv) {
    if (argc < 3) return 2;
    const double delta = std::atof(argv[2]);
    WorldOffset offset = kNoOffset;
    if (argc >= 6) offset = {std::atof(argv[3]), std::atof(argv[4]), std::atof(argv[5])};
    long long kx, ky, kz;
    while (std::scanf("%lld %lld %lld", &kx, &ky, &kz) == 3) {
        const NodeKey key = {kx, ky, kz};
        const float mean = node_density(delta, key, offset);
        std::printf("%04x %08x\n", node_word(delta, key, offset), float_bits(mean));
    }
    return 0;
}

int mode_half() {
    unsigned int bits;
    while (std::scanf("%x", &bits) == 1) std::printf("%04x\n", float_to_half_rne(bits_float(bits)));
    return 0;
}

int mode_unhalf() {
    unsigned int bits;
    while (std::scanf("%x", &bits) == 1) std::printf("%08x\n", float_bits(half_to_float(static_cast<std::uint16_t>(bits))));
    return 0;
}

int mode_lod() {
    double s;
    while (std::scanf("%lf", &s) == 1) {
        const LodWeights w = lod_weights(s);
        std::printf("%016llx %016llx %d %d\n", (unsigned long long)double_bits(w.lambda), (unsigned long long)double_bits(w.taper),
                    w.fine_level ? 1 : 0, w.far_level ? 1 : 0);
    }
    return 0;
}

// Atlas constants, then one line "offset texel_x texel_y lane" per stdin "sx sy sz".
int mode_layout() {
    std::printf("%d %d %d %llu %llu %d %d\n", kAtlasWidth, kAtlasHeight, kTexelBytes, (unsigned long long)kAtlasPitch,
                (unsigned long long)kAtlasBytes, kTileTexels, kGroupCount);
    int sx, sy, sz;
    while (std::scanf("%d %d %d", &sx, &sy, &sz) == 3) {
        const AtlasTexel t = atlas_texel(sx, sy, sz);
        std::printf("%llu %d %d %d\n", (unsigned long long)atlas_offset(t), t.x, t.y, t.lane);
    }
    return 0;
}

// Fills a whole atlas with a texel pattern (word = x*7+y*13+lane, low 16 bits),
// applies duplicate_tile_border(group) and writes the atlas bytes to stdout.
int mode_border(int argc, char** argv) {
    if (argc < 3) return 2;
    const int group = std::atoi(argv[2]);
    if (group < 0 || group >= kGroupCount) return 2;
    std::vector<std::uint8_t> atlas(kAtlasBytes);
    for (int y = 0; y < kAtlasHeight; ++y)
        for (int x = 0; x < kAtlasWidth; ++x)
            for (int lane = 0; lane < kLanes; ++lane) {
                const std::uint16_t word = static_cast<std::uint16_t>(x * 7 + y * 13 + lane);
                std::uint8_t* at = atlas.data() + std::size_t(y) * kAtlasPitch + std::size_t(x) * kTexelBytes + 2 * lane;
                at[0] = static_cast<std::uint8_t>(word & 0xff);
                at[1] = static_cast<std::uint8_t>(word >> 8);
            }
    duplicate_tile_border(group, atlas.data(), kAtlasPitch);
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    return std::fwrite(atlas.data(), 1, atlas.size(), stdout) == atlas.size() ? 0 : 1;
}

int mode_address(int argc, char** argv) {
    if (argc < 6) return 2;
    const int level = std::atoi(argv[2]);
    if (level < 0 || level >= kLevelCount) {
        std::fprintf(stderr, "level must be 0 (fine) or 1 (far)\n");
        return 2;
    }
    const double delta = kLevelDelta[level];
    const NodeKey origin = window_origin(delta, std::atof(argv[3]), std::atof(argv[4]), std::atof(argv[5]));
    std::printf("origin %lld %lld %lld\n", (long long)origin.x, (long long)origin.y, (long long)origin.z);
    double px, py, pz;
    while (std::scanf("%lf %lf %lf", &px, &py, &pz) == 3) {
        const Address a = address(level, origin, px, py, pz);
        std::printf("%lld %lld %lld %lld %lld %lld %d %d %d %016llx %016llx %016llx %d\n", (long long)a.key.x,
                    (long long)a.key.y, (long long)a.key.z, (long long)a.local.x, (long long)a.local.y,
                    (long long)a.local.z, a.texel.x, a.texel.y, a.texel.lane, (unsigned long long)double_bits(a.fx),
                    (unsigned long long)double_bits(a.fy), (unsigned long long)double_bits(a.fz), a.contained ? 1 : 0);
    }
    return 0;
}

int mode_tile(int argc, char** argv) {
    if (argc < 7) return 2;
    const double delta = std::atof(argv[2]);
    const NodeKey origin = {std::atoll(argv[3]), std::atoll(argv[4]), std::atoll(argv[5])};
    const int group = std::atoi(argv[6]);
    WorldOffset offset = kNoOffset;
    if (argc >= 10) offset = {std::atof(argv[7]), std::atof(argv[8]), std::atof(argv[9])};
    std::vector<std::uint8_t> tile(kTileBytes, 0xcd);
    generate_tile(delta, origin, offset, group, tile.data(), kTilePitch);
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    return std::fwrite(tile.data(), 1, tile.size(), stdout) == tile.size() ? 0 : 1;
}

int mode_bench(int argc, char** argv) {
    const double seconds = argc >= 3 ? std::atof(argv[2]) : 2.0;
    // Fixed windows for reproducible checksums: fine around (95576,97323,82698)
    // (pose A), far around the negative-coordinate camera (-10000,20000,-30000).
    const NodeKey origins[kLevelCount] = {window_origin(kFineDelta, 95576.0, 97323.0, 82698.0),
                                          window_origin(kFarDelta, -10000.0, 20000.0, -30000.0)};
    std::printf("{\"bricks_texels\":%d,\"nodes_per_brick\":%d,\"levels\":{", kBrickTexels,
                kBrickTexels * kBrickTexels * kLanes);
    std::vector<std::uint8_t> brick(kBrickBytes), tile(kTileBytes);
    for (int level = 0; level < kLevelCount; ++level) {
        const double delta = kLevelDelta[level];
        using clock = std::chrono::steady_clock;
        // Warm up one brick, then time whole bricks until the budget is spent.
        generate_brick(delta, origins[level], kNoOffset, 0, 0, 0, brick.data(), kBrickPitch);
        std::uint64_t checksum = 0;
        int bricks = 0;
        const auto start = clock::now();
        double elapsed = 0.0;
        while (elapsed < seconds) {
            const int index = bricks % (16 * kGroupCount);
            generate_brick(delta, origins[level], kNoOffset, index % 4, (index / 4) % 4, index / 16, brick.data(),
                           kBrickPitch);
            checksum ^= fnv1a(brick.data(), brick.size());
            ++bricks;
            elapsed = std::chrono::duration<double>(clock::now() - start).count();
        }
        const double nodes = double(bricks) * kBrickTexels * kBrickTexels * kLanes;
        // One full tile (65,536 nodes) for the identity checksum and tile time.
        const auto tile_start = clock::now();
        generate_tile(delta, origins[level], kNoOffset, 0, tile.data(), kTilePitch);
        const double tile_seconds = std::chrono::duration<double>(clock::now() - tile_start).count();
        std::printf("%s\"%s\":{\"delta\":%g,\"origin\":[%lld,%lld,%lld],\"bricks\":%d,\"seconds\":%.6f,"
                    "\"nodes_per_second\":%.1f,\"brick_checksum_xor\":\"%016llx\",\"tile_seconds\":%.6f,"
                    "\"tile_nodes_per_second\":%.1f,\"tile_group0_fnv1a\":\"%016llx\"}",
                    level ? "," : "", level ? "far" : "fine", delta, (long long)origins[level].x,
                    (long long)origins[level].y, (long long)origins[level].z, bricks, elapsed, nodes / elapsed,
                    (unsigned long long)checksum, tile_seconds, double(kWindowNodes * kWindowNodes * kLanes) / tile_seconds,
                    (unsigned long long)fnv1a(tile.data(), tile.size()));
    }
    std::printf("}}\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: fog_density_generator_host constants|field|words|half|unhalf|lod|layout|border|address|tile|bench ...\n");
        return 2;
    }
    const std::string mode = argv[1];
    if (mode == "constants") return mode_constants();
    if (mode == "field") return mode_field();
    if (mode == "words") return mode_words(argc, argv);
    if (mode == "half") return mode_half();
    if (mode == "unhalf") return mode_unhalf();
    if (mode == "lod") return mode_lod();
    if (mode == "layout") return mode_layout();
    if (mode == "border") return mode_border(argc, argv);
    if (mode == "address") return mode_address(argc, argv);
    if (mode == "tile") return mode_tile(argc, argv);
    if (mode == "bench") return mode_bench(argc, argv);
    std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
}
