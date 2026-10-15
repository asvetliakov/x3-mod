// Ship steering: the frame boundary (the join predictor, the controller's frame, the --debug rows, the trace rows and the 1 Hz
// summary) and the trace slots' claim.
#include "ship_dock_glue.h"
namespace {
std::uint32_t join_eval_max_ticks_ = 0, join_evals_ = 0, join_predictions_ = 0;
// The summary's gates text (frame boundary): its worst case, every conversion at its widest, as summary_bound.py computes it from
// this file (the host test keeps the constant equal to the computed figure); the buffer must hold it. It held 4,096 B up to B.3,
// below the 4,123-character worst case: the tail truncated.
constexpr std::size_t kSummaryGatesWorstCase = 3658;
constexpr std::size_t kSummaryGatesBytes = 8192;
static_assert(kSummaryGatesWorstCase < kSummaryGatesBytes, "the summary's gates text fits its buffer (summary_bound.py)");
std::uint32_t rep_gate_[29] = {};
std::uint32_t rep_trace_[2] = {};
// Reported totals (frame boundary only).
std::uint32_t rep_calls_ = 0, rep_handled_ = 0, rep_reasons_[core::kReasonCount] = {}, rep_clamp_calls_ = 0,
              rep_clamp_applied_ = 0, rep_clamp_stale_ = 0, rep_leaf_[3] = {}, rep_redirected_ = 0;
std::uint32_t rep_av_[13] = {}, rep_av_why_[unsigned(av::Unresolved::count)] = {}, rep_av_ep_[4] = {};
unsigned long long last_detour_tick_ = 0;
unsigned detour_rows_this_second_ = 0;
unsigned long long last_summary_tick_ = 0, last_sample_tick_ = 0;
std::uint32_t last_logged_seq_ = 0; // the sample row is logged once per written sample
unsigned stuck_reported_ = 0;
inline double ticks_us(std::uint32_t ticks) noexcept { // via int32 (cvtsi2sd): a 64-bit integer -> double conversion is x87 fild on i686
    return qpc_frequency_ ? double(std::int32_t(ticks > 0x7fffffffu ? 0x7fffffffu : ticks)) * 1e6 /
                                double(std::int32_t(qpc_frequency_ > 0x7fffffffull ? 0x7fffffffu : std::uint32_t(qpc_frequency_)))
                          : 0.0;
}
} // namespace
namespace x3m::ship_steering {
void frame_boundary(std::uint64_t frame) {
    if (!installed_.load(std::memory_order_relaxed)) return;
    // Always tier: the stuck witnesses (first 8 per session).
    const std::uint32_t stuck = stuck_count_.load(std::memory_order_acquire);
    while (stuck_reported_ < stuck && stuck_reported_ < 8) {
        const Stuck& s = stuck_[stuck_reported_++];
        log("ship_steering_stuck frame=%llu obj=0x%08lx order=0x%lx d_m=%.1f best_d_m=%.1f no_progress_s=%.0f",
            static_cast<unsigned long long>(frame), static_cast<unsigned long>(s.obj), static_cast<unsigned long>(s.order),
            double(s.d), double(s.best_d), double(core::kStuckMs / 1000.0f));
    }
    // Amendment 15 (section 15.3): the join predictor, at most one capital per frame (round robin over the registry), each at 1 Hz
    // of game time; never in the steering handler. A slot whose ship left join mode (or whose state was reused) leaves the registry.
    if (gates_capital_on_ && join_reg_.count) {
        const std::uint32_t t_now = game_time_ms();
        for (unsigned i = 0; i < join_reg_.count;) { // the sweep: a completed, dead, redirected or reused slot leaves (review blocker)
            GateState& g = gate_[join_reg_.slots[i]];
            const bool same_ship = g.obj && g.join_registered && table_.entries[join_reg_.slots[i]].obj == g.obj;
            if (!core::join_registry_keep(g.join, same_ship, t_now)) {
                g.join_registered = 0;
                join_reg_.remove_at(i);
                continue;
            }
            ++i;
        }
        for (unsigned n = 0; n < join_reg_.count; ++n) {
            const unsigned idx = (join_reg_.next + n) % join_reg_.count;
            GateState& g = gate_[join_reg_.slots[idx]];
            const std::uint64_t t0 = timing_ ? qpc() : 0;
            bool fell = false, slowed = false;
            unsigned predictions = 0;
            if (!core::join_evaluate(g.join, t_now, av::fresh_window_ms(av::kSteeredFreshMs, frame_step_ms()), &fell, &slowed, &predictions)) continue;
            join_reg_.next = idx + 1;
            ++join_evals_;
            join_predictions_ += predictions;
            if (timing_) {
                const std::uint64_t dt = qpc() - t0;
                const std::uint32_t ticks = dt > 0xffffffffull ? 0xffffffffu : std::uint32_t(dt);
                if (ticks > join_eval_max_ticks_) join_eval_max_ticks_ = ticks;
            }
            if (slowed) gate_capital_slowed_.add();
            if (fell) gate_capital_fallback_pred_.add(); // latched: the registry's sweep drops it
            if ((fell || slowed) && trace_on_ && trace_slot_of(g.obj)) {
                join_row(g, g.obj, t_now, false, -1.0f, -1.0f, -1.0f, 0, 0);
                g.join_row_ms = t_now;
                g.join_logged_mode = std::uint8_t(g.join.mode);
            }
            break; // one capital per frame
        }
    }
    if (traffic_live_.load(std::memory_order_relaxed)) ctl_frame(); // the gate traffic controller's decisions (D.1), never in a handler
    if (!perf_ && !debug_) return;
    if (debug_ && traffic_on_) ctl_rows(frame);
    if (debug_ && dock_on_) dock_rows(frame);
    const unsigned long long now = GetTickCount64();
    if (debug_) {
        // One sampled decision per second: request it, log the previous one when complete.
        const std::uint32_t seq = sample_seq_.load(std::memory_order_acquire);
        if (seq && !(seq & 1u) && seq != last_logged_seq_ && now - last_sample_tick_ >= 1000) {
            Sample S = sample_;
            if (sample_seq_.load(std::memory_order_acquire) == seq) {
                last_sample_tick_ = now;
                last_logged_seq_ = seq;
                log("ship_steering_decision frame=%llu obj=0x%08lx order=0x%lx class=%s leaf=%lu held=%u theta_deg=%.2f n_yaw=%.3f "
                    "n_pitch=%.3f omega_dps=%.2f e=%ld,%ld,%ld w=%ld,%ld,%ld W_dps=%.1f,%.1f,%.1f k=%.3f d_m=%.1f d_arr_m=%.1f "
                    "v_ms=%.1f v_cmd_ms=%.1f v_corner_ms=%.1f v_ref_ms=%.1f P_m=%.1f,%.1f,%.1f x_m=%.1f,%.1f,%.1f flags=0x%08lx "
                    "a_dps2=%.2f,%.2f,%.2f f2b0=0x%08lx rot_pct=%ld omega_cmd_dps=%.2f,%.2f,%.2f lag_deg=%.1f aim_off_deg=%.1f v_slip_ms=%.1f "
                    "slip_ms=%.1f,%.1f,%.1f",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(S.obj), static_cast<unsigned long>(S.order),
                    S.code < core::kClassCount ? core::kClassNames[S.code] : "?", static_cast<unsigned long>(S.leaf), S.held ? 1u : 0u,
                    double(S.theta_deg), double(S.n_y), double(S.n_p), double(S.omega_dps), long(S.e[0]), long(S.e[1]), long(S.e[2]),
                    long(S.w[0]), long(S.w[1]), long(S.w[2]), double(S.W_dps[0]), double(S.W_dps[1]), double(S.W_dps[2]), double(S.k),
                    double(S.d), double(S.d_arr), double(S.v), double(S.v_cmd), double(S.v_corner), double(S.v_ref), double(S.P[0]),
                    double(S.P[1]), double(S.P[2]), double(S.x[0]), double(S.x[1]), double(S.x[2]), static_cast<unsigned long>(S.flags),
                    double(S.a_dps[0]), double(S.a_dps[1]), double(S.a_dps[2]), static_cast<unsigned long>(S.rot_accel_raw), long(S.rot_pct),
                    double(S.omega_axis_dps[0]), double(S.omega_axis_dps[1]), double(S.omega_axis_dps[2]), double(S.lag_deg),
                    double(core::acos_deg(S.aim_cos)), double(S.v_slip), double(S.slip[0]), double(S.slip[1]), double(S.slip[2]));
                sample_request_.store(true, std::memory_order_relaxed);
            }
        } else if ((!seq || seq == last_logged_seq_) && now - last_sample_tick_ >= 1000) {
            last_sample_tick_ = now;
            sample_request_.store(true, std::memory_order_relaxed);
        }
    }
    if (debug_ && avoidance_on_) {
        // the census rows (once per sector per session) and the detour samples (at most 32 rows per second)
        for (std::uint32_t w = census_write_.load(std::memory_order_acquire); census_read_ != w; ++census_read_) {
            const Census& c = census_[census_read_ & 3];
            char classes[256], buckets[256];
            int n = 0;
            for (unsigned k = 0; k < 32; ++k)
                if (c.classes[k]) {
                    const int m = std::snprintf(classes + n, sizeof classes - size_t(n), "%s%u:%u", n ? "," : "", k, unsigned(c.classes[k]));
                    if (m > 0 && n + m < int(sizeof classes)) n += m;
                }
            if (!n) classes[0] = 0;
            n = 0;
            for (unsigned k = 0; k < 32; ++k) {
                const int m = std::snprintf(buckets + n, sizeof buckets - size_t(n), "%s%u", k ? "," : "", unsigned(c.buckets[k]));
                if (m > 0 && n + m < int(sizeof buckets)) n += m;
            }
            float root_r;
            std::memcpy(&root_r, &c.station_root_r, 4);
            log("ship_avoid_census frame=%llu sector=0x%08lx objects=%lu overflow=%lu classes=%s buckets=%s", static_cast<unsigned long long>(frame),
                static_cast<unsigned long>(c.sector), static_cast<unsigned long>(c.objects), static_cast<unsigned long>(c.overflow), classes, buckets);
            if (c.station)
                log("ship_avoid_station frame=%llu sector=0x%08lx obj=0x%08lx root_r_m=%.1f node_scale_0x70=0x%08lx own_r_units=%lu children=%lu "
                    "unfilled=%lu scaled=%lu extent_units=%lu model=%s parts_offset_units:radius_units:cache_units:own_units:scale:flags="
                    "%ld,%ld,%ld:%ld:%ld:%ld:%08lx:%08lx|%ld,%ld,%ld:%ld:%ld:%ld:%08lx:%08lx|%ld,%ld,%ld:%ld:%ld:%ld:%08lx:%08lx|"
                    "%ld,%ld,%ld:%ld:%ld:%ld:%08lx:%08lx",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(c.sector), static_cast<unsigned long>(c.station), double(root_r),
                    static_cast<unsigned long>(c.station_scale), static_cast<unsigned long>(c.station_own_r), static_cast<unsigned long>(c.station_children),
                    static_cast<unsigned long>(c.station_dirty), static_cast<unsigned long>(c.station_scaled), static_cast<unsigned long>(c.station_extent),
                    c.station_valid ? "parts" : "fallback",
                    long(c.part_offset[0][0]), long(c.part_offset[0][1]), long(c.part_offset[0][2]), long(c.part_r[0]), long(c.part_cache[0]),
                    long(c.part_own[0]), static_cast<unsigned long>(c.part_scale[0]), static_cast<unsigned long>(c.part_flags[0]),
                    long(c.part_offset[1][0]), long(c.part_offset[1][1]), long(c.part_offset[1][2]), long(c.part_r[1]), long(c.part_cache[1]),
                    long(c.part_own[1]), static_cast<unsigned long>(c.part_scale[1]), static_cast<unsigned long>(c.part_flags[1]),
                    long(c.part_offset[2][0]), long(c.part_offset[2][1]), long(c.part_offset[2][2]), long(c.part_r[2]), long(c.part_cache[2]),
                    long(c.part_own[2]), static_cast<unsigned long>(c.part_scale[2]), static_cast<unsigned long>(c.part_flags[2]),
                    long(c.part_offset[3][0]), long(c.part_offset[3][1]), long(c.part_offset[3][2]), long(c.part_r[3]), long(c.part_cache[3]),
                    long(c.part_own[3]), static_cast<unsigned long>(c.part_scale[3]), static_cast<unsigned long>(c.part_flags[3]));
        }
        if (now - last_detour_tick_ >= 1000) {
            last_detour_tick_ = now;
            detour_rows_this_second_ = 0;
        }
        for (std::uint32_t w = detour_write_.load(std::memory_order_acquire); detour_read_ != w; ++detour_read_) {
            if (detour_rows_this_second_ >= 32) continue;
            ++detour_rows_this_second_;
            const DetourEvent& e = detours_[detour_read_ & 15];
            log("ship_avoid_detour frame=%llu obj=0x%08lx order=0x%lx obstacle=0x%08lx kind=%lu side=%ld vertical=%lu t_cpa_s=%.2f miss_m=%.1f "
                "C_m=%.1f L_m=%.0f P_m=%.1f,%.1f,%.1f x_m=%.1f,%.1f,%.1f leader=0x%08lx obstacle_order=0x%lx",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(e.obj), static_cast<unsigned long>(e.order),
                static_cast<unsigned long>(e.obstacle), static_cast<unsigned long>(e.kind), long(std::int32_t(e.side)),
                static_cast<unsigned long>(e.vertical), double(e.t_cpa), double(e.miss), double(e.C), double(e.L), double(e.P[0]), double(e.P[1]),
                double(e.P[2]), double(e.x[0]), double(e.x[1]), double(e.x[2]), static_cast<unsigned long>(e.obstacle_leader),
                static_cast<unsigned long>(e.obstacle_order));
        }
    }
    if (debug_) { // amendment 16 (section 16.5): the width census per capital type
        // review of the arrival round item 1: the gate's collision-tree read, once per gate (state none / tree / unknown, the first guard
        // that failed, nodes visited, [gate+0x98] and whether a visited node carried it, the TGates row; verdict none = no solid (the ring
        // row suppresses on the path alone), ring = the TGates row's ring (a tree, or unknown: fail closed))
        for (std::uint32_t w = ring_read_write_.load(std::memory_order_acquire); ring_read_read_ != w; ++ring_read_read_) {
            static const char* const kState[3] = {"unknown", "none", "tree"};
            const RingReadRow& r = ring_read_rows_[ring_read_read_ & 7];
            log("ship_gate_ring_read frame=%llu t_ms=%lu gate=0x%08lx id=%lu type=%u board=%u state=%s why=%s nodes=%u scene=%lu seen=%u verdict=%s",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate),
                static_cast<unsigned long>(r.id), unsigned(r.type), unsigned(r.board), kState[r.state < 3 ? r.state : 0],
                wk::tree_why_name(wk::TreeWhy(r.why)), unsigned(r.nodes), static_cast<unsigned long>(r.scene), unsigned(r.seen),
                r.state == 1 ? "none" : "ring");
        }
        // Run175 round item 6: the visited nodes of a gate's first read (more than 3 nodes), the 60 s re-reads, the slow handler calls
        for (std::uint32_t w = ring_node_write_.load(std::memory_order_acquire); ring_node_read_ != w; ++ring_node_read_) {
            const RingNodeRow& r = ring_node_rows_[ring_node_read_ % kRingNodeRows];
            log("ship_gate_ring_node frame=%llu t_ms=%lu gate=0x%08lx i=%u depth=%u scene=%lu f25c=0x%08lx f140=0x%08lx flags=0x%08lx",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), unsigned(r.i),
                unsigned(r.depth), static_cast<unsigned long>(r.scene), static_cast<unsigned long>(r.f25c), static_cast<unsigned long>(r.f140),
                static_cast<unsigned long>(r.flags));
        }
        for (std::uint32_t w = ring_reread_write_.load(std::memory_order_acquire); ring_reread_read_ != w; ++ring_reread_read_) {
            static const char* const kState[4] = {"unknown", "none", "tree", "unknown"};
            const RingRereadRow& r = ring_reread_rows_[ring_reread_read_ & 7];
            log("ship_gate_ring_reread frame=%llu t_ms=%lu gate=0x%08lx id=%lu nodes_before=%u nodes=%u state_before=%s state=%s changed=%u",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate),
                static_cast<unsigned long>(r.id), unsigned(r.nodes_before), unsigned(r.nodes), kState[r.state_before & 3u], kState[r.state & 3u],
                unsigned(r.changed));
        }
        for (std::uint32_t w = handler_slow_write_.load(std::memory_order_acquire); handler_slow_read_ != w; ++handler_slow_read_) {
            const HandlerSlowRow& r = handler_slow_rows_[handler_slow_read_ & 7];
            log("ship_steering_handler_slow frame=%llu t_ms=%lu site=A obj=0x%08lx leaf=%u order=0x%x us=%lu cycles=%llu snap_walk=%u snap_us=%lu",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.obj), unsigned(r.leaf),
                unsigned(r.order), static_cast<unsigned long>(r.us), static_cast<unsigned long long>(r.cycles), unsigned(r.snap_walk),
                static_cast<unsigned long>(r.snap_us));
        }
        for (std::uint32_t w = width_census_write_.load(std::memory_order_acquire); width_census_read_ != w; ++width_census_read_) {
            const WidthCensus& c = width_census_[width_census_read_ & 7];
            // amendment 16b: h_rw_m = sqrt(r^2 + w^2) (the note's h; l_m must not exceed it), len_m = max(r, l) and h_m = sqrt(len^2 + w^2),
            // the half-length and h the rule uses
            const float len = c.l > c.r ? c.l : c.r;
            const double h_rw = core::sqrt_ss(c.r * c.r + c.w * c.w), h = core::sqrt_ss(len * len + c.w * c.w);
            log("ship_capital_census frame=%llu type=%lu class=%s r_m=%.0f w_m=%.0f w_r=%.3f w_src=%s retries=%u l_m=%.0f h_rw_m=%.0f len_m=%.0f h_m=%.0f "
                "part_id=%ld part_lod=%ld o_x=%.0f o_y=%.0f e_x=%.0f e_y=%.0f l_r=%.3f capsule_L_m=%.0f",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(c.type), c.code < core::kClassCount ? core::kClassNames[c.code] : "?",
                double(c.r), double(c.w), c.r > 0.0f ? double(c.w / c.r) : 0.0, ln::width_src_name(ln::WidthSrc(c.w_src)), unsigned(c.retries),
                double(c.l), h_rw, double(len), h, static_cast<long>(c.part_id), static_cast<long>(c.part_lod), double(c.o_x), double(c.o_y),
                double(c.e_x), double(c.e_y), c.r > 0.0f ? double(c.l / c.r) : 0.0, double(len)); // the controller's capsule L = max(r, l)
        }
    }
    if (trace_on_) {
        const std::uint32_t t_now = game_time_ms();
        for (unsigned i = 0; i < kTraceMax; ++i) // a followed ship not seen for kTraceIdleMs frees its slot (gone, or no longer steered)
            if (trace_slots_[i].obj && t_now - trace_slots_[i].last_seen_ms > kTraceIdleMs) trace_slots_[i].obj = 0;
        if (cap_warn_used_) // review F3: a watched capital not hit for kCapWarnExpiryMs (docked, dead, gone from site B) frees its slot
            for (unsigned k = 0; k < kCapWarnSlots; ++k)
                if (cap_warn_[k].obj && t_now - cap_warn_[k].seen_ms > kCapWarnExpiryMs) cap_warn_free(k);
        for (std::uint32_t w = engine_warn_write_.load(std::memory_order_acquire); engine_warn_read_ != w; ++engine_warn_read_) {
            const EngineWarn& e = engine_warns_[engine_warn_read_ & 7];
            log("ship_avoid_engine_warn frame=%llu obj=0x%08lx order=0x12 count=%lu other=0x%08lx class=%lu code=%s r_m=%.1f d_m=%.1f",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(e.obj), static_cast<unsigned long>(e.count),
                static_cast<unsigned long>(e.other), static_cast<unsigned long>(e.cls),
                e.code < core::kClassCount ? core::kClassNames[e.code] : "-", double(e.r), double(e.d));
        }
        // amendment 15 (section 15.4): the capital join, site E's verdicts for slot 0, the engine's list on the warn frame
        static const char* const kModes[4] = {"none", "out", "join", "fallback"};
        static const char* const kReasons[4] = {"-", "pred_none", "past_x", "grid_invalid"};
        static const char* const kWhy[5] = {"-", "mode", "nose", "vel", "bound"};
        for (std::uint32_t w = join_row_write_.load(std::memory_order_acquire); join_row_read_ != w; ++join_row_read_) {
            const JoinRow& r = join_rows_[join_row_read_ & 7];
            char cross[160] = "";
            if (r.crossing)
                std::snprintf(cross, sizeof cross, " cross_nose_deg=%.2f cross_vel_deg=%.2f cross_v=%.1f kept=%u why_not=%s window_s=%.1f",
                              double(r.cross_nose), double(r.cross_vel), double(r.cross_v), unsigned(r.kept), r.why_not < 5 ? kWhy[r.why_not] : "?",
                              double(r.ms_in_window) / 1000.0);
            log("ship_gate_capital_join frame=%llu obj=0x%08lx class=%s slot=%u mode=%s reason=%s s_m=%.0f e_m=%.0f nose_deg=%.2f vel_deg=%.2f "
                "rate_dps=%.2f v_ms=%.1f v_cmd=%.0f carrot_m=%.0f pred_nose_deg=%.2f pred_e_m=%.0f%s",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.obj), r.code < core::kClassCount ? core::kClassNames[r.code] : "?",
                unsigned(r.slot), r.mode < 4 ? kModes[r.mode] : "?", r.latched ? "latched" : r.reason < 4 ? kReasons[r.reason] : "?", double(r.s),
                double(r.e), double(r.nose), double(r.vel), double(r.rate_dps), double(r.v), double(r.v_cmd), double(r.carrot), double(r.pred_nose),
                double(r.pred_e), cross);
        }
        for (std::uint32_t w = e_row_write_.load(std::memory_order_acquire); e_row_read_ != w; ++e_row_read_) {
            const EVerdictRow& r = e_rows_[e_row_read_ & (kERows - 1)];
            // B.3 item 6: an own-gate row of a member A carries why the row passed (gc::GateRow), A's state age and the game step
            char gate_tail[128] = "";
            if (r.gate_row != 0xff)
                std::snprintf(gate_tail, sizeof gate_tail, " gate_row=%s ctl_age_ms=%ld step_ms=%lu", gc::gate_row_name(gc::GateRow(r.gate_row)),
                              long(r.gate_age_ms), static_cast<unsigned long>(r.step_ms));
            else if (r.obst_row != 0xff) // lane fix round 2 item 1: the lane obstacle row's reason
                std::snprintf(gate_tail, sizeof gate_tail, " obst_row=%s step_ms=%lu", gc::obst_row_name(gc::ObstRow(r.obst_row)),
                              static_cast<unsigned long>(r.step_ms));
            log("ship_avoid_e_verdict frame=%llu A=0x%08lx A_order=0x%x B=0x%08lx B_class=%lu B_code=%s B_order=0x%x B_leader=0x%08lx B_flags=0x%08lx "
                "member_leader=0x%08lx member_age_ms=%ld d_m=%.0f r_B=%.1f A_age_ms=%ld B_age_ms=%ld verdict=%s%s",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.a), unsigned(r.a_order), static_cast<unsigned long>(r.b),
                static_cast<unsigned long>(r.b_class), r.b_code < core::kClassCount ? core::kClassNames[r.b_code] : "-", unsigned(r.b_order),
                static_cast<unsigned long>(r.b_leader), static_cast<unsigned long>(r.b_flags), static_cast<unsigned long>(r.member_leader),
                long(r.member_age_ms), double(r.d), double(r.r_b), long(r.a_age_ms), long(r.b_age_ms),
                av::e_verdict_name(EV(r.verdict)), gate_tail);
        }
        for (std::uint32_t w = warn_pre_write_.load(std::memory_order_acquire); warn_pre_read_ != w; ++warn_pre_read_) {
            const WarnPreRow& p = warn_pre_[warn_pre_read_ & 7];
            char entries[720] = "";
            int n = 0;
            for (unsigned i = 0; i < 5 && i < p.count; ++i) {
                const int m = std::snprintf(entries + n, sizeof entries - size_t(n), "%s0x%08lx:%lu:%s:0x%lx:0x%08lx:0x%08lx:%.0f:%.0f:%s", i ? "|" : "",
                                            static_cast<unsigned long>(p.other[i]), static_cast<unsigned long>(p.cls[i]),
                                            p.code[i] < core::kClassCount ? core::kClassNames[p.code[i]] : "-", static_cast<unsigned long>(p.o_order[i]),
                                            static_cast<unsigned long>(p.leader[i]), static_cast<unsigned long>(p.member[i]), double(p.d[i]),
                                            double(p.r[i]), av::e_verdict_name(EV(p.verdict[i])));
                if (m > 0 && n + m < int(sizeof entries)) n += m;
            }
            log("ship_avoid_engine_warn_pre frame=%llu obj=0x%08lx order=0x%lx tgt=0x%08lx count=%lu "
                "entries_other:class:code:order:leader:member:d_m:r_m:e_verdict=%s gate_row=%s obst_row=%s",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(p.obj), static_cast<unsigned long>(p.order),
                static_cast<unsigned long>(p.tgt), static_cast<unsigned long>(p.count), n ? entries : "-",
                p.gate_row != 0xff ? gc::gate_row_name(gc::GateRow(p.gate_row)) : "-",
                p.obst_row != 0xff ? gc::obst_row_name(gc::ObstRow(p.obst_row)) : "-");
        }
        for (std::uint32_t w = trace_write_.load(std::memory_order_acquire); trace_read_ != w; ++trace_read_) {
            const TraceRow& r = trace_ring_[trace_read_ & (kTraceRing - 1)];
            trace_rows_.add();
            if (r.has_e)
                log("ship_trace frame=%llu t_ms=%lu obj=0x%08lx class=%s order=0x%lx tgt=0x%08lx tgt_class=%u pos=%ld,%ld,%ld "
                    "fwd=%.5f,%.5f,%.5f up=%.5f,%.5f,%.5f rate=%ld,%ld,%ld v=%ld des=%ld des_engine=%ld point=%ld,%ld,%ld given=%ld,%ld,%ld "
                    "e=%ld,%ld,%ld dt_ms=%lu W=%ld,%ld,%ld rot=0x%08lx rot_pct=%ld vmax=%ld acc=%ld emg=%u omega_cmd=%.2f,%.2f,%.2f obst=0x%08lx slip=%ld,%ld,%ld cap_ms=%.1f cap_age_ms=%ld",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.obj),
                    r.code < core::kClassCount ? core::kClassNames[r.code] : "?", static_cast<unsigned long>(r.order),
                    static_cast<unsigned long>(r.tgt), unsigned(r.tgt_class), long(r.pos[0]), long(r.pos[1]), long(r.pos[2]), double(r.fwd[0]),
                    double(r.fwd[1]), double(r.fwd[2]), double(r.up[0]), double(r.up[1]), double(r.up[2]), long(r.rate[0]), long(r.rate[1]),
                    long(r.rate[2]), long(r.v), long(r.des), long(r.des_engine), long(r.point[0]), long(r.point[1]), long(r.point[2]), long(r.given[0]), long(r.given[1]),
                    long(r.given[2]), long(r.e[0]), long(r.e[1]), long(r.e[2]), static_cast<unsigned long>(r.dt_ms), long(r.W[0]), long(r.W[1]),
                    long(r.W[2]), static_cast<unsigned long>(r.rot_raw), long(r.rot_pct), long(r.vmax), long(r.accel), unsigned(r.emergency),
                    double(r.omega_cmd[0]), double(r.omega_cmd[1]), double(r.omega_cmd[2]), static_cast<unsigned long>(r.obstacle), long(r.slip[0]),
                    long(r.slip[1]), long(r.slip[2]), double(r.cap), long(r.cap_age_ms));
            else
                log("ship_trace frame=%llu t_ms=%lu obj=0x%08lx class=%s order=0x%lx tgt=0x%08lx tgt_class=%u pos=%ld,%ld,%ld "
                    "fwd=%.5f,%.5f,%.5f up=%.5f,%.5f,%.5f rate=%ld,%ld,%ld v=%ld des=%ld des_engine=%ld point=%ld,%ld,%ld given=%ld,%ld,%ld "
                    "e=- dt_ms=%lu W=%ld,%ld,%ld rot=0x%08lx rot_pct=%ld vmax=%ld acc=%ld emg=%u omega_cmd=%.2f,%.2f,%.2f obst=0x%08lx slip=%ld,%ld,%ld "
                    "cap_ms=%.1f cap_age_ms=%ld",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.obj),
                    r.code < core::kClassCount ? core::kClassNames[r.code] : "?", static_cast<unsigned long>(r.order),
                    static_cast<unsigned long>(r.tgt), unsigned(r.tgt_class), long(r.pos[0]), long(r.pos[1]), long(r.pos[2]), double(r.fwd[0]),
                    double(r.fwd[1]), double(r.fwd[2]), double(r.up[0]), double(r.up[1]), double(r.up[2]), long(r.rate[0]), long(r.rate[1]),
                    long(r.rate[2]), long(r.v), long(r.des), long(r.des_engine), long(r.point[0]), long(r.point[1]), long(r.point[2]), long(r.given[0]), long(r.given[1]),
                    long(r.given[2]), static_cast<unsigned long>(r.dt_ms), long(r.W[0]), long(r.W[1]), long(r.W[2]), static_cast<unsigned long>(r.rot_raw),
                    long(r.rot_pct), long(r.vmax), long(r.accel), unsigned(r.emergency), double(r.omega_cmd[0]), double(r.omega_cmd[1]),
                    double(r.omega_cmd[2]), static_cast<unsigned long>(r.obstacle), long(r.slip[0]), long(r.slip[1]), long(r.slip[2]), double(r.cap), long(r.cap_age_ms));
        }
    }
    if (debug_ && gates_on_) {
        for (std::uint32_t w = gate_trans_write_.load(std::memory_order_acquire); gate_trans_read_ != w; ++gate_trans_read_) {
            const GateTransition& e = gate_trans_[gate_trans_read_ & 7];
            log("ship_gate_transition frame=%llu obj=0x%08lx class=%s capital=%u validated=%s crossed=%u last_dA_r=%.2f err_B_deg=%.1f "
                "v_ms=%.1f desired_ms=%.1f B_gate_r=%.2f,%.2f,%.2f A_gate_r=%.2f,%.2f,%.2f dock_blocked=%u "
                "p2_pos_r=%.2f,%.2f,%.2f p2_fwd=%.2f,%.2f,%.2f p2_carrot_r=%.2f,%.2f,%.2f o5_pos_r=%.2f,%.2f,%.2f o5_fwd=%.2f,%.2f,%.2f "
                "o5_dA_r=%.2f o5_dB_r=%.2f ms_since_p2=%lu carrot_age_ms=%ld",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(e.obj),
                e.code < core::kClassCount ? core::kClassNames[e.code] : "?", unsigned(e.capital),
                e.validated == 1 ? "matched" : e.validated == 2 ? "fallback" : "none", unsigned(e.crossed), double(e.dA_r), double(e.err_B_deg),
                double(e.v_ms), double(e.desired_ms), double(e.B_local[0]), double(e.B_local[1]), double(e.B_local[2]), double(e.A_local[0]),
                double(e.A_local[1]), double(e.A_local[2]), unsigned(e.dock_blocked), double(e.p2_pos[0]), double(e.p2_pos[1]), double(e.p2_pos[2]),
                double(e.p2_fwd[0]), double(e.p2_fwd[1]), double(e.p2_fwd[2]), double(e.p2_carrot[0]), double(e.p2_carrot[1]), double(e.p2_carrot[2]),
                double(e.o5_pos[0]), double(e.o5_pos[1]), double(e.o5_pos[2]), double(e.o5_fwd[0]), double(e.o5_fwd[1]), double(e.o5_fwd[2]),
                double(e.o5_dA_r), double(e.o5_dB_r), static_cast<unsigned long>(e.ms_since_p2), long(e.carrot_age_ms));
        }
        for (std::uint32_t w = gate_event_write_.load(std::memory_order_acquire); gate_event_read_ != w; ++gate_event_read_) {
            const GateEvent& e = gate_events_[gate_event_read_ & 7];
            log("ship_gate_approach frame=%llu obj=0x%08lx class=%s capital=%u ring=%u offset_m=%.0f,%.0f cross_v_ms=%.1f "
                "cross_heading_err_deg=%.1f cross_to_launch_ms=%lu v0_ms=%.1f",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(e.obj),
                e.code < core::kClassCount ? core::kClassNames[e.code] : "?", unsigned(e.capital), unsigned(e.spread_k), double(e.off[0]),
                double(e.off[1]), double(e.cross_v), double(e.cross_err_deg), static_cast<unsigned long>(e.cross_to_launch_ms),
                double(e.v0_units) / double(core::kUnitsPerMetre));
        }
    }
    if (now - last_summary_tick_ < 1000) return;
    const unsigned long long interval = last_summary_tick_ ? now - last_summary_tick_ : 1000;
    last_summary_tick_ = now;
    const std::uint32_t calls = calls_.get(), handled = handled_.get(), ccalls = clamp_calls_.get(),
                        capplied = clamp_applied_.get(), cstale = clamp_stale_.get(), redirected = redirected_.get();
    const std::uint32_t d_calls = calls - rep_calls_;
    if (!d_calls && ccalls == rep_clamp_calls_) return; // nothing happened: no row
    // Percentiles from the log2 histogram (bucket b holds ticks in [2^b, 2^(b+1))).
    std::uint32_t hist[32], total = 0;
    for (unsigned b = 0; b < 32; ++b) {
        hist[b] = qpc_hist_[b].exchange(0, std::memory_order_relaxed);
        total += hist[b];
    }
    const std::uint32_t max_ticks = qpc_max_.exchange(0, std::memory_order_relaxed);
    auto percentile = [&](double p) {
        std::uint32_t seen = 0;
        for (unsigned b = 0; b < 32; ++b) {
            seen += hist[b];
            if (total && double(seen) >= p * double(total)) return double(1ull << (b + 1)) * 1e6 / double(qpc_frequency_);
        }
        return 0.0;
    };
    const double us_max = qpc_frequency_ ? double(max_ticks) * 1e6 / double(qpc_frequency_) : 0.0;
    // the percentiles are bucket upper bounds (powers of two): never above the measured maximum
    auto clamped = [&](double p) {
        const double v = percentile(p);
        return v > us_max ? us_max : v;
    };
    const unsigned ships = table_.live(game_time_ms(), 1000);
    const double per_ship = ships ? double(d_calls) * 1000.0 / double(interval) / double(ships) : 0.0;
    char reasons[512];
    int n = 0;
    for (unsigned r = 1; r < core::kReasonCount; ++r) {
        const std::uint32_t v = reasons_[r].get();
        const int k = std::snprintf(reasons + n, sizeof reasons - size_t(n), " pass_%s=%lu", core::reason_name(core::Reason(r)),
                                    static_cast<unsigned long>(v - rep_reasons_[r]));
        rep_reasons_[r] = v;
        if (k > 0 && n + k < int(sizeof reasons)) n += k;
    }
    char orders[256];
    n = 0;
    for (unsigned o = 0; o < 0x40; ++o) {
        const std::uint32_t v = handled_by_order_[o].get();
        if (!v) continue;
        const int k = std::snprintf(orders + n, sizeof orders - size_t(n), "%s0x%x:%lu", n ? "," : "", o, static_cast<unsigned long>(v));
        if (k > 0 && n + k < int(sizeof orders)) n += k;
    }
    const std::uint32_t l0 = leaf_calls_[0].get(), l1 = leaf_calls_[1].get(), l2 = leaf_calls_[2].get();
    char avoidance[2048] = "";
    if (avoidance_on_) {
        const Counter* cs[13] = {&av_calls_, &av_conflicts_, &av_detours_, &av_unresolved_, &av_overflow_, &av_snapshots_, &av_walk_objects_,
                                 &av_emergency_, &e_calls_, &e_suppressed_, &part_built_, &part_fallback_, &part_invalid_};
        std::uint32_t d[13];
        for (unsigned i = 0; i < 13; ++i) {
            const std::uint32_t v = cs[i]->get();
            d[i] = v - rep_av_[i];
            rep_av_[i] = v;
        }
        unsigned snap_max = 0;
        for (unsigned i = 0; i < sites::kSnapshotSlots; ++i)
            if (snaps_[i].count > snap_max) snap_max = snaps_[i].count;
        std::uint32_t why[unsigned(av::Unresolved::count)];
        for (unsigned i = 0; i < unsigned(av::Unresolved::count); ++i) {
            const std::uint32_t v = av_unresolved_why_[i].get();
            why[i] = v - rep_av_why_[i];
            rep_av_why_[i] = v;
        }
        const Counter* ep[4] = {&av_conflict_episodes_, &av_unresolved_episodes_, &part_dirty_, &part_scaled_};
        std::uint32_t e[4];
        for (unsigned i = 0; i < 4; ++i) {
            const std::uint32_t v = ep[i]->get();
            e[i] = v - rep_av_ep_[i];
            rep_av_ep_[i] = v;
        }
        // amendments 13/15: site E's capital rows and the membership memory, totals since install (per reason: the pass-throughs)
        const std::uint32_t es_small = e_suppressed_small_.get(), ep_small = e_small_pass_.get();
        std::uint32_t er_small[unsigned(av::SmallPair::count)];
        for (unsigned i = 0; i < unsigned(av::SmallPair::count); ++i) er_small[i] = e_small_pass_reason_[i].get();
        const av::MemberStats& ms = member_stats_;
        std::snprintf(avoidance, sizeof avoidance,
                      " avoid_calls=%lu conflicts=%lu conflict_episodes=%lu detours=%lu unresolved=%lu unresolved_episodes=%lu "
                      "unresolved_inside_stop=%lu unresolved_inside_turn=%lu unresolved_blocked=%lu unresolved_behind=%lu unresolved_degenerate=%lu "
                      "snapshot_overflow=%lu snapshots=%lu walked=%lu emergency=%lu e_calls=%lu e_suppressed=%lu part_models=%lu "
                      "part_fallback=%lu part_invalid=%lu part_unfilled=%lu part_scaled=%lu snapshot_max=%u warn=%s "
                      "e_suppressed_small=%lu e_small_pass=%lu e_small_pass_no_autopilot=%lu e_small_pass_not_steerable=%lu e_small_pass_class=%lu "
                      "e_small_pass_b_dock_leg=%lu e_small_pass_own_leader=%lu e_suppressed_member=%lu e_suppressed_gate=%lu e_suppressed_gate_small=%lu "
                      "avoid_excluded_member=%lu member_set=%u member_refresh=%u member_replace=%u member_expire=%u member_leader_gone=%u "
                      "member_id_mismatch=%u member_reversed=%u member_player=%u member_cleared=%u member_full=%u e_small_gap=%lu",
                      static_cast<unsigned long>(d[0]), static_cast<unsigned long>(d[1]), static_cast<unsigned long>(e[0]),
                      static_cast<unsigned long>(d[2]), static_cast<unsigned long>(d[3]), static_cast<unsigned long>(e[1]),
                      static_cast<unsigned long>(why[1]), static_cast<unsigned long>(why[2]), static_cast<unsigned long>(why[3]),
                      static_cast<unsigned long>(why[4]), static_cast<unsigned long>(why[5]), static_cast<unsigned long>(d[4]),
                      static_cast<unsigned long>(d[5]), static_cast<unsigned long>(d[6]), static_cast<unsigned long>(d[7]),
                      static_cast<unsigned long>(d[8]), static_cast<unsigned long>(d[9]), static_cast<unsigned long>(d[10]),
                      static_cast<unsigned long>(d[11]), static_cast<unsigned long>(d[12]), static_cast<unsigned long>(e[2]),
                      static_cast<unsigned long>(e[3]), snap_max, warn_state_, static_cast<unsigned long>(es_small),
                      static_cast<unsigned long>(ep_small), static_cast<unsigned long>(er_small[1]), static_cast<unsigned long>(er_small[2]),
                      static_cast<unsigned long>(er_small[3]), static_cast<unsigned long>(er_small[4]), static_cast<unsigned long>(er_small[5]),
                      static_cast<unsigned long>(e_suppressed_member_.get()), static_cast<unsigned long>(e_suppressed_gate_.get()),
                      static_cast<unsigned long>(e_suppressed_gate_small_.get()), static_cast<unsigned long>(avoid_excluded_member_.get()), ms.set,
                      ms.refresh, ms.replace, ms.expire, ms.leader_gone, ms.id_mismatch, ms.reversed, ms.player, ms.cleared, ms.full,
                      static_cast<unsigned long>(e_small_gap_.get())); // B.4: suppress_small on a stale A (the order-0 gap)
    }
    char gates[kSummaryGatesBytes] = "";
    if (gates_on_) {
        const Counter* gc[29] = {&gate_lead_, &gate_cross_, &gate_p2_, &gate_stops_skipped_, &gate_p4_, &gate_v0_kept_, &gate_p5_,
                                 &gate_launch_continued_, &gate_speed_writes_, &gate_spread_offsets_, &gate_capital_aligned_, &gate_lead_validated_,
                                 &gate_lead_fallback_, &gate_lead_window_rejected_, &gate_dock_writes_, &gate_dock_blocked_,
                                 &gate_p2_no_gate_target_, &gate_p2_capital_, &gate_p2_no_state_, &gate_p4_no_gate_target_, &gate_p4_capital_,
                                 &gate_p4_no_state_, &gate_dock_invalid_, &gate_rows_dropped_, &avoid_excluded_followers_, &avoid_skipped_small_,
                                 &gate_capital_kept_, &gate_capital_lead_validated_, &gate_capital_lead_fallback_};
        std::uint32_t d[29];
        for (unsigned i = 0; i < 29; ++i) {
            const std::uint32_t v = gc[i]->get();
            d[i] = v - rep_gate_[i];
            rep_gate_[i] = v;
        }
        std::snprintf(gates, sizeof gates,
                      " gates=%s gate_lead_calls=%lu gate_crossings=%lu gate_p2=%lu gate_stops_skipped=%lu gate_p4=%lu gate_v0_kept=%lu gate_p5=%lu "
                      "gate_launch_continued=%lu gate_speed_writes=%lu gate_spread_offsets=%lu gate_capital_aligned=%lu gate_lead_validated=%lu "
                      "gate_lead_fallback=%lu gate_lead_window_rejected=%lu gate_dock_writes=%lu gate_dock_blocked=%lu gate_p2_no_gate_target=%lu "
                      "gate_p2_capital=%lu gate_p2_no_state=%lu gate_p4_no_gate_target=%lu gate_p4_capital=%lu gate_p4_no_state=%lu "
                      "gate_dock_invalid=%lu gate_rows_dropped=%lu avoid_excluded_followers=%lu avoid_skipped_small=%lu gates_capital=%s "
                      "gate_capital_kept=%lu gate_capital_lead_validated=%lu gate_capital_lead_fallback=%lu",
                      gates_state_, static_cast<unsigned long>(d[0]), static_cast<unsigned long>(d[1]), static_cast<unsigned long>(d[2]),
                      static_cast<unsigned long>(d[3]), static_cast<unsigned long>(d[4]), static_cast<unsigned long>(d[5]),
                      static_cast<unsigned long>(d[6]), static_cast<unsigned long>(d[7]), static_cast<unsigned long>(d[8]),
                      static_cast<unsigned long>(d[9]), static_cast<unsigned long>(d[10]), static_cast<unsigned long>(d[11]),
                      static_cast<unsigned long>(d[12]), static_cast<unsigned long>(d[13]), static_cast<unsigned long>(d[14]),
                      static_cast<unsigned long>(d[15]), static_cast<unsigned long>(d[16]), static_cast<unsigned long>(d[17]),
                      static_cast<unsigned long>(d[18]), static_cast<unsigned long>(d[19]), static_cast<unsigned long>(d[20]),
                      static_cast<unsigned long>(d[21]), static_cast<unsigned long>(d[22]), static_cast<unsigned long>(d[23]),
                      static_cast<unsigned long>(d[24]), static_cast<unsigned long>(d[25]), gates_capital_state_,
                      static_cast<unsigned long>(d[26]), static_cast<unsigned long>(d[27]), static_cast<unsigned long>(d[28]));
        // amendment 15 (section 15.4): the join, totals since install; the predictor's cost (the slowest evaluation, us)
        const std::size_t used = std::strlen(gates);
        std::snprintf(gates + used, sizeof gates - used,
                      " gate_capital_join=%lu gate_capital_slowed=%lu gate_capital_fallback_pred=%lu gate_capital_fallback_past_x=%lu "
                      "gate_capital_not_kept_mode=%lu gate_capital_not_kept_nose=%lu gate_capital_not_kept_vel=%lu gate_capital_not_kept_bound=%lu "
                      "join_evals=%lu join_predictions=%lu join_eval_us_max=%.1f join_slots=%u join_slots_dropped=%u join_slots_refused=%u",
                      static_cast<unsigned long>(gate_capital_join_.get()), static_cast<unsigned long>(gate_capital_slowed_.get()),
                      static_cast<unsigned long>(gate_capital_fallback_pred_.get()), static_cast<unsigned long>(gate_capital_fallback_past_x_.get()),
                      static_cast<unsigned long>(gate_capital_not_kept_[1].get()), static_cast<unsigned long>(gate_capital_not_kept_[2].get()),
                      static_cast<unsigned long>(gate_capital_not_kept_[3].get()), static_cast<unsigned long>(gate_capital_not_kept_[4].get()),
                      static_cast<unsigned long>(join_evals_), static_cast<unsigned long>(join_predictions_),
                      // via int32 (cvtsi2sd): a 64-bit integer -> double conversion is x87 fild on i686
                      qpc_frequency_ ? double(std::int32_t(join_eval_max_ticks_ > 0x7fffffffu ? 0x7fffffffu : join_eval_max_ticks_)) * 1e6 /
                                           double(std::int32_t(qpc_frequency_ > 0x7fffffffull ? 0x7fffffffu : std::uint32_t(qpc_frequency_)))
                                     : 0.0,
                      join_reg_.count, join_reg_.dropped, join_reg_.refused);
        // amendment 16 fix: the join's write kept on a detour, totals since install
        const std::size_t used16 = std::strlen(gates);
        std::snprintf(gates + used16, sizeof gates - used16, " gate_capital_detour_write=%lu",
                      static_cast<unsigned long>(gate_capital_detour_write_.get()));
        // the gate traffic controller's ring changes (D.7): the per-source drop counters, totals since install (their sum is
        // gate_rows_dropped's total)
        // every append is bounded by the buffer: snprintf truncates, and used is clamped below its size (a full buffer ends the row; the
        // longest row is bounded at 3,614 characters by summary_bound.py, each conversion at its widest: gate-traffic-controller/)
        auto clamp_used = [&](std::size_t u) { return u < sizeof gates - 1 ? u : sizeof gates - 1; };
        std::size_t used_d = std::strlen(gates);
        used_d = clamp_used(used_d + std::size_t(std::snprintf(gates + used_d, sizeof gates - used_d, " rows_dropped=")));
        for (unsigned i = 0; i < unsigned(Drop::count); ++i)
            used_d = clamp_used(used_d + std::size_t(std::snprintf(gates + used_d, sizeof gates - used_d, "%s%s:%lu", i ? "," : "", kDropNames[i],
                                                                   static_cast<unsigned long>(rows_dropped_[i].get()))));
        if (traffic_on_) { // the controller (D.7), totals since install
            unsigned members = 0;
            for (const gc::Gate& cg : ctl_.g)
                for (unsigned i = 0; i < cg.n; ++i) members += gc::pair_role(cg.m[i].role) ? 1u : 0u;
            std::snprintf(gates + used_d, sizeof gates - used_d,
                          " traffic=%s ctl_members=%u ctl_decisions=%lu ctl_rollouts=%lu ctl_sweeps=%lu ctl_narrow=%lu ctl_conflicts=%lu "
                          "ctl_yields=%lu ctl_capped=%lu ctl_released=%lu ctl_unresolved=%lu ctl_entered=%lu ctl_left=%lu ctl_evicted=%lu "
                          "ctl_full=%u,%u e_suppressed_ctl=%lu e_pass_ctl=%lu e_suppressed_arrival_gate=%lu avoid_ctl_excluded=%lu "
                          "arr_launches=%lu arr_offset_capital=%lu arr_small_side=%lu arr_small_unseparated=%lu arr_skipped=%lu "
                          "arr_overwritten=%u ctl_decide_us_max=%.1f ctl_frame_us_max=%.1f ctl_frame_decisions_max=%u ctl_arrival_us_max=%.1f "
                          "ctl_x_refused=%lu ctl_x_wait_s=%lu ctl_sb_steered=%lu "
                          "ctl_p2_zeroed=%lu small_b_rewrites=%lu ctl_sb_us_max=%.1f e_pass_ctl_why=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu e_gate_small_gap=%lu "
                          "ctl_misc=%lu,%lu ctl_small=%lu,%lu,%lu ctl_prof=%lu,%lu,%lu ctl_prof_us_max=%.1f ctl_evict_ignored_gate=%lu ctl_budget_cut=%lu ctl_live=%lu,%lu,%lu,%lu,%lu,%lu "
                          "ctl_live_us_max=%.1f",
                          traffic_state_, members, static_cast<unsigned long>(ctl_decisions_.get()), static_cast<unsigned long>(ctl_rollouts_.get()),
                          static_cast<unsigned long>(ctl_sweeps_.get()), static_cast<unsigned long>(ctl_narrow_.get()),
                          static_cast<unsigned long>(ctl_conflicts_.get()), static_cast<unsigned long>(ctl_yields_.get()),
                          static_cast<unsigned long>(ctl_capped_.get()), static_cast<unsigned long>(ctl_released_.get()),
                          static_cast<unsigned long>(ctl_unresolved_.get()), static_cast<unsigned long>(ctl_entered_.get()),
                          static_cast<unsigned long>(ctl_left_.get()), static_cast<unsigned long>(ctl_evicted_.get()), ctl_.full_gates,
                          ctl_.full_members, static_cast<unsigned long>(e_suppressed_ctl_.get()), static_cast<unsigned long>(e_pass_ctl_.get()),
                          static_cast<unsigned long>(e_suppressed_arrival_gate_.get()), static_cast<unsigned long>(avoid_ctl_excluded_.get()),
                          static_cast<unsigned long>(arr_launches_.get()), static_cast<unsigned long>(arr_offset_capital_.get()),
                          static_cast<unsigned long>(arr_small_side_.get()), static_cast<unsigned long>(arr_small_unseparated_.get()),
                          static_cast<unsigned long>(arr_skipped_.get()), arrivals_.overwritten, ticks_us(ctl_decide_max_ticks_),
                          ticks_us(ctl_frame_max_ticks_), ctl_frame_max_decisions_, ticks_us(ctl_arrival_max_ticks_),
                          static_cast<unsigned long>(ctl_x_refused_.get()), static_cast<unsigned long>(ctl_x_wait_ms_ / 1000u),
                          static_cast<unsigned long>(ctl_sb_steered_.get()), static_cast<unsigned long>(ctl_p2_zeroed_.get()),
                          static_cast<unsigned long>(small_b_rewrites_.get()), ticks_us(ctl_sb_max_ticks_),
                          // e_pass_ctl_why: none, unresolved, stale, starved, on_0x12, rows, order0, not_member (gc::EWhy 1..8)
                          static_cast<unsigned long>(e_pass_ctl_why_[1].get()), static_cast<unsigned long>(e_pass_ctl_why_[2].get()),
                          static_cast<unsigned long>(e_pass_ctl_why_[3].get()), static_cast<unsigned long>(e_pass_ctl_why_[4].get()),
                          static_cast<unsigned long>(e_pass_ctl_why_[5].get()), static_cast<unsigned long>(e_pass_ctl_why_[6].get()),
                          static_cast<unsigned long>(e_pass_ctl_why_[7].get()), static_cast<unsigned long>(e_pass_ctl_why_[8].get()),
                          static_cast<unsigned long>(e_gate_small_gap_.get()),
                          // ctl_misc: game-clock resets, small B both sides taken
                          static_cast<unsigned long>(ctl_clock_resets_.get()), static_cast<unsigned long>(small_b_both_.get()),
                          // section 12.2 ctl_small: order-4 decisions on the side lead-in, D.5 edge rewrites, holds begun
                          static_cast<unsigned long>(ctl_small_side_.get()), static_cast<unsigned long>(ctl_small_edge_.get()),
                          static_cast<unsigned long>(ctl_small_hold_.get()),
                          // section 12.3 ctl_prof: lane profile builds, builds with a shift, statics a member's avoidance left to the profile;
                          // the longest build
                          static_cast<unsigned long>(ctl_prof_builds_.get()), static_cast<unsigned long>(ctl_prof_shifted_.get()),
                          static_cast<unsigned long>(avoid_lane_skipped_.get()), ticks_us(ctl_prof_max_ticks_),
                          // B.3: 0x12 episodes kept through (item 2), frames whose decisions stopped at the 1 ms ceiling
                          static_cast<unsigned long>(ctl_evict_ignored_gate_.get()), static_cast<unsigned long>(ctl_budget_cut_.get()),
                          // B.5 ctl_live: members left unproven (gone), launched members left, sector resets, entries they dropped,
                          // member-frames without a proof, frames whose set overflowed; the proofs' longest frame
                          static_cast<unsigned long>(ctl_live_gone_.get()), static_cast<unsigned long>(ctl_live_launch_.get()),
                          static_cast<unsigned long>(ctl_sector_resets_.get()), static_cast<unsigned long>(ctl_sector_dropped_.get()),
                          static_cast<unsigned long>(ctl_unproven_.get()), static_cast<unsigned long>(ctl_live_overflow_.get()),
                          ticks_us(ctl_live_max_ticks_));
            ctl_sb_max_ticks_ = 0; // B.2 item 5: ctl_sb_us_max is the summary interval's maximum (the other maxima stay session-lifetime)
        }
    }
    char dock[640] = "";
    if (avoidance_on_) { // amendment 18, totals since install
        std::snprintf(dock, sizeof dock,
                      " dock=%s e_suppressed_dock=%lu e_pass_dock_parts=%lu e_pass_dock_path=%lu e_pass_dock_ray=%lu e_pass_dock_zone=%lu "
                      "dock_part_obstacles=%lu dock_detours=%lu idle_steered=%lu dock_models_built=%lu dock_no_model=%lu dock_rows_dropped=%lu "
                      "dock_parts_evicted=%lu dock_e_us_max=%.1f",
                      dock_state_, static_cast<unsigned long>(e_suppressed_dock_.get()),
                      static_cast<unsigned long>(e_pass_dock_[unsigned(av::DockE::no_parts)].get()),
                      static_cast<unsigned long>(e_pass_dock_[unsigned(av::DockE::path)].get()),
                      static_cast<unsigned long>(e_pass_dock_[unsigned(av::DockE::ray)].get()),
                      static_cast<unsigned long>(e_pass_dock_[unsigned(av::DockE::zone)].get()),
                      static_cast<unsigned long>(dock_part_obstacles_.get()), static_cast<unsigned long>(dock_detours_.get()),
                      static_cast<unsigned long>(idle_steered_.get()), static_cast<unsigned long>(dock_models_built_.get()),
                      static_cast<unsigned long>(dock_no_model_.get()), static_cast<unsigned long>(dock_rows_dropped_.get()),
                      static_cast<unsigned long>(dock_parts_evicted_.get()), ticks_us(dock_e_max_ticks_));
    }
    char trace[96] = "";
    if (trace_on_) {
        const std::uint32_t rows = trace_rows_.get(), dropped = trace_dropped_.get();
        std::snprintf(trace, sizeof trace, " ship_trace_rows=%lu ship_trace_dropped=%lu", static_cast<unsigned long>(rows - rep_trace_[0]),
                      static_cast<unsigned long>(dropped - rep_trace_[1]));
        rep_trace_[0] = rows;
        rep_trace_[1] = dropped;
    }
    log("ship_steering_summary frame=%llu interval_ms=%llu claims=%u calls=%lu handled=%lu ships=%u calls_per_ship_s=%.1f "
        "leaf_calls=%lu,%lu,%lu%s clamp_calls=%lu clamp_applied=%lu clamp_stale=%lu redirected=%lu timed=%lu "
        "handler_us_p50=%.2f handler_us_p99=%.2f handler_us_max=%.2f handled_by_order_total=%s stuck_total=%lu%s%s%s%s",
        static_cast<unsigned long long>(frame), interval, unsigned(sites::Count), static_cast<unsigned long>(d_calls),
        static_cast<unsigned long>(handled - rep_handled_), ships, per_ship, static_cast<unsigned long>(l0 - rep_leaf_[0]),
        static_cast<unsigned long>(l1 - rep_leaf_[1]), static_cast<unsigned long>(l2 - rep_leaf_[2]), reasons,
        static_cast<unsigned long>(ccalls - rep_clamp_calls_), static_cast<unsigned long>(capplied - rep_clamp_applied_),
        static_cast<unsigned long>(cstale - rep_clamp_stale_), static_cast<unsigned long>(redirected - rep_redirected_),
        static_cast<unsigned long>(total), clamped(0.5), clamped(0.99), us_max, orders,
        static_cast<unsigned long>(stuck_count_.load(std::memory_order_relaxed)), avoidance, gates, dock, trace);
    if (lane_on_) { // lane stage 1 (convoy-gate-transit.md section 6.9), totals since install: its own row (the summary's gates text is bounded)
        // lane fix round 1 item 5: each lane gate's frame (its F, the axis the lane's s and the rows' p_m F component run along; G; r)
        char frames[4 * 112] = "-";
        int nf = 0;
        for (const gc::Gate& lg : ctl_.g) {
            if (!lg.gate || !lg.lane) continue;
            const int m = std::snprintf(frames + nf, sizeof frames - size_t(nf), "%s0x%08lx:%.4f,%.4f,%.4f:%.0f,%.0f,%.0f:%.0f", nf ? "|" : "",
                                        static_cast<unsigned long>(lg.gate), double(lg.F[0]), double(lg.F[1]), double(lg.F[2]), double(lg.G[0]),
                                        double(lg.G[1]), double(lg.G[2]), double(lg.r_gate));
            if (m > 0 && nf + m < int(sizeof frames)) nf += m;
        }
        log("ship_steering_lane_summary frame=%llu lane=%s ctl_merge_yield=%lu ctl_merge_stop=%lu ctl_point_rewrites=%lu ctl_lane_writes=%lu "
            "ctl_x_refused=%lu ctl_x_wait_s=%lu ctl_grid_miss=%lu ctl_point_adopts=%lu ctl_lane_defers=%lu ctl_defer_rewrites=%lu "
            "ctl_recommit_held=%lu e_lane_obst=%lu e_lane_obst_roll=%lu e_gate_ring=%lu e_gate_ring_kept=%lu ctl_mouth_expired=%lu gates_gate:F:G:r=%s",
            static_cast<unsigned long long>(frame), lane_state_, static_cast<unsigned long>(ctl_merge_yield_.get()),
            static_cast<unsigned long>(ctl_merge_stop_.get()), static_cast<unsigned long>(ctl_point_rewrites_.get()),
            static_cast<unsigned long>(ctl_lane_writes_.get()), static_cast<unsigned long>(ctl_x_refused_.get()),
            static_cast<unsigned long>(ctl_x_wait_ms_ / 1000u), static_cast<unsigned long>(ctl_grid_miss_.get()),
            static_cast<unsigned long>(ctl_point_adopts_.get()), static_cast<unsigned long>(ctl_lane_defers_.get()),
            static_cast<unsigned long>(ctl_defer_rewrites_.get()), static_cast<unsigned long>(ctl_recommit_held_.get()),
            static_cast<unsigned long>(e_lane_obst_.get()), static_cast<unsigned long>(e_lane_obst_roll_.get()),
            static_cast<unsigned long>(e_gate_ring_.get()), static_cast<unsigned long>(e_gate_ring_kept_.get()),
            static_cast<unsigned long>(ctl_mouth_expired_.get()), frames);
        // the arrival round item 6: the piloted ship within 15 km of a gate, once per report: its position (the freshest snapshot of the
        // last 2 s of game time that holds it), the nearest gate of that snapshot and the nearest board member (its state), m
        const std::uint32_t t_now = game_time_ms();
        const av::Snapshot* ps = nullptr;
        for (const av::Snapshot& sn : snaps_)
            if (sn.player && std::int32_t(t_now - sn.epoch) >= 0 && t_now - sn.epoch <= 2000u && (!ps || std::int32_t(sn.epoch - ps->epoch) > 0)) ps = &sn;
        if (ps) {
            float dg = 3.4e38f, dm = 3.4e38f;
            std::uint32_t gate = 0, member = 0;
            for (unsigned i = 0; i < ps->count; ++i) {
                const av::Obstacle& o = ps->items[i];
                if (o.kind != std::uint8_t(av::Kind::gate)) continue;
                const float d[3] = {o.x[0] - ps->player_x[0], o.x[1] - ps->player_x[1], o.x[2] - ps->player_x[2]}, dd = core::sqrt_ss(core::dot3(d, d));
                if (dd < dg) dg = dd, gate = o.ptr;
            }
            if (gate && dg <= 15000.0f) {
                for (const gc::Gate& lg : ctl_.g)
                    for (unsigned i = 0; lg.gate && i < lg.n; ++i) {
                        const gc::Member& m = lg.m[i];
                        if (!m.ship || !gc::pair_role(m.role) || !m.st.ms) continue;
                        const float d[3] = {m.st.x[0] - ps->player_x[0], m.st.x[1] - ps->player_x[1], m.st.x[2] - ps->player_x[2]};
                        const float dd = core::sqrt_ss(core::dot3(d, d));
                        if (dd < dm) dm = dd, member = m.ship;
                    }
                log("ship_player_pos frame=%llu t_ms=%lu obj=0x%08lx pos=%.0f,%.0f,%.0f gate=0x%08lx d_gate_m=%.0f member=0x%08lx d_member_m=%.0f",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(t_now), static_cast<unsigned long>(ps->player),
                    double(ps->player_x[0]), double(ps->player_x[1]), double(ps->player_x[2]), static_cast<unsigned long>(gate), double(dg),
                    static_cast<unsigned long>(member), member ? double(dm) : -1.0);
            }
        }
    }
    rep_calls_ = calls;
    rep_handled_ = handled;
    rep_clamp_calls_ = ccalls;
    rep_clamp_applied_ = capplied;
    rep_clamp_stale_ = cstale;
    rep_redirected_ = redirected;
    rep_leaf_[0] = l0;
    rep_leaf_[1] = l1;
    rep_leaf_[2] = l2;
}
} // namespace x3m::ship_steering
