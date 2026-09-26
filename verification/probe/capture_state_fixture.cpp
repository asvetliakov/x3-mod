// Exercises typed constants, stateblock-restored buffer/texture bindings and
// resource allocation IDs through the public D3D9 API. No game assets required.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <cwchar>

static unsigned failures;
static bool check(const char* name, HRESULT hr) {
    std::printf("%s: %08lx %s\n", name, hr, SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (FAILED(hr)) ++failures;
    return SUCCEEDED(hr);
}
static void expect(const char* name, bool value) {
    std::printf("%s: %s\n", name, value ? "PASS" : "FAIL");
    if (!value) ++failures;
}
// Test-only interception occurs on the backend device BEFORE the real proxy
// installs capture hooks or adopts ownership. Thus the public Clear call must
// cross the shipped DLL boundary before reaching this original-call witness.
// This deliberately does not include/use the production CPU-state helper.
struct ClearCpuState {
    unsigned char x87[108]{};
    unsigned mxcsr = 0;
    DWORD error = 0;
    void capture() {
        error = GetLastError();
        asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1" : "=m"(x87), "=m"(mxcsr)::"memory");
    }
    void restore() const {
        asm volatile("frstor %0\n\tldmxcsr %1" ::"m"(x87), "m"(mxcsr) : "memory");
        SetLastError(error);
    }
    bool same(const ClearCpuState& other) const {
        return error == other.error && mxcsr == other.mxcsr && !std::memcmp(x87, other.x87, sizeof x87);
    }
};
class NativeClearProbe {
    using Create = HRESULT(WINAPI*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
                                    IDirect3DDevice9**);
    using Clear = HRESULT(WINAPI*)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
    struct Device {
        IDirect3DDevice9* object = nullptr;
        void* table[119]{};
        Clear original = nullptr;
    } devices_[2];
    static NativeClearProbe* active_;
    HMODULE module_ = nullptr;
    IDirect3D9* factory_ = nullptr;
    void** factory_table_ = nullptr;
    Create create_ = nullptr;
    DWORD original_protection_ = 0;
    bool patched_ = false;
    unsigned created_ = 0;
    static HRESULT WINAPI create(IDirect3D9* api, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                 D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
        auto& self = *active_;
        const HRESULT hr = self.create_(api, adapter, type, window, flags, pp, out);
        if (SUCCEEDED(hr) && out && *out && self.created_ < 2) {
            auto& record = self.devices_[self.created_++];
            record.object = *out;
            std::memcpy(record.table, *reinterpret_cast<void***>(*out), sizeof record.table);
            std::memcpy(&record.original, &record.table[43], sizeof record.original);
            record.table[43] = reinterpret_cast<void*>(&clear);
            *reinterpret_cast<void***>(*out) = record.table;
        }
        return hr;
    }
    static HRESULT WINAPI clear(IDirect3DDevice9* device, DWORD count, const D3DRECT* rects, DWORD flags,
                                D3DCOLOR color, float depth, DWORD stencil) {
        ClearCpuState entry;
        entry.capture();
        auto& self = *active_;
        Clear original = nullptr;
        for (const auto& record : self.devices_)
            if (record.object == device) original = record.original;
        if (!original) return E_UNEXPECTED;
        if (self.armed) {
            self.observed = entry;
            ++self.calls;
            self.arguments = count == self.count && rects == self.rects && flags == self.flags && color == self.color &&
                             stencil == self.stencil && !std::memcmp(&depth, &self.depth, sizeof depth);
        }
        entry.restore();
        const HRESULT hr = original(device, count, rects, flags, color, depth, stencil);
        if (self.armed) {
            self.result = hr;
            self.outgoing.restore();
        }
        return hr;
    }

public:
    bool armed = false, arguments = false;
    unsigned calls = 0;
    DWORD count = 0, flags = 0, stencil = 0;
    const D3DRECT* rects = nullptr;
    D3DCOLOR color = 0;
    float depth = 0;
    HRESULT result = E_UNEXPECTED;
    ClearCpuState observed, outgoing;
    bool install() {
        wchar_t path[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(path, MAX_PATH);
        if (!length || length + 11 >= MAX_PATH) return false;
        std::wcscat(path, L"\\d3d9.dll");
        module_ = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        auto address = module_ ? GetProcAddress(module_, "Direct3DCreate9") : nullptr;
        IDirect3D9*(WINAPI * factory)(UINT) = nullptr;
        std::memcpy(&factory, &address, sizeof factory);
        factory_ = factory ? factory(D3D_SDK_VERSION) : nullptr;
        if (!factory_) return false;
        factory_table_ = *reinterpret_cast<void***>(factory_);
        std::memcpy(&create_, &factory_table_[16], sizeof create_);
        DWORD protection = 0;
        if (!VirtualProtect(&factory_table_[16], sizeof(void*), PAGE_EXECUTE_READWRITE, &protection)) return false;
        original_protection_ = protection;
        active_ = this;
        patched_ = true; // Retain restoration ownership before writing.
        InterlockedExchangePointer(&factory_table_[16], reinterpret_cast<void*>(&create));
        DWORD ignored = 0;
        return VirtualProtect(&factory_table_[16], sizeof(void*), protection, &ignored) != FALSE;
    }
    void restore_factory() {
        if (!patched_) return;
        DWORD protection = 0;
        if (VirtualProtect(&factory_table_[16], sizeof(void*), PAGE_EXECUTE_READWRITE, &protection)) {
            InterlockedExchangePointer(&factory_table_[16], reinterpret_cast<void*>(create_));
            DWORD ignored = 0;
            const bool restored = VirtualProtect(&factory_table_[16], sizeof(void*), original_protection_, &ignored) !=
                                  FALSE;
            expect("restore backend factory page", restored);
            patched_ = !restored; // A failed protection restore remains owned for destructor retry.
        } else
            expect("restore backend factory slot", false);
    }
    ~NativeClearProbe() {
        restore_factory();
        if (factory_) factory_->Release();
        if (module_) FreeLibrary(module_);
        active_ = nullptr;
    }
};
NativeClearProbe* NativeClearProbe::active_ = nullptr;

static void clear_cpu_boundary(IDirect3DDevice9* device, NativeClearProbe& probe) {
    ClearCpuState saved;
    saved.capture();
    const unsigned short incoming_control = 0x077f, outgoing_control = 0x0b7f;
    const unsigned incoming_mxcsr = 0x3fa0, outgoing_mxcsr = 0x5f81;
    asm volatile("fninit\n\tfld1\n\tfldz\n\tfldpi\n\tfldcw %0\n\tldmxcsr %1" ::"m"(incoming_control),
                 "m"(incoming_mxcsr)
                 : "memory");
    SetLastError(0x13579bdf);
    ClearCpuState incoming;
    incoming.capture();
    asm volatile("fninit\n\tfldln2\n\tfld1\n\tfldcw %0\n\tldmxcsr %1" ::"m"(outgoing_control), "m"(outgoing_mxcsr)
                 : "memory");
    SetLastError(0x2468ace0);
    probe.outgoing.capture();
    saved.restore();
    const D3DRECT rectangle{1, 2, 9, 10};
    for (unsigned failure = 0; failure < 2; ++failure) {
        probe.count = failure ? 0 : 1;
        probe.rects = failure ? nullptr : &rectangle;
        probe.flags = failure ? D3DCLEAR_ZBUFFER : D3DCLEAR_TARGET;
        probe.color = 0xff314159;
        probe.depth = .75f;
        probe.stencil = 123;
        probe.calls = 0;
        probe.arguments = false;
        probe.armed = true;
        incoming.restore();
        const HRESULT hr = device->Clear(probe.count, probe.rects, probe.flags, probe.color, probe.depth,
                                         probe.stencil);
        ClearCpuState actual;
        actual.capture();
        saved.restore();
        probe.armed = false;
        const bool entry = probe.calls == 1 && incoming.same(probe.observed);
        const bool exit = actual.same(probe.outgoing);
        expect("Clear native incoming x87 MXCSR LastError", entry);
        expect("Clear native outgoing x87 MXCSR LastError", exit);
        expect("Clear exact native arguments", probe.arguments);
        expect("Clear exact native HRESULT", hr == probe.result && bool(FAILED(hr)) == bool(failure));
        std::printf("CPU_CLEAR case=%s incoming=%u outgoing=%u args=%u result=%08lx\n", failure ? "failure" : "success",
                    entry, exit, probe.arguments, hr);
    }
    saved.restore();
}
struct Vertex {
    float x, y, z, rhw;
    DWORD color;
};
static constexpr DWORD fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
static IDirect3DVertexBuffer9* vertices(IDirect3DDevice9* d, float z) {
    IDirect3DVertexBuffer9* buffer = nullptr;
    // Prefix deliberately exercises a nonzero stream offset.
    if (!check("create VB", d->CreateVertexBuffer(16 + 3 * sizeof(Vertex), 0, fvf, D3DPOOL_MANAGED, &buffer, nullptr)))
        return nullptr;
    const Vertex data[] = {{10, 10, z, 1, 0xffffffff}, {140, 10, z, 1, 0xffffffff}, {10, 100, z, 1, 0xffffffff}};
    void* bytes = nullptr;
    if (check("VB lock", buffer->Lock(16, sizeof data, &bytes, 0))) {
        std::memcpy(bytes, data, sizeof data);
        check("VB unlock", buffer->Unlock());
    }
    return buffer;
}
static void typed(IDirect3DDevice9* d, bool populated) {
    const int nonzero[] = {7, -2, 2147483647, -2147483647};
    const int zero[] = {0, 0, 0, 0};
    const BOOL flags[] = {TRUE, FALSE, TRUE}, clear[] = {FALSE, FALSE, FALSE};
    check("VS int", d->SetVertexShaderConstantI(0, populated ? nonzero : zero, 1));
    check("PS int", d->SetPixelShaderConstantI(0, populated ? nonzero : zero, 1));
    check("VS bool", d->SetVertexShaderConstantB(0, populated ? flags : clear, 3));
    check("PS bool", d->SetPixelShaderConstantB(0, populated ? flags : clear, 3));
    const float negative_zero[] = {-0.0f, 0.0f, 0.0f, 0.0f};
    check("VS signed-zero float", d->SetVertexShaderConstantF(10, negative_zero, 1));
}
static void frame(IDirect3DDevice9* d, bool invalid = false) {
    check("BeginScene", d->BeginScene());
    check("indexed draw", d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1));
    if (invalid) {
        IDirect3DIndexBuffer9* bound = nullptr;
        check("save index binding", d->GetIndices(&bound));
        check("remove required index buffer", d->SetIndices(nullptr));
        expect("missing-index draw forwarded", FAILED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1)));
        check("restore index binding", d->SetIndices(bound));
        if (bound) bound->Release();
    }
    check("EndScene", d->EndScene());
    check("Present", d->Present(nullptr, nullptr, nullptr, nullptr));
}
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    HMODULE module = LoadLibraryA("d3d9.dll");
    if (!module) return 2;
    // Load the app-local DLL first, but delay its factory entry until the
    // backend witness is installed. Otherwise basename lookup could select the
    // already-loaded system DLL and accidentally bypass the proxy under test.
    NativeClearProbe clear_probe;
    if (!clear_probe.install()) {
        expect("install native Clear witness", false);
        return 2;
    }
    FARPROC address = GetProcAddress(module, "Direct3DCreate9");
    IDirect3D9*(WINAPI * create)(UINT) = nullptr;
    std::memcpy(&create, &address, sizeof create);
    IDirect3D9* api = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!api) return 2;
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "X3CaptureStateFixture";
    RegisterClassA(&wc);
    HWND window = CreateWindowA(wc.lpszClassName, "X3 capture-state verification", WS_OVERLAPPEDWINDOW, 80, 80, 180,
                                160, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    for (unsigned iteration = 0; iteration < 2; ++iteration) {
        std::printf("DEVICE %u pure=%u\n", iteration + 1, iteration);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.BackBufferWidth = 160;
        pp.BackBufferHeight = 120;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        IDirect3DDevice9* d = nullptr;
        DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE |
                      (iteration ? D3DCREATE_PUREDEVICE : 0);
        if (!check("CreateDevice", api->CreateDevice(0, D3DDEVTYPE_HAL, window, flags, &pp, &d))) break;
        IDirect3DVertexBuffer9* vb = vertices(d, .5f);
        IDirect3DIndexBuffer9* ib = nullptr;
        check("create IB", d->CreateIndexBuffer(6, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, nullptr));
        if (ib) {
            void* data = nullptr;
            const WORD idx[] = {0, 1, 2};
            if (check("IB lock", ib->Lock(0, 6, &data, 0))) {
                std::memcpy(data, idx, 6);
                check("IB unlock", ib->Unlock());
            }
        }
        IDirect3DTexture9* texture = nullptr;
        check("create sample texture",
              d->CreateTexture(8, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr));
        IDirect3DTexture9* target = nullptr;
        IDirect3DSurface9* surface = nullptr;
        check("create target texture",
              d->CreateTexture(160, 120, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &target, nullptr));
        if (target) check("target surface", target->GetSurfaceLevel(0, &surface));
        IDirect3DSurface9* backbuffer = nullptr;
        check("backbuffer", d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer));
        check("set target", d->SetRenderTarget(0, surface));
        check("set stream", d->SetStreamSource(0, vb, 16, sizeof(Vertex)));
        check("set indices", d->SetIndices(ib));
        check("set texture", d->SetTexture(0, texture));
        check("set FVF", d->SetFVF(fvf));
        check("lighting off", d->SetRenderState(D3DRS_LIGHTING, FALSE));
        check("cull off", d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
        typed(d, true);
        IDirect3DStateBlock9* state = nullptr;
        check("stateblock", d->CreateStateBlock(D3DSBT_ALL, &state));
        frame(d);                           // frame 0 arms automatic capture for frame 1.
        clear_cpu_boundary(d, clear_probe); // Logged inside frame 1, without extra draws/Present.
        frame(d);                           // frame 1: initial bindings/values.
        typed(d, false);
        d->SetStreamSource(0, nullptr, 0, 0);
        d->SetIndices(nullptr);
        d->SetTexture(0, nullptr);
        if (state) check("restore stateblock", state->Apply());
        frame(d); // frame 2: same allocation IDs and nonzero constants via Apply.
        typed(d, false);
        frame(d, true); // frame 3: explicit zeros, plus one failed draw.
        if (state) state->Release();
        check("unbind old VB", d->SetStreamSource(0, nullptr, 0, 0));
        if (vb) expect("old VB released", vb->Release() == 0);
        vb = vertices(d, .7f);
        check("set recreated VB", d->SetStreamSource(0, vb, 16, sizeof(Vertex)));
        frame(d); // frame 4 must receive a new resource ID, even if address reused.
        // UP data must not inherit the still-bound VB/IB identities.
        const Vertex up[] = {
            {10, 10, .5f, 1, 0xffffffff}, {140, 10, .5f, 1, 0xffffffff}, {10, 100, .5f, 1, 0xffffffff}};
        const WORD up_indices[] = {0, 1, 2};
        check("UP BeginScene", d->BeginScene());
        check("UP draw",
              d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 3, 1, up_indices, D3DFMT_INDEX16, up, sizeof(Vertex)));
        check("UP EndScene", d->EndScene());
        check("UP Present", d->Present(nullptr, nullptr, nullptr, nullptr));
        d->SetStreamSource(0, nullptr, 0, 0);
        d->SetIndices(nullptr);
        d->SetTexture(0, nullptr);
        d->SetRenderTarget(0, backbuffer);
        if (vb) expect("new VB released", vb->Release() == 0);
        if (ib) expect("IB released", ib->Release() == 0);
        if (texture) expect("sample texture released", texture->Release() == 0);
        if (surface) surface->Release();
        if (target) expect("target released", target->Release() == 0);
        if (backbuffer) backbuffer->Release();
        check("Reset after capture", d->Reset(&pp));
        expect("device released", d->Release() == 0);
    }
    clear_probe.restore_factory(); // Restore shared backend slot before factory teardown.
    expect("API released", api->Release() == 0);
    DestroyWindow(window);
    std::printf("CAPTURE STATE RESULT: %u failures\n", failures);
    return failures ? 1 : 0;
}
