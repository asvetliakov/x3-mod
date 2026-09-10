// Public API fixture for optional telemetry. It renders only a disposable window.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
static unsigned failures;
static bool check(const char* label,HRESULT hr){std::printf("%s hr=%08lx %s\n",label,hr,SUCCEEDED(hr)?"PASS":"FAIL");failures+=FAILED(hr);return SUCCEEDED(hr);}
static void expect(const char* label,bool valid){std::printf("%s %s\n",label,valid?"PASS":"FAIL");failures+=!valid;}
template<class T>static void release(T*& p){if(p){p->Release();p=nullptr;}}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    HMODULE module=LoadLibraryA("d3d9.dll");auto address=GetProcAddress(module,"Direct3DCreate9");IDirect3D9*(WINAPI*create)(UINT)=nullptr;std::memcpy(&create,&address,sizeof create);
    IDirect3D9* api=create?create(D3D_SDK_VERSION):nullptr;if(!api)return 2;
    WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="X3TelemetryFixture";RegisterClassA(&wc);
    HWND window=CreateWindowA(wc.lpszClassName,"X3 telemetry verification",WS_OVERLAPPEDWINDOW,80,80,160,160,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(window,SW_SHOWNOACTIVATE);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;if(!check("device",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&d)))return 2;
    IDirect3DTexture9* texture=nullptr;
    check("texture",d->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));release(texture);
    const HRESULT invalid=d->CreateTexture(0,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr);expect("invalid texture forwarded",FAILED(invalid)&&texture==nullptr);
    // No Present occurs before this deadline. Resource calls must flush the window.
    Sleep(1100);
    check("texture after load gap",d->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));release(texture);
    IDirect3DCubeTexture9* cube=nullptr;check("cube",d->CreateCubeTexture(8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&cube,nullptr));release(cube);
    IDirect3DVolumeTexture9* volume=nullptr;check("volume",d->CreateVolumeTexture(8,8,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&volume,nullptr));release(volume);
    IDirect3DVertexBuffer9* vb=nullptr;check("vb",d->CreateVertexBuffer(64,0,0,D3DPOOL_MANAGED,&vb,nullptr));release(vb);
    IDirect3DIndexBuffer9* ib=nullptr;check("ib",d->CreateIndexBuffer(12,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr));release(ib);
    IDirect3DSurface9 *rt=nullptr,*depth=nullptr,*back=nullptr,*copy=nullptr,*cursor=nullptr;
    check("rt",d->CreateRenderTarget(64,64,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr));
    check("depth",d->CreateDepthStencilSurface(64,64,D3DFMT_D16,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr));
    check("back",d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));
    check("readback",d->CreateOffscreenPlainSurface(64,64,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr));
    check("cursor surface",d->CreateOffscreenPlainSurface(32,32,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cursor,nullptr));
    if(cursor){D3DLOCKED_RECT lock{};if(check("cursor lock",cursor->LockRect(&lock,nullptr,0))){for(int y=0;y<32;++y)std::memset(static_cast<char*>(lock.pBits)+y*lock.Pitch,0,128);check("cursor unlock",cursor->UnlockRect());}check("cursor properties",d->SetCursorProperties(0,0,cursor));}
    POINT original{};GetCursorPos(&original);const BOOL previous=d->ShowCursor(FALSE);SetLastError(0x4139);const BOOL second_previous=d->ShowCursor(FALSE);const DWORD show_error=GetLastError();expect("D3D ShowCursor previous bool",second_previous==FALSE);std::printf("cursor show last_error=%08lx\n",show_error);
    for(int i=0;i<300;++i){SetLastError(0x4137);d->SetCursorPosition(original.x,original.y,0);}
    const DWORD position_error=GetLastError();std::printf("cursor position last_error=%08lx\n",position_error);
    d->ShowCursor(previous);release(cursor);
    check("first present",d->Present(nullptr,nullptr,nullptr,nullptr));
    check("set rt",d->SetRenderTarget(0,rt));check("set depth",d->SetDepthStencilSurface(depth));
    check("clear target depth",d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff2468ac,1,0));
    check("depth only clear",d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,0.5f,0));
    check("unset depth",d->SetDepthStencilSurface(nullptr));
    expect("invalid RT forwarded",FAILED(d->SetRenderTarget(0,nullptr)));
    check("readback copy",d->GetRenderTargetData(rt,copy));D3DLOCKED_RECT lock{};
    if(copy&&check("readback lock",copy->LockRect(&lock,nullptr,D3DLOCK_READONLY))){expect("clear pixel preserved",(*static_cast<DWORD*>(lock.pBits)&0xffffff)==0x2468ac);check("readback unlock",copy->UnlockRect());}
    check("stretch",d->StretchRect(rt,nullptr,back,nullptr,D3DTEXF_NONE));check("restore rt",d->SetRenderTarget(0,back));
    const DWORD code[]={0xffff0200,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};IDirect3DPixelShader9* shader=nullptr;check("shader",d->CreatePixelShader(code,&shader));check("bind shader",d->SetPixelShader(shader));
    const float color[]={.2f,.6f,.9f,1};check("constants",d->SetPixelShaderConstantF(0,color,1));
    struct Vertex{float x,y,z,rhw;DWORD color;};const Vertex vertices[]={{0,0,.5f,1,0xffffffff},{0,64,.5f,1,0xffffffff},{64,0,.5f,1,0xffffffff}};
    check("fvf",d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));check("lighting",d->SetRenderState(D3DRS_LIGHTING,FALSE));check("cull",d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));check("begin",d->BeginScene());check("draw",d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,sizeof(Vertex)));check("end",d->EndScene());check("captured present",d->Present(nullptr,nullptr,nullptr,nullptr));check("unbind shader",d->SetPixelShader(nullptr));release(shader);
    release(copy);release(back);release(depth);release(rt);
    check("reset",d->Reset(&pp));check("after reset present",d->Present(nullptr,nullptr,nullptr,nullptr));expect("device refs zero",d->Release()==0);expect("api refs zero",api->Release()==0);DestroyWindow(window);
    std::printf("TELEMETRY FIXTURE: %u failures\n",failures);return failures?1:0;
}
