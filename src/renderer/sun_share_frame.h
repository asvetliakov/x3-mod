#pragma once
#include <cstdint>
namespace x3m::renderer {
// Draw/frame bookkeeping only; no API calls, allocation, locks or arithmetic.
// Pixel eligibility additionally requires depth in [0,1], share in [0,1] and
// zero same-frame composition coverage. Counts are draws, never pixel counts.
struct SunShareFrame {
    std::uint32_t receivers=0, covered=0, untracked=0;
    bool failed=false, published=false, available=false, coverage_required=false;
    void draw(bool success, bool receiver, bool conservative_coverage, bool depth_updated=false) noexcept {
        if(!success)return;
        published=false; available=false;
        if(receiver)++receivers;
        else if(conservative_coverage){++covered;coverage_required=true;}
        else if(!depth_updated&&receivers){++untracked;failed=true;}
    }
    bool publish(bool ready, bool owner_valid, bool coverage_valid) noexcept {
        published=true;
        available=ready&&owner_valid&&receivers&&!failed&&(!coverage_required||coverage_valid);
        return available;
    }
};
}
