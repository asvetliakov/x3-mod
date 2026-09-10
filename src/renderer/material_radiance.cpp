#include "material_radiance.h"

namespace x3m::renderer {
namespace {
constexpr std::size_t max_words = 1024 * 1024;
constexpr std::uint32_t ps_3_0 = 0xffff0300;
constexpr std::uint32_t mov = 0x02000001, max = 0x0300000b;
constexpr std::uint32_t def = 0x05000051, end = 0x0000ffff;
constexpr std::uint32_t saturate = 0x00100000;
constexpr RadianceProfile profiles[] = {
#include "material_radiance_profiles_inc.h"
};

bool bounded(const std::uint32_t* source, std::size_t count) noexcept {
    return source && count >= 2 && count <= max_words;
}

// SM3 lengths include operand DWORDs, not the instruction token. Comments have
// their own length field and may contain arbitrary data resembling instructions.
// This framing check never treats a constant/comment word as a patch site.
bool validate(const RadianceProfile& profile, const std::uint32_t* source,
              std::size_t count, std::uint32_t& zero_source) noexcept {
    if (source[0] != ps_3_0 || !profile.site_count || profile.site_count > 2 ||
        profile.zero_component > 3) return false;
    for (unsigned i = 0; i < profile.site_count; ++i) {
        const auto& site = profile.sites[i];
        if (!site.offset || site.offset >= count ||
            (i && site.offset <= profile.sites[i - 1].offset)) return false;
        // Temporary r0..r31, RGB only, SAT required; optional PP preserved.
        // No destination shift, centroid, predicate or source modification.
        const auto destination = site.destination & ~std::uint32_t(0x0020001f);
        if (destination != 0x80170000 || site.source != 0x90e40000) return false;
    }
    bool zero_seen = false;
    unsigned sites_seen = 0;
    for (std::size_t offset = 1; offset < count;) {
        const auto token = source[offset];
        const auto opcode = token & 0xffff;
        if (opcode == 0xffff)
            return token == end && offset == count - 1 && zero_seen &&
                sites_seen == profile.site_count;
        const std::size_t operands = opcode == 0xfffe
            ? ((token >> 16) & 0x7fff) : ((token >> 24) & 0xf);
        if (operands > count - offset - 1) return false;
        if (offset == profile.zero_def_offset) {
            if (token != def || operands != 5) return false;
            const auto destination = source[offset + 1];
            // Shader-local DEF c0..c223, all four components; never reserve an
            // external application constant or modify its value.
            if ((destination & ~std::uint32_t(0x7ff)) != 0xa00f0000 ||
                (destination & 0x7ff) >= 224 ||
                source[offset + 2 + profile.zero_component] != 0) return false;
            zero_source = 0xa0000000 | (destination & 0x7ff) |
                ((profile.zero_component * 0x55) << 16);
            zero_seen = true;
        }
        bool named_site = false;
        for (unsigned i = 0; i < profile.site_count; ++i) {
            const auto& site = profile.sites[i];
            if (offset != site.offset) continue;
            if (token != mov || operands != 2 ||
                source[offset + 1] != site.destination ||
                source[offset + 2] != site.source || !zero_seen) return false;
            ++sites_seen;
            named_site = true;
        }
        // A profile may not silently omit another identical RGB-input clamp
        // (for example, the other arm of the material's boolean branch).
        if (token == mov && operands == 2 && source[offset + 2] == 0x90e40000 &&
            (source[offset + 1] & ~std::uint32_t(0x0020001f)) == 0x80170000 &&
            !named_site) return false;
        offset += operands + 1;
    }
    return false; // Missing terminal END.
}
} // namespace

std::uint64_t shader_fingerprint(const std::uint32_t* source,
                                 std::size_t count) noexcept {
    if (!bounded(source, count)) return 0;
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < count; ++i)
        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash ^= (source[i] >> shift) & 0xff;
            hash *= 1099511628211ull;
        }
    return hash;
}

const RadianceProfile* material_radiance_profiles(std::size_t* count) noexcept {
    if (count) *count = sizeof(profiles) / sizeof(profiles[0]);
    return profiles;
}

RadianceResult apply_radiance_profile(const RadianceProfile& profile,
    const std::uint32_t* source, std::size_t count,
    std::vector<std::uint32_t>& output) noexcept {
    if (!bounded(source, count)) return RadianceResult::InvalidInput;
    if (profile.word_count != count || profile.fnv != shader_fingerprint(source, count))
        return RadianceResult::ProfileMismatch;
    std::uint32_t zero_source = 0;
    if (!validate(profile, source, count, zero_source)) return RadianceResult::InvalidProfile;
    try {
        std::vector<std::uint32_t> variant;
        variant.reserve(count + profile.site_count);
        std::size_t begin = 0;
        for (unsigned i = 0; i < profile.site_count; ++i) {
            const auto& site = profile.sites[i];
            variant.insert(variant.end(), source + begin, source + site.offset);
            variant.push_back(max);
            variant.push_back(site.destination & ~saturate);
            variant.push_back(site.source);
            variant.push_back(zero_source);
            begin = site.offset + 3;
        }
        variant.insert(variant.end(), source + begin, source + count);
        output.swap(variant);
        return RadianceResult::Applied;
    } catch (...) { return RadianceResult::AllocationFailure; }
}

RadianceResult material_radiance_variant(const std::uint32_t* source,
    std::size_t count, std::vector<std::uint32_t>& output) noexcept {
    if (!bounded(source, count)) return RadianceResult::InvalidInput;
    const auto hash = shader_fingerprint(source, count);
    for (const auto& profile : profiles)
        if (profile.word_count == count && profile.fnv == hash)
            return apply_radiance_profile(profile, source, count, output);
    return RadianceResult::UnsupportedShader;
}
} // namespace x3m::renderer
