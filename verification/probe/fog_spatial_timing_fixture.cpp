// Separate executable; includes the frozen correctness utilities without changing
// their source or executable. Timing never instantiates the fault interposer.
#define main fog_spatial_correctness_main
#include "fog_spatial_fixture.cpp"
#undef main
#include "fog_spatial_timing_inc.h"
int main(int argc,char** argv){
    if(argc!=3)return 2;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try{
        WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3FogSpatialTiming";RegisterClassA(&cls);
        Window window;window.handle=CreateWindowA(cls.lpszClassName,"Detached production fog timing",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,cls.hInstance,nullptr);
        if(!window.handle)throw std::runtime_error("window");
        HMODULE library=LoadLibraryA("d3d9.dll");if(!library)throw std::runtime_error("d3d9");
        auto addr=GetProcAddress(library,"Direct3DCreate9");IDirect3D9*(WINAPI* create)(UINT)=nullptr;std::memcpy(&create,&addr,sizeof create);if(!create)throw std::runtime_error("Create9 symbol");
        Com<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);if(!api.p)throw std::runtime_error("Create9");
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window.handle;pp.BackBufferWidth=1280;pp.BackBufferHeight=768;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;check(api->CreateDevice(0,D3DDEVTYPE_HAL,window.handle,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device.p),"CreateDevice");
        D3DDISPLAYMODE mode{};check(api->GetAdapterDisplayMode(0,&mode),"display");
        return fog_spatial_timing::qualify(device.p,mode.Format,argv[1],argv[2]);
    }catch(const std::exception& e){std::printf("RESULT FAIL error=%s\n",e.what());return 1;}
}
