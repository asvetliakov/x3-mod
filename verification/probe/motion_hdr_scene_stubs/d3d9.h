#pragma once
// Host-only COM seam for the MotionOutput handoff control-flow fixture. This
// does not model Windows ABI, rendering or device restoration correctness.
#include <cstdint>
using UINT = unsigned;
using ULONG = unsigned long;
using DWORD = std::uint32_t;
using INT = int;
using HRESULT = std::int32_t;
constexpr HRESULT S_OK = 0, S_FALSE = 1, E_FAIL = -1, E_ABORT = -2,
    E_NOINTERFACE = -3, D3DERR_DEVICELOST = -4, D3DERR_DEVICENOTRESET = -5,
    D3DERR_NOTAVAILABLE = -6, D3DERR_INVALIDCALL = -7, D3DERR_NOTFOUND = -8;
constexpr bool SUCCEEDED(HRESULT hr) { return hr >= 0; }
constexpr bool FAILED(HRESULT hr) { return hr < 0; }
enum D3DFORMAT { D3DFMT_UNKNOWN, D3DFMT_A16B16G16R16F, D3DFMT_A8R8G8B8, D3DFMT_G32R32F, D3DFMT_A32B32G32R32F, D3DFMT_R32F };
enum D3DPRIMITIVETYPE { D3DPT_TRIANGLELIST };
enum D3DTEXTUREFILTERTYPE { D3DTEXF_POINT };
enum D3DRENDERSTATETYPE { D3DRS_ZENABLE };
enum D3DCULL { D3DCULL_NONE = 1, D3DCULL_CW = 2, D3DCULL_CCW = 3 }; // documented D3D9 values; the replay records default to no culling
enum D3DSAMPLERSTATETYPE { D3DSAMP_MIPMAPLODBIAS };
struct D3DCAPS9 {};
struct D3DVIEWPORT9 { UINT X, Y, Width, Height; float MinZ, MaxZ; };
struct RECT {};
struct IUnknown {};
struct IDirect3DResource9 {};
struct IDirect3DBaseTexture9 {};
struct IDirect3DPixelShader9 {};
struct IDirect3DVertexShader9 {};
struct IDirect3DVertexBuffer9 {};
struct IDirect3DIndexBuffer9 {};
struct IDirect3DVertexDeclaration9 {};
struct IDirect3DDevice9 {};
struct IDirect3DStateBlock9 {};
struct IDirect3DQuery9 {};
constexpr int IID_IDirect3DTexture9 = 1;
struct IDirect3DTexture9 {
    unsigned refs = 1, adds = 0, releases = 0;
    void AddRef() { ++refs; ++adds; }
    void Release() { --refs; ++releases; }
};
struct IDirect3DSurface9 {
    IDirect3DTexture9* texture = nullptr;
    HRESULT container_hr = S_OK;
    bool failure_output = false;
    unsigned refs = 1, adds = 0, releases = 0, containers = 0;
    unsigned identity = 1, width = 17, height = 11;
    void AddRef() { ++refs; ++adds; }
    void Release() { --refs; ++releases; }
    HRESULT GetContainer(int, void** out) {
        ++containers; *out = nullptr;
        if ((SUCCEEDED(container_hr) || failure_output) && texture) { texture->AddRef(); *out = texture; }
        return container_hr;
    }
};
