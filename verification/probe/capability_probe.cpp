// Disposable runtime probe: creates its own small window and no game resources.
// Successful API creation is capability evidence, not proof of physical HDR output.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <initializer_list>
#include <cstring>
template <typename T> static T symbol(HMODULE lib, const char* name) {
    FARPROC address = GetProcAddress(lib, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    return result;
}

static void hr(const char* name, HRESULT result) {
    std::printf("%s: 0x%08lx %s\n", name, (unsigned long)result, SUCCEEDED(result) ? "OK" : "FAILED");
    std::fflush(stdout);
}
static void module(const char* name) {
    char path[MAX_PATH] = {};
    HMODULE mod = GetModuleHandleA(name);
    if (mod) GetModuleFileNameA(mod, path, MAX_PATH);
    std::printf("module %s: %s\n", name, mod ? path : "not loaded");
}
// Public D3D9 feasibility only: no device/window, shaders, draws or private APIs.
// A completed probe can report eligible=0; that is a measured refusal, not a
// failed probe or evidence that the cutout route has been GPU-qualified.
static bool alpha_test_caps(IDirect3D9* api) {
    D3DADAPTER_IDENTIFIER9 id{};
    D3DCAPS9 caps{};
    D3DDISPLAYMODE mode{};
    const HRESULT identity = api->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    const HRESULT capability = api->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
    const HRESULT display = api->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);
    std::printf(
        "ALPHA_TEST_ADAPTER adapter=%u type=%u identity=%08lx caps=%08lx display=%08lx vendor=%08lx device=%08lx adapter_format=%u\n",
        unsigned(D3DADAPTER_DEFAULT), unsigned(D3DDEVTYPE_HAL), static_cast<unsigned long>(identity),
        static_cast<unsigned long>(capability), static_cast<unsigned long>(display),
        static_cast<unsigned long>(id.VendorId), static_cast<unsigned long>(id.DeviceId), unsigned(mode.Format));
    if (FAILED(identity) || FAILED(capability) || FAILED(display)) {
        std::puts("ALPHA_TEST_RESULT complete=0 eligible=0 formats=0 queries=0");
        return false;
    }
    constexpr DWORD required_misc = D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING | D3DPMISCCAPS_INDEPENDENTWRITEMASKS |
                                    D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS;
    const bool caps_ok = caps.NumSimultaneousRTs >= 3 && (caps.PrimitiveMiscCaps & required_misc) == required_misc &&
                         (caps.AlphaCmpCaps & D3DPCMPCAPS_GREATEREQUAL) && caps.MaxVertexShaderConst >= 256 &&
                         D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion) >= 3 &&
                         D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion) >= 3;
    std::printf(
        "ALPHA_TEST_CAPS mrt=%lu misc=%08lx required_misc=%08lx alpha_compare=%08lx required_compare=%08lx vs=%08lx ps=%08lx vertex_constants=%lu caps_ok=%u\n",
        static_cast<unsigned long>(caps.NumSimultaneousRTs), static_cast<unsigned long>(caps.PrimitiveMiscCaps),
        static_cast<unsigned long>(required_misc), static_cast<unsigned long>(caps.AlphaCmpCaps),
        static_cast<unsigned long>(D3DPCMPCAPS_GREATEREQUAL), static_cast<unsigned long>(caps.VertexShaderVersion),
        static_cast<unsigned long>(caps.PixelShaderVersion), static_cast<unsigned long>(caps.MaxVertexShaderConst),
        unsigned(caps_ok));
    bool formats_ok = true;
    for (const auto format : {D3DFMT_A16B16G16R16F, D3DFMT_A32B32G32R32F, D3DFMT_R32F}) {
        const HRESULT rt = api->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format,
                                                  D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, format);
        const HRESULT post = api->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format,
                                                    D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
                                                    D3DRTYPE_TEXTURE, format);
        const bool supported = SUCCEEDED(rt) && SUCCEEDED(post);
        formats_ok = formats_ok && supported;
        std::printf("ALPHA_TEST_FORMAT format=%u rt_usage=%08lx post_usage=%08lx rt=%08lx post=%08lx supported=%u\n",
                    unsigned(format), static_cast<unsigned long>(D3DUSAGE_RENDERTARGET),
                    static_cast<unsigned long>(D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING),
                    static_cast<unsigned long>(rt), static_cast<unsigned long>(post), unsigned(supported));
    }
    std::printf("ALPHA_TEST_RESULT complete=1 eligible=%u formats=3 queries=6\n", unsigned(caps_ok && formats_ok));
    return true;
}
static int alpha_test_caps_mode() {
    HMODULE lib = LoadLibraryA("d3d9.dll");
    if (!lib) {
        std::puts("ALPHA_TEST_RESULT complete=0 eligible=0 reason=runtime");
        return 1;
    }
    auto create = symbol<IDirect3D9*(WINAPI*)(UINT)>(lib, "Direct3DCreate9");
    IDirect3D9* api = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!api) {
        std::puts("ALPHA_TEST_RESULT complete=0 eligible=0 reason=factory");
        FreeLibrary(lib);
        return 1;
    }
    module("d3d9.dll");
    const bool complete = alpha_test_caps(api);
    api->Release();
    FreeLibrary(lib);
    return complete ? 0 : 1;
}
static void d3d9(HWND window) {
    HMODULE lib = LoadLibraryA("d3d9.dll");
    auto create = symbol<IDirect3D9*(WINAPI*)(UINT)>(lib, "Direct3DCreate9");
    if (!create) {
        std::puts("Direct3DCreate9 unavailable");
        return;
    }
    IDirect3D9* api = create(D3D_SDK_VERSION);
    if (!api) {
        std::puts("Direct3DCreate9 returned null");
        return;
    }
    D3DADAPTER_IDENTIFIER9 id = {};
    hr("D3D9 GetAdapterIdentifier", api->GetAdapterIdentifier(0, 0, &id));
    std::printf("D3D9 adapter: %s; driver %s; vendor=0x%x device=0x%x\n", id.Description, id.Driver,
                unsigned(id.VendorId), unsigned(id.DeviceId));
    D3DCAPS9 caps = {};
    hr("D3D9 GetDeviceCaps", api->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps));
    std::printf("D3D9 shader versions: VS=%lx PS=%lx; MRT=%lu; maxTexture=%lux%lu\n",
                (unsigned long)caps.VertexShaderVersion, (unsigned long)caps.PixelShaderVersion,
                (unsigned long)caps.NumSimultaneousRTs, (unsigned long)caps.MaxTextureWidth,
                (unsigned long)caps.MaxTextureHeight);
    D3DDISPLAYMODE mode = {};
    hr("D3D9 GetAdapterDisplayMode", api->GetAdapterDisplayMode(0, &mode));
    for (auto usage :
         {DWORD(D3DUSAGE_RENDERTARGET), DWORD(D3DUSAGE_QUERY_FILTER), DWORD(D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING)}) {
        char label[80];
        std::snprintf(label, sizeof label, "D3D9 RGBA16F usage=0x%lx", (unsigned long)usage);
        hr(label,
           api->CheckDeviceFormat(0, D3DDEVTYPE_HAL, mode.Format, usage, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F));
    }
    hr("D3D9 INTZ depth texture", api->CheckDeviceFormat(0, D3DDEVTYPE_HAL, mode.Format, D3DUSAGE_DEPTHSTENCIL,
                                                         D3DRTYPE_TEXTURE, D3DFORMAT(MAKEFOURCC('I', 'N', 'T', 'Z'))));
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 320;
    pp.BackBufferHeight = 200;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device = nullptr;
    hr("D3D9 CreateDevice",
       api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device));
    if (device) {
        IDirect3DTexture9* fp16 = nullptr;
        hr("D3D9 create FP16 RT texture", device->CreateTexture(320, 200, 1, D3DUSAGE_RENDERTARGET,
                                                                D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &fp16, nullptr));
        if (fp16) fp16->Release();
        hr("D3D9 Clear", device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff102030, 1, 0));
        hr("D3D9 Present", device->Present(nullptr, nullptr, nullptr, nullptr));
        device->Release();
    }
    api->Release();
}
static void dx11(HWND window) {
    HMODULE lib = LoadLibraryA("d3d11.dll");
    using Create = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    auto create = symbol<Create>(lib, "D3D11CreateDevice");
    if (!create) {
        std::puts("D3D11CreateDevice unavailable");
        return;
    }
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level = {};
    hr("D3D11CreateDevice",
       create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context));
    if (!device) return;
    std::printf("D3D11 feature level: 0x%x\n", unsigned(level));
    UINT support = 0;
    hr("D3D11 RGBA16F CheckFormatSupport", device->CheckFormatSupport(DXGI_FORMAT_R16G16B16A16_FLOAT, &support));
    std::printf("D3D11 RGBA16F support: 0x%x (RT=%d blend=%d shaderSample=%d)\n", support,
                !!(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET), !!(support & D3D11_FORMAT_SUPPORT_BLENDABLE),
                !!(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE));
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    hr("D3D11 QI IDXGIDevice", device->QueryInterface(IID_PPV_ARGS(&dxgi)));
    if (dxgi) hr("DXGI GetAdapter", dxgi->GetAdapter(&adapter));
    if (adapter) {
        DXGI_ADAPTER_DESC desc = {};
        adapter->GetDesc(&desc);
        std::printf("DXGI adapter: %ls; vendor=0x%x device=0x%x\n", desc.Description, desc.VendorId, desc.DeviceId);
        hr("DXGI factory2", adapter->GetParent(IID_PPV_ARGS(&factory)));
        IDXGIOutput* output = nullptr;
        for (UINT i = 0; adapter->EnumOutputs(i, &output) == S_OK; ++i) {
            DXGI_OUTPUT_DESC desc = {};
            output->GetDesc(&desc);
            std::printf("DXGI output %u: %ls attached=%d\n", i, desc.DeviceName, desc.AttachedToDesktop);
            IDXGIOutput6* six = nullptr;
            hr("DXGI QI Output6", output->QueryInterface(IID_PPV_ARGS(&six)));
            if (six) {
                DXGI_OUTPUT_DESC1 d = {};
                hr("DXGI Output6 GetDesc1", six->GetDesc1(&d));
                std::printf("output bits=%u colorSpace=%u minLum=%f maxLum=%f fullFrameLum=%f\n", d.BitsPerColor,
                            unsigned(d.ColorSpace), d.MinLuminance, d.MaxLuminance, d.MaxFullFrameLuminance);
                six->Release();
            }
            output->Release();
        }
    }
    if (factory) {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = 320;
        desc.Height = 200;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        IDXGISwapChain1* chain = nullptr;
        hr("DXGI FP16 flip-discard CreateSwapChainForHwnd",
           factory->CreateSwapChainForHwnd(device, window, &desc, nullptr, nullptr, &chain));
        if (chain) {
            IDXGISwapChain3* three = nullptr;
            hr("DXGI QI SwapChain3", chain->QueryInterface(IID_PPV_ARGS(&three)));
            if (three) {
                UINT flags = 0;
                hr("DXGI scRGB CheckColorSpaceSupport",
                   three->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &flags));
                std::printf("scRGB support flags: 0x%x\n", flags);
                hr("DXGI scRGB SetColorSpace1", three->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709));
                three->Release();
            }
            hr("DXGI FP16 Present", chain->Present(0, 0));
            chain->Release();
        }
        factory->Release();
    }
    if (adapter) adapter->Release();
    if (dxgi) dxgi->Release();
    context->Release();
    device->Release();
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 2 && !std::strcmp(argv[1], "--alpha-test-caps")) return alpha_test_caps_mode();
    if (argc != 1) {
        std::fputs("usage: capability_probe.exe [--alpha-test-caps]\n", stderr);
        return 2;
    }
    std::puts("X3 modern renderer capability probe (32-bit); physical HDR not validated");
    WNDCLASSA cls = {};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "X3CapabilityProbe";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "X3 disposable graphics capability probe", WS_OVERLAPPEDWINDOW, 80,
                                80, 340, 240, nullptr, nullptr, cls.hInstance, nullptr);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    d3d9(window);
    dx11(window);
    for (const char* name : {"d3d9.dll", "d3d11.dll", "dxgi.dll", "wined3d.dll", "d3dshared.dll", "winevulkan.dll"})
        module(name);
    DestroyWindow(window);
    return 0;
}
