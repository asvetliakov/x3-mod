// Engine light on the hull (included by motion_output.cpp; engine_light_core.h, renderer/linear_engine_light_inc.h;
// docs/architecture/engine-light.md): the option, the per-frame ship and node tables built from the previous frame's
// glow-jet records and routed hull draws, the pixel twins created beside the original-shading variants, and the
// per-draw selection and constant upload.
//
// Per routed draw with the light configured and a lit ship in the table: two hash probes on the ship table (a draw of a
// lit ship is logged for the next frame: node, parent, handle, world rows, 15 words) and one on the node table. Only on a hit: the constants from the draw's
// own shadowed world and view-inverse rows (double precision, about 30 multiply-adds, and about 12 per nozzle plate
// of the ship for its plate register plus a colour copy, up to the tier's run of at most 72), the twin of the program
// the pair selection chose (pointer compares), and two SetPixelShaderConstantF calls (the run's plates and colours,
// 2 run - 1 registers ending at c197: one for a one-plate ship; the light c200-c202; never c52-c54 or c198-c199, the
// twins' DEFs) in the route's existing apply chain (a failure rolls the route back to the native draw like every other
// apply step). No
// allocation, no device Get, no memory read of game structures (the parent comes from the scope's node block that
// sample_scope already read).
void MotionOutput::configure_engine_light(bool plumes) noexcept {
    wchar_t text[8]{};
    const DWORD n = x3m::config::get(L"X3M_ENGINE_LIGHT", text, 8);
    char shown[8]{};
    for (DWORD i = 0; i < n && i < 7; ++i) shown[i] = text[i] > 0x20 && text[i] < 0x7f ? char(text[i]) : '?';
    engine_light::core::Mode mode = engine_light::core::default_mode;
    const bool ok = !n || (n < 8 && engine_light::core::parse_mode(text, n, &mode));
    if (!ok) mode = engine_light::core::default_mode;
    // The hold window (engine_light_core.h hold_ships): unset = hold_default, anything but 0..hold_max refused.
    wchar_t hold_text[8]{};
    const DWORD hn = x3m::config::get(L"X3M_ENGINE_LIGHT_HOLD", hold_text, 8);
    char hold_shown[8]{};
    for (DWORD i = 0; i < hn && i < 7; ++i)
        hold_shown[i] = hold_text[i] > 0x20 && hold_text[i] < 0x7f ? char(hold_text[i]) : '?';
    unsigned hold = engine_light::core::hold_default;
    const bool hold_ok = !hn || (hn < 8 && engine_light::core::parse_hold(hold_text, hn, &hold));
    if (!hold_ok) hold = engine_light::core::hold_default;
    engine_light_requested_ = mode == engine_light::core::Mode::on && plumes && plumes_requested_ && engine_ring_ &&
                              !linear_material_requested_;
    const char* reason = mode != engine_light::core::Mode::on ? "off"
                         : !plumes || !plumes_requested_  ? "no_plumes"
                         : !engine_ring_                  ? "no_ring"
                         : linear_material_requested_     ? "linear_materials"
                                                          : "requested";
    if (engine_light_requested_ && !engine_light_) {
        engine_light_ = new (std::nothrow) EngineLightState;
        if (!engine_light_) {
            engine_light_requested_ = false;
            reason = "allocation";
        }
    }
    if (engine_light_) {
        engine_light_->tables[0].clear();
        engine_light_->tables[1].clear();
        engine_light_->hold = hold;
        engine_light_->nodes.clear();
        engine_light_->log.clear();
        engine_light_->built_frame = ~std::uint64_t(0);
    }
    log("engine_light_mode device=%llu setting=%s status=%s mode=%s requested=%u reason=%s behind=%.2f reach=%.2f colour_scale=%.2f cap=1 constants=c%u-c%u ships_max=%u plates_max=%u hold=%u hold_setting=%s hold_status=%s",
        id_, n ? shown : "-", ok ? "ok" : n >= 8 ? "too_long" : "invalid_setting", engine_light::core::mode_name(mode),
        unsigned(engine_light_requested_), reason, double(engine_light::core::behind),
        double(engine_light::core::reach), double(engine_light::core::colour_scale),
        renderer::EngineLightAbi::light_constant,
        renderer::EngineLightAbi::pixel_constant + renderer::EngineLightAbi::pixel_constant_count - 1,
        engine_light::core::ship_capacity, engine_light::core::plate_slots, hold, hn ? hold_shown : "-",
        hold_ok ? "ok" : hn >= 8 ? "too_long" : "invalid_setting");
}
// The twins of the program's original-shading variants, created once at registration beside them with the options
// that built each (engine_light_kind order: the plain motion variant at K = 0, the fill, the gained and widened, the
// share producer and its gained and widened forms). A refused transform, a twin whose share / gain / widening out-flags
// differ from its base's (engine_light::core::twin_matches_base; `mismatched`) or a failed create leaves that kind
// without a twin: a lit draw selecting it stays unlit (counted no_twin). One engine_light_variant row per reviewed
// program.
void MotionOutput::engine_light_create_twins(ShaderEntry& entry, const void* code, UINT bytes,
                                             std::uint64_t hash) noexcept {
    if (!engine_light_requested_ || !entry.variant) return;
    const float fill = original_fill_requested_ ? original_fill_ : 0.f;
    const renderer::HullLightmapWiden widen{lightmap_widen_k_, lightmap_widen_b_};
    IDirect3DPixelShader9* bases[engine_light_kinds] = {
        static_cast<IDirect3DPixelShader9*>(entry.variant), entry.original_fill_variant, entry.hull_lightmap_variant,
        entry.hull_lightmap_widen_variant, entry.sun_original_variant, entry.sun_original_lightmap_variant,
        entry.sun_original_lightmap_widen_variant};
    renderer::OriginalVariantOptions options[engine_light_kinds];
    options[1].fill = original_fill_;
    for (unsigned k = 2; k < engine_light_kinds; ++k) options[k].fill = fill;
    for (unsigned k : {2u, 3u, 5u, 6u}) {
        options[k].lightmap_gain = hull_lightmap_gain_;
        options[k].lightmap_dynamic = lightmap_far_fade_;
    }
    for (unsigned k : {3u, 6u}) options[k].widen = &widen;
    for (unsigned k : {4u, 5u, 6u}) options[k].share = true;
    static_assert(engine_light_kinds == engine_light::core::twin_kinds, "one expectation per twin kind");
    unsigned created = 0, refused = 0, failed = 0, mismatched = 0;
    std::size_t words_max = 0;
    std::uint32_t slots_max = 0; // the largest created twin's ps_3_0 slots (ps3_program_slots)
    for (unsigned k = 0; k < engine_light_kinds; ++k) {
        release(entry.engine_twin[k]);
        if (!bases[k]) continue;
        try {
            std::vector<std::uint32_t> words;
            bool applied = false, share = false, gain = false, widened = false;
            const auto result = renderer::linear_material_original_engine_light_pixel_variant(
                static_cast<const std::uint32_t*>(code), bytes / 4, options[k], words, depth_enabled_, applied, &share,
                &gain, &widened);
            if (result != renderer::LinearMaterialResult::Applied || !applied) {
                if (result != renderer::LinearMaterialResult::UnsupportedShader) refused |= 1u << k;
                continue;
            }
            if (!engine_light::core::twin_matches_base(k, applied, share, gain, widened)) {
                mismatched |= 1u << k;
                continue;
            }
            IDirect3DPixelShader9* twin = nullptr;
            const HRESULT hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()),
                                                                     &twin);
            if (SUCCEEDED(hr) && twin) {
                entry.engine_twin[k] = twin;
                created |= 1u << k;
                words_max = words.size() > words_max ? words.size() : words_max;
                const std::uint32_t slots = renderer::ps3_program_slots(words.data(), words.size());
                slots_max = slots > slots_max ? slots : slots_max;
                ++engine_light_->twins;
            } else {
                release(twin);
                failed |= 1u << k;
            }
        } catch (...) {
            failed |= 1u << k;
        }
    }
    if (created || refused || failed || mismatched)
        log("engine_light_variant device=%llu original=%016llx created=%02x refused=%02x mismatched=%02x failed=%02x words_max=%u slots_max=%lu slot_budget=%lu depth=%u",
            id_, hash, created, refused, mismatched, failed, unsigned(words_max), static_cast<unsigned long>(slots_max),
            static_cast<unsigned long>(renderer::ps3_slot_budget()), unsigned(depth_enabled_));
}
void MotionOutput::engine_light_release(ShaderEntry& entry) noexcept {
    for (auto*& twin : entry.engine_twin) release(twin);
}
// The twin of the program bind_variant_pair chose, or null (a program kind without one).
IDirect3DPixelShader9* MotionOutput::engine_light_twin(IDirect3DPixelShader9* ps) const noexcept {
    if (!ps) return nullptr;
    const IDirect3DPixelShader9* bases[engine_light_kinds] = {
        shadow_.ps_variant,          shadow_.ps_original_fill_variant, shadow_.ps_hull_lightmap_variant,
        shadow_.ps_hull_lightmap_widen, shadow_.ps_sun_original,    shadow_.ps_sun_original_lightmap,
        shadow_.ps_sun_original_lightmap_widen};
    for (unsigned k = 0; k < engine_light_kinds; ++k)
        if (ps == bases[k]) return shadow_.engine_twin[k];
    return nullptr;
}
// VS constants: the world and view-inverse rows of both layouts (c7-9, c13-15, c28-30, c34-36), kept while the light
// is configured; bit r of the window's mask = c(base + r) known.
void MotionOutput::engine_light_shadow_rows(UINT start, const float* data, UINT count) noexcept {
    const UINT end = start + count;
    for (unsigned w = 0; w < 4; ++w) {
        const UINT base = engine_light_world_bases[w], base_end = base + 3;
        if (start >= base_end || end <= base) continue;
        const UINT lo = start > base ? start : base, hi = end < base_end ? end : base_end;
        std::memcpy(shadow_.engine_rows[w] + (lo - base) * 4, data + (lo - start) * 4, (hi - lo) * 16);
        for (UINT r = lo; r < hi; ++r) shadow_.engine_rows_known[w] |= std::uint8_t(1u << (r - base));
    }
}
const float* MotionOutput::engine_light_rows(unsigned base) const noexcept {
    for (unsigned w = 0; w < 4; ++w)
        if (engine_light_world_bases[w] == base) return shadow_.engine_rows_known[w] == 7 ? shadow_.engine_rows[w] : nullptr;
    return nullptr;
}
// The pair predicate, refreshed with the pair identities: a reviewed original pair (the eye and normal semantics the
// twin reads are those of the reviewed VS), both motion variants, and a VS whose row layout is known.
void MotionOutput::refresh_engine_light_pair() noexcept {
    shadow_.engine_layout = engine_light_requested_ ? engine_light::core::vertex_layout(shadow_.vs_hash)
                                                    : engine_light::core::VertexLayout{};
    shadow_.engine_light_pair = engine_light_requested_ && shadow_.engine_layout.world && shadow_.vs_variant &&
                                shadow_.ps_variant && shadow_.vs_registered && shadow_.ps_registered &&
                                renderer::linear_material_pair_reviewed(shadow_.vs_hash, shadow_.ps_hash);
}
// At the frame boundary (begin_frame, before engine_effects_frame_begin clears the ring): the previous frame's row,
// then the ship table from the previous frame's records of the scene view, the held ships (hold_ships: a ship lit in
// the previous frame without a record, its hull still drawn, for engine_light_hold frames) and the node table from its
// logged hull draws. A Reset's repeated begin of the same frame builds nothing.
void MotionOutput::engine_light_frame() noexcept {
    if (!engine_light_requested_ || !engine_light_ || engine_light_->built_frame == frame_) return;
    auto& s = *engine_light_;
    namespace el = engine_light::core;
    if (log_tier::cached_debug && engine_census_ && (s.counts.candidates || s.nodes.count || s.ships->count)) {
        const auto& c = s.counts;
        // plates=: the ships by nozzle-plate count 1..8, plates_more= the ships above eight and plates_max= the largest
        // count (the per-ship main nozzles after the co-located merge, capped at plate_slots; plates_dropped the
        // nozzles beyond it).
        unsigned by_count[9] = {}, more = 0, most_plates = 0, lights = 0;
        int own = -1, most = -1;
        for (unsigned i = 0; i < s.ships->count; ++i) {
            const el::Light& l = s.ships->lights[i];
            const unsigned k = l.plate_count;
            if (k <= 8) ++by_count[k];
            else ++more;
            most_plates = k > most_plates ? k : most_plates;
            lights += k;
            if (l.own && own < 0) own = int(i);
            if (most < 0 || k > s.ships->lights[most].plate_count) most = int(i);
        }
        // lights=: the plates over all ships, each carrying its nozzle's light (block_constants); own_lights= and
        // most_lights=: the own ship's and the ship of the most plates' (ties: the first entry) root and plate node
        // handles, brightest first, the plate slot order ('-': none). Two fixed buffers, no allocation.
        char own_text[12 + el::plate_slots * 9] = "-", most_text[12 + el::plate_slots * 9] = "-";
        const auto handles = [&](int ship, char* out, std::size_t size) {
            if (ship < 0) return;
            const el::Light& l = s.ships->lights[ship];
            int at = std::snprintf(out, size, "%08lx:", static_cast<unsigned long>(l.root));
            for (unsigned p = 0; p < l.plate_count && p < el::plate_slots && at > 0 && std::size_t(at) < size; ++p)
                at += std::snprintf(out + at, size - std::size_t(at), "%s%lx", p ? "," : "",
                                    static_cast<unsigned long>(l.plates[p].handle));
        };
        handles(own, own_text, sizeof own_text);
        handles(most, most_text, sizeof most_text);
        log("engine_light_frame device=%llu frame=%llu ships=%u ships_drawn=%u nodes=%u candidates=%u draws_lit=%u no_twin=%u no_rows=%u records=%u main=%u rcs=%u brake=%u other_view=%u invalid=%u orphan=%u ships_dropped=%u logged=%u log_dropped=%u matched=%u singular=%u nodes_dropped=%u twins=%u plates=%u,%u,%u,%u,%u,%u,%u,%u plates_more=%u plates_max=%u plates_none=%u unfloored=%u plates_dropped=%u lights=%u own_lights=%s most_lights=%s held=%u hold_expired=%u hold_walked=%u",
            id_, s.built_frame, s.ships->count, s.nodes.ships, s.nodes.count, c.candidates, c.draws_lit, c.no_twin,
            c.no_rows, s.ships->stats.records, s.ships->stats.main, s.ships->stats.rcs, s.ships->stats.brake,
            s.ships->stats.other_view, s.ships->stats.invalid, s.ships->stats.orphan, s.ships->stats.dropped,
            s.nodes.stats.logged, s.nodes.stats.log_dropped, s.nodes.stats.matched, s.nodes.stats.singular,
            s.nodes.stats.dropped, s.twins, by_count[1], by_count[2], by_count[3], by_count[4], by_count[5], by_count[6],
            by_count[7], by_count[8], more, most_plates, by_count[0], s.ships->stats.unfloored, s.ships->stats.plates_dropped, lights,
            own_text, most_text, s.ships->stats.held, s.ships->stats.hold_expired, s.ships->stats.hold_walked);
    }
    s.counts = {};
    s.built_frame = frame_;
    // The plume stage's records: only while the stage is attached (a refused or disarmed stage forwards the glow, and
    // a light without its plume is not drawn either); the hold likewise. The table the previous frame drew with becomes
    // `previous` (its draws are the logged ones) and the older buffer takes this boundary's table.
    std::uint32_t scene_camera = 0;
    const bool attached = engine_ring_ && !engine_plumes_stage_off();
    // The node-sourced append for a frame whose stage did not run (engine_node_append is a no-op after the stage's).
    if (attached) engine_node_append();
    std::swap(s.ships, s.previous);
    const bool records = attached && engine_ring_->count &&
                         engine_plumes::scene_view_camera(engine_ring_->camera, engine_ring_->scene, engine_ring_->own,
                                                          engine_ring_->count, &scene_camera);
    float preset_scale = 1.f;
    engine_plumes::preset_scale(plumes_preset_, &preset_scale);
    if (records)
        el::build_ships(engine_ring_->records, engine_ring_->parent, engine_ring_->parent_radius, engine_ring_->camera,
                        engine_ring_->scene, scene_camera, engine_ring_->count, &engine_effects::body, plumes_look_,
                        preset_scale, s.ships, engine_ring_->own);
    else
        s.ships->clear();
    if (records) { // the root filter when the stage did not run, and its reset clock
        engine_scene_camera_last_ = scene_camera;
        engine_scene_camera_frame_ = frame_;
    }
    // The hold, minus the ships whose root the walk covered this frame (engine-nozzle-source.md section 7).
    if (attached) el::hold_ships(*s.previous, s.ships, s.log, s.hold, engine_node_walked_, engine_node_walked_count_);
    el::build_nodes(*s.ships, s.log, &s.nodes);
    s.log.clear();
}
// After the motion ABI's upload on a draw whose twin bind_variant_pair bound: the run of the draw's plate tier (its
// plates and their lights' colours, the 2 run - 1 registers ending at c197: c197 alone for a one-plate ship) and the
// light c200-c202 (EngineLightAbi), two calls; c52-c54 and c198-c199 (the twins' DEFs) are never written.
HRESULT MotionOutput::engine_light_upload() noexcept {
    using Abi = renderer::EngineLightAbi;
    namespace el = engine_light::core;
    static_assert(Abi::plate_count == el::plate_slots && Abi::light_constant == el::block_first &&
                      Abi::block_registers == el::block_registers && Abi::block_light * 4 == el::block_light &&
                      Abi::plate_constant == el::plate_register(0) && Abi::tier_count == el::plate_tiers &&
                      Abi::upload_first(el::plate_slots) == el::run_first(el::plate_slots) &&
                      sizeof(EngineLightState::constants) == Abi::block_registers * 4 * sizeof(float),
                  "the staging block: the plates and their colours c55-c197, two unused rows, the light");
    static_assert(el::same_runs(Abi::plate_runs), "the core's plate tiers are the twins' branch levels");
    const unsigned run = el::block_upload_run(engine_light_->constants), first = Abi::upload_first(run);
    HRESULT hr = direct_call<SetConstantsFFn>(SetPixelShaderConstantF, first,
                                              engine_light_->constants + (first - Abi::light_constant) * 4,
                                              Abi::upload_count(run));
    if (SUCCEEDED(hr))
        hr = direct_call<SetConstantsFFn>(SetPixelShaderConstantF, Abi::pixel_constant,
                                          engine_light_->constants + Abi::block_light * 4, Abi::pixel_constant_count);
    if (SUCCEEDED(hr)) ++engine_light_->counts.draws_lit;
    return hr;
}
// Before bind_variant_pair on a routed draw: log the draw for the next frame when its node or its parent is a lit ship's
// root this frame (the log then holds the lit ships' hull draws only, not the scene's thousands), and decide whether its
// node is lit (route.engine_light_want, the constants in engine_light_->constants). Like the fill, never on a fade-arm
// draw (its own RT2 and blend contract).
void MotionOutput::engine_light_prepare(MotionRoute& route, bool material) noexcept {
    route.engine_light_want = false;
    if (!engine_light_requested_ || !engine_light_ || material || route.fade_arm || !shadow_.engine_light_pair) return;
    const std::uint32_t node = std::uint32_t(route.key.node), handle = route.key.node_handle;
    if (!node) return;
    auto& s = *engine_light_;
    if (!s.ships->count) return;
    const float* world = engine_light_rows(shadow_.engine_layout.world);
    if (world && (engine_light::core::find_ship(*s.ships, node) >= 0 ||
                  engine_light::core::find_ship(*s.ships, route.scope_parent) >= 0))
        s.log.push(node, route.scope_parent, handle, world);
    const auto* light = engine_light::core::find_node(s.nodes, node, handle);
    if (!light) return;
    ++s.counts.candidates;
    const float* view = engine_light_rows(shadow_.engine_layout.view_inverse);
    // c55-c202: the run's plates and colours, the light and the tier in c202.w; c198-c199 stay 0.
    if (!world || !view || hdr_state_ != HdrState::Active ||
        !engine_light::core::block_constants(*light, world, view, s.constants)) {
        ++s.counts.no_rows;
        return;
    }
    route.engine_light_want = true;
}
