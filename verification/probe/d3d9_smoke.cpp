// Verifies the public D3D9 behavior most easily broken by a capture proxy.
// Runs on either the platform backend or an app-local d3d9.dll, without X3.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
static unsigned failures=0;
static bool check(const char* label,HRESULT result) {
    std::printf("%s: 0x%08lx %s\n",label,(unsigned long)result,SUCCEEDED(result)?"PASS":"FAIL");
    if(FAILED(result)) ++failures;
    return SUCCEEDED(result);
}
static void expect(const char* label,bool condition) {
    std::printf("%s: %s\n",label,condition?"PASS":"FAIL"); if(!condition) ++failures;
}
struct Vertex { float x,y,z,rhw; DWORD color; };
static void draw(IDirect3DDevice9* device) {
    const Vertex vertices[]={{10,10,.5f,1,0xffff0000},{160,180,.5f,1,0xff00ff00},{310,10,.5f,1,0xff0000ff}};
    check("Clear",device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff102030,1,0));
    check("BeginScene",device->BeginScene());
    check("SetFVF",device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
    check("SetRenderState lighting off",device->SetRenderState(D3DRS_LIGHTING,FALSE));
    check("SetRenderState cull none",device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    check("DrawPrimitiveUP",device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,sizeof(Vertex)));
    check("EndScene",device->EndScene());
    check("Present",device->Present(nullptr,nullptr,nullptr,nullptr));
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    HMODULE lib=LoadLibraryA("d3d9.dll");
    if(!lib) { std::printf("LoadLibrary failed: %lu\n",GetLastError());return 1; }
    char path[MAX_PATH]={};GetModuleFileNameA(lib,path,MAX_PATH);std::printf("D3D9 loaded: %s\n",path);
    FARPROC address=GetProcAddress(lib,"Direct3DCreate9");
    IDirect3D9* (WINAPI*create)(UINT)=nullptr;std::memcpy(&create,&address,sizeof create);
    if(!create) return 1;
    IDirect3D9* api=create(D3D_SDK_VERSION);if(!api) {std::puts("Create9 null");return 1;}
    IUnknown* apiIdentity=nullptr;
    check("D3D9 QueryInterface IUnknown",api->QueryInterface(IID_PPV_ARGS(&apiIdentity)));
    expect("D3D9 COM identity",apiIdentity==static_cast<IUnknown*>(api));if(apiIdentity) apiIdentity->Release();
    WNDCLASSA cls={};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3Smoke";RegisterClassA(&cls);
    HWND window=CreateWindowA(cls.lpszClassName,"X3 D3D9 proxy smoke test",WS_OVERLAPPEDWINDOW,80,80,340,240,nullptr,nullptr,cls.hInstance,nullptr);ShowWindow(window,SW_SHOWNOACTIVATE);
    D3DPRESENT_PARAMETERS pp={};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=320;pp.BackBufferHeight=200;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    for(int iteration=0;iteration<2;++iteration) {
        std::printf("DEVICE ITERATION %d\n",iteration);
        IDirect3DDevice9* device=nullptr;
        if(!check("CreateDevice",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device))) break;
        IDirect3D9* parent=nullptr;check("GetDirect3D",device->GetDirect3D(&parent));expect("GetDirect3D identity",parent==api);if(parent)parent->Release();
        IUnknown* identity=nullptr;check("Device QueryInterface IUnknown",device->QueryInterface(IID_PPV_ARGS(&identity)));expect("Device COM identity",identity==static_cast<IUnknown*>(device));if(identity)identity->Release();
        IDirect3DTexture9* texture=nullptr;
        if(check("CreateTexture managed",device->CreateTexture(16,16,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr))) {
            IDirect3DDevice9* owner=nullptr;check("Texture GetDevice",texture->GetDevice(&owner));expect("Texture GetDevice identity",owner==device);if(owner)owner->Release();
            check("SetTexture",device->SetTexture(0,texture));check("UnsetTexture",device->SetTexture(0,nullptr));texture->Release();
        }
        IDirect3DStateBlock9* state=nullptr;
        check("Set state before capture",device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        if(check("CreateStateBlock",device->CreateStateBlock(D3DSBT_ALL,&state))) {
            check("StateBlock Capture",state->Capture());check("Change captured state",device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));check("StateBlock Apply",state->Apply());
            DWORD value=1;check("GetRenderState after Apply",device->GetRenderState(D3DRS_ALPHABLENDENABLE,&value));expect("StateBlock restored value",value==FALSE);state->Release();
        }
        check("BeginStateBlock",device->BeginStateBlock());check("Record state",device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));state=nullptr;check("EndStateBlock",device->EndStateBlock(&state));if(state) {check("Recorded state Apply",state->Apply());state->Release();}
        // ps_2_0: mov oC0, c0. Valid precompiled bytecode avoids a shader-compiler dependency.
        const DWORD bytecode[]={0xffff0200,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};
        IDirect3DPixelShader9* shader=nullptr;
        if(check("CreatePixelShader",device->CreatePixelShader(bytecode,&shader))) {
            DWORD copy[5]={};UINT size=sizeof copy;
            check("PixelShader GetFunction",shader->GetFunction(copy,&size));
            expect("PixelShader bytecode preserved",size==sizeof bytecode && std::memcmp(copy,bytecode,sizeof bytecode)==0);
            IDirect3DDevice9* owner=nullptr;check("PixelShader GetDevice",shader->GetDevice(&owner));expect("PixelShader owner identity",owner==device);if(owner)owner->Release();
            check("SetPixelShader",device->SetPixelShader(shader));
            const float color[]={.2f,.6f,.9f,1};check("SetPixelShaderConstantF",device->SetPixelShaderConstantF(0,color,1));
        }
        draw(device);draw(device);
        check("UnsetPixelShader",device->SetPixelShader(nullptr));if(shader)shader->Release();
        check("Reset",device->Reset(&pp));draw(device);
        expect("Device final Release reaches zero",device->Release()==0);
    }
    expect("D3D9 final Release reaches zero",api->Release()==0);DestroyWindow(window);
    std::printf("SMOKE RESULT: %u failures\n",failures);return failures?1:0;
}
