// Engine light on the hull (included by motion_output.cpp; engine_light_core.h, renderer/linear_engine_light_inc.h;
// docs/architecture/engine-light.md): the option, the per-frame ship and node tables built from the previous frame's
// glow-jet records and routed hull draws, the pixel twins created beside the original-shading variants, and the
// per-draw selection and constant upload.
//
// Per routed draw with the light configured and a lit ship in the table: two hash probes on the ship table (a draw of a
// lit ship is logged for the next frame: node, parent, handle, world rows, 15 words) and one on the node table. Only on a hit: the constants from the draw's
// own shadowed world and view-inverse rows (double precision, about 30 multiply-adds, and about 12 per nozzle plate
// of the ship, at most eight), the twin of the program the pair selection chose (pointer compares), and one
// SetPixelShaderConstantF of thirteen registers (the plates c190-c197, two filler registers c198-c199 the twins' DEFs
// shadow, the light c200-c202) in the route's existing apply chain (a failure rolls the route back to the native draw
// like every other apply step). No
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
        engine_light_->ships.clear();
        engine_light_->nodes.clear();
        engine_light_->log.clear();
        engine_light_->built_frame = ~std::uint64_t(0);
    }
    log("engine_light_mode device=%llu setting=%s status=%s mode=%s requested=%u reason=%s behind=%.2f reach=%.2f colour_scale=%.2f cap=1 constants=c%u-c%u ships_max=%u plates_max=%u",
        id_, n ? shown : "-", ok ? "ok" : n >= 8 ? "too_long" : "invalid_setting", engine_light::core::mode_name(mode),
        unsigned(engine_light_requested_), reason, double(engine_light::core::behind),
        double(engine_light::core::reach), double(engine_light::core::colour_scale),
        renderer::EngineLightAbi::plate_constant,
        renderer::EngineLightAbi::pixel_constant + renderer::EngineLightAbi::pixel_constant_count - 1,
        engine_light::core::ship_capacity, engine_light::core::plate_slots);
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
        log("engine_light_variant device=%llu original=%016llx created=%02x refused=%02x mismatched=%02x failed=%02x words_max=%u depth=%u",
            id_, hash, created, refused, mismatched, failed, unsigned(words_max), unsigned(depth_enabled_));
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
// then the ship table from the previous frame's records of the scene view and the node table from its logged hull
// draws. A Reset's repeated begin of the same frame builds nothing.
void MotionOutput::engine_light_frame() noexcept {
    if (!engine_light_requested_ || !engine_light_ || engine_light_->built_frame == frame_) return;
    auto& s = *engine_light_;
    namespace el = engine_light::core;
    static_assert(el::plate_slots == 8, "the plates= field lists eight counts");
    if (log_tier::cached_debug && engine_census_ && (s.counts.candidates || s.nodes.count || s.ships.count)) {
        const auto& c = s.counts;
        // plates=: the ships by nozzle-plate count, 1..plate_slots (the per-ship main nozzles after the co-located
        // merge, capped at plate_slots; plates_dropped the nozzles beyond it).
        unsigned by_count[el::plate_slots + 1] = {};
        for (unsigned i = 0; i < s.ships.count; ++i) {
            const unsigned k = s.ships.lights[i].plate_count;
            ++by_count[k <= el::plate_slots ? k : el::plate_slots];
        }
        log("engine_light_frame device=%llu frame=%llu ships=%u ships_drawn=%u nodes=%u candidates=%u draws_lit=%u no_twin=%u no_rows=%u records=%u main=%u rcs=%u brake=%u other_view=%u invalid=%u orphan=%u ships_dropped=%u logged=%u log_dropped=%u matched=%u singular=%u nodes_dropped=%u twins=%u plates=%u,%u,%u,%u,%u,%u,%u,%u plates_none=%u merged=%u plates_dropped=%u",
            id_, s.built_frame, s.ships.count, s.nodes.ships, s.nodes.count, c.candidates, c.draws_lit, c.no_twin,
            c.no_rows, s.ships.stats.records, s.ships.stats.main, s.ships.stats.rcs, s.ships.stats.brake,
            s.ships.stats.other_view, s.ships.stats.invalid, s.ships.stats.orphan, s.ships.stats.dropped,
            s.nodes.stats.logged, s.nodes.stats.log_dropped, s.nodes.stats.matched, s.nodes.stats.singular,
            s.nodes.stats.dropped, s.twins, by_count[1], by_count[2], by_count[3], by_count[4], by_count[5], by_count[6],
            by_count[7], by_count[8], by_count[0], s.ships.stats.merged, s.ships.stats.plates_dropped);
    }
    s.counts = {};
    s.built_frame = frame_;
    // The plume stage's records: only while the stage is attached (a refused or disarmed stage forwards the glow, and
    // a light without its plume is not drawn either).
    std::uint32_t scene_camera = 0;
    const bool records = engine_ring_ && engine_ring_->count && !engine_plumes_stage_off() &&
                         engine_plumes::scene_view_camera(engine_ring_->camera, engine_ring_->scene, engine_ring_->own,
                                                          engine_ring_->count, &scene_camera);
    float preset_scale = 1.f;
    engine_plumes::preset_scale(plumes_preset_, &preset_scale);
    if (records)
        el::build_ships(engine_ring_->records, engine_ring_->parent, engine_ring_->parent_radius, engine_ring_->camera,
                        engine_ring_->scene, scene_camera, engine_ring_->count, &engine_effects::body, plumes_look_,
                        preset_scale, &s.ships, engine_ring_->own);
    else
        s.ships.clear();
    el::build_nodes(s.ships, s.log, &s.nodes);
    s.log.clear();
}
// After the motion ABI's upload on a draw whose twin bind_variant_pair bound: the plates c190-c197 and the light
// c200-c202 (EngineLightAbi), two calls; c198-c199 (the twins' DEFs) are never written.
HRESULT MotionOutput::engine_light_upload() noexcept {
    using Abi = renderer::EngineLightAbi;
    static_assert(Abi::plate_count == engine_light::core::plate_slots &&
                      sizeof(EngineLightState::constants) == Abi::block_registers * 4 * sizeof(float) && Abi::block_light == 10,
                  "the staging block: eight plates, two unused rows, the light at offset 10 registers (engine_light_prepare)");
    HRESULT hr = direct_call<SetConstantsFFn>(SetPixelShaderConstantF, Abi::plate_constant, engine_light_->constants,
                                              Abi::plate_count);
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
    if (!s.ships.count) return;
    const float* world = engine_light_rows(shadow_.engine_layout.world);
    if (world && (engine_light::core::find_ship(s.ships, node) >= 0 ||
                  engine_light::core::find_ship(s.ships, route.scope_parent) >= 0))
        s.log.push(node, route.scope_parent, handle, world);
    const auto* light = engine_light::core::find_node(s.nodes, node, handle);
    if (!light) return;
    ++s.counts.candidates;
    const float* view = engine_light_rows(shadow_.engine_layout.view_inverse);
    if (!world || !view || hdr_state_ != HdrState::Active ||
        !engine_light::core::draw_constants(*light, world, view, s.constants + 40)) {
        ++s.counts.no_rows;
        return;
    }
    // c190-c197 and the tier in c202.w; c198-c199 stay 0.
    engine_light::core::plate_constants(*light, world, view, s.constants, &s.constants[40 + 11]);
    route.engine_light_want = true;
}
