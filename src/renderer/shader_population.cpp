#include "shader_population.h"
#include "motion_output_profiles.h"
#include "depth_prepass_profiles.h"
#include "scene_boundary.h"
// Admission tables that live with the proxy arm that owns them. They are
// dependency-free constexpr headers (no D3D, no Win32); including them here
// keeps every hash literal in its owning file and the consulted set in one.
#include "../proxy/fade_route_core.h"
#include "../proxy/linear_cutout.h"
#include "../proxy/screen_emission_admission.h"

namespace x3m::renderer {
namespace {

// Tables owned by headers, enumerated in place.
constexpr SceneSignatures scene_signatures{};
constexpr std::size_t scene_bloom_pairs = sizeof scene_signatures.bloom / sizeof scene_signatures.bloom[0];

constexpr ShaderTable header_tables[] = {
    {"motion_output_pairs", motion_output_profile_count * 2,
     [](std::size_t i) noexcept -> std::uint64_t {
         const auto& row = motion_output_profiles[i / 2];
         return (i & 1u) ? row.pixel_fingerprint : row.vertex_fingerprint;
     }},
    {"depth_prepass_vertex", sizeof depth_prepass_profiles / sizeof depth_prepass_profiles[0],
     [](std::size_t i) noexcept -> std::uint64_t { return depth_prepass_profiles[i].vertex_fingerprint; }},
    {"scene_boundary_bloom_pairs", scene_bloom_pairs * 2,
     [](std::size_t i) noexcept -> std::uint64_t {
         const auto& pair = scene_signatures.bloom[i / 2];
         return (i & 1u) ? pair.ps : pair.vs;
     }},
    {"fade_route_vertex", x3m::fade_route::vertex_program_count,
     [](std::size_t i) noexcept -> std::uint64_t { return x3m::fade_route::vertex_programs[i].hash; }},
    {"cutout_pairs", x3m::cutout::pair_hash_count,
     [](std::size_t i) noexcept -> std::uint64_t { return x3m::cutout::pair_hashes[i]; }},
    {"screen_emission_pairs", x3m::screen_emission::pair_count * 2,
     [](std::size_t i) noexcept -> std::uint64_t {
         const auto& pair = x3m::screen_emission::pairs[i / 2];
         return (i & 1u) ? pair.pixel : pair.vertex;
     }},
};

using Provider = const ShaderTable* (*)(std::size_t&) noexcept;
constexpr Provider providers[] = {
    &linear_material_shader_tables, &linear_emission_shader_tables,
    &linear_emission_sm1_shader_tables, &rigid_position_shader_tables,
    &material_radiance_shader_tables,
};

// Flat view over the header tables followed by each provider's tables. The
// order is fixed by this file, so a log line's table count is stable.
const ShaderTable* locate(std::size_t index) noexcept {
    constexpr std::size_t header_count = sizeof header_tables / sizeof header_tables[0];
    if (index < header_count) return &header_tables[index];
    std::size_t consumed = header_count;
    for (const auto provider : providers) {
        std::size_t count = 0;
        const ShaderTable* tables = provider(count);
        if (!tables) continue;
        if (index < consumed + count) return &tables[index - consumed];
        consumed += count;
    }
    return nullptr;
}

} // namespace

std::size_t shader_table_count() noexcept {
    std::size_t total = sizeof header_tables / sizeof header_tables[0];
    for (const auto provider : providers) {
        std::size_t count = 0;
        if (provider(count)) total += count;
    }
    return total;
}

const ShaderTable& shader_table(std::size_t index) noexcept {
    static constexpr ShaderTable empty{"", 0, [](std::size_t) noexcept -> std::uint64_t { return 0; }};
    const ShaderTable* table = locate(index);
    return table ? *table : empty;
}

std::size_t shader_table_entry_count() noexcept {
    std::size_t total = 0;
    const std::size_t tables = shader_table_count();
    for (std::size_t t = 0; t < tables; ++t) total += shader_table(t).count;
    return total;
}

bool shader_hash_known(std::uint64_t hash) noexcept {
    if (!hash) return false;
    const std::size_t tables = shader_table_count();
    for (std::size_t t = 0; t < tables; ++t) {
        const ShaderTable& table = shader_table(t);
        for (std::size_t i = 0; i < table.count; ++i)
            if (table.at(i) == hash) return true;
    }
    return false;
}

bool ShaderPopulation::observe(std::uint64_t hash, bool vertex, std::uint32_t version,
                               std::uint32_t bytes) noexcept {
    if (!hash) return false;
    if (shader_hash_known(hash)) { ++known_; changed_ = true; return false; }
    for (std::size_t i = 0; i < used_; ++i)
        if (entries_[i].hash == hash) return false; // Already recorded this session.
    if (used_ == capacity) { ++overflow_; changed_ = true; return false; }
    entries_[used_++] = Entry{hash, version, bytes, vertex};
    ++unknown_;
    changed_ = true;
    return true;
}

bool ShaderPopulation::take(Entry& out) noexcept {
    if (pending_ >= used_) return false;
    out = entries_[pending_++];
    return true;
}

} // namespace x3m::renderer
