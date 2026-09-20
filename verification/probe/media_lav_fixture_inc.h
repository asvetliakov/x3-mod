// Fixture-only public DirectShow/provider boundary. No registry or pin hooks.
// Build with --lav-provider-record to use the matched, locally retained headers.
#ifdef X3_FIXTURE_LAV
#include <initguid.h>
#include <LAVVideoSettings.h>
#include <LAVSplitterSettings.h>
#endif

struct LavTrial {
    HANDLE context=INVALID_HANDLE_VALUE;ULONG_PTR cookie=0;
    IMediaStreamFilter* sink{};IFileSourceFilter* file{};IMediaSeeking* seeking{};IMediaEvent* events{};
    bool transport_allocator=false;
    IMemInputPin* terminal_input{};IMemAllocator* terminal_allocator{};
    IPin *source_out{},*decoder_in{},*decoder_out{},*sink_in{};
#ifdef X3_FIXTURE_LAV
    ILAVFSettings* splitter_settings{};ILAVVideoSettings* video_settings{};
#endif
    static void free_type(AM_MEDIA_TYPE& t){if(t.pbFormat)CoTaskMemFree(t.pbFormat);if(t.pUnk)t.pUnk->Release();t={};}
    static bool unique_pin(IBaseFilter* filter,PIN_DIRECTION direction,bool video,IPin** result){
        IEnumPins* pins{};HRESULT hr=call("lav_enum_pins","public_EnumPins",[&]{return filter->EnumPins(&pins);});
        if(FAILED(hr))return false;
        unsigned matches=0;bool complete=false;
        for(unsigned i=0;i<32;++i){
            IPin* pin{};ULONG fetched=0;hr=call("lav_pin_next","one",[&]{return pins->Next(1,&pin,&fetched);});
            if(hr==S_FALSE){complete=true;break;}if(hr!=S_OK)break;
            PIN_DIRECTION actual{};hr=call("lav_pin_direction","out",[&]{return pin->QueryDirection(&actual);});
            bool compatible=SUCCEEDED(hr)&&actual==direction;
            if(compatible&&video){
                compatible=false;IEnumMediaTypes* types{};
                hr=call("lav_enum_types","public_EnumMediaTypes",[&]{return pin->EnumMediaTypes(&types);});
                if(SUCCEEDED(hr))for(unsigned j=0;j<128;++j){
                    AM_MEDIA_TYPE* t{};ULONG got=0;hr=call("lav_type_next","one",[&]{return types->Next(1,&t,&got);});
                    if(hr!=S_OK)break;
                    std::printf("MP_LAV_TYPE major=%s subtype=%s format=%s bytes=%lu\n",guid(t->majortype).c_str(),guid(t->subtype).c_str(),guid(t->formattype).c_str(),t->cbFormat);
                    compatible|=t->majortype==MEDIATYPE_Video;free_type(*t);CoTaskMemFree(t);
                }
                release("lav_release_types",types);
            }
            if(compatible){++matches;if(matches==1){*result=pin;pin=nullptr;}}
            release("lav_release_pin_candidate",pin);
        }
        release("lav_release_pin_enum",pins);
        return need("lav_unique_pin",complete&&matches==1?S_OK:E_UNEXPECTED);
    }
    bool observe_context(const std::wstring& manifest){
        // Supported application APIs observe the thread's active manifest.
        // They do not expose which COM redirection entry was used.
        std::vector<unsigned char> buffer(256*1024);HANDLE current=nullptr;SIZE_T written=0;
        NEED("lav_get_current_context","owner_STA_GetCurrentActCtx",win(GetCurrentActCtx(&current)));
        if(!current)return need("lav_active_context_present",E_UNEXPECTED);
        HRESULT hr=call("lav_query_context","QueryActCtxW_DetailedInformation",[&]{return win(QueryActCtxW(0,current,nullptr,ActivationContextDetailedInformation,buffer.data(),buffer.size(),&written));});
        std::wstring root;
        if(SUCCEEDED(hr)&&written>=sizeof(ACTIVATION_CONTEXT_DETAILED_INFORMATION)){
            const auto* info=reinterpret_cast<const ACTIVATION_CONTEXT_DETAILED_INFORMATION*>(buffer.data());
            uintptr_t address=reinterpret_cast<uintptr_t>(info->lpRootManifestPath),begin=reinterpret_cast<uintptr_t>(buffer.data()),end=begin+buffer.size();
            if(address>=begin&&address<=end-sizeof(wchar_t)&&address%alignof(wchar_t)==0){
                size_t capacity=(end-address)/sizeof(wchar_t),length=0;
                while(length<capacity&&info->lpRootManifestPath[length])++length;
                if(length<capacity)root.assign(info->lpRootManifestPath,length);
            }
        }
        bool matches=!root.empty()&&_wcsicmp(root.c_str(),manifest.c_str())==0;
        if(SUCCEEDED(hr))std::printf("MP_LAV_CONTEXT root_manifest=%s expected=%s manifest_matches=%d com_redirection_observed=0\n",encoded(root.c_str()).c_str(),encoded(manifest.c_str()).c_str(),matches);
        call("lav_release_current_context","GetCurrentActCtx_reference",[&]{ReleaseActCtx(current);return S_OK;});
        return need("lav_query_context",hr)&&need("lav_context_manifest_match",matches?S_OK:E_UNEXPECTED);
    }
    bool observe_class(IBaseFilter* filter,REFGUID expected,const char* role){
        CLSID actual{};
        NEED("lav_created_class","created_filter_GetClassID",filter->GetClassID(&actual));
        bool matches=actual==expected;
        std::printf("MP_LAV_CLASS role=%s clsid=%s expected=%s matches=%d\n",role,guid(actual).c_str(),guid(expected).c_str(),matches);
        return need("lav_created_class_match",matches?S_OK:E_UNEXPECTED);
    }
    bool module(const wchar_t* name,const std::wstring& directory,bool assembly=false){
        wchar_t path[32768]{};HMODULE m=GetModuleHandleW(name);
        NEED("lav_module_path","loaded_module",win(m&&GetModuleFileNameW(m,path,32768)>0));
        std::wstring expected=directory+L"\\"+name;
        bool exact=_wcsicmp(expected.c_str(),path)==0;
        std::printf("%s name=%s path=%s expected=%s exact=%d\n",assembly?"MP_LAV_ASSEMBLY_MODULE":"MP_LAV_MODULE",utf8(name).c_str(),encoded(path).c_str(),encoded(expected.c_str()).c_str(),exact);
        return need("lav_local_module",exact?S_OK:E_UNEXPECTED);
    }
    bool observe_assembly(const std::wstring& manifest){
        std::wstring dir=manifest.substr(0,manifest.find_last_of(L"\\/"));
        for(const wchar_t* name:{L"LAVSplitter.ax",L"LAVVideo.ax",L"avcodec-lav-62.dll",L"avformat-lav-62.dll",L"avutil-lav-60.dll",L"avfilter-lav-11.dll",L"swresample-lav-6.dll",L"swscale-lav-9.dll",L"libbluray.dll"})
            if(!module(name,dir,true))return false;
        return true;
    }
    bool construct(const std::wstring& manifest,const std::wstring& media,IAMMultiMediaStream* multi,IGraphBuilder* graph,IBaseFilter** source,IBaseFilter** decoder,bool transport,bool derived){
#ifndef X3_FIXTURE_LAV
        (void)manifest;(void)media;(void)multi;(void)graph;(void)source;(void)decoder;(void)transport;(void)derived;
        return need("lav_public_headers_not_built",E_NOTIMPL);
#else
        ACTCTXW act{};act.cbSize=sizeof act;act.lpSource=manifest.c_str();
        NEED("lav_create_context","private_manifest",win((context=CreateActCtxW(&act))!=INVALID_HANDLE_VALUE));
        NEED("lav_activate_context","owner_STA",win(ActivateActCtx(context,&cookie)));
        GUID source_cls{},video_cls{};
        CLSIDFromString(L"{B98D13E7-55DB-4385-A33D-09FD1BA26338}",&source_cls);
        CLSIDFromString(L"{EE30215D-164F-4A92-A4EB-9D4C13390F9F}",&video_cls);
        if(!observe_context(manifest))return false;
        NEED("lav_create_source","CoCreateInstance_CLSCTX_INPROC_SERVER",CoCreateInstance(source_cls,nullptr,CLSCTX_INPROC_SERVER,IID_IBaseFilter,reinterpret_cast<void**>(source)));
        NEED("lav_create_decoder","CoCreateInstance_CLSCTX_INPROC_SERVER",CoCreateInstance(video_cls,nullptr,CLSCTX_INPROC_SERVER,IID_IBaseFilter,reinterpret_cast<void**>(decoder)));
        if(!observe_class(*source,source_cls,"source")||!observe_class(*decoder,video_cls,"decoder"))return false;
        std::wstring dir=manifest.substr(0,manifest.find_last_of(L"\\/"));
        if(!module(L"LAVSplitter.ax",dir)||!module(L"LAVVideo.ax",dir))return false;
        NEED("lav_qi_source_settings","ILAVFSettings_0_81",(*source)->QueryInterface(IID_ILAVFSettings,reinterpret_cast<void**>(&splitter_settings)));
        NEED("lav_source_runtime","TRUE_before_connect",splitter_settings->SetRuntimeConfig(TRUE));
        NEED("lav_qi_video_settings","ILAVVideoSettings_0_81",(*decoder)->QueryInterface(IID_ILAVVideoSettings,reinterpret_cast<void**>(&video_settings)));
        NEED("lav_video_runtime","TRUE_before_connect",video_settings->SetRuntimeConfig(TRUE));
        NEED("lav_software","HWAccel_None",video_settings->SetHWAccel(HWAccel_None));
        NEED("lav_threads","1",video_settings->SetNumThreads(1));
        if(transport){
            // Public runtime-only setting: separate processes otherwise seed
            // random RGB dithering from time(), invalidating a byte-exact oracle.
            HRESULT set=call("lav_dither_set","LAVDither_Ordered_runtime_before_connect",[&]{return video_settings->SetDitherMode(LAVDither_Ordered);});
            if(!need("lav_dither_set_contract",set==S_OK||set==S_FALSE?S_OK:E_UNEXPECTED))return false;
            LAVDitherMode actual=LAVDither_Random;
            NEED("lav_dither_get","GetDitherMode_enum_wrapped_S_OK",(actual=video_settings->GetDitherMode(),S_OK));
            std::printf("MP_LAV_DITHER requested=0 actual=%d mode=ordered scope=transport_runtime_before_connect\n",int(actual));
            if(!need("lav_dither_ordered",actual==LAVDither_Ordered?S_OK:E_UNEXPECTED))return false;
        }
        for(int i=0;i<LAVOutPixFmt_NB;++i){
            NEED("lav_pixel_format","RGB32_only",video_settings->SetPixelFormat(LAVOutPixFmts(i),i==LAVOutPixFmt_RGB32));
            std::printf("MP_LAV_SETTING format=%d enabled=%d\n",i,i==LAVOutPixFmt_RGB32);
        }
        NEED("lav_get_sink","existing_IMediaStreamFilter",multi->GetFilter(&sink));
        CLSID sink_cls{};NEED("lav_sink_clsid","out",sink->GetClassID(&sink_cls));
        std::printf("MP_LAV_SINK clsid=%s\n",guid(sink_cls).c_str());
        NEED("lav_add_source","explicit_LAV_Source",graph->AddFilter(*source,L"Fixture LAV Source"));
        NEED("lav_add_decoder","explicit_LAV_Video",graph->AddFilter(*decoder,L"Fixture LAV Video"));
        NEED("lav_qi_file","IFileSourceFilter",(*source)->QueryInterface(IID_IFileSourceFilter,reinterpret_cast<void**>(&file)));
        NEED("lav_load",derived?"derived_Matroska_NULL_type":"original_ES_NULL_type",file->Load(media.c_str(),nullptr));
        if(!unique_pin(*source,PINDIR_OUTPUT,true,&source_out)||!unique_pin(*decoder,PINDIR_INPUT,false,&decoder_in))return false;
        NEED("lav_connect_compressed","ConnectDirect_source_decoder_NULL",graph->ConnectDirect(source_out,decoder_in,nullptr));
        if(!unique_pin(*decoder,PINDIR_OUTPUT,true,&decoder_out)||!unique_pin(sink,PINDIR_INPUT,false,&sink_in))return false;
        NEED("lav_connect_rgb","ConnectDirect_decoder_sink_NULL",graph->ConnectDirect(decoder_out,sink_in,nullptr));
        AM_MEDIA_TYPE mt{};NEED("lav_rgb_type","connected_type",decoder_out->ConnectionMediaType(&mt));
        const BITMAPINFOHEADER* bitmap=nullptr;
        if(mt.formattype==FORMAT_VideoInfo&&mt.cbFormat>=sizeof(VIDEOINFOHEADER))bitmap=&reinterpret_cast<VIDEOINFOHEADER*>(mt.pbFormat)->bmiHeader;
        if(mt.formattype==FORMAT_VideoInfo2&&mt.cbFormat>=sizeof(VIDEOINFOHEADER2))bitmap=&reinterpret_cast<VIDEOINFOHEADER2*>(mt.pbFormat)->bmiHeader;
        bool valid=mt.majortype==MEDIATYPE_Video&&mt.subtype==MEDIASUBTYPE_RGB32&&bitmap&&bitmap->biWidth==512&&(bitmap->biHeight==512||bitmap->biHeight==-512)&&bitmap->biBitCount==32&&bitmap->biCompression==BI_RGB;
        std::printf("MP_LAV_RGB width=%ld height=%ld bits=%u compression=%lu subtype=%s valid=%d\n",bitmap?bitmap->biWidth:0,bitmap?bitmap->biHeight:0,bitmap?bitmap->biBitCount:0,bitmap?bitmap->biCompression:0,guid(mt.subtype).c_str(),valid);
        free_type(mt);if(!need("lav_RGB32_contract",valid?S_OK:E_NOTIMPL))return false;
        NEED("lav_qi_seeking","graph_IMediaSeeking",graph->QueryInterface(IID_IMediaSeeking,reinterpret_cast<void**>(&seeking)));
        NEED("lav_qi_events","graph_IMediaEvent",graph->QueryInterface(IID_IMediaEvent,reinterpret_cast<void**>(&events)));
        DWORD caps=0;GUID format{};
        NEED("lav_seek_caps","out",seeking->GetCapabilities(&caps));
        NEED("lav_time_format","out",seeking->GetTimeFormat(&format));
        bool seekable=(caps&AM_SEEKING_CanSeekAbsolute)&&format==TIME_FORMAT_MEDIA_TIME;
        std::printf("MP_LAV_CAPS capabilities=%08lx format=%s absolute_media_time=%d\n",caps,guid(format).c_str(),seekable);
        return need("lav_seek_capability",seekable?S_OK:E_NOTIMPL);
#endif
    }
    bool acquire_terminal_allocator(bool transport=false){
        transport_allocator=transport;
        // After connection GetAllocator returns the allocator selected for this
        // input. Retain both public interface references through terminal Stop.
        NEED(transport?"lav_qi_transport_input":"lav_qi_terminal_input","connected_sink_IMemInputPin",sink_in?sink_in->QueryInterface(IID_IMemInputPin,reinterpret_cast<void**>(&terminal_input)):E_NOINTERFACE);
        if(!need("lav_terminal_input_present",terminal_input?S_OK:E_NOINTERFACE))return false;
        NEED(transport?"lav_get_transport_allocator":"lav_get_terminal_allocator","connected_selected_allocator",terminal_input->GetAllocator(&terminal_allocator));
        if(!need("lav_terminal_allocator_present",terminal_allocator?S_OK:E_NOINTERFACE))return false;
        ALLOCATOR_PROPERTIES properties{};
        NEED(transport?"lav_transport_properties":"lav_terminal_properties","selected_allocator_GetProperties",terminal_allocator->GetProperties(&properties));
        std::printf("MP_LAV_ALLOCATOR sink_input=%p mem_input=%p allocator=%p buffers=%ld bytes=%ld alignment=%ld prefix=%ld selected_after_connection=1 terminal_only=%d\n",static_cast<void*>(sink_in),static_cast<void*>(terminal_input),static_cast<void*>(terminal_allocator),properties.cBuffers,properties.cbBuffer,properties.cbAlign,properties.cbPrefix,int(!transport));
        return need("lav_terminal_properties_valid",properties.cBuffers>0&&properties.cbBuffer>0&&properties.cbAlign>0&&properties.cbPrefix>=0?S_OK:E_UNEXPECTED);
    }
    void decommit_terminal_allocator(){
        // Terminal intervention only: there is no resumed delivery, hence no
        // Commit. Outstanding samples remain valid until their owners release.
        HRESULT hr=call("lav_terminal_decommit","selected_allocator_terminal_no_recommit",[&]{return terminal_allocator?terminal_allocator->Decommit():E_NOINTERFACE;});
        if(need("lav_terminal_decommit",hr))std::printf("MP_LAV_DECOMMIT allocator=%p terminal_only=1 recommit=0\n",static_cast<void*>(terminal_allocator));
    }
    bool decommit_transport_allocator(bool cleanup=false){
        const char* name=cleanup?"lav_transport_cleanup_decommit":"lav_transport_decommit";
        NEED(name,"selected_allocator_filter_restart_owns_commit",terminal_allocator?terminal_allocator->Decommit():E_NOINTERFACE);
        std::printf("MP_LAV_TRANSPORT_DECOMMIT allocator=%p cleanup=%d manual_commit=0\n",static_cast<void*>(terminal_allocator),int(cleanup));
        return true;
    }
    bool check_events(){
        phase="provider-check";
        for(unsigned i=0;i<32;++i){
            long code=0;LONG_PTR a=0,b=0;
            HRESULT hr=call("lav_event_poll","GetEvent_timeout0",[&]{return events->GetEvent(&code,&a,&b,0);});
            if(hr==E_ABORT){std::printf("MP_LAV_EVENTS drained=1 count=%u\n",i);return true;}
            if(!need("lav_event_poll",hr))return false;
            std::printf("MP_LAV_EVENT code=%ld param1=%lld param2=%lld\n",code,(long long)a,(long long)b);
            NEED("lav_free_event","FreeEventParams",events->FreeEventParams(code,a,b));
            if(code==EC_ERRORABORT||code==EC_USERABORT||code==EC_STREAM_ERROR_STOPPED||code==EC_STREAM_ERROR_STILLPLAYING)return need("lav_async_error",E_FAIL);
        }
        return need("lav_event_queue_bound",E_UNEXPECTED);
    }
    void cleanup_interfaces(){
        release(transport_allocator?"lav_release_transport_allocator":"lav_release_terminal_allocator",terminal_allocator);
        release(transport_allocator?"lav_release_transport_input":"lav_release_terminal_input",terminal_input);
        release("lav_release_events",events);
        release("lav_release_seeking",seeking);release("lav_release_sink_in",sink_in);release("lav_release_decoder_out",decoder_out);
        release("lav_release_decoder_in",decoder_in);release("lav_release_source_out",source_out);release("lav_release_file",file);release("lav_release_sink",sink);
#ifdef X3_FIXTURE_LAV
        release("lav_release_video_settings",video_settings);release("lav_release_source_settings",splitter_settings);
#endif
    }
    void cleanup_context(){
        if(cookie){call("lav_deactivate_context","owner_STA",[&]{return win(DeactivateActCtx(0,cookie));});cookie=0;}
        if(context!=INVALID_HANDLE_VALUE){call("lav_release_context","after_graph_release",[&]{ReleaseActCtx(context);return S_OK;});context=INVALID_HANDLE_VALUE;}
    }
};
