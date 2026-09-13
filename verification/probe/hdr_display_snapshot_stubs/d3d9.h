#pragma once
// Host-only declarations for the write_back control-flow fixture. These are
// deliberately not a D3D runtime or ABI substitute; production uses MinGW's
// real Windows SDK headers and is separately cross-compiled.
#include <cstdint>
using UINT = unsigned;
using HRESULT = std::int32_t;
constexpr HRESULT S_OK = 0, S_FALSE = 1, E_FAIL = -1, E_ABORT = -2,
    E_NOINTERFACE = -3, D3DERR_DEVICELOST = -4, D3DERR_DEVICENOTRESET = -5,
    D3DERR_NOTAVAILABLE = -6;
constexpr bool SUCCEEDED(HRESULT hr) { return hr >= 0; }
constexpr bool FAILED(HRESULT hr) { return hr < 0; }
enum D3DFORMAT { D3DFMT_UNKNOWN };
enum D3DTEXTUREFILTERTYPE { D3DTEXF_POINT };
struct D3DCAPS9 {};
struct RECT {};
struct IDirect3DPixelShader9 {};
struct IDirect3DVertexShader9 {};
struct IDirect3DVertexDeclaration9 {};
struct IDirect3DDevice9 {};
constexpr int IID_IDirect3DTexture9 = 1;
struct IDirect3DTexture9 {
    unsigned refs = 1, adds = 0, releases = 0;
    void AddRef() { ++refs; ++adds; }
    void Release() { --refs; ++releases; }
};
struct IDirect3DSurface9 {
    IDirect3DTexture9* texture = nullptr;
    HRESULT container_hr = S_OK;
    HRESULT GetContainer(int, void** out) {
        *out = nullptr;
        if (SUCCEEDED(container_hr) && texture) { texture->AddRef(); *out = texture; }
        return container_hr;
    }
};
