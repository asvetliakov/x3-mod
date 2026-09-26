// Original hidden native D3D9 device; fault injection only replaces per-instance
// native method slots. No application DLL, game process or install is touched.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/ownership/d3d9_ownership.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace x3m::ownership;
namespace {
unsigned checks = 0, failures = 0, retired = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", name);
    }
}
void require(HRESULT hr, const char* name) {
    if (FAILED(hr)) {
        std::printf("API_FAIL %s %08lx\n", name, hr);
        throw std::runtime_error(name);
    }
}
ExecutionView view(IDirect3DDevice9* d) {
    ExecutionView v;
    require(get_execution_view(d, &v), "execution view");
    return v;
}
const GUID marker_id = {0xbe659151, 0x44a1, 0x43bf, {0x88, 0x27, 0x01, 0x53, 0x62, 0xa7, 0x4e, 0x1d}};
struct Marker final : IUnknown {
    ULONG refs = 1;
    HRESULT WINAPI QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown) return E_NOINTERFACE;
        *out = this;
        AddRef();
        return S_OK;
    }
    ULONG WINAPI AddRef() override { return ++refs; }
    ULONG WINAPI Release() override {
        auto n = --refs;
        if (!n) {
            ++retired;
            delete this;
        }
        return n;
    }
};
struct Device {
    IDirect3DDevice9* app = nullptr;
    IDirect3DDevice9* native = nullptr;
    D3DPRESENT_PARAMETERS pp{};
    Device(IDirect3D9* factory, HWND window, bool pure = false) {
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.EnableAutoDepthStencil = TRUE;
        pp.AutoDepthStencilFormat = D3DFMT_D24X8;
        require(factory->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                      D3DCREATE_HARDWARE_VERTEXPROCESSING | (pure ? D3DCREATE_PUREDEVICE : 0), &pp,
                                      &app),
                "CreateDevice");
        native = borrowed_native_device(app);
    }
    ~Device() {
        if (app) app->Release();
    }
    void reset() { require(app->Reset(&pp), "Reset"); }
    void history() {
        IDirect3DTexture9* t = nullptr;
        require(native->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t, nullptr), "history");
        auto m = new Marker;
        require(t->SetPrivateData(marker_id, m, sizeof(IUnknown*), D3DSPD_IUNKNOWN), "history marker");
        m->Release();
        require(retain_renderer_resource(app, t), "retain history");
    }
};
struct Patch {
    void* object;
    void** saved;
    std::array<void*, 119> table{};
    Patch(void* target, unsigned count, unsigned slot, void* replacement)
        : object(target)
        , saved(*reinterpret_cast<void***>(target)) {
        std::memcpy(table.data(), saved, count * sizeof(void*));
        table[slot] = replacement;
        *reinterpret_cast<void***>(target) = table.data();
    }
    ~Patch() { *reinterpret_cast<void***>(object) = saved; }
};
HRESULT injected = E_FAIL;
HRESULT WINAPI noarg(IDirect3DDevice9*) {
    return injected;
}
HRESULT WINAPI gettexture(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9** out) {
    if (out) *out = nullptr;
    return injected;
}
HRESULT WINAPI getdepth(IDirect3DDevice9*, IDirect3DSurface9** out) {
    if (out) *out = nullptr;
    return injected;
}
HRESULT WINAPI container(IDirect3DSurface9*, REFIID, void** out) {
    if (out) *out = nullptr;
    return injected;
}
HRESULT WINAPI private_data(IDirect3DVertexBuffer9*, REFGUID, const void*, DWORD, DWORD) {
    return injected;
}
HRESULT WINAPI process(IDirect3DDevice9*, UINT, UINT, UINT, IDirect3DVertexBuffer9*, IDirect3DVertexDeclaration9*,
                       DWORD) {
    return injected;
}
void basics(IDirect3D9* factory, HWND window, bool pure) {
    Device d(factory, window, pure);
    auto v = view(d.app);
    check(v.known && !v.scene_open && !v.stateblock_recording && v.queries_idle, "pristine known idle");
    ExecutionView invalid;
    check(get_execution_view(d.native, &invalid) == E_INVALIDARG && !invalid.known, "native view rejected");
    check(get_execution_view(d.app, nullptr) == E_POINTER, "null output rejected");
    check(invalidate_execution_state(d.native) == E_INVALIDARG, "native invalidation rejected");
    require(d.app->BeginScene(), "BeginScene");
    check(view(d.app).scene_open, "scene opened");
    require(d.app->BeginStateBlock(), "BeginStateBlock");
    check(view(d.app).stateblock_recording, "recording observed");
    IDirect3DStateBlock9* state = nullptr;
    require(d.app->EndStateBlock(&state), "EndStateBlock");
    check(!view(d.app).stateblock_recording, "recording ended");
    require(state->Capture(), "state capture");
    require(state->Apply(), "state apply");
    state->Release();
    check(view(d.app).known && view(d.app).scene_open, "stateblock preserves scene");
    require(d.app->CreateQuery(D3DQUERYTYPE_EVENT, nullptr), "query support probe");
    check(view(d.app).known, "support probe not query object");
    IDirect3DQuery9* q = nullptr;
    require(d.app->CreateQuery(D3DQUERYTYPE_OCCLUSION, &q), "occlusion query");
    check(view(d.app).queries_idle, "created occlusion idle");
    require(q->Issue(D3DISSUE_BEGIN), "query begin");
    check(view(d.app).known && !view(d.app).queries_idle && view(d.app).active_queries == 1,
          "active interval blocks replay");
    require(d.app->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0), "query enclosed clear");
    require(q->Issue(D3DISSUE_END), "query end");
    check(view(d.app).queries_idle, "ended interval idle without GetData");
    q->Release();
    require(d.app->EndScene(), "EndScene");
    check(!view(d.app).scene_open, "scene closed");
    auto generation = view(d.app).generation;
    d.reset();
    check(view(d.app).known && view(d.app).queries_idle && view(d.app).generation > generation,
          "Reset fresh scene generation");
    {
        injected = E_FAIL;
        Patch p(d.native, 119, 41, reinterpret_cast<void*>(&noarg));
        check(d.app->BeginScene() == E_FAIL, "ordinary Begin HRESULT preserved");
    }
    check(!view(d.app).known, "failed Begin unknown");
    d.reset();
    check(view(d.app).known, "Reset recovers transition failure");
    require(invalidate_execution_state(d.app), "explicit invalidation");
    check(!view(d.app).known, "native failure permanent invalidation");
    d.reset();
    check(!view(d.app).known, "invalidation survives Reset");
    std::printf("CASE basic pure=%u PASS\n", pure);
}
void unsupported(IDirect3D9* factory, HWND window) {
    Device d(factory, window);
    IDirect3DQuery9* q = nullptr;
    require(d.app->CreateQuery(D3DQUERYTYPE_EVENT, &q), "real event query");
    check(!view(d.app).known && view(d.app).reason == ExecutionReason::UnsupportedQuery, "event creation unavailable");
    q->Release();
    d.reset();
    check(!view(d.app).known, "query taint survives release Reset");
}
void lost_case(IDirect3D9* factory, HWND window, unsigned kind, HRESULT hr) {
    Device d(factory, window);
    d.history();
    unsigned old = retired;
    require(d.app->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, .5f, 0), "source clear");
    require(copy_auto_depth(d.app), "depth snapshot");
    CopyDepthView before{}, after{};
    require(get_copy_depth_view(d.app, &before), "initial copy view");
    check(before.copy_valid, "copy initially valid");
    injected = hr;
    HRESULT result = E_UNEXPECTED;
    if (kind == 0) {
        Patch p(d.native, 119, 64, reinterpret_cast<void*>(&gettexture));
        IDirect3DBaseTexture9* t = nullptr;
        result = d.app->GetTexture(0, &t);
        check(!t, "base output null");
    }
    if (kind == 1) {
        Patch p(d.native, 119, 40, reinterpret_cast<void*>(&getdepth));
        IDirect3DSurface9* t = nullptr;
        result = d.app->GetDepthStencilSurface(&t);
        check(!t, "typed output null");
    }
    if (kind == 2) {
        IDirect3DTexture9 *texture = nullptr, *native_texture = nullptr;
        IDirect3DSurface9 *surface = nullptr, *native_surface = nullptr;
        require(d.app->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr),
                "container texture");
        require(texture->GetSurfaceLevel(0, &surface), "wrapped texture surface");
        require(d.app->SetTexture(0, texture), "bind container texture");
        require(d.native->GetTexture(0, reinterpret_cast<IDirect3DBaseTexture9**>(&native_texture)), "native texture");
        require(native_texture->GetSurfaceLevel(0, &native_surface), "native surface");
        {
            Patch p(native_surface, 17, 11, reinterpret_cast<void*>(&container));
            void* out = nullptr;
            result = surface->GetContainer(IID_IDirect3DTexture9, &out);
            check(!out, "container output null");
        }
        native_surface->Release();
        native_texture->Release();
        surface->Release();
        texture->Release();
    }
    if (kind == 3 || kind == 4) {
        IDirect3DVertexBuffer9* vb = nullptr;
        require(d.app->CreateVertexBuffer(64, 0, 0, D3DPOOL_MANAGED, &vb, nullptr), "fault VB");
        auto nv = borrowed_native_buffer_for_lock_contract(vb);
        check(nv != nullptr, "native VB endpoint");
        if (kind == 3) {
            Patch p(nv, 14, 4, reinterpret_cast<void*>(&private_data));
            DWORD bytes = 1;
            result = vb->SetPrivateData(marker_id, &bytes, 4, 0);
        } else {
            Patch p(d.native, 119, 85, reinterpret_cast<void*>(&process));
            result = d.app->ProcessVertices(0, 0, 1, vb, nullptr, 0);
        }
        vb->Release();
    }
    check(result == hr, "injected native HRESULT unchanged");
    require(get_copy_depth_view(d.app, &after), "post fault copy view");
    if (hr == E_FAIL) {
        check(view(d.app).known, "ordinary getter failure preserves execution");
        check(after.available && after.copy_valid, "ordinary error preserves snapshot");
        check(retired == old, "ordinary error preserves renderer resource");
    } else {
        check(!view(d.app).known, "getter loss invalidates execution");
        check(!after.available && !after.texture && !after.copy_valid, "getter loss retires snapshot");
        check(retired == old + 1, "getter loss retires renderer resource");
    }
    std::printf("CASE loss method=%u hr=%08lx PASS\n", kind, hr);
}
struct Barrier {
    IDirect3DDevice9* device = nullptr;
    IDirect3DQuery9* query = nullptr;
    HANDLE entered = CreateEventA(nullptr, TRUE, FALSE, nullptr), resume = CreateEventA(nullptr, TRUE, FALSE, nullptr),
           read_done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    unsigned operation = 0;
    HRESULT result = E_UNEXPECTED, read_result = E_UNEXPECTED;
    ExecutionView observed;
    volatile LONG timed_out = 0;
    ~Barrier() {
        CloseHandle(entered);
        CloseHandle(resume);
        CloseHandle(read_done);
    }
    static Barrier* active;
    static HRESULT WINAPI pause_scene(IDirect3DDevice9*) { return pause(); }
    static HRESULT WINAPI pause() {
        auto& b = *active;
        SetEvent(b.entered);
        if (WaitForSingleObject(b.resume, 5000) != WAIT_OBJECT_0) InterlockedExchange(&b.timed_out, 1);
        return S_OK;
    }
    static HRESULT WINAPI end_scene(IDirect3DDevice9*) { return S_OK; }
    static HRESULT WINAPI end_block(IDirect3DDevice9*, IDirect3DStateBlock9** out) {
        if (out) *out = nullptr;
        return S_OK;
    }
    static HRESULT WINAPI issue(IDirect3DQuery9*, DWORD flags) { return flags == D3DISSUE_BEGIN ? pause() : S_OK; }
    static DWORD WINAPI writer(void* p) {
        auto& b = *static_cast<Barrier*>(p);
        b.result = b.operation == 0   ? b.device->BeginScene()
                   : b.operation == 1 ? b.device->BeginStateBlock()
                                      : b.query->Issue(D3DISSUE_BEGIN);
        return 0;
    }
    static DWORD WINAPI reader(void* p) {
        auto& b = *static_cast<Barrier*>(p);
        b.read_result = get_execution_view(b.device, &b.observed);
        SetEvent(b.read_done);
        return 0;
    }
};
Barrier* Barrier::active = nullptr;
IDirect3DQuery9* captured_query = nullptr;
using CreateQueryFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DQUERYTYPE, IDirect3DQuery9**);
CreateQueryFn original_create_query = nullptr;
HRESULT WINAPI capture_query(IDirect3DDevice9* d, D3DQUERYTYPE type, IDirect3DQuery9** out) {
    auto hr = original_create_query(d, type, out);
    if (SUCCEEDED(hr) && out && *out) {
        captured_query = *out;
        captured_query->AddRef();
    }
    return hr;
}
void threads(IDirect3D9* factory, HWND window, unsigned operation) {
    Device d(factory, window);
    Barrier b;
    b.device = d.app;
    b.operation = operation;
    Barrier::active = &b;
    check(b.entered && b.resume && b.read_done, "Win32 barrier events");
    if (operation == 2) {
        auto table = *reinterpret_cast<void***>(d.native);
        original_create_query = reinterpret_cast<CreateQueryFn>(table[118]);
        {
            Patch p(d.native, 119, 118, reinterpret_cast<void*>(&capture_query));
            require(d.app->CreateQuery(D3DQUERYTYPE_OCCLUSION, &b.query), "thread query");
        }
        check(captured_query != nullptr, "retained actual native query for test-only Issue barrier");
    }
    {
        Patch pause(operation == 2 ? static_cast<void*>(captured_query) : static_cast<void*>(d.native),
                    operation == 2 ? 8 : 119,
                    operation == 2   ? 6
                    : operation == 0 ? 41
                                     : 60,
                    operation == 2 ? reinterpret_cast<void*>(&Barrier::issue)
                                   : reinterpret_cast<void*>(&Barrier::pause_scene));
        Patch finish(d.native, 119, operation == 1 ? 61 : 42,
                     operation == 1 ? reinterpret_cast<void*>(&Barrier::end_block)
                                    : reinterpret_cast<void*>(&Barrier::end_scene));
        HANDLE writer = CreateThread(nullptr, 0, &Barrier::writer, &b, 0, nullptr);
        check(writer != nullptr, "native barrier writer thread");
        bool entered = WaitForSingleObject(b.entered, 3000) == WAIT_OBJECT_0;
        check(entered, "native method entered before snapshot");
        HANDLE reader = CreateThread(nullptr, 0, &Barrier::reader, &b, 0, nullptr);
        check(reader != nullptr, "concurrent observer reader");
        bool read = WaitForSingleObject(b.read_done, 2000) == WAIT_OBJECT_0;
        check(read, "snapshot returns while native call blocked (no observer mutex held)");
        if (read) {
            check(b.read_result == S_OK && !b.observed.known && !b.observed.queries_idle && b.observed.in_flight == 1 &&
                      b.observed.reason == ExecutionReason::NativeCallInFlight,
                  "in-flight native transition refuses snapshot");
            HRESULT second;
            if (operation == 0)
                second = d.app->EndScene();
            else if (operation == 1) {
                IDirect3DStateBlock9* output = nullptr;
                second = d.app->EndStateBlock(&output);
                if (output) output->Release();
            } else
                second = b.query->Issue(D3DISSUE_END);
            check(second == S_OK, "overlapping native result returned unchanged");
            check(!view(d.app).known && view(d.app).reason == ExecutionReason::OverlappingNativeCalls,
                  "reversed completion permanently tainted before first result");
        }
        SetEvent(b.resume);
        if (WaitForSingleObject(writer, 3000) != WAIT_OBJECT_0 || WaitForSingleObject(reader, 3000) != WAIT_OBJECT_0)
            ExitProcess(9);
        CloseHandle(writer);
        CloseHandle(reader);
        check(b.result == S_OK && !b.timed_out, "blocked native result completes normally");
    }
    check(!view(d.app).known && view(d.app).in_flight == 0, "late successful native return does not heal overlap");
    if (b.query) b.query->Release();
    if (captured_query) {
        captured_query->Release();
        captured_query = nullptr;
    }
    d.reset();
    check(!view(d.app).known, "overlap remains unknown after actual Reset");
    Barrier::active = nullptr;
    std::printf("CASE thread operation=%u PASS\n", operation);
}
struct CallState {
    unsigned char x87[108];
    unsigned mxcsr;
    DWORD error;
    CallState()
        : error(GetLastError()) {
        asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1" : "=m"(x87), "=m"(mxcsr)::"memory");
    }
    void restore() const {
        asm volatile("frstor %0\n\tldmxcsr %1" ::"m"(x87), "m"(mxcsr) : "memory");
        SetLastError(error);
    }
    bool equal(const CallState& other) const {
        return !std::memcmp(x87, other.x87, 108) && mxcsr == other.mxcsr && error == other.error;
    }
};
CallState *cpu_seen = nullptr, *cpu_output = nullptr;
unsigned cpu_scene_calls = 0;
HRESULT WINAPI cpu_scene(IDirect3DDevice9*) {
    *cpu_seen = CallState{};
    ++cpu_scene_calls;
    cpu_output->restore();
    return E_FAIL;
}
HRESULT WINAPI cpu_reset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) {
    *cpu_seen = CallState{};
    cpu_output->restore();
    return E_FAIL;
}
HRESULT WINAPI cpu_query(IDirect3DDevice9* d, D3DQUERYTYPE type, IDirect3DQuery9** out) {
    *cpu_seen = CallState{};
    auto hr = original_create_query(d, type, out);
    cpu_output->restore();
    return hr;
}
void cpu_preservation(IDirect3D9* factory, HWND window, bool disabled = false) {
    cpu_scene_calls = 0;
    Device d(factory, window);
    CallState original;
    unsigned short control = 0x077f;
    unsigned mxcsr = 0x3fa0;
    asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1" ::"m"(control), "m"(mxcsr) : "memory");
    SetLastError(0x99224466);
    CallState incoming;
    control = 0x0b7f;
    mxcsr = 0x5f80;
    asm volatile("fninit\n\tfldz\n\tfldcw %0\n\tldmxcsr %1" ::"m"(control), "m"(mxcsr) : "memory");
    SetLastError(0xaabb1177);
    CallState outgoing;
    original.restore();
    for (unsigned operation = 0; operation < (disabled ? 1u : 3u); ++operation) {
        CallState seen;
        cpu_seen = &seen;
        cpu_output = &outgoing;
        auto table = *reinterpret_cast<void***>(d.native);
        original_create_query = reinterpret_cast<CreateQueryFn>(table[118]);
        Patch p(d.native, 119,
                operation == 0   ? 41
                : operation == 1 ? 118
                                 : 16,
                operation == 0   ? reinterpret_cast<void*>(&cpu_scene)
                : operation == 1 ? reinterpret_cast<void*>(&cpu_query)
                                 : reinterpret_cast<void*>(&cpu_reset));
        IDirect3DQuery9* q = nullptr;
        incoming.restore();
        HRESULT hr = operation == 0   ? d.app->BeginScene()
                     : operation == 1 ? d.app->CreateQuery(D3DQUERYTYPE_OCCLUSION, &q)
                                      : d.app->Reset(&d.pp);
        CallState after;
        original.restore();
        check(seen.equal(incoming), "native receives exact entry x87/MXCSR/LastError before observer mutex");
        check(after.equal(outgoing), "native outgoing CPU state survives ticket destruction and result bookkeeping");
        check(hr == (operation == 1 ? S_OK : E_FAIL), "CPU control native HRESULT unchanged");
        if (q) q->Release();
    }
    ExecutionView v;
    incoming.restore();
    get_execution_view(d.app, &v);
    CallState getter;
    original.restore();
    check(getter.equal(incoming), "snapshot lock preserves caller CPU state");
    incoming.restore();
    invalidate_execution_state(d.app);
    CallState invalidate;
    original.restore();
    check(invalidate.equal(incoming), "invalidation lock preserves caller CPU state");
    if (disabled) check(cpu_scene_calls == 1, "disabled BeginScene dispatches exactly once without observer work");
    std::printf("CASE cpu-state disabled=%u PASS\n", disabled);
}
void timing(IDirect3D9* factory, HWND window) {
    Device d(factory, window);
    constexpr unsigned count = 1000;
    LARGE_INTEGER frequency, start, stop;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    for (unsigned i = 0; i < count; ++i) {
        require(d.native->BeginScene(), "baseline Begin");
        require(d.native->EndScene(), "baseline End");
    }
    QueryPerformanceCounter(&stop);
    auto baseline = stop.QuadPart - start.QuadPart;
    QueryPerformanceCounter(&start);
    for (unsigned i = 0; i < count; ++i) {
        require(d.app->BeginScene(), "observed Begin");
        require(d.app->EndScene(), "observed End");
    }
    QueryPerformanceCounter(&stop);
    auto observed = stop.QuadPart - start.QuadPart;
    check(view(d.app).known && !view(d.app).scene_open && view(d.app).in_flight == 0,
          "timed completed scope pairs remain known");
    std::printf("TIMING pairs=%u native_ticks=%lld observer_ticks=%lld frequency=%lld\n", count, baseline, observed,
                frequency.QuadPart);
    std::printf("CASE dispatch-timing PASS\n");
}

}
int main() {
    try {
        auto module = LoadLibraryA("d3d9.dll");
        if (!module) throw std::runtime_error("d3d9");
        IDirect3D9*(WINAPI * create)(UINT) = nullptr;
        auto proc = GetProcAddress(module, "Direct3DCreate9");
        std::memcpy(&create, &proc, sizeof(create));
        if (!create) throw std::runtime_error("Direct3DCreate9");
        IDirect3D9* factory = nullptr;
        Options options;
        options.track_execution_state = true;
        options.capture_auto_depth = true;
        require(wrap_factory(create(D3D_SDK_VERSION), &factory, options), "wrap factory");
        HWND window = CreateWindowExA(0, "STATIC", "Original execution observer fixture", WS_POPUP, 0, 0, 64, 64,
                                      nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
        if (!window) throw std::runtime_error("window");
        {
            IDirect3D9* disabled = nullptr;
            require(wrap_factory(create(D3D_SDK_VERSION), &disabled), "default disabled factory");
            {
                Device d(disabled, window);
                check(!view(d.app).requested && !view(d.app).known && !view(d.app).queries_idle,
                      "default option unavailable");
                require(d.app->BeginScene(), "disabled BeginScene");
                require(d.app->EndScene(), "disabled EndScene");
                d.reset();
                check(!view(d.app).known, "disabled stays unknown after calls and Reset");
            }
            cpu_preservation(disabled, window, true);
            disabled->Release();
        }
        basics(factory, window, false);
        basics(factory, window, true);
        unsupported(factory, window);
        for (unsigned kind = 0; kind < 5; ++kind)
            for (auto hr : {E_FAIL, D3DERR_DEVICELOST, D3DERR_DEVICENOTRESET}) lost_case(factory, window, kind, hr);
        for (unsigned operation = 0; operation < 3; ++operation) threads(factory, window, operation);
        cpu_preservation(factory, window);
        timing(factory, window);
        factory->Release();
        DestroyWindow(window);
        FreeLibrary(module);
        std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::printf("EXCEPTION %s\nRESULT FAIL checks=%u failures=%u\n", e.what(), checks, failures + 1);
        return 2;
    }
}
