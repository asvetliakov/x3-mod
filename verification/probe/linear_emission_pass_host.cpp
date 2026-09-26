// Host control-flow/COM ownership tests of the entire production component.
// These scripted public interfaces neither execute shaders nor prove x86 ABI.
#include "../../src/renderer/linear_emission_pass.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace x3m::renderer;
namespace {
unsigned checks = 0, scenarios = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) {
        std::cerr << "FAILED scenario " << scenarios << ": " << message << '\n';
        std::exit(1);
    }
}
// DWORD index of the composite's `def c1` token (generate_screen_emission_programs.gain_literal_index).
constexpr unsigned gain_literal_index = 25;
struct Device : IDirect3DDevice9 {
    void* slots[119]{};
    std::vector<std::vector<DWORD>> created_ps; // every CreatePixelShader payload, in order
    D3DCAPS9 caps{};
    IDirect3D9 factory;
    std::vector<std::unique_ptr<IUnknown>> objects;
    IDirect3DSurface9 *rt[4]{}, *depth = nullptr, *back = nullptr;
    IDirect3DBaseTexture9* textures[5]{};
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    IDirect3DVertexBuffer9* stream = nullptr;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    DWORD fvf = 0, freq = 1, rs[27]{}, ss[5][8]{};
    unsigned stage_gets = 0, stage_sets = 0; // sampler-stage texture/state traffic
    unsigned offset = 0, stride = 0, draws = 0, source_draws = 0, clears = 0, texture_creates = 0;
    unsigned bad_state_calls = 0, bad_rt_calls = 0, rs3_calls = 0, bad_back_binds = 0;
    IDirect3DSurface9* draw_rt[3]{};
    DWORD draw_blend = 1;
    bool fail_energy_bind = false;
    IDirect3DVertexShader9* fail_after_vs = nullptr;
    bool device_calls_forbidden = false;
    unsigned forbidden_device_accesses = 0;
    int fault_slot = -1;
    unsigned fault_at = 1, fault_calls = 0;
    bool fault_null = false;
    HRESULT fault_hr = E_FAIL;
    unsigned level_fault_at = 0;
    bool level_null = false;
    HRESULT level_hr = E_FAIL;
    bool alias_pool = false;
    unsigned pool_qi_at = 0, pool_qi_mode = 0;
    IDirect3DSurface9* first_pool = nullptr;
    template <class T> T* make() {
        auto p = std::make_unique<T>();
        auto* raw = p.get();
        objects.push_back(std::move(p));
        return raw;
    }
    bool fault(unsigned slot) { return int(slot) == fault_slot && ++fault_calls == fault_at; }
    template <class T> HRESULT output(unsigned slot, T* object, T** out) {
        bool f = fault(slot);
        *out = f && fault_null ? nullptr : object;
        if (*out) (*out)->AddRef();
        return f ? fault_hr : S_OK;
    }
    template <class T> static void bind(T*& old, T* value) {
        if (value) value->AddRef();
        if (old) old->Release();
        old = value;
    }
    bool supported(D3DRENDERSTATETYPE s) {
        if (s == D3DRS_COLORWRITEENABLE) return caps.PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE;
        if (s == D3DRS_SEPARATEALPHABLENDENABLE) return caps.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND;
        unsigned n = s == D3DRS_COLORWRITEENABLE1   ? 1
                     : s == D3DRS_COLORWRITEENABLE2 ? 2
                     : s == D3DRS_COLORWRITEENABLE3 ? 3
                                                    : 0;
        return !n || (n < caps.NumSimultaneousRTs && (caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS));
    }
    static Device& d(IDirect3DDevice9* p) {
        auto& device = *static_cast<Device*>(p);
        if (device.device_calls_forbidden) ++device.forbidden_device_accesses;
        return device;
    }
    template <class F> void slot(unsigned n, F f) { slots[n] = reinterpret_cast<void*>(+f); }
    Device() {
        slot(6, [](IDirect3DDevice9* p, IDirect3D9** o) -> HRESULT { return d(p).output(6, &d(p).factory, o); });
        slot(9, [](IDirect3DDevice9* p, D3DDEVICE_CREATION_PARAMETERS* o) -> HRESULT {
            (void)d(p);
            *o = {};
            return S_OK;
        });
        slot(18, [](IDirect3DDevice9* p, UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9** o) -> HRESULT {
            return d(p).output(18, d(p).back, o);
        });
        slot(23,
             [](IDirect3DDevice9* p, UINT w, UINT h, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9** o,
                HANDLE*) -> HRESULT {
                 auto& a = d(p);
                 auto* t = a.make<IDirect3DTexture9>();
                 auto* s = a.make<IDirect3DSurface9>();
                 t->level = s;
                 s->texture = t;
                 s->desc.Width = w;
                 s->desc.Height = h;
                 ++a.texture_creates;
                 if (a.texture_creates == a.level_fault_at) {
                     t->level_hr = a.level_hr;
                     t->level_null = a.level_null;
                     t->level_output_on_failure = true;
                 }
                 if (a.alias_pool && a.first_pool) s->identity = a.first_pool;
                 if (a.texture_creates == a.pool_qi_at) {
                     s->qi_hr = a.pool_qi_mode == 0 ? S_OK : E_FAIL;
                     s->qi_null = a.pool_qi_mode == 0;
                     s->qi_output_on_failure = a.pool_qi_mode == 2;
                 }
                 if (!a.first_pool) a.first_pool = s;
                 return a.output(23, t, o);
             });
        slot(37, [](IDirect3DDevice9* p, DWORD i, IDirect3DSurface9* v) -> HRESULT {
            auto& a = d(p);
            if (i >= a.caps.NumSimultaneousRTs) {
                ++a.bad_rt_calls;
                return E_FAIL;
            }
            if (i == 1 && v && a.fail_energy_bind) {
                a.fail_energy_bind = false;
                return E_FAIL;
            }
            // Reset cases provide a backbuffer with a deliberately incompatible
            // saved depth pairing; color MRTs and DS must be detached first.
            if (i == 0 && v == a.back && (a.rt[1] || a.rt[2] || (v->desc.Format == D3DFMT_A8R8G8B8 && a.depth))) {
                ++a.bad_back_binds;
                return E_FAIL;
            }
            bind(a.rt[i], v);
            return S_OK;
        });
        slot(38, [](IDirect3DDevice9* p, DWORD i, IDirect3DSurface9** o) -> HRESULT {
            auto& a = d(p);
            if (i >= a.caps.NumSimultaneousRTs) {
                ++a.bad_rt_calls;
                *o = nullptr;
                return E_FAIL;
            }
            auto hr = a.output(38, a.rt[i], o);
            return SUCCEEDED(hr) && !*o ? D3DERR_NOTFOUND : hr;
        });
        slot(39, [](IDirect3DDevice9* p, IDirect3DSurface9* v) -> HRESULT {
            bind(d(p).depth, v);
            return S_OK;
        });
        slot(40, [](IDirect3DDevice9* p, IDirect3DSurface9** o) -> HRESULT {
            auto hr = d(p).output(40, d(p).depth, o);
            return SUCCEEDED(hr) && !*o ? D3DERR_NOTFOUND : hr;
        });
        slot(43, [](IDirect3DDevice9* p, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD) -> HRESULT {
            ++d(p).clears;
            return S_OK;
        });
        slot(47, [](IDirect3DDevice9* p, D3DVIEWPORT9* v) -> HRESULT {
            d(p).viewport = *v;
            return S_OK;
        });
        slot(48, [](IDirect3DDevice9* p, D3DVIEWPORT9* v) -> HRESULT {
            *v = d(p).viewport;
            return S_OK;
        });
        slot(57, [](IDirect3DDevice9* p, D3DRENDERSTATETYPE s, DWORD v) -> HRESULT {
            auto& a = d(p);
            if (s == D3DRS_COLORWRITEENABLE3) ++a.rs3_calls;
            if (!a.supported(s)) {
                ++a.bad_state_calls;
                return E_FAIL;
            }
            a.rs[s] = v;
            return S_OK;
        });
        slot(58, [](IDirect3DDevice9* p, D3DRENDERSTATETYPE s, DWORD* v) -> HRESULT {
            auto& a = d(p);
            if (s == D3DRS_COLORWRITEENABLE3) ++a.rs3_calls;
            if (!a.supported(s)) {
                ++a.bad_state_calls;
                return E_FAIL;
            }
            *v = a.rs[s];
            return S_OK;
        });
        slot(64, [](IDirect3DDevice9* p, DWORD i, IDirect3DBaseTexture9** o) -> HRESULT {
            if (i >= 5) std::abort(); // stage inventory bound
            ++d(p).stage_gets;
            return d(p).output(64, d(p).textures[i], o);
        });
        slot(65, [](IDirect3DDevice9* p, DWORD i, IDirect3DBaseTexture9* v) -> HRESULT {
            if (i >= 5) std::abort(); // stage inventory bound
            ++d(p).stage_sets;
            bind(d(p).textures[i], v);
            return S_OK;
        });
        slot(68, [](IDirect3DDevice9* p, DWORD i, D3DSAMPLERSTATETYPE s, DWORD* v) -> HRESULT {
            if (i >= 5) std::abort(); // stage inventory bound
            ++d(p).stage_gets;
            *v = d(p).ss[i][s];
            return S_OK;
        });
        slot(69, [](IDirect3DDevice9* p, DWORD i, D3DSAMPLERSTATETYPE s, DWORD v) -> HRESULT {
            if (i >= 5) std::abort(); // stage inventory bound
            ++d(p).stage_sets;
            d(p).ss[i][s] = v;
            return S_OK;
        });
        slot(75, [](IDirect3DDevice9* p, RECT* v) -> HRESULT {
            d(p).scissor = *v;
            return S_OK;
        });
        slot(76, [](IDirect3DDevice9* p, RECT* v) -> HRESULT {
            *v = d(p).scissor;
            return S_OK;
        });
        slot(83, [](IDirect3DDevice9* p, D3DPRIMITIVETYPE, UINT, const void*, UINT) -> HRESULT {
            auto& a = d(p);
            ++a.draws;
            for (unsigned i = 0; i < 3; ++i) a.draw_rt[i] = a.rt[i];
            a.draw_blend = a.rs[D3DRS_ALPHABLENDENABLE];
            return a.fault(83) ? E_FAIL : S_OK;
        });
        slot(86, [](IDirect3DDevice9* p, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9** o) -> HRESULT {
            return d(p).output(86, d(p).make<IDirect3DVertexDeclaration9>(), o);
        });
        slot(87, [](IDirect3DDevice9* p, IDirect3DVertexDeclaration9* v) -> HRESULT {
            bind(d(p).decl, v);
            return S_OK;
        });
        slot(88, [](IDirect3DDevice9* p, IDirect3DVertexDeclaration9** o) -> HRESULT {
            return d(p).output(88, d(p).decl, o);
        });
        slot(89, [](IDirect3DDevice9* p, DWORD v) -> HRESULT {
            d(p).fvf = v;
            return S_OK;
        });
        slot(90, [](IDirect3DDevice9* p, DWORD* v) -> HRESULT {
            *v = d(p).fvf;
            return S_OK;
        });
        slot(91, [](IDirect3DDevice9* p, const DWORD*, IDirect3DVertexShader9** o) -> HRESULT {
            return d(p).output(91, d(p).make<IDirect3DVertexShader9>(), o);
        });
        slot(92, [](IDirect3DDevice9* p, IDirect3DVertexShader9* v) -> HRESULT {
            auto& a = d(p);
            bind(a.vs, v);
            if (v && v == a.fail_after_vs) {
                a.fail_after_vs = nullptr;
                return E_FAIL;
            }
            return S_OK;
        });
        slot(93,
             [](IDirect3DDevice9* p, IDirect3DVertexShader9** o) -> HRESULT { return d(p).output(93, d(p).vs, o); });
        slot(100, [](IDirect3DDevice9* p, UINT, IDirect3DVertexBuffer9* v, UINT o, UINT s) -> HRESULT {
            auto& a = d(p);
            bind(a.stream, v);
            a.offset = o;
            a.stride = s;
            return S_OK;
        });
        slot(101, [](IDirect3DDevice9* p, UINT, IDirect3DVertexBuffer9** v, UINT* o, UINT* s) -> HRESULT {
            auto& a = d(p);
            *o = a.offset;
            *s = a.stride;
            return a.output(101, a.stream, v);
        });
        slot(102, [](IDirect3DDevice9* p, UINT, UINT v) -> HRESULT {
            d(p).freq = v;
            return S_OK;
        });
        slot(103, [](IDirect3DDevice9* p, UINT, UINT* v) -> HRESULT {
            *v = d(p).freq;
            return S_OK;
        });
        slot(106, [](IDirect3DDevice9* p, const DWORD* words, IDirect3DPixelShader9** o) -> HRESULT {
            std::vector<DWORD> copy;
            for (const DWORD* w = words;; ++w) {
                copy.push_back(*w);
                if (*w == 0x0000ffffu) break;
            }
            d(p).created_ps.push_back(std::move(copy));
            return d(p).output(106, d(p).make<IDirect3DPixelShader9>(), o);
        });
        slot(107, [](IDirect3DDevice9* p, IDirect3DPixelShader9* v) -> HRESULT {
            bind(d(p).ps, v);
            return S_OK;
        });
        slot(108,
             [](IDirect3DDevice9* p, IDirect3DPixelShader9** o) -> HRESULT { return d(p).output(108, d(p).ps, o); });
        // No production path may submit the application's source draw.
        slot(82, [](IDirect3DDevice9* p, D3DPRIMITIVETYPE, int, UINT, UINT, UINT, UINT) -> HRESULT {
            ++d(p).source_draws;
            return S_OK;
        });
    }
    void unbind() {
        for (auto*& t : textures) bind(t, static_cast<IDirect3DBaseTexture9*>(nullptr));
        for (auto*& r : rt) bind(r, static_cast<IDirect3DSurface9*>(nullptr));
        bind(depth, static_cast<IDirect3DSurface9*>(nullptr));
        bind(ps, static_cast<IDirect3DPixelShader9*>(nullptr));
        bind(vs, static_cast<IDirect3DVertexShader9*>(nullptr));
        bind(decl, static_cast<IDirect3DVertexDeclaration9*>(nullptr));
        bind(stream, static_cast<IDirect3DVertexBuffer9*>(nullptr));
    }
    void no_leaks() {
        unbind();
        check(factory.refs == 0, "factory leaked");
        for (auto& v : objects) check(v->refs == 0, "COM output leaked");
        check(source_draws == 0, "source replayed");
    }
    IDirect3DSurface9* scene() {
        auto* t = make<IDirect3DTexture9>();
        auto* s = make<IDirect3DSurface9>();
        s->texture = t;
        t->level = s;
        bind(rt[0], s);
        auto* z = make<IDirect3DSurface9>();
        z->desc.Format = D3DFMT_D24S8;
        bind(depth, z);
        back = s;
        rs[D3DRS_ALPHABLENDENABLE] = 1;
        rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
        rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
        rs[D3DRS_DESTBLEND] = D3DBLEND_ONE;
        rs[D3DRS_COLORWRITEENABLE] = 15;
        rs[D3DRS_ZENABLE] = D3DZB_TRUE;
        return s;
    }
    void fault_output(unsigned slot, unsigned nth, bool null) {
        fault_slot = slot;
        fault_at = nth;
        fault_calls = 0;
        fault_null = null;
        fault_hr = null ? S_OK : E_FAIL;
    }
};
HRESULT attach(LinearEmissionPass& p, Device& d) {
    return p.attach(&d, d.slots, d.caps, D3DFMT_A8R8G8B8, D3DFMT_D24S8);
}
void setup(LinearEmissionPass& p, Device& d) {
    check(attach(p, d) == S_OK, "attach");
    check(p.ensure_targets(17, 11) == S_OK, "targets");
    check(p.allocations() == 4 && p.references() == 8, "committed pool count");
}
void outputs() {
    for (unsigned slot : {6u, 106u, 91u, 86u})
        for (unsigned nth = 1; nth <= (slot == 106 ? 2u : 1u); ++nth)
            for (bool null : {false, true}) {
                ++scenarios;
                Device d;
                LinearEmissionPass p;
                d.fault_output(slot, nth, null);
                check(FAILED(attach(p, d)), "attach accepted malformed output");
                check(!p.caps().enabled && p.references() == 0, "partial programs retained");
                p.detach();
                d.no_leaks();
            }
    for (bool level : {false, true})
        for (unsigned nth = 1; nth <= 4; ++nth)
            for (bool null : {false, true}) {
                ++scenarios;
                Device d;
                LinearEmissionPass p;
                check(attach(p, d) == S_OK, "attach for allocation");
                if (level) {
                    d.level_fault_at = nth;
                    d.level_null = null;
                    d.level_hr = null ? S_OK : E_FAIL;
                } else
                    d.fault_output(23, nth, null);
                check(FAILED(p.ensure_targets(17, 11)), "allocation accepted malformed output");
                check(p.allocations() == 0 && p.references() == 4 && !p.coverage_target(),
                      "partial target pool committed");
                d.fault_slot = -1;
                d.level_fault_at = 0;
                check(p.ensure_targets(17, 11) == S_OK, "allocation retry");
                check(p.allocations() == 4 && p.references() == 8, "retry count");
                check(p.ensure_targets(17, 11) == S_OK && p.allocations() == 4, "same size allocated again");
                p.detach();
                d.no_leaks();
            }
}
void pool_identity() {
    ++scenarios;
    Device d;
    LinearEmissionPass p;
    check(attach(p, d) == S_OK, "attach alias");
    d.alias_pool = true;
    check(FAILED(p.ensure_targets(17, 11)), "canonical pool aliases admitted");
    check(p.allocations() == 0 && p.references() == 4, "aliased pool committed");
    p.detach();
    d.no_leaks();
}
void identity_faults() {
    for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned nth : {1u, 2u}) {
            ++scenarios;
            Device d;
            LinearEmissionPass p;
            check(attach(p, d) == S_OK, "attach pool QI");
            d.pool_qi_at = nth;
            d.pool_qi_mode = mode;
            check(FAILED(p.ensure_targets(17, 11)), "unknown pool identity admitted");
            check(p.allocations() == 0 && p.references() == 4, "unknown pool committed");
            p.detach();
            d.no_leaks();
        }
    // RT0 exact pointer equality is known, but source-versus-owned targets must
    // still establish distinct identities. Fault one side of that comparison.
    for (unsigned mode = 0; mode < 3; ++mode)
        for (bool source_side : {false, true}) {
            ++scenarios;
            Device d;
            auto* a = d.scene();
            LinearEmissionPass p;
            setup(p, d);
            check(p.begin_frame(1).ready, "identity frame");
            auto* bad = source_side ? a : p.fixture_native();
            bad->qi_hr = mode == 0 ? S_OK : E_FAIL;
            bad->qi_null = mode == 0;
            bad->qi_output_on_failure = mode == 2;
            unsigned before = bad->refs;
            auto* aug = d.make<IDirect3DPixelShader9>();
            auto prep = p.prepare({a, aug, 1, true});
            check(!prep.ready && prep.state_preserved, "source unknown identity admitted");
            check(bad->refs == before, "source identity leaked QI output");
            check(d.rt[0] == a && p.coverage_valid() && d.draws == 0,
                  "clean identity refusal changed image or coverage");
            bad->qi_hr = S_OK;
            bad->qi_null = false;
            bad->qi_output_on_failure = false;
            p.detach();
            d.no_leaks();
        }
}
void allocation_rollback() {
    ++scenarios;
    Device d;
    LinearEmissionPass p;
    setup(p, d);
    auto* old = p.coverage_target();
    d.fault_output(23, 3, false);
    check(FAILED(p.ensure_targets(18, 12)), "failed resize accepted");
    check(p.coverage_target() == old && p.allocations() == 4 && p.references() == 8,
          "failed resize destroyed committed pool");
    d.fault_slot = -1;
    check(p.ensure_targets(18, 12) == S_OK, "resize retry");
    check(p.coverage_target() != old && p.allocations() == 8, "resize allocation commit count");
    p.detach();
    d.no_leaks();
}
void reset_outputs() {
    for (unsigned mode = 0; mode < 3; ++mode) {
        ++scenarios;
        Device d;
        LinearEmissionPass p;
        setup(p, d);
        auto* t = d.make<IDirect3DTexture9>();
        t->level = p.coverage_target();
        Device::bind(d.textures[0], static_cast<IDirect3DBaseTexture9*>(t));
        if (mode == 0) d.fault_output(64, 1, false);
        if (mode == 1) {
            t->qi_hr = E_FAIL;
            t->qi_output_on_failure = true;
        }
        if (mode == 2) {
            t->level_hr = E_FAIL;
            t->level_output_on_failure = true;
        }
        unsigned refs = t->refs;
        p.before_reset();
        check(t->refs == refs, "reset getter or QI leaked");
        check(p.references() == 4 && !p.coverage_target(), "reset retained target");
        d.fault_slot = -1;
        t->qi_hr = S_OK;
        t->level = nullptr;
        p.detach();
        d.no_leaks();
    }
}
void draw_contract(bool optional_caps, bool fail_source, bool alias_scene, bool null_identity) {
    ++scenarios;
    Device d;
    if (optional_caps)
        d.caps.PrimitiveMiscCaps |= D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_SEPARATEALPHABLEND |
                                    D3DPMISCCAPS_INDEPENDENTWRITEMASKS;
    auto* a = d.scene();
    a->AddRef(); // caller's owning scene slot
    auto* boundary_scene = a;
    if (alias_scene) {
        boundary_scene = d.make<IDirect3DSurface9>();
        boundary_scene->identity = a;
        boundary_scene->texture = a->texture;
    }
    if (null_identity) {
        a->qi_null = true;
        boundary_scene->qi_null = true;
    }
    LinearEmissionPass p;
    setup(p, d);
    check(p.begin_frame(1).ready, "begin frame");
    check(!p.begin_frame(1).ready, "same frame recleared");
    auto* aug = d.make<IDirect3DPixelShader9>();
    LinearEmissionBoundary b{boundary_scene, aug, 1, true};
    auto prepared = p.prepare(b);
    if (null_identity) {
        check(!prepared.ready, "null canonical identities admitted");
        check(p.coverage_valid(), "clean refusal invalidated prior coverage");
        a->Release();
        p.detach();
        d.no_leaks();
        return;
    }
    check(prepared.ready && p.reference_accounting_busy(), "prepare or saved ownership");
    check(!p.owning_candidate(), "premature owning slot");
    check(!p.coverage_valid(), "prepared coverage exposed");
    unsigned draws = d.draws;
    auto done = p.finish(fail_source ? E_FAIL : S_OK);
    check(done.candidate_bound, "candidate not bound");
    check(!p.coverage_valid(), "pending coverage exposed");
    check(done.image == (fail_source ? LinearEmissionImage::Incomplete : LinearEmissionImage::Linear),
          "completion image");
    check(d.draws == draws + (fail_source ? 0 : 1), "failed source composed or replayed");
    check(p.reference_accounting_busy(), "pending saved references discarded");
    auto** slot = p.owning_candidate();
    check(slot && *slot == d.rt[0], "owning slot mismatch");
    check(p.acknowledge_exchange(true) == E_INVALIDARG, "ack before exchange");
    check(p.acknowledge_exchange(false) == S_FALSE, "negative ack");
    check(!p.coverage_valid(), "negative acknowledgement exposed coverage");
    // Model an owning slot containing exactly boundary.scene. GetRenderTarget(0)
    // may separately return the canonical alias; identity equality admits that
    // getter, but acknowledgement still requires the exact boundary.scene
    // pointer.
    if (alias_scene) {
        boundary_scene->AddRef();
        a->Release();
        a = boundary_scene;
    }
    std::swap(a, *slot);
    check(p.acknowledge_exchange(true) == S_OK, "owning exchange ack");
    check(!p.reference_accounting_busy() && !p.owning_candidate(), "ack retained bracket");
    check(p.fixture_completion().image == done.image, "ack changed completion validity");
    check(p.coverage_valid() == !fail_source, "coverage/source outcome");
    if (fail_source) {
        b.scene = a;
        check(!p.prepare(b).ready, "failed source unblocked by ack");
        check(p.recover_native().image == LinearEmissionImage::None, "ack left recovery pending");
    }
    check(d.bad_state_calls == 0 && d.bad_rt_calls == 0 && d.rs3_calls == 0, "unsupported state or RT accessed");
    a->Release();
    p.detach();
    d.no_leaks();
}
void clean_refusals() {
    for (auto fault : {LinearEmissionPassFault::Save, LinearEmissionPassFault::Copy,
                       LinearEmissionPassFault::EmissionClear, LinearEmissionPassFault::SourceBind}) {
        ++scenarios;
        Device d;
        auto* a = d.scene();
        LinearEmissionPass p;
        setup(p, d);
        check(p.begin_frame(1).ready, "refusal frame");
        auto* mask = p.coverage_target();
        auto* aug = d.make<IDirect3DPixelShader9>();
        auto prior = d.rs[D3DRS_ALPHABLENDENABLE];
        p.inject(fault);
        auto prep = p.prepare({a, aug, 1, true});
        check(!prep.ready && prep.state_preserved, "clean refusal did not restore");
        check(d.rt[0] == a && d.rs[D3DRS_ALPHABLENDENABLE] == prior, "refusal changed application target/state");
        check(p.coverage_target() == mask && p.coverage_valid() && !p.reference_accounting_busy(),
              "clean refusal invalidated prior coverage or retained getters");
        check(p.prepare({a, aug, 1, true}).ready, "clean refusal blocked subsequent enhancement");
        p.before_reset();
        p.detach();
        d.no_leaks();
    }
    // A failing save getter can populate an owned output before returning
    // failure.
    ++scenarios;
    Device d;
    auto* a = d.scene();
    auto* t = d.make<IDirect3DTexture9>();
    Device::bind(d.textures[0], static_cast<IDirect3DBaseTexture9*>(t));
    LinearEmissionPass p;
    setup(p, d);
    check(p.begin_frame(1).ready, "save output frame");
    d.fault_output(64, 1, false);
    unsigned before = t->refs;
    auto prep = p.prepare({a, d.make<IDirect3DPixelShader9>(), 1, true});
    check(!prep.ready && prep.state_preserved && p.coverage_valid(), "failed save policy");
    check(t->refs == before && !p.reference_accounting_busy(), "failed save output leaked");
    d.fault_slot = -1;
    p.detach();
    d.no_leaks();
}
void reset_brackets() {
    for (bool pending : {false, true}) {
        ++scenarios;
        Device d;
        auto* a = d.scene();
        d.back = d.make<IDirect3DSurface9>();
        d.back->desc.Format = D3DFMT_A8R8G8B8;
        LinearEmissionPass p;
        setup(p, d);
        check(p.begin_frame(1).ready, "reset bracket frame");
        auto* b = p.fixture_native();
        auto* e = p.fixture_energy();
        auto* m = p.coverage_target();
        auto* aug = d.make<IDirect3DPixelShader9>();
        check(p.prepare({a, aug, 1, true}).ready, "reset bracket prepare");
        if (pending) check(p.finish(S_OK).candidate_bound, "reset pending candidate");
        auto* bound = d.rt[0];
        check(p.reference_accounting_busy(), "reset bracket expected retained state");
        p.before_reset();
        check(!d.bad_back_binds && d.rt[0] == d.back && !d.rt[1] && !d.rt[2] && !d.depth,
              "reset must detach MRTs and depth before replacing RT0");
        check(b->refs == 0 && e->refs == 0 && m->refs == 0 && bound->refs == 0, "reset retained owned target binding");
        check(p.references() == 4 && !p.coverage_valid() && !p.reference_accounting_busy() && !p.owning_candidate(),
              "reset retained bracket state");
        check(p.ensure_targets(17, 11) == S_OK && p.allocations() == 8, "reset cannot recreate pool");
        check(p.begin_frame(2).ready, "reset cannot begin next frame");
        p.detach();
        d.no_leaks();
    }
}

void detach_after_failed_reset() {
    for (unsigned phase = 0; phase < 3; ++phase) {
        ++scenarios;
        Device d;
        auto* a = d.scene();
        LinearEmissionPass p;
        setup(p, d);
        if (phase) {
            check(p.begin_frame(1).ready, "failed Reset frame");
            check(p.prepare({a, d.make<IDirect3DPixelShader9>(), 1, true}).ready, "failed Reset prepare");
            if (phase == 2) check(p.finish(S_OK).candidate_bound, "failed Reset pending");
        }
        p.before_reset();
        check(p.references() == 4 && !p.reference_accounting_busy(),
              "before Reset must release default-pool and saved interfaces");
        // The caller's native Reset failed: device getters/setters are unavailable.
        // A repeated cleanup and detach must only release the retained programs.
        d.device_calls_forbidden = true;
        p.before_reset();
        p.detach();
        check(d.forbidden_device_accesses == 0, "post-failed-Reset cleanup touched device");
        check(p.references() == 0, "post-failed-Reset detach retained programs");
        d.no_leaks();
    }
}

void recovery() {
    for (unsigned mode = 0; mode < 4; ++mode) {
        ++scenarios;
        Device d;
        auto* a = d.scene();
        a->AddRef();
        LinearEmissionPass p;
        setup(p, d);
        check(p.begin_frame(1).ready, "recovery frame");
        check(p.prepare({a, d.make<IDirect3DPixelShader9>(), 1, true}).ready, "recovery prepare");
        if (mode == 0)
            p.inject(LinearEmissionPassFault::Composite);
        else if (mode != 3)
            p.inject(LinearEmissionPassFault::Restore);
        auto done = p.finish(mode == 3 ? E_FAIL : S_OK);
        unsigned draws = d.draws;
        if (mode == 0) check(done.image == LinearEmissionImage::Native && done.candidate_bound, "composite fallback");
        if (mode == 1 || mode == 2)
            check(done.image == LinearEmissionImage::Incomplete && !p.owning_candidate() && !p.coverage_valid(),
                  "restore failure exposed candidate");
        if (mode == 2) p.inject(LinearEmissionPassFault::RecoveryRestore);
        auto recovered = p.recover_native();
        check(!p.coverage_valid(), "pending recovery exposed coverage");
        check(d.draws == draws, "recovery replayed draw");
        check(recovered.image ==
                  ((mode == 2 || mode == 3) ? LinearEmissionImage::Incomplete : LinearEmissionImage::Native),
              "recovery image");
        check(p.recover_native().image == LinearEmissionImage::None, "second recovery allowed");
        if (mode == 2) {
            check(!p.owning_candidate() && !p.coverage_valid(), "failed recovery exposed candidate");
        } else {
            auto** slot = p.owning_candidate();
            check(slot && *slot == d.rt[0], "recovery owning slot");
            std::swap(a, *slot);
            check(p.acknowledge_exchange(true) == S_OK, "recovery ack");
            check(p.coverage_valid() == (mode == 0), "recovery acknowledgement validity");
        }
        a->Release();
        p.detach();
        d.no_leaks();
    }
}

void fused_preparation() {
    for (unsigned mode = 0; mode < 4; ++mode) {
        ++scenarios;
        Device d;
        auto* a = d.scene();
        a->AddRef();
        LinearEmissionPass p;
        const bool separate = mode == 0;
        p.fixture_separate_copy(separate);
        setup(p, d);
        check(p.begin_frame(1).ready, "fused frame initialization");
        const unsigned clears = d.clears, draws = d.draws;
        if (mode == 2) {
            d.fault_slot = 83;
            d.fault_at = 1;
            d.fault_calls = 0;
        }
        if (mode == 3) d.fail_energy_bind = true;
        auto* ps = d.make<IDirect3DPixelShader9>();
        const auto ready = p.prepare({a, ps, 1, true});
        if (mode >= 2) {
            check(d.draws == draws + (mode == 2 ? 1 : 0), "fused failure missed intended copy boundary");
            if (mode == 2)
                check(d.draw_rt[0] == p.fixture_native() && d.draw_rt[1] == p.fixture_energy() && !d.draw_rt[2] &&
                          !d.draw_blend,
                      "failed copy attempted wrong MRT state");
            check(!ready.ready && ready.state_preserved, "fused preparation failure was not clean");
            check(d.rt[0] == a && !d.rt[1] && !d.rt[2], "fused failure did not restore application targets");
            check(p.coverage_valid() && !p.reference_accounting_busy(), "fused refusal lost earlier M");
            check(p.finish(S_OK).image == LinearEmissionImage::None, "refused source became submitted");
        } else {
            check(ready.ready, "fused/separate prepare");
            check(d.draws == draws + 1, "copy rasterizations changed");
            check(d.clears == clears + (separate ? 1 : 0), "fused copy retained energy Clear");
            check(d.draw_rt[0] == p.fixture_native(), "copy did not write native B");
            check(d.draw_rt[1] == (separate ? nullptr : p.fixture_energy()), "fused energy attachment");
            check(!d.draw_rt[2] && !d.draw_blend, "copy wrote M or used source blending");
            auto completed = p.finish(S_OK);
            check(completed.image == LinearEmissionImage::Linear, "fused completion");
            std::swap(a, *p.owning_candidate());
            check(p.acknowledge_exchange(true) == S_OK && p.coverage_valid(), "fused ownership/coverage");
        }
        a->Release();
        p.detach();
        d.no_leaks();
    }
}

void composition_policies() {
    // Both policies share one pool/M clear, and policy switches are bracket-local.
    for (unsigned mode = 0; mode < 5; ++mode) {
        ++scenarios;
        Device d;
        if (mode != 0)
            d.caps.PrimitiveMiscCaps |= D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_SEPARATEALPHABLEND |
                                        D3DPMISCCAPS_INDEPENDENTWRITEMASKS;
        auto* a = d.scene();
        a->AddRef();
        LinearEmissionPass p;
        if (mode == 1) d.fault_output(106, 3, false); // Fade creation fails; additive survives.
        const HRESULT attached = p.attach(&d, d.slots, d.caps, D3DFMT_A8R8G8B8, D3DFMT_D24S8, 3);
        check(p.caps().supported_policies == (mode == 0 ? 1u : 3u), "immutable policy qualification");
        check(p.caps().available_policies == (mode <= 1 ? 1u : 3u), "created policy inventory");
        check(attached == (mode == 1 ? E_FAIL : S_OK), "policy creation first HRESULT");
        if (mode <= 1) {
            a->Release();
            p.detach();
            d.no_leaks();
            continue;
        }
        check(p.ensure_targets(17, 11) == S_OK && p.references() == 9 && p.allocations() == 4,
              "one mixed-policy B/E/C/M pool");
        check(p.begin_frame(4).ready, "mixed frame clear");
        const auto clears = d.clears;
        auto* old_vs = d.make<IDirect3DVertexShader9>();
        Device::bind(d.vs, old_vs);
        auto* fade_vs = d.make<IDirect3DVertexShader9>();
        auto* ps = d.make<IDirect3DPixelShader9>();
        for (unsigned which = 0; which < 3; ++which) {
            const bool fade = which == 1;
            d.rs[D3DRS_SRCBLEND] = fade ? D3DBLEND_SRCALPHA : D3DBLEND_ONE;
            d.rs[D3DRS_DESTBLEND] = fade ? D3DBLEND_INVSRCALPHA : D3DBLEND_ONE;
            d.rs[D3DRS_COLORWRITEENABLE] = fade ? 7 : 15;
            d.rs[D3DRS_SRCBLENDALPHA] = 17;
            d.rs[D3DRS_DESTBLENDALPHA] = 23;
            d.rs[D3DRS_BLENDOPALPHA] = 31;
            LinearEmissionBoundary boundary{a, ps, 4, true};
            boundary.augmented_vertex = fade_vs;
            boundary.policy = fade ? LinearCompositionPolicy::DistanceFade : LinearCompositionPolicy::AdditiveEmission;
            if (mode == 3 && fade) d.fail_after_vs = fade_vs;
            auto prepared = p.prepare(boundary);
            if (mode == 3 && fade) {
                check(!prepared.ready && prepared.operation == E_FAIL && prepared.state_preserved,
                      "partial VS failure rollback");
                check(d.vs == old_vs && d.rt[0] == a && !d.rt[1] && !d.rt[2], "partial VS restored bindings");
                check(p.coverage_valid(), "clean refusal preserves earlier M bytes");
            } else {
                check(prepared.ready, "policy bracket ready");
                check(d.vs == (fade ? fade_vs : old_vs), "policy-specific augmented VS binding");
                if (fade)
                    check(d.rs[D3DRS_SEPARATEALPHABLENDENABLE] && d.rs[D3DRS_SRCBLENDALPHA] == D3DBLEND_ONE &&
                              d.rs[D3DRS_DESTBLENDALPHA] == D3DBLEND_INVSRCALPHA &&
                              d.rs[D3DRS_BLENDOPALPHA] == D3DBLENDOP_ADD && d.rs[D3DRS_COLORWRITEENABLE2] == 7,
                          "source-over MRT alpha/coverage contract");
                if (mode == 4 && fade) p.inject(LinearEmissionPassFault::Composite);
                auto done = p.finish(S_OK);
                check(done.image == (mode == 4 && fade ? LinearEmissionImage::Native : LinearEmissionImage::Linear),
                      "policy image/native-B recovery");
                check(p.owning_candidate() != nullptr, "policy owning candidate");
                std::swap(a, *p.owning_candidate());
                check(p.acknowledge_exchange(true) == S_OK && p.coverage_valid(), "mixed coverage/ownership ack");
                check(d.vs == old_vs, "original VS restored after policy bracket");
            }
            check(d.rs[D3DRS_SRCBLENDALPHA] == 17 && d.rs[D3DRS_DESTBLENDALPHA] == 23 &&
                      d.rs[D3DRS_BLENDOPALPHA] == 31 && !d.rs[D3DRS_SEPARATEALPHABLENDENABLE],
                  "separate-alpha state restored across policies");
            check(d.clears == clears && p.allocations() == 4, "policy switch recleared M or reallocated pool");
        }
        a->Release();
        p.before_reset();
        check(p.references() == 5, "mixed programs survive Reset with pool retired");
        p.detach();
        d.no_leaks();
    }
}

} // namespace
// Policy 8 (packed screen): capability gate, plane pool, bracket bindings,
// and the stage inventory of a policy-4 bracket unchanged by its availability.
void packed_policy() {
    constexpr DWORD packed_misc = D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_SEPARATEALPHABLEND |
                                  D3DPMISCCAPS_INDEPENDENTWRITEMASKS;
    auto fade_bracket = [](LinearEmissionPass& p, Device& d, IDirect3DSurface9* a, unsigned& gets, unsigned& sets) {
        d.rs[D3DRS_SRCBLEND] = D3DBLEND_SRCALPHA;
        d.rs[D3DRS_DESTBLEND] = D3DBLEND_INVSRCALPHA;
        d.rs[D3DRS_COLORWRITEENABLE] = 7;
        auto* fade_vs = d.make<IDirect3DVertexShader9>();
        auto* ps = d.make<IDirect3DPixelShader9>();
        LinearEmissionBoundary boundary{a, ps, 9, true, fade_vs};
        boundary.policy = LinearCompositionPolicy::DistanceFadeInPlace;
        boundary.region = RECT{2, 2, 9, 7};
        boundary.region_known = true;
        d.stage_gets = d.stage_sets = 0;
        check(p.prepare(boundary).ready, "policy-4 bracket ready");
        check(p.finish(S_OK).image == LinearEmissionImage::Linear, "policy-4 bracket linear");
        gets = d.stage_gets;
        sets = d.stage_sets;
    };
    unsigned baseline_gets = 0, baseline_sets = 0;
    for (unsigned mode : {1u, 0u, 2u, 3u}) { // the three-target baseline first
        ++scenarios;
        Device d;
        d.caps.PrimitiveMiscCaps |= packed_misc;
        d.caps.RasterCaps = D3DPRASTERCAPS_SCISSORTEST;
        d.caps.SrcBlendCaps = D3DPBLENDCAPS_ONE;
        d.caps.DestBlendCaps = D3DPBLENDCAPS_INVSRCALPHA;
        d.caps.NumSimultaneousRTs = mode == 1 ? 3 : 4;
        if (mode == 2) d.caps.DestBlendCaps = 0;
        if (mode == 3) d.caps.PrimitiveMiscCaps &= ~DWORD(D3DPMISCCAPS_INDEPENDENTWRITEMASKS);
        auto* a = d.scene();
        a->AddRef();
        LinearEmissionPass p;
        if (mode) {
            // Packed-only request: refused at attach with its own reason and no allocation.
            LinearEmissionPass alone;
            check(alone.attach(&d, d.slots, d.caps, D3DFMT_A8R8G8B8, D3DFMT_D24S8, 8) == D3DERR_NOTAVAILABLE &&
                      !alone.caps().enabled && alone.references() == 0 &&
                      std::string(alone.caps().reason) == "packed caps",
                  "packed-only capability refusal");
        }
        // Step E gain: finite 0..16 before attach only; the default is 1.
        check(p.packed_gain() == 1.f && p.configure_packed_gain(2.f) && p.packed_gain() == 2.f,
              "packed gain configured before attach");
        check(!p.configure_packed_gain(-1.f) && !p.configure_packed_gain(17.f) &&
                  !p.configure_packed_gain(std::nanf("")) &&
                  !p.configure_packed_gain(std::numeric_limits<float>::infinity()) && p.packed_gain() == 2.f,
              "packed gain domain refusals keep the configured value");
        check(p.configure_packed_gain(mode == 0 ? 2.f : 1.f), "packed gain reconfigured");
        check(p.attach(&d, d.slots, d.caps, D3DFMT_A8R8G8B8, D3DFMT_D24S8, 15) == S_OK,
              "attach with policy 8 requested");
        check(!p.configure_packed_gain(mode == 0 ? 1.f : 2.f) && p.configure_packed_gain(mode == 0 ? 2.f : 1.f) &&
                  p.packed_gain() == (mode == 0 ? 2.f : 1.f),
              "packed gain frozen after attach; the applied value is accepted again (re-attach)");
        if (mode == 0) {
            // The created composite carries the patched literal: exactly one
            // `def c1` (0x05000051, 0xa00f0001) at the generator's index
            // (gain_literal_index in generate_screen_emission_programs.py) with
            // g and 1 - g in the two following lanes.
            const auto& words = d.created_ps.back();
            unsigned found = 0, at = 0;
            for (unsigned i = 0; i + 5 < words.size(); ++i)
                if (words[i] == 0x05000051u && words[i + 1] == 0xa00f0001u) {
                    ++found;
                    at = i;
                }
            float lanes[2]{};
            if (found == 1) std::memcpy(lanes, &words[at + 2], sizeof lanes);
            check(found == 1 && at == gain_literal_index && lanes[0] == 2.f && lanes[1] == -1.f,
                  "packed composite carries the patched gain literal at the generator's index");
        }
        const unsigned expected = mode == 0 ? 15u : mode == 3 ? 1u : 7u;
        check(p.caps().supported_policies == expected && p.caps().available_policies == expected,
              "policy-8 capability gate");
        check(p.ensure_targets(17, 11) == S_OK, "pool");
        check(p.allocations() == (mode == 0 ? 5u : 4u) && p.references() == (mode == 0   ? 12u
                                                                             : mode == 3 ? 8u
                                                                                         : 9u),
              "five-target pool only with policy 8");
        check(p.begin_frame(9).ready, "frame");
        if (mode == 3) {
            a->Release();
            p.before_reset();
            p.detach();
            d.no_leaks();
            continue;
        }
        unsigned gets = 0, sets = 0;
        fade_bracket(p, d, a, gets, sets);
        if (mode == 1) {
            baseline_gets = gets;
            baseline_sets = sets;
        }
        if (mode == 0)
            check(gets == baseline_gets && sets == baseline_sets,
                  "policy-4 stage inventory changed by policy-8 availability");
        // Packed boundary: the native screen state.
        d.rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
        d.rs[D3DRS_DESTBLEND] = D3DBLEND_INVSRCCOLOR;
        d.rs[D3DRS_COLORWRITEENABLE] = 15;
        auto* ps = d.make<IDirect3DPixelShader9>();
        LinearEmissionBoundary boundary{a, ps, 9, true, nullptr};
        boundary.policy = LinearCompositionPolicy::PackedScreenInPlace;
        boundary.region = RECT{1, 1, 8, 6};
        boundary.region_known = true;
        d.stage_gets = d.stage_sets = 0;
        auto prepared = p.prepare(boundary);
        if (mode != 0) {
            check(!prepared.ready && prepared.operation == D3DERR_NOTAVAILABLE && prepared.saved == S_FALSE &&
                      d.stage_gets == 0 && p.coverage_valid(),
                  "policy-8 refusal before any getter");
        } else {
            check(prepared.ready, "packed bracket ready");
            check(d.rt[0] == p.coverage_target() && d.rt[1] == p.fixture_energy() && d.rt[3] == p.fixture_plane_b() &&
                      d.rt[2] && d.rt[2] != d.rt[1] && d.rt[2] != d.rt[3],
                  "packed source targets: M, P_r = E, P_g, P_b");
            check(d.rs[D3DRS_DESTBLEND] == D3DBLEND_INVSRCALPHA && d.rs[D3DRS_COLORWRITEENABLE] == 9 &&
                      d.rs[D3DRS_COLORWRITEENABLE1] == 5 && d.rs[D3DRS_COLORWRITEENABLE2] == 5 &&
                      d.rs[D3DRS_COLORWRITEENABLE3] == 5 && d.ps == ps && d.vs == nullptr,
                  "packed source blend, red|blue plane masks (step E) and untouched VS");
            check(d.stage_gets > gets, "packed bracket saves five stages");
            auto done = p.finish(S_OK);
            check(done.image == LinearEmissionImage::Linear && !done.candidate_bound && !p.owning_candidate() &&
                      p.coverage_valid(),
                  "packed bracket composed in place");
            check(d.rt[0] == a && !d.rt[1] && !d.rt[2] && !d.rt[3] && d.rs[D3DRS_DESTBLEND] == D3DBLEND_INVSRCCOLOR &&
                      d.rs[D3DRS_COLORWRITEENABLE] == 15 && !d.textures[3] && !d.textures[4],
                  "packed bracket restored the caller state");
        }
        a->Release();
        p.before_reset();
        check(p.references() == (mode == 0 ? 7u : 5u), "programs survive pool retirement");
        p.detach();
        d.no_leaks();
    }
}
int main() {
    packed_policy();
    composition_policies();
    fused_preparation();
    outputs();
    pool_identity();
    identity_faults();
    allocation_rollback();
    reset_outputs();
    clean_refusals();
    recovery();
    reset_brackets();
    detach_after_failed_reset();
    for (bool caps : {false, true})
        for (bool failed : {false, true}) draw_contract(caps, failed, false, false);
    draw_contract(false, false, true, false);
    draw_contract(false, false, true, true);
    std::cout << "linear_emission_pass_host scenarios=" << scenarios << " checks=" << checks << " failures=0\n";
}
