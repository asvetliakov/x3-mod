// Follow-up only: explicit ASF source/decoder, eight fresh graphs in one STA.
// Reuse the frozen, reviewed R1 timing, manual-sink guard, cancellation, sample
// verification and teardown. The renamed R1 entry point is never invoked.
#define wmain voice_r1_unused_entry
#include "voice_stream_probe.cpp"
#undef wmain

struct PinNames {
    const char* role;const char* enumerate;const char* next;const char* direction;
    const char* types;const char* type_next;const char* selection;
};
static constexpr PinNames source_names{"source","source_enum_pins","source_pin_next","source_direction","source_enum_types","source_type_next","source_audio_selection"};
static constexpr PinNames sink_names{"sink","sink_enum_pins","sink_pin_next","sink_direction","sink_enum_types","sink_type_next","sink_audio_selection"};
static constexpr PinNames input_names{"decoder_input","decoder_in_enum_pins","decoder_in_pin_next","decoder_in_direction","decoder_in_enum_types","decoder_in_type_next","decoder_input_selection"};
static constexpr PinNames output_names{"decoder_output","decoder_out_enum_pins","decoder_out_pin_next","decoder_out_direction","decoder_out_enum_types","decoder_out_type_next","decoder_output_selection"};
static void free_type(AM_MEDIA_TYPE*& mt) {
    if(!mt)return;
    CoTaskMemFree(mt->pbFormat);if(mt->pUnk)mt->pUnk->Release();CoTaskMemFree(mt);mt=nullptr;
}
static bool audio_pin(Trace& t,IBaseFilter* filter,const PinNames& names,PIN_DIRECTION wanted,IPin*& chosen,AM_MEDIA_TYPE*& offered) {
    IEnumPins* pins{};unsigned candidates=0;HRESULT failure=S_OK;
    HRESULT hr=t.call(names.enumerate,1,true,[&]{return filter->EnumPins(&pins);});
    if(!t.need(names.enumerate,hr))return false;
    for(unsigned pi=0;;++pi) {
        IPin* pin{};hr=t.call(names.next,pi+1,true,[&]{return pins->Next(1,&pin,nullptr);});
        if(hr==S_FALSE)break;
        if(hr!=S_OK||pi==16) {if(pin)pin->Release();failure=FAILED(hr)?hr:E_UNEXPECTED;t.need(names.next,failure);break;}
        PIN_DIRECTION direction{};
        hr=t.call(names.direction,pi+1,true,[&]{return pin->QueryDirection(&direction);});
        if(FAILED(hr)) {pin->Release();failure=hr;t.need(names.direction,hr);break;}
        bool audio=false;AM_MEDIA_TYPE* first{};
        if(direction==wanted) {
            IEnumMediaTypes* types{};
            hr=t.call(names.types,pi+1,true,[&]{return pin->EnumMediaTypes(&types);});
            if(SUCCEEDED(hr)) {
                for(unsigned ti=0;;++ti) {
                    AM_MEDIA_TYPE* mt{};
                    hr=t.call(names.type_next,ti+1,true,[&]{return types->Next(1,&mt,nullptr);});
                    if(hr==S_FALSE)break;
                    if(hr!=S_OK||ti==16||!mt) {free_type(mt);failure=FAILED(hr)?hr:E_UNEXPECTED;t.need(names.type_next,failure);break;}
                    char major[40],sub[40],format[40];guid_text(mt->majortype,major);guid_text(mt->subtype,sub);guid_text(mt->formattype,format);
                    WAVEFORMATEX wave{};
                    if(mt->formattype==FORMAT_WaveFormatEx&&mt->pbFormat&&mt->cbFormat>=16)
                        std::memcpy(&wave,mt->pbFormat,std::min<std::size_t>(sizeof(wave),mt->cbFormat));
                    t.prefix("ASF_TYPE");std::printf(" role=%s pin=%u type=%u major=%s subtype=%s format=%s format_bytes=%lu tag=%u channels=%u rate=%lu bits=%u align=%u avg=%lu extra=%u\n",names.role,pi,ti,major,sub,format,static_cast<unsigned long>(mt->cbFormat),wave.wFormatTag,wave.nChannels,static_cast<unsigned long>(wave.nSamplesPerSec),wave.wBitsPerSample,wave.nBlockAlign,static_cast<unsigned long>(wave.nAvgBytesPerSec),wave.cbSize);
                    if(mt->majortype==MEDIATYPE_Audio) {audio=true;if(!first){first=mt;mt=nullptr;}}
                    free_type(mt);
                }
                types->Release();
            } else {failure=hr;t.need(names.types,hr);}
        }
        t.prefix("ASF_PIN");std::printf(" role=%s pin=%u direction=%d audio=%d\n",names.role,pi,int(direction),audio);
        if(audio) {
            ++candidates;
            if(!chosen) {chosen=pin;pin=nullptr;offered=first;first=nullptr;}
        }
        free_type(first);if(pin)pin->Release();
        if(FAILED(failure))break;
    }
    pins->Release();
    t.prefix("ASF_SELECTION");std::printf(" role=%s candidates=%u\n",names.role,candidates);
    if(FAILED(failure)||candidates!=1) {
        if(chosen){chosen->Release();chosen=nullptr;}free_type(offered);
        return t.need(names.selection,FAILED(failure)?failure:E_UNEXPECTED);
    }
    return true;
}
// Record the exact selected endpoint, including a failed negotiated-type query.
// This complements the optional graph snapshot with role-bound connection proof.
static bool negotiated(Trace& t,IPin* pin,const char* role,const char* name) {
    AM_MEDIA_TYPE mt{};
    HRESULT hr=t.call(name,1,true,[&]{return pin->ConnectionMediaType(&mt);});
    if(SUCCEEDED(hr)) {
        char major[40],sub[40],format[40];guid_text(mt.majortype,major);guid_text(mt.subtype,sub);guid_text(mt.formattype,format);
        WAVEFORMATEX wave{};
        if(mt.formattype==FORMAT_WaveFormatEx&&mt.pbFormat&&mt.cbFormat>=16)
            std::memcpy(&wave,mt.pbFormat,std::min<std::size_t>(sizeof(wave),mt.cbFormat));
        t.prefix("ASF_NEGOTIATED");std::printf(" role=%s major=%s subtype=%s format=%s format_bytes=%lu tag=%u channels=%u rate=%lu bits=%u align=%u avg=%lu extra=%u\n",role,major,sub,format,static_cast<unsigned long>(mt.cbFormat),wave.wFormatTag,wave.nChannels,static_cast<unsigned long>(wave.nSamplesPerSec),wave.wBitsPerSample,wave.nBlockAlign,static_cast<unsigned long>(wave.nAvgBytesPerSec),wave.cbSize);
        if(mt.majortype!=MEDIATYPE_Audio)hr=VFW_E_INVALIDMEDIATYPE;
    }
    CoTaskMemFree(mt.pbFormat);if(mt.pUnk)mt.pUnk->Release();
    return t.need(name,hr);
}
struct State {
    bool decoder_attempted{};bool decoder_available{};HRESULT decoder_hr{S_OK};
    bool source_available{};bool loaded{};bool pins_selected{};bool connected{};
};
static bool explicit_create(Graph& g,const wchar_t* path,IDirectSound8* sound,int mode,State& state) {
    Trace& t=g.t;t.phase="create";
    IFileSourceFilter* file{};IMediaStreamFilter* sink{};IDMOWrapperFilter* dmo{};
    IPin* out{};IPin* in{};IPin* decoder_in{};IPin* decoder_out{};
    AM_MEDIA_TYPE* source_type{};AM_MEDIA_TYPE* sink_type{};AM_MEDIA_TYPE* decoder_in_type{};AM_MEDIA_TYPE* decoder_out_type{};
    // Capability control executes independently, even if source activation
    // later fails. It is not yet a required edge and does not replace its error.
    if(mode==1) {
        state.decoder_attempted=true;
        HRESULT hr=t.call("wma_wrapper_activate",1,false,[&]{return CoCreateInstance(CLSID_DMOWrapperFilter,nullptr,CLSCTX_INPROC_SERVER,IID_IBaseFilter,reinterpret_cast<void**>(&g.wrapper));});
        if(SUCCEEDED(hr))hr=t.call("wma_wrapper_qi",1,false,[&]{return g.wrapper->QueryInterface(IID_IDMOWrapperFilter,reinterpret_cast<void**>(&dmo));});
        if(SUCCEEDED(hr))hr=t.call("wma_decoder_init",1,false,[&]{return dmo->Init(CLSID_CWMADecMediaObject,DMOCATEGORY_AUDIO_DECODER);});
        state.decoder_hr=hr;state.decoder_available=SUCCEEDED(hr);t.release("release_asf_dmo_interface",dmo);
        t.prefix("ASF_DECODER");std::printf(" available=%d hr=%08lx\n",state.decoder_available,static_cast<unsigned long>(hr));
    }
    auto work=[&]()->bool {
#define NEED(name,expr) do {if(!t.need(name,t.call(name,1,true,[&]{return (expr);})))return false;}while(0)
        NEED("activate_stream",CoCreateInstance(CLSID_AMMultiMediaStream,nullptr,CLSCTX_INPROC_SERVER,IID_IAMMultiMediaStream,reinterpret_cast<void**>(&g.multi)));
        NEED("initialize",g.multi->Initialize(STREAMTYPE_READ,AMMSF_NOGRAPHTHREAD,nullptr));
        NEED("add_audio",g.multi->AddMediaStream(nullptr,&MSPID_PrimaryAudio,0,&g.media));
        NEED("audio_qi_pre",g.media->QueryInterface(IID_IAudioMediaStream,reinterpret_cast<void**>(&g.audio)));
        WAVEFORMATEX wanted{WAVE_FORMAT_PCM,1,44100,88200,2,16,0};
        NEED("set_pcm",g.audio->SetFormat(&wanted));NEED("get_graph",g.multi->GetFilterGraph(&g.graph));
        NEED("asf_activate",CoCreateInstance(CLSID_WMAsfReader,nullptr,CLSCTX_INPROC_SERVER,IID_IBaseFilter,reinterpret_cast<void**>(&g.source)));
        state.source_available=true;
        NEED("asf_add",g.graph->AddFilter(g.source,L"Explicit ASF Source"));
        NEED("asf_file_qi",g.source->QueryInterface(IID_IFileSourceFilter,reinterpret_cast<void**>(&file)));
        // IFileSourceFilter::Load is single-initialization. Never retry on this
        // source object; every matrix repeat creates a completely fresh graph.
        NEED("asf_load",file->Load(path,nullptr));state.loaded=true;
        NEED("get_manual_filter",g.multi->GetFilter(&sink));
        if(!audio_pin(t,g.source,source_names,PINDIR_OUTPUT,out,source_type))return false;
        if(!audio_pin(t,sink,sink_names,PINDIR_INPUT,in,sink_type))return false;
        state.pins_selected=true;
        t.call("sink_query_source_type",1,false,[&]{return in->QueryAccept(source_type);});
        if(mode==0) {
            NEED("connect_auto_to_manual",g.graph->Connect(out,in));
            if(!negotiated(t,out,"source","source_connected_type")||!negotiated(t,in,"sink","sink_connected_type"))return false;
        } else {
            if(!state.decoder_available)return t.need("explicit_decoder_unavailable",state.decoder_hr);
            NEED("wma_add",g.graph->AddFilter(g.wrapper,L"Explicit WMA Decoder"));
            if(!audio_pin(t,g.wrapper,input_names,PINDIR_INPUT,decoder_in,decoder_in_type))return false;
            t.call("decoder_query_source_type",1,false,[&]{return decoder_in->QueryAccept(source_type);});
            NEED("connect_source_decoder",g.graph->ConnectDirect(out,decoder_in,nullptr));
            if(!negotiated(t,out,"source","source_connected_type")||!negotiated(t,decoder_in,"decoder_input","decoder_input_connected_type"))return false;
            // Output type availability can depend on the connected input type.
            if(!audio_pin(t,g.wrapper,output_names,PINDIR_OUTPUT,decoder_out,decoder_out_type))return false;
            t.call("sink_query_decoder_type",1,false,[&]{return in->QueryAccept(decoder_out_type);});
            NEED("connect_decoder_manual",g.graph->ConnectDirect(decoder_out,in,nullptr));
            if(!negotiated(t,decoder_out,"decoder_output","decoder_output_connected_type")||!negotiated(t,in,"sink","sink_connected_type"))return false;
        }
        state.connected=true;
        // Preserve the original constructor's post-open sample/buffer contract.
        t.release("release_audio_pre",g.audio);t.release("release_media_pre",g.media);
        if(!sound)return t.need("native_directsound_missing",E_POINTER);
        NEED("get_audio",g.multi->GetMediaStream(MSPID_PrimaryAudio,&g.media));
        NEED("audio_qi_post",g.media->QueryInterface(IID_IAudioMediaStream,reinterpret_cast<void**>(&g.audio)));
        NEED("get_format",g.audio->GetFormat(&g.format));
        const auto& f=g.format;
        t.prefix("VOICE_FORMAT");std::printf(" tag=%u rate=%lu channels=%u bits=%u align=%u avg=%lu extra=%u\n",f.wFormatTag,static_cast<unsigned long>(f.nSamplesPerSec),f.nChannels,f.wBitsPerSample,f.nBlockAlign,static_cast<unsigned long>(f.nAvgBytesPerSec),f.cbSize);
        if(f.wFormatTag!=WAVE_FORMAT_PCM||f.wBitsPerSample!=16||!f.nChannels||f.nBlockAlign!=2*f.nChannels||!f.nSamplesPerSec||!f.nAvgBytesPerSec||f.nAvgBytesPerSec>1000000||std::uint64_t(f.nSamplesPerSec)*f.nBlockAlign!=f.nAvgBytesPerSec)return t.need("pcm_domain",E_INVALIDARG);
        g.pcm.resize(f.nAvgBytesPerSec*2);
        NEED("activate_audio_data",CoCreateInstance(CLSID_AMAudioData,nullptr,CLSCTX_INPROC_SERVER,IID_IAudioData,reinterpret_cast<void**>(&g.data)));
        NEED("set_buffer_native",g.data->SetBuffer(static_cast<DWORD>(g.pcm.size()),g.pcm.data(),0));
        NEED("data_format",g.data->SetFormat(&f));NEED("create_sample",g.audio->CreateSample(g.data,0,&g.sample));
        DSBUFFERDESC desc{};desc.dwSize=sizeof(desc);desc.dwFlags=0x10088;desc.dwBufferBytes=f.nAvgBytesPerSec*5;desc.lpwfxFormat=&g.format;
        NEED("create_dsound_buffer",sound->CreateSoundBuffer(&desc,&g.buffer,nullptr));
        NEED("position_qi",g.graph->QueryInterface(IID_IMediaPosition,reinterpret_cast<void**>(&g.position)));
        NEED("control_qi",g.graph->QueryInterface(IID_IMediaControl,reinterpret_cast<void**>(&g.control)));
        t.call("get_duration",1,false,[&]{return g.position->get_Duration(&g.duration);});
        NEED("probe_manual_sink_guard",g.manual_sink_only());
        NEED("stream_run",g.multi->SetState(STREAMSTATE_RUN));NEED("control_pause",g.control->Pause());
        g.constructed=true;return true;
#undef NEED
    };
    bool result=work();
    t.release("release_explicit_file",file);t.release("release_explicit_manual_filter",sink);
    t.release("release_source_pin",out);t.release("release_sink_pin",in);
    t.release("release_decoder_input_pin",decoder_in);t.release("release_decoder_output_pin",decoder_out);
    free_type(source_type);free_type(sink_type);free_type(decoder_in_type);free_type(decoder_out_type);
    return result;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    setvbuf(stdout,nullptr,_IONBF,0);owner_thread=GetCurrentThreadId();QueryPerformanceFrequency(&frequency);
    watchdog_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);HANDLE watcher=CreateThread(nullptr,0,watchdog,nullptr,0,nullptr);
    if(!watchdog_stop||!watcher)return 3;
    std::printf("ASF_HEADER schema=1 sources=2 modes=2 repeats=2 cases=8 thread=%lu audible=0\n",static_cast<unsigned long>(owner_thread));
    Trace t;HRESULT init=t.call("co_initialize",1,true,[]{return CoInitialize(nullptr);});
    if(FAILED(init)) {std::printf("ASF_ABORT stage=co_initialize hr=%08lx\n",static_cast<unsigned long>(init));SetEvent(watchdog_stop);WaitForSingleObject(watcher,1000);CloseHandle(watcher);CloseHandle(watchdog_stop);return 0;}
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"X3ExplicitAsfProbe";RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"X3 silent ASF probe",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
    IDirectSound8* sound{};HRESULT ds=t.call("directsound_create",1,false,[&]{return DirectSoundCreate8(&DSDEVID_DefaultPlayback,&sound,nullptr);});
    HRESULT coop=E_POINTER;if(sound&&window)coop=t.call("directsound_cooperative",1,false,[&]{return sound->SetCooperativeLevel(window,DSSCL_PRIORITY);});
    std::printf("VOICE_STARTUP ds_hr=%08lx coop_hr=%08lx ds_present=%d window=%d primary_play=0\n",static_cast<unsigned long>(ds),static_cast<unsigned long>(coop),sound!=nullptr,window!=nullptr);
    for(int source=0;source<2;++source)for(int mode=0;mode<2;++mode)for(int repeat=0;repeat<2;++repeat) {
        pump();t.source=source?244:144;t.route=mode;t.wrapper=mode;t.repeat=repeat;t.fatal="none";t.fatal_hr=S_OK;
        Graph g(t);State state;bool made=explicit_create(g,argv[1+source],sound,mode,state);topology(t,g.graph);
        bool decoded=made&&g.decode();g.cleanup();
        t.prefix("ASF_CASE");std::printf(" source_available=%d loaded=%d pins_selected=%d decoder_attempted=%d decoder_available=%d decoder_hr=%08lx connected=%d constructed=%d decoded=%d fatal=%s hr=%08lx cleanup=1\n",state.source_available,state.loaded,state.pins_selected,state.decoder_attempted,state.decoder_available,static_cast<unsigned long>(state.decoder_hr),state.connected,made,decoded,t.fatal,static_cast<unsigned long>(t.fatal_hr));
    }
    t.source=0;t.route=0;t.wrapper=0;t.repeat=-1;t.phase="shutdown";t.release("release_directsound",sound);if(window)DestroyWindow(window);
    t.call("co_uninitialize",1,false,[]{CoUninitialize();return S_OK;});
    SetEvent(watchdog_stop);WaitForSingleObject(watcher,1000);CloseHandle(watcher);CloseHandle(watchdog_stop);
    std::printf("ASF_COMPLETE cases=8 processes=1 audible=0 owner_thread=1\n");return 0;
}
