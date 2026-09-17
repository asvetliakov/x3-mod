// ---- own-ship-adaptive cascade 0 (shadow-cascade-extents.md, section 5) ----
//
// X3M_SHADOW_CASCADE_ADAPTIVE_C0 = k (default off). Included by
// motion_output.cpp inside namespace x3m. Off: no code runs (every site tests
// cascade_adaptive_on()). On, per frame: the own ship is resolved once at the
// frame's first candidate draw (six to eight bounded engine reads through the
// registry walk the chase camera uses: active cockpit, ref object, root node),
// each z-writing draw with a known extent costs one pointer compare (a scope
// node other than the root: one cache probe keyed on node and handle; a cache
// miss walks the parent links, at most own_ship::walks_per_frame walks per
// frame, the rest deferred), and an own-ship draw adds eight corner
// transforms. Draws without a known extent pay nothing. The commit runs at the frame boundary (begin_frame) so a
// frame's box test, replay and apply share one set; a changed E0 re-snaps
// cascade 0's grid (the texel changed) and voids only its retained map. No
// allocation, no device call.

// The own ship of this frame. Production: the verified executable's registry
// (docs/reverse-engineering/chase-camera-first-flight.md; the same walk as
// chase_lead::active and object_capture::target) at the cockpit registry root
// 0x608504; a foreign executable, a missing cockpit or an unreadable field
// leaves the frame without an own ship (E0 returns to the configured value
// after the hysteresis). Fixture: the seam's injected node.
void MotionOutput::resolve_own_ship() noexcept {
    if (own_ship_frame_ == frame_) return;
    own_ship_frame_ = frame_; own_ship_node_ = 0; own_ship_handle_ = 0;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_own_ship_set_) { own_ship_node_ = fixture_own_ship_node_; own_ship_handle_ = fixture_own_ship_handle_; own_ship_status_ = unsigned(object_capture::Status::Ready); return; }
#endif
    if (!object_trace::executable_verified()) { own_ship_status_ = ~0u; return; }
    const DWORD error = GetLastError();
    auto read = [](std::uintptr_t p, void* out, std::size_t n) { return engine_memory::read(p, out, n); };
    const auto s = object_capture::own_ship(read, 0x608504);
    own_ship_status_ = unsigned(s.status);
    if (s.status == object_capture::Status::Ready) { own_ship_node_ = s.node; own_ship_handle_ = s.node_handle; }
    SetLastError(error);
}
// Whether a draw's scope node belongs to the own ship: the root itself, or a
// node whose parent chain reaches it (turrets, engines and other parts hang
// under the ship's root node). The verdict is cached per (node, handle) while
// the root and the epochs are the same (own_ship_cache.h: at most
// walks_per_frame walks per frame, the rest deferred as not-own this frame).
bool MotionOutput::own_ship_draw(std::uintptr_t node, std::uint32_t handle, std::uint64_t load_epoch, std::uint64_t registry_epoch) noexcept {
    if (!node) return false;
    resolve_own_ship();
    if (!own_ship_node_) return false;
    own_cache_.bind(std::uint32_t(frame_), own_ship_node_, own_ship_handle_, load_epoch, registry_epoch);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_own_ship_set_) return own_cache_.own(node, handle, [this](std::uintptr_t n, std::uint32_t h) { return n == fixture_own_part_node_ && h == fixture_own_part_handle_; }); // the seam's nodes are synthetic: its one declared part stands for the parent walk
#endif
    return own_cache_.own(node, handle, [this](std::uintptr_t n, std::uint32_t) {
        const DWORD error = GetLastError();
        auto read = [](std::uintptr_t p, void* out, std::size_t bytes) { return engine_memory::read(p, out, bytes); };
        const bool own = object_capture::own_ship_descends(read, std::uint32_t(n), std::uint32_t(own_ship_node_), own_ship_handle_);
        SetLastError(error);
        return own;
    });
}
// One own-ship z-writing draw with a known extent: its AABB corners through
// its rows, the largest corner distance from the object origin in world units.
void MotionOutput::note_own_ship_draw(const shadow_replay::ExtentEntry& extent) noexcept {
    const UINT matrix_register = shadow_.vs_row ? shadow_.vs_row->matrix_register : shadow_.vs_prepass ? shadow_.vs_prepass->matrix_register : ~0u;
    const std::size_t window = matrix_register == ~0u ? motion_matrix_windows_max : window_of(matrix_register);
    if (window >= motion_matrix_windows_max || !shadow_.rows_known[window]) return;
    const float r = renderer::shadow_cascade_draw_radius(camera_scene_, shadow_.rows[window], extent.lo, extent.hi);
    if (r > own_radius_frame_) own_radius_frame_ = r;
    ++own_draws_frame_;
}
// The frame boundary: the previous frame's own ship and radius go through the
// hysteresis; a changed E0 replaces the set (cascade 0 re-anchored to its new
// texel grid, the ladder slid behind it: every cascade whose extent changed
// re-anchors its grid and voids its retained map, the others keep theirs; one
// pop per commit, and commits are a ship change or a > 20 % size change).
// Logged on every commit and on capture (F8) frames.
void MotionOutput::update_adaptive_cascades() noexcept {
    const std::uintptr_t node = own_ship_frame_ == frame_ ? own_ship_node_ : 0;
    const float radius = own_draws_frame_ ? own_radius_frame_ : 0.f;
    const char* reason = nullptr;
    const bool changed = renderer::shadow_cascade_adaptive_update(cascade_adaptive_, node, radius, cascade_adaptive_k_, cascade_ladder_ratio_, depth_cascade_base_, depth_cascades_, &reason);
    if (changed) {
        // Every cascade whose extent or active bit moved: its retained basis is void (a dropped-and-restored one must not republish
        // the map it held before the drop); the slid policies (caps, static-only mask) replace the attach-time ones on the draw path.
        if (depth_replay_) for (unsigned i = 0; i < depth_cascades_.count; ++i) if (cascade_adaptive_.changed >> i & 1u) depth_replay_->invalidate_retained(i);
        for (unsigned i = 0; i < renderer::shadow_cascade_max; ++i) depth_cascade_draw_caps_[i] = depth_cascades_.importance ? candidate_capacity_ : depth_cascades_.bound(i);
        depth_cascade_static_mask_ = depth_cascades_.static_only_mask();
    }
    if (reason || capture_) log_cascade_set(reason ? reason : "capture");
    own_radius_frame_ = 0.f; own_draws_frame_ = 0;
}
void MotionOutput::log_cascade_set(const char* reason) noexcept {
    const auto& c0 = depth_cascades_.cascades[0];
    unsigned slots[renderer::shadow_cascade_max]{}; const unsigned n = renderer::shadow_cascade_apply_slots(depth_cascades_, slots);
    char slot_text[16]; int used = 0;
    for (unsigned s = 0; s < n && used >= 0 && used < int(sizeof slot_text); ++s) used += std::snprintf(slot_text + used, sizeof slot_text - used, "%s%u", s ? "," : "", slots[s]);
    if (used < 0 || used >= int(sizeof slot_text)) slot_text[0] = 0;
    char extent_text[96]; used = 0; // the live set's extents, every configured cascade (a dropped one prints its slid extent; active_mask says which count)
    for (unsigned i = 0; i < depth_cascades_.count && used >= 0 && used < int(sizeof extent_text); ++i) used += std::snprintf(extent_text + used, sizeof extent_text - used, "%s%.9g", i ? "," : "", double(depth_cascades_.cascades[i].half_extent));
    if (used < 0 || used >= int(sizeof extent_text)) extent_text[0] = 0;
    char cap_text[64]; used = 0; // the slid policies: the live caps (0: a dropped cascade, idle), the first static-only cascade and the mover threshold
    for (unsigned i = 0; i < depth_cascades_.count && used >= 0 && used < int(sizeof cap_text); ++i) used += std::snprintf(cap_text + used, sizeof cap_text - used, "%s%u", i ? "," : "", depth_cascades_.caps[i]);
    if (used < 0 || used >= int(sizeof cap_text)) cap_text[0] = 0;
    char static_text[12]; std::snprintf(static_text, sizeof static_text, "%u", depth_cascades_.static_from < depth_cascades_.count ? depth_cascades_.static_from : 0u);
    log("shadow_cascade_set device=%llu frame=%llu reason=%s own_node=%p own_status=%u own_radius=%.9g e0=%.9g texel0=%.9g depth_behind0=%.9g active_mask=%u apply_slots=%s slid=%u changed=%u extents=%s caps=%s static_from=%s large_min=%.9g k=%.9g ratio=%.9g pending_radius=%.9g pending_frames=%u held_frames=%u"
        " frame_radius=%.9g own_draws=%u own_hits=%u own_walks=%u own_walk_deferred=%u own_flushes=%u",
        id_, frame_, reason, reinterpret_cast<void*>(cascade_adaptive_.node), own_ship_status_, double(cascade_adaptive_.radius), double(c0.half_extent),
        renderer::shadow_replay_world_texel(c0), double(c0.depth_behind), depth_cascades_.active, slot_text, cascade_adaptive_.slid, cascade_adaptive_.changed, extent_text, cap_text,
        depth_cascades_.static_from < depth_cascades_.count ? static_text : "none", double(depth_cascades_.large_min), double(cascade_adaptive_k_), double(cascade_ladder_ratio_),
        double(cascade_adaptive_.pending), cascade_adaptive_.pending_frames,
        cascade_adaptive_.held_frames, double(own_radius_frame_), own_draws_frame_, own_cache_.hits, own_cache_.walks, own_cache_.deferred, own_cache_.flushes);
}
