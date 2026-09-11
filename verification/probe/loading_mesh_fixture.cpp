// Original tiny native meshes exercise production mesh telemetry, never X3.
#include "../../src/proxy/loading_trace.h"
#include <d3dx9.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "loading_admission_witness.h"
namespace x3m {void log(const char* fmt,...){va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);putchar('\n');}}
using namespace x3m::loading_trace;
static unsigned checks=0;
void require(bool good,const char* label){++checks;if(!good){printf("FAIL %s error=%lu\n",label,GetLastError());throw std::runtime_error(label);}}
void ok(HRESULT hr,const char* label){require(SUCCEEDED(hr),label);}
template<class T> struct Com {T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}Com()=default;Com(const Com&)=delete;};
template<class T>T symbol(HMODULE m,const char* name){auto address=GetProcAddress(m,name);T fn=nullptr;static_assert(sizeof fn==sizeof address);std::memcpy(&fn,&address,sizeof fn);require(fn!=nullptr,name);return fn;}
using Create=decltype(&D3DXCreateMesh);using Clean=decltype(&D3DXCleanMesh);
using Generate=HRESULT(WINAPI*)(ID3DXMesh*,FLOAT,DWORD*);
static Generate previous_generate=nullptr;
static HRESULT WINAPI foreign_generate(ID3DXMesh* mesh,FLOAT epsilon,DWORD* adjacency){return previous_generate(mesh,epsilon,adjacency);}
void replace(PVOID* slot,PVOID value){DWORD old=0,ignored=0;require(VirtualProtect(slot,sizeof(PVOID),PAGE_READWRITE,&old)!=0,"slot writable");InterlockedExchangePointer(slot,value);require(VirtualProtect(slot,sizeof(PVOID),old,&ignored)!=0,"slot protection restored");}
using Bytes=std::vector<unsigned char>;
void append(Bytes& out,const void* data,size_t size){const auto p=static_cast<const unsigned char*>(data);out.insert(out.end(),p,p+size);}
Bytes mesh_bytes(ID3DXMesh* mesh){Bytes out;void* data=nullptr;DWORD* attr=nullptr;ok(mesh->LockVertexBuffer(D3DLOCK_READONLY,&data),"lock vertices");append(out,data,mesh->GetNumVertices()*mesh->GetNumBytesPerVertex());ok(mesh->UnlockVertexBuffer(),"unlock vertices");ok(mesh->LockIndexBuffer(D3DLOCK_READONLY,&data),"lock indices");append(out,data,mesh->GetNumFaces()*3*sizeof(WORD));ok(mesh->UnlockIndexBuffer(),"unlock indices");ok(mesh->LockAttributeBuffer(D3DLOCK_READONLY,&attr),"lock attrs");append(out,attr,mesh->GetNumFaces()*sizeof(DWORD));ok(mesh->UnlockAttributeBuffer(),"unlock attrs");return out;}
void create_mesh(IDirect3DDevice9* device,Create fn,ID3DXMesh** out){
    const D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
    ok(fn(4,4,D3DXMESH_SYSTEMMEM,decl,device,out),"CreateMesh");
    const float vertices[]={0,0,0,1,0,0,0,1,0,0,0,1};const WORD indices[]={0,2,1,0,1,3,1,2,3,2,0,3};void* data=nullptr;
    ok((*out)->LockVertexBuffer(0,&data),"fill vertices lock");std::memcpy(data,vertices,sizeof vertices);ok((*out)->UnlockVertexBuffer(),"fill vertices unlock");
    ok((*out)->LockIndexBuffer(0,&data),"fill indices lock");std::memcpy(data,indices,sizeof indices);ok((*out)->UnlockIndexBuffer(),"fill indices unlock");DWORD* attrs=nullptr;ok((*out)->LockAttributeBuffer(0,&attrs),"fill attrs lock");for(unsigned i=0;i<4;++i)attrs[i]=i%2;ok((*out)->UnlockAttributeBuffer(),"fill attrs unlock");
}
struct Result {std::array<HRESULT,6> hr{};std::array<DWORD,6> error{};std::array<DWORD,12> adjacency{},reps_adjacency{},cleaned{},optimized{};std::array<DWORD,4> faces{};Bytes before,after,remap;bool error_buffer=false,clean_alias=false;bool operator==(const Result& r)const{return hr==r.hr&&error==r.error&&adjacency==r.adjacency&&reps_adjacency==r.reps_adjacency&&cleaned==r.cleaned&&optimized==r.optimized&&faces==r.faces&&before==r.before&&after==r.after&&remap==r.remap&&error_buffer==r.error_buffer&&clean_alias==r.clean_alias;}};
Result sequence(IDirect3DDevice9* device,Create create,Clean clean){
    ID3DXMesh* mesh=nullptr;create_mesh(device,create,&mesh);Result r;
    void* identity=nullptr;ok(mesh->QueryInterface(IID_IUnknown,&identity),"QI identity");require(identity==mesh,"IUnknown identity unchanged");require(static_cast<IUnknown*>(identity)->Release()==1,"QI release native count");require(mesh->AddRef()==2&&mesh->Release()==1,"AddRef Release native counts");
    IDirect3DDevice9* returned=nullptr;ok(mesh->GetDevice(&returned),"GetDevice");require(returned==device,"native device identity");returned->Release();
    SetLastError(0x1357);r.hr[0]=mesh->GenerateAdjacency(1e-6f,r.adjacency.data());r.error[0]=GetLastError();ok(r.hr[0],"GenerateAdjacency");
    const DWORD points[]={0,1,2,3};SetLastError(0x1357);r.hr[1]=mesh->ConvertPointRepsToAdjacency(points,r.reps_adjacency.data());r.error[1]=GetLastError();ok(r.hr[1],"ConvertPointRepsToAdjacency");
    ID3DXMesh* cleaned=nullptr;Com<ID3DXBuffer> errors;SetLastError(0x1357);r.hr[2]=clean(D3DXCLEANTYPE(3),mesh,r.adjacency.data(),&cleaned,r.cleaned.data(),&errors.p);r.error[2]=GetLastError();r.error_buffer=errors.p!=nullptr;r.clean_alias=cleaned==mesh;ok(r.hr[2],"CleanMesh");
    r.before=mesh_bytes(cleaned);Com<ID3DXBuffer> remap;SetLastError(0x1357);r.hr[3]=cleaned->OptimizeInplace(D3DXMESHOPT_VERTEXCACHE,r.cleaned.data(),r.optimized.data(),r.faces.data(),&remap.p);r.error[3]=GetLastError();ok(r.hr[3],"OptimizeInplace");r.after=mesh_bytes(cleaned);if(remap.p)append(r.remap,remap->GetBufferPointer(),remap->GetBufferSize());
    // Valid buffers with invalid documented flags give a safe failure contract.
    SetLastError(0x1357);r.hr[4]=cleaned->OptimizeInplace(0xffffffff,r.cleaned.data(),nullptr,nullptr,nullptr);r.error[4]=GetLastError();
    SetLastError(0x1357);r.hr[5]=mesh->GenerateAdjacency(-1.f,r.adjacency.data());r.error[5]=GetLastError();
    require(cleaned->Release()==(r.clean_alias?1u:0u),"clean mesh releases exact native alias reference");require(mesh->Release()==0,"source mesh final release zero");return r;
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);int exit=1;HWND window=nullptr;
    try{
        HMODULE native=GetModuleHandleW(L"d3dx9_37.dll");require(native!=nullptr,"native D3DX import");auto create=symbol<Create>(native,"D3DXCreateMesh");auto clean=symbol<Clean>(native,"D3DXCleanMesh");
        HMODULE d3d=LoadLibraryW(L"d3d9.dll");require(d3d!=nullptr,"builtin D3D9");auto create9=symbol<IDirect3D9*(WINAPI*)(UINT)>(d3d,"Direct3DCreate9");Com<IDirect3D9> api;api.p=create9(D3D_SDK_VERSION);require(api.p!=nullptr,"Create9");
        WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName="X3LoadingMeshFixture";RegisterClassA(&cls);window=CreateWindowA(cls.lpszClassName,"Original mesh loading diagnostics",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);require(window!=nullptr,"window");
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;IDirect3DDevice9* device=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device),"device");
        const Result baseline=sequence(device,create,clean);
        ID3DXMesh* existing=nullptr;create_mesh(device,create,&existing);auto table=*reinterpret_cast<PVOID**>(existing);const PVOID original[]={table[20],table[22],table[27]};MEMORY_BASIC_INFORMATION page{};VirtualQuery(table,&page,sizeof page);
        SetEnvironmentVariableW(L"X3M_TELEMETRY",L"1");require(fixture_initialize(GetModuleHandleW(nullptr)),"install telemetry");
        fixture_fail_protection_restores(2);ID3DXMesh* debt_mesh=nullptr;create_mesh(device,&D3DXCreateMesh,&debt_mesh);
        require(table[20]==original[0]&&table[22]==original[1]&&table[27]==original[2]&&fixture_protection_debts()==1,"failed restore and rollback protection retain recovery debt");
        require(debt_mesh->Release()==0,"protection debt retains no mesh");
        fixture_fail_mesh_patch(2);ID3DXMesh* partial=nullptr;create_mesh(device,&D3DXCreateMesh,&partial);require(table[20]==original[0]&&table[22]==original[1]&&table[27]==original[2],"partial method install rolled back all slots");require(partial->Release()==0,"partial install retains no object ref");MEMORY_BASIC_INFORMATION repaired{};VirtualQuery(table,&repaired,sizeof repaired);require(fixture_protection_debts()==0&&repaired.Protect==page.Protect,"next patch recovers original protection not temporary RW");
        take_snapshot();const Result instrumented=sequence(device,&D3DXCreateMesh,&D3DXCleanMesh);require(instrumented==baseline,"full mesh outputs HRESULT LastError parity");
        require(table[20]!=original[0]&&table[22]!=original[1]&&table[27]!=original[2],"all three shared slots patched");MEMORY_BASIC_INFORMATION after{};VirtualQuery(table,&after,sizeof after);require(page.Protect==after.Protect,"shared vtable page protection preserved");
        auto data=take_snapshot();auto at=[&](Operation op)->const Sample&{return data[static_cast<unsigned>(op)];};
        require(at(Operation::MeshCreate).count==1&&at(Operation::MeshClean).count==1,"exact IAT mesh spans");require(at(Operation::MeshAdjacency).count>=2&&at(Operation::MeshPointReps).count>=1&&at(Operation::MeshOptimize).count>=2,"native shared method spans");
        for(auto op:{Operation::MeshCreate,Operation::MeshClean,Operation::MeshPointReps,Operation::MeshAdjacency,Operation::MeshOptimize}){const auto& s=at(op);require(s.maximum_ticks<=s.inclusive_ticks&&s.exclusive_ticks<=s.inclusive_ticks&&s.bytes==0,"bounded span aggregates");printf("MESH_COUNT op=%u count=%llu failures=%llu inclusive=%llu max=%llu\n",unsigned(op),s.count,s.failures,s.inclusive_ticks,s.maximum_ticks);}
        // This mesh existed before install, proving explicitly broader shared-table scope.
        DWORD adjacency[12]{};SetLastError(0x1357);ok(existing->GenerateAdjacency(1e-6f,adjacency),"preexisting object method");require(GetLastError()==baseline.error[0],"method LastError on preexisting object");require(take_snapshot()[unsigned(Operation::MeshAdjacency)].count>=1,"preexisting object counted");
        std::memcpy(&previous_generate,&table[22],sizeof previous_generate);auto foreign=&foreign_generate;PVOID foreign_pointer=nullptr;std::memcpy(&foreign_pointer,&foreign,sizeof foreign_pointer);replace(&table[22],foreign_pointer);
        fixture_fail_protection_restores(100);SetLastError(0x2468);shutdown();require(GetLastError()==0x2468&&active()&&fixture_protection_debts()>0,"failed shutdown protection recovery stays active");
        fixture_fail_protection_restores(0);SetLastError(0x2468);shutdown();require(GetLastError()==0x2468&&!active()&&fixture_protection_debts()==0,"quiescent retry restores protection and LastError");
        VirtualQuery(table,&repaired,sizeof repaired);require(repaired.Protect==page.Protect,"shutdown original page protection recovered");
        require(!fixture_initialize(GetModuleHandleW(nullptr)),"one shot initialization rejects foreign chain reinstall");require(table[20]==original[0]&&table[27]==original[2]&&table[22]==foreign_pointer,"shutdown restores owned slots preserves foreign hook");
        SetLastError(0x1357);ok(existing->GenerateAdjacency(1e-6f,adjacency),"foreign chain remains callable after shutdown");require(GetLastError()==baseline.error[0],"chained method LastError");replace(&table[22],original[1]);require(existing->Release()==0,"preexisting final release zero");
        require(device->Release()==0,"telemetry retained no device references");if(!loading_admission_witness())throw std::runtime_error("admission witness");printf("LOADING MESH RESULT checks=%u failures=0\n",checks);exit=0;
    }catch(const std::exception& e){printf("LOADING MESH FAIL %s checks=%u\n",e.what(),checks);}if(window)DestroyWindow(window);return exit;
}
