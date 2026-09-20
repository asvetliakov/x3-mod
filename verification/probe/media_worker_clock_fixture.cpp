// Verification consumer of canonical production LavWorker and owned Clock.
// No private transport implementation. Graphics/oracles/watchdog stay here.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstdarg>
#include <cfenv>
#include "lav_worker.h"
#include "package_config.h"
#include "owned_clock/clock.h"
namespace transport=x3m::media;
constexpr unsigned BYTES=512*512*4,SLOTS=3,FRAMES=40,EVENTS=131072;
static LARGE_INTEGER frequency;
static DWORD main_thread=0;
constexpr transport::SourceKey SOURCE_ID=2,MISSING_ID=UINT32_MAX;
static bool package_mode=false;
static std::atomic<DWORD> package_call{0};
static std::atomic<bool> services_started{false};
struct PackagePreparation {
    HMODULE module=nullptr;std::shared_ptr<const transport::PackageConfig> owner;
    transport::PackageStatus status=transport::PackageStatus::error;transport::PackageError error{};
    DWORD thread=0;long long begin=0,end=0;std::atomic<bool> done{false};
    static DWORD WINAPI entry(void*);
};
static PackagePreparation package_preparation;
static std::weak_ptr<const transport::PackageConfig> package_weak;
static thread_local unsigned event_session=0,event_instance=0;
static thread_local transport::FrameIdentity event_identity{};
static long long ticks(){LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}
DWORD WINAPI PackagePreparation::entry(void*){
    auto& p=package_preparation;p.thread=GetCurrentThreadId();p.begin=ticks();
    package_call.store(GetTickCount(),std::memory_order_release);p.status=transport::load_package_config(p.module,p.owner,p.error);p.end=ticks();package_call.store(0,std::memory_order_release);p.done.store(true,std::memory_order_release);return 0;
}
struct Event {const char *name,*label;long long begin,end,a,b,c,d,e,f;HRESULT hr;DWORD thread;unsigned session,instance;transport::FrameIdentity identity;};
struct Trace {
    Event rows[EVENTS]{};unsigned count=0;bool overflow=false;
    void add(const char* name,const char* label,long long begin,long long end=0,long long a=0,long long b=0,long long c=0,long long d=0,long long e=0,long long f=0,HRESULT hr=S_OK){
        if(count==EVENTS){overflow=true;return;}
        rows[count++]={name,label,begin,end?end:begin,a,b,c,d,e,f,hr,GetCurrentThreadId(),event_session,event_instance,event_identity};
    }
    void dump(const char* owner){
        std::printf("MC_TRACE owner=%s instance=%u count=%u overflow=%d\n",owner,event_instance,count,overflow);
        for(unsigned i=0;i<count;++i){const auto& r=rows[i];std::printf("MC_EVENT owner=%s instance=%u index=%u thread=%lu session=%u name=%s label=%s begin=%lld end=%lld a=%lld b=%lld c=%lld d=%lld e=%lld f=%lld hr=%08lx handle_slot=%u handle_generation=%llu operation=%llu epoch=%llu\n",owner,r.instance,i,r.thread,r.session,r.name,r.label,r.begin,r.end,r.a,r.b,r.c,r.d,r.e,r.f,(unsigned long)r.hr,r.identity.session.slot,(unsigned long long)r.identity.session.generation,(unsigned long long)r.identity.operation,(unsigned long long)r.identity.epoch);}
    }
};
static Trace main_trace;
struct MetadataBuffer {
    char bytes[131072]{};unsigned used=0;bool overflow=false;
    void append(const char* format,...){
        if(overflow)return;
        int head=std::snprintf(bytes+used,sizeof bytes-used,"MC_META instance=%u session=%u ",event_instance,event_session);
        if(head<0||unsigned(head)>=sizeof bytes-used){overflow=true;return;}used+=unsigned(head);
        va_list args;va_start(args,format);int count=std::vsnprintf(bytes+used,sizeof bytes-used,format,args);va_end(args);
        if(count<0||unsigned(count)>=sizeof bytes-used){overflow=true;return;}used+=unsigned(count);
    }
};
static std::string utf8(const wchar_t* w){int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,w,-1,&s[0],n,nullptr,nullptr);if(n)s.pop_back();return s;}
static std::string encoded(const wchar_t* w){std::string out;for(unsigned char c:utf8(w)){if(c>32&&c<127&&c!='%')out+=char(c);else{char b[4];std::snprintf(b,sizeof b,"%%%02X",c);out+=b;}}return out;}
static std::string guid(const std::array<unsigned char,16>& bytes){GUID value{};std::memcpy(&value,bytes.data(),16);wchar_t s[40]{};StringFromGUID2(value,s,40);return utf8(s);}
static LRESULT CALLBACK window_proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static void messages(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
struct Observer final:transport::WorkerObserver {
    unsigned id=0;Trace trace;MetadataBuffer buffer;
    std::atomic<unsigned> slots[SLOTS]{};std::atomic<bool> overflow{false};
    void observe(const transport::Observation& o) noexcept override {
        static constexpr const char* names[]={"owner","call","failure","operation","command","desired","slot","lease","notify","clock","cooperative","service","dd_identity","session","surface","seek_position","retirement","facts","source_error","event_batch","provider_event","drain","deadline_anchor","source_copy","window","session_cleanup","cleanup","eof","identity"};
        event_instance=id;event_session=o.graph;event_identity=o.identity;
        const long long begin=o.begin?o.begin:ticks(),end=o.end?o.end:begin;
        Trace& target=GetCurrentThreadId()==main_thread?main_trace:trace;
        target.add(names[unsigned(o.kind)],o.label,begin,end,o.a,o.b,o.c,o.d,o.e,o.f,o.status);
        if(o.kind==transport::ObservationKind::slot){
            unsigned state=0;if(std::string_view(o.label)=="writing")state=1;else if(std::string_view(o.label)=="ready")state=2;else if(std::string_view(o.label)=="reading")state=3;
            if(o.a>=0&&o.a<SLOTS)slots[o.a].store(state,std::memory_order_release);
        }
        if(target.overflow)overflow.store(true,std::memory_order_release);
    }
    void metadata(const transport::Metadata& m) noexcept override try {
        event_instance=id;event_session=m.graph;using K=transport::MetadataKind;
        switch(m.kind){
        case K::type:buffer.append("MP_LAV_TYPE major=%s subtype=%s format=%s bytes=%lld\n",guid(m.guid[0]).c_str(),guid(m.guid[1]).c_str(),guid(m.guid[2]).c_str(),(long long)m.a);break;
        case K::context:buffer.append("MP_LAV_CONTEXT root_manifest=%s expected=%s manifest_matches=%lld com_redirection_observed=0\n",encoded(m.text).c_str(),encoded(m.expected).c_str(),(long long)m.a);break;
        case K::created_class:buffer.append("MP_LAV_CLASS role=%s clsid=%s expected=%s matches=%lld\n",m.role,guid(m.guid[0]).c_str(),guid(m.guid[1]).c_str(),(long long)m.a);break;
        case K::module:{const wchar_t* name=m.expected;for(const wchar_t* c=m.expected;*c;++c)if(*c==L'\\'||*c==L'/')name=c+1;
            buffer.append("%s name=%s path=%s expected=%s exact=%lld\n",m.b?"MP_LAV_ASSEMBLY_MODULE":"MP_LAV_MODULE",utf8(name).c_str(),encoded(m.text).c_str(),encoded(m.expected).c_str(),(long long)m.a);break;}
        case K::cohort:buffer.append("MP_LAV_COHORT index=%lld count=%lld same=%lld path=%s expected=%s\n",(long long)m.a,(long long)m.b,(long long)m.c,encoded(m.text).c_str(),encoded(m.expected).c_str());break;
        case K::dither:buffer.append("MP_LAV_DITHER requested=0 actual=%lld mode=ordered scope=transport_runtime_before_connect\n",(long long)m.a);break;
        case K::pixel_format:buffer.append("MP_LAV_SETTING format=%lld enabled=%lld\n",(long long)m.a,(long long)m.b);break;
        case K::sink:buffer.append("MP_LAV_SINK clsid=%s\n",guid(m.guid[0]).c_str());break;
        case K::rgb:buffer.append("MP_LAV_RGB width=%lld height=%lld bits=%lld compression=%lld subtype=%s valid=%lld\n",(long long)m.a,(long long)m.b,(long long)m.c,(long long)m.d,guid(m.guid[0]).c_str(),(long long)m.e);break;
        case K::allocator:buffer.append("MP_LAV_ALLOCATOR sink_input=%p mem_input=%p allocator=%p buffers=%lld bytes=%lld alignment=%lld prefix=%lld selected_after_connection=1 terminal_only=0\n",reinterpret_cast<void*>(uintptr_t(m.a)),reinterpret_cast<void*>(uintptr_t(m.b)),reinterpret_cast<void*>(uintptr_t(m.c)),(long long)m.d,(long long)m.e,(long long)m.f,(long long)m.g);break;
        case K::decommit:buffer.append("MP_LAV_TRANSPORT_DECOMMIT allocator=%p cleanup=%lld manual_commit=0\n",reinterpret_cast<void*>(uintptr_t(m.a)),(long long)m.b);break;
        case K::seek_caps:buffer.append("MP_LAV_CAPS capabilities=%08lx format=%s absolute_media_time=%lld\n",(unsigned long)m.a,guid(m.guid[0]).c_str(),(long long)m.b);break;
        case K::pin:break;
        }
        if(buffer.overflow)overflow.store(true,std::memory_order_release);
    }catch(...){overflow.store(true,std::memory_order_release);}
    bool free()const{for(const auto& slot:slots)if(slot.load(std::memory_order_acquire))return false;return true;}
};
struct Pump final:transport::VerificationPump {
    unsigned instance=0;
    unsigned picture_budget(const transport::Command& c)const noexcept override {if(c.request.source==MISSING_ID)return 0;if(instance==1)return 9;return c.epoch==2?7:6;}
};
struct WorkerContext {
    unsigned id=0;transport::LavWorker worker;std::shared_ptr<Observer> observer;
    std::wstring media,manifest,missing;transport::SessionHandle handle{};
    unsigned prepared=0,eof_epoch=0;bool source_failed=false,cleaned=false;
    std::atomic<bool> finished{false};
    bool start(bool tail=false){
        observer=std::make_shared<Observer>();observer->id=id;auto pump=std::make_shared<Pump>();pump->instance=id;
        transport::WorkerConfig config;config.instance=id;config.provider_manifest=manifest;config.sources={{SOURCE_ID,media},{MISSING_ID,missing}};
        if(package_mode){config.package_owner=package_preparation.owner;config.provider_manifest=config.package_owner->provider_manifest;config.sources={{config.package_owner->sources[0].id,config.package_owner->sources[0].path},{MISSING_ID,missing}};}config.observer=observer;if(!tail)config.verification_pump=pump;
        handle={id,1};return worker.start_service(std::move(config));
    }
    void poll(){transport::WorkerEvent value;for(unsigned i=0;i<3&&worker.poll_event(value);++i){
        event_instance=id;event_session=value.graph;event_identity=value.identity;
        main_trace.add("worker_event","observed",ticks(),0,value.flags,value.revision,value.status,value.graph_guard_safe,value.graph_released,value.events_clean);
        if(value.flags&transport::worker_prepared)prepared=unsigned(value.identity.epoch);
        if(value.flags&transport::worker_failed)source_failed=true;
        if(value.flags&transport::worker_source_eof)eof_epoch=unsigned(value.identity.epoch);
        if(value.flags&transport::worker_service_retired)cleaned=value.graph_guard_safe&&value.graph_released&&value.events_clean;
    }}
};
static WorkerContext contexts[2];
static std::atomic<DWORD> main_call{0};static std::atomic<bool> finished{false};
enum FixtureCommand {CONSTRUCT=1,SEEK=2,RETIRE=3,FAIL_LOAD=5};
static bool post(WorkerContext& ctx,unsigned kind,unsigned generation,LONGLONG target,unsigned limit,bool running){
    (void)limit; // The immutable verification pump supplies this budget to worker.
    if(kind==FAIL_LOAD)ctx.handle={ctx.id,2};
    transport::Publication desired{ctx.handle,ctx.id?202u:101u,generation,kind!=RETIRE,running};
    ctx.worker.publish_desired(desired);
    if(kind==RETIRE){ctx.worker.request_shutdown();return true;}
    transport::Command command{};command.session=desired.session;command.operation=desired.operation;command.epoch=desired.epoch;command.playing=running;
    command.kind=kind==SEEK?transport::CommandKind::seek:transport::CommandKind::play;command.request.source=kind==FAIL_LOAD?MISSING_ID:SOURCE_ID;command.request.start_ms=int32_t(target/10000);
    return ctx.worker.try_submit(command);
}

struct ClockRow {
    const char* name{};unsigned instance=0,tick=0;long long qpc=0,a=0,b=0,c=0,d=0;
    uint64_t operation=0,generation=0,rate=0,seconds=0,scaled=0;media_owned::Wide numerator;
    unsigned queued=0;int result=0,ms=0,end=0;bool armed=false,paused=false,intent=false,in_range=false;
    media_owned::Update update;
};
static ClockRow clock_rows[EVENTS];static unsigned clock_count=0,manager_tick=0;
struct Capture {unsigned instance=0,generation=0,sequence=0,tick=0;long long start=0,end=0;unsigned char pixels[BYTES]{};};
static Capture captures[FRAMES];static unsigned capture_count=0;
struct Playback {
    WorkerContext& ctx;media_owned::Clock clock;transport::FrameLease frames[SLOTS];unsigned leases[SLOTS]{},size=0,selected=0,epoch_selected=0;
    int selected_slot=-1;unsigned phase=0,paused_updates=0,epochs=1;bool retire_posted=false,exited=false;
    explicit Playback(WorkerContext& c):ctx(c),clock(uint64_t(frequency.QuadPart)){}
    void stamp(){event_instance=ctx.id;event_session=1;event_identity={ctx.handle,ctx.id?202u:101u,clock.generation()};}
    bool transaction(const char* name,long long a=0,long long b=0,long long c=0,long long d=0){
        if(clock_count==EVENTS)return false;
        stamp();auto& row=clock_rows[clock_count++];row.name=name;row.instance=ctx.id;row.tick=manager_tick;row.qpc=ticks();row.a=a;row.b=b;row.c=c;row.d=d;
        if((std::string_view(name)=="begin"))row.result=clock.begin(uint64_t(a),b,int32_t(c),d!=0,uint64_t(row.qpc));
        else if((std::string_view(name)=="seek"))row.result=clock.seek(a,int32_t(b),uint64_t(row.qpc));
        else if((std::string_view(name)=="pause"))row.result=clock.pause(uint64_t(row.qpc));
        else if((std::string_view(name)=="resume"))row.result=clock.resume(uint64_t(row.qpc));
        else if((std::string_view(name)=="stop"))row.result=clock.stop(uint64_t(row.qpc));
        else if((std::string_view(name)=="restart"))row.result=clock.restart_loop(uint64_t(row.qpc));
        else if((std::string_view(name)=="rate"))row.result=clock.set_rate(int32_t(a),uint64_t(row.qpc));
        else if((std::string_view(name)=="submit"))row.result=int(clock.submit({uint64_t(a),uint64_t(b),c,d}));
        else if((std::string_view(name)=="update")){row.update=clock.update(uint64_t(row.qpc));row.result=row.update.accepted;}
        else return false;
        row.operation=clock.operation();row.generation=clock.generation();row.rate=clock.rate_numerator();row.numerator=clock.numerator();
        row.armed=clock.armed();row.paused=clock.paused();row.intent=clock.intent();row.queued=clock.queued();row.end=int(clock.end());
        auto ms=clock.milliseconds();row.ms=ms.truncated;row.in_range=ms.in_range;std::memcpy(&row.seconds,&ms.seconds,8);std::memcpy(&row.scaled,&ms.scaled,8);
        return (std::string_view(name)=="submit")?row.result==int(media_owned::Admission::accepted):row.result!=0;
    }
    void free_lease(unsigned index,const char* reason){
        transport::LeaseRelease release=transport::LeaseRelease::discard;
        if(std::string_view(reason)=="revoked")release=transport::LeaseRelease::revoked;
        else if(std::string_view(reason)=="clock_consumed")release=transport::LeaseRelease::clock_consumed;
        else if(std::string_view(reason)=="selected_uploaded")release=transport::LeaseRelease::selected_uploaded;
        frames[index].release(release);
    }
    void revoke(){for(unsigned i=0;i<size;++i)free_lease(leases[i],"revoked");size=0;}
    bool ingest(){
        stamp();for(unsigned i=0;i<SLOTS&&size<SLOTS;++i){
            auto frame=ctx.worker.try_acquire_frame();if(!frame)break;
            const auto view=frame.view();unsigned index=view.slot;if(index>=SLOTS||frames[index])return false;
            frames[index]=std::move(frame);
            if(view.identity.epoch!=clock.generation()||!clock.intent()||clock.end()!=media_owned::End::none){free_lease(index,"stale_or_inactive");continue;}
            if(!transaction("submit",view.identity.epoch,view.sequence+1,view.start,view.end)){free_lease(index,"admission_error");return false;}
            leases[size++]=index;
        }return true;
    }
    bool update(){
        if(!transaction("update"))return false;
        auto u=clock_rows[clock_count-1].update;
        if(u.consumed>size)return false;
        selected_slot=-1;
        for(unsigned i=0;i<u.consumed;++i){unsigned index=leases[i];const auto& slot=frames[index].view();
            if(u.selected&&slot.sequence+1==u.frame.token){selected_slot=int(index);}
            else free_lease(index,"clock_consumed");
        }
        for(unsigned i=u.consumed;i<size;++i)leases[i-u.consumed]=leases[i];
        size-=u.consumed;
        if(u.selected&&selected_slot<0)return false;
        return u.end==media_owned::End::none||u.end==media_owned::End::positive;
    }
};
struct Graphics {
    HWND window{};IDirect3D9* d3d{};IDirect3DDevice9* device{};IDirect3DTexture9* textures[2]{};
    IDirect3DSurface9 *target{},*readback{},*backbuffer{};int last[2]{-1,-1};unsigned readbacks=0,transitions=0;
    struct Vertex {float x,y,z,rhw;DWORD color;float u,v;};
    void span(const char* label,long long begin,HRESULT hr,unsigned instance=2,long long a=0,long long b=0){event_instance=instance;main_trace.add("graphics",label,begin,ticks(),manager_tick,a,b,0,0,0,hr);}
    bool setup(){
        WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"X3ClockDrawMain";
        if(!RegisterClassW(&wc))return false;
        window=CreateWindowW(wc.lpszClassName,L"Two worker owned clocks",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1040,560,nullptr,nullptr,wc.hInstance,nullptr);
        d3d=Direct3DCreate9(D3D_SDK_VERSION);if(!window||!d3d)return false;
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=1024;pp.BackBufferHeight=512;pp.BackBufferFormat=D3DFMT_UNKNOWN;
        if(FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device)))return false;
        for(unsigned i=0;i<2;++i){if(FAILED(device->CreateTexture(512,512,1,0,D3DFMT_X8R8G8B8,D3DPOOL_MANAGED,&textures[i],nullptr)))return false;event_instance=i;main_trace.add("texture","identity",ticks(),0,reinterpret_cast<uintptr_t>(textures[i]));}
        if(FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer))||FAILED(device->CreateRenderTarget(1024,512,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr))||FAILED(device->CreateOffscreenPlainSurface(1024,512,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr)))return false;
        if(FAILED(device->SetRenderTarget(0,target))||FAILED(device->SetDepthStencilSurface(nullptr)))return false;
        D3DVIEWPORT9 viewport{0,0,1024,512,0,1};if(FAILED(device->SetViewport(&viewport)))return false;
        const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={{D3DRS_LIGHTING,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_DITHERENABLE,FALSE},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_SCISSORTESTENABLE,FALSE}};
        for(const auto& state:states)if(FAILED(device->SetRenderState(state.first,state.second)))return false;
        if(FAILED(device->SetVertexShader(nullptr))||FAILED(device->SetPixelShader(nullptr))||FAILED(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1)))return false;
        if(FAILED(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1))||FAILED(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE))||FAILED(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1))||FAILED(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE))||FAILED(device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE))||FAILED(device->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE)))return false;
        const std::pair<D3DSAMPLERSTATETYPE,DWORD> samplers[]={{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE}};
        for(const auto& sampler:samplers)if(FAILED(device->SetSamplerState(0,sampler.first,sampler.second)))return false;
        event_instance=2;main_trace.add("render_setup","point_1to1",ticks(),0,1024,512,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM);return true;
    }
    bool upload(Playback& p,int& selected_capture){
        if(p.selected_slot<0)return true;
        if(capture_count==FRAMES)return false;
        const auto& slot=p.frames[p.selected_slot].view();unsigned id=p.ctx.id;long long begin=ticks();D3DLOCKED_RECT lock{};
        HRESULT hr=textures[id]->LockRect(0,&lock,nullptr,0);span("texture_lock",begin,hr,id,slot.sequence);if(FAILED(hr))return false;
        bool valid=lock.pBits&&lock.Pitch>=2048;begin=ticks();if(valid)for(unsigned y=0;y<512;++y)std::memcpy(static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch,slot.bgra+y*2048,2048);span("texture_rows",begin,valid?S_OK:E_FAIL,id,slot.sequence);
        begin=ticks();hr=textures[id]->UnlockRect(0);span("texture_unlock",begin,hr,id,slot.sequence);if(!valid||FAILED(hr))return false;
        selected_capture=int(capture_count++);auto& cap=captures[selected_capture];cap.instance=id;cap.generation=unsigned(slot.identity.epoch);cap.sequence=slot.sequence;cap.start=slot.start;cap.end=slot.end;cap.tick=manager_tick;
        p.free_lease(unsigned(p.selected_slot),"selected_uploaded");p.selected_slot=-1;++p.selected;++p.epoch_selected;return true;
    }
    bool heartbeat(int selected[2],const char* transition){
        long long begin=ticks();main_call.store(GetTickCount(),std::memory_order_release);bool ok=true;
        HRESULT hr=device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff203040,1,0);long long middle=ticks();if(FAILED(hr))ok=false;
        long long draw=ticks();hr=device->BeginScene();bool scene=SUCCEEDED(hr);ok=ok&&scene;
        for(unsigned i=0;i<2&&scene;++i){if(selected[i]<0&&last[i]<0)continue;float left=float(i*512)-0.5f,right=left+512;
            Vertex vertices[4]={{left,-0.5f,0,1,0xffffffff,0,0},{right,-0.5f,0,1,0xffffffff,1,0},{left,511.5f,0,1,0xffffffff,0,1},{right,511.5f,0,1,0xffffffff,1,1}};
            hr=device->SetTexture(0,textures[i]);if(SUCCEEDED(hr))hr=device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));
            event_instance=i;main_trace.add("draw","texture",draw,ticks(),manager_tick,reinterpret_cast<uintptr_t>(textures[i]),selected[i]>=0?selected[i]:last[i],0,0,0,hr);ok=ok&&SUCCEEDED(hr);
        }
        span("draw_submission",draw,ok?S_OK:E_FAIL);long long compose=ticks();HRESULT end=scene?device->EndScene():E_FAIL;span("end_scene",compose,end);ok=ok&&SUCCEEDED(end);
        bool newly=selected[0]>=0||selected[1]>=0;bool inspect=newly||transition;
        if(inspect&&ok){
            if(readbacks>=52||(!newly&&transitions>=12))ok=false;
            else{++readbacks;if(!newly)++transitions;long long transfer=ticks();hr=device->GetRenderTargetData(target,readback);span("readback_transfer",transfer,hr);ok=SUCCEEDED(hr);
                if(ok){D3DLOCKED_RECT lock{};long long lb=ticks();hr=readback->LockRect(&lock,nullptr,D3DLOCK_READONLY);span("readback_lock",lb,hr);ok=SUCCEEDED(hr)&&lock.pBits&&lock.Pitch>=4096;
                    if(SUCCEEDED(hr)){long long snapshot=ticks();
                        for(unsigned i=0;i<2&&ok;++i){if(selected[i]<0&&last[i]<0)continue;bool matched=true;
                            for(unsigned y=0;y<512;++y){const auto* row=static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch+i*2048;
                                if(selected[i]>=0)std::memcpy(captures[selected[i]].pixels+y*2048,row,2048);
                                else matched=std::memcmp(captures[last[i]].pixels+y*2048,row,2048)==0&&matched;
                            }
                            event_instance=i;main_trace.add("render_region",newly?"selection":transition,snapshot,ticks(),manager_tick,selected[i]>=0?selected[i]:last[i],selected[i]>=0,matched);ok=ok&&matched;
                        }
                        span("snapshot_compare",snapshot,ok?S_OK:E_FAIL);long long ub=ticks();hr=readback->UnlockRect();span("readback_unlock",ub,hr);ok=ok&&SUCCEEDED(hr);
                    }
                }
            }
        }
        if(ok)for(unsigned i=0;i<2;++i)if(selected[i]>=0)last[i]=selected[i];
        compose=ticks();hr=device->StretchRect(target,nullptr,backbuffer,nullptr,D3DTEXF_NONE);if(SUCCEEDED(hr))hr=device->Present(nullptr,nullptr,nullptr,nullptr);span("compose_present",compose,hr);ok=ok&&SUCCEEDED(hr);
        event_instance=2;main_trace.add("heartbeat","draw_present",begin,ticks(),manager_tick,middle,ok,readbacks,transitions,0,ok?S_OK:E_FAIL);main_call.store(0,std::memory_order_release);return ok;
    }
    void cleanup(){if(readback)readback->Release();if(target)target->Release();if(backbuffer)backbuffer->Release();for(auto* texture:textures)if(texture)texture->Release();if(device)device->Release();if(d3d)d3d->Release();if(window)DestroyWindow(window);}
};
static DWORD WINAPI watchdog(void*){
    DWORD began=GetTickCount();while(!finished.load(std::memory_order_acquire)){
        DWORD now=GetTickCount(),main=main_call.load(std::memory_order_acquire);const char* reason=nullptr;
        if(DWORD(now-began)>=60000)reason="child_60s";
        if(main&&DWORD(now-main)>=10000)reason="main_call_10s";
        DWORD preparing=package_call.load(std::memory_order_acquire);if(preparing&&DWORD(now-preparing)>=10000)reason="package_read_10s";
        if(services_started.load(std::memory_order_acquire))for(auto& ctx:contexts)if(!ctx.finished.load(std::memory_order_acquire)){
            auto state=ctx.worker.poll_service_state();DWORD call_tick=state.call_tick,p=state.progress_tick;
            if(call_tick&&DWORD(now-call_tick)>=10000)reason="worker_call_10s";
            if(p&&DWORD(now-p)>=10000)reason="worker_progress_10s";
        }
        if(reason){std::printf("MC_TIMEOUT reason=%s\n",reason);std::fflush(stdout);TerminateProcess(GetCurrentProcess(),124);return 124;}Sleep(10);
    }return 0;
}
static bool prepare_package(Graphics& graphics){
    if(!package_mode)return true;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&PackagePreparation::entry),&package_preparation.module))return false;
    HANDLE thread=CreateThread(nullptr,0,&PackagePreparation::entry,nullptr,0,nullptr);if(!thread)return false;
    bool ok=true;
    while(!package_preparation.done.load(std::memory_order_acquire)){
        ++manager_tick;messages();int empty[2]{-1,-1};ok=graphics.heartbeat(empty,nullptr)&&ok;
        MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
    DWORD code=STILL_ACTIVE;
    while(GetExitCodeThread(thread,&code)&&code==STILL_ACTIVE)MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    CloseHandle(thread);
    const auto& p=package_preparation;
    std::printf("MC_PACKAGE kind=read begin=%lld end=%lld thread=%lu status=%u error=%u system=%lu module=%llu owner=%llu module_relative=1\n",p.begin,p.end,p.thread,unsigned(p.status),unsigned(p.error.code),(unsigned long)p.error.system,(unsigned long long)reinterpret_cast<uintptr_t>(p.module),(unsigned long long)reinterpret_cast<uintptr_t>(p.owner.get()));
    if(!ok||code||p.status!=transport::PackageStatus::ready||!p.owner||p.owner->sources.size()!=1||p.owner->sources[0].id!=SOURCE_ID||p.owner->sources[0].effective_flags!=8)return false;
    std::printf("MC_PACKAGE kind=paths manifest=%s media=%s id=%u flags=%u\n",encoded(p.owner->provider_manifest.c_str()).c_str(),encoded(p.owner->sources[0].path.c_str()).c_str(),p.owner->sources[0].id,p.owner->sources[0].effective_flags);
    package_weak=p.owner;return true;
}
static bool initial_assignment_quiescence(Graphics& graphics){
    transport::Publication canceled[2];
    for(unsigned i=0;i<2;++i){auto& ctx=contexts[i];canceled[i]={ctx.handle,i?202u:101u,1,false,false};ctx.worker.publish_desired(canceled[i]);}
    const DWORD began=GetTickCount(); // Existing 10 s operation envelope, not reset per observation.
    for(unsigned stage=0;stage<2;++stage){
        bool observed[2]{false,false};
        while(!(observed[0]&&observed[1])){
            if(DWORD(GetTickCount()-began)>=10000)return false;
            ++manager_tick;messages();
            for(unsigned i=0;i<2;++i){auto& ctx=contexts[i];ctx.poll();
                if(ctx.source_failed)return false;
                auto frame=ctx.worker.try_acquire_frame();if(frame)return false;
                if(!observed[i]&&ctx.worker.poll_assignment_quiescent(canceled[i])){
                    observed[i]=true;event_instance=i;event_session=0;event_identity=transport::identity(canceled[i]);
                    main_trace.add("quiescence","observed",ticks(),0,stage?3:1,0,0,3);
                }
            }
            int empty[2]{-1,-1};if(!graphics.heartbeat(empty,nullptr))return false;
            if(!(observed[0]&&observed[1]))MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        if(stage==0)for(unsigned i=0;i<2;++i){auto& ctx=contexts[i];auto live=canceled[i];live.live=true;ctx.worker.publish_desired(live);
            const bool refused=!ctx.worker.poll_assignment_quiescent(canceled[i]);
            event_instance=i;event_session=0;event_identity=transport::identity(canceled[i]);main_trace.add("quiescence","old_fact_refused",ticks(),0,2,refused);
            if(!refused)return false;
            ctx.worker.publish_desired(canceled[i]);
        }
    }
    return true;
}
static bool transfer_package_owner(unsigned workers){
    if(!package_mode)return true;
    package_preparation.owner.reset();const auto count=package_weak.use_count();
    std::printf("MC_PACKAGE kind=owners_transferred count=%ld expected=%u alive=%d qpc=%lld\n",count,workers,!package_weak.expired(),ticks());
    return count==static_cast<long>(workers);
}
static bool release_package_owners(){
    if(!package_mode)return true;
    // Called only after actual worker exits and the fixture watchdog's exit.
    // Their Impl configs, not this observer/global, own the package identity pins.
    for(auto& ctx:contexts)ctx.worker=transport::LavWorker{};
    std::printf("MC_PACKAGE kind=owners_released count=%ld expired=%d qpc=%lld\n",package_weak.use_count(),package_weak.expired(),ticks());
    return package_weak.expired();
}

static bool stop_watchdog_and_release_package(HANDLE watch){
    finished.store(true,std::memory_order_release);
    const DWORD waited=WaitForSingleObject(watch,1000);
    if(waited!=WAIT_OBJECT_0){
        std::printf("MC_TIMEOUT reason=watchdog_exit_unproved wait=%lu\n",(unsigned long)waited);
        // Retain every worker/config member and avoid static destruction while
        // the watchdog may still read them. The existing 60 s parent supervisor
        // ends this failed child; no new deadline or teardown shortcut is added.
        for(;;){messages();MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
    }
    std::printf("MC_WATCHDOG wait=%lu qpc=%lld\n",(unsigned long)waited,ticks());
    CloseHandle(watch);return release_package_owners();
}
static void dump_clocks(){
    for(unsigned i=0;i<clock_count;++i){const auto& r=clock_rows[i];
        std::printf("MC_CLOCK index=%u instance=%u tick=%u name=%s qpc=%lld a=%lld b=%lld c=%lld d=%lld result=%d operation=%llu generation=%llu rate=%llu armed=%d paused=%d intent=%d queued=%u end=%d ms=%d in_range=%d seconds=%llu scaled=%llu numerator=",i,r.instance,r.tick,r.name,r.qpc,r.a,r.b,r.c,r.d,r.result,(unsigned long long)r.operation,(unsigned long long)r.generation,(unsigned long long)r.rate,r.armed,r.paused,r.intent,r.queued,r.end,r.ms,r.in_range,(unsigned long long)r.seconds,(unsigned long long)r.scaled);
        for(unsigned j=8;j-- >0;)std::printf("%08x",r.numerator.word[j]);
        std::printf(" selected=%d token=%llu frame_generation=%llu start=%lld stop=%lld consumed=%u late=%u superseded=%u\n",r.update.selected,(unsigned long long)r.update.frame.token,(unsigned long long)r.update.frame.generation,(long long)r.update.frame.start,(long long)r.update.frame.end,r.update.consumed,r.update.late,r.update.superseded);
    }
}
static int child(){
    if(std::fegetround()!=FE_TONEAREST)return 4;
    main_thread=GetCurrentThreadId();for(unsigned i=0;i<2;++i)contexts[i].id=i;
    HANDLE watch=nullptr;
    Graphics graphics;main_call.store(GetTickCount(),std::memory_order_release);bool ok=graphics.setup();main_call.store(0,std::memory_order_release);
    if(!ok){finished.store(true,std::memory_order_release);CloseHandle(watch);graphics.cleanup();return 4;}
    Playback a(contexts[0]),b(contexts[1]);Playback* instances[]={&a,&b};
    ok=a.transaction("begin",101,0,280,0)&&b.transaction("begin",202,100000000,10360,1)&&b.transaction("rate",50000);
    int empty[2]{-1,-1};ok=graphics.heartbeat(empty,"initial")&&ok;
    watch=CreateThread(nullptr,0,watchdog,nullptr,0,nullptr);if(!watch)return 4;
    if(!prepare_package(graphics))return 4;
    for(auto* p:instances)if(!p->ctx.start())ok=false;
    ok=transfer_package_owner(2)&&ok;services_started.store(true,std::memory_order_release);
    ok=initial_assignment_quiescence(graphics)&&ok;
    ok=post(a.ctx,CONSTRUCT,unsigned(a.clock.generation()),0,6,true)&&ok;
    ok=post(b.ctx,CONSTRUCT,unsigned(b.clock.generation()),100000000,9,true)&&ok;
    bool b_restart=false,b_done=false,aborting=false;const char* transition=nullptr;unsigned transition_requests=0;
    unsigned b_selected_at_pause=0;DWORD pause_begin=0;
    while(!(a.exited&&b.exited)){
        ++manager_tick;messages();
        if(b_restart&&ok){ok=b.transaction("restart");b.revoke();ok=ok&&post(b.ctx,SEEK,unsigned(b.clock.generation()),100000000,9,true);b.epoch_selected=0;++b.epochs;b_restart=false;transition="restart_preparation";}
        for(auto* p:instances){p->stamp();
            p->ctx.poll();if(p->ctx.source_failed&&!(p==&a&&a.phase>=6))ok=false;
            if(p==&a&&a.phase>=6&&p->ctx.source_failed&&!p->retire_posted){p->ctx.worker.request_shutdown();p->retire_posted=true;}
            if(ok&&!p->exited){ok=p->ingest()&&p->update()&&ok;}
        }
        int chosen[2]{-1,-1};
        if(ok){main_call.store(GetTickCount(),std::memory_order_release);ok=graphics.upload(a,chosen[0])&&graphics.upload(b,chosen[1]);main_call.store(0,std::memory_order_release);}
        if(transition){++transition_requests;if(transition_requests>12)ok=false;}
        bool drawn=graphics.heartbeat(chosen,transition);transition=nullptr;ok=ok&&drawn;
        if(ok){
            if(a.phase==0&&chosen[0]>=0){ok=a.transaction("pause");a.phase=1;transition="pause";
                b_selected_at_pause=b.selected;pause_begin=GetTickCount();}
            else if(a.phase==1){++a.paused_updates;
                // Progress only after the required peer activity really occurred
                // after pause. B.selected increments after actual texture upload;
                // this phase runs after the successful draw/readback heartbeat.
                // Both players keep ingesting/updating/rendering while we wait.
                const DWORD elapsed=DWORD(GetTickCount()-pause_begin);
                if(elapsed>=10000){std::puts("MC_TIMEOUT reason=paused_peer_selection_10s");ok=false;}
                else if(a.size==3&&a.paused_updates>=2&&b.selected>b_selected_at_pause){a.stamp();main_trace.add("phase","paused_three_leases",ticks(),0,a.size,a.paused_updates,a.clock.generation(),b_selected_at_pause,b.selected,elapsed);
                    ok=a.transaction("seek",22000000,2480);a.revoke();ok=ok&&post(a.ctx,SEEK,unsigned(a.clock.generation()),22000000,7,true);a.phase=2;transition="paused_seek";}
            }else if(a.phase==2&&a.size>0){ok=a.transaction("rate",50000)&&a.transaction("resume");a.phase=3;}
            else if(a.phase==3&&a.clock.compare(22800000)>=0&&a.clock.end()==media_owned::End::none){ok=a.transaction("rate",200000);a.phase=4;}
            if(a.phase>=3&&a.phase<=4&&a.clock.end()==media_owned::End::positive){ok=a.transaction("stop")&&a.transaction("seek",0,280);a.revoke();ok=ok&&post(a.ctx,SEEK,unsigned(a.clock.generation()),0,6,false);a.phase=5;transition="A_positive_stop";}
            else if(a.phase==5&&a.ctx.prepared==a.clock.generation()){
                if(a.ctx.observer->free()){ok=post(a.ctx,FAIL_LOAD,unsigned(a.clock.generation()),0,0,false);a.phase=6;transition="A_stopped_failure_request";}
            }
            if(!b_done&&b.clock.end()==media_owned::End::positive){
                if(b.epochs<3)b_restart=true;
                else{ok=b.transaction("stop");b.revoke();ok=ok&&post(b.ctx,RETIRE,unsigned(b.clock.generation()),0,0,false);b.retire_posted=true;b_done=true;transition="B_positive_end";}
            }
        }
        if(!ok&&!aborting){aborting=true;for(auto* p:instances){p->transaction("stop");if(p->selected_slot>=0){p->free_lease(unsigned(p->selected_slot),"graphics_failure");p->selected_slot=-1;}p->revoke();p->ctx.worker.publish_desired({p->ctx.handle,p->ctx.id?202u:101u,p->clock.generation(),false,false});p->ctx.worker.request_shutdown();}}
        for(auto* p:instances)if(!p->exited){
            // Poll only. LavWorker owns the thread handle and all COM retirement.
            if(p->ctx.worker.poll_retired()){
                p->ctx.poll();p->exited=true;p->ctx.finished.store(true,std::memory_order_release);p->stamp();
                main_trace.add("worker_exit","observed",ticks(),0,0,p->ctx.cleaned);
                p->revoke();for(unsigned i=0;i<SLOTS;++i){auto frame=p->ctx.worker.try_acquire_frame();if(frame)frame.release();}
                for(unsigned i=0;i<SLOTS;++i){unsigned state=p->ctx.observer->slots[i].load(std::memory_order_acquire);main_trace.add("cpu_final","slot",ticks(),0,i,state);if(state)ok=false;}
                if(!p->ctx.cleaned)ok=false;
                if(p==&a&&a.phase==6)transition="A_source_failure_retired";
            }
        }
        for(const auto& ctx:contexts)if(ctx.observer->overflow.load(std::memory_order_acquire))ok=false;
        if(main_trace.overflow||clock_count>=EVENTS)ok=false;
        if(!(a.exited&&b.exited))MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
    if(transition){int hold[2]{-1,-1};ok=graphics.heartbeat(hold,transition)&&ok;}
    ok=stop_watchdog_and_release_package(watch)&&ok;
    std::printf("MC_HEADER kind=worker_owned_clock_two_textures_v2 transport=production_lav_worker support_seeking=1 module_pin=process_lifetime qpc_frequency=%lld main_thread=%lu slots_per_instance=3 instances=2 capture_limit=40 readback_limit=52 graph_clock=none_explicit clock_commit=0abe0a44 package_config=%d\n",frequency.QuadPart,GetCurrentThreadId(),package_mode);
    dump_clocks();event_instance=2;main_trace.dump("main");for(auto& ctx:contexts){event_instance=ctx.id;ctx.observer->trace.dump("worker");std::fwrite(ctx.observer->buffer.bytes,1,ctx.observer->buffer.used,stdout);}
    long long files=ticks();
    for(unsigned i=0;i<capture_count;++i){wchar_t path[64];std::swprintf(path,64,L"clock-f%02u.bgra",i);HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);DWORD wrote=0;
        bool saved=file!=INVALID_HANDLE_VALUE&&WriteFile(file,captures[i].pixels,BYTES,&wrote,nullptr)&&wrote==BYTES;if(file!=INVALID_HANDLE_VALUE)saved=CloseHandle(file)&&saved;ok=ok&&saved;
        const auto& cap=captures[i];std::printf("MC_CAPTURE index=%u instance=%u generation=%u sequence=%u tick=%u start=%lld end=%lld bytes=%lu written=%d file=clock-f%02u.bgra\n",i,cap.instance,cap.generation,cap.sequence,cap.tick,cap.start,cap.end,wrote,saved,i);
    }
    std::printf("MC_OUTPUT begin=%lld end=%lld\n",files,ticks());
    graphics.cleanup();std::printf("MC_RESULT functional=%d captures=%u readbacks=%u transition_readbacks=%u A_phase=%u B_epochs=%u A_clean=%d B_clean=%d\n",ok,capture_count,graphics.readbacks,graphics.transitions,a.phase,b.epochs,int(a.ctx.cleaned),int(b.ctx.cleaned));return ok?0:2;
}
// Minimal affected EOF check: consume every actual lease, without a presentation
// clock or verification pump cap. No content count is substituted for source EOF.
static int tail_child(){
    if(std::fegetround()!=FE_TONEAREST)return 4;
    main_thread=GetCurrentThreadId();contexts[0].id=0;contexts[1].id=1;contexts[1].finished.store(true);
    Graphics graphics;main_call.store(GetTickCount());bool ok=graphics.setup();main_call.store(0);if(!ok){graphics.cleanup();return 4;}
    Playback a(contexts[0]);int empty[2]{-1,-1};ok=graphics.heartbeat(empty,nullptr)&&ok;
    HANDLE watch=CreateThread(nullptr,0,watchdog,nullptr,0,nullptr);if(!watch)return 4;
    if(!prepare_package(graphics)||!a.ctx.start(true)||!transfer_package_owner(1))return 4;
    services_started.store(true,std::memory_order_release);
    unsigned epoch=1;bool shutdown=false;
    auto begin_source=[&]{
        a.ctx.handle={0,epoch};transport::Command command{};command.session=a.ctx.handle;command.operation=303;command.epoch=epoch;
        command.kind=transport::CommandKind::play;command.request.source=SOURCE_ID;command.request.start_ms=1939320;command.playing=true;
        a.ctx.worker.publish_desired({command.session,command.operation,command.epoch,true,true});return a.ctx.worker.try_submit(command);
    };
    ok=begin_source()&&ok;
    while(!a.exited){
        ++manager_tick;messages();a.ctx.poll();int chosen[2]{-1,-1};
        if(a.ctx.source_failed)ok=false;
        if(ok&&!shutdown){
            auto frame=a.ctx.worker.try_acquire_frame();
            if(frame){const auto view=frame.view();
                if(view.identity.session==a.ctx.handle&&view.identity.operation==303&&view.identity.epoch==epoch&&view.sequence==a.selected&&a.epoch_selected<6){
                    unsigned index=view.slot;a.frames[index]=std::move(frame);a.selected_slot=int(index);
                    event_instance=0;event_session=view.graph;event_identity=view.identity;
                    main_trace.add("tail_selection","actual_lease",ticks(),0,manager_tick,view.sequence,view.start,view.end,index,epoch);
                    main_call.store(GetTickCount());ok=graphics.upload(a,chosen[0]);main_call.store(0);
                }else ok=false;
            }
        }
        ok=graphics.heartbeat(chosen,nullptr)&&ok;
        if(ok&&!shutdown&&a.ctx.eof_epoch==epoch&&a.epoch_selected==6&&a.ctx.observer->free()){
            event_instance=0;event_session=epoch;event_identity={a.ctx.handle,303,epoch};main_trace.add("tail_phase","eof_six_free",ticks(),0,epoch,a.epoch_selected,3);
            if(epoch==1){++epoch;a.epoch_selected=0;ok=begin_source();}
            else{a.ctx.worker.publish_desired({a.ctx.handle,303,epoch,false,false});a.ctx.worker.request_shutdown();shutdown=true;}
        }
        if(!ok&&!shutdown){for(unsigned i=0;i<SLOTS;++i)a.frames[i].release();a.ctx.worker.publish_desired({a.ctx.handle,303,epoch,false,false});a.ctx.worker.request_shutdown();shutdown=true;}
        if(a.ctx.worker.poll_retired()){
            a.ctx.poll();a.exited=true;a.ctx.finished.store(true);event_instance=0;event_identity={a.ctx.handle,303,epoch};main_trace.add("worker_exit","observed",ticks(),0,0,a.ctx.cleaned);
            for(unsigned i=0;i<SLOTS;++i){auto frame=a.ctx.worker.try_acquire_frame();if(frame)frame.release();}
            for(unsigned i=0;i<SLOTS;++i){unsigned state=a.ctx.observer->slots[i].load(std::memory_order_acquire);main_trace.add("cpu_final","slot",ticks(),0,i,state);if(state)ok=false;}
        }
        if(main_trace.overflow||a.ctx.observer->overflow.load(std::memory_order_acquire))ok=false;
        if(!a.exited)MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
    ok=stop_watchdog_and_release_package(watch)&&ok;
    std::printf("MC_HEADER kind=worker_shared_transport_eof_tail_v1 transport=production_lav_worker mode=eof-tail support_seeking=1 module_pin=process_lifetime qpc_frequency=%lld main_thread=%lu slots_per_instance=3 instances=1 capture_limit=12 readback_limit=12 graph_clock=none_explicit clock_commit=0abe0a44 package_config=%d\n",frequency.QuadPart,main_thread,package_mode);
    event_instance=2;main_trace.dump("main");event_instance=0;a.ctx.observer->trace.dump("worker");std::fwrite(a.ctx.observer->buffer.bytes,1,a.ctx.observer->buffer.used,stdout);
    const auto begin=ticks();
    for(unsigned i=0;i<capture_count;++i){wchar_t path[64];std::swprintf(path,64,L"clock-f%02u.bgra",i);HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);DWORD wrote=0;
        bool saved=file!=INVALID_HANDLE_VALUE&&WriteFile(file,captures[i].pixels,BYTES,&wrote,nullptr)&&wrote==BYTES;if(file!=INVALID_HANDLE_VALUE)saved=CloseHandle(file)&&saved;ok=ok&&saved;
        const auto& cap=captures[i];std::printf("MC_CAPTURE index=%u instance=%u generation=%u sequence=%u tick=%u start=%lld end=%lld bytes=%lu written=%d file=clock-f%02u.bgra\n",i,cap.instance,cap.generation,cap.sequence,cap.tick,cap.start,cap.end,wrote,saved,i);
    }
    std::printf("MC_OUTPUT begin=%lld end=%lld\n",begin,ticks());graphics.cleanup();ok=ok&&capture_count==12&&a.ctx.cleaned;
    std::printf("MC_RESULT functional=%d captures=%u readbacks=%u transition_readbacks=%u A_clean=%d B_clean=0 eof_sessions=%u\n",ok,capture_count,graphics.readbacks,graphics.transitions,a.ctx.cleaned,a.ctx.eof_epoch);return ok?0:2;
}

static std::wstring quote(const std::wstring& a){std::wstring r=L"\"";unsigned slashes=0;for(wchar_t c:a){if(c==L'\\'){++slashes;continue;}r.append(c==L'"'?slashes*2+1:slashes,L'\\');slashes=0;r+=c;}r.append(slashes*2,L'\\');return r+L'"';}
static int supervise(int argc,wchar_t** argv){
    HANDLE job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof limits))return 4;
    std::wstring cmd=quote(argv[0]);for(int i=1;i<argc;++i)cmd+=L" "+quote(argv[i]);cmd+=L" --child";
    STARTUPINFOW si{};si.cb=sizeof si;si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=GetStdHandle(STD_ERROR_HANDLE);PROCESS_INFORMATION pi{};
    if(!CreateProcessW(nullptr,&cmd[0],nullptr,nullptr,TRUE,CREATE_SUSPENDED,nullptr,nullptr,&si,&pi)){CloseHandle(job);return 4;}
    if(!AssignProcessToJobObject(job,pi.hProcess)||ResumeThread(pi.hThread)==DWORD(-1)){TerminateProcess(pi.hProcess,125);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(job);return 4;}
    CloseHandle(pi.hThread);DWORD wait=WaitForSingleObject(pi.hProcess,60000),code=124;
    if(wait!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,124);WaitForSingleObject(pi.hProcess,5000);std::puts("MC_TIMEOUT reason=supervisor_60s");}else GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hProcess);CloseHandle(job);return int(code);
}
int wmain(int argc,wchar_t** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);QueryPerformanceFrequency(&frequency);bool is_child=false;
    std::wstring media,manifest,missing,mode=L"clock";
    for(int i=1;i<argc;++i){std::wstring arg=argv[i];if(arg==L"--child"){is_child=true;continue;}if(i+1>=argc)return 64;std::wstring value=argv[++i];if(arg==L"--media")media=value;else if(arg==L"--lav-manifest")manifest=value;else if(arg==L"--missing-media")missing=value;else if(arg==L"--mode")mode=value;else if(arg==L"--package-config"){if(value!=L"1")return 64;package_mode=true;}else return 64;}
    if(media.empty()||manifest.empty()||missing.empty()||GetFileAttributesW(missing.c_str())!=INVALID_FILE_ATTRIBUTES)return 64;
    for(auto& ctx:contexts){ctx.media=media;ctx.manifest=manifest;ctx.missing=missing;}
    if(mode!=L"clock"&&mode!=L"eof-tail")return 64;
    return is_child?(mode==L"eof-tail"?tail_child():child()):supervise(argc,argv);
}
