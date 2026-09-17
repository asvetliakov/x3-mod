// ---- own-ship-adaptive cascade 0 (shadow-cascade-extents.md, section 5) ----
//
// X3M_SHADOW_CASCADE_ADAPTIVE_C0 = k (default off). Included by
// motion_output.cpp inside namespace x3m. Off: no code runs (every site tests
// cascade_adaptive_on()). On, per frame: the own ship is resolved once at the
// frame's first candidate draw (six to eight bounded engine reads through the
// registry walk the chase camera uses: active cockpit, ref object, root node),
// each z-writing draw with a known extent costs one pointer compare (a scope
// node other than the root: one cache probe; a cache miss walks the parent
// links once per own_node_cache_frames), and an own-ship draw adds eight
// corner transforms. The commit runs at the frame boundary (begin_frame) so a
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
// under the ship's root node). The verdict is cached per node while the root
// is the same, re-walked every own_node_cache_frames frames.
bool MotionOutput::own_ship_draw(std::uintptr_t node) noexcept {
    if (!node) return false;
    resolve_own_ship();
    if (!own_ship_node_) return false;
    if (node == own_ship_node_) return true;
    auto& e = own_nodes_[(node >> 4) % own_node_cache_size];
    const std::uint32_t now = std::uint32_t(frame_);
    if (e.node == node && e.root == own_ship_node_ && now - e.stamp < own_node_cache_frames) return e.own;
    e.node = node; e.root = own_ship_node_; e.stamp = now; e.own = false;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_own_ship_set_) return false; // the seam's nodes are synthetic: no parent links to walk
#endif
    const DWORD error = GetLastError();
    auto read = [](std::uintptr_t p, void* out, std::size_t n) { return engine_memory::read(p, out, n); };
    e.own = object_capture::own_ship_descends(read, std::uint32_t(node), std::uint32_t(own_ship_node_), own_ship_handle_);
    SetLastError(error);
    return e.own;
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
// texel grid, the ratio guard recomputed) and voids cascade 0's retained map
// only. Logged on every commit and on capture (F8) frames.
void MotionOutput::update_adaptive_cascades() noexcept {
    const std::uintptr_t node = own_ship_frame_ == frame_ ? own_ship_node_ : 0;
    const float radius = own_draws_frame_ ? own_radius_frame_ : 0.f;
    const char* reason = nullptr;
    const bool changed = renderer::shadow_cascade_adaptive_update(cascade_adaptive_, node, radius, cascade_adaptive_k_, depth_cascade_base_, depth_cascades_, &reason);
    if (changed && depth_replay_) depth_replay_->invalidate_retained(0);
    if (reason || capture_) log_cascade_set(reason ? reason : "capture");
    own_radius_frame_ = 0.f; own_draws_frame_ = 0;
}
void MotionOutput::log_cascade_set(const char* reason) noexcept {
    const auto& c0 = depth_cascades_.cascades[0];
    log("shadow_cascade_set device=%llu frame=%llu reason=%s own_node=%p own_status=%u own_radius=%.9g e0=%.9g texel0=%.9g depth_behind0=%.9g active_mask=%u k=%.9g pending_radius=%.9g pending_frames=%u",
        id_, frame_, reason, reinterpret_cast<void*>(cascade_adaptive_.node), own_ship_status_, double(cascade_adaptive_.radius), double(c0.half_extent),
        renderer::shadow_replay_world_texel(c0), double(c0.depth_behind), depth_cascades_.active, double(cascade_adaptive_k_), double(cascade_adaptive_.pending), cascade_adaptive_.pending_frames);
}
