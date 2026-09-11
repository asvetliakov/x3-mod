// Original synthetic resources only. Qualifier never locks or reads payload.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "managed_upload_contract.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <type_traits>
namespace upload = x3m::ownership::managed_upload;
template<class T> struct Com { T* p=nullptr; ~Com(){if(p)p->Release();} T* operator->()const{return p;} };
unsigned checks=0;
void check(bool result,const char* name){++checks;std::printf("CHECK %s %s\n",name,result?"PASS":"FAIL");if(!result)throw std::runtime_error(name);}
void okay(HRESULT hr,const char* name){check(hr==S_OK,name);}
struct Fp {DWORD env[7]{};DWORD mxcsr=0;};
Fp fp(){Fp state;asm volatile("fnstenv %0\n\tfldenv %0\n\tstmxcsr %1":"=m"(state.env),"=m"(state.mxcsr)::"memory");return state;}
bool same_fp(const Fp& a,const Fp& b){return !std::memcmp(a.env,b.env,sizeof a.env)&&a.mxcsr==b.mxcsr;}
template<class Fn> bool preserved(Fn fn){const auto before=fp();SetLastError(0x24681357);const bool result=fn();const DWORD error=GetLastError();const auto after=fp();
 if(!same_fp(before,after)){for(unsigned i=0;i<7;++i)std::printf("OBS env[%u] before=%08lx after=%08lx\n",i,before.env[i],after.env[i]);std::printf("OBS mxcsr before=%08lx after=%08lx\n",before.mxcsr,after.mxcsr);}
 check(error==0x24681357&&same_fp(before,after),"qualifier preserves x87 environment MXCSR LastError");return result;}
template<class Buffer> void create(IDirect3DDevice9* device,Com<Buffer>& buffer,DWORD usage=D3DUSAGE_WRITEONLY,D3DPOOL pool=D3DPOOL_MANAGED,D3DFORMAT format=D3DFMT_INDEX16){
 if constexpr(std::is_same_v<Buffer,IDirect3DVertexBuffer9>)okay(device->CreateVertexBuffer(256,usage,0,pool,&buffer.p,nullptr),"create original VB");
 else okay(device->CreateIndexBuffer(256,usage,format,pool,&buffer.p,nullptr),"create original IB");
}
struct Table {
 IUnknown* resource;void** original;void* slots[14];
 explicit Table(IUnknown* value):resource(value),original(*reinterpret_cast<void***>(value)){std::memcpy(slots,original,sizeof slots);*reinterpret_cast<void***>(resource)=slots;}
 ~Table(){*reinterpret_cast<void***>(resource)=original;}
};
HRESULT WINAPI failed_lock(IUnknown*,UINT,UINT,void** out,DWORD flags){if(out&&(flags&1))*out=nullptr;SetLastError(0x87654321);return E_FAIL;}
template<class Buffer> void nondefault_fp_case(Buffer* buffer){
 struct Restore {alignas(16) unsigned char state[512];Restore(){asm volatile("fxsave %0":"=m"(state)::"memory");}~Restore(){asm volatile("fxrstor %0"::"m"(state):"memory");}} restore;
 // Constant transfer only: no x87 arithmetic. Keep one live ST value while
 // changing precision/rounding and masked sticky flags in both FP environments.
 asm volatile("fld1" ::: "memory");
 auto changed=fp();changed.env[0]=(changed.env[0]&0xffff0000u)|0x077fu;changed.env[1]|=0x20u;
 changed.mxcsr=(changed.mxcsr&~0x6000u)|0x2000u|0x20u;
 asm volatile("fldenv %0\n\tldmxcsr %1"::"m"(changed.env),"m"(changed.mxcsr):"memory");
 upload::BufferContract token{};
 check(preserved([&]{return upload::inspect(buffer,&token);}),"nondefault FP environment qualifies unchanged");
 unsigned char value[10]{};asm volatile("fstpt %0":"=m"(value)::"memory");
 const unsigned char one[10]={0,0,0,0,0,0,0,0x80,0xff,0x3f};
 check(!std::memcmp(value,one,sizeof value),"live x87 value survives qualification");
}
template<class Buffer> void cases(IDirect3DDevice9* device,D3DFORMAT format=D3DFMT_INDEX16){
 std::printf("CASE buffer kind=%s format=%u\n",std::is_same_v<Buffer,IDirect3DVertexBuffer9>?"vertex":"index",unsigned(format));
 Com<Buffer> buffer;create(device,buffer,D3DUSAGE_WRITEONLY,D3DPOOL_MANAGED,format);
 upload::BufferContract token{};
 // An unverified module candidate must not poison later valid qualification.
 {Table table(buffer.p);auto bad=&failed_lock;std::memcpy(&table.slots[12],&bad,sizeof bad);check(!preserved([&]{return upload::inspect(buffer.p,&token);}),"foreign module endpoint rejects");}
 check(preserved([&]{return upload::inspect(buffer.p,&token);}),"native managed WRITEONLY qualifies");
 check(token.generation&&token.size==256&&token.borrowed_native==buffer.p&&token.heap_data&&token.backend_resource,"contract actual allocation metadata");
 nondefault_fp_case(buffer.p);
 const auto retained=buffer->AddRef();buffer->Release();upload::BufferContract repeated{};
 check(upload::inspect(buffer.p,&repeated)&&repeated.generation==token.generation,"stable contract generation");
 check(buffer->AddRef()==retained,"qualifier adds no COM reference");buffer->Release();
 check(upload::validate_closed(token),"new allocation closed");
 check(!upload::inspect(buffer.p,nullptr),"null output rejects");
 check(!upload::inspect(static_cast<Buffer*>(nullptr),&repeated)&&!repeated.generation,"null input clears output");
 check(!upload::inspect(reinterpret_cast<Buffer*>(0x1234),&repeated),"unreadable input rejects");
 for(unsigned slot:{4u,5u,6u,11u,12u,13u}){Table table(buffer.p);table.slots[slot]=nullptr;check(!preserved([&]{return upload::inspect(buffer.p,&repeated);}),"replaced native endpoint rejects");}
 for(auto field:{0u,1u,2u,3u,4u,5u}){
   auto stale=token;
   switch(field){case 0:stale.generation++;break;case 1:stale.size--;break;case 2:stale.heap_data=static_cast<const char*>(stale.heap_data)+4;break;
    case 3:stale.backend_resource=static_cast<const char*>(stale.backend_resource)+4;break;case 4:stale.format=D3DFMT_UNKNOWN;break;default:stale.type=D3DRTYPE_SURFACE;}
   check(!upload::validate_closed(stale),"altered token rejects");
 }
 Com<Buffer> other;create(device,other,D3DUSAGE_WRITEONLY,D3DPOOL_MANAGED,format);
 auto stale=token;stale.borrowed_native=other.p;check(!upload::validate_closed(stale),"different live allocation cannot refresh token");
 for(DWORD flags:{DWORD(0),DWORD(D3DLOCK_NOSYSLOCK)})for(bool partial:{false,true}){
   const UINT offset=partial?16:0,size=partial?32:0;void* data=nullptr;
   const HRESULT hr=buffer->Lock(offset,size,&data,flags);okay(hr,"original application write Lock");
   upload::Window window{};
   check(preserved([&]{return upload::validate_window(token,offset,size,flags,data,&window);}),"live successful write window qualifies");
   check(window.offset==offset&&window.size==(partial?32u:256u)&&window.generation==token.generation,"window normalized without changing application args");
   check(!upload::validate_closed(token),"locked allocation not closed");
   check(!upload::validate_window(token,offset,size,flags,static_cast<char*>(data)+1,&window)&&!window.generation,"wrong mapping pointer rejects and clears window");
   check(!upload::validate_window(token,offset,size,flags,nullptr,&window),"null mapping pointer rejects");
   check(!upload::validate_window(token,offset,size,flags,data,nullptr),"null window output rejects");
   for(DWORD unsupported:{DWORD(D3DLOCK_READONLY),DWORD(D3DLOCK_DISCARD),DWORD(D3DLOCK_NOOVERWRITE),DWORD(0xffffffff)})
     check(!upload::validate_window(token,offset,size,unsupported,data,&window),"unsupported observed flags reject");
   check(!upload::validate_window(token,16,0,flags,data,&window),"nonzero offset zero size rejects");
   check(!upload::validate_window(token,257,1,flags,data,&window),"offset past allocation rejects");
   check(!upload::validate_window(token,16,241,flags,data,&window),"range past allocation rejects");
   check(!upload::validate_window(token,0xfffffff0,32,flags,data,&window),"wrapping range rejects");
   std::memset(data,0x5a,partial?32:256); // The fixture is the original application writer.
   asm volatile("mfence" ::: "memory");
   check(upload::validate_window(token,offset,size,flags,data,&window),"qualification remains valid after application writes");
   okay(buffer->Unlock(),"original application Unlock");
   check(preserved([&]{return upload::validate_closed(token);}),"matching native Unlock closes mapping");
   check(!upload::validate_window(token,offset,size,flags,data,&window),"retired mapping cannot qualify after Unlock");
 }
 void* first=nullptr;void* second=nullptr;
 okay(buffer->Lock(0,32,&first,0),"outer original Lock");
 const HRESULT nested=buffer->Lock(32,32,&second,0);std::printf("OBS nested type=%u hr=%08lx\n",unsigned(token.type),nested);
 if(SUCCEEDED(nested)){upload::Window window{};check(!upload::validate_window(token,0,32,0,first,&window),"two native maps reject");okay(buffer->Unlock(),"nested original Unlock");}
 okay(buffer->Unlock(),"outer original Unlock");check(upload::validate_closed(token),"nested control cleanup closes native maps");
 {Table table(buffer.p);auto fail=&failed_lock;std::memcpy(&table.slots[11],&fail,sizeof fail);
  for(DWORD flags:{DWORD(0),DWORD(1)}){void* data=reinterpret_cast<void*>(0x11223344);const HRESULT hr=buffer->Lock(0,0,&data,flags);
   check(hr==E_FAIL&&GetLastError()==0x87654321&&(flags?data==nullptr:data==reinterpret_cast<void*>(0x11223344)),"injected failed original Lock HRESULT output LastError unchanged");
   upload::Window window{};check(!upload::validate_window(token,0,0,0,data,&window),"failed replaced Lock supplies no qualified window");}}
 // GetDesc/Lock/Unlock all depend on these live import bindings, including after
 // the module identity has been cached. Restore before any original COM call.
 auto table=*reinterpret_cast<void***>(buffer.p);HMODULE module=nullptr;
 check(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(table[12]),&module)!=FALSE,"resolve actual native module");
 for(unsigned rva:{0x23460u,0x235b8u,0x235c4u,0x235acu,0x23560u,0x23564u}){
  auto slot=reinterpret_cast<void**>(reinterpret_cast<char*>(module)+rva);DWORD protection=0;
  check(VirtualProtect(slot,sizeof *slot,PAGE_READWRITE,&protection)!=FALSE,"import fault page writable");void* prior=*slot;*slot=nullptr;
  const bool rejected=!upload::inspect(buffer.p,&repeated);*slot=prior;DWORD ignored=0;const bool restored=VirtualProtect(slot,sizeof *slot,protection,&ignored)!=FALSE;
  check(rejected,"replaced import rejects cached contract");check(restored,"import fault page protection restored");
 }
 check(upload::inspect(buffer.p,&repeated)&&upload::validate_closed(token),"restored endpoint and import contract recovers");
 for(bool badUsage:{false,true}){Com<Buffer> unsupported;create(device,unsupported,badUsage?0:D3DUSAGE_WRITEONLY,badUsage?D3DPOOL_MANAGED:D3DPOOL_SYSTEMMEM,format);
  check(!upload::inspect(unsupported.p,&repeated),"unsupported actual pool or usage rejects");}
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);HMODULE dll=LoadLibraryA("d3d9.dll");WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="X3ManagedUploadContract";RegisterClassA(&wc);
 HWND window=CreateWindowA(wc.lpszClassName,"X3 managed upload qualification",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);int result=1;
 try {check(dll&&window,"hidden original fixture ready");using Create=IDirect3D9*(WINAPI*)(UINT);auto address=GetProcAddress(dll,"Direct3DCreate9");Create createApi=nullptr;std::memcpy(&createApi,&address,sizeof createApi);check(createApi!=nullptr,"native D3D9 export");
  Com<IDirect3D9> api;api.p=createApi(D3D_SDK_VERSION);check(api.p!=nullptr,"native factory");Com<IDirect3DDevice9> device;D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
  okay(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device.p),"native device");
  cases<IDirect3DVertexBuffer9>(device.p);cases<IDirect3DIndexBuffer9>(device.p,D3DFMT_INDEX16);cases<IDirect3DIndexBuffer9>(device.p,D3DFMT_INDEX32);
  okay(device->Reset(&pp),"resources cleaned before Reset");std::printf("RESULT PASS checks=%u\n",checks);result=0;
 }catch(const std::exception& error){std::printf("RESULT FAIL %s checks=%u\n",error.what(),checks);}
 if(window)DestroyWindow(window);
 UnregisterClassA(wc.lpszClassName,wc.hInstance);
 if(dll)FreeLibrary(dll);
 return result;
}
