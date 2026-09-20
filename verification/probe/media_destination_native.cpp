// Standalone public D3D9 fixture. Root owns build/Wine queue; no game launch.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include "../../src/proxy/media_destination.h"
#include "../../src/proxy/media_presentation_gate_win32.h"
#include "../../src/ownership/d3d9_ownership.h"
#include "lav_worker.h"
#include <array>
#include <cstdio>
#include <cstring>
namespace d=x3m::media_destination;namespace g=x3m::media_presentation_gate;namespace m=x3m::media;namespace p=x3m::media_playback;
namespace x3m::object_trace {bool executable_verified(){return true;}}
namespace x3m::engine_patch {bool install_window_open(){return true;}}
unsigned checks=0,failures=0;
void verify(bool v,const char* label){++checks;if(!v){++failures;std::printf("FAIL %s\n",label);}}
void require(bool v,const char* label){verify(v,label);if(!v)ExitProcess(2);}
unsigned address(const void* p){return reinterpret_cast<unsigned>(p);}
void word(unsigned char* p,unsigned v){std::memcpy(p,&v,4);}
void jump(unsigned char* p,unsigned target){p[0]=0xe9;word(p+1,target-address(p)-5);}
struct Setup {
 HMODULE dll=nullptr;HWND window=nullptr;IDirect3D9* factory=nullptr;IDirect3DDevice9* app=nullptr;D3DPRESENT_PARAMETERS pp{};
 Setup(){dll=LoadLibraryA("d3d9.dll");require(dll,"D3D9 loaded");using Create=IDirect3D9*(WINAPI*)(UINT);Create create=nullptr;auto symbol=GetProcAddress(dll,"Direct3DCreate9");std::memcpy(&create,&symbol,sizeof create);require(create,"factory export");
 require(x3m::ownership::wrap_factory(create(D3D_SDK_VERSION),&factory)==S_OK,"canonical factory");window=CreateWindowExA(0,"STATIC","Media destination fixture",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);require(window,"fixture window");
 pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=32;
 require(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&app)==S_OK,"canonical native device");}
 ~Setup(){if(app)app->Release();if(factory)factory->Release();if(window)DestroyWindow(window);if(dll)FreeLibrary(dll);}
};
static d::Destination* active_destination;static Setup* active_setup;static unsigned presents=0,resets=0;static bool reset_requested=false,fail_reset=false;static HRESULT last_reset=S_OK;
void __cdecl present_work(){++presents;if(!reset_requested)return;reset_requested=false;++resets;require(active_destination->begin_engine_reset(GetCurrentThreadId()),"engine Reset begins after gate");auto pp=active_setup->pp;if(fail_reset)pp.SwapEffect=static_cast<D3DSWAPEFFECT>(0);last_reset=active_setup->app->Reset(&pp);active_destination->end_engine_reset(GetCurrentThreadId(),SUCCEEDED(last_reset));}
struct Gate {
 g::Admission admission;g::Win32Platform platform;g::Transaction transaction;unsigned renderer[8]{},root=0,available=1;unsigned char* body=nullptr;
 Gate(){body=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));require(body,"synthetic presentation body");root=address(renderer);
 g::Site site{address(body),address(&root),address(body)+5,address(body)+128,address(&available),{0xa1,0,0,0,0,0x8b,0x48,0x18},{0xc7,0x05,0,0,0,0,0,0,0,0,0xc3}};
 word(site.entry_bytes+1,address(&root));word(site.defer_bytes+2,address(&available));std::memcpy(body,site.entry_bytes,8);jump(body+8,address(reinterpret_cast<void*>(&present_work)));std::memcpy(body+128,site.defer_bytes,11);
 require(FlushInstructionCache(GetCurrentProcess(),body,4096),"presentation body cache flush");require(admission.qualify_owner(GetCurrentThreadId()),"presentation owner");require(g::stage(platform,transaction,site,admission)&&g::install(platform,transaction,admission)&&admission.enable(GetCurrentThreadId()),"actual emitted presentation gate installed");}
 void present(){reinterpret_cast<void(__cdecl*)()>(body)();}
};
struct Patch {
 void* object=nullptr;void** original=nullptr;std::array<void*,119> table{};
 Patch(void* p,unsigned n):object(p),original(*reinterpret_cast<void***>(p)){std::memcpy(table.data(),original,n*4);*reinterpret_cast<void***>(p)=table.data();}
 void restore(){if(object){*reinterpret_cast<void***>(object)=original;object=nullptr;}}
 ~Patch(){restore();}
};
static IDirect3DSurface9* raw_surface=nullptr;
using CreateSurface=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,D3DPOOL,IDirect3DSurface9**,HANDLE*);
static CreateSurface original_create;
HRESULT WINAPI create_spy(IDirect3DDevice9* device,UINT w,UINT h,D3DFORMAT format,D3DPOOL pool,IDirect3DSurface9** out,HANDLE* shared){const auto hr=original_create(device,w,h,format,pool,out,shared);if(SUCCEEDED(hr))raw_surface=*out;return hr;}
struct NativeRefSpy {
 static NativeRefSpy* current;Patch patch;using Ref=ULONG(WINAPI*)(IUnknown*);Ref add,release;unsigned adds=0,releases=0;Gate& gate;
 static ULONG WINAPI add_call(IUnknown* p){auto& s=*current;++s.adds;return s.add(p);}
 static ULONG WINAPI release_call(IUnknown* p){auto& s=*current;++s.releases;return s.release(p);}
 NativeRefSpy(IDirect3DSurface9* s,Gate& g):patch(s,17),add(reinterpret_cast<Ref>(patch.original[1])),release(reinterpret_cast<Ref>(patch.original[2])),gate(g){current=this;patch.table[1]=reinterpret_cast<void*>(&add_call);patch.table[2]=reinterpret_cast<void*>(&release_call);}
 ~NativeRefSpy(){current=nullptr;}
};NativeRefSpy* NativeRefSpy::current=nullptr;
static d::BindingOwner binding{{0,1},{0x8000,1}};
struct Live {d::Current value{true,p::Continuation::live};static d::Current read(void* p,const d::CopyRequest&) noexcept{return static_cast<Live*>(p)->value;}};
struct SurfaceSpy {
 static SurfaceSpy* current;Patch patch;Gate& gate;d::Destination& destination;Live& live;unsigned stage=0,action=0,unlocks=0,locks=0,releases=0;
 using Desc=HRESULT(WINAPI*)(IDirect3DSurface9*,D3DSURFACE_DESC*);using Lock=HRESULT(WINAPI*)(IDirect3DSurface9*,D3DLOCKED_RECT*,const RECT*,DWORD);using Unlock=HRESULT(WINAPI*)(IDirect3DSurface9*);using Ref=ULONG(WINAPI*)(IUnknown*);
 void event(unsigned n){verify(gate.admission.depth()==1,"canonical COM spy inside copy depth");const auto before=resets;reset_requested=true;gate.present();verify(resets==before&&gate.available==0,"emitted gate excludes native Reset while retained or mapped");reset_requested=false;
  if(n!=stage)return;
  if(action==1)destination.observe_record(binding,1,true);
  if(action==2)live.value.continuation=p::Continuation::abort_manager;
  if(action==3)live.value.operation_live=false;}
 static HRESULT WINAPI desc(IDirect3DSurface9* p,D3DSURFACE_DESC* out){auto& s=*current;const auto hr=reinterpret_cast<Desc>(s.patch.original[12])(p,out);s.event(1);return hr;}
 static HRESULT WINAPI lock(IDirect3DSurface9* p,D3DLOCKED_RECT* out,const RECT* rect,DWORD flags){auto& s=*current;const auto hr=reinterpret_cast<Lock>(s.patch.original[13])(p,out,rect,flags);if(SUCCEEDED(hr))++s.locks;s.event(2);return hr;}
 static HRESULT WINAPI unlock(IDirect3DSurface9* p){auto& s=*current;++s.unlocks;const auto hr=reinterpret_cast<Unlock>(s.patch.original[14])(p);s.event(3);return hr;}
 static ULONG WINAPI release(IUnknown* p){auto& s=*current;++s.releases;const auto call=reinterpret_cast<Ref>(s.patch.original[2]);s.event(4);return call(p);}
 SurfaceSpy(IDirect3DSurface9* p,Gate& g,d::Destination& d,Live& l):patch(p,17),gate(g),destination(d),live(l){current=this;patch.table[12]=reinterpret_cast<void*>(&desc);patch.table[13]=reinterpret_cast<void*>(&lock);patch.table[14]=reinterpret_cast<void*>(&unlock);patch.table[2]=reinterpret_cast<void*>(&release);}
 ~SurfaceSpy(){current=nullptr;}
};SurfaceSpy* SurfaceSpy::current=nullptr;
extern unsigned destination_abi_checks();
int main(){Setup setup;Gate gate;d::NativeIdentitySource source;d::Destination destination(gate.admission,source,GetCurrentThreadId());active_setup=&setup;active_destination=&destination;d::bind_reset_observer(&destination);destination.set_device_key(address(setup.app),GetCurrentThreadId());
 IDirect3DSurface9* surface=nullptr;{Patch device(x3m::ownership::borrowed_native_device(setup.app),119);original_create=reinterpret_cast<CreateSurface>(device.original[36]);device.table[36]=reinterpret_cast<void*>(&create_spy);require(setup.app->CreateOffscreenPlainSurface(4,3,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&surface,nullptr)==S_OK&&raw_surface,"actual canonical SYSTEMMEM surface");}
 {std::lock_guard<std::mutex> lock(destination.domain());require(destination.state_locked().table(0x20000,4,0,false),"table provenance allocated");}
 d::State::Publication publication;{std::lock_guard<std::mutex> lock(destination.domain());publication=destination.state_locked().publish_slot(0x20000,0,0x30000,address(surface));}destination.qualify(publication);destination.observe_record(binding,0,true);
 auto storage=std::make_shared<m::lav_detail::FrameStorage>(nullptr);require(storage->reserve(0,{binding.session,3,4},1,9),"canonical frame reserved");auto& pixels=storage->slots[0];pixels.view.width=4;pixels.view.height=3;pixels.view.pitch=16;pixels.view.end=1;for(unsigned i=0;i<48;++i)pixels.pixels[i]=static_cast<unsigned char>(i*17);storage->publish(0);auto frame=storage->acquire(storage,0);
 Live live;const d::CopyRequest request{binding,3,4,{1,p::Traversal::manager}};
 {NativeRefSpy refs(raw_surface,gate);SurfaceSpy spy(surface,gate,destination,live);const auto adds=refs.adds;const auto result=destination.try_copy(request,frame,{&live,Live::read});verify(result.kind==d::CopyKind::written,"real SYSTEMMEM copy acknowledged after release");verify(refs.adds==adds&&spy.releases==1&&spy.locks==spy.unlocks,"logical acquire adds no native ref and balances mapping");}
 D3DLOCKED_RECT mapping{};require(surface->LockRect(&mapping,nullptr,D3DLOCK_READONLY)==S_OK,"actual readback lock");for(unsigned y=0;y<3;++y)verify(!std::memcmp(static_cast<unsigned char*>(mapping.pBits)+y*mapping.Pitch,pixels.pixels.data()+y*16,16),"actual readback exact BGRA row");require(surface->UnlockRect()==S_OK,"readback unlock");
 for(unsigned stage=1;stage<=4;++stage)for(unsigned action=1;action<=3;++action){live.value={true,p::Continuation::live};destination.observe_record(binding,0,true);SurfaceSpy spy(surface,gate,destination,live);spy.stage=stage;spy.action=action;auto result=destination.try_copy(request,frame,{&live,Live::read});verify(result.kind==(action==1?d::CopyKind::unavailable:action==2?d::CopyKind::abort_traversal:d::CopyKind::superseded),"real COM reentry classification");verify(spy.locks==spy.unlocks&&spy.releases==1&&!gate.admission.depth(),"real COM cleanup balanced through final Release");}
 live.value={true,p::Continuation::live};destination.observe_record(binding,0,true);fail_reset=true;reset_requested=true;gate.present();verify(FAILED(last_reset),"actual native invalid-parameters Reset fails");verify(destination.try_copy(request,frame,{&live,Live::read}).kind==d::CopyKind::unavailable,"failed actual Reset blocks copy");
 fail_reset=false;reset_requested=true;gate.present();verify(SUCCEEDED(last_reset),"actual native Reset recovers after copy refs gone");verify(destination.try_copy(request,frame,{&live,Live::read}).kind==d::CopyKind::written,"actual generation recovery permits canonical surface copy");
 d::bind_reset_observer(nullptr);surface->Release();frame.release();verify(storage->all_free(),"canonical frame released");
 const auto abi_failures=destination_abi_checks();std::printf("MEDIA DESTINATION NATIVE checks=%u failures=%u abi_failures=%u native_resets=%u presents=%u\n",checks,failures,abi_failures,resets,presents);return failures||abi_failures?1:0;}
