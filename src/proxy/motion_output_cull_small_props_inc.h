// ---- small-prop cull (X3M_CULL_SMALL_PROPS=on; cull_small_props_core.h) ----
//
// Included by motion_output.cpp inside namespace x3m. A main-scene draw (gate 2
// passed, before the jitter and every other binding) whose scope node is a
// ships\props\ body below X3M_CULL_SMALL_PARTS_PX pixels of projected radius,
// and not on the player's ship or its target, is not forwarded: the hook
// returns D3D_OK and nothing was set or bound, so nothing is undone. The
// engine's state is never read back or written: the cull/LOD pass, the
// renderable bit, the script occluder list and the simulation run as vanilla.
// Per scene draw with the option on: one thread-local scope read (no engine
// read) and one memo probe; the first draw of a node in a frame adds one 4-byte
// engine read and one class-cache probe; a prop draw adds the bookend view of
// its vertex buffer, one extent-cache probe (a missing extent is queued for the
// scene-end read the shadow replay already runs) and eight corner transforms;
// a prop below the threshold adds, once per frame, the
// own-ship/target resolution and, once per node per 256 frames, a bounded
// parent walk. No allocation after the first scene draw, no draw call (the hook's own Get*/restore work still runs, as on the bolt drop path).

// The decision state, committed once at the first scene draw with the option on (kept out of line so the per-draw
// function carries no allocation and no exception registration).
__attribute__((noinline)) bool MotionOutput::attach_small_props() noexcept {
    if (props_attach_failed_) return false;
    props_ = new (std::nothrow) cull_small_props::core::Culler();
    if (!props_) {
        props_attach_failed_ = true;
        props_on_ = false;
        log("cull_small_props_device device=%llu attached=0 reason=out_of_memory", id_);
        return false;
    }
    props_->px = props_px_;
    log("cull_small_props_device device=%llu attached=1 px=%.4f executable=%u", id_, double(props_px_),
        object_trace::executable_verified() ? 1u : 0u);
    return true;
}
// The object-space AABB of this draw's POSITION0 range from the shadow-replay extent cache (the box the
// object_bounds rows project; run375/376: turret draws 0.85-2.1 px), or null: the candidate reads are off
// (X3M_SHADOW_REPLAY_DEPTH / the candidate counter), the buffer is not a managed one with a known bookend view, the
// range or position type is not readable, or the extent is not known yet (then queued: read at this frame's scene
// end even though the draw is skipped, so a skipped prop keeps its extent). A stale extent of an earlier revision of
// the same range stands in while it is young, as in the caster verdict.
const cull_small_props::core::Box* MotionOutput::small_prop_extent(const MotionDrawCall& call,
                                                                   cull_small_props::core::Box& out,
                                                                   bool allow_stale) noexcept {
    if (!candidates_requested_ || call.user_memory || !shadow_.stream0 || !shadow_.stream0_stride ||
        (call.indexed && !shadow_.indices) || shadow_.stream0_pool != shadow_replay::PoolClass::Managed)
        return nullptr;
    ownership::BufferLockView vb{};
    if (!shadow_.stream0_identity ||
        FAILED(ownership::get_buffer_lock_view_light(reinterpret_cast<IDirect3DResource9*>(shadow_.stream0_identity),
                                                     &vb)) ||
        !vb.known)
        return nullptr;
    shadow_replay::ExtentKey key{};
    key.vb = shadow_.stream0;
    key.revision = vb.revision;
    key.stream_offset = shadow_.stream0_offset;
    key.stride = shadow_.stream0_stride;
    key.position_offset = shadow_.position_offset;
    key.position_type = shadow_.position_type;
    const std::int64_t first = call.indexed ? std::int64_t(call.base_vertex) + call.min_vertex : std::int64_t(call.first);
    const std::uint32_t count = call.indexed ? call.vertex_count : shadow_replay::vertices_of(call.topology, call.primitives);
    if (first < 0 || first > 0xFFFFFFFFll || !count || !shadow_replay::extent_type_supported(key.position_type) ||
        key.position_offset + shadow_replay::extent_type_bytes(key.position_type) > key.stride)
        return nullptr;
    key.first = std::uint32_t(first);
    key.count = count;
    const shadow_replay::ExtentEntry* stale = nullptr;
    const shadow_replay::ExtentEntry* e = candidate_extents_.find(key, &stale);
    if (!e && !(stale && stale->abandoned())) queue_candidate_extent(key, shadow_.stream0_identity, stale != nullptr);
    if (!e && allow_stale && stale && !stale->abandoned() &&
        candidate_extents_.stale_age(*stale) <= shadow_replay::extent_stale_frames)
        e = stale;
    if (!e || e->state != shadow_replay::ExtentState::Known) return nullptr;
    for (unsigned i = 0; i < 3; ++i) {
        out.lo[i] = e->lo[i];
        out.hi[i] = e->hi[i];
    }
    return &out;
}
bool MotionOutput::cull_small_prop(const MotionDrawCall& call, MotionRoute& route) {
    namespace props = cull_small_props::core;
    if (!props_ && !attach_small_props()) return false;
    auto& c = *props_;
    const std::uint32_t frame = std::uint32_t(frame_);
    if (!c.frame_started || c.frame != frame) c.begin_frame(frame);
    std::uintptr_t descriptor = 0, node = 0;
    if (!object_trace::executable_verified() || !object_trace::scope_node(&descriptor, &node)) {
        c.count(props::Verdict::no_scope);
        return false;
    }
    // The draw's own clip rows (the object_bounds convention): none for a VS without a profile row.
    const UINT matrix_register = shadow_.vs_row       ? shadow_.vs_row->matrix_register
                                 : shadow_.vs_prepass ? shadow_.vs_prepass->matrix_register
                                                      : ~0u;
    const std::size_t window = matrix_register == ~0u ? motion_matrix_windows_max : window_of(matrix_register);
    const float* rows = window < motion_matrix_windows_max && shadow_.rows_known[window] ? shadow_.rows[window] : nullptr;
    const DWORD error = GetLastError();
    auto read = [](std::uintptr_t p, void* out, std::size_t n) { return engine_memory::read(p, out, n); };
    bool first = false;
    props::Box extent_box{};
    const props::Box* extent = nullptr;
    const auto extent_of = [&]() noexcept { return extent = small_prop_extent(call, extent_box); };
    const auto verdict = c.evaluate(read, props::Addresses{}, std::uint32_t(node), extent_of, rows, target_width_,
                                    target_height_, &first);
    // F8 frames: one row per prop node decided this frame (capped), the vertex-extent radius the decision used beside
    // the engine's mesh-part box radius (diagnostic; the run376 gap).
    if (capture_ && first && props::prop_verdict(verdict) && small_prop_rows_frame_ != frame_) {
        small_prop_rows_frame_ = frame_;
        small_prop_rows_ = 0;
    }
    if (capture_ && first && props::prop_verdict(verdict) && small_prop_rows_ < 128) {
        ++small_prop_rows_;
        float vb_px = -1.f, part_px = -1.f;
        props::Box part{};
        if (extent && rows) props::screen_radius(rows, *extent, target_width_, target_height_, &vb_px);
        if (rows && props::Culler::engine_part_box(read, std::uint32_t(descriptor), part))
            props::screen_radius(rows, part, target_width_, target_height_, &part_px);
        log("cull_small_prop_box device=%llu frame=%llu index=%lu node=%08lx verdict=%s extent_px=%.3f part_px=%.3f extent_lo=%g,%g,%g extent_hi=%g,%g,%g part_lo=%g,%g,%g part_hi=%g,%g,%g",
            id_, frame_, static_cast<unsigned long>(counters_.draws), static_cast<unsigned long>(node),
            props::verdict_name(verdict), double(vb_px), double(part_px), extent ? double(extent->lo[0]) : 0.,
            extent ? double(extent->lo[1]) : 0., extent ? double(extent->lo[2]) : 0., extent ? double(extent->hi[0]) : 0.,
            extent ? double(extent->hi[1]) : 0., extent ? double(extent->hi[2]) : 0., double(part.lo[0]),
            double(part.lo[1]), double(part.lo[2]), double(part.hi[0]), double(part.hi[1]), double(part.hi[2]));
    }
    SetLastError(error);
    if (verdict != props::Verdict::culled) return false;
    if (first) cull_census::note_culled_prop(std::uint32_t(node)); // captured frames only (the census filters)
    route.submit = false;
    route.submission_error = D3D_OK;
    route.sun_color_writer = false; // not a depth writer: no stamp, no untracked-writer bookkeeping
    route.sun_stamp = false;
    route.unmatched = UnmatchedReason::None;
    if (capture_ && first)
        log("cull_small_prop device=%llu frame=%llu index=%lu node=%08lx descriptor=%08lx", id_, frame_,
            static_cast<unsigned long>(counters_.draws), static_cast<unsigned long>(node),
            static_cast<unsigned long>(descriptor));
    return true;
}
// Every 300 frames with scene draws while the option is on (every tier: the plain flight's evidence that the cull
// acted; about 330 B per row, inferred): prop draws seen, skipped and drawn, and why each drawn one was kept. The
// model-class cache is flushed with the row (a body id reused after a reload is re-read within 300 frames).
void MotionOutput::log_cull_small_props_window() noexcept {
    auto& w = props_->window;
    log("cull_small_props_frame device=%llu frame=%llu frames=%u px=%.4f draws=%u culled=%u kept=%u nodes_culled=%u kept_size=%u exempt_own=%u exempt_target=%u unresolved=%u deferred=%u no_bounds=%u unbounded=%u no_scope=%u resolves=%u walks=%u",
        id_, frame_, w.frames, double(props_->px), w.draws, w.culled, w.kept, w.nodes_culled, w.kept_size,
        w.exempt_own, w.exempt_target, w.unresolved, w.deferred, w.no_bounds, w.unbounded, w.no_scope, w.resolves,
        w.walks);
    props_->reset_window();
    props_->flush_classes();
}
