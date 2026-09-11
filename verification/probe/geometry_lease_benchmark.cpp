#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/ownership/d3d9_ownership.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <cstdint>
using namespace x3m::ownership;
unsigned checks=0;
void ensure(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
void ok(HRESULT value,const char* label){ensure(value==S_OK,label);}
template<class T>struct Com{T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;};
using Create=IDirect3D9*(WINAPI*)(UINT);
std::int64_t now(){LARGE_INTEGER q;QueryPerformanceCounter(&q);return q.QuadPart;}
std::int64_t frequency;
double us(std::int64_t ticks){return double(ticks)*1e6/double(frequency);}
struct Pair{IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;IDirect3DVertexBuffer9* native_vb=nullptr;IDirect3DIndexBuffer9* native_ib=nullptr;GeometryLeaseRequest request;};
struct Session{
 Com<IDirect3D9> factory;Com<IDirect3DDevice9> device;std::vector<Pair> pairs;UINT vb_bytes,ib_bytes;bool varying;
 Session(Create create,HWND window,unsigned distinct,UINT vbsize,UINT ibsize,bool varying_range):vb_bytes(vbsize),ib_bytes(ibsize),varying(varying_range){
  auto* native=create(D3D_SDK_VERSION);ensure(native,"factory");Options options;options.capture_finite_positions=true;options.track_buffer_writes=true;ok(wrap_factory(native,&factory.p,options),"wrap");D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=pp.BackBufferHeight=16;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;ok(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device.p),"device");
  pairs.resize(distinct);
  for(auto& p:pairs){ok(device->CreateVertexBuffer(vbsize,D3DUSAGE_WRITEONLY,D3DFVF_XYZ,D3DPOOL_MANAGED,&p.vb,nullptr),"VB");ok(device->CreateIndexBuffer(ibsize,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&p.ib,nullptr),"IB");void* memory=nullptr;ok(p.vb->Lock(0,0,&memory,0),"VB Lock");std::memset(memory,0,vbsize);ok(p.vb->Unlock(),"VB Unlock");ok(p.ib->Lock(0,0,&memory,0),"IB Lock");std::memset(memory,0,ibsize);ok(p.ib->Unlock(),"IB Unlock");p.request.positions.expected_revision=1;p.request.positions.stride=12;p.request.positions.vertex_count=varying?256:4;p.request.positions.position_type=D3DDECLTYPE_FLOAT3;p.request.indexed=true;p.request.indices.expected_revision=1;p.request.indices.format=D3DFMT_INDEX16;p.request.indices.index_count=ibsize/2;FiniteUploadStatistics stats;ok(get_finite_upload_statistics(device.p,&stats),"initial stats");p.request.expected_generation=stats.generation;p.native_vb=borrowed_native_buffer_for_lock_contract(p.vb);p.native_ib=borrowed_native_buffer_for_lock_contract(p.ib);ensure(p.native_vb&&p.native_ib,"native endpoints");}
 }
 ~Session(){for(auto& p:pairs){p.ib->Release();p.vb->Release();}}
 GeometryLeaseRequest request(unsigned index){auto request=pairs[index%pairs.size()].request;if(varying)request.positions.first_vertex=index%16;return request;}
 FiniteUploadStatistics stats(){FiniteUploadStatistics value;ok(get_finite_upload_statistics(device.p,&value),"stats");return value;}
};
void sample(Session& session,const char* name,unsigned count,unsigned iteration,bool output){
 std::vector<GeometryLeaseHandle> leases(count);GeometryFrameHandle frame;auto t0=now();ok(begin_geometry_frame(session.device.p,&frame),"begin");auto t1=now();auto before=session.stats();
 auto a0=now();for(unsigned i=0;i<count;++i){auto& p=session.pairs[i%session.pairs.size()];ok(acquire_geometry_lease(frame,p.vb,p.ib,session.request(i),&leases[i]),"acquire");}auto a1=now();auto acquired=session.stats();
 auto i0=now();for(auto lease:leases){GeometryLeaseView view;ok(inspect_geometry_lease(frame,lease,&view),"inspect");ensure(view.vertex_buffer&&view.index_buffer&&view.status==S_OK,"inspect result");}auto i1=now();auto inspected=session.stats();
 auto l0=now();for(unsigned i=0;i<count;++i){GeometryLeaseView view;ensure(inspect_geometry_lease(frame,{UINT64_MAX},&view)==E_INVALIDARG,"invalid lookup");}auto l1=now();
 auto e0=now();ok(end_geometry_frame(frame),"end");auto e1=now();
 auto p0=now();for(unsigned i=0;i<count;++i){auto& p=session.pairs[i%session.pairs.size()];auto request=session.request(i);FinitePositionView vertex;IndexRangeView index;ok(get_finite_position_view(p.vb,request.positions,&vertex),"position baseline");ok(get_index_range_view(p.ib,request.indices,&index),"index baseline");ensure(vertex.state==FiniteStatus::Finite&&index.known,"baseline evidence");}auto p1=now();
 auto n0=now();for(unsigned i=0;i<count;++i){auto& p=session.pairs[i%session.pairs.size()];p.native_vb->AddRef();p.native_ib->AddRef();}auto n1=now();for(unsigned i=0;i<count;++i){auto& p=session.pairs[i%session.pairs.size()];p.native_ib->Release();p.native_vb->Release();}auto n2=now();
 if(output)std::printf("SAMPLE profile=%s leases=%u iteration=%u distinct_pairs=%u reserved_bytes=%llu begin_us=%.3f acquire_us=%.3f inspect_us=%.3f retire_us=%.3f public_evidence_us=%.3f invalid_lookup_us=%.3f native_addref_us=%.3f native_release_us=%.3f acquire_qualifier_us=%.3f inspect_qualifier_us=%.3f acquire_components=%llu inspect_components=%llu acquire_cache_hits=%llu inspect_cache_hits=%llu\n",name,count,iteration,unsigned(session.pairs.size()),static_cast<unsigned long long>(std::uint64_t(count)*(session.vb_bytes+session.ib_bytes)),us(t1-t0),us(a1-a0),us(i1-i0),us(e1-e0),us(p1-p0),us(l1-l0),us(n1-n0),us(n2-n1),us(acquired.qualifier_ticks-before.qualifier_ticks),us(inspected.qualifier_ticks-acquired.qualifier_ticks),static_cast<unsigned long long>(acquired.position_components-before.position_components),static_cast<unsigned long long>(inspected.position_components-acquired.position_components),static_cast<unsigned long long>(acquired.query_cache_hits-before.query_cache_hits),static_cast<unsigned long long>(inspected.query_cache_hits-acquired.query_cache_hits));
}
int main(){try{LARGE_INTEGER f;ensure(QueryPerformanceFrequency(&f),"QPC");frequency=f.QuadPart;auto module=LoadLibraryA("C:\\windows\\system32\\d3d9.dll");ensure(module,"native module");Create create=nullptr;auto entry=GetProcAddress(module,"Direct3DCreate9");std::memcpy(&create,&entry,sizeof create);ensure(create,"create entry");auto window=CreateWindowExA(0,"STATIC","geometry lease CPU benchmark",0,0,0,16,16,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);ensure(window,"window");
 struct Profile{const char* name;unsigned distinct;UINT vb,ib;bool varying;};
 const Profile profiles[]={{"shared_small",1,48,12,false},{"shared_varying_range",1,65536,12288,true},{"many_small",1024,48,12,false}};
 std::printf("BENCHMARK qpc_frequency=%lld draws=0 trials=7 counts=1,100,700,4096\n",static_cast<long long>(frequency));
 for(auto profile:profiles){auto setup=now();Session session(create,window,profile.distinct,profile.vb,profile.ib,profile.varying);std::printf("SETUP profile=%s wall_us=%.3f native_buffer_bytes=%llu\n",profile.name,us(now()-setup),static_cast<unsigned long long>(std::uint64_t(profile.distinct)*(profile.vb+profile.ib)));for(unsigned count:{1u,100u,700u,4096u}){sample(session,profile.name,count,0,false);for(unsigned iteration=0;iteration<7;++iteration)sample(session,profile.name,count,iteration,true);}}
 DestroyWindow(window);FreeLibrary(module);std::printf("RESULT PASS checks=%u samples=84 draws=0\n",checks);return 0;
 }catch(const std::exception& e){std::printf("RESULT FAIL error=%s checks=%u\n",e.what(),checks);return 1;}}
