// Actual production cache, original meshes. The earlier fixture supplies only
// original geometry/native sequence helpers; its prototype cache is never used.
#define main previous_mesh_fixture_main
#include "mesh_preparation.cpp"
#undef main
#include "../../src/proxy/mesh_adjacency_cache.h"
#include <thread>
namespace mac=x3m::mesh_adjacency_cache;
struct Env {DWORD control,status,tag,ip,cs,dp,ds;};
struct FP {Env x87;DWORD mxcsr;};
static FP read_fp(){FP f{};asm volatile("fnstenv %0\n\tfldenv %0\n\tstmxcsr %1":"=m"(f.x87),"=m"(f.mxcsr)::"memory");return f;}
static void write_fp(const FP& f){asm volatile("fldenv %0\n\tldmxcsr %1"::"m"(f.x87),"m"(f.mxcsr):"memory");}
static FP seed;
static bool same_fp(const FP&a,const FP&b){return a.x87.control==b.x87.control&&a.x87.status==b.x87.status&&a.x87.tag==b.x87.tag&&a.mxcsr==b.mxcsr;}
static mac::Generate backend=nullptr;
static std::atomic<unsigned> calls{0};static thread_local DWORD observed_error=0;static thread_local FP observed_fp{};
static unsigned behavior=0;
static bool disturb_acquisition=false;
static mac::Cache* nested_cache=nullptr;static mac::RuntimeIdentity identity;
static std::atomic<bool> entered{false},resume_native{false};
static HRESULT STDMETHODCALLTYPE native(ID3DXMesh*m,FLOAT epsilon,DWORD*out){
    ++calls;observed_error=GetLastError();observed_fp=read_fp();
    if(behavior==1){if(out)out[0]=123;SetLastError(0x9876);return E_FAIL;}
    if(behavior==2){if(out)out[0]=456;return S_FALSE;}
    if(behavior==3){auto hr=backend(m,epsilon,out);SetLastError(0x8765);return hr;}
    if(behavior==4){behavior=0;std::vector<DWORD> nested(m->GetNumFaces()*3);auto r=nested_cache->generate(m,epsilon,nested.data(),native,identity);require(r.origin==mac::Origin::Native,"recursive lease bypass");}
    if(behavior==5&&!entered.exchange(true)){while(!resume_native.load())Sleep(1);}
    if(behavior==6)return E_POINTER;
    if(behavior==8){auto hr=backend(m,epsilon,out);const DWORD error=GetLastError();asm volatile("fldz\n\tfldz\n\tfcompp":::"st","st(1)","memory");SetLastError(error);return hr;}
    if(behavior==7){const auto incoming=observed_error;auto hr=backend(m,epsilon,out);SetLastError(incoming==123?0:incoming);return hr;}
    const bool disturbed=disturb_acquisition;disturb_acquisition=false;auto hr=backend(m,epsilon,out);disturb_acquisition=disturbed;return hr;
}
struct Call {mac::Outcome result;Adjacency output;DWORD error;FP fp;};
static Call invoke(mac::Cache* cache,ID3DXMesh*m,float e,const FP& fp=seed,const mac::RuntimeIdentity* id=nullptr,DWORD incoming_error=0x12345){
    Call c;c.output.assign(size_t(m->GetNumFaces())*3,0xabababab);write_fp(fp);SetLastError(incoming_error);
    if(cache)c.result=cache->generate(m,e,c.output.data(),native,id?*id:identity);
    else c.result={native(m,e,c.output.data()),mac::Origin::Native};
    c.error=GetLastError();c.fp=read_fp();return c;
}
static void parity(const Call&a,const Call&b,const char* label){
    require(a.result.hr==b.result.hr&&a.output==b.output&&a.error==b.error&&same_fp(a.fp,b.fp),label);
}
static void create32(Create create,IDirect3DDevice9*d,const Input&in,ID3DXMesh**out){
    ok(create(DWORD(in.indices.size()/3),DWORD(in.vertices.size()),in.options|D3DXMESH_32BIT,in.decl.data(),d,out),"create32");
    void*p=nullptr;ok((*out)->LockVertexBuffer(0,&p),"lock32 vertex");std::memcpy(p,in.vertices.data(),in.vertices.size()*sizeof(Vertex));ok((*out)->UnlockVertexBuffer(),"unlock32 vertex");
    ok((*out)->LockIndexBuffer(0,&p),"lock32 index");auto idx=static_cast<DWORD*>(p);for(size_t i=0;i<in.indices.size();++i)idx[i]=in.indices[i];ok((*out)->UnlockIndexBuffer(),"unlock32 index");
    DWORD*attr=nullptr;ok((*out)->LockAttributeBuffer(0,&attr),"lock32 attrs");std::memcpy(attr,in.attributes.data(),in.attributes.size()*4);ok((*out)->UnlockAttributeBuffer(),"unlock32 attrs");
}
static Result sequence2(Create create,Clean clean,IDirect3DDevice9*d,const Input&in,mac::Cache*cache,bool*hit){
    Com<ID3DXMesh>m,cleaned;createMesh(create,d,in,&m.p);Result r;auto generated=invoke(cache,m.p,in.epsilon);r.adjacency_hr=generated.result.hr;r.adjacent=generated.output;if(hit)*hit=generated.result.origin==mac::Origin::CacheHit;
    if(FAILED(r.adjacency_hr))return r;
    r.cleaned.resize(in.indices.size(),0xffffffff);Com<ID3DXBuffer> errors;
    r.clean_hr=clean(D3DXCLEANTYPE(3),m.p,r.adjacent.data(),&cleaned.p,r.cleaned.data(),&errors.p);
    if(FAILED(r.clean_hr))return r;
    r.cleaned.resize(size_t(cleaned->GetNumFaces())*3);r.clean_mesh=snapshot(cleaned.p);
    r.optimized.resize(r.cleaned.size(),0xffffffff);r.face_remap.resize(cleaned->GetNumFaces(),0xffffffff);Com<ID3DXBuffer> remap;
    r.optimize_hr=cleaned->OptimizeInplace(D3DXMESHOPT_VERTEXCACHE,r.cleaned.data(),r.optimized.data(),r.face_remap.data(),&remap.p);
    if(SUCCEEDED(r.optimize_hr)){r.mesh=snapshot(cleaned.p);if(remap.p)append(r.vertex_remap,remap->GetBufferPointer(),remap->GetBufferSize());}return r;
}
// Isolated per-object vptr clone; native shared table is never patched.
using Lock=HRESULT(STDMETHODCALLTYPE*)(ID3DXMesh*,DWORD,void**);using Unlock=HRESULT(STDMETHODCALLTYPE*)(ID3DXMesh*);
static Lock lock_v=nullptr,lock_i=nullptr;static Unlock unlock_v=nullptr,unlock_i=nullptr;
static void disturb(){if(disturb_acquisition){FP f=read_fp();f.x87.control=(f.x87.control&0xffff0000)|0x037f;f.x87.status=(f.x87.status&0xffff0000)|0x4120;f.mxcsr=0x9fa0;write_fp(f);SetLastError(0xdeadbeef);}}
static unsigned fail_lock=0,fail_unlock=0;static int unlock_remaining=0;static unsigned unlock_calls=0;
static DWORD STDMETHODCALLTYPE extreme(ID3DXMesh*){return 0xffffffffu;}
static HRESULT STDMETHODCALLTYPE lv(ID3DXMesh*m,DWORD f,void**p){if(fail_lock==1){SetLastError(999);return E_FAIL;}auto hr=lock_v(m,f,p);disturb();return hr;}
static HRESULT STDMETHODCALLTYPE li(ID3DXMesh*m,DWORD f,void**p){if(fail_lock==2){SetLastError(999);return E_FAIL;}auto hr=lock_i(m,f,p);disturb();return hr;}
static HRESULT STDMETHODCALLTYPE uv(ID3DXMesh*m){++unlock_calls;if(fail_unlock==1&&unlock_remaining-- >0)return E_FAIL;return unlock_v(m);}
static HRESULT STDMETHODCALLTYPE ui(ID3DXMesh*m){++unlock_calls;if(fail_unlock==2&&unlock_remaining-- >0)return E_FAIL;return unlock_i(m);}
struct Table {
    ID3DXMesh*m;void**original;std::array<void*,35> slots;
    explicit Table(ID3DXMesh*p):m(p),original(*reinterpret_cast<void***>(p)){
        std::copy_n(original,slots.size(),slots.data());
        lock_v=reinterpret_cast<Lock>(slots[15]);unlock_v=reinterpret_cast<Unlock>(slots[16]);
        lock_i=reinterpret_cast<Lock>(slots[17]);unlock_i=reinterpret_cast<Unlock>(slots[18]);
        slots[15]=reinterpret_cast<void*>(lv);slots[16]=reinterpret_cast<void*>(uv);slots[17]=reinterpret_cast<void*>(li);slots[18]=reinterpret_cast<void*>(ui);
        *reinterpret_cast<void***>(m)=slots.data();
    }
    ~Table(){*reinterpret_cast<void***>(m)=original;}
};
int main(int argc,char**argv){
    setvbuf(stdout,nullptr,_IONBF,0);
    try{
        require(argc==2,"native DLL path");
        identity.algorithm_token=1; // Original fixture's process-local namespace.
        identity.public_contract=true;identity.generation=1;
        HMODULE d3dx=LoadLibraryA(argv[1]),runtime=LoadLibraryA("d3d9.dll");require(d3dx&&runtime,"modules");
        auto create=symbol<Create>(d3dx,"D3DXCreateMesh");auto clean=symbol<Clean>(d3dx,"D3DXCleanMesh");auto factory=symbol<decltype(&Direct3DCreate9)>(runtime,"Direct3DCreate9");
        WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3ProductionMeshCache";RegisterClassA(&cls);
        HWND window=CreateWindowA(cls.lpszClassName,"Original production-cache fixture",0,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);require(window,"hidden window");
        Com<IDirect3D9>api;api.p=factory(D3D_SDK_VERSION);require(api.p,"factory");Com<IDirect3DDevice9>device;
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;
        ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device.p),"device");
        seed=read_fp();seed.x87.control=(seed.x87.control&0xffff0000)|0x007f;seed.x87.status&=0xffff0000;seed.mxcsr=0x1f80;write_fp(seed);
        auto base=examples()[2];Com<ID3DXMesh>mesh;createMesh(create,device.p,base,&mesh.p);backend=reinterpret_cast<mac::Generate>((*reinterpret_cast<void***>(mesh.p))[22]);require(backend,"native GenerateAdjacency");
        for(auto in:examples()){
            mac::Cache cache;bool hit=false;auto a=sequence2(create,clean,device.p,in,nullptr,nullptr);auto b=sequence2(create,clean,device.p,in,&cache,&hit);require(!hit&&equal(a,b),"production fill downstream parity");
            auto c=sequence2(create,clean,device.p,in,&cache,&hit);require(hit&&equal(a,c),"production reuse downstream parity");
            std::printf("CASE name=%s adjacency_hr=%08lx clean_hr=%08lx optimize_hr=%08lx cache_hit=1 downstream_exact=1\n",in.name.c_str(),a.adjacency_hr,a.clean_hr,a.optimize_hr);
        }
        mac::Cache cache;auto baseline=invoke(nullptr,mesh.p,base.epsilon);auto fill=invoke(&cache,mesh.p,base.epsilon);auto hit=invoke(&cache,mesh.p,base.epsilon);
        parity(baseline,fill,"fill computational state parity");parity(baseline,hit,"hit computational state parity");require(hit.result.origin==mac::Origin::CacheHit,"exact reuse hit");
        // Actual D3DX is permitted to leave status unchanged. A separate original
        // callback supplies a known outgoing status to test replay on every backend.
        {mac::Cache c;behavior=8;auto expected=invoke(nullptr,mesh.p,base.epsilon);auto first=invoke(&c,mesh.p,base.epsilon);auto reused=invoke(&c,mesh.p,base.epsilon);behavior=0;
            require((expected.fp.x87.status&0x4500)==0x4000&&expected.fp.x87.status!=baseline.fp.x87.status&&expected.fp.x87.control==baseline.fp.x87.control&&expected.fp.x87.tag==baseline.fp.x87.tag&&expected.fp.mxcsr==baseline.fp.mxcsr&&expected.error==baseline.error,"authored outgoing status observed with native controls and LastError");
            parity(expected,first,"authored outgoing FP status miss parity");parity(expected,reused,"authored outgoing FP status hit parity");
            require(first.result.origin==mac::Origin::Native&&reused.result.origin==mac::Origin::CacheHit,"authored outgoing status replayed on real cache hit");
            std::printf("FP_AUTHORED equal_condition=4000 status_changed=1 miss_equal=1 hit_equal=1 controls_error_equal=1 native_status_changed=%u\n",unsigned((baseline.fp.x87.status&0xffff)!=(seed.x87.status&0xffff)));
        }
        std::printf("FP native_cw=%04lx native_sw=%04lx native_mxcsr=%08lx hit_computational_parity=1 diagnostic_instruction_pointers_excluded=1\n",baseline.fp.x87.control&0xffff,baseline.fp.x87.status&0xffff,baseline.fp.mxcsr);
        for(unsigned variation=0;variation<8;++variation){auto in=base;auto id=identity;
            switch(variation){case 0:in.vertices[3].x+=.25f;break;case 1:std::swap(in.indices[4],in.indices[5]);break;case 2:in.decl[1].UsageIndex=1;break;case 3:in.epsilon=1e-8f;break;case 4:in.vertices[0].u=.25f;break;case 5:id.algorithm_token++;break;case 6:id.generation++;break;case 7:in.options|=D3DXMESH_SOFTWAREPROCESSING;break;}
            Com<ID3DXMesh>m;createMesh(create,device.p,in,&m.p);auto a=invoke(nullptr,m.p,in.epsilon);auto b=invoke(&cache,m.p,in.epsilon,seed,&id);require(b.result.origin==mac::Origin::Native,"mutated exact key miss");parity(a,b,"mutated exact key parity");
        }
        auto attrs=base;attrs.attributes[1]=7;bool attrs_hit=false;auto ar=sequence2(create,clean,device.p,attrs,nullptr,nullptr);auto ac=sequence2(create,clean,device.p,attrs,&cache,&attrs_hit);require(attrs_hit&&equal(ar,ac),"attribute-only change retains downstream parity");
        {Com<ID3DXMesh>m;create32(create,device.p,base,&m.p);mac::Cache c;auto a=invoke(nullptr,m.p,base.epsilon);parity(a,invoke(&c,m.p,base.epsilon),"32bit fill");auto b=invoke(&c,m.p,base.epsilon);require(b.result.origin==mac::Origin::CacheHit,"32bit hit");parity(a,b,"32bit parity");}
        // Exact equality remains decisive even when the hash is deliberately constant.
        {mac::Cache c;c.fixture_force_hash_collision(true);auto a=invoke(&c,mesh.p,base.epsilon);auto changed=base;changed.vertices[3].x+=.25f;Com<ID3DXMesh>m;createMesh(create,device.p,changed,&m.p);auto b=invoke(&c,m.p,base.epsilon);require(b.result.origin==mac::Origin::Native,"hash collision misses different bytes");parity(invoke(nullptr,m.p,base.epsilon),b,"collision parity");require(invoke(&c,mesh.p,base.epsilon).result.origin==mac::Origin::CacheHit,"collision retains original key");require(a.result.hr==S_OK,"collision seed");}
        // Sticky status, precision, rounding and FTZ/DAZ are keyed and replayed
        // (variant 7 is the game's 0x027f/0x9fc0); unmasked exceptions bypass.
        for(unsigned variant=0;variant<10;++variant){FP state=seed;
            switch(variant){case 0:state.x87.status|=0x20;break;case 1:state.x87.status|=0x4100;break;case 2:state.x87.control=(state.x87.control&0xffff0000)|0x037f;break;case 3:state.x87.control|=0x0400;break;case 4:state.mxcsr|=0x8000;break;case 5:state.mxcsr|=0x40;break;case 6:state.mxcsr|=0x20;break;
                case 7:state.x87.control=(state.x87.control&0xffff0000)|0x027f;state.mxcsr=0x9fc0;break;case 8:state.x87.control&=~DWORD(0x2);break;case 9:state.mxcsr&=~DWORD(0x100);break;}
            // The expectation follows the state the host actually applies: Rosetta 2
            // (Steam bottle) keeps the SSE exception masks set, so variant 9 reads back
            // masked there and is keyed like a supported state; variant 8 (x87) applies everywhere.
            write_fp(state);const FP applied=read_fp();write_fp(seed);
            const bool masked=(applied.x87.control&0x3f)==0x3f&&(applied.mxcsr&0x1f80)==0x1f80;
            std::printf("FP_VARIANT variant=%u cw=%04lx sw=%04lx mxcsr=%08lx applied_cw=%04lx applied_sw=%04lx applied_mxcsr=%08lx masked=%u expect=%s\n",variant,state.x87.control&0xffff,state.x87.status&0xffff,state.mxcsr,applied.x87.control&0xffff,applied.x87.status&0xffff,applied.mxcsr,unsigned(masked),masked?"keyed":"bypass");
            if(variant<8)require(masked,"FP keyed variant applies a supported state");else if(variant==8)require(!masked,"FP x87 unmask applies");
            mac::Cache c;auto a=invoke(nullptr,mesh.p,base.epsilon,state);auto b=invoke(&c,mesh.p,base.epsilon,state);auto d=invoke(&c,mesh.p,base.epsilon,state);parity(a,b,"FP fill variant parity");parity(a,d,"FP reuse variant parity");
            char fp_label[160];std::snprintf(fp_label,sizeof fp_label,"FP unmasked exceptions bypass, controls are keyed (variant %u fill=%u reuse=%u applied_mxcsr=%08lx)",variant,unsigned(b.result.origin),unsigned(d.result.origin),applied.mxcsr);
            require((d.result.origin==mac::Origin::CacheHit)==masked,fp_label);
            if(variant<8)require(c.statistics().first_fp_available&&c.statistics().first_fp_supported&&c.statistics().first_fp.mxcsr==applied.mxcsr,"first incoming FP state published as supported");
        }
        write_fp(seed);
        for(unsigned mode=1;mode<=3;++mode){mac::Cache c;behavior=mode;auto a=invoke(nullptr,mesh.p,base.epsilon);parity(a,invoke(&c,mesh.p,base.epsilon),"native failure/alternate success/error fill");parity(a,invoke(&c,mesh.p,base.epsilon),"native failure/alternate success/error repeat");require(c.statistics().admissions==0,"only stable S_OK admitted");}behavior=0;
        {mac::Cache c;behavior=7;
            auto first=invoke(&c,mesh.p,base.epsilon,seed,nullptr,0);require(first.result.origin==mac::Origin::Native&&first.error==0&&c.statistics().admissions==1,"LastError zero candidate admitted");
            auto expected=invoke(nullptr,mesh.p,base.epsilon,seed,nullptr,123);auto changed=invoke(&c,mesh.p,base.epsilon,seed,nullptr,123);
            parity(expected,changed,"conditional native LastError change never bypassed");
            require(changed.result.origin==mac::Origin::Native&&changed.error==0&&c.statistics().rejected_last_error==1,"different incoming LastError misses and rejects nonpreserving candidate");
            auto repeated=invoke(&c,mesh.p,base.epsilon,seed,nullptr,0);parity(first,repeated,"original LastError key remains exact hit");
            require(repeated.result.origin==mac::Origin::CacheHit&&c.statistics().hits==1&&c.statistics().misses==2,"LastError key partitions reuse");
            std::printf("LAST_ERROR_KEY conditional_change=1 misses=2 hits=1 rejected=1\n");behavior=0;
        }
        {mac::Cache c;c.fixture_fail_next_allocation();auto a=invoke(&c,mesh.p,base.epsilon);parity(baseline,a,"allocation failure fallback");require(observed_error==0x12345&&same_fp(observed_fp,seed),"acquisition preserves native incoming state");require(c.statistics().allocation_failures==1,"allocation failure counter");}
        {mac::Cache c;nested_cache=&c;behavior=4;require(invoke(&c,mesh.p,base.epsilon).result.hr==S_OK,"reentrant native succeeds");require(c.statistics().contention==1,"reentrant contention counted");nested_cache=nullptr;}
        {mac::Cache c;Table table(mesh.p);table.slots[4]=reinterpret_cast<void*>(extreme);table.slots[5]=reinterpret_cast<void*>(extreme);table.slots[8]=reinterpret_cast<void*>(extreme);behavior=1;DWORD out=999;write_fp(seed);SetLastError(0x12345);auto r=c.generate(mesh.p,base.epsilon,&out,native,identity);require(r.hr==E_FAIL&&r.origin==mac::Origin::Native&&out==123&&c.statistics().bypasses==1,"hostile counts/stride bypass without arithmetic wrap");behavior=0;}
        {mac::Cache c;Table table(mesh.p);disturb_acquisition=true;auto a=invoke(&c,mesh.p,base.epsilon);require(observed_error==0x12345&&same_fp(observed_fp,seed),"successful acquisition restores incoming native FP/error");parity(baseline,a,"disturbed acquisition miss parity");auto b=invoke(&c,mesh.p,base.epsilon);require(b.result.origin==mac::Origin::CacheHit,"disturbed acquisition hit");parity(baseline,b,"disturbed acquisition hit restores state");disturb_acquisition=false;}
        {mac::Cache c;invoke(&c,mesh.p,base.epsilon);FP changed=seed;changed.x87.status|=0x0100;write_fp(changed);const FP applied=read_fp();write_fp(seed);auto b=invoke(&c,mesh.p,base.epsilon,changed);require(applied.x87.status!=seed.x87.status&&b.result.origin==mac::Origin::Native&&c.statistics().misses==2,"observably changed input FP status participates in exact key");}
        {mac::Cache c;auto id=identity;id.generation=0;require(invoke(&c,mesh.p,base.epsilon,seed,&id).result.origin==mac::Origin::Native&&c.statistics().bypasses==1,"unknown generation bypass");}
        for(unsigned mode=0;mode<2;++mode){mac::Cache c;auto id=identity;if(mode)id.public_contract=false;else id.algorithm_token=0;
            auto result=invoke(&c,mesh.p,base.epsilon,seed,&id);parity(baseline,result,"missing public algorithm identity preserves native result");
            require(result.result.origin==mac::Origin::Native&&c.statistics().acquired_bytes==0&&c.statistics().bypasses==1,"missing token or public contract performs no acquisition");}

        {mac::Cache c;behavior=5;entered.store(false);resume_native.store(false);Call worker;std::thread thread([&]{worker=invoke(&c,mesh.p,base.epsilon);});while(!entered.load())Sleep(1);auto concurrent=invoke(&c,mesh.p,base.epsilon);resume_native.store(true);thread.join();behavior=0;parity(baseline,worker,"lease owner parity");parity(baseline,concurrent,"thread contention native parity");require(c.statistics().contention==1,"thread contention bounded fallback");}
        {Com<ID3DXMesh>m;createMesh(create,device.p,base,&m.p);void*data=nullptr;ok(m->LockVertexBuffer(0,&data),"alias pointer acquire");ok(m->UnlockVertexBuffer(),"alias pointer release");mac::Cache c;behavior=1;write_fp(seed);auto r=c.generate(m.p,base.epsilon,static_cast<DWORD*>(data),native,identity);require(r.hr==E_FAIL&&r.origin==mac::Origin::Native&&c.statistics().bypasses==1,"output vertex alias bypasses before backend mutation");behavior=0;}
        {mac::Cache c;behavior=6;write_fp(seed);auto r=c.generate(mesh.p,base.epsilon,reinterpret_cast<DWORD*>(0xfffffff8u),native,identity);require(r.hr==E_POINTER&&r.origin==mac::Origin::Native&&c.statistics().bypasses==1,"overflowing output span bypass without dereference");behavior=0;}
        // Null optional output reaches the native function with the original pointer.
        {mac::Cache c;write_fp(seed);SetLastError(42);HRESULT a=backend(mesh.p,base.epsilon,nullptr);DWORD err=GetLastError();FP f=read_fp();write_fp(seed);SetLastError(42);auto b=c.generate(mesh.p,base.epsilon,nullptr,native,identity);DWORD err2=GetLastError();FP f2=read_fp();require(a==b.hr&&b.origin==mac::Origin::Native&&err==err2&&same_fp(f,f2),"null output native contract");}
        // Acquisition error fixture uses per-object vtable aliases, leaves shared slots intact.
        for(unsigned step=1;step<=2;++step){mac::Cache c;Table table(mesh.p);fail_lock=step;behavior=1;auto b=invoke(&c,mesh.p,base.epsilon);require(b.result.hr==E_FAIL&&b.output[0]==123&&b.error==0x9876,"failed lock calls native exact args/result");require(observed_error==0x12345&&same_fp(observed_fp,seed),"failed acquisition restores incoming env");fail_lock=0;behavior=0;require(c.statistics().acquisition_failures==1,"failed lock count");}
        for(unsigned step=1;step<=2;++step){mac::Cache c;Table table(mesh.p);fail_unlock=step;unlock_remaining=1;unlock_calls=0;auto b=invoke(&c,mesh.p,base.epsilon);parity(baseline,b,"unlock recovery native parity");require(b.result.origin==mac::Origin::Native&&unlock_calls>=2,"unlock recovery bypasses");fail_unlock=0;}
        {mac::Cache c;Table table(mesh.p);fail_unlock=1;unlock_remaining=2;unsigned before=calls;auto b=invoke(&c,mesh.p,base.epsilon);require(b.result.origin==mac::Origin::AcquisitionCleanupFailure&&b.result.hr==E_FAIL&&calls==before,"persistent unlock stops algorithm with distinct outcome");require(c.statistics().unrecoverable_unlocks==1,"persistent unlock count");fail_unlock=0;ok(unlock_v(mesh.p),"fixture explicitly repairs its injected retained lock");require(invoke(&c,mesh.p,base.epsilon).result.origin==mac::Origin::Native&&c.statistics().admissions==0,"poisoned instance remains disabled");}
        {mac::Cache c({sizeof(mac::Cache)+900,4096,2});for(unsigned i=0;i<8;++i){auto changed=base;changed.vertices[0].u=float(i);Com<ID3DXMesh>m;createMesh(create,device.p,changed,&m.p);invoke(&c,m.p,base.epsilon);require(c.statistics().retained_bytes<=sizeof(mac::Cache)+900,"retained bound");}require(c.statistics().evictions>0&&c.statistics().retained_entries<=2,"entry/byte eviction");c.clear();require(c.statistics().retained_entries==0&&c.statistics().retained_bytes==sizeof(mac::Cache),"clear frees all payload");}
        {mac::Cache c({sizeof(mac::Cache)+600,4096,8});for(unsigned i=0;i<8;++i){auto changed=base;changed.vertices[0].u=float(i);Com<ID3DXMesh>m;createMesh(create,device.p,changed,&m.p);invoke(&c,m.p,base.epsilon);require(c.statistics().retained_bytes<=sizeof(mac::Cache)+600,"byte-only retained bound");}require(c.statistics().evictions>0&&c.statistics().retained_entries<8,"byte eviction before slot ceiling");}
        {mac::Cache c({sizeof(mac::Cache)+20,20,1});parity(baseline,invoke(&c,mesh.p,base.epsilon),"oversize bypass");require(c.statistics().admissions==0,"oversize not admitted");}
        {auto in=base;in.options=D3DXMESH_MANAGED;Com<ID3DXMesh>m;createMesh(create,device.p,in,&m.p);mac::Cache c;parity(invoke(nullptr,m.p,in.epsilon),invoke(&c,m.p,in.epsilon),"unsupported managed bypass");require(c.statistics().bypasses==1,"managed bypass counted");}
        {auto in=base;in.options=D3DXMESH_SYSTEMMEM|D3DXMESH_DYNAMIC;Com<ID3DXMesh>m;createMesh(create,device.p,in,&m.p);mac::Cache unverified;
            auto expected=invoke(nullptr,m.p,in.epsilon);auto rejected=invoke(&unverified,m.p,in.epsilon);parity(expected,rejected,"dynamic without explicit readonly contract preserves native result");
            require(rejected.result.origin==mac::Origin::Native&&unverified.statistics().acquired_bytes==0&&unverified.statistics().bypass_reasons[static_cast<unsigned>(mac::BypassReason::Options)]==1,"dynamic contract defaults to no acquisition");
            auto verified=identity;verified.systemmem_dynamic_readonly_verified=true;mac::Cache accepted;auto first=invoke(&accepted,m.p,in.epsilon,seed,&verified);auto second=invoke(&accepted,m.p,in.epsilon,seed,&verified);parity(expected,first,"verified dynamic fill parity");parity(expected,second,"verified dynamic hit parity");require(first.result.origin==mac::Origin::Native&&second.result.origin==mac::Origin::CacheHit,"positive exact readonly contract admits dynamic");
        }
        // Timed production adapter includes real native readonly locks, key copy,
        // hash+memcmp and result copy. No fixture snapshots are inside these spans.
        auto big=grid(96);Com<ID3DXMesh>large;createMesh(create,device.p,big,&large.p);mac::Cache timed;invoke(&timed,large.p,big.epsilon);LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
        std::vector<double> fresh,reused,missed;for(unsigned i=0;i<15;++i){auto start=counter();auto a=invoke(nullptr,large.p,big.epsilon);auto end=counter();fresh.push_back(double(end-start)*1e6/frequency.QuadPart);timed.clear();start=counter();auto miss=invoke(&timed,large.p,big.epsilon);end=counter();missed.push_back(double(end-start)*1e6/frequency.QuadPart);parity(a,miss,"grid timed miss parity");require(miss.result.origin==mac::Origin::Native,"grid timed miss invokes native");start=counter();auto b=invoke(&timed,large.p,big.epsilon);end=counter();reused.push_back(double(end-start)*1e6/frequency.QuadPart);parity(a,b,"grid timed exact computational parity");require(b.result.origin==mac::Origin::CacheHit,"grid timed hit");}
        auto stats=timed.statistics();std::printf("TIMING faces=18432 vertices=9409 repetitions=15 native_median_us=%.3f adapter_miss_median_us=%.3f adapter_hit_median_us=%.3f includes_native_buffer_acquisition=1 retained_bytes=%llu scratch_cap_bytes=4194304 acquisition_ticks=%llu lookup_ticks=%llu copy_ticks=%llu native_ticks=%llu total_ticks=%llu hits=%llu misses=%llu\n",median(fresh),median(missed),median(reused),stats.retained_bytes,stats.acquisition_ticks,stats.lookup_ticks,stats.copy_ticks,stats.native_ticks,stats.total_ticks,stats.hits,stats.misses);
        std::printf("RESULT PASS checks=%u\n",checks);DestroyWindow(window);return 0;
    }catch(const std::exception&e){std::printf("RESULT FAIL checks=%u error=%s\n",checks,e.what());return 1;}
}
