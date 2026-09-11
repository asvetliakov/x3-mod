#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/ownership/d3d9_ownership.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <thread>
using namespace x3m::ownership;
unsigned checks=0;
void check(bool value,const char* label){++checks;std::printf("CHECK %s %s\n",label,value?"PASS":"FAIL");if(!value)throw std::runtime_error(label);}
void ok(HRESULT value,const char* label){check(value==S_OK,label);}
template<class T>struct Com{T* p=nullptr;~Com(){reset();}void reset(){if(p)p->Release();p=nullptr;}T* operator->()const{return p;}};
using Create=IDirect3D9*(WINAPI*)(UINT);
struct Session{
 Com<IDirect3D9> factory;Com<IDirect3DDevice9> device;D3DPRESENT_PARAMETERS pp{};
 Session(Create create,HWND window){auto* native=create(D3D_SDK_VERSION);check(native!=nullptr,"factory");Options o;o.track_buffer_writes=true;o.capture_finite_positions=true;ok(wrap_factory(native,&factory.p,o),"wrap");pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;ok(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device.p),"device");}
};
struct Geometry{
 Com<IDirect3DVertexBuffer9> vb;Com<IDirect3DIndexBuffer9> ib;GeometryLeaseRequest request;
 Geometry(Session& s,bool indexed=true,UINT size=48){ok(s.device->CreateVertexBuffer(size,D3DUSAGE_WRITEONLY,D3DFVF_XYZ,D3DPOOL_MANAGED,&vb.p,nullptr),"VB");void* p=nullptr;ok(vb->Lock(0,0,&p,0),"VB upload Lock");std::memset(p,0,size);const float xyz[]={-.7f,-.7f,.5f,0,.7f,.5f,.7f,-.7f,.5f,0,0,0};std::memcpy(p,xyz,sizeof xyz);ok(vb->Unlock(),"VB upload Unlock");request.positions.expected_revision=1;request.positions.stride=12;request.positions.vertex_count=4;request.positions.position_type=D3DDECLTYPE_FLOAT3;request.indexed=indexed;
 if(indexed){ok(s.device->CreateIndexBuffer(12,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib.p,nullptr),"IB");ok(ib->Lock(0,0,&p,0),"IB upload Lock");const WORD values[]={0,1,2,0,1,2};std::memcpy(p,values,12);ok(ib->Unlock(),"IB upload Unlock");request.indices.expected_revision=1;request.indices.format=D3DFMT_INDEX16;request.indices.index_count=6;}
 FinitePositionView view;ok(get_finite_position_view(vb.p,request.positions,&view),"initial finite view");check(view.state==FiniteStatus::Finite,"initial finite");request.expected_generation=view.generation;
 }
};
GeometryFrameHandle begin(Session& s){GeometryFrameHandle frame;ok(begin_geometry_frame(s.device.p,&frame),"begin frame");check(frame.value!=0,"nonzero frame");return frame;}
GeometryLeaseHandle acquire(GeometryFrameHandle frame,Geometry& g){GeometryLeaseHandle lease;ok(acquire_geometry_lease(frame,g.vb.p,g.ib.p,g.request,&lease),"acquire pair");check(lease.value!=0,"nonzero lease");return lease;}
GeometryLeaseView inspect(GeometryFrameHandle frame,GeometryLeaseHandle lease){GeometryLeaseView view;ok(inspect_geometry_lease(frame,lease,&view),"inspect");check(view.status==S_OK&&view.vertex_buffer&&view.positions.state==FiniteStatus::Finite,"valid borrowed view");return view;}
ULONG refs(IUnknown* p){auto count=p->AddRef();p->Release();return count-1;}
void lifetime(Create create,HWND window){
 Session s(create,window);Geometry g(s);auto frame=begin(s);GeometryFrameHandle duplicate;check(begin_geometry_frame(s.device.p,&duplicate)==D3DERR_INVALIDCALL&&!duplicate.value,"begin never silently evicts active frame");auto* vb=borrowed_native_buffer_for_lock_contract(g.vb.p);auto* ib=borrowed_native_buffer_for_lock_contract(g.ib.p);const auto vr=refs(vb),ir=refs(ib);auto lease=acquire(frame,g);check(refs(vb)==vr+1&&refs(ib)==ir+1,"one independent native reference per buffer");
 ok(s.device->SetStreamSource(0,g.vb.p,0,12),"bind original VB");ok(s.device->SetIndices(g.ib.p),"bind original IB");g.vb.reset();g.ib.reset();auto view=inspect(frame,lease);check(view.vertex_buffer==vb&&view.index_buffer==ib,"inspect survives wrapper external zero without recreation");
 UINT offset=0,stride=0;ok(s.device->GetStreamSource(0,&g.vb.p,&offset,&stride),"recreate VB wrapper");ok(s.device->GetIndices(&g.ib.p),"recreate IB wrapper");check(inspect(frame,lease).generation==g.request.expected_generation,"same allocation evidence survives recreation");g.vb.reset();g.ib.reset();
 auto* native=borrowed_native_device(s.device.p);ok(native->SetVertexShader(nullptr),"native fixed vertex");ok(native->SetPixelShader(nullptr),"native fixed pixel");ok(native->SetFVF(D3DFVF_XYZ),"native FVF");ok(native->SetRenderState(D3DRS_LIGHTING,FALSE),"native lighting");ok(native->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"native cull");ok(native->SetRenderState(D3DRS_ZENABLE,FALSE),"native Z");ok(native->SetRenderState(D3DRS_TEXTUREFACTOR,0xffffffff),"native color");ok(native->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1),"native colorop");ok(native->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TFACTOR),"native colorarg");
 D3DMATRIX identity{};identity._11=identity._22=identity._33=identity._44=1;ok(native->SetTransform(D3DTS_WORLD,&identity),"native world");ok(native->SetTransform(D3DTS_VIEW,&identity),"native view");ok(native->SetTransform(D3DTS_PROJECTION,&identity),"native projection");ok(native->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear");ok(native->BeginScene(),"native scene");ok(native->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2),"actual native draw from leased allocation");ok(native->EndScene(),"native end");
 Com<IDirect3DSurface9> back,read;ok(native->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back.p),"backbuffer");D3DSURFACE_DESC desc{};ok(back->GetDesc(&desc),"backbuffer desc");ok(native->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&read.p,nullptr),"readback surface");ok(native->GetRenderTargetData(back.p,read.p),"fixture pixel readback");D3DLOCKED_RECT locked{};ok(read->LockRect(&locked,nullptr,D3DLOCK_READONLY),"readback Lock");const DWORD pixel=*reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+16*locked.Pitch+16*4);ok(read->UnlockRect(),"readback Unlock");check((pixel&0xffffff)==0xffffff,"leased native draw produced white center pixel");
 ok(native->SetStreamSource(0,nullptr,0,0),"unbind VB");ok(native->SetIndices(nullptr),"unbind IB");ok(release_geometry_lease(frame,lease),"release single pair");GeometryLeaseView stale;check(inspect_geometry_lease(frame,lease,&stale)==E_INVALIDARG&&!stale.vertex_buffer,"released lease stale");check(release_geometry_lease(frame,lease)==E_INVALIDARG,"double release rejected");ok(end_geometry_frame(frame),"end frame");check(end_geometry_frame(frame)==E_INVALIDARG,"double end rejected");
}
void mutation(Create create,HWND window){
 Session s(create,window),other(create,window);Geometry g(s),foreign(other);auto frame=begin(s);GeometryLeaseHandle refused{99};auto vr=refs(borrowed_native_buffer_for_lock_contract(g.vb.p));check(acquire_geometry_lease(frame,g.vb.p,foreign.ib.p,g.request,&refused)==E_INVALIDARG&&!refused.value,"cross-device pair refused atomically");check(refs(borrowed_native_buffer_for_lock_contract(g.vb.p))==vr,"partial failed pair retains no VB ref");
 auto bad=g.request;++bad.indices.expected_revision;check(acquire_geometry_lease(frame,g.vb.p,g.ib.p,bad,&refused)==S_FALSE&&!refused.value,"stale IB revision refused before retention");check(refs(borrowed_native_buffer_for_lock_contract(g.vb.p))==vr,"failed second evidence retains no first native ref");
 bad=g.request;++bad.expected_generation;check(acquire_geometry_lease(frame,g.vb.p,g.ib.p,bad,&refused)==S_FALSE&&!refused.value,"original generation must match");bad=g.request;bad.indexed=false;check(acquire_geometry_lease(frame,g.vb.p,g.ib.p,bad,&refused)==E_INVALIDARG,"nonindexed cannot retain stale bound IB");
 auto lease=acquire(frame,g);void* p=nullptr;ok(g.vb->Lock(0,12,&p,0),"pending write");GeometryLeaseView view;check(inspect_geometry_lease(frame,lease,&view)==S_FALSE&&!view.vertex_buffer,"pending write prevents replay");std::memset(p,0,12);ok(g.vb->Unlock(),"write completed");check(inspect_geometry_lease(frame,lease,&view)==S_FALSE&&!view.vertex_buffer&&view.reason==FiniteEvidenceReason::RevisionMismatch,"rewritten finite bytes do not satisfy old revision");ok(end_geometry_frame(frame),"end invalidated frame");
 g.request.positions.expected_revision=2;frame=begin(s);lease=acquire(frame,g);ok(g.ib->Lock(0,12,&p,0),"IB rewrite");std::memset(p,0,12);ok(g.ib->Unlock(),"IB rewritten");check(inspect_geometry_lease(frame,lease,&view)==S_FALSE&&!view.index_buffer,"IB changed between draw and replay refused");ok(end_geometry_frame(frame),"end IB mutation");
 g.request.indices.expected_revision=2;frame=begin(s);lease=acquire(frame,g);ok(s.device->Reset(&s.pp),"Reset retires leases");check(inspect_geometry_lease(frame,lease,&view)==E_INVALIDARG&&!view.vertex_buffer,"Reset stale handle refused");check(refs(borrowed_native_buffer_for_lock_contract(g.vb.p))==vr,"Reset balances lease native refs");
}
struct Fault {
 static HRESULT WINAPI reset(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*){return D3DERR_INVALIDCALL;}
 static HRESULT WINAPI present(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*){return D3DERR_DEVICELOST;}
 IUnknown* object;void** previous;void* table[119];
 Fault(IDirect3DDevice9* device,bool loss):object(device),previous(*reinterpret_cast<void***>(device)){std::memcpy(table,previous,sizeof table);if(loss){auto fn=&present;std::memcpy(&table[17],&fn,sizeof fn);}else{auto fn=&reset;std::memcpy(&table[16],&fn,sizeof fn);}*reinterpret_cast<void***>(object)=table;}
 ~Fault(){*reinterpret_cast<void***>(object)=previous;}
};
void retirement(Create create,HWND window){
 for(bool loss:{false,true}){Session s(create,window);Geometry g(s);auto frame=begin(s);auto lease=acquire(frame,g);auto* native=borrowed_native_device(s.device.p);{Fault fault(native,loss);check((loss?s.device->Present(nullptr,nullptr,nullptr,nullptr):s.device->Reset(&s.pp))==(loss?D3DERR_DEVICELOST:D3DERR_INVALIDCALL),"retirement native HRESULT preserved");}GeometryLeaseView out;check(inspect_geometry_lease(frame,lease,&out)==E_INVALIDARG&&!out.vertex_buffer,"loss or failed Reset invalidates frame");ok(s.device->Reset(&s.pp),"recover native device");}
 GeometryFrameHandle stale_frame;GeometryLeaseHandle stale_lease;
 {Session s(create,window);Geometry g(s);stale_frame=begin(s);stale_lease=acquire(stale_frame,g);s.device.reset();g.vb.reset();g.ib.reset();GeometryLeaseView out;check(inspect_geometry_lease(stale_frame,stale_lease,&out)==E_INVALIDARG,"last logical parent release drains lease without cycle");check(s.factory.p->Release()==0,"factory final logical release returns zero");s.factory.p=nullptr;}
 {Session s(create,window);Geometry g(s);auto frame=begin(s);auto lease=acquire(frame,g);check(frame.value!=stale_frame.value&&lease.value!=stale_lease.value,"new device and slots never reuse old serials");GeometryLeaseView out;check(inspect_geometry_lease(frame,stale_lease,&out)==E_INVALIDARG,"old lease cannot target reused slot");ok(end_geometry_frame(frame),"new frame cleanup");}
}
void capacity(Create create,HWND window){
 Session s(create,window);Geometry g(s,false);auto frame=begin(s);std::vector<GeometryLeaseHandle> leases(geometry_leases_per_frame);bool accepted=true;for(auto& lease:leases)accepted=accepted&&(acquire_geometry_lease(frame,g.vb.p,nullptr,g.request,&lease)==S_OK);check(accepted,"full bounded frame capacity accepted");GeometryLeaseHandle extra{99};check(acquire_geometry_lease(frame,g.vb.p,nullptr,g.request,&extra)==E_OUTOFMEMORY&&!extra.value,"frame capacity refuses without eviction");inspect(frame,leases.front());ok(release_geometry_lease(frame,leases.front()),"release capacity slot");auto old=leases.front();ok(acquire_geometry_lease(frame,g.vb.p,nullptr,g.request,&leases.front()),"capacity slot reusable");check(old.value!=leases.front().value,"reused slot has fresh handle");GeometryLeaseView stale;check(inspect_geometry_lease(frame,old,&stale)==E_INVALIDARG&&!stale.vertex_buffer,"stale reused slot cannot expose new reservation");check(release_geometry_lease(frame,old)==E_INVALIDARG,"stale reused slot cannot release new reservation");inspect(frame,leases.front());ok(end_geometry_frame(frame),"capacity frame drained");
 Geometry larger(s,false,256*1024);frame=begin(s);unsigned admitted=0;while(admitted<geometry_leases_per_frame&&acquire_geometry_lease(frame,larger.vb.p,nullptr,larger.request,&extra)==S_OK)++admitted;check(admitted==geometry_native_byte_limit/(256*1024),"conservative native byte budget exact bound");check(!extra.value,"byte budget refusal clears output");ok(end_geometry_frame(frame),"byte reservations returned");frame=begin(s);acquire(frame,larger);ok(end_geometry_frame(frame),"returned byte budget admits later frame");
}
struct SlotChange {
 static inline unsigned foreign_calls=0;
 static ULONG WINAPI foreign_ref(IUnknown*){++foreign_calls;return 99;}
 static HRESULT WINAPI foreign_lock(IUnknown*,UINT,UINT,void**,DWORD){return E_FAIL;}
 IUnknown* object;void** previous;void* table[14];
 SlotChange(IUnknown* value,unsigned slot):object(value),previous(*reinterpret_cast<void***>(value)){
  std::memcpy(table,previous,sizeof table);if(slot<=2){auto fn=&foreign_ref;std::memcpy(&table[slot],&fn,sizeof fn);}else{auto fn=&foreign_lock;std::memcpy(&table[slot],&fn,sizeof fn);}*reinterpret_cast<void***>(object)=table;
 }
 ~SlotChange(){*reinterpret_cast<void***>(object)=previous;}
};
void endpoint_changes(Create create,HWND window){
 Session s(create,window);Geometry g(s);auto frame=begin(s);auto lease=acquire(frame,g);GeometryLeaseView out;
 {SlotChange changed(g.vb.p,11);check(inspect_geometry_lease(frame,lease,&out)==S_FALSE&&!out.vertex_buffer,"current canonical wrapper foreign Lock refuses retained replay");}
 inspect(frame,lease);{SlotChange changed(g.ib.p,12);check(inspect_geometry_lease(frame,lease,&out)==S_FALSE&&!out.vertex_buffer,"current canonical wrapper foreign Unlock refuses retained replay");}
 auto* native=borrowed_native_buffer_for_lock_contract(g.vb.p);const ULONG baseline=refs(native)-1;
 {SlotChange changed(native,2);check(inspect_geometry_lease(frame,lease,&out)==S_FALSE&&!out.vertex_buffer,"native foreign Release refuses replay");ok(release_geometry_lease(frame,lease),"stored certified Release balances retained reference");check(SlotChange::foreign_calls==0,"cleanup never dispatches foreign Release");}
 check(refs(native)==baseline,"certified cleanup released actual original native ref");
 {SlotChange changed(native,1);GeometryLeaseHandle rejected;check(acquire_geometry_lease(frame,g.vb.p,g.ib.p,g.request,&rejected)==S_FALSE&&!rejected.value,"foreign AddRef cannot fake acquisition");check(SlotChange::foreign_calls==0,"foreign AddRef not called");}
 ok(end_geometry_frame(frame),"endpoint frame cleanup");
}
struct ReleaseProbe final:IUnknown {
 ULONG count=1;IDirect3DDevice9* device;HANDLE start=CreateEventA(nullptr,TRUE,FALSE,nullptr),done=CreateEventA(nullptr,TRUE,FALSE,nullptr);bool completed=false;
 explicit ReleaseProbe(IDirect3DDevice9* value):device(value){}~ReleaseProbe(){CloseHandle(start);CloseHandle(done);}
 HRESULT WINAPI QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown)return E_NOINTERFACE;AddRef();*out=this;return S_OK;}
 ULONG WINAPI AddRef()override{return ++count;}
 ULONG WINAPI Release()override{auto remaining=--count;if(!remaining){SetEvent(start);completed=WaitForSingleObject(done,3000)==WAIT_OBJECT_0;}return remaining;}
};
void release_lock_order(Create create,HWND window){
 Session s(create,window);Geometry g(s,false);auto frame=begin(s);acquire(frame,g);auto* native=borrowed_native_buffer_for_lock_contract(g.vb.p);
 const GUID tag={0xae476b80,0x0b75,0x45ea,{0x92,0x9f,0x51,0x5c,0x7e,0x3e,0xce,0x30}};ReleaseProbe probe(s.device.p);ok(native->SetPrivateData(tag,&probe,sizeof(IUnknown*),D3DSPD_IUNKNOWN),"attach destructor lock-order probe");probe.Release();g.vb.reset();
 HRESULT query=E_FAIL;std::thread worker([&]{WaitForSingleObject(probe.start,INFINITE);FiniteUploadStatistics stats;query=get_finite_upload_statistics(probe.device,&stats);SetEvent(probe.done);});ok(end_geometry_frame(frame),"retire last native reference");worker.join();check(probe.completed&&query==S_OK,"native destruction occurs outside global registry");
}
struct CallState{unsigned char x87[108];unsigned mxcsr;DWORD error;CallState():error(GetLastError()){asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1":"=m"(x87),"=m"(mxcsr)::"memory");}void restore(){asm volatile("frstor %0\n\tldmxcsr %1"::"m"(x87),"m"(mxcsr):"memory");SetLastError(error);}};
void execution_preservation(Create create,HWND window){
 Session s(create,window);Geometry g(s);CallState original;unsigned short control=0x077f;unsigned mxcsr=0x3fa0;asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1"::"m"(control),"m"(mxcsr):"memory");SetLastError(0x9abc1234);CallState before;GeometryFrameHandle frame;GeometryLeaseHandle lease;GeometryLeaseView view;
 HRESULT a=begin_geometry_frame(s.device.p,&frame),b=acquire_geometry_lease(frame,g.vb.p,g.ib.p,g.request,&lease),c=inspect_geometry_lease(frame,lease,&view),d=release_geometry_lease(frame,lease),e=end_geometry_frame(frame);CallState after;original.restore();check(a==S_OK&&b==S_OK&&c==S_OK&&d==S_OK&&e==S_OK,"all lease operations succeed under seeded FP");check(before.error==after.error&&before.mxcsr==after.mxcsr&&!std::memcmp(before.x87,after.x87,108),"lease operations preserve live x87 MXCSR LastError");
}


void process_capacity(Create create,HWND window){
 Session first(create,window),second(create,window),third(create,window);Geometry a(first,false),b(second,false),c(third,false),unique(first,false);auto fa=begin(first),fb=begin(second),fc=begin(third);GeometryLeaseHandle lease,unique_lease;bool full=true;
 for(unsigned n=0;n<geometry_leases_per_frame;++n){auto& item=n?a:unique;full=full&&(acquire_geometry_lease(fa,item.vb.p,nullptr,item.request,&lease)==S_OK);if(!n)unique_lease=lease;full=full&&(acquire_geometry_lease(fb,b.vb.p,nullptr,b.request,&lease)==S_OK);}
 check(full,"two frames fill exact global lease bound");check(acquire_geometry_lease(fc,c.vb.p,nullptr,c.request,&lease)==E_OUTOFMEMORY&&!lease.value,"global capacity refuses third frame without native retention");
 auto* native=borrowed_native_buffer_for_lock_contract(unique.vb.p);const GUID tag={0x3351da12,0x71db,0x44b6,{0x98,0xee,0x42,0x8b,0x79,0x4a,0x29,0x1d}};ReleaseProbe probe(first.device.p);ok(native->SetPrivateData(tag,&probe,sizeof(IUnknown*),D3DSPD_IUNKNOWN),"attach delayed quota release probe");probe.Release();unique.vb.reset();HRESULT during=E_FAIL;GeometryLeaseHandle blocked{99};
 std::thread worker([&]{WaitForSingleObject(probe.start,INFINITE);during=acquire_geometry_lease(fc,c.vb.p,nullptr,c.request,&blocked);SetEvent(probe.done);});ok(release_geometry_lease(fa,unique_lease),"release pending native destructor at global bound");worker.join();check(probe.completed&&during==E_OUTOFMEMORY&&!blocked.value,"detached native refs remain globally charged until Release finishes");ok(acquire_geometry_lease(fc,c.vb.p,nullptr,c.request,&lease),"completed native cleanup returns global reservation");
 ok(end_geometry_frame(fa),"first global frame releases capacity");ok(end_geometry_frame(fb),"second global frame cleanup");ok(end_geometry_frame(fc),"third global frame cleanup");
}
void all_clean(Create create,HWND window){Session s(create,window);FiniteUploadStatistics stats;ok(get_finite_upload_statistics(s.device.p,&stats),"final global accounting");check(stats.global_sidecars==0&&stats.global_payload_bytes==0,"all lease native and CPU references returned");}


void malformed_handles(Create create,HWND window){
 Session s(create,window);Geometry g(s);auto frame=begin(s);auto lease=acquire(frame,g);GeometryLeaseView view;
 const GeometryLeaseHandle forged_lease{lease.value^(std::uint64_t(1)<<63)};
 const GeometryFrameHandle forged_frame{frame.value^(std::uint64_t(1)<<63)};
 check(inspect_geometry_lease(frame,forged_lease,&view)==E_INVALIDARG&&!view.vertex_buffer,"forged same-index lease serial rejected");
 check(release_geometry_lease(frame,forged_lease)==E_INVALIDARG,"forged lease cannot release live native refs");
 check(inspect_geometry_lease(forged_frame,lease,&view)==E_INVALIDARG&&!view.vertex_buffer,"forged same-index frame serial rejected");
 check(end_geometry_frame(forged_frame)==E_INVALIDARG,"forged frame cannot retire live reservations");
 check(inspect_geometry_lease({lease.value},lease,&view)==E_INVALIDARG,"lease value cannot impersonate frame type");
 check(inspect_geometry_lease(frame,{frame.value},&view)==E_INVALIDARG,"frame value cannot impersonate lease type");
 inspect(frame,lease);ok(end_geometry_frame(frame),"real handle still owns reservation after malformed operations");
}

int main(){try{auto module=LoadLibraryA("C:\\windows\\system32\\d3d9.dll");check(module!=nullptr,"native module");Create create=nullptr;auto entry=GetProcAddress(module,"Direct3DCreate9");std::memcpy(&create,&entry,sizeof create);check(create!=nullptr,"native entry");auto window=CreateWindowExA(0,"STATIC","geometry lease fixture",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);check(window!=nullptr,"window");lifetime(create,window);mutation(create,window);retirement(create,window);capacity(create,window);endpoint_changes(create,window);release_lock_order(create,window);execution_preservation(create,window);process_capacity(create,window);malformed_handles(create,window);all_clean(create,window);DestroyWindow(window);FreeLibrary(module);std::printf("RESULT PASS checks=%u\n",checks);return 0;}catch(const std::exception& e){std::printf("RESULT FAIL %s checks=%u\n",e.what(),checks);return 1;}}
