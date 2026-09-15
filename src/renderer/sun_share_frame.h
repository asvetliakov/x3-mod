#pragma once
#include <cstdint>
namespace x3m::renderer {
// Why a successful color writer after the first receiver stayed untracked
// (diagnostics only; the coverage veto itself never consults the reason).
// Values are the stable ids the sun_shadow_lane_refusals line reports.
enum class SunUntrackedReason : std::uint8_t {
    Unknown = 0,      // no gate recorded (lane bookkeeping without a route decision)
    Feature = 1,      // gate 1: no route target, recording a state block or MSAA main target
    Scene = 2,        // gate 2: latched main color/depth pair not bound
    Unregistered = 3, // gate 3: program outside the registry or VS without a profile row
    Pair = 4,         // gate 3: registered row without a reviewed pair (or xt pair not ready)
    NoZWrite = 5,     // gate 4: z test or z write disabled (never an untracked writer: counted non_writers)
    Blended = 6,      // gate 4: alpha blending enabled (blends every bound target, so the lane values would be blended too)
    State = 7,        // gate 4: sRGB write, or a cutout pair outside its exact cutout state
    Rows = 8,         // gate 4: clip rows unknown or light loop unbounded
    Geometry = 9,     // gate 4: user memory, instancing, missing stream/declaration/indices
    NoDepth = 10,     // routed pair whose profile writes no depth
    FadeArm = 11,     // routed through the fade-band arm (RT2 masked)
    ApplyFailed = 12, // gates passed; variant/target apply failed and rolled back
    Scope = 13,       // gate 5: object/camera scope unverified (still routes; recorded for completeness)
    History = 14,     // gate 6: no previous rows (still routes; recorded for completeness)
    ReadFailed = 15   // gate 4: a state getter failed, so the draw-state verdict is unreadable
};
constexpr unsigned sun_untracked_reason_count = 16;
constexpr const char* sun_untracked_reason_name(unsigned reason) noexcept {
    switch (reason) {
    case 1: return "feature"; case 2: return "scene"; case 3: return "unregistered"; case 4: return "pair";
    case 5: return "no_zwrite"; case 6: return "blended"; case 7: return "state"; case 8: return "rows";
    case 9: return "geometry"; case 10: return "no_depth"; case 11: return "fade_arm"; case 12: return "apply_failed";
    case 13: return "scope"; case 14: return "history"; case 15: return "read_failed";
    default: return "unknown";
    }
}
// Draw/frame bookkeeping only; no API calls, allocation, locks or arithmetic.
// Pixel eligibility additionally requires depth in [0,1], share in [0,1] and
// zero same-frame composition coverage. Counts are draws, never pixel counts.
struct SunShareFrame {
    std::uint32_t receivers=0, covered=0, untracked=0;
    std::uint32_t non_writers=0; // color writers after the first receiver that wrote no depth (never a veto)
    std::uint32_t reasons[sun_untracked_reason_count]{}; // untracked writers per SunUntrackedReason
    bool failed=false, published=false, available=false, coverage_required=false;
    // Returns true when the draw was counted as an untracked writer. Only a
    // draw that actually wrote depth (z test and z write both on and known)
    // can leave the lane's depth stale; a color-only draw over a receiver
    // changes what is lit, never which depth the lane tracks. Unknown z state
    // is a writer (fail closed).
    bool draw(bool success, bool receiver, bool conservative_coverage, bool depth_updated=false,
              SunUntrackedReason reason=SunUntrackedReason::Unknown, bool depth_writer=true) noexcept {
        if(!success)return false;
        published=false; available=false;
        if(receiver)++receivers;
        else if(conservative_coverage){++covered;coverage_required=true;}
        else if(!depth_updated&&receivers){
            if(!depth_writer){++non_writers;return false;}
            ++untracked;failed=true;
            const unsigned index=unsigned(reason);
            ++reasons[index<sun_untracked_reason_count?index:0u];
            return true;
        }
        return false;
    }
    bool publish(bool ready, bool owner_valid, bool coverage_valid) noexcept {
        published=true;
        available=ready&&owner_valid&&receivers&&!failed&&(!coverage_required||coverage_valid);
        return available;
    }
};
}
