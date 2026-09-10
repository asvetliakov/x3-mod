// Compile-only guard for the vtable slots used by production capture.cpp.
#define CINTERFACE
#include <d3d9.h>
#include <cstddef>
#define SLOT(type, method, index) static_assert(offsetof(type, method) == (index)*sizeof(void*), #method)
SLOT(IDirect3D9Vtbl, Release, 2);
SLOT(IDirect3D9Vtbl, CreateDevice, 16);
SLOT(IDirect3DDevice9Vtbl, SetCursorProperties, 10);
SLOT(IDirect3DDevice9Vtbl, SetCursorPosition, 11);
SLOT(IDirect3DDevice9Vtbl, ShowCursor, 12);
SLOT(IDirect3DDevice9Vtbl, CreateTexture, 23);
SLOT(IDirect3DDevice9Vtbl, CreateVolumeTexture, 24);
SLOT(IDirect3DDevice9Vtbl, CreateCubeTexture, 25);
SLOT(IDirect3DDevice9Vtbl, CreateVertexBuffer, 26);
SLOT(IDirect3DDevice9Vtbl, CreateIndexBuffer, 27);
SLOT(IDirect3DDevice9Vtbl, CreateRenderTarget, 28);
SLOT(IDirect3DDevice9Vtbl, CreateDepthStencilSurface, 29);
SLOT(IDirect3DDevice9Vtbl, StretchRect, 34);
SLOT(IDirect3DDevice9Vtbl, SetDepthStencilSurface, 39);
SLOT(IDirect3DDevice9Vtbl, Reset, 16);
SLOT(IDirect3DDevice9Vtbl, Present, 17);
SLOT(IDirect3DDevice9Vtbl, SetRenderTarget, 37);
SLOT(IDirect3DDevice9Vtbl, Clear, 43);
SLOT(IDirect3DDevice9Vtbl, DrawPrimitive, 81);
SLOT(IDirect3DDevice9Vtbl, DrawIndexedPrimitive, 82);
SLOT(IDirect3DDevice9Vtbl, DrawPrimitiveUP, 83);
SLOT(IDirect3DDevice9Vtbl, DrawIndexedPrimitiveUP, 84);
SLOT(IDirect3DDevice9Vtbl, CreateVertexShader, 91);
SLOT(IDirect3DDevice9Vtbl, CreatePixelShader, 106);
static_assert(sizeof(IDirect3D9Vtbl) == 17*sizeof(void*));
static_assert(sizeof(IDirect3D9ExVtbl) == 22*sizeof(void*));
static_assert(sizeof(IDirect3DDevice9Vtbl) == 119*sizeof(void*));
static_assert(sizeof(IDirect3DDevice9ExVtbl) == 134*sizeof(void*));
