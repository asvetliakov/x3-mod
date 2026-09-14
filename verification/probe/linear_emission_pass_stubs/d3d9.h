#pragma once
// HOST ONLY: scripted public interfaces, not D3D ABI or GPU qualification.
#include <cassert>
#include <cstdint>
#define WINAPI
using UINT = unsigned;
using DWORD = std::uint32_t;
using LONG = std::int32_t;
using HRESULT = std::int32_t;
using HANDLE = void *;
using D3DCOLOR = DWORD;
constexpr HRESULT S_OK = 0, S_FALSE = 1, E_FAIL = -1, E_NOINTERFACE = -2,
                  E_INVALIDARG = -3, E_OUTOFMEMORY = -4,
                  D3DERR_NOTAVAILABLE = -5, D3DERR_INVALIDCALL = -6,
                  D3DERR_NOTFOUND = -7;
constexpr bool SUCCEEDED(HRESULT h) { return h >= 0; }
constexpr bool FAILED(HRESULT h) { return h < 0; }
constexpr DWORD FALSE = 0, TRUE = 1;
enum D3DFORMAT {
  D3DFMT_UNKNOWN,
  D3DFMT_A16B16G16R16F,
  D3DFMT_A8R8G8B8,
  D3DFMT_D24S8
};
enum D3DPOOL { D3DPOOL_DEFAULT };
enum D3DRESOURCETYPE { D3DRTYPE_SURFACE, D3DRTYPE_TEXTURE };
enum D3DMULTISAMPLE_TYPE { D3DMULTISAMPLE_NONE };
enum D3DBACKBUFFER_TYPE { D3DBACKBUFFER_TYPE_MONO };
enum D3DPRIMITIVETYPE { D3DPT_TRIANGLESTRIP };
enum D3DDEVTYPE { D3DDEVTYPE_HAL };
enum D3DRENDERSTATETYPE {
  D3DRS_ZENABLE,
  D3DRS_ZWRITEENABLE,
  D3DRS_ALPHATESTENABLE,
  D3DRS_ALPHABLENDENABLE,
  D3DRS_SEPARATEALPHABLENDENABLE,
  D3DRS_CULLMODE,
  D3DRS_FILLMODE,
  D3DRS_COLORWRITEENABLE,
  D3DRS_SCISSORTESTENABLE,
  D3DRS_STENCILENABLE,
  D3DRS_STENCILWRITEMASK,
  D3DRS_FOGENABLE,
  D3DRS_SRGBWRITEENABLE,
  D3DRS_CLIPPLANEENABLE,
  D3DRS_DITHERENABLE,
  D3DRS_WRAP0,
  D3DRS_MULTISAMPLEMASK,
  D3DRS_CLIPPING,
  D3DRS_SRCBLEND,
  D3DRS_DESTBLEND,
  D3DRS_BLENDOP,
  D3DRS_COLORWRITEENABLE1,
  D3DRS_COLORWRITEENABLE2,
  D3DRS_COLORWRITEENABLE3,
  D3DRS_BLENDOPALPHA, D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA
};
enum D3DSAMPLERSTATETYPE {
  D3DSAMP_MINFILTER,
  D3DSAMP_MAGFILTER,
  D3DSAMP_MIPFILTER,
  D3DSAMP_ADDRESSU,
  D3DSAMP_ADDRESSV,
  D3DSAMP_SRGBTEXTURE,
  D3DSAMP_MAXMIPLEVEL,
  D3DSAMP_MIPMAPLODBIAS
};
constexpr DWORD D3DCULL_NONE = 1, D3DFILL_SOLID = 3, D3DBLEND_ONE = 2,
                D3DBLEND_ZERO = 1, D3DBLEND_SRCALPHA = 5, D3DBLEND_INVSRCALPHA = 6, D3DBLENDOP_ADD = 1, D3DTEXF_NONE = 0,
                D3DTEXF_POINT = 1, D3DTADDRESS_CLAMP = 3, D3DZB_TRUE = 1,
                D3DCLEAR_TARGET = 1, D3DUSAGE_RENDERTARGET = 1,
                D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING = 2,
                D3DPMISCCAPS_COLORWRITEENABLE = 1,
                D3DPMISCCAPS_SEPARATEALPHABLEND = 2,
                D3DPMISCCAPS_INDEPENDENTWRITEMASKS = 4,
                D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING = 8, D3DFVF_XYZRHW = 4,
                D3DFVF_TEX1 = 256, D3DPRASTERCAPS_SCISSORTEST = 0x01000000;
constexpr DWORD D3DPS_VERSION(unsigned a, unsigned b) { return a * 256 + b; }
constexpr DWORD D3DVS_VERSION(unsigned a, unsigned b) { return a * 256 + b; }
constexpr unsigned char D3DDECLTYPE_FLOAT4 = 3, D3DDECLTYPE_FLOAT2 = 1,
                        D3DDECLMETHOD_DEFAULT = 0, D3DDECLUSAGE_POSITION = 0,
                        D3DDECLUSAGE_TEXCOORD = 5;
struct D3DVERTEXELEMENT9 {
  std::uint16_t Stream, Offset;
  unsigned char Type, Method, Usage, UsageIndex;
};
#define D3DDECL_END() {255, 0, 17, 0, 0, 0}
struct RECT {
  LONG left = 0, top = 0, right = 17, bottom = 11;
};
struct D3DRECT {
  LONG x1, y1, x2, y2;
};
struct D3DVIEWPORT9 {
  DWORD X = 0, Y = 0, Width = 17, Height = 11;
  float MinZ = 0, MaxZ = 1;
};
struct D3DSURFACE_DESC {
  D3DFORMAT Format = D3DFMT_A16B16G16R16F;
  D3DRESOURCETYPE Type = D3DRTYPE_SURFACE;
  DWORD Usage = D3DUSAGE_RENDERTARGET;
  D3DPOOL Pool = D3DPOOL_DEFAULT;
  D3DMULTISAMPLE_TYPE MultiSampleType = D3DMULTISAMPLE_NONE;
  DWORD MultiSampleQuality = 0;
  UINT Width = 17, Height = 11;
};
struct D3DCAPS9 {
  DWORD NumSimultaneousRTs = 3, PixelShaderVersion = D3DPS_VERSION(3, 0),
        VertexShaderVersion = D3DVS_VERSION(3, 0),
        PrimitiveMiscCaps = D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING,
        MaxTextureWidth = 4096, MaxTextureHeight = 4096, RasterCaps = 0;
};
struct D3DDEVICE_CREATION_PARAMETERS {
  UINT AdapterOrdinal = 0;
  D3DDEVTYPE DeviceType = D3DDEVTYPE_HAL;
};
constexpr int IID_IUnknown = 0, IID_IDirect3DTexture9 = 1;
struct IUnknown {
  unsigned refs = 0, adds = 0, releases = 0, qi_calls = 0;
  IUnknown *identity = nullptr;
  HRESULT qi_hr = S_OK;
  bool qi_null = false, qi_output_on_failure = false;
  virtual ~IUnknown() = default;
  void AddRef() {
    if (identity)
      identity->AddRef();
    else {
      ++refs;
      ++adds;
    }
  }
  void Release() {
    if (identity)
      identity->Release();
    else {
      assert(refs);
      --refs;
      ++releases;
    }
  }
  virtual HRESULT QueryInterface(int iid, void **out) {
    ++qi_calls;
    *out = nullptr;
    if (!qi_null && (SUCCEEDED(qi_hr) || qi_output_on_failure)) {
      IUnknown *value = iid == IID_IUnknown && identity ? identity : this;
      value->AddRef();
      *out = value;
    }
    return qi_hr;
  }
};
struct IDirect3DDevice9 : IUnknown {};
struct IDirect3DPixelShader9 : IUnknown {};
struct IDirect3DVertexShader9 : IUnknown {};
struct IDirect3DVertexDeclaration9 : IUnknown {};
struct IDirect3DVertexBuffer9 : IUnknown {};
struct IDirect3DBaseTexture9 : IUnknown {};
struct IDirect3DSurface9;
struct IDirect3DTexture9 : IDirect3DBaseTexture9 {
  IDirect3DSurface9 *level = nullptr;
  HRESULT level_hr = S_OK;
  bool level_null = false, level_output_on_failure = false;
  HRESULT GetSurfaceLevel(UINT, IDirect3DSurface9 **);
};
struct IDirect3DSurface9 : IUnknown {
  IDirect3DTexture9 *texture = nullptr;
  D3DSURFACE_DESC desc{};
  HRESULT GetDesc(D3DSURFACE_DESC *out) {
    *out = desc;
    return S_OK;
  }
  HRESULT GetContainer(int, void **out) {
    *out = texture;
    if (texture)
      texture->AddRef();
    return texture ? S_OK : E_NOINTERFACE;
  }
};
inline HRESULT IDirect3DTexture9::GetSurfaceLevel(UINT,
                                                  IDirect3DSurface9 **out) {
  *out = nullptr;
  if (!level_null && level &&
      (SUCCEEDED(level_hr) || level_output_on_failure)) {
    level->AddRef();
    *out = level;
  }
  return level_hr;
}
struct IDirect3D9 : IUnknown {
  HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE,
                            D3DFORMAT) {
    return S_OK;
  }
  HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT,
                                 D3DFORMAT) {
    return S_OK;
  }
};
