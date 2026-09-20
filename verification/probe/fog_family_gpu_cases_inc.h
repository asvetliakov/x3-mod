// Fixture-only bounded family switch witness. No production API changes.
namespace {
void qualify_families(IDirect3D9* api,IDirect3DDevice9* d,D3DFORMAT format,const D3DCAPS9& caps,
                      const D3DPRESENT_PARAMETERS& pp,const std::string& cases,const std::string& output){
    Inputs input(cases);Hooks hooks(d,api);FogPass pass;
    check(pass.attach(d,hooks.methods,caps,format),"family attach");
    auto scene=std::make_unique<Scene>(d,input,caps,std::vector<DWORD>(std::begin(march_words),std::end(march_words)));
    Com<IDirect3DSurface9> back;check(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back.p),"family backbuffer");
    struct Pixels {std::vector<std::uint16_t> st,composite;};
    std::array<Pixels,3> originals;
    std::uint64_t generation=0;FogFrame previous{};unsigned run=0;
    auto execute=[&](unsigned id,bool revisit,unsigned saved){
        auto& s=*scene;const auto profile=static_cast<fog_field::Profile>(id);
        const auto label="family_"+std::to_string(run);
        hooks.clear();
        check(cpu_method((label+"_cpu_prepare").c_str(),[&]{return pass.prepare_field(GetModuleHandleA(nullptr),profile);}),"family prepare");
        require(pass.field_profile()==profile&&pass.field_generation()>generation&&hooks.calls[23]==2&&hooks.calls[31]==1,
                (label+"_switch").c_str());
        generation=pass.field_generation();check(pass.prepare_targets(input.w,input.h),"family targets");
        require(pass.fixture_cpu_bytes()==17846400&&pass.references()==10,(label+"_one_atlas").c_str());
        hooks.clear();const auto allocations=pass.allocations();
        check(pass.prepare_field(nullptr,profile),"family warm reuse");
        require(hooks.writes()==0&&pass.allocations()==allocations&&pass.field_generation()==generation,(label+"_warm_reuse").c_str());
        if(run){FogResult refused;hooks.clear();require(FAILED(pass.execute(previous,&refused))&&hooks.writes()==0,(label+"_stale_refused").c_str());}
        s.refill();s.hostile();const Protected before(d,s);auto frame=s.frame(pass);FogResult result;hooks.clear();
        check(cpu_method((label+"_cpu_execute").c_str(),[&]{return pass.execute(frame,&result);}),"family execute");
        require(result.applied&&result.caller_state_restored&&result.scene_known&&!result.scene_open&&!result.route_poisoned&&before.same(d,s),
                (label+"_transaction").c_str());
        Pixels pixels{readback_words(d,pass.fixture_st()),readback_words(d,s.s0.p)};
        unsigned nonempty=0,changed=0;bool alpha=true;
        for(size_t i=0;i<pixels.st.size();i+=4)nonempty+=pixels.st[i]!=0||pixels.st[i+1]!=0||pixels.st[i+2]!=0||pixels.st[i+3]!=0x3c00;
        for(size_t i=0;i<pixels.composite.size();i+=4){changed+=pixels.composite[i]!=input.scene[i]||pixels.composite[i+1]!=input.scene[i+1]||pixels.composite[i+2]!=input.scene[i+2];alpha&=pixels.composite[i+3]==input.scene[i+3];}
        require(nonempty>=64&&changed>=64&&alpha,(label+"_nonempty_alpha").c_str());
        if(revisit)require(pixels.st==originals[saved].st&&pixels.composite==originals[saved].composite,(label+"_revisit_exact").c_str());
        else if(id==1||id==2||id==14)originals[id==14?2:id-1]=pixels;
        const auto prefix=output+"/family-"+std::to_string(run);
        write_words(pixels.st,prefix+".st.rgba16f");write_words(pixels.composite,prefix+".composite.rgba16f");
        std::printf("FAMILY_GPU run=%u profile=%u generation=%llu nonempty=%u changed=%u cpu_bytes=%zu refs=%u\n",run,id,static_cast<unsigned long long>(generation),nonempty,changed,pass.fixture_cpu_bytes(),pass.references());
        previous=frame;++run;
    };
    for(const auto& family:fog_field::family_profiles)execute(static_cast<unsigned>(family.profile),false,0);
    execute(1,true,0);execute(2,true,1);execute(14,true,2);
    hooks.clear();FogResult refused;
    require(pass.prepare_field(nullptr,static_cast<fog_field::Profile>(0xffffffffu))==E_INVALIDARG&&pass.field_profile()==fog_field::Profile::None&&
            !pass.resources_ready(input.w,input.h)&&FAILED(pass.execute(previous,&refused))&&hooks.writes()==0,"family_invalid_id_disarmed");
    check(pass.prepare_field(nullptr,fog_field::Profile::Khaakhive),"family cached rearm");
    require(pass.field_generation()>generation&&hooks.writes()==0,"family_invalid_rearm_cached");generation=pass.field_generation();
    pass.before_reset();require(pass.references()==4&&pass.fixture_cpu_bytes()==17846400,"family_reset_retains_one_cpu");
    scene->unbind(back.p);scene.reset();back.reset();
    auto attempt=pp;const auto reset=d->Reset(&attempt);pass.after_reset(reset);require(SUCCEEDED(reset),"family_real_reset");
    check(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back.p),"family reset backbuffer");
    scene=std::make_unique<Scene>(d,input,caps,std::vector<DWORD>(std::begin(march_words),std::end(march_words)));
    hooks.clear();check(pass.prepare_field(nullptr,fog_field::Profile::Khaakhive),"family retained reupload");
    require(hooks.calls[23]==2&&hooks.calls[31]==1&&pass.field_generation()>generation&&pass.field_profile()==fog_field::Profile::Khaakhive,"family_reset_cached_upload");
    check(pass.prepare_targets(input.w,input.h),"family reset targets");
    scene->refill();scene->hostile();const Protected before(d,*scene);FogResult result;
    check(pass.execute(scene->frame(pass),&result),"family reset execute");
    const auto st=readback_words(d,pass.fixture_st()),composite=readback_words(d,scene->s0.p);
    require(result.applied&&result.caller_state_restored&&result.scene_known&&!result.scene_open&&!result.route_poisoned&&before.same(d,*scene)&&
            st==originals[2].st&&composite==originals[2].composite&&pass.fixture_cpu_bytes()==17846400&&pass.references()==10,"family_reset_exact_pixels_state");
    write_words(st,output+"/family-17.st.rgba16f");write_words(composite,output+"/family-17.composite.rgba16f");
    pass.detach();require(pass.references()==0&&pass.fixture_cpu_bytes()==0,"family_detach_empty");
    scene->unbind(back.p);scene.reset();back.reset();
    std::printf("FAMILY_SCOPE profiles=14 switches=17 executes=18 real_reset=1 view=64x48 shafts=unshadowed\n");
}
} // namespace
