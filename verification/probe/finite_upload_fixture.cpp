#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/ownership/d3d9_ownership.h"
#include "../../src/ownership/portable_managed_upload.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <thread>
#include <atomic>
using namespace x3m::ownership;
namespace x3m::ownership {void finite_fixture_index_controls(void(*)(bool,const char*),void(*)(IUnknown*));void finite_fixture_with_registry(void(*)(void*),void*);void finite_fixture_addref_callback(void(*)(void*),void*);}
unsigned checks=0;
void check(bool condition,const char* label){++checks;std::printf("CHECK %s %s\n",label,condition?"PASS":"FAIL");if(!condition)throw std::runtime_error(label);}
void ok(HRESULT hr,const char* label){check(SUCCEEDED(hr),label);}
template<class T>struct Com{T* p=nullptr;~Com(){reset();}void reset(){if(p)p->Release();p=nullptr;}T* operator->()const{return p;}};
using Create=IDirect3D9*(WINAPI*)(UINT);
const GUID sidecar_guid={0x03ee519d,0x9308,0x4a86,{0x9b,0x94,0xd3,0x7e,0x35,0xac,0x49,0xaa}};
struct Session{
    Com<IDirect3D9> factory;Com<IDirect3DDevice9> device;D3DPRESENT_PARAMETERS pp{};
    Session(Create create,HWND window,bool enabled=true,UINT budget=32u*1024u*1024u,UINT limit=4096,bool tracking=true){
        auto* raw=create(D3D_SDK_VERSION);check(raw!=nullptr,"factory");Options options;options.track_buffer_writes=tracking;options.capture_finite_positions=enabled;options.finite_payload_budget=budget;options.finite_sidecar_limit=limit;
        ok(wrap_factory(raw,&factory.p,options),"wrap factory");pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        ok(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device.p),"device");
    }
};
BufferContentView content(IDirect3DResource9* value){BufferContentView result;ok(get_buffer_content_view(value,&result),"content");return result;}
FiniteUploadStatistics stats(Session& s){FiniteUploadStatistics value;ok(get_finite_upload_statistics(s.device.p,&value),"statistics");return value;}
void make(Session& s,Com<IDirect3DVertexBuffer9>& vb,UINT size=48,DWORD usage=D3DUSAGE_WRITEONLY,D3DPOOL pool=D3DPOOL_MANAGED){ok(s.device->CreateVertexBuffer(size,usage,D3DFVF_XYZ,pool,&vb.p,nullptr),"create VB");}
void upload(IDirect3DVertexBuffer9* vb,const void* bytes,UINT size,UINT offset=0,DWORD flags=0){void* data=nullptr;ok(vb->Lock(offset,size,&data,flags),"write lock");check(data!=nullptr,"mapping");std::memcpy(data,bytes,size);ok(vb->Unlock(),"write unlock");}
FinitePositionView positions(IDirect3DVertexBuffer9* vb,UINT count=4,D3DDECLTYPE type=D3DDECLTYPE_FLOAT3,UINT stride=12,UINT offset=0){auto c=content(vb);FinitePositionRequest req;req.expected_revision=c.revision;req.vertex_count=count;req.stride=stride;req.position_offset=offset;req.position_type=type;FinitePositionView view;ok(get_finite_position_view(vb,req,&view),"position view");return view;}
struct State{unsigned char x87[108];unsigned mxcsr;DWORD error;State():error(GetLastError()){asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1":"=m"(x87),"=m"(mxcsr)::"memory");}void restore(){asm volatile("frstor %0\n\tldmxcsr %1"::"m"(x87),"m"(mxcsr):"memory");SetLastError(error);}};
void description(IDirect3DVertexBuffer9* wrapped,DWORD expected=D3DUSAGE_WRITEONLY){
    auto* native=borrowed_native_buffer_for_lock_contract(wrapped);D3DVERTEXBUFFER_DESC app{},back{};
    ok(wrapped->GetDesc(&app),"application VB descriptor");ok(native->GetDesc(&back),"native readable VB descriptor");
    check(app.Usage==expected&&back.Usage==(expected&~DWORD(D3DUSAGE_WRITEONLY)),"immutable original Usage masks only readable conversion");
    app.Usage=back.Usage;check(!std::memcmp(&app,&back,sizeof app),"all other VB descriptor fields preserved");
}
void normal(Create create,HWND window){
    Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);description(vb.p);
    const std::uint32_t finite[12]={0x3f800000,0x40000000,0x40400000,0,0,0,0,0,0,0,0,0};
    check(positions(vb.p).state==FiniteStatus::Unknown,"unobserved initial bytes unknown");
    upload(vb.p,finite,sizeof finite);auto view=positions(vb.p);check(view.state==FiniteStatus::Finite&&view.status==S_OK&&view.generation!=0,"full Float3 finite");
    // Test-only native bypass: wrapper pending/revision cannot reveal this map.
    // Standard D3D9 cannot discover it. Explicit notification retires the snapshot.
    auto* native_open=borrowed_native_buffer_for_lock_contract(vb.p);
    check(native_open!=nullptr,"native open-map control endpoint");
    const auto before_native_map=content(vb.p);void* native_mapping=nullptr;
    ok(invalidate_native_buffer_evidence(vb.p),"notify trusted native mutation before mapping");
    ok(native_open->Lock(0,48,&native_mapping,0),"direct native write map control");
    const auto during_native_map=content(vb.p);
    check(native_mapping&&during_native_map.known&&!during_native_map.pending_locks&&
          during_native_map.revision==before_native_map.revision+1,
          "notification advances wrapper revision with no invented pending map");
    const auto native_open_view=positions(vb.p);
    check(native_open_view.state==FiniteStatus::Unknown&&native_open_view.reason==FiniteEvidenceReason::NativeContract,
          "explicit invalidation rejects native map despite wrapper pending zero");
    ok(native_open->Unlock(),"direct native control closes mapping");
    check(positions(vb.p).state==FiniteStatus::Unknown,"native closure alone cannot restore invalidated finite evidence");
    upload(vb.p,finite,sizeof finite);
    check(positions(vb.p).state==FiniteStatus::Finite,"fresh observed full upload restores finite evidence after native map refusal");
    auto baseline=stats(s);positions(vb.p);check(stats(s).query_cache_hits>baseline.query_cache_hits,"query cache hit");
    void* mapped=nullptr;ok(vb->Lock(0,12,&mapped,0),"partial lock");check(positions(vb.p).state==FiniteStatus::Unknown,"pending query unknown");std::memcpy(mapped,finite,12);ok(vb->Unlock(),"partial unlock");check(positions(vb.p).state==FiniteStatus::Finite,"preserving partial retains untouched cells");
    std::uint32_t nan=0x7fc00001;upload(vb.p,&nan,4,4);check(positions(vb.p).state==FiniteStatus::NonFinite,"NaN XYZ rejected");upload(vb.p,finite,sizeof finite);check(positions(vb.p).state==FiniteStatus::Finite,"full rewrite recovers observed bytes");
    Com<IDirect3DVertexBuffer9> half;make(s,half,32);std::uint16_t h[16]={0x3c00,0x4000,0x4200,0x7c00,0,0,0,0x7e00,0,0,0,0,0,0,0,0};upload(half.p,h,sizeof h);check(positions(half.p,4,D3DDECLTYPE_FLOAT16_4,8).state==FiniteStatus::Finite,"Half4 W nonfinite ignored");h[2]=0x7c00;upload(half.p,h,sizeof h);check(positions(half.p,4,D3DDECLTYPE_FLOAT16_4,8).state==FiniteStatus::NonFinite,"Half4 XYZ infinity rejected");
    auto raw=borrowed_native_buffer_for_lock_contract(vb.p);check(raw!=nullptr,"native borrowed");
    ok(s.device->SetStreamSource(0,vb.p,0,12),"retain native binding");vb.reset();UINT offset=0,stride=0;ok(s.device->GetStreamSource(0,&vb.p,&offset,&stride),"wrapper recreated");check(positions(vb.p).state==FiniteStatus::Finite,"allocation evidence survives wrapper recreation");description(vb.p);ok(s.device->SetStreamSource(0,nullptr,0,0),"unbind");
    auto old=positions(vb.p).generation;ok(s.device->Reset(&s.pp),"managed Reset");check(positions(vb.p).state==FiniteStatus::Unknown&&positions(vb.p).generation>old,"Reset retires atlas and advances generation");description(vb.p);upload(vb.p,finite,sizeof finite);check(positions(vb.p).state==FiniteStatus::Finite,"post Reset observed write recertifies");
    // Existing native API status changes remain native-owned; query itself preserves full caller FP/LE.
    State original;unsigned short cw=0x077f;unsigned mx=0x3fa0;asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");SetLastError(0x1234abcd);State before;FinitePositionRequest req;req.expected_revision=content(vb.p).revision;req.vertex_count=4;req.stride=12;req.position_type=D3DDECLTYPE_FLOAT3;before.restore();FinitePositionView out;get_finite_position_view(vb.p,req,&out);State after;original.restore();check(!std::memcmp(before.x87,after.x87,sizeof before.x87)&&before.mxcsr==after.mxcsr&&before.error==after.error,"query preserves live x87 and MXCSR and LastError");
    for(auto format:{D3DFMT_INDEX16,D3DFMT_INDEX32}){
        Com<IDirect3DIndexBuffer9> ib;UINT bytes=format==D3DFMT_INDEX16?12:24;ok(s.device->CreateIndexBuffer(bytes,D3DUSAGE_WRITEONLY,format,D3DPOOL_MANAGED,&ib.p,nullptr),"create IB");void* p=nullptr;ok(ib->Lock(0,0,&p,D3DLOCK_NOSYSLOCK),"IB lock");std::uint16_t i16[]={2,1,3,0,1,2};std::uint32_t i32[]={2,1,3,0,1,2};std::memcpy(p,format==D3DFMT_INDEX16?static_cast<void*>(i16):static_cast<void*>(i32),bytes);ok(ib->Unlock(),"IB unlock");IndexRangeRequest ir;ir.expected_revision=content(ib.p).revision;ir.format=format;ir.start_index=1;ir.index_count=3;IndexRangeView iv;ok(get_index_range_view(ib.p,ir,&iv),"index query");check(iv.known&&iv.minimum==0&&iv.maximum==3&&!iv.exact_range&&iv.generation==positions(vb.p).generation,"IB conservative subdraw certificate");
    }
    check(stats(s).publications>=9&&stats(s).classified_bytes>0&&stats(s).qualifier_ticks>0,"bounded counters record real work");
}
void gates(Create create,HWND window){
    const std::uint32_t values[12]={};
    {Session s(create,window,false);Com<IDirect3DVertexBuffer9> vb;make(s,vb);upload(vb.p,values,sizeof values);check(!positions(vb.p).requested&&stats(s).sidecars==0,"off mode no sidecars");}
    {Session s(create,window,true,0);Com<IDirect3DVertexBuffer9> vb;make(s,vb);upload(vb.p,values,sizeof values);check(positions(vb.p).state==FiniteStatus::Unknown&&stats(s).payload_bytes==0&&stats(s).sidecars==0,"payload budget refuses without residue");}
    {Session s(create,window,true,1024,1);Com<IDirect3DVertexBuffer9> a,b;make(s,a);make(s,b);upload(a.p,values,sizeof values);upload(b.p,values,sizeof values);check(positions(a.p).state==FiniteStatus::Finite&&positions(b.p).state==FiniteStatus::Unknown&&stats(s).sidecars==1,"sidecar count budget bounded");}
    {Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb,48,8,D3DPOOL_DEFAULT);upload(vb.p,values,sizeof values);check(positions(vb.p).state==FiniteStatus::Unknown&&stats(s).first_refusal.available,"unsupported default descriptor diagnosed");}
    {Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);upload(vb.p,values,sizeof values);void* p=nullptr;ok(vb->Lock(0,48,&p,D3DLOCK_NOOVERWRITE),"unsupported flag native Lock");std::memcpy(p,values,sizeof values);ok(vb->Unlock(),"unsupported flag native Unlock");check(positions(vb.p).state==FiniteStatus::Unknown,"unsupported flags invalidate");}
    {Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);upload(vb.p,values,sizeof values);void* p=nullptr;ok(vb->Lock(0,48,&p,0),"cross thread Lock");std::memcpy(p,values,sizeof values);HRESULT hr=E_FAIL;std::thread t([&]{hr=vb->Unlock();});t.join();ok(hr,"cross thread native Unlock");check(positions(vb.p).state==FiniteStatus::Unknown,"cross thread publication rejected");}
}
struct ConcurrentFree{IDirect3DVertexBuffer9* native;HANDLE start=CreateEventA(nullptr,TRUE,FALSE,nullptr),done=CreateEventA(nullptr,TRUE,FALSE,nullptr);HRESULT hr=E_FAIL;bool completed=false;~ConcurrentFree(){CloseHandle(start);CloseHandle(done);}};
void registry_wait(void* value){auto& c=*static_cast<ConcurrentFree*>(value);SetEvent(c.start);c.completed=WaitForSingleObject(c.done,3000)==WAIT_OBJECT_0;}
void lifetime(Create create,HWND window){
    Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);auto* native=borrowed_native_buffer_for_lock_contract(vb.p);
    IUnknown* side=nullptr;DWORD bytes=sizeof side;ok(native->GetPrivateData(sidecar_guid,&side,&bytes),"retain private sidecar");check(side!=nullptr,"private IUnknown returned");vb.reset();check(stats(s).sidecars==1,"external CPU sidecar survives native allocation");side->Release();check(stats(s).sidecars==0,"external final Release returns budget");
    make(s,vb);native=borrowed_native_buffer_for_lock_contract(vb.p);ConcurrentFree c{native};std::thread worker([&]{WaitForSingleObject(c.start,INFINITE);c.hr=native->FreePrivateData(sidecar_guid);SetEvent(c.done);});finite_fixture_with_registry(registry_wait,&c);worker.join();check(c.completed&&SUCCEEDED(c.hr),"native private Release never waits for registry");check(stats(s).sidecars==0,"deferred final Release drained and budget returned");
    vb.reset();make(s,vb);native=borrowed_native_buffer_for_lock_contract(vb.p);side=nullptr;bytes=sizeof side;ok(native->GetPrivateData(sidecar_guid,&side,&bytes),"retain sidecar for POD spoof");ok(native->SetPrivateData(sidecar_guid,&side,sizeof side,0),"foreign POD replaces reservation");std::uint32_t values[12]={};upload(vb.p,values,sizeof values);check(positions(vb.p).state==FiniteStatus::Unknown&&!stats(s).active,"POD pointer cannot impersonate private IUnknown");side->Release();
}
struct NativeFault {
    using Lock=HRESULT(WINAPI*)(IUnknown*,UINT,UINT,void**,DWORD);
    using Unlock=HRESULT(WINAPI*)(IUnknown*);
    static inline bool clear=false;static inline UINT seen_offset=0,seen_size=0;static inline DWORD seen_flags=0;
    static HRESULT WINAPI fail_lock(IUnknown*,UINT o,UINT n,void** out,DWORD flags){seen_offset=o;seen_size=n;seen_flags=flags;if(clear&&out)*out=nullptr;SetLastError(0x568899aa);return E_ACCESSDENIED;}
    static HRESULT WINAPI fail_unlock(IUnknown*){SetLastError(0x33445566);return E_FAIL;}
    IUnknown* object;void** previous;void* table[14];
    NativeFault(IUnknown* value,bool unlock):object(value),previous(*reinterpret_cast<void***>(value)){
        std::memcpy(table,previous,sizeof table);if(unlock){auto function=&fail_unlock;std::memcpy(&table[12],&function,sizeof function);}else{auto function=&fail_lock;std::memcpy(&table[11],&function,sizeof function);}*reinterpret_cast<void***>(object)=table;
    }
    ~NativeFault(){*reinterpret_cast<void***>(object)=previous;}
};
void fault_cases(Create create,HWND window){
    std::uint32_t finite[12]={};
    for(bool clear:{false,true}){
        Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);upload(vb.p,finite,sizeof finite);auto* native=borrowed_native_buffer_for_lock_contract(vb.p);
        NativeFault fault(native,false);NativeFault::clear=clear;
        State original;unsigned short cw=0x077f;unsigned mx=0x3fa0;asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");State expected;
        void* sentinel=reinterpret_cast<void*>(0x12345678);void* output=sentinel;SetLastError(0x8899);HRESULT hr=vb->Lock(7,11,&output,D3DLOCK_NOSYSLOCK);State after;original.restore();
        check(hr==E_ACCESSDENIED&&output==(clear?nullptr:sentinel),"failed native Lock exact HRESULT/output");check(NativeFault::seen_offset==7&&NativeFault::seen_size==11&&NativeFault::seen_flags==D3DLOCK_NOSYSLOCK,"failed native Lock exact arguments");check(after.error==0x568899aa&&after.mxcsr==expected.mxcsr&&!std::memcmp(after.x87,expected.x87,108),"failed native Lock FP/LastError preserved");
    }
    {Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);auto* native=borrowed_native_buffer_for_lock_contract(vb.p);void* p=nullptr;ok(vb->Lock(0,48,&p,0),"failed Unlock setup");std::memcpy(p,finite,48);
     {NativeFault fault(native,true);State original;asm volatile("fninit\n\tfld1":::"memory");State expected;SetLastError(0x9abc);HRESULT hr=vb->Unlock();State after;original.restore();check(hr==E_FAIL&&after.error==0x33445566&&!std::memcmp(after.x87,expected.x87,108),"failed native Unlock exact result/FP/LastError");}
     ok(native->Unlock(),"explicit fixture repairs failed native mapping");check(positions(vb.p).state==FiniteStatus::Unknown&&content(vb.p).ambiguous,"failed Unlock never publishes");}
    {Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);void* p=nullptr;ok(vb->Lock(0,48,&p,0),"pending Reset setup");std::memcpy(p,finite,48);auto generation=stats(s).generation;s.device->Reset(&s.pp);check(stats(s).generation>generation&&positions(vb.p).state==FiniteStatus::Unknown,"Reset attempt invalidates pending publication");ok(vb->Unlock(),"pending Reset Unlock native");check(positions(vb.p).state==FiniteStatus::Unknown,"old staged upload cannot publish after Reset");}
    {Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);upload(vb.p,finite,48);void *a=nullptr,*b=nullptr;ok(vb->Lock(0,48,&a,0),"nested outer Lock");ok(vb->Lock(0,48,&b,D3DLOCK_READONLY),"nested readonly Lock");ok(vb->Unlock(),"nested inner Unlock");ok(vb->Unlock(),"nested outer Unlock");check(positions(vb.p).state==FiniteStatus::Unknown&&content(vb.p).ambiguous,"nested readonly cannot publish earlier write");}
}
struct ConcurrentRef {IUnknown* side;HANDLE start=CreateEventA(nullptr,TRUE,FALSE,nullptr),done=CreateEventA(nullptr,TRUE,FALSE,nullptr);bool completed=false;~ConcurrentRef(){CloseHandle(start);CloseHandle(done);}};
void addref_wait(void* data){auto& c=*static_cast<ConcurrentRef*>(data);SetEvent(c.start);c.completed=WaitForSingleObject(c.done,3000)==WAIT_OBJECT_0;}
void external_refs(Create create,HWND window){
    IUnknown* survivor=nullptr;
    {Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);auto* native=borrowed_native_buffer_for_lock_contract(vb.p);DWORD bytes=sizeof survivor;ok(native->GetPrivateData(sidecar_guid,&survivor,&bytes),"external ref setup");}
    check(survivor->AddRef()==2,"CPU sidecar survives complete device teardown");check(survivor->Release()==1,"external reference balanced after device teardown");check(survivor->Release()==0,"final external reference frees owner without device cycle");
    Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);std::uint32_t values[12]={};upload(vb.p,values,48);auto* native=borrowed_native_buffer_for_lock_contract(vb.p);IUnknown* side=nullptr;DWORD bytes=sizeof side;ok(native->GetPrivateData(sidecar_guid,&side,&bytes),"concurrent external ref setup");
    ConcurrentRef c{side};std::thread worker([&]{WaitForSingleObject(c.start,INFINITE);side->AddRef();SetEvent(c.done);});finite_fixture_addref_callback(addref_wait,&c);auto view=positions(vb.p);finite_fixture_addref_callback(nullptr,nullptr);worker.join();check(c.completed&&view.state==FiniteStatus::Finite,"unrelated concurrent AddRef cannot spoof reservation counter");side->Release();side->Release();vb.reset();check(stats(s).sidecars==0,"native GetPrivateData ref never leaked during external AddRef");
}

struct CallObservation {HRESULT result;State state;bool output;};
bool same_state(const State& a,const State& b){return a.error==b.error&&a.mxcsr==b.mxcsr&&!std::memcmp(a.x87,b.x87,108);}
void native_parity(Create create,HWND window){
    Session s(create,window);Com<IDirect3DVertexBuffer9> wrapped,native;make(s,wrapped);
    auto* device=borrowed_native_device(s.device.p);ok(device->CreateVertexBuffer(48,0,D3DFVF_XYZ,D3DPOOL_MANAGED,&native.p,nullptr),"native parity buffer");
    State original;unsigned short cw=0x077f;unsigned mx=0x3fa0;asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");SetLastError(0x99228811);State seed;original.restore();
    const std::uint32_t payload[12]={0x3f800000,0,0,0x40000000,0,0,0x40400000,0,0,0,0,0};
    for(DWORD flags:{DWORD(0),DWORD(D3DLOCK_NOSYSLOCK)}){
        void *a=nullptr,*b=nullptr;seed.restore();HRESULT ha=native->Lock(0,48,&a,flags);State sa;original.restore();seed.restore();HRESULT hb=wrapped->Lock(0,48,&b,flags);State sb;original.restore();
        check(ha==hb&&a&&b,"native/wrapped successful Lock output parity");check(same_state(sa,sb),"native/wrapped successful Lock FP and LastError parity");
        std::memcpy(a,payload,48);std::memcpy(b,payload,48);seed.restore();ha=native->Unlock();State ua;original.restore();seed.restore();hb=wrapped->Unlock();State ub;original.restore();
        check(ha==hb&&same_state(ua,ub),"native/wrapped successful Unlock FP and LastError parity");
        // Both native backings are readable MANAGED buffers; no WRITEONLY read assumption.
        ok(native->Lock(0,48,&a,0),"native byte comparison mapping");ok(wrapped->Lock(0,48,&b,0),"wrapped byte comparison mapping");check(!std::memcmp(a,payload,48)&&!std::memcmp(b,payload,48),"observer never mutates mapped native payload");ok(native->Unlock(),"native byte comparison close");ok(wrapped->Unlock(),"wrapped byte comparison close");
    }
    for(bool clear:{false,true}){
        auto* backend=borrowed_native_buffer_for_lock_contract(wrapped.p);NativeFault fault_a(native.p,false);NativeFault fault_b(backend,false);NativeFault::clear=clear;
        void* a=reinterpret_cast<void*>(0x11223344);void* b=a;seed.restore();HRESULT ha=native->Lock(5,7,&a,0);State sa;original.restore();seed.restore();HRESULT hb=wrapped->Lock(5,7,&b,0);State sb;original.restore();
        check(ha==hb&&a==b&&same_state(sa,sb),"native/wrapped failed Lock output FP LastError parity");
    }
}

struct ForeignUnknown final:IUnknown {
    std::atomic<ULONG> refs{1};std::atomic<unsigned> adds{0},releases{0};
    HRESULT WINAPI QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown)return E_NOINTERFACE;AddRef();*out=this;return S_OK;}
    ULONG WINAPI AddRef()override{++adds;return ++refs;}ULONG WINAPI Release()override{++releases;return --refs;}
};
void foreign_unknown(Create create,HWND window){
    Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);auto* native=borrowed_native_buffer_for_lock_contract(vb.p);IUnknown* old=nullptr;DWORD size=sizeof old;ok(native->GetPrivateData(sidecar_guid,&old,&size),"retain old sidecar for foreign object");
    ForeignUnknown foreign;ok(native->SetPrivateData(sidecar_guid,&foreign,sizeof(IUnknown*),D3DSPD_IUNKNOWN),"foreign native IUnknown reservation replacement");check(foreign.refs==2,"native store owns one foreign reference");std::uint32_t values[12]={};upload(vb.p,values,48);check(foreign.refs==3&&foreign.adds==2&&foreign.releases==0,"one untrusted observation retained without guessed Release");
    D3DVERTEXBUFFER_DESC ignored{};for(unsigned n=0;n<4;++n)ok(vb->GetDesc(&ignored),"repeated GetDesc after foreign reservation");
    upload(vb.p,values,48);positions(vb.p);check(foreign.refs==3&&foreign.adds==2&&!stats(s).active,"permanent disable prevents repeated foreign reference growth");ok(native->FreePrivateData(sidecar_guid),"fixture removes foreign reservation");check(foreign.refs==2,"native store releases its own foreign reference");foreign.Release();old->Release();check(foreign.refs==1,"fixture explicitly balances documented tamper-only retained reference");
}
struct DeviceFailure {
    static HRESULT WINAPI process(IDirect3DDevice9*,UINT,UINT,UINT,IDirect3DVertexBuffer9*,IDirect3DVertexDeclaration9*,DWORD){return E_ACCESSDENIED;}
    static HRESULT WINAPI reset(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*){return D3DERR_INVALIDCALL;}
    static HRESULT WINAPI present(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*){return D3DERR_DEVICELOST;}
    IDirect3DDevice9* native;void** previous;void* table[119];
    DeviceFailure(IDirect3DDevice9* value,unsigned slot):native(value),previous(*reinterpret_cast<void***>(value)){
        std::memcpy(table,previous,sizeof table);if(slot==85){auto f=&process;std::memcpy(&table[slot],&f,sizeof f);}else if(slot==16){auto f=&reset;std::memcpy(&table[slot],&f,sizeof f);}else{auto f=&present;std::memcpy(&table[slot],&f,sizeof f);}*reinterpret_cast<void***>(native)=table;
    }~DeviceFailure(){*reinterpret_cast<void***>(native)=previous;}
};
void device_failures(Create create,HWND window){
    Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);std::uint32_t values[12]={};upload(vb.p,values,48);auto* native=borrowed_native_device(s.device.p);
    {DeviceFailure fault(native,85);check(s.device->ProcessVertices(0,0,4,vb.p,nullptr,0)==E_ACCESSDENIED,"ProcessVertices failure preserves HRESULT");}
    check(positions(vb.p).state==FiniteStatus::Unknown,"ProcessVertices attempt invalidates evidence");upload(vb.p,values,48);
    auto generation=stats(s).generation;{DeviceFailure fault(native,16);check(s.device->Reset(&s.pp)==D3DERR_INVALIDCALL,"failed Reset HRESULT preserved");}check(positions(vb.p).state==FiniteStatus::Unknown&&stats(s).payload_bytes==0&&stats(s).generation>generation,"failed Reset retires evidence and budget");ok(s.device->Reset(&s.pp),"failed Reset retry native success");upload(vb.p,values,48);check(positions(vb.p).state==FiniteStatus::Finite,"successful Reset fresh upload restores evidence");
    generation=stats(s).generation;{DeviceFailure fault(native,17);check(s.device->Present(nullptr,nullptr,nullptr,nullptr)==D3DERR_DEVICELOST,"observed loss HRESULT preserved");}check(positions(vb.p).state==FiniteStatus::Unknown&&stats(s).generation>generation&&stats(s).payload_bytes==0,"observed loss clears atlas");ok(s.device->Reset(&s.pp),"loss retry native Reset");
}


struct TrackingSetFailure {
    static HRESULT WINAPI set(IUnknown*,REFGUID,const void*,DWORD,DWORD){return E_OUTOFMEMORY;}
    IUnknown* resource;void** old;void* table[14];
    explicit TrackingSetFailure(IUnknown* value):resource(value),old(*reinterpret_cast<void***>(value)){
        std::memcpy(table,old,sizeof table);auto function=&set;std::memcpy(&table[4],&function,sizeof function);*reinterpret_cast<void***>(resource)=table;
    }
    ~TrackingSetFailure(){*reinterpret_cast<void***>(resource)=old;}
};
void tracking_failure(Create create,HWND window){
    Session s(create,window);Com<IDirect3DVertexBuffer9> first,second;make(s,first);make(s,second);std::uint32_t bytes[12]={};upload(first.p,bytes,48);upload(second.p,bytes,48);check(stats(s).payload_bytes>0,"tracking failure has retained atlases");
    auto* native=borrowed_native_buffer_for_lock_contract(first.p);void* p=nullptr;
    {TrackingSetFailure fault(native);ok(first->Lock(0,48,&p,0),"tracker SetPrivateData failure preserves native Lock");std::memcpy(p,bytes,48);}
    ok(first->Unlock(),"tracker failure preserves native Unlock");auto value=stats(s);check(!value.active&&value.payload_bytes==0&&FAILED(value.status),"first tracker failure retires all owner atlases");check(positions(second.p).state==FiniteStatus::Unknown,"other buffer cannot use stale atlas after tracker failure");ok(s.device->Reset(&s.pp),"tracking failure Reset still native success");upload(second.p,bytes,48);check(!stats(s).active&&positions(second.p).state==FiniteStatus::Unknown,"Reset cannot clear permanent tracking failure");
    description(first.p);description(second.p);
}

struct CreationFault {
    using Function=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*);
    using Set=HRESULT(WINAPI*)(IUnknown*,REFGUID,const void*,DWORD,DWORD);
    static inline Function original=nullptr;static inline unsigned calls=0;
    static inline DWORD usages[2]{};static inline bool clear=false,attachment=false,retry_state=false;
    static inline State* expected=nullptr;static inline State* selected=nullptr;
    static inline void** buffer_previous=nullptr;static inline void* buffer_table[14]{};static inline Set original_set=nullptr;
    static HRESULT WINAPI fail_attachment(IUnknown* resource,REFGUID guid,const void* bytes,DWORD size,DWORD flags){
        if(guid==sidecar_guid){*reinterpret_cast<void***>(resource)=buffer_previous;return E_OUTOFMEMORY;}
        return original_set(resource,guid,bytes,size,flags);
    }
    static HRESULT WINAPI run(IDirect3DDevice9* d,UINT n,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* shared){
        usages[calls]=usage;++calls;
        if(calls==1){
            if(attachment){
                HRESULT hr=original(d,n,usage,fvf,pool,out,shared);
                if(SUCCEEDED(hr)&&out&&*out){buffer_previous=*reinterpret_cast<void***>(*out);std::memcpy(buffer_table,buffer_previous,sizeof buffer_table);std::memcpy(&original_set,&buffer_table[4],sizeof original_set);auto fn=&fail_attachment;std::memcpy(&buffer_table[4],&fn,sizeof fn);*reinterpret_cast<void***>(*out)=buffer_table;}
                return hr;
            }
            if(clear&&out)*out=nullptr;
            asm volatile("fninit\n\tfldz":::"memory");SetLastError(0x10203040);return E_OUTOFMEMORY;
        }
        State incoming;retry_state=expected&&same_state(incoming,*expected);
        HRESULT hr=original(d,n,usage,fvf,pool,out,shared);
        unsigned short cw=0x027f;unsigned mx=0x5f80;
        asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");
        SetLastError(0x50607080);if(selected)*selected=State{};return hr;
    }
    IDirect3DDevice9* device;void** previous;void* table[119];
    CreationFault(IDirect3DDevice9* d,bool clear_output,bool fail_metadata):device(d),previous(*reinterpret_cast<void***>(d)){
        calls=0;clear=clear_output;attachment=fail_metadata;retry_state=false;std::memcpy(table,previous,sizeof table);std::memcpy(&original,&table[26],sizeof original);
        auto function=&run;std::memcpy(&table[26],&function,sizeof function);*reinterpret_cast<void***>(d)=table;
    }
    ~CreationFault(){*reinterpret_cast<void***>(device)=previous;expected=nullptr;selected=nullptr;}
};
struct DescriptionFault {
    static inline bool write=false;
    static HRESULT WINAPI run(IDirect3DVertexBuffer9*,D3DVERTEXBUFFER_DESC* out){
        if(write&&out){out->Usage=0x11223344;}SetLastError(0x66778899);return E_ACCESSDENIED;
    }
    IDirect3DVertexBuffer9* object;void** previous;void* table[14];
    DescriptionFault(IDirect3DVertexBuffer9* p,bool mutate):object(p),previous(*reinterpret_cast<void***>(p)){
        write=mutate;std::memcpy(table,previous,sizeof table);auto function=&run;std::memcpy(&table[13],&function,sizeof function);*reinterpret_cast<void***>(p)=table;
    }
    ~DescriptionFault(){*reinterpret_cast<void***>(object)=previous;}
};
void portable_fallback(Create create,HWND window){
    for(bool attach:{false,true})for(bool clear:{false,true}){
        Session s(create,window);Com<IDirect3DVertexBuffer9> vb;auto* native=borrowed_native_device(s.device.p);
        {CreationFault fault(native,clear,attach);State original;unsigned short cw=0x077f;unsigned mx=0x3fa0;asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");SetLastError(0x22334455);State incoming,selected;CreationFault::expected=&incoming;CreationFault::selected=&selected;
         HRESULT hr=s.device->CreateVertexBuffer(48,8,D3DFVF_XYZ,D3DPOOL_MANAGED,&vb.p,nullptr);State after;original.restore();
         check(hr==S_OK&&vb.p&&CreationFault::calls==2&&CreationFault::usages[0]==0&&CreationFault::usages[1]==8,"failed readable Create retries exact original Usage");
         check(CreationFault::retry_state,"fallback receives original incoming full FP and LastError");
         check(same_state(after,selected),"fallback preserves selected native outgoing full FP and LastError");}
        D3DVERTEXBUFFER_DESC app{},back{};ok(vb->GetDesc(&app),"fallback application descriptor");ok(borrowed_native_buffer_for_lock_contract(vb.p)->GetDesc(&back),"fallback native descriptor");
        check(app.Usage==8&&back.Usage==8&&stats(s).sidecars==0,"creation fallback leaves original unconverted resource");
    }
    {Session s(create,window,true,0);Com<IDirect3DVertexBuffer9> vb;make(s,vb);D3DVERTEXBUFFER_DESC desc{};
     ok(borrowed_native_buffer_for_lock_contract(vb.p)->GetDesc(&desc),"metadata-budget fallback native descriptor");
     check(desc.Usage==8&&stats(s).sidecars==0,"failed atlas admission rolls conversion back");}
    {Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);
     for(bool write:{false,true}){
         DescriptionFault fault(borrowed_native_buffer_for_lock_contract(vb.p),write);D3DVERTEXBUFFER_DESC desc{};desc.Usage=0xaabbccdd;
         State original;unsigned short cw=0x077f;unsigned mx=0x3fa0;asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");State expected;
         HRESULT hr=vb->GetDesc(&desc);State after;original.restore();
         check(hr==E_ACCESSDENIED&&desc.Usage==(write?0x11223344u:0xaabbccddu),"failed GetDesc preserves exact native output");
         check(after.error==0x66778899&&after.mxcsr==expected.mxcsr&&!std::memcmp(after.x87,expected.x87,108),"failed GetDesc preserves native FP and LastError");
     }

    }
}
void portable_contract(Create create,HWND window){
    {Session s(create,window,false,1024,4096,false);Com<IDirect3DVertexBuffer9> vb;make(s,vb);
     {NativeFault fault(vb.p,false);auto view=content(vb.p);check(!view.requested&&!view.known&&view.ambiguous&&FAILED(view.status),"off-tracking wrapper slot loss remains explicit failure");}
     auto view=content(vb.p);check(!view.requested&&!view.ambiguous&&!FAILED(view.status),"off-tracking intact wrapper keeps ordinary off status");}
    namespace pu=x3m::ownership::portable_upload;
    for(DWORD usage:{DWORD(0),DWORD(D3DUSAGE_WRITEONLY),DWORD(D3DUSAGE_WRITEONLY|D3DUSAGE_SOFTWAREPROCESSING)}){
        const auto p=pu::plan_creation(true,48,usage,D3DPOOL_MANAGED,nullptr);
        check(p.requested_usage==usage&&p.native_usage==(usage&~DWORD(D3DUSAGE_WRITEONLY))&&p.converted==bool(usage&D3DUSAGE_WRITEONLY),"creation policy strips only WRITEONLY");
    }
    HANDLE shared=nullptr;
    check(!pu::plan_creation(false,48,8,D3DPOOL_MANAGED,nullptr).converted,"disabled creation unchanged");
    check(!pu::plan_creation(true,0,8,D3DPOOL_MANAGED,nullptr).converted,"zero creation unchanged");
    check(!pu::plan_creation(true,pu::maximum_buffer_bytes+1,8,D3DPOOL_MANAGED,nullptr).converted,"oversize creation unchanged");
    check(!pu::plan_creation(true,48,8,D3DPOOL_DEFAULT,nullptr).converted,"default creation unchanged");
    check(!pu::plan_creation(true,48,8|D3DUSAGE_DYNAMIC,D3DPOOL_MANAGED,nullptr).converted,"invalid managed dynamic not repaired");
    check(!pu::plan_creation(true,48,8,D3DPOOL_MANAGED,&shared).converted,"shared creation unchanged");
    Session s(create,window);Com<IDirect3DVertexBuffer9> vb;make(s,vb);auto* native=borrowed_native_buffer_for_lock_contract(vb.p);
    pu::BufferContract contract;check(pu::inspect(native,&contract)&&contract.usage==0&&contract.size==48,"public native readable descriptor qualifies");
    auto changed=contract;changed.size++;check(!pu::same_description(native,changed),"changed immutable descriptor refused");
    check(!pu::inspect(static_cast<IDirect3DVertexBuffer9*>(nullptr),&changed),"null native descriptor refused");
    check(!pu::inspect(native,nullptr),"null descriptor output refused");
    check(invalidate_native_buffer_evidence(nullptr)==E_INVALIDARG,"unknown native invalidation rejected");
    check(invalidate_native_buffer_evidence(s.device.p)==E_INVALIDARG,"wrong-kind native invalidation rejected");
    check(invalidate_native_buffer_evidence(native)==E_INVALIDARG,"unrecognized native invalidation rejected");
    void* pointer=nullptr;ok(vb->Lock(0,0,&pointer,0),"portable whole application mapping");
    pu::Window window_range;check(pu::normalize_window(native,contract,0,0,0,pointer,&window_range)&&window_range.size==48,"whole zero-size window normalized");
    check(pu::normalize_window(native,contract,4,44,D3DLOCK_NOSYSLOCK,static_cast<char*>(pointer)+4,&window_range)&&window_range.offset==4&&window_range.size==44,"bounded exact-end partial normalized");
    check(!pu::normalize_window(native,contract,4,0,0,pointer,&window_range),"nonzero-offset zero-size refused");
    check(!pu::normalize_window(native,contract,4,45,0,pointer,&window_range),"out-of-range window refused");
    check(!pu::normalize_window(native,contract,0,48,D3DLOCK_READONLY,pointer,&window_range),"read-only is not write producer");
    check(!pu::normalize_window(native,contract,0,48,D3DLOCK_DISCARD,pointer,&window_range),"discard producer refused");
    check(!pu::normalize_window(native,contract,0,48,D3DLOCK_NOOVERWRITE,pointer,&window_range),"nooverwrite producer refused");
    check(!pu::normalize_window(native,contract,0,48,0,nullptr,&window_range),"null mapping refused");
    check(!pu::normalize_window(native,contract,0,48,0,reinterpret_cast<void*>(UINTPTR_MAX-3),&window_range),"mapping address overflow refused");
    std::memset(pointer,0,48);ok(vb->Unlock(),"portable mapping closes");
    for(auto format:{D3DFMT_INDEX16,D3DFMT_INDEX32}){
        Com<IDirect3DIndexBuffer9> ib;ok(s.device->CreateIndexBuffer(24,8,format,D3DPOOL_MANAGED,&ib.p,nullptr),"portable IB create");
        auto* backing=borrowed_native_buffer_for_lock_contract(ib.p);D3DINDEXBUFFER_DESC app{},back{};
        ok(ib->GetDesc(&app),"application IB descriptor");ok(backing->GetDesc(&back),"native IB descriptor");
        check(app.Usage==8&&back.Usage==0,"IB original Usage retained");app.Usage=back.Usage;
        check(!std::memcmp(&app,&back,sizeof app),"all other IB descriptor fields preserved");
        check(pu::inspect(backing,&contract)&&contract.format==format,"public IB native descriptor qualifies");
    }
}
void release_index_elsewhere(IUnknown* side){std::thread worker([=]{side->Release();});worker.join();}
int main(){try{finite_fixture_index_controls(check,release_index_elsewhere);HMODULE module=LoadLibraryA("C:\\windows\\system32\\d3d9.dll");check(module!=nullptr,"load native D3D9");Create create=nullptr;auto entry=GetProcAddress(module,"Direct3DCreate9");std::memcpy(&create,&entry,sizeof create);check(create!=nullptr,"create entry");HWND window=CreateWindowExA(0,"STATIC","finite upload fixture",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);check(window!=nullptr,"window");portable_fallback(create,window);portable_contract(create,window);normal(create,window);gates(create,window);lifetime(create,window);fault_cases(create,window);external_refs(create,window);native_parity(create,window);foreign_unknown(create,window);device_failures(create,window);tracking_failure(create,window);DestroyWindow(window);FreeLibrary(module);std::printf("RESULT PASS checks=%u\n",checks);return 0;}catch(const std::exception& e){std::printf("RESULT FAIL %s checks=%u\n",e.what(),checks);return 1;}}
