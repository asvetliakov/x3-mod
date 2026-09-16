// Per-call cost of the proxy's hooked state setters, of QueryPerformanceCounter
// and of the hook guard's primitives, measured in the real bottle.
//
// Original synthetic workload: one windowed HAL device, no draws, no Present,
// no readback. `device <native|proxy>` issues an interleaved mix of the game's
// hot setters (SetRenderState, SetTexture, SetSamplerState,
// SetTextureStageState, SetVertexShaderConstantF with four registers,
// SetStreamSource) in equal shares, then the same calls per setter, and prints
// ns per call per repetition. run87's frame_timing carries no per-entry
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
constexpr unsigned primitive_iterations = 10000000;
constexpr unsigned boundary_iterations = 2000000; // FNSAVE/FRSTOR dominate CpuCallBoundary
constexpr unsigned repetitions = 3;

unsigned checks = 0;
void check(bool value, const char* label) { ++checks; if (!value) throw std::runtime_error(label); }
void ok(HRESULT hr, const char* label) { check(hr == S_OK, label); }
template <class T> struct Com { T* p = nullptr; ~Com() { if (p) p->Release(); } T* operator->() const { return p; } };
using Create = IDirect3D9* (WINAPI*)(UINT);

double frequency_hz = 1;
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
    float constants[8];

    void set_render_state(unsigned i) { status_or |= device->SetRenderState(render_states[i & 3], i * 2654435761u); }
    void set_texture(unsigned i) { status_or |= device->SetTexture(0, textures[i & 1]); }
    void set_sampler_state(unsigned i) { status_or |= device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, 1 + (i & 3)); }
    void set_stage_state(unsigned i) { status_or |= device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, i & 1); }
    void set_vs_constant(unsigned i) { constants[0] = float(i); status_or |= device->SetVertexShaderConstantF(8, constants, 4); }
    void set_stream_source(unsigned i) { status_or |= device->SetStreamSource(0, buffers[i & 1], 0, 32); }
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
    }
    std::printf("STATUS or=%08lx\n", static_cast<unsigned long>(status_or));
}

// The hooked vtable entries of the device the fixture holds: slot -> owning
// module. `slot` indices are the SDK layout verified in abi_check.cpp.
void report_slots(IDirect3DDevice9* device) {
    void** vtable = *reinterpret_cast<void***>(device);
    const struct { unsigned slot; const char* name; } entries[] = {
        {57, "SetRenderState"}, {65, "SetTexture"}, {69, "SetSamplerState"}, {67, "SetTextureStageState"},
        {94, "SetVertexShaderConstantF"}, {100, "SetStreamSource"}, {81, "DrawIndexedPrimitive"}};
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
                Workload w{device.p, {a.p, b.p}, {va.p, vb.p}, {}};
                for (unsigned i = 0; i < 8; ++i) w.constants[i] = float(i);
                device_benchmark(w);
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
