// Detached fixture links the actual production FogPass and packaged field decoder.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../src/renderer/fog_pass.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <limits>
using namespace x3m::renderer;
namespace {
template <class T> struct Com {
    T* p = nullptr;
    ~Com() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T* operator->() const { return p; }
    Com() = default;
    Com(const Com&) = delete;
};
void check(HRESULT hr, const char* name) {
    if (FAILED(hr)) {
        std::printf("FAIL api=%s hr=%08lx\n", name, (unsigned long)hr);
        throw std::runtime_error(name);
    }
}
unsigned checks = 0;
void require(bool value, const char* name) {
    ++checks;
    if (!value) throw std::runtime_error(name);
    std::printf("CHECK %s PASS\n", name);
}
template <class T> std::vector<T> read(const std::string& name) {
    std::ifstream in(name, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("open " + name);
    auto n = in.tellg();
    if (n <= 0 || n % sizeof(T)) throw std::runtime_error("size " + name);
    std::vector<T> out(static_cast<size_t>(n) / sizeof(T));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(out.data()), n);
    if (!in) throw std::runtime_error("read " + name);
    return out;
}
void upload(IDirect3DDevice9* d, UINT w, UINT h, D3DFORMAT format, UINT stride, const void* bytes,
            IDirect3DTexture9** out, bool rt) {
    Com<IDirect3DTexture9> sys;
    check(d->CreateTexture(w, h, 1, 0, format, D3DPOOL_SYSTEMMEM, &sys.p, nullptr), "upload texture");
    D3DLOCKED_RECT lock{};
    check(sys->LockRect(0, &lock, nullptr, 0), "upload lock");
    for (UINT y = 0; y < h; ++y)
        std::memcpy(static_cast<char*>(lock.pBits) + y * lock.Pitch, static_cast<const char*>(bytes) + y * w * stride,
                    w * stride);
    check(sys->UnlockRect(0), "upload unlock");
    check(d->CreateTexture(w, h, 1, rt ? D3DUSAGE_RENDERTARGET : 0, format, D3DPOOL_DEFAULT, out, nullptr),
          "input texture");
    check(d->UpdateTexture(sys.p, *out), "update input");
}
std::vector<std::uint16_t> readback_words(IDirect3DDevice9* d, IDirect3DSurface9* rt) {
    D3DSURFACE_DESC desc{};
    check(rt->GetDesc(&desc), "readback desc");
    Com<IDirect3DSurface9> sys;
    check(d->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys.p, nullptr),
          "readback surface");
    check(d->GetRenderTargetData(rt, sys.p), "readback");
    D3DLOCKED_RECT lock{};
    check(sys->LockRect(&lock, nullptr, D3DLOCK_READONLY), "readback lock");
    std::vector<std::uint16_t> out(size_t(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(out.data() + size_t(y) * desc.Width * 4, static_cast<char*>(lock.pBits) + y * lock.Pitch,
                    desc.Width * 8);
    check(sys->UnlockRect(), "readback unlock");
    return out;
}
void write_words(const std::vector<std::uint16_t>& words, const std::string& file) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(words.data()), words.size() * 2);
    out.close();
    if (!out) throw std::runtime_error("write " + file);
}
void readback(IDirect3DDevice9* d, IDirect3DSurface9* rt, const std::string& file) {
    write_words(readback_words(d, rt), file);
}

struct Window {
    HWND handle = nullptr;
    ~Window() {
        if (handle) DestroyWindow(handle);
    }
};
}
#include "fog_spatial_state_inc.h"
namespace {
constexpr DWORD march_words[] = {
#include "../../src/renderer/fog_march_program_inc.h"
};
using namespace fog_spatial_state;
template <class F> HRESULT cpu_method(const char* name, F&& operation) {
    x3m::CpuState original, before, after;
    original.capture();
    const unsigned short control = 0x077f;
    const unsigned mxcsr = 0x3fa0;
    asm volatile("fninit\n\tfld1\n\tfldpi\n\tfldcw %0\n\tldmxcsr %1" ::"m"(control), "m"(mxcsr) : "memory");
    SetLastError(0x92345678);
    before.capture();
    before.restore();
    const HRESULT hr = operation();
    after.capture();
    original.restore();
    require(before.error == after.error && before.mxcsr == after.mxcsr &&
                !std::memcmp(before.x87, after.x87, sizeof before.x87),
            name);
    return hr;
}
template <class F> HRESULT prepare_time(const char* name, F&& operation) {
    LARGE_INTEGER frequency{}, start{}, end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const HRESULT hr = operation();
    QueryPerformanceCounter(&end);
    std::printf("PREPARE_TIME stage=%s cpu_us=%.3f hr=%08lx\n", name,
                double(end.QuadPart - start.QuadPart) * 1e6 / double(frequency.QuadPart), (unsigned long)hr);
    return hr;
}

void qualify_state(IDirect3D9* api, IDirect3DDevice9* d, D3DFORMAT format, const D3DCAPS9& caps,
                   const D3DPRESENT_PARAMETERS& pp, const std::string& cases) {
    Inputs input(cases);
    Hooks hooks(d, api);
    FogPass pass;
    check(cpu_method("cpu_attach", [&] { return pass.attach(d, hooks.methods, caps, format); }), "production attach");
    check(prepare_time("first_resource_decode_upload",
                       [&] {
                           return cpu_method("cpu_prepare_field", [&] {
                               return pass.prepare_field(GetModuleHandleA(nullptr), input.profile);
                           });
                       }),
          "production field");
    check(cpu_method("cpu_prepare_targets", [&] { return pass.prepare_targets(input.w, input.h); }),
          "production targets");
    require(pass.fixture_cpu_bytes() == 17846400 && pass.references() == 10, "one_cpu_atlas_and_ten_refs");
    std::printf(
        "ACTUAL_CAPS texture=%08lx filters=%08lx devcaps2=%08lx streams=%lu mrt=%lu maxw=%lu maxh=%lu slots=%lu\n",
        (unsigned long)caps.TextureCaps, (unsigned long)caps.TextureFilterCaps, (unsigned long)caps.DevCaps2,
        (unsigned long)caps.MaxStreams, (unsigned long)caps.NumSimultaneousRTs, (unsigned long)caps.MaxTextureWidth,
        (unsigned long)caps.MaxTextureHeight, (unsigned long)caps.MaxPixelShader30InstructionSlots);
    struct CapCase {
        Hooks::CapsMode mode;
        const char* label;
    };
    for (const auto test : {CapCase{Hooks::Ps2, "ps2"}, CapCase{Hooks::Vs2, "vs2"}, CapCase{Hooks::Slots, "slots"},
                            CapCase{Hooks::ConditionalNpot, "conditional_npot"}, CapCase{Hooks::Square, "square"},
                            CapCase{Hooks::SmallAtlas, "dimensions"}, CapCase{Hooks::NoMinLinear, "min_linear"},
                            CapCase{Hooks::NoMagLinear, "mag_linear"}, CapCase{Hooks::NoStretch, "stretch"},
                            CapCase{Hooks::TooManyStreams, "streams"}}) {
        hooks.caps_mode = test.mode;
        D3DCAPS9 altered{};
        check(d->GetDeviceCaps(&altered), "altered caps");
        hooks.clear();
        FogPass candidate;
        require(candidate.attach(d, hooks.methods, altered, format) == D3DERR_NOTAVAILABLE &&
                    candidate.references() == 0 && hooks.writes() == 0,
                (std::string("caps_") + test.label).c_str());
    }
    hooks.caps_mode = Hooks::Real;
    for (const auto test : {std::pair<D3DFORMAT, DWORD>{D3DFMT_A32B32G32R32F, 0},
                            {D3DFMT_A16B16G16R16F, D3DUSAGE_RENDERTARGET},
                            {D3DFMT_A16B16G16R16F, D3DUSAGE_QUERY_FILTER}}) {
        hooks.refuse_format = test.first;
        hooks.refuse_usage = test.second;
        hooks.clear();
        FogPass candidate;
        require(candidate.attach(d, hooks.methods, caps, format) == D3DERR_NOTAVAILABLE &&
                    candidate.references() == 0 && hooks.writes() == 0,
                ("format_" + std::to_string(test.first) + "_" + std::to_string(test.second)).c_str());
    }
    hooks.refuse_format = D3DFMT_UNKNOWN;
    for (const auto target : {Fault{91, 1, E_OUTOFMEMORY}, Fault{86, 1, E_OUTOFMEMORY}, Fault{106, 1, E_OUTOFMEMORY},
                              Fault{106, 2, E_OUTOFMEMORY}}) {
        hooks.clear();
        hooks.fault = target;
        FogPass candidate;
        require(candidate.attach(d, hooks.methods, caps, format) == target.error && candidate.references() == 0,
                ("attach_partial_" + std::to_string(target.slot) + "_" + std::to_string(target.at)).c_str());
        hooks.clear();
        check(candidate.attach(d, hooks.methods, caps, format), "attach retry");
    }
    for (const auto target : {Fault{23, 1, E_OUTOFMEMORY}, Fault{23, 2, E_OUTOFMEMORY}, Fault{31, 1, E_FAIL}}) {
        hooks.clear();
        FogPass candidate;
        check(candidate.attach(d, hooks.methods, caps, format), "field allocation attach");
        hooks.clear();
        hooks.fault = target;
        require(candidate.prepare_field(GetModuleHandleA(nullptr), input.profile) == target.error &&
                    candidate.references() == 4 && candidate.field_profile() == fog_field::Profile::None &&
                    candidate.fixture_cpu_bytes() == 17846400,
                ("field_partial_" + std::to_string(target.slot) + "_" + std::to_string(target.at)).c_str());
        hooks.clear();
        check(candidate.prepare_field(nullptr, input.profile), "cached field upload retry");
        require(candidate.references() == 5 && hooks.calls[31] == 1,
                ("field_retry_" + std::to_string(target.slot) + "_" + std::to_string(target.at)).c_str());
    }
    for (const auto target : {Fault{23, 1, E_OUTOFMEMORY}, Fault{23, 2, E_OUTOFMEMORY}, Fault{59, 1, E_FAIL}}) {
        hooks.clear();
        FogPass candidate;
        check(candidate.attach(d, hooks.methods, caps, format), "target allocation attach");
        hooks.clear();
        hooks.fault = target;
        require(candidate.prepare_targets(input.w, input.h) == target.error && candidate.references() == 4,
                ("targets_partial_" + std::to_string(target.slot) + "_" + std::to_string(target.at)).c_str());
        hooks.clear();
        check(candidate.prepare_targets(input.w, input.h), "targets retry");
        require(candidate.references() == 9,
                ("targets_retry_" + std::to_string(target.slot) + "_" + std::to_string(target.at)).c_str());
    }
    {
        FogPass candidate;
        check(candidate.attach(d, hooks.methods, caps, format), "resource refusal attach");
        hooks.clear();
        require(FAILED(candidate.prepare_field(GetModuleHandleA("kernel32.dll"), input.profile)) &&
                    candidate.references() == 4 && candidate.fixture_cpu_bytes() == 0 &&
                    candidate.field_profile() == fog_field::Profile::None && hooks.writes() == 0,
                "resource_lookup_failure_disarmed");
    }
    hooks.clear();

    auto scene_owner = std::make_unique<Scene>(d, input, caps,
                                               std::vector<DWORD>(std::begin(march_words), std::end(march_words)));
    auto& scene = *scene_owner;
    auto frame = scene.frame(pass);
    FogResult result;
    scene.hostile();
    const Protected before(d, scene);
    const auto alloc = pass.allocations(), owned = pass.references();
    auto run = [&](bool open) {
        scene.refill();
        scene.hostile();
        frame = scene.frame(pass);
        frame.caller_scene_open = open;
        if (open) check(d->BeginScene(), "caller BeginScene");
        hooks.clear();
        SetLastError(0x13579);
        const HRESULT hr = pass.execute(frame, &result);
        const DWORD error = GetLastError();
        require(SUCCEEDED(hr) && result.applied && result.scene_known && result.scene_open == open &&
                    result.caller_state_restored && !result.route_poisoned && error == 0x13579,
                open ? "open_success_scene_contract" : "closed_success_scene_contract");
        require(hooks.calls[41] == 1 && hooks.calls[42] == 1 && hooks.calls[34] == 1 && hooks.calls[83] == 2 &&
                    hooks.copy_inside_scene == 0 && hooks.draw_outside_scene == 0,
                open ? "open_scene_copy_draw_counts" : "closed_scene_copy_draw_counts");
        if (open) check(d->EndScene(), "caller EndScene");
        require(before.same(d, scene),
                open ? "open_hostile_all_state_aux_depth" : "closed_hostile_all_state_aux_depth");
    };
    run(false);
    const auto baseline = readback_words(d, scene.s0.p);
    require(baseline != input.scene, "composite_actual_write");
    run(true);
    require(readback_words(d, scene.s0.p) == baseline, "open_closed_identical_pixels");
    const auto refs = caller_references(d, scene);
    for (unsigned i = 0; i < 16; ++i) {
        scene.refill();
        scene.hostile();
        frame = scene.frame(pass);
        hooks.clear();
        check(pass.execute(frame, &result), "warm execute");
    }
    require(pass.allocations() == alloc && pass.references() == owned && caller_references(d, scene) == refs &&
                hooks.calls[23] == 0 && hooks.calls[31] == 0 && hooks.calls[59] == 0,
            "warm_allocation_upload_refs_stable");
    // Actual R32F comparisons against the unchanged authored volume. Synthetic
    // maps isolate lighting from density; every call also checks all saved state.
    {
        Com<IDirect3DTexture9> dark, lit;
        std::vector<float> zeros(64 * 64, 0.f), ones(64 * 64, 1.f);
        upload(d, 64, 64, D3DFMT_R32F, 4, zeros.data(), &dark.p, false);
        upload(d, 64, 64, D3DFMT_R32F, 4, ones.data(), &lit.p, false);
        const auto st_baseline = readback_words(d, pass.fixture_st());
        auto shadow_frame = [&](unsigned slot, IDirect3DTexture9* map) {
            auto f = scene.frame(pass);
            f.frame = 73;
            f.count = 3;
            auto& k = f.cascades[slot];
            k.map = map;
            k.frame = 73;
            k.valid = true;
            k.bias = .00001f;
            k.rows[0] = k.rows[5] = k.rows[10] = .00001f;
            k.rows[11] = .5f;
            return f;
        };
        for (unsigned slot = 0; slot < 3; ++slot) {
            scene.refill();
            scene.hostile();
            frame = shadow_frame(slot, dark.p);
            hooks.clear();
            check(cpu_method(("cpu_shadow_execute_" + std::to_string(slot)).c_str(),
                             [&] { return pass.execute(frame, &result); }),
                  "shadow execute");
            auto st = readback_words(d, pass.fixture_st());
            bool same_t = true, zero_s = true, had_s = false, empty = true;
            for (size_t i = 0; i < st.size(); i += 4) {
                same_t &= st[i + 3] == st_baseline[i + 3];
                for (unsigned j = 0; j < 3; ++j) {
                    zero_s &= st[i + j] == 0;
                    had_s |= st_baseline[i + j] != 0;
                }
                if (st_baseline[i] == 0 && st_baseline[i + 1] == 0 && st_baseline[i + 2] == 0 &&
                    st_baseline[i + 3] == 0x3c00)
                    empty &= st[i] == 0 && st[i + 1] == 0 && st[i + 2] == 0 && st[i + 3] == 0x3c00;
            }
            require(result.cascades_bound == 1 && same_t && zero_s && had_s && empty && before.same(d, scene) &&
                        pass.references() == owned && hooks.calls[23] == 0 && hooks.calls[31] == 0,
                    ("shafts_dark_map_slot_" + std::to_string(slot)).c_str());
            scene.refill();
            scene.hostile();
            frame = shadow_frame(slot, lit.p);
            check(pass.execute(frame, &result), "lit shadow map");
            require(readback_words(d, pass.fixture_st()) == st_baseline && readback_words(d, scene.s0.p) == baseline &&
                        before.same(d, scene),
                    ("shafts_lit_map_identity_slot_" + std::to_string(slot)).c_str());
        }
        for (unsigned invalid = 0; invalid < 6; ++invalid) {
            scene.refill();
            scene.hostile();
            frame = shadow_frame(0, dark.p);
            auto& k = frame.cascades[0];
            switch (invalid) {
            case 0: --k.frame; break;
            case 1: k.valid = false; break;
            case 2: k.map = nullptr; break;
            case 3: k.rows[0] = std::numeric_limits<float>::quiet_NaN(); break;
            case 4: k.bias = -1; break;
            case 5: k.map = scene.spare.p; break;
            }
            check(pass.execute(frame, &result), "absent shadow fallback");
            require(result.cascades_bound == 0 && readback_words(d, pass.fixture_st()) == st_baseline &&
                        readback_words(d, scene.s0.p) == baseline && before.same(d, scene),
                    ("shafts_unavailable_identity_" + std::to_string(invalid)).c_str());
        }
        Com<IDirect3DTexture9> patterned;
        std::vector<float> stripes(64 * 64);
        for (unsigned y = 0; y < 64; ++y)
            for (unsigned x = 0; x < 64; ++x) stripes[y * 64 + x] = float(x & 1);
        upload(d, 64, 64, D3DFMT_R32F, 4, stripes.data(), &patterned.p, false);
        auto half_value = [](unsigned h) {
            const unsigned e = (h >> 10) & 31, m = h & 1023;
            return std::ldexp(float(e ? 1024 + m : m), e ? int(e) - 25 : -24);
        };
        for (unsigned mode = 0; mode < 3; ++mode) {
            scene.refill();
            scene.hostile();
            frame = shadow_frame(0, mode ? dark.p : patterned.p);
            auto& nearer_map = frame.cascades[0];
            nearer_map.rows[0] = nearer_map.rows[5] = 1e-12f;
            nearer_map.rows[3] = mode ? .9f : 1.f / 64.f;
            if (mode) {
                frame.cascades[1] = shadow_frame(1, mode == 1 ? lit.p : dark.p).cascades[1];
            }
            check(pass.execute(frame, &result), "PCF and cascade blend");
            const auto actual = readback_words(d, pass.fixture_st());
            bool matched = true, nonempty = false;
            for (size_t i = 0; i < actual.size(); i += 4) {
                matched &= actual[i + 3] == st_baseline[i + 3];
                for (unsigned j = 0; j < 3; ++j) {
                    const float source = half_value(st_baseline[i + j]), value = half_value(actual[i + j]);
                    nonempty |= source > 1e-5f;
                    // Includes the baseline half quantization before halving and the
                    // output quantization afterwards, plus float32 band roundoff.
                    matched &= std::abs(value - (mode == 2 ? 0.f : .5f * source)) <= std::max(1.2e-7f, source * .0015f);
                }
            }
            const char* names[] = {"shafts_fractional_pcf", "shafts_blend_to_lit_coarser",
                                   "shafts_blend_to_dark_coarser"};
            require(matched && nonempty && result.cascades_bound == (mode ? 2u : 1u) && before.same(d, scene),
                    names[mode]);
        }
        // Maps must not survive in the stateblock after returning to hostile
        // bindings. Releasing these caller-owned references needs no pass Reset.
        require(dark->AddRef() == 2, "shafts_borrowed_no_retained_reference");
        dark->Release();
    }
    // Positive caller facts are independently necessary, and refusal never writes.
    for (unsigned i = 0; i < 7; ++i) {
        scene.refill();
        scene.hostile();
        frame = scene.frame(pass);
        switch (i) {
        case 0: frame.main_target = false; break;
        case 1: frame.linear_depth_current = false; break;
        case 2: frame.caller_scene_known = false; break;
        case 3: frame.caller_queries_idle = false; break;
        case 4: frame.caller_stateblock_recording = true; break;
        case 5: ++frame.field_generation; break;
        case 6: frame.profile = fog_field::Profile::None; break;
        }
        hooks.clear();
        const HRESULT hr = pass.execute(frame, &result);
        require(FAILED(hr) && hooks.writes() == 0 && readback_words(d, scene.s0.p) == input.scene &&
                    before.same(d, scene),
                ("refusal_" + std::to_string(i)).c_str());
    }
    {
        scene.refill();
        scene.hostile();
        frame = scene.frame(pass);
        Com<IDirect3DQuery9> query;
        if (FAILED(d->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query.p))) throw Gap("occlusion_query");
        check(d->BeginScene(), "query scene begin");
        check(query->Issue(D3DISSUE_BEGIN), "query begin");
        frame.caller_scene_open = true;
        frame.caller_queries_idle = false;
        hooks.clear();
        const HRESULT hr = pass.execute(frame, &result);
        const bool zero = hooks.writes() == 0;
        check(query->Issue(D3DISSUE_END), "query end");
        check(d->EndScene(), "query scene end");
        require(FAILED(hr) && zero && before.same(d, scene) && readback_words(d, scene.s0.p) == input.scene,
                "actual_query_zero_writes");
        frame = scene.frame(pass);
        check(d->BeginStateBlock(), "record begin");
        frame.caller_stateblock_recording = true;
        hooks.clear();
        const HRESULT recorded = pass.execute(frame, &result);
        const bool untouched = hooks.writes() == 0;
        Com<IDirect3DStateBlock9> block;
        check(d->EndStateBlock(&block.p), "record end");
        require(FAILED(recorded) && untouched && before.same(d, scene), "actual_recording_zero_writes");
        Com<IDirect3DSurface9> msaa;
        HRESULT ms = d->CreateRenderTarget(input.w, input.h, D3DFMT_A16B16G16R16F, D3DMULTISAMPLE_2_SAMPLES, 0, FALSE,
                                           &msaa.p, nullptr);
        if (FAILED(ms)) throw Gap("fp16_msaa2_surface");
        frame = scene.frame(pass);
        frame.target = msaa.p;
        hooks.clear();
        require(FAILED(pass.execute(frame, &result)) && hooks.writes() == 0, "actual_msaa_refusal");
        frame = scene.frame(pass);
        frame.depth_share = scene.rt1.p;
        hooks.clear();
        require(FAILED(pass.execute(frame, &result)) && hooks.writes() == 0, "wrong_rt2_format_refusal");
    }
    frame = scene.frame(pass);
    frame.params.density_scale = 0;
    hooks.clear();
    require(pass.execute(frame, &result) == S_FALSE && result.device_calls == 0 && hooks.writes() == 0,
            "sigma_zero_zero_calls");
    struct Case {
        const char* name;
        bool open;
        Fault a, b;
        bool write, poison, known, scene_open;
        unsigned draws;
    };
    const Case faults[] = {
        {"close_failure", true, {42, 1, E_FAIL, false}, {}, false, true, false, true, 0},
        {"copy_failure_closed", false, {34, 1, E_FAIL, false}, {}, false, false, true, false, 0},
        {"copy_failure_open_recovered", true, {34, 1, E_FAIL, false}, {}, false, false, true, true, 0},
        {"reopen_once_recovered", true, {41, 1, E_FAIL, false}, {}, false, false, true, true, 0},
        {"reopen_twice_poison", true, {41, 1, E_FAIL, false}, {41, 2, E_FAIL, false}, false, true, false, false, 0},
        {"march_failure", false, {83, 1, E_FAIL, false}, {}, false, false, true, false, 1},
        {"composite_before_write", true, {83, 2, E_FAIL, false}, {}, true, false, true, true, 2},
        {"composite_after_write", true, {83, 2, E_FAIL, true}, {}, true, false, true, true, 2},
        {"end_failure", false, {42, 1, E_FAIL, false}, {}, true, true, false, true, 2},
    };
    for (const auto& fault : faults) {
        scene.refill();
        scene.hostile();
        frame = scene.frame(pass);
        frame.caller_scene_open = fault.open;
        if (fault.open) check(d->BeginScene(), "fault caller BeginScene");
        hooks.clear();
        hooks.fault = fault.a;
        hooks.fault2 = fault.b;
        const HRESULT hr = pass.execute(frame, &result);
        const auto draws = hooks.calls[83];
        std::printf(
            "SCENE_FAULT name=%s hr=%08lx restore=%08lx known=%u open=%u poison=%u write=%u draws=%u recover=%08lx\n",
            fault.name, (unsigned long)hr, (unsigned long)result.restore, unsigned(result.scene_known),
            unsigned(result.scene_open), unsigned(result.route_poisoned), unsigned(result.scene_write_started), draws,
            (unsigned long)result.scene_recovery);
        require(FAILED(hr) && result.scene_known == fault.known && result.route_poisoned == fault.poison &&
                    result.scene_write_started == fault.write && draws == fault.draws &&
                    (!fault.known || result.scene_open == fault.scene_open),
                fault.name);
        hooks.clear();
        // Injected pre-forward failures leave known fixture-native scene state;
        // this cleanup knowledge is deliberately not available to production.
        if (fault.open && std::string(fault.name) != "reopen_twice_poison")
            check(d->EndScene(), "fault actual open cleanup");
        if (!fault.open && std::string(fault.name) == "end_failure") check(d->EndScene(), "fault actual end cleanup");
        if (std::string(fault.name) == "composite_after_write")
            require(readback_words(d, scene.s0.p) != input.scene, "failed_composite_no_false_rollback");
        else if (!fault.write)
            require(readback_words(d, scene.s0.p) == input.scene,
                    (std::string(fault.name) + "_zero_scene_writes").c_str());
        require(before.same(d, scene), (std::string(fault.name) + "_bindings_aux_restored").c_str());
    }
    for (bool fail : {false, true}) {
        scene.refill();
        scene.hostile();
        frame = scene.frame(pass);
        frame.caller_scene_open = true;
        check(d->BeginScene(), "cpu caller begin");
        hooks.clear();
        if (fail) hooks.fault = {83, 1, E_FAIL};
        const HRESULT hr = cpu_method(fail ? "cpu_execute_fault" : "cpu_execute_success",
                                      [&] { return pass.execute(frame, &result); });
        hooks.clear();
        check(d->EndScene(), "cpu caller end");
        require(hr == (fail ? E_FAIL : S_OK), fail ? "cpu_fault_result" : "cpu_success_result");
    }
    for (unsigned slot : {101u, 103u}) {
        scene.refill();
        scene.hostile();
        frame = scene.frame(pass);
        const auto references = caller_references(d, scene);
        hooks.clear();
        hooks.fault = {slot, caps.MaxStreams, E_FAIL};
        const HRESULT hr = pass.execute(frame, &result);
        hooks.clear();
        require(hr == E_FAIL && !result.scene_write_started && result.caller_state_restored && !result.route_poisoned &&
                    before.same(d, scene) && caller_references(d, scene) == references,
                ("capture_partial_" + std::to_string(slot)).c_str());
    }
    for (unsigned slot : {100u, 102u}) {
        scene.refill();
        scene.hostile();
        frame = scene.frame(pass);
        const auto references = caller_references(d, scene);
        hooks.clear();
        hooks.fault = {slot, 2 * caps.MaxStreams, E_FAIL};
        const HRESULT hr = pass.execute(frame, &result);
        hooks.clear();
        require(hr == E_FAIL && result.operation == S_OK && result.restore == E_FAIL && result.route_poisoned &&
                    !result.caller_state_restored && result.scene_write_started && before.aux_same(d, scene) &&
                    caller_references(d, scene) == references,
                ("restore_failure_" + std::to_string(slot)).c_str());
        std::printf("RESTORE_FAULT slot=%u caller_state_identical=%u rollback_claimed=0\n", slot,
                    unsigned(before.state == Snapshot(d, caps)));
    }
    struct Lost {
        const char* name;
        bool open;
        Fault first, second;
        bool actual_open;
    };
    const Lost losses[] = {
        {"capture", false, {48, 1, D3DERR_DEVICELOST}, {}, false},
        {"close", true, {42, 1, D3DERR_DEVICELOST}, {}, true},
        {"copy", true, {34, 1, D3DERR_DEVICELOST}, {}, false},
        {"reopen", true, {41, 1, D3DERR_DEVICELOST}, {}, false},
        {"recovery_after_error", true, {41, 1, E_FAIL}, {41, 2, D3DERR_DEVICELOST}, false},
        {"march", true, {83, 1, D3DERR_DEVICELOST}, {}, true},
        {"composite_after_write", true, {83, 2, D3DERR_DEVICELOST, true}, {}, true},
        {"end_after_error", false, {83, 1, E_FAIL}, {42, 1, D3DERR_DEVICELOST}, true},
        {"restore_after_error", false, {47, 3, E_FAIL}, {75, 1, D3DERR_DEVICENOTRESET}, false},
        {"stream_restore", false, {100, 2 * caps.MaxStreams, D3DERR_DEVICELOST}, {}, false},
    };
    for (const auto& test : losses) {
        scene.refill();
        scene.hostile();
        frame = scene.frame(pass);
        frame.caller_scene_open = test.open;
        if (test.open) check(d->BeginScene(), "loss caller begin");
        hooks.clear();
        hooks.fault = test.first;
        hooks.fault2 = test.second;
        const HRESULT hr = pass.execute(frame, &result);
        const bool stopped = hooks.observed_lost && hooks.calls_after_lost == 0;
        hooks.clear();
        FogResult pending;
        require(FAILED(hr) && stopped && pass.reset_pending() && result.route_poisoned && !result.scene_known &&
                    !result.caller_state_restored && pass.execute(frame, &pending) == D3DERR_DEVICENOTRESET &&
                    hooks.writes() == 0,
                (std::string("injected_loss_") + test.name).c_str());
        hooks.clear();
        if (test.actual_open) check(d->EndScene(), "healthy simulated-loss cleanup");
        // The device was never actually lost. Clear only the renderer latch;
        // actual failed Reset behavior is exercised below independently.
        pass.after_reset(S_OK);
    }
    std::printf("LOSS_EVIDENCE injected_only=1 native_loss_observed=0\n");
    // Resolution changes reuse the only atlas; clear disarms it without sampling.
    const auto generation = pass.field_generation();
    hooks.clear();
    check(prepare_time("resize_targets_only", [&] { return pass.prepare_targets(80, 60); }), "resize targets");
    require(hooks.calls[23] == 2 && hooks.calls[31] == 0 && pass.field_generation() == generation &&
                pass.fixture_cpu_bytes() == 17846400,
            "resize_preserves_field");
    check(pass.prepare_targets(input.w, input.h), "restore target size");
    pass.prepare_field(nullptr, fog_field::Profile::None);
    frame = scene.frame(pass);
    hooks.clear();
    require(FAILED(pass.execute(frame, &result)) && hooks.writes() == 0 && !pass.resources_ready(input.w, input.h),
            "clear_disarms");
    check(prepare_time("reactivate_cached_field", [&] { return pass.prepare_field(nullptr, input.profile); }),
          "reactivate cached field");
    require(pass.field_generation() > generation && hooks.calls[23] == 0 && hooks.calls[31] == 0,
            "same_cached_field_no_reupload");
    const auto other = input.profile == fog_field::Profile::Bluewell ? fog_field::Profile::Foggreenoutlands
                                                                     : fog_field::Profile::Bluewell;
    hooks.clear();
    hooks.fault = {23, 2, E_OUTOFMEMORY, false};
    require(FAILED(prepare_time("family_switch_failed_upload",
                                [&] { return pass.prepare_field(GetModuleHandleA(nullptr), other); })) &&
                pass.field_profile() == fog_field::Profile::None && !pass.resources_ready(input.w, input.h),
            "failed_family_transition_disarmed");
    hooks.clear();
    check(prepare_time("family_retry_cached_upload",
                       [&] { return pass.prepare_field(GetModuleHandleA(nullptr), other); }),
          "family retry");
    require(pass.field_profile() == other && pass.fixture_cpu_bytes() == 17846400 && hooks.calls[31] == 1,
            "family_retry_one_buffer_upload");
    scene.refill();
    scene.hostile();
    frame = scene.frame(pass);
    check(pass.execute(frame, &result), "pre-reset family execute");
    const auto reset_baseline = readback_words(d, scene.s0.p), reset_st = readback_words(d, pass.fixture_st());
    Com<IDirect3DSurface9> back;
    check(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back.p), "backbuffer");
    Com<IDirect3DTexture9> blocker;
    check(d->CreateTexture(8, 8, 1, 0, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &blocker.p, nullptr), "Reset blocker");
    cpu_method("cpu_before_reset", [&] {
        pass.before_reset();
        return S_OK;
    });
    require(pass.references() == 4 && pass.fixture_cpu_bytes() == 17846400, "reset_releases_defaults_retains_cpu");
    scene.unbind(back.p);
    scene_owner.reset();
    back.reset();
    D3DPRESENT_PARAMETERS attempt = pp;
    HRESULT reset = d->Reset(&attempt);
    pass.after_reset(reset);
    std::printf("RESET failed_attempt_hr=%08lx\n", (unsigned long)reset);
    require(FAILED(reset), "native_reset_blocker_failure");
    // After failed Reset, only Reset/TestCooperativeLevel/Release may touch D3D.
    hooks.clear();
    require(pass.execute(frame, &result) == D3DERR_DEVICENOTRESET &&
                pass.prepare_field(nullptr, other) == D3DERR_DEVICENOTRESET &&
                pass.prepare_targets(64, 48) == D3DERR_DEVICENOTRESET && hooks.writes() == 0,
            "failed_reset_pending_zero_device_calls");
    blocker.reset();
    attempt = pp;
    reset = d->Reset(&attempt);
    cpu_method("cpu_after_reset", [&] {
        pass.after_reset(reset);
        return S_OK;
    });
    require(SUCCEEDED(reset), "native_reset_retry_success");
    check(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back.p), "reset backbuffer");
    scene_owner = std::make_unique<Scene>(d, input, caps,
                                          std::vector<DWORD>(std::begin(march_words), std::end(march_words)));
    auto& restored = *scene_owner;
    hooks.clear();
    check(prepare_time("reset_cached_reupload", [&] { return pass.prepare_field(nullptr, other); }),
          "reset field upload");
    check(pass.prepare_targets(64, 48), "reset targets");
    require(hooks.calls[31] == 1 && pass.fixture_cpu_bytes() == 17846400, "reset_reupload_cached_bytes");
    restored.refill();
    restored.hostile();
    const Protected reset_before(d, restored);
    check(pass.execute(restored.frame(pass), &result), "reset execute");
    require(result.caller_state_restored && readback_words(d, restored.s0.p) == reset_baseline &&
                readback_words(d, pass.fixture_st()) == reset_st && reset_before.same(d, restored),
            "reset_reupload_identical_pixels_state");
    cpu_method("cpu_detach", [&] {
        pass.detach();
        return S_OK;
    });
    require(pass.references() == 0 && pass.fixture_cpu_bytes() == 0, "detach_releases_all");
    restored.unbind(back.p);
    scene_owner.reset();
    back.reset();
}
} // namespace
#include "fog_family_gpu_cases_inc.h"
int main(int argc, char** argv) {
    // case-list (id, host-mapped input directory, numeric profile), output-dir, [--state]
    if (argc != 3 && argc != 4) return 2;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        WNDCLASSA cls{};
        cls.lpfnWndProc = DefWindowProcA;
        cls.hInstance = GetModuleHandleA(nullptr);
        cls.lpszClassName = "X3FogSpatialProductionFixture";
        RegisterClassA(&cls);
        Window window;
        window.handle = CreateWindowA(cls.lpszClassName, "Detached production fog", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
                                      nullptr, nullptr, cls.hInstance, nullptr);
        if (!window.handle) throw std::runtime_error("window");
        HMODULE library = LoadLibraryA("d3d9.dll");
        if (!library) throw std::runtime_error("d3d9");
        auto addr = GetProcAddress(library, "Direct3DCreate9");
        IDirect3D9*(WINAPI * create)(UINT) = nullptr;
        std::memcpy(&create, &addr, sizeof create);
        if (!create) throw std::runtime_error("Create9 symbol");
        Com<IDirect3D9> api;
        api.p = create(D3D_SDK_VERSION);
        if (!api.p) throw std::runtime_error("Create9");
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window.handle;
        pp.BackBufferWidth = 1280;
        pp.BackBufferHeight = 768;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;
        check(api->CreateDevice(0, D3DDEVTYPE_HAL, window.handle, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p),
              "CreateDevice");
        auto d = device.p;
        D3DDISPLAYMODE mode{};
        check(api->GetAdapterDisplayMode(0, &mode), "display");
        D3DCAPS9 caps{};
        check(d->GetDeviceCaps(&caps), "caps");
        if (argc == 4) {
            if (!std::strcmp(argv[3], "--state"))
                qualify_state(api.p, d, mode.Format, caps, pp, argv[1]);
            else if (!std::strcmp(argv[3], "--families"))
                qualify_families(api.p, d, mode.Format, caps, pp, argv[1], argv[2]);
            else
                return 2;
        } else {
            FogPass pass;
            check(pass.attach(d, *reinterpret_cast<void***>(d), caps, mode.Format), "attach");
            std::printf("CAPS slots=%u texture=%08lx filters=%08lx maxw=%lu maxh=%lu streams=%lu\n",
                        pass.caps().largest_program_slots, (unsigned long)caps.TextureCaps,
                        (unsigned long)caps.TextureFilterCaps, (unsigned long)caps.MaxTextureWidth,
                        (unsigned long)caps.MaxTextureHeight, (unsigned long)caps.MaxStreams);
            std::ifstream list(argv[1]);
            std::string id, dir, profile_text;
            while (std::getline(list, id) && std::getline(list, dir) && std::getline(list, profile_text)) {
                const auto profile = static_cast<fog_field::Profile>(std::stoul(profile_text));
                const auto* info = fog_field::profile_info(profile);
                if (!info) throw std::runtime_error("profile");
                auto c = read<float>(dir + "/constants.f32");
                if (c.size() != 32) throw std::runtime_error("constants size");
                const UINT w = UINT(c[4]), h = UINT(c[5]);
                auto depth = read<float>(dir + "/depth.rgba32f");
                auto scene = read<std::uint16_t>(dir + "/scene.rgba16f");
                if (!w || !h || depth.size() != size_t(w) * h * 4 || scene.size() != depth.size())
                    throw std::runtime_error("input size");
                check(pass.prepare_field(GetModuleHandleA(nullptr), profile), "field");
                check(pass.prepare_targets(w, h), "targets");
                for (unsigned variant = 0; variant < 10; ++variant) {
                    auto adjusted = depth;
                    if (variant == 6)
                        for (size_t i = 0; i < adjusted.size(); i += 4) {
                            adjusted[i] = .5f;
                            adjusted[i + 2] = (i / 4) % 4 == 0
                                                  ? 0.f
                                                  : ((i / 4) % 4 == 1
                                                         ? -1.f
                                                         : ((i / 4) % 4 == 2 ? std::numeric_limits<float>::quiet_NaN()
                                                                             : std::numeric_limits<float>::infinity()));
                        }
                    if (variant >= 7)
                        for (UINT y = 0; y < h; y += 2)
                            for (UINT x = 0; x < w; x += 2) {
                                const size_t i = (size_t(y) * w + x) * 4;
                                adjusted[i] = .5f;
                                adjusted[i + 2] = std::numeric_limits<float>::quiet_NaN();
                            }
                    Com<IDirect3DTexture9> dt, st, shadow_map;
                    Com<IDirect3DSurface9> ss;
                    upload(d, w, h, D3DFMT_A32B32G32R32F, 16, adjusted.data(), &dt.p, false);
                    upload(d, w, h, D3DFMT_A16B16G16R16F, 8, scene.data(), &st.p, true);
                    check(st->GetSurfaceLevel(0, &ss.p), "scene surface");
                    auto f = make_frame(pass, dt.p, ss.p, c);
                    f.params.density_scale = c[11] / info->base_sigma;
                    if (variant == 1) f.caller_scene_open = true;
                    if (variant == 2) f.params.decode_exponent = 1;
                    if (variant == 3) f.params.anisotropy = 0;
                    if (variant == 4) f.params.anisotropy = .9f;
                    if (variant == 5) {
                        f.params.sun_radiance[0] = 0;
                        f.params.sun_radiance[1] *= 2;
                        f.params.sun_radiance[2] *= .5f;
                    }
                    if (variant >= 8) {
                        std::vector<float> depths(64 * 64, variant == 8 ? 0.f : 1.f);
                        upload(d, 64, 64, D3DFMT_R32F, 4, depths.data(), &shadow_map.p, false);
                        f.frame = 73;
                        f.count = 1;
                        auto& k = f.cascades[0];
                        k.map = shadow_map.p;
                        k.frame = 73;
                        k.valid = true;
                        k.bias = .00001f;
                        k.rows[0] = k.rows[5] = k.rows[10] = .00001f;
                        k.rows[11] = .5f;
                    }
                    if (f.caller_scene_open) check(d->BeginScene(), "input caller begin");
                    FogResult result;
                    check(pass.execute(f, &result), "execute");
                    require(result.applied && result.cascades_bound == (variant >= 8 ? 1u : 0u) && result.scene_known &&
                                result.scene_open == f.caller_scene_open && result.caller_state_restored &&
                                !result.route_poisoned,
                            (id + "_transaction_" + std::to_string(variant)).c_str());
                    if (f.caller_scene_open) check(d->EndScene(), "input caller end");
                    const auto prefix = std::string(argv[2]) + "/" + id + "-v" + std::to_string(variant);
                    readback(d, pass.fixture_st(), prefix + ".st.rgba16f");
                    const auto pixels = readback_words(d, ss.p);
                    write_words(pixels, prefix + ".composite.rgba16f");
                    bool alpha = true;
                    for (size_t i = 3; i < scene.size(); i += 4) alpha &= scene[i] == pixels[i];
                    require(alpha, (id + "_alpha_" + std::to_string(variant)).c_str());
                    if (variant == 6) {
                        require(pixels == scene, (id + "_invalid_depth_exact_identity").c_str());
                        const auto half = readback_words(d, pass.fixture_st());
                        bool identity = true;
                        for (size_t i = 0; i < half.size(); i += 4)
                            identity &= half[i] == 0 && half[i + 1] == 0 && half[i + 2] == 0 && half[i + 3] == 0x3c00;
                        require(identity, (id + "_invalid_depth_exact_st").c_str());
                    }
                }
            }
        }
        std::printf("RESULT production_fog checks=%u PASS\n", checks);
        return 0;
    } catch (const fog_spatial_state::Gap& e) {
        std::printf("GAP %s\n", e.what());
        return 3;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL error=%s\n", e.what());
        return 1;
    }
}
