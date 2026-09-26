// Reuse the frozen production-pass fixture's authored COM/window/readback helpers.
// Its correctness main is not called. No fixture DLL or game identity bypass.
#define main fog_spatial_unused_main
#include X3M_FOG_SPATIAL_BASE
#undef main
#include "fog_route_owner_inc.h"
namespace {
constexpr DWORD card_program[]{0xffff0300u, 0x02000001u, 0x800f0800u, 0xa0e40000u, 0x0000ffffu};
#include "fog_route_camera_basis_inc.h"
std::uint64_t fnv(const std::vector<std::uint16_t>& words) {
    std::uint64_t h = 1469598103934665603ull;
    for (auto w : words)
        for (unsigned k = 0; k < 2; ++k) {
            h ^= (w >> (8 * k)) & 255u;
            h *= 1099511628211ull;
        }
    return h;
}
struct Bridge {
    IDirect3DDevice9* d;
    fog_spatial_state::Hooks& hooks;
    fog_spatial_state::Scene& scene;
    x3m::MotionOutput motion;
    Com<IDirect3DPixelShader9> card;
    Com<IDirect3DVertexDeclaration9> declaration;
    unsigned submitted = 0;
    std::vector<double> prepare_us, prepare_upload_us;
    double first_prepare_us = 0, worst_upload_us = 0;
    std::uint64_t worst_upload_frame = 0;
    unsigned worst_upload_rects = 0;
    int index = 1; // background record index of the copied sample
    explicit Bridge(IDirect3DDevice9* device, fog_spatial_state::Hooks& h, fog_spatial_state::Scene& s,
                    const D3DCAPS9& caps)
        : d(device)
        , hooks(h)
        , scene(s) {
        motion.device_ = d;
        motion.native_ = h.methods;
        motion.caps_ = caps;
        motion.owner_.surface = scene.s0.p;
        motion.depth_surface_ = scene.s2.p;
        motion.target_width_ = scene.input.w;
        motion.target_height_ = scene.input.h;
        motion.camera_scene_.valid = true;
        motion.camera_scene_.m00 = scene.input.constants[0];
        motion.camera_scene_.m11 = scene.input.constants[1];
        for (unsigned i = 0; i < 3; ++i) {
            motion.camera_scene_.r[3 * i + i] = 1;
            motion.camera_scene_.t[i] = -scene.input.constants[8 + i];
        }
        motion.depth_cascades_.count = 1;
        motion.depth_cascades_.active = 1;
        check(d->CreatePixelShader(card_program, &card.p), "card pixel program");
        const D3DVERTEXELEMENT9 decl[]{{0, 0, D3DDECLTYPE_FLOAT16_4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                       {0, 8, D3DDECLTYPE_FLOAT16_4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                       {0, 16, D3DDECLTYPE_FLOAT16_4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
                                       D3DDECL_END()};
        check(d->CreateVertexDeclaration(decl, &declaration.p), "card declaration");
        const std::uint16_t vertices[4][12]{{0xbc00, 0x3c00, 0, 0x3c00, 0, 0, 0, 0, 0, 0, 0, 0},
                                            {0x3c00, 0x3c00, 0, 0x3c00, 0x3c00, 0, 0, 0, 0, 0, 0, 0},
                                            {0xbc00, 0xbc00, 0, 0x3c00, 0, 0x3c00, 0, 0, 0, 0, 0, 0},
                                            {0x3c00, 0xbc00, 0, 0x3c00, 0x3c00, 0x3c00, 0, 0, 0, 0, 0, 0}};
        void* data = nullptr;
        check(scene.vb->Lock(0, sizeof vertices, &data, 0), "card vertices lock");
        std::memcpy(data, vertices, sizeof vertices);
        check(scene.vb->Unlock(), "card vertices unlock");
        const WORD indices[]{0, 1, 2, 2, 1, 3};
        check(scene.ib->Lock(0, sizeof indices, &data, 0), "card indices lock");
        std::memcpy(data, indices, sizeof indices);
        check(scene.ib->Unlock(), "card indices unlock");
        hooks.methods[82] = reinterpret_cast<void*>(
            &fog_spatial_state::Hooks::intercept<82, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT>);
    }
    ~Bridge() { motion.fog_.reset(); }
    void cards_state() {
        check(d->SetRenderTarget(0, scene.s0.p), "owner RT0");
        check(d->SetRenderTarget(1, nullptr), "native RT1");
        check(d->SetRenderTarget(2, nullptr), "native RT2");
        check(d->SetDepthStencilSurface(scene.z.p), "native depth");
        D3DVIEWPORT9 vp{0, 0, scene.input.w, scene.input.h, 0, 1};
        check(d->SetViewport(&vp), "card viewport");
        for (UINT i = 0; i < 16; ++i) check(d->SetTexture(i, nullptr), "card clear texture");
        const std::pair<D3DRENDERSTATETYPE, DWORD> states[]{
            {D3DRS_ZENABLE, 0},           {D3DRS_ZWRITEENABLE, 0},
            {D3DRS_ALPHATESTENABLE, 0},   {D3DRS_ALPHABLENDENABLE, 1},
            {D3DRS_COLORWRITEENABLE, 7},  {D3DRS_COLORWRITEENABLE1, 0},
            {D3DRS_COLORWRITEENABLE2, 0}, {D3DRS_CULLMODE, 1},
            {D3DRS_STENCILENABLE, 0},     {D3DRS_FILLMODE, 3},
            {D3DRS_SRCBLEND, 2},          {D3DRS_DESTBLEND, 4},
            {D3DRS_BLENDOP, 1},           {D3DRS_SEPARATEALPHABLENDENABLE, 0},
            {D3DRS_SCISSORTESTENABLE, 0}, {D3DRS_SRGBWRITEENABLE, 0},
            {D3DRS_FOGENABLE, 0},         {D3DRS_CLIPPLANEENABLE, 0}};
        for (const auto& state : states) check(d->SetRenderState(state.first, state.second), "card state");
        check(d->SetVertexShader(scene.vs.p), "card VS");
        check(d->SetPixelShader(card.p), "card PS");
        check(d->SetVertexDeclaration(declaration.p), "card decl");
        check(d->SetStreamSource(0, scene.vb.p, 0, 24), "card stream");
        check(d->SetStreamSourceFreq(0, 1), "card freq");
        check(d->SetIndices(scene.ib.p), "card indices");
        const float colour[]{.25f, .125f, .0625f, 1};
        check(d->SetPixelShaderConstantF(0, colour, 1), "card colour");
    }
    void begin(const char* family = "bluewell", unsigned sector = 0x1000, int dust = 8, bool sample = true) {
        ++motion.frame_;
        motion.counters_.taa.attempted = false;
        motion.sun_frame_.published = true;
        motion.volumetric_fog_begin_frame();
        if (sample) {
            x3m::sector_background::Sample s;
            s.status = x3m::sector_background::Status::Ready;
            s.row_valid = s.name_valid = true;
            s.dust = dust;
            s.sector = sector;
            s.table = 0x2000;
            s.record = 0x2044;
            s.index = index;
            std::strcpy(s.family, family);
            motion.volumetric_fog_sector_sample(motion.frame_, s);
        }
        motion.prepare_volumetric_fog_targets(scene.input.w, scene.input.h);
#ifndef X3M_ROUTE_BRIDGE_BASELINE
        if (motion.fog_density_requested_) { // the production owner-latch statement, timed
            const bool first = !motion.fog_density_config_logged_;
            LARGE_INTEGER t0{}, t1{}, f{};
            QueryPerformanceCounter(&t0);
            motion.prepare_volumetric_fog_density(scene.input.w, scene.input.h);
            QueryPerformanceCounter(&t1);
            QueryPerformanceFrequency(&f);
            const double us = double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart);
            // The resource-creating first call (programs, four textures, caches, thread) is reported apart from steady
            // and upload frames.
            if (first && motion.fog_density_config_logged_)
                first_prepare_us = us;
            else if (motion.fog_density_prepared_) {
                const unsigned rects = motion.fog_->density_status().upload_rects;
                (rects ? prepare_upload_us : prepare_us).push_back(us);
                if (rects && us > worst_upload_us) {
                    worst_upload_us = us;
                    worst_upload_frame = motion.frame_;
                    worst_upload_rects = rects;
                }
            }
        }
#endif
        scene.refill();
        cards_state();
        hooks.clear();
        check(d->BeginScene(), "card begin");
        motion.scene_open_ = true;
    }
    HRESULT draw() {
        x3m::MotionRoute route;
        HRESULT hr = S_FALSE;
        DWORD native_error = 0;
        const auto calls = hooks.calls[82];
        {
            // The same draw-root envelope: routing cannot leak its LastError
            // over the original native result (including an injected failure).
            x3m::LightCallBoundary boundary;
            motion.prepare_fog_card({}, route);
            boundary.before_original();
            hr = route.submit ? (++submitted, d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2))
                              : route.submission_error;
            boundary.after_original();
            native_error = GetLastError();
            if (route.fog_card_mask.masked) motion.finish_fog_card(route, hr);
        }
        const DWORD returned_error = GetLastError();
        require(returned_error == native_error, "native_card_LastError_preserved");
        require(hooks.calls[82] == calls + unsigned(route.submit), "native_card_forward_once");
        return hr;
    }
    std::vector<std::uint16_t> source() {
        check(d->EndScene(), "source observation end");
        motion.scene_open_ = false;
        const auto image = readback_words(d, scene.s0.p);
        check(d->BeginScene(), "source observation reopen");
        motion.scene_open_ = true;
        return image;
    }
    void end() {
        // The real FogPass must preserve already-owned auxiliary RT contents and
        // bindings. Cards were submitted with only RT0, as their scene guard requires.
        check(d->SetRenderTarget(1, scene.s1.p), "pass RT1");
        check(d->SetRenderTarget(2, scene.s2.p), "pass RT2");
        const fog_spatial_state::Snapshot before(d, motion.caps_);
        const auto copies = hooks.calls[34], quads = hooks.calls[83];
        motion.run_volumetric_fog();
        const auto after = fog_spatial_state::Snapshot(d, motion.caps_);
        require(before == after, "route_pass_state_restored");
        const auto first_copies = hooks.calls[34], first_quads = hooks.calls[83];
        motion.run_volumetric_fog();
        require(hooks.calls[34] == first_copies && hooks.calls[83] == first_quads, "once_only_hook_fallback_guard");
        require(first_copies - copies <= 1 && first_quads - quads <= (motion.fog_density_requested_ ? 3u : 2u),
                "one_fog_transaction_maximum");
        check(d->EndScene(), "frame end");
        motion.scene_open_ = false;
        require(fog_spatial_state::surface_bytes(d, scene.s1.p, 8) == rt1_before &&
                    fog_spatial_state::surface_bytes(d, scene.s2.p, 16) == rt2_before,
                "route_RT1_RT2_bytes");
    }
    std::vector<unsigned char> rt1_before, rt2_before;
    void record_aux() {
        rt1_before = fog_spatial_state::surface_bytes(d, scene.s1.p, 8);
        rt2_before = fog_spatial_state::surface_bytes(d, scene.s2.p, 16);
    }
};
void qualify_bridge(IDirect3D9* api, IDirect3DDevice9* d, const D3DCAPS9& caps, const std::string& input) {
    fog_spatial_state::Inputs inputs(input);
    fog_spatial_state::Hooks hooks(d, api);
    fog_spatial_state::Scene scene(d, inputs, caps,
                                   std::vector<DWORD>(std::begin(card_program), std::end(card_program)));
    Bridge b(d, hooks, scene, caps);
    b.record_aux();
    auto& m = b.motion;
    b.begin();
    check(b.draw(), "warm native card");
    const auto vanilla = b.source();
    require(vanilla != inputs.scene && m.fog_cards_.warmup && m.fog_cards_.suppressed == 0, "warmup_native_source");
    b.end();
    require(m.fog_cards_.armed && m.fog_applied_frames_ == 1, "matching_warmup_arms");
    std::printf("IMAGE legacy_warm %016llx\n", fnv(readback_words(d, scene.s0.p)));
    b.begin();
    m.history_valid = true;
    const auto inv = m.invalidations;
    check(b.draw(), "replacement native card");
    require(b.source() == inputs.scene && m.fog_cards_.suppressed == 1, "replacement_source_clean");
    require(m.invalidations == inv + 1 && !m.history_valid, "replacement_transition_requests_history_clear");
    check(b.draw(), "second replacement card");
    require(m.invalidations == inv + 1, "transition_once_per_frame");
    b.end();
    require(!m.fog_cards_.fault && m.fog_applied_frames_ == 2, "replacement_pass_applied");
    std::printf("IMAGE legacy_replaced %016llx\n", fnv(readback_words(d, scene.s0.p)));
    require(b.submitted == 3, "native_draw_count_exact");
    // Initial warmup and mode-2 transition above are complete. Replay actual
    // engine precision through the whole production parameter/admission path,
    // then the real FogPass (including uploaded inverse-column validation).
    const auto camera = m.camera_scene_;
    const auto camera_invalidations = m.invalidations, camera_submitted = b.submitted;
    const auto camera_applied = m.fog_applied_frames_;
    std::uint64_t camera_images = 0;
    for (const auto& fixed : captured_camera_basis) {
        for (unsigned i = 0; i < 9; ++i) m.camera_scene_.r[i] = float(fixed[i]) / 65536.f;
        for (unsigned j = 0; j < 3; ++j) {
            double t = 0;
            for (unsigned i = 0; i < 3; ++i) t += double(camera.t[i]) * m.camera_scene_.r[3 * i + j];
            m.camera_scene_.t[j] = float(t);
        }
        b.begin();
        check(b.draw(), "captured camera card");
        require(m.fog_card_ready_ && !m.fog_cards_.warmup && m.fog_cards_.suppressed == 1 && !m.fog_cards_.refused &&
                    b.source() == inputs.scene,
                "captured_camera_suppression");
        const auto applied_before = m.fog_applied_frames_;
        b.end();
        require(m.fog_applied_frames_ == applied_before + 1 && !m.fog_cards_.fault && hooks.calls[34] == 1 &&
                    hooks.calls[83] == 2,
                "captured_camera_actual_pass");
        require(m.invalidations == camera_invalidations, "captured_camera_no_transition");
        camera_images = camera_images * 1099511628211ull ^ fnv(readback_words(d, scene.s0.p));
    }
    require(b.submitted == camera_submitted + 33 && m.fog_applied_frames_ == camera_applied + 33,
            "captured_camera_sequence_33");
    // Reflection, excessive shear, nonfinite input, and a row-Gram boundary
    // accepted by fog_world_basis but refused by the uploaded inverse Gram.
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        m.camera_scene_ = camera;
        if (scenario == 0) m.camera_scene_.r[0] = -1;
        if (scenario == 1) m.camera_scene_.r[1] = .002f;
        if (scenario == 2) m.camera_scene_.r[0] = std::numeric_limits<float>::quiet_NaN();
        if (scenario == 3) m.camera_scene_.r[0] = std::sqrt(1.f - .0009995f);
        b.begin();
        x3m::renderer::FogFrame parameters{};
        parameters.width = scene.input.w;
        parameters.height = scene.input.h;
        bool tracked = false;
        const char* reason = m.fog_frame_parameters(parameters, 1.f, tracked);
        require(reason != nullptr, "malformed_camera_parameter_refusal");
        if (scenario == 3)
            require(parameters.params.world.valid && std::strcmp(reason, "parameters") == 0,
                    "inverse_gram_boundary_refusal");
        const auto applied_before = m.fog_applied_frames_;
        check(b.draw(), "malformed camera native card");
        require(m.fog_cards_.refused && !m.fog_cards_.suppressed && b.source() == vanilla,
                "malformed_camera_native_exact");
        b.end();
        require(m.fog_applied_frames_ == applied_before && !m.fog_cards_.fault && hooks.calls[34] == 0 &&
                    hooks.calls[83] == 0 && readback_words(d, scene.s0.p) == vanilla,
                "malformed_camera_no_pass");
        m.camera_scene_ = camera;
        b.begin();
        check(b.draw(), "camera refusal recover");
        b.end();
        require(m.fog_applied_frames_ == applied_before + 1 && m.fog_cards_.suppressed == 1 && !m.fog_cards_.fault,
                "malformed_camera_recovery");
    }
    std::printf("IMAGE legacy_captured_cameras_33 %016llx\n", camera_images);
    std::puts("CAMERA_BRIDGE captured=33 first_person=32 worst=1 malformed=4 inverse_boundary=1 PASS");
    b.begin("bluewell", 0x3000);
    check(b.draw(), "new sector card");
    require(m.fog_cards_.warmup && !m.fog_cards_.suppressed && b.source() == vanilla, "same_family_sector_rewarm");
    b.end();
    const auto previous_generation = m.fog_sector_.field_generation;
    b.begin("foggreenoutlands", 0x4000);
    check(b.draw(), "new family card");
    require(m.fog_cards_.warmup && !m.fog_cards_.suppressed && m.fog_sector_.profile == 2 &&
                m.fog_sector_.field_generation != previous_generation,
            "new_family_generation_rewarm");
    b.end();
    std::printf("IMAGE legacy_new_family_warm %016llx\n", fnv(readback_words(d, scene.s0.p)));
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        if (scenario == 2) m.volumetric_fog_toggle();
        if (scenario == 3) {
            if (!m.fog_enabled_) m.volumetric_fog_toggle();
            m.fog_strength_ = 0;
        }
        if (scenario == 4) m.fog_strength_ = .02f;
        b.begin(scenario == 0 ? "unsupported" : "bluewell", 0x1000, scenario == 1 ? 0 : 8, scenario != 4);
        const auto applied = m.fog_applied_frames_;
        check(b.draw(), "native refusal card");
        require(b.source() == vanilla && !m.fog_cards_.suppressed,
                ("native_exact_refusal_" + std::to_string(scenario)).c_str());
        const auto copies = hooks.calls[34], quads = hooks.calls[83];
        b.end();
        require(m.fog_applied_frames_ == applied && hooks.calls[34] == copies && hooks.calls[83] == quads,
                "refusal_no_medium");
        require(readback_words(d, scene.s0.p) == vanilla, "refusal_final_RT0_exact");
    }
    // Re-arm, then independently refuse an altered frozen resource generation.
    b.begin();
    check(b.draw(), "rewarm card");
    b.end();
    b.begin();
    ++m.fog_sector_.field_generation;
    check(b.draw(), "stale generation card");
    require(!m.fog_cards_.suppressed && m.fog_cards_.refused && b.source() == vanilla, "stale_generation_native");
    b.end();
    b.begin();
    check(b.draw(), "generation recover warmup");
    b.end();
    b.begin();
    check(b.draw(), "query fault suppress");
    require(m.fog_cards_.suppressed == 1, "query_fault_suppressed_first");
    Com<IDirect3DQuery9> query;
    check(d->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query.p), "late query");
    check(query->Issue(D3DISSUE_BEGIN), "late query begin");
    m.active_queries_ = 1;
    m.run_volumetric_fog();
    require(m.fog_cards_.fault && m.fog_card_mode_ == 3, "late_query_fault_latched");
    check(query->Issue(D3DISSUE_END), "late query end");
    m.active_queries_ = 0;
    b.end();
    m.volumetric_fog_toggle();
    m.volumetric_fog_toggle();
    b.begin();
    check(b.draw(), "fault native card");
    require(b.source() == vanilla && m.fog_cards_.fault && !m.fog_cards_.suppressed, "F9_cannot_erase_fault");
    b.end();
    // This witness exercises the actual pass Reset edges and policy reset;
    // the frozen spatial state fixture separately executes native device Reset.
    m.fog_->before_reset();
    m.fog_sector_ = {};
    m.fog_cards_ = {};
    m.fog_attach_failed_ = false;
    ++m.generation_;
    m.fog_->after_reset(S_OK);
    b.begin();
    check(b.draw(), "reset warmup card");
    require(m.fog_cards_.warmup && !m.fog_cards_.suppressed, "reset_requires_warmup");
    b.end();
    b.begin();
    hooks.fault = {82, 1, E_FAIL};
    const auto submissions = b.submitted;
    const HRESULT failed = b.draw();
    require(failed == E_FAIL && b.submitted == submissions + 1 && hooks.calls[82] == 1 && m.fog_cards_.fault,
            "source_HRESULT_count_fault");
    b.end();
    // A real borrowed-scene FogPass failure must cross the unchanged result
    // reconciliation method. Both reopen attempts fail before the backend;
    // native scene is closed, while the old logical latch is deliberately kept.
    m.fog_->before_reset();
    m.fog_sector_ = {};
    m.fog_cards_ = {};
    m.fog_attach_failed_ = false;
    ++m.generation_;
    m.fog_->after_reset(S_OK);
    b.begin();
    check(b.draw(), "failure witness rewarm");
    b.end();
    b.begin();
    check(b.draw(), "failure witness suppress");
    require(m.fog_cards_.suppressed == 1, "execution_failure_suppressed_first");
    check(d->SetRenderTarget(1, scene.s1.p), "failure RT1");
    check(d->SetRenderTarget(2, scene.s2.p), "failure RT2");
    const fog_spatial_state::Snapshot failure_before(d, caps);
    const auto applied = m.fog_applied_frames_;
    const auto copies = hooks.calls[34], quads = hooks.calls[83];
    hooks.fault = {41, hooks.calls[41] + 1, E_FAIL};
    hooks.fault2 = {41, hooks.calls[41] + 2, E_FAIL};
    m.run_volumetric_fog();
    require(hooks.injected_count == 2 && hooks.calls[34] == copies + 1 && hooks.calls[83] == quads &&
                m.fog_applied_frames_ == applied,
            "actual_pass_reopen_failure");
    require(m.motion_state_lost_ && m.motion_state_error_ == E_FAIL && m.scene_open_ && !hooks.native_scene_open &&
                m.fog_cards_.fault,
            "actual_result_reconciles_unknown_scene_poison");
    require(failure_before == fog_spatial_state::Snapshot(d, caps), "failed_pass_bindings_restored");
    require(readback_words(d, scene.s0.p) == inputs.scene, "failed_pass_source_clean");
    require(fog_spatial_state::surface_bytes(d, scene.s1.p, 8) == b.rt1_before &&
                fog_spatial_state::surface_bytes(d, scene.s2.p, 16) == b.rt2_before,
            "failed_pass_RT1_RT2_bytes");
    const auto writes = hooks.writes();
    m.run_volumetric_fog();
    require(hooks.writes() == writes, "poisoned_fallback_no_second_transaction");
    m.volumetric_fog_toggle();
    m.volumetric_fog_toggle();
    require(m.motion_state_lost_ && m.fog_cards_.fault && m.scene_open_ && !hooks.native_scene_open,
            "execution_poison_survives_F9");
    // Synthetic owner Reset completion, matching the already-host-tested reset
    // method; actual native Reset/lifetime is the separate state fixture scope.
    m.fog_->before_reset();
    m.fog_sector_ = {};
    m.fog_cards_ = {};
    m.fog_attach_failed_ = false;
    m.motion_state_lost_ = false;
    m.motion_state_error_ = S_OK;
    m.scene_open_ = false;
    ++m.generation_;
    m.fog_->after_reset(S_OK);
    b.begin();
    check(b.draw(), "poison Reset warmup");
    require(m.fog_cards_.warmup && !m.fog_cards_.suppressed && !m.motion_state_lost_, "execution_poison_reset_rewarm");
    b.end();
    require(m.fog_applied_frames_ == applied + 1 && m.fog_cards_.armed, "execution_poison_reset_applies");
    std::printf("IMAGE legacy_after_reset %016llx\n", fnv(readback_words(d, scene.s0.p)));
#ifndef X3M_ROUTE_BRIDGE_BASELINE
    require(!m.fog_->fixture_density_cache() && !m.fog_->density_status().available &&
                m.fog_->density_status().nodes_generated == 0,
            "legacy_route_never_starts_density");
#endif
    std::printf(
        "BRIDGE_SCOPE methods=production_fog_fragment native_d3d=1 synthetic_owner=1 cached_shader_identity=synthetic selector_hook=not_exercised taa_history=request_endpoint_only native_reset=reused_state_fixture\n");
}
#ifndef X3M_ROUTE_BRIDGE_BASELINE
#include "fog_route_density_inc.h"
#endif
}
int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: fog_route_bridge.exe case-list\n");
        return 2;
    }
    try {
        WNDCLASSA cls{};
        cls.lpfnWndProc = DefWindowProcA;
        cls.hInstance = GetModuleHandleA(nullptr);
        cls.lpszClassName = "X3FogRouteBridge";
        RegisterClassA(&cls);
        Window window;
        window.handle = CreateWindowA(cls.lpszClassName, "Fog route bridge", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
                                      nullptr, nullptr, cls.hInstance, nullptr);
        if (!window.handle) throw std::runtime_error("window");
        HMODULE dll = LoadLibraryA("d3d9.dll");
        if (!dll) throw std::runtime_error("d3d9");
        auto address = GetProcAddress(dll, "Direct3DCreate9");
        IDirect3D9*(WINAPI * create)(UINT) = nullptr;
        std::memcpy(&create, &address, sizeof create);
        if (!create) throw std::runtime_error("Create9");
        Com<IDirect3D9> api;
        api.p = create(D3D_SDK_VERSION);
        if (!api.p) throw std::runtime_error("factory");
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window.handle;
        pp.BackBufferWidth = 128;
        pp.BackBufferHeight = 128;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        Com<IDirect3DDevice9> device;
        check(api->CreateDevice(0, D3DDEVTYPE_HAL, window.handle, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p),
              "device");
        D3DCAPS9 caps{};
        check(device->GetDeviceCaps(&caps), "caps");
        qualify_bridge(api.p, device.p, caps, argv[1]);
#ifndef X3M_ROUTE_BRIDGE_BASELINE
        {
            fog_spatial_state::Inputs inputs(argv[1]);
            DensityRun{api.p, device.p, caps, inputs}.run();
            std::puts(
                "DENSITY_SCOPE range=stored worker=real_thread cache=dynamic owner=synthetic native_reset=pass_edges_only process_exit=separate_exit_fixture");
        }
#endif
        std::printf("RESULT fog_route_bridge checks=%u PASS\n", checks);
        return 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL error=%s\n", e.what());
        return 1;
    }
}
