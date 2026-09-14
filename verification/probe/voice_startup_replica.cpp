// Startup replica: the game's load-time media sequence without the game.
// Follows docs/reverse-engineering/voice-startup-sequence.md: 004cf460 builds
// every stream (Initialize NOGRAPHTHREAD, primary audio, OpenFile, sample,
// DirectSound ring, SetState(RUN) then IMediaControl::Pause), the main thread
// then alternates the 004d34b0 PeekMessage drain with the 00498370 poll that
// issues at most one async Update per object and re-checks CompletionStatus.
// Savegame-restored streams are constructed and never pumped. Documented COM
// only; authored code; no audible output (the DirectSound ring is never played).
// A watchdog thread reports any step that has not returned within 15 s and
// leaves the process alive so the runner can sample its threads.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <dshow.h>
#include <amstream.h>
#include <austream.h>
#include <dsound.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static_assert(sizeof(void*)==4,"probe must use the game's x86 COM ABI");
static_assert((DSBCAPS_LOCSOFTWARE|DSBCAPS_CTRLVOLUME|DSBCAPS_GETCURRENTPOSITION2)==0x10088,"SDK DirectSound flags required");
static const DWORD WATCHDOG_MS=15000;
static LARGE_INTEGER frequency;
static HANDLE watchdog_stop;
static DWORD owner_thread;
static std::atomic<const char*> step_name{nullptr};
static std::atomic<int> step_stream{0};
static std::atomic<DWORD> step_began{0};
static std::atomic<bool> hung{false};

static DWORD WINAPI watchdog(void*) {
    while(WaitForSingleObject(watchdog_stop,100)==WAIT_TIMEOUT) {
        const DWORD began=step_began.load();const char* name=step_name.load();
        if(!began||!name||hung.load())continue;
        const DWORD elapsed=GetTickCount()-began;
        if(elapsed>WATCHDOG_MS&&elapsed<0x80000000u) {
            hung.store(true);
            std::printf("REPLICA_HUNG step=%s stream=%d elapsed_ms=%lu thread=%lu\n",name,step_stream.load(),static_cast<unsigned long>(elapsed),static_cast<unsigned long>(owner_thread));
            std::fflush(stdout);
        }
    }
    return 0;
}
// 004d34b0 active-mode pump: PeekMessageA, then the unbounded drain loop
// 004d3532..004d3562 (GetMessageA/TranslateMessage/DispatchMessageA/PeekMessageA).
static unsigned pump() {
    MSG m;unsigned n=0;
    if(PeekMessageA(&m,nullptr,0,0,PM_NOREMOVE)) {
        do {
            if(GetMessageA(&m,nullptr,0,0)<=0)break;
            TranslateMessage(&m);DispatchMessageA(&m);++n;
        } while(PeekMessageA(&m,nullptr,0,0,PM_NOREMOVE));
    }
    return n;
}
struct Trace {
    int seq{};
    template<class F> HRESULT step(int stream,const char* name,int attempt,bool loud,F f) {
        if(GetCurrentThreadId()!=owner_thread)ExitProcess(125);
        if(loud){std::printf("REPLICA_BEGIN seq=%d stream=%d name=%s attempt=%d\n",seq,stream,name,attempt);std::fflush(stdout);}
        LARGE_INTEGER a{},b{};QueryPerformanceCounter(&a);
        const DWORD now=GetTickCount();step_name.store(name);step_stream.store(stream);step_began.store(now?now:1); // never ahead of the watchdog's clock; 0 means idle
        HRESULT hr=f();
        step_began.store(0);step_name.store(nullptr);
        QueryPerformanceCounter(&b);
        if(loud){std::printf("REPLICA_STAGE seq=%d stream=%d name=%s attempt=%d hr=%08lx wall_ms=%.3f\n",seq++,stream,name,attempt,static_cast<unsigned long>(hr),1000.*(b.QuadPart-a.QuadPart)/frequency.QuadPart);std::fflush(stdout);}
        return hr;
    }
    template<class F> HRESULT twice(int stream,const char* name,F f) {
        HRESULT hr=E_FAIL;
        for(int i=1;i<=2;++i){hr=step(stream,name,i,true,f);if(SUCCEEDED(hr))break;}
        return hr;
    }
    template<class T> void release(int stream,const char* name,T*& p) {
        if(p){step(stream,name,1,true,[&]{p->Release();return S_OK;});p=nullptr;}
    }
};
enum Mode { MODE_GAME, MODE_NOPAUSE, MODE_EARLY_UPDATE, MODE_SINGLE, MODE_EXPLICIT };
struct Stream {
    Trace& t;int index;IAMMultiMediaStream* multi{};IMediaStream* media{};IAudioMediaStream* audio{};
    IGraphBuilder* graph{};IBaseFilter* source{};IAudioData* data{};IAudioStreamSample* sample{};IDirectSoundBuffer* buffer{};
    IMediaPosition* position{};IMediaControl* control{};std::vector<BYTE> pcm;WAVEFORMATEX format{};
    double duration{};const char* fatal="none";HRESULT fatal_hr=S_OK;bool created{};HRESULT early_update_hr=E_PENDING;
    int state{1};bool pending{};
    Stream(Trace& x,int i):t(x),index(i){}
    bool need(const char* name,HRESULT hr) {
        if(FAILED(hr)){if(!std::strcmp(fatal,"none")){fatal=name;fatal_hr=hr;std::printf("REPLICA_FAILURE stream=%d name=%s hr=%08lx\n",index,name,static_cast<unsigned long>(hr));std::fflush(stdout);}return false;}
        return true;
    }
#define NEED(name,expr) do{if(!need(name,t.step(index,name,1,true,[&]{return (expr);})))return false;}while(0)
    bool create(const wchar_t* path,IDirectSound8* sound,Mode mode) {
        NEED("activate_stream",CoCreateInstance(CLSID_AMMultiMediaStream,nullptr,CLSCTX_INPROC_SERVER,IID_IAMMultiMediaStream,reinterpret_cast<void**>(&multi)));
        NEED("initialize",multi->Initialize(STREAMTYPE_READ,AMMSF_NOGRAPHTHREAD,nullptr));
        HRESULT added=E_FAIL;
        for(int i=1;i<=2;++i){added=t.step(index,"add_audio",i,true,[&]{return multi->AddMediaStream(nullptr,&MSPID_PrimaryAudio,0,&media);});if(SUCCEEDED(added))break;}
        if(!need("add_audio",added))return false;
        NEED("audio_qi_pre",media->QueryInterface(IID_IAudioMediaStream,reinterpret_cast<void**>(&audio)));
        WAVEFORMATEX wanted{WAVE_FORMAT_PCM,1,44100,88200,2,16,0};
        NEED("set_pcm",audio->SetFormat(&wanted));
        NEED("get_graph",multi->GetFilterGraph(&graph));
        HRESULT opened=E_FAIL;
        if(mode==MODE_EXPLICIT) {
            // 004d00f5 alternative under DAT_00606f34+0x100 & 0x4000: AddSourceFilter, FindPin("Output"),
            // Render (retried once), then the OpenFile fallback on the same graph (native probe route 2).
            opened=t.step(index,"add_source",1,true,[&]{return graph->AddSourceFilter(path,L"X File Source",&source);});
            if(SUCCEEDED(opened)) {
                IPin* pin{};opened=t.step(index,"find_output",1,true,[&]{return source->FindPin(L"Output",&pin);});
                if(SUCCEEDED(opened))for(int i=1;i<=2;++i){opened=t.step(index,"render",i,true,[&]{return graph->Render(pin);});if(SUCCEEDED(opened))break;}
                t.release(index,"release_output_pin",pin);
            }
            if(FAILED(opened))t.release(index,"release_failed_source",source);
        }
        if(FAILED(opened))opened=t.twice(index,"open_file",[&]{return multi->OpenFile(path,0);});
        if(!need("open_file",opened))return false;
        t.release(index,"release_audio_pre",audio);t.release(index,"release_media_pre",media);
        NEED("get_audio",multi->GetMediaStream(MSPID_PrimaryAudio,&media));
        NEED("audio_qi_post",media->QueryInterface(IID_IAudioMediaStream,reinterpret_cast<void**>(&audio)));
        NEED("get_format",audio->GetFormat(&format));
        std::printf("REPLICA_FORMAT stream=%d tag=%u rate=%lu channels=%u bits=%u align=%u avg=%lu\n",index,format.wFormatTag,static_cast<unsigned long>(format.nSamplesPerSec),format.nChannels,format.wBitsPerSample,format.nBlockAlign,static_cast<unsigned long>(format.nAvgBytesPerSec));
        if(format.wFormatTag!=WAVE_FORMAT_PCM||!format.nBlockAlign||!format.nAvgBytesPerSec||format.nAvgBytesPerSec>1000000)return need("pcm_domain",E_INVALIDARG);
        pcm.resize(format.nAvgBytesPerSec*2); // +0x60: two seconds per application sample
        NEED("activate_audio_data",CoCreateInstance(CLSID_AMAudioData,nullptr,CLSCTX_INPROC_SERVER,IID_IAudioData,reinterpret_cast<void**>(&data)));
        NEED("set_buffer",data->SetBuffer(static_cast<DWORD>(pcm.size()),pcm.data(),0));
        NEED("data_format",data->SetFormat(&format));
        NEED("create_sample",audio->CreateSample(data,0,&sample));
        if(sound) {
            DSBUFFERDESC desc{};desc.dwSize=sizeof(desc);desc.dwFlags=0x10088;desc.dwBufferBytes=format.nAvgBytesPerSec*5;desc.lpwfxFormat=&format;
            t.step(index,"create_dsound_buffer",1,true,[&]{return sound->CreateSoundBuffer(&desc,&buffer,nullptr);});
        }
        NEED("position_qi",graph->QueryInterface(IID_IMediaPosition,reinterpret_cast<void**>(&position)));
        NEED("control_qi",graph->QueryInterface(IID_IMediaControl,reinterpret_cast<void**>(&control)));
        t.step(index,"get_duration",1,true,[&]{return position->get_Duration(&duration);});
        LONG seekable{};t.step(index,"can_seek_forward",1,true,[&]{return position->CanSeekForward(&seekable);}); // 004d03c4
        NEED("stream_run",multi->SetState(STREAMSTATE_RUN)); // 004d03f5
        if(mode==MODE_EARLY_UPDATE) {
            early_update_hr=t.step(index,"early_update",1,true,[&]{return sample->Update(SSUPDATE_ASYNC,nullptr,nullptr,0);});
            pending=early_update_hr==MS_S_PENDING;
        }
        if(mode!=MODE_NOPAUSE)NEED("control_pause",control->Pause()); // 004d0407
        created=true;return true;
    }
#undef NEED
    // One 004d0700 visit: fixed sequence, never a spin.
    // state 1: Update (two attempts on failure); 2: CompletionStatus(0,0) advances on S_OK only; 4: consume, back to 1.
    const char* poll(HRESULT& hr,DWORD& bytes) {
        bytes=0;
        if(buffer){DWORD play{},write{};hr=t.step(index,"ds_position",1,false,[&]{return buffer->GetCurrentPosition(&play,&write);});if(FAILED(hr))return "ds_error";}
        if(state==2) {
            hr=t.step(index,"completion_status",1,false,[&]{return sample->CompletionStatus(0,0);});
            if(hr==S_OK){state=4;pending=false;return "completed";}
            if(hr==MS_S_ENDOFSTREAM)return "eos_status_stuck"; // 004d0769: nonzero never advances
            return "pending";
        }
        if(state==4) {
            DWORD size{},actual{};BYTE* ptr{};STREAM_TIME start{},end{},now{};
            hr=t.step(index,"sample_info",1,false,[&]{return data->GetInfo(&size,&ptr,&actual);});
            if(FAILED(hr))return "info_error";
            hr=t.step(index,"sample_times",1,false,[&]{return sample->GetSampleTimes(&start,&end,&now);});
            bytes=actual;state=1;return "consumed";
        }
        for(int i=1;i<=2;++i){hr=t.step(index,"sample_update",i,false,[&]{return sample->Update(SSUPDATE_ASYNC,nullptr,nullptr,0);});if(SUCCEEDED(hr))break;}
        if(hr==S_OK){state=4;pending=false;return "immediate";}
        if(hr==MS_S_ENDOFSTREAM){pending=false;return "eos";}
        if(hr==MS_S_PENDING)pending=true;
        state=2;return FAILED(hr)?"update_error":"queued";
    }
    void cleanup() {
        if(pending) {
            // Bounded abort of the outstanding update before releasing the buffer it writes.
            HRESULT hr=t.step(index,"sample_cancel",1,true,[&]{
                const DWORD begin=GetTickCount();
                do{pump();HRESULT done=sample->CompletionStatus(COMPSTAT_ABORT|COMPSTAT_WAIT,10);if(done!=MS_S_PENDING)return done;}while(DWORD(GetTickCount()-begin)<2000);
                return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
            });
            if(hr==MS_S_PENDING||hr==HRESULT_FROM_WIN32(WAIT_TIMEOUT)){std::printf("REPLICA_ABORT stage=sample_cancel_unresolved stream=%d hr=%08lx\n",index,static_cast<unsigned long>(hr));std::fflush(stdout);ExitProcess(126);}
            pending=false;
        }
        if(buffer)t.step(index,"buffer_stop",1,true,[&]{return buffer->Stop();});
        if(control)t.step(index,"control_stop",1,true,[&]{return control->Stop();});
        if(multi)t.step(index,"stream_stop",1,true,[&]{return multi->SetState(STREAMSTATE_STOP);});
        t.release(index,"release_position",position);t.release(index,"release_control",control);
        t.release(index,"release_buffer",buffer);t.release(index,"release_sample",sample);t.release(index,"release_data",data);
        t.release(index,"release_audio",audio);t.release(index,"release_media",media);
        if(graph&&source)t.step(index,"remove_source",1,true,[&]{return graph->RemoveFilter(source);});
        t.release(index,"release_source",source);
        t.release(index,"release_graph",graph);t.release(index,"release_multimedia",multi);pcm.clear();
    }
};
static Mode parse_mode(const wchar_t* text,bool& ok) {
    ok=true;
    if(!wcscmp(text,L"game"))return MODE_GAME;
    if(!wcscmp(text,L"nopause"))return MODE_NOPAUSE;
    if(!wcscmp(text,L"early_update"))return MODE_EARLY_UPDATE;
    if(!wcscmp(text,L"single"))return MODE_SINGLE;
    if(!wcscmp(text,L"explicit"))return MODE_EXPLICIT;
    ok=false;return MODE_GAME;
}
int wmain(int argc,wchar_t** argv) {
    // argv: mode dwell_ms path1 path2 path3 ; stream 1 is the played/pumped one, 2 and 3 are restored (never pumped).
    // dwell_ms: loading-screen time after each construction and before play, spent pumping only (0..20000).
    if(argc!=6)return 2;
    bool ok{};const Mode mode=parse_mode(argv[1],ok);if(!ok)return 2;
    const int dwell_ms=_wtoi(argv[2]);if(dwell_ms<0||dwell_ms>20000)return 2;
    const int streams=mode==MODE_SINGLE?1:3;
    setvbuf(stdout,nullptr,_IONBF,0);owner_thread=GetCurrentThreadId();QueryPerformanceFrequency(&frequency);
    watchdog_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);HANDLE watcher=CreateThread(nullptr,0,watchdog,nullptr,0,nullptr);
    if(!watchdog_stop||!watcher)return 3;
    char mode_text[16];std::snprintf(mode_text,sizeof mode_text,"%ls",argv[1]);
    std::printf("REPLICA_HEADER schema=1 mode=%s streams=%d dwell_ms=%d watchdog_ms=%lu thread=%lu audible=0\n",mode_text,streams,dwell_ms,static_cast<unsigned long>(WATCHDOG_MS),static_cast<unsigned long>(owner_thread));
    Trace t;
    HRESULT init=t.step(0,"co_initialize",1,true,[]{return CoInitialize(nullptr);});
    if(FAILED(init)){std::printf("REPLICA_ABORT stage=co_initialize hr=%08lx\n",static_cast<unsigned long>(init));return 0;}
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"X3VoiceStartupReplica";RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"X3 startup replica",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
    IDirectSound8* sound{};
    HRESULT ds=t.step(0,"directsound_create",1,true,[&]{return DirectSoundCreate8(&DSDEVID_DefaultPlayback,&sound,nullptr);});
    HRESULT coop=E_POINTER;if(sound&&window)coop=t.step(0,"directsound_cooperative",1,true,[&]{return sound->SetCooperativeLevel(window,DSSCL_PRIORITY);});
    if(FAILED(coop)&&sound){sound->Release();sound=nullptr;}
    std::printf("REPLICA_STARTUP ds_hr=%08lx coop_hr=%08lx ds_present=%d window=%d\n",static_cast<unsigned long>(ds),static_cast<unsigned long>(coop),sound!=nullptr,window!=nullptr);
    std::vector<Stream*> all;
    auto dwell=[&](const char* where){
        if(!dwell_ms)return;
        const DWORD begin=GetTickCount();unsigned pumps=0;
        while(DWORD(GetTickCount()-begin)<DWORD(dwell_ms)&&!hung.load()){t.step(0,"pump",1,false,[&]{pump();return S_OK;});++pumps;Sleep(5);}
        std::printf("REPLICA_DWELL after=%s pumps=%u elapsed_ms=%lu\n",where,pumps,static_cast<unsigned long>(GetTickCount()-begin));std::fflush(stdout);
    };
    // Load order: the restored (never pumped) streams are built first, the
    // played stream last; each constructor is followed by one pump as the
    // asset loaders do between 00498370 visits.
    for(int n=1;n<=streams;++n) {
        const int index=streams==1?1:(n==streams?1:n+1);
        Stream* s=new Stream(t,index);all.push_back(s);
        t.step(index,"pump",1,true,[&]{pump();return S_OK;});
        const bool made=s->create(argv[2+index],sound,mode);
        std::printf("REPLICA_STREAM stream=%d created=%d role=%s fatal=%s hr=%08lx duration=%.3f early_update_hr=%08lx\n",index,made,index==1?"played":"restored",s->fatal,static_cast<unsigned long>(s->fatal_hr),s->duration,static_cast<unsigned long>(s->early_update_hr));
        std::fflush(stdout);dwell(index==1?"played_construction":"restored_construction");
    }
    Stream* played=nullptr;for(Stream* s:all)if(s->index==1)played=s;
    if(played&&played->created) {
        // 004d1870 play path: IMediaControl::Run marks the record playing (bit 2), then 00498370 pumps it.
        HRESULT run=t.twice(1,"control_run",[&]{return played->control->Run();});
        if(SUCCEEDED(run)) {
            const DWORD begin=GetTickCount();int cycles=0,completed=0,queued=0,pending_polls=0,errors=0,eos=0,stuck=0;std::uint64_t bytes=0;
            const char* last="";HRESULT hr=S_OK;
            while(DWORD(GetTickCount()-begin)<30000&&completed<5&&!eos&&!stuck&&errors<4&&!hung.load()) {
                t.step(1,"pump",1,false,[&]{pump();return S_OK;});
                DWORD got{};const char* outcome=played->poll(hr,got);++cycles;
                if(!std::strcmp(outcome,"consumed")){++completed;bytes+=got;}
                else if(!std::strcmp(outcome,"queued"))++queued;
                else if(!std::strcmp(outcome,"pending"))++pending_polls;
                else if(!std::strcmp(outcome,"eos"))++eos;
                else if(!std::strcmp(outcome,"eos_status_stuck"))++stuck;
                else if(std::strstr(outcome,"error"))++errors;
                if(std::strcmp(outcome,last)){std::printf("REPLICA_POLL cycle=%d state=%d outcome=%s hr=%08lx bytes=%lu elapsed_ms=%lu\n",cycles,played->state,outcome,static_cast<unsigned long>(hr),static_cast<unsigned long>(got),static_cast<unsigned long>(GetTickCount()-begin));last=outcome;}
                Sleep(5); // frame cadence stand-in; the game never waits on a handle
            }
            std::printf("REPLICA_PLAY stream=1 cycles=%d completed=%d queued=%d pending_polls=%d eos=%d stuck=%d errors=%d bytes=%llu elapsed_ms=%lu\n",cycles,completed,queued,pending_polls,eos,stuck,errors,static_cast<unsigned long long>(bytes),static_cast<unsigned long>(GetTickCount()-begin));
        } else std::printf("REPLICA_PLAY stream=1 cycles=0 completed=0 queued=0 pending_polls=0 eos=0 stuck=0 errors=1 bytes=0 elapsed_ms=0\n");
        std::fflush(stdout);
    }
    for(auto it=all.rbegin();it!=all.rend();++it){(*it)->cleanup();delete *it;}
    t.release(0,"release_directsound",sound);if(window)DestroyWindow(window);
    t.step(0,"co_uninitialize",1,true,[]{CoUninitialize();return S_OK;});
    SetEvent(watchdog_stop);WaitForSingleObject(watcher,1000);CloseHandle(watcher);CloseHandle(watchdog_stop);
    std::printf("REPLICA_COMPLETE streams=%d audible=0\n",streams);
    return 0;
}
