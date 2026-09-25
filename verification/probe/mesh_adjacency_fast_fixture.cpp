// Original synthetic meshes against the real d3dx9_37 GenerateAdjacency: byte
// equality of the D3DX-rule module, rule evidence (weld refusal, heap order,
// entry lifetime, key window), a random differential sweep, timing, and the
// verify/fast hook path (the adjacency cache was removed on 2026-09-25). Never X3.
// argv[1] = "admission-only", "replay-safe" or "replay"
// followed by mesh-adjacency-<n>.bin dumps (loading_trace.h) to run native D3DX
// and the module on captured game meshes (game data: never committed).
#include "../../src/proxy/loading_trace.h"
#include "../../src/proxy/mesh_adjacency_fast.h"
#include <d3dx9.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "loading_admission_witness.h"
namespace x3m {void log(const char* fmt,...){va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);putchar('\n');}}
using namespace x3m::loading_trace;
namespace fast=x3m::mesh_adjacency_fast;
static unsigned checks=0;
void require(bool good,const char* label){++checks;if(!good){printf("FAIL %s error=%lu\n",label,GetLastError());throw std::runtime_error(label);}}
void ok(HRESULT hr,const char* label){require(SUCCEEDED(hr),label);}
template<class T> struct Com {T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}Com()=default;Com(const Com&)=delete;};
template<class T>T symbol(HMODULE m,const char* name){auto address=GetProcAddress(m,name);T fn=nullptr;static_assert(sizeof fn==sizeof address);std::memcpy(&fn,&address,sizeof fn);require(fn!=nullptr,name);return fn;}
using Create=decltype(&D3DXCreateMesh);
static uint64_t qpc(){LARGE_INTEGER v{};QueryPerformanceCounter(&v);return uint64_t(v.QuadPart);}
static double frequency=1;
static double us(uint64_t ticks){return double(ticks)*1e6/frequency;}
static float bits_float(uint32_t bits){float f;std::memcpy(&f,&bits,sizeof f);return f;}
constexpr float quantum=1.f/16384.f; // the engine's int16 position scale
static float grid(int k){return float(k)*quantum;}
struct Case {
    std::string name;std::vector<float> vertices;std::vector<uint32_t> indices;
    bool bits32=false;unsigned stride=20,position_offset=0;float epsilon=1e-6f;DWORD options=D3DXMESH_SYSTEMMEM;
    const char* expected_status="ok";bool tie_evidence=false;
    unsigned vertex_count()const{return unsigned(vertices.size()*sizeof(float)/stride);}
    unsigned face_count()const{return unsigned(indices.size()/3);}
};
static void push_vertex(Case& c,float x,float y,float z){
    const unsigned floats=c.stride/4;std::vector<float> v(floats,0.f);
    v[c.position_offset/4]=x;v[c.position_offset/4+1]=y;v[c.position_offset/4+2]=z;
    c.vertices.insert(c.vertices.end(),v.begin(),v.end());
}
static void push_face(Case& c,uint32_t a,uint32_t b,uint32_t d){c.indices.insert(c.indices.end(),{a,b,d});}
// The float at byte 0 of a vertex is D3DX's sweep key; with the position elsewhere it is set explicitly.
static void set_key(Case& c,unsigned v,float key){c.vertices[size_t(v)*(c.stride/4)]=key;}
static Case quad(const char* name){Case c;c.name=name;push_vertex(c,0,0,0);push_vertex(c,grid(16),0,0);push_vertex(c,grid(16),grid(16),0);push_vertex(c,0,grid(16),0);push_face(c,0,1,2);push_face(c,0,2,3);return c;}
static Case fan(const char* name,int tilt,std::array<unsigned,3> order){
    Case c;c.name=name;c.tie_evidence=true;
    push_vertex(c,0,0,0);push_vertex(c,grid(16),0,0);push_vertex(c,grid(8),grid(16),0);push_vertex(c,grid(8),grid(-16),0);push_vertex(c,grid(8),grid(-16),grid(tilt));
    const uint32_t faces[3][3]={{0,1,2},{1,0,3},{1,0,4}};
    for(unsigned i:order)push_face(c,faces[i][0],faces[i][1],faces[i][2]);
    return c;
}
static std::vector<Case> cases(){
    std::vector<Case> all;
    all.push_back(quad("quad-16"));
    {Case c=quad("quad-32");c.bits32=true;all.push_back(c);}
    {Case c=quad("exact-duplicates");push_vertex(c,0,0,0);push_vertex(c,grid(16),grid(16),0);c.indices.clear();push_face(c,0,1,2);push_face(c,4,5,3);all.push_back(c);}
    {Case c=quad("grid-neighbours-6.1e-5");push_vertex(c,grid(1),0,0);push_vertex(c,grid(16),grid(16),grid(1));c.indices.clear();push_face(c,0,1,2);push_face(c,4,5,3);all.push_back(c);}
    {Case c=quad("signed-zero");push_vertex(c,-0.f,0,-0.f);push_vertex(c,grid(16),grid(16),-0.f);c.indices.clear();push_face(c,0,1,2);push_face(c,4,5,3);all.push_back(c);}
    {Case c=quad("nan-position");c.vertices[3*5]=bits_float(0x7fc00000u);c.expected_status="non_finite";all.push_back(c);}
    {Case c=quad("position-offset-8");c.position_offset=8;c.vertices.clear();push_vertex(c,0,0,0);push_vertex(c,grid(16),0,0);push_vertex(c,grid(16),grid(16),0);push_vertex(c,0,grid(16),0);all.push_back(c);}
    {Case c=quad("epsilon-zero");c.epsilon=0.f;all.push_back(c);}
    {Case c=quad("unquantized-far");c.vertices[5]=0.3f;all.push_back(c);}
    {Case c=quad("unquantized-near-1.5e-6");c.vertices[5]=0.3f;c.vertices[15]=0.3f+1.5e-6f;c.vertices[16]=0;c.expected_status="epsilon_neighbour";all.push_back(c);}
    all.push_back(fan("fan-tilted-abc",16,{0,1,2}));
    all.push_back(fan("fan-tilted-bac",16,{1,0,2}));
    all.push_back(fan("fan-tilted-cba",16,{2,1,0}));
    all.push_back(fan("fan-coplanar-abc",0,{0,1,2}));
    all.push_back(fan("fan-coplanar-cba",0,{2,1,0}));
    // Normalize dispatch (run 11): two candidates whose cross products have
    // |n|^2 within 1e-5 of 1, so the generic table copies them unnormalized and
    // both score exactly 1.0 against the query (tie: the chain head, face 2,
    // wins), while the SSE2 table normalizes and prefers the less tilted face 1
    // by 1.4e-6. Native picks face 2 under FEX (generic) and face 1 under
    // Rosetta or on x86 hardware (SSE2); the other_normalize variant mismatches.
    {Case c;c.name="near-unit-normals";c.tie_evidence=true;
        push_vertex(c,0,0,0);push_vertex(c,grid(16384),0,0);push_vertex(c,grid(-4),grid(16384),0);
        push_vertex(c,grid(-1),grid(-16384),grid(-9));push_vertex(c,grid(-7),grid(-16384),grid(-29));
        push_face(c,0,1,2);push_face(c,1,0,3);push_face(c,1,0,4);all.push_back(c);}
    {Case c=fan("fan-four",16,{0,1,2});push_vertex(c,grid(8),grid(-16),grid(-16));push_face(c,1,0,5);push_face(c,0,1,2);all.push_back(c);}
    {Case c=quad("degenerate-aba-aaa");c.tie_evidence=true;c.indices.clear();push_face(c,0,1,0);push_face(c,2,2,2);push_face(c,0,1,2);all.push_back(c);}
    {Case c=quad("degenerate-lookup");c.tie_evidence=true;c.indices.clear();push_face(c,0,1,0);push_face(c,1,0,2);all.push_back(c);}
    {Case c=quad("degenerate-welded");c.tie_evidence=true;push_vertex(c,0,0,0);c.indices.clear();push_face(c,0,1,4);push_face(c,1,0,2);push_face(c,0,1,3);all.push_back(c);}
    {Case c=quad("degenerate-welded-041");c.tie_evidence=true;push_vertex(c,0,0,0);c.indices.clear();push_face(c,0,4,1);push_face(c,1,0,2);all.push_back(c);}
    {Case c=quad("degenerate-welded-041b");c.tie_evidence=true;push_vertex(c,0,0,0);c.indices.clear();push_face(c,0,4,1);push_face(c,0,1,2);all.push_back(c);}
    {Case c=quad("degenerate-welded-104");c.tie_evidence=true;push_vertex(c,0,0,0);c.indices.clear();push_face(c,1,0,4);push_face(c,0,1,2);all.push_back(c);}
    {Case c=quad("degenerate-welded-104b");c.tie_evidence=true;push_vertex(c,0,0,0);c.indices.clear();push_face(c,1,0,4);push_face(c,1,0,2);all.push_back(c);}
    {Case c=quad("degenerate-welded-401");c.tie_evidence=true;push_vertex(c,0,0,0);c.indices.clear();push_face(c,4,0,1);push_face(c,0,1,2);all.push_back(c);}
    {Case c=quad("degenerate-welded-410");c.tie_evidence=true;push_vertex(c,0,0,0);c.indices.clear();push_face(c,4,1,0);push_face(c,1,0,2);push_face(c,0,1,3);all.push_back(c);}
    {Case c=quad("degenerate-welded-451");c.tie_evidence=true;push_vertex(c,0,0,0);push_vertex(c,0,0,0);c.indices.clear();push_face(c,4,5,1);push_face(c,1,0,2);push_face(c,0,1,3);all.push_back(c);}
    {Case c=quad("degenerate-welded-541a");c.tie_evidence=true;push_vertex(c,0,0,0);push_vertex(c,0,0,0);c.indices.clear();push_face(c,5,4,1);push_face(c,1,0,2);all.push_back(c);}
    {Case c=quad("degenerate-welded-541b");c.tie_evidence=true;push_vertex(c,0,0,0);push_vertex(c,0,0,0);c.indices.clear();push_face(c,5,4,1);push_face(c,0,1,3);all.push_back(c);}
    {Case c=quad("degenerate-welded-abb");c.tie_evidence=true;push_vertex(c,grid(16),0,0);c.indices.clear();push_face(c,0,1,4);push_face(c,1,0,2);push_face(c,0,1,3);all.push_back(c);}
    {Case c=quad("degenerate-welded-second");c.tie_evidence=true;push_vertex(c,0,0,0);c.indices.clear();push_face(c,1,0,2);push_face(c,0,1,4);push_face(c,0,1,3);all.push_back(c);}
    {Case c=quad("duplicate-faces");c.tie_evidence=true;c.indices.clear();push_face(c,0,1,2);push_face(c,0,1,2);push_face(c,2,1,0);all.push_back(c);}
    {Case c=quad("double-adjacency-order");c.tie_evidence=true;c.indices.clear();push_face(c,0,1,2);push_face(c,2,1,3);push_face(c,2,1,0);all.push_back(c);}
    {Case c=quad("unreferenced-vertices");push_vertex(c,grid(40),0,0);push_vertex(c,grid(40),0,0);all.push_back(c);}
    // Decompiled rules (docs/reverse-engineering/d3dx-generate-adjacency.md).
    // Weld refusal: a face holding both raw indices keeps them apart; three
    // copies of one position where the heap order decides the representative.
    {Case c=quad("refusal-chain");c.tie_evidence=true;push_vertex(c,0,0,0);push_vertex(c,0,0,0);push_vertex(c,0,0,0);c.indices.clear();push_face(c,0,1,4);push_face(c,5,2,1);push_face(c,6,3,2);push_face(c,4,6,3);all.push_back(c);}
    {Case c=quad("refusal-pair-face");c.tie_evidence=true;push_vertex(c,0,0,0);push_vertex(c,0,0,0);c.indices.clear();push_face(c,4,5,1);push_face(c,1,2,5);push_face(c,0,3,2);push_face(c,2,1,0);all.push_back(c);}
    for(unsigned variant=0;variant<6;++variant){ // the duplicates sit at different indices, so the heap permutation among equal keys differs
        Case c;c.name="heap-order-"+std::to_string(variant);c.tie_evidence=true;
        const int p[9][3]={{0,0,0},{16,0,0},{16,16,0},{0,16,0},{0,0,0},{0,0,0},{16,16,0},{0,16,0},{16,0,16}};
        for(unsigned v=0;v<9;++v){const int* q=p[(v+variant*4)%9];push_vertex(c,grid(q[0]),grid(q[1]),grid(q[2]));}
        push_face(c,0,1,2);push_face(c,3,4,1);push_face(c,5,2,6);push_face(c,7,8,0);push_face(c,2,1,4);push_face(c,6,8,7);
        all.push_back(c);}
    // Entry lifetime: the querying edge is removed only after a successful
    // lookup (face 1's 1->2 survives and pairs face 2), and a refused selection
    // still consumes the candidate.
    {Case c=quad("own-entry-survives");c.tie_evidence=true;push_vertex(c,grid(8),grid(8),grid(16));c.indices.clear();push_face(c,0,1,2);push_face(c,1,2,4);push_face(c,2,1,0);all.push_back(c);}
    {Case c=quad("refused-consumed");c.tie_evidence=true;push_vertex(c,grid(8),grid(8),grid(16));c.indices.clear();push_face(c,0,1,2);push_face(c,2,1,3);push_face(c,2,1,0);push_face(c,1,2,4);all.push_back(c);}
    {Case c=quad("later-slot-prefilled");c.tie_evidence=true;push_vertex(c,grid(8),grid(8),grid(16));c.indices.clear();push_face(c,2,1,0);push_face(c,1,0,2);push_face(c,0,1,2);push_face(c,2,1,4);all.push_back(c);}
    // Key window: the sweep key is the float at byte 0 whatever element it is.
    // With TEXCOORD first, equal positions weld only when their u differs by at most epsilon.
    {Case c;c.name="key-window-offset-8-apart";c.tie_evidence=true;c.position_offset=8;c.stride=20;
        push_vertex(c,0,0,0);push_vertex(c,grid(16),0,0);push_vertex(c,grid(16),grid(16),0);push_vertex(c,0,grid(16),0);push_vertex(c,0,0,0);push_vertex(c,grid(16),grid(16),0);
        set_key(c,4,0.5f);set_key(c,5,0.5f);push_face(c,0,1,2);push_face(c,4,5,3);all.push_back(c);}
    {Case c;c.name="key-window-offset-8-equal";c.tie_evidence=true;c.position_offset=8;c.stride=20;
        push_vertex(c,0,0,0);push_vertex(c,grid(16),0,0);push_vertex(c,grid(16),grid(16),0);push_vertex(c,0,grid(16),0);push_vertex(c,0,0,0);push_vertex(c,grid(16),grid(16),0);
        set_key(c,0,0.5f);set_key(c,4,0.5f);set_key(c,2,0.25f);set_key(c,5,0.25f);push_face(c,0,1,2);push_face(c,4,5,3);all.push_back(c);}
    {Case c;c.name="key-window-offset-8-within-eps";c.tie_evidence=true;c.position_offset=8;c.stride=20;
        push_vertex(c,0,0,0);push_vertex(c,grid(16),0,0);push_vertex(c,grid(16),grid(16),0);push_vertex(c,0,grid(16),0);push_vertex(c,0,0,0);push_vertex(c,grid(16),grid(16),0);
        set_key(c,4,5e-7f);set_key(c,5,-5e-7f);push_face(c,0,1,2);push_face(c,4,5,3);all.push_back(c);}
    // Epsilon 0 takes D3DX's exact hash path with the same refusal.
    {Case c=quad("epsilon-zero-refusal");c.tie_evidence=true;c.epsilon=0.f;push_vertex(c,0,0,0);push_vertex(c,grid(16),grid(16),0);push_vertex(c,0,0,0);c.indices.clear();push_face(c,0,1,4);push_face(c,6,1,3);push_face(c,1,0,2);all.push_back(c);}
    // Many faces on one edge with distinct normals: the most parallel wins, ties keep the head.
    {Case c;c.name="fan-eight";c.tie_evidence=true;push_vertex(c,0,0,0);push_vertex(c,grid(16),0,0);push_vertex(c,grid(8),grid(16),0);
        for(int i=0;i<8;++i)push_vertex(c,grid(8),grid(-16),grid(i*4-14));
        push_face(c,0,1,2);for(uint32_t i=0;i<8;++i)push_face(c,1,0,3+i);push_face(c,0,1,2);all.push_back(c);}
    // Large meshes for timing: a shared-vertex grid, a split-vertex grid (every face
    // has private copies, the engine's usual welded layout) and a star with one
    // position duplicated per face (the degenerate D3DX bucket).
    {Case c;c.name="grid-224x224-32";c.bits32=true;const int n=224;for(int y=0;y<n;++y)for(int x=0;x<n;++x)push_vertex(c,grid(x-112),grid(y-112),grid((x*7+y*3)%5));
        for(int y=0;y+1<n;++y)for(int x=0;x+1<n;++x){const uint32_t a=y*n+x,b=a+1,d=a+n,e=d+1;push_face(c,a,b,e);push_face(c,a,e,d);}
        all.push_back(c);}
    {Case c;c.name="split-150x150-32";c.bits32=true;const int n=150;auto at=[&](int x,int y){push_vertex(c,grid(x-75),grid(y-75),grid((x*5+y*11)%7));};uint32_t v=0;
        for(int y=0;y+1<n;++y)for(int x=0;x+1<n;++x){at(x,y);at(x+1,y);at(x+1,y+1);push_face(c,v,v+1,v+2);v+=3;at(x,y);at(x+1,y+1);at(x,y+1);push_face(c,v,v+1,v+2);v+=3;}
        all.push_back(c);}
    {Case c;c.name="star-20000-32";c.bits32=true;const int n=20000;for(int i=0;i<n;++i){push_vertex(c,0,0,0);push_vertex(c,grid(1000+(i%97)),grid((i*13)%2000-1000),grid((i*7)%300));}
        for(int i=0;i<n;++i)push_face(c,uint32_t(2*i),uint32_t(2*i+1),uint32_t((2*(i+1)+1)%(2*n)));
        all.push_back(c);}
    {Case c;c.name="split-16";const int n=100;auto at=[&](int x,int y){push_vertex(c,grid(x-50),grid(y-50),0);};uint32_t v=0;
        for(int y=0;y+1<n;++y)for(int x=0;x+1<n;++x){if(v+6>65535)break;at(x,y);at(x+1,y);at(x+1,y+1);push_face(c,v,v+1,v+2);v+=3;at(x,y);at(x+1,y+1);at(x,y+1);push_face(c,v,v+1,v+2);v+=3;}
        all.push_back(c);}
    return all;
}
static void create_mesh(Create fn,IDirect3DDevice9* device,const Case& c,ID3DXMesh** out){
    std::vector<D3DVERTEXELEMENT9> decl;
    if(c.position_offset){decl.push_back({0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0});}
    decl.push_back({0,WORD(c.position_offset),D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0});
    if(!c.position_offset&&c.stride>=20)decl.push_back({0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0});
    decl.push_back(D3DDECL_END());
    ok(fn(c.face_count(),c.vertex_count(),c.options|(c.bits32?D3DXMESH_32BIT:0),decl.data(),device,out),"CreateMesh");
    require((*out)->GetNumBytesPerVertex()==c.stride,"stride as declared");
    void* data=nullptr;
    ok((*out)->LockVertexBuffer(0,&data),"fill vertices lock");std::memcpy(data,c.vertices.data(),c.vertices.size()*sizeof(float));ok((*out)->UnlockVertexBuffer(),"fill vertices unlock");
    ok((*out)->LockIndexBuffer(0,&data),"fill indices lock");
    if(c.bits32)std::memcpy(data,c.indices.data(),c.indices.size()*4);else{auto* w=static_cast<WORD*>(data);for(size_t i=0;i<c.indices.size();++i)w[i]=WORD(c.indices[i]);}
    ok((*out)->UnlockIndexBuffer(),"fill indices unlock");
    DWORD* attrs=nullptr;ok((*out)->LockAttributeBuffer(0,&attrs),"fill attrs lock");for(unsigned i=0;i<c.face_count();++i)attrs[i]=0;ok((*out)->UnlockAttributeBuffer(),"fill attrs unlock");
}
struct Native {HRESULT hr;DWORD error;std::vector<DWORD> adjacency;uint64_t ticks;};
using Generate=HRESULT(WINAPI*)(ID3DXMesh*,FLOAT,DWORD*);
// Slot 22 saved per shared vtable before the hooks (D3DX uses distinct classes
// for 16- and 32-bit meshes): the unhooked native method for a mesh's own class.
static std::vector<std::pair<PVOID*,Generate>> raw_generates;
static void remember_raw(ID3DXMesh* mesh){auto table=*reinterpret_cast<PVOID**>(mesh);for(const auto& r:raw_generates)if(r.first==table)return;Generate g=nullptr;std::memcpy(&g,&table[22],sizeof g);raw_generates.emplace_back(table,g);}
static Generate raw_for(ID3DXMesh* mesh){auto table=*reinterpret_cast<PVOID**>(mesh);for(const auto& r:raw_generates)if(r.first==table)return r.second;require(false,"unhooked native method known for this mesh class");return nullptr;}
static Native native_generate(ID3DXMesh* mesh,float epsilon,bool raw=false){
    Native n;n.adjacency.assign(size_t(mesh->GetNumFaces())*3,0xabcdefu);const Generate g=raw?raw_for(mesh):nullptr;SetLastError(0x1357);
    const auto begin=qpc();n.hr=raw?g(mesh,epsilon,n.adjacency.data()):mesh->GenerateAdjacency(epsilon,n.adjacency.data());n.ticks=qpc()-begin;n.error=GetLastError();return n;
}
struct Fast {fast::Report report;std::vector<DWORD> adjacency;uint64_t ticks;};
// The module must normalize with the D3DXVec3Normalize this process's D3DX
// installed (loading_trace.h, d3dx_math_table): the SSE2 table under Rosetta
// and on x86 hardware, the generic table under FEX. The fixture cannot reproduce
// the 3DNow and SSE tables (native-only in the service) and refuses to run there.
static fast::Normalize detected_normalize(){
    const D3dxMathTable table=d3dx_math_table();
    require(table==D3dxMathTable::Sse2||table==D3dxMathTable::Generic,"D3DX math table reproducible by the module");
    return table==D3dxMathTable::Generic?fast::Normalize::Generic:fast::Normalize::Sse2;
}
static fast::Policy default_policy(){fast::Policy p;p.normalize=detected_normalize();return p;}
static fast::Policy other_normalize(){fast::Policy p=default_policy();p.normalize=p.normalize==fast::Normalize::Generic?fast::Normalize::Sse2:fast::Normalize::Generic;return p;}
static Fast fast_generate(ID3DXMesh* mesh,const Case& c,const fast::Policy& policy=default_policy()){
    Fast f;f.adjacency.assign(size_t(mesh->GetNumFaces())*3,0xabcdefu);void* vb=nullptr;void* ib=nullptr;
    ok(mesh->LockVertexBuffer(D3DLOCK_READONLY,&vb),"fast lock vb");ok(mesh->LockIndexBuffer(D3DLOCK_READONLY,&ib),"fast lock ib");
    fast::Input in;in.vertices=vb;in.vertex_count=mesh->GetNumVertices();in.stride=mesh->GetNumBytesPerVertex();in.position_offset=c.position_offset;
    in.indices=ib;in.indices_32bit=c.bits32;in.face_count=mesh->GetNumFaces();in.epsilon=c.epsilon;
    const auto begin=qpc();f.report=fast::generate(in,reinterpret_cast<uint32_t*>(f.adjacency.data()),policy);f.ticks=qpc()-begin;
    ok(mesh->UnlockIndexBuffer(),"fast unlock ib");ok(mesh->UnlockVertexBuffer(),"fast unlock vb");return f;
}
static size_t mismatches(const std::vector<DWORD>& a,const std::vector<DWORD>& b,size_t& first){first=a.size();size_t n=0;for(size_t i=0;i<a.size();++i)if(a[i]!=b[i]){if(!n)first=i;++n;}return n;}
static void print_adjacency(const char* label,const std::vector<DWORD>& a){printf("%s",label);for(auto v:a)printf(" %ld",v==0xffffffffu?-1L:long(v));printf("\n");}
// mesh-adjacency-<n>.bin dumps (loading_trace.h): read, rebuild the mesh, run native D3DX and the module.
// Original failing callback: witnesses what the service passes to native after
// refusal. It deliberately returns a distinct HRESULT/LastError and writes output.
struct FpEnvironment {DWORD control,status,tag,ip,cs,dp,ds;};
struct FpState {FpEnvironment x87;DWORD mxcsr;};
static FpState read_state(){FpState f{};asm volatile("fnstenv %0\n\tfldenv %0\n\tstmxcsr %1":"=m"(f.x87),"=m"(f.mxcsr)::"memory");return f;}
static void write_state(const FpState& f){asm volatile("fldenv %0\n\tldmxcsr %1"::"m"(f.x87),"m"(f.mxcsr):"memory");}
static FpState fallback_expected{},fallback_observed{};
static DWORD fallback_error=0;static bool fallback_untouched=false;static unsigned fallback_calls=0;
static HRESULT WINAPI refused_original(ID3DXMesh*,FLOAT,DWORD* output){
    fallback_error=GetLastError();fallback_observed=read_state();++fallback_calls;
    fallback_untouched=true;for(unsigned i=0;i<9;++i)fallback_untouched&=output[i]==0xabcdefu;
    output[0]=0x12345678;SetLastError(0x2468);return E_FAIL;
}
static void admission_controls(IDirect3DDevice9* device){
    const unsigned before=checks;
    require(fixture_adjacency_registry_dword(ERROR_SUCCESS,REG_DWORD,4),"registry valid DWORD");
    require(!fixture_adjacency_registry_dword(ERROR_FILE_NOT_FOUND,REG_DWORD,4),"registry absent");
    require(!fixture_adjacency_registry_dword(ERROR_SUCCESS,REG_BINARY,4),"registry binary not DWORD");
    require(!fixture_adjacency_registry_dword(ERROR_SUCCESS,REG_SZ,4),"registry string not DWORD");
    require(!fixture_adjacency_registry_dword(ERROR_SUCCESS,REG_DWORD,3),"registry short DWORD");
    require(!fixture_adjacency_registry_dword(ERROR_MORE_DATA,REG_DWORD,8),"registry overlong DWORD");
    require(fixture_adjacency_registry_ambiguous(ERROR_SUCCESS,REG_DWORD,3),"short successful DWORD makes dispatch unknown");
    require(!fixture_adjacency_registry_ambiguous(ERROR_SUCCESS,REG_BINARY,4),"wrong registry type is ignored as native does");
    Case c=fan("admission-refusal",16,{0,1,2});ID3DXMesh* mesh=nullptr;create_mesh(&D3DXCreateMesh,device,c,&mesh);
    const FpState saved=read_state();FpState seed=saved;seed.x87.control=(seed.x87.control&0xffff0000)|0x027f;seed.x87.status&=0xffff0000;seed.x87.tag|=0xffff;seed.mxcsr=0x1f80;
    struct Domain {DWORD cw,mx,tag;bool supported;};
    const Domain domains[]={{0x023f,0x1f80,0xffff,true},{0x027f,0x9fc0,0xffff,true},
        {0x007f,0x1f80,0xffff,false},{0x037f,0x1f80,0xffff,false},
        {0x067f,0x1f80,0xffff,false},{0x0a7f,0x1f80,0xffff,false},{0x0e7f,0x1f80,0xffff,false},
        {0x027f,0x3f80,0xffff,false},{0x027f,0x1fc0,0xffff,false},{0x027f,0x9f80,0xffff,false},
        {0x027e,0x1f80,0xffff,false},{0x027f,0x1f00,0xffff,false},{0x027f,0x1f80,0xfffc,false}};
    unsigned canonicalized_domains=0;
    for(const auto& d:domains){
        require(fast::supported_fp_domain(d.cw,d.tag,d.mx)==d.supported,"explicit FP domain classification");
        // Force only the fixture's dispatch classification, never the native DLL.
        // This makes the SSE2 preflight witness run on both fixture bottles.
        fixture_adjacency_math_table(int(D3dxMathTable::Sse2));
        fallback_expected=seed;fallback_expected.x87.control=(fallback_expected.x87.control&0xffff0000)|d.cw;fallback_expected.x87.tag=(fallback_expected.x87.tag&0xffff0000)|d.tag;fallback_expected.mxcsr=d.mx;
        DWORD output[9];for(auto& v:output)v=0xabcdefu;
        const auto stats=fixture_adjacency_statistics();const unsigned calls=fallback_calls;
        write_state(fallback_expected);
        // FLDENV tags alone do not create a physical stack value on every
        // emulator. Construct a real x87 value for the nonempty-stack control.
        if(d.tag!=0xffff){asm volatile("fldz":::"st","memory");}
        fallback_expected=read_state();const FpState actual_before=fallback_expected;SetLastError(0x1357); // actual canonical hardware state
        const HRESULT hr=fixture_adjacency_fast(mesh,c.epsilon,output,refused_original);
        const DWORD error=GetLastError();const FpState after=read_state();write_state(saved);fixture_adjacency_math_table(-1);
        const auto final=fixture_adjacency_statistics();
        require(hr==E_FAIL&&error==0x2468&&output[0]==0x12345678,"native failure/output/LastError forwarded exactly");
        require(fallback_calls==calls+1&&fallback_untouched,"refused candidate never publishes output before native");
        if(fallback_error!=0x1357||fallback_observed.x87.control!=fallback_expected.x87.control||fallback_observed.x87.status!=fallback_expected.x87.status||fallback_observed.x87.tag!=fallback_expected.x87.tag||fallback_observed.mxcsr!=fallback_expected.mxcsr)
            printf("ADMISSION_STATE_DIAGNOSTIC expected=%08lx/%08lx/%08lx/%08lx observed=%08lx/%08lx/%08lx/%08lx error=%08lx\n",fallback_expected.x87.control,fallback_expected.x87.status,fallback_expected.x87.tag,fallback_expected.mxcsr,fallback_observed.x87.control,fallback_observed.x87.status,fallback_observed.x87.tag,fallback_observed.mxcsr,fallback_error);
        require(fallback_error==0x1357&&fallback_observed.x87.control==fallback_expected.x87.control&&fallback_observed.x87.status==fallback_expected.x87.status&&fallback_observed.x87.tag==fallback_expected.x87.tag&&fallback_observed.mxcsr==fallback_expected.mxcsr,"native receives incoming computational state and LastError");
        require(after.x87.control==fallback_observed.x87.control&&after.x87.status==fallback_observed.x87.status&&after.x87.tag==fallback_observed.x87.tag&&after.mxcsr==fallback_observed.mxcsr,"native outgoing computational state retained");
        // Rosetta can mask an attempted unmask of the invalid SSE exception.
        // Count this narrowly observed normalization, never call it coverage of
        // an unmasked runtime domain. All other requested controls must survive.
        const bool same_control=(actual_before.x87.control&0x0f3f)==(d.cw&0x0f3f);
        const bool same_occupancy=((actual_before.x87.tag&0xffff)==0xffff)==(d.tag==0xffff);
        const bool canonicalized=same_control&&same_occupancy&&d.mx==0x1f00&&actual_before.mxcsr==0x1f80;
        require(same_control&&same_occupancy&&(actual_before.mxcsr==d.mx||canonicalized),"requested FP controls represented or exact recorded mask canonicalization");
        canonicalized_domains+=canonicalized;
        const bool actual_supported=fast::supported_fp_domain(actual_before.x87.control,actual_before.x87.tag,actual_before.mxcsr);
        const unsigned reason=actual_supported?7:8;
        if(final.fallback_reasons[reason]!=stats.fallback_reasons[reason]+1||final.computed!=stats.computed){
            printf("ADMISSION_EARLY_STATE requested_mx=%08lx requested_tag=%08lx canonical=%08lx/%08lx/%08lx/%08lx\n",d.mx,d.tag,actual_before.x87.control,actual_before.x87.status,actual_before.x87.tag,actual_before.mxcsr);
            printf("ADMISSION_COUNTER_DIAGNOSTIC requested_cw=%04lx actual_cw=%08lx tag=%08lx mxcsr=%08lx supported=%u computed=%llu reasons=",d.cw,fallback_expected.x87.control,fallback_expected.x87.tag,fallback_expected.mxcsr,unsigned(d.supported),final.computed-stats.computed);
            for(unsigned i=0;i<final.fallback_reasons.size();++i){printf("%u:%llu,",i,final.fallback_reasons[i]-stats.fallback_reasons[i]);}putchar('\n');
        }
        require(final.fallback_reasons[reason]==stats.fallback_reasons[reason]+1&&final.computed==stats.computed,"distinct normal/FP refusal counters");
    }
    write_state(seed);
    fixture_adjacency_mode(1);const auto before_verify=fixture_adjacency_statistics();
    FpState rejected=seed;rejected.x87.control=(rejected.x87.control&0xffff0000)|0x007f;write_state(rejected);
    const Native diagnostic=native_generate(mesh,c.epsilon);write_state(saved);
    const auto after_verify=fixture_adjacency_statistics();
    require(SUCCEEDED(diagnostic.hr)&&after_verify.verify_meshes==before_verify.verify_meshes+1&&after_verify.verify_refused_fp==before_verify.verify_refused_fp+1&&after_verify.verify_admitted==before_verify.verify_admitted,"verify retains rejected-domain comparison with separate coverage");
    require(mesh->Release()==0,"admission control mesh released");
    printf("ADJACENCY_ADMISSION requested_domains=%u distinct_runtime_domains=%u canonicalized_domains=%u registry=8 checks=%u untouched=1 incoming_preserved=1 diagnostic_refused=1\n",unsigned(sizeof domains/sizeof *domains),unsigned(sizeof domains/sizeof *domains)-canonicalized_domains,canonicalized_domains,checks-before);
}

struct Dump {AdjacencyDumpHeader header{};std::vector<D3DVERTEXELEMENT9> declaration;std::vector<unsigned char> vertices,indices;std::vector<DWORD> native,module;};
static bool read_dump(const wchar_t* path,Dump& d){
    FILE* f=_wfopen(path,L"rb");if(!f)return false;
    auto get=[&](void* data,size_t bytes){return bytes==0||std::fread(data,1,bytes,f)==bytes;};
    bool good=get(&d.header,sizeof d.header)&&!std::memcmp(d.header.magic,"X3MADJ01",8)&&d.header.header_size==sizeof d.header&&d.header.faces&&d.header.vertices&&d.header.stride>=12;
    if(good){
        d.declaration.resize(d.header.declaration_count);d.vertices.resize(size_t(d.header.vertices)*d.header.stride);
        d.indices.resize(size_t(d.header.faces)*3*((d.header.options&D3DXMESH_32BIT)?4:2));d.native.resize(size_t(d.header.faces)*3);d.module.resize(size_t(d.header.faces)*3);
        good=get(d.declaration.data(),d.declaration.size()*sizeof(D3DVERTEXELEMENT9))&&get(d.vertices.data(),d.vertices.size())&&get(d.indices.data(),d.indices.size())&&get(d.native.data(),d.native.size()*sizeof(DWORD))&&get(d.module.data(),d.module.size()*sizeof(DWORD));
    }
    std::fclose(f);return good;
}
static void dump_face_indices(const Dump& d,DWORD face,unsigned* out){
    for(unsigned k=0;k<3;++k){if(d.header.options&D3DXMESH_32BIT){DWORD i;std::memcpy(&i,&d.indices[(size_t(face)*3+k)*4],4);out[k]=i;}else{WORD i;std::memcpy(&i,&d.indices[(size_t(face)*3+k)*2],2);out[k]=i;}}
}
static bool safe_replay=false;
static bool replay_dump(Create create,IDirect3DDevice9* device,const char* label,const Dump& d){
    std::vector<D3DVERTEXELEMENT9> decl=d.declaration;decl.push_back(D3DDECL_END());
    ID3DXMesh* mesh=nullptr;ok(create(d.header.faces,d.header.vertices,(d.header.options&D3DXMESH_32BIT)|D3DXMESH_SYSTEMMEM,decl.data(),device,&mesh),"replay CreateMesh");
    require(mesh->GetNumBytesPerVertex()==d.header.stride,"replay stride");
    void* data=nullptr;
    ok(mesh->LockVertexBuffer(0,&data),"replay vb lock");std::memcpy(data,d.vertices.data(),d.vertices.size());ok(mesh->UnlockVertexBuffer(),"replay vb unlock");
    ok(mesh->LockIndexBuffer(0,&data),"replay ib lock");std::memcpy(data,d.indices.data(),d.indices.size());ok(mesh->UnlockIndexBuffer(),"replay ib unlock");
    DWORD* attrs=nullptr;ok(mesh->LockAttributeBuffer(0,&attrs),"replay attrs lock");for(unsigned i=0;i<d.header.faces;++i)attrs[i]=0;ok(mesh->UnlockAttributeBuffer(),"replay attrs unlock");
    float epsilon;std::memcpy(&epsilon,&d.header.epsilon_bits,sizeof epsilon);
    Case c;c.name=label;c.bits32=(d.header.options&D3DXMESH_32BIT)!=0;c.stride=d.header.stride;c.position_offset=d.header.position_offset;c.epsilon=epsilon;
    const FpState saved_state=read_state();FpState captured=saved_state;captured.x87.control=(captured.x87.control&0xffff0000)|(d.header.x87_control&0xffff);captured.x87.tag|=0xffff;captured.x87.status&=0xffff0000;captured.mxcsr=d.header.mxcsr;
    if(safe_replay)write_state(captured);
    const Native n=native_generate(mesh,epsilon,safe_replay);ok(n.hr,"replay native GenerateAdjacency");
    if(safe_replay){FpState compute=captured;compute.mxcsr=0x1f80;write_state(compute);}
    const Fast fa=fast_generate(mesh,c);
    if(safe_replay)write_state(saved_state);
    size_t first_native=0,first=0,first_module=0;
    const size_t native_diff=mismatches(n.adjacency,d.native,first_native);
    const size_t diff=fa.report.status==fast::Status::Ok?mismatches(n.adjacency,fa.adjacency,first):n.adjacency.size();
    const size_t module_diff=fa.report.status==fast::Status::Ok?mismatches(fa.adjacency,d.module,first_module):fa.adjacency.size();
    printf("REPLAY_CASE file=%s faces=%u vertices=%u bits=%u stride=%u position_offset=%u epsilon=%g options=%08x x87_control=%04x mxcsr=%08x dump_mismatches=%u dump_first=%u native_equal_dump=%u native_dump_diff=%u status=%s module_equal_native=%u mismatches=%u first=%u module_equal_dump_module=%u native_us=%.3f fast_us=%.3f representatives=%lu welded=%lu refused_welds=%lu multi_candidates=%lu normal_selected=%lu degenerate_faces=%lu welded_degenerate_faces=%lu repeated_neighbours=%lu rsqrt=%s normalize=%s\n",
        label,unsigned(d.header.faces),unsigned(d.header.vertices),c.bits32?32u:16u,unsigned(d.header.stride),unsigned(d.header.position_offset),double(epsilon),unsigned(d.header.options),unsigned(d.header.x87_control),unsigned(d.header.mxcsr),unsigned(d.header.mismatches),unsigned(d.header.first),unsigned(native_diff==0),unsigned(native_diff),fast::status_name(unsigned(fa.report.status)),unsigned(diff==0),unsigned(diff),unsigned(first),unsigned(module_diff==0),us(n.ticks),us(fa.ticks),
        DWORD(fa.report.representatives),DWORD(fa.report.welded),DWORD(fa.report.refused_welds),DWORD(fa.report.multi_candidates),DWORD(fa.report.normal_selected),DWORD(fa.report.degenerate_faces),DWORD(fa.report.welded_degenerate_faces),DWORD(fa.report.repeated_neighbours),fast::rsqrt_implementation(),fast::normalize_name(default_policy().normalize));
    if(diff){
        unsigned printed=0;
        for(size_t i=0;i<n.adjacency.size()&&printed<16;++i){
            if(n.adjacency[i]==fa.adjacency[i])continue;
            ++printed;
            const DWORD face=DWORD(i/3);unsigned own[3],other[3]={0,0,0};dump_face_indices(d,face,own);
            const DWORD neighbour=n.adjacency[i]!=0xffffffffu?n.adjacency[i]:fa.adjacency[i];if(neighbour!=0xffffffffu)dump_face_indices(d,neighbour,other);
            printf("REPLAY_MISMATCH file=%s index=%u face=%lu slot=%u native=%ld fast=%ld face_indices=%u,%u,%u other=%ld other_indices=%u,%u,%u\n",label,unsigned(i),(unsigned long)face,unsigned(i%3),n.adjacency[i]==0xffffffffu?-1L:long(n.adjacency[i]),fa.adjacency[i]==0xffffffffu?-1L:long(fa.adjacency[i]),own[0],own[1],own[2],neighbour==0xffffffffu?-1L:long(neighbour),other[0],other[1],other[2]);
        }
        struct Variant {const char* name;fast::Policy policy;};
        Variant variants[8]={{"tail_insertion",default_policy()},{"no_normal_selection",default_policy()},{"no_weld_refusal",default_policy()},{"index_order_sweep",default_policy()},{"retire_own_entry",default_policy()},{"keep_refused_entry",default_policy()},{"later_slot_check",default_policy()},{"other_normalize",other_normalize()}};
        variants[0].policy.head_insertion=false;variants[1].policy.normal_selection=false;variants[2].policy.weld_refusal=false;variants[3].policy.heap_order=false;variants[4].policy.retire_own_entry=true;variants[5].policy.unlink_refused=false;variants[6].policy.later_slot_check=true;
        for(const auto& variant:variants){const Fast alt=fast_generate(mesh,c,variant.policy);size_t alt_first=0;const size_t alt_diff=alt.report.status==fast::Status::Ok?mismatches(n.adjacency,alt.adjacency,alt_first):alt.adjacency.size();
            printf("REPLAY_POLICY file=%s variant=%s equal=%u mismatches=%u\n",label,variant.name,unsigned(alt_diff==0),unsigned(alt_diff));}
    }
    bool accepted=diff==0;
    if(safe_replay){
        const auto start=fixture_adjacency_statistics();fixture_adjacency_mode(1);write_state(captured);
        const Native verified=native_generate(mesh,epsilon);write_state(saved_state);
        const auto verify=fixture_adjacency_statistics();fixture_adjacency_mode(2);write_state(captured);
        const Native served=native_generate(mesh,epsilon);write_state(saved_state);
        const auto end=fixture_adjacency_statistics();
        accepted=served.hr==n.hr&&served.adjacency==n.adjacency&&verified.hr==n.hr&&verified.adjacency==n.adjacency&&end.verify_admitted_mismatched==start.verify_admitted_mismatched;
        printf("REPLAY_ADMISSION file=%s equal_native=%u computed=%llu fallback_normals=%llu fallback_fp=%llu verify_admitted=%llu verify_refused_normals=%llu verify_refused_fp=%llu diagnostic_mismatched=%llu faults=%llu native_us=%.3f served_us=%.3f\n",label,unsigned(accepted),end.computed-verify.computed,end.fallback_reasons[7]-verify.fallback_reasons[7],end.fallback_reasons[8]-verify.fallback_reasons[8],verify.verify_admitted-start.verify_admitted,verify.verify_refused_competing-start.verify_refused_competing,verify.verify_refused_fp-start.verify_refused_fp,verify.verify_mismatched-start.verify_mismatched,end.faults-start.faults,us(n.ticks),us(served.ticks));
        require(accepted&&end.faults==start.faults,"replayed admitted/fallback output equals native without faults");
    }
    require(mesh->Release()==0,"replay mesh released");
    return accepted;
}
static int replay_main(Create create,IDirect3DDevice9* device,int argc,char** argv){
    unsigned dumps=0,equal=0,unreadable=0;
    printf("MESH ADJACENCY MATH_TABLE table=%s normalize=%s rsqrt=%s\n",d3dx_math_table_name(d3dx_math_table()),fast::normalize_name(default_policy().normalize),fast::rsqrt_implementation());
    for(int i=2;i<argc;++i){
        wchar_t path[32768]{};MultiByteToWideChar(CP_ACP,0,argv[i],-1,path,32767);
        Dump d;if(!read_dump(path,d)){printf("REPLAY_UNREADABLE file=%s\n",argv[i]);++unreadable;continue;}
        ++dumps;const char* label=argv[i];for(const char* p=argv[i];*p;++p)if(*p=='\\'||*p=='/')label=p+1;
        equal+=replay_dump(create,device,label,d);
    }
    printf("MESH ADJACENCY REPLAY dumps=%u equal=%u mismatched=%u unreadable=%u checks=%u failures=0\n",dumps,equal,dumps-equal,unreadable,checks);
    return dumps==equal&&!unreadable?0:1;
}
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);int exit=1;HWND window=nullptr;
    try{
        LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency=double(f.QuadPart);
        HMODULE native=GetModuleHandleW(L"d3dx9_37.dll");require(native!=nullptr,"native D3DX import");auto create=symbol<Create>(native,"D3DXCreateMesh");
        HMODULE d3d=LoadLibraryW(L"d3d9.dll");require(d3d!=nullptr,"builtin D3D9");auto create9=symbol<IDirect3D9*(WINAPI*)(UINT)>(d3d,"Direct3DCreate9");Com<IDirect3D9> api;api.p=create9(D3D_SDK_VERSION);require(api.p!=nullptr,"Create9");
        WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName="X3MeshAdjacencyFastFixture";RegisterClassA(&cls);window=CreateWindowA(cls.lpszClassName,"Original mesh adjacency fixture",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);require(window!=nullptr,"window");
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;IDirect3DDevice9* device=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device),"device");
        if(argc>1&&!std::strcmp(argv[1],"admission-only")){
            SetEnvironmentVariableW(L"X3M_TELEMETRY",L"1");SetEnvironmentVariableW(L"X3M_MESH_ADJACENCY",L"verify");
            require(fixture_initialize(GetModuleHandleW(nullptr)),"control hooks installed");admission_controls(device);shutdown();require(device->Release()==0,"control device released");if(window)DestroyWindow(window);return 0;
        }
        if(argc>1&&!std::strcmp(argv[1],"replay-safe")){
            safe_replay=true;
            const D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
            for(DWORD options:{DWORD(D3DXMESH_SYSTEMMEM),DWORD(D3DXMESH_SYSTEMMEM|D3DXMESH_32BIT)}){ID3DXMesh* probe=nullptr;ok(create(1,3,options,decl,device,&probe),"replay raw class");remember_raw(probe);require(probe->Release()==0,"replay raw probe released");}
            SetEnvironmentVariableW(L"X3M_TELEMETRY",L"1");SetEnvironmentVariableW(L"X3M_MESH_ADJACENCY",L"verify");
            require(fixture_initialize(GetModuleHandleW(nullptr)),"replay hooks installed");
            exit=replay_main(&D3DXCreateMesh,device,argc,argv);fixture_adjacency_report();shutdown();require(device->Release()==0,"safe replay device released");if(window)DestroyWindow(window);return exit;
        }
        if(argc>1&&!std::strcmp(argv[1],"replay")){exit=replay_main(create,device,argc,argv);require(device->Release()==0,"replay device released");if(window)DestroyWindow(window);return exit;}
        printf("MESH ADJACENCY MATH_TABLE table=%s normalize=%s rsqrt=%s\n",d3dx_math_table_name(d3dx_math_table()),fast::normalize_name(default_policy().normalize),fast::rsqrt_implementation());
        // Establish the supported ordinary domain explicitly; CreateDevice/CRT
        // defaults on another platform are not evidence for fast admission.
        FpState ordinary=read_state();ordinary.x87.control=(ordinary.x87.control&0xffff0000)|0x027f;ordinary.x87.tag|=0xffff;ordinary.x87.status&=0xffff0000;ordinary.mxcsr=0x1f80;write_state(ordinary);
        const auto all=cases();std::vector<Native> baselines;unsigned expected_fast_refused=0;
        // Part A: uninstrumented native D3DX against the pure module.
        for(const auto& c:all){
            ID3DXMesh* mesh=nullptr;create_mesh(create,device,c,&mesh);
            remember_raw(mesh);
            const Native n=native_generate(mesh,c.epsilon);
            if(!std::strcmp(c.expected_status,"ok"))ok(n.hr,"native GenerateAdjacency");
            const Fast fa=fast_generate(mesh,c);
            if(fa.report.status==fast::Status::Ok&&d3dx_math_table()==D3dxMathTable::Sse2&&fa.report.potential_competing_normals)++expected_fast_refused;
            const char* status=fast::status_name(unsigned(fa.report.status));
            require(!std::strcmp(status,c.expected_status),"module status as expected");
            size_t first=0;const size_t diff=fa.report.status==fast::Status::Ok?mismatches(n.adjacency,fa.adjacency,first):0;
            const bool equal=fa.report.status==fast::Status::Ok&&diff==0;
            printf("ADJACENCY_CASE name=%s faces=%u vertices=%u bits=%u epsilon=%g status=%s native_hr=%08lx native_error=%08lx equal=%u mismatches=%u first=%u native_us=%.3f fast_us=%.3f speedup=%.2f quantized=%u representatives=%lu welded=%lu multi_candidates=%lu normal_selected=%lu degenerate_faces=%lu welded_degenerate_faces=%lu refused_welds=%lu repeated_neighbours=%lu unmatched=%lu normalize=%s\n",
                c.name.c_str(),c.face_count(),c.vertex_count(),c.bits32?32u:16u,double(c.epsilon),status,n.hr,n.error,unsigned(equal),unsigned(diff),unsigned(first),us(n.ticks),us(fa.ticks),fa.ticks?double(n.ticks)/double(fa.ticks):0.0,
                unsigned(fa.report.quantized),DWORD(fa.report.representatives),DWORD(fa.report.welded),DWORD(fa.report.multi_candidates),DWORD(fa.report.normal_selected),DWORD(fa.report.degenerate_faces),DWORD(fa.report.welded_degenerate_faces),DWORD(fa.report.refused_welds),DWORD(fa.report.repeated_neighbours),DWORD(fa.report.unmatched),fast::normalize_name(default_policy().normalize));
            if(c.tie_evidence||!equal){
                print_adjacency("NATIVE_ADJACENCY",n.adjacency);
                struct Variant {const char* name;fast::Policy policy;};
                Variant variants[9]={{"default",default_policy()},{"tail_insertion",default_policy()},{"no_normal_selection",default_policy()},{"no_weld_refusal",default_policy()},{"index_order_sweep",default_policy()},{"retire_own_entry",default_policy()},{"keep_refused_entry",default_policy()},{"later_slot_check",default_policy()},{"other_normalize",other_normalize()}};
                variants[1].policy.head_insertion=false;variants[2].policy.normal_selection=false;variants[3].policy.weld_refusal=false;variants[4].policy.heap_order=false;variants[5].policy.retire_own_entry=true;variants[6].policy.unlink_refused=false;variants[7].policy.later_slot_check=true;
                for(const auto& variant:variants){
                    const Fast alt=fast_generate(mesh,c,variant.policy);size_t alt_first=0;const size_t alt_diff=alt.report.status==fast::Status::Ok?mismatches(n.adjacency,alt.adjacency,alt_first):alt.adjacency.size();
                    printf("ADJACENCY_POLICY name=%s variant=%s equal=%u mismatches=%u\n",c.name.c_str(),variant.name,unsigned(alt_diff==0),unsigned(alt_diff));
                    if(alt_diff)print_adjacency("POLICY_ADJACENCY",alt.adjacency);
                }
            }
            if(fa.report.status==fast::Status::Ok)require(equal,"module output equals native D3DX");
            if(c.name=="fan-tilted-abc"){
                // Dump writer/reader round trip on this mesh (the game's verify mode writes
                // the same format); the replay path must then reproduce native D3DX.
                require(adjacency_write_dump(L"mesh-adjacency-selftest.bin",mesh,c.epsilon,n.adjacency.data(),fa.adjacency.data(),0,0,0x027f,0x9fc0),"dump written");
                Dump d;require(read_dump(L"mesh-adjacency-selftest.bin",d),"dump readable");
                require(d.header.faces==c.face_count()&&d.header.vertices==c.vertex_count()&&d.header.stride==c.stride&&d.header.position_offset==c.position_offset&&d.header.declaration_count==2&&d.header.x87_control==0x027f&&d.header.mxcsr==0x9fc0,"dump header");
                require(d.vertices.size()==c.vertices.size()*sizeof(float)&&!std::memcmp(d.vertices.data(),c.vertices.data(),d.vertices.size()),"dump vertex bytes");
                require(d.native==n.adjacency&&d.module==fa.adjacency,"dump adjacency arrays");
                require(replay_dump(create,device,"mesh-adjacency-selftest.bin",d),"replay of the self-test dump equals native");
            }
            baselines.push_back(n);require(mesh->Release()==0,"case mesh released");
        }
        // Random differential sweep: small meshes over a 3x3x2 grid with frequent
        // duplicates, degenerate and reversed faces, 16- and 32-bit indices,
        // epsilon 1e-6 or 0, and TEXCOORD-first layouts with random keys.
        {uint64_t seed=0x9e3779b97f4a7c15ull;auto rnd=[&](){seed=seed*6364136223846793005ull+1442695040888963407ull;return uint32_t(seed>>33);};
            const unsigned trials=2000;unsigned equal_count=0,printed=0,multi=0,refused=0;
            for(unsigned t=0;t<trials;++t){
                Case c;c.name="random-"+std::to_string(t);c.bits32=(t%3)==1;c.epsilon=(t%11)==10?0.f:1e-6f;if((t%5)==4)c.position_offset=8;
                const unsigned V=3+rnd()%7,F=1+rnd()%8;
                for(unsigned v=0;v<V;++v){push_vertex(c,grid(int(rnd()%3)*16-16),grid(int(rnd()%3)*16-16),grid(int(rnd()%2)*16));if(c.position_offset)set_key(c,v,float(rnd()%3)*0.25f);}
                for(unsigned f=0;f<F;++f)push_face(c,rnd()%V,rnd()%V,rnd()%V);
                ID3DXMesh* mesh=nullptr;create_mesh(create,device,c,&mesh);
                const Native n=native_generate(mesh,c.epsilon);ok(n.hr,"random native GenerateAdjacency");
                const Fast fa=fast_generate(mesh,c);require(fa.report.status==fast::Status::Ok,"random module status ok");
                size_t first=0;const size_t diff=mismatches(n.adjacency,fa.adjacency,first);
                multi+=fa.report.multi_candidates!=0;refused+=fa.report.refused_welds!=0;
                if(!diff)++equal_count;
                else if(printed<3){++printed;
                    printf("ADJACENCY_RANDOM_MISMATCH trial=%u faces=%u vertices=%u bits=%u epsilon=%g position_offset=%u mismatches=%u first=%u\n",t,c.face_count(),c.vertex_count(),c.bits32?32u:16u,double(c.epsilon),c.position_offset,unsigned(diff),unsigned(first));
                    printf("RANDOM_VERTICES");for(unsigned v=0;v<V;++v){const float* q=&c.vertices[size_t(v)*(c.stride/4)];printf(" %g,%g,%g,%g",q[0],q[c.position_offset/4],q[c.position_offset/4+1],q[c.position_offset/4+2]);}printf("\n");
                    printf("RANDOM_FACES");for(unsigned f=0;f<F;++f)printf(" %u,%u,%u",c.indices[f*3],c.indices[f*3+1],c.indices[f*3+2]);printf("\n");
                    print_adjacency("NATIVE_ADJACENCY",n.adjacency);print_adjacency("MODULE_ADJACENCY",fa.adjacency);}
                require(mesh->Release()==0,"random mesh released");
            }
            printf("ADJACENCY_RANDOM trials=%u equal=%u mismatched=%u multi_candidate_meshes=%u refused_weld_meshes=%u\n",trials,equal_count,trials-equal_count,multi,refused);
            require(equal_count==trials,"random differential sweep equals native D3DX");}
        // Native D3DX under computational states: the game's 0x027f/0x9fc0
        // (53-bit x87, FTZ+DAZ) and its components, twice each for determinism,
        // compared with the module (state-independent) and the default-state result.
        {struct Env {DWORD control,status,tag,ip,cs,dp,ds;};struct FP {Env x87;DWORD mxcsr;};
            auto read_fp=[](){FP v{};asm volatile("fnstenv %0\n\tfldenv %0\n\tstmxcsr %1":"=m"(v.x87),"=m"(v.mxcsr)::"memory");return v;};
            auto write_fp=[](const FP& v){asm volatile("fldenv %0\n\tldmxcsr %1"::"m"(v.x87),"m"(v.mxcsr):"memory");};
            const FP saved=read_fp();
            const struct {DWORD cw,mxcsr;} states[]={{0x027f,0x1f80},{0x007f,0x1f80},{0x027f,0x9fc0},{0x007f,0x9fc0},{0x027f,0x9f80},{0x027f,0x1fc0},{0x037f,0x1f80}};
            for(const char* name:{"quad-16","fan-tilted-abc","split-16","grid-224x224-32","split-150x150-32","star-20000-32"}){
                size_t i=0;while(all[i].name!=name)++i;const Case& c=all[i];
                ID3DXMesh* mesh=nullptr;create_mesh(create,device,c,&mesh);const Fast module=fast_generate(mesh,c);
                for(const auto& s:states){
                    std::vector<DWORD> first,second;DWORD cw_after=0,mxcsr_after=0;
                    for(unsigned pass=0;pass<2;++pass){FP state=saved;state.x87.control=(state.x87.control&0xffff0000)|s.cw;state.x87.status&=0xffff0000;state.mxcsr=s.mxcsr;write_fp(state);
                        const Native n=native_generate(mesh,c.epsilon,true);const FP after=read_fp();write_fp(saved);ok(n.hr,"native under state");
                        (pass?second:first)=n.adjacency;cw_after=after.x87.control&0xffff;mxcsr_after=after.mxcsr;}
                    size_t f1=0,f2=0;const size_t diff_module=mismatches(module.adjacency,first,f1),diff_pass=mismatches(first,second,f2);
                    size_t unused_count=0;for(auto v:first)unused_count+=v==0xffffffffu;
                    printf("FP_STATE_NATIVE name=%s cw=%04lx mxcsr=%08lx after_cw=%04lx after_mxcsr=%08lx faces=%u equal_module=%u mismatches=%u first=%u unused=%u deterministic=%u\n",name,s.cw,s.mxcsr,cw_after,mxcsr_after,c.face_count(),unsigned(diff_module==0),unsigned(diff_module),unsigned(f1),unsigned(unused_count),unsigned(diff_pass==0));
                }
                require(mesh->Release()==0,"state sweep mesh released");
            }
            write_fp(saved);}
        // Part B: the production hook path, verify then fast.
        SetEnvironmentVariableW(L"X3M_TELEMETRY",L"1");SetEnvironmentVariableW(L"X3M_MESH_ADJACENCY",L"verify");
        require(fixture_initialize(GetModuleHandleW(nullptr)),"install hooks");
        auto stats_before=fixture_adjacency_statistics();require(stats_before.calls==0,"no adjacency calls before hooked meshes");
        std::vector<ID3DXMesh*> hooked;
        for(size_t i=0;i<all.size();++i){ID3DXMesh* mesh=nullptr;create_mesh(&D3DXCreateMesh,device,all[i],&mesh);hooked.push_back(mesh);}
        // Verify mode returns the native result on every case and compares ours.
        for(size_t i=0;i<all.size();++i){
            const Native n=native_generate(hooked[i],all[i].epsilon);
            require(n.hr==baselines[i].hr&&n.adjacency==baselines[i].adjacency,"verify mode returns the native result");
            require(n.error==baselines[i].error,"verify mode keeps the native LastError");
        }
        auto stats=fixture_adjacency_statistics();
        const unsigned expected_ok=unsigned(std::count_if(all.begin(),all.end(),[](const Case& c){return !std::strcmp(c.expected_status,"ok");}));
        printf("VERIFY_STATS calls=%llu computed=%llu fallbacks=%llu verify_meshes=%llu verify_equal=%llu verify_mismatched=%llu mismatch_entries=%llu quantized=%llu unquantized=%llu module_non_finite=%llu module_epsilon_neighbour=%llu\n",
            stats.calls,stats.computed,stats.fallbacks,stats.verify_meshes,stats.verify_equal,stats.verify_mismatched,stats.verify_mismatch_entries,stats.quantized,stats.unquantized,stats.module_status[3],stats.module_status[5]);
        require(stats.calls==all.size()&&stats.verify_meshes==expected_ok&&stats.verify_equal==expected_ok&&stats.verify_mismatched==0,"verify: every computable case equal, none mismatched");
        require(stats.fallbacks==all.size()-expected_ok&&stats.module_status[3]==1&&stats.module_status[5]==1,"verify: NaN and near-neighbour cases fell back with their module status");
        const unsigned expected_fast_ok=expected_ok-expected_fast_refused;
        // Fast mode: fresh content is computed by the module; the caller's LastError is preserved.
        fixture_adjacency_mode(2);
        for(size_t i=0;i<all.size();++i){
            const Native n=native_generate(hooked[i],all[i].epsilon);
            require(n.hr==baselines[i].hr&&n.adjacency==baselines[i].adjacency,"fast mode output equals the native baseline");
            require(n.error==0x1357||n.error==baselines[i].error,"fast mode LastError is the caller's or the native fallback's");
        }
        stats=fixture_adjacency_statistics();
        printf("FAST_STATS calls=%llu computed=%llu fallbacks=%llu faults=%llu fast_ticks=%llu\n",stats.calls,stats.computed,stats.fallbacks,stats.faults,stats.fast_ticks);
        require(stats.calls==2*all.size()&&stats.computed==expected_ok+expected_fast_ok&&stats.fallbacks==2*(all.size()-expected_ok)+expected_fast_refused,"fast: every computable case computed (verify computed them once already), the rest fell back to native");
        // Fresh content in fast mode: computed by the module.
        {Case c=quad("fresh-fast");c.vertices[1]=grid(3);ID3DXMesh* mesh=nullptr;create_mesh(&D3DXCreateMesh,device,c,&mesh);
            struct Env {DWORD control,status,tag,ip,cs,dp,ds;};struct FP {Env x87;DWORD mxcsr;};
            auto read_fp=[](){FP v{};asm volatile("fnstenv %0\n\tfldenv %0\n\tstmxcsr %1":"=m"(v.x87),"=m"(v.mxcsr)::"memory");return v;};
            auto write_fp=[](const FP& v){asm volatile("fldenv %0\n\tldmxcsr %1"::"m"(v.x87),"m"(v.mxcsr):"memory");};
            FP seed=read_fp();seed.x87.status&=0xffff0000;seed.mxcsr&=~DWORD(0x3f);
            const auto before=fixture_adjacency_statistics();
            write_fp(seed);const Native first=native_generate(mesh,c.epsilon);ok(first.hr,"fresh fast call");
            const auto after=fixture_adjacency_statistics();
            require(after.computed==before.computed+1&&after.fallbacks==before.fallbacks,"fresh content computed by the module");
            require(first.error==0x1357,"fast path preserves the caller's LastError");
            const Fast pure=fast_generate(mesh,c);require(pure.report.status==fast::Status::Ok&&pure.adjacency==first.adjacency,"hooked fast output equals the pure module");
            require(native_generate(mesh,c.epsilon,true).adjacency==first.adjacency,"hooked fast output equals the unhooked native method");
            require(mesh->Release()==0,"fresh mesh released");}
        // Fallbacks through the hook: a MANAGED mesh fails the public SYSTEMMEM gate.
        {Case c=quad("managed-fallback");c.options=D3DXMESH_MANAGED;ID3DXMesh* mesh=nullptr;create_mesh(&D3DXCreateMesh,device,c,&mesh);
            const auto before=fixture_adjacency_statistics();const Native n=native_generate(mesh,c.epsilon);ok(n.hr,"managed native fallback");
            const auto after=fixture_adjacency_statistics();require(after.fallbacks==before.fallbacks+1&&after.fallback_reasons[1]==before.fallback_reasons[1]+1,"managed mesh fell back at the gate");
            require(n.adjacency==baselines[0].adjacency,"managed fallback returned the native result");require(mesh->Release()==0,"managed mesh released");}
        // The game's actual computational state (x87 control 0x027f = 53-bit
        // precision, MXCSR 0x9fc0 = FTZ+DAZ) through verify and fast: identical
        // output, state restored.
        {struct Env {DWORD control,status,tag,ip,cs,dp,ds;};struct FP {Env x87;DWORD mxcsr;};
            auto read_fp=[](){FP v{};asm volatile("fnstenv %0\n\tfldenv %0\n\tstmxcsr %1":"=m"(v.x87),"=m"(v.mxcsr)::"memory");return v;};
            auto write_fp=[](const FP& v){asm volatile("fldenv %0\n\tldmxcsr %1"::"m"(v.x87),"m"(v.mxcsr):"memory");};
            const FP saved=read_fp();FP game=saved;game.x87.control=(game.x87.control&0xffff0000)|0x027f;game.x87.status&=0xffff0000;game.mxcsr=0x9fc0;
            const char* names[]={"quad-16","exact-duplicates","fan-tilted-abc","degenerate-welded","grid-224x224-32","split-150x150-32"};
            unsigned verified=0,fast_equal=0,restored=0;
            for(unsigned mode=1;mode<=2;++mode){fixture_adjacency_mode(mode);
                for(const char* name:names){size_t i=0;while(all[i].name!=name)++i;
                    Case c=all[i];c.vertices[2]=grid(mode==1?5:6); // fresh content per mode
                    ID3DXMesh* reference=nullptr;create_mesh(create,device,c,&reference);ID3DXMesh* mesh=nullptr;create_mesh(&D3DXCreateMesh,device,c,&mesh);
                    write_fp(game);const Native expected=native_generate(reference,c.epsilon,true);const FP native_after=read_fp();
                    const auto computed_before=fixture_adjacency_statistics().computed;
                    write_fp(game);const Native n=native_generate(mesh,c.epsilon);const FP after=read_fp();
                    const bool was_computed=fixture_adjacency_statistics().computed!=computed_before;
                    if(n.hr!=expected.hr||n.adjacency!=expected.adjacency){printf("GAME_FP_MISMATCH mode=%u name=%s hr=%08lx expected_hr=%08lx\n",mode,name,n.hr,expected.hr);print_adjacency("GAME_FP_EXPECTED",expected.adjacency);print_adjacency("GAME_FP_ACTUAL",n.adjacency);}
                    require(n.hr==expected.hr&&n.adjacency==expected.adjacency,"game FP state: hooked output equals native");
                    // verify returns D3DX's outgoing state; fast is state-transparent (the caller's own state).
                    const FP& want=mode==1||!was_computed?native_after:game;
                    const bool same=(after.x87.control&0xffff)==(want.x87.control&0xffff)&&after.mxcsr==want.mxcsr&&(after.x87.tag&0xffff)==(want.x87.tag&0xffff);
                    printf("GAME_FP_CALL mode=%s name=%s incoming_cw=027f incoming_mxcsr=9fc0 native_cw=%04lx native_sw=%04lx native_mxcsr=%08lx after_cw=%04lx after_sw=%04lx after_tag=%04lx after_mxcsr=%08lx restored=%u\n",mode==1?"verify":"fast",name,native_after.x87.control&0xffff,native_after.x87.status&0xffff,native_after.mxcsr,after.x87.control&0xffff,after.x87.status&0xffff,after.x87.tag&0xffff,after.mxcsr,unsigned(same));
                    require(same,"game FP state restored after the hooked call");
                    verified+=mode==1;fast_equal+=mode==2;restored+=same;
                    require(reference->Release()==0&&mesh->Release()==0,"game FP meshes released");
                }
            }
            write_fp(saved);
            const auto s=fixture_adjacency_statistics();
            require(s.computed>=6&&s.verify_meshes>=expected_ok+6,"game FP state: module ran in verify and fast");
            printf("GAME_FP_STATE control=027f mxcsr=9fc0 meshes=%u verify_equal=%u fast_equal=%u restored=%u\n",unsigned(2*6),verified,fast_equal,restored);
        }
        admission_controls(device);
        // Native mode leaves the module idle.
        fixture_adjacency_mode(0);
        {const auto before=fixture_adjacency_statistics();const Native n=native_generate(hooked[0],all[0].epsilon);require(n.adjacency==baselines[0].adjacency&&fixture_adjacency_statistics().calls==before.calls,"native mode does not invoke the service");}
        fixture_adjacency_report();report();
        for(auto* mesh:hooked)require(mesh->Release()==0,"hooked mesh released");
        SetLastError(0x2468);shutdown();require(!active(),"quiescent shutdown");
        require(device->Release()==0,"no retained device references");if(!loading_admission_witness())throw std::runtime_error("admission witness");
        printf("MESH ADJACENCY RESULT checks=%u failures=0\n",checks);exit=0;
    }catch(const std::exception& e){printf("MESH ADJACENCY FAIL %s checks=%u\n",e.what(),checks);}if(window)DestroyWindow(window);return exit;
}
