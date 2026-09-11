// Original normal-D3D9 wrappers with per-instance native spies. No proxy/game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/ownership/d3d9_ownership.h"
#include "../../src/ownership/application_admission_abi.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
using namespace x3m::ownership;
namespace {
unsigned checks=0,failures=0;
void check(bool value,const char* label){++checks;if(!value)++failures;std::printf("CHECK %s %s\n",label,value?"PASS":"FAIL");}
void require(HRESULT hr,const char* label){if(FAILED(hr))throw std::runtime_error(label);}
struct State {unsigned char x87[108];unsigned mxcsr;DWORD error;
 State(){asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1":"=m"(x87),"=m"(mxcsr)::"memory");error=GetLastError();}
 void restore()const{SetLastError(error);asm volatile("frstor %0\n\tldmxcsr %1"::"m"(x87),"m"(mxcsr):"memory");}};
bool same(const State&a,const State&b){return a.error==b.error&&a.mxcsr==b.mxcsr&&!std::memcmp(a.x87,b.x87,108);}
void seed(bool outgoing=false){unsigned short cw=outgoing?0x0b7f:0x077f;unsigned mx=outgoing?0x5fa0:0x3fa1;
 asm volatile("fninit\n\tfldz\n\tfldz\n\tfdivp\n\tfld1\n\tfldpi\n\tfldcw %0\n\tldmxcsr %1"::"m"(cw),"m"(mx):"memory");SetLastError(outgoing?0x55667788:0x11223344);}
struct Patch {void* object;void** saved;std::array<void*,119> table{};
 Patch(void*o,unsigned count,unsigned slot,void*replacement):object(o),saved(*reinterpret_cast<void***>(o)){std::memcpy(table.data(),saved,count*sizeof(void*));table[slot]=replacement;*reinterpret_cast<void***>(o)=table.data();}
 void restore(){if(object){*reinterpret_cast<void***>(object)=saved;object=nullptr;}}
 ~Patch(){restore();}};
struct Setup {
 HMODULE module=nullptr;HWND window=nullptr;IDirect3D9 *native_factory=nullptr,*factory=nullptr;IDirect3DDevice9 *app=nullptr,*native=nullptr;
 Setup(){module=LoadLibraryA("d3d9.dll");if(!module)throw std::runtime_error("load d3d9");
  IDirect3D9*(WINAPI*create)(UINT)=nullptr;auto symbol=GetProcAddress(module,"Direct3DCreate9");std::memcpy(&create,&symbol,sizeof(create));if(!create)throw std::runtime_error("export");
  native_factory=create(D3D_SDK_VERSION);if(!native_factory)throw std::runtime_error("native factory");require(wrap_factory(native_factory,&factory),"wrap");
  window=CreateWindowExA(0,"STATIC","Ownership admission fixture",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);if(!window)throw std::runtime_error("window");
  D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;
  require(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&app),"device");native=borrowed_native_device(app);if(!native)throw std::runtime_error("native device");
 }
 ~Setup(){if(app)app->Release();if(factory)factory->Release();if(window)DestroyWindow(window);if(module)FreeLibrary(module);}
};
AdmissionSnapshot snap(){return admission_snapshot(process_admission_monitor());}
bool veto(AdmissionVeto value){return (snap().vetoes&static_cast<unsigned>(value))!=0;}
State native_in,native_out;std::atomic<unsigned> native_calls{0};bool native_ticket=false;
UINT WINAPI memory_spy(IDirect3DDevice9*){native_in=State{};native_ticket=snap().active_roots!=0;++native_calls;seed(true);native_out=State{};return 0x1234abcd;}
UINT WINAPI memory_fast(IDirect3DDevice9*){return 0x1234abcd;}
void cpu(Setup&s,bool enabled){Patch p(s.native,119,4,reinterpret_cast<void*>(&memory_spy));State original;seed();State incoming;const auto value=s.app->GetAvailableTextureMem();State outgoing;original.restore();
 check(value==0x1234abcd,"native scalar result unchanged");check(same(incoming,native_in),"native receives complete incoming CPU state");check(same(outgoing,native_out),"wrapper returns complete native outgoing CPU state");check(native_ticket==enabled,"native dispatch observes configured admission");check(snap().active_roots==0,"ordinary wrapper ticket retires");}
std::atomic<bool> entered{false},resume{false};
UINT WINAPI blocking_memory(IDirect3DDevice9*){++native_calls;entered=true;while(!resume.load())std::this_thread::yield();return 0x4567;}
template<class Predicate>bool until(Predicate predicate){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()){if(std::chrono::steady_clock::now()>end)return false;std::this_thread::yield();}return true;}
void blocked(Setup&s){native_calls=0;entered=false;resume=false;Patch p(s.native,119,4,reinterpret_cast<void*>(&blocking_memory));ApplicationAdmissionAbi root(process_admission_monitor());ReplayAdmissionAbi replay(root);check(replay.admitted(),"fixture sole root promotes");unsigned result=0;std::thread worker([&]{result=s.app->GetAvailableTextureMem();});
 check(until([]{return snap().waiting_roots==1;}),"actual wrapper waits before native dispatch");check(native_calls==0&&!entered,"waiting wrapper did not dispatch");check(replay.finish(),"replay finishes to admit waiting wrapper");check(root.finish(),"fixture root finishes");check(until([]{return entered.load();}),"waiting wrapper reaches original native after replay");check(snap().active_roots==1,"native operation retains wrapper admission");resume=true;worker.join();check(result==0x4567&&native_calls==1,"waiting native dispatch executes exactly once");check(!snap().active_roots&&!snap().waiting_roots,"waiting wrapper retires after native completion");}
using QIFn=HRESULT(WINAPI*)(IUnknown*,REFIID,void**);QIFn original_qi=nullptr;std::atomic<bool> publication_entered{false},publication_resume{false};Patch* publication_patch=nullptr;
HRESULT WINAPI publication_qi(IUnknown*object,REFIID iid,void**out){publication_entered=true;while(!publication_resume.load())std::this_thread::yield();publication_patch->restore();return original_qi(object,iid,out);}
using CreateVB=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*);CreateVB original_create=nullptr;
HRESULT WINAPI publication_create(IDirect3DDevice9*d,UINT n,DWORD u,DWORD f,D3DPOOL p,IDirect3DVertexBuffer9**out,HANDLE*h){const auto hr=original_create(d,n,u,f,p,out,h);if(SUCCEEDED(hr)&&out&&*out){original_qi=reinterpret_cast<QIFn>((*reinterpret_cast<void***>(*out))[0]);publication_patch=new Patch(*out,14,0,reinterpret_cast<void*>(&publication_qi));}return hr;}
void publication(Setup&s){publication_entered=false;publication_resume=false;original_create=reinterpret_cast<CreateVB>((*reinterpret_cast<void***>(s.native))[26]);Patch patch(s.native,119,26,reinterpret_cast<void*>(&publication_create));IDirect3DVertexBuffer9* output=nullptr;HRESULT result=E_UNEXPECTED;
 std::thread worker([&]{result=s.app->CreateVertexBuffer(64,0,0,D3DPOOL_MANAGED,&output,nullptr);});check(until([]{return publication_entered.load();}),"native output entered actual wrapper adoption");check(snap().active_roots==1,"admission spans native output adoption");check(output==nullptr,"application output not published during adoption");publication_resume=true;worker.join();delete publication_patch;publication_patch=nullptr;
 check(result==S_OK&&output,"wrapped output published after completed adoption");check(!snap().active_roots,"output publication finishes before ticket retirement");if(output)output->Release();}
using ReleaseFn=ULONG(WINAPI*)(IUnknown*);ReleaseFn original_child_release=nullptr,original_parent_release=nullptr;Patch *child_patch=nullptr,*parent_patch=nullptr;bool child_active=false,parent_idle=false;unsigned parent_calls=0;
ULONG WINAPI child_release(IUnknown*o){child_active=snap().active_roots==1;child_patch->restore();return original_child_release(o);}
ULONG WINAPI parent_release(IUnknown*o){parent_idle=snap().active_roots==0;++parent_calls;return original_parent_release(o);}
void handoff(Setup&s){IDirect3DVertexBuffer9*vb=nullptr;require(s.app->CreateVertexBuffer(64,0,0,D3DPOOL_MANAGED,&vb,nullptr),"handoff VB");auto native=borrowed_native_buffer_for_lock_contract(vb);if(!native)throw std::runtime_error("handoff native");original_child_release=reinterpret_cast<ReleaseFn>((*reinterpret_cast<void***>(native))[2]);original_parent_release=reinterpret_cast<ReleaseFn>((*reinterpret_cast<void***>(s.app))[2]);
 Patch child(native,14,2,reinterpret_cast<void*>(&child_release)),parent(s.app,119,2,reinterpret_cast<void*>(&parent_release));child_patch=&child;parent_patch=&parent;const auto before=snap().admitted_roots;check(vb->Release()==0,"final child wrapper release returns zero");
 check(child_active,"child native destruction retains child ticket");check(parent_calls==1&&parent_idle,"parent application release begins after child ticket finishes");check(snap().admitted_roots==before+2,"parent release receives fresh ordinary root");check(!snap().active_roots&&!snap().vetoes,"release handoff remains balanced and unvetoed");child_patch=nullptr;parent_patch=nullptr;}
void timing(Setup&s){Patch p(s.native,119,4,reinterpret_cast<void*>(&memory_fast));LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);for(unsigned trial=0;trial<8;++trial){const unsigned iterations=trial?100000:10000;unsigned value=0;LARGE_INTEGER before,after;QueryPerformanceCounter(&before);for(unsigned i=0;i<iterations;++i)value+=s.app->GetAvailableTextureMem();QueryPerformanceCounter(&after);check(value==iterations*0x1234abcdu,"timed native result inventory");if(trial)std::printf("SAMPLE trial=%u iterations=%u ns_per_call=%.3f\n",trial,iterations,1e9*double(after.QuadPart-before.QuadPart)/double(frequency.QuadPart)/iterations);}}
struct Callback final:IUnknown {IDirect3DDevice9* device;unsigned refs=1,calls=0;bool nested=false;explicit Callback(IDirect3DDevice9*d):device(d){}
 HRESULT WINAPI QueryInterface(REFIID,void**out)override{if(out)*out=nullptr;return E_NOINTERFACE;}
 ULONG WINAPI AddRef()override{++calls;const auto before=snap().admitted_roots;device->GetAvailableTextureMem();nested=snap().active_roots==1&&snap().admitted_roots==before;return ++refs;}
 ULONG WINAPI Release()override{return --refs;}};
// Deliberately use our finite metadata GUID: public callers never receive an
// exemption based on GUID identity, even when native registration fails.
const GUID spoof={0x03ee519d,0x9308,0x4a86,{0x9b,0x94,0xd3,0x7e,0x35,0xac,0x49,0xaa}};
bool private_pre=false,private_flags=false;unsigned private_calls=0;
HRESULT WINAPI private_spy(IUnknown*,REFGUID guid,const void*data,DWORD size,DWORD flags){native_in=State{};++private_calls;private_pre=veto(AdmissionVeto::PrivateUnknown);private_flags=guid==spoof&&size==sizeof(IUnknown*)&&flags==D3DSPD_IUNKNOWN;
 auto callback=static_cast<IUnknown*>(const_cast<void*>(data));callback->AddRef();callback->Release();seed(true);native_out=State{};return E_ACCESSDENIED;}
struct Resource {IUnknown*app=nullptr,*native=nullptr;unsigned count=0;IDirect3DVolumeTexture9* volume_owner=nullptr;~Resource(){if(app)app->Release();if(volume_owner)volume_owner->Release();}};
void resource(Setup&s,unsigned kind,Resource&r){
 if(kind==0||kind==1||kind==2||kind==4){IDirect3DBaseTexture9* nt=nullptr;
  if(kind==0)require(s.native->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,reinterpret_cast<IDirect3DTexture9**>(&nt),nullptr),"native texture");
  if(kind==1)require(s.native->CreateCubeTexture(4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,reinterpret_cast<IDirect3DCubeTexture9**>(&nt),nullptr),"native cube");
  if(kind==2||kind==4)require(s.native->CreateVolumeTexture(4,4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,reinterpret_cast<IDirect3DVolumeTexture9**>(&nt),nullptr),"native volume texture");
  require(s.native->SetTexture(0,nt),"native texture binding");IDirect3DBaseTexture9*at=nullptr;require(s.app->GetTexture(0,&at),"wrapped texture adoption");
  r.app=at;r.native=nt;r.count=22;
  if(kind==4){r.volume_owner=static_cast<IDirect3DVolumeTexture9*>(at);IDirect3DVolume9 *av=nullptr,*nv=nullptr;require(r.volume_owner->GetVolumeLevel(0,&av),"wrapped volume");require(static_cast<IDirect3DVolumeTexture9*>(nt)->GetVolumeLevel(0,&nv),"native volume");r.app=av;r.native=nv;r.count=11;nv->Release();}
  nt->Release();
 }else if(kind==3){IDirect3DSurface9 *a=nullptr,*n=nullptr;require(s.app->GetRenderTarget(0,&a),"wrapped surface");require(s.native->GetRenderTarget(0,&n),"native surface");r.app=a;r.native=n;r.count=17;n->Release();
 }else if(kind==5){IDirect3DVertexBuffer9*a=nullptr;require(s.app->CreateVertexBuffer(64,0,0,D3DPOOL_MANAGED,&a,nullptr),"VB");r.app=a;r.native=borrowed_native_buffer_for_lock_contract(a);r.count=14;
 }else if(kind==6){IDirect3DIndexBuffer9*a=nullptr;require(s.app->CreateIndexBuffer(64,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&a,nullptr),"IB");r.app=a;r.native=borrowed_native_buffer_for_lock_contract(a);r.count=14;
 }if(!r.app||!r.native)throw std::runtime_error("resource route");}
void private_case(Setup&s,unsigned kind){Resource r;resource(s,kind,r);check(!snap().vetoes,"resource setup has no application private-IUnknown veto");Callback callback(s.app);Patch p(r.native,r.count,4,reinterpret_cast<void*>(&private_spy));auto call=reinterpret_cast<HRESULT(WINAPI*)(IUnknown*,REFGUID,const void*,DWORD,DWORD)>((*reinterpret_cast<void***>(r.app))[4]);State original;seed();State before;const auto hr=call(r.app,spoof,&callback,sizeof(IUnknown*),D3DSPD_IUNKNOWN);State after;original.restore();
 check(hr==E_ACCESSDENIED&&private_calls==1,"failed private registration reaches native once unchanged");check(private_pre&&private_flags,"private-IUnknown veto precedes failed native dispatch and GUID spoof");check(callback.calls==1&&callback.refs==1&&callback.nested,"native callback ordinary reentry remains nested and balanced");check(same(before,native_in),"private registration native incoming CPU state unchanged");check(same(after,native_out),"private registration native outgoing CPU state unchanged");check(veto(AdmissionVeto::PrivateUnknown)&&!snap().active_roots,"failed registration permanently vetoes after return");
 p.restore();const unsigned prior_callbacks=callback.calls;
 check(call(r.app,spoof,&callback,sizeof(IUnknown*),D3DSPD_IUNKNOWN)==S_OK,"actual native private registration succeeds unchanged");
 check(callback.calls==prior_callbacks+1&&callback.refs==2&&callback.nested,"actual native callback retention remains nested and balanced");
 auto free_data=reinterpret_cast<HRESULT(WINAPI*)(IUnknown*,REFGUID)>((*reinterpret_cast<void***>(r.app))[6]);
 check(free_data(r.app,spoof)==S_OK,"actual native private registration cleanup succeeds");
 check(callback.refs==1&&veto(AdmissionVeto::PrivateUnknown),"actual native cleanup balances callback without clearing veto");}
HANDLE* observed_shared=nullptr;bool route_pre=false;unsigned route_calls=0;
HRESULT WINAPI shared_spy(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**out,HANDLE*h){++route_calls;observed_shared=h;route_pre=veto(AdmissionVeto::ExternalResource);if(out)*out=nullptr;return E_ACCESSDENIED;}
HRESULT WINAPI foreign_spy(IDirect3DDevice9*,UINT,IDirect3DVertexBuffer9*buffer,UINT,UINT){++route_calls;route_pre=veto(AdmissionVeto::UnobservedRoute);return buffer?E_ACCESSDENIED:E_POINTER;}
HRESULT WINAPI software_spy(IDirect3D9*,void*callback){++route_calls;route_pre=veto(AdmissionVeto::UnobservedRoute);return callback?E_ACCESSDENIED:E_POINTER;}
void route(Setup&s,const char*mode){check(!snap().vetoes,"route starts without veto");if(!std::strcmp(mode,"shared")){Patch p(s.native,119,26,reinterpret_cast<void*>(&shared_spy));HANDLE handle=nullptr;IDirect3DVertexBuffer9*out=nullptr;check(s.app->CreateVertexBuffer(64,0,0,D3DPOOL_MANAGED,&out,&handle)==E_ACCESSDENIED,"shared failed native HRESULT unchanged");check(observed_shared==&handle&&!out,"nonnull shared request forwarded exactly");}
 else if(!std::strcmp(mode,"foreign")){IDirect3DVertexBuffer9*n=nullptr;require(s.native->CreateVertexBuffer(64,0,0,D3DPOOL_MANAGED,&n,nullptr),"foreign buffer");Patch p(s.native,119,100,reinterpret_cast<void*>(&foreign_spy));check(s.app->SetStreamSource(0,n,0,16)==E_ACCESSDENIED,"foreign input reaches original native validation");n->Release();}
 else{Patch p(s.native_factory,17,3,reinterpret_cast<void*>(&software_spy));check(s.factory->RegisterSoftwareDevice(reinterpret_cast<void*>(0x1234))==E_ACCESSDENIED,"software callback failed native HRESULT unchanged");}
 check(route_calls==1&&route_pre,"unobserved route veto precedes exactly one native dispatch");check(!snap().active_roots&&snap().vetoes,"route veto persists after ticket retirement");}
}
int main(int argc,char**argv){try{if(argc!=2)throw std::runtime_error("mode");const bool enabled=std::strcmp(argv[1],"disabled")!=0;
 check((process_admission_monitor()!=nullptr)==enabled,"process monitor matches explicit configuration");Setup setup;
 if(!std::strcmp(argv[1],"enabled")||!enabled){cpu(setup,enabled);if(enabled){blocked(setup);publication(setup);handoff(setup);}timing(setup);}
 else if(!std::strncmp(argv[1],"private",7)){unsigned kind=unsigned(argv[1][7]-'0');if(kind>6)throw std::runtime_error("private kind");private_case(setup,kind);}
 else route(setup,argv[1]);
 std::printf("RESULT %s mode=%s checks=%u failures=%u\n",failures?"FAIL":"PASS",argv[1],checks,failures);return failures?1:0;
 }catch(const std::exception&e){std::printf("EXCEPTION %s\nRESULT FAIL checks=%u failures=%u\n",e.what(),checks,failures+1);return 2;}}
