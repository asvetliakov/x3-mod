// ---- sun-shadow caster retention (shadow_retention_core.h) -----------------------
//
// docs/architecture/shadow-caster-retention.md, stages 1 (census,
// X3M_SHADOW_RETENTION_CENSUS=1) and 2 (static retention live,
// X3M_SHADOW_CASTER_RETENTION=1); cascades only. Included by motion_output.cpp
// inside namespace x3m. Off: retention_ is null and no code runs (every site
// tests it). On, per recorded caster draw: one class mask test, one node probe,
// a 64-byte rows copy; a new record in live mode takes the store's own AddRef
// on its VB, IB and declaration (one native reference per distinct resource).
// At the scene end, before the replay: the journal drain, the sun flush, the
// orphan probe slice, the seen nodes' classification and the unseen walk; the
// cascade transaction then issues the admitted records behind the live ones.
// Every Release the store owes happens here at the scene end, at a frame
// begin or in a flush, where the depth leases retire today. No allocation
// after attach. LastError is kept.
namespace {
constexpr unsigned retention_create_vertex_buffer_slot = 26; // IDirect3DDevice9::CreateVertexBuffer
using RetentionCreateVbFn = HRESULT(WINAPI*)(IDirect3DDevice9*, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*);
inline std::int64_t retention_ticks() noexcept { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double retention_us(std::int64_t ticks) noexcept {
    static const std::int64_t frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    return frequency ? double(ticks) * 1e6 / double(frequency) : 0.;
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// The seam's synthetic lifetime observer: the journal's semantics
// (object_lifetime.h) on the fixture's synthetic nodes. Absent from production.
namespace retention_fixture {
using namespace object_lifetime;
struct Birth { std::uint32_t handle = 0; std::uint64_t serial = 0; bool alive = false; };
JournalEntry ring[JournalCapacity]; std::uint64_t head = 0; unsigned consumers = 0; bool available = true, camera_dead = false;
std::uint64_t load_epoch = 1, registry_epoch = 1, revision = 0;
Birth births[4096]; unsigned birth_count = 0;
void append(JournalKind kind, std::uint64_t serial) noexcept {
    ++revision;
    if (!consumers) return;
    auto& e = ring[unsigned(head) & (JournalCapacity - 1)];
    e = {}; e.kind = kind; e.load_epoch = load_epoch; e.registry_epoch = registry_epoch; e.node_serial = serial; ++head;
}
JournalCursor journal_register() { if (!consumers) head += JournalCapacity + 1; ++consumers; return {head}; }
void journal_unregister() { if (consumers) --consumers; }
JournalDrain journal_drain(JournalCursor& cursor, JournalEntry* out, std::uint32_t limit) {
    JournalDrain result{};
    result.available = available && consumers && cursor.valid();
    result.load_epoch = load_epoch; result.registry_epoch = registry_epoch; result.mutation_revision = revision;
    if (!out || !limit) { result.invalid = true; return result; }
    if (!cursor.valid()) { result.overflow = true; return result; }
    if (!consumers || cursor.sequence > head || head - cursor.sequence > JournalCapacity) { result.overflow = true; cursor.sequence = head; return result; }
    const std::uint64_t waiting = head - cursor.sequence;
    const std::uint32_t count = waiting < limit ? std::uint32_t(waiting) : limit;
    for (std::uint32_t i = 0; i < count; ++i) out[i] = ring[unsigned(cursor.sequence + i) & (JournalCapacity - 1)];
    cursor.sequence += count; result.count = count; result.more = cursor.sequence != head;
    return result;
}
bool current(std::uintptr_t, std::uintptr_t, std::uint32_t node_handle, std::uintptr_t, std::uint32_t, Snapshot* out) {
    *out = {}; out->load_epoch = load_epoch; out->registry_epoch = registry_epoch; out->mutation_revision = revision;
    if (!available) { out->reason = Reason::Disabled; return false; }
    if (camera_dead) { out->reason = Reason::UnknownCameraBirth; return false; } // op 9: the recorded camera is gone
    for (unsigned i = 0; i < birth_count; ++i) if (births[i].handle == node_handle && births[i].alive) { out->known = true; out->reason = Reason::Known; out->node_serial = births[i].serial; out->camera_serial = 1; return true; }
    out->reason = Reason::UnknownNodeBirth; return false;
}
} // namespace retention_fixture
#endif
} // namespace
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Seam: 0 birth(handle, serial), 1 retire(serial) through the journal, 2 kill(serial) with no entry,
// 3 epochs(load, registry), 4 available(flag), 5 FlushAll, 6 `a` retirements of unknown serials, 7 reset,
// 8 device loss (handled by the export: every hooked device), 9 camera_dead(flag).
void shadow_retention_fixture_lifetime(unsigned op, std::uint64_t a, std::uint64_t b) noexcept {
    namespace fx = retention_fixture;
    const auto kill = [](std::uint64_t serial) { for (unsigned i = 0; i < fx::birth_count; ++i) if (fx::births[i].serial == serial) fx::births[i].alive = false; };
    switch (op) {
    case 0: if (fx::birth_count < 4096) fx::births[fx::birth_count++] = {std::uint32_t(a), b, true}; ++fx::revision; break;
    case 1: kill(a); fx::append(object_lifetime::JournalKind::Retired, a); break;
    case 2: kill(a); ++fx::revision; break;
    case 3: fx::load_epoch = a; fx::registry_epoch = b; break;
    case 4: fx::available = a != 0; break;
    case 5: fx::append(object_lifetime::JournalKind::FlushAll, 0); break;
    case 6: for (std::uint64_t i = 0; i < a; ++i) fx::append(object_lifetime::JournalKind::Retired, 0xFFFF000000000000ull + i); break;
    case 9: fx::camera_dead = a != 0; break; // the camera of every recorded scope is unknown to the observer (a lost context, not a death)
    default: fx::birth_count = 0; fx::available = true; fx::camera_dead = false; fx::load_epoch = fx::registry_epoch = 1; break;
    }
}
#endif

// Once per device, after the cascades are settled: the store, the draw list
// and the orphan probe's capability test. Refused (logged, feature off for
// the device) without cascades, without the lifetime observer, or on an
// allocation failure.
void MotionOutput::attach_shadow_retention() noexcept {
    detach_shadow_retention();
    if (retention_mode_ == shadow_retention::Mode::Off) return;
    const bool live = retention_mode_ == shadow_retention::Mode::Live;
    const char* reason = "ok";
    bool ok = true;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    const bool observer = true; // the seam's scope and observer are synthetic
#else
    const bool observer = object_trace::active() && object_lifetime::active(); // the executable gate and a known lifetime snapshot
#endif
    if (!depth_cascades_on()) { reason = "cascades"; ok = false; }
    else if (!observer) { reason = "lifetime"; ok = false; }
    if (ok) {
        retention_.reset(new (std::nothrow) ShadowRetention);
        if (retention_ && live) {
            retention_->draw_capacity = candidate_capacity_ + shadow_retention::draw_capacity; // the record list's capacity (attach_candidate_storage ran first)
            retention_->draws.reset(new (std::nothrow) renderer::ShadowReplayDraw[retention_->draw_capacity]);
            if (!retention_->draws) retention_.reset();
        }
        if (!retention_) reason = "allocation";
    }
    if (retention_) {
        auto& st = *retention_;
        st.mode = retention_mode_; st.timing = retention_timing_; st.store.configure(live);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        st.lifetime = {retention_fixture::journal_register, retention_fixture::journal_unregister, retention_fixture::journal_drain, retention_fixture::current};
#else
        st.lifetime = {object_lifetime::journal_register, object_lifetime::journal_unregister, object_lifetime::journal_drain, object_lifetime::current};
#endif
        // Registration: a fresh cursor says nothing about earlier retirements; the store is empty, so the full revalidation is vacuous.
        const object_lifetime::JournalCursor registered = st.lifetime.journal_register();
        st.cursor = registered.sequence; st.registered = registered.valid();
        if (!st.registered) { retention_.reset(); reason = "journal"; }
    }
    if (retention_ && live) {
        // The advisory orphan probe reads the documented AddRef/Release return
        // values; whether this runtime reports counts is tested once on a
        // private managed buffer (2 after AddRef, 1 after Release).
        const DWORD error = GetLastError();
        IDirect3DVertexBuffer9* probe = nullptr;
        if (SUCCEEDED(native<RetentionCreateVbFn>(retention_create_vertex_buffer_slot)(device_, 64, 0, 0, D3DPOOL_MANAGED, &probe, nullptr)) && probe) {
            const ULONG up = probe->AddRef(), down = probe->Release();
            retention_->orphan_probe = up == 2 && down == 1;
            probe->Release();
        }
        SetLastError(error);
    }
    log("shadow_retention_device device=%llu mode=%s enabled=%u reason=%s age_cap=%u eps=%.9g orphan_probe=%u nodes=%u records=%u resources=%u bytes=%u",
        id_, live ? "live" : "census", retention_ != nullptr, reason, unsigned(retention_age_cap_), retention_eps_, retention_ ? unsigned(retention_->orphan_probe) : 0u,
        shadow_retention::node_capacity, shadow_retention::draw_capacity, shadow_retention::resource_capacity, unsigned(sizeof(ShadowRetention)));
}
void MotionOutput::detach_shadow_retention() noexcept {
    if (!retention_) return;
    flush_shadow_retention(shadow_retention::Flush::Teardown);
    if (retention_->registered) retention_->lifetime.journal_unregister();
    retention_.reset();
}
void MotionOutput::release_retention_pending() noexcept {
    auto& store = retention_->store;
    if (!store.pending_count) return;
    const DWORD error = GetLastError();
    // The queue is the store's resource table (pop_owed): no owed reference can be dropped.
    // A final Release of a resource the application already released drops the
    // device reference it pinned and re-enters the device Release hook through
    // the wrapper's parent release: the accounting is held busy meanwhile (as
    // taa_call and release_resources do), so the hook's final-Release probe
    // cannot match on a count in motion and nothing tears this object down
    // under the loop. Each entry leaves the list before its Release.
    const bool busy = taa_busy_; taa_busy_ = true;
    while (const std::uintptr_t identity = store.pop_owed()) reinterpret_cast<IUnknown*>(identity)->Release();
    taa_busy_ = busy;
    SetLastError(error);
}
// Every Reset attempt (before the native call), device loss, teardown, a sun
// re-latch: every held reference goes and the store is empty.
void MotionOutput::flush_shadow_retention(shadow_retention::Flush reason) noexcept {
    if (!retention_) return;
    auto& st = *retention_;
    const unsigned nodes = st.store.nodes_used, references = st.store.references();
    st.store.flush(reason);
    release_retention_pending();
    if (nodes || references)
        log("shadow_retention_flush device=%llu frame=%llu reason=%s nodes=%u refs=%u", id_, frame_, shadow_retention::flush_name(reason), nodes, references);
}
// The journal, O(retired): at the scene end before the unseen walk and at the
// frame begin. Overflow, an unavailable observer: full revalidation through
// current(); FlushAll: the whole store; epochs from the drain, never the entry.
void MotionOutput::drain_retention_journal() noexcept {
    auto& st = *retention_; auto& store = st.store;
    object_lifetime::JournalEntry entries[64];
    bool revalidate = false, flush = false;
    object_lifetime::JournalDrain drain{};
    object_lifetime::JournalCursor cursor{st.cursor};
    for (unsigned guard = 0; guard <= object_lifetime::JournalCapacity / 64 + 1; ++guard) {
        drain = st.lifetime.journal_drain(cursor, entries, 64);
        st.cursor = cursor.sequence;
        if (drain.overflow) { ++store.frame.journal_overflow; ++store.totals.journal_overflow; revalidate = true; }
        if (!drain.available) revalidate = true;
        for (std::uint32_t i = 0; i < drain.count; ++i) {
            if (entries[i].kind == object_lifetime::JournalKind::FlushAll) flush = true;
            else store.retire(entries[i].node_serial, frame_);
        }
        if (!drain.more) break;
    }
    st.available = drain.available;
    if (st.mutation_known) store.frame.mutation_delta += drain.mutation_revision - st.mutation_revision;
    st.mutation_revision = drain.mutation_revision; st.mutation_known = true;
    const bool epoch_moved = drain.available && store.epochs_set && store.nodes_used && (drain.load_epoch != store.load_epoch || drain.registry_epoch != store.registry_epoch);
    if (flush || epoch_moved) { store.flush(epoch_moved ? shadow_retention::Flush::Epoch : shadow_retention::Flush::Observer); return; }
    if (revalidate && store.nodes_used) {
        const unsigned before = store.nodes_used;
        // Per node with its own recorded registry, node and camera identity. A node whose camera or
        // registry is gone cannot be confirmed: it leaves under revalidate_context_lost, not as retired.
        store.revalidate([&](const shadow_retention::Node& n) noexcept {
            object_lifetime::Snapshot s{};
            using shadow_retention::Revalidation;
            if (st.lifetime.current(n.registry, n.node, n.handle, n.camera, n.camera_handle, &s) && s.known && s.node_serial == n.serial
                && s.load_epoch == store.load_epoch && s.registry_epoch == store.registry_epoch) return Revalidation::Known;
            using object_lifetime::Reason;
            // Dead: the observer answered about this node (unknown birth, another pointer, another serial or epoch).
            // Anything else (camera or registry gone, observer disabled or mid-mutation) is a lost context.
            const bool answered = s.known || s.reason == Reason::UnknownNodeBirth || s.reason == Reason::PointerMismatch;
            return answered ? Revalidation::Dead : Revalidation::ContextLost;
        }, frame_);
        if (!drain.available && before && !store.nodes_used) { store.frame.flush = shadow_retention::Flush::Observer; ++store.totals.flushes[unsigned(shadow_retention::Flush::Observer)]; }
    }
}
// A frame begin: sightings of a frame that never reached a scene end leave
// (their rows belong to a camera latch that is gone); retirements are consumed.
void MotionOutput::retention_frame_begin() noexcept {
    auto& st = *retention_;
    if (st.store.dirty_count) st.store.abandon_sightings();
    // The idle watchdog: presented frames without a scene end (menus, loading) keep up to the whole
    // store's wrappers alive; after idle_flush_frames of them the store is flushed (flush=idle).
    if (st.published_frame != frame_ - 1 && st.store.nodes_used && ++st.idle_frames >= shadow_retention::idle_flush_frames) { st.idle_frames = 0; flush_shadow_retention(shadow_retention::Flush::Idle); }
    drain_retention_journal();
    release_retention_pending();
}
// The seen path: the record note_candidate_draw just made and its lease.
void MotionOutput::note_retention_draw(const MotionRoute& route, const shadow_replay::Record& record, const shadow_replay::DepthGeometry& g, const shadow_replay::ExtentEntry* extent) noexcept {
    auto& st = *retention_;
    const std::int64_t t0 = st.timing ? retention_ticks() : 0;
    const auto& k = route.key;
    // A draw the scope gate refused is still routed (unmatched) and replayed live, but it has no node
    // scope or no known lifetime: never retained. Missing lifetime evidence never becomes implied stability.
    if (route.gate == MotionGate::Scope || !k.object_lifetime) { ++st.store.frame.unscoped; return; }
    if (!g.leased) { ++st.store.frame.refused; return; } // no rows window or an unreadable declaration: nothing to retain
    shadow_retention::Sighting s;
    s.serial = k.object_lifetime; s.load_epoch = route.load_epoch; s.registry_epoch = route.registry_epoch; s.observer_epoch = route.observer_epoch;
    s.node = std::uintptr_t(k.node); s.handle = k.node_handle; s.registry = route.registry; s.camera = std::uintptr_t(k.camera); s.camera_handle = k.camera_handle; s.model = k.model; s.lod = k.lod; s.flags12c = route.node_flags12c; s.flags130 = route.node_flags130;
    s.key.vb = k.vertex_buffer; s.key.ib = k.indexed ? k.index_buffer : 0; s.key.declaration = k.declaration;
    s.key.stream_offset = k.stream_offset; s.key.stride = k.stride; s.key.topology = k.topology; s.key.primitives = k.primitives; s.key.first = k.first;
    s.key.min_vertex = k.min_vertex; s.key.vertex_count = k.vertex_count; s.key.base_vertex = k.base_vertex; s.key.indexed = k.indexed;
    s.vb = {record.vb_identity, record.vb_view.allocation_id, record.vb_generation, record.vb_view.revision};
    if (k.indexed) s.ib = {record.ib_identity, record.ib_view.allocation_id, record.ib_generation, record.ib_view.revision};
    s.declaration = reinterpret_cast<std::uintptr_t>(g.declaration); s.cull_mode = g.cull_mode; s.rows = g.rows;
    if (extent) { s.lo = extent->lo; s.hi = extent->hi; }
    if (st.eye_frame != frame_) { st.eye_frame = frame_; st.store.set_eye(camera_scene_); } // capacity eviction measures from this frame's eye
    shadow_retention::Acquired acquired;
    st.store.seen(s, frame_, acquired);
    if (acquired.count) {
        // The store's own reference, taken now while the application's binding is live.
        const DWORD error = GetLastError();
        for (unsigned i = 0; i < acquired.count; ++i) reinterpret_cast<IUnknown*>(acquired.identity[i])->AddRef();
        SetLastError(error);
    }
    st.registry = route.registry; st.camera = std::uintptr_t(k.camera); st.camera_handle = k.camera_handle;
    if (st.timing) { st.draw_ticks += retention_ticks() - t0; ++st.draw_calls; }
}
// The scene end, after the frame's sun verdict and before the replay.
void MotionOutput::retention_scene_end(bool sun_source_switched) noexcept {
    auto& st = *retention_; auto& store = st.store;
    if (st.published_frame == frame_) return;
    const std::int64_t t0 = retention_ticks();
    const DWORD error = GetLastError();
    st.idle_frames = 0; // a scene end: the frame is not idle
    drain_retention_journal();
    const std::int64_t t1 = retention_ticks();
    // The validated sun changed beyond the gate, or is the first after a period with none.
    const bool none = sun_verdict_ == shadow_replay::SunVerdict::None;
    // A switch of the sun source (point <-> latch) voids the retained maps' bases the same way.
    if (sun_verdict_ == shadow_replay::SunVerdict::Relatched || (st.sun_none && !none) || sun_source_switched) {
        ++store.frame.sun_relatch; store.flush(shadow_retention::Flush::Sun);
    }
    st.sun_none = none;
    // The orphan probe: one eighth of the held resources per frame. A count of 1
    // means the store's reference is the last one (advisory, fail-safe).
    if (st.mode == shadow_retention::Mode::Live && st.orphan_probe && store.references()) {
        constexpr unsigned slice = shadow_retention::resource_capacity / shadow_retention::check_period;
        for (unsigned i = 0; i < slice; ++i) {
            const unsigned slot = (st.probe_cursor + i) % shadow_retention::resource_capacity;
            const auto& r = store.resources[slot];
            if (!r.used) continue;
            auto* object = reinterpret_cast<IUnknown*>(r.identity);
            object->AddRef();
            if (object->Release() == 1) store.drop_resource(std::uint16_t(slot), frame_);
        }
        st.probe_cursor = (st.probe_cursor + slice) % shadow_retention::resource_capacity;
    }
    shadow_retention::FrameInput in;
    in.frame = frame_; in.camera = camera_scene_; in.set = depth_cascades_; in.eps = retention_eps_; in.age_cap = retention_age_cap_;
    const float* sun = shadow_replay::sun_verdict_usable(sun_verdict_) ? sun_latch_.frame_sun() : nullptr;
    in.bases_valid = sun && camera_scene_.valid && depth_cascades_.count != 0;
    // Each cascade's own current basis, exactly as the transaction builds it (its own sun and grid anchor).
    for (unsigned c = 0; in.bases_valid && c < depth_cascades_.count; ++c)
        in.bases_valid = renderer::shadow_replay_basis(camera_scene_, cascade_sun(c), depth_cascades_.cascades[c], in.bases[c], point_sun_.grid_anchor(c));
    // Room per cascade: the cap, bounded by the record capacity the issue storage was sized with (the env
    // cap parser accepts more), minus this frame's live records. Retained issues never make a cascade
    // exceed its storage: they are the ones dropped first.
    for (unsigned c = 0; c < depth_cascades_.count; ++c) {
        const unsigned live = candidates_.counts.cascade[c];
        const unsigned cap = depth_cascades_.bound(c);
        in.room[c] = live < cap ? cap - live : 0;
    }
    store.end_scene(in, [](std::uintptr_t identity) noexcept {
        // The buffer-lock view of an unseen buffer, one registry lookup per distinct buffer per
        // scene end (the store compares every record naming it: allocation, generation, revision,
        // pending or in-flight Lock; shadow_retention::buffer_verdict). Census mode holds no
        // reference: a view that is gone counts buffer_gone there.
        shadow_retention::BufferView v;
        ownership::BufferLockView view{};
        if (FAILED(ownership::get_buffer_lock_view(reinterpret_cast<IDirect3DResource9*>(identity), &view)) || !view.requested) return v;
        v.present = true; v.allocation = view.allocation_id; v.generation = view.generation; v.revision = view.revision;
        v.known = view.known; v.quiet = !view.pending_locks && !view.in_flight_locks && !view.in_flight_unlocks;
        return v;
    });
    release_retention_pending();
    SetLastError(error);
    const std::int64_t t2 = retention_ticks();
    st.journal_us = retention_us(t1 - t0); st.walk_us = retention_us(t2 - t1); st.us = retention_us(t2 - t0);
    for (unsigned c = 0; c < renderer::shadow_cascade_max; ++c) st.replayed_live[c] = st.replayed_retained[c] = 0;
    st.retained_issues = 0;
}
// After the replay: the frame line, the capture lines, the 300-frame resight line.
void MotionOutput::publish_shadow_retention() noexcept {
    auto& st = *retention_; auto& store = st.store;
    if (st.published_frame == frame_) return;
    st.published_frame = frame_;
    auto& f = store.frame;
    const bool live = st.mode == shadow_retention::Mode::Live;
    const auto& c = candidates_.counts;
    log("shadow_retention_frame device=%llu frame=%llu mode=%s known=%u nodes_live=%u nodes_unseen=%u records=%u records_unseen=%u static=%u moving=%u"
        " excluded_class=%u unscoped=%u new_nodes=%u first_seen_in_range=%u promoted=%u superseded=%u lod_replaced=%u model_replaced=%u reclassified=%u"
        " retired=%u journal_overflow=%u revalidated=%u mutation_delta=%llu buffer_changed=%u buffer_gone=%u buffer_orphaned=%u orphan_probe=%u"
        " box_exit=%u age=%u evicted=%u flush=%s unseen_in_frustum=%u unseen_outside=%u live_c0=%u live_c1=%u live_c2=%u live_c3=%u live_c4=%u"
        " would_c0=%u would_c1=%u would_c2=%u would_c3=%u would_c4=%u capped_c0=%u capped_c1=%u capped_c2=%u capped_c3=%u capped_c4=%u drift_n=%u drift_p99=%.6g drift_max=%.6g"
        " age_max=%llu refs_held=%u sun_relatch=%u cam_jump=%u transit_survivors=%u us=%.1f"
        " refused=%u moving_dropped=%u abandoned=%u deferred=%u journal_us=%.1f walk_us=%.1f draw_us=%.1f draw_calls=%u"
        " far_alternate_due_to_retained=%u revalidate_context_lost=%u release_queue_full=%u reclassified_after_unseen=%u admitted_checked=%u buffer_views=%u idle_frames=%u",
        id_, frame_, live ? "live" : "census", unsigned(st.registered && st.available), f.nodes_live, f.nodes_unseen, f.records, f.records_unseen, f.statics, f.moving,
        f.excluded_class, f.unscoped, f.new_nodes, f.first_seen_in_range, f.promoted, f.superseded, f.lod_replaced, f.model_replaced, f.reclassified,
        f.retired, f.journal_overflow, f.revalidated, static_cast<unsigned long long>(f.mutation_delta), f.buffer_changed, f.buffer_gone, f.buffer_orphaned, unsigned(live && st.orphan_probe),
        f.box_exit, f.age, f.evicted, shadow_retention::flush_name(f.flush), f.unseen_in_frustum, f.unseen_outside, c.cascade[0], c.cascade[1], c.cascade[2], c.cascade[3], c.cascade[4],
        f.would[0], f.would[1], f.would[2], f.would[3], f.would[4], f.capped[0], f.capped[1], f.capped[2], f.capped[3], f.capped[4], f.drift_n, double(f.drift_p99), double(f.drift_max),
        static_cast<unsigned long long>(f.age_max), store.references(), f.sun_relatch, f.cam_jump, f.transit_survivors, st.us,
        f.refused, f.moving_dropped, f.abandoned, f.deferred, st.journal_us, st.walk_us, retention_us(st.draw_ticks), st.draw_calls,
        f.far_alternate_due_to_retained, f.revalidate_context_lost, f.release_queue_full, f.reclassified_after_unseen, f.admitted_checked, f.buffer_views, st.idle_frames);
    if (capture_) {
        for (unsigned i = 0; i < shadow_retention::node_capacity; ++i) {
            const auto& n = store.nodes[i];
            if (!n.used) continue;
            const bool unseen = n.last_seen != frame_;
            for (std::uint16_t q = n.head; q != shadow_retention::none; q = store.draws[q].next) {
                const auto& d = store.draws[q];
                log("shadow_retention_caster device=%llu frame=%llu handle=%u serial=%llu model=%08x lod=%u flags12c=%08x class=%s unseen=%llu centre=%.9g,%.9g,%.9g half=%.6g,%.6g,%.6g cascades=%u in_frustum=%u vb=%llu primitives=%u streak=%u",
                    id_, frame_, unsigned(n.handle), static_cast<unsigned long long>(n.serial), unsigned(n.model), unsigned(n.lod), unsigned(n.flags12c), n.is_static ? "static" : "moving",
                    static_cast<unsigned long long>(unseen ? frame_ - n.last_seen : 0), d.centre[0], d.centre[1], d.centre[2], double(d.half[0]), double(d.half[1]), double(d.half[2]),
                    unsigned(d.cascades), unsigned(n.bounds_valid && camera_scene_.valid && shadow_retention::Store::inside_frustum(camera_scene_, n.centre, n.half)),
                    static_cast<unsigned long long>(d.key.vb), unsigned(d.key.primitives), unsigned(d.streak));
            }
        }
    }
    st.last_nodes_unseen = f.nodes_unseen;
    f = {};
    st.draw_ticks = 0; st.draw_calls = 0;
    if (frame_ && frame_ % shadow_retention::resight_period == 0) {
        const auto& t = store.totals;
        char text[1024]; int used = 0;
        const auto put = [&](const char* name, const std::uint64_t* v) { for (unsigned b = 0; b < shadow_retention::resight_buckets && used >= 0 && used < int(sizeof text); ++b) used += std::snprintf(text + used, sizeof text - used, " %s_b%u=%llu", name, b, static_cast<unsigned long long>(v[b])); };
        for (unsigned b = 0; b < shadow_retention::resight_buckets && used >= 0 && used < int(sizeof text); ++b)
            used += std::snprintf(text + used, sizeof text - used, " b%u_same=%llu b%u_moved=%llu b%u_changed=%llu", b, static_cast<unsigned long long>(t.same[b]), b, static_cast<unsigned long long>(t.moved[b]), b, static_cast<unsigned long long>(t.changed[b]));
        put("expired_retired", t.expired_retired); put("expired_box", t.expired_box); put("expired_gone", t.expired_gone);
        if (used < 0 || used >= int(sizeof text)) text[0] = 0;
        log("shadow_retention_resight device=%llu frame=%llu%s", id_, frame_, text);
    }
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Seam: 0 enabled, 1 mode, 2 nodes, 3 records, 4 refs_held, 5 static nodes, 6 unseen nodes, 7 retained issues of the last replay,
// 8 retired, 9 box_exit, 10 age, 11 evicted, 12 buffer_changed, 13 buffer_gone, 14 buffer_orphaned, 15 reclassified, 16 lod_replaced,
// 17 model_replaced, 18 moving_dropped, 19 revalidated, 20 journal_overflow, 21 refused, 22 promoted, 23..29 flushes by reason, 30 orphan_probe, 31 pending releases,
// 32 revalidate_context_lost, 33 far_alternate_due_to_retained, 34 flush idle, 35 reclassified_after_unseen.
unsigned MotionOutput::fixture_shadow_retention_stats(std::uint64_t* out, unsigned count) noexcept {
    constexpr unsigned stats = 36;
    std::uint64_t v[stats]{};
    if (retention_) {
        const auto& s = retention_->store; const auto& t = s.totals;
        unsigned statics = 0;
        for (const auto& n : s.nodes) if (n.used) statics += n.is_static;
        const std::uint64_t values[] = {1, std::uint64_t(retention_->mode), s.nodes_used, s.draws_used, s.references(), statics, retention_->last_nodes_unseen, retention_->retained_issues,
            t.retired, t.box_exit, t.age, t.evicted, t.buffer_changed, t.buffer_gone, t.buffer_orphaned, t.reclassified, t.lod_replaced, t.model_replaced, t.moving_dropped,
            t.revalidated, t.journal_overflow, t.refused, t.promoted, t.flushes[0], t.flushes[1], t.flushes[2], t.flushes[3], t.flushes[4], t.flushes[5], t.flushes[6],
            std::uint64_t(retention_->orphan_probe), s.pending_count, t.revalidate_context_lost, t.far_alternate_due_to_retained, t.flushes[7], t.reclassified_after_unseen};
        std::memcpy(v, values, sizeof values);
    }
    const unsigned n = count < stats ? count : stats;
    if (out) std::memcpy(out, v, n * sizeof *v);
    return n;
}
#endif
