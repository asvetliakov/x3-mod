// Exercises typed constants, stateblock-restored buffer/texture bindings and
// resource allocation IDs through the public D3D9 API. No game assets required.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

static unsigned failures;
static bool check(const char* name,HRESULT hr) {
    std::printf("%s: %08lx %s\n",name,hr,SUCCEEDED(hr)?"PASS":"FAIL");
    if (FAILED(hr)) ++failures;
    return SUCCEEDED(hr);
}
static void expect(const char* name,bool value) {
    std::printf("%s: %s\n",name,value?"PASS":"FAIL");
    if (!value) ++failures;
}
struct Vertex { float x,y,z,rhw; DWORD color; };
static constexpr DWORD fvf=D3DFVF_XYZRHW|D3DFVF_DIFFUSE;
static IDirect3DVertexBuffer9* vertices(IDirect3DDevice9* d,float z) {
    IDirect3DVertexBuffer9* buffer=nullptr;
    // Prefix deliberately exercises a nonzero stream offset.
    if (!check("create VB",d->CreateVertexBuffer(16+3*sizeof(Vertex),0,fvf,D3DPOOL_MANAGED,&buffer,nullptr))) return nullptr;
    const Vertex data[]={{10,10,z,1,0xffffffff},{140,10,z,1,0xffffffff},{10,100,z,1,0xffffffff}};
    void* bytes=nullptr;
    if(check("VB lock",buffer->Lock(16,sizeof data,&bytes,0))) {
        std::memcpy(bytes,data,sizeof data);check("VB unlock",buffer->Unlock());
    }
    return buffer;
}
static void typed(IDirect3DDevice9* d,bool populated) {
    const int nonzero[]={7,-2,2147483647,-2147483647};
    const int zero[]={0,0,0,0};
    const BOOL flags[]={TRUE,FALSE,TRUE}, clear[]={FALSE,FALSE,FALSE};
    check("VS int",d->SetVertexShaderConstantI(0,populated?nonzero:zero,1));
    check("PS int",d->SetPixelShaderConstantI(0,populated?nonzero:zero,1));
    check("VS bool",d->SetVertexShaderConstantB(0,populated?flags:clear,3));
    check("PS bool",d->SetPixelShaderConstantB(0,populated?flags:clear,3));
    const float negative_zero[]={-0.0f,0.0f,0.0f,0.0f};
    check("VS signed-zero float",d->SetVertexShaderConstantF(10,negative_zero,1));
}
static void frame(IDirect3DDevice9* d,bool invalid=false) {
    check("BeginScene",d->BeginScene());
    check("indexed draw",d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1));
    if(invalid) {
        IDirect3DIndexBuffer9* bound=nullptr;
        check("save index binding",d->GetIndices(&bound));
        check("remove required index buffer",d->SetIndices(nullptr));
        expect("missing-index draw forwarded",FAILED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1)));
        check("restore index binding",d->SetIndices(bound));
        if(bound)bound->Release();
    }
    check("EndScene",d->EndScene());check("Present",d->Present(nullptr,nullptr,nullptr,nullptr));
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    HMODULE module=LoadLibraryA("d3d9.dll");
    if(!module) return 2;
    FARPROC address=GetProcAddress(module,"Direct3DCreate9");
    IDirect3D9* (WINAPI*create)(UINT)=nullptr;
    std::memcpy(&create,&address,sizeof create);
    IDirect3D9* api=create?create(D3D_SDK_VERSION):nullptr;
    if(!api) return 2;
    WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="X3CaptureStateFixture";
    RegisterClassA(&wc);
    HWND window=CreateWindowA(wc.lpszClassName,"X3 capture-state verification",WS_OVERLAPPEDWINDOW,80,80,180,160,nullptr,nullptr,wc.hInstance,nullptr);
    ShowWindow(window,SW_SHOWNOACTIVATE);
    for(unsigned iteration=0;iteration<2;++iteration) {
        std::printf("DEVICE %u pure=%u\n",iteration+1,iteration);
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
        pp.BackBufferWidth=160;pp.BackBufferHeight=120;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        IDirect3DDevice9* d=nullptr;
        DWORD flags=D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE|(iteration?D3DCREATE_PUREDEVICE:0);
        if(!check("CreateDevice",api->CreateDevice(0,D3DDEVTYPE_HAL,window,flags,&pp,&d))) break;
        IDirect3DVertexBuffer9* vb=vertices(d,.5f);
        IDirect3DIndexBuffer9* ib=nullptr;
        check("create IB",d->CreateIndexBuffer(6,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr));
        if(ib) { void* data=nullptr;const WORD idx[]={0,1,2};if(check("IB lock",ib->Lock(0,6,&data,0))) {std::memcpy(data,idx,6);check("IB unlock",ib->Unlock());} }
        IDirect3DTexture9* texture=nullptr;check("create sample texture",d->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));
        IDirect3DTexture9* target=nullptr;IDirect3DSurface9* surface=nullptr;
        check("create target texture",d->CreateTexture(160,120,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&target,nullptr));
        if(target)check("target surface",target->GetSurfaceLevel(0,&surface));
        IDirect3DSurface9* backbuffer=nullptr;check("backbuffer",d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer));
        check("set target",d->SetRenderTarget(0,surface));
        check("set stream",d->SetStreamSource(0,vb,16,sizeof(Vertex)));check("set indices",d->SetIndices(ib));
        check("set texture",d->SetTexture(0,texture));check("set FVF",d->SetFVF(fvf));
        check("lighting off",d->SetRenderState(D3DRS_LIGHTING,FALSE));check("cull off",d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        typed(d,true);
        IDirect3DStateBlock9* state=nullptr;check("stateblock",d->CreateStateBlock(D3DSBT_ALL,&state));
        frame(d); // frame 0 arms automatic capture for frame 1.
        frame(d); // frame 1: initial bindings/values.
        typed(d,false);d->SetStreamSource(0,nullptr,0,0);d->SetIndices(nullptr);d->SetTexture(0,nullptr);
        if(state)check("restore stateblock",state->Apply());
        frame(d); // frame 2: same allocation IDs and nonzero constants via Apply.
        typed(d,false);frame(d,true); // frame 3: explicit zeros, plus one failed draw.
        if(state)state->Release();
        check("unbind old VB",d->SetStreamSource(0,nullptr,0,0));
        if(vb)expect("old VB released",vb->Release()==0);
        vb=vertices(d,.7f);check("set recreated VB",d->SetStreamSource(0,vb,16,sizeof(Vertex)));
        frame(d); // frame 4 must receive a new resource ID, even if address reused.
        // UP data must not inherit the still-bound VB/IB identities.
        const Vertex up[]={{10,10,.5f,1,0xffffffff},{140,10,.5f,1,0xffffffff},{10,100,.5f,1,0xffffffff}};
        const WORD up_indices[]={0,1,2};
        check("UP BeginScene",d->BeginScene());
        check("UP draw",d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,3,1,up_indices,D3DFMT_INDEX16,up,sizeof(Vertex)));
        check("UP EndScene",d->EndScene());check("UP Present",d->Present(nullptr,nullptr,nullptr,nullptr));
        d->SetStreamSource(0,nullptr,0,0);d->SetIndices(nullptr);d->SetTexture(0,nullptr);d->SetRenderTarget(0,backbuffer);
        if(vb)expect("new VB released",vb->Release()==0);
        if(ib)expect("IB released",ib->Release()==0);
        if(texture)expect("sample texture released",texture->Release()==0);
        if(surface)surface->Release();
        if(target)expect("target released",target->Release()==0);
        if(backbuffer)backbuffer->Release();
        check("Reset after capture",d->Reset(&pp));
        expect("device released",d->Release()==0);
    }
    expect("API released",api->Release()==0);DestroyWindow(window);
    std::printf("CAPTURE STATE RESULT: %u failures\n",failures);return failures?1:0;
}
