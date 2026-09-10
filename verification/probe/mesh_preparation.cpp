// Original synthetic meshes: exact adjacency-cache parity and bounded retained memory.
// Standalone fixture only. No game hooks, assets, launch, rendering or persistent cache.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9mesh.h>
#include <array>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <limits>

static unsigned checks=0;
void require(bool good,const char* label){++checks;if(!good)throw std::runtime_error(label);}
void ok(HRESULT hr,const char* label){require(SUCCEEDED(hr),label);}
template<class T> struct Com {T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}Com()=default;Com(const Com&)=delete;};
template<class T>T symbol(HMODULE module,const char* name){auto p=GetProcAddress(module,name);T f=nullptr;static_assert(sizeof f==sizeof p);std::memcpy(&f,&p,sizeof f);require(f!=nullptr,name);return f;}
using Bytes=std::vector<unsigned char>;
using Adjacency=std::vector<DWORD>;
struct Vertex {float x,y,z,u,v;};
struct Input {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<WORD> indices;
    std::vector<DWORD> attributes;
    std::array<D3DVERTEXELEMENT9,3> decl={{{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()}};
    float epsilon=1e-6f;
    DWORD options=D3DXMESH_SYSTEMMEM;
};
void append(Bytes& b,const void* p,size_t n){if(n){auto v=static_cast<const unsigned char*>(p);b.insert(b.end(),v,v+n);}}
template<class T>void append(Bytes& b,const T& v){append(b,&v,sizeof v);}
constexpr size_t budget=2*1024*1024;
constexpr DWORD schema=1;
// Full input bytes avoid hash collisions. This is process-local and tied to the
// exact DLL fingerprint supplied by the runner, not a portable disk-cache format.
Bytes key(const Input& in,const std::string& runtime){
    const uint64_t size=uint64_t(in.vertices.size())*sizeof(Vertex)+uint64_t(in.indices.size())*sizeof(WORD)+sizeof(in.decl)+runtime.size()+128;
    if(size>budget)return {};
    Bytes out;out.reserve(size_t(size));
    append(out,schema);append(out,in.epsilon);append(out,in.options);
    DWORD count=DWORD(in.vertices.size());append(out,count);count=DWORD(in.indices.size());append(out,count);
    DWORD stride=sizeof(Vertex);append(out,stride);count=DWORD(runtime.size());append(out,count);
    append(out,runtime.data(),runtime.size());append(out,in.decl.data(),sizeof(in.decl));
    append(out,in.vertices.data(),in.vertices.size()*sizeof(Vertex));append(out,in.indices.data(),in.indices.size()*sizeof(WORD));
    return out;
}
// Fixed slot count and payload-capacity accounting. Caller mesh/output/scratch
// memory is separate. Admission is checked before retaining any new allocation.
struct Cache {
    struct Entry {Bytes key;Adjacency value;};
    std::array<Entry,8> entries;
    size_t used=sizeof(entries),next=0,hits=0,misses=0,evictions=0,rejected=0;
    bool get(const Bytes& k,Adjacency& out){
        if(!k.empty())for(const auto& e:entries)if(e.key==k){out=e.value;++hits;return true;}
        ++misses;return false;
    }
    bool put(const Bytes& k,const Adjacency& a,HRESULT hr){
        if(hr!=S_OK||k.empty()||uint64_t(k.size())+uint64_t(a.size())*sizeof(DWORD)>budget-sizeof(entries)){++rejected;return false;}
        Entry fresh;fresh.key=k;fresh.value=a;
        size_t cost=fresh.key.capacity()+fresh.value.capacity()*sizeof(DWORD);
        if(cost>budget-sizeof(entries)){++rejected;return false;}
        // Temporary candidate allocation is outside retained budget, bounded by
        // the same admission limit. Release slots before moving it into storage.
        while(used+cost>budget||!entries[next].key.empty()){
            auto& old=entries[next];used-=old.key.capacity()+old.value.capacity()*sizeof(DWORD);
            if(!old.key.empty())++evictions;
            Entry empty;old.key.swap(empty.key);old.value.swap(empty.value);
            if(used+cost<=budget)break;
            next=(next+1)%entries.size();
        }
        entries[next]=std::move(fresh);used+=cost;next=(next+1)%entries.size();
        require(used<=budget,"retained cache budget");return true;
    }
};
using Create=decltype(&D3DXCreateMesh);using Clean=decltype(&D3DXCleanMesh);
void createMesh(Create create,IDirect3DDevice9* device,const Input& in,ID3DXMesh** result){
    ok(create(DWORD(in.indices.size()/3),DWORD(in.vertices.size()),in.options,in.decl.data(),device,result),"CreateMesh");
    auto mesh=*result;void* data=nullptr;ok(mesh->LockVertexBuffer(0,&data),"lock vertices");std::memcpy(data,in.vertices.data(),in.vertices.size()*sizeof(Vertex));ok(mesh->UnlockVertexBuffer(),"unlock vertices");
    ok(mesh->LockIndexBuffer(0,&data),"lock indices");std::memcpy(data,in.indices.data(),in.indices.size()*sizeof(WORD));ok(mesh->UnlockIndexBuffer(),"unlock indices");
    DWORD* attrs=nullptr;ok(mesh->LockAttributeBuffer(0,&attrs),"lock attributes");std::memcpy(attrs,in.attributes.data(),in.attributes.size()*sizeof(DWORD));ok(mesh->UnlockAttributeBuffer(),"unlock attributes");
}
Bytes snapshot(ID3DXMesh* mesh){
    Bytes b;auto vertices=mesh->GetNumVertices(),faces=mesh->GetNumFaces(),stride=mesh->GetNumBytesPerVertex(),options=mesh->GetOptions();
    append(b,vertices);append(b,faces);append(b,stride);append(b,options);
    D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE]{};ok(mesh->GetDeclaration(decl),"snapshot declaration");
    for(const auto& e:decl){append(b,e);if(e.Stream==0xff)break;}
    void* p=nullptr;ok(mesh->LockVertexBuffer(D3DLOCK_READONLY,&p),"snapshot vertices");append(b,p,size_t(vertices)*stride);ok(mesh->UnlockVertexBuffer(),"snapshot unlock vertices");
    ok(mesh->LockIndexBuffer(D3DLOCK_READONLY,&p),"snapshot indices");append(b,p,size_t(faces)*3*((options&D3DXMESH_32BIT)?4:2));ok(mesh->UnlockIndexBuffer(),"snapshot unlock indices");
    DWORD* attrs=nullptr;ok(mesh->LockAttributeBuffer(D3DLOCK_READONLY,&attrs),"snapshot attrs");append(b,attrs,size_t(faces)*4);ok(mesh->UnlockAttributeBuffer(),"snapshot unlock attrs");
    return b;
}
struct Result {HRESULT adjacency_hr=E_FAIL,clean_hr=E_FAIL,optimize_hr=E_FAIL;Adjacency adjacent,cleaned,optimized,face_remap;Bytes clean_mesh,mesh,vertex_remap;};
Result sequence(Create create,Clean clean,IDirect3DDevice9* device,const Input& in,Cache* cache,const std::string& fingerprint,bool* hit=nullptr){
    Com<ID3DXMesh> source,cleaned;createMesh(create,device,in,&source.p);Result r;r.adjacent.resize(in.indices.size(),0xffffffff);
    Bytes k;if(cache)k=key(in,fingerprint);
    bool found=cache&&cache->get(k,r.adjacent);if(hit)*hit=found;
    r.adjacency_hr=found?S_OK:source->GenerateAdjacency(in.epsilon,r.adjacent.data());
    if(cache&&!found)cache->put(k,r.adjacent,r.adjacency_hr);
    if(FAILED(r.adjacency_hr))return r; // Never cache failures or emulate engine fallback from a guessed hook.
    r.cleaned.resize(in.indices.size(),0xffffffff);Com<ID3DXBuffer> errors;
    r.clean_hr=clean(D3DXCLEANTYPE(3),source.p,r.adjacent.data(),&cleaned.p,r.cleaned.data(),&errors.p);
    if(FAILED(r.clean_hr))return r;
    r.cleaned.resize(size_t(cleaned->GetNumFaces())*3);r.clean_mesh=snapshot(cleaned.p);
    r.optimized.resize(r.cleaned.size(),0xffffffff);r.face_remap.resize(cleaned->GetNumFaces(),0xffffffff);Com<ID3DXBuffer> remap;
    r.optimize_hr=cleaned->OptimizeInplace(D3DXMESHOPT_VERTEXCACHE,r.cleaned.data(),r.optimized.data(),r.face_remap.data(),&remap.p);
    if(SUCCEEDED(r.optimize_hr)){r.mesh=snapshot(cleaned.p);if(remap.p)append(r.vertex_remap,remap->GetBufferPointer(),remap->GetBufferSize());}
    return r;
}
bool equal(const Result& a,const Result& b){return a.adjacency_hr==b.adjacency_hr&&a.clean_hr==b.clean_hr&&a.optimize_hr==b.optimize_hr&&a.adjacent==b.adjacent&&a.cleaned==b.cleaned&&a.optimized==b.optimized&&a.face_remap==b.face_remap&&a.clean_mesh==b.clean_mesh&&a.mesh==b.mesh&&a.vertex_remap==b.vertex_remap;}
Input grid(unsigned n){
    Input in;in.name="grid";
    for(unsigned y=0;y<=n;++y)for(unsigned x=0;x<=n;++x)in.vertices.push_back({float(x),float(y),0,float(x)/n,float(y)/n});
    for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){WORD a=WORD(y*(n+1)+x),b=a+1,c=a+WORD(n+1),d=c+1;WORD indices[]={a,b,c,b,d,c};in.indices.insert(in.indices.end(),indices,indices+6);in.attributes.push_back((x/8)%2);in.attributes.push_back((x/8)%2);}
    return in;
}
std::vector<Input> examples(){
    Input manifold;manifold.name="closed_manifold";manifold.vertices={{0,0,0,0,0},{1,0,0,1,0},{0,1,0,0,1},{0,0,1,1,1}};manifold.indices={0,2,1,0,1,3,1,2,3,2,0,3};
    Input seam;seam.name="duplicated_seam";seam.vertices={{0,0,0,0,0},{1,0,0,1,0},{0,1,0,0,1},{1,0,0,0,0},{1,1,0,1,1},{0,1,0,1,0}};seam.indices={0,1,2,3,4,5};
    Input closeSeam=seam;closeSeam.name="epsilon_seam";closeSeam.vertices[3].x+=5e-7f;closeSeam.vertices[5].y+=5e-7f;
    Input separate=seam;separate.name="disconnected";for(size_t i=3;i<6;++i)separate.vertices[i].x+=3;
    Input degenerate=seam;degenerate.name="degenerate";degenerate.indices={0,1,2,3,3,5};
    std::vector<Input> all={manifold,seam,closeSeam,separate,degenerate};for(auto& in:all)in.attributes.assign(in.indices.size()/3,0);return all;
}
long long counter(){LARGE_INTEGER v;QueryPerformanceCounter(&v);return v.QuadPart;}
double median(std::vector<double> values){std::sort(values.begin(),values.end());return values[values.size()/2];}
void benchmark(Create create,Clean clean,IDirect3DDevice9* device,const std::string& fingerprint){
    auto in=grid(96);Com<ID3DXMesh> mesh;createMesh(create,device,in,&mesh.p);Adjacency expected(in.indices.size());ok(mesh->GenerateAdjacency(in.epsilon,expected.data()),"benchmark warmup");
    Cache cache;auto k=key(in,fingerprint);require(cache.put(k,expected,S_OK),"benchmark admission");LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    std::vector<double> cold,warm;
    for(unsigned i=0;i<21;++i){Adjacency a(in.indices.size());auto begin=counter();HRESULT hr=mesh->GenerateAdjacency(in.epsilon,a.data());auto end=counter();ok(hr,"benchmark GenerateAdjacency");require(a==expected,"benchmark cold parity");cold.push_back(double(end-begin)*1e6/frequency.QuadPart);
        begin=counter();auto generated=key(in,fingerprint);Adjacency reused;bool hit=cache.get(generated,reused);end=counter();require(hit&&reused==expected,"benchmark reuse parity");warm.push_back(double(end-begin)*1e6/frequency.QuadPart);}
    std::printf("TIMING mesh=grid96 faces=%u vertices=%u repetitions=21 cold_median_us=%.3f reuse_median_us=%.3f cold_min_us=%.3f cold_max_us=%.3f reuse_min_us=%.3f reuse_max_us=%.3f retained_bytes=%u budget_bytes=%u\n",unsigned(in.indices.size()/3),unsigned(in.vertices.size()),median(cold),median(warm),*std::min_element(cold.begin(),cold.end()),*std::max_element(cold.begin(),cold.end()),*std::min_element(warm.begin(),warm.end()),*std::max_element(warm.begin(),warm.end()),unsigned(cache.used),unsigned(budget));
    std::vector<double> fullCold,fullWarm;
    for(unsigned i=0;i<7;++i){
        auto begin=counter();auto a=sequence(create,clean,device,in,nullptr,fingerprint);auto end=counter();fullCold.push_back(double(end-begin)*1e6/frequency.QuadPart);
        bool hit=false;begin=counter();auto b=sequence(create,clean,device,in,&cache,fingerprint,&hit);end=counter();fullWarm.push_back(double(end-begin)*1e6/frequency.QuadPart);
        require(hit&&equal(a,b)&&SUCCEEDED(a.optimize_hr),"large mesh full sequence parity");
    }
    std::printf("SEQUENCE_TIMING mesh=grid96 repetitions=7 cold_median_us=%.3f reuse_median_us=%.3f cold_min_us=%.3f cold_max_us=%.3f reuse_min_us=%.3f reuse_max_us=%.3f exact_parity=1 includes_creation_clean_optimize_snapshots=1\n",median(fullCold),median(fullWarm),*std::min_element(fullCold.begin(),fullCold.end()),*std::max_element(fullCold.begin(),fullCold.end()),*std::min_element(fullWarm.begin(),fullWarm.end()),*std::max_element(fullWarm.begin(),fullWarm.end()));
}
int main(int argc,char** argv){
    setvbuf(stdout,nullptr,_IONBF,0);HWND window=nullptr;HMODULE d3dx=nullptr,runtime=nullptr;
    try{
        require(argc==3,"DLL path and fingerprint arguments");require(sizeof(void*)==4,"x86 fixture");
        d3dx=LoadLibraryA(argv[1]);runtime=LoadLibraryA("d3d9.dll");require(d3dx&&runtime,"load modules");
        auto create=symbol<Create>(d3dx,"D3DXCreateMesh");auto clean=symbol<Clean>(d3dx,"D3DXCleanMesh");auto factory=symbol<decltype(&Direct3DCreate9)>(runtime,"Direct3DCreate9");
        WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3MeshFixture";RegisterClassA(&cls);window=CreateWindowA(cls.lpszClassName,"Original mesh fixture",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);require(window,"hidden window");
        {Com<IDirect3D9> api;api.p=factory(D3D_SDK_VERSION);require(api.p,"factory");Com<IDirect3DDevice9> device;D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device.p),"CreateDevice");
        for(auto in:examples()){
            Cache cache;auto baseline=sequence(create,clean,device.p,in,nullptr,argv[2]);bool hit=false;auto fill=sequence(create,clean,device.p,in,&cache,argv[2],&hit);require(!hit,"first lookup misses");require(equal(baseline,fill),"fill sequence parity");auto reuse=sequence(create,clean,device.p,in,&cache,argv[2],&hit);require(hit==SUCCEEDED(baseline.adjacency_hr),"successful adjacency cached");require(equal(baseline,reuse),"reuse sequence byte parity");
            size_t linked=std::count_if(baseline.adjacent.begin(),baseline.adjacent.end(),[](DWORD v){return v!=0xffffffff;});
            if(in.name=="closed_manifold")require(linked==12,"closed manifold links");
            if(in.name=="duplicated_seam"||in.name=="epsilon_seam")require(linked==2,"coincident seam links");
            if(in.name=="disconnected")require(linked==0,"disconnected boundary edges");
            std::printf("CASE name=%s adjacency_hr=%08lx clean_hr=%08lx optimize_hr=%08lx adjacency_words=%u mesh_bytes=%u cache_hit=%u exact_parity=1 linked_edges=%u optimize_called=%u\n",in.name.c_str(),baseline.adjacency_hr,baseline.clean_hr,baseline.optimize_hr,unsigned(baseline.adjacent.size()),unsigned(baseline.mesh.size()),unsigned(hit),unsigned(linked),unsigned(SUCCEEDED(baseline.clean_hr)));
        }
        auto base=examples()[2];Cache cache;auto original=sequence(create,clean,device.p,base,&cache,argv[2]);require(SUCCEEDED(original.adjacency_hr),"invalidation seed");
        for(unsigned variant=0;variant<7;++variant){auto changed=base;std::string fingerprint=argv[2];const char* name="";
            switch(variant){case 0:name="vertex_position";changed.vertices[3].x+=.25f;break;case 1:name="index_topology";std::swap(changed.indices[4],changed.indices[5]);break;case 2:name="declaration";changed.decl[1].UsageIndex=1;break;case 3:name="epsilon";changed.epsilon=1e-8f;break;case 4:name="vertex_uv";changed.vertices[0].u=.25f;break;case 5:name="runtime_identity";fingerprint+="-different";break;case 6:name="mesh_options";changed.options=D3DXMESH_MANAGED;break;}
            bool hit=true;auto a=sequence(create,clean,device.p,changed,nullptr,fingerprint);auto b=sequence(create,clean,device.p,changed,&cache,fingerprint,&hit);require(!hit,"mutated input invalidation");require(equal(a,b),"mutated input sequence parity");std::printf("INVALIDATION field=%s miss=1 exact_parity=1\n",name);
        }
        // Attribute changes are intentionally not adjacency inputs. Downstream
        // operations always run on the current mesh, so attributes are preserved.
        auto attrs=base;attrs.attributes[1]=7;bool hit=false;auto a=sequence(create,clean,device.p,attrs,nullptr,argv[2]);auto b=sequence(create,clean,device.p,attrs,&cache,argv[2],&hit);require(hit&&equal(a,b),"changed attributes downstream parity");std::printf("ATTRIBUTE_CHANGE adjacency_hit=1 downstream_exact_parity=1\n");
        Cache bounded;Adjacency value(128,42);for(unsigned i=0;i<40;++i){Bytes input(100000,static_cast<unsigned char>(i));require(bounded.put(input,value,S_OK),"eviction admission");require(bounded.used<=budget,"eviction memory bound");}
        Adjacency out;require(!bounded.get(Bytes(100000,0),out),"evicted entry misses");Bytes latest(100000,39);require(bounded.get(latest,out)&&out==value,"retained entry hits");require(!bounded.put(latest,value,E_FAIL),"failed result not admitted");require(!bounded.put(latest,value,S_FALSE),"non-S_OK success bypass preserves HRESULT");out[0]=99;Adjacency fresh;require(bounded.get(latest,fresh)&&fresh==value,"caller output mutation does not alter cache");Input oversize;oversize.vertices.resize(budget/sizeof(Vertex)+1);require(key(oversize,argv[2]).empty(),"oversized key bypass");require(!bounded.put(Bytes(budget,1),value,S_OK),"oversized entry bypass");
        Cache byteBounded;for(unsigned i=0;i<12;++i)require(byteBounded.put(Bytes(700000,static_cast<unsigned char>(i)),value,S_OK),"byte budget admission");require(byteBounded.evictions>0&&byteBounded.used<=budget,"byte budget evicts before slot limit");
        std::printf("BOUND slots=8 budget_bytes=%u retained_bytes=%u slot_evictions=%u byte_evictions=%u oversize_bypass=1 failed_result_bypass=1\n",unsigned(budget),unsigned(byteBounded.used),unsigned(bounded.evictions),unsigned(byteBounded.evictions));
        benchmark(create,clean,device.p,argv[2]);
        }DestroyWindow(window);FreeLibrary(d3dx);FreeLibrary(runtime);std::printf("RESULT PASS checks=%u\n",checks);return 0;
    }catch(const std::exception& e){std::printf("RESULT FAIL checks=%u error=%s\n",checks,e.what());if(window)DestroyWindow(window);if(d3dx)FreeLibrary(d3dx);if(runtime)FreeLibrary(runtime);return 1;}
}
