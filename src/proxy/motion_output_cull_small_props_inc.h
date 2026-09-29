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
// engine read and one class-cache probe; a prop draw adds two reads and eight
// corner transforms; a prop below the threshold adds, once per frame, the
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
bool MotionOutput::cull_small_prop(MotionRoute& route) {
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
    const auto verdict = c.evaluate(read, props::Addresses{}, std::uint32_t(node), std::uint32_t(descriptor), rows,
                                    target_width_, target_height_, &first);
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
