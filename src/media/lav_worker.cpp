// Canonical private LAV/AMStream transport. Derived from reviewed fixture ae0743a0.
// No renderer, clock policy, callbacks, fixture watchdog, or game addresses.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <amstream.h>
#include <ddstream.h>
#include <tlhelp32.h>
#include <initguid.h>
#include <LAVVideoSettings.h>
#include <LAVSplitterSettings.h>
#include "lav_worker.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <climits>

namespace x3m::media {
namespace {
using lav_detail::frame_bytes;
using lav_detail::slot_count;
static std::int64_t counter() noexcept {LARGE_INTEGER value{};QueryPerformanceCounter(&value);return value.QuadPart;}
static HRESULT win(bool ok) noexcept {return ok?S_OK:HRESULT_FROM_WIN32(GetLastError()?GetLastError():ERROR_GEN_FAILURE);}
static void copy_guid(std::array<unsigned char,16>& output,REFGUID value) noexcept {std::memcpy(output.data(),&value,16);}
static LRESULT CALLBACK worker_window(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static void messages(){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
struct Desired {Publication publication{};DWORD tick=0;std::int64_t qpc=0;std::uint64_t serial=0;};
struct Posted {Command command{};DWORD tick=0;std::int64_t qpc=0;};
struct Facts {bool load=false,connect=false,allocator=false,run=false,sample=false,update=false;HRESULT load_hr=S_OK;};
struct Channel {
    std::uint32_t mask=0;unsigned shift=0;
    bool init(std::uint32_t value){shift=0;mask=value;while(mask&&!(mask&1)){mask>>=1;++shift;}return mask&&!(mask&(mask+1));}
    unsigned char get(std::uint32_t value)const{return static_cast<unsigned char>((std::uint64_t((value>>shift)&mask)*255+mask/2)/mask);}
};
}

struct LavWorker::Impl {
    WorkerConfig config;
    std::shared_ptr<lav_detail::FrameStorage> frames;
    lav_detail::Mailbox<Posted> commands;
    lav_detail::Latest<Desired> desired;
    Publication main_desired{};std::uint64_t main_desired_serial=0;
    lav_detail::AssignmentQuiescence quiescence;
    lav_detail::Mailbox<WorkerEvent> ordinary;
    lav_detail::TerminalPublication terminal;
    std::atomic<WorkerState> state{WorkerState::starting};
    std::atomic<unsigned> shutdown{0},shutdown_tick{0},call_tick{0},progress{0};
    std::atomic<std::int32_t> error{0};
    HANDLE thread=nullptr;HMODULE module=nullptr;std::int64_t shutdown_qpc=0;
    std::uint64_t read_sequence=0;
    explicit Impl(WorkerConfig c):config(std::move(c)),frames(std::make_shared<lav_detail::FrameStorage>(config.observer)){}
    ~Impl(){if(thread)CloseHandle(thread);}
    struct Engine;
    static DWORD WINAPI entry(void*);
};

struct LavWorker::Impl::Engine {
    Impl& owner;
    explicit Engine(Impl& value) noexcept:owner(value){}
    Facts facts;
    Command current{};
    Publication desired{};
    bool have_desired=false,cleanup_mode=false,cancelled=false,building=false;
    std::uint64_t desired_serial=0;
    DWORD desired_tick=0;std::int64_t desired_qpc=0;
    HRESULT failure=S_OK;
    unsigned graph_serial=0,drains=0,produced=0,window_limit=0,complete_events=0;
    std::uint64_t sequence=0;
    DWORD anchor=0;std::int64_t anchor_qpc=0,target=0,last=-1;
    bool sample_eos=false,eof_announced=false,running=false,com=false,pending=false,session_live=false;
    bool all_guards_safe=true,all_events_clean=true,cohort_checked=false;
    int leased=-1;
    HWND window=nullptr;
    IAMMultiMediaStream* multi=nullptr;IGraphBuilder* graph=nullptr;
    IBaseFilter *source=nullptr,*decoder=nullptr;
    IMediaStream* media=nullptr;IDirectDrawMediaStream* ddmedia=nullptr;
    IMediaControl* control=nullptr;IMediaFilter* graph_filter=nullptr;IMediaEventEx* notify=nullptr;
    IDirectDraw7* dd7=nullptr;IDirectDraw* dd=nullptr;IUnknown* dd_identity=nullptr;
    IDirectDrawStreamSample* sample=nullptr;IDirectDrawSurface* surface=nullptr;
    RECT rect{};DDSURFACEDESC format{};Channel red,green,blue;

    void emit(ObservationKind kind,const char* label,std::int64_t begin=0,std::int64_t end=0,
              std::int64_t a=0,std::int64_t b=0,std::int64_t c=0,std::int64_t d=0,std::int64_t e=0,std::int64_t f=0,HRESULT hr=S_OK){
        if(!owner.config.observer)return;
        Observation o{};o.kind=kind;o.label=label;o.identity=identity(current);o.graph=graph_serial;
        o.begin=begin?begin:counter();o.end=end?end:o.begin;o.a=a;o.b=b;o.c=c;o.d=d;o.e=e;o.f=f;o.status=hr;owner.config.observer->observe(o);
    }
    void meta(Metadata m){if(owner.config.observer){m.identity=identity(current);m.graph=graph_serial;owner.config.observer->metadata(m);}}
    void set_anchor(const char* label,DWORD tick,std::int64_t qpc){
        anchor=tick;anchor_qpc=qpc;emit(ObservationKind::deadline_anchor,label,0,0,tick,current.epoch,sequence,qpc);
    }
    bool refresh(){
        Desired next;if(owner.desired.observe(next)){desired=next.publication;desired_tick=next.tick;desired_qpc=next.qpc;desired_serial=next.serial;have_desired=true;}
        if(cleanup_mode)return true;
        const bool active=!owner.shutdown.load(std::memory_order_acquire)&&have_desired&&desired.live&&identity(desired)==identity(current);
        cancelled=!active;return active;
    }
    template<class F> HRESULT call(const char* name,const char*,F fn){
        if(!refresh()&&!cleanup_mode&&!building){emit(ObservationKind::desired,"cancelled_before_call");return HRESULT_FROM_WIN32(ERROR_CANCELLED);}
        DWORD incoming=GetLastError();const auto begin=owner.config.observer?counter():0;owner.call_tick.store(GetTickCount(),std::memory_order_release);
        SetLastError(incoming);HRESULT hr=fn();DWORD error=GetLastError();const auto end=owner.config.observer?counter():0;
        owner.call_tick.store(0,std::memory_order_release);emit(ObservationKind::call,name,begin,end,0,0,0,0,0,0,hr);
        refresh();SetLastError(error);return hr;
    }
    bool need(const char* name,HRESULT hr){
        if(SUCCEEDED(hr)&&(!cancelled||cleanup_mode||building))return true;
        if(FAILED(hr)&&hr!=HRESULT_FROM_WIN32(ERROR_CANCELLED)){
            if(SUCCEEDED(failure))failure=hr;
            owner.error.store(hr,std::memory_order_release);emit(ObservationKind::failure,name,0,0,0,0,0,0,0,0,hr);
        }
        return false;
    }
    template<class T> void release(const char* name,T*& pointer){if(pointer){const bool previous=cleanup_mode;cleanup_mode=true;call(name,"Release",[&]{pointer->Release();return S_OK;});pointer=nullptr;cleanup_mode=previous;}}
#define NEED(n,a,e) do{if(!need(n,call(n,a,[&]{return (e);})))return false;}while(0)
#include "lav_graph_inc.h"
    Graph lav{*this};
    static bool terminal_status(HRESULT hr){return hr==S_OK||hr==MS_S_NOUPDATE||hr==MS_S_ENDOFSTREAM||hr==E_ABORT;}
    bool state_stopped(){OAFilterState state=State_Running;HRESULT hr=call("state_stopped","timeout0",[&]{return control->GetState(0,&state);});return need("state_stopped_exact",hr==S_OK&&state==State_Stopped?S_OK:E_UNEXPECTED);}
    bool observe_clock(const char* site,Epoch epoch){
        IReferenceClock* clock=nullptr;HRESULT hr=call("get_sync_source","graph_manager",[&]{return graph_filter->GetSyncSource(&clock);});
        const bool absent=clock==nullptr;emit(ObservationKind::clock,site,0,0,epoch,absent,0,0,0,0,hr);
        release("release_unexpected_clock",clock);return need("no_reference_clock",hr==S_OK&&absent?S_OK:E_UNEXPECTED);
    }
    bool initialize_service(){
        NEED("CoInitialize","worker_STA",CoInitialize(nullptr));com=true;
        // Immutable process-lifetime class; every HWND is still owned by its STA.
        const wchar_t* class_name=L"X3OwnedMediaWorker";
        WNDCLASSW wc{};wc.lpfnWndProc=worker_window;wc.hInstance=owner.module;wc.lpszClassName=class_name;
        HRESULT registered=call("RegisterClass","private_worker_class",[&]{
            if(RegisterClassW(&wc))return S_OK;
            if(GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return win(false);
            WNDCLASSW existing{};return GetClassInfoW(owner.module,class_name,&existing)&&existing.lpfnWndProc==worker_window&&existing.hInstance==owner.module?S_OK:E_UNEXPECTED;
        });if(!need("RegisterClass",registered))return false;
        NEED("CreateWindow","worker_hidden_top_level",(window=CreateWindowW(class_name,L"Private media worker",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr),win(window!=nullptr)));
        NEED("DirectDrawCreateEx","IDirectDraw7",DirectDrawCreateEx(nullptr,reinterpret_cast<void**>(&dd7),IID_IDirectDraw7,nullptr));
        HRESULT coop=call("SetCooperativeLevel","NORMAL_MULTITHREADED",[&]{return dd7->SetCooperativeLevel(window,DDSCL_NORMAL|DDSCL_MULTITHREADED);});
        emit(ObservationKind::cooperative,"worker_window",0,0,DDSCL_NORMAL|DDSCL_MULTITHREADED,GetWindowThreadProcessId(window,nullptr),0,0,0,0,coop);
        if(!need("cooperative",coop))return false;
        NEED("qi_dd","IDirectDraw",dd7->QueryInterface(IID_IDirectDraw,reinterpret_cast<void**>(&dd)));
        NEED("service_dd_identity","IID_IUnknown",dd->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&dd_identity)));
        emit(ObservationKind::service,"ready",0,0,reinterpret_cast<uintptr_t>(dd_identity),reinterpret_cast<uintptr_t>(owner.config.package_owner.get()));return true;
    }
    bool observe_notify(const char* site,unsigned operation,bool require_enabled){
        long flags=-1;HRESULT hr=call("get_notify_flags",site,[&]{return notify->GetNotifyFlags(&flags);});
        if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return false;
        emit(ObservationKind::notify,site,0,0,graph_serial,operation,flags,0,0,0,hr);
        return need("notify_flags_contract",hr==S_OK&&(!require_enabled||flags==0)?S_OK:E_UNEXPECTED);
    }
    static bool fatal_event(long code){return code==2||code==3||code==6||code==7||code==8||code==0x45;}
    bool drain_events(const char* site,bool cleanup=false){
        ++drains;unsigned total=0,batches=0;bool empty=false;HRESULT drain_status=S_OK;
        if(!observe_notify("before_drain",drains,true)){all_events_clean=false;return false;}
        const auto begin=counter();
        for(;;){
            const auto batch_begin=counter();unsigned retrieved=0;const char* outcome="More";HRESULT status=S_OK;
            DWORD now=GetTickCount();
            if(DWORD(now-anchor)>=10000){outcome="Deadline";status=HRESULT_FROM_WIN32(WAIT_TIMEOUT);}
            else if(!cleanup&&!refresh()){outcome="Cancelled";status=HRESULT_FROM_WIN32(ERROR_CANCELLED);}
            else for(unsigned i=0;i<32;++i){
                long code=0;LONG_PTR p1=0,p2=0;
                // Once acquired, params must be freed even if cancellation arrives
                // during GetEvent. Calls still observe the latest desired tuple.
                const bool previous=cleanup_mode;cleanup_mode=true;
                HRESULT got=call("worker_event_poll","timeout0",[&]{return notify->GetEvent(&code,&p1,&p2,0);});
                if(got==E_ABORT){cleanup_mode=previous;outcome="Empty";empty=true;break;}
                if(got!=S_OK){cleanup_mode=previous;outcome="PollError";status=got;break;}
                ++retrieved;++total;emit(ObservationKind::provider_event,"scalar",0,0,drains,total,code,p1,p2);
                HRESULT freed=call("worker_event_free","params",[&]{return notify->FreeEventParams(code,p1,p2);});cleanup_mode=previous;
                if(freed!=S_OK){outcome="FreeError";status=freed;break;}
                if(fatal_event(code)){
                    if(cleanup&&facts.load&&FAILED(facts.load_hr)&&!facts.connect&&!facts.allocator&&!facts.run&&!facts.sample&&!facts.update&&code==EC_ERRORABORT&&HRESULT(p1)==facts.load_hr)
                        emit(ObservationKind::source_error,"attributable_event",0,0,code,p1,p2);
                    else{outcome="ProviderError";status=E_FAIL;break;}
                }
                if(code==EC_COMPLETE){
                    ++complete_events;emit(ObservationKind::eof,"graph_complete",0,0,current.epoch,produced,complete_events,p1,p2);
                    if(complete_events!=1||p1!=S_OK||p2!=0){outcome="CompletionError";status=E_UNEXPECTED;break;}
                }
            }
            now=GetTickCount();if(status==S_OK&&DWORD(now-anchor)>=10000){outcome="Deadline";status=HRESULT_FROM_WIN32(WAIT_TIMEOUT);empty=false;}
            ++batches;emit(ObservationKind::event_batch,outcome,batch_begin,counter(),drains,batches,retrieved,anchor,now,0,status);
            drain_status=status;if(empty||status!=S_OK)break;
            messages();
        }
        emit(ObservationKind::drain,site,begin,counter(),current.epoch,drains,empty,anchor,total,batches);
        if(!empty&&drain_status!=HRESULT_FROM_WIN32(ERROR_CANCELLED)){all_events_clean=false;need("event_drain",FAILED(drain_status)?drain_status:E_FAIL);}return empty;
    }
    bool observe_dd_identity(){
        IDirectDraw* supplied=nullptr;IUnknown* same_identity=nullptr;
        HRESULT got=call("session_get_dd","public_GetDirectDraw",[&]{return ddmedia->GetDirectDraw(&supplied);});
        HRESULT queried=SUCCEEDED(got)&&supplied?call("session_dd_identity","IID_IUnknown",[&]{return supplied->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&same_identity));}):E_POINTER;
        const bool same=got==S_OK&&queried==S_OK&&same_identity&&same_identity==dd_identity;
        emit(ObservationKind::dd_identity,"session",0,0,graph_serial,reinterpret_cast<uintptr_t>(dd_identity),reinterpret_cast<uintptr_t>(same_identity),same,got,queried);
        release("observe_release_dd_identity",same_identity);release("observe_release_dd",supplied);return need("session_reuses_service_dd",same?S_OK:E_UNEXPECTED);
    }
    // https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew
    bool normalized_file(const std::wstring& path,std::wstring& output){
        HANDLE file=INVALID_HANDLE_VALUE;
        HRESULT opened=call("provider_identity_open","FILE_READ_ATTRIBUTES_share_all",[&]{file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);return win(file!=INVALID_HANDLE_VALUE);});
        if(FAILED(opened))return false;
        wchar_t buffer[32768]{};DWORD length=0;
        HRESULT normalized=call("provider_final_path","FILE_NAME_NORMALIZED_VOLUME_NAME_DOS",[&]{length=GetFinalPathNameByHandleW(file,buffer,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);return length&&length<32768?S_OK:win(false);});
        const bool previous=cleanup_mode;cleanup_mode=true;HRESULT closed=call("provider_identity_close","own_handle",[&]{return win(CloseHandle(file));});cleanup_mode=previous;
        if(normalized!=S_OK||closed!=S_OK)return false;
        output.assign(buffer,length);return true;
    }
    bool same_file_identity(const std::wstring& a,const std::wstring& b){
        std::wstring left,right;return normalized_file(a,left)&&normalized_file(b,right)&&_wcsicmp(left.c_str(),right.c_str())==0;
    }
    bool validate_cohort(){
        if(cohort_checked)return true;
        // Public current-process module snapshot, not ambiguous basename lookup.
        // Snapshot retry on ERROR_BAD_LENGTH stays inside this command's deadline.
        // https://learn.microsoft.com/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot
        static constexpr const wchar_t* names[]={L"LAVSplitter.ax",L"LAVVideo.ax",L"avcodec-lav-62.dll",L"avformat-lav-62.dll",L"avutil-lav-60.dll",L"avfilter-lav-11.dll",L"swresample-lav-6.dll",L"swscale-lav-9.dll",L"libbluray.dll"};
        unsigned counts[9]{};HANDLE snapshot=INVALID_HANDLE_VALUE;
        for(;;){
            HRESULT got=call("provider_module_snapshot","current_process",[&]{snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());return win(snapshot!=INVALID_HANDLE_VALUE);});
            if(got==S_OK)break;
            if(GetLastError()!=ERROR_BAD_LENGTH||DWORD(GetTickCount()-anchor)>=10000)return need("provider_module_snapshot",got);
        }
        MODULEENTRY32W entry{};entry.dwSize=sizeof entry;bool complete=false,valid=true;
        HRESULT got=call("provider_module_first","snapshot",[&]{if(Module32FirstW(snapshot,&entry))return S_OK;return GetLastError()==ERROR_NO_MORE_FILES?S_FALSE:win(false);});
        const std::wstring directory=owner.config.provider_manifest.substr(0,owner.config.provider_manifest.find_last_of(L"\\/"));
        while(got==S_OK){
            if(DWORD(GetTickCount()-anchor)>=10000){valid=false;break;}
            for(unsigned i=0;i<9;++i)if(_wcsicmp(entry.szModule,names[i])==0){
                ++counts[i];const std::wstring expected=directory+L"\\"+names[i];
                wchar_t module_path[32768]{};DWORD length=0;
                HRESULT located=call("provider_enumerated_module_path","snapshot_HMODULE",[&]{length=GetModuleFileNameW(entry.hModule,module_path,32768);return length&&length<32768?S_OK:win(false);});
                std::wstring actual_final,expected_final;
                const bool same=located==S_OK&&normalized_file(module_path,actual_final)&&normalized_file(expected,expected_final)&&_wcsicmp(actual_final.c_str(),expected_final.c_str())==0;
                Metadata m{};m.kind=MetadataKind::cohort;m.text=actual_final.c_str();m.expected=expected_final.c_str();m.a=i;m.b=counts[i];m.c=same;meta(m);
                valid=valid&&same&&counts[i]==1;
            }
            got=call("provider_module_next","snapshot",[&]{if(Module32NextW(snapshot,&entry))return S_OK;return GetLastError()==ERROR_NO_MORE_FILES?S_FALSE:win(false);});
        }
        complete=got==S_FALSE;
        const bool previous=cleanup_mode;cleanup_mode=true;HRESULT closed=call("provider_snapshot_close","own_handle",[&]{return win(CloseHandle(snapshot));});cleanup_mode=previous;
        for(unsigned count:counts)valid=valid&&count==1;
        cohort_checked=valid&&complete&&closed==S_OK;
        return need("provider_unique_local_cohort",cohort_checked?S_OK:E_UNEXPECTED);
    }
    bool construct(const std::wstring& path){
        if(!need("no_live_session",!session_live?S_OK:E_UNEXPECTED))return false;
        ++graph_serial;drains=0;session_live=true;produced=complete_events=0;facts={};sample_eos=eof_announced=false;
        emit(ObservationKind::session,"begin",0,0,graph_serial);
        NEED("activate_stream","CLSCTX_INPROC_SERVER",CoCreateInstance(CLSID_AMMultiMediaStream,nullptr,CLSCTX_INPROC_SERVER,IID_IAMMultiMediaStream,reinterpret_cast<void**>(&multi)));
        NEED("initialize","READ_NOGRAPHTHREAD",multi->Initialize(STREAMTYPE_READ,AMMSF_NOGRAPHTHREAD,nullptr));
        NEED("add_video","private_IDirectDraw_PrimaryVideo_flags0",multi->AddMediaStream(dd,&MSPID_PrimaryVideo,0,nullptr));
        NEED("get_graph","out",multi->GetFilterGraph(&graph));
        NEED("qi_control","IMediaControl",graph->QueryInterface(IID_IMediaControl,reinterpret_cast<void**>(&control)));
        NEED("qi_notify","graph_IMediaEventEx",graph->QueryInterface(IID_IMediaEventEx,reinterpret_cast<void**>(&notify)));
        if(!observe_notify("initial",0,false))return false;
        HRESULT enabled=call("set_notify_flags","zero",[&]{return notify->SetNotifyFlags(0);});
        emit(ObservationKind::notify,"enable",0,0,graph_serial,0,0,0,0,0,enabled);
        if(!need("notify_enable",enabled==S_OK?S_OK:E_UNEXPECTED)||!observe_notify("configured",0,true))return false;
        if(!lav.construct(owner.config.provider_manifest,path,multi,graph,&source,&decoder,true,true)||!lav.observe_assembly(owner.config.provider_manifest)||!lav.acquire_terminal_allocator(true)||!validate_cohort())return false;
        HRESULT support=call("support_seeking","renderer_TRUE_no_seek",[&]{return lav.sink->SupportSeeking(TRUE);});
        if(!need("support_seeking_exact",support==S_OK?S_OK:E_UNEXPECTED))return false;
        NEED("get_video","PrimaryVideo",multi->GetMediaStream(MSPID_PrimaryVideo,&media));
        NEED("qi_ddmedia","IDirectDrawMediaStream",media->QueryInterface(IID_IDirectDrawMediaStream,reinterpret_cast<void**>(&ddmedia)));
        if(!observe_dd_identity()||!state_stopped())return false;
        NEED("qi_graph_filter","graph_manager_IMediaFilter",graph->QueryInterface(IID_IMediaFilter,reinterpret_cast<void**>(&graph_filter)));
        HRESULT clock=call("set_sync_source","graph_manager_NULL",[&]{return graph_filter->SetSyncSource(nullptr);});
        if(!need("set_sync_source_exact",clock==S_OK?S_OK:E_UNEXPECTED))return false;
        return observe_clock("setup",0);
    }
    bool make_sample(){
        if(sample)return true;
        facts.sample=true;NEED("create_sample","NULL_NULL_0",ddmedia->CreateSample(nullptr,nullptr,0,&sample));
        NEED("get_surface","out_rect",sample->GetSurface(&surface,&rect));
        format.dwSize=sizeof format;NEED("surface_desc","public",surface->GetSurfaceDesc(&format));const auto& p=format.ddpfPixelFormat;
        emit(ObservationKind::surface,"caps_format",0,0,format.ddsCaps.dwCaps,format.dwWidth,format.dwHeight,format.lPitch,p.dwRGBBitCount,p.dwFlags);
        emit(ObservationKind::surface,"masks_rect",0,0,p.dwRBitMask,p.dwGBitMask,p.dwBBitMask,p.dwFourCC,rect.right-rect.left,rect.bottom-rect.top);
        const bool masks=!(p.dwRBitMask&p.dwGBitMask)&&!(p.dwRBitMask&p.dwBBitMask)&&!(p.dwGBitMask&p.dwBBitMask);
        return need("source_format",format.dwWidth==512&&format.dwHeight==512&&p.dwSize==sizeof p&&p.dwRGBBitCount==32&&(p.dwFlags&DDPF_RGB)&&!(p.dwFlags&DDPF_FOURCC)&&masks&&red.init(p.dwRBitMask)&&green.init(p.dwGBitMask)&&blue.init(p.dwBBitMask)?S_OK:E_NOTIMPL);
    }
    bool run(){facts.run=true;NEED("stream_run","RUN",multi->SetState(STREAMSTATE_RUN));NEED("run","none",control->Run());running=true;return observe_clock("after_run",current.epoch);}
    bool seek(){
        if(!drain_events("before_seek"))return false;
        NEED("seek_pause","none",control->Pause());if(!lav.decommit_transport_allocator())return false;
        NEED("seek_stop","none",control->Stop());if(!state_stopped()||!drain_events("seek_stopped"))return false;
        // Never replay a graph whose asynchronous completion arrived at the
        // stop boundary. The caller retires it; no SetPositions follows EOF.
        if(!need("no_completed_graph_replay",!sample_eos&&!complete_events?S_OK:E_UNEXPECTED))return false;
        if(sample){
            HRESULT a=call("seek_abort","COMPSTAT_ABORT_timeout0",[&]{return sample->CompletionStatus(COMPSTAT_ABORT,0);});
            HRESULT b=call("seek_settled","flags0_timeout0",[&]{return sample->CompletionStatus(0,0);});
            if(!need("seek_retired",terminal_status(a)&&terminal_status(b)?S_OK:E_UNEXPECTED))return false;
            pending=false;emit(ObservationKind::retirement,"seek_guard",0,0,1,0,1,State_Stopped,a,b);
            if(leased>=0){owner.frames->abandon_retired(unsigned(leased));leased=-1;}
        }
        LONGLONG before_current=-1,before_stop=-1,after_current=-1,after_stop=-1;
        HRESULT before=call("positions_before_seek","public_GetPositions",[&]{return lav.seeking->GetPositions(&before_current,&before_stop);});
        emit(ObservationKind::seek_position,"before",0,0,current.epoch,before_current,before_stop,0,0,0,before);
        if(!need("positions_before_exact",before==S_OK?S_OK:E_UNEXPECTED))return false;
        LONGLONG requested=std::int64_t(current.request.start_ms)*10000,position=requested,actual=-1;
        HRESULT hr=call("integer_seek","absolute_stop_NULL",[&]{return lav.seeking->SetPositions(&position,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning);});
        if(!need("integer_seek_exact_status",hr==S_OK?S_OK:E_UNEXPECTED))return false;
        NEED("integer_position","stopped",lav.seeking->GetCurrentPosition(&actual));
        HRESULT after=call("positions_after_seek","public_GetPositions",[&]{return lav.seeking->GetPositions(&after_current,&after_stop);});
        emit(ObservationKind::seek_position,"after",0,0,current.epoch,after_current,after_stop,0,0,0,after);
        if(!need("positions_stop_unchanged",after==S_OK&&before_stop==after_stop?S_OK:E_UNEXPECTED))return false;
        emit(ObservationKind::seek_position,"integer",0,0,current.epoch,requested,actual);
        if(!need("integer_position_exact",actual==requested?S_OK:E_UNEXPECTED)||!make_sample()||!observe_clock("after_seek",current.epoch))return false;
        target=requested;produced=complete_events=0;sample_eos=eof_announced=false;last=-1;running=false;
        if(current.playing){if(!run())return false;}else if(!observe_clock("after_run",current.epoch))return false;
        emit(ObservationKind::identity,"retained",0,0,current.epoch,reinterpret_cast<uintptr_t>(sample),reinterpret_cast<uintptr_t>(surface),reinterpret_cast<uintptr_t>(lav.terminal_allocator));return true;
    }
    bool copy(lav_detail::FrameStorage::Slot& slot){
        const auto begin=owner.config.observer?counter():0;DDSURFACEDESC desc{};desc.dwSize=sizeof desc;
        HRESULT locked=call("source_lock","WAIT_READONLY",[&]{return surface->Lock(nullptr,&desc,DDLOCK_WAIT|DDLOCK_READONLY,nullptr);});
        if(FAILED(locked))return need("source_lock",locked);
        const auto& p=desc.ddpfPixelFormat;
        bool valid=desc.lpSurface&&desc.dwWidth==512&&desc.dwHeight==512&&desc.lPitch>=2048&&p.dwRGBBitCount==32&&p.dwFlags==format.ddpfPixelFormat.dwFlags&&p.dwRBitMask==format.ddpfPixelFormat.dwRBitMask&&p.dwGBitMask==format.ddpfPixelFormat.dwGBitMask&&p.dwBBitMask==format.ddpfPixelFormat.dwBBitMask;
        if(valid)for(unsigned y=0;y<512;++y){const auto* src=static_cast<unsigned char*>(desc.lpSurface)+y*desc.lPitch;auto* dst=slot.pixels.data()+y*2048;
            for(unsigned x=0;x<512;++x){std::uint32_t pixel;std::memcpy(&pixel,src+x*4,4);dst[x*4]=blue.get(pixel);dst[x*4+1]=green.get(pixel);dst[x*4+2]=red.get(pixel);dst[x*4+3]=255;}}
        // A successful Lock is always paired, even if desired changed meanwhile.
        const bool previous=cleanup_mode;cleanup_mode=true;
        HRESULT unlocked=call("source_unlock","matched",[&]{return surface->Unlock(nullptr);});cleanup_mode=previous;
        emit(ObservationKind::source_copy,"normalized_BGRA",begin,counter(),current.epoch,sequence,frame_bytes,desc.lPitch,valid,0,unlocked);
        return need("source_copy_format",valid?S_OK:E_NOTIMPL)&&need("source_unlock",unlocked)&&refresh();
    }
    bool step(){
        if(!running||eof_announced||(window_limit&&produced>=window_limit))return true;
        if(!refresh())return false;
        if(sample_eos){
            if(!drain_events("eof_wait"))return false;
            if(complete_events==1){eof_announced=true;emit(ObservationKind::eof,"paired_empty",0,0,current.epoch,produced,complete_events);}
            return true;
        }
        if(leased<0){const unsigned index=unsigned(sequence%slot_count);
            if(!owner.frames->reserve(index,identity(current),graph_serial,sequence))return true;
            leased=int(index);pending=false;
            // Backpressure/paused ownership is not time spent in an Update.
            // Start one immutable envelope for the new public sample request.
            anchor=GetTickCount();anchor_qpc=counter();
            emit(ObservationKind::deadline_anchor,"sample_request",anchor_qpc,0,anchor,current.epoch,sequence);
        }
        if(DWORD(GetTickCount()-anchor)>=10000)return need("sample_request_deadline",HRESULT_FROM_WIN32(WAIT_TIMEOUT));
        const bool polling=pending;pending=true;facts.update=true;
        HRESULT hr=call(polling?"completion":"update","ASYNC_or_poll0",[&]{return polling?sample->CompletionStatus(0,0):sample->Update(SSUPDATE_ASYNC,nullptr,nullptr,0);});
        if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return false;
        if(hr==MS_S_PENDING)return refresh();
        if(hr==MS_S_ENDOFSTREAM){
            // Actual public terminal result. No READY publication for this lease;
            // only the subsequent physical guard can abandon WRITING storage.
            pending=false;sample_eos=true;emit(ObservationKind::eof,polling?"eof_completion":"eof_update",0,0,current.epoch,produced,sequence,0,0,0,hr);
            if(!drain_events("eof_wait"))return false;
            if(complete_events==1){eof_announced=true;emit(ObservationKind::eof,"paired_empty",0,0,current.epoch,produced,complete_events);}return true;
        }
        if(!need("sample_completed",hr==S_OK?S_OK:E_UNEXPECTED))return false;
        pending=false;anchor=GetTickCount();anchor_qpc=counter();
        emit(ObservationKind::deadline_anchor,"frame",anchor_qpc,0,anchor,current.epoch,sequence);
        auto& slot=owner.frames->slots[leased];STREAM_TIME start=0,end=0,position=0;
        NEED("sample_times","ready",sample->GetSampleTimes(&start,&end,&position));
        if(!need("sample_progress",start>=0&&end>start&&start>last?S_OK:E_UNEXPECTED)||!copy(slot))return false;
        last=start;slot.view.start=start+target;slot.view.end=end+target;
        if(!refresh())return false;
        owner.frames->publish(unsigned(leased));leased=-1;++sequence;++produced;owner.progress.store(GetTickCount(),std::memory_order_release);
        // Draining each completed frame bounds normal event accumulation without
        // fixture frame counts. The same fairness/deadline/event rules apply.
        if(window_limit&&produced==window_limit){emit(ObservationKind::window,"supplied",0,0,current.epoch,produced,target);return drain_events("window_supplied");}
        return drain_events("frame_completed");
    }
    bool physical_guard(bool seek_only=false){
        const bool partial=facts.load&&FAILED(facts.load_hr)&&!facts.connect&&!facts.allocator&&!facts.run&&!facts.sample&&!facts.update;
        if(!seek_only)emit(ObservationKind::facts,"cleanup",0,0,facts.load,facts.connect,facts.allocator,facts.run,facts.sample,facts.update,facts.load_hr);
        if(partial){
            HRESULT stop=control?call("partial_stop","public",[&]{return control->Stop();}):E_POINTER;
            HRESULT stream=multi?call("partial_stream_stop","public",[&]{return multi->SetState(STREAMSTATE_STOP);}):E_POINTER;
            OAFilterState state=State_Running;HRESULT got=control?call("partial_state","timeout0",[&]{return control->GetState(0,&state);}):E_POINTER;
            const bool safe=stop==S_OK&&stream==S_OK&&got==S_OK&&state==State_Stopped;
            emit(ObservationKind::retirement,"preconnection",0,0,safe,0,0,state);all_guards_safe=all_guards_safe&&safe;
            if(!safe)return need("unsafe_partial_retirement",E_UNEXPECTED);
        }else if(multi||sample||lav.terminal_allocator){
            const bool decommitted=lav.terminal_allocator&&lav.decommit_transport_allocator(true);
            HRESULT stop=control?call("cleanup_stop","none",[&]{return control->Stop();}):E_POINTER;
            HRESULT stream=multi?call("cleanup_stream_stop","STOP",[&]{return multi->SetState(STREAMSTATE_STOP);}):E_POINTER;
            OAFilterState state=State_Running;HRESULT got=control?call("cleanup_state","timeout0",[&]{return control->GetState(0,&state);}):E_POINTER;
            HRESULT abort=sample?call("cleanup_abort","ABORT_timeout0",[&]{return sample->CompletionStatus(COMPSTAT_ABORT,0);}):MS_S_NOUPDATE;
            HRESULT settled=sample?call("cleanup_settled","flags0_timeout0",[&]{return sample->CompletionStatus(0,0);}):MS_S_NOUPDATE;
            const bool safe=decommitted&&stop==S_OK&&stream==S_OK&&got==S_OK&&state==State_Stopped&&terminal_status(abort)&&terminal_status(settled);
            emit(ObservationKind::retirement,seek_only?"cancel_guard":"guard",0,0,safe,pending,decommitted,state,abort,settled);all_guards_safe=all_guards_safe&&safe;
            if(!safe)return need("unsafe_retirement_storage_retained",E_UNEXPECTED);
        }
        pending=false;running=false;
        if(leased>=0){owner.frames->abandon_retired(unsigned(leased));leased=-1;}
        return true;
    }
    bool cleanup_session(){
        if(!session_live)return true;
        const bool previous=cleanup_mode;cleanup_mode=true;
        const auto begin=counter();
        if(!physical_guard()){cleanup_mode=previous;return false;}
        const bool events_clean=!notify||drain_events("cleanup_stopped",true);
        release("release_sample",sample);release("release_surface",surface);release("release_ddmedia",ddmedia);release("release_media",media);
        lav.cleanup_interfaces();release("release_control",control);release("release_source",source);release("release_decoder",decoder);
        release("release_graph_filter",graph_filter);release("release_notify",notify);release("release_graph",graph);release("release_multi",multi);lav.cleanup_context();
        unsigned free=0;for(const auto& slot:owner.frames->slots)free+=slot.state.load(std::memory_order_acquire)==lav_detail::free_slot;
        emit(ObservationKind::session_cleanup,"safe_complete",0,0,graph_serial,free,1,events_clean);emit(ObservationKind::session,"end",0,0,graph_serial);
        emit(ObservationKind::operation,"session_cleanup",begin,counter(),graph_serial,events_clean);
        session_live=false;format={};cleanup_mode=previous;
        return events_clean;
    }
    bool cleanup_service(){
        if(session_live)return false;
        const bool previous=cleanup_mode;cleanup_mode=true;const auto begin=counter();
        release("release_dd_identity",dd_identity);release("release_dd",dd);release("release_dd7",dd7);
        if(window){HRESULT hr=call("destroy_worker_window","own",[&]{return win(DestroyWindow(window));});if(FAILED(hr)){cleanup_mode=previous;return false;}window=nullptr;}
        if(com){call("CoUninitialize","worker",[]{CoUninitialize();return S_OK;});com=false;}
        emit(ObservationKind::cleanup,"safe_complete",0,0,1,reinterpret_cast<uintptr_t>(owner.config.package_owner.get()));emit(ObservationKind::operation,"service_cleanup",begin,counter(),0,1);cleanup_mode=previous;return true;
    }
    bool terminal_acknowledged() const {return owner.terminal.ready_for_next();}
    bool reset_terminal(){
        return need("terminal_previous_observed",owner.terminal.reset(identity(current),graph_serial)?S_OK:E_UNEXPECTED);
    }
    void terminal_update(unsigned flags,HRESULT hr=S_OK,bool final=false){
        WorkerEvent value{};value.identity=identity(current);value.graph=graph_serial;value.flags=flags;value.status=hr;
        value.graph_guard_safe=all_guards_safe;value.graph_released=!session_live;value.events_clean=all_events_clean;
        if(!owner.terminal.update(value,final))owner.state.store(WorkerState::unsafe_retained,std::memory_order_release);
    }
    const std::wstring* resolve(SourceKey key) const {
        for(const auto& source_path:owner.config.sources)if(source_path.key==key)return &source_path.path;
        return nullptr;
    }
    void publish_failure(){
        HRESULT hr=FAILED(failure)?failure:E_FAIL;owner.error.store(hr,std::memory_order_release);owner.state.store(WorkerState::failed,std::memory_order_release);
        const bool preconnection=facts.load&&FAILED(facts.load_hr)&&!facts.connect&&!facts.allocator&&!facts.run&&!facts.sample&&!facts.update;
        emit(ObservationKind::source_error,"published",0,0,preconnection,graph_serial,0,0,0,0,hr);
        WorkerEvent value{};value.identity=identity(current);value.graph=graph_serial;value.status=hr;
        value.graph_guard_safe=all_guards_safe;value.graph_released=!session_live;value.events_clean=all_events_clean;
        if(!owner.terminal.failure(value))owner.state.store(WorkerState::unsafe_retained,std::memory_order_release);
    }
    bool cancel_current(){
        if(!session_live)return true;
        const bool previous=cleanup_mode;cleanup_mode=true;const auto begin=counter();
        bool safe=physical_guard(true);if(safe)safe=drain_events("cancel_stopped",true);
        emit(ObservationKind::operation,"cancel",begin,counter(),current.epoch,desired.epoch,safe);cleanup_mode=previous;return safe;
    }
    bool retire_source(unsigned flags=0){
        const bool clean=cleanup_session();
        if(!clean&&session_live){terminal_update(flags|worker_unsafe_retained,FAILED(failure)?failure:E_FAIL,true);return false;}
        if(!clean){flags|=worker_failed;owner.error.store(E_FAIL,std::memory_order_release);owner.state.store(WorkerState::failed,std::memory_order_release);}
        terminal_update(flags|worker_graph_retired,clean?S_OK:E_FAIL,true);return true;
    }
    bool execute(const Posted& posted){
        current=posted.command;anchor=posted.tick;anchor_qpc=posted.qpc;failure=S_OK;cancelled=false;if(!reset_terminal())return false;
        owner.error.store(S_OK,std::memory_order_release);owner.state.store(WorkerState::ready,std::memory_order_release);
        window_limit=owner.config.verification_pump?owner.config.verification_pump->picture_budget(current):0;
        emit(ObservationKind::command,"take",0,0,unsigned(current.kind),current.epoch,std::int64_t(current.request.start_ms)*10000,anchor,window_limit,current.playing);
        const std::wstring* path=resolve(current.request.source);
        if(!path)return need("source_key_resolved",E_INVALIDARG);
        if(!session_live){
            const auto begin=counter();building=true;bool built=construct(*path);building=false;
            emit(ObservationKind::operation,"construct",begin,counter(),graph_serial,built);
            if(!built||!refresh())return false;
        }
        if(current.kind!=CommandKind::construct){
            const auto begin=counter();const bool sought=seek();emit(ObservationKind::operation,"seek",begin,counter(),current.epoch,sought);if(!sought)return false;
        }
        WorkerEvent prepared{};prepared.identity=identity(current);prepared.graph=graph_serial;prepared.flags=worker_prepared;
        return owner.ordinary.push(prepared);
    }
    DWORD loop(){
        emit(ObservationKind::owner,"worker",0,0,GetCurrentThreadId());
        cleanup_mode=true;const auto service_begin=counter();bool initialized=initialize_service();
        emit(ObservationKind::operation,"service_initialize",service_begin,counter(),0,initialized);cleanup_mode=false;
        if(!initialized){publish_failure();cleanup_mode=true;if(!cleanup_service())return retain_unsafe();owner.state.store(WorkerState::retired,std::memory_order_release);terminal_update(worker_service_retired,S_OK,true);return 0;}
        owner.state.store(WorkerState::ready,std::memory_order_release);
        Posted next{};bool have_next=false;SourceKey graph_source=0;SessionHandle graph_session{};
        for(;;){
            messages();refresh();
            if(owner.shutdown.load(std::memory_order_acquire)){
                set_anchor("shutdown",owner.shutdown_tick.load(std::memory_order_acquire),owner.shutdown_qpc);
                cleanup_mode=true;
                if(session_live&&!retire_source())return retain_unsafe();
                if(!cleanup_service())return retain_unsafe();
                terminal_update(worker_service_retired,S_OK,true);owner.state.store(WorkerState::retired,std::memory_order_release);return 0;
            }
            if(!have_next&&terminal_acknowledged()&&!owner.ordinary.occupied())have_next=owner.commands.pop(next);
            if(have_next&&terminal_acknowledged()){
                if(!have_desired||!desired.live||!(identity(next.command)==identity(desired))||next.command.playing!=desired.playing){
                    emit(ObservationKind::desired,"discard_stale_command");have_next=false;continue;
                }
                // A revoked graph is stopped under the old identity before a
                // new command can seek it. Its complete event is drained first,
                // so physical EOF takes the fresh-graph branch below.
                if(session_live&&!(identity(next.command)==identity(current))&&(running||pending||leased>=0)){
                    set_anchor("pending_command",next.tick,next.qpc);if(!cancel_current())return retain_unsafe();
                }
                if(session_live&&(graph_source!=next.command.request.source||!(graph_session==next.command.session)||sample_eos||complete_events)){
                    set_anchor("pending_command",next.tick,next.qpc);if(!retire_source())return retain_unsafe();continue;
                }
                if(!session_live&&!owner.frames->all_free()){
                    // Old READY/READING storage is owned by main. Never force it
                    // FREE to make a replacement graph appear ready.
                    MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE);continue;
                }
                const FrameIdentity prior=identity(current);const bool same_graph=session_live;
                const bool continue_run=next.command.kind==CommandKind::run&&same_graph&&identity(next.command)==prior;
                bool ok;
                if(continue_run){current=next.command;set_anchor("pending_command",next.tick,next.qpc);failure=S_OK;cancelled=false;const auto begin=counter();ok=run();emit(ObservationKind::operation,"run",begin,counter(),current.epoch,ok);
                    if(ok){WorkerEvent prepared{};prepared.identity=identity(current);prepared.graph=graph_serial;prepared.flags=worker_prepared;ok=owner.ordinary.push(prepared);}}
                else ok=execute(next);
                graph_source=next.command.request.source;graph_session=next.command.session;have_next=false;
                if(!ok){
                    if(cancelled){if(!cancel_current())return retain_unsafe();}
                    else{publish_failure();if(session_live&&!retire_source(worker_failed))return retain_unsafe();}
                }
            }else if(session_live&&!refresh()){
                if(!desired.live){set_anchor("desired_cancel",desired_tick,desired_qpc);if(!retire_source())return retain_unsafe();}
                else if(running||pending||leased>=0){
                    set_anchor("desired_cancel",desired_tick,desired_qpc);if(!cancel_current())return retain_unsafe();
                }
            }else if(session_live&&running&&!owner.ordinary.occupied()){
                const bool ok=step();
                if(!ok){if(cancelled){if(!cancel_current())return retain_unsafe();}else{publish_failure();if(!retire_source(worker_failed))return retain_unsafe();}}
                else if(eof_announced){if(!retire_source(worker_source_eof))return retain_unsafe();}
            }
            // Only this STA can remove a pending command or retire its graph.
            // Main exclusively drains events/READY and releases READING leases.
            // A concurrent new desired publication invalidates the main-side
            // serial check even if it arrives immediately after this observation.
            const auto quiet_qpc=owner.config.observer&&have_desired&&!desired.live&&!session_live?counter():0;
            if(have_desired&&!desired.live&&!session_live&&owner.quiescence.publish(desired,desired_serial,
                    !owner.shutdown.load(std::memory_order_acquire)&&
                        (owner.state.load(std::memory_order_acquire)==WorkerState::ready||owner.state.load(std::memory_order_acquire)==WorkerState::failed),!session_live,
                    !have_next&&!owner.commands.occupied(),!owner.ordinary.occupied(),
                    terminal_acknowledged(),owner.frames->all_free())){
                if(owner.config.observer){Observation o{};o.kind=ObservationKind::desired;o.label="assignment_quiescent";
                    o.identity=identity(desired);o.graph=graph_serial;o.begin=o.end=quiet_qpc;
                    o.a=static_cast<std::int64_t>(desired_serial);o.b=desired.live;o.c=desired.playing;o.d=slot_count;
                    owner.config.observer->observe(o);}
            }
            MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
    }
    DWORD retain_unsafe(){
        all_guards_safe=false; // No current safe-retirement claim for retained state.
        owner.state.store(WorkerState::unsafe_retained,std::memory_order_release);terminal_update(worker_unsafe_retained,FAILED(failure)?failure:E_FAIL,true);
        // Fail closed: this STA, its refs, pixel storage and pinned module stay
        // alive. No main-thread wait and no process termination/unsafe Release.
        for(;;){messages();MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
    }
#undef NEED
};

DWORD WINAPI LavWorker::Impl::entry(void* argument){
    std::unique_ptr<std::shared_ptr<Impl>> holder(static_cast<std::shared_ptr<Impl>*>(argument));
    const auto self=*holder;holder.reset();Engine engine(*self);
    // Keep Engine and its raw COM references alive across the exception boundary.
    // Graph construction allocates; unwinding through the thread entry would
    // terminate the process. Exception cleanup is deliberately retention-only:
    // no allocator or sample may be released without its public guard.
    try{return engine.loop();}
    catch(const std::bad_alloc&){engine.failure=E_OUTOFMEMORY;}
    catch(...){engine.failure=E_UNEXPECTED;}
    engine.publish_failure();return engine.retain_unsafe();
}
LavWorker::~LavWorker(){request_shutdown();}
LavWorker::LavWorker(LavWorker&& other) noexcept:impl_(std::move(other.impl_)){}
LavWorker& LavWorker::operator=(LavWorker&& other) noexcept {if(this!=&other){request_shutdown();impl_=std::move(other.impl_);}return *this;}
bool LavWorker::start_service(WorkerConfig config) try {
    if(impl_||config.provider_manifest.empty()||config.sources.empty())return false;
    for(std::size_t i=0;i<config.sources.size();++i){if(config.sources[i].path.empty())return false;for(std::size_t j=0;j<i;++j)if(config.sources[j].key==config.sources[i].key)return false;}
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Impl::entry),&module))return false;
    // Process-lifetime pin is intentional. Unloadable plugin teardown is not a
    // supported capability of this checkpoint; no calls originate in DllMain.
    auto state=std::make_shared<Impl>(std::move(config));state->module=module;
    std::unique_ptr<std::shared_ptr<Impl>> argument(new std::shared_ptr<Impl>(state));
    state->progress.store(GetTickCount(),std::memory_order_release);
    if(state->config.observer){Observation o{};o.kind=ObservationKind::service;o.label="start";o.begin=o.end=counter();o.a=reinterpret_cast<uintptr_t>(module);o.b=reinterpret_cast<uintptr_t>(state->config.package_owner.get());state->config.observer->observe(o);}
    state->thread=CreateThread(nullptr,0,&Impl::entry,argument.get(),0,nullptr);
    if(!state->thread)return false;
    argument.release();impl_=std::move(state);return true;
}catch(...){return false;}
bool LavWorker::try_submit(const Command& command) noexcept {
    if(!impl_||impl_->shutdown.load(std::memory_order_acquire)||!impl_->main_desired.live||!(identity(command)==identity(impl_->main_desired))||command.playing!=impl_->main_desired.playing||command.request.start_ms<0)return false;
    const auto state=impl_->state.load(std::memory_order_acquire);if(state==WorkerState::retired||state==WorkerState::unsafe_retained)return false;
    Posted post{command,GetTickCount(),counter()};if(!impl_->commands.push(post))return false;
    if(impl_->config.observer){Observation o{};o.kind=ObservationKind::command;o.label="post";o.identity=identity(command);o.begin=o.end=post.qpc;
        o.a=unsigned(command.kind);o.b=command.epoch;o.c=std::int64_t(command.request.start_ms)*10000;o.d=post.tick;o.e=command.request.source;o.f=command.playing;impl_->config.observer->observe(o);}
    return true;
}
void LavWorker::publish_desired(const Publication& desired) noexcept {
    if(!impl_)return;
    if(impl_->main_desired_serial==UINT64_MAX){request_shutdown();return;}
    impl_->main_desired=desired;const Desired next{desired,GetTickCount(),counter(),++impl_->main_desired_serial};impl_->desired.publish(next);
    if(impl_->config.observer){Observation o{};o.kind=ObservationKind::desired;o.label="publish";o.identity=identity(desired);o.begin=o.end=next.qpc;o.a=next.tick;o.b=desired.live;o.c=desired.playing;o.d=static_cast<std::int64_t>(next.serial);impl_->config.observer->observe(o);}
}
FrameLease LavWorker::try_acquire_frame() noexcept {
    if(!impl_)return {};
    for(unsigned i=0;i<slot_count;++i){
        auto lease=impl_->frames->acquire(impl_->frames,unsigned(impl_->read_sequence%slot_count));if(!lease)return {};
        if(lease.view().sequence!=impl_->read_sequence){lease.release();return {};}
        ++impl_->read_sequence;
        const auto& desired=impl_->main_desired;
        if(!lav_detail::admit_ready(impl_->state.load(std::memory_order_acquire),impl_->shutdown.load(std::memory_order_acquire)!=0,desired,lease.view().identity)){lease.release();continue;}
        return lease;
    }
    return {};
}
bool LavWorker::poll_event(WorkerEvent& output) noexcept {
    if(!impl_)return false;
    if(impl_->terminal.poll(output))return true;
    return impl_->ordinary.pop(output);
}
bool LavWorker::poll_assignment_quiescent(const Publication& canceled) noexcept {
    if(!impl_||impl_->shutdown.load(std::memory_order_acquire))return false;
    const auto state=impl_->state.load(std::memory_order_acquire);
    if(state!=WorkerState::ready&&state!=WorkerState::failed)return false;
    return impl_->quiescence.poll(canceled,impl_->main_desired,impl_->main_desired_serial);
}
WorkerPoll LavWorker::poll_service_state() const noexcept {
    if(!impl_)return {};
    return {impl_->state.load(std::memory_order_acquire),impl_->error.load(std::memory_order_acquire),impl_->call_tick.load(std::memory_order_acquire),impl_->progress.load(std::memory_order_acquire)};
}
void LavWorker::request_shutdown() noexcept {
    if(impl_&&!impl_->shutdown.load(std::memory_order_relaxed)){const DWORD tick=GetTickCount();const auto qpc=counter();impl_->shutdown_qpc=qpc;impl_->shutdown_tick.store(tick,std::memory_order_relaxed);impl_->shutdown.store(1,std::memory_order_release);
        if(impl_->config.observer){Observation o{};o.kind=ObservationKind::desired;o.label="shutdown";o.identity=identity(impl_->main_desired);o.begin=o.end=qpc;o.a=tick;impl_->config.observer->observe(o);}}
}
bool LavWorker::poll_retired() const noexcept {
    if(!impl_)return true;
    DWORD code=STILL_ACTIVE;return GetExitCodeThread(impl_->thread,&code)&&code!=STILL_ACTIVE&&impl_->state.load(std::memory_order_acquire)==WorkerState::retired;
}
} // namespace x3m::media
