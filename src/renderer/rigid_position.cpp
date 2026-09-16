#include "rigid_position.h"
#include "shader_population.h"

namespace x3m::renderer {
namespace {
constexpr RigidPositionProfile profiles[] = {
#include "rigid_position_profiles_inc.h"
};
struct PositionException {
    std::uint64_t hash;
    std::uint32_t word_count;
    VertexPositionPath path;
};
constexpr PositionException exceptions[] = {
#include "position_path_profiles_inc.h"
};
constexpr PixelCoverageProfile pixel_profiles[] = {
#include "pixel_coverage_profiles_inc.h"
};

template<class T, std::size_t N>
constexpr std::size_t maximum_words(const T (&table)[N]) noexcept {
    std::size_t result = 0;
    for (const auto& p : table) if (p.word_count > result) result = p.word_count;
    return result;
}
template<class T, std::size_t N>
constexpr bool strictly_sorted(const T (&table)[N]) noexcept {
    for (std::size_t i = 1; i < N; ++i) if (table[i-1].hash >= table[i].hash) return false;
    return true;
}
static_assert(strictly_sorted(profiles) && strictly_sorted(exceptions) && strictly_sorted(pixel_profiles));
static_assert(maximum_words(profiles) <= kMaxReviewedVertexShaderWords);
static_assert(maximum_words(exceptions) <= kMaxReviewedVertexShaderWords);
static_assert(maximum_words(pixel_profiles) <= kMaxReviewedPixelShaderWords);

template<class T, std::size_t N>
bool length_known(const T (&table)[N], std::size_t count) noexcept {
    for (const auto& p : table) if (p.word_count == count) return true;
    return false;
}
std::uint64_t fingerprint(const std::uint32_t* words, std::size_t word_count) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < word_count; ++i) {
        // Explicit little-endian byte order agrees with the captured token stream
        // and keeps host-side verification independent of host representation.
        for (unsigned byte = 0; byte < 4; ++byte) {
            hash ^= (words[i] >> (byte * 8)) & 0xffu;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}
template<class T, std::size_t N>
const T* lookup(const T (&table)[N], std::uint64_t hash, std::size_t count) noexcept {
    std::size_t first = 0, last = N;
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        if (table[middle].hash < hash) first = middle + 1;
        else last = middle;
    }
    return first < N && table[first].hash == hash && table[first].word_count == count
        ? &table[first] : nullptr;
}
}
const RigidPositionProfile* find_rigid_position(const std::uint32_t* words,
                                              std::size_t word_count) noexcept {
    if (!words || word_count > kMaxReviewedVertexShaderWords || !length_known(profiles, word_count)) return nullptr;
    return lookup(profiles, fingerprint(words, word_count), word_count);
}
VertexPositionPath classify_vertex_position(const std::uint32_t* words,
                                            std::size_t word_count) noexcept {
    if (!words || word_count > kMaxReviewedVertexShaderWords ||
        (!length_known(profiles, word_count) && !length_known(exceptions, word_count))) return VertexPositionPath::Unknown;
    const auto hash = fingerprint(words, word_count);
    if (lookup(profiles, hash, word_count)) return VertexPositionPath::HomogeneousRowDots;
    if (const auto* p = lookup(exceptions, hash, word_count)) return p->path;
    return VertexPositionPath::Unknown;
}
const PixelCoverageProfile* find_pixel_coverage(const std::uint32_t* words,
                                              std::size_t word_count) noexcept {
    if (!words || word_count > kMaxReviewedPixelShaderWords || !length_known(pixel_profiles, word_count)) return nullptr;
    return lookup(pixel_profiles, fingerprint(words, word_count), word_count);
}
// Shader-population provider (src/renderer/shader_population.h): the three
// profile tables this unit owns, enumerated in place.
const ShaderTable* rigid_position_shader_tables(std::size_t& count) noexcept {
    static constexpr ShaderTable tables[] = {
        {"rigid_position_vertex", sizeof profiles / sizeof profiles[0],
         [](std::size_t i) noexcept -> std::uint64_t { return profiles[i].hash; }},
        {"position_path_vertex", sizeof exceptions / sizeof exceptions[0],
         [](std::size_t i) noexcept -> std::uint64_t { return exceptions[i].hash; }},
        {"pixel_coverage_pixel", sizeof pixel_profiles / sizeof pixel_profiles[0],
         [](std::size_t i) noexcept -> std::uint64_t { return pixel_profiles[i].hash; }},
    };
    count = sizeof tables / sizeof tables[0];
    return tables;
}
} // namespace x3m::renderer
