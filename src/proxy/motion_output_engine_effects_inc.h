// Engine effects, phase 1a (included by motion_output.cpp; engine_effects.h, engine_effects_core.h;
// docs/architecture/engine-effects-modern.md sections 1, 2 and 5): the glow-jet recogniser on the draw path, the
// frame's record ring and the --debug census rows.
//
// Per draw with the hook on: the effect-pair compare and the shadowed Z-write / blend states (no device call with the
// state hooks on; with the hooks off the route's per-draw cache reads each once, as for every other consumer). A
// candidate (the effects pair, or blending with Z-write off) reads the object scope (object_trace::current: the node
// block, no matrices); a node carrying node+0x130 & 0x4000001 is a glow jet. With off|plumes it is recorded (64 bytes:
// throttle from node+0x88, geometry from the c4-6 shadow, identity, body table entry) and not forwarded: the hook
// returns D3D_OK, which the engine never tests (0x004c403c), the lens-gain skip contract. Fail closed: no scope, an
// unreadable node, Z-write on or unknown, a full ring -> forwarded natively and counted. No string formatting unless
// the census is on.
void MotionOutput::configure_engine_effects(bool hook, bool suppress, bool census) noexcept {
    engine_hook_ = engine_suppress_ = engine_census_ = false;
    engine_partial_logged_ = engine_quiet_logged_ = false;
    if (!hook) return;
    if (!engine_ring_) engine_ring_ = new (std::nothrow) engine_effects::core::Ring;
    if (!engine_ring_) {
        log("engine_effects_device device=%llu hook=0 reason=allocation", id_);
        return;
    }
    engine_ring_->clear();
    engine_hook_ = true;
    engine_suppress_ = suppress;
    engine_redirects_ = engine_effects::redirects_live();
    engine_census_ = census;
}
void MotionOutput::engine_effects_frame_begin() noexcept {
    if (!engine_device_logged_) {
        engine_device_logged_ = true;
        log("engine_effects_device device=%llu frame=%llu hook=1 suppress=%u redirects=%u census=%u route=%u ring=%u record_bytes=%u",
            id_, frame_, unsigned(engine_suppress_), unsigned(engine_effects::redirects_live()), unsigned(engine_census_), unsigned(enabled_),
            engine_effects::core::ring_capacity, unsigned(sizeof(engine_effects::core::Record)));
    }
    engine_ring_->clear();
    engine_counts_ = {};
    engine_redirects_ = engine_effects::redirects_live(); // one call per frame; the redirects arm once at load
    // off|plumes with the redirects live but the motion route off on this device: the redirects (process-wide) hide
    // the engine's sprites and trails while before_draw returns before this hook, so the glow jets stay native. One
    // row each time the device enters that state.
    const bool partial = engine_suppress_ && engine_redirects_ && !enabled_;
    if (partial && !engine_partial_logged_)
        log("engine_effects_partial device=%llu frame=%llu route=off redirects=on suppress=1 glow=native sprites=off trails=off",
            id_, frame_);
    engine_partial_logged_ = partial;
    // plumes whose stage is not attached on this device (refused at attach or creation, failed until Reset, or within
    // the 64-frame disarm after a failed stage frame): this frame's recognised glow jets are forwarded natively
    // (forwarded_stage_off), the game's glow rather than nothing. Latched once per frame from the states the last
    // resolve left (a refusal found at this frame's resolve forwards from the next frame on).
    engine_stage_off_ = engine_plumes_stage_off();
    if (engine_rows_) ++engine_row_frames_; // the previous frame wrote rows: one of the first eight spent
    engine_rows_ = engine_rows_more_ = 0;
    // The plume stage's per-frame report (engine_stage row; motion_output_engine_plumes_inc.h).
    plumes_ran_ = plumes_fenced_ = plumes_evaluated_ = false;
    plumes_report_ = {};
    ribbons_report_ = {}; // phase 3: the ribbons' share of the engine_stage row
    plumes_stage_us_ = 0.f;
    plumes_view_rule_ = engine_plumes::ViewRule::none;
}
void MotionOutput::engine_effects_frame_end() noexcept {
    // Plumes requested on a frame with records whose resolve never asked for the stage: a device that cannot arm by
    // configuration (no --taa or --hdr, or the suppression off) says so once in engine_plumes_state; the glow stays
    // suppressed (the off look). Frames whose resolve was merely skipped show ran=0 in engine_stage instead.
    if (plumes_requested_ && !plumes_evaluated_ && engine_ring_->count) {
        if (!engine_redirects_) note_engine_plumes_state(false, "suppression_off");
        else if (!taa_enabled_ || !hdr_requested_) note_engine_plumes_state(false, "hdr_taa_path");
    }
    if (!engine_census_) return; // configure: census = the --debug tier
    const auto& c = engine_counts_;
    namespace ee = engine_effects::core;
    // A frame with a candidate writes its row; frames without one write at most one row per 300 frames.
    if (!c.candidates) {
        if (engine_quiet_logged_ && frame_ - engine_quiet_frame_ < engine_quiet_interval) return;
        engine_quiet_logged_ = true;
        engine_quiet_frame_ = frame_;
    }
    const auto stats = engine_effects::stats();
    if (log_tier::cached_debug)
        log("engine_frame device=%llu frame=%llu mode=%s candidates=%lu not_jet=%lu records=%u suppressed=%lu forwarded_unscoped=%lu forwarded_snapshot=%lu forwarded_opaque=%lu forwarded_state=%lu forwarded_overflow=%lu forwarded_native=%lu forwarded_patch_missing=%lu forwarded_stage_off=%lu redirects=%u unknown_body=%lu steering=%lu rows_unknown=%lu order_a=%lu order_b=%lu order_ambiguous=%lu order_mismatch=%lu order_invalid=%lu pinned=%s bodies=%u mapped=%u rows=%u rows_more=%u",
        id_, frame_, ee::mode_name(engine_effects::mode()), static_cast<unsigned long>(c.candidates),
        static_cast<unsigned long>(c.not_jet), engine_ring_->count, static_cast<unsigned long>(c.suppressed),
        static_cast<unsigned long>(c.forwarded[0]), static_cast<unsigned long>(c.forwarded[1]),
        static_cast<unsigned long>(c.forwarded[2]), static_cast<unsigned long>(c.forwarded[3]),
        static_cast<unsigned long>(c.forwarded[4]), static_cast<unsigned long>(c.forwarded[5]),
        static_cast<unsigned long>(c.forwarded[6]), static_cast<unsigned long>(c.forwarded[7]), unsigned(engine_redirects_),
        static_cast<unsigned long>(c.unknown_body),
        static_cast<unsigned long>(c.steering), static_cast<unsigned long>(c.rows_unknown),
        static_cast<unsigned long>(c.match[0]), static_cast<unsigned long>(c.match[1]),
        static_cast<unsigned long>(c.match[2]), static_cast<unsigned long>(c.match[3]),
        static_cast<unsigned long>(c.match[4]), engine_order_ == ee::Order::b ? "b" : "a", stats.bodies, stats.mapped,
        engine_rows_, engine_rows_more_);
    if (plumes_requested_) log_engine_stage(); // the plume stage's row at the same cadence
}
// The own-ship tag of a suppressed record: the jet node is the own ship's root (node and handle) or hangs directly
// under it (parent +0x18 = the root: every engine part's parent, docs/reverse-engineering/engine-effects.md section 4).
// The own ship is resolved once per frame (resolve_own_ship: the cockpit registry walk, LastError preserved); one
// bounded read of node+0x18 per record while an own ship exists (none in the fixture, whose node block is already read).
bool MotionOutput::engine_record_own(std::uintptr_t node, std::uint32_t handle, bool parent_known,
                                     std::uint32_t parent) noexcept {
    if (!node) return false;
    resolve_own_ship();
    if (!own_ship_node_) return false;
    if (node == own_ship_node_) return handle == own_ship_handle_;
    if (!parent_known) {
        const DWORD error = GetLastError();
        parent_known = engine_memory::read(node + 0x18, &parent, sizeof parent);
        SetLastError(error);
        if (!parent_known) return false;
    }
    return parent && std::uintptr_t(parent) == own_ship_node_;
}
// The ship's radius for the plume floor: the parent's (the root node's) +0xa4, the subtree radius the engine caches
// (docs/reverse-engineering/engine-effects.md, "Ship radius"; -1 while dirty). One bounded read (LastError preserved) per
// ship and frame: the jets of one root share it, so the last parent's answer stands for the next jets of the same parent
// in the frame. 0 when the parent is unknown, the read fails or the value is not positive (no floor for its jets).
std::int32_t MotionOutput::engine_parent_radius(std::uint32_t parent) noexcept {
    if (!parent) return 0;
    if (parent != engine_radius_parent_ || frame_ != engine_radius_frame_) {
        std::int32_t radius = 0;
        const DWORD error = GetLastError();
        const bool known = engine_memory::read(std::uintptr_t(parent) + engine_effects::core::parent_radius_offset, &radius,
                                               sizeof radius);
        SetLastError(error);
        engine_radius_parent_ = parent;
        engine_radius_frame_ = frame_;
        engine_radius_ = known && radius > 0 ? radius : 0;
    }
    return engine_radius_;
}
bool MotionOutput::engine_effects_draw(const MotionDrawCall& call, MotionRoute& route) noexcept {
    namespace ee = engine_effects::core;
    ee::DrawState st;
    st.pair = ee::effect_pair(shadow_.vs_hash, shadow_.ps_hash);
    // A draw of another pair outside an object scope is never a candidate (classify: none): decided by the scope's
    // presence alone (a TLS read, no memory read, no device call) before any render-state read. With the render-state
    // hooks off (the default hybrid unhook) state_known() is the route's per-draw cached Get: Z-write is the read
    // evaluate_draw makes for every tracked draw anyway; blending is read only for scoped Z-write-off draws.
    if (!st.pair) {
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        if (fixture_configured_) {
            if (!fixture_.scope.known || !fixture_.scope.node) return false;
        } else
#endif
        {
            std::uintptr_t descriptor = 0, node = 0;
            if (!object_trace::scope_node(&descriptor, &node) || !node) return false;
        }
    }
    st.zwrite_known = state_known(1); // D3DRS_ZWRITEENABLE (shadow_states[1])
    st.zwrite = shadow_.states[1];
    if (!st.pair && (!st.zwrite_known || st.zwrite)) return false; // every opaque draw ends here
    if (!st.pair || engine_census_) {
        st.blend_known = state_known(3); // D3DRS_ALPHABLENDENABLE (shadow_states[3]); the pair qualifies without it
        st.blend = shadow_.states[3];
    }
    if (!ee::candidate(st)) return false;
    ++engine_counts_.candidates;
    // The object scope: the node block of the innermost 0x004c0150 scope on this thread (bounded reads, no matrices).
    object_trace::Snapshot scope{};
    ee::Facts facts;
    facts.suppress = engine_suppress_;
    facts.redirects = engine_redirects_;
    facts.stage_off = engine_stage_off_;
    facts.ring_full = engine_ring_->full();
    std::uint64_t serial = 0;
    std::uint32_t scope_parent = 0; // node+0x18 from the scope's node block (the ship key, the own-ship tag)
    bool parent_known = false;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_configured_) {
        // The seam's scope: the fixture's node block read through engine_memory exactly as object_trace reads a node
        // (the same offsets), the serial from the configured lifetime.
        const auto& f = fixture_.scope;
        facts.scoped = f.known && f.node;
        if (facts.scoped) {
            std::uint32_t node[0x150 / 4]{};
            scope.node = f.node;
            scope.scope_depth = 1;
            if (engine_memory::read(f.node, node, sizeof node)) {
                scope.valid |= object_trace::Node;
                scope.node_handle = node[0x28 / 4];
                scope_parent = node[0x18 / 4];
                parent_known = true;
                scope.model = node[0x140 / 4];
                scope.flags130 = node[0x130 / 4];
                std::memcpy(scope.position, node + 0xb0 / 4, sizeof scope.position);
                scope.scale[0] = node[0x70 / 4];
                std::memcpy(scope.scale + 1, node + 0x80 / 4, 12);
                for (unsigned i = 0; i < 3; ++i) std::memcpy(scope.basis + i * 3, node + 0xc0 / 4 + i * 4, 12);
            }
            serial = f.node_serial;
            scope.camera_handle = f.camera_handle;
            if (f.camera_handle) scope.valid |= object_trace::Camera;
        }
    } else
#endif
    {
        facts.scoped = object_trace::current(&scope, false);
        // object_trace::current copies node+0x18 from the node block it reads (Snapshot::parent): no second read.
        parent_known = facts.scoped && (scope.valid & object_trace::Node);
        scope_parent = parent_known ? scope.parent : 0u;
    }
    facts.snapshot = facts.scoped && (scope.valid & object_trace::Node) && scope.node;
    facts.flags130 = scope.flags130;
    const ee::Verdict verdict = ee::classify(st, facts);
    if (verdict == ee::Verdict::none) return false;
    if (verdict == ee::Verdict::not_jet) {
        ++engine_counts_.not_jet;
        return false;
    }
    const bool jet = facts.snapshot && verdict != ee::Verdict::snapshot;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (!fixture_configured_)
#endif
        if (jet && object_lifetime::active() &&
            (scope.valid & (object_trace::Camera | object_trace::Registry)) == (object_trace::Camera | object_trace::Registry)) {
            object_lifetime::Snapshot life{};
            if (object_lifetime::current(scope.registry, scope.node, scope.node_handle, scope.camera, scope.camera_handle, &life) &&
                life.known) {
                serial = life.node_serial;
                engine_load_epoch_ = life.load_epoch; // phase 3: a change clears the ribbon pool (a load)
            }
        }
    // The record (built for a suppressed draw, and for the census row of a forwarded JET draw).
    ee::Record record{};
    ee::Geometry geometry{};
    const ee::Body* entry = nullptr;
    int body = -1;
    if (jet) {
        body = engine_effects::body_for_model(scope.model);
        entry = engine_effects::body(body);
        ee::RecordInput in;
        in.scale3 = scope.scale[3];
        in.model = scope.model;
        in.node_handle = scope.node_handle;
        in.serial = serial;
        in.rows = st.pair && shadow_.world46_known == 7 ? shadow_.world46 : nullptr;
        in.pair = st.pair;
        in.additive = blend_known(1) && shadow_.composition_blend[1] == D3DBLEND_ONE;
        in.body = body;
        in.entry = entry;
        in.frame = frame_;
        // Only a suppressed draw pins the order and enters the counts (a forwarded one is a census witness).
        ee::Order pinned = engine_order_;
        ee::fill_record(in, verdict == ee::Verdict::suppressed ? &engine_order_ : &pinned, &record, &geometry,
                        verdict == ee::Verdict::suppressed ? &engine_counts_ : nullptr);
    }
    if (verdict == ee::Verdict::suppressed) {
        const unsigned slot = engine_ring_->count;
        *engine_ring_->push() = record; // not full: classify saw room
        // The view tags (the plume stage draws the scene view's records only): the scope's camera handle and the
        // scene phase.
        engine_ring_->camera[slot] = (scope.valid & object_trace::Camera) ? scope.camera_handle : 0u;
        engine_ring_->scene[slot] = selector_.state() == renderer::BoundaryState::Scene ? 1u : 0u;
        // The ship key (the plume stage's sub-engine floor) and the own-ship tag take the same field: node+0x18, the
        // parent (the ship's root node), copied from the scope's node block (object_trace::current; the fixture's seam
        // reads the same offsets); 0 when the block was unreadable.
        engine_ring_->parent[slot] = parent_known ? scope_parent : 0u;
        // The ship's radius in the record's units (the plume floor): the parent's +0xa4 against the jet's own +0x70 and
        // +0x80 from the same node block.
        ee::parent_radius_in_record(parent_known ? engine_parent_radius(scope_parent) : 0, scope.scale[0], scope.scale[1],
                                    record.size, &engine_ring_->parent_radius[slot]);
        engine_ring_->own[slot] = engine_record_own(scope.node, scope.node_handle, parent_known, scope_parent) ? 1u : 0u;
        ++engine_counts_.suppressed;
        if (!entry) ++engine_counts_.unknown_body;
        if (record.flags & ee::flag_steering) ++engine_counts_.steering;
        route.submit = false; // the lens-gain skip contract: not forwarded, the game's call returns D3D_OK
        route.submission_error = D3D_OK;
    } else {
        const unsigned reason = ee::forward_index(verdict);
        if (reason < ee::forward_reasons) ++engine_counts_.forwarded[reason];
    }
    if (engine_census_) {
        // The first eight frames with rows, and every F8 capture frame (the own ship's z in the log of each capture).
        if (engine_row_frames_ >= engine_row_frame_cap && !capture_) {
        } else if (engine_rows_ >= engine_row_cap)
            ++engine_rows_more_;
        else {
            ++engine_rows_;
            char name[40] = "-";
            if (entry) {
                unsigned n = 0;
                for (; entry->name[n] && n + 1 < sizeof name; ++n)
                    name[n] = entry->name[n] > 0x20 && entry->name[n] < 0x7f ? entry->name[n] : '?';
                name[n] = 0;
            }
            // The node basis z row (16.16) against both c4-6 orders' unit z: the cross-check of the register order.
            float bz[3] = {float(std::int32_t(scope.basis[6])) * (1.f / 65536.f),
                           float(std::int32_t(scope.basis[7])) * (1.f / 65536.f),
                           float(std::int32_t(scope.basis[8])) * (1.f / 65536.f)};
            float bl = 0.f;
            ee::length3(bz, &bl);
            if (bl > 0.f)
                for (float& v : bz) v /= bl;
            const float cos_a = geometry.z_a[0] * bz[0] + geometry.z_a[1] * bz[1] + geometry.z_a[2] * bz[2];
            const float cos_b = geometry.z_b[0] * bz[0] + geometry.z_b[1] * bz[1] + geometry.z_b[2] * bz[2];
            const long src = blend_known(0) ? long(shadow_.composition_blend[0]) : -1;
            const long dst = blend_known(1) ? long(shadow_.composition_blend[1]) : -1;
            log("engine_draw device=%llu frame=%llu index=%lu vs=%016llx ps=%016llx pair=%s primitives=%u flags130=%08lx model=%lu name=%s body=%d cluster=%s z=%.5f s=%.5f order=%s ratio_a=%.5f ratio_b=%.5f size=%.6g origin=%.6g,%.6g,%.6g axis=%.5f,%.5f,%.5f za_basis=%.5f zb_basis=%.5f basis_z=%.5f,%.5f,%.5f position=%ld,%ld,%ld blend=%ld src=%ld dst=%ld zwrite=%ld scope_depth=%lu node=%08lx handle=%08lx serial=%llu flags=%04x verdict=%s",
                id_, frame_, static_cast<unsigned long>(counters_.draws), static_cast<unsigned long long>(shadow_.vs_hash),
                static_cast<unsigned long long>(shadow_.ps_hash), st.pair ? "effect" : "other", call.primitives,
                static_cast<unsigned long>(scope.flags130), static_cast<unsigned long>(scope.model), name, jet ? body : -1,
                jet ? ee::cluster_name(record.flags >> ee::cluster_shift) : "-", double(record.z), double(record.s),
                jet ? ee::match_name(geometry.match) : "-", double(geometry.ratio_a), double(geometry.ratio_b),
                double(record.size), double(record.origin[0]), double(record.origin[1]), double(record.origin[2]),
                double(record.axis[0]), double(record.axis[1]), double(record.axis[2]), double(cos_a), double(cos_b),
                double(bz[0]), double(bz[1]), double(bz[2]), static_cast<long>(std::int32_t(scope.position[0])),
                static_cast<long>(std::int32_t(scope.position[1])), static_cast<long>(std::int32_t(scope.position[2])),
                st.blend_known ? long(st.blend) : -1l, src, dst, st.zwrite_known ? long(st.zwrite) : -1l,
                static_cast<unsigned long>(scope.scope_depth), static_cast<unsigned long>(scope.node),
                static_cast<unsigned long>(scope.node_handle), static_cast<unsigned long long>(serial),
                unsigned(record.flags), ee::verdict_name(verdict));
        }
    }
    return verdict == ee::Verdict::suppressed;
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// 0 candidates, 1 not_jet, 2 records (ring), 3 suppressed, 4..10 forwarded (unscoped, snapshot, opaque, state,
// overflow, native, patch_missing), 11 unknown_body, 12 steering, 13 rows_unknown, 14..18 order a/b/ambiguous/mismatch/
// invalid, 19 hook, 20 suppress, 21 the c4-6 known mask, 22 pinned order (1 = b), 23 census rows this frame,
// 24 redirects live (this frame's cached signal); the plume stage (this frame's report until the next frame begins):
// 25 armed, 26 ran, 27 the stage's result, 28 nozzles, 29 skipped_other_view, 30 drew, 31 the pass's references,
// 32 taa_references, 33 consecutive failures, 34 refused until Reset, 35 the record's camera tag of index 0, 36 its
// scene tag, 37 forwarded_stage_off (this frame), 38 the stage-off latch, 39 the plume pass refused at attach, 40..43
// the parent radius of records 0..3 in record units (float bits; 0 unknown).
unsigned MotionOutput::fixture_engine_status(unsigned key) const noexcept {
    const auto& c = engine_counts_;
    if (key == 0) return c.candidates;
    if (key == 1) return c.not_jet;
    if (key == 2) return engine_ring_ ? engine_ring_->count : 0u;
    if (key == 3) return c.suppressed;
    if (key >= 4 && key < 11) return c.forwarded[key - 4];
    if (key == 11) return c.unknown_body;
    if (key == 12) return c.steering;
    if (key == 13) return c.rows_unknown;
    if (key >= 14 && key < 19) return c.match[key - 14];
    if (key == 19) return engine_hook_ ? 1u : 0u;
    if (key == 20) return engine_suppress_ ? 1u : 0u;
    if (key == 21) return shadow_.world46_known;
    if (key == 22) return engine_order_ == engine_effects::core::Order::b ? 1u : 0u;
    if (key == 23) return engine_rows_;
    if (key == 24) return engine_redirects_ ? 1u : 0u;
    if (key == 25) return plumes_armed_ ? 1u : 0u;
    if (key == 26) return plumes_ran_ ? 1u : 0u;
    if (key == 27) return unsigned(plumes_report_.operation);
    if (key == 28) return plumes_report_.stats.nozzles;
    if (key == 29) return plumes_report_.stats.skipped_other_view;
    if (key == 30) return plumes_report_.drew ? 1u : 0u;
    if (key == 31) return plumes_ ? plumes_->references() : 0u;
    if (key == 32) return taa_references_;
    if (key == 33) return plumes_failures_;
    if (key == 34) return plumes_failed_out_ ? 1u : 0u;
    if (key == 35) return engine_ring_ && engine_ring_->count ? engine_ring_->camera[0] : 0u;
    if (key == 36) return engine_ring_ && engine_ring_->count ? engine_ring_->scene[0] : 0u;
    if (key == 37) return c.forwarded[7];
    if (key == 38) return engine_stage_off_ ? 1u : 0u;
    if (key == 39) return plumes_attach_failed_ ? 1u : 0u;
    if (key >= 40 && key < 44) {
        std::uint32_t bits = 0;
        if (engine_ring_ && key - 40u < engine_ring_->count) std::memcpy(&bits, &engine_ring_->parent_radius[key - 40u], 4);
        return bits;
    }
    return 0;
}
bool MotionOutput::fixture_plumes_fault(unsigned faults) noexcept {
    if (!plumes_) plumes_.reset(new (std::nothrow) renderer::EnginePlumesPass); // attached at the next arming
    if (!plumes_) return false;
    plumes_->set_faults(faults);
    return true;
}
bool MotionOutput::fixture_engine_record(unsigned index, void* out, unsigned size) const noexcept {
    if (!engine_ring_ || !out || size != sizeof(engine_effects::core::Record) || index >= engine_ring_->count) return false;
    std::memcpy(out, &engine_ring_->records[index], size);
    return true;
}
#endif
