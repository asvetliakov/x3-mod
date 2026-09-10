// Regression for observed CopyDepthView loss retirement. Original standalone
// hidden device only; per-instance native vtable faults simulate precise HRESULTs.
// No game, production hooks, registry settings or disk assets are modified.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/ownership/d3d9_ownership.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <array>

namespace {
unsigned checks=0, failures=0, backendDeaths=0, historyDeaths=0;
void expect(bool pass,const char* name){++checks;if(!pass){++failures;std::printf("FAIL %s\n",name);}}
void require(HRESULT hr,const char* name){if(FAILED(hr)){std::printf("API_FAIL %s %08lx\n",name,hr);throw std::runtime_error(name);}}
const GUID markerId={0x5ad5c6f2,0x0b41,0x4d1c,{0x87,0x06,0x45,0x02,0x77,0x45,0x10,0xab}};
struct Marker final:IUnknown{
    ULONG refs=1;unsigned* counter;
    explicit Marker(unsigned* value):counter(value){}
    HRESULT WINAPI QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG WINAPI AddRef()override{return ++refs;}
    ULONG WINAPI Release()override{const auto remaining=--refs;if(!remaining){++*counter;delete this;}return remaining;}
};
template<class T>void mark(T* resource,unsigned& counter){auto marker=new Marker(&counter);const auto hr=resource->SetPrivateData(markerId,marker,sizeof(IUnknown*),D3DSPD_IUNKNOWN);marker->Release();require(hr,"SetPrivateData lifetime marker");}
enum class Fault{GetTexture,GetPointSize,SetTexture,Trigger,PointRestore,TextureRestore,GenericThenRestore,GetDSCopy,GetDSView,ClearMetadata,ClearResult,OrdinaryGetTexture,Recording,BeginBlock,EndBlock,InitGetTexture,InitCreateTexture,InitAllocationFailure};
struct Patch {
    IDirect3DDevice9* native;void** original;std::array<void*,119> table{};
    Fault mode;HRESULT loss;
    unsigned setTextures=0,setPoints=0,clearCalls=0,depthQueries=0,callsAfterLoss=0;
    bool observedLoss=false;
    static Patch* active;
    Patch(IDirect3DDevice9* value,Fault fault,HRESULT result):native(value),original(*reinterpret_cast<void***>(value)),mode(fault),loss(result){
        std::memcpy(table.data(),original,sizeof(table));
        table[40]=reinterpret_cast<void*>(&getDepth);table[64]=reinterpret_cast<void*>(&getTexture);
        table[58]=reinterpret_cast<void*>(&getPoint);table[65]=reinterpret_cast<void*>(&setTexture);
        table[57]=reinterpret_cast<void*>(&setPoint);table[43]=reinterpret_cast<void*>(&clear);
        table[60]=reinterpret_cast<void*>(&beginBlock);table[61]=reinterpret_cast<void*>(&endBlock);table[23]=reinterpret_cast<void*>(&createTexture);
        active=this;*reinterpret_cast<void***>(native)=table.data();
    }
    ~Patch(){*reinterpret_cast<void***>(native)=original;active=nullptr;}
    template<class F>F call(unsigned slot){return reinterpret_cast<F>(original[slot]);}
    HRESULT lost(){observedLoss=true;return loss;}
    static HRESULT WINAPI getDepth(IDirect3DDevice9* d,IDirect3DSurface9** out){auto& p=*active;++p.depthQueries;
        if(p.mode==Fault::GetDSCopy||p.mode==Fault::GetDSView||p.mode==Fault::ClearMetadata){if(out)*out=nullptr;return p.lost();}
        return p.call<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9**)>(40)(d,out);
    }
    static HRESULT WINAPI getTexture(IDirect3DDevice9* d,DWORD stage,IDirect3DBaseTexture9** out){auto& p=*active;
        if(p.mode==Fault::GetTexture||p.mode==Fault::OrdinaryGetTexture||p.mode==Fault::InitGetTexture){if(out)*out=nullptr;return p.mode==Fault::OrdinaryGetTexture?E_FAIL:p.lost();}
        return p.call<HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9**)>(64)(d,stage,out);
    }
    static HRESULT WINAPI getPoint(IDirect3DDevice9* d,D3DRENDERSTATETYPE state,DWORD* out){auto& p=*active;
        if(p.mode==Fault::GetPointSize&&state==D3DRS_POINTSIZE)return p.lost();
        return p.call<HRESULT(WINAPI*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD*)>(58)(d,state,out);
    }
    static HRESULT WINAPI setTexture(IDirect3DDevice9* d,DWORD stage,IDirect3DBaseTexture9* value){auto& p=*active;++p.setTextures;if(p.observedLoss)++p.callsAfterLoss;
        if(p.mode==Fault::SetTexture&&p.setTextures==1)return p.lost();
        if((p.mode==Fault::TextureRestore||p.mode==Fault::GenericThenRestore)&&p.setTextures==2)return p.lost();
        if(p.mode==Fault::GenericThenRestore&&p.setTextures==1)return E_FAIL;
        return p.call<HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*)>(65)(d,stage,value);
    }
    static HRESULT WINAPI setPoint(IDirect3DDevice9* d,D3DRENDERSTATETYPE state,DWORD value){auto& p=*active;++p.setPoints;if(p.observedLoss)++p.callsAfterLoss;
        if((p.mode==Fault::Trigger&&p.setPoints==1)||(p.mode==Fault::PointRestore&&p.setPoints==2))return p.lost();
        return p.call<HRESULT(WINAPI*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD)>(57)(d,state,value);
    }
    static HRESULT WINAPI beginBlock(IDirect3DDevice9* d){auto& p=*active;
        if(p.mode==Fault::BeginBlock)return p.lost();
        return p.call<HRESULT(WINAPI*)(IDirect3DDevice9*)>(60)(d);
    }
    static HRESULT WINAPI endBlock(IDirect3DDevice9* d,IDirect3DStateBlock9** out){auto& p=*active;
        if(p.mode==Fault::EndBlock){if(out)*out=nullptr;return p.lost();}
        return p.call<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DStateBlock9**)>(61)(d,out);
    }
    static HRESULT WINAPI createTexture(IDirect3DDevice9* d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared){auto& p=*active;
        if(p.mode==Fault::InitCreateTexture||p.mode==Fault::InitAllocationFailure){if(out)*out=nullptr;return p.mode==Fault::InitAllocationFailure?E_OUTOFMEMORY:p.lost();}
        return p.call<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*)>(23)(d,w,h,levels,usage,format,pool,out,shared);
    }
    static HRESULT WINAPI clear(IDirect3DDevice9* d,DWORD count,const D3DRECT* rects,DWORD flags,D3DCOLOR color,float depth,DWORD stencil){auto& p=*active;++p.clearCalls;
        if(p.mode==Fault::ClearResult)return p.lost();
        return p.call<HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD)>(43)(d,count,rects,flags,color,depth,stencil);
    }
};
Patch* Patch::active=nullptr;

void runCase(IDirect3D9* factory,HWND window,Fault mode,HRESULT loss,const char* name){
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.hDeviceWindow=window;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24X8;
    IDirect3DDevice9* device=nullptr;require(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device),"CreateDevice");
    const unsigned backendBefore=backendDeaths,historyBefore=historyDeaths;
    IDirect3DSurface9* back=nullptr;require(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back),"GetBackBuffer");mark(back,backendDeaths);back->Release();
    auto native=x3m::ownership::borrowed_native_device(device);IDirect3DTexture9* history=nullptr;
    require(native->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&history,nullptr),"native renderer history");mark(history,historyDeaths);require(x3m::ownership::retain_renderer_resource(device,history),"adopt renderer history");
    require(device->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,.25f,0),"source clear");require(x3m::ownership::copy_auto_depth(device),"initial valid copy");
    x3m::ownership::CopyDepthView before{},after{};require(x3m::ownership::get_copy_depth_view(device,&before),"initial view");expect(before.available&&before.copy_valid&&before.texture,"initial copy valid");
    HRESULT result=S_OK;unsigned settersAfterLoss=0,clearCalls=0;bool viewGetter=false;
    if(mode==Fault::Recording||mode==Fault::EndBlock)require(device->BeginStateBlock(),"begin recording");
    {
        Patch patch(native,mode,loss);
        if(mode==Fault::BeginBlock)result=device->BeginStateBlock();
        else if(mode==Fault::EndBlock){IDirect3DStateBlock9* output=reinterpret_cast<IDirect3DStateBlock9*>(0x1234);result=device->EndStateBlock(&output);expect(output==nullptr,"failed EndStateBlock preserves null backend output");}
        else if(mode==Fault::GetDSView){result=x3m::ownership::get_copy_depth_view(device,&after);viewGetter=true;}
        else if(mode==Fault::ClearMetadata||mode==Fault::ClearResult)result=device->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,.75f,0);
        else result=x3m::ownership::copy_auto_depth(device);
        settersAfterLoss=patch.callsAfterLoss;clearCalls=patch.clearCalls;
    }
    if(!viewGetter)require(x3m::ownership::get_copy_depth_view(device,&after),"post-fault view");
    const bool ordinary=mode==Fault::OrdinaryGetTexture||mode==Fault::Recording;
    if(ordinary){
        expect(after.available&&after.copy_valid&&after.texture==before.texture,"ordinary preflight preserves previous copy");
        expect(historyDeaths==historyBefore,"ordinary failure preserves history");
        expect(result==(mode==Fault::Recording?D3DERR_INVALIDCALL:E_FAIL),"ordinary failure result preserved");
    }else{
        expect(!after.available&&!after.copy_valid&&!after.texture,"observed loss retires copy and borrowed pointer");
        expect(after.status==loss,"observed loss remains visible in snapshot");
        expect(after.generation>before.generation,"loss advances storage generation");
        expect(historyDeaths==historyBefore+1,"loss retires actual renderer texture");
        expect(settersAfterLoss==0,"no injected Set calls after observed loss");
        if(mode==Fault::ClearMetadata){expect(result==S_OK,"Clear forwards original result despite metadata loss");expect(clearCalls==1,"Clear forwarded exactly once");}
        else if(mode==Fault::GetDSView)expect(result==S_OK,"view getter returns recognized wrapper status");
        else if(mode==Fault::GenericThenRestore)expect(FAILED(result),"combined failure remains a failure");
        else expect(result==loss,"copy/Clear loss HRESULT preserved");
    }
    if(mode==Fault::Recording||mode==Fault::EndBlock){IDirect3DStateBlock9* block=nullptr;require(device->EndStateBlock(&block),"end recording");block->Release();}
    expect(backendDeaths==backendBefore,"backend alive until final logical release");
    const auto refs=device->Release();expect(refs==0,"final wrapper references zero");expect(backendDeaths==backendBefore+1,"native backend destroyed after final wrapper release");expect(historyDeaths==historyBefore+1,"history destroyed exactly once");
    std::printf("CASE name=%s injected=%08lx result=%08lx status=%08lx available=%u valid=%u pointer_null=%u history_retired=%u backend_destroyed=%u after_loss_setters=%u checks=%u failures=%u\n",name,loss,result,after.status,after.available,after.copy_valid,after.texture==nullptr,historyDeaths-historyBefore,backendDeaths-backendBefore,settersAfterLoss,checks,failures);
}
void runInitCase(IDirect3D9* factory,HWND window,Fault mode,HRESULT loss,const char* name){
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.hDeviceWindow=window;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24X8;
    IDirect3DDevice9* device=nullptr;require(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device),"CreateDevice init test");
    auto native=x3m::ownership::borrowed_native_device(device);
    x3m::ownership::CopyDepthView before{},after{};require(x3m::ownership::get_copy_depth_view(device,&before),"initial init test view");expect(before.available,"init test starts allocated");
    HRESULT result;
    {Patch patch(native,mode,loss);result=device->Reset(&pp);}
    expect(result==S_OK,"optional initialization failure preserves successful native Reset");
    require(x3m::ownership::get_copy_depth_view(device,&after),"init failure view");
    const bool ordinary=mode==Fault::InitAllocationFailure;
    expect(!after.available&&!after.copy_valid&&!after.texture,"failed initialization exposes no borrowed copy");
    expect(after.status==(ordinary?E_OUTOFMEMORY:loss),"init failure status preserved");
    expect(after.generation>before.generation,"Reset retires prior storage generation");
    const unsigned historyBefore=historyDeaths,backendBefore=backendDeaths;
    IDirect3DSurface9* back=nullptr;require(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back),"post Reset backbuffer");mark(back,backendDeaths);back->Release();
    IDirect3DTexture9* history=nullptr;require(native->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&history,nullptr),"new renderer history after synthetic failure");mark(history,historyDeaths);
    const HRESULT retained=x3m::ownership::retain_renderer_resource(device,history);
    expect(retained==(ordinary?S_OK:D3DERR_INVALIDCALL),"observed init loss rejects renderer history; ordinary allocation failure permits it");
    if(FAILED(retained))history->Release();
    expect(x3m::ownership::copy_auto_depth(device)==(ordinary?D3DERR_INVALIDCALL:loss),"copy preserves observed initialization loss");
    expect(device->Release()==0,"init failure final wrapper refs zero");
    expect(historyDeaths==historyBefore+1,"init failure history destroyed exactly once");
    expect(backendDeaths==backendBefore+1,"init failure backend destroyed");
    std::printf("CASE name=%s injected=%08lx result=%08lx status=%08lx available=%u valid=%u pointer_null=%u history_retired=%u backend_destroyed=%u after_loss_setters=0 checks=%u failures=%u\n",name,loss,result,after.status,after.available,after.copy_valid,after.texture==nullptr,historyDeaths-historyBefore,backendDeaths-backendBefore,checks,failures);
}

}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);HWND window=nullptr;IDirect3D9* factory=nullptr;HMODULE module=nullptr;
    try{
        module=LoadLibraryA("d3d9.dll");if(!module)throw std::runtime_error("LoadLibrary");auto proc=GetProcAddress(module,"Direct3DCreate9");IDirect3D9*(WINAPI*create)(UINT);std::memcpy(&create,&proc,sizeof create);if(!create)throw std::runtime_error("factory export");
        auto native=create(D3D_SDK_VERSION);if(!native)throw std::runtime_error("native factory");x3m::ownership::Options options{};options.capture_auto_depth=true;const HRESULT wrapped=x3m::ownership::wrap_factory(native,&factory,options);if(FAILED(wrapped)){native->Release();require(wrapped,"wrap_factory");}
        WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3CopyLossRegression";RegisterClassA(&cls);window=CreateWindowA(cls.lpszClassName,"Original copy loss regression",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);if(!window)throw std::runtime_error("hidden window");
        struct Case {Fault mode;const char* name;};
        const Case cases[]={{Fault::GetTexture,"get_texture"},{Fault::GetPointSize,"get_pointsize"},{Fault::SetTexture,"set_texture"},{Fault::Trigger,"trigger"},{Fault::PointRestore,"restore_pointsize"},{Fault::TextureRestore,"restore_texture"},{Fault::GenericThenRestore,"generic_then_restore_loss"},{Fault::GetDSCopy,"copy_source_query"},{Fault::GetDSView,"view_source_query"},{Fault::ClearMetadata,"clear_metadata_query"},{Fault::ClearResult,"clear_result"},{Fault::BeginBlock,"begin_stateblock"},{Fault::EndBlock,"end_stateblock"}};
        for(HRESULT loss:{D3DERR_DEVICELOST,D3DERR_DEVICENOTRESET})for(const auto& item:cases)runCase(factory,window,item.mode,loss,item.name);
        runCase(factory,window,Fault::OrdinaryGetTexture,D3DERR_DEVICELOST,"ordinary_getter_failure");runCase(factory,window,Fault::Recording,D3DERR_DEVICELOST,"stateblock_recording");
        for(HRESULT loss:{D3DERR_DEVICELOST,D3DERR_DEVICENOTRESET}){
            runInitCase(factory,window,Fault::InitGetTexture,loss,"init_get_texture");
            runInitCase(factory,window,Fault::InitCreateTexture,loss,"init_create_texture");
        }
        runInitCase(factory,window,Fault::InitAllocationFailure,E_OUTOFMEMORY,"init_allocation_failure");
        expect(factory->Release()==0,"final factory references zero");factory=nullptr;DestroyWindow(window);window=nullptr;FreeLibrary(module);module=nullptr;
        std::printf("RESULT %s checks=%u failures=%u cases=33\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
    }catch(const std::exception& error){std::printf("RESULT FAIL exception=%s checks=%u failures=%u\n",error.what(),checks,failures);if(factory)factory->Release();if(window)DestroyWindow(window);if(module)FreeLibrary(module);return 2;}
}
