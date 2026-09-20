// Public graph construction/settings extracted from qualified helper d092f2a3.
// Included only inside Engine; no fixture dependency, logging, or global state.
struct Graph {
    Engine& owner;
    explicit Graph(Engine& value):owner(value){}
    template<class F> HRESULT call(const char* name,const char* args,F fn){return owner.call(name,args,fn);}
    bool need(const char* name,HRESULT hr){return owner.need(name,hr);}
    template<class T> void release(const char* name,T*& value){owner.release(name,value);}

    HANDLE context=INVALID_HANDLE_VALUE;ULONG_PTR cookie=0;
    IMediaStreamFilter* sink{};IFileSourceFilter* file{};IMediaSeeking* seeking{};IMediaEvent* events{};
    bool transport_allocator=false;
    IMemInputPin* terminal_input{};IMemAllocator* terminal_allocator{};
    IPin *source_out{},*decoder_in{},*decoder_out{},*sink_in{};
    ILAVFSettings* splitter_settings{};ILAVVideoSettings* video_settings{};
    static void free_type(AM_MEDIA_TYPE& t){if(t.pbFormat)CoTaskMemFree(t.pbFormat);if(t.pUnk)t.pUnk->Release();t={};}
    bool unique_pin(IBaseFilter* filter,PIN_DIRECTION direction,bool video,IPin** result){
        IEnumPins* pins{};HRESULT hr=call("lav_enum_pins","public_EnumPins",[&]{return filter->EnumPins(&pins);});
        if(hr!=S_OK||!pins){release("lav_release_pin_enum",pins);return need("lav_enum_pins_exact",FAILED(hr)?hr:E_UNEXPECTED);}
        unsigned matches=0;bool complete=false,valid=true;
        for(unsigned i=0;i<32;++i){
            IPin* pin{};ULONG fetched=0;hr=call("lav_pin_next","one",[&]{return pins->Next(1,&pin,&fetched);});
            if(hr==S_FALSE){complete=fetched==0&&pin==nullptr;release("lav_release_pin_candidate",pin);break;}
            if(hr!=S_OK||fetched!=1||!pin){release("lav_release_pin_candidate",pin);valid=false;break;}
            PIN_DIRECTION actual{};hr=call("lav_pin_direction","out",[&]{return pin->QueryDirection(&actual);});
            valid=hr==S_OK&&(actual==PINDIR_INPUT||actual==PINDIR_OUTPUT);
            bool compatible=valid&&actual==direction;
            if(compatible&&video){
                compatible=false;IEnumMediaTypes* types{};
                hr=call("lav_enum_types","public_EnumMediaTypes",[&]{return pin->EnumMediaTypes(&types);});
                valid=hr==S_OK&&types;bool types_complete=false;
                if(valid)for(unsigned j=0;j<128;++j){
                    AM_MEDIA_TYPE* t{};ULONG got=0;hr=call("lav_type_next","one",[&]{return types->Next(1,&t,&got);});
                    if(hr==S_FALSE){types_complete=got==0&&t==nullptr;if(t){free_type(*t);CoTaskMemFree(t);}break;}
                    if(hr!=S_OK||got!=1||!t){if(t){free_type(*t);CoTaskMemFree(t);}valid=false;break;}
                    Metadata m{};m.kind=MetadataKind::type;copy_guid(m.guid[0],t->majortype);copy_guid(m.guid[1],t->subtype);copy_guid(m.guid[2],t->formattype);m.a=t->cbFormat;owner.meta(m);
                    compatible|=t->majortype==MEDIATYPE_Video;free_type(*t);CoTaskMemFree(t);
                }
                valid=valid&&types_complete;release("lav_release_types",types);
            }
            if(valid&&compatible){++matches;if(matches==1){*result=pin;pin=nullptr;}}
            release("lav_release_pin_candidate",pin);
            if(!valid)break;
        }
        release("lav_release_pin_enum",pins);
        if(!valid)return need("lav_pin_discovery_complete",FAILED(hr)?hr:E_UNEXPECTED);
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
        bool matches=!root.empty()&&owner.same_file_identity(root,manifest);
        if(SUCCEEDED(hr)){Metadata m{};m.kind=MetadataKind::context;m.text=root.c_str();m.expected=manifest.c_str();m.a=matches;owner.meta(m);}
        call("lav_release_current_context","GetCurrentActCtx_reference",[&]{ReleaseActCtx(current);return S_OK;});
        return need("lav_query_context",hr)&&need("lav_context_manifest_match",matches?S_OK:E_UNEXPECTED);
    }
    bool observe_class(IBaseFilter* filter,REFGUID expected,const char* role){
        CLSID actual{};
        NEED("lav_created_class","created_filter_GetClassID",filter->GetClassID(&actual));
        bool matches=actual==expected;
        Metadata m{};m.kind=MetadataKind::created_class;m.role=role;copy_guid(m.guid[0],actual);copy_guid(m.guid[1],expected);m.a=matches;owner.meta(m);
        return need("lav_created_class_match",matches?S_OK:E_UNEXPECTED);
    }
    bool module(const wchar_t* name,const std::wstring& directory,bool assembly=false){
        wchar_t path[32768]{};HMODULE m=GetModuleHandleW(name);
        NEED("lav_module_path","loaded_module",win(m&&GetModuleFileNameW(m,path,32768)>0));
        std::wstring expected=directory+L"\\"+name;
        bool exact=owner.same_file_identity(expected,path);
        Metadata mdata{};mdata.kind=MetadataKind::module;mdata.text=path;mdata.expected=expected.c_str();mdata.a=exact;mdata.b=assembly;owner.meta(mdata);
        return need("lav_local_module",exact?S_OK:E_UNEXPECTED);
    }
    bool observe_assembly(const std::wstring& manifest){
        std::wstring dir=manifest.substr(0,manifest.find_last_of(L"\\/"));
        for(const wchar_t* name:{L"LAVSplitter.ax",L"LAVVideo.ax",L"avcodec-lav-62.dll",L"avformat-lav-62.dll",L"avutil-lav-60.dll",L"avfilter-lav-11.dll",L"swresample-lav-6.dll",L"swscale-lav-9.dll",L"libbluray.dll"})
            if(!module(name,dir,true))return false;
        return true;
    }
    bool construct(const std::wstring& manifest,const std::wstring& media,IAMMultiMediaStream* multi,IGraphBuilder* graph,IBaseFilter** source,IBaseFilter** decoder,bool transport,bool derived){
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
            Metadata m{};m.kind=MetadataKind::dither;m.a=int(actual);owner.meta(m);
            if(!need("lav_dither_ordered",actual==LAVDither_Ordered?S_OK:E_UNEXPECTED))return false;
        }
        for(int i=0;i<LAVOutPixFmt_NB;++i){
            NEED("lav_pixel_format","RGB32_only",video_settings->SetPixelFormat(LAVOutPixFmts(i),i==LAVOutPixFmt_RGB32));
            Metadata m{};m.kind=MetadataKind::pixel_format;m.a=i;m.b=i==LAVOutPixFmt_RGB32;owner.meta(m);
        }
        NEED("lav_get_sink","existing_IMediaStreamFilter",multi->GetFilter(&sink));
        CLSID sink_cls{};NEED("lav_sink_clsid","out",sink->GetClassID(&sink_cls));
        Metadata sink_data{};sink_data.kind=MetadataKind::sink;copy_guid(sink_data.guid[0],sink_cls);owner.meta(sink_data);
        NEED("lav_add_source","explicit_LAV_Source",graph->AddFilter(*source,L"Owned LAV Source"));
        NEED("lav_add_decoder","explicit_LAV_Video",graph->AddFilter(*decoder,L"Owned LAV Video"));
        NEED("lav_qi_file","IFileSourceFilter",(*source)->QueryInterface(IID_IFileSourceFilter,reinterpret_cast<void**>(&file)));
        owner.facts.load=true;
        HRESULT loaded=call("lav_load",derived?"derived_Matroska_NULL_type":"original_ES_NULL_type",[&]{return file->Load(media.c_str(),nullptr);});
        owner.facts.load_hr=loaded;if(!need("lav_load",loaded))return false;
        if(!unique_pin(*source,PINDIR_OUTPUT,true,&source_out)||!unique_pin(*decoder,PINDIR_INPUT,false,&decoder_in))return false;
        owner.facts.connect=true;
        NEED("lav_connect_compressed","ConnectDirect_source_decoder_NULL",graph->ConnectDirect(source_out,decoder_in,nullptr));
        if(!unique_pin(*decoder,PINDIR_OUTPUT,true,&decoder_out)||!unique_pin(sink,PINDIR_INPUT,false,&sink_in))return false;
        owner.facts.connect=true;
        NEED("lav_connect_rgb","ConnectDirect_decoder_sink_NULL",graph->ConnectDirect(decoder_out,sink_in,nullptr));
        AM_MEDIA_TYPE mt{};NEED("lav_rgb_type","connected_type",decoder_out->ConnectionMediaType(&mt));
        const BITMAPINFOHEADER* bitmap=nullptr;
        if(mt.formattype==FORMAT_VideoInfo&&mt.cbFormat>=sizeof(VIDEOINFOHEADER))bitmap=&reinterpret_cast<VIDEOINFOHEADER*>(mt.pbFormat)->bmiHeader;
        if(mt.formattype==FORMAT_VideoInfo2&&mt.cbFormat>=sizeof(VIDEOINFOHEADER2))bitmap=&reinterpret_cast<VIDEOINFOHEADER2*>(mt.pbFormat)->bmiHeader;
        bool valid=mt.majortype==MEDIATYPE_Video&&mt.subtype==MEDIASUBTYPE_RGB32&&bitmap&&bitmap->biWidth==512&&(bitmap->biHeight==512||bitmap->biHeight==-512)&&bitmap->biBitCount==32&&bitmap->biCompression==BI_RGB;
        Metadata rgb{};rgb.kind=MetadataKind::rgb;rgb.a=bitmap?bitmap->biWidth:0;rgb.b=bitmap?bitmap->biHeight:0;rgb.c=bitmap?bitmap->biBitCount:0;rgb.d=bitmap?bitmap->biCompression:0;rgb.e=valid;copy_guid(rgb.guid[0],mt.subtype);owner.meta(rgb);
        free_type(mt);if(!need("lav_RGB32_contract",valid?S_OK:E_NOTIMPL))return false;
        NEED("lav_qi_seeking","graph_IMediaSeeking",graph->QueryInterface(IID_IMediaSeeking,reinterpret_cast<void**>(&seeking)));
        NEED("lav_qi_events","graph_IMediaEvent",graph->QueryInterface(IID_IMediaEvent,reinterpret_cast<void**>(&events)));
        DWORD caps=0;GUID format{};
        NEED("lav_seek_caps","out",seeking->GetCapabilities(&caps));
        NEED("lav_time_format","out",seeking->GetTimeFormat(&format));
        bool seekable=(caps&AM_SEEKING_CanSeekAbsolute)&&format==TIME_FORMAT_MEDIA_TIME;
        Metadata capabilities{};capabilities.kind=MetadataKind::seek_caps;capabilities.a=caps;capabilities.b=seekable;copy_guid(capabilities.guid[0],format);owner.meta(capabilities);
        return need("lav_seek_capability",seekable?S_OK:E_NOTIMPL);
    }
    bool acquire_terminal_allocator(bool transport=false){
        transport_allocator=transport;
        // After connection GetAllocator returns the allocator selected for this
        // input. Retain both public interface references through terminal Stop.
        NEED(transport?"lav_qi_transport_input":"lav_qi_terminal_input","connected_sink_IMemInputPin",sink_in?sink_in->QueryInterface(IID_IMemInputPin,reinterpret_cast<void**>(&terminal_input)):E_NOINTERFACE);
        if(!need("lav_terminal_input_present",terminal_input?S_OK:E_NOINTERFACE))return false;
        owner.facts.allocator=true;
        NEED(transport?"lav_get_transport_allocator":"lav_get_terminal_allocator","connected_selected_allocator",terminal_input->GetAllocator(&terminal_allocator));
        if(!need("lav_terminal_allocator_present",terminal_allocator?S_OK:E_NOINTERFACE))return false;
        ALLOCATOR_PROPERTIES properties{};
        NEED(transport?"lav_transport_properties":"lav_terminal_properties","selected_allocator_GetProperties",terminal_allocator->GetProperties(&properties));
        Metadata m{};m.kind=MetadataKind::allocator;m.a=reinterpret_cast<uintptr_t>(sink_in);m.b=reinterpret_cast<uintptr_t>(terminal_input);m.c=reinterpret_cast<uintptr_t>(terminal_allocator);m.d=properties.cBuffers;m.e=properties.cbBuffer;m.f=properties.cbAlign;m.g=properties.cbPrefix;owner.meta(m);
        return need("lav_terminal_properties_valid",properties.cBuffers>0&&properties.cbBuffer>0&&properties.cbAlign>0&&properties.cbPrefix>=0?S_OK:E_UNEXPECTED);
    }
    bool decommit_transport_allocator(bool cleanup=false){
        const char* name=cleanup?"lav_transport_cleanup_decommit":"lav_transport_decommit";
        HRESULT hr=call(name,"selected_allocator_filter_restart_owns_commit",[&]{return terminal_allocator?terminal_allocator->Decommit():E_NOINTERFACE;});
        if(!need("allocator_decommit_exact",hr==S_OK?S_OK:E_UNEXPECTED))return false;
        Metadata m{};m.kind=MetadataKind::decommit;m.a=reinterpret_cast<uintptr_t>(terminal_allocator);m.b=cleanup;owner.meta(m);
        return true;
    }
    void cleanup_interfaces(){
        release(transport_allocator?"lav_release_transport_allocator":"lav_release_terminal_allocator",terminal_allocator);
        release(transport_allocator?"lav_release_transport_input":"lav_release_terminal_input",terminal_input);
        release("lav_release_events",events);
        release("lav_release_seeking",seeking);release("lav_release_sink_in",sink_in);release("lav_release_decoder_out",decoder_out);
        release("lav_release_decoder_in",decoder_in);release("lav_release_source_out",source_out);release("lav_release_file",file);release("lav_release_sink",sink);
        release("lav_release_video_settings",video_settings);release("lav_release_source_settings",splitter_settings);
    }
    void cleanup_context(){
        if(cookie){call("lav_deactivate_context","owner_STA",[&]{return win(DeactivateActCtx(0,cookie));});cookie=0;}
        if(context!=INVALID_HANDLE_VALUE){call("lav_release_context","after_graph_release",[&]{ReleaseActCtx(context);return S_OK;});context=INVALID_HANDLE_VALUE;}
    }
};
