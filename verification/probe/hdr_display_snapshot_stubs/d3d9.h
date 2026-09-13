#pragma once
// Host-only declarations for HDR control-flow/ownership tests, not a runtime
// or ABI substitute. Production separately compiles against real D3D headers.
#include <cstdint>
using UINT = unsigned;
using DWORD = std::uint32_t;
using HRESULT = std::int32_t;
constexpr HRESULT S_OK = 0, S_FALSE = 1, E_FAIL = -1, E_ABORT = -2,
    E_NOINTERFACE = -3, D3DERR_DEVICELOST = -4, D3DERR_DEVICENOTRESET = -5,
    D3DERR_NOTAVAILABLE = -6, E_INVALIDARG = -7;
constexpr bool SUCCEEDED(HRESULT hr) { return hr >= 0; }
constexpr bool FAILED(HRESULT hr) { return hr < 0; }
enum D3DFORMAT { D3DFMT_UNKNOWN, D3DFMT_A16B16G16R16F, D3DFMT_A8R8G8B8 };
enum D3DTEXTUREFILTERTYPE { D3DTEXF_POINT };
enum D3DRESOURCETYPE { D3DRTYPE_SURFACE, D3DRTYPE_TEXTURE };
enum D3DPOOL { D3DPOOL_DEFAULT, D3DPOOL_SYSTEMMEM };
enum D3DMULTISAMPLE_TYPE { D3DMULTISAMPLE_NONE, D3DMULTISAMPLE_2_SAMPLES };
constexpr DWORD D3DUSAGE_RENDERTARGET = 1, D3DUSAGE_DYNAMIC = 2;
struct D3DSURFACE_DESC {
    D3DFORMAT Format = D3DFMT_A16B16G16R16F;
    D3DRESOURCETYPE Type = D3DRTYPE_SURFACE;
    DWORD Usage = D3DUSAGE_RENDERTARGET;
    D3DPOOL Pool = D3DPOOL_DEFAULT;
    D3DMULTISAMPLE_TYPE MultiSampleType = D3DMULTISAMPLE_NONE;
    DWORD MultiSampleQuality = 0;
    UINT Width = 17, Height = 11;
};
struct D3DCAPS9 {};
struct RECT {};
struct Ref {
    unsigned refs = 1, adds = 0, releases = 0;
    void AddRef() { ++refs; ++adds; }
    void Release() { --refs; ++releases; }
};
struct IDirect3DPixelShader9 : Ref {};
struct IDirect3DVertexShader9 : Ref {};
struct IDirect3DVertexDeclaration9 : Ref {};
constexpr int IID_IUnknown = 0, IID_IDirect3DTexture9 = 1;
struct IUnknown : Ref {
    IUnknown* identity = nullptr; // null: this object is canonical
    HRESULT qi_hr = S_OK;
    unsigned qi_calls = 0, qi_fault_at = 0;
    bool qi_null = false, qi_output_on_failure = false;
    void AddRef() { if (identity) identity->AddRef(); else Ref::AddRef(); }
    void Release() { if (identity) identity->Release(); else Ref::Release(); }
    HRESULT QueryInterface(int, void** out) {
        ++qi_calls; *out = nullptr;
        const bool fault = !qi_fault_at || qi_calls == qi_fault_at;
        const HRESULT hr = fault ? qi_hr : S_OK;
        if (!(fault && qi_null) && (SUCCEEDED(hr) || (fault && qi_output_on_failure))) {
            auto* canonical = identity ? identity : this;
            canonical->AddRef(); *out = canonical;
        }
        return hr;
    }
};
struct IDirect3DDevice9 : IUnknown {};
struct IDirect3DSurface9;
struct IDirect3DTexture9 : IUnknown {
    IDirect3DDevice9* device = nullptr;
    IDirect3DSurface9* level = nullptr;
    D3DSURFACE_DESC desc{};
    UINT levels = 1;
    HRESULT desc_hr = S_OK, device_hr = S_OK, level_hr = S_OK;
    bool output_on_failure = false;
    UINT GetLevelCount() { return levels; }
    HRESULT GetLevelDesc(UINT, D3DSURFACE_DESC* out) { *out = desc; return desc_hr; }
    HRESULT GetDevice(IDirect3DDevice9** out) {
        *out = nullptr;
        if (device && (SUCCEEDED(device_hr) || output_on_failure)) { device->AddRef(); *out = device; }
        return device_hr;
    }
    HRESULT GetSurfaceLevel(UINT, IDirect3DSurface9** out);
};
struct IDirect3DSurface9 : IUnknown {
    explicit IDirect3DSurface9(IDirect3DTexture9* source = nullptr) : texture(source) {}
    IDirect3DTexture9* texture = nullptr;
    HRESULT container_hr = S_OK;
    IDirect3DDevice9* device = nullptr;
    D3DSURFACE_DESC desc{};
    HRESULT desc_hr = S_OK, device_hr = S_OK;
    bool output_on_failure = false;
    HRESULT GetDesc(D3DSURFACE_DESC* out) { *out = desc; return desc_hr; }
    HRESULT GetDevice(IDirect3DDevice9** out) {
        *out = nullptr;
        if (device && (SUCCEEDED(device_hr) || output_on_failure)) { device->AddRef(); *out = device; }
        return device_hr;
    }
    HRESULT GetContainer(int, void** out) {
        *out = nullptr;
        if (texture && (SUCCEEDED(container_hr) || output_on_failure)) { texture->AddRef(); *out = texture; }
        return container_hr;
    }
};
inline HRESULT IDirect3DTexture9::GetSurfaceLevel(UINT, IDirect3DSurface9** out) {
    *out = nullptr;
    if (level && (SUCCEEDED(level_hr) || output_on_failure)) { level->AddRef(); *out = level; }
    return level_hr;
}
