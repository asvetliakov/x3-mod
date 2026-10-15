// Amendment 18 (docking): the --debug dock rows (the inline dock helpers are ship_dock_glue.h).
#include "ship_dock_glue.h"
namespace x3m_ship_steering_rt {
// Amendment 18: the ship_avoid_dock rows (--debug), drained at the frame boundary.
void dock_rows(unsigned long long frame) noexcept {
    static const char* const kZone[4] = {"out", "near", "in", "inner"};
    for (std::uint32_t w = dock_row_write_.load(std::memory_order_acquire); dock_row_read_ != w; ++dock_row_read_) {
        const DockRow& r = dock_rows_[dock_row_read_ & 31];
        x3m::log("ship_avoid_dock frame=%llu t_ms=%lu ship=0x%08lx cont=0x%08lx class=%u order=0x%x parts=%u n_parts=%u path_m=%.0f ray_m=%.0f "
                 "margin_m=%.0f reach_m=%.0f e=%s zone=%s d_c_m=%.0f r_c_m=%.0f v_ms=%.1f kept=%u detour=%u",
                 frame, static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.ship), static_cast<unsigned long>(r.cont), unsigned(r.cls),
                 unsigned(r.order), unsigned(r.parts_ok), r.n_parts, double(r.path_m), double(r.ray_m), double(r.margin_m), double(r.reach_m),
                 av::dock_e_name(av::DockE(r.verdict)), kZone[r.zone & 3], double(r.d_c_m), double(r.r_c_m), double(r.v_ms), unsigned(r.kept),
                 unsigned(r.detour));
    }
}
} // namespace x3m_ship_steering_rt
