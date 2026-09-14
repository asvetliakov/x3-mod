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
// Modes game-ds / game-ds-stereo add the section-10 DirectSound initialisation
// (004de060: primary buffer 0xd1/0x11/0x1, Play(LOOPING), SetFormat, 3D
// listener), the game's visible 640x480 window, the route-dependent pre-OpenFile
// SetFormat channel count, no get_Duration, and the synchronous 004d1d40
// destructor order (Stop / Stop / SetState(STOP) issued twice) on every path.
// Mode game-dmo adds what user run 12's CX_LOG trace shows the game doing
// between GetFilterGraph and OpenFile: CoCreateInstance(CLSID_DMOWrapperFilter),
// IDMOWrapperFilter::Init(CLSID_CWMSPDecMediaObject, DMOCATEGORY_AUDIO_DECODER)
// (two attempts) and AddFilter(NULL name) whatever Init returned. Teardown is
// section 12's straight-line 004d1c20: RemoveFilter(slot); Release(slot) per
// slot with every HRESULT discarded, then the graph Release; amstream holds the
// last graph reference, so the unfixed mode hangs at release_multimedia, where
// Wine's filter_graph_Release loop spins (run plugin-v3-game-dmo-teardown12).
// Mode game-dmo-fallback is the production hook's action: when Init returns
// REGDB_E_CLASSNOTREG it re-issues Init with the registered WMA decoder DMO
// (CLSID_CWMADecMediaObject) and the same category, and continues unchanged.
// Mode game-dmo-skip is the alternative: on Init failure release the wrapper
// and null the slot so AddFilter is skipped.
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
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../src/proxy/voice_dmo_fallback.h"

static_assert(sizeof(void*)==4,"probe must use the game's x86 COM ABI");
// Fixture-side definitions of the proxy symbols the production hook links
// against: its log lines go to stdout (the runner keeps every
// voice_dmo_fallback* line), the executable identity check is the fixture's.
namespace x3m { void log(const char* format,...){va_list args;va_start(args,format);std::vprintf(format,args);va_end(args);std::putchar('\n');std::fflush(stdout);} void log_flush(){std::fflush(stdout);} }
namespace x3m::object_trace { bool executable_verified(){return true;} }
// dmodshow.h/dmoreg.h/wmcodecdsp.h are not in this MinGW; documented GUIDs, local names.
DEFINE_GUID(CLSID_DMOWrapperFilter_local,0x94297043,0xbd82,0x4dfd,0xb0,0xde,0x81,0x77,0x73,0x9c,0x6d,0x20);
DEFINE_GUID(IID_IDMOWrapperFilter_local,0x52d6f586,0x9f0f,0x4824,0x8f,0xc8,0xe3,0x2c,0xa0,0x49,0x30,0xc2);
DEFINE_GUID(CLSID_CWMSPDecMediaObject_local,0x874131cb,0x4ecc,0x443b,0x89,0x48,0x74,0x6b,0x89,0x59,0x5d,0x20);
DEFINE_GUID(CLSID_CWMADecMediaObject_local,0x2eeb4adf,0x4578,0x4d10,0xbc,0xa7,0xbb,0x95,0x5f,0x56,0x32,0x0a);
DEFINE_GUID(DMOCATEGORY_AUDIO_DECODER_local,0x57f2db8b,0xe6bb,0x4513,0x9d,0x43,0xdc,0xd2,0xa6,0x59,0x31,0x25);
struct IDMOWrapperFilterLocal : public IUnknown { virtual HRESULT STDMETHODCALLTYPE Init(REFCLSID clsid,REFCLSID category)=0; };
// Mode game-dmo-hook: the constructor's Init loop 004cfd0e..004cfd7c as
// machine code with the game's register contract (EBX = media object whose
// +0x9c holds the wrapper, EDI = 0, [ESP+0x14] = the IDMOWrapperFilter view,
// [ESP+0x10] = attempt counter, EAX = Init's HRESULT at the site) so the
// production hook (src/proxy/voice_dmo_fallback.cpp) is installed through
// engine_patch on `replica_init_site`, which carries the site's eight bytes
// `8b f0 81 fe 0e 00 07 80` exactly, and its emitted stub, dispatcher and tail
// execute as in the game. `replica_out_of_memory` stands in for 004b8b60 (an
// E_OUTOFMEMORY report the fixture never reaches). The witness records what
// the game code after the site would consume: ESI (the HRESULT it tests),
// EDI, EBX and ESP at the return.
extern "C" {
HRESULT replica_media_init(void* object);
extern unsigned char replica_init_site[];
void replica_out_of_memory(){}
std::uint32_t replica_site_witness[4];
extern const GUID replica_iid_wrapper=IID_IDMOWrapperFilter_local;
extern const GUID replica_clsid_speech=CLSID_CWMSPDecMediaObject_local;
extern const GUID replica_category_audio=DMOCATEGORY_AUDIO_DECODER_local;
}
asm(".text\n.globl _replica_media_init\n_replica_media_init:\n"
"pushl %ebx\n pushl %esi\n pushl %edi\n pushl %ebp\n subl $0x20,%esp\n"
"movl 0x34(%esp),%ebx\n xorl %edi,%edi\n movl %edi,0x14(%esp)\n"
// 004cfd0e..004cfd29: QueryInterface([EBX+0x9c], IID_IDMOWrapperFilter, &[ESP+0x14]); jl done
"movl 0x9c(%ebx),%eax\n movl (%eax),%ecx\n leal 0x14(%esp),%edx\n pushl %edx\n pushl $_replica_iid_wrapper\n pushl %eax\n movl (%ecx),%eax\n call *%eax\n"
"cmpl %edi,%eax\n jl 1f\n movl %edi,0x10(%esp)\n"
// 004cfd30..004cfd44: Init(view, CLSID speech, DMOCATEGORY_AUDIO_DECODER) via vtable+0x0c
"2: movl 0x14(%esp),%eax\n movl (%eax),%ecx\n movl 0xc(%ecx),%edx\n pushl $_replica_category_audio\n pushl $_replica_clsid_speech\n pushl %eax\n call *%edx\n"
// 004cfd46: the site, mov esi,eax / cmp esi,0x8007000e (the exact encoding the game uses)
".globl _replica_init_site\n_replica_init_site:\n .byte 0x8b,0xf0,0x81,0xfe,0x0e,0x00,0x07,0x80\n"
"jne 3f\n call _replica_out_of_memory\n"
"3: movl $1,%eax\n addl %eax,0x10(%esp)\n cmpl %edi,%esi\n jge 4f\n cmpl %eax,0x10(%esp)\n jbe 2b\n jmp 4f\n"
"1: movl %eax,%esi\n"
// 004cfd68..004cfd7c: Release the view when present
"4: movl 0x14(%esp),%eax\n cmpl %edi,%eax\n je 5f\n movl (%eax),%ecx\n movl 0x8(%ecx),%edx\n pushl %eax\n call *%edx\n movl %edi,0x14(%esp)\n"
"5: movl %esi,_replica_site_witness\n movl %edi,_replica_site_witness+4\n movl %ebx,_replica_site_witness+8\n movl %esp,_replica_site_witness+12\n"
"movl %esi,%eax\n addl $0x20,%esp\n popl %ebp\n popl %edi\n popl %esi\n popl %ebx\n ret\n");
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
enum Mode { MODE_GAME, MODE_NOPAUSE, MODE_EARLY_UPDATE, MODE_SINGLE, MODE_EXPLICIT, MODE_GAME_DS, MODE_GAME_DS_STEREO, MODE_GAME_DMO, MODE_GAME_DMO_FALLBACK, MODE_GAME_DMO_SKIP, MODE_GAME_DMO_HOOK };
static bool game_dmo(Mode m){return m==MODE_GAME_DMO||m==MODE_GAME_DMO_FALLBACK||m==MODE_GAME_DMO_SKIP||m==MODE_GAME_DMO_HOOK;}
static bool game_ds(Mode m){return m==MODE_GAME_DS||m==MODE_GAME_DS_STEREO||game_dmo(m);}
static const DWORD PRIMARY_RATE=44100; // 0x608afc as initialised by 004b8740 (004b7400 config override not modelled)
struct Stream {
    Trace& t;int index;IAMMultiMediaStream* multi{};IMediaStream* media{};IAudioMediaStream* audio{};
    IGraphBuilder* graph{};IBaseFilter* source{};IAudioData* data{};IAudioStreamSample* sample{};IDirectSoundBuffer* buffer{};
    IMediaPosition* position{};IMediaControl* control{};IBaseFilter* wrapper{};std::vector<BYTE> pcm;WAVEFORMATEX format{};
    double duration{};const char* fatal="none";HRESULT fatal_hr=S_OK;bool created{};HRESULT early_update_hr=E_PENDING;
    int state{1};bool pending{};Mode mode{MODE_GAME};
    Stream(Trace& x,int i,Mode m):t(x),index(i),mode(m){}
    bool need(const char* name,HRESULT hr) {
        if(FAILED(hr)){if(!std::strcmp(fatal,"none")){fatal=name;fatal_hr=hr;std::printf("REPLICA_FAILURE stream=%d name=%s hr=%08lx\n",index,name,static_cast<unsigned long>(hr));std::fflush(stdout);}return false;}
        return true;
    }
#define NEED(name,expr) do{if(!need(name,t.step(index,name,1,true,[&]{return (expr);})))return false;}while(0)
    bool create(const wchar_t* path,IDirectSound8* sound) {
        const bool made=build(path,sound);
        if(!made&&game_ds(mode))cleanup(); // 004d0160: MOV EAX,EBX; CALL 004d1d40 on the constructing thread, HRESULT discarded
        return made;
    }
    bool build(const wchar_t* path,IDirectSound8* sound) {
        NEED("activate_stream",CoCreateInstance(CLSID_AMMultiMediaStream,nullptr,CLSCTX_INPROC_SERVER,IID_IAMMultiMediaStream,reinterpret_cast<void**>(&multi)));
        NEED("initialize",multi->Initialize(STREAMTYPE_READ,AMMSF_NOGRAPHTHREAD,nullptr));
        HRESULT added=E_FAIL;
        for(int i=1;i<=2;++i){added=t.step(index,"add_audio",i,true,[&]{return multi->AddMediaStream(nullptr,&MSPID_PrimaryAudio,0,&media);});if(SUCCEEDED(added))break;}
        if(!need("add_audio",added))return false;
        NEED("audio_qi_pre",media->QueryInterface(IID_IAudioMediaStream,reinterpret_cast<void**>(&audio)));
        // 004cfba7: PCM / 16 bit / rate 0x608afc / nChannels 1 (004cf99f route) or 2 (004cf8bf route).
        const WORD channels=mode==MODE_GAME_DS_STEREO?2:1;
        WAVEFORMATEX wanted{WAVE_FORMAT_PCM,channels,PRIMARY_RATE,PRIMARY_RATE*channels*2u,static_cast<WORD>(channels*2),16,0};
        NEED("set_pcm",audio->SetFormat(&wanted));
        NEED("get_graph",multi->GetFilterGraph(&graph));
        if(game_dmo(mode)) {
            // 004cfcd0..004cfe18 (section 12): DMO wrapper created (CLSCTX 3) and Init'd with the WM Speech decoder
            // DMO (not registered in the bottle), two attempts, then AddFilter with a NULL name whatever Init returned.
            HRESULT made=t.twice(index,"dmo_wrapper_create",[&]{return CoCreateInstance(CLSID_DMOWrapperFilter_local,nullptr,CLSCTX_INPROC_SERVER|CLSCTX_INPROC_HANDLER,IID_IBaseFilter,reinterpret_cast<void**>(&wrapper));});
            if(SUCCEEDED(made)&&mode==MODE_GAME_DMO_HOOK) {
                // The game's Init loop in machine code with the production hook on its site: the
                // object is a replica of the media object with the wrapper in its +0x9c slot.
                std::vector<std::uint32_t> object(0x40,0);object[0x9c/4]=reinterpret_cast<std::uint32_t>(wrapper);
                const std::uint32_t esp_before=[]{std::uint32_t v;asm volatile("movl %%esp,%0":"=r"(v));return v;}();
                const HRESULT inited=t.step(index,"dmo_wrapper_init_hooked",1,true,[&]{return replica_media_init(object.data());});
                const std::uint32_t esp_after=[]{std::uint32_t v;asm volatile("movl %%esp,%0":"=r"(v));return v;}();
                x3m::voice_dmo_fallback::report();
                std::printf("REPLICA_SITE stream=%d hr=%08lx esi=%08lx edi=%08lx ebx_ok=%d esp_ok=%d object=%08lx wrapper=%08lx\n",index,static_cast<unsigned long>(inited),
                    static_cast<unsigned long>(replica_site_witness[0]),static_cast<unsigned long>(replica_site_witness[1]),
                    replica_site_witness[2]==reinterpret_cast<std::uint32_t>(object.data()),esp_before==esp_after,
                    reinterpret_cast<unsigned long>(object.data()),reinterpret_cast<unsigned long>(wrapper));
                std::fflush(stdout);
                if(wrapper)t.step(index,"dmo_wrapper_add",1,true,[&]{return graph->AddFilter(wrapper,nullptr);});
            } else if(SUCCEEDED(made)) {
                IDMOWrapperFilterLocal* init{};HRESULT inited=E_FAIL;
                if(SUCCEEDED(t.step(index,"dmo_wrapper_qi",1,true,[&]{return wrapper->QueryInterface(IID_IDMOWrapperFilter_local,reinterpret_cast<void**>(&init));}))) {
                    for(int i=1;i<=2;++i) {
                        inited=t.step(index,"dmo_wrapper_init",i,true,[&]{return init->Init(CLSID_CWMSPDecMediaObject_local,DMOCATEGORY_AUDIO_DECODER_local);});
                        // Hook action at the 004cfd46 return: on REGDB_E_CLASSNOTREG retry with the registered WMA decoder
                        // DMO; the game then sees the retry's HRESULT (Windows behaviour: one successful attempt).
                        if(inited==REGDB_E_CLASSNOTREG&&mode==MODE_GAME_DMO_FALLBACK)
                            inited=t.step(index,"dmo_wrapper_init_fallback",i,true,[&]{return init->Init(CLSID_CWMADecMediaObject_local,DMOCATEGORY_AUDIO_DECODER_local);});
                        if(SUCCEEDED(inited))break;
                    }
                    init->Release();
                }
                if(FAILED(inited)&&mode==MODE_GAME_DMO_SKIP)t.step(index,"dmo_wrapper_skip",1,true,[&]{wrapper->Release();wrapper=nullptr;return S_OK;});
                if(wrapper)t.step(index,"dmo_wrapper_add",1,true,[&]{return graph->AddFilter(wrapper,nullptr);});
            }
        }
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
        if(!game_ds(mode))t.step(index,"get_duration",1,true,[&]{return position->get_Duration(&duration);}); // not in 004cf460
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
        if(game_ds(mode)){cleanup_game();return;}
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
    // 004d1d40 destructor: Stop(+0x44), IMediaControl::Stop(+0x74), SetState(STOP), Release +0x70/+0x74, helper 004d1c20
    // (section 12: straight-line RemoveFilter(graph, slot); Release(slot) for +0xa4/+0xa0/+0x9c/+0xac/+0xa8, HRESULTs
    // discarded, no retry), Release graph +0x78 (Wine's filter_graph_Release loop runs inside it), then 004d1a40 repeats
    // Stop/Stop/SetState(STOP) on what is still held, frees the PCM buffer and releases +0x44/+0x6c/+0x58/+0x54; 004d1b70
    // releases +0x04. No sample cancel: the game relies on the STOP transitions alone.
    void cleanup_game() {
        for(int round=1;round<=2;++round) {
            if(buffer)t.step(index,"buffer_stop",round,true,[&]{return buffer->Stop();});
            if(control)t.step(index,"control_stop",round,true,[&]{return control->Stop();});
            if(multi)t.step(index,"stream_stop",round,true,[&]{return multi->SetState(STREAMSTATE_STOP);});
            if(round==1) {
                t.release(index,"release_position",position);t.release(index,"release_control",control);
                t.release(index,"release_audio",audio);t.release(index,"release_media",media);
                if(graph&&wrapper)t.step(index,"remove_dmo_wrapper",1,true,[&]{return graph->RemoveFilter(wrapper);});
                t.release(index,"release_dmo_wrapper",wrapper);
                if(graph&&source)t.step(index,"remove_source",1,true,[&]{return graph->RemoveFilter(source);});
                t.release(index,"release_source",source);
                t.release(index,"release_graph",graph);
            }
        }
        pcm.clear();pending=false;
        t.release(index,"release_buffer",buffer);t.release(index,"release_sample",sample);t.release(index,"release_data",data);
        t.release(index,"release_multimedia",multi);
    }
};
static LRESULT CALLBACK replica_wndproc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcA(h,m,w,l);}
static Mode parse_mode(const wchar_t* text,bool& ok) {
    ok=true;
    if(!wcscmp(text,L"game"))return MODE_GAME;
    if(!wcscmp(text,L"nopause"))return MODE_NOPAUSE;
    if(!wcscmp(text,L"early_update"))return MODE_EARLY_UPDATE;
    if(!wcscmp(text,L"single"))return MODE_SINGLE;
    if(!wcscmp(text,L"explicit"))return MODE_EXPLICIT;
    if(!wcscmp(text,L"game-ds"))return MODE_GAME_DS;
    if(!wcscmp(text,L"game-ds-stereo"))return MODE_GAME_DS_STEREO;
    if(!wcscmp(text,L"game-dmo"))return MODE_GAME_DMO;
    if(!wcscmp(text,L"game-dmo-fallback"))return MODE_GAME_DMO_FALLBACK;
    if(!wcscmp(text,L"game-dmo-skip"))return MODE_GAME_DMO_SKIP;
    if(!wcscmp(text,L"game-dmo-hook"))return MODE_GAME_DMO_HOOK;
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
    char mode_text[24];std::snprintf(mode_text,sizeof mode_text,"%ls",argv[1]);
    std::printf("REPLICA_HEADER schema=1 mode=%s streams=%d dwell_ms=%d watchdog_ms=%lu thread=%lu audible=0\n",mode_text,streams,dwell_ms,static_cast<unsigned long>(WATCHDOG_MS),static_cast<unsigned long>(owner_thread));
    Trace t;
    if(mode==MODE_GAME_DMO_HOOK) {
        // Production install path: the gate variable, the replica site, engine_patch claim, stub chained.
        SetEnvironmentVariableW(L"X3M_VOICE_DMO_FALLBACK",L"1");
        const auto site=reinterpret_cast<std::uintptr_t>(replica_init_site);
        const bool installed=x3m::voice_dmo_fallback::fixture_site(site)&&x3m::voice_dmo_fallback::initialize();
        unsigned char now[8]{};std::memcpy(now,replica_init_site,8);
        std::printf("REPLICA_HOOK installed=%d site=%08lx patched=%d stub=%08lx tail=%08lx\n",installed,static_cast<unsigned long>(site),now[0]==0xe9,
            reinterpret_cast<unsigned long>(x3m::voice_dmo_fallback::fixture_stub()),reinterpret_cast<unsigned long>(x3m::voice_dmo_fallback::fixture_tail()));
        if(installed) {
            // The emitted bytes, for objdump: stub up to and including its jmp [next] slot, then the tail block.
            const auto* stub=static_cast<const unsigned char*>(x3m::voice_dmo_fallback::fixture_stub());
            const auto* tail=static_cast<const unsigned char*>(x3m::voice_dmo_fallback::fixture_tail());
            std::printf("REPLICA_STUB_BYTES kind=stub base=%08lx hex=",reinterpret_cast<unsigned long>(stub));for(unsigned i=0;i<144;++i)std::printf("%02x",stub[i]);std::putchar('\n');
            std::printf("REPLICA_STUB_BYTES kind=tail base=%08lx hex=",reinterpret_cast<unsigned long>(tail));for(unsigned i=0;i<32;++i)std::printf("%02x",tail[i]);std::putchar('\n');
        }
        std::fflush(stdout);
        if(!installed){std::printf("REPLICA_ABORT stage=hook_install\n");return 4;}
    }
    HRESULT init=t.step(0,"co_initialize",1,true,[]{return CoInitialize(nullptr);});
    if(FAILED(init)){std::printf("REPLICA_ABORT stage=co_initialize hr=%08lx\n",static_cast<unsigned long>(init));return 0;}
    HWND window{};
    if(game_ds(mode)) {
        // 004dadd1/004dae0d: RegisterClassA with a real WndProc, CreateWindowExA(0, cls, cls, WS_VISIBLE|0xca0000, 0, 0, 640, 480, ...).
        WNDCLASSA wc{};wc.lpfnWndProc=replica_wndproc;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="X3VoiceStartupReplicaGame";wc.hCursor=LoadCursorA(nullptr,MAKEINTRESOURCEA(32512));RegisterClassA(&wc);
        window=CreateWindowExA(0,wc.lpszClassName,wc.lpszClassName,0x10000000|0xca0000,0,0,640,480,nullptr,nullptr,wc.hInstance,nullptr);
    } else {
        WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"X3VoiceStartupReplica";RegisterClassW(&wc);
        window=CreateWindowW(wc.lpszClassName,L"X3 startup replica",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
    }
    IDirectSound8* sound{};IDirectSoundBuffer* primary{};IDirectSound3DListener* listener{};LONG primary_volume=0;bool volume_read=false;
    HRESULT ds=t.step(0,"directsound_create",1,true,[&]{return DirectSoundCreate8(&DSDEVID_DefaultPlayback,&sound,nullptr);});
    if(sound&&game_ds(mode)){DSCAPS caps{};caps.dwSize=sizeof(caps);static_assert(sizeof(DSCAPS)==0x60,"004de180 DSCAPS size");t.step(0,"directsound_caps",1,true,[&]{return sound->GetCaps(&caps);});}
    HRESULT coop=E_POINTER;if(sound&&window)coop=t.step(0,"directsound_cooperative",1,true,[&]{return sound->SetCooperativeLevel(window,DSSCL_PRIORITY);});
    if(FAILED(coop)&&sound){sound->Release();sound=nullptr;}
    std::printf("REPLICA_STARTUP ds_hr=%08lx coop_hr=%08lx ds_present=%d window=%d\n",static_cast<unsigned long>(ds),static_cast<unsigned long>(coop),sound!=nullptr,window!=nullptr);
    if(sound&&game_ds(mode)) {
        // 004de201..004de390: primary buffer with the 0xd1 -> 0x11 -> 0x1 fallback, 3D listener QI (tolerated), Play(LOOPING),
        // then SetFormat(0x608af8), GetVolume and the listener setup with CommitDeferredSettings.
        static const DWORD chain[3]={DSBCAPS_PRIMARYBUFFER|DSBCAPS_CTRL3D|DSBCAPS_CTRLPAN|DSBCAPS_CTRLVOLUME,DSBCAPS_PRIMARYBUFFER|DSBCAPS_CTRL3D,DSBCAPS_PRIMARYBUFFER};
        static_assert(DSBCAPS_PRIMARYBUFFER==0x1&&DSBCAPS_CTRL3D==0x10&&DSBCAPS_CTRLPAN==0x40&&DSBCAPS_CTRLVOLUME==0x80,"SDK DirectSound flags required");
        DWORD flags=0;HRESULT create_hr=E_FAIL,play_hr=E_FAIL,format_hr=E_FAIL,commit_hr=E_FAIL;
        for(int i=0;i<3&&!primary;++i){flags=chain[i];create_hr=t.step(0,"primary_create",i+1,true,[&]{DSBUFFERDESC d{};d.dwSize=sizeof(d);static_assert(sizeof(DSBUFFERDESC)==0x24,"004de201 DSBUFFERDESC size");d.dwFlags=flags;return sound->CreateSoundBuffer(&d,&primary,nullptr);});}
        if(primary) {
            if(FAILED(t.step(0,"listener_qi",1,true,[&]{return primary->QueryInterface(IID_IDirectSound3DListener,reinterpret_cast<void**>(&listener));})))listener=nullptr;
            play_hr=t.step(0,"primary_play",1,true,[&]{return primary->Play(0,0,DSBPLAY_LOOPING);});
            if(play_hr==S_OK) {
                WAVEFORMATEX wfx{WAVE_FORMAT_PCM,2,PRIMARY_RATE,PRIMARY_RATE*4,4,16,0}; // 0x608af8 from 004b8740
                format_hr=t.step(0,"primary_set_format",1,true,[&]{return primary->SetFormat(&wfx);});
                volume_read=SUCCEEDED(t.step(0,"primary_get_volume",1,true,[&]{return primary->GetVolume(&primary_volume);}));
                if(listener) {
                    t.step(0,"listener_doppler",1,true,[&]{return listener->SetDopplerFactor(1.0f,DS3D_IMMEDIATE);});
                    t.step(0,"listener_distance",1,true,[&]{return listener->SetDistanceFactor(0.001f,DS3D_IMMEDIATE);}); // _0x565624
                    t.step(0,"listener_rolloff",1,true,[&]{return listener->SetRolloffFactor(1.5f,DS3D_IMMEDIATE);}); // _0x565620
                    t.step(0,"listener_position",1,true,[&]{return listener->SetPosition(0.f,0.f,0.f,DS3D_IMMEDIATE);});
                    commit_hr=t.step(0,"listener_commit",1,true,[&]{return listener->CommitDeferredSettings();});
                }
            }
        }
        std::printf("REPLICA_PRIMARY flags=%lx create_hr=%08lx play_hr=%08lx format_hr=%08lx volume=%ld listener=%d commit_hr=%08lx\n",static_cast<unsigned long>(flags),static_cast<unsigned long>(create_hr),static_cast<unsigned long>(play_hr),static_cast<unsigned long>(format_hr),static_cast<long>(primary_volume),listener!=nullptr,static_cast<unsigned long>(commit_hr));
        std::fflush(stdout);
    }
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
        Stream* s=new Stream(t,index,mode);all.push_back(s);
        t.step(index,"pump",1,true,[&]{pump();return S_OK;});
        const bool made=s->create(argv[2+index],sound);
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
    // 004de3b0: restore the primary volume, drop the listener and primary buffer, then the device.
    if(primary&&volume_read)t.step(0,"primary_set_volume",1,true,[&]{return primary->SetVolume(primary_volume);});
    t.release(0,"release_listener",listener);t.release(0,"release_primary",primary);
    t.release(0,"release_directsound",sound);if(window)DestroyWindow(window);
    t.step(0,"co_uninitialize",1,true,[]{CoUninitialize();return S_OK;});
    SetEvent(watchdog_stop);WaitForSingleObject(watcher,1000);CloseHandle(watcher);CloseHandle(watchdog_stop);
    std::printf("REPLICA_COMPLETE streams=%d audible=0\n",streams);
    return 0;
}
