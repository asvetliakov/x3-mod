// Included in the actual D3D Fixture. Engine light seam case (seam-engine-light; docs/architecture/engine-light.md,
// docs/verification/engine-light.md): the route-level path of the hull light through the production MotionOutput,
// X3M_ENGINE_EFFECTS=plumes X3M_ENGINE_LIGHT=on with the HDR scene, TAA off, X3M_ORIGINAL_FILL=0.01 and
// X3M_HULL_LIGHTMAP_GAIN=4 (the bound base is the gained fill variant, twin kind 2).
//
// Per frame, in the scene phase: one glow-jet draw of the effects pair (vs_d5e1c75351ed3f04 / ps_8360f422de08b5bd,
// blended, Z-write off) on a synthetic JET node whose node+0x18 is a synthetic ship root, suppressed and recorded by
// the production hook; then the reviewed hull pair vs_494fe349b8bc12ec / ps_7c83ed50c9894e44 drawn twice over the same
// quad (z 0.5, normal -z towards the camera, albedo white, mask and light map black): first as node B of another ship
// (unlit), then as node A hanging under the jets' root (lit from the third jet frame on: the ship table from frame
// N-1's record, the node table from frame N-1's logged draw). The VS rows are consistent: world identity (c28-30),
// camera at (0, 0, -4) with the identity basis (view inverse c34-36), clip w = view depth 4.5 (c24-27). The light: a
// record at (0.3, 0.2, 0.2), axis -z, size 0.4, throttle 0 -> L = (0.3, 0.2, 0), R = 1.2, colour 0.3 (white x 1.2 x
// 0.25). Checks per frame: the bound program (key 500: the twin of kind 2 on a lit A, the gained base on B), c200-c202
// untouched by B (the application's sentinel stays) and the record law on A, the frame's counts (keys 501-504: one
// candidate, one upload), the lit image against the unlit one by the law, the zero region bit-identical, state
// restored after each routed draw. Then a Reset and four more frames (the twins and tables survive; the rows are
// resynchronised). The runner reads the ENGINE_LIGHT rows and the session log's engine_light_* rows.
void run_engine_light(const char* bootstrap_vertex) {
    require(seam && enabled && hdr && hdr_readback && !taa && camera && emission_status != nullptr,
            "engine light seam needs the HDR seam, its readback, the camera, TAA off and the status export");
    using EffectsStatus = unsigned (*)(IDirect3DDevice9*, unsigned);
    const auto effects_status = symbol<EffectsStatus>(runtime, "x3m_engine_effects_fixture_status", true);
    require(emission_status(d.p, 508) == 1, "the engine light is requested on this device");
    const std::string bootstrap_path(bootstrap_vertex);
    const auto slash = bootstrap_path.find_last_of("/\\");
    const auto folder = slash == std::string::npos ? std::string{} : bootstrap_path.substr(0, slash + 1);
    const auto hv = load((folder + "vs_494fe349b8bc12ec.bin").c_str()), hp = load((folder + "ps_7c83ed50c9894e44.bin").c_str());
    const auto ev = load((folder + "vs_d5e1c75351ed3f04.bin").c_str()), ep = load((folder + "ps_8360f422de08b5bd.bin").c_str());
    require(fnv(hv.data(), hv.size() * 4) == 0x494fe349b8bc12ecull && fnv(hp.data(), hp.size() * 4) == 0x7c83ed50c9894e44ull,
            "hull original pair");
    require(fnv(ev.data(), ev.size() * 4) == 0xd5e1c75351ed3f04ull && fnv(ep.data(), ep.size() * 4) == 0x8360f422de08b5bdull,
            "effects original pair");
    Com<IDirect3DVertexShader9> hull_vs, jet_vs;
    Com<IDirect3DPixelShader9> hull_ps, jet_ps;
    api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(hv.data()), &hull_vs.p), "hull VS");
    const unsigned twins_before = emission_status(d.p, 505);
    api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(hp.data()), &hull_ps.p), "hull PS");
    const unsigned twins_hull = emission_status(d.p, 505) - twins_before;
    api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(ev.data()), &jet_vs.p), "effects VS");
    api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(ep.data()), &jet_ps.p), "effects PS");
    std::printf("ENGINE_LIGHT_TWINS hull=%u effects=%u total=%u\n", twins_hull,
                emission_status(d.p, 505) - twins_before - twins_hull, emission_status(d.p, 505));
    // Registration: the motion variant, the fill variant (K 0.01) and the gained variant each get their twin.
    require(twins_hull == 3, "three twins for the hull program (motion, fill, gained)");
    Com<IDirect3DTexture9> white, black;
    for (unsigned which = 0; which < 2; ++which) {
        auto& t = which ? black : white;
        api(d->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t.p, nullptr), "engine light texture");
        D3DLOCKED_RECT locked{};
        api(t->LockRect(0, &locked, nullptr, 0), "engine light texture lock");
        *static_cast<DWORD*>(locked.pBits) = which ? 0xff000000u : 0xffffffffu;
        api(t->UnlockRect(0), "engine light texture unlock");
    }
    // The quad: x, y in [-0.8, 0.8] at z 0.5, UV 0..1, normal (0, 0, -1) (towards the camera at z -4).
    Com<IDirect3DVertexBuffer9> quad;
    api(d->CreateVertexBuffer(24 * 6, 0, 0, D3DPOOL_MANAGED, &quad.p, nullptr), "engine light quad");
    {
        void* dst = nullptr;
        api(quad->Lock(0, 0, &dst, 0), "engine light quad lock");
        const float corners[6][2] = {{-.8f, .8f}, {.8f, .8f}, {-.8f, -.8f}, {.8f, .8f}, {.8f, -.8f}, {-.8f, -.8f}};
        for (unsigned i = 0; i < 6; ++i) {
            const float x = corners[i][0], y = corners[i][1];
            const unsigned short v[12] = {half(x), half(y), half(.5f), half(1), half(x * .5f + .5f), half(.5f - y * .5f), 0, 0,
                                          0, 0, half(-1), 0};
            std::memcpy(static_cast<char*>(dst) + i * 24, v, 24);
        }
        api(quad->Unlock(), "engine light quad unlock");
    }
    // The ship: a root node (radius +0xa4 0: no plume floor) and one main jet under it (+0x18), identity basis, +0x88
    // throttle 0.25 (s 0), the c4-6 rows of size 0.4 at (0.3, 0.2, 0.2) (register order a: row i = (X_i, Y_i, Z_i, t_i)).
    alignas(16) static std::uint32_t root[0x150 / 4];
    alignas(16) static std::uint32_t jet[0x150 / 4];
    std::memset(root, 0, sizeof root);
    std::memset(jet, 0, sizeof jet);
    const float k = .4f, zt = .25f, t[3] = {.3f, .2f, .2f};
    jet[0x18 / 4] = std::uint32_t(reinterpret_cast<std::uintptr_t>(root));
    jet[0x28 / 4] = 0x5a;
    jet[0x70 / 4] = 1000u << 16;
    jet[0x80 / 4] = jet[0x84 / 4] = 0x10000;
    jet[0x88 / 4] = std::uint32_t(std::int32_t(std::lround(zt * 65536.f)));
    jet[0xc0 / 4] = jet[0xd0 / 4 + 1] = jet[0xe0 / 4 + 2] = 0x10000; // basis rows 16.16: identity
    jet[0x130 / 4] = 0x4000001u;
    jet[0x140 / 4] = 20000;
    const float jet_rows[12] = {k, 0, 0, t[0], 0, k, 0, t[1], 0, 0, k * zt, t[2]};
    const std::uint32_t root_va = std::uint32_t(reinterpret_cast<std::uintptr_t>(root));
    x3m::MotionOutputFixtureScope jet_scope{}, lit{}, unlit{};
    jet_scope.known = 1;
    jet_scope.node = reinterpret_cast<std::uintptr_t>(jet);
    jet_scope.node_serial = 41;
    jet_scope.camera_handle = 9;
    lit.known = unlit.known = 1;
    lit.load_epoch = unlit.load_epoch = 1;
    lit.registry_epoch = unlit.registry_epoch = 3;
    lit.camera_serial = unlit.camera_serial = 21;
    lit.camera = unlit.camera = 0x2000;
    lit.registry = unlit.registry = 0x3000;
    lit.camera_handle = unlit.camera_handle = 9;
    lit.lod = unlit.lod = 2;
    lit.node_serial = 31;
    lit.node = 0x7100;
    lit.mesh = 0x7200;
    lit.node_handle = 0x71;
    lit.model = 0x11;
    lit.parent = root_va; // node A hangs directly under the jets' root
    unlit.node_serial = 32;
    unlit.node = 0x7300;
    unlit.mesh = 0x7400;
    unlit.node_handle = 0x73;
    unlit.model = 0x12;
    unlit.parent = 0x7500; // another ship, no jets
    const auto engine_scope = [&](const x3m::MotionOutputFixtureScope& s) {
        x3m::MotionOutputFixtureConfig config{};
        config.background_vs[0] = vs_hash;
        config.background_ps[0] = flat_hash;
        config.scope = s;
        configure(&config);
    };
    // The VS rows: world identity, the view inverse (identity basis, camera (0, 0, -4)), clip = (4.5 x, 4.5 y, z, 9 z):
    // NDC = (x, y) and clip w = 4.5 = the view depth of the plane. Only x, y, z columns: the position's w is unused.
    const float cam[3] = {0, 0, -4};
    const float wvp[16] = {4.5f, 0, 0, 0, 0, 4.5f, 0, 0, 0, 0, 1, 0, 0, 0, 9, 0};
    const float world[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const float view_inverse[12] = {1, 0, 0, cam[0], 0, 1, 0, cam[1], 0, 0, 1, cam[2]};
    const float sentinel[12] = {-7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7};
    // The record law (engine_light_core.h, computed here from the jet's geometry): L = t + axis x 0.5 x size, axis
    // -(model z) = -z; R = 3 x size; colour = white x I(0) 1.248 (core_low, 1.2 until flight F) x preset 1 x 0.25.
    const double size = k, R = 3. * size, L[3] = {t[0], t[1], t[2] - .5 * size};
    const double expected[12] = {L[0] - cam[0], L[1] - cam[1], L[2] - cam[2], R * R, .312, .312, .312, 1. / (R * R), 0, 0, 1, 0};
    unsigned lit_frames = 0, checked_images = 0, first_lit_after_reset = 0;
    double worst_relative = 0, worst_constant = 0;
    unsigned zero_differ = 0, zero_total = 0, visible_total = 0, darker = 0;
    const unsigned frames = 9, reset_after = 4; // frames 0..3, Reset, frames 4..8
    for (unsigned f = 0; f < frames; ++f) {
        if (f == reset_after) reset();
        frame_begin();
        // The glow jet, scene phase: suppressed and recorded by the production hook (the ring's one record).
        api(d->SetVertexShader(jet_vs.p), "effects VS bind");
        api(d->SetPixelShader(jet_ps.p), "effects PS bind");
        api(d->SetVertexShaderConstantF(4, jet_rows, 3), "jet rows c4-6");
        api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE), "jet blend");
        api(d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE), "jet src");
        api(d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR), "jet dest");
        api(d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE), "jet zwrite");
        engine_scope(jet_scope);
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "jet draw");
        ++draw_index;
        const unsigned records = effects_status(d.p, 2), suppressed = effects_status(d.p, 3);
        api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE), "hull blend");
        api(d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE), "hull zwrite");
        // The hull: material_state again (the jet's c4-6 overwrote light rows), then the case's rows and textures.
        material_state();
        api(d->SetVertexDeclaration(declaration.p), "hull declaration");
        api(d->SetStreamSource(0, quad.p, 0, 24), "hull stream");
        api(d->SetVertexShader(hull_vs.p), "hull VS bind");
        api(d->SetPixelShader(hull_ps.p), "hull PS bind");
        api(d->SetTexture(0, white.p), "albedo white");
        api(d->SetTexture(1, black.p), "mask black");
        api(d->SetTexture(2, black.p), "light map black");
        api(d->SetVertexShaderConstantF(24, wvp, 4), "hull rows c24-27");
        api(d->SetVertexShaderConstantF(28, world, 3), "hull world c28-30");
        api(d->SetVertexShaderConstantF(34, view_inverse, 3), "hull view inverse c34-36");
        api(d->SetPixelShaderConstantF(200, sentinel, 3), "application c200-c202");
        // B: another ship's node, never lit.
        engine_scope(unlit);
        Snapshot before = snapshot();
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2), "unlit hull draw");
        ++draw_index;
        compare(before, snapshot(), "engine light unlit draw");
        const unsigned bound_unlit = emission_status(d.p, 500), lit_after_unlit = emission_status(d.p, 502);
        float after_unlit[12]{};
        api(d->GetPixelShaderConstantF(200, after_unlit, 3), "c200 after the unlit draw");
        unsigned w = 0, h = 0;
        const std::vector<float> base = hdr_image(&w, &h);
        require(w == W && h == H, "the FP16 target matches the main dimensions");
        // A: the node under the lit root.
        engine_scope(lit);
        before = snapshot();
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2), "lit hull draw");
        ++draw_index;
        compare(before, snapshot(), "engine light lit draw");
        const unsigned bound_lit = emission_status(d.p, 500);
        const unsigned candidates = emission_status(d.p, 501), draws_lit = emission_status(d.p, 502),
                       no_twin = emission_status(d.p, 503), no_rows = emission_status(d.p, 504);
        const unsigned ships = emission_status(d.p, 506), nodes = emission_status(d.p, 507);
        float constants[12]{};
        api(d->GetPixelShaderConstantF(200, constants, 3), "c200 after the lit draw");
        const std::vector<float> image = hdr_image(&w, &h);
        api(d->EndScene(), "EndScene");
        const bool lit_frame = draws_lit == 1;
        const bool sentinel_kept = std::memcmp(after_unlit, sentinel, sizeof sentinel) == 0;
        double constant_error = 0;
        if (lit_frame)
            for (unsigned i = 0; i < 12; ++i)
                constant_error = std::max(constant_error, std::fabs(double(constants[i]) - expected[i]) / std::max(1., std::fabs(expected[i])));
        // The image: every pixel centre inside the quad (|x|, |y| <= 0.75 in NDC).
        unsigned visible = 0, zero = 0, zero_bad = 0, lit_samples = 0, not_brighter = 0;
        double max_relative = 0;
        for (UINT y = 0; y < H; ++y)
            for (UINT x = 0; x < W; ++x) {
                const double nx = 2. * x / W - 1., ny = 1. - 2. * y / H;
                if (std::fabs(nx) > .75 || std::fabs(ny) > .75) continue;
                const std::size_t at = (std::size_t(y) * W + x) * 4;
                bool identical = true;
                for (unsigned c = 0; c < 4; ++c) identical = identical && base[at + c] == image[at + c];
                if (!lit_frame) {
                    zero_bad += identical ? 0u : 1u; // nothing lit yet: A is B's image bit for bit
                    continue;
                }
                const double rel[3] = {nx - cam[0], ny - cam[1], .5 - cam[2]};
                const double l[3] = {double(constants[0]) - rel[0], double(constants[1]) - rel[1], double(constants[2]) - rel[2]};
                const double d2 = l[0] * l[0] + l[1] * l[1] + l[2] * l[2];
                const double ndl = std::min(std::max(-l[2] / std::sqrt(d2), 0.), 1.);
                const double r2 = double(constants[3]);
                if (d2 > r2 * 1.004 || ndl == 0.) {
                    ++zero;
                    zero_bad += identical ? 0u : 1u;
                    continue;
                }
                if (d2 > r2 * .996) continue; // the pixel-centre band at the radius: no claim
                const double q = std::min(std::max(1. - d2 / r2, 0.), 1.);
                ++lit_samples;
                for (unsigned c = 0; c < 3; ++c) {
                    const double u = std::max(double(base[at + c]), 0.), m = std::max(double(image[at + c]), 0.);
                    const double ud = std::pow(u, 2.2);
                    const double e = std::min(double(constants[4 + c]) * ndl * q * q, std::max(0., 1. - ud));
                    if (m < u) ++not_brighter;
                    if (e < .02) continue;
                    ++visible;
                    max_relative = std::max(max_relative, std::fabs((std::pow(m, 2.2) - ud) - e) / e);
                }
                not_brighter += base[at + 3] == image[at + 3] ? 0u : 1u; // alpha is the base's
            }
        std::printf("ENGINE_LIGHT frame=%llu step=%u reset=%u records=%u suppressed=%u ships=%u nodes=%u candidates=%u draws_lit=%u no_twin=%u no_rows=%u bound_unlit=%03x bound_lit=%03x lit_after_unlit=%u sentinel_kept=%u constant_error=%.3g constants=%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g lit_samples=%u visible=%u max_relative=%.6f zero=%u zero_differ=%u not_brighter=%u\n",
                    frame, f, unsigned(f >= reset_after), records, suppressed, ships, nodes, candidates, draws_lit, no_twin, no_rows,
                    bound_unlit, bound_lit, lit_after_unlit, unsigned(sentinel_kept), constant_error, double(constants[0]),
                    double(constants[1]), double(constants[2]), double(constants[3]), double(constants[4]), double(constants[5]),
                    double(constants[6]), double(constants[7]), double(constants[8]), double(constants[9]), double(constants[10]),
                    double(constants[11]), lit_samples, visible, max_relative, zero, zero_bad, not_brighter);
        require(records == 1 && suppressed == 1, "the jet is recorded and suppressed");
        require(bound_unlit == 0x202u, "B binds the gained base (kind 2)");
        require(lit_after_unlit == 0 && sentinel_kept, "B uploads nothing: c200-c202 keep the application's values");
        // Before the Reset the protocol is exact: the third jet frame is the first lit one.
        if (f < reset_after) require(lit_frame == (f >= 2), "lit from the third jet frame on");
        if (f >= reset_after && lit_frame && !first_lit_after_reset) first_lit_after_reset = f - reset_after + 1;
        if (f + 2 >= frames) require(lit_frame, "lit again after the Reset");
        if (lit_frame) {
            ++lit_frames;
            require(bound_lit == 0x102u, "A binds the twin of the gained base");
            require(candidates == 1 && draws_lit == 1 && no_twin == 0 && no_rows == 0, "one candidate, one upload");
            require(constant_error <= 1e-5, "c200-c202 carry the record law");
            require(visible >= 200 && max_relative <= .05, "A is brighter than B by the law within 5 %");
            require(zero >= 50 && zero_bad == 0, "beyond the radius A is B's image bit for bit");
            require(not_brighter == 0, "no lit sample darker than the base, alpha unchanged");
            worst_relative = std::max(worst_relative, max_relative);
            worst_constant = std::max(worst_constant, constant_error);
            zero_differ += zero_bad;
            zero_total += zero;
            visible_total += visible;
            ++checked_images;
        } else {
            require(bound_lit == 0x202u && draws_lit == 0, "an unlit A binds the base and uploads nothing");
            require(zero_bad == 0, "unlit A is B's image bit for bit");
            darker += zero_bad;
        }
        api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++frame;
        ++frames_since_reset;
    }
    std::printf("ENGINE_LIGHT_RESULT lit_frames=%u checked_images=%u first_lit_after_reset=%u worst_relative=%.6f worst_constant=%.3g visible=%u zero=%u zero_differ=%u unlit_differ=%u\n",
                lit_frames, checked_images, first_lit_after_reset, worst_relative, worst_constant, visible_total, zero_total,
                zero_differ, darker);
    for (UINT i = 0; i < 4; ++i) api(d->SetTexture(i, nullptr), "unbind engine light textures");
    api(d->SetStreamSource(0, nullptr, 0, 0), "unbind engine light quad");
    api(d->SetVertexShader(nullptr), "unbind engine light VS");
    api(d->SetPixelShader(nullptr), "unbind engine light PS");
}
