// B2a actual ownership-on Capture Release/Reset integration. Reuse authored
// renderer/query helpers; none of the included fixture's old suites is run.
#define main unused_motion_output_fixture_main
#include "motion_output_fixture.cpp"
#undef main
#define X3M_LATTICE_UPLOAD_FIXTURE
#include "lattice_capture_lifecycle_abi.h"
#include <d3dx9mesh.h>

namespace {
namespace own=x3m::ownership;
using Token=own::CloneUploadArmToken;
using Close=own::CloneUploadClose;
struct CaptureApi {
    HRESULT (*factory)(IDirect3D9*,IDirect3D9**);
    HRESULT (*watch)(IDirect3DDevice9*);
    HRESULT (*view)(IDirect3DDevice9*,LatticeCaptureLifecycleView*);
    HRESULT (*arm)(IDirect3DDevice9*,const D3DVERTEXELEMENT9*);
    HRESULT (*close)(IDirect3DDevice9*,Token,Close*);
    bool (*hook)(void*);
    void (*callback)(LatticeCaptureCallback);
    HRESULT (*clone)(ID3DXMesh*,DWORD,const D3DVERTEXELEMENT9*,IDirect3DDevice9*,ID3DXMesh**);
    IDirect3DDevice9* (*native)(IDirect3DDevice9*);
    LatticeGuardArm query_arm;LatticeGuardRead query_read;
    HRESULT (*query_disarm)(IDirect3DDevice9*);
    explicit CaptureApi(HMODULE m){
#define LOAD(member,name) member=symbol<decltype(member)>(m,name,true)
        LOAD(factory,"x3m_lattice_capture_factory");LOAD(watch,"x3m_lattice_capture_watch");
        LOAD(view,"x3m_lattice_capture_view");LOAD(arm,"x3m_lattice_capture_arm");
        LOAD(close,"x3m_lattice_capture_close");LOAD(hook,"x3m_lattice_capture_hook");
        LOAD(callback,"x3m_lattice_capture_callback");LOAD(clone,"x3m_lattice_capture_clone");
        LOAD(native,"x3m_lattice_capture_native");LOAD(query_arm,"x3m_lattice_observer_fixture_arm");
        LOAD(query_read,"x3m_lattice_observer_fixture_read");LOAD(query_disarm,"x3m_lattice_observer_fixture_disarm");
#undef LOAD
    }
    LatticeCaptureLifecycleView state(IDirect3DDevice9* d){LatticeCaptureLifecycleView s{};require(view(d,&s)==S_OK,"capture CPU snapshot exact S_OK");return s;}
};
const D3DVERTEXELEMENT9 upload_decl[]={
    {0,0,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_POSITION,0},
    {0,8,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_TEXCOORD,0},
    {0,16,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_NORMAL,0},
    {0,24,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_TANGENT,0},
    {0,32,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_BINORMAL,0},D3DDECL_END()};
using NativeCreate=IDirect3D9*(WINAPI*)(UINT);
struct CaptureSession {
    Com<IDirect3D9> factory;Com<IDirect3DDevice9> device;D3DPRESENT_PARAMETERS pp{};
    CaptureSession(CaptureApi& a,NativeCreate create,HWND window){
        auto* native=create(D3D_SDK_VERSION);require(native!=nullptr,"native D3D factory");
        require(a.factory(native,&factory.p)==S_OK,"actual readable ownership and Capture factory");
        pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
        pp.BackBufferWidth=Fixture::W;pp.BackBufferHeight=Fixture::H;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
        pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24X8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        api(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device.p),"actual Capture CreateDevice");
        require(a.watch(device.p)==S_OK,"watch actual Capture device");
    }
    void factory_close(){require(factory.p->Release()==0,"actual factory final count zero");factory.p=nullptr;}
};
ULONG current_refs(IDirect3DDevice9* d){d->AddRef();return d->Release();}
Token arm(CaptureApi& a,IDirect3DDevice9* d){
    LatticeGuardCpu saved{},before{},after{};guard_cpu(saved);guard_seed();guard_cpu(before);
    const HRESULT hr=a.arm(d,upload_decl);guard_cpu(after);
    asm volatile("frstor %0\n ldmxcsr %1"::"m"(saved.x87),"m"(saved.mxcsr):"memory");SetLastError(saved.error);
    require(hr==S_OK,"coordinator arm exact S_OK");
    require(guard_same_cpu(before,after),"new arm coordinator preserves CPU and LastError");
    const auto state=a.state(d);require(state.live&&state.pin==1&&state.gate&&!state.closing,"one real ownership pin and enabled A gate");return state.arm;
}
void retired(CaptureApi& a,IDirect3DDevice9* key){const auto s=a.state(key);
    require(!s.live&&!s.pin&&!s.gate&&s.retired==1,"actual Capture retires device exactly once and disables gate");}
void basic(CaptureApi& a,NativeCreate create,HWND window){
    std::uint64_t serial=0,id=0;
    for(unsigned mode=0;mode<4;++mode){CaptureSession s(a,create,window);auto* const key=s.device.p;
        const auto initial=a.state(key);require(initial.live&&initial.capture_id>id,"recreation has fresh Capture owner");id=initial.capture_id;
        current_refs(key);auto state=a.state(key);require(!state.pin_queries&&!state.pin,"default-off Release performs no upload query");
        if(mode){const auto token=arm(a,key);require(token.serial>serial,"recreated arm serial never reused");serial=token.serial;}
        Com<IDirect3DVertexBuffer9> child;
        if(mode==2)api(key->CreateVertexBuffer(48,0,0,D3DPOOL_MANAGED,&child.p,nullptr),"real child owns final device reference");
        if(mode==3){
            Com<IDirect3DDevice9> other;auto pp=s.pp;
            api(s.factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&other.p),"second concurrent actual Capture device");
            require(a.arm(other.p,upload_decl)==S_FALSE&&a.state(key).gate,"second device arm refuses without disabling first gate");
            require(other.p->Release()==0,"unarmed second device final Release returns zero");other.p=nullptr;
            require(a.state(key).gate&&a.state(key).pin==1,"unrelated device retirement preserves active upload owner");
            const auto token=a.state(key).arm;const ULONG before=current_refs(key);Close close{};
            require(a.close(key,token,&close)==S_OK&&close==Close::Released,"explicit immediate close exact Released");
            require(current_refs(key)+1==before,"explicit close releases one actual device pin");
            const auto next=arm(a,key);require(next.serial!=token.serial,"same-device rearm fresh token");
            require(a.close(key,token,&close)==S_FALSE&&close==Close::None&&a.state(key).gate,"stale close cannot disable newer A gate");}
        const ULONG refs=key->Release();s.device.p=nullptr;
        if(mode==2){require(refs>0&&a.state(key).live&&a.state(key).pin==1,"app Release leaves legitimate child and upload pin");
            require(child.p->Release()==0,"actual final child Release completes");child.p=nullptr;
        }else require(refs==0,"direct application final Release returns actual zero");
        retired(a,key);s.factory_close();
        std::printf("CAPTURE_LIFETIME mode=%u retired=1\n",mode);
    }
}
struct ResetFault {
    using Fn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
    IDirect3DDevice9* native;void** old;void* table[119]{};
    static HRESULT WINAPI fail(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*){return D3DERR_DEVICELOST;}
    explicit ResetFault(IDirect3DDevice9* d):native(d),old(*reinterpret_cast<void***>(d)){
        std::memcpy(table,old,sizeof table);table[16]=reinterpret_cast<void*>(&fail);*reinterpret_cast<void***>(d)=table;}
    ~ResetFault(){*reinterpret_cast<void***>(native)=old;}
};
void resets(CaptureApi& a,NativeCreate create,HWND window){CaptureSession s(a,create,window);auto* key=s.device.p;arm(a,key);
    const auto before=a.state(key);
    {ResetFault fault(a.native(key));require(key->Reset(&s.pp)==D3DERR_DEVICELOST,"actual Capture forwards native Reset failure");}
    const auto lost=a.state(key);require(lost.pin==1&&lost.gate&&lost.ownership_generation>before.ownership_generation&&lost.reset_generation>before.reset_generation,
        "failed Capture Reset keeps pin and advances both generations");
    require(key->Reset(&s.pp)==S_OK,"actual Capture native Reset recovery");
    const auto recovered=a.state(key);require(recovered.pin==1&&recovered.gate&&recovered.ownership_generation>lost.ownership_generation,"recovery keeps same arm with new generation");
    require(recovered.arm.serial==before.arm.serial,"Reset never silently rearms Store");
    require(key->Release()==0,"post-Reset final Release actual zero");s.device.p=nullptr;retired(a,key);s.factory_close();
}
CaptureApi* callback_api=nullptr;IDirect3DDevice9* callback_device=nullptr;Token callback_arm{};
unsigned callback_calls=0;ULONG callback_refs=0;
void deferred_callback(unsigned event,IUnknown*){
    if(event!=unsigned(own::CloneUploadFixtureEvent::BeforeOriginal)||callback_calls)return;
    ++callback_calls;Close close{};
    require(callback_api->close(callback_device,callback_arm,&close)==S_OK&&close==Close::Deferred,"active actual Clone close returns Deferred without waiting");
    const auto state=callback_api->state(callback_device);
    require(state.pin==1&&state.closing&&state.scope&&!state.gate&&!state.retiring,"Deferred leaves active scope pin with stack guard unwound");
    const ULONG before=current_refs(callback_device);
    callback_refs=callback_device->Release();
    require(callback_refs>0&&callback_refs+1==before,"last app Release during real Clone returns exact child-protected count");
}
void deferred(CaptureApi& a,NativeCreate create,HWND window,HMODULE d3dx){CaptureSession s(a,create,window);auto* const key=s.device.p;
    using MeshCreate=HRESULT(WINAPI*)(DWORD,DWORD,DWORD,const D3DVERTEXELEMENT9*,IDirect3DDevice9*,ID3DXMesh**);
    const auto make=symbol<MeshCreate>(d3dx,"D3DXCreateMesh",true);Com<ID3DXMesh> source,clone;
    const auto shape=own::clone_upload::shapes[1];
    api(make(shape.faces,shape.vertices,D3DXMESH_SYSTEMMEM,upload_decl,key,&source.p),"actual source mesh for deferred lifecycle");
    void* map=nullptr;api(source->LockVertexBuffer(0,&map),"author source vertices");std::memset(map,0,shape.vertices*40u);api(source->UnlockVertexBuffer(),"source vertices complete");
    api(source->LockIndexBuffer(0,&map),"author source indices");std::memset(map,0,shape.faces*6u);api(source->UnlockIndexBuffer(),"source indices complete");
    DWORD* attrs=nullptr;api(source->LockAttributeBuffer(0,&attrs),"author source attributes");std::memset(attrs,0,shape.faces*sizeof(DWORD));api(source->UnlockAttributeBuffer(),"source attributes complete");
    callback_api=&a;callback_device=key;callback_arm=arm(a,key);callback_calls=0;callback_refs=0;a.callback(deferred_callback);
    const HRESULT hr=a.clone(source.p,D3DXMESH_MANAGED|D3DXMESH_WRITEONLY,upload_decl,key,&clone.p);a.callback(nullptr);s.device.p=nullptr;
    require(hr==S_OK&&clone.p&&callback_calls==1,"actual Clone unchanged success with one deferred close");
    const auto after=a.state(key);require(after.live&&!after.pin&&!after.scope&&!after.gate&&after.deferred==1,"actual Clone finish drains pin after unbinding");
    require(current_refs(key)>0,"real mesh children still own device after deferred app Release");
    clone.reset();source.reset();retired(a,key);s.factory_close();
    std::printf("CAPTURE_DEFERRED explicit_close=1 child_protected_release=%lu actual_clone=1\n",callback_refs);
}
void drop_resources(Fixture& f){
    f.back.reset();f.depth.reset();f.bloom_surface.reset();f.bloom.reset();
    f.vs.reset();f.ps.reset();f.flat.reset();f.hdr2.reset();f.hdr8.reset();f.hdrmid.reset();f.hdrconst.reset();
    f.declaration.reset();f.vb_a.reset();f.vb_b.reset();f.cube.reset();f.ramp.reset();for(auto& t:f.textures)t.reset();
}
void guarded_release(CaptureApi& a,NativeCreate create,HWND window,HMODULE runtime,const Words& vs,const Words& ps){
    for(unsigned routed=0;routed<2;++routed){CaptureSession s(a,create,window);auto* const key=s.device.p;
        Fixture f;f.runtime=runtime;f.window=window;f.pp=s.pp;f.enabled=true;f.seam=true;f.lazy=true;
        f.configure=symbol<decltype(f.configure)>(runtime,"x3m_motion_output_fixture_configure",true);
        f.vs_words=vs;f.ps_words=ps;f.vs_hash=fnv(vs.data(),vs.size()*4);f.ps_hash=fnv(ps.data(),ps.size()*4);f.flat_hash=fnv(flat_program,sizeof flat_program);
        f.d.p=key;s.device.p=nullptr;Com<IDirect3DIndexBuffer9> indices;
        if(routed){f.create(false);f.frame_begin();f.scope(&f.a);
            api(key->CreateIndexBuffer(6,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices.p,nullptr),"guard actual index child");void* map=nullptr;const WORD values[]={0,1,2};
            api(indices->Lock(0,0,&map,0),"guard actual indices Lock");std::memcpy(map,values,6);api(indices->Unlock(),"guard actual indices Unlock");
            api(key->SetIndices(indices.p),"guard actual indices bind");api(key->SetVertexShader(f.vs.p),"guard actual VS");api(key->SetPixelShader(f.ps.p),"guard actual PS");f.rows(0,0,0);
            drop_resources(f);indices.reset();
        }
        arm(a,key);require(a.query_arm(key,2,3)==S_OK,"arm actual guarded last-app Release");
        const auto before=a.state(key);
        if(routed)require(before.motion_refs+before.bloom_refs>0,"routed final-release case has actual renderer-owned references");
        guard_seed();LatticeGuardCpu incoming{},outgoing{};guard_cpu(incoming);
        const HRESULT hr=key->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1);guard_cpu(outgoing);f.d.p=nullptr;
        LatticeGuardResult query{};require(a.query_read(key,&query)==S_OK,"guarded draw CPU result after retirement");
        require(hr==query.native_result&&query.native_calls==1&&query.native_query_depth==0,"query final Release preserves native submission once outside guard");
        require(guard_same_cpu(incoming,query.incoming)&&guard_same_cpu(outgoing,query.outgoing),"query native incoming outgoing CPU and LastError preserved");
        require(query.pin_acquires==1&&query.pin_releases==1&&!query.query_restores,"query and draw pins remain balanced without reentrant restoration");
        const auto after=a.state(key);
        std::printf("CAPTURE_QUERY routed=%u renderer_before=%u live=%u retired=%u query_retired=%u\n",routed,before.motion_refs+before.bloom_refs,after.live,after.retired,query.retired);
        require(!after.live&&after.retired==1&&query.retired==1,"draw pin retirement accounts for actual renderer and upload references");
        require(a.query_disarm(key)==S_OK,"retired query seam cleanup");s.factory_close();
    }
}
}
int main(int argc,char** argv){try{
    require(argc==5,"usage: fixture proxy.dll vs.bin ps.bin native-d3dx.dll");
    const HMODULE runtime=LoadLibraryA(argv[1]);require(runtime!=nullptr,"load frozen proxy fixture DLL");CaptureApi a(runtime);
    char system[MAX_PATH]{};GetSystemDirectoryA(system,MAX_PATH);const std::string path=std::string(system)+"\\d3d9.dll";
    HMODULE native=LoadLibraryExA(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);require(native&&native!=runtime,"actual native D3D backend separate from proxy");
    auto create=symbol<NativeCreate>(native,"Direct3DCreate9",true);
    HMODULE d3dx=LoadLibraryA(argv[4]);require(d3dx!=nullptr,"load explicit native D3DX");char dllpath[MAX_PATH]{};GetModuleFileNameA(d3dx,dllpath,MAX_PATH);
    require(GetProcAddress(d3dx,"__wine_spec_dll_entry")==nullptr,"D3DX fixture is not builtin");
    std::printf("CAPTURE_D3DX path=%s builtin=0\n",dllpath);
    const unsigned char bytes[]={0x8b,0x47,0x14,0x8b,0x08,0x8d,0x54,0x24,0x10,0x52,0x8b,0x54,0x24,0x1c,0x52,0x56,0x55,0x50,0x8b,0x41,0x30,0xff,0xd0,0x8b,0xf8,0x81,0xff,0x0e,0x00,0x07,0x80};
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));require(code!=nullptr,"fixture site storage");std::memcpy(code,bytes,sizeof bytes);
    require(a.hook(code+18),"install actual A gate on fixture-owned validated site"); // site never executes; accepted A ABI evidence reused
    HWND window=CreateWindowExA(0,"STATIC","capture lifecycle",WS_OVERLAPPEDWINDOW,0,0,96,96,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);require(window!=nullptr,"fixture window");
    const auto vs=load(argv[2]),ps=load(argv[3]);require(fnv(vs.data(),vs.size()*4)==0x53a0a641107ed76cull&&fnv(ps.data(),ps.size()*4)==0x8759c7838bbc86c2ull,"reviewed renderer shader inputs");
    basic(a,create,window);resets(a,create,window);deferred(a,create,window,d3dx);guarded_release(a,create,window,runtime,vs,ps);
    DestroyWindow(window);
    std::printf("RESULT PASS phase=capture_lifecycle_b2a checks=%u actual_capture=1 ownership=1 payload_serialization=0 inherited_callback_seh_tested=0\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"RESULT FAIL phase=capture_lifecycle_b2a checks=%u error=%s\n",checks,e.what());return 1;}}
