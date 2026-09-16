// Per-call cost of the proxy's hooked state setters, of QueryPerformanceCounter
// and of the hook guard's primitives, measured in the real bottle.
//
// Original synthetic workload: one windowed HAL device, no draws, no Present,
// no readback. `device <native|proxy>` issues an interleaved mix of the game's
// hot setters (SetRenderState, SetTexture, SetSamplerState,
// SetTextureStageState, SetVertexShaderConstantF with four registers,
// SetStreamSource) in equal shares, then the same calls per setter, then the
// application getters (GetRenderState, GetSamplerState, GetTexture with the
// Release of the returned reference, GetVertexShaderConstantF with four
// registers, 1,000,000 calls each) and finally 200,000 hooked draw pairs
// (SetStreamSource + DrawIndexedPrimitive of a two-triangle buffer, inside one
// scene), and prints ns per call per repetition. run87's frame_timing carries no per-entry
// breakdown of the `state` bucket (frame_timing.h keeps one counter per bucket
// plus the slowest call's name), so the mix is equal shares by construction.
// `primitives` times QueryPerformanceCounter, an uncontended
// std::recursive_mutex lock/unlock pair (the hook guards' serialisation),
// a GetLastError/SetLastError pair, and the full four-stamp envelopes of
// LightCallBoundary and CpuCallBoundary from src/proxy/cpu_state.h.
//
// `SLOT` lines name the module that owns each hooked vtable entry after device
// creation, so a run proves whether the proxy's setter hooks were installed at
// all: the proxy hooks the device's private vtable in place, and the module
// file name separates the proxy (workdir) from the backend (system32).
//
// `PRESERVE` lines are the fail-closed proof for the light envelope
// (cpu_state.h LightCallBoundary on the binding and draw hooks): each hooked
// call is entered with a seeded live x87 stack (three values, a masked invalid
// sticky flag), a non-default control word (round toward zero, 64-bit
// precision), a non-default MXCSR (FTZ, round down) and a distinct LastError,
// and the FNSAVE image, MXCSR and LastError are compared after it. The same
// lines in native mode show what the backend itself does to that state.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/proxy/cpu_state.h" // measured, not modified
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace {
constexpr unsigned mix_rounds = 166667;      // x 6 calls = 1,000,002 calls per repetition
constexpr unsigned per_setter_calls = 166667;
constexpr unsigned getter_calls = 1000000;
constexpr unsigned draw_iterations = 200000; // SetStreamSource + DrawIndexedPrimitive pairs
constexpr unsigned primitive_iterations = 10000000;
constexpr unsigned boundary_iterations = 2000000; // FNSAVE/FRSTOR dominate CpuCallBoundary
constexpr unsigned repetitions = 3;

unsigned checks = 0;
void check(bool value, const char* label) { ++checks; if (!value) throw std::runtime_error(label); }
void ok(HRESULT hr, const char* label) { check(hr == S_OK, label); }
template <class T> struct Com { T* p = nullptr; ~Com() { if (p) p->Release(); } T* operator->() const { return p; } };
using Create = IDirect3D9* (WINAPI*)(UINT);

double frequency_hz = 1;

// x87/MXCSR/LastError image around one call (the FNSAVE form the proxy itself
// uses: fnsave resets the FPU, so it is followed by frstor).
struct CpuImage {
    alignas(16) unsigned char x87[108]; unsigned mxcsr; DWORD error;
    void capture() { error = GetLastError(); asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1" : "=m"(x87), "=m"(mxcsr) :: "memory"); }
};
// Seed: fninit, two zeros divided (masked invalid -> sticky IE, one NaN on the
// stack), 1.0 and pi pushed (three live registers), control word 0x0f7f (all
// masked, PC=64-bit, RC=truncate; default 0x027f), MXCSR 0xbf80 (FTZ, RC down,
// all masked; default 0x1f80), LastError 0x3ac.
void seed_cpu_state() {
    const unsigned short cw = 0x0f7f; const unsigned mx = 0xbf80;
    asm volatile("fninit\n\tfldz\n\tfldz\n\tfdivp\n\tfld1\n\tfldpi\n\tfldcw %0\n\tldmxcsr %1" :: "m"(cw), "m"(mx) : "memory");
    SetLastError(0x3ac);
}
void clear_cpu_state() { asm volatile("fninit" ::: "memory"); const unsigned mx = 0x1f80; asm volatile("ldmxcsr %0" :: "m"(mx) : "memory"); }
template <class Call> void preserve_check(const char* op, Call call) {
    CpuImage before{}, after{};
    seed_cpu_state(); before.capture();       // capture() restores the seeded image
    const HRESULT hr = call();
    after.capture(); clear_cpu_state();
    const bool x87 = !std::memcmp(before.x87, after.x87, sizeof before.x87);
    // Control word (bytes 0-1), status word (4-5), tag word (8-9) named separately.
    const bool control = !std::memcmp(before.x87, after.x87, 2), status = !std::memcmp(before.x87 + 4, after.x87 + 4, 2),
               tags = !std::memcmp(before.x87 + 8, after.x87 + 8, 2);
    std::printf("PRESERVE op=%s result=%08lx x87=%u control=%u status=%u tags=%u mxcsr=%u error=%u mxcsr_before=%04x mxcsr_after=%04x error_before=%lu error_after=%lu\n",
                op, static_cast<unsigned long>(hr), x87, control, status, tags, before.mxcsr == after.mxcsr, before.error == after.error,
                before.mxcsr, after.mxcsr, static_cast<unsigned long>(before.error), static_cast<unsigned long>(after.error));
}
std::uint64_t ticks() { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return static_cast<std::uint64_t>(t.QuadPart); }
void report(const char* op, unsigned rep, std::uint64_t calls, std::uint64_t elapsed) {
    std::printf("BENCH op=%s rep=%u calls=%llu elapsed_ticks=%llu ns_per_call=%.3f\n", op, rep,
                static_cast<unsigned long long>(calls), static_cast<unsigned long long>(elapsed),
                double(elapsed) * 1e9 / frequency_hz / double(calls));
}

// Rotating arguments: every call carries a value the previous one did not, so a
// redundant-state filter in the backend cannot short-circuit the measurement.
const D3DRENDERSTATETYPE render_states[4] = {D3DRS_ALPHAREF, D3DRS_STENCILREF, D3DRS_TEXTUREFACTOR, D3DRS_FOGCOLOR};
HRESULT status_or = 0;

struct Workload {
    IDirect3DDevice9* device;
    IDirect3DTexture9* textures[2];
    IDirect3DVertexBuffer9* buffers[2];
    IDirect3DVertexBuffer9* draw_vertices;
    IDirect3DIndexBuffer9* draw_indices;
    float constants[8];

    void set_render_state(unsigned i) { status_or |= device->SetRenderState(render_states[i & 3], i * 2654435761u); }
    void set_texture(unsigned i) { status_or |= device->SetTexture(0, textures[i & 1]); }
    void set_sampler_state(unsigned i) { status_or |= device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, 1 + (i & 3)); }
    void set_stage_state(unsigned i) { status_or |= device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, i & 1); }
    void set_vs_constant(unsigned i) { constants[0] = float(i); status_or |= device->SetVertexShaderConstantF(8, constants, 4); }
    void set_stream_source(unsigned i) { status_or |= device->SetStreamSource(0, buffers[i & 1], 0, 32); }

    // Application getters, measured with the same rotation discipline. GetTexture
    // returns an AddRef'd reference, so the Release is part of the timed pair.
    void get_render_state(unsigned i) { DWORD v = 0; status_or |= device->GetRenderState(render_states[i & 3], &v); }
    void get_sampler_state(unsigned i) { DWORD v = 0; status_or |= device->GetSamplerState(0, i & 1 ? D3DSAMP_MAXANISOTROPY : D3DSAMP_MIPMAPLODBIAS, &v); }
    void get_texture(unsigned) { IDirect3DBaseTexture9* t = nullptr; status_or |= device->GetTexture(0, &t); if (t) t->Release(); }
    void get_vs_constant(unsigned) { float v[4]{}; status_or |= device->GetVertexShaderConstantF(8, v, 4); }
    // The hybrid unhook's per-draw read set (state-call-fast-path.md, step 5):
    // the eight render states an admitted routed draw reads once each with the
    // SetRenderState hook off (selector z pair, gate-4 quartet, RT1 mask, one
    // wrap) plus the two sampler reads of a bias-eligible stage (MIPFILTER,
    // MIPMAPLODBIAS). One "call" of this row is the whole ten-read set.
    void draw_state_reads(unsigned) {
        static const D3DRENDERSTATETYPE set[8] = {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
                                                  D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_WRAP0};
        DWORD v = 0;
        for (auto state : set) status_or |= device->GetRenderState(state, &v);
        status_or |= device->GetSamplerState(0, D3DSAMP_MIPFILTER, &v);
        status_or |= device->GetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, &v);
    }
    // The hybrid unhook's mip-bias work per routed draw with the hooks off
    // (motion_output.cpp apply_mip_bias + the after_draw restore): per
    // bias-eligible stage GetSamplerState MIPFILTER, GetSamplerState
    // MIPMAPLODBIAS, SetSamplerState bias (-0.5f), SetSamplerState the saved
    // value back; two stages. The hooked design keeps the bias across
    // consecutive routed draws and issues none of these there.
    void routed_draw_mip_bias(unsigned) {
        for (DWORD stage = 0; stage < 2; ++stage) {
            DWORD filter = 0, saved = 0;
            status_or |= device->GetSamplerState(stage, D3DSAMP_MIPFILTER, &filter);
            status_or |= device->GetSamplerState(stage, D3DSAMP_MIPMAPLODBIAS, &saved);
            status_or |= device->SetSamplerState(stage, D3DSAMP_MIPMAPLODBIAS, 0xbf000000u);
            status_or |= device->SetSamplerState(stage, D3DSAMP_MIPMAPLODBIAS, saved);
        }
    }

    // One hooked draw: the stream rebind the game issues per draw plus the draw
    // itself (two triangles, four vertices, one index buffer).
    void draw_pair(unsigned i) {
        status_or |= device->SetStreamSource(0, draw_vertices, 0, 16);
        status_or |= device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
        (void)i;
    }
};

void mix(Workload& w, unsigned rounds) {
    for (unsigned i = 0; i < rounds; ++i) {
        w.set_render_state(i); w.set_texture(i); w.set_sampler_state(i);
        w.set_stage_state(i); w.set_vs_constant(i); w.set_stream_source(i);
    }
}

void device_benchmark(Workload& w) {
    mix(w, 20000); // warm up the backend's state tracking; untimed
    for (unsigned rep = 0; rep < repetitions; ++rep) {
        std::uint64_t begin = ticks(); mix(w, mix_rounds); report("state_mix", rep, std::uint64_t(mix_rounds) * 6, ticks() - begin);
        begin = ticks(); for (unsigned i = 0; i < per_setter_calls; ++i) w.set_render_state(i); report("SetRenderState", rep, per_setter_calls, ticks() - begin);
        begin = ticks(); for (unsigned i = 0; i < per_setter_calls; ++i) w.set_texture(i); report("SetTexture", rep, per_setter_calls, ticks() - begin);
        begin = ticks(); for (unsigned i = 0; i < per_setter_calls; ++i) w.set_sampler_state(i); report("SetSamplerState", rep, per_setter_calls, ticks() - begin);
        begin = ticks(); for (unsigned i = 0; i < per_setter_calls; ++i) w.set_stage_state(i); report("SetTextureStageState", rep, per_setter_calls, ticks() - begin);
        begin = ticks(); for (unsigned i = 0; i < per_setter_calls; ++i) w.set_vs_constant(i); report("SetVertexShaderConstantF4", rep, per_setter_calls, ticks() - begin);
        begin = ticks(); for (unsigned i = 0; i < per_setter_calls; ++i) w.set_stream_source(i); report("SetStreamSource", rep, per_setter_calls, ticks() - begin);

        begin = ticks(); for (unsigned i = 0; i < getter_calls; ++i) w.get_render_state(i); report("GetRenderState", rep, getter_calls, ticks() - begin);
        begin = ticks(); for (unsigned i = 0; i < getter_calls; ++i) w.get_sampler_state(i); report("GetSamplerState", rep, getter_calls, ticks() - begin);
        begin = ticks(); for (unsigned i = 0; i < getter_calls; ++i) w.get_texture(i); report("GetTexture_Release", rep, getter_calls, ticks() - begin);
        begin = ticks(); for (unsigned i = 0; i < getter_calls; ++i) w.get_vs_constant(i); report("GetVertexShaderConstantF4", rep, getter_calls, ticks() - begin);
        begin = ticks(); for (unsigned i = 0; i < getter_calls; ++i) w.draw_state_reads(i); report("GetState_draw_set_10", rep, getter_calls, ticks() - begin);
        begin = ticks(); for (unsigned i = 0; i < getter_calls; ++i) w.routed_draw_mip_bias(i); report("routed_draw_mip_bias_2stages", rep, getter_calls, ticks() - begin);

        // The draw pair runs inside one scene, as the game's draws do.
        check(w.device->BeginScene() == S_OK, "BeginScene");
        begin = ticks(); for (unsigned i = 0; i < draw_iterations; ++i) w.draw_pair(i);
        report("SetStreamSource_DrawIndexedPrimitive_pair", rep, draw_iterations, ticks() - begin);
        check(w.device->EndScene() == S_OK, "EndScene");
    }
    std::printf("STATUS or=%08lx\n", static_cast<unsigned long>(status_or));
}

// The light-envelope binding hooks and the draw hooks, entered with the seeded
// state (see preserve_check); the draws run inside a scene like the
// benchmark's draw pairs.
void preserve_benchmark(Workload& w, IDirect3DVertexDeclaration9* declaration) {
    IDirect3DDevice9* device = w.device;
    preserve_check("SetStreamSource", [&] { return device->SetStreamSource(0, w.draw_vertices, 0, 16); });
    preserve_check("SetIndices", [&] { return device->SetIndices(w.draw_indices); });
    preserve_check("SetVertexDeclaration", [&] { return device->SetVertexDeclaration(declaration); });
    preserve_check("SetFVF", [&] { return device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE); });
    preserve_check("SetRenderState", [&] { return device->SetRenderState(D3DRS_ALPHAREF, 7); });
    check(device->BeginScene() == S_OK, "BeginScene (preserve)");
    preserve_check("DrawIndexedPrimitive", [&] { return device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2); });
    preserve_check("DrawPrimitive", [&] { return device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1); });
    check(device->EndScene() == S_OK, "EndScene (preserve)");
}

// The hooked vtable entries of the device the fixture holds: slot -> owning
// module. `slot` indices are the SDK layout verified in abi_check.cpp.
void report_slots(IDirect3DDevice9* device) {
    void** vtable = *reinterpret_cast<void***>(device);
    const struct { unsigned slot; const char* name; } entries[] = {
        {57, "SetRenderState"}, {65, "SetTexture"}, {69, "SetSamplerState"}, {67, "SetTextureStageState"},
        {94, "SetVertexShaderConstantF"}, {100, "SetStreamSource"}, {81, "DrawIndexedPrimitive"},
        {58, "GetRenderState"}, {68, "GetSamplerState"}, {64, "GetTexture"}, {95, "GetVertexShaderConstantF"}};
    for (const auto& entry : entries) {
        HMODULE owner = nullptr; char path[2048] = "unknown";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(vtable[entry.slot]), &owner) && owner)
            GetModuleFileNameA(owner, path, sizeof path);
        std::printf("SLOT slot=%u name=%s address=%p module=%s\n", entry.slot, entry.name, vtable[entry.slot], path);
    }
}

void primitives() {
    std::recursive_mutex lock_primitive;
    for (unsigned rep = 0; rep < repetitions; ++rep) {
        std::uint64_t begin = ticks();
        for (unsigned i = 0; i < primitive_iterations; ++i) { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); asm volatile("" :: "m"(t) : "memory"); }
        report("QueryPerformanceCounter", rep, primitive_iterations, ticks() - begin);

        begin = ticks();
        for (unsigned i = 0; i < primitive_iterations; ++i) { std::lock_guard<std::recursive_mutex> held(lock_primitive); asm volatile("" ::: "memory"); }
        report("recursive_mutex_lock_unlock", rep, primitive_iterations, ticks() - begin);

        begin = ticks();
        for (unsigned i = 0; i < primitive_iterations; ++i) { const DWORD saved = GetLastError(); SetLastError(saved); asm volatile("" ::: "memory"); }
        report("GetLastError_SetLastError", rep, primitive_iterations, ticks() - begin);

        begin = ticks();
        for (unsigned i = 0; i < boundary_iterations; ++i) { x3m::LightCallBoundary b; b.before_original(); asm volatile("" ::: "memory"); b.after_original(); }
        report("LightCallBoundary_envelope", rep, boundary_iterations, ticks() - begin);

        begin = ticks();
        for (unsigned i = 0; i < boundary_iterations; ++i) { x3m::CpuCallBoundary b; b.before_original(); asm volatile("" ::: "memory"); b.after_original(); }
        report("CpuCallBoundary_envelope", rep, boundary_iterations, ticks() - begin);
    }
}
} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        check(argc >= 2, "usage: state_hook_benchmark <primitives|device> [native|proxy]");
        LARGE_INTEGER f{}; check(QueryPerformanceFrequency(&f) && f.QuadPart > 0, "QPC frequency");
        frequency_hz = double(f.QuadPart);
        std::printf("BENCHMARK frequency=%lld mix_rounds=%u per_setter_calls=%u primitive_iterations=%u boundary_iterations=%u repetitions=%u\n",
                    static_cast<long long>(f.QuadPart), mix_rounds, per_setter_calls, primitive_iterations, boundary_iterations, repetitions);
        if (!std::strcmp(argv[1], "primitives")) { primitives(); }
        else {
            check(argc == 3 && !std::strcmp(argv[1], "device"), "device mode needs native|proxy");
            const bool native = !std::strcmp(argv[2], "native");
            check(native || !std::strcmp(argv[2], "proxy"), "mode");
            HMODULE module = LoadLibraryA(native ? "C:\\windows\\system32\\d3d9.dll" : "d3d9.dll");
            check(module != nullptr, "load D3D9");
            char module_path[2048]{}; GetModuleFileNameA(module, module_path, sizeof module_path);
            std::printf("MODULE mode=%s path=%s\n", argv[2], module_path);
            const auto entry = GetProcAddress(module, "Direct3DCreate9");
            Create create = nullptr; std::memcpy(&create, &entry, sizeof create);
            check(create != nullptr, "D3D9 entry");
            HWND window = CreateWindowExA(0, "STATIC", "state hook benchmark", WS_OVERLAPPEDWINDOW, 0, 0, 96, 96,
                                          nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
            check(window != nullptr, "window");
            {
                Com<IDirect3D9> factory; factory.p = create(D3D_SDK_VERSION); check(factory.p != nullptr, "factory");
                D3DPRESENT_PARAMETERS pp{};
                pp.Windowed = TRUE; pp.hDeviceWindow = window; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
                pp.BackBufferWidth = pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_A8R8G8B8;
                pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
                pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
                Com<IDirect3DDevice9> device;
                ok(factory->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p), "device");
                report_slots(device.p);
                Com<IDirect3DTexture9> a, b; Com<IDirect3DVertexBuffer9> va, vb;
                ok(device->CreateTexture(64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &a.p, nullptr), "texture a");
                ok(device->CreateTexture(64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &b.p, nullptr), "texture b");
                ok(device->CreateVertexBuffer(32 * 64, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &va.p, nullptr), "vb a");
                ok(device->CreateVertexBuffer(32 * 64, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb.p, nullptr), "vb b");
                // Two triangles, four XYZ|DIFFUSE vertices, six indices.
                Com<IDirect3DVertexBuffer9> quad; Com<IDirect3DIndexBuffer9> quad_indices;
                ok(device->CreateVertexBuffer(4 * 16, D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &quad.p, nullptr), "quad vb");
                ok(device->CreateIndexBuffer(6 * sizeof(std::uint16_t), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &quad_indices.p, nullptr), "quad ib");
                {
                    struct Vertex { float x, y, z; DWORD color; };
                    const Vertex vertices[4] = {{-0.5f, -0.5f, 0.5f, 0xffff0000}, {-0.5f, 0.5f, 0.5f, 0xff00ff00},
                                                {0.5f, 0.5f, 0.5f, 0xff0000ff}, {0.5f, -0.5f, 0.5f, 0xffffffff}};
                    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
                    void* data = nullptr;
                    ok(quad->Lock(0, sizeof vertices, &data, 0), "quad vb lock"); std::memcpy(data, vertices, sizeof vertices); ok(quad->Unlock(), "quad vb unlock");
                    ok(quad_indices->Lock(0, sizeof indices, &data, 0), "quad ib lock"); std::memcpy(data, indices, sizeof indices); ok(quad_indices->Unlock(), "quad ib unlock");
                }
                ok(device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE), "quad fvf");
                ok(device->SetIndices(quad_indices.p), "quad indices");
                ok(device->SetRenderState(D3DRS_LIGHTING, FALSE), "lighting off");
                Workload w{device.p, {a.p, b.p}, {va.p, vb.p}, quad.p, quad_indices.p, {}};
                for (unsigned i = 0; i < 8; ++i) w.constants[i] = float(i);
                device_benchmark(w);
                Com<IDirect3DVertexDeclaration9> declaration;
                {
                    const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                                          {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0}, D3DDECL_END()};
                    ok(device->CreateVertexDeclaration(elements, &declaration.p), "declaration");
                }
                preserve_benchmark(w, declaration.p);
                ok(device->SetTexture(0, nullptr), "unbind texture");
                ok(device->SetStreamSource(0, nullptr, 0, 0), "unbind stream");
            }
            DestroyWindow(window);
        }
        std::printf("RESULT checks=%u status=pass\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::printf("RESULT checks=%u status=fail reason=%s\n", checks, error.what());
        return 1;
    }
}
