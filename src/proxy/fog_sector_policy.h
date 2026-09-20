#pragma once
#include "sector_background.h"
#include "../renderer/fog_field_assets.h"
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
    bool enabled = false, forced = false;
    const char* reason = "sample_missing";
    bool same_key(const FogSectorFrame& b) const noexcept {
        return generation == b.generation && field_generation == b.field_generation && sector == b.sector && table == b.table && record == b.record &&
            index == b.index && profile == b.profile && recipe == b.recipe && status == b.status && forced == b.forced;
    }
    bool current(std::uint64_t f) const noexcept { return frame == f && enabled && profile != 0; }
};
inline FogSectorFrame fog_sector_frame(const sector_background::Sample& s, std::uint64_t frame,
                                     std::uint64_t generation, float strength, bool enabled, bool everywhere) noexcept {
    FogSectorFrame out;
    out.frame = frame; out.generation = generation; out.sector = s.sector; out.table = s.table;
    out.record = s.record; out.index = s.index; out.status = s.status;
    out.density_scale = std::isfinite(strength) && strength >= 0.f && strength <= .1f ? strength / .02f : 0.f;
    if (s.status != sector_background::Status::Ready) out.reason = sector_background::name(s.status);
    else if (!s.row_valid) out.reason = "row_invalid";
    else if (!s.name_valid) out.reason = "name_invalid";
    else if (s.camera_check == sector_background::Check::Mismatch || s.anchor_check == sector_background::Check::Mismatch) out.reason = "sample_mismatch";
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
    }
    // Explicit debug forcing still needs a valid view at card/pass admission.
    if (!out.profile && everywhere) { out.profile = 1; out.forced = true; out.reason = "forced_profile_bluewell"; }
    out.enabled = enabled && out.density_scale > 0.f && out.profile != 0;
    return out;
}
} // namespace x3m
