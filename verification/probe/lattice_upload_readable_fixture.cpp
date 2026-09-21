// Focused prerequisite fixture: actual D3D9 ownership and readable preparation.
// Reuse unchanged native fault/CPU witnesses, without running the old suite.
#define main unused_finite_fixture_main
#include "finite_upload_fixture.cpp"
#undef main

namespace {
struct ReadableSession {
    Com<IDirect3D9> factory;
    Com<IDirect3DDevice9> device;
    D3DPRESENT_PARAMETERS pp{};
    ReadableSession(Create create,HWND window,UINT budget=0,UINT limit=4096) {
        auto* native=create(D3D_SDK_VERSION);check(native!=nullptr,"readable factory");
        Options options;options.track_buffer_writes=true;options.track_buffer_lock_attempts=true;
        options.track_execution_state=true;options.prepare_readable_managed_uploads=true;
        options.finite_payload_budget=budget;options.finite_sidecar_limit=limit;
        ok(wrap_factory(native,&factory.p,options),"wrap readable factory");
        pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
        pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
        pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        ok(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                 &pp,&device.p),"readable device");
    }
    FiniteUploadStatistics statistics() {
        FiniteUploadStatistics result;ok(get_finite_upload_statistics(device.p,&result),"readable statistics");return result;
    }
    void empty(unsigned sidecars) {
        const auto s=statistics();
        check(!s.requested&&!s.active&&s.sidecars==sidecars,"metadata exists without finite mode");
        check(!s.payload_bytes&&!s.peak_payload_bytes&&!s.global_payload_bytes&&!s.scans&&
              !s.classified_bytes&&!s.publications&&!s.uploads,"zero atlas allocation and typed payload work");
    }
    void close() {
        empty(0);
        check(device.p->Release()==0,"readable device no retained references");device.p=nullptr;
        check(factory.p->Release()==0,"readable factory no retained references");factory.p=nullptr;
    }
};
void metadata(Create create,HWND window) {
    ReadableSession s(create,window);Com<IDirect3DVertexBuffer9> vb;Com<IDirect3DIndexBuffer9> ib;
    ok(s.device->CreateVertexBuffer(48,8,D3DFVF_XYZ,D3DPOOL_MANAGED,&vb.p,nullptr),"readable VB");
    ok(s.device->CreateIndexBuffer(12,8,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib.p,nullptr),"readable IB");
    description(vb.p);D3DINDEXBUFFER_DESC app{},native{};
    ok(ib->GetDesc(&app),"readable IB application desc");
    ok(borrowed_native_buffer_for_lock_contract(ib.p)->GetDesc(&native),"readable IB actual desc");
    check(app.Usage==8&&native.Usage==0,"IB requested Usage independent from actual readable backing");
    app.Usage=native.Usage;check(!std::memcmp(&app,&native,sizeof app),"IB remaining descriptor unchanged");
    s.empty(2);
    // Content deliberately includes NaN-looking and arbitrary words. This mode
    // does not interpret them, and producer writes remain byte-identical.
    const std::uint32_t v[12]={0x7fa00001,0xdeadbeef,0x7f800000,0xffffffff};
    const std::uint16_t i[6]={2,1,0,0,1,2};void* map=nullptr;
    ok(vb->Lock(0,0,&map,0x800),"original full VB Lock");std::memcpy(map,v,sizeof v);
    ok(vb->Unlock(),"original VB Unlock");
    ok(ib->Lock(0,0,&map,0x800),"original full IB Lock");std::memcpy(map,i,sizeof i);
    ok(ib->Unlock(),"original IB Unlock");s.empty(2);
    check(!positions(vb.p).requested,"finite position interface remains disabled");
    // These are authorized fixture-only validation reads, not observer reads.
    ok(vb->Lock(0,0,&map,D3DLOCK_READONLY),"fixture verify VB Lock");
    check(!std::memcmp(map,v,sizeof v),"exact VB payload preserved");ok(vb->Unlock(),"fixture verify VB Unlock");
    ok(ib->Lock(0,0,&map,D3DLOCK_READONLY),"fixture verify IB Lock");
    check(!std::memcmp(map,i,sizeof i),"exact IB payload preserved");ok(ib->Unlock(),"fixture verify IB Unlock");
    s.empty(2);const auto old=s.statistics().generation;
    const HRESULT reset=s.device->Reset(&s.pp);std::printf("RESET hr=%08lx\n",static_cast<unsigned long>(reset));
    ok(reset,"readable metadata Reset");check(s.statistics().generation>old,"Reset advances metadata generation");
    description(vb.p);s.empty(2);
    vb.reset();ib.reset();s.close();
}
struct NoReadMap {
    static inline void* forbidden=nullptr;
    static inline unsigned locks=0,unlocks=0;
    static HRESULT WINAPI lock(IUnknown*,UINT offset,UINT size,void** out,DWORD flags) {
        ++locks;if(offset||size||flags!=0x800||!out)return D3DERR_INVALIDCALL;
        *out=forbidden;SetLastError(0x13572468);return S_OK;
    }
    static HRESULT WINAPI unlock(IUnknown*) {++unlocks;SetLastError(0x24681357);return S_OK;}
    IUnknown* resource;void** old;void* table[14];
    explicit NoReadMap(IUnknown* object):resource(object),old(*reinterpret_cast<void***>(object)) {
        forbidden=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);
        check(forbidden!=nullptr,"inaccessible mapping allocation");locks=unlocks=0;
        std::memcpy(table,old,sizeof table);auto l=&lock;auto u=&unlock;
        std::memcpy(&table[11],&l,sizeof l);std::memcpy(&table[12],&u,sizeof u);
        *reinterpret_cast<void***>(resource)=table;
    }
    ~NoReadMap(){*reinterpret_cast<void***>(resource)=old;VirtualFree(forbidden,0,MEM_RELEASE);forbidden=nullptr;}
};
void no_payload_read(Create create,HWND window) {
    // Nonzero atlas budget makes this control distinguish disabled scanning
    // from merely refusing a legacy allocation because the budget is zero.
    ReadableSession s(create,window,32u*1024u*1024u);Com<IDirect3DVertexBuffer9> vb;
    ok(s.device->CreateVertexBuffer(48,8,D3DFVF_XYZ,D3DPOOL_MANAGED,&vb.p,nullptr),"no-read VB");
    {
        NoReadMap spy(borrowed_native_buffer_for_lock_contract(vb.p));void* p=nullptr;
        const HRESULT locked=vb->Lock(0,0,&p,0x800);const DWORD lock_error=GetLastError();
        const HRESULT unlocked=vb->Unlock();const DWORD unlock_error=GetLastError();
        check(locked==S_OK&&unlocked==S_OK&&p==NoReadMap::forbidden,"mapping forwarded without any payload read");
        check(NoReadMap::locks==1&&NoReadMap::unlocks==1,"no extra native Lock or Unlock");
        check(lock_error==0x13572468&&unlock_error==0x24681357,"native Lock and Unlock LastError preserved");
    }
    s.empty(1);vb.reset();s.close();
}
void fallback(Create create,HWND window) {
    for(bool attach:{false,true})for(bool clear:{false,true}) {
        ReadableSession s(create,window);Com<IDirect3DVertexBuffer9> vb;
        {
            CreationFault fault(borrowed_native_device(s.device.p),clear,attach);
            State original;unsigned short cw=0x077f;unsigned mx=0x3fa0;
            asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");
            SetLastError(0x22334455);State incoming,selected;
            CreationFault::expected=&incoming;CreationFault::selected=&selected;
            const HRESULT hr=s.device->CreateVertexBuffer(48,8,D3DFVF_XYZ,D3DPOOL_MANAGED,&vb.p,nullptr);
            State after;original.restore();
            check(hr==S_OK&&vb.p&&CreationFault::calls==2&&CreationFault::usages[0]==0&&CreationFault::usages[1]==8,
                  "readable failure retries exact original creation Usage");
            check(CreationFault::retry_state&&same_state(after,selected),"fallback full incoming and selected outgoing CPU state preserved");
        }
        D3DVERTEXBUFFER_DESC a{},b{};ok(vb->GetDesc(&a),"fallback app descriptor");
        ok(borrowed_native_buffer_for_lock_contract(vb.p)->GetDesc(&b),"fallback actual descriptor");
        check(a.Usage==8&&b.Usage==8,"fallback remains actual WRITEONLY");s.empty(0);vb.reset();s.close();
    }
    ReadableSession s(create,window,0,0);Com<IDirect3DVertexBuffer9> vb;
    ok(s.device->CreateVertexBuffer(48,8,D3DFVF_XYZ,D3DPOOL_MANAGED,&vb.p,nullptr),"metadata budget fallback");
    D3DVERTEXBUFFER_DESC d{};ok(borrowed_native_buffer_for_lock_contract(vb.p)->GetDesc(&d),"metadata budget actual desc");
    check(d.Usage==8,"metadata admission failure rolls readable conversion back");s.empty(0);vb.reset();s.close();
}
void incompatible(Create create) {
    for(unsigned variant=0;variant<3;++variant) {
        auto* native=create(D3D_SDK_VERSION);check(native!=nullptr,"invalid-option factory");
        Options o;o.prepare_readable_managed_uploads=true;o.track_buffer_writes=variant!=0;
        o.capture_finite_positions=variant==1;o.locked_prefix_bounds=variant==2;
        IDirect3D9* output=nullptr;check(wrap_factory(native,&output,o)==E_INVALIDARG&&!output,
                                      "readable mode rejects missing tracking or typed scanner");
        check(native->Release()==0,"rejected wrap preserves caller ownership");
    }
}
}
#ifdef X3M_LATTICE_UPLOAD_READABLE_EMBEDDED
#define main unused_readable_fixture_main
#endif
int main() {
    try {
        HMODULE module=LoadLibraryA("C:\\windows\\system32\\d3d9.dll");check(module!=nullptr,"load D3D9");
        Create create=nullptr;auto entry=GetProcAddress(module,"Direct3DCreate9");std::memcpy(&create,&entry,sizeof create);
        check(create!=nullptr,"D3D9 entry");
        HWND window=CreateWindowExA(0,"STATIC","lattice readable fixture",WS_OVERLAPPEDWINDOW,
                                   0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);check(window!=nullptr,"window");
        incompatible(create);metadata(create,window);no_payload_read(create,window);fallback(create,window);
        DestroyWindow(window);FreeLibrary(module);
        std::printf("RESULT PASS phase=readable_metadata checks=%u clone_adapter_tested=0 seh_tested=0\n",checks);return 0;
    }catch(const std::exception& e){std::printf("RESULT FAIL %s checks=%u\n",e.what(),checks);return 1;}
}

#ifdef X3M_LATTICE_UPLOAD_READABLE_EMBEDDED
#undef main
#endif
