// Original synthetic D3DX DLL used only by loading_trace_fixture.exe.
// No graphics objects are created: sentinel args/results prove ABI forwarding.
#include <windows.h>
#include <d3dx9.h>
#define PTR(T,n) reinterpret_cast<T*>(static_cast<uintptr_t>(n))
static HRESULT finish(bool match){SetLastError(match?0x4321:0x8765);return match?S_FALSE:E_INVALIDARG;}
extern "C" __declspec(dllexport) HRESULT WINAPI D3DXCreateEffect(IDirect3DDevice9* d,const void* data,UINT size,const D3DXMACRO* defs,ID3DXInclude* inc,DWORD flags,ID3DXEffectPool* pool,ID3DXEffect** out,ID3DXBuffer** errors) {
    bool match=d==PTR(IDirect3DDevice9,0x1000)&&data==PTR(void,0x2000)&&size==123&&defs==PTR(D3DXMACRO,0x3000)&&inc==PTR(ID3DXInclude,0x4000)&&flags==456&&pool==PTR(ID3DXEffectPool,0x5000);
    if(out)*out=PTR(ID3DXEffect,0x6000);
    if(errors)*errors=PTR(ID3DXBuffer,0x7000);
    return finish(match);
}
extern "C" __declspec(dllexport) HRESULT WINAPI D3DXCreateTextureFromFileInMemoryEx(IDirect3DDevice9* d,const void* data,UINT size,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT fmt,D3DPOOL pool,DWORD filter,DWORD mip,D3DCOLOR key,D3DXIMAGE_INFO* info,PALETTEENTRY* pal,IDirect3DTexture9** out) {
    bool match=d==PTR(IDirect3DDevice9,0x1000)&&data==PTR(void,0x2000)&&size==321&&w==12&&h==13&&levels==14&&usage==15&&fmt==D3DFMT_A8R8G8B8&&pool==D3DPOOL_MANAGED&&filter==16&&mip==17&&key==18&&info==PTR(D3DXIMAGE_INFO,0x3000)&&pal==PTR(PALETTEENTRY,0x4000);
    if(out)*out=PTR(IDirect3DTexture9,0x5000);
    return finish(match);
}
extern "C" __declspec(dllexport) HRESULT WINAPI D3DXCreateCubeTextureFromFileInMemoryEx(IDirect3DDevice9* d,const void* data,UINT size,UINT edge,UINT levels,DWORD usage,D3DFORMAT fmt,D3DPOOL pool,DWORD filter,DWORD mip,D3DCOLOR key,D3DXIMAGE_INFO* info,PALETTEENTRY* pal,IDirect3DCubeTexture9** out) {
    bool match=d==PTR(IDirect3DDevice9,0x1000)&&data==PTR(void,0x2000)&&size==654&&edge==12&&levels==14&&usage==15&&fmt==D3DFMT_A8R8G8B8&&pool==D3DPOOL_MANAGED&&filter==16&&mip==17&&key==18&&info==PTR(D3DXIMAGE_INFO,0x3000)&&pal==PTR(PALETTEENTRY,0x4000);
    if(out)*out=PTR(IDirect3DCubeTexture9,0x5000);
    return finish(match);
}
extern "C" __declspec(dllexport) HRESULT WINAPI D3DXLoadSurfaceFromFileInMemory(IDirect3DSurface9* dest,const PALETTEENTRY* pal,const RECT* rect,const void* data,UINT size,const RECT* srcrect,DWORD filter,D3DCOLOR key,D3DXIMAGE_INFO* info) {
    return finish(dest==PTR(IDirect3DSurface9,0x1000)&&pal==PTR(PALETTEENTRY,0x2000)&&rect==PTR(RECT,0x3000)&&data==PTR(void,0x4000)&&size==987&&srcrect==PTR(RECT,0x5000)&&filter==16&&key==18&&info==PTR(D3DXIMAGE_INFO,0x6000));
}

extern "C" __declspec(dllexport) HRESULT WINAPI D3DXCreateMesh(DWORD faces,DWORD vertices,DWORD options,const D3DVERTEXELEMENT9* decl,IDirect3DDevice9* device,ID3DXMesh** out) {
    const bool match=faces==11&&vertices==13&&options==0x41&&decl==PTR(D3DVERTEXELEMENT9,0x1000)&&device==PTR(IDirect3DDevice9,0x2000)&&GetLastError()==0x1357;
    if(out)*out=PTR(ID3DXMesh,0x3000);
    return finish(match);
}
extern "C" __declspec(dllexport) HRESULT WINAPI D3DXCleanMesh(D3DXCLEANTYPE type,ID3DXMesh* input,const DWORD* adjacency,ID3DXMesh** out,DWORD* new_adjacency,ID3DXBuffer** errors) {
    const bool match=type==D3DXCLEANTYPE(3)&&input==PTR(ID3DXMesh,0x1000)&&adjacency==PTR(DWORD,0x2000)&&new_adjacency&&GetLastError()==0x1357;
    if(out)*out=PTR(ID3DXMesh,0x3000);
    if(new_adjacency)*new_adjacency=0xabcdef01;
    if(errors)*errors=PTR(ID3DXBuffer,0x4000);
    return finish(match);
}
