// Step A of docs/architecture/screen-emission-region.md: the packed screen law
// as LinearCompositionPolicy::PackedScreenInPlace (8) of the production pass,
// run inside the in-place region bracket and compared bit-exactly with the
// frozen packed prototype's C. The prototype TU is included verbatim (its
// main is renamed) so that the corpus, oracle, source draws and helper
// programs are the qualified ones. Detached device only; never the game.
#define main packed_prototype_main
#include "linear_emission_sm1_packed_fixture.cpp"
#undef main
#include "../../src/renderer/linear_emission_pass.h"
namespace {
namespace re = x3m::renderer;
using re::LinearCompositionPolicy;
using re::LinearEmissionBoundary;
using re::LinearEmissionImage;
using re::LinearEmissionPass;
using re::LinearEmissionPassFault;
constexpr auto packed_policy = LinearCompositionPolicy::PackedScreenInPlace;
constexpr auto fade_policy = LinearCompositionPolicy::DistanceFadeInPlace;
constexpr unsigned all_policies = 15;
using Slots = std::array<void*, 119>;
Slots device_slots(IDirect3DDevice9* d) {
    Slots slots{};
    std::memcpy(slots.data(), *reinterpret_cast<void***>(d), sizeof slots);
    return slots;
}
Image surface_image(Fixture& f, IDirect3DSurface9* s) {
    api(f.d->GetRenderTargetData(s, f.readback.p), "read surface");
    D3DLOCKED_RECT lock{};
    api(f.readback->LockRect(&lock, nullptr, D3DLOCK_READONLY), "read lock");
    Image im(f.width * f.height * 4);
    for (unsigned y = 0; y < f.height; ++y)
        std::memcpy(im.data() + y * f.width * 4, static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch, f.width * 8);
    api(f.readback->UnlockRect(), "read unlock");
    return im;
}
// The live witness's criterion (motion_output.cpp witness_readback): a
// non-negative nonzero half marks coverage.
bool covered(unsigned short bits) {
    return !(bits & 0x8000u) && (bits & 0x7fffu);
}
bool inside(const RECT& r, unsigned x, unsigned y) {
    return LONG(x) >= r.left && LONG(x) < r.right && LONG(y) >= r.top && LONG(y) < r.bottom;
}
// Conservative rectangle from the prototype's coverage (M red) plus a margin;
// an empty coverage selects the whole target.
RECT coverage_rect(const Image& mask, unsigned w, unsigned h, LONG margin) {
    LONG l = LONG(w), t = LONG(h), r = 0, b = 0;
    for (unsigned p = 0; p < w * h; ++p)
        if (covered(mask[p * 4])) {
            const LONG x = LONG(p % w), y = LONG(p / w);
            l = std::min(l, x);
            t = std::min(t, y);
            r = std::max(r, x + 1);
            b = std::max(b, y + 1);
        }
    if (r <= l || b <= t) return RECT{0, 0, LONG(w), LONG(h)};
    return RECT{std::max(0L, l - margin), std::max(0L, t - margin), std::min(LONG(w), r + margin),
                std::min(LONG(h), b + margin)};
}
// Channel differences of `got` against `ref_in` inside the rectangle and
// `ref_out` outside it (all four lanes).
struct Split {
    unsigned inside = 0, outside = 0;
};
Split split_diff(const Image& got, const Image& ref_in, const Image& ref_out, const RECT& r, unsigned w, unsigned h) {
    Split s;
    for (unsigned p = 0; p < w * h; ++p) {
        const bool in = inside(r, p % w, p / w);
        for (unsigned k = 0; k < 4; ++k)
            if (got[p * 4 + k] != (in ? ref_in : ref_out)[p * 4 + k]) ++(in ? s.inside : s.outside);
    }
    return s;
}
// Pass M against the prototype's M: red (live coverage), green and blue whole;
// alpha (per-bracket scratch, seeded only inside R) inside the rectangle.
unsigned mask_diff(const Image& got, const Image& ref, const RECT& r, unsigned w, unsigned h) {
    unsigned d = 0;
    for (unsigned p = 0; p < w * h; ++p)
        for (unsigned k = 0; k < 4; ++k)
            if ((k < 3 || inside(r, p % w, p / w)) && got[p * 4 + k] != ref[p * 4 + k]) ++d;
    return d;
}
unsigned red_outside(const Image& mask, const RECT& r, unsigned w, unsigned h) {
    unsigned n = 0;
    for (unsigned p = 0; p < w * h; ++p)
        if (covered(mask[p * 4]) && !inside(r, p % w, p / w)) ++n;
    return n;
}
struct Sources {
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> original, measure, packed[4];
    HRESULT created[4]{};
    unsigned layout = 0;
    void load_pair(Fixture& f, const char* programs, unsigned p) {
        const auto& pair = pairs[p];
        layout = pair.layout;
        auto vw = load(programs, "vs", pair.vs), pw = load(programs, "ps", pair.ps);
        need(vw[0] == 0xfffe0101u && pw[0] == 0xffff0101u, "untouched native models");
        need(re::linear_emission_sm1_pair_reviewed(hash(vw), hash(pw)), "exact native pair");
        api(f.d->CreateVertexShader(reinterpret_cast<const DWORD*>(vw.data()), &vs.p), "native VS");
        api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(pw.data()), &original.p), "native PS");
        auto measuring = witness(pair.layout == 2, false);
        api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(measuring.data()), &measure.p), "witness");
        const float gains[] = {1, 0, .25f, 2.5f};
        for (unsigned g = 0; g < 4; ++g) {
            re::LinearEmissionSm1Config config;
            config.outputs = re::LinearEmissionSm1Outputs::PackedScreen;
            config.gain = gains[g];
            Words code;
            need(re::linear_emission_sm1_pixel_variant(pw.data(), pw.size(), config, code) ==
                     re::LinearEmissionResult::Applied,
                 "packed transformer");
            created[g] = f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &packed[g].p);
        }
    }
};
// The source DIP(s) of one schedule without the prototype's Begin/EndScene:
// the bracket's quads and the source share one scene.
HRESULT issue(Fixture& f, unsigned schedule) {
    if (schedule == 1) {
        HRESULT hr = f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 8, 0, 2);
        return FAILED(hr) ? hr : f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 8, 6, 2);
    }
    return f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 8, 0, 4);
}
// Native screen state on `scene`: exactly the prototype's original binding
// (ONE/INVSRCCOLOR, all four masks, Z test, the original PS).
void bind_native(Fixture& f, IDirect3DSurface9* scene, unsigned which, Sources& s) {
    f.detach();
    api(f.d->SetRenderTarget(0, scene), "scene target");
    f.clear_depth();
    f.bind_source(which, s.vs.p, s.original.p, false);
}
struct Bracket {
    re::LinearEmissionPreparation prep;
    re::LinearEmissionCompletion done;
    HRESULT source = S_FALSE;
};
template <class Issue>
Bracket bracket(Fixture& f, LinearEmissionPass& pass, const LinearEmissionBoundary& boundary, Issue&& run) {
    Bracket b;
    api(f.d->BeginScene(), "bracket BeginScene");
    b.prep = pass.prepare(boundary);
    if (b.prep.ready) {
        b.source = run();
        b.done = pass.finish(b.source);
    }
    api(f.d->EndScene(), "bracket EndScene");
    return b;
}
void require_idle_no_exchange(LinearEmissionPass& pass, const char* why) {
    need(!pass.owning_candidate() && pass.acknowledge_exchange(true) == D3DERR_INVALIDCALL &&
             pass.recover_native().image == LinearEmissionImage::None && !pass.reference_accounting_busy(),
         why);
}
void require_caller_state(Fixture& f, IDirect3DSurface9* scene, const char* why) {
    Com<IDirect3DSurface9> rt0;
    api(f.d->GetRenderTarget(0, &rt0.p), "caller RT0");
    need(rt0.p == scene, why);
    for (unsigned i = 1; i < 4; ++i) {
        Com<IDirect3DSurface9> rt;
        const HRESULT hr = f.d->GetRenderTarget(i, &rt.p);
        need((hr == D3DERR_NOTFOUND || SUCCEEDED(hr)) && !rt.p, why);
    }
    for (unsigned i = 0; i < 5; ++i) {
        Com<IDirect3DBaseTexture9> texture;
        api(f.d->GetTexture(i, &texture.p), "caller stage");
        need(i ? !texture.p : texture.p == static_cast<IDirect3DBaseTexture9*>(f.atlas.p), why);
    }
    DWORD dest = 0, mask = 0;
    api(f.d->GetRenderState(D3DRS_DESTBLEND, &dest), "caller DESTBLEND");
    api(f.d->GetRenderState(D3DRS_COLORWRITEENABLE, &mask), "caller mask");
    need(dest == D3DBLEND_INVSRCCOLOR && mask == 15, why);
}
// Fixture-authored fade and emission producers for the sequence and alias
// cases: constant outputs (native RGBA, (L, a) or E, and M), drawn as one
// clip-space quad over a pixel rectangle with the -0.5 raster shift.
// c0 = native RGBA (oC0), c1 = the E lane (oC1), c2 = the M lane (oC2).
Words constant_ps(const float (&c0)[4], const float (&c1)[4], const float (&c2)[4]) {
    Words w{0xffff0200u};
    literal(w, 0, c0[0], c0[1], c0[2], c0[3]);
    literal(w, 1, c1[0], c1[1], c1[2], c1[3]);
    literal(w, 2, c2[0], c2[1], c2[2], c2[3]);
    ins(w, 1, {dst(8, 0), src(2, 0)});
    ins(w, 1, {dst(8, 1), src(2, 1)});
    ins(w, 1, {dst(8, 2), src(2, 2)});
    w.push_back(0xffffu);
    return w;
}
constexpr float fade_L[3] = {.3f, .15f, .6f}, fade_a = .5f;
HRESULT quad_up(Fixture& f, const RECT& q, float z) {
    const float l = -1 + (float(q.left) - .5f) * 2 / f.width, r = -1 + (float(q.right) - .5f) * 2 / f.width,
                t = 1 - (float(q.top) - .5f) * 2 / f.height, b = 1 - (float(q.bottom) - .5f) * 2 / f.height;
    const float quad[] = {l, t, z, 1, 0, 0, r, t, z, 1, 1, 0, l, b, z, 1, 0, 1, r, b, z, 1, 1, 1};
    return f.d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, 24);
}
void bind_authored(Fixture& f, IDirect3DSurface9* scene, IDirect3DPixelShader9* ps, bool fade) {
    f.detach();
    api(f.d->SetRenderTarget(0, scene), "authored scene");
    f.clear_depth();
    f.states(true, 0, false);
    api(f.d->SetRenderState(D3DRS_SRCBLEND, fade ? D3DBLEND_SRCALPHA : D3DBLEND_ONE), "authored src");
    api(f.d->SetRenderState(D3DRS_DESTBLEND, fade ? D3DBLEND_INVSRCALPHA : D3DBLEND_ONE), "authored dst");
    f.masks(fade ? 7 : 15, 15);
    api(f.d->SetTexture(0, f.atlas.p), "authored atlas");
    api(f.d->SetVertexShader(f.full_vs.p), "authored VS");
    api(f.d->SetPixelShader(ps), "authored PS");
    api(f.d->SetVertexDeclaration(f.full_decl.p), "authored declaration");
    api(f.d->SetDepthStencilSurface(f.depth.p), "authored depth");
}
double sanitize(double v) {
    return std::isnan(v) || v <= 0 ? 0. : std::min(v, 65504.);
}
double decode(double v) {
    v = sanitize(v);
    return v > 0 ? std::pow(std::max(v, 1e-10), 2.2) : 0.;
}
double encode(double v) {
    v = sanitize(v);
    return v > 0 ? std::pow(std::max(v, 1e-22), 1 / 2.2) : 0.;
}
// Independent CPU oracle of one fade bracket (policy 4, the prototype-1
// composite): inside the quad C = encode(Q + (1 - q) decode(A)) with
// Q = fp16(a L), q = a, alpha unchanged; elsewhere exactly A.
unsigned fade_oracle_diff(const Image& got, const Image& before, const RECT& quad, unsigned w, unsigned h,
                          double& max_fraction) {
    unsigned d = 0;
    for (unsigned p = 0; p < w * h; ++p) {
        const bool in = inside(quad, p % w, p / w);
        for (unsigned k = 0; k < 4; ++k) {
            const unsigned short a = before[p * 4 + k], g = got[p * 4 + k];
            if (!in || k == 3) {
                if (g != a) ++d;
                continue;
            }
            const double q = fp16(half(fade_a)), Q = fp16(half(fade_a * fade_L[k]));
            const double want = encode(Q + (1 - q) * decode(fp16(a)));
            const double fraction = std::abs(fp16(g) - want) / (2e-5 + .006 * std::abs(want));
            max_fraction = std::max(max_fraction, fraction);
            if (!(fraction <= 1)) ++d;
        }
    }
    return d;
}
struct Pass {
    LinearEmissionPass pass;
    Pass(Fixture& f, const Slots& slots, unsigned policies = all_policies, const D3DCAPS9* caps = nullptr) {
        D3DDISPLAYMODE display{};
        api(f.d->GetDisplayMode(0, &display), "display");
        api(pass.attach(f.d.p, slots.data(), caps ? *caps : f.caps, display.Format, D3DFMT_D24S8, policies), "attach");
        need(pass.caps().enabled && pass.caps().supports(packed_policy), "policy 8 available");
        api(pass.ensure_targets(f.width, f.height), "pool");
    }
    ~Pass() {
        pass.before_reset();
        pass.detach();
    }
};
LinearEmissionBoundary packed_boundary(IDirect3DSurface9* scene, IDirect3DPixelShader9* ps, std::uint64_t frame,
                                       const RECT* rect) {
    LinearEmissionBoundary b{scene, ps, frame, true, nullptr};
    b.policy = packed_policy;
    if (rect) {
        b.region = *rect;
        b.region_known = true;
    }
    return b;
}
struct Functional {
    Fixture& f;
    const char* programs;
    Slots slots;
    Target work, pristine;
    std::uint64_t frame = 1;
    Functional(Fixture& fx, const char* p)
        : f(fx)
        , programs(p)
        , slots(device_slots(fx.d.p)) {
        targets();
    }
    void targets() {
        f.target(work);
        f.target(pristine);
        api(f.d->StretchRect(f.a.surface.p, nullptr, pristine.surface.p, nullptr, D3DTEXF_NONE), "pristine copy");
    }
    void reset_work(IDirect3DSurface9* from = nullptr) {
        api(f.d->StretchRect(from ? from : f.a.surface.p, nullptr, work.surface.p, nullptr, D3DTEXF_NONE),
            "work reset");
    }
    // One policy-8 bracket of the schedule on `scene` with the native state
    // bound; the pass's M is cleared by the new frame (and seeded as the
    // prototype seeds it for the mask case).
    Bracket packed_bracket(LinearEmissionPass& pass, IDirect3DSurface9* scene, unsigned which, unsigned schedule,
                           Sources& s, const RECT* rect, bool new_frame = true) {
        bind_native(f, scene, which, s);
        if (new_frame) {
            need(pass.begin_frame(frame).ready, "M frame clear");
            if (which == 14) {
                RECT left{0, 0, LONG(f.width / 2), LONG(f.height)};
                api(f.d->ColorFill(pass.coverage_target(), &left, D3DCOLOR_ARGB(0, 255, 0, 0)), "pass M seed");
            }
        }
        const auto boundary = packed_boundary(scene, s.packed[gain_index(which)].p, frame, rect);
        auto b = bracket(f, pass, boundary, [&] { return issue(f, schedule); });
        if (new_frame) ++frame;
        return b;
    }
};
void corpus(Functional& x, Pass& P, const char* rect_arg) {
    Fixture& f = x.f;
    const unsigned w = f.width, h = f.height;
    const auto a = f.image(f.a);
    unsigned rows = 0, exact = 0, exact_inside = 0, exact_outside = 0, exact_mask = 0, conservative = 0,
             prototype_failures = 0, unsupported = 0, per_schedule[3]{}, bracket_pixels = 0;
    bool witness_saved = false;
    RECT straddle{8, 8, 24, 24};
    if (rect_arg)
        need(std::sscanf(rect_arg, "%ld,%ld,%ld,%ld", &straddle.left, &straddle.top, &straddle.right,
                         &straddle.bottom) == 4,
             "--rect l,t,r,b");
    for (unsigned p = 0; p < 9; ++p) {
        Sources s;
        s.load_pair(f, x.programs, p);
        for (unsigned which = 0; which < CASES; ++which) {
            if (boundary(which)) continue;
            for (unsigned schedule = 0; schedule < 3; ++schedule) {
                ++rows;
                if (FAILED(s.created[gain_index(which)])) {
                    ++unsupported;
                    continue;
                }
                auto r = run_case(f, which, s.layout, schedule, s.vs.p, s.original.p, s.packed[gain_index(which)].p,
                                  s.measure.p);
                prototype_failures += r.metrics.failed();
                // Rectangles: the prototype's coverage plus one pixel (schedule 0),
                // the unknown whole target (1), coverage plus three pixels (2).
                RECT rect = coverage_rect(r.mask, w, h, schedule == 2 ? 3 : 1);
                const bool known = schedule != 1;
                if (!known) rect = RECT{0, 0, LONG(w), LONG(h)};
                x.reset_work();
                auto b = x.packed_bracket(P.pass, x.work.surface.p, which, schedule, s, known ? &rect : nullptr);
                need(b.prep.ready && SUCCEEDED(b.source) && b.done.image == LinearEmissionImage::Linear &&
                         !b.done.candidate_bound && b.done.recovery == S_FALSE && SUCCEEDED(b.done.restore) &&
                         SUCCEEDED(b.done.composition),
                     "packed bracket completion");
                require_idle_no_exchange(P.pass, "packed bracket reached the exchange path");
                need(P.pass.coverage_valid(), "coverage after the packed bracket");
                require_caller_state(f, x.work.surface.p, "packed bracket did not restore the caller state");
                const RECT used = b.done.region;
                need(used.left == rect.left && used.top == rect.top && used.right == rect.right &&
                         used.bottom == rect.bottom,
                     "bracket rectangle");
                bracket_pixels += unsigned((used.right - used.left) * (used.bottom - used.top));
                const auto got = surface_image(f, x.work.surface.p), mask = surface_image(f, P.pass.coverage_target());
                const auto d = split_diff(got, r.c, a, used, w, h);
                const unsigned md = mask_diff(mask, r.mask, used, w, h), outside = red_outside(mask, used, w, h);
                exact_inside += !d.inside;
                exact_outside += !d.outside;
                exact_mask += !md;
                conservative += !outside;
                const bool ok = !d.inside && !d.outside && !md && !outside;
                exact += ok;
                per_schedule[schedule] += ok;
                std::printf(
                    "STEP_A_ROW pair=%u case=%u name=%s schedule=%u known=%u rect=%ld,%ld,%ld,%ld inside_diff=%u outside_diff=%u "
                    "mask_diff=%u red_outside=%u prototype_failed=%u surviving=%u overlap=%u\n",
                    p, which, cases[which], schedule, unsigned(known), used.left, used.top, used.right, used.bottom,
                    d.inside, d.outside, md, outside, unsigned(r.metrics.failed()), r.metrics.surviving,
                    r.metrics.overlap);
                if (!ok && !witness_saved) {
                    witness_saved = true;
                    const std::string prefix = "step_a_p" + std::to_string(p) + "_c" + std::to_string(which) + "_s" +
                                               std::to_string(schedule);
                    raw(prefix + "_pass_a.rgba16f", got);
                    raw(prefix + "_pass_m.rgba16f", mask);
                    raw(prefix + "_prototype_c.rgba16f", r.c);
                    raw(prefix + "_prototype_m.rgba16f", r.mask);
                    std::printf("STEP_A_WITNESS prefix=%s\n", prefix.c_str());
                }
                // The straddling injected rectangle: the packed law still holds
                // inside, A is untouched outside (the bullet is missing there) and
                // the M-outside-union witness fires.
                if (p == 0 && which == 0 && schedule == 0) {
                    x.reset_work();
                    auto sb = x.packed_bracket(P.pass, x.work.surface.p, which, schedule, s, &straddle);
                    need(sb.prep.ready && sb.done.image == LinearEmissionImage::Linear, "straddle bracket");
                    const auto sg = surface_image(f, x.work.surface.p), sm = surface_image(f, P.pass.coverage_target());
                    const auto sd = split_diff(sg, r.c, a, sb.done.region, w, h);
                    const unsigned fired = red_outside(sm, sb.done.region, w, h);
                    std::printf(
                        "STEP_A_STRADDLE rect=%ld,%ld,%ld,%ld inside_diff=%u outside_diff=%u red_outside=%u witness_fired=%u\n",
                        sb.done.region.left, sb.done.region.top, sb.done.region.right, sb.done.region.bottom, sd.inside,
                        sd.outside, fired, unsigned(fired > 0));
                }
            }
        }
    }
    std::printf(
        "STEP_A_CORPUS rows=%u unsupported=%u exact=%u exact_inside=%u exact_outside=%u exact_mask=%u conservative=%u "
        "prototype_failures=%u one_dip=%u two_dips=%u reverse=%u region_pixels=%u\n",
        rows, unsupported, exact, exact_inside, exact_outside, exact_mask, conservative, prototype_failures,
        per_schedule[0], per_schedule[1], per_schedule[2], bracket_pixels);
}
// The prototype's pipeline on an arbitrary A (uploaded into its immutable
// target for the run and restored afterwards): C for the sequential oracle.
Image prototype_c(Functional& x, Sources& s, IDirect3DSurface9* a_source) {
    Fixture& f = x.f;
    api(f.d->StretchRect(a_source, nullptr, f.a.surface.p, nullptr, D3DTEXF_NONE), "oracle A");
    f.prepare(0, s.layout, false);
    f.seed(0);
    f.initialize();
    f.packed_begin(0, s.vs.p, s.packed[0].p);
    f.submit(0);
    f.assemble(true);
    auto c = f.image(f.c);
    api(f.d->StretchRect(x.pristine.surface.p, nullptr, f.a.surface.p, nullptr, D3DTEXF_NONE), "oracle A restore");
    return c;
}
void sequences(Functional& x, Pass& P, Sources& s, IDirect3DPixelShader9* fade_ps) {
    Fixture& f = x.f;
    const unsigned w = f.width, h = f.height;
    const RECT quad{4, 4, 20, 20}, fade_rect{2, 2, 22, 22};
    const auto a0 = f.image(f.a);
    auto ref = run_case(f, 0, s.layout, 0, s.vs.p, s.original.p, s.packed[0].p, s.measure.p);
    const RECT packed_rect = coverage_rect(ref.mask, w, h, 1);
    need(packed_rect.left < fade_rect.right && packed_rect.top < fade_rect.bottom, "rectangles overlap");
    auto fade_bracket = [&](IDirect3DSurface9* scene, bool new_frame) {
        bind_authored(f, scene, fade_ps, true);
        if (new_frame) need(P.pass.begin_frame(x.frame).ready, "fade frame");
        LinearEmissionBoundary boundary{scene, fade_ps, x.frame, true, f.full_vs.p};
        boundary.policy = fade_policy;
        boundary.region = fade_rect;
        boundary.region_known = true;
        auto b = bracket(f, P.pass, boundary, [&] { return quad_up(f, quad, .2f); });
        need(b.prep.ready && SUCCEEDED(b.source) && b.done.image == LinearEmissionImage::Linear, "fade bracket");
        return b;
    };
    for (const bool fade_first : {true, false}) {
        x.reset_work();
        double max_fraction = 0;
        unsigned fade_diff = 0, packed_inside = 0, packed_outside = 0;
        if (fade_first) {
            fade_bracket(x.work.surface.p, true);
            const auto a1 = surface_image(f, x.work.surface.p);
            fade_diff = fade_oracle_diff(a1, a0, quad, w, h, max_fraction);
            // Keep A1 for the packed oracle, then the packed bracket on the composed
            // A1 in the same frame (M accumulates) with an overlapping rectangle.
            Target hold;
            f.target(hold);
            api(f.d->StretchRect(x.work.surface.p, nullptr, hold.surface.p, nullptr, D3DTEXF_NONE), "hold A1");
            auto b = x.packed_bracket(P.pass, x.work.surface.p, 0, 0, s, &packed_rect, false);
            need(b.prep.ready && b.done.image == LinearEmissionImage::Linear, "packed after fade");
            const auto a2 = surface_image(f, x.work.surface.p);
            const auto c1 = prototype_c(x, s, hold.surface.p);
            const auto d = split_diff(a2, c1, a1, packed_rect, w, h);
            packed_inside = d.inside;
            packed_outside = d.outside;
            ++x.frame;
        } else {
            bind_native(f, x.work.surface.p, 0, s);
            need(P.pass.begin_frame(x.frame).ready, "packed-first frame");
            auto b = x.packed_bracket(P.pass, x.work.surface.p, 0, 0, s, &packed_rect, false);
            need(b.prep.ready && b.done.image == LinearEmissionImage::Linear, "packed before fade");
            const auto a1 = surface_image(f, x.work.surface.p);
            const auto d = split_diff(a1, ref.c, a0, packed_rect, w, h);
            packed_inside = d.inside;
            packed_outside = d.outside;
            fade_bracket(x.work.surface.p, false);
            const auto a2 = surface_image(f, x.work.surface.p);
            fade_diff = fade_oracle_diff(a2, a1, quad, w, h, max_fraction);
            ++x.frame;
        }
        std::printf(
            "STEP_A_SEQUENCE order=%s fade_rect=%ld,%ld,%ld,%ld packed_rect=%ld,%ld,%ld,%ld fade_diff=%u fade_max_fraction=%.6g "
            "packed_inside_diff=%u packed_outside_diff=%u\n",
            fade_first ? "fade_packed" : "packed_fade", fade_rect.left, fade_rect.top, fade_rect.right,
            fade_rect.bottom, packed_rect.left, packed_rect.top, packed_rect.right, packed_rect.bottom, fade_diff,
            max_fraction, packed_inside, packed_outside);
    }
}
// Emission exchange after a packed bracket on the same pass (E and C hold
// plane residues) against a fresh pass on a bit-exact copy: the exchanged C
// and M must be identical, proving the E/C plane alias leaks nothing.
void alias(Functional& x, Pass& P, Sources& s, IDirect3DPixelShader9* emission_ps) {
    Fixture& f = x.f;
    const RECT quad{6, 6, 26, 26};
    x.reset_work();
    const RECT rect = coverage_rect(run_case(f, 0, s.layout, 0, s.vs.p, s.original.p, s.packed[0].p, s.measure.p).mask,
                                    f.width, f.height, 1);
    auto b = x.packed_bracket(P.pass, x.work.surface.p, 0, 0, s, &rect);
    need(b.prep.ready && b.done.image == LinearEmissionImage::Linear, "packed before the emission exchange");
    Pass Q(f, x.slots);
    Target scene_p, scene_q;
    f.target(scene_p);
    f.target(scene_q);
    Image c[2], m[2];
    for (unsigned i = 0; i < 2; ++i) {
        auto& pass = i ? Q.pass : P.pass;
        auto& scene = i ? scene_q : scene_p;
        api(f.d->StretchRect(x.work.surface.p, nullptr, scene.surface.p, nullptr, D3DTEXF_NONE), "exchange scene");
        bind_authored(f, scene.surface.p, emission_ps, false);
        need(pass.begin_frame(x.frame).ready, "emission frame");
        LinearEmissionBoundary boundary{scene.surface.p, emission_ps, x.frame, true, nullptr};
        boundary.policy = LinearCompositionPolicy::AdditiveEmission;
        auto e = bracket(f, pass, boundary, [&] { return quad_up(f, quad, .2f); });
        need(e.prep.ready && SUCCEEDED(e.source) && e.done.image == LinearEmissionImage::Linear &&
                 e.done.candidate_bound,
             "emission exchange bracket");
        auto* slot = pass.owning_candidate();
        need(slot && *slot, "emission owning candidate");
        std::swap(scene.surface.p, *slot);
        api(pass.acknowledge_exchange(true), "emission acknowledgement");
        c[i] = surface_image(f, scene.surface.p);
        m[i] = surface_image(f, pass.coverage_target());
    }
    ++x.frame;
    unsigned c_diff = 0, m_diff = 0;
    for (std::size_t i = 0; i < c[0].size(); ++i) {
        c_diff += c[0][i] != c[1][i];
        m_diff += m[0][i] != m[1][i];
    }
    f.detach();
    api(f.d->SetRenderTarget(0, f.native.surface.p), "alias RT0");
    std::printf("STEP_A_ALIAS exchange=1 c_diff=%u m_diff=%u\n", c_diff, m_diff);
}
void ladder(Functional& x, Pass& P, Sources& s) {
    Fixture& f = x.f;
    const unsigned w = f.width, h = f.height;
    const auto a = f.image(f.a);
    auto ref = run_case(f, 0, s.layout, 0, s.vs.p, s.original.p, s.packed[0].p, s.measure.p);
    const RECT rect = coverage_rect(ref.mask, w, h, 1);
    const char* const labels[] = {"copy",      "region_scissor",    "plane_init", "source_bind", "source",
                                  "composite", "composite_scissor", "restore",    "recovery",    "restore_recovery"};
    constexpr LinearEmissionPassFault faults[] = {LinearEmissionPassFault::Copy,
                                                  LinearEmissionPassFault::RegionScissor,
                                                  LinearEmissionPassFault::PlaneInit,
                                                  LinearEmissionPassFault::SourceBind,
                                                  LinearEmissionPassFault::None,
                                                  LinearEmissionPassFault::Composite,
                                                  LinearEmissionPassFault::CompositeScissor,
                                                  LinearEmissionPassFault::Restore,
                                                  LinearEmissionPassFault::RegionRecovery,
                                                  LinearEmissionPassFault::Restore};
    for (unsigned stage = 1; stage <= 10; ++stage) {
        x.reset_work();
        bind_native(f, x.work.surface.p, 0, s);
        need(P.pass.begin_frame(x.frame).ready, "ladder frame");
        const auto prior_mask = surface_image(f, P.pass.coverage_target());
        const auto boundary = packed_boundary(x.work.surface.p, s.packed[0].p, x.frame, &rect);
        if (faults[stage - 1] != LinearEmissionPassFault::None) P.pass.inject(faults[stage - 1]);
        const bool failing_source = stage == 5 || stage == 9 || stage == 10;
        HRESULT first = S_OK, recovery = S_FALSE;
        unsigned prepared = 0, coverage = 0, blocked = 0, exact_a = 0;
        api(f.d->BeginScene(), "ladder BeginScene");
        const auto prep = P.pass.prepare(boundary);
        if (stage <= 4) {
            need(!prep.ready && prep.operation == E_FAIL && prep.state_preserved && SUCCEEDED(prep.saved),
                 "pre-source refusal");
            require_caller_state(f, x.work.surface.p, "refusal did not roll the state back");
            need(surface_image(f, x.work.surface.p) == a, "clean refusal touched A");
            // M red/green/blue (the coverage lanes every consumer reads) are
            // untouched; alpha is per-bracket scratch that the plane init may
            // already have seeded before the source-bind seam.
            const auto mask_now = surface_image(f, P.pass.coverage_target());
            for (unsigned i = 0; i < mask_now.size(); ++i)
                need(i % 4 == 3 || mask_now[i] == prior_mask[i], "clean refusal touched M coverage");
            api(issue(f, 0), "native source after refusal");
            api(f.d->EndScene(), "refusal EndScene");
            first = prep.operation;
            coverage = P.pass.coverage_valid();
            need(coverage == 1, "clean refusal lost coverage");
            // Not blocked: the same frame accepts the next bracket.
            const auto again = bracket(f, P.pass, boundary, [&] { return issue(f, 0); });
            need(again.prep.ready && again.done.image == LinearEmissionImage::Linear, "refusal blocked the frame");
            exact_a = 1;
            api(f.d->BeginScene(), "ladder reopen");
        } else {
            need(prep.ready, "ladder prepared");
            prepared = 1;
            const HRESULT source = failing_source ? D3DERR_INVALIDCALL : issue(f, 0);
            const auto done = P.pass.finish(source);
            need(done.source == source, "source HRESULT retained");
            need(done.image == LinearEmissionImage::Incomplete && !done.candidate_bound, "failure image");
            require_idle_no_exchange(P.pass, "failure reached the exchange path");
            recovery = done.recovery;
            const auto got = surface_image(f, x.work.surface.p);
            if (stage == 10) {
                need(FAILED(source) && done.composition == S_FALSE && done.restore == E_FAIL && done.recovery == S_OK,
                     "restore+source");
                first = source;
                Com<IDirect3DSurface9> rt0;
                api(f.d->GetRenderTarget(0, &rt0.p), "ladder RT0");
                need(rt0.p == x.work.surface.p, "restore/recovery stage lost the caller's A");
                for (unsigned i = 0; i < 5; ++i) {
                    Com<IDirect3DBaseTexture9> texture;
                    api(f.d->GetTexture(i, &texture.p), "ladder stage");
                    need(!texture.p, "recovery after failed restore left a sampler stage bound");
                }
            } else
                require_caller_state(f, x.work.surface.p, "post-source failure did not restore the caller state");
            if (stage == 5 || stage == 9) {
                need(FAILED(source) && done.composition == S_FALSE, "invalid source before composition");
                first = source;
                need(done.recovery == (stage == 9 ? E_FAIL : S_OK), "source failure recovery");
            } else if (stage == 6 || stage == 7) {
                need(SUCCEEDED(source) && done.composition == E_FAIL && SUCCEEDED(done.restore) &&
                         done.recovery == S_OK,
                     "composite recovery");
                first = done.composition;
            } else if (stage == 8) {
                need(SUCCEEDED(source) && SUCCEEDED(done.composition) && done.restore == E_FAIL &&
                         done.recovery == S_FALSE,
                     "restore failure");
                first = done.restore;
                const auto d = split_diff(got, ref.c, a, rect, w, h);
                exact_a = !d.inside && !d.outside;
                need(exact_a, "restore failure lost the composed rectangle");
            }
            if (stage != 8) {
                // The packed source never writes A: even the failed recovery (stage
                // 9) leaves A exactly as before the draw.
                exact_a = got == a;
                need(exact_a, "failure left A changed");
            }
            coverage = P.pass.coverage_valid();
            need(!coverage, "failure left coverage valid");
            const auto refused = P.pass.prepare(boundary);
            blocked = !refused.ready && refused.saved == S_FALSE && refused.operation == S_FALSE;
            need(blocked, "failed frame accepted another bracket");
        }
        api(f.d->EndScene(), "ladder EndScene");
        ++x.frame;
        std::printf(
            "STEP_A_FAILURE stage=%u label=%s native=1 prepared=%u first=%08lx recovery=%08lx coverage=%u blocked=%u "
            "exact_a=%u exchange=0\n",
            stage, labels[stage - 1], prepared, first, recovery, coverage, blocked, exact_a);
    }
}
void capability(Functional& x) {
    Fixture& f = x.f;
    D3DDISPLAYMODE display{};
    api(f.d->GetDisplayMode(0, &display), "display");
    unsigned refused = 0;
    // Three simultaneous targets: policy 8 alone is refused at attach; beside
    // the other policies it is absent and a policy-8 boundary is refused
    // before any getter.
    {
        auto caps = f.caps;
        caps.NumSimultaneousRTs = 3;
        LinearEmissionPass alone;
        need(alone.attach(f.d.p, x.slots.data(), caps, display.Format, D3DFMT_D24S8, 8) == D3DERR_NOTAVAILABLE &&
                 !alone.caps().enabled && alone.references() == 0,
             "three-target refusal at attach");
        ++refused;
        LinearEmissionPass three;
        api(three.attach(f.d.p, x.slots.data(), caps, display.Format, D3DFMT_D24S8, all_policies),
            "three-target attach");
        need(three.caps().supported_policies == 7 && three.caps().available_policies == 7 &&
                 !three.caps().supports(packed_policy),
             "policy 8 absent with three targets");
        api(three.ensure_targets(f.width, f.height), "three-target pool");
        need(three.allocations() == 4, "four-target pool without policy 8");
        Sources s;
        s.load_pair(f, x.programs, 0);
        bind_native(f, x.work.surface.p, 0, s);
        need(three.begin_frame(x.frame).ready, "three-target frame");
        const RECT rect{2, 2, 30, 30};
        const auto boundary = packed_boundary(x.work.surface.p, s.packed[0].p, x.frame, &rect);
        const auto refusal = three.prepare(boundary);
        need(!refusal.ready && refusal.operation == D3DERR_NOTAVAILABLE && refusal.saved == S_FALSE &&
                 refusal.state_preserved && refusal.restore == S_FALSE && three.coverage_valid(),
             "policy 8 refused before any getter");
        ++refused;
        ++x.frame;
        three.before_reset();
        three.detach();
    }
    for (unsigned which = 0; which < 2; ++which) {
        auto caps = f.caps;
        if (which == 0)
            caps.DestBlendCaps &= ~DWORD(D3DPBLENDCAPS_INVSRCALPHA);
        else
            caps.PrimitiveMiscCaps &= ~DWORD(D3DPMISCCAPS_INDEPENDENTWRITEMASKS);
        LinearEmissionPass pass;
        api(pass.attach(f.d.p, x.slots.data(), caps, display.Format, D3DFMT_D24S8, all_policies), "capability attach");
        need(!pass.caps().supports(packed_policy) && pass.caps().available_policies == (which ? 1u : 7u),
             "capability gate");
        ++refused;
        pass.detach();
    }
    f.detach();
    api(f.d->SetRenderTarget(0, f.native.surface.p), "capability RT0");
    std::printf("STEP_A_CAPS refused=%u\n", refused);
}
void reset_case(Functional& x, Pass& P, Sources& s) {
    Fixture& f = x.f;
    const unsigned w = f.width, h = f.height;
    const auto a = f.image(f.a);
    auto ref = run_case(f, 0, s.layout, 0, s.vs.p, s.original.p, s.packed[0].p, s.measure.p);
    const RECT rect = coverage_rect(ref.mask, w, h, 1);
    x.reset_work();
    bind_native(f, x.work.surface.p, 0, s);
    need(P.pass.begin_frame(x.frame).ready, "interrupted frame");
    const auto boundary = packed_boundary(x.work.surface.p, s.packed[0].p, x.frame, &rect);
    api(f.d->BeginScene(), "interrupted BeginScene");
    need(P.pass.prepare(boundary).ready, "interrupted bracket prepared");
    api(f.d->EndScene(), "interrupted EndScene");
    need(P.pass.reference_accounting_busy(), "interrupted bracket retains getters");
    P.pass.before_reset();
    {
        // Scoped: outstanding backbuffer references would refuse the Reset.
        Com<IDirect3DSurface9> rt0, back;
        api(f.d->GetRenderTarget(0, &rt0.p), "Reset RT0");
        api(f.d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back.p), "backbuffer");
        need(rt0.p == back.p, "interrupted packed bracket left an owned target bound");
    }
    for (unsigned i = 1; i < 4; ++i) {
        Com<IDirect3DSurface9> rt;
        const HRESULT hr = f.d->GetRenderTarget(i, &rt.p);
        need((hr == D3DERR_NOTFOUND || SUCCEEDED(hr)) && !rt.p, "interrupted Reset retained plane attachments");
    }
    need(P.pass.references() == 7 && !P.pass.reference_accounting_busy() && !P.pass.coverage_valid() &&
             !P.pass.owning_candidate(),
         "interrupted Reset retained bracket state");
    x.work.release();
    x.pristine.release();
    f.reset();
    x.targets();
    api(P.pass.ensure_targets(w, h), "pool after Reset");
    need(P.pass.allocations() == 10, "five-target pool recreated after Reset");
    Sources again;
    again.load_pair(f, x.programs, 0);
    ++x.frame;
    auto post = run_case(f, 0, again.layout, 0, again.vs.p, again.original.p, again.packed[0].p, again.measure.p);
    x.reset_work();
    auto b = x.packed_bracket(P.pass, x.work.surface.p, 0, 0, again, &rect);
    need(b.prep.ready && b.done.image == LinearEmissionImage::Linear, "post-Reset bracket");
    const auto d = split_diff(surface_image(f, x.work.surface.p), post.c, a, rect, w, h);
    f.detach();
    api(f.d->SetRenderTarget(0, f.native.surface.p), "post-Reset RT0");
    std::printf("STEP_A_RESET interrupted=1 detached=1 post_reset_inside_diff=%u post_reset_outside_diff=%u\n",
                d.inside, d.outside);
}
void functional(const char* programs, const char* rect_arg) {
    Fixture f(W, H);
    Functional x(f, programs);
    Pass P(f, x.slots);
    need(P.pass.allocations() == 5 && P.pass.references() == 12, "five-target pool and seven programs");
    std::printf("STEP_A_ATTACH supported=%u available=%u allocations=%u references=%u\n",
                P.pass.caps().supported_policies, P.pass.caps().available_policies, P.pass.allocations(),
                P.pass.references());
    corpus(x, P, rect_arg);
    Sources s;
    s.load_pair(f, programs, 0);
    need(SUCCEEDED(s.created[0]), "pair 0 packed producer");
    const float enc[4] = {float(encode(fade_L[0])), float(encode(fade_L[1])), float(encode(fade_L[2])), fade_a},
                lin[4] = {fade_L[0], fade_L[1], fade_L[2], fade_a}, one[4] = {1, 1, 1, 1};
    const float em_native[4] = {.1f, .2f, .05f, 0}, em_e[4] = {.4f, .3f, .2f, 0}, em_m[4] = {1, 1, 1, 0};
    const auto fade_words = constant_ps(enc, lin, one), emission_words = constant_ps(em_native, em_e, em_m);
    Com<IDirect3DPixelShader9> fade_ps, emission_ps;
    api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(fade_words.data()), &fade_ps.p), "fade producer");
    api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(emission_words.data()), &emission_ps.p),
        "emission producer");
    sequences(x, P, s, fade_ps.p);
    alias(x, P, s, emission_ps.p);
    ladder(x, P, s);
    capability(x);
    reset_case(x, P, s);
    std::printf(
        "STEP_A_COMPLETE policy=8 pairs=9 cases=20 schedules=3 owned_targets=5 target_bytes=%llu live_publication=0\n",
        5ull * W * H * 8);
}
// Paired EVENT-fenced windows: the native source alone against the policy-8
// bracket (prepare/source/finish) for 1/4/16 DIPs per frame at the run-11
// median (2352 px) and maximum (24150 px) rectangle areas. M is cleared
// outside the window. Diagnostic timings on a detached device, not game FPS.
void rect_vertices(Fixture& f, const RECT& rect) {
    Vertex vertices[8];
    for (unsigned i = 0; i < 2; ++i) {
        const float x0 = float(rect.left) + (i ? .25f : .05f) * float(rect.right - rect.left),
                    x1 = float(rect.left) + (i ? .95f : .75f) * float(rect.right - rect.left),
                    y0 = float(rect.top) + .05f * float(rect.bottom - rect.top),
                    y1 = float(rect.top) + .95f * float(rect.bottom - rect.top);
        const float l = -1 + x0 * 2 / f.width, r = -1 + x1 * 2 / f.width, t = 1 - y0 * 2 / f.height,
                    b = 1 - y1 * 2 / f.height;
        const float z = i ? .3f : .2f;
        const DWORD color = ((i ? 192u : 128u) << 24) | 0x3070d0u;
        Vertex quad[] = {{l, t, z, i ? .75f : .25f, .5f, color},
                         {r, t, z, i ? .75f : .25f, .5f, color},
                         {l, b, z, i ? .75f : .25f, .5f, color},
                         {r, b, z, i ? .75f : .25f, .5f, color}};
        std::memcpy(vertices + i * 4, quad, sizeof quad);
    }
    void* data = nullptr;
    api(f.vb->Lock(0, 0, &data, 0), "timing vertices lock");
    std::memcpy(data, vertices, sizeof vertices);
    api(f.vb->Unlock(), "timing vertices unlock");
}
void timing(const char* programs) {
    for (const auto& size : {std::make_pair(1280u, 768u), std::make_pair(1920u, 1080u)}) {
        const unsigned w = size.first, h = size.second;
        Fixture f(w, h);
        Functional x(f, programs);
        Pass P(f, x.slots);
        Sources s;
        s.load_pair(f, programs, 0);
        need(SUCCEEDED(s.created[0]), "timing packed producer");
        f.prepare(0, s.layout, false);
        for (const auto& area : {std::make_pair(56L, 42L), std::make_pair(150L, 161L)}) {
            const RECT rect{LONG(w / 2) - area.first / 2, LONG(h / 2) - area.second / 2,
                            LONG(w / 2) - area.first / 2 + area.first, LONG(h / 2) - area.second / 2 + area.second};
            rect_vertices(f, rect);
            for (const bool packed : {false, true})
                for (const unsigned dips : {1u, 4u, 16u})
                    for (unsigned iteration = 0; iteration < 10; ++iteration) {
                        x.reset_work();
                        bind_native(f, x.work.surface.p, 0, s);
                        need(P.pass.begin_frame(x.frame).ready, "timing frame");
                        f.fence();
                        LARGE_INTEGER begin, end;
                        QueryPerformanceCounter(&begin);
                        api(f.d->BeginScene(), "timing BeginScene");
                        for (unsigned k = 0; k < dips; ++k) {
                            if (!packed) {
                                api(issue(f, 0), "timing native source");
                                continue;
                            }
                            const auto boundary = packed_boundary(x.work.surface.p, s.packed[0].p, x.frame, &rect);
                            need(P.pass.prepare(boundary).ready, "timing bracket prepared");
                            need(P.pass.finish(issue(f, 0)).image == LinearEmissionImage::Linear,
                                 "timing bracket completed");
                        }
                        api(f.d->EndScene(), "timing EndScene");
                        f.fence();
                        QueryPerformanceCounter(&end);
                        ++x.frame;
                        if (iteration >= 2)
                            std::printf(
                                "STEP_A_TIMING width=%u height=%u policy=%s rect=%ld,%ld,%ld,%ld area=%ld dips=%u iteration=%u completed_ms=%.6f\n",
                                w, h, packed ? "packed" : "native", rect.left, rect.top, rect.right, rect.bottom,
                                area.first * area.second, dips, iteration - 2,
                                1000. * double(end.QuadPart - begin.QuadPart) / double(f.frequency.QuadPart));
                    }
        }
        f.detach();
        api(f.d->SetRenderTarget(0, f.native.surface.p), "timing RT0");
    }
    std::printf("STEP_A_TIMING_RESULT sizes=2 rects=2 policies=2 dips=3 iterations=8\n");
}
} // namespace
int main(int argc, char** argv) {
    // Unbuffered: every witness line reaches the runner's file as it happens.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        need(argc >= 2 && argc <= 4, "usage: step_a.exe local-program-directory [timing | --rect l,t,r,b]");
        const char* rect = nullptr;
        bool bench = false;
        for (int i = 2; i < argc; ++i) {
            if (!std::strcmp(argv[i], "timing"))
                bench = true;
            else if (!std::strncmp(argv[i], "--rect=", 7))
                rect = argv[i] + 7;
            else
                need(false, "unknown argument");
        }
        if (bench)
            timing(argv[1]);
        else
            functional(argv[1], rect);
        std::printf("STEP_A_EXIT\n");
        std::fflush(stdout);
        return 0;
    } catch (const std::exception& e) {
        std::printf("STEP_A_ABORT reason=%s\n", e.what());
        return 1;
    }
}
