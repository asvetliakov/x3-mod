// Real production wrapper with test-only native entry/result barriers. No game.
#define X3M_BUFFER_LOCK_FIXTURE
#include "../../src/ownership/d3d9_ownership.cpp"
#include <cstdio>
#include <stdexcept>
#include <thread>
using namespace x3m::ownership;
unsigned checks=0;
void check(bool condition,const char* label){++checks;if(!condition){std::printf("FAIL %s\n",label);throw std::runtime_error(label);}}
void ok(HRESULT hr,const char* label){check(SUCCEEDED(hr),label);}
template<class T>struct Com {T* p=nullptr;~Com(){reset();}void reset(){if(p)p->Release();p=nullptr;}T* operator->(){return p;}};
BufferLockView view(IDirect3DResource9* buffer){BufferLockView out;ok(get_buffer_lock_view(buffer,&out),"counter snapshot");return out;}
struct State {
    unsigned char fp[108];unsigned mxcsr;DWORD error;
    State():error(GetLastError()){asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1":"=m"(fp),"=m"(mxcsr)::"memory");}
    void restore()const{asm volatile("frstor %0\n\tldmxcsr %1"::"m"(fp),"m"(mxcsr):"memory");SetLastError(error);}
    bool same(const State& s)const{return !std::memcmp(fp,s.fp,sizeof fp)&&mxcsr==s.mxcsr&&error==s.error;}
};
void seed(){unsigned short cw=0x077f;unsigned mx=0x3fa0;asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");SetLastError(0x12345678);}
struct Gate {
    HANDLE entered=CreateEventA(nullptr,TRUE,FALSE,nullptr),resume=CreateEventA(nullptr,TRUE,FALSE,nullptr);
    ~Gate(){CloseHandle(entered);CloseHandle(resume);}
    void wait(){check(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0,"native entry reached");}
    void hold(){SetEvent(entered);WaitForSingleObject(resume,5000);}
    void go(){SetEvent(resume);}
};
struct NativePatch {
    using Lock=HRESULT(WINAPI*)(IUnknown*,UINT,UINT,void**,DWORD);
    using Unlock=HRESULT(WINAPI*)(IUnknown*);
    IUnknown* object;void** old;void* table[14];
    static inline NativePatch* active=nullptr;
    Gate* gate=nullptr;bool fail=false;State incoming,outgoing;
    static HRESULT WINAPI lock(IUnknown* object,UINT offset,UINT size,void** data,DWORD flags){
        auto& p=*active;p.incoming=State{};
        if(p.gate)p.gate->hold();
        HRESULT hr=p.fail?E_ACCESSDENIED:reinterpret_cast<Lock>(p.old[11])(object,offset,size,data,flags);
        seed();SetLastError(0x44556677);p.outgoing=State{};return hr;
    }
    static HRESULT WINAPI unlock(IUnknown* object){
        auto& p=*active;p.incoming=State{};
        if(p.gate)p.gate->hold();
        HRESULT hr=p.fail?E_ACCESSDENIED:reinterpret_cast<Unlock>(p.old[12])(object);
        seed();SetLastError(0x44556677);p.outgoing=State{};return hr;
    }
    NativePatch(IUnknown* value,bool unlocking=false):object(value),old(*reinterpret_cast<void***>(value)){
        std::memcpy(table,old,sizeof table);table[unlocking?12:11]=unlocking?reinterpret_cast<void*>(&unlock):reinterpret_cast<void*>(&lock);
        active=this;*reinterpret_cast<void***>(object)=table;
    }
    ~NativePatch(){*reinterpret_cast<void***>(object)=old;active=nullptr;}
};
struct ResetPatch {
    IDirect3DDevice9* object;void** old;void* table[119];Gate gate;
    static inline ResetPatch* active=nullptr;
    static HRESULT WINAPI reset(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*){active->gate.hold();return E_FAIL;}
    explicit ResetPatch(IDirect3DDevice9* value):object(value),old(*reinterpret_cast<void***>(value)){
        std::memcpy(table,old,sizeof table);table[16]=reinterpret_cast<void*>(&reset);
        active=this;*reinterpret_cast<void***>(object)=table;
    }
    ~ResetPatch(){*reinterpret_cast<void***>(object)=old;active=nullptr;}
};
Gate* publication_gate=nullptr;DWORD publication_thread=0;
void publication(bool){if(GetCurrentThreadId()==publication_thread)publication_gate->hold();}
struct ForeignData final : IUnknown {
    ULONG refs=1,adds=0;
    HRESULT WINAPI QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;if(iid!=IID_IUnknown)return E_NOINTERFACE;
        AddRef();*out=this;return S_OK;
    }
    ULONG WINAPI AddRef() override {++adds;return ++refs;}
    ULONG WINAPI Release() override {auto result=--refs;if(!result)delete this;return result;}
};
struct Session {
    Com<IDirect3D9> api;Com<IDirect3DDevice9> device;D3DPRESENT_PARAMETERS pp{};
    Session(HWND window,bool counters=true){
        HMODULE module=LoadLibraryA("d3d9.dll");check(module!=nullptr,"load D3D9");
        IDirect3D9*(WINAPI* create)(UINT)=nullptr;
        auto address=GetProcAddress(module,"Direct3DCreate9");std::memcpy(&create,&address,sizeof create);
        auto* native=create(D3D_SDK_VERSION);check(native!=nullptr,"factory");
        Options options;options.track_buffer_writes=true;options.track_buffer_lock_attempts=counters;
        ok(wrap_factory(native,&api.p,options),"wrap factory");
        pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
        pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
        ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&device.p),"device");
    }
};
enum class ColdOperation { AddRef, Release, QueryInterface, Snapshot };
struct ColdCall {
    ColdOperation operation;IUnknown* callback=nullptr;IDirect3DResource9* buffer=nullptr;
    State before,after;ULONG count=0;HRESULT hr=E_FAIL;IUnknown* returned=nullptr;BufferLockView snapshot;
    explicit ColdCall(ColdOperation op,IUnknown* cb=nullptr,IDirect3DResource9* resource=nullptr)
        :operation(op),callback(cb),buffer(resource){}
};
// Raw Win32 thread entry, with no libstdc++ thread or EH/TLS warmup before the
// tested call. Compare captured state on the parent thread, after ordinary return.
#pragma GCC push_options
#pragma GCC optimize("no-exceptions")
__attribute__((noinline)) DWORD WINAPI cold_call(void* raw) {
    auto& call=*static_cast<ColdCall*>(raw);
    seed();call.before=State{};
    switch(call.operation){
    case ColdOperation::AddRef:call.count=call.callback->AddRef();break;
    case ColdOperation::Release:call.count=call.callback->Release();break;
    case ColdOperation::QueryInterface:call.hr=call.callback->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&call.returned));break;
    case ColdOperation::Snapshot:call.hr=get_buffer_lock_view(call.buffer,&call.snapshot);break;
    }
    call.after=State{};return 0;
}
#pragma GCC pop_options
void run_cold(ColdCall& call){
    HANDLE thread=CreateThread(nullptr,0,cold_call,&call,0,nullptr);check(thread!=nullptr,"fresh CreateThread");
    check(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"fresh thread completed");CloseHandle(thread);
    check(call.before.same(call.after),"cold native callback or direct snapshot preserves x87/MXCSR/LastError");
}
void cold_boundaries(HWND window){
    Session session(window);Com<IDirect3DVertexBuffer9> vb;
    ok(session.device->CreateVertexBuffer(48,0,0,D3DPOOL_MANAGED,&vb.p,nullptr),"cold-boundary VB");
    auto* native=borrowed_native_buffer_for_lock_contract(vb.p);
    IUnknown* callback=nullptr;DWORD bytes=sizeof callback;
    ok(native->GetPrivateData(lock_sidecar_guid,&callback,&bytes),"retain callback for cold thread");
    auto* side=static_cast<LockSidecar*>(callback);
    const auto references=side->refs.load();
    ColdCall add{ColdOperation::AddRef,callback};run_cold(add);
    check(add.count==references+1&&side->authentication_adds.load()==0,"cold AddRef has no authentication witness");
    ColdCall release{ColdOperation::Release,callback};run_cold(release);
    check(release.count==references,"cold Release balances reference");
    ColdCall query{ColdOperation::QueryInterface,callback};run_cold(query);
    check(query.hr==S_OK&&query.returned==callback,"cold QueryInterface identity");query.returned->Release();
    side->authentication_thread.store(GetCurrentThreadId());
    ColdCall foreign_thread{ColdOperation::AddRef,callback};run_cold(foreign_thread);
    check(side->authentication_adds.load()==0,"other-thread AddRef cannot authenticate adoption");callback->Release();
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        check(acquire_lock_sidecar(static_cast<Device*>(application_nodes.at(vb.p)->parent),native,side->identity)==nullptr&&
            side->authentication_adds.load()==0&&side->authentication_thread.load()==GetCurrentThreadId(),
            "nested authentication refuses without changing outer witness");}
    side->authentication_thread.store(0);
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        auto* acquired=acquire_lock_sidecar(static_cast<Device*>(application_nodes.at(vb.p)->parent),native,side->identity);
        check(acquired==side&&side->authentication_thread.load()==0&&side->authentication_adds.load()==0,
            "ordinary authentication publishes and clears side-local witness");release_lock_sidecar(acquired);}
    ColdCall snapshot{ColdOperation::Snapshot,nullptr,vb.p};run_cold(snapshot);
    check(snapshot.hr==S_OK&&snapshot.snapshot.known,"cold direct snapshot succeeds");
    buffer_snapshot_fixture_fail=true;
    ColdCall failed{ColdOperation::Snapshot,nullptr,vb.p};run_cold(failed);
    buffer_snapshot_fixture_fail=false;
    check(failed.hr==S_FALSE&&!failed.snapshot.known&&failed.snapshot.status==E_FAIL,"snapshot core exception returns unknown through ABI shell");
    vb.reset(); // The retained CPU callback is now the allocation record's last ref.
    ColdCall final_release{ColdOperation::Release,callback};run_cold(final_release);
    check(final_release.count==0,"cold final Release only enqueues retirement");
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);drain_lock_retired();}
}
void exercise(HWND window){
    Session session(window);Com<IDirect3DVertexBuffer9> vb;
    ok(session.device->CreateVertexBuffer(48,0,0,D3DPOOL_MANAGED,&vb.p,nullptr),"VB");
    auto initial=view(vb.p);check(initial.known&&initial.allocation_id&&initial.generation,"pristine identity");
    auto* native=borrowed_native_buffer_for_lock_contract(vb.p);check(native!=nullptr,"native endpoint");
    void* mapping=nullptr;HRESULT result=E_FAIL;
    {Gate gate;NativePatch patch(native);patch.gate=&gate;
        std::thread worker([&]{result=vb->Lock(0,48,&mapping,0);});gate.wait();
        auto during=view(vb.p);check(during.attempt_serial==1&&during.in_flight_locks==1&&!during.pending_locks&&!during.revision&&!during.known,"Lock before return visible");
        gate.go();worker.join();ok(result,"native Lock result");
    }
    auto mapped=view(vb.p);check(mapping&&mapped.pending_locks==1&&!mapped.in_flight_locks&&mapped.revision==1&&!mapped.known,"returned map visible");
    {Gate gate;NativePatch patch(native,true);patch.gate=&gate;
        std::thread worker([&]{result=vb->Unlock();});gate.wait();
        auto during=view(vb.p);check(during.in_flight_unlocks==1&&during.pending_locks==1&&!during.known,"Unlock before return visible");
        gate.go();worker.join();ok(result,"native Unlock result");
    }
    check(view(vb.p).known,"completed publication quiet");
    // Hold after metadata publication, then submit an overlapping failed Lock.
    {Gate gate;publication_gate=&gate;buffer_publication_fixture_hook=publication;
        std::thread worker([&]{publication_thread=GetCurrentThreadId();result=vb->Lock(0,48,&mapping,D3DLOCK_READONLY);});
        gate.wait();auto during=view(vb.p);
        check(during.in_flight_locks==1&&during.pending_locks==1&&during.revision==1&&!during.known,"publication still in flight");
        {NativePatch patch(native);patch.fail=true;void* untouched=reinterpret_cast<void*>(0x1234);
            check(vb->Lock(8,12,&untouched,D3DLOCK_NOOVERWRITE)==E_ACCESSDENIED&&untouched==reinterpret_cast<void*>(0x1234),"failed concurrent Lock output preserved");}
        auto raced=view(vb.p);check(raced.in_flight_locks==1&&raced.failed_locks==1&&raced.pending_locks==1&&raced.revision==1,"racing completion does not lose state");
        gate.go();worker.join();buffer_publication_fixture_hook=nullptr;ok(result,"READONLY native Lock");
    }
    ok(vb->Unlock(),"READONLY unlock");auto readonly=view(vb.p);
    check(readonly.known&&readonly.attempt_serial==3&&readonly.readonly_attempts==1&&readonly.nooverwrite_attempts==1&&readonly.revision==1,"attempt distinct from content write");
    {State original;NativePatch patch(native);patch.fail=true;seed();State before;void* untouched=reinterpret_cast<void*>(0x1234);
        before.restore();HRESULT hr=vb->Lock(1,2,&untouched,D3DLOCK_DISCARD);State after;original.restore();
        check(hr==E_ACCESSDENIED&&untouched==reinterpret_cast<void*>(0x1234),"failed Lock HRESULT/output");
        check(patch.incoming.same(before)&&patch.outgoing.same(after),"Lock native incoming/outgoing FP and LastError");
    }
    // Managed native binding retains allocation while the application shell dies.
    auto retained=view(vb.p);ok(session.device->SetStreamSource(0,vb.p,0,12),"binding retains allocation");vb.reset();
    UINT offset=0,stride=0;ok(session.device->GetStreamSource(0,&vb.p,&offset,&stride),"recreate wrapper");
    auto recreated=view(vb.p);check(recreated.allocation_id==retained.allocation_id&&recreated.attempt_serial==retained.attempt_serial,"recreation shares allocation counters");
    ok(session.device->SetStreamSource(0,nullptr,0,0),"unbind");
    {ResetPatch patch(borrowed_native_device(session.device.p));
        std::thread worker([&]{result=session.device->Reset(&session.pp);});patch.gate.wait();
        auto during=view(vb.p);check(!during.known&&during.generation>initial.generation,"Reset entry generation visible before native return");
        patch.gate.go();worker.join();check(result==E_FAIL&&!view(vb.p).known,"failed Reset remains unavailable");
    }
    ok(session.device->Reset(&session.pp),"Reset");auto reset=view(vb.p);
    check(reset.known&&reset.allocation_id==initial.allocation_id&&reset.generation>initial.generation&&reset.attempt_serial==recreated.attempt_serial,"Reset changes generation without erasing counters");
    // Saturation injection uses the actual production allocation record.
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);application_nodes.at(vb.p)->lock_sidecar->observation.attempt_serial=UINT64_MAX;}
    {NativePatch patch(borrowed_native_buffer_for_lock_contract(vb.p));patch.fail=true;vb->Lock(0,0,&mapping,0);}
    check(view(vb.p).saturated&&!view(vb.p).known,"production saturation veto");
    auto old_id=view(vb.p).allocation_id;vb.reset();
    ok(session.device->CreateVertexBuffer(48,0,0,D3DPOOL_MANAGED,&vb.p,nullptr),"replacement allocation");
    check(view(vb.p).allocation_id!=old_id&&view(vb.p).known,"new allocation never inherits retired serial");
    ok(vb->Lock(0,48,&mapping,0),"write before failed Unlock");
    {State original;NativePatch patch(borrowed_native_buffer_for_lock_contract(vb.p),true);patch.fail=true;seed();State before;
        before.restore();HRESULT hr=vb->Unlock();State after;original.restore();
        check(hr==E_ACCESSDENIED&&patch.incoming.same(before)&&patch.outgoing.same(after),"failed Unlock preserves native CPU/LastError");}
    auto failed=view(vb.p);check(failed.failed_unlocks==1&&failed.pending_locks==1&&failed.ambiguous&&!failed.known&&!failed.in_flight_unlocks,"failed Unlock keeps pending and taint");
    ok(vb->Unlock(),"close native map");
    Com<IDirect3DIndexBuffer9> ib;ok(session.device->CreateIndexBuffer(12,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib.p,nullptr),"IB");
    ok(ib->Lock(0,12,&mapping,0),"IB Lock");ok(ib->Unlock(),"IB Unlock");auto index=view(ib.p);
    check(index.known&&index.attempt_serial==1&&index.unlock_serial==1&&index.revision==1,"IB uses same bookends");
    check(get_buffer_lock_view(reinterpret_cast<IDirect3DResource9*>(native),&initial)==E_INVALIDARG,"raw pointer snapshot rejected");
    // Application metadata mutation cannot leave a cached authenticated record known.
    ok(ib->FreePrivateData(lock_sidecar_guid),"remove sidecar through application boundary");
    check(!view(ib.p).known&&!view(vb.p).known,"sidecar tamper permanently vetoes device counter evidence");
    // Keep the old CPU entry alive while native private data is replaced. Without
    // the failed-tracking adoption guard, authenticating it would call foreign AddRef.
    IUnknown* old_side=nullptr;DWORD bytes=sizeof old_side;
    auto* vb_native=borrowed_native_buffer_for_lock_contract(vb.p);
    ok(vb_native->GetPrivateData(lock_sidecar_guid,&old_side,&bytes),"retain old counter sidecar");
    auto* foreign=new ForeignData;
    ok(vb->SetPrivateData(lock_sidecar_guid,foreign,sizeof(IUnknown*),D3DSPD_IUNKNOWN),"replace sidecar with foreign callback");
    const auto adds=foreign->adds;
    ok(session.device->SetStreamSource(0,vb.p,0,12),"retain tampered native allocation");vb.reset();
    ok(session.device->GetStreamSource(0,&vb.p,&offset,&stride),"recreate known-tampered wrapper");
    check(foreign->adds==adds&&!view(vb.p).known,"known tamper skips native authentication and foreign callback");
    ok(session.device->SetStreamSource(0,nullptr,0,0),"unbind tampered native allocation");
    old_side->Release();foreign->Release();
}
int main(){try{
    HWND window=CreateWindowExA(0,"STATIC","counter fixture",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    check(window!=nullptr,"window");cold_boundaries(window);exercise(window);
    {Session off(window,false);Com<IDirect3DVertexBuffer9> vb;const auto count=lock_sidecars_used;
        ok(off.device->CreateVertexBuffer(48,0,0,D3DPOOL_MANAGED,&vb.p,nullptr),"disabled VB");
        const auto entries=buffer_bookend_fixture_entries.load();
        void* mapping=nullptr;ok(vb->Lock(0,0,&mapping,0),"disabled Lock");ok(vb->Unlock(),"disabled Unlock");
        check(!view(vb.p).requested&&lock_sidecars_used==count,"disabled creates no counter sidecar");
        check(buffer_bookend_fixture_entries.load()==entries,"disabled bypasses both bookend helper entries");}
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);drain_lock_retired();check(lock_sidecars_used==0&&lock_sidecars.empty(),"all CPU sidecars retired without owner cycle");}
    DestroyWindow(window);std::printf("buffer_lock_bookends checks=%u failures=0\n",checks);return 0;
}catch(const std::exception& error){std::printf("ERROR %s checks=%u\n",error.what(),checks);return 1;}}
