#pragma once
#include "sector_background.h"
#include "../renderer/fog_field_assets.h"
#include "../fog/fog_density_generator.h"
#include <cstring>
#include <cmath>
namespace x3m {
// Value-only frame authority: identity tokens are never dereferenced later.
// Profile IDs match the embedded packets; preserve the original identities.
static_assert(unsigned(renderer::fog_field::Profile::Bluewell) == 1 && unsigned(renderer::fog_field::Profile::Foggreenoutlands) == 2, "embedded fog profile IDs");
struct FogSectorFrame {
    std::uint64_t frame = ~std::uint64_t(0), generation = 0, field_generation = 0;
    std::uint32_t sector = 0, table = 0, record = 0;
    std::int32_t index = -1;
    unsigned profile = 0, recipe = renderer::fog_field::qualified_recipe_id;
    sector_background::Status status = sector_background::Status::ReadFailure;
    float density_scale = 0.f;
    bool enabled = false;
    const char* reason = "sample_missing";
    bool same_key(const FogSectorFrame& b) const noexcept {
        return generation == b.generation && field_generation == b.field_generation && sector == b.sector && table == b.table && record == b.record &&
            index == b.index && profile == b.profile && recipe == b.recipe && status == b.status;
    }
    bool current(std::uint64_t f) const noexcept { return frame == f && enabled && profile != 0; }
};
// Stored-density field placement (fog-density-runtime-integration.md §1, amended): a per-sector
// translation of the stationary field by whole far nodes, so node keys stay integers and the
// accuracy statistics carry over. Keyed by session-stable identity only: the sector's background
// record index, the family profile and the recipe. The heap tokens (sector, table, record) still
// detect a change for the card/TAA rewarm through same_key, but never enter this key: a sector's
// clouds are the same on every visit, reload and session, and a reallocation neither re-keys nor
// refills. Sectors that share one background record share a placement.
struct FogSectorPlacement { std::uint64_t key = 0; double offset[3]{}; };
inline std::uint64_t fog_sector_mix(std::uint64_t x) noexcept { // splitmix64 finalizer
    x += 0x9e3779b97f4a7c15ull; x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull; x = (x ^ (x >> 27)) * 0x94d049bb133111ebull; return x ^ (x >> 31);
}
inline FogSectorPlacement fog_sector_placement(const FogSectorFrame& f) noexcept {
    FogSectorPlacement out;
    out.key = fog_sector_mix(fog_sector_mix(fog_sector_mix(std::uint64_t(std::uint32_t(f.index))) ^ f.profile) ^ f.recipe);
    std::uint64_t h = out.key;
    for (unsigned axis = 0; axis < 3; ++axis) {
        h = fog_sector_mix(h + axis);
        out.offset[axis] = fog::kFarDelta * double(int(h % 4096u) - 2048);
    }
    return out;
}
// `families` (fog-family-data.md): the loaded <game>/x3m/fog-families.bin rows, scanned only
// after the 14 compiled names miss, so a compiled name always wins; disabled rows never match.
inline FogSectorFrame fog_sector_frame(const sector_background::Sample& s, std::uint64_t frame,
                                     std::uint64_t generation, float strength, bool enabled,
                                     const renderer::fog_field::FamilyTable* families = nullptr) noexcept {
    FogSectorFrame out;
    out.frame = frame; out.generation = generation; out.sector = s.sector; out.table = s.table;
    out.record = s.record; out.index = s.index; out.status = s.status;
    out.density_scale = std::isfinite(strength) && strength >= 0.f && strength <= .1f ? strength / .02f : 0.f;
    if (s.status != sector_background::Status::Ready) out.reason = sector_background::name(s.status);
    else if (!s.row_valid) out.reason = "row_invalid";
    else if (!s.name_valid) out.reason = "name_invalid";
    else if (s.camera_check == sector_background::Check::Mismatch || sector_background::anchor_refused(s)) out.reason = "sample_mismatch";
    else if (s.dust == 0) out.reason = "clear";
    else if (s.dust < 0) out.reason = "dust_invalid";
    else {
        out.reason = "family_unsupported";
        for (const auto& family : renderer::fog_field::family_profiles) {
            if (!std::strcmp(s.family, family.family)) {
                out.profile = static_cast<unsigned>(family.profile);
                out.reason = family.family;
                break;
            }
        }
        if (!out.profile && families) {
            if (const auto* row = families->find(s.family)) { out.profile = row->profile; out.reason = row->name; }
        }
    }
    out.enabled = enabled && out.density_scale > 0.f && out.profile != 0;
    return out;
}
} // namespace x3m
