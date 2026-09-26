// Bounded lifetime-only experiment: no rendering, game launch, or settings edits.
// Returned COM refcounts are diagnostic observations of this backend, not a
// portable production algorithm for deciding application ownership.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

static unsigned failures;
static void expect(const char* name, bool pass) {
    std::printf("%s: %s\n", name, pass ? "PASS" : "FAIL");
    failures += !pass;
}
static bool check(const char* name, HRESULT hr) {
    std::printf("%s: 0x%08lx\n", name, (unsigned long)hr);
    expect(name, SUCCEEDED(hr));
    return SUCCEEDED(hr);
}
static const GUID ownerGuid = {0xa691bb13, 0xcb4e, 0x4d7d, {0xb2, 0x19, 0x95, 0x33, 0x21, 0xd9, 0x8f, 0x02}};
static bool ownerDestroyed;
struct Owner final : IUnknown {
    ULONG refs = 1;
    IDirect3DTexture9* texture;
    explicit Owner(IDirect3DTexture9* value)
        : texture(value) {
        texture->AddRef();
    }
    HRESULT WINAPI QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown) return E_NOINTERFACE;
        *out = this;
        AddRef();
        return S_OK;
    }
    ULONG WINAPI AddRef() override { return ++refs; }
    ULONG WINAPI Release() override {
        const ULONG remaining = --refs;
        if (!remaining) {
            ownerDestroyed = true;
            texture->Release();
            delete this;
        }
        return remaining;
    }
};

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    HMODULE dll = LoadLibraryA("d3d9.dll");
    FARPROC address = dll ? GetProcAddress(dll, "Direct3DCreate9") : nullptr;
    IDirect3D9*(WINAPI * create)(UINT) = nullptr;
    std::memcpy(&create, &address, sizeof(create));
    if (!create) return 1;
    IDirect3D9* api = create(D3D_SDK_VERSION);
    if (!api) return 1;
    WNDCLASSA cls = {};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "X3ResourceLifetimeProbe";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "X3 lifetime probe", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                                nullptr, cls.hInstance, nullptr);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    for (unsigned kind = 0; kind < 2; ++kind) {
        std::printf("CASE %s\n", kind ? "bound_surface_private_owner" : "texture_retains_device");
        IDirect3DDevice9* device = nullptr;
        if (!check("CreateDevice",
                   api->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                     D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE, &pp, &device)))
            break;
        IDirect3DTexture9* texture = nullptr;
        if (!check("CreateTexture FP16", device->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                                                               D3DPOOL_DEFAULT, &texture, nullptr))) {
            device->Release();
            break;
        }
        // In the first case texture is our recovery owner. In the second, the
        // private-data Owner becomes its sole owner and this local is borrowed.
        if (kind) {
            IDirect3DSurface9* depth = nullptr;
            if (!check("CreateDepthStencilSurface D24X8",
                       device->CreateDepthStencilSurface(64, 64, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &depth,
                                                         nullptr))) {
                texture->Release();
                device->Release();
                break;
            }
            Owner* owner = new Owner(texture);
            ownerDestroyed = false;
            check("SetPrivateData owner", depth->SetPrivateData(ownerGuid, owner, sizeof(IUnknown*), D3DSPD_IUNKNOWN));
            owner->Release();
            check("SetDepthStencilSurface", device->SetDepthStencilSurface(depth));
            const ULONG surfaceRefs = depth->Release();
            std::printf("source surface Release returns %lu; private owner destroyed=%u\n", (unsigned long)surfaceRefs,
                        ownerDestroyed);
            expect("private owner survives release of bound source", !ownerDestroyed);
            texture->Release();        // Only Owner now holds the history texture.
            if (ownerDestroyed) break; // Do not use a dangling borrowed pointer.
        }
        const ULONG remaining = device->Release();
        std::printf("application device Release returns %lu\n", (unsigned long)remaining);
        expect("texture prevents device Release reaching zero", remaining != 0);
        // Recover through the live texture, never through the released device
        // pointer. In case 2 its sole owning reference is the verified-live
        // private-data Owner. This deterministic fixture has no concurrent user.
        IDirect3DDevice9* recovered = nullptr;
        if (!check("Recovery GetDevice", texture->GetDevice(&recovered))) break;
        expect("GetDevice preserves native identity", recovered == device);
        IUnknown* identity = nullptr;
        if (check("QueryInterface IUnknown", recovered->QueryInterface(IID_PPV_ARGS(&identity)))) {
            expect("IUnknown preserves native identity", identity == recovered);
            identity->Release();
        }
        if (kind) {
            expect("owner still alive after application device release", !ownerDestroyed);
            check("Unbind source to break anchor retention", recovered->SetDepthStencilSurface(nullptr));
        }
        if (!kind) texture->Release();
        const ULONG finalRefs = recovered->Release();
        std::printf("recovered device final Release returns %lu\n", (unsigned long)finalRefs);
        expect("device reaches zero after owned resources release", finalRefs == 0);
        if (kind) expect("private owner destroyed on cleanup", ownerDestroyed);
    }
    expect("factory reaches zero", api->Release() == 0);
    DestroyWindow(window);
    std::printf("LIFETIME RESULT: %u failures\n", failures);
    return failures ? 1 : 0;
}
