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
#include "loading_admission_witness.h"
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
    auto vb=v.p;auto ib=i.p;
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
// Replace only the public GetDesc dispatch on a held object. No backend offset,
// module identity or implementation import is consulted by these controls.
template<class Buffer,class Desc> struct DescriptorSpy {
    using GetDesc=HRESULT(WINAPI*)(Buffer*,Desc*);
    static inline GetDesc original=nullptr;
    static inline unsigned mode=0;
    Buffer* object;void** prior;void* table[14]{};
    explicit DescriptorSpy(Buffer* p):object(p),prior(*reinterpret_cast<void***>(p)){
        std::copy_n(prior,14,table);original=reinterpret_cast<GetDesc>(table[13]);
        table[13]=reinterpret_cast<void*>(&get);*reinterpret_cast<void***>(object)=table;
    }
    ~DescriptorSpy(){*reinterpret_cast<void***>(object)=prior;mode=0;}
    static HRESULT WINAPI get(Buffer* p,Desc* out){
        const HRESULT result=original(p,out);if(FAILED(result))return result;
        switch(mode){
        case 1:return E_FAIL; // Populated output must never override failed status.
        case 2:out->Pool=D3DPOOL_DEFAULT;break;
        case 3:out->Usage|=D3DUSAGE_WRITEONLY;break;
        case 4:out->Format=D3DFMT_UNKNOWN;break;
        case 5:out->Size=0;break;
        case 6:out->Usage|=D3DUSAGE_DYNAMIC;break;
        case 7:out->Type=D3DRTYPE_TEXTURE;break;
        case 8:out->Usage|=D3DUSAGE_DONOTCLIP;break;
        }
        return result;
    }
};
static void public_descriptor_controls(ID3DXMesh* m){
    Com<IDirect3DVertexBuffer9>v;Com<IDirect3DIndexBuffer9>i;
    ok(m->GetVertexBuffer(&v.p),"public descriptor VB");ok(m->GetIndexBuffer(&i.p),"public descriptor IB");
    const char* reasons[]={"","descriptor_call","descriptor_pool","descriptor_usage","descriptor_format","descriptor_size","descriptor_usage","descriptor_format","descriptor_usage"};
    auto check=[&](auto& spy,const char* type){
        require(lt::fixture_cache_contract(m),"forwarding public GetDesc endpoint accepted");
        for(unsigned mode=1;mode<=8;++mode){
            spy.mode=mode;auto before=lt::fixture_cache_gate_reason(reasons[mode]);
            require(!lt::fixture_cache_contract(m)&&lt::fixture_cache_gate_reason(reasons[mode])==before+1,"public descriptor failure rejects with exact reason");
        }
        spy.mode=0;require(lt::fixture_cache_contract(m),"public descriptor restoration accepts");
        std::printf("PUBLIC_DESCRIPTOR type=%s forwarding=1 negatives=8 restored=1\n",type);
    };
    {DescriptorSpy<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC> spy(v.p);check(spy,"vertex");}
    {DescriptorSpy<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC> spy(i.p);check(spy,"index");}
}

static void replaced_metadata_control(ID3DXMesh* mesh){
    auto table=*reinterpret_cast<void***>(mesh);saved_vertices=reinterpret_cast<decltype(saved_vertices)>(table[5]);
    DWORD old=0,ignored=0;require(VirtualProtect(&table[5],sizeof(void*),PAGE_READWRITE,&old),"metadata slot writable");
    auto original=table[5];InterlockedExchangePointer(&table[5],reinterpret_cast<void*>(foreign_vertices));
    const auto before=lt::fixture_cache_gate_reason("mesh_method");const bool accepted=lt::fixture_cache_contract(mesh);
    InterlockedExchangePointer(&table[5],original);require(VirtualProtect(&table[5],sizeof(void*),old,&ignored),"metadata protection restored");
    require(!accepted&&lt::fixture_cache_gate_reason("mesh_method")==before+1,"changed recorded metadata method rejected");
}
static void off_tracker_routes(decltype(&Direct3DCreate9) factory,HWND window,const Input& input){
    IDirect3D9* native=factory(D3D_SDK_VERSION);require(native,"off tracker native factory");Com<IDirect3D9> api;
    ok(own::wrap_factory(native,&api.p,{}),"off tracker wrapped factory");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;
    Com<IDirect3DDevice9> device;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device.p),"off tracker device");
    Com<ID3DXMesh> mesh;createMesh(&D3DXCreateMesh,device.p,input,&mesh.p);Com<IDirect3DVertexBuffer9> vb;Com<IDirect3DIndexBuffer9> ib;
    ok(mesh->GetVertexBuffer(&vb.p),"off tracker VB");ok(mesh->GetIndexBuffer(&ib.p),"off tracker IB");
    auto control=[&](auto* buffer){
        own::BufferContentView view{};ok(own::get_buffer_content_view(buffer,&view),"off view recognized");require(!view.requested,"off request remains false");
        require(lt::fixture_cache_contract(mesh.p),"unchanged off-tracking route accepted");
        auto prior=*reinterpret_cast<void***>(buffer);void* table[14]{};std::copy_n(prior,14,table);
        // Never call the substituted slot: this probes wrapper recognition only.
        for(unsigned slot:{11u,12u}){
            auto saved=table[slot];table[slot]=reinterpret_cast<void*>(foreign_unlock);*reinterpret_cast<void***>(buffer)=table;
            const auto before=lt::fixture_cache_gate_reason("tracker");const bool accepted=lt::fixture_cache_contract(mesh.p);
            *reinterpret_cast<void***>(buffer)=prior;table[slot]=saved;
            require(!accepted&&lt::fixture_cache_gate_reason("tracker")==before+1,"changed off-tracking Lock/Unlock route rejected");
        }
    };
    control(vb.p);control(ib.p);std::printf("OFF_TRACKER_ROUTE vertex=2 index=2 requested_preserved=1\n");
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
      require(lt::fixture_initialize(GetModuleHandleW(nullptr)),"actual IAT hook installation");
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
        Com<ID3DXMesh>iat_mesh;createMesh(&D3DXCreateMesh,device,input,&iat_mesh.p);require(lt::fixture_cache_contract(iat_mesh.p),"fresh native preflight accepted");public_descriptor_controls(iat_mesh.p);replaced_metadata_control(iat_mesh.p);if(wrapped)off_tracker_routes(factory,window,input);
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
        require(lt::fixture_cache_gate_reason("mesh_method")>=1&&lt::fixture_cache_gate_reason("mesh_table")>=2&&lt::fixture_cache_gate_reason("descriptor_call")>=2,"mesh endpoint and public descriptor rejections have distinct reasons");
        auto saved_seed=seed;seed.x87.control&=~DWORD(0x2);Com<ID3DXMesh>unsupported_reference,unsupported_hook;createMesh(create,device,input,&unsupported_reference.p);createMesh(&D3DXCreateMesh,device,input,&unsupported_hook.p);auto unsupported_expected=generate(unsupported_reference.p,input.epsilon,true);auto fp_before=lt::fixture_cache_statistics();auto unsupported_actual=generate(unsupported_hook.p,input.epsilon,false);parity(unsupported_expected,unsupported_actual,"unsupported FP mode preserves native behavior");auto fp_after=lt::fixture_cache_statistics();require(fp_after.bypass_reasons[static_cast<unsigned>(x3m::mesh_adjacency_cache::BypassReason::FloatingPoint)]==fp_before.bypass_reasons[static_cast<unsigned>(x3m::mesh_adjacency_cache::BypassReason::FloatingPoint)]+1,"core FP rejection has exact reason");require(fp_after.unsupported_fp_available&&fp_after.unsupported_fp.mxcsr==seed.mxcsr&&fp_after.unsupported_fp.control==seed.x87.control,"unsupported FP first detail is published coherently");seed=saved_seed;
        // The game's actual state (x87 control 0x027f, MXCSR 0x9fc0: 53-bit precision, FTZ+DAZ) is keyed, not refused.
        {auto game=seed;game.x87.control=(game.x87.control&0xffff0000)|0x027f;game.mxcsr=0x9fc0;Com<ID3DXMesh>game_reference,game_hook,game_hook_second;createMesh(create,device,input,&game_reference.p);createMesh(&D3DXCreateMesh,device,input,&game_hook.p);createMesh(&D3DXCreateMesh,device,input,&game_hook_second.p);
            static const char* const gate_names[]={"unavailable","input","mesh_object","mesh_table","mesh_method","mesh_pool","mesh_options","vertex_acquire","index_acquire","buffer_missing","tracker","descriptor_call","descriptor_pool","descriptor_usage","descriptor_format","descriptor_size"};uint64_t gates_before[16];for(unsigned i=0;i<16;++i)gates_before[i]=lt::fixture_cache_gate_reason(gate_names[i]);
            const auto before=lt::fixture_cache_statistics();const auto saved_game_seed=seed;seed=game; // generate() applies seed before every call, so the game state must be the seed here
            auto expected=generate(game_reference.p,input.epsilon,true);auto first=generate(game_hook.p,input.epsilon,false);const auto mid=lt::fixture_cache_statistics();auto second=generate(game_hook_second.p,input.epsilon,false); // the reuse is taken on a content-equal second mesh, as the DYNAMIC loop does: under wrapped ownership the tracker reads the first mesh's buffers as ambiguous after its admitting miss (docs/verification/mesh-cache-hook.md)
            const auto after=lt::fixture_cache_statistics();seed=saved_game_seed;fp(seed);
            parity(expected,first,"game FP state fill parity");parity(expected,second,"game FP state hit parity");
            constexpr unsigned fp_reason=static_cast<unsigned>(x3m::mesh_adjacency_cache::BypassReason::FloatingPoint);char game_fp_label[1024];
            std::snprintf(game_fp_label,sizeof game_fp_label,"game FP state is keyed and reused, not bypassed (fp_bypass %llu->%llu hits %llu->%llu misses %llu->%llu calls %llu->%llu bypasses %llu->%llu rejected_fp %llu->%llu first_fp_supported=%u)",(unsigned long long)before.bypass_reasons[fp_reason],(unsigned long long)after.bypass_reasons[fp_reason],(unsigned long long)before.hits,(unsigned long long)after.hits,(unsigned long long)before.misses,(unsigned long long)after.misses,(unsigned long long)before.calls,(unsigned long long)after.calls,(unsigned long long)before.bypasses,(unsigned long long)after.bypasses,(unsigned long long)before.rejected_fp,(unsigned long long)after.rejected_fp,unsigned(after.first_fp_supported));
            {const size_t used=std::strlen(game_fp_label);auto view=[](const own::BufferContentView& v){static char t[96];std::snprintf(t,sizeof t,"known=%u ambiguous=%u pending=%u requested=%u status=%08lx rev=%llu",unsigned(v.known),unsigned(v.ambiguous),unsigned(v.pending_locks),unsigned(v.requested),(unsigned long)v.status,(unsigned long long)v.revision);return t;};
             std::snprintf(game_fp_label+used,sizeof game_fp_label-used," mid: calls=%llu misses=%llu hits=%llu first.before.vertex[%s] ",(unsigned long long)mid.calls,(unsigned long long)mid.misses,(unsigned long long)mid.hits,view(first.before.vertex));
             const size_t used2=std::strlen(game_fp_label);std::snprintf(game_fp_label+used2,sizeof game_fp_label-used2,"first.before.index[%s] ",view(first.before.index));const size_t used3=std::strlen(game_fp_label);std::snprintf(game_fp_label+used3,sizeof game_fp_label-used3,"second.before.vertex[%s] ",view(second.before.vertex));const size_t used4=std::strlen(game_fp_label);std::snprintf(game_fp_label+used4,sizeof game_fp_label-used4,"second.before.index[%s]",view(second.before.index));}
            for(unsigned i=0;i<16;++i){const uint64_t now=lt::fixture_cache_gate_reason(gate_names[i]);if(now!=gates_before[i]){const size_t used=std::strlen(game_fp_label);std::snprintf(game_fp_label+used,sizeof game_fp_label-used," gate_%s %llu->%llu",gate_names[i],(unsigned long long)gates_before[i],(unsigned long long)now);}}
            require(after.bypass_reasons[static_cast<unsigned>(x3m::mesh_adjacency_cache::BypassReason::FloatingPoint)]==before.bypass_reasons[static_cast<unsigned>(x3m::mesh_adjacency_cache::BypassReason::FloatingPoint)]&&after.hits==before.hits+1&&after.misses==before.misses+1,game_fp_label);
            require(after.first_fp_available&&after.first_fp_supported,"first incoming FP state published as supported");
            std::printf("GAME_FP_STATE control=027f mxcsr=9fc0 restored_control=%04lx restored_mxcsr=%08lx keyed=1 bypassed=0 hit=1\n",first.state.x87.control&0xffff,first.state.mxcsr);}
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
    require(device->Release()==0,"cache/hook retain no device references");require(api->Release()==0,"cache/hook retain no factory references");DestroyWindow(window);if(!loading_admission_witness())throw std::runtime_error("admission witness");printf("RESULT PASS checks=%u\n",checks);return 0;
 }catch(const std::exception&e){printf("RESULT FAIL checks=%u error=%s\n",checks,e.what());return 1;}
}
