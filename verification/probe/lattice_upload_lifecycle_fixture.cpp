// B1 actual ownership-wrapper controls; capture.cpp lifecycle is NOT tested.
#define X3M_LATTICE_UPLOAD_READABLE_EMBEDDED
#include "lattice_upload_readable_fixture.cpp"
#undef X3M_LATTICE_UPLOAD_READABLE_EMBEDDED
#include "../../src/ownership/clone_upload_observer.h"
#include "../../src/ownership/clone_upload_abi.h"
#include <d3dx9mesh.h>
#include <memory>
#include <algorithm>
#include <csetjmp>

namespace {
namespace cu=x3m::ownership::clone_upload;
using MeshCreate=HRESULT(WINAPI*)(DWORD,DWORD,DWORD,const D3DVERTEXELEMENT9*,IDirect3DDevice9*,ID3DXMesh**);
const D3DVERTEXELEMENT9 target_decl[]={
    {0,0,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_POSITION,0},
    {0,8,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
    {0,20,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},
    {0,28,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,1},
    {0,36,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
const D3DVERTEXELEMENT9 source_decl[]={
    {0,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITION,0},
    {0,16,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
    {0,28,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},
    {0,36,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,1},
    {0,44,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
struct MeshCase {
    Com<ID3DXMesh> source,clone;
    Com<IDirect3DVertexBuffer9> source_vb,final_vb;
    Com<IDirect3DIndexBuffer9> source_ib,final_ib;
    std::vector<unsigned char> vertices,indices;
    unsigned slot;
    MeshCase(MeshCreate create,ReadableSession& s,unsigned selected,bool convert=false):slot(selected) {
        const auto shape=cu::shapes[slot];vertices.resize(shape.vertices*40u);indices.resize(shape.faces*6u);
        const std::uint16_t half[]={0x0000,0x3c00,0xc000,0x3800};
        const float whole[]={0.f,1.f,-2.f,.5f};
        std::vector<unsigned char> input(shape.vertices*(convert?48u:40u));
        for(unsigned v=0;v<shape.vertices;++v){
            auto* dst=vertices.data()+v*40u;
            const std::uint16_t position[]={half[v%4],half[(v+1)%4],half[(v+2)%4],0x3c00};
            std::memcpy(dst,position,sizeof position);
            const float attributes[]={0,1,0,float(v%4)*.25f,float((v+1)%4)*.25f,.5f,.75f};
            std::memcpy(dst+8,attributes,sizeof attributes);const std::uint32_t color=0xff000000u|((v*7919u)&0xffffffu);
            std::memcpy(dst+36,&color,sizeof color);
            if(convert){const float p[]={whole[v%4],whole[(v+1)%4],whole[(v+2)%4],1.f};
                std::memcpy(input.data()+v*48u,p,sizeof p);std::memcpy(input.data()+v*48u+16,dst+8,32);
            }else std::memcpy(input.data()+v*40u,dst,40);
        }
        for(unsigned f=0;f<shape.faces;++f)for(unsigned c=0;c<3;++c){
            const auto index=static_cast<std::uint16_t>((f*7u+c*11u)%shape.vertices);
            std::memcpy(indices.data()+(f*3u+c)*2u,&index,2);
        }
        ok(create(shape.faces,shape.vertices,D3DXMESH_SYSTEMMEM,convert?source_decl:target_decl,s.device.p,&source.p),"create actual authored source mesh");
        void* map=nullptr;ok(source->LockVertexBuffer(0,&map),"author source VB Lock");std::memcpy(map,input.data(),input.size());ok(source->UnlockVertexBuffer(),"author source VB Unlock");
        ok(source->LockIndexBuffer(0,&map),"author source IB Lock");std::memcpy(map,indices.data(),indices.size());ok(source->UnlockIndexBuffer(),"author source IB Unlock");
        DWORD* attributes=nullptr;ok(source->LockAttributeBuffer(0,&attributes),"author attributes Lock");
        for(unsigned f=0;f<shape.faces;++f)attributes[f]=f%2;
        ok(source->UnlockAttributeBuffer(),"author attributes Unlock");
        ok(source->GetVertexBuffer(&source_vb.p),"source public VB");ok(source->GetIndexBuffer(&source_ib.p),"source public IB");
    }
    HRESULT run(ReadableSession& s) {return clone_mesh_upload(source.p,D3DXMESH_MANAGED|D3DXMESH_WRITEONLY,target_decl,s.device.p,&clone.p);}
    void final_refs(){ok(clone->GetVertexBuffer(&final_vb.p),"final public VB");ok(clone->GetIndexBuffer(&final_ib.p),"final public IB");}
    void close(){final_vb.reset();final_ib.reset();clone.reset();source_vb.reset();source_ib.reset();source.reset();}
};

ULONG references(IDirect3DDevice9* d){d->AddRef();return d->Release();}
CloneUploadPinView pin_view(IDirect3DDevice9* d){CloneUploadPinView v{};check(query_clone_upload_pin(d,&v)==S_OK,"query actual upload pin");return v;}
void no_pin(IDirect3DDevice9* d){CloneUploadPinView v{};check(query_clone_upload_pin(d,&v)==S_FALSE&&!v.references&&!v.arm.serial,"no upload pin after retirement");}
void finish_session(ReadableSession& s){s.close();}
struct DeviceReleaseProbe {
    using Fn=ULONG(WINAPI*)(IDirect3DDevice9*);
    static inline DeviceReleaseProbe* active=nullptr;
    IDirect3DDevice9* device;Fn original=nullptr;void** previous;void* table[119]{};
    CloneUploadArmToken token{};bool watch=false,outside=false,cleared=false;unsigned drains=0;
    static ULONG WINAPI release(IDirect3DDevice9* d){
        auto& probe=*active;CloneUploadPinView view{};
        if(probe.watch&&query_clone_upload_pin(d,&view)==S_FALSE){
            probe.watch=false;++probe.drains;
            CloneUploadClose result=CloneUploadClose::Released;
            probe.cleared=close_clone_upload(d,probe.token,&result)==S_FALSE&&result==CloneUploadClose::None;
            HANDLE done=CreateEventA(nullptr,TRUE,FALSE,nullptr);
            std::thread worker([d,done]{CloneUploadPinView v{};query_clone_upload_pin(d,&v);SetEvent(done);});
            probe.outside=WaitForSingleObject(done,5000)==WAIT_OBJECT_0;
            // A held registry would make the worker wait until this Release
            // returns. Do not deadlock the failure witness by joining here.
            if(!probe.outside){worker.detach();throw std::runtime_error("pin Release still holds registry");}
            worker.join();CloseHandle(done);
        }
        return probe.original(d);
    }
    explicit DeviceReleaseProbe(IDirect3DDevice9* d):device(d),previous(*reinterpret_cast<void***>(d)){
        std::memcpy(table,previous,sizeof table);std::memcpy(&original,&table[2],sizeof original);
        auto fn=&release;std::memcpy(&table[2],&fn,sizeof fn);*reinterpret_cast<void***>(d)=table;active=this;
    }
    ~DeviceReleaseProbe(){*reinterpret_cast<void***>(device)=previous;active=nullptr;}
    void expect(CloneUploadArmToken value){token=value;watch=true;}
    void verify(){check(drains==1&&outside&&cleared,"one pin Release outside registry after control detached");}
};
void basic_lifecycle(Create create,HWND window,cu::Store& store){
    for(unsigned cycle=0;cycle<2;++cycle){ReadableSession s(create,window);
        check(references(s.device.p)==1,"unarmed actual wrapper count one");
        std::uint64_t owner=0,exhausted=1;
        check(clone_upload_fixture_owner_serial(s.device.p,0,&owner)&&owner,"inject exhausted owner sentinel on actual wrapper");
        check(arm_clone_upload(&store,s.device.p,target_decl)==S_FALSE&&references(s.device.p)==1,"exhausted owner refuses before pin acquisition");
        check(clone_upload_fixture_owner_serial(s.device.p,owner,&exhausted)&&!exhausted,"restore fixture owner identity");
        check(arm_clone_upload(&store,s.device.p,target_decl)==S_OK,"B1 arm exact S_OK");
        const auto first=pin_view(s.device.p);check(first.references==1&&!first.closing&&!first.scope_active,"query reports exactly one owned pin");
        check(references(s.device.p)==2,"arm adds exactly one actual wrapper reference");
        {DeviceReleaseProbe probe(s.device.p);probe.expect(first.arm);CloneUploadClose close{};
            State original;unsigned short cw=0x077f;unsigned mx=0x3fa0;
            asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");SetLastError(0x22334455);State incoming;
            CloneUploadPinView observed{};const HRESULT queried=query_clone_upload_pin(s.device.p,&observed);State query_after;
            incoming.restore();const HRESULT retired=close_clone_upload(s.device.p,first.arm,&close);State close_after;original.restore();
            check(queried==S_OK&&same_state(incoming,query_after)&&same_state(incoming,close_after),"pin query and callback close preserve full CPU and LastError");
            check(retired==S_OK&&close==CloneUploadClose::Released,"idle close exact Released");probe.verify();}
        no_pin(s.device.p);check(references(s.device.p)==1,"idle close restores caller count one");
        check(arm_clone_upload(&store,s.device.p,target_decl)==S_OK,"rearm same store and actual device");const auto next=pin_view(s.device.p);
        check(next.arm.serial>first.arm.serial&&next.arm.owner==first.arm.owner,"rearm token never reuses serial");CloneUploadClose stale{};
        check(close_clone_upload(s.device.p,first.arm,&stale)==S_FALSE&&stale==CloneUploadClose::None,"stale close cannot retire newer arm");
        auto other=std::make_unique<cu::Store>();check(arm_clone_upload(other.get(),s.device.p,target_decl)==S_FALSE,"second Store cannot steal armed device");
        {ReadableSession other_device(create,window);
            check(arm_clone_upload(&store,other_device.device.p,target_decl)==S_FALSE,"second actual device cannot steal active Store");
            no_pin(other_device.device.p);finish_session(other_device);}

        check(disarm_clone_upload(&store)==S_OK,"old manual disarm API remains compatible");
        no_pin(s.device.p);finish_session(s);
    }
}
CloneUploadPairRequest request_pair(MeshCase& c,IDirect3DDevice9* d){
    c.final_refs();BufferLockView vb{},ib{};
    check(get_buffer_lock_view(c.final_vb.p,&vb)==S_OK&&get_buffer_lock_view(c.final_ib.p,&ib)==S_OK,"actual paired allocation observations");
    check(vb.known&&ib.known&&vb.quiet()&&ib.quiet()&&vb.generation==ib.generation,"paired observations quiet same generation");
    const auto pin=pin_view(d);CloneUploadPairRequest request;
    request.arm=pin.arm;request.slot=c.slot;request.generation=vb.generation;
    request.vertex_key=c.final_vb.p;request.index_key=c.final_ib.p;
    request.vertex={vb.allocation_id,vb.revision};request.index={ib.allocation_id,ib.revision};
    return request;
}
struct Packet {
    std::vector<unsigned char> vertex,index;CloneUploadPairResult result{};
    explicit Packet(const MeshCase& c):vertex(c.vertices.size()+2,0xa5),index(c.indices.size()+2,0xa5){}
    HRESULT copy(IDirect3DDevice9* d,const CloneUploadPairRequest& r){return copy_clone_upload_pair(d,r,vertex.data()+1,vertex.size()-2,index.data()+1,index.size()-2,&result);}
    bool untouched()const{return std::all_of(vertex.begin(),vertex.end(),[](auto v){return v==0xa5;})&&std::all_of(index.begin(),index.end(),[](auto v){return v==0xa5;});}
    void clear(){std::fill(vertex.begin(),vertex.end(),0xa5);std::fill(index.begin(),index.end(),0xa5);result={};}
};
void pair_controls(MeshCreate create_mesh,Create create,HWND window,cu::Store& store){
    ReadableSession s(create,window);check(arm_clone_upload(&store,s.device.p,target_decl)==S_OK,"arm atomic pair cases");
    for(unsigned slot=0;slot<2;++slot){MeshCase c(create_mesh,s,slot);ok(c.run(s),"actual Clone for paired copy");
        auto request=request_pair(c,s.device.p);const auto vb_before=content(c.final_vb.p),ib_before=content(c.final_ib.p);
        c.final_vb.reset();c.final_ib.reset();Packet packet(c);
        State original;unsigned short cw=0x077f;unsigned mx=0x3fa0;
        asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");SetLastError(0x22334455);State incoming;
        const HRESULT hr=packet.copy(s.device.p,request);State outgoing;original.restore();
        check(hr==S_OK&&packet.result.status==CloneUploadPairStatus::Copied,"one guarded pair copies after getter Releases");
        check(same_state(incoming,outgoing),"paired copy preserves full CPU and LastError");
        check(packet.result.record.producer_payload_valid&&packet.result.binding_revision_match_at_observation&&
              packet.result.record.buffers[0]==request.vertex&&packet.result.record.buffers[1]==request.index&&
              packet.result.record.generation==request.generation,"paired metadata matches both observed identities");
        check(packet.vertex.front()==0xa5&&packet.vertex.back()==0xa5&&packet.index.front()==0xa5&&packet.index.back()==0xa5&&
              std::equal(c.vertices.begin(),c.vertices.end(),packet.vertex.begin()+1)&&std::equal(c.indices.begin(),c.indices.end(),packet.index.begin()+1),"paired exact bytes and four canaries");
        for(unsigned mode=0;mode<4;++mode){packet.clear();auto bad=request;
            if(mode==0)++bad.arm.serial;else if(mode==1)++bad.generation;else if(mode==2)++bad.vertex.allocation;else ++bad.index.revision;
            check(packet.copy(s.device.p,bad)==S_FALSE&&!packet.result.record.producer_payload_valid&&packet.untouched(),"stale pair facts refuse both payloads");
        }
        packet.clear();check(copy_clone_upload_pair(s.device.p,request,packet.vertex.data()+1,packet.vertex.size()-2,
            packet.index.data()+1,packet.index.size()-3,&packet.result)==S_FALSE&&packet.result.status==CloneUploadPairStatus::Capacity&&packet.untouched(),"short IB capacity never copies VB prefix");
        packet.clear();check(copy_clone_upload_pair(s.device.p,request,packet.vertex.data()+1,packet.vertex.size()-2,
            packet.vertex.data()+2,packet.index.size()-2,&packet.result)==E_INVALIDARG&&packet.untouched(),"overlapping payload spans rejected before writes");
        packet.clear();check(copy_clone_upload_pair(s.device.p,request,&store,sizeof(store),packet.index.data()+1,packet.index.size()-2,&packet.result)==E_INVALIDARG&&packet.untouched(),"Store alias rejected without modifying evidence");
        void* map=nullptr;ok(request.index_key->Lock(0,0,&map,D3DLOCK_READONLY),"fixture-only pending map");
        check(packet.copy(s.device.p,request)==S_FALSE&&packet.result.status==CloneUploadPairStatus::OpenMapping&&packet.untouched(),"pending map refuses atomic pair");ok(request.index_key->Unlock(),"fixture-only pending map close");
        c.final_refs();const auto va=content(c.final_vb.p),ia=content(c.final_ib.p);
        check(va.revision==vb_before.revision&&ia.revision==ib_before.revision,"paired query does not mutate resource revisions");
        BufferLockView v{},i{};ok(get_buffer_lock_view(c.final_vb.p,&v),"paired VB native call counters");ok(get_buffer_lock_view(c.final_ib.p,&i),"paired IB native call counters");
        check(v.attempt_serial==1&&v.unlock_serial==1&&i.attempt_serial==2&&i.unlock_serial==2,"paired observer adds no native Lock or Unlock");
        c.final_vb.reset();c.final_ib.reset();
        check(packet.copy(s.device.p,request)==S_OK,"valid pair survives refused queries");
        c.close();packet.clear();
        check(packet.copy(s.device.p,request)==S_FALSE&&packet.result.status==CloneUploadPairStatus::Binding&&packet.untouched(),"released weak keys refuse without resurrection");
    }
    check(disarm_clone_upload(&store)==S_OK,"disarm atomic pair cases");finish_session(s);
}
}
namespace {
ReadableSession* closing_session=nullptr;
cu::Store* closing_store=nullptr;
DeviceReleaseProbe* closing_probe=nullptr;
CloneUploadArmToken closing_arm{};
bool closing_seen=false,closing_early=false;
void closing_callback(CloneUploadFixtureEvent event,IUnknown*){
    if(closing_seen||event!=(closing_early?CloneUploadFixtureEvent::BeforeOriginal:CloneUploadFixtureEvent::BeforeStage))return;
    closing_seen=true;auto* d=closing_session->device.p;const ULONG before=references(d);
    CloneUploadClose result{};check(close_clone_upload(d,closing_arm,&result)==S_OK&&result==CloneUploadClose::Deferred,"active explicit close returns Deferred without waiting");
    const auto view=pin_view(d);check(view.closing&&view.scope_active&&view.references==1&&references(d)==before,"Deferred keeps exact actual wrapper pin count");
    check(disarm_clone_upload(closing_store)==S_FALSE,"old manual disarm still refuses active scope");
    unsigned char vb=0xa5,ib=0xa5;CloneUploadPairRequest request;request.arm=closing_arm;request.generation=view.generation;
    CloneUploadPairResult copied;
    check(copy_clone_upload_pair(d,request,&vb,1,&ib,1,&copied)==S_FALSE&&copied.status==CloneUploadPairStatus::Closing&&vb==0xa5&&ib==0xa5,"Closing refuses CPU pair before payload access");
    closing_probe->expect(closing_arm);
}
struct BoundaryError {};
unsigned boundary_mode=0,boundary_calls=0,native_unwinds=0;
std::jmp_buf lifecycle_recovery;
MeshCase* abort_case=nullptr;
HRESULT STDMETHODCALLTYPE lifecycle_throwing_target(ID3DXMesh*,DWORD,const D3DVERTEXELEMENT9*,IDirect3DDevice9*,ID3DXMesh**){
    ++boundary_calls;
    if(boundary_mode)RaiseException(0xe3450191,0,0,nullptr);
    throw BoundaryError{};
}
void deferred_controls(MeshCreate create_mesh,Create create,HWND window,cu::Store& store){
    ReadableSession s(create,window);MeshCase c(create_mesh,s,0);const auto before=store.statistics().staged_bytes;
    check(arm_clone_upload(&store,s.device.p,target_decl)==S_OK,"arm real Clone deferred close");
    closing_session=&s;closing_store=&store;closing_arm=pin_view(s.device.p).arm;closing_seen=false;closing_early=false;
    {DeviceReleaseProbe probe(s.device.p);closing_probe=&probe;clone_upload_fixture_hook(closing_callback);
        ok(c.run(s),"explicit deferred close preserves real Clone success");clone_upload_fixture_hook(nullptr);
        check(closing_seen,"real Clone reached explicit close seam");probe.verify();}
    no_pin(s.device.p);check(!clone_upload_fixture_scope_active()&&store.statistics().staged_bytes==before&&store.discarded_arena_is_zero(),"normal finish unbinds closes and wipes without later staging");
    c.close();finish_session(s);
}
}
#pragma GCC push_options
#pragma GCC optimize("no-exceptions")
extern "C" __attribute__((noinline)) void lifecycle_native_invoke(){
    clone_mesh_upload_target(lifecycle_throwing_target,abort_case->source.p,D3DXMESH_MANAGED|D3DXMESH_WRITEONLY,
                            target_decl,closing_session->device.p,&abort_case->clone.p);
}
extern "C" EXCEPTION_DISPOSITION __cdecl lifecycle_outer_handler(EXCEPTION_RECORD* record,void* frame,CONTEXT*,void*){
    if(!(record->ExceptionFlags&(EXCEPTION_UNWINDING|EXCEPTION_EXIT_UNWIND))&&record->ExceptionCode==0xe3450191){
        ++native_unwinds;RtlUnwind(frame,nullptr,nullptr,nullptr);
        void* previous=*static_cast<void**>(frame);asm volatile("movl %0,%%fs:0"::"r"(previous):"memory");
        std::longjmp(lifecycle_recovery,1);
    }
    return ExceptionContinueSearch;
}
extern "C" __attribute__((naked)) void lifecycle_outer(){
    asm volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $8,%esp\n\t"
        "movl %fs:0,%eax\n\tmovl %eax,-8(%ebp)\n\t"
        "movl $_lifecycle_outer_handler,-4(%ebp)\n\tleal -8(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
        "call _lifecycle_native_invoke\n\tmovl -8(%ebp),%edx\n\tmovl %edx,%fs:0\n\tleave\n\tret");
}
// Keep setjmp in a no-EH, no-local-state frame. The outer C++ test's loop,
// owned wrappers and reference-count snapshots are not setjmp locals.
__attribute__((noinline)) bool lifecycle_catch_native(){
    if(setjmp(lifecycle_recovery))return true;
    lifecycle_outer();return false;
}
#pragma GCC pop_options
namespace {
void boundary_abort_controls(MeshCreate create_mesh,Create create,HWND window,cu::Store& store){
    for(unsigned mode=0;mode<2;++mode){ReadableSession s(create,window);MeshCase c(create_mesh,s,0);
        check(arm_clone_upload(&store,s.device.p,target_decl)==S_OK,"arm new-scope boundary abort");
        closing_session=&s;closing_store=&store;closing_arm=pin_view(s.device.p).arm;closing_seen=false;closing_early=true;boundary_mode=mode;abort_case=&c;
        const auto count=references(s.device.p);void* chain=nullptr;asm volatile("movl %%fs:0,%0":"=r"(chain));
        {DeviceReleaseProbe probe(s.device.p);closing_probe=&probe;clone_upload_fixture_hook(closing_callback);
            if(!mode){bool caught=false;try{clone_mesh_upload_target(lifecycle_throwing_target,c.source.p,D3DXMESH_MANAGED|D3DXMESH_WRITEONLY,target_decl,s.device.p,&c.clone.p);}catch(const BoundaryError&){caught=true;}
                check(caught,"synthetic original C++ boundary propagates with real observer cleanup");
            }else check(lifecycle_catch_native(),"synthetic native boundary propagated to outer recovery");
            clone_upload_fixture_hook(nullptr);probe.verify();}
        void* after=nullptr;asm volatile("movl %%fs:0,%0":"=r"(after));
        check(after==chain&&closing_seen&&!clone_upload_fixture_scope_active(),"boundary abort restores chain and unbinds actual observer");
        no_pin(s.device.p);check(references(s.device.p)+1==count&&store.discarded_arena_is_zero(),"boundary abort drops exactly one actual pin and wipes");
        c.close();finish_session(s);
    }
    check(boundary_calls==2&&native_unwinds==1,"scope-only boundary exception counts");
}
struct BufferReleaseMutation {
    using Fn=ULONG(WINAPI*)(IUnknown*);static inline Fn original=nullptr;static inline unsigned calls=0;
    IUnknown* resource;void** previous;void* table[14]{};
    static ULONG WINAPI release(IUnknown* value){++calls;invalidate_native_buffer_evidence(value);return original(value);}
    explicit BufferReleaseMutation(IUnknown* p):resource(p),previous(*reinterpret_cast<void***>(p)){
        std::memcpy(table,previous,sizeof table);std::memcpy(&original,&table[2],sizeof original);auto fn=&release;std::memcpy(&table[2],&fn,sizeof fn);*reinterpret_cast<void***>(p)=table;calls=0;
    }
    ~BufferReleaseMutation(){*reinterpret_cast<void***>(resource)=previous;}
};
void release_and_duplicate(MeshCreate create_mesh,Create create,HWND window,cu::Store& store){
    for(unsigned mode=0;mode<2;++mode){ReadableSession s(create,window);MeshCase c(create_mesh,s,0);
        check(arm_clone_upload(&store,s.device.p,target_decl)==S_OK,"arm final Release and duplicate controls");ok(c.run(s),"actual Clone for final qualifier control");
        auto request=request_pair(c,s.device.p);Packet packet(c);
        if(!mode){
            c.final_ib.reset();{BufferReleaseMutation action(c.final_vb.p);c.final_vb.reset();}
            check(BufferReleaseMutation::calls==1&&packet.copy(s.device.p,request)==S_FALSE&&packet.result.status==CloneUploadPairStatus::Revision&&packet.untouched(),"last getter Release mutation vetoes both copied payloads");
        }else{
            c.final_vb.reset();c.final_ib.reset();MeshCase duplicate(create_mesh,s,0);ok(duplicate.run(s),"duplicate shape still forwards real Clone");
            check(packet.copy(s.device.p,request)==S_FALSE&&packet.result.status==CloneUploadPairStatus::Duplicate&&packet.untouched(),"duplicate poisoning remains Busy rather than replacement");duplicate.close();
        }
        check(disarm_clone_upload(&store)==S_OK,"disarm final Release and duplicate controls");c.close();finish_session(s);
    }
}
struct FailedReset {
    using Fn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
    IDirect3DDevice9* resource;void** previous;void* table[119]{};static inline unsigned calls=0;
    static HRESULT WINAPI fail(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*){++calls;return D3DERR_DEVICELOST;}
    explicit FailedReset(IDirect3DDevice9* d):resource(d),previous(*reinterpret_cast<void***>(d)){
        std::memcpy(table,previous,sizeof table);auto fn=&fail;std::memcpy(&table[16],&fn,sizeof fn);*reinterpret_cast<void***>(d)=table;calls=0;}
    ~FailedReset(){*reinterpret_cast<void***>(resource)=previous;}
};
void reset_controls(MeshCreate create_mesh,Create create,HWND window,cu::Store& store){
    ReadableSession s(create,window);MeshCase c(create_mesh,s,0);check(arm_clone_upload(&store,s.device.p,target_decl)==S_OK,"arm actual Reset controls");
    ok(c.run(s),"actual Clone before Reset");auto request=request_pair(c,s.device.p);c.final_vb.reset();c.final_ib.reset();Packet packet(c);
    {FailedReset failure(borrowed_native_device(s.device.p));check(s.device->Reset(&s.pp)==D3DERR_DEVICELOST&&FailedReset::calls==1,"actual wrapper forwards injected native Reset failure");}
    const auto lost=pin_view(s.device.p);check(lost.references==1&&lost.generation>request.generation,"failed Reset preserves pin and advances generation");
    check(packet.copy(s.device.p,request)==S_FALSE&&packet.result.status==CloneUploadPairStatus::Device&&packet.untouched(),"lost generation refuses CPU copy without native access");
    ok(s.device->Reset(&s.pp),"actual native Reset recovery");const auto recovered=pin_view(s.device.p);request.generation=recovered.generation;
    check(recovered.arm.serial==lost.arm.serial&&packet.copy(s.device.p,request)==S_FALSE&&packet.result.status==CloneUploadPairStatus::Missing&&packet.untouched(),"successful Reset cannot resurrect pre-Reset payload");
    check(disarm_clone_upload(&store)==S_OK,"disarm actual Reset controls");c.close();finish_session(s);
}
}
namespace {
struct PairBarrier {
    HANDLE locked=CreateEventA(nullptr,TRUE,FALSE,nullptr),reset_entered=CreateEventA(nullptr,TRUE,FALSE,nullptr);
    std::atomic<bool> copied{false},native_started{false},native_after_copy{false};
    bool wait_ok=false,excluded=false;DWORD creation=GetCurrentThreadId(),reset_thread=0;
    ~PairBarrier(){CloseHandle(locked);CloseHandle(reset_entered);}
};
PairBarrier* pair_barrier=nullptr;
void pair_barrier_callback(CloneUploadFixtureEvent event,IUnknown*){
    auto& b=*pair_barrier;
    if(event==CloneUploadFixtureEvent::PairRegistryAcquired){SetEvent(b.locked);b.wait_ok=WaitForSingleObject(b.reset_entered,5000)==WAIT_OBJECT_0;b.excluded=!b.native_started.load();}
    else if(event==CloneUploadFixtureEvent::PairCopyCompleted)b.copied=true;
    else if(event==CloneUploadFixtureEvent::ResetEntry){b.reset_thread=GetCurrentThreadId();SetEvent(b.reset_entered);}
}
struct PairResetSpy {
    using Fn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);static inline Fn original=nullptr;
    IDirect3DDevice9* resource;void** previous;void* table[119]{};
    static HRESULT WINAPI reset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p){pair_barrier->native_started=true;pair_barrier->native_after_copy=pair_barrier->copied.load();return original(d,p);}
    explicit PairResetSpy(IDirect3DDevice9* d):resource(d),previous(*reinterpret_cast<void***>(d)){
        std::memcpy(table,previous,sizeof table);std::memcpy(&original,&table[16],sizeof original);auto fn=&reset;std::memcpy(&table[16],&fn,sizeof fn);*reinterpret_cast<void***>(d)=table;}
    ~PairResetSpy(){*reinterpret_cast<void***>(resource)=previous;}
};
void pair_reset_barrier(MeshCreate create_mesh,Create create,HWND window,cu::Store& store){
    ReadableSession s(create,window);s.device.reset();ok(s.factory->CreateDevice(0,D3DDEVTYPE_HAL,window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&s.pp,&s.device.p),"create paired Reset device on main thread");
    MeshCase c(create_mesh,s,0);check(arm_clone_upload(&store,s.device.p,target_decl)==S_OK,"arm paired Reset barrier");ok(c.run(s),"actual Clone before pair barrier");
    auto request=request_pair(c,s.device.p);c.final_vb.reset();c.final_ib.reset();Packet packet(c);PairBarrier barrier;pair_barrier=&barrier;
    HRESULT copied=E_FAIL,reset_hr=E_FAIL;
    {PairResetSpy spy(borrowed_native_device(s.device.p));clone_upload_fixture_hook(pair_barrier_callback);
        std::thread worker([&]{copied=packet.copy(s.device.p,request);});const bool ready=WaitForSingleObject(barrier.locked,5000)==WAIT_OBJECT_0;
        if(ready)reset_hr=s.device->Reset(&s.pp);else SetEvent(barrier.reset_entered);
        worker.join();clone_upload_fixture_hook(nullptr);
        check(ready&&barrier.wait_ok&&barrier.excluded,"actual Reset waits behind complete pair registry guard");}
    check(copied==S_OK&&packet.result.record.producer_payload_valid&&barrier.native_started&&barrier.native_after_copy,
          "metadata VB and IB copy complete before native Reset starts");
    check(barrier.reset_thread==barrier.creation,"pair barrier native Reset uses creation thread");
    std::printf("PAIR_RESET copy=%08lx reset=%08lx\n",static_cast<unsigned long>(copied),static_cast<unsigned long>(reset_hr));
    packet.clear();check(packet.copy(s.device.p,request)==S_FALSE&&packet.untouched(),"post-barrier Reset never admits old pair again");
    check(disarm_clone_upload(&store)==S_OK,"disarm paired Reset barrier");c.close();finish_session(s);pair_barrier=nullptr;
}
}
int main(int argc,char** argv){try{
    setvbuf(stdout,nullptr,_IONBF,0);check(argc==2,"explicit B1 native D3DX path");
    HMODULE d3d=LoadLibraryA("C:\\windows\\system32\\d3d9.dll"),mesh=LoadLibraryA(argv[1]);check(d3d&&mesh,"load B1 D3D and native D3DX");
    Create create=nullptr;MeshCreate create_mesh=nullptr;auto a=GetProcAddress(d3d,"Direct3DCreate9"),b=GetProcAddress(mesh,"D3DXCreateMesh");
    std::memcpy(&create,&a,sizeof create);std::memcpy(&create_mesh,&b,sizeof create_mesh);check(create&&create_mesh,"B1 public fixture exports");
    char path[32768]{};check(GetModuleFileNameA(mesh,path,sizeof path)>0,"B1 D3DX module filename");
    const char marker[]="Wine builtin DLL";const auto* header=reinterpret_cast<const unsigned char*>(mesh);bool builtin=false;
    for(unsigned i=0;i+sizeof(marker)-1<=128;++i)builtin=builtin||!std::memcmp(header+i,marker,sizeof(marker)-1);
    check(!builtin,"B1 selected native D3DX route");std::printf("B1_D3DX path=%s create_mesh_rva=%08lx builtin=%u\n",path,
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(b)-reinterpret_cast<std::uintptr_t>(mesh)),unsigned(builtin));
    HWND window=CreateWindowExA(0,"STATIC","upload B1 lifecycle",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);check(window!=nullptr,"B1 fixture window");
    // One arena reused across device lifetimes, never destroyed by pin cleanup.
    auto store=std::make_unique<cu::Store>();
    basic_lifecycle(create,window,*store);pair_controls(create_mesh,create,window,*store);
    deferred_controls(create_mesh,create,window,*store);boundary_abort_controls(create_mesh,create,window,*store);
    release_and_duplicate(create_mesh,create,window,*store);reset_controls(create_mesh,create,window,*store);
    pair_reset_barrier(create_mesh,create,window,*store);
    DestroyWindow(window);FreeLibrary(mesh);FreeLibrary(d3d);
    std::printf("RESULT PASS phase=upload_lifecycle_b1 checks=%u capture_release_tested=0 inherited_callback_seh_tested=0\n",checks);return 0;
}catch(const std::exception& e){std::printf("RESULT FAIL %s checks=%u\n",e.what(),checks);return 1;}}
