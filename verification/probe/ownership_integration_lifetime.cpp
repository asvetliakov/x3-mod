// Exercises final child->device->factory release through the actual DLL hooks.
// Every round drops factory/device caller refs before the last child reference.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
static unsigned failures;
static bool check(const char* name,HRESULT result){std::printf("%s %08lx\n",name,result);if(FAILED(result))++failures;return SUCCEEDED(result);}
static void expect(const char* name,bool valid){std::printf("%s %s\n",name,valid?"PASS":"FAIL");failures+=!valid;}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);HMODULE library=LoadLibraryA("d3d9.dll");auto address=library?GetProcAddress(library,"Direct3DCreate9"):nullptr;
    IDirect3D9*(WINAPI*create)(UINT)=nullptr;std::memcpy(&create,&address,sizeof create);if(!create)return 2;
    WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3OwnershipIntegrationLifetime";RegisterClassA(&cls);
    HWND window=CreateWindowA(cls.lpszClassName,"X3 ownership DLL lifetime fixture",WS_OVERLAPPEDWINDOW,80,80,160,160,nullptr,nullptr,cls.hInstance,nullptr);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    for(unsigned round=0;round<16;++round){
        std::printf("ROUND %u\n",round);IDirect3D9* factory=create(D3D_SDK_VERSION);if(!factory){++failures;break;}
        IDirect3DDevice9* device=nullptr;if(!check("create device",factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device))){factory->Release();break;}
        IDirect3DTexture9* child=nullptr;if(!check("create final child",device->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&child,nullptr))){device->Release();factory->Release();break;}
        auto expected_factory=factory;auto expected_device=device;
        factory->Release();factory=nullptr;device->Release();device=nullptr;
        if(check("recover device through child",child->GetDevice(&device))){
            expect("device identity",device==expected_device);
            if(check("recover factory through device",device->GetDirect3D(&factory))){expect("factory identity",factory==expected_factory);factory->Release();factory=nullptr;}
            device->Release();device=nullptr;
        }
        // This call performs the final parent releases, testing both capture
        // map cleanup and the ownership Node secondary-vptr destructor path.
        expect("last child reaches zero",child->Release()==0);
    }
    DestroyWindow(window);std::printf("INTEGRATION LIFETIME RESULT failures=%u\n",failures);return failures?1:0;
}
