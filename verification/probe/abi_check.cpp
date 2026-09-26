// Compile-only guard for the vtable slots used by production capture.cpp.
#define CINTERFACE
#include <d3d9.h>
#include <cstddef>
#define SLOT(type, method, index) static_assert(offsetof(type, method) == (index) * sizeof(void*), #method)
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
// Live motion route (src/proxy/motion_output.cpp): hooked setters and native slots.
SLOT(IDirect3DDevice9Vtbl, GetDirect3D, 6);
SLOT(IDirect3DDevice9Vtbl, GetDisplayMode, 8);
SLOT(IDirect3DDevice9Vtbl, GetCreationParameters, 9);
SLOT(IDirect3DDevice9Vtbl, UpdateSurface, 30);
SLOT(IDirect3DDevice9Vtbl, UpdateTexture, 31);
SLOT(IDirect3DDevice9Vtbl, GetRenderTargetData, 32);
SLOT(IDirect3DDevice9Vtbl, ColorFill, 35);
SLOT(IDirect3DDevice9Vtbl, CreateOffscreenPlainSurface, 36);
SLOT(IDirect3DDevice9Vtbl, GetRenderTarget, 38);
SLOT(IDirect3DDevice9Vtbl, GetDepthStencilSurface, 40);
SLOT(IDirect3DDevice9Vtbl, BeginScene, 41);
SLOT(IDirect3DDevice9Vtbl, EndScene, 42);
SLOT(IDirect3DDevice9Vtbl, SetViewport, 47);
SLOT(IDirect3DDevice9Vtbl, GetViewport, 48);
SLOT(IDirect3DDevice9Vtbl, SetRenderState, 57);
SLOT(IDirect3DDevice9Vtbl, GetRenderState, 58);
SLOT(IDirect3DDevice9Vtbl, CreateStateBlock, 59);
SLOT(IDirect3DDevice9Vtbl, BeginStateBlock, 60);
SLOT(IDirect3DDevice9Vtbl, EndStateBlock, 61);
SLOT(IDirect3DDevice9Vtbl, SetScissorRect, 75);
SLOT(IDirect3DDevice9Vtbl, GetScissorRect, 76);
SLOT(IDirect3DDevice9Vtbl, CreateVertexDeclaration, 86); // the quad's declaration (renderer/quad_vertex_program.h)
SLOT(IDirect3DDevice9Vtbl, SetVertexDeclaration, 87);
SLOT(IDirect3DDevice9Vtbl, GetVertexDeclaration, 88);
SLOT(IDirect3DDevice9Vtbl, SetFVF, 89);
SLOT(IDirect3DDevice9Vtbl, GetFVF, 90);
SLOT(IDirect3DDevice9Vtbl, SetVertexShader, 92);
SLOT(IDirect3DDevice9Vtbl, GetVertexShader, 93);
SLOT(IDirect3DDevice9Vtbl, SetVertexShaderConstantF, 94);
SLOT(IDirect3DDevice9Vtbl, GetVertexShaderConstantF, 95);
SLOT(IDirect3DDevice9Vtbl, SetVertexShaderConstantI, 96);
SLOT(IDirect3DDevice9Vtbl, GetVertexShaderConstantI, 97);
SLOT(IDirect3DDevice9Vtbl, SetStreamSource, 100);
SLOT(IDirect3DDevice9Vtbl, GetStreamSource, 101);
SLOT(IDirect3DDevice9Vtbl, GetStreamSourceFreq, 103);
SLOT(IDirect3DDevice9Vtbl, SetIndices, 104);
SLOT(IDirect3DDevice9Vtbl, GetIndices, 105);
SLOT(IDirect3DDevice9Vtbl, SetPixelShader, 107);
SLOT(IDirect3DDevice9Vtbl, GetPixelShader, 108);
SLOT(IDirect3DDevice9Vtbl, SetPixelShaderConstantF, 109);
SLOT(IDirect3DDevice9Vtbl, GetPixelShaderConstantF, 110);
SLOT(IDirect3DDevice9Vtbl, DrawRectPatch, 115);
SLOT(IDirect3DDevice9Vtbl, DrawTriPatch, 116);
// Temporal step 3 (src/renderer/temporal_pass.cpp native slots, the route's
// scene/query tracking hooks and the query-object wrapper in capture.cpp).
SLOT(IDirect3DDevice9Vtbl, AddRef, 1);
SLOT(IDirect3DDevice9Vtbl, GetDeviceCaps, 7);
SLOT(IDirect3DDevice9Vtbl, GetTexture, 64);
SLOT(IDirect3DDevice9Vtbl, SetTexture, 65);
SLOT(IDirect3DDevice9Vtbl, GetTextureStageState, 66);
SLOT(IDirect3DDevice9Vtbl, GetSamplerState, 68);
SLOT(IDirect3DDevice9Vtbl, SetTextureStageState, 67);
SLOT(IDirect3DDevice9Vtbl, SetSamplerState, 69);
SLOT(IDirect3DDevice9Vtbl, SetStreamSourceFreq, 102);
SLOT(IDirect3DDevice9Vtbl, CreateQuery, 118);
SLOT(IDirect3DStateBlock9Vtbl, Release, 2);
SLOT(IDirect3DStateBlock9Vtbl, Capture, 4);
SLOT(IDirect3DStateBlock9Vtbl, Apply, 5);
SLOT(IDirect3DQuery9Vtbl, Release, 2);
SLOT(IDirect3DQuery9Vtbl, Issue, 6);
static_assert(sizeof(IDirect3DQuery9Vtbl) == 8 * sizeof(void*));
static_assert(sizeof(IDirect3DStateBlock9Vtbl) == 6 * sizeof(void*));
static_assert(sizeof(IDirect3D9Vtbl) == 17 * sizeof(void*));
static_assert(sizeof(IDirect3D9ExVtbl) == 22 * sizeof(void*));
static_assert(sizeof(IDirect3DDevice9Vtbl) == 119 * sizeof(void*));
static_assert(sizeof(IDirect3DDevice9ExVtbl) == 134 * sizeof(void*));
