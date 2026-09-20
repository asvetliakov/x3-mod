// Detached timing harness; no intercepted methods or active interval queries.
#include <map>
namespace { namespace fog_spatial_timing {
struct Case {std::string name,family,directory,profile_id,expected_scene,expected_st;};
LONGLONG ticks(){LARGE_INTEGER value{};if(!QueryPerformanceCounter(&value))throw std::runtime_error("QPC");return value.QuadPart;}
HRESULT fence(IDirect3DQuery9* query){
    HRESULT hr=query->Issue(D3DISSUE_END);if(hr!=S_OK)return FAILED(hr)?hr:E_FAIL;
    const DWORD start=GetTickCount();
    do{hr=query->GetData(nullptr,0,D3DGETDATA_FLUSH);if(hr!=S_FALSE)return hr==S_OK||FAILED(hr)?hr:E_FAIL;if(GetTickCount()-start>=5000)return HRESULT_FROM_WIN32(WAIT_TIMEOUT);Sleep(0);}while(true);
}
struct View {
    Case source;std::vector<float> constants;UINT w=0,h=0;std::vector<std::uint16_t> original,baseline,baseline_st;
    Com<IDirect3DTexture9> depth,pristine,scene,final_st;Com<IDirect3DSurface9> ds,ps,ss,final_st_surface;
    View(IDirect3DDevice9* d,const Case& item):source(item){
        const auto c=read<float>(item.directory+"/constants.f32");if(c.size()!=32)throw std::runtime_error("timing constants size");
        for(float v:c)if(!std::isfinite(v))throw std::runtime_error("timing constants finite");
        if(!((c[4]==1280&&c[5]==768)||(c[4]==1920&&c[5]==1080)))throw std::runtime_error("timing dimensions");
        constants=c;w=UINT(c[4]);h=UINT(c[5]);
        const auto data=read<float>(item.directory+"/depth.rgba32f");original=read<std::uint16_t>(item.directory+"/scene.rgba16f");
        if(data.size()!=size_t(w)*h*4||original.size()!=data.size())throw std::runtime_error("timing input size");
        for(size_t i=0;i<data.size();++i){if(!std::isfinite(data[i]))throw std::runtime_error("timing depth finite");if(i%4==0&&data[i]>=0&&data[i]<=1&&data[i+2]<=0)throw std::runtime_error("timing positive linear depth");}
        for(auto v:original)if((v&0x7c00)==0x7c00)throw std::runtime_error("timing scene finite");
        upload(d,w,h,D3DFMT_A32B32G32R32F,16,data.data(),&depth.p,true);check(depth->GetSurfaceLevel(0,&ds.p),"timing depth surface");
        upload(d,w,h,D3DFMT_A16B16G16R16F,8,original.data(),&pristine.p,true);check(pristine->GetSurfaceLevel(0,&ps.p),"timing pristine surface");
        check(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&scene.p,nullptr),"timing writable scene");check(scene->GetSurfaceLevel(0,&ss.p),"timing scene surface");
        check(d->CreateTexture((w+1)/2,(h+1)/2,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&final_st.p,nullptr),"timing final ST witness");check(final_st->GetSurfaceLevel(0,&final_st_surface.p),"timing final ST surface");
    }
    FogFrame frame(FogPass& pass)const{auto f=fog_spatial_state::make_frame(pass,depth.p,ss.p,constants);f.caller_scene_open=true;return f;}
};
struct Caller {
    IDirect3DDevice9* d;D3DCAPS9 caps{};Com<IDirect3DVertexBuffer9> buffer;Com<IDirect3DSurface9> depth,backbuffer;
    Caller(IDirect3DDevice9* device,UINT w,UINT h):d(device){
        check(d->GetDeviceCaps(&caps),"timing caps");if(caps.NumSimultaneousRTs<3||!(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS))throw std::runtime_error("timing caller mixed MRT capability");
        check(d->GetRenderTarget(0,&backbuffer.p),"timing original target");
        check(d->CreateVertexBuffer(128,0,0,D3DPOOL_DEFAULT,&buffer.p,nullptr),"timing caller streams");
        check(d->CreateDepthStencilSurface(w,h,D3DFMT_D16_LOCKABLE,D3DMULTISAMPLE_NONE,0,FALSE,&depth.p,nullptr),"timing caller depth");
    }
    void bind(const View& view){
        check(d->SetRenderTarget(0,view.ss.p),"timing caller rt0");check(d->SetRenderTarget(1,view.ps.p),"timing caller rt1");check(d->SetRenderTarget(2,view.ds.p),"timing caller rt2");check(d->SetDepthStencilSurface(depth.p),"timing caller depth bind");
        for(UINT i=0;i<caps.MaxStreams;++i){check(d->SetStreamSource(i,buffer.p,4,16),"timing caller stream");check(d->SetStreamSourceFreq(i,1),"timing caller frequency");}
        if(caps.MaxStreams>1){check(d->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|3),"timing caller indexed");check(d->SetStreamSourceFreq(1,D3DSTREAMSOURCE_INSTANCEDATA|1),"timing caller instanced");}
        check(d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE),"timing caller fvf");check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"timing caller blend");check(d->SetRenderState(D3DRS_COLORWRITEENABLE,3),"timing caller mask");
    }
    void reset_scene(const View& view){
        check(d->SetDepthStencilSurface(nullptr),"timing reset depth");for(UINT i=1;i<caps.NumSimultaneousRTs;++i)check(d->SetRenderTarget(i,nullptr),"timing reset aux");
        check(d->StretchRect(view.ps.p,nullptr,view.ss.p,nullptr,D3DTEXF_NONE),"timing resident scene reset");bind(view);
    }
    void unbind(){
        for(UINT i=0;i<16;++i)check(d->SetTexture(i,nullptr),"timing unbind texture");
        for(UINT i=0;i<4;++i)check(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+i,nullptr),"timing unbind vertex texture");
        check(d->SetDepthStencilSurface(nullptr),"timing unbind depth");for(UINT i=1;i<caps.NumSimultaneousRTs;++i)check(d->SetRenderTarget(i,nullptr),"timing unbind aux");check(d->SetRenderTarget(0,backbuffer.p),"timing unbind target");
        for(UINT i=0;i<caps.MaxStreams;++i){check(d->SetStreamSource(i,nullptr,0,0),"timing unbind stream");check(d->SetStreamSourceFreq(i,1),"timing unbind frequency");}
    }
};
void verify_output(const View& view,const std::vector<std::uint16_t>& out,const std::vector<std::uint16_t>& st){
    if(out.size()!=view.original.size())throw std::runtime_error("timing output size");
    for(size_t i=0;i<out.size();++i){if((out[i]&0x7c00)==0x7c00)throw std::runtime_error("timing finite composite");if(i%4==3&&out[i]!=view.original[i])throw std::runtime_error("timing alpha identity");}
    if(st.size()!=size_t((view.w+1)/2)*((view.h+1)/2)*4)throw std::runtime_error("timing ST size");
    for(size_t i=0;i<st.size();++i){if((st[i]&0x7c00)==0x7c00)throw std::runtime_error("timing finite ST");if(i%4==3&&((st[i]&0x8000)||st[i]>0x3c00))throw std::runtime_error("timing T range");}
}
int qualify(IDirect3DDevice9* d,D3DFORMAT format,const std::string& casefile,const std::string& output){
    LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)throw std::runtime_error("QPC frequency");
    std::printf("TIMING_CLOCK frequency=%lld timestamps=not_collected_no_active_query_contract\n",frequency.QuadPart);
    std::ifstream input(casefile);std::vector<Case> cases;Case row;
    while(std::getline(input,row.name)&&std::getline(input,row.family)&&std::getline(input,row.directory)&&std::getline(input,row.profile_id)&&std::getline(input,row.expected_scene)&&std::getline(input,row.expected_st))cases.push_back(row);
    if(cases.size()!=8)throw std::runtime_error("timing case count");
    for(unsigned profile=0;profile<2;++profile){
        const LONGLONG prepare_start=ticks();const UINT w=profile?1920:1280,h=profile?1080:768;
        Caller caller(d,w,h);Com<IDirect3DQuery9> query;check(d->CreateQuery(D3DQUERYTYPE_EVENT,&query.p),"timing EVENT capability");
        std::vector<std::unique_ptr<View>> views;std::map<std::string,std::unique_ptr<FogPass>> passes;
        for(unsigned i=0;i<4;++i){auto view=std::make_unique<View>(d,cases[profile*4+i]);if(view->w!=w||view->h!=h)throw std::runtime_error("timing profile order");
            if(!passes.count(view->source.family)){
                auto pass=std::make_unique<FogPass>();D3DCAPS9 caps{};check(d->GetDeviceCaps(&caps),"timing attach caps");
                check(pass->attach(d,*reinterpret_cast<void***>(d),caps,format),"timing attach");
                const auto family=static_cast<fog_field::Profile>(std::stoul(view->source.profile_id));
                const auto* info=fog_field::profile_info(family);if(!info||view->constants[11]!=info->base_sigma)throw std::runtime_error("timing family sigma");
                const LONGLONG field_start=ticks();check(pass->prepare_field(GetModuleHandleA(nullptr),family),"timing embedded field decode/upload");const LONGLONG field_end=ticks();
                check(pass->prepare_targets(w,h),"timing targets");const LONGLONG target_end=ticks();
                std::printf("TIMING_FIELD width=%u height=%u family=%s field_ticks=%lld target_ticks=%lld cpu_atlas_bytes=%llu\n",w,h,view->source.family.c_str(),field_end-field_start,target_end-field_end,static_cast<unsigned long long>(pass->fixture_cpu_bytes()));
                passes.emplace(view->source.family,std::move(pass));
            }
            views.push_back(std::move(view));
        }
        check(fence(query.p),"timing upload fence");const LONGLONG prepare_end=ticks();
        // Counts describe explicitly created bytes, not driver allocation overhead.
        const unsigned long long atlas_bytes=17846400ull*passes.size();
        const unsigned long long target_bytes=passes.size()*(8ull*w*h+8ull*((w+1)/2)*((h+1)/2));
        const unsigned long long input_bytes=4ull*w*h*32+2ull*w*h+128;
        std::printf("TIMING_PREP width=%u height=%u ticks=%lld families=%u atlas_bytes=%llu input_bytes=%llu pass_target_bytes=%llu witness_bytes=%llu streams=%lu counted_calls_exclude=resource_validation_and_COM_Releases\n",w,h,prepare_end-prepare_start,unsigned(passes.size()),atlas_bytes,input_bytes,target_bytes,4ull*8*((w+1)/2)*((h+1)/2),(unsigned long)caller.caps.MaxStreams);
        for(auto& view:views){
            auto& pass=*passes.at(view->source.family);caller.reset_scene(*view);check(fence(query.p),"timing baseline prefence");const fog_spatial_state::Snapshot before(d,caller.caps);FogResult result;
            check(d->BeginScene(),"timing baseline caller begin");check(pass.execute(view->frame(pass),&result),"timing baseline execute");check(fence(query.p),"timing baseline completion");check(d->EndScene(),"timing baseline caller end");
            require((result.applied&&result.caller_state_restored&&result.scene_known&&result.scene_open&&!result.route_poisoned)&&before==fog_spatial_state::Snapshot(d,caller.caps),(view->source.name+"_baseline_state").c_str());
            view->baseline=readback_words(d,view->ss.p);view->baseline_st=readback_words(d,pass.fixture_st());verify_output(*view,view->baseline,view->baseline_st);
            if(view->source.expected_scene!="-")require(view->baseline==read<std::uint16_t>(view->source.expected_scene)&&view->baseline_st==read<std::uint16_t>(view->source.expected_st),(view->source.name+"_accepted_bytes").c_str());
            write_words(view->baseline,output+"/"+view->source.name+".baseline.composite.rgba16f");write_words(view->baseline_st,output+"/"+view->source.name+".baseline.st.rgba16f");
        }
        std::map<std::string,std::pair<unsigned,unsigned>> owned;
        // Warmed reference baseline is taken only after all16 warm iterations.
        auto ref_count=[](IUnknown* object){object->AddRef();return object->Release();};
        ULONG device_refs=0,buffer_refs=0,depth_refs=0;
        struct Sample{LONGLONG start=0,submitted=0,finished=0;HRESULT operation=S_FALSE,fence=S_FALSE,caller_end=S_FALSE;FogResult result;};
        Sample samples[80]{}; // fixed storage allocated before the loop

        for(unsigned iteration=0;iteration<80;++iteration){
            auto& view=*views[iteration%4];auto& pass=*passes.at(view.source.family);caller.reset_scene(view);check(fence(query.p),"timing sample prefence");
            if(iteration==16){
                for(auto& entry:passes)owned.emplace(entry.first,std::make_pair(entry.second->allocations(),entry.second->references()));
                device_refs=ref_count(d);buffer_refs=ref_count(caller.buffer.p);depth_refs=ref_count(caller.depth.p);
            }
            FogResult result;const FogFrame frame=view.frame(pass);check(d->BeginScene(),"timing sample caller begin");
            const LONGLONG start=ticks();const HRESULT operation=pass.execute(frame,&result);const LONGLONG submitted=ticks();const HRESULT completed=SUCCEEDED(operation)&&(result.applied&&result.caller_state_restored&&result.scene_known&&result.scene_open&&!result.route_poisoned)?fence(query.p):(FAILED(operation)?operation:E_FAIL);const LONGLONG finish=ticks();
            // Caller EndScene, formatting, checks and readback follow the completion clock.
            const bool lost_device=completed==D3DERR_DEVICELOST||completed==D3DERR_DEVICENOTRESET;
            const HRESULT caller_end=!lost_device&&result.scene_known&&result.scene_open?d->EndScene():D3DERR_INVALIDCALL;
            samples[iteration]={start,submitted,finish,operation,completed,caller_end,result};
            std::printf("TIMING_SAMPLE width=%u height=%u phase=%s index=%u view=%s start=%lld submitted=%lld completed=%lld hr=%08lx restore=%08lx fence=%08lx valid=%u counted_calls=%u\n",w,h,iteration<16?"warm":"measured",iteration<16?iteration:iteration-16,view.source.name.c_str(),start,submitted,finish,(unsigned long)operation,(unsigned long)result.restore,(unsigned long)completed,unsigned((result.applied&&result.caller_state_restored&&result.scene_known&&result.scene_open&&!result.route_poisoned)),result.device_calls);
            check(operation,"timing sample operation");check(completed,"timing sample fence");check(caller_end,"timing caller end");if(!(result.applied&&result.caller_state_restored&&result.scene_known&&result.scene_open&&!result.route_poisoned)||result.restore!=S_OK||submitted<=start||finish<submitted)throw std::runtime_error("timing invalid sample");
            // Preserve the last measured ST outside the clock interval. The
            // next prefence drains this copy; CPU readback waits until all64 end.
            if(iteration>=76)check(d->StretchRect(pass.fixture_st(),nullptr,view.final_st_surface.p,nullptr,D3DTEXF_NONE),"timing final ST copy");
        }
        require(samples[79].operation==S_OK&&samples[79].fence==S_OK,(std::to_string(w)+"_all_samples_recorded").c_str());
        check(fence(query.p),"timing witness copy completion");
        for(auto& view:views){const auto final=readback_words(d,view->ss.p),final_st=readback_words(d,view->final_st_surface.p);
            require(final==view->baseline&&final_st==view->baseline_st,(view->source.name+"_final_bytes").c_str());
            write_words(final,output+"/"+view->source.name+".final.composite.rgba16f");write_words(final_st,output+"/"+view->source.name+".final.st.rgba16f");}
        require(device_refs==ref_count(d)&&buffer_refs==ref_count(caller.buffer.p)&&depth_refs==ref_count(caller.depth.p),(std::to_string(w)+"_stable_caller_refs").c_str());
        for(auto& entry:passes)require(owned.at(entry.first)==std::make_pair(entry.second->allocations(),entry.second->references()),(std::to_string(w)+"_"+entry.first+"_stable_owned").c_str());
        for(auto& entry:passes)entry.second->detach();
        caller.unbind();
    }
    std::printf("RESULT checkpoint=timing profiles=2 warm_per_profile=16 samples_per_profile=64 PASS\n");return 0;
}
}} // anonymous namespace / fog_spatial_timing
