// Original hidden-window API-contract probe. No game assets or installation.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
template<class T> struct Com {T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}};
template<class T>T proc(HMODULE m,const char* n){FARPROC p=GetProcAddress(m,n);T v=nullptr;static_assert(sizeof(v)==sizeof(p));std::memcpy(&v,&p,sizeof(v));return v;}
void module(const char* n){char p[MAX_PATH]{};HMODULE m=GetModuleHandleA(n);if(m)GetModuleFileNameA(m,p,MAX_PATH);std::printf("MODULE name=%s path=%s\n",n,p);}
void texture9(IDirect3DDevice9* d,const char* kind,D3DFORMAT f,HANDLE input=nullptr){
    Com<IDirect3DTexture9> t;HANDLE h=input;
    const HRESULT hr=d->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,f,D3DPOOL_DEFAULT,&t.p,&h);
    std::printf("D3D9_TEXTURE kind=%s format=%u input_handle=%p result=%08lx texture=%u output_handle=%p\n",kind,unsigned(f),input,hr,t.p!=nullptr,h);
}
bool read11(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* t,unsigned char* pixel,UINT bytes){
    D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Com<ID3D11Texture2D> stage;HRESULT hr=d->CreateTexture2D(&desc,nullptr,&stage.p);if(FAILED(hr))return false;
    c->CopyResource(stage.p,t);D3D11_MAPPED_SUBRESOURCE map{};hr=c->Map(stage.p,0,D3D11_MAP_READ,0,&map);if(FAILED(hr))return false;
    std::memcpy(pixel,map.pData,bytes);c->Unmap(stage.p,0);return true;
}
void printpixel(const char* kind,const unsigned char* p,UINT bytes){std::printf("PIXEL kind=%s bytes=",kind);for(UINT i=0;i<bytes;++i)std::printf("%02x",p[i]);std::puts("");}
void pixel_contract(IDirect3DDevice9Ex* d9,ID3D11Device* d11,ID3D11DeviceContext* c,ID3D11Texture2D* original,ID3D11Texture2D* alias,D3DFORMAT f,HANDLE handle){
    const UINT bytes=f==D3DFMT_A16B16G16R16F?8:4;unsigned char before[8]{},own[8]{},after[8]{};
    Com<ID3D11RenderTargetView> view;HRESULT hr=d11->CreateRenderTargetView(alias,nullptr,&view.p);std::printf("ALIAS_RTV result=%08lx\n",hr);if(FAILED(hr))return;
    const float seed[]={.25f,.5f,.75f,1};c->ClearRenderTargetView(view.p,seed);
    if(!read11(d11,c,original,before,bytes)){std::puts("PIXEL_ERROR phase=d3d11_before");return;}printpixel("dxmt_original_after_alias_clear",before,bytes);
    Com<IDirect3DTexture9> imported;hr=d9->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,f,D3DPOOL_DEFAULT,&imported.p,&handle);if(FAILED(hr))return;
    Com<IDirect3DSurface9> surface,previous,stage;imported->GetSurfaceLevel(0,&surface.p);d9->GetRenderTarget(0,&previous.p);
    hr=d9->SetRenderTarget(0,surface.p);if(SUCCEEDED(hr))hr=d9->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(255,0,255,0),1,0);
    std::printf("WINE9_IMPORT_CLEAR result=%08lx\n",hr);d9->SetRenderTarget(0,previous.p);if(FAILED(hr))return;
    hr=d9->CreateOffscreenPlainSurface(64,64,f,D3DPOOL_SYSTEMMEM,&stage.p,nullptr);if(SUCCEEDED(hr))hr=d9->GetRenderTargetData(surface.p,stage.p);
    D3DLOCKED_RECT map{};if(SUCCEEDED(hr))hr=stage->LockRect(&map,nullptr,D3DLOCK_READONLY);std::printf("WINE9_READBACK result=%08lx\n",hr);if(FAILED(hr))return;
    std::memcpy(own,map.pBits,bytes);stage->UnlockRect();printpixel("wine9_import_after_green_clear",own,bytes);
    if(!read11(d11,c,original,after,bytes)){std::puts("PIXEL_ERROR phase=d3d11_after");return;}printpixel("dxmt_original_after_wine9_clear",after,bytes);
    std::printf("SHARING_CONTROL format=%u dxmt_unchanged=%u wine9_differs=%u\n",unsigned(f),!std::memcmp(before,after,bytes),std::memcmp(own,after,bytes)!=0);
}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="X3HDRSharedProbe";RegisterClassA(&wc);
    HWND w=CreateWindowA(wc.lpszClassName,"Hidden HDR handle contract",WS_OVERLAPPEDWINDOW,0,0,96,96,nullptr,nullptr,wc.hInstance,nullptr);
    if(!w)return 2;
    HMODULE m9=LoadLibraryA("d3d9.dll"),m11=LoadLibraryA("d3d11.dll");
    auto create9=proc<IDirect3D9*(WINAPI*)(UINT)>(m9,"Direct3DCreate9");
    auto createEx=proc<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>(m9,"Direct3DCreate9Ex");
    auto create11=proc<decltype(&D3D11CreateDevice)>(m11,"D3D11CreateDevice");
    if(!create9||!createEx||!create11)return 3;
    {
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    Com<IDirect3D9> a;a.p=create9(D3D_SDK_VERSION);Com<IDirect3DDevice9> d;
    HRESULT hr=a->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&d.p);std::printf("CREATE_D3D9 result=%08lx\n",hr);if(FAILED(hr))return 4;
    Com<IDirect3D9Ex> ax;Com<IDirect3DDevice9Ex> dx;hr=createEx(D3D_SDK_VERSION,&ax.p);std::printf("CREATE_D3D9EX_FACTORY result=%08lx\n",hr);
    if(SUCCEEDED(hr)){hr=ax->CreateDeviceEx(0,D3DDEVTYPE_HAL,w,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&dx.p);std::printf("CREATE_D3D9EX_DEVICE result=%08lx\n",hr);}
    Com<ID3D11Device> b;Com<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level{};
    hr=create11(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&b.p,&level,&c.p);std::printf("CREATE_D3D11 result=%08lx level=%x\n",hr,unsigned(level));if(FAILED(hr))return 5;
    for(unsigned i=0;i<2;++i){
        D3DFORMAT f=i?D3DFMT_A16B16G16R16F:D3DFMT_A8R8G8B8;
        texture9(d.p,"ordinary_export",f);if(dx.p)texture9(dx.p,"ex_export",f);
        D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=64;desc.MipLevels=1;desc.ArraySize=1;desc.Format=i?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
        Com<ID3D11Texture2D> t;hr=b->CreateTexture2D(&desc,nullptr,&t.p);std::printf("D3D11_SHARED_CREATE format=%u result=%08lx\n",unsigned(desc.Format),hr);
        if(SUCCEEDED(hr)){Com<IDXGIResource> r;hr=t->QueryInterface(IID_IDXGIResource,reinterpret_cast<void**>(&r.p));HANDLE h=nullptr;if(SUCCEEDED(hr))hr=r->GetSharedHandle(&h);std::printf("D3D11_SHARED_HANDLE result=%08lx handle=%p\n",hr,h);
            if(SUCCEEDED(hr)&&h){Com<ID3D11Texture2D> alias;hr=b->OpenSharedResource(h,IID_ID3D11Texture2D,reinterpret_cast<void**>(&alias.p));std::printf("D3D11_REOPEN result=%08lx alias=%u\n",hr,alias.p!=nullptr);if(dx.p){texture9(dx.p,"ex_import_dxmt",f,h);if(alias.p)pixel_contract(dx.p,b.p,c.p,t.p,alias.p,f,h);}}
        }
    }
    for(const char* n:{"d3d9.dll","wined3d.dll","d3d11.dll","dxgi.dll","winemetal.dll","opengl32.dll"})module(n);
    }
    DestroyWindow(w);std::puts("PROBE_COMPLETED Readback is verification only, not a proposed production bridge.");return 0;
}
