// Real imported D3DX mesh APIs and actual production shared-vtable cache hook.
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
#define main old_mesh_fixture_main
#include "mesh_preparation.cpp"
#undef main
#include "../../src/proxy/loading_trace.h"
#include "../../src/ownership/d3d9_ownership.h"
#include <cstdarg>
namespace x3m {void log(const char* format,...){va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');}}
namespace lt=x3m::loading_trace;namespace own=x3m::ownership;
using Generate=HRESULT(WINAPI*)(ID3DXMesh*,FLOAT,DWORD*);
struct Env {DWORD control,status,tag,ip,cs,dp,ds;};struct FP {Env x87;DWORD mxcsr;};
static FP fp(){FP v{};asm volatile("fnstenv %0\n\tfldenv %0\n\tstmxcsr %1":"=m"(v.x87),"=m"(v.mxcsr)::"memory");return v;}
static void fp(const FP&v){asm volatile("fldenv %0\n\tldmxcsr %1"::"m"(v.x87),"m"(v.mxcsr):"memory");}
static bool equal(const FP&a,const FP&b){return a.x87.control==b.x87.control&&a.x87.status==b.x87.status&&a.x87.tag==b.x87.tag&&a.mxcsr==b.mxcsr;}
static FP seed;static Generate raw_generate=nullptr;static bool wrapped=false;
struct Views {own::BufferContentView vertex,index;};
static Views views(ID3DXMesh*m){Views v;if(!wrapped)return v;Com<IDirect3DVertexBuffer9>vb;Com<IDirect3DIndexBuffer9>ib;ok(m->GetVertexBuffer(&vb.p),"tracker VB");ok(m->GetIndexBuffer(&ib.p),"tracker IB");ok(own::get_buffer_content_view(vb.p,&v.vertex),"tracker vertex view");ok(own::get_buffer_content_view(ib.p,&v.index),"tracker index view");return v;}
static bool equal(const own::BufferContentView&a,const own::BufferContentView&b){return a.revision==b.revision&&a.pending_locks==b.pending_locks&&a.last_lock_flags==b.last_lock_flags&&a.status==b.status&&a.requested==b.requested&&a.known==b.known&&a.ambiguous==b.ambiguous;}
static Bytes physical_bytes(ID3DXMesh*m){
    Com<IDirect3DVertexBuffer9>v;Com<IDirect3DIndexBuffer9>i;ok(m->GetVertexBuffer(&v.p),"physical VB");ok(m->GetIndexBuffer(&i.p),"physical IB");
    auto vb=own::borrowed_native_buffer_for_lock_contract(v.p);if(!vb)vb=v.p;auto ib=own::borrowed_native_buffer_for_lock_contract(i.p);if(!ib)ib=i.p;
    D3DVERTEXBUFFER_DESC vd{};D3DINDEXBUFFER_DESC id{};ok(vb->GetDesc(&vd),"physical VB desc");ok(ib->GetDesc(&id),"physical IB desc");Bytes bytes;void*data=nullptr;
    ok(vb->Lock(0,0,&data,D3DLOCK_READONLY),"physical vertex snapshot");append(bytes,data,vd.Size);ok(vb->Unlock(),"physical vertex snapshot release");
    ok(ib->Lock(0,0,&data,D3DLOCK_READONLY),"physical index snapshot");append(bytes,data,id.Size);ok(ib->Unlock(),"physical index snapshot release");return bytes;
}
struct Generated {Adjacency adjacency;HRESULT hr;DWORD error;FP state;Views before,after;bool cache_hit=false;};
static Generated generate(ID3DXMesh*m,float epsilon,bool raw){Generated g;g.adjacency.assign(size_t(m->GetNumFaces())*3,0xabcdef);auto bytes=physical_bytes(m);g.before=views(m);const auto hits=lt::fixture_cache_statistics().hits;
    fp(seed);SetLastError(0x12345);g.hr=raw?raw_generate(m,epsilon,g.adjacency.data()):m->GenerateAdjacency(epsilon,g.adjacency.data());g.error=GetLastError();g.state=fp();g.after=views(m);g.cache_hit=!raw&&lt::fixture_cache_statistics().hits>hits;require(physical_bytes(m)==bytes,"adjacency leaves all vertex/index bytes unchanged");return g;
}
static void parity(const Generated&a,const Generated&b,const char*label){
    require(a.hr==b.hr&&a.error==b.error&&a.adjacency==b.adjacency&&equal(a.state,b.state),label);
    if(!wrapped)return;
    require(a.after.vertex.revision==b.after.vertex.revision&&a.after.index.revision==b.after.index.revision&&a.after.vertex.pending_locks==b.after.vertex.pending_locks&&a.after.index.pending_locks==b.after.index.pending_locks,"tracker revision/pending parity");
    if(b.cache_hit){
        require(b.before.vertex.known&&b.before.index.known&&!b.before.vertex.pending_locks&&!b.before.index.pending_locks,"hit requires previously known unlocked inputs");
        require(b.after.vertex.known&&b.after.index.known&&!b.after.vertex.ambiguous&&!b.after.index.ambiguous&&!b.after.vertex.pending_locks&&!b.after.index.pending_locks,"actual flat read-only hit retains known state");
        require(b.after.vertex.revision==b.before.vertex.revision&&b.after.index.revision==b.before.index.revision,"hit records no write revision");
        require(b.after.vertex.last_lock_flags==(D3DLOCK_READONLY|D3DLOCK_NOSYSLOCK)&&b.after.index.last_lock_flags==(D3DLOCK_READONLY|D3DLOCK_NOSYSLOCK),"hit flags truthfully describe actual native mesh reads");
    }else require(equal(a.after.vertex,b.after.vertex)&&equal(a.after.index,b.after.index),"native forwarding full tracker parity");
}
static void create_fixture_mesh(Create,IDirect3DDevice9*,const Input&,ID3DXMesh**);
static Result full_sequence(Create create,Clean clean,IDirect3DDevice9*d,const Input&in,bool raw){
    Com<ID3DXMesh>m,cleaned;create_fixture_mesh(create,d,in,&m.p);Result r;auto g=generate(m.p,in.epsilon,raw);r.adjacency_hr=g.hr;r.adjacent=g.adjacency;if(FAILED(g.hr))return r;
    r.cleaned.resize(in.indices.size(),0xffffffff);Com<ID3DXBuffer>errors;r.clean_hr=clean(D3DXCLEANTYPE(3),m.p,r.adjacent.data(),&cleaned.p,r.cleaned.data(),&errors.p);if(FAILED(r.clean_hr))return r;
    r.cleaned.resize(size_t(cleaned->GetNumFaces())*3);r.clean_mesh=snapshot(cleaned.p);r.optimized.resize(r.cleaned.size(),0xffffffff);r.face_remap.resize(cleaned->GetNumFaces(),0xffffffff);Com<ID3DXBuffer>remap;
    r.optimize_hr=cleaned->OptimizeInplace(D3DXMESHOPT_VERTEXCACHE,r.cleaned.data(),r.optimized.data(),r.face_remap.data(),&remap.p);if(SUCCEEDED(r.optimize_hr)){r.mesh=snapshot(cleaned.p);if(remap.p)append(r.vertex_remap,remap->GetBufferPointer(),remap->GetBufferSize());}return r;
}
static HRESULT(WINAPI* saved_unlock)(ID3DXMesh*)=nullptr;
static HRESULT WINAPI foreign_unlock(ID3DXMesh*m){return saved_unlock(m);}
static DWORD(WINAPI* saved_vertices)(ID3DXMesh*)=nullptr;
static DWORD WINAPI foreign_vertices(ID3DXMesh*m){return saved_vertices(m);}
static void create32(Create create,IDirect3DDevice9*d,const Input&in,ID3DXMesh**out){
    ok(create(DWORD(in.indices.size()/3),DWORD(in.vertices.size()),in.options|D3DXMESH_32BIT,in.decl.data(),d,out),"Create32 mesh");
    void*p=nullptr;ok((*out)->LockVertexBuffer(0,&p),"32 vertices");std::memcpy(p,in.vertices.data(),in.vertices.size()*sizeof(Vertex));ok((*out)->UnlockVertexBuffer(),"32 vertices release");
    ok((*out)->LockIndexBuffer(0,&p),"32 indices");auto indices=static_cast<DWORD*>(p);for(size_t n=0;n<in.indices.size();++n)indices[n]=in.indices[n];ok((*out)->UnlockIndexBuffer(),"32 indices release");
    DWORD*attrs=nullptr;ok((*out)->LockAttributeBuffer(0,&attrs),"32 attributes");std::memcpy(attrs,in.attributes.data(),in.attributes.size()*sizeof(DWORD));ok((*out)->UnlockAttributeBuffer(),"32 attributes release");
}
static void create_fixture_mesh(Create create,IDirect3DDevice9*d,const Input&in,ID3DXMesh**out){if(in.options&D3DXMESH_32BIT)create32(create,d,in,out);else createMesh(create,d,in,out);}
static void write_changed_vertex(ID3DXMesh*m){void*p=nullptr;ok(m->LockVertexBuffer(0,&p),"dynamic writable acquisition");static_cast<Vertex*>(p)[3].x+=.25f;ok(m->UnlockVertexBuffer(),"dynamic writable release");}
static void negative_iat(ID3DXMesh*m){
    Com<IDirect3DVertexBuffer9>v;ok(m->GetVertexBuffer(&v.p),"IAT control buffer");auto native=own::borrowed_native_buffer_for_lock_contract(v.p);if(!native)native=v.p;
    auto endpoint=(*reinterpret_cast<void***>(native))[12];HMODULE module=nullptr;require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(endpoint),&module),"IAT control native module");
    auto slot=reinterpret_cast<PVOID*>(reinterpret_cast<BYTE*>(module)+0x235c4);auto original=*slot;DWORD protection=0,unused=0;
    require(VirtualProtect(slot,sizeof(PVOID),PAGE_READWRITE,&protection),"IAT control writable");InterlockedExchangePointer(slot,reinterpret_cast<BYTE*>(original)+1);
    // Preflight only: never call the deliberately invalid backend target.
    const bool accepted=lt::fixture_cache_contract(m);InterlockedExchangePointer(slot,original);require(VirtualProtect(slot,sizeof(PVOID),protection,&unused),"IAT control restored protection");
    require(!accepted&&*slot==original,"changed critical IAT target rejected before acquisition");
}
int main(int argc,char**argv){std::setvbuf(stdout,nullptr,_IONBF,0);
 try{
    require(argc==4,"cache mode, ownership mode, fault mode arguments");const bool enabled=std::strcmp(argv[1],"on")==0;wrapped=std::strcmp(argv[2],"wrapped")==0;const bool fault=std::strcmp(argv[3],"fault")==0;
    SetEnvironmentVariableW(L"X3M_TELEMETRY",L"1");SetEnvironmentVariableW(L"X3M_MESH_CACHE",enabled?L"1":L"0");
    auto dx=GetModuleHandleW(L"d3dx9_37.dll"),d3d=LoadLibraryW(L"d3d9.dll");require(dx&&d3d,"exact imported D3DX and builtin D3D9");auto create=symbol<Create>(dx,"D3DXCreateMesh");auto clean=symbol<Clean>(dx,"D3DXCleanMesh");auto factory=symbol<decltype(&Direct3DCreate9)>(d3d,"Direct3DCreate9");
    WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName="X3CacheHookFixture";RegisterClassA(&cls);HWND window=CreateWindowA(cls.lpszClassName,"Original mesh cache hook fixture",0,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);require(window,"hidden window");
    IDirect3D9*api=factory(D3D_SDK_VERSION);require(api,"factory");if(wrapped){IDirect3D9*w=nullptr;own::Options o{};o.track_buffer_writes=true;ok(own::wrap_factory(api,&w,o),"ownership factory");api=w;}
    IDirect3DDevice9*device=nullptr;D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device),"device");
    seed=fp();seed.x87.control=(seed.x87.control&0xffff0000)|0x007f;seed.x87.status&=0xffff0000;seed.mxcsr=0x1f80;fp(seed);
    {
      auto input=examples()[2];Com<ID3DXMesh>baseline,first,second;createMesh(create,device,input,&baseline.p);auto table=*reinterpret_cast<void***>(baseline.p);raw_generate=reinterpret_cast<Generate>(table[22]);
      Com<ID3DXMesh>baseline32;create32(create,device,input,&baseline32.p);auto raw32=reinterpret_cast<Generate>((*reinterpret_cast<void***>(baseline32.p))[22]);
      auto expected=generate(baseline.p,input.epsilon,true);require(expected.hr==S_OK,"native baseline adjacency");
      if(wrapped)printf("TRACKER baseline_vertex_revision=%llu baseline_index_revision=%llu vertex_flags=%08lx index_flags=%08lx known=%u\n",expected.after.vertex.revision,expected.after.index.revision,expected.after.vertex.last_lock_flags,expected.after.index.last_lock_flags,expected.after.vertex.known&&expected.after.index.known);
      require(lt::fixture_initialize(GetModuleHandleW(nullptr),lt::fixture_fingerprint(GetModuleHandleW(nullptr))),"actual IAT hook installation");
      createMesh(&D3DXCreateMesh,device,input,&first.p);createMesh(&D3DXCreateMesh,device,input,&second.p);require(table[22]!=reinterpret_cast<void*>(raw_generate),"actual native shared adjacency hook installed");
      auto before=lt::fixture_cache_statistics();auto filled=generate(first.p,input.epsilon,false);auto after_fill=lt::fixture_cache_statistics();auto reused=generate(second.p,input.epsilon,false);auto after_hit=lt::fixture_cache_statistics();parity(expected,filled,"actual hook first-call parity");parity(expected,reused,"actual hook repeated-call parity");
      if(enabled)require(after_fill.misses>before.misses&&after_hit.hits>after_fill.hits,"actual hook cache miss then hit");else require(!lt::fixture_cache_constructed()&&after_hit.calls==0,"off constructs/acquires nothing");
      for(auto in:examples()){auto a=full_sequence(create,clean,device,in,true);auto b=full_sequence(&D3DXCreateMesh,&D3DXCleanMesh,device,in,false);auto c=full_sequence(&D3DXCreateMesh,&D3DXCleanMesh,device,in,false);require(equal(a,b)&&equal(a,c),"full native hooked downstream parity");printf("CASE name=%s parity=1\n",in.name.c_str());}
      auto changed=input;changed.vertices[3].x+=.25f;Com<ID3DXMesh>different,changed_reference;createMesh(create,device,changed,&changed_reference.p);createMesh(&D3DXCreateMesh,device,changed,&different.p);auto changed_expected=generate(changed_reference.p,changed.epsilon,true);auto miss_before=lt::fixture_cache_statistics();auto changed_actual=generate(different.p,changed.epsilon,false);parity(changed_expected,changed_actual,"changed exact input parity");if(enabled)require(lt::fixture_cache_statistics().misses>miss_before.misses,"changed bytes miss");
      fp(seed);SetLastError(99);auto raw_null=raw_generate(first.p,input.epsilon,nullptr);auto raw_error=GetLastError();auto raw_fp=fp();fp(seed);SetLastError(99);auto hook_null=first->GenerateAdjacency(input.epsilon,nullptr);auto hook_error=GetLastError();auto hook_fp=fp();require(raw_null==hook_null&&raw_error==hook_error&&equal(raw_fp,hook_fp),"null native failure parity");
      if(enabled){
        // An unknown mesh Unlock slot fails the preflight before any core lock.
        Com<ID3DXMesh>foreign_mesh;createMesh(&D3DXCreateMesh,device,input,&foreign_mesh.p);
        std::array<void*,35> clone{};std::copy_n(table,clone.size(),clone.data());saved_unlock=reinterpret_cast<decltype(saved_unlock)>(clone[16]);clone[16]=reinterpret_cast<void*>(foreign_unlock);auto old=*reinterpret_cast<void***>(foreign_mesh.p);*reinterpret_cast<void***>(foreign_mesh.p)=clone.data();
        const auto rejects=lt::fixture_cache_gate_rejections(),calls=lt::fixture_cache_statistics().calls;
        parity(expected,generate(foreign_mesh.p,input.epsilon,false),"foreign forwarding endpoint native parity");
        require(lt::fixture_cache_gate_rejections()>rejects&&lt::fixture_cache_statistics().calls==calls,"foreign endpoint bypasses cache");*reinterpret_cast<void***>(foreign_mesh.p)=old;
        Com<ID3DXMesh>metadata_mesh;createMesh(&D3DXCreateMesh,device,input,&metadata_mesh.p);auto metadata_old=*reinterpret_cast<void***>(metadata_mesh.p);std::copy_n(metadata_old,clone.size(),clone.data());saved_vertices=reinterpret_cast<decltype(saved_vertices)>(clone[5]);clone[5]=reinterpret_cast<void*>(foreign_vertices);*reinterpret_cast<void***>(metadata_mesh.p)=clone.data();
        const auto metadata_calls=lt::fixture_cache_statistics().calls;parity(expected,generate(metadata_mesh.p,input.epsilon,false),"foreign metadata forwarding native parity");require(lt::fixture_cache_statistics().calls==metadata_calls,"foreign metadata endpoint bypasses cache");*reinterpret_cast<void***>(metadata_mesh.p)=metadata_old;
        Com<ID3DXMesh>iat_mesh;createMesh(&D3DXCreateMesh,device,input,&iat_mesh.p);require(lt::fixture_cache_contract(iat_mesh.p),"fresh native preflight accepted");negative_iat(iat_mesh.p);
        auto recursive_input=input;recursive_input.vertices[0].y+=.375f;Com<ID3DXMesh>recursive_mesh,recursive_reference;createMesh(create,device,recursive_input,&recursive_reference.p);createMesh(&D3DXCreateMesh,device,recursive_input,&recursive_mesh.p);auto recursive_expected=generate(recursive_reference.p,input.epsilon,true);const auto recursive=lt::fixture_cache_statistics().contention;lt::fixture_cache_reenter_once();parity(recursive_expected,generate(recursive_mesh.p,input.epsilon,false),"actual hook recursive miss parity");require(lt::fixture_cache_statistics().contention>recursive,"actual hook recursive cache lease bypass");
        if(wrapped){auto prior=views(first.p);require(!prior.vertex.known&&prior.vertex.ambiguous,"native nesting leaves conservative unknown evidence");auto previous_calls=lt::fixture_cache_statistics().calls;auto unknown=generate(first.p,input.epsilon,false);require(lt::fixture_cache_statistics().calls==previous_calls&&!unknown.after.vertex.known&&unknown.after.vertex.ambiguous,"preexisting ambiguity bypasses and is never cleared");Com<ID3DXMesh>pending_mesh;createMesh(&D3DXCreateMesh,device,input,&pending_mesh.p);Com<IDirect3DVertexBuffer9>vb;ok(pending_mesh->GetVertexBuffer(&vb.p),"pending gate buffer");void*p=nullptr;ok(vb->Lock(0,0,&p,D3DLOCK_READONLY),"preexisting external read lock");auto rejections=lt::fixture_cache_gate_rejections();auto existing=lt::fixture_cache_statistics().calls;
          // Actual native call proceeds; the cache itself must acquire nothing.
          require(generate(pending_mesh.p,input.epsilon,false).hr==S_OK,"pending tracker native fallback");require(lt::fixture_cache_gate_rejections()>rejections&&lt::fixture_cache_statistics().calls==existing,"pending tracker gate bypass");ok(vb->Unlock(),"release external lock");}
      }
      {
        auto raw16=raw_generate;raw_generate=raw32;auto expected32=generate(baseline32.p,input.epsilon,true);Com<ID3DXMesh>first32,second32;create32(&D3DXCreateMesh,device,input,&first32.p);create32(&D3DXCreateMesh,device,input,&second32.p);
        auto previous32=lt::fixture_cache_statistics();auto generated32=generate(first32.p,input.epsilon,false);auto repeated32=generate(second32.p,input.epsilon,false);parity(expected32,generated32,"live32 first-call parity");parity(expected32,repeated32,"live32 repeated-call parity");
        if(enabled)require(lt::fixture_cache_statistics().misses>previous32.misses&&lt::fixture_cache_statistics().hits>previous32.hits,"actual32 adapter miss then hit");
        raw_generate=raw16;
        printf("LIVE32 parity=1 enabled=%u\n",enabled);
      }
      for(DWORD options:{0x990u,0x991u,0x18990u,0x18991u}){
        auto in=input;in.options=options;auto raw16=raw_generate;raw_generate=(options&D3DXMESH_32BIT)?raw32:raw16;
        Com<ID3DXMesh>reference,dynamic_first,dynamic_second;create_fixture_mesh(create,device,in,&reference.p);create_fixture_mesh(&D3DXCreateMesh,device,in,&dynamic_first.p);create_fixture_mesh(&D3DXCreateMesh,device,in,&dynamic_second.p);
        require(dynamic_first->GetOptions()==options,"exact game mesh options retained");
        {Com<IDirect3DVertexBuffer9>v;Com<IDirect3DIndexBuffer9>i;ok(dynamic_first->GetVertexBuffer(&v.p),"dynamic VB descriptor");ok(dynamic_first->GetIndexBuffer(&i.p),"dynamic IB descriptor");D3DVERTEXBUFFER_DESC vd{};D3DINDEXBUFFER_DESC id{};ok(v->GetDesc(&vd),"dynamic VB desc");ok(i->GetDesc(&id),"dynamic IB desc");require(vd.Pool==D3DPOOL_SYSTEMMEM&&id.Pool==D3DPOOL_SYSTEMMEM&&(vd.Usage&D3DUSAGE_DYNAMIC)&&(id.Usage&D3DUSAGE_DYNAMIC)&&!(vd.Usage&D3DUSAGE_WRITEONLY)&&!(id.Usage&D3DUSAGE_WRITEONLY),"actual dynamic SYSTEMMEM buffers without WRITEONLY");}
        auto native=generate(reference.p,in.epsilon,true);auto previous=lt::fixture_cache_statistics();auto filled=generate(dynamic_first.p,in.epsilon,false);auto repeated=generate(dynamic_second.p,in.epsilon,false);parity(native,filled,"dynamic game-option miss parity");parity(native,repeated,"dynamic game-option hit parity");
        if(enabled)require(lt::fixture_cache_statistics().misses==previous.misses+1&&lt::fixture_cache_statistics().hits==previous.hits+1,"each exact dynamic option misses then hits");
        Com<ID3DXMesh>changed_reference;create_fixture_mesh(create,device,in,&changed_reference.p);write_changed_vertex(changed_reference.p);write_changed_vertex(dynamic_second.p);
        auto changed_native=generate(changed_reference.p,in.epsilon,true);auto changed_before=lt::fixture_cache_statistics();auto changed_hook=generate(dynamic_second.p,in.epsilon,false);parity(changed_native,changed_hook,"write after hit exact dynamic input parity");
        if(enabled)require(lt::fixture_cache_statistics().misses==changed_before.misses+1&&!changed_hook.cache_hit,"write after hit forces a fresh-byte miss");
        auto downstream_native=full_sequence(create,clean,device,in,true);auto downstream_first=full_sequence(&D3DXCreateMesh,&D3DXCleanMesh,device,in,false);auto downstream_second=full_sequence(&D3DXCreateMesh,&D3DXCleanMesh,device,in,false);require(equal(downstream_native,downstream_first)&&equal(downstream_native,downstream_second),"dynamic clean/optimize/remap exact parity");
        printf("DYNAMIC options=%08lx parity=1 write_after_hit_miss=%u\n",options,enabled);raw_generate=raw16;
      }
      if(enabled){
        auto excluded=input;excluded.options=D3DXMESH_SYSTEMMEM|D3DXMESH_WRITEONLY;Com<ID3DXMesh>writeonly;createMesh(&D3DXCreateMesh,device,excluded,&writeonly.p);auto old_reason=lt::fixture_cache_gate_reason("mesh_options");require(!lt::fixture_cache_contract(writeonly.p)&&lt::fixture_cache_gate_reason("mesh_options")==old_reason+1,"writeonly remains rejected with exact reason");
        excluded.options=D3DXMESH_MANAGED;Com<ID3DXMesh>managed;createMesh(&D3DXCreateMesh,device,excluded,&managed.p);old_reason=lt::fixture_cache_gate_reason("mesh_pool");require(!lt::fixture_cache_contract(managed.p)&&lt::fixture_cache_gate_reason("mesh_pool")==old_reason+1,"managed pool remains rejected with exact reason");
        require(lt::fixture_cache_gate_reason("mesh_method")>=2&&lt::fixture_cache_gate_reason("backend_imports")>=1,"endpoint and import rejections have distinct reasons");
        auto saved_seed=seed;seed.mxcsr|=0x8000;Com<ID3DXMesh>unsupported_reference,unsupported_hook;createMesh(create,device,input,&unsupported_reference.p);createMesh(&D3DXCreateMesh,device,input,&unsupported_hook.p);auto unsupported_expected=generate(unsupported_reference.p,input.epsilon,true);auto fp_before=lt::fixture_cache_statistics();auto unsupported_actual=generate(unsupported_hook.p,input.epsilon,false);parity(unsupported_expected,unsupported_actual,"unsupported FP mode preserves native behavior");auto fp_after=lt::fixture_cache_statistics();require(fp_after.bypass_reasons[static_cast<unsigned>(x3m::mesh_adjacency_cache::BypassReason::FloatingPoint)]==fp_before.bypass_reasons[static_cast<unsigned>(x3m::mesh_adjacency_cache::BypassReason::FloatingPoint)]+1,"core FP rejection has exact reason");require(fp_after.unsupported_fp_available&&fp_after.unsupported_fp.mxcsr==seed.mxcsr&&fp_after.unsupported_fp.control==seed.x87.control,"unsupported FP first detail is published coherently");seed=saved_seed;
      }
      fp(seed);SetLastError(0x3344);const auto report_fp=fp();lt::report();require(GetLastError()==0x3344&&equal(report_fp,fp()),"report preserves LastError and computational state");
      Generate retained_thunk=reinterpret_cast<Generate>(table[22]);
      if(fault){require(enabled,"fault case cache enabled");lt::fixture_cache_cleanup_failure(E_OUTOFMEMORY);DWORD output[12];std::fill_n(output,12,0xaabbccdd);fp(seed);SetLastError(0x4567);const auto f=fp();auto hr=second->GenerateAdjacency(input.epsilon,output);require(hr==E_OUTOFMEMORY&&GetLastError()==0x4567&&equal(f,fp())&&output[0]==0xaabbccdd&&lt::fixture_cache_faulted(),"explicit cleanup-origin triggering HRESULT/state/output");
        auto blocked=lt::fixture_cache_blocked();DWORD reps[4]={0,1,2,3};ID3DXBuffer*remap=reinterpret_cast<ID3DXBuffer*>(0x12340000);ID3DXMesh*cleaned=first.p;ID3DXBuffer*errors=remap;
        require(second->GenerateAdjacency(input.epsilon,output)==E_FAIL,"later adjacency explicitly blocked");require(second->ConvertPointRepsToAdjacency(reps,output)==E_FAIL,"later point reps explicitly blocked");require(second->OptimizeInplace(D3DXMESHOPT_VERTEXCACHE,output,output,reps,&remap)==E_FAIL,"later optimize explicitly blocked");require(D3DXCleanMesh(D3DXCLEANTYPE(3),second.p,output,&cleaned,output,&errors)==E_FAIL,"later imported CleanMesh explicitly blocked");
        require(lt::fixture_cache_blocked()==blocked+4&&output[0]==0xaabbccdd&&reps[0]==0&&remap==reinterpret_cast<ID3DXBuffer*>(0x12340000)&&cleaned==first.p&&errors==remap,"blocked outputs unconsumed");require(GetLastError()==0x4567&&equal(f,fp()),"blocked calls preserve incoming state");lt::report();lt::report();
      }
      const auto final=lt::fixture_cache_statistics();printf("CACHE_RESULT enabled=%u wrapped=%u fault=%u calls=%llu hits=%llu misses=%llu native_calls=%llu rejections=%llu blocked=%llu retained=%llu\n",enabled,wrapped,fault,final.calls,final.hits,final.misses,final.native_calls,lt::fixture_cache_gate_rejections(),lt::fixture_cache_blocked(),final.retained_bytes);
      lt::shutdown();require(!lt::active(),"owned hooks fully restored");require(table[22]==reinterpret_cast<void*>(raw_generate),"native adjacency slot restored");
      auto after_shutdown=lt::fixture_cache_statistics();DWORD out[12]{};fp(seed);SetLastError(0x5678);auto h=retained_thunk(second.p,input.epsilon,out);require(h==(fault?E_FAIL:S_OK),"retained foreign-chain thunk after shutdown");require(lt::fixture_cache_statistics().calls==after_shutdown.calls,"shutdown disables further cache acquisition");
    }
    require(device->Release()==0,"cache/hook retain no device references");require(api->Release()==0,"cache/hook retain no factory references");DestroyWindow(window);printf("RESULT PASS checks=%u\n",checks);return 0;
 }catch(const std::exception&e){printf("RESULT FAIL checks=%u error=%s\n",checks,e.what());return 1;}
}
