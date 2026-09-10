// Standalone diagnostic-write tracking fixture. No game, payload capture or install.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/ownership/d3d9_ownership.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <type_traits>
#include <algorithm>
using namespace x3m::ownership;
template<class T> struct Com { T* p=nullptr; ~Com(){reset();} void reset(){if(p)p->Release();p=nullptr;} T* operator->()const{return p;} };
unsigned checks=0;
void require(bool value,const char* name){++checks;std::printf("CHECK %s %s\n",name,value?"PASS":"FAIL");if(!value)throw std::runtime_error(name);}
void okay(HRESULT hr,const char* name){require(SUCCEEDED(hr),name);}
using Create=IDirect3D9*(WINAPI*)(UINT);
struct Session {
    Com<IDirect3D9> api;Com<IDirect3DDevice9> device;D3DPRESENT_PARAMETERS pp{};
    Session(Create create,HWND window,bool wrapped,bool tracking,bool pure=false){
        api.p=create(D3D_SDK_VERSION);require(api.p!=nullptr,"native factory");
        if(wrapped){auto native=api.p;api.p=nullptr;Options options;options.track_buffer_writes=tracking;okay(wrap_factory(native,&api.p,options),"wrap factory");}
        pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        okay(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|(pure?D3DCREATE_PUREDEVICE:0),&pp,&device.p),"device");
    }
    IDirect3DDevice9* native(){auto raw=borrowed_native_device(device.p);return raw?raw:device.p;}
};
template<class T> void createBuffer(Session& s,Com<T>& buffer,bool dynamic=false){
    const DWORD usage=dynamic?D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY:0;
    const auto pool=dynamic?D3DPOOL_DEFAULT:D3DPOOL_MANAGED;
    if constexpr(std::is_same_v<T,IDirect3DVertexBuffer9>)okay(s.device->CreateVertexBuffer(256,usage,D3DFVF_XYZ,pool,&buffer.p,nullptr),"create VB");
    else okay(s.device->CreateIndexBuffer(256,usage,D3DFMT_INDEX16,pool,&buffer.p,nullptr),"create IB");
}
BufferContentView view(IDirect3DResource9* resource){BufferContentView result;okay(get_buffer_content_view(resource,&result),"content view");return result;}
template<class T> void bind(Session& s,T* buffer){
    if constexpr(std::is_same_v<T,IDirect3DVertexBuffer9>)okay(s.device->SetStreamSource(0,buffer,0,12),"bind VB");
    else okay(s.device->SetIndices(buffer),"bind IB");
}
template<class T> void get(Session& s,Com<T>& buffer,bool native=false){
    if constexpr(std::is_same_v<T,IDirect3DVertexBuffer9>){UINT offset=0,stride=0;okay((native?s.native():s.device.p)->GetStreamSource(0,&buffer.p,&offset,&stride),"get VB");}
    else okay((native?s.native():s.device.p)->GetIndices(&buffer.p),"get IB");
}
struct Fault {
    using Lock=HRESULT(WINAPI*)(IUnknown*,UINT,UINT,void**,DWORD);
    using Unlock=HRESULT(WINAPI*)(IUnknown*);
    using PrivateGet=HRESULT(WINAPI*)(IUnknown*,REFGUID,void*,DWORD*);
    using PrivateSet=HRESULT(WINAPI*)(IUnknown*,REFGUID,const void*,DWORD,DWORD);
    static inline Lock originalLock=nullptr;static inline Unlock originalUnlock=nullptr;
    static inline PrivateGet originalGet=nullptr;static inline PrivateSet originalSet=nullptr;
    static inline bool failLock=false,clearOutput=false,failUnlock=false,failGet=false,failSet=false;
    static inline UINT seenOffset=0,seenSize=0;static inline DWORD seenFlags=0;static inline unsigned gets=0,sets=0;
    void** previous;void* table[14];IUnknown* resource;
    static HRESULT WINAPI lock(IUnknown* r,UINT o,UINT n,void** p,DWORD f){seenOffset=o;seenSize=n;seenFlags=f;if(failLock){if(clearOutput&&p)*p=nullptr;return E_FAIL;}return originalLock(r,o,n,p,f);}
    static HRESULT WINAPI unlock(IUnknown* r){return failUnlock?E_FAIL:originalUnlock(r);}
    static HRESULT WINAPI get(IUnknown* r,REFGUID g,void* p,DWORD* n){++gets;return failGet?E_FAIL:originalGet(r,g,p,n);}
    static HRESULT WINAPI set(IUnknown* r,REFGUID g,const void* p,DWORD n,DWORD f){++sets;return failSet?E_OUTOFMEMORY:originalSet(r,g,p,n,f);}
    template<class Fn>void hook(unsigned index,Fn replacement,Fn& original){std::memcpy(&original,&table[index],sizeof original);std::memcpy(&table[index],&replacement,sizeof replacement);}
    explicit Fault(IUnknown* r):previous(*reinterpret_cast<void***>(r)),resource(r){std::copy(previous,previous+14,table);hook(11,&lock,originalLock);hook(12,&unlock,originalUnlock);hook(5,&get,originalGet);hook(4,&set,originalSet);failLock=clearOutput=failUnlock=failGet=failSet=false;gets=sets=0;*reinterpret_cast<void***>(resource)=table;}
    ~Fault(){*reinterpret_cast<void***>(resource)=previous;}
};
struct DeviceFault {
    using Process=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,IDirect3DVertexBuffer9*,IDirect3DVertexDeclaration9*,DWORD);
    using CreateVB=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*);
    static inline Process process=nullptr;static inline CreateVB createVB=nullptr;static inline Fault* resourceFault=nullptr;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI failProcess(IDirect3DDevice9*,UINT,UINT,UINT,IDirect3DVertexBuffer9*,IDirect3DVertexDeclaration9*,DWORD){return E_FAIL;}
    static HRESULT WINAPI create(IDirect3DDevice9* d,UINT n,DWORD u,DWORD f,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* shared){HRESULT hr=createVB(d,n,u,f,pool,out,shared);if(SUCCEEDED(hr)&&out&&*out){resourceFault=new Fault(*out);Fault::failSet=true;}return hr;}
    DeviceFault(IDirect3DDevice9* d,bool creation):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);if(creation){std::memcpy(&createVB,&table[26],sizeof createVB);auto hook=&create;std::memcpy(&table[26],&hook,sizeof hook);}else{std::memcpy(&process,&table[85],sizeof process);auto hook=&failProcess;std::memcpy(&table[85],&hook,sizeof hook);}*reinterpret_cast<void***>(d)=table;}
    ~DeviceFault(){delete resourceFault;resourceFault=nullptr;*reinterpret_cast<void***>(device)=previous;}
};
struct Outcome {HRESULT hr;unsigned output;bool operator==(const Outcome& other)const{return hr==other.hr&&output==other.output;}};
template<class T>std::vector<Outcome> parity(Session& s,bool tracking){
    Com<T> buffer,raw;createBuffer(s,buffer);bind(s,buffer.p);get(s,raw,true);std::vector<Outcome> results;
    {Fault fault(raw.p);for(bool clear:{false,true}){Fault::failLock=true;Fault::clearOutput=clear;void* sentinel=reinterpret_cast<void*>(0x11223344);void* data=sentinel;HRESULT hr=buffer->Lock(12,24,&data,D3DLOCK_READONLY);results.push_back({hr,data==sentinel?0u:!data?1u:2u});require(Fault::seenOffset==12&&Fault::seenSize==24&&Fault::seenFlags==D3DLOCK_READONLY,"Lock arguments preserved");if(tracking)require(view(buffer.p).revision==0&&view(buffer.p).known,"failed Lock leaves revision");}
        Fault::failLock=false;void* data=nullptr;okay(buffer->Lock(0,16,&data,0),"parity writable Lock");okay(buffer->Unlock(),"parity Unlock");if(!tracking)require(!Fault::gets&&!Fault::sets,"tracking off performs no private calls");}
    bind<T>(s,nullptr);return results;
}
template<class T>void bufferCases(Session& s){
    Com<T> buffer;createBuffer(s,buffer);auto first=view(buffer.p);require(first.requested&&first.known&&first.revision==0&&!first.pending_locks,"created buffer known zero");
    void* data=nullptr;okay(buffer->Lock(0,16,&data,D3DLOCK_READONLY),"readonly Lock");auto pending=view(buffer.p);require(!pending.known&&pending.pending_locks==1&&pending.revision==0,"readonly pending no revision");okay(buffer->Unlock(),"readonly Unlock");require(view(buffer.p).known&&view(buffer.p).revision==0,"readonly closed unchanged");
    okay(buffer->Lock(8,16,&data,0),"writable Lock");pending=view(buffer.p);require(!pending.known&&pending.pending_locks==1&&pending.revision==1,"writable advances before return");okay(buffer->Unlock(),"writable Unlock");require(view(buffer.p).known&&view(buffer.p).revision==1,"write closed known");
    bind(s,buffer.p);buffer.reset();get(s,buffer);require(view(buffer.p).known&&view(buffer.p).revision==1,"revision survives wrapper recreation");
    okay(buffer->Lock(0,16,&data,0),"Lock before wrapper recreation");buffer.reset();get(s,buffer);pending=view(buffer.p);require(!pending.known&&pending.pending_locks==1&&pending.revision==2,"pending write survives wrapper recreation");okay(buffer->Unlock(),"Unlock after wrapper recreation");require(view(buffer.p).known&&view(buffer.p).revision==2,"recreated pending write closes");
    okay(buffer->Lock(0,16,&data,0),"outer Lock");void* nested=nullptr;const HRESULT nestedResult=buffer->Lock(16,16,&nested,D3DLOCK_READONLY);std::printf("OBS nested=%08lx\n",nestedResult);
    if(SUCCEEDED(nestedResult)){require(!view(buffer.p).known&&view(buffer.p).ambiguous&&view(buffer.p).pending_locks==2,"nested Lock explicitly ambiguous");okay(buffer->Unlock(),"nested Unlock");}
    okay(buffer->Unlock(),"outer Unlock");if(SUCCEEDED(nestedResult))require(view(buffer.p).ambiguous&&!view(buffer.p).known&&!view(buffer.p).pending_locks,"nested uncertainty stays sticky");
    bind<T>(s,nullptr);buffer.reset();createBuffer(s,buffer,true);
    for(DWORD flags:{DWORD(D3DLOCK_DISCARD),DWORD(D3DLOCK_NOOVERWRITE)}){auto before=view(buffer.p).revision;okay(buffer->Lock(0,16,&data,flags),"dynamic Lock");auto during=view(buffer.p);require(during.revision==before+1&&during.last_lock_flags==flags&&!during.known,"dynamic write revision");okay(buffer->Unlock(),"dynamic Unlock");require(view(buffer.p).known,"dynamic write closes");}
    buffer.reset();createBuffer(s,buffer);bind(s,buffer.p);Com<T> raw;get(s,raw,true);
    {Fault fault(raw.p);okay(buffer->Lock(0,16,&data,0),"lock before failed Unlock");Fault::failUnlock=true;require(buffer->Unlock()==E_FAIL,"failed Unlock HRESULT preserved");auto bad=view(buffer.p);require(!bad.known&&bad.ambiguous&&bad.pending_locks==1,"failed Unlock remains unknown pending");Fault::failUnlock=false;okay(buffer->Unlock(),"actual recovery Unlock");require(!view(buffer.p).known&&view(buffer.p).ambiguous,"failed Unlock ambiguity sticky");}
    bind<T>(s,nullptr);
}
void processCases(Session& s){
    Com<IDirect3DVertexBuffer9> source,destination;
    okay(s.device->CreateVertexBuffer(48,0,D3DFVF_XYZ,D3DPOOL_MANAGED,&source.p,nullptr),"ProcessVertices source");
    okay(s.device->CreateVertexBuffer(64,0,D3DFVF_XYZRHW,D3DPOOL_MANAGED,&destination.p,nullptr),"ProcessVertices destination");
    void* data=nullptr;okay(source->Lock(0,0,&data,0),"process source Lock");const float positions[12]={0,0,.5f, .1f,0,.5f, 0,.1f,.5f, .1f,.1f,.5f};std::memcpy(data,positions,sizeof positions);okay(source->Unlock(),"process source Unlock");
    okay(s.device->SetStreamSource(0,source.p,0,12),"process stream");okay(s.device->SetFVF(D3DFVF_XYZ),"process FVF");okay(s.device->SetVertexShader(nullptr),"process fixed vertex");okay(s.device->SetRenderState(D3DRS_LIGHTING,FALSE),"process lighting");
    D3DMATRIX identity{};identity._11=identity._22=identity._33=identity._44=1;for(auto t:{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION})okay(s.device->SetTransform(t,&identity),"process matrix");
    HRESULT hr=s.device->ProcessVertices(0,0,4,destination.p,nullptr,0);std::printf("OBS ProcessVertices=%08lx\n",hr);okay(hr,"ProcessVertices succeeds");auto result=view(destination.p);require(result.known&&result.revision==1&&!result.pending_locks,"ProcessVertices destination revision");
    {DeviceFault fault(s.native(),false);require(s.device->ProcessVertices(0,0,4,destination.p,nullptr,0)==E_FAIL,"failed ProcessVertices result preserved");}require(view(destination.p).known&&view(destination.p).revision==1,"failed ProcessVertices no revision");
    okay(destination->Lock(0,16,&data,0),"pending ProcessVertices Lock");hr=s.device->ProcessVertices(0,0,4,destination.p,nullptr,0);std::printf("OBS pending_ProcessVertices=%08lx\n",hr);okay(hr,"pending ProcessVertices succeeds");result=view(destination.p);require(!result.known&&result.ambiguous&&result.pending_locks==1&&result.revision==3,"pending ProcessVertices marks ambiguity");okay(destination->Unlock(),"pending ProcessVertices Unlock");require(!view(destination.p).known&&view(destination.p).ambiguous,"pending ProcessVertices ambiguity persists");
    okay(s.device->SetStreamSource(0,nullptr,0,0),"process unbind");
}
void unknownCases(Create create,HWND window,unsigned faultKind){
    Session s(create,window,true,true);Com<IDirect3DVertexBuffer9> buffer,raw;createBuffer(s,buffer);bind(s,buffer.p);get(s,raw,true);
    {Fault fault(raw.p);void* data=nullptr;Fault::failGet=faultKind==0;Fault::failSet=faultKind==1;
        if(faultKind<2){okay(buffer->Lock(0,16,&data,0),"metadata failure preserves Lock success");okay(buffer->Unlock(),"metadata failure preserves Unlock success");}
        else {const GUID privateGuid={0x0cdb7df1,0xd3ca,0x4c69,{0x9a,0x4f,0x6e,0x28,0x33,0xf2,0x95,0x68}};if(faultKind==2)okay(buffer->FreePrivateData(privateGuid),"reserved metadata removal forwarded");else{DWORD corrupt=7;okay(buffer->SetPrivateData(privateGuid,&corrupt,sizeof corrupt,0),"reserved metadata replacement forwarded");}}
        Fault::failGet=Fault::failSet=false;require(!view(buffer.p).known&&FAILED(view(buffer.p).status),"bookkeeping failure device unknown");}
    raw.reset();buffer.reset();get(s,buffer);require(!view(buffer.p).known,"unknown survives wrapper recreation");
    Com<IDirect3DVertexBuffer9> other;createBuffer(s,other);require(!view(other.p).known,"device latch includes new buffers");bind<IDirect3DVertexBuffer9>(s,nullptr);okay(s.device->Reset(&s.pp),"managed buffers Reset");require(!view(buffer.p).known&&!view(other.p).known,"unknown latch survives Reset");
}
void creationFailure(Create create,HWND window){Session s(create,window,true,true);Com<IDirect3DVertexBuffer9> buffer;{DeviceFault fault(s.native(),true);createBuffer(s,buffer);}require(buffer.p&&!view(buffer.p).known&&view(buffer.p).status==E_OUTOFMEMORY,"creation metadata failure preserves buffer output");}
void knownReset(Create create,HWND window){Session s(create,window,true,true);Com<IDirect3DVertexBuffer9> buffer;createBuffer(s,buffer);void* data=nullptr;okay(buffer->Lock(0,16,&data,0),"managed beforeReset Lock");okay(buffer->Unlock(),"managed beforeReset Unlock");okay(s.device->Reset(&s.pp),"known managed Reset");require(view(buffer.p).known&&view(buffer.p).revision==1,"known managed revision persists Reset");}
void untaggedCase(Create create,HWND window){Session s(create,window,true,true);Com<IDirect3DVertexBuffer9> raw,application;okay(s.native()->CreateVertexBuffer(256,0,D3DFVF_XYZ,D3DPOOL_MANAGED,&raw.p,nullptr),"native preexisting buffer");okay(s.native()->SetStreamSource(0,raw.p,0,12),"native preexisting binding");get(s,application);auto v=view(application.p);require(!v.known&&v.ambiguous&&v.status==D3DERR_NOTFOUND,"untagged adoption unknown");void* p=nullptr;okay(application->Lock(0,16,&p,0),"untagged write Lock");okay(application->Unlock(),"untagged write Unlock");v=view(application.p);require(!v.known&&v.ambiguous&&v.revision==1,"untagged write cannot fabricate known");bind<IDirect3DVertexBuffer9>(s,nullptr);}
template<class T> void endpointCases(Session& s) {
    Com<T> buffer,raw;createBuffer(s,buffer);bind(s,buffer.p);get(s,raw,true);
    require(!borrowed_native_buffer_for_lock_contract(static_cast<T*>(nullptr)),"endpoint null rejected");
    require(!borrowed_native_buffer_for_lock_contract(reinterpret_cast<T*>(0x1234)),"endpoint unknown pointer not dereferenced");
    require(!borrowed_native_buffer_for_lock_contract(raw.p),"endpoint native input rejected");
    require(!borrowed_native_buffer_for_lock_contract(reinterpret_cast<T*>(s.device.p)),"endpoint nonbuffer wrapper rejected");
    if constexpr(std::is_same_v<T,IDirect3DVertexBuffer9>)
        require(!borrowed_native_buffer_for_lock_contract(reinterpret_cast<IDirect3DIndexBuffer9*>(buffer.p)),"endpoint wrong buffer kind rejected");
    else require(!borrowed_native_buffer_for_lock_contract(reinterpret_cast<IDirect3DVertexBuffer9*>(buffer.p)),"endpoint wrong buffer kind rejected");
    const auto appRefs=buffer->AddRef();buffer->Release();const auto nativeRefs=raw->AddRef();raw->Release();
    const auto before=view(buffer.p);SetLastError(0x5a1c23);
    for(unsigned i=0;i<16;++i)require(borrowed_native_buffer_for_lock_contract(buffer.p)==raw.p,"endpoint exact borrowed native");
    require(GetLastError()==0x5a1c23,"endpoint LastError unchanged");
    require(buffer->AddRef()==appRefs,"endpoint adds no wrapper references");buffer->Release();
    require(raw->AddRef()==nativeRefs,"endpoint adds no native references");raw->Release();
    const auto after=view(buffer.p);
    require(before.revision==after.revision&&before.pending_locks==after.pending_locks&&before.known==after.known&&before.last_lock_flags==after.last_lock_flags,"endpoint does not mutate tracking");
    auto original=*reinterpret_cast<void***>(buffer.p);void* table[14];std::copy(original,original+14,table);
    *reinterpret_cast<void***>(buffer.p)=table;
    require(borrowed_native_buffer_for_lock_contract(buffer.p)==raw.p,"endpoint copied unchanged slots accepted");
    for(unsigned slot:{11u,12u}){table[slot]=nullptr;require(!borrowed_native_buffer_for_lock_contract(buffer.p),"endpoint replaced Lock or Unlock rejected");table[slot]=original[slot];}
    table[10]=nullptr;require(borrowed_native_buffer_for_lock_contract(buffer.p)==raw.p,"endpoint unrelated slot not certified");
    *reinterpret_cast<void***>(buffer.p)=original;
    // Shared-table changes must not redefine the pristine expected method.
    DWORD protection=0;okay(VirtualProtect(original+11,sizeof(void*)*2,PAGE_EXECUTE_READWRITE,&protection)?S_OK:E_FAIL,"endpoint shared table writable");
    void* oldSlot=original[12];original[12]=nullptr;
    const bool rejected=!borrowed_native_buffer_for_lock_contract(buffer.p);original[12]=oldSlot;
    DWORD ignored=0;const bool restored=VirtualProtect(original+11,sizeof(void*)*2,protection,&ignored)!=FALSE;
    require(rejected,"endpoint shared Unlock replacement rejected");require(restored,"endpoint shared table protection restored");
    require(borrowed_native_buffer_for_lock_contract(buffer.p)==raw.p,"endpoint restored slots accepted");
    auto retired=buffer.p;buffer.reset();require(!borrowed_native_buffer_for_lock_contract(retired),"endpoint retired wrapper rejected without dereference");
    get(s,buffer);require(borrowed_native_buffer_for_lock_contract(buffer.p)==raw.p,"endpoint recreated wrapper accepted");
    bind<T>(s,nullptr);
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);int result=1;HMODULE dll=LoadLibraryA("d3d9.dll");WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3BufferContent";RegisterClassA(&cls);HWND window=CreateWindowA(cls.lpszClassName,"X3 buffer tracking fixture",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);
    try{require(dll&&window,"hidden fixture initialized");auto address=GetProcAddress(dll,"Direct3DCreate9");Create create=nullptr;std::memcpy(&create,&address,sizeof create);require(create!=nullptr,"factory entrypoint");
        std::vector<Outcome> baselineVB,baselineIB;{Session s(create,window,false,false);baselineVB=parity<IDirect3DVertexBuffer9>(s,false);baselineIB=parity<IDirect3DIndexBuffer9>(s,false);}
        for(bool tracked:{false,true}){Session s(create,window,true,tracked);require(parity<IDirect3DVertexBuffer9>(s,tracked)==baselineVB,"VB failure outputs match baseline");require(parity<IDirect3DIndexBuffer9>(s,tracked)==baselineIB,"IB failure outputs match baseline");endpointCases<IDirect3DVertexBuffer9>(s);endpointCases<IDirect3DIndexBuffer9>(s);}
        for(bool pure:{false,true}){std::printf("CASE normal_tracking pure=%u\n",pure);Session s(create,window,true,true,pure);bufferCases<IDirect3DVertexBuffer9>(s);bufferCases<IDirect3DIndexBuffer9>(s);processCases(s);}
        for(unsigned n=0;n<4;++n){std::printf("CASE bookkeeping_fault kind=%u\n",n);unknownCases(create,window,n);}untaggedCase(create,window);creationFailure(create,window);knownReset(create,window);
        std::printf("RESULT PASS checks=%u\n",checks);result=0;
    }catch(const std::exception& e){std::printf("RESULT FAIL %s checks=%u\n",e.what(),checks);}if(window)DestroyWindow(window);UnregisterClassA(cls.lpszClassName,cls.hInstance);if(dll)FreeLibrary(dll);return result;}
