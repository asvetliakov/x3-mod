// Bolts through the TAA (docs/architecture/bolts-through-taa.md, B'; the Run 93 A finding of lod-overlay.md), lattice mode.
// Included after temporal_far_jitter_line_inc.h: uses halton, EdgeScene and the metric helpers.
//   BOLT_FAR_STREAK: a 64 x 16 scene, static camera, no jitter. Left half: a far, routed hull (four-channel lane .r = z/w inside the
//   farw = 1 band at view z 100,000 units, .g = share 0.5, .b = w, .a = 1: no thin vote) carrying a static checker (0.3 / 0.5,
//   alpha 1), its columns 16-23 on the far ramp instead (z/w at the ramp's midpoint, farw 0.5 after the UNORM8 quantisation:
//   held < 1, the continuous share); right half: the depth sentinel (-1, -1, -1, -1), dark space (0.02, alpha 0). The
//   camera-gate resolve at the launcher defaults (history weight 0.9, far weight 0.985 on the camera gate, 7x7 far clip, thin
//   region 0.97) runs 24 warm-up frames, then frame N carries a one-frame additive streak (+0.4, +0.2, +0.1, alpha unchanged)
//   on rows 6-9 across both halves and a bright non-bolt addition of the same colour on rows 12-13 over the hull, then 8 plain
//   frames; the alpha history (--taa-alpha-history, builtin off) is run off (the default flight) and on.
//   Two runs per W, identical except that the flagged run's lane .g carries + 25.6 (K = 32 times a gained max(rgb) of 0.8) on
//   the streak rows of frame N, exactly what the late bullet draw's variant adds under its ONE/ONE blend. The write-back is the
//   embedded dithered identity program (hdr_writeback_dither_ps.hlsl: s0 the resolved image, s2 the frame-N scene, c29.x = W)
//   drawn into an FP16 target after frame N of each run; the dither is the same static pattern in both runs, so their
//   difference is the composite alone.
//   The sun-shadow cascade apply (the embedded sun_shadow_cascade_apply program, ON in the default flight, run at scene end
//   after the late bullet draw and before the resolve) is drawn over each run's frame-N lane with one cascade that contains
//   every pixel and a map of zeros (fully shadowed, exponent 1): its factor is 1 - share = 0.5 on the hull, 1 over space, and
//   at a flagged texel (g > 1, the share lost in the sum) it must be 1 (unshadowed for that frame) rather than the 0 that
//   reading the sum as share 1 gives: apply_plain / apply_flag = the mean factor over the flagged pixels in the unflagged and
//   the flagged run, apply_darker_px = flagged pixels whose factor is below the unflagged run's (0).
//   The default write-back chain over the flagged run's frame N (the display dither off, c8.z = 0, so the host model below needs
//   none): the embedded AgX tonemap, AgX + RCAS (--taa-sharpen 0.75), the bloom extract (bloom_extract_even_gamma, 64 x 16 ->
//   32 x 8) and bloom_agx (its U0 = that extract), each drawn twice: over the resolved image with s2 = the scene and c29.x = W,
//   and over a host-composited copy of the resolved image (r.rgb = lerp(r, s, W * share), r.a = s.a at the flagged texels,
//   FP16) with nothing to flag; agx_err_codes / bloom_agx_err_codes / extract_err_codes = the largest |difference| x 255
//   (within 1 code: the composite at W through the chain, the residue the FP16 rounding of the host copy),
//   agx_outside_diff = bytes of the point tonemap differing at the unflagged pixels (0). AgX + RCAS composites its centre
//   tap only (its four neighbour taps see the resolved, bolt-free values by design), so its model is the host's RCAS
//   (rcas.hlsl in double) over the GPU's AgX outputs: the centre from the composited tonemap, the neighbours from the tonemap
//   with the composite off (c29.x = 0), taps clamped at the border; sharpen_err_codes = the largest |GPU - model| x 255
//   over every pixel (within 1 code), sharpen_vs_host_composite_codes = the largest difference against the sharpen of the
//   host-composited copy (informational: the neighbour taps' composite, 8-15 codes at the streak's edges).
//   Rows (per W, with the alpha history off (the default flight: X3M_TAA_ALPHA_HISTORY builtin 0) and on): flagged_px = pixels whose flagged-run alpha is <= -1 (expected 128: the streak over the hull only; the same
//   addition over space and the non-bolt band never flag); alpha_min / alpha_max there (-(1 + held), held the far weight's share);
//   color_diff / age_diff / depth_diff = bytes of the frame-N colour, age and depth-history targets that differ between the
//   runs (0: every pixel bit-identical, the flag lives in the output alpha only); alpha_outside_diff = alpha bytes differing
//   outside the flagged pixels (0); wb_space_diff / wb_far_diff = write-back bytes differing between the runs over space and over
//   the unflagged hull (0); wb_err_codes = the largest |delta - W * share * (scene - resolved)| over the flagged pixels in 1/255
//   codes (the streak shows at W of the strength the resolve dropped; share = 1 inside the far band, farw = 0.502 on the ramp
//   columns: ramp_px = the streak's ramp pixels (32), ramp_alpha_min / max their alpha, -(1 + 0.502)); far_out_add /
//   space_out_add = the flagged run's mean
//   write-back addition over the streak (output minus the pre-streak output) over the hull and over space, and their ratio;
//   after_diff = bytes of the colour rgb lanes, age and depth targets differing between the runs over frames N+1..N+8 (0: no
//   residual beyond today's); after_alpha_px = pixels whose frame N+1..N+8 alpha differs, after_alpha_frames = the frames after N
//   with any such pixel: 0 with the alpha history off (the flagged alpha is never read back); with it on, a flagged texel's
//   negative history alpha enters the neighbours' alpha blend, clamped to the current 3x3 alpha range, which at the hull /
//   space silhouette spans [0, 1], so the silhouette pixels beside the streak carry a decaying alpha residual (colour, age and
//   depth never): reported, the limitation of the opt-in alpha history.
namespace bolt_far_streak {
// The embedded sun-shadow cascade apply program (src/renderer/sun_shadow_cascade_apply_program_inc.h has no wrapper header).
static const DWORD sun_shadow_cascade_apply_words[] = {
#include "../../src/renderer/sun_shadow_cascade_apply_program_inc.h"
};
void bolt_far_streak_cases(IDirect3DDevice9* d, Compiler compiler, const DWORD* resolver) {
    std::puts("BOLT_FAR_STREAK_CASES");
    constexpr UINT W = 64, H = 16;
    constexpr unsigned warm = 24, after = 8, frames = warm + 1 + after, streakFrame = warm;
    constexpr UINT S0 = 6, S1 = 10, B0 = 12, B1 = 14, R0 = 16, R1 = 24; // the streak rows, the non-bolt band rows, the ramp columns
    constexpr float p00 = .4999979f, p22 = 1.00000298f, p32 = -6.00001812f, viewZ = 100000.f;
    float gateD0 = 0, gateInv = 0;
    require(x3::temporal::far_gate(p00, p22, p32, 5120, 60.f, 68.f, gateD0, gateInv), "bfs far gate of the production footprints");
    const float hullDepth = float(double(p22) + double(p32) / double(viewZ));
    require((hullDepth - gateD0) * gateInv >= 1.f, "bfs hull depth inside the farw = 1 band");
    const float rampDepth = gateD0 + .5f / gateInv, rampZ = float(double(p32) / (double(rampDepth) - double(p22))); // farw 0.5 on the ramp
    require(rampDepth > gateD0 && rampDepth < hullDepth && rampZ > 0 && rampZ < viewZ, "bfs ramp depth between d0 and the hull");
    const auto ramp = [](UINT x) { return x >= R0 && x < R1; };
    struct Defer { Defer() { deferMetrics = true; deferredFailures.clear(); } ~Defer() { deferMetrics = false; } } defer;
    EdgeScene s(d, compiler); // the state setup and the quad; its own 32x32 targets are not used
    Com<IDirect3DTexture9> color, lane, motion, writeback, composited, chainA, chainB, extractA, extractB, applyTarget, map;
    Com<IDirect3DSurface9> writebackSurface, chainASurface, chainBSurface, extractASurface, extractBSurface, applySurface;
    auto target = [&](Com<IDirect3DTexture9>& t, Com<IDirect3DSurface9>& surface, UINT w, UINT h, D3DFORMAT format, const char* label) {
        check(label, d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &t.p, nullptr));
        check(label, t->GetSurfaceLevel(0, &surface.p));
    };
    check("bfs color", d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &color.p, nullptr));
    check("bfs lane", d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &lane.p, nullptr));
    check("bfs motion", d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &motion.p, nullptr));
    check("bfs composited", d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &composited.p, nullptr));
    check("bfs map", d->CreateTexture(4, 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &map.p, nullptr));
    target(writeback, writebackSurface, W, H, D3DFMT_A16B16G16R16F, "bfs writeback");
    target(chainA, chainASurface, W, H, D3DFMT_A16B16G16R16F, "bfs chain A");
    target(chainB, chainBSurface, W, H, D3DFMT_A16B16G16R16F, "bfs chain B");
    target(extractA, extractASurface, W / 2, H / 2, D3DFMT_A16B16G16R16F, "bfs extract A");
    target(extractB, extractBSurface, W / 2, H / 2, D3DFMT_A16B16G16R16F, "bfs extract B");
    target(applyTarget, applySurface, W, H, D3DFMT_A16B16G16R16F, "bfs apply");
    Com<IDirect3DPixelShader9> writebackPS, tonemapPS, sharpenPS, extractPS, bloomAgxPS, applyPS;
    check("bfs writeback PS (the embedded dithered identity write-back)",
          d->CreatePixelShader(reinterpret_cast<const DWORD*>(x3m::renderer::hdr_writeback_dither_program()), &writebackPS.p));
    check("bfs tonemap PS (the embedded AgX write-back)", d->CreatePixelShader(reinterpret_cast<const DWORD*>(x3m::renderer::hdr_tonemap_program()), &tonemapPS.p));
    check("bfs sharpen PS (the embedded AgX + RCAS write-back)", d->CreatePixelShader(reinterpret_cast<const DWORD*>(x3m::renderer::hdr_tonemap_sharpen_program()), &sharpenPS.p));
    check("bfs extract PS (the embedded even gamma2.2 bloom extract)", d->CreatePixelShader(x3m::renderer::detail::bloom_extract_even_gamma_words, &extractPS.p));
    check("bfs bloom_agx PS (the embedded bloom candidate write)", d->CreatePixelShader(x3m::renderer::detail::bloom_agx_words, &bloomAgxPS.p));
    check("bfs apply PS (the embedded sun-shadow cascade apply)", d->CreatePixelShader(sun_shadow_cascade_apply_words, &applyPS.p));
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
            const float v[4] = {farSide ? (ramp(x) ? rampDepth : hullDepth) : -1.f, (farSide ? .5f : -1.f) + flag, farSide ? (ramp(x) ? rampZ : viewZ) : -1.f, farSide ? 1.f : -1.f};
            std::memcpy(p, v, 16);
        });
    };
    upload(motion.p, D3DFMT_A32B32G32R32F, 16, [&](UINT, UINT, char* p) { const float m[4] = {0, 0, 0, 0}; std::memcpy(p, m, 16); });
    { // the shadow map: zeros (every receiver behind the map: fully shadowed)
        Com<IDirect3DTexture9> staging;
        check("bfs map staging", d->CreateTexture(4, 4, 1, 0, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &staging.p, nullptr));
        D3DLOCKED_RECT lock{};
        check("bfs map lock", staging->LockRect(0, &lock, nullptr, 0));
        for (UINT y = 0; y < 4; ++y) std::memset(static_cast<char*>(lock.pBits) + y * lock.Pitch, 0, 16);
        check("bfs map unlock", staging->UnlockRect(0));
        check("bfs map upload", d->UpdateTexture(staging.p, map.p));
    }
    // One full-screen draw of `ps` into `surface` (w x h): s0 / s1 / s2 as given (point / clamp; s1 linear for the bloom tent).
    auto draw = [&](IDirect3DSurface9* surface, UINT w, UINT h, IDirect3DPixelShader9* ps, IDirect3DTexture9* s0, IDirect3DTexture9* s1, IDirect3DTexture9* s2, bool linear1) {
        s.target(surface);
        D3DVIEWPORT9 vp{0, 0, w, h, 0, 1};
        check("bfs draw viewport", d->SetViewport(&vp));
        for (DWORD stage : {DWORD(0), DWORD(1), DWORD(x3::temporal::kBoltSceneSampler)}) {
            for (auto filter : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER}) check("bfs draw filter", d->SetSamplerState(stage, filter, linear1 && stage == 1 ? D3DTEXF_LINEAR : D3DTEXF_POINT));
            check("bfs draw mip", d->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
            for (auto address : {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV}) check("bfs draw clamp", d->SetSamplerState(stage, address, D3DTADDRESS_CLAMP));
            check("bfs draw srgb", d->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE));
        }
        check("bfs draw PS", d->SetPixelShader(ps));
        check("bfs draw s0", d->SetTexture(0, s0));
        check("bfs draw s1", d->SetTexture(1, s1));
        check("bfs draw s2", d->SetTexture(x3::temporal::kBoltSceneSampler, s2));
        check("bfs draw Begin", d->BeginScene());
        s.quad(0, 0, w, h, 0, 0);
        check("bfs draw End", d->EndScene());
        for (DWORD stage : {DWORD(0), DWORD(1), DWORD(x3::temporal::kBoltSceneSampler)}) check("bfs draw unbind", d->SetTexture(stage, nullptr));
    };
    // The apply's constants: c0 view (m00, m11 = 1, no offset), c1 terms (exponent 1, the plane fit voided at a unit step), c3
    // select (margin 1, the band beyond it), c4-c12 taps at the texel, c13.. cascade 0 mapping every pixel to sun (0, 0, 0.5)
    // with a 4-texel map of zeros, cascades 1-4 unused (row 0 w = 2, invalid).
    auto applyConstants = [&]() {
        const float view[4] = {1, 1, 0, 0}, terms[4] = {1, 0, 1, 1}, select[4] = {1, 2, 1, 0}, zero[4] = {0, 0, 0, 0};
        check("bfs apply c0", d->SetPixelShaderConstantF(0, view, 1));
        check("bfs apply c1", d->SetPixelShaderConstantF(1, terms, 1));
        check("bfs apply c3", d->SetPixelShaderConstantF(3, select, 1));
        for (unsigned k = 4; k < 13; ++k) check("bfs apply taps", d->SetPixelShaderConstantF(k, zero, 1));
        float cascades[25][4] = {};
        cascades[2][3] = .5f;                                           // cascade 0: sun.z = 0.5 for every pixel, x = y = 0
        cascades[3][0] = 4; cascades[3][1] = .25f;                      // map size 4, 1 / 4, constant bias 0, clamp 0
        cascades[4][0] = 1; cascades[4][1] = 1;                         // valid, last
        for (unsigned i = 1; i < 5; ++i) { cascades[5 * i][3] = 2; cascades[5 * i + 3][0] = 4; cascades[5 * i + 3][1] = .25f; cascades[5 * i + 4][1] = 1; }
        check("bfs apply cascades", d->SetPixelShaderConstantF(13, &cascades[0][0], 25));
    };
    struct Run { std::vector<std::vector<unsigned char>> color, age, depth; std::vector<unsigned char> writeback, sceneN, apply, agxA, agxB, agxPlain, sharpenA, sharpenB, extractA, extractB, bloomA, bloomB; };
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
                // The sun-shadow cascade apply over this frame's lane (before the resolve in production; the resolve above read the same lane).
                applyConstants();
                draw(applySurface.p, W, H, applyPS.p, lane.p, map.p, nullptr, false);
                run.apply = bytesOf(applyTarget.p);
                if (flagged) {
                    // The host-composited copy of the resolved image: the flagged texels at W, nothing left to flag.
                    const auto resolved = bytesOf(out.color);
                    upload(composited.p, D3DFMT_A16B16G16R16F, 8, [&](UINT x, UINT y, char* p) {
                        const std::size_t i = (std::size_t(y) * W + x) * 8;
                        unsigned short v[4];
                        std::memcpy(v, &resolved[i], 8);
                        const float a = halfFloat(v[3]);
                        if (a <= -1) {
                            const float shareT = show * std::min(std::max(-a - 1, 0.f), 1.f);
                            unsigned short sc[4];
                            std::memcpy(sc, &run.sceneN[i], 8);
                            for (unsigned c = 0; c < 3; ++c) { const float r = halfFloat(v[c]), sv = halfFloat(sc[c]); v[c] = toHalf(r + shareT * (sv - r)); }
                            v[3] = sc[3];
                        }
                        std::memcpy(p, v, 8);
                    });
                    // The default chain: AgX c8..c21 (exposure 1, gamma 2.2, no look, the display dither on), RCAS c23 (0.75), c29 = W.
                    x3::temporal::AgxConstants agx{};
                    require(x3::temporal::prepare(agx, 1.f, x3::temporal::kAgxClampOff, x3::temporal::AgxDecode::gamma22, x3::temporal::AgxLook::none), "bfs agx constants");
                    x3::temporal::set_dither(agx, false); // the host RCAS model below carries no dither
                    x3::temporal::SharpenConstants sharp{};
                    require(x3::temporal::prepare_sharpen(sharp, .75f, W, H), "bfs sharpen constants");
                    const float c29[4] = {show, 0, 0, 0};
                    check("bfs chain c8", d->SetPixelShaderConstantF(x3::temporal::kAgxFirstRegister, agx.exposure, x3::temporal::kAgxRegisterCount));
                    check("bfs chain c23", d->SetPixelShaderConstantF(x3::temporal::kSharpenRegister, sharp.values, 1));
                    check("bfs chain c29", d->SetPixelShaderConstantF(x3::temporal::kBoltShowRegister, c29, 1));
                    draw(chainASurface.p, W, H, tonemapPS.p, out.color, nullptr, color.p, false); run.agxA = bytesOf(chainA.p);
                    draw(chainBSurface.p, W, H, tonemapPS.p, composited.p, nullptr, color.p, false); run.agxB = bytesOf(chainB.p);
                    { // the tonemap with the composite off: the RCAS model's neighbour taps
                        const float off[4] = {0, 0, 0, 0};
                        check("bfs chain c29 off", d->SetPixelShaderConstantF(x3::temporal::kBoltShowRegister, off, 1));
                        draw(chainBSurface.p, W, H, tonemapPS.p, out.color, nullptr, color.p, false); run.agxPlain = bytesOf(chainB.p);
                        check("bfs chain c29 back", d->SetPixelShaderConstantF(x3::temporal::kBoltShowRegister, c29, 1));
                    }
                    draw(chainASurface.p, W, H, sharpenPS.p, out.color, nullptr, color.p, false); run.sharpenA = bytesOf(chainA.p);
                    draw(chainBSurface.p, W, H, sharpenPS.p, composited.p, nullptr, color.p, false); run.sharpenB = bytesOf(chainB.p);
                    // The bloom: the extract (64 x 16 -> 32 x 8) and bloom_agx over its own extract as U0; c24..c29 per draw.
                    x3::temporal::BloomParams params{};
                    x3::temporal::BloomConstants bloom{};
                    require(x3::temporal::prepare_bloom(bloom, {W, H}, {W / 2, H / 2}, params, 1.f, x3::temporal::kAgxClampOff, x3::temporal::AgxDecode::gamma22), "bfs bloom extract constants");
                    bloom.bolt[0] = show;
                    check("bfs bloom c24", d->SetPixelShaderConstantF(x3::temporal::kBloomFirstRegister, bloom.source, x3::temporal::kBloomRegisterCount));
                    draw(extractASurface.p, W / 2, H / 2, extractPS.p, out.color, nullptr, color.p, false); run.extractA = bytesOf(extractA.p);
                    draw(extractBSurface.p, W / 2, H / 2, extractPS.p, composited.p, nullptr, color.p, false); run.extractB = bytesOf(extractB.p);
                    require(x3::temporal::prepare_bloom(bloom, {W / 2, H / 2}, {W, H}, params, 1.f, x3::temporal::kAgxClampOff, x3::temporal::AgxDecode::gamma22), "bfs bloom agx constants");
                    bloom.bolt[0] = show;
                    check("bfs bloom c24 (agx)", d->SetPixelShaderConstantF(x3::temporal::kBloomFirstRegister, bloom.source, x3::temporal::kBloomRegisterCount));
                    check("bfs bloom c8", d->SetPixelShaderConstantF(x3::temporal::kAgxFirstRegister, agx.exposure, x3::temporal::kAgxRegisterCount));
                    draw(chainASurface.p, W, H, bloomAgxPS.p, out.color, extractA.p, color.p, true); run.bloomA = bytesOf(chainA.p);
                    draw(chainBSurface.p, W, H, bloomAgxPS.p, composited.p, extractB.p, color.p, true); run.bloomB = bytesOf(chainB.p);
                }
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
    // rcas.hlsl in double: the five saturated taps, the gain of --taa-sharpen 0.75 (sharpen.h: exp2(-2 * (1 - 0.75))).
    const double gain = std::exp2(-x3::temporal::kSharpenMaxStops * (1 - .75));
    auto rcas = [&](const double* b, const double* dd, const double* e, const double* f, const double* h, double* out) {
        auto sat = [](double v) { return std::min(std::max(v, 0.), 1.); };
        double B[3], D[3], E[3], F[3], Hh[3];
        for (unsigned c = 0; c < 3; ++c) { B[c] = sat(b[c]); D[c] = sat(dd[c]); E[c] = sat(e[c]); F[c] = sat(f[c]); Hh[c] = sat(h[c]); }
        auto luma = [](const double* v) { return .5 * v[0] + v[1] + .5 * v[2]; };
        const double bL = luma(B), dL = luma(D), eL = luma(E), fL = luma(F), hL = luma(Hh);
        const double lumaMax = std::max(std::max(std::max(bL, dL), std::max(eL, fL)), hL), lumaMin = std::min(std::min(std::min(bL, dL), std::min(eL, fL)), hL);
        double nz = sat(std::fabs(.25 * (bL + dL + fL + hL) - eL) / std::max(lumaMax - lumaMin, 1. / 256));
        nz = 1 - .5 * nz;
        double lobeMax = -1e9;
        double mn4[3], mx4[3];
        for (unsigned c = 0; c < 3; ++c) {
            mn4[c] = std::min(std::min(B[c], D[c]), std::min(F[c], Hh[c])); mx4[c] = std::max(std::max(B[c], D[c]), std::max(F[c], Hh[c]));
            const double hitMin = mn4[c] / std::max(4 * mx4[c], 1. / 4096), hitMax = (1 - mx4[c]) / std::min(4 * mn4[c] - 4, -1. / 4096);
            lobeMax = std::max(lobeMax, std::max(-hitMin, hitMax));
        }
        const double lobe = std::max(-.1875, std::min(lobeMax, 0.)) * gain * nz;
        for (unsigned c = 0; c < 3; ++c) {
            const double pix = ((B[c] + D[c] + F[c] + Hh[c]) * lobe + E[c]) / (4 * lobe + 1);
            out[c] = std::min(std::max(pix, std::min(mn4[c], E[c])), std::max(mx4[c], E[c]));
        }
    };
    auto differing = [](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) { unsigned n = a.size() != b.size(); for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) n += a[i] != b[i]; return n; };
    const float shows[2] = {.5f, 1.f};
    for (const bool alphaHistory : {false, true}) for (const float show : shows) {
        const Run plain = sequence(false, show, alphaHistory), flagged = sequence(true, show, alphaHistory);
        unsigned flaggedPx = 0, flagOutside = 0, alphaOutside = 0, wbSpace = 0, wbFar = 0, rampPx = 0, applyDarker = 0, agxOutside = 0;
        double alphaMin = 1e9, alphaMax = -1e9, wbErr = 0, farAdd = 0, spaceAdd = 0, rampAlphaMin = 1e9, rampAlphaMax = -1e9;
        double applyPlain = 0, applyFlag = 0, agxErr = 0, sharpenErr = 0, sharpenVsHost = 0, bloomErr = 0, extractErr = 0;
        unsigned farN = 0, spaceN = 0;
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            const bool streakRow = y >= S0 && y < S1, farSide = x < W / 2;
            const double a = half(flagged.color[streakFrame], x, y, 3), aPlain = half(plain.color[streakFrame], x, y, 3);
            if (a <= -1) {
                ++flaggedPx; if (!(streakRow && farSide)) ++flagOutside; alphaMin = std::min(alphaMin, a); alphaMax = std::max(alphaMax, a);
                if (ramp(x)) { ++rampPx; rampAlphaMin = std::min(rampAlphaMin, a); rampAlphaMax = std::max(rampAlphaMax, a); }
                const double fPlain = half(plain.apply, x, y, 0), fFlag = half(flagged.apply, x, y, 0);
                applyPlain += fPlain; applyFlag += fFlag; applyDarker += fFlag < fPlain - 1e-6;
            } else {
                if (a != aPlain) ++alphaOutside;
                for (unsigned c = 0; c < 4; ++c) agxOutside += half(flagged.agxA, x, y, c) != half(flagged.agxB, x, y, c);
            }
            { // the RCAS model: the composited centre, the composite-free neighbours (taps clamped at the border)
                auto tap = [&](const std::vector<unsigned char>& img, int tx, int ty, double* v) { tx = std::min(std::max(tx, 0), int(W) - 1); ty = std::min(std::max(ty, 0), int(H) - 1); for (unsigned c = 0; c < 3; ++c) v[c] = half(img, UINT(tx), UINT(ty), c); };
                double b[3], dd[3], e[3], f[3], h[3], model[3];
                tap(flagged.agxPlain, int(x), int(y) - 1, b); tap(flagged.agxPlain, int(x) - 1, int(y), dd); tap(flagged.agxA, int(x), int(y), e);
                tap(flagged.agxPlain, int(x) + 1, int(y), f); tap(flagged.agxPlain, int(x), int(y) + 1, h);
                rcas(b, dd, e, f, h, model);
                for (unsigned c = 0; c < 3; ++c) sharpenErr = std::max(sharpenErr, std::fabs(half(flagged.sharpenA, x, y, c) - model[c]) * 255);
            }
            for (unsigned c = 0; c < 3; ++c) {
                agxErr = std::max(agxErr, std::fabs(half(flagged.agxA, x, y, c) - half(flagged.agxB, x, y, c)) * 255);
                sharpenVsHost = std::max(sharpenVsHost, std::fabs(half(flagged.sharpenA, x, y, c) - half(flagged.sharpenB, x, y, c)) * 255);
                bloomErr = std::max(bloomErr, std::fabs(half(flagged.bloomA, x, y, c) - half(flagged.bloomB, x, y, c)) * 255);
                if (x < W / 2 && y < H / 2) extractErr = std::max(extractErr, double(std::fabs(halfFloat(*reinterpret_cast<const unsigned short*>(&flagged.extractA[(std::size_t(y) * (W / 2) + x) * 8 + c * 2])) - halfFloat(*reinterpret_cast<const unsigned short*>(&flagged.extractB[(std::size_t(y) * (W / 2) + x) * 8 + c * 2])))) * 255);
            }
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
        applyPlain /= std::max(flaggedPx, 1u); applyFlag /= std::max(flaggedPx, 1u);
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
        std::printf("BOLT_FAR_STREAK show=%.2f alpha_history=%u flagged_px=%u flag_outside=%u alpha_min=%.6f alpha_max=%.6f ramp_px=%u ramp_alpha_min=%.6f ramp_alpha_max=%.6f color_rgb_diff=%u color_diff=%u age_diff=%u depth_diff=%u alpha_outside_diff=%u wb_space_diff=%u wb_far_diff=%u wb_err_codes=%.4f far_out_add=%.6f space_out_add=%.6f far_over_space=%.4f after_diff=%u after_alpha_px=%u after_alpha_frames=%u after_frames=%u apply_plain=%.6f apply_flag=%.6f apply_darker_px=%u agx_err_codes=%.4f agx_outside_diff=%u sharpen_err_codes=%.4f sharpen_vs_host_composite_codes=%.4f bloom_agx_err_codes=%.4f extract_err_codes=%.4f\n",
                    double(show), unsigned(alphaHistory), flaggedPx, flagOutside, alphaMin, alphaMax, rampPx, rampAlphaMin, rampAlphaMax, colorRgbDiff, colorDiff, ageDiff, depthDiff, alphaOutside, wbSpace, wbFar, wbErr, farAdd, spaceAdd, spaceAdd > 0 ? farAdd / spaceAdd : 0., afterDiff, afterAlphaPx, afterAlphaFrames, after, applyPlain, applyFlag, applyDarker, agxErr, agxOutside, sharpenErr, sharpenVsHost, bloomErr, extractErr);
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
        metric((prefix + "the streak's ramp pixels carry a fractional share (32 pixels, -2 < alpha < -1)").c_str(), rampPx == 32 && rampAlphaMin > -2 && rampAlphaMax < -1 ? 0. : 1., 0, 0);
        metric((prefix + "the sun-shadow apply leaves no flagged hull pixel darker than without the bolt").c_str(), applyDarker, 0, 0);
        metric((prefix + "AgX tonemap: the composite at W through the chain (max error, codes; within 1)").c_str(), std::max(agxErr - 1, 0.), 0, 0);
        metric((prefix + "AgX tonemap bit-identical at the unflagged pixels").c_str(), agxOutside, 0, 0);
        metric((prefix + "AgX + RCAS: the composited centre through RCAS over resolved neighbours (max error against the host RCAS, codes; within 1)").c_str(), std::max(sharpenErr - 1, 0.), 0, 0);
        metric((prefix + "bloom_agx: the composite at W through the chain (max error, codes; within 1)").c_str(), std::max(bloomErr - 1, 0.), 0, 0);
        metric((prefix + "bloom extract: the composite at W through the chain (max error, codes; within 1)").c_str(), std::max(extractErr - 1, 0.), 0, 0);
        (void)colorDiff;
    }
    if (!deferredFailures.empty()) throw std::runtime_error(deferredFailures.front());
}
} // namespace bolt_far_streak
using bolt_far_streak::bolt_far_streak_cases;
