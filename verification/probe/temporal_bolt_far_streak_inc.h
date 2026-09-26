// Bolts through the TAA (docs/architecture/bolts-through-taa.md, B'; the Run 93 A finding of lod-overlay.md), lattice mode.
// Included after temporal_far_jitter_line_inc.h: uses halton, EdgeScene and the metric helpers.
//   BOLT_FAR_STREAK: a 64 x 16 scene, static camera, no jitter. Left half: a far, routed hull (four-channel lane .r = z/w inside the
//   farw = 1 band at view z 100,000 units, .g = share 0.5, .b = w, .a = 1: no thin vote) carrying a static checker (0.3 / 0.5,
//   alpha 1); right half: the depth sentinel (-1, -1, -1, -1), dark space (0.02, alpha 0). The camera-gate resolve at the
//   launcher defaults (history weight 0.9, far weight 0.985 on the camera gate, 7x7 far clip, thin region 0.97, alpha history
//   on) runs 24 warm-up frames, then frame N carries a one-frame additive streak (+0.4, +0.2, +0.1, alpha unchanged) on rows
//   6-9 across both halves and a bright non-bolt addition of the same colour on rows 12-13 over the hull, then 8 plain frames.
//   Two runs per W, identical except that the flagged run's lane .g carries + 25.6 (K = 32 times a gained max(rgb) of 0.8) on
//   the streak rows of frame N, exactly what the late bullet draw's variant adds under its ONE/ONE blend. The write-back is the
//   embedded dithered identity program (hdr_writeback_dither_ps.hlsl: s0 the resolved image, s2 the frame-N scene, c29.x = W)
//   drawn into an FP16 target after frame N of each run; the dither is the same static pattern in both runs, so their
//   difference is the composite alone.
//   Rows (per W, with the alpha history off (the default flight: X3M_TAA_ALPHA_HISTORY builtin 0) and on): flagged_px = pixels whose flagged-run alpha is <= -1 (expected 128: the streak over the hull only; the same
//   addition over space and the non-bolt band never flag); alpha_min / alpha_max there (-(1 + held), held the far weight's share);
//   color_diff / age_diff / depth_diff = bytes of the frame-N colour, age and depth-history targets that differ between the
//   runs (0: every pixel bit-identical, the flag lives in the output alpha only); alpha_outside_diff = alpha bytes differing
//   outside the flagged pixels (0); wb_space_diff / wb_far_diff = write-back bytes differing between the runs over space and over
//   the unflagged hull (0); wb_err_codes = the largest |delta - W * share * (scene - resolved)| over the flagged pixels in 1/255
//   codes (the streak shows at W of the strength the resolve dropped); far_out_add / space_out_add = the flagged run's mean
//   write-back addition over the streak (output minus the pre-streak output) over the hull and over space, and their ratio;
//   after_diff = bytes of the colour rgb lanes, age and depth targets differing between the runs over frames N+1..N+8 (0: no
//   residual beyond today's); after_alpha_px = pixels whose frame N+1..N+8 alpha differs, after_alpha_frames = the frames after N
//   with any such pixel: 0 with the alpha history off (the flagged alpha is never read back); with it on, a flagged texel's
//   negative history alpha enters the neighbours' alpha blend, clamped to the current 3x3 alpha range, which at the hull /
//   space silhouette spans [0, 1], so the silhouette pixels beside the streak carry a decaying alpha residual (colour, age and
//   depth never): reported, the limitation of the opt-in alpha history.
namespace bolt_far_streak {
void bolt_far_streak_cases(IDirect3DDevice9* d, Compiler compiler, const DWORD* resolver) {
    std::puts("BOLT_FAR_STREAK_CASES");
    constexpr UINT W = 64, H = 16;
    constexpr unsigned warm = 24, after = 8, frames = warm + 1 + after, streakFrame = warm;
    constexpr UINT S0 = 6, S1 = 10, B0 = 12, B1 = 14; // the streak rows and the non-bolt band rows
    constexpr float p00 = .4999979f, p22 = 1.00000298f, p32 = -6.00001812f, viewZ = 100000.f;
    float gateD0 = 0, gateInv = 0;
    require(x3::temporal::far_gate(p00, p22, p32, 5120, 60.f, 68.f, gateD0, gateInv), "bfs far gate of the production footprints");
    const float hullDepth = float(double(p22) + double(p32) / double(viewZ));
    require((hullDepth - gateD0) * gateInv >= 1.f, "bfs hull depth inside the farw = 1 band");
    struct Defer { Defer() { deferMetrics = true; deferredFailures.clear(); } ~Defer() { deferMetrics = false; } } defer;
    EdgeScene s(d, compiler); // the state setup and the quad; its own 32x32 targets are not used
    Com<IDirect3DTexture9> color, lane, motion, writeback;
    Com<IDirect3DSurface9> writebackSurface;
    check("bfs color", d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &color.p, nullptr));
    check("bfs lane", d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &lane.p, nullptr));
    check("bfs motion", d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &motion.p, nullptr));
    check("bfs writeback", d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &writeback.p, nullptr));
    check("bfs writeback surface", writeback->GetSurfaceLevel(0, &writebackSurface.p));
    Com<IDirect3DPixelShader9> writebackPS;
    check("bfs writeback PS (the embedded dithered identity write-back)",
          d->CreatePixelShader(reinterpret_cast<const DWORD*>(x3m::renderer::hdr_writeback_dither_program()), &writebackPS.p));
    auto upload = [&](IDirect3DTexture9* target, D3DFORMAT format, UINT pixel, auto texel) {
        Com<IDirect3DTexture9> staging;
        check("bfs staging", d->CreateTexture(W, H, 1, 0, format, D3DPOOL_SYSTEMMEM, &staging.p, nullptr));
        D3DLOCKED_RECT lock{};
        check("bfs staging lock", staging->LockRect(0, &lock, nullptr, 0));
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) texel(x, y, static_cast<char*>(lock.pBits) + y * lock.Pitch + x * pixel);
        check("bfs staging unlock", staging->UnlockRect(0));
        check("bfs upload", d->UpdateTexture(staging.p, target));
    };
    auto bytesOf = [&](IDirect3DTexture9* texture) {
        D3DSURFACE_DESC desc{};
        check("bfs desc", texture->GetLevelDesc(0, &desc));
        const UINT pixel = desc.Format == D3DFMT_A16B16G16R16F ? 8 : desc.Format == D3DFMT_A32B32G32R32F ? 16 : 4;
        Com<IDirect3DSurface9> level, read;
        check("bfs level", texture->GetSurfaceLevel(0, &level.p));
        check("bfs readback surface", d->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &read.p, nullptr));
        check("bfs readback", d->GetRenderTargetData(level.p, read.p));
        D3DLOCKED_RECT lock{};
        check("bfs readback lock", read->LockRect(&lock, nullptr, D3DLOCK_READONLY));
        std::vector<unsigned char> out(std::size_t(desc.Width) * desc.Height * pixel);
        for (UINT y = 0; y < desc.Height; ++y) std::memcpy(&out[std::size_t(y) * desc.Width * pixel], static_cast<char*>(lock.pBits) + y * lock.Pitch, std::size_t(desc.Width) * pixel);
        check("bfs readback unlock", read->UnlockRect());
        return out;
    };
    const auto hull = [](UINT x, UINT y) { return x < W / 2 ? (((x + y) & 1) ? .5f : .3f) : .02f; };
    const auto streak = [](UINT x, UINT y, unsigned n, unsigned c) { return n == streakFrame && ((y >= S0 && y < S1) || (y >= B0 && y < B1 && x < W / 2)) ? (c == 0 ? .4f : c == 1 ? .2f : .1f) : 0.f; };
    const auto sceneAlpha = [](UINT x) { return x < W / 2 ? 1.f : 0.f; };
    auto uploadFrame = [&](unsigned n, bool flagged) {
        upload(color.p, D3DFMT_A16B16G16R16F, 8, [&](UINT x, UINT y, char* p) {
            unsigned short v[4];
            for (unsigned c = 0; c < 3; ++c) v[c] = toHalf(hull(x, y) + streak(x, y, n, c));
            v[3] = toHalf(sceneAlpha(x));
            std::memcpy(p, v, 8);
        });
        upload(lane.p, D3DFMT_A32B32G32R32F, 16, [&](UINT x, UINT y, char* p) {
            const bool farSide = x < W / 2;
            const float flag = flagged && n == streakFrame && y >= S0 && y < S1 ? 32.f * .8f : 0.f; // the variant's K * max(rgb)
            const float v[4] = {farSide ? hullDepth : -1.f, (farSide ? .5f : -1.f) + flag, farSide ? viewZ : -1.f, farSide ? 1.f : -1.f};
            std::memcpy(p, v, 16);
        });
    };
    upload(motion.p, D3DFMT_A32B32G32R32F, 16, [&](UINT, UINT, char* p) { const float m[4] = {0, 0, 0, 0}; std::memcpy(p, m, 16); });
    struct Run { std::vector<std::vector<unsigned char>> color, age, depth; std::vector<unsigned char> writeback, sceneN; };
    auto sequence = [&](bool flagged, float show, bool alphaHistory) {
        TemporalPass pass;
        check("bfs initialize", pass.initialize(d, nullptr, resolver, nullptr, nullptr, reinterpret_cast<const DWORD*>(x3m::renderer::hdr_writeback_program())));
        check("bfs configure flicker", pass.configure_flicker());
        check("bfs configure far", pass.configure_far());
        require(pass.far_available() && pass.camera_gate_available() && pass.flicker_available(), "bfs far, camera-gate and flicker programs available");
        Run run;
        for (unsigned n = 0; n < frames; ++n) {
            uploadFrame(n, flagged);
            FrameInputs in;
            in.color = color.p; in.current_depth = lane.p; in.motion = motion.p; in.width = W; in.height = H; in.epoch = 1;
            const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            std::copy(identity, identity + 16, in.clip_to_previous);
            in.camera_lane_parallax[3] = 1.f; // the lane's .b is the view z (a static camera: no parallax)
            in.weight = .9f; in.motion_policy = MotionPolicy::PerPixel; in.reactive_policy = ReactivePolicy::DerivedFromDepthSentinel;
            in.sentinel_camera = true; in.sentinel_strict_sky = true; in.sky_history_exit_px = .25f; in.history_allowed = true; in.caller_queries_idle = true; in.caller_scene_open = true;
            in.far_weight = .985f; in.far_d0 = gateD0; in.far_inv = gateInv; in.far_speed_lo = x3::temporal::kFarSpeedLo; in.far_speed_hi = x3::temporal::kFarSpeedHi;
            in.thin_region_weight = .97f; in.thin_region_relax = 1; in.thin_region_camera_gate = true; in.thin_region_hold_frames = 8; in.far_camera_gate = true; in.far_clip = x3::temporal::kFarClipThreshold;
            in.alpha_history = alphaHistory;
            Output out;
            check("bfs Begin resolve", d->BeginScene());
            check("bfs resolve", pass.run(in, &out));
            check("bfs End resolve", d->EndScene());
            require(out.color && out.age && out.depth && pass.diagnostics().history_valid && out.used_history == (n > 0), "bfs history follows the sequence");
            run.color.push_back(bytesOf(out.color)); run.age.push_back(bytesOf(out.age)); run.depth.push_back(bytesOf(out.depth));
            if (n == streakFrame) {
                run.sceneN = bytesOf(color.p);
                // The write-back: the embedded dithered identity program over the resolved image, the frame's scene at s2, W at c29.
                s.target(writebackSurface.p);
                D3DVIEWPORT9 vp{0, 0, W, H, 0, 1};
                check("bfs writeback viewport", d->SetViewport(&vp));
                for (DWORD stage : {DWORD(0), DWORD(x3::temporal::kBoltSceneSampler)}) {
                    for (auto filter : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER}) check("bfs writeback point", d->SetSamplerState(stage, filter, D3DTEXF_POINT));
                    check("bfs writeback mip", d->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
                    for (auto address : {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV}) check("bfs writeback clamp", d->SetSamplerState(stage, address, D3DTADDRESS_CLAMP));
                    check("bfs writeback srgb", d->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE));
                }
                check("bfs writeback PS bind", d->SetPixelShader(writebackPS.p));
                const float c29[4] = {show, 0, 0, 0};
                check("bfs writeback c29", d->SetPixelShaderConstantF(x3::temporal::kBoltShowRegister, c29, 1));
                check("bfs writeback s0", d->SetTexture(0, out.color));
                check("bfs writeback s2", d->SetTexture(x3::temporal::kBoltSceneSampler, color.p));
                check("bfs writeback Begin", d->BeginScene());
                s.quad(0, 0, W, H, 0, 0);
                check("bfs writeback End", d->EndScene());
                check("bfs writeback unbind s0", d->SetTexture(0, nullptr));
                check("bfs writeback unbind s2", d->SetTexture(x3::temporal::kBoltSceneSampler, nullptr));
                run.writeback = bytesOf(writeback.p);
            }
        }
        return run;
    };
    auto half = [](const std::vector<unsigned char>& bytes, UINT x, UINT y, unsigned c) { unsigned short h; std::memcpy(&h, &bytes[(std::size_t(y) * W + x) * 8 + c * 2], 2); return double(halfFloat(h)); };
    auto differing = [](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) { unsigned n = a.size() != b.size(); for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) n += a[i] != b[i]; return n; };
    const float shows[2] = {.5f, 1.f};
    for (const bool alphaHistory : {false, true}) for (const float show : shows) {
        const Run plain = sequence(false, show, alphaHistory), flagged = sequence(true, show, alphaHistory);
        unsigned flaggedPx = 0, flagOutside = 0, alphaOutside = 0, wbSpace = 0, wbFar = 0;
        double alphaMin = 1e9, alphaMax = -1e9, wbErr = 0, farAdd = 0, spaceAdd = 0;
        unsigned farN = 0, spaceN = 0;
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            const bool streakRow = y >= S0 && y < S1, farSide = x < W / 2;
            const double a = half(flagged.color[streakFrame], x, y, 3), aPlain = half(plain.color[streakFrame], x, y, 3);
            if (a <= -1) { ++flaggedPx; if (!(streakRow && farSide)) ++flagOutside; alphaMin = std::min(alphaMin, a); alphaMax = std::max(alphaMax, a); }
            else if (a != aPlain) ++alphaOutside;
            for (unsigned c = 0; c < 4; ++c) {
                const double out = half(flagged.writeback, x, y, c), outPlain = half(plain.writeback, x, y, c);
                if (a <= -1) {
                    const double share = std::min(std::max(-a - 1, 0.), 1.), scene = half(flagged.sceneN, x, y, c), resolved = half(flagged.color[streakFrame], x, y, c);
                    const double expected = c < 3 ? show * share * (scene - resolved) : scene - std::min(std::max(resolved, 0.), 1.); // rgb: the composite; alpha: the scene's, both saturated by the dither store
                    if (c < 3) wbErr = std::max(wbErr, std::fabs((out - outPlain) - expected) * 255);
                } else if (out != outPlain) { if (farSide) ++wbFar; else ++wbSpace; }
            }
            if (streakRow) { // the flagged run's output addition over the streak, against the pre-streak resolved image (the output of frame N-1 is the resolved image; the dither is common)
                double add = 0;
                for (unsigned c = 0; c < 3; ++c) add += half(flagged.writeback, x, y, c) - std::min(std::max(half(flagged.color[streakFrame - 1], x, y, c), 0.), 1.);
                if (farSide) { farAdd += add; ++farN; } else { spaceAdd += add; ++spaceN; }
            }
        }
        farAdd /= std::max(farN, 1u); spaceAdd /= std::max(spaceN, 1u);
        const unsigned colorDiff = differing(plain.color[streakFrame], flagged.color[streakFrame]), ageDiff = differing(plain.age[streakFrame], flagged.age[streakFrame]), depthDiff = differing(plain.depth[streakFrame], flagged.depth[streakFrame]);
        // The colour target's alpha bytes differ exactly at the flagged pixels; every other byte of the frame-N colour target is identical.
        unsigned colorRgbDiff = 0;
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) for (unsigned c = 0; c < 3; ++c) colorRgbDiff += half(plain.color[streakFrame], x, y, c) != half(flagged.color[streakFrame], x, y, c);
        unsigned afterDiff = 0, afterAlphaPx = 0, afterAlphaFrames = 0;
        for (unsigned n = streakFrame + 1; n < frames; ++n) {
            afterDiff += differing(plain.age[n], flagged.age[n]) + differing(plain.depth[n], flagged.depth[n]);
            unsigned alphaPx = 0;
            for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
                for (unsigned c = 0; c < 3; ++c) afterDiff += half(plain.color[n], x, y, c) != half(flagged.color[n], x, y, c);
                alphaPx += half(plain.color[n], x, y, 3) != half(flagged.color[n], x, y, 3);
            }
            afterAlphaPx += alphaPx;
            afterAlphaFrames += alphaPx > 0;
        }
        std::printf("BOLT_FAR_STREAK show=%.2f alpha_history=%u flagged_px=%u flag_outside=%u alpha_min=%.6f alpha_max=%.6f color_rgb_diff=%u color_diff=%u age_diff=%u depth_diff=%u alpha_outside_diff=%u wb_space_diff=%u wb_far_diff=%u wb_err_codes=%.4f far_out_add=%.6f space_out_add=%.6f far_over_space=%.4f after_diff=%u after_alpha_px=%u after_alpha_frames=%u after_frames=%u\n",
                    double(show), unsigned(alphaHistory), flaggedPx, flagOutside, alphaMin, alphaMax, colorRgbDiff, colorDiff, ageDiff, depthDiff, alphaOutside, wbSpace, wbFar, wbErr, farAdd, spaceAdd, spaceAdd > 0 ? farAdd / spaceAdd : 0., afterDiff, afterAlphaPx, afterAlphaFrames, after);
        char label[160];
        std::snprintf(label, sizeof label, "bolt far streak, W %.2f, alpha history %s: ", double(show), alphaHistory ? "on" : "off");
        const std::string prefix = label;
        metric((prefix + "the streak over the far hull is flagged (128 pixels, alpha <= -1)").c_str(), flaggedPx, 128, 0);
        metric((prefix + "no flag on a bright non-bolt texel or over space").c_str(), flagOutside, 0, 0);
        metric((prefix + "frame N colour bit-identical without the flag (rgb lanes)").c_str(), colorRgbDiff, 0, 0);
        metric((prefix + "frame N age bit-identical without the flag").c_str(), ageDiff, 0, 0);
        metric((prefix + "frame N depth history bit-identical without the flag").c_str(), depthDiff, 0, 0);
        metric((prefix + "frame N alpha bit-identical outside the flagged pixels").c_str(), alphaOutside, 0, 0);
        metric((prefix + "write-back bit-identical over space").c_str(), wbSpace, 0, 0);
        metric((prefix + "write-back bit-identical over the unflagged hull").c_str(), wbFar, 0, 0);
        metric((prefix + "write-back shows the streak at W of the dropped strength (max error, codes; within 1)").c_str(), std::max(wbErr - 1, 0.), 0, 0);
        metric((prefix + "frames N+1..N+8 colour rgb, age and depth bit-identical (no residual beyond today's)").c_str(), afterDiff, 0, 0);
        if (!alphaHistory) metric((prefix + "frames N+1..N+8 alpha bit-identical (the flag is never read back)").c_str(), afterAlphaPx, 0, 0);
        (void)colorDiff;
    }
    if (!deferredFailures.empty()) throw std::runtime_error(deferredFailures.front());
}
} // namespace bolt_far_streak
using bolt_far_streak::bolt_far_streak_cases;
