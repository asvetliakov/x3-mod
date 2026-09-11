#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/ownership/d3d9_ownership.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace x3m::ownership;
unsigned checks=0,samples=0;
void ensure(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
void ok(HRESULT result,const char* label){ensure(SUCCEEDED(result),label);}
template<class T>struct Com{T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}};
using Create=IDirect3D9*(WINAPI*)(UINT);
struct Session{
    Com<IDirect3D9> factory;Com<IDirect3DDevice9> device;
    Session(Create create,HWND window,int mode){
        auto* native=create(D3D_SDK_VERSION);ensure(native,"factory");
        if(mode>=2){Options options;options.track_buffer_writes=true;options.capture_finite_positions=mode==3;ok(wrap_factory(native,&factory.p,options),"wrap");}else factory.p=native;
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=pp.BackBufferHeight=16;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        ok(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device.p),"device");
    }
};
std::int64_t ticks(){LARGE_INTEGER value;QueryPerformanceCounter(&value);return value.QuadPart;}
struct Timing{double create,lock,copy,unlock;};
constexpr unsigned repeats=7;
Timing trial(Session& session,int mode,int kind,UINT size,double frequency){
    Com<IDirect3DVertexBuffer9> vb;Com<IDirect3DIndexBuffer9> ib;
    const DWORD usage=mode==1?0:D3DUSAGE_WRITEONLY;
    const auto begin=ticks();
    HRESULT hr=kind==0?session.device->CreateVertexBuffer(size,usage,0,D3DPOOL_MANAGED,&vb.p,nullptr):session.device->CreateIndexBuffer(size,usage,kind==1?D3DFMT_INDEX16:D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib.p,nullptr);
    const auto created=ticks();ok(hr,"create buffer");
    DWORD presented=0,backing=0;
    if(vb.p){D3DVERTEXBUFFER_DESC a{},b{};ok(vb->GetDesc(&a),"VB description");auto* native=mode>=2?borrowed_native_buffer_for_lock_contract(vb.p):vb.p;ensure(native,"VB endpoint");ok(native->GetDesc(&b),"native VB description");presented=a.Usage;backing=b.Usage;}
    else {D3DINDEXBUFFER_DESC a{},b{};ok(ib->GetDesc(&a),"IB description");auto* native=mode>=2?borrowed_native_buffer_for_lock_contract(ib.p):ib.p;ensure(native,"IB endpoint");ok(native->GetDesc(&b),"native IB description");presented=a.Usage;backing=b.Usage;}
    ensure(presented==usage&&backing==((mode==1||mode==3)?0:D3DUSAGE_WRITEONLY),"expected actual readable policy");
    void* pointer=nullptr;const auto lock_begin=ticks();
    hr=vb.p?vb->Lock(0,0,&pointer,D3DLOCK_NOSYSLOCK):ib->Lock(0,0,&pointer,D3DLOCK_NOSYSLOCK);
    const auto locked=ticks();ok(hr,"Lock");ensure(pointer,"mapping");
    const auto copy_begin=ticks();std::memset(pointer,0,size);const auto copied=ticks();
    hr=vb.p?vb->Unlock():ib->Unlock();const auto unlocked=ticks();ok(hr,"Unlock");
    if(mode==3){
        if(vb.p){FinitePositionRequest request;request.expected_revision=1;request.stride=12;request.vertex_count=size/12;request.position_type=D3DDECLTYPE_FLOAT3;FinitePositionView view;ok(get_finite_position_view(vb.p,request,&view),"finite query");ensure(view.state==FiniteStatus::Finite,"actual classified positions");}
        else {IndexRangeRequest request;request.expected_revision=1;request.format=kind==1?D3DFMT_INDEX16:D3DFMT_INDEX32;request.index_count=size/(kind==1?2:4);IndexRangeView view;ok(get_index_range_view(ib.p,request,&view),"index query");ensure(view.known&&view.minimum==0&&view.maximum==0,"actual classified indices");}
    }
    return {1e6*(created-begin)/frequency,1e6*(locked-lock_begin)/frequency,1e6*(copied-copy_begin)/frequency,1e6*(unlocked-copied)/frequency};
}
double median(std::array<double,repeats> values){std::sort(values.begin(),values.end());return values[repeats/2];}
int main(){try{
    LARGE_INTEGER frequency;ensure(QueryPerformanceFrequency(&frequency),"QPC");
    auto module=LoadLibraryA("C:\\windows\\system32\\d3d9.dll");ensure(module,"native D3D9");Create create=nullptr;auto entry=GetProcAddress(module,"Direct3DCreate9");std::memcpy(&create,&entry,sizeof create);ensure(create,"entry");
    auto window=CreateWindowExA(0,"STATIC","managed upload CPU performance",0,0,0,16,16,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);ensure(window,"window");
    const char* modes[]={"native_writeonly","native_readable","wrapped_off","wrapped_finite"};
    const char* kinds[]={"vertex","index16","index32"};
    for(int mode=0;mode<4;++mode){Session session(create,window,mode);
      for(int kind=0;kind<3;++kind)for(UINT size:{65536u,1048576u,8388608u}){
        for(unsigned warm=0;warm<2;++warm)trial(session,mode,kind,size,double(frequency.QuadPart));
        std::array<double,repeats> creation{},lock{},copy{},unlock{};
        for(unsigned repeat=0;repeat<repeats;++repeat){const auto result=trial(session,mode,kind,size,double(frequency.QuadPart));creation[repeat]=result.create;lock[repeat]=result.lock;copy[repeat]=result.copy;unlock[repeat]=result.unlock;}
        ++samples;std::printf("SAMPLE mode=%s kind=%s bytes=%u repeats=%u create_us=%.3f lock_us=%.3f copy_us=%.3f unlock_us=%.3f\n",modes[mode],kinds[kind],size,repeats,median(creation),median(lock),median(copy),median(unlock));
      }
    }
    DestroyWindow(window);FreeLibrary(module);std::printf("RESULT PASS samples=%u checks=%u\n",samples,checks);return 0;
}catch(const std::exception& error){std::printf("RESULT FAIL %s checks=%u\n",error.what(),checks);return 1;}}
